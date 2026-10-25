using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
namespace YeManFanBackend.Capabilities;

public sealed record LightingCapabilityDescriptor(string DeviceId, bool Brightness, bool SecondColor,
    HcSourceLedEffect DeclaredEffects, bool StatusAvailable, bool PresetDeclared, bool PresetAvailable, bool IndependentCore, bool StatusTransportDisabled);
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
    private static readonly IReadOnlyDictionary<string, HcSourceLedEffect> levels = new ReadOnlyDictionary<string, HcSourceLedEffect>(
        new Dictionary<string, HcSourceLedEffect>(StringComparer.OrdinalIgnoreCase) {
            ["solid"] = HcSourceLedEffect.SolidColor, ["breathing"] = HcSourceLedEffect.Breathing,
            ["rainbow"] = HcSourceLedEffect.Rainbow, ["wave"] = HcSourceLedEffect.Wave,
            ["wheel"] = HcSourceLedEffect.Wheel, ["gradient"] = HcSourceLedEffect.Gradient,
            ["ambilight"] = HcSourceLedEffect.Ambilight });
    static HcLightingCapabilityCatalog()
    {
        dispatcher = HcDeviceCapabilityCatalog.Bind("lighting", GeneratedRoutes.ToDictionary(r => r.DeviceId, StringComparer.Ordinal));
    }
    public static IReadOnlyList<HcCapabilityBinding<LightingCapabilityDescriptor>> Bindings => dispatcher.Bindings;
    public static HcCapabilityBinding<LightingCapabilityDescriptor>? Resolve(string? deviceClass) => dispatcher.Resolve(deviceClass);
    public static bool IdentityMatches(string? actual, string? expected)
    {
        var a = HcDeviceCapabilityCatalog.Find(actual); var e = HcDeviceCapabilityCatalog.Find(expected);
        return a is not null && e is not null && (a.DeviceId == e.DeviceId ||
            GeneratedIdentityAliases.Any(pair => pair.Expected == e.DeviceId && pair.Actual == a.DeviceId));
    }
    public static LightingCapabilityOperation Require(string? actual, string? expected, string action,
        string? level = null, bool includesBrightness = false, string? presetId = null)
    {
        var binding = dispatcher.Resolve(actual) ?? throw new NotSupportedException("LIGHTING_CAPABILITY_UNAVAILABLE: " + actual);
        if (expected is not null && !IdentityMatches(actual, expected)) throw new InvalidOperationException("LIGHTING_CAPABILITY_IDENTITY_MISMATCH");
        var c = binding.Consumer; var effect = string.IsNullOrWhiteSpace(level) ? "solid" : level.ToLowerInvariant();
        if (includesBrightness && !c.Brightness) throw new NotSupportedException("LIGHTING_BRIGHTNESS_UNAVAILABLE");
        var allowed = action switch {
            "color" => levels.TryGetValue(effect, out var flag) && c.DeclaredEffects.HasFlag(flag),
            "brightness" => c.Brightness,
            "status" => c.StatusAvailable,
            "off" => c.DeclaredEffects.HasFlag(HcSourceLedEffect.SolidColor),
            "preset" => c.PresetAvailable && presetId is not null && GeneratedPresetIds.Contains(presetId, StringComparer.Ordinal),
            _ => false };
        if (!allowed) throw new NotSupportedException("LIGHTING_OPERATION_UNAVAILABLE: " + action + ":" + effect);
        var independent = c.IndependentCore && (action == "brightness" || action == "color" && (effect is "solid" or "rainbow"));
        return new(binding.Device, c, action, effect, independent ? IndependentProvider : "hc-device-core");
    }
}
