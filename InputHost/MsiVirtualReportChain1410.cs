// Standalone, opt-in MSI diagnostic helper. NO SDK dependency, device APIs,
// filesystem writes, observers, timers, settings changes, or input suppression.
// Runtime integration is explicitly negotiated; delegates are ALWAYS invoked once.
// LB+RB summon/shoulder-reservation proposals are WITHDRAWN/out of scope.
using System;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace YeManInputHost.Diagnostics;

public sealed record MsiReportChainTuple20261012(bool IsMsi, int NativePid, int HostPid,
    string RunId, ulong Epoch, ulong PowerGeneration, string Persona, string Profile,
    string HostInstanceId, string TargetHidInstance);

// Explicit native-authorized diagnostic-session receipt. Persona is NOT family
// authority. Missing build/detail/operator gates cannot activate the tracker.
public sealed record MsiReportChainActivation20261012(bool MsiClawKnownBoardAuthority,
    bool VisibilityDiagnosticBuild, bool VirtualDetailLogEnabled, bool ExplicitSessionRequested,
    int NativePid, string RunId, ulong Epoch, ulong PowerGeneration, string DiagnosticSessionId);

public sealed record MsiReportChainFrame20261012(MsiReportChainTuple20261012 Tuple,
    ulong CommandSequence, ulong? SourceFrameSequence, ulong? SourceGeneration,
    // Compatibility name: final submitted native canonical-frame buttons, NOT
    // raw physical-source bits. Source metadata carries sequence/generation only.
    long? EncoderFrameIndex, string Reason, uint? SourceNativeButtons);

public sealed record MsiReportChainEntry20261012(
    MsiReportChainFrame20261012 Frame, long BackendAttemptOrdinal, long BeginTick, long EndTick,
    string BackendApi, string Outcome, string? ExceptionType, int? EncodedArgumentLength,
    string? EncodedArgumentSha256, string? PredictedEdgeOverlaySha256, string? PredictedDriverWireSha256,
    uint? EncodedDigitalMask, uint? PressMask, uint? ReleaseMask, bool HasReturnedSubmitBaseline,
    // Candidate means same FINAL SUBMITTED native-frame bits; it does not prove
    // that the physical source stayed pressed, or identify a double-click cause.
    bool RepressAfterNeutralWithSameSourceBits, string EncodingScope, string DeliveryScope);

public sealed record MsiReportChainSnapshot20261012(MsiReportChainEntry20261012[] Entries,
    long MeaningfulRecordsDropped, long AttemptsCaptured, long StaleCompletionsDiscarded, long UnchangedReportsSuppressed, long DiagnosticFailures,
    string Scope = "bounded-SDK-call-boundary-only;not-driver-read-or-Steam-delivery");

public sealed class MsiPersonaReportChain20261012
{
    private const int MaxAttempts = 4096;
    private const int MaxDurationMs = 30000;
    private readonly object _gate = new();
    private readonly Func<long> _clock;
    private readonly Func<byte[], string>? _diagnosticDigest;
    private readonly Func<MsiReportChainEntry20261012, string>? _formatter;
    private readonly Action<string>? _logger;
    private MsiReportChainTuple20261012? _tuple;
    private MsiReportChainEntry20261012?[] _ring = Array.Empty<MsiReportChainEntry20261012?>();
    private long _deadline, _generation, _attempts, _overwritten, _stale, _concurrencyVersion, _unchanged, _diagnosticFailures;
    private int _count, _next, _inFlight;
    private uint? _lastReturnedMask, _lastLiveSource, _lastLiveEncoded;
    private ulong? _lastLiveGeneration, _lastLiveSampleSequence;
    private string? _lastReturnedReason, _lastRecordedSignature;

    private sealed record Attempt(long Generation, long Ordinal, long Begin, long ConcurrencyVersion,
        MsiReportChainFrame20261012 Frame, int? Length, string? ArgHash, string? OverlayHash,
        string? WireHash, uint? Mask, string Api, string EncodingScope);

    public MsiPersonaReportChain20261012(Func<long>? monotonicClock = null,
        Func<byte[], string>? diagnosticDigest = null,
        Func<MsiReportChainEntry20261012, string>? formatter = null, Action<string>? logger = null)
    {
        _clock = monotonicClock ?? (() => Environment.TickCount64);
        _diagnosticDigest = diagnosticDigest; _formatter = formatter; _logger = logger;
    }

    public bool Arm(MsiReportChainTuple20261012 tuple, MsiReportChainActivation20261012 activation,
        int durationMs = 10000, int capacity = 128)
    {
        // Call only at an explicit MSI diagnostic arm/target boundary. Invalid
        // or non-MSI identities cannot enable tracing for ROG/GPD/common lanes.
        if (tuple == null || activation == null || !activation.MsiClawKnownBoardAuthority ||
            !activation.VisibilityDiagnosticBuild || !activation.VirtualDetailLogEnabled || !activation.ExplicitSessionRequested ||
            activation.NativePid != tuple.NativePid || activation.RunId != tuple.RunId || activation.Epoch != tuple.Epoch ||
            activation.PowerGeneration != tuple.PowerGeneration || !Bounded(activation.DiagnosticSessionId, 128) || !tuple.IsMsi || tuple.NativePid <= 0 || tuple.HostPid <= 0 || tuple.Epoch == 0 || tuple.PowerGeneration == 0 ||
            !Bounded(tuple.RunId, 128) || !Bounded(tuple.HostInstanceId, 128) || !Bounded(tuple.TargetHidInstance, 256) ||
            !((tuple.Persona == "dualsense-edge" && tuple.Profile == "dualsense-edge") ||
              (tuple.Persona == "elite" && tuple.Profile == "xbox-elite-v2")) ||
            durationMs is < 1 or > MaxDurationMs || capacity is < 8 or > 128) return false;
        try
        {
            lock (_gate)
            {
                var deadline = checked(_clock() + durationMs);
                var ring = new MsiReportChainEntry20261012?[capacity];
                _tuple = tuple; _deadline = deadline; ++_generation;
                _ring = ring; _count = _next = 0;
                _attempts = _overwritten = _stale = _unchanged = _diagnosticFailures = 0;
                _lastReturnedMask = _lastLiveSource = _lastLiveEncoded = null;
                _lastReturnedReason = _lastRecordedSignature = null;
                _lastLiveGeneration = _lastLiveSampleSequence = null;
            }
            return true;
        }
        catch { DiagnosticFailureSafely(invalidateHistory: true); return false; }
    }
    private static bool Bounded(string value, int max) => !string.IsNullOrWhiteSpace(value) && value.Length <= max;
    public void Disarm() { lock (_gate) { _tuple = null; ++_generation; _lastReturnedMask = null; } }

    // This wraps the actual SubmitRawReport call, NOT a pipe ACK or frame build.
    // The argument digest is exact at the pre-call boundary. The other digests
    // are explicitly PREDICTIONS from locked Edge defaults/driver source, not
    // captured shared memory or observed driver/Steam bytes. Input is unchanged.
    public void InvokeRaw(MsiReportChainFrame20261012 frame, byte[] dataOnly, Action<byte[]> submit)
    {
        Attempt? attempt = null;
        try { attempt = Begin(frame, dataOnly, "SubmitRawReport"); } catch { DiagnosticFailureSafely(invalidateHistory: true); }
        try { submit(dataOnly); }
        catch (Exception ex) { CompleteSafely(attempt, "threw", ex.GetType().FullName); throw; }
        CompleteSafely(attempt, "returned", null);
    }

    // Elite Host uses SubmitState. Its actual SDK encoder buffer is not exposed
    // by this helper. NEVER hash a state/frame and call it an encoded report.
    public void InvokeUnencodedState(MsiReportChainFrame20261012 frame, Action submit)
    {
        Attempt? attempt = null;
        try { attempt = Begin(frame, null, "SubmitState"); } catch { DiagnosticFailureSafely(invalidateHistory: true); }
        try { submit(); }
        catch (Exception ex) { CompleteSafely(attempt, "threw", ex.GetType().FullName); throw; }
        CompleteSafely(attempt, "returned", null);
    }

    private Attempt? Begin(MsiReportChainFrame20261012 frame, byte[]? payload, string api)
    {
        lock (_gate)
        {
            var now = _clock();
            if (_tuple == null || frame.Tuple != _tuple || frame.CommandSequence == 0 ||
                !Bounded(frame.Reason, 64) || now >= _deadline || _attempts >= MaxAttempts || _count >= _ring.Length)
            { _lastReturnedMask = _lastLiveSource = _lastLiveEncoded = null; return null; }
            var ordinal = ++_attempts;
            string? argHash = null, overlayHash = null, wireHash = null; uint? mask = null;
            string encodingScope = "SDK-state-encoder-buffer-not-observed";
            // No unbounded buffer copy/hash. Invalid layout is recorded unknown,
            // but still sent through unchanged so instrumentation cannot repair
            // or reject a report on the caller's behalf.
            if (api == "SubmitRawReport" && payload != null && payload.Length <= 64)
            {
                argHash = Digest(payload);
                encodingScope = "exact-encoded-argument-at-SDK-call;driver-wire-not-observed";
                if (payload.Length == 63 && frame.Tuple.Persona == "dualsense-edge")
                {
                    try { mask = DecodeDualSenseDigitalMask(payload); }
                    catch (ArgumentException) { encodingScope += ";invalid-digital-layout"; }
                    if (frame.Tuple.Persona == "dualsense-edge")
                    {
                        var projection = ProjectLockedEdgeOverlay(payload);
                        overlayHash = Digest(projection);
                        var wire = new byte[64]; wire[0] = 1; projection.CopyTo(wire, 1);
                        wireHash = Digest(wire);
                    }
                }
                else encodingScope += ";digital-layout-not-admitted";
            }
            // No locks around backend calls. Concurrent submitters invalidate
            // transition continuity instead of being silently serialized here.
            if (++_inFlight > 1) ++_concurrencyVersion;
            return new Attempt(_generation, ordinal, now, _concurrencyVersion, frame,
                payload?.Length, argHash, overlayHash, wireHash, mask, api, encodingScope);
        }
    }
    private string Digest(byte[] bytes) => _diagnosticDigest == null
        ? Convert.ToHexString(SHA256.HashData(bytes)) : _diagnosticDigest((byte[])bytes.Clone());
    private void DiagnosticFailureSafely(bool invalidateHistory = false)
    {
        try { lock (_gate) { ++_diagnosticFailures; if (invalidateHistory) _lastReturnedMask = _lastLiveSource = _lastLiveEncoded = null; } } catch { }
    }
    private void CompleteSafely(Attempt? attempt, string outcome, string? exception)
    {
        try
        {
            var entry = attempt != null ? Complete(attempt, outcome, exception) : null;
            // Serialization and logger are outside the backend's try/catch and
            // outside the tracker lock. They run ONLY for <=128 meaningful
            // edges/lifecycle/failure changes, never on every held report.
            if (entry != null && _formatter != null && _logger != null)
            { try { _logger(_formatter(entry)); } catch { DiagnosticFailureSafely(); } }
        }
        catch { DiagnosticFailureSafely(invalidateHistory: true); }
    }
    private MsiReportChainEntry20261012? Complete(Attempt attempt, string outcome, string? exception)
    {
        lock (_gate)
        {
            var concurrent = _inFlight > 1 || attempt.ConcurrencyVersion != _concurrencyVersion;
            --_inFlight;
            if (attempt.Generation != _generation || attempt.Frame.Tuple != _tuple) { ++_stale; return null; }
            var mask = attempt.Mask;
            bool success = outcome == "returned" && !concurrent && mask != null;
            bool baseline = success && _lastReturnedMask != null;
            uint? pressed = baseline ? mask!.Value & ~_lastReturnedMask!.Value : null;
            uint? released = baseline ? _lastReturnedMask!.Value & ~mask!.Value : null;
            bool repress = baseline && pressed != 0 && IsNeutralReason(_lastReturnedReason) &&
                attempt.Frame.Reason == "live-frame" && attempt.Frame.SourceNativeButtons != null &&
                _lastLiveSource == attempt.Frame.SourceNativeButtons && _lastLiveEncoded == mask &&
                attempt.Frame.SourceGeneration != null && _lastLiveGeneration == attempt.Frame.SourceGeneration &&
                attempt.Frame.SourceFrameSequence != null && _lastLiveSampleSequence != null &&
                attempt.Frame.SourceFrameSequence >= _lastLiveSampleSequence;
            var entry = new MsiReportChainEntry20261012(attempt.Frame, attempt.Ordinal, attempt.Begin, _clock(),
                attempt.Api, outcome, exception, attempt.Length, attempt.ArgHash, attempt.OverlayHash, attempt.WireHash,
                mask, pressed, released, baseline, repress, attempt.EncodingScope,
                concurrent ? "concurrent-calls;transport-order-unproven;not-ACK-or-delivery-proof;native-final-submitted-frame-buttons;raw-physical-source-bits-unavailable;repress-not-same-physical-press-proof" :
                    "SDK-return-or-throw-only;not-ACK-driver-delivery-or-Steam-consumption-proof;native-final-submitted-frame-buttons;raw-physical-source-bits-unavailable;repress-not-same-physical-press-proof");
            var signature = outcome + ":" + exception + ":" + attempt.Frame.Reason + ":" + (mask?.ToString() ?? "unknown") + ":concurrent=" + concurrent +
                (attempt.Api == "SubmitState" ? ":native-source=" + attempt.Frame.SourceNativeButtons : "");
            bool meaningful = _lastRecordedSignature != signature || pressed is > 0 || released is > 0 || repress;
            MsiReportChainEntry20261012? emitted = null;
            if (meaningful && _count < _ring.Length)
            { _ring[_next] = entry; _next = (_next + 1) % _ring.Length; ++_count; emitted = entry; _lastRecordedSignature = signature; }
            else if (!meaningful) ++_unchanged;
            else ++_overwritten; // hard-cap drop count, never overwrite/emit >128
            // A thrown SDK method may have written before throwing. Never assume
            // the previous transport state is still present after a failure.
            _lastReturnedMask = success ? mask : null;
            _lastReturnedReason = success ? attempt.Frame.Reason : null;
            if (!success) _lastLiveSource = _lastLiveEncoded = null;
            else if (attempt.Frame.Reason == "live-frame")
            { _lastLiveSource = attempt.Frame.SourceNativeButtons; _lastLiveEncoded = mask;
                _lastLiveGeneration = attempt.Frame.SourceGeneration; _lastLiveSampleSequence = attempt.Frame.SourceFrameSequence; }
            return emitted;
        }
    }
    private static bool IsNeutralReason(string? reason) => reason is "submit-neutral-command" or "release-neutral";
    public bool BudgetExhausted { get { lock (_gate) return _attempts >= MaxAttempts || (_ring.Length > 0 && _count >= _ring.Length); } }
    public MsiReportChainSnapshot20261012 Snapshot()
    {
        lock (_gate)
        {
            var entries = new MsiReportChainEntry20261012[_count];
            var first = (_next - _count + _ring.Length) % Math.Max(1, _ring.Length);
            for (int i = 0; i < _count; ++i) entries[i] = _ring[(first + i) % _ring.Length]!;
            return new(entries, _overwritten, _attempts, _stale, _unchanged, _diagnosticFailures);
        }
    }
    public static byte[] ProjectLockedEdgeOverlay(ReadOnlySpan<byte> dataOnly)
    {
        if (dataOnly.Length != 63) throw new ArgumentException("Expected DualSense data-only length63");
        var result = dataOnly.ToArray();
        // Core@46054b862830fcec7bc98d72ccb7c4f0c0179fb1 Edge inputDefaults
        // on-wire bytes49..52 -> data-only indices48..51 (report ID is1).
        result[48] = 128; result[49] = result[50] = result[51] = 0; return result;
    }
    public static uint DecodeDualSenseDigitalMask(ReadOnlySpan<byte> dataOnly)
    {
        if (dataOnly.Length != 63) throw new ArgumentException("Expected DualSense data-only length63");
        var hat = dataOnly[7] & 15;
        if (hat > 8) throw new ArgumentException("Invalid DualSense hat");
        uint directions = hat switch { 0 => 1, 1 => 1 | 8, 2 => 8, 3 => 2 | 8,
            4 => 2, 5 => 2 | 4, 6 => 4, 7 => 1 | 4, 8 => 0, _ => 0 };
        return (uint)(dataOnly[7] >> 4) | ((uint)dataOnly[8] << 4) |
            ((uint)(dataOnly[9] & 0xF7) << 12) | (directions << 20);
    }
}








// The only protocol surface: optional trace metadata is parsed independently of
// ordinary controls. Invalid metadata is OFF/unknown, never a control rejection.
public sealed record MsiReportChainPrepare1410(MsiReportChainActivation20261012 Activation,
    int DurationMs, int MaxMeaningfulEdges);
public sealed record MsiReportChainSource1410(string DiagnosticSessionId, ulong SourceFrameSequence, ulong SourceGeneration);

public static class MsiReportChainProtocol1410
{
    public const int DiagnosticVersion = 1;
    public const string PrepareField = "msiReportChainDiagnostic";
    public const string SourceField = "msiReportChainSource";
    public static bool IsMetadataField(string field) => field is PrepareField or SourceField;
    private static bool ExactFields(JsonElement value, string[] required, string[] optional)
    {
        if (value.ValueKind != JsonValueKind.Object) return false;
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var item in value.EnumerateObject())
            if (!seen.Add(item.Name) || (!required.Contains(item.Name, StringComparer.Ordinal) && !optional.Contains(item.Name, StringComparer.Ordinal))) return false;
        return required.All(seen.Contains);
    }
    private static bool True(JsonElement value, string key) => value.TryGetProperty(key, out var p) && p.ValueKind == JsonValueKind.True;
    private static bool UInt(JsonElement value, string key, out ulong n)
    {
        n = 0; return value.TryGetProperty(key, out var p) && p.ValueKind == JsonValueKind.Number && p.TryGetUInt64(out n) && n > 0;
    }
    private static bool Text(JsonElement value, string key, int max, out string text)
    {
        text = ""; if (!value.TryGetProperty(key, out var p) || p.ValueKind != JsonValueKind.String) return false;
        text = p.GetString() ?? ""; return !string.IsNullOrWhiteSpace(text) && text.Length <= max;
    }
    private static bool OptionalBound(JsonElement value, string key, int fallback, int min, int max, out int n)
    {
        n = fallback; return !value.TryGetProperty(key, out var p) ||
            (p.ValueKind == JsonValueKind.Number && p.TryGetInt32(out n) && n >= min && n <= max);
    }
    public static MsiReportChainPrepare1410? ParsePrepareSafely(JsonElement value, string command,
        string runId, ulong epoch, ulong powerGeneration)
    {
        try
        {
            if (command != "PREPARE_TARGET" || !ExactFields(value,
                new[] { "schemaVersion", "enabled", "msiClawKnownBoardAuthority", "visibilityDiagnosticBuild", "virtualDetailLogEnabled",
                    "explicitSessionRequested", "nativePid", "runId", "epoch", "powerGeneration", "diagnosticSessionId" },
                new[] { "durationMs", "maxMeaningfulEdges" })) return null;
            if (!UInt(value, "schemaVersion", out var version) || version != DiagnosticVersion ||
                !True(value, "enabled") || !True(value, "msiClawKnownBoardAuthority") || !True(value, "visibilityDiagnosticBuild") ||
                !True(value, "virtualDetailLogEnabled") || !True(value, "explicitSessionRequested") ||
                !UInt(value, "nativePid", out var nativePid) || nativePid > int.MaxValue ||
                !Text(value, "runId", 160, out var requestedRun) || requestedRun != runId ||
                !UInt(value, "epoch", out var requestedEpoch) || requestedEpoch != epoch ||
                !UInt(value, "powerGeneration", out var requestedPower) || requestedPower != powerGeneration ||
                !Text(value, "diagnosticSessionId", 128, out var sessionId) ||
                !OptionalBound(value, "durationMs", 10000, 1, 30000, out var duration) ||
                !OptionalBound(value, "maxMeaningfulEdges", 128, 8, 128, out var capacity)) return null;
            return new(new(true, true, true, true, (int)nativePid, runId, epoch, powerGeneration, sessionId), duration, capacity);
        }
        catch { return null; }
    }
    public static MsiReportChainSource1410? ParseSourceSafely(JsonElement value, string command)
    {
        try
        {
            if (command is not ("SUBMIT_FRAME" or "SUBMIT_NEUTRAL") || !ExactFields(value,
                new[] { "schemaVersion", "diagnosticSessionId", "sourceFrameSequence", "sourceGeneration" }, Array.Empty<string>()) ||
                !UInt(value, "schemaVersion", out var version) || version != DiagnosticVersion ||
                !Text(value, "diagnosticSessionId", 128, out var sessionId) ||
                !UInt(value, "sourceFrameSequence", out var sourceSequence) ||
                !UInt(value, "sourceGeneration", out var sourceGeneration)) return null;
            return new(sessionId, sourceSequence, sourceGeneration);
        }
        catch { return null; }
    }
}

// Per-Host/per-target ephemeral runtime; no setting, UI, service, driver read,
// controller read, SDK reflection, observer, timer or environment mutation.
public sealed class MsiReportChainRuntime1410
{
    private readonly Func<long> _clock;
    private readonly Func<byte[], string>? _digest;
    private readonly Func<object, string> _formatter;
    private readonly Action<string> _sink;
    private MsiPersonaReportChain20261012? _tracker;
    private MsiReportChainTuple20261012? _tuple;
    private string? _sessionId;
    private long _deadline;
    public bool Armed => _tracker != null && _tuple != null && _sessionId != null;
    public string? SessionId => Armed ? _sessionId : null;

    public MsiReportChainRuntime1410(Func<long>? clock = null, Func<byte[], string>? digest = null,
        Func<object, string>? formatter = null, Action<string>? sink = null)
    {
        _clock = clock ?? (() => Environment.TickCount64); _digest = digest;
        _formatter = formatter ?? (value => JsonSerializer.Serialize(value));
        _sink = sink ?? AppendDurableLine;
    }
    public bool ArmSafely(MsiReportChainPrepare1410? prepare, int actualParentPid, int actualHostPid,
        string hostInstanceId, string persona, string profile, string runId, ulong epoch, ulong powerGeneration,
        string admittedPnpDetail, bool targetAdmitted)
    {
        try
        {
            CloseSafely("target-replaced");
            if (!targetAdmitted || prepare == null || actualParentPid <= 0 || actualParentPid != prepare.Activation.NativePid ||
                prepare.Activation.RunId != runId || prepare.Activation.Epoch != epoch || prepare.Activation.PowerGeneration != powerGeneration) return false;
            const string prefix = "pnp-hid-ids=";
            var start = admittedPnpDetail.IndexOf(prefix, StringComparison.Ordinal);
            if (start < 0) return false; start += prefix.Length;
            var end = admittedPnpDetail.IndexOf('[', start);
            if (end < start || end - start > 256) return false;
            var hid = admittedPnpDetail.Substring(start, end - start);
            if (!hid.StartsWith("HID\\", StringComparison.OrdinalIgnoreCase) || hid.Contains(';') || hid.Contains(',')) return false;
            var tuple = new MsiReportChainTuple20261012(true, actualParentPid, actualHostPid, runId, epoch, powerGeneration,
                persona, profile, hostInstanceId, hid);
            var tracker = new MsiPersonaReportChain20261012(_clock, _digest,
                entry => _formatter(new { @event = "msi-report-chain-submit", schemaVersion = 1,
                    diagnosticSessionId = prepare.Activation.DiagnosticSessionId, entry }), _sink);
            var deadline = checked(_clock() + prepare.DurationMs);
            if (!tracker.Arm(tuple, prepare.Activation, prepare.DurationMs, prepare.MaxMeaningfulEdges)) return false;
            _tracker = tracker; _tuple = tuple; _sessionId = prepare.Activation.DiagnosticSessionId;
            _deadline = deadline;
            return true;
        }
        catch { _tracker = null; _tuple = null; _sessionId = null; return false; }
    }
    public bool TryInvocationSafely(string runId, ulong epoch, ulong powerGeneration, string persona, string profile,
        ulong commandSequence, string reason, long? encoderFrameIndex, MsiReportChainSource1410? source,
        uint? nativeProducerButtons, out MsiPersonaReportChain20261012? tracker, out MsiReportChainFrame20261012? frame)
    {
        tracker = null; frame = null;
        try
        {
            if (!Armed) return false;
            var tuple = _tuple!;
            if (tuple.RunId != runId || tuple.Epoch != epoch || tuple.PowerGeneration != powerGeneration ||
                tuple.Persona != persona || tuple.Profile != profile)
            { CloseSafely("tuple-changed"); return false; }
            // No per-frame snapshot/copy/log loop; budgets use tracker counters.
            if (_clock() >= _deadline || _tracker!.BudgetExhausted)
            { CloseSafely("budget-expired"); return false; }
            var correlated = source != null && source.DiagnosticSessionId == _sessionId;
            frame = new(tuple, commandSequence, correlated ? source!.SourceFrameSequence : null,
                correlated ? source!.SourceGeneration : null, encoderFrameIndex, reason, correlated ? nativeProducerButtons : null);
            tracker = _tracker;
            return true;
        }
        catch { tracker = null; frame = null; return false; }
    }
    public void AfterBackendSafely()
    {
        try { if (Armed && (_clock() >= _deadline || _tracker!.BudgetExhausted)) CloseSafely("budget-complete"); }
        catch { }
    }
    public void CloseSafely(string reason)
    {
        var tracker = _tracker; var tuple = _tuple; var session = _sessionId;
        _tracker = null; _tuple = null; _sessionId = null;
        if (tracker == null || tuple == null || session == null) return;
        try
        {
            tracker.Disarm();
            var snapshot = tracker.Snapshot();
            // Single durable close snapshot, built only from completed backend
            // attempts. Edges were already logged <=128; no ACK/delivery claim.
            _sink(_formatter(new { @event = "msi-report-chain-session-close", schemaVersion = 1,
                diagnosticSessionId = session, reason, tuple, snapshot }));
        }
        catch { }
    }
    private static void AppendDurableLine(string line)
    {
        // Called only after a completed backend attempt or lifecycle close.
        // The caller isolates every path/digest/serialization/logger failure.
        if (Encoding.UTF8.GetByteCount(line) > 512 * 1024) return;
        var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC");
        Directory.CreateDirectory(dir);
        var file = Path.Combine(dir, "input-host-report-chain.log");
        if (File.Exists(file) && new FileInfo(file).Length >= 1024 * 1024) File.Move(file, file + ".prev", true);
        File.AppendAllText(file, line + Environment.NewLine, Encoding.UTF8);
    }
}
