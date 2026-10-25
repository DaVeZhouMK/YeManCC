using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using HIDMaestro;
using YeManInputHost.Diagnostics;

internal static partial class Program
{
    // Caller-driven only: no flag reads, device reads, worker, timer or device ownership.
    // The Host lifecycle gate normally serializes callers; this gate also invalidates stale completions.
    private sealed class HostVirtualSignalEvidence
    {
        internal const int MaximumLineBytes = 8192, MaximumFileBytes = 5 * 1024 * 1024, HexBytes = 128;
        private const long WindowMs = 5000;
        private static readonly object RateGate = new(), WriterGate = new();
        private static long _nextExtraTick = long.MinValue; // process-wide; NEVER reset on OFF/rebind
        private static ulong? _globalLiveBucket;
        private static readonly JsonSerializerOptions StateOptions = new() { IncludeFields = true };
        private readonly object _gate = new();
        private readonly string _directory;
        private readonly Func<long>? _clock;
        private readonly Action<string>? _write;
        private readonly Counts[] _reasons = new Counts[5];
        private Counts _total;
        private readonly ParameterWindow _parameters = new();
        private readonly LossCounts _loss = new(); // instance lifetime; survives OFF/controller/binding resets
        private long _parameterWindowId;
        private long _generation, _windowStart, _ordinal, _suppressed, _diagnosticFailures;
        private object? _controller;
        private string? _hostInstance;
        private bool _hostInstanceTextTruncated;
        private CommandEnvelope? _binding, _lastCanonical;
        private string? _lastReason;
        private long _lastSampleTick;
        private ulong? _liveBucket;
        private bool _active, _lastFailed, _outcomeChanged;

        // Optional pure clock/sink seams are used by the actual-source fake SDK fixture.
        internal HostVirtualSignalEvidence(string directory, Func<long>? clock = null, Action<string>? write = null)
        { _directory = directory; _clock = clock; _write = write; }
        private long Now => _clock is null ? Environment.TickCount64 : _clock();
        private struct Counts { public long Calls, Returned, Threw; }
        internal readonly record struct Attempt(long Generation, int Reason, long Ordinal, Sample? Snapshot)
        {
            internal long ParameterWindowId { get; init; }
            internal ControlPoint Parameters { get; init; }
        }
        internal sealed record Sample(long Tick, string Selection, object Binding, object Canonical,
            string Reason, long? EncoderIndex, object SdkArguments, bool WireTickKnown, ulong WireTick, ulong WireSampleSequence, bool JoinEligible);

        internal void ResetSafely(bool preservePendingTail = false)
        {
            try { lock (_gate) Reset(preservePendingTail); } catch { }
        }
        // Optional read-only export seam: no SDK access, JSON, I/O, rate token or reset.
        // Enabled lifecycle resets also try a separate tail receipt. A failed/disabled reset
        // retains cumulative loss counts; detailed pending data is available before that reset.
        internal object? SnapshotPendingSafely()
        {
            try
            {
                lock (_gate)
                {
                    return new
                    {
                        schema = 1, kind = "host-virtual-signal-pending-snapshot", joinEligible = false,
                        scope = "read-only-memory-snapshot;not-a-new-SDK-attempt-or-disk-receipt;caller-integration-required",
                        active = _active, tailNotFlushed = _parameters.Calls != 0,
                        tailPendingAttempts = _parameters.Calls, bindingGeneration = _generation,
                        request = CaptureBindingTuple(),
                        parameterWindow = _parameters.Calls == 0 ? null : _parameters.Capture(_generation, _parameterWindowId, Now, _loss),
                        loss = _loss.Capture()
                    };
                }
            }
            catch { return null; }
        }
        private object? CaptureBindingTuple() => _binding is null ? null : new
        {
            hostInstanceId = _hostInstance, bindingGeneration = _generation,
            runId = Clip(_binding.RunId), epoch = _binding.Epoch, powerGeneration = _binding.PowerGeneration,
            persona = Clip(_binding.Persona), profileIdentity = Clip(_binding.ProfileIdentity),
            targetId = Clip(_binding.TargetId), ownerIdentity = Clip(_binding.OwnerIdentity),
            startupNonce = Clip(_binding.StartupNonce), configRevision = _binding.ConfigRevision, configHash = Clip(_binding.ConfigHash),
            bindingTextTruncated = _hostInstanceTextTruncated || Long(_binding.RunId) || Long(_binding.Persona) ||
                Long(_binding.ProfileIdentity) || Long(_binding.TargetId) || Long(_binding.OwnerIdentity) ||
                Long(_binding.StartupNonce) || Long(_binding.ConfigHash)
        };
        // Called only for a real binding transition or an explicitly enabled lifecycle reset.
        // This is a receipt of ALREADY observed attempts, never an SDK attempt or join candidate.
        // No retry, timer, rate-token change or shutdown-policy change; at most one existing-sink write.
        private void WritePendingTailSafely(string reason)
        {
            if (_parameters.Calls == 0) return;
            ++_loss.tailFlushAttempts;
            try
            {
                var now = Now;
                var entry = new
                {
                    schema = 1, kind = "host-virtual-signal-tail", reason, tick = now,
                    evidenceScope = "pending-Host-SDK-argument-summary-only;not-new-SDK-attempt;not-driver/game-delivery",
                    sdkAttempt = false, joinEligible = false, tailFlushed = true,
                    request = CaptureBindingTuple(),
                    parameterWindow = _parameters.Capture(_generation, _parameterWindowId, now, _loss)
                };
                var line = JsonSerializer.Serialize(entry, StateOptions);
                if (Encoding.UTF8.GetByteCount(line) + 1 > MaximumLineBytes)
                {
                    ++_loss.tailFlushFailures; ++_loss.tailFlushOversizeFailures; ++_loss.oversizeEmissionFailures;
                    return; // Reset explicitly accounts for every discarded attempt below.
                }
                if (_write is not null) _write(line); else WriteBounded(line);
                ++_loss.tailFlushedWindows; _loss.tailFlushedAttempts += _parameters.Calls;
                _parameters.Clear(); // Clear only after the existing sink accepted the bounded receipt.
            }
            catch { ++_loss.tailFlushFailures; ++_loss.emissionFailures; }
        }
        private void Reset(bool preservePendingTail = false, string reason = "close-or-reset")
        {
            if (preservePendingTail) WritePendingTailSafely(reason);
            if (_parameters.Calls != 0)
            {
                ++_loss.resetDiscardedWindows; ++_loss.tailNotFlushedWindows;
                _loss.resetDiscardedAttempts += _parameters.Calls;
                _loss.tailNotFlushedAttempts += _parameters.Calls;
                _loss.resetDiscardedPending += _parameters.Pending;
            }
            _parameters.Clear(); ++_parameterWindowId;
            ++_generation; _active = false; _controller = null; _binding = _lastCanonical = null;
            _hostInstance = null; _hostInstanceTextTruncated = false;
            _lastReason = null; _lastSampleTick = 0; _liveBucket = null; _ordinal = _suppressed = _diagnosticFailures = 0;
            _lastFailed = _outcomeChanged = false; _total = default; Array.Clear(_reasons);
        }
        private static bool SameBinding(CommandEnvelope a, CommandEnvelope b) =>
            a.RunId == b.RunId && a.Epoch == b.Epoch && a.PowerGeneration == b.PowerGeneration &&
            a.Persona == b.Persona && a.ProfileIdentity == b.ProfileIdentity &&
            a.TargetId == b.TargetId && a.OwnerIdentity == b.OwnerIdentity && a.StartupNonce == b.StartupNonce &&
            a.ConfigRevision == b.ConfigRevision && a.ConfigHash == b.ConfigHash;
        private static int ReasonIndex(string reason) => reason switch
        {
            "live-frame" => 0,
            "touch" or "internal-touch" => 1,
            "idle" or "internal-idle" => 3,
            "submit-neutral-command" or "release-neutral" or "neutral" => 2,
            _ => 4
        };
        private static string Clip(string value) => value.Length <= 160 ? value : value[..160];
        private static bool Long(string value) => value.Length > 160;
        private static bool AdmitExtra(long now)
        {
            lock (RateGate)
            {
                if (now < _nextExtraTick) return false;
                _nextExtraTick = now + 1000;
                return true;
            }
        }

        internal Attempt BeginSafely(string hostInstance, object controller, CommandEnvelope binding,
            CommandEnvelope canonical, ulong commandSequence, bool commandKnown, string reason, long? encoderIndex,
            byte[]? report, HMGamepadState? state)
        {
            try
            {
                lock (_gate)
                {
                    var now = Now;
                    if (!_active || !ReferenceEquals(controller, _controller) || _binding is null || !SameBinding(binding, _binding))
                    {
                        // Begin is reached only through the enabled Host entry. Flush the OLD tuple
                        // before changing controller/binding; initial activation has no pending tail.
                        Reset(preservePendingTail: _active, reason: "controller-or-binding-change");
                        _active = true; _controller = controller; _binding = binding; _windowStart = now;
                        _hostInstance = Clip(hostInstance); _hostInstanceTextTruncated = Long(hostInstance);
                    }
                    var category = ReasonIndex(reason);
                    ++_total.Calls; ++_reasons[category].Calls;
                    var attempt = new Attempt(_generation, category, ++_ordinal, null) { ParameterWindowId = _parameterWindowId };
                    // EVERY actual argument is observed before any sampling/rate-admission return.
                    // No JSON, hashing, report retention, I/O or unbounded container on this path.
                    _parameters.Begin(now, attempt.Ordinal, binding, canonical, commandSequence, commandKnown, category);
                    try
                    {
                        attempt = attempt with { Parameters = _parameters.Observe(binding, report, state, attempt.Ordinal, _loss) };
                    }
                    catch { ++_parameters.ObservationFailures; ++_loss.parameterObservationFailures; }
                    var windowDue = now - _windowStart >= WindowMs;
                    var first = _lastCanonical is null;
                    var live = reason == "live-frame";
                    var tickKnown = canonical.MonotonicTick != 0;
                    var bucket = canonical.MonotonicTick / 1000UL;
                    var joinEligible = false;
                    string selection;
                    if (live && tickKnown && _liveBucket != bucket)
                    {
                        // Consume the tuple+WIRE bucket's FIRST live attempt even if globally rate-dropped.
                        // Never retry a later command in that bucket: native must join the exact sequence.
                        _liveBucket = bucket;
                        lock (RateGate)
                        {
                            if (_globalLiveBucket is null || bucket > _globalLiveBucket.Value) { _globalLiveBucket = bucket; joinEligible = true; }
                        }
                        if (!joinEligible) { ++_suppressed; return attempt; }
                        selection = first ? "binding-wire-bucket-first-live" : "wire-bucket-first-live";
                    }
                    else
                    {
                        // Internal traffic cannot consume live admission. A heartbeat is explicitly
                        // not a native first-live match. Unknown ticks NEVER use host time as wire time.
                        var extraDue = windowDue || (!live && (first || _lastReason != reason || _outcomeChanged || now - _lastSampleTick >= WindowMs)) ||
                            (live && !tickKnown && (first || now - _lastSampleTick >= WindowMs));
                        if (!extraDue) return attempt;
                        if (!AdmitExtra(now)) { ++_suppressed; return attempt; }
                        selection = windowDue ? "window-heartbeat" : live ? "wire-tick-unknown" : "internal-sample";
                    }
                    _lastCanonical = canonical; _lastReason = reason; _lastSampleTick = now; _outcomeChanged = false;
                    try
                    {
                        // Snapshot BEFORE the SDK call. No live SDK object/array is retained in a sample.
                        object arguments = report is not null ? new
                        {
                            kind = "SubmitRawReport-argument", length = report.Length,
                            sha256 = Convert.ToHexString(SHA256.HashData(report)),
                            hex = Convert.ToHexString(report.AsSpan(0, Math.Min(report.Length, HexBytes))),
                            hexBytes = Math.Min(report.Length, HexBytes), hexTruncated = report.Length > HexBytes,
                            encodingScope = "actual-SDK-byte-array-argument"
                        } : new
                        {
                            kind = "SubmitState-argument", state = JsonSerializer.SerializeToElement(state, StateOptions),
                            encodingScope = "HMGamepadState-actual-parameters;encoded-report-unavailable"
                        };
                        var ids = new
                        {
                            hostInstanceId = Clip(hostInstance), bindingGeneration = _generation,
                            startupNonce = Clip(binding.StartupNonce), runId = Clip(binding.RunId), epoch = binding.Epoch,
                            powerGeneration = binding.PowerGeneration, persona = Clip(binding.Persona),
                            profileIdentity = Clip(binding.ProfileIdentity), targetId = Clip(binding.TargetId),
                            ownerIdentity = Clip(binding.OwnerIdentity), configRevision = binding.ConfigRevision,
                            configHash = Clip(binding.ConfigHash), commandSequence, sampleSequence = canonical.SampleSequence,
                            commandSequenceSource = commandKnown ? "actual-command-envelope" : "last-accepted-command-context",
                            command = commandKnown ? Clip(binding.Command) : null, requestId = commandKnown ? Clip(binding.RequestId) : null,
                            bindingTextTruncated = Long(hostInstance) || Long(binding.RunId) || Long(binding.Persona) ||
                                Long(binding.ProfileIdentity) || Long(binding.TargetId) || Long(binding.OwnerIdentity) ||
                                Long(binding.Command) || Long(binding.RequestId) || Long(binding.StartupNonce) || Long(binding.ConfigHash)
                        };
                        var controls = new
                        {
                            buttons = canonical.Buttons, lx = canonical.Lx, ly = canonical.Ly,
                            rx = canonical.Rx, ry = canonical.Ry, lt = canonical.Lt, rt = canonical.Rt,
                            ltRaw = canonical.LtRaw, rtRaw = canonical.RtRaw, hasRawDomain = canonical.HasRawDomain,
                            rawButtons = canonical.RawButtons, rawThumbLX = canonical.RawThumbLX, rawThumbLY = canonical.RawThumbLY,
                            rawThumbRX = canonical.RawThumbRX, rawThumbRY = canonical.RawThumbRY,
                            rawLeftTrigger = canonical.RawLeftTrigger, rawRightTrigger = canonical.RawRightTrigger,
                            backButtons = canonical.BackButtons, screenButtons = canonical.ScreenButtons,
                            screenPadsPresent = canonical.ScreenPadsPresent,
                            screenPadLeft = canonical.ScreenPadLeft, screenPadRight = canonical.ScreenPadRight,
                            imuAdmitted = canonical.ImuAdmitted, gyroX = canonical.ImuGyroX, gyroY = canonical.ImuGyroY,
                            gyroZ = canonical.ImuGyroZ, accelX = canonical.ImuAccelX, accelY = canonical.ImuAccelY, accelZ = canonical.ImuAccelZ,
                            imuQuaternionPresent = canonical.ImuQuaternionPresent, quatW = canonical.ImuQuatW,
                            quatX = canonical.ImuQuatX, quatY = canonical.ImuQuatY, quatZ = canonical.ImuQuatZ,
                            sampleSequence = canonical.SampleSequence, monotonicTick = canonical.MonotonicTick,
                            source = reason == "live-frame" ? "final-live-envelope" : "actual-internal-envelope"
                        };
                        return attempt with { Snapshot = new(now, selection, ids, controls, Clip(reason), encoderIndex, arguments, tickKnown, canonical.MonotonicTick, canonical.SampleSequence, joinEligible) };
                    }
                    catch { ++_diagnosticFailures; return attempt; }
                }
            }
            catch { return default; }
        }

        internal void CompleteSafely(Attempt attempt, Exception? sdkException)
        {
            try
            {
                lock (_gate)
                {
                    if (attempt.Ordinal == 0) return;
                    if (!_active || attempt.Generation != _generation) { ++_loss.staleGenerationCompletions; return; }
                    var failed = sdkException is not null;
                    if (attempt.ParameterWindowId == _parameterWindowId)
                        _parameters.Complete(attempt, failed);
                    else ++_loss.lateWindowCompletions; // previous emitted window explicitly reported pending

                    if (failed) { ++_total.Threw; ++_reasons[attempt.Reason].Threw; }
                    else { ++_total.Returned; ++_reasons[attempt.Reason].Returned; }
                    if (failed != _lastFailed) _outcomeChanged = true;
                    _lastFailed = failed;
                    var sample = attempt.Snapshot;
                    if (sample is null) return;
                    var now = Now;
                    var windowDue = now - _windowStart >= WindowMs;
                    object? window = null;
                    if (windowDue)
                    {
                        window = new
                        {
                            startTick = _windowStart, endTick = now, elapsedMs = now - _windowStart,
                            calls = _total.Calls, returned = _total.Returned, threw = _total.Threw,
                            liveFrame = _reasons[0], internalTouch = _reasons[1], neutral = _reasons[2],
                            internalIdle = _reasons[3], other = _reasons[4],
                            rateSuppressed = _suppressed, diagnosticFailures = _diagnosticFailures
                        };
                        _windowStart = now; _total = default; Array.Clear(_reasons); _suppressed = _diagnosticFailures = 0;
                    }
                    var parameterWindow = _parameters.Capture(_generation, _parameterWindowId, now, _loss);
                    var entry = new
                    {
                        schema = 1, kind = "host-virtual-signal", evidenceScope = "SDK-call-only;not-driver/game-delivery",
                        coverage = "Sony/Elite/SteamDeck-host-submissions;excludes-SDK-internal-idle/touch",
                        tick = sample.Tick, completedTick = now, selection = sample.Selection,
                        wireTickKnown = sample.WireTickKnown, wireMonotonicTick = sample.WireTickKnown ? (ulong?)sample.WireTick : null,
                        wireSampleSequence = sample.WireTickKnown ? (ulong?)sample.WireSampleSequence : null,
                        sampleBucket = sample.WireTickKnown ? (ulong?)(sample.WireTick / 1000UL) : null,
                        joinEligible = sample.JoinEligible,
                        request = sample.Binding, ordinal = attempt.Ordinal, reason = sample.Reason,
                        encoderIndex = sample.EncoderIndex, canonical = sample.Canonical, sdkArguments = sample.SdkArguments,
                        outcome = failed ? "threw" : "returned", exceptionType = failed ? Clip(sdkException!.GetType().FullName ?? "Exception") : null,
                        exceptionHResult = failed ? (int?)sdkException!.HResult : null, window, parameterWindow
                    };
                    var line = JsonSerializer.Serialize(entry, StateOptions);
                    if (Encoding.UTF8.GetByteCount(line) + 1 > MaximumLineBytes)
                    {
                        // Keep the all-call summary even when the individual sampled payload is too large.
                        ++_loss.samplesOmittedForLineBudget;
                        parameterWindow = _parameters.Capture(_generation, _parameterWindowId, now, _loss);
                        line = JsonSerializer.Serialize(new
                        {
                            schema = 1, kind = "host-virtual-signal", evidenceScope = "SDK-call-only;not-driver/game-delivery",
                            tick = sample.Tick, request = sample.Binding, ordinal = attempt.Ordinal,
                            wireTickKnown = sample.WireTickKnown, wireMonotonicTick = sample.WireTickKnown ? (ulong?)sample.WireTick : null,
                            wireSampleSequence = sample.WireTickKnown ? (ulong?)sample.WireSampleSequence : null,
                            sampleBucket = sample.WireTickKnown ? (ulong?)(sample.WireTick / 1000UL) : null, joinEligible = sample.JoinEligible,
                            outcome = failed ? "threw" : "returned", sampleOmitted = "line-byte-budget", window, parameterWindow
                        }, StateOptions);
                    }
                    if (Encoding.UTF8.GetByteCount(line) + 1 > MaximumLineBytes)
                    { ++_diagnosticFailures; ++_loss.oversizeEmissionFailures; return; }
                    if (_write is not null) _write(line); else WriteBounded(line);
                    // Only successful existing-sample writes close a parameter window. Failures retain it.
                    _parameters.Clear(); ++_parameterWindowId;
                }
            }
            catch { try { lock (_gate) { ++_diagnosticFailures; ++_loss.emissionFailures; } } catch { } }
        }

        // Fixed storage, independent of frame count and elapsed time. Text is copied into two
        // bounded stamps, never retained as live envelopes/SDK arrays. No event ring can overflow.
        private sealed class LossCounts
        {
            public long resetDiscardedWindows, resetDiscardedAttempts, resetDiscardedPending;
            public long tailNotFlushedWindows, tailNotFlushedAttempts; // discarded tails, not successful writes
            public long tailFlushAttempts, tailFlushedWindows, tailFlushedAttempts, tailFlushFailures, tailFlushOversizeFailures;
            public long staleGenerationCompletions, lateWindowCompletions, parameterObservationFailures;
            public long rawCallsTruncated, rawBytesOmitted, requestTextTruncatedCalls;
            public long samplesOmittedForLineBudget, oversizeEmissionFailures, emissionFailures;
            internal LossCounts Capture() => (LossCounts)MemberwiseClone();
        }
        private sealed class RequestStamp
        {
            private readonly char[] _request = new char[160], _command = new char[160];
            private int _requestLength, _commandLength;
            private long _tick, _ordinal;
            private ulong _commandSequence, _sampleSequence, _wireTick;
            private bool _commandKnown, _truncated;
            internal void Set(long tick, long ordinal, CommandEnvelope binding, CommandEnvelope canonical,
                ulong commandSequence, bool commandKnown)
            {
                _tick = tick; _ordinal = ordinal; _commandSequence = commandSequence; _commandKnown = commandKnown;
                _sampleSequence = canonical.SampleSequence; _wireTick = canonical.MonotonicTick;
                _requestLength = commandKnown ? Math.Min(binding.RequestId.Length, _request.Length) : 0;
                _commandLength = commandKnown ? Math.Min(binding.Command.Length, _command.Length) : 0;
                binding.RequestId.AsSpan(0, _requestLength).CopyTo(_request);
                binding.Command.AsSpan(0, _commandLength).CopyTo(_command);
                _truncated = commandKnown && (Long(binding.RequestId) || Long(binding.Command));
            }
            internal object Capture() => new
            {
                tick = _tick, ordinal = _ordinal, commandSequence = _commandSequence,
                commandSequenceSource = _commandKnown ? "actual-command-envelope" : "last-accepted-command-context",
                requestId = _commandKnown ? new string(_request, 0, _requestLength) : null,
                command = _commandKnown ? new string(_command, 0, _commandLength) : null,
                sampleSequence = _sampleSequence, wireTickKnown = _wireTick != 0,
                wireMonotonicTick = _wireTick != 0 ? (ulong?)_wireTick : null, textTruncated = _truncated
            };
        }
        // Kind: 1=validated SD64, 2=current Sony data-only63, 3=typed HM parameters.
        // Typed values have no digital-trigger/touch/report semantics; no encoding is inferred.
        internal readonly record struct ControlPoint(int Kind, ulong Buttons, float Lt, float Rt,
            bool AnalogKnown, byte DigitalTriggers, byte Touch, byte Click);
        private struct ControlCounts
        {
            private long _calls, _analogCalls, _bothAnalog, _bothDigital, _touchCalls, _clickCalls;
            private long _firstBothOrdinal, _lastBothOrdinal;
            private ulong _buttonsOr, _buttonsAnd;
            private float _ltMin, _ltMax, _rtMin, _rtMax;
            private byte _digitalOr, _digitalAnd, _touchOr, _touchAnd, _clickOr, _clickAnd;
            internal void Record(ControlPoint p, long ordinal)
            {
                if (_calls++ == 0)
                {
                    _buttonsAnd = p.Buttons; _digitalAnd = p.DigitalTriggers;
                    _touchAnd = p.Touch; _clickAnd = p.Click;
                }
                _buttonsOr |= p.Buttons; _buttonsAnd &= p.Buttons;
                _digitalOr |= p.DigitalTriggers; _digitalAnd &= p.DigitalTriggers;
                _touchOr |= p.Touch; _touchAnd &= p.Touch; _clickOr |= p.Click; _clickAnd &= p.Click;
                if (p.DigitalTriggers == 3) ++_bothDigital;
                if (p.Touch != 0) ++_touchCalls;
                if (p.Click != 0) ++_clickCalls;
                if (!p.AnalogKnown) return;
                if (_analogCalls++ == 0) { _ltMin = _ltMax = p.Lt; _rtMin = _rtMax = p.Rt; }
                _ltMin = Math.Min(_ltMin, p.Lt); _ltMax = Math.Max(_ltMax, p.Lt);
                _rtMin = Math.Min(_rtMin, p.Rt); _rtMax = Math.Max(_rtMax, p.Rt);
                if (p.Lt > 0 && p.Rt > 0)
                {
                    if (_bothAnalog++ == 0) _firstBothOrdinal = ordinal;
                    _lastBothOrdinal = ordinal;
                }
            }
            internal readonly object? Capture(int kind) => _calls == 0 ? null : new
            {
                calls = _calls, buttonsOrHex = _buttonsOr.ToString("X16"), buttonsAndHex = _buttonsAnd.ToString("X16"),
                analogKnownCalls = _analogCalls, ltMin = _analogCalls == 0 ? (float?)null : _ltMin,
                ltMax = _analogCalls == 0 ? (float?)null : _ltMax, rtMin = _analogCalls == 0 ? (float?)null : _rtMin,
                rtMax = _analogCalls == 0 ? (float?)null : _rtMax, bothAnalogNonzeroCalls = _bothAnalog,
                firstBothAnalogOrdinal = _bothAnalog == 0 ? (long?)null : _firstBothOrdinal,
                lastBothAnalogOrdinal = _bothAnalog == 0 ? (long?)null : _lastBothOrdinal,
                bothDigitalCalls = kind == 3 ? (long?)null : _bothDigital,
                digitalTriggerMaskOr = kind == 3 ? (byte?)null : _digitalOr,
                digitalTriggerMaskAnd = kind == 3 ? (byte?)null : _digitalAnd,
                touchCalls = kind == 3 ? (long?)null : _touchCalls, clickCalls = kind == 3 ? (long?)null : _clickCalls,
                touchMaskOr = kind == 3 ? (byte?)null : _touchOr, touchMaskAnd = kind == 3 ? (byte?)null : _touchAnd,
                clickMaskOr = kind == 3 ? (byte?)null : _clickOr, clickMaskAnd = kind == 3 ? (byte?)null : _clickAnd
            };
        }
        private struct AxisCounts
        {
            internal long Present, Missing, Nonfinite;
            internal float Min, Max;
            internal void Record(bool present, float value)
            {
                if (!present) { ++Missing; return; }
                if (!float.IsFinite(value)) { ++Nonfinite; return; }
                if (Present++ == 0) Min = Max = value;
                Min = Math.Min(Min, value); Max = Math.Max(Max, value);
            }
            internal readonly object Capture(HMAxis axis) => new
            {
                axis = axis.ToString(), finiteCalls = Present, missingCalls = Missing, nonfiniteCalls = Nonfinite,
                min = Present == 0 ? (float?)null : Min, max = Present == 0 ? (float?)null : Max
            };
        }
        private sealed class ParameterWindow
        {
            private static readonly HMAxis[] TypedAxes = { HMAxis.X, HMAxis.Y, HMAxis.Rx, HMAxis.Ry, HMAxis.Z, HMAxis.Rz };
            private readonly byte[] _or = new byte[HexBytes], _and = new byte[HexBytes];
            private readonly RequestStamp _first = new(), _last = new();
            private readonly Counts[] _reasons = new Counts[5];
            private readonly AxisCounts[] _axes = new AxisCounts[6];
            private ControlCounts _attempted, _returnedControls, _thrownControls;
            private int _bytes, _minLength, _maxLength, _kind;
            private long _rawCalls, _typedCalls, _missingCalls, _rawOnlyCalls, _mixedCalls, _hatOtherCalls;
            private ulong _hatValues;
            internal long Calls, Returned, Threw, ObservationFailures;
            internal long Pending => Calls - Returned - Threw;
            internal void Begin(long now, long ordinal, CommandEnvelope binding, CommandEnvelope canonical,
                ulong commandSequence, bool commandKnown, int reason)
            {
                if (Calls++ == 0) _first.Set(now, ordinal, binding, canonical, commandSequence, commandKnown);
                _last.Set(now, ordinal, binding, canonical, commandSequence, commandKnown);
                ++_reasons[reason].Calls;
            }
            internal ControlPoint Observe(CommandEnvelope binding, byte[]? report, HMGamepadState? state, long ordinal, LossCounts loss)
            {
                if (Long(binding.RequestId) || Long(binding.Command)) ++loss.requestTextTruncatedCalls;
                ControlPoint point = default;
                if (report is not null)
                {
                    int n = Math.Min(report.Length, HexBytes), previousBytes = _bytes;
                    bool first = _rawCalls++ == 0;
                    _minLength = first ? report.Length : Math.Min(_minLength, report.Length);
                    _maxLength = Math.Max(_maxLength, report.Length); _bytes = Math.Max(_bytes, n);
                    for (int i = 0; i < _bytes; ++i)
                    {
                        byte value = i < n ? report[i] : (byte)0;
                        _or[i] |= value;
                        _and[i] = first ? value : (byte)((i < previousBytes ? _and[i] : 0) & value);
                    }
                    if (report.Length > HexBytes)
                    { ++loss.rawCallsTruncated; loss.rawBytesOmitted += report.Length - HexBytes; }
                    if (binding.Persona == "steamdeck" && binding.ProfileIdentity == "steam-deck-composite")
                    {
                        // Reuse numeric fields only; the decoder's cached-report scope does NOT apply here.
                        var d = SteamDeckPadReportObservation.Decode(report);
                        if (d.ButtonBits is ulong bits && d.LeftTrigger is ushort lt && d.RightTrigger is ushort rt)
                            point = new(1, bits, lt, rt, true,
                                (byte)((d.LeftTriggerButton == true ? 1 : 0) | (d.RightTriggerButton == true ? 2 : 0)),
                                (byte)((d.LeftTouch == true ? 1 : 0) | (d.RightTouch == true ? 2 : 0)),
                                (byte)((d.LeftClick == true ? 1 : 0) | (d.RightClick == true ? 2 : 0)));
                    }
                    else if (binding.Persona == "dualsense-edge" && binding.ProfileIdentity == "dualsense-edge" &&
                        report.Length == 63 && (report[7] & 15) <= 8)
                        point = new(2, MsiPersonaReportChain20261012.DecodeDualSenseDigitalMask(report), report[4], report[5], true,
                            (byte)((report[8] >> 2) & 3),
                            (byte)(((report[32] & 128) == 0 ? 1 : 0) | ((report[36] & 128) == 0 ? 2 : 0)),
                            (byte)((report[9] >> 1) & 1)); // Sony has one surface click, not two pad clicks.
                    if (point.Kind == 0) ++_rawOnlyCalls;
                }
                else if (state is { } typed)
                {
                    ++_typedCalls;
                    float lt = 0, rt = 0;
                    bool leftKnown = false, rightKnown = false;
                    // Inspect only six known parameter keys, never enumerate a possibly extensible dictionary.
                    for (int i = 0; i < TypedAxes.Length; ++i)
                    {
                        float value = 0;
                        bool present = typed.Axes is not null && typed.Axes.TryGetValue(TypedAxes[i], out value);
                        _axes[i].Record(present, value);
                        if (i == 4) { lt = value; leftKnown = present; }
                        if (i == 5) { rt = value; rightKnown = present; }
                    }
                    int hat = (int)typed.Hat;
                    if (hat is >= 0 and < 64) _hatValues |= 1UL << hat; else ++_hatOtherCalls;
                    point = new(3, unchecked((ulong)typed.Buttons), lt, rt,
                        leftKnown && rightKnown && float.IsFinite(lt) && float.IsFinite(rt), 0, 0, 0);
                }
                else ++_missingCalls;
                if (point.Kind != 0)
                {
                    if (_kind == 0) _kind = point.Kind;
                    if (_kind != point.Kind) { ++_mixedCalls; return default; } // never merge unlike bit/axis domains
                    _attempted.Record(point, ordinal);
                }
                return point;
            }
            internal void Complete(Attempt attempt, bool failed)
            {
                if (failed) { ++Threw; ++_reasons[attempt.Reason].Threw; }
                else { ++Returned; ++_reasons[attempt.Reason].Returned; }
                if (attempt.Parameters.Kind == 0) return;
                if (failed) _thrownControls.Record(attempt.Parameters, attempt.Ordinal);
                else _returnedControls.Record(attempt.Parameters, attempt.Ordinal);
            }
            internal object Capture(long generation, long id, long now, LossCounts loss)
            {
                var axes = _typedCalls == 0 ? null : new object[TypedAxes.Length];
                if (axes is not null) for (int i = 0; i < axes.Length; ++i) axes[i] = _axes[i].Capture(TypedAxes[i]);
                return new
                {
                    version = 1, bindingGeneration = generation, windowId = id, observedThroughTick = now,
                    boundary = "since-last-successful-sample-or-tail-write;reset-on-controller-or-full-binding-change",
                    tuple = "entry.request;shared-by-first-and-last;bindingGeneration-is-controller/binding-generation",
                    scope = "actual-Host-SDK-arguments-only;not-system-wide;excludes-SDK-internal-writers,UU-internals,driver/game-consumption",
                    acceptance = "SDK-void-return-is-not-acceptance-or-delivery", accepted = (long?)null,
                    tail = "enabled-normal-reset/binding-change-flush;disabled-or-failed-flush-counted;kill/reboot-may-lose-unwritten-tail",
                    attempts = Calls, sdkReturned = Returned, sdkThrew = Threw, pendingCompletions = Pending,
                    first = _first.Capture(), last = _last.Capture(),
                    liveFrame = _reasons[0], internalTouch = _reasons[1], neutral = _reasons[2], internalIdle = _reasons[3], other = _reasons[4],
                    rawArguments = _rawCalls, typedArguments = _typedCalls, missingArguments = _missingCalls,
                    rawOnlyCalls = _rawOnlyCalls, mixedSemanticDomainCallsOmitted = _mixedCalls, observationFailures = ObservationFailures,
                    rawBytes = new
                    {
                        scope = "attempts;byte-index-in-actual-argument;missing-indices-contribute-zero-to-AND;no-anomaly-classification",
                        minLength = _rawCalls == 0 ? (int?)null : _minLength, maxLength = _rawCalls == 0 ? (int?)null : _maxLength,
                        prefixBudget = HexBytes, coveredPrefixBytes = _bytes,
                        orHex = Convert.ToHexString(_or.AsSpan(0, _bytes)), andHex = Convert.ToHexString(_and.AsSpan(0, _bytes)),
                        counterAndImu = "included-in-raw;changes-are-not-input-anomalies;excluded-from-control-state-counts"
                    },
                    controls = new
                    {
                        domain = _kind switch { 1 => "SD64-button-bits;trigger-u16", 2 => "Sony-data-only63-digital-mask;trigger-u8",
                            3 => "HM-typed-buttons-and-Z/Rz-only;no-encoded-report-or-touch-semantics", _ => "unavailable-unknown-persona-or-shape" },
                        masks = "trigger/touch:bit0-left,bit1-right;Sony-click:bit0-single-surface;SD64-click:bit0-left,bit1-right",
                        bothAnalog = "same-call-LT>0-and-RT>0;raw-parameter-domain;no-deadzone-or-consumer-inference",
                        attempted = _attempted.Capture(_kind), sdkReturned = _returnedControls.Capture(_kind), sdkThrew = _thrownControls.Capture(_kind)
                    },
                    typed = axes is null ? null : new
                    {
                        scope = "six-HM-axis-parameters-only;other-keys-and-encoded-report-unavailable;unknown-persona-has-no-wire-semantics",
                        axes, hatValuesMaskHex = _hatValues.ToString("X16"), hatOutsideMaskCalls = _hatOtherCalls
                    },
                    loss = loss.Capture() // immutable copy, also safe when returned to a later export serializer
                };
            }
            internal void Clear()
            {
                Calls = Returned = Threw = ObservationFailures = 0;
                _rawCalls = _typedCalls = _missingCalls = _rawOnlyCalls = _mixedCalls = _hatOtherCalls = 0;
                _bytes = _minLength = _maxLength = _kind = 0; _hatValues = 0;
                _attempted = _returnedControls = _thrownControls = default;
                Array.Clear(_or); Array.Clear(_and); Array.Clear(_reasons); Array.Clear(_axes);
            }
        }
        private void WriteBounded(string line)
        {
            var text = line + "\n";
            var bytes = Encoding.UTF8.GetByteCount(text);
            if (bytes > MaximumLineBytes) return;
            lock (WriterGate)
            {
                Directory.CreateDirectory(_directory);
                var file = Path.Combine(_directory, "input-host-signal.log");
                if (File.Exists(file) && new FileInfo(file).Length + bytes > MaximumFileBytes)
                    File.Move(file, file + ".prev", true);
                File.AppendAllText(file, text, new UTF8Encoding(false));
            }
        }
    }
}
