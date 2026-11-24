#pragma once
#include <array>
#include <cstdint>
#include <mutex>

namespace ymcc::sleeppower {
inline constexpr unsigned kernelCycleReadLimit=8;
struct KernelCycleSample {
    std::uint64_t at=0,generation=0;
    int reason=-1;
    std::uint64_t sessionUs=0;
    bool continuing=false;
    bool sessionEnded=false;
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
    Samples entries_{},completedExits_{},continuingExits_{};
    KernelCycleSample latestExit_{},latestUnclassifiedExit_{};
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
        std::lock_guard lock(mutex_);entries_={};completedExits_={};continuingExits_={};latestExit_={};latestUnclassifiedExit_={};
    }
    void record(int id,KernelCycleSample sample) noexcept {
        if((id!=506&&id!=507)||!sample.at||!sample.generation)return;
        std::lock_guard lock(mutex_);
        if(id==506)insert(entries_,sample);
        else {
            auto& latest=sample.reason>=0?latestExit_:latestUnclassifiedExit_;
            if(newer(sample,latest))latest=sample;
        }
    }
    void observeSleepExit(KernelCycleSample exit) noexcept {
        if(!exit.at||!exit.generation||exit.reason<0||!exit.sessionUs)return;
        std::lock_guard lock(mutex_);
        // Parsed fields belong to this callback, even after a newer exit was recorded.
        insert(exit.continuing ? continuingExits_ : completedExits_,exit);
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
