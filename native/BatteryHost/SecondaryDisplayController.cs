using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using YeManFanBackend.Capabilities;
namespace YeManBatteryHost;

internal sealed record SecondaryDisplayPreference(int Schema,string IdentityKey,string CapabilityId,byte LastNonzeroRaw)
{
    [System.Text.Json.Serialization.JsonExtensionData]
    public Dictionary<string,JsonElement>? Extra {get;init;}
}
internal sealed record SecondaryDisplaySnapshot(int Schema,bool Ok,bool Supported,string Model,string ModelId,string CapabilityId,
    byte? Raw,int? Brightness,bool? Enabled,string Fence,string Verification,string Reason,string Stage);

// Persistent display settings have no fan/charge-style restore operation. Every EC session is short-lived.
internal sealed class SecondaryDisplayController
{
    internal const string SettingsFile="secondary-display.settings.json";
    readonly object gate=new();
    readonly Identity identity;
    readonly Store store;
    readonly Func<IEcAccess> open;
    readonly Func<bool> ready;
    readonly HcCapabilityBinding<SecondaryDisplayController>? binding;
    string CapabilityId=>binding is null?"":HcSecondaryDisplayCapabilityCatalog.CapabilityFor(binding.Device);
    readonly string session=Guid.NewGuid().ToString("N");
    long revision;
    string fence="";
    byte? fencedRaw;
    byte lastNonzero=HcSecondaryDisplayCapabilityCatalog.DefaultRaw;
    bool invalidPreference;
    string preferenceError="";
    SecondaryDisplayPreference? preference;
    // Same format limit as Store. Oversize/empty content is corrupt only when
    // this exact regular source can be inspected; IO is never a missing default.
    const int PreferenceMaxBytes=16384;
    volatile bool suspended,stopped;
    public SecondaryDisplayController(Identity identity,Store store,Func<bool,IEcAccess>? ecFactory=null,Func<bool>? ready=null)
    {
        this.identity=identity;this.store=store;this.open=()=>(ecFactory??(direct=>new EcAccess(direct)))(false);this.ready=ready??(()=>true);
        binding=HcSecondaryDisplayCapabilityCatalog.Resolve(HcSecondaryDisplayCapabilityCatalog.Bind(this),identity.Manufacturer,identity.BoardProduct);
        if(binding is null)return;
        ReloadPreference();
    }
    static void ValidateKeys(JsonElement node)
    {
        if(node.ValueKind==JsonValueKind.Object)
        {
            var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach(var field in node.EnumerateObject())
            {if(!names.Add(field.Name))throw new InvalidDataException("副屏偏好含重复字段");ValidateKeys(field.Value);}
        }
        else if(node.ValueKind==JsonValueKind.Array)foreach(var value in node.EnumerateArray())ValidateKeys(value);
    }
    SecondaryDisplayPreference ValidatePreference(JsonElement document)
    {
        if(document.ValueKind!=JsonValueKind.Object)throw new InvalidDataException("副屏偏好必须是对象");
        ValidateKeys(document);
        var fields=document.EnumerateObject().ToDictionary(p=>p.Name,p=>p.Value,StringComparer.OrdinalIgnoreCase);
        string[] required=["schema","identityKey","capabilityId","lastNonzeroRaw"];
        if(required.Any(name=>!fields.ContainsKey(name)))throw new InvalidDataException("副屏偏好缺少必需字段");
        if(fields["schema"].ValueKind!=JsonValueKind.Number||!fields["schema"].TryGetInt32(out int schema)||schema!=1||
           fields["identityKey"].ValueKind!=JsonValueKind.String||fields["identityKey"].GetString()!=identity.Key||
           fields["capabilityId"].ValueKind!=JsonValueKind.String||fields["capabilityId"].GetString()!=CapabilityId||
           fields["lastNonzeroRaw"].ValueKind!=JsonValueKind.Number||!fields["lastNonzeroRaw"].TryGetByte(out byte remembered)||remembered==0)
            throw new InvalidDataException("副屏偏好类型、版本或设备身份无效");
        return JsonSerializer.Deserialize<SecondaryDisplayPreference>(document.GetRawText(),Policy.Json)??throw new InvalidDataException("副屏偏好损坏");
    }
    bool CorruptContent(Exception error)
    {
        if(error is JsonException or InvalidDataException or DecoderFallbackException)return true;
        if(error is not IOException)return false;
        try
        {
            string path=Path.Combine(store.DirectoryPath,SettingsFile);
            if((File.GetAttributes(path)&(FileAttributes.Directory|FileAttributes.ReparsePoint))!=0)return false;
            // Metadata alone is insufficient: an empty/oversize file may also
            // be unreadable. Confirm a real read handle before classifying it.
            using var readable=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read);
            long length=readable.Length;return length==0||length>PreferenceMaxBytes;
        }
        catch{return false;}
    }
    bool ReloadPreference()
    {
        try
        {
            // Read the original JSON before record conversion can coerce numeric
            // strings, supply missing fields or discard duplicate identities.
            var document=store.Load<JsonElement>(SettingsFile);
            preference=document.ValueKind==JsonValueKind.Undefined?null:ValidatePreference(document);
            lastNonzero=preference?.LastNonzeroRaw??HcSecondaryDisplayCapabilityCatalog.DefaultRaw;
            invalidPreference=false;preferenceError="";return true;
        }
        catch(Exception error)
        {
            preference=null;lastNonzero=HcSecondaryDisplayCapabilityCatalog.DefaultRaw;
            invalidPreference=CorruptContent(error);preferenceError=error.Message;
            TraceLog.Error("secondary-display-preference-error",error);
            return invalidPreference; // Corrupt settings may be explicitly repaired; unreadable IO may not.
        }
    }
    void RequirePreference()
    {if(!ReloadPreference())throw new IOException("副屏偏好无法读取，未按缺失处理："+preferenceError);}
    void PreserveInvalidPreference()
    {
        string path=Path.Combine(Path.GetFullPath(store.DirectoryPath),SettingsFile);FileAttributes attributes;
        try{attributes=File.GetAttributes(path);}
        catch(FileNotFoundException){return;} // A virtual test Store, or an original removed by its owner.
        catch(DirectoryNotFoundException){return;}
        if((attributes&(FileAttributes.Directory|FileAttributes.ReparsePoint))!=0)throw new IOException("副屏偏好原件不是普通文件");
        for(string? parent=Path.GetDirectoryName(path);parent is not null;parent=Path.GetDirectoryName(parent))
            if((File.GetAttributes(parent)&FileAttributes.ReparsePoint)!=0)throw new IOException("副屏偏好保全路径被重定向");
        using var original=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read);
        string hash=Convert.ToHexString(SHA256.HashData(original)).ToLowerInvariant();
        string archive=path+".invalid-"+hash+".json";
        bool ConfirmArchive()
        {
            FileAttributes existing;
            try{existing=File.GetAttributes(archive);}
            catch(FileNotFoundException){return false;}
            if((existing&(FileAttributes.Directory|FileAttributes.ReparsePoint))!=0)throw new IOException("副屏偏好保全目标不是普通文件");
            using var saved=new FileStream(archive,FileMode.Open,FileAccess.Read,FileShare.Read);
            if(saved.Length!=original.Length||!Convert.ToHexString(SHA256.HashData(saved)).Equals(hash,StringComparison.OrdinalIgnoreCase))throw new IOException("副屏偏好原件保全校验失败");
            return true;
        }
        if(ConfirmArchive())return;
        string temporary=archive+"."+Guid.NewGuid().ToString("N")+".tmp";
        try
        {
            original.Position=0;
            using(var output=new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.None)){original.CopyTo(output);output.Flush(true);}
            try{File.Move(temporary,archive);}catch(IOException){if(!ConfirmArchive())throw;}
            if(!ConfirmArchive())throw new IOException("副屏偏好原件保全未确认");
        }
        finally{if(File.Exists(temporary))File.Delete(temporary);}
    }
    void Invalidate(){fence="";fencedRaw=null;revision++;}
    void RequireReady()
    {
        if(binding is null||!ReferenceEquals(binding.Consumer,this))throw new IOException("当前精确机型没有副屏执行器");
        if(stopped||suspended||!ready())throw new IOException("副屏来源或电源会话已失效");
    }
    SecondaryDisplaySnapshot State(bool ok,byte? raw,string verification,string reason,string stage)=>new(1,ok,binding is not null,
        identity.BoardProduct,binding?.Device.DeviceId??"",CapabilityId,raw,raw is byte r?HcSecondaryDisplayCapabilityCatalog.DescribePercent(r):null,
        raw is byte current?current!=0:null,fence,verification,reason,stage);
    public SecondaryDisplaySnapshot Read()
    {
        lock(gate)
        {
            Invalidate();
            try
            {
                RequireReady();RequirePreference();byte raw;
                using(var ec=open()){RequireReady();raw=ec.Read(HcSecondaryDisplayCapabilityCatalog.BrightnessRegister);RequireReady();}
                RequireReady();fencedRaw=raw;
                fence=Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes($"{session}|{identity.Key}|{revision}|{raw}")));
                return State(true,raw,"ec-register-readback",invalidPreference?"副屏偏好无效，请先明确设置亮度":"","read");
            }
            catch(Exception e){Invalidate();TraceLog.Error("secondary-display-read-error",e);return State(false,null,"unknown",e.Message,"read");}
        }
    }
    public SecondaryDisplaySnapshot Apply(string expectedFence,short brightness)=>Change(expectedFence,HcSecondaryDisplayCapabilityCatalog.Encode(brightness));
    public SecondaryDisplaySnapshot Toggle(string expectedFence)=>Change(expectedFence,null);
    SecondaryDisplaySnapshot Change(string expectedFence,byte? explicitRaw)
    {
        lock(gate)
        {
            string stage="preflight";byte? verified=null;
            try
            {
                RequireReady();
                if(string.IsNullOrEmpty(fence)||fencedRaw is null||expectedFence!=fence)throw new IOException("副屏读取凭据已失效，请重新读取");
                byte before=fencedRaw.Value;Invalidate();RequirePreference();
                if(explicitRaw is null&&invalidPreference)throw new IOException("副屏偏好无效，未猜测切换亮度");
                if(invalidPreference)PreserveInvalidPreference();
                byte target=explicitRaw??(before==0?lastNonzero:(byte)0);
                using(var ec=open())
                {
                    RequireReady();stage="prewrite-read";
                    if(ec.Read(HcSecondaryDisplayCapabilityCatalog.BrightnessRegister)!=before)throw new IOException("副屏亮度已被外部修改，请重新读取");
                    RequireReady();stage="latch-write";ec.Write(HcSecondaryDisplayCapabilityCatalog.LatchRegister,HcSecondaryDisplayCapabilityCatalog.LatchValue);
                    RequireReady();stage="brightness-write";ec.Write(HcSecondaryDisplayCapabilityCatalog.BrightnessRegister,target);
                    RequireReady();stage="readback";byte after=ec.Read(HcSecondaryDisplayCapabilityCatalog.BrightnessRegister);
                    if(after!=target)throw new IOException("副屏亮度寄存器回读不一致");
                    RequireReady();verified=after;stage="dispose";
                }
                RequireReady();stage="preference-save";
                // Saving is authorized only by a successfully read-back explicit operation, never startup/resume.
                byte remember=target!=0?target:before!=0?before:lastNonzero;
                // Recheck after the EC session too: a newly bad/unreadable file
                // cannot be overwritten merely because readback succeeded.
                RequirePreference();if(invalidPreference)PreserveInvalidPreference();
                var saved=new SecondaryDisplayPreference(1,identity.Key,CapabilityId,remember){Extra=preference?.Extra};
                store.Save(SettingsFile,saved);
                lastNonzero=remember;invalidPreference=false;RequireReady();stage="complete";
                return State(true,verified,"ec-register-readback","",stage);
            }
            catch(Exception e)
            {
                Invalidate();TraceLog.Error("secondary-display-change-error",e,new {stage,readback=verified});
                return State(false,verified,verified is null?"unconfirmed":"ec-register-readback",e.Message,stage);
            }
        }
    }
    public SecondaryDisplaySnapshot Release(){lock(gate){Invalidate();return State(true,null,"released-no-restore","","release");}}
    public void Suspend(){suspended=true;lock(gate)Invalidate();}
    public void Resume(){lock(gate){Invalidate();suspended=false;}}
    public void Shutdown(){stopped=true;lock(gate)Invalidate();}
}
