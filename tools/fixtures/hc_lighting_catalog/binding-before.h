#pragma once
#include <string>

namespace before {
namespace lighting {

inline constexpr char kIndependentDeviceCoreFileName[] = "YeManLightDeviceCore.dll";
// Append to POWER_CONTROL_DIR; the caller owns packaging and absolute path resolution.
inline constexpr wchar_t kIndependentDeviceCorePathSuffix[] =
    L"\\light-setter\\YeManLightDeviceCore.dll";
inline constexpr char kIndependentDeviceCoreSha256[] =
    "88e5a77f781b2bf7e12df4d45b636b16f97031d02e8617572c7bba7176b8fb7e";

// Native calls use canonical lowercase actions/modes. This deliberately narrow
// gate runs before any HC-file requirement. Brightness does not select an effect.
inline bool useIndependentDeviceCore(const std::string& deviceClass,
                                     const std::string& action,
                                     const std::string& mode) {
    if (deviceClass != "HandheldCompanion.Devices.OneXPlayerX1Mini") return false;
    return action == "brightness" ||
           (action == "color" && (mode == "solid" || mode == "rainbow"));
}

} // namespace lighting
} // namespace ymcc
