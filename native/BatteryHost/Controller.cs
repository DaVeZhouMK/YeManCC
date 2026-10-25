using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
namespace YeManBatteryHost;
internal sealed record Preference(int Schema,string IdentityKey,string RouteId,int Target,bool Managed);
internal sealed record Recovery(int Schema,string IdentityKey,string RouteId,int? OriginalRaw,int? OwnedRaw,bool Software);
internal sealed record Snapshot(int Schema,bool Ok,bool Supported,bool BatteryPresent,string Model,string Family,string Kind,int[] Choices,int? CurrentLimit,int? RequestedLimit,bool Managed,string Verification,string Fence,string Reason)
{
    public string? CapabilityId {get;init;}
    public string? CapabilityRuleId {get;init;}
    public string? CapabilityOrigin {get;init;}
    public bool? CapabilityDeclaredByFrozenHc {get;init;}
    public string? CapabilityCatalogSha256 {get;init;}
}
internal class Store(string directory)
{
    public string DirectoryPath=>directory;
    public virtual T? Load<T>(string name)
    {
        var p=Path.Combine(directory,name);if(!File.Exists(p))return default;
        if(new FileInfo(p).Length>16384)throw new IOException("电池设置文件过大");
        return JsonSerializer.Deserialize<T>(File.ReadAllText(p),Policy.Json)??throw new IOException("电池设置文件损坏");
    }
    public virtual void Save<T>(string name,T value)
    {
        Directory.CreateDirectory(directory);var path=Path.Combine(directory,name);var temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";
        try{using(var f=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)){var bytes=JsonSerializer.SerializeToUtf8Bytes(value,Policy.Json);f.Write(bytes);f.Flush(true);}File.Move(temp,path,true);}
        finally{if(File.Exists(temp))File.Delete(temp);}
    }
    public virtual void Clear(string name){var p=Path.Combine(directory,name);if(File.Exists(p))File.Delete(p);}
}
internal sealed class Controller
{
    public const string SettingsFile="battery-charge.settings.json",RecoveryFile="battery-charge.recovery.json";
    readonly object gate=new();
    readonly Identity identity;
    readonly Route? route;
    readonly YeManFanBackend.Capabilities.BatteryCapabilityBinding<Route>? capability;
    readonly Func<IChargeProvider> open;
    readonly Func<(bool Present,bool Ac,int? Percent)> power;
    readonly Store store;
    readonly string session=Guid.NewGuid().ToString("N");
    Preference? preference;
    Recovery? recovery;
    int revision;
    bool active,conflict;
    bool invalidStore;
    volatile bool suspended;
    string warning="";
    string verification="unknown";
    int? expectedRaw;
    public Controller(Identity identity,Store store,Func<Route,IChargeProvider> factory,Func<(bool Present,bool Ac,int? Percent)>? power=null)
    {
        this.identity=identity;this.store=store;capability=Policy.ResolveCapability(identity);route=capability?.Consumer;open=()=>factory(route??throw new IOException("未知机型"));this.power=power??WindowsFacts.Power;
        try
        {
            preference=store.Load<Preference>(SettingsFile);recovery=store.Load<Recovery>(RecoveryFile);
            if(preference is not null&&(preference.Schema!=1||route is null||preference.IdentityKey!=identity.Key||preference.RouteId!=route.RouteId||!Policy.ValueAllowed(route,preference.Target)))
            {warning="设备或固件已变化，未自动应用旧设置";preference=null;}
            if(recovery is not null&&(recovery.Schema!=1||route is null||recovery.IdentityKey!=identity.Key||recovery.RouteId!=route.RouteId))
            {warning="旧恢复记录与设备不匹配，请先核对厂商充电设置";conflict=true;invalidStore=true;}
            else if(recovery is not null)
            {
                if(recovery.Software != route!.Software || (recovery.Software && (recovery.OriginalRaw != ResumeRaw || (recovery.OwnedRaw != ResumeRaw && recovery.OwnedRaw != PauseRaw))))
                { warning="充电恢复记录内容异常，请先在厂商软件中恢复正常充电，再移除电池恢复记录";conflict=true;invalidStore=true; }
                else if(!recovery.Software){conflict=true;verification="unconfirmed";warning="上次设置结果未确认，请重新读取后再应用";}
            }
        }
        catch(Exception e){warning="电池设置未能读取："+e.Message;conflict=true;invalidStore=true;}
    }
    int PauseRaw=>route?.RouteId=="ayaneo-ceii-bypass"?1:0x55;
    int ResumeRaw=>route?.RouteId=="ayaneo-ceii-bypass"?0x65:0xaa;
    string Fence(Reading? reading)=>Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes($"{session}|{identity.Key}|{revision}|{reading?.Raw}")));
    Snapshot State(bool ok,bool supported,Reading? reading,string reason="")=>new(1,ok,supported,power().Present,identity.BoardProduct,route?.Family??"",route?.Kind??"unsupported",route?.Choices??[],reading?.Limit,preference?.Target,preference?.Managed==true||recovery?.Software==true,verification,Fence(reading),string.IsNullOrEmpty(reason)?warning:reason) {CapabilityId=capability?.Descriptor.DeviceId,CapabilityRuleId=capability?.Descriptor.RuleId,CapabilityOrigin=capability?.Descriptor.Origin,CapabilityDeclaredByFrozenHc=capability?.Descriptor.DeclaredByFrozenHc,CapabilityCatalogSha256=YeManFanBackend.Capabilities.HcBatteryCapabilityCatalog.CatalogSha256};
    void RequireAvailable()
    {
        if(route is null)throw new IOException(Policy.Unavailable(identity));
        if(!power().Present)throw new IOException("未检测到内置电池");
        if(!Policy.FirmwareAllowed(route,identity))throw new IOException("当前 EC 或 BIOS 版本尚未通过充电接口校验");
        if(suspended)throw new IOException("正在睡眠或恢复，充电设置稍后可用");
    }
    public Snapshot Read()
    {
        lock(gate)
        {
            try
            {
                RequireAvailable();using var p=open();var read=p.Read();
                if(p.CanReadLimit&&!conflict)verification=read.Limit is null?"unknown":"readback";
                if(active&&expectedRaw!=read.Raw){active=false;conflict=true;warning="充电状态被其他程序修改，软件限充已暂停";verification="conflict";revision++;}
                return State(true,!conflict,read);
            }
            catch(Exception e){TraceLog.Error("controller-read-error",e);return State(false,false,null,e.Message);}
        }
    }
    public Snapshot Apply(string fence,int target)
    {
        lock(gate)
        {
            Reading? before=null;
            try
            {
                RequireAvailable();if(route is null||!Policy.ValueAllowed(route,target))return State(false,false,null,"此机型不支持所选充电档位");
                using var p=open();before=p.Read();
                if(fence!=Fence(before))return State(false,!conflict,before,"充电状态已更新，请确认当前值后重新应用");
                if(conflict)throw new IOException("存在未解决的充电恢复记录，请先停止软件限充或核对厂商设置");
                if(route.Software)
                {
                    if(active && expectedRaw!=before.Raw)throw new IOException("充电状态已被其他程序修改，请先停止软件限充");
                    if(target==100)return ReleaseLocked();
                    if(!active&&before.Paused==true)throw new IOException("旁路充电已由其他程序开启，请先关闭厂商软件的旁路设置");
                    recovery??=new(1,identity.Key,route.RouteId,before.Raw,before.Raw,true);
                    store.Save(RecoveryFile,recovery); // durable owner before any bypass write
                    var next=new Preference(1,identity.Key,route.RouteId,target,true);store.Save(SettingsFile,next);preference=next;
                    active=true;expectedRaw=before.Raw;warning="";verification="software-active";revision++;
                    TickLocked();var result=Read();return result with {Ok=!conflict&&result.Ok};
                }
                var desired=new Preference(1,identity.Key,route.RouteId,target,true);
                if(p.CanReadLimit&&before.Limit==target)
                {store.Save(SettingsFile,desired);preference=desired;verification="readback";warning="";revision++;return State(true,true,before);}
                recovery=new(1,identity.Key,route.RouteId,before.Raw,null,false);store.Save(RecoveryFile,recovery);
                p.Write(target,before);
                var after=p.Read();
                if(p.CanReadLimit&&after.Limit!=target)throw new IOException("写入后回读不一致；显示实际值，请检查厂商软件是否同时控制充电");
                store.Save(SettingsFile,desired);preference=desired;store.Clear(RecoveryFile);recovery=null;
                verification=p.CanReadLimit?"readback":"acknowledged";warning="";revision++;
                return State(true,true,after);
            }
            catch(Exception e)
            {
                TraceLog.Error("controller-apply-error",e,new {target,preflightCompleted=before is not null});
                // A rejected input or failed preflight must not disable an already-running software policy.
                if(before is null)return State(false,false,null,e.Message);
                warning=e.Message;revision++;
                if(recovery is not null){active=false;conflict=true;verification="unconfirmed";}
                try{using var p=open();return State(false,!conflict,p.Read(),warning);}catch{return State(false,false,null,warning);}
            }
        }
    }
    // An uncertain firmware request is never replayed. A fresh read acknowledges the uncertainty;
    // the next explicit Apply can then replace it, rather than repeating an old target on restart.
    public Snapshot Acknowledge()
    {
        lock(gate)
        {
            if(!invalidStore&&recovery is {Software:false}&&route is not null&&recovery.IdentityKey==identity.Key)
            {
                try{RequireAvailable();using var p=open();_ = p.Read();
                    if(preference is not null){var disabled=preference with {Managed=false};store.Save(SettingsFile,disabled);preference=disabled;}
                    store.Clear(RecoveryFile);recovery=null;conflict=false;warning="";revision++;}
                catch(Exception e){TraceLog.Error("controller-read-error",e);return State(false,false,null,e.Message);}
            }
            return Read();
        }
    }
    void RestoreOwned()
    {
        if(invalidStore)throw new IOException(warning);
        if(recovery is not {Software:true})return;
        if(route is null||recovery.IdentityKey!=identity.Key||recovery.RouteId!=route.RouteId)throw new IOException("恢复记录与当前设备不匹配");
        using var p=open();var current=p.Read();
        if(current.Raw!=recovery.OriginalRaw)
        {
            if(current.Raw!=recovery.OwnedRaw)throw new IOException("电池旁路已被外部修改，未覆盖外部状态");
            p.Write(0,current);if(p.Read().Raw!=recovery.OriginalRaw)throw new IOException("尚未确认恢复正常充电");
        }
        store.Clear(RecoveryFile);recovery=null;expectedRaw=null;active=false;
    }
    Snapshot ReleaseLocked()
    {
        try
        {
            if(route?.Software!=true || invalidStore)throw new IOException("无法自动恢复此记录，请核对厂商充电设置");
            active=false;
            var disabled=new Preference(1,identity.Key,route.RouteId,100,false);store.Save(SettingsFile,disabled);preference=disabled;
            RestoreOwned();conflict=false;verification="unmanaged";warning="";revision++;return Read();
        }
        catch(Exception e){conflict=true;warning=e.Message;verification="unconfirmed";return State(false,false,null,warning);}
    }
    public Snapshot Release(){lock(gate)return ReleaseLocked();}
    void TickLocked()
    {
        if(!active||suspended||route is null||preference is null)return;
        try
        {
            using var p=open();var current=p.Read();
            if(current.Raw!=expectedRaw)throw new IOException("检测到其他软件改变充电状态，已暂停软件限充");
            var facts=power();int decision=Policy.BypassDecision(facts.Percent,facts.Present&&facts.Ac,preference.Target,current.Paused==true)??0;
            if((current.Paused==true)==(decision==1))return;
            int wantedRaw=decision==1?PauseRaw:ResumeRaw;
            var nextRecovery=recovery! with {OwnedRaw=wantedRaw};store.Save(RecoveryFile,nextRecovery);recovery=nextRecovery;
            p.Write(decision,current);var after=p.Read();
            if(after.Raw!=wantedRaw)throw new IOException("未能确认旁路充电状态");
            expectedRaw=after.Raw;revision++;
        }
        catch(Exception e)
        {
            active=false;conflict=true;warning=e.Message;verification="unconfirmed";
            // Only restore a byte that still equals our journalled write. Never guess through a read failure.
            try{RestoreOwned();}catch(Exception cleanup){warning+="；"+cleanup.Message;}
        }
    }
    public void Tick(){lock(gate)TickLocked();}
    public void RecoverSoftware(){lock(gate){if(!invalidStore&&recovery is {Software:true})StartSaved();}}
    public void StartSaved()
    {
        lock(gate)
        {
            try
            {
                RequireAvailable();
                if(invalidStore)return;
                if(active)return; // duplicate startup requests must not restore a running policy
                if(recovery is {Software:true}){RestoreOwned();conflict=false;}
                else if(recovery is not null){conflict=true;warning="上次充电设置结果未确认，请重新读取并确认";return;}
                if(conflict||preference?.Managed!=true)return;
                var s=Read();if(!s.Supported)return;
                // A valid transport with an unknown value allows an explicit user
                // setting, but cannot authorize unattended startup/resume replay.
                using(var p=open())if(p.CanReadLimit&&s.CurrentLimit is null){warning="当前充电上限未知，未自动恢复；请手动选择档位后应用";return;}
                _=Apply(s.Fence,preference.Target);
            }
            catch(Exception e){warning=e.Message;}
        }
    }
    public void Suspend()
    {
        suspended=true;
        lock(gate){active=false;try{RestoreOwned();}catch(Exception e){warning=e.Message;conflict=true;}}
    }
    public void Resume()
    {
        suspended=false;
        // Firmware thresholds are not fought against OEM software on resume; software bypass restarts only after cleanup.
        if(route?.Software==true)StartSaved();
    }
    public void Shutdown(){Suspend();}
}
