using System;

namespace YeManFanBackend.Asus;

/**
 * HC-SLIM-01 R3 §3/§4.2：ASUS 曲线写入叶组件。
 *
 * 来源：`HandheldCompanion/Devices/ASUS/ROGAlly.cs`
 *  - `defaultCPUFan`/`defaultGPUFan`（@223-224）
 *  - `ToAsusCurve(double[])`（@226-249）
 *  - `PowerProfileManager_Applied` 的 Software 三写（@251-259）
 *  - `SetFanControl(false)` 的默认表交还（@370-384）
 *
 * 按函数抽取，不继承 ROGAlly 整类：仅保留默认 CPU/GPU 表、11 点→ASUS 16 字节曲线转换，
 * 以及 CPU/GPU/Mid 写序与默认表交还。旧返回值由 AsusACPI 的 legacy 语义给出；
 * 每次写入的收据经 AsusAcpiObservations 逐扇发布。
 */
public static class AsusFanCurveWriter
{
    /**
     * 会话内首次硬件动作前冻结并记录的组件版本（R3 §6.2②）。
     * 与 provider 选路一同进入交回证据。
     */
    public const string ComponentVersion = "YeManFanBackend/1.0.0";

    // 逐字镜像 ROGAlly.cs：ASUS 默认 CPU/GPU 风扇表（前 8 温度，后 8 占空）。
    private static readonly byte[] defaultCPUFan = new byte[] { 0x3A, 0x3D, 0x40, 0x44, 0x48, 0x4D, 0x51, 0x62, 0x08, 0x11, 0x16, 0x1A, 0x22, 0x29, 0x30, 0x45 };
    private static readonly byte[] defaultGPUFan = new byte[] { 0x3A, 0x3D, 0x40, 0x44, 0x48, 0x4D, 0x51, 0x62, 0x0C, 0x16, 0x1D, 0x1F, 0x26, 0x2D, 0x34, 0x4A };

    /// <summary>默认 CPU 表副本（调用方可安全持有，不暴露内部静态数组）。</summary>
    public static byte[] DefaultCpuCurve => (byte[])defaultCPUFan.Clone();

    /// <summary>默认 GPU 表副本。</summary>
    public static byte[] DefaultGpuCurve => (byte[])defaultGPUFan.Clone();

    /**
     * 逐字镜像 ROGAlly.ToAsusCurve：11 点手柄曲线 → ASUS 16 字节曲线。
     * 输入非 11 点（含 null）时原样返回默认 CPU 表（保持原语义）。
     */
    public static byte[] ToAsusCurve(double[]? fanSpeeds)
    {
        if (fanSpeeds is null || fanSpeeds.Length != 11)
            return defaultCPUFan;

        int[] anchorTemps = { 20, 30, 40, 50, 60, 70, 80, 90 }; // °C
        int[] sourceIdx = { 2, 3, 4, 5, 6, 7, 8, 9 }; // map to 0..100 steps

        byte[] curve = new byte[16];

        // first 8: temps
        for (int i = 0; i < 8; i++)
            curve[i] = (byte)anchorTemps[i];

        // last 8: duties (clamped 0..100, monotonic non-decreasing)
        byte last = 0;
        for (int i = 0; i < 8; i++)
        {
            byte duty = (byte)Math.Max(0, Math.Min(100, Math.Round(fanSpeeds[sourceIdx[i]])));
            if (duty < last) duty = last;   // ensure monotonic
            curve[8 + i] = last = duty;
        }
        return curve;
    }

    /**
     * 镜像 ROGAlly.PowerProfileManager_Applied 的 Software 分支：CPU/GPU/Mid 依序写入同一曲线
     * （复用同一数组，与 HC 行为一致）。
     * 返回最后一次（Mid）写入的旧返回值；三通道各自的真实结果在 AsusAcpiObservations 收据中。
     */
    public static int ApplySoftwareCurve(byte[] asusCurve)
    {
        AsusACPI.SetFanCurveObserved(AsusFan.CPU, asusCurve, out _);
        AsusACPI.SetFanCurveObserved(AsusFan.GPU, asusCurve, out _);
        return AsusACPI.SetFanCurveObserved(AsusFan.Mid, asusCurve, out _);
    }

    /**
     * 镜像 ROGAlly.SetFanControl(false)：交还 ASUS 默认表（CPU=默认CPU表、GPU=默认GPU表、Mid=默认CPU表）。
     * 返回最后一次（Mid）写入的旧返回值；三通道收据逐扇发布。
     */
    public static int RestoreDefaultCurves()
    {
        AsusACPI.SetFanCurveObserved(AsusFan.CPU, defaultCPUFan, out _);
        AsusACPI.SetFanCurveObserved(AsusFan.GPU, defaultGPUFan, out _);
        return AsusACPI.SetFanCurveObserved(AsusFan.Mid, defaultCPUFan, out _);
    }
}
