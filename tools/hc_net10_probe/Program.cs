using System.Reflection;
using System.Runtime.Loader;
using System.Runtime.InteropServices;
using System.Text.Json;
using System;
using System.IO;
using System.Linq;

if (args.Length != 1) throw new ArgumentException("probe root required");
var root = Path.GetFullPath(args[0]);
var result = new Dictionary<string, object?> { ["root"] = root, ["managerFactoryInvoked"] = false };
AssemblyLoadContext.Default.Resolving += (_, name) => {
    var path = Path.Combine(root, name.Name + ".dll");
    return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null;
};
try {
    var assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(root, "HandheldCompanion.dll"));
    try {
        var types = assembly.GetTypes();
        result["handHeldCompanion"] = new { load = "succeeded", getTypes = "succeeded", typeCount = types.Length };
        var factory = assembly.GetType("HandheldCompanion.Managers.ManagerFactory", throwOnError: false, ignoreCase: false);
        result["managerFactoryMetadata"] = factory is null ? new { found = false } : new {
            found = true,
            staticFields = factory.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static).Select(x => x.Name).ToArray(),
            staticMethods = factory.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static).Where(x => !x.IsSpecialName).Select(x => x.Name).Distinct().ToArray(),
            staticConstructorInvoked = false
        };
    } catch (ReflectionTypeLoadException ex) {
        result["handHeldCompanion"] = new { load = "succeeded", getTypes = "failed", loaderExceptions = ex.LoaderExceptions.Where(x => x is not null).Select(x => x!.ToString()).ToArray() };
    }
} catch (Exception ex) { result["handHeldCompanion"] = new { load = "failed", exception = ex.ToString() }; }
// GamepadMotion.dll is a native HC backend, not a managed .NET assembly.
// Do not call AssemblyLoadContext.LoadFromAssemblyPath on it: BadImageFormatException
// would describe the probe, not the product. NativeLibrary.Load plus export checks below
// is the authoritative metadata-only observation and invokes no motion export.
result["gamepadMotion"] = new { load = "not-applicable", kind = "native", managedAssemblyLoad = "skipped" };
var nativeExports = new[] { "CreateGamepadMotion", "DeleteGamepadMotion", "ResetGamepadMotion", "ProcessMotion", "GetCalibratedGyro", "GetGravity", "GetProcessedAcceleration", "GetOrientation", "GetPlayerSpaceGyro", "GetWorldSpaceGyro", "StartContinuousCalibration", "PauseContinuousCalibration", "ResetContinuousCalibration", "GetCalibrationOffset", "SetCalibrationOffset", "GetAutoCalibrationConfidence", "SetAutoCalibrationConfidence", "GetAutoCalibrationIsSteady", "GetCalibrationMode", "SetCalibrationMode", "ResetMotion" };
try {
    var handle = NativeLibrary.Load(Path.Combine(root, "GamepadMotion.dll"));
    try { result["gamepadMotionNative"] = new { load = "succeeded", exports = nativeExports.ToDictionary(x => x, x => NativeLibrary.TryGetExport(handle, x, out _)) }; }
    finally { NativeLibrary.Free(handle); }
} catch (Exception ex) { result["gamepadMotionNative"] = new { load = "failed", exception = ex.ToString() }; }
Console.WriteLine(JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
