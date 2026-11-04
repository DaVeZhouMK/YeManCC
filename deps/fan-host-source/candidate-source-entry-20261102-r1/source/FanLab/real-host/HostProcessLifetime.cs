using System.Diagnostics;
using System.Runtime.InteropServices;

namespace YeManFanHost;

// FAN-951: a Main-scope handle is insufficient when a foreground HC thread
// outlives Main. The kernel owns final handle release at actual process exit.
internal sealed class HostInstanceLease : IDisposable
{
    internal const string MachineName = "Global\\YeManFanHost-v2";
    internal const string LegacySessionName = "Local\\YeManFanHost-v2";
    private static HostInstanceLease? processLifetimeRoot;
    private readonly Mutex machine;
    private readonly Mutex legacy;
    private bool retained;
    private HostInstanceLease(Mutex machine, Mutex legacy)
        => (this.machine, this.legacy) = (machine, legacy);

    public static HostInstanceLease? TryAcquire()
    {
        Mutex? machine = null;
        Mutex? legacy = null;
        try
        {
            machine = new Mutex(false, MachineName, out var machineCreated);
            if (!machineCreated) { machine.Dispose(); return null; }
            // Compatibility with older binaries in this session. Never fall
            // back to session-only ownership when the machine gate is denied.
            legacy = new Mutex(false, LegacySessionName, out var legacyCreated);
            if (!legacyCreated) { legacy.Dispose(); machine.Dispose(); return null; }
            return new HostInstanceLease(machine, legacy);
        }
        catch (UnauthorizedAccessException)
        {
            legacy?.Dispose(); machine?.Dispose(); return null;
        }
        catch
        {
            legacy?.Dispose(); machine?.Dispose(); throw;
        }
    }

    public void HoldUntilProcessExit()
    {
        retained = true;
        processLifetimeRoot = this;
        GC.KeepAlive(processLifetimeRoot);
    }

    public void Dispose()
    {
        if (retained) return;
        legacy.Dispose(); machine.Dispose();
    }
}

// An independent background thread enforces a wall-clock bound even if the
// engine gate, STA call, Dispose, or ProcessExit handler never returns.
internal sealed class HostProcessLifetime
{
    internal const int StopBudgetMs = 25000;
    internal const int FinalizationBudgetMs = 5000;
    internal const int ForcedExitCode = 72;
    private static HostProcessLifetime? current;
    private readonly object gate = new();
    private readonly AutoResetEvent changed = new(false);
    private readonly Action<string, object?> report;
    private readonly bool observeStopRequests;
    private long deadline = long.MaxValue;
    private string reason = "unarmed";

    internal HostProcessLifetime(Action<string, object?> report, bool observeStopRequests = true)
    {
        this.report = report;
        this.observeStopRequests = observeStopRequests;
        var thread = new Thread(WatchDeadline) { IsBackground = true, Name = "FanHost-exit-deadline" };
        thread.Start();
    }

    internal static void Initialize(Action<string, object?> report, bool observeStopRequests = true)
        => current ??= new HostProcessLifetime(report, observeStopRequests);

    internal static void RequestStop(string reason)
    {
        if (current is { observeStopRequests: true } owner) owner.Arm(reason, StopBudgetMs);
    }

    internal static void RequestFinalization()
        => current?.Arm("Main-finalization", FinalizationBudgetMs);

    internal void Arm(string source, int budgetMs)
    {
        var candidate = Environment.TickCount64 + Math.Max(1, budgetMs);
        lock (gate)
        {
            if (candidate >= deadline) return; // repeated requests cannot extend the bound
            deadline = candidate;
            reason = source;
        }
        changed.Set();
    }

    private void WatchDeadline()
    {
        while (true)
        {
            long due;
            string source;
            lock (gate) { due = deadline; source = reason; }
            if (due == long.MaxValue) { changed.WaitOne(); continue; }
            var remaining = due - Environment.TickCount64;
            if (remaining > 0) { changed.WaitOne((int)Math.Min(remaining, int.MaxValue)); continue; }
            // Logging itself must not prevent termination. Bound even a held
            // diagnostic lock; the failure record never claims OEM restoration.
            var logger = new Thread(() =>
            {
                try { report("host.exit-forced", new {
                    pid = Environment.ProcessId, reason = source,
                    exitCode = ForcedExitCode, cleanupConfirmed = false,
                    oemOwnershipStatus = "unknown-after-forced-process-exit",
                    processExitProven = false,
                    proofRequired = "external-exact-process-handle-signaled"
                }); } catch { }
            }) { IsBackground = true };
            logger.Start();
            logger.Join(100);
            if (!TerminateProcess(GetCurrentProcess(), ForcedExitCode))
                Environment.FailFast("FanHost bounded exit failed: " + source);
            return;
        }
    }

    [DllImport("kernel32.dll")]
    private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool TerminateProcess(IntPtr process, uint exitCode);
}
