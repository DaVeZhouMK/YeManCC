using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.ExceptionServices;
using YeManFanBackend.Capabilities;

namespace YeManFanHost;

// Original YMCC adapter for the audited UMAInterface protocol facts. Construction
// is dormant: the existing catalog session owns this terminal before Initialize.
internal sealed class PinnedUmaEcFanTransport : IPinnedFanTransport
{
    private readonly PinnedFanDescriptor descriptor;
    private readonly Type? searcherTypeForSelfTest, managementClassTypeForSelfTest;
    private readonly List<object> resources = new();
    private readonly string identity = "owned-uma:" + Guid.NewGuid().ToString("N");
    private object? instance;
    private MethodInfo? invokeMethod, getParameters;
    private bool started, initialized, closed, cleanupDebt;
    internal string? DevicePath { get; private set; }
    public string EndpointIdentity => identity;
    public bool IsReady => initialized && !closed && !cleanupDebt && instance is not null;

    internal PinnedUmaEcFanTransport(PinnedFanDescriptor descriptor, Type? searcherType = null, Type? managementClassType = null)
    {
        this.descriptor = descriptor ?? throw new ArgumentNullException(nameof(descriptor));
        if (descriptor.Transport != PinnedFanTransportKind.WmiUmaEc16)
            throw new ArgumentException("UMA descriptor required", nameof(descriptor));
        searcherTypeForSelfTest = searcherType;
        managementClassTypeForSelfTest = managementClassType;
    }

    private object Own(object value)
    {
        if (!resources.Any(other => ReferenceEquals(other, value))) resources.Add(value);
        return value;
    }
    private static object? Item(object value, string key) =>
        value.GetType().GetProperty("Item", new[] { typeof(string) })?.GetValue(value, new object[] { key });
    private static object? Property(object value, string key) =>
        value.GetType().GetProperty(key)?.GetValue(value);
    private static object Required(object? value, string detail) =>
        value ?? throw new IOException("UMA metadata missing: " + detail);
    private static void Parameter(object value, string key, object input)
    {
        var indexer = value.GetType().GetProperty("Item", new[] { typeof(string) });
        if (indexer?.CanWrite != true) throw new IOException("UMA input is not writable");
        indexer.SetValue(value, input, new object[] { key });
    }
    private static void CheckInteger(object parameters, string name, bool byteDomain, bool writeData = false)
    {
        var properties = Required(Property(parameters, "Properties"), "Properties");
        var field = Required(Item(properties, name), name);
        if (Property(field, "IsArray") is not false) throw new IOException("UMA array or unknown field shape: " + name);
        var kind = Required(Property(field, "Type"), name + ".Type").ToString();
        // System.Management maps UInt16/UInt32 and SInt32 inputs to VT_I4.
        // SInt16 maps to VT_I2. Only UInt8 preserves Data's source VT_UI1.
        bool compatible = writeData ? kind == "UInt8" : byteDomain
            ? kind is "UInt8" or "SInt16" or "UInt16" or "SInt32" or "UInt32"
            : kind is "UInt16" or "SInt32" or "UInt32";
        if (!compatible) throw new IOException("UMA incompatible integer metadata: " + name);
    }
    private void CheckMethod(object selected, string name, bool write)
    {
        var methods = Required(Property(selected, "Methods"), "Methods");
        var method = Required(Item(methods, name), name);
        var input = Own(Required(Property(method, "InParameters"), name + ".InParameters"));
        CheckInteger(input, "Index", false);
        if (write) CheckInteger(input, "Data", true, true);
        else
        {
            var output = Own(Required(Property(method, "OutParameters"), name + ".OutParameters"));
            CheckInteger(output, "Data", true);
        }
    }
    // Failed disposal leaves the exact object in resources for a later stop.
    // Successfully disposed objects are never replayed or replaced.
    private Exception? Release(IEnumerable<object> values)
    {
        Exception? first = null;
        foreach (var value in values.ToArray().Reverse())
        {
            if (!resources.Any(other => ReferenceEquals(other, value))) continue;
            try
            {
                // ManagementBaseObject hides Component.Dispose. Its inherited
                // IDisposable map can bypass WMI cleanup, so invoke the public
                // most-derived method before considering an interface fallback.
                if (value.GetType().GetMethod("Dispose", Type.EmptyTypes) is { } dispose)
                    PinnedSourceFanTransports.Invoke(dispose, value);
                else if (value is IDisposable disposable) disposable.Dispose();
                resources.RemoveAll(other => ReferenceEquals(other, value));
            }
            catch (Exception error) { first ??= error; cleanupDebt = true; }
        }
        return first;
    }
    public void Initialize()
    {
        if (started || closed) throw new IOException("UMA initialization cannot be repeated");
        started = true;
        try
        {
            var type = searcherTypeForSelfTest ?? Assembly.Load("System.Management")
                .GetType("System.Management.ManagementObjectSearcher", true)!;
            var searcher = Own(Required(Activator.CreateInstance(type,
                new object[] { "root\\WMI", "SELECT * FROM UMAInterface" }), "searcher"));
            var options = Required(Property(searcher, "Options"), "query options");
            var timeout = options.GetType().GetProperty("Timeout") ?? throw new IOException("UMA query timeout unavailable");
            timeout.SetValue(options, TimeSpan.FromSeconds(3));
            var get = type.GetMethod("Get", Type.EmptyTypes) ?? throw new IOException("UMA query unavailable");
            var collection = Own(Required(PinnedSourceFanTransports.Invoke(get, searcher), "query result"));
            if (collection is not IEnumerable rows) throw new IOException("UMA query is not enumerable");
            object? selected = null;
            var enumerator = rows.GetEnumerator();
            Own(enumerator);
            while (enumerator.MoveNext())
            {
                var row = enumerator.Current;
                if (row is null) throw new IOException("UMA null endpoint");
                Own(row);
                if (Item(row, "InstanceName") is not string name || !name.Contains("SETUMAWMI", StringComparison.Ordinal)) continue;
                if (selected is not null) throw new IOException("UMA matching endpoint is ambiguous");
                selected = row;
            }
            if (selected is null) throw new IOException("UMA matching endpoint missing");
            if (Item(selected, "__PATH") is not string path || string.IsNullOrWhiteSpace(path))
                throw new IOException("UMA stable endpoint path missing");
            instance = selected;
            DevicePath = path;
            getParameters = selected.GetType().GetMethod("GetMethodParameters", new[] { typeof(string) })
                ?? throw new IOException("UMA method input unavailable");
            var invokers = selected.GetType().GetMethods().Where(method => method.Name == "InvokeMethod" &&
                method.GetParameters() is { Length: 3 } args && args[0].ParameterType == typeof(string) &&
                args[1].ParameterType != typeof(object[])).ToArray();
            if (invokers.Length != 1) throw new IOException("UMA method invocation contract is ambiguous");
            invokeMethod = invokers[0];
            // Method definitions belong to ManagementClass, not the selected
            // ManagementObject. Use this captured instance's scope/class path.
            var classType = managementClassTypeForSelfTest ?? Assembly.Load("System.Management")
                .GetType("System.Management.ManagementClass", true)!;
            var scope = Required(Property(selected, "Scope"), "selected scope");
            var classPath = Required(Property(selected, "ClassPath"), "selected class path");
            var metadata = Own(Required(Activator.CreateInstance(classType,
                new object?[] { scope, classPath, null }), "method class"));
            CheckMethod(metadata, "GetEcValue", false);
            CheckMethod(metadata, "SetEcValue", true);
            var cleanup = Release(resources.Where(value => !ReferenceEquals(value, instance)));
            if (cleanup is not null) ExceptionDispatchInfo.Capture(cleanup).Throw();
            initialized = true;
        }
        catch { initialized = false; throw; }
    }
    private void Check(ushort register)
    {
        if (!IsReady) throw new IOException("Owned UMA terminal unavailable");
        if (register != descriptor.ControlRegister && register != descriptor.DutyRegister)
            throw new IOException("UMA register is outside the admitted fan consumer");
        if (Item(instance!, "__PATH") is not string path || !string.Equals(path, DevicePath, StringComparison.Ordinal))
            throw new IOException("UMA owned endpoint path changed");
    }
    private byte Invoke(bool write, ushort register, byte value = 0)
    {
        Check(register);
        var callResources = new List<object>();
        Exception? operationError = null;
        byte resultValue = 0;
        try
        {
            var name = write ? "SetEcValue" : "GetEcValue";
            var input = Own(Required(PinnedSourceFanTransports.Invoke(getParameters!, instance!, name), name + " input"));
            callResources.Add(input);
            CheckInteger(input, "Index", false);
            Parameter(input, "Index", (int)register); // Raw EC16; caller VARIANT is VT_I4, never bank-swapped.
            if (write)
            {
                CheckInteger(input, "Data", true, true);
                Parameter(input, "Data", value); // Caller VARIANT is VT_UI1.
            }
            // Check again after parameter construction, immediately before the external call.
            Check(register);
            var result = PinnedSourceFanTransports.Invoke(invokeMethod!, instance!, name, input, null);
            if (result is not null) { Own(result); callResources.Add(result); }
            if (!write)
            {
                var data = Item(Required(result, "GetEcValue output"), "Data");
                resultValue = data switch
                {
                    byte number => number,
                    int number when number >= 0 && number <= 255 => (byte)number,
                    uint number when number <= 255 => (byte)number,
                    _ => throw new IOException("UMA read Data is not a checked byte/int32/uint32")
                };
            }
        }
        catch (Exception error) { operationError = error; }
        var cleanup = Release(callResources);
        if (operationError is not null) ExceptionDispatchInfo.Capture(operationError).Throw();
        if (cleanup is not null) ExceptionDispatchInfo.Capture(cleanup).Throw();
        return resultValue;
    }
    public byte ReadByte(ushort register) => Invoke(false, register);
    public void WriteByte(ushort register, byte value)
    {
        Check(register);
        if (register == descriptor.ControlRegister && value != descriptor.ControlOn && value != descriptor.ControlOff)
            throw new IOException("UMA mode is outside the admitted automatic/manual values");
        Invoke(true, register, value);
        if (ReadByte(register) != value) throw new IOException("UMA write readback mismatch");
    }
    public void PrepareRetirement()
    {
        if (closed) return;
        var error = Release(resources.Where(value => !ReferenceEquals(value, instance)));
        if (error is not null) ExceptionDispatchInfo.Capture(error).Throw();
        cleanupDebt = false;
    }
    public void Dispose()
    {
        closed = true;
        initialized = false;
        var error = Release(resources.ToArray());
        if (error is not null) ExceptionDispatchInfo.Capture(error).Throw();
        instance = null;
    }
}