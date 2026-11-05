#pragma once
#include "hc-device-catalog.generated.h"
#include "HcOneXVirtualControllerCapability.generated.h"
#include <string_view>

// Immutable identity/source capability only. The runtime owns current class,
// internal root, source/topology generations, Host ownership and lifecycle time.
namespace ymcc::capabilities::onexVirtual {
struct Binding {
    const Device* model = nullptr;
    const SourceIdentity* source = nullptr;
    bool known() const noexcept;
    std::string_view deviceId() const noexcept;
    std::string_view canonicalClass() const noexcept;
};

namespace detail {
inline bool catalogOneXModel(const Device* model) noexcept {
    for (const auto& row : devices) {
        if (model != &row) continue; // Establish membership before dereferencing.
        constexpr std::string_view prefix = "HandheldCompanion.Devices.OneXPlayer";
        return row.factoryInstantiable && row.consumerClass.size() > prefix.size() &&
            row.consumerClass.substr(0, prefix.size()) == prefix;
    }
    return false;
}
inline bool reviewedOneXSource(const SourceIdentity* source) noexcept {
    for (const auto& row : reviewedSourceIdentities)
        if (source == &row) return true;
    return false;
}
} // namespace detail

inline bool Binding::known() const noexcept {
    if (model) return !source && detail::catalogOneXModel(model);
    return source && detail::reviewedOneXSource(source);
}
inline std::string_view Binding::deviceId() const noexcept {
    if (!known()) return {};
    return model ? model->deviceId : source->deviceId;
}
inline std::string_view Binding::canonicalClass() const noexcept {
    if (!known()) return {};
    return model ? model->consumerClass : source->canonicalClass;
}

inline Binding bind(std::string_view deviceClass) noexcept {
    const auto* model = find(deviceClass); // Legacy aliases remain catalog-owned.
    if (detail::catalogOneXModel(model)) return {model, nullptr};
    for (const auto& source : reviewedSourceIdentities)
        if (source.canonicalClass == deviceClass) return {nullptr, &source};
    return {}; // No nicknames, short-name aliases, or inferred factory entries.
}
inline bool recognized(Binding binding, std::string_view deviceClass) noexcept {
    if (!binding.known()) return false;
    const auto current = bind(deviceClass);
    return current.known() && binding.model == current.model && binding.source == current.source &&
        binding.canonicalClass() == current.canonicalClass();
}
// Immutable collection classification only, with no hardware-ID or topology
// claim. A known OneX binding and standard GAME usage are metadata, not proof
// of the selected physical source, internal wiring, root/slot binding, or any
// hide/Cycle permission. Those live checks remain with the existing consumer.
inline bool standardGameCollection(Binding binding, unsigned usagePage,
                                   unsigned usage) noexcept {
    return binding.known() && usagePage == 0x0001 &&
        (usage == 0x0004 || usage == 0x0005);
}
inline bool gameSource(Binding binding, unsigned vid, unsigned pid,
                       unsigned page, unsigned usage) noexcept {
    // Match an actually observed source for the optional refresh only. 045E:028E
    // has Apex field evidence; it is not an inherited hardware-ID claim for all
    // registered classes. Other IDs skip this refresh, not generic virtual input.
    // Vendor-key endpoints grant no game-root capability. IDs alone cannot
    // distinguish an internal device from an external controller.
    return binding.known() && vid == 0x045E && pid == 0x028E &&
        page == 0x0001 && (usage == 0x0004 || usage == 0x0005);
}
} // namespace ymcc::capabilities::onexVirtual
