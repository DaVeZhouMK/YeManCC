using System.IO;
using System.Reflection;
using System.Windows.Threading;

internal static class FanPublicPowerStaCases
{
    private const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    private static object? Field(object target, string name) => target.GetType().GetField(name, Flags)!.GetValue(target);
    private static Exception? Failure(Action action) { try { action(); return null; } catch (TargetInvocationException error) when (error.InnerException is not null) { return error.InnerException; } catch (Exception error) { return error; } }
    private static string Code(Exception? error) => error?.GetType().GetProperty("Code", Flags)?.GetValue(error)?.ToString() ?? "none";
    private static void Need(bool value, string message) { if (!value) throw new InvalidOperationException(message); }

    internal static void Run(Assembly host, Func<object> createBackend, Action<object, Action, string> legacy,
        Action<string, bool, string> check, Action<object> scenario, string? selection)
    {
        if (selection?.StartsWith("resume-live-", StringComparison.Ordinal) == true)
        {
            FanLiveResumeCases.Run(host, check, scenario, selection);
            return;
        }
        if (selection?.StartsWith("prepare-", StringComparison.Ordinal) == true)
        {
            FanPublicPowerPreparationCases.Run(host, createBackend, legacy, check, scenario, selection);
            return;
        }
        if (selection?.StartsWith("host-asus-", StringComparison.Ordinal) == true)
        {
            FanPublicPowerHostAsusCases.Run(host, createBackend, legacy, check, scenario, selection);
            return;
        }
        if (selection?.StartsWith("queue-", StringComparison.Ordinal) == true)
        {
            FanPublicPowerQueueCases.Run(host, check, scenario, selection);
            return;
        }
        if (selection?.StartsWith("asus-", StringComparison.Ordinal) == true)
        {
            FanPublicPowerAsusCases.Run(host, check, scenario, selection);
            return;
        }
        if (selection?.StartsWith("control-", StringComparison.Ordinal) == true)
        {
            FanPublicPowerControlCases.Run(host, createBackend, check, scenario, selection);
            return;
        }
        if (selection?.StartsWith("resume-", StringComparison.Ordinal) == true)
        {
            FanPublicPowerResumeCases.Run(host, check, scenario, selection);
            return;
        }
        var kinds = new[] { "queued-revocation", "inflight-revocation", "actual-timeout-borrow", "same-sta-and-fresh-request" };
        if (selection is not null)
        {
            if (!kinds.Contains(selection, StringComparer.Ordinal)) throw new ArgumentException("Unknown explicit public power STA scenario");
            kinds = [selection];
        }
        foreach (var kind in kinds)
        {
            var backend = createBackend();
            Thread? staThread = null;
            Dispatcher? dispatcher = null;
            DispatcherHookEventHandler? hook = null;
            Task? caller = null;
            using var entered = new ManualResetEventSlim();
            using var release = new ManualResetEventSlim();
            using var queued = new ManualResetEventSlim();
            using var drained = new ManualResetEventSlim();
            FanPublicPowerScopePort? source = null;
            IDisposable? request = null;
            Exception? response = null;
            var actions = 0;
            var callerThread = 0;
            var ok = true;
            void C(string name, bool value) { ok &= value; check(kind + ": " + name, value, ""); }
            void Power(Action action)
            {
                try { backend.GetType().GetMethods(Flags).Single(method => method.Name == "OnStaPowerBounded" && !method.IsGenericMethod).Invoke(backend, new object[] { action, kind }); }
                catch (TargetInvocationException error) when (error.InnerException is not null) { throw error.InnerException; }
            }
            IDisposable Bind(FanPublicPowerScopePort port) => (IDisposable)backend.GetType().GetMethod("BindImportedPowerForRequest", Flags)!.Invoke(backend, new[] { (object)port, port.Purpose })!;
            try
            {
                legacy(backend, () => { }, "public-scope-sta-warmup");
                dispatcher = (Dispatcher)Field(backend, "staDispatcher")!;
                staThread = (Thread)Field(backend, "staThread")!;
                source = FanPublicPowerScopePort.Create(host);
                request = Bind(source);
                if (kind == "queued-revocation")
                {
                    _ = dispatcher.BeginInvoke(DispatcherPriority.Send, new Action(() => { entered.Set(); release.Wait(); }));
                    Need(entered.Wait(2000), "actual dispatcher barrier not entered");
                    hook = (_, args) => { if (Environment.CurrentManagedThreadId == Volatile.Read(ref callerThread)) queued.Set(); };
                    dispatcher.Hooks.OperationPosted += hook;
                    caller = Task.Run(() => { callerThread = Environment.CurrentManagedThreadId; response = Failure(() => Power(() => Interlocked.Increment(ref actions))); });
                    Need(queued.Wait(2000), "actual control action not queued");
                    request.Dispose(); request = null;
                    C("HTTP owner returned while queued borrower retained import", source.Disposals == 0 && actions == 0);
                    source.Revoke();
                    release.Set();
                    Need(caller.Wait(2000), "cancelled queued caller did not finish");
                    C("revoked queued action never enters SDK port", actions == 0 && Code(response) == "FAN_PUBLIC_POWER_SUPERSEDED");
                    C("queued return closes import without timeout", source.Disposals == 1 && source.HandlesClosed && !(bool)Field(backend, "operationTimedOut")!);
                }
                else if (kind == "inflight-revocation" || kind == "actual-timeout-borrow")
                {
                    caller = Task.Run(() => { response = Failure(() => Power(() => { Interlocked.Increment(ref actions); entered.Set(); release.Wait(); })); });
                    Need(entered.Wait(2000), "actual control action did not start");
                    request.Dispose(); request = null;
                    C("inflight call retains import after HTTP owner returns", source.Disposals == 0 && actions == 1);
                    if (kind == "actual-timeout-borrow")
                    {
                        Need(caller.Wait(17500), "original 15s caller did not finish");
                        C("original timeout does not close actual inflight borrow", Code(response) == "HC_OPERATION_TIMEOUT" && source.Disposals == 0 && Field(backend, "activeStaWorkItem") is not null);
                    }
                    source.Revoke();
                    C("cancel does not pretend entered SDK returned", source.Disposals == 0);
                    release.Set();
                    Need(caller.Wait(2000), "actual inflight caller did not finish");
                    _ = dispatcher.BeginInvoke(DispatcherPriority.Background, new Action(drained.Set));
                    Need(drained.Wait(2000), "actual dispatcher owner did not return");
                    if (kind == "actual-timeout-borrow") C("actual timeout remains an honest timeout", Code(response) == "HC_OPERATION_TIMEOUT");
                    else C("post-call current check rejects revoked success", Code(response) == "FAN_PUBLIC_POWER_SUPERSEDED");
                    C("actual return releases exact import and STA owner", source.Disposals == 1 && source.HandlesClosed && Field(backend, "activeStaWorkItem") is null && !(bool)Field(backend, "operationTimedOut")!);
                }
                else
                {
                    var nested = 0;
                    response = Failure(() => Power(() => Power(() => { nested++; source.Revoke(); })));
                    C("same STA nested path checks revoked result", nested == 1 && Code(response) == "FAN_PUBLIC_POWER_SUPERSEDED");
                    request.Dispose(); request = null;
                    C("same STA return releases all scope borrowers", source.Disposals == 1 && source.HandlesClosed);
                }

                var before = actions;
                response = Failure(() => Power(() => actions++));
                C("missing current scope never falls back after activation", Code(response) == "FAN_PUBLIC_POWER_SUPERSEDED" && actions == before);
                var cleanup = 0;
                legacy(backend, () => cleanup++, "public-revoked-cleanup-remains-callable");
                C("resource return path remains callable after cancellation", cleanup == 1);
                using var fresh = FanPublicPowerScopePort.Create(host);
                using (Bind(fresh)) response = Failure(() => Power(() => actions++));
                C("new legitimate request reaches original STA", response is null && actions == before + 1 && fresh.Disposals == 1 && fresh.HandlesClosed);
                C("no cancellation or disposal synthesizes ACK", source.Settlements == 0 && fresh.Settlements == 0);
            }
            catch (Exception error) { ok = false; check(kind + ": original scenario completed", false, error.ToString()); }
            finally
            {
                release.Set();
                request?.Dispose();
                if (caller is not null) { if (!caller.Wait(3000)) { ok = false; check(kind + ": caller returned", false, "actual caller still active"); } }
                if (dispatcher is not null && hook is not null) dispatcher.Hooks.OperationPosted -= hook;
                var close = Failure(() => backend.GetType().GetMethod("Dispose", Flags)!.Invoke(backend, null));
                var joined = staThread is null || staThread.Join(3000);
                C("owned actual STA returns", close is null && joined && staThread?.IsAlive != true);
                source?.Dispose();
            }
            scenario(new { name = kind, ok, actions, response = Code(response), nativeFacts = "port", actualSta = true, hardwareWrites = 0 });
            if (!ok) return;
        }
    }
}

// One explicit interface adapter to a controlled Native authority port. It
// implements the actual loaded Host ABI; it is not an authenticated importer.
public class FanPublicPowerScopePort : DispatchProxy, IDisposable
{
    private EventWaitHandle? revoked;
    private EventWaitHandle? exited;
    private object? binding;
    private Assembly? host;
    public object Purpose { get; private set; } = null!;
    public int Disposals { get; private set; }
    public int Settlements { get; private set; }
    public int CurrentCalls { get; private set; }
    public Action? BeforeCurrent { get; set; }
    public bool HandlesClosed => revoked?.SafeWaitHandle.IsClosed == true && exited?.SafeWaitHandle.IsClosed == true;
    public static FanPublicPowerScopePort Create(Assembly host, uint purpose = 3)
    {
        var result = (FanPublicPowerScopePort)DispatchProxy.Create(host.GetType("YeManCC.SharedSleepPower.IRemoteNativePowerScope", true)!, typeof(FanPublicPowerScopePort));
        result.host = host;
        result.revoked = new(false, EventResetMode.ManualReset);
        result.exited = new(false, EventResetMode.ManualReset);
        result.Purpose = Enum.ToObject(host.GetType("YeManCC.SharedSleepPower.RemotePowerPurpose", true)!, purpose);
        object New(string name) => Activator.CreateInstance(host.GetType("YeManCC.SharedSleepPower." + name, true)!)!;
        void Set(object value, string name, object field) => value.GetType().GetField(name)!.SetValue(value, field);
        var scope = New("RemotePowerKey128"); Set(scope, "Low", 31UL);
        var request = New("RemotePowerKey128"); Set(request, "Low", 32UL);
        var provider = New("RemotePowerProcessV1"); Set(provider, "Pid", 111U); Set(provider, "Session", 1U); Set(provider, "Birth100ns", 100UL);
        var consumer = New("RemotePowerProcessV1"); Set(consumer, "Pid", 222U); Set(consumer, "Session", 1U); Set(consumer, "Birth100ns", 200UL);
        var snapshot = New("SharedPowerSnapshotV1"); Set(snapshot, "Size", 48U); Set(snapshot, "Version", 1U); Set(snapshot, "Generation", 9UL); Set(snapshot, "PublicationRevision", 1UL); Set(snapshot, "ObservedNotificationSequence", 7UL); Set(snapshot, "SuspendSerial", 3UL); Set(snapshot, "Phase", purpose == 2 ? 3U : 0U); Set(snapshot, "Flags", purpose == 2 ? 1U : 3U);
        result.binding = New("RemotePowerDescriptorV1");
        foreach (var pair in new (string, object)[] { ("Size", 128U), ("Version", 1U), ("Scope", scope), ("Request", request), ("Provider", provider), ("Consumer", consumer), ("Purpose", result.Purpose), ("Captured", snapshot) }) Set(result.binding, pair.Item1, pair.Item2);
        return result;
    }
    public void Revoke() => revoked!.Set();
    public FanPublicPowerScopePort WriteChild(bool differentRequest = false)
    {
        var child = Create(host!, 3);
        foreach (var name in new[] { "Captured", "Request", "Provider", "Consumer" })
            child.binding!.GetType().GetField(name)!.SetValue(child.binding, binding!.GetType().GetField(name)!.GetValue(binding));
        var scope = child.binding!.GetType().GetField("Scope")!.GetValue(child.binding)!;
        scope.GetType().GetField("Low")!.SetValue(scope, 131UL);
        child.binding.GetType().GetField("Scope")!.SetValue(child.binding, scope);
        if (differentRequest)
        {
            var request = child.binding.GetType().GetField("Request")!.GetValue(child.binding)!;
            request.GetType().GetField("Low")!.SetValue(request, 132UL);
            child.binding.GetType().GetField("Request")!.SetValue(child.binding, request);
        }
        return child;
    }
    protected override object? Invoke(MethodInfo? method, object?[]? arguments)
    {
        switch (method!.Name)
        {
            case "get_Binding": return binding;
            case "get_Revoked": return revoked;
            case "get_ProviderExited": return exited;
            case "Current": CurrentCalls++; BeforeCurrent?.Invoke(); return Disposals == 0 && !revoked!.WaitOne(0) && !exited!.WaitOne(0) && Equals(arguments![0], Purpose);
            case "CancelLocal": Revoke(); return null;
            case "TryReadCurrent": arguments![0] = binding!.GetType().GetField("Captured")!.GetValue(binding); return Disposals == 0;
            case "TryCreateSettlement": Settlements++; arguments![2] = Activator.CreateInstance(host!.GetType("YeManCC.SharedSleepPower.RemotePowerSettlementV1", true)!); return false;
            case "Dispose": ReleasePort(); return null;
            default: throw new NotSupportedException(method.Name);
        }
    }
    public virtual void Dispose() => ReleasePort();
    private void ReleasePort()
    {
        if (Disposals != 0) return;
        Disposals++;
        revoked?.Dispose();
        exited?.Dispose();
    }
}
