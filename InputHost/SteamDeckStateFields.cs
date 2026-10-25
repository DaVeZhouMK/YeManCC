using HIDMaestro;

internal static partial class Program
{
    // Per-frame values only; no SDK state or mutable axis dictionary is retained.
    private readonly record struct SteamDeckStateFields(
        HMButton Buttons, HMHat Hat,
        float X, float Y, float Rx, float Ry, float Z, float Rz,
        short AccelX, short AccelY, short AccelZ,
        short GyroPitch, short GyroYaw, short GyroRoll);
}
