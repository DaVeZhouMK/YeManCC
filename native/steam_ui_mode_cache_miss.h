#pragma once
#include <cstdint>
#include "sleep_power_state.h"

namespace ymcc::steamui {
// Observation only. Nonzero sequence identifies one cache clear or lifecycle reset.
struct ModeCacheMiss {
    const char* reason = nullptr;
    std::uint32_t originLine = 0;
    std::uint64_t sequence = 0;
};
// Diagnostic provenance only. Neither the timestamps nor this copy of the
// public lifecycle receipt can authorize reading or changing Steam.
struct ModeCacheTiming {
    sleeppower::SharedPowerV1 power;
    std::uint64_t readerReturnedTick = 0, publishedTick = 0;
    bool matches(const sleeppower::SharedPowerV1& scope) const noexcept {
        return publishedTick && (sleeppower::samePowerRevocation(power, scope) || sleeppower::samePowerRevocation(scope, power));
    }
};
}
