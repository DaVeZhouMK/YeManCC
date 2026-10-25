// AI-NAV[controller-configuration]: hc-controller-configuration-policy.json and CONTROLLER-CONSUMPTION.json own reviewed MSI routes.
// Follow native/main.cpp bindRear and native/msi_rear_protocol.h; keep exact endpoint/firmware admission.
// The native keyboard route remains record-only; adding metadata must not activate default hardware writes.
#pragma once
#include "HcControllerConfigurationCapabilityCatalog.generated.h"
#include "../native/msi_rear_protocol.h"
namespace ymcc::capabilities::configuration {
// Only immutable recipe/source data. Actual endpoint ownership, build lock,
// write admission, power/mode/source generations and retained IO stay consumer-owned.
struct RearEndpoint {std::uint16_t vendor=0,product=0,firmware=0,usagePage=0,usage=0,outputBytes=0;};
struct RearBinding {const Model* model=nullptr;const Firmware* firmware=nullptr;RearRoute route=RearRoute::None;RearEndpoint endpoint;};
inline bool rearEndpointAdmitted(const RearEndpoint& e,RearRoute route)noexcept{
 if(e.vendor!=0x0db0||e.outputBytes<64||route!=RearRoute::DInputButtons)return false; // inactive native keyboard stays record-only
 const bool xinput=e.product==0x1901&&e.usagePage==0xffa0&&e.usage==1;
 const bool dinput=e.product==0x1902&&e.usagePage==0xfff0&&e.usage==0x40;
 return xinput||dinput;
}
inline const Model* rearModel(std::string_view deviceClass,std::string_view board)noexcept{
 for(const auto& row:rearModels)if(row.deviceClass==deviceClass){for(const auto token:row.boards)
  if(!token.empty()&&board.find(token)!=std::string_view::npos)return &row;return nullptr;}return nullptr;
}
inline RearBinding bindRear(std::string_view deviceClass,std::string_view board,const RearEndpoint& e,RearRoute route)noexcept{
 if(!rearEndpointAdmitted(e,route))return {};
 const auto* model=rearModel(deviceClass,board);if(!model)return {};
 for(const auto& fw:rearFirmware)if(fw.value==e.firmware)return {model,&fw,route,e};return {};
}
inline ymcc::msi::rearProtocol::Reports consumeRear(const RearBinding& binding,std::string_view currentClass,std::string_view currentBoard,RearRoute route)noexcept{
 bool modelOwned=false,firmwareOwned=false;
 for(const auto& row:rearModels)if(binding.model==&row)modelOwned=true;
 for(const auto& row:rearFirmware)if(binding.firmware==&row)firmwareOwned=true;
 if(!modelOwned||!firmwareOwned||binding.route!=route||rearCapabilityId(route).empty()||
  rearModel(currentClass,currentBoard)!=binding.model||binding.endpoint.firmware!=binding.firmware->value||!rearEndpointAdmitted(binding.endpoint,route))return {};
 auto reports=ymcc::msi::rearProtocol::reports(binding.firmware->value,route==RearRoute::DInputButtons?ymcc::msi::rearProtocol::Route::DInputButtons:ymcc::msi::rearProtocol::Route::XInputKeyboard);
 const auto& m1=route==RearRoute::DInputButtons?binding.firmware->d1:binding.firmware->x1;
 const auto& m2=route==RearRoute::DInputButtons?binding.firmware->d2:binding.firmware->x2;
 if(!reports.valid||reports.m1[6]!=m1[0]||reports.m1[7]!=m1[1]||reports.m2[6]!=m2[0]||reports.m2[7]!=m2[1])return {};
 return reports; // Same original report factory; no second writer or payload implementation.
}
} // namespace ymcc::capabilities::configuration