#pragma once
#include "sleep_power_state.h"
#include "sleep_power_revocation.h"
namespace ymcc::steamsource {
// The same three existing Native-owner callbacks. No UI/document dependency,
// new power producer, sampling, timer or independently granted capability.
struct NativePowerAccess {
    bool (*capture)(sleeppower::SharedPowerV1&) noexcept = nullptr;
    bool (*current)(const sleeppower::SharedPowerV1&) noexcept = nullptr;
    sleeppower::RevocationMirrors::Registration (*observe)(const sleeppower::SharedPowerV1&) noexcept = nullptr;
};
}
