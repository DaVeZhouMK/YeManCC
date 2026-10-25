using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

// InputHost lifetime is tied to the original parent process, not a reusable PID.
// The one-shot OS wait cancels pipe IO; cleanup stays on the serial Host path.
internal sealed class ParentProcessLifetime : IDisposable
{
    private const uint Synchronize = 0x00100000;
    private const uint WaitTimeout = 258;
    private readonly SafeWaitHandle _handle;
    private readonly ProcessExitWaitHandle _waitHandle;
    private readonly CancellationTokenSource _exit = new();
    private readonly RegisteredWaitHandle _registration;
    private readonly object _gate = new();
    private bool _disposed;

    private sealed class ProcessExitWaitHandle : WaitHandle
    {
        internal ProcessExitWaitHandle(SafeWaitHandle handle) => SafeWaitHandle = handle;
    }

    private ParentProcessLifetime(SafeWaitHandle handle)
    {
        _handle = handle;
        _waitHandle = new ProcessExitWaitHandle(handle);
        _registration = ThreadPool.RegisterWaitForSingleObject(_waitHandle,
            static (state, _) => ((ParentProcessLifetime)state!).ParentExited(),
            this, Timeout.Infinite, executeOnlyOnce: true);
    }

    private void ParentExited()
    {
        lock (_gate)
        {
            if (_disposed) return;
            // Cancellation marks IO terminal before invoking its registrations.
            // A throwing cancellation callback must not escape the pool thread.
            try { _exit.Cancel(); }
            catch (AggregateException) { }
        }
    }

    internal CancellationToken ExitToken => _exit.Token;

    internal static ParentProcessLifetime? TryOpen(int processId)
    {
        if (processId <= 0) return null;
        var handle = OpenProcess(Synchronize, false, processId);
        if (!handle.IsInvalid)
        {
            try { return new ParentProcessLifetime(handle); }
            catch { handle.Dispose(); return null; }
        }
        handle.Dispose();
        return null;
    }

    internal bool IsAlive
    {
        get
        {
            if (_handle.IsClosed || _handle.IsInvalid) return false;
            try { return WaitForSingleObject(_handle, 0) == WaitTimeout; }
            catch (ObjectDisposedException) { return false; }
        }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            if (_disposed) return;
            _disposed = true;
            _registration.Unregister(null);
            _waitHandle.Dispose();
            _exit.Dispose();
        }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeWaitHandle OpenProcess(uint desiredAccess, bool inheritHandle, int processId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(SafeWaitHandle handle, uint milliseconds);
}
