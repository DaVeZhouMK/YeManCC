using System;
using System.Collections.Generic;
using System.Threading;

namespace Ymcc.CapabilityPilot.HcReference;

/// <summary>A curve argument snapshot at the AsusACPI call boundary, before ACPI normalization.</summary>
public sealed record CapturedFanCurveWrite(
    int Ordinal, string Fan, byte[] Curve, int BufferIdentity, int SyntheticReturnValue);

public sealed record RecordedFanCurveRun(
    IReadOnlyList<CapturedFanCurveWrite> Writes, int? LegacyReturnValue);

/// <summary>Only records memory. No device, transport, HC runtime, or native library is referenced.</summary>
public static class CurveWriteCapture
{
    private sealed class Session
    {
        internal readonly List<CapturedFanCurveWrite> Writes = new();
        internal readonly Dictionary<byte[], int> Buffers = new(ReferenceEqualityComparer.Instance);
    }

    private static readonly AsyncLocal<Session?> Active = new();

    public static RecordedFanCurveRun Capture(Action action)
    {
        var session = Begin();
        try
        {
            action();
            return new RecordedFanCurveRun(session.Writes.ToArray(), null);
        }
        finally { Active.Value = null; }
    }

    public static RecordedFanCurveRun CaptureReturning(Func<int> action)
    {
        var session = Begin();
        try
        {
            int result = action();
            return new RecordedFanCurveRun(session.Writes.ToArray(), result);
        }
        finally { Active.Value = null; }
    }

    private static Session Begin()
    {
        if (Active.Value is not null)
            throw new InvalidOperationException("Nested curve recording is not supported.");
        var session = new Session();
        Active.Value = session;
        return session;
    }

    /// <summary>Record only. Return 1000 + ordinal, a synthetic test sentinel with no firmware meaning.</summary>
    public static int Record(string fan, byte[] curve)
    {
        var session = Active.Value ?? throw new InvalidOperationException("A capture session is required.");
        if (!session.Buffers.TryGetValue(curve, out int identity))
        {
            identity = session.Buffers.Count + 1;
            session.Buffers.Add(curve, identity);
        }
        int ordinal = session.Writes.Count + 1;
        int result = 1000 + ordinal;
        session.Writes.Add(new CapturedFanCurveWrite(ordinal, fan, (byte[])curve.Clone(), identity, result));
        return result;
    }
}

/// <summary>Data-only shell. It deliberately does not run the actual HC PowerProfile constructor.</summary>
public sealed class PowerProfile
{
    public FanProfile FanProfile { get; set; } = new();
}

/// <summary>Only the two members read by the extracted branch; no smoothing or policy implementation.</summary>
public sealed class FanProfile
{
    public FanMode fanMode;
    public double[] fanSpeeds = Array.Empty<double>();
}

/// <summary>Compile-time scaffolding for verbatim overrides. IsOpen is only an in-memory boolean.</summary>
public abstract class HcRogSourceOracleBase
{
    public bool IsOpen { get; set; } = true;
    public abstract void PowerProfileManager_Applied(PowerProfile profile, UpdateSource source);
    public abstract void SetFanControl(bool enable, int mode = 0);
}

/// <summary>The sole replacement for the original terminal AsusACPI.SetFanCurve call.</summary>
internal static class AsusACPI
{
    internal static int SetFanCurve(AsusFan fan, byte[] curve) => CurveWriteCapture.Record(fan.ToString(), curve);
}

/// <summary>Public test accessors around the byte-identical generated HC declarations.</summary>
public sealed partial class HcRogSourceOracle
{
    public HcRogSourceOracle(bool isOpen = true) { IsOpen = isOpen; }

    // Preserve the original conversion return/alias semantics, including the invalid-input CPU default.
    public static byte[] ConvertCurve(double[]? fanSpeeds) => ToAsusCurve(fanSpeeds!);
    public static byte[] DefaultCpuCurve => (byte[])defaultCPUFan.Clone();
    public static byte[] DefaultGpuCurve => (byte[])defaultGPUFan.Clone();

    public static RecordedFanCurveRun ApplySoftware(double[]? fanSpeeds, bool isOpen = true) =>
        ApplyProfile(FanMode.Software, fanSpeeds, isOpen);

    public static RecordedFanCurveRun ApplyProfile(
        FanMode fanMode, double[]? fanSpeeds, bool isOpen = true, UpdateSource source = UpdateSource.Background)
    {
        var oracle = new HcRogSourceOracle(isOpen);
        var profile = new PowerProfile { FanProfile = new FanProfile { fanMode = fanMode, fanSpeeds = fanSpeeds! } };
        return CurveWriteCapture.Capture(() => oracle.PowerProfileManager_Applied(profile, source));
    }

    public static RecordedFanCurveRun RestoreDefaultCurves(bool isOpen = true) =>
        SetFanControlCaptured(false, isOpen);

    public static RecordedFanCurveRun SetFanControlCaptured(bool enable, bool isOpen = true, int mode = 0)
    {
        var oracle = new HcRogSourceOracle(isOpen);
        return CurveWriteCapture.Capture(() => oracle.SetFanControl(enable, mode));
    }
}