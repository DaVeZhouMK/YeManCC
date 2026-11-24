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
        bool queryObserveOnly=false;
        explicit operator bool() const noexcept {return sequence!=0;}
    };
    enum class QueryDisposition { Candidate, ObserveOnly };
    enum class SuspendDisposition { Accepted, Ignored };
private:
    // Fixed process-lifetime Native sink; it may only revoke owned events.
    using RevocationSink=void(*)(void*) noexcept;
    const RevocationSink revocationSink_;
    void* const revocationContext_;
    mutable std::mutex mutex_;
    HANDLE receiptWake_=nullptr;
    bool projectionWake_=false;
    bool committedWake_=false;
    Sequence lastCommittedWakeGeneration_=0;
    Sequence issued_=0,claimed_=0,completed_=0;
    Receipt latest_; // Latest receipt eligible for delivery, independent of diagnostic-only receipts.
    Receipt latestSleepBoundary_; // Original ingress witness; never a second power owner.
    Receipt latestResumeBoundary_; // Same ordered ingress, only a scalar pause-work veto witness.
    Receipt unclaimedSuspend_; // Actual suspend delivery debt, independent of the post result and diagnostic history.
    Receipt unclaimedResume_;  // Wake after the latest pending suspend, or without one.
    Receipt beforeSuspendResume_; // Quarantined until the pending suspend is admitted.
    Receipt activeBeforeSuspendResume_; // Same debt while its suspend is in the UI handler.
    bool consuming_=false;
    Sequence active_=0;
    std::uint64_t stale_=0,postFailures_=0,replays_=0,deferred_=0;
    std::atomic<bool> pendingSuspend_{false};
    std::atomic<Sequence> observed_{0}; // Last receipt eligible to change public power.
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
    // Automatic wake cannot replace a waiting interactive or critical wake.
    static unsigned resumeRank(ULONG type) noexcept {
        switch(type){
        case PBT_APMRESUMEAUTOMATIC:return 1;
        case PBT_APMRESUMESUSPEND:return 2;
        case PBT_APMRESUMECRITICAL:return 3;
        default:return 0;
        }
    }
    static void retainResume(Receipt& slot,const Receipt& receipt) noexcept {
        if(!receipt)return;
        const auto rank=resumeRank(receipt.type),previous=resumeRank(slot.type);
        if(rank>previous||(rank==previous&&receipt.sequence>slot.sequence))slot=receipt;
    }
    Receipt firstUnclaimedBoundary() const noexcept {
        return unclaimedSuspend_?unclaimedSuspend_:unclaimedResume_;
    }
    void startDelivery(const Receipt& receipt,Sequence tick,bool known) noexcept {
        if(receipt.sequence>claimed_)claimed_=receipt.sequence;
        active_=receipt.sequence;consuming_=true;
        if(receipt.sequence==unclaimedSuspend_.sequence){
            unclaimedSuspend_={};
            activeBeforeSuspendResume_=beforeSuspendResume_;
            beforeSuspendResume_={};
        }
        if(receipt.sequence==unclaimedResume_.sequence)unclaimedResume_={};
        claimTiming(receipt,tick,known);
    }
    Receipt receiveLocked(ULONG type,Sequence tick,bool callback,Sequence fileTime100ns=0,
        QueryDisposition query=QueryDisposition::Candidate) noexcept {
        // No wraparound can turn an old receipt into a new delivery.
        if(!transition(type)||issued_==~Sequence{0})return {};
        const bool queryObserveOnly=!callback&&type==PBT_APMQUERYSUSPEND&&query==QueryDisposition::ObserveOnly;
        const Receipt receipt{++issued_,tick,type,callback,fileTime100ns,queryObserveOnly};
        auto& timing=timingFor(receipt.sequence);
        if(timing.receipt)++timingEvictions_;
        timing=Timing{};timing.receipt=receipt;
        if(queryObserveOnly){
            // Settle at ingress without advancing the delivery watermarks or replacing pending work.
            timing.startedTick=timing.completedTick=tick;
            timing.claimed=timing.completed=timing.startKnown=timing.endKnown=true;
            return receipt;
        }
        latest_=receipt;
        if(type==PBT_APMSUSPEND){
            unclaimedSuspend_=latest_;
            retainResume(beforeSuspendResume_,unclaimedResume_);
            unclaimedResume_={};
        }
        if(type==PBT_APMSUSPEND||type==PBT_APMQUERYSUSPEND){
            latestSleepBoundary_=latest_;
            observedSleepBoundary_.store(latest_.sequence,std::memory_order_release);
        }
        if(resumeRank(type)){
            latestResumeBoundary_=latest_;
            retainResume(unclaimedResume_,latest_);
        }
        if(type==PBT_APMSUSPEND||type==PBT_APMQUERYSUSPEND)
            pendingSuspend_.store(true,std::memory_order_release);
        observed_.store(latest_.sequence,std::memory_order_release);
        if(revocationSink_)revocationSink_(revocationContext_);
        return latest_;
    }
    bool waitsForBoundary(Sequence sequence) const noexcept {
        const auto first=firstUnclaimedBoundary();
        return first&&sequence>first.sequence;
    }
public:
    explicit Dispatch(RevocationSink sink=nullptr,void* context=nullptr) noexcept
        :revocationSink_(sink),revocationContext_(context){}
    ~Dispatch(){closeReceiptWake();}

    // Only the UI owner opens, waits on, and closes the event. Ingress can
    // signal it under the receipt lock while the owner is waiting or closing.
    bool openReceiptWake() {
        std::lock_guard lock(mutex_);
        if(!receiptWake_)receiptWake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        return receiptWake_!=nullptr;
    }
    HANDLE receiptWakeHandle() const noexcept {return receiptWake_;}
    void closeReceiptWake() {
        std::lock_guard lock(mutex_);
        if(receiptWake_){CloseHandle(receiptWake_);receiptWake_=nullptr;}
    }
    void signalReceiptWakeIfPending() {
        std::lock_guard lock(mutex_);
        if(receiptWake_&&(firstUnclaimedBoundary()||latest_.sequence>claimed_))
            SetEvent(receiptWake_);
    }
    bool signalOwnerWake() {
        std::lock_guard lock(mutex_);
        return receiptWake_&&SetEvent(receiptWake_)!=FALSE;
    }
    bool signalProjectionWake() {
        std::lock_guard lock(mutex_);
        if(!receiptWake_||!SetEvent(receiptWake_))return false;
        projectionWake_=true;
        return true;
    }
    bool takeProjectionWake() {
        std::lock_guard lock(mutex_);
        const bool pending=projectionWake_;
        projectionWake_=false;
        return pending;
    }
    // The event is a one-shot transport fallback when both posting and the
    // repair timer fail. Persistent failures must not spin the window pump.
    void signalCommittedWakeOnce(Sequence generation) {
        std::lock_guard lock(mutex_);
        if(generation<=lastCommittedWakeGeneration_||!receiptWake_||!SetEvent(receiptWake_))return;
        lastCommittedWakeGeneration_=generation;
        committedWake_=true;
    }
    bool takeCommittedWake() {
        std::lock_guard lock(mutex_);
        const bool pending=committedWake_;
        committedWake_=false;
        return pending;
    }
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
    Receipt window(ULONG type,Sequence tick,Sequence fileTime100ns=0,
        QueryDisposition query=QueryDisposition::Candidate) {
        std::lock_guard lock(mutex_);
        const auto receipt=receiveLocked(type,tick,false,fileTime100ns,query);
        if(receipt.queryObserveOnly)return receipt;
        if(consuming_||waitsForBoundary(receipt.sequence)){++deferred_;return {};}
        if(receipt)startDelivery(receipt,tick,true);
        return receipt;
    }
    Receipt forwarded(Sequence sequence,ULONG type,Sequence dispatchTick=0) {
        std::lock_guard lock(mutex_);
        const auto boundary=firstUnclaimedBoundary();
        const bool retained=boundary&&sequence==boundary.sequence;
        if(!transition(type)||!sequence||sequence>latest_.sequence||(sequence<=claimed_&&!retained)){++stale_;return {};}
        if((sequence==latest_.sequence&&type!=latest_.type)||(retained&&type!=boundary.type)){++stale_;return {};}
        // Production Query starts come from the window. Only the current
        // eligible Query can owe replay; older observations were never posted.
        if(type==PBT_APMQUERYSUSPEND&&sequence!=latest_.sequence){++stale_;return {};}
        // Older boundary posts cannot bypass the selected suspend or its disposition.
        if(consuming_||waitsForBoundary(sequence)||
            (unclaimedSuspend_&&sequence<unclaimedSuspend_.sequence&&(type==PBT_APMSUSPEND||resumeRank(type)))){++deferred_;return {};}
        // Keep an older queued receipt's original time. Cache overflow retains
        // the original permissive path with unknown time, never a new gate.
        const auto& timing=timingFor(sequence);
        const auto receipt=retained?boundary:sequence==latest_.sequence?latest_:
            timing.receipt.sequence==sequence&&timing.receipt.type==type?timing.receipt:Receipt{sequence,0,type,true};
        startDelivery(receipt,dispatchTick,dispatchTick!=0);
        return receipt;
    }
    void complete(const Receipt& receipt,Sequence completedTick=0,
        SuspendDisposition suspendDisposition=SuspendDisposition::Accepted) {
        if(!receipt||receipt.queryObserveOnly)return;
        std::lock_guard lock(mutex_);
        auto& timing=timingFor(receipt.sequence);
        if(timing.receipt.sequence==receipt.sequence&&!timing.completed){
            timing.completed=true;timing.completedTick=completedTick;timing.endKnown=completedTick!=0;
        }
        if(receipt.sequence>completed_)completed_=receipt.sequence;
        if(receipt.sequence==active_){
            if(receipt.type==PBT_APMSUSPEND){
                if(suspendDisposition==SuspendDisposition::Ignored)
                    retainResume(unclaimedSuspend_?beforeSuspendResume_:unclaimedResume_,activeBeforeSuspendResume_);
                activeBeforeSuspendResume_={};
            }
            consuming_=false;active_=0;
            // Keep the ingress fence closed until both the newest receipt and
            // a wake restored below the claim watermark have been consumed.
            if(latest_.sequence<=completed_&&!firstUnclaimedBoundary())
                pendingSuspend_.store(false,std::memory_order_release);
        }
    }
    Receipt pendingReplay() {
        std::lock_guard lock(mutex_);
        if(consuming_)return {};
        const auto first=firstUnclaimedBoundary();
        if(!first&&latest_.sequence<=claimed_)return {};
        ++replays_;
        return first?first:latest_;
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
        return firstUnclaimedBoundary()||latest_.sequence>claimed_;
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
                {"status",timing.completed?"completed":timing.claimed?"claimed":"received"},
                {"queryObserveOnly",timing.receipt.queryObserveOnly}});
        }
        return {{"schemaVersion",1},{"issued",issued_},{"claimed",claimed_},
            {"completed",completed_},{"staleForwardedIgnored",stale_},
            {"failedPosts",postFailures_},{"replayDeliveries",replays_},
            {"deferredReentrantDeliveries",deferred_},{"consumerActive",consuming_},
            {"pendingSuspend",pendingSuspend()},{"latestSequence",latest_.sequence},
            {"pendingDelivery",bool(firstUnclaimedBoundary())||latest_.sequence>claimed_},
            {"unclaimedResumeSequence",unclaimedResume_.sequence},
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
