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
    static void Check(bool value,string reason="assertion failed"){if(!value)throw new Exception(reason);}
    static void Test(string name,Action test){test();count++;Console.WriteLine("PASS "+name);}
    static void Main()
    {
        Test("unified catalog selects every exact actual battery rule and trusted receipt",()=>{
            Check(Policy.Routes.Length==40);
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
        Console.WriteLine($"{count} groups passed; fake providers only, no hardware writes.");
    }
}
