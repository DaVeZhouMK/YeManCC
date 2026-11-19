#pragma once
#include <windows.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <mutex>
#include "json.hpp"

namespace ymcc::sleeppower {
// The sole in-process receipt order. This owns delivery, never game/device
// cleanup, a second power generation, or permission to invent a resume.
// Receive time is process-observed, not the original Windows transition time.
class Dispatch {
public:
    using Sequence=std::uint64_t;
    struct Receipt {
        Sequence sequence=0,tick=0;
        ULONG type=0;
        bool callback=false;
        Sequence fileTime100ns=0;
        explicit operator bool() const noexcept {return sequence!=0;}
    };
private:
    // Fixed process-lifetime Native sink; it may only revoke owned events.
    using RevocationSink=void(*)(void*) noexcept;
    const RevocationSink revocationSink_;
    void* const revocationContext_;
    mutable std::mutex mutex_;
    Sequence issued_=0,claimed_=0,completed_=0;
    Receipt latest_;
    Receipt latestSleepBoundary_; // Original ingress witness; never a second power owner.
    Receipt latestResumeBoundary_; // Same ordered ingress, only a scalar pause-work veto witness.
    bool consuming_=false;
    Sequence active_=0;
    std::uint64_t stale_=0,postFailures_=0,replays_=0,deferred_=0;
    std::atomic<bool> pendingSuspend_{false};
    std::atomic<Sequence> observed_{0};
    std::atomic<Sequence> observedSleepBoundary_{0};
    // Bounded evidence only; eviction never changes delivery or cleanup duties.
    struct Timing {
        Receipt receipt;
        Sequence startedTick=0,completedTick=0;
        bool claimed=false,completed=false,startKnown=false,endKnown=false;
    };
    static constexpr std::size_t timingLimit_=8;
    std::array<Timing,timingLimit_> timing_{};
    std::uint64_t timingEvictions_=0;
    Timing& timingFor(Sequence sequence) noexcept {return timing_[(sequence-1)%timingLimit_];}
    void claimTiming(const Receipt& receipt,Sequence tick,bool known) noexcept {
        auto& timing=timingFor(receipt.sequence);
        if(timing.receipt.sequence!=receipt.sequence)return;
        timing.claimed=true;timing.startedTick=tick;timing.startKnown=known;
    }
    Receipt receiveLocked(ULONG type,Sequence tick,bool callback,Sequence fileTime100ns=0) noexcept {
        // No wraparound can turn an old receipt into a new delivery.
        if(!transition(type)||issued_==~Sequence{0})return {};
        latest_={++issued_,tick,type,callback,fileTime100ns};
        if(type==PBT_APMSUSPEND||type==PBT_APMQUERYSUSPEND){
            latestSleepBoundary_=latest_;
            observedSleepBoundary_.store(latest_.sequence,std::memory_order_release);
        }
        if(type==PBT_APMRESUMEAUTOMATIC||type==PBT_APMRESUMESUSPEND||type==PBT_APMRESUMECRITICAL)
            latestResumeBoundary_=latest_;
        auto& timing=timingFor(latest_.sequence);
        if(timing.receipt)++timingEvictions_;
        timing=Timing{};timing.receipt=latest_;
        if(type==PBT_APMSUSPEND||type==PBT_APMQUERYSUSPEND)
            pendingSuspend_.store(true,std::memory_order_release);
        observed_.store(latest_.sequence,std::memory_order_release);
        if(revocationSink_)revocationSink_(revocationContext_);
        return latest_;
    }
public:
    explicit Dispatch(RevocationSink sink=nullptr,void* context=nullptr) noexcept
        :revocationSink_(sink),revocationContext_(context){}
    static bool transition(ULONG type) noexcept {
        switch(type){
        case PBT_APMSUSPEND:case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:case PBT_APMRESUMECRITICAL:
        case PBT_APMQUERYSUSPEND:case PBT_APMQUERYSUSPENDFAILED:
        case PBT_APMQUERYSTANDBYFAILED:return true;
        default:return false;
        }
    }
    // Serialize only receipt + immediate sensor stop + nonblocking window post.
    // No lifecycle worker, device/Host operation, JSON, disk or wait belongs in
    // this callback. A direct window receipt cannot overtake that small ingress.
    template<class Forward> bool callback(ULONG type,Sequence tick,Forward&& forward,Sequence fileTime100ns=0) {
        std::lock_guard lock(mutex_);
        const auto receipt=receiveLocked(type,tick,true,fileTime100ns);
        if(!receipt)return false;
        const bool posted=forward(receipt);
        if(!posted)++postFailures_;
        return posted;
    }
    Receipt window(ULONG type,Sequence tick,Sequence fileTime100ns=0) {
        std::lock_guard lock(mutex_);
        const auto receipt=receiveLocked(type,tick,false,fileTime100ns);
        if(consuming_){++deferred_;return {};}
        if(receipt){claimed_=receipt.sequence;active_=receipt.sequence;consuming_=true;claimTiming(receipt,tick,true);}
        return receipt;
    }
    Receipt forwarded(Sequence sequence,ULONG type,Sequence dispatchTick=0) {
        std::lock_guard lock(mutex_);
        if(!transition(type)||!sequence||sequence>issued_||sequence<=claimed_){++stale_;return {};}
        if(sequence==latest_.sequence&&type!=latest_.type){++stale_;return {};}
        if(consuming_){++deferred_;return {};}
        claimed_=sequence;
        active_=sequence;consuming_=true;
        // Keep an older queued receipt's original time. Cache overflow retains
        // the original permissive path with unknown time, never a new gate.
        const auto& timing=timingFor(sequence);
        const auto receipt=sequence==latest_.sequence?latest_:
            timing.receipt.sequence==sequence&&timing.receipt.type==type?timing.receipt:Receipt{sequence,0,type,true};
        claimTiming(receipt,dispatchTick,dispatchTick!=0);
        return receipt;
    }
    void complete(const Receipt& receipt,Sequence completedTick=0) {
        if(!receipt)return;
        std::lock_guard lock(mutex_);
        auto& timing=timingFor(receipt.sequence);
        if(timing.receipt.sequence==receipt.sequence&&!timing.completed){
            timing.completed=true;timing.completedTick=completedTick;timing.endKnown=completedTick!=0;
        }
        if(receipt.sequence>completed_)completed_=receipt.sequence;
        if(receipt.sequence==active_){consuming_=false;active_=0;}
        // A newer received suspend must keep the PUBLIC ingress fence until
        // its own consumer has run; an older resume cannot clear it afterward.
        if(receipt.sequence==latest_.sequence)
            pendingSuspend_.store(false,std::memory_order_release);
    }
    Receipt pendingReplay() {
        std::lock_guard lock(mutex_);
        if(consuming_||latest_.sequence<=claimed_)return {};
        ++replays_;
        return latest_;
    }
    Receipt latestSleepBoundary() const {
        std::lock_guard lock(mutex_);
        return latestSleepBoundary_;
    }
    Receipt latestResumeBoundary() const {
        std::lock_guard lock(mutex_);
        return latestResumeBoundary_;
    }
    bool pendingSuspend() const noexcept {
        return pendingSuspend_.load(std::memory_order_acquire);
    }
    Sequence observedSequence() const noexcept {return observed_.load(std::memory_order_acquire);}
    Sequence observedSleepBoundarySequence() const noexcept {return observedSleepBoundary_.load(std::memory_order_acquire);}
    bool hasPending() const {
        std::lock_guard lock(mutex_);
        return latest_.sequence>claimed_;
    }
    nlohmann::json snapshot() const {
        std::lock_guard lock(mutex_);
        auto recent=nlohmann::json::array();
        const auto retained=issued_<timingLimit_?issued_:timingLimit_;
        const auto first=issued_-retained+1;
        for(Sequence index=0;index<retained;++index){
            const auto sequence=first+index;
            const auto& timing=timing_[(sequence-1)%timingLimit_];
            if(timing.receipt.sequence!=sequence)continue;
            const bool queueKnown=timing.startKnown&&timing.startedTick>=timing.receipt.tick;
            const bool executionKnown=timing.startKnown&&timing.endKnown&&timing.completedTick>=timing.startedTick;
            recent.push_back({{"receiptSequence",sequence},{"type",timing.receipt.type},
                {"source",timing.receipt.callback?"callback":"window"},{"receiptTickMs",timing.receipt.tick},{"receiptFileTime100ns",timing.receipt.fileTime100ns},
                {"dispatchStartedTickMs",timing.startKnown?nlohmann::json(timing.startedTick):nlohmann::json(nullptr)},
                {"completedTickMs",timing.endKnown?nlohmann::json(timing.completedTick):nlohmann::json(nullptr)},
                {"dispatchWaitMs",queueKnown?nlohmann::json(timing.startedTick-timing.receipt.tick):nlohmann::json(nullptr)},
                {"handlerElapsedMs",executionKnown?nlohmann::json(timing.completedTick-timing.startedTick):nlohmann::json(nullptr)},
                {"status",timing.completed?"completed":timing.claimed?"claimed":"received"}});
        }
        return {{"schemaVersion",1},{"issued",issued_},{"claimed",claimed_},
            {"completed",completed_},{"staleForwardedIgnored",stale_},
            {"failedPosts",postFailures_},{"replayDeliveries",replays_},
            {"deferredReentrantDeliveries",deferred_},{"consumerActive",consuming_},
            {"pendingSuspend",pendingSuspend()},{"latestSequence",latest_.sequence},
            {"latestType",latest_.type},{"latestCallback",latest_.callback},
            {"latestReceiptTickMs",latest_.tick},
            {"recentDeliveries",recent},{"retainedDeliveryLimit",timingLimit_},
            {"diagnosticHistoryEvictions",timingEvictions_},{"diagnosticHistoryOwnsAuthority",false},
            {"elapsedScope","caller-boot-tick;may-include-suspension;not-CPU-time-or-physical-sleep-proof"},
            {"clockScope","process-receipt-order-not-original-Windows-event-time"},
            {"ownsPowerGeneration",false},{"ownsDeviceCleanup",false}};
    }
};
} // namespace ymcc::sleeppower
