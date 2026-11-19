#pragma once
#include "sleep_power_state.h"
namespace ymcc::sleeppower {
// Coherent observations from the same Native reader; never hardware admission.
inline constexpr bool powerObservationKnown(const SharedPowerV1& value)noexcept{
    return value.size==sizeof(value)&&value.version==1&&value.generation&&value.phase<=3&&(value.flags&kKnown);
}
inline constexpr bool samePowerObservation(const SharedPowerV1& captured,const SharedPowerV1& current)noexcept{
    return powerObservationKnown(captured)&&powerObservationKnown(current)&&captured.generation==current.generation&&
        captured.suspendSerial==current.suspendSerial&&captured.observedNotificationSequence==current.observedNotificationSequence&&
        captured.phase==current.phase&&captured.flags==current.flags&&current.publicationRevision>=captured.publicationRevision;
}
inline bool capturePowerObservation(Reader read,SharedPowerV1& output)noexcept{
    output=SharedPowerV1{};if(!read)return false;
    try{SharedPowerV1 observed;if(read(&observed,sizeof(observed))!=1||!powerObservationKnown(observed))return false;output=observed;return true;}catch(...){return false;}
}
inline bool powerObservationCurrent(Reader read,const SharedPowerV1& captured)noexcept{
    SharedPowerV1 now;return powerObservationKnown(captured)&&capturePowerObservation(read,now)&&samePowerObservation(captured,now);
}
}
