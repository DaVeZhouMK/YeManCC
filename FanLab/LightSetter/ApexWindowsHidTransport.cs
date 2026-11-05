using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

// Opens only the native owner's already-selected APEX RGB collection. Device
// discovery remains in the native leased-HID owner. No controller reader or
// RGB initialization command is started by this one-shot helper.
internal sealed class ApexWindowsHidTransport : IApexLightingTransport
{
    private readonly FileStream stream;
    private readonly Func<bool> ownerCurrent;
    private bool disposed;
    public string DevicePath { get; }
    public bool IsConnected => !disposed && !stream.SafeFileHandle.IsInvalid && !stream.SafeFileHandle.IsClosed;
    private ApexWindowsHidTransport(FileStream stream, string path, Func<bool> ownerCurrent)
    { this.stream = stream; DevicePath = path; this.ownerCurrent = ownerCurrent; }

    internal static bool MatchesDescriptor(ushort vid, ushort pid, ushort page, ushort usage, ushort outputLength) =>
        vid == ApexLightingConsumer.VendorId && pid == ApexLightingConsumer.ProductId &&
        page == ApexLightingConsumer.UsagePage && usage == ApexLightingConsumer.Usage &&
        outputLength == ApexLightingConsumer.ReportLength;

    internal static ApexWindowsHidTransport Open(SuperJoyIdentity identity, Func<bool> ownerCurrent,
        CancellationToken cancellationToken)
    {
        if (!ApexLightingConsumer.MatchesOwnerIdentity(identity)) throw new ArgumentException("APEX_HID_IDENTITY_REQUIRED");
        Current();
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("APEX HID requires Windows");
        // A source path or native observation alone is insufficient. Recheck
        // the current descriptor both before and after opening for writes.
        using (var probe = Native.CreateFile(identity.DevicePath, 0, 3, IntPtr.Zero, 3, 0, IntPtr.Zero))
        {
            if (probe.IsInvalid) throw Failure("APEX_HID_PROBE_FAILED");
            RequireDescriptor(probe);
        }
        Current();
        var handle = File.OpenHandle(identity.DevicePath, FileMode.Open, FileAccess.ReadWrite,
            FileShare.ReadWrite, FileOptions.Asynchronous);
        try
        {
            RequireDescriptor(handle); Current();
            return new(new FileStream(handle, FileAccess.ReadWrite, ApexLightingConsumer.ReportLength, true), identity.DevicePath, ownerCurrent);
        }
        catch { handle.Dispose(); throw; }
        void Current()
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!ownerCurrent()) throw new OperationCanceledException("APEX_OWNER_RETIRED", cancellationToken);
        }
    }
    private static void RequireDescriptor(SafeFileHandle handle)
    {
        var attributes = new Native.HidAttributes { Size = Marshal.SizeOf<Native.HidAttributes>() };
        if (!Native.HidD_GetAttributes(handle, ref attributes)) throw Failure("APEX_HID_ATTRIBUTES_FAILED");
        if (!Native.HidD_GetPreparsedData(handle, out var preparsed)) throw Failure("APEX_HID_PREPARSED_FAILED");
        try
        {
            if (Native.HidP_GetCaps(preparsed, out var caps) != 0x00110000) throw new IOException("APEX_HID_CAPS_FAILED");
            if (!MatchesDescriptor(attributes.VendorId, attributes.ProductId, caps.UsagePage, caps.Usage, caps.OutputReportByteLength))
                throw new IOException("APEX_HID_DESCRIPTOR_MISMATCH");
        }
        finally { Native.HidD_FreePreparsedData(preparsed); }
    }
    public async ValueTask<int> WriteAsync(ReadOnlyMemory<byte> report, CancellationToken cancellationToken)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (report.Length != ApexLightingConsumer.ReportLength || report.Span[0] != 0 || report.Span[1] != 0x07 || report.Span[2] != 0xFF)
            throw new ArgumentException("APEX_FULL_WINDOWS_REPORT_REQUIRED");
        Current();
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(TimeSpan.FromSeconds(2));
        // FileStream.WriteAsync completes the requested memory or throws; it
        // exposes no partial-count success. Parent ownership remains until this
        // helper actually returns, including cancellation and process cleanup.
        await stream.WriteAsync(report, deadline.Token).ConfigureAwait(false);
        Current();
        return report.Length;
        void Current()
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!ownerCurrent()) throw new OperationCanceledException("APEX_OWNER_RETIRED", cancellationToken);
        }
    }
    public void CancelPendingIo()
    {
        if (disposed) return;
        if (!Native.CancelIoEx(stream.SafeFileHandle, IntPtr.Zero) && Marshal.GetLastWin32Error() != 1168)
            throw Failure("APEX_CANCEL_IO_FAILED");
    }
    public void Dispose()
    {
        if (disposed) return;
        // Set closed only after disposal completes. A throwing close must not
        // be presented as a released success by the consumer.
        stream.Dispose(); disposed = true;
    }
    private static IOException Failure(string operation) => new(operation + ": " + new Win32Exception(Marshal.GetLastWin32Error()).Message);
    private static class Native
    {
        [StructLayout(LayoutKind.Sequential)] internal struct HidAttributes { internal int Size; internal ushort VendorId, ProductId, Version; }
        [StructLayout(LayoutKind.Sequential)] internal struct HidCaps
        {
            internal ushort Usage, UsagePage, InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] internal ushort[] Reserved;
            internal ushort LinkCollectionNodes, InputButtonCaps, InputValueCaps, InputDataIndices;
            internal ushort OutputButtonCaps, OutputValueCaps, OutputDataIndices;
            internal ushort FeatureButtonCaps, FeatureValueCaps, FeatureDataIndices;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, EntryPoint = "CreateFileW")]
        internal static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("hid.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool HidD_GetAttributes(SafeFileHandle handle, ref HidAttributes attributes);
        [DllImport("hid.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool HidD_GetPreparsedData(SafeFileHandle handle, out IntPtr preparsed);
        [DllImport("hid.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.U1)]
        internal static extern bool HidD_FreePreparsedData(IntPtr preparsed);
        [DllImport("hid.dll")] internal static extern int HidP_GetCaps(IntPtr preparsed, out HidCaps caps);
        [DllImport("kernel32.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool CancelIoEx(SafeFileHandle file, IntPtr overlapped);
    }
}
