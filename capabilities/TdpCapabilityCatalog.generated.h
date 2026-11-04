// Generated from uxtu-tdp-policy.json and the existing unified model alias catalog.
#pragma once
#include "hc-device-catalog.generated.h"
#include <string_view>
namespace ymcc::capabilities::tdp {
inline constexpr std::string_view libraryId="ymcc.uxtu.tdp", version="26.3.1", policySha256="ece50ddf62b779ab24a26dc269afefa098d0edba94d1c35a5e6b3c5710edb72d";
inline constexpr std::string_view msiBoard="MS-1T91";
inline constexpr int intelPl2Offset=1;
struct RequestDomain {int minimumW,maximumW;};
inline constexpr RequestDomain defaultDomain{2,200}, msiDomain{8,200};
inline constexpr RequestDomain requestDomain(bool oemSelected) noexcept {return oemSelected?msiDomain:defaultDomain;}
inline std::string_view modelId(std::string_view modelClass) noexcept {return ymcc::capabilities::deviceId(modelClass);}
inline constexpr bool coreSwitchActive=false,voltageSchedulingActive=false,applyHcDeclaredLimits=false;
inline constexpr bool ccdControlActive=true,voltageOffsetActive=true,ccdPhysicalPowerOff=false;
inline constexpr std::string_view ccdOperation="process_affinity",ccdEffectScope="global_process_affinity_by_l3_domain",ccdMechanism="SetProcessAffinityMask",ccdTopologySource="windows_relation_cache_l3";
inline constexpr int ccdMinimumDomains=2;
inline constexpr std::string_view amdVoltageUnit="co_step",intelVoltageUnit="mv";
inline constexpr int amdVoltageMinimum=-60,amdVoltageMaximum=60,amdVoltageStep=1,intelVoltageMinimum=-150,intelVoltageMaximum=0,intelVoltageStep=1;
} // namespace ymcc::capabilities::tdp
