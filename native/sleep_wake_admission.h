#pragma once
#include <cstdint>
#include <limits>
#include <mutex>
#include "sleep_power_state.h"
namespace ymcc::sleeppower {
// Listener delivery identity only. This never admits business work or refunds
// the consumer's original episode, reservations, deadlines or cancellation.
inline bool wakeDeliveryAlreadyCurrent(bool known,const SharedPowerV1& delivered,const SharedPowerV1& current,
                                       std::uint64_t deliveredRevision,std::uint64_t currentRevision) noexcept {
    return known&&samePowerRevocation(delivered,current)&&deliveredRevision==currentRevision;
}

// Native evidence only. This shared policy never reads Windows or grants a
// consumer action, hardware capability, generation or cancellation renewal.
struct CompletedWake {
    std::uint64_t generation=0,epoch=0,boundary=0,boundaryGeneration=0;
    std::uint64_t readyGeneration=0,readyEpoch=0,readyBoundary=0;
    std::uint64_t interactiveGeneration=0,interactiveEpoch=0,interactiveBoundary=0,interactiveAt=0;
    std::uint64_t completedGeneration=0,completedEpoch=0,completedBoundary=0,completedAt=0;
    std::uint64_t latestEntry=0,now=0,limitMs=0;int eventId=0;
    bool ownerAllowed=false,retrying=true,ready=false;
};
enum class CompletedWakeResult {Context,Window,Kind,Gap,Allowed};
inline bool followsWakeBoundary(std::uint64_t observed,std::uint64_t generation,std::uint64_t boundaryGeneration) noexcept {
    return observed==generation||(observed&&observed!=(std::numeric_limits<std::uint64_t>::max)()&&
        observed+1==generation&&boundaryGeneration==observed);
}
inline CompletedWakeResult completedWake(const CompletedWake& v) noexcept {
    if(!v.generation||!v.epoch||!v.boundary||!v.limitMs||!v.ownerAllowed||v.retrying||!v.ready||
       v.readyGeneration!=v.generation||v.readyEpoch!=v.epoch||v.readyBoundary!=v.boundary||
       v.interactiveEpoch!=v.epoch||v.completedEpoch!=v.epoch||
       !followsWakeBoundary(v.interactiveGeneration,v.generation,v.boundaryGeneration)||
       !followsWakeBoundary(v.completedGeneration,v.generation,v.boundaryGeneration)||
       v.interactiveBoundary!=v.boundary||v.completedBoundary!=v.boundary)return CompletedWakeResult::Context;
    const auto limit=v.limitMs*10000ULL;
    if(v.interactiveAt<=v.boundary||v.interactiveAt<=v.latestEntry||v.completedAt<=v.boundary||v.completedAt<=v.latestEntry||
       v.interactiveAt>v.now||v.completedAt>v.now||v.now-v.interactiveAt>=limit)return CompletedWakeResult::Window;
    if(v.eventId==107)return CompletedWakeResult::Allowed;
    if(v.eventId!=507||v.now-v.completedAt>=limit)return CompletedWakeResult::Kind;
    if(v.interactiveAt<v.completedAt&&v.completedAt-v.interactiveAt>5000ULL*10000ULL)return CompletedWakeResult::Gap;
    return CompletedWakeResult::Allowed;
}
struct WakeIdentity {
    std::uint64_t generation=0,epoch=0,request=0,boundary=0,pid=0,birth=0;
    bool operator==(const WakeIdentity&) const = default;
};
// A consumer may retain one zero-action episode while the shared sleep retry
// settles. The original owner supplies currentness and action reservations.
// Admission never refunds clocks or the old proof's UTC observation deadline.
class WakeContinuation {
    WakeIdentity original_{};std::uint64_t armed_=0,stage_=0,retired_=0;bool awaiting_=false;
public:
    void begin(WakeIdentity original,std::uint64_t armed,std::uint64_t stage) noexcept {
        original_=original;armed_=armed;stage_=stage;retired_=0;awaiting_=false;
    }
    bool awaiting() const noexcept{return awaiting_;}
    std::uint64_t armed() const noexcept{return armed_;}
    std::uint64_t stage() const noexcept{return stage_;}
    std::uint64_t retiredEvent() const noexcept{return retired_;}
    bool current(const WakeIdentity& original,std::uint64_t tick,std::uint64_t utc,bool ownerCurrent,bool zeroActions) const noexcept {
        return ownerCurrent&&zeroActions&&original==original_&&original.generation&&original.epoch&&
            original.request&&original.boundary&&original.pid&&original.birth&&tick>=armed_&&tick>=stage_&&
            tick-armed_<30000&&tick-stage_<=45000&&
            (!retired_||(utc>=retired_&&utc-retired_<60000ULL*10000ULL));
    }
    bool park(const WakeIdentity& original,std::uint64_t tick,std::uint64_t utc,int eventId,
              std::uint64_t eventAt,bool ownerCurrent,bool zeroActions) noexcept {
        if(awaiting_||eventId!=107||eventAt<=original.boundary||eventAt>utc||
           utc-eventAt>=60000ULL*10000ULL||!current(original,tick,utc,ownerCurrent,zeroActions))return false;
        if(!retired_)retired_=eventAt;
        awaiting_=true;return true;
    }
    // A late original entry can invalidate a previously admitted interactive
    // ingress while the same final exit remains current. Retain only this
    // exact zero-action episode and its first deadlines until a valid PBT joins.
    bool parkInteractiveEntry(const WakeIdentity& original,std::uint64_t tick,std::uint64_t utc,
                              std::uint64_t interactiveAt,std::uint64_t completedAt,std::uint64_t latestEntry,
                              bool ownerCurrent,bool zeroActions) noexcept {
        if(awaiting_||!latestEntry||latestEntry<=original.boundary||interactiveAt<=original.boundary||
           interactiveAt>latestEntry||completedAt<=latestEntry||completedAt>utc||
           utc-interactiveAt>=60000ULL*10000ULL||!current(original,tick,utc,ownerCurrent,zeroActions))return false;
        if(!retired_)retired_=interactiveAt;
        awaiting_=true;return true;
    }
    // A later original entry retires an already admitted strict exit. Keep
    // the exact unused episode and its first observation anchors until a new
    // completed public join arrives; repeated entries cannot renew those clocks.
    bool parkSupersededEntry(const WakeIdentity& original,std::uint64_t tick,std::uint64_t utc,
                             std::uint64_t eventAt,std::uint64_t latestEntry,
                             bool ownerCurrent,bool zeroActions) noexcept {
        if(awaiting_||!latestEntry||eventAt<=original.boundary||eventAt>latestEntry||latestEntry>utc||
           utc-eventAt>=60000ULL*10000ULL||!current(original,tick,utc,ownerCurrent,zeroActions))return false;
        if(!retired_)retired_=eventAt;
        awaiting_=true;return true;
    }
    bool canContinue(const WakeIdentity& original,std::uint64_t tick,std::uint64_t utc,bool ownerCurrent,bool zeroActions,
                     bool classified,bool retrying,bool nativeJoined,int completedId,std::uint64_t completedAt,
                     int strictId,std::uint64_t strictAt) const noexcept {
        return awaiting_&&current(original,tick,utc,ownerCurrent,zeroActions)&&classified&&!retrying&&nativeJoined&&
            completedId==507&&completedAt>retired_&&(!strictAt||(strictId==507&&strictAt==completedAt));
    }
    void resumed() noexcept{awaiting_=false;}
    bool operationCurrent(std::uint64_t utc) const noexcept {
        return !retired_||(utc>=retired_&&utc-retired_<60000ULL*10000ULL);
    }
};
}

// One Native owner, fed by original event/PBT/Ready producers. Config epoch is
// the Native power generation at publication; consumer configuration is not a source.
namespace ymcc::sleeppower {
struct KernelWake {unsigned long long generation=0,sourceGeneration=0;std::uint64_t boundaryFileTime=0,eventFileTime=0;int eventId=0;bool awaitedCapture=false,uiWakeRequested=false;unsigned long long requestGeneration=0;};

struct KernelSleepEntry {unsigned long long sourceGeneration=0,generation=0;std::uint64_t eventFileTime=0;};

// Independent dynamic wake evidence. A reason code is not a compatibility
// gate: any completed 507 can join a fresh interactive OS resume and this
// generation's committed Ready. Neither raw notification alone grants work.
struct InteractiveResume {unsigned long long generation=0,sourceGeneration=0;std::uint64_t boundaryFileTime=0,eventFileTime=0,receiptTick=0;};
struct CompletedResume {unsigned long long generation=0,sourceGeneration=0;std::uint64_t boundaryFileTime=0,eventFileTime=0;int eventId=0,reason=0;};
struct ReadyCommit {unsigned long long generation=0,sourceGeneration=0;std::uint64_t boundaryFileTime=0;};




struct WakeFacts {std::uint64_t revision=0;KernelWake kernelWake;KernelSleepEntry kernelSleepEntry;InteractiveResume interactiveResume;CompletedResume completedResume;ReadyCommit readyCommit;};
// Consumer revocation tags are projections; the stored source remains Native.
template<class NativeFact> struct ConsumerWakeFact : NativeFact {std::uint64_t configEpoch=0;};
struct WakeProjection {std::uint64_t sourceRevision=0;ConsumerWakeFact<KernelWake> kernelWake;ConsumerWakeFact<KernelSleepEntry> kernelSleepEntry;ConsumerWakeFact<InteractiveResume> interactiveResume;ConsumerWakeFact<CompletedResume> completedResume;ConsumerWakeFact<ReadyCommit> readyCommit;};
class WakeFactsOwner {
    WakeFacts facts_,completedFacts_;
    bool completedFactsKnown_=false;
    void changedLocked() noexcept {if(facts_.revision!=(std::numeric_limits<std::uint64_t>::max)())++facts_.revision;}
    void retainCompletedLocked() noexcept {
        if(completedFactsKnown_)return;
        const auto& i=facts_.interactiveResume;const auto& c=facts_.completedResume;const auto& r=facts_.readyCommit;
        if(!i.generation||i.generation!=c.generation||i.generation!=r.generation||
            i.sourceGeneration!=i.generation||c.sourceGeneration!=c.generation||r.sourceGeneration!=r.generation||
            !i.eventFileTime||!c.eventFileTime||i.eventFileTime<=facts_.kernelSleepEntry.eventFileTime||
            c.eventFileTime<=facts_.kernelSleepEntry.eventFileTime)return;
        if(c.eventId!=107&&(c.eventId!=507||(i.eventFileTime<c.eventFileTime&&c.eventFileTime-i.eventFileTime>5000ULL*10000ULL)))return;
        completedFacts_=facts_;completedFactsKnown_=true;
    }
public:
    mutable std::mutex mutex;
    // Native callers already hold mutex; consumers receive a value snapshot.
    WakeFacts snapshotLocked() const {return facts_;}
    void resetLocked() noexcept{facts_={};completedFacts_={};completedFactsKnown_=false;}
    // Boundary attachment is a projection of an existing caller's captured
    // native tuple. It never changes the one original event timestamp.
    WakeProjection snapshotForLocked(std::uint64_t gen,std::uint64_t consumerEpoch,std::uint64_t boundary,std::uint64_t boundaryGeneration) const {
        const auto& source=completedFactsKnown_&&followsWakeBoundary(completedFacts_.completedResume.generation,gen,boundaryGeneration)?completedFacts_:facts_;
        WakeProjection out{facts_.revision,{source.kernelWake,0},{source.kernelSleepEntry,0},{source.interactiveResume,0},{source.completedResume,0},{source.readyCommit,0}};
        const auto project=[&](auto& value){
            const bool belongs=value.generation&&value.sourceGeneration==value.generation&&followsWakeBoundary(value.generation,gen,boundaryGeneration);
            if(!belongs){value={};return;}
            value.configEpoch=consumerEpoch;
        };
        project(out.kernelWake);project(out.kernelSleepEntry);project(out.interactiveResume);project(out.completedResume);project(out.readyCommit);
        const auto bind=[&](auto& value){if(!value.boundaryFileTime&&value.configEpoch==consumerEpoch)value.boundaryFileTime=boundary;};
        bind(out.kernelWake);bind(out.interactiveResume);bind(out.completedResume);bind(out.readyCommit);
        return out;
    }
    // Safe public read: one coherent value, no caller lock or borrowed fields.
    WakeFacts snapshot() const {std::lock_guard lock(mutex);return facts_;}
    bool kernel(std::uint64_t gen,int id,int reason,int target,std::uint64_t at,std::uint64_t now) {
        std::lock_guard lock(mutex);const auto before=facts_.revision;
        if(id==506)(void)entryLocked(gen,gen,at,now);
        else (void)kernelLocked(gen,gen,0,id,reason,target,at,now,false);
        return facts_.revision!=before;
    }
    void interactive(std::uint64_t gen,int code,std::uint64_t receipt,std::uint64_t tick,std::uint64_t utc) {
        std::lock_guard lock(mutex);interactiveLocked(gen,gen,0,code,receipt,tick,utc,false);
    }
    void ready(std::uint64_t gen) {
        std::lock_guard lock(mutex);readyLocked(gen,gen,0);
    }

    bool entryLocked(std::uint64_t gen,std::uint64_t epoch,std::uint64_t at,std::uint64_t now) noexcept {
        if(!gen||!epoch||!at||at>now)return false;
        if(facts_.kernelSleepEntry.eventFileTime>=at)return false;
        if(completedFactsKnown_&&(at>=completedFacts_.interactiveResume.eventFileTime||
           at>=completedFacts_.completedResume.eventFileTime)){completedFactsKnown_=false;completedFacts_={};}
        facts_.kernelSleepEntry={epoch,gen,at};changedLocked();
        if(facts_.readyCommit.generation==gen&&at>=facts_.completedResume.eventFileTime)facts_.readyCommit={};
        // A late entry may invalidate the first copy after a newer, valid PBT
        // is already present. Freeze that qualified tuple without new clocks.
        retainCompletedLocked();
        return true;
    }
    void interactiveLocked(std::uint64_t gen,std::uint64_t epoch,std::uint64_t boundary,int code,
                           std::uint64_t receipt,std::uint64_t tick,std::uint64_t utc,bool admittedEpisode) noexcept {
        if(admittedEpisode||(code!=7&&code!=6)||!gen||!epoch||receipt>tick||tick-receipt>=60000)return;
        const auto age=(tick-receipt)*10000ULL;if(utc<=age||utc-age<=boundary)return;
        if(facts_.interactiveResume.sourceGeneration==epoch&&facts_.interactiveResume.boundaryFileTime==boundary&&receipt<=facts_.interactiveResume.receiptTick)return;
        facts_.interactiveResume={gen,epoch,boundary,utc-age,receipt};changedLocked();retainCompletedLocked();
    }
    void completedLocked(std::uint64_t gen,std::uint64_t epoch,std::uint64_t boundary,int id,int reason,int target,
                         std::uint64_t at,std::uint64_t now) noexcept {
        if((id!=507&&!(id==107&&target==5))||!gen||!epoch||at<=boundary||at>now||
            (facts_.kernelSleepEntry.sourceGeneration==epoch&&at<=facts_.kernelSleepEntry.eventFileTime))return;
        if(facts_.completedResume.eventFileTime>=at)return;
        facts_.completedResume={gen,epoch,boundary,at,id,reason};changedLocked();retainCompletedLocked();
    }
    bool kernelLocked(std::uint64_t gen,std::uint64_t epoch,std::uint64_t boundary,int id,int reason,int target,
                      std::uint64_t at,std::uint64_t now,bool captureMatched) noexcept {
        completedLocked(gen,epoch,boundary,id,reason,target,at,now);
        if(!((id==507&&reason==1)||(id==107&&target==5))||!gen||!epoch||!at||at>now||
            (boundary&&at<=boundary)||now-at>60000ULL*10000ULL||
            (facts_.kernelSleepEntry.sourceGeneration==epoch&&at<=facts_.kernelSleepEntry.eventFileTime))return false;
        if(facts_.kernelWake.eventFileTime>=at)return false;
        facts_.kernelWake={gen,epoch,boundary,at,id,!captureMatched,false,0};changedLocked();
        if(completedFactsKnown_&&completedFacts_.completedResume.eventFileTime==at)completedFacts_.kernelWake=facts_.kernelWake;
        retainCompletedLocked();return true;
    }
    void requestedLocked(std::uint64_t gen,std::uint64_t boundaryGeneration) noexcept {
        auto& wake=facts_.kernelWake;
        if(wake.eventFileTime&&wake.sourceGeneration==wake.generation&&
           (wake.generation==gen||(wake.generation!=(std::numeric_limits<std::uint64_t>::max)()&&wake.generation+1==gen&&boundaryGeneration==wake.generation))){
            wake.uiWakeRequested=true;wake.requestGeneration=gen;
            if(completedFactsKnown_&&completedFacts_.kernelWake.eventFileTime==wake.eventFileTime){completedFacts_.kernelWake.uiWakeRequested=true;completedFacts_.kernelWake.requestGeneration=gen;}
        }
    }
    void readyLocked(std::uint64_t gen,std::uint64_t epoch,std::uint64_t boundary) noexcept {
        if(gen&&epoch&&(facts_.readyCommit.generation!=gen||facts_.readyCommit.sourceGeneration!=epoch||facts_.readyCommit.boundaryFileTime!=boundary)){
            facts_.readyCommit={gen,epoch,boundary};changedLocked();retainCompletedLocked();
        }
    }
};
inline WakeFactsOwner nativeWakeFacts;
}
