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
    IReadOnlyList<string> Values, string? ProcessorPattern, bool DeclaredByFrozenHc);
public sealed record BatteryCapabilityBinding<T>(BatteryCapabilityDescriptor Descriptor, T Consumer) where T : class;

// This selects only an exact reviewed rule and its actual production consumer. It cannot
// establish battery presence, firmware compatibility, write success, readback or ownership.
public static partial class HcBatteryCapabilityCatalog
{
    public static HcBatteryCapabilityDispatcher<T> Bind<T>(string ruleDocumentSha256, IReadOnlyList<T> consumers,
        Func<T,string> provider, Func<T,string> deviceClass) where T : class
    {
        if (!string.Equals(ruleDocumentSha256, RuleDocumentSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("BATTERY_CAPABILITY_RULE_DOCUMENT_MISMATCH");
        if (consumers.Count != GeneratedRules.Length) throw new InvalidOperationException("BATTERY_CAPABILITY_RULE_COUNT_MISMATCH");
        var bindings = new BatteryCapabilityBinding<T>[consumers.Count];
        for (var i=0;i<bindings.Length;i++)
        {
            var rule=GeneratedRules[i];var consumer=consumers[i];
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
            var r=binding.Descriptor;var value=r.IdentityField=="systemProduct"?system:board;
            if (r.Manufacturers.Any(v=>Norm(v)==m) && r.Values.Any(v=>Norm(v)==value) &&
                (r.ProcessorPattern is null || Regex.IsMatch(processor,r.ProcessorPattern,RegexOptions.IgnoreCase))) return binding;
        }
        return null;
    }
}
