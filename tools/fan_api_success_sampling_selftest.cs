using System.Collections;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
using System.Text.Json.Serialization;

internal sealed record LegacySuccessDetails(string RequestId, string Method, string? Path, string? State, double DurationMs, int HttpStatus);
internal sealed class CountingFactory : JsonConverterFactory
{
    internal static int Materializations;
    internal required JsonSerializerOptions Plain;
    public override bool CanConvert(Type type) => type == typeof(LegacySuccessDetails) || type.Name == "ApiRequestSuccessDetails";
    public override JsonConverter CreateConverter(Type type, JsonSerializerOptions options) =>
        (JsonConverter)Activator.CreateInstance(typeof(CountingConverter<>).MakeGenericType(type), Plain)!;
    private sealed class CountingConverter<T>(JsonSerializerOptions plain) : JsonConverter<T>
    {
        public override T? Read(ref Utf8JsonReader reader, Type type, JsonSerializerOptions options) => throw new NotSupportedException();
        public override void Write(Utf8JsonWriter writer, T value, JsonSerializerOptions options)
        {
            Materializations++;
            JsonSerializer.Serialize(writer, value, plain);
        }
    }
}
internal static class SamplingChecks
{
    private const BindingFlags All = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
    private static readonly List<string> Passed = [];
    private static readonly List<string> Failures = [];
    private static readonly List<object> Observations = [];
    private static Type LogType = null!;
    private static MethodInfo Write = null!;
    private static MethodInfo? TypedWrite;
    private static string Root = "";
    private static bool ExpectTyped;
    private static object Log(string name, bool enabled = true)
    {
        var directory = Path.Combine(Root, name); Directory.CreateDirectory(directory);
        if (enabled) File.WriteAllText(Path.Combine(directory, "fan-logging-enabled.flag"), "enabled");
        return Activator.CreateInstance(LogType, All, null, new object?[] { directory, 0 }, null)!;
    }
    private static string LogPath(object log) => (string)LogType.GetProperty("Path", All)!.GetValue(log)!;
    private static List<JsonElement> Rows(object log) => !File.Exists(LogPath(log)) ? [] : File.ReadAllLines(LogPath(log)).Where(x => x.Length > 0).Select(x => JsonDocument.Parse(x).RootElement.Clone()).ToList();
    private static void Emit(object log, string path = "/api/heartbeat", string state = "Ready", double duration = 1.25)
    {
        if (TypedWrite is not null) TypedWrite.Invoke(log, ["request-fixed", "POST", path, state, duration]);
        else Write.Invoke(log, ["api.request-success", new LegacySuccessDetails("request-fixed", "POST", path, state, duration, 200)]);
    }
    private static void Assert(bool value, string reason) { if (!value) throw new InvalidOperationException(reason); }
    private static void Check(string name, Action work) { try { work(); Passed.Add(name); Console.WriteLine("PASS " + name); } catch (Exception error) { Failures.Add(name + ": " + error.GetBaseException().Message); Console.WriteLine("FAIL " + name + ": " + error.GetBaseException().Message); } }
    public static int Main(string[] args)
    {
        try
        {
            var dll = Path.GetFullPath(args[0]); Root = Path.GetFullPath(args[1]); ExpectTyped = args.Length > 2 && args[2] == "typed";
            if (Directory.Exists(Root) && Directory.EnumerateFileSystemEntries(Root).Any()) throw new InvalidOperationException("Use a fresh evidence directory.");
            Directory.CreateDirectory(Root);
            AssemblyLoadContext.Default.Resolving += (_, name) => { var p = Path.Combine(Path.GetDirectoryName(dll)!, name.Name + ".dll"); return File.Exists(p) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(p) : null; };
            var assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);
            LogType = assembly.GetType("YeManFanHost.HostDiagnosticLog", true)!;
            Write = LogType.GetMethod("Write", All)!; TypedWrite = LogType.GetMethod("WriteApiRequestSuccess", All);
            var options = (JsonSerializerOptions)assembly.GetType("YeManFanHost.JsonDefaults", true)!.GetField("Options", All)!.GetValue(null)!;
            options.Converters.Insert(0, new CountingFactory { Plain = new JsonSerializerOptions(options) });
            Check("gate off: no detail serialization and no runtime row", () => {
                var log = Log("off", false); CountingFactory.Materializations = 0; Emit(log);
                Assert(CountingFactory.Materializations == 0 && Rows(log).Count == 0, "gate-off materialized details");
            });
            var detailsType = LogType.GetNestedType("ApiRequestSuccessDetails", All);
            var constructionProbe = detailsType?.GetField("ConstructionsForSelfTest", All);
            var requireConstructionProbe = Environment.GetEnvironmentVariable("YMCC_FAN_SUCCESS_CONSTRUCTION_PROBE") == "1";
            Check("construction evidence mode requires its explicit scratch-only witness", () =>
                Assert(requireConstructionProbe ? constructionProbe is not null : constructionProbe is null, "constructor witness must exist only in explicit scratch-probe mode"));
            Check(constructionProbe is not null ? "ten closed-gate successes retain zero detail construction" : "ten closed-gate successes have no rows (unprobed production DLL)", () => {
                var log = Log("closed-details", false);
                var before = constructionProbe is null ? -1 : (int)constructionProbe.GetValue(null)!;
                for (var i = 0; i < 10; i++) Emit(log);
                var after = constructionProbe is null ? -1 : (int)constructionProbe.GetValue(null)!;
                if (constructionProbe is not null) Observations.Add(new { scenario = "ten-closed-successes", requests = 10, constructionDelta = after-before });
                Assert(Rows(log).Count == 0, "closed success leaked a runtime row");
                if (constructionProbe is not null) Assert(after == before, "closed successes constructed " + (after-before) + " details");
            });
            Check("success gate toggles stay live and state-changing rows remain complete", () => {
                var log = Log("gate-toggle", false);
                var flag = System.IO.Path.Combine(System.IO.Path.GetDirectoryName(LogPath(log))!, "fan-logging-enabled.flag");
                Emit(log); File.WriteAllText(flag, "enabled"); Emit(log);
                File.WriteAllText(flag, "disabled"); Emit(log, state: "Suspended");
                File.WriteAllText(flag, "1"); Emit(log, state: "Suspended");
                var rows = Rows(log); Assert(rows.Count == 2, "gate decisions were cached or rows disappeared");
                Assert(rows[0].GetProperty("details").GetProperty("state").GetString() == "Ready" && rows[1].GetProperty("details").GetProperty("state").GetString() == "Suspended", "state-changing successes lost original details");
            });
            Check("unreadable success flag stays diagnostic-only and recovery is immediate", () => {
                var log = Log("flag-locked");
                var flag = System.IO.Path.Combine(System.IO.Path.GetDirectoryName(LogPath(log))!, "fan-logging-enabled.flag");
                using (var held = new FileStream(flag, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) Emit(log);
                Assert(Rows(log).Count == 0, "unreadable gate allowed success log");
                Emit(log); Assert(Rows(log).Count == 1, "gate read failure poisoned a later success");
            });
            Check("self-test capture still observes success before an unreadable disk gate", () => {
                var log = Log("capture-flag-locked"); var capture = LogType.GetProperty("SelfTestCaptureEnabled", All)!;
                var flag = System.IO.Path.Combine(System.IO.Path.GetDirectoryName(LogPath(log))!, "fan-logging-enabled.flag");
                LogType.GetMethod("ResetSelfTestCapture", All)!.Invoke(null, null); capture.SetValue(null, true);
                try {
                    using (var held = new FileStream(flag, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) Emit(log);
                    var captured = ((IEnumerable)LogType.GetProperty("SelfTestCaptured", All)!.GetValue(null)!).Cast<object>().ToList();
                    Assert(captured.Count == 1 && Rows(log).Count == 0, "early flag read discarded self-test evidence");
                } finally { capture.SetValue(null, false); LogType.GetMethod("ResetSelfTestCapture", All)!.Invoke(null, null); }
            });

            int steadySerializations = 0;
            Check("ten unchanged successes: one row; materialize only emitted typed details", () => {
                var log = Log("steady"); CountingFactory.Materializations = 0;
                for (var i = 0; i < 10; i++) Emit(log);
                steadySerializations = CountingFactory.Materializations;
                Assert(Rows(log).Count == 1, "success sampling changed");
                Assert(steadySerializations == (ExpectTyped ? 1 : 10), "unexpected detail serialization count " + steadySerializations);
            });
            Check("typed output details equal the legacy public logger payload", () => {
                var typed = Log("shape-typed"); var legacy = Log("shape-legacy"); Emit(typed);
                Write.Invoke(legacy, ["api.request-success", new LegacySuccessDetails("request-fixed", "POST", "/api/heartbeat", "Ready", 1.25, 200)]);
                Assert(JsonElement.DeepEquals(Rows(typed).Single().GetProperty("details"), Rows(legacy).Single().GetProperty("details")), "payload shape drift");
            });
            Check("state changes stay immediate; endpoint slots stay independent", () => {
                var log = Log("changes"); Emit(log); Emit(log, state: "Resuming"); Emit(log, state: "Resuming"); Emit(log, "/api/state", "Resuming"); Emit(log, "/health", "Resuming");
                Assert(Rows(log).Count == 4, "state change or endpoint evidence lost");
            });
            Check("30-second sampling expiry emits unchanged state", () => {
                var log = Log("expired"); Emit(log);
                var slots = (IDictionary)LogType.GetField("pollingSuccessLastWrite", All)!.GetValue(log)!;
                slots["/api/heartbeat|200"] = (Environment.TickCount64 - 30_001, "Ready"); Emit(log);
                Assert(Rows(log).Count == 2, "sampling expiry altered");
            });
            Check("enable success and failure events are not sampled", () => {
                var log = Log("unsampled"); for (var i = 0; i < 3; i++) Emit(log, "/api/enable");
                for (var i = 0; i < 3; i++) Write.Invoke(log, ["api.request-failure", new LegacySuccessDetails("request-fixed", "POST", "/api/heartbeat", "FaultLocked", 1, 409)]);
                Assert(Rows(log).Count == 6, "nonpolling or failed evidence sampled");
            });
            Check("nonfinite details preserve fallback without poisoning later samples", () => {
                var log = Log("invalid"); Emit(log, duration: double.NaN); Assert(Rows(log).Count == 0, "strict JSON accepted NaN"); Emit(log);
                Assert(Rows(log).Count == 1, "invalid details poisoned later sample");
            });
            Check("arbitrary legacy payloads retain the old JSON path", () => {
                var log = Log("legacy"); CountingFactory.Materializations = 0;
                for (var i = 0; i < 3; i++) Write.Invoke(log, ["api.request-success", new LegacySuccessDetails("old", "GET", "/health", "Ready", 0, 200)]);
                Assert(CountingFactory.Materializations == 3 && Rows(log).Count == 1, "legacy compatibility changed");
            });
            Check("self-test capture remains complete with disk gate off", () => {
                var log = Log("capture", false); var capture = LogType.GetProperty("SelfTestCaptureEnabled", All)!;
                LogType.GetMethod("ResetSelfTestCapture", All)!.Invoke(null, null); capture.SetValue(null, true);
                try { Emit(log); var values = (IEnumerable)LogType.GetProperty("SelfTestCaptured", All)!.GetValue(null)!; Assert(values.Cast<object>().Count() == 1 && Rows(log).Count == 0, "capture or gate changed"); }
                finally { capture.SetValue(null, false); LogType.GetMethod("ResetSelfTestCapture", All)!.Invoke(null, null); }
            });
            Console.WriteLine(JsonSerializer.Serialize(new { ok = Failures.Count == 0, checks = Passed.Count + Failures.Count, failures = Failures, observations = Observations, constructorWitnessAvailable = constructionProbe is not null, constructorWitnessRequired = requireConstructionProbe, steadyRequests = 10, steadyRows = 1, steadyDetailSerializations = steadySerializations, typedMethodPresent = TypedWrite is not null, expectedMode = ExpectTyped ? "typed" : "legacy", hardwareCalls = 0, cpuBenchmark = false, scope = "actual HostDiagnosticLog; real JSON converter counts and isolated files only; no Host Main, HC, IPC or devices", passed = Passed }));
            return Failures.Count == 0 ? 0 : 1;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
