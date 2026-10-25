#pragma once
#include "sleep_residency_evidence.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

// A bounded observation phase retains its original human intent. No OS calls or private budget.
namespace ymcc::abnormalsleep {
using U64=std::uint64_t;
template<class T> inline void publishConfirmedWake(std::atomic<T>& marker,T at) {
    auto prior=marker.load(std::memory_order_acquire);
    while(prior<at&&!marker.compare_exchange_weak(prior,at,std::memory_order_acq_rel)){}
}
enum class Quality { Unknown, Bad, Healthy };
enum class Action { None, Sleep, Hibernate, PowerOff };
struct Observation {
    Quality quality=Quality::Unknown;
    U64 begin=0,end=0; // Windows event FILETIME, 100ns, never collection time.
    unsigned eventId=0;
};
inline Observation decode(unsigned id,const std::string& xml,U64 at) {
    Observation result;result.eventId=id;result.end=at;
    const auto data=sleep_residency::readEventData(xml);
    const auto n=[&](const char* key)->std::optional<U64>{
        const auto it=data.values.find(key);
        if(it==data.values.end()||data.duplicates.count(key))return {};
        const auto value=sleep_residency::unsignedValue(it->second);
        return value&&*value!=std::numeric_limits<U64>::max()?value:std::nullopt;
    };
    if(!at)return result;
    if(id==161) {
        const auto period=n("DurationThisPeriodMs"),bad=n("ResiliencyPhaseNonActivatedNoDripsMs");
        const auto cpu=n("NonActivatedCpuTimeMs");
        // Do not convert cumulative/contradictory counters into a live period.
        if(!period||!*period||*period>30000||!bad||*bad>*period||!cpu||*cpu>*period||at<*period*10000)return result;
        result.begin=at-*period*10000;
        if(*bad==*period)result.quality=Quality::Bad;
    } else if(id==507) {
        const auto sw=n("DripsResidencyInUs"),hw=n("HwDripsResidencyInUs"),duration=n("DurationInUs");
        const auto flag=data.values.find("SleepEntered");
        if(duration&&*duration>0&&*duration<=at/10)result.begin=at-*duration*10;
        if(result.begin&&sw&&hw&&duration&&*sw>0&&*hw>0&&*sw<=*duration&&*hw<=*duration&&
            flag!=data.values.end()&&!data.duplicates.count("SleepEntered")&&
            (flag->second=="true"||flag->second=="1"))result.quality=Quality::Healthy;
        // Completed accounting may cancel escalation; it never authorizes a retry.
    } else if(id==522) {
        const auto valid=data.values.find("HwDripsTotalTimeValid");
        const auto sw=n("DripsTotalTimeThisPeriodUs"),hw=n("HwDripsTotalTimeThisPeriodUs");
        if(valid!=data.values.end()&&!data.duplicates.count(valid->first)&&
            (valid->second=="true"||valid->second=="1")&&sw&&hw&&*sw>0&&*hw>0)
            result.quality=Quality::Healthy;
        // This schema has no total period length. Zero HW never authorizes action.
    }
    return result;
}
struct Decision {Action action=Action::None;unsigned stage=4;const char* reason="inactive";};
struct Episode {
    static constexpr std::array<U64,4> deadlines{30000,230000,330000,360000};
    static constexpr U64 dispatchWindow=10000,freshMs=5000,escalationBadMs=30000;
    U64 origin=0,originalHumanIntent=0,startTick=0,generation=0,badBegin=0,badEnd=0,lastObservation=0;
    unsigned next=4;bool hibernateRejected=false,hibernateAccepted=false;
    bool active()const{return origin!=0&&next<4;}
    void cancel(){*this=Episode{};}
    bool arm(U64 eventTime,U64 tick,U64 fileTime,U64 gen,U64 humanIntent=0) {
        cancel();if(!eventTime||!gen||humanIntent>eventTime||fileTime<eventTime||(fileTime-eventTime)/10000>10000)return false;
        const auto age=(fileTime-eventTime)/10000;if(tick<age)return false;
        origin=eventTime;originalHumanIntent=humanIntent?humanIntent:eventTime;startTick=tick-age;generation=gen;next=0;return true;
    }
    static bool healthyForPhase(const Observation& o,U64 phase,U64 now) {
        return phase&&o.quality==Quality::Healthy&&o.end>=phase&&o.end<=now&&
            (o.eventId!=507||(o.begin>=phase&&o.begin<o.end));
    }
    void observe(const Observation& o,U64 now) {
        if(!active()||!o.end||o.end<origin||now<o.end)return;
        // A completed previous session cannot cancel a new owned retry.
        if(healthyForPhase(o,origin,now)){cancel();return;}
        if(o.quality==Quality::Healthy)return;
        if(o.end<lastObservation||now-o.end>freshMs*10000)return;
        // Repeated records cannot add duration or erase a newer result.
        if(o.end==lastObservation){if(o.quality==Quality::Healthy)cancel();else if(o.quality!=Quality::Bad)badBegin=badEnd=0;return;}
        lastObservation=o.end;
        if(o.quality==Quality::Healthy){cancel();return;}
        if(o.quality!=Quality::Bad||!o.begin||o.begin<origin||o.begin>=o.end){badBegin=badEnd=0;return;}
        if(!badEnd||o.begin>badEnd||o.begin<badBegin){badBegin=o.begin;}
        badEnd=o.end;
    }
    void invalidate(){badBegin=badEnd=lastObservation=0;}
    Decision poll(U64 tick,U64 fileTime,bool permitted) {
        if(!active())return {};
        if(!permitted||tick<startTick||fileTime<origin){cancel();return {Action::None,4,"canceled"};}
        const auto elapsed=tick-startTick,wall=(fileTime-origin)/10000;
        if((elapsed>wall?elapsed-wall:wall-elapsed)>5000){cancel();return {Action::None,4,"clock-drift"};}
        // Never replay multiple expired actions when a sleeping machine resumes.
        while(next<4&&elapsed>deadlines[next]+dispatchWindow)++next;
        if(next>=4){cancel();return {Action::None,4,"expired"};}
        if(elapsed<deadlines[next])return {Action::None,next,"waiting"};
        const bool fresh=badEnd&&fileTime>=badEnd&&fileTime-badEnd<=freshMs*10000;
        if(!fresh||badEnd<=badBegin)return {Action::None,next,"unknown-or-stale"};
        const auto stage=next;
        if((badEnd-badBegin)/10000<escalationBadMs)return {Action::None,stage,"insufficient-continuous-evidence"};
        if(stage==3&&(!hibernateRejected||hibernateAccepted)) {++next;return {Action::None,stage,"hibernate-not-proven-rejected"};}
        ++next;return {stage<2?Action::Sleep:stage==2?Action::Hibernate:Action::PowerOff,stage,"current-known-bad"};
    }
    U64 delay(U64 tick)const {
        if(!active()||tick<startTick)return 0;
        const auto elapsed=tick-startTick;
        const auto target=elapsed<deadlines[next]?deadlines[next]:deadlines[next]+dispatchWindow+1;
        return target>elapsed?target-elapsed:1;
    }
    void hibernateResult(bool accepted){hibernateAccepted=accepted;hibernateRejected=!accepted;invalidate();}
};
} // namespace ymcc::abnormalsleep
