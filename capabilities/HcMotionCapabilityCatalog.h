// AI-NAV[motion]: hc-motion-policy.json -> tools/lib/hc_motion_catalog.mjs -> this binding -> native/main.cpp.
// Follow inputCaptureMotionSelectorBound/inputCaptureProcessMotionContribution; AI-INDEX.json lists the source anchors.
// Cache immutable recipe identity only; live sample pairing, freshness, power and final publication stay consumer-owned.
#pragma once
#include "HcMotionCapabilityCatalog.generated.h"
namespace ymcc::capabilities::motion {
// Only immutable recipe identity is retained. No source lease, sample validity,
// provider admission, hardware handle or publication permission is cached here.
struct Binding {
    Kind kind = Kind::None;
    std::size_t slot = 0;
    const Descriptor* registration = nullptr;
    const void* recipe = nullptr;
};
inline const Descriptor* descriptor(const Binding& binding) noexcept {
    if(binding.slot >= count(binding.kind)) return nullptr;
    const auto* row = &descriptors[offset(binding.kind) + binding.slot];
    return binding.registration == row ? row : nullptr;
}
// The selected row retains its canonical class owner, including Reviewed precedence over Legacy.
// A mismatched/forged row never chooses another class's route. Live Bound checks follow this lookup.
inline Kind primaryKind(const Binding& binding,std::string_view deviceClass) noexcept {
    const auto* row=descriptor(binding);
    return row && row->deviceClass==deviceClass ? row->primaryKind : Kind::None;
}
template<class Recipe> inline Binding bindOne(Kind kind, std::size_t slot, const Recipe* recipe) noexcept {
    if(!recipe || slot >= count(kind)) return {};
    const auto& row = descriptors[offset(kind) + slot];
    if(recipe->deviceClass != row.deviceClass || recipe->selectorId != row.selectorId ||
        recipe->gyroMatrixId != row.gyroMatrixId || recipe->accelMatrixId != row.accelMatrixId ||
        recipe->sourceSha256 != row.sourceSha256) return {};
    if constexpr(requires {recipe->controllerPreferred;}) {
        if(recipe->controllerPreferred != row.controllerPreferred || recipe->controllerPreferred) return {};
    }
    return {kind, slot, &row, recipe};
}
template<class Recipe, std::size_t N> inline Binding bind(Kind kind, const Recipe* recipe, const Recipe (&rows)[N]) noexcept {
    if(count(kind) != N) return {};
    for(std::size_t slot = 0; slot < N; ++slot)
        if(recipe == &rows[slot]) return bindOne(kind, slot, recipe);
    return {};
}
template<class Recipe> inline const Recipe* consumeOne(const Binding& binding, Kind kind, std::size_t slot, const Recipe& recipe) noexcept {
    return binding.kind == kind && binding.slot == slot && descriptor(binding) && binding.recipe == &recipe ? &recipe : nullptr;
}
template<class Recipe, std::size_t N> inline const Recipe* consume(const Binding& binding, Kind kind, const Recipe (&rows)[N]) noexcept {
    if(binding.kind != kind || count(kind) != N || binding.slot >= N || !descriptor(binding)) return nullptr;
    const auto* recipe = &rows[binding.slot];
    return binding.recipe == recipe ? recipe : nullptr;
}
} // namespace ymcc::capabilities::motion
