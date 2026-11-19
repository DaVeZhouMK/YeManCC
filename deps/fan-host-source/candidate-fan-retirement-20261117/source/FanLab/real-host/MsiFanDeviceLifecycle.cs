using System.Collections;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.ExceptionServices;

namespace YeManFanHost;

// MSI's virtual Open/Close/OpenEvents also own power and controller settings.
// The fan session owns only the base transport, read-only WMI preparation and
// the two HC fan-ready flags. Keep the original virtual methods for other devices.
internal sealed class MsiFanDeviceLifecycle
{
    private const string BaseName = "HandheldCompanion.Devices.IDevice";
    private const string MsiName = "HandheldCompanion.Devices.ClawA1M";
    private static readonly HashSet<string> Models = new(StringComparer.Ordinal)
    {
        MsiName, "HandheldCompanion.Devices.ClawA2VM",
        "HandheldCompanion.Devices.ClawBZ2EM", "HandheldCompanion.Devices.MSI.ClawCG3EM",
    };
    private readonly object device;
    private readonly FieldInfo deviceOpen, clawOpen, firmware, major, minor, openLib;
    private readonly PropertyInfo isOpen, wmiScope, wmiPath;
    private readonly Func<object, bool> baseOpen;
    private readonly MethodInfo structuredRead, release;
    private object? ownedOpenLib;
    private bool acquiredBase, ownsSession, cleanupPending;
    internal bool HasResources => acquiredBase || ownsSession || ownedOpenLib is not null || cleanupPending;
    internal bool IsOpen => ownsSession && (bool)isOpen.GetValue(device)!;

    internal static bool IsMsiFamily(Type type)
    {
        for (var current = type; current is not null; current = current.BaseType)
            if (current.FullName == MsiName) return true;
        return false;
    }

    internal MsiFanDeviceLifecycle(object target)
    {
        device = target;
        var type = target.GetType();
        if (!Models.Contains(type.FullName ?? "")) throw Contract("unreviewed MSI device type");
        var msi = type.FullName == MsiName ? type : type.BaseType;
        var root = msi?.BaseType;
        if (msi?.FullName != MsiName || root?.FullName != BaseName ||
            root.Assembly != type.Assembly || msi.Assembly != type.Assembly)
            throw Contract("MSI inheritance changed");
        const BindingFlags fields = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        deviceOpen = RequiredField(root, "DeviceOpen", typeof(bool), fields);
        clawOpen = RequiredField(msi, "ClawOpen", typeof(bool), fields);
        firmware = RequiredField(msi, "Firmware", typeof(int), fields);
        major = RequiredField(msi, "WmiMajorVersion", typeof(int), fields);
        minor = RequiredField(msi, "WmiMinorVersion", typeof(int), fields);
        openLib = root.GetField("openLibSys", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.DeclaredOnly)
            ?? throw Contract("base OpenLib ownership field missing");
        isOpen = msi.GetProperty("IsOpen", BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly)
            ?? throw Contract("MSI IsOpen missing");
        wmiScope = msi.GetProperty("WmiScope", fields) ?? throw Contract("WMI scope missing");
        wmiPath = msi.GetProperty("WmiPath", fields) ?? throw Contract("WMI path missing");
        release = msi.GetMethod("ReleaseMsiFanControl", [typeof(string)]) ?? throw Contract("fan release missing");
        var wmi = type.Assembly.GetType("HandheldCompanion.WMI") ?? throw Contract("WMI reader missing");
        structuredRead = wmi.GetMethod("GetStructured", BindingFlags.Static | BindingFlags.Public,
            [typeof(string), typeof(string), typeof(string), typeof(byte), typeof(int), typeof(int)])
            ?? throw Contract("structured WMI readiness reader missing");
        var method = root.GetMethod("Open", BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly,
            Type.EmptyTypes) ?? throw Contract("base Open missing");
        if (method.ReturnType != typeof(bool)) throw Contract("base Open signature changed");
        // MethodInfo.Invoke on a virtual base method dispatches back to Claw.Open.
        // An explicit call opcode is necessary to retain ONLY IDevice.Open.
        var call = new DynamicMethod("MsiFanBaseTransportOpen", typeof(bool), [typeof(object)],
            typeof(MsiFanDeviceLifecycle).Module, skipVisibility: true);
        var il = call.GetILGenerator();
        il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Castclass, root);
        il.Emit(OpCodes.Call, method); il.Emit(OpCodes.Ret);
        baseOpen = call.CreateDelegate<Func<object, bool>>();
    }

    internal bool Open(Func<bool> stillCurrent)
    {
        CheckCurrent(stillCurrent);
        if (ownsSession)
        {
            if (!IsOpen) throw Contract("owned fan session lost its HC open markers");
            return true;
        }
        if (HasResources) throw Contract("previous MSI cleanup is still pending");
        if ((bool)deviceOpen.GetValue(device)! || (bool)clawOpen.GetValue(device)!)
            throw Contract("refusing to adopt a foreign or partially open MSI session");
        var previousOpenLib = openLib.GetValue(null);
        try
        {
            var opened = baseOpen(device);
            acquiredBase = (bool)deviceOpen.GetValue(device)!;
            CaptureOwnedOpenLib(previousOpenLib);
            if (!opened) { Cleanup(); return false; }
            CheckCurrent(stillCurrent);
            // Same Get_WMI query as Claw.GetWMI, with an explicit read outcome.
            // A failed/short response must not turn into a fabricated ready flag.
            var result = Invoke(structuredRead, null, wmiScope.GetValue(device), wmiPath.GetValue(device),
                "Get_WMI", (byte)1, 32, 3) ?? throw Contract("WMI readiness result missing");
            var resultType = result.GetType();
            var usable = resultType.GetProperty("IsUsable")?.GetValue(result) as bool? == true;
            var bytes = resultType.GetProperty("Data")?.GetValue(result) as byte[];
            if (!usable || bytes is null || bytes.Length < 3) throw Contract("WMI readiness read failed or truncated");
            CheckCurrent(stillCurrent);
            ObserveBoundFirmware();
            if (bytes[1] >= 2) { major.SetValue(device, (int)bytes[1]); minor.SetValue(device, (int)bytes[2]); }
            // No Shift/UEFI/TDP/M12/ROM/mode writes, watcher, QuerySettings or HID Open.
            clawOpen.SetValue(device, true);
            ownsSession = true;
            CheckCurrent(stillCurrent);
            if (!IsOpen) throw Contract("MSI fan-ready markers were not established");
            return true;
        }
        catch
        {
            acquiredBase |= (bool)deviceOpen.GetValue(device)!;
            CaptureOwnedOpenLib(previousOpenLib);
            Cleanup();
            throw;
        }
    }

    internal void EnsureEventsReady(Func<bool> stillCurrent)
    {
        CheckCurrent(stillCurrent);
        if (!IsOpen) throw Contract("MSI fan Open must complete before fan event subscription");
        // Host subscribes its own generation-bound power-profile events. HC's
        // general OpenEvents dispatches QuerySettings/Device_Inserted and is not fan-only.
    }

    internal void Close()
    {
        Exception? releaseError = null;
        try
        {
            if (ownsSession) Invoke(release, device, "hc-close");
        }
        catch (Exception error) { releaseError = error; }
        try { Cleanup(); }
        catch (Exception cleanupError)
        {
            if (releaseError is not null) throw new AggregateException(releaseError, cleanupError);
            throw;
        }
        if (releaseError is not null) ExceptionDispatchInfo.Capture(releaseError).Throw();
    }

    private void CaptureOwnedOpenLib(object? previous)
    {
        var current = openLib.GetValue(null);
        if (current is not null && !ReferenceEquals(current, previous)) ownedOpenLib = current;
    }

    private void Cleanup()
    {
        cleanupPending = true;
        // Leave a failed Dispose owned and retryable; never clear its reference
        // or report a completed close after an unconfirmed resource cleanup.
        if (ownedOpenLib is not null)
        {
            if (ownedOpenLib is IDisposable disposable) disposable.Dispose();
            else
            {
                var dispose = ownedOpenLib.GetType().GetMethod("Dispose", Type.EmptyTypes)
                    ?? throw Contract("owned OpenLib disposal contract missing");
                Invoke(dispose, ownedOpenLib);
            }
            if (ReferenceEquals(openLib.GetValue(null), ownedOpenLib)) openLib.SetValue(null, null);
            ownedOpenLib = null;
        }
        clawOpen.SetValue(device, false);
        deviceOpen.SetValue(device, false);
        ownsSession = false; acquiredBase = false; cleanupPending = false;
    }

    private void ObserveBoundFirmware()
    {
        FieldInfo? hid = null;
        for (var type = device.GetType(); type is not null && hid is null; type = type.BaseType)
            hid = type.GetField("hidDevices", BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.DeclaredOnly);
        if (hid?.GetValue(device) is not IDictionary handles || !handles.Contains(1)) return;
        var bound = handles[1];
        var attributes = bound?.GetType().GetProperty("Attributes")?.GetValue(bound);
        if (attributes?.GetType().GetProperty("Version")?.GetValue(attributes) is int value)
            firmware.SetValue(device, value);
    }

    private static FieldInfo RequiredField(Type type, string name, Type expected, BindingFlags flags)
    {
        var field = type.GetField(name, flags) ?? throw Contract(name + " missing");
        if (field.FieldType != expected) throw Contract(name + " changed type");
        return field;
    }
    private static object? Invoke(MethodInfo method, object? target, params object?[] args)
    {
        try { return method.Invoke(target, args); }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); throw; }
    }
    private static void CheckCurrent(Func<bool> current)
    {
        if (!current()) throw new FanApiException(409, "HC_RESUME_SUPERSEDED", "MSI fan initialization was superseded");
    }
    private static InvalidOperationException Contract(string detail) => new("MSI_FAN_OPEN_CONTRACT: " + detail);
}

