#pragma once
#include <cstdint>
#include "json.hpp"

// Reuse the existing 1 Hz sampled snapshot. No input API, clock or I/O here.
// The existing diagnostic owner serializes this policy with its log drain.
namespace ymcc::keyboardEvents {
struct ModifierState {
    bool present=false,asyncKnown=false,queueStatusKnown=false,queueReadOk=false,queueKnown=false;
    std::uint16_t asyncMask=0,queueMask=0;
    bool operator==(const ModifierState&)const=default;
    bool nonzero()const noexcept{return (asyncKnown&&asyncMask)||(queueKnown&&queueMask);}
};
inline ModifierState modifierStateAtDrain(const nlohmann::json& idle) {
    ModifierState state;
    const auto it=idle.find("keyboardModifiers");
    if(it==idle.end()||!it->is_object())return state;
    state.present=true;
    auto readMask=[](const nlohmann::json& object,const char* name,std::uint16_t& value){
        const auto v=object.find(name);
        if(v==object.end()||!v->is_number_integer())return false;
        if(v->is_number_unsigned()){
            const auto n=v->get<std::uint64_t>();if(n>255)return false;value=static_cast<std::uint16_t>(n);
        }else{
            const auto n=v->get<std::int64_t>();if(n<0||n>255)return false;value=static_cast<std::uint16_t>(n);
        }
        return true;
    };
    state.asyncKnown=readMask(*it,"asyncDownMask",state.asyncMask);
    const auto q=it->find("callingThreadQueueReadOk");
    state.queueStatusKnown=q!=it->end()&&q->is_boolean();
    state.queueReadOk=state.queueStatusKnown&&q->get<bool>();
    state.queueKnown=state.queueReadOk&&readMask(*it,"callingThreadQueueDownMask",state.queueMask);
    return state;
}
struct RetentionDecision {bool emit=false,stateChanged=false,nonzeroHeartbeat=false;};
class ModifierRetention {
    bool known_=false;
    ModifierState last_{};
    std::uint64_t epoch_=0,lastAttemptTick_=0;
public:
    static constexpr std::uint64_t heartbeatMs=30000;
    RetentionDecision decide(const ModifierState& state,std::uint64_t epoch,std::uint64_t now,
                             bool eventsOrLoss,bool coverageChanged)const noexcept {
        const bool changed=!known_||epoch_!=epoch||!(last_==state);
        const bool heartbeat=!changed&&state.nonzero()&&
            (now<lastAttemptTick_||now-lastAttemptTick_>=heartbeatMs);
        return {eventsOrLoss||coverageChanged||changed||heartbeat,changed,heartbeat};
    }
    // This records an append attempt; the existing writer does not provide a
    // durable-write receipt. Do not interpret this as successful disk retention.
    void attempted(const ModifierState& state,std::uint64_t epoch,std::uint64_t now)noexcept{
        last_=state;epoch_=epoch;lastAttemptTick_=now;known_=true;
    }
};
} // namespace ymcc::keyboardEvents
