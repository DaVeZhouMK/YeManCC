// This file contains only fixtures/fakes and the standalone report entry point.
// Actual PnP methods, records, SP_DEVINFO_DATA and constants are extracted by the
// paired runner from current Program.cs into a separate partial class file.
using System.Text;
using System.Text.Json;
using System.Runtime.InteropServices;
using YeManInputHost.Diagnostics;

internal static partial class ActualPnp
{
    private sealed class FixtureNode(uint devInst, string instanceId, params string[] hardwareIds)
    {
        public uint DevInst { get; } = devInst;
        public string InstanceId { get; } = instanceId;
        public string[] HardwareIds { get; } = hardwareIds;
        public uint? Parent { get; init; }
        public bool InstanceSizeFails { get; init; }
        public bool InstanceReadFails { get; init; }
        public bool HardwareSizeFails { get; init; }
        public bool HardwareReadFails { get; init; }
        public bool ParentFails { get; init; }
        public bool ParentIdReadFails { get; init; }
    }
    private static class Fake
    {
        private static readonly IntPtr Handle = new(0x4242);
        internal static readonly string[] ApiNames = [
            "SetupDiGetClassDevsW", "SetupDiEnumDeviceInfo", "SetupDiGetDeviceInstanceIdW",
            "SetupDiGetDeviceRegistryPropertyW", "SetupDiDestroyDeviceInfoList",
            "CM_Get_Parent", "CM_Get_Device_IDW", "CM_Get_DevNode_Status", "GetLastWin32Error"];
        internal static FixtureNode[] Nodes = [];
        internal static bool OpenFails, Active;
        internal static int EnumError = 259, LastError, SuccessfulEnumCount, HardwareSizeFailureCount, HardwareReadFailureCount, ParentNonSuccessCount, AncestorIdNonSuccessCount;
        internal static Dictionary<string, int> Calls = [];
        internal static bool EnumerationReachedEnd;
        internal static readonly List<string> Trace = [];
        internal static readonly Dictionary<string, int> Totals = ApiNames.ToDictionary(n => n, _ => 0);
        internal static readonly List<object> InjectedFaults = [];
        internal static void Reset(FixtureNode[] nodes, bool openFails, int enumError)
        {
            if (nodes.Length > 16 || nodes.Select(n => n.DevInst).Distinct().Count() != nodes.Length)
                throw new InvalidOperationException("invalid bounded fixture");
            Nodes = nodes; OpenFails = openFails; EnumError = enumError; Active = false; LastError = 0;
            Calls = ApiNames.ToDictionary(n => n, _ => 0); InjectedFaults.Clear(); Trace.Clear();
            SuccessfulEnumCount = HardwareSizeFailureCount = HardwareReadFailureCount = ParentNonSuccessCount = AncestorIdNonSuccessCount = 0; EnumerationReachedEnd = false;
        }
        private static void Call(string name, string arguments = "") { ++Calls[name]; ++Totals[name]; Trace.Add(name + "(" + arguments + ")"); }
        private static void Fault(string api, uint devInst, string phase, int error) =>
            InjectedFaults.Add(new { api, devInst, phase, error });
        private static void Require(bool condition)
        {
            if (!condition) throw new InvalidOperationException("fake-call-contract-violation");
        }
        private static void Set(IntPtr handle) => Require(Active && handle == Handle);
        private static FixtureNode Node(uint devInst) => Nodes.Single(n => n.DevInst == devInst);
        internal static int GetLastWin32Error() { Call(nameof(GetLastWin32Error), LastError.ToString()); return LastError; }
        internal static IntPtr SetupDiGetClassDevsW(IntPtr classGuid, string? enumerator, IntPtr hwndParent, uint flags)
        {
            Call(nameof(SetupDiGetClassDevsW), $"classGuid={classGuid};enumerator={enumerator};hwnd={hwndParent};flags={flags}");
            Require(classGuid == IntPtr.Zero && enumerator is null && hwndParent == IntPtr.Zero && flags == 6);
            if (OpenFails) { LastError = 5; Fault(nameof(SetupDiGetClassDevsW), 0, "open", 5); return new(-1); }
            Active = true; LastError = 0; return Handle;
        }
        internal static bool SetupDiEnumDeviceInfo(IntPtr set, uint index, ref SP_DEVINFO_DATA data)
        {
            Call(nameof(SetupDiEnumDeviceInfo), $"set={set};index={index};cbSize={data.cbSize}"); Set(set);
            Require(data.cbSize == (uint)Marshal.SizeOf<SP_DEVINFO_DATA>());
            if (index >= (uint)Nodes.Length)
            {
                Require(index == (uint)Nodes.Length); LastError = EnumError; EnumerationReachedEnd = EnumError == 259;
                if (EnumError != 259) Fault(nameof(SetupDiEnumDeviceInfo), 0, "enumerate", EnumError);
                return false;
            }
            data.DevInst = Nodes[(int)index].DevInst; ++SuccessfulEnumCount; LastError = 0; return true;
        }
        internal static bool SetupDiGetDeviceInstanceIdW(IntPtr set, ref SP_DEVINFO_DATA data,
            StringBuilder? buffer, int size, out uint required)
        {
            Call(nameof(SetupDiGetDeviceInstanceIdW), $"set={set};node={data.DevInst};phase={(buffer is null ? "size" : "read")};capacity={size}"); Set(set); var node = Node(data.DevInst);
            required = (uint)node.InstanceId.Length + 1;
            if (buffer is null)
            {
                Require(size == 0);
                if (node.InstanceSizeFails) { required = 0; LastError = 5; Fault(nameof(SetupDiGetDeviceInstanceIdW), node.DevInst, "size", 5); }
                else LastError = 122;
                return false;
            }
            Require(size == buffer.Capacity && size >= required);
            if (node.InstanceReadFails) { LastError = 5; Fault(nameof(SetupDiGetDeviceInstanceIdW), node.DevInst, "read", 5); return false; }
            buffer.Append(node.InstanceId); LastError = 0; return true;
        }
        internal static bool SetupDiGetDeviceRegistryPropertyW(IntPtr set, ref SP_DEVINFO_DATA data,
            uint property, out uint type, byte[]? buffer, uint size, out uint required)
        {
            Call(nameof(SetupDiGetDeviceRegistryPropertyW), $"set={set};node={data.DevInst};property={property};phase={(buffer is null ? "size" : "read")};capacity={size}"); Set(set); Require(property == 1);
            var node = Node(data.DevInst); type = 7; // fixture REG_MULTI_SZ
            var bytes = Encoding.Unicode.GetBytes(string.Join('\0', node.HardwareIds) + "\0\0");
            required = (uint)bytes.Length;
            if (buffer is null)
            {
                Require(size == 0);
                if (node.HardwareSizeFails) { ++HardwareSizeFailureCount; required = 0; LastError = 5; Fault(nameof(SetupDiGetDeviceRegistryPropertyW), node.DevInst, "size", 5); }
                else LastError = 122;
                return false;
            }
            Require(size == buffer.Length && size == required);
            if (node.HardwareReadFails) { ++HardwareReadFailureCount; LastError = 13; Fault(nameof(SetupDiGetDeviceRegistryPropertyW), node.DevInst, "read", 13); return false; }
            bytes.CopyTo(buffer, 0); LastError = 0; return true;
        }
        internal static bool SetupDiDestroyDeviceInfoList(IntPtr set)
        {
            Call(nameof(SetupDiDestroyDeviceInfoList), $"set={set}"); Set(set); Active = false; LastError = 0; return true;
        }
        internal static int CM_Get_Parent(out uint parent, uint devInst, uint flags)
        {
            Call(nameof(CM_Get_Parent), $"node={devInst};flags={flags}"); Require(flags == 0); var node = Node(devInst); parent = node.Parent ?? 0;
            if (node.ParentFails) { ++ParentNonSuccessCount; Fault(nameof(CM_Get_Parent), devInst, "parent", 13); return 13; }
            if (!node.Parent.HasValue) ++ParentNonSuccessCount; return node.Parent.HasValue ? 0 : 13;
        }
        internal static int CM_Get_Device_IDW(uint devInst, StringBuilder buffer, int size, uint flags)
        {
            Call(nameof(CM_Get_Device_IDW), $"node={devInst};capacity={size};flags={flags}"); Require(flags == 0 && size == buffer.Capacity);
            var node = Node(devInst);
            if (node.ParentIdReadFails) { ++AncestorIdNonSuccessCount; Fault(nameof(CM_Get_Device_IDW), devInst, "ancestor-id", 13); return 13; }
            Require(size > node.InstanceId.Length); buffer.Append(node.InstanceId); return 0;
        }
        internal static int CM_Get_DevNode_Status(out uint status, out uint problem, uint devInst, uint flags)
        {
            Call(nameof(CM_Get_DevNode_Status), $"node={devInst};flags={flags}"); Require(flags == 0); _ = Node(devInst);
            status = 8; problem = 0; return 0;
        }
    }
    private sealed class Checks
    {
        internal readonly List<object> Items = [];
        internal int Count, Failures;
        internal void That(string check, bool passed)
        {
            Items.Add(new { check, passed }); ++Count; if (!passed) ++Failures;
        }
        internal void Counts(VirtualEndpointObservation o, int primary, int companion, int hid, int steam = 0, int steamHid = 0) =>
            That("actual classified counts match current behavior", o.PrimaryRootCount == primary && o.XboxCompanionRootCount == companion && o.HidChildCount == hid && o.SteamDeckRootCount == steam && o.SteamDeckHidChildCount == steamHid);
    }
    private static readonly List<object> Cases = [];
    private static readonly List<object> Gaps = [];
    private static int CaseCount, CheckCount, FailureCount, ObserveCalls;
    private static object View(VirtualEndpointObservation o) => new {
        o.ProbeSucceeded, o.PrimaryRootCount, o.XboxCompanionRootCount, o.HidChildCount,
        o.SteamDeckRootCount, o.SteamDeckHidChildCount, o.AllStarted, o.IsAbsent, o.IsFullyAbsent, o.Detail,
        o.PresentInstanceIds, o.SteamDeckRootInstanceIds, o.SteamDeckHidChildInstanceIds
    };
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
    private static readonly Dictionary<string, JsonElement> InitialCases = [];
    private static int OnOffFixtureCount, HelperBoundaryCount;
    private sealed record ModeResult(VirtualEndpointObservation Observation, Dictionary<string, int> ApiCalls, string[] ApiTrace, object[] Faults);
    private static Dictionary<string, bool> Admissions(VirtualEndpointObservation o) =>
        new[] { "xbox360", "elite", "dualshock4", "dualsense", "dualsense-edge", "steamdeck" }.ToDictionary(p => p, o.IsUniqueStartedTarget);
    private static bool JsonSame(object? left, object? right) => JsonElement.DeepEquals(JsonSerializer.SerializeToElement(left, JsonOptions), JsonSerializer.SerializeToElement(right, JsonOptions));
    private static void ProofFlags(Checks c, MsiPnpCensusDiagnostic.Receipt? receipt)
    {
        c.That("receipt exists with all exact/classification authority flags false", receipt is not null && !receipt.ClassificationComplete && !receipt.ExactOwnedIdsPinned && !receipt.ExactOwnedAbsenceProven);
    }
    private static ModeResult ObserveMode(bool capture, FixtureNode[] nodes, bool strict, string? persona,
        bool openFails, int enumError, Action<VirtualEndpointObservation, Checks> verify, Checks checks, bool gapExpected)
    {
        Fake.Reset(nodes, openFails, enumError);
        VirtualEndpointObservation observed;
        using (new MsiPnpCensusDiagnostic.Scope(capture))
        {
            ++ObserveCalls; observed = ObserveVirtualEndpoint(persona, strict); verify(observed, checks);
            checks.That("observation scope restores current and preserves requested flag", MsiPnpCensusDiagnostic.Current is null && MsiPnpCensusDiagnostic.Requested == capture);
            checks.That("fake-only device set closed exactly once when opened", !Fake.Active && Fake.Calls["SetupDiDestroyDeviceInfoList"] == (openFails ? 0 : 1));
            checks.That("fault fixture actually exercised", !gapExpected || Fake.InjectedFaults.Count > 0);
            if (capture)
            {
                var receipt = observed.MsiPnpCensus; ProofFlags(checks, receipt);
                if (receipt is not null)
                {
                    checks.That("missing-ID and enumeration coverage facts match actual fake returns",
                        receipt.EnumeratedNodeCount == Fake.SuccessfulEnumCount && receipt.NamedNodeCount == Fake.Calls["CM_Get_DevNode_Status"] &&
                        receipt.MissingInstanceIdCount == Fake.SuccessfulEnumCount - Fake.Calls["CM_Get_DevNode_Status"] &&
                        receipt.EnumerationReachedEnd == Fake.EnumerationReachedEnd && receipt.ExistingProbeSucceeded == observed.ProbeSucceeded);
                    checks.That("hardware unavailable/read/shape counters match actual fake returns",
                        receipt.HardwareIdUnavailableCount == Fake.HardwareSizeFailureCount && receipt.HardwareIdReadFailureCount == Fake.HardwareReadFailureCount && receipt.HardwareIdReadShapeUnknownCount == 0);
                    checks.That("ancestor non-success counters match returns including normal chain ends",
                        receipt.AncestorParentNonSuccessCount == Fake.ParentNonSuccessCount && receipt.AncestorIdNonSuccessCount == Fake.AncestorIdNonSuccessCount && receipt.AncestorDepthLimitCount == 0);
                    checks.That("actual capture includes exactly the classified candidates", receipt.ObservedCandidates.Count ==
                        observed.PrimaryRootCount + observed.XboxCompanionRootCount + observed.HidChildCount + observed.SteamDeckRootCount + observed.SteamDeckHidChildCount);
                }
            }
            else checks.That("disabled observation attaches no census receipt", observed.MsiPnpCensus is null);
        }
        checks.That("request/current state never leaks across observation calls", !MsiPnpCensusDiagnostic.Requested && MsiPnpCensusDiagnostic.Current is null);
        return new(observed, new(Fake.Calls), Fake.Trace.ToArray(), Fake.InjectedFaults.ToArray());
    }
    private static object ModeView(ModeResult? mode) => mode is null ? new { missing = true } : (object)new {
        observation = View(mode.Observation), admission = Admissions(mode.Observation), msiPnpCensus = mode.Observation.MsiPnpCensus,
        fakeApiCalls = mode.ApiCalls, fakeApiTrace = mode.ApiTrace, injectedFaults = mode.Faults
    };
    private static void Run(string name, string behavior, FixtureNode[] nodes, bool strict,
        Action<VirtualEndpointObservation, Checks> verify, string? gap = null, string? persona = null,
        bool openFails = false, int enumError = 259)
    {
        var checks = new Checks(); ModeResult? off = null, on = null; string? unexpected = null;
        try
        {
            off = ObserveMode(false, nodes, strict, persona, openFails, enumError, verify, checks, gap is not null);
            on = ObserveMode(true, nodes, strict, persona, openFails, enumError, verify, checks, gap is not null);
            var baseline = InitialCases[name];
            checks.That("fixture is identical to frozen initial baseline", JsonElement.DeepEquals(JsonSerializer.SerializeToElement(nodes, JsonOptions), baseline.GetProperty("fixture")));
            checks.That("all original observation fields unchanged off/on", JsonSame(View(off.Observation), View(on.Observation)));
            checks.That("original result/count/detail/identity matches frozen baseline", JsonElement.DeepEquals(JsonSerializer.SerializeToElement(View(off.Observation), JsonOptions), baseline.GetProperty("observation")));
            checks.That("all existing persona admission results unchanged off/on", JsonSame(Admissions(off.Observation), Admissions(on.Observation)));
            var deviceApis = Fake.ApiNames.Where(api => api != "GetLastWin32Error").ToArray();
            checks.That("SetupDi/CM counts exactly unchanged off/on", deviceApis.All(api => off.ApiCalls[api] == on.ApiCalls[api]));
            checks.That("SetupDi/CM counts exactly match initial baseline", deviceApis.All(api => off.ApiCalls[api] == baseline.GetProperty("fakeApiCalls").GetProperty(api).GetInt32()));
            checks.That("disabled full API counts match initial baseline", Fake.ApiNames.All(api => off.ApiCalls[api] == baseline.GetProperty("fakeApiCalls").GetProperty(api).GetInt32()));
            checks.That("SetupDi/CM trace and arguments exactly unchanged off/on", off.ApiTrace.Where(t => !t.StartsWith("GetLastWin32Error(", StringComparison.Ordinal)).SequenceEqual(on.ApiTrace.Where(t => !t.StartsWith("GetLastWin32Error(", StringComparison.Ordinal))));
            var receipt = on.Observation.MsiPnpCensus;
            checks.That("only allowed extra last-error reads correspond to hardware failure facts", receipt is not null && on.ApiCalls["GetLastWin32Error"] - off.ApiCalls["GetLastWin32Error"] == receipt.HardwareIdUnavailableCount + receipt.HardwareIdReadFailureCount);
        }
        catch (Exception ex) { unexpected = ex.ToString(); checks.That("unexpected harness exception", false); }
        ++CaseCount; ++OnOffFixtureCount; CheckCount += checks.Count; FailureCount += checks.Failures;
        Cases.Add(new { name, behavior, strict, persona, kind = "original-fixture-off-on-comparison", passed = checks.Failures == 0,
            checks = checks.Items, fixture = nodes, off = ModeView(off), on = ModeView(on), unexpected });
        if (gap is not null && on is not null) Gaps.Add(new { name, gap, strict, observation = View(on.Observation), diagnostic = on.Observation.MsiPnpCensus, faultCount = on.Faults.Length });
    }
    private static FixtureNode Primary(bool idSizeFails = false, bool idReadFails = false,
        bool hwSizeFails = false, bool hwReadFails = false, bool ancestorIdFails = false) =>
        new(1, "ROOT\\HIDMAESTRO\\0000", "ROOT\\HIDMAESTRO") {
            InstanceSizeFails = idSizeFails, InstanceReadFails = idReadFails,
            HardwareSizeFails = hwSizeFails, HardwareReadFails = hwReadFails, ParentIdReadFails = ancestorIdFails
        };
    private static FixtureNode Child(bool parentFails = false) => new(2, "HID\\FAKE_VIRTUAL\\0000", "HID\\FAKE_VIRTUAL") { Parent = 1, ParentFails = parentFails };
    private static void ApparentAbsence(VirtualEndpointObservation o, Checks c)
    {
        c.That("current probe reports success", o.ProbeSucceeded); c.Counts(o, 0, 0, 0);
        c.That("current result reports absent and fully absent", o.IsAbsent && o.IsFullyAbsent);
        c.That("empty classified sets make AllStarted vacuously true", o.AllStarted);
    }
    private static void ProbeRejected(VirtualEndpointObservation o, Checks c)
    {
        c.That("current probe rejects incomplete enumeration", !o.ProbeSucceeded);
        c.That("failed probe never reports absent", !o.IsAbsent && !o.IsFullyAbsent && !o.AllStarted);
        c.Counts(o, 0, 0, 0);
        c.That("actual exception detail retained", o.Detail == "pnp-probe-exception:InvalidOperationException");
    }
    private static void BaselineCases()
    {
        Run("empty-census", "Successful empty enumeration is the clean control.", [], false, ApparentAbsence);
        Run("instance-size-failed-nonstrict", "Unavailable instance ID is silently skipped before status/hardware reads.", [Primary(idSizeFails: true)], false, (o, c) => {
            ApparentAbsence(o, c);
            c.That("skipped node never reaches hardware read", Fake.Calls["SetupDiGetDeviceRegistryPropertyW"] == 0);
        }, "Known fixture root exists but failed instance-ID sizing is reported as successful absence.");
        Run("instance-size-failed-strict", "Strict enumeration rejects an unavailable instance ID.", [Primary(idSizeFails: true)], true, ProbeRejected);
        Run("instance-read-failed-nonstrict", "Instance-ID second read failure is also silently skipped.", [Primary(idReadFails: true)], false, (o, c) => {
            ApparentAbsence(o, c);
            c.That("both instance-ID phases ran", Fake.Calls["SetupDiGetDeviceInstanceIdW"] == 2);
        }, "Known fixture root exists but failed instance-ID read is reported as successful absence.");
        Run("instance-read-failed-strict", "Strict enumeration rejects failed second instance-ID read.", [Primary(idReadFails: true)], true, ProbeRejected);
        Run("hardware-size-failed-nonstrict", "Hardware-ID sizing failure becomes an empty ID list.", [Primary(hwSizeFails: true)], false, (o, c) => {
            ApparentAbsence(o, c);
            c.That("hardware sizing called once without read", Fake.Calls["SetupDiGetDeviceRegistryPropertyW"] == 1);
        }, "Instance is known but hardware-ID failure prevents root classification without failing the probe.");
        Run("hardware-read-failed-strict", "Strict IDs do not make hardware classification complete.", [Primary(hwReadFails: true)], true, (o, c) => {
            ApparentAbsence(o, c);
            c.That("strict snapshot still contains unclassified instance", o.PresentInstanceIds!.SequenceEqual(["ROOT\\HIDMAESTRO\\0000"]));
            c.That("both hardware-ID phases ran", Fake.Calls["SetupDiGetDeviceRegistryPropertyW"] == 2);
        }, "Strict probe records the root instance, yet failed hardware read produces successful apparent absence.");
        Run("hardware-failed-root-with-present-child", "An unclassified root also hides its otherwise-readable HID descendant.", [Primary(hwReadFails: true), Child()], true, (o, c) => {
            ApparentAbsence(o, c);
            c.That("strict snapshot contains both fixture instances", o.PresentInstanceIds!.Count == 2);
            c.That("ancestor device ID was actually read", Fake.Calls["CM_Get_Device_IDW"] > 0);
        }, "Readable HID child cannot match an empty classified-root ID set after a root hardware read failure.");
        Run("ancestor-parent-failed", "CM parent failure truncates ancestry without failing the probe.", [Primary(), Child(parentFails: true)], true, (o, c) => {
            c.That("probe still reports success", o.ProbeSucceeded); c.Counts(o, 1, 0, 0);
            c.That("child exists in strict snapshot but not classified count", o.PresentInstanceIds!.Count == 2 && o.HidChildCount == 0);
            c.That("target is not admitted with hidden HID child", !o.IsUniqueStartedTarget("dualshock4"));
        }, "CM_Get_Parent failure hides a present HID child while ProbeSucceeded remains true.");
        Run("ancestor-device-id-failed", "CM ancestor-ID failure truncates ancestry without failing the probe.", [Primary(ancestorIdFails: true), Child()], true, (o, c) => {
            c.That("probe still reports success", o.ProbeSucceeded); c.Counts(o, 1, 0, 0);
            c.That("child exists in strict snapshot but not classified count", o.PresentInstanceIds!.Count == 2 && o.HidChildCount == 0);
            c.That("CM ancestor-ID failure path exercised", Fake.Calls["CM_Get_Device_IDW"] == 2);
        }, "CM_Get_Device_IDW failure hides a present HID child while ProbeSucceeded remains true.");
        Run("normal-x360", "Complete X360 root/companion/HID census.", [Primary(), new(3, "ROOT\\HIDMAESTROXUSB\\0000", "root\\hidmaestroxusb"), Child()], true, (o, c) => {
            c.That("complete census succeeds and started", o.ProbeSucceeded && o.AllStarted); c.Counts(o, 1, 1, 1);
            c.That("actual X360 admission predicate satisfied", o.IsUniqueStartedTarget("xbox360"));
            c.That("actual absence predicates reject present roots", !o.IsAbsent && !o.IsFullyAbsent);
            c.That("strict IDs retain full enumeration", o.PresentInstanceIds!.Count == 3);
            c.That("actual detail lists root/companion/HID identity and hardware", o.Detail.Contains("pnp-primary-root-ids=ROOT\\HIDMAESTRO\\0000[ROOT\\HIDMAESTRO]") && o.Detail.Contains("pnp-xbox-companion-roots=1") && o.Detail.Contains("pnp-hid-children=1"));
        }, persona: "xbox360");
        Run("normal-sony", "Complete Sony topology and real MULTI_SZ decoding.", [new(1, "ROOT\\HIDMAESTRO\\0000", "root\\hidmaestro", "FAKE\\ALTERNATE"), Child()], true, (o, c) => {
            c.That("complete census succeeds and started", o.ProbeSucceeded && o.AllStarted); c.Counts(o, 1, 0, 1);
            foreach (var persona in new[] { "dualshock4", "dualsense", "dualsense-edge" })
                c.That("actual Sony admission: " + persona, o.IsUniqueStartedTarget(persona));
            c.That("actual Unicode MULTI_SZ split retains both IDs", o.Detail.Contains("[root\\hidmaestro|FAKE\\ALTERNATE]"));
            c.That("present Sony is not absent", !o.IsAbsent && !o.IsFullyAbsent);
        }, persona: "dualshock4");
        Run("normal-steamdeck", "Composite root counted once, MI interface excluded, HID follows two ancestor levels.", [
            new(10, "USB\\VID_28DE&PID_1205\\ROOT", " USB\\VID_28DE&PID_1205&REV_0300 "),
            new(11, "USB\\VID_28DE&PID_1205&MI_00\\IFACE", "USB\\VID_28DE&PID_1205&MI_00") { Parent = 10 },
            new(12, "HID\\VID_28DE&PID_1205\\CHILD", "HID\\VID_28DE&PID_1205") { Parent = 11 }
        ], true, (o, c) => {
            c.That("complete census succeeds and started", o.ProbeSucceeded && o.AllStarted); c.Counts(o, 0, 0, 0, 1, 1);
            c.That("actual SteamDeck admission predicate satisfied", o.IsUniqueStartedTarget("steamdeck"));
            c.That("legacy IsAbsent ignores SD while IsFullyAbsent retains SD", o.IsAbsent && !o.IsFullyAbsent);
            c.That("strict SD root/child snapshots exclude MI root duplicate", o.SteamDeckRootInstanceIds!.Count == 1 && o.SteamDeckHidChildInstanceIds!.Count == 1 && o.PresentInstanceIds!.Count == 3);
            c.That("actual detail lists SD counts", o.Detail.Contains("pnp-steamdeck-roots=1") && o.Detail.Contains("pnp-steamdeck-hid-children=1"));
        }, persona: "steamdeck");
        Run("enumeration-error-after-node", "Non-NO_MORE_ITEMS error rejects the whole partially-read census.", [Primary()], false, (o, c) => {
            ProbeRejected(o, c);
            c.That("partial node read before failing next enumerate", Fake.Calls["SetupDiEnumDeviceInfo"] == 2 && Fake.Calls["SetupDiGetDeviceRegistryPropertyW"] == 2);
        }, enumError: 5);
        Run("open-access-denied", "Failed set open rejects probe and never enumerates/destroys invalid handle.", [], false, (o, c) => {
            ProbeRejected(o, c);
            c.That("no enumerator or destroy reached after open failure", Fake.Calls["SetupDiEnumDeviceInfo"] == 0 && Fake.Calls["SetupDiDestroyDeviceInfoList"] == 0);
            c.That("last error supplied only by fake", Fake.Calls["GetLastWin32Error"] == 1);
        }, openFails: true);
    }
    private static void Boundary(string name, Action<Checks> verify)
    {
        var checks = new Checks(); string? unexpected = null; var callsBefore = new Dictionary<string, int>(Fake.Totals);
        try { verify(checks); }
        catch (Exception ex) { unexpected = ex.ToString(); checks.That("unexpected helper boundary exception", false); }
        checks.That("bounded helper test performs no PnP/fake API calls", Fake.Totals.All(p => callsBefore[p.Key] == p.Value));
        checks.That("thread request/current restored after helper test", !MsiPnpCensusDiagnostic.Requested && MsiPnpCensusDiagnostic.Current is null);
        ++CaseCount; ++HelperBoundaryCount; CheckCount += checks.Count; FailureCount += checks.Failures;
        Cases.Add(new { name, kind = "linked-pure-helper-boundary", passed = checks.Failures == 0, checks = checks.Items, unexpected });
    }
    private static void HelperBoundaries()
    {
        Boundary("bounded-events-and-candidates", c => {
            using var requested = new MsiPnpCensusDiagnostic.Scope(true);
            using var observation = new MsiPnpCensusDiagnostic.ObservationScope(100);
            var d = observation.Capture!;
            for (var i = 0; i < 20; ++i) { d.TryNodeEnumerated(); d.TryMissingInstanceId(); }
            d.TryEnumerationEnd();
            d.TryCandidates("oversized-id", [new string('x', 257)]);
            d.TryCandidates("fixture-candidate", Enumerable.Range(0, 35).Select(i => "FAKE\\" + i));
            var receipt = d.TryComplete(true, 200); ProofFlags(c, receipt);
            c.That("event budget preserves counters while truncating to 16", receipt is not null && receipt.EnumeratedNodeCount == 20 && receipt.MissingInstanceIdCount == 20 && receipt.Issues.Count == 16 && receipt.IssuesTruncated);
            c.That("candidate budget clips oversized identities and caps at 32", receipt is not null && receipt.ObservedCandidates.Count == 32 && receipt.CandidatesTruncated && receipt.ObservedCandidates.All(n => n.InstanceId.Length <= 256));
            c.That("budget loss reports diagnostic incomplete without changing supplied probe status", receipt is not null && !receipt.DiagnosticComplete && receipt.ExistingProbeSucceeded);
            d.TryMissingInstanceId();
            c.That("completed receipt snapshot does not change after more facts", receipt is not null && receipt.MissingInstanceIdCount == 20 && d.TryComplete(true, 201)?.MissingInstanceIdCount == 21);
        });
        Boundary("nested-request-and-observation-restoration", c => {
            using var requested = new MsiPnpCensusDiagnostic.Scope(true);
            using var outer = new MsiPnpCensusDiagnostic.ObservationScope(100);
            var parent = outer.Capture!; parent.TryNamedNode();
            using (new MsiPnpCensusDiagnostic.Scope(false))
            {
                using (var disabled = new MsiPnpCensusDiagnostic.ObservationScope(110))
                    c.That("disabled nested observation hides outer collector and creates none", disabled.Capture is null && MsiPnpCensusDiagnostic.Current is null && !MsiPnpCensusDiagnostic.Requested);
                c.That("nested observation restores outer current", ReferenceEquals(MsiPnpCensusDiagnostic.Current, parent));
            }
            c.That("nested request restores outer authorization", MsiPnpCensusDiagnostic.Requested);
            using (var inner = new MsiPnpCensusDiagnostic.ObservationScope(120))
            {
                c.That("enabled nested collector is distinct", inner.Capture is not null && !ReferenceEquals(inner.Capture, parent) && ReferenceEquals(MsiPnpCensusDiagnostic.Current, inner.Capture));
                inner.Capture!.TryMissingInstanceId(); ProofFlags(c, inner.Capture.TryComplete(true, 121));
            }
            c.That("enabled nested observation restores outer collector", ReferenceEquals(MsiPnpCensusDiagnostic.Current, parent));
            var receipt = parent.TryComplete(true, 130); ProofFlags(c, receipt);
            c.That("nested facts do not leak into outer collector", receipt is not null && receipt.NamedNodeCount == 1 && receipt.MissingInstanceIdCount == 0);
        });
        Boundary("thread-local-disabled-and-no-leak", c => {
            using var requested = new MsiPnpCensusDiagnostic.Scope(true);
            using var parentObservation = new MsiPnpCensusDiagnostic.ObservationScope(100);
            var parent = parentObservation.Capture!;
            bool childStartsDisabled = false, disabledIsEmpty = false, childRestored = false;
            string? childException = null; MsiPnpCensusDiagnostic.Receipt? childReceipt = null;
            var thread = new Thread(() => {
                try {
                    childStartsDisabled = !MsiPnpCensusDiagnostic.Requested && MsiPnpCensusDiagnostic.Current is null;
                    using (var disabled = new MsiPnpCensusDiagnostic.ObservationScope(200))
                        disabledIsEmpty = disabled.Capture is null && MsiPnpCensusDiagnostic.Current is null;
                    using (new MsiPnpCensusDiagnostic.Scope(true))
                    using (var observation = new MsiPnpCensusDiagnostic.ObservationScope(210)) {
                        observation.Capture!.TryNamedNode(); childReceipt = observation.Capture.TryComplete(true, 220);
                    }
                    childRestored = !MsiPnpCensusDiagnostic.Requested && MsiPnpCensusDiagnostic.Current is null;
                } catch (Exception ex) { childException = ex.ToString(); }
            }) { IsBackground = true };
            thread.Start(); var joined = thread.Join(5000);
            c.That("new thread is disabled and never inherits parent collector", joined && childStartsDisabled && disabledIsEmpty);
            c.That("child thread restores its own state", joined && childException is null && childRestored);
            ProofFlags(c, childReceipt);
            c.That("child capture stays independent of parent", childReceipt?.NamedNodeCount == 1 && parent.TryComplete(true, 300)?.NamedNodeCount == 0 && ReferenceEquals(MsiPnpCensusDiagnostic.Current, parent));
        });
        Boundary("checked-counter-overflow-recorded-as-incomplete", c => {
            using var requested = new MsiPnpCensusDiagnostic.Scope(true);
            using var observation = new MsiPnpCensusDiagnostic.ObservationScope(100);
            var d = observation.Capture!;
            // Fixture injection reaches the checked overflow boundary in one call,
            // avoiding billions of iterations. No method or receipt is substituted.
            var counter = typeof(MsiPnpCensusDiagnostic).GetField("_enumerated", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
                ?? throw new InvalidOperationException("actual private overflow fixture field not found");
            counter.SetValue(d, int.MaxValue); d.TryNodeEnumerated(); d.TryEnumerationEnd();
            var receipt = d.TryComplete(true, 200); ProofFlags(c, receipt);
            c.That("checked overflow preserves maximum count and is swallowed", receipt?.EnumeratedNodeCount == int.MaxValue);
            c.That("counter overflow records diagnostic incomplete with probe result retained", receipt is not null && !receipt.DiagnosticComplete && receipt.ExistingProbeSucceeded && receipt.EnumerationReachedEnd);
        });
    }
    private static int Main(string[] args)
    {
        if (args.Length != 1) { Console.Error.WriteLine("usage: msi-pnp-enumeration-actual-selftest report.json"); return 2; }
        try
        {
            using (var initial = JsonDocument.Parse(File.ReadAllText(InitialReportPath)))
                foreach (var fixture in initial.RootElement.GetProperty("cases").EnumerateArray()) InitialCases.Add(fixture.GetProperty("name").GetString()!, fixture.Clone());
            BaselineCases(); HelperBoundaries();
            var report = new {
                testKind = "current-actual-host-pnp-methods-with-linked-census-off-on", passed = FailureCount == 0,
                caseCount = CaseCount, checkCount = CheckCount, failureCount = FailureCount,
                sourceSha256 = SourceSha256, helperSha256 = HelperSha256, extractionSha256 = ExtractionSha256, initialReportSha256 = InitialReportSha256,
                actualObserveCalls = ObserveCalls, fakeApiCallTotals = Fake.Totals, onOffFixtureCount = OnOffFixtureCount, helperBoundaryCount = HelperBoundaryCount,
                structFixtureUsed = false, cases = Cases, currentGaps = Gaps,
                authorityConclusion = "Fixture presence is ground truth only for these fakes. ProbeSucceeded/IsAbsent are current method outputs, not classification completeness or exact-owned absence proof.",
                scope = "Actual extracted census/capture methods/types/constants and linked pure helper; SetupDi/CM/last-error rebound to managed fakes. Original 15 fixtures run off/on, plus four bounded helper tests; real BCL TickCount64/encoding/collections/JSON/Marshal.SizeOf retained. No full Host execution.",
                hostExecuted = false, sdkExecuted = false, nativeDllImports = 0, liveDeviceApiCalls = 0, registryApiCalls = 0, serviceApiCalls = 0
            };
            File.WriteAllText(args[0], JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase }), new UTF8Encoding(false));
            Console.WriteLine($"MSI_PNP_ACTUAL_SELFTEST cases={CaseCount} checks={CheckCount} failures={FailureCount}");
            Console.WriteLine("FAKE_API_CALL_TOTALS " + JsonSerializer.Serialize(Fake.Totals));
            return FailureCount == 0 ? 0 : 1;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 2; }
    }
}