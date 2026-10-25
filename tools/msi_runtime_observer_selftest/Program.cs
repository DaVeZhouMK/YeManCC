using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using YeManInputHost.Diagnostics;

internal static class Program
{
    private static int _checks, _groups;
    [DllImport("kernel32.dll", ExactSpelling = true)] private static extern uint GetCurrentThreadId();
    private static void Check(bool value, string why) { _checks++; if (!value) throw new Exception(why); }
    private static void Until(Func<bool> condition, string why) => Check(SpinWait.SpinUntil(condition, 3000), why);
    private static void Group(string name, Action test) { test(); _groups++; Console.WriteLine("PASS " + name); }
    private static MsiCostWindowDiagnostics.Window Window(ulong sequence = 1) => new(
        EpochMs: 1, CapturedTick: 1000, WindowMs: 1000, Frames: 1, SubmitUs: new long[] { 4 },
        JsonSumUs: 3, JsonCount: 1, HidSumUs: 2, HidCount: 1, ReadSumUs: 1, ReadCount: 1,
        Rejected: 0, HidHideFull: 0, HidHideState: 0,
        Roles: new[] { new KeyValuePair<string, int>("pipe-read", 7) }, Phase: "Active", HostInstanceId: "fixture-only",
        RunId: "fixture-run", Epoch: 2, PowerGeneration: 3, Persona: "steamdeck", ProfileIdentity: "fixture",
        Sequence: sequence, ControllerGeneration: 0, Controller: null, ReadApprovedSdk: null, AssetVerified: false);
    private static MsiCostWindowDiagnostics.RuntimeSample Sample(long tick, double cpu, int thread, uint observer) =>
        new(tick, cpu, tick, 0, 1234, 1, 0, 0,
            new[] { new MsiCostWindowDiagnostics.ThreadCpu(thread, cpu) }, observer);
    private static JsonDocument Pair(int thread, uint observer)
    {
        var rows = new ConcurrentQueue<string>(); int ordinal = 0;
        using var diagnostics = new MsiCostWindowDiagnostics(true, () => true,
            () => Interlocked.Increment(ref ordinal) == 1 ? Sample(1000, 2, thread, observer) : Sample(2000, 22, thread, observer), rows.Enqueue);
        Check(diagnostics.TryRecord(Window()), "first window admitted");
        Until(() => diagnostics.QueueCounters.Completed == 1, "first window completed");
        Check(diagnostics.TryRecord(Window(2)), "second window admitted");
        Until(() => diagnostics.QueueCounters.Completed == 2, "second window completed");
        diagnostics.Dispose(); Until(() => diagnostics.QueueCounters.Stopped, "sampler actually stopped");
        Check(rows.Count == 2, "one row per window");
        using var first = JsonDocument.Parse(rows.First());
        Check(first.RootElement.GetProperty("procCpuMsPerSec").ValueKind == JsonValueKind.Null, "baseline CPU unavailable stays null");
        return JsonDocument.Parse(rows.Last());
    }
    private static void Role(int thread, uint observer, string expected)
    {
        using var doc = Pair(thread, observer); var root = doc.RootElement;
        var item = root.GetProperty("byThread")[0];
        Check(item.GetProperty("role").GetString() == expected, "role matches observer binding");
        Check(item.GetProperty("tid").GetInt32() == thread, "existing thread ID preserved");
        Check(item.GetProperty("cpuMsPerSec").GetInt64() == 20, "thread CPU rate unchanged");
        Check(root.GetProperty("procCpuMsPerSec").GetDouble() == 20, "process CPU rate unchanged");
        Check(root.GetProperty("allocBytesPerSec").GetInt64() == 1000, "allocation denominator unchanged");
        Check(root.GetProperty("allocPerFrame").ValueKind == JsonValueKind.Null, "no per-frame attribution fabricated");
        var metadata = root.GetProperty("msiCostWindowDeferred");
        if (observer == 0) Check(metadata.GetProperty("runtimeObserverNativeThreadId").ValueKind == JsonValueKind.Null, "unknown observer stays null");
        else Check(metadata.GetProperty("runtimeObserverNativeThreadId").GetUInt32() == observer, "all observer DWORD bits retained");
        Check(!metadata.GetProperty("pipeWrites").GetBoolean() && !metadata.GetProperty("controllerWrites").GetBoolean(), "diagnostic-only authority unchanged");
    }
    private static void Disabled()
    {
        int calls = 0; var rows = new ConcurrentQueue<string>();
        using var diagnostics = new MsiCostWindowDiagnostics(true, () => false,
            () => { Interlocked.Increment(ref calls); return Sample(1000, 2, 7, 7); }, rows.Enqueue);
        Check(diagnostics.TryRecord(Window()), "disabled window queued");
        Until(() => diagnostics.QueueCounters.Completed == 1, "disabled flag read completed");
        diagnostics.Dispose(); Until(() => diagnostics.QueueCounters.Stopped, "disabled observer stopped");
        Check(calls == 0 && rows.IsEmpty, "disabled flag does not read runtime or emit a row");
    }
    private static void RealSelfSampler()
    {
        var method = typeof(MsiCostWindowDiagnostics).GetMethod("ReadRuntime", BindingFlags.NonPublic | BindingFlags.Static)!;
        MsiCostWindowDiagnostics.RuntimeSample? actual = null;
        var rows = new ConcurrentQueue<string>(); uint appendThread = 0;
        var callerThread = GetCurrentThreadId();
        using var diagnostics = new MsiCostWindowDiagnostics(true, () => true,
            () => actual = (MsiCostWindowDiagnostics.RuntimeSample)method.Invoke(null, null)!,
            row => { appendThread = GetCurrentThreadId(); rows.Enqueue(row); });
        Check(diagnostics.TryRecord(Window()), "self-process sample queued");
        Until(() => diagnostics.QueueCounters.Completed == 1, "real self-process sample completed");
        diagnostics.Dispose(); Until(() => diagnostics.QueueCounters.Stopped, "real self-process worker stopped");
        Check(actual is not null && actual.ObserverNativeThreadId != 0, "real runtime observer known");
        Check(actual!.ObserverNativeThreadId == appendThread && appendThread != callerThread, "identity is executing diagnostic worker, not caller");
        Check(actual.Threads.Any(t => unchecked((uint)t.Id) == appendThread), "worker identity present in actual process thread sample");
        using var doc = JsonDocument.Parse(rows.Single());
        Check(doc.RootElement.GetProperty("msiCostWindowDeferred").GetProperty("runtimeObserverNativeThreadId").GetUInt32() == appendThread, "real observer identity retained in emitted row");
        Check(doc.RootElement.GetProperty("usbipTransport").ValueKind == JsonValueKind.Null, "no controller/SDK source used");
        Check(System.Text.Encoding.UTF8.GetByteCount(rows.Single()) <= 16384, "existing row budget retained");
    }
    private static int Main(string[] args)
    {
        try
        {
            Group("current sampler overrides captured caller role", () => Role(7, 7, "msi-cost-diagnostic"));
            Group("other sampled thread retains captured role", () => Role(7, 8, "pipe-read"));
            Group("unknown sampler does not guess a role", () => Role(7, 0, "pipe-read"));
            Group("unmatched non-observer stays unlabeled", () => Role(9, 8, "unlabeled"));
            Group("DWORD native identity high bits preserved", () => Role(unchecked((int)0xFFFFFFF0u), 0xFFFFFFF0u, "msi-cost-diagnostic"));
            Group("disabled diagnostics have no runtime side effect", Disabled);
            Group("actual runtime sampler queries this fixture process only", RealSelfSampler);
            var result = new { pass = true, groups = _groups, checks = _checks,
                productionHelpersCompiledDirectly = true, realRuntimeQueriesSelfProcessOnly = true,
                productExecuted = false, liveDevices = false, serviceRegistryDriverOperations = false,
                sdkAssembliesLoaded = AppDomain.CurrentDomain.GetAssemblies().Count(a => a.GetName().Name?.StartsWith("HIDMaestro", StringComparison.Ordinal) == true) };
            if (args.Length == 1) File.WriteAllText(args[0], JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine(JsonSerializer.Serialize(result)); return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
