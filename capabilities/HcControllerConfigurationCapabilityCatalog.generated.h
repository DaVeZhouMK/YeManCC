// Generated from frozen HC model/method and firmware source. Registration grants no write permission.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace ymcc::capabilities::configuration {
enum class RearRoute { None, DInputButtons, XInputKeyboard };
struct Model {std::string_view deviceId,deviceClass,sourceSha256;std::array<std::string_view,2> boards;};
struct Firmware {std::uint16_t value;std::array<std::uint8_t,2> d1,d2,x1,x2;};
inline constexpr std::string_view rearCatalogSha256="471704887a4b8f5d910bfd79fe691a80e055b4c2cb381a4d2b2da0b2aafc1ce7";
inline constexpr std::array<Model,4> rearModels={{
 {"hc.device.ClawA1M","HandheldCompanion.Devices.ClawA1M","3a67b506ab11b2236b046696b2055e6f6d95c1794672b15300b30314a7643821",{"MS-1T41",""}},
 {"hc.device.ClawA2VM","HandheldCompanion.Devices.ClawA2VM","aa54ca29a82e9dbbe0fe23b98cb766e81a5e6558fd7c1221dcc8b8e900020d5e",{"MS-1T42","MS-1T52"}},
 {"hc.device.ClawBZ2EM","HandheldCompanion.Devices.ClawBZ2EM","644e95ef21a6f191e92976026595ecf1e13153f7bcc877757baf2911079fb726",{"MS-1T8K",""}},
 {"hc.device.ClawCG3EM","HandheldCompanion.Devices.ClawCG3EM","45518a1ef85f40d2427ca5259d1b1eb8538883618f99c6b2d39dbc4567e0bfbd",{"MS-1T91",""}},
 }};
inline constexpr std::array<Firmware,8> rearFirmware={{
 {355,{0,122},{1,31},{0,123},{1,32}},
 {358,{0,186},{1,99},{0,187},{1,100}},
 {359,{0,186},{1,99},{0,187},{1,100}},
 {529,{0,122},{1,31},{0,123},{1,32}},
 {535,{0,186},{1,99},{0,187},{1,100}},
 {537,{0,186},{1,99},{0,187},{1,100}},
 {776,{0,186},{1,99},{0,187},{1,100}},
 {1041,{0,186},{1,99},{0,187},{1,100}},
 }};
inline constexpr std::string_view rearCapabilityId(RearRoute route)noexcept{return route==RearRoute::DInputButtons?"controller.configuration.msi.rear.dinput-buttons":route==RearRoute::XInputKeyboard?"controller.configuration.msi.rear.xinput-keyboard":"";}
} // namespace ymcc::capabilities::configuration
