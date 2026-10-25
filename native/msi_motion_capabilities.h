#pragma once
#include <array>
#include <string_view>
namespace ymcc::msi_sensor {
// HC 1.3.3.1 @ a250b19a: A2VM and CG3EM resource JSON BOTH declare the
// B14C/7,8,9 Physical Gyrometer and Physical Accelerometer field mappings.
// Availability is ALWAYS a runtime reading, not an entry in this library.
// These model-specific policies only select permitted import formats and label
// the provenance of the existing HC recipe. They do not overwrite user choices.
struct MotionCapability {
    std::string_view deviceClass;
    std::array<std::string_view,2> boards;
    bool clawHidSensorApiLayout;
    bool exWorldSpaceDefaultOff;
    std::string_view matrixEvidence;
    std::string_view frozenMatrixSourcesSha256;
};
inline constexpr std::array<MotionCapability,4> kMotionCapabilities{{
    {"HandheldCompanion.Devices.ClawA1M",{"MS-1T41",""},false,false,"hc-a1m-declaration","fa7e4a2c2f6b56ba019689361769127875be4de0985351001839a48614d08748"},
    {"HandheldCompanion.Devices.ClawA2VM",{"MS-1T42","MS-1T52"},true,false,"hc-a250b19a-a2vm-json-declaration;258v-winrt-baseline-preserved","a3aa76fa867a3b57ba54d3d0a05accad7b998f13d84d59605c2442fecb41ab65"},
    {"HandheldCompanion.Devices.ClawCG3EM",{"MS-1T91",""},true,true,"hc-a250b19a-cg3em-json-declaration;physical-axis-validation-pending","42a75712d2d6daf1fa1b5064a75f7150e422eae6abfc6b16ce68e150b76d6f4f"},
    {"HandheldCompanion.Devices.ClawBZ2EM",{"MS-1T8K",""},false,false,"hc-bz2em-declaration;no-ctw-intel-hid-layout-assumption","884553759f272b1018ce88b3bcd95937855207dcd5fb89f8b778fd4e6be5161c"}
}};
inline const MotionCapability* motionCapability(std::string_view deviceClass,std::string_view board) {
    for(const auto& capability:kMotionCapabilities) {
        if(capability.deviceClass!=deviceClass) continue;
        for(const auto token:capability.boards)
            if(!token.empty() && board.find(token)!=std::string_view::npos) return &capability;
    }
    return nullptr;
}
// Current HC resource JSON describes the proprietary Physical Gyrometer/
// Accelerometer (B14C / 7,8,9) as X,Z,-Y. Use this ONLY on that evidenced
// legacy import format. The previously successful 258V WinRT route and every
// other device/SDK format retain their existing named HC matrix recipe.
inline std::array<double,3> configuredLegacyAxes(std::array<double,3> raw) {
    return {raw[0],raw[2],-raw[1]};
}
inline std::string_view legacyRecipeSha256(const MotionCapability& capability) {
    if(capability.deviceClass=="HandheldCompanion.Devices.ClawCG3EM")
        return "a1f4b96eb92c61c0405593a0abe93d2c007b5507867377f7f5f7002d8d663e90";
    if(capability.deviceClass=="HandheldCompanion.Devices.ClawA2VM")
        return "b9894fdb7bcbbc2e018e9c70bd54b9fda7a18fe7793ef4b2782308c606d1a7a3";
    return {};
}
} // namespace ymcc::msi_sensor
