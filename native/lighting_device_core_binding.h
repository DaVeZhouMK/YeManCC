// Generated from the unified lighting policy; the caller owns absolute paths.
#pragma once
#include <string>
namespace ymcc { namespace lighting {
inline constexpr char kIndependentDeviceCoreFileName[] = "YeManLightDeviceCore.dll";
inline constexpr wchar_t kIndependentDeviceCorePathSuffix[] = L"\\light-setter\\YeManLightDeviceCore.dll";
inline constexpr char kIndependentDeviceCoreSha256[] = "924576fffd04ead813fc7cb0f1ddbda4b15d8e43416b564c1107b88a76211675";
inline bool useIndependentDeviceCore(const std::string& deviceClass, const std::string& action, const std::string& mode) {
    if (deviceClass == "HandheldCompanion.Devices.OneXPlayerX1Mini") return action == "brightness" || (action == "color" && (mode == "solid" || mode == "rainbow"));
    if (deviceClass == "HandheldCompanion.Devices.OneXPlayerX2MiniPro") return action == "brightness" || action == "off" || (action == "color" && (mode == "solid" || mode == "breathing"));
    return false;
}
} } // namespace ymcc::lighting
