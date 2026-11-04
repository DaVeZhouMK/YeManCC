#pragma once
#include "json.hpp"
#include <cstdint>
#include <limits>

// Observation only: scheduler CPU accounting is independent of DRIPS, but is
// neither a power meter nor authority to issue sleep, hibernate or shutdown.
namespace ymcc::sleepactivity {
using U64=std::uint64_t;
using Json=nlohmann::json;
inline constexpr U64 maximumCaptureSpanMs=1000; // Diagnostic quality only, never an entry deadline.
struct Sample {
    U64 fileTime=0,tickMs=0,captureCostMs=0;
    U64 idle100ns=0,kernel100ns=0,user100ns=0;
    U64 processCreated100ns=0,processKernel100ns=0,processUser100ns=0;
    U64 unbiased100ns=0;
    unsigned logicalProcessors=0,processorGroups=0;
    unsigned systemError=0,processError=0,unbiasedError=0;
    bool systemKnown=false,processKnown=false,unbiasedKnown=false;
};
inline void increment(U64& n){if(n!=(std::numeric_limits<U64>::max)())++n;}
inline bool delta(U64 before,U64 after,U64& out){if(after<before)return false;out=after-before;return true;}
inline bool sum(U64 a,U64 b,U64& out){if(b>(std::numeric_limits<U64>::max)()-a)return false;out=a+b;return true;}
inline Json raw(const Sample& s) {
    return {{"observedFileTime100ns",s.fileTime},{"observedBootTickMs",s.tickMs},{"captureCostMs",s.captureCostMs},
        {"captureSufficientlyBounded",s.captureCostMs<=maximumCaptureSpanMs},
        {"systemTimesAvailable",s.systemKnown},{"systemTimesError",s.systemError},
        {"systemIdle100ns",s.systemKnown?Json(s.idle100ns):Json(nullptr)},
        {"systemKernelIncludesIdle100ns",s.systemKnown?Json(s.kernel100ns):Json(nullptr)},
        {"systemUser100ns",s.systemKnown?Json(s.user100ns):Json(nullptr)},
        {"processTimesAvailable",s.processKnown},{"processTimesError",s.processError},
        {"processCreated100ns",s.processKnown?Json(s.processCreated100ns):Json(nullptr)},
        {"processKernel100ns",s.processKnown?Json(s.processKernel100ns):Json(nullptr)},
        {"processUser100ns",s.processKnown?Json(s.processUser100ns):Json(nullptr)},
        {"unbiasedTimeAvailable",s.unbiasedKnown},{"unbiasedTimeError",s.unbiasedError},
        {"unbiasedTime100ns",s.unbiasedKnown?Json(s.unbiased100ns):Json(nullptr)},
        {"logicalProcessors",s.logicalProcessors},{"processorGroups",s.processorGroups}};
}
inline Json interval(const Sample& first,const Sample& last) {
    Json j={{"observationElapsedMs",nullptr},{"wallElapsedMs",nullptr},{"wallClockConsistent",false},
        {"captureSufficientlyBounded",first.captureCostMs<=maximumCaptureSpanMs&&last.captureCostMs<=maximumCaptureSpanMs},
        {"systemCpuDeltaAvailable",false},{"systemBusyCpu100ns",nullptr},{"systemBusyPercentOfCapacity",nullptr},
        {"processCpuDeltaAvailable",false},{"processCpu100ns",nullptr},{"processCpuPercentOfMachine",nullptr},
        {"processCpuEquivalentCores",nullptr},{"continuousHighCpuProven",false},{"unbiasedDelta100ns",nullptr},
        {"systemCpuScope","single-processor-group-only;multi-group-deltas-unverified"},
        {"counterScope","observed-scheduler-accounting;not-original-Windows-event-interval"},
        {"processCpuScope","current-native-process-only;excludes-child-host-WebView-Steam-processes"},
        {"processCpuIncludesDiagnosticWork",true},
        {"unbiasedScope","Windows-unbiased-interrupt-time;not-a-S0-or-DRIPS-state-witness"},
        {"assessment","observation-only;physical-power-UNKNOWN"},{"powerActionAuthorized",false}};
    U64 elapsed=0,wall=0;
    if(!delta(first.tickMs,last.tickMs,elapsed)||!delta(first.fileTime,last.fileTime,wall))return j;
    j["observationElapsedMs"]=elapsed;j["wallElapsedMs"]=wall/10000;
    const auto wallMs=wall/10000;const auto drift=elapsed>wallMs?elapsed-wallMs:wallMs-elapsed;
    j["wallClockConsistent"]=drift<=5000;
    // A thread may have been descheduled/suspended between OS reads. Retain
    // each raw counter but do not combine a stretched capture into CPU rates.
    if(!j["captureSufficientlyBounded"].get<bool>())return j;
    if(first.unbiasedKnown&&last.unbiasedKnown){U64 n=0;if(delta(first.unbiased100ns,last.unbiased100ns,n))j["unbiasedDelta100ns"]=n;}
    U64 pk=0,pu=0,process=0;
    if(first.processKnown&&last.processKnown&&first.processCreated100ns!=0&&first.processCreated100ns==last.processCreated100ns&&
        delta(first.processKernel100ns,last.processKernel100ns,pk)&&delta(first.processUser100ns,last.processUser100ns,pu)&&sum(pk,pu,process)) {
        j["processCpuDeltaAvailable"]=true;j["processCpu100ns"]=process;
        if(elapsed&&drift<=5000) {
            const double cores=static_cast<double>(process)/(static_cast<double>(elapsed)*10000.0);
            j["processCpuEquivalentCores"]=cores;
            if(first.logicalProcessors>0&&first.logicalProcessors==last.logicalProcessors) {
                const double percent=100.0*cores/last.logicalProcessors;
                if(percent<=100.0)j["processCpuPercentOfMachine"]=percent;
            }
        }
    }
    U64 idle=0,kernel=0,user=0,total=0;
    if(first.systemKnown&&last.systemKnown&&first.processorGroups==1&&last.processorGroups==1&&
        first.logicalProcessors>0&&first.logicalProcessors==last.logicalProcessors&&
        delta(first.idle100ns,last.idle100ns,idle)&&delta(first.kernel100ns,last.kernel100ns,kernel)&&
        delta(first.user100ns,last.user100ns,user)&&sum(kernel,user,total)&&total>=idle) {
        const auto busy=total-idle;j["systemCpuDeltaAvailable"]=true;j["systemBusyCpu100ns"]=busy;
        if(elapsed&&drift<=5000) {
            const double percent=100.0*static_cast<double>(busy)/(static_cast<double>(elapsed)*10000.0*last.logicalProcessors);
            // Preserve raw deltas, but do not fabricate a valid normalized
            // percentage when capture skew/counters exceed the wall interval.
            if(percent<=100.0)j["systemBusyPercentOfCapacity"]=percent;
        }
    }
    return j;
}
struct Window {
    static constexpr U64 minimumEventSpacingMs=30000,maxAdmissionAgeMs=10000;
    static constexpr unsigned maxCheckpoints=3,maxRecords=5;
    U64 intent=0,generation=0,retired=0,seen=0,suppressed=0;
    unsigned checkpoints=0;
    Sample first{},last{};
    bool active()const{return intent!=0;}
    bool admits(U64 original,U64 gen,U64 userWake,U64 now)const {
        return !active()&&original&&gen&&original>retired&&userWake<original&&now>=original&&
            (now-original)/10000<=maxAdmissionAgeMs;
    }
    void begin(U64 original,U64 gen,const Sample& sample){intent=original;generation=gen;first=last=sample;seen=suppressed=0;checkpoints=0;}
    bool observe(U64 eventTime,U64 nowTick) {
        if(!active())return false;increment(seen);
        if((eventTime&&eventTime<intent)||checkpoints>=maxCheckpoints||nowTick<last.tickMs||
            nowTick-last.tickMs<minimumEventSpacingMs){increment(suppressed);return false;}
        return true;
    }
    void checkpoint(const Sample& s){last=s;++checkpoints;}
    void retire(){if(intent>retired)retired=intent;intent=generation=0;}
};
} // namespace ymcc::sleepactivity
