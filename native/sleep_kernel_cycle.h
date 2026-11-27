#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

namespace ymcc::sleeppower {
inline constexpr unsigned kernelCycleReadLimit=8;
// Diagnostic attribution only. A power-source transition can start a new segment.
inline constexpr bool longScreenOffEntryReason(int reason,bool continuingAcSegment) noexcept {
    return reason==5 || reason==12 || (!continuingAcSegment&&reason==28);
}
// An observed continuing segment, not a Windows sleep deadline or action gate.
inline constexpr int briefSleepSegmentReason=16777220;
inline constexpr char briefSleepScreenOffEvent[]="s0-long-screen-off-brief-sleep-continuing";
inline constexpr bool briefSleepScreenOffEntryReason(int reason) noexcept {
    return reason==1||reason==briefSleepSegmentReason;
}
inline constexpr bool briefSleepScreenOffCounters(std::uint64_t total,std::uint64_t off,std::uint64_t sleep) noexcept {
    if(off<180000000||off>total||!sleep||sleep>2000000)return false;
    const auto remainder=total-off;
    return (remainder>sleep?remainder-sleep:sleep-remainder)<=1000000;
}
inline constexpr bool briefSleepScreenOffTimes(std::uint64_t entry,std::uint64_t exit,std::uint64_t total) noexcept {
    if(!entry||entry>=exit)return false;
    const auto elapsed=(exit-entry)/10;
    return (elapsed>total?elapsed-total:total-elapsed)<=2000000;
}
struct KernelCycleIdentity {
    std::optional<std::uint64_t> boot{},scenarioV2{};
    std::optional<std::uint64_t> exitScenario{};
};
inline bool sameKnownKernelId(std::optional<std::uint64_t> a,std::optional<std::uint64_t> b) noexcept {
    return !a||!b||a==b;
}
struct KernelCycleSample {
    std::uint64_t at=0,generation=0;
    int reason=-1;
    std::uint64_t sessionUs=0;
    bool continuing=false;
    bool sessionEnded=false;
    std::uint64_t sleepUs=0;
    KernelCycleIdentity identity{};
};
inline bool enteredKernelCycle(const KernelCycleSample& entry,const KernelCycleSample& exit,
                               std::uint64_t earlyWindowMs) noexcept {
    if(!entry.at||exit.at<=entry.at||!exit.sessionUs)return false;
    const auto elapsedUs=(exit.at-entry.at)/10;
    if((exit.reason==7||exit.reason==8)&&elapsedUs<=earlyWindowMs*1000)return false;
    constexpr std::uint64_t maxSkewUs=2000000;
    return (exit.sessionUs>elapsedUs?exit.sessionUs-elapsedUs:elapsedUs-exit.sessionUs)<=maxSkewUs;
}
inline bool completedKernelCycle(const KernelCycleSample& entry,const KernelCycleSample& exit,
                                 std::uint64_t earlyWindowMs) noexcept {
    return !exit.continuing && enteredKernelCycle(entry,exit,earlyWindowMs);
}

enum class KernelCycleResult { Unknown, Completed, ManualWake, SystemWake, EnteredInProgress };
struct OriginalKernelCycle {
    std::uint64_t entry=0;
    KernelCycleResult result=KernelCycleResult::Unknown;
    bool veto() const noexcept {return result!=KernelCycleResult::Unknown;}
};

// Readback must include the observed 506 and cannot cross a newer entry.
struct KernelWakeRecord {
    std::uint64_t at=0;
    int id=0,reason=-1,target=-1;
    bool completed=false;
};
class KernelWakeReadback {
    std::uint64_t entry_,boundary_;
    bool matched_=false,ambiguous_=false;
    KernelWakeRecord latest_{};
public:
    KernelWakeReadback(std::uint64_t entry,std::uint64_t boundary) noexcept : entry_(entry),boundary_(boundary) {}
    bool observe(KernelWakeRecord event) noexcept {
        if(!event.at){ambiguous_=true;return false;}
        if(event.id==506){
            if(event.at>entry_||(event.at==entry_&&matched_))ambiguous_=true;
            if(event.at==entry_)matched_=true;
            return false;
        }
        if(event.at<=entry_||event.at<=boundary_)return false;
        if(event.at>latest_.at){latest_=event;return true;}
        if(event.at==latest_.at)ambiguous_=true;
        return false;
    }
    KernelWakeRecord result(std::uint64_t now) const noexcept {
        if(!matched_||ambiguous_||!latest_.at||latest_.at>now||now-latest_.at>60000ULL*10000)return {};
        if((latest_.id==107&&latest_.target==5)||(latest_.id==507&&latest_.completed))return latest_;
        return {};
    }
};

// Reverse record order stops at the first 506. Incomplete exits carry no
// completion, but cannot erase another exit from the same read.
class ReverseKernelCycle {
    std::array<KernelCycleSample,kernelCycleReadLimit> exits_{};
    unsigned count_=0;
public:
    void exit(KernelCycleSample sample) noexcept {
        if(count_<exits_.size())exits_[count_++]=sample;
    }
    OriginalKernelCycle original(KernelCycleSample entry,std::uint64_t observed,
                                 std::uint64_t closed,std::uint64_t earlyWindowMs) const noexcept {
        if(entry.reason!=1||!entry.at||entry.at>observed||entry.at<=closed||
           observed-entry.at>earlyWindowMs*10000)return {};
        OriginalKernelCycle out{entry.at};
        for(unsigned i=0;i<count_;++i){
            const auto& exit=exits_[i];
            if(exit.at<entry.at)continue;
            if(exit.reason==1)return {entry.at,KernelCycleResult::ManualWake};
            if(exit.reason==20)out.result=KernelCycleResult::SystemWake;
            else if((out.result==KernelCycleResult::Unknown||out.result==KernelCycleResult::EnteredInProgress)&&
                    completedKernelCycle(entry,exit,earlyWindowMs))
                out.result=KernelCycleResult::Completed;
            else if(out.result==KernelCycleResult::Unknown&&exit.continuing&&enteredKernelCycle(entry,exit,earlyWindowMs))
                out.result=KernelCycleResult::EnteredInProgress;
        }
        return out;
    }
};

// Callback identity is captured before rendering. Keep older valid exits
// available until their matching entry and parser result have both arrived.
class KernelCycleObservations {
    using Samples=std::array<KernelCycleSample,kernelCycleReadLimit>;
    mutable std::mutex mutex_;
    Samples entries_{},exits_{},completedExits_{},continuingExits_{};
    KernelCycleSample latestExit_{},latestUnclassifiedExit_{};
    std::uint64_t lastLongScreenOffReported_=0,lastBriefSleepScreenOffReported_=0,unknownBoundaryGeneration_=0;
    static bool newer(KernelCycleSample a,KernelCycleSample b) noexcept {
        return a.at>b.at||(a.at==b.at&&a.generation>b.generation);
    }
    static void insert(Samples& samples,KernelCycleSample value) noexcept {
        for(const auto& sample:samples)if(sample.at==value.at&&sample.generation==value.generation)return;
        for(unsigned i=0;i<samples.size();++i){
            if(!newer(value,samples[i]))continue;
            for(auto j=samples.size()-1;j>i;--j)samples[j]=samples[j-1];
            samples[i]=value;return;
        }
    }
public:
    struct Latest { KernelCycleSample entry,exit; };
    Latest latest() const noexcept {std::lock_guard lock(mutex_);return {entries_.front(),latestExit_};}
    void reset() noexcept {
        std::lock_guard lock(mutex_);entries_={};exits_={};completedExits_={};continuingExits_={};latestExit_={};latestUnclassifiedExit_={};
        lastLongScreenOffReported_=lastBriefSleepScreenOffReported_=unknownBoundaryGeneration_=0;
    }
    void record(int id,KernelCycleSample sample) noexcept {
        if((id!=506&&id!=507&&id!=107)||!sample.generation)return;
        std::lock_guard lock(mutex_);
        if(!sample.at){if(sample.generation>unknownBoundaryGeneration_)unknownBoundaryGeneration_=sample.generation;return;}
        if(id==506)insert(entries_,sample);
        else if(id==507) {
            insert(exits_,sample);
            auto& latest=sample.reason>=0?latestExit_:latestUnclassifiedExit_;
            if(newer(sample,latest))latest=sample;
        }
    }
    KernelCycleSample continuedUserEntry(std::uint64_t generation,std::uint64_t nativeSuspend,std::uint64_t latestEntry,
                                          std::uint64_t finalExit,int reason,std::uint64_t earlyWindowMs) const noexcept {
        if(generation<=1||!nativeSuspend||nativeSuspend>=finalExit||reason<0||reason==1||reason==20)return {};
        std::lock_guard lock(mutex_);
        const auto prior=generation-1;
        const auto& tail=entries_[0];
        const auto& origin=entries_[1];
        const auto& last=exits_[0];
        const auto& continuing=exits_[1];
        constexpr std::uint64_t maxReentryGap=100*10000; // Original Event Log timestamps.
        // Require the adjacent reason-55 exit/reentry recorded before this Native suspend.
        if(unknownBoundaryGeneration_>=prior||last.at!=finalExit||last.reason!=reason||last.generation!=generation||
           !last.sessionEnded||last.continuing||tail.reason!=55||tail.generation!=prior||
           tail.at!=latestEntry||tail.at>nativeSuspend||
           continuing.reason!=55||!continuing.continuing||continuing.generation!=prior||
           !continuing.at||continuing.at>=tail.at||tail.at-continuing.at>maxReentryGap||
           !origin.at||origin.at>=continuing.at||(origin.reason!=1&&origin.reason!=3)||origin.generation!=prior||
           exits_[2].at>=origin.at)return {};
        for(const auto& parsed:continuingExits_)
            if(parsed.at==continuing.at&&parsed.generation==prior&&parsed.reason==55&&
               enteredKernelCycle(origin,parsed,earlyWindowMs))return origin;
        return {};
    }
    void observeSleepExit(KernelCycleSample exit) noexcept {
        if(!exit.at||!exit.generation||exit.reason<0||!exit.sessionUs)return;
        std::lock_guard lock(mutex_);
        // Parsed fields belong to this callback, even after a newer exit was recorded.
        insert(exit.continuing ? continuingExits_ : completedExits_,exit);
    }
    bool briefContinuationsFrom(std::uint64_t origin,std::uint64_t generation,std::uint64_t latestEntry) const noexcept {
        return briefContinuationState(origin,generation,latestEntry).state==BriefContinuationState::Paired;
    }
    enum class BriefContinuationState { Rejected, Paired, AwaitingExit, AwaitingEntry };
    struct BriefContinuation {
        BriefContinuationState state=BriefContinuationState::Rejected;
        std::uint64_t tailExit=0;
    };
    BriefContinuation briefContinuationState(std::uint64_t origin,std::uint64_t generation,
                                              std::uint64_t latestEntry) const noexcept {
        std::lock_guard lock(mutex_);
        using State=BriefContinuationState;
        if(!origin||!generation||latestEntry<=origin||unknownBoundaryGeneration_>=generation||
           latestUnclassifiedExit_.at>=origin||entries_[0].at!=latestEntry)return {};
        // Only one unmatched end of the pair may follow an already paired prefix.
        const bool awaitingExit=entries_[0].reason==briefSleepSegmentReason&&entries_[0].generation==generation&&
            entries_[1].at>origin&&exits_[0].at<entries_[1].at;
        const bool awaitingEntry=exits_[0].at>entries_[0].at;
        // Old providers can omit IDs. A known boot must agree throughout the
        // chain, while a successor can start a different V2 segment.
        auto boot=entries_[0].identity.boot;
        const auto sameBoot=[&](const KernelCycleSample& sample) noexcept {
            if(!sameKnownKernelId(boot,sample.identity.boot))return false;
            if(!boot)boot=sample.identity.boot;
            return true;
        };
        const auto briefExit=[&](const KernelCycleSample& prior,const KernelCycleSample& exit) noexcept {
            if(prior.generation!=generation||prior.at<origin||exit.generation!=generation||
               exit.reason!=briefSleepSegmentReason||!exit.continuing||exit.sessionEnded||exit.at<=prior.at||
               !sameBoot(prior)||!sameBoot(exit)||
               !sameKnownKernelId(prior.identity.scenarioV2,exit.identity.scenarioV2))return false;
            for(const auto& parsed:continuingExits_)
                if(parsed.at==exit.at&&parsed.generation==generation&&parsed.reason==exit.reason&&
                   parsed.sleepUs&&parsed.sleepUs<=2000000&&briefSleepScreenOffTimes(prior.at,exit.at,parsed.sessionUs))return true;
            return false;
        };
        if(awaitingEntry&&!briefExit(entries_[0],exits_[0]))return {};
        for(unsigned i=awaitingExit?1:0,j=awaitingEntry?1:0;i+1<entries_.size()&&j+1<exits_.size();++i,++j){
            const auto& next=entries_[i];
            const auto& prior=entries_[i+1];
            const auto& exit=exits_[j];
            if(next.reason!=briefSleepSegmentReason||next.generation!=generation||exit.at>=next.at||
               next.at-exit.at>100*10000||!sameBoot(next)||
               !sameKnownKernelId(exit.identity.exitScenario,next.identity.scenarioV2)||!briefExit(prior,exit))return {};
            if(prior.at==origin)
                return prior.reason==1&&exits_[j+1].at<origin?
                    BriefContinuation{awaitingExit?State::AwaitingExit:awaitingEntry?State::AwaitingEntry:State::Paired,
                                      awaitingExit?0:exits_[0].at}:BriefContinuation{};
        }
        return {};
    }
    KernelCycleSample claimLongScreenOffExit(std::uint64_t at,std::uint64_t generation,int reason,
                                             std::uint64_t totalUs,std::uint64_t systemWake,
                                             bool continuingAcSegment=false) noexcept {
        std::lock_guard lock(mutex_);
        if(!generation||at<=lastLongScreenOffReported_||reason<0||reason==20||latestExit_.at!=at||
           latestExit_.generation!=generation||latestExit_.reason!=reason||!newer(latestExit_,latestUnclassifiedExit_))return {};
        auto entry=entries_.front();
        if(continuingAcSegment){
            if(reason!=28||!latestExit_.continuing||latestExit_.sessionEnded)return {};
            // A paired power-source successor may arrive first with the same timestamp.
            if(entry.at>=at){
                if(entry.reason!=28||entry.at-at>20000000||entry.generation!=generation)return {};
                entry=entries_[1];
            }
        }else if(!latestExit_.sessionEnded)return {};
        if(!longScreenOffEntryReason(entry.reason,continuingAcSegment)||!entry.at||entry.at>=at||
           entry.generation>generation||generation-entry.generation>1||systemWake>=entry.at||
           exits_[1].at>entry.at)return {};
        const auto elapsedUs=(at-entry.at)/10;
        // Accommodate Windows starting session accounting slightly after its 506.
        constexpr std::uint64_t maxSkewUs=5000000;
        if((elapsedUs>totalUs?elapsedUs-totalUs:totalUs-elapsedUs)>maxSkewUs)return {};
        lastLongScreenOffReported_=at;
        return entry;
    }
    KernelCycleSample claimBriefSleepScreenOffExit(std::uint64_t at,std::uint64_t generation,
                                                   std::uint64_t totalUs,std::uint64_t systemWake) noexcept {
        std::lock_guard lock(mutex_);
        if(!generation||unknownBoundaryGeneration_>=generation||at<=lastBriefSleepScreenOffReported_||
           latestExit_.at!=at||latestExit_.generation!=generation||latestExit_.reason!=briefSleepSegmentReason||
           !latestExit_.continuing||latestExit_.sessionEnded||!newer(latestExit_,latestUnclassifiedExit_))return {};
        auto entry=entries_.front();
        // Only the immediately adjacent, matching next segment may arrive first.
        if(entry.at>=at){
            if(entry.reason!=briefSleepSegmentReason||entry.generation!=generation||entry.at-at>100*10000||
               !sameKnownKernelId(entry.identity.boot,latestExit_.identity.boot)||
               !sameKnownKernelId(entry.identity.boot,entries_[1].identity.boot)||
               !sameKnownKernelId(latestExit_.identity.exitScenario,entry.identity.scenarioV2))return {};
            entry=entries_[1];
        }
        if(!briefSleepScreenOffEntryReason(entry.reason)||entry.generation!=generation||systemWake>=entry.at||
           exits_[1].at>entry.at||(exits_[1].at==entry.at&&exits_[1].reason!=briefSleepSegmentReason)||
           !sameKnownKernelId(entry.identity.boot,latestExit_.identity.boot)||
           !sameKnownKernelId(entry.identity.scenarioV2,latestExit_.identity.scenarioV2)||
           !briefSleepScreenOffTimes(entry.at,at,totalUs))return {};
        lastBriefSleepScreenOffReported_=at;
        return entry;
    }
    bool latestCompletedAfter(std::uint64_t requestAt, std::uint64_t exitAt, int reason,
                              std::uint64_t generation, std::uint64_t earlyWindowMs) const noexcept {
        if(!requestAt||!exitAt||!generation)return false;
        std::lock_guard lock(mutex_);
        const auto& entry=entries_.front();
        if(entry.generation!=generation||entry.at<requestAt||entry.at>=exitAt||entry.reason<0||
           latestExit_.at!=exitAt||latestExit_.generation!=generation||latestExit_.reason!=reason)return false;
        for(const auto& parsed:completedExits_){
            if(parsed.at==exitAt&&parsed.generation==generation&&parsed.reason==reason)
                return completedKernelCycle(entry,parsed,earlyWindowMs);
        }
        return false;
    }
    struct DisplayCycle { KernelCycleSample entry,exit; bool continuedOriginal=false; };
    DisplayCycle displayCycle(std::uint64_t original,std::uint64_t offGeneration,std::uint64_t generation,
                              std::uint64_t entryWindowMs,std::uint64_t boundarySlack100ns) const noexcept {
        std::lock_guard lock(mutex_);
        // An unclassified 507 can supersede evidence; it cannot confirm relight.
        DisplayCycle out{entries_.front(),newer(latestUnclassifiedExit_,latestExit_)?latestUnclassifiedExit_:latestExit_};
        if(!offGeneration||generation<offGeneration||generation-offGeneration!=2||
           !original||out.entry.at<=original||out.entry.generation<offGeneration||
           out.entry.generation>generation||!out.exit.sessionEnded||out.exit.generation<out.entry.generation||
           out.exit.at<=out.entry.at)return out;
        const auto successor=out.entry.at;
        for(const auto& exit:continuingExits_){
            if(exit.at>=successor||successor-exit.at>boundarySlack100ns||
               exit.generation<offGeneration||exit.generation>out.entry.generation)continue;
            for(const auto& entry:entries_){
                if(entry.at>exit.at)continue;
                if(entry.at==original&&(entry.reason==1||entry.reason==3)&&
                   entry.generation>=offGeneration&&entry.generation<=exit.generation&&
                   enteredKernelCycle(entry,exit,entryWindowMs)){
                    out.continuedOriginal=true;return out;
                }
                break;
            }
        }
        return out;
    }
    bool entered(std::uint64_t original,std::uint64_t generation,std::uint64_t earlyWindowMs) const noexcept {
        if(!original||!generation)return false;
        std::lock_guard lock(mutex_);
        for(const auto* samples:{&completedExits_,&continuingExits_})for(const auto& exit:*samples){
            if(!exit.sessionUs||exit.generation>generation||generation-exit.generation>1)continue;
            for(const auto& entry:entries_){
                if(entry.at>exit.at)continue;
                // Even an entry from another generation is a barrier. Never
                // search past it for a numerically convenient duration match.
                if(entry.at==original&&entry.reason>=0&&entry.generation<=exit.generation&&generation-entry.generation<=1&&
                   enteredKernelCycle(entry,exit,earlyWindowMs))return true;
                break;
            }
        }
        return false;
    }
};
}
