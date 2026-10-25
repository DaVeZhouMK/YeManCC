using System.Collections;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;

static partial class Harness
{
    static Assembly Bootstrap=null!;
    static Type BootstrapPorts=null!,BootstrapBoard=null!,BootstrapDevice=null!;
    static string BootstrapRoot="",BootstrapPath="";
    static object? BootCall(Type type,string method,params object?[] args)
    {
        try{return type.GetMethods(All).Single(m=>m.Name==method&&m.GetParameters().Length==args.Length).Invoke(null,args);}
        catch(TargetInvocationException error)when(error.InnerException is not null){throw error.InnerException;}
    }
    static object? BootField(string field)=>BootstrapPorts.GetField(field,All)!.GetValue(null);
    static void BootField(string field,object? value)=>BootstrapPorts.GetField(field,All)!.SetValue(null,value);
    static IList BootList(string field)=>(IList)BootField(field)!;
    static object BootCurrent()=>BootCall(BootstrapPorts,"Current")!;
    static void BootReset()=>BootCall(BootstrapPorts,"Reset",BootstrapRoot);
    static object? Board(string name)=>BootstrapBoard.GetProperty(name,All)!.GetValue(null);
    static object Sensor(string type,string id){var sensor=Activator.CreateInstance(Bootstrap.GetType(type,true)!)!;Set(sensor,"DeviceId",id);return sensor;}
    static object DirectSensorDevice(){var device=Activator.CreateInstance(BootstrapDevice)!;Invoke(device,"CountFixtureSensorEvents");return device;}
    static ushort SensorFlags(object device)=>Convert.ToUInt16(Get(device,"Capabilities"));
    static int BootstrapSensorTerminalCalls()=>BootList("Calls").Cast<string>().Count(x=>x is "gyro" or "accel" or "usb-lookup" or "serial" or "serial-name");
    static void SeedBoard(Dictionary<string,object?> values)=>BootCall(BootstrapPorts,"SeedCache",JsonSerializer.Serialize(values));
    static void BootRun(string id,Action action)=>Run(id,()=>{BootReset();action();});
    static void Identity(string manufacturer,string product,string model="fixture-model",string version="fixture-version")=>BootCall(BootstrapPorts,"Identity",manufacturer,product,"fixture-system",model,version,"Fixture CPU   ",8);
    static void RunBootstrapAlgorithms()
    {
        var models=new(string manufacturer,string product,string model,string expected)[]{
            ("ASUSTeK COMPUTER INC.","RC71L","unused","ROGAlly"),("ASUSTEK COMPUTER INC.","RC72LA","unused","ROGAllyX"),
            ("ASUSTEK COMPUTER INC.","RC73YA","unused","XboxROGAlly"),("ASUSTEK COMPUTER INC.","RC73XA","unused","XboxROGAllyX"),
            ("ASUSTEK COMPUTER INC.","unknown-asus","unused","ASUS"),
            ("AYANEO","FLIP DS","unused","AYANEOFlipDS"),("AYANEO","FLIP KB","unused","AYANEOFlipKB"),
            ("AYANEO","FLIP 1S DS","unused","AYANEOFlip1SDS"),("AYANEO","FLIP 1S KB","unused","AYANEOFlip1SKB"),
            ("ONE-NETBOOK","ONEXPLAYER X1 i","unused","OneXPlayerX1Intel"),("ONE-NETBOOK TECHNOLOGY CO., LTD.","ONEXPLAYER X1 A","unused","OneXPlayerX1AMD"),
            ("ONE-NETBOOK","ONEXPLAYER X1z","unused","OneXPlayerX1AMD"),("ONE-NETBOOK","ONEXPLAYER X1Pro","unused","OneXPlayerX1Pro"),
            ("Valve","Jupiter","unused","SteamDeck"),("Valve","Galileo","unused","SteamDeck"),
            ("LENOVO","different-product","83E1","LegionGoTablet"),("LENOVO","different-product","83N1","LegionGoTablet2"),
            ("MICRO-STAR INTERNATIONAL CO., LTD.","MS-1T42","unused","ClawA2VM"),("MICRO-STAR INTERNATIONAL CO., LTD.","MS-1T91","unused","ClawCG3EM"),
            ("unknown-vendor","unknown-product","unknown-model","DefaultDevice")};
        foreach(var row in models)BootRun("bootstrap-model:"+row.expected+":"+row.product,()=>
        {
            Identity(row.manufacturer,row.product,row.model);var device=BootCurrent();
            Check(device.GetType().Name==row.expected,"Original model switch selects the admitted expected device: "+row.expected);
            Check((string)Get(device,"ManufacturerName")==row.manufacturer.ToUpper()&&(string)Get(device,"ProductName")==row.product&&(string)Get(device,"SystemModel")==row.model,"Original factory populates the exact motherboard identity");
            Check((string)Get(device,"Processor")=="Fixture CPU"&&(int)Get(device,"NumberOfCores")==8,"Original factory preserves CPU metadata conversions");
            Check(BootList("Constructors").Count==1&&SensorFlags(device)==0,"Only one inert constructor ran and it grants no fan/sensor capability");
            var scope=BootField("LastScope")!;var context=Get(Get(scope,"Contexts")!,"Device")!;
            Check((string)Get(context,"Model")==row.model&&(string)Get(context,"Manufacturer")==row.manufacturer.ToUpper(),"Actual bound telemetry callback consumes selected identity");
            Check((string)Get(context,"DeviceType")== (row.expected=="DefaultDevice"?"Desktop":"Handheld"),"Telemetry observes the original fallback ordering");
        });
        BootRun("bootstrap-model:cached-instance",()=>
        {
            var first=BootCurrent();var calls=BootList("Calls").Count;var queries=BootList("Queries").Count;
            Identity("unknown-vendor","replacement");BootField("Failure","constructor");
            Check(ReferenceEquals(first,BootCurrent())&&BootList("Calls").Count==calls&&BootList("Queries").Count==queries,"Cached device returns before new motherboard/constructor/telemetry work");
        });
        BootRun("bootstrap-cache:repeated-property",()=>
        {
            Check((string)Board("Manufacturer")=="ASUSTeK COMPUTER INC.","Original WMI lookup supplies the value");
            Check((string)Board("Manufacturer")=="ASUSTeK COMPUTER INC."&&BootList("Queries").Count==1,"Original nonempty cache prevents repeated WMI");
            Check((int)BootField("DisposedCollections")! ==1&&File.Exists((string)BootstrapPorts.GetProperty("CachePath")!.GetValue(null)!),"Real query disposes its collection and original Newtonsoft writeCache persists");
        });
        BootRun("bootstrap-cache:valid-collect",()=>
        {
            SeedBoard(new(){{"baseboard-Manufacturer","cached-vendor"},{"processor-NumberOfCores",12L}});
            Check(BootCall(BootstrapBoard,"Collect") is true,"Original Collect accepts an actual valid dictionary cache");
            BootField("Failure","wmi:Win32_BaseBoard");Check((string)Board("Manufacturer")=="cached-vendor"&&(int)Board("NumberOfCores")==12&&BootList("Queries").Count==0,"Loaded typed positive primitives avoid all WMI");
        });
        foreach(var invalid in new[]{"missing","malformed","null","invalid-type","directory"})BootRun("bootstrap-cache:collect-"+invalid,()=>
        {
            var file=(string)BootstrapPorts.GetProperty("CachePath")!.GetValue(null)!;
            if(invalid=="directory")Directory.CreateDirectory(file);
            else if(invalid!="missing")File.WriteAllText(file,invalid=="malformed"?"{broken":invalid=="null"?"null":"{\"$type\":\"Unknown.Type, MissingAssembly\"}");
            try{Check(BootCall(BootstrapBoard,"Collect") is false,"Original Collect refuses invalid cache without throwing: "+invalid);Check((string)Board("Manufacturer")=="ASUSTeK COMPUTER INC.","Invalid cache/write failure preserves actual WMI fallback");}
            finally{if(invalid=="directory")Directory.Delete(file);}
        });
        foreach(var zero in new object?[]{"",0,0L,0u,0d,null})BootRun("bootstrap-cache:invalid-value-"+(zero?.GetType().Name??"null"),()=>
        {
            SeedBoard(new(){{"baseboard-Manufacturer",zero}});BootCall(BootstrapBoard,"Collect");
            Check((string)Board("Manufacturer")=="ASUSTeK COMPUTER INC."&&BootList("Queries").Count==1,"Original empty/zero cached value must query WMI");
        });
        BootRun("bootstrap-cache:wmi-error-propagates",()=>{BootField("Failure","wmi:Win32_BaseBoard");Reject(()=>Board("Manufacturer"),"BOOTSTRAP_IO");});
        BootRun("bootstrap-sensors:none",()=>
        {
            var device=DirectSensorDevice();Invoke(device,"PullSensors");
            Check(SensorFlags(device)==0&&(int)BootField("SensorEvents")! ==1,"Absent sensors keep zero capabilities and publish one original event");
        });
        foreach(var gyro in new[]{false,true})BootRun("bootstrap-sensors:"+(gyro?"gyro":"accel"),()=>
        {
            var device=DirectSensorDevice();BootField(gyro?"Gyro":"Accel",Sensor("Windows.Devices.Sensors."+(gyro?"Gyrometer":"Accelerometer"),@"\\?\USB#VID_1234#fixture#{guid}"));BootField("UsbName","inert internal IMU");
            Invoke(device,"PullSensors");Check(SensorFlags(device)==1&&(string)Get(device,"InternalSensorName")=="inert internal IMU","Original internal sensor bit and USB name are applied");
            Check((string)BootField("LookupId")==@"USB\VID_1234\fixture", "Original Between and path replacement normalize the actual sensor DeviceId");
            Check((int)BootField("SensorEvents")! ==1,"Original sensor event occurs once after completion");
        });
        BootRun("bootstrap-sensors:gyro-precedence",()=>
        {
            var device=DirectSensorDevice();BootField("Gyro",Sensor("Windows.Devices.Sensors.Gyrometer","gyro-raw"));BootField("Accel",Sensor("Windows.Devices.Sensors.Accelerometer","accel-raw"));
            Invoke(device,"PullSensors");Check((string)BootField("LookupId")=="gyro-raw"&&SensorFlags(device)==1,"Original precedence chooses gyro and preserves an unmatched raw id");
        });
        BootRun("bootstrap-sensors:external-and-remove",()=>
        {
            var device=DirectSensorDevice();var external=Activator.CreateInstance(Bootstrap.GetType("HandheldCompanion.Sensors.SerialUSBIMU",true)!)!;Set(external,"Name","inert external IMU");BootField("External",external);
            Invoke(device,"PullSensors");Check(SensorFlags(device)==2&&(string)Get(device,"ExternalSensorName")=="inert external IMU","Original external sensor flag/name are applied");
            BootField("External",null);Invoke(device,"PullSensors");Check(SensorFlags(device)==0&&(int)BootField("SensorEvents")! ==2,"Removed external sensor clears its bit and raises the next event");
        });
        BootRun("bootstrap-sensors:internal-remove-keeps-other-bits",()=>
        {
            var device=DirectSensorDevice();var field=BootstrapDevice.GetField("Capabilities")!;field.SetValue(device,Enum.ToObject(field.FieldType,8));
            BootField("Gyro",Sensor("Windows.Devices.Sensors.Gyrometer","raw"));Invoke(device,"PullSensors");
            Check(SensorFlags(device)==9,"Sensor addition preserves preexisting non-sensor bits");
            BootField("Gyro",null);Invoke(device,"PullSensors");Check(SensorFlags(device)==8,"Original removal clears only the internal sensor bit");
        });
        foreach(var stage in new[]{"gyro","accel","usb-lookup","serial","serial-name"})BootRun("bootstrap-sensors:error-"+stage,()=>
        {
            var device=DirectSensorDevice();BootField("Gyro",Sensor("Windows.Devices.Sensors.Gyrometer","raw"));
            BootField("External",Activator.CreateInstance(Bootstrap.GetType("HandheldCompanion.Sensors.SerialUSBIMU",true)!));BootField("Failure",stage);
            Reject(()=>Invoke(device,"PullSensors"),"BOOTSTRAP_IO:"+stage);Check((int)BootField("SensorEvents")! ==0,"Failed original sensor pull cannot publish a completion event");
        });
    }
    static object BootstrapBackend()
    {
        ConsumerFixture.FactoryPorts.Reset();var backend=NewFactoryBackend(authorized:false,assemblyPath:BootstrapPath);
        Set(Get(backend,"options")!,"HcRuntimeRoot",Path.GetDirectoryName(BootstrapPath));return backend;
    }
    static void RunBootstrapFactory()
    {
        BootRun("bootstrap-factory:healthy-original",()=>
        {
            var backend=BootstrapBackend();try
            {
                Invoke(backend,"LoadAssemblyAndFactory");var device=Get(backend,"device")!;
                Check(Get(backend,"IsLoaded")is true&&Get(backend,"hcCoreInitialized")is true,"Actual factory completes the original bootstrap algorithms");
                Check(device.GetType().Name=="ROGAlly"&&SensorFlags(device)==0&&Get(backend,"CanWrite")is false,"Factory model is correct and inert constructors/unauthorized options cannot grant writes");
                var identity=(Dictionary<string,string>)Get(backend,"DeviceIdentity")!;
                Check(identity["ProductName"]=="RC71L"&&identity["SystemModel"]=="ROG Ally","Actual Host identity reads the original algorithm's populated fields");
                Check(BootList("Constructors").Count==1&&BootstrapSensorTerminalCalls()==0&&(int)BootField("SensorEvents")! ==0,"Fan-only factory selects the original device without entering motion-sensor endpoints or publishing sensor events");
                var calls=BootList("Calls").Count;Invoke(backend,"LoadAssemblyAndFactory");Check(BootList("Calls").Count==calls,"Ready actual factory cannot replay bootstrap");
            }finally{EndFactory(backend);}
        });
        BootRun("bootstrap-factory:loaded-cache",()=>
        {
            SeedBoard(new(){{"baseboard-Manufacturer","ONE-NETBOOK"},{"baseboard-Product","ONEXPLAYER X1 A"},{"baseboard-Version","cached-version"},{"motherboard-SystemName","cached-system"},{"computer-Model","cached-model"},{"processor-Name","cached-cpu"},{"processor-NumberOfCores",8L}});
            BootField("Failure","wmi:Win32_BaseBoard");var backend=BootstrapBackend();try
            {
                Invoke(backend,"LoadAssemblyAndFactory");Check(Get(backend,"IsLoaded")is true&&Get(backend,"device")!.GetType().Name=="OneXPlayerX1AMD"&&BootList("Queries").Count==0,"Actual factory must load valid motherboard cache before original model selection");
            }finally{EndFactory(backend);}
        });
        BootRun("bootstrap-factory:unsupported-sensors",()=>
        {
            BootField("Failure","gyro:unsupported");var backend=BootstrapBackend();try
            {
                Invoke(backend,"LoadAssemblyAndFactory");Check(Get(backend,"IsLoaded")is true&&Get(backend,"HcCoreWarning")is null,"Fan-only factory completes without calling the configured unsupported sensor endpoint or fabricating a warning");
                Check(BootstrapSensorTerminalCalls()==0&&(int)BootField("SensorEvents")! ==0&&(string)BootField("Failure")! =="gyro:unsupported","Unsupported sensor failure remains configured but no sensor endpoint or completion event may run");
                Check(SensorFlags(Get(backend,"device")!)==0&&Get(backend,"CanWrite")is false,"Skipped motion discovery cannot fabricate sensor/fan write permission");
            }finally{EndFactory(backend);}
        });
        foreach(var stage in new[]{"gyro","accel","usb-lookup","serial","serial-name"})BootRun("bootstrap-factory:sensor-not-called-"+stage,()=>
        {
            BootField("Failure",stage);var backend=BootstrapBackend();try
            {
                Invoke(backend,"LoadAssemblyAndFactory");
                Check(Get(backend,"IsLoaded")is true&&Get(backend,"hcCoreInitialized")is true&&Get(backend,"hcDeviceInitializeReturned")is true,"Fan-only factory must complete its remaining initialization while each sensor terminal is configured to throw");
                Check(BootstrapSensorTerminalCalls()==0&&(int)BootField("SensorEvents")! ==0&&(string)BootField("Failure")! ==stage,"No motion-sensor terminal may consume its configured failure or publish a sensor event during fan initialization");
                Check(SensorFlags(Get(backend,"device")!)==0&&Get(backend,"CanWrite")is false&&Get(backend,"HcCoreWarning")is null,"Uncalled failing sensor terminals cannot grant writes or fabricate capability/warning evidence");
            }finally{EndFactory(backend);}
        });
        foreach(var stage in new[]{"wmi:Win32_BaseBoard","constructor"})BootRun("bootstrap-factory:retry-"+stage,()=>
        {
            BootField("Failure",stage);var backend=BootstrapBackend();try
            {
                Reject(()=>Invoke(backend,"LoadAssemblyAndFactory"),"BOOTSTRAP_IO:"+stage);
                Check(Get(backend,"IsLoaded")is false&&Get(backend,"hcCoreInitialized")is false&&Get(backend,"CanWrite")is false,"Original bootstrap failure cannot publish actual Host readiness");
                var partial=Get(backend,"device");var constructors=BootList("Constructors").Count;BootField("Failure","");Invoke(backend,"LoadAssemblyAndFactory");
                Check(Get(backend,"IsLoaded")is true&&Get(backend,"CanWrite")is false,"Same actual factory completes a valid retry without write authorization");
                if(partial is not null)Check(ReferenceEquals(partial,Get(backend,"device"))&&BootList("Constructors").Count==constructors,"Bootstrap retry retains any actual previously selected instance");
                Check(ConsumerFixture.FactoryPorts.Calls.Count(x=>x=="assembly.configure")==1,"Bootstrap retry cannot replay completed Host runtime configuration");
            }finally{EndFactory(backend);}
        });
    }
    static int RunBootstrapCases(string[] args)
    {
        BootstrapPath=Path.GetFullPath(args[4]);BootstrapRoot=Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[3]))!,"bootstrap-owned-cache");
        Check(!Directory.Exists(BootstrapRoot)&&BootstrapRoot.Split(Path.DirectorySeparatorChar).Any(x=>x.Equals("Build",StringComparison.OrdinalIgnoreCase)),"Fresh owned Build cache required");
        Bootstrap=LoadBootstrapAssembly(BootstrapPath);
        BootstrapPorts=Bootstrap.GetType("BootstrapFixture.Ports",true)!;BootstrapBoard=Bootstrap.GetType("HandheldCompanion.MotherboardInfo",true)!;BootstrapDevice=Bootstrap.GetType("HandheldCompanion.Devices.IDevice",true)!;
        RunBootstrapAlgorithms();RunBootstrapFactory();var failed=Cases.Count(c=>JsonSerializer.Serialize(c).Contains("\"status\":\"FAIL\""));
        File.WriteAllText(args[3],JsonSerializer.Serialize(new{schemaVersion=1,status=failed==0?"PASS":"FAIL",checks=Checks,cases=Cases,candidateHostSha256=Hash(args[0]),bootstrapSha256=Hash(BootstrapPath),nativeAttempts=NativeAttempts,originalBootstrapAlgorithmsExecuted=true,actualFactoryExecuted=true,constructorsOriginalBodiesExecuted=false,hardwareIo=false,hostMainStarted=false},new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine($"Bootstrap cases={Cases.Count} failed={failed} checks={Checks}");return failed==0?0:2;
    }
}
