using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

// Pure OXP3 lighting protocol for the existing one-shot helper. The frozen HC
// assembly predates this model, so the helper performs only the reviewed
// source-derived HID v1 writes and never constructs or replaces an HC device.
internal interface IOxp3HidTransport : IDisposable
{
    string DevicePath { get; }
    bool IsConnected { get; }
    bool Write(byte[] report, CancellationToken token);
    void CancelPendingIo();
}

internal readonly record struct Oxp3Rgb(byte R, byte G, byte B);

internal sealed class Oxp3WindowsHidTransport : IOxp3HidTransport
{
    internal const ushort VendorId = 0x1A86;
    internal const ushort ProductId = 0xFE00;
    internal const ushort UsagePage = 0xFF00;
    internal const ushort Usage = 0x0001;
    internal const int ReportLength = 65;

    private readonly FileStream stream;
    private readonly string devicePath;
    private readonly Action? writeEntered;
    private readonly Action? writeIssued;
    private bool disposed;
    private Oxp3WindowsHidTransport(FileStream stream, string devicePath, Action? writeEntered = null,
        Action? writeIssued = null)
    {
        this.stream = stream;
        this.devicePath = devicePath;
        this.writeEntered = writeEntered;
        this.writeIssued = writeIssued;
    }
    public string DevicePath => devicePath;
    public bool IsConnected => !disposed && !stream.SafeFileHandle.IsInvalid && !stream.SafeFileHandle.IsClosed;

    internal static Oxp3WindowsHidTransport Open(string? expectedPath = null, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("HID lighting requires Windows");
        var matches = new List<string>();
        Native.HidD_GetHidGuid(out var hidGuid);
        using var devices = Native.SetupDiGetClassDevs(ref hidGuid, null, IntPtr.Zero, 0x12);
        if (devices.IsInvalid) throw Failure("enumerate HID interfaces");
        for (uint index = 0; ; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();
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
                var path = Marshal.PtrToStringUni(IntPtr.Add(detail, 4)) ?? throw new IOException("empty HID path");
                if (!path.Contains("vid_1a86&pid_fe00", StringComparison.OrdinalIgnoreCase)) continue;
                using var probe = Native.CreateFile(path, 0, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
                if (probe.IsInvalid) throw Failure("inspect OXP3 HID");
                if (IsOxp3VendorInterface(probe)) matches.Add(path);
            }
            finally { Marshal.FreeHGlobal(detail); }
        }
        if (matches.Count != 1)
            throw new IOException($"expected exactly one OXP3 vendor HID interface (1A86:FE00 FF00:0001), found {matches.Count}");
        if (!string.IsNullOrWhiteSpace(expectedPath) && !Oxp3OwnerLease.PathsMatch(expectedPath, matches[0]))
            throw new InvalidOperationException("OXP3 owner interface path does not match the sole vendor collection");
        cancellationToken.ThrowIfCancellationRequested();
        var handle = File.OpenHandle(matches[0], FileMode.Open, FileAccess.ReadWrite,
            FileShare.ReadWrite, FileOptions.Asynchronous);
        if (cancellationToken.IsCancellationRequested)
        {
            handle.Dispose();
            cancellationToken.ThrowIfCancellationRequested();
        }
        try
        {
            // Recheck after opening the writable endpoint, before any report.
            if (!IsOxp3VendorInterface(handle)) throw new IOException("OXP3 HID identity changed while opening");
            return new Oxp3WindowsHidTransport(new FileStream(handle, FileAccess.ReadWrite, ReportLength, true), matches[0]);
        }
        catch { handle.Dispose(); throw; }
    }

    internal static Oxp3WindowsHidTransport ForTest(SafeFileHandle handle, string devicePath,
        FileAccess access = FileAccess.ReadWrite, Action? writeEntered = null, Action? writeIssued = null) =>
        new(new FileStream(handle, access, ReportLength, true), devicePath, writeEntered, writeIssued);

    private static bool IsOxp3VendorInterface(SafeFileHandle handle)
    {
        var attributes = new Native.HidAttributes { Size = Marshal.SizeOf<Native.HidAttributes>() };
        if (!Native.HidD_GetAttributes(handle, ref attributes)) throw Failure("read HID attributes");
        if (attributes.VendorId != VendorId || attributes.ProductId != ProductId) return false;
        if (!Native.HidD_GetPreparsedData(handle, out var preparsed)) throw Failure("read HID preparsed data");
        try
        {
            if (Native.HidP_GetCaps(preparsed, out var caps) != 0x00110000)
                throw new IOException("cannot read OXP3 HID capabilities");
            if (caps.UsagePage != UsagePage || caps.Usage != Usage) return false;
            if (caps.OutputReportByteLength != ReportLength)
                throw new IOException($"OXP3 HID output length {caps.OutputReportByteLength} is not {ReportLength}");
            return true;
        }
        finally { Native.HidD_FreePreparsedData(preparsed); }
    }

    public bool Write(byte[] report, CancellationToken token)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (report.Length != ReportLength || report[0] != 0)
            throw new ArgumentException("OXP3 requires report ID 0 plus 64 payload bytes");
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(token, timeout.Token);
        LastWriteIssuedIncomplete = false;
        LastWriteTokenCancelledAtIssue = false;
        LastWriteCancelledWhileIncomplete = false;
        writeEntered?.Invoke();
        try
        {
            var write = stream.WriteAsync(report.AsMemory(), linked.Token);
            LastWriteIssuedIncomplete = !write.IsCompleted;
            LastWriteTokenCancelledAtIssue = token.IsCancellationRequested;
            writeIssued?.Invoke();
            write.AsTask().GetAwaiter().GetResult();
            return true;
        }
        catch (OperationCanceledException)
        {
            LastWriteCancelledWhileIncomplete = LastWriteIssuedIncomplete && !LastWriteTokenCancelledAtIssue;
            CancelPendingIo();
            throw;
        }
    }

    internal bool LastCancelIoResult { get; private set; }
    internal int LastCancelIoError { get; private set; }
    internal int CancelIoCalls { get; private set; }
    internal bool AnyCancelIoSucceeded { get; private set; }
    internal List<int> CancelIoOutcomes { get; } = new List<int>();
    internal bool LastWriteIssuedIncomplete { get; private set; }
    internal bool LastWriteTokenCancelledAtIssue { get; private set; }
    internal bool LastWriteCancelledWhileIncomplete { get; private set; }

    public void CancelPendingIo()
    {
        if (disposed || stream.SafeFileHandle.IsInvalid)
        {
            LastCancelIoResult = false;
            LastCancelIoError = 0;
            return;
        }
        CancelIoCalls++;
        LastCancelIoResult = Native.CancelIoEx(stream.SafeFileHandle, IntPtr.Zero);
        if (LastCancelIoResult) AnyCancelIoSucceeded = true;
        LastCancelIoError = LastCancelIoResult ? 0 : Marshal.GetLastWin32Error();
        CancelIoOutcomes.Add(LastCancelIoResult ? 0 : LastCancelIoError);
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
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool CancelIoEx(SafeFileHandle handle, IntPtr overlapped);
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

// Source order and waits are copied from OneXPlayer3.ApplyLeds at the pinned
// f2e58468 source. The transport is injected so no real HID is touched in tests.
internal sealed class Oxp3LightingWriter
{
    internal const byte LeftStickZone = 0x01;
    internal const byte RightStickZone = 0x02;
    internal const byte GKeyZone = 0x05;
    internal const byte LogoZone = 0x06;
    internal const int ZoneDelay = 150;
    internal const int SafeDelay = ZoneDelay * 2;
    internal const int StickDelay = ZoneDelay * 4;
    internal const int ClassicPreset = 1000;
    internal static readonly Oxp3Rgb ClassicColor = new(183, 48, 0);

    private readonly IOxp3HidTransport transport;
    private readonly Action<int> wait;
    private readonly CancellationToken cancellationToken;
    private readonly Action? ownerCheck;
    private int brightness = 100;
    private bool isPreset;
    private bool breathing;
    private Oxp3Rgb main = new(0, 0, 0);
    private Oxp3Rgb second = new(0, 0, 0);
    private int preset;

    internal Oxp3LightingWriter(IOxp3HidTransport transport, Action<int>? wait = null,
        CancellationToken cancellationToken = default, Action? ownerCheck = null)
    {
        this.transport = transport;
        this.wait = wait ?? Thread.Sleep;
        this.cancellationToken = cancellationToken;
        this.ownerCheck = ownerCheck;
    }

    internal int Writes { get; private set; }
    internal int FailedWrites { get; private set; }
    internal List<string> Operations { get; } = [];
    internal List<int> WaitsMs { get; } = [];
    internal bool LastResult { get; private set; }

    internal bool ApplyColor(Oxp3Rgb mainColor, Oxp3Rgb secondColor, bool breathingLevel, int requestedBrightness)
    {
        main = mainColor; second = secondColor; brightness = Math.Clamp(requestedBrightness, 0, 100);
        isPreset = false; breathing = breathingLevel;
        return ApplyLeds();
    }

    internal bool ApplyBrightness(int requestedBrightness, Oxp3Rgb mainColor, Oxp3Rgb secondColor) =>
        ApplyColor(mainColor, secondColor, false, requestedBrightness);

    internal bool ApplyOff()
    {
        brightness = 0; isPreset = false; breathing = false;
        return ApplyLeds();
    }

    internal bool ApplyPreset(int requestedPreset, Oxp3Rgb secondColor, int requestedBrightness)
    {
        preset = requestedPreset; second = secondColor; brightness = Math.Clamp(requestedBrightness, 0, 100);
        isPreset = true; breathing = false;
        return ApplyLeds();
    }

    private bool ApplyLeds()
    {
        if (!transport.IsConnected) throw new IOException("OXP3 lighting HID unavailable");
        var ok = true;
        Wait(SafeDelay);
        if (brightness > 0)
        {
            ok &= SendBrightness(GKeyZone);
            Wait(SafeDelay);
            ok &= SendSolidColor(second, LogoZone, breathing);
            Wait(SafeDelay);
            ok &= SendBrightness(LogoZone);
            Wait(ZoneDelay);
            ok &= SendSolidColor(second, GKeyZone, breathing);
            Wait(SafeDelay);
        }
        else
        {
            ok &= SendBrightness(LogoZone);
            Wait(SafeDelay);
            ok &= SendBrightness(GKeyZone);
            Wait(SafeDelay);
        }
        Wait(SafeDelay);
        if (brightness > 0)
        {
            foreach (var zone in new[] { LeftStickZone, RightStickZone })
            {
                if (!isPreset)
                    ok &= SendSolidColor(main, zone, false);
                else if (preset == ClassicPreset)
                    ok &= SendSolidColor(ClassicColor, zone, false);
                else
                    ok &= SendPreset(zone);
                Wait(StickDelay);
            }
        }
        foreach (var zone in new[] { LeftStickZone, RightStickZone })
        {
            ok &= SendBrightness(zone);
            Wait(StickDelay);
        }
        LastResult = ok;
        return ok;
    }

    private bool SendBrightness(byte side)
    {
        var enabled = brightness > 0;
        byte level = !enabled || brightness <= 33 ? (byte)0x01 : brightness <= 66 ? (byte)0x03 : (byte)0x04;
        Operations.Add($"brightness:{ZoneName(side)}:{brightness}:{level}");
        return Write(BuildV1Command(0xB8, 0x01, new byte[] { 0xFD, side, 0x02, (byte)(enabled ? 1 : 0), 0x05, level }));
    }

    private bool SendSolidColor(Oxp3Rgb color, byte side, bool breathingMode)
    {
        var command = new List<byte> { breathingMode ? (byte)0xF0 : (byte)0xFE, side, 0x02 };
        for (var i = 0; i < 18; i++)
        {
            command.Add(color.R); command.Add(color.G); command.Add(color.B);
        }
        command.Add(color.R); command.Add(color.G);
        Operations.Add($"color:{ZoneName(side)}:{color.R},{color.G},{color.B}:{breathingMode}");
        return Write(BuildV1Command(0xB8, 0x01, command));
    }

    private bool SendPreset(byte side)
    {
        Operations.Add($"preset:{ZoneName(side)}:{preset}");
        return Write(BuildV1Command(0xB8, 0x01, new byte[] { (byte)preset, side, 0x02 }));
    }

    private bool Write(byte[] payload)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ownerCheck?.Invoke();
        Writes++;
        if (transport.Write(WithReportID(payload), cancellationToken)) return true;
        FailedWrites++;
        return false;
    }

    private void Wait(int delay)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (ownerCheck is null)
        {
            WaitsMs.Add(delay);
            wait(delay);
            cancellationToken.ThrowIfCancellationRequested();
            return;
        }
        // Slice the source wait so generator/interface revocation is observed
        // before a later non-return write, without adding a new reader or thread.
        var remaining = delay;
        while (remaining > 0)
        {
            var slice = Math.Min(remaining, 50);
            WaitsMs.Add(slice);
            wait(slice);
            remaining -= slice;
            ownerCheck();
        }
        ownerCheck();
    }

    private static string ZoneName(byte side) => side switch
    {
        LeftStickZone => "left",
        RightStickZone => "right",
        GKeyZone => "gkey",
        LogoZone => "logo",
        _ => side.ToString()
    };

    internal static byte[] BuildV1Command(byte cid, byte idx, IList<byte> cmd)
    {
        const int size = 64;
        var buffer = new byte[size];
        var offset = 0;
        buffer[offset++] = cid;
        buffer[offset++] = 0x3F;
        buffer[offset++] = idx;
        var maxCmdBytes = size - 5;
        var cmdLen = Math.Min(cmd.Count, maxCmdBytes);
        for (var i = 0; i < cmdLen; i++) buffer[offset++] = cmd[i];
        buffer[size - 2] = 0x3F;
        buffer[size - 1] = cid;
        return buffer;
    }

    internal static byte[] WithReportID(byte[] payload, byte reportID = 0x00, int reportLen = 64)
    {
        var buffer = new byte[1 + reportLen];
        buffer[0] = reportID;
        var len = Math.Min(payload.Length, reportLen);
        Buffer.BlockCopy(payload, 0, buffer, 1, len);
        return buffer;
    }
}
