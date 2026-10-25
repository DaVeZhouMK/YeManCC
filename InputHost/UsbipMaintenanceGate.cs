namespace YeManInputHost;

// A short prepare lease closes the race with the separate startup upgrader.
// It is not a poller, a driver repair, or an output lease. Non-USBIP personas
// never create/open this object, and a busy gate never constructs HMContext.
internal sealed class UsbipMaintenanceGate : IDisposable
{
    private Mutex? mutex;
    private bool held;
    private YeManCC.ModuleMaintenanceGate? moduleLease;
    private UsbipMaintenanceGate() { }
    internal static UsbipMaintenanceGate? TryEnter(bool required)
    {
        var module = YeManCC.ModuleMaintenanceGate.TryEnter();
        if (module is null) return null;
        var lease = TryEnter(required, () => YeManCC.UsbipMaintenanceState.Pending,
            () => new Mutex(false, @"Global\YeManCC.UsbipVersionMaintenance"), 250);
        if (lease is null) { module.Dispose(); return null; }
        lease.moduleLease = module; return lease;
    }
    internal static UsbipMaintenanceGate? TryEnter(bool required, Func<bool> pending, Func<Mutex> create, int waitMs = 0)
    {
        var lease = new UsbipMaintenanceGate();
        if (!required) return lease;
        try
        {
            if (pending()) return null;
            lease.mutex = create();
            try { lease.held = lease.mutex.WaitOne(Math.Clamp(waitMs, 0, 250)); }
            catch (AbandonedMutexException) { lease.held = true; lease.Dispose(); return null; }
            if (!lease.held || pending()) { lease.Dispose(); return null; }
            return lease;
        }
        catch { lease.Dispose(); return null; }
    }
    public void Dispose()
    {
        if (held) { held = false; mutex!.ReleaseMutex(); }
        mutex?.Dispose(); mutex = null;
        moduleLease?.Dispose(); moduleLease = null;
    }
}
