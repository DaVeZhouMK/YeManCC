#pragma once
#include <string_view>
// Requires hc_model_motion_catalog.h first. Fixed HC default transforms only;
// a table entry never establishes actual sensor presence or physical axes.
namespace ymcc::hc_default_motion {
inline constexpr hc_model_motion::Recipe recipes[] = {
    {"HandheldCompanion.Devices.GPDWin2", "hc-a250b19a:observed-default:GPDWin2", "hc-a250b19a:default-GPDWin2:gyro:x-y-z", "hc-a250b19a:default-GPDWin2:accel:x-y-z", "HC-a250b19a/IDevice.IMUMatrix+GPDWin2/no-device-resource", "d387c867e32be25bc38ecb3208af2de4ae5e78893a7c67ba46355a98837aeec9"},
    {"HandheldCompanion.Devices.GPDWin3", "hc-a250b19a:observed-default:GPDWin3", "hc-a250b19a:default-GPDWin3:gyro:x-y-z", "hc-a250b19a:default-GPDWin3:accel:x-y-z", "HC-a250b19a/IDevice.IMUMatrix+GPDWin3/no-device-resource", "985f334c447bfb0d21c4535a5d0b0b6bc8fa08882f5e9c95eee64d97e9610f0c"},
    {"HandheldCompanion.Devices.SteamMachine", "hc-a250b19a:observed-default:SteamMachine", "hc-a250b19a:default-SteamMachine:gyro:x-y-z", "hc-a250b19a:default-SteamMachine:accel:x-y-z", "HC-a250b19a/IDevice.IMUMatrix+SteamMachine/no-device-resource", "aa9f9ffe00fcec76d5bcdfe6cb702b2194c019d7f4014d43941e1067688aabb1"},
};
inline constexpr const hc_model_motion::Recipe* find(std::string_view deviceClass) noexcept {
    for(const auto& row:recipes) if(row.deviceClass==deviceClass) return &row;
    return nullptr;
}
}
