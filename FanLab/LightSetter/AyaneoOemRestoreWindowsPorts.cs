using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using YeManFanBackend.Capabilities;
using YeManBatteryHost;

// Reuses the already pinned HC/LHM transport. No device lifecycle or manager
// is started here. The caller must resolve actual DMI through the sole catalog.
internal static class AyaneoOemRestoreWindowsPorts
{
    internal static IAyaneoOemRestoreIo Open(LightingOemOwnershipCapability capability,
        string runtimeRoot, Assembly approvedHc, CancellationToken stop)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException();
        stop.ThrowIfCancellationRequested();
        return capability.Protocol switch {
            LightingOemOwnershipProtocol.LegacyCec => new Legacy(AyaneoCeWindowsEcTransport.Open(runtimeRoot, stop)),
            LightingOemOwnershipProtocol.CeiiRam => OpenCeii(stop, () => CreateCheckedSession(runtimeRoot, stop)),
            _ => throw new NotSupportedException("OEM_LIGHTING_PROTOCOL_UNAVAILABLE") };
    }
    private static ISuperXPortSession CreateCheckedSession(string runtimeRoot, CancellationToken stop)
    {
        // Reuse only the existing checked LpcIO transport, never its charging
        // writer. The same library/module bytes are already shipped and pinned.
        YeMan.SharedRuntime.RuntimeDependencyContract.LoadPinned(AssemblyLoadContext.Default,
            new AssemblyName("LibreHardwareMonitorLib"),
            Path.Combine(Path.GetFullPath(runtimeRoot), "LibreHardwareMonitorLib.dll"),
            SuperXFixedLpcModule.LibrarySha256);
        return CreateCheckedCeiiSession(stop);
    }
    internal static ISuperXPortSession CreateCheckedCeiiSession(CancellationToken stop,
        Func<ISuperXKernelChannel>? openKernel = null, Func<byte[]>? image = null, Func<string>? path = null)
        => new SuperXCheckedLpcSession(
            () => new CancelledKernel(openKernel?.Invoke() ?? new SuperXNativeKernelChannel(), stop), image, path);
    private sealed class CancelledKernel(ISuperXKernelChannel kernel, CancellationToken stop) : ISuperXKernelChannel
    {
        public void Open(string path) { stop.ThrowIfCancellationRequested(); kernel.Open(path); }
        public SuperXIoResult Ioctl(uint code, byte[] input)
        { stop.ThrowIfCancellationRequested(); return kernel.Ioctl(code, input); }
        public void Dispose() => kernel.Dispose();
    }
    internal static IAyaneoOemRestoreIo OpenCeii(CancellationToken stop, Func<ISuperXPortSession> open,
        Func<string, Mutex>? createMutex = null)
        => Ram.Open(stop, open, createMutex);
    private sealed class Legacy(IAyaneoCeLightingEc ec) : IAyaneoOemRestoreIo
    {
        public LightingOemOwnershipProtocol Protocol => LightingOemOwnershipProtocol.LegacyCec;
        public bool ReleaseLegacy() => ec.TryWriteByte(0xBF, 0x00);
        public void EnterRam() => throw new InvalidOperationException("OEM_LIGHTING_WRONG_TRANSPORT");
        public void WritePort(ushort port, byte value) => throw new InvalidOperationException("OEM_LIGHTING_WRONG_TRANSPORT");
        public void ExitRam() => throw new InvalidOperationException("OEM_LIGHTING_WRONG_TRANSPORT");
        public void Dispose() => ec.Dispose();
    }
    private sealed class Ram : IAyaneoOemRestoreIo
    {
        private readonly ISuperXPortSession io;
        private readonly CancellationToken stop;
        private readonly List<Mutex> mutexes;
        private readonly int thread = Environment.CurrentManagedThreadId;
        private bool disposed;
        private Ram(ISuperXPortSession io, CancellationToken stop, List<Mutex> mutexes)
        { this.io=io; this.stop=stop; this.mutexes=mutexes; }
        public LightingOemOwnershipProtocol Protocol => LightingOemOwnershipProtocol.CeiiRam;
        internal static Ram Open(CancellationToken stop, Func<ISuperXPortSession> open, Func<string, Mutex>? createMutex)
        {
            var mutexes = new List<Mutex>(); ISuperXPortSession? io = null;
            try
            {
                foreach (var name in new[] { @"Global\Access_ISABUS", @"Global\Access_EC" })
                {
                    stop.ThrowIfCancellationRequested(); var mutex = createMutex is null ? new Mutex(false, name) : createMutex(name); var acquired = false;
                    try
                    {
                        try { acquired = mutex.WaitOne(10); }
                        catch (AbandonedMutexException) { acquired=true; throw new IOException("OEM_LIGHTING_ABANDONED_BUS"); }
                        if (!acquired) throw new IOException("OEM_LIGHTING_BUS_BUSY");
                        mutexes.Add(mutex);
                    }
                    catch { if(acquired)mutex.ReleaseMutex(); mutex.Dispose(); throw; }
                }
                stop.ThrowIfCancellationRequested();
                io = open() ?? throw new IOException("OEM_LIGHTING_CHECKED_TRANSPORT_MISSING");
                return new Ram(io, stop, mutexes);
            }
            catch
            {
                try { io?.Dispose(); }
                finally { for(var i=mutexes.Count-1;i>=0;i--){mutexes[i].ReleaseMutex();mutexes[i].Dispose();} }
                throw;
            }
        }
        private void Current()
        {
            if(disposed)throw new ObjectDisposedException(nameof(Ram));
            if(thread!=Environment.CurrentManagedThreadId)throw new InvalidOperationException("OEM_LIGHTING_THREAD_AFFINITY");
        }
        public bool ReleaseLegacy() => throw new InvalidOperationException("OEM_LIGHTING_WRONG_TRANSPORT");
        public void EnterRam()
        {
            Current(); stop.ThrowIfCancellationRequested();
            io.SelectSlot(1);
            // Exact fixed LpcPort.IT87Enter(4E) sequence. Check every load,
            // slot/unlock/data call; a failed/unknown call seals this session.
            foreach (byte value in new byte[] { 0x87, 0x01, 0x55, 0xAA })
            { stop.ThrowIfCancellationRequested(); io.WritePort(0x4E, value); }
        }
        public void WritePort(ushort port, byte value)
        { Current(); if(port is not (0x4E or 0x4F))throw new ArgumentOutOfRangeException(nameof(port)); stop.ThrowIfCancellationRequested(); io.WritePort(port,value); }
        // Fixed LpcPort.IT87Exit is a no-op for4E. Never issue speculative
        // cleanup writes after a failed or cancelled checked transaction.
        public void ExitRam() { Current(); }
        public void Dispose()
        {
            if(disposed)return; Current();
            try { ExitRam(); }
            finally
            {
                disposed=true;
                try { io.Dispose(); }
                finally { for(var i=mutexes.Count-1;i>=0;i--){mutexes[i].ReleaseMutex();mutexes[i].Dispose();} }
            }
        }
    }
}
