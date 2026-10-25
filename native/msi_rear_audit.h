#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <mutex>
namespace ymcc::msi {
// One producer and low-rate diagnostics share memory only. BUS updates are
// try-lock and can be dropped explicitly; no logging/I/O/timer in this class.
struct RearAuditWindow {
    bool enabled=false,mixedGeneration=false;
    uint64_t firstGeneration=0,lastGeneration=0,samples=0,attempts=0,accepted=0,rejected=0,dropped=0;
    std::array<uint64_t,2> physicalDown{},logicalDown{},press{},release{},attemptRear{},acceptedRear{};
    std::array<uint64_t,3> extraDown{},extraPress{},extraRelease{};
    std::array<uint64_t,16> heldRearStage{};
};
class RearAudit {
    std::atomic<bool> enabled_{false};std::atomic<uint64_t> dropped_{0};std::mutex mx_;
    RearAuditWindow window_;bool have_=false;uint64_t generation_=0;unsigned back_=0,extra_=0;
public:
    void enable(bool on){if(enabled_.load(std::memory_order_acquire)==on)return;std::lock_guard<std::mutex> l(mx_);window_={};have_=false;generation_=0;back_=extra_=0;dropped_=0;enabled_.store(on,std::memory_order_release);}
    void physical(uint64_t generation,unsigned back,unsigned logical,unsigned extra){
        if(!enabled_.load(std::memory_order_acquire))return;std::unique_lock<std::mutex> l(mx_,std::try_to_lock);if(!l.owns_lock()){++dropped_;return;}
        if(!enabled_.load(std::memory_order_relaxed)||generation==0)return;
        if(window_.samples==0)window_.firstGeneration=generation;window_.lastGeneration=generation;++window_.samples;
        if(window_.firstGeneration!=generation)window_.mixedGeneration=true;
        const bool edge=have_&&generation_==generation;
        for(unsigned i=0;i<2;i++){const unsigned bit=1u<<i;window_.physicalDown[i]+=(back&bit)!=0;window_.logicalDown[i]+=(logical&bit)!=0;if(edge){window_.press[i]+=!(back_&bit)&&(back&bit);window_.release[i]+=(back_&bit)&&!(back&bit);}}
        for(unsigned i=0;i<3;i++){const unsigned bit=1u<<i;window_.extraDown[i]+=(extra&bit)!=0;if(edge){window_.extraPress[i]+=!(extra_&bit)&&(extra&bit);window_.extraRelease[i]+=(extra_&bit)&&!(extra&bit);}}
        have_=true;generation_=generation;back_=back;extra_=extra;
    }
    void stage(unsigned stage,unsigned held){if(!enabled_.load(std::memory_order_acquire)||!held||stage>=16)return;std::unique_lock<std::mutex> l(mx_,std::try_to_lock);if(!l.owns_lock()){++dropped_;return;}if(enabled_.load(std::memory_order_relaxed))++window_.heldRearStage[stage];}
    void submit(unsigned rear,bool accepted){if(!enabled_.load(std::memory_order_acquire))return;std::unique_lock<std::mutex> l(mx_,std::try_to_lock);if(!l.owns_lock()){++dropped_;return;}if(!enabled_.load(std::memory_order_relaxed))return;++window_.attempts;window_.accepted+=accepted;window_.rejected+=!accepted;for(unsigned i=0;i<2;i++){window_.attemptRear[i]+=(rear&(1u<<i))!=0;window_.acceptedRear[i]+=accepted&&(rear&(1u<<i));}}
    RearAuditWindow take(){std::lock_guard<std::mutex> l(mx_);auto w=window_;w.enabled=enabled_.load(std::memory_order_relaxed);w.dropped=dropped_.exchange(0);window_={};return w;}
};
}
