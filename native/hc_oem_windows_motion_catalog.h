#pragma once
#include <array>
#include <iterator>
#include <string_view>
// Requires hc_model_motion_catalog.h, hc_legacy_motion_catalog.h and msi_motion_capabilities.h first.

// Existing frozen WinRT recipes for the six OEM routes intentionally excluded
// from hc_legacy_motion_catalog. No matrix, mode write or default route changes.
namespace ymcc::hc_oem_windows_motion {
constexpr std::string_view msiDigest(std::string_view deviceClass) noexcept {
    for(const auto& item:msi_sensor::kMotionCapabilities)
        if(item.deviceClass==deviceClass)return item.frozenMatrixSourcesSha256;
    return {};
}
inline constexpr hc_model_motion::Recipe recipes[] = {
    {"HandheldCompanion.Devices.ClawA1M","msi-MS-1T41","hc-06c0b954:msi-claw-a1m:gyro:x-z-neg-y","hc-06c0b954:msi-claw-a1m:accel:neg-x-neg-z-y","native/msi_motion_capabilities.h#ClawA1M/frozen-CSharp-bundle",msiDigest("HandheldCompanion.Devices.ClawA1M")},
    {"HandheldCompanion.Devices.ClawA2VM","msi-MS-1T42","hc-06c0b954:msi-claw-sub:gyro:x-y-neg-z","hc-06c0b954:msi-claw-a1m:accel:neg-x-neg-z-y","native/msi_motion_capabilities.h#ClawA2VM/frozen-CSharp-bundle",msiDigest("HandheldCompanion.Devices.ClawA2VM")},
    {"HandheldCompanion.Devices.ClawBZ2EM","msi-MS-1T8K","hc-06c0b954:msi-claw-sub:gyro:x-y-neg-z","hc-06c0b954:msi-claw-a1m:accel:neg-x-neg-z-y","native/msi_motion_capabilities.h#ClawBZ2EM/frozen-CSharp-bundle",msiDigest("HandheldCompanion.Devices.ClawBZ2EM")},
    {"HandheldCompanion.Devices.ClawCG3EM","msi-MS-1T91","hc-06c0b954:msi-claw-sub:gyro:x-y-neg-z","hc-06c0b954:msi-claw-a1m:accel:neg-x-neg-z-y","native/msi_motion_capabilities.h#ClawCG3EM/frozen-CSharp-bundle",msiDigest("HandheldCompanion.Devices.ClawCG3EM")},
    {"HandheldCompanion.Devices.XboxROGAlly","asus-rc73ya","hc-06c0b954:xbox-rog-ally:gyro:x-z-neg-y","hc-06c0b954:xbox-rog-ally:accel:neg-x-neg-z-y","deps/handheldcompanion-runtime/source/Devices/ASUS/XboxROGAlly.cs","78BF90520419B63C6C8FD2BC12543B2D16280895802D8F9952849C4E030FE3D4"},
    {"HandheldCompanion.Devices.XboxROGAllyX","asus-rc73xa","hc-06c0b954:xbox-rog-ally-x:gyro:x-z-neg-y","hc-06c0b954:xbox-rog-ally-x:accel:neg-x-neg-z-y","deps/handheldcompanion-runtime/source/Devices/ASUS/XboxROGAllyX.cs","404987B6116C5DC79FEC80AABDF97D6E28F146EC2FFC3AF69ED8FC71E40FF8EE"}
};
inline const hc_model_motion::Recipe* find(std::string_view deviceClass) noexcept {
    for(const auto& recipe:recipes)if(recipe.deviceClass==deviceClass)return &recipe;
    return nullptr;
}
// Reuse exact existing legacy tuples for private ROG/GPD captures. Source IDs,
// hashes and axis implementations stay owned by the original catalog.
inline const hc_model_motion::Recipe* findLegacy(std::string_view deviceClass,
    std::string_view gyro,std::string_view accel) noexcept {
    const auto* legacy=hc_legacy_motion::find(deviceClass,gyro,accel);
    if(!legacy||legacy->controllerPreferred)return nullptr;
    static constexpr auto converted=[] {
        std::array<hc_model_motion::Recipe,std::size(hc_legacy_motion::recipes)> result{};
        for(std::size_t i=0;i<result.size();++i){const auto& r=hc_legacy_motion::recipes[i];
            result[i]={r.deviceClass,r.selectorId,r.gyroMatrixId,r.accelMatrixId,r.sourcePath,r.sourceSha256};}
        return result;
    }();
    return &converted[legacy-std::begin(hc_legacy_motion::recipes)];
}
} // namespace ymcc::hc_oem_windows_motion
