using System.Buffers.Binary;

namespace YeManInputHost.Diagnostics;

// Data-only, bounded decoder. It neither originates a report nor reads a device.
// The caller copies at most 64 bytes under the existing nonblocking SDK lock.
internal readonly record struct SteamDeckPadReportObservation
{
    public int Version => 1;
    public string Status { get; init; }
    public uint? PacketCounter { get; init; }
    public ulong? ButtonBits { get; init; }
    public bool? LeftTriggerButton => ButtonBits is ulong b ? (b & (1UL << 1)) != 0 : null;
    public bool? RightTriggerButton => ButtonBits is ulong b ? (b & 1UL) != 0 : null;
    public bool? LeftStickClick => ButtonBits is ulong b ? (b & (1UL << 22)) != 0 : null;
    public bool? RightStickClick => ButtonBits is ulong b ? (b & (1UL << 26)) != 0 : null;
    public bool? SteamButton => ButtonBits is ulong b ? (b & (1UL << 13)) != 0 : null;
    public bool? QuickAccessButton => ButtonBits is ulong b ? (b & (1UL << 50)) != 0 : null;
    public bool? LeftTouch { get; init; }
    public bool? RightTouch { get; init; }
    public bool? LeftClick { get; init; }
    public bool? RightClick { get; init; }
    public short? LeftX { get; init; }
    public short? LeftY { get; init; }
    public short? RightX { get; init; }
    public short? RightY { get; init; }
    public ushort? LeftPressure { get; init; }
    public ushort? RightPressure { get; init; }
    public bool? OtherDigitalFieldsNonzero { get; init; }
    public bool? StickFieldsNonzero { get; init; }
    public short? LeftStickX { get; init; }
    public short? LeftStickY { get; init; }
    public short? RightStickX { get; init; }
    public short? RightStickY { get; init; }
    public int ReferenceStickDeadzone => 4096;
    public ushort? StickClassBits => LeftStickX is short lx && LeftStickY is short ly &&
        RightStickX is short rx && RightStickY is short ry
        ? (ushort)(StickClass(lx) | (StickClass(ly) << 3) | (StickClass(rx) << 6) | (StickClass(ry) << 9)) : null;
    public byte? PressureNonzeroMask => LeftPressure is ushort lp && RightPressure is ushort rp
        ? (byte)((lp != 0 ? 1 : 0) | (rp != 0 ? 2 : 0)) : null;
    public byte? PressureOutOfRangeMask => LeftPressure is ushort lp && RightPressure is ushort rp
        ? (byte)((lp > 32767 ? 1 : 0) | (rp > 32767 ? 2 : 0)) : null;
    public byte? InactivePadDataMask { get; init; }
    public bool? TriggerFieldsNonzero { get; init; }
    public ushort? LeftTrigger { get; init; }
    public ushort? RightTrigger { get; init; }
    public bool? ImuFieldsNonzero { get; init; }
    // Bits 0..9: accel X/Y/Z, gyro pitch/yaw/roll, quaternion W/X/Y/Z.
    public ushort? ImuNonzeroMask { get; init; }
    public short? AccelX { get; init; }
    public short? AccelY { get; init; }
    public short? AccelZ { get; init; }
    public short? GyroPitch { get; init; }
    public short? GyroYaw { get; init; }
    public short? GyroRoll { get; init; }
    public short? QuatW { get; init; }
    public short? QuatX { get; init; }
    public short? QuatY { get; init; }
    public short? QuatZ { get; init; }
    public bool SteamConsumptionProven => false;
    public bool TransferCompletedProven => false;
    public bool AtomicWithHostFrame => false;
    public string Scope => "Last cached SDK report copied under its hidLock; not an input transfer, current request, driver readback, or Steam receipt. A producer writing outside that lock is not ruled out.";

    // Diagnostic raw-axis reference, not a Steam deadzone or input filter.
    // Three bits per axis: 0=within +/-4096, 1=negative, 2=positive,
    // 3=-32767 full, 4=32767 full, 5=-32768 outside the Host builder domain.
    private static byte StickClass(short value) => value switch
    {
        < -32767 => 5, -32767 => 3, < -4096 => 1,
        <= 4096 => 0, < 32767 => 2, _ => 4
    };

    internal static SteamDeckPadReportObservation Decode(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != 64 || bytes[0] != 1 || bytes[1] != 0 || bytes[2] != 9 || bytes[3] != 64)
            return new() { Status = "unsupported-frame-shape" };
        var bits = BinaryPrimitives.ReadUInt64LittleEndian(bytes[8..]);
        static short I16(ReadOnlySpan<byte> b, int n) => BinaryPrimitives.ReadInt16LittleEndian(b[n..]);
        ushort imuNonzero = 0;
        for (int axis = 0; axis < 10; ++axis)
            if (I16(bytes, 24 + axis * 2) != 0) imuNonzero |= (ushort)(1 << axis);
        short lx = I16(bytes, 16), ly = I16(bytes, 18), rx = I16(bytes, 20), ry = I16(bytes, 22);
        ushort lp = BinaryPrimitives.ReadUInt16LittleEndian(bytes[56..]),
            rp = BinaryPrimitives.ReadUInt16LittleEndian(bytes[58..]);
        return new()
        {
            Status = "decoded-cached-steamdeck-report",
            PacketCounter = BinaryPrimitives.ReadUInt32LittleEndian(bytes[4..]), ButtonBits = bits,
            LeftTouch = (bits & (1UL << 19)) != 0, RightTouch = (bits & (1UL << 20)) != 0,
            LeftClick = (bits & (1UL << 17)) != 0, RightClick = (bits & (1UL << 18)) != 0,
            LeftX = lx, LeftY = ly, RightX = rx, RightY = ry,
            LeftPressure = lp, RightPressure = rp,
            InactivePadDataMask = (byte)(
                ((bits & (1UL << 19)) == 0 ? (lx != 0 ? 1 : 0) | (ly != 0 ? 2 : 0) | (lp != 0 ? 16 : 0) : 0) |
                ((bits & (1UL << 20)) == 0 ? (rx != 0 ? 4 : 0) | (ry != 0 ? 8 : 0) | (rp != 0 ? 32 : 0) : 0)),
            OtherDigitalFieldsNonzero = (bits & ~(15UL << 17)) != 0,
            StickFieldsNonzero = bytes[48..56].ContainsAnyExcept((byte)0),
            LeftStickX = I16(bytes, 48), LeftStickY = I16(bytes, 50),
            RightStickX = I16(bytes, 52), RightStickY = I16(bytes, 54),
            TriggerFieldsNonzero = bytes[44..48].ContainsAnyExcept((byte)0),
            LeftTrigger = BinaryPrimitives.ReadUInt16LittleEndian(bytes[44..]),
            RightTrigger = BinaryPrimitives.ReadUInt16LittleEndian(bytes[46..]),
            ImuFieldsNonzero = imuNonzero != 0, ImuNonzeroMask = imuNonzero,
            AccelX = I16(bytes, 24),
            AccelY = I16(bytes, 26),
            AccelZ = I16(bytes, 28),
            GyroPitch = I16(bytes, 30),
            GyroYaw = I16(bytes, 32),
            GyroRoll = I16(bytes, 34),
            QuatW = I16(bytes, 36),
            QuatX = I16(bytes, 38),
            QuatY = I16(bytes, 40),
            QuatZ = I16(bytes, 42)
        };
    }
}
