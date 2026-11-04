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
        var portLibrary=hc.ValidatePinnedSourcePortMetadata();
        Trace.Add(new {name="actual-source-port-metadata",portLibrary,loadedImageSha256=hc.LoadedImageSha256,portConstructorsInvoked=false});
        Require(portLibrary.StartsWith("LibreHardwareMonitorLib, Version=",StringComparison.Ordinal),
            "actual-pinned-port-metadata-binds-production-source-transport-without-port-construction");
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
            var a1 = hc.ApplyA1Terminal(new[] { 0d, 25d, 50d, 75d, 100d });
            Require(a1.BaseIsA1, "a1-derived-terminal-base-is-pinned-aokzoea1");
            Require(!a1.ConstructorExecuted && a1.ConstructorNote.Contains("GamepadMotion", StringComparison.Ordinal),
                "a1-derived-terminal-constructor-not-run-native-motion-blocked");
            Require(a1.UseOpenLib && a1.AcpiMode == 0x4A && a1.AcpiDuty == 0x4B,
                "a1-derived-terminal-pinned-open-flag-and-addresses");
            Require(a1.WritesHex.SequenceEqual(new[] { "4A:01", "4A:00", "4B:00", "4B:3F", "4B:7F", "4B:BF", "4B:FF" }),
                "a1-derived-terminal-manual-auto-and-duty-scaling");
            Require(a1.WriteCountBeforeGuard == 7 && a1.WriteCountAfterClosedGuard == 7,
                "a1-derived-terminal-closed-guard-refuses-write");
            Require(!a1.NativeImportAttempted, "a1-derived-terminal-no-native-import");
            Require(a1.StaticOpenLibNull, "a1-derived-terminal-openlibsys-never-created");
            Trace.Add(new
            {
                stage = "a1-derived-terminal",
                receipt = a1,
                limitedToLowerLayer = true,
                hostRouteAdmissionNotUsed = true,
                physicalObservation = "not-observed",
            });
            Trace.Add(RunA1ExactHostBoundary(hc));
            Trace.Add(new { stage = "hc-binary-roundtrip", hc = hcDll, hcSha256 = hc.Sha256,
                hcVersion = hc.AssemblyVersion, baselinePackets = baseline.Select(Convert.ToHexString).ToArray(),
                editedPackets = ymccEdited.Select(Convert.ToHexString).ToArray(),
                restored = session.LastResult, actualHcCallbackReturned = session.HcCallbackReturned,
                physicalObservation = "not-observed", transport = "injected-recording-only" });
        }
        finally { AsusACPI.Close(); session.ObserveHostHandleClosed(); Reset(); }
    }

    private static string? HostBoundaryCode(Action action)
    {
        try { action(); return null; }
        catch (FanApiException ex) { return ex.Code; }
    }

    /**
     * C04: exercise the production Host guards for the exact pinned A1 runtime type without
     * routeOverrideForSelfTest, FullName spoofing, forced open/admission or a substitute
     * decision model. The instance is constructor-skipped and kept closed, and every selected
     * negative path throws before any device/EC/native call.
     */
    private static object RunA1ExactHostBoundary(HcBinaryAdapter hc)
    {
        object exact = hc.CreateA1ExactDevice(out var deviceReceipt);
        using var host = new RealHcBackend(HostOptions.Parse(new[] { "--fan-provider", "backend" }));
        // Existing Host self-test hook: replace only the device reference (production never calls it).
        host.SwapDeviceForSelfTest(exact);
        Require(host.FactoryType == "HandheldCompanion.Devices.AOKZOEA1",
            "a1-exact-host-factory-type-is-pinned-runtime-type");
        Require(!deviceReceipt.IsOpen && !deviceReceipt.DeviceOpen,
            "a1-exact-device-initialized-closed");
        var route = host.Route ?? throw new InvalidOperationException("A1 exact Host route did not resolve");
        Require(route.Kind == FanRouteKind.GenericDuty
                && route.RestoreStrategy == FanRestoreStrategy.OneXAcpiFanMode
                && route.Precondition == FanRoutePrecondition.OpenLibHandleOpen,
            "a1-exact-host-route-generic-duty-onex-openlib-precondition");
        Require(route.HcCallbackReady, "a1-exact-host-route-hc-callback-contract");
        Require(!host.CanWrite, "a1-exact-host-canwrite-false-without-authorization");
        var writeCode = HostBoundaryCode(() => host.ApplyCurve(
            new List<FanNode> { new(0d, 0d), new(50d, 30d), new(100d, 100d) }));
        Require(writeCode == "REAL_WRITE_AUTH_REQUIRED",
            "a1-exact-host-write-refused-by-production-authorization-gate");
        var returnCode = HostBoundaryCode(() => host.RestoreOem());
        Require(returnCode == "HOST_NOT_OPEN",
            "a1-exact-host-oem-return-refused-with-no-owned-session");
        host.VerifyActiveCurveSession();
        Require(!host.HcOpenEventsInvoked && !host.HcDeviceManagerStopInvoked,
            "a1-exact-host-no-open-events-or-manager-stop");
        Require(!host.CanWrite, "a1-exact-host-still-cannot-write-after-refusals");
        var post = hc.ReadA1ExactPostState(exact);
        Require(!post.IsOpen && !post.DeviceOpen, "a1-exact-device-remained-closed");
        Require(post.StaticOpenLibNull, "a1-exact-device-openlibsys-static-never-created");
        return new
        {
            stage = "a1-exact-host-boundaries",
            receipt = deviceReceipt,
            hostBoundary = new
            {
                factoryType = host.FactoryType,
                routeKind = route.Kind.ToString(),
                restoreStrategy = route.RestoreStrategy.ToString(),
                precondition = route.Precondition.ToString(),
                hcCallbackReady = route.HcCallbackReady,
                canWrite = host.CanWrite,
                writeRefusalCode = writeCode,
                oemReturnRefusalCode = returnCode,
                hcOpenEventsInvoked = host.HcOpenEventsInvoked,
                hcDeviceManagerStopInvoked = host.HcDeviceManagerStopInvoked,
                postState = post,
            },
            limitedToHostNegativeGuards = true,
            hostPositiveAction = "NOT_RUN",
            physicalObservation = "not-observed",
        };
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
    private static void RunDefaultHcRogallyBoundary()
    {
        var route = RealHcBackend.ResolveRouteForSelfTest(Rog)!;

        // No-prior hardware must skip the actual default-hc callback.
        var boundary = new RealHcBackend.SelfTestHcBoundary();
        var host = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var device = new HandheldCompanion.Devices.ROGAlly();
        host.InstallSelfTestRouteSessionForSelfTest(device, route, true);
        host.InstallHcBoundaryForSelfTest(boundary);
        try
        {
            host.ApplyPowerProfileForSelfTest(Profile(Array.Empty<double>(), "Hardware"));
            Require(boundary.CallbackInvocationCount == 0, "default-hc-no-prior-hardware-skipped");
            Require(!host.HcRogSoftwareAppliedForSelfTest && !host.HcRogReturnPendingForSelfTest,
                "default-hc-no-prior-ledger-empty");

            // The actual RestoreOem no-write branch must report its existing
            // release column, never dispatch or manufacture prior ownership.
            host.RestoreOem();
            Require(boundary.CallbackInvocationCount == 0,
                "default-hc-no-prior-restore-no-callback");
            Require(host.OemReleaseActionKind == FanHostEngine.ReleaseActionKindName(OemReleaseAction.NotApplicable),
                "default-hc-no-prior-restore-action-not-applicable");
            Require(host.OemRestoreEvidence == "not-applicable-no-write-history" &&
                host.OemOwnershipStatus == "not-applicable-no-software-ownership",
                "default-hc-no-prior-restore-no-physical-ownership-claim");
            Require(!host.HcRogSoftwareAppliedForSelfTest && !host.HcRogReturnPendingForSelfTest,
                "default-hc-no-prior-restore-ledger-empty");

            // Software is dispatched and records host ownership.
            host.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            Require(boundary.CallbackInvocationCount == 1 && host.HcRogSoftwareAppliedForSelfTest,
                "default-hc-software-dispatched-and-ledger-applied");
            Require(!host.HcRogReturnPendingForSelfTest, "default-hc-software-pending-clear");

            // First hardware return is dispatched and leaves a pending obligation.
            host.ApplyPowerProfileForSelfTest(Profile(Array.Empty<double>(), "Hardware"));
            Require(boundary.CallbackInvocationCount == 2 && host.HcRogReturnPendingForSelfTest,
                "default-hc-first-return-pending");
            Require(host.HcRogSoftwareAppliedForSelfTest, "default-hc-first-return-keeps-applied");

            // Existing partial=>Proven contract may satisfy final Confirm and clear both bits.
            host.AsusPrimaryReleaseVerdictForSelfTest =
                new RealHcBackend.AsusWriteVerdict("partial", "injected-partial-final", false, false, false, 3);
            host.RestoreOem();
            Require(boundary.CallbackInvocationCount == 3, "default-hc-restore-dispatches-one-return");
            Require(!host.HcRogSoftwareAppliedForSelfTest && !host.HcRogReturnPendingForSelfTest,
                "default-hc-partial-final-confirm-clears-ledger");

            // Duplicate after successful return must not dispatch again.
            host.ApplyPowerProfileForSelfTest(Profile(Array.Empty<double>(), "Hardware"));
            Require(boundary.CallbackInvocationCount == 3, "default-hc-duplicate-return-skipped");
        }
        finally { host.Dispose(); Reset(); }

        // Exception and closed-device callbacks must not claim ownership.
        var exceptionBoundary = new RealHcBackend.SelfTestHcBoundary();
        var exceptionHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var exceptionDevice = new HandheldCompanion.Devices.ROGAlly();
        exceptionHost.InstallSelfTestRouteSessionForSelfTest(exceptionDevice, route, true);
        exceptionHost.InstallHcBoundaryForSelfTest(exceptionBoundary);
        try
        {
            exceptionBoundary.CallbackFailure = new IOException("injected-default-hc-software-failure");
            try { exceptionHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software")); }
            catch (IOException) { }
            Require(!exceptionHost.HcRogSoftwareAppliedForSelfTest, "default-hc-software-exception-no-claim");

            exceptionBoundary.CallbackFailure = null;
            exceptionDevice.IsOpen = false;
            exceptionHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            Require(!exceptionHost.HcRogSoftwareAppliedForSelfTest, "default-hc-closed-software-no-claim");

            exceptionDevice.IsOpen = true;
            exceptionHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            Require(exceptionHost.HcRogSoftwareAppliedForSelfTest, "default-hc-reopen-software-ledger");
        }
        finally { exceptionHost.Dispose(); Reset(); }

        // Existing duties recover a conservative return obligation and clear only
        // through the existing successful final Confirm.
        var dutyBoundary = new RealHcBackend.SelfTestHcBoundary();
        var dutyHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var dutyDevice = new HandheldCompanion.Devices.ROGAlly();
        dutyHost.InstallSelfTestRouteSessionForSelfTest(dutyDevice, route, true);
        dutyHost.InstallHcBoundaryForSelfTest(dutyBoundary);
        try
        {
            dutyHost.SetActiveCurveDutiesForSelfTest(Original);
            dutyHost.AsusPrimaryReleaseVerdictForSelfTest =
                new RealHcBackend.AsusWriteVerdict("transported", "injected-duty-recovery", false, false, false, 3);
            dutyHost.RestoreOem();
            Require(dutyBoundary.CallbackInvocationCount == 1, "default-hc-duty-recovery-invokes-callback");
            Require(!dutyHost.HcRogSoftwareAppliedForSelfTest && !dutyHost.HcRogReturnPendingForSelfTest,
                "default-hc-duty-recovery-final-confirm-clears");
        }
        finally { dutyHost.Dispose(); Reset(); }

        // ExplicitFailure through the Host RestoreOem path retains both bits and
        // a later qualifying retry clears them.
        var failBoundary = new RealHcBackend.SelfTestHcBoundary();
        var failHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var failDevice = new HandheldCompanion.Devices.ROGAlly();
        failHost.InstallSelfTestRouteSessionForSelfTest(failDevice, route, true);
        failHost.InstallHcBoundaryForSelfTest(failBoundary);
        try
        {
            failHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            failHost.AsusPrimaryReleaseVerdictForSelfTest =
                new RealHcBackend.AsusWriteVerdict("explicit-failure", "injected-explicit-failure", true, false, true, 0);
            var explicitFailed = false;
            try { failHost.RestoreOem(); }
            catch (FanApiException ex) { explicitFailed = ex.Code == "OEM_RESTORE_FAILED"; }
            Require(explicitFailed, "default-hc-explicit-failure-host-path-throws");
            Require(failHost.HcRogSoftwareAppliedForSelfTest && failHost.HcRogReturnPendingForSelfTest,
                "default-hc-explicit-failure-retains-obligation");

            failHost.AsusPrimaryReleaseVerdictForSelfTest =
                new RealHcBackend.AsusWriteVerdict("partial", "injected-retry-success", false, false, false, 3);
            failHost.RestoreOem();
            Require(!failHost.HcRogSoftwareAppliedForSelfTest && !failHost.HcRogReturnPendingForSelfTest,
                "default-hc-explicit-failure-retry-clears");
        }
        finally { failHost.Dispose(); Reset(); }

        // Unknown primary with the existing fallback skipped reaches final
        // failure/retention and remains retryable.
        var unknownBoundary = new RealHcBackend.SelfTestHcBoundary();
        var unknownHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var unknownDevice = new HandheldCompanion.Devices.ROGAlly();
        unknownHost.InstallSelfTestRouteSessionForSelfTest(unknownDevice, route, true);
        unknownHost.InstallHcBoundaryForSelfTest(unknownBoundary);
        try
        {
            unknownHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            unknownHost.AsusPrimaryReleaseVerdictForSelfTest =
                new RealHcBackend.AsusWriteVerdict("no-receipts", "injected-unknown-primary", false, true, true, 0);
            var unknownFailed = false;
            try { unknownHost.RestoreOem(); }
            catch (InvalidOperationException) { unknownFailed = true; }
            Require(unknownFailed, "default-hc-unknown-skipped-fallback-final-confirm-fails");
            Require(unknownHost.HcRogSoftwareAppliedForSelfTest && unknownHost.HcRogReturnPendingForSelfTest,
                "default-hc-unknown-failure-retains-obligation");

            unknownHost.AsusPrimaryReleaseVerdictForSelfTest =
                new RealHcBackend.AsusWriteVerdict("partial", "injected-unknown-retry-success", false, false, false, 3);
            unknownHost.RestoreOem();
            Require(!unknownHost.HcRogSoftwareAppliedForSelfTest && !unknownHost.HcRogReturnPendingForSelfTest,
                "default-hc-unknown-failure-retry-clears");
        }
        finally { unknownHost.Dispose(); Reset(); }

        // Close while an obligation is outstanding must diagnose unresolved
        // return and reset the current session ledger.
        var closeBoundary = new RealHcBackend.SelfTestHcBoundary();
        var closeHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var closeDevice = new HandheldCompanion.Devices.ROGAlly();
        closeHost.InstallSelfTestRouteSessionForSelfTest(closeDevice, route, true);
        closeHost.InstallHcBoundaryForSelfTest(closeBoundary);
        var sawPendingClose = false;
        closeHost.Diagnostic = (name, _) => { if (name == "hc.fan-return.pending-at-close") sawPendingClose = true; };
        try
        {
            closeHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            try { closeHost.Close(); }
            catch (Exception) { }
            Require(sawPendingClose, "default-hc-close-pending-diagnostic");
            Require(!closeHost.HcRogSoftwareAppliedForSelfTest && !closeHost.HcRogReturnPendingForSelfTest,
                "default-hc-close-resets-ledger");
        }
        finally { closeHost.Dispose(); Reset(); }

        // A returned Hardware callback may close the HC device; the later marker
        // is then false and the existing gated fallback can produce final Proven.
        var fallbackBoundary = new RealHcBackend.SelfTestHcBoundary();
        var fallbackHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var fallbackDevice = new HandheldCompanion.Devices.ROGAlly();
        fallbackHost.InstallSelfTestRouteSessionForSelfTest(fallbackDevice, route, true);
        fallbackHost.InstallHcBoundaryForSelfTest(fallbackBoundary);
        var fallbackInvoked = false;
        try
        {
            fallbackHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            fallbackBoundary.AfterApply = () => fallbackDevice.IsOpen = false;
            fallbackHost.AsusPrimaryReleaseVerdictForSelfTest =
                new RealHcBackend.AsusWriteVerdict("no-receipts", "injected-unknown-before-fallback", false, true, true, 0);
            RealHcBackend.AsusDirectRestoreProbeForSelfTest = () =>
            {
                fallbackInvoked = true;
                return (true, true, "proven-by-host-hc-acpi-fallback");
            };
            fallbackHost.RestoreOem();
            Require(fallbackInvoked, "default-hc-later-marker-reaches-existing-fallback");
            Require(!fallbackHost.HcRogSoftwareAppliedForSelfTest && !fallbackHost.HcRogReturnPendingForSelfTest,
                "default-hc-unknown-plus-proven-fallback-final-proven-clears");
        }
        finally
        {
            RealHcBackend.AsusDirectRestoreProbeForSelfTest = null;
            fallbackHost.Dispose();
            Reset();
        }

        // The same later-marker path with a non-proven/skipped probe keeps the
        // final merged Unknown and the pending obligation.
        var skippedBoundary = new RealHcBackend.SelfTestHcBoundary();
        var skippedHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var skippedDevice = new HandheldCompanion.Devices.ROGAlly();
        skippedHost.InstallSelfTestRouteSessionForSelfTest(skippedDevice, route, true);
        skippedHost.InstallHcBoundaryForSelfTest(skippedBoundary);
        try
        {
            skippedHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            skippedBoundary.AfterApply = () => skippedDevice.IsOpen = false;
            skippedHost.AsusPrimaryReleaseVerdictForSelfTest =
                new RealHcBackend.AsusWriteVerdict("no-receipts", "injected-unknown-skipped-fallback", false, true, true, 0);
            RealHcBackend.AsusDirectRestoreProbeForSelfTest =
                () => (false, false, "not-attempted:existing-fallback-not-proven");
            var skippedFailed = false;
            try { skippedHost.RestoreOem(); }
            catch (InvalidOperationException) { skippedFailed = true; }
            Require(skippedFailed, "default-hc-unknown-plus-nonproven-fallback-final-confirm-fails");
            Require(skippedHost.HcRogSoftwareAppliedForSelfTest,
                "default-hc-unknown-plus-nonproven-fallback-retains-applied");
            Require(!skippedHost.HcRogReturnPendingForSelfTest,
                "default-hc-unknown-plus-nonproven-fallback-closed-device-no-pending-claim");
        }
        finally
        {
            RealHcBackend.AsusDirectRestoreProbeForSelfTest = null;
            skippedHost.Dispose();
            Reset();
        }

        // Direct late self-test profile after Close does not claim a ledger state;
        // externalProfileQueue invalidation is the existing rejection boundary.
        var lateBoundary = new RealHcBackend.SelfTestHcBoundary();
        var lateHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var lateDevice = new HandheldCompanion.Devices.ROGAlly();
        lateHost.InstallSelfTestRouteSessionForSelfTest(lateDevice, route, true);
        lateHost.InstallHcBoundaryForSelfTest(lateBoundary);
        try
        {
            lateHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            try { lateHost.Close(); } catch (Exception) { }
            lateDevice.IsOpen = false;
            lateHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            Require(!lateHost.HcRogSoftwareAppliedForSelfTest && !lateHost.HcRogReturnPendingForSelfTest,
                "default-hc-late-direct-profile-no-ledger-claim");
        }
        finally { lateHost.Dispose(); Reset(); }

        // Actual active Host queued ingress: positive current-generation admission,
        // then same owning Host next-generation stale rejection with no callback,
        // no pending snapshot and no ledger mutation.
        var queueHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var queueDevice = new HandheldCompanion.Devices.ROGAlly();
        queueHost.InstallSelfTestRouteSessionForSelfTest(queueDevice,
            RealHcBackend.ResolveRouteForSelfTest(Rog)!, true);
        var queueBoundary = new RealHcBackend.SelfTestHcBoundary();
        queueHost.InstallHcBoundaryForSelfTest(queueBoundary);
        var activeGeneration = queueHost.ActivateExternalProfileSessionForSelfTest();
        RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = true;
        var ignoredReasons = new List<string>();
        queueHost.Diagnostic = (name, data) =>
        {
            if (name != "hc.external-profile-applied-ignored" || data is null) return;
            var reason = data.GetType().GetProperty("reason")?.GetValue(data)?.ToString() ?? string.Empty;
            ignoredReasons.Add(reason);
        };
        try
        {
            queueHost.OnExternalProfileAppliedForSelfTest(
                activeGeneration, Profile(Original, "Software"), "Background");
            Require(queueHost.ExternalProfileQueueHasPendingForSelfTest && ignoredReasons.Count == 0,
                "default-hc-active-current-generation-accepted");

            var ignoredBeforeClose = ignoredReasons.Count;
            var callbackBeforeClose = queueBoundary.CallbackInvocationCount;
            // Close may throw; only the queue-invalidation observation is claimed,
            // not completed Close. Observe it BEFORE any helper Begin/Activate.
            try { queueHost.Close(); } catch (Exception) { }
            queueHost.OnExternalProfileAppliedForSelfTest(
                activeGeneration, Profile(Original, "Software"), "Background");
            Require(ignoredReasons.Count == ignoredBeforeClose + 1 &&
                ignoredReasons[^1] == "session-inactive",
                "default-hc-close-invalidation-observed-before-reactivation");
            Require(!queueHost.ExternalProfileQueueHasPendingForSelfTest,
                "default-hc-close-invalidation-clears-pending");
            Require(!queueHost.HcRogSoftwareAppliedForSelfTest && !queueHost.HcRogReturnPendingForSelfTest,
                "default-hc-close-invalidation-no-ledger-mutation");
            Require(queueBoundary.CallbackInvocationCount == callbackBeforeClose,
                "default-hc-close-invalidation-no-callback");

            var ignoredBeforeReactivated = ignoredReasons.Count;
            var nextGeneration = queueHost.ActivateExternalProfileSessionForSelfTest();
            queueHost.OnExternalProfileAppliedForSelfTest(
                activeGeneration, Profile(Original, "Software"), "Background");
            Require(ignoredReasons.Count == ignoredBeforeReactivated + 1 &&
                ignoredReasons[^1] == "stale-session-generation",
                "default-hc-next-generation-stale-rejected-explicit");
            Require(!queueHost.ExternalProfileQueueHasPendingForSelfTest,
                "default-hc-next-generation-stale-does-not-pend");
            Require(!queueHost.HcRogSoftwareAppliedForSelfTest && !queueHost.HcRogReturnPendingForSelfTest,
                "default-hc-next-generation-stale-no-ledger-mutation");
            Require(queueBoundary.CallbackInvocationCount == callbackBeforeClose,
                "default-hc-next-generation-stale-no-callback");

            queueHost.OnExternalProfileAppliedForSelfTest(
                nextGeneration, Profile(Original, "Software"), "Background");
            Require(queueHost.ExternalProfileQueueHasPendingForSelfTest,
                "default-hc-next-generation-current-accepted");
        }
        finally
        {
            RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = false;
            queueHost.Dispose();
            Reset();
        }
        Checks.AddRange(RealHcBackend.RunExternalProfileQueueSelfTest());

        // Pure source-contract assertions (no receipts or execution inferred).
        Require(FanHostEngine.ClassifyReleaseAction("partial") == OemReleaseAction.Proven,
            "partial-classifies-proven-release-action");
        Require(FanHostEngine.MergeReleaseActions(OemReleaseAction.Unknown, OemReleaseAction.Proven) ==
            OemReleaseAction.Proven, "unknown-primary-plus-proven-fallback-merges-proven");
        Require(FanHostEngine.IsOemReleaseSafe(true, "proven-by-scope-receipts") &&
            !FanHostEngine.IsOemReleaseSafe(true, "unknown-release-action"),
            "merged-proven-safe-unknown-not-safe");
        Require(FanHostEngine.ClassifyReleaseAction("explicit-failure") == OemReleaseAction.Failed,
            "explicit-failure-classifies-failed");
    }

    private static void RunDefaultHcQueuedConsumerSourceCases()
    {
        var route = RealHcBackend.ResolveRouteForSelfTest(Rog)!;
        static string Field(object? data, string name) =>
            data?.GetType().GetProperty(name)?.GetValue(data)?.ToString() ?? string.Empty;

        // r12: guarded synchronous composition through the real queued consumer.
        // The strict self-test clone/build branch is installed only for this
        // exact default-hc ROGAlly Host with the existing boundary/profile shape.
        var consumerBoundary = new RealHcBackend.SelfTestHcBoundary();
        var consumerHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var consumerDevice = new HandheldCompanion.Devices.ROGAlly();
        consumerHost.InstallSelfTestRouteSessionForSelfTest(consumerDevice, route, true);
        consumerHost.InstallHcBoundaryForSelfTest(consumerBoundary);
        consumerHost.SetActiveCurveDutiesForSelfTest(Original);
        RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = true;
        var consumerDiagnostics = new List<(string Name, object? Data)>();
        consumerHost.Diagnostic = (name, data) => consumerDiagnostics.Add((name, data));
        try
        {
            var generation = consumerHost.ActivateExternalProfileSessionForSelfTest();
            consumerHost.OnExternalProfileAppliedForSelfTest(
                generation, Profile(Original, "Software"), "Background");
            Require(consumerHost.ExternalProfileQueueHasPendingForSelfTest,
                "queued-consumer-positive-precondition-pending");
            Require(consumerBoundary.CallbackInvocationCount == 0 &&
                consumerBoundary.ProfileBuildCount == 0,
                "queued-consumer-positive-precondition-no-callback-or-build");
            Require(!consumerHost.HcRogSoftwareAppliedForSelfTest &&
                !consumerHost.HcRogReturnPendingForSelfTest,
                "queued-consumer-positive-precondition-ledger-empty");

            var consumerActed = consumerHost.RunPowerProfileWatchdogTickCoreForSelfTest();
            Require(consumerActed, "queued-consumer-positive-tick-core-dispatched");
            Require(consumerBoundary.ProfileBuildCount == 1,
                "queued-consumer-positive-one-build");
            Require(consumerBoundary.CallbackInvocationCount == 1,
                "queued-consumer-positive-one-callback");
            Require(consumerBoundary.CallbackSources.Count == 1 &&
                consumerBoundary.CallbackSources[0] == "Background",
                "queued-consumer-positive-real-source");
            Require(consumerHost.HcRogSoftwareAppliedForSelfTest &&
                !consumerHost.HcRogReturnPendingForSelfTest,
                "queued-consumer-positive-software-ledger");
            Require(!consumerHost.ExternalProfileQueueHasPendingForSelfTest,
                "queued-consumer-positive-queue-complete");
            Require(consumerDiagnostics.Any(d => d.Name == "hc.power-line-profile-applied"),
                "queued-consumer-positive-applied-diagnostic");
            Require(consumerDiagnostics.All(d => d.Name != "hc.external-profile-retry-scheduled"),
                "queued-consumer-positive-no-clone-retry");

            var callbackAfterFirst = consumerBoundary.CallbackInvocationCount;
            var buildAfterFirst = consumerBoundary.ProfileBuildCount;
            var repeatActed = consumerHost.RunPowerProfileWatchdogTickCoreForSelfTest();
            Require(!repeatActed, "queued-consumer-repeat-no-dispatch");
            Require(consumerBoundary.CallbackInvocationCount == callbackAfterFirst &&
                consumerBoundary.ProfileBuildCount == buildAfterFirst,
                "queued-consumer-repeat-no-extra-build-or-callback");
            Require(!consumerHost.ExternalProfileQueueHasPendingForSelfTest,
                "queued-consumer-repeat-no-pending");
            Require(consumerHost.HcRogSoftwareAppliedForSelfTest,
                "queued-consumer-repeat-ledger-retained");
        }
        finally
        {
            RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = false;
            consumerHost.Dispose();
            Reset();
        }

        // r12 normal-path control: an unsupported queued template must still take
        // the unchanged HC ManagerFactory/FanMode path and fail. The strict
        // self-test branch must not become unconditional.
        var unsupportedBoundary = new RealHcBackend.SelfTestHcBoundary();
        var unsupportedHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var unsupportedDevice = new HandheldCompanion.Devices.ROGAlly();
        unsupportedHost.InstallSelfTestRouteSessionForSelfTest(unsupportedDevice, route, true);
        unsupportedHost.InstallHcBoundaryForSelfTest(unsupportedBoundary);
        unsupportedHost.SetActiveCurveDutiesForSelfTest(Original);
        RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = true;
        var unsupportedDiagnostics = new List<(string Name, object? Data)>();
        unsupportedHost.Diagnostic = (name, data) => unsupportedDiagnostics.Add((name, data));
        try
        {
            var generation = unsupportedHost.ActivateExternalProfileSessionForSelfTest();
            unsupportedHost.OnExternalProfileAppliedForSelfTest(
                generation, new object(), "Background");
            Require(unsupportedHost.ExternalProfileQueueHasPendingForSelfTest,
                "queued-consumer-unsupported-precondition-pending");
            var unsupportedActed = unsupportedHost.RunPowerProfileWatchdogTickCoreForSelfTest();
            Require(!unsupportedActed, "queued-consumer-unsupported-no-dispatch");
            Require(unsupportedBoundary.CallbackInvocationCount == 0 &&
                unsupportedBoundary.ProfileBuildCount == 0,
                "queued-consumer-unsupported-no-callback-or-build");
            Require(!unsupportedHost.HcRogSoftwareAppliedForSelfTest &&
                !unsupportedHost.HcRogReturnPendingForSelfTest,
                "queued-consumer-unsupported-no-ledger");
            Require(unsupportedHost.ExternalProfileQueueHasPendingForSelfTest,
                "queued-consumer-unsupported-pending-retained");

            var cloneRetries = unsupportedDiagnostics
                .Where(d => d.Name == "hc.external-profile-retry-scheduled")
                .ToList();
            Require(cloneRetries.Count == 1,
                "queued-consumer-unsupported-exactly-one-clone-retry");
            Require(Field(cloneRetries[0].Data, "phase") == "clone",
                "queued-consumer-unsupported-phase-clone");
            Require(Field(cloneRetries[0].Data, "error").StartsWith(
                    "MissingMemberException: HC ManagerFactory not found",
                    StringComparison.Ordinal),
                "queued-consumer-unsupported-managerfactory-abi-absent");
        }
        finally
        {
            RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = false;
            unsupportedHost.Dispose();
            Reset();
        }

        // r11: same owning Host after attempted Close, before any helper
        // reactivation. The real consumer entry must not invoke the callback,
        // claim the next session ledger or resurrect the invalidated snapshot.
        var closedBoundary = new RealHcBackend.SelfTestHcBoundary();
        var closedHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var closedDevice = new HandheldCompanion.Devices.ROGAlly();
        closedHost.InstallSelfTestRouteSessionForSelfTest(closedDevice, route, true);
        closedHost.InstallHcBoundaryForSelfTest(closedBoundary);
        closedHost.SetActiveCurveDutiesForSelfTest(Original);
        RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = true;
        try
        {
            var generation = closedHost.ActivateExternalProfileSessionForSelfTest();
            closedHost.OnExternalProfileAppliedForSelfTest(
                generation, Profile(Original, "Software"), "Background");
            Require(closedHost.ExternalProfileQueueHasPendingForSelfTest,
                "queued-consumer-post-close-positive-precondition-pending");
            var callbackBeforeClose = closedBoundary.CallbackInvocationCount;
            try { closedHost.Close(); } catch (Exception) { }
            Require(!closedHost.ExternalProfileQueueHasPendingForSelfTest,
                "queued-consumer-post-close-pending-cleared");
            Require(!closedHost.HcRogSoftwareAppliedForSelfTest &&
                !closedHost.HcRogReturnPendingForSelfTest,
                "queued-consumer-post-close-ledger-empty");
            Require(closedBoundary.CallbackInvocationCount == callbackBeforeClose,
                "queued-consumer-post-close-close-no-callback");

            var closedActed = closedHost.RunPowerProfileWatchdogTickCoreForSelfTest();
            Require(!closedActed, "queued-consumer-post-close-tick-core-no-dispatch");
            Require(closedBoundary.CallbackInvocationCount == callbackBeforeClose &&
                closedBoundary.ProfileBuildCount == 0,
                "queued-consumer-post-close-no-callback-or-build");
            Require(!closedHost.HcRogSoftwareAppliedForSelfTest &&
                !closedHost.HcRogReturnPendingForSelfTest,
                "queued-consumer-post-close-no-ledger");
            Require(!closedHost.ExternalProfileQueueHasPendingForSelfTest,
                "queued-consumer-post-close-no-pending");
        }
        finally
        {
            RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = false;
            closedHost.Dispose();
            Reset();
        }
    }

    private static void RunDefaultHcQueuedConsumerAsyncCases()
    {
        var route = RealHcBackend.ResolveRouteForSelfTest(Rog)!;
        var boundary = new RealHcBackend.SelfTestHcBoundary();
        var host = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var device = new HandheldCompanion.Devices.ROGAlly();
        using var consumed = new ManualResetEventSlim();
        var ownerThread = Environment.CurrentManagedThreadId;
        var callbackThread = ownerThread;
        host.InstallSelfTestRouteSessionForSelfTest(device, route, true);
        host.InstallHcBoundaryForSelfTest(boundary);
        host.SetActiveCurveDutiesForSelfTest(Original);
        host.AsusPrimaryReleaseVerdictForSelfTest =
            new RealHcBackend.AsusWriteVerdict("transported", "async-software-terminal", false, false, false, 3);
        host.Diagnostic = (name, _) =>
        {
            if (name == "profile.watchdog-sample") consumed.Set();
        };
        boundary.AfterApply = () => callbackThread = Environment.CurrentManagedThreadId;
        RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = false;
        try
        {
            var generation = host.ActivateExternalProfileSessionForSelfTest();
            var retiredCallback = host.CapturePowerProfileWatchdogCallbackForSelfTest();
            host.StartPowerProfileWatchdogForSelfTest();
            host.OnExternalProfileAppliedForSelfTest(generation, Profile(Original, "Software"), "Background");
            Require(consumed.Wait(TimeSpan.FromSeconds(2)), "queued-async-real-timer-consumed");
            Require(callbackThread != ownerThread, "queued-async-callback-on-timer-thread");
            Require(boundary.ProfileBuildCount == 1 && boundary.CallbackInvocationCount == 1,
                "queued-async-single-build-and-callback");
            Require(host.HcRogSoftwareAppliedForSelfTest && !host.HcRogReturnPendingForSelfTest,
                "queued-async-host-ledger-applied");
            Require(!host.ExternalProfileQueueHasPendingForSelfTest, "queued-async-real-queue-completed");
            host.Close();
            Require(!host.IsOpen && !host.PowerProfileWatchdogActiveForSelfTest,
                "queued-async-close-disposes-owned-timer");
            Require(!host.ExternalProfileQueueHasPendingForSelfTest &&
                !host.HcRogSoftwareAppliedForSelfTest && !host.HcRogReturnPendingForSelfTest,
                "queued-async-close-clears-queue-and-software-ledger");
            var afterClose = boundary.CallbackInvocationCount;
            retiredCallback();
            host.OnExternalProfileAppliedForSelfTest(generation, Profile(Original, "Software"), "Background");
            Require(boundary.CallbackInvocationCount == afterClose && !host.ExternalProfileQueueHasPendingForSelfTest,
                "queued-async-late-callback-and-old-event-after-close-refused");

            // Production Close is terminal for this backend. A successor has
            // its own Host/queue/owner; never reset a close gate to make it pass.
            var nextBoundary = new RealHcBackend.SelfTestHcBoundary();
            using var nextConsumed = new ManualResetEventSlim();
            using var nextHost = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
            nextHost.InstallSelfTestRouteSessionForSelfTest(new HandheldCompanion.Devices.ROGAlly(), route, true);
            nextHost.InstallHcBoundaryForSelfTest(nextBoundary);
            nextHost.SetActiveCurveDutiesForSelfTest(Original);
            nextHost.AsusPrimaryReleaseVerdictForSelfTest =
                new RealHcBackend.AsusWriteVerdict("transported", "async-successor-terminal", false, false, false, 3);
            nextHost.Diagnostic = (name, _) =>
            {
                if (name == "profile.watchdog-sample") nextConsumed.Set();
            };
            var nextGeneration = nextHost.ActivateExternalProfileSessionForSelfTest();
            retiredCallback();
            host.OnExternalProfileAppliedForSelfTest(generation, Profile(Original, "Software"), "Background");
            Require(boundary.CallbackInvocationCount == afterClose && !host.ExternalProfileQueueHasPendingForSelfTest &&
                nextBoundary.CallbackInvocationCount == 0 && !nextHost.ExternalProfileQueueHasPendingForSelfTest,
                "queued-async-retired-callback-cannot-write-successor-host");
            nextHost.StartPowerProfileWatchdogForSelfTest();
            nextHost.OnExternalProfileAppliedForSelfTest(nextGeneration, Profile(Original, "Software"), "Background");
            Require(nextConsumed.Wait(TimeSpan.FromSeconds(2)), "queued-async-successor-real-timer-consumed");
            Require(nextBoundary.CallbackInvocationCount == 1 && nextHost.HcRogSoftwareAppliedForSelfTest &&
                !nextHost.ExternalProfileQueueHasPendingForSelfTest,
                "queued-async-successor-single-callback-ledger-and-complete");
        }
        finally
        {
            host.Dispose();
            RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = false;
            Reset();
        }
    }

    private sealed class LegacyOneXDevice
    {
        public bool UseOpenLib { get; set; } = true;
        public bool IsOpen { get; set; } = true;
        public bool IsSupported { get; set; } = true;
        public int OpenEventsCount { get; private set; }
        public Action? OpenEventsAction { get; set; }
        public void OpenEvents() { OpenEventsCount++; OpenEventsAction?.Invoke(); }
        public bool ThrowClose { get; set; }
        public int CloseCount { get; private set; }
        public void Close()
        {
            CloseCount++;
            if (ThrowClose) throw new IOException("one-x-close-terminal-failure");
            IsOpen = false;
        }
    }
    private static (RealHcBackend Host, LegacyOneXDevice Device, RealHcBackend.SelfTestHcBoundary Boundary)
        LegacyOneXSession(FanRoute route)
    {
        var host = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        var device = new LegacyOneXDevice();
        var boundary = new RealHcBackend.SelfTestHcBoundary();
        host.InstallSelfTestRouteSessionForSelfTest(device, route, true);
        host.InstallHcBoundaryForSelfTest(boundary);
        return (host, device, boundary);
    }
    private static void RunLegacyOneXInitialHandoffGuard()
    {
        var routes = RealHcBackend.FanRoutesForSelfTest
            .Where(pair => pair.Value.RestoreStrategy == FanRestoreStrategy.OneXAcpiFanMode).ToArray();
        Require(routes.Length == 9, "legacy-one-x-existing-catalog-nine-strategies");
        foreach (var pair in routes)
        {
            var (host, device, boundary) = LegacyOneXSession(pair.Value);
            try
            {
                host.ApplyPowerProfileForSelfTest(Profile(Array.Empty<double>(), "Hardware"));
                Require(boundary.CallbackInvocationCount == 0,
                    "legacy-one-x-no-prior-hardware-skipped:" + pair.Key);
                host.RestoreOem();
                Require(boundary.CallbackInvocationCount == 0 &&
                    host.OemReleaseActionKind == FanHostEngine.ReleaseActionKindName(OemReleaseAction.NotApplicable) &&
                    host.OemRestoreEvidence == "not-applicable-no-write-history" &&
                    !host.LegacyOneXFanReturnOwedForSelfTest,
                    "legacy-one-x-no-prior-restore-not-applicable:" + pair.Key);
                host.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
                Require(boundary.CallbackInvocationCount == 1 && host.LegacyOneXFanReturnOwedForSelfTest,
                    "legacy-one-x-software-callback-creates-return-debt:" + pair.Key);
                host.RestoreOem();
                Require(boundary.CallbackInvocationCount == 2 && !host.LegacyOneXFanReturnOwedForSelfTest &&
                    host.OemOwnershipStatus == "hc-callback-only-physical-unknown",
                    "legacy-one-x-qualified-restore-clears-debt-not-physical-proof:" + pair.Key);
                host.ApplyPowerProfileForSelfTest(Profile(Array.Empty<double>(), "Hardware"));
                Require(boundary.CallbackInvocationCount == 2,
                    "legacy-one-x-default-after-qualified-return-skipped:" + pair.Key);
                host.Close(false);
                Require(device.CloseCount == 1 && !host.LegacyOneXFanReturnOwedForSelfTest,
                    "legacy-one-x-original-virtual-close-still-runs:" + pair.Key);
                var next = new LegacyOneXDevice();
                host.InstallSelfTestRouteSessionForSelfTest(next, pair.Value, true);
                host.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
                Require(boundary.CallbackInvocationCount == 3 && host.LegacyOneXFanReturnOwedForSelfTest,
                    "legacy-one-x-new-exact-owner-can-control-after-retirement:" + pair.Key);
                host.RestoreOem();
            }
            finally { host.Dispose(); }
        }
        var route = routes.First(pair => pair.Key == "HandheldCompanion.Devices.AOKZOEA1").Value;
        var (failed, failedDevice, failedBoundary) = LegacyOneXSession(route);
        try
        {
            failedBoundary.CallbackFailure = new IOException("one-x-partial-software-attempt");
            var threw = false;
            try { failed.ApplyPowerProfileForSelfTest(Profile(Original, "Software")); }
            catch (IOException) { threw = true; }
            Require(threw && failed.LegacyOneXFanReturnOwedForSelfTest,
                "legacy-one-x-throwing-software-attempt-retains-debt");
            failedBoundary.CallbackFailure = null;
            failed.AsusPrimaryReleaseVerdictForSelfTest =
                new RealHcBackend.AsusWriteVerdict("unknown", "one-x-return-unproven", false, true, false, 0);
            var unproven = false;
            try { failed.RestoreOem(); } catch (InvalidOperationException) { unproven = true; }
            Require(unproven && failed.LegacyOneXFanReturnOwedForSelfTest,
                "legacy-one-x-unproven-return-retains-debt");
            failed.AsusPrimaryReleaseVerdictForSelfTest = null;
            failedDevice.IsOpen = false;
            var closedRejected = false;
            try { failed.RestoreOem(); } catch (Exception) { closedRejected = true; }
            Require(closedRejected && failed.LegacyOneXFanReturnOwedForSelfTest,
                "legacy-one-x-closed-device-cannot-erase-return-debt");
            failedDevice.IsOpen = true;
            failed.RestoreOem();
            Require(!failed.LegacyOneXFanReturnOwedForSelfTest,
                "legacy-one-x-current-qualified-retry-clears-debt");
            failed.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            var callbacks = failedBoundary.CallbackInvocationCount;
            var foreign = new LegacyOneXDevice();
            failed.InstallSelfTestRouteSessionForSelfTest(foreign, route, true);
            var wrongOwner = false;
            try { failed.ApplyPowerProfileForSelfTest(Profile(Array.Empty<double>(), "Hardware")); }
            catch (FanApiException error) when (error.Code == "FAN_RETURN_OWNER_CHANGED") { wrongOwner = true; }
            Require(wrongOwner && failedBoundary.CallbackInvocationCount == callbacks &&
                failed.LegacyOneXFanReturnOwedForSelfTest,
                "legacy-one-x-old-debt-cannot-cross-exact-device-instance");
            failed.InstallSelfTestRouteSessionForSelfTest(failedDevice, route, true);
            failed.RestoreOem();
        }
        finally { failed.Dispose(); }

        // The exact fixed setters return early for a closed or disabled
        // device. The Host must not invent a software-control obligation.
        foreach (var mode in new[] { "device-closed", "openlib-disabled", "backend-closed" })
        {
            var (inactive, inactiveDevice, inactiveBoundary) = LegacyOneXSession(route);
            try
            {
                if (mode == "device-closed") inactiveDevice.IsOpen = false;
                if (mode == "openlib-disabled") inactiveDevice.UseOpenLib = false;
                if (mode == "backend-closed")
                    inactive.InstallSelfTestRouteSessionForSelfTest(inactiveDevice, route, false);
                inactive.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
                Require(!inactive.LegacyOneXFanReturnOwedForSelfTest,
                    "legacy-one-x-inactive-software-does-not-create-debt:" + mode);
            }
            finally { inactive.Dispose(); }
        }

        var (queued, queuedDevice, queuedBoundary) = LegacyOneXSession(route);
        var previousSuppress = RealHcBackend.SuppressExternalProfileWatchdogForSelfTest;
        RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = true;
        var ignoredReasons = new List<string>();
        queued.Diagnostic = (name, data) =>
        {
            if (name == "hc.external-profile-applied-ignored" && data is not null)
                ignoredReasons.Add(data.GetType().GetProperty("reason")?.GetValue(data)?.ToString() ?? "");
        };
        try
        {
            var oldGeneration = queued.ActivateExternalProfileSessionForSelfTest();
            queued.OnExternalProfileAppliedForSelfTest(oldGeneration, Profile(Original, "Software"), "Background");
            Require(queued.ExternalProfileQueueHasPendingForSelfTest && !queued.LegacyOneXFanReturnOwedForSelfTest,
                "legacy-one-x-actual-current-queue-admits-without-premature-debt");
            queued.Close(false);
            queued.OnExternalProfileAppliedForSelfTest(oldGeneration, Profile(Original, "Software"), "Background");
            Require(ignoredReasons.Last() == "session-inactive" && !queued.ExternalProfileQueueHasPendingForSelfTest &&
                !queued.LegacyOneXFanReturnOwedForSelfTest && queuedBoundary.CallbackInvocationCount == 0,
                "legacy-one-x-late-callback-after-close-cannot-create-debt");
            var nextGeneration = queued.ActivateExternalProfileSessionForSelfTest();
            queued.OnExternalProfileAppliedForSelfTest(oldGeneration, Profile(Original, "Software"), "Background");
            Require(nextGeneration != oldGeneration && ignoredReasons.Last() == "stale-session-generation" &&
                !queued.ExternalProfileQueueHasPendingForSelfTest && !queued.LegacyOneXFanReturnOwedForSelfTest &&
                queuedBoundary.CallbackInvocationCount == 0,
                "legacy-one-x-actual-next-generation-rejects-old-callback-without-debt");
        }
        finally
        {
            queued.Dispose();
            RealHcBackend.SuppressExternalProfileWatchdogForSelfTest = previousSuppress;
        }

        var (closing, closingDevice, closingBoundary) = LegacyOneXSession(route);
        try
        {
            closing.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            closingDevice.ThrowClose = true;
            var threw = false;
            try { closing.Close(false); } catch (Exception) { threw = true; }
            Require(threw && closing.LegacyOneXFanReturnOwedForSelfTest,
                "legacy-one-x-throwing-virtual-close-retains-debt");
            closingDevice.ThrowClose = false;
            closingBoundary.ManagerObservationAvailable = false;
            var pending = false;
            try { closing.Close(false); } catch (Exception) { pending = true; }
            Require(pending && closing.LegacyOneXFanReturnOwedForSelfTest,
                "legacy-one-x-unsettled-manager-cleanup-retains-debt");
            closingBoundary.ManagerObservationAvailable = true;
            closing.Close(false);
            Require(!closing.LegacyOneXFanReturnOwedForSelfTest && closingDevice.CloseCount == 3,
                "legacy-one-x-exact-close-and-settled-cleanup-retire-debt");
        }
        finally { closing.Dispose(); }
    }

    private static void InvokeRawOpenEventsEntry(RealHcBackend host)
    {
        try
        {
            typeof(RealHcBackend).GetMethod("OpenEventsBeginCore", BindingFlags.Instance | BindingFlags.NonPublic)!
                .Invoke(host, null);
        }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        {
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(error.InnerException).Throw();
        }
    }
    private static void RunOtherLegacyInitialDefaultGuard(string evidenceDirectory)
    {
        var registry = RealHcBackend.FanRoutesForSelfTest.ToArray();
        // Independent fixture selection follows the original non-profile legacy
        // contracts, not the new production predicate or a second model table.
        var legacy = registry.Where(x => x.Value.Evidence == FanEvidenceMode.CompatCallbackNoScope &&
            x.Value.Kind != FanRouteKind.ProfileCurve).ToArray();
        var added = legacy.Where(x => x.Value.RestoreStrategy != FanRestoreStrategy.OneXAcpiFanMode).ToArray();
        Require(legacy.Length == 59 && added.Length == 50,
            "legacy-applied-existing59-and-other50-alias-units");
        foreach (var pair in added)
        {
            var (host, device, boundary) = LegacyOneXSession(pair.Value);
            try
            {
                host.ApplyPowerProfileForSelfTest(Profile(Array.Empty<double>(), "Hardware"));
                Require(boundary.CallbackInvocationCount == 0,
                    "legacy-applied-initial-default-without-software-skipped:" + pair.Key);
                host.RestoreOem();
                Require(boundary.CallbackInvocationCount == 0 && !host.AppliedProfileReturnOwedForSelfTest &&
                    host.OemRestoreEvidence == "not-applicable-no-write-history",
                    "legacy-applied-no-history-restore-not-release-proof:" + pair.Key);
                host.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
                Require(boundary.CallbackInvocationCount == 1 && host.AppliedProfileReturnOwedForSelfTest,
                    "legacy-applied-current-software-creates-return-debt:" + pair.Key);
                host.RestoreOem();
                Require(boundary.CallbackInvocationCount == 2 && !host.AppliedProfileReturnOwedForSelfTest &&
                    host.OemOwnershipStatus == "hc-callback-only-physical-unknown",
                    "legacy-applied-qualified-callback-return-clears-not-physical:" + pair.Key);
                host.ApplyPowerProfileForSelfTest(Profile(Array.Empty<double>(), "Hardware"));
                Require(boundary.CallbackInvocationCount == 2,
                    "legacy-applied-default-after-return-is-idempotent:" + pair.Key);
                host.Close(false);
                Require(device.CloseCount == 1 && !host.AppliedProfileReturnOwedForSelfTest,
                    "legacy-applied-original-virtual-close-retained:" + pair.Key);
            }
            finally { host.Dispose(); }
        }
        var groups = added.GroupBy(x => (x.Value.RestoreStrategy, x.Value.Precondition)).Select(x => x.First()).ToArray();
        Require(groups.Length == 8, "legacy-applied-eight-distinct-other-contract-pairs");
        foreach (var pair in groups)
        {
            var (host, device, boundary) = LegacyOneXSession(pair.Value);
            try
            {
                boundary.CallbackFailure = new IOException("legacy-applied-partial-control");
                var threw = false;
                try { host.ApplyPowerProfileForSelfTest(Profile(Original, "Software")); }
                catch (IOException) { threw = true; }
                Require(threw && host.AppliedProfileReturnOwedForSelfTest,
                    "legacy-applied-throw-retains-debt:" + pair.Value.RestoreStrategy + "/" + pair.Value.Precondition);
                boundary.CallbackFailure = null;
                host.AsusPrimaryReleaseVerdictForSelfTest =
                    new RealHcBackend.AsusWriteVerdict("unknown", "legacy-return-unproven", false, true, false, 0);
                var unproven = false;
                try { host.RestoreOem(); } catch (InvalidOperationException) { unproven = true; }
                Require(unproven && host.AppliedProfileReturnOwedForSelfTest,
                    "legacy-applied-unproven-return-retains-debt:" + pair.Value.RestoreStrategy + "/" + pair.Value.Precondition);
                host.AsusPrimaryReleaseVerdictForSelfTest = null;
                host.RestoreOem();
                Require(!host.AppliedProfileReturnOwedForSelfTest,
                    "legacy-applied-qualified-retry-clears:" + pair.Value.RestoreStrategy + "/" + pair.Value.Precondition);
            }
            finally { host.Dispose(); }
        }
        var steam = legacy.Single(x => x.Key == "HandheldCompanion.Devices.SteamDeck");
        var (valve, valveDevice, valveBoundary) = LegacyOneXSession(steam.Value);
        try
        {
            valveDevice.UseOpenLib = false;
            valve.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            Require(valve.AppliedProfileReturnOwedForSelfTest,
                "legacy-applied-steam-supported-open-does-not-require-openlib");
            valve.RestoreOem();
            valveDevice.IsSupported = false;
            valve.ApplyPowerProfileForSelfTest(Profile(Original, "Software"));
            Require(!valve.AppliedProfileReturnOwedForSelfTest,
                "legacy-applied-steam-unsupported-cannot-create-debt");
        }
        finally { valve.Dispose(); }
        // Existing MSI/Lenovo table and ASUS provider paths are excluded.
        var excluded = registry.Where(x => x.Value.Kind == FanRouteKind.ProfileCurve).ToArray();
        Require(excluded.Length == 11, "legacy-applied-eleven-profile-provider-aliases-excluded");
        foreach (var pair in excluded)
        {
            var (host, device, boundary) = LegacyOneXSession(pair.Value);
            try
            {
                host.ApplyPowerProfileForSelfTest(Profile(Array.Empty<double>(), "Hardware"));
                Require(boundary.CallbackInvocationCount == 1 && !host.AppliedProfileReturnOwedForSelfTest,
                    "legacy-applied-independent-profile-provider-retained:" + pair.Key);
            }
            finally { host.Dispose(); }
        }
        // This calls the actual raw production OpenEvents entry with an explicit
        // fixture assembly identity and inert mock methods. It is not an HC
        // payload identity, a real device constructor or a transport positive.
        Directory.CreateDirectory(evidenceDirectory);
        var authorization = Path.Combine(evidenceDirectory, "raw-legacy-authorization.txt");
        File.WriteAllText(authorization, "hardwareWriteAuthorizationGranted: true\nallMappedFanRoutesAuthorized: true\n");
        const string token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        var rawRoutes = new[] { legacy.First(x => x.Value.RestoreStrategy == FanRestoreStrategy.AyanEOCEiiModeRegister),
            legacy.First(x => x.Value.RestoreStrategy == FanRestoreStrategy.OneXAcpiFanMode), steam };
        foreach (var pair in rawRoutes)
        foreach (var partialThrow in new[] { false, true })
        {
            var host = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()) with {
                AllowHardwareWrites = true, AuthorizationPath = authorization, Confirmation = token, SessionToken = token });
            var device = new LegacyOneXDevice();
            var boundary = new RealHcBackend.SelfTestHcBoundary();
            host.InstallSelfTestRouteSessionForSelfTest(device, pair.Value, true);
            host.InstallHcBoundaryForSelfTest(boundary);
            foreach (var name in new[] { "assembly", "hcInitializationAssembly" })
                typeof(RealHcBackend).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!
                    .SetValue(host, typeof(FanCapabilityRoundtrip).Assembly);
            typeof(RealHcBackend).GetField("hcInitializationDevice", BindingFlags.Instance | BindingFlags.NonPublic)!
                .SetValue(host, device);
            if (partialThrow) device.OpenEventsAction = () => throw new IOException("raw-open-events-partial");
            try
            {
                var threw = false;
                try { InvokeRawOpenEventsEntry(host); }
                catch (IOException error) when (error.Message == "raw-open-events-partial") { threw = true; }
                catch (TargetInvocationException error) when
                    (error.InnerException is IOException inner && inner.Message == "raw-open-events-partial") { threw = true; }
                Require(threw == partialThrow && device.OpenEventsCount == 1 && host.AppliedProfileReturnOwedForSelfTest,
                    "legacy-applied-raw-entry-before-return-debt:" + pair.Key + "/" + partialThrow);
                if (partialThrow)
                {
                    var blocked = false;
                    try { InvokeRawOpenEventsEntry(host); }
                    catch (FanApiException error) when (error.Code == "HC_CLOSE_PENDING") { blocked = true; }
                    Require(blocked && device.OpenEventsCount == 1 && host.AppliedProfileReturnOwedForSelfTest,
                        "legacy-applied-partial-raw-entry-not-replayed:" + pair.Key);
                }
                else
                {
                    host.RestoreOem();
                    Require(boundary.CallbackInvocationCount == 1 && !host.AppliedProfileReturnOwedForSelfTest,
                        "legacy-applied-raw-risk-cannot-be-skipped-as-no-history:" + pair.Key);
                }
                host.Close(false);
                Require(device.CloseCount == 1 && !host.AppliedProfileReturnOwedForSelfTest,
                    "legacy-applied-original-close-retires-raw-risk:" + pair.Key + "/" + partialThrow);
                Trace.Add(new { stage = "legacy-applied-raw-entry", alias = pair.Key, partialThrow,
                    device.OpenEventsCount, device.CloseCount, callbackCount = boundary.CallbackInvocationCount,
                    testAssemblyIdentity = typeof(FanCapabilityRoundtrip).Assembly.GetName().Name,
                    actualHcAssemblyUsedAtRawBoundary = false, hardwareTransportInjected = true });
            }
            finally { host.Dispose(); }
        }
    }

    // Real Host dispatch with an inert vendor callback, not real Lenovo WMI.
    // The fixture deliberately inherits both base EC setters and has no EC handle,
    // matching the shape that previously hijacked Lenovo's ProfileCurve callback.
    private static void RunLenovoProfileProtocolSelection()
    {
        var registry = RealHcBackend.FanRoutesForSelfTest;
        var routes = registry.Where(x => x.Value.RestoreStrategy == FanRestoreStrategy.LenovoHcDefaultTable).ToArray();
        Require(routes.Length == 3, "lenovo-profile:three-table-family-routes");
        foreach (var name in new[] { "LegionGoTablet", "LegionGoSZ1", "LegionGoSZ2", "LegionGoTablet2" })
        {
            var descriptor = HcDeviceCapabilityCatalog.Find("hc.device." + name)!;
            var selected = RealHcBackend.ResolveRouteForSelfTest(descriptor.ConsumerClass);
            Require(descriptor.Instantiable && descriptor.Family == "Lenovo" && selected is not null,
                "lenovo-profile:catalog-family-has-actual-consumer:" + name);
            foreach (var alias in new[] { descriptor.DeviceId, descriptor.FactoryType, descriptor.ConsumerClass })
                Require(ReferenceEquals(RealHcBackend.ResolveRouteForSelfTest(alias), selected),
                    "lenovo-profile:catalog-alias-selects-same-consumer:" + alias);
        }
        Require(RealHcBackend.ResolveRouteForSelfTest("hc.device.LegionGo") is null &&
            RealHcBackend.ResolveRouteForSelfTest("HandheldCompanion.Devices.Lenovo.LegionGoUnknown") is null,
            "lenovo-profile:base-and-unknown-do-not-invent-enabled-routes");
        foreach (var pair in routes)
        {
            var target = new HandheldCompanion.Devices.ProfileRouteFixtureDevice();
            var host = new RealHcBackend(HostOptions.Parse(new[] { "--fan-provider", "hc" }));
            host.InstallSelfTestRouteSessionForSelfTest(target, pair.Value, true);
            typeof(RealHcBackend).GetField("assembly", BindingFlags.Instance | BindingFlags.NonPublic)!
                .SetValue(host, typeof(FanCapabilityRoundtrip).Assembly);
            try
            {
                Require(host.hcBoundaryForSelfTest is null && !target.UseOpenLib &&
                    HandheldCompanion.Devices.IDevice.ExistingTransportForSelfTest is null,
                    "lenovo-profile:no-callback-seam-or-ec-handle:" + pair.Key);
                var software = Profile(Original, "Software");
                var hardware = Profile(Array.Empty<double>(), "Hardware");
                var sessionType = typeof(RealHcBackend).GetNestedType("InheritedEcFanSession", BindingFlags.NonPublic)!;
                var sessionField = typeof(RealHcBackend).GetField("inheritedEcFanSession", BindingFlags.Instance | BindingFlags.NonPublic)!;
                object Session(object owner) => Activator.CreateInstance(sessionType,
                    BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, null,
                    new object[] { owner, typeof(HandheldCompanion.Devices.IDevice) }, null)!;
                var staleOwnerRefused = false;
                sessionField.SetValue(host, Session(new HandheldCompanion.Devices.ProfileRouteFixtureDevice()));
                try { host.ApplyPowerProfileForSelfTest(software); }
                catch (InvalidOperationException failure)
                { staleOwnerRefused = failure.Message.Contains("HC_INHERITED_EC_SESSION: device ownership changed"); }
                finally { sessionField.SetValue(host, null); }
                Require(staleOwnerRefused && target.CallbackCalls == 0 && target.EcSetterCalls == 0,
                    "lenovo-profile:stale-ec-owner-refused-before-non-ec-short-circuit:" + pair.Key);
                var foreignTransportRefused = false;
                sessionField.SetValue(host, Session(target));
                HandheldCompanion.Devices.IDevice.ExistingTransportForSelfTest = new object();
                try { host.ApplyPowerProfileForSelfTest(software); }
                catch (InvalidOperationException failure)
                { foreignTransportRefused = failure.Message.Contains("HC_INHERITED_EC_SESSION: transport ownership changed"); }
                finally
                {
                    HandheldCompanion.Devices.IDevice.ExistingTransportForSelfTest = null;
                    sessionField.SetValue(host, null);
                }
                Require(foreignTransportRefused && target.CallbackCalls == 0 && target.EcSetterCalls == 0,
                    "lenovo-profile:foreign-ec-transport-refused-before-non-ec-short-circuit:" + pair.Key);
                host.ApplyPowerProfileForSelfTest(software);
                host.ApplyPowerProfileForSelfTest(hardware);
                Require(target.Profiles.Count == 2 && ReferenceEquals(target.Profiles[0], software) &&
                    ReferenceEquals(target.Profiles[1], hardware) && target.EcSetterCalls == 0,
                    "lenovo-profile:software-and-hardware-reach-original-callback-once:" + pair.Key);
                target.CallbackFailure = new IOException("injected Lenovo profile callback failure");
                var propagated = false;
                try { host.ApplyPowerProfileForSelfTest(software); }
                catch (Exception failure) { propagated = ReferenceEquals(failure, target.CallbackFailure) ||
                    ReferenceEquals(failure.InnerException, target.CallbackFailure); }
                Require(propagated && target.CallbackCalls == 3 && target.EcSetterCalls == 0,
                    "lenovo-profile:callback-failure-is-not-replaced-by-ec-fallback:" + pair.Key);
                var getter = typeof(RealHcBackend).GetMethod("GetInheritedEcFanWriter", BindingFlags.Instance | BindingFlags.NonPublic)!;
                foreach (var halfMatch in new[] {
                    pair.Value with { Kind = FanRouteKind.GenericDuty },
                    pair.Value with { RestoreStrategy = FanRestoreStrategy.GenericEcControl } })
                {
                    var halfHost = new RealHcBackend(HostOptions.Parse(new[] { "--fan-provider", "hc" }));
                    halfHost.InstallSelfTestRouteSessionForSelfTest(target, halfMatch, true);
                    try
                    {
                        Require(getter.Invoke(halfHost, new object[] { target }) is null,
                            "lenovo-profile:partial-protocol-match-cannot-select-ec:" + pair.Key + ":" + halfMatch.Kind);
                    }
                    finally
                    {
                        halfHost.InstallHcBoundaryForSelfTest(new RealHcBackend.SelfTestHcBoundary());
                        halfHost.Dispose();
                    }
                }
                Trace.Add(new { stage = "lenovo-profile-protocol-selection", alias = pair.Key,
                    target.CallbackCalls, target.EcSetterCalls, realHostDispatch = true,
                    hcCallbackTerminal = "inert fixture callback", actualLenovoWmi = false, physicalAcceptance = false });
            }
            finally
            {
                // Only teardown uses the existing ManagerFactory observation seam.
                host.InstallHcBoundaryForSelfTest(new RealHcBackend.SelfTestHcBoundary());
                host.Dispose();
            }
        }
        var ec = registry.First(x => x.Value.Kind == FanRouteKind.GenericDuty &&
            x.Value.RestoreStrategy == FanRestoreStrategy.GenericEcControl);
        var ecTarget = new HandheldCompanion.Devices.InheritedEcRouteFixtureDevice();
        var ecHost = new RealHcBackend(HostOptions.Parse(new[] { "--fan-provider", "hc" }));
        ecHost.InstallSelfTestRouteSessionForSelfTest(ecTarget, ec.Value, true);
        typeof(RealHcBackend).GetField("assembly", BindingFlags.Instance | BindingFlags.NonPublic)!
            .SetValue(ecHost, typeof(FanCapabilityRoundtrip).Assembly);
        try
        {
            var refused = false;
            try { ecHost.ApplyPowerProfileForSelfTest(Profile(Original, "Software")); }
            catch (InvalidOperationException failure) { refused = failure.Message.Contains("HC_INHERITED_EC_FAN_CONTRACT") &&
                failure.Message.Contains("existing owned EC transport is unavailable"); }
            Require(refused && ecTarget.BaseProfileCalls == 0 && ecTarget.EcSetterCalls == 0,
                "lenovo-profile:real-ec-route-still-refuses-missing-transport");
            Require(registry["HandheldCompanion.Devices.LegionGoTablet2"].RestoreStrategy ==
                FanRestoreStrategy.LegionGo2EcRpmOverride, "lenovo-profile:go2-rpm-contract-remains-independent");
        }
        finally
        {
            ecHost.InstallHcBoundaryForSelfTest(new RealHcBackend.SelfTestHcBoundary());
            ecHost.Dispose();
        }
    }

    // Existing SourceOnly consumer: exact protocol dispatch, not actual device/EC proof.
    private static void RunInheritedEcProtocolContract()
    {
        var registry = RealHcBackend.FanRoutesForSelfTest;
        var getter = typeof(RealHcBackend).GetMethod("GetInheritedEcFanWriter", BindingFlags.Instance|BindingFlags.NonPublic)!;
        var writerType = typeof(RealHcBackend).GetNestedType("InheritedEcFanWriter", BindingFlags.NonPublic)!;
        var bind = writerType.GetMethod("Bind", BindingFlags.Static|BindingFlags.NonPublic)!;
        void Configure(HandheldCompanion.Devices.IDevice target)
        {
            target.UseOpenLib=true;target.IsOpen=true;
            target.ECDetails = new HandheldCompanion.Devices.ProfileFixtureEcDetails {
                AddressStatusCommandPort=0x4e,AddressDataPort=0x4f,AddressFanControl=0x100,
                AddressFanDuty=0x101,FanValueMin=0,FanValueMax=244 };
        }
        object? Bind(object target, FanRoute route) => bind.Invoke(null,
            bind.GetParameters().Length==2?new object?[]{target,route}:new object?[]{target});
        var savedTransport=HandheldCompanion.Devices.IDevice.ExistingTransportForSelfTest;
        HandheldCompanion.Devices.IDevice.ExistingTransportForSelfTest=new object();
        try
        {
            foreach(var pair in registry)
            {
                var target=new HandheldCompanion.Devices.InheritedEcRouteFixtureDevice();Configure(target);
                var route=pair.Value;
                var expected=route.Kind==FanRouteKind.GenericDuty&&route.RestoreStrategy==FanRestoreStrategy.GenericEcControl;
                var direct=Bind(target,route);
                Require((direct is not null)==expected,"ec-protocol:bind-honors-route:"+pair.Key);
                var host=new RealHcBackend(HostOptions.Parse(new[]{"--fan-provider","hc"}));
                host.InstallSelfTestRouteSessionForSelfTest(target,route,true);
                try { Require((getter.Invoke(host,new object[]{target}) is not null)==expected,
                    "ec-protocol:writer-getter-honors-route:"+pair.Key); }
                finally { host.InstallHcBoundaryForSelfTest(new RealHcBackend.SelfTestHcBoundary());host.Dispose(); }
                Require(target.EcSetterCalls==0&&target.BaseProfileCalls==0,"ec-protocol:bind-is-readonly:"+pair.Key);
            }
            var generic=registry.First(x=>x.Value.Kind==FanRouteKind.GenericDuty&&x.Value.RestoreStrategy==FanRestoreStrategy.GenericEcControl).Value;
            var aliases=(IReadOnlyDictionary<string,HcDeviceDescriptor>)typeof(HcDeviceCapabilityCatalog)
                .GetField("aliases",BindingFlags.Static|BindingFlags.NonPublic)!.GetValue(null)!;
            var registered=registry.ToDictionary(pair=>HcDeviceCapabilityCatalog.Find(pair.Key)!.DeviceId,pair=>pair.Value,StringComparer.Ordinal);
            var catalogRows=new List<object>();
            foreach(var model in HcDeviceCapabilityCatalog.Devices)
            {
                registered.TryGetValue(model.DeviceId,out var route);
                var identities=aliases.Where(pair=>ReferenceEquals(pair.Value,model)).Select(pair=>pair.Key).OrderBy(x=>x,StringComparer.Ordinal).ToArray();
                Require(identities.Contains(model.DeviceId)&&identities.Contains(model.ConsumerClass),
                    "ec-catalog:canonical-identities-present:"+model.DeviceId);
                foreach(var identity in identities)
                    Require(ReferenceEquals(RealHcBackend.ResolveRouteForSelfTest(identity),route),
                        "ec-catalog:actual-resolver-matches-registration:"+identity);
                if(route is not null)
                {
                    var target=new HandheldCompanion.Devices.InheritedEcRouteFixtureDevice();Configure(target);
                    var wrongKind=route with {Kind=FanRouteKind.ProfileCurve,RestoreStrategy=FanRestoreStrategy.GenericEcControl};
                    var wrongRestore=route with {Kind=FanRouteKind.GenericDuty,RestoreStrategy=FanRestoreStrategy.LenovoHcDefaultTable};
                    Require(Bind(target,wrongKind) is null,"ec-catalog:wrong-kind-refused:"+model.DeviceId);
                    Require(Bind(target,wrongRestore) is null,"ec-catalog:wrong-restore-refused:"+model.DeviceId);
                    var overrideTarget=new HandheldCompanion.Devices.ProfileRouteFixtureDevice();Configure(overrideTarget);
                    Require(Bind(overrideTarget,route) is null,"ec-catalog:vendor-override-retained:"+model.DeviceId);
                    Require(target.EcSetterCalls==0&&target.BaseProfileCalls==0&&overrideTarget.EcSetterCalls==0&&overrideTarget.CallbackCalls==0,
                        "ec-catalog:counterexamples-do-not-write:"+model.DeviceId);
                }
                catalogRows.Add(new {model.DeviceId,model.ConsumerClass,model.Family,model.Instantiable,
                    declaredPossible=model.DeclaredPossible.ToString(),aliases=identities,registered=route is not null,
                    routeKind=route?.Kind.ToString(),restoreStrategy=route?.RestoreStrategy.ToString(),
                    inheritedEcAllowedForBaseShape=route is {Kind:FanRouteKind.GenericDuty,RestoreStrategy:FanRestoreStrategy.GenericEcControl},
                    protocolCounterexamples=route is null?"NOT_ENABLED_NO_RUNTIME_CLAIM":"wrong-kind/wrong-restore/vendor-override refused before write",
                    positiveChecks=registry.Where(pair=>ReferenceEquals(pair.Value,route)).Select(pair=>"ec-protocol:bind-honors-route:"+pair.Key).ToArray(),
                    scope="actual catalog/resolver/Bind/getter with inert fixture types; no OEM constructor or hardware"});
            }
            Require(registered.Count==catalogRows.Count(row=>registered.ContainsKey((string)row.GetType().GetProperty("DeviceId")!.GetValue(row)!)),
                "ec-catalog:all-registrations-accounted-for");
            Trace.Add(new {stage="catalog-inherited-ec-protocol",rows=catalogRows,hardwareAccess=false});
            var sourceRows=new List<object>();
            foreach(var source in FanCapabilityCatalog.PinnedSourceDeclarations)
            {
                int terminalOpens=0;
                var host=new RealHcBackend(HostOptions.Parse(new[]{"--fan-provider","hc"}));
                host.InstallPinnedSourceConsumerForSelfTest(source.Manufacturers[0],source.Products[0],
                    ()=>{terminalOpens++;throw new InvalidOperationException("protocol lookup must not open a terminal");});
                try
                {
                    var route=host.Route!;
                    Require(host.SourceControlMode&&route.RestoreStrategy==FanRestoreStrategy.PinnedSourceAutomatic,
                        "ec-source:actual-host-keeps-source-protocol:"+source.DeviceId);
                    var target=new HandheldCompanion.Devices.InheritedEcRouteFixtureDevice();Configure(target);
                    Require(Bind(target,route) is null&&getter.Invoke(host,new object[]{target}) is null,
                        "ec-source:inherited-shape-cannot-intercept-source-route:"+source.DeviceId);
                    Require(terminalOpens==0&&target.EcSetterCalls==0&&target.BaseProfileCalls==0,
                        "ec-source:admission-is-readonly:"+source.DeviceId);
                    sourceRows.Add(new {source.DeviceId,source.ModelClass,routeKind=route.Kind.ToString(),
                        restoreStrategy=route.RestoreStrategy.ToString(),transport=source.Transport.ToString(),
                        inheritedEcAllowed=false,terminalOpens,positive="actual Host source route selected",
                        negative="inherited base shape cannot intercept source route",physicalAcceptance=false});
                }
                finally{host.Dispose();}
            }
            Trace.Add(new {stage="source-catalog-inherited-ec-exclusion",rows=sourceRows,hardwareAccess=false});
            var vendor=new HandheldCompanion.Devices.ProfileRouteFixtureDevice();Configure(vendor);
            vendor.EcTransportForSelfTest=(_,_)=>true;
            var vendorHost=new RealHcBackend(HostOptions.Parse(new[]{"--fan-provider","hc"}));
            vendorHost.InstallSelfTestRouteSessionForSelfTest(vendor,generic,true);
            typeof(RealHcBackend).GetField("assembly",BindingFlags.Instance|BindingFlags.NonPublic)!.SetValue(vendorHost,typeof(FanCapabilityRoundtrip).Assembly);
            try
            {
                vendorHost.ApplyPowerProfileForSelfTest(Profile(Original,"Software"));
                vendorHost.ApplyPowerProfileForSelfTest(Profile(Original,"Hardware"));
                Require(vendor.CallbackCalls==2&&vendor.EcSetterCalls==0,
                    "ec-protocol:vendor-profile-override-must-not-be-intercepted");
            }
            finally { vendorHost.InstallHcBoundaryForSelfTest(new RealHcBackend.SelfTestHcBoundary());vendorHost.Dispose(); }
            var ec=new HandheldCompanion.Devices.InheritedEcRouteFixtureDevice();Configure(ec);
            var writes=new List<(ushort register,byte value)>();var accepted=true;
            ec.EcTransportForSelfTest=(register,value)=>{writes.Add((register,value));return accepted;};
            var writer=Bind(ec,generic)!;
            var control=writerType.GetMethod("SetControl",BindingFlags.Instance|BindingFlags.NonPublic)!;
            var duty=writerType.GetMethod("SetDuty",BindingFlags.Instance|BindingFlags.NonPublic)!;
            Func<bool> current=()=>true;
            control.Invoke(writer,new object[]{true,current});
            foreach(var value in new[]{0d,12.5d,50d,100d})duty.Invoke(writer,new object[]{value,current});
            control.Invoke(writer,new object[]{false,current});
            Require(writes.SequenceEqual(new[]{((ushort)0x100,(byte)1),((ushort)0x101,(byte)0),
                ((ushort)0x101,(byte)30),((ushort)0x101,(byte)122),((ushort)0x101,(byte)244),((ushort)0x100,(byte)0)}),
                "ec-protocol:original-hc-packets-scaling-and-midpoint-rounding-retained");
            accepted=false;var refused=false;
            try { duty.Invoke(writer,new object[]{50d,current}); }
            catch(TargetInvocationException error) { refused=error.InnerException is FanApiException failure&&failure.Code=="FAN_CURVE_WRITE_FAILED"; }
            Require(refused&&ec.BaseProfileCalls==0,"ec-protocol:false-transport-is-refused-without-callback-fallback");
            accepted=true;var count=writes.Count;var retired=false;
            try { duty.Invoke(writer,new object[]{50d,(Func<bool>)(()=>false)}); }
            catch(TargetInvocationException error) { retired=error.InnerException is FanApiException failure&&failure.Code=="FAN_WRITE_SUPERSEDED"; }
            Require(retired&&writes.Count==count,"ec-protocol:retired-owner-does-not-write");
            var token=HandheldCompanion.Devices.IDevice.ExistingTransportForSelfTest;
            HandheldCompanion.Devices.IDevice.ExistingTransportForSelfTest=new object();var foreign=false;
            try { duty.Invoke(writer,new object[]{50d,current}); }
            catch(TargetInvocationException error) { foreign=error.InnerException?.Message.Contains("existing owned EC transport is not ready")==true; }
            finally { HandheldCompanion.Devices.IDevice.ExistingTransportForSelfTest=token; }
            Require(foreign&&writes.Count==count,"ec-protocol:foreign-transport-does-not-write");
            ec.ECDetails.AddressFanDuty++;var drift=false;
            try { duty.Invoke(writer,new object[]{50d,current}); }
            catch(TargetInvocationException error) { drift=error.InnerException?.Message.Contains("EC fan data changed before write")==true; }
            Require(drift&&writes.Count==count,"ec-protocol:changed-register-metadata-does-not-write");
        }
        finally { HandheldCompanion.Devices.IDevice.ExistingTransportForSelfTest=savedTransport; }
    }

    private sealed record FixtureCase(string Group, string Name, Action Run)
    {
        internal string Identity => Group + "/" + Name;
    }

    public static int Main(string[] args)
    {
        const string canonicalId = "fan.unified-capability-roundtrip";
        var actualCases = new List<string>();
        var caseResults = new List<object>();
        var expectedCases = Array.Empty<string>();
        var selectedGroups = Array.Empty<string>();
        var requestedSelection = Array.Empty<string>();
        string? resultPath = null;
        bool directed = false, selectionVerified = false;
        var binaryStatus = "not-run-source-only";
        var shippingOneXStatus = "not-run-source-only";
        var elapsed = System.Diagnostics.Stopwatch.StartNew();
        void Receipt(string status, string? error = null)
        {
            if (resultPath is null) return;
            var result = new { status, passed = status == "passed", canonicalId,
                selection = directed ? "explicit" : "default", requestedSelection, selectedGroups,
                expectedCases, actualCases, caseResults, selectionVerified,
                checks = Checks.Count, checkNames = Checks, elapsedMs = elapsed.Elapsed.TotalMilliseconds,
                binaryStatus, shippingOneXStatus,
                softwareScope = "actual Host source with injected terminals", physicalAcceptance = false,
                trace = Trace, hardwareAccess = "none; transport injected", package = "no handheld package", error };
            File.WriteAllText(resultPath, JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
        }
        try
        {
            if (args.Length < 2 || string.IsNullOrWhiteSpace(args[0]))
                throw new ArgumentException("Expected result path and HC DLL path");
            resultPath = Path.GetFullPath(args[0]);
            var directory = Path.GetDirectoryName(resultPath)!;
            Directory.CreateDirectory(directory);
            if (args.Length > 2 && args[2] is not ("normal" or "probe-throws"))
                throw new ArgumentException("Unknown close probe selection");
            directed = args.Length > 4;
            if (directed && (args[4] != "--case-filter" || args.Length == 5))
                throw new ArgumentException("Expected --case-filter followed by nonempty, unique original groups or case identities");
            if (directed) { binaryStatus = "not-run-selected-scope"; shippingOneXStatus = "not-run-selected-scope"; }
            var cases = new List<FixtureCase>
            {
                new("legacy", "initial-default-guard", () => RunOtherLegacyInitialDefaultGuard(directory)),
                new("legacy", "onex-initial-handoff", RunLegacyOneXInitialHandoffGuard),
                new("legacy", "lenovo-profile-protocol", RunLenovoProfileProtocolSelection),
                new("legacy", "inherited-ec-protocol", RunInheritedEcProtocolContract),
                new("provider", "backend-provider-and-references", () => {
                    Checks.AddRange(FanRouteEvidenceSelfTest.RunFanBackendProviderSelfTest());
                    var backendReferences = typeof(FanCapabilityCatalog).Assembly.GetReferencedAssemblies();
                    Require(!backendReferences.Any(x => x.Name!.Contains("HandheldCompanion", StringComparison.OrdinalIgnoreCase)),
                        "compiled-capability-backend-has-no-hc-assembly-reference");
                    Require(!backendReferences.Any(x => x.Name is "PresentationFramework" or "PresentationCore" or "WindowsBase" or "Microsoft.Windows.SDK.NET"),
                        "compiled-capability-backend-has-no-wpf-or-windows-sdk-reference");
                }),
                new("pinned-source", "source-terminals-curve-and-owners", () => PinnedFanSourceChecks.Run(Require)),
                new("pinned-host", "source-host-and-outer-lifecycle", () => PinnedFanHostConsumerChecks.Run(Require,directory))
            };
            cases.AddRange(PinnedUmaFanChecks.Cases(Require,directory)
                .Select(item => new FixtureCase("pinned-uma",item.Name,item.Run)));
            cases.AddRange(new FixtureCase[] {
                new("asus", "real-host-consumer", RunRealHostConsumer),
                new("asus", "failure-cases", RunFailureCases),
                new("asus", "stale-proof", RunStaleProofCase),
                new("asus", "two-hosts", RunTwoHosts)
            });
#if HC_BINARY_ADAPTER_READY
            cases.Add(new("hc-binary", "locked-dll-roundtrip", () => {
                RunHcBinaryRoundtrip(args[1]); binaryStatus = "passed-actual-locked-dll";
                shippingOneXStatus = "not-run-no-explicit-shipping-input";
            }));
            if (args.Length > 3 && !string.IsNullOrWhiteSpace(args[3]))
            cases.Add(new("hc-binary", "shipping-onex-contracts", () => {
                var shipping = HcBinaryAdapter.RunShippingOneXContracts(args[3]);
                var json = JsonSerializer.SerializeToElement(shipping);
                foreach (var check in json.GetProperty("checks").EnumerateArray()) Require(true, check.GetString()!);
                Trace.Add(new { stage = "actual-shipping-onex-contracts", receipt = shipping });
                shippingOneXStatus = "passed-actual-shipping-one-x-contract";
            }));
#endif
            cases.AddRange(new FixtureCase[] {
                new("hc-close", "unknown-close-last", () => RunUnknownHcCloseLast(args.Length > 2 && args[2] == "probe-throws")),
                new("default-hc", "rogally-boundary", RunDefaultHcRogallyBoundary),
                new("default-hc", "queued-source", RunDefaultHcQueuedConsumerSourceCases),
                new("default-hc", "queued-async", RunDefaultHcQueuedConsumerAsyncCases)
            });
            if (cases.Select(item => item.Identity).Distinct(StringComparer.Ordinal).Count() != cases.Count)
                throw new InvalidOperationException("Duplicate declared case identity");
            var knownGroups = cases.Select(item => item.Group).Distinct(StringComparer.Ordinal).ToArray();
            requestedSelection = directed ? args.Skip(5).ToArray() : knownGroups;
            if (requestedSelection.Length == 0 || requestedSelection.Any(string.IsNullOrWhiteSpace) ||
                requestedSelection.Distinct(StringComparer.Ordinal).Count() != requestedSelection.Length)
                throw new ArgumentException("Empty or duplicate case selection");
            // Expand a group or an exact identity from the same registry. Unknown
            // names and overlapping expansions fail before any consumer executes.
            var selected = requestedSelection.SelectMany(selection => {
                var matches = cases.Where(item => item.Group == selection || item.Identity == selection).ToArray();
                if (matches.Length == 0) throw new ArgumentException("Unknown case/group: "+selection);
                return matches;
            }).ToArray();
            if (selected.Select(item => item.Identity).Distinct(StringComparer.Ordinal).Count() != selected.Length)
                throw new ArgumentException("Overlapping case/group selections would execute a case twice");
            selectedGroups = selected.Select(item => item.Group).Distinct(StringComparer.Ordinal).ToArray();
            // Freeze the exact expected identities before the first consumer runs.
            // Explicit selection and default execution share this one registry.
            expectedCases = selected.Select(item => item.Identity).ToArray();
            Receipt("running");
#if !HC_BINARY_ADAPTER_READY
            Require(!AppDomain.CurrentDomain.GetAssemblies().Any(x => x.GetName().Name == "HandheldCompanion"),
                "source-only-no-hc-assembly-loaded-before");
#endif
            foreach (var item in selected)
            {
                int firstCheck = Checks.Count;
                var clock = System.Diagnostics.Stopwatch.StartNew();
                try
                {
                    item.Run();
                    if (Checks.Count == firstCheck) throw new InvalidOperationException("Case executed no assertions: "+item.Identity);
                    actualCases.Add(item.Identity);
                    caseResults.Add(new { identity=item.Identity, group=item.Group, name=item.Name,
                        status="passed", checks=Checks.Count-firstCheck, elapsedMs=clock.Elapsed.TotalMilliseconds });
                }
                catch (Exception error)
                {
                    actualCases.Add(item.Identity);
                    caseResults.Add(new { identity=item.Identity, group=item.Group, name=item.Name,
                        status="failed", checks=Checks.Count-firstCheck, elapsedMs=clock.Elapsed.TotalMilliseconds, error=error.ToString() });
                    throw;
                }
            }
#if !HC_BINARY_ADAPTER_READY
            Require(!AppDomain.CurrentDomain.GetAssemblies().Any(x => x.GetName().Name == "HandheldCompanion"),
                "source-only-no-hc-assembly-loaded-after");
#endif
            selectionVerified = expectedCases.SequenceEqual(actualCases,StringComparer.Ordinal) &&
                actualCases.Distinct(StringComparer.Ordinal).Count() == actualCases.Count;
            if (!selectionVerified) throw new InvalidOperationException("Actual case identities differ from the predeclared selection");
            Receipt("passed");
            Console.WriteLine(JsonSerializer.Serialize(new { status="passed", checks=Checks.Count, binaryStatus,
                selectedGroups, actualCases, selectionVerified }));
            return 0;
        }
        catch (Exception ex)
        {
            Receipt("failed",ex.ToString());
            Console.Error.WriteLine(ex);
            return 1;
        }
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

namespace HandheldCompanion.Devices
{
    /** Fixture-only canonical type name; never loaded into the product assembly. */
    internal sealed class ROGAlly
    {
        public bool IsOpen { get; set; } = true;
        public bool IsSupported => true;
        public bool UseOpenLib => false;
        public void Close() { }
        public void Dispose() { }
    }
}


namespace HandheldCompanion
{
    internal enum UpdateSource { Background }
}
namespace HandheldCompanion.Devices
{
    internal struct ProfileFixtureEcDetails
    {
        public ushort AddressStatusCommandPort, AddressDataPort, AddressFanControl, AddressFanDuty;
        public short FanValueMin, FanValueMax;
        public ProfileFixtureEcDetails()
        { AddressStatusCommandPort=0;AddressDataPort=0;AddressFanControl=0;AddressFanDuty=0;FanValueMin=0;FanValueMax=100; }
    }
    internal abstract class IDevice
    {
        private static object? openLibSys;
        // Fixture identity token only; no driver or device is created.
        public static object? ExistingTransportForSelfTest { get => openLibSys; set => openLibSys = value; }
        public ProfileFixtureEcDetails ECDetails = new();
        public bool UseOpenLib = false;
        public bool IsOpen { get; set; } = true;
        public bool IsSupported => true;
        public int EcSetterCalls;
        public virtual void SetFanControl(bool enable, int mode = 0)
        { EcSetterCalls++;throw new InvalidOperationException("fixture EC setter must not run"); }
        public virtual void SetFanDuty(double percent)
        { EcSetterCalls++;throw new InvalidOperationException("fixture EC setter must not run"); }
        public Func<ushort, byte, bool>? EcTransportForSelfTest;
        public int BaseProfileCalls;
        public bool ECRamDirectWriteByte(ushort register, ProfileFixtureEcDetails details, byte value)
        { EcSetterCalls++; return EcTransportForSelfTest?.Invoke(register,value)
            ?? throw new InvalidOperationException("fixture EC transport must not run"); }
        public virtual void PowerProfileManager_Applied(object profile, HandheldCompanion.UpdateSource source)
        { BaseProfileCalls++;throw new InvalidOperationException("fixture base callback must not run"); }
        public void Close() => IsOpen = false;
        public void Dispose() { }
    }
    internal sealed class InheritedEcRouteFixtureDevice : IDevice { }
    internal sealed class ProfileRouteFixtureDevice : IDevice
    {
        public readonly List<object> Profiles = new();
        public Exception? CallbackFailure;
        public int CallbackCalls;
        public override void PowerProfileManager_Applied(object profile, HandheldCompanion.UpdateSource source)
        {
            CallbackCalls++;
            if (CallbackFailure is not null) throw CallbackFailure;
            Profiles.Add(profile);
        }
    }
}
