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
  public Fixture(){var options=host.GetType("YeManFanHost.HostOptions",true)!.GetMethod("Parse",F)!.Invoke(null,[Array.Empty<string>()]);Backend=Activator.CreateInstance(backend,F,null,[options],null)!;var route=backend.GetMethod("ResolveRouteForSelfTest",F)!.Invoke(null,["HandheldCompanion.Devices.GPDWin5"])!;Call(Backend,"InstallSelfTestRouteSessionForSelfTest",Device,route,true);Set(Backend,"activeCurveDuties",new double[11]);}
  public void Dispose(){Call(Backend,"DetachCpuTemperatureLoop");Set(Backend,"activeCurveDuties",null);Set(Backend,"activeFanProfile",null);backend.GetProperty("IsOpen",F)!.SetValue(Backend,false);Set(Backend,"openAttempted",false);Set(Backend,"hcOpenInvocationStarted",false);Set(Backend,"device",null);Set(Backend,"sessionDeviceReference",null);((IDisposable)Backend).Dispose();}
 }
 static int RunInt(object owner,Func<int> action,string name){try{return(int)backend.GetMethods(F).Single(m=>m.Name=="OnStaBounded"&&m.IsGenericMethodDefinition).MakeGenericMethod(typeof(int)).Invoke(owner,[action,name])!;}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
 public static int Main(string[] args){try{
  var dll=Path.GetFullPath(args[0]);var resolver=new AssemblyDependencyResolver(dll);AssemblyLoadContext.Default.Resolving+=(_,n)=>resolver.ResolveAssemblyToPath(n)is{}p?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;
  host=AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);backend=host.GetType("YeManFanHost.RealHcBackend",true)!;host.GetType("YeManFanHost.Program",true)!.GetProperty("IsSelfTestProcess",F)!.SetValue(null,true);
  var method=backend.GetMethods(F).Single(m=>m.Name=="OnStaBounded"&&m.IsGenericMethodDefinition);var il=Instructions(method);
  var closures=il.Count(x=>x.Op==OpCodes.Newobj&&x.Method?.DeclaringType?.Name.Contains("DisplayClass",StringComparison.Ordinal)==true);
  Check("compiled STA dispatch has no per-call DisplayClass construction",()=>Require(closures==0,$"expected zero forwarding closures, found {closures}"));
  using var a=new Fixture();using var b=new Fixture();
  var caller=Environment.CurrentManagedThreadId;int staA=0,staB=0;
  Check("first queued task returns its own value on STA",()=>{Require(RunInt(a.Backend,()=>{staA=Environment.CurrentManagedThreadId;Require(Thread.CurrentThread.GetApartmentState()==ApartmentState.STA,"wrong apartment");return 17;},"binding-a-first")==17,"first result changed");Require(staA!=caller,"caller executed queued work");});
  Check("next queued task consumes the next action exactly once",()=>{var calls=0;var value=RunInt(a.Backend,()=>{calls++;return 29;},"binding-a-next");Require(value==29&&calls==1,"previous task/action was reused or duplicated");});
  Check("second backend does not share the first backend callback",()=>{Require(RunInt(b.Backend,()=>{staB=Environment.CurrentManagedThreadId;return 43;},"binding-b-first")==43,"wrong backend result");Require(staB!=caller&&staB!=staA,"backend STA identity leaked");});
  Check("completed tasks retire their exact queue ownership",()=>Require(Get(a.Backend,"activeStaWorkItem") is null&&Get(b.Backend,"activeStaWorkItem") is null,"completed task still owns a queue"));
  Check("nested same-STA call keeps the original task and runs inline",()=>{
    var result=RunInt(a.Backend,()=>{var outer=Get(a.Backend,"activeStaWorkItem");var thread=Environment.CurrentManagedThreadId;var value=RunInt(a.Backend,()=>{Require(Environment.CurrentManagedThreadId==thread,"nested call moved thread");Require(ReferenceEquals(Get(a.Backend,"activeStaWorkItem"),outer),"nested call replaced outer owner");return 59;},"binding-inline");return value+1;},"binding-outer");Require(result==60,"nested result changed");
  });
  var fault=new InvalidOperationException("fixture-dispatch-target-error");
  Check("target exception remains the exact caller-visible failure",()=>{Exception? found=null;try{RunInt(a.Backend,()=>throw fault,"binding-failure");}catch(Exception error){found=error;}Require(ReferenceEquals(found,fault),"exception instance or type was replaced");});
  Check("a failure retires ownership and leaves the next task usable",()=>{Require(Get(a.Backend,"activeStaWorkItem") is null,"failed task was not retired");Require(RunInt(a.Backend,()=>71,"binding-after-failure")==71,"later task did not recover");});
  Check("parallel backends retain separate live actions and results",()=>{using var go=new ManualResetEventSlim();var one=Task.Run(()=>RunInt(a.Backend,()=>{go.Wait();return 83;},"binding-parallel-a"));var two=Task.Run(()=>RunInt(b.Backend,()=>{go.Wait();return 97;},"binding-parallel-b"));go.Set();Require(Task.WaitAll([one,two],TimeSpan.FromSeconds(3)),"parallel fixture tasks did not settle");Require(one.Result==83&&two.Result==97,"parallel result/action crossed backends");});
  Check("public live verifier still checks the current marker",()=>{Require(Verify(a.Backend)is null,"healthy marker was rejected");a.Device.Value=false;Require(Verify(a.Backend)is not null,"closed marker was cached healthy");a.Device.Value=true;Require(Verify(a.Backend)is null,"corrected marker could not recover");});
  Check("live marker getter failure does not poison the next task",()=>{a.Device.Throw=true;Require(Verify(a.Backend)is not null,"getter failure was cached healthy");a.Device.Throw=false;Require(Verify(a.Backend)is null,"corrected getter could not recover");});
  Check("a finished dispatcher starts no competing hardware source",()=>Require(!AppDomain.CurrentDomain.GetAssemblies().Any(x=>x.GetName().Name=="HandheldCompanion"),"real HC dependency loaded"));
  var result=new{ok=failures.Count==0,checks,failures,results,il=new{forwardingDisplayClassConstructors=closures},hostSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dll))).ToLowerInvariant(),hardwareCalls=0,cpuBenchmark=false,scope="compiled OnStaBounded; real WPF STA/task ownership and public verifier; inert device/route marker, no Host Main, HC assembly or device operations"};
  File.WriteAllText(args[1],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.ok,result.checks,result.failures,result.il}));return result.ok?0:1;
 }catch(Exception e){Console.Error.WriteLine(e);return 2;}}
}
