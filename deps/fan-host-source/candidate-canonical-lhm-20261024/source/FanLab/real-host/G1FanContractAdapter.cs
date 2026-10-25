// FAN-942 R3: backport only G1's upstream EC constructor data to the pinned HC instance.
// No HC upgrade, device callback, EC I/O, capability-bit change or authorization bypass.
using System.Reflection;

namespace YeManFanHost;
internal static class G1FanContractAdapter
{
    internal const string UpstreamCommit = "9333ba47e16ea9a0dd5d73b38a679cc041fa38f5";
    // PE metadata of the deployed 0C5132A9... HC. This is a cheap loaded-module check;
    // the existing deployment boundary still owns full file hashing/authorization.
    internal static readonly Guid PinnedHcMvid = new("12247d92-9727-45a3-9934-dbfcb914ea90");
    private const string EcWriter = "HandheldCompanion.Devices.OneXAOKZOE";
    private static readonly string[] Fields = ["AddressFanControl", "AddressFanDuty",
        "AddressStatusCommandPort", "AddressDataPort", "FanValueMin", "FanValueMax"];

    internal static HcEcFanContract? Expected(string? factory) => factory switch
    {
        "HandheldCompanion.Devices.OneXPlayerG1AMD" => new(factory, true,
            0x44A, 0x44B, 0x4E, 0x4F, 0, 255, EcWriter, EcWriter),
        "HandheldCompanion.Devices.OneXPlayerG1Intel" => new(factory, true,
            0x44A, 0x44B, 0x4E, 0x4F, 0, 184, EcWriter, EcWriter),
        _ => null
    };

    internal static bool Matches(HcEcFanContract? observed) => observed is not null
        && Expected(observed.FactoryType) is { } expected && observed == expected;
    internal static bool IsEmpty(HcEcFanContract c) => c.ControlAddress == 0 && c.DutyAddress == 0
        && c.CommandPort == 0 && c.DataPort == 0 && c.Min == 0 && c.Max == 0;
    internal static bool CanComplete(HcEcFanContract? c) => c is not null
        && Expected(c.FactoryType) is not null && IsEmpty(c) && c.UseOpenLib
        && c.ControlDeclaringType == EcWriter && c.DutyDeclaringType == EcWriter;

    // Called only after production route resolution and deployment validation, on the
    // backend STA. A partial/conflicting contract is never repaired or overwritten.
    internal static bool TryComplete(object device, bool? declared, HcEcFanContract? observed,
        out HcEcFanContract? completed)
    {
        completed = observed;
        if (declared != true || !CanComplete(observed)) return false;
        var type = device.GetType();
        if (type.FullName != observed!.FactoryType || type.Module.ModuleVersionId != PinnedHcMvid)
            return false;
        var field = type.GetField("ECDetails", BindingFlags.Instance | BindingFlags.Public);
        if (field?.DeclaringType?.FullName != "HandheldCompanion.Devices.IDevice"
            || field.FieldType.FullName != "HandheldCompanion.Devices.ECDetails"
            || field.FieldType.Assembly != type.Assembly
            || type.GetMethod("SetFanControl", [typeof(bool), typeof(int)])?.DeclaringType?.FullName != EcWriter
            || type.GetMethod("SetFanDuty", [typeof(double)])?.DeclaringType?.FullName != EcWriter)
            return false;
        var expected = Expected(observed.FactoryType)!;
        if (!TrySetEmptyDetails(device, field, expected)) return false;
        completed = expected;
        return true;
    }

    // Reflection is restricted to a public six-field value type. Stage a boxed copy,
    // validate EVERY member first, then assign once and read back. No method invoked.
    private static bool TrySetEmptyDetails(object device, FieldInfo? field, HcEcFanContract expected)
    {
        if (field is null || field.IsStatic || field.IsInitOnly || !field.FieldType.IsValueType)
            return false;
        var members = field.FieldType.GetFields(BindingFlags.Public | BindingFlags.Instance);
        if (members.Length != Fields.Length) return false;
        var ordered = Fields.Select(name => members.SingleOrDefault(f => f.Name == name)).ToArray();
        for (var i = 0; i < ordered.Length; i++)
            if (ordered[i] is not { IsInitOnly: false, IsStatic: false } member
                || member.FieldType != (i < 4 ? typeof(ushort) : typeof(short))) return false;
        object? original = null;
        object? staged = null;
        var assigned = false;
        try
        {
            original = field.GetValue(device);
            staged = field.GetValue(device);
            if (original is null || staged is null
                || ordered.Any(f => Convert.ToInt32(f!.GetValue(original)) != 0)) return false;
            int[] values = [expected.ControlAddress, expected.DutyAddress, expected.CommandPort,
                expected.DataPort, expected.Min, expected.Max];
            for (var i = 0; i < ordered.Length; i++)
            {
                object value = i < 4 ? (object)checked((ushort)values[i]) : checked((short)values[i]);
                ordered[i]!.SetValue(staged, value);
            }
            field.SetValue(device, staged);
            assigned = true;
            if (Equals(field.GetValue(device), staged)) return true;
        }
        catch (Exception ex) when (ex is ArgumentException or FieldAccessException or TargetException
            or InvalidCastException or OverflowException or InvalidOperationException or NotSupportedException)
        {
            // Fail closed. Best-effort rollback touches only our exact staged postimage.
        }
        if (assigned && original is not null && Equals(field.GetValue(device), staged))
            field.SetValue(device, original);
        return false;
    }

    internal static IReadOnlyList<string> RunSelfTest()
    {
        var checks = new List<string>();
        void Check(bool ok, string name) { if (!ok) throw new InvalidOperationException(name); checks.Add(name); }
        foreach (var name in new[] { "OneXPlayerG1AMD", "OneXPlayerG1Intel" })
        {
            var factory = "HandheldCompanion.Devices." + name;
            var expected = Expected(factory)!;
            var empty = expected with { ControlAddress = 0, DutyAddress = 0, CommandPort = 0,
                DataPort = 0, Min = 0, Max = 0 };
            Check(CanComplete(empty), "fan942-g1-empty-completable-" + name);
            Check(expected.Max == (name.EndsWith("AMD", StringComparison.Ordinal) ? 255 : 184),
                "fan942-g1-model-range-" + name);
            var probe = new DetailsProbe();
            var field = typeof(DetailsProbe).GetField(nameof(DetailsProbe.ECDetails))!;
            Check(TrySetEmptyDetails(probe, field, expected), "fan942-g1-reflection-complete-" + name);
            var c = probe.ECDetails;
            Check(c.AddressFanControl == 0x44A && c.AddressFanDuty == 0x44B
                && c.AddressStatusCommandPort == 0x4E && c.AddressDataPort == 0x4F
                && c.FanValueMin == 0 && c.FanValueMax == expected.Max,
                "fan942-g1-reflection-readback-" + name);
            var before = probe.ECDetails;
            Check(!TrySetEmptyDetails(probe, field, expected) && probe.ECDetails.Equals(before),
                "fan942-g1-existing-data-not-overwritten-" + name);
            Check(!CanComplete(empty with { DataPort = 0x4F }), "fan942-g1-partial-not-completed-" + name);
            Check(!CanComplete(empty with { UseOpenLib = false }), "fan942-g1-openlib-required-" + name);
            Check(!CanComplete(empty with { ControlDeclaringType = "overridden" }), "fan942-g1-writer-required-" + name);
            Check(!Matches(expected with { Max = expected.Max == 255 ? 184 : 255 }), "fan942-g1-swapped-range-rejected-" + name);
            Check(!Matches(expected with { CommandPort = 0x2E }), "fan942-g1-wrong-port-rejected-" + name);
            Check(!TryComplete(new DetailsProbe(), true, empty, out _), "fan942-g1-foreign-device-module-rejected-" + name);
            Check(!TryComplete(new DetailsProbe(), false, empty, out _), "fan942-g1-undeclared-not-completed-" + name);
            Check(!TryComplete(new DetailsProbe(), null, empty, out _), "fan942-g1-unreadable-capability-not-completed-" + name);
            Check(Convert.ToByte(0.0 * expected.Max / 100) == 0
                && Convert.ToByte(50.0 * expected.Max / 100) == (expected.Max == 255 ? 128 : 92)
                && Convert.ToByte(100.0 * expected.Max / 100) == expected.Max,
                "fan942-g1-inherited-duty-scaling-" + name);
        }
        Check(Expected("HandheldCompanion.Devices.OneXPlayerG1") is null, "fan942-g1-base-not-enabled");
        Check(Expected("HandheldCompanion.Devices.OneXPlayerX1AMD") is null, "fan942-g1-not-family-wide");
        Check(!TrySetEmptyDetails(new MalformedProbe(), typeof(MalformedProbe).GetField("ECDetails"),
            Expected("HandheldCompanion.Devices.OneXPlayerG1AMD")!), "fan942-g1-malformed-fields-rejected");
        var partial = new DetailsProbe { ECDetails = new ProbeDetails { AddressFanControl = 1 } };
        var partialBefore = partial.ECDetails;
        Check(!TrySetEmptyDetails(partial, typeof(DetailsProbe).GetField("ECDetails"),
            Expected("HandheldCompanion.Devices.OneXPlayerG1AMD")!) && partial.ECDetails.Equals(partialBefore),
            "fan942-g1-partial-field-not-overwritten");
        return checks;
    }
    private struct ProbeDetails
    {
        public ushort AddressFanControl, AddressFanDuty, AddressStatusCommandPort, AddressDataPort;
        public short FanValueMin, FanValueMax;
        public ProbeDetails()
        {
            AddressFanControl = AddressFanDuty = AddressStatusCommandPort = AddressDataPort = 0;
            FanValueMin = FanValueMax = 0;
        }
    }
    private sealed class DetailsProbe { public ProbeDetails ECDetails = default; }
    private sealed class MalformedProbe { public int ECDetails = 0; }
}
