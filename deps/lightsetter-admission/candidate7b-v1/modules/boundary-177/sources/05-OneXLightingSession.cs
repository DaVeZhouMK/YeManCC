using System.Collections;
using System.Reflection;
using System.Text.RegularExpressions;

// X1 lighting needs its serial transport, or Mini's already-bound HID. The
// general Open also initializes OLS and changes three OEM-button registers.
// This one-shot helper starts no HC sensor service, so no IMU port reservation
// or general device lifecycle is needed. Only a port opened here is released.
internal sealed class OneXLightingSession : IDisposable
{
    private const string Family = "HandheldCompanion.Devices.OneXPlayerX1";
    private const string Fly = "HandheldCompanion.Devices.OneXPlayerOneXFly";
    private const string FlyPro = "HandheldCompanion.Devices.OneXPlayerOneXFlyF1Pro";
    private const string Mini = "HandheldCompanion.Devices.OneXPlayerX1Mini";
    private const BindingFlags Fields = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
    private readonly object device;
    private readonly FieldInfo? ownedField;
    private object? ownedPort;
    private OneXLightingSession(object device, FieldInfo? field = null, object? port = null)
    { this.device = device; ownedField = field; ownedPort = port; }
    private static Type? Ancestor(Type type, string name)
    { for (Type? current = type; current is not null; current = current.BaseType) if (current.FullName == name) return current; return null; }
    internal static bool IsFamily(Type type) => Ancestor(type, Family) is not null || Ancestor(type, Fly) is not null;
    private static FieldInfo Field(Type type, string name) => type.GetField(name, Fields)
        ?? throw new MissingFieldException(type.FullName, name);
    private static bool Connected(object? port) => port?.GetType().GetProperty("IsOpen")?.GetValue(port) is true;
    private static bool BoundFlyHidReady(object device, Type fly)
    {
        var type = device.GetType();
        var open = type.GetMethod("Open", BindingFlags.Public | BindingFlags.Instance, null, Type.EmptyTypes, null);
        var hid = Field(fly, "hidDevice");
        if (type.FullName is not (Fly or FlyPro) || fly.Assembly != type.Assembly ||
            fly.BaseType?.FullName != "HandheldCompanion.Devices.OneXAOKZOE" ||
            open?.DeclaringType != fly.BaseType || open.ReturnType != typeof(bool) ||
            hid.FieldType.FullName != "HidLibrary.HidDevice")
            throw new InvalidOperationException("HC OneXFly bound HID lighting contract changed");
        var bound = hid.GetValue(device);
        return bound is not null && bound.GetType().GetProperty("IsConnected")?.GetValue(bound) is true;
    }
    internal static bool IsReady(object device)
    {
        var type = device.GetType();
        if (Ancestor(type, Fly) is { } fly) return BoundFlyHidReady(device, fly);
        var family = Ancestor(type, Family);
        if (family is null) return false;
        var mini = Ancestor(type, Mini);
        if (mini is not null)
            return Field(family, "EnableSerialPort").GetValue(device) is false &&
                Field(mini, "hidDevice").GetValue(device) is { } hid &&
                hid.GetType().GetProperty("IsConnected")?.GetValue(hid) is true;
        return Field(family, "EnableSerialPort").GetValue(device) is true && Connected(Field(family, "_serialPort").GetValue(device));
    }
    internal static OneXLightingSession? Prepare(object device)
    {
        var type = device.GetType();
        if (Ancestor(type, Fly) is { } fly)
        {
            if (!BoundFlyHidReady(device, fly)) throw new InvalidOperationException("OneXFly lighting HID unavailable");
            return new(device); // IsReady bound this HID; LED methods own their original writes.
        }
        var family = Ancestor(type, Family);
        if (family is null) return null;
        var open = type.GetMethod("Open", BindingFlags.Public | BindingFlags.Instance, null, Type.EmptyTypes, null);
        if (family.Assembly != type.Assembly || family.BaseType?.FullName != "HandheldCompanion.Devices.OneXAOKZOE" ||
            open?.DeclaringType != family || open.ReturnType != typeof(bool))
            throw new InvalidOperationException("HC OneX lighting Open owner changed");
        var serialField = Field(family, "_serialPort");
        if (serialField.FieldType.FullName != "System.IO.Ports.SerialPort" ||
            Field(family, "_queue").FieldType.FullName != "HandheldCompanion.Misc.Threading.Tasks.SerialQueue")
            throw new InvalidOperationException("HC OneX lighting transport contract changed");
        if (Ancestor(type, Mini) is not null)
        {
            if (!IsReady(device)) throw new InvalidOperationException("OneX Mini lighting HID unavailable");
            return new(device);
        }
        if (Field(family, "EnableSerialPort").GetValue(device) is not true)
            throw new InvalidOperationException("OneX lighting serial transport disabled");
        if (serialField.GetValue(device) is { } existing)
        {
            if (!Connected(existing)) throw new InvalidOperationException("OneX lighting serial transport closed");
            return new(device); // A borrowed transport is never closed by this session.
        }
        var descriptor = Field(family, "SerialPortDeviceName").GetValue(device) as string;
        if (string.IsNullOrWhiteSpace(descriptor)) throw new InvalidOperationException("OneX serial descriptor missing");
        var utils = type.Assembly.GetType("HandheldCompanion.Utils.DeviceUtils", throwOnError: true)!;
        var discover = utils.GetMethod("GetSerialDevices", BindingFlags.Public | BindingFlags.Static, null, Type.EmptyTypes, null)
            ?? throw new MissingMethodException(utils.FullName, "GetSerialDevices");
        var rows = discover.Invoke(null, null) as IEnumerable ?? throw new InvalidOperationException("OneX serial discovery failed");
        var ports = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var row in rows)
        {
            var name = row?.GetType().GetProperty("Name")?.GetValue(row) as string;
            if (name is null || !name.Contains(descriptor, StringComparison.Ordinal)) continue;
            var match = Regex.Match(name, @"(?:^|\W)(COM[1-9]\d*)(?:\W|$)");
            if (match.Success) ports.Add(match.Groups[1].Value);
        }
        if (ports.Count != 1) throw new InvalidOperationException(ports.Count == 0 ? "OneX lighting serial port not found" : "OneX lighting serial port ambiguous");
        var settings = new object?[] { ports.Single(), Field(family,"SerialPortBaudRate").GetValue(device),
            Field(family,"SerialPortParity").GetValue(device), Field(family,"SerialPortDataBits").GetValue(device), Field(family,"SerialPortStopBits").GetValue(device) };
        var serial = Activator.CreateInstance(serialField.FieldType, settings)
            ?? throw new InvalidOperationException("OneX lighting serial construction failed");
        var session = new OneXLightingSession(device, serialField, serial);
        try
        {
            var timeout = serial.GetType().GetProperty("WriteTimeout") ?? throw new MissingMemberException("SerialPort.WriteTimeout");
            timeout.SetValue(serial, 1000);
            serial.GetType().GetMethod("Open", Type.EmptyTypes)!.Invoke(serial, null);
            if (!Connected(serial)) throw new InvalidOperationException("OneX lighting serial Open did not connect");
            serialField.SetValue(device, serial);
            return session;
        }
        catch { session.Dispose(); throw; }
    }
    public void Dispose()
    {
        var serial = ownedPort; ownedPort = null;
        if (serial is null) return;
        try { if (Connected(serial)) serial.GetType().GetMethod("Close", Type.EmptyTypes)!.Invoke(serial, null); }
        finally
        {
            try { ((IDisposable)serial).Dispose(); }
            finally { if (ReferenceEquals(ownedField!.GetValue(device), serial)) ownedField.SetValue(device, null); }
        }
    }
}
