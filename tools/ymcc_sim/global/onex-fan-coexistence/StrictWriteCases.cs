using System.IO;
using System.Reflection;
using HandheldCompanion.Devices;
using HandheldCompanion.Misc;
using Trace = ConsumerFixture.Trace;

static partial class Harness
{
    static readonly Type[] WriteModels = [typeof(OneXPlayerX1AMD), typeof(OneXPlayerX1Intel), typeof(OneXPlayerX1Pro), typeof(HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex)];
    static void Profile(object backend, bool software) => Invoke(backend, "ApplyPowerProfile",
        new PowerProfile { FanProfile = new() { fanMode = software ? FanMode.Software : FanMode.Hardware } }, "Background", null, null);
    static void Duty(object backend, double percent) => Invoke(backend, "WriteFanProfileDuty", percent);
    static object Monitor()
    {
        var monitor = Activator.CreateInstance(Candidate.GetType("YeManFanHost.HWiNFOTemperatureMonitor", true)!, true)!;
        Set(monitor, "sampleReadTick", Environment.TickCount64); return monitor;
    }
    static object AttachTemperature(object backend)
    {
        Events(backend); Set(backend, "activeFanProfile", new FanProfile { fanMode = FanMode.Software });
        Set(backend, "activeCurveDuties", new double[11]); var monitor = Monitor(); Set(backend, "cpuTemperatureMonitor", monitor); return monitor;
    }
    static void Temperature(object backend, object monitor, float value)
    { Set(monitor, "sampleReadTick", Environment.TickCount64); Invoke(backend, "OnHcCpuTemperatureChanged", monitor, (float?)value); }
    static object Engine(object backend)
    {
        var opts = Candidate.GetType("YeManFanHost.HostOptions", true)!.GetMethod("Parse", All)!.Invoke(null, [Array.Empty<string>()])!;
        var engine = Activator.CreateInstance(Candidate.GetType("YeManFanHost.FanHostEngine", true)!, [opts, null])!;
        Set(engine, "realBackend", backend);
        // Start inside a synthetic existing recovery window. This avoids opening
        // a production recovery ledger while exercising the real timer/budget code.
        Set(engine, "recoveryRetryWindowStartedMonoMs", Environment.TickCount64);
        return engine;
    }
    static void EndEngine(object engine)
    {
        Invoke(engine, "StopRecoveryRetry"); Set(engine, "realBackend", null); ((IDisposable)engine).Dispose();
    }
    static void RunStrictWriteCases()
    {
        foreach (var type in WriteModels)
        {
            Run("strict-duty-goldens:" + type.Name, () =>
            {
                var b = Backend(Model(type)); Open(b);
                foreach (var value in new[] { (0.0, 0), (0.25, 0), (0.5, 1), (1.0, 2), (25.0, 46), (50.0, 92), (75.0, 138), (99.0, 182), (100.0, 184) })
                { Trace.Calls.Clear(); Duty(b, value.Item1); Check(Trace.Calls.Contains($"FAN:044b:{value.Item2}"), "Exact original duty conversion at " + value.Item1); }
                Profile(b, true); Check(Trace.Calls.Contains("FAN:044a:1"), "Software profile preserves manual byte");
                Profile(b, false); Check(Trace.Calls.Contains("FAN:044a:0"), "Hardware profile preserves automatic byte"); Close(b); NoUnrelated();
            });
            foreach (var mode in new[] { "cold-duty", "primed-duty", "software-profile", "hardware-profile" })
                Run("strict-false:" + type.Name + ":" + mode, () =>
                {
                    var d = Model(type); var b = Backend(d); Open(b); var logs = new List<string>();
                    b.GetType().GetProperty("Diagnostic")!.SetValue(b, new Action<string, object?>((name, _) => logs.Add(name)));
                    if (mode == "primed-duty") Duty(b, 25);
                    Trace.Calls.Clear(); logs.Clear(); Trace.ReleaseThrows = true;
                    Reject(() => { if (mode.EndsWith("duty")) Duty(b, 50); else Profile(b, mode == "software-profile"); }, "EC fan write was rejected");
                    Check(Trace.Calls.Any(x => x.Contains("PORT_WRITE_THROW")) && !Trace.Calls.Any(x => x.StartsWith("FAN:")), "Actual EC method false is consumed");
                    Check(!logs.Contains("hc.fan-dispatch.callback-returned"), "No successful profile callback evidence on software failure");
                    Check(d.IsOpen && Debt(b) && !Trace.Calls.Contains("OpenLib.Dispose"), "Write failure retains owned transport for return");
                    Trace.ReleaseThrows = false;
                    if (mode.EndsWith("duty")) { Duty(b, 50); Check(Trace.Calls.Contains("FAN:044b:92"), "Same duty explicitly retries after error"); }
                    else { Profile(b, mode == "software-profile"); Check(logs.Contains("hc.fan-dispatch.callback-returned"), "Successful retry reaches profile-returned boundary"); }
                    Close(b); NoUnrelated();
                });
        }
        Run("strict-legacy-cache-cannot-bypass-result", () =>
        {
            var d = new OneXPlayerX1AMD(); var b = Backend(d); Open(b);
            Set(b, "fanDutyWriterDevice", d); Set(b, "fanDutyWriter", new Action<double>(_ => throw new InvalidOperationException("STALE_DELEGATE_USED")));
            Duty(b, 50); Check(Trace.Calls.Contains("FAN:044b:92"), "OneX strict dispatch precedes any legacy cached delegate");
            Trace.ReleaseThrows = true; Reject(() => Duty(b, 50), "EC fan write was rejected"); Trace.ReleaseThrows = false; Close(b);
        });
        foreach (var fault in new[] { "duty-sleep-before", "profile-sleep-before", "duty-sleep-during", "duty-exit-throw", "profile-exit-throw" })
            Run("strict-" + fault, () =>
            {
                var b = Backend(new OneXPlayerX1AMD()); Open(b); Trace.Calls.Clear();
                if (fault.EndsWith("before")) Set(b, "suspendGateEpoch", 1L);
                if (fault == "duty-sleep-during") Trace.OnPortWrite = (_, _) => Set(b, "suspendGateEpoch", 1L);
                if (fault.EndsWith("throw")) Trace.ExitThrows = true;
                Reject(() => { if (fault.StartsWith("profile")) Profile(b, true); else Duty(b, 50); }, fault.EndsWith("throw") ? "IO_EXIT_THROW" : "superseded");
                if (fault.EndsWith("before")) Check(!Trace.Calls.Any(x => x.StartsWith("FAN:")), "Stale write rejected before transport");
                if (fault == "duty-sleep-during") Check(Trace.Calls.Contains("FAN:044b:92"), "Post-write supersession preserves evidence that write may have happened");
                Trace.OnPortWrite = null; Trace.ExitThrows = false; Close(b); NoUnrelated();
            });
        Run("strict-invalid-duty-and-lost-preconditions", () =>
        {
            var d = new OneXPlayerX1AMD(); var b = Backend(d); Open(b); Trace.Calls.Clear();
            foreach (var bad in new[] { double.NaN, double.PositiveInfinity, -1, 101 }) Reject(() => Duty(b, bad), "between 0 and 100");
            Check(!Trace.Calls.Any(x => x.StartsWith("FAN:")), "Invalid duty does not reach EC");
            d.DisableOpenLib(); Reject(() => Duty(b, 50), "owned transport"); d.EnableOpenLib();
            d.LoseMarker(); Reject(() => Profile(b, true), "owned transport"); d.ForeignMarker();
            d.ECDetails.FanValueMax = 255; Reject(() => Duty(b, 50), "EC fan data changed"); d.ECDetails.FanValueMax = 184; Close(b);
        });
        Run("temperature-false-detaches-no-target-or-success-cache", () =>
        {
            var d = new OneXPlayerX1AMD(); var b = Backend(d); Open(b); var monitor = AttachTemperature(b); var errors = new List<Exception>();
            using var notified = new ManualResetEventSlim();
            b.GetType().GetProperty("FanDispatchFailure")!.SetValue(b, new Action<Exception>(e => { lock (errors) errors.Add(e); notified.Set(); }));
            Trace.ReleaseThrows = true; Trace.Calls.Clear(); Temperature(b, monitor, 65);
            Check(notified.Wait(5000), "Actual existing asynchronous dispatch failure is delivered");
            Check(Invoke(b, "MonitorTargetForRead") is null && Get(b, "monitorTarget") is null, "No target survives failed temperature write");
            Check(Get(b, "lastDispatchedTemperature") is null && Get(b, "lastDispatchedFanProfile") is null, "Failed temperature does not commit duplicate suppression cache");
            Check(Get(b, "cpuTemperatureMonitor") is null && Get(b, "activeFanProfile") is null, "Existing Detach stops producer after failure");
            Check(Debt(b) && d.IsOpen, "Failed producer leaves return responsibility owned");
            var failure = errors.Single(); Check(failure.GetType().Name == "BoundFanDispatchFailure", "Existing failure binds curve/control cycle/epoch");
            var before = Trace.Calls.Count; Temperature(b, monitor, 65); Check(errors.Count == 1 && Trace.Calls.Count == before, "Repeated detached callbacks cannot write or notify twice");
            Trace.ReleaseThrows = false; ((IDisposable)monitor).Dispose();
            // Resume uses a fresh source and the same temperature. It must really write.
            var resumed = AttachTemperature(b); Trace.Calls.Clear(); Temperature(b, resumed, 65);
            Check(Trace.Calls.Contains("FAN:044b:92") && Invoke(b, "MonitorTargetForRead") is not null, "Same-temperature recovery retries and publishes only after successful EC return");
            Invoke(b, "DetachCpuTemperatureLoop"); Close(b); NoUnrelated();
        });
        Run("temperature-wrong-source-and-retired-owner", () =>
        {
            var b = Backend(new OneXPlayerX1AMD()); Open(b); var current = AttachTemperature(b); var wrong = Monitor(); var errors = new List<Exception>();
            using var signal = new ManualResetEventSlim();
            b.GetType().GetProperty("FanDispatchFailure")!.SetValue(b, new Action<Exception>(e => { errors.Add(e); signal.Set(); }));
            Trace.Calls.Clear(); Temperature(b, wrong, 65); Check(Trace.Calls.Count == 0 && Invoke(b, "MonitorTargetForRead") is null, "Unowned source cannot write or publish");
            Trace.ReleaseThrows = true; Temperature(b, current, 65); Check(signal.Wait(5000), "Bound failure captured");
            var bound = errors.Single(); var owner = Get(bound, "Owner")!; Check(Invoke(b, "IsFanDispatchOwnerCurrent", owner, false) is true, "Detach preserves current failure owner identity");
            Set(b, "ControlCycleId", 42L); Check(Invoke(b, "IsFanDispatchOwnerCurrent", owner, false) is false, "New control cycle retires old failure");
            Trace.ReleaseThrows = false; ((IDisposable)wrong).Dispose(); Close(b);
        });
        Run("temperature-failure-feeds-existing-bounded-engine", () =>
        {
            var b = Backend(new OneXPlayerX1AMD()); Open(b); var monitor = AttachTemperature(b); Exception? failure = null;
            using var delivered = new ManualResetEventSlim();
            b.GetType().GetProperty("FanDispatchFailure")!.SetValue(b, new Action<Exception>(e => { failure = e; delivered.Set(); }));
            Trace.ReleaseThrows = true; Temperature(b, monitor, 65); Check(delivered.Wait(5000), "Real producer emitted bound failure");
            var engine = Engine(b);
            try
            {
                Invoke(engine, "OnBackendFanDispatchFailure", failure!);
                var state = Get(engine, "state")!;
                Check(Get(state, "State") is string text && text == "FaultLocked" && Get(state, "UnknownState") is true, "Actual engine enters honest failure state");
                Check(Get(engine, "RecoveryRetryTimerActiveForSelfTest") is true && Get(engine, "RecoveryRetryAttemptForSelfTest") is int count && count == 1, "Existing bounded recovery schedules once");
                Invoke(engine, "OnBackendFanDispatchFailure", failure!);
                Check(Get(engine, "RecoveryRetryAttemptForSelfTest") is int same && same == 1, "Duplicate failure does not create another timer or consume another attempt");
                ((IDisposable)Get(engine, "recoveryTimer")!).Dispose(); Set(engine, "recoveryTimer", null); Set(engine, "recoveryRetryAttempt", 30);
                Invoke(engine, "ScheduleRecoveryRetry");
                Check(Get(engine, "RecoveryRetryTerminalForSelfTest") is true && Get(engine, "RecoveryRetryTimerActiveForSelfTest") is false, "Actual thirty-attempt cap ends with no automatic timer");
                Invoke(engine, "OnBackendFanDispatchFailure", failure!);
                Check(Get(engine, "RecoveryRetryTerminalForSelfTest") is true && Get(engine, "RecoveryRetryAttemptForSelfTest") is int terminal && terminal == 30, "Repeated failure cannot reset terminal budget");
            }
            finally { EndEngine(engine); Trace.ReleaseThrows = false; Close(b); }
        });
        Run("existing-engine-sixty-second-window-remains-terminal", () =>
        {
            var b=Backend(new OneXPlayerX1AMD());Open(b);var engine=Engine(b);
            try
            {
                Set(engine,"recoveryRetryWindowStartedMonoMs",Environment.TickCount64-60_001);
                Invoke(engine,"ScheduleRecoveryRetry");
                Check(Get(engine,"RecoveryRetryTerminalForSelfTest")is true&&Get(engine,"RecoveryRetryTimerActiveForSelfTest")is false,"Actual existing sixty-second recovery window terminates without scheduling more work");
                Invoke(engine,"ScheduleRecoveryRetry");
                Check(Get(engine,"RecoveryRetryTerminalForSelfTest")is true&&Get(engine,"RecoveryRetryAttemptForSelfTest")is int attempts&&attempts==0,"Terminal time budget cannot rearm or reset automatically");
            }
            finally{EndEngine(engine);Close(b);}
        });
        Run("old-bound-temperature-failure-does-not-fault-new-engine-owner", () =>
        {
            var b = Backend(new OneXPlayerX1AMD()); Open(b); var monitor = AttachTemperature(b); Exception? error = null;
            using var done = new ManualResetEventSlim(); b.GetType().GetProperty("FanDispatchFailure")!.SetValue(b, new Action<Exception>(e => { error = e; done.Set(); }));
            Trace.ReleaseThrows = true; Temperature(b, monitor, 65); Check(done.Wait(5000), "Actual bound failure ready"); Set(b, "ControlCycleId", 2L);
            var engine = Engine(b); try { Invoke(engine, "OnBackendFanDispatchFailure", error!); Check(Get(engine, "RecoveryRetryTimerActiveForSelfTest") is false && Get(Get(engine, "state")!, "State") is string state && state != "FaultLocked", "Retired error cannot claim current engine state or recovery budget"); }
            finally { EndEngine(engine); Trace.ReleaseThrows = false; Close(b); }
        });
        Run("write-failure-and-failed-return-retain-debt", () =>
        {
            var d = new OneXPlayerX1AMD(); var b = Backend(d); Open(b); Events(b); Trace.ReleaseThrows = true;
            Reject(() => Duty(b, 50), "EC fan write was rejected"); Reject(() => Close(b), "automatic return was rejected");
            Check(Debt(b) && d.IsOpen && !Trace.Calls.Contains("OpenLib.Dispose"), "Neither write nor return failure erases owned resources");
            Reject(() => Duty(b, 50), "cleanup is pending"); Trace.ReleaseThrows = false; Close(b); Check(!Debt(b), "Recovered return finally settles debt");
        });
    }
}
