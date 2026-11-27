using System.Reflection;
using System.Text.Json;

internal static class FanPublicPowerControlCases
{
    private const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    private static object? Field(object target, string name) => target.GetType().GetField(name, Flags)!.GetValue(target);
    private static void Set(object target, string name, object? value) => target.GetType().GetField(name, Flags)!.SetValue(target, value);
    private static void SetProp(object target, string name, object value) => target.GetType().GetProperty(name, Flags)!.SetValue(target, value);
    private static object? Call(object target, string name, params object?[] values)
    {
        var method = target.GetType().GetMethods(Flags).Single(item => item.Name == name && !item.IsGenericMethod && item.GetParameters().Length >= values.Length && item.GetParameters().Skip(values.Length).All(item => item.IsOptional));
        try { return method.Invoke(target, values.Concat(Enumerable.Repeat<object?>(Type.Missing, method.GetParameters().Length - values.Length)).ToArray()); }
        catch (TargetInvocationException error) when (error.InnerException is not null) { throw error.InnerException; }
    }
    private static Exception? Error(Action action) { try { action(); return null; } catch (Exception error) { return error; } }
    private static string Code(Exception? error) => error?.GetType().GetProperty("Code", Flags)?.GetValue(error)?.ToString() ?? error?.GetType().Name ?? "none";

    internal static void Run(Assembly host, Func<object> create, Action<string, bool, string> check, Action<object> scenario, string selection)
    {
        if (selection == "control-queued-request-revocation")
        {
            RunQueuedRequestRevocation(host, check, scenario);
            return;
        }
        var cases = new[] { "control-hints-and-duty", "control-stale-producer", "control-native-write-child", "control-foreign-child", "control-oem-return-and-msi-source" };
        if (selection != "control-all" && !cases.Contains(selection, StringComparer.Ordinal)) throw new ArgumentException("Unknown explicit control power case");
        foreach (var name in cases.Where(name => selection == "control-all" || name == selection))
        {
            var backend = create();
            var source = FanPublicPowerScopePort.Create(host, name.Contains("child", StringComparison.Ordinal) ? 2U : 3U);
            FanPublicPowerScopePort? child = null;
            FanPublicPowerScopePort? replacement = null;
            object? parent = null;
            IDisposable? request = null;
            var writes = 0;
            var ok = true;
            var target = new object();
            void C(string text, bool value) { ok &= value; check(name + ": " + text, value, ""); }
            IDisposable Bind(FanPublicPowerScopePort scope) => (IDisposable)Call(backend, "BindImportedPowerForRequest", scope, scope.Purpose)!;
            void Adopt(object? resumeParent = null) => Call(backend, "AdoptCurrentWritePowerForControl", resumeParent, false);
            void Duty() => Call(backend, "WriteFanProfileDuty", 100d, null);
            try
            {
                Set(backend, "device", target);
                Set(backend, "fanDutyWriterDevice", target);
                Set(backend, "fanDutyWriter", new Action<double>(duty => { if (duty != 100d) throw new InvalidOperationException("actual delegate received changed duty"); writes++; }));
                if (name.Contains("child", StringComparison.Ordinal))
                {
                    using (Bind(source))
                    {
                        C("FanResume is not adopted as continuous FanWrite", Call(backend, "AdoptCurrentWritePowerForControl", null, false) is false);
                    }
                    // The first independent rejected-purpose request returned.
                    // Create the explicit parent for this separate child scenario.
                    C("independent non-upgraded resume request returned", source.Disposals == 1 && source.HandlesClosed);
                    source = FanPublicPowerScopePort.Create(host, 2);
                    parent = Activator.CreateInstance(host.GetType("YeManFanHost.FanRemotePowerScope", true)!, Flags, null, new[] { (object)source, source.Purpose }, null)!;
                    child = source.WriteChild(name == "control-foreign-child");
                    request = Bind(child);
                    var accepted = Error(() => Adopt(parent));
                    request.Dispose(); request = null;
                    if (name == "control-foreign-child")
                    {
                        C("foreign request cannot be a Native resume write child", Code(accepted) == "FAN_PUBLIC_POWER_SUPERSEDED" && Field(backend, "controlPublicPower") is null);
                        C("rejected child never reaches scalar writer", Code(Error(Duty)) == "FAN_PUBLIC_POWER_SUPERSEDED" && writes == 0);
                    }
                    else
                    {
                        C("same request cohort accepts explicit Native-issued child", accepted is null && Field(backend, "controlPublicPower") is not null);
                        ((IDisposable)parent).Dispose(); parent = null;
                        C("child retains resume parent beyond caller return", source.Disposals == 0 && child.Disposals == 0);
                        Duty();
                        C("child reaches existing duty delegate", writes == 1 && child.CurrentCalls > 0);
                    }
                }
                else
                {
                    request = Bind(source); Adopt(); request.Dispose(); request = null;
                    C("control retains imported write grant after request", source.Disposals == 0 && Field(backend, "controlPublicPower") is not null);
                    var producer = Call(backend, "CaptureFanDispatchOwner")!;
                    if (name == "control-hints-and-duty")
                    {
                        var before = source.CurrentCalls;
                        var healthy = true;
                        for (var i = 0; i < 1000; i++)
                            healthy &= !(bool)backend.GetType().GetProperty("TemperatureWritesBlockedForSelfTest", Flags)!.GetValue(backend)! &&
                                Call(backend, "IsFanDispatchOwnerCurrent", producer, false) is true;
                        C("early hints do not call remote Current", healthy && source.CurrentCalls == before);
                        Duty();
                        C("actual duty leaf still rereads current authority", writes == 1 && source.CurrentCalls > before);
                        source.Revoke();
                        C("sticky revoke blocks the next duty action", Code(Error(Duty)) == "FAN_PUBLIC_POWER_SUPERSEDED" && writes == 1);
                        C("revoked hints remain blocked", backend.GetType().GetProperty("TemperatureWritesBlockedForSelfTest", Flags)!.GetValue(backend) is true);
                    }
                    else if (name == "control-stale-producer")
                    {
                        replacement = FanPublicPowerScopePort.Create(host);
                        using (Bind(replacement)) Adopt();
                        C("old producer never borrows replacement grant", Call(backend, "IsFanDispatchOwnerCurrent", producer, false) is false && source.Disposals == 1);
                        Duty(); C("new legitimate grant still controls", writes == 1);
                    }
                    else
                    {
                        source.Revoke();
                        SetProp(backend, "IsOpen", true);
                        C("MSI command predicate requires public current", Code(Error(() => Call(backend, "MsiCurveCommandStillCurrentForSelfTest", (long)Field(backend, "suspendGateEpoch")!))) == "FAN_PUBLIC_POWER_SUPERSEDED");
                        C("source scalar entry rejects before missing session access", Code(Error(() => Call(backend, "WritePinnedSourceDuty", 100d, null))) == "FAN_PUBLIC_POWER_SUPERSEDED");
                        var boundary = Activator.CreateInstance(host.GetType("YeManFanHost.RealHcBackend+SelfTestHcBoundary", true)!, true)!;
                        Call(backend, "InstallHcBoundaryForSelfTest", boundary);
                        var profile = Call(boundary, "BuildProfile", Array.Empty<double>(), false)!;
                        var release = Error(() => Call(backend, "ApplyPowerProfile", profile));
                        C("OEM callback return remains reachable after public cancel", release is null && Convert.ToInt32(boundary.GetType().GetProperty("CallbackInvocationCount")!.GetValue(boundary)) == 1);
                    }
                }
            }
            catch (Exception error) { ok = false; check(name + ": original scenario completed", false, error.ToString()); }
            finally
            {
                request?.Dispose();
                SetProp(backend, "IsOpen", false);
                Set(backend, "device", null);
                Call(backend, "ReleaseRetiredControlPublicPower");
                if (parent is IDisposable held) held.Dispose();
                C("declared no SDK resources releases local child and parent", source.Disposals == 1 && source.HandlesClosed && (child is null || child.Disposals == 1 && child.HandlesClosed));
                if (replacement is not null) C("replacement control import returned by owner", replacement.Disposals == 1 && replacement.HandlesClosed);
                C("no hint or disposal synthesizes remote ACK", source.Settlements == 0 && (child?.Settlements ?? 0) == 0);
                Call(backend, "Dispose");
                source.Dispose(); child?.Dispose(); replacement?.Dispose();
            }
            scenario(new { name, ok, writes, actualDutyDelegate = true, NativeAuthorityAndProcessAndSdkArePorts = true, physicalWrites = 0 });
            if (!ok) return;
        }
    }

    private static void RunQueuedRequestRevocation(Assembly host, Action<string, bool, string> check, Action<object> scenario)
    {
        Type T(string name) => host.GetType("YeManFanHost." + name, true)!;
        foreach (var enable in new[] { false, true })
        {
            var name = enable ? "control-queued-enable" : "control-queued-acquire";
            var options = T("HostOptions").GetMethod("Parse", Flags)!.Invoke(null,
                new object[] { new[] { "--mock-handshake", "--mock-zero-hardware-evidence" } })!;
            var engine = Activator.CreateInstance(T("FanHostEngine"), Flags, null, new object?[] { options, null }, null)!;
            var gate = Field(engine, "gate")!;
            var state = Field(engine, "state")!;
            object? State(string property) => state.GetType().GetProperty(property, Flags)!.GetValue(state);
            IDisposable Bind(FanPublicPowerScopePort port) => (IDisposable)Call(engine, "BindImportedPowerForRequest", port, port.Purpose)!;
            var ok = true;
            void C(string text, bool value, string detail = "") { ok &= value; check(name + ": " + text, value, detail); }
            using var old = FanPublicPowerScopePort.Create(host);
            using var fresh = FanPublicPowerScopePort.Create(host);
            using var imported = new ManualResetEventSlim();
            Exception? retired = null;
            Thread? worker = null;
            try
            {
                object? initialLease = null;
                JsonElement body = default;
                if (enable)
                {
                    using var seed = FanPublicPowerScopePort.Create(host);
                    using var seedRequest = Bind(seed);
                    initialLease = Call(engine, "AcquireControl")!;
                    var leaseId = initialLease.GetType().GetProperty("LeaseId", Flags)!.GetValue(initialLease);
                    var nodes = new[] { 0, 30, 60, 90 }.Select(tempC => new { tempC, dutyPercent = tempC });
                    body = JsonSerializer.SerializeToElement(new { leaseId, nodes });
                }
                void Execute() { if (enable) Call(engine, "Enable", body); else Call(engine, "AcquireControl"); }
                lock (gate)
                {
                    worker = new Thread(() => retired = Error(() =>
                    {
                        using var oldRequest = Bind(old);
                        imported.Set();
                        Execute();
                    })) { IsBackground = true };
                    worker.Start();
                    C("imported original request is waiting inside engine call", imported.Wait(2000) &&
                        SpinWait.SpinUntil(() => (worker.ThreadState & ThreadState.WaitSleepJoin) != 0, 2000));
                    old.Revoke();
                }
                if (!worker.Join(2000)) throw new TimeoutException("Host control request did not return");
                C("rejects retired request at commit boundary", Code(retired) == "FAN_PUBLIC_POWER_SUPERSEDED", Code(retired));
                C("old request leaves lease and curve state unchanged", ReferenceEquals(State("Lease"), initialLease) &&
                    State("HardwareWritesEnabled") is false);
                using (Bind(fresh))
                {
                    var current = Error(Execute);
                    C("new request with its own current grant succeeds", current is null &&
                        (enable ? State("HardwareWritesEnabled") is true : State("Lease") is not null), Code(current));
                }
                C("old request returns only its own import", old.Disposals == 1 && old.HandlesClosed && old.Settlements == 0);
            }
            catch (Exception error) { C("original engine scenario completed", false, error.ToString()); }
            finally { if (worker is null || !worker.IsAlive) Call(engine, "Dispose"); }
            scenario(new { name, ok, actualEngine = true, physicalWrites = 0 });
        }
    }
}
