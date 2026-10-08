using System.Runtime.InteropServices;

namespace HandheldCompanion.Targets.Viiper
{
    internal static class ViiperXInput
    {
        [DllImport("xinput1_4.dll", EntryPoint = "XInputSetState")]
        public static extern uint SetState(uint dwUserIndex, ref ViiperXInputVibration pVibration);

        public const uint ErrorSuccess = 0;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ViiperXInputVibration
    {
        public ushort LeftMotorSpeed;
        public ushort RightMotorSpeed;
    }
}
