#pragma once
#include <string_view>

namespace ymcc::input::virtualTargetPolicy {
// Machine identity, never a connected controller VID/PID or requested target,
// decides this restriction. Other machines may still emulate Steam Deck.
inline constexpr const char* kSteamDeckSelfOutputBlocked = "physical-steamdeck-self-output-blocked";
inline const char* rejectionReason(bool identifiedPhysicalSteamDeck,
        std::string_view requestedPersona) noexcept {
    return identifiedPhysicalSteamDeck && requestedPersona == "steamdeck"
        ? kSteamDeckSelfOutputBlocked : nullptr;
}
} // namespace ymcc::input::virtualTargetPolicy
