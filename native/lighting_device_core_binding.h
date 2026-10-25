// Generated from the unified lighting policy; the caller owns absolute paths.
#pragma once
#include <string>
namespace ymcc { namespace lighting {
inline constexpr char kIndependentDeviceCoreFileName[] = "YeManLightDeviceCore.dll";
inline constexpr wchar_t kIndependentDeviceCorePathSuffix[] = L"\\light-setter\\YeManLightDeviceCore.dll";
inline constexpr char kIndependentDeviceCoreSha256[] = "88e5a77f781b2bf7e12df4d45b636b16f97031d02e8617572c7bba7176b8fb7e";
inline bool useIndependentDeviceCore(const std::string& deviceClass, const std::string& action, const std::string& mode) {
    if (deviceClass != "HandheldCompanion.Devices.OneXPlayerX1Mini") return false;
    return action == "brightness" || (action == "color" && (mode == "solid" || mode == "rainbow"));
}
} } // namespace ymcc::lighting
