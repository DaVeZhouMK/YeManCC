#pragma once
#include "recovery_resume_commit.h"
#include "sleep_power_state.h"
#include "recovery_power_boundary.h"

namespace ymcc::sleeppower {
// Optional tail of the existing RecoveryState mapping. This carries the Native
// owner's facts, not a second power state or permission to write a device.
struct RecoveryPowerProjection {
    std::uint32_t size=sizeof(RecoveryPowerProjection),version=1;
    SharedPowerV1 power;
    std::uint64_t committedGeneration=0,committedTick=0;
};
static_assert(sizeof(RecoveryPowerProjection)==72);
// One watchdog request owns this local fence. It grants no Native/device Ready.
struct RecoveryTerminationPermit {
    std::uint64_t notificationSequence=0,targetStartKey=0;
    std::uint32_t targetPid=0;
    bool accepted=false;
    RecoveryBoundaryTicket boundary;
};
struct RecoveryWakeObservation {
    RecoveryPowerProjection projection;
    std::uint64_t publishedTick=0;
    bool processBound=false;
    RecoveryBoundaryTicket boundary;
};
inline constexpr bool sameRecoveryPowerObservation(const SharedPowerV1& a,const SharedPowerV1& b) noexcept {
    return a.size==b.size&&a.version==b.version&&a.generation==b.generation&&
        a.publicationRevision==b.publicationRevision&&a.observedNotificationSequence==b.observedNotificationSequence&&
        a.phase==b.phase&&a.flags==b.flags&&a.suspendSerial==b.suspendSerial;
}
inline bool freshRecoveryWake(const RecoveryWakeObservation& observation,
        std::uint64_t suspendTick,std::uint64_t now,std::uint64_t maxAge,bool requireSuspend=true,bool requireHardware=true) noexcept {
    const auto& value=observation.projection;
    return observation.boundary.current()&&observation.processBound&&value.size==sizeof(value)&&value.version==1&&
        (requireHardware?powerAdmitted(value.power):(recoveryWatchdogEligible(value.power)&&value.power.phase==0))&&value.committedGeneration==value.power.generation&&
        (!requireSuspend||suspendTick)&&value.committedTick&&value.committedTick>suspendTick&&value.committedTick<=observation.publishedTick&&
        observation.publishedTick<=now&&now-observation.publishedTick<=maxAge;
}
}
