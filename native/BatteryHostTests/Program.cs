using System.Text.Json;
using YeManBatteryHost;

internal sealed class MemoryStore() : Store("unused-by-mock")
{
    public readonly Dictionary<string,string> Data=[];
    public string? FailSave;
    public bool FailClear;
    public override T? Load<T>(string name) where T:default => Data.TryGetValue(name,out var value)?JsonSerializer.Deserialize<T>(value,Policy.Json):default;
    public override void Save<T>(string name,T value){if(FailSave==name)throw new IOException("mock disk full");Data[name]=JsonSerializer.Serialize(value,Policy.Json);}
    public override void Clear(string name){if(FailClear)throw new IOException("mock clear failed");Data.Remove(name);}
}
internal sealed class Fake(Route route) : IChargeProvider
{
    public int Raw=route.Software?(route.RouteId=="ayaneo-ceii-bypass"?0x65:0xaa):100;
    public int Writes,Reads;
    public bool FailRead,ThrowAfterWrite,IgnoreWrite;
    public bool CanReadLimit=>route.RouteId!="asus-ally-acpi"&&!route.Software;
    public Reading Read(){Reads++;if(FailRead)throw new IOException("mock read failed");return new(CanReadLimit?(route.Fixed?(Raw==1?80:100):Raw):null,route.RouteId=="asus-ally-acpi"?null:Raw,route.Software?Raw==(route.RouteId=="ayaneo-ceii-bypass"?1:0x55):null);}
    public void Write(int value,Reading before){Writes++;if(!IgnoreWrite)Raw=route.Software?(value==1?(route.RouteId=="ayaneo-ceii-bypass"?1:0x55):(route.RouteId=="ayaneo-ceii-bypass"?0x65:0xaa)):route.Fixed?(value==80?1:0):value;if(ThrowAfterWrite)throw new IOException("mock acknowledgement lost");}
    public void Dispose(){}
}
internal sealed class Ports(ushort expected,byte initial) : IEcAccess
{
    public byte Value=initial;
    public int Writes;
    public byte Read(ushort register){if(register!=expected)throw new Exception("wrong EC address space");return Value;}
    public void Write(ushort register,byte value){if(register!=expected)throw new Exception("write outside reviewed EC address");Writes++;Value=value;}
    public void Dispose(){}
}
internal sealed class Oxp3WmiFixture : IOxp3WmiEc
{
    public byte Limit=80;
    public int Reads,Writes;
    public bool FailRead,FailWrite,IgnoreWrite;
    public readonly List<(ushort Register,byte Value)> Log=[];
    public byte ReadByte(ushort register)
    {
        Reads++;if(FailRead)throw new IOException("mock OXP3 WMI read failed");
        if(register==Oxp3WmiProvider.BypassRegister)return 0;
        if(register!=Oxp3WmiProvider.LimitRegister)throw new Exception("wrong OXP3 WMI register");
        return Limit;
    }
    public void WriteByte(ushort register,byte value)
    {
        Writes++;Log.Add((register,value));if(FailWrite)throw new IOException("mock OXP3 WMI write failed");
        if(!IgnoreWrite&&register==Oxp3WmiProvider.LimitRegister)Limit=value;
    }
    public void Dispose(){}
}
internal sealed class Oxp3WmiMethodFixture : IOxp3WmiMethod
{
    public byte Limit=80;
    public int Reads,Writes,DisposeCalls;
    public bool FailRead,FailWrite,NullRead,NullWrite,ThrowDispose;
    public string? ReadResponse,WriteResponse;
    public readonly List<(string Method,uint Arg)> Calls=[];
    public string? Invoke(string method,object[] arguments)
    {
        var arg=Convert.ToUInt32(arguments[0]);Calls.Add((method,arg));
        if(method=="ReadECReg")
        {
            Reads++;if(FailRead)throw new IOException("mock OXP3 WMI method failed");
            if(NullRead)return null;
            return ReadResponse??("0,"+Limit.ToString("X2"));
        }
        if(method=="WriteECReg")
        {
            Writes++;if(FailWrite)throw new IOException("mock OXP3 WMI write method failed");
            if(NullWrite)return null;
            if(WriteResponse is not null)return WriteResponse;
            Limit=(byte)(arg>>16);return "0";
        }
        throw new Exception("unexpected OXP3 WMI method");
    }
    public void Dispose(){DisposeCalls++;if(ThrowDispose)throw new IOException("mock OXP3 WMI method disposal failed");}
}
internal sealed class Rig
{
    public readonly Identity Id;
    public readonly Route Route;
    public readonly MemoryStore Store=new();
    public readonly Fake Device;
    public bool Present=true,Ac=true;
    public int? Percent=75;
    public int Opens;
    public Controller Controller;
    public Rig(string routeId="oxp-x1-amd-ec")
    {
        Route=Policy.Routes.First(r=>r.RouteId==routeId);
        Id=new(Route.Identity.Manufacturers[0],Route.Identity.Field=="boardProduct"?Route.Identity.Values[0]:"board",Route.Identity.Field=="systemProduct"?Route.Identity.Values[0]:"system","AMD Ryzen HX 370","2.10",2,80);
        Device=new(Route);Controller=New();
    }
    public Controller New()=>new(Id,Store,_=>{Opens++;return Device;},()=> (Present,Ac,Percent));
    public Snapshot Apply(int target)=>Controller.Apply(Controller.Read().Fence,target);
}
internal static class Program
{
    static int count;
    static string? selectedCase;
    static void Check(bool value,string reason="assertion failed"){if(!value)throw new Exception(reason);}
    static void Test(string name,Action test)
    {
        if(selectedCase is not null&&!string.Equals(name,selectedCase,StringComparison.Ordinal))return;
        test();count++;Console.WriteLine("PASS "+name);
    }
    static void Main(string[] args)
    {
        if(args.Length==1&&args[0]=="--in-process"){InProcessFixture.Run();return;}
        if(args.Length!=0)
        {
            if(args.Length!=2||args[0]!="--case"||string.IsNullOrWhiteSpace(args[1]))
            {
                Console.Error.WriteLine("FAIL invalid arguments; use no arguments, --in-process or --case <exact existing Test name>; count=0.");
                Environment.ExitCode=1;return;
            }
            selectedCase=args[1];
        }
        Test("unified catalog selects every exact actual battery rule and trusted receipt",()=>{
            Check(Policy.Routes.Length==41);
            foreach(var route in Policy.Routes)foreach(var value in route.Identity.Values){
                var id=new Identity(route.Identity.Manufacturers[0],route.Identity.Field=="boardProduct"?value:"board",route.Identity.Field=="systemProduct"?value:"system","HX 370","2.10",2,route.RouteId=="gpd-win-mini-2025"?7:80);
                var binding=Policy.ResolveCapability(id);Check(binding is not null&&ReferenceEquals(binding.Consumer,route));
                Check(binding!.Descriptor.Provider==route.RouteId&&binding.Descriptor.DeviceClass==route.Identity.DeviceClass);
                var isFrozen=YeManFanBackend.Capabilities.HcDeviceCapabilityCatalog.Devices.Any(d=>d.ConsumerClass=="HandheldCompanion.Devices."+route.Identity.DeviceClass);
                Check(binding.Descriptor.Origin==(isFrozen?"frozen-HC-identity":"reviewed-native-extension"));
                Check(binding.Descriptor.DeviceId==(isFrozen?"hc.device.":"ymcc.device.")+route.Identity.DeviceClass);
                var store=new MemoryStore();var fake=new Fake(route);var c=new Controller(id,store,_=>fake,()=> (true,true,90));var result=c.Read();
                Check(result.Ok&&result.Supported&&fake.Reads>0,"selected rule did not reach actual controller/provider read");
                var applied=c.Apply(result.Fence,80);Check(applied.Ok&&fake.Writes>0,"selected rule did not reach actual controller/provider write");
                c.Shutdown();if(route.Software)Check(fake.Raw==(route.RouteId=="ayaneo-ceii-bypass"?0x65:0xaa),"software bypass was not restored on shutdown");
                Check(result.CapabilityId==binding.Descriptor.DeviceId&&result.CapabilityRuleId==binding.Descriptor.RuleId);
                Check(result.CapabilityDeclaredByFrozenHc==binding.Descriptor.DeclaredByFrozenHc);
                Check(result.CapabilityCatalogSha256==YeManFanBackend.Capabilities.HcBatteryCapabilityCatalog.CatalogSha256);
                var opens=0;var unknown=id with {Manufacturer="Unreviewed Vendor"};var rejected=new Controller(unknown,new MemoryStore(),_=>{opens++;return fake;},()=> (true,true,70));Check(!rejected.Read().Supported&&opens==0);
            }
            Check(Policy.Resolve(new("SUIPLAY","SUIPlay 0X1","","","",1,80)) is null,"research-only inherited protocol was enabled");
            var wrongCpu=new Identity("GPD","G1617-02","","HX 365","2.10",2,7);Check(Policy.Resolve(wrongCpu) is null);
        });
        Test("unified catalog rejects stale document and mismatched consumer registration",()=>{
            var hash=YeManFanBackend.Capabilities.HcBatteryCapabilityCatalog.RuleDocumentSha256;
            bool Reject(Action run){try{run();return false;}catch(InvalidOperationException){return true;}}
            Check(Reject(()=>YeManFanBackend.Capabilities.HcBatteryCapabilityCatalog.Bind(new string('0',64),Policy.Routes,r=>r.RouteId,r=>r.Identity.DeviceClass)));
            Check(Reject(()=>YeManFanBackend.Capabilities.HcBatteryCapabilityCatalog.Bind(hash,Policy.Routes.Skip(1).ToArray(),r=>r.RouteId,r=>r.Identity.DeviceClass)));
            var copied=Policy.Routes.ToArray();copied[0]=copied[1];Check(Reject(()=>YeManFanBackend.Capabilities.HcBatteryCapabilityCatalog.Bind(hash,copied,r=>r.RouteId,r=>r.Identity.DeviceClass)));
            copied=Policy.Routes.ToArray();var d=YeManFanBackend.Capabilities.HcBatteryCapabilityCatalog.Bind(hash,copied,r=>r.RouteId,r=>r.Identity.DeviceClass);copied[0]=copied[1];Check(ReferenceEquals(d.Bindings[0].Consumer,Policy.Routes[0]));
        });
        Test("runtime contract rejects old, mixed and malformed clients before dispatch",()=>{
            using var good=JsonDocument.Parse(JsonSerializer.Serialize(new {protocol=RuntimeContract.Protocol,sourceId=RuntimeContract.SourceId}));
            RuntimeContract.Validate(good.RootElement);
            foreach(var text in new[]{"{}","[]","null","{\"protocol\":\"2\"}","{\"protocol\":2,\"sourceId\":false}","{\"protocol\":1,\"sourceId\":\"old\"}","{\"protocol\":2,\"sourceId\":\"old\"}"}){
                using var bad=JsonDocument.Parse(text);bool rejected=false;
                try{RuntimeContract.Validate(bad.RootElement);}catch(IOException){rejected=true;}
                Check(rejected,"mismatched runtime admitted");
            }
            var identity=JsonSerializer.SerializeToElement(RuntimeContract.Identity(),Policy.Json);
            Check(identity.GetProperty("sourceId").GetString()==RuntimeContract.SourceId);
            Check(identity.GetProperty("implementation").GetString()=="msi-cg3em-ap-unknown-v2");
        });
        Test("exact family identity, no brand fallback",()=>{
            foreach(var route in Policy.Routes)foreach(var name in route.Identity.Values){var id=new Identity(route.Identity.Manufacturers[0],route.Identity.Field=="boardProduct"?name:"",route.Identity.Field=="systemProduct"?name:"","HX 370","2.10",2,7);Check(Policy.Resolve(id)?.RouteId==route.RouteId);}
            foreach(var name in new[]{"ONEXPLAYER X2","ONEXPLAYER X2Mini PRO","ONEXPLAYER 3","unknown"})Check(Policy.Resolve(new("ONE-NETBOOK",name,"","","",1,80)) is null);
            Check(Policy.Resolve(new("AOKZOE","AOKZOE A1X","","","",1,80)) is null);
            // Fixed newer X2 returns IsBatteryProtectionSupported=false. Exercise the
            // actual controller, not just the route table: neither identity field may
            // borrow X1/OXP3 charging, even when a battery and AC are present.
            var inert=new Rig();
            foreach(var manufacturer in new[]{"ONE-NETBOOK","ONE-NETBOOK TECHNOLOGY CO., LTD."})
            foreach(var product in new[]{"ONEXPLAYER X2","ONEXPLAYER X2Mini PRO"})
            foreach(var fields in new[]{(Board:product,System:product),(Board:product,System:""),(Board:"",System:product)}){
                var opens=0;
                var identity=new Identity(manufacturer,fields.Board,fields.System,"AMD Ryzen AI MAX+ 395","2.10",2,80);
                var store=new MemoryStore();
                var consumer=new Controller(identity,store,_=>{opens++;return inert.Device;},()=> (true,true,70));
                var read=consumer.Read();
                Check(!read.Supported,"exact X2 source-negative model was admitted");
                Check(!consumer.Apply(read.Fence,80).Ok&&!consumer.Apply(read.Fence,1).Ok,"exact X2 limit/bypass request accepted");
                Check(opens==0&&inert.Device.Reads==0&&inert.Device.Writes==0&&store.Data.Count==0,"exact X2 rejection touched provider or recovery state");
            }
        });
        Test("product minimum 50 preserves stricter hardware choices and rejects lower writes",()=>{
            foreach(var route in Policy.Routes){
                Check(route.Choices.All(n=>n>=50));Check(!Policy.ValueAllowed(route,45));
                if(!route.Fixed)Check(route.Choices[0]==Math.Max(50,route.Min));
                var r=new Rig(route.RouteId);Check(!r.Apply(45).Ok&&r.Device.Writes==0);
            }
            var msi=new Rig("msi-claw-cg3em-ap");
            msi.Store.Save(Controller.SettingsFile,new Preference(1,msi.Id.Key,msi.Route.RouteId,40,true));
            var c=msi.New();c.StartSaved();Check(msi.Device.Writes==0&&c.Read().RequestedLimit is null);
        });
        Test("fixed vs percent choices and BIOS gates",()=>{
            var fixedRoute=new Rig("lenovo-go-conservation").Route;Check(fixedRoute.Choices.SequenceEqual(new[]{80,100}));Check(!Policy.ValueAllowed(fixedRoute,75));
            var r=new Rig();Check(!Policy.FirmwareAllowed(r.Route,r.Id with {EcMinor=255}));Check(!Policy.FirmwareAllowed(r.Route,r.Id with {EcMinor=2}));
            var g=new Rig("gpd-win-mini-2025");Check(Policy.FirmwareAllowed(g.Route,g.Id with {EcMinor=7}));Check(!Policy.FirmwareAllowed(g.Route,g.Id with {EcMinor=8}));Check(!Policy.FirmwareAllowed(g.Route,g.Id with {Bios="12.10",EcMinor=7}));Check(g.Route.Choices[0]==60);
        });
        Test("read and unknown devices never write or open providers",()=>{
            var r=new Rig();r.Controller.Read();Check(r.Device.Writes==0);r.Present=false;Check(!r.Controller.Read().Supported);var before=r.Opens;r.Apply(80);Check(r.Opens==before);
            var called=false;var unknown=new Controller(r.Id with {BoardProduct="unknown"},r.Store,_=>{called=true;return r.Device;},()=> (true,true,70));unknown.Read();Check(!called);Check(r.Device.Writes==0);
        });
        Test("failed read and invalid target never write",()=>{var r=new Rig();r.Device.FailRead=true;Check(!r.Apply(80).Ok);Check(r.Device.Writes==0);r.Device.FailRead=false;Check(!r.Apply(77).Ok);Check(r.Device.Writes==0);});
        Test("hardware value 77 is preserved on read",()=>{var r=new Rig();r.Device.Raw=77;Check(r.Controller.Read().CurrentLimit==77);Check(r.Device.Writes==0);});
        Test("stale fences reject external state changes and replay",()=>{var r=new Rig();var s=r.Controller.Read();r.Device.Raw=90;Check(!r.Controller.Apply(s.Fence,80).Ok);Check(r.Device.Writes==0);s=r.Controller.Read();Check(r.Controller.Apply(s.Fence,80).Ok);Check(!r.Controller.Apply(s.Fence,80).Ok);Check(r.Device.Writes==1);});
        Test("same firmware target avoids redundant hardware writes",()=>{var r=new Rig();Check(r.Apply(100).Ok);Check(r.Device.Writes==0);Check(r.Apply(80).CurrentLimit==80);r.Controller.StartSaved();Check(r.Device.Writes==1);});
        Test("ASUS reports acknowledgement with unknown current limit",()=>{var r=new Rig("asus-ally-acpi");var s=r.Apply(80);Check(s.Ok&&s.CurrentLimit is null&&s.RequestedLimit==80&&s.Verification=="acknowledged");});
        Test("MSI disable preserves lower bits; enable encodes target",()=>{Check(Policy.MsiEncode(0xd0,100)==0x50);Check(Policy.MsiEncode(0xd0,60)==0xbc);});
        Test("uncertain write is fenced across restart; refresh disarms old replay",()=>{
            var r=new Rig();r.Apply(60);r.Device.ThrowAfterWrite=true;Check(!r.Apply(80).Ok);r.Device.ThrowAfterWrite=false;var fresh=r.New();Check(!fresh.Read().Supported);fresh.StartSaved();Check(r.Device.Writes==2);Check(fresh.Acknowledge().Supported);fresh.StartSaved();Check(r.Device.Writes==2);Check(r.Store.Load<Preference>(Controller.SettingsFile)?.Managed==false);
        });
        Test("readback mismatch never confirms success",()=>{var r=new Rig();r.Device.IgnoreWrite=true;var s=r.Apply(80);Check(!s.Ok&&s.CurrentLimit==100&&s.Verification=="unconfirmed");Check(!r.Controller.Read().Supported);});
        Test("journal failure precedes every firmware write",()=>{var r=new Rig();r.Store.FailSave=Controller.RecoveryFile;Check(!r.Apply(80).Ok);Check(r.Device.Writes==0);});
        Test("settings failure after hardware change retains recovery",()=>{var r=new Rig();r.Store.FailSave=Controller.SettingsFile;Check(!r.Apply(80).Ok);Check(r.Device.Raw==80&&r.Store.Data.ContainsKey(Controller.RecoveryFile));Check(!r.New().Read().Supported);});
        Test("clear failure retains unresolved transaction",()=>{var r=new Rig();r.Store.FailClear=true;Check(!r.Apply(80).Ok);Check(r.Store.Data.ContainsKey(Controller.RecoveryFile));});
        foreach(var route in new[]{"ayaneo-cec-bypass","ayaneo-ceii-bypass"})Test(route+" hysteresis, DC, unknown SOC and release",()=>{
            var r=new Rig(route);var original=r.Device.Raw;Check(r.Apply(80).Ok);Check(r.Device.Writes==0);r.Percent=80;r.Controller.Tick();Check(r.Device.Writes==1);r.Percent=79;r.Controller.Tick();Check(r.Device.Writes==1);r.Percent=77;r.Controller.Tick();Check(r.Device.Writes==2&&r.Device.Raw==original);r.Percent=90;r.Controller.Tick();r.Ac=false;r.Controller.Tick();Check(r.Device.Raw==original);r.Ac=true;r.Controller.Tick();r.Percent=null;r.Controller.Tick();Check(r.Device.Raw==original);Check(r.Controller.Release().Ok);r.Percent=100;r.Controller.Tick();Check(r.Device.Raw==original&&!r.Store.Data.ContainsKey(Controller.RecoveryFile));
        });
        Test("invalid input and failed preflight do not stop an existing software policy",()=>{
            var r=new Rig("ayaneo-cec-bypass");r.Percent=90;r.Apply(80);var writes=r.Device.Writes;
            Check(!r.Apply(77).Ok);Check(r.Device.Writes==writes&&r.Device.Raw==0x55);
            r.Device.FailRead=true;Check(!r.Controller.Apply("bad",80).Ok);r.Device.FailRead=false;
            r.Percent=77;r.Controller.Tick();Check(r.Device.Raw==0xaa&&r.Device.Writes==writes+1);
            Check(r.Controller.Read().Supported&&r.Controller.Read().Managed);
        });
        Test("explicit software stop records 100 and never restarts the old target",()=>{
            var r=new Rig("ayaneo-cec-bypass");r.Percent=90;r.Apply(80);Check(r.Device.Raw==0x55);
            var stopped=r.Apply(100);Check(stopped.Ok&&!stopped.Managed&&stopped.RequestedLimit==100&&r.Device.Raw==0xaa);
            var writes=r.Device.Writes;r.Controller=r.New();r.Controller.StartSaved();Check(r.Device.Writes==writes&&!r.Controller.Read().Managed);
            var fresh=new Rig("ayaneo-ceii-bypass");Check(fresh.Apply(100).RequestedLimit==100&&fresh.Device.Writes==0);
        });
        Test("software suspend/resume and normal parent shutdown restore original",()=>{var r=new Rig("ayaneo-cec-bypass");r.Percent=90;r.Apply(80);Check(r.Device.Raw==0x55);r.Controller.Suspend();Check(r.Device.Raw==0xaa);r.Controller.Resume();Check(r.Device.Raw==0x55);r.Controller.Shutdown();Check(r.Device.Raw==0xaa);});
        Test("journalled crash recovery restores before saved policy restarts",()=>{var r=new Rig("ayaneo-cec-bypass");r.Percent=90;r.Apply(80);var before=r.Device.Writes;r.Controller=r.New();r.Controller.RecoverSoftware();Check(r.Device.Writes==before+2);r.Controller.StartSaved();Check(r.Device.Writes==before+2);r.Controller.Release();});
        Test("never takes over existing paused bypass",()=>{var r=new Rig("ayaneo-cec-bypass");r.Device.Raw=0x55;Check(!r.Apply(80).Ok);Check(r.Device.Writes==0);});
        Test("unknown external state stops policy and cannot be overwritten by release",()=>{var r=new Rig("ayaneo-cec-bypass");r.Apply(80);r.Device.Raw=42;r.Controller.Tick();Check(!r.Controller.Read().Supported);Check(!r.Controller.Release().Ok);Check(r.Device.Raw==42&&r.Device.Writes==0);});
        Test("partial bypass write is recovered, failure is reported",()=>{var r=new Rig("ayaneo-cec-bypass");r.Percent=90;r.Device.ThrowAfterWrite=true;Check(!r.Apply(80).Ok);Check(r.Device.Raw==0xaa);r.Device.ThrowAfterWrite=false;Check(r.Controller.Release().Ok);});
        Test("software journal save failure does not lose previous ownership",()=>{var r=new Rig("ayaneo-cec-bypass");r.Percent=90;r.Apply(80);r.Store.FailSave=Controller.RecoveryFile;r.Percent=70;r.Controller.Tick();Check(r.Device.Raw==0xaa);Check(!r.Controller.Read().Supported);});
        Test("malformed storage is fail closed and does not kill reads",()=>{var r=new Rig();r.Store.Data[Controller.RecoveryFile]="broken";r.Controller=r.New();r.Controller.RecoverSoftware();Check(!r.Controller.Read().Supported);Check(!r.Apply(80).Ok);Check(r.Device.Writes==0);});
        Test("invalid software journal never restores guessed bytes",()=>{var r=new Rig("ayaneo-cec-bypass");r.Store.Save(Controller.RecoveryFile,new Recovery(1,r.Id.Key,r.Route.RouteId,42,0x55,true));r.Controller=r.New();r.Controller.RecoverSoftware();Check(!r.Controller.Release().Ok);Check(r.Device.Writes==0);});
        Test("firmware mismatch never opens a low-level provider",()=>{
            var r=new Rig();var controller=new Controller(r.Id with {EcMinor=255},r.Store,_=>{throw new Exception("must not open");},()=> (true,true,70));
            Check(!controller.Read().Supported);Check(!controller.Apply("",80).Ok);
        });
        Test("ASUS real adapter validates presence and write acknowledgement without a fabricated threshold",()=>{
            var writes=0;using var p=new AsusProvider((method,value)=>{if(method==0x53545344)return 0x10001;if(method==0x53564544&&value==80){writes++;return 1;}throw new Exception("wrong ASUS request");});
            Check(p.Read().Limit is null&&!p.CanReadLimit);p.Write(80,p.Read());Check(writes==1&&p.Read().Limit is null);
            using var absent=new AsusProvider((_,_)=>0);try{absent.Read();throw new Exception("absence accepted");}catch(IOException){}
            using var rejected=new AsusProvider((_,_)=>0);try{rejected.Write(80,new(null,null));throw new Exception("rejection ignored");}catch(IOException){}
        });
        Test("MSI real adapter read-modify-write preserves bits and requires a successful read",()=>{
            byte raw=0xd0;var writes=0;var fail=false;
            using var p=new MsiProvider((method,packet)=>{
                Check(packet.Length==32&&packet[0]==215);
                if(method=="Get_Data")return new byte[]{fail?(byte)0:(byte)1,raw};
                Check(method=="Set_Data");writes++;raw=packet[1];return [];
            });
            var before=p.Read();Check(before.Limit==80);p.Write(100,before);Check(raw==0x50&&p.Read().Limit==100);before=p.Read();p.Write(60,before);Check(raw==0xbc&&p.Read().Limit==60);
            fail=true;try{p.Write(80,new(60,raw));throw new Exception("failed read did not block write");}catch(IOException){}Check(writes==2);
        });
        Test("real EC adapters use separate CEc, CEii, X1 and GPD address spaces",()=>{
            foreach(var (name,address,initial,target,expected,direct) in new (string,ushort,byte,int,byte,bool)[]{
                ("ayaneo-cec-bypass",0x1e,0xaa,1,0x55,false),("ayaneo-ceii-bypass",0xd1d1,0x65,1,1,true),
                ("oxp-x1-amd-ec",0x04a3,100,80,80,true),("gpd-win-mini-2025",0x1d,100,80,80,false)}){
                var route=new Rig(name).Route;var ports=new Ports(address,initial);using var p=new EcProvider(route,d=>{Check(d==direct);return ports;});
                p.Write(target,p.Read());Check(ports.Value==expected&&ports.Writes==1);
                var stale=p.Read();ports.Value=initial;try{p.Write(target,stale);throw new Exception("EC race accepted");}catch(IOException){}Check(ports.Writes==1);
                ports.Value=255;try{p.Read();throw new Exception("unknown EC byte accepted");}catch(IOException){}
            }
        });
        Test("real atomic storage roundtrip stays in a temporary test directory",()=>{var directory=Path.Combine(Path.GetTempPath(),"YMCC-BatteryTest-"+Guid.NewGuid().ToString("N"));var store=new Store(directory);try{store.Save("test.json",new[]{1,2});Check(store.Load<int[]>("test.json")!.SequenceEqual(new[]{1,2}));store.Save("test.json",new[]{3});Check(store.Load<int[]>("test.json")![0]==3);store.Clear("test.json");Check(store.Load<int[]>("test.json") is null);}finally{Directory.Delete(directory);}});
        foreach(var mode in new[]{"direct","throw","null","missing-data","throw-data"})
            Test("MSI WMI parameter contract: "+mode,()=>{
                var wmi=new MsiFixture(mode);using var provider=new MsiProvider(wmi);
                var rig=new Rig("msi-claw-wmi");var controller=new Controller(rig.Id,rig.Store,_=>new MsiProvider(wmi),()=> (true,true,60));
                var before=controller.Read();Check(before.Ok&&before.CurrentLimit==80);
                var after=controller.Apply(before.Fence,60);Check(after.Ok&&after.CurrentLimit==60&&after.Verification=="readback");
                Check(wmi.Writes==1&&wmi.Raw==0xbc);Check(mode=="direct"?wmi.Templates==0:wmi.Templates==wmi.ParameterCalls);
                Check(wmi.Bags.All(b=>b.Disposed),"WMI objects leaked");
            });
        Test("MSI invalid Get_WMI shape stops before hardware calls",()=>{
            var wmi=new MsiFixture("throw"){BrokenTemplate=true};using var p=new MsiProvider(wmi);
            try{p.Read();throw new Exception("bad template accepted");}catch(IOException e){Check(e.Message.Contains("get-wmi-template"));}
            Check(wmi.Reads==0&&wmi.Writes==0&&wmi.Templates==1);Check(wmi.Bags.All(b=>b.Disposed));
        });
        Test("MSI failed flag blocks writes through full WMI fallback",()=>{
            var wmi=new MsiFixture("missing-data"){FailedRead=true};using var p=new MsiProvider(wmi);
            try{p.Write(80,new(80,0xd0));throw new Exception("write accepted after failed read");}catch(IOException){}
            Check(wmi.Writes==0&&wmi.Reads==1);
        });
        Test("MSI invoked write is never retried after lost acknowledgement",()=>{
            var wmi=new MsiFixture("throw"){ThrowAfterWrite=true};using var p=new MsiProvider(wmi);
            try{p.Write(60,p.Read());throw new Exception("lost reply accepted");}catch(IOException e){Check(e.Message.Contains("Set_Data/invoke"));}
            Check(wmi.Writes==1&&wmi.Raw==0xbc);Check(wmi.Bags.All(b=>b.Disposed));
        });
        Test("MSI accepted packet still requires changed firmware readback",()=>{
            var wmi=new MsiFixture("null"){IgnoreWrite=true};var r=new Rig("msi-claw-wmi");
            var c=new Controller(r.Id,r.Store,_=>new MsiProvider(wmi),()=> (true,true,60));var result=c.Apply(c.Read().Fence,60);
            Check(!result.Ok&&result.CurrentLimit==80&&result.Verification=="unconfirmed"&&wmi.Writes==1);
            Check(r.Store.Data.ContainsKey(Controller.RecoveryFile));
        });
        Test("battery diagnostics deduplicate, rotate and tolerate an unwritable directory",()=>{
            var directory=Path.Combine(Path.GetTempPath(),"YMCC-BatteryTrace-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(directory);
            var log=Path.Combine(directory,"battery-charge-host.log");var blocker=Path.Combine(directory,"file");
            try{
                TraceLog.Configure(directory);TraceLog.Write("fixture",new {value=80});TraceLog.Write("fixture",new {value=80});Check(File.ReadAllLines(log).Length==1);
                using(var row=JsonDocument.Parse(File.ReadAllLines(log)[0]))Check(row.RootElement.GetProperty("event").GetString()=="fixture");
                File.WriteAllText(log,new string('x',256*1024));TraceLog.Write("fixture",new {value=60});Check(File.Exists(log+".1")&&File.ReadAllLines(log).Length==1);
                File.WriteAllText(blocker,"blocked");TraceLog.Configure(blocker);TraceLog.Error("nonfatal",new IOException("test"));
            }finally{TraceLog.Configure(directory);foreach(var file in Directory.GetFiles(directory))File.Delete(file);Directory.Delete(directory);}
        });
        Test("CG3EM selects reviewed AP protocol; earlier Claw boards keep legacy protocol",()=>{
            var id=new Identity("Micro-Star International Co., Ltd.","MS-1T91","Claw 8 EX AI+ CG3EM Launch Pack","Intel Arc G3 Extreme","E1T91IMS.105",255,255);
            var route=Policy.Resolve(id)!;Check(route.RouteId=="msi-claw-cg3em-ap"&&Policy.FirmwareAllowed(route,id));Check(route.Choices.SequenceEqual(Enumerable.Range(10,11).Select(i=>i*5)));
            var legacy=Policy.Routes.Where(r=>r.RouteId=="msi-claw-wmi").ToArray();Check(legacy.Length==3);
            foreach(var board in legacy.SelectMany(r=>r.Identity.Values))Check(Policy.Resolve(id with {BoardProduct=board})?.RouteId=="msi-claw-wmi");
            Check(Policy.Resolve(id with {BoardProduct="MS-1T91-UNKNOWN"}) is null);
        });
        Test("field regression: legacy 0x80 remains invalid, CG3EM reads actual AP byte 5",()=>{
            using var legacy=new MsiProvider((_,_)=>new byte[]{1,128});try{legacy.Read();throw new Exception("legacy unknown threshold accepted");}catch(IOException){}
            var f=new MsiApFixture();using var p=new MsiApProvider(f);Check(p.Read().Limit==100&&p.Read().Raw==0xe4&&f.Writes==0);
            Check(f.Methods.All(m=>m is "Get_WMI" or "Get_AP"));
        });
        Test("captured .105 AP 0x80 is unknown but permits explicit verified Apply",()=>{
            foreach(var target in new[]{50,60,80,100}){
                var f=new MsiApFixture();Convert.FromHexString("010000C680800000000000000000000000000000000000000000000000000000").CopyTo(f.Packet,0);
                var r=new Rig("msi-claw-cg3em-ap");var c=new Controller(r.Id,r.Store,_=>new MsiApProvider(f),()=> (true,true,74));
                var before=c.Read();Check(before.Ok&&before.Supported&&before.CurrentLimit is null&&before.Verification=="unknown"&&f.Writes==0&&r.Store.Data.Count==0);
                var after=c.Apply(before.Fence,target);Check(after.Ok&&after.CurrentLimit==target&&after.Verification=="readback"&&f.Writes==1);
                Check(f.Sent[0][3]==0xc6&&f.Sent[0][4]==0x80&&f.Sent[0][5]==(0x80|target));
                Check(!r.Store.Data.ContainsKey(Controller.RecoveryFile));
            }
        });
        Test("captured 0x80 ignored write remains unknown and journalled across restart",()=>{
            var f=new MsiApFixture{IgnoreWrite=true};f.Packet[5]=0x80;var r=new Rig("msi-claw-cg3em-ap");
            Controller New()=>new(r.Id,r.Store,_=>new MsiApProvider(f),()=> (true,true,74));
            var c=New();var after=c.Apply(c.Read().Fence,80);
            Check(!after.Ok&&!after.Supported&&after.CurrentLimit is null&&after.Verification=="unconfirmed"&&f.Writes==1);
            Check(r.Store.Data.ContainsKey(Controller.RecoveryFile));New().StartSaved();Check(f.Writes==1);
        });
        Test("unknown AP state never triggers saved startup or resume writes",()=>{
            var f=new MsiApFixture();var r=new Rig("msi-claw-cg3em-ap");
            Controller New()=>new(r.Id,r.Store,_=>new MsiApProvider(f),()=> (true,true,74));
            var c=New();Check(c.Apply(c.Read().Fence,80).Ok);f.Packet[5]=0x80;var writes=f.Writes;
            var restarted=New();restarted.StartSaved();restarted.Suspend();restarted.Resume();restarted.Tick();Check(f.Writes==writes);
            var state=restarted.Read();Check(state.CurrentLimit is null&&state.Verification=="unknown");
            Check(restarted.Apply(state.Fence,60).Ok&&f.Writes==writes+1);
        });
        Test("unknown AP still checks stale fence and journals before the first write",()=>{
            var f=new MsiApFixture();f.Packet[5]=0x80;var r=new Rig("msi-claw-cg3em-ap");
            var c=new Controller(r.Id,r.Store,_=>new MsiApProvider(f),()=> (true,true,74));var before=c.Read();
            f.Packet[5]=0xe4;Check(!c.Apply(before.Fence,80).Ok&&f.Writes==0);f.Packet[5]=0x80;
            r.Store.FailSave=Controller.RecoveryFile;Check(!c.Apply(c.Read().Fence,80).Ok&&f.Writes==0);
        });
        Test("AP writes preserve every non-charge byte and retain the flag for 100 percent",()=>{
            foreach(int target in new[]{20,60,80,100}){
                var f=new MsiApFixture();using var p=new MsiApProvider(f);var before=p.Read();f.Packet[3]=0xc0;f.Packet[17]=99;
                p.Write(target,before);Check(f.Writes==1&&f.Sent[0][0]==0&&f.Sent[0][3]==0xc0&&f.Sent[0][17]==99);
                Check(f.Sent[0][5]==(0x80|target)&&p.Read().Limit==target);
            }
        });
        Test("AP full controller apply is verified by separate Get_AP, not setter echo",()=>{
            var f=new MsiApFixture();var r=new Rig("msi-claw-cg3em-ap");var c=new Controller(r.Id,r.Store,_=>new MsiApProvider(f),()=> (true,true,74));
            var result=c.Apply(c.Read().Fence,80);Check(result.Ok&&result.CurrentLimit==80&&result.Verification=="readback"&&f.Writes==1);
            Check(f.Methods.Last()=="Get_AP"&&!r.Store.Data.ContainsKey(Controller.RecoveryFile));
        });
        Test("AP stale charge value cannot be overwritten",()=>{
            var f=new MsiApFixture();using var p=new MsiApProvider(f);var before=p.Read();f.Packet[5]=0xbc;
            try{p.Write(80,before);throw new Exception("stale AP write accepted");}catch(IOException){}Check(f.Writes==0);
        });
        Test("AP bad length flag and threshold block writes",()=>{
            foreach(var bad in new (int Length,byte Flag,byte Raw)[]{(0,1,0xe4),(6,1,0xe4),(31,1,0xe4),(33,1,0xe4),(32,0,0xe4),(32,1,100),(32,1,255),(32,1,0x81)}){
                var f=new MsiApFixture{ReplyLength=bad.Length,Flag=bad.Flag};f.Packet[5]=bad.Raw;using var p=new MsiApProvider(f);
                try{p.Write(80,new(100,0xe4));throw new Exception("invalid AP read accepted");}catch(IOException){}Check(f.Writes==0);
            }
        });
        Test("AP ignored write lost reply bad ack and failed readback never confirm success",()=>{
            foreach(var mode in new[]{"ignored","lost-reply","bad-ack","failed-readback"}){
                var f=new MsiApFixture{IgnoreWrite=mode=="ignored",LoseReply=mode=="lost-reply",SetFlag=mode=="bad-ack"?(byte)0:(byte)1,FailReadAfterWrite=mode=="failed-readback"};
                var r=new Rig("msi-claw-cg3em-ap");var c=new Controller(r.Id,r.Store,_=>new MsiApProvider(f),()=> (true,true,74));
                var result=c.Apply(c.Read().Fence,80);Check(!result.Ok&&result.Verification=="unconfirmed"&&f.Writes==1);
                Check(r.Store.Data.ContainsKey(Controller.RecoveryFile));
            }
        });
        Test("MSI Boolean WMI success is distinct from numeric status",()=>{
            foreach(var good in new object?[]{null,true,0,0u})MsiPacket.CheckReturn(good);
            foreach(var bad in new object[]{false,1u}){try{MsiPacket.CheckReturn(bad);throw new Exception("failed WMI status accepted");}catch(IOException){}}
            var f=new MsiApFixture{ReturnValue=false};using var p=new MsiApProvider(f);try{p.Read();throw new Exception("Boolean rejection accepted");}catch(IOException){}Check(f.Writes==0);
        });
        Test("Win5 field identity never inherits Mini charging even when a battery is present",()=>{
            var id=new Identity("GPD","G1618-05","G1618-05","AMD RYZEN AI MAX+ 395 w/ Radeon 8060S","2.20",2,20);
            foreach(var present in new[]{false,true}){
                var opens=0;var c=new Controller(id,new MemoryStore(),_=>{opens++;throw new Exception("Win5 must not open a guessed provider");},()=> (present,true,75));
                Check(Policy.Resolve(id) is null);
                var state=c.Read();Check(!state.Ok&&!state.Supported&&state.BatteryPresent==present&&state.CurrentLimit is null&&state.Choices.Length==0);
                Check(state.Reason=="此设备暂无可确认的充电上限接口");
                Check(!c.Apply(state.Fence,80).Ok);c.StartSaved();c.Suspend();c.Resume();c.Tick();Check(opens==0);
            }
            Check(Policy.Resolve(id with {Bios="2.10",EcMinor=7}) is null,"Mini firmware numbers must not admit Win5");
        });
        Test("RC73XA field identity exposes choices but DSTS low bits never become a threshold",()=>{
            var id=new Identity("ASUSTeK COMPUTER INC.","RC73XA","ROG Xbox Ally X RC73XA_RC73XA","AMD Ryzen AI Z2 Extreme","RC73XA.317",3,14);
            foreach(var dsts in new uint[]{0x10001,0x10050}){
                var writes=0;var store=new MemoryStore();
                Controller New()=>new(id,store,_=>new AsusProvider((method,value)=>{
                    if(method==0x53545344)return dsts;
                    Check(method==0x53564544&&value==80);writes++;return 1;
                }),()=> (true,true,75));
                var c=New();var before=c.Read();Check(before.Ok&&before.Supported&&before.CurrentLimit is null&&before.RequestedLimit is null&&before.Verification=="unknown"&&writes==0);
                Check(before.Choices.SequenceEqual(Enumerable.Range(0,11).Select(n=>50+5*n)));
                var after=c.Apply(before.Fence,80);Check(after.Ok&&after.CurrentLimit is null&&after.RequestedLimit==80&&after.Verification=="acknowledged"&&writes==1);
                var reopened=New().Read();Check(reopened.CurrentLimit is null&&reopened.RequestedLimit==80&&reopened.Verification=="unknown"&&writes==1);
            }
        });
        Test("RC73XA absence or a rejected firmware acknowledgement never confirms a setting",()=>{
            var id=new Identity("ASUSTeK COMPUTER INC.","RC73XA","ROG Xbox Ally X RC73XA_RC73XA","AMD Ryzen AI Z2 Extreme","RC73XA.317",3,14);
            foreach(var present in new[]{false,true}){
                var writes=0;var store=new MemoryStore();var c=new Controller(id,store,_=>new AsusProvider((method,value)=>{
                    if(method==0x53545344)return present?0x10000u:0u;
                    Check(method==0x53564544);writes++;return 0;
                }),()=> (true,true,75));
                var before=c.Read();Check(before.Supported==present);
                var after=c.Apply(before.Fence,80);Check(!after.Ok&&after.CurrentLimit is null&&after.RequestedLimit is null&&writes==(present?1:0));
                Check(after.Verification==(present?"unconfirmed":"unknown"));Check(store.Data.ContainsKey(Controller.RecoveryFile)==present);
            }
        });
        Test("OXP3 WMI EC production route reads back independently and rejects stale or failed writes",()=>{
            var id=new Identity("ONE-NETBOOK","board","ONEXPLAYER 3","Intel","2.10",2,80);
            var binding=Policy.ResolveCapability(id);
            Check(binding?.Consumer.RouteId=="oxp3-wmi-ec");
            Check(binding!.Descriptor.RuleId=="battery.rule.oxp3-wmi-ec.OneXPlayer3"&&binding.Descriptor.DeviceId=="ymcc.device.OneXPlayer3"&&binding.Descriptor.Origin=="reviewed-native-extension");
            Check(Policy.Resolve(id with {SystemProduct="ONEXPLAYER-3"})?.RouteId=="oxp3-wmi-ec");
            Check(Policy.Resolve(id with {Manufacturer="AOKZOE"}) is null);
            Check(Policy.Resolve(new Identity("ONE-NETBOOK","ONEXPLAYER X2","ONEXPLAYER X2","Intel","2.10",2,80)) is null);
            Check(binding.Consumer.Choices.All(v=>v>=50&&v<=100&&v%5==0));
            var f=new Oxp3WmiFixture();var store=new MemoryStore();
            Controller New()=>new(id,store,_=>new Oxp3WmiProvider(f),()=> (true,true,75));
            var before=New().Read();
            Check(before.Ok&&before.Supported&&before.CurrentLimit==80&&before.Verification=="readback"&&f.Writes==0);
            var c=New();var after=c.Apply(c.Read().Fence,90);
            Check(after.Ok&&after.CurrentLimit==90&&after.Verification=="readback"&&f.Writes==1);
            Check(f.Log.Count==1&&f.Log[0].Register==Oxp3WmiProvider.LimitRegister&&f.Log[0].Value==90);
            Check(!store.Data.ContainsKey(Controller.RecoveryFile));
            Check(c.Apply(after.Fence,90).Ok&&f.Writes==1);
            Check(Oxp3WmiProvider.PackAddress(Oxp3WmiProvider.LimitRegister)==0xA304u);
            Check(Oxp3WmiProvider.PackWrite(Oxp3WmiProvider.LimitRegister,80)==0x50A304u);
            Check(Oxp3WmiProvider.BypassValue(0)==0x00&&Oxp3WmiProvider.BypassValue(1)==0x01&&Oxp3WmiProvider.BypassValue(2)==0x0B);
            var productWrites=f.Writes;
            using(var bypass=new Oxp3WmiProvider(f))
            {
                bypass.WriteBypassMode(1);
                Check(f.Log.Last().Register==Oxp3WmiProvider.BypassRegister&&f.Log.Last().Value==0x01,"bypass source mapping was not exercised");
                try{bypass.WriteBypassMode(3);throw new Exception("unknown OXP3 bypass mode accepted");}catch(IOException){}
            }
            Check(f.Writes==productWrites+1,"product limit workflow must not write the bypass register");
            var fc=New();f.Limit=20;var low=fc.Read();Check(low.Ok&&low.CurrentLimit==20&&low.Choices.All(v=>v>=50));
            f.Limit=70;Check(!fc.Apply(low.Fence,80).Ok&&f.Writes==productWrites+1);
            using(var p=new Oxp3WmiProvider(f)){var rb=p.Read();f.Limit=60;try{p.Write(80,rb);throw new Exception("stale OXP3 write accepted");}catch(IOException){}Check(f.Writes==productWrites+1);}
            var failedRead=new Oxp3WmiFixture{FailRead=true};
            Check(!new Controller(id,new MemoryStore(),_=>new Oxp3WmiProvider(failedRead),()=> (true,true,75)).Read().Ok&&failedRead.Writes==0);
            var failedWrite=new Oxp3WmiFixture{FailWrite=true};var store2=new MemoryStore();
            var cw=new Controller(id,store2,_=>new Oxp3WmiProvider(failedWrite),()=> (true,true,75));
            Check(!cw.Apply(cw.Read().Fence,90).Ok&&failedWrite.Writes==1&&store2.Data.ContainsKey(Controller.RecoveryFile));
            var ignored=new Oxp3WmiFixture{IgnoreWrite=true};
            var ci=new Controller(id,new MemoryStore(),_=>new Oxp3WmiProvider(ignored),()=> (true,true,75));
            var mismatch=ci.Apply(ci.Read().Fence,90);Check(!mismatch.Ok&&mismatch.Verification=="unconfirmed"&&ignored.Writes==1);
            var invalid=new Oxp3WmiFixture{Limit=101};
            Check(!new Controller(id,new MemoryStore(),_=>new Oxp3WmiProvider(invalid),()=> (true,true,75)).Read().Ok);
            var aok=new Oxp3WmiFixture();var opens=0;
            var unknown=new Controller(id with {Manufacturer="AOKZOE"},new MemoryStore(),_=>{opens++;return new Oxp3WmiProvider(aok);},()=> (true,true,75));
            Check(!unknown.Read().Supported&&opens==0);
        });
        Test("OXP3 production WMI adapter records exact arguments, responses and failures",()=>{
            var method=new Oxp3WmiMethodFixture();using var ec=new Oxp3WmiEc(method);using var p=new Oxp3WmiProvider(ec);
            var before=p.Read();
            Check(before.Limit==80&&method.Calls.Count==1&&method.Calls[0].Method=="ReadECReg"&&method.Calls[0].Arg==0xA304u);
            p.Write(90,before);
            Check(method.Calls.Count==3&&method.Calls[1].Method=="ReadECReg"&&method.Calls[1].Arg==0xA304u&&method.Calls[2].Method=="WriteECReg"&&method.Calls[2].Arg==0x5AA304u&&method.Limit==90);
            Check(p.Read().Limit==90);
            method.ReadResponse="1,00";try{p.Read();throw new Exception("non-zero OXP3 status accepted");}catch(IOException){}
            method.ReadResponse="0,zz";try{p.Read();throw new Exception("invalid OXP3 response byte accepted");}catch(IOException){}
            method.ReadResponse="0";try{p.Read();throw new Exception("missing OXP3 status/value accepted");}catch(IOException){}
            method.ReadResponse=null;method.FailRead=true;try{p.Read();throw new Exception("OXP3 method exception accepted");}catch(IOException){}
            method.FailRead=false;method.NullRead=true;try{p.Read();throw new Exception("missing OXP3 read result accepted");}catch(IOException){}
            method.NullRead=false;method.NullWrite=true;try{p.Write(80,p.Read());throw new Exception("missing OXP3 write result accepted");}catch(IOException){}
            method.NullWrite=false;method.WriteResponse="";try{p.Write(80,p.Read());throw new Exception("empty OXP3 write status accepted");}catch(IOException){}
            method.WriteResponse="1,00";try{p.Write(80,p.Read());throw new Exception("OXP3 write status accepted");}catch(IOException){}
            method.WriteResponse=null;method.FailWrite=true;try{p.Write(80,p.Read());throw new Exception("OXP3 write exception accepted");}catch(IOException){}
            var mem=new Oxp3WmiMethodFixture();var id=new Identity("ONE-NETBOOK","board","ONEXPLAYER 3","Intel","2.10",2,80);var store=new MemoryStore();
            Controller New()=>new(id,store,_=>new Oxp3WmiProvider(new Oxp3WmiEc(mem)),()=> (true,true,75));
            var c=New();var state=c.Read();Check(state.Ok&&state.CurrentLimit==80);
            var applied=c.Apply(state.Fence,90);Check(applied.Ok&&applied.CurrentLimit==90&&mem.Reads>=2&&mem.Writes==1);

            // Exercise the production selector with inert method objects, never a WMI query.
            int yielded=0,returned=0;
            IEnumerable<Oxp3WmiMethodFixture> Candidates(Oxp3WmiMethodFixture[] values,bool failMoveNext=false,bool failDispose=false)
            {
                yielded=returned=0;
                try
                {
                    foreach(var value in values){yielded++;yield return value;}
                    if(failMoveNext)throw new IOException("mock OXP3 enumeration failed");
                }
                finally
                {
                    returned++;
                    if(failDispose)throw new IOException("mock OXP3 enumerator disposal failed");
                }
            }
            var unique=new Oxp3WmiMethodFixture();
            var selected=Oxp3WmiManagementMethod.SelectUniqueInstance(Candidates([unique]));
            Check(ReferenceEquals(selected,unique)&&yielded==1&&returned==1,"unique OXP3 endpoint was not returned after enumeration cleanup");
            Check(unique.DisposeCalls==0&&unique.Calls.Count==0,"selection disposed or invoked the caller-owned unique endpoint");
            selected.Dispose();Check(unique.DisposeCalls==1,"caller did not retain unique endpoint disposal ownership");

            try{Oxp3WmiManagementMethod.SelectUniqueInstance(Candidates([]));throw new Exception("empty OXP3 endpoint set accepted");}catch(IOException){}
            Check(yielded==0&&returned==1,"empty OXP3 enumeration was not returned");
            foreach(var mode in new[]{"distinct","first-dispose","second-dispose","both-dispose","enumerator-dispose"})
            {
                var first=new Oxp3WmiMethodFixture{ThrowDispose=mode is "first-dispose" or "both-dispose"};
                var second=new Oxp3WmiMethodFixture{ThrowDispose=mode is "second-dispose" or "both-dispose"};
                var untouched=new Oxp3WmiMethodFixture();
                try{Oxp3WmiManagementMethod.SelectUniqueInstance(Candidates([first,second,untouched],failDispose:mode=="enumerator-dispose"));throw new Exception("ambiguous OXP3 endpoint set accepted: "+mode);}catch(IOException){}
                Check(yielded==2&&returned==1,"OXP3 ambiguity did not stop at the second object and return enumeration: "+mode);
                Check(first.DisposeCalls==1&&second.DisposeCalls==1&&untouched.DisposeCalls==0,"OXP3 ambiguity did not attempt disposal of every acquired object exactly once: "+mode);
                Check(first.Calls.Count==0&&second.Calls.Count==0&&untouched.Calls.Count==0,"ambiguous OXP3 endpoint enumeration invoked a method: "+mode);
            }
            foreach(var nullFirst in new[]{true,false})
            {
                var first=new Oxp3WmiMethodFixture();var untouched=new Oxp3WmiMethodFixture();
                Oxp3WmiMethodFixture[] values=nullFirst ? [null!,untouched] : [first,null!,untouched];
                try{Oxp3WmiManagementMethod.SelectUniqueInstance(Candidates(values));throw new Exception("null OXP3 endpoint accepted");}catch(IOException){}
                Check(yielded==(nullFirst?1:2)&&returned==1,"null OXP3 endpoint did not stop and return enumeration");
                Check(first.DisposeCalls==(nullFirst?0:1)&&untouched.DisposeCalls==0,"null OXP3 endpoint leaked an acquired object or disposed an unacquired object");
                Check(first.Calls.Count==0&&untouched.Calls.Count==0,"null OXP3 endpoint enumeration invoked a method");
            }
            var duplicate=new Oxp3WmiMethodFixture();var afterDuplicate=new Oxp3WmiMethodFixture();
            try{Oxp3WmiManagementMethod.SelectUniqueInstance(Candidates([duplicate,duplicate,afterDuplicate]));throw new Exception("duplicate OXP3 reference accepted as unique");}catch(IOException){}
            Check(yielded==2&&returned==1&&duplicate.DisposeCalls==1&&afterDuplicate.DisposeCalls==0,"duplicate OXP3 reference was not rejected and released exactly once");
            Check(duplicate.Calls.Count==0&&afterDuplicate.Calls.Count==0,"duplicate OXP3 endpoint enumeration invoked a method");
            foreach(var failMoveNext in new[]{true,false})
            {
                var first=new Oxp3WmiMethodFixture();
                try{Oxp3WmiManagementMethod.SelectUniqueInstance(Candidates([first],failMoveNext:failMoveNext,failDispose:!failMoveNext));throw new Exception("failed OXP3 enumeration accepted");}catch(IOException){}
                Check(yielded==1&&returned==1&&first.DisposeCalls==1,"failed OXP3 enumeration leaked its selected endpoint or enumerator");
                Check(first.Calls.Count==0,"failed OXP3 endpoint enumeration invoked a method");
            }
        });
        if(selectedCase is not null)
        {
            if(count!=1)
            {
                Console.Error.WriteLine($"FAIL --case '{selectedCase}': expected exactly 1 group, actual count={count}.");
                Environment.ExitCode=1;return;
            }
            Console.WriteLine($"Selected case count={count}.");
        }
        Console.WriteLine($"{count} groups passed; fake providers only, no hardware writes.");
    }
}
