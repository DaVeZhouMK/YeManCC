using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.Json;

namespace YeManInputHost.Diagnostics;

// One instance per validated release command, owned by the caller's existing
// lifecycle synchronization. No SDK, device, logging, tuple or frame access.
// Begin/End/Complete/Finish allocate nothing. Null duration means unknown;
// untouched stages are omitted. Timing never establishes endpoint absence.
internal sealed class HostReleaseTimingEvidence
{
    internal enum Stage
    {
        LifecycleGate, SteamDeckIdleStop, DiagnosticsBefore, Neutral,
        OutputReaderStop, ControllerDispose, ReportChainClose, RemoveAllAttempt1,
        EndpointWaitAttempt1, RemoveAllAttempt2, EndpointWaitAttempt2,
        ContextDispose, FinalObservation
    }

    internal const string EvidenceField = "releaseTimingEvidence";
    internal const int MaximumResponseBytes = 64 * 1024;
    internal const int MaximumEvidenceBytes = 2048;
    private const int StageCount = 13;
    private const int MaximumRootProperties = 128;
    private static readonly UTF8Encoding StrictUtf8 = new(false, true);
    private enum State : byte { Untouched, Running, Measured, Unknown }
    private struct Measurement
    {
        internal State State;
        internal long Started;
        internal long ElapsedUs;
    }

    private readonly Measurement[] _stages = new Measurement[StageCount];
    private readonly Func<long> _clock;
    private readonly long _frequency;
    private long _started, _lastTick;
    private long? _totalUs;
    private int _active = -1;
    private bool _clockUnknown, _finished, _completionSeen, _completionUnknown;
    private bool? _localReleaseOk;

    private HostReleaseTimingEvidence(Func<long> clock, long frequency)
    {
        _clock = clock;
        _frequency = frequency;
        if (TryRead(out var started)) _started = started;
    }

    internal static HostReleaseTimingEvidence? TryCreate()
    {
        try { return new(Stopwatch.GetTimestamp, Stopwatch.Frequency); }
        catch { return null; }
    }

    // Test-only clock injection; no Host or SDK is linked into the fixture.
    internal static HostReleaseTimingEvidence? TryCreateForTest(Func<long>? clock,
        long frequency = 1_000_000)
    {
        try { return clock is null || frequency <= 0 ? null : new(clock, frequency); }
        catch { return null; }
    }

    private bool TryRead(out long tick)
    {
        tick = 0;
        if (_clockUnknown) return false;
        try
        {
            tick = _clock();
            if (tick < 0 || tick < _lastTick) { _clockUnknown = true; return false; }
            _lastTick = tick;
            return true;
        }
        catch { _clockUnknown = true; return false; }
    }

    private long? Microseconds(long ticks)
    {
        try
        {
            if (ticks < 0 || _frequency <= 0) return null;
            // Decimal avoids long multiplication overflow and floating-point
            // rounding; truncate only after converting to microseconds.
            var value = (decimal)ticks * 1_000_000m / _frequency;
            return value > long.MaxValue ? null : (long)value;
        }
        catch { return null; }
    }

    private void UnknownActive()
    {
        if (_active >= 0) _stages[_active].State = State.Unknown;
        _active = -1;
    }

    internal void Begin(Stage stage)
    {
        try
        {
            var index = (int)stage;
            if ((uint)index >= StageCount) { _clockUnknown = true; UnknownActive(); return; }
            ref var entry = ref _stages[index];
            if (_finished || entry.State != State.Untouched || _active >= 0)
            {
                UnknownActive();
                entry.State = State.Unknown;
                return;
            }
            if (!TryRead(out var tick)) { entry.State = State.Unknown; return; }
            entry.Started = tick;
            entry.State = State.Running;
            _active = index;
        }
        catch { _clockUnknown = true; }
    }

    internal void End(Stage stage)
    {
        try
        {
            var index = (int)stage;
            if ((uint)index >= StageCount) { _clockUnknown = true; UnknownActive(); return; }
            ref var entry = ref _stages[index];
            if (_finished || entry.State != State.Running || _active != index)
            {
                UnknownActive();
                entry.State = State.Unknown;
                return;
            }
            _active = -1;
            var elapsed = TryRead(out var tick) ? Microseconds(tick - entry.Started) : null;
            entry.State = elapsed.HasValue ? State.Measured : State.Unknown;
            entry.ElapsedUs = elapsed.GetValueOrDefault();
        }
        catch { _clockUnknown = true; }
    }

    // This is the caller's logical return value, independent of timing health.
    // Missing or conflicting completion calls remain explicitly unknown.
    internal void Complete(bool localReleaseOk)
    {
        try
        {
            if (_completionUnknown) return;
            if (_completionSeen && _localReleaseOk != localReleaseOk)
            { _completionUnknown = true; _localReleaseOk = null; return; }
            _completionSeen = true;
            _localReleaseOk = localReleaseOk;
        }
        catch { _completionUnknown = true; _localReleaseOk = null; }
    }

    internal void Finish()
    {
        try
        {
            if (_finished) return;
            _finished = true;
            UnknownActive(); // An unmatched Begin is never an elapsed success.
            _totalUs = TryRead(out var tick) ? Microseconds(tick - _started) : null;
        }
        catch { _clockUnknown = true; _totalUs = null; }
    }

    private static string Name(Stage stage) => stage switch
    {
        Stage.LifecycleGate => "lifecycleGate",
        Stage.SteamDeckIdleStop => "steamDeckIdleStop",
        Stage.DiagnosticsBefore => "diagnosticsBefore",
        Stage.Neutral => "neutral",
        Stage.OutputReaderStop => "outputReaderStop",
        Stage.ControllerDispose => "controllerDispose",
        Stage.ReportChainClose => "reportChainClose",
        Stage.RemoveAllAttempt1 => "removeAllAttempt1",
        Stage.EndpointWaitAttempt1 => "endpointWaitAttempt1",
        Stage.RemoveAllAttempt2 => "removeAllAttempt2",
        Stage.EndpointWaitAttempt2 => "endpointWaitAttempt2",
        Stage.ContextDispose => "contextDispose",
        Stage.FinalObservation => "finalObservation",
        _ => throw new ArgumentOutOfRangeException(nameof(stage))
    };

    private string SerializeEvidence()
    {
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream))
        {
            writer.WriteStartObject();
            writer.WriteNumber("version", 1);
            writer.WriteBoolean("diagnosticOnly", true);
            writer.WriteString("scope", "release-local-call;excludes-handler-queue-and-response-serialization");
            writer.WriteString("stageUnit", "microseconds");
            writer.WriteBoolean("finished", _finished);
            var total = _clockUnknown ? null : _totalUs;
            if (total.HasValue) writer.WriteNumber("totalUs", total.Value);
            else writer.WriteNull("totalUs");
            if (_localReleaseOk.HasValue) writer.WriteBoolean("localReleaseOk", _localReleaseOk.Value);
            else writer.WriteNull("localReleaseOk");
            writer.WriteStartObject("stages");
            long? sum = 0;
            for (var i = 0; i < StageCount; ++i)
            {
                var entry = _stages[i];
                if (entry.State == State.Untouched) continue;
                var name = Name((Stage)i);
                if (!_clockUnknown && entry.State == State.Measured)
                {
                    writer.WriteNumber(name, entry.ElapsedUs);
                    if (sum.HasValue) sum = checked(sum.Value + entry.ElapsedUs);
                }
                else { writer.WriteNull(name); sum = null; }
            }
            writer.WriteEndObject();
            if (total.HasValue && sum.HasValue && sum.Value <= total.Value)
                writer.WriteNumber("unmeasuredUs", total.Value - sum.Value);
            writer.WriteEndObject();
        }
        if (stream.Length > MaximumEvidenceBytes) throw new InvalidOperationException("Evidence budget");
        return StrictUtf8.GetString(stream.GetBuffer(), 0, checked((int)stream.Length));
    }

    // Preserve the exact original response bytes/fields; never reserialize its
    // tuple or infer release success from the evidence. Any failure is inert.
    internal string TryAppend(string response, int maxBytes)
    {
        try
        {
            if (response is null || maxBytes <= 0 || maxBytes > MaximumResponseBytes ||
                response.Length > maxBytes) return response!;
            var originalBytes = StrictUtf8.GetByteCount(response);
            if (originalBytes > maxBytes) return response;
            using var document = JsonDocument.Parse(response, new JsonDocumentOptions { MaxDepth = 16 });
            if (document.RootElement.ValueKind != JsonValueKind.Object) return response;
            var fields = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in document.RootElement.EnumerateObject())
                if (!fields.Add(property.Name) || fields.Count > MaximumRootProperties ||
                    property.NameEquals(EvidenceField)) return response;
            var end = response.Length - 1;
            while (end >= 0 && response[end] is ' ' or '\t' or '\r' or '\n') --end;
            if (end < 0 || response[end] != '}') return response;
            var evidence = SerializeEvidence();
            var suffix = (fields.Count == 0 ? "" : ",") + "\"" + EvidenceField + "\":" + evidence;
            if (originalBytes + StrictUtf8.GetByteCount(suffix) > maxBytes) return response;
            return response[..end] + suffix + response[end..];
        }
        catch { return response; }
    }
}