static const std::vector<std::string>& ymccDeviceClassLightingSet() {
    static const std::vector<std::string> s_set = {
        "HandheldCompanion.Devices.XboxROGAllyX",
        "HandheldCompanion.Devices.XboxROGAlly",
        "HandheldCompanion.Devices.ROGAlly",
        "HandheldCompanion.Devices.ROGAllyX",
        "HandheldCompanion.Devices.LegionGoTablet",
        "HandheldCompanion.Devices.LegionGoTablet2",
        "HandheldCompanion.Devices.LegionGoSZ1",   // 普查补漏（2026-09-16）：继承 LegionGo 灯效（LegionGoSZ1.cs:154-207）
        "HandheldCompanion.Devices.LegionGoSZ2",   // 同上
        "HandheldCompanion.Devices.ClawA1M",
        "HandheldCompanion.Devices.ClawA2VM",
        "HandheldCompanion.Devices.ClawBZ2EM",
        "HandheldCompanion.Devices.ClawCG3EM",
        "HandheldCompanion.Devices.AYANEOSlide",
        "HandheldCompanion.Devices.AYANEOFlipKB",
        "HandheldCompanion.Devices.AYANEO2S",
        "HandheldCompanion.Devices.AYANEO2",
        "HandheldCompanion.Devices.AYANEOAIRPlusAMD",
        // 普查补漏（2026-09-16）：AYANEO CEc 家族全支——CEc.cs:32-34 赋
        // DynamicLighting/Brightness/SecondLEDColor，子类未清除（FlipDS/
        // Flip1SDS 显式 -= 故排除）。
        "HandheldCompanion.Devices.AYANEOAIRPlusIntel",
        "HandheldCompanion.Devices.AYANEOAIRPlusAMDMendocino",
        "HandheldCompanion.Devices.AYANEOAIR1S",
        "HandheldCompanion.Devices.AYANEOAIRLite",
        "HandheldCompanion.Devices.AYANEOAIRPro",
        "HandheldCompanion.Devices.AYANEOKUN",
        "HandheldCompanion.Devices.AYANEOFlip1SKB",
        "HandheldCompanion.Devices.AYANEOAIR",
        "HandheldCompanion.Devices.OneXPlayerX1Mini",
        "HandheldCompanion.Devices.OneXPlayerX1Pro",
        "HandheldCompanion.Devices.OneXPlayerX1AMD",
        // 普查补漏（2026-09-16）：X1 基类 `Capabilities = DynamicLighting`（OneXPlayerX1.cs:82，
        // 赋值式）→ X1/X1Intel 子类继承；G1 基类同（G1.cs:25-26）。
        "HandheldCompanion.Devices.OneXPlayerX1",
        "HandheldCompanion.Devices.OneXPlayerX1Intel",
        "HandheldCompanion.Devices.OneXPlayerApex",
        "HandheldCompanion.Devices.OneXPlayerG1",
        "HandheldCompanion.Devices.OneXPlayerG1AMD",
        "HandheldCompanion.Devices.OneXPlayerG1Intel",
        "HandheldCompanion.Devices.OneXPlayerOneXFlyF1Pro",
        "HandheldCompanion.Devices.OneXPlayerOneXFly",
        "HandheldCompanion.Devices.GamingZone",
        "HandheldCompanion.Devices.LokiMiniPro",
        "HandheldCompanion.Devices.LokiZero",
        // 普查补漏（2026-09-16 灯光审计）：HC IDevice.GetCurrent 对 AYN Loki Max
        // 按 CPU 实例化 LokiMax6600U/6800U（IDevice.cs switch），两者继承
        // AynLoki 的 Light+Solid（AynLoki.cs:54-58）。
        "HandheldCompanion.Devices.LokiMax6600U",
        "HandheldCompanion.Devices.LokiMax6800U",
        "HandheldCompanion.Devices.AynLoki",
        "HandheldCompanion.Devices.AOKZOEA1",
        "HandheldCompanion.Devices.AOKZOEA1Pro",
        "HandheldCompanion.Devices.AOKZOEA1X",
        "HandheldCompanion.Devices.AOKZOEA2",
        "HandheldCompanion.Devices.SuiPlay0X1",
    };
    return s_set;
}

static const std::vector<std::string>& ymccDeviceClassSecondColorSet() {
    static const std::vector<std::string> s_set = {
        // AYANEO CEc 族赋 SecondLEDColor（AYANEODeviceCEc.cs:34）
        "HandheldCompanion.Devices.AYANEOSlide",
        "HandheldCompanion.Devices.AYANEOFlipKB",
        "HandheldCompanion.Devices.AYANEO2S",
        "HandheldCompanion.Devices.AYANEO2",
        "HandheldCompanion.Devices.AYANEOAIRPlusAMD",
        // 普查补漏（2026-09-16）：CEc:34 SecondLEDColor 全支
        "HandheldCompanion.Devices.AYANEOAIRPlusIntel",
        "HandheldCompanion.Devices.AYANEOAIRPlusAMDMendocino",
        "HandheldCompanion.Devices.AYANEOAIR1S",
        "HandheldCompanion.Devices.AYANEOAIRLite",
        "HandheldCompanion.Devices.AYANEOAIRPro",
        "HandheldCompanion.Devices.AYANEOKUN",
        "HandheldCompanion.Devices.AYANEOFlip1SKB",
        "HandheldCompanion.Devices.AYANEOAIR",
        // OneXPlayer X1 族赋 SecondLEDColor（OneXPlayerX1.cs:84；X1Mini 继承
        // 未清除，OneXPlayerX1Mini.cs 无覆盖行；普查补漏：X1 基类与 X1Intel）
        "HandheldCompanion.Devices.OneXPlayerX1Mini",
        "HandheldCompanion.Devices.OneXPlayerX1Pro",
        "HandheldCompanion.Devices.OneXPlayerX1AMD",
        "HandheldCompanion.Devices.OneXPlayerX1",
        "HandheldCompanion.Devices.OneXPlayerX1Intel",
        "HandheldCompanion.Devices.OneXPlayerApex",
        "HandheldCompanion.Devices.GamingZone",   // GamingZone.cs:173
        "HandheldCompanion.Devices.SuiPlay0X1",   // 继承 CEc:34
    };
    return s_set;
}

static bool ymccDeviceClassSupportsLighting(const std::string& deviceClass) {
    const auto& set = ymccDeviceClassLightingSet();
    return std::find(set.begin(), set.end(), deviceClass) != set.end();
}

static bool ymccDeviceClassSupportsSecondColor(const std::string& deviceClass) {
    const auto& set = ymccDeviceClassSecondColorSet();
    return std::find(set.begin(), set.end(), deviceClass) != set.end();
}

static std::vector<std::string> ymccDeviceClassLedEffects(const std::string& deviceClass) {
    // ROG 4 系（ROGAlly.cs:120-124 Solid/Breathing/Rainbow/Wheel/Ambilight）
    // 2026-09-29 对齐 HC 权威能力位：ROGAlly.cs:120-124 的 DynamicLightingCapabilities
    // 仅 Solid/Breathing/Rainbow/Wheel/Ambilight，**不含 Wave**；SetLedColor 首行按该位
    // 门禁（:475），DevicePage 亦按该位隐藏 Wave 控件（Views\Pages\DevicePage.xaml.cs:156）。
    // 2026-09-16 曾据 switch 内 `case LEDLevel.Wave`（:492）误加 wave，但该分支因位门禁
    // 不可达 ⇒ 属"广告不可用模式"，与 MSI 误报同类。删除之，保持能力集真实。
    if (deviceClass == "HandheldCompanion.Devices.XboxROGAllyX" ||
        deviceClass == "HandheldCompanion.Devices.XboxROGAlly" ||
        deviceClass == "HandheldCompanion.Devices.ROGAlly" ||
        deviceClass == "HandheldCompanion.Devices.ROGAllyX")
        return {"solid", "breathing", "rainbow", "wheel", "ambilight"};
    // LegionGo Table 系 + SZ1/SZ2（LegionGo.cs:269-272；LegionGoSZ1.cs:154-207 同 4 效）
    if (deviceClass == "HandheldCompanion.Devices.LegionGoTablet" ||
        deviceClass == "HandheldCompanion.Devices.LegionGoTablet2" ||
        deviceClass == "HandheldCompanion.Devices.LegionGoSZ1" ||
        deviceClass == "HandheldCompanion.Devices.LegionGoSZ2")
        return {"solid", "breathing", "rainbow", "wheel"};
    // MSI Claw 4 系（ClawA1M.cs:224-225）
    if (deviceClass == "HandheldCompanion.Devices.ClawA1M" ||
        deviceClass == "HandheldCompanion.Devices.ClawA2VM" ||
        deviceClass == "HandheldCompanion.Devices.ClawBZ2EM" ||
        deviceClass == "HandheldCompanion.Devices.ClawCG3EM")
        return {"solid", "ambilight"};
    // AYANEO CEc 系（AYANEODeviceCEc.cs:39 Solid；普查补漏 2026-09-16 全支）
    if (deviceClass == "HandheldCompanion.Devices.AYANEOSlide" ||
        deviceClass == "HandheldCompanion.Devices.AYANEOFlipKB" ||
        deviceClass == "HandheldCompanion.Devices.AYANEO2S" ||
        deviceClass == "HandheldCompanion.Devices.AYANEO2" ||
        deviceClass == "HandheldCompanion.Devices.AYANEOAIRPlusAMD" ||
        deviceClass == "HandheldCompanion.Devices.AYANEOAIRPlusIntel" ||
        deviceClass == "HandheldCompanion.Devices.AYANEOAIRPlusAMDMendocino" ||
        deviceClass == "HandheldCompanion.Devices.AYANEOAIR1S" ||
        deviceClass == "HandheldCompanion.Devices.AYANEOAIRLite" ||
        deviceClass == "HandheldCompanion.Devices.AYANEOAIRPro" ||
        deviceClass == "HandheldCompanion.Devices.AYANEOKUN" ||
        deviceClass == "HandheldCompanion.Devices.AYANEOFlip1SKB" ||
        deviceClass == "HandheldCompanion.Devices.AYANEOAIR" ||
        deviceClass == "HandheldCompanion.Devices.SuiPlay0X1")
        return {"solid"};
    // OneXPlayer X1 系（OneXPlayerX1.cs:87-88；普查补漏：X1 基类与 X1Intel）
    if (deviceClass == "HandheldCompanion.Devices.OneXPlayerX1Pro" ||
        deviceClass == "HandheldCompanion.Devices.OneXPlayerX1AMD" ||
        deviceClass == "HandheldCompanion.Devices.OneXPlayerX1" ||
        deviceClass == "HandheldCompanion.Devices.OneXPlayerX1Intel" ||
        deviceClass == "HandheldCompanion.Devices.OneXPlayerApex")
        return {"solid", "preset"};
    // X1Mini（OneXPlayerX1Mini.cs:32-34 追加 Rainbow）
    if (deviceClass == "HandheldCompanion.Devices.OneXPlayerX1Mini")
        return {"solid", "rainbow", "preset"};
    // G1 / OneXFly（G1.cs:29-30 / OneXFly.cs:59-60；普查补漏：G1 基类与 G1Intel）
    if (deviceClass == "HandheldCompanion.Devices.OneXPlayerG1" ||
        deviceClass == "HandheldCompanion.Devices.OneXPlayerG1AMD" ||
        deviceClass == "HandheldCompanion.Devices.OneXPlayerG1Intel" ||
        deviceClass == "HandheldCompanion.Devices.OneXPlayerOneXFlyF1Pro" ||
        deviceClass == "HandheldCompanion.Devices.OneXPlayerOneXFly")
        return {"solid", "rainbow"};
    // Zotac（GamingZone.cs:176-179）
    if (deviceClass == "HandheldCompanion.Devices.GamingZone")
        return {"solid", "breathing", "rainbow", "gradient"};
    // AYN Loki（AynLoki.cs:58；LokiMax6600U/6800U 继承同源——灯光审计补漏 2026-09-16）
    if (deviceClass == "HandheldCompanion.Devices.LokiMiniPro" ||
        deviceClass == "HandheldCompanion.Devices.LokiZero" ||
        deviceClass == "HandheldCompanion.Devices.LokiMax6600U" ||
        deviceClass == "HandheldCompanion.Devices.LokiMax6800U" ||
        deviceClass == "HandheldCompanion.Devices.AynLoki")
        return {"solid"};
    // AOKZOE（AOKZOEA1.cs:57-58）
    if (deviceClass == "HandheldCompanion.Devices.AOKZOEA1" ||
        deviceClass == "HandheldCompanion.Devices.AOKZOEA1Pro" ||
        deviceClass == "HandheldCompanion.Devices.AOKZOEA1X" ||
        deviceClass == "HandheldCompanion.Devices.AOKZOEA2")
        return {"solid", "rainbow"};
    return {};
}
