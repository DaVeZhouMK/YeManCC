using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace YeManFanBackend.Capabilities;

// Source declarations describe provenance. Runtime admission remains the consumer's responsibility.
public sealed record HcDeviceDescriptor(string DeviceId, string FactoryType, string ConsumerClass,
    string Family, string SourcePath, string SourceSha256, bool Instantiable,
    HcSourceCapability DeclaredMinimum, HcSourceCapability DeclaredPossible,
    HcSourceLedEffect DeclaredLedMinimum, HcSourceLedEffect DeclaredLedPossible);

public sealed record HcCapabilityBinding<T>(string Domain, HcDeviceDescriptor Device, T Consumer) where T : class;

public static partial class HcDeviceCapabilityCatalog
{
    private static readonly IReadOnlyDictionary<string, HcDeviceDescriptor> aliases;
    public static IReadOnlyList<HcDeviceDescriptor> Devices { get; }

    static HcDeviceCapabilityCatalog()
    {
        aliases = BuildAliases();
        Devices = Array.AsReadOnly(GeneratedDevices);
    }

    private static IReadOnlyDictionary<string, HcDeviceDescriptor> BuildAliases()
    {
        var result = new Dictionary<string, HcDeviceDescriptor>(StringComparer.Ordinal);
        for (var i = 0; i < GeneratedDevices.Length; i++)
            foreach (var alias in GeneratedAliases[i])
                if (result.TryGetValue(alias, out var existing) && existing.DeviceId != GeneratedDevices[i].DeviceId)
                    throw new InvalidOperationException("HC_CAPABILITY_ALIAS_CONFLICT: " + alias);
                else result[alias] = GeneratedDevices[i];
        return new ReadOnlyDictionary<string, HcDeviceDescriptor>(result);
    }

    public static HcDeviceDescriptor? Find(string? identity) =>
        identity is not null && aliases.TryGetValue(identity, out var device) ? device : null;

    public static HcCapabilityDispatcher<T> Bind<T>(string domain, IReadOnlyDictionary<string, T> registrations) where T : class =>
        new(domain, registrations);
}

// Takes an immutable snapshot of the actual domain consumers. It does not infer availability
// from source flags, inheritance, family membership, or a matching class-name suffix.
public sealed class HcCapabilityDispatcher<T> where T : class
{
    private readonly IReadOnlyDictionary<string, HcCapabilityBinding<T>> routes;
    public string Domain { get; }
    public IReadOnlyList<HcCapabilityBinding<T>> Bindings { get; }

    internal HcCapabilityDispatcher(string domain, IReadOnlyDictionary<string, T> registrations)
    {
        if (string.IsNullOrWhiteSpace(domain)) throw new ArgumentException("Capability domain required", nameof(domain));
        ArgumentNullException.ThrowIfNull(registrations);
        Domain = domain;
        var snapshot = new Dictionary<string, HcCapabilityBinding<T>>(StringComparer.Ordinal);
        foreach (var pair in registrations)
        {
            var device = HcDeviceCapabilityCatalog.Find(pair.Key) ?? throw new InvalidOperationException("HC_CAPABILITY_UNREGISTERED_DEVICE: " + pair.Key);
            ArgumentNullException.ThrowIfNull(pair.Value);
            if (snapshot.TryGetValue(device.DeviceId, out var existing))
            {
                if (!ReferenceEquals(existing.Consumer, pair.Value)) throw new InvalidOperationException("HC_CAPABILITY_CONSUMER_CONFLICT: " + device.DeviceId);
            }
            else snapshot.Add(device.DeviceId, new(domain, device, pair.Value));
        }
        routes = new ReadOnlyDictionary<string, HcCapabilityBinding<T>>(snapshot);
        Bindings = Array.AsReadOnly(snapshot.Values.OrderBy(r => r.Device.DeviceId, StringComparer.Ordinal).ToArray());
    }

    // Bind immutable model metadata once per runtime type. Live admission remains consumer-owned.
    public HcCapabilityTypeCache<T> CreateTypeCache() => new(this);

    public HcCapabilityBinding<T>? Resolve(string? identity)
    {
        var device = HcDeviceCapabilityCatalog.Find(identity);
        return device is not null && routes.TryGetValue(device.DeviceId, out var binding) ? binding : null;
    }
}

// One entry per consumer; unknown types are cached as unavailable. Never retains a device,
// handle, lease, readiness result or write authorization. A different Type always rebinds.
public sealed class HcCapabilityTypeCache<T> where T : class
{
    private sealed record Entry(Type DeviceType, HcCapabilityBinding<T>? Binding);
    private readonly HcCapabilityDispatcher<T> dispatcher;
    private Entry? entry;
    private long resolutions;
    internal HcCapabilityTypeCache(HcCapabilityDispatcher<T> dispatcher) => this.dispatcher = dispatcher;
    public long ResolutionCount => System.Threading.Interlocked.Read(ref resolutions);
    public HcCapabilityBinding<T>? Resolve(Type? deviceType)
    {
        if (deviceType is null) { Invalidate(); return null; }
        var current = System.Threading.Volatile.Read(ref entry);
        if (current is not null && ReferenceEquals(current.DeviceType, deviceType)) return current.Binding;
        var binding = dispatcher.Resolve(deviceType.FullName);
        current = new(deviceType, binding);
        System.Threading.Interlocked.Increment(ref resolutions);
        System.Threading.Volatile.Write(ref entry, current);
        return current.Binding;
    }
    public void Invalidate() => System.Threading.Volatile.Write(ref entry, null);
}
