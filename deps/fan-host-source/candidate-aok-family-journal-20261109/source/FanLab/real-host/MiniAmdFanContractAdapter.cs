// Fixed MiniAMD constructor-data backport. The existing HC Mini writer remains ACPI C4/C5.
using System.Reflection;
namespace YeManFanHost;
internal static class MiniAmdFanContractAdapter
{
    internal const string Factory = "HandheldCompanion.Devices.OneXPlayerMiniAMD";
    private const string Writer = "HandheldCompanion.Devices.OneXPlayerMini";
    private const string DeviceBase = "HandheldCompanion.Devices.IDevice";
    private const string EcBase = "HandheldCompanion.Devices.OneXAOKZOE";
    private static readonly string[] Fields = ["AddressFanControl", "AddressFanDuty", "AddressStatusCommandPort", "AddressDataPort", "FanValueMin", "FanValueMax"];
    internal static readonly HcEcFanContract Expected = new(Factory, true, 0x44A, 0x44B, 0x4E, 0x4F, 0, 100, Writer, Writer);
    internal static bool IsFactory(string? factory) => factory == Factory;
    internal static bool Matches(HcEcFanContract? observed) => observed == Expected;
    private static bool CanNormalize(HcEcFanContract? observed) => observed is not null
        && (observed == Expected || observed == (Expected with { Max = 255 }));
    // This is a metadata boundary, never authority to construct or open a device.
    internal static FieldInfo? CompletionField(Type type)
    {
        if (type.FullName != Factory || type.Assembly.GetName().Name != "HandheldCompanion"
            || type.Module != type.Assembly.ManifestModule
            || type.Module.ModuleVersionId != G1FanContractAdapter.ShippingHcMvid) return null;
        var field = type.GetField("ECDetails", BindingFlags.Public | BindingFlags.Instance);
        if (field is null || field.IsStatic || field.IsInitOnly || !field.FieldType.IsValueType
            || field.DeclaringType?.FullName != DeviceBase || field.DeclaringType.Assembly != type.Assembly || field.DeclaringType.Module != type.Module
            || field.FieldType.FullName != "HandheldCompanion.Devices.ECDetails"
            || field.FieldType.Assembly != type.Assembly || field.FieldType.Module != type.Module) return null;
        bool Owned(MethodInfo? method, Type result, string owner) => method is not null
            && !method.IsStatic && !method.IsAbstract && !method.IsGenericMethod
            && method.ReturnType == result && method.DeclaringType?.FullName == owner
            && method.DeclaringType.Assembly == type.Assembly && method.Module == type.Module;
        if (!Owned(type.GetMethod("SetFanControl", [typeof(bool), typeof(int)]), typeof(void), Writer)
            || !Owned(type.GetMethod("SetFanDuty", [typeof(double)]), typeof(void), Writer)
            || !Owned(type.GetProperty("IsOpen")?.GetMethod, typeof(bool), DeviceBase)) return null;
        var members = field.FieldType.GetFields(BindingFlags.Public | BindingFlags.Instance);
        if (members.Length != Fields.Length) return null;
        for (var i = 0; i < Fields.Length; i++)
            if (members.SingleOrDefault(f => f.Name == Fields[i]) is not { IsInitOnly: false, IsStatic: false } member
                || member.FieldType != (i < 4 ? typeof(ushort) : typeof(short))) return null;
        foreach (var name in new[] { "ACPI_FanMode_Address", "ACPI_FanPWMDutyCycle_Address" })
        {
            var address = type.GetField(name, BindingFlags.NonPublic | BindingFlags.Instance);
            if (address?.DeclaringType?.FullName != EcBase || address.FieldType != typeof(byte)
                || address.DeclaringType.Assembly != type.Assembly || address.Module != type.Module) return null;
        }
        return field;
    }
    // Called by the original STA handshake/write-admission path before Open/profile.
    // A failed identity, conflicting data or unknown readback closes MiniAMD admission.
    internal static bool TryNormalize(object device, bool? declared, HcEcFanContract? observed,
        out HcEcFanContract? completed)
    {
        completed = null;
        if (declared != true || !CanNormalize(observed)) return false;
        var type = device.GetType(); var field = CompletionField(type);
        if (field is null) return false;
        object? original = null; object? staged = null; bool assigned = false;
        try
        {
            original = field.GetValue(device); staged = field.GetValue(device);
            if (original is null || staged is null) return false;
            int[] actual = Fields.Select(name => Convert.ToInt32(field.FieldType.GetField(name)!.GetValue(original))).ToArray();
            int[] claimed = [observed!.ControlAddress, observed.DutyAddress, observed.CommandPort, observed.DataPort, observed.Min, observed.Max];
            if (!actual.SequenceEqual(claimed)) return false;
            byte Address(string name) => (byte)type.GetField(name, BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(device)!;
            if (Address("ACPI_FanMode_Address") != 0xC4 || Address("ACPI_FanPWMDutyCycle_Address") != 0xC5) return false;
            if (Matches(observed)) { completed = Expected; return true; }
            if (type.GetProperty("IsOpen")!.GetValue(device) is not false) return false;
            field.FieldType.GetField("FanValueMax")!.SetValue(staged, (short)100);
            field.SetValue(device, staged); assigned = true;
            if (Equals(field.GetValue(device), staged)) { completed = Expected; return true; }
        }
        catch (Exception ex) when (ex is ArgumentException or FieldAccessException or TargetException
            or InvalidCastException or OverflowException or InvalidOperationException or NotSupportedException) { }
        if (assigned && original is not null && Equals(field.GetValue(device), staged)) field.SetValue(device, original);
        return false;
    }
}
