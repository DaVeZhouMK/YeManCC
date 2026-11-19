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
};
inline bool completedKernelCycle(const KernelCycleSample& entry,const KernelCycleSample& exit,
                                 std::uint64_t earlyWindowMs) noexcept {
    if(!entry.at||exit.at<=entry.at||!exit.sessionUs)return false;
    const auto elapsedUs=(exit.at-entry.at)/10;
    if((exit.reason==7||exit.reason==8)&&elapsedUs<=earlyWindowMs*1000)return false;
    constexpr std::uint64_t maxSkewUs=2000000;
    return (exit.sessionUs>elapsedUs?exit.sessionUs-elapsedUs:elapsedUs-exit.sessionUs)<=maxSkewUs;
}

enum class KernelCycleResult { Unknown, Completed, ManualWake, SystemWake };
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
            else if(out.result==KernelCycleResult::Unknown&&completedKernelCycle(entry,exit,earlyWindowMs))
                out.result=KernelCycleResult::Completed;
        }
        return out;
    }
};

// Callback identity is captured before rendering. Keep older valid exits
// available until their matching entry and parser result have both arrived.
class KernelCycleObservations {
    using Samples=std::array<KernelCycleSample,kernelCycleReadLimit>;
    mutable std::mutex mutex_;
    Samples entries_{},completedExits_{};
    KernelCycleSample latestExit_{};
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
    void reset() noexcept {std::lock_guard lock(mutex_);entries_={};completedExits_={};latestExit_={};}
    void record(int id,KernelCycleSample sample) noexcept {
        if((id!=506&&id!=507)||!sample.at||!sample.generation)return;
        std::lock_guard lock(mutex_);
        if(id==506)insert(entries_,sample);
        else if(sample.reason>=0&&newer(sample,latestExit_))latestExit_=sample;
    }
    void completeExit(KernelCycleSample exit) noexcept {
        if(!exit.at||!exit.generation||exit.reason<0||!exit.sessionUs)return;
        std::lock_guard lock(mutex_);
        // Parsed fields belong to this callback, even after a newer exit was recorded.
        insert(completedExits_,exit);
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
    bool completed(std::uint64_t original,std::uint64_t generation,std::uint64_t earlyWindowMs) const noexcept {
        if(!original||!generation)return false;
        std::lock_guard lock(mutex_);
        for(const auto& exit:completedExits_){
            if(!exit.sessionUs||exit.generation>generation||generation-exit.generation>1)continue;
            for(const auto& entry:entries_){
                if(entry.at>exit.at)continue;
                // Even an entry from another generation is a barrier. Never
                // search past it for a numerically convenient duration match.
                if(entry.at==original&&entry.reason>=0&&entry.generation<=exit.generation&&generation-entry.generation<=1&&
                   completedKernelCycle(entry,exit,earlyWindowMs))return true;
                break;
            }
        }
        return false;
    }
};
}
