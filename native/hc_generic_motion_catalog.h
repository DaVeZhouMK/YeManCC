#pragma once
#include <string_view>
// Requires hc_model_motion_catalog.h. These are generic HC fallback recipes,
// never a claim that an unknown PC is a particular handheld. No hardware writes.
namespace ymcc::hc_generic_motion {
inline constexpr hc_model_motion::Recipe defaultDevice={
    "HandheldCompanion.Devices.DefaultDevice","hc-a250b19a:generic:DefaultDevice",
    "hc-a250b19a:generic-DefaultDevice:gyro:x-z-y","hc-a250b19a:generic-DefaultDevice:accel:x-z-y",
    "HC-a250b19a/Resources/Devices/DefaultDevice.json","c5c539950e9bc737bff519b4bfd6879669f5e3d952e4dd2fdc7ee9f78528ccd7"};
inline constexpr hc_model_motion::Recipe asus={
    "HandheldCompanion.Devices.ASUS.ASUS","hc-a250b19a:generic:ASUS",
    "hc-a250b19a:generic-ASUS:gyro:x-y-z","hc-a250b19a:generic-ASUS:accel:x-y-z",
    "HC-a250b19a/IDevice.IMUMatrix+ASUS/no-device-resource","0a32983b05d3c9e30b3a4c6bfc9608ac962cbc70c005ed2d2a271946501e3cee"};
}
