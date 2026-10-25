// AI-NAV[controller-feedback]: Use hc-controller-feedback-policy.json plus the CONTROLLER-*-RECEIPT contracts in AI-INDEX.json.
// This selects an existing physical writer; native/main.cpp owns writes, zero/stop, source revocation and pending settlement.
// Raw output observation, decoded motor requests, software receipts and physical vibration are separate evidence stages.
#pragma once
#include "HcControllerFeedbackCapabilityCatalog.generated.h"
namespace ymcc::capabilities::controller {
struct PhysicalFeedbackBinding {
    const PhysicalFeedbackDescriptor* provider=nullptr;
    const PhysicalFeedbackDescriptor* descriptor()const noexcept {
        for(const auto& row:physicalFeedbackProviders)if(provider==&row)return &row;
        return nullptr;
    }
    bool known()const noexcept {const auto* row=descriptor();return row&&row->token&&row->kind!=PhysicalFeedbackKind::None&&!row->capabilityId.empty();}
    std::uint16_t token()const noexcept {const auto* row=descriptor();return row?row->token:0;}
};
inline PhysicalFeedbackBinding bindPhysicalFeedback(PhysicalFeedbackKind kind)noexcept {
    const auto token=static_cast<unsigned>(kind);
    if(!token||token>physicalFeedbackProviders.size())return {};
    const auto& row=physicalFeedbackProviders[token-1];
    return row.token==token&&row.kind==kind?PhysicalFeedbackBinding{&row}:PhysicalFeedbackBinding{};
}
inline const PhysicalFeedbackDescriptor* physicalFeedbackDescriptorForToken(std::uint16_t token)noexcept {
    if(!token||token>physicalFeedbackProviders.size())return nullptr;
    const auto& row=physicalFeedbackProviders[token-1];return row.token==token?&row:nullptr;
}
} // namespace ymcc::capabilities::controller
