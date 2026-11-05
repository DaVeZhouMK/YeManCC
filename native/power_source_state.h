#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mutex>
#include <cstdint>
#include <algorithm>
#include <atomic>

// One owner per YMCC process. Only event/startup/wake/monitor producers sample
// Windows. Consumers read the immutable-by-value receipt without OS queries.
namespace ymcc::powerSource {
struct Snapshot {
    SYSTEM_POWER_STATUS status{255,255,255,255,0xFFFFFFFFu,0xFFFFFFFFu};
    bool readable=false,known=false,batteryKnown=false,hasBattery=false;
    int ac=-1;
    std::uint64_t revision=0,sampleSequence=0;
    ULONGLONG observedAt=0;
};
using Sampler=BOOL (WINAPI*)(LPSYSTEM_POWER_STATUS);
using Clock=ULONGLONG (WINAPI*)();
using Listener=void (*)(const Snapshot&) noexcept;
using ExternalReader=Snapshot (*)() noexcept;
class Store {
    std::mutex mutex_;
    Snapshot state_;
    Listener listener_=nullptr,observer_=nullptr;
    ULONGLONG attemptedAt_=0;
    bool attempted_=false;
    int monitorAc_=-1;ULONGLONG monitorUntil_=0,monitorObserved_=0,activeUntil_=0;bool activeSampling_=false;
    ULONGLONG boundaryAt_=0;std::uint64_t monitorBoundary_=0;
public:
    Snapshot snapshot(){std::lock_guard lock(mutex_);return state_;}
    void listen(Listener listener){std::lock_guard lock(mutex_);listener_=listener;}
    // Every fresh receipt reaches the cross-process publisher; UI listeners only
    // observe semantic source/capability changes. Both run outside the lock.
    void observe(Listener observer){std::lock_guard lock(mutex_);observer_=observer;}
    void discardMonitor(){std::lock_guard lock(mutex_);++monitorBoundary_;monitorAc_=-1;monitorUntil_=monitorObserved_=0;}
    std::uint64_t monitorBoundary(){std::lock_guard lock(mutex_);return monitorBoundary_;}
    void beginActiveWindow(ULONGLONG now){std::lock_guard lock(mutex_);if(now<boundaryAt_)return;++monitorBoundary_;boundaryAt_=now;monitorAc_=-1;monitorUntil_=monitorObserved_=0;activeSampling_=true;activeUntil_=now+30000;}
    bool activeSampling(){std::lock_guard lock(mutex_);return activeSampling_;}
    Snapshot sample(Sampler sampler,ULONGLONG now,bool event=false,ULONGLONG maxAge=250,Clock clock=nullptr) {
        Snapshot next;Listener notify=nullptr,publish=nullptr;
        {
            std::lock_guard lock(mutex_);
            // Production reads the clock after acquiring the lock, so a
            // descheduled producer cannot spend an old tick's sampling budget.
            if(clock)now=clock();
            // A caller may have captured its tick before waiting for this lock.
            // Never let that late caller bypass the cache or move time backwards.
            if(now<state_.observedAt||(attempted_&&now<attemptedAt_))return state_;
            // All callers, including foreground/status events, share the current
            // 2s active-probe ceiling. Monitor display reads cannot raise it.
            if(activeSampling_&&attempted_&&now>=attemptedAt_&&now-attemptedAt_<2000)return state_;
            // A confirmed desktop sleeps between actual OS topology/power events.
            if(!event&&state_.batteryKnown&&!state_.hasBattery)return state_;
            if(!event&&attempted_&&now>=attemptedAt_&&now-attemptedAt_<maxAge)return state_;
            attempted_=true;attemptedAt_=now;
            SYSTEM_POWER_STATUS status{};
            next=state_;
            next.readable=sampler&&sampler(&status)!=FALSE;
            next.observedAt=now;
            if(next.readable){
                next.status=status;
                // Explicit DC is usable even on G3E firmware with no percentage,
                // rate or BatteryFlag reading. Missing telemetry is not absence.
                if(status.ACLineStatus==0){next.batteryKnown=next.hasBattery=true;}
                else if(status.BatteryFlag!=255){next.batteryKnown=true;next.hasBattery=(status.BatteryFlag&128)==0;}
                // Missing flags do not erase a previously observed physical
                // battery. Only an explicit absence reading can retire it.
                next.ac=status.ACLineStatus<=1?int(status.ACLineStatus):next.batteryKnown&&!next.hasBattery?1:-1;
                next.known=next.ac>=0;
            }else{next.known=false;next.ac=-1;}
            // The independent battery driver's complete online/discharge proof
            // outranks a lagging aggregate status until the next real event or
            // the next monitor observation. A negative estimated wattage cannot
            // enter this path.
            if(!activeSampling_&&monitorAc_>=0&&now<monitorUntil_){next.batteryKnown=next.hasBattery=true;next.known=true;next.ac=monitorAc_;next.status.ACLineStatus=static_cast<BYTE>(next.ac);}
            if(next.known!=state_.known||next.ac!=state_.ac||next.batteryKnown!=state_.batteryKnown||
                next.hasBattery!=state_.hasBattery){++next.revision;notify=listener_;}
            ++next.sampleSequence;publish=observer_;state_=next;
        }
        if(publish)publish(next);
        if(notify)notify(next); // Never run consumer code under the sample lock.
        return next;
    }
    Snapshot sourceEvent(DWORD condition,ULONGLONG now){
        // PoAc=0, PoDc=1, PoHot=2. Unknown payloads are not DC.
        if(condition>2)return snapshot();
        Snapshot next;Listener notify=nullptr,publish=nullptr;
        {
            std::lock_guard lock(mutex_);
            if(now<state_.observedAt)return state_;
            const int ac=condition==0?1:0;
            if(state_.known&&state_.ac==ac&&(ac!=0||(state_.batteryKnown&&state_.hasBattery)))return state_;
            ++monitorBoundary_;boundaryAt_=now;monitorAc_=-1;monitorUntil_=monitorObserved_=0;next=state_;next.known=true;
            next.ac=ac;if(next.ac==0)next.batteryKnown=next.hasBattery=true;
            next.status.ACLineStatus=static_cast<BYTE>(next.ac);next.observedAt=now;
            if(!state_.known||next.ac!=state_.ac){++next.revision;notify=listener_;}
            ++next.sampleSequence;publish=observer_;state_=next;
        }
        if(publish)publish(next);
        if(notify)notify(next);return next;
    }
    Snapshot monitorSample(Sampler sampler,ULONGLONG now,ULONGLONG maxAge=250,Clock clock=nullptr){
        std::uint64_t boundary=0;
        {std::lock_guard lock(mutex_);if(clock)now=clock();if(activeSampling_&&now<activeUntil_)return state_;boundary=monitorBoundary_;}
        // Promote only a still-fresh, complete cached monitor proof at handoff,
        // before a lagging aggregate status can momentarily flip the source.
        (void)finishActiveWindow(now);
        const auto receipt=sample(sampler,now,false,maxAge,clock);
        // A running monitor with an explicit AC/DC status can take ownership
        // even when battery percent/watts are missing (MSI G3E).
        if(receipt.readable&&receipt.known){std::lock_guard lock(mutex_);if(clock)now=clock();if(boundary==monitorBoundary_&&receipt.sampleSequence==state_.sampleSequence&&now>=activeUntil_)activeSampling_=false;}
        return receipt;
    }
    Snapshot monitorEvent(int ac,ULONGLONG now,std::uint64_t boundary=~std::uint64_t{0}){
        Snapshot next;Listener notify=nullptr,publish=nullptr;
        {
            std::lock_guard lock(mutex_);
            if(now<boundaryAt_||now<monitorObserved_||(boundary!=~std::uint64_t{0}&&boundary!=monitorBoundary_))return state_;
            if(ac!=0&&ac!=1){monitorAc_=-1;monitorUntil_=monitorObserved_=0;return state_;}
            monitorAc_=ac;monitorObserved_=now;monitorUntil_=now+45000;
            if(activeSampling_&&now<activeUntil_)return state_;
            activeSampling_=false;next=state_;
            next.known=true;next.ac=ac;next.batteryKnown=next.hasBattery=true;
            next.status.ACLineStatus=static_cast<BYTE>(ac);next.observedAt=std::max(now,next.observedAt);
            if(!state_.known||state_.ac!=ac||!state_.batteryKnown||!state_.hasBattery){++next.revision;notify=listener_;}
            ++next.sampleSequence;publish=observer_;state_=next;
        }
        if(publish)publish(next);
        if(notify)notify(next);return next;
    }
    bool finishActiveWindow(ULONGLONG now){
        Snapshot next;Listener notify=nullptr,publish=nullptr;
        {
            std::lock_guard lock(mutex_);
            if(!activeSampling_)return true;
            if(now<activeUntil_)return false;
            if(state_.batteryKnown&&!state_.hasBattery){activeSampling_=false;return true;}
            if(monitorAc_<0||now>=monitorUntil_)return false;
            // Promote under the same lock. An intervening plug/wake boundary
            // must never be overwritten by a saved pre-boundary observation.
            activeSampling_=false;next=state_;next.known=true;next.ac=monitorAc_;
            next.batteryKnown=next.hasBattery=true;next.status.ACLineStatus=static_cast<BYTE>(next.ac);
            next.observedAt=std::max(next.observedAt,monitorObserved_);
            if(!state_.known||state_.ac!=next.ac||!state_.batteryKnown||!state_.hasBattery){++next.revision;notify=listener_;}
            ++next.sampleSequence;publish=observer_;state_=next;
        }
        if(publish)publish(next);
        if(notify)notify(next);return true;
    }

};
inline Store& owner(){static Store store;return store;}
inline std::atomic<ExternalReader>& externalReader(){static std::atomic<ExternalReader> reader{nullptr};return reader;}
inline void setExternalReader(ExternalReader reader) noexcept {externalReader().store(reader,std::memory_order_release);}
inline Snapshot snapshot(){const auto reader=externalReader().load(std::memory_order_acquire);return reader?reader():owner().snapshot();}
inline Snapshot refresh(bool event=false,ULONGLONG maxAge=250){const auto reader=externalReader().load(std::memory_order_acquire);return reader?reader():owner().sample(GetSystemPowerStatus,GetTickCount64(),event,maxAge,GetTickCount64);}
inline Snapshot refreshMonitor(){const auto reader=externalReader().load(std::memory_order_acquire);return reader?reader():owner().monitorSample(GetSystemPowerStatus,GetTickCount64(),250,GetTickCount64);}
inline bool read(SYSTEM_POWER_STATUS& status){const auto state=snapshot();status=state.status;status.ACLineStatus=state.known?static_cast<BYTE>(state.ac):255;return state.readable;}
inline int ac(){const auto state=snapshot();return state.known?state.ac:-1;}
inline bool monitorNeeded(const Snapshot& state,bool ready){return ready&&!(state.batteryKnown&&!state.hasBattery);}
inline ULONGLONG monitorDelay(ULONGLONG sampledAt,ULONGLONG now){
    return now>=sampledAt&&now-sampledAt<30000?30000-(now-sampledAt):0;
}
// Startup, committed wake and real source edges each reserve30s at2s cadence.
// Afterward a live monitor takes over; without usable monitor data, probes continue.
class Heartbeat {
    ULONGLONG deadline_=0;std::uint64_t generation_=0;bool armed_=false,seen_=false;
public:
    bool arm(std::uint64_t generation,ULONGLONG now,const Snapshot& state){
        if(seen_&&generation_==generation)return armed_;
        seen_=true;
        generation_=generation;deadline_=now+30000;armed_=!(state.batteryKnown&&!state.hasBattery);return armed_;
    }
    bool due(ULONGLONG now,const Snapshot& state,bool monitorReady=false){
        if(!armed_||(now>=deadline_&&monitorReady)||(state.batteryKnown&&!state.hasBattery)){armed_=false;return false;}return true;
    }
    void cancel(){armed_=false;}
};
} // namespace ymcc::powerSource
