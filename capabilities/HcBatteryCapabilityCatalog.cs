// AI-NAV[battery]: hc-battery-policy.json and native/BatteryHost/battery-routes.json are generator inputs.
// Policy.ResolveCapability/Controller in native/BatteryHost own the existing charge workflow; see AI-INDEX.json domains[battery].
// A bound identity rule does not establish firmware compatibility, write/readback success or release ownership.
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text.RegularExpressions;
namespace YeManFanBackend.Capabilities;

public sealed record BatteryCapabilityDescriptor(string RuleId, string DeviceId, string DeviceClass,
    string Origin, string Provider, string IdentityField, IReadOnlyList<string> Manufacturers,
    IReadOnlyList<string> Values, string? ProcessorPattern, bool DeclaredByFrozenHc,
    string ManufacturerMatch = "normalized-exact", string ValueMatch = "normalized-exact");
public sealed record BatteryCapabilityBinding<T>(BatteryCapabilityDescriptor Descriptor, T Consumer) where T : class;

// This selects a reviewed rule and its actual production consumer. It cannot
// establish battery presence, firmware compatibility, write success, readback or ownership.
public static partial class HcBatteryCapabilityCatalog
{
    private static void ValidatePredicates(BatteryCapabilityDescriptor rule)
    {
        bool defaults = rule.ManufacturerMatch == "normalized-exact" && rule.ValueMatch == "normalized-exact";
        if (defaults && rule.Provider != "oxp-superx-dual-ec" && rule.DeviceClass != "OneXPlayerSuperX") return;
        // The optional predicates admit this one source-bound Linux DMI rule.
        // They do not grant a configurable contains/exact rule to other models.
        if (rule.RuleId == "battery.rule.oxp-superx-dual-ec.OneXPlayerSuperX" &&
            rule.DeviceId == "ymcc.device.OneXPlayerSuperX" && rule.DeviceClass == "OneXPlayerSuperX" &&
            rule.Origin == "reviewed-native-extension" && !rule.DeclaredByFrozenHc &&
            rule.Provider == "oxp-superx-dual-ec" && rule.IdentityField == "boardProduct" &&
            rule.ManufacturerMatch == "ordinal-contains" && rule.ValueMatch == "ordinal-exact" &&
            rule.ProcessorPattern is null && rule.Manufacturers is not null && rule.Values is not null &&
            rule.Manufacturers.SequenceEqual(new[] { "ONE-NETBOOK" }, StringComparer.Ordinal) &&
            rule.Values.SequenceEqual(new[] { "ONEXPLAYER SUPER X" }, StringComparer.Ordinal)) return;
        throw new InvalidOperationException("UNREVIEWED_BATTERY_IDENTITY_PREDICATE: " + rule.RuleId);
    }

    public static HcBatteryCapabilityDispatcher<T> Bind<T>(string ruleDocumentSha256, IReadOnlyList<T> consumers,
        Func<T,string> provider, Func<T,string> deviceClass) where T : class
    {
        if (!string.Equals(ruleDocumentSha256, RuleDocumentSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("BATTERY_CAPABILITY_RULE_DOCUMENT_MISMATCH");
        if (consumers.Count != GeneratedRules.Length) throw new InvalidOperationException("BATTERY_CAPABILITY_RULE_COUNT_MISMATCH");
        var bindings = new BatteryCapabilityBinding<T>[consumers.Count];
        var keys = new HashSet<string>(StringComparer.Ordinal);
        for (var i=0;i<bindings.Length;i++)
        {
            var rule=GeneratedRules[i];var consumer=consumers[i];
            ValidatePredicates(rule);
            if (!keys.Add(rule.RuleId)) throw new InvalidOperationException("DUPLICATE_BATTERY_RULE: " + rule.RuleId);
            if (consumer is null || provider(consumer)!=rule.Provider || deviceClass(consumer)!=rule.DeviceClass)
                throw new InvalidOperationException("BATTERY_CAPABILITY_CONSUMER_MISMATCH: "+rule.RuleId);
            if (rule.Origin=="frozen-HC-identity" && HcDeviceCapabilityCatalog.Find(rule.DeviceId)?.ConsumerClass != "HandheldCompanion.Devices."+rule.DeviceClass)
                throw new InvalidOperationException("BATTERY_CAPABILITY_FROZEN_IDENTITY_MISMATCH: "+rule.RuleId);
            var immutable=rule with {Manufacturers=Array.AsReadOnly(rule.Manufacturers.ToArray()),Values=Array.AsReadOnly(rule.Values.ToArray())};
            bindings[i]=new(immutable,consumer);
        }
        return new(bindings);
    }
}
public sealed class HcBatteryCapabilityDispatcher<T> where T : class
{
    private readonly IReadOnlyList<BatteryCapabilityBinding<T>> bindings;
    public IReadOnlyList<BatteryCapabilityBinding<T>> Bindings => bindings;
    internal HcBatteryCapabilityDispatcher(BatteryCapabilityBinding<T>[] bindings) => this.bindings=Array.AsReadOnly(bindings);
    public BatteryCapabilityBinding<T>? Resolve(string manufacturer,string boardProduct,string systemProduct,string processor)
    {
        static string Norm(string value)=>value.Trim().ToUpperInvariant();
        var m=Norm(manufacturer);var board=Norm(boardProduct);var system=Norm(systemProduct);
        foreach(var binding in bindings)
        {
            var r=binding.Descriptor;
            var value=r.IdentityField=="systemProduct"?system:board;
            var rawValue=r.IdentityField=="systemProduct"?systemProduct:boardProduct;
            bool manufacturerMatches=r.ManufacturerMatch=="ordinal-contains"
                ? r.Manufacturers.Any(v=>manufacturer.Contains(v,StringComparison.Ordinal))
                : r.Manufacturers.Any(v=>Norm(v)==m);
            bool valueMatches=r.ValueMatch=="ordinal-exact"
                ? r.Values.Any(v=>string.Equals(v,rawValue,StringComparison.Ordinal))
                : r.Values.Any(v=>Norm(v)==value);
            if (manufacturerMatches && valueMatches &&
                (r.ProcessorPattern is null || Regex.IsMatch(processor,r.ProcessorPattern,RegexOptions.IgnoreCase))) return binding;
        }
        return null;
    }
}
