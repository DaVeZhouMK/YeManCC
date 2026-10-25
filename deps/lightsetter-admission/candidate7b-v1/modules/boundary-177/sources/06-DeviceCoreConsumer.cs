using YeManFanBackend.Capabilities;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

internal static class DeviceCoreConsumer
{
    internal static Dictionary<string, object?>? TryExecute(string path, string? callerHash,
        string? deviceClass, string action, string? level, object request)
    {
        if (string.IsNullOrWhiteSpace(deviceClass))
            throw new ArgumentException("--expected-device-class is required for --device-core-assembly");
        var route = HcLightingCapabilityCatalog.Resolve(deviceClass) ??
            throw new NotSupportedException("LIGHTING_CAPABILITY_UNAVAILABLE: " + deviceClass);
        if (!route.Consumer.IndependentCore) return null;
        var parameters = JsonSerializer.SerializeToElement(request);
        var hasBrightness = parameters.TryGetProperty("Brightness", out var requestedBrightness) && requestedBrightness.ValueKind == JsonValueKind.Number;
        var requestedPreset = parameters.TryGetProperty("PresetId", out var presetValue) && presetValue.ValueKind == JsonValueKind.String ? presetValue.GetString() : null;
        var capability = HcLightingCapabilityCatalog.Require(deviceClass, null, action, level, hasBrightness, requestedPreset);
        if (capability.PreferredProvider != HcLightingCapabilityCatalog.IndependentProvider) return null;
        deviceClass = capability.Device.ConsumerClass;
        level = capability.Level;
        var pin = LightingAssemblyPins.StandaloneLightingSha256;
        // CLI hashes/sidecars may add a consistency check but cannot authorize
        // a new binary. Only a reviewed, compiled pin can do that.
        if (callerHash is not null && !pin.Equals(callerHash, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("device-core caller SHA-256 is not an allowed pin");
        var fullPath = Path.GetFullPath(path);
        using var stream = File.OpenRead(fullPath);
        var actual = Convert.ToHexString(SHA256.HashData(stream));
        if (!pin.Equals(actual, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException($"device-core assembly SHA-256 mismatch: {actual}");
        var core = YeMan.SharedRuntime.RuntimeDependencyContract.LoadPinned(
            AssemblyLoadContext.Default, new AssemblyName("YeManLightDeviceCore"), fullPath, pin);
        if (core.GetName().Name != "YeManLightDeviceCore")
            throw new InvalidOperationException("unexpected device-core assembly identity");
        var api = core.GetType("YeManCC.Lighting.LightingDeviceCore", throwOnError: true)!;
        var (supports, execute) = ResolveApi(api);
        if (supports.Invoke(null, [deviceClass, action, level]) is not true) return null;
        // Exceptions and failed notes propagate. Never retry using HC after a
        // supported request has entered the component's transport boundary.
        var json = execute.Invoke(null, [deviceClass, JsonSerializer.Serialize(request)]) as string
            ?? throw new InvalidOperationException("empty device-core receipt");
        var result = ReadReceipt(json, deviceClass, action, request);
        result["backend"] = HcLightingCapabilityCatalog.IndependentProvider;
        result["capabilityId"] = capability.Device.DeviceId;
        result["capabilityOperation"] = capability.OperationId;
        result["capabilityCatalogSha256"] = HcLightingCapabilityCatalog.CatalogSha256;
        result["deviceCoreSha256"] = actual.ToLowerInvariant();
        result["hcLoaded"] = false;
        return result;
    }

    internal static (MethodInfo Supports, MethodInfo Execute) ResolveApi(Type api)
    {
        if (api.GetField("ApiVersion", BindingFlags.Public | BindingFlags.Static)?.GetRawConstantValue() is not int version || version != 1)
            throw new InvalidOperationException("unsupported device-core API version");
        var supports = api.GetMethod("Supports", BindingFlags.Public | BindingFlags.Static,
            null, [typeof(string), typeof(string), typeof(string)], null)
            ?? throw new MissingMethodException("device-core Supports");
        if (supports.ReturnType != typeof(bool)) throw new InvalidOperationException("invalid device-core Supports result");
        var execute = api.GetMethod("Execute", BindingFlags.Public | BindingFlags.Static,
            null, [typeof(string), typeof(string)], null)
            ?? throw new MissingMethodException("device-core Execute");
        if (execute.ReturnType != typeof(string)) throw new InvalidOperationException("invalid device-core Execute result");
        return (supports, execute);
    }

    internal static Dictionary<string, object?> ReadReceipt(string json, string deviceClass, string action, object request)
    {
        var fields = JsonSerializer.Deserialize<Dictionary<string, JsonElement>>(json)
            ?? throw new InvalidOperationException("invalid device-core receipt");
        string? Text(string key) => fields.TryGetValue(key, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() : null;
        if (Text("deviceClass") != deviceClass || Text("action") != action)
            throw new InvalidOperationException("device-core receipt identity/action mismatch");
        foreach (var key in new[] { "ready", "opened", "released" })
            if (!fields.TryGetValue(key, out var value) || value.ValueKind != JsonValueKind.True)
                throw new InvalidOperationException($"device-core receipt missing completed {key}");
        if (!fields.TryGetValue("physicalEffectVerified", out var physical) || physical.ValueKind != JsonValueKind.False)
            throw new InvalidOperationException("device-core receipt cannot establish physical effect");
        var parameters = JsonSerializer.SerializeToElement(request);
        var hasBrightness = parameters.EnumerateObject().Any(item =>
            string.Equals(item.Name, "brightness", StringComparison.OrdinalIgnoreCase) && item.Value.ValueKind != JsonValueKind.Null);
        var expectedWrites = action == "color" && hasBrightness ? 2 : 1;
        if (!fields.TryGetValue("writeAttempts", out var attempts) || (attempts.ValueKind != JsonValueKind.Number || !attempts.TryGetInt32(out var count)) || count != expectedWrites)
            throw new InvalidOperationException("device-core receipt write-attempt mismatch");
        var note = Text("note");
        if (note != "ok" && note != "setLedBrightness-false" && !(action == "color" && note == "setLedColor-false"))
            throw new InvalidOperationException("device-core receipt has invalid note");
        if (note == "setLedBrightness-false" && action == "color" && !hasBrightness)
            throw new InvalidOperationException("device-core receipt has unrelated failure");
        var result = fields.ToDictionary(item => item.Key, item => (object?)item.Value);
        result["note"] = note;
        return result;
    }

}
