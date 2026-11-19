using System.Diagnostics;
using System.Globalization;
using Microsoft.Win32.SafeHandles;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace YeManInputHost.Diagnostics;

// Shared by Host sessions; historical MSI type/receipt names are retained.
// The existing Native detailed-log flag controls observation and output.
// The command thread transfers bounded immutable counters; all process
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
    internal sealed record ThreadIdentity(string State, string? BirthFileTime = null,
        string? Description = null, bool DescriptionTruncated = false,
        uint? OwnerPid = null, int? Win32Error = null, int? DescriptionHResult = null,
        long ObservationTick = 0);
    internal const int ThreadIdentityQueryLimit = 4;
    internal const int ThreadDescriptionMaxChars = 256;
    internal sealed record RuntimeSample(long Tick, double ProcCpuMs,
        long AllocBytes, double GcPauseMs, long HeapBytes,
        int Gen0, int Gen1, int Gen2, ThreadCpu[] Threads, uint ObserverNativeThreadId = 0,
        string? WriterBirthFileTime = null);

    // Cost and signal diagnostics share the ORIGINAL one-active/one-pending slot.
    // A signal item owns only immutable text/path; never a controller/session callback.
    private readonly record struct SignalLogRecord(string Directory, string Text);
    private readonly record struct WorkItem(Window? CostWindow, SignalLogRecord? SignalLog);
    private readonly MsiCostWindowDispatcher<WorkItem> _dispatcher;
    private readonly Func<bool> _readFlag;
    private readonly Func<RuntimeSample> _readRuntime;
    private readonly Func<int, ThreadIdentity>? _readThreadIdentity;
    private readonly Action<byte[]> _append;
    private readonly string? _signalLogDirectory;
    private readonly Action<string, string>? _writeSignalLog;
    internal const int SignalLogRecordMaxBytes = 8192;
    private long _signalOffered, _signalAccepted, _signalRejected, _signalInvalid;
    private long _signalWritten, _signalFailed, _signalFlagDisabled;
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

    internal MsiCostWindowDiagnostics(bool initiallyEnabled, string stateDir,
        Action<string, string>? writeSignalLog = null)
        : this(initiallyEnabled,
            () => ReadFlag(stateDir), ReadRuntime,
            (byte[] line) =>
            {
                if (!HostCostLog.AppendUtf8(stateDir, line))
                    throw new System.IO.IOException("Cost diagnostic record append was rejected.");
            }, stateDir, writeSignalLog, ReadThreadIdentity) { }

    // Pure/fake callers inject every external boundary. These delegates are
    // never invoked by TryRecord, Dispose or controller-generation invalidation.
    internal MsiCostWindowDiagnostics(bool initiallyEnabled, Func<bool> readFlag,
        Func<RuntimeSample> readRuntime, Action<string> append,
        string? signalLogDirectory = null, Action<string, string>? writeSignalLog = null,
        Func<int, ThreadIdentity>? readThreadIdentity = null)
        : this(initiallyEnabled, readFlag, readRuntime, (byte[] line) => append(Encoding.UTF8.GetString(line)),
            signalLogDirectory, writeSignalLog, readThreadIdentity) { }

    private MsiCostWindowDiagnostics(bool initiallyEnabled, Func<bool> readFlag,
        Func<RuntimeSample> readRuntime, Action<byte[]> append,
        string? signalLogDirectory, Action<string, string>? writeSignalLog,
        Func<int, ThreadIdentity>? readThreadIdentity)
    {
        _loggingEnabled = initiallyEnabled ? 1 : 0;
        _readFlag = readFlag;
        _readRuntime = readRuntime;
        _readThreadIdentity = readThreadIdentity;
        _append = append;
        _signalLogDirectory = signalLogDirectory;
        _writeSignalLog = writeSignalLog;
        _dispatcher = new(ConsumeWorkItem, "YMCC MSI cost diagnostic");
    }

    internal bool LoggingEnabled => Volatile.Read(ref _loggingEnabled) != 0;
    internal long ControllerGeneration => Interlocked.Read(ref _controllerGeneration);
    internal void InvalidateController() => Interlocked.Increment(ref _controllerGeneration);
    internal bool TryRejectBeforeSnapshot() => _dispatcher.TryRejectBeforeSnapshot();
    internal bool TryRecord(Window window) => _dispatcher.TryPost(new(window, null));
    // Preserve the existing public counter type and JSON member names for consumers.
    internal MsiCostWindowDispatcher<Window>.Counters QueueCounters
    {
        get
        {
            var value = _dispatcher.SnapshotCounters();
            return new(value.Accepted, value.Dropped, value.Completed, value.Failed,
                value.Discarded, value.Stopped, value.DroppedBusy, value.DroppedFull, value.DroppedClosed);
        }
    }
    public void Dispose() => _dispatcher.Dispose();

    // Caller-side admission is bounded memory/atomic work only. No flag/file read,
    // sink callback, retry, replacement worker or synchronous fallback is allowed.
    internal bool TryWriteSignalLog(string line)
    {
        Interlocked.Increment(ref _signalOffered);
        if (_writeSignalLog is null || string.IsNullOrEmpty(_signalLogDirectory) ||
            string.IsNullOrEmpty(line) || line.Length >= SignalLogRecordMaxBytes ||
            line.IndexOf('\r') >= 0 || line.IndexOf('\n') >= 0 ||
            Encoding.UTF8.GetByteCount(line) + 1 > SignalLogRecordMaxBytes)
        {
            Interlocked.Increment(ref _signalInvalid);
            return false;
        }
        if (_dispatcher.TryPost(new(null, new SignalLogRecord(_signalLogDirectory, line))))
        {
            Interlocked.Increment(ref _signalAccepted);
            return true; // Admission only: the worker may fail or close with this pending.
        }
        Interlocked.Increment(ref _signalRejected);
        return false;
    }

    // Read-only, nontransactional diagnostics. An unfinished accepted row is NOT
    // a write receipt. Only actual worker termination permits discard reconciliation;
    // final totals also require admission callers to have returned.
    internal object SnapshotSignalLogCounters() => SnapshotSignalLogCounters(includeScope: true);

    private object SnapshotSignalLogCounters(bool includeScope)
    {
        var queue = _dispatcher.SnapshotCounters();
        var offered = Interlocked.Read(ref _signalOffered);
        var accepted = Interlocked.Read(ref _signalAccepted);
        var rejected = Interlocked.Read(ref _signalRejected);
        var invalid = Interlocked.Read(ref _signalInvalid);
        var written = Interlocked.Read(ref _signalWritten);
        var failed = Interlocked.Read(ref _signalFailed);
        var flagDisabled = Interlocked.Read(ref _signalFlagDisabled);
        var admissionInProgress = Math.Max(0, offered - accepted - rejected - invalid);
        var unfinished = Math.Max(0, accepted - written - failed - flagDisabled);
        var discardedOnClose = queue.Stopped && admissionInProgress == 0 ? (long?)unfinished : null;
        if (!includeScope)
        {
            return new
            {
                version = 2, offered, accepted, rejected, invalid, written, failed, flagDisabled,
                unfinished, admissionInProgress, discardedOnClose,
                workerStopped = queue.Stopped, snapshotAtomic = false,
                sharedQueue = queue, recordMaxBytes = SignalLogRecordMaxBytes
            };
        }
        return new
        {
            version = 1, offered, accepted, rejected, invalid, written, failed, flagDisabled,
            unfinished, admissionInProgress, discardedOnClose,
            workerStopped = queue.Stopped, snapshotAtomic = false,
            sharedQueue = queue, recordMaxBytes = SignalLogRecordMaxBytes,
            scope = "Admission is not file-write success; rejected/invalid rows are dropped. Unfinished includes active/pending or close-discarded rows. Final discard count requires stopped worker and quiescent producers; no retry or control proof."
        };
    }

    private void ConsumeWorkItem(WorkItem item)
    {
        if (item.CostWindow is { } window)
        {
            Consume(window);
            return;
        }
        if (item.SignalLog is not { } signal) return;
        try
        {
            // A signal can occupy the slot instead of a cost window. Observe OFF
            // on this same worker too; no extra timer/polling lane is introduced.
            var enabled = _readFlag();
            Volatile.Write(ref _loggingEnabled, enabled ? 1 : 0);
            if (!enabled)
            {
                ResetRuntimeBaseline();
                Interlocked.Increment(ref _signalFlagDisabled);
                return;
            }
            _writeSignalLog!(signal.Directory, signal.Text);
            Interlocked.Increment(ref _signalWritten);
        }
        catch
        {
            Interlocked.Increment(ref _signalFailed);
            throw; // Original dispatcher accounts failure and continues; never replay.
        }
    }

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

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeFileTime
    {
        public uint Low, High;
        public readonly ulong Value => ((ulong)High << 32) | Low;
    }

    [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
    private static extern SafeFileHandle OpenThread(uint access,
        [MarshalAs(UnmanagedType.Bool)] bool inherit, uint threadId);
    [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
    private static extern uint GetProcessIdOfThread(SafeFileHandle thread);
    [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetThreadTimes(SafeFileHandle thread,
        out NativeFileTime creation, out NativeFileTime exit,
        out NativeFileTime kernel, out NativeFileTime user);
    [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetProcessTimes(SafeProcessHandle process,
        out NativeFileTime creation, out NativeFileTime exit,
        out NativeFileTime kernel, out NativeFileTime user);
    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern int GetThreadDescription(SafeFileHandle thread, out nint description);
    [DllImport("kernel32.dll", ExactSpelling = true)]
    private static extern nint LocalFree(nint memory);

    // Current OS metadata on one held, query-only thread handle. The earlier
    // ProcessThread CPU sample and this metadata are NOT an atomic observation;
    // a description is not an execution stack, and TID reuse remains possible.
    internal static ThreadIdentity ReadThreadIdentity(int threadId)
    {
        var tick = Environment.TickCount64;
        if (threadId <= 0) return new("invalid-tid", ObservationTick: tick);
        try
        {
            const uint queryLimitedInformation = 0x0800;
            using var thread = OpenThread(queryLimitedInformation, false, (uint)threadId);
            if (thread.IsInvalid)
                return new("open-failed", Win32Error: Marshal.GetLastWin32Error(), ObservationTick: tick);
            var owner = GetProcessIdOfThread(thread);
            if (owner == 0)
                return new("owner-unavailable", Win32Error: Marshal.GetLastWin32Error(), ObservationTick: tick);
            if (owner != (uint)Environment.ProcessId)
                return new("owner-mismatch", OwnerPid: owner, ObservationTick: tick);
            if (!GetThreadTimes(thread, out var creation, out _, out _, out _))
                return new("birth-unavailable", OwnerPid: owner,
                    Win32Error: Marshal.GetLastWin32Error(), ObservationTick: tick);
            var birth = creation.Value.ToString(CultureInfo.InvariantCulture);
            nint buffer = 0;
            try
            {
                var hr = GetThreadDescription(thread, out buffer);
                string? description = null;
                var truncated = false;
                if (hr >= 0)
                {
                    var length = 0;
                    if (buffer != 0)
                    {
                        while (length < ThreadDescriptionMaxChars && Marshal.ReadInt16(buffer, length * 2) != 0) ++length;
                        truncated = length == ThreadDescriptionMaxChars && Marshal.ReadInt16(buffer, length * 2) != 0;
                        description = Marshal.PtrToStringUni(buffer, length);
                        if (truncated && description is { Length: > 0 } && char.IsHighSurrogate(description[^1]))
                            description = description[..^1];
                    }
                    else description = "";
                }
                return new("observed-current-thread", birth, description, truncated, owner,
                    DescriptionHResult: hr, ObservationTick: tick);
            }
            finally { if (buffer != 0) LocalFree(buffer); }
        }
        catch { return new("query-unavailable", ObservationTick: tick); }
    }

    private object ObserveThreadIdentity(int threadId)
    {
        ThreadIdentity value;
        try { value = _readThreadIdentity?.Invoke(threadId) ?? new("not-requested"); }
        catch { value = new("query-unavailable"); }
        var description = value.Description;
        var truncated = value.DescriptionTruncated;
        if (description is { Length: > ThreadDescriptionMaxChars })
        {
            description = description[..ThreadDescriptionMaxChars];
            if (char.IsHighSurrogate(description[^1])) description = description[..^1];
            truncated = true;
        }
        return new
        {
            state = value.State, birthFileTime = value.BirthFileTime,
            description, descriptionTruncated = truncated, ownerPid = value.OwnerPid,
            win32Error = value.Win32Error, descriptionHResult = value.DescriptionHResult,
            observationTick = value.ObservationTick
        };
    }

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
        string? writerBirth = null;
        try
        {
            if (GetProcessTimes(process.SafeHandle, out var creation, out _, out _, out _))
                writerBirth = creation.Value.ToString(CultureInfo.InvariantCulture);
        }
        catch { /* Process birth is supplementary; preserve the original CPU sample. */ }
        return new(Environment.TickCount64, process.TotalProcessorTime.TotalMilliseconds,
            GC.GetTotalAllocatedBytes(precise: false), GC.GetTotalPauseDuration().TotalMilliseconds,
            GC.GetTotalMemory(false), GC.CollectionCount(0), GC.CollectionCount(1),
            GC.CollectionCount(2), threads.ToArray(), observerThreadId, writerBirth);
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
        var identityReadStartedTimestamp = Stopwatch.GetTimestamp();
        // At most four queries on this original opt-in worker; no name cache,
        // retained thread handles, extra timer, or caller-side OS work.
        var topThreads = byThread.Take(ThreadIdentityQueryLimit).Select(r => new
        {
            tid = r.Id, cpuMsPerSec = r.Rate, role = r.Role,
            identity = ObserveThreadIdentity(r.Id)
        }).ToArray();
        var identityReadReturnedTimestamp = Stopwatch.GetTimestamp();
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
        var line = JsonSerializer.SerializeToUtf8Bytes(new
        {
            // Use the captured window's session identity, never the worker's current controller/session.
            writerPid = Environment.ProcessId, writerBirthFileTime = runtime.WriterBirthFileTime,
            hostInstanceId = window.HostInstanceId,
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
            byThread = topThreads,
            threadIdentityObservation = new
            {
                version = 2, queryLimit = ThreadIdentityQueryLimit,
                attempted = _readThreadIdentity is null ? 0 : topThreads.Length,
                descriptionMaxChars = ThreadDescriptionMaxChars, atomicWithCpuSample = false
            },
            threads = runtime.Threads.Length, gcHeapBytes = runtime.HeapBytes,
            gen0 = runtime.Gen0, gen1 = runtime.Gen1, gen2 = runtime.Gen2,
            phase = window.Phase, usbipTransport = observation,
            steamDeckPadSubmissions = window.SteamDeckPadSubmissions,
            logRetention = HostCostLog.Snapshot(),
            signalLogWriter = SnapshotSignalLogCounters(includeScope: false),
            msiCostWindowDeferred = new
            {
                version = 3, logBudgetPolicy = LogBudgetPolicy, recordMaxBytes = DiagnosticRecordMaxBytes,
                // The row root already contains the other three values from this captured window.
                runId = window.RunId,
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
                    sdkRead = ElapsedMicroseconds(sdkReadStartedTimestamp, sdkReadReturnedTimestamp),
                    threadIdentityRead = ElapsedMicroseconds(identityReadStartedTimestamp, identityReadReturnedTimestamp)
                },
                emittedAtUtc = DateTimeOffset.UtcNow.ToString("O"),
                runtimeObservationTick = runtime.Tick, runtimeWindowMs, runtimeDeltaKnown,
                runtimeControllerGenerationBefore, runtimeControllerGenerationAfter,
                runtimeControllerGenerationCurrent,
                runtimeObserverNativeThreadId = runtime.ObserverNativeThreadId == 0
                    ? (uint?)null : runtime.ObserverNativeThreadId,
                queue = _dispatcher.SnapshotCounters(), queueSnapshotAtomic = false,
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
    private byte[] ApplyRecordBudget(Window window, byte[] line)
    {
        var originalBytes = line.Length;
        if (originalBytes <= DiagnosticRecordMaxBytes) return line;
        ++_oversizedWindows;
        using var original = JsonDocument.Parse(line);
        // Reuse already captured scalar evidence when the large SDK payloads
        // are shed. No extra SDK read, sequence interpretation or state claim.
        static void CopyScalars(Utf8JsonWriter writer, JsonElement source,
            string[] names, ref int truncatedStrings, ref int invalidScalars)
        {
            foreach (var name in names)
            {
                if (!source.TryGetProperty(name, out var value)) continue;
                switch (value.ValueKind)
                {
                    case JsonValueKind.Null: writer.WriteNull(name); break;
                    case JsonValueKind.True: writer.WriteBoolean(name, true); break;
                    case JsonValueKind.False: writer.WriteBoolean(name, false); break;
                    case JsonValueKind.Number:
                        if (value.TryGetInt64(out var number)) writer.WriteNumber(name, number);
                        else ++invalidScalars;
                        break;
                    case JsonValueKind.String:
                        var text = value.GetString()!;
                        if (text.Length > 96)
                        {
                            text = text[..96];
                            if (char.IsHighSurrogate(text[^1])) text = text[..^1];
                            ++truncatedStrings;
                        }
                        writer.WriteString(name, text);
                        break;
                    default: ++invalidScalars; break;
                }
            }
        }
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
                schemaVersion = 2, policy = LogBudgetPolicy,
                state = "bulk-evidence-omitted", originalRecordBytes = originalBytes,
                recordMaxBytes = DiagnosticRecordMaxBytes,
                oversizedWindows = _oversizedWindows, summaryOnlyWindows = _summaryOnlyWindows,
                failedWindows = _failedWindows,
                omittedFields = new[] { "steamDeckPadSubmissions", "usbipTransport" },
                completePayloadRetained = false
            });
            if (original.RootElement.TryGetProperty("usbipTransport", out var transport) &&
                transport.ValueKind == JsonValueKind.Object)
            {
                writer.WritePropertyName("usbipTransportSummary");
                writer.WriteStartObject();
                writer.WriteNumber("version", 2);
                writer.WriteBoolean("completePayloadRetained", false);
                int truncatedStrings = 0, invalidScalars = 0;
                CopyScalars(writer, transport, new[] { "controllerGenerationCurrentBefore",
                    "controllerGenerationCurrentAfter", "omission" }, ref truncatedStrings, ref invalidScalars);
                writer.WritePropertyName("snapshot");
                if (transport.TryGetProperty("snapshot", out var snapshot) && snapshot.ValueKind == JsonValueKind.Object)
                {
                    writer.WriteStartObject();
                    CopyScalars(writer, snapshot, new[] { "Status", "Detail", "ObserverId", "SampleOrdinal",
                        "DeviceIdentity", "ConnectionIdentityBefore", "ConnectionIdentityAfter",
                        "OwnerPresentBefore", "OwnerPresentAfter", "OwnerReferenceObservation",
                        "StopFlag", "InputThreadIsAlive", "InputThreadObservation", "QueueObservation",
                        "FrameQueueCount", "SampledPendingRequestCount" }, ref truncatedStrings, ref invalidScalars);
                    WritePrimaryEndpointRequest(writer, snapshot);
                    WriteCachedPadWitness(writer, snapshot);
                    writer.WritePropertyName("ProducerProgress");
                    if (snapshot.TryGetProperty("ProducerProgress", out var progress) && progress.ValueKind == JsonValueKind.Object)
                    {
                        writer.WriteStartObject();
                        CopyScalars(writer, progress, new[] { "Version", "Status", "Detail", "BeforeTick", "AfterTick",
                            "ProducerSharedSequenceBefore", "ProducerSharedSequenceAfter",
                            "PumpAcceptedSharedSequenceBefore", "PumpAcceptedSharedSequenceAfter",
                            "SequenceValuesUnchangedAcrossCapture", "IdleThreadIsAlive", "IdleThreadObservation",
                            "InputWaitHandlePresent" }, ref truncatedStrings, ref invalidScalars);
                        writer.WriteEndObject();
                    }
                    else writer.WriteNullValue();
                    writer.WriteEndObject();
                }
                else writer.WriteNullValue();
                writer.WriteNumber("stringsTruncated", truncatedStrings);
                writer.WriteNumber("invalidScalarsOmitted", invalidScalars);
                writer.WriteEndObject();
            }
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
                WriteSubmittedPadWitness(writer, audit);
                writer.WriteEndObject();
            }
            writer.WriteEndObject();
        }
        if (buffer.Length <= DiagnosticRecordMaxBytes)
            return buffer.ToArray();
        // Unexpectedly large non-SDK metadata still gets an explicitly partial,
        // bounded identity receipt; it must not silently remove every later window.
        ++_summaryOnlyWindows;
        return SummaryRecord(window, "cost-window-summary", "non-bulk-metadata-over-budget", originalBytes);
    }

    // Preserve bounded witnesses from already-captured reports. The writer
    // and SDK cache are sampled separately; do not join them here.
    private static bool TryReadPadCounter(JsonElement snapshot, string name, out JsonElement report, out uint counter)
    {
        counter = 0;
        return snapshot.TryGetProperty(name, out report) && report.ValueKind == JsonValueKind.Object &&
            report.TryGetProperty("PacketCounter", out var value) && value.ValueKind == JsonValueKind.Number && value.TryGetUInt32(out counter);
    }

    private static bool TryReadPadBoolean(JsonElement report, string name, out bool value)
    {
        value = false;
        if (!report.TryGetProperty(name, out var property) || property.ValueKind is not (JsonValueKind.True or JsonValueKind.False)) return false;
        value = property.GetBoolean();
        return true;
    }

    private static void WriteCachedPadWitness(Utf8JsonWriter writer, JsonElement snapshot)
    {
        writer.WritePropertyName("CachedPadWitness");
        if (snapshot.TryGetProperty("InputReportCacheObservation", out var cache) &&
            cache.ValueKind == JsonValueKind.String && cache.ValueEquals("observed-primary-report-cache") &&
            TryReadPadCounter(snapshot, "LastSteamDeckPadReport", out var report, out var counter) &&
            report.TryGetProperty("Status", out var status) && status.ValueKind == JsonValueKind.String && status.ValueEquals("decoded-cached-steamdeck-report") &&
            TryReadPadBoolean(report, "LeftTouch", out var leftTouch) && TryReadPadBoolean(report, "RightTouch", out var rightTouch) &&
            TryReadPadBoolean(report, "LeftClick", out var leftClick) && TryReadPadBoolean(report, "RightClick", out var rightClick))
        {
            writer.WriteStartObject();
            writer.WriteNumber("PacketCounter", counter);
            writer.WriteNumber("TouchMask", (leftTouch ? 1 : 0) | (rightTouch ? 2 : 0));
            writer.WriteNumber("ClickMask", (leftClick ? 1 : 0) | (rightClick ? 2 : 0));
            if (report.TryGetProperty("ButtonBits", out var bits) && bits.ValueKind == JsonValueKind.Number &&
                bits.TryGetUInt64(out var mask))
                writer.WriteString("ButtonBitsHex", mask.ToString("X16", CultureInfo.InvariantCulture));
            else
                writer.WriteNull("ButtonBitsHex");
            writer.WriteEndObject();
        }
        else writer.WriteNullValue();
    }

    private static void WriteSubmittedPadWitness(Utf8JsonWriter writer, JsonElement audit)
    {
        writer.WritePropertyName("SubmittedPadWitness");
        if (TryReadPadCounter(audit, "LastSuccessfulReport", out var report, out var counter) &&
            report.TryGetProperty("TouchMask", out var touch) && touch.ValueKind == JsonValueKind.Number && touch.TryGetByte(out byte touchMask) && touchMask <= 3 &&
            report.TryGetProperty("ClickMask", out var click) && click.ValueKind == JsonValueKind.Number && click.TryGetByte(out byte clickMask) && clickMask <= 3 &&
            report.TryGetProperty("SourceTick", out var tick) && tick.ValueKind == JsonValueKind.Number && tick.TryGetUInt64(out var sourceTick) &&
            report.TryGetProperty("SampleSequence", out var sample) && sample.ValueKind == JsonValueKind.Number && sample.TryGetUInt64(out var sequence) &&
            TryReadPadBoolean(report, "NeutralCommand", out var neutral))
        {
            writer.WriteStartObject();
            writer.WriteNumber("PacketCounter", counter);
            writer.WriteNumber("TouchMask", touchMask);
            writer.WriteNumber("ClickMask", clickMask);
            writer.WriteNumber("SourceTick", sourceTick);
            writer.WriteNumber("SampleSequence", sequence);
            writer.WriteBoolean("NeutralCommand", neutral);
            writer.WriteEndObject();
        }
        else writer.WriteNullValue();
    }

    private static void WritePrimaryEndpointRequest(Utf8JsonWriter writer, JsonElement snapshot)
    {
        writer.WritePropertyName("PrimaryEndpointRequest");
        // Only project the exact primary entry from the already bounded observer sample.
        if (snapshot.TryGetProperty("QueueObservation", out var state) &&
            state.ValueKind == JsonValueKind.String && state.ValueEquals("observed") &&
            snapshot.TryGetProperty("PrimaryInEndpoint", out var primary) &&
            primary.ValueKind == JsonValueKind.Number && primary.TryGetByte(out var address) &&
            snapshot.TryGetProperty("Endpoints", out var endpoints) &&
            endpoints.ValueKind == JsonValueKind.Array && endpoints.GetArrayLength() <= 8)
        {
            JsonElement entry = default;
            foreach (var item in endpoints.EnumerateArray())
            {
                if (item.ValueKind != JsonValueKind.Object || !item.TryGetProperty("Address", out var itemAddress) ||
                    itemAddress.ValueKind != JsonValueKind.Number || !itemAddress.TryGetByte(out var value) || value != address)
                    continue;
                if (entry.ValueKind != JsonValueKind.Undefined)
                {
                    writer.WriteNullValue();
                    return;
                }
                entry = item;
            }
            if (entry.ValueKind != JsonValueKind.Undefined &&
                entry.TryGetProperty("PendingCount", out var pending) &&
                pending.ValueKind == JsonValueKind.Number && pending.TryGetInt32(out var count) && count >= 0 &&
                entry.TryGetProperty("OldestSequence", out var oldest) &&
                (count == 0 ? oldest.ValueKind == JsonValueKind.Null :
                    oldest.ValueKind == JsonValueKind.Number && oldest.TryGetUInt32(out _)))
            {
                writer.WriteStartObject();
                writer.WriteNumber("Address", address);
                writer.WriteNumber("PendingCount", count);
                writer.WritePropertyName("OldestSequence");
                oldest.WriteTo(writer);
                writer.WriteEndObject();
                return;
            }
        }
        writer.WriteNullValue();
    }

    private byte[] SummaryRecord(Window window, string kind, string reason, int originalBytes,
        Exception? error = null)
    {
        static string Bounded(string? value, int limit) =>
            value is null ? "" : value.Length <= limit ? value : value[..limit];
        return JsonSerializer.SerializeToUtf8Bytes(new
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
