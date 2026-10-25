using System.Reflection;
using System.IO;
using YeManFanHost;
using System.Text.Json;
using YeManFanBackend.Asus;
using YeManFanBackend.Capabilities;

internal static class FanCapabilityRoundtrip
{
    private static readonly List<string> Checks = new();
    private static readonly List<object> Trace = new();
    private const string Rog = "HandheldCompanion.Devices.ROGAlly";
    private static readonly double[] Original = { 0, 0, 10, 20, 30, 40, 50, 60, 70, 80, 100 };
    private static void Require(bool value, string name)
    {
        if (!value) throw new InvalidOperationException(name);
        Checks.Add(name);
    }
    private static void Reject(Action action, string name)
    {
        var rejected = false;
        try { action(); } catch (InvalidOperationException) { rejected = true; }
        Require(rejected, name);
    }
    private static void Reset()
    {
        AsusACPI.Close();
        AsusACPI.SetTransportForInjection(null);
        AsusACPI.DiagnosticSink = null;
        AsusAcpiObservations.ClearReceipts();
    }
    private static object Profile(double[] points, string mode) =>
        new RealHcBackend.SelfTestHcProfile(new RealHcBackend.SelfTestHcFanProfile
        { fanMode = mode, fanSpeeds = (double[])points.Clone() });
    private sealed class Device
    {
        public bool UseOpenLib => true;
        public bool IsOpen => true;
        public bool IsSupported => true;
    }
    private static AsusFanCapabilitySession SessionOf(RealHcBackend host) =>
        (AsusFanCapabilitySession)(typeof(RealHcBackend)
            .GetField("backendFanCapability", BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(host)
            ?? throw new InvalidOperationException("Host did not acquire its real catalog lease"));

    private static void RunRealHostConsumer()
    {
        var transport = new RecordingTransport();
        AsusACPI.SetTransportForInjection(transport);
        var host = new RealHcBackend(HostOptions.Parse(new[] { "--fan-provider", "backend" }));
        host.Diagnostic = (name, data) =>
        {
            if (name == "fan.capability.result") Trace.Add(new { stage = "production-host-result", data });
        };
        host.InstallSelfTestRouteSessionForSelfTest(new Device(),
            RealHcBackend.ResolveRouteForSelfTest(Rog)!, true);
        try
        {
            using var scope = AsusAcpiObservations.BeginScope("pilot-host-consumer");
            host.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            var session = SessionOf(host);
            Require(session.Descriptor.DeviceId == "asus.rog-ally", "host-resolves-stable-device-id");
            Require(session.Descriptor.CapabilityId == "hc.device.ROGAlly", "host-descriptor-resolves-unified-catalog-id");
            Require(session.LastResult?.CapabilityId == "hc.device.ROGAlly", "actual-write-receipt-carries-unified-catalog-id");
            Require(session.LastResult?.Outcome == FanCapabilityTransportOutcome.CompleteTransport,
                "host-production-dispatch-records-transport-result-in-catalog-session");
            Require(scope.PeekReceipts().Length == 3, "host-outer-evidence-scope-retains-all-three-receipts");
            Reject(() => FanCapabilityCatalog.AcquireAsusCurve(Rog), "second-independent-owner-rejected");
            var calls = 0;
            Reject(() => FanCapabilityCatalog.InvokeHcAfterRelease(session, () => calls++, () => true),
                "hc-callback-blocked-while-ymcc-owns-handle");
            Require(calls == 0, "blocked-handoff-does-not-call-hc");
            Require(transport.Packets.Count == 3 && transport.Packets[0][23] == 40,
                "production-host-original-node-reaches-real-acpi-packet");
            var edited = (double[])Original.Clone(); edited[5] = 47;
            host.ApplyPowerProfileForSelfTest(Profile(edited, "Software"));
            Require(transport.Packets.Count == 6 && transport.Packets[3][23] == 47,
                "production-host-edited-one-node-reaches-real-acpi-packet");
            Require(session.LastResult?.Sequence == 2, "catalog-result-sequence-increments");
            host.ApplyPowerProfileForSelfTest(Profile(Array.Empty<double>(), "Hardware"));
            Require(session.LastResult?.Operation == "restore-default-curves", "host-hardware-path-restores-defaults-through-catalog");
            host.Dispose();
            Require(session.HandleReleased && session.CanHandBackToHc && !AsusACPI.IsOpen,
                "host-dispose-returns-real-handle-and-catalog-lease");
            Require(transport.OpenCount == 1 && transport.CloseCount == 1, "host-handle-open-close-balanced");
            var callbackCount = 0;
            FanCapabilityCatalog.InvokeHcAfterRelease(session, () => callbackCount++, () => true);
            Require(callbackCount == 1 && session.HcCallbackReturned, "explicit-catalog-handoff-callback-executes-once");
            Reject(() => FanCapabilityCatalog.InvokeHcAfterRelease(session, () => callbackCount++, () => true),
                "old-release-proof-cannot-be-reused");
            Reject(() => session.ApplySoftwareCurve(edited), "released-ymcc-session-cannot-write");
        }
        finally { host.Dispose(); Reset(); }
    }

    private static void RunTwoHosts()
    {
        var transport = new RecordingTransport(); AsusACPI.SetTransportForInjection(transport);
        var a = new RealHcBackend(HostOptions.Parse(new[] { "--fan-provider", "backend" }));
        var b = new RealHcBackend(HostOptions.Parse(new[] { "--fan-provider", "backend" }));
        var route = RealHcBackend.ResolveRouteForSelfTest(Rog)!;
        a.InstallSelfTestRouteSessionForSelfTest(new Device(), route, true);
        b.InstallSelfTestRouteSessionForSelfTest(new Device(), route, true);
        try
        {
            a.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            Reject(() => b.ApplyPowerProfileForSelfTest(Profile(Original, "Software")), "second-host-cannot-acquire-first-host-lease");
            b.Dispose();
            Require(AsusACPI.IsOpen && transport.CloseCount == 0, "rejected-host-dispose-cannot-close-active-owner-handle");
            a.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            Require(transport.Packets.Count == 6, "first-host-continues-writing-after-rejected-host-dispose");
            a.Dispose();
            Require(transport.CloseCount == 1 && !AsusACPI.IsOpen, "only-owning-host-closes-handle-once");
        }
        finally { a.Dispose(); b.Dispose(); Reset(); }
    }

    private static void RunStaleProofCase()
    {
        var transport = new RecordingTransport(); AsusACPI.SetTransportForInjection(transport);
        var old = FanCapabilityCatalog.AcquireAsusCurve(Rog);
        try
        {
            AsusACPI.Open(); old.RestoreDefaultCurves(); AsusACPI.Close(); old.ObserveHostHandleClosed();
            var newer = FanCapabilityCatalog.AcquireAsusCurve(Rog);
            AsusACPI.Open(); newer.ApplySoftwareCurve(Original); AsusACPI.Close(); newer.ObserveHostHandleClosed();
            Reject(() => FanCapabilityCatalog.InvokeHcAfterRelease(old,
                () => throw new Exception("stale callback invoked"), () => true), "intervening-session-invalidates-old-release-proof");
        }
        finally { AsusACPI.Close(); old.ObserveHostHandleClosed(); Reset(); }
    }

    private static void RunFailureCases()
    {
        foreach (var mode in new[] { "partial", "unknown", "short", "closed" })
        {
            var transport = new RecordingTransport { Mode = mode };
            AsusACPI.SetTransportForInjection(transport);
            var session = FanCapabilityCatalog.AcquireAsusCurve(Rog);
            try
            {
                if (mode != "closed") AsusACPI.Open();
                var result = session.RestoreDefaultCurves();
                var expected = mode switch
                {
                    "partial" => FanCapabilityTransportOutcome.Partial,
                    "unknown" => FanCapabilityTransportOutcome.Unknown,
                    "short" => FanCapabilityTransportOutcome.ShortTransport,
                    _ => FanCapabilityTransportOutcome.Failed,
                };
                Require(result.Outcome == expected, $"{mode}-has-truthful-catalog-outcome");
                Require(result.PhysicalObservation == "not-observed", $"{mode}-no-physical-claim");
                AsusACPI.Close(); session.ObserveHostHandleClosed();
                Reject(() => FanCapabilityCatalog.InvokeHcAfterRelease(session,
                    () => throw new Exception("unsafe callback invoked"), () => true), $"{mode}-cannot-auto-fallback-to-hc");
                Trace.Add(new { stage = "failure", mode, result, handleReleased = session.HandleReleased });
            }
            finally { AsusACPI.Close(); session.ObserveHostHandleClosed(); Reset(); }
        }
    }

#if HC_BINARY_ADAPTER_READY
    private static void RunHcBinaryRoundtrip(string hcDll)
    {
        using var hc = new HcBinaryAdapter(hcDll);
        var baseline = hc.ApplySoftware(Original);
        Require(baseline.Count == 3, "actual-hc-dll-initial-software-callback-three-writes");
        Require(hc.DefaultCpuCurve.SequenceEqual(AsusFanCurveWriter.DefaultCpuCurve),
            "actual-hc-dll-default-cpu-curve-matches-ymcc-restore-table");
        Require(hc.DefaultGpuCurve.SequenceEqual(AsusFanCurveWriter.DefaultGpuCurve),
            "actual-hc-dll-default-gpu-curve-matches-ymcc-restore-table");
        var transport = new RecordingTransport(); AsusACPI.SetTransportForInjection(transport);
        var session = FanCapabilityCatalog.AcquireAsusCurve(Rog);
        try
        {
            AsusACPI.Open(); session.ApplySoftwareCurve(Original);
            Require(baseline.Zip(transport.Packets.Take(3)).All(pair => pair.First.SequenceEqual(pair.Second)),
                "actual-hc-dll-and-ymcc-production-packets-byte-identical");
            var edited = (double[])Original.Clone(); edited[5] = 47;
            session.ApplySoftwareCurve(edited);
            var ymccEdited = transport.Packets.Skip(3).Take(3).Select(x => (byte[])x.Clone()).ToArray();
            session.RestoreDefaultCurves(); AsusACPI.Close(); session.ObserveHostHandleClosed();
            IReadOnlyList<byte[]>? returned = null;
            FanCapabilityCatalog.InvokeHcAfterRelease(session, () => returned = hc.ApplySoftware(edited), () => hc.HandleReleased);
            Require(returned?.Count == 3 && returned.Zip(ymccEdited).All(pair => pair.First.SequenceEqual(pair.Second)),
                "catalog-release-to-actual-hc-dll-callback-and-edited-node-packets-match");
            Require(session.HcCallbackReturned, "catalog-records-actual-hc-callback-return");
            Trace.Add(new { stage = "hc-binary-roundtrip", hc = hcDll, hcSha256 = hc.Sha256,
                hcVersion = hc.AssemblyVersion, baselinePackets = baseline.Select(Convert.ToHexString).ToArray(),
                editedPackets = ymccEdited.Select(Convert.ToHexString).ToArray(),
                restored = session.LastResult, actualHcCallbackReturned = session.HcCallbackReturned,
                physicalObservation = "not-observed", transport = "injected-recording-only" });
        }
        finally { AsusACPI.Close(); session.ObserveHostHandleClosed(); Reset(); }
    }
#endif
    private static void RunUnknownHcCloseLast(bool probeThrows)
    {
        var transport = new RecordingTransport(); AsusACPI.SetTransportForInjection(transport);
        var session = FanCapabilityCatalog.AcquireAsusCurve(Rog);
        try
        {
            AsusACPI.Open(); session.RestoreDefaultCurves(); AsusACPI.Close(); session.ObserveHostHandleClosed();
            if (probeThrows)
            {
                AggregateException? both = null;
                try { FanCapabilityCatalog.InvokeHcAfterRelease(session,
                    () => throw new IOException("callback failure"), () => throw new ApplicationException("close probe failure")); }
                catch (AggregateException ex) { both = ex; }
                Require(both?.InnerExceptions.Count == 2 && both.InnerExceptions[0] is IOException &&
                    both.InnerExceptions[1] is ApplicationException, "callback-and-close-probe-errors-both-preserved");
            }
            else
            {
                Reject(() => FanCapabilityCatalog.InvokeHcAfterRelease(session, () => { }, () => false),
                    "returned-hc-callback-with-unknown-close-cannot-release-writer");
            }
            Reject(() => FanCapabilityCatalog.AcquireAsusCurve(Rog),
                "unclosed-hc-callback-blocks-new-ymcc-owner");
        }
        finally { Reset(); }
    }
    public static int Main(string[] args)
    {
        try
        {
            Checks.AddRange(FanRouteEvidenceSelfTest.RunFanBackendProviderSelfTest());
            var backendReferences = typeof(FanCapabilityCatalog).Assembly.GetReferencedAssemblies();
            Require(!backendReferences.Any(x => x.Name!.Contains("HandheldCompanion", StringComparison.OrdinalIgnoreCase)),
                "compiled-capability-backend-has-no-hc-assembly-reference");
            Require(!backendReferences.Any(x => x.Name is "PresentationFramework" or "PresentationCore" or "WindowsBase" or "Microsoft.Windows.SDK.NET"),
                "compiled-capability-backend-has-no-wpf-or-windows-sdk-reference");
            RunRealHostConsumer();
            RunFailureCases();
            RunStaleProofCase();
            RunTwoHosts();
            var binaryStatus = "pending-adapter";
#if HC_BINARY_ADAPTER_READY
            RunHcBinaryRoundtrip(args[1]); binaryStatus = "passed-actual-locked-dll";
#endif
            RunUnknownHcCloseLast(args.Length > 2 && args[2] == "probe-throws");
            var result = new { status = "passed", passed = true, checks = Checks.Count, checkNames = Checks,
                binaryStatus, trace = Trace, hardwareAccess = "none; transport injected", package = "no handheld package" };
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[0]))!);
            File.WriteAllText(args[0], JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine(JsonSerializer.Serialize(new { result.status, result.checks, binaryStatus }));
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}

internal sealed class RecordingTransport : IAsusAcpiTransport
{
    private static readonly IntPtr FakeHandle = new(937);
    public List<byte[]> Packets { get; } = new();
    public int OpenCount { get; private set; }
    public int CloseCount { get; private set; }
    public string Mode { get; init; } = "complete";
    public bool IsHandleValid(IntPtr handle) => handle == FakeHandle;
    public bool TryOpen(out IntPtr handle, out int error) { OpenCount++; handle = FakeHandle; error = 0; return true; }
    public bool Control(IntPtr handle, uint code, byte[] input, byte[] output, out uint returned, out int error)
    {
        if (!IsHandleValid(handle)) throw new InvalidOperationException("unexpected handle");
        Packets.Add((byte[])input.Clone()); Array.Clear(output);
        if (Mode == "unknown") throw new IOException("injected-write-unknown");
        returned = Mode == "short" ? 8u : (uint)output.Length;
        error = Mode == "partial" && Packets.Count % 3 != 1 ? 31 : 0;
        return error == 0;
    }
    public void Close(IntPtr handle) { if (!IsHandleValid(handle)) throw new InvalidOperationException("unexpected close handle"); CloseCount++; }
}
