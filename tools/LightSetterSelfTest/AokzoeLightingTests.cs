using System.Diagnostics;
using System.Reflection;
using System.Text.Json;
using HandheldCompanion.Devices;
using YeManFanBackend.Capabilities;

// Executes the production catalog and action consumer with memory terminals.
// Command order/elapsed time are software evidence, never physical light readback.
internal static class AokzoeLightingTests
{
    internal static void Run()
    {
        var checks = 0;
        var cases = new List<string>();
        var spacing = new List<double>();
        void Check(bool value, string name) { if (!value) throw new Exception("AOKZOE: " + name); checks++; }
        void Case(string name, Action body) { body(); cases.Add(name); }
        string Apply(object device, string action, string level = "solid", int? brightness = 65, string? expected = null) =>
            (string)typeof(Program).GetMethod("ExecuteCatalogLedAction", BindingFlags.Static | BindingFlags.NonPublic)!
                .Invoke(null, [device, device.GetType(), expected ?? device.GetType().FullName, action, level, "off",
                    10, 20, 30, 40, 50, 60, brightness, 75, null])!;
        void On(AOKZOEA1 device, string level, int brightness = 65)
        {
            Check(Apply(device, "color", level, brightness) == "ok", "color result");
            Check(device.LightingCommands.Select(c => c.Command).SequenceEqual(new[] { "brightness", "color" }), "brightness precedes color exactly once");
            Check(device.LightingCommands[0].Value == brightness, "explicit brightness retained");
            Check(device.LightingCommands[1].Value == (level == "solid" ? 1 : 4), "selected effect retained");
            var elapsed = Stopwatch.GetElapsedTime(device.LightingCommands[0].Tick, device.LightingCommands[1].Tick).TotalMilliseconds;
            spacing.Add(elapsed);
            Check(elapsed >= 290, "real 300ms settle before color");
            Check(device.DriverInitializations == 0 && device.EcWrites.Count == 0, "no device Open or EC");
        }
        Func<AOKZOEA1>[] families = [() => new AOKZOEA1(), () => new AOKZOEA1Pro(), () => new AOKZOEA1X(), () => new AOKZOEA2()];
        var catalogClasses = HcLightingCapabilityCatalog.Bindings
            .Where(b => string.Equals(b.Device.Family, "aokzoe", StringComparison.OrdinalIgnoreCase))
            .Select(b => b.Device.ConsumerClass).Order().ToArray();
        Check(catalogClasses.SequenceEqual(families.Select(f => f().GetType().FullName!).Order()), "all current catalog AOKZOE lighting identities covered");
        foreach (var create in families)
        {
            var id = create().GetType().Name;
            Case(id + "/fresh-solid", () => On(create(), "solid"));
            Case(id + "/off-new-helper-reenable", () =>
            {
                var off = create();
                Check(Apply(off, "off", brightness: null) == "ok", "original off result");
                Check(off.LightingCommands.Select(c => (c.Command, c.Value)).SequenceEqual(new[] { ("brightness", 0), ("color", 1) }), "original off commands preserved");
                On(create(), "solid");
            });
            Case(id + "/rainbow-reenable", () => On(create(), "rainbow"));
            Case(id + "/brightness-false-stops-color", () =>
            {
                var d = create(); d.BrightnessOk = false;
                Check(Apply(d, "color") == "setLedBrightness-false", "brightness error preserved");
                Check(d.LightingCommands.Count == 1 && d.LightingCommands[0].Command == "brightness", "no color after failed enable");
            });
            Case(id + "/color-false-preserved", () =>
            {
                var d = create(); d.ColorOk = false;
                Check(Apply(d, "color") == "setLedColor-false", "color error preserved");
                Check(d.LightingCommands.Select(c => c.Command).SequenceEqual(new[] { "brightness", "color" }), "failed color not replayed");
            });
            Case(id + "/no-brightness-no-enable", () =>
            {
                var d = create(); Check(Apply(d, "color", brightness: null) == "ok", "color-only accepted");
                Check(d.LightingCommands.Count == 1 && d.LightingCommands[0].Command == "color", "no invented brightness");
            });
            Case(id + "/zero-brightness-original-order", () =>
            {
                var d = create(); Check(Apply(d, "color", brightness: 0) == "ok", "zero accepted");
                Check(d.LightingCommands.Select(c => (c.Command, c.Value)).SequenceEqual(new[] { ("color", 1), ("brightness", 0) }), "zero preserves original semantics");
            });
            Case(id + "/brightness-only", () =>
            {
                var d = create(); Check(Apply(d, "brightness") == "ok", "brightness-only accepted");
                Check(d.LightingCommands.Count == 1 && d.LightingCommands[0].Command == "brightness", "no extra color");
            });
            Case(id + "/wrong-identity-before-write", () =>
            {
                var d = create(); var rejected = false;
                try { Apply(d, "color", expected: "HandheldCompanion.Devices.OneXPlayerG1"); }
                catch (TargetInvocationException ex) when (ex.InnerException is InvalidOperationException) { rejected = true; }
                Check(rejected && d.LightingCommands.Count == 0, "catalog identity mismatch rejects before writes");
            });
            Case(id + "/unsupported-effect-before-write", () =>
            {
                var d = create(); var rejected = false;
                try { Apply(d, "color", level: "breathing"); }
                catch (TargetInvocationException ex) when (ex.InnerException is NotSupportedException) { rejected = true; }
                Check(rejected && d.LightingCommands.Count == 0, "catalog effect mismatch rejects before writes");
            });
        }
        Case("unrelated-family-original-order", () =>
        {
            var d = new FakeDevice();
            var note = typeof(Program).GetMethod("ExecuteLedAction", BindingFlags.Static | BindingFlags.NonPublic)!
                .Invoke(null, [d, d.GetType(), "color", "solid", "off", 10, 20, 30, 40, 50, 60, 65, 75]);
            Check((string)note! == "ok" && d.Calls.SequenceEqual(new[] { "color", "brightness" }), "other family order retained");
        });
        Check(!AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "HandheldCompanion"), "no HC assembly loaded");
        Console.WriteLine(JsonSerializer.Serialize(new { ok = true, scope = "aokzoe-hid-v2-reenable", cases, checks,
            modelCount = families.Length, observedSpacingMs = spacing, productionCatalogAndAction = true,
            hardwareIo = false, physicalEffectVerified = false, defaultSuiteRun = false }));
    }
}
