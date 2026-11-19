using System.Diagnostics;
using System.Reflection;
using System.Text.Json;

internal static class FanPublicPowerResumeCases
{
    private const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    private static object? Field(object target, string name) => target.GetType().GetField(name, Flags)!.GetValue(target);
    private static void Set(object target, string name, object? value) => target.GetType().GetField(name, Flags)!.SetValue(target, value);
    private static object? Prop(object target, string name) => target.GetType().GetProperty(name, Flags)!.GetValue(target);
    private static void SetProp(object target, string name, object? value) => target.GetType().GetProperty(name, Flags)!.SetValue(target, value);
    private static object? Call(object target, string name, params object?[] values)
    {
        var method = target.GetType().GetMethods(Flags).Single(item => item.Name == name && !item.IsGenericMethod && item.GetParameters().Length >= values.Length && item.GetParameters().Skip(values.Length).All(item => item.IsOptional));
        try { return method.Invoke(target, values.Concat(Enumerable.Repeat<object?>(Type.Missing, method.GetParameters().Length - values.Length)).ToArray()); }
        catch (TargetInvocationException error) when (error.InnerException is not null) { throw error.InnerException; }
    }
    private static bool Wait(Func<bool> predicate)
    {
        var watch = Stopwatch.StartNew();
        while (watch.ElapsedMilliseconds < 4000) { if (predicate()) return true; Thread.Sleep(5); }
        return predicate();
    }
    internal static void Run(Assembly host, Action<string, bool, string> check, Action<object> scenario, string selection)
    {
        if (selection.StartsWith("resume-retirement-", StringComparison.Ordinal))
        {
            RunRetirement(host, check, scenario, selection);
            return;
        }
        var names = new[] { "resume-before-open", "resume-after-acquire", "resume-foreign-lease", "resume-deferred-owner", "resume-control-responsibility" };
        var from = selection.StartsWith("resume-from:", StringComparison.Ordinal) ? selection[12..] : null;
        if (selection != "resume-all" && !names.Contains(from ?? selection, StringComparer.Ordinal)) throw new ArgumentException("Unknown explicit public resume case");
        var selected = from is not null ? names[Array.IndexOf(names, from)..] : names.Where(name => selection == "resume-all" || name == selection).ToArray();
        foreach (var name in selected)
        {
            Type T(string type) => host.GetType("YeManFanHost." + type, true)!;
            var options = T("HostOptions").GetMethod("Parse", Flags)!.Invoke(null, new object[] { new[] { "--mock-handshake", "--mock-zero-hardware-evidence", "--mock-resume-rebuild-delay-ms", "150" } })!;
            var engine = Activator.CreateInstance(T("FanHostEngine"), Flags, null, new object?[] { options, null }, null)!;
            T("HostDiagnosticLog").GetProperty("SelfTestCaptureEnabled", Flags)!.SetValue(null, true);
            T("HostDiagnosticLog").GetMethod("ResetSelfTestCapture", Flags)!.Invoke(null, null);
            var state = Field(engine, "state")!;
            using var source = FanPublicPowerScopePort.Create(host, 2);
            FanPublicPowerScopePort? next = null;
            IDisposable? request = null;
            var ok = true;
            void C(string text, bool passed) { ok &= passed; check(name + ": " + text, passed, ""); }
            IDisposable Bind(FanPublicPowerScopePort scope) => (IDisposable)Call(engine, "BindImportedPowerForRequest", scope, scope.Purpose)!;
            void SeedSuspended()
            {
                var curve = Array.CreateInstance(T("FanNode"), 4);
                var temps = new double[] { 0, 40, 65, 80 };
                var duties = new double[] { 0, 30, 67, 100 };
                for (var i = 0; i < 4; i++) curve.SetValue(Activator.CreateInstance(T("FanNode"), temps[i], duties[i]), i);
                Set(engine, "suspendedCurve", curve);
                SetProp(state, "PowerState", "Suspended"); SetProp(state, "State", "Suspended");
            }
            void RetireMockResources()
            {
                SetProp(state, "OpenCalled", false); SetProp(state, "OpenEventsCalled", false);
                SetProp(state, "HardwareWritesEnabled", false); SetProp(state, "HardwareWritesObserved", false);
                Call(engine, "SnapshotUnlocked");
            }
            try
            {
                SeedSuspended();
                object? foreignLease = null;
                if (name is "resume-after-acquire" or "resume-foreign-lease")
                {
                    source.BeforeCurrent = () =>
                    {
                        if (Prop(state, "Lease") is null) return;
                        if (name == "resume-foreign-lease")
                        {
                            foreignLease = Activator.CreateInstance(T("FanLease"), "different-current-client", 77L, Environment.TickCount64 + 30000, "fixture-client")!;
                            SetProp(state, "Lease", foreignLease);
                        }
                        source.Revoke();
                        source.BeforeCurrent = null;
                    };
                }
                request = Bind(source);
                Call(engine, "Resume");
                request.Dispose(); request = null;
                C("accepted worker retains source beyond request", source.Disposals == 0);
                if (name == "resume-before-open")
                {
                    if (!Wait(() => ((System.Collections.IEnumerable)T("HostDiagnosticLog").GetProperty("SelfTestCaptured", Flags)!.GetValue(null)!).Cast<object>().Any(item => Equals(item.GetType().GetField("Item1")!.GetValue(item), "power.resume-mock-slow-rebuild-window")))) throw new TimeoutException("actual worker did not enter its existing mock delay");
                    source.Revoke();
                }
                if (name == "resume-deferred-owner")
                {
                    source.Revoke();
                    SeedSuspended();
                    next = FanPublicPowerScopePort.Create(host, 2);
                    using (Bind(next)) Call(engine, "Resume");
                    C("deferred original queue retains next exact source", next.Disposals == 0 && Field(engine, "resumeContinuationPublicPower") is not null);
                }
                if (!Wait(() => (int)Field(engine, "resumeWorkerActive")! == 0 && (Task?)Field(engine, "resumeTask") is not { IsCompleted: false })) throw new TimeoutException("accepted worker/continuation not returned");
                var sequence = Convert.ToInt64(Prop(state, "MockControlSequence"));
                var success = name is "resume-deferred-owner" or "resume-control-responsibility";
                C("actual enable count follows current accepted owner", sequence == (success ? 1 : 0));
                C("public cancellation never manufactures hardware fault", Prop(state, "UnknownState") is false && !Equals(Prop(state, "State"), "FaultLocked"));
                if (!success)
                {
                    C("cancelled attempt settles as superseded", Equals(Prop(state, "PowerOperationStatus"), "superseded"));
                    C("saved recovery curve survives public cancellation", Field(engine, "suspendedCurve") is not null);
                    C("only worker occupancy lease is removed", name == "resume-foreign-lease" ? ReferenceEquals(Prop(state, "Lease"), foreignLease) : Prop(state, "Lease") is null && Field(engine, "leaseTimer") is null);
                    C("no OEM restore is invented", !((System.Collections.IEnumerable)T("HostDiagnosticLog").GetProperty("SelfTestCaptured", Flags)!.GetValue(null)!).Cast<object>().Any(item => ((string)item.GetType().GetField("Item1")!.GetValue(item)!).StartsWith("restore.", StringComparison.Ordinal)));
                }
                else
                {
                    var live = next ?? source;
                    C("control retains parent after worker returned", live.Disposals == 0 && Field(engine, "residentResumePublicPower") is System.Collections.ICollection { Count: > 0 });
                    Call(engine, "StopCurveTimer");
                    C("clearing intent alone is not SDK return", live.Disposals == 0);
                }
                RetireMockResources();
                C("actual declared mock resource return releases local imports", source.Disposals == 1 && source.HandlesClosed && (next is null || next.Disposals == 1 && next.HandlesClosed));
                if (name == "resume-before-open")
                {
                    using var manual = FanPublicPowerScopePort.Create(host);
                    using (Bind(manual)) { Call(engine, "Open"); Call(engine, "OpenEvents"); }
                    C("next manual request remains reachable", Prop(state, "OpenCalled") is true && Prop(state, "OpenEventsCalled") is true);
                }
                C("no disposal or worker completion synthesizes ACK", source.Settlements == 0 && (next?.Settlements ?? 0) == 0);
            }
            catch (Exception error) { ok = false; check(name + ": original scenario completed", false, error.ToString()); }
            finally
            {
                request?.Dispose();
                source.BeforeCurrent = null;
                if (source.Disposals == 0) source.Revoke();
                if (next?.Disposals == 0) next.Revoke();
                var returned = Wait(() => (int)Field(engine, "resumeWorkerActive")! == 0);
                C("accepted worker returned before fixture cleanup", returned);
                if (returned) { RetireMockResources(); Call(engine, "Dispose"); }
                next?.Dispose();
            }
            scenario(new { name, ok, actualHostEngine = true, actualNativeImporter = false, device = "mock", physicalWrites = 0 });
            if (!ok) return;
        }
    }
    private static void RunRetirement(Assembly image, Action<string, bool, string> check, Action<object> scenario, string selection)
    {
        var names = new[] { "resume-retirement-catalog", "resume-retirement-debt", "resume-retirement-close-failure", "resume-retirement-stale", "resume-retirement-finalize" };
        if (!names.Contains(selection, StringComparer.Ordinal)) throw new ArgumentException("Unknown explicit retirement case");
        Type T(string name) => image.GetType("YeManFanHost." + name, true)!;
        var backendType = T("RealHcBackend");
        var catalog = (System.Collections.IEnumerable)backendType.GetProperty("FanRoutesForSelfTest", Flags)!.GetValue(null)!;
        var routes = catalog.Cast<object>().Where(row =>
        {
            var route = Prop(row, "Value")!;
            return Prop(route, "Evidence")?.ToString() == "CompatCallbackNoScope" &&
                Prop(route, "Kind")?.ToString() is "GenericDuty" or "GenericRpmTarget" &&
                Prop(route, "Precondition")?.ToString() is "OpenLibHandleOpen" or "DeviceOpenAndSupported";
        }).ToArray();
        if (selection != names[0] && selection != names[4]) routes = routes.Where(row => Prop(row, "Key")?.ToString() == "HandheldCompanion.Devices.GPDWin5").ToArray();
        if (routes.Length == 0) throw new InvalidOperationException("No original catalog routes selected");
        foreach (var row in routes)
        {
            var model = Prop(row, "Key")!.ToString()!;
            var route = Prop(row, "Value")!;
            var options = T("HostOptions").GetMethod("Parse", Flags)!.Invoke(null, new object[] { Array.Empty<string>() })!;
            var backend = Activator.CreateInstance(backendType, Flags, null, new[] { options }, null)!;
            var engine = Activator.CreateInstance(T("FanHostEngine"), Flags, null, new object?[] { options, null }, null)!;
            var boundary = Activator.CreateInstance(backendType.GetNestedType("SelfTestHcBoundary", Flags)!, true)!;
            var state = Field(engine, "state")!;
            var device = new FanRetirementDevicePort();
            var ok = true;
            Thread? sta = null;
            void C(string text, bool value) { ok &= value; check(selection + "/" + model + ": " + text, value, ""); }
            void Apply(double duty)
            {
                var profile = Call(boundary, "BuildProfile", Enumerable.Repeat(duty, 11).ToArray(), true)!;
                Call(backend, "ApplyPowerProfileForSelfTest", profile);
            }
            void Session(bool reopen)
            {
                device.IsOpen = true;
                SetProp(backend, "ControlCycleId", Convert.ToInt64(Prop(backend, "ControlCycleId")) + 1);
                Call(backend, reopen ? "ReopenSelfTestSessionForSelfTest" : "InstallSelfTestRouteSessionForSelfTest", device, route, true);
                // Explicit post-Open/pre-OpenEvents boundary, matching the field.
                // Native importer, HC Open and manager graph are inert ports.
                Set(backend, "oemRestoreVerified", false);
                Set(backend, "oemReleaseActionKind", "not-attempted");
                Set(backend, "oemRestoreEvidence", "none");
                SetProp(state, "OpenCalled", true); SetProp(state, "OpenEventsCalled", false);
                SetProp(state, "OemRestoreConfirmed", false);
                SetProp(state, "HcCloseCleanupPending", false);
            }
            try
            {
                Set(engine, "realBackend", backend);
                Set(engine, "recoveryRetryWindowStartedMonoMs", Environment.TickCount64);
                Call(backend, "InstallHcBoundaryForSelfTest", boundary);
                Session(false);
                Apply(50);
                SetProp(state, "HardwareWritesObserved", true);
                C("actual callback creates exact return debt", Prop(backend, "AppliedProfileReturnOwedForSelfTest") is true);
                C("previous written session returns and closes", Call(engine, "CloseForSystemPending") is true && device.Closes == 1);
                C("process write history is preserved", Prop(state, "HardwareWritesObserved") is true);
                sta = (Thread?)Field(backend, "staThread");
                Session(true);
                if (selection == names[1])
                {
                    SetProp(boundary, "CallbackFailure", new System.IO.IOException("inert-partial-software-write"));
                    try { Apply(50); } catch (System.IO.IOException) { }
                    C("throwing callback retains current return debt", Prop(backend, "AppliedProfileReturnOwedForSelfTest") is true);
                }
                device.ThrowClose = selection == names[2];
                var closed = Call(engine, "CloseForSystemPending") is true;
                if (selection is "resume-retirement-debt" or "resume-retirement-close-failure")
                {
                    C("unsafe return or incomplete cleanup remains refused", !closed && Prop(state, "UnknownState") is true);
                    if (selection == names[2])
                    {
                        C("incomplete cleanup retains existing owner", Prop(state, "HcCloseCleanupPending") is true && device.IsOpen);
                        device.ThrowClose = false;
                        C("same owner can finish original cleanup", Call(engine, "CloseForSystemPending") is true && !device.IsOpen);
                    }
                }
                else
                {
                    C("new unwritten session closes without false FaultLocked", closed && Prop(state, "UnknownState") is false && Prop(state, "RecoveryReadbackRequired") is false);
                    C("return stays not-applicable with physical ownership unknown", Prop(backend, "OemReleaseActionKind")?.ToString() == "not-applicable-no-write-history" && Prop(state, "OemPhysicalOwnershipConfirmed") is false);
                    if (selection == names[4])
                    {
                        SetProp(state, "State", "Stopped");
                        var snapshot=Call(engine, "Snapshot")!;
                        C("historical snapshot alone remains strict", T("LocalFanHostServer").GetMethod("ParentRecoveryIsSafeToFinalizeForSelfTest",Flags)!.Invoke(null,[snapshot]) is false);
                        C("exact current retired session can finalize parent exit", Call(engine,"ParentRecoveryCanFinalize") is true);
                        foreach(var field in new[]{"UnknownState","HardwareWritesEnabled","OpenCalled","HcCloseCleanupPending"})
                        {
                            var original=Prop(state,field);SetProp(state,field,true);
                            C("unsafe current "+field+" prevents finalization",Call(engine,"ParentRecoveryCanFinalize") is false);
                            SetProp(state,field,original);
                        }
                        var cycle=Prop(backend,"ControlCycleId");SetProp(backend,"ControlCycleId",Convert.ToInt64(cycle)+1);
                        C("retired fact cannot finalize a new cycle",Call(engine,"ParentRecoveryCanFinalize") is false);
                        SetProp(backend,"ControlCycleId",cycle);
                        Call(backend,"SwapDeviceForSelfTest",new FanRetirementDevicePort());
                        C("retired fact cannot finalize another device",Call(engine,"ParentRecoveryCanFinalize") is false);
                        Call(backend,"SwapDeviceForSelfTest",device);
                        C("same original closed owner remains finalizable",Call(engine,"ParentRecoveryCanFinalize") is true);
                    }
                    else if (selection == names[3])
                    {
                        var priorCycle = Prop(backend, "ControlCycleId");
                        SetProp(backend, "ControlCycleId", Convert.ToInt64(priorCycle) + 1);
                        C("retired evidence cannot authorize another control cycle", Prop(backend, "ClosedLegacyCallbackWithoutReturnObligation") is false);
                        SetProp(backend, "ControlCycleId", priorCycle);
                        Call(backend, "SwapDeviceForSelfTest", new FanRetirementDevicePort());
                        C("retired evidence cannot authorize another device", Prop(backend, "ClosedLegacyCallbackWithoutReturnObligation") is false);
                        Call(backend, "SwapDeviceForSelfTest", device);
                    }
                    else
                    {
                        Call(engine, "EnsureControlAvailable");
                        var count = Convert.ToInt32(Prop(boundary, "CallbackInvocationCount"));
                        Session(true);
                        Apply(25); Apply(75);
                        C("next current control and adjustment reach original callback", Convert.ToInt32(Prop(boundary, "CallbackInvocationCount")) == count + 2 && Prop(backend, "AppliedProfileReturnOwedForSelfTest") is true);
                    }
                }
            }
            catch (Exception error) { ok = false; check(selection + "/" + model + ": original consumer completed", false, error.ToString()); }
            finally
            {
                SetProp(boundary, "CallbackFailure", null); device.ThrowClose = false;
                Call(backend, "SwapDeviceForSelfTest", device);
                Call(engine, "StopRecoveryRetry");
                if (device.IsOpen) Call(backend, "RestoreOem");
                Call(backend, "Dispose");
                Set(engine, "realBackend", null); Call(engine, "Dispose");
                C("original STA resource returned", sta is null || !sta.IsAlive);
            }
            scenario(new { name = selection + "/" + model, ok, actualCloseEngine = true, actualReturnLedger = true,
                originalCatalogRoute = model, hcOpenAndManagerAndCallbackArePorts = true, fullAutomaticReopen = false, physicalWrites = 0 });
        }
    }
}

public sealed class FanRetirementDevicePort
{
    public bool IsOpen { get; set; } = true;
    public bool UseOpenLib => true;
    public bool IsSupported => true;
    public bool ThrowClose { get; set; }
    public int Closes { get; private set; }
    public void Close() { Closes++; if (ThrowClose) throw new System.IO.IOException("inert-close-not-returned"); IsOpen = false; }
}
