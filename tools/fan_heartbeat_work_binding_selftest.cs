// Compile and inspect the exact scratch Host; invoke its real STA verifier with inert device objects only.
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
sealed class OpenProbe {
 public volatile bool Value=true, Throw;
 public int Reads;
 public bool IsOpen { get { Interlocked.Increment(ref Reads);if(Throw)throw new InvalidOperationException("fixture-open-probe-failure");return Value; } }
}
static class Checks {
 const BindingFlags F=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
 static Assembly host=null!;static Type backend=null!;static int checks;static readonly List<object> results=[];static readonly List<string> failures=[];
 static void Check(string name,Action work){checks++;try{work();results.Add(new{name,ok=true});}catch(Exception e){failures.Add(name+": "+e.Message);results.Add(new{name,ok=false,error=e.ToString()});}}
 static void Require(bool ok,string message){if(!ok)throw new InvalidOperationException(message);}
 static object? Get(object o,string name)=>o.GetType().GetField(name,F)?.GetValue(o);
 static void Set(object o,string name,object? value)=>o.GetType().GetField(name,F)!.SetValue(o,value);
 static object? Call(object o,string name,params object?[] args){try{return o.GetType().GetMethods(F).Single(m=>m.Name==name&&!m.IsGenericMethod&&m.GetParameters().Length==args.Length).Invoke(o,args);}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
 static Exception? Verify(object o){try{Call(o,"VerifyActiveCurveSession");return null;}catch(Exception e){return e;}}
 static List<(OpCode Op,MethodBase? Method)> Instructions(MethodBase method){
  var bytes=method.GetMethodBody()!.GetILAsByteArray()!;var code=typeof(OpCodes).GetFields(BindingFlags.Public|BindingFlags.Static).Where(f=>f.FieldType==typeof(OpCode)).Select(f=>(OpCode)f.GetValue(null)!).ToDictionary(x=>(ushort)x.Value);
  var rows=new List<(OpCode,MethodBase?)>();for(var i=0;i<bytes.Length;){var value=(ushort)bytes[i++];if(value==0xfe)value=(ushort)(0xfe00|bytes[i++]);var op=code[value];var size=op.OperandType switch{OperandType.InlineNone=>0,OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar=>1,OperandType.InlineVar=>2,OperandType.InlineI8 or OperandType.InlineR=>8,OperandType.InlineSwitch=>4+4*BitConverter.ToInt32(bytes,i),_=>4};MethodBase? called=null;if(op.OperandType==OperandType.InlineMethod)called=method.Module.ResolveMethod(BitConverter.ToInt32(bytes,i),method.DeclaringType?.GetGenericArguments(),method.IsGenericMethod?method.GetGenericArguments():null);rows.Add((op,called));i+=size;}return rows;
 }
 sealed class Fixture:IDisposable {
  public readonly object Backend;public readonly OpenProbe Device=new();
  public Fixture(){var options=host.GetType("YeManFanHost.HostOptions",true)!.GetMethod("Parse",F)!.Invoke(null,[Array.Empty<string>()]);Backend=Activator.CreateInstance(backend,F,null,[options],null)!;var route=backend.GetMethod("ResolveRouteForSelfTest",F)!.Invoke(null,["HandheldCompanion.Devices.GPD.GPDWin5"])!;Call(Backend,"InstallSelfTestRouteSessionForSelfTest",Device,route,true);Set(Backend,"activeCurveDuties",new double[11]);}
  public void Dispose(){Call(Backend,"DetachCpuTemperatureLoop");Set(Backend,"activeCurveDuties",null);Set(Backend,"activeFanProfile",null);backend.GetProperty("IsOpen",F)!.SetValue(Backend,false);Set(Backend,"openAttempted",false);Set(Backend,"hcOpenInvocationStarted",false);Set(Backend,"device",null);Set(Backend,"sessionDeviceReference",null);((IDisposable)Backend).Dispose();}
 }
 public static int Main(string[] args){try{
  var dll=Path.GetFullPath(args[0]);var resolver=new AssemblyDependencyResolver(dll);AssemblyLoadContext.Default.Resolving+=(_,n)=>resolver.ResolveAssemblyToPath(n) is {} p?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;
  host=AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);backend=host.GetType("YeManFanHost.RealHcBackend",true)!;host.GetType("YeManFanHost.Program",true)!.GetProperty("IsSelfTestProcess",F)!.SetValue(null,true);
  var method=backend.GetMethod("VerifyActiveCurveSession",F)!;var instructions=Instructions(method);
  var delegateNews=instructions.Count(x=>x.Op==OpCodes.Newobj&&x.Method?.DeclaringType is {} t&&typeof(Delegate).IsAssignableFrom(t));
  var wrappers=instructions.Count(x=>x.Method is MethodInfo m&&m.Name=="OnStaBounded"&&!m.IsGenericMethod&&m.GetParameters()[0].ParameterType==typeof(Action));
  var genericCalls=instructions.Count(x=>x.Method is MethodInfo m&&m.Name=="OnStaBounded"&&m.IsGenericMethod&&m.GetGenericArguments().Single()==typeof(bool));
  var field=backend.GetField("heartbeatVerificationWork",F);
  Check("steady verifier has no per-call delegate construction in IL",()=>Require(delegateNews==0,$"delegate constructors per public method IL={delegateNews}"));
  Check("steady verifier avoids the Action-to-Func adapter",()=>Require(wrappers==0&&genericCalls==1,$"Action wrapper calls={wrappers}, bool generic calls={genericCalls}"));
  Check("cached work is a readonly per-backend Func<bool>",()=>Require(field is not null&&!field.IsStatic&&field.IsInitOnly&&field.FieldType==typeof(Func<bool>),"missing immutable backend-owned work binding"));
  using var fixture=new Fixture();var b=fixture.Backend;
  var bound=field?.GetValue(b) as Delegate;
  Check("bound work belongs to the current Backend",()=>Require(bound is not null&&ReferenceEquals(bound.Target,b),"work target is not this backend"));
  var threads=new HashSet<int>();var operations=new List<string>();
  backend.GetProperty("Diagnostic",F)!.SetValue(b,(Action<string,object?>)((name,data)=>{if(name=="hc-operation.begin"){lock(threads){threads.Add(Environment.CurrentManagedThreadId);var op=data?.GetType().GetProperty("operation")?.GetValue(data) as string;operations.Add(op??"missing");}}}));
  var reads=fixture.Device.Reads;var perf=host.GetType("YeManFanHost.FanPerfCounters",true)!.GetField("StaVerifyRequests",F)!;var before=(long)perf.GetValue(null)!;
  for(var i=0;i<6;i++)Check("live readiness succeeds on heartbeat "+i,()=>Require(Verify(b) is null,"healthy live marker was rejected"));
  Check("every heartbeat still reads the live HC marker",()=>Require(fixture.Device.Reads-reads==6,"live reads were cached or skipped"));
  Check("every heartbeat still increments the existing request counter",()=>Require((long)perf.GetValue(null)!-before==6,"verification counter was skipped"));
  Check("heartbeats retain one STA owner and operation identity",()=>{lock(threads)Require(threads.Count==1&&!threads.Contains(Environment.CurrentManagedThreadId)&&operations.Count==6&&operations.All(x=>x=="heartbeat-session-verify"),"STA or operation-name contract changed");});
  Check("cached work reference remains identical after warm calls",()=>Require(bound is not null&&ReferenceEquals(bound,field!.GetValue(b)),"work delegate was rebuilt"));
  Check("live false is rejected without replacing the work binding",()=>{fixture.Device.Value=false;var error=Verify(b);Require(error?.ToString().Contains("hc-device-is-open-false")==true,"closed HC marker was accepted");});
  Check("the next live true recovers through the same verifier",()=>{fixture.Device.Value=true;Require(Verify(b) is null,"temporary false permanently locked verification");});
  Check("live getter error preserves its failure",()=>{fixture.Device.Throw=true;var error=Verify(b);Require(error?.ToString().Contains("fixture-open-probe-failure")==true,"getter failure was hidden");});
  Check("getter recovery is not cached as a failure",()=>{fixture.Device.Throw=false;Require(Verify(b) is null,"getter error poisoned future heartbeats");});
  Check("another Backend owns another work delegate",()=>{using var other=new Fixture();var second=field?.GetValue(other.Backend) as Delegate;Require(second is not null&&ReferenceEquals(second.Target,other.Backend)&&!ReferenceEquals(bound,second),"work delegate crosses backend instances");});
  Check("cached work follows a replacement device in the same Backend",()=>{
   var replacement=new OpenProbe{Value=false};var count=fixture.Device.Reads;
   var route=backend.GetMethod("ResolveRouteForSelfTest",F)!.Invoke(null,["HandheldCompanion.Devices.GPD.GPDWin5"])!;
   try{Call(b,"InstallSelfTestRouteSessionForSelfTest",replacement,route,true);var error=Verify(b);Require(error?.ToString().Contains("hc-device-is-open-false")==true&&replacement.Reads==1&&fixture.Device.Reads==count,"cached work kept an old device");}
   finally{Call(b,"InstallSelfTestRouteSessionForSelfTest",fixture.Device,route,true);}
  });
  Check("reinstalled healthy device remains reachable through the same work",()=>Require(Verify(b) is null,"replacement failure poisoned the Backend"));
  Check("inactive curve still needs no device read",()=>{Set(b,"activeCurveDuties",null);var count=fixture.Device.Reads;Require(Verify(b) is null&&fixture.Device.Reads==count,"inactive heartbeat entered device readiness");});
  ((System.Windows.Threading.Dispatcher)Get(b,"staDispatcher")!).Invoke(()=>{},System.Windows.Threading.DispatcherPriority.Send);
  var logType=host.GetType("YeManFanHost.HostDiagnosticLog",true)!;
  var logRoot=Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[1]))!,"isolated-log-gate");Directory.CreateDirectory(logRoot);
  var logger=Activator.CreateInstance(logType,F,null,[logRoot,0],null)!;
  var runtimePath=(string)logType.GetProperty("Path",F)!.GetValue(logger)!;
  string faultPath=Path.Combine(logRoot,"fan-host-fault.log"),flag=Path.Combine(logRoot,"fan-logging-enabled.flag");
  var capture=logType.GetProperty("SelfTestCaptureEnabled",F)!;capture.SetValue(null,false);
  var previousDiagnostic=backend.GetProperty("Diagnostic",F)!.GetValue(b);
  var contexts=0;var routinePayloads=0;using var logReturned=new ManualResetEventSlim(false);
  var contextProvider=(Func<object?>)(()=>{contexts++;return new{scope="inert-context-counter"};});
  logType.GetMethod("SetContextProvider",F)!.Invoke(logger,[contextProvider]);
  var logWrite=logType.GetMethod("Write",F)!;var lazyWrite=logType.GetMethod("WriteLazy",F)!;
  backend.GetProperty("Diagnostic",F)!.SetValue(b,(Action<string,object?>)((name,data)=>{try{if(name is "hc-operation.begin" or "hc-operation.return")routinePayloads++;logWrite.Invoke(logger,[name,data]);}finally{if(name=="hc-operation.return")logReturned.Set();}}));
  Exception? LoggedVerify(){logReturned.Reset();var error=Verify(b);Require(logReturned.Wait(2000),"diagnostic return did not settle");return error;}
  Set(b,"activeCurveDuties",new double[11]);
  int Lines(string path)=>File.Exists(path)?File.ReadLines(path).Count():0;
  int RoutineLines()=>!File.Exists(runtimePath)?0:File.ReadLines(runtimePath).Select(line=>JsonDocument.Parse(line)).Count(doc=>{using(doc)return doc.RootElement.GetProperty("eventName").GetString() is "hc-operation.begin" or "hc-operation.return";});
  Check("closed real log gate leaves heartbeat successful without runtime records",()=>{for(var i=0;i<3;i++)Require(LoggedVerify() is null,"log gate blocked live heartbeat");Require(!File.Exists(runtimePath)&&contexts==0&&routinePayloads==6,"closed gate created context/runtime or lost existing routine callback shape");});
  Check("open real log gate keeps lightweight heartbeat evidence without heavy context",()=>{File.WriteAllText(flag,"enabled");Require(LoggedVerify() is null,"enabled logger blocked heartbeat");Require(Lines(runtimePath)==2&&contexts==0,"routine success built full context or lost its two records");});
  Check("legacy true log flag remains accepted",()=>{File.WriteAllText(flag,"1");Require(LoggedVerify() is null&&Lines(runtimePath)==4,"legacy explicit true changed");});
  Check("turning the real log gate off does not stop verification",()=>{File.Delete(flag);Require(LoggedVerify() is null&&Lines(runtimePath)==4&&contexts==0,"closed gate affected control or continued runtime logging");});
  Check("malformed log flag stays closed without locking verification",()=>{File.WriteAllText(flag,"not-enabled");Require(LoggedVerify() is null&&Lines(runtimePath)==4,"invalid flag changed control or enabled logs");File.Delete(flag);});
  Check("closed-gate responsibility evidence still reaches its fault ledger",()=>{logWrite.Invoke(logger,["power.suspend-preclose-release-decision",new{scope="fixture-only-responsibility"}]);Require(Lines(faultPath)==1&&contexts==1,"closed detailed gate erased responsibility evidence");});
  Check("unreadable flag cannot poison heartbeat or responsibility logging",()=>{File.WriteAllText(flag,"enabled");using(var blocked=new FileStream(flag,FileMode.Open,FileAccess.ReadWrite,FileShare.None)){Require(LoggedVerify() is null,"flag read failure escaped diagnostics into heartbeat");logWrite.Invoke(logger,["power.suspend-preclose-release-decision",new{scope="fixture-only-unreadable-flag"}]);}File.Delete(flag);Require(RoutineLines()==4&&Lines(faultPath)==2,"flag read failure lost responsibility or invented routine logging");Require(LoggedVerify() is null,"flag failure permanently poisoned future heartbeats");});
  var factories=0;Func<object?> payload=()=>{factories++;return new{scope="inert-lazy-payload"};};
  Check("closed lazy gate never evaluates its optional payload factory",()=>{lazyWrite.Invoke(logger,["hc.fan-dispatch.begin",payload]);Require(factories==0,"closed gate evaluated optional data");});
  Check("enabled lazy gate evaluates exactly once",()=>{File.WriteAllText(flag,"enabled");lazyWrite.Invoke(logger,["hc.fan-dispatch.begin",payload]);Require(factories==1,"enabled lazy payload was lost or repeated");File.Delete(flag);});
  Check("responsibility lazy payload survives the closed detailed gate",()=>{lazyWrite.Invoke(logger,["power.suspend-preclose-release-decision",payload]);Require(factories==2&&Lines(faultPath)==3,"closed gate skipped lazy responsibility");});
  Check("self-test capture still sees routine evidence with disk gate closed",()=>{logType.GetMethod("ResetSelfTestCapture",F)!.Invoke(null,null);capture.SetValue(null,true);try{lazyWrite.Invoke(logger,["hc.fan-dispatch.begin",payload]);Require(factories==3,"self-test capture skipped its payload");}finally{capture.SetValue(null,false);logType.GetMethod("ResetSelfTestCapture",F)!.Invoke(null,null);}});
  backend.GetProperty("Diagnostic",F)!.SetValue(b,previousDiagnostic);
  Check("no HC assembly was loaded",()=>Require(!AppDomain.CurrentDomain.GetAssemblies().Any(x=>x.GetName().Name=="HandheldCompanion"),"real HC dependency loaded"));
  var result=new{ok=failures.Count==0,checks,failures,results,il=new{delegateConstructors=delegateNews,actionAdapterCalls=wrappers,boolGenericCalls=genericCalls},hostSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dll))).ToLowerInvariant(),hardwareCalls=0,cpuBenchmark=false,scope="compiled public VerifyActiveCurveSession on real bounded STA; inert route/device marker only, no Host Main, HC assembly, device writes or CPU/allocation benchmark"};
  File.WriteAllText(args[1],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.ok,result.checks,result.failures,result.il}));return result.ok?0:1;
 }catch(Exception e){Console.Error.WriteLine(e);return 2;}}
}
