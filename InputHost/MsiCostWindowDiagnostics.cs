using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace YeManInputHost.Diagnostics;

// Only the existing Native known-MSI + virtual-log launch flag creates this
// helper. The command thread transfers bounded immutable counters; all process
// queries, approved passive SDK reads, serialization and file I/O run here.
// This worker cannot originate a pipe, target, mode, HidHide or recovery action.
internal sealed class MsiCostWindowDiagnostics : IDisposable
{
    internal sealed record Window(long EpochMs, long CapturedTick, long WindowMs,
        long Frames, long[] SubmitUs, long JsonSumUs, long JsonCount,
        long HidSumUs, long HidCount, long ReadSumUs, long ReadCount,
        long Rejected, long HidHideFull, long HidHideState,
        KeyValuePair<string, int>[] Roles, string Phase, string HostInstanceId,
        string? RunId, ulong Epoch, ulong PowerGeneration, string? Persona,
        string? ProfileIdentity, ulong Sequence, long ControllerGeneration,
        WeakReference<object>? Controller,
        Func<object?, bool, object?>? ReadApprovedSdk, bool AssetVerified)
    {
        internal object? SteamDeckPadSubmissions { get; init; }
    }

    internal sealed record ThreadCpu(int Id, double TotalMs);
    internal sealed record RuntimeSample(long Tick, double ProcCpuMs,
        long AllocBytes, double GcPauseMs, long HeapBytes,
        int Gen0, int Gen1, int Gen2, ThreadCpu[] Threads, uint ObserverNativeThreadId = 0);

    private readonly MsiCostWindowDispatcher<Window> _dispatcher;
    private readonly Func<bool> _readFlag;
    private readonly Func<RuntimeSample> _readRuntime;
    private readonly Action<string> _append;
    private readonly HostCpuCostWindow _cpuWindow = new();
    private RuntimeScope? _previousScope;
    private readonly record struct RuntimeScope(string HostInstanceId, string? RunId,
        ulong Epoch, ulong PowerGeneration, string? Persona, long ControllerGeneration);
    // Match the native diagnostic continuity boundary. This is a reporting limit,
    // never a timer, device timeout or recovery decision.
    internal const long MaximumContinuousRuntimeGapMs = 3000;
    private RuntimeSample? _previousRuntime;
    private int _loggingEnabled;
    private long _controllerGeneration;
    internal const int DiagnosticRecordMaxBytes = 16 * 1024;
    private long _oversizedWindows, _summaryOnlyWindows, _failedWindows;
    private const string LogBudgetPolicy = "VC-MSI-COST-LOG-BUDGET-20261024-R1";

    internal MsiCostWindowDiagnostics(bool initiallyEnabled, string stateDir)
        : this(initiallyEnabled,
            () => ReadFlag(stateDir), ReadRuntime,
            line =>
            {
                HostCostLog.Append(stateDir, line);
            }) { }

    // Pure/fake callers inject every external boundary. These delegates are
    // never invoked by TryRecord, Dispose or controller-generation invalidation.
    internal MsiCostWindowDiagnostics(bool initiallyEnabled, Func<bool> readFlag,
        Func<RuntimeSample> readRuntime, Action<string> append)
    {
        _loggingEnabled = initiallyEnabled ? 1 : 0;
        _readFlag = readFlag;
        _readRuntime = readRuntime;
        _append = append;
        _dispatcher = new(Consume, "YMCC MSI cost diagnostic");
    }

    internal bool LoggingEnabled => Volatile.Read(ref _loggingEnabled) != 0;
    internal long ControllerGeneration => Interlocked.Read(ref _controllerGeneration);
    internal void InvalidateController() => Interlocked.Increment(ref _controllerGeneration);
    internal bool TryRejectBeforeSnapshot() => _dispatcher.TryRejectBeforeSnapshot();
    internal bool TryRecord(Window window) => _dispatcher.TryPost(window);
    internal MsiCostWindowDispatcher<Window>.Counters QueueCounters => _dispatcher.SnapshotCounters();
    public void Dispose() => _dispatcher.Dispose();

    private static bool ReadFlag(string stateDir)
    {
        try
        {
            var flag = Path.Combine(stateDir, "input-host-cost-enabled.flag");
            if (!File.Exists(flag)) return false;
            var value = File.ReadAllText(flag).Trim();
            return value.Equals("enabled", StringComparison.OrdinalIgnoreCase) || value == "1";
        }
        catch { return false; }
    }

    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern uint GetCurrentThreadId();

    private static RuntimeSample ReadRuntime()
    {
        // This is the executing sampler, not a command/SDK role captured earlier.
        var observerThreadId = GetCurrentThreadId();
        using var process = Process.GetCurrentProcess();
        var threads = new List<ThreadCpu>();
        foreach (ProcessThread thread in process.Threads)
        {
            using (thread)
            {
                try { threads.Add(new(thread.Id, thread.TotalProcessorTime.TotalMilliseconds)); }
                catch { /* A vanished thread is unavailable, not zero CPU. */ }
            }
        }
        return new(Environment.TickCount64, process.TotalProcessorTime.TotalMilliseconds,
            GC.GetTotalAllocatedBytes(precise: false), GC.GetTotalPauseDuration().TotalMilliseconds,
            GC.GetTotalMemory(false), GC.CollectionCount(0), GC.CollectionCount(1),
            GC.CollectionCount(2), threads.ToArray(), observerThreadId);
    }

    private object? ObserveApprovedSdk(Window window)
    {
        if (window.Persona != "steamdeck" || !window.AssetVerified ||
            window.Controller is null || window.ReadApprovedSdk is null) return null;
        var currentBefore = window.ControllerGeneration == ControllerGeneration;
        if (!currentBefore || !window.Controller.TryGetTarget(out var controller))
            return new { state = "omitted", reason = "controller-generation-ended-or-reference-unavailable" };
        var snapshot = window.ReadApprovedSdk(controller, true);
        var currentAfter = window.ControllerGeneration == ControllerGeneration;
        return new
        {
            version = 1, phase = "cost-window-deferred", capturedAtUtc = DateTimeOffset.UtcNow.ToString("O"),
            hostPid = Environment.ProcessId, hostInstanceId = window.HostInstanceId,
            runId = window.RunId, epoch = window.Epoch, powerGeneration = window.PowerGeneration,
            persona = window.Persona, profileIdentity = window.ProfileIdentity,
            lastHostCommandSequence = window.Sequence,
            controllerGenerationCurrentBefore = currentBefore,
            controllerGenerationCurrentAfter = currentAfter,
            scope = "Deferred passive SDK observation for the immutable requested window; no current request, ownership, release or Steam acceptance proof.",
            snapshot = currentAfter ? snapshot : null,
            omission = currentAfter ? null : "controller-generation-ended-during-observation"
        };
    }

    private static long ElapsedMicroseconds(long startTimestamp, long endTimestamp)
    {
        if (endTimestamp <= startTimestamp) return 0;
        return (long)Math.Max(0,
            (endTimestamp - startTimestamp) * 1_000_000.0 / Stopwatch.Frequency);
    }

    private void ResetRuntimeBaseline()
    {
        _previousRuntime = null;
        _previousScope = null;
        _cpuWindow.Reset();
    }

    private void Consume(Window window)
    {
        var stage = "flag-read";
        try { ConsumeWindow(window, ref stage); }
        catch (Exception error)
        {
            // This receipt is diagnostic only. Keep the dispatcher's failure and
            // baseline reset; do not turn a failed observation into measured CPU.
            ResetRuntimeBaseline();
            ++_failedWindows;
            if (LoggingEnabled)
            {
                try { _append(SummaryRecord(window, "cost-window-failed", stage, 0, error)); }
                catch { /* A failed writer cannot prove that its own receipt was saved. */ }
            }
            throw; // The existing dispatcher records the failure and keeps running.
        }
    }

    private void ConsumeWindow(Window window, ref string stage)
    {
        var workerStartedTick = Environment.TickCount64;
        var workerStartedTimestamp = Stopwatch.GetTimestamp();
        var enabled = _readFlag();
        var flagReadReturnedTick = Environment.TickCount64;
        var flagReadReturnedTimestamp = Stopwatch.GetTimestamp();
        Volatile.Write(ref _loggingEnabled, enabled ? 1 : 0);
        if (!enabled)
        {
            ResetRuntimeBaseline();
            return;
        }
        var runtimeControllerGenerationBefore = ControllerGeneration;
        stage = "runtime-read";
        var runtime = _readRuntime();
        var runtimeControllerGenerationAfter = ControllerGeneration;
        var runtimeControllerGenerationCurrent =
            window.ControllerGeneration == runtimeControllerGenerationBefore &&
            window.ControllerGeneration == runtimeControllerGenerationAfter;
        var runtimeReadReturnedTick = Environment.TickCount64;
        var runtimeReadReturnedTimestamp = Stopwatch.GetTimestamp();
        var scope = new RuntimeScope(window.HostInstanceId, window.RunId, window.Epoch,
            window.PowerGeneration, window.Persona, window.ControllerGeneration);
        var previous = _previousRuntime;
        var runtimeWindowMs = previous is null ? 0 : runtime.Tick - previous.Tick;
        var continuityState = !runtimeControllerGenerationCurrent ? "controller-generation-ended" :
            previous is null ? "baseline" :
            _previousScope != scope ? "scope-changed" :
            runtimeWindowMs <= 0 ? "invalid-window" :
            runtimeWindowMs > MaximumContinuousRuntimeGapMs ? "observation-gap" : "continuous";
        if (continuityState != "continuous")
        {
            _cpuWindow.Reset();
            previous = null;
        }
        var runtimeDeltaKnown = previous is not null;
        var processCpu = _cpuWindow.ObserveProcess(runtime.ProcCpuMs, runtime.Tick);
        var byThread = new List<(int Id, long Rate, string Role)>();
        var threadRatesKnown = 0;
        var threadBaselines = 0;
        var threadOtherUnknown = 0;
        _cpuWindow.BeginThreadObservation();
        foreach (var thread in runtime.Threads)
        {
            var measured = _cpuWindow.ObserveThread(thread.Id, thread.TotalMs, runtime.Tick);
            if (measured.MsPerSec is long rate)
            {
                threadRatesKnown++;
                if (rate <= 0) continue; // Known idle threads need no role lookup or top-four entry.
                // A known current sampler identity takes precedence over a stale caller role.
                var role = runtime.ObserverNativeThreadId != 0 &&
                    unchecked((uint)thread.Id) == runtime.ObserverNativeThreadId
                    ? "msi-cost-diagnostic"
                    : window.Roles.FirstOrDefault(r => r.Value == thread.Id).Key ?? "unlabeled";
                byThread.Add((thread.Id, rate, role));
            }
            else if (measured.Status == "baseline") threadBaselines++;
            else threadOtherUnknown++;
        }
        _cpuWindow.FinishThreadObservation();
        if (runtimeControllerGenerationCurrent)
        {
            _previousRuntime = runtime;
            _previousScope = scope;
        }
        else
        {
            // An old queued window cannot seed a later valid controller window.
            // Only this worker owns baseline state; invalidation remains atomic-only.
            ResetRuntimeBaseline();
        }
        byThread.Sort((a, b) => b.Rate.CompareTo(a.Rate));
        var samples = window.SubmitUs;
        Array.Sort(samples); // This array is owned by the immutable work item.
        long Percentile(double p) => samples.Length == 0 ? 0 :
            samples[Math.Min(samples.Length - 1, (int)(samples.Length * p))];
        long? Rate(long delta) => runtimeDeltaKnown && delta >= 0 ?
            (long)(delta * 1000.0 / runtimeWindowMs) : null;
        double? CpuRate(double delta) => runtimeDeltaKnown && delta >= 0 ?
            Math.Round(delta * 1000.0 / runtimeWindowMs) : null;
        var sdkReadStartedTick = Environment.TickCount64;
        var sdkReadStartedTimestamp = Stopwatch.GetTimestamp();
        stage = "sdk-read";
        var observation = ObserveApprovedSdk(window);
        var sdkReadReturnedTick = Environment.TickCount64;
        var sdkReadReturnedTimestamp = Stopwatch.GetTimestamp();
        stage = "serialize";
        var line = JsonSerializer.Serialize(new
        {
            // Use the captured window's session identity, never the worker's current controller/session.
            writerPid = Environment.ProcessId, hostInstanceId = window.HostInstanceId,
            runId = window.RunId ?? "", epoch = window.Epoch, powerGeneration = window.PowerGeneration,
            t = window.EpochMs, windowMs = window.WindowMs, frames = window.Frames,
            submitElapsedUs = new { p50 = Percentile(0.50), p95 = Percentile(0.95), max = samples.Length == 0 ? 0 : samples[^1] },
            jsonParseUs = new { n = window.JsonCount, mean = window.JsonCount == 0 ? 0 : window.JsonSumUs / window.JsonCount },
            hidSubmitUs = new { n = window.HidCount, mean = window.HidCount == 0 ? 0 : window.HidSumUs / window.HidCount },
            pipeReadUs = new { n = window.ReadCount, mean = window.ReadCount == 0 ? 0 : window.ReadSumUs / window.ReadCount },
            framesRejected = window.Rejected,
            gcPauseMsPerSec = previous is null ? null : CpuRate(runtime.GcPauseMs - previous.GcPauseMs),
            allocBytesPerSec = previous is null ? null : Rate(runtime.AllocBytes - previous.AllocBytes),
            procCpuMsPerSec = processCpu.MsPerSec,
            cpuObservation = new
            {
                version = 1, state = processCpu.Status,
                observationTick = processCpu.ObservationTick,
                runtimeWindowMs = processCpu.WindowMs,
                continuityState, previousObservationGapMs = runtimeWindowMs,
                maximumContinuousGapMs = MaximumContinuousRuntimeGapMs,
                threadRatesKnown, threadBaselines, threadOtherUnknown
            },
            // Runtime samples happen on the worker; they are not attributed to
            // a particular frame or used to infer an allocation-per-frame value.
            allocPerFrame = (long?)null,
            hidhideQuery = new { full = window.HidHideFull, state = window.HidHideState },
            byThread = byThread.Take(4).Select(r => new { tid = r.Id, cpuMsPerSec = r.Rate, role = r.Role }).ToArray(),
            threads = runtime.Threads.Length, gcHeapBytes = runtime.HeapBytes,
            gen0 = runtime.Gen0, gen1 = runtime.Gen1, gen2 = runtime.Gen2,
            phase = window.Phase, usbipTransport = observation,
            steamDeckPadSubmissions = window.SteamDeckPadSubmissions,
            logRetention = HostCostLog.Snapshot(),
            msiCostWindowDeferred = new
            {
                version = 1, logBudgetPolicy = LogBudgetPolicy, recordMaxBytes = DiagnosticRecordMaxBytes,
                hostInstanceId = window.HostInstanceId,
                runId = window.RunId, epoch = window.Epoch, powerGeneration = window.PowerGeneration,
                requestedSequence = window.Sequence, controllerGeneration = window.ControllerGeneration,
                requestedTick = window.CapturedTick, workerStartedTick,
                queueDelayMs = Math.Max(0, workerStartedTick - window.CapturedTick),
                flagReadMs = Math.Max(0, flagReadReturnedTick - workerStartedTick),
                runtimeReadMs = Math.Max(0, runtimeReadReturnedTick - flagReadReturnedTick),
                sdkReadMs = Math.Max(0, sdkReadReturnedTick - sdkReadStartedTick),
                stageTimingUs = new
                {
                    version = 1,
                    flagRead = ElapsedMicroseconds(workerStartedTimestamp, flagReadReturnedTimestamp),
                    runtimeRead = ElapsedMicroseconds(flagReadReturnedTimestamp, runtimeReadReturnedTimestamp),
                    sdkRead = ElapsedMicroseconds(sdkReadStartedTimestamp, sdkReadReturnedTimestamp)
                },
                emittedAtUtc = DateTimeOffset.UtcNow.ToString("O"),
                runtimeObservationTick = runtime.Tick, runtimeWindowMs, runtimeDeltaKnown,
                runtimeControllerGenerationBefore, runtimeControllerGenerationAfter,
                runtimeControllerGenerationCurrent,
                runtimeObserverNativeThreadId = runtime.ObserverNativeThreadId == 0
                    ? (uint?)null : runtime.ObserverNativeThreadId,
                queue = _dispatcher.SnapshotCounters(), queueSnapshotAtomic = false,
                scope = "Bounded diagnostic windows; dropped/discarded windows are not recovered. No response, controller freshness or consumer proof.",
                controllerWrites = false, pipeWrites = false
            }
        });
        stage = "record-budget";
        line = ApplyRecordBudget(window, line);
        stage = "append";
        _append(line);
    }

    // Runs on the existing opt-in diagnostic worker, at most once per cost
    // window. Never retries a frame, calls an SDK writer, or raises the budget.
    private string ApplyRecordBudget(Window window, string line)
    {
        var originalBytes = Encoding.UTF8.GetByteCount(line);
        if (originalBytes <= DiagnosticRecordMaxBytes) return line;
        ++_oversizedWindows;
        using var original = JsonDocument.Parse(line);
        using var buffer = new MemoryStream();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            foreach (var property in original.RootElement.EnumerateObject())
            {
                // Keep the identity, frames, CPU baseline/rates, queue counters
                // and timings. These two passive snapshots contain the large rings.
                if (property.Name is not ("steamDeckPadSubmissions" or "usbipTransport"))
                    property.WriteTo(writer);
                else
                    writer.WriteNull(property.Name); // Preserve the existing nullable schema; not a zero observation.
            }
            writer.WritePropertyName("diagnosticBudget");
            JsonSerializer.Serialize(writer, new
            {
                schemaVersion = 1, policy = LogBudgetPolicy,
                state = "bulk-evidence-omitted", originalRecordBytes = originalBytes,
                recordMaxBytes = DiagnosticRecordMaxBytes,
                oversizedWindows = _oversizedWindows, summaryOnlyWindows = _summaryOnlyWindows,
                failedWindows = _failedWindows,
                omittedFields = new[] { "steamDeckPadSubmissions", "usbipTransport" },
                completePayloadRetained = false,
                scope = "Diagnostic payload omitted; no input frames dropped by this budget. SDK/driver/game delivery unproven."
            });
            if (original.RootElement.TryGetProperty("steamDeckPadSubmissions", out var audit) &&
                audit.ValueKind == JsonValueKind.Object)
            {
                writer.WritePropertyName("steamDeckPadSubmissionSummary");
                writer.WriteStartObject();
                foreach (var name in new[] { "Version", "TargetGeneration", "Reports", "Edges",
                    "TouchChanges", "ClickChanges", "InvalidFrames", "TouchMismatch", "ClickMismatch",
                    "CoordinateMismatch", "PressureMismatch", "CounterDiscontinuities", "ObservationErrors",
                    "OmittedRows", "TransferCompletedProven", "SteamConsumptionProven" })
                {
                    if (!audit.TryGetProperty(name, out var value) ||
                        value.ValueKind is not (JsonValueKind.Number or JsonValueKind.True or JsonValueKind.False)) continue;
                    writer.WritePropertyName(name); value.WriteTo(writer);
                }
                if (audit.TryGetProperty("Entries", out var entries) && entries.ValueKind == JsonValueKind.Array)
                    writer.WriteNumber("entriesOmittedByRecordBudget", entries.GetArrayLength());
                writer.WriteEndObject();
            }
            writer.WriteEndObject();
        }
        if (buffer.Length <= DiagnosticRecordMaxBytes)
            return Encoding.UTF8.GetString(buffer.GetBuffer(), 0, (int)buffer.Length);
        // Unexpectedly large non-SDK metadata still gets an explicitly partial,
        // bounded identity receipt; it must not silently remove every later window.
        ++_summaryOnlyWindows;
        return SummaryRecord(window, "cost-window-summary", "non-bulk-metadata-over-budget", originalBytes);
    }

    private string SummaryRecord(Window window, string kind, string reason, int originalBytes,
        Exception? error = null)
    {
        static string Bounded(string? value, int limit) =>
            value is null ? "" : value.Length <= limit ? value : value[..limit];
        return JsonSerializer.Serialize(new
        {
            t = window.EpochMs, writerPid = Environment.ProcessId, kind,
            hostInstanceId = Bounded(window.HostInstanceId, 64), runId = Bounded(window.RunId, 64),
            epoch = window.Epoch, powerGeneration = window.PowerGeneration,
            persona = Bounded(window.Persona, 64), profileIdentity = Bounded(window.ProfileIdentity, 96),
            requestedSequence = window.Sequence, controllerGeneration = window.ControllerGeneration,
            requestedTick = window.CapturedTick, windowMs = window.WindowMs,
            capturedFrames = window.Frames, capturedFramesRejected = window.Rejected,
            diagnosticBudget = new
            {
                schemaVersion = 1, policy = LogBudgetPolicy, state = kind, reason,
                originalRecordBytes = originalBytes == 0 ? (int?)null : originalBytes,
                recordMaxBytes = DiagnosticRecordMaxBytes, oversizedWindows = _oversizedWindows,
                summaryOnlyWindows = _summaryOnlyWindows, failedWindows = _failedWindows,
                errorType = error is null ? null : Bounded(error.GetType().Name, 96),
                errorHResult = error?.HResult, completePayloadRetained = false,
                identityTextMayBeTruncated = true,
                scope = "Captured diagnostic window only; omitted observations are unknown, not zero or successful delivery."
            }
        });
    }
}