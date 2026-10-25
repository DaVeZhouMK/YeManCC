#pragma once
#include <cstdint>
namespace ymcc::steamOriginal {
// Immutable join of the already admitted kernel witness and configuration.
// Power Ready alone grants no native Running mutation authority.
struct WakeProof {
    std::uint64_t generation=0,epoch=0,observedGeneration=0,observedEpoch=0,
        requestedGeneration=0,admissionObservedGeneration=0,boundaryGeneration=0,
        eventFileTime=0,admissionFileTime=0,boundaryFileTime=0,observedBoundaryFileTime=0,
        latestEntryFileTime=0,latestEntryEpoch=0;
    int eventId=0;
    bool admitted=false,confirmed=false,fresh=false,requested=false,transferred=false,sourceMatched=false;
    bool operator==(const WakeProof&) const=default;
    bool valid(std::uint64_t currentGeneration,std::uint64_t currentEpoch,std::uint64_t now) const {
        const bool same=observedGeneration==generation;
        const bool next=transferred&&observedGeneration&&observedGeneration!=UINT64_MAX&&
            observedGeneration+1==generation&&boundaryGeneration==observedGeneration;
        return generation&&epoch&&generation==currentGeneration&&epoch==currentEpoch&&observedEpoch==epoch&&
            requestedGeneration==generation&&admissionObservedGeneration==observedGeneration&&(same||next)&&
            admitted&&confirmed&&fresh&&requested&&sourceMatched&&(eventId==507||eventId==107)&&
            eventFileTime&&eventFileTime==admissionFileTime&&boundaryFileTime&&eventFileTime>boundaryFileTime&&
            (!observedBoundaryFileTime||observedBoundaryFileTime==boundaryFileTime)&&
            (latestEntryEpoch!=epoch||latestEntryFileTime<eventFileTime)&&now>=eventFileTime&&
            now-eventFileTime<60000ULL*10000;
    }
};
// Bounded read scheduling within an already admitted physical wake. The host
// validates related hints, checks cancellation first, and owns the CV wait.
// Hints change cadence only; they never create/refresh proof or mutation rights.
struct WakeWatch {
    static constexpr std::uint64_t budgetMs=60000,observationMs=55000,pollMs=10000,
        fastWindowMs=2000,fastPollMs=50,hintWindowMs=3000,hintPollMs=1000,
        settleWindowMs=budgetMs,settlePollMs=1000,settleFastWindowMs=20000,settleFastPollMs=250,
        precisionStartMs=4000,precisionEndMs=7500,precisionPollMs=25;
    static constexpr unsigned maximumFastProbes=240,maximumProbes=320;
    std::uint64_t eventFileTime=0,observationStartFileTime=0,nextProbeFileTime=0,hintUntilFileTime=0,
        lastActivityFileTime=0,lastProbeFileTime=0;
    unsigned probes=0,fastProbes=0,settleFastProbes=0,precisionProbes=0;
    static constexpr std::uint64_t addMs(std::uint64_t base,std::uint64_t ms) {
        return ms>(UINT64_MAX-base)/10000ULL?UINT64_MAX:base+ms*10000ULL;
    }
    std::uint64_t observationEnd() const { return addMs(eventFileTime,observationMs); }
    bool allows(std::uint64_t nowFileTime,bool current) const {
        return current&&eventFileTime&&nowFileTime>=eventFileTime&&
            nowFileTime>=lastActivityFileTime&&
            nowFileTime-eventFileTime<budgetMs*10000ULL&&probes<maximumProbes;
    }
    bool observes(std::uint64_t nowFileTime,bool current) const {
        return allows(nowFileTime,current)&&nowFileTime<observationEnd();
    }
    // Start scheduling once when observation really begins. Physical wake
    // proof, the 55s observation end and the 60s mutation end stay immutable.
    bool beginObservation(std::uint64_t nowFileTime,bool current) {
        if(observationStartFileTime||probes||!observes(nowFileTime,current))return false;
        observationStartFileTime=lastActivityFileTime=nextProbeFileTime=nowFileTime;
        return true;
    }
    bool fastWindow(std::uint64_t nowFileTime) const {
        const auto start=observationStartFileTime?observationStartFileTime:eventFileTime;
        return nowFileTime<addMs(start,fastWindowMs)||nowFileTime<hintUntilFileTime;
    }
    // Both confirmed field losses were near physical wake +6s. Reserve a
    // fixed window around that evidence; delayed observer start and hints must
    // not move it, restart it, or grant any additional recovery authority.
    bool precisionWindow(std::uint64_t nowFileTime) const {
        return observationStartFileTime&&nowFileTime>=observationStartFileTime&&
            nowFileTime>=addMs(eventFileTime,precisionStartMs)&&
            nowFileTime<addMs(eventFileTime,precisionEndMs);
    }
    // Keep the 1s fallback throughout an admitted observation. observes()
    // still clamps every read to the original physical wake +55s endpoint.
    bool settling(std::uint64_t nowFileTime) const {
        return observationStartFileTime&&nowFileTime>=observationStartFileTime&&
            nowFileTime<addMs(observationStartFileTime,settleWindowMs);
    }
    // The 25ms, 50ms and 250ms stages share the existing subsecond read allowance.
    // Spending moderate reads cannot leave a later hint burst free to exhaust
    // the total read budget before the original physical observation endpoint.
    bool subsecondCapacity() const {
        return fastProbes<maximumFastProbes&&settleFastProbes<maximumFastProbes-fastProbes;
    }
    std::uint64_t periodMs(std::uint64_t nowFileTime) const {
        if(precisionWindow(nowFileTime)&&subsecondCapacity())return precisionPollMs;
        if(fastWindow(nowFileTime))return subsecondCapacity()?fastPollMs:hintPollMs;
        if(!settling(nowFileTime))return pollMs;
        // Late loss has been observed after the initial 2s burst. Keep a
        // moderate cadence only in the first 20s of this admitted observer;
        // neither hints nor a late start move the physical 55/60s endpoints.
        return nowFileTime<addMs(observationStartFileTime,settleFastWindowMs)&&subsecondCapacity()?settleFastPollMs:settlePollMs;
    }
    std::uint64_t dueFileTime() const {
        auto due=nextProbeFileTime?nextProbeFileTime:eventFileTime;
        const auto precisionStart=addMs(eventFileTime,precisionStartMs);
        // A pending moderate wait must wake at the fixed window boundary.
        if(observationStartFileTime&&observationStartFileTime<precisionStart&&
            lastProbeFileTime<precisionStart&&due>precisionStart)
            due=precisionStart;
        return due<observationEnd()?due:observationEnd();
    }
    bool probeDue(std::uint64_t nowFileTime) const {
        return observes(nowFileTime,true)&&nowFileTime>=dueFileTime();
    }
    // Positive ceil milliseconds, including when already due; the host checks
    // probeDue before waiting. Queries never consume or refund any budget.
    std::uint64_t delayMs(std::uint64_t nowFileTime) const {
        if(!observes(nowFileTime,true))return pollMs;
        const auto due=dueFileTime();
        if(due<=nowFileTime)return 1;
        const auto ticks=due-nowFileTime;
        return ticks/10000ULL+(ticks%10000ULL!=0);
    }
    // current means both a trusted related hint and still-current authority.
    // Invalid/future/expired/backward hints leave every field untouched.
    bool noteHint(std::uint64_t nowFileTime,bool current) {
        if(!observes(nowFileTime,current))return false;
        const auto end=addMs(nowFileTime,hintWindowMs);
        hintUntilFileTime=end<observationEnd()?end:observationEnd();
        lastActivityFileTime=nowFileTime;
        // Wake a quiet wait early, but merge events against the last actual
        // read. An event storm cannot bypass the current phase cadence or budget.
        auto due=lastProbeFileTime?addMs(lastProbeFileTime,periodMs(nowFileTime)):nowFileTime;
        if(due<nowFileTime)due=nowFileTime;
        if(due>observationEnd())due=observationEnd();
        if(!nextProbeFileTime||due<nextProbeFileTime)nextProbeFileTime=due;
        return true;
    }
    // Intentionally budget-only (no due check), preserving authority fixtures.
    // Production callers must check probeDue, then reserve immediately before
    // the read. One attempted mutation still consumes the host's original right.
    bool reserveProbe(std::uint64_t nowFileTime,bool current) {
        if(!observes(nowFileTime,current))return false;
        if(subsecondCapacity()){
            if(precisionWindow(nowFileTime)){++fastProbes;++precisionProbes;}
            else if(fastWindow(nowFileTime))++fastProbes;
            else if(settling(nowFileTime)&&nowFileTime<addMs(observationStartFileTime,settleFastWindowMs))++settleFastProbes;
        }
        ++probes;
        lastActivityFileTime=lastProbeFileTime=nowFileTime;
        const auto due=addMs(nowFileTime,periodMs(nowFileTime));
        nextProbeFileTime=due<observationEnd()?due:observationEnd();
        return true;
    }
};
}