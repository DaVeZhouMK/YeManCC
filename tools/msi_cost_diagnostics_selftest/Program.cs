using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text.Json;
using YeManInputHost.Diagnostics;

internal enum FakePhase { Active }
internal sealed record FakeTuple(string RunId, ulong Epoch, ulong PowerGeneration, string Persona, string ProfileIdentity);
internal sealed class MsiHostStageEvidence { internal void BeginCost() { } internal void EndCost() { } }
internal sealed class UsbipTransportObservation
{
    internal static Func<object?, bool, object?>? Probe;
    internal object? Capture(object? controller, bool verified) => Probe?.Invoke(controller, verified) ?? new { Status = "fake-observed" };
}

internal static class Program
{
    private static int _checks, _groups;
    private static void Check(bool condition, string name) { _checks++; if (!condition) throw new Exception(name); }
    private static void Wait(ManualResetEventSlim signal, string name) => Check(signal.Wait(3000), name);
    private static void Until(Func<bool> condition, string name) => Check(SpinWait.SpinUntil(condition, 3000), name);
    private static MsiCostWindowDiagnostics.RuntimeSample Runtime(long tick = 1000, long alloc = 100, double cpu = 2) =>
        new(tick, cpu, alloc, 1, 1234, 1, 2, 3, new[] { new MsiCostWindowDiagnostics.ThreadCpu(7, cpu) });
    private static JsonDocument Read(string row) => JsonDocument.Parse(row);
    private static void Group(string name, Action test) { test(); _groups++; Console.WriteLine("PASS " + name); }
    private static void Stop(MsiCostWindowDiagnostics diagnostics) { diagnostics.Dispose(); Until(() => diagnostics.QueueCounters.Stopped, "worker actually stopped"); }

    private static void LegacyBeforeAfter()
    {
        using var entered = new ManualResetEventSlim(); using var release = new ManualResetEventSlim();
        using var acknowledged = new ManualResetEventSlim();
        var host = new LegacyCostHost(true, null, () => { entered.Set(); release.Wait(); return false; });
        var call = Task.Run(() => { host.DueFrame(); acknowledged.Set(); });
        try { Wait(entered, "legacy entered caller flag read"); Check(!acknowledged.IsSet, "legacy due-window blocks caller response"); }
        finally { release.Set(); Check(call.Wait(3000), "legacy fixture released"); }
        Check(acknowledged.IsSet, "legacy eventually returns after external diagnostic boundary");
    }

    private static void BlockedBoundary(string boundary)
    {
        using var entered = new ManualResetEventSlim(); using var release = new ManualResetEventSlim();
        using var acknowledged = new ManualResetEventSlim();
        var rows = new ConcurrentQueue<string>();
        long boundaryEnteredTick=0;
        void Block(string where) { if (where == boundary) { boundaryEnteredTick=Environment.TickCount64; entered.Set(); release.Wait(); } }
        UsbipTransportObservation.Probe = (c, v) => { Block("sdk"); return new { Status = "fake-observed" }; };
        using var diagnostics = new MsiCostWindowDiagnostics(true,
            () => { Block("flag"); return true; }, () => { Block("runtime"); return Runtime(); },
            row => { Block("append"); rows.Enqueue(row); });
        var host = new CurrentCostHost(true, diagnostics);
        var call = Task.Run(() => { host.DueFrame(); acknowledged.Set(); });
        try
        {
            Wait(acknowledged, boundary + " does not block actual current cost caller");
            Wait(entered, boundary + " reached only on diagnostic worker");
            Check(call.IsCompleted, "actual current cost call completed");
            Check(host.FramesRemaining == 0 && host.SamplesRemaining == 0, "producer counters reset");
            host.DueFrame(8); host.DueFrame(9);
            Check(diagnostics.QueueCounters.Accepted == 2, "only active plus pending accepted");
            Check(diagnostics.QueueCounters.DroppedFull >= 1, "full diagnostic queue drops without extra writer");
            var sw = Stopwatch.StartNew(); diagnostics.Dispose();
            Check(sw.ElapsedMilliseconds < 500, "Dispose does not await external diagnostic boundary");
            Check(!diagnostics.QueueCounters.Stopped, "stalled worker is not mislabeled stopped");
            Check(diagnostics.QueueCounters.Discarded == 1, "pending discarded on close");
            host.DueFrame(); Check(diagnostics.QueueCounters.DroppedClosed >= 1, "closed worker not restarted");
        }
        finally { release.Set(); Stop(diagnostics); UsbipTransportObservation.Probe = null; }
        Check(rows.Count == 1, "only already-active diagnostic may finish after close");
        using var observed=Read(rows.Single());
        var timing=observed.RootElement.GetProperty("msiCostWindowDeferred");
        Check(timing.GetProperty("workerStartedTick").GetInt64()<=boundaryEnteredTick, "worker start precedes external read and append boundaries");
        Check(timing.GetProperty("flagReadMs").GetInt64()>=0 && timing.GetProperty("runtimeReadMs").GetInt64()>=0 && timing.GetProperty("sdkReadMs").GetInt64()>=0, "worker component intervals reported separately from queue delay");
    }

    private static void ImmutableWindow()
    {
        using var entered = new ManualResetEventSlim(); using var release = new ManualResetEventSlim();
        var rows = new ConcurrentQueue<string>();
        using var diagnostics = new MsiCostWindowDiagnostics(true, () => { entered.Set(); release.Wait(); return true; }, () => Runtime(), rows.Enqueue);
        var host = new CurrentCostHost(true, diagnostics);
        try { host.DueFrame(4); Wait(entered, "immutable test flag entered"); host.MutateAfterQueue(); }
        finally { release.Set(); }
        Until(() => diagnostics.QueueCounters.Completed == 1, "immutable window completed"); Stop(diagnostics);
        using var doc = Read(rows.Single()); var root = doc.RootElement;
        Check(root.GetProperty("frames").GetInt64() == 1, "frozen frame counter");
        Check(root.GetProperty("submitElapsedUs").GetProperty("max").GetInt64() == 4, "submit sample clone independent of producer mutation");
        var deferred = root.GetProperty("msiCostWindowDeferred");
        Check(deferred.GetProperty("runId").GetString() == "old-run", "historical run identity retained");
        Check(deferred.GetProperty("epoch").GetUInt64() == 8 && deferred.GetProperty("requestedSequence").GetUInt64() == 20588, "historical request tuple retained");
        Check(!deferred.GetProperty("controllerWrites").GetBoolean() && !deferred.GetProperty("pipeWrites").GetBoolean(), "no invented control proof");
        Check(root.GetProperty("procCpuMsPerSec").ValueKind == JsonValueKind.Null, "initial runtime delta unknown");
        Check(root.GetProperty("allocPerFrame").ValueKind == JsonValueKind.Null, "worker allocation cannot be attributed to requested frame");
    }

    private static void EndedGeneration(bool during)
    {
        using var entered = new ManualResetEventSlim(); using var release = new ManualResetEventSlim();
        var rows = new ConcurrentQueue<string>(); int captures = 0;
        UsbipTransportObservation.Probe = (c, v) => { Interlocked.Increment(ref captures); if (during) { entered.Set(); release.Wait(); } return new { Status = "fake-observed" }; };
        using var diagnostics = new MsiCostWindowDiagnostics(true, () => { if (!during) { entered.Set(); release.Wait(); } return true; }, () => Runtime(), rows.Enqueue);
        var host = new CurrentCostHost(true, diagnostics);
        try { host.DueFrame(); Wait(entered, "generation test entered"); diagnostics.InvalidateController(); }
        finally { release.Set(); }
        Until(() => diagnostics.QueueCounters.Completed == 1, "ended-generation window finished"); Stop(diagnostics);
        using var doc = Read(rows.Single()); var observation = doc.RootElement.GetProperty("usbipTransport");
        if (during) { Check(captures == 1, "one passive read before generation ended"); Check(observation.GetProperty("snapshot").ValueKind == JsonValueKind.Null, "in-flight stale observation withheld"); Check(!observation.GetProperty("controllerGenerationCurrentAfter").GetBoolean(), "generation ending explicitly reported"); }
        else { Check(captures == 0, "ended generation does not invoke approved snapshot reader"); Check(observation.GetProperty("state").GetString() == "omitted", "queued stale target observation omitted"); }
        UsbipTransportObservation.Probe = null;
    }

    private static void LoggingAndFailure()
    {
        var rows = new ConcurrentQueue<string>(); int enabled = 0, fail = 1;
        using var diagnostics = new MsiCostWindowDiagnostics(false, () => Volatile.Read(ref enabled) != 0,
            () => Interlocked.Exchange(ref fail, 0) == 1 ? throw new IOException("fake blocked-read failure") : Runtime(), rows.Enqueue);
        var host = new CurrentCostHost(true, diagnostics); host.Enabled(false);
        host.DueFrame(); Until(() => diagnostics.QueueCounters.Completed == 1, "disabled flag sampled in background");
        Check(rows.IsEmpty && !diagnostics.LoggingEnabled, "logging off writes no row");
        Volatile.Write(ref enabled, 1); host.DueFrame();
        Until(() => diagnostics.QueueCounters.Failed == 1, "diagnostic runtime failure recorded");
        Check(diagnostics.LoggingEnabled, "flag can re-enable asynchronously");
        host.DueFrame(); Until(() => diagnostics.QueueCounters.Completed == 2, "later diagnostic window survives exception");
        Stop(diagnostics); Check(rows.Count == 1, "failure does not poison next diagnostic window");
    }

    private static void RuntimeRates()
    {
        var rows = new ConcurrentQueue<string>(); int ordinal = 0;
        using var diagnostics = new MsiCostWindowDiagnostics(true, () => true,
            () => Interlocked.Increment(ref ordinal) == 1 ? Runtime(1000, 100, 2) : Runtime(3000, 300, 22), rows.Enqueue);
        var host = new CurrentCostHost(true, diagnostics); host.RemoveSource();
        host.DueFrame(); Until(() => diagnostics.QueueCounters.Completed == 1, "first runtime row");
        host.DueFrame(); Until(() => diagnostics.QueueCounters.Completed == 2, "second runtime row"); Stop(diagnostics);
        using var doc = Read(rows.Last()); var root = doc.RootElement;
        Check(root.GetProperty("procCpuMsPerSec").GetDouble() == 10, "CPU denominator uses actual worker observation interval");
        Check(root.GetProperty("allocBytesPerSec").GetInt64() == 100, "allocation denominator uses actual worker observation interval");
        var metadata = root.GetProperty("msiCostWindowDeferred");
        Check(metadata.GetProperty("runtimeWindowMs").GetInt64() == 2000, "worker sampling interval explicit");
        Check(root.GetProperty("usbipTransport").ValueKind == JsonValueKind.Null, "no source means no passive SDK read");
    }

    private static void GateAndUnavailable()
    {
        var nonMsi = new CurrentCostHost(false, null); nonMsi.Enabled(false); nonMsi.DueFrame();
        Check(nonMsi.FramesRemaining == 0, "non-MSI disabled behavior preserved");
        var unavailable = new CurrentCostHost(true, null); unavailable.DueFrame();
        Check(unavailable.FramesRemaining == 0 && unavailable.SamplesRemaining == 0, "missing MSI dispatcher never falls back to caller IO");
    }

    private static int Main(string[] args)
    {
        try
        {
            Group("legacy synchronous due-window reproduces response delay", LegacyBeforeAfter);
            foreach (var boundary in new[] { "flag", "runtime", "sdk", "append" }) Group("actual MSI cost method blocked " + boundary, () => BlockedBoundary(boundary));
            Group("immutable window identities and counters", ImmutableWindow);
            Group("released queued source is not observed", () => EndedGeneration(false));
            Group("in-flight ended source is not promoted", () => EndedGeneration(true));
            Group("log flag and callback failure", LoggingAndFailure);
            Group("worker runtime interval semantics", RuntimeRates);
            Group("family gate and worker unavailable", GateAndUnavailable);
            var receipt = new { passed = true, groups = _groups, checks = _checks,
                actualCostMethodsExtracted = true, legacyFailureReproduced = true,
                realSdkAssembliesLoaded = AppDomain.CurrentDomain.GetAssemblies().Count(a => a.GetName().Name?.StartsWith("HIDMaestro", StringComparison.Ordinal) == true),
                productExecuted = false, liveDevices = false, serviceRegistryDriverOperations = false };
            if (args.Length == 1) File.WriteAllText(args[0], JsonSerializer.Serialize(receipt, new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine(JsonSerializer.Serialize(receipt)); return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}