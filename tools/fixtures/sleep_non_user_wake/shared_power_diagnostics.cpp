// Supplemental diagnostics of sleep.non-user-wake-consumer. No OS power action,
// device/provider I/O, window, timer, subscriber or additional background work.
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include "sleep_power_dispatch.h"
#include "sleep_power_diagnostics.h"
using Json=nlohmann::json;
using Dispatch=ymcc::sleeppower::Dispatch;
static unsigned cases=0;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void timingCases(){
    Dispatch d;Dispatch::Receipt a,b,c;
    d.callback(PBT_APMSUSPEND,18859125,[&](auto r){a=r;return true;});
    const auto first=d.forwarded(a.sequence,a.type,18859126);need(first.tick==18859125,"first receipt time");
    d.callback(PBT_APMRESUMEAUTOMATIC,18952249,[&](auto r){b=r;return true;});
    d.callback(PBT_APMRESUMESUSPEND,18952250,[&](auto r){c=r;return true;});
    d.complete(first,18952310);
    const auto second=d.forwarded(b.sequence,b.type,18952311);
    need(second.tick==18952249,"older queued receipt retains original tick");++cases;
    d.complete(second,18952312);
    const auto third=d.forwarded(c.sequence,c.type,18952313);d.complete(third,18952314);
    auto state=d.snapshot();auto rows=state["recentDeliveries"];
    need(rows.size()==3&&rows[0]["dispatchWaitMs"]==1&&rows[0]["handlerElapsedMs"]==93184,"queue and elapsed clocks remain separate");++cases;
    need(state["issued"]==3&&state["completed"]==3&&!d.pendingSuspend()&&!d.hasPending(),"diagnostics leave completed public state unchanged");++cases;
    d.complete(third,99999999);need(d.snapshot()["recentDeliveries"][2]["completedTickMs"]==18952314,"duplicate completion cannot rewrite timing");++cases;
    Dispatch overflow;Dispatch::Receipt old;
    overflow.callback(PBT_APMSUSPEND,100,[&](auto r){old=r;return true;});
    for(unsigned i=0;i!=64;++i)overflow.callback(PBT_APMRESUMEAUTOMATIC,200+i,[](auto){return true;});
    auto evicted=overflow.forwarded(old.sequence,old.type,500);need(bool(evicted)&&evicted.tick==0,"diagnostic eviction must not become an admission gate");overflow.complete(evicted,501);
    auto kept=overflow.snapshot();need(kept["recentDeliveries"].size()==8&&kept["diagnosticHistoryEvictions"]==57,"history stays bounded during storm");++cases;
    auto replay=overflow.pendingReplay();need(replay.sequence==65,"existing latest replay is preserved");auto last=overflow.forwarded(replay.sequence,replay.type,502);overflow.complete(last,503);need(!overflow.hasPending()&&!overflow.pendingSuspend(),"overflow replay still releases public fence");++cases;
    Dispatch unknown;auto w=unknown.window(PBT_APMRESUMEAUTOMATIC,1);unknown.complete(w);need(unknown.snapshot()["recentDeliveries"][0]["completedTickMs"].is_null(),"unobserved completion time is unknown");++cases;
}
static void exportCases(){
    using namespace ymcc::sleeppower;
    SharedPowerV1 native;native.generation=4;native.publicationRevision=7;native.flags=kKnown;native.phase=3;
    const auto initial=native;
    Json renderer={{"active",true},{"lifetimeEpoch",2},{"subscriberCount",3},
      {"snapshot",{{"known",true},{"phase","ready"},{"generation",999},{"hardwareWritesAllowed",true},{"secret","not-exported"}}},
      {"counters",{{"readStarted",3},{"readKnown",2}}},{"lastObservation",{{"source","read"},{"outcome","known"}}}};
    auto result=exportDiagnostics(false,native,renderer);need(result["native"]["known"]==false&&!result["native"].contains("generation"),"unknown native authority is not replaced by renderer Ready");++cases;
    result=exportDiagnostics(true,native,renderer);need(result["native"]["generation"]==4&&result["native"]["hardwareWritesAllowed"]==false&&result["grantsPowerAuthority"]==false,"renderer claims never open native hardware gate");++cases;
    need(!result["renderer"]["snapshot"].contains("secret")&&native.flags==initial.flags&&native.generation==initial.generation,"export is filtered and cannot modify native state");++cases;
    renderer["read"]={{"inFlight","yes"},{"retryStep",true},{"demand",false}};
    renderer["lastObservation"]["source"]=std::string(5000,'a');
    result=exportDiagnostics(true,native,renderer);need(!result["renderer"]["read"].contains("inFlight")&&!result["renderer"]["read"].contains("retryStep")&&!result["renderer"]["lastObservation"].contains("source"),"invalid or oversized fields are omitted");
    need(exportDiagnostics(true,native,nullptr)["renderer"]["present"]==false,"older renderer export remains supported");++cases;
}
static void ordinaryPauseWitnessCases(){
    // Execute the complete public header; no alternative receipt model.
    using Receipt=Dispatch::Receipt;
    for(const ULONG type:{PBT_APMRESUMEAUTOMATIC,PBT_APMRESUMESUSPEND,PBT_APMRESUMECRITICAL}){
        Dispatch d;Receipt resume;
        need(!d.latestResumeBoundary(),"new dispatcher has no resume witness");
        need(d.callback(type,100,[&](Receipt r){resume=r;return true;},300),"callback posted");
        const auto beforeGui=d.latestResumeBoundary();
        need(beforeGui.sequence==resume.sequence&&beforeGui.type==type&&beforeGui.fileTime100ns==300&&beforeGui.callback,"resume witness published before GUI claim");
        const auto delivered=d.forwarded(resume.sequence,type,200);d.complete(delivered,201);
        need(d.latestResumeBoundary().sequence==resume.sequence,"completion retains veto witness");
        d.callback(PBT_APMSUSPEND,202,[](Receipt){return true;},400);
        need(d.latestResumeBoundary().sequence==resume.sequence&&d.latestSleepBoundary().fileTime100ns==400,"new suspend preserves separate old resume witness");++cases;
    }
    {
        Dispatch d;Receipt resume;
        need(!d.callback(PBT_APMRESUMESUSPEND,1,[&](Receipt r){resume=r;return false;},300),"post failure observed");
        need(d.latestResumeBoundary().sequence==resume.sequence&&d.hasPending(),"failed post still vetoes stale pause");++cases;
    }
    {
        Dispatch d;auto r=d.window(PBT_APMRESUMEAUTOMATIC,1,300);
        need(d.latestResumeBoundary().sequence==r.sequence&&!d.latestResumeBoundary().callback,"direct window publishes same witness");d.complete(r);++cases;
    }
    {
        Dispatch d;Receipt resume;
        d.callback(PBT_APMRESUMESUSPEND,1,[&](Receipt r){resume=r;return true;},300);
        for(unsigned i=0;i!=16;++i)d.callback(PBT_APMSUSPEND,2+i,[](Receipt){return true;},400+i);
        need(d.latestResumeBoundary().sequence==resume.sequence&&d.latestResumeBoundary().fileTime100ns==300,"bounded diagnostic eviction cannot erase veto witness");++cases;
    }
    {
        Dispatch d;Receipt newer;
        d.callback(PBT_APMRESUMEAUTOMATIC,1,[](Receipt){return true;},300);
        d.callback(PBT_APMRESUMECRITICAL,2,[&](Receipt r){newer=r;return true;},301);
        need(d.latestResumeBoundary().sequence==newer.sequence&&newer.sequence==2,"newer resume replaces witness without resetting sequence");
        need(!d.callback(0xffffffffUL,3,[](Receipt){return true;},999)&&d.latestResumeBoundary().sequence==newer.sequence,"non-transition cannot replace witness");++cases;
    }
    {
        Dispatch d;d.callback(PBT_APMRESUMESUSPEND,1,[](Receipt){return true;});
        need(d.latestResumeBoundary()&&d.latestResumeBoundary().fileTime100ns==0,"unknown receipt time stays unknown");++cases;
    }
}

int main(int argc,char**argv){try{
    const std::string selection=argc>1?argv[1]:"all";
#ifdef YMCC_ORIGINAL_DISPATCH_CASES
    if(selection=="all")verifySharedPowerDispatch();
#endif
    if(selection=="all"||selection=="pause-witness")ordinaryPauseWitnessCases();
    if(selection=="all"||selection=="timing")timingCases();
    if(selection=="all"||selection=="export")exportCases();
    need(cases>0,"unknown diagnostic selection");
    std::cout<<Json{{"status","PASS"},{"selection",selection},{"diagnosticCases",cases},{"realOsSleep",false},{"deviceIo",false}}.dump()<<"\n";return 0;
}catch(const std::exception&error){std::cerr<<error.what()<<"\n";return 2;}}
