using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

// InputHost lifetime is tied to the original parent process, not a reusable PID.
// No watchdog timer, lease, retry, or automatic target rearm is introduced.
internal sealed class ParentProcessLifetime : IDisposable
{
    private const uint Synchronize = 0x00100000;
    private const uint WaitTimeout = 258;
    private readonly SafeProcessHandle _handle;

    private ParentProcessLifetime(SafeProcessHandle handle) => _handle = handle;

    internal static ParentProcessLifetime? TryOpen(int processId)
    {
        if (processId <= 0) return null;
        var handle = OpenProcess(Synchronize, false, processId);
        if (!handle.IsInvalid) return new ParentProcessLifetime(handle);
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

    public void Dispose() => _handle.Dispose();

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeProcessHandle OpenProcess(uint desiredAccess, bool inheritHandle, int processId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(SafeProcessHandle handle, uint milliseconds);
}