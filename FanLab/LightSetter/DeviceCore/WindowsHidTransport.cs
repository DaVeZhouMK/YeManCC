using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace YeManCC.Lighting;

// Native terminal only. It does not initialize HC, EC, OEM keys, a controller
// monitor, WMI, serial ports or any application/UI service.
internal sealed class WindowsHidTransport : ILightingHidTransport
{
    private readonly FileStream stream;
    private bool disposed;
    private WindowsHidTransport(FileStream stream) => this.stream = stream;
    public bool IsConnected => !disposed && !stream.SafeFileHandle.IsInvalid && !stream.SafeFileHandle.IsClosed;

    internal static ILightingHidTransport OpenX1Mini()
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("HID lighting requires Windows");
        var matches = new List<string>();
        Native.HidD_GetHidGuid(out var hidGuid);
        using var devices = Native.SetupDiGetClassDevs(ref hidGuid, null, IntPtr.Zero, 0x12);
        if (devices.IsInvalid) throw Failure("enumerate HID interfaces");
        for (uint index = 0; ; index++)
        {
            var info = new Native.DeviceInterfaceData { Size = Marshal.SizeOf<Native.DeviceInterfaceData>() };
            if (!Native.SetupDiEnumDeviceInterfaces(devices, IntPtr.Zero, ref hidGuid, index, ref info))
            {
                if (Marshal.GetLastWin32Error() == 259) break;
                throw Failure("enumerate HID interface");
            }
            Native.SetupDiGetDeviceInterfaceDetail(devices, ref info, IntPtr.Zero, 0, out var length, IntPtr.Zero);
            if (length < 8 || length > 1_048_576) throw new IOException("invalid HID interface detail length");
            var detail = Marshal.AllocHGlobal((int)length);
            try
            {
                Marshal.WriteInt32(detail, IntPtr.Size == 8 ? 8 : 6);
                if (!Native.SetupDiGetDeviceInterfaceDetail(devices, ref info, detail, length, out _, IntPtr.Zero))
                    throw Failure("read HID interface path");
                var path = Marshal.PtrToStringUni(IntPtr.Add(detail, 4))
                    ?? throw new IOException("empty HID path");
                // Narrow before even opening an inspection handle. Attributes
                // and usage are still verified; a path string is not identity.
                if (!path.Contains("vid_1a86&pid_fe00", StringComparison.OrdinalIgnoreCase)) continue;
                using var probe = Native.CreateFile(path, 0, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
                if (probe.IsInvalid) throw Failure("inspect X1 Mini HID");
                if (IsX1Mini(probe)) matches.Add(path);
            }
            finally { Marshal.FreeHGlobal(detail); }
        }
        if (matches.Count != 1)
            throw new IOException($"expected exactly one X1 Mini lighting HID (1A86:FE00 FF00:0001), found {matches.Count}");
        var handle = File.OpenHandle(matches[0], FileMode.Open, FileAccess.ReadWrite,
            FileShare.ReadWrite, FileOptions.Asynchronous);
        try
        {
            // Recheck after opening the writable endpoint, before any report.
            if (!IsX1Mini(handle)) throw new IOException("X1 Mini HID identity changed while opening");
            return new WindowsHidTransport(new FileStream(handle, FileAccess.ReadWrite, 65, true));
        }
        catch { handle.Dispose(); throw; }
    }

    private static bool IsX1Mini(SafeFileHandle handle)
    {
        var attributes = new Native.HidAttributes { Size = Marshal.SizeOf<Native.HidAttributes>() };
        if (!Native.HidD_GetAttributes(handle, ref attributes)) throw Failure("read HID attributes");
        if (attributes.VendorId != 0x1A86 || attributes.ProductId != 0xFE00) return false;
        if (!Native.HidD_GetPreparsedData(handle, out var preparsed)) throw Failure("read HID preparsed data");
        try
        {
            if (Native.HidP_GetCaps(preparsed, out var caps) != 0x00110000)
                throw new IOException("cannot read X1 Mini HID capabilities");
            if (caps.UsagePage != 0xFF00 || caps.Usage != 1) return false;
            // Source emits report ID + 64 bytes. Never silently truncate or
            // repad a report against an unreviewed descriptor.
            if (caps.OutputReportByteLength != 65)
                throw new IOException($"X1 Mini HID output length {caps.OutputReportByteLength} is not 65");
            return true;
        }
        finally { Native.HidD_FreePreparsedData(preparsed); }
    }

    public bool Write(byte[] report)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (report.Length != 65 || report[0] != 0)
            throw new ArgumentException("X1 Mini requires report ID 0 plus 64 payload bytes");
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
        // The async handle permits cancellation of a stuck write. No automatic
        // retries or alternate endpoints: failures must not duplicate a write.
        stream.WriteAsync(report.AsMemory(), timeout.Token).AsTask().GetAwaiter().GetResult();
        return true;
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        stream.Dispose();
    }

    private static IOException Failure(string operation) =>
        new(operation + ": " + new Win32Exception(Marshal.GetLastWin32Error()).Message);

    private static class Native
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct DeviceInterfaceData
        {
            internal int Size;
            internal Guid Guid;
            internal uint Flags;
            internal UIntPtr Reserved;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct HidAttributes
        {
            internal int Size;
            internal ushort VendorId, ProductId, Version;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct HidCaps
        {
            internal ushort Usage, UsagePage, InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] internal ushort[] Reserved;
            internal ushort LinkCollectionNodes, InputButtonCaps, InputValueCaps, InputDataIndices;
            internal ushort OutputButtonCaps, OutputValueCaps, OutputDataIndices;
            internal ushort FeatureButtonCaps, FeatureValueCaps, FeatureDataIndices;
        }
        internal sealed class DeviceInfoSet : SafeHandleZeroOrMinusOneIsInvalid
        {
            public DeviceInfoSet() : base(true) { }
            protected override bool ReleaseHandle() => SetupDiDestroyDeviceInfoList(handle);
        }
        [DllImport("hid.dll")] internal static extern void HidD_GetHidGuid(out Guid guid);
        [DllImport("hid.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.U1)] internal static extern bool HidD_GetAttributes(SafeFileHandle handle, ref HidAttributes attributes);
        [DllImport("hid.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.U1)] internal static extern bool HidD_GetPreparsedData(SafeFileHandle handle, out IntPtr data);
        [DllImport("hid.dll")]
        [return: MarshalAs(UnmanagedType.U1)] internal static extern bool HidD_FreePreparsedData(IntPtr data);
        [DllImport("hid.dll")] internal static extern int HidP_GetCaps(IntPtr data, out HidCaps caps);
        [DllImport("kernel32.dll", EntryPoint = "CreateFileW", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("setupapi.dll", EntryPoint = "SetupDiGetClassDevsW", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern DeviceInfoSet SetupDiGetClassDevs(ref Guid guid, string? enumerator, IntPtr parent, uint flags);
        [DllImport("setupapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool SetupDiEnumDeviceInterfaces(DeviceInfoSet devices, IntPtr data, ref Guid guid, uint index, ref DeviceInterfaceData info);
        [DllImport("setupapi.dll", EntryPoint = "SetupDiGetDeviceInterfaceDetailW", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool SetupDiGetDeviceInterfaceDetail(DeviceInfoSet devices, ref DeviceInterfaceData info, IntPtr detail, uint size, out uint required, IntPtr data);
        [DllImport("setupapi.dll")]
        [return: MarshalAs(UnmanagedType.Bool)] private static extern bool SetupDiDestroyDeviceInfoList(IntPtr devices);
    }
}
