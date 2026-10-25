// FAN980: actual scratch Host DLL, metadata-only Backend/monitor construction.
// Hold the real hardwareGate; fake diagnostic failure. No Host process/HC load/device writes.
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;

internal static class Program {
 const BindingFlags F=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
 static Assembly host=null!;static int checks;static readonly List<string> failures=[];
 static Type T(string n)=>host.GetType("YeManFanHost."+n,true)!;
 static object? Field(object o,string n)=>o.GetType().GetField(n,F)!.GetValue(o);
 static void Set(object o,string n,object? value)=>o.GetType().GetField(n,F)!.SetValue(o,value);
 static object? Call(object o,string name,params object?[] args){var m=o.GetType().GetMethods(F).Single(m=>m.Name==name&&!m.IsGenericMethod&&m.GetParameters().Length>=args.Length&&m.GetParameters().Skip(args.Length).All(p=>p.IsOptional));try{return m.Invoke(o,args.Concat(Enumerable.Repeat<object?>(Type.Missing,m.GetParameters().Length-args.Length)).ToArray());}catch(TargetInvocationException e)when(e.InnerException!=null){throw e.InnerException;}}
 static void Check(bool ok,string name){checks++;if(!ok)failures.Add(name);}
 static object Backend(){var options=T("HostOptions").GetMethod("Parse",F)!.Invoke(null,new object[]{Array.Empty<string>()});return Activator.CreateInstance(T("RealHcBackend"),F,null,[options],null)!;}
 static object TemperatureMonitor()=>Activator.CreateInstance(T("HWiNFOTemperatureMonitor"),true)!;
 static void DisposeBackend(object b){Set(b,"activeFanProfile",null);Set(b,"activeCurveDuties",null);b.GetType().GetProperty("IsOpen",F)!.SetValue(b,false);Call(b,"Dispose");}
 static void ContentionCase(object b,string prefix){
  var gate=Field(b,"hardwareGate")!;Exception? aError=null,cError=null;using var aDone=new ManualResetEventSlim();using var cDone=new ManualResetEventSlim();
  var a=new Thread(()=>{try{Call(b,"TemperatureWatchdogTick");}catch(Exception e){aError=e;}finally{aDone.Set();}}){IsBackground=true};
  var c=new Thread(()=>{try{Call(b,"TemperatureWatchdogTick");}catch(Exception e){cError=e;}finally{cDone.Set();}}){IsBackground=true};
  Monitor.Enter(gate);
  try {
   a.Start();Check(SpinWait.SpinUntil(()=>aDone.IsSet||(a.ThreadState&System.Threading.ThreadState.WaitSleepJoin)!=0,1500)&&!aDone.IsSet,prefix+" first owner waits on actual hardwareGate");
   c.Start();Check(cDone.Wait(500),prefix+" second periodic tick returns without waiting behind the HC owner");
   Check(!aDone.IsSet,prefix+" original waiting owner is retained");
  }finally{Monitor.Exit(gate);}
  Check(aDone.Wait(2000)&&cDone.Wait(2000)&&aError is null&&cError is null,prefix+" both calls settle when owner lock is released");
  a.Join();c.Join();
  var inFlight=b.GetType().GetField("temperatureWatchdogInFlight",F);Check(inFlight is null||(int)inFlight.GetValue(b)! == 0,prefix+" running marker is retired");
 }
 public static int Main(string[] args){
  var dll=Path.GetFullPath(args[0]);AssemblyLoadContext.Default.Resolving+=(_,n)=>{var p=Path.Combine(Path.GetDirectoryName(dll)!,n.Name+".dll");return File.Exists(p)?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;};host=AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);
  T("Program").GetProperty("IsSelfTestProcess",F)!.SetValue(null,true);
  var b=Backend();try{
   ContentionCase(b,"normal");
   // Force a diagnostic failure in the stale/detach branch; no device exists.
   Set(b,"cpuTemperatureMonitor",TemperatureMonitor());Set(b,"activeFanProfile",new object());b.GetType().GetProperty("IsOpen",F)!.SetValue(b,true);
   Set(b,"lastTemperatureTick",Environment.TickCount64-10000);Set(b,"temperatureStaleSignaled",false);
   b.GetType().GetProperty("Diagnostic",F)!.SetValue(b,(Action<string,object?>)((_,_)=>throw new InvalidOperationException("fixture-diagnostic-failure")));
   Exception? expected=null;try{Call(b,"TemperatureWatchdogTick");}catch(Exception e){expected=e;}
   Check(expected is InvalidOperationException&&expected.Message=="fixture-diagnostic-failure","exception is observed rather than silently reporting a successful stale check");
   b.GetType().GetProperty("Diagnostic",F)!.SetValue(b,null);Set(b,"activeFanProfile",null);b.GetType().GetProperty("IsOpen",F)!.SetValue(b,false);
   ContentionCase(b,"after-exception");
   var old=TemperatureMonitor();var current=TemperatureMonitor();Set(b,"cpuTemperatureMonitor",current);Set(b,"lastTemperatureTick",123L);Set(b,"temperatureHealthRevision",7L);
   Call(b,"OnTemperatureMonitorSampled",old);Check((long)Field(b,"temperatureHealthRevision")! ==7L&&(long)Field(b,"lastTemperatureTick")! ==123L,"old monitor healthy callback cannot refresh a replacement source");
   Call(b,"OnTemperatureMonitorChanged",old,70f);Check((long)Field(b,"temperatureHealthRevision")! ==7L,"old monitor changed callback cannot refresh current source");
   // This metadata fixture represents a valid sample from the current monitor.
   current.GetType().GetField("sampleReadTick",F)?.SetValue(current,Environment.TickCount64);
   Call(b,"OnTemperatureMonitorSampled",current);Check((long)Field(b,"temperatureHealthRevision")! ==8L,"current monitor still refreshes health after old callbacks are rejected");
   var owner=Call(b,"CaptureFanDispatchOwner")!;var signalType=T("RealHcBackend").GetNestedType("TemperatureStaleSignal",F)!;
   var signal=Activator.CreateInstance(signalType,F,null,[owner,8L],null)!;
   Check(Call(b,"IsTemperatureStaleCurrent",signal) is true,"current stale signal retains its actual owner/revision identity");
   Call(b,"OnTemperatureMonitorSampled",current);Check(Call(b,"IsTemperatureStaleCurrent",signal) is false,"later healthy sample retires queued stale signal");
   var newSignal=Activator.CreateInstance(signalType,F,null,[Call(b,"CaptureFanDispatchOwner")!,9L],null)!;
   var dutiesField=b.GetType().GetField("activeCurveDuties",F)!;Set(b,"activeCurveDuties",Array.CreateInstance(dutiesField.FieldType.GetElementType()!,101));
   Check(Call(b,"IsTemperatureStaleCurrent",newSignal) is false,"new curve identity retires previous stale recovery responsibility");
   Set(b,"activeCurveDuties",null);Set(b,"cpuTemperatureMonitor",null);Call(b,"OnTemperatureMonitorSampled",current);
   Check((long)Field(b,"temperatureHealthRevision")! ==9L,"detached source callback cannot bless an OFF control session");
   Call(old,"Dispose");Call(current,"Dispose");
  }finally{DisposeBackend(b);}
  Check(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),"HC assembly is not loaded by this metadata/callback/lock fixture");
  Console.WriteLine(JsonSerializer.Serialize(new{ok=failures.Count==0,checks,failed=failures.Count,failures,scope="actual-host-temperature-watchdog-lock-and-callback-owner-fixtures-no-host-start-hc-load-device-or-cpu-benchmark"},new JsonSerializerOptions{WriteIndented=true}));return failures.Count==0?0:1;
 }
}
