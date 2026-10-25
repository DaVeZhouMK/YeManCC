// AI-NAV[controller-motion]: hc-controller-motion-policy.json -> this model/protocol binding -> native/main.cpp legionMotionBoundSource.
// CONTROLLER-CONSUMPTION.json separates protocol decode, source lease/banks, calibration and final submission.
// ModelBindingCache retains immutable selection; PID, readiness and source epoch must stay live.
#pragma once
#include "HcControllerMotionCapabilityCatalog.generated.h"
#include "hc-device-catalog.generated.h"
#include <string>

namespace ymcc::capabilities::controller_motion {
inline const Descriptor* descriptorForToken(std::uint16_t token) noexcept {
    return token && token <= descriptors.size() && descriptors[token - 1].token == token
        ? &descriptors[token - 1] : nullptr;
}
class Binding {
public:
    bool known() const noexcept { return descriptor_ != nullptr; }
    std::uint16_t token() const noexcept { return descriptor_ ? descriptor_->token : 0; }
    unsigned bankCount() const noexcept { return descriptor_ ? descriptor_->bankCount : 0; }
    const Descriptor* descriptor() const noexcept { return descriptor_; }
    template<class TabletPid, class GoSPid>
    bool hardwareAdmitted(std::uint16_t vendorId, std::uint16_t productId,
                          TabletPid tabletPid, GoSPid goSPid) const noexcept {
        if (!descriptor_ || descriptor_->vendorId != vendorId) return false;
        return descriptor_->protocol == ProtocolKind::Tablet ? tabletPid(productId) : goSPid(productId);
    }
    // Invoke the caller's exact existing decoder and original protocol values.
    template<class Protocol, class Decoder>
    auto decode(Protocol tablet, Protocol goS, Decoder originalDecode,
        const std::uint8_t* logical, std::size_t capacity, std::size_t bytes, unsigned index) const
        -> decltype(originalDecode(tablet,logical,capacity,bytes,index)) {
        if (!descriptor_) return {};
        return originalDecode(descriptor_->protocol == ProtocolKind::Tablet ? tablet : goS,
            logical,capacity,bytes,index);
    }

private:
    const Descriptor* descriptor_ = nullptr;
    friend Binding bindModel(std::string_view) noexcept;
};
inline Binding bindModel(std::string_view deviceClass) noexcept {
    Binding binding;
    const auto* model = ymcc::capabilities::find(deviceClass);
    if (model) for (const auto& descriptor : descriptors) {
        if (descriptor.deviceId == model->deviceId) { binding.descriptor_ = &descriptor; break; }
    }
    return binding;
}
// Only immutable model/protocol selection is retained. Hardware identity, PID,
// source epoch, readiness and final ownership remain live native predicates.
class ModelBindingCache {
public:
    Binding bind(const std::string& deviceClass) noexcept {
        if (initialized_ && deviceClass_ == deviceClass) return binding_;
        try {
            std::string nextClass(deviceClass);
            auto next = bindModel(deviceClass);
            // Native admission retains the original exact canonical class gate.
            if (next.known() && next.descriptor()->deviceClass != deviceClass) next = {};
            deviceClass_.swap(nextClass); binding_ = next; initialized_ = true;
            return binding_;
        } catch (...) { return {}; } // Allocation failure refuses this call; no stale model is returned.
    }
private:
    bool initialized_ = false;
    std::string deviceClass_;
    Binding binding_;
};
} // namespace ymcc::capabilities::controller_motion
