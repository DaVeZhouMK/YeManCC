// Generated from capabilities/hc-device-catalog.json and hc-lighting-policy.json.
// Declared capabilities and implemented operations are separate. No device access.
#pragma once
#include <array>
#include <string_view>
#include "../capabilities/hc-device-catalog.generated.h"
namespace ymcc::lighting {
inline constexpr char kCatalogSha256[] = "56fcc0dcaed4376ead15956aeb9f05ce52e3552b782497617a7691370878f45e";
inline bool identityMatches(std::string_view actual, std::string_view expected) noexcept {
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
struct Route {bool registered, brightness, preset, declaredPresetWithoutTransport; NativeProvider provider; unsigned colorEffects;};
// Aligned to the shared identity table: one binary identity lookup, then constant-time operation/provider selection.
inline constexpr std::array<Route, 92> routes = {{
    {true, true, false, false, NativeProvider::Helper, 5}, // AOKZOEA1
    {true, true, false, false, NativeProvider::Helper, 5}, // AOKZOEA1Pro
    {true, true, false, false, NativeProvider::Helper, 5}, // AOKZOEA1X
    {true, true, false, false, NativeProvider::Helper, 5}, // AOKZOEA2
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEO2
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEO2S
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOAIR
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOAIR1S
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOAIRLite
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOAIRPlusAMD
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOAIRPlusAMDMendocino
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOAIRPlusIntel
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOAIRPro
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOFlip1SKB
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOFlipKB
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOKUN
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1}, // AYANEOSlide
    {true, false, false, false, NativeProvider::Helper, 1}, // AynLoki
    {true, true, false, false, NativeProvider::MsiHid, 65}, // ClawA1M
    {true, true, false, false, NativeProvider::MsiHid, 65}, // ClawA2VM
    {true, true, false, false, NativeProvider::MsiHid, 65}, // ClawBZ2EM
    {true, true, false, false, NativeProvider::MsiHid, 65}, // ClawCG3EM
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
    {true, true, false, false, NativeProvider::Helper, 39}, // GamingZone
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 23}, // LegionGoSZ1
    {true, true, false, false, NativeProvider::Helper, 23}, // LegionGoSZ2
    {true, true, false, false, NativeProvider::Helper, 23}, // LegionGoTablet
    {true, true, false, false, NativeProvider::Helper, 23}, // LegionGoTablet2
    {true, false, false, false, NativeProvider::Helper, 1}, // LokiMax6600U
    {true, false, false, false, NativeProvider::Helper, 1}, // LokiMax6800U
    {true, false, false, false, NativeProvider::Helper, 1}, // LokiMiniPro
    {true, false, false, false, NativeProvider::Helper, 1}, // LokiZero
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, true, false, NativeProvider::Helper, 1}, // OneXPlayerApex
    {true, true, false, false, NativeProvider::Helper, 5}, // OneXPlayerG1
    {true, true, false, false, NativeProvider::Helper, 5}, // OneXPlayerG1AMD
    {true, true, false, false, NativeProvider::Helper, 5}, // OneXPlayerG1Intel
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 5}, // OneXPlayerOneXFly
    {true, true, false, false, NativeProvider::Helper, 5}, // OneXPlayerOneXFlyF1Pro
    {true, true, true, false, NativeProvider::Helper, 1}, // OneXPlayerX1
    {true, true, true, false, NativeProvider::Helper, 1}, // OneXPlayerX1AMD
    {true, true, true, false, NativeProvider::Helper, 1}, // OneXPlayerX1Intel
    {true, true, false, true, NativeProvider::Helper, 5}, // OneXPlayerX1Mini
    {true, true, true, false, NativeProvider::Helper, 1}, // OneXPlayerX1Pro
    {true, true, false, false, NativeProvider::RogAura, 87}, // ROGAlly
    {true, true, false, false, NativeProvider::RogAura, 87}, // ROGAllyX
    {false, false, false, false, NativeProvider::Helper, 0},
    {true, true, false, false, NativeProvider::Helper, 1}, // SuiPlay0X1
    {true, true, false, false, NativeProvider::RogAura, 87}, // XboxROGAlly
    {true, true, false, false, NativeProvider::RogAura, 87}, // XboxROGAllyX
 }};
inline const Route* resolve(const ymcc::capabilities::Device* device) noexcept {
    if (!device) return nullptr;
    const auto index = static_cast<std::size_t>(device - ymcc::capabilities::devices.data());
    return index < routes.size() && routes[index].registered ? &routes[index] : nullptr;
}
inline const Route* resolve(std::string_view cls) noexcept {return resolve(ymcc::capabilities::find(cls));}
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
inline bool validPreset(std::string_view cls, std::string_view id) noexcept {const auto* r=resolve(cls);return r && validPreset(*r,id);}
} // namespace ymcc::lighting
