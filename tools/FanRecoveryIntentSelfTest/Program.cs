using System.IO;
using System.Collections;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;

// Exercises the production engine, not a duplicate curve-selection function.
// All engines use the explicit mock backend. Initial curve and successful Close
// are fixtures; no HC/WMI/ACPI calls or native IPC; loopback HTTP is exercised below.
var evidence = Path.GetFullPath(args[0]);
Directory.CreateDirectory(evidence);
Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT", Path.Combine(evidence, "logs"));
var assembly = args.Length > 1
    ? new HostTestLoadContext(Path.GetFullPath(args[1])).LoadFromAssemblyPath(Path.GetFullPath(args[1]))
    : Assembly.Load("YeManFanHost");
if (args.Length > 1 && !string.Equals(assembly.Location, Path.GetFullPath(args[1]), StringComparison.OrdinalIgnoreCase))
    throw new InvalidOperationException("Test loaded a different Host assembly than the requested payload");
Type T(string name) => assembly.GetType("YeManFanHost." + name, true)!;
const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
object? Call(object obj, string name, params object?[] arguments)
{
    try {
        var method = obj.GetType().GetMethods(flags).Single(m => m.Name == name && m.GetParameters().Length >= arguments.Length && m.GetParameters().Skip(arguments.Length).All(p => p.IsOptional));
        var supplied = arguments.Concat(Enumerable.Repeat<object?>(Type.Missing, method.GetParameters().Length - arguments.Length)).ToArray();
        return method.Invoke(obj, supplied);
    }
    catch (TargetInvocationException e) when (e.InnerException is not null) { throw e.InnerException; }
}
object? Field(object obj, string name) => obj.GetType().GetField(name, flags)!.GetValue(obj);
void SetField(object obj, string name, object? value) => obj.GetType().GetField(name, flags)!.SetValue(obj, value);
void SetProperty(object obj, string name, object? value) => obj.GetType().GetProperty(name, flags)!.SetValue(obj, value);
string P(object obj, string name) => obj.GetType().GetProperty(name, flags)!.GetValue(obj)?.ToString() ?? "null";
T("Program").GetProperty("IsSelfTestProcess", flags)!.SetValue(null, true);
var diagnosticType = T("HostDiagnosticLog");
diagnosticType.GetProperty("SelfTestCaptureEnabled", flags)!.SetValue(null, true);
List<string> Events() => ((IEnumerable)diagnosticType.GetProperty("SelfTestCaptured", flags)!.GetValue(null)!).Cast<object>().Select(x => (string)x.GetType().GetField("Item1")!.GetValue(x)!).ToList();
void ResetEvents() => diagnosticType.GetMethod("ResetSelfTestCapture", flags)!.Invoke(null, null);
Array Curve(params double[] duties)
{
    var result = Array.CreateInstance(T("FanNode"), duties.Length);
    var temps = new double[] { 0, 44, 78, 100 };
    for (int i = 0; i < duties.Length; i++) result.SetValue(Activator.CreateInstance(T("FanNode"), temps[i], duties[i]), i);
    return result;
}
string CurveJson(object? curve) => JsonSerializer.Serialize(curve, curve?.GetType() ?? typeof(object), new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase });
var checks = new List<object>();
bool ok = true;
void Check(string name, bool passed)
{
    checks.Add(new { name, passed });
    if (!passed) ok = false;
    Console.WriteLine((passed ? "PASS " : "FAIL ") + name);
}
(object engine, object options) NewEngine(int delay = 0)
{
    var argv = new[] { "--mock-handshake", "--mock-resume-rebuild-delay-ms", delay.ToString() };
    var options = T("HostOptions").GetMethod("Parse", flags)!.Invoke(null, new object[] { argv })!;
    var diagnostics = Activator.CreateInstance(diagnosticType, flags, null, new object[] { Path.Combine(evidence, "logs"), 0 }, null)!;
    var engine = Activator.CreateInstance(T("FanHostEngine"), flags, null, new[] { options, diagnostics }, null)!;
    return (engine, options);
}
void SeedActive(object engine, Array curve)
{
    SetField(engine, "activeCurve", curve);
    var state = Field(engine, "state")!;
    SetProperty(state, "ActiveCurve", curve);
    SetProperty(state, "State", "Ready");
    SetProperty(state, "PowerState", "On");
}
// The mock enable records simulated writes. Before Close, declare the fixture's
// actual zero hardware writes, allowing the existing safe Close boundary.
void SafeCloseFixture(object engine) => SetProperty(Field(engine, "state")!, "HardwareWritesObserved", false);
void DisposeFixture(object engine)
{
    SafeCloseFixture(engine);
    SetProperty(Field(engine, "state")!, "HardwareWritesEnabled", false);
    Call(engine, "Dispose");
}

try
{
    foreach (var scenario in new[] { "valid", "wrong-cycle", "wrong-predecessor", "cancelled", "missing-cycle-record", "expired-cycle", "parent-mismatch", "completed-cycle", "terminal-cycle", "replacement-cap", "invalid-curve", "curve-count-mismatch" })
    {
        var root = Path.Combine(evidence, scenario);
        Directory.CreateDirectory(root);
        Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT", root);
        var (predecessor, _) = NewEngine();
        Call(predecessor, "Handshake");
        var desired = Curve(34, 52, 71, 93);
        SeedActive(predecessor, desired);
        Call(predecessor, "PersistRecoveryCycleForSelfTest", Environment.ProcessId,
            scenario == "expired-cycle" ? DateTime.UtcNow.AddMinutes(-3) : DateTime.UtcNow, 0);
        Call(predecessor, "PersistControlIntentForSuccessor");
        var intentFile = Path.Combine(root, "YeManFanHost.control-intent.json");
        var intent = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(intentFile))!;
        var expectedPredecessor = (string)Field(predecessor, "engineInstanceId")!;
        if (scenario == "wrong-cycle") intent["cycleId"] = "unrelated-cycle";
        if (scenario == "wrong-predecessor") intent["predecessorInstanceId"] = "unrelated-host";
        if (scenario == "invalid-curve") intent["curve"]![1]!["dutyPercent"] = 101;
        if (scenario == "curve-count-mismatch") intent["curveNodes"] = 2;
        var cycleFile = Path.Combine(root, "YeManFanHost.recovery-cycle.json");
        var cycle = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(cycleFile))!;
        if (scenario == "completed-cycle") cycle["completed"] = true;
        if (scenario == "terminal-cycle") cycle["terminal"] = true;
        if (scenario == "replacement-cap") cycle["replacements"] = 1;
        File.WriteAllText(cycleFile, cycle.ToJsonString());
        File.WriteAllText(intentFile, intent.ToJsonString());
        if (scenario == "cancelled") Call(predecessor, "CancelControlIntent");
        if (scenario == "missing-cycle-record") File.Delete(Path.Combine(root, "YeManFanHost.recovery-cycle.json"));
        var (successor, options) = NewEngine();
        SetProperty(options, "ParentPid", scenario == "parent-mismatch" ? Environment.ProcessId + 100000 : Environment.ProcessId);
        SetProperty(options, "SuccessorTakeover", true);
        SetProperty(options, "PredecessorInstanceId", expectedPredecessor);
        Call(successor, "Handshake");
        ResetEvents();
        Call(successor, "RunTakeoverRecovery");
        var applied = Field(successor, "activeCurve") is not null;
        Check(scenario + "-takeover-replay", applied == (scenario == "valid"));
        if (scenario != "valid")
            Check(scenario + "-does-not-open-device", P(Call(successor, "Snapshot")!, "OpenCalled") == "False");
        if (scenario != "valid") Check(scenario + "-does-not-report-restored", !Events().Contains("recovery.control-intent-restored"));
        if (scenario == "valid")
            Check("valid-takeover-restores-exact-user-curve", CurveJson(Field(successor, "activeCurve")) == CurveJson(desired));
        DisposeFixture(successor);
        DisposeFixture(predecessor);
    }

    var sameRoot = Path.Combine(evidence, "same-process");
    Directory.CreateDirectory(sameRoot);
    Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT", sameRoot);
    var (sameProcess, _) = NewEngine();
    var currentCurve = Curve(20, 35, 60, 85);
    var foreignCurve = Curve(100, 100, 100, 100);
    File.WriteAllText(Path.Combine(sameRoot, "YeManFanHost.control-intent.json"), "{\"cycleId\":\"foreign\",\"predecessorInstanceId\":\"foreign-host\",\"curve\":" + CurveJson(foreignCurve) + ",\"curveNodes\":4}");
    SetField(sameProcess, "internalRecoveryAdmissionDepth", 1);
    object?[] reopenArgs = { currentCurve, "" };
    var sameApplied = (bool)sameProcess.GetType().GetMethod("RestoreCurrentIntentAfterReopen", flags)!.Invoke(sameProcess, reopenArgs)!;
    Check("same-process-reopen-uses-current-memory-not-foreign-file", sameApplied && CurveJson(Field(sameProcess, "activeCurve")) == CurveJson(currentCurve));
    DisposeFixture(sameProcess);

    foreach (var endpoint in new[] { "CancelControlIntent", "Disable", "Restore", "Release" })
    {
        var faultRoot = Path.Combine(evidence, "cancel-file-failure-" + endpoint);
        Directory.CreateDirectory(faultRoot);
        Environment.SetEnvironmentVariable("YEMANCC_SELFTEST_LOG_ROOT", faultRoot);
        var (locked, _) = NewEngine();
        var intentFile = Path.Combine(faultRoot, "YeManFanHost.control-intent.json");
        File.WriteAllText(intentFile, "{}");
        SetField(locked, "suspendedCurve", currentCurve);
        SetProperty(Field(locked, "state")!, "State", "Ready");
        SetProperty(Field(locked, "state")!, "PowerState", "On");
        var refused = false;
        ResetEvents();
        using (var fileLock = new FileStream(intentFile, FileMode.Open, FileAccess.Read, FileShare.Read))
        {
            try
            {
                using var empty = JsonDocument.Parse("{}");
                if (endpoint == "CancelControlIntent") Call(locked, endpoint); else Call(locked, endpoint, empty.RootElement);
            }
            catch (Exception error) { refused = P(error, "Code") == "CONTROL_INTENT_CANCEL_FAILED"; }
            Check(endpoint + "-file-revoke-failure-never-reports-success", refused);
            Check(endpoint + "-file-revoke-failure-still-clears-local-intent", Field(locked, "suspendedCurve") is null);
            if (endpoint != "CancelControlIntent") Check(endpoint + "-file-error-does-not-skip-safety-restore", Events().Contains("restore.mock-applied"));
        }
        Call(locked, "CancelControlIntent");
        Check(endpoint + "-retry-after-file-unlock-removes-handoff", !File.Exists(intentFile));
        DisposeFixture(locked);
    }
}
catch (Exception e) { Check("runner-no-unexpected-exception", false); Console.Error.WriteLine(e); }
var result = new { ok, count = checks.Count, checks, hostAssemblyPath = assembly.Location,
    hostAssemblySha256 = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(assembly.Location))).ToLowerInvariant(),
    scope = "production Host takeover startup and persistence; mock hardware; lifecycle/process exclusivity is not exercised" };
File.WriteAllText(Path.Combine(evidence, "result.json"), JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine($"ok={ok} checks={checks.Count}");
return ok ? 0 : 1;

sealed class HostTestLoadContext(string hostPath) : AssemblyLoadContext
{
    private readonly AssemblyDependencyResolver resolver = new(hostPath);
    protected override Assembly? Load(AssemblyName name) { var path = resolver.ResolveAssemblyToPath(name); return path is null ? null : LoadFromAssemblyPath(path); }
}
