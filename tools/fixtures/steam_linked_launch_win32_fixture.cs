// Win32 lifecycle fixture only; never starts or stops Steam/YMCC.
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.ComponentModel;
public static class SteamLinkedLaunchFixture {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct STARTUPINFO {
        public uint cb; public IntPtr lpReserved, lpDesktop, lpTitle;
        public uint dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public ushort wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public uint dwProcessId, dwThreadId; }
    [StructLayout(LayoutKind.Sequential)]
    public struct BASIC_LIMITS {
        public long ProcessTime, JobTime; public uint LimitFlags;
        public UIntPtr MinimumWorkingSet, MaximumWorkingSet; public uint ActiveProcessLimit;
        public UIntPtr Affinity; public uint PriorityClass, SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct IO_COUNTERS { public ulong A, B, C, D, E, F; }
    [StructLayout(LayoutKind.Sequential)]
    public struct EXTENDED_LIMITS {
        public BASIC_LIMITS Basic; public IO_COUNTERS Io;
        public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool CreateProcessW(string app, StringBuilder command, IntPtr pa, IntPtr ta,
        bool inherit, uint flags, IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool SetInformationJobObject(IntPtr job, int kind, ref EXTENDED_LIMITS limits, uint length);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll")] static extern bool TerminateJobObject(IntPtr job, uint code);
    [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle, uint ms);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr process, uint code);
    // Mirrors the production CreateProcess flags and Job lifecycle, not IPC itself.
    public static int Launch(string cscript, string vbs, bool synchronous) {
        IntPtr job = IntPtr.Zero; PROCESS_INFORMATION pi = new PROCESS_INFORMATION();
        try {
            if (synchronous) {
                job = CreateJobObjectW(IntPtr.Zero, null);
                if (job == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
                EXTENDED_LIMITS limits = new EXTENDED_LIMITS(); limits.Basic.LimitFlags = 0x2000; // KILL_ON_JOB_CLOSE
                if (!SetInformationJobObject(job, 9, ref limits, (uint)Marshal.SizeOf(limits)))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            STARTUPINFO si = new STARTUPINFO(); si.cb = (uint)Marshal.SizeOf(si);
            StringBuilder command = new StringBuilder("\"" + cscript + "\" //nologo \"" + vbs + "\"");
            if (!CreateProcessW(null, command, IntPtr.Zero, IntPtr.Zero, false,
                0x08000000u | (synchronous ? 4u : 0u), IntPtr.Zero, null, ref si, out pi))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            if (synchronous) {
                if (!AssignProcessToJobObject(job, pi.hProcess)) {
                    int error = Marshal.GetLastWin32Error();
                    TerminateProcess(pi.hProcess, 5); throw new Win32Exception(error);
                }
                ResumeThread(pi.hThread);
                if (WaitForSingleObject(pi.hProcess, 15000) != 0) throw new Exception("Fixture wrapper timed out");
                TerminateJobObject(job, 0); // Same cleanup as shell.run on successful root exit.
            }
            return (int)pi.dwProcessId;
        } finally {
            if (pi.hThread != IntPtr.Zero) CloseHandle(pi.hThread);
            if (pi.hProcess != IntPtr.Zero) CloseHandle(pi.hProcess);
            if (job != IntPtr.Zero) CloseHandle(job);
        }
    }
}
