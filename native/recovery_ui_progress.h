#pragma once
#include <cstdint>

namespace ymcc {
// Schema-1 reserved word is optional: old publishers keep zero, not a dead UI.
// The publisher thread cannot manufacture progress; only a posted UI ping can.
inline constexpr std::uint32_t kRecoveryUiPulsePresent = 0x80000000u;
inline constexpr std::uint32_t kRecoveryUiPulseMask = 0x7fffffffu;
inline constexpr std::uint64_t kRecoveryUiStallMs = 30000;
inline constexpr unsigned kRecoveryUiStallSamples = 3;
inline std::uint32_t encodeRecoveryUiPulse(std::uint32_t pulse) {
    return kRecoveryUiPulsePresent | (pulse & kRecoveryUiPulseMask);
}
enum class RecoveryUiProgressResult { Unsupported, Observing, Advanced, Stalled };
struct RecoveryUiProgress {
    bool active = false;
    std::uint32_t pulse = 0;
    std::uint64_t lastProgressTick = 0;
    unsigned failedRounds = 0;
    void reset() { *this = {}; }
    RecoveryUiProgressResult observe(bool known, std::uint32_t nextPulse, std::uint64_t now) {
        if (!known) { reset(); return RecoveryUiProgressResult::Unsupported; }
        nextPulse &= kRecoveryUiPulseMask;
        if (!active || now < lastProgressTick) {
            active = true;
            pulse = nextPulse;
            lastProgressTick = now;
            failedRounds = 1;
            return RecoveryUiProgressResult::Observing;
        }
        if (nextPulse != pulse) {
            pulse = nextPulse;
            lastProgressTick = now;
            failedRounds = 1;
            return RecoveryUiProgressResult::Advanced;
        }
        if (failedRounds < kRecoveryUiStallSamples) ++failedRounds;
        return now - lastProgressTick >= kRecoveryUiStallMs && failedRounds >= kRecoveryUiStallSamples
            ? RecoveryUiProgressResult::Stalled : RecoveryUiProgressResult::Observing;
    }
};
} // namespace ymcc
