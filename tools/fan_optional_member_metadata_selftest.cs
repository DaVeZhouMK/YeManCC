using System.Collections;
using System.Collections.Concurrent;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
internal static class Checks {
 const BindingFlags All=BindingFlags.Instance|BindingFlags.Static|BindingFlags.Public|BindingFlags.NonPublic;
 static Assembly host=null!;static Type backend=null!;static MethodInfo getMember=null!;static string mode="";static readonly List<object> results=[];static readonly List<string> failures=[];static readonly List<object> observations=[];
 static void Require(bool ok,string message){if(!ok)throw new InvalidOperationException(message);}
 static object? Call(MethodInfo m,object? target,params object?[] args){try{return m.Invoke(target,args);}catch(TargetInvocationException ex)when(ex.InnerException is not null){throw ex.InnerException;}}
 static object? Member(object target,string name)=>Call(getMember,null,target,name);
 static long Walks()=>backend.GetField("OptionalMemberMetadataWalksForSelfTest",All)?.GetValue(null) is long n?n:-1;
 static IDictionary? Missing() => backend.GetField("absentInstanceMemberMetadata",All)?.GetValue(null) as IDictionary;
 static int MissingCount()=>Missing()?.Count??0;
 static void Check(string name,Action test){try{test();results.Add(new{name,ok=true});Console.WriteLine("PASS "+name);}catch(Exception ex){failures.Add(name+": "+ex.Message);results.Add(new{name,ok=false,error=ex.Message});Console.WriteLine("FAIL "+name+": "+ex.Message);}}
 sealed class MsiLikeNoLoops {public bool IsOpen{get;set;}}
 sealed class NullValueProbe {public string? Value{get;set;}public int Reads;public string? Current{get{Reads++;return Value;}}}
 sealed class LiveProbe {public int Number;public bool IsOpen{get;set;}public string? Value{get;set;}}
 class BaseProbe {private int Secret=41;public int Touch()=>Secret;}
 sealed class DerivedProbe:BaseProbe {public int Value{get;set;}=77;}
 class ShadowBase {private int Shared=>10;}
 sealed class ShadowDerived:ShadowBase {private int Shared=20;public int Touch()=>Shared;}
 sealed class ThrowProbe {public bool Fail=true;public int Calls;public object? Receipt{get{Calls++;if(Fail)throw new InvalidOperationException("fixture-getter-error");return "new-receipt";}}}
 static IDictionary? MissingMethods()=>backend.GetField("absentInstanceMethodMetadata",All)?.GetValue(null) as IDictionary;
 static (bool Found,object? Value) InvokeMethod(object target,string name,params object?[] arguments){var invocation=new object?[]{target,name,arguments,null};var found=(bool)Call(backend.GetMethod("TryInvokeCached",All)!,null,invocation)!;return(found,invocation[3]);}
 sealed class MethodProbe { public int Current;public int Calls;public bool Fail;public object? Receipt;public int Read(int add){Calls++;return Current+add;}public object? ReadReceipt(){Calls++;return Receipt;}public int Throws(){Calls++;if(Fail)throw new InvalidOperationException("fixture-method-error");return Current;}private int PrivateReceipt()=>Current;public int OnlyZero()=>Current;public static int StaticOnly()=>12; }
 sealed class LifecycleMethodProbe {public bool Ended;public bool Settled;public int Calls;public string LastCompatEntryOperationId=>"fixture-op";public bool TryWaitReadLoopEnded(int timeoutMs,out string detail){if(timeoutMs!=0)throw new InvalidOperationException("unexpected wait");Calls++;detail=Ended?"done":"pending-fixture";return Ended;}public bool TryWaitDeviceOpenReceipt(string operationId,int timeoutMs,out object? receipt){if(operationId!="fixture-op"||timeoutMs!=0)throw new InvalidOperationException("unexpected receipt arguments");Calls++;receipt=Settled?"terminal":null;return Settled;}}
 static object MakeMethodType(string assemblyName,bool present){var a=AssemblyBuilder.DefineDynamicAssembly(new AssemblyName(assemblyName),AssemblyBuilderAccess.Run);var b=a.DefineDynamicModule("M").DefineType("Fixtures.SameMethodName",TypeAttributes.Public);if(present){var m=b.DefineMethod("Optional",MethodAttributes.Public,typeof(int),Type.EmptyTypes);var il=m.GetILGenerator();il.Emit(OpCodes.Ldc_I4,91);il.Emit(OpCodes.Ret);}return Activator.CreateInstance(b.CreateType()!)!;}
 static object MakeSameName(string assemblyName,bool present) {var a=AssemblyBuilder.DefineDynamicAssembly(new AssemblyName(assemblyName),AssemblyBuilderAccess.Run);var m=a.DefineDynamicModule("M");var b=m.DefineType("Fixtures.SameName",TypeAttributes.Public);if(present)b.DefineField("Optional",typeof(int),FieldAttributes.Public);var type=b.CreateType()!;var instance=Activator.CreateInstance(type)!;if(present)type.GetField("Optional")!.SetValue(instance,93);return instance;}
 public static int Main(string[] args) {
 var dll=Path.GetFullPath(args[0]);mode=args[1];var resultFile=args[2];var resolver=new AssemblyDependencyResolver(dll);AssemblyLoadContext.Default.Resolving+=(_,n)=>resolver.ResolveAssemblyToPath(n) is {} p?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;host=AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);backend=host.GetType("YeManFanHost.RealHcBackend",true)!;getMember=backend.GetMethod("GetMember",All)!;
 Check("one thousand absent optional lookups retain null and avoid repeated metadata walks",()=>{
  var device=new MsiLikeNoLoops();var names=new[]{"ReadLoopState","LifecycleApiVersion","IsReading","isReading"};var before=Walks();var missingBefore=MissingCount();for(var i=0;i<1000;i++)Require(Member(device,names[i%4]) is null,"missing member became false or a successful receipt");var count=Walks()<0?-1:Walks()-before;var growth=MissingCount()-missingBefore;
  if(mode=="legacy")Require(count==1000&&growth==0,"legacy work count differs from exact baseline");else {if(count>=0)Require(count==4,"cached miss still repeated metadata search");Require(growth==4,"cache must retain one absent key per Type/name");}
  observations.Add(new{scenario="1000-absent-instance-member-queries",requests=1000,metadataWalks=count,absentKeysAdded=growth,valuesAllNull=true});
 });
 Check("existing fields and properties stay live through cache hits and distinct devices",()=>{
  var first=new LiveProbe{Number=2,IsOpen=true};var second=new LiveProbe{Number=9,IsOpen=false};for(var i=0;i<100;i++){first.Number=i;first.IsOpen=i%2==0;Require(Equals(Member(first,"Number"),i)&&Equals(Member(first,"IsOpen"),first.IsOpen),"cached live value became stale");Require(Equals(Member(second,"Number"),9)&&Equals(Member(second,"IsOpen"),false),"prior object value crossed device identity");}
 });
 Check("null getter values are real existing metadata and later new receipts remain visible",()=>{
  var p=new NullValueProbe();var before=MissingCount();Require(Member(p,"Current") is null,"initial null changed");p.Value="current-receipt";Require(Equals(Member(p,"Current"),"current-receipt"),"null getter was cached as nonexistent metadata");p.Value=null;Require(Member(p,"Current") is null&&p.Reads==3,"getter stopped running");Require(MissingCount()==before,"existing null getter entered absent metadata cache");
 });
 Check("getter exceptions repeat honestly and recovery is not hidden by the absence cache",()=>{
  var p=new ThrowProbe();var before=MissingCount();for(var i=0;i<2;i++){try{Member(p,"Receipt");throw new Exception("getter failure swallowed");}catch(TargetInvocationException ex){Require(ex.InnerException is InvalidOperationException inner&&inner.Message=="fixture-getter-error","getter exception shape changed");}}p.Fail=false;Require(Equals(Member(p,"Receipt"),"new-receipt")&&p.Calls==3,"cached exception suppressed later current value");Require(MissingCount()==before,"throwing getter was marked absent");
 });
 Check("derived-before-base and private metadata lookup order stays identical",()=>{
  Require(Equals(Member(new DerivedProbe(),"Secret"),41),"private base field was hidden");Require(Equals(Member(new ShadowDerived(),"Shared"),20),"base property incorrectly beat a derived field");
 });
 Check("cache remains case-sensitive and a missing spelling cannot hide an existing field",()=>{
  var p=new LiveProbe{Number=63};Require(Member(p,"number") is null,"case-insensitive query gained a value");Require(Equals(Member(p,"Number"),63),"missing lower-case spelling poisoned correct metadata");
 });
 Check("the same full type name in a different assembly never reuses another absence",()=>{
  var absent=MakeSameName("FanAbsentMetadataA",false);var present=MakeSameName("FanAbsentMetadataB",true);Require(absent.GetType().FullName==present.GetType().FullName,"fixture type names differ");Require(Member(absent,"Optional") is null,"missing fixture gained a field");Require(Equals(Member(present,"Optional"),93),"name-only absence poisoned a different runtime Type");
 });
 Check("parallel missing and present queries keep per-object values and exact unknowns",()=>{
  var bad=new ConcurrentQueue<string>();Parallel.For(0,100,i=>{var p=new LiveProbe{Number=i};if(!Equals(Member(p,"Number"),i)||Member(p,"ParallelMissing") is not null)bad.Enqueue(i.ToString());});Require(bad.IsEmpty,"concurrent lookups changed values");
 });
 Check("actual HC-loop maintenance continues to run without granting a fake MSI loop or control",()=>{
  var options=host.GetType("YeManFanHost.HostOptions",true)!.GetMethod("Parse",All)!.Invoke(null,[Array.Empty<string>()]);var b=Activator.CreateInstance(backend,All,null,[options],null)!;var device=new MsiLikeNoLoops();backend.GetField("device",All)!.SetValue(b,device);Require(backend.GetField("assembly",All)!.GetValue(b) is null,"HC assembly unexpectedly installed");var stop=backend.GetMethod("StopHcControllerCpuLoops",All)!;
  for(var i=0;i<4;i++)Require(Equals(Call(stop,b,"metadata-only-fixture",1L,"Suspended",null),false),"missing methods produced a stop action");Require(Convert.ToInt64(backend.GetField("hcLoopStopAttempts",All)!.GetValue(b))==4,"maintenance calls were skipped");Require(backend.GetField("device",All)!.GetValue(b)==device&&!device.IsOpen,"metadata changed device state");backend.GetField("device",All)!.SetValue(b,null);((IDisposable)b).Dispose();
 });
 Check("absent optional methods remain absent and retain exactly one Type/name/arity miss",()=>{
  var p=new MethodProbe();var cache=MissingMethods();var count=cache?.Count??0;for(var i=0;i<1000;i++){var result=InvokeMethod(p,"NotInThisHcContract",17);Require(!result.Found&&result.Value is null,"missing method gained a result");}Require(cache is not null&&cache.Count==count+1&&cache.Contains((p.GetType(),"NotInThisHcContract",1)),"missing method metadata was not cached once");observations.Add(new{scenario="1000-absent-optional-method-queries",requests=1000,absentKeysAdded=cache!.Count-count,valuesAllAbsent=true});
 });
 Check("cached methods read current values on every invocation and never share device values",()=>{
  var p=new MethodProbe();var other=new MethodProbe{Current=900};for(var i=0;i<80;i++){p.Current=i;var a=InvokeMethod(p,"Read",3);var b=InvokeMethod(other,"Read",4);Require(a.Found&&Equals(a.Value,i+3)&&b.Found&&Equals(b.Value,904),"cached return value crossed calls or devices");}Require(p.Calls==80&&other.Calls==80,"existing methods were skipped");
 });
 Check("existing null method results never become absent metadata",()=>{
  var p=new MethodProbe();var before=MissingMethods()?.Count??0;var first=InvokeMethod(p,"ReadReceipt");Require(first.Found&&first.Value is null,"null return was marked absent");p.Receipt="fresh";var second=InvokeMethod(p,"ReadReceipt");Require(second.Found&&Equals(second.Value,"fresh")&&p.Calls==2,"later method receipt was hidden");Require((MissingMethods()?.Count??0)==before,"null result entered absence cache");
 });
 Check("method exceptions remain visible on every call and later recovery is read live",()=>{
  var p=new MethodProbe{Fail=true,Current=43};var before=MissingMethods()?.Count??0;for(var i=0;i<2;i++){try{InvokeMethod(p,"Throws");throw new Exception("method failure swallowed");}catch(TargetInvocationException ex){Require(ex.InnerException is InvalidOperationException inner&&inner.Message=="fixture-method-error","method exception shape changed");}}p.Fail=false;Require(Equals(InvokeMethod(p,"Throws").Value,43)&&p.Calls==3,"method exception poisoned later recovery");Require((MissingMethods()?.Count??0)==before,"exception entered absence cache");
 });
 Check("method miss keys retain arity and ordinal case while private methods remain callable",()=>{
  var p=new MethodProbe{Current=58};Require(!InvokeMethod(p,"OnlyZero",1).Found,"wrong arity gained a method");Require(Equals(InvokeMethod(p,"OnlyZero").Value,58),"wrong-arity miss hid real method");Require(!InvokeMethod(p,"read",1).Found&&Equals(InvokeMethod(p,"Read",1).Value,59),"case folding poisoned valid name");Require(Equals(InvokeMethod(p,"PrivateReceipt").Value,58)&&!InvokeMethod(p,"StaticOnly").Found,"instance selection flags changed");
 });
 Check("identical method type names in distinct assemblies cannot share a miss",()=>{
  var absent=MakeMethodType("FanMethodAbsentA",false);var present=MakeMethodType("FanMethodPresentB",true);Require(absent.GetType().FullName==present.GetType().FullName,"fixture names differ");Require(!InvokeMethod(absent,"Optional").Found&&Equals(InvokeMethod(present,"Optional").Value,91),"Type identity was reduced to a name");
 });
 Check("parallel absent method probes preserve absence and keep one metadata key",()=>{
  var before=MissingMethods()?.Count??0;var bad=new ConcurrentQueue<int>();Parallel.For(0,100,i=>{var result=InvokeMethod(new MethodProbe(),"ParallelMissingMethod");if(result.Found||result.Value is not null)bad.Enqueue(i);});Require(bad.IsEmpty,"parallel miss gained a method");Require(MissingMethods() is {} cache&&cache.Count==before+1,"parallel miss did not settle to one key");
 });
 Check("real lifecycle verify caller consumes current out receipts rather than cached outcomes",()=>{
  var p=new LifecycleMethodProbe();var method=backend.GetMethod("TryInvokeWaitReadLoopEnded",All)!;var args=new object?[]{p,null};Require(Equals(Call(method,null,args),false)&&Equals(args[1],"verify:pending-fixture"),"pending receipt changed");p.Ended=true;args[1]=null;Require(Equals(Call(method,null,args),true)&&Equals(args[1],"verify:read-loop-ended")&&p.Calls==2,"fresh terminal receipt was not observed");var missing=new object?[]{new MsiLikeNoLoops(),null};Require(Equals(Call(method,null,missing),false)&&Equals(missing[1],"verify:api-absent"),"missing verification was promoted to success");
 });
 Check("real settle caller still observes current pipeline state with zero wait",()=>{
  var p=new LifecycleMethodProbe();var method=backend.GetMethod("TryObserveOpenPipelineSettled",All)!;var args=new object?[]{p,null};Require(Equals(Call(method,null,args),false)&&args[1] is string pending&&pending.StartsWith("settle:pipeline-in-flight"),"in-flight pipeline was called settled");p.Settled=true;args[1]=null;Require(Equals(Call(method,null,args),true)&&args[1] is string terminal&&terminal.StartsWith("settle:receipt-terminal")&&p.Calls==2,"settled current receipt was hidden");
 });
 Check("no HC DLL initialization or physical operation is needed for metadata behavior",()=>Require(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),"HC assembly loaded during metadata-only checks"));
 var output=new{ok=failures.Count==0,checks=results.Count,mode,results,observations,failures,hostDllSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dll))).ToLowerInvariant(),hardwareCalls=0,cpuBenchmark=false,scope="actual Host GetMember/TryInvokeCached and lifecycle receipt callers with in-memory inert Types; optional scratch-only reflection-walk counter; no Host Main/HC assembly/device/physical sleep/CPU timing"};var json=JsonSerializer.Serialize(output);File.WriteAllText(resultFile,json);Console.WriteLine(json);return failures.Count==0?0:1;
 }
}