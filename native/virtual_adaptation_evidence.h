#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <algorithm>
namespace ymcc::virtualAdaptation {
struct FrameStamp {
    std::uint64_t epoch=0,power=0,sourceGeneration=0,topology=0,commandSequence=0,wireSample=0,wireTick=0;
    bool neutral=false,accepted=false,sourceAdmitted=false,fastReceipt=false,bindingTruncated=false;
    std::array<char,128> runId{},hostId{};
    void captureBinding(const std::string& run,const std::string& host) noexcept {
        runId.fill(0);hostId.fill(0);
        bindingTruncated=run.size()>=runId.size()||host.size()>=hostId.size();
        if(!bindingTruncated){std::copy(run.begin(),run.end(),runId.begin());std::copy(host.begin(),host.end(),hostId.begin());}
    }
};
// Existing submit caller only: fixed storage, no clocks/heap/I/O and no source decisions.
class FrameSelector {
    struct Context {std::uint64_t epoch=0,power=0,source=0,topology=0;unsigned seen=0;};
    std::array<Context,64> contexts_{};std::mutex mx_;
    std::atomic<unsigned long long> capacity_{0},busy_{0},unknown_{0},selected_{0};
public:
    bool select(const FrameStamp& s) noexcept {
        if(!s.epoch){++unknown_;return false;}
        std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);if(!lock){++busy_;return false;}
        Context* free=nullptr;Context* found=nullptr;
        for(auto& c:contexts_){if(c.epoch==s.epoch&&c.power==s.power&&c.source==s.sourceGeneration&&c.topology==s.topology){found=&c;break;}if(!c.epoch&&!free)free=&c;}
        if(!found){if(!free){++capacity_;return false;}*free={s.epoch,s.power,s.sourceGeneration,s.topology,0};found=free;}
        const unsigned bit=1u<<((s.neutral?1:0)|(s.accepted?2:0)|(s.sourceAdmitted?4:0));
        if(found->seen&bit)return false;found->seen|=bit;++selected_;return true;
    }
    unsigned long long selected() const noexcept{return selected_.load();}
    unsigned long long capacityDrops() const noexcept{return capacity_.load();}
    unsigned long long busyDrops() const noexcept{return busy_.load();}
    unsigned long long unknownDrops() const noexcept{return unknown_.load();}
};
}
