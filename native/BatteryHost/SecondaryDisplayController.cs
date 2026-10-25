using System.Security.Cryptography;
using System.Text;
using YeManFanBackend.Capabilities;
namespace YeManBatteryHost;

internal sealed record SecondaryDisplayPreference(int Schema,string IdentityKey,string CapabilityId,byte LastNonzeroRaw);
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
    volatile bool suspended,stopped;
    public SecondaryDisplayController(Identity identity,Store store,Func<bool,IEcAccess>? ecFactory=null,Func<bool>? ready=null)
    {
        this.identity=identity;this.store=store;this.open=()=>(ecFactory??(direct=>new EcAccess(direct)))(false);this.ready=ready??(()=>true);
        binding=HcSecondaryDisplayCapabilityCatalog.Resolve(HcSecondaryDisplayCapabilityCatalog.Bind(this),identity.Manufacturer,identity.BoardProduct);
        if(binding is null)return;
        try
        {
            var saved=store.Load<SecondaryDisplayPreference>(SettingsFile);
            if(saved is null)return;
            if(saved.Schema!=1||saved.IdentityKey!=identity.Key||saved.CapabilityId!=CapabilityId||saved.LastNonzeroRaw==0)
                throw new IOException("副屏偏好与当前设备不匹配");
            lastNonzero=saved.LastNonzeroRaw;
        }
        catch(Exception e){invalidPreference=true;TraceLog.Error("secondary-display-preference-error",e);}
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
                RequireReady();byte raw;
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
                byte before=fencedRaw.Value;Invalidate();
                if(explicitRaw is null&&invalidPreference)throw new IOException("副屏偏好无效，未猜测切换亮度");
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
                store.Save(SettingsFile,new SecondaryDisplayPreference(1,identity.Key,CapabilityId,remember));
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
