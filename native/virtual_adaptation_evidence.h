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
    // Exact fixed-size membership index; at most half full, no allocation or
    // eviction. A saturated budget must not scan all 64 contexts every frame.
    std::array<std::uint8_t,128> index_{};
    std::atomic<unsigned> contextsUsed_{0};
    static std::size_t bucket(const FrameStamp& s) noexcept {
        std::uint64_t hash=14695981039346656037ull;
        for(const auto value:{s.epoch,s.power,s.sourceGeneration,s.topology}){
            hash^=value;hash*=1099511628211ull;}
        hash^=hash>>32;return static_cast<std::size_t>(hash)&127u;
    }
    std::atomic<unsigned long long> capacity_{0},busy_{0},unknown_{0},selected_{0};
    // First capacity rejection per outcome shape only. A bounded diagnostic
    // witness, not another selected context or a per-frame sample queue.
    unsigned capacityShapeMask_=0;
    std::array<FrameStamp,8> capacityFirst_{};
    std::uint64_t capacityHighestPower_=0;
    unsigned capacityHighestPowerMask_=0;
    std::array<FrameStamp,8> capacityHighestPowerFirst_{};
public:
    struct ContextEvidence {std::uint64_t epoch=0,power=0,source=0,topology=0;unsigned selectedOutcomeMask=0;};
    struct ExportSnapshot {
        bool available=false;unsigned knownContexts=0,capacityShapeMask=0;
        std::array<ContextEvidence,64> contexts{};
        std::array<FrameStamp,8> capacityFirst{};
        std::uint64_t capacityHighestPower=0;
        unsigned capacityHighestPowerMask=0;
        std::array<FrameStamp,8> capacityHighestPowerFirst{};
    };
    ExportSnapshot snapshotForExport() {
        ExportSnapshot result;
        std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);if(!lock)return result;
        result.available=true;result.knownContexts=contextsUsed_.load(std::memory_order_relaxed);
        result.capacityShapeMask=capacityShapeMask_;
        for(unsigned i=0;i<result.knownContexts;++i){const auto& c=contexts_[i];
            result.contexts[i]={c.epoch,c.power,c.source,c.topology,c.seen};}
        for(unsigned i=0;i<8;++i)if(capacityShapeMask_&(1u<<i))result.capacityFirst[i]=capacityFirst_[i];
        result.capacityHighestPower=capacityHighestPower_;
        result.capacityHighestPowerMask=capacityHighestPowerMask_;
        for(unsigned i=0;i<8;++i)if(capacityHighestPowerMask_&(1u<<i))result.capacityHighestPowerFirst[i]=capacityHighestPowerFirst_[i];
        return result;
    }
    bool select(const FrameStamp& s) noexcept {
        if(!s.epoch){++unknown_;return false;}
        std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);if(!lock){++busy_;return false;}
        Context* found=nullptr;auto position=bucket(s);
        for(;;){
            const auto slot=index_[position];
            if(!slot){
                const auto used=contextsUsed_.load(std::memory_order_relaxed);
                if(used==contexts_.size()){
                    ++capacity_;
                    const unsigned shape=(s.neutral?1u:0u)|(s.accepted?2u:0u)|(s.sourceAdmitted?4u:0u);
                    const unsigned bit=1u<<shape;
                    if(!(capacityShapeMask_&bit)){capacityFirst_[shape]=s;capacityShapeMask_|=bit;}
                    // Keep the newest observed power generation separate from
                    // the immutable process-first witnesses. Late older stamps
                    // must not replace it or fill holes in its outcome mask.
                    if(s.power>capacityHighestPower_){capacityHighestPower_=s.power;capacityHighestPowerMask_=0;}
                    if(s.power&&s.power==capacityHighestPower_&&!(capacityHighestPowerMask_&bit)){
                        capacityHighestPowerFirst_[shape]=s;capacityHighestPowerMask_|=bit;
                    }
                    return false;
                }
                found=&contexts_[used];*found={s.epoch,s.power,s.sourceGeneration,s.topology,0};
                index_[position]=static_cast<std::uint8_t>(used+1);
                contextsUsed_.store(used+1,std::memory_order_relaxed);break;
            }
            auto& c=contexts_[slot-1];
            if(c.epoch==s.epoch&&c.power==s.power&&c.source==s.sourceGeneration&&c.topology==s.topology){found=&c;break;}
            position=(position+1)&127u;
        }
        const unsigned bit=1u<<((s.neutral?1:0)|(s.accepted?2:0)|(s.sourceAdmitted?4:0));
        if(found->seen&bit)return false;found->seen|=bit;++selected_;return true;
    }
    unsigned knownContexts() const noexcept{return contextsUsed_.load(std::memory_order_relaxed);}
    unsigned long long selected() const noexcept{return selected_.load();}
    unsigned long long capacityDrops() const noexcept{return capacity_.load();}
    unsigned long long busyDrops() const noexcept{return busy_.load();}
    unsigned long long unknownDrops() const noexcept{return unknown_.load();}
};
}
