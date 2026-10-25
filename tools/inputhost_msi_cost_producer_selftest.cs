using System.Collections.Concurrent;
using System.Text.Json;
using YeManInputHost.Diagnostics;
using UsbipTransportObservation = FixtureObserver;
using D = YeManInputHost.Diagnostics.MsiCostWindowDiagnostics;

internal sealed record FixtureController(string Tag);
internal sealed class FixtureObserver
{
    internal static int Created, Captures;
    internal static readonly ConcurrentQueue<int> Threads = new();
    internal FixtureObserver() => Interlocked.Increment(ref Created);
    internal object Capture(object? target,bool verified)
    {
        Interlocked.Increment(ref Captures); Threads.Enqueue(Environment.CurrentManagedThreadId);
        return new { state="fixture-only",tag=(target as FixtureController)?.Tag,verified };
    }
}
internal static class FixtureClock { internal static long Tick; }
internal sealed class FixtureStage
{
    internal int Begins, Ends;
    internal void BeginCost()=>Begins++;
    internal void EndCost()=>Ends++;
}
internal sealed record Scope(string RunId,ulong Epoch,ulong PowerGeneration,string Persona,string ProfileIdentity);
internal enum FixturePhase { Prepared, Active, Quiesced }
internal sealed class ProductionProducer
{
    private readonly D? _msiCostDiagnostics;
    private bool _costFlagEnabled;
    private readonly long[] _costSubmitUsSamples = new long[2048];
    private int _costSubmitUsCount;
    private long _costFrames,_costJsonParseUsSum,_costJsonParseCount,_costHidSubmitUsSum,_costHidSubmitCount;
    private long _costPipeReadUsSum,_costPipeReadCount,_costFramesRejected,_costHidHideQueryFull,_costHidHideQueryState;
    private readonly Dictionary<string,int> _costRoleOsTid = new();
    private bool _assetVerified=true;
    private Scope? _tuple=new("run-A",1,1,"steamdeck","fixture-profile");
    private object? _controller;
    private UsbipTransportObservation? _usbipTransportObservation;
    private static readonly SteamDeckPadSubmissionEvidence ScreenPadSubmissionEvidence = new();
    private FixturePhase _phase=FixturePhase.Active;
    private string _hostInstanceId="host-A";
    private ulong _lastSequence=1;
    private long _costWindowStartTicks;
    private readonly bool _msiCostDeferredRequested=true;
    private readonly FixtureStage _msiHostStageEvidence=new();
    private HostCpuCostWindow? _costCpuWindow;
    private long _costLastAllocBytes;
    // The common production branch is compiled but outside this MSI fixture.
    // A wrong branch fails before any common runtime/file observation.
    private static string CostStateDir=>throw new InvalidOperationException("common path forbidden");
    private static bool CostLoggingRequestedByFlag()=>throw new InvalidOperationException("common path forbidden");
    private static double ComputeGcPauseMsPerSec(long ms)=>throw new InvalidOperationException("common path forbidden");
    private object? CaptureUsbipTransportSafely(string phase,bool on)=>throw new InvalidOperationException("common path forbidden");
    private string RoleOfCostThread(int tid)=>_costRoleOsTid.FirstOrDefault(x=>x.Value==tid).Key??"unlabeled";
    internal void SetInitialCostGate(bool enabled)=>_costFlagEnabled=enabled;
    internal void RecordAt(long tick){FixtureClock.Tick=tick;RecordCostFrame(4,false);}
    internal (int Begin,int End) StageCalls=>(_msiHostStageEvidence.Begins,_msiHostStageEvidence.Ends);
    internal ProductionProducer(D? diagnostics,object? controller)
    { _msiCostDiagnostics=diagnostics;_controller=controller; }
    internal void Seed(int frames,long[] samples)
    {
        _costFrames=frames;_costSubmitUsCount=samples.Length;
        Array.Copy(samples,_costSubmitUsSamples,samples.Length);
        _costJsonParseUsSum=30;_costJsonParseCount=3;_costHidSubmitUsSum=60;_costHidSubmitCount=3;
        _costPipeReadUsSum=90;_costPipeReadCount=3;_costFramesRejected=1;_costHidHideQueryFull=2;_costHidHideQueryState=4;
        _costRoleOsTid.Clear();_costRoleOsTid["pipe-read"]=7;
    }
    internal bool CountersReset => _costFrames==0&&_costSubmitUsCount==0&&_costJsonParseUsSum==0&&_costJsonParseCount==0&&
        _costHidSubmitUsSum==0&&_costHidSubmitCount==0&&_costPipeReadUsSum==0&&_costPipeReadCount==0&&
        _costFramesRejected==0&&_costHidHideQueryFull==0&&_costHidHideQueryState==0;
    internal bool FlagEnabled=>_costFlagEnabled;
    internal void Queue(long tick)=>QueueMsiCostWindow(tick,1000);
    internal void Mutate(object? controller,string persona="steamdeck",bool verified=true)
    {
        _controller=controller;_tuple=new("run-B",2,2,persona,"profile-B");_hostInstanceId="host-B";
        _assetVerified=verified;_phase=FixturePhase.Quiesced;_lastSequence=99;
        _usbipTransportObservation=null;_costSubmitUsSamples[0]=999;_costRoleOsTid.Clear();
    }
    internal void SetInvalidCount()=>_costSubmitUsCount=3000;
    internal void SeedPadEdge(uint packet,bool active,bool reset=false)
    {
        if(reset)ScreenPadSubmissionEvidence.Reset();var bytes=new byte[64];bytes[0]=1;bytes[2]=9;bytes[3]=64;
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(4),packet);
        if(active)System.Buffers.Binary.BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(8),1UL<<19);
        ScreenPadSubmissionEvidence.Record(true,bytes,new(active,false,0,0,0),default,packet,packet,packet*8);
    }
    // EXACT_PRODUCTION_METHODS
}
internal sealed class Worker : IDisposable
{
    internal readonly D Diagnostic;
    internal readonly ConcurrentQueue<string> Rows=new();
    internal readonly ManualResetEventSlim Entered=new(),Release=new();
    internal int RuntimeCalls,FlagCalls,AppendCalls;
    internal int FailFlagOnce;
    private readonly bool enabled,blocked;
    internal readonly ConcurrentQueue<int> RuntimeThreads=new(),FlagThreads=new(),AppendThreads=new();
    internal Worker(bool enabled=true,bool blocked=false)
    {
        this.enabled=enabled;this.blocked=blocked;
        Diagnostic=new(enabled,
            ()=>{Interlocked.Increment(ref FlagCalls);FlagThreads.Enqueue(Environment.CurrentManagedThreadId);if(Interlocked.Exchange(ref FailFlagOnce,0)!=0)throw new IOException("fixture-flag");return this.enabled;},
            ()=>{int n=Interlocked.Increment(ref RuntimeCalls);RuntimeThreads.Enqueue(Environment.CurrentManagedThreadId);if(this.blocked){Entered.Set();if(!Release.Wait(TimeSpan.FromSeconds(5)))throw new TimeoutException("release runtime fixture");}return new D.RuntimeSample(1000L*n,50*n,1000*n,0,1024,0,0,0,[new(7,10*n)],7);},
            line=>{Interlocked.Increment(ref AppendCalls);AppendThreads.Enqueue(Environment.CurrentManagedThreadId);Rows.Enqueue(line);});
    }
    public void Dispose()
    {Release.Set();Diagnostic.Dispose();if(!SpinWait.SpinUntil(()=>Diagnostic.QueueCounters.Stopped,5000))throw new Exception("worker did not stop");Entered.Dispose();Release.Dispose();}
}
internal static class ProducerTests
{
    private static int checks;private static readonly List<object> groups=new();
    private static void Check(bool condition,string why){checks++;if(!condition)throw new Exception(why);}
    private static void Until(Func<bool> predicate,string why){Check(SpinWait.SpinUntil(predicate,5000),why);}
    private static void Group(string name,Action action){int start=checks;action();groups.Add(new{name,checks=checks-start,pass=true});Console.WriteLine("PASS "+name);}
    private static int CallAndReturnBeforeRelease(Action action,Worker worker)
    {
        using var done=new ManualResetEventSlim();Exception? error=null;int tid=0;
        var producer=new Thread(()=>{tid=Environment.CurrentManagedThreadId;try{action();}catch(Exception ex){error=ex;}finally{done.Set();}}){IsBackground=true};
        producer.Start();bool returned=done.Wait(TimeSpan.FromSeconds(3));
        if(!returned)worker.Release.Set();
        Check(producer.Join(5000),"producer cleanup");Check(returned,"producer returned before worker release");
        Check(error is null,"no producer exception");Check(!worker.Release.IsSet,"did not release blocked worker to make producer finish");return tid;
    }
    private static void PadSubmissionSnapshot()
    {
        using var w=new Worker(blocked:true);var host=new ProductionProducer(w.Diagnostic,new FixtureController("pad"));
        host.SeedPadEdge(1,true,reset:true);CallAndReturnBeforeRelease(()=>host.Queue(1000),w);
        Check(w.Entered.Wait(TimeSpan.FromSeconds(3)),"pad worker blocked");
        host.SeedPadEdge(2,false);CallAndReturnBeforeRelease(()=>host.Queue(2000),w);host.SeedPadEdge(3,true);
        w.Release.Set();Until(()=>w.Diagnostic.QueueCounters.Completed==2,"pad snapshots emitted");
        var rows=w.Rows.Select(x=>JsonDocument.Parse(x).RootElement.Clone()).ToArray();
        var a=rows[0].GetProperty("steamDeckPadSubmissions");var b=rows[1].GetProperty("steamDeckPadSubmissions");
        Check(a.GetProperty("Reports").GetUInt64()==1&&a.GetProperty("LastSuccessfulReport").GetProperty("PacketCounter").GetUInt32()==1,"first queued pad snapshot remains before later release");
        Check(b.GetProperty("Reports").GetUInt64()==2&&b.GetProperty("TouchChanges").GetUInt64()==1,"second pad counters do not see later recontact");
        Check(b.GetProperty("Entries")[1].GetProperty("TouchMask").GetByte()==0&&b.GetProperty("LastSuccessfulReport").GetProperty("TouchMask").GetByte()==0,"short release retained in actual worker JSON");
        Check(!b.GetProperty("SteamConsumptionProven").GetBoolean()&&!b.GetProperty("TransferCompletedProven").GetBoolean(),"worker JSON keeps delivery limits");
    }
    private static void BlockedAndImmutable()
    {
        int capturesBefore=FixtureObserver.Captures;
        using var w=new Worker(blocked:true);var a=new FixtureController("A");var b=new FixtureController("B");
        var host=new ProductionProducer(w.Diagnostic,a);host.Seed(125,[11,3,7]);
        int producer=CallAndReturnBeforeRelease(()=>host.Queue(1000),w);
        Check(w.Entered.Wait(TimeSpan.FromSeconds(3)),"real diagnostic worker blocked in fake runtime");
        Check(host.CountersReset,"first source finally resets counters");
        host.Mutate(b);host.Seed(250,[25,5,15]);CallAndReturnBeforeRelease(()=>host.Queue(2000),w);
        Check(host.CountersReset,"second source finally resets counters");
        host.Seed(375,[1,2,3]);CallAndReturnBeforeRelease(()=>host.Queue(3000),w);
        Check(host.CountersReset,"rejected source snapshot still resets counters");
        var q=w.Diagnostic.QueueCounters;Check(q.Accepted==2&&q.DroppedFull==1&&q.Completed==0,"one active and one pending; third dropped");
        Check(w.RuntimeCalls==1&&w.AppendCalls==0&&FixtureObserver.Captures==capturesBefore,"no producer runtime/SDK/write");
        host.Mutate(null,persona:"xbox360",verified:false);w.Release.Set();Until(()=>w.Diagnostic.QueueCounters.Completed==2,"two immutable windows completed");
        var rows=w.Rows.Select(s=>JsonDocument.Parse(s).RootElement.Clone()).ToArray();Check(rows.Length==2,"two rows");
        Check(rows[0].GetProperty("frames").GetInt64()==125&&rows[1].GetProperty("frames").GetInt64()==250,"captured frame counts");
        Check(rows[0].GetProperty("submitElapsedUs").GetProperty("p50").GetInt64()==7,"captured array retained before original mutation");
        Check(rows[1].GetProperty("submitElapsedUs").GetProperty("max").GetInt64()==25,"pending array retained");
        Check(rows[0].GetProperty("msiCostWindowDeferred").GetProperty("runId").GetString()=="run-A","first tuple captured");
        Check(rows[1].GetProperty("msiCostWindowDeferred").GetProperty("runId").GetString()=="run-B","second tuple captured");
        Check(rows[0].GetProperty("usbipTransport").GetProperty("snapshot").GetProperty("tag").GetString()=="A","first weak target and observer captured");
        Check(rows[1].GetProperty("usbipTransport").GetProperty("snapshot").GetProperty("tag").GetString()=="B","pending target independent from current host");
        Check(w.RuntimeThreads.All(t=>t!=producer)&&w.AppendThreads.All(t=>t!=producer),"runtime and append are not first caller thread");
        GC.KeepAlive(a);GC.KeepAlive(b);
    }
    private static void OffGate()
    {
        int created=FixtureObserver.Created,captures=FixtureObserver.Captures;using var w=new Worker(enabled:false);
        var host=new ProductionProducer(w.Diagnostic,new FixtureController("off"));
        for(int i=0;i<3;i++){host.Seed(0,[]);host.Queue((i+1)*1000);Until(()=>w.Diagnostic.QueueCounters.Completed==i+1,"off flag checked asynchronously");Check(host.CountersReset&&!host.FlagEnabled,"off producer counter/gate state");}
        Check(w.FlagCalls==3&&w.RuntimeCalls==0&&w.AppendCalls==0&&w.Rows.IsEmpty,"off never samples or writes");
        Check(FixtureObserver.Captures==captures,"off never invokes SDK reader");
        Check(FixtureObserver.Created==created+1,"source still creates cached SDK observer before worker learns gate");
    }
    private static void CloseDoesNotWait()
    {
        using var w=new Worker(blocked:true);var host=new ProductionProducer(w.Diagnostic,new FixtureController("close"));host.Seed(10,[2]);
        CallAndReturnBeforeRelease(()=>host.Queue(1000),w);Check(w.Entered.Wait(TimeSpan.FromSeconds(3)),"active blocked");
        host.Seed(20,[4]);CallAndReturnBeforeRelease(()=>host.Queue(2000),w);
        CallAndReturnBeforeRelease(w.Diagnostic.Dispose,w);var q=w.Diagnostic.QueueCounters;
        Check(q.Discarded==1&&!q.Stopped,"dispose discarded pending, active not falsely stopped");
        host.Seed(30,[6]);CallAndReturnBeforeRelease(()=>host.Queue(3000),w);
        Check(w.Diagnostic.QueueCounters.DroppedClosed==1&&host.CountersReset,"closed observer cannot block or retain counters");
        w.Release.Set();Until(()=>w.Diagnostic.QueueCounters.Stopped,"active completes then stops");Check(w.RuntimeCalls==1,"pending/closed windows never sampled");
    }
    private static void Failures()
    {
        using var w=new Worker();var host=new ProductionProducer(w.Diagnostic,new FixtureController("failure"));
        w.FailFlagOnce=1;host.Seed(5,[1]);host.Queue(1000);Until(()=>w.Diagnostic.QueueCounters.Failed==1,"flag failure recorded");Check(host.CountersReset,"source reset before worker failure");
        host.Seed(6,[2]);host.Queue(2000);Until(()=>w.Diagnostic.QueueCounters.Completed==1,"worker recovers after flag failure");Check(w.Rows.Count==1,"recovery row");
        long accepted=w.Diagnostic.QueueCounters.Accepted;host.Seed(7,[3]);host.SetInvalidCount();host.Queue(3000);
        Check(host.CountersReset&&w.Diagnostic.QueueCounters.Accepted==accepted,"copy error contained on source and finally reset");
        var absent=new ProductionProducer(null,new FixtureController("absent"));absent.Seed(8,[4]);absent.Queue(4000);Check(absent.CountersReset&&!absent.FlagEnabled,"unavailable diagnostics return/reset");
    }
    private static void Generation()
    {
        int before=FixtureObserver.Captures;using var w=new Worker(blocked:true);var host=new ProductionProducer(w.Diagnostic,new FixtureController("old"));host.Seed(9,[1]);
        CallAndReturnBeforeRelease(()=>host.Queue(1000),w);Check(w.Entered.Wait(TimeSpan.FromSeconds(3)),"old generation sample entered");
        w.Diagnostic.InvalidateController();w.Release.Set();Until(()=>w.Diagnostic.QueueCounters.Completed==1,"invalid generation processed");
        using var doc=JsonDocument.Parse(w.Rows.Single());var r=doc.RootElement;
        Check(r.GetProperty("procCpuMsPerSec").ValueKind==JsonValueKind.Null,"stale runtime not a rate");
        Check(r.GetProperty("cpuObservation").GetProperty("continuityState").GetString()=="controller-generation-ended","stale source/worker crossing marked");
        Check(FixtureObserver.Captures==before,"old generation never observes SDK");
    }
    private static void CadenceBoundaries()
    {
        using var w=new Worker(blocked:true);var host=new ProductionProducer(w.Diagnostic,new FixtureController("cadence"));host.SetInitialCostGate(true);
        CallAndReturnBeforeRelease(()=>{host.RecordAt(0);host.RecordAt(999);},w);
        Check(w.Diagnostic.QueueCounters.Accepted==0&&host.StageCalls==(0,0),"no window before 1000ms");
        CallAndReturnBeforeRelease(()=>host.RecordAt(1000),w);Check(w.Entered.Wait(TimeSpan.FromSeconds(3)),"first due window worker entered");
        CallAndReturnBeforeRelease(()=>{host.RecordAt(1000);host.RecordAt(1999);},w);
        Check(w.Diagnostic.QueueCounters.Accepted==1&&host.StageCalls==(1,1),"same clock/before next boundary does not repost");
        CallAndReturnBeforeRelease(()=>host.RecordAt(2000),w);Check(w.Diagnostic.QueueCounters.Accepted==2&&host.StageCalls==(2,2),"second boundary exactly once");
        w.Release.Set();Until(()=>w.Diagnostic.QueueCounters.Completed==2,"cadence windows completed");
        var counts=w.Rows.Select(x=>JsonDocument.Parse(x).RootElement.GetProperty("frames").GetInt64()).ToArray();Check(counts.SequenceEqual(new long[]{3,3}),"every logical cost callback counted in its window");
    }
    private static void LogicalStreamWhileBlocked()
    {
        using var w=new Worker(blocked:true);var host=new ProductionProducer(w.Diagnostic,new FixtureController("logical-stream"));host.SetInitialCostGate(true);int continued=0;
        CallAndReturnBeforeRelease(()=>{for(int i=1;i<=125;i++){host.RecordAt(i*8);continued++;}},w);
        Check(w.Entered.Wait(TimeSpan.FromSeconds(3)),"worker blocked after first 125 logical callbacks");
        CallAndReturnBeforeRelease(()=>{for(int i=126;i<=500;i++){host.RecordAt(i*8);continued++;}},w);
        var q=w.Diagnostic.QueueCounters;Check(continued==500,"all caller continuation sentinels reached");
        Check(q.Accepted==2&&q.DroppedFull==2&&q.Completed==0,"4 logical 1Hz windows while worker blocked: active/pending/2 dropped");
        Check(host.CountersReset&&host.StageCalls==(4,4),"cadence reset/finally independent from full queue");
        w.Release.Set();Until(()=>w.Diagnostic.QueueCounters.Completed==2,"bounded backlog drained");
        Check(w.Rows.All(x=>JsonDocument.Parse(x).RootElement.GetProperty("frames").GetInt64()==125),"retained rows are 125 logical callbacks each");
    }
    private static void OffCadence()
    {
        using var w=new Worker(enabled:false);var host=new ProductionProducer(w.Diagnostic,new FixtureController("off-cadence"));host.SetInitialCostGate(false);
        for(int i=1;i<=500;i++)host.RecordAt(i*8);
        Until(()=>w.Diagnostic.QueueCounters.Completed+w.Diagnostic.QueueCounters.Failed==w.Diagnostic.QueueCounters.Accepted,"off requests drained");
        var q=w.Diagnostic.QueueCounters;Check(q.Accepted+q.Dropped==4&&host.StageCalls==(4,4),"off checks only on four logical boundaries");
        Check(w.RuntimeCalls==0&&w.AppendCalls==0&&w.Rows.IsEmpty,"off cadence does not sample runtime or write logs");
        Check(host.CountersReset&&!host.FlagEnabled,"off callbacks do not collect frame metrics");
    }
    private static void PreflightSkipsPayload()
    {
        using var w=new Worker(blocked:true);
        var host=new ProductionProducer(w.Diagnostic,new FixtureController("preflight"));
        host.Seed(1,[1]);host.Queue(1000);
        Check(w.Entered.Wait(TimeSpan.FromSeconds(3)),"preflight fixture active worker blocked");
        host.Seed(2,[2]);host.Queue(2000);
        Check(w.Diagnostic.QueueCounters.Accepted==2,"preflight fixture pending slot full");
        host.Mutate(new FixtureController("must-not-construct-observer"));
        int created=FixtureObserver.Created;
        host.Seed(3,[3]);host.Queue(3000);
        var full=w.Diagnostic.QueueCounters;
        Check(FixtureObserver.Created==created,"full request skips observer/delegate/weak reference construction");
        Check(full.Dropped==1&&full.DroppedFull==1&&full.Accepted==2,"full request counted once");
        Check(host.CountersReset,"full preflight resets source counters");
        host.Seed(4,[4]);host.SetInvalidCount();host.Queue(4000);
        full=w.Diagnostic.QueueCounters;
        Check(full.Dropped==2&&full.DroppedFull==2,"full malformed-copy sentinel rejected before copying samples");
        Check(host.CountersReset&&FixtureObserver.Created==created,"malformed full request resets without payload");
        w.Diagnostic.Dispose();
        host.Seed(5,[5]);host.Queue(5000);
        var closed=w.Diagnostic.QueueCounters;
        Check(closed.Dropped==3&&closed.DroppedClosed==1&&closed.Discarded==1,"closed request counted once, pending discarded once");
        Check(host.CountersReset&&FixtureObserver.Created==created,"closed skips observer construction and resets");
        host.Seed(6,[6]);host.SetInvalidCount();host.Queue(6000);
        closed=w.Diagnostic.QueueCounters;
        Check(closed.Dropped==4&&closed.DroppedClosed==2,"closed malformed-copy sentinel rejected before sample copy");
        Check(host.CountersReset,"closed malformed request resets");
        Check(w.RuntimeCalls==1&&w.AppendCalls==0,"preflight rejections cause no external sampling or writes");
        w.Release.Set();Until(()=>w.Diagnostic.QueueCounters.Stopped,"preflight active worker exits after release");
    }
    private static void PreflightDoesNotReserve()
    {
        using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();
        var calls=new ConcurrentQueue<int>();
        using var d=new MsiCostWindowDispatcher<int>(x=>{calls.Enqueue(x);if(x==1){entered.Set();release.Wait();}});
        try
        {
            Check(!d.TryRejectBeforeSnapshot()&&!d.TryRejectBeforeSnapshot(),"empty preflight can be repeated without a reservation");
            Check(d.SnapshotCounters().Accepted==0&&d.SnapshotCounters().Dropped==0,"empty preflight changes no counters");
            Check(d.TryPost(1)&&entered.Wait(TimeSpan.FromSeconds(3)),"active callback consumes slot");
            Check(!d.TryRejectBeforeSnapshot(),"active callback alone leaves one pending slot");
            Check(d.TryPost(2),"another request takes slot after false preflight");
            Check(!d.TryPost(3),"actual admission still rejects now-full queue");
            Check(d.TryRejectBeforeSnapshot(),"preflight observes full pending slot");
            release.Set();Until(()=>d.SnapshotCounters().Completed==2,"original active/pending jobs drain");
            var q=d.SnapshotCounters();
            Check(q.Accepted==2&&q.Dropped==2&&q.DroppedFull==2,"each actual/post and preflight rejection counted exactly once");
            Check(!d.TryRejectBeforeSnapshot()&&d.TryPost(4),"full preflight did not leave a reservation after drain");
            Until(()=>d.SnapshotCounters().Completed==3,"subsequent valid request completes");
            Check(calls.SequenceEqual(new[]{1,2,4}),"rejected snapshot never creates or replaces a job");
            Check(!d.TryRejectBeforeSnapshot(),"empty preflight before closure");
            d.Dispose();Check(!d.TryPost(5),"close after false preflight wins final admission");
            Until(()=>d.SnapshotCounters().Stopped,"dispatcher closes after drain");
            Check(d.TryRejectBeforeSnapshot(),"stopped dispatcher rejects before payload");
            q=d.SnapshotCounters();
            Check(q.Accepted==3&&q.Completed==3&&q.Dropped==4&&q.DroppedClosed==2,"final rejection totals reconcile after thread stop");
        }
        finally {release.Set();d.Dispose();}
    }
    private static int Main(string[] args)
    {
        Group("actual producer is nonblocking and captures immutable data",BlockedAndImmutable);
        Group("actual producer carries immutable per-report pad edges",PadSubmissionSnapshot);
        Group("disabled diagnostics use worker flag gate",OffGate);
        Group("active/pending/closed shutdown stays nonblocking",CloseDoesNotWait);
        Group("source and worker failures stay diagnostic-only",Failures);
        Group("actual controller invalidation crosses queued source safely",Generation);
        Group("actual MSI cadence boundary and duplicate clock",CadenceBoundaries);
        Group("500 logical callbacks continue while worker is blocked",LogicalStreamWhileBlocked);
        Group("disabled cost cadence retains one-Hz flag checks",OffCadence);
        Group("full and closed preflight skips payload with exactly one rejection",PreflightSkipsPayload);
        Group("advisory preflight leaves final admission authoritative",PreflightDoesNotReserve);
        var r=new{pass=true,groups=groups.Count,checks,results=groups,productionQueueAndResetCompiledExactly=true,productionCadenceAndRecordCompiledWithInjectedClock=true,
            runtimeAndSdkAreInjected=true,deviceCalls=0,productExecuted=false,timingBenchmark=false,
            scope="Source diagnostic producer, logical cadence and worker protocol only; no real virtual submit, pipe ACK, game consumption, input jitter or hardware proof"};
        System.IO.File.WriteAllText(args[0],JsonSerializer.Serialize(r,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine($"groups={groups.Count} checks={checks}");return 0;
    }
}
