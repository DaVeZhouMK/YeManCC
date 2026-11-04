using YeManFanBackend.Capabilities;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

internal static class DeviceCoreConsumer
{
    internal const string X2MiniPro = "HandheldCompanion.Devices.OneXPlayerX2MiniPro";
    internal static Dictionary<string, object?>? TryExecute(string path, string? callerHash,
        string? deviceClass, string action, string? level, object request,
        string? ownerInterfacePath = null, string? ownerGeneration = null, int timeoutMs = 5000)
    {
        if (string.IsNullOrWhiteSpace(deviceClass))
            throw new ArgumentException("--expected-device-class is required for --device-core-assembly");
        var route = HcLightingCapabilityCatalog.Resolve(deviceClass) ??
            throw new NotSupportedException("LIGHTING_CAPABILITY_UNAVAILABLE: " + deviceClass);
        if (route.Device.ConsumerClass == X2MiniPro &&
            (route.Consumer.SourceAdapter != LightingSourceAdapter.OneXX2MiniHid ||
             route.Device.SourceSha256 != "7f06350daeb1d1c7157ae21f1dc231637394008256b3fc4c22c747109e2c7b67"))
            throw new InvalidOperationException("X2 Mini Pro source adapter/SHA is not the reviewed fixed source");
        if (!route.Consumer.IndependentCore)
        {
            if (deviceClass == X2MiniPro) throw new NotSupportedException("X2 Mini Pro independent lighting is not admitted");
            return null;
        }
        var parameters = JsonSerializer.SerializeToElement(request);
        var hasBrightness = parameters.TryGetProperty("Brightness", out var requestedBrightness) && requestedBrightness.ValueKind == JsonValueKind.Number;
        var requestedPreset = parameters.TryGetProperty("PresetId", out var presetValue) && presetValue.ValueKind == JsonValueKind.String ? presetValue.GetString() : null;
        var capability = HcLightingCapabilityCatalog.Require(deviceClass, null, action, level, hasBrightness, requestedPreset);
        if (capability.PreferredProvider != HcLightingCapabilityCatalog.IndependentProvider)
        {
            if (deviceClass == X2MiniPro) throw new NotSupportedException("X2 Mini Pro operation has no admitted independent provider");
            return null;
        }
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
        var api = core.GetType(deviceClass == X2MiniPro
            ? "YeManCC.Lighting.X2MiniProDeviceCore" : "YeManCC.Lighting.LightingDeviceCore", throwOnError: true)!;
        var (supports, execute) = ResolveApi(api);
        if (supports.Invoke(null, [deviceClass, action, level]) is not true)
        {
            if (deviceClass == X2MiniPro) throw new NotSupportedException("X2 Mini Pro device-core operation unavailable");
            return null;
        }
        // Exceptions and failed notes propagate. Never retry using HC after a
        // supported request has entered the component's transport boundary.
        var requestJson = JsonSerializer.Serialize(request);
        var result = deviceClass == X2MiniPro
            ? ExecuteOwned(api, deviceClass, action, requestJson, request,
                ownerInterfacePath, ownerGeneration, timeoutMs)
            : ReadReceipt(execute.Invoke(null, [deviceClass, requestJson]) as string
                ?? throw new InvalidOperationException("empty device-core receipt"), deviceClass, action, request);
        result["backend"] = HcLightingCapabilityCatalog.IndependentProvider;
        result["capabilityId"] = capability.Device.DeviceId;
        result["capabilityOperation"] = capability.OperationId;
        result["capabilityCatalogSha256"] = HcLightingCapabilityCatalog.CatalogSha256;
        result["deviceCoreSha256"] = actual.ToLowerInvariant();
        result["hcLoaded"] = false;
        return result;
    }

    private static Dictionary<string, object?> ExecuteOwned(Type api, string deviceClass, string action,
        string requestJson, object request, string? interfacePath, string? generation, int timeoutMs)
    {
        if (string.IsNullOrWhiteSpace(interfacePath) || string.IsNullOrWhiteSpace(generation))
            throw new ArgumentException("X2 Mini Pro requires the current native OneX owner path and generation");
        if (timeoutMs <= 0) throw new ArgumentOutOfRangeException(nameof(timeoutMs));
        var execute = api.GetMethod("ExecuteOwned", BindingFlags.Public | BindingFlags.Static,
            null, [typeof(string), typeof(string), typeof(string), typeof(CancellationToken), typeof(Action)], null);
        if (execute is null || execute.ReturnType != typeof(string)) throw new MissingMethodException("device-core ExecuteOwned");
        using var deadline = new CancellationTokenSource(timeoutMs);
        // Reuse only the existing OneX OWNER handoff ABI. X2 reports are built
        // in DeviceCore; no OXP3 writer, transport, preset or zone is involved.
        // Native must admit the exact X2 model after its own initialization.
        using var lease = Oxp3OwnerLease.Acquire(interfacePath, generation, deadline.Token, Oxp3OwnerLease.X2MiniProModelTag);
        using var operation = CancellationTokenSource.CreateLinkedTokenSource(deadline.Token, lease.Token);
        lease.EnsureCurrent();
        var json = execute.Invoke(null, [deviceClass, requestJson, lease.ExpectedPath,
            operation.Token, (Action)lease.EnsureCurrent]) as string
            ?? throw new InvalidOperationException("empty owned device-core receipt");
        operation.Token.ThrowIfCancellationRequested();
        lease.EnsureCurrent();
        var result = ReadReceipt(json, deviceClass, action, request);
        result["ownerAdmission"] = "native-owner-handoff";
        result["ownerGeneration"] = lease.Generation;
        result["ownerRevoked"] = lease.WasRevoked;
        result["ownerGenerationChanged"] = lease.WasSuperseded;
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
        var expectedWrites = deviceClass == X2MiniPro
            ? (action == "off" || action == "color" && hasBrightness ? 10 : 5)
            : action == "color" && hasBrightness ? 2 : 1;
        if (deviceClass == X2MiniPro &&
            (Text("sourceCommit") != "f2e58468d9917189d4be7ccfc242385ce2c3035b" ||
             Text("sourceSha256") != "7f06350daeb1d1c7157ae21f1dc231637394008256b3fc4c22c747109e2c7b67" ||
             Text("sourceAdapter") != "onex-x2mini-hid-v1" ||
             !fields.TryGetValue("speedApplied", out var speedApplied) || speedApplied.ValueKind != JsonValueKind.False))
            throw new InvalidOperationException("X2 Mini Pro device-core source receipt mismatch");
        if (!fields.TryGetValue("writeAttempts", out var attempts) || (attempts.ValueKind != JsonValueKind.Number || !attempts.TryGetInt32(out var count)) || count != expectedWrites)
            throw new InvalidOperationException("device-core receipt write-attempt mismatch");
        var note = Text("note");
        if (deviceClass == X2MiniPro && action == "off")
        {
            if (note is not ("ok" or "off-partial") ||
                (note == "ok" ? Text("firstFailure") is not null :
                 Text("firstFailure") is not ("setLedBrightness-false" or "setLedColor-false")))
                throw new InvalidOperationException("X2 Mini Pro off receipt is inconsistent");
        }
        else if (note != "ok" && note != "setLedBrightness-false" && !(action == "color" && note == "setLedColor-false"))
            throw new InvalidOperationException("device-core receipt has invalid note");
        if (note == "setLedBrightness-false" && action == "color" && !hasBrightness)
            throw new InvalidOperationException("device-core receipt has unrelated failure");
        var result = fields.ToDictionary(item => item.Key, item => (object?)item.Value);
        result["note"] = note;
        return result;
    }

}
