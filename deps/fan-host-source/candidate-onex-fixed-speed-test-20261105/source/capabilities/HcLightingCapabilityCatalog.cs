// AI-NAV[lighting]: hc-lighting-policy.json is the action-policy input; generated rows are not edited here.
// Follow native/main.cpp lighting dispatch and FanLab/LightSetter/Program.cs ExecuteResolvedCatalogLedAction.
// Require selects a permitted action/provider; actual helper completion, device writes and cleanup remain consumer-owned.
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
namespace YeManFanBackend.Capabilities;

public enum LightingSourceAdapter { FrozenHc, Oxp3Hid, SuperJoyCec, OneXX2MiniHid, ApexHidV2 }

public sealed record LightingCapabilityDescriptor(string DeviceId, bool Brightness, bool SecondColor,
    HcSourceLedEffect DeclaredEffects, bool StatusAvailable, bool PresetDeclared, bool PresetAvailable, bool IndependentCore, bool StatusTransportDisabled)
{
    public LightingSourceAdapter SourceAdapter { get; init; } = LightingSourceAdapter.FrozenHc;
    public bool FixedSpeed { get; init; }
}
public sealed record LightingCapabilityOperation(HcDeviceDescriptor Device, LightingCapabilityDescriptor Capabilities,
    string Action, string Level, string PreferredProvider)
{
    public string OperationId => "lighting." + Action + (Action == "color" ? "." + Level : "");
}

// Model selection and operation admission share the same generated catalog used by native UI gates.
// This never opens a device or promotes a callback result into physical confirmation.
public static partial class HcLightingCapabilityCatalog
{
    private static readonly HcCapabilityDispatcher<LightingCapabilityDescriptor> dispatcher;
    private static readonly IReadOnlyDictionary<string, HcCapabilityBinding<LightingCapabilityDescriptor>> extensionRoutes;
    private static readonly IReadOnlyDictionary<string, string[]> extensionPresetIds;
    private static readonly IReadOnlyDictionary<string, IReadOnlyDictionary<string, int>> extensionPresetValues;
    private static readonly IReadOnlyDictionary<string, HcSourceLedEffect> levels = new ReadOnlyDictionary<string, HcSourceLedEffect>(
        new Dictionary<string, HcSourceLedEffect>(StringComparer.OrdinalIgnoreCase) {
            ["solid"] = HcSourceLedEffect.SolidColor, ["breathing"] = HcSourceLedEffect.Breathing,
            ["rainbow"] = HcSourceLedEffect.Rainbow, ["wave"] = HcSourceLedEffect.Wave,
            ["wheel"] = HcSourceLedEffect.Wheel, ["gradient"] = HcSourceLedEffect.Gradient,
            ["ambilight"] = HcSourceLedEffect.Ambilight });
    static HcLightingCapabilityCatalog()
    {
        dispatcher = HcDeviceCapabilityCatalog.Bind("lighting", GeneratedRoutes.ToDictionary(r => r.DeviceId, StringComparer.Ordinal));
        extensionRoutes = BuildExtensionRoutes();
        extensionPresetIds = BuildExtensionPresetIds();
        extensionPresetValues = BuildExtensionPresetValues();
    }
    public static IReadOnlyList<HcCapabilityBinding<LightingCapabilityDescriptor>> Bindings => dispatcher.Bindings;
    public static HcCapabilityBinding<LightingCapabilityDescriptor>? Resolve(string? deviceClass) =>
        dispatcher.Resolve(deviceClass) ??
        (deviceClass is not null && extensionRoutes.TryGetValue(deviceClass, out var binding) ? binding : null);
    public static bool IsReviewedExtension(string? deviceClass) =>
        deviceClass is not null && extensionRoutes.ContainsKey(deviceClass);
    public static bool IdentityMatches(string? actual, string? expected)
    {
        var a = HcDeviceCapabilityCatalog.Find(actual); var e = HcDeviceCapabilityCatalog.Find(expected);
        if (a is not null && e is not null && (a.DeviceId == e.DeviceId ||
            GeneratedIdentityAliases.Any(pair => pair.Expected == e.DeviceId && pair.Actual == a.DeviceId))) return true;
        return actual is not null && expected is not null &&
            string.Equals(actual, expected, StringComparison.Ordinal) && extensionRoutes.ContainsKey(actual);
    }
    public static LightingCapabilityOperation Require(string? actual, string? expected, string action,
        string? level = null, bool includesBrightness = false, string? presetId = null)
    {
        var binding = Resolve(actual) ?? throw new NotSupportedException("LIGHTING_CAPABILITY_UNAVAILABLE: " + actual);
        if (expected is not null && !IdentityMatches(actual, expected)) throw new InvalidOperationException("LIGHTING_CAPABILITY_IDENTITY_MISMATCH");
        var c = binding.Consumer; var effect = string.IsNullOrWhiteSpace(level) ? "solid" : level.ToLowerInvariant();
        if (includesBrightness && !c.Brightness) throw new NotSupportedException("LIGHTING_BRIGHTNESS_UNAVAILABLE");
        var presetIds = extensionPresetIds.TryGetValue(binding.Device.DeviceId, out var ids) ? ids : GeneratedPresetIds;
        var allowed = action switch {
            "color" => levels.TryGetValue(effect, out var flag) && c.DeclaredEffects.HasFlag(flag),
            "brightness" => c.Brightness,
            "status" => c.StatusAvailable,
            "off" => c.DeclaredEffects.HasFlag(HcSourceLedEffect.SolidColor),
            "preset" => c.PresetAvailable && presetId is not null && presetIds.Contains(presetId, StringComparer.Ordinal),
            _ => false };
        if (!allowed) throw new NotSupportedException("LIGHTING_OPERATION_UNAVAILABLE: " + action + ":" + effect);
        var independent = c.IndependentCore && GeneratedIndependentActionAllowed(binding.Device.DeviceId, action, effect);
        var provider = c.SourceAdapter switch {
            LightingSourceAdapter.ApexHidV2 => "onex-apex-hid-v2",
            LightingSourceAdapter.SuperJoyCec => "superjoy-cec-v1",
            _ => independent ? IndependentProvider : "hc-device-core" };
        return new(binding.Device, c, action, effect, provider);
    }
    public static bool TryResolveExtensionPreset(string deviceId, string presetId, out int value)
    {
        value = 0;
        return extensionPresetValues.TryGetValue(deviceId, out var map) && map.TryGetValue(presetId, out value);
    }
    internal static IReadOnlyDictionary<string, HcCapabilityBinding<LightingCapabilityDescriptor>> BuildExtensionRoutes(
        IEnumerable<(HcDeviceDescriptor Device, LightingCapabilityDescriptor Consumer)>? source = null)
    {
        var result = new Dictionary<string, HcCapabilityBinding<LightingCapabilityDescriptor>>(StringComparer.Ordinal);
        foreach (var row in source ?? GeneratedExtensionRoutes)
        {
            var binding = new HcCapabilityBinding<LightingCapabilityDescriptor>("lighting", row.Device, row.Consumer);
            foreach (var identity in new[] { row.Device.DeviceId, row.Device.FactoryType, row.Device.ConsumerClass }.Distinct(StringComparer.Ordinal))
                if (!result.TryAdd(identity, binding)) throw new InvalidOperationException("LIGHTING_EXTENSION_IDENTITY_CONFLICT: " + identity);
        }
        return new ReadOnlyDictionary<string, HcCapabilityBinding<LightingCapabilityDescriptor>>(result);
    }
    private static IReadOnlyDictionary<string, string[]> BuildExtensionPresetIds()
    {
        var result = new Dictionary<string, string[]>(StringComparer.Ordinal);
        foreach (var row in GeneratedExtensionPresets)
        {
            if (result.TryGetValue(row.DeviceId, out var existing))
            {
                result[row.DeviceId] = [.. existing, row.Id];
            }
            else result[row.DeviceId] = [row.Id];
        }
        return new ReadOnlyDictionary<string, string[]>(result);
    }
    private static IReadOnlyDictionary<string, IReadOnlyDictionary<string, int>> BuildExtensionPresetValues()
    {
        var result = new Dictionary<string, IReadOnlyDictionary<string, int>>(StringComparer.Ordinal);
        foreach (var row in GeneratedExtensionPresets)
        {
            var rowMap = result.TryGetValue(row.DeviceId, out var existing)
                ? new Dictionary<string, int>(existing, StringComparer.Ordinal)
                : new Dictionary<string, int>(StringComparer.Ordinal);
            rowMap[row.Id] = row.Value;
            result[row.DeviceId] = new ReadOnlyDictionary<string, int>(rowMap);
        }
        return new ReadOnlyDictionary<string, IReadOnlyDictionary<string, int>>(result);
    }
}
