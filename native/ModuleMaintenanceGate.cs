namespace YeManCC;

// Lock order: module maintenance -> gamepad setup -> USB/IP -> SDK transport.
// Fail promptly; downloads never acquire this mutation lease.
internal sealed class ModuleMaintenanceGate : IDisposable
{
    internal const string Name = @"Global\YeManCC.ModuleMaintenance";
    private Mutex? mutex;
    private bool held;
    internal static ModuleMaintenanceGate? TryEnter()
    {
        var lease = new ModuleMaintenanceGate();
        try {
            lease.mutex = new Mutex(false, Name);
            try { lease.held = lease.mutex.WaitOne(0); }
            catch (AbandonedMutexException) { lease.held = true; lease.Dispose(); return null; }
            if (lease.held) return lease;
        } catch { }
        lease.Dispose(); return null;
    }
    public void Dispose()
    {
        if (held) { held = false; mutex!.ReleaseMutex(); }
        mutex?.Dispose(); mutex = null;
    }
}
