using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Diagnostics;
using System.Windows.Threading;
using System.Runtime.CompilerServices;
var publicPowerOnly=args.Length is 3 or 4&&args[2]=="--public-power-only";
if(args.Length is <2 or >4||(!publicPowerOnly&&args.Length>=3&&args[2]!="--scheduler-pressure")||(!publicPowerOnly&&args.Length==4&&args[3]!="--executing-timeout"&&args[3]!="--executing-timeout-only"))throw new ArgumentException("Expected HostAssembly OutputDirectory [--public-power-only [scenario] | --scheduler-pressure [--executing-timeout|--executing-timeout-only]]");
var schedulerPressure=!publicPowerOnly&&args.Length>=3;var executingTimeout=args.Length==4;var executingTimeoutOnly=executingTimeout&&args[3]=="--executing-timeout-only";
var hostPath=Path.GetFullPath(args[0]);var output=Path.GetFullPath(args[1]);Directory.CreateDirectory(output);
// Reserve the original result before doing any work; a repeated path cannot run again.
using var resultFile=new FileStream(Path.Combine(output,"result.json"),FileMode.CreateNew,FileAccess.Write,FileShare.Read);
var totalElapsed=Stopwatch.StartNew();
string SourcePath([CallerFilePath]string path="")=>path;
string Hash(string path)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
var sourcePath=SourcePath();var sourceSha=Hash(sourcePath);var hostSha=Hash(hostPath);
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
var scenarios=new List<object>();
bool Passed(object check)=>(bool)check.GetType().GetProperty("ok")!.GetValue(check)!;
int Finish(){
 var identityUnchanged=Hash(hostPath)==hostSha&&Hash(sourcePath)==sourceSha;
 var ok=checks.All(Passed)&&identityUnchanged;
 var result=new{ok,selection=publicPowerOnly?"public-power-only":schedulerPressure?(executingTimeoutOnly?"scheduler-pressure+executing-timeout-only":executingTimeout?"scheduler-pressure+executing-timeout":"scheduler-pressure"):"default",checks=checks.Count,actualPassed=checks.Count(Passed),actualFailed=checks.Count(c=>!Passed(c)),elapsedMs=totalElapsed.Elapsed.TotalMilliseconds,hostAssemblyPath=hostPath,hostAssemblySha256=hostSha,fixtureSourcePath=sourcePath,fixtureSourceSha256=sourceSha,identityUnchanged,scope=publicPowerOnly && args.Length == 4 && args[3].StartsWith("prepare-", StringComparison.Ordinal)?"actual Host MSI/OneX preparation predicates and actual pinned source Open session; public Native and kernel transport are explicit ports; no complete MSI/OneX lifecycle, HTTP or physical acceptance":publicPowerOnly && args.Length == 4 && args[3].StartsWith("host-asus-", StringComparison.Ordinal)?"actual Host OnStaPowerBounded and ApplyAsusCurveViaBackend with actual ASUS library; Native importer, profile object and kernel transport are explicit ports; no HTTP/native pipe/physical acceptance":publicPowerOnly && args.Length == 4 && args[3].StartsWith("queue-", StringComparison.Ordinal)?"actual Host Channel ingress and reader scope ownership; Native importer/facts/process are explicit ports; no STA, production pipe or physical acceptance":publicPowerOnly && args.Length == 4 && args[3].StartsWith("asus-", StringComparison.Ordinal)?"actual ASUS catalog session, controller lock, curve writer and receipts; Native authority callback and kernel transport are explicit ports; no STA, production pipe or physical acceptance":publicPowerOnly?"actual Host public scope context and real STA queue; Native importer/facts/process/SDK action are ports; no production transport, HC device or physical acceptance":schedulerPressure?"actual OnStaBounded and WPF dispatcher queue; inert barriers/actions only; no HC Open, EC, WMI, device or scheduler changes; default51 NOT_RUN":"actual Host engine heartbeat + actual message-pumped STA; fake session/owner actions; no HC Open or WMI/EC/ACPI or physical evidence",scenarios,results=checks};
 JsonSerializer.Serialize(resultFile,result,new JsonSerializerOptions{WriteIndented=true});resultFile.Flush(true);
 Console.WriteLine(JsonSerializer.Serialize(new{result.ok,result.selection,result.checks,result.actualPassed,result.actualFailed,result.elapsedMs,result.hostAssemblySha256,result.fixtureSourceSha256,scenarios=scenarios.Select(s=>s.GetType().GetProperty("name")!.GetValue(s))}));return ok?0:1;
}
var sta=T("RealHcBackend").GetMethods(F).Single(m=>m.Name=="OnStaBounded"&&!m.IsGenericMethod&&m.GetParameters()[0].ParameterType==typeof(Action));
void Run(object b,Action a,string n="fixture"){try{sta.Invoke(b,new object[]{a,n});}catch(TargetInvocationException e)when(e.InnerException!=null){throw e.InnerException;}}
if(publicPowerOnly){
 FanPublicPowerStaCases.Run(host,Backend,Run,C,scenarios.Add,args.Length==4?args[3]:null);
 return Finish();
}
if(schedulerPressure){
 // This mode is exclusive: neither the old ownership substitutes nor the
 // default51 (including its other 15s cases) execute on this path.
 if(!executingTimeoutOnly){
  RunQueuedDeadline();
  RunSequentialJitter();
 }
 if(executingTimeout)RunExecutingTimeout();
 C("scheduler pressure loads no real hardware assembly",!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name is "HandheldCompanion" or "LibreHardwareMonitorLib" or "System.Management"));
 return Finish();
}
void RunQueuedDeadline(){
 var watch=Stopwatch.StartNew();var firstCheck=checks.Count;object? b=null;Dispatcher? dispatcher=null;Thread? ownerThread=null;Thread? caller=null;
 using var barrierEntered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();using var queued=new ManualResetEventSlim();using var drained=new ManualResetEventSlim();
 DispatcherHookEventHandler? posted=null;DispatcherOperation? queuedOperation=null;
 Exception? response=null;string? failure=null;string? disposeFailure=null;
 int warmActions=0,warmThread=0,barrierActions=0,barrierThread=0,postedCount=0,expiredActions=0,expiredAtDeadline=-1,freshActions=0,timeoutDiagnostics=0;
 bool genuineSta=false,barrierWithoutOwner=false,pendingObserved=false,callerReturned=false,queueRetired=false,drainCompleted=false,freshSucceeded=false,callerJoined=true,staJoined=true,disposed=false;
 bool timeoutBefore=false,timeoutAtDeadline=true,timeoutAfter=true;int blockedBefore=0,blockedAtDeadline=-1,blockedAfter=-1;long epochBefore=0,epochAtDeadline=-1,epochAfter=-1;
 double callMs=0,releaseAtMs=0;string? queuedStatusAtDeadline=null,queuedStatusAfterDrain=null;object? responseDetails=null;
 try{
  b=Backend();var backend=b;
  Prop(backend,"Diagnostic",new Action<string,object?>((name,_)=>{if(name=="hc-operation.timeout")Interlocked.Increment(ref timeoutDiagnostics);}));
  Run(backend,()=>{warmActions++;warmThread=Environment.CurrentManagedThreadId;genuineSta=Thread.CurrentThread.GetApartmentState()==ApartmentState.STA&&Dispatcher.CurrentDispatcher.CheckAccess();},"pressure-queue-warmup");
  dispatcher=(Dispatcher)Field(backend,"staDispatcher")!;ownerThread=(Thread)Field(backend,"staThread")!;
  timeoutBefore=(bool)Field(backend,"operationTimedOut")!;blockedBefore=(int)Field(backend,"suspendWriteBlocked")!;epochBefore=(long)Field(backend,"suspendGateEpoch")!;
  // Direct WPF work holds the message pump. It never enters OnStaBounded and
  // never writes activeStaWorkItem, so this is genuine queued, unstarted work.
  _=dispatcher.BeginInvoke(DispatcherPriority.Send,new Action(()=>{Interlocked.Increment(ref barrierActions);barrierThread=Environment.CurrentManagedThreadId;barrierEntered.Set();release.Wait();}));
  if(!barrierEntered.Wait(2000))throw new TimeoutException("Direct dispatcher barrier did not enter");
  barrierWithoutOwner=Field(backend,"activeStaWorkItem")==null;
  posted=(_,eventArgs)=>{if(Thread.CurrentThread==caller){queuedOperation=eventArgs.Operation;Interlocked.Increment(ref postedCount);queued.Set();}};
  dispatcher.Hooks.OperationPosted+=posted;
  caller=new Thread(()=>{var timer=Stopwatch.StartNew();try{response=Fail(()=>Run(backend,()=>Interlocked.Increment(ref expiredActions),"pressure-queued-expired-action"));}finally{callMs=timer.Elapsed.TotalMilliseconds;}}){IsBackground=true,Name="FanStaAdmissionSelfTest.queued-caller"};
  caller.Start();
  if(!queued.Wait(2000))throw new TimeoutException("Original OnStaBounded did not post a dispatcher operation");
  pendingObserved=queuedOperation?.Status==DispatcherOperationStatus.Pending&&Field(backend,"activeStaWorkItem")!=null&&expiredActions==0;
  callerReturned=caller.Join(17500);
  if(!callerReturned)throw new TimeoutException("Original queued call did not return within its existing deadline allowance");
  expiredAtDeadline=Volatile.Read(ref expiredActions);queuedStatusAtDeadline=queuedOperation?.Status.ToString();
  responseDetails=response?.GetType().GetProperty("Details",F)?.GetValue(response);
  timeoutAtDeadline=(bool)Field(backend,"operationTimedOut")!;blockedAtDeadline=(int)Field(backend,"suspendWriteBlocked")!;epochAtDeadline=(long)Field(backend,"suspendGateEpoch")!;
  queueRetired=Field(backend,"activeStaWorkItem")==null;
  releaseAtMs=watch.Elapsed.TotalMilliseconds;release.Set();
  // Background priority proves the original Send-priority callback has either
  // run (old bug) or been cancelled/rejected; a short arbitrary sleep cannot.
  _=dispatcher.BeginInvoke(DispatcherPriority.Background,new Action(drained.Set));
  drainCompleted=drained.Wait(2000);if(!drainCompleted)throw new TimeoutException("Dispatcher did not drain after releasing the barrier");
  queuedStatusAfterDrain=queuedOperation?.Status.ToString();
  var freshError=Fail(()=>Run(backend,()=>Interlocked.Increment(ref freshActions),"pressure-fresh-after-queue-expiry"));
  freshSucceeded=freshError==null&&freshActions==1&&Field(backend,"activeStaWorkItem")==null;
  timeoutAfter=(bool)Field(backend,"operationTimedOut")!;blockedAfter=(int)Field(backend,"suspendWriteBlocked")!;epochAfter=(long)Field(backend,"suspendGateEpoch")!;
 }catch(Exception ex){failure=ex.ToString();}
 finally{
  release.Set();
  if(caller is {IsAlive:true})callerJoined=caller.Join(2000);
  if(dispatcher!=null&&posted!=null)dispatcher.Hooks.OperationPosted-=posted;
  if(b!=null){try{Call(b,"Dispose");disposed=true;}catch(Exception ex){disposeFailure=ex.ToString();}}
  if(ownerThread is {IsAlive:true})staJoined=ownerThread.Join(2000);
 }
 bool FalseDetail(string name)=>responseDetails?.GetType().GetProperty(name,F)?.GetValue(responseDetails) is false;
 C("scheduler pressure warmup uses real message-pumped STA",failure==null&&genuineSta&&warmActions==1&&warmThread!=Environment.CurrentManagedThreadId);
 C("scheduler pressure barrier owns pump without fabricated backend work",barrierWithoutOwner&&barrierActions==1&&barrierThread==warmThread);
 C("scheduler pressure original request is pending in WPF queue",pendingObserved&&postedCount==1);
 C("scheduler pressure queued deadline returns unsubmitted busy",callerReturned&&Code(response)=="HC_OPERATION_BUSY"&&Status(response)==409&&FalseDetail("submitted")&&FalseDetail("hardwareFailureClaimed"),$"status={Status(response)} code={Code(response)} details={JsonSerializer.Serialize(responseDetails)}");
 C("scheduler pressure queued deadline keeps original15s budget",callerReturned&&callMs>=14000&&callMs<17500,$"elapsedMs={callMs:F3}");
 C("scheduler pressure queued expiry preserves timeout/write/epoch state",!timeoutBefore&&!timeoutAtDeadline&&!timeoutAfter&&blockedBefore==0&&blockedAtDeadline==blockedBefore&&blockedAfter==blockedBefore&&epochAtDeadline==epochBefore&&epochAfter==epochBefore&&timeoutDiagnostics==0,$"timeout={timeoutAtDeadline}/{timeoutAfter} blocked={blockedAtDeadline}/{blockedAfter} epoch={epochBefore}/{epochAtDeadline}/{epochAfter} timeoutDiagnostics={timeoutDiagnostics}");
 C("scheduler pressure expired queue ownership is retired",queueRetired);
 C("scheduler pressure released barrier never executes expired action",drainCompleted&&expiredAtDeadline==0&&expiredActions==0,$"atDeadline={expiredAtDeadline} afterDrain={expiredActions} queue={queuedStatusAtDeadline}/{queuedStatusAfterDrain}");
 C("scheduler pressure fresh action succeeds after queue expiry",freshSucceeded);
 C("scheduler pressure queued scenario releases all owned resources",failure==null&&disposeFailure==null&&disposed&&callerJoined&&staJoined&&caller?.IsAlive!=true&&ownerThread?.IsAlive!=true,failure??disposeFailure??"");
 scenarios.Add(new{name="queued-dispatcher-deadline",ok=checks.Skip(firstCheck).All(Passed),elapsedMs=watch.Elapsed.TotalMilliseconds,callMs,totalDeadlineMs=15000,callReturnLimitMs=17500,releaseAtMs,warmActions,barrierActions,postedCount,expiredAtDeadline,expiredActions,freshActions,warmThread,barrierThread,pendingObserved,queuedStatusAtDeadline,queuedStatusAfterDrain,response=new{status=Status(response),code=Code(response),details=responseDetails},before=new{operationTimedOut=timeoutBefore,suspendWriteBlocked=blockedBefore,suspendGateEpoch=epochBefore},atDeadline=new{operationTimedOut=timeoutAtDeadline,suspendWriteBlocked=blockedAtDeadline,suspendGateEpoch=epochAtDeadline},afterFresh=new{operationTimedOut=timeoutAfter,suspendWriteBlocked=blockedAfter,suspendGateEpoch=epochAfter},timeoutDiagnostics,callerJoined,staJoined,disposed,failure,disposeFailure});
}
void RunSequentialJitter(){
 var watch=Stopwatch.StartNew();var firstCheck=checks.Count;object? b=null;Thread? ownerThread=null;string? failure=null;string? disposeFailure=null;
 const int count=100;var executionCounts=new int[count];var order=new List<int>();var durationsMs=new List<double>();int attempts=0,jitterActions=0,actionThread=0,freshActions=0;bool sameSta=true,ownersRetired=true,gatesPreserved=true,freshSucceeded=false,disposed=false,staJoined=true;
 long epochBefore=0;int blockedBefore=0;bool timeoutBefore=false;
 try{
  b=Backend();var backend=b;
  Run(backend,()=>actionThread=Environment.CurrentManagedThreadId,"pressure-sequential-warmup");
  var dispatcher=(Dispatcher)Field(backend,"staDispatcher")!;ownerThread=(Thread)Field(backend,"staThread")!;
  epochBefore=(long)Field(backend,"suspendGateEpoch")!;blockedBefore=(int)Field(backend,"suspendWriteBlocked")!;timeoutBefore=(bool)Field(backend,"operationTimedOut")!;
  for(int i=0;i<count;i++){
   int current=i;var timer=Stopwatch.StartNew();attempts++;
   _=dispatcher.BeginInvoke(DispatcherPriority.Send,new Action(()=>{Interlocked.Increment(ref jitterActions);Thread.Sleep(current%4);}));
   Run(backend,()=>{executionCounts[current]++;order.Add(current);sameSta&=Environment.CurrentManagedThreadId==actionThread&&Thread.CurrentThread.GetApartmentState()==ApartmentState.STA;},"pressure-sequential-"+current);
   durationsMs.Add(timer.Elapsed.TotalMilliseconds);
   ownersRetired&=Field(backend,"activeStaWorkItem")==null;
   gatesPreserved&=(bool)Field(backend,"operationTimedOut")! == timeoutBefore&&(int)Field(backend,"suspendWriteBlocked")! == blockedBefore&&(long)Field(backend,"suspendGateEpoch")! == epochBefore;
  }
  freshSucceeded=Fail(()=>Run(backend,()=>freshActions++,"pressure-sequential-fresh"))==null&&freshActions==1&&Field(backend,"activeStaWorkItem")==null;
 }catch(Exception ex){failure=ex.ToString();}
 finally{
  if(b!=null){try{Call(b,"Dispose");disposed=true;}catch(Exception ex){disposeFailure=ex.ToString();}}
  if(ownerThread is {IsAlive:true})staJoined=ownerThread.Join(2000);
 }
 C("scheduler pressure sequential 100 executes each action once",failure==null&&attempts==count&&jitterActions==count&&executionCounts.All(n=>n==1),$"attempts={attempts} jitter={jitterActions} executed={executionCounts.Sum()}");
 C("scheduler pressure sequential 100 preserves order and STA identity",sameSta&&actionThread!=0&&order.SequenceEqual(Enumerable.Range(0,count)));
 C("scheduler pressure sequential 100 preserves gates and retires owners",gatesPreserved&&ownersRetired&&!timeoutBefore&&blockedBefore==0);
 C("scheduler pressure sequential 100 accepts following fresh action",freshSucceeded);
 C("scheduler pressure sequential scenario releases all owned resources",failure==null&&disposeFailure==null&&disposed&&staJoined&&ownerThread?.IsAlive!=true,failure??disposeFailure??"");
 scenarios.Add(new{name="sequential-jitter-100",ok=checks.Skip(firstCheck).All(Passed),elapsedMs=watch.Elapsed.TotalMilliseconds,requestedActions=count,attempts,jitterActions,executedActions=executionCounts.Sum(),freshActions,actionThread,executionCounts,order,durationsMs,jitterDelayMs="index % 4",perCallDeadlineMs=15000,ownersRetired,gatesPreserved,staJoined,disposed,failure,disposeFailure});
}
// The original default51 genuine executing-timeout case is shared here, not
// copied into another tester. Explicit opt-in adds it to the After pressure arm.
void RunExecutingTimeout(){
 var watch=Stopwatch.StartNew();var firstCheck=checks.Count;object? b=null;Thread? caller=null;Thread? ownerThread=null;
 using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();using var drained=new ManualResetEventSlim();
 Exception? response=null;Exception? secondResponse=null;string? failure=null;string? disposeFailure=null;
 int executingActions=0,secondWrites=0,forbiddenWrites=-1;double callMs=0;long epochBefore=0,epochAtTimeout=0;int blockedAtTimeout=0;bool timedOutFlag=false,admitted=false,returned=false,retired=false,freshSucceeded=false,disposed=false,callerJoined=true,staJoined=true,exactOwnerRetained=false;
 try{
  b=Backend();var backend=b;Run(backend,()=>{},"executing-timeout-warmup");ownerThread=(Thread)Field(backend,"staThread")!;
  epochBefore=(long)Field(backend,"suspendGateEpoch")!;
  caller=new Thread(()=>{var timer=Stopwatch.StartNew();try{response=Fail(()=>Run(backend,()=>{Interlocked.Increment(ref executingActions);entered.Set();release.Wait();},"real-timeout-fixture"));}finally{callMs=timer.Elapsed.TotalMilliseconds;}}){IsBackground=true,Name="FanStaAdmissionSelfTest.executing-caller"};
  caller.Start();admitted=entered.Wait(2000);
  if(!admitted)throw new TimeoutException("Original executing-timeout fixture did not enter");
  var exactOwner=Field(backend,"activeStaWorkItem");
  returned=caller.Join(17500);if(!returned)throw new TimeoutException("Original executing-timeout caller did not return");
  timedOutFlag=(bool)Field(backend,"operationTimedOut")!;blockedAtTimeout=(int)Field(backend,"suspendWriteBlocked")!;epochAtTimeout=(long)Field(backend,"suspendGateEpoch")!;
  secondResponse=Fail(()=>Run(backend,()=>secondWrites++,"forbidden-concurrent"));
  forbiddenWrites=secondWrites;
  exactOwnerRetained=exactOwner!=null&&ReferenceEquals(exactOwner,Field(backend,"activeStaWorkItem"));
  release.Set();
  var dispatcher=(Dispatcher)Field(backend,"staDispatcher")!;
  _=dispatcher.BeginInvoke(DispatcherPriority.Background,new Action(drained.Set));
  retired=drained.Wait(2000)&&Field(backend,"activeStaWorkItem")==null&&!(bool)Field(backend,"operationTimedOut")!;
  freshSucceeded=Fail(()=>Run(backend,()=>secondWrites++,"after-return"))==null&&secondWrites==1;
 }catch(Exception ex){failure=ex.ToString();}
 finally{
  release.Set();if(caller is {IsAlive:true})callerJoined=caller.Join(2000);
  if(b!=null){try{Call(b,"Dispose");disposed=true;}catch(Exception ex){disposeFailure=ex.ToString();}}
  if(ownerThread is {IsAlive:true})staJoined=ownerThread.Join(2000);
 }
 C("real timeout fixture entered",admitted&&executingActions==1);
 C("genuine15s call still classified timeout",returned&&Code(response)=="HC_OPERATION_TIMEOUT"&&Status(response)==504&&callMs>=14000&&callMs<17500&&timedOutFlag&&blockedAtTimeout==1&&epochAtTimeout==epochBefore+1,$"status={Status(response)} code={Code(response)} elapsedMs={callMs:F3} blocked={blockedAtTimeout} epoch={epochBefore}/{epochAtTimeout}");
 C("pending timed-out call cannot overlap cleanup",Code(secondResponse)=="HC_OPERATION_TIMEOUT"&&Status(secondResponse)==504&&exactOwnerRetained&&forbiddenWrites==0);
 C("actual return clears exact pending ownership",retired);
 C("next operation only after actual owner return",freshSucceeded&&failure==null&&disposeFailure==null&&disposed&&callerJoined&&staJoined&&caller?.IsAlive!=true&&ownerThread?.IsAlive!=true,failure??disposeFailure??"");
 scenarios.Add(new{name="executing-timeout-isolation",ok=checks.Skip(firstCheck).All(Passed),elapsedMs=watch.Elapsed.TotalMilliseconds,callMs,totalDeadlineMs=15000,executingActions,forbiddenWrites,secondWrites,status=Status(response),code=Code(response),secondStatus=Status(secondResponse),secondCode=Code(secondResponse),timedOutFlag,blockedAtTimeout,epochBefore,epochAtTimeout,exactOwnerRetained,retired,freshSucceeded,callerJoined,staJoined,disposed,failure,disposeFailure});
}
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
RunExecutingTimeout();
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
return Finish();
class SessionFixture {public bool IsOpen {get;set;}=true;public bool IsSupported {get;set;}=true;public bool UseOpenLib {get;set;}=true;public void SetFanControl(bool on){}public void Close(){IsOpen=false;}}
