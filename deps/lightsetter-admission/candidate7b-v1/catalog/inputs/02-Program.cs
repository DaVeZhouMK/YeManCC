using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.ExceptionServices;
using System.Runtime.Loader;
using System.Collections;
using System.Security.Cryptography;
using System.Text.Json;
using System.Windows.Media;
using YeManFanBackend.Capabilities;

// Actual product decision/action functions with explicit memory-only device terminals.
internal static class LightingCatalogHarness
{
    private static readonly List<object> checks = [];
    private static Type ProductProgram = null!;
    private static readonly List<string> targetCalls = [];
    private static readonly List<string> dependencyAttempts = [];
    private static readonly List<string> nativeAttempts = [];
    private static int checkedCalls;
    private static object? InvokeTarget(Type type,string method,params object?[] args) {
        targetCalls.Add(type.FullName+"."+method);
        try { return type.GetMethod(method, BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static)!.Invoke(null,args); }
        catch(TargetInvocationException e) when(e.InnerException is not null) { ExceptionDispatchInfo.Capture(e.GetBaseException()).Throw();throw; }
    }
    private static Type Catalog => ProductProgram.Assembly.GetType("YeManFanBackend.Capabilities.HcLightingCapabilityCatalog",true)!;
    private static object? Prop(object? value,string name)=>value?.GetType().GetProperty(name)!.GetValue(value);
    private static string? ActualId(string alias)=>(string?)Prop(Prop(InvokeTarget(Catalog,"Resolve",alias),"Device"),"DeviceId");
    private static int ActualCount() { targetCalls.Add(Catalog.FullName+".Bindings");return ((IEnumerable)Catalog.GetProperty("Bindings")!.GetValue(null)!).Cast<object>().Count(); }
    private static bool ActualBrightness(string id)=>(bool)Prop(Prop(InvokeTarget(Catalog,"Resolve",id),"Consumer"),"Brightness")!;
    private static bool ActualSourceBrightness(string id) {
        var t=ProductProgram.Assembly.GetType("YeManFanBackend.Capabilities.HcDeviceCapabilityCatalog",true)!;
        return (Convert.ToUInt64(Prop(InvokeTarget(t,"Find",id),"DeclaredMinimum"))&8UL)!=0;
    }
    private static readonly List<string> failures = [];
    private static object? Call(string method, params object?[] args)
    {
        try { return InvokeTarget(ProductProgram,method,args); }
        catch (TargetInvocationException e) when (e.InnerException is not null) { ExceptionDispatchInfo.Capture(e.GetBaseException()).Throw(); throw; }
    }
    private static void Check(string id, bool ok) { checks.Add(new { id, ok, actualTargetCalls=targetCalls.Count-checkedCalls }); checkedCalls=targetCalls.Count; if (!ok) failures.Add(id); }
    private static bool Reject(Action call) { try { call(); return false; } catch (Exception e) when (e is NotSupportedException or ArgumentException or InvalidOperationException) { return true; } }
    private static string Execute(RecordingLightingState device, string? expected, string action, string? level="solid", int? brightness=null, string? preset=null) =>
        (string)Call("ExecuteCatalogLedAction", device, device.GetType(), expected, action, level, "off", 10,20,30,40,50,60,brightness,75,preset)!;
    private static string PureOff(RecordingLightingState device) => (string)Call("ExecuteLedAction", device, device.GetType(), "off", "solid", "off", null,null,null,null,null,null,null,null)!;
    private static Type MemoryDevice(ModuleBuilder module, HcCapabilityBinding<LightingCapabilityDescriptor> binding, bool skipBrightness=false, bool forceStatus=false)
    {
        var type=module.DefineType(binding.Device.FactoryType, TypeAttributes.Public, typeof(HandheldCompanion.Devices.IDevice));
        type.DefineDefaultConstructor(MethodAttributes.Public);
        if(binding.Device.ConsumerClass=="HandheldCompanion.Devices.OneXPlayerX1") {
            type.DefineField("LEDEnabled",typeof(bool),FieldAttributes.Public);
            type.DefineField("LEDBrightness",typeof(int),FieldAttributes.Public);
            type.DefineField("LEDPreset",typeof(object),FieldAttributes.Public);
        }
        void Override(string api,string recorder,Type[] parameters) {
            var method=type.DefineMethod(api,MethodAttributes.Public|MethodAttributes.Virtual,typeof(bool),parameters);
            var il=method.GetILGenerator();il.Emit(OpCodes.Ldarg_0);
            for(int i=0;i<parameters.Length;i++)il.Emit(OpCodes.Ldarg,i+1);
            il.Emit(OpCodes.Call,typeof(RecordingLightingState).GetMethod(recorder)!);il.Emit(OpCodes.Ret);
            type.DefineMethodOverride(method,typeof(HandheldCompanion.Devices.IDevice).GetMethod(api)!);
        }
        Override("SetLedColor","RecordColor",[typeof(Color),typeof(Color),typeof(MemoryLedLevel),typeof(int)]);
        if(binding.Consumer.Brightness&&!skipBrightness)Override("SetLedBrightness","RecordBrightness",[typeof(int)]);
        if(binding.Consumer.StatusAvailable||binding.Consumer.StatusTransportDisabled||forceStatus)Override("SetLedStatus","RecordStatus",[typeof(bool)]);
        if(binding.Consumer.PresetAvailable)Override("SetLEDPreset","RecordPreset",[typeof(MemoryPreset)]);
        return type.CreateType()!;
    }
    public static int Main(string[] args)
    {
        if(args.Length is <4 or >5)throw new ArgumentException("catalog.json result.json helper.dll expected-sha256 [--loki-only]");
        var helper=Path.GetFullPath(args[2]);
        if(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(helper))).ToLowerInvariant()!=args[3])throw new InvalidOperationException("Exact helper identity mismatch");
        AssemblyLoadContext.Default.Resolving += (_,name)=> {dependencyAttempts.Add(name.FullName);throw new InvalidOperationException("Unexpected target dependency "+name);};
        AssemblyLoadContext.Default.ResolvingUnmanagedDll += (_,name)=> {nativeAttempts.Add(name);throw new InvalidOperationException("Native target load refused "+name);};
        ProductProgram=AssemblyLoadContext.Default.LoadFromAssemblyPath(helper).GetType("Program",true)!;
        using var source=JsonDocument.Parse(File.ReadAllBytes(args[0]));
        var module=AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("MemoryLightingDevices"),AssemblyBuilderAccess.Run).DefineDynamicModule("devices");
        var types=HcLightingCapabilityCatalog.Bindings.ToDictionary(b=>b.Device.DeviceId,b=>MemoryDevice(module,b));
        RecordingLightingState Device(string id)=> (RecordingLightingState)Activator.CreateInstance(types[id])!;
        var loki=Device("hc.device.LokiZero");
        try { Check("loki-off-reaches-black-color-writer",PureOff(loki)=="ok"&&loki.Calls.SequenceEqual(["color"])&&loki.Main==Colors.Black); }
        catch(Exception e){Check("loki-off-reaches-black-color-writer",false);failures.Add(e.GetBaseException().Message);}
        if(args.Length==5&&args[4]=="--loki-only")return Save(args[1],1);
        Check("47-registered-lighting-classes",ActualCount()==47);
        var rows=source.RootElement.GetProperty("domains").GetProperty("lighting").GetProperty("rows").EnumerateArray().ToDictionary(r=>r.GetProperty("deviceId").GetString()!);
        foreach(var b in HcLightingCapabilityCatalog.Bindings)
        {
            var id=b.Device.DeviceId;var row=rows[id];
            foreach(var effect in row.GetProperty("declaredEffects").EnumerateArray().Select(e=>e.GetString()!).Where(e=>e!="preset")) {
                var d=Device(id);Check(id+":actual-color-consumer:"+effect,Execute(d,id,"color",effect,b.Consumer.Brightness?60:null)=="ok"&&d.Calls[0]=="color"&&!d.Calls.Any(c=>c.StartsWith("noop-")));
            }
            var off=Device(id);Check(id+":actual-off-consumer",Execute(off,id,"off")=="ok"&&off.Main==Colors.Black&&off.Calls.Count==1+(b.Consumer.Brightness?1:0)+(b.Consumer.StatusAvailable?1:0)&&!off.Calls.Any(c=>c.StartsWith("noop-")));
            var bright=Device(id);
            if(b.Consumer.Brightness)Check(id+":brightness-consumed",Execute(bright,id,"brightness",brightness:45)=="ok"&&bright.Brightness==45&&bright.Calls.SequenceEqual(["brightness"]));
            else Check(id+":unsupported-brightness-zero-io",Reject(()=>Execute(bright,id,"brightness",brightness:45))&&bright.Calls.Count==0);
            var status=Device(id);
            if(b.Consumer.StatusAvailable)Check(id+":status-consumed",Execute(status,id,"status")=="ok"&&status.Calls.SequenceEqual(["status"]));
            else Check(id+":base-status-zero-io",Reject(()=>Execute(status,id,"status"))&&status.Calls.Count==0);
            var bad=Device(id);Check(id+":unadvertised-effect-zero-io",Reject(()=>Execute(bad,id,"color","unreviewed-effect"))&&bad.Calls.Count==0);
            Check(id+":spoofed-expected-class-zero-io",Reject(()=>Execute(bad,"Unreviewed.Vendor."+b.Device.FactoryType.Split('.').Last(),"color"))&&bad.Calls.Count==0);
            if(b.Consumer.PresetDeclared) {
                var d=Device(id);d.Presets.AddRange(source.RootElement.GetProperty("domains").GetProperty("lighting").GetProperty("presets").EnumerateArray().Select(p=>new MemoryPreset(p.GetProperty("id").GetString()!,p.GetProperty("value").GetInt32())));
                const string preset="LEDPreset_OneXPlayerX1_Preset09";
                if(b.Consumer.PresetAvailable)Check(id+":named-preset-consumed",Execute(d,id,"preset",brightness:50,preset:preset)=="ok"&&d.Selected?.Value==0x1e3&&d.Calls.SequenceEqual(["brightness","preset"]));
                else Check(id+":declared-preset-without-transport-zero-io",Reject(()=>Execute(d,id,"preset",preset:preset))&&d.Calls.Count==0);
            }
        }
        foreach(var d in source.RootElement.GetProperty("devices").EnumerateArray())foreach(var alias in d.GetProperty("aliases").EnumerateArray().Select(a=>a.GetString()!))
            Check("exact-lighting-alias:"+alias,rows.ContainsKey(d.GetProperty("deviceId").GetString()!) ? ActualId(alias)==d.GetProperty("deviceId").GetString() : ActualId(alias)is null);
        foreach(var name in new[]{"LokiZero","ROGAlly","ClawA1M"}) {
            var id="hc.device."+name;var d=Device(id);d.ColorOk=false;
            Check(id+":failed-callback-not-success",Execute(d,id,"color")=="setLedColor-false"&&(int)Call("ExitCodeForNote","setLedColor-false")! !=0);
            var off=Device(id);off.ColorOk=false;Check(id+":failed-off-not-success",Execute(off,id,"off")=="off-partial");
            var throwing=Device(id);throwing.ThrowColor=true;Check(id+":throwing-consumer-propagates",Reject(()=>Execute(throwing,id,"color"))&&throwing.Calls.Count==1);
        }
        Check("Mini-serial-status-is-not-advertised",Reject(()=>InvokeTarget(Catalog,"Require","hc.device.OneXPlayerX1Mini",null,"status",null,false,null)));
        Check("reviewed-Loki-native-base-alias",(bool)InvokeTarget(Catalog,"IdentityMatches","hc.device.LokiMax6800U","hc.device.AynLoki")!);
        Check("unreviewed-cross-model-alias-refused",!(bool)InvokeTarget(Catalog,"IdentityMatches","hc.device.ROGAllyX","hc.device.ROGAlly")!);
        Check("MSI-real-brightness-preserved-without-source-flag",ActualBrightness("hc.device.ClawA1M")&&!ActualSourceBrightness("hc.device.ClawA1M"));
        var drift=AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("MemoryLightingRuntimeDrift"),AssemblyBuilderAccess.Run).DefineDynamicModule("drift");
        var missing=(RecordingLightingState)Activator.CreateInstance(MemoryDevice(drift,HcLightingCapabilityCatalog.Resolve("hc.device.ROGAlly")!,skipBrightness:true))!;
        Check("off-runtime-missing-writer-zero-io",Reject(()=>Execute(missing,"hc.device.ROGAlly","off"))&&missing.Calls.Count==0);
        var extra=(RecordingLightingState)Activator.CreateInstance(MemoryDevice(drift,HcLightingCapabilityCatalog.Resolve("hc.device.LokiZero")!,forceStatus:true))!;
        Check("off-runtime-extra-writer-zero-io",Reject(()=>Execute(extra,"hc.device.LokiZero","off"))&&extra.Calls.Count==0);
        Check("HC-not-loaded",!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"));
        return Save(args[1],HcLightingCapabilityCatalog.Bindings.Count);
    }
    private static int Save(string file,int models) {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(file))!);
        var result=new{status=failures.Count==0?"PASS":"FAIL",checks=checks.Count,models,failures,hardwareIo=false,HCAssemblyLoaded=AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),productionEntryPointInvoked=false,assemblySha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(ProductProgram.Assembly.Location))).ToLowerInvariant(),assemblyPath=ProductProgram.Assembly.Location,targetCalls,dependencyAttempts,nativeAttempts,cases=checks};
        File.WriteAllText(file,JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true})+"\n");
        Console.WriteLine(JsonSerializer.Serialize(new{result.status,result.checks,result.models,failures}));return failures.Count==0?0:1;
    }
}
public enum MemoryLedLevel { SolidColor=1,Breathing=2,Rainbow=4,Wave=8,Wheel=16,Gradient=32,Ambilight=64,LEDPreset=128 }
public sealed record MemoryPreset(string NameKey,int Value);
public class RecordingLightingState
{
    public List<string> Calls{get;}=[];public List<MemoryPreset> Presets{get;}=[];public Color Main;public int Brightness;public MemoryPreset? Selected;public bool ColorOk=true,ThrowColor;
    public bool RecordColor(Color a,Color b,MemoryLedLevel level,int speed){Calls.Add("color");Main=a;if(ThrowColor)throw new InvalidOperationException("memory color failure");return ColorOk;}
    public bool RecordBrightness(int value){Calls.Add("brightness");Brightness=value;return true;}
    public bool RecordStatus(bool value){Calls.Add("status");return true;}
    public bool RecordPreset(MemoryPreset value){Calls.Add("preset");Selected=value;return true;}
}
namespace HandheldCompanion.Devices
{
    // Exact name of the real base no-op contract; these methods have no hardware terminal.
    public class IDevice:RecordingLightingState
    {
        public List<MemoryPreset> LEDPresets=>Presets;
        public virtual bool SetLedColor(Color a,Color b,MemoryLedLevel level,int speed){Calls.Add("noop-color");return true;}
        public virtual bool SetLedBrightness(int value){Calls.Add("noop-brightness");return true;}
        public virtual bool SetLedStatus(bool value){Calls.Add("noop-status");return true;}
        public virtual bool SetLEDPreset(MemoryPreset value){Calls.Add("noop-preset");return true;}
    }
}
