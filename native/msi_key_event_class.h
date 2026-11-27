#pragma once
#include <cstdint>
namespace ymcc::msi {
enum class EventClassState { Present, Absent, Unknown, Cancelled };
struct EventClassProbe {
    EventClassState state=EventClassState::Unknown;
    int32_t beginStatus=0, waitStatus=0;
    bool requestIssued=false, classObject=false;
};
// GetObject's asynchronous call acceptance is not class availability. In
// particular WBEM_S_TIMEDOUT is a positive HRESULT, NOT an available class.
inline bool classObjectAdmitted(int32_t status,bool object) {
    return status==0 && object;
}
inline bool classDefinitelyAbsent(int32_t status) {
    return uint32_t(status)==0x80041010u || uint32_t(status)==0x80041002u;
}

// Only the serial watcher writes this log state. Retry decisions never read it.
struct EventClassRetryLogState {
    struct Selection { bool detail=false; unsigned sameRetries=0; };
    uint64_t generation=0,power=0,tick=0,deliveryEpoch=0;
    int32_t beginStatus=0,waitStatus=0;
    unsigned sameRetries=0;

    Selection observe(const EventClassProbe& probe,uint64_t source,uint64_t readyPower,uint64_t now,
                      uint64_t failedAt,uint64_t backoff,uint64_t epoch,bool deliveryUncertain) noexcept {
        if(!source||!readyPower||!probe.requestIssued||probe.classObject||
            probe.state!=EventClassState::Absent||!classDefinitelyAbsent(probe.waitStatus)) {
            *this={};return {};
        }
        const auto age=now>=failedAt?now-failedAt:0;
        const bool same=!deliveryUncertain&&generation&&source>generation&&source-generation==1&&
            power==readyPower&&epoch==deliveryEpoch&&failedAt&&failedAt>=tick&&
            age>=backoff&&age-backoff<=backoff&&beginStatus==probe.beginStatus&&waitStatus==probe.waitStatus;
        generation=source;power=readyPower;tick=now;deliveryEpoch=epoch;
        beginStatus=probe.beginStatus;waitStatus=probe.waitStatus;
        sameRetries=same?sameRetries+1:0;
        const Selection selection{sameRetries!=0&&sameRetries<10,sameRetries};
        if(!selection.detail)sameRetries=0;
        return selection;
    }
};

template<class Reader,class Current> EventClassProbe probeEventClass(Reader& reader,Current current) {
    EventClassProbe r;
    if(!current()){r.state=EventClassState::Cancelled;return r;}
    r.beginStatus=reader.begin();r.requestIssued=true;
    if(r.beginStatus<0){r.waitStatus=r.beginStatus;r.state=classDefinitelyAbsent(r.beginStatus)?EventClassState::Absent:EventClassState::Unknown;return r;}
    const auto result=reader.wait(500);r.waitStatus=result.first;r.classObject=result.second;
    if(!current()){r.state=EventClassState::Cancelled;return r;}
    r.state=classObjectAdmitted(r.waitStatus,r.classObject)?EventClassState::Present:
        classDefinitelyAbsent(r.waitStatus)?EventClassState::Absent:EventClassState::Unknown;
    return r;
}
// Definite absence/cancellation refuses subscription. UNKNOWN preserves the
// pre-existing async listener rather than disabling a potentially healthy
// 258V/A1M provider because a new metadata read was slow/permission-limited.
inline bool maySubscribeEventClass(EventClassState state) {
    return state==EventClassState::Present || state==EventClassState::Unknown;
}
inline const char* eventClassStateName(EventClassState state) {
    switch(state){case EventClassState::Present:return "present";case EventClassState::Absent:return "absent";case EventClassState::Cancelled:return "cancelled";default:return "unknown";}
}
}
