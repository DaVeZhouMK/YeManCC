using System.Collections;
using System.Reflection;
using System.Runtime.ExceptionServices;

namespace YeManBatteryHost;

internal interface ISuperXChargeReader : IDisposable
{
    byte ReadLimit();
}

// Source-bounded Super X charge read: raw EC16 04A3 through the captured UMA
// endpoint. The provider must own this dormant reader before calling ReadLimit.
internal sealed class SuperXUmaChargeReader : ISuperXChargeReader
{
    private const string ReadMethod = "GetEcValue";
    private static readonly TimeSpan CallTimeout = TimeSpan.FromSeconds(3);
    private readonly object gate = new();
    private readonly Type? searcherType, managementClassType;
    private readonly List<object> resources = new();
    private readonly HashSet<object> retired = new(ReferenceEqualityComparer.Instance);
    private object? instance;
    private string? endpointPath;
    private MethodInfo? getParameters, invokeMethod;
    private Type? invokeOptionsType;
    private bool started, initialized, stopped, faulted, cleanupDebt;

    internal SuperXUmaChargeReader(Type? searcherType = null, Type? managementClassType = null)
    {
        this.searcherType = searcherType;
        this.managementClassType = managementClassType;
    }

    private object Own(object value)
    {
        if (retired.Contains(value)) throw new IOException("UMA returned an already released object");
        if (!resources.Any(other => ReferenceEquals(other, value))) resources.Add(value);
        return value;
    }

    private static object Required(object? value, string detail) =>
        value ?? throw new IOException("UMA missing " + detail);

    private static object? Call(MethodInfo method, object owner, params object?[] args)
    {
        try { return method.Invoke(owner, args); }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        {
            ExceptionDispatchInfo.Capture(error.InnerException).Throw();
            throw;
        }
    }

    private static PropertyInfo PropertyInfo(object owner, string name) =>
        owner.GetType().GetProperty(name, BindingFlags.Public | BindingFlags.Instance)
        ?? throw new IOException("UMA missing property " + name);

    private static object? Property(object owner, string name)
    {
        var property = PropertyInfo(owner, name);
        var getter = property.GetGetMethod() ?? throw new IOException("UMA unreadable " + name);
        return Call(getter, owner);
    }

    private static PropertyInfo Indexer(object owner) =>
        owner.GetType().GetProperty("Item", BindingFlags.Public | BindingFlags.Instance,
            null, null, new[] { typeof(string) }, null)
        ?? throw new IOException("UMA missing string indexer");

    private static object? Item(object owner, string name)
    {
        var getter = Indexer(owner).GetGetMethod() ?? throw new IOException("UMA unreadable indexer");
        return Call(getter, owner, name);
    }

    private object OwnedProperty(object owner, string name) => Own(Required(Property(owner, name), name));
    private object OwnedItem(object owner, string name) => Own(Required(Item(owner, name), name));

    // Retain the options contract; synchronous System.Management.InvokeMethod
    // does not consume Timeout, so this is not an elapsed-time deadline.
    private static void SetTimeout(object options)
    {
        var timeout = PropertyInfo(options, "Timeout");
        if (timeout.PropertyType != typeof(TimeSpan) || timeout.GetSetMethod() is not { } setter ||
            timeout.GetGetMethod() is not { } getter || timeout.GetIndexParameters().Length != 0)
            throw new IOException("UMA requires a public readable/writable TimeSpan timeout");
        Call(setter, options, CallTimeout);
        if (Call(getter, options) is not TimeSpan actual || actual != CallTimeout)
            throw new IOException("UMA did not retain the three second timeout");
    }

    private string CheckInteger(object parameters, string name, bool index)
    {
        var properties = OwnedProperty(parameters, "Properties");
        var field = OwnedItem(properties, name);
        if (Property(field, "IsArray") is not false)
            throw new IOException("UMA array or unknown field shape: " + name);
        // The pinned System.Management contract maps UInt16/SInt32/UInt32 to
        // VT_I4. SInt16 is VT_I2 and cannot carry the source's boxed int Index.
        if (Property(field, "Type") is not Enum kind)
            throw new IOException("UMA missing CIM integer type: " + name);
        var typeName = Enum.GetName(kind.GetType(), kind);
        bool accepted = index ? typeName is "UInt16" or "SInt32" or "UInt32" :
            typeName is "UInt8" or "SInt8" or "UInt16" or "SInt16" or
                "UInt32" or "SInt32" or "UInt64" or "SInt64";
        if (!accepted) throw new IOException("UMA incompatible integer metadata: " + name);
        return typeName!;
    }

    private void CheckMethod(object metadata)
    {
        var methods = OwnedProperty(metadata, "Methods");
        var method = OwnedItem(methods, ReadMethod);
        var input = OwnedProperty(method, "InParameters");
        CheckInteger(input, "Index", true);
        var output = OwnedProperty(method, "OutParameters");
        CheckInteger(output, "Data", false);
    }

    private void BindInvocation(object selected)
    {
        getParameters = selected.GetType().GetMethod("GetMethodParameters",
            BindingFlags.Public | BindingFlags.Instance, null, new[] { typeof(string) }, null)
            ?? throw new IOException("UMA method input unavailable");
        var candidates = selected.GetType().GetMethods(BindingFlags.Public | BindingFlags.Instance)
            .Where(method => method.Name == "InvokeMethod" &&
                method.GetParameters() is { Length: 3 } candidateArgs &&
                candidateArgs[0].ParameterType == typeof(string) && candidateArgs[1].ParameterType != typeof(object[]))
            .ToArray();
        if (candidates.Length != 1) throw new IOException("UMA invocation contract is ambiguous or missing");
        var invoke = candidates[0];
        var args = invoke.GetParameters();
        if (getParameters.ContainsGenericParameters || invoke.ContainsGenericParameters ||
            getParameters.ReturnType == typeof(void) || getParameters.ReturnType.IsValueType ||
            invoke.ReturnType == typeof(void) || invoke.ReturnType.IsValueType ||
            !args[1].ParameterType.IsAssignableFrom(getParameters.ReturnType))
            throw new IOException("UMA incompatible invocation parameters");
        var options = args[2].ParameterType;
        if (!options.IsClass || options.IsAbstract || options == typeof(object) || options.IsArray ||
            options.GetConstructor(Type.EmptyTypes) is null)
            throw new IOException("UMA invocation options unavailable");
        // No null options fallback: ReadLimit constructs and verifies this exact
        // reflected options type before calling the external method.
        invokeOptionsType = options;
        invokeMethod = invoke;
    }

    private static MethodInfo? ConcreteDispose(object value)
    {
        for (Type? type = value.GetType(); type is not null; type = type.BaseType)
        {
            var methods = type.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
                .Where(method => method.Name == "Dispose" && !method.ContainsGenericParameters &&
                    method.ReturnType == typeof(void) && method.GetParameters().Length == 0).ToArray();
            if (methods.Length > 1) throw new IOException("UMA ambiguous public Dispose");
            if (methods.Length == 1) return methods[0];
        }
        return null;
    }

    private Exception? Release(IEnumerable<object> values)
    {
        var errors = new List<Exception>();
        foreach (var value in values.ToArray().Reverse())
        {
            if (!resources.Any(other => ReferenceEquals(other, value))) continue;
            try
            {
                // ManagementBaseObject hides Component.Dispose; its inherited
                // IDisposable dispatch can miss the concrete WMI cleanup.
                if (ConcreteDispose(value) is { } dispose) Call(dispose, value);
                else if (value is IDisposable disposable) disposable.Dispose();
                resources.RemoveAll(other => ReferenceEquals(other, value));
                retired.Add(value);
            }
            catch (Exception error) { errors.Add(error); cleanupDebt = true; }
        }
        return errors.Count switch
        {
            0 => null,
            1 => errors[0],
            _ => new AggregateException("UMA resource cleanup failures", errors)
        };
    }

    private static void ThrowFailures(Exception? operation, Exception? cleanup)
    {
        if (operation is not null && cleanup is not null)
            throw new AggregateException("UMA operation and cleanup failed", operation, cleanup);
        if (operation is not null) ExceptionDispatchInfo.Capture(operation).Throw();
        if (cleanup is not null) ExceptionDispatchInfo.Capture(cleanup).Throw();
    }

    private void Initialize()
    {
        if (started) throw new IOException("UMA initialization cannot be repeated");
        started = true;
        Exception? failure = null;
        try
        {
            var type = searcherType ?? Assembly.Load("System.Management")
                .GetType("System.Management.ManagementObjectSearcher", true)!;
            var searcher = Own(Required(Activator.CreateInstance(type,
                new object[] { "root\\WMI", "SELECT * FROM UMAInterface" }), "query searcher"));
            SetTimeout(OwnedProperty(searcher, "Options"));
            var get = type.GetMethod("Get", BindingFlags.Public | BindingFlags.Instance,
                null, Type.EmptyTypes, null) ?? throw new IOException("UMA query unavailable");
            var collection = Own(Required(Call(get, searcher), "query result"));
            if (collection is not IEnumerable rows) throw new IOException("UMA query result is not enumerable");
            var enumerator = (IEnumerator)Own(Required(rows.GetEnumerator(), "query enumerator"));
            object? selected = null;
            while (enumerator.MoveNext())
            {
                var row = Own(Required(enumerator.Current, "query endpoint"));
                if (Item(row, "InstanceName") is not string name ||
                    !name.Contains("SETUMAWMI", StringComparison.Ordinal)) continue;
                // A second occurrence is ambiguous even if it is the same reference.
                if (selected is not null) throw new IOException("UMA matching endpoint is ambiguous");
                selected = row;
            }
            if (selected is null) throw new IOException("UMA matching endpoint missing");
            if (Item(selected, "__PATH") is not string path || string.IsNullOrWhiteSpace(path))
                throw new IOException("UMA stable endpoint path missing");
            instance = selected;
            endpointPath = path;
            BindInvocation(selected);
            var classType = managementClassType ?? Assembly.Load("System.Management")
                .GetType("System.Management.ManagementClass", true)!;
            var scope = OwnedProperty(selected, "Scope");
            var classPath = OwnedProperty(selected, "ClassPath");
            var metadata = Own(Required(Activator.CreateInstance(classType,
                new object?[] { scope, classPath, null }), "method class"));
            CheckMethod(metadata);
        }
        catch (Exception error) { failure = error; }
        // Failed locals remain in this reader's ownership if actual cleanup fails.
        // Do not retry them a second time during this initialization attempt.
        var cleanup = Release(failure is null
            ? resources.Where(value => !ReferenceEquals(value, instance)) : resources);
        if (failure is not null || cleanup is not null)
        {
            faulted = true;
            ThrowFailures(failure, cleanup);
        }
        initialized = true;
    }

    private void CheckEndpoint()
    {
        if (!initialized || cleanupDebt || faulted || instance is null)
            throw new IOException("Owned UMA charge endpoint unavailable");
        if (Item(instance, "__PATH") is not string path ||
            !string.Equals(path, endpointPath, StringComparison.Ordinal))
            throw new IOException("UMA owned endpoint path changed");
    }

    public byte ReadLimit()
    {
        lock (gate)
        {
            if (stopped) throw new ObjectDisposedException(nameof(SuperXUmaChargeReader));
            if (cleanupDebt || faulted) throw new IOException("UMA charge reader has retained failure or cleanup debt");
            if (!initialized) Initialize();
            Exception? failure = null;
            byte result = 0;
            try
            {
                CheckEndpoint();
                var options = Own(Required(Activator.CreateInstance(invokeOptionsType!), "invoke options"));
                SetTimeout(options);
                var input = Own(Required(Call(getParameters!, instance!, ReadMethod), "read input"));
                if (ReferenceEquals(input, instance)) throw new IOException("UMA input aliases its endpoint");
                CheckInteger(input, "Index", true);
                var setter = Indexer(input).GetSetMethod() ?? throw new IOException("UMA input is not writable");
                Call(setter, input, "Index", (int)0x04A3); // Raw address; caller VARIANT is VT_I4.
                CheckEndpoint(); // Recheck after all preparation, immediately before dispatch.
                var output = Own(Required(Call(invokeMethod!, instance!, ReadMethod, input, options), "read output"));
                if (ReferenceEquals(output, instance)) throw new IOException("UMA output aliases its endpoint");
                var dataType = CheckInteger(output, "Data", false);
                result = Item(output, "Data") switch
                {
                    byte number => number,
                    // System.Management projects CIM UInt16's source VT_I4 to
                    // managed UInt16. Accept only its checked byte domain.
                    ushort number when dataType == "UInt16" && number <= 255 => (byte)number,
                    int number when number is >= 0 and <= 255 => (byte)number,
                    uint number when number <= 255 => (byte)number,
                    _ => throw new IOException("UMA read Data is not a checked source-compatible byte")
                };
            }
            catch (Exception error) { failure = error; faulted = true; }
            var cleanup = Release(resources.Where(value => !ReferenceEquals(value, instance)));
            ThrowFailures(failure, cleanup);
            return result;
        }
    }

    public void Dispose()
    {
        lock (gate)
        {
            stopped = true;
            initialized = false;
            // Always retry exact retained objects, including after an earlier stop.
            var cleanup = Release(resources);
            cleanupDebt = resources.Count != 0;
            if (!cleanupDebt)
            {
                instance = null;
                endpointPath = null;
                getParameters = invokeMethod = null;
                invokeOptionsType = null;
                retired.Clear();
            }
            ThrowFailures(null, cleanup);
        }
    }
}
