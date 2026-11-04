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

// This exception is a hard admission refusal, not an advisory backend warning.
internal sealed class UsbipBackendBudgetRejectedException : IOException
{
    internal UsbipBackendBudgetRejectedException(string reason, Exception? inner = null) : base(reason, inner) { }
}

// Called only under the existing USB/IP maintenance gate, before an unavailable
// backend can reach either SDK install or the controller factory. One reservation
// covers that entire prepare, including the stock SDK's own finite retries.
// This record never confirms setup/product/driver completion and is not cleared
// on success: a healthy backend bypasses it without any new budget I/O.
internal static class UsbipHostBackendBudget
{
    private const string Operation = "usbip-host-backend-attempt";
    internal static void Reserve()
    {
        try {
            Reserve(Path.Combine(YeManCC.UsbipMaintenanceState.DirectoryPath, "host-backend-attempt.json"),
                YeManCC.UsbipMaintenanceState.CurrentBootSessionIdentity,
                YeManCC.UsbipMaintenanceState.PrepareProtectedDirectory);
        }
        catch (UsbipBackendBudgetRejectedException) { throw; }
        catch (Exception ex) { throw Rejected("authority-unavailable", ex); }
    }
    // File/boot port for the original backend preparation fixture. Production
    // supplies only its protected machine directory and actual V2 boot provider.
    internal static void Reserve(string path, string? currentBoot, Action prepareDirectory)
    {
        string? temp = null;
        try {
            if (!YeManCC.UsbipMaintenanceState.ValidBootSession(currentBoot)) throw Rejected("boot-unverified");
            string? previousBoot = null;
            try {
                var saved = YeManCC.UsbipMaintenanceState.ReadRecord(path);
                if (saved.Kind == YeManCC.UsbipMaintenanceState.RecordKind.Missing) throw new FileNotFoundException();
                if (saved.Kind != YeManCC.UsbipMaintenanceState.RecordKind.Ready) throw Rejected("record-invalid");
                using var doc = System.Text.Json.JsonDocument.Parse(saved.Value.GetRawText());
                var record = doc.RootElement;
                if (record.GetProperty("schemaVersion").GetInt32() != 1 ||
                    record.GetProperty("operation").GetString() != Operation) throw Rejected("record-invalid");
                previousBoot = record.GetProperty("bootSessionIdentity").GetString();
                if (!YeManCC.UsbipMaintenanceState.ValidBootSession(previousBoot)) throw Rejected("record-boot-unverified");
            }
            catch (FileNotFoundException) { }
            catch (DirectoryNotFoundException) { }
            if (string.Equals(previousBoot, currentBoot, StringComparison.OrdinalIgnoreCase))
                throw Rejected("exhausted-this-boot");
            prepareDirectory(); // Failure must prevent both install and factory.
            temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            YeManCC.UsbipMaintenanceState.WriteRecord(path, new {
                schemaVersion = 1, operation = Operation, bootSessionIdentity = currentBoot,
                hostPid = Environment.ProcessId, reservation = Guid.NewGuid().ToString("N"),
                startedAt = DateTimeOffset.UtcNow, scope = "host-backend-prepare;not-product-complete"
            });
            temp = null;
        }
        catch (UsbipBackendBudgetRejectedException) { throw; }
        catch (Exception ex) { throw Rejected("record-unavailable:" + ex.GetType().Name, ex); }
        finally {
            if (temp is not null) { try { File.Delete(temp); } catch { } }
        }
    }
    private static UsbipBackendBudgetRejectedException Rejected(string reason, Exception? inner = null) =>
        new("usbip-host-backend-budget-" + reason, inner);
}
