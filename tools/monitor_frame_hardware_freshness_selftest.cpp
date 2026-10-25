// Production Output and exact ordinary-lane admission; no device or timing benchmark.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include "../deps/json/json.hpp"
using json=nlohmann::json;
static bool track=false;
static unsigned long long allocations=0;
void* operator new(std::size_t n){if(track)++allocations;auto* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#include "../native/monitor_frame_output.h"
#include "../native/monitor_presented_target.h"
using namespace ymcc::monitorframe;
struct Hw { std::array<char,128> fpsTargetCaption{}; bool sharedOk=true,fpsSensor=true,cpuPowerValid=true,periodKnown=true; std::uint64_t producerPollTime=0,producerPeriodMs=3000;double fps=120,fps1=90,gpuLoad=97,packagePower=31,frametimeMs=8.3; };
struct Game {std::uint64_t pid=77,processCreated=99;std::wstring name=L"fixture";};
std::string W2U(const std::wstring& s){return {s.begin(),s.end()};}
static void ordinary(Output& frameOutput,Hw hw,const Game& game,std::uint64_t nowMs,bool hwReadable=true,bool hwFresh=true){
    const bool hwHealthy=monitorHwHealthyVerdict(hwReadable,hw.sharedOk,hw.producerPollTime,hwFresh);
    // YMCC_HARDWARE_BRANCH
}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 constexpr std::uint64_t t=1700000000000ULL;
 json checks=json::array(),packets=json::array();
 const auto check=[&](const char* n,bool ok){checks.push_back({{"name",n},{"ok",ok}});if(!ok)std::cerr<<n<<'\n';};
 const auto record=[&](const char* n,Output& o,std::uint64_t at){auto p=o.packet(at);packets.push_back({{"name",n},{"at",at},{"packet",p}});return p;};
 Hw hw;hw.producerPollTime=t/1000;Game game;
 Output o;o.bind(77,99,t-10000);ordinary(o,hw,game,t);
 auto p=record("initial",o,t);
 check("hardware proof uses producer age and the existing health budget",p["gpu"]==97&&p["packagePower"]==31&&p["hardwareObservedAt"]==t&&p["hardwareValidUntil"]==t+6000&&p["hardwarePollPeriodMs"]==3000);
 o.observeRtss({true,8.3,120,t+10000});p=record("expired-hardware-rtss",o,t+10001);
 check("genuine RTSS FPS survives original hardware expiry",p["fps"]==120&&p["fpsSource"]=="rtss"&&p["fpsObservedAt"]==t+10000);
 check("RTSS never renews GPU or power",p["gpu"].is_null()&&p["packagePower"]==-1&&p["hardwareObservedAt"]==t&&p["hardwareValidUntil"]==t+6000);
 check("LOW and curve retain independent proofs",p["fps1"]==-1&&p["fps1ValidUntil"]==t+hardwareLifetimeMs(3000)&&p["frameTimeSource"]=="rtss-current-frame"&&p["frameTimeTs"]==t+10000);
 hw.producerPollTime=(t+10000)/1000;hw.fpsSensor=false;hw.gpuLoad=46;hw.packagePower=22.5;
 ordinary(o,hw,game,t+10001);p=record("healthy-hardware-missing-fps",o,t+10001);
 check("healthy hardware refreshes without a Presented FPS sensor",p["gpu"]==46&&p["packagePower"]==22.5&&p["hardwareObservedAt"]==t+10000&&p["hardwareValidUntil"]==t+16000&&p["fpsSource"]=="rtss");
 ordinary(o,hw,game,t+10002,false);p=record("immediate-read-failure",o,t+10002);
 check("read failure withdraws metrics immediately without withdrawing RTSS FPS",p["gpu"].is_null()&&p["packagePower"]==-1&&p["fps"]==120&&p["hardwareValidUntil"]==t+16000);
 ordinary(o,hw,game,t+10003);p=o.packet(t+10003);
 check("a healthy reread can restore values without renewing a repeated poll",p["gpu"]==46&&p["hardwareValidUntil"]==t+16000);
 ordinary(o,hw,game,t+10004,true,false);p=o.packet(t+10004);
 check("unhealthy producer withdraws values even with readable shared memory",p["gpu"].is_null()&&p["packagePower"]==-1&&p["fps"]==120);
 Output noFps;noFps.bind(77,99,t-10000);ordinary(noFps,hw,game,t+10000);noFps.observeRtss({true,8.3,120,t+10000});p=record("hardware-never-had-fps",noFps,t+10001);
 check("hardware that never supplied FPS is still available beside RTSS",p["gpu"]==46&&p["packagePower"]==22.5&&p["game"]=="fixture"&&p["fpsSource"]=="rtss");
 check("no synthetic LOW or hardware curve is created",p["fps1"]==-1&&p["frameTimeSource"]=="rtss-current-frame"&&p["frameTimes"].size()==1);
 hw.fpsSensor=true;hw.fps=240;hw.fps1=150;hw.producerPollTime=(t+11000)/1000;ordinary(noFps,hw,game,t+11000);p=record("presented-restored",noFps,t+11000);
 check("Presented FPS including generated frames retakes priority",p["fps"]==240&&p["fpsSource"]=="hwinfo-presented"&&!noFps.needsRtssRescue(t+11000)&&p["fps1"]==150);
 hw.cpuPowerValid=false;ordinary(noFps,hw,game,t+11001);p=noFps.packet(t+11001);
 check("absent CPU power is not fabricated as a measured zero",p["gpu"]==46&&p["packagePower"]==-1);
 noFps.invalidateHardwareMetrics();p=noFps.packet(t+11002);
 check("metric withdrawal does not alter valid Presented FPS or LOW",p["gpu"].is_null()&&p["fps"]==240&&p["fps1"]==150);
 Output period;period.bind(77,99,t-10000);period.hardware(t/1000,120,90,40,20,"fixture",t,8.3,1000);
 period.hardwareMetrics(t/1000,41,21,"fixture",t+1000,60000);p=record("same-poll-period-correction",period,t+1000);
 check("period correction cannot extend an original metrics deadline",p["gpu"]==41&&p["hardwareValidUntil"]==t+5000&&p["hardwarePollPeriodMs"]==1000);
 const auto stable=p;
 period.hardwareMetrics((t-1000)/1000,99,99,"old",t+1000,1000);
 period.hardwareMetrics((t+10000)/1000,99,99,"future",t+1000,1000);
 check("older and future hardware receipts cannot replace accepted metrics or game",period.packet(t+1000)==stable);
 period.observeRtss({true,8.3,120,t+4999});p=record("exact-metric-expiry",period,t+5000);
 check("metric expiry is strict at its original deadline",p["gpu"].is_null()&&p["packagePower"]==-1&&p["fps"]==120);
 Output clean;clean.bind(77,99,t-10000);clean.hardwareMetrics(t/1000,40,20,"fixture",t,3000);
 check("metrics alone never create an FPS packet",clean.packet(t).is_null());
 clean.observeRtss({true,8.3,120,t});clean.bind(77,100,t+1000);clean.observeRtss({true,8.3,120,t+1001});p=record("pid-reuse",clean,t+1001);
 check("new process lifetime cannot inherit old metrics",p["gpu"].is_null()&&p["packagePower"]==-1);
 for(const auto invalid:{-1.0,2001.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){
  clean.hardwareMetrics((t+2000)/1000,invalid,invalid,"fixture",t+2000,3000);p=clean.packet(t+2000);
  check("invalid hardware values remain absent",p["gpu"].is_null()&&p["packagePower"]==-1);
 }
 Output clock;clock.bind(77,99,t-10000);clock.hardware(t/1000,120,90,40,20,"fixture",t,8.3,3000);
 check("clock before observation cannot use a packet",clock.packet(t-1).is_null());
 const std::string title="steady";Output budget;budget.bind(77,99,t-10000);budget.hardware(t/1000,120,90,40,20,title,t,8.3,3000);allocations=0;track=true;
 for(std::uint64_t i=1;i<=300;++i){const auto at=t+i*1000;budget.hardwareMetrics(at/1000,40,20,title,at,3000);budget.hardware(at/1000,120,90,40,20,title,at,8.3,3000);}
 track=false;check("300 ordinary receipts with separate metrics require no new heap allocations",allocations==0);
 bool ok=true;for(const auto& c:checks)ok=ok&&c["ok"].get<bool>();
 std::ofstream(argv[1])<<json({{"ok",ok},{"checks",checks},{"packets",packets},{"steadyReceipts",300},{"steadyAllocations",allocations},{"realHardwareOrRtssReads",false},{"timingOrCpuPercentageMeasured",false}}).dump(2)<<'\n';return ok?0:1;
}
