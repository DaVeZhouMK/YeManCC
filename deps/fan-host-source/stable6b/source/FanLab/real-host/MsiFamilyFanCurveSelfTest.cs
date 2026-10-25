namespace YeManFanHost;

internal static class MsiFamilyFanCurveSelfTest
{
    private sealed class Fake : IMsiFamilyFanTransport
    {
        public object Gate { get; } = new();
        public List<string> Calls { get; } = [];
        public int[]? CpuAxis { get; set; } = [0, 50, 60, 70, 80, 88];
        public int[]? GpuAxis { get; set; } = [0, 40, 50, 60, 70, 80];
        public string Block2Status { get; set; } = "ok";
        public string ControlStatus { get; set; } = "ok";
        public string FullSpeedStatus { get; set; } = "ok";
        public Action<byte>? AfterBlock { get; set; }
        public int[]? ReadTemperatureAxis(byte block) { Calls.Add($"axis{block}"); return block == 1 ? CpuAxis : GpuAxis; }
        public object? WriteFanBlock(byte block, byte[] table)
        {
            Calls.Add($"table{block}"); AfterBlock?.Invoke(block);
            var status = block == 2 ? Block2Status : "ok";
            return new { Status = status, WriteDisposition = status == "ok" ? "verified" : "written-unverified", ReadbackDuties6 = table[1..7].Select(b => (int)b).ToArray() };
        }
        public object? SetSoftwareControl() { Calls.Add("control-on"); return new { Status = ControlStatus }; }
        public object? SetFullSpeed(bool enable) { Calls.Add(enable ? "fullspeed-on" : "fullspeed-off"); return new { Status = FullSpeedStatus }; }
    }
    private sealed class InheritedPrivateWriter : PrivateWriterBase { }
    private class PrivateWriterBase { private void WriteMsiFanTableBlock(byte block, byte[] table) { } }

    private class LiveMetadataBase
    {
        private int fieldValue = 1;
        internal int Value { get => fieldValue; set => fieldValue = value; }
        internal object? Receipt { get; set; }
        internal bool ThrowGetter { get; set; }
        private int CurrentValue => ThrowGetter ? throw new InvalidOperationException("live-getter-failure") : fieldValue;
        private object? CurrentReceipt => Receipt;
        private int Read(int offset) => fieldValue + offset;
        private int Read() => fieldValue;
    }
    private sealed class LiveMetadataDevice : LiveMetadataBase { }
    private sealed class HiddenMetadataDevice : LiveMetadataBase { public int CurrentValue = 91; }

    internal static IReadOnlyList<string> Run()
    {
        var checks = new List<string>();
        void Check(bool condition, string name)
        {
            if (!condition) throw new InvalidOperationException("fan952: " + name);
            checks.Add("fan952-" + name);
        }
        var firstDevice = new LiveMetadataDevice();
        var secondDevice = new LiveMetadataDevice { Value = 37 };
        Check((int?)ReflectionMsiFanTransport.Member(firstDevice, "fieldValue") == 1, "metadata-inherited-private-field");
        firstDevice.Value = 9;
        Check((int?)ReflectionMsiFanTransport.Member(firstDevice, "fieldValue") == 9, "metadata-field-value-remains-live");
        Check((int?)ReflectionMsiFanTransport.Member(firstDevice, "CurrentValue") == 9, "metadata-inherited-private-property");
        Check((int?)ReflectionMsiFanTransport.Member(secondDevice, "CurrentValue") == 37, "metadata-cache-does-not-retain-prior-device");
        firstDevice.Value = 13;
        Check((int?)ReflectionMsiFanTransport.Member(firstDevice, "CurrentValue") == 13, "metadata-property-value-remains-live");
        Check(ReflectionMsiFanTransport.Member(firstDevice, "CurrentReceipt") is null, "metadata-null-receipt-remains-null");
        var currentReceipt = new { Status = "ok" };
        firstDevice.Receipt = currentReceipt;
        Check(ReferenceEquals(ReflectionMsiFanTransport.Member(firstDevice, "CurrentReceipt"), currentReceipt), "metadata-new-receipt-after-null-is-read");
        Check(ReflectionMsiFanTransport.Member(null, "CurrentValue") is null && ReflectionMsiFanTransport.Member(firstDevice, "Missing") is null, "metadata-null-target-and-missing-stay-absent");
        Check((int?)ReflectionMsiFanTransport.Member(new HiddenMetadataDevice(), "CurrentValue") == 91, "metadata-derived-member-precedes-private-base");
        var inheritedRead = ReflectionMsiFanTransport.Method(typeof(LiveMetadataDevice), "Read", 1);
        Check(inheritedRead.DeclaringType == typeof(LiveMetadataBase) && (int?)inheritedRead.Invoke(firstDevice, [2]) == 15, "metadata-inherited-method-invokes-live-target");
        Check((int?)ReflectionMsiFanTransport.Method(typeof(LiveMetadataDevice), "Read", 1).Invoke(secondDevice, [2]) == 39, "metadata-method-current-device-after-cache-hit");
        Check((int?)ReflectionMsiFanTransport.Method(typeof(LiveMetadataDevice), "Read", 0).Invoke(firstDevice, []) == 13, "metadata-method-argument-count-is-part-of-key");
        bool missingMethod = false;
        try { ReflectionMsiFanTransport.Method(typeof(LiveMetadataDevice), "Missing", 0); }
        catch (MissingMethodException) { missingMethod = true; }
        Check(missingMethod, "metadata-missing-method-still-throws");
        firstDevice.ThrowGetter = true;
        int getterFailures = 0;
        for (int i = 0; i < 2; i++)
        {
            try { ReflectionMsiFanTransport.Member(firstDevice, "CurrentValue"); }
            catch (System.Reflection.TargetInvocationException ex) when (ex.InnerException is InvalidOperationException) { getterFailures++; }
        }
        firstDevice.ThrowGetter = false;
        Check(getterFailures == 2 && (int?)ReflectionMsiFanTransport.Member(firstDevice, "CurrentValue") == 13, "metadata-getter-errors-and-recovery-remain-live");
        FanNode[] curve = [new(0, 0), new(40, 20), new(70, 45), new(100, 90)];
        var fake = new Fake();
        var result = MsiFamilyFanCurve.Apply(curve, fake, () => true, _ => { });
        Check(result.Success && !result.PhysicalRpmConfirmed, "fan-only-success-is-not-physical-rpm");
        Check(fake.Calls.SequenceEqual(new[] { "axis1", "axis2", "table1", "table2", "control-on", "fullspeed-off" }), "ctw-order-without-shift-or-profile-callback");
        Check(result.Blocks[0].RequestedRawDuties.SequenceEqual(new[] { 0, 42, 55, 67, 90, 108 }), "cpu-live-axis-normalized100-to-raw150:" + string.Join(",", result.Blocks[0].RequestedRawDuties));
        Check(!result.Blocks[0].RequestedRawDuties.SequenceEqual(result.Blocks[1].RequestedRawDuties), "different-cpu-gpu-axes-are-preserved");
        fake = new Fake { CpuAxis = null, GpuAxis = [0, 40, 30, 60, 70, 80] };
        result = MsiFamilyFanCurve.Apply(curve, fake, () => true, _ => { });
        Check(result.Blocks.All(b => b.AxisSource.Contains("axis-unknown") && b.RequestedRawDuties.SequenceEqual(new[] { 0, 15, 42, 55, 90, 112 })), "axis-missing-or-invalid-honest-legacy-compatibility");
        fake = new Fake();
        result = MsiFamilyFanCurve.Apply([new(0, 100), new(40, 100), new(70, 100), new(100, 100)], fake, () => true, _ => { });
        Check(result.Blocks.All(b => b.RequestedRawDuties.All(v => v == 150)), "user100-preserved-as-six-raw150-no-guessed75cap");
        Check(result.FullSpeedRequested && fake.Calls.Last() == "fullspeed-on", "flat100-uses-ctw-firmware-full-blast-not-just-curve-slots");
        fake.Calls.Clear();
        result = MsiFamilyFanCurve.Apply([new(0, 99), new(40, 100), new(70, 100), new(100, 100)], fake, () => true, _ => { });
        Check(!result.FullSpeedRequested && fake.Calls.Last() == "fullspeed-off", "leaving-flat100-clears-fullspeed-before-claiming-curve");
        fake = new Fake { FullSpeedStatus = "unknown" };
        bool full100Rejected = false;
        try { MsiFamilyFanCurve.Apply([new(0,100),new(40,100),new(70,100),new(100,100)], fake, () => true, _ => { }); }
        catch(FanApiException ex) { full100Rejected = ex.Code == "FAN_CURVE_WRITE_FAILED"; }
        Check(full100Rejected && fake.Calls.Contains("fullspeed-on"), "flat100-unverified-override-is-not-success");
        foreach (var status in new[] { "failed", "unknown" })
        {
            fake = new Fake { Block2Status = status };
            MsiFamilyFanSubmission? observed = null;
            bool rejected = false;
            try { MsiFamilyFanCurve.Apply(curve, fake, () => true, r => observed = r); }
            catch (FanApiException ex) { rejected = ex.Code == "FAN_CURVE_WRITE_FAILED"; }
            Check(rejected && observed?.Blocks.Count == 2 && observed.Blocks[0].Status == "ok" && !fake.Calls.Contains("control-on"), "block2-" + status + "-keeps-block1-responsibility-and-no-success");
        }
        foreach (bool controlFailure in new[] { true, false })
        {
            fake = new Fake { ControlStatus = controlFailure ? "unknown" : "ok", FullSpeedStatus = "unknown" };
            bool rejected = false;
            try { MsiFamilyFanCurve.Apply(curve, fake, () => true, _ => { }); }
            catch (FanApiException ex) { rejected = ex.Code == "FAN_CURVE_WRITE_FAILED"; }
            Check(rejected && (!controlFailure || !fake.Calls.Contains("fullspeed-off")), controlFailure ? "control-unknown-is-not-success" : "fullspeed-unknown-is-not-success");
        }
        fake = new Fake { ControlStatus = "skipped-already-target", FullSpeedStatus = "skipped-already-target" };
        Check(MsiFamilyFanCurve.Apply(curve, fake, () => true, _ => { }).Success, "already-target-receipts-accepted");
        bool allowed = true;
        fake = new Fake { AfterBlock = _ => allowed = false };
        bool superseded = false;
        try { MsiFamilyFanCurve.Apply(curve, fake, () => allowed, _ => { }); }
        catch (FanApiException ex) { superseded = ex.Code == "FAN_WRITE_SUPERSEDED"; }
        Check(superseded && fake.Calls.Contains("table1") && !fake.Calls.Contains("table2") && !fake.Calls.Contains("control-on"), "sleep-or-close-fence-stops-next-write");
        Check(MsiFamilyFanCurve.DecodeTemperatureAxis([0, 99, 99, 50, 60, 70, 80, 88])?.SequenceEqual(new[] { 0, 50, 60, 70, 80, 88 }) == true, "ctw-temperature-payload-offsets");
        Check(MsiFamilyFanCurve.DecodeTemperatureAxis([0, 0, 0, 50, 50, 70, 80, 88]) is null && MsiFamilyFanCurve.DecodeTemperatureAxis([0, 0]) is null, "invalid-axis-is-not-live-evidence");
        Check(ReflectionMsiFanTransport.Method(typeof(InheritedPrivateWriter), "WriteMsiFanTableBlock", 2).DeclaringType == typeof(PrivateWriterBase), "derived-family-resolves-base-private-writer");
        foreach (var factory in new[] { "ClawA1M", "ClawA2VM", "ClawBZ2EM", "ClawCG3EM" })
            Check(RealHcBackend.ResolveRouteForSelfTest("HandheldCompanion.Devices.MSI." + factory)?.RestoreStrategy == FanRestoreStrategy.MsiHcDefaultRelease, "family-route-" + factory);
        foreach (var factory in new[] { "ROGAlly", "GPD.GPDWin5", "Lenovo.LegionGo" })
            Check(RealHcBackend.ResolveRouteForSelfTest("HandheldCompanion.Devices." + factory)?.RestoreStrategy != FanRestoreStrategy.MsiHcDefaultRelease, "other-route-unchanged-" + factory);
        using (var backend = new RealHcBackend(HostOptions.Parse(Array.Empty<string>())))
        {
            typeof(RealHcBackend).GetProperty("IsOpen")!.SetValue(backend, true);
            backend.routeOverrideForSelfTest = RealHcBackend.ResolveRouteForSelfTest("HandheldCompanion.Devices.MSI.ClawA2VM");
            typeof(RealHcBackend).GetField("activeCurveDuties", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.SetValue(backend, new double[10]);
            Check(backend.NotifyMsiFirmwareModeChanged(1), "known-msi-mode-edge-accepted-on-live-owner");
            Check(!backend.NotifyMsiFirmwareModeChanged(1) && !backend.NotifyMsiFirmwareModeChanged(0), "duplicate-or-zero-mode-revision-is-read-only");
            Check(backend.NotifyMsiFirmwareModeChanged(2), "next-mode-revision-can-arm-once");
            backend.BlockWritesForSuspend();
            Check(!backend.NotifyMsiFirmwareModeChanged(3), "sleep-gate-refuses-mode-replay-admission");
            var admittedEpoch = backend.SuspendGateEpochForResume;
            Check(backend.TemperatureWritesBlockedForSelfTest, "resume-first-curve-keeps-temperature-callbacks-blocked");
            Check(backend.MsiCurveCommandStillCurrentForSelfTest(admittedEpoch), "resume-first-curve-admitted-before-callback-unblock");
            bool unadmittedRejected = false;
            try { backend.ApplyCurve(curve); }
            catch (FanApiException ex) { unadmittedRejected = ex.Code == "FAN_WRITE_SUPERSEDED"; }
            Check(unadmittedRejected, "unadmitted-takeover-cannot-bypass-sleep-callback-gate");
            typeof(RealHcBackend).GetProperty("IsOpen")!.SetValue(backend, true);
            backend.BlockWritesForSuspend();
            Check(!backend.MsiCurveCommandStillCurrentForSelfTest(admittedEpoch), "new-sleep-invalidates-admitted-msi-command");
            backend.BlockWritesForClose();
            Check(!backend.MsiCurveCommandStillCurrentForSelfTest(backend.SuspendGateEpochForResume), "terminal-close-blocks-even-matching-epoch-command");
            typeof(RealHcBackend).GetProperty("IsOpen")!.SetValue(backend, false);
        }
        using (var notOpen = new RealHcBackend(HostOptions.Parse(Array.Empty<string>())))
            Check(!notOpen.MsiCurveCommandStillCurrentForSelfTest(notOpen.SuspendGateEpochForResume), "msi-command-cannot-replace-hc-open");
        return checks;
    }
}
