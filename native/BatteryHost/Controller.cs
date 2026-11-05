using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
namespace YeManBatteryHost;
internal sealed record Preference(int Schema,string IdentityKey,string RouteId,int Target,bool Managed);
internal sealed record Recovery(int Schema,string IdentityKey,string RouteId,int? OriginalRaw,int? OwnedRaw,bool Software)
{
    // Forward-compatible annotations survive updates to the same owned journal.
    [System.Text.Json.Serialization.JsonExtensionData]
    public Dictionary<string,JsonElement>? Extra {get;init;}
}
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
    const int MaxBytes=16384;
    public string DirectoryPath=>directory;
    static void ValidateJournal(string text)
    {
        using var doc=JsonDocument.Parse(text);
        if(doc.RootElement.ValueKind!=JsonValueKind.Object)throw new IOException("充电恢复记录必须是对象");
        string[] names=["schema","identityKey","routeId","originalRaw","ownedRaw","software"];
        var fields=new Dictionary<string,JsonElement>(StringComparer.OrdinalIgnoreCase);
        foreach(var property in doc.RootElement.EnumerateObject())
            if(names.Contains(property.Name,StringComparer.OrdinalIgnoreCase)&&!fields.TryAdd(property.Name,property.Value))
                throw new IOException("充电恢复记录含重复字段");
        if(names.Any(n=>!fields.ContainsKey(n)))throw new IOException("充电恢复记录缺少必需字段");
        if(fields["schema"].ValueKind!=JsonValueKind.Number||!fields["schema"].TryGetInt32(out _))throw new IOException("充电恢复版本类型异常");
        foreach(var name in new[]{"identityKey","routeId"})
            if(fields[name].ValueKind!=JsonValueKind.String||string.IsNullOrWhiteSpace(fields[name].GetString()))throw new IOException("充电恢复身份类型异常");
        foreach(var name in new[]{"originalRaw","ownedRaw"})
            if(fields[name].ValueKind!=JsonValueKind.Null&&(fields[name].ValueKind!=JsonValueKind.Number||!fields[name].TryGetInt32(out _)))throw new IOException("充电恢复原值类型异常");
        if(fields["software"].ValueKind is not (JsonValueKind.True or JsonValueKind.False))throw new IOException("充电恢复方式类型异常");
    }
    public virtual T? Load<T>(string name)
    {
        var p=Path.Combine(directory,name);FileAttributes attributes;
        // File.Exists hides access errors and directories as absence. Only actual
        // missing paths are defaults; an unreadable existing journal stays debt.
        try{attributes=File.GetAttributes(p);}
        catch(FileNotFoundException){return default;}
        catch(DirectoryNotFoundException)
        {
            try{if((File.GetAttributes(directory)&FileAttributes.Directory)==0)throw new IOException("电池状态目录不是目录");}
            catch(FileNotFoundException){return default;}
            catch(DirectoryNotFoundException){return default;}
            throw;
        }
        if((attributes&(FileAttributes.Directory|FileAttributes.ReparsePoint))!=0)throw new IOException("电池状态文件类型异常");
        using var stream=new FileStream(p,FileMode.Open,FileAccess.Read,FileShare.Read);
        if(stream.Length==0||stream.Length>MaxBytes)throw new IOException("电池设置文件为空或过大");
        using var reader=new StreamReader(stream,new UTF8Encoding(false,true),true);
        var text=reader.ReadToEnd();
        if(typeof(T)==typeof(Recovery))ValidateJournal(text);
        return JsonSerializer.Deserialize<T>(text,Policy.Json)??throw new IOException("电池设置文件损坏");
    }
    public virtual void Save<T>(string name,T value)
    {
        var bytes=JsonSerializer.SerializeToUtf8Bytes(value,Policy.Json);
        if(bytes.Length>MaxBytes)throw new IOException("电池设置文件过大，未覆盖原文件");
        if(typeof(T)==typeof(Recovery))
        {
            ValidateJournal(Encoding.UTF8.GetString(bytes));
            _=Load<Recovery>(name); // Never overwrite a newly corrupt or unreadable original.
        }
        Directory.CreateDirectory(directory);var path=Path.Combine(directory,name);var temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";
        try{using(var f=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)){f.Write(bytes);f.Flush(true);}File.Move(temp,path,true);}
        finally{if(File.Exists(temp))File.Delete(temp);}
    }
    public virtual void Clear(string name)
    {
        var p=Path.Combine(directory,name);
        if(name==Controller.RecoveryFile&&Load<Recovery>(name) is null)throw new IOException("充电恢复记录缺失，未确认归还记录已清理");
        File.Delete(p);
    }
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
    readonly Func<bool> ready;
    readonly string session=Guid.NewGuid().ToString("N");
    Preference? preference;
    Recovery? recovery;
    int revision;
    bool active,conflict;
    bool invalidStore,journalUnresolved;
    volatile bool suspended;
    string warning="";
    string verification="unknown";
    int? expectedRaw;
    public Controller(Identity identity,Store store,Func<Route,IChargeProvider> factory,Func<(bool Present,bool Ac,int? Percent)>? power=null,Func<bool>? ready=null)
    {
        this.identity=identity;this.store=store;capability=Policy.ResolveCapability(identity);route=capability?.Consumer;open=()=>factory(route??throw new IOException("未知机型"));this.power=power??WindowsFacts.Power;
        this.ready=ready??(static ()=>true);
        ReloadStore();
    }
    // Keep known ownership in memory on every failed reload. Missing/corrupt IO
    // cannot acknowledge a debt; only an explicit refresh retries persistence.
    void StorageFailure(Exception error)
    {
        active=false;invalidStore=true;conflict=true;verification="unknown";
        warning="电池恢复记录未确认："+error.Message;revision++;
    }
    void ValidateRecovery(Recovery? value)
    {
        if(value is null)return;
        if(value.Schema!=1||route is null||value.IdentityKey!=identity.Key||value.RouteId!=route.RouteId||value.Software!=route.Software)
            throw new IOException("充电恢复记录与当前设备或接口不匹配");
        if(value.OriginalRaw is <0 or >255||value.OwnedRaw is <0 or >255)
            throw new IOException("充电恢复原值范围异常");
        if(value.Software&&(value.OriginalRaw!=ResumeRaw||(value.OwnedRaw!=ResumeRaw&&value.OwnedRaw!=PauseRaw)))
            throw new IOException("充电恢复记录不能证明原始正常充电值");
    }
    static bool SameOwner(Recovery a,Recovery b)=>a.Schema==b.Schema&&a.IdentityKey==b.IdentityKey&&a.RouteId==b.RouteId&&a.OriginalRaw==b.OriginalRaw&&a.Software==b.Software;
    void ReloadStore()
    {
        Exception? error=null;Preference? savedPreference=null;
        try
        {
            savedPreference=store.Load<Preference>(SettingsFile);
            if(savedPreference is not null&&(savedPreference.Schema!=1||route is null||savedPreference.IdentityKey!=identity.Key||savedPreference.RouteId!=route.RouteId||!Policy.ValueAllowed(route,savedPreference.Target)))savedPreference=null;
        }
        catch(Exception e){error=e;}
        try
        {
            var saved=store.Load<Recovery>(RecoveryFile);ValidateRecovery(saved);
            if(saved is null&&(recovery is not null||journalUnresolved))throw new IOException("仍有未确认的充电归还责任，恢复记录缺失");
            if(saved is not null&&recovery is not null&&!SameOwner(saved,recovery))throw new IOException("恢复原值已变化，保留尚未归还的旧责任");
            recovery=saved;journalUnresolved=false;
        }
        catch(Exception e){journalUnresolved=true;error=e;}
        if(error is not null){StorageFailure(error);return;}
        preference=savedPreference;invalidStore=false;active=false;expectedRaw=null;
        conflict=recovery is {Software:false};verification=conflict?"unconfirmed":"unknown";
        warning=conflict?"上次设置结果未确认，请重新读取后再应用":"";
    }
    void EnsureJournal()
    {
        if(invalidStore)throw new IOException(warning);
        try
        {
            var saved=store.Load<Recovery>(RecoveryFile);ValidateRecovery(saved);
            if(saved is null&&recovery is not null)throw new IOException("充电归还记录意外缺失");
            if(saved is not null)
            {
                if(recovery is null){recovery=saved;throw new IOException("出现新的未确认充电记录，请明确刷新后处理");}
                if(!SameOwner(saved,recovery)||saved.OwnedRaw!=recovery.OwnedRaw)throw new IOException("充电恢复原值或已拥有值已被修改，未接管新记录");
            }
            recovery=saved;
        }
        catch(Exception e){journalUnresolved=true;StorageFailure(e);throw;}
    }
    void SaveRecovery(Recovery next)
    {
        ValidateRecovery(next);
        try{store.Save(RecoveryFile,next);recovery=next;}
        catch(Exception e)
        {
            // A write may have committed before an IO error was returned. Read
            // back at most once; never guess that it failed or discard old debt.
            bool verifiedOwner=false;
            try
            {
                var saved=store.Load<Recovery>(RecoveryFile);ValidateRecovery(saved);
                if(saved is not null&&SameOwner(saved,next)){recovery=saved;verifiedOwner=true;}
                else if(saved is not null||recovery is not null)journalUnresolved=true;
            }
            catch{journalUnresolved=true;}
            StorageFailure(e);
            // An unchanged, re-read valid owner may still return a paused device
            // to its recorded original. Do not abandon that existing duty just
            // because saving the next policy byte failed.
            if(verifiedOwner){invalidStore=false;journalUnresolved=false;}
            throw;
        }
    }

    void ClearRecovery()
    {
        EnsureJournal();
        try{store.Clear(RecoveryFile);}
        catch(Exception e){StorageFailure(e);throw;}
        recovery=null;journalUnresolved=false;
    }
    int PauseRaw=>route?.RouteId=="ayaneo-ceii-bypass"?1:0x55;
    int ResumeRaw=>route?.RouteId=="ayaneo-ceii-bypass"?0x65:0xaa;
    string Fence(Reading? reading)=>Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes($"{session}|{identity.Key}|{revision}|{reading?.Raw}")));
    Snapshot State(bool ok,bool supported,Reading? reading,string reason="")=>new(1,ok,supported,power().Present,identity.BoardProduct,route?.Family??"",route?.Kind??"unsupported",route?.Choices??[],reading?.Limit,preference?.Target,!invalidStore&&(preference?.Managed==true||recovery?.Software==true),verification,Fence(reading),string.IsNullOrEmpty(reason)?warning:reason) {CapabilityId=capability?.Descriptor.DeviceId,CapabilityRuleId=capability?.Descriptor.RuleId,CapabilityOrigin=capability?.Descriptor.Origin,CapabilityDeclaredByFrozenHc=capability?.Descriptor.DeclaredByFrozenHc,CapabilityCatalogSha256=YeManFanBackend.Capabilities.HcBatteryCapabilityCatalog.CatalogSha256};
    readonly List<IChargeProvider> providerCleanupDebt=new();
    string providerCleanupReason="";
    sealed class ProviderLease(Controller owner,IChargeProvider provider) : IChargeProvider
    {
        bool closed;
        public bool CanReadLimit=>provider.CanReadLimit;
        public Reading Read()=>provider.Read();
        public void Write(int value,Reading before)
        {
            if(provider is IChargeWriteAdmission guarded)guarded.Write(value,before,owner.RequireAvailable);
            else provider.Write(value,before); // Preserve existing explicit restoration during suspend/stop.
        }
        public void Dispose()
        {
            if(closed)return;
            try{provider.Dispose();closed=true;}
            catch(Exception error)
            {
                if(!owner.providerCleanupDebt.Any(other=>ReferenceEquals(other,provider)))owner.providerCleanupDebt.Add(provider);
                owner.active=false;owner.verification="unknown";
                owner.providerCleanupReason="硬件通道清理尚未完成："+error.Message;
                owner.warning=owner.providerCleanupReason;owner.revision++;
                throw new IOException(owner.providerCleanupReason,error);
            }
        }
    }
    IChargeProvider OpenOwnedProvider()
    {
        if(providerCleanupDebt.Count!=0)throw new IOException("原硬件通道尚未归还，未创建新终端");
        return new ProviderLease(this,open());
    }
    void RetryProviderCleanup()
    {
        Exception? first=null;
        foreach(var provider in providerCleanupDebt.ToArray().Reverse())
        {
            try
            {
                provider.Dispose();
                int index=providerCleanupDebt.FindIndex(other=>ReferenceEquals(other,provider));
                if(index>=0)providerCleanupDebt.RemoveAt(index);
            }
            catch(Exception error){first??=error;}
        }
        if(first is not null)throw new IOException("原硬件通道仍未确认归还",first);
        if(!conflict&&warning==providerCleanupReason)warning="";
        providerCleanupReason="";
    }
    void RequireAvailable()
    {
        if(route is null)throw new IOException(Policy.Unavailable(identity));
        if(!power().Present)throw new IOException("未检测到内置电池");
        if(!Policy.FirmwareAllowed(route,identity))
        {
            // Reuse the identity facts already collected for this consumer. A
            // rejected route never opens an EC endpoint merely for diagnostics.
            TraceLog.Write("firmware-admission-rejected",new {
                route=route.RouteId,deviceClass=route.Identity.DeviceClass,
                observed=new {identity.Manufacturer,identity.BoardProduct,identity.SystemProduct,identity.Bios,
                    identity.EcMajor,identity.EcMinor,
                    ecMajorKnown=identity.EcMajor>=0&&identity.EcMajor<255,
                    ecMinorKnown=identity.EcMinor>=0&&identity.EcMinor<255,
                    source="Win32_BIOS.EmbeddedControllerMajorVersion/EmbeddedControllerMinorVersion;SMBIOSBIOSVersion"},
                required=route.FirmwareGate,allowed=false,providerOpened=false,
                scope="firmware-admission-only;no-EC-IO;no-write"});
            throw new IOException("当前 EC 或 BIOS 版本尚未通过充电接口校验");
        }
        if(suspended||!ready())throw new IOException("正在停止、睡眠或恢复，充电设置稍后可用");
    }
    public Snapshot Read()
    {
        lock(gate)
        {
            try
            {
                EnsureJournal();RequireAvailable();using var p=OpenOwnedProvider();var read=p.Read();
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
                EnsureJournal();RequireAvailable();if(route is null||!Policy.ValueAllowed(route,target))return State(false,false,null,"此机型不支持所选充电档位");
                using var p=OpenOwnedProvider();before=p.Read();
                if(fence!=Fence(before))return State(false,!conflict,before,"充电状态已更新，请确认当前值后重新应用");
                if(conflict)throw new IOException("存在未解决的充电恢复记录，请先停止软件限充或核对厂商设置");
                if(route.Software)
                {
                    if(active && expectedRaw!=before.Raw)throw new IOException("充电状态已被其他程序修改，请先停止软件限充");
                    if(target==100)return ReleaseLocked();
                    if(!active&&before.Paused==true)throw new IOException("旁路充电已由其他程序开启，请先关闭厂商软件的旁路设置");
                    var owner=recovery??new(1,identity.Key,route.RouteId,before.Raw,before.Raw,true);
                    SaveRecovery(owner); // durable owner before any bypass write
                    var next=new Preference(1,identity.Key,route.RouteId,target,true);store.Save(SettingsFile,next);preference=next;
                    active=true;expectedRaw=before.Raw;warning="";verification="software-active";revision++;
                    TickLocked();var result=Read();return result with {Ok=!conflict&&result.Ok};
                }
                var desired=new Preference(1,identity.Key,route.RouteId,target,true);
                if(p.CanReadLimit&&before.Limit==target)
                {store.Save(SettingsFile,desired);preference=desired;verification="readback";warning="";revision++;return State(true,true,before);}
                SaveRecovery(new(1,identity.Key,route.RouteId,before.Raw,null,false));
                RequireAvailable(); // Stop/power changes during pre-read must precede any new firmware write.
                p.Write(target,before);
                var after=p.Read();
                if(p.CanReadLimit&&after.Limit!=target)throw new IOException("写入后回读不一致；显示实际值，请检查厂商软件是否同时控制充电");
                store.Save(SettingsFile,desired);preference=desired;ClearRecovery();
                verification=p.CanReadLimit?"readback":"acknowledged";warning="";revision++;
                return State(true,true,after);
            }
            catch(Exception e)
            {
                TraceLog.Error("controller-apply-error",e,new {target,preflightCompleted=before is not null});
                // A rejected input or failed preflight must not disable an already-running software policy.
                if(before is null)return State(false,false,null,e.Message);
                warning=e.Message;revision++;
                if(recovery is not null){active=false;conflict=true;verification=invalidStore?"unknown":"unconfirmed";}
                try{using var p=OpenOwnedProvider();return State(false,!conflict,p.Read(),warning);}catch{return State(false,false,null,warning);}
            }
        }
    }
    // An uncertain firmware request is never replayed. A fresh read acknowledges the uncertainty;
    // the next explicit Apply can then replace it, rather than repeating an old target on restart.
    public Snapshot Acknowledge()
    {
        lock(gate)
        {
            try
            {
                RetryProviderCleanup();
                if(invalidStore){ReloadStore();if(invalidStore)return State(false,false,null,warning);}
                EnsureJournal();
                if(recovery is {Software:false}&&route is not null&&recovery.IdentityKey==identity.Key)
                {
                    RequireAvailable();using var p=OpenOwnedProvider();_ = p.Read();
                    if(preference is not null){var disabled=preference with {Managed=false};store.Save(SettingsFile,disabled);preference=disabled;}
                    ClearRecovery();conflict=false;warning="";revision++;
                }
                return Read();
            }
            catch(Exception e){TraceLog.Error("controller-read-error",e);return State(false,false,null,e.Message);}
        }
    }
    void RestoreOwned()
    {
        EnsureJournal();
        if(recovery is not {Software:true})return;
        if(route is null||recovery.IdentityKey!=identity.Key||recovery.RouteId!=route.RouteId)throw new IOException("恢复记录与当前设备不匹配");
        using var p=OpenOwnedProvider();var current=p.Read();
        if(current.Raw!=recovery.OriginalRaw)
        {
            if(current.Raw!=recovery.OwnedRaw)throw new IOException("电池旁路已被外部修改，未覆盖外部状态");
            p.Write(0,current);if(p.Read().Raw!=recovery.OriginalRaw)throw new IOException("尚未确认恢复正常充电");
        }
        ClearRecovery();expectedRaw=null;active=false;
    }
    Snapshot ReleaseLocked()
    {
        try
        {
            EnsureJournal();
            if(route?.Software!=true || invalidStore)throw new IOException("无法自动恢复此记录，请核对厂商充电设置");
            active=false;
            var disabled=new Preference(1,identity.Key,route.RouteId,100,false);store.Save(SettingsFile,disabled);preference=disabled;
            RestoreOwned();conflict=false;verification="unmanaged";warning="";revision++;return Read();
        }
        catch(Exception e){conflict=true;warning=e.Message;verification=invalidStore?"unknown":"unconfirmed";return State(false,false,null,warning);}
    }
    public Snapshot Release()
    {
        lock(gate)
        {
            try{RetryProviderCleanup();}catch(Exception error){return State(false,false,null,error.Message);}
            if(invalidStore){ReloadStore();if(invalidStore)return State(false,false,null,warning);}
            return ReleaseLocked();
        }
    }
    void TickLocked()
    {
        if(!active||suspended||route is null||preference is null)return;
        try
        {
            EnsureJournal();using var p=OpenOwnedProvider();var current=p.Read();
            if(current.Raw!=expectedRaw)throw new IOException("检测到其他软件改变充电状态，已暂停软件限充");
            var facts=power();int decision=Policy.BypassDecision(facts.Percent,facts.Present&&facts.Ac,preference.Target,current.Paused==true)??0;
            if((current.Paused==true)==(decision==1))return;
            int wantedRaw=decision==1?PauseRaw:ResumeRaw;
            var nextRecovery=recovery! with {OwnedRaw=wantedRaw};SaveRecovery(nextRecovery);
            p.Write(decision,current);var after=p.Read();
            if(after.Raw!=wantedRaw)throw new IOException("未能确认旁路充电状态");
            expectedRaw=after.Raw;revision++;
        }
        catch(Exception e)
        {
            active=false;conflict=true;warning=e.Message;verification=invalidStore?"unknown":"unconfirmed";
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
                EnsureJournal();RequireAvailable();
                if(invalidStore)return;
                if(active)return; // duplicate startup requests must not restore a running policy
                if(recovery is {Software:true}){RestoreOwned();conflict=false;}
                else if(recovery is not null){conflict=true;warning="上次充电设置结果未确认，请重新读取并确认";return;}
                if(conflict||preference?.Managed!=true)return;
                var s=Read();if(!s.Supported)return;
                // A valid transport with an unknown value allows an explicit user
                // setting, but cannot authorize unattended startup/resume replay.
                using(var p=OpenOwnedProvider())if(p.CanReadLimit&&s.CurrentLimit is null){warning="当前充电上限未知，未自动恢复；请手动选择档位后应用";return;}
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
    public void Shutdown()
    {
        suspended=true;
        lock(gate)
        {
            RetryProviderCleanup();
            active=false;
            try{RestoreOwned();}catch(Exception error){warning=error.Message;conflict=true;}
            // Restoration debt remains journal-owned. Actual provider/resource
            // close debt must propagate so the host cannot return its owner.
            RetryProviderCleanup();
        }
    }
}
