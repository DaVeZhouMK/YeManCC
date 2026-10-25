// Exercises the actual 1 Hz worker sampling branch and production Output.
// The RTSS reader boundary is inert: no process, mapping, sensor or timer IO.
#include <atomic>
#include <fstream>
#include <iostream>
#include <limits>
#include "../deps/json/json.hpp"
using json=nlohmann::json;
#include YMCC_RTSS_HEADER
namespace {
struct Boundary {
    ymcc::monitorframe::RtssFrame next{};
    bool firstReadNeedsDiscovery=false;
    std::uint64_t reads=0,tick=0,pid=0,birth=0,at=0;
} boundary;
std::atomic<bool> g_monitorFastFrames{false};
ULONGLONG ReplayTickCount64(){return boundary.tick;}
}
namespace ymcc::monitorframe {
class ReplayRtssReader {
    bool observed_=false;
public:
    void reset(){observed_=false;}
    bool terminated()const{return false;}
    RtssFrame sample(std::uint32_t pid,std::uint64_t birth,std::uint64_t tick,std::uint64_t at){
        ++boundary.reads;boundary.pid=pid;boundary.birth=birth;boundary.tick=tick;boundary.at=at;
        if(boundary.firstReadNeedsDiscovery&&!observed_){observed_=true;return {};}
        return pid&&birth?boundary.next:RtssFrame{};
    }
};
}
#define RtssReader ReplayRtssReader
#include YMCC_FRAME_OUTPUT_HEADER
#undef RtssReader
#define GetTickCount64 ReplayTickCount64
using namespace ymcc::monitorframe;
int main(int argc,char**argv){
    if(argc!=2)return 2;
    constexpr std::uint64_t now=1700000000000ULL;
    json cases=json::array(),packets=json::object();
    auto check=[&](const char* name,bool ok){cases.push_back({{"name",name},{"ok",ok}});if(!ok)std::cerr<<name<<'\n';};
    auto fpsIs=[](const json& p,double fps,const char* source){return p.is_object()&&p.value("fps",-1.0)==fps&&p.value("fpsSource",std::string{})==source;};
    auto iteration=[&](Output& frameOutput,std::uint64_t pid,bool curve,std::uint64_t nowMs,RtssFrame next=RtssFrame{}){
        struct {std::uint64_t pid;} game{pid};
        g_monitorFastFrames.store(curve,std::memory_order_release);
        boundary.next=next;boundary.tick=nowMs-now+10000;
        // YMCC_WORKER_SAMPLING_BRANCH
        return frameOutput.packet(nowMs);
    };
    auto bind=[&](Output& o){o.bind(2077,99,now-10000);};

#ifdef YMCC_RTSS_WARM_STANDBY
    Output noGame;const auto idleReads=boundary.reads;
    check("no game never starts RTSS standby reads",iteration(noGame,0,false,now).is_null()&&boundary.reads==idleReads);
    Output standby;bind(standby);boundary.firstReadNeedsDiscovery=true;
    standby.hardware(now/1000,180,101,70,30,"fixture",now,5.5,3000);
    const auto deadline=standby.packet(now).value("fpsValidUntil",now);
    const auto readsAtStart=boundary.reads;bool primary=true;std::uint64_t iterations=0;
    for(auto at=now;at<deadline;at+=1000){
        primary=primary&&fpsIs(iteration(standby,2077,false,at,{true,10,100,at}),180,"hwinfo-presented");++iterations;
    }
    check("healthy HWiNFO keeps primary FPS while standby stays warm",primary);
    check("active game warms RTSS once per ordinary iteration with curve off",boundary.reads-readsAtStart==iterations);
    const auto fallback=iteration(standby,2077,false,deadline,{true,10,100,deadline});
    check("primary expiry has an already observed RTSS fallback without a cold-read gap",fpsIs(fallback,100,"rtss"));
    check("standby fallback preserves exact game identity and real observation",fallback.is_object()&&fallback.value("pid",0ULL)==2077&&fallback.value("processCreated",std::string{})=="99"&&fallback.value("fpsObservedAt",0ULL)==deadline);
    const auto lowUntil=fallback.is_object()?fallback.value("fps1ValidUntil",0ULL):0ULL;
    check("RTSS standby never renews the hardware LOW proof",fallback.is_object()&&fallback.value("fps1",0.0)==-1&&lowUntil==now+hardwareLifetimeMs(3000));
    const auto recovery=deadline+1000;standby.hardware(recovery/1000,200,110,70,30,"fixture",recovery,5,3000);
    check("recovered HWiNFO FPS retakes priority over warm RTSS",fpsIs(iteration(standby,2077,false,recovery,{true,10,100,recovery}),200,"hwinfo-presented"));
    const auto end=standby.packet(recovery).value("fpsValidUntil",0ULL);
    check("frozen fallback and expired hardware become unavailable",iteration(standby,2077,false,end+5000).is_null());
    standby.bind(2077,100,now-1000);
    check("PID reuse cannot inherit standby FPS",standby.packet(end+5001).is_null());
    bool ok=true;for(const auto& c:cases)ok=ok&&c["ok"].get<bool>();
    std::ofstream(argv[1])<<json({{"ok",ok},{"checks",cases.size()},{"cases",cases},{"operationCounts",{{"ordinaryIterations",iterations},{"standbyReads",boundary.reads-readsAtStart}}},{"actualWorkerSamplingBranch",true},{"actualProductionOutput",true},{"realHardwareOrRtssReads",false}}).dump(2);
    return ok?0:1;
#else
    Output noGame;
    const auto noGameStart=boundary.reads;
    check("no target game does not read RTSS with curve disabled",iteration(noGame,0,false,now).is_null());
    check("no target game does not read RTSS with curve enabled",iteration(noGame,0,true,now).is_null());
    check("no-game iterations performed zero reader calls",boundary.reads==noGameStart);

    Output healthy;bind(healthy);
    const auto healthyStart=boundary.reads;
    for(std::uint64_t i=0;i<300;++i){
        const auto at=now+i*1000;
        healthy.hardware(at/1000,180,101,70,30,"fixture",at,5.5,3000);
        if(!fpsIs(iteration(healthy,2077,false,at,{true,11.1,90,at}),180,"hwinfo-presented"))return 3;
    }
    const auto healthyReads=boundary.reads-healthyStart;
    check("300 genuine hardware polls warm RTSS exactly once per ordinary iteration",healthyReads==300);

    Output absent;bind(absent);
    const auto absentStart=boundary.reads;
    check("curve-off absent hardware and first unavailable RTSS read do not invent FPS",iteration(absent,2077,false,now).is_null());
    auto rescue=iteration(absent,2077,false,now+1000,{true,8.3,120,now+1000});
    check("curve-off absent hardware reaches RTSS on both ordinary iterations",boundary.reads-absentStart==2);
    check("curve-off absent hardware emits the genuine fallback FPS",fpsIs(rescue,120,"rtss"));
    check("rescue reader receives exact game PID birth and ordinary iteration time",boundary.pid==2077&&boundary.birth==99&&boundary.at==now+1000);
    check("rescue does not fabricate hardware low provenance",rescue.is_object()&&rescue.value("fps1",-2.0)==-1&&!rescue.contains("fps1ObservedAt"));
    if(rescue.is_object())packets["rtss-only"]=rescue;
    auto repeated=iteration(absent,2077,false,now+2000,{true,8.3,120,now+1000});
    check("unchanged fallback observation retains its original measurement time",repeated.is_object()&&repeated.value("fpsObservedAt",0ULL)==now+1000);
    check("unchanged fallback observation retains its original deadline",repeated.is_object()&&repeated.value("fpsValidUntil",0ULL)==now+6000);
    check("an unchanged fallback cannot keep expired FPS visible",iteration(absent,2077,false,now+6000,{true,8.3,120,now+1000}).is_null());

    Output expiry;bind(expiry);
    expiry.hardware(now/1000,180,101,70,30,"fixture",now,5.5,3000);
    const auto hardwareEnd=expiry.packet(now).value("fpsValidUntil",0ULL);
    const auto beforeEnd=boundary.reads;
    auto before=iteration(expiry,2077,false,now+4999,{true,11.1,90,now+4999});
    check("hardware proof remains primary through the last valid millisecond",fpsIs(before,180,"hwinfo-presented"));
    check("healthy hardware retains one warm standby read",boundary.reads==beforeEnd+1);
    auto expired=iteration(expiry,2077,false,hardwareEnd,{true,11.2,89,hardwareEnd});
    check("curve-off expired hardware performs a rescue read at its exact deadline",boundary.reads==beforeEnd+2);
    check("curve-off expired hardware selects genuine RTSS FPS",fpsIs(expired,89,"rtss"));
    check("RTSS fallback cannot renew expired hardware low proof",expired.is_object()&&expired.value("fps1",0.0)==-1&&expired.value("fps1ObservedAt",0ULL)==now&&expired.value("fps1ValidUntil",0ULL)==hardwareEnd);
    if(expired.is_object())packets["hardware-missing"]=expired;
    const auto recoveryReads=boundary.reads;
    expiry.hardware((hardwareEnd+1000)/1000,200,110,70,30,"fixture",hardwareEnd+1000,5,3000);
    auto recovered=iteration(expiry,2077,false,hardwareEnd+1000,{true,12.5,80,hardwareEnd+1000});
    check("hardware recovery retains one ordinary standby read",boundary.reads==recoveryReads+1);
    check("hardware recovery immediately resumes primary ownership",fpsIs(recovered,200,"hwinfo-presented"));

    Output duplicatePoll;bind(duplicatePoll);
    duplicatePoll.hardware(now/1000,180,101,70,30,"fixture",now,5.5,3000);
    duplicatePoll.hardware(now/1000,180,-1,70,30,"fixture",hardwareEnd,5.5,3000);
    auto dup=iteration(duplicatePoll,2077,false,hardwareEnd,{true,9,111,hardwareEnd});
    check("repackaging an old hardware poll does not block rescue",fpsIs(dup,111,"rtss"));
    check("repackaging does not renew hardware LOW expiry",dup.is_object()&&dup.value("fps1ValidUntil",0ULL)==hardwareEnd);

    Output heldPrimary;bind(heldPrimary);
    heldPrimary.hardware(now/1000,180,101,70,30,"fixture",now,5.5,3000);
    const auto heldProof=heldPrimary.packet(now);
    const auto heldEnd=heldProof.value("fpsValidUntil",0ULL);
    bool stayedPrimary=true,proofUnchanged=true;
    for(auto dt=1000ULL;dt<6000;dt+=1000){
        heldPrimary.observeRtss({true,10,100,now+dt});
        heldPrimary.hardware((now+dt)/1000,dt%2000==0?.5:0,0,71,31,"fixture",now+dt,0,3000);
        const auto p=heldPrimary.packet(now+dt);
        stayedPrimary=stayedPrimary&&fpsIs(p,180,"hwinfo-presented");
        proofUnchanged=proofUnchanged&&p.value("fpsObservedAt",0ULL)==now&&p.value("fpsValidUntil",0ULL)==heldEnd&&
            p.value("fps1ObservedAt",0ULL)==now&&p.value("fps1ValidUntil",0ULL)==heldProof.value("fps1ValidUntil",0ULL);
    }
    check("isolated zero/sub-one HW samples preserve the still-valid HW primary",stayedPrimary);
    check("unavailable HW samples never renew the retained FPS or LOW proof",proofUnchanged);
    check("sustained HW loss permits RTSS only after the original HW deadline",fpsIs(heldPrimary.packet(heldEnd),100,"rtss"));
    heldPrimary.hardware(heldEnd/1000,200,110,70,30,"fixture",heldEnd,5,3000);
    check("a recovered real HW observation immediately retakes priority",fpsIs(heldPrimary.packet(heldEnd),200,"hwinfo-presented"));
    check("unavailable observations cannot keep either source alive forever",heldPrimary.packet(heldEnd+6000).is_null());

    Output zero;bind(zero);
    zero.hardware(now/1000,0,0,70,30,"fixture",now,0,3000);
    auto zeroRescue=iteration(zero,2077,false,now+1,{true,10,100,now+1});
    check("curve-off zero hardware FPS permits real RTSS rescue",fpsIs(zeroRescue,100,"rtss"));
    Output subOne;bind(subOne);
    subOne.hardware(now/1000,0.5,-1,70,30,"fixture",now,2000,3000);
    check("curve-off below-one hardware FPS permits real RTSS rescue",fpsIs(iteration(subOne,2077,false,now+1,{true,10,100,now+1}),100,"rtss"));
    Output noFallback;bind(noFallback);
    noFallback.hardware(now/1000,0,-1,70,30,"fixture",now,0,3000);
    const auto invalidStart=boundary.reads;
    auto invalid=iteration(noFallback,2077,false,now+1,{false,0,999,now+1});
    check("invalid RTSS is still attempted when the primary is zero",boundary.reads==invalidStart+1);
    check("invalid RTSS cannot manufacture a positive FPS",invalid.is_null()||(invalid.is_object()&&invalid.value("fps",-1.0)<=0));
    Output invalidHardware;bind(invalidHardware);
    invalidHardware.hardware(now/1000,std::numeric_limits<double>::quiet_NaN(),101,70,30,"fixture",now,5.5,3000);
    check("invalid hardware FPS does not prevent fallback",fpsIs(iteration(invalidHardware,2077,false,now+1,{true,10,100,now+1}),100,"rtss"));

    Output curve;bind(curve);
    curve.hardware(now/1000,180,101,70,30,"fixture",now,5.5,3000);
    const auto curveStart=boundary.reads;
    iteration(curve,2077,true,now+100,{true,11.1,90,now+100});
    auto dual=iteration(curve,2077,true,now+200,{true,11.2,89,now+200});
    check("enabled curve preserves both RTSS sampling calls with valid hardware FPS",boundary.reads==curveStart+2);
    check("curve sampling never overrides valid presented FPS",fpsIs(dual,180,"hwinfo-presented"));
    check("curve keeps genuine RTSS durations independently",dual.is_object()&&dual.value("frameTimeSource",std::string{})=="rtss-current-frame"&&dual.value("frametime",0.0)==11.2&&dual["frameTimes"].size()==2);
    check("curve sampling preserves the hardware LOW source and time",dual.is_object()&&dual.value("fps1",0.0)==101&&dual.value("fps1ObservedAt",0ULL)==now);
    if(dual.is_object())packets["dual-source"]=dual;
    const auto afterCurve=boundary.reads;
    check("turning curve off retains hardware FPS",fpsIs(iteration(curve,2077,false,now+300,{true,10,100,now+300}),180,"hwinfo-presented"));
    check("turning curve off retains only the ordinary standby read",boundary.reads==afterCurve+1);

    Output changed;bind(changed);
    iteration(changed,2077,false,now,{true,8.3,120,now});
    changed.bind(2077,100,now+1000);
    auto oldProcess=iteration(changed,2077,false,now+1001,{true,8.3,120,now});
    check("same-PID new process cannot inherit the old fallback observation",oldProcess.is_null());
    changed.bind(88,101,now+2000);
    auto newProcess=iteration(changed,88,false,now+2001,{true,12,83,now+2001});
    check("new process fallback carries its exact PID and creation identity",newProcess.is_object()&&newProcess.value("pid",0ULL)==88&&newProcess.value("processCreated",std::string{})=="101");

    Output rollback;bind(rollback);
    rollback.hardware(now/1000,180,101,70,30,"fixture",now,5.5,3000);
    check("clock before the hardware observation does not claim fresh hardware",fpsIs(iteration(rollback,2077,false,now-1,{true,10,100,now-1}),100,"rtss"));

    Output sustained;bind(sustained);
    const auto sustainedStart=boundary.reads;
    bool sustainedValid=true;
    for(std::uint64_t i=0;i<300;++i){
        const auto at=now+i*1000;
        sustainedValid=sustainedValid&&fpsIs(iteration(sustained,2077,false,at,{true,10,100,at}),100,"rtss");
    }
    const auto fallbackReads=boundary.reads-sustainedStart;
    check("300 curve-off missing-primary iterations each read RTSS once",fallbackReads==300);
    check("sustained rescue keeps genuine FPS available",sustainedValid);
    const auto sustainedRecovery=boundary.reads;
    bool sustainedRecovered=true;
    for(std::uint64_t i=300;i<600;++i){
        const auto at=now+i*1000;
        sustained.hardware(at/1000,180,101,70,30,"fixture",at,5.5,3000);
        sustainedRecovered=sustainedRecovered&&fpsIs(iteration(sustained,2077,false,at,{true,10,100,at}),180,"hwinfo-presented");
    }
    check("300 recovered-primary iterations each preserve HWiNFO FPS",sustainedRecovered);
    check("300 recovered-primary iterations retain one standby read each",boundary.reads==sustainedRecovery+300);

    bool ok=true;for(const auto& c:cases)ok=ok&&c["ok"].get<bool>();
    std::ofstream(argv[1])<<json({{"ok",ok},{"checks",cases.size()},{"cases",cases},{"packets",packets},
        {"operationCounts",{{"healthyCurveOffIterations",300},{"healthyCurveOffRtssReads",healthyReads},{"missingPrimaryCurveOffIterations",300},{"missingPrimaryCurveOffRtssReads",fallbackReads},{"recoveredCurveOffIterations",300},{"recoveredCurveOffRtssReads",boundary.reads-sustainedRecovery}}},
        {"actualWorkerSamplingBranch",true},{"actualProductionOutput",true},{"realHardwareOrRtssReads",false},{"realProcessOrTimerOperations",false}}).dump(2)<<'\n';
    return ok?0:1;
#endif
}
