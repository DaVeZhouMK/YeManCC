using System.Diagnostics;
using System.IO.Pipes;
using System.Reflection;
using System.Text;
using System.Text.Json;
using HIDMaestro;
using Guard=YeManInputHost.Diagnostics.HostControllerOutputStop;
using IdleGuard=YeManInputHost.Diagnostics.HostControllerIdleCtsRelease;
internal static partial class Program
{
    internal static void Assert(bool ok,string why="assertion failed"){if(!ok)throw new Exception(why);}
    private sealed partial class HostSession
    {
        internal HMController? Controller=>_controller;
        internal HMContext? Context=>_context;
        internal bool Pending=>LocalReleasePending;
        internal bool ReportPending=>_gameOutputObservation.HasPending;
        internal string Failure=>_sdkDisposeFailure;
        internal object? CapturedReceipt=>_idleCtsRelease;
        public bool CostEnabledForRead=>false;
        public void RecordCostPipeRead(long us){}
    }
    private static readonly List<string> Fatal=new();
    private static void LogFatalExit(string kind,string detail){lock(Fatal)Fatal.Add(kind+":"+detail);}
    private static void ResetManaged(){Fixture.Reset();OwnedWorkers.Reset();Fatal.Clear();_screenPadOwnsReport=false;_screenPadPacketCounter=0;}
    private static HostSession ManagedReady()
    {
        var s=Prepared();Fixture.OnSweep=()=>Fixture.Current=Empty();return s;
    }
    private static void Good(JsonElement r)=>Assert(r.GetProperty("ok").GetBoolean(),r.ToString());
    private static void Bad(JsonElement r,string status)=>Assert(!r.GetProperty("ok").GetBoolean()&&r.GetProperty("status").GetString()==status,r.ToString());
    private static void AllClosed()=>Assert(OwnedWorkers.All.All(w=>w.Closed),"workers/owned handles not closed before fixture cleanup");
    private static void NoDestruction()=>Assert(!Fixture.Events.Contains("controller-dispose")&&!Fixture.Events.Contains("context-dispose")&&!Fixture.Events.Contains("existing-sweep"),"release crossed unfinished worker barrier");
    private static void Deliver(Action<HMController,HMOutputPacket>? callback,HMController sender,uint n=8)=>callback?.Invoke(sender,new HMOutputPacket(HMOutputSource.Output,1,new byte[]{1,2,3,4},n));
    private static void AssertNoReport(HostSession s,HMController old,Action<HMController,HMOutputPacket>? callback)
    {Deliver(callback,old);Assert(!s.ReportPending,"revoked delegate revived pending output");}
    private static JsonElement ManagedFrame(HostSession s,ulong sequence)
    {
        var m=Message("SUBMIT_FRAME",sequence);m["buttons"]=0;m["lx"]=0;m["ly"]=0;m["rx"]=0;m["ry"]=0;m["lt"]=0;m["rt"]=0;
        m["sampleSequence"]=sequence;m["monotonicTick"]=sequence;m["motionPairProofId"]="offline";m["normalizedInputHash"]=CanonicalFrameHash(0,0,0,0,0,0,0);
        return Send(s,m,out _);
    }
    private static Dictionary<string,object?> Case(string name,Action body)
    {
        ResetManaged();bool passed=true;string? error=null;
        try{body();Assert(World.UnsafeFakeUnmaps==0,"context teardown raced owned live worker");Assert(World.UnsafeNeutralWrites==0,"neutral raced idle clock");}catch(Exception ex){passed=false;error=ex.ToString();}
        var resources=ResourceSnapshot();var trace=Fixture.Events.ToArray();
        try{OwnedWorkers.Cleanup();}catch(Exception ex){passed=false;error+=" cleanup: "+ex;}
        return new(){["name"]=name,["pass"]=passed,["error"]=error,["beforeFixtureCleanup"]=resources,["trace"]=trace};
    }
    private static object ResourceSnapshot()=>new{workers=OwnedWorkers.All.Select(w=>new{outputAlive=w.Output.IsAlive,clockAlive=w.Clock.Thread.IsAlive,outputCtsClosed=w.CtsHandle.IsClosed,clockEventClosed=w.ClockHandle.IsClosed,outputFinallyRan=w.Controller.OutputClosed,sdkDisposedFlag=w.Controller.Disposed,callbacks=w.Controller.SubscriberCount}).ToArray(),World.ContextCalls,World.UnsafeFakeUnmaps,World.UnsafeNeutralWrites};
    private static Process StartOwnedParent()
    {
        var info=new ProcessStartInfo(Environment.ProcessPath!){UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true};
        info.ArgumentList.Add(typeof(Program).Assembly.Location);info.ArgumentList.Add("--owned-parent");
        var p=Process.Start(info)!;Assert(p.StandardOutput.ReadLine()=="READY","owned parent startup");return p;
    }
    private static async Task ExitOwnedParent(Process p){if(p.HasExited)return;p.StandardInput.Close();await p.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(3));}
    private static NamedPipeServerStream ManagedServer(string name)=>new(name,PipeDirection.InOut,1,PipeTransmissionMode.Message,PipeOptions.Asynchronous,1024,1024);
    private static async Task<Dictionary<string,object?>> PipeCase(string mode,bool stuck=false)
    {
        ResetManaged();OwnedWorkers.BlockOutput=stuck&&mode=="read-output-stalled";OwnedWorkers.BlockClock=stuck&&mode=="read-clock-stalled";
        var s=ManagedReady();var original=s.Controller!;var callback=original.CapturedOutput;
        using var parent=StartOwnedParent();var name="ymcc-host-managed23-"+Guid.NewGuid().ToString("N");
        await using var server=ManagedServer(name);Task<int>? run=null;bool passed=true;string? error=null;int? exit=null;bool? finalRelease=null;string detail="";
        try
        {
            if(mode=="already-dead")await ExitOwnedParent(parent);
            var connected=mode=="connect-loss"?Task.CompletedTask:server.WaitForConnectionAsync();run=RunPipeClientAsync(mode=="connect-loss"?name+"-absent":name,parent.Id,s);
            if(mode!="already-dead"&&mode!="connect-loss")await connected.WaitAsync(TimeSpan.FromSeconds(3));
            if(mode=="shutdown")
            {
                await server.WriteAsync(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(Message("SHUTDOWN",3))));
                var data=new byte[16384];int n=await server.ReadAsync(data).AsTask().WaitAsync(TimeSpan.FromSeconds(3));
                using var reply=JsonDocument.Parse(data.AsMemory(0,n));Good(reply.RootElement);
            }
            else if(mode=="pipe-break")server.Dispose();
            else await ExitOwnedParent(parent);
            exit=await run.WaitAsync(TimeSpan.FromSeconds(5));
            // This is the exact single Main finally release call, extracted by the runner.
            finalRelease=ExecuteMainFinallyRelease(s,out detail);
            AssertNoReport(s,original,callback);
            if(!stuck)
            {
                Assert(!s.HasController&&!s.HasContext,"pipe terminal retained SDK references");AllClosed();
                Assert(exit==(mode=="pipe-break"?5:0),"pipe exit code");
                Assert(Fatal.Any(v=>v.StartsWith(mode=="shutdown"?"shutdown-ordered:":mode=="pipe-break"?"read-failed:":"parent-exited:")),"terminal reason");
            }
            else
            {
                // Preserve the current failure against the bounded resource-release contract.
                Assert(exit==0&&finalRelease==false&&s.HasController&&s.HasContext&&s.Pending,"counterexample no longer reproduced");
                Assert(OwnedWorkers.All.Any(w=>w.Output.IsAlive||w.Clock.Thread.IsAlive),"counterexample worker already ended");
                NoDestruction();
            }
        }
        catch(Exception ex){passed=false;error=ex.ToString();}
        var resources=ResourceSnapshot();var fatal=Fatal.ToArray();var trace=Fixture.Events.ToArray();
        try{server.Dispose();if(!parent.HasExited)await ExitOwnedParent(parent);if(run is not null)await run.WaitAsync(TimeSpan.FromSeconds(5));OwnedWorkers.Cleanup();}
        catch(Exception ex){passed=false;error+=" cleanup: "+ex;}
        return new(){["name"]="pipe-"+mode,["pass"]=passed,["error"]=error,["loopExit"]=exit,["mainFinallyReleaseOk"]=finalRelease,["mainFinallyDetail"]=detail,["resourceContractPass"]=!stuck&&passed,["knownFailureReproduced"]=stuck&&passed,["beforeFixtureCleanup"]=resources,["fatal"]=fatal,["trace"]=trace};
    }
    private static async Task<int> Main(string[] args)
    {
        if(args.SequenceEqual(new[]{"--owned-parent"})){Console.WriteLine("READY");Console.In.ReadLine();return 0;}
        var rows=new List<Dictionary<string,object?>>();
        rows.Add(Case("production metadata refuses fake SDK without cancellation",()=>{
            var s=ManagedReady();var c=s.Controller!;
            Assert(!Guard.TryStop(c,out _)&&!IdleGuard.TryStopInputClock(c,out _));Assert(!OwnedWorkers.All[0].Cts.IsCancellationRequested);
            Good(Send(s,"RELEASE_TARGET",3));AllClosed();
        }));
        rows.Add(Case("QUIESCE revokes captured callback; late frames rejected; ordered release",()=>{
            var s=ManagedReady();var c=s.Controller!;var cb=c.CapturedOutput;Deliver(cb,c);Assert(s.ReportPending,"positive report missing");
            Good(Send(s,"QUIESCE",3));AssertNoReport(s,c,cb);Bad(ManagedFrame(s,4),"state-rejected");
            Fixture.Events.Clear();Good(Send(s,"RELEASE_TARGET",5));AllClosed();Assert(c.SubscriberCount==0);
            var events=Fixture.Events;Assert(events.Contains("idle-stop")&&events.IndexOf("idle-stop")<events.IndexOf("neutral"));Assert(events.IndexOf("output-stop")<events.IndexOf("controller-dispose"));Assert(events.IndexOf("controller-dispose")<events.IndexOf("report-chain-close"));Assert(events.IndexOf("report-chain-close")<events.IndexOf("context-dispose"));
            Assert(s.ReleaseLocal(out _));Good(Send(s,Message("SHUTDOWN",6),out var shutdown));Assert(shutdown);AssertNoReport(s,c,cb);
        }));
        rows.Add(Case("RECREATE revokes old binding but accepts new sender",()=>{
            var s=ManagedReady();var old=s.Controller!;var cb=old.CapturedOutput;Good(Send(s,"RECREATE_TARGET",3));Assert(!ReferenceEquals(s.Controller,old));
            AssertNoReport(s,old,cb);Deliver(s.Controller!.CapturedOutput,old);Assert(!s.ReportPending,"new delegate accepted old sender");Deliver(s.Controller.CapturedOutput,s.Controller);Assert(s.ReportPending,"new output binding not accepted");
            var changed=Message("RECREATE_TARGET",4);changed["powerGeneration"]=4;Bad(Send(s,changed,out _),"protocol-error");Assert(Fixture.Creates==2);
            Good(Send(s,"SHUTDOWN",4));AllClosed();
        }));
        foreach(var worker in new[]{"output","clock"})rows.Add(Case(worker+" barrier refuses release and same-owner retry succeeds",()=>{
            OwnedWorkers.BlockOutput=worker=="output";OwnedWorkers.BlockClock=worker=="clock";var s=ManagedReady();var c=s.Controller!;var context=s.Context;var cb=c.CapturedOutput;
            Fixture.Events.Clear();var timer=Stopwatch.StartNew();Bad(Send(s,Message("SHUTDOWN",3),out var shutdown),"shutdown-local-dispose-failed");
            Assert(!shutdown&&s.Pending&&ReferenceEquals(c,s.Controller)&&ReferenceEquals(context,s.Context),"owner lost across failed barrier");Assert(timer.ElapsedMilliseconds<2500,"barrier not bounded");NoDestruction();AssertNoReport(s,c,cb);
            Bad(ManagedFrame(s,4),"state-rejected");OwnedWorkers.All[0].Gate.Set();OwnedWorkers.All[0].Clock.ExternalGate.Set();
            Good(Send(s,Message("SHUTDOWN",5),out shutdown));Assert(shutdown&&!s.HasController&&!s.HasContext);AllClosed();
        }));
        foreach(var failure in new[]{"controller","context"})rows.Add(Case(failure+" Dispose failure survives repeated SHUTDOWN and RECREATE",()=>{
            var s=ManagedReady();var c=s.Controller!;World.ThrowContextDispose=failure=="controller";Fixture.ContextThrows=failure=="context";
            Bad(Send(s,Message("SHUTDOWN",3),out var shutdown),"shutdown-local-dispose-failed");var receipt=s.CapturedReceipt;var failureDetail=s.Failure;Assert(!shutdown&&s.Pending&&failureDetail.Length>0&&s.HasContext);
            Assert(c.Disposed,"SDK early disposed flag missing");if(failure=="controller")Assert(ReferenceEquals(c,s.Controller)&&receipt is not null);
            World.ThrowContextDispose=Fixture.ContextThrows=false;
            Bad(Send(s,Message("SHUTDOWN",4),out shutdown),"shutdown-local-dispose-failed");Assert(!shutdown&&s.Failure==failureDetail&&ReferenceEquals(receipt,s.CapturedReceipt));
            Bad(Send(s,"RECREATE_TARGET",5),"recreate-release-failed");Assert(Fixture.Creates==1&&World.ContextCalls==1,"false disposal retry/recreate");AllClosed();
        }));
        rows.Add(Case("post-Dispose completion proof stays pending and retries same capture",()=>{
            var s=ManagedReady();World.AfterContextDispose=()=>ManagedAdmission.Idle.TerminationFails=true;
            Bad(Send(s,"RELEASE_TARGET",3),"release-local-dispose-failed");var capture=s.CapturedReceipt;
            Assert(!s.HasController&&s.HasContext&&s.Pending&&capture is not null,"completion proof not retained");
            Assert(!Fixture.Events.Contains("existing-sweep")&&!Fixture.Events.Contains("context-dispose"));
            ManagedAdmission.Idle.TerminationFails=false;World.AfterContextDispose=null;
            Good(Send(s,"RELEASE_TARGET",4));Assert(s.CapturedReceipt is null&&!s.HasContext&&World.ContextCalls==1,"completion retry disposed twice");AllClosed();
        }));
        rows.Add(Case("output cancellation lock timeout keeps owners retryable",()=>{
            var s=ManagedReady();using var entered=new ManualResetEventSlim();using var leave=new ManualResetEventSlim();
            var holding=Task.Run(()=>{lock(s.Controller!.OutputLock){entered.Set();Assert(leave.Wait(3000),"lock holder deadline");}});Assert(entered.Wait(2000));
            Fixture.Events.Clear();try{Bad(Send(s,"RELEASE_TARGET",3),"release-local-dispose-failed");Assert(s.HasController&&s.HasContext&&s.Pending);NoDestruction();}
            finally{leave.Set();holding.GetAwaiter().GetResult();}
            Good(Send(s,"SHUTDOWN",4));AllClosed();
        }));
        rows.Add(Case("direct ReleaseLocal serializes pending Handle on lifecycle gate",()=>{
            var s=ManagedReady();var c=s.Controller!;using var entered=new ManualResetEventSlim();using var resume=new ManualResetEventSlim();
            World.AfterContextDispose=()=>{entered.Set();Assert(resume.Wait(2000),"release coordination");};
            var release=Task.Run(()=>s.ReleaseLocal(out _));Assert(entered.Wait(2000));var handle=Task.Run(()=>Send(s,"QUIESCE",3));
            Assert(!handle.Wait(75),"Handle escaped release lock");resume.Set();Assert(release.GetAwaiter().GetResult());handle.GetAwaiter().GetResult();AllClosed();
        }));
        rows.Add(Case("ParentProcessLifetime disposal closes bound process handle and revokes wait",()=>{
            using var p=StartOwnedParent();var life=ParentProcessLifetime.TryOpen(p.Id)!;Assert(life.IsAlive);var token=life.ExitToken;
            var field=typeof(ParentProcessLifetime).GetField("_handle",BindingFlags.Instance|BindingFlags.NonPublic)!;
            var handle=(Microsoft.Win32.SafeHandles.SafeWaitHandle)field.GetValue(life)!;life.Dispose();life.Dispose();Assert(handle.IsClosed&&!life.IsAlive);
            ExitOwnedParent(p).GetAwaiter().GetResult();typeof(ParentProcessLifetime).GetMethod("ParentExited",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(life,null);
            Assert(!token.IsCancellationRequested,"disposed lifetime delivered late cancellation");
        }));
        foreach(var mode in new[]{"shutdown","read-loss","connect-loss","already-dead","pipe-break"})rows.Add(await PipeCase(mode));
        foreach(var mode in new[]{"read-output-stalled","read-clock-stalled"})rows.Add(await PipeCase(mode,true));
        bool pass=rows.All(r=>Equals(r["pass"],true));
        Console.WriteLine(JsonSerializer.Serialize(new{passed=pass,cases=rows,knownResourceContractFailures=rows.Where(r=>r.TryGetValue("knownFailureReproduced",out var known)&&Equals(known,true)).Select(r=>r["name"]),productMainExecuted=false,realBclWorkers=true,realBclNamedPipes=true,realParentProcessHandle=true,realSdkAssemblyLoaded=false,realDriver=false}));
        return pass?0:1;
    }
}


