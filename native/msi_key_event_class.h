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
