using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Diagnostics;
var hostPath=Path.GetFullPath(args[0]);var output=Path.GetFullPath(args[1]);Directory.CreateDirectory(output);
Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT",Path.Combine(output,"logs"));
var host=AssemblyLoadContext.Default.LoadFromAssemblyPath(hostPath);
AssemblyLoadContext.Default.Resolving+=(c,n)=>{var p=Path.Combine(Path.GetDirectoryName(hostPath)!,n.Name+".dll");return File.Exists(p)?c.LoadFromAssemblyPath(p):null;};
const BindingFlags F=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
Type T(string n)=>host.GetType("YeManFanHost."+n,true)!;
object? Call(object o,string n,params object?[] a){try{var m=o.GetType().GetMethods(F).Single(m=>m.Name==n&&!m.IsGenericMethod&&m.GetParameters().Length>=a.Length&&m.GetParameters().Skip(a.Length).All(p=>p.IsOptional));return m.Invoke(o,a.Concat(Enumerable.Repeat<object?>(Type.Missing,m.GetParameters().Length-a.Length)).ToArray());}catch(TargetInvocationException e)when(e.InnerException!=null){throw e.InnerException;}}
object? Field(object o,string n)=>o.GetType().GetField(n,F)!.GetValue(o);
void Set(object o,string n,object? v)=>o.GetType().GetField(n,F)!.SetValue(o,v);
void Prop(object o,string n,object? v)=>o.GetType().GetProperty(n,F)!.SetValue(o,v);
string P(object o,string n)=>o.GetType().GetProperty(n,F)!.GetValue(o)?.ToString()??"null";
object Options(params string[] a)=>T("HostOptions").GetMethod("Parse",F)!.Invoke(null,new object[]{a})!;
object Backend()=>Activator.CreateInstance(T("RealHcBackend"),F,null,new[]{Options()},null)!;
Exception? Fail(Action a){try{a();return null;}catch(Exception e){return e;}}
string Code(Exception? e)=>e?.GetType().GetProperty("Code",F)?.GetValue(e)?.ToString()??e?.GetType().Name??"none";
int Status(Exception? e)=>e?.GetType().GetProperty("StatusCode",F)?.GetValue(e) is int n?n:0;
T("Program").GetProperty("IsSelfTestProcess",F)!.SetValue(null,true);
var checks=new List<object>();void C(string name,bool ok,string detail=""){checks.Add(new{name,ok,detail});Console.WriteLine((ok?"PASS ":"FAIL ")+name+(detail.Length>0?" "+detail:""));}
var sta=T("RealHcBackend").GetMethods(F).Single(m=>m.Name=="OnStaBounded"&&!m.IsGenericMethod&&m.GetParameters()[0].ParameterType==typeof(Action));
void Run(object b,Action a,string n="fixture"){try{sta.Invoke(b,new object[]{a,n});}catch(TargetInvocationException e)when(e.InnerException!=null){throw e.InnerException;}}
using(var entered=new ManualResetEventSlim())using(var release=new ManualResetEventSlim()){
 var b=Backend();int owner=0,reader=0,overlap=0,max=0;var errors=new List<Exception>();
 var first=Task.Run(()=>Fail(()=>Run(b,()=>{owner=Environment.CurrentManagedThreadId;Interlocked.Increment(ref overlap);entered.Set();release.Wait();Interlocked.Decrement(ref overlap);},"profile-replay-fixture")));
 C("replay occupies real message-pumped STA",entered.Wait(2000));
 var second=Task.Run(()=>Fail(()=>Run(b,()=>{reader=Environment.CurrentManagedThreadId;max=Math.Max(max,Volatile.Read(ref overlap));},"heartbeat-metadata-fixture")));
 Thread.Sleep(100);C("heartbeat waits for admitted replay instead of reporting timeout",!second.IsCompleted);release.Set();Task.WaitAll(first,second);
 C("admitted replay returns naturally",first.Result==null,Code(first.Result));C("heartbeat accepted after replay",second.Result==null,Code(second.Result));C("single STA owner and no overlapping actions",owner!=0&&reader==owner&&max==0);
 C("completed call already retired before next entry",Field(b,"activeStaWorkItem")==null);
 C("nested STA entry does not deadlock",Fail(()=>Run(b,()=>Run(b,()=>{},"nested"),"outer"))==null);
 Call(b,"Dispose");
}
foreach(var family in new[]{"HandheldCompanion.Devices.MSI.ClawA1M","HandheldCompanion.Devices.MSI.ClawA2VM","HandheldCompanion.Devices.MSI.ClawCG3EM","HandheldCompanion.Devices.ROGAlly","HandheldCompanion.Devices.GPDWin5"}){
 var opt=Options("--real-backend");var e=Activator.CreateInstance(T("FanHostEngine"),F,null,new object?[]{opt,null},null)!;
 var b=Field(e,"realBackend")!;var s=Field(e,"state")!;var route=T("RealHcBackend").GetMethod("ResolveRouteForSelfTest",F)!.Invoke(null,new object[]{family})!;
 Call(b,"InstallSelfTestRouteSessionForSelfTest",new SessionFixture(),route,true);Prop(s,"State","Ready");Prop(s,"PowerState","On");Prop(s,"HardwareCapable",true);Prop(s,"HardwareWriteAuthorizationGranted",true);Prop(s,"OpenCalled",true);Prop(s,"OpenEventsCalled",true);
 var id="fixture-exact-lease";var lease=Activator.CreateInstance(T("FanLease"),id,1L,Environment.TickCount64+15000,"YeManCC")!;Prop(s,"Lease",lease);
 using var started=new ManualResetEventSlim();using var done=new ManualResetEventSlim();
 var op=Task.Run(()=>Fail(()=>Run(b,()=>{started.Set();done.Wait();},"family-replay-fixture")));C(family+" owner admitted",started.Wait(2000));
 var hb=Task.Run(()=>Fail(()=>Call(e,"Heartbeat",id,0L)));Thread.Sleep(80);done.Set();Task.WaitAll(op,hb);
 C(family+" actual engine heartbeat not faulted by admitted HC call",hb.Result==null,Code(hb.Result));
 C(family+" no false fault/restore/Close",P(s,"State")=="Ready"&&P(s,"UnknownState")=="False"&&P(s,"OpenCalled")=="True");
 C(family+" exact lease retained",P(Field(s,"<Lease>k__BackingField")!,"LeaseId")==id);
 Set(e,"realBackend",null);Prop(s,"HardwareWritesEnabled",false);Prop(s,"HardwareWritesObserved",false);Prop(s,"OpenCalled",false);Prop(s,"OpenEventsCalled",false);Call(e,"Dispose");Call(b,"Dispose");
}
{
 var b=Backend();Call(b,"MarkOperationTimedOutForSelfTest");int writes=0;var x=Fail(()=>Run(b,()=>writes++,"must-not-enter"));C("actual timed-out owner remains504 and forbids a second write",Code(x)=="HC_OPERATION_TIMEOUT"&&Status(x)==504&&writes==0);Call(b,"Dispose");
}
{
 var b=Backend();using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();
 var op=Task.Run(()=>Fail(()=>Run(b,()=>{entered.Set();release.Wait();},"real-timeout-fixture")));C("real timeout fixture entered",entered.Wait(2000));var timer=Stopwatch.StartNew();op.Wait();C("genuine15s call still classified timeout",Code(op.Result)=="HC_OPERATION_TIMEOUT"&&timer.ElapsedMilliseconds>=14000&&timer.ElapsedMilliseconds<17500);
 int secondWrites=0;var second=Fail(()=>Run(b,()=>secondWrites++,"forbidden-concurrent"));C("pending timed-out call cannot overlap cleanup",Code(second)=="HC_OPERATION_TIMEOUT"&&secondWrites==0);release.Set();
 var retired=Stopwatch.StartNew();while(Field(b,"activeStaWorkItem")!=null&&retired.ElapsedMilliseconds<2000)Thread.Sleep(5);
 C("actual return clears exact pending ownership",Field(b,"activeStaWorkItem")==null);
 C("next operation only after actual owner return",Fail(()=>Run(b,()=>secondWrites++,"after-return"))==null&&secondWrites==1);Call(b,"Dispose");
}
// Admission-budget exhaustion is an unsubmitted request, not evidence of a
// vendor failure. The busy slot below is an explicit ownership fixture: no HC.
{
 var opt=Options("--real-backend");var e=Activator.CreateInstance(T("FanHostEngine"),F,null,new object?[]{opt,null},null)!;
 var b=Field(e,"realBackend")!;var s=Field(e,"state")!;var route=T("RealHcBackend").GetMethod("ResolveRouteForSelfTest",F)!.Invoke(null,new object[]{"HandheldCompanion.Devices.MSI.ClawCG3EM"})!;
 Call(b,"InstallSelfTestRouteSessionForSelfTest",new SessionFixture(),route,true);Prop(s,"State","Ready");Prop(s,"PowerState","On");Prop(s,"HardwareCapable",true);Prop(s,"HardwareWriteAuthorizationGranted",true);Prop(s,"OpenCalled",true);Prop(s,"OpenEventsCalled",true);
 var id="busy-budget-fixture";var lease=Activator.CreateInstance(T("FanLease"),id,1L,Environment.TickCount64+30000,"YeManCC")!;Prop(s,"Lease",lease);
 Run(b,()=>{},"warm-before-busy-fixture");var busyOwner=new object();Set(b,"activeStaWorkItem",busyOwner);var beforeExpiry=P(lease,"ExpiresAtMonoMs");
 var started=Stopwatch.StartNew();var x=Fail(()=>Call(e,"Heartbeat",id,0L));
 C("unsubmitted busy heartbeat is409 distinct from hardware timeout",Code(x)=="HC_OPERATION_BUSY"&&Status(x)==409,Code(x));
 C("admission waiting remains bounded within15s",started.ElapsedMilliseconds>=14000&&started.ElapsedMilliseconds<17500);
 C("busy cannot retire current owner or mark it timed out",ReferenceEquals(Field(b,"activeStaWorkItem"),busyOwner)&&!(bool)Field(b,"operationTimedOut")!);
 C("busy heartbeat neither restores nor faults actual engine",P(s,"State")=="Ready"&&P(s,"UnknownState")=="False"&&P(s,"OpenCalled")=="True");
 C("busy rejection does not claim a renewed lease",P(Field(s,"<Lease>k__BackingField")!,"ExpiresAtMonoMs")==beforeExpiry);
 Set(b,"activeStaWorkItem",null);C("following heartbeat remains actionable",Fail(()=>Call(e,"Heartbeat",id,0L))==null);
 var duties=Array.CreateInstance(b.GetType().GetField("activeCurveDuties",F)!.FieldType.GetElementType()!,101);Set(b,"activeCurveDuties",duties);
 C("current MSI mode revision arms replay",Call(b,"NotifyMsiFirmwareModeChanged",1L) is true);
 C("ordinary duplicate pending revision stays read-only",Call(b,"NotifyMsiFirmwareModeChanged",1L) is false);
 if(b.GetType().GetField("msiFirmwareModeDeferredRevision",F)!=null)Set(b,"msiFirmwareModeDeferredRevision",1L);
 C("explicitly deferred same revision can rearm after unsubmitted busy",Call(b,"NotifyMsiFirmwareModeChanged",1L) is true);
 Set(b,"msiFirmwareModeSettledRevision",1L);C("settled revision cannot duplicate write",Call(b,"NotifyMsiFirmwareModeChanged",1L) is false);
 C("new revision retained",Call(b,"NotifyMsiFirmwareModeChanged",2L) is true);
 C("older revision cannot rearm latest one",Call(b,"NotifyMsiFirmwareModeChanged",1L) is false);
 Set(b,"activeCurveDuties",null);Set(e,"realBackend",null);Prop(s,"HardwareWritesEnabled",false);Prop(s,"HardwareWritesObserved",false);Prop(s,"OpenCalled",false);Prop(s,"OpenEventsCalled",false);Call(e,"Dispose");Call(b,"Dispose");
}
{
 var b=Backend();Run(b,()=>{},"dispose-warmup");Set(b,"activeStaWorkItem",new object());
 var waiting=Task.Run(()=>Fail(()=>Run(b,()=>{},"must-not-enter-after-dispose")));Thread.Sleep(60);var timer=Stopwatch.StartNew();Call(b,"Dispose");waiting.Wait();
 C("disposing dispatcher wakes waiting admission without a late action",waiting.Result is ObjectDisposedException&&timer.ElapsedMilliseconds<1500);Set(b,"activeStaWorkItem",null);
}
// Real admission-budget boundary: owner retirement wins just before the
// deadline, but acquiring its monitor completes after the deadline. This
// fixture must never start a stale/unsubmitted HC operation.
{
 var b=Backend();Run(b,()=>{},"deadline-warmup");var ownerGate=Field(b,"staGate")!;Set(b,"activeStaWorkItem",new object());
 int lateActions=0;using var entered=new ManualResetEventSlim();var elapsed=Stopwatch.StartNew();
 var waiting=Task.Run(()=>{entered.Set();return Fail(()=>Run(b,()=>Interlocked.Increment(ref lateActions),"expired-admission"));});entered.Wait();
 while(elapsed.ElapsedMilliseconds<14850)Thread.Sleep(20);
 lock(ownerGate){Set(b,"activeStaWorkItem",null);Monitor.PulseAll(ownerGate);Thread.Sleep(350);}
 waiting.Wait();
 C("expired admission never executes a late action",lateActions==0);
 C("expired admission returns unsubmitted busy",Code(waiting.Result)=="HC_OPERATION_BUSY"&&Status(waiting.Result)==409);
 C("expired waiter cannot fabricate a timed-out owner",!(bool)Field(b,"operationTimedOut")!);
 C("deadline response remains bounded",elapsed.ElapsedMilliseconds<17000);
 C("current request after expired admission remains usable",Fail(()=>Run(b,()=>{},"current-after-expired"))==null);Call(b,"Dispose");
}
var ok=checks.All(c=>(bool)c.GetType().GetProperty("ok")!.GetValue(c)!);File.WriteAllText(Path.Combine(output,"result.json"),JsonSerializer.Serialize(new{ok,checks=checks.Count,hostAssemblySha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(hostPath))).ToLowerInvariant(),scope="actual Host engine heartbeat + actual message-pumped STA; fake session/owner actions; no HC Open or WMI/EC/ACPI or physical evidence",results=checks},new JsonSerializerOptions{WriteIndented=true}));return ok?0:1;
class SessionFixture {public bool IsOpen {get;set;}=true;public bool IsSupported {get;set;}=true;public bool UseOpenLib {get;set;}=true;public void SetFanControl(bool on){}public void Close(){IsOpen=false;}}
