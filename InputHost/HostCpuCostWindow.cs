using System;
using System.Collections.Generic;

namespace YeManInputHost.Diagnostics;

// Diagnostic-only cumulative counters. A valid zero is a baseline; an absent
// read is not measured zero. The caller owns sampling and the original gate.
internal sealed class HostCpuCostWindow
{
    private readonly record struct Sample(double TotalMs, long Tick);
    internal readonly record struct Measurement(long? MsPerSec, long ObservationTick, long? WindowMs, string Status);

    private Sample? _process;
    private Dictionary<int, Sample> _threads = new();
    private Dictionary<int, Sample> _observedThreads = new();

    internal Measurement ObserveProcess(double? totalMs, long tick)
    {
        var current = ValidSample(totalMs, tick);
        var result = Measure(_process, current, tick);
        _process = current;
        return result;
    }

    internal void BeginThreadObservation() => _observedThreads.Clear();

    internal Measurement ObserveThread(int id, double? totalMs, long tick)
    {
        var current = ValidSample(totalMs, tick);
        Sample? previous = _threads.TryGetValue(id, out var found) ? found : null;
        if (current is { } sample) _observedThreads[id] = sample;
        return Measure(previous, current, tick);
    }

    internal void FinishThreadObservation()
    {
        // Retain only successfully observed threads. A departed/failed thread
        // returning later must establish a new baseline, not contribute lifetime CPU.
        (_threads, _observedThreads) = (_observedThreads, _threads);
        _observedThreads.Clear();
    }

    internal void Reset()
    {
        _process = null;
        _threads.Clear();
        _observedThreads.Clear();
    }

    private static Sample? ValidSample(double? totalMs, long tick) =>
        totalMs is double value && double.IsFinite(value) && value >= 0 ? new(value, tick) : null;

    private static Measurement Measure(Sample? previous, Sample? current, long tick)
    {
        if (current is null) return new(null, tick, null, "read-unavailable");
        if (previous is null) return new(null, tick, null, "baseline");
        var windowMs = current.Value.Tick - previous.Value.Tick;
        if (windowMs <= 0) return new(null, tick, windowMs, "invalid-window");
        var delta = current.Value.TotalMs - previous.Value.TotalMs;
        if (delta < 0) return new(null, tick, windowMs, "counter-regressed");
        var rate = Math.Round(delta * 1000.0 / windowMs);
        if (!double.IsFinite(rate) || rate >= long.MaxValue)
            return new(null, tick, windowMs, "invalid-rate");
        return new((long)rate, tick, windowMs, "measured");
    }
}
