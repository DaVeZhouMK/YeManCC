// Generated from capabilities/hc-device-catalog.json and hc-lighting-policy.json.
// Declared capabilities and implemented operations are separate. No device access.
#pragma once
#include <array>
#include <string_view>
#include "../capabilities/hc-device-catalog.generated.h"
namespace ymcc::lighting {
inline constexpr char kCatalogSha256[] = "2b1f4189623fa73ebb828ee7f4fcb368b573982df39ee06646b51668001683ce";
inline constexpr const char* kExtensionLightingClasses[] = {"HandheldCompanion.Devices.OneXPlayer3", "HandheldCompanion.Devices.AYANEO3", "HandheldCompanion.Devices.AYANEONEXT2", "HandheldCompanion.Devices.OneXPlayerX2MiniPro"};
inline bool identityMatches(std::string_view actual, std::string_view expected) noexcept {
    for (const char* cls : kExtensionLightingClasses) if (actual == cls && expected == cls) return true;
    const auto* a=ymcc::capabilities::find(actual); const auto* e=ymcc::capabilities::find(expected);
    if (!a || !e) return false;
    if (a->deviceId == e->deviceId) return true;
    if (e->deviceId == "hc.device.AynLoki") return a->deviceId == "hc.device.LokiMax6600U" || a->deviceId == "hc.device.LokiMax6800U" || a->deviceId == "hc.device.LokiMiniPro" || a->deviceId == "hc.device.LokiZero";
    if (e->deviceId == "hc.device.OneXPlayerG1") return a->deviceId == "hc.device.OneXPlayerG1AMD" || a->deviceId == "hc.device.OneXPlayerG1Intel";
    if (e->deviceId == "hc.device.OneXPlayerX1") return a->deviceId == "hc.device.OneXPlayerApex" || a->deviceId == "hc.device.OneXPlayerX1AMD" || a->deviceId == "hc.device.OneXPlayerX1Intel" || a->deviceId == "hc.device.OneXPlayerX1Mini" || a->deviceId == "hc.device.OneXPlayerX1Pro";
    return false;
}
struct Preset { const char* id; const char* label; int hcValue; };
inline constexpr std::array<Preset, 11> kOneXPresets = {{
    {"LEDPreset_OneXPlayerX1_Preset01", "预设 1", 0xd},
    {"LEDPreset_OneXPlayerX1_Preset02", "预设 2", 0x3},
    {"LEDPreset_OneXPlayerX1_Preset03", "预设 3", 0xb},
    {"LEDPreset_OneXPlayerX1_Preset04", "预设 4", 0x5},
    {"LEDPreset_OneXPlayerX1_Preset05", "预设 5", 0x7},
    {"LEDPreset_OneXPlayerX1_Preset06", "预设 6", 0x9},
    {"LEDPreset_OneXPlayerX1_Preset07", "预设 7", 0xc},
    {"LEDPreset_OneXPlayerX1_Preset08", "预设 8", 0x14},
    {"LEDPreset_OneXPlayerX1_Preset09", "预设 9", 0x1e3},
    {"LEDPreset_OneXPlayerX1_Preset10", "预设 10", 0x1},
    {"LEDPreset_OneXPlayerX1_Preset11", "预设 11", 0x8},
 }};
enum class NativeProvider : unsigned char { Helper, RogAura, MsiHid };
enum class SourceAdapter : unsigned char { FrozenHc, Oxp3Hid, SuperJoyCec, OneXX2MiniHid };
struct Route {bool registered, brightness, preset, declaredPresetWithoutTransport; NativeProvider provider; unsigned colorEffects; bool fixedSpeed = false, presetSecondColor = false;};
// Aligned to the shared identity table: one binary identity lookup, then constant-time operation/provider selection.
inline constexpr std::array<Route, 92> routes = {{
    {true, true, false, false, NativeProvider::Helper, 5, false, false}, // AOKZOEA1
    {true, true, false, false, NativeProvider::Helper, 5, false, false}, // AOKZOEA1Pro
    {true, true, false, false, NativeProvider::Helper, 5, false, false}, // AOKZOEA1X
    {true, true, false, false, NativeProvider::Helper, 5, false, false}, // AOKZOEA2
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEO2
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEO2S
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOAIR
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOAIR1S
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOAIRLite
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOAIRPlusAMD
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOAIRPlusAMDMendocino
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOAIRPlusIntel
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOAIRPro
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOFlip1SKB
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOFlipKB
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOKUN
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // AYANEOSlide
    {true, false, false, false, NativeProvider::Helper, 1, false, false}, // AynLoki
    {true, true, false, false, NativeProvider::MsiHid, 65, false, false}, // ClawA1M
    {true, true, false, false, NativeProvider::MsiHid, 65, false, false}, // ClawA2VM
    {true, true, false, false, NativeProvider::MsiHid, 65, false, false}, // ClawBZ2EM
    {true, true, false, false, NativeProvider::MsiHid, 65, false, false}, // ClawCG3EM
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 39, false, false}, // GamingZone
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 23, false, false}, // LegionGoSZ1
    {true, true, false, false, NativeProvider::Helper, 23, false, false}, // LegionGoSZ2
    {true, true, false, false, NativeProvider::Helper, 23, false, false}, // LegionGoTablet
    {true, true, false, false, NativeProvider::Helper, 23, false, false}, // LegionGoTablet2
    {true, false, false, false, NativeProvider::Helper, 1, false, false}, // LokiMax6600U
    {true, false, false, false, NativeProvider::Helper, 1, false, false}, // LokiMax6800U
    {true, false, false, false, NativeProvider::Helper, 1, false, false}, // LokiMiniPro
    {true, false, false, false, NativeProvider::Helper, 1, false, false}, // LokiZero
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, true, false, NativeProvider::Helper, 1, false, false}, // OneXPlayerApex
    {true, true, false, false, NativeProvider::Helper, 5, false, false}, // OneXPlayerG1
    {true, true, false, false, NativeProvider::Helper, 5, false, false}, // OneXPlayerG1AMD
    {true, true, false, false, NativeProvider::Helper, 5, false, false}, // OneXPlayerG1Intel
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 5, false, false}, // OneXPlayerOneXFly
    {true, true, false, false, NativeProvider::Helper, 5, false, false}, // OneXPlayerOneXFlyF1Pro
    {true, true, true, false, NativeProvider::Helper, 1, false, false}, // OneXPlayerX1
    {true, true, true, false, NativeProvider::Helper, 1, false, false}, // OneXPlayerX1AMD
    {true, true, true, false, NativeProvider::Helper, 1, false, false}, // OneXPlayerX1Intel
    {true, true, false, true, NativeProvider::Helper, 5, false, false}, // OneXPlayerX1Mini
    {true, true, true, false, NativeProvider::Helper, 1, false, false}, // OneXPlayerX1Pro
    {true, true, false, false, NativeProvider::RogAura, 87, false, false}, // ROGAlly
    {true, true, false, false, NativeProvider::RogAura, 87, false, false}, // ROGAllyX
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1, false, false}, // SuiPlay0X1
    {true, true, false, false, NativeProvider::RogAura, 87, false, false}, // XboxROGAlly
    {true, true, false, false, NativeProvider::RogAura, 87, false, false}, // XboxROGAllyX
 }};
inline const Route* resolve(const ymcc::capabilities::Device* device) noexcept {
    if (!device) return nullptr;
    const auto index = static_cast<std::size_t>(device - ymcc::capabilities::devices.data());
    return index < routes.size() && routes[index].registered ? &routes[index] : nullptr;
}
inline constexpr std::array<const char*, 10> kOneXPlayer3PresetIds = {"LEDPreset_OneXPlayerX1_Preset01", "LEDPreset_OneXPlayerX1_Preset02", "LEDPreset_OneXPlayerX1_Preset03", "LEDPreset_OneXPlayerX1_Preset04", "LEDPreset_OneXPlayerX1_Preset05", "LEDPreset_OneXPlayerX1_Preset06", "LEDPreset_OneXPlayerX1_Preset07", "LEDPreset_OneXPlayerX1_Preset09", "LEDPreset_OneXPlayerX1_Preset10", "LEDPreset_OneXPlayerX1_Preset11"};
inline constexpr std::array<const char*, 0> kAYANEO3PresetIds = {};
inline constexpr std::array<const char*, 0> kAYANEONEXT2PresetIds = {};
inline constexpr std::array<const char*, 0> kOneXPlayerX2MiniProPresetIds = {};
struct ExtensionRoute {const char* deviceClass; const char* deviceId; const char* const* presetIds; int presetCount; SourceAdapter sourceAdapter; Route route;};
inline constexpr std::array<ExtensionRoute, 4> extensionRoutes = {{
    {"HandheldCompanion.Devices.OneXPlayer3", "ymcc.device.OneXPlayer3", kOneXPlayer3PresetIds.data(), 10, SourceAdapter::Oxp3Hid, {true, true, true, false, NativeProvider::Helper, 3, true, true}}, // OneXPlayer3
    {"HandheldCompanion.Devices.AYANEO3", "ymcc.device.AYANEO3", kAYANEO3PresetIds.data(), 0, SourceAdapter::SuperJoyCec, {true, true, false, false, NativeProvider::Helper, 1, false, false}}, // AYANEO3
    {"HandheldCompanion.Devices.AYANEONEXT2", "ymcc.device.AYANEONEXT2", kAYANEONEXT2PresetIds.data(), 0, SourceAdapter::SuperJoyCec, {true, true, false, false, NativeProvider::Helper, 1, false, false}}, // AYANEONEXT2
    {"HandheldCompanion.Devices.OneXPlayerX2MiniPro", "ymcc.device.OneXPlayerX2MiniPro", kOneXPlayerX2MiniProPresetIds.data(), 0, SourceAdapter::OneXX2MiniHid, {true, true, false, true, NativeProvider::Helper, 3, true, false}}, // OneXPlayerX2MiniPro
}};
inline SourceAdapter sourceAdapter(std::string_view cls) noexcept {
    for (const auto& row : extensionRoutes) if (cls == row.deviceClass) return row.sourceAdapter;
    return SourceAdapter::FrozenHc;
}
inline bool usesOneXVendorOwner(std::string_view cls) noexcept {
    const auto adapter = sourceAdapter(cls);
    return adapter == SourceAdapter::Oxp3Hid || adapter == SourceAdapter::OneXX2MiniHid;
}
inline const Route* resolve(std::string_view cls) noexcept {
    const auto* frozen=resolve(ymcc::capabilities::find(cls));
    if (frozen) return frozen;
    for (const auto& row : extensionRoutes) if (cls == row.deviceClass) return &row.route;
    return nullptr;
}
inline const char* deviceId(std::string_view cls) noexcept {
    const auto* device=ymcc::capabilities::find(cls);
    if (device) return device->deviceId.data();
    for (const auto& row : extensionRoutes) if (cls == row.deviceClass) return row.deviceId;
    return "";
}
inline bool usesRogAura(std::string_view cls) noexcept {const auto* r=resolve(cls);return r && r->provider==NativeProvider::RogAura;}
inline bool usesMsiNativeHid(std::string_view cls) noexcept {const auto* r=resolve(cls);return r && r->provider==NativeProvider::MsiHid;}
inline bool supportsColorEffect(const Route& route, std::string_view mode) noexcept {
    if (mode == "solid") return (route.colorEffects & 1u) != 0;
    if (mode == "breathing") return (route.colorEffects & 2u) != 0;
    if (mode == "rainbow") return (route.colorEffects & 4u) != 0;
    if (mode == "wave") return (route.colorEffects & 8u) != 0;
    if (mode == "wheel") return (route.colorEffects & 16u) != 0;
    if (mode == "gradient") return (route.colorEffects & 32u) != 0;
    if (mode == "ambilight") return (route.colorEffects & 64u) != 0;
    return false;
}
inline bool supportsColorEffect(std::string_view cls, std::string_view mode) noexcept {const auto* r=resolve(cls);return r && supportsColorEffect(*r,mode);}
inline bool presetImplemented(std::string_view cls) noexcept {const auto* r=resolve(cls);return r && r->preset;}
inline bool presetDeclaredButUnavailable(std::string_view cls) noexcept {const auto* r=resolve(cls);return r && r->declaredPresetWithoutTransport;}
inline bool brightnessImplemented(std::string_view cls) noexcept {const auto* r=resolve(cls);return r && r->brightness;}
inline bool validPreset(const Route& route, std::string_view id) noexcept {
    if (!route.preset) return false;
    for (const auto& preset : kOneXPresets) if (id == preset.id) return true;
    return false;
}
inline bool validPreset(std::string_view cls, std::string_view id) noexcept {
    const auto* r=resolve(cls); if (!r || !r->preset) return false;
    for (const auto& row : extensionRoutes) if (cls == row.deviceClass) {
        for (int i=0;i<row.presetCount;i++) if (id == row.presetIds[i]) return true;
        return false;
    }
    return validPreset(*r,id);
}
} // namespace ymcc::lighting
