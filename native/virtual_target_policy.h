#pragma once
#include "controller_persona.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace ymcc::input::virtualTargetPolicy {
using HidHideVersion = std::array<std::uint16_t,4>;
inline constexpr HidHideVersion kMinimumHidHideDriver{1,4,181,0};
inline constexpr const char* kHidHideDriverTooOld = "hidhide-driver-too-old";
inline bool hidHideDriverTooOld(const HidHideVersion& version) noexcept {
    return version != HidHideVersion{} && version < kMinimumHidHideDriver;
}
// Machine identity, never a connected controller VID/PID or requested target,
// decides this restriction. Other machines may still emulate Steam Deck.
inline constexpr const char* kSteamDeckSelfOutputBlocked = "physical-steamdeck-self-output-blocked";
inline const char* rejectionReason(bool identifiedPhysicalSteamDeck,
        std::string_view requestedPersona, bool legacyHidHide = false) noexcept {
    if (legacyHidHide && controllerPersona::currentOutput(requestedPersona)) return kHidHideDriverTooOld;
    return identifiedPhysicalSteamDeck && requestedPersona == "steamdeck"
        ? kSteamDeckSelfOutputBlocked : nullptr;
}
} // namespace ymcc::input::virtualTargetPolicy
