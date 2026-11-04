// Development-only external counters. No process, thread, device or application control.
using System;
using System.ComponentModel;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Ymcc.RogField.V1
{
    internal sealed class QueryHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        internal QueryHandle(IntPtr value) : base(true) { SetHandle(value); }
        protected override bool ReleaseHandle() { return Native.CloseHandle(handle); }
    }

    internal static class Native
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct FileTime
        {
            internal uint Low;
            internal uint High;
            internal ulong Value { get { return ((ulong)High << 32) | Low; } }
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct PowerStatus
        {
            internal byte AcLineStatus, BatteryFlag, BatteryLifePercent, SystemStatusFlag;
            internal uint BatteryLifeTime, BatteryFullLifeTime;
        }
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern IntPtr OpenProcess(uint access, bool inherit, uint processId);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern uint GetProcessId(QueryHandle handle);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool GetProcessTimes(QueryHandle handle, out FileTime creation,
            out FileTime exit, out FileTime kernel, out FileTime user);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool QueryFullProcessImageName(QueryHandle handle, uint flags,
            StringBuilder path, ref uint size);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern uint WaitForSingleObject(QueryHandle handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool QueryProcessCycleTime(QueryHandle handle, out ulong cycles);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool QueryPerformanceCounter(out long value);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool QueryPerformanceFrequency(out long value);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern uint GetActiveProcessorCount(ushort group);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool GetSystemPowerStatus(out PowerStatus status);
    }

    public sealed class CounterRead
    {
        public string Status { get; set; }
        public string UtcBefore { get; set; }
        public string UtcAfter { get; set; }
        public string QpcBefore { get; set; }
        public string QpcAfter { get; set; }
        public string QpcMidpoint { get; set; }
        public string CreationFileTime100ns { get; set; }
        // Undefined while alive. Never use this value for liveness.
        public string ExitFileTime100nsDiagnostic { get; set; }
        public bool ExitFileTimeDefined { get; set; }
        public string Kernel100ns { get; set; }
        public string User100ns { get; set; }
        public string Cycles { get; set; }
        public string WaitBefore { get; set; }
        public string WaitAfter { get; set; }
        public bool TimesSucceeded { get; set; }
        public bool CyclesSucceeded { get; set; }
        public bool LiveBeforeAndAfter { get; set; }
        public int TimesWin32Error { get; set; }
        public int CyclesWin32Error { get; set; }
        public int WaitBeforeWin32Error { get; set; }
        public int WaitAfterWin32Error { get; set; }
        public string Error { get; set; }
    }

    public sealed class PowerRead
    {
        public string Status { get; set; }
        public string Source { get; set; }
        public int Win32Error { get; set; }
        public int? AcLineStatus { get; set; }
        public int? BatteryFlag { get; set; }
        public int? BatteryPercent { get; set; }
    }

    public sealed class DiskCompanionFile
    {
        public string Name { get; set; }
        public string Path { get; set; }
        public string Status { get; set; }
        public string DiskSha256 { get; set; }
        public string SizeBytes { get; set; }
        public string BytesHashed { get; set; }
        public string SizeBytesAfterRead { get; set; }
        public string LastWriteUtcBefore { get; set; }
        public string LastWriteUtcAfter { get; set; }
        public string Error { get; set; }
    }

    public sealed class DiskCompanionSnapshot
    {
        public string UtcBefore { get; set; }
        public string UtcAfter { get; set; }
        public string Status { get; set; }
        public string Directory { get; set; }
        public string Scope { get; set; }
        public DiskCompanionFile[] Files { get; set; }
    }

    // Separate from native counters: invoked only at Host binding and finalization.
    // These are same-directory disk witnesses, never a claim that a process loaded them.
    public static class DiskCompanionIdentity
    {
        private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;

        public static DiskCompanionSnapshot Read(string nativeHostImagePath)
        {
            string[] names = { "YeManInputHost.dll", "HIDMaestro.Core.dll", "YeManInputHost.runtimeconfig.json" };
            DiskCompanionSnapshot snapshot = new DiskCompanionSnapshot {
                UtcBefore = DateTime.UtcNow.ToString("o", Invariant), Status = "OK",
                Scope = "DISK_COMPANION_IDENTITY_ONLY_NOT_LOADED_MODULE_PROOF; best-effort sequential reads; not an atomic directory snapshot",
                Files = new DiskCompanionFile[names.Length]
            };
            for (int index = 0; index < names.Length; index++)
            {
                DiskCompanionFile file = new DiskCompanionFile { Name = names[index], Status = "UNKNOWN" };
                snapshot.Files[index] = file;
                try
                {
                    string directory = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(nativeHostImagePath));
                    snapshot.Directory = directory;
                    file.Path = System.IO.Path.Combine(directory, names[index]);
                    // Share reads/writes/deletion: never lock product updates/configuration on purpose.
                    using (System.IO.FileStream stream = new System.IO.FileStream(file.Path, System.IO.FileMode.Open,
                        System.IO.FileAccess.Read, System.IO.FileShare.ReadWrite | System.IO.FileShare.Delete))
                    using (System.Security.Cryptography.SHA256 algorithm = System.Security.Cryptography.SHA256.Create())
                    {
                        long lengthBefore = stream.Length;
                        file.SizeBytes = lengthBefore.ToString(Invariant);
                        file.LastWriteUtcBefore = System.IO.File.GetLastWriteTimeUtc(file.Path).ToString("o", Invariant);
                        file.DiskSha256 = BitConverter.ToString(algorithm.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
                        file.BytesHashed = stream.Position.ToString(Invariant);
                        file.SizeBytesAfterRead = stream.Length.ToString(Invariant);
                        file.LastWriteUtcAfter = System.IO.File.GetLastWriteTimeUtc(file.Path).ToString("o", Invariant);
                        if (file.SizeBytes != file.SizeBytesAfterRead || file.BytesHashed != file.SizeBytes ||
                            file.LastWriteUtcBefore != file.LastWriteUtcAfter)
                            file.Error = "File changed during disk read; successful raw fields retained, identity UNKNOWN.";
                        else file.Status = "OK";
                    }
                }
                catch (Exception error) { file.Status = "UNKNOWN"; file.Error = error.GetType().Name + ": " + error.Message; }
                if (file.Status != "OK") snapshot.Status = "UNKNOWN";
            }
            snapshot.UtcAfter = DateTime.UtcNow.ToString("o", Invariant);
            return snapshot;
        }
    }
    public sealed class ReadOnlyCounters : IDisposable
    {
        private readonly QueryHandle held;
        public uint ProcessId { get; private set; }
        public const uint QueryLimitedInformationAndSynchronize = 0x00101000;
        private static readonly CultureInfo Invariant = CultureInfo.InvariantCulture;

        private ReadOnlyCounters(QueryHandle handle, uint processId)
        { held = handle; ProcessId = processId; }

        public static ReadOnlyCounters Open(uint processId)
        {
            IntPtr raw = Native.OpenProcess(QueryLimitedInformationAndSynchronize, false, processId);
            if (raw == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "OpenProcess");
            QueryHandle handle = new QueryHandle(raw);
            try
            {
                uint actual = Native.GetProcessId(handle);
                if (actual == 0) throw new Win32Exception(Marshal.GetLastWin32Error(), "GetProcessId");
                if (actual != processId) throw new InvalidOperationException("Held handle PID mismatch.");
                return new ReadOnlyCounters(handle, actual);
            }
            catch { handle.Dispose(); throw; }
        }

        public string ImagePath()
        {
            StringBuilder path = new StringBuilder(32768);
            uint size = (uint)path.Capacity;
            if (!Native.QueryFullProcessImageName(held, 0, path, ref size))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "QueryFullProcessImageName");
            return path.ToString();
        }

        public static long QpcNow()
        {
            long value;
            if (!Native.QueryPerformanceCounter(out value))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "QueryPerformanceCounter");
            return value;
        }

        public static long QpcFrequency()
        {
            long value;
            if (!Native.QueryPerformanceFrequency(out value) || value <= 0)
                throw new InvalidOperationException("QueryPerformanceFrequency unavailable.");
            return value;
        }

        public static uint LogicalProcessors()
        {
            uint value = Native.GetActiveProcessorCount(0xffff);
            if (value == 0) throw new Win32Exception(Marshal.GetLastWin32Error(), "GetActiveProcessorCount");
            return value;
        }

        public static PowerRead ReadPowerOnce()
        {
            Native.PowerStatus value;
            if (!Native.GetSystemPowerStatus(out value))
                return new PowerRead { Status = "UNKNOWN", Source = "UNKNOWN",
                    Win32Error = Marshal.GetLastWin32Error() };
            return new PowerRead { Status = value.AcLineStatus == 255 ? "UNKNOWN" : "OK",
                Source = value.AcLineStatus == 0 ? "BATTERY" : value.AcLineStatus == 1 ? "AC" : "UNKNOWN",
                AcLineStatus = value.AcLineStatus, BatteryFlag = value.BatteryFlag,
                BatteryPercent = value.BatteryLifePercent == 255 ? (int?)null : value.BatteryLifePercent };
        }

        public CounterRead Read()
        {
            CounterRead result = new CounterRead { Status = "COUNTER_ERROR", UtcBefore = DateTime.UtcNow.ToString("o", Invariant) };
            try
            {
                long before = QpcNow();
                result.QpcBefore = before.ToString(Invariant);
                uint waitBefore = Native.WaitForSingleObject(held, 0);
                result.WaitBeforeWin32Error = waitBefore == 0xffffffff ? Marshal.GetLastWin32Error() : 0;
                result.WaitBefore = waitBefore.ToString(Invariant);
                Native.FileTime creation, exit, kernel, user;
                result.TimesSucceeded = Native.GetProcessTimes(held, out creation, out exit, out kernel, out user);
                result.TimesWin32Error = result.TimesSucceeded ? 0 : Marshal.GetLastWin32Error();
                if (result.TimesSucceeded)
                {
                    result.CreationFileTime100ns = creation.Value.ToString(Invariant);
                    result.ExitFileTime100nsDiagnostic = exit.Value.ToString(Invariant);
                    result.Kernel100ns = kernel.Value.ToString(Invariant);
                    result.User100ns = user.Value.ToString(Invariant);
                }
                ulong cycles;
                result.CyclesSucceeded = Native.QueryProcessCycleTime(held, out cycles);
                result.CyclesWin32Error = result.CyclesSucceeded ? 0 : Marshal.GetLastWin32Error();
                if (result.CyclesSucceeded) result.Cycles = cycles.ToString(Invariant);
                uint waitAfter = Native.WaitForSingleObject(held, 0);
                result.WaitAfterWin32Error = waitAfter == 0xffffffff ? Marshal.GetLastWin32Error() : 0;
                result.WaitAfter = waitAfter.ToString(Invariant);
                long after = QpcNow();
                result.QpcAfter = after.ToString(Invariant);
                result.QpcMidpoint = (before + (after - before) / 2).ToString(Invariant);
                result.ExitFileTimeDefined = result.TimesSucceeded && waitAfter == 0;
                result.LiveBeforeAndAfter = waitBefore == 258 && waitAfter == 258;
                if ((waitBefore != 0 && waitBefore != 258) || (waitAfter != 0 && waitAfter != 258))
                    result.Error = "WaitForSingleObject did not return WAIT_TIMEOUT or WAIT_OBJECT_0.";
                else if (!result.TimesSucceeded || !result.CyclesSucceeded)
                    result.Error = "GetProcessTimes and/or QueryProcessCycleTime failed; keep successful raw fields.";
                else if (after < before)
                    result.Error = "Non-monotonic QPC.";
                else if (!result.LiveBeforeAndAfter)
                { result.Status = "EXITED"; result.Error = "Held process handle signaled exit."; }
                else result.Status = "OK";
            }
            catch (Exception error) { result.Error = error.GetType().Name + ": " + error.Message; }
            result.UtcAfter = DateTime.UtcNow.ToString("o", Invariant);
            return result;
        }

        public void Dispose() { held.Dispose(); }
    }
}