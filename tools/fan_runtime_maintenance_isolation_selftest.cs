using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;

internal static class Program
{
 const BindingFlags F=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
 static Assembly host=null!;static Type engineType=null!,backendType=null!;static int checks;static readonly List<string> failures=[];
 static void Require(bool v,string why){if(!v)throw new InvalidOperationException(why);}
 static void Check(string name,Action action){checks++;try{action();}catch(Exception ex){failures.Add(name+": "+ex.Message);}}
 static object? Field(object o,string n)=>o.GetType().GetField(n,F)!.GetValue(o);
 static void Set(object o,string n,object? v)=>o.GetType().GetField(n,F)!.SetValue(o,v);
 static object? Call(object o,string n,params object?[] args){try{return o.GetType().GetMethods(F).Single(m=>m.Name==n&&m.GetParameters().Length==args.Length).Invoke(o,args);}catch(TargetInvocationException ex)when(ex.InnerException is not null){throw ex.InnerException;}}
 sealed class Fixture : IDisposable
 {
  public object Engine {get;}public object Backend {get;}public FakeDevice Device {get;}=new();public string Root {get;}
  public Fixture(string baseRoot){
   Root=Path.Combine(baseRoot,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(Root);
   var options=host.GetType("YeManFanHost.HostOptions",true)!.GetMethod("Parse",F)!.Invoke(null,[new[]{"--real-backend"}]);
   var log=Activator.CreateInstance(host.GetType("YeManFanHost.HostDiagnosticLog",true)!,F,null,[Root,0],null);
   Engine=Activator.CreateInstance(engineType,F,null,[options,log],null)!;Backend=Field(Engine,"realBackend") ?? throw new InvalidOperationException("metadata-only backend absent");
   Require(Field(Backend,"assembly") is null,"HC assembly must remain absent");
   (Field(Engine,"runtimeDiagnosticsTimer") as Timer)?.Dispose();Set(Engine,"runtimeDiagnosticsTimer",null);
   Set(Backend,"device",Device);
  }
  public long Runs=>(long)engineType.GetProperty("RuntimeLoopMaintenanceRunsForSelfTest",F)!.GetValue(Engine)!;
  public int Abandoned=>(int)Field(Backend,"abandonedOpenEpisodeReported")!;
  public int InFlight=>(int)Field(Engine,"runtimeSampleInFlight")!;
  public void Tick()=>Call(Engine,"WriteRuntimeDiagnostics");
  public void Dispose(){
   (Field(Engine,"runtimeDiagnosticsTimer") as Timer)?.DisposeAsync().AsTask().GetAwaiter().GetResult();Set(Engine,"runtimeDiagnosticsTimer",null);
   backendType.GetProperty("Diagnostic",F)!.SetValue(Backend,null);Set(Backend,"device",null);Set(Backend,"activeCurveDuties",null);Set(Backend,"activeFanProfile",null);backendType.GetProperty("IsOpen",F)!.SetValue(Backend,false);((IDisposable)Engine).Dispose();
  }
 }
 public sealed class FakeDevice{public bool isReading=true;public bool IsOpen=>true;}
 public static int Main(string[] args){
  if(args.Length!=2)throw new ArgumentException("exact Host DLL and isolated evidence root required");var path=Path.GetFullPath(args[0]);var root=Path.GetFullPath(args[1]);Directory.CreateDirectory(root);Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT",root);
  AssemblyLoadContext.Default.Resolving+=(_,n)=>{var p=Path.Combine(Path.GetDirectoryName(path)!,n.Name+".dll");return File.Exists(p)?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;};host=AssemblyLoadContext.Default.LoadFromAssemblyPath(path);engineType=host.GetType("YeManFanHost.FanHostEngine",true)!;backendType=host.GetType("YeManFanHost.RealHcBackend",true)!;
  Check("normal gate-off tick still stops legacy fake read loop",()=>{using var f=new Fixture(root);var before=f.Runs;f.Tick();Require(!f.Device.isReading,"existing loop maintenance not executed");Require(f.Runs==before+1,"maintenance window not counted");Require(f.Abandoned==1,"abandoned-device probe not executed");Require(f.InFlight==0,"runtime owner not retired");});
  Check("exclusive diagnostic flag read failure must not skip loop stop or abandoned probe",()=>{
   using var f=new Fixture(root);var before=f.Runs;var flag=Path.Combine(f.Root,"fan-logging-enabled.flag");using(var held=new FileStream(flag,FileMode.Create,FileAccess.ReadWrite,FileShare.None)){held.Write("enabled"u8);held.Flush();f.Tick();}
   Require(!f.Device.isReading,"diagnostic flag IO failure skipped the real loop stop");Require(f.Runs==before+1,"diagnostic failure skipped maintenance accounting");Require(f.Abandoned==1,"diagnostic failure skipped abandoned-open probe");Require(f.InFlight==0,"diagnostic failure held runtime owner");
  });
  Check("one maintenance callback failure cannot skip the second maintenance responsibility",()=>{
   using var f=new Fixture(root);var before=f.Runs;backendType.GetProperty("Diagnostic",F)!.SetValue(f.Backend,(Action<string,object?>)((_,_)=>throw new InvalidOperationException("fixture-maintenance-diagnostic-failure")));f.Tick();
   Require(!f.Device.isReading,"loop stop itself failed");Require(f.Abandoned==1,"first maintenance exception skipped the abandoned-open probe");Require(f.Runs==before+1,"maintenance window disappeared on one callback failure");Require(f.InFlight==0,"maintenance exception held runtime owner");
  });
  Check("sampling failure releases in-flight guard and next tick can continue maintenance",()=>{
   using var f=new Fixture(root);var flag=Path.Combine(f.Root,"fan-logging-enabled.flag");using(var held=new FileStream(flag,FileMode.Create,FileAccess.ReadWrite,FileShare.None)){held.Write("enabled"u8);held.Flush();f.Tick();}Require(f.InFlight==0,"first failure retained owner");File.Delete(flag);f.Device.isReading=true;f.Tick();Require(!f.Device.isReading,"next tick cannot continue loop maintenance");Require(f.Runs==2,"failed sample did not preserve both maintenance attempts");
  });
  Check("runtime reentry skips concurrent diagnostic work without a second maintenance owner",()=>{
   using var f=new Fixture(root);Set(f.Engine,"runtimeSampleInFlight",1);f.Tick();Require(f.Device.isReading&&f.Runs==0,"concurrent tick ran maintenance");Require(f.InFlight==1,"concurrent tick retired another owner's guard");Set(f.Engine,"runtimeSampleInFlight",0);
  });
  Check("normal maintenance does not grant control or change engine state",()=>{
   using var f=new Fixture(root);var state=Field(f.Engine,"state")!;var stateName=(string)state.GetType().GetProperty("State",F)!.GetValue(state)!;f.Tick();Require((string)state.GetType().GetProperty("State",F)!.GetValue(state)! ==stateName,"diagnostic changed engine control state");Require((bool)state.GetType().GetProperty("HardwareWritesEnabled",F)!.GetValue(state)! ==false,"diagnostic granted hardware writes");Require(state.GetType().GetProperty("Lease",F)!.GetValue(state) is null,"diagnostic minted a lease");
  });
  Check("sampling exception still rearms the existing fault-cadence timer",()=>{
   using var f=new Fixture(root);using var fired=new ManualResetEventSlim();var timer=new Timer(_=>fired.Set(),null,Timeout.Infinite,Timeout.Infinite);Set(f.Engine,"runtimeDiagnosticsTimer",timer);var state=Field(f.Engine,"state")!;state.GetType().GetProperty("UnknownState",F)!.SetValue(state,true);
   var flag=Path.Combine(f.Root,"fan-logging-enabled.flag");using(var held=new FileStream(flag,FileMode.Create,FileAccess.ReadWrite,FileShare.None)){held.Write("enabled"u8);held.Flush();f.Tick();}
   Require(f.InFlight==0,"failure held diagnostic owner");Require(fired.Wait(3000),"fault-cadence timer not rearmed after sampling error");timer.DisposeAsync().AsTask().GetAwaiter().GetResult();Set(f.Engine,"runtimeDiagnosticsTimer",null);
  });
  Check("HC never loaded and fake-device flag is the only device target",()=>Require(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),"HC unexpectedly loaded"));
  Console.WriteLine(JsonSerializer.Serialize(new{ok=failures.Count==0,checks,failures,hostDllSha256=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant(),hardwareCalls=0,scope="actual-runtime-diagnostic-and-maintenance-methods-with-in-memory-fake-device-and-exclusive-flag-no-user-process-no-cpu-benchmark"}));return failures.Count==0?0:1;
 }
}