using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using Mono.Cecil;
using HandheldCompanion.Devices;
using HandheldCompanion.Misc;
using Trace=ConsumerFixture.Trace;
static partial class OldWriteProbe
{
 static Assembly ExistingFixtureAssembly()=>typeof(IDevice).Assembly;
 const BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
 static int NativeAttempts;static readonly List<object> Cases=new();static readonly List<object> Checks=new();static readonly List<object> GoldenWrites=new();
 static string Hash(string p)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))).ToLowerInvariant();
 static void Check(bool b,string name){Checks.Add(new{name,passed=b});if(!b)throw new InvalidOperationException(name);}
 static object? Invoke(object target,string name,params object?[] args){try{return target.GetType().GetMethods(All).Single(m=>m.Name==name&&m.GetParameters().Length==args.Length).Invoke(target,args);}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
 static void Set(object target,string name,object? value){var f=target.GetType().GetField(name,All);if(f is not null)f.SetValue(target,value);else target.GetType().GetProperty(name,All)!.SetValue(target,value);}
 static object? Get(object target,string name)=>target.GetType().GetField(name,All)?.GetValue(target)??target.GetType().GetProperty(name,All)?.GetValue(target);
 static object Backend(Assembly a,IDevice device,List<string> logs)
 {var opts=RuntimeHelpers.GetUninitializedObject(a.GetType("YeManFanHost.HostOptions",true)!);var b=Activator.CreateInstance(a.GetType("YeManFanHost.RealHcBackend",true)!,All,null,[opts],null)!;Set(b,"device",device);Set(b,"assembly",typeof(IDevice).Assembly);b.GetType().GetProperty("Diagnostic")!.SetValue(b,new Action<string,object?>((name,value)=>logs.Add(name+":"+JsonSerializer.Serialize(value))));Invoke(b,"BeginOpenAttemptEvidence");Invoke(b,"OpenHcDevice");return b;}
 static string[] Ops(MethodDefinition m)=>m.Body.Instructions.Where(i=>i.OpCode.Name!="nop").Select(i=>i.OpCode.Name.EndsWith(".s")?i.OpCode.Name[..^2]:i.OpCode.Name).ToArray();
 internal static int Run(string[] args)
 {
  Exception? failure=null;
  try{
   Check(Hash(args[0])=="a6788164463da89f7e7df6a6ab1edaf7c2d5eff69c27d127b0d3ce5c47d36539","Exact last accepted source candidate Host");
   using(var actual=AssemblyDefinition.ReadAssembly(args[1]))using(var model=AssemblyDefinition.ReadAssembly(typeof(IDevice).Assembly.Location))
   {
    var a=actual.MainModule.GetType("HandheldCompanion.Devices.IDevice").Methods.Single(m=>m.Name=="PowerProfileManager_Applied");var f=model.MainModule.GetType("HandheldCompanion.Devices.IDevice").Methods.Single(m=>m.Name=="PowerProfileManager_Applied");
    Check(Ops(a).SequenceEqual(Ops(f)),"Complete actual profile callback opcodes matched");
    var ac=a.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName);var fc=f.Body.Instructions.Where(i=>i.Operand is MethodReference).Select(i=>((MethodReference)i.Operand).FullName);
    Check(ac.SequenceEqual(fc),"Complete actual profile callback call sequence matched");
   }
   var host=Load(args[0]);
   foreach(var mode in new[]{"duty-cold","duty-cached","manual-profile","automatic-profile"})
   {
    Trace.ReleaseThrows=false;IDevice.ResetLibrary();Trace.Calls.Clear();var logs=new List<string>();var d=new OneXPlayerX1AMD();var b=Backend(host,d,logs);
    if(mode=="duty-cached"){Invoke(b,"WriteFanProfileDuty",25.0);Check(Trace.Calls.Contains("FAN:044b:46"),"First duty call primes actual Host delegate with valid write");}
    Trace.Calls.Clear();logs.Clear();Trace.ReleaseThrows=true;Exception? observed=null;
    try{if(mode.StartsWith("duty"))Invoke(b,"WriteFanProfileDuty",50.0);else Invoke(b,"ApplyPowerProfile",new PowerProfile{FanProfile=new(){fanMode=mode=="manual-profile"?FanMode.Software:FanMode.Hardware}},"Background",null,null);}catch(Exception e){observed=e;}
    Check(observed is null,"Actual consumer returns normally despite exact lower write error: "+mode);
    Check(Trace.Calls.Any(x=>x.Contains("PORT_WRITE_THROW")),"Actual EC method logged the injected port failure: "+mode);
    Check(!Trace.Calls.Any(x=>x.StartsWith("FAN:")),"No completed EC write in failed operation: "+mode);
    if(mode.EndsWith("profile"))Check(logs.Any(x=>x.StartsWith("hc.fan-dispatch.callback-returned:")),"Actual Host emits callback-returned evidence on swallowed failure: "+mode);
    Cases.Add(new{id=mode,status="SOFTWARE_FALSE_SWALLOWED_REPRODUCED",consumerReturned=true,exception=observed?.ToString(),calls=Trace.Calls.ToArray(),diagnostics=logs.ToArray()});
    Trace.ReleaseThrows=false;Invoke(b,"CloseHcDevice");
   }
   Trace.ReleaseThrows=false;IDevice.ResetLibrary();Trace.Calls.Clear();var temperatureLogs=new List<string>();var temperatureDevice=new OneXPlayerX1AMD();var temperatureBackend=Backend(host,temperatureDevice,temperatureLogs);
   Set(temperatureBackend,"sessionDeviceReference",temperatureDevice);Set(temperatureBackend,"oemBaselineCaptured",true);Set(temperatureBackend,"baselineRestoreRoute",temperatureDevice.GetType().FullName);Set(temperatureBackend,"baselineStrategy",Get(Get(temperatureBackend,"Route")!,"RestoreStrategy"));Invoke(temperatureBackend,"CaptureSessionRouteMarker");
   var fanProfile=new FanProfile{fanMode=FanMode.Software};Set(temperatureBackend,"activeFanProfile",fanProfile);Set(temperatureBackend,"activeCurveDuties",new double[11]);
   var monitor=Activator.CreateInstance(host.GetType("YeManFanHost.HWiNFOTemperatureMonitor",true)!,true)!;
   Set(monitor,"sampleReadTick",Environment.TickCount64);Set(temperatureBackend,"cpuTemperatureMonitor",monitor);int failures=0;
   temperatureBackend.GetType().GetProperty("FanDispatchFailure")!.SetValue(temperatureBackend,new Action<Exception>(_=>Interlocked.Increment(ref failures)));
   Trace.Calls.Clear();Trace.ReleaseThrows=true;Invoke(temperatureBackend,"OnHcCpuTemperatureChanged",monitor,(float?)65);
   Check(Trace.Calls.Any(x=>x.Contains("PORT_WRITE_THROW"))&&!Trace.Calls.Any(x=>x.StartsWith("FAN:")),"Temperature path used actual failing EC write");
   var target=Invoke(temperatureBackend,"MonitorTargetForRead");
   Check(target is not null && Get(target,"Percent") is double percent && percent==50.0,"Actual Host publishes target 50 despite EC write failure");
   Check(Get(temperatureBackend,"lastDispatchedTemperature") is float temperature && temperature==65,"Actual Host commits last successful temperature on swallowed failure");
   Check(ReferenceEquals(Get(temperatureBackend,"activeFanProfile"),fanProfile)&&ReferenceEquals(Get(temperatureBackend,"cpuTemperatureMonitor"),monitor)&&failures==0,"Swallowed failure leaves temperature consumer active without recovery notification");
   Cases.Add(new{id="temperature-target-false-progress",status="SOFTWARE_FALSE_SWALLOWED_REPRODUCED",targetPublished=true,targetPercent=50,temperatureCommitted=65,failureCallbackCount=failures,calls=Trace.Calls.ToArray(),scope="Actual Host temperature sample/write/monitor-target consumers; injected fixed curve output is a leaf fixture, no real curve or HWiNFO mapping started"});
   Trace.ReleaseThrows=false;Trace.Calls.Clear();Set(monitor,"sampleReadTick",Environment.TickCount64);
   Invoke(temperatureBackend,"OnHcCpuTemperatureChanged",monitor,(float?)65);
   Check(!Trace.Calls.Any(x=>x.StartsWith("FAN:")),"Recovered port with unchanged temperature is skipped because failed write committed cache");
   Check(Invoke(temperatureBackend,"MonitorTargetForRead")is not null,"False target remains readable after skipped retry");
   Cases.Add(new{id="same-temperature-recovery-never-retries",status="SOFTWARE_FALSE_SWALLOWED_REPRODUCED",portFailureCleared=true,completedRetryWrites=0,targetStillPublished=true,calls=Trace.Calls.ToArray(),scope="Actual Host same-temperature duplicate filter after swallowed write; scripted fixed curve output"});
   Set(monitor,"sampleReadTick",Environment.TickCount64);Invoke(temperatureBackend,"OnHcCpuTemperatureChanged",monitor,(float?)66);
   Check(Trace.Calls.Contains("FAN:044b:92"),"Changed temperature reaches recovered actual EC write path, proving prior skip was cache not disabled fixture");
   Invoke(temperatureBackend,"DetachCpuTemperatureLoop");Invoke(temperatureBackend,"CloseHcDevice");
   Trace.Calls.Clear();var goldenDevice=new OneXPlayerX1AMD();var goldenBackend=Backend(host,goldenDevice,new());
   foreach(var golden in new[]{(0.0,0),(0.25,0),(0.5,1),(1.0,2),(25.0,46),(50.0,92),(75.0,138),(99.0,182),(100.0,184)})
   {Trace.Calls.Clear();Invoke(goldenBackend,"WriteFanProfileDuty",golden.Item1);Check(Trace.Calls.Contains($"FAN:044b:{golden.Item2}"),"Actual inherited duty conversion golden: "+golden.Item1);GoldenWrites.Add(new{percent=golden.Item1,raw=golden.Item2,calls=Trace.Calls.ToArray()});}
   Invoke(goldenBackend,"CloseHcDevice");
   Check(NativeAttempts==0,"No native IO attempted");
  }catch(Exception e){failure=e;}
  var result=new{schemaVersion=1,status=failure is null?"DEFECT_REPRODUCED_IN_MEMORY":"PROBE_FAILED",error=failure?.ToString(),cases=Cases,checks=Checks,goldenWrites=GoldenWrites,nativeAttempts=NativeAttempts,
   host=new{path=args[0],sha256=Hash(args[0])},hc=new{path=args[1],sha256=Hash(args[1]),codeExecuted=false},productSourcesChanged=false,installed=false,hardwareIo=false,
   scope="Actual last source candidate Host WriteFanProfileDuty (cold and cached delegate) and ApplyPowerProfile. HC full original profile/fan/EC bodies are source-derived and checked; inert OLS ports; also actual OnHcCpuTemperatureChanged sample/commit/target path with scripted curve output; not public HTTP control, real curve computation, timer or physical acceptance."};
  File.WriteAllText(args[2],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(result.status+" cases="+Cases.Count+" native="+NativeAttempts+" "+failure?.Message);return failure is null?0:2;
 }
}
