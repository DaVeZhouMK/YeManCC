#pragma once
#include <chrono>
#include <cstdint>
#include <type_traits>
#include "touch_haptic_dispatch.h"
namespace ymcc::game_feedback {
// Process-lifetime counters. The existing pulse mutex owns every mutation.
// An API success never claims physical motor motion or firmware acknowledgement.
struct ExecutionStats {
    std::uint64_t writeCalls=0,writeAttempts=0,writeAccepted=0,writeFailed=0,writeRefused=0,writeNoop=0;
    std::uint64_t stopCalls=0,stopAttempts=0,stopAccepted=0,stopFailed=0,stopPending=0,stopNoAttempt=0;
    std::uint64_t measuredWrites=0,writeElapsedUs=0,writeMaxUs=0,measuredStops=0,stopElapsedUs=0,stopMaxUs=0;
};
static_assert(std::is_trivially_copyable_v<ExecutionStats>);
template<class Clock=std::chrono::steady_clock> class WriteObservation {
    ExecutionStats& stats_;const touchhaptic::Receipt& result_;
    bool enabled_,measure_;typename Clock::time_point started_{};
public:
    WriteObservation(ExecutionStats& stats,const touchhaptic::Receipt& result,bool enabled,bool measure)noexcept
        :stats_(stats),result_(result),enabled_(enabled),measure_(enabled&&measure){if(measure_)started_=Clock::now();}
    ~WriteObservation()noexcept {
        if(!enabled_)return;++stats_.writeCalls;
        if(result_.attempted){++stats_.writeAttempts;if(result_.ok)++stats_.writeAccepted;else ++stats_.writeFailed;}
        else if(result_.ok)++stats_.writeNoop;else ++stats_.writeRefused;
        if(measure_){const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-started_).count();
            const auto us=elapsed>0?static_cast<std::uint64_t>(elapsed):0;++stats_.measuredWrites;stats_.writeElapsedUs+=us;if(us>stats_.writeMaxUs)stats_.writeMaxUs=us;}
    }
};
template<class Clock=std::chrono::steady_clock> class StopObservation {
    ExecutionStats& stats_;const bool& attempted_;const bool& accepted_;const bool& pending_;
    bool enabled_,measure_;typename Clock::time_point started_{};
public:
    StopObservation(ExecutionStats& stats,const bool& attempted,const bool& accepted,const bool& pending,bool enabled,bool measure)noexcept
        :stats_(stats),attempted_(attempted),accepted_(accepted),pending_(pending),enabled_(enabled),measure_(enabled&&measure){if(measure_)started_=Clock::now();}
    ~StopObservation()noexcept {
        if(!enabled_)return;++stats_.stopCalls;
        if(attempted_){++stats_.stopAttempts;if(accepted_)++stats_.stopAccepted;else ++stats_.stopFailed;}
        else ++stats_.stopNoAttempt;
        if(pending_)++stats_.stopPending;
        if(measure_){const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-started_).count();
            const auto us=elapsed>0?static_cast<std::uint64_t>(elapsed):0;++stats_.measuredStops;stats_.stopElapsedUs+=us;if(us>stats_.stopMaxUs)stats_.stopMaxUs=us;}
    }
};
}
