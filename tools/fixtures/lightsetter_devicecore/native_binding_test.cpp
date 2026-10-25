#include <array>
#include <iostream>
#include <string>
// Coexist with existing lighting members; no UI or engine headers are needed.
namespace ymcc { namespace lighting { inline bool bindingFixtureNeighbor() { return true; } } }
#include "../../../native/lighting_device_core_binding.h"
#include "../../../native/lighting_device_core_binding.h"

int main() {
    const std::string mini = "HandheldCompanion.Devices.OneXPlayerX1Mini";
    struct Case { std::string device; const char* action; const char* mode; bool expected; };
    const std::array<Case, 30> cases{{
        {mini, "color", "solid", true}, {mini, "color", "rainbow", true},
        {mini, "brightness", "", true}, {mini, "brightness", "solid", true},
        {mini, "brightness", "rainbow", true}, {mini, "brightness", "preset", true},
        {mini, "off", "solid", false}, {mini, "status", "solid", false},
        {mini, "preset", "solid", false}, {mini, "color", "preset", false},
        {mini, "color", "off", false}, {mini, "color", "breathing", false},
        {mini, "color", "wave", false}, {mini, "color", "wheel", false},
        {mini, "color", "gradient", false}, {mini, "color", "ambilight", false},
        {mini, "color", "", false}, {mini, "color", "SOLID", false},
        {mini, "Color", "solid", false}, {mini, "unknown", "rainbow", false},
        {"HandheldCompanion.Devices.OneXPlayerX1", "color", "solid", false},
        {"HandheldCompanion.Devices.OneXPlayerG1", "color", "rainbow", false},
        {"HandheldCompanion.Devices.OneXPlayerOneXFly", "brightness", "", false},
        {"HandheldCompanion.Devices.GPDWinMini", "color", "solid", false},
        {"HandheldCompanion.Devices.AYANEODevice", "brightness", "", false},
        {"OneXPlayerX1Mini", "color", "solid", false},
        {"HandheldCompanion.Devices.OneXPlayerX1MiniUnknown", "color", "rainbow", false},
        {"", "brightness", "", false}, {"unknown", "color", "solid", false},
        {mini + " ", "color", "solid", false},
    }};
    int checked = 0;
    for (const auto& item : cases) {
        if (ymcc::lighting::useIndependentDeviceCore(item.device, item.action, item.mode) != item.expected) {
            std::cerr << "route mismatch at case " << checked << '\n'; return 1;
        }
        ++checked;
    }
    const bool constants =
        std::string(ymcc::lighting::kIndependentDeviceCoreFileName) == "YeManLightDeviceCore.dll" &&
        std::wstring(ymcc::lighting::kIndependentDeviceCorePathSuffix) == L"\\light-setter\\YeManLightDeviceCore.dll" &&
        std::string(ymcc::lighting::kIndependentDeviceCoreSha256) ==
            "88e5a77f781b2bf7e12df4d45b636b16f97031d02e8617572c7bba7176b8fb7e";
    if (!constants || !ymcc::lighting::bindingFixtureNeighbor()) return 2;
    std::cout << "{\"ok\":true,\"routeCases\":" << checked
              << ",\"constantChecks\":3,\"actualProductionHeader\":true,\"hardwareIo\":false}\n";
}
