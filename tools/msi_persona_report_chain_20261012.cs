// Standalone, opt-in MSI diagnostic helper. NO SDK dependency, device APIs,
// filesystem writes, observers, timers, settings changes, or input suppression.
// Not integrated into Program.cs. Submit delegates are ALWAYS invoked once.
using System;
using System.Security.Cryptography;

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
    long? EncoderFrameIndex, string Reason, uint? SourceNativeButtons);

public sealed record MsiReportChainEntry20261012(
    MsiReportChainFrame20261012 Frame, long BackendAttemptOrdinal, long BeginTick, long EndTick,
    string BackendApi, string Outcome, string? ExceptionType, int? EncodedArgumentLength,
    string? EncodedArgumentSha256, string? PredictedEdgeOverlaySha256, string? PredictedDriverWireSha256,
    uint? EncodedDigitalMask, uint? PressMask, uint? ReleaseMask, bool HasReturnedSubmitBaseline,
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
              (tuple.Persona == "dualsense" && tuple.Profile == "dualsense") ||
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
                if (payload.Length == 63 && frame.Tuple.Persona is "dualsense" or "dualsense-edge")
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
                concurrent ? "concurrent-calls;transport-order-unproven;not-ACK-or-delivery-proof" :
                    "SDK-return-or-throw-only;not-ACK-driver-delivery-or-Steam-consumption-proof");
            var signature = outcome + ":" + exception + ":" + attempt.Frame.Reason + ":" + (mask?.ToString() ?? "unknown") + ":concurrent=" + concurrent;
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






