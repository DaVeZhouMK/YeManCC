// Actual production Output with packet equivalence and bounded allocation counts.
// No RTSS sample/process/mapping calls; hardware() receives deterministic receipts.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include "../deps/json/json.hpp"
using json=nlohmann::json;
static bool tracking=false;
static std::uint64_t allocations=0,allocatedBytes=0;
void* operator new(std::size_t n){if(tracking){++allocations;allocatedBytes+=n;}auto* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#include YMCC_FRAME_OUTPUT_HEADER
using namespace ymcc::monitorframe;
int main(int argc,char** argv){
    if(argc!=2)return 2;
    constexpr std::uint64_t now=1700000000000ULL;
    json checks=json::array(),trace=json::array(),workloads=json::array();
    auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"ok",ok}});if(!ok)std::cerr<<name<<'\n';};
    auto record=[&](const char* label,Output& o,std::uint64_t at){auto p=o.packet(at);trace.push_back({{"label",label},{"at",at},{"rescue",o.needsRtssRescue(at)},{"packet",p},{"packetBytes",p.dump()}});return p;};
    auto fpsIs=[](const json& p,double fps){return p.is_object()&&p.value("fps",-1.0)==fps;};
    const std::string title="Cyberpunk 2077 / \xe4\xb8\xad\xe6\x96\x87 / \"\\\n / frame-generation fixture";
    Output o;
    check("unbound output has no packet",record("unbound",o,now).is_null());
    o.hardware(now/1000,180,101,70,30,title,now,5.5,3000);
    check("unbound hardware receipt cannot create a packet",record("unbound-hardware",o,now).is_null());
    o.bind(2077,99,now-10000);
    o.hardware(now/1000,180,101,70,30,title,now,5.5,3000);
    auto p=record("initial",o,now);
    check("initial packet preserves presented FPS and raw title",fpsIs(p,180)&&p["game"]==title&&p["fpsSource"]=="hwinfo-presented");
    check("initial proof deadlines match the producer period",p["fpsObservedAt"]==now&&p["fpsValidUntil"]==now+5000&&p["fps1ValidUntil"]==now+5000);
    o.hardware(now/1000,180,101,70,30,title,now+1000,5.5,3000);
    auto duplicate=record("same-poll",o,now+1000);
    check("duplicate poll updates only envelope time, not proof expiry",duplicate["ts"]==now+1000&&duplicate["fpsObservedAt"]==now&&duplicate["fpsValidUntil"]==now+5000&&duplicate["fps1ValidUntil"]==now+5000);
    check("duplicate poll does not append a frame-history point",duplicate["frameTimes"].size()==1);
    o.hardware(now/1000,179.24,-1,-1,31.26,"short",now+1500,6.25,1500);
    auto changed=record("same-poll-correction",o,now+1500);
    check("changed values within one producer second remain visible",fpsIs(changed,179.2)&&changed["gpu"].is_null()&&changed["packagePower"]==31.3&&changed["game"]=="short");
    check("missing LOW correction retains the original LOW proof",changed["fps1"]==101&&changed["fps1ObservedAt"]==now&&changed["fps1ValidUntil"]==now+5000&&changed["fps1PollPeriodMs"]==3000);
    check("same-poll frame correction replaces rather than appends",changed["frametime"]==6.25&&changed["frameTimes"].size()==1&&changed["frameTimePollPeriodMs"]==3000);
    o.hardware(now/1000,178.25,0,88.6,32.14,title,now+2000,7.25,6000);
    auto lowZero=record("same-poll-low-zero",o,now+2000);
    check("same-poll true zero LOW is retained without deadline renewal",lowZero["fps1"]==0&&lowZero["fps1ValidUntil"]==now+5000&&lowZero["fps1PollPeriodMs"]==3000);
    check("GPU null to integer transition preserves rounding",lowZero["gpu"].is_number_integer()&&lowZero["gpu"]==89);
    check("FPS period metadata can change on the same producer poll",lowZero["fpsPollPeriodMs"]==6000&&lowZero["fpsValidUntil"]==now+6000);
    // Mutating the returned packet must never alter internal metadata or types.
    lowZero["fpsSource"]="external";lowZero["fps"]=nullptr;lowZero["game"]=false;lowZero["fps1ObservedAt"]=false;
    o.hardware((now+3000)/1000,200,110,0,0,title,now+3000,5,3000);
    auto newer=record("new-producer-poll",o,now+3000);
    check("returned packet mutation cannot corrupt future output",fpsIs(newer,200)&&newer["fpsSource"]=="hwinfo-presented"&&newer["game"]==title&&newer["fps1ObservedAt"]==now+3000);
    check("new producer poll gives a new genuine proof",newer["fpsObservedAt"]==now+3000&&newer["fps1ValidUntil"]==now+8000);
    o.hardware((now+2000)/1000,999,999,99,99,"old",now+4000,1,3000);
    check("older producer poll cannot replace current values",fpsIs(record("older-poll-rejected",o,now+4000),200));
    for(const auto fps:{-1.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),1000001.0}){
        o.hardware((now+4000)/1000,fps,999,99,99,"invalid",now+4000,1,3000);
        check("invalid FPS cannot overwrite the accepted packet",fpsIs(record("invalid-fps-rejected",o,now+4000),200));
    }
    o.hardware(0,999,999,99,99,"zero-poll",now+4000,1,3000);
    check("zero producer timestamp cannot overwrite a valid packet",fpsIs(record("zero-poll-rejected",o,now+4000),200));
    check("hardware packet expires at its original deadline",record("expired",o,now+8000).is_null());
    o.observeRtss({true,8.3,120,now+8000});
    auto fallback=record("rescue",o,now+8001);
    check("expired hardware can still use genuine RTSS rescue",fpsIs(fallback,120)&&fallback["fpsSource"]=="rtss");
    check("rescue cannot freshen old LOW",fallback["fps1"]==-1&&fallback["fps1ValidUntil"]==now+8000);
    o.hardware((now+9000)/1000,240,150,75,33,title,now+9000,4.2,3000);
    auto recover=record("recovery",o,now+9000);
    check("hardware resumes primary ownership after rescue",fpsIs(recover,240)&&recover["fpsSource"]=="hwinfo-presented"&&!o.needsRtssRescue(now+9000));
    o.hardware((now+10000)/1000,0,-1,-1,-1,"",now+10000,0,3000);
    auto zero=record("zero",o,now+10000);
    check("real zero hardware FPS still permits genuine RTSS fallback",fpsIs(zero,120)&&zero["fpsSource"]=="rtss"&&zero["game"]==""&&zero["gpu"].is_null());
    Output isolatedZero;isolatedZero.bind(2077,99,now-10000);
    isolatedZero.hardware(now/1000,180,101,70,30,title,now,5.5,3000);
    isolatedZero.hardware((now+1000)/1000,0,-1,-1,-1,"",now+1000,0,3000);
    auto noRtssZero=record("zero-without-rtss",isolatedZero,now+1000);
    check("zero without RTSS retains zero and clears the hardware curve",fpsIs(noRtssZero,0)&&!noRtssZero.contains("frameTimes"));
    o.bind(2077,100,now+11000);
    check("same PID with new creation identity immediately clears old proof",record("pid-reuse",o,now+11000).is_null());
    o.hardware((now+12000)/1000,180,-1,50,30,title,now+12000,5.5,3000);
    auto next=record("new-instance",o,now+12000);
    check("new instance has exact new process identity",next["processCreated"]=="100"&&next["pid"]==2077&&next["fps1"]==-1);
    o.bind(88,101,now+13000);
    check("different process immediately clears proof",record("new-game",o,now+13000).is_null());
    o.hardware((now+14000)/1000,120,90,70,30,title,now+14000,8.3,1000);
    o.reset();check("explicit reset clears packet",record("reset",o,now+14000).is_null());

    // Character-level equivalence includes numeric extremes, null transitions,
    // changing titles, invalid LOW, changing periods and same-poll corrections.
    Output varied;varied.bind(2077,99,now-10000);
    const std::string longTitle(4096,'x');
    const std::uint64_t periods[]={0,1000,1500,2000,3000,6000,60000};
    for(std::uint64_t i=0;i<360;++i){
        const auto at=now+i*1000,poll=(now+(i/3)*3000)/1000;
        const double fps=i%17==0?0:i%7==0?0.5:i%11==0?1000000:1+(i*37)%997+0.04;
        const double low=i%5==0?-1:i%13==0?std::numeric_limits<double>::quiet_NaN():double((i*19)%180);
        const double gpu=i%3==0?-1:double(i%101)+0.4;
        const double power=i%19==0?std::numeric_limits<double>::quiet_NaN():double(i%80)+0.16;
        const auto& name=i%5==0?longTitle:title;
        varied.hardware(poll,fps,low,gpu,power,name,at,i%9==0?-1:4.25+(i%23),periods[i%7]);
        if(i%4==0)varied.observeRtss({true,7.5+(i%5),double(100+i%60),at+100});
        record("varied-producer",varied,at+101);trace.back()["hardwareInput"]={{"gpu",gpu},{"power",power}};
        record("varied-aging",varied,at+900);trace.back()["hardwareInput"]={{"gpu",gpu},{"power",power}};
    }
    // Three production cadence patterns. Track only the actual hardware()
    // call, so packet snapshots, fixture bookkeeping and dumps are excluded.
    for(const auto period:{1000ULL,3000ULL,6000ULL}){
        Output measured;measured.bind(2077,99,now-10000);
        measured.hardware(now/1000,180,101,70,30,title,now,5.5,period);
        const auto objectBytes=sizeof(measured);
        allocations=allocatedBytes=0;
        bool coherent=true;
        for(std::uint64_t i=1;i<=300;++i){
            const auto at=now+i*1000,poll=(now+(i*1000/period)*period)/1000;
            const double fps=180+(i%3),low=100+(i%2),gpu=i%101,power=30+(i%7)*0.1;
            tracking=true;
            measured.hardware(poll,fps,low,gpu,power,title,at,5.5+(i%4)*0.1,period);
            tracking=false;
            auto current=record("measured-cadence",measured,at);
            coherent=coherent&&fpsIs(current,fps)&&current["fpsObservedAt"]==poll*1000&&current["fpsSource"]=="hwinfo-presented";
        }
        check("300 accepted receipts retain current values at the requested cadence",coherent);
        workloads.push_back({{"producerPeriodMs",period},{"calls",300},{"allocations",allocations},{"allocatedBytes",allocatedBytes},{"outputObjectBytes",objectBytes}});
    }
    // No usable source must return null without copying an expired JSON tree.
    Output expired;expired.bind(2077,99,now-10000);
    expired.hardware(now/1000,180,101,70,30,title,now,5.5,3000);
    allocations=allocatedBytes=0;bool allExpired=true;
    for(std::uint64_t i=0;i<300;++i){
        const auto at=now+5000+i*1000;
        tracking=true;auto absent=expired.packet(at);tracking=false;
        allExpired=allExpired&&absent.is_null();
        record("expired-without-source",expired,at);
    }
    check("300 expired-source packets stay absent",allExpired);
    workloads.push_back({{"operation","expired-packet"},{"calls",300},{"allocations",allocations},{"allocatedBytes",allocatedBytes},{"outputObjectBytes",sizeof(expired)}});
    bool ok=true;for(const auto& c:checks)ok=ok&&c["ok"].get<bool>();
    std::ofstream(argv[1])<<json({{"ok",ok},{"checks",checks.size()},{"cases",checks},{"packetComparisons",trace.size()},{"trace",trace},{"workloads",workloads},{"realHardwareOrRtssReads",false},{"timingOrCpuPercentageMeasured",false}}).dump(2)<<'\n';
    return ok?0:1;
}
