using System.Collections.Concurrent;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

// Windows terminal for the source-only SuperJoy dependency. No discovery,
// fallback or native-owner authority is created here. Program still refuses
// this unconnected model route before opening an HC/native endpoint.
internal sealed class SuperJoyWindowsHidTransport : ISuperJoyLightingTransport
{
    private readonly FileStream stream;
    private readonly Func<bool> ownerIsCurrent;
    private readonly CancellationToken ownerStop;
    private readonly CancellationTokenSource lifetime = new();
    private readonly Action? readIssuedForTest;
    private int closed;
    private SuperJoyWindowsHidTransport(FileStream stream, SuperJoyIdentity identity,
        Func<bool> ownerIsCurrent, CancellationToken ownerStop, Action? readIssuedForTest = null)
    { this.stream=stream;Identity=identity;this.ownerIsCurrent=ownerIsCurrent;this.ownerStop=ownerStop;this.readIssuedForTest=readIssuedForTest; }
    public SuperJoyIdentity Identity { get; }
    public bool IsConnected => Volatile.Read(ref closed)==0 && !stream.SafeFileHandle.IsClosed && !stream.SafeFileHandle.IsInvalid;
    internal bool LastReadIssuedIncomplete { get; private set; }
    internal bool LastReadTokenCancelledAtIssue { get; private set; }
    internal bool LastReadCancelledWhileIncomplete { get; private set; }
    internal ConcurrentQueue<int> CancelIoOutcomes { get; } = new();

    internal static SuperJoyWindowsHidTransport Open(SuperJoyIdentity identity, Func<bool> ownerIsCurrent,
        CancellationToken ownerStop, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(ownerIsCurrent);
        if(!identity.MatchesProtocol || !identity.DevicePath.StartsWith(@"\\?\hid#vid_1c4f&pid_0002",StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("SUPERJOY_OWNED_HID_PATH_REQUIRED",nameof(identity));
        cancellationToken.ThrowIfCancellationRequested();ownerStop.ThrowIfCancellationRequested();
        if(!ownerIsCurrent())throw new OperationCanceledException("SUPERJOY_OWNER_RETIRED");
        if(!OperatingSystem.IsWindows())throw new PlatformNotSupportedException("SuperJoy HID requires Windows");
        // Conservative exclusive open: fail on contention, never probe an
        // alternate endpoint or silently share a competing reader.
        var handle=File.OpenHandle(identity.DevicePath,FileMode.Open,FileAccess.ReadWrite,FileShare.None,FileOptions.Asynchronous);
        try
        {
            var attributes=new Native.HidAttributes{Size=Marshal.SizeOf<Native.HidAttributes>()};
            if(!Native.HidD_GetAttributes(handle,ref attributes))throw Failure("read SuperJoy HID attributes");
            if(attributes.VendorId!=identity.VendorId||attributes.ProductId!=identity.ProductId)
                throw new IOException("SUPERJOY_HID_ATTRIBUTES_MISMATCH");
            if(!Native.HidD_GetPreparsedData(handle,out var data))throw Failure("read SuperJoy HID descriptor");
            try
            {
                if(Native.HidP_GetCaps(data,out var caps)!=0x00110000||caps.UsagePage!=identity.UsagePage||caps.Usage!=identity.Usage||
                    caps.InputReportByteLength!=65||caps.OutputReportByteLength!=65)
                    throw new IOException("SUPERJOY_HID_DESCRIPTOR_MISMATCH");
            }
            finally{Native.HidD_FreePreparsedData(data);}
            cancellationToken.ThrowIfCancellationRequested();ownerStop.ThrowIfCancellationRequested();
            if(!ownerIsCurrent())throw new OperationCanceledException("SUPERJOY_OWNER_RETIRED");
            return new(new FileStream(handle,FileAccess.ReadWrite,65,true),identity,ownerIsCurrent,ownerStop);
        }
        catch{handle.Dispose();throw;}
    }
    internal static SuperJoyWindowsHidTransport ForTest(SafeFileHandle handle,SuperJoyIdentity identity,
        Func<bool> ownerIsCurrent,CancellationToken ownerStop=default,Action? readIssued=null) =>
        new(new FileStream(handle,FileAccess.ReadWrite,65,true),identity,ownerIsCurrent,ownerStop,readIssued);
    private void RequireCurrent()
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref closed)!=0,this);
        if(ownerStop.IsCancellationRequested||!ownerIsCurrent())
        {Dispose();throw new OperationCanceledException("SUPERJOY_OWNER_RETIRED");}
    }
    public async ValueTask<int> WriteAsync(ReadOnlyMemory<byte> report,CancellationToken cancellationToken)
    {
        RequireCurrent();cancellationToken.ThrowIfCancellationRequested();
        if(report.Length!=65||report.Span[0]!=0)throw new ArgumentException("SUPERJOY_REPORT_ID_ZERO_AND_64_BYTES_REQUIRED");
        using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(2));
        using var linked=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token,ownerStop,cancellationToken,timeout.Token);
        try{await stream.WriteAsync(report,linked.Token).ConfigureAwait(false);RequireCurrent();return report.Length;}
        catch(OperationCanceledException){CancelPendingIo();if(ownerStop.IsCancellationRequested)Dispose();throw;}
    }
    public async ValueTask<int> ReadAsync(Memory<byte> response,CancellationToken cancellationToken)
    {
        RequireCurrent();cancellationToken.ThrowIfCancellationRequested();
        if(response.Length<64)throw new ArgumentException("SUPERJOY_RESPONSE_BUFFER_TOO_SMALL");
        using var linked=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token,ownerStop,cancellationToken);
        var raw=new byte[65];
        try
        {
            var read=stream.ReadAsync(raw.AsMemory(),linked.Token);
            LastReadIssuedIncomplete=!read.IsCompleted;LastReadTokenCancelledAtIssue=linked.IsCancellationRequested;
            readIssuedForTest?.Invoke();
            var count=await read.ConfigureAwait(false);RequireCurrent();
            return DecodeResponse(raw.AsSpan(0,count),response.Span);
        }
        catch(OperationCanceledException)
        {LastReadCancelledWhileIncomplete=LastReadIssuedIncomplete&&!LastReadTokenCancelledAtIssue;CancelPendingIo();if(ownerStop.IsCancellationRequested)Dispose();throw;}
    }
    internal static int DecodeResponse(ReadOnlySpan<byte> raw,Span<byte> payload)
    {
        // Windows HIDP_CAPS includes the ID byte even for unnumbered reports.
        // Match hidapi's Windows zero-ID removal before the fixed 64-byte check.
        // A short report, numbered report or naked payload is never guessed.
        if(raw.Length!=65||raw[0]!=0)throw new InvalidDataException("SUPERJOY_WINDOWS_RESPONSE_SHAPE");
        if(payload.Length<64)throw new ArgumentException("SUPERJOY_RESPONSE_BUFFER_TOO_SMALL");
        raw[1..].CopyTo(payload);return 64;
    }
    public void CancelPendingIo()
    {
        try
        {
            if(stream.SafeFileHandle.IsClosed||stream.SafeFileHandle.IsInvalid)return;
            var ok=Native.CancelIoEx(stream.SafeFileHandle,IntPtr.Zero);
            CancelIoOutcomes.Enqueue(ok?0:Marshal.GetLastWin32Error());
        }
        catch(ObjectDisposedException){ }
    }
    public void Dispose()
    {
        if(Interlocked.Exchange(ref closed,1)!=0)return;
        lifetime.Cancel();
        try{CancelPendingIo();}finally{stream.Dispose();}
    }
    private static IOException Failure(string action)=>new(action+": "+new Win32Exception(Marshal.GetLastWin32Error()).Message);
    private static class Native
    {
        [StructLayout(LayoutKind.Sequential)]internal struct HidAttributes{internal int Size;internal ushort VendorId,ProductId,Version;}
        [StructLayout(LayoutKind.Sequential)]internal struct HidCaps
        {
            internal ushort Usage,UsagePage,InputReportByteLength,OutputReportByteLength,FeatureReportByteLength;
            [MarshalAs(UnmanagedType.ByValArray,SizeConst=17)]internal ushort[] Reserved;
            internal ushort LinkCollectionNodes,InputButtonCaps,InputValueCaps,InputDataIndices;
            internal ushort OutputButtonCaps,OutputValueCaps,OutputDataIndices;
            internal ushort FeatureButtonCaps,FeatureValueCaps,FeatureDataIndices;
        }
        [DllImport("hid.dll",SetLastError=true)][return:MarshalAs(UnmanagedType.U1)]internal static extern bool HidD_GetAttributes(SafeFileHandle handle,ref HidAttributes attributes);
        [DllImport("hid.dll",SetLastError=true)][return:MarshalAs(UnmanagedType.U1)]internal static extern bool HidD_GetPreparsedData(SafeFileHandle handle,out IntPtr data);
        [DllImport("hid.dll")][return:MarshalAs(UnmanagedType.U1)]internal static extern bool HidD_FreePreparsedData(IntPtr data);
        [DllImport("hid.dll")]internal static extern int HidP_GetCaps(IntPtr data,out HidCaps caps);
        [DllImport("kernel32.dll",SetLastError=true)][return:MarshalAs(UnmanagedType.Bool)]internal static extern bool CancelIoEx(SafeFileHandle handle,IntPtr overlapped);
    }
}
