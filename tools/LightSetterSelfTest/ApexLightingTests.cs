using System.IO;
using System.Text.Json;
using YeManFanBackend.Capabilities;

// Extends the existing one-shot helper consumer fixture. All endpoints are
// memory doubles; neither helper Main nor the real Windows open method runs.
internal static class ApexLightingTests
{
    internal static void Run()
    {
        var checks = 0; var cases = new List<string>();
        void Check(bool value, string name) { if (!value) throw new Exception("APEX: " + name); checks++; }
        void Case(string name, Action body) { body(); cases.Add(name); }
        void Reject<T>(Action body, string name) where T : Exception
        { try { body(); } catch (T) { checks++; return; } throw new Exception("APEX expected " + name); }
        var path = @"\\?\hid#vid_1a2c&pid_b001#memory-only";
        var identity = ApexLightingConsumer.OwnerIdentity(path, 7);
        var solid = ApexLightingRequest.Create("color", "solid", 10, 20, 30, 60);
        var noWait = new Action<int, CancellationToken>((ms, token) => { Check(ms == 300, "source300ms brightness-color spacing"); token.ThrowIfCancellationRequested(); });

        Case("exact-owner-model-and-endpoint", () =>
        {
            Check(ApexLightingConsumer.MatchesOwnerIdentity(identity), "exact APEX owner");
            foreach (var wrong in new[] { identity with { ModelClass = "HandheldCompanion.Devices.OneXPlayer3" },
                identity with { ModelClass = "HandheldCompanion.Devices.OneXPlayerX2MiniPro" },
                identity with { ModelClass = "HandheldCompanion.Devices.OneXPlayerApex " },
                identity with { VendorId = 0x1A86 }, identity with { ProductId = 0xFE00 },
                identity with { UsagePage = 0xFF00 }, identity with { Usage = 2 },
                identity with { OutputReportLength = 64 }, identity with { Generation = 0 } })
            {
                var opens = 0;
                Reject<ArgumentException>(() => ApexLightingConsumer.Apply(wrong, solid, () => true, default,
                    () => { opens++; return new Fake(path); }, noWait), "wrong identity");
                Check(opens == 0, "wrong identity0 opens");
            }
        });
        Case("descriptor-rejects-intel-and-wrong-framing", () =>
        {
            Check(ApexWindowsHidTransport.MatchesDescriptor(0x1A2C, 0xB001, 0xFF01, 1, 65), "exact APEX descriptor");
            foreach (var length in new ushort[] { 0, 7, 64, 66 })
                Check(!ApexWindowsHidTransport.MatchesDescriptor(0x1A2C, 0xB001, 0xFF01, 1, length), "reject short/long report");
            Check(!ApexWindowsHidTransport.MatchesDescriptor(0x1A86, 0xFE00, 0xFF00, 1, 65), "OXP3 collection rejected");
            Check(!ApexWindowsHidTransport.MatchesDescriptor(0x1A2C, 0xB001, 0xFF00, 1, 65), "wrong usage page rejected");
        });
        Case("full-windows-report-and-rgb-boundaries", () =>
        {
            foreach (var rgb in new[] { (0, 0, 0), (255, 255, 255), (255, 0, 1), (10, 20, 30), (1, 128, 254) })
            {
                var report = ApexLightingConsumer.BuildSolid((byte)rgb.Item1, (byte)rgb.Item2, (byte)rgb.Item3);
                Check(report.Length == 65 && report.Take(4).SequenceEqual(new byte[] { 0, 7, 255, 254 }) && report[64] == 0, "Windows ID0 + fixed command");
                Check(Enumerable.Range(0, 20).All(i => report[4 + i * 3] == rgb.Item1 && report[5 + i * 3] == rgb.Item2 && report[6 + i * 3] == rgb.Item3), "20 complete RGB triplets");
            }
            var rainbow = ApexLightingConsumer.BuildRainbow();
            Check(rainbow.Take(4).SequenceEqual(new byte[] { 0, 7, 255, 3 }) && rainbow.Skip(4).All(b => b == 0), "flowing03 no preset alias");
        });
        Case("all101-brightness-values-and-off", () =>
        {
            // Explicit source intervals independently encode ties-even rounding
            // at10/30/50/70/90 rather than reproducing the implementation.
            for (var percent = 0; percent <= 100; percent++)
            {
                var report = ApexLightingConsumer.BuildBrightness(percent);
                var expected = percent <= 29 ? 1 : percent <= 69 ? 3 : 4;
                Check(report.Length == 65 && report.Take(4).SequenceEqual(new byte[] { 0, 7, 255, 253 }) && report[5] == 5, "brightness framing");
                Check(report[4] == (percent == 0 ? 0 : 1) && report[6] == expected && report.Skip(7).All(b => b == 0), "brightness discrete level and explicit enable");
            }
            Reject<ArgumentOutOfRangeException>(() => ApexLightingConsumer.BuildBrightness(-1), "negative brightness");
            Reject<ArgumentOutOfRangeException>(() => ApexLightingConsumer.BuildBrightness(101), "excess brightness");
        });
        Case("solid-real-consumer-order-and-release", () =>
        {
            var transport = new Fake(path);
            var result = ApexLightingConsumer.Apply(identity, solid, () => true, default, () => transport, noWait);
            Check(result.Completed && result.Released && result.Writes == 2 && transport.Disposals == 1, "full success closes first");
            Check(transport.Reports[0][3] == 0xFD && transport.Reports[1][3] == 0xFE && transport.Cancels == 0, "brightness before color, no cleanup cancel on success");
        });
        Case("rainbow-no-brightness-does-one-write", () =>
        {
            var transport = new Fake(path);
            var result = ApexLightingConsumer.Apply(identity, ApexLightingRequest.Create("color", "rainbow", null, null, null, null), () => true, default, () => transport, noWait);
            Check(result.Completed && result.Writes == 1 && transport.Reports.Single()[3] == 3, "one source flowing command");
        });
        Case("brightness-only-no-cached-color", () =>
        {
            var transport = new Fake(path);
            var result = ApexLightingConsumer.Apply(identity, ApexLightingRequest.Create("brightness", null, null, null, null, 20), () => true, default, () => transport, noWait);
            Check(result.Completed && result.Writes == 1 && transport.Reports.Single()[3] == 0xFD, "brightness does not invent RGB");
        });
        Case("off-from-any-stale-effect-is-explicit-disable", () =>
        {
            var transport = new Fake(path);
            var request = ApexLightingRequest.Create("off", "breathing", null, null, null, null);
            var result = ApexLightingConsumer.Apply(identity, request, () => true, default, () => transport, noWait);
            Check(result.Completed && result.Writes == 1 && transport.Reports.Single()[4] == 0, "off explicit enabled0");
        });
        Case("short-and-failed-first-write-never-replay", () =>
        {
            foreach (var length in new[] { 0, 7, 64 })
            {
                var transport = new Fake(path) { ReturnLength = length };
                var result = ApexLightingConsumer.Apply(identity, solid, () => true, default, () => transport, noWait);
                Check(!result.Completed && result.Released && result.Writes == 1 && transport.Reports.Count == 1 && transport.Disposals == 1, "short write abort and release");
            }
            var failed = new Fake(path) { Failure = new IOException("injected transport failure") };
            var failure = ApexLightingConsumer.Apply(identity, solid, () => true, default, () => failed, noWait);
            Check(!failure.Completed && failure.Writes == 1 && failed.Reports.Count == 1, "exception has no retry");
        });
        Case("second-write-failure-retains-partial-result", () =>
        {
            var transport = new Fake(path) { FailAt = 2 };
            var result = ApexLightingConsumer.Apply(identity, solid, () => true, default, () => transport, noWait);
            Check(!result.Completed && result.Writes == 2 && result.Released && transport.Reports.Count == 2, "no success after prior valid brightness");
        });
        Case("retired-owner-and-cancel-before-open", () =>
        {
            var opens = 0;
            var result = ApexLightingConsumer.Apply(identity, solid, () => false, default, () => { opens++; return new Fake(path); }, noWait);
            Check(!result.Completed && result.Writes == 0 && opens == 0, "retired owner0 IO");
            using var cancelled = new CancellationTokenSource(); cancelled.Cancel();
            result = ApexLightingConsumer.Apply(identity, solid, () => true, cancelled.Token, () => { opens++; return new Fake(path); }, noWait);
            Check(!result.Completed && opens == 0, "pre-cancel0 IO");
        });
        Case("retirement-after-first-write-prevents-next", () =>
        {
            var current = true;
            var transport = new Fake(path) { AfterWrite = () => current = false };
            var result = ApexLightingConsumer.Apply(identity, solid, () => current, default, () => transport, noWait);
            Check(!result.Completed && result.Writes == 1 && result.Released && transport.Disposals == 1, "late write cannot promote old generation");
        });
        Case("cancel-during-incomplete-write-no-late-success", () =>
        {
            using var stop = new CancellationTokenSource();
            var pending = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
            var issued = new ManualResetEventSlim();
            var transport = new Fake(path) { Pending = pending.Task, Issued = issued };
            var apply = Task.Run(() => ApexLightingConsumer.Apply(identity, solid, () => true, stop.Token, () => transport, noWait));
            Check(issued.Wait(2000), "pending injected write entered");
            stop.Cancel();
            Check(!apply.IsCompleted, "unknown write retains request until completion");
            pending.SetResult(65);
            Check(apply.Wait(2000), "late injected write returned");
            var result = apply.Result;
            Check(!result.Completed && result.Writes == 1 && result.Released, "late successful write cancelled before second");
            issued.Dispose();
        });
        Case("endpoint-change-disconnect-and-open-failure", () =>
        {
            foreach (var transport in new[] { new Fake(path + "-changed"), new Fake(path) { Connected = false } })
            {
                var result = ApexLightingConsumer.Apply(identity, solid, () => true, default, () => transport, noWait);
                Check(!result.Completed && result.Writes == 0 && result.Released, "wrong/disconnected endpoint0 writes");
            }
            var failed = ApexLightingConsumer.Apply(identity, solid, () => true, default, () => throw new IOException("open-failed"), noWait);
            Check(!failed.Completed && failed.Writes == 0, "open failure preserved");
        });
        Case("close-failure-never-released-success", () =>
        {
            var transport = new Fake(path) { DisposeFailure = true };
            var result = ApexLightingConsumer.Apply(identity, solid, () => true, default, () => transport, noWait);
            Check(!result.Completed && !result.Released && result.Reason == "release-failed", "close error stays failed");
        });
        Case("retirement-at-close-prevents-final-success", () =>
        {
            var current = true; var transport = new Fake(path) { AfterDispose = () => current = false };
            var result = ApexLightingConsumer.Apply(identity, solid, () => current, default, () => transport, noWait);
            Check(!result.Completed && result.Released, "final generation admission after release");
        });
        Case("unsupported-requests-rejected-without-open", () =>
        {
            Reject<NotSupportedException>(() => ApexLightingRequest.Create("status", null, null, null, null, null), "no status");
            Reject<NotSupportedException>(() => ApexLightingRequest.Create("preset", null, null, null, null, null), "no preset");
            Reject<NotSupportedException>(() => ApexLightingRequest.Create("color", "breathing", 1, 2, 3, null), "no borrowed V1 breathing");
            Reject<ArgumentException>(() => ApexLightingRequest.Create("color", "solid", null, 2, 3, null), "no cached color");
            Reject<ArgumentException>(() => ApexLightingRequest.Create("brightness", null, null, null, null, null), "no absent brightness");
            Reject<ArgumentOutOfRangeException>(() => ApexLightingRequest.Create("color", "solid", 256, 2, 3, null), "no truncated RGB");
        });
        Case("generated-catalog-consumes-exact-apex-adapter", () =>
        {
            var operation = HcLightingCapabilityCatalog.Require(ApexLightingConsumer.Model, ApexLightingConsumer.Model, "color", "solid", true);
            Check(operation.Capabilities.SourceAdapter == LightingSourceAdapter.ApexHidV2 && operation.PreferredProvider == "onex-apex-hid-v2", "real catalog adapter and provider");
            Check(!operation.Capabilities.SecondColor && !operation.Capabilities.PresetAvailable && !operation.Capabilities.StatusAvailable, "no inherited unavailable features");
            Reject<NotSupportedException>(() => HcLightingCapabilityCatalog.Require(ApexLightingConsumer.Model, ApexLightingConsumer.Model, "preset", presetId: "LEDPreset_OneXPlayerX1_Preset01"), "old serial preset rejected");
            Reject<NotSupportedException>(() => HcLightingCapabilityCatalog.Require(ApexLightingConsumer.Model, ApexLightingConsumer.Model, "status"), "old serial status rejected");
            Reject<InvalidOperationException>(() => HcLightingCapabilityCatalog.Require(ApexLightingConsumer.Model, "HandheldCompanion.Devices.OneXPlayer3", "color", "solid"), "Intel identity cannot alias APEX");
        });
        Case("real-parser-missing-owner-refused-before-terminal", () =>
        {
            var opens = 0;
            Reject<ArgumentException>(() => Program.RunWithSuperJoyPorts(
                ["--expected-device-class", ApexLightingConsumer.Model, "--action", "color", "--level", "solid",
                 "--r", "10", "--g", "20", "--b", "30", "--brightness", "60"],
                (_, _, _) => throw new Exception("Unexpected AYANEO HID"),
                (_, _) => throw new Exception("Unexpected EC"),
                (_, _, _) => { opens++; return new Fake(path); }), "missing real native owner");
            Check(opens == 0, "real parser0 terminal opens without owner");
            Reject<InvalidOperationException>(() => HcLightingCapabilityCatalog.Require(ApexLightingConsumer.Model,
                "HandheldCompanion.Devices.OneXPlayerX1", "color", "solid"), "old X1 identity cannot authorize APEX v2");
        });
        Case("unsupported-secondary-and-speed-refused-before-owner", () =>
        {
            Program.ValidateApexOptionalArguments("color", 10, 20, 30, null, null, null, null);
            Program.ValidateApexOptionalArguments("color", 10, 20, 30, 10, 20, 30, 50);
            checks += 2;
            Reject<NotSupportedException>(() => Program.ValidateApexOptionalArguments("color", 10, 20, 30, 99, 20, 30, null), "different second color");
            Reject<NotSupportedException>(() => Program.ValidateApexOptionalArguments("color", 10, 20, 30, 10, null, null, null), "partial second color");
            Reject<NotSupportedException>(() => Program.ValidateApexOptionalArguments("color", 10, 20, 30, null, null, null, 75), "custom fixed speed");
            var opens = 0;
            Reject<NotSupportedException>(() => Program.RunWithSuperJoyPorts(
                ["--expected-device-class", ApexLightingConsumer.Model, "--action", "color", "--level", "solid",
                 "--r", "10", "--g", "20", "--b", "30", "--r2", "99", "--g2", "20", "--b2", "30"],
                (_, _, _) => throw new Exception("Unexpected AYANEO HID"), (_, _) => throw new Exception("Unexpected EC"),
                (_, _, _) => { opens++; return new Fake(path); }), "real parser incompatible second color");
            Check(opens == 0, "incompatible request0 terminal opens");
        });
        Case("retirement-during-source-delay-prevents-color", () =>
        {
            var current = true; var transport = new Fake(path); var waits = 0;
            var result = ApexLightingConsumer.Apply(identity, solid, () => current, default, () => transport,
                (milliseconds, _) => { Check(milliseconds == 300, "full source transition delay"); waits++; current = false; });
            Check(!result.Completed && result.Writes == 1 && result.Released && waits == 1, "revocation after delay no second write");
        });
        Console.WriteLine(JsonSerializer.Serialize(new { ok = true, scope = "apex-hid-v2", cases, checks,
            productionConsumer = true, generatedCatalog = true, hardwareWrites = false, defaultSuiteRun = false,
            physicalEffectVerified = false, downloadedSourceExecuted = false }));
    }
    private sealed class Fake(string path) : IApexLightingTransport
    {
        internal List<byte[]> Reports = [];
        internal int ReturnLength = 65, FailAt = 0, Disposals, Cancels;
        internal bool Connected = true, DisposeFailure;
        internal Exception? Failure;
        internal Action? AfterWrite, AfterDispose;
        internal Task<int>? Pending;
        internal ManualResetEventSlim? Issued;
        public string DevicePath => path;
        public bool IsConnected => Connected;
        public ValueTask<int> WriteAsync(ReadOnlyMemory<byte> report, CancellationToken cancellationToken)
        {
            Reports.Add(report.ToArray()); Issued?.Set(); AfterWrite?.Invoke();
            if (Failure is not null) return ValueTask.FromException<int>(Failure);
            if (FailAt == Reports.Count) return ValueTask.FromException<int>(new IOException("injected write failure"));
            return Pending is null ? ValueTask.FromResult(ReturnLength) : new(Pending);
        }
        public void CancelPendingIo() { Cancels++; }
        public void Dispose() { Disposals++; if (DisposeFailure) throw new IOException("injected close failure"); Connected = false; AfterDispose?.Invoke(); }
    }
}
