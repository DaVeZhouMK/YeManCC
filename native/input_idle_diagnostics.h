#pragma once
// Read-only diagnostics. Thresholds below classify observations, NEVER filter
// or alter the controller report. Sequence/packet/time changes are not input.
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
namespace ymcc::idle {
struct Pad {
    uint32_t buttons=0,back=0,screen=0;
    std::array<int,4> sticks{};
    std::array<unsigned,2> triggers{};
    bool operator==(const Pad&) const=default;
};
inline bool rawActive(const Pad& p) {
    if(p.buttons||p.back||p.screen)return true;
    for(auto v:p.sticks)if(v)return true;
    for(auto v:p.triggers)if(v)return true;
    return false;
}
inline bool meaningful(const Pad& p) {
    if(p.buttons||p.back||p.screen)return true;
    for(auto v:p.sticks)if(std::abs(v)>2048)return true;
    for(auto v:p.triggers)if(v>30)return true;
    return false;
}
struct Channel {
    uint64_t samples=0,changes=0,rawActiveFrames=0,meaningfulFrames=0,smallAnalogOnlyFrames=0;
    uint64_t lastMeaningfulTick=0,lastSampleTick=0;
    Pad last{}; bool haveLast=false;
    void observe(const Pad& p,uint64_t now) {
        ++samples;
        if(haveLast&&p!=last)++changes;
        if(rawActive(p))++rawActiveFrames;
        if(meaningful(p)){++meaningfulFrames;lastMeaningfulTick=now;}
        else if(rawActive(p))++smallAnalogOnlyFrames;
        last=p;haveLast=true;lastSampleTick=now;
    }
    void clearWindow() {
        samples=changes=rawActiveFrames=meaningfulFrames=smallAnalogOnlyFrames=0;
    }
};
struct Window {
    Channel physical,attempted,accepted;
    uint64_t sourceAbsent=0,rejected=0,imuFrames=0,imuChanges=0,nonFiniteImu=0;
    uint64_t startTick=0,endTick=0,powerGeneration=0,contextTransitions=0;
    bool contextKnown=false,latestPhysicalLive=false;
    uint64_t latestPhysicalObservationTick=0;
    bool virtualEnabled=false;
    unsigned connectedMask=0;
    std::string source;
};
struct PhysicalSnapshot {
    uint64_t observationTick=0,sampleTick=0,powerGeneration=0,contextTransitions=0;
    uint32_t buttons=0,back=0,screen=0;
    unsigned connectedMask=0;
    bool enabled=false,haveLastSample=false,latestPhysicalLive=false,contextKnown=false,virtualEnabled=false;
};
namespace detail { struct AccumulatorTestAccess; }
class Accumulator {
    friend struct detail::AccumulatorTestAccess; // Defined only by standalone memory tests.

    std::atomic<bool> enabled_{false};
    std::mutex mx_;
    Window window_{};
    std::array<double,6> lastImu_{};bool haveImu_=false;
public:
    bool enabled()const{return enabled_.load(std::memory_order_acquire);}
    void enable(bool on,uint64_t now) {
        std::lock_guard<std::mutex> lock(mx_);
        if(enabled()==on)return;
        window_={};window_.startTick=now;haveImu_=false;
        enabled_.store(on,std::memory_order_release);
    }
    void physical(bool live,const Pad& p,uint64_t now,bool wanted,unsigned mask,
            uint64_t powerGeneration,const std::string& source) {
        if(!enabled())return;
        std::lock_guard<std::mutex> lock(mx_);if(!enabled())return;
        if(window_.contextKnown&&(window_.virtualEnabled!=wanted||window_.connectedMask!=mask||window_.powerGeneration!=powerGeneration||window_.source!=source))++window_.contextTransitions;
        window_.contextKnown=true;window_.virtualEnabled=wanted;window_.connectedMask=mask;window_.powerGeneration=powerGeneration;
        if(window_.source!=source){window_.source=source;window_.physical.haveLast=false;}
        window_.latestPhysicalLive=live;window_.latestPhysicalObservationTick=now;
        if(live)window_.physical.observe(p,now);else ++window_.sourceAbsent;
    }
    void submit(const Pad& p,bool accepted,bool imu,const std::array<double,6>& values,uint64_t now) {
        if(!enabled())return;
        std::lock_guard<std::mutex> lock(mx_);if(!enabled())return;
        window_.attempted.observe(p,now);
        if(accepted)window_.accepted.observe(p,now);else ++window_.rejected;
        if(imu){
            ++window_.imuFrames;
            bool finite=true;for(auto v:values)finite&=std::isfinite(v);
            if(!finite)++window_.nonFiniteImu;
            else {if(haveImu_&&lastImu_!=values)++window_.imuChanges;lastImu_=values;haveImu_=true;}
        }
    }
    // Diagnostic peek only: one try-lock, fixed scalar copy, no drain/allocation.
    // Failure leaves output unchanged. A prior sample can outlive source absence.
    bool peekPhysical(PhysicalSnapshot& out) noexcept {
        if(!enabled())return false;
        std::unique_lock<std::mutex> lock(mx_,std::try_to_lock);
        if(!lock.owns_lock()||!enabled())return false;
        PhysicalSnapshot copy;
        copy.enabled=true;copy.contextKnown=window_.contextKnown;
        copy.observationTick=window_.latestPhysicalObservationTick;
        copy.sampleTick=window_.physical.lastSampleTick;
        copy.haveLastSample=window_.physical.haveLast;
        copy.latestPhysicalLive=window_.latestPhysicalLive;
        copy.powerGeneration=window_.powerGeneration;copy.contextTransitions=window_.contextTransitions;
        copy.virtualEnabled=window_.virtualEnabled;copy.connectedMask=window_.connectedMask;
        copy.buttons=window_.physical.last.buttons;copy.back=window_.physical.last.back;copy.screen=window_.physical.last.screen;
        out=copy;return true;
    }
    Window take(uint64_t now) {
        std::lock_guard<std::mutex> lock(mx_);
        window_.endTick=now;auto result=window_;
        window_.physical.clearWindow();window_.attempted.clearWindow();window_.accepted.clearWindow();
        window_.sourceAbsent=window_.rejected=window_.imuFrames=window_.imuChanges=window_.nonFiniteImu=window_.contextTransitions=0;
        window_.startTick=now;
        return result;
    }
};
inline bool timeoutElapsed(bool idleValid,uint64_t idleMs,bool timeoutValid,uint32_t seconds) {
    return idleValid&&timeoutValid&&seconds!=0&&idleMs>=uint64_t(seconds)*1000;
}
} // namespace ymcc::idle
