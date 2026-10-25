using System.Text.Json;
using YeManBatteryHost;
using YeManFanBackend.Capabilities;

internal sealed class MemoryStore():Store("never-used-by-fixture")
{
    public readonly Dictionary<string,string> Data=[];
    public readonly List<string> Loads=[];
    public string? FailSave;
    public Action? AfterSave;
    public bool RejectBattery;
    public override T? Load<T>(string name) where T:default
    {
        Loads.Add(name);if(RejectBattery&&name.StartsWith("battery-"))throw new Exception("display read battery state");
        return Data.TryGetValue(name,out var text)?JsonSerializer.Deserialize<T>(text,Policy.Json):default;
    }
    public override void Save<T>(string name,T value){if(FailSave==name)throw new IOException("save fault");Data[name]=JsonSerializer.Serialize(value,Policy.Json);AfterSave?.Invoke();}
    public override void Clear(string name)=>Data.Remove(name);
}
internal sealed class MemoryEc
{
    public byte Raw=200;
    public int Opens,Disposals,ReadCount;
    public readonly List<string> Events=[];
    public string? FailAt;
    public bool IgnoreBrightness;
    public Action<string>? After;
    public IEcAccess Open(bool direct)
    {
        Program.Check(!direct,"wrong SuperIO address space");Opens++;Events.Add("open-acpi");
        if(FailAt=="open")throw new IOException("open fault");return new Session(this);
    }
    sealed class Session(MemoryEc ec):IEcAccess
    {
        public byte Read(ushort register)
        {
            Program.Check(register==0x4e,"wrong brightness register");ec.ReadCount++;ec.Events.Add("read-4e");
            if(ec.FailAt=="read"||ec.FailAt=="read-"+ec.ReadCount)throw new IOException("read fault");
            byte value=ec.Raw;ec.After?.Invoke("read-"+ec.ReadCount);return value;
        }
        public void Write(ushort register,byte value)
        {
            string name=register==0x4f?"latch":"brightness";
            Program.Check(register==0x4f||register==0x4e,"write outside reviewed registers");
            if(register==0x4f)Program.Check(value==0,"wrong latch value");
            ec.Events.Add($"write-{register:x2}-{value}");
            if(ec.FailAt==name+"-before")throw new IOException(name+" fault");
            if(register==0x4e&&!ec.IgnoreBrightness)ec.Raw=value;
            ec.After?.Invoke(name);
            if(ec.FailAt==name+"-after")throw new IOException(name+" acknowledgement lost");
        }
        public void Dispose(){ec.Disposals++;ec.Events.Add("dispose");ec.After?.Invoke("dispose");if(ec.FailAt=="dispose")throw new IOException("dispose fault");}
    }
}
internal sealed class Charge(Route route):IChargeProvider
{
    public int Raw=route.Software?0xaa:100,Writes,Reads;
    public bool CanReadLimit=>!route.Software;
    public Reading Read(){Reads++;return new(CanReadLimit?Raw:null,Raw,route.Software?Raw==0x55:null);}
    public void Write(int value,Reading before){Writes++;Raw=route.Software?(value==1?0x55:0xaa):value;}
    public void Dispose(){}
}
internal sealed class Rig(string board="FLIP DS")
{
    public readonly Identity Id=new("AYANEO",board,"device","cpu","bios",2,80);
    public readonly MemoryStore Store=new();
    public readonly MemoryEc Ec=new();
    public bool Ready=true;
    public SecondaryDisplayController New()=>new(Id,Store,Ec.Open,()=>Ready);
}
internal static class Program
{
    static int checks,cases;
    public static void Check(bool value,string reason="assertion failed"){checks++;if(!value)throw new Exception(reason);}
    static void Test(string name,Action body){body();cases++;Console.WriteLine("PASS "+name);}
    static JsonElement Request(string op,string? fence=null,int? target=null)=>JsonSerializer.SerializeToElement(new {op,fence,target},Policy.Json);
    static SecondaryDisplaySnapshot Send(HostDomainRouter router,string op,string? fence=null,int? target=null)=>(SecondaryDisplaySnapshot)router.Dispatch(Request(op,fence,target));
    static void Throws(Action body){bool failed=false;try{body();}catch{failed=true;}Check(failed,"expected rejection");}
    static void Main()
    {
        Test("exact two model source registrations select actual consumers",()=>{
            foreach(var board in new[]{"FLIP DS","FLIP 1S DS"})foreach(var maker in new[]{"AYANEO","AYADEVICE"})
            {
                var actual=new object();var routes=HcSecondaryDisplayCapabilityCatalog.Bind(actual);
                var binding=HcSecondaryDisplayCapabilityCatalog.Resolve(routes,maker,board);
                Check(binding is not null&&ReferenceEquals(binding.Consumer,actual));
                var r=new Rig(board);var c=r.New();var s=c.Read();Check(s.Ok&&s.ModelId==binding!.Device.DeviceId);
                Check(s.CapabilityId==HcSecondaryDisplayCapabilityCatalog.CapabilityFor(binding!.Device));Check(r.Ec.Disposals==1);
            }
        });
        Test("unknown and adjacent keyboard models never open EC or preferences",()=>{
            foreach(var board in new[]{"FLIP KB","FLIP 1S KB","FLIP DS extra","FLIP",""}){var r=new Rig(board);Check(!r.New().Read().Supported&&r.Ec.Opens==0&&r.Store.Loads.Count==0);}
            var routes=HcSecondaryDisplayCapabilityCatalog.Bind(new object());Check(HcSecondaryDisplayCapabilityCatalog.Resolve(routes,"AYA","FLIP DS") is null);
        });
        Test("all signed short values retain exact HC clamp and floor",()=>{
            for(int i=short.MinValue;i<=short.MaxValue;i++)Check(HcSecondaryDisplayCapabilityCatalog.Encode((short)i)==Math.Clamp(i,0,100)*255/100);
        });
        Test("read binds raw state and disposes without writes",()=>{
            var r=new Rig();var s=r.New().Read();Check(s.Ok&&s.Raw==200&&s.Fence.Length==64&&s.Verification=="ec-register-readback");
            Check(r.Ec.Events.SequenceEqual(new[]{"open-acpi","read-4e","dispose"}));Check(r.Store.Data.Count==0);
        });
        Test("actual setters execute read latch brightness and independent readback",()=>{
            foreach(short target in new short[]{short.MinValue,-1,0,1,33,50,99,100,101,short.MaxValue})
            {
                var r=new Rig();var c=r.New();var initial=c.Read();r.Ec.Events.Clear();var s=c.Apply(initial.Fence,target);int raw=Math.Clamp((int)target,0,100)*255/100;
                Check(s.Ok&&s.Raw==raw&&s.Stage=="complete"&&s.Fence=="");
                Check(r.Ec.Events.SequenceEqual(new[]{"open-acpi","read-4e","write-4f-0",$"write-4e-{raw}","read-4e","dispose"}));
                Check(r.Ec.Opens==r.Ec.Disposals);Check(r.Store.Data.ContainsKey(SecondaryDisplayController.SettingsFile));
            }
        });
        Test("external state change never reaches the latch",()=>{
            var r=new Rig();var c=r.New();var initial=c.Read();r.Ec.Raw=42;var s=c.Apply(initial.Fence,80);Check(!s.Ok&&r.Ec.Raw==42);Check(!r.Ec.Events.Any(e=>e.StartsWith("write-")));Check(r.Ec.Opens==r.Ec.Disposals);
        });
        Test("new read and restart invalidate old fences",()=>{
            var r=new Rig();var c=r.New();var old=c.Read();c.Read();int opens=r.Ec.Opens;Check(!c.Apply(old.Fence,50).Ok&&r.Ec.Opens==opens);
            var fresh=c.Read();var replacement=r.New();Check(!replacement.Apply(fresh.Fence,50).Ok&&r.Ec.Opens==opens+1);
        });
        Test("toggle is single use and restores exact persisted nonzero byte",()=>{
            var r=new Rig();r.Ec.Raw=173;var c=r.New();var initial=c.Read();var off=c.Toggle(initial.Fence);Check(off.Ok&&r.Ec.Raw==0);
            int opens=r.Ec.Opens;Check(!c.Toggle(initial.Fence).Ok&&r.Ec.Opens==opens);
            c=r.New();var on=c.Toggle(c.Read().Fence);Check(on.Ok&&on.Raw==173);Check(r.Ec.Opens==r.Ec.Disposals);
        });
        Test("fresh off display uses frozen default255",()=>{var r=new Rig();r.Ec.Raw=0;var c=r.New();Check(c.Toggle(c.Read().Fence).Raw==255);});
        Test("explicit zero retains selected brightness across toggles",()=>{
            var r=new Rig();var c=r.New();Check(c.Apply(c.Read().Fence,50).Raw==127);Check(c.Apply(c.Read().Fence,0).Ok);Check(c.Toggle(c.Read().Fence).Raw==127);
        });
        foreach(var failure in new[]{"open","read-2","latch-before","latch-after","brightness-before","brightness-after","read-3","dispose"})Test("partial failure "+failure+" preserves failure and consumes fence",()=>{
            var r=new Rig();var c=r.New();var s=c.Read();r.Ec.FailAt=failure;var failed=c.Apply(s.Fence,50);Check(!failed.Ok&&failed.Fence=="");
            if(failure!="open")Check(r.Ec.Opens==r.Ec.Disposals);
            int opens=r.Ec.Opens;Check(!c.Apply(s.Fence,50).Ok&&r.Ec.Opens==opens);Check(r.Store.Data.Count==0);
        });
        Test("readback mismatch is unknown and never persisted",()=>{
            var r=new Rig();var c=r.New();var s=c.Read();r.Ec.IgnoreBrightness=true;var result=c.Apply(s.Fence,50);Check(!result.Ok&&result.Verification=="unconfirmed"&&r.Store.Data.Count==0);
        });
        Test("disk failure retains verified hardware result but no success",()=>{
            var r=new Rig();var c=r.New();var s=c.Read();r.Store.FailSave=SecondaryDisplayController.SettingsFile;
            var result=c.Apply(s.Fence,50);Check(!result.Ok&&result.Raw==127&&result.Verification=="ec-register-readback"&&result.Stage=="preference-save");Check(r.Ec.Opens==r.Ec.Disposals);
        });
        Test("readiness loss during persistence cannot report success",()=>{
            var r=new Rig();var c=r.New();var s=c.Read();r.Store.AfterSave=()=>r.Ready=false;
            var result=c.Apply(s.Fence,50);Check(!result.Ok&&result.Fence==""&&result.Raw==127);Check(r.Ec.Opens==r.Ec.Disposals);
        });
        Test("malformed preference blocks guessed toggle and explicit set repairs",()=>{
            var r=new Rig();r.Store.Data[SecondaryDisplayController.SettingsFile]="broken";r.Ec.Raw=0;var c=r.New();var s=c.Read();Check(s.Ok);int opens=r.Ec.Opens;
            Check(!c.Toggle(s.Fence).Ok&&r.Ec.Opens==opens);Check(c.Apply(c.Read().Fence,60).Ok);Check(c.Toggle(c.Read().Fence).Ok);Check(c.Toggle(c.Read().Fence).Raw==153);
        });
        Test("parent or power blocks before opening",()=>{var r=new Rig();var c=r.New();var s=c.Read();r.Ready=false;int opens=r.Ec.Opens;Check(!c.Apply(s.Fence,50).Ok&&r.Ec.Opens==opens);Check(!c.Read().Ok&&r.Ec.Opens==opens);});
        foreach(var boundary in new[]{"latch","brightness","read-3","dispose"})Test("readiness rechecked at "+boundary,()=>{
            var r=new Rig();var c=r.New();var s=c.Read();r.Ec.After=e=>{if(e==boundary)r.Ready=false;};var result=c.Apply(s.Fence,50);Check(!result.Ok&&result.Fence=="");Check(r.Store.Data.Count==0);
            if(boundary=="latch")Check(!r.Ec.Events.Any(e=>e.StartsWith("write-4e-")));Check(r.Ec.Opens==r.Ec.Disposals);
        });
        Test("suspend during latch prevents brightness write",()=>{
            var r=new Rig();var c=r.New();var s=c.Read();r.Ec.After=e=>{if(e=="latch")c.Suspend();};Check(!c.Apply(s.Fence,50).Ok);Check(!r.Ec.Events.Any(e=>e.StartsWith("write-4e-")));
        });
        Test("lifecycle never replays or restores persistent brightness",()=>{
            var r=new Rig();var c=r.New();Check(c.Apply(c.Read().Fence,50).Ok);var old=c.Read();int opens=r.Ec.Opens;byte raw=r.Ec.Raw;
            c.Suspend();Check(!c.Apply(old.Fence,80).Ok);c.Resume();Check(!c.Apply(old.Fence,80).Ok);Check(c.Release().Ok);c.Shutdown();Check(!c.Read().Ok);Check(r.Ec.Opens==opens&&r.Ec.Raw==raw);
        });
        Test("router display domain leaves corrupt charge journals untouched",()=>{
            var r=new Rig();r.Store.RejectBattery=true;r.Store.Data[Controller.RecoveryFile]="broken";int batteries=0,displays=0;
            var router=new HostDomainRouter(()=>{batteries++;throw new Exception("charge initialized");},ready=>{displays++;return new(r.Id,r.Store,r.Ec.Open,ready);});
            router.Tick();Check(displays==0&&batteries==0);var s=Send(router,"secondaryDisplay.get");Check(s.Ok);
            Check(Send(router,"secondaryDisplay.set",s.Fence,50).Ok);Check(Send(router,"secondaryDisplay.release").Ok);
            router.Suspend();router.Resume();router.Shutdown();Check(batteries==0&&displays==1&&r.Store.Data[Controller.RecoveryFile]=="broken");
        });
        Test("suspended router creates neither domain",()=>{
            int created=0;var router=new HostDomainRouter(()=>{created++;throw new Exception();},_=>{created++;throw new Exception();});router.Suspend();
            Throws(()=>router.Dispatch(Request("get")));Throws(()=>Send(router,"secondaryDisplay.get"));router.Tick();router.Shutdown();Check(created==0);
        });
        Test("first battery request retains journal recovery exactly once",()=>{
            var r=new Rig();var route=Policy.Resolve(r.Id)!;Check(route.Software);var charge=new Charge(route){Raw=0x55};int created=0;
            r.Store.Save(Controller.SettingsFile,new Preference(1,r.Id.Key,route.RouteId,80,true));
            r.Store.Save(Controller.RecoveryFile,new Recovery(1,r.Id.Key,route.RouteId,0xaa,0x55,true));
            var router=new HostDomainRouter(()=>{created++;return new(r.Id,r.Store,_=>charge,()=> (true,true,90));},ready=>new(r.Id,r.Store,r.Ec.Open,ready));
            Check(Send(router,"secondaryDisplay.get").Ok&&created==0&&charge.Writes==0);
            var s=(Snapshot)router.Dispatch(Request("get"));Check(s.Ok&&created==1&&charge.Writes==2&&charge.Raw==0x55);
            router.Dispatch(Request("get"));Check(created==1&&charge.Writes==2);router.Suspend();Check(charge.Raw==0xaa);router.Resume();Check(charge.Raw==0x55);router.Shutdown();Check(charge.Raw==0xaa);
        });
        Test("router rejects unknown domain and signed short overflow",()=>{
            var r=new Rig();int battery=0;var router=new HostDomainRouter(()=>{battery++;throw new Exception();},ready=>new(r.Id,r.Store,r.Ec.Open,ready));
            Throws(()=>router.Dispatch(Request("secondaryDisplay.bad")));Throws(()=>router.Dispatch(Request("other")));Throws(()=>Send(router,"secondaryDisplay.set","stale",40000));Check(battery==0&&r.Ec.Opens==0);
        });
        Test("host identity mismatch is refused before dispatch",()=>{Throws(()=>RuntimeContract.Validate(Request("secondaryDisplay.get")));Check(RuntimeContract.Protocol==2);});
        Console.WriteLine(JsonSerializer.Serialize(new {schemaVersion=1,status="PASS",cases,checks,scope="actual production controllers/router with memory EC/store/parent boundaries",physicalAcceptance=false,productHostStarted=false},Policy.Json));
    }
}
