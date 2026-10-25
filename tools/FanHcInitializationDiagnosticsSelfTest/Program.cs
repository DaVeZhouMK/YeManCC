// Loads a scratch Host DLL as a library; never starts Host, HC, a backend or a device.
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

if (args.Length != 3)
    throw new ArgumentException("Expected scratch Host DLL, source Program.cs, and evidence directory.");
var hostPath = Path.GetFullPath(args[0]);
var sourcePath = Path.GetFullPath(args[1]);
var evidence = Path.GetFullPath(args[2]);
Directory.CreateDirectory(evidence);
AssemblyLoadContext.Default.Resolving += (_, name) =>
{
    var path = Path.Combine(Path.GetDirectoryName(hostPath)!, name.Name + ".dll");
    return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null;
};
var host = AssemblyLoadContext.Default.LoadFromAssemblyPath(hostPath);
const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance;
var backendType = host.GetType("YeManFanHost.RealHcBackend", throwOnError: true)!;
var runStage = backendType.GetMethod("RunHcInitializationStage", flags)!
    .CreateDelegate<Action<string, Action, Action<string, object?>?>>();
var rows = new List<object>();
var captures = new List<JsonElement>();
var jsonOptions = new JsonSerializerOptions { WriteIndented = true };
void Check(string name, bool ok)
{
    rows.Add(new { name, ok });
    Console.WriteLine((ok ? "PASS " : "FAIL ") + name);
}
Exception? Capture(Action action)
{
    try { action(); return null; }
    catch (Exception error) { return error; }
}
void CaptureDiagnostic(string eventName, object? payload) =>
    captures.Add(JsonSerializer.SerializeToElement(new { eventName, details = payload }));

Exception? original = null;
var calls = 0;
var failure = Capture(() => runStage("device.get-current", () =>
{
    calls++;
    try { typeof(SoftwareFactoryFixture).GetMethod(nameof(SoftwareFactoryFixture.Trigger))!.Invoke(null, null); }
    catch (Exception error) { original = error; throw; }
}, CaptureDiagnostic));
Check("initialization action runs exactly once and fails", calls == 1 && failure is not null);
Check("original outer exception is rethrown by reference", ReferenceEquals(original, failure));
Check("fixture reproduces reflection -> type initialization -> ArgumentException", failure is TargetInvocationException
    { InnerException: TypeInitializationException { InnerException: ArgumentException } });
Check("one failure event", captures.Count == 1 && captures[0].GetProperty("eventName").GetString() == "hc.initialization.failure");
var details = captures[0].GetProperty("details");
Check("fixed stage binds to phase and stage", details.GetProperty("phase").GetString() == "device.get-current" &&
    details.GetProperty("stage").GetString() == "device.get-current");
var chain = details.GetProperty("exceptionChain").EnumerateArray().ToArray();
Check("all three real exception layers retained", chain.Length == 3 &&
    chain[0].GetProperty("exceptionType").GetString() == typeof(TargetInvocationException).FullName &&
    chain[1].GetProperty("exceptionType").GetString() == typeof(TypeInitializationException).FullName &&
    chain[2].GetProperty("exceptionType").GetString() == typeof(ArgumentException).FullName);
var leaf = failure!.InnerException!.InnerException!;
Check("each layer preserves its real HResult", chain.Select(item => item.GetProperty("hResult").GetInt32())
    .SequenceEqual(new[] { failure.HResult, failure.InnerException.HResult, leaf.HResult }));
Check("hex HResult is present", chain[^1].GetProperty("hResultHex").GetString() == $"0x{leaf.HResult:X8}");
Check("failing software type initializer identified", chain[1].GetProperty("typeInitializationType").GetString() == typeof(SoftwareFactoryFixture).FullName);
Check("leaf TargetSite names exact throw method", chain[^1].GetProperty("targetSite").GetString()!
    .Contains("SoftwareFactoryFixture.Void ThrowExpectedRange()", StringComparison.Ordinal));
Check("leaf stack retains original throw method and cctor", chain[^1].GetProperty("stackTrace").GetString()!
    .Contains("SoftwareFactoryFixture.ThrowExpectedRange", StringComparison.Ordinal) &&
    chain[^1].GetProperty("stackTrace").GetString()!.Contains("SoftwareFactoryFixture..cctor", StringComparison.Ordinal));
Check("leaf stack is unchanged by instrumentation", chain[^1].GetProperty("stackTrace").GetString() == leaf.StackTrace);
Check("outer rethrow retains fixture reflection origin", failure.StackTrace!.Contains("System.Reflection", StringComparison.Ordinal));
Check("UI reason inputs retain original messages", failure.Message == original!.Message &&
    leaf.Message.Contains(SoftwareOnlySecrets.MessageMarker, StringComparison.Ordinal));
var serialized = JsonSerializer.Serialize(captures);
Check("exception Data and freeform messages cannot enter payload", !serialized.Contains(SoftwareOnlySecrets.MessageMarker, StringComparison.Ordinal) &&
    !serialized.Contains(SoftwareOnlySecrets.DataMarker, StringComparison.Ordinal) &&
    chain.All(item => !item.TryGetProperty("message", out _) && !item.TryGetProperty("data", out _)));

var stages = new[] { "validation", "assembly.load", "assembly.configure", "motherboard.collect", "device.type",
    "device.get-current", "device.construct", "device.initialize", "device.identity" };
var source = File.ReadAllText(sourcePath);
var methodStart = source.IndexOf("    private void LoadAssemblyAndFactory()", StringComparison.Ordinal);
var methodEnd = source.IndexOf("    /**", methodStart, StringComparison.Ordinal);
var loadMethod = source[methodStart..methodEnd];
var actualStages = Regex.Matches(loadMethod, "RunHcInitializationStage\\(\"([^\"]+)\"")
    .Select(match => match.Groups[1].Value).ToArray();
Check("production initialization sites bind all fixed stages in existing order", actualStages.SequenceEqual(stages));
Check("fan-only initialization contains no motion/serial discovery call", !loadMethod.Contains("\"PullSensors\"", StringComparison.Ordinal));
foreach (var stage in stages)
{
    JsonElement? captured = null;
    var stopped = true;
    var caught = Capture(() =>
    {
        runStage(stage, () => ExceptionDispatchInfo.Capture(failure).Throw(), (_, payload) => captured = JsonSerializer.SerializeToElement(payload));
        stopped = false;
    });
    Check("stage binding and failure propagation: " + stage, ReferenceEquals(failure, caught) && stopped &&
        captured!.Value.GetProperty("stage").GetString() == stage);
}
Check("missing diagnostic cannot suppress or replace failure", ReferenceEquals(failure,
    Capture(() => runStage("device.initialize", () => ExceptionDispatchInfo.Capture(failure).Throw(), null))));
Check("throwing diagnostic cannot suppress or replace failure", ReferenceEquals(failure,
    Capture(() => runStage("device.initialize", () => ExceptionDispatchInfo.Capture(failure).Throw(), (_, _) => throw new IOException("synthetic log failure")))));
var successCalls = 0;
var successLogs = 0;
var successError = Capture(() => runStage("validation", () => successCalls++, (_, _) => successLogs++));
Check("successful stage runs once with no failure event", successError is null && successCalls == 1 && successLogs == 0);

// Verify the existing disk channel and its gate using an explicit isolated log directory.
var logRoot = Path.Combine(evidence, "logs");
Directory.CreateDirectory(logRoot);
var logType = host.GetType("YeManFanHost.HostDiagnosticLog", throwOnError: true)!;
var log = Activator.CreateInstance(logType, flags, binder: null, args: [logRoot, 0], culture: null)!;
var write = logType.GetMethod("Write", flags)!.CreateDelegate<Action<string, object?>>(log);
var logPath = (string)logType.GetProperty("Path", flags)!.GetValue(log)!;
var flagPath = Path.Combine(logRoot, "fan-logging-enabled.flag");
if (File.Exists(flagPath)) File.Delete(flagPath);
var beforeGateClosed = File.Exists(logPath) ? File.ReadAllText(logPath) : "";
var closedFailure = Capture(() => runStage("assembly.configure", () => ExceptionDispatchInfo.Capture(failure).Throw(), write));
Check("existing closed log gate remains effective and failure still propagates", ReferenceEquals(failure, closedFailure) &&
    (File.Exists(logPath) ? File.ReadAllText(logPath) : "") == beforeGateClosed);
File.WriteAllText(flagPath, "enabled");
var diskFailure = Capture(() => runStage("motherboard.collect", () => ExceptionDispatchInfo.Capture(failure).Throw(), write));
Check("HostDiagnosticLog disk write does not change failure", ReferenceEquals(failure, diskFailure));
var diskLine = File.ReadLines(logPath).Last();
using var diskDocument = JsonDocument.Parse(diskLine);
var diskRecord = diskDocument.RootElement;
var diskDetails = diskRecord.GetProperty("details");
Check("existing log format retains stage and inner method and stack", diskRecord.GetProperty("eventName").GetString() == "hc.initialization.failure" &&
    diskDetails.GetProperty("phase").GetString() == "motherboard.collect" &&
    diskDetails.GetProperty("exceptionChain")[2].GetProperty("stackTrace").GetString()!.Contains("ThrowExpectedRange", StringComparison.Ordinal) &&
    diskDetails.GetProperty("exceptionChain")[2].GetProperty("targetSite").GetString()!.Contains("ThrowExpectedRange", StringComparison.Ordinal));
Check("disk log excludes synthetic message and Data secrets", !diskLine.Contains(SoftwareOnlySecrets.MessageMarker, StringComparison.Ordinal) &&
    !diskLine.Contains(SoftwareOnlySecrets.DataMarker, StringComparison.Ordinal));
Check("HandheldCompanion assembly was never loaded", !AppDomain.CurrentDomain.GetAssemblies()
    .Any(assembly => assembly.GetName().Name?.StartsWith("HandheldCompanion", StringComparison.OrdinalIgnoreCase) == true));
await FanColdStartupChecks.Run(host, evidence, Check);
var ok = rows.All(row => (bool)row.GetType().GetProperty("ok")!.GetValue(row)!);
File.WriteAllText(Path.Combine(evidence, "nested-exception-diagnostic.json"), JsonSerializer.Serialize(captures[0], jsonOptions));
File.WriteAllText(Path.Combine(evidence, "result.json"), JsonSerializer.Serialize(new
{
    ok,
    checks = rows.Count,
    hostAssemblySha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(hostPath))).ToLowerInvariant(),
    sourceSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(sourcePath))).ToLowerInvariant(),
    scope = "actual scratch Host diagnostics plus authenticated in-process LocalFanHostServer/engine with an inert preloaded metadata device; no Host Main, HC assembly, real hardware, EC/ACPI/WMI or sensor enumeration",
    results = rows,
}, jsonOptions));
return ok ? 0 : 1;

internal static class SoftwareOnlySecrets
{
    internal const string MessageMarker = "synthetic-token-lease-authorization-message-secret";
    internal const string DataMarker = "synthetic-exception-data-secret";
}
internal static class SoftwareFactoryFixture
{
    static SoftwareFactoryFixture() => ThrowExpectedRange();
    public static void Trigger() { }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void ThrowExpectedRange()
    {
        var error = new ArgumentException("Value does not fall within the expected range. " + SoftwareOnlySecrets.MessageMarker);
        error.Data["authorization"] = SoftwareOnlySecrets.DataMarker;
        throw error;
    }
}