using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
using System.Windows.Media;

internal static class LightingSignalsHarness
{
    const string Model = "HandheldCompanion.Devices.OneXPlayerX1Mini";
    static readonly List<object> Cases = [];
    static int failures;
    static object? Invoke(string name, params object?[] args)
    {
        try { return typeof(Program).GetMethod(name, BindingFlags.Static | BindingFlags.NonPublic)!.Invoke(null, args); }
        catch (TargetInvocationException ex) { throw ex.InnerException!; }
    }
    static void Check(bool valid, string reason) { if (!valid) throw new Exception(reason); }
    static void Case(string id, object input, string consumer, Func<object> execute)
    {
        try { Cases.Add(new { id, input, consumer, state = execute(), feedback = "expected contract observed", status = "PASS" }); }
        catch (Exception ex) { failures++; Cases.Add(new { id, input, consumer, state = (object?)null, feedback = ex.GetBaseException().Message, status = "FAIL" }); }
    }
    static object Reject(Action action, Func<object> state, Func<bool> untouched)
    {
        try { action(); } catch (Exception ex) when (ex is ArgumentException or NotSupportedException or InvalidOperationException or MissingMethodException)
        { Check(untouched(), "rejected after operation was dispatched"); return new { rejected = true, error = ex.Message, observed = state() }; }
        throw new Exception("invalid input was accepted");
    }
    static string Action(object device, string action, string? status = "off", int? brightness = null, string? level = "solid") =>
        (string)Invoke("ExecuteLedAction", device, device.GetType(), action, level, status, 10, 20, 30, null, null, null, brightness, 75)!;
    static Dictionary<string, object?> Receipt(string note = "ok") => new()
    {
        ["deviceClass"] = Model, ["action"] = "color", ["note"] = note,
        ["ready"] = true, ["opened"] = true, ["released"] = true,
        ["writeAttempts"] = 1, ["physicalEffectVerified"] = false,
    };
    static (int Exit, JsonElement Json, bool Executed) Helper(string core, Dictionary<string, object?> receipt,
        params string[] suffix)
    {
        Environment.SetEnvironmentVariable("YMCC_LIGHTING_FIXTURE_RECEIPT", JsonSerializer.Serialize(receipt));
        Environment.SetEnvironmentVariable("YMCC_LIGHTING_FIXTURE_EXECUTED", null);
        var args = new[] { "--device-core-assembly", core, "--expected-device-class", Model, "--action", "color", "--level", "solid" }.Concat(suffix).ToArray();
        var stdout = Console.Out;
        using var captured = new StringWriter();
        try
        {
            Console.SetOut(captured);
            int exit = (int)Invoke("Main", (object)args)!;
            return (exit, JsonDocument.Parse(captured.ToString()).RootElement.Clone(),
                Environment.GetEnvironmentVariable("YMCC_LIGHTING_FIXTURE_EXECUTED") == "true");
        }
        finally { Console.SetOut(stdout); }
    }
    static object Feedback((int Exit, JsonElement Json, bool Executed) result) =>
        new { exitCode = result.Exit, json = result.Json, fixtureExecuted = result.Executed, hardwareIo = false };
    [STAThread]
    public static int Main(string[] args)
    {
        string core = args[0];
        if (args.Length > 1 && args[1] == "--preload")
        {
            _ = AssemblyLoadContext.Default.LoadFromAssemblyPath(core);
            var result = Helper(core, Receipt());
            bool passed = result.Exit == 1 && !result.Executed && result.Json.ToString().Contains("LOADED_ORIGIN_UNVERIFIED");
            Console.WriteLine(JsonSerializer.Serialize(new { schema = "ymcc.lighting-signals.v1", id = "unverified-loaded-image",
                input = new { sameNameAssemblyAlreadyLoaded = true }, consumer = "DeviceCoreConsumer.TryExecute -> RuntimeDependencyContract.LoadPinned",
                state = Feedback(result), feedback = "foreign-loaded image must be rejected", status = passed ? "PASS" : "FAIL", hardwareIo = false }));
            return passed ? 0 : 1;
        }
        Case("legacy-color-success", new { action = "color", brightness = 50 }, "Program.ExecuteLedAction", () =>
        {
            var d = new FakeDevice(); Check(Action(d, "color", brightness: 50) == "ok", "color failed");
            Check(d.Calls.SequenceEqual(new[] { "color", "brightness" }), "color order wrong");
            return new { d.Calls, d.Brightness };
        });
        foreach (string status in new[] { "on", "off", "ON" }) Case("status-" + status, new { action = "status", status }, "Program.ExecuteLedAction", () =>
        {
            var d = new FakeDevice(); Check(Action(d, "status", status) == "ok", "status failed");
            Check(d.Status == !status.Equals("off", StringComparison.OrdinalIgnoreCase), "status polarity wrong");
            return new { d.Calls, d.Status };
        });
        foreach (string? status in new string?[] { "of", "", null }) Case("reject-status-" + (status ?? "missing"), new { action = "status", status }, "Program.ExecuteLedAction", () =>
        {
            var d = new FakeDevice(); return Reject(() => Action(d, "status", status), () => new { d.Calls, d.Status }, () => d.Calls.Count == 0);
        });
        foreach (string action in new[] { "color", "brightness", "status", "off" }) Case("reject-base-noop-" + action, new { action, device = "inherited HC IDevice default" }, "Program.ExecuteLedAction", () =>
        {
            var d = new HandheldCompanion.Devices.IDevice();
            return Reject(() => Action(d, action), () => new { d.Calls }, () => d.Calls.Count == 0);
        });
        Case("valid-core-receipt", Receipt(), "Program.Main -> DeviceCoreConsumer.TryExecute", () =>
        {
            var result = Helper(core, Receipt());
            Check(result.Exit == 0 && result.Json.GetProperty("ok").GetBoolean() && result.Executed, "valid receipt rejected");
            Check(result.Json.GetProperty("result").GetProperty("hcLoaded").GetBoolean() == false, "unexpected HC load");
            Check(result.Json.GetProperty("result").GetProperty("capabilityId").GetString() == "hc.device.OneXPlayerX1Mini", "unified capability identity missing");
            return Feedback(result);
        });
        foreach(var request in new[] {
            new[] { "--level", "wave" },
            new[] { "--expected-device-class", "Unreviewed.Vendor.OneXPlayerX1Mini" },
            new[] { "--action", "preset", "--preset-id", "LEDPreset_OneXPlayerX1_Preset09" },
            new[] { "--action", "status", "--status", "on" },
            new[] { "--action", "off" } })
            Case("catalog-refuses-unreviewed-core-claim-" + string.Join('-',request), request, "Program.Main -> unified catalog -> DeviceCoreConsumer", () =>
            {
                var result=Helper(core,Receipt(),request);
                Check(result.Exit==1&&!result.Executed,"unreviewed core claim reached Execute or was accepted");
                return Feedback(result);
            });
        Case("operation-failure-envelope", Receipt("setLedColor-false"), "Program.Main -> DeviceCoreConsumer.TryExecute", () =>
        {
            var result = Helper(core, Receipt("setLedColor-false"));
            Check(result.Exit == 1 && !result.Json.GetProperty("ok").GetBoolean(), "failed operation advertised ok:true");
            Check(result.Json.GetProperty("result").GetProperty("note").GetString() == "setLedColor-false", "device failure note lost");
            return Feedback(result);
        });
        var corruptions = new (string Id, Action<Dictionary<string, object?>> Mutate)[] {
            ("wrong-device", r => r["deviceClass"] = "HandheldCompanion.Devices.Unknown"),
            ("wrong-action", r => r["action"] = "brightness"),
            ("unreleased", r => r["released"] = false),
            ("missing-ready", r => r.Remove("ready")),
            ("not-opened", r => r["opened"] = false),
            ("no-write", r => r["writeAttempts"] = 0),
            ("extra-write", r => r["writeAttempts"] = 2),
            ("unknown-note", r => r["note"] = "saved"),
            ("missing-note", r => r.Remove("note")),
            ("claimed-physical", r => r["physicalEffectVerified"] = true),
            ("unrelated-failure", r => r["note"] = "setLedBrightness-false"),
        };
        foreach (var corruption in corruptions) { var receipt = Receipt(); corruption.Mutate(receipt);
            Case("receipt-" + corruption.Id, receipt, "Program.Main -> DeviceCoreConsumer.TryExecute", () =>
            {
                var result = Helper(core, receipt);
                Check(result.Exit == 1 && !result.Json.GetProperty("ok").GetBoolean(), "invalid receipt accepted");
                return Feedback(result);
            });
        }
        Case("combined-write-receipt", new { action = "color", brightness = 40, writeAttempts = 2 }, "Program.Main -> DeviceCoreConsumer.TryExecute", () =>
        {
            var receipt = Receipt(); receipt["writeAttempts"] = 2;
            var result = Helper(core, receipt, "--brightness", "40"); Check(result.Exit == 0, "combined valid receipt rejected"); return Feedback(result);
        });
        Case("unsupported-no-fallback", new { supports = false }, "Program.Run -> DeviceCoreConsumer.TryExecute", () =>
        {
            Environment.SetEnvironmentVariable("YMCC_LIGHTING_FIXTURE_UNSUPPORTED", "true");
            try { var result = Helper(core, Receipt()); Check(result.Exit == 1 && !result.Executed, "unsupported capability executed"); return Feedback(result); }
            finally { Environment.SetEnvironmentVariable("YMCC_LIGHTING_FIXTURE_UNSUPPORTED", null); }
        });
        Case("caller-pin-rejected", new { callerSha256 = new string('0', 64) }, "DeviceCoreConsumer.TryExecute", () =>
        {
            var result = Helper(core, Receipt(), "--device-core-sha256", new string('0', 64));
            Check(result.Exit == 1 && !result.Executed, "caller changed trusted pin"); return Feedback(result);
        });
        foreach (var command in new[] { new[] { "--action", "color", "--level", "unknown" }, new[] { "--action", "preset" }, new[] { "--action", "status", "--status", "of" } })
            Case("preflight-" + command[1], command, "Program.Run", () =>
            {
                var output = Console.Out; using var captured = new StringWriter(); int exit;
                try { Console.SetOut(captured); exit = (int)Invoke("Main", (object)command)!; } finally { Console.SetOut(output); }
                var json = JsonDocument.Parse(captured.ToString()).RootElement.Clone();
                Check(exit == 1 && !json.GetProperty("error").GetString()!.Contains("assembly"), "invalid request reached runtime selection");
                return new { exitCode = exit, json };
            });
        var resolve = typeof(DeviceCoreConsumer).GetMethod("ResolveApi", BindingFlags.Static | BindingFlags.NonPublic);
        if (resolve is not null) foreach (var type in new[] { typeof(ApiV1), typeof(ApiV2), typeof(BadSupports), typeof(BadExecute) })
            Case("abi-" + type.Name, new { api = type.Name }, "DeviceCoreConsumer.ResolveApi", () =>
            {
                try { _ = resolve.Invoke(null, [type]); Check(type == typeof(ApiV1), "incompatible API accepted"); return new { compatible = true }; }
                catch (TargetInvocationException ex) { Check(type != typeof(ApiV1), "valid API rejected"); return new { compatible = false, difference = ex.GetBaseException().Message }; }
            });
        Console.WriteLine(JsonSerializer.Serialize(new { schema = "ymcc.lighting-signals.v1", cases = Cases, failures, hardwareIo = false,
            loadedAssemblies = AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetName().Name).Order().ToArray() }));
        return failures == 0 ? 0 : 1;
    }
}
internal sealed class FakeDevice
{
    public List<string> Calls { get; } = []; public bool Status; public int Brightness;
    public bool SetLedColor(Color a, Color b, FakeLevel mode, int speed) { Calls.Add("color"); return true; }
    public bool SetLedBrightness(int value) { Calls.Add("brightness"); Brightness = value; return true; }
    public bool SetLedStatus(bool value) { Calls.Add("status"); Status = value; return true; }
}
internal enum FakeLevel { Solid = 1 }
internal static class ApiV1 { public const int ApiVersion = 1; public static bool Supports(string a, string b, string c) => true; public static string Execute(string a, string b) => ""; }
internal static class ApiV2 { public const int ApiVersion = 2; public static bool Supports(string a, string b, string c) => true; public static string Execute(string a, string b) => ""; }
internal static class BadSupports { public const int ApiVersion = 1; public static int Supports(string a, string b, string c) => 1; public static string Execute(string a, string b) => ""; }
internal static class BadExecute { public const int ApiVersion = 1; public static bool Supports(string a, string b, string c) => true; public static bool Execute(string a, string b) => true; }
namespace HandheldCompanion.Devices
{
    // Mirrors the pinned source's no-op bodies; this class has no hardware code.
    internal class IDevice
    {
        public List<string> Calls { get; } = [];
        public virtual bool SetLedColor(Color a, Color b, FakeLevel mode, int speed) { Calls.Add("noop-color"); return true; }
        public virtual bool SetLedBrightness(int value) { Calls.Add("noop-brightness"); return true; }
        public virtual bool SetLedStatus(bool value) { Calls.Add("noop-status"); return true; }
    }
}



