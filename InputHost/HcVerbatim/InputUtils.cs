// ── HcVerbatim: InputUtils（BuildReport 依赖子集）────────────────────────
// HC verbatim 06c0b954: deps/handheldcompanion-runtime/source/Utils/InputUtils.cs
// 仅提取 Targets BuildReport 消费的静态 helpers（Clamp/RoundClampToShort/
// ClampToUShort/ClampToByte/NegateClampToShort/RoundToInt/deg2rad/
// NormalizeXboxInput），逐行保留常量与方法体。MotionInput/MotionOutput 等
// 非 BuildReport 依赖，不提取。
using System;
using System.Runtime.CompilerServices;

namespace YeManInputHost.HcVerbatim;

public static class InputUtils
{
    private const float SHORT_MAX_F = 32767f;
    private const float BYTE_MAX_F = 255f;
    private const float INV_USHORT_MAX = 1f / 65535f; // for mapping [-32768..32767] to [0..255]
    private const float DEG2RAD = (float)Math.PI / 180f;
    private const float RAD2DEG = 180f / (float)Math.PI;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Clamp(float value, float min, float max) => MathF.Min(max, MathF.Max(min, value));

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int RoundToInt(float value) => (int)MathF.Round(value);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static short RoundClampToShort(float value) => (short)Math.Clamp(RoundToInt(value), short.MinValue, short.MaxValue);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static ushort ClampToUShort(int value) => (ushort)Math.Clamp(value, ushort.MinValue, ushort.MaxValue);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static ushort ClampToUShort(int value, int min, int max) => (ushort)Math.Clamp(value, min, max);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static byte ClampToByte(int value) => (byte)Math.Clamp(value, byte.MinValue, byte.MaxValue);

    public static short NegateClampToShort(short value)
    {
        int neg = -value;
        if (neg > short.MaxValue) return short.MaxValue;
        if (neg < short.MinValue) return short.MinValue;
        return (short)neg;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float rangeMap(float value, float minIn, float maxIn, float minOut, float maxOut)
    {
        float inRange = maxIn - minIn;
        float outRange = maxOut - minOut;
        return minOut + outRange * ((value - minIn) / inRange);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float deg2rad(float degrees) => degrees * DEG2RAD;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float rad2deg(float rad) => rad * RAD2DEG;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static byte NormalizeXboxInput(float input)
    {
        input = MathF.Min(SHORT_MAX_F, MathF.Max(-32768f, input));
        float scaled = (input + 32768f) * INV_USHORT_MAX * BYTE_MAX_F;
        return (byte)MathF.Round(scaled);
    }
}