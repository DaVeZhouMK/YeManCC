using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using YeManBatteryHost;

// Added only to the existing simulator. The original BatteryHost test Main and
// fake transports are compiled unchanged; production Program.Main is never run.
internal static class BatterySimulationBoundary
{
    static readonly ConcurrentDictionary<string,object> Targets = new(StringComparer.OrdinalIgnoreCase);
    static readonly ConcurrentQueue<string> NativeAttempts = new();
    static readonly ConcurrentDictionary<Assembly,byte> Guarded = new();
    static string Receipt = "";
    static bool Unhandled;
    static bool NegativeRejected;
    static bool SampleFailed;
    static bool BridgeMode;
    [DllImport("YMCC_BATTERY_SIM_FORBIDDEN_PROBE")]
    static extern void ForbiddenProbe();

    [ModuleInitializer]
    internal static void Initialize()
    {
        Receipt = Environment.GetEnvironmentVariable("YMCC_BATTERY_SIM_RECEIPT") ?? throw new InvalidOperationException("EXPLICIT_SIM_RECEIPT_REQUIRED");
        BridgeMode = Environment.GetEnvironmentVariable("YMCC_BATTERY_SIM_BRIDGE") == "1";
        if (!Path.IsPathFullyQualified(Receipt)) throw new InvalidOperationException("ABSOLUTE_SIM_RECEIPT_REQUIRED");
        AppDomain.CurrentDomain.AssemblyLoad += (_, e) => Guard(e.LoadedAssembly);
        foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies()) Guard(assembly);
        AppDomain.CurrentDomain.UnhandledException += (_, _) => Unhandled = true;
        AppDomain.CurrentDomain.ProcessExit += (_, _) => Finish();
        if (Environment.GetEnvironmentVariable("YMCC_BATTERY_SIM_GUARD_NEGATIVE") == "1")
        {
            try { ForbiddenProbe(); }
            catch (DllNotFoundException) { NegativeRejected = true; }
            // Let the original test Main return; exiting while its module initializer
            // is still running can deadlock ProcessExit on the runtime thread.
        }
    }
    static void Guard(Assembly assembly)
    {
        var name = assembly.GetName().Name;
        if (name is not ("BatteryHostTests" or "YeManBatteryHost" or "LibreHardwareMonitorLib" or "RAMSPDToolkit-NDD") || !Guarded.TryAdd(assembly, 0)) return;
        NativeLibrary.SetDllImportResolver(assembly, (library, _, _) => {
            NativeAttempts.Enqueue(name + ":" + library);
            throw new DllNotFoundException("BATTERY_SIM_TARGET_NATIVE_BLOCKED");
        });
        var file = assembly.Location;
        Targets[name] = new { name, path = file, sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant() };
    }
    static void Finish()
    {
        object? feedback = null;
        if (!BridgeMode && !Unhandled && Environment.ExitCode == 0 && NativeAttempts.IsEmpty)
        {
            try
            {
                var rig = new Rig("lenovo-go-conservation");
                var before = rig.Controller.Read();
                var after = rig.Controller.Apply(before.Fence, 80);
                if (!before.Ok || !after.Ok || before.CurrentLimit != 100 || after.CurrentLimit != 80 || rig.Device.Writes != 1)
                    throw new InvalidOperationException("REAL_CONTROLLER_FEEDBACK_UNCONFIRMED");
                feedback = new { input = new { operation = "battery.charge.set", target = 80, fence = "from-actual-prior-snapshot" },
                    consumer = "YeManBatteryHost.Controller.Apply", before, after, fakeProviderWrites = rig.Device.Writes, physicalWrites = 0 };
            }
            catch { SampleFailed = true; }
        }
        if (!NativeAttempts.IsEmpty || Unhandled || SampleFailed) Environment.ExitCode = 2;
        var report = new { schemaVersion = 1, kind = "battery-controller-simulator-boundary",
            status = Environment.ExitCode == 0 && !Unhandled && !SampleFailed ? "PASS" : "FAIL",
            nativeAttempts = NativeAttempts.ToArray(), negativeProbeRejected = NegativeRejected,
            unhandledException = Unhandled, sampleFailed = SampleFailed,
            loadedTargets = Targets.Values.ToArray(), controllerFeedback = feedback,
            scope = new { originalTestsReused = !BridgeMode, actualControllerBridge = BridgeMode, productionMainInvoked = false,
                actualProvidersUseInjectedTransports = true, physicalHardwareValidated = false,
                operatingSystemSleepPerformed = false, targetNativeImportsBlocked = true } };
        File.WriteAllText(Receipt, JsonSerializer.Serialize(report, new JsonSerializerOptions(JsonSerializerDefaults.Web) { WriteIndented = true }));
    }
}