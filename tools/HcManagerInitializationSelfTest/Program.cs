using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

if (args.Length != 3) throw new ArgumentException("runtimeRoot sharedRoot evidenceRoot");
var runtime = Path.GetFullPath(args[0]);
var sharedRoot = Path.GetFullPath(args[1]);
var evidence = Path.GetFullPath(args[2]);
Directory.CreateDirectory(evidence);
var settings = Path.Combine(evidence, "isolated-settings");
var logs = Path.Combine(evidence, "logs");
Directory.CreateDirectory(settings); Directory.CreateDirectory(logs);
// LogManager chooses config relative to the probe EXE. No production configuration is changed.
File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "HcManagerInitializationSelfTest.json"), "{\"Serilog\":{\"MinimumLevel\":\"Fatal\"}}");
Environment.SetEnvironmentVariable("LOG_PATH", logs);
var bindings = new List<object>();
AssemblyLoadContext.Default.Resolving += (context, name) => {
    foreach (var root in new[] { runtime, sharedRoot }) {
        var direct = Path.Combine(root, name.Name + ".dll");
        var paths = File.Exists(direct) ? new[] { direct } : Directory.GetFiles(root, name.Name + ".dll", SearchOption.AllDirectories);
        if (paths.Length > 1) throw new InvalidOperationException("AMBIGUOUS_PROBE_DEPENDENCY:" + name.Name);
        if (paths.Length == 1) {
            var path = paths[0];
            var identity = AssemblyName.GetAssemblyName(path);
            if (identity.Name != name.Name || identity.Version != name.Version ||
                !identity.GetPublicKeyToken()!.SequenceEqual(name.GetPublicKeyToken()!))
                throw new InvalidOperationException("PROBE_DEPENDENCY_IDENTITY_MISMATCH:" + name.Name);
            bindings.Add(new { assembly = identity.FullName, path, sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant() });
            return context.LoadFromAssemblyPath(path);
        }
    }
    return null;
};
var report = new Dictionary<string, object?> {
    ["scope"] = "Real HC ManagerFactory constructor on isolated STA; no Host, device construction/Open, manager Start, EC, firmware, or fan control actions",
    ["runtimeRoot"] = runtime, ["sharedRoot"] = sharedRoot, ["managerStartsCalled"] = false,
    ["deviceConstructorsCalled"] = false, ["deviceOpenCalled"] = false, ["hostStarted"] = false,
    ["settingsPath"] = settings, ["physicalHardwareAcceptance"] = false
};
var worker = new Thread(() => {
    try {
        Directory.SetCurrentDirectory(runtime);
        var hc = AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(runtime, "HandheldCompanion.dll"));
        var app = hc.GetType("HandheldCompanion.App", true)!;
        foreach (var (name, value) in new[] { ("SettingsPath", settings), ("LogsPath", logs), ("InstallPath", runtime), ("GameControllerDbPath", Path.Combine(runtime, "gamecontrollerdb.txt")) })
            app.GetField(name, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static)!.SetValue(null, value);
        var shared = AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(runtime, "Shared.dll"));
        shared.GetType("HandheldCompanion.Shared.LogManager", true)!.GetMethod("Initialize", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static, new[] { typeof(string) })!.Invoke(null, new object[] { "HcManagerInitializationSelfTest" });
        report["stage"] = "manager-factory-constructor";
        var factory = hc.GetType("HandheldCompanion.Managers.ManagerFactory", true)!;
        var values = (System.Collections.IEnumerable)factory.GetProperty("Managers", BindingFlags.Public | BindingFlags.Static)!.GetValue(null)!;
        var managerTypes = new List<string>();
        foreach (var value in values) managerTypes.Add(value?.GetType().FullName ?? "null");
        report["managerTypes"] = managerTypes;
        report["managerCount"] = managerTypes.Count;
        report["ok"] = managerTypes.Count == 13 && managerTypes.All(name => name != "null");
        report["stage"] = "manager-factory-created";
    } catch (Exception exception) {
        report["ok"] = false;
        var chain = new List<object>();
        for (Exception? item = exception; item != null; item = item.InnerException)
            chain.Add(new { type = item.GetType().FullName, hresult = $"0x{item.HResult:X8}", target = item.TargetSite?.DeclaringType?.FullName + "." + item.TargetSite?.Name, stack = item.StackTrace });
        report["exceptionChain"] = chain;
    }
}) { IsBackground = true };
worker.SetApartmentState(ApartmentState.STA); worker.Start();
if (!worker.Join(TimeSpan.FromSeconds(45))) { report["ok"] = false; report["stage"] = "timeout"; }
report["bindings"] = bindings;
report["timestampUtc"] = DateTime.UtcNow.ToString("O");
File.WriteAllText(Path.Combine(evidence, "result.json"), JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine(JsonSerializer.Serialize(new { ok = report.GetValueOrDefault("ok"), stage = report.GetValueOrDefault("stage"), managerCount = report.GetValueOrDefault("managerCount") }));
Environment.ExitCode = report.GetValueOrDefault("ok") is true ? 0 : 1;