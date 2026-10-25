using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using HandheldCompanion.Devices;
using HandheldCompanion.Managers;
using HandheldCompanion.Misc;
using Trace = ConsumerFixture.Trace;

static partial class Harness
{
    const BindingFlags All = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
    static readonly List<object> Cases = new();
    static int NativeAttempts, Checks;
    static string AuthFile = "";
    static Assembly Candidate = null!, Helper = null!;
    static readonly List<AssemblyLoadContext> OwnedProductContexts=new();
    static void Check(bool value, string message) { Checks++; if (!value) throw new InvalidOperationException(message); }
    static string Hash(string file) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant();
    static object? Invoke(object target, string name, params object?[] args)
    {
        try { return target.GetType().GetMethods(All).Single(m => m.Name == name && m.GetParameters().Length == args.Length).Invoke(target, args); }
        catch (TargetInvocationException error) when (error.InnerException is not null) { throw error.InnerException; }
    }
    static void Set(object target, string name, object? value)
    {
        var field = target.GetType().GetField(name, All);
        if (field is not null) field.SetValue(target, value);
        else target.GetType().GetProperty(name, All)!.SetValue(target, value);
    }
    static object? Get(object target, string name) => target.GetType().GetField(name, All)?.GetValue(target)
        ?? target.GetType().GetProperty(name, All)?.GetValue(target);
    static Exception Reject(Action action, string fragment)
    {
        Exception? found = null; try { action(); } catch (Exception error) { found = error; }
        Check(found is not null && found.ToString().Contains(fragment, StringComparison.OrdinalIgnoreCase), "Expected " + fragment + "; got " + found);
        return found!;
    }
    static Assembly ExistingFixtureAssembly()=>typeof(IDevice).Assembly;
    static object Backend(IDevice device)
    {
        var optionsType = Candidate.GetType("YeManFanHost.HostOptions", true)!;
        var options = RuntimeHelpers.GetUninitializedObject(optionsType);
        var synthetic = Guid.NewGuid().ToString("N");
        Set(options, "AllowHardwareWrites", true); Set(options, "Confirmation", synthetic);
        Set(options, "SessionToken", synthetic); Set(options, "AuthorizationPath", AuthFile);
        var backend = Activator.CreateInstance(Candidate.GetType("YeManFanHost.RealHcBackend", true)!, All, null, [options], null)!;
        Set(backend, "device", device); Set(backend, "assembly", typeof(IDevice).Assembly);
        Set(backend, "hcCoreInitialized", true);
        Set(backend, "hcInitializationAssembly", typeof(IDevice).Assembly);Set(backend, "hcInitializationDevice", device);
        Invoke(backend, "BeginOpenAttemptEvidence");
        return backend;
    }
    static object Life(object backend) => Get(backend, "oneXFanLifecycle") ?? throw new InvalidOperationException("OneX lifecycle not dispatched");
    static IDisposable Lighting(object device)
    {
        try { return (IDisposable)Helper.GetType("OneXLightingSession", true)!.GetMethod("Prepare", All)!.Invoke(null, [device])!; }
        catch (TargetInvocationException error) when (error.InnerException is not null) { throw error.InnerException; }
    }
    static void Open(object backend) => Invoke(backend, "OpenHcDevice");
    static void Close(object backend) => Invoke(backend, "CloseHcDevice");
    static void SeedEventsContext(object backend)
    {
        var device = Get(backend, "device")!;
        Set(backend, "sessionDeviceReference", device); Set(backend, "oemBaselineCaptured", true);
        Set(backend, "baselineRestoreRoute", device.GetType().FullName);
        Set(backend, "baselineStrategy", Get(Get(backend, "Route")!, "RestoreStrategy"));
        Invoke(backend, "CaptureSessionRouteMarker");
    }
    static object PrepareEvents(object backend)
    {
        SeedEventsContext(backend);
        // Old exact payloads predate immutable preparation. Execute their
        // original same logical begin/finalize boundaries for counterexamples.
        var preparation=backend.GetType().GetMethod("PrepareOpenEventsCore",All) is null
            ? Invoke(backend,"OpenEventsBeginCore")! : Invoke(backend,"PrepareOpenEventsCore")!;
        Check((Get(preparation,"Need")??preparation).ToString()=="None","OneX event preparation requires no device insertion lifecycle");
        return preparation;
    }
    static void FinalizeEvents(object backend,object preparation)
    {
        var admission=Get(preparation,"Admission");
        if(admission is null)Invoke(backend,"OpenEventsFinalizeCore");
        else Invoke(backend,"OpenEventsFinalizeCore",admission);
    }
    static void Events(object backend)=>FinalizeEvents(backend,PrepareEvents(backend));
    static bool Debt(object backend) => Get(Life(backend), "HasResources") is true;
    static void CleanCounters()
    {
        Trace.InitThrows = Trace.DisposeThrows = Trace.ReleaseThrows = Trace.ExitThrows = Trace.FailInit = Trace.RgbReleaseThrows = false; Trace.OnInit = null; Trace.OnPortWrite = null;
        IDevice.ResetLibrary();
        Check(Trace.Ports.Count == 0, "No serial ownership leak between cases");
        Check(ManagerFactory.powerProfileManager.Subscribers == 0, "No Host profile subscription leak between cases");
        ManagerFactory.powerProfileManager.OnAdd=null;ManagerFactory.powerProfileManager.OnRemove=null;
        ManagerFactory.deviceManager.Status = ManagerStatus.None;
        Trace.Calls.Clear(); Trace.DriverInitializations = Trace.SerialOpenAttempts = 0;
    }
    static void Run(string id, Action action)
    {
        var before = Checks;
        try { CleanCounters(); action(); Check(NativeAttempts == 0, "Native IO remains denied"); Cases.Add(new { id, status = "PASS", checks = Checks - before, calls = Trace.Calls.ToArray(), mixedSubscriptions=MixedSubscriberSnapshot() }); }
        catch (Exception error) { Cases.Add(new { id, status = "FAIL", checks = Checks - before, error = error.ToString(), calls = Trace.Calls.ToArray(), mixedSubscriptions=MixedSubscriberSnapshot() }); }
    }
    static void NoUnrelated()
    {
        Check(!Trace.Calls.Any(x => x.StartsWith("EC:") || x.StartsWith("General") || x == "Base.Close" || x == "Base.Events"), "No general OEM/OpenEvents/Close path entered");
    }
    static IDevice Model(Type type) => (IDevice)Activator.CreateInstance(type)!;
    internal static int Main(string[] args)
    {
        if (args[0].StartsWith("fault-original-constructor-",StringComparison.Ordinal)) return MutateOriginalConstructor(args[1..],args[0][27..]);
        if (args[0] == "fault-public-factory-dispatch") return MutatePublicFactoryDispatch(args[1..]);
        if (args[0] == "fault-native-path") return MutateNativePath(args[1..]);
        if (args[0] == "joined-host-ports") return InstrumentJoinedHost(args[1..]);
        if (args[0].StartsWith("fault-lifecycle-",StringComparison.Ordinal)) return MutateLifecycle(args[1..],args[0][16..]);
        if (args[0].StartsWith("fault-msi-lifecycle-",StringComparison.Ordinal)) return MutateMsiLifecycle(args[1..],args[0][20..]);
        if (args[0].StartsWith("fault-msi-manual-",StringComparison.Ordinal)) return MutateMsiManual(args[1..],args[0][17..]);
        if (args[0] == "msi-manual-inventory") return MsiManualConsumerInventory(args[1..]);
        if (args[0].StartsWith("fault-msi-replay-",StringComparison.Ordinal)) return MutateMsiReplay(args[1..],args[0][17..]);
        if (args[0] == "msi-replay-host-ports") return InstrumentMsiReplayHost(args[1..]);
        if (args[0] == "msi-manual-host-ports") return InstrumentMsiManualHost(args[1..]);
        if (args[0] == "msi-lifecycle-host-ports") return InstrumentMsiLifecycleHost(args[1..]);
        if (args[0] == "msi-lifecycle-hc-ports") return InstrumentMsiLifecycleHc(args[1..]);
        if (args[0] == "msi-lifecycle-inventory") return MsiLifecycleInventory(args[1..]);
        if (args[0] == "lifecycle-hc-ports") return InstrumentLifecycleHc(args[1..]);
        if (args[0] == "joined-hc-ports") return InstrumentJoinedHc(args[1..]);
        if (args[0] == "constructor-leaf-ports") return InstrumentConstructorLeaves(args[1..]);
        if (args[0] == "original-constructors") return RunOriginalConstructors(args[1..]);
        if (args[0] == "constructor-inventory") return ConstructorInventory(args[1..]);
        if (args[0].StartsWith("fault-bootstrap-",StringComparison.Ordinal)) return MutateBootstrapAlgorithm(args[1..],args[0][16..]);
        if (args[0] == "bootstrap-bind-original") return BindBootstrapOriginal(args[1..]);
        if (args[0] == "bootstrap-structure") return BootstrapStructure(args[1..]);
        if (args[0] == "factory-leaf-ports") return InstrumentFactoryLeaves(args[1..]);
        if (args[0] == "sta-leaf-ports") return InstrumentStaPorts(args[1..]);
        if (args[0] == "validation-path-ports") return InstrumentValidationPaths(args[1..]);
        if (args[0] == "capability-acl-ports") return InstrumentCapabilityAclFolder(args[1..]);
        if (args[0].StartsWith("fault-capability-acl-",StringComparison.Ordinal)) return MutateCapabilityAclGuard(args[1..],args[0][21..]);
        if (args[0].StartsWith("fault-validation-",StringComparison.Ordinal)) return MutateValidationGuard(args[1..],args[0][17..]);
        if (args[0].StartsWith("fault-sta-",StringComparison.Ordinal)) return MutateStaOwnership(args[1..],args[0][10..]);
        if (args[0] == "fault-factory-readiness") return MutateFactoryGuard(args[1..],true);
        if (args[0] == "fault-factory-continuation") return MutateFactoryGuard(args[1..],false);
        if (args[0] == "fault-ec-bool") return MutateInheritedEcBool(args[1..]);
        if (args[0] == "fault-close-release") return MutateCloseReleaseGuard(args[1..]);
        if (args[0] == "fault-ec-session") return MutateInheritedEcSession(args[1..]);
        if (args[0] == "fault-mixed-manager-flags") return MutateMixedManagerFlags(args[1..]);
        if (args[0] == "fault-mixed-reentry") return MutateMixedReentryGuard(args[1..]);
        if (args[0] == "fault-initialize-first-start") return MutateInitializationFirstStart(args[1..]);
        if (args[0] == "fault-open-events-admission") return MutateEventAdmission(args[1..]);
        if (args[0] == "old") return OldBinaryProbe.Run(args[1..]);
        if (args[0] == "old-write") return OldWriteProbe.Run(args[1..]);
        var bootstrapOnly=args[0]=="bootstrap-init";
        var capabilityAclOnly=args[0]=="capability-acl";
        var validationReal=args[0]=="validation-real";
        var validationOnly=args[0]=="validation-init";var validationStaticOnly=args[0]=="validation-static";
        var staOnly=args[0]=="sta-init";
        var factoryOnly=args[0]=="factory-init";var mixedOnly=args[0]=="mixed-reentry";var mixedFlagsOnly=args[0]=="mixed-flags";if(bootstrapOnly||capabilityAclOnly||validationReal||validationOnly||validationStaticOnly||staOnly||factoryOnly||mixedOnly||mixedFlagsOnly)args=args[1..];
        AuthFile = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[3]))!, "synthetic-authorization.txt");
        File.WriteAllText(AuthFile, "hardwareWritesAuthorized: true\nallMappedFanRoutesAuthorized: true\n");
        Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT", Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[3]))!, "isolated-host-logs"));
        Candidate = Load(args[0]); Helper = Load(args[1]);
        if(bootstrapOnly)return RunBootstrapCases(args);
        if(capabilityAclOnly)return RunCapabilityAclCases(args);
        if(validationReal)return RunValidationRealMode(args);
        if(validationOnly||validationStaticOnly)return RunValidationCasesMode(args,validationStaticOnly);
        if(staOnly)return RunStaCasesMode(args);
        if(factoryOnly)return RunFactoryCases(args);
        if(mixedOnly)return RunMixedReproduction(args);
        if(mixedFlagsOnly)return RunMixedFlagReproduction(args);
        foreach (var type in new[] { typeof(OneXPlayerX1AMD), typeof(OneXPlayerX1Intel), typeof(OneXPlayerX1Pro), typeof(HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex) })
        {
            Run("fan-then-light:" + type.Name, () =>
            {
                var device = Model(type); var backend = Backend(device); var admission = Invoke(backend, "ObserveFanCapability")!;
                Check(Get(admission, "Supported") is true && Get(admission, "Declared") is false, "Real Host preserves X1 inherited EC admission");
                Open(backend); Events(backend);
                Check(device.IsOpen && IDevice.LibraryInitialized && Trace.DriverInitializations == 1, "Fan prerequisites established once");
                Check(Trace.SerialOpenAttempts == 0, "Fan never attempts LED serial");
                Check(ManagerFactory.powerProfileManager.Subscribers == 2, "Actual Host owns only its two profile delegates");
                device.SetFanControl(true); device.SetFanDuty(50);
                using (Lighting(Model(type)))
                {
                    Check(Trace.Ports.Contains("COM3"), "Actual helper can open LED COM while fan holds OLS");
                    Close(backend);
                    Check(Trace.Ports.Contains("COM3"), "Host Close does not close helper-owned COM");
                }
                Check(!device.IsOpen && !Debt(backend) && Trace.Ports.Count == 0, "Both owned resources returned");
                Check(ManagerFactory.powerProfileManager.Subscribers == 0, "Actual Host removes profile delegates");
                Check(Trace.Calls.Contains("FAN:044a:1") && Trace.Calls.Contains("FAN:044b:92") && Trace.Calls.Contains("FAN:044a:0"), "Actual inherited manual/duty/automatic fan calls retained");
                Check(Trace.Calls.IndexOf("FAN:044a:0") < Trace.Calls.IndexOf("OpenLib.Dispose"), "Fan automatic return precedes transport disposal");
                NoUnrelated();
            });
            Run("light-then-fan-reopen:" + type.Name, () =>
            {
                var device = Model(type); var backend = Backend(device);
                using (Lighting(Model(type)))
                {
                    for (var i = 0; i < 2; i++) { Open(backend); Events(backend); device.SetFanDuty(25); Close(backend); Check(Trace.Ports.Contains("COM3"), "Each fan cycle preserves lighting owner"); Invoke(backend, "BeginOpenAttemptEvidence"); }
                }
                Check(Trace.DriverInitializations == 2 && Trace.Ports.Count == 0 && !Debt(backend), "Reopen acquires fresh OLS while LED session survives"); NoUnrelated();
            });
        }
        Run("ols-false-clean-retry", () =>
        {
            var d = new OneXPlayerX1AMD(); var b = Backend(d); Trace.FailInit = true;
            Reject(() => Open(b), "Open returned false");
            Check(!d.IsOpen && !Debt(b) && Get(b, "HcOpenInvocationStarted") is false, "Failed initialization leaves no fabricated Close debt");
            Trace.FailInit = false; Open(b); Check(Trace.DriverInitializations == 2 && d.IsOpen, "Retry actually initializes OLS again"); Close(b); NoUnrelated();
        });
        Run("ols-throw-never-general-close", () =>
        {
            var b = Backend(new OneXPlayerX1AMD()); Trace.InitThrows = true;
            Reject(() => Open(b), "OLS_INIT_THROW"); Check(!Debt(b), "Throwing initialization disposed local transport");
            Trace.InitThrows = false; Open(b); Close(b); NoUnrelated();
        });
        Run("ols-failure-and-dispose-failure-retains-both", () =>
        {
            var b = Backend(new OneXPlayerX1AMD()); Trace.InitThrows = Trace.DisposeThrows = true;
            var error = Reject(() => Open(b), "OLS_INIT_THROW"); Check(error.ToString().Contains("OLS_DISPOSE_THROW"), "Original and cleanup failures preserved");
            Check(Debt(b) && Get(b, "HcOpenInvocationStarted") is true, "Failed cleanup remains owed");
            Reject(() => Open(b), "cleanup is pending"); Trace.InitThrows = Trace.DisposeThrows = false; Close(b);
            Check(!Debt(b), "Actual Host Close settles failed initialization debt"); Open(b); Close(b); NoUnrelated();
        });
        Run("ols-false-dispose-failure-retry", () =>
        {
            var b = Backend(new OneXPlayerX1AMD()); Trace.FailInit = Trace.DisposeThrows = true;
            Reject(() => Open(b), "OLS_DISPOSE_THROW"); Check(Debt(b), "False init disposal failure remains owned");
            Trace.FailInit = Trace.DisposeThrows = false; Close(b); Open(b); Close(b); NoUnrelated();
        });
        foreach (var fault in new[] { "sleep-before", "sleep-during-init", "close-during-init", "timeout-during-init" })
            Run(fault, () =>
            {
                var d = new OneXPlayerX1AMD(); var b = Backend(d);
                if (fault == "sleep-before") Set(b, "suspendGateEpoch", 1L);
                else Trace.OnInit = () => { if (fault == "sleep-during-init") Set(b, "suspendGateEpoch", 1L); else if (fault == "close-during-init") Set(b, "closeWriteBlocked", 1); else Set(b, "operationTimedOut", true); };
                Reject(() => Open(b), "superseded"); Check(!d.IsOpen && !Debt(b) && !Trace.Calls.Any(x => x.StartsWith("FAN:")), "Stale initialization cannot publish or write fan state");
                Trace.OnInit = null; Set(b, "oneXFanOpenAdmissionEpoch", 1L); Set(b, "suspendGateEpoch", 1L); Set(b, "closeWriteBlocked", 0); Set(b, "operationTimedOut", false);
                Open(b); Close(b); NoUnrelated();
            });
        Run("events-new-sleep-rejects-then-resume", () =>
        {
            var b = Backend(new OneXPlayerX1AMD()); Open(b); Set(b, "suspendGateEpoch", 1L);
            Reject(() => Events(b), "superseded"); Check(ManagerFactory.powerProfileManager.Subscribers == 0, "Stale events did not subscribe"); Close(b);
            Set(b, "oneXFanOpenAdmissionEpoch", 1L); Invoke(b, "BeginOpenAttemptEvidence"); Open(b); Events(b); Close(b); NoUnrelated();
        });
        Run("fan-release-failure-retains-handle", () =>
        {
            var d = new OneXPlayerX1AMD(); var b = Backend(d); Open(b); Events(b); Trace.ReleaseThrows = true;
            Reject(() => Close(b), "automatic return was rejected");
            Check(Trace.Calls.Any(x=>x.Contains("PORT_WRITE_THROW")), "Underlying write exception retained in actual EC error logging"); Check(Debt(b) && d.IsOpen && IDevice.LibraryInitialized, "Failed automatic return retains fan transport and marker");
            Check(!Trace.Calls.Contains("OpenLib.Dispose"), "No disposal after failed release");
            Reject(() => Open(b), "cleanup is pending"); Trace.ReleaseThrows = false; Close(b);
            Check(!Debt(b) && !d.IsOpen && ManagerFactory.powerProfileManager.Subscribers == 0, "Retry completes actual Host cleanup"); NoUnrelated();
        });
        Run("void-fan-return-swallowing-bool-is-real", () =>
        {
            var d=new OneXPlayerX1AMD();var b=Backend(d);Open(b);Trace.ReleaseThrows=true;
            d.SetFanControl(false);
            Check(Trace.Calls.Any(x=>x.Contains("PORT_WRITE_THROW")) && !Trace.Calls.Contains("FAN:044a:0"),"Complete actual FanControl and EC method swallow port error into ignored false");
            var closeError=Reject(()=>Close(b),"automatic return was rejected");Check(closeError.ToString().Contains("ManagerFactory cleanup"),"Unknown-manager cleanup error preserved beside original EC false");Check(Debt(b)&&d.IsOpen,"Strict return result preserves debt");Trace.ReleaseThrows=false;Close(b);NoUnrelated();
        });
        Run("exit-config-throw-preserves-return-debt", () =>
        {
            var d=new OneXPlayerX1AMD();var b=Backend(d);Open(b);Trace.ExitThrows=true;
            var closeError=Reject(()=>Close(b),"IO_EXIT_THROW");Check(closeError.ToString().Contains("ManagerFactory cleanup"),"Unknown-manager cleanup error preserved beside Exit error");Check(Debt(b)&&d.IsOpen&&!Trace.Calls.Contains("OpenLib.Dispose"),"Exit failure after write does not clear transport");
            Trace.ExitThrows=false;Close(b);Check(!Debt(b),"Exit cleanup can retry");NoUnrelated();
        });
        foreach(var loss in new[]{"open-marker","use-openlib"}) Run("close-lost-"+loss,()=>
        {
            var d=new OneXPlayerX1AMD();var b=Backend(d);Open(b);if(loss=="open-marker")d.LoseMarker();else d.DisableOpenLib();
            var closeError=Reject(()=>Close(b),"lost its owned transport");Check(closeError.ToString().Contains("ManagerFactory cleanup"),"Unknown-manager cleanup error preserved beside lost precondition");Check(Debt(b)&&!Trace.Calls.Contains("OpenLib.Dispose"),"Lost precondition retains transport and failure");
            if(loss=="open-marker")d.ForeignMarker();else d.EnableOpenLib();Close(b);Check(!Debt(b),"Recovered exact marker allows actual return retry");NoUnrelated();
        });
        Run("dispose-failure-does-not-repeat-fan-return", () =>
        {
            var d = new OneXPlayerX1AMD(); var b = Backend(d); Open(b); Trace.DisposeThrows = true;
            var closeError = Reject(() => Close(b), "OLS_DISPOSE_THROW"); Check(closeError.ToString().Contains("ManagerFactory cleanup"), "Unknown-manager cleanup error preserved beside Dispose error"); Check(Debt(b), "Disposal debt retained");
            Trace.DisposeThrows = false; Close(b);
            Check(Trace.Calls.Count(x => x == "FAN:044a:0") == 1, "Successful fan return not replayed on disposal retry"); Check(!Debt(b) && !d.IsOpen, "Disposal retry settles ownership"); NoUnrelated();
        });
        foreach (var foreign in new[] { "marker", "transport", "serial", "hid" })
            Run("foreign-" + foreign, () =>
            {
                var d = new OneXPlayerX1AMD(); if (foreign == "marker") d.ForeignMarker(); else if (foreign == "transport") IDevice.ForeignTransport(); else if (foreign == "serial") d.ForeignSerial(); else d.ForeignHid();
                var b = Backend(d); Reject(() => Open(b), "foreign"); Check(!Debt(b) && Trace.DriverInitializations == 0 && Trace.Calls.Count == 0, "Foreign resources never borrowed, disposed or overwritten");
            });
        Run("foreign-arrives-during-initialize", () =>
        {
            var b = Backend(new OneXPlayerX1AMD()); Trace.OnInit = IDevice.ForeignTransport;
            Reject(() => Open(b), "ownership changed"); Check(!Debt(b) && Trace.Calls.Count(x => x == "OpenLib.Dispose") == 1, "Only local initialized candidate disposed");
            Check(!IDevice.LibraryInitialized, "Foreign static object still present and uninitialized"); Trace.OnInit = null;
            Reject(() => Open(b), "foreign"); NoUnrelated();
        });
        foreach (var type in new[] { typeof(UnknownX1), typeof(UnknownOverride) })
            Run("unknown:" + type.Name, () => { var b = Backend(Model(type)); Reject(() => Open(b), "unreviewed"); Check(Trace.Calls.Count == 0 && Get(b, "HcOpenInvocationStarted") is false, "Unknown override rejected before resource acquisition"); });
        Run("invalid-ec-contract", () => { var d = new OneXPlayerX1AMD(); d.ECDetails.FanValueMax = 255; var b = Backend(d); Reject(() => Open(b), "EC fan data changed"); Check(Trace.DriverInitializations == 0, "EC drift rejected before init"); });
        Run("two-backends-do-not-adopt-shared-static", () =>
        {
            var a = Backend(new OneXPlayerX1AMD()); var b = Backend(new OneXPlayerX1Intel()); Open(a);
            Reject(() => Open(b), "foreign"); Check(Debt(a) && !Debt(b) && Trace.DriverInitializations == 1, "Only first backend owns static transport");
            Close(a); Open(b); Close(b); Check(Trace.DriverInitializations == 2, "Second backend acquires its own fresh transport after return"); NoUnrelated();
        });
        Run("reentrant-second-backend-claim", () =>
        {
            var first = Backend(new OneXPlayerX1AMD()); var second = Backend(new OneXPlayerX1Intel());
            Trace.OnInit = () => { Trace.OnInit = null; Open(second); };
            Reject(() => Open(first), "ownership changed"); Check(!Debt(first) && Debt(second), "Late first commit preserves second owner's transport"); Close(second); NoUnrelated();
        });
        Run("parallel-backends-static-claim-is-serialized", () =>
        {
            var a = Backend(new OneXPlayerX1AMD()); var b = Backend(new OneXPlayerX1Intel());
            using var entered = new ManualResetEventSlim(); using var finish = new ManualResetEventSlim();
            Trace.OnInit = () => { entered.Set(); if (!finish.Wait(5000)) throw new TimeoutException("test barrier"); };
            var first = Task.Run(() => Open(a)); Check(entered.Wait(5000), "First entered initialize");
            var second = Task.Run(() => { try { Open(b); return false; } catch (Exception e) { return e.Message.Contains("foreign"); } });
            finish.Set(); Check(first.Wait(5000) && second.Wait(5000) && second.Result, "Second claim waits then rejects owned static handle");
            Check(Trace.DriverInitializations == 1, "Concurrent owners did not both initialize"); Trace.OnInit = null; Close(a); NoUnrelated();
        });
        Run("unrelated-device-keeps-virtual-open", () => { var d = new UnrelatedDevice(); Open(Backend(d)); Check(Trace.Calls.Contains("Unrelated.Open"), "Other family virtual Open preserved"); d.Close(); });
        Run("actual-host-release-proof-remains-bounded", () =>
        {
            var b = Backend(new OneXPlayerX1AMD()); Open(b); ManagerFactory.deviceManager.Status = ManagerStatus.Initialized;
            Reject(() => Close(b), "HC_CLOSE_PENDING"); Check(Get(b, "HcVirtualCloseReturned") is true && !Debt(b), "Transport close does not fabricate clean manager evidence");
            ManagerFactory.deviceManager.Status = ManagerStatus.None; Close(b); NoUnrelated();
        });
        RunStrictWriteCases();
        RunGenericEcWriteCases(args[2]);
        RunGenericCloseCases(args[2]);
        RunGenericOwnershipCases(args[2]);
        RunMixedEventsCases(args[2]);
        RunMixedReentryCases();
        RunMixedDebtCases();
        RunMixedManagerFlagCases();
        RunEventAdmissionCases();
        RunInitializationCases(args[2]);
        var failed = Cases.Any(c => JsonSerializer.Serialize(c).Contains("\"status\":\"FAIL\""));
        File.WriteAllText(args[3], JsonSerializer.Serialize(new { schemaVersion = 1, status = failed ? "FAIL" : "PASS", checks = Checks, cases = Cases,
            candidateHostSha256 = Hash(args[0]), helperSha256 = Hash(args[1]), hcSha256 = Hash(args[2]), nativeAttempts = NativeAttempts,
            hardwareIo = false, hostMainStarted = false, physicalConflictObserved = false, fullProductAcceptance = false,
            scope = "Actual candidate Host capability/Open/complete original mixed events and callbacks/events preparation/events finalization/Close and wired factory initialization delegate/stage consumers, actual helper Prepare, complete PE-matched HC methods; full factory assembly loading is not executed; leaf OLS/EC/serial/managers/settings are inert fixture terminals." }, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine((failed ? "FAIL" : "PASS") + " cases=" + Cases.Count + " checks=" + Checks + " native=" + NativeAttempts); return failed ? 2 : 0;
    }
}
