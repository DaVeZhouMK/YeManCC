#pragma once
#include <cstdint>

namespace ymcc::msi::shoulderModifiers {

// Event-time context captured for a candidate keyboard edge. This is diagnostic
// data only and has no input ownership or delivery side effects.
struct Observation {
    bool observed;
    bool eligible;
    std::uint16_t buttons;
    std::uint64_t sampleTick;
    std::uint64_t hostEpoch;
    std::uint64_t powerGeneration;
};

struct Context {
    bool knownMsiBoard;
    bool steamDeckBound;
    bool targetEnabled;
    bool hostUsable;
    bool sourceLive;
    bool msiDinput;
    std::uint64_t sampleTick;
    std::uint16_t buttons;
};

struct Event {
    std::uint16_t vk;
    bool down;
    bool injected;
    bool lowerIntegrityInjected;
    std::uintptr_t extraInfo;
};

// Manual API: pass a coherent physical-source Context, an observed keyboard Event,
// and now/sampleTick in the same monotonic millisecond clock. knownMsiBoard must
// come from the caller's existing board authority, never a vendor-name guess;
// buttons must be physical source bits, not virtual output. physicalModifierHeld
// describes the matching physical modifier, independently of injected key state.
// true means discard only this down; false means pass it through. The caller owns
// snapshot publication and hook integration. This function performs no input I/O,
// release synthesis, state tracking, allocation, locking, or platform API calls.
// Basis: three historical Raw0 lower-IL downs correlated with shoulder presses.
// A half-second interval alone is not anomalous; 64 ms bounds source freshness,
// not a keyboard repeat/pulse detector or a claim of device causation.
constexpr bool shouldSuppress(const Context& context, const Event& event,
                              std::uint64_t now, bool physicalModifierHeld) noexcept {
    if (!event.down || !event.injected || !event.lowerIntegrityInjected ||
        event.extraInfo != 0 || physicalModifierHeld) {
        return false;
    }
    if (!context.knownMsiBoard || !context.steamDeckBound || !context.targetEnabled ||
        !context.hostUsable || !context.sourceLive || !context.msiDinput) {
        return false;
    }
    if (context.sampleTick == 0 || now < context.sampleTick ||
        now - context.sampleTick > 64) {
        return false;
    }
    return (event.vk == 162 && (context.buttons & 0x0100u) != 0) ||
           (event.vk == 164 && (context.buttons & 0x0200u) != 0);
}

} // namespace ymcc::msi::shoulderModifiers
