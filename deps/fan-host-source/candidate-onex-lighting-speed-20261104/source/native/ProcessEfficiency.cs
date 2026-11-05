// Shared startup policy; this file also stays compatible with .NET Framework csc 4.
namespace YeManCC
{
    using System;
    using System.Runtime.InteropServices;
    using System.Threading;

    internal static class ProcessEfficiency
    {
        private const int ProcessPowerThrottling = 4;
        private const uint ExecutionSpeed = 1;
        private const uint IdlePriorityClass = 0x40;
        private static int applied;

        [StructLayout(LayoutKind.Sequential)]
        private struct PowerThrottlingState
        {
            public uint Version;
            public uint ControlMask;
            public uint StateMask;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetProcessInformation(IntPtr process, int informationClass,
            ref PowerThrottlingState information, uint informationSize);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetProcessInformation(IntPtr process, int informationClass,
            ref PowerThrottlingState information, uint informationSize);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetPriorityClass(IntPtr process, uint priorityClass);

        internal static void ApplyCurrentProcessOnce()
        {
            if (Interlocked.Exchange(ref applied, 1) != 0)
                return;

            // GetCurrentProcess pseudo-handle: no other process is opened or changed.
            IntPtr process = new IntPtr(-1);
            try
            {
                var state = new PowerThrottlingState { Version = 1 };
                if (GetProcessInformation(process, ProcessPowerThrottling, ref state, 12))
                {
                    // A failed read must not replace unknown control/state bits.
                    state.ControlMask |= ExecutionSpeed;
                    state.StateMask |= ExecutionSpeed;
                    SetProcessInformation(process, ProcessPowerThrottling, ref state, 12);
                }
            }
            catch (Exception)
            {
                // Unsupported APIs (including old Windows entry points) are optional.
            }

            try
            {
                // Independent of EcoQoS support or failure.
                SetPriorityClass(process, IdlePriorityClass);
            }
            catch (Exception)
            {
                // Startup must continue even if the priority request is unavailable.
            }
        }
    }
}
