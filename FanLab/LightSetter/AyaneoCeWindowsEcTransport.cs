using System.IO;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Runtime.Loader;

// Uses the existing, byte-pinned runtime; never initializes HC/OpenLibSys/LpcIo
// or managers. Factory is reached only by an admitted exact native owner.
internal sealed class AyaneoCeWindowsEcTransport : IAyaneoCeLightingEc
{
    internal const string LibrarySha256 = "f7ed30f07ea636c0ddcc5764c15c0a25ae8a0aca02ded77e4af24439955487cf";
    private readonly Mutex mutex;
    private readonly IDisposable controller;
    private readonly Func<byte, byte, bool> write;
    private readonly int threadId = Environment.CurrentManagedThreadId;
    private bool disposed;
    private AyaneoCeWindowsEcTransport(Mutex mutex, IDisposable controller, Func<byte, byte, bool> write)
    { this.mutex = mutex; this.controller = controller; this.write = write; }

    internal static AyaneoCeWindowsEcTransport Open(string runtimeRoot, CancellationToken stop)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException();
        stop.ThrowIfCancellationRequested();
        var path = Path.Combine(Path.GetFullPath(runtimeRoot), "LibreHardwareMonitorLib.dll");
        var library = YeMan.SharedRuntime.RuntimeDependencyContract.LoadPinned(AssemblyLoadContext.Default,
            new AssemblyName("LibreHardwareMonitorLib"), path, LibrarySha256);
        var type = library.GetType("LibreHardwareMonitor.Hardware.Motherboard.Lpc.EC.WindowsEmbeddedControllerIO", true)!;
        var method = type.GetMethod("WriteByteOp", BindingFlags.Instance | BindingFlags.Public, [typeof(byte), typeof(byte)]);
        if (method?.ReturnType != typeof(bool) || !typeof(IDisposable).IsAssignableFrom(type))
            throw new InvalidDataException("CEC_PINNED_WRITE_CONTRACT_CHANGED");
        // The pinned constructor's WaitEc silently accepts a null library mutex.
        // This dedicated pre-HC route therefore owns the SAME named OS mutex
        // itself, across the whole combined request. Never call Mutexes.Open(),
        // which would initialize unrelated ISA/PCI/USB/Razer mutexes.
        var field = library.GetType("LibreHardwareMonitor.Hardware.Mutexes", true)!
            .GetField("_ecMutex", BindingFlags.Static | BindingFlags.NonPublic);
        if (field is null || field.GetValue(null) is not null)
            throw new InvalidOperationException("CEC_LIBRARY_ALREADY_INITIALIZED");
        var mutex = new Mutex(false, @"Global\Access_EC");
        var acquired = false; IDisposable? controller = null;
        try
        {
            try { acquired = mutex.WaitOne(10); }
            catch (AbandonedMutexException) { acquired = true; throw new IOException("CEC_ABANDONED_MUTEX"); }
            if (!acquired) throw new IOException("CEC_BUS_BUSY");
            stop.ThrowIfCancellationRequested();
            try { controller = (IDisposable)Activator.CreateInstance(type)!; }
            catch (TargetInvocationException error) when (error.InnerException is not null)
            { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); throw; }
            var write = (Func<byte, byte, bool>)method.CreateDelegate(typeof(Func<byte, byte, bool>), controller);
            return new(mutex, controller, write);
        }
        catch
        {
            try { controller?.Dispose(); }
            finally { if (acquired) mutex.ReleaseMutex(); mutex.Dispose(); }
            throw;
        }
    }
    public bool TryWriteByte(byte register, byte value)
    {
        if (disposed) throw new ObjectDisposedException(nameof(AyaneoCeWindowsEcTransport));
        if (Environment.CurrentManagedThreadId != threadId) throw new InvalidOperationException("CEC_THREAD_AFFINITY");
        if (register is not (0x6D or 0xB1 or 0xB2 or 0xBF)) throw new ArgumentException("CEC_NON_LIGHTING_REGISTER");
        // Unlike WriteByte(void), the pinned bool reports failed port handshakes.
        // No blind five-attempt loop after cancellation or a partial command.
        return write(register, value);
    }
    public void Dispose()
    {
        if (disposed) return;
        if (Environment.CurrentManagedThreadId != threadId) throw new InvalidOperationException("CEC_THREAD_AFFINITY");
        disposed = true;
        try { controller.Dispose(); }
        finally { mutex.ReleaseMutex(); mutex.Dispose(); }
    }
}
