using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using YeManInputHost.Diagnostics;

internal static class FakeClock { internal static long Tick; }
internal sealed class FakeThread(int id, double? total, int readStep = 0) : IDisposable
{
    internal int Id => id;
    internal int Disposals;
    internal TimeSpan TotalProcessorTime { get { FakeClock.Tick += readStep; return total is { } value ? TimeSpan.FromMilliseconds(value) : throw new Exception("fake-thread-read"); } }
    public void Dispose() => Disposals++;
}
internal sealed class FakeThreadCollection(FakeThread[] threads, int throwAt = -1) : IEnumerable<FakeThread>
{
    internal int Count => threads.Length;
    public IEnumerator<FakeThread> GetEnumerator() { for (var i = 0; i < threads.Length; i++) { if (i == throwAt) throw new Exception("fake-enumeration-fault"); yield return threads[i]; } }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
internal sealed class FakeProcess(double? total, FakeThread[] threads, int readStep = 0, int throwAt = -1) : IDisposable
{
    internal int Disposals, ThreadPropertyReads;
    internal TimeSpan TotalProcessorTime { get { FakeClock.Tick += readStep; return total is { } value ? TimeSpan.FromMilliseconds(value) : throw new Exception("fake-process-read"); } }
    internal FakeThreadCollection Threads { get { ThreadPropertyReads++; return new(threads, throwAt); } }
    public void Dispose() => Disposals++;
}
internal static class FakeProcessFactory
{
    internal static FakeProcess Current = new(0, []);
    internal static int Calls;
    internal static FakeProcess GetCurrentProcess() { Calls++; return Current; }
}
internal static class File
{
    internal static readonly List<string> Rows = new();
    internal static bool FailWrite;
    internal static void AppendAllText(string path, string text, Encoding encoding) { if (FailWrite) throw new Exception("fake-write-failure"); Rows.Add(text); }
}
internal static class Directory { internal static void CreateDirectory(string path) { } }
internal sealed class FakeStageEvidence
{
    internal int Begins, Ends;
    internal void BeginCost() => Begins++;
    internal void EndCost() => Ends++;
}

internal sealed record FakeCostTuple(string Persona, string RunId, ulong Epoch, ulong PowerGeneration);
internal static class ScreenPadSubmissionEvidence { internal static object? Capture() => null; }
internal sealed class ActualCostHost
{
    private readonly string _hostInstanceId = "fixture-cost-host";
    private readonly FakeCostTuple? _tuple = new("dualshock4", "fixture-cost-run", 1, 1);
    private readonly long[] _costSubmitUsSamples = [1, 7, 3, 9];
    private int _costSubmitUsCount = 4;
    private long _costFrames = 10, _costJsonParseUsSum = 20, _costJsonParseCount = 2;
    private long _costHidSubmitUsSum = 30, _costHidSubmitCount = 3, _costPipeReadUsSum = 50, _costPipeReadCount = 5;
    private long _costFramesRejected = 2, _costHidHideQueryFull = 3, _costHidHideQueryState = 4;
    private long _costLastAllocBytes = GC.GetTotalAllocatedBytes(false);
    private readonly Dictionary<string, int> _costRoleOsTid = new(StringComparer.Ordinal) { ["submit"] = 7 };
    private HostCpuCostWindow? _costCpuWindow;
    private long _costWindowStartTicks;
    private bool _costFlagEnabled;
    internal bool Gate = true;
    internal bool _msiCostDeferredRequested;
    internal int FlagReads, DeferredCalls, TransportReads;
    internal readonly FakeStageEvidence _msiHostStageEvidence = new();
    private static string CostStateDir => "FAKE-NO-IO";
    private string _phase = "Active";
    private bool CostLoggingRequestedByFlag() { FlagReads++; return Gate; }
    private double ComputeGcPauseMsPerSec(long windowMs) => 0;
    private object? CaptureUsbipTransportSafely(string phase, bool enabled) { TransportReads++; return null; }
    private void QueueMsiCostWindow(long now, long windowMs) { DeferredCalls++; ResetCostWindow(); }
    internal bool HasTracker => _costCpuWindow is not null;
    internal long Frames => _costFrames;
    internal void Due(long tick, FakeProcess process) { FakeClock.Tick = tick; FakeProcessFactory.Current = process; MaybeEmitCostWindow(); }
    // ACTUAL_METHODS_INSERTED_BY_RUNNER
}

internal static class HostCpuCostFixture
{
    private static int _checks, _groups;
    private static readonly List<string> _names = new();
    private static void Check(bool value, string name) { _checks++; if (!value) throw new Exception(name); }
    private static void Group(string name, Action test) { test(); _groups++; _names.Add(name); Console.WriteLine("PASS " + name); }
    private static JsonDocument Last() => JsonDocument.Parse(File.Rows[^1]);
    private static void Rate(JsonElement row, long? expected, string status)
    {
        Check(row.GetProperty("writerPid").GetInt32() == Environment.ProcessId, "cost writer independent of null transport");
        Check(row.GetProperty("hostInstanceId").GetString() == "fixture-cost-host" && row.GetProperty("runId").GetString() == "fixture-cost-run", "cost host/run identity");
        var rate = row.GetProperty("procCpuMsPerSec");
        Check(expected is { } value ? rate.ValueKind == JsonValueKind.Number && rate.GetInt64() == value : rate.ValueKind == JsonValueKind.Null, "nullable process rate");
        Check(row.GetProperty("cpuObservation").GetProperty("state").GetString() == status, "process status");
    }
    private static void Measurement(HostCpuCostWindow.Measurement result, long? rate, string status, long? window)
    { Check(result.MsPerSec == rate, "pure rate"); Check(result.Status == status, "pure status"); Check(result.WindowMs == window, "pure sampling interval"); }

    private static void ProcessEvidence()
    {
        var host = new ActualCostHost();
        host.Due(1000, new(0, [])); using (var row = Last()) Rate(row.RootElement, null, "baseline");
        host.Due(2000, new(50, [])); using (var row = Last()) Rate(row.RootElement, 50, "measured");
        host.Due(3000, new(50, [])); using (var row = Last()) Rate(row.RootElement, 0, "measured");
        host.Due(4000, new(null, [])); using (var row = Last()) Rate(row.RootElement, null, "read-unavailable");
        host.Due(5000, new(150, [])); using (var row = Last()) Rate(row.RootElement, null, "baseline");
        host.Due(6000, new(170, [])); using (var row = Last()) Rate(row.RootElement, 20, "measured");
        host.Due(7000, new(5, [])); using (var row = Last()) Rate(row.RootElement, null, "counter-regressed");
        host.Due(8000, new(15, [])); using (var row = Last()) Rate(row.RootElement, 10, "measured");
    }
    private static void ObservationInterval()
    {
        var host = new ActualCostHost();
        host.Due(1000, new(10, [new(7, 100, 25)], 50));
        host.Due(2500, new(60, [new(7, 133, 25)], 200));
        using var row = Last(); var root = row.RootElement;
        Rate(root, 30, "measured");
        Check(root.GetProperty("windowMs").GetInt64() == 1500, "original frame window retained");
        var meta = root.GetProperty("cpuObservation");
        Check(meta.GetProperty("runtimeWindowMs").GetInt64() == 1650, "actual process observation denominator");
        Check(meta.GetProperty("observationTick").GetInt64() == 2700, "process observation tick");
        Check(root.GetProperty("byThread")[0].GetProperty("cpuMsPerSec").GetInt64() == 20, "thread read observation denominator");
        Check(root.GetProperty("byThread")[0].GetProperty("role").GetString() == "submit", "original role mapping");
    }
    private static void ThreadLifetime()
    {
        var host = new ActualCostHost();
        host.Due(1000, new(20, [new(7, 500), new(8, null)]));
        using (var row = Last())
        { Check(row.RootElement.GetProperty("byThread").GetArrayLength() == 0, "no lifetime CPU on first observation"); Check(row.RootElement.GetProperty("cpuObservation").GetProperty("threadBaselines").GetInt32() == 1, "thread baseline evidence"); Check(row.RootElement.GetProperty("cpuObservation").GetProperty("threadReadFailures").GetInt32() == 1, "thread failed-read evidence"); }
        host.Due(2000, new(30, [new(7, 510), new(8, 900)]));
        using (var row = Last()) Check(row.RootElement.GetProperty("byThread").GetArrayLength() == 1, "new thread cumulative value excluded");
        host.Due(3000, new(40, [new(7, null), new(8, 920)]));
        using (var row = Last()) Check(row.RootElement.GetProperty("byThread")[0].GetProperty("tid").GetInt32() == 8, "failed thread omitted");
        host.Due(4000, new(50, [new(7, 550)]));
        using (var row = Last()) Check(row.RootElement.GetProperty("byThread").GetArrayLength() == 0, "thread failure recovery needs baseline");
        host.Due(5000, new(60, []));
        host.Due(6000, new(70, [new(7, 600), new(8, 999)]));
        using (var row = Last()) { Check(row.RootElement.GetProperty("byThread").GetArrayLength() == 0, "departed thread returning needs baseline"); Check(row.RootElement.GetProperty("cpuObservation").GetProperty("threadBaselines").GetInt32() == 2, "all returning identities rebased"); }
    }
    private static void TopFourAndOtherFields()
    {
        var host = new ActualCostHost();
        host.Due(1000, new(20, Enumerable.Range(1, 6).Select(i => new FakeThread(i, 100)).ToArray()));
        using (var row = Last())
        { var root = row.RootElement; Check(root.GetProperty("frames").GetInt64() == 10, "frame counter retained"); Check(root.GetProperty("submitElapsedUs").GetProperty("p50").GetInt64() == 7, "percentile retained"); Check(root.GetProperty("jsonParseUs").GetProperty("mean").GetInt64() == 10, "parse count/mean retained"); Check(root.GetProperty("hidhideQuery").GetProperty("state").GetInt64() == 4, "hidhide diagnostic count retained"); }
        Check(host.Frames == 0, "original aggregate reset");
        host.Due(2000, new(120, Enumerable.Range(1, 6).Select(i => new FakeThread(i, 100+i*10)).ToArray()));
        using var next = Last(); var rows = next.RootElement.GetProperty("byThread");
        Check(rows.GetArrayLength() == 4, "top four cap");
        for (var i = 0; i < 4; i++) Check(rows[i].GetProperty("cpuMsPerSec").GetInt64() == (6-i)*10, "sorted top four");
        Check(next.RootElement.GetProperty("threads").GetInt32() == 6, "full observed count");
        Check(next.RootElement.GetProperty("cpuObservation").GetProperty("threadRatesKnown").GetInt32() == 6, "known thread count distinct from top four");
    }
    private static void GateAndFailure()
    {
        var host = new ActualCostHost { Gate = false }; var calls = FakeProcessFactory.Calls; var rows = File.Rows.Count;
        host.Due(500, new(10, [])); Check(host.FlagReads == 0, "subsecond no gate IO");
        host.Due(1000, new(10, [])); Check(!host.HasTracker, "closed gate allocates no tracker"); Check(FakeProcessFactory.Calls == calls && File.Rows.Count == rows, "closed gate no runtime reads/rows");
        host.Gate = true; host.Due(2000, new(10, [new(7, 50)]));
        host.Due(3000, new(20, [new(7, 60)]));
        host.Gate = false; host.Due(4000, new(30, [new(7, 70)]));
        host.Gate = true; host.Due(10000, new(200, [new(7, 500)]));
        using (var row = Last()) { Rate(row.RootElement, null, "baseline"); Check(row.RootElement.GetProperty("byThread").GetArrayLength() == 0, "gate gap not current-window thread CPU"); }
        File.FailWrite = true; host.Due(11000, new(210, [new(7, 520)])); File.FailWrite = false;
        host.Due(12000, new(230, [new(7, 540)]));
        using (var row = Last()) { Rate(row.RootElement, null, "baseline"); Check(row.RootElement.GetProperty("byThread").GetArrayLength() == 0, "failed write invalidates observations"); }
        var partialThreads = new[] { new FakeThread(7, 550), new FakeThread(8, 900) }; var partial = new FakeProcess(240, partialThreads, throwAt:1); rows = File.Rows.Count;
        host.Due(13000, partial); Check(File.Rows.Count == rows, "partial enumeration retains original no-row behavior"); Check(partial.Disposals == 1 && partialThreads[0].Disposals == 1, "exception releases acquired diagnostic objects");
        host.Due(14000, new(250, [new(7, 570)])); using (var row = Last()) Rate(row.RootElement, null, "baseline");
        Check(host._msiHostStageEvidence.Begins == host._msiHostStageEvidence.Ends, "stage finally balanced after errors");
    }
    private static void DisposalAndDeferredBoundary()
    {
        var host = new ActualCostHost(); var threads = new[] { new FakeThread(7, 100), new FakeThread(8, null) }; var process = new FakeProcess(null, threads);
        host.Due(1000, process); Check(process.Disposals == 1 && threads.All(t => t.Disposals == 1), "actual method disposes diagnostic process and threads");
        Check(process.ThreadPropertyReads == 1, "actual method uses one thread snapshot");
        var deferred = new ActualCostHost { _msiCostDeferredRequested = true }; var calls = FakeProcessFactory.Calls; var count = File.Rows.Count;
        deferred.Due(1000, new(50, [])); Check(deferred.DeferredCalls == 1 && deferred.FlagReads == 0 && !deferred.HasTracker, "MSI deferred early branch untouched"); Check(FakeProcessFactory.Calls == calls && File.Rows.Count == count, "MSI branch no common runtime/sink calls");
    }
    private static void InvalidCounterAndRecovery()
    {
        var c = new HostCpuCostWindow();
        Measurement(c.ObserveProcess(0,100),null,"baseline",null);
        Measurement(c.ObserveProcess(10,100),null,"invalid-window",0);
        Measurement(c.ObserveProcess(20,50),null,"invalid-window",-50);
        Measurement(c.ObserveProcess(30,1050),10,"measured",1000);
        foreach (var v in new double?[] { null,-1,double.NaN,double.PositiveInfinity,double.NegativeInfinity })
        { Measurement(c.ObserveProcess(v,2050),null,"read-unavailable",null); Measurement(c.ObserveProcess(0,3050),null,"baseline",null); Measurement(c.ObserveProcess(50,4050),50,"measured",1000); c.Reset(); }
        Measurement(c.ObserveProcess(0,0),null,"baseline",null);
        Measurement(c.ObserveProcess(double.MaxValue,1),null,"invalid-rate",1);
        c.Reset(); Measurement(c.ObserveProcess(100,0),null,"baseline",null);
        Measurement(c.ObserveProcess(10,1000),null,"counter-regressed",1000);
        Measurement(c.ObserveProcess(20,2000),10,"measured",1000);
    }
    private static void ThreadChurnBound()
    {
        var c = new HostCpuCostWindow();
        var tableField = typeof(HostCpuCostWindow).GetField("_threads", BindingFlags.Instance|BindingFlags.NonPublic)!;
        var pendingField = typeof(HostCpuCostWindow).GetField("_observedThreads", BindingFlags.Instance|BindingFlags.NonPublic)!;
        for(var i=0;i<4096;i++)
        {
            c.BeginThreadObservation();
            Measurement(c.ObserveThread(i,500+i,i*1000),null,"baseline",null);
            c.FinishThreadObservation();
            Check(((IDictionary)tableField.GetValue(c)!).Count==1,"churn table contains only observed identity");
            Check(((IDictionary)pendingField.GetValue(c)!).Count==0,"old table cleared after swap");
        }
        c.BeginThreadObservation(); Measurement(c.ObserveThread(0,10000,4096000),null,"baseline",null); c.FinishThreadObservation();
        c.Reset(); Check(((IDictionary)tableField.GetValue(c)!).Count==0,"reset clears table");
    }
    private static void IndependentTimelineOracle()
    {
        var random = new Random(0x435055); var tracker = new HostCpuCostWindow();
        double? previous = null; long previousTick = 0, tick = 0; double total = 0;
        for(var i=0;i<4096;i++)
        {
            tick += random.Next(1,10000); total += random.Next(0,1000);
            double? input = random.Next(16)==0 ? null : total;
            var m = tracker.ObserveProcess(input,tick);
            if(input is null) { Check(m.MsPerSec is null && m.Status=="read-unavailable","oracle unavailable"); previous=null; continue; }
            if(previous is null) Check(m.MsPerSec is null && m.Status=="baseline","oracle first/recovery");
            else { var exact = decimal.Round(((decimal)input.Value-(decimal)previous.Value)*1000m/(tick-previousTick),0,MidpointRounding.ToEven); Check(m.MsPerSec==(long)exact && m.Status=="measured","independent decimal interval oracle"); }
            previous=input; previousTick=tick;
        }
    }
    internal static int Main(string[] args)
    {
        Group("actual source process unknown/zero/failed-read/recovery",ProcessEvidence);
        Group("actual source uses observation denominator",ObservationInterval);
        Group("actual source thread first/failure/departure baselines",ThreadLifetime);
        Group("top four and non-CPU counters preserved",TopFourAndOtherFields);
        Group("gate gaps and diagnostic errors invalidate baselines",GateAndFailure);
        Group("resource disposal and MSI deferred boundary",DisposalAndDeferredBoundary);
        Group("pure invalid counter/window/overflow guards",InvalidCounterAndRecovery);
        Group("thread churn table has no stale identity",ThreadChurnBound);
        Group("independent randomized decimal rate oracle",IndependentTimelineOracle);
        var result = JsonSerializer.Serialize(new { pass=true, checks=_checks, groups=_groups, names=_names, actualMethodSource=true, fakeBoundaries="Process/ProcessThread, monotonic clock, flag read and diagnostic sink; no product, process counters, SDK or device execution", sourcePathCoverage="Common MaybeEmitCostWindow only; MSI deferred branch early return checked", wholeProcessCpuProof=false, controllerWrites=false },new JsonSerializerOptions{WriteIndented=true});
        System.IO.File.WriteAllText(args[0],result+Environment.NewLine);
        return 0;
    }
}
