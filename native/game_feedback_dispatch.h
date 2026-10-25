#pragma once
#include "game_feedback_source_ticket.h"
#include <optional>
#include <limits>
namespace ymcc::game_feedback {
// Caller holds the existing serial mutex. No I/O, clock reads or allocation.
class Dispatch {
public:
    enum class Kind { Apply, Stop };
    struct Work {Kind kind=Kind::Apply;SourceBoundCommand command{};std::uint64_t version=0,pulse=0;};
    struct Stats {bool desired=false,dirty=false,busy=false,waitingStop=false,manualBlocked=false,yielding=false;std::uint64_t owner=0,offered=0,coalesced=0,applied=0,expired=0,revoked=0;};
private:
    SourceBoundCommand desired_{},applied_{};
    Stats state_{};
    std::uint64_t version_=0,manualGeneration_=0;
    bool stop_=false,manualActive_=false;
    static bool sameContext(const SourceBoundCommand& a,const SourceBoundCommand& b) noexcept {
        return a.ticket.host==b.ticket.host&&a.ticket.power==b.ticket.power&&a.ticket.intent==b.ticket.intent&&
            a.motors.generation==b.motors.generation&&a.source==b.source;
    }
    static bool sameMotors(const SourceBoundCommand& a,const SourceBoundCommand& b) noexcept {
        return sameContext(a,b)&&a.motors.left==b.motors.left&&a.motors.right==b.motors.right;
    }
    void retire() noexcept {if(state_.owner)stop_=true;state_.desired=false;state_.dirty=false;desired_={};++version_;}
public:
    Stats snapshot()const noexcept{return state_;}
    bool interested()const noexcept{return state_.desired||state_.owner||state_.busy||state_.waitingStop;}
    bool offer(const SourceBoundCommand& frame,bool update) noexcept {
        ++state_.offered;
        if(!frame.valid){const bool had=interested();retire();return had;}
        if(!update){
            if(!state_.desired)return false;
            if(!sameContext(frame,desired_)){retire();return true;}
            desired_.ticket=frame.ticket;desired_.read=frame.read;return false;
        }
        if(!frame.motors.present||!frame.motors.valid)return false;
        if(state_.desired&&sameContext(frame,desired_)&&frame.motors.revision<=desired_.motors.revision)return false;
        ++version_;if(state_.dirty)++state_.coalesced;
        const bool zero=frame.motors.left==0&&frame.motors.right==0;
        if(zero){retire();return true;}
        if(state_.owner&&!sameContext(frame,applied_))stop_=true;
        desired_=frame;state_.desired=true;
        state_.dirty=!(state_.owner&&!stop_&&sameMotors(frame,applied_));
        return state_.dirty&&!state_.manualBlocked&&!state_.yielding;
    }
    void revoke()noexcept{if(interested())++state_.revoked;retire();}
    std::uint64_t deadline()const noexcept {
        if(!state_.desired)return 0;
        const auto at=desired_.read.window.readCompletedTick;
        constexpr auto age=ymcc::kVirtualOutputSourceWindowMaxAgeMs+1;
        return at>std::numeric_limits<std::uint64_t>::max()-age?std::numeric_limits<std::uint64_t>::max():at+age;
    }
    void yieldToJob()noexcept {state_.yielding=true;if(state_.owner)stop_=true;}
    bool jobMayRun()const noexcept{return !state_.busy&&(!state_.owner||state_.waitingStop);}
    void resumeAfterJob()noexcept {state_.yielding=false;if(state_.desired&&!state_.owner)state_.dirty=true;}
    std::optional<Work> take(std::uint64_t now,bool stopping=false)noexcept {
        if(stopping)revoke();
        if(const auto due=deadline();due&&now>=due){++state_.expired;retire();}
        if(state_.busy)return{};
        if(stop_&&state_.owner&&!state_.waitingStop){state_.busy=true;return Work{Kind::Stop,{},version_,state_.owner};}
        if(state_.owner&&state_.waitingStop)return{};
        if(state_.dirty&&state_.desired&&!state_.manualBlocked&&!state_.yielding){
            state_.busy=true;state_.dirty=false;return Work{Kind::Apply,desired_,version_,0};}
        return{};
    }
    void completeApply(const Work& work,const ymcc::touchhaptic::Receipt& receipt) noexcept {
        state_.busy=false;
        if(receipt.reason==ymcc::touchhaptic::Reason::ManualPulseActive&&!receipt.attempted){state_.manualBlocked=true;state_.dirty=state_.desired;return;}
        if(receipt.pulseGeneration&&receipt.pulseGeneration>manualGeneration_){
            state_.owner=receipt.pulseGeneration;applied_=work.command;
            if(receipt.ok)++state_.applied;
            const bool stillDesired=state_.desired&&sameMotors(desired_,work.command);
            if(!receipt.ok||!stillDesired){stop_=true;if(!receipt.ok)retire();}
            else{state_.dirty=false;stop_=false;}
        }else if(receipt.pulseGeneration&&receipt.pulseGeneration<=manualGeneration_){
            state_.manualBlocked=manualActive_;state_.dirty=state_.desired;
        }else if(!receipt.ok){retire();}
    }
    void completeStop(const Work& work,bool settled)noexcept {
        state_.busy=false;
        if(state_.owner!=work.pulse)return;
        if(settled){state_.owner=0;state_.waitingStop=false;stop_=false;applied_={};if(state_.desired)state_.dirty=true;}
        else{state_.waitingStop=true;stop_=false;}
    }
    void manualStarted(std::uint64_t generation)noexcept {
        if(generation<=manualGeneration_)return;
        manualGeneration_=generation;manualActive_=true;state_.owner=0;state_.waitingStop=false;stop_=false;state_.manualBlocked=true;state_.dirty=state_.desired;applied_={};
    }
    void ownerFinished(std::uint64_t generation)noexcept {
        if(state_.owner==generation){state_.owner=0;state_.waitingStop=false;stop_=false;applied_={};if(state_.desired)state_.dirty=true;}
        if(generation==manualGeneration_){manualActive_=false;state_.manualBlocked=false;if(state_.desired)state_.dirty=true;}
    }
};
}
