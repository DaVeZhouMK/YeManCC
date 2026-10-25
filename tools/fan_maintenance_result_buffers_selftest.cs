using System.IO;
using System.Collections;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

internal static class Program
{
 const BindingFlags F=BindingFlags.Instance|BindingFlags.Static|BindingFlags.Public|BindingFlags.NonPublic;
 static Assembly host=null!;static Type backend=null!;static string mode="";
 static readonly List<object> results=[];static readonly List<string> failures=[];
 static object? Get(object o,string name)=>o.GetType().GetField(name,F)!.GetValue(o);
 static void Set(object o,string name,object? value)=>o.GetType().GetField(name,F)!.SetValue(o,value);
 static object? Prop(object o,string name)=>o.GetType().GetProperty(name,F)!.GetValue(o);
 static void Require(bool condition,string message){if(!condition)throw new InvalidOperationException(message);}
 static object? Call(object o,string name,params object?[] args){try{return o.GetType().GetMethods(F).Single(m=>m.Name==name&&m.GetParameters().Length==args.Length).Invoke(o,args);}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
 static readonly HashSet<string> MomentFields=new(StringComparer.Ordinal){"firstStopTick","lastStopTick","genFirstStopTick","genLastStopTick","firstStopTick","lastStopTick","PendingStopRequestTick","lastStopToVerifyMs","genLastStopToVerifyMs"};
 static JsonNode? Normalize(JsonNode? node,string? key=null)
 {
  if(node is JsonObject obj){var result=new JsonObject();foreach(var pair in obj)result[pair.Key]=Normalize(pair.Value,pair.Key);return result;}
  if(node is JsonArray arr)return new JsonArray(arr.Select(x=>Normalize(x)).ToArray());
  if(node is JsonValue number&&key is not null&&MomentFields.Contains(key)&&number.TryGetValue<long>(out var tick))return JsonValue.Create(tick<0?-1:tick==0?0:1);
  return node?.DeepClone();
 }
 static JsonNode? Json(object? value)=>JsonSerializer.SerializeToNode(value);
 public sealed class LegacyDevice {public bool isReading;public bool IsOpen=>false;}
 public sealed class ReadOnlyLegacyDevice {public bool IsReading=>true;public bool IsOpen=>false;}
 public sealed class ThrowLegacyDevice {public bool IsReading=>throw new InvalidOperationException("fixture-reading-failure");public bool IsOpen=>false;}
 public sealed class LifecycleDevice
 {
  public string State="Running";public bool Settled=true,StopAccepts=true,VerifyEnds;public int StateReads,VersionReads,ReceiptReads,StopCalls,VerifyCalls;
  public string ReadLoopState {get{StateReads++;return State;}}
  public int LifecycleApiVersion {get{VersionReads++;return 1;}}
  public string LastCompatEntryOperationId=>"fixture-op";
  public bool TryWaitDeviceOpenReceipt(string operation,int milliseconds,out object? receipt){ReceiptReads++;receipt=null;return Settled;}
  public bool RequestReadLoopStop(string source){StopCalls++;return StopAccepts;}
  public bool TryWaitReadLoopEnded(int milliseconds,out string detail){VerifyCalls++;detail=VerifyEnds?"ended":"pending";return VerifyEnds;}
  public bool IsOpen=>false;
  public object Counters=>new{StateReads,VersionReads,ReceiptReads,StopCalls,VerifyCalls};
 }
 sealed class Fixture
 {
  public object Backend{get;}public object? Device{get;}public List<(string Name,JsonNode? Value)> Events{get;}=[];
  readonly FieldInfo? allocation=backend.GetField("MaintenanceResultListAllocationsForFixture",F);
  readonly long startAllocations;
  public Fixture(object? device=null,bool manager=false,bool clearDiagnosticActions=false)
  {
   HandheldCompanion.Managers.ControllerManager.Reset();
   var options=host.GetType("YeManFanHost.HostOptions",true)!.GetMethod("Parse",F)!.Invoke(null,[Array.Empty<string>()]);
   Backend=Activator.CreateInstance(backend,F,null,[options],null)!;Device=device;Set(Backend,"device",device);
   if(manager)Set(Backend,"assembly",typeof(HandheldCompanion.Managers.ControllerManager).Assembly);
   backend.GetProperty("Diagnostic",F)!.SetValue(Backend,(Action<string,object?>)((name,value)=>{Events.Add((name,Json(value)));if(clearDiagnosticActions&&name=="hc.controller-cpu-loops-stopped")((IList)value!.GetType().GetProperty("stopped")!.GetValue(value)!).Clear();}));
   startAllocations=allocation is null?0:Convert.ToInt64(allocation.GetValue(null));
  }
  public object Ledger(long generation=7)=>Call(Backend,"LedgerFor",generation)!;
  public long N(string field,long generation=7)=>Convert.ToInt64(Get(Ledger(generation),field));
  public void Pending(long tick=500)=>Set(Ledger(),"PendingStopRequestTick",tick);
  public bool Tick(long generation=7,double? cpu=201)=> (bool)Call(Backend,"StopHcControllerCpuLoops","fixture",generation,"FaultLocked",cpu)!;
  public long? Allocations=>allocation is null?null:Convert.ToInt64(allocation.GetValue(null))-startAllocations;
  public void VerifyAllocations(long candidateCount,long baselineCount=2)
  {
   if(mode=="allocation-candidate")Require(Allocations==candidateCount,$"result-list allocations expected {candidateCount}, got {Allocations}");
   else if(mode=="allocation-baseline")Require(Allocations==baselineCount,$"baseline result-list allocations expected {baselineCount}, got {Allocations}");
  }
  public void CheckArrays(int stopped,int failed)
  {
   var items=Events.Where(x=>x.Name=="hc.controller-cpu-loops-stopped").ToArray();Require(items.Length==1,"expected exactly one controller-loop result event");
   Require(items[0].Value?["stopped"] is JsonArray s&&s.Count==stopped,"stopped must remain the exact JSON array");
   Require(items[0].Value?["failures"] is JsonArray f&&f.Count==failed,"failures must remain the exact JSON array");
  }
  public object Snapshot()=>new{
   ledger=Normalize(Json(Prop(Backend,"HcLoopGenerationLedgersForSelfTest"))),
   evidence=Normalize(Json(Prop(Backend,"HcLoopStopEvidenceForSelfTest"))),
   pending=N("PendingStopRequestTick")==0?0:1,
   events=Events.Select(x=>new{name=x.Name,payload=Normalize(x.Value)}).ToArray(),
   probe=Device is LifecycleDevice l?l.Counters:null,
   legacyReading=Device is LegacyDevice legacy?(bool?)legacy.isReading:null,
   manager=new{HandheldCompanion.Managers.ControllerManager.PumpReads,HandheldCompanion.Managers.ControllerManager.PumpWrites,HandheldCompanion.Managers.ControllerManager.PumpValue,slotMode=HandheldCompanion.Managers.ControllerManager.slotManagementMode.ToString()}
  };
 }
 static void Check(string name,Func<object> test){try{results.Add(new{name,ok=true,observation=test()});}catch(Exception e){failures.Add(name+": "+e.Message);results.Add(new{name,ok=false,error=e.ToString()});}}
 public static int Main(string[] args)
 {
  if(args.Length!=3)throw new ArgumentException("Host DLL, behavior|allocation-baseline|allocation-candidate, result JSON required");
  string input=Path.GetFullPath(args[0]);mode=args[1];
  AssemblyLoadContext.Default.Resolving+=(_,n)=>{var p=Path.Combine(Path.GetDirectoryName(input)!,n.Name+".dll");return File.Exists(p)?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;};
  host=AssemblyLoadContext.Default.LoadFromAssemblyPath(input);backend=host.GetType("YeManFanHost.RealHcBackend",true)!;
  Check("no-device-idle",()=>{var f=new Fixture();var ret=f.Tick();Require(!ret&&f.N("IdleAttempts")==1&&f.Events.Count==0,"idle return/ledger/event changed");f.VerifyAllocations(0);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("legacy-idle",()=>{var f=new Fixture(new LegacyDevice());var ret=f.Tick();Require(!ret&&f.N("IdleAttempts")==1&&f.Events.Count==0,"legacy idle changed");f.VerifyAllocations(0);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("lifecycle-not-started",()=>{var d=new LifecycleDevice{State="NotStarted"};var f=new Fixture(d);var ret=f.Tick();Require(!ret&&d.ReceiptReads==0&&d.StopCalls==0&&d.VerifyCalls==0&&f.N("IdleAttempts")==1,"not-started acquired new duties");f.VerifyAllocations(0);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("pending-stop-is-not-idle-or-new-stop",()=>{var d=new LifecycleDevice();var f=new Fixture(d);f.Pending();var ret=f.Tick();Require(!ret&&d.VerifyCalls==1&&d.StopCalls==0&&f.N("IdleAttempts")==0&&f.N("StopAlreadyPending")==1&&f.N("VerifyPending")==1&&f.N("Attempts")==1&&f.N("LastCpuMsPerSec")==201,"pending classification/duties/cpu evidence changed");Require(f.Events.All(x=>x.Name!="hc.controller-cpu-loops-stopped"),"pending-only emitted stop result");f.VerifyAllocations(0);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("pending-late-verify-keeps-empty-arrays",()=>{var d=new LifecycleDevice{VerifyEnds=true};var f=new Fixture(d);f.Pending();var ret=f.Tick();Require(!ret&&d.VerifyCalls==1&&d.StopCalls==0&&f.N("StopVerified")==1&&f.N("PendingStopRequestTick")==0,"late verification settlement changed");f.CheckArrays(0,0);f.VerifyAllocations(0);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("pipeline-not-settled-keeps-failure",()=>{var d=new LifecycleDevice{Settled=false};var f=new Fixture(d);var ret=f.Tick();Require(!ret&&d.ReceiptReads==1&&d.StopCalls==0&&f.N("SettleDeferred")==1,"deferred pipeline requested stop or lost reason");f.CheckArrays(0,1);f.VerifyAllocations(1);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("request-rejected-keeps-failure",()=>{var d=new LifecycleDevice{StopAccepts=false};var f=new Fixture(d);var ret=f.Tick();Require(!ret&&d.StopCalls==1&&d.VerifyCalls==0&&f.N("StopRequests")==0,"rejected request changed");f.CheckArrays(0,1);f.VerifyAllocations(1);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("new-request-pending-keeps-action",()=>{var d=new LifecycleDevice();var f=new Fixture(d);var ret=f.Tick();Require(ret&&d.StopCalls==1&&d.VerifyCalls==1&&f.N("StopRequests")==1&&f.N("PendingStopRequestTick")>0,"new request pending evidence changed");f.CheckArrays(1,0);f.VerifyAllocations(1);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("new-request-immediate-verify",()=>{var d=new LifecycleDevice{VerifyEnds=true};var f=new Fixture(d);var ret=f.Tick();Require(ret&&d.StopCalls==1&&d.VerifyCalls==1&&f.N("StopVerified")==1&&f.N("PendingStopRequestTick")==0,"immediate verification changed");f.CheckArrays(1,0);f.VerifyAllocations(1);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("legacy-stop-preserved",()=>{var d=new LegacyDevice{isReading=true};var f=new Fixture(d);var ret=f.Tick();Require(ret&&!d.isReading&&f.N("StopCalls")==1,"legacy stop suppressed");f.CheckArrays(1,0);f.VerifyAllocations(1);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("return-count-is-read-after-diagnostic-callback",()=>{var d=new LegacyDevice{isReading=true};var f=new Fixture(d,clearDiagnosticActions:true);var ret=f.Tick();Require(!ret&&!d.isReading&&f.N("StopCalls")==1,"return froze list count before diagnostic callback");f.CheckArrays(1,0);f.VerifyAllocations(1);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("legacy-unwritable-failure",()=>{var f=new Fixture(new ReadOnlyLegacyDevice());var ret=f.Tick();Require(!ret&&f.N("IdleAttempts")==0,"unwritable stop was treated as idle");f.CheckArrays(0,1);f.VerifyAllocations(1);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("getter-exception-is-reported",()=>{var f=new Fixture(new ThrowLegacyDevice());var ret=f.Tick();Require(!ret&&f.N("IdleAttempts")==0,"getter exception erased");f.CheckArrays(0,1);f.VerifyAllocations(1);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("controller-pump-and-manual-mode-maintained",()=>{var f=new Fixture(manager:true);HandheldCompanion.Managers.ControllerManager.PumpValue=true;HandheldCompanion.Managers.ControllerManager.slotManagementMode=HandheldCompanion.Managers.Mode.Automatic;var ret=f.Tick();Require(ret&&!HandheldCompanion.Managers.ControllerManager.PumpValue&&HandheldCompanion.Managers.ControllerManager.slotManagementMode==HandheldCompanion.Managers.Mode.Manual,"controller stop/mode changed");f.CheckArrays(2,0);f.VerifyAllocations(1);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("successful-stop-survives-later-manager-failure",()=>{var d=new LegacyDevice{isReading=true};var f=new Fixture(d,true);HandheldCompanion.Managers.ControllerManager.ThrowPumpRead=true;var ret=f.Tick();Require(ret&&!d.isReading&&f.N("StopCalls")==1,"later failure lost completed action");f.CheckArrays(1,1);f.VerifyAllocations(2);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("one-hundred-idle-ticks-keep-all-live-reads-and-ledger",()=>{var d=new LifecycleDevice{State="NotStarted"};var f=new Fixture(d,true);bool ret=false;for(int i=0;i<100;i++)ret|=f.Tick();Require(!ret&&d.StateReads==100&&d.VersionReads==100&&f.N("Attempts")==100&&f.N("IdleAttempts")==100&&HandheldCompanion.Managers.ControllerManager.PumpReads==100&&f.Events.Count==0,"idle checks were skipped or cached");f.VerifyAllocations(0,200);return new{ret,allocationCount=f.Allocations,state=f.Snapshot()};});
  Check("request-observe-final-verify-single-generation",()=>{var d=new LifecycleDevice();var f=new Fixture(d);var first=f.Tick();var second=f.Tick();d.VerifyEnds=true;var third=f.Tick();Require(first&&!second&&!third&&d.StopCalls==1&&d.VerifyCalls==3&&f.N("StopRequests")==1&&f.N("StopAlreadyPending")==1&&f.N("StopVerified")==1&&f.N("IdleAttempts")==0&&f.N("Attempts")==3,"same-generation settle/request/observe/verify sequence changed");Require(f.Events.Count(x=>x.Name=="hc.controller-cpu-loops-stopped")==2,"sequence result count changed");f.VerifyAllocations(1,6);return new{first,second,third,allocationCount=f.Allocations,state=f.Snapshot()};});
  foreach(var method in new[]{"RunHcLoopBudgetSelfTest","RunHcLoopTargetASelfTest"})Check(method,()=>{var values=(IEnumerable)backend.GetMethod(method,F)!.Invoke(null,null)!;return new{passed=values.Cast<string>().ToArray()};});
  Check("real-HC-is-not-loaded",()=>{Require(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),"real HC unexpectedly loaded");return new{actualHostMain=false,actualHc=false,hardwareCalls=0};});
  var result=new{ok=failures.Count==0,checks=results.Count,mode,hostDllSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(input))).ToLowerInvariant(),results,failures,scope="actual production StopHcControllerCpuLoops/EvaluateLoopStop/ledger and shared Host selftests, inert reflection targets; no Host Main/HC/device/benchmark",allocationMeaning="only construction expressions for the two result buffers are counted in optional scratch-instrumented builds; no process CPU/time/GC benchmark"};
  var json=JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true});File.WriteAllText(args[2],json);Console.WriteLine(JsonSerializer.Serialize(new{result.ok,result.checks,mode,failures}));return failures.Count==0?0:1;
 }
}
namespace HandheldCompanion.Managers
{
 public enum Mode {Automatic,Manual}
 public static class ControllerManager
 {
  public static bool PumpValue,ThrowPumpRead;public static int PumpReads,PumpWrites;public static Mode slotManagementMode=Mode.Manual;
  public static bool pumpThreadRunning {get{PumpReads++;if(ThrowPumpRead)throw new InvalidOperationException("fixture-pump-read-failure");return PumpValue;}set{PumpWrites++;PumpValue=value;}}
  public static void Reset(){PumpValue=ThrowPumpRead=false;PumpReads=PumpWrites=0;slotManagementMode=Mode.Manual;}
 }
}
