// FAN-942: compatibility is limited to HC's original inherited EC writer. No device I/O.
namespace YeManFanHost;
internal sealed record FanCapabilityDecision(bool Supported, bool? Declared, string Evidence, string Reason);
internal sealed record HcEcFanContract(string FactoryType, bool UseOpenLib, int ControlAddress,
    int DutyAddress, int CommandPort, int DataPort, int Min, int Max,
    string? ControlDeclaringType, string? DutyDeclaringType);
internal static class FanCapabilityPolicy
{
    private const string EcWriter = "HandheldCompanion.Devices.OneXAOKZOE";
    private const string ApexFactory = "HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex";
    internal static bool IsX1Factory(string? type) => type is
        "HandheldCompanion.Devices.OneXPlayerX1Mini" or "HandheldCompanion.Devices.OneXPlayerX1AMD" or
        "HandheldCompanion.Devices.OneXPlayerX1Intel" or "HandheldCompanion.Devices.OneXPlayerX1Pro" or ApexFactory;
    private static bool IsG1Factory(string? type) => type is
        "HandheldCompanion.Devices.OneXPlayerG1AMD" or "HandheldCompanion.Devices.OneXPlayerG1Intel";
    internal static bool NeedsEcContract(string? type) => IsX1Factory(type) || IsG1Factory(type);
    private static bool HasUsableInheritedEcContract(HcEcFanContract? contract) => contract is not null
        && contract.UseOpenLib && contract.ControlAddress is > 0 and <= ushort.MaxValue
        && contract.DutyAddress is > 0 and <= ushort.MaxValue && contract.ControlAddress != contract.DutyAddress
        && contract.CommandPort is > 0 and <= ushort.MaxValue && contract.DataPort is > 0 and <= ushort.MaxValue
        && contract.CommandPort != contract.DataPort && contract.Min >= 0 && contract.Max <= byte.MaxValue
        && contract.Max > contract.Min && contract.ControlDeclaringType == EcWriter && contract.DutyDeclaringType == EcWriter;
    internal static FanCapabilityDecision Evaluate(bool routeKnown, bool? declared, HcEcFanContract? contract,
        string? factoryType = null)
    {
        if (!routeKnown) return new(false, declared, "route-unmapped", "HC factory 类型不在 YeMan 风扇设备矩阵内");
        // FAN-942 R3: G1 must match its model-specific upstream contract in full.
        // Empty/partial/conflicting data stays rejected even when FanControl is declared.
        var factory = factoryType ?? contract?.FactoryType;
        if (IsG1Factory(factory))
            return declared == true && contract?.FactoryType == factory && G1FanContractAdapter.Matches(contract)
                ? new(true, true, "hc-g1-upstream-ec-contract", "G1 已匹配上游机型专属 EC 契约并沿用 HC 继承 writer（物理未验证）")
                : new(false, declared, "hc-inherited-ec-contract-incomplete", "当前 G1 未匹配完整的机型专属上游 EC 写入契约");
        if (declared == true) return new(true, true, "hc-fan-control-declared", "HC 已声明 FanControl 能力");
        // The legacy implementation accepts mapped routes when HC exposes no capability field.
        if (declared is null) return new(true, null, "legacy-route-capability-unavailable", "HC 路线已登记，能力字段不可读");
        var compatible = contract is not null && IsX1Factory(factory) && contract.FactoryType == factory
            && HasUsableInheritedEcContract(contract) && contract.ControlAddress == 0x44A && contract.DutyAddress == 0x44B
            && contract.CommandPort == 0x4E && contract.DataPort == 0x4F && contract.Min == 0 && contract.Max == 184;
        return compatible
            ? new(true, false, "hc-x1-inherited-ec-contract", "X1 继承链灯光声明覆盖风扇位；兼容 HC 原有继承 EC 风扇路线（物理未验证）")
            : new(false, false, "hc-fan-control-rejected", "HC 当前设备未声明 FanControl 能力，且未匹配已知兼容契约");
    }
    internal static IReadOnlyList<string> RunSelfTest()
    {
        var checks = new List<string>();
        void Check(bool value, string name) { if (!value) throw new InvalidOperationException(name); checks.Add(name); }
        var source = new HcEcFanContract("HandheldCompanion.Devices.OneXPlayerX1Mini", true,
            0x44A, 0x44B, 0x4E, 0x4F, 0, 184, EcWriter, EcWriter);
        foreach (var name in new[] { "OneXPlayerX1Mini", "OneXPlayerX1AMD", "OneXPlayerX1Intel", "OneXPlayerX1Pro" })
            Check(Evaluate(true, false, source with { FactoryType = "HandheldCompanion.Devices." + name }).Supported, "fan942-x1-" + name);
        Check(Evaluate(true, false, source with { FactoryType = ApexFactory }).Supported, "fan942-apex-real-namespace-inherited-contract");
        Check(!Evaluate(true, false, source with { FactoryType = "HandheldCompanion.Devices.OneXPlayerApex" }).Supported, "fan942-apex-wrong-namespace-rejected");
        Check(!Evaluate(true, false, source, ApexFactory).Supported, "fan942-factory-contract-mismatch-rejected");
        Check(Evaluate(true, true, null).Supported, "fan942-declared-fan-unchanged");
        Check(Evaluate(true, null, null).Supported, "fan942-legacy-metadata-unchanged");
        Check(!Evaluate(false, true, source).Supported, "fan942-unmapped-rejected");
        Check(!Evaluate(true, false, source with { FactoryType = "HandheldCompanion.Devices.OneXPlayer2" }).Supported, "fan942-no-family-wide-bypass");
        Check(!Evaluate(true, false, source with { UseOpenLib = false }).Supported, "fan942-openlib-required");
        Check(!Evaluate(true, false, source with { ControlAddress = 0 }).Supported, "fan942-control-address-required");
        Check(!Evaluate(true, false, source with { DutyAddress = 0 }).Supported, "fan942-duty-address-required");
        Check(!Evaluate(true, false, source with { DataPort = 0 }).Supported, "fan942-port-required");
        Check(!Evaluate(true, false, source with { Max = 255 }).Supported, "fan942-range-required");
        Check(!Evaluate(true, false, source with { DutyDeclaringType = "different-writer" }).Supported, "fan942-writer-override-rejected");
        Check(!Evaluate(true, false, null).Supported, "fan942-missing-contract-rejected");
        Check(Evaluate(true, false, source).Declared == false, "fan942-hc-declaration-not-forged");
        foreach (var name in new[] { "AOKZOEA1", "AOKZOEA1Pro", "AOKZOEA1X", "AOKZOEA2", "OneXPlayer2", "OneXPlayer2Pro",
            "OneXPlayerMiniAMD", "OneXPlayerMiniIntel", "OneXPlayerMiniPro", "OneXPlayerOneXFly", "OneXPlayerOneXFlyF1Pro" })
            Check(Evaluate(true, true, null, "HandheldCompanion.Devices." + name).Supported, "fan942-family-declared-unchanged-" + name);
        foreach (var name in new[] { "OneXPlayerG1AMD", "OneXPlayerG1Intel" })
        {
            var factory = "HandheldCompanion.Devices." + name;
            var empty = source with { FactoryType = factory, ControlAddress = 0, DutyAddress = 0, CommandPort = 0, DataPort = 0, Min = 0, Max = 0 };
            var rejected = Evaluate(true, true, empty, factory);
            Check(!rejected.Supported && rejected.Declared == true && rejected.Evidence == "hc-inherited-ec-contract-incomplete", "fan942-g1-empty-writer-" + name);
            Check(!Evaluate(true, true, null, factory).Supported, "fan942-g1-missing-contract-" + name);
            Check(!Evaluate(true, true, empty with { ControlAddress = 0x44A }, factory).Supported, "fan942-g1-partial-contract-" + name);
            var expected = G1FanContractAdapter.Expected(factory)!;
            Check(Evaluate(true, true, expected, factory).Supported, "fan942-g1-completed-supported-" + name);
            Check(!Evaluate(true, false, expected, factory).Supported, "fan942-g1-capability-false-rejected-" + name);
            Check(!Evaluate(true, null, expected, factory).Supported, "fan942-g1-capability-null-rejected-" + name);
            Check(!Evaluate(true, true, expected with { Max = expected.Max == 255 ? 184 : 255 }, factory).Supported,
                "fan942-g1-cross-model-range-rejected-" + name);
        }
        return checks;
    }
}