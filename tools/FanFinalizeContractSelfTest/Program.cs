using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

if (args.Length == 3 && args[0] == "--write-negative-fixture")
{
    MutationFixture.WriteWrongTerminalState(args[1], args[2]);
    return 0;
}
if (args.Length != 3) throw new ArgumentException("host-payload-root evidence-root report-json");
var hostRoot = Path.GetFullPath(args[0]);
var evidenceRoot = Path.GetFullPath(args[1]);
Directory.CreateDirectory(evidenceRoot);
var checks = new List<object>();
var nativeAttempts = new List<string>();
var identities = new List<object>();
var artifactHashes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
string? failure = null;
string Hash(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
const BindingFlags All = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance;
void Check(string name, bool ok, object? detail = null)
{
    checks.Add(new { name, passed = ok, detail });
    if (!ok) throw new InvalidOperationException(name + ": " + JsonSerializer.Serialize(detail));
}
object? Invoke(Type type, string method, object? instance = null, params object?[] parameters) =>
    type.GetMethod(method, All)!.Invoke(instance, parameters);
object? Prop(object instance, string name) => instance.GetType().GetProperty(name, All)!.GetValue(instance);
try
{
    var allowed = new[] { "YeManFanHost.dll", "YeManFanBackend.dll", "Microsoft.Windows.SDK.NET.dll", "WinRT.Runtime.dll" };
    var assemblyMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    foreach (var name in allowed)
    {
        var path = Path.Combine(hostRoot, name);
        if (!File.Exists(path)) continue;
        var identity = AssemblyName.GetAssemblyName(path);
        assemblyMap.Add(identity.Name!, path);
        artifactHashes.Add(path, Hash(path));
        identities.Add(new { file = path, identity = identity.FullName, sha256 = Hash(path) });
    }
    var tpa = ((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") ?? "").Split(Path.PathSeparator)
        .Where(File.Exists).Select(p => (path: p, name: AssemblyName.GetAssemblyName(p).Name!))
        .GroupBy(p => p.name, StringComparer.OrdinalIgnoreCase).ToDictionary(g => g.Key, g => g.First().path, StringComparer.OrdinalIgnoreCase);
    Assembly Load(AssemblyName requested)
    {
        if (assemblyMap.TryGetValue(requested.Name!, out var path))
        {
            var actual = AssemblyName.GetAssemblyName(path);
            if (requested.Version != null && requested.Version != actual.Version) throw new FileLoadException("Host dependency version mismatch: " + requested);
            var loaded = AssemblyLoadContext.Default.LoadFromAssemblyPath(path);
            NativeLibrary.SetDllImportResolver(loaded, (name, _, _) => { nativeAttempts.Add(name); throw new DllNotFoundException("Test target native access refused: " + name); });
            return loaded;
        }
        if (tpa.TryGetValue(requested.Name!, out var platform)) return AssemblyLoadContext.Default.LoadFromAssemblyPath(platform);
        throw new FileNotFoundException("Dependency is outside pinned Host and platform whitelist", requested.FullName);
    }
    AssemblyLoadContext.Default.Resolving += (_, name) => Load(name);
    var host = Load(new AssemblyName("YeManFanHost"));
    var program = host.GetType("YeManFanHost.Program", true)!;
    program.GetProperty("IsSelfTestProcess", All)!.SetValue(null, true);
    var logType = host.GetType("YeManFanHost.HostDiagnosticLog", true)!;
    Invoke(logType, "EnterSelfTestLogIsolation", null, "finalize-contract");
    var hostTests = host.GetType("YeManFanHost.FanHostSelfTest", true)!;
    var retryTests = host.GetType("YeManFanHost.BoundedRecoveryRetrySelfTest", true)!;
    var retryChecks = ((IEnumerable)Invoke(retryTests, "RunSelfTest")!).Cast<object>().Select(x => x.ToString()).ToArray();
    Check("actual Host retry cap, inherited budget and terminal refusal", retryChecks.Length >= 6, retryChecks);
    foreach (var name in new[] { "RunParentExitAcceptanceSelfTestAsync", "RunBudgetExhaustionAndExplicitRetrySelfTestAsync" })
    {
        var task = (Task)Invoke(hostTests, name)!;
        await task.WaitAsync(TimeSpan.FromSeconds(45));
        var rows = ((IEnumerable)task.GetType().GetProperty("Result")!.GetValue(task)!).Cast<object>().Select(x => x.ToString()).ToArray();
        Check("actual Host " + name, rows.Length > 0, rows);
    }
    var optionsType = host.GetType("YeManFanHost.HostOptions", true)!;
    var options = Invoke(optionsType, "Parse", null, new object?[] { new[] { "--port", "8797", "--mock-handshake", "--mock-zero-hardware-evidence" } })!;
    var isolatedLogs = Path.Combine(evidenceRoot, "synthetic-engine-logs");
    var diagnostics = Activator.CreateInstance(logType, All, null, new object?[] { isolatedLogs, 0 }, null)!;
    var engineType = host.GetType("YeManFanHost.FanHostEngine", true)!;
    var engine = Activator.CreateInstance(engineType, All, null, new[] { options, diagnostics }, null)!;
    try
    {
        var gate = engineType.GetField("gate", All)!.GetValue(engine)!;
        var waitBudget = (int)engineType.GetField("AbnormalFinalizeGateWaitMs", All)!.GetRawConstantValue()!;
        foreach (var method in new[] { "EscalateAbnormalFinalize", "MarkAbnormalFinalizeTerminal" })
        {
            using var held = new ManualResetEventSlim();
            using var release = new ManualResetEventSlim();
            var holder = Task.Run(() => { lock (gate) { held.Set(); if (!release.Wait(TimeSpan.FromSeconds(10))) throw new TimeoutException("Synthetic gate holder not released"); } });
            Check(method + " negative fixture actually holds engine gate", held.Wait(3000));
            var timer = Stopwatch.StartNew();
            object? result;
            try { result = Invoke(engineType, method, engine, "synthetic-gate-busy"); }
            finally { timer.Stop(); release.Set(); await holder; }
            Check(method + " bounded despite busy gate", timer.ElapsedMilliseconds >= waitBudget - 400 && timer.ElapsedMilliseconds <= waitBudget + 1200,
                new { elapsedMs = timer.ElapsedMilliseconds, configuredWaitMs = waitBudget, noDeviceOrHcCall = true });
            if (method == "EscalateAbnormalFinalize") Check("unprovable budget while gate busy is refused", Convert.ToInt64(result) == 0, result);
        }
        Invoke(engineType, "MarkAbnormalFinalizeTerminal", engine, "synthetic-budget-exhausted");
        var snapshot = Invoke(engineType, "Snapshot", engine)!;
        Check("exhausted finalize is honest failure, never restoration success", (string?)Prop(snapshot, "State") == "FaultLocked" && (bool)Prop(snapshot, "UnknownState")! && !(bool)Prop(snapshot, "OemRestoreConfirmed")!,
            new { state = Prop(snapshot, "State"), unknown = Prop(snapshot, "UnknownState"), oemRestoreConfirmed = Prop(snapshot, "OemRestoreConfirmed") });
    }
    finally { ((IDisposable)engine).Dispose(); }
    Check("Host artifacts unchanged across actual production self-tests", artifactHashes.All(item => Hash(item.Key) == item.Value));
    Check("no target native or HC assembly loaded", nativeAttempts.Count == 0 && !AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "HandheldCompanion"), nativeAttempts);
}
catch (Exception ex)
{
    while (ex is TargetInvocationException && ex.InnerException != null) ex = ex.InnerException;
    failure = ex.ToString();
}
var report = new { status = failure == null ? "PASS" : "FAIL", passed = failure == null, checks, identities, nativeAttempts,
    actualHostCode = true, hostPayloadRoot = hostRoot, managerStartsCalled = false, realDeviceOpenCalled = false, hardwareAcceptance = false, failure };
File.WriteAllText(Path.GetFullPath(args[2]), JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine($"FAN_FINALIZE_CONTRACT {report.status} checks={checks.Count}");
return failure == null ? 0 : 1;
