using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using YeManCC.SharedSleepPower;

// InputHost lifetime is tied to the original parent process, not a reusable PID.
// The one-shot OS wait cancels pipe IO; cleanup stays on the serial Host path.
internal sealed class ParentProcessLifetime : IDisposable
{
    private const uint Synchronize = 0x00100000, QueryLimitedInformation = 0x1000;
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
    internal RemotePowerProcessV1 Identity { get; private init; }
    internal string ImagePath { get; private init; } = "";

    // This is the same held original process used by the exit registration.
    // No second PID lookup may choose another incarnation of the parent.
    internal bool IsPipeServer(SafePipeHandle pipe) => IsAlive &&
        GetNamedPipeServerProcessId(pipe, out var pid) && pid == Identity.Pid && IsAlive;


    internal static ParentProcessLifetime? TryOpen(int processId)
    {
        if (processId <= 0) return null;
        var handle = OpenProcess(Synchronize | QueryLimitedInformation, false, processId);
        if (!handle.IsInvalid)
        {
            try
            {
                var image = new char[32768]; uint length = (uint)image.Length;
                if (WaitForSingleObject(handle, 0) != WaitTimeout || GetProcessId(handle) != (uint)processId ||
                    !GetProcessTimes(handle, out var birth, out _, out _, out _) ||
                    !ProcessIdToSessionId((uint)processId, out var session) ||
                    !QueryFullProcessImageName(handle, 0, image, ref length) || length == 0 ||
                    WaitForSingleObject(handle, 0) != WaitTimeout)
                { handle.Dispose(); return null; }
                return new ParentProcessLifetime(handle) { Identity = new() {
                    Pid = (uint)processId, Session = session, Birth100ns = birth },
                    ImagePath = Path.GetFullPath(new string(image, 0, checked((int)length))) };
            }
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
    private static extern uint GetProcessId(SafeWaitHandle process);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetProcessTimes(SafeWaitHandle process, out ulong created, out ulong exited, out ulong kernel, out ulong user);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool ProcessIdToSessionId(uint pid, out uint session);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode, EntryPoint = "QueryFullProcessImageNameW")]
    private static extern bool QueryFullProcessImageName(SafeWaitHandle process, uint flags, [Out] char[] image, ref uint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(SafeWaitHandle handle, uint milliseconds);
}
