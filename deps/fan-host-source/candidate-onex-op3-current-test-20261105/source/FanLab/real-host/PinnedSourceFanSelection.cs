using System;
using System.Collections.Generic;
using System.Reflection;
using YeManFanBackend.Capabilities;
namespace YeManFanHost;

internal sealed partial class RealHcBackend
{
    private PinnedFanDescriptor? pinnedFanSource;
    public string? SourceModelClass=>pinnedFanSource?.ModelClass;
    public string? SourceCommit=>pinnedFanSource?.SourceCommit;
    // All source control/return/generation consumers use the same catalog owner.
    // Capability declaration grants no write authorization or physical-effect claim.
    private FanCapabilityDecision ObservePinnedSourceFanCapability() => fanCapabilityDecision = new(
        SourceControlConnected,true,SourceControlConnected ? "fixed-source-fan-transport-contract" : "fixed-source-fan-declared-consumer-pending",
        SourceControlConnected ? "固定来源软件消费合同；物理效果未观测" :
            "固定来源已声明FanControl；真实Host曲线消费及外层归还代次尚未接通，写入不可用");
    private bool TrySelectPinnedSourceFanFromMotherboard()
    {
        var board=assembly!.GetType("HandheldCompanion.MotherboardInfo",true)!;
        string? Read(string name)=>board.GetProperty(name,BindingFlags.Public|BindingFlags.Static)?.GetValue(null) as string;
        return SelectPinnedSourceFanIdentity(Read("Manufacturer"),Read("Product"));
    }
    private bool SelectPinnedSourceFanIdentity(string? manufacturer,string? product)
    {
        var selected=FanCapabilityCatalog.MatchPinnedFanSource(manufacturer,product);
        if(selected is null)
        {
            if(pinnedFanSource is not null)throw new InvalidOperationException("PINNED_FAN_IDENTITY_CHANGED");
            return false;
        }
        // A diagnostic type is never authority to bypass the factory's exact
        // motherboard aliases or substitute a different source identity.
        if(!string.IsNullOrWhiteSpace(options.HcDeviceType)&&options.HcDeviceType!=selected.ModelClass)
            throw new InvalidOperationException("PINNED_FAN_DIAGNOSTIC_IDENTITY_MISMATCH");
        if(device is not null)throw new InvalidOperationException("PINNED_FAN_HC_FACTORY_ALREADY_SELECTED");
        if(pinnedFanSource is not null&&!ReferenceEquals(selected,pinnedFanSource))
            throw new InvalidOperationException("PINNED_FAN_IDENTITY_CHANGED");
        pinnedFanSource=selected;
        DeviceIdentity=new Dictionary<string,string>(StringComparer.Ordinal)
        {
            ["manufacturer"]=manufacturer!,["product"]=product!,
            ["capabilityModelClass"]=selected.ModelClass,["capabilityDeviceId"]=selected.DeviceId,
            ["capabilitySourceCommit"]=selected.SourceCommit,["capabilitySourceSha256"]=selected.SourceSha256,
            ["capabilitySourceRepository"]=selected.SourceRepository,["capabilitySourceFile"]=selected.SourceFile,
            ["capabilityTransport"]=selected.Transport.ToString(),["identityKind"]="fixed-source-dmi",
            ["runtimeFactory"]="not-constructed"
        };
        ObservePinnedSourceFanCapability();return true;
    }
    private RealHandshakeResult ReadPinnedSourceFanHandshake()
    {
        var selected=pinnedFanSource??throw new InvalidOperationException("PINNED_FAN_IDENTITY_MISSING");
        var support=ObservePinnedSourceFanCapability();
        return new RealHandshakeResult(support.Supported,FactoryType,Route?.Kind.ToString(),DeviceIdentity,null,null,
            support.Reason,support.Supported,support.Supported && IsAuthorized()){SourceModelClass=selected.ModelClass,SourceCommit=selected.SourceCommit};
    }
    // The original fixture enters the same production selector/handshake, with
    // DMI values only. No runtime assembly, transport or device is created.
    internal bool SelectPinnedSourceFanIdentityForSelfTest(string? manufacturer,string? product)=>
        SelectPinnedSourceFanIdentity(manufacturer,product);
    internal RealHandshakeResult ReadPinnedSourceFanHandshakeForSelfTest()=>ReadPinnedSourceFanHandshake();
}
