using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;
using Nefarius.Utilities.DeviceManagement.Extensions;
using Nefarius.Utilities.DeviceManagement.PnP;

// The caller proves that this exact root is the builtin controller using USB
// topology or its exact MSI family. This helper never discovers a controller or
// guesses a slot. It only revalidates the supplied root/location and cycles once.
internal static class ExternalControllerPortCycle
{
    internal const string Command = "--external-controller-priority-cycle";

    // Same exit codes as MsiNativePortCycle: invalid request, lost owner, unknown
    // board, location mismatch, I/O failure, and missing prepared device.
    private const int InvalidRequest = 2, OwnerGone = 3, UnknownBoard = 4,
        LocationChanged = 5, OperationFailed = 6, NoDevice = 7;
    private const uint BufferSmall = 0x1A, StringList = 0x2012;
    private static readonly PropertyKey LocationKey = new()
    {
        Format = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"), Id = 37
    };

    [StructLayout(LayoutKind.Sequential)]
    private struct PropertyKey { public Guid Format; public uint Id; }

    // Model evidence comes from native/main.cpp::ymccResolveFamily and the
    // corresponding local HC Devices/IDevice.cs classes. Manufacturer alone is
    // never authority. Keep actual model codes, including the complete GPD list.
    private static readonly string[] AsusModels = ["RC71L", "RC72LA", "RC73XA", "RC73YA"];
    private static readonly string[] MsiModels = ["MS-1T41", "MS-1T42", "MS-1T52", "MS-1T8K", "MS-1T91"];
    private static readonly string[] GpdModels =
        ["WIN2", "G1618-03", "G1618-04", "G1618-05", "G1617-01", "G1617-02", "G1619-03", "G1619-04", "G1619-05"];
    private static readonly string[] LenovoModels = ["83E1", "83N0", "83N1", "83L3", "83N6", "83Q2", "83Q3"];
    private static readonly string[] AyaModels =
        ["NEXT LITE", "NEXT", "KUN", "AS01", "SLIDE", "FLIP 1S DS", "FLIP 1S", "FLIP DS", "FLIP",
         "GEEK 1S", "AYANEO 2S", "AYA NEO FOUNDER", "AYANEO 2021", "GEEK", "AYANEO 2", "AB05-INTEL",
         "AB05-MENDOCINO", "AB05", "AIR 1S", "AIR PRO", "AIR LITE", "AIR"];
    private static readonly string[] OneXModels =
        ["X1 MINI", "X1PRO", "X1 I", "X1", "APEX", "G1 I", "G1", "F1PRO", "F1", "MINI A07",
         "MINI PRO", "ONE XPLAYER", "2 PRO", "2 ARP23", "ONEXPLAYER X2", "ONEXPLAYER X2MINI PRO",
         "ONEXPLAYER 3", "ONEXPLAYER-3"];
    private static readonly string[] AynModels = ["LOKI MINIPRO", "LOKI ZERO", "LOKI MAX"];
    private static readonly string[] AokzoeModels = ["AOKZOE A1 AR07", "AOKZOE A1 PRO", "AOKZOE A1X", "AOKZOE A2 PRO"];
    private static readonly string[] VirtualMarkers = ["VIGEM", "HIDMAESTRO", "VIRTUAL", "VJOY", "VHID", "USBIP", "ROOT", "SWD"];

    internal static bool ValidRoot(string? root)
    {
        // USB\VID_xxxx&PID_xxxx\instance, with exactly two backslashes.
        if (root is null || root.Length is < 23 or >= 512 ||
            !root.StartsWith(@"USB\VID_", StringComparison.OrdinalIgnoreCase) ||
            !root.AsSpan(12, 5).Equals("&PID_", StringComparison.OrdinalIgnoreCase) ||
            root[21] != '\\' || root.IndexOf('\\', 22) >= 0) return false;
        for (int i = 8; i < 12; i++) if (!char.IsAsciiHexDigit(root[i])) return false;
        for (int i = 17; i < 21; i++) if (!char.IsAsciiHexDigit(root[i])) return false;
        if (root.Any(c => !char.IsAsciiLetterOrDigit(c) && c is not ('\\' or '&' or '_' or '-' or '{' or '}')) ||
            root.Contains("&MI_", StringComparison.OrdinalIgnoreCase)) return false;
        return !VirtualMarkers.Any(marker => root.Contains(marker, StringComparison.OrdinalIgnoreCase));
    }

    internal static bool ValidLocation(string? location) => location is not null &&
        location.Length is >= 4 and <= 4096 && location.Contains("USB(", StringComparison.OrdinalIgnoreCase) &&
        location.All(c => char.IsAsciiLetterOrDigit(c) || c is '#' or '(' or ')' or '_' or '&' or '-' or '.');

    internal static bool TryArguments(string[]? args, out string root, out string location, out int parent)
    {
        root = location = "";
        parent = 0;
        if (args is null || args.Length != 7 || args[0] != Command || args[2] != "--location" ||
            args[4] != "--parent-pid" || args[6] != "--once" || !ValidRoot(args[1]) || !ValidLocation(args[3]) ||
            !int.TryParse(args[5], NumberStyles.None, CultureInfo.InvariantCulture, out var pid) ||
            pid <= 0 || pid == Environment.ProcessId) return false;
        root = args[1]; location = args[3]; parent = pid;
        return true;
    }

    internal static int Run(string[] args)
    {
        if (!TryArguments(args, out var root, out var location, out var parent)) return InvalidRequest;
        try
        {
            using var lifetime = ParentProcessLifetime.TryOpen(parent);
            if (lifetime is null) return OwnerGone;
            // Capture this original process handle; never reopen by reusable PID.
            return ExecuteVerified(root, location, () => lifetime.IsAlive, CurrentBoardAllowed,
                CurrentLocations, PrepareDeviceCycle);
        }
        catch { return OperationFailed; }
    }

    private static bool ModelCharacter(char c) => char.IsAsciiLetterOrDigit(c) || c is '-' or '_';

    private static bool HasModel(string value, string[] models)
    {
        foreach (var model in models)
        {
            int start = 0;
            while (start < value.Length)
            {
                int index = value.IndexOf(model, start, StringComparison.OrdinalIgnoreCase);
                if (index < 0) break;
                int end = index + model.Length;
                if ((index == 0 || !ModelCharacter(value[index - 1])) &&
                    (end == value.Length || !ModelCharacter(value[end]))) return true;
                start = end;
            }
        }
        return false;
    }

    internal static bool IsKnownHandheld(string? manufacturer, string? board, string? systemProduct = null)
    {
        // Firmware sometimes includes a trailing NUL; match native DMI cleanup.
        string m = (manufacturer ?? "").Replace("\0", "").Trim();
        string b = (board ?? "").Replace("\0", "").Trim();
        string s = (systemProduct ?? "").Replace("\0", "").Trim();
        if (m.Equals("ASUSTEK COMPUTER INC.", StringComparison.OrdinalIgnoreCase)) return HasModel(b, AsusModels);
        if (m.Contains("MICRO-STAR", StringComparison.OrdinalIgnoreCase)) return HasModel(b, MsiModels);
        if (m.Contains("GPD", StringComparison.OrdinalIgnoreCase)) return HasModel(b, GpdModels);
        if (m.Contains("LENOVO", StringComparison.OrdinalIgnoreCase))
        {
            // Lenovo uses SystemProductName, including the ten-character MTM.
            return HasModel(s, LenovoModels) || (s.Length == 10 && s.All(char.IsAsciiLetterOrDigit) &&
                LenovoModels.Any(model => s.StartsWith(model, StringComparison.OrdinalIgnoreCase)));
        }
        if (m.Contains("VALVE", StringComparison.OrdinalIgnoreCase))
            return new[] { "JUPITER", "GALILEO" }.Contains(b, StringComparer.OrdinalIgnoreCase) ||
                new[] { "JUPITER", "GALILEO" }.Contains(s, StringComparer.OrdinalIgnoreCase);
        if (m.Contains("AYANEO", StringComparison.OrdinalIgnoreCase) || m.Contains("AYADEVICE", StringComparison.OrdinalIgnoreCase))
            return HasModel(b, AyaModels);
        if (m.Contains("ONE-NETBOOK", StringComparison.OrdinalIgnoreCase)) return HasModel(b, OneXModels);
        if (m.Contains("AYN", StringComparison.OrdinalIgnoreCase)) return HasModel(b, AynModels);
        if (m.Contains("AOKZOE", StringComparison.OrdinalIgnoreCase)) return HasModel(b, AokzoeModels);
        if (m.Contains("MYSTEN", StringComparison.OrdinalIgnoreCase)) return b.Equals("SUIPLAY0X1", StringComparison.OrdinalIgnoreCase);
        // Native resolves this precise board code without a manufacturer gate.
        return b.Equals("G0A1W", StringComparison.OrdinalIgnoreCase);
    }

    private static bool CurrentBoardAllowed()
    {
        using var bios = Registry.LocalMachine.OpenSubKey(@"HARDWARE\DESCRIPTION\System\BIOS", writable: false);
        return IsKnownHandheld(bios?.GetValue("BaseBoardManufacturer") as string,
            bios?.GetValue("BaseBoardProduct") as string, bios?.GetValue("SystemProductName") as string);
    }

    // Tests run this production control flow with every I/O delegate injected.
    // Each post-operation owner check also gates the next blocking operation.
    // No retry, session, virtual target, installation, service, or mode write.
    internal static int ExecuteVerified(string root, string location, Func<bool> alive, Func<bool> knownBoard,
        Func<string, string[]> locations, Func<string, Action?> prepare)
    {
        if (!ValidRoot(root) || !ValidLocation(location)) return InvalidRequest;
        try
        {
            if (!alive()) return OwnerGone;
            bool allowed = knownBoard();
            if (!alive()) return OwnerGone;
            if (!allowed) return UnknownBoard;

            var before = locations(root);
            if (!alive()) return OwnerGone;
            if (!before.Contains(location, StringComparer.OrdinalIgnoreCase)) return LocationChanged;

            var cycle = prepare(root);
            if (!alive()) return OwnerGone;
            if (cycle is null) return NoDevice;

            var after = locations(root);
            if (!alive()) return OwnerGone;
            if (!after.Contains(location, StringComparer.OrdinalIgnoreCase)) return LocationChanged;

            cycle(); // Nefarius void API: completion is not proof of input order.
            if (!alive()) return OwnerGone;
            return 0;
        }
        catch
        {
            // Recheck the original owner even when a blocking operation throws.
            try { return alive() ? OperationFailed : OwnerGone; }
            catch { return OperationFailed; }
        }
    }

    private static Action? PrepareDeviceCycle(string root)
    {
        var device = PnPDevice.GetDeviceByInstanceId(root, DeviceLocationFlags.Normal);
        if (device is null || !string.Equals(device.InstanceId, root, StringComparison.OrdinalIgnoreCase)) return null;
        var usb = device.ToUsbPnPDevice();
        // A conversion must retain the exact supplied USB root, never its hub.
        if (usb is null || !string.Equals(usb.InstanceId, root, StringComparison.OrdinalIgnoreCase)) return null;
        return () => usb.CyclePort();
    }

    private static string[] CurrentLocations(string root)
    {
        uint node = 0;
        if (Locate(ref node, root, 0) != 0) return [];
        var key = LocationKey;
        uint bytes = 0;
        if (Property(node, ref key, out _, null, ref bytes, 0) != BufferSmall ||
            bytes is < 4 or > 65536 || (bytes & 1) != 0) return [];
        var data = new byte[bytes];
        if (Property(node, ref key, out var type, data, ref bytes, 0) != 0 || type != StringList ||
            bytes < 4 || bytes > data.Length || (bytes & 1) != 0 ||
            data[bytes - 1] != 0 || data[bytes - 2] != 0 || data[bytes - 3] != 0 || data[bytes - 4] != 0) return [];
        return Encoding.Unicode.GetString(data, 0, (int)bytes).Split('\0', StringSplitOptions.RemoveEmptyEntries);
    }

    internal static bool SelfTest()
    {
        var failures = new List<string>();
        int cases = 0;
        void Check(string name, bool result) { cases++; if (!result) failures.Add(name); }
        const string root = @"USB\VID_0DB0&PID_1901\00006F64096B22E7";
        const string location = "PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)";
        foreach (var valid in new[] { root, root.ToLowerInvariant(), @"USB\VID_045E&PID_028E\6&123ABCD&0&4", @"USB\VID_0B05&PID_1ABE\X_{123-456}" })
            Check("valid root: " + valid, ValidRoot(valid));
        string?[] invalidRoots = [null, "", @"USB\VID_0DB0&PID_1901\", @"HID\VID_0DB0&PID_1901\X",
            @"BTH\VID_0DB0&PID_1901\X", @"ROOT\VID_0DB0&PID_1901\X", @"SWD\VID_0DB0&PID_1901\X",
            @"USB\VID_0DB0&PID_1901&MI_00\X", @"USB\VID_0DB0&PID_1901&IG_00\X", root + @"\CHILD",
            @"USB\VID_0DG0&PID_1901\X", @"USB\VID_0DB0&PID_19G1\X", @"USB\VID_0DB&PID_1901\X",
            @"USB\VID_0DB00&PID_1901\X", root + "/X", root + "'", root + "\" --once", root + " ",
            root + "\n", root + "\r", root + "\t", root + "\0", root + "\u007f", root + "\u0085", root + "\u00e9",
            @"USB\VID_0DB0&PID_1901\X&MI_00", @"USB\VID_0DB0&PID_1901\" + new string('X', 490)];
        foreach (var invalid in invalidRoots) Check("invalid USB root", !ValidRoot(invalid));
        foreach (var marker in VirtualMarkers) Check("virtual marker: " + marker, !ValidRoot(@"USB\VID_045E&PID_028E\" + marker));

        foreach (var model in AsusModels) Check("ASUS " + model, IsKnownHandheld("ASUSTeK COMPUTER INC.", model));
        foreach (var model in MsiModels) Check("MSI " + model, IsKnownHandheld("Micro-Star International Co., Ltd.", model));
        foreach (var model in GpdModels) Check("GPD " + model, IsKnownHandheld("GPD", model));
        foreach (var model in LenovoModels) Check("Lenovo " + model, IsKnownHandheld("LENOVO", "LNVNB161216", model));
        foreach (var model in AyaModels) Check("AYA " + model, IsKnownHandheld("AYANEO", model));
        foreach (var model in OneXModels) Check("OneX " + model, IsKnownHandheld("ONE-NETBOOK", model));
        foreach (var model in AynModels) Check("AYN " + model, IsKnownHandheld("AYN", model));
        foreach (var model in AokzoeModels) Check("AOKZOE " + model, IsKnownHandheld("AOKZOE", model));
        Check("Lenovo MTM", IsKnownHandheld("LENOVO", "UNKNOWN", "83E10006US"));
        Check("Valve board", IsKnownHandheld("VALVE", "Jupiter"));
        Check("Valve system model", IsKnownHandheld("VALVE", "UNKNOWN", "Galileo"));
        Check("Zotac board", IsKnownHandheld("ZOTAC", "G0A1W"));
        Check("SuiPlay board", IsKnownHandheld("MYSTEN", "SUIPLAY0X1"));
        Check("DMI case/whitespace/NUL", IsKnownHandheld(" ASUSTeK COMPUTER INC.\0 ", " ROG Ally rc71l\0 "));
        Check("model token boundary", !IsKnownHandheld("GPD", "G1618-040"));
        Check("model prefix boundary", !IsKnownHandheld("ASUSTeK COMPUTER INC.", "NOTRC71L"));
        Check("unlisted variant", !IsKnownHandheld("Micro-Star International Co., Ltd.", "MS-1T91-UNKNOWN"));
        Check("foreign manufacturer", !IsKnownHandheld("ASUSTeK COMPUTER INC.", "MS-1T91"));
        Check("ASUS exact manufacturer", !IsKnownHandheld("ASUS", "RC71L"));
        Check("null identity", !IsKnownHandheld(null, null));
        foreach (var brand in new[] { "ASUSTeK COMPUTER INC.", "Micro-Star International Co., Ltd.", "GPD", "LENOVO", "VALVE", "AYANEO", "AYADEVICE", "ONE-NETBOOK", "AYN", "AOKZOE", "MYSTEN", "ZOTAC" })
        {
            Check("brand only: " + brand, !IsKnownHandheld(brand, "UNKNOWN", "UNKNOWN"));
            Check("empty board: " + brand, !IsKnownHandheld(brand, "", ""));
        }

        string pid = (Environment.ProcessId == 1 ? 2 : 1).ToString(CultureInfo.InvariantCulture);
        string[] request = [Command, root, "--location", location, "--parent-pid", pid, "--once"];
        Check("command constant", Command == "--external-controller-priority-cycle");
        Check("parse request", TryArguments(request, out var parsed, out var position, out var parent) &&
            parsed == root && position == location && parent.ToString(CultureInfo.InvariantCulture) == pid);
        Check("missing once", !TryArguments(request[..6], out _, out _, out _));
        Check("extra arguments", !TryArguments([.. request, "--once"], out _, out _, out _));
        Check("null arguments", !TryArguments(null, out _, out _, out _));
        foreach (var badPid in new[] { "0", "-1", "+1", " 1", "1 ", "2147483648", "1.0", Environment.ProcessId.ToString(CultureInfo.InvariantCulture) })
        {
            var bad = (string[])request.Clone(); bad[5] = badPid;
            Check("invalid parent PID", !TryArguments(bad, out _, out _, out _));
        }
        foreach (int index in new[] { 0, 2, 4, 6 })
        {
            var bad = (string[])request.Clone(); bad[index] = "--invalid";
            Check("strict argument order", !TryArguments(bad, out _, out _, out _));
        }
        foreach (var invalid in new[] { "", "PCIROOT(0)", location + "\n", location + "\"", location + "'", location + "\u007f", location + "\u00e9", new string('X', 4097) })
        {
            var bad = (string[])request.Clone(); bad[3] = invalid;
            Check("invalid location", !TryArguments(bad, out _, out _, out _));
        }
        Check("Run invalid request is inert", Run([]) == InvalidRequest);

        // Simulate loss/exception in every blocking stage and at every owner
        // gate. Assertions include operation order and hard once-only counts.
        void Scenario(string name, int expected, int boards, int reads, int preparations, int cycles,
            string? loseAt = null, string? throwAt = null, int driftAt = 0, bool allowed = true,
            bool missing = false, int loseAtGate = 0, string suppliedRoot = root, string suppliedLocation = location)
        {
            bool owner = true, exactRoot = true;
            int b = 0, l = 0, p = 0, c = 0, gates = 0;
            var events = new List<string>();
            bool Alive() { gates++; events.Add("owner"); if (gates == loseAtGate) owner = false; return owner; }
            void Stage(string stage)
            {
                events.Add(stage);
                if (loseAt == stage) owner = false;
                if (throwAt == stage) throw new InvalidOperationException("fixture");
            }
            bool Board() { b++; Stage("board"); return allowed; }
            string[] Locations(string id)
            {
                exactRoot &= id == suppliedRoot; l++; Stage("location" + l);
                return l == driftAt ? [location + "#USB(9)"] : ["PCIROOT(9)#USBROOT(0)#USB(9)", location.ToLowerInvariant()];
            }
            Action? Prepare(string id)
            {
                exactRoot &= id == suppliedRoot; p++; Stage("prepare");
                return missing ? null : () => { c++; Stage("cycle"); };
            }
            int result = ExecuteVerified(suppliedRoot, suppliedLocation, Alive, Board, Locations, Prepare);
            Check(name + " result", result == expected);
            Check(name + " exact counts", b == boards && l == reads && p == preparations && c == cycles && exactRoot);
            Check(name + " bounded once", b <= 1 && l <= 2 && p <= 1 && c <= 1 && gates <= 6);
            bool bracketed = true;
            for (int i = 0; i < events.Count; i++)
                if (events[i] != "owner") bracketed &= i > 0 && i + 1 < events.Count &&
                    events[i - 1] == "owner" && events[i + 1] == "owner";
            Check(name + " owner before and after I/O", bracketed);
            if (name == "normal") Check("normal operation order", string.Join(",", events) ==
                "owner,board,owner,location1,owner,prepare,owner,location2,owner,cycle,owner");
        }
        Scenario("normal", 0, 1, 2, 1, 1);
        Scenario("unknown board", UnknownBoard, 1, 0, 0, 0, allowed: false);
        Scenario("no device", NoDevice, 1, 1, 1, 0, missing: true);
        Scenario("initial location drift", LocationChanged, 1, 1, 0, 0, driftAt: 1);
        Scenario("location drift after prepare", LocationChanged, 1, 2, 1, 0, driftAt: 2);
        Scenario("owner dies with unknown board", OwnerGone, 1, 0, 0, 0, loseAt: "board", allowed: false);
        Scenario("owner dies with absent device", OwnerGone, 1, 1, 1, 0, loseAt: "prepare", missing: true);
        Scenario("owner dies with location drift", OwnerGone, 1, 2, 1, 0, loseAt: "location2", driftAt: 2);
        (string stage, int boards, int reads, int prepares, int cycles)[] stages =
            [("board", 1, 0, 0, 0), ("location1", 1, 1, 0, 0), ("prepare", 1, 1, 1, 0),
             ("location2", 1, 2, 1, 0), ("cycle", 1, 2, 1, 1)];
        foreach (var stage in stages)
        {
            Scenario("owner dies in " + stage.stage, OwnerGone, stage.boards, stage.reads, stage.prepares, stage.cycles, loseAt: stage.stage);
            Scenario("exception in " + stage.stage, OperationFailed, stage.boards, stage.reads, stage.prepares, stage.cycles, throwAt: stage.stage);
            Scenario("owner dies plus exception in " + stage.stage, OwnerGone, stage.boards, stage.reads, stage.prepares, stage.cycles,
                loseAt: stage.stage, throwAt: stage.stage);
        }
        for (int gate = 1; gate <= 6; gate++)
            Scenario("owner gate " + gate, OwnerGone, gate >= 2 ? 1 : 0, gate >= 5 ? 2 : gate >= 3 ? 1 : 0,
                gate >= 4 ? 1 : 0, gate >= 6 ? 1 : 0, loseAtGate: gate);
        foreach (var invalid in invalidRoots)
            Scenario("invalid root is inert", InvalidRequest, 0, 0, 0, 0, suppliedRoot: invalid!);
        Scenario("invalid location is inert", InvalidRequest, 0, 0, 0, 0, suppliedLocation: "PCIROOT(0)");
        Check("parent check exception", ExecuteVerified(root, location, () => throw new InvalidOperationException("fixture"),
            () => true, _ => [location], _ => () => { }) == OperationFailed);
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
        {
            cases, failures = failures.Count, failedCases = failures, realHardwareAccess = false,
            noTargetCreation = true, maxLocationReads = 2, maxPreparations = 1, maxCycles = 1
        }));
        return failures.Count == 0;
    }

    [DllImport("cfgmgr32.dll", EntryPoint = "CM_Locate_DevNodeW", CharSet = CharSet.Unicode)]
    private static extern uint Locate(ref uint node, string instance, uint flags);
    [DllImport("cfgmgr32.dll", EntryPoint = "CM_Get_DevNode_PropertyW")]
    private static extern uint Property(uint node, ref PropertyKey key, out uint type, byte[]? buffer, ref uint bytes, uint flags);
}
