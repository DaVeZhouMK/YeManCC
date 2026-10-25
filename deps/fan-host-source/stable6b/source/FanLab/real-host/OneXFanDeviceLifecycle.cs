using System.Reflection;
using System.Runtime.ExceptionServices;

namespace YeManFanHost;

// X1's general lifecycle also owns LED COM ports, OEM buttons and vendor HID.
// The audited inherited fan writer needs only its EC data and an owned OpenLib.
internal sealed class OneXFanDeviceLifecycle
{
    private const string RootName = "HandheldCompanion.Devices.IDevice";
    private const string ParentName = "HandheldCompanion.Devices.OneXAOKZOE";
    private const string X1Name = "HandheldCompanion.Devices.OneXPlayerX1";
    private static readonly HashSet<string> Models = new(StringComparer.Ordinal)
    {
        "HandheldCompanion.Devices.OneXPlayerX1AMD",
        "HandheldCompanion.Devices.OneXPlayerX1Intel",
        "HandheldCompanion.Devices.OneXPlayerX1Pro",
        "HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex",
    };
    private const BindingFlags InstanceFields = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
    private static readonly object TransportGate = new();
    private readonly object device;
    private readonly FieldInfo deviceOpen, useOpenLib, ecDetails, openLib, serial, vendorHid;
    private readonly Type transportType;
    private readonly MethodInfo initialize, dispose, release;
    private object? ownedTransport;
    private bool ownsMarker, releaseReturned, cleanupPending;

    internal bool HasResources => ownedTransport is not null || ownsMarker || cleanupPending;
    internal bool IsOpen => ownsMarker && (bool)deviceOpen.GetValue(device)! &&
        ownedTransport is not null && ReferenceEquals(openLib.GetValue(null), ownedTransport);

    internal static bool IsFamily(Type type)
    {
        for (var current = type; current is not null; current = current.BaseType)
            if (Models.Contains(current.FullName ?? "")) return true;
        return false;
    }

    internal OneXFanDeviceLifecycle(object target)
    {
        device = target;
        var type = target.GetType();
        if (!Models.Contains(type.FullName ?? "")) throw Contract("unreviewed OneX device type");
        var x1 = type;
        while (x1 is not null && x1.FullName != X1Name) x1 = x1.BaseType;
        var parent = x1?.BaseType;
        var root = parent?.BaseType;
        var expectedBase = type.FullName is "HandheldCompanion.Devices.OneXPlayerX1Pro" or
            "HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex"
            ? "HandheldCompanion.Devices.OneXPlayerX1AMD" : X1Name;
        if (x1 is null || parent?.FullName != ParentName || root?.FullName != RootName ||
            type.BaseType?.FullName != expectedBase || root.Assembly != type.Assembly)
            throw Contract("device hierarchy changed");
        RequireOwner(type, "Open", x1, typeof(bool), Type.EmptyTypes);
        RequireOwner(type, "Close", x1, typeof(void), Type.EmptyTypes);
        RequireOwner(type, "OpenEvents", root, typeof(void), Type.EmptyTypes);
        RequireOwner(type, "SetFanDuty", parent, typeof(void), [typeof(double)]);
        var profileType = type.Assembly.GetType("HandheldCompanion.Misc.PowerProfile") ?? throw Contract("power profile type missing");
        var sourceType = type.Assembly.GetType("HandheldCompanion.UpdateSource") ?? throw Contract("profile source type missing");
        RequireOwner(type, "PowerProfileManager_Applied", root, typeof(void), [profileType, sourceType]);
        RequireOwner(type, "SetFanControl", parent, typeof(void), [typeof(bool), typeof(int)]);
        if (type.GetProperty("IsOpen")?.GetMethod?.DeclaringType != root)
            throw Contract("fan open marker owner changed");
        deviceOpen = RequireField(root, "DeviceOpen", typeof(bool));
        useOpenLib = RequireField(root, "UseOpenLib", typeof(bool));
        if (useOpenLib.GetValue(device) is not true)
            throw Contract("fan EC transport disabled");
        openLib = root.GetField("openLibSys", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.DeclaredOnly)
            ?? throw Contract("OpenLib ownership field missing");
        transportType = type.Assembly.GetType("HandheldCompanion.OpenLibSys") ?? throw Contract("OpenLib type missing");
        if (openLib.FieldType != transportType || transportType.GetConstructor(Type.EmptyTypes) is null)
            throw Contract("OpenLib construction contract changed");
        initialize = transportType.GetMethod("InitializeOls", Type.EmptyTypes) ?? throw Contract("OpenLib initialize missing");
        dispose = transportType.GetMethod("Dispose", Type.EmptyTypes) ?? throw Contract("OpenLib dispose missing");
        if (initialize.ReturnType != typeof(bool) || dispose.ReturnType != typeof(void))
            throw Contract("OpenLib lifecycle signature changed");
        serial = x1.GetField("_serialPort", InstanceFields) ?? throw Contract("LED port field missing");
        vendorHid = x1.GetField("_vendorHidMonitor", InstanceFields) ?? throw Contract("vendor HID field missing");
        ecDetails = root.GetField("ECDetails", InstanceFields) ?? throw Contract("EC details missing");
        var ec = ecDetails.GetValue(device) ?? throw Contract("EC details missing");
        release = RequireOwner(type, "ECRamDirectWriteByte", root, typeof(bool), [typeof(ushort), ecDetails.FieldType, typeof(byte)]);
        int Read(string name) => Convert.ToInt32(ec.GetType().GetField(name)?.GetValue(ec) ?? -1);
        if (Read("AddressFanControl") != 0x44a || Read("AddressFanDuty") != 0x44b ||
            Read("AddressStatusCommandPort") != 0x4e || Read("AddressDataPort") != 0x4f ||
            Read("FanValueMin") != 0 || Read("FanValueMax") != 184)
            throw Contract("inherited EC fan data changed");
    }

    internal bool Open(Func<bool> stillCurrent)
    {
        lock (TransportGate) return OpenCore(stillCurrent);
    }

    private bool OpenCore(Func<bool> stillCurrent)
    {
        CheckCurrent(stillCurrent);
        if (HasResources)
        {
            if (IsOpen && !cleanupPending && !releaseReturned) { EnsureEventsReady(stillCurrent); return true; }
            throw Contract("previous cleanup is pending");
        }
        if ((bool)deviceOpen.GetValue(device)! || openLib.GetValue(null) is not null ||
            serial.GetValue(device) is not null || vendorHid.GetValue(device) is not null)
            throw Contract("refusing a foreign or partially open session");
        try
        {
            // HC's base helper caches non-null even when InitializeOls returns false.
            // Keep this object's ownership local until initialization really succeeds.
            ownedTransport = Activator.CreateInstance(transportType) ?? throw Contract("OpenLib construction returned null");
            var initialized = Invoke(initialize, ownedTransport) is true;
            if (!initialized) { CleanupTransport(); return false; }
            CheckCurrent(stillCurrent);
            if (openLib.GetValue(null) is not null || (bool)deviceOpen.GetValue(device)!)
                throw Contract("transport ownership changed during initialization");
            openLib.SetValue(null, ownedTransport);
            deviceOpen.SetValue(device, true);
            ownsMarker = true;
            releaseReturned = false;
            CheckCurrent(stillCurrent);
            return true;
        }
        catch (Exception failure)
        {
            if (cleanupPending) throw;
            try { CleanupTransport(); }
            catch (Exception cleanup) { throw new AggregateException(failure, cleanup); }
            ExceptionDispatchInfo.Capture(failure).Throw(); throw;
        }
    }

    internal void EnsureEventsReady(Func<bool> stillCurrent)
    {
        lock (TransportGate) EnsureEventsReadyCore(stillCurrent);
    }

    private void EnsureEventsReadyCore(Func<bool> stillCurrent)
    {
        CheckCurrent(stillCurrent);
        if (!IsOpen || cleanupPending || releaseReturned) throw Contract("fan transport is not ready");
        if (serial.GetValue(device) is not null || vendorHid.GetValue(device) is not null)
            throw Contract("foreign device resources appeared during fan ownership");
        // No HC device/keyboard/LED callbacks were subscribed. The Host retains its
        // own generation-bound profile subscription and normal manager observation.
    }

    internal void SetControl(bool enable, Func<bool> stillCurrent)
    {
        lock (TransportGate) WriteFanByte(0x44a, enable ? (byte)1 : (byte)0, stillCurrent);
    }

    internal void SetDuty(double percent, Func<bool> stillCurrent)
    {
        if (!double.IsFinite(percent) || percent < 0 || percent > 100)
            throw new ArgumentOutOfRangeException(nameof(percent), "OneX duty must be finite and between 0 and 100");
        // Exact inherited conversion, including Convert.ToByte's rounding.
        var data = Convert.ToByte(percent * 184 / 100);
        lock (TransportGate) WriteFanByte(0x44b, data, stillCurrent);
    }

    private void WriteFanByte(ushort address, byte data, Func<bool> stillCurrent)
    {
        CheckWriteCurrent(stillCurrent);
        if (!IsOpen || useOpenLib.GetValue(device) is not true || cleanupPending || releaseReturned)
            throw Contract("fan write lost its owned transport or cleanup is pending");
        if (serial.GetValue(device) is not null || vendorHid.GetValue(device) is not null)
            throw Contract("foreign device resources appeared during fan ownership");
        var ec = ValidatedFanData();
        if (Invoke(release, device, address, ec, data) is not true)
            throw new FanApiException(409, "FAN_CURVE_WRITE_FAILED",
                $"OneX EC fan write was rejected at 0x{address:x4}; owned transport retained");
        // A write may have happened before a new sleep/close. Preserve that
        // uncertainty while refusing to publish a success for a retired owner.
        CheckWriteCurrent(stillCurrent);
    }

    private object ValidatedFanData()
    {
        var ec = ecDetails.GetValue(device) ?? throw Contract("EC details missing at write");
        int Read(string name) => Convert.ToInt32(ec.GetType().GetField(name)?.GetValue(ec) ?? -1);
        if (Read("AddressFanControl") != 0x44a || Read("AddressFanDuty") != 0x44b ||
            Read("AddressStatusCommandPort") != 0x4e || Read("AddressDataPort") != 0x4f ||
            Read("FanValueMin") != 0 || Read("FanValueMax") != 184)
            throw Contract("EC fan data changed before write");
        return ec;
    }

    private static void CheckWriteCurrent(Func<bool> current)
    {
        if (!current()) throw new FanApiException(409, "FAN_WRITE_SUPERSEDED", "OneX fan write was superseded");
    }

    internal void Close()
    {
        lock (TransportGate) CloseCore();
    }

    private void CloseCore()
    {
        if (!HasResources) return;
        cleanupPending = true;
        if (ownsMarker && !releaseReturned)
        {
            if (!IsOpen || useOpenLib.GetValue(device) is not true) throw Contract("fan release lost its owned transport");
            // SetFanControl(false) ignores this exact EC method's bool. Preserve
            // its 0x44a=0 command and consume the actual software write result.
            var ec = ValidatedFanData();
            if (Invoke(release, device, (ushort)0x44a, ec, (byte)0) is not true)
                throw Contract("EC fan automatic return was rejected; owned transport retained");
            releaseReturned = true;
        }
        CleanupTransport();
    }

    private void CleanupTransport()
    {
        cleanupPending = true;
        if (ownedTransport is not null)
        {
            Invoke(dispose, ownedTransport); // A failure stays owned and retryable.
            if (ReferenceEquals(openLib.GetValue(null), ownedTransport)) openLib.SetValue(null, null);
            ownedTransport = null;
        }
        if (ownsMarker) deviceOpen.SetValue(device, false);
        ownsMarker = false; releaseReturned = false; cleanupPending = false;
    }

    private static FieldInfo RequireField(Type type, string name, Type expected)
    {
        var field = type.GetField(name, InstanceFields) ?? throw Contract(name + " missing");
        if (field.FieldType != expected) throw Contract(name + " type changed");
        return field;
    }
    private static MethodInfo RequireOwner(Type type, string name, Type owner, Type result, Type[] args)
    {
        var method = type.GetMethod(name, BindingFlags.Public | BindingFlags.Instance, null, args, null);
        if (method?.DeclaringType != owner || method.ReturnType != result) throw Contract(name + " owner/signature changed");
        return method;
    }
    private static object? Invoke(MethodInfo method, object? target, params object?[] args)
    {
        try { return method.Invoke(target, args); }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); throw; }
    }
    private static void CheckCurrent(Func<bool> current)
    {
        if (!current()) throw new FanApiException(409, "HC_RESUME_SUPERSEDED", "OneX fan initialization was superseded");
    }
    private static InvalidOperationException Contract(string detail) => new("ONEX_FAN_OPEN_CONTRACT: " + detail);
}
