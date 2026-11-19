#pragma once
#include "sleep_power_observation.h"

namespace ymcc::presentationpower {
// Presentation consumes the existing system observation, not hardware/input
// admission. A user must still be able to open settings to repair a device.
inline constexpr bool ready(const sleeppower::SharedPowerV1& value) noexcept {
    return sleeppower::powerObservationKnown(value)&&value.phase==0&&
        !(value.flags&sleeppower::kNotificationPending);
}
inline constexpr bool sameReadyEpisode(const sleeppower::SharedPowerV1& captured,
                                       const sleeppower::SharedPowerV1& observed) noexcept {
    return ready(captured)&&ready(observed)&&captured.generation==observed.generation&&
        captured.suspendSerial==observed.suspendSerial&&
        observed.publicationRevision>=captured.publicationRevision;
}
inline constexpr bool current(const sleeppower::SharedPowerV1& captured,
                              const sleeppower::SharedPowerV1& observed) noexcept {
    return sameReadyEpisode(captured,observed)&&
        captured.observedNotificationSequence==observed.observedNotificationSequence;
}
inline bool capture(sleeppower::Reader read,sleeppower::SharedPowerV1& value) noexcept {
    return sleeppower::capturePowerObservation(read,value)&&ready(value);
}
inline bool current(sleeppower::Reader read,const sleeppower::SharedPowerV1& captured,
                    bool committedOriginalUi=false) noexcept {
    sleeppower::SharedPowerV1 observed;
    return capture(read,observed)&&(committedOriginalUi?sameReadyEpisode(captured,observed):current(captured,observed));
}
}
