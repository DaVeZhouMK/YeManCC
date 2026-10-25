// AI-NAV[controller]: hc-controller-input-policy.json describes immutable providers; CONTROLLER-CONSUMPTION.json defines stages.
// The existing native/main.cpp frame chain calls bindStandardInput and owns live source/final-submission guards.
// Do not choose external-controller capabilities from chassis DMI or reuse a stale connection mask as ownership.
#pragma once
#include "HcControllerInputCapabilityCatalog.generated.h"
namespace ymcc::capabilities::controller {
// Immutable provider selection only. Live physical identity, read freshness,
// target ownership and final submission permission remain in the original consumer.
struct InputBinding {
    const InputDescriptor* provider = nullptr;
    InputKind kind() const noexcept {
        for(const auto& row : inputProviders) if(provider == &row) return row.kind;
        return InputKind::None;
    }
    bool known() const noexcept { return kind() != InputKind::None && !provider->capabilityId.empty() && provider->standardFrameConsumer; }
    std::string_view capabilityId() const noexcept { return known() ? provider->capabilityId : std::string_view{}; }
};
inline InputBinding bindStandardInput(std::string_view actualSource, unsigned connectedMask) noexcept {
    for(const auto& provider : inputProviders) {
        for(const auto alias : provider.sourceAliases) {
            if(!alias.empty() && actualSource == alias) {
                if(provider.requiresConnectedMask && !connectedMask) return {};
                return {&provider};
            }
        }
    }
    return {};
}
} // namespace ymcc::capabilities::controller
