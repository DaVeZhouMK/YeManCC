// Bounded measurements of actual touch handlers with existing offline API stubs.
// No device, pointer injection, HWND creation, program settings or process priority change.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static ULONGLONG cpuVirtualTick=1000;
static ULONGLONG WINAPI cpuTick(){return cpuVirtualTick;}
#define GetTickCount64 cpuTick
#define wmain embeddedButtonContractTest
#include "screen_touchpads_button_cpu_simulation.cpp"
#undef wmain
#undef GetTickCount64
#include <chrono>
#include <string>
struct Result {double wallUs=0,threadCpuUs=0;unsigned long long sum=0,cancels=0;unsigned pulses=0,dispatches=0;};
static unsigned long long threadTime(){FILETIME a{},b{},k{},u{};if(!GetThreadTimes(GetCurrentThread(),&a,&b,&k,&u))return 0;return ((ULONGLONG)k.dwHighDateTime<<32)+k.dwLowDateTime+((ULONGLONG)u.dwHighDateTime<<32)+u.dwLowDateTime;}
static Result measure(const std::string& kind,int count){
 reset();bool owned=true;cpuVirtualTick=1000;feedbackOnlyForegroundFixture=nullptr;
 const bool buttonPath=kind.rfind("button-",0)==0,off=kind.find("-off")!=std::string::npos;
 core.config.touchFeedback=off?TouchFeedbackMode::Off:TouchFeedbackMode::Medium;
 const HWND h=reinterpret_cast<HWND>(uintptr_t{2});
 if(buttonPath)handleButtonPointer(h,1,WM_POINTERDOWN,501,30,20);
 else {
  feedbackOnlyForegroundFixture=&owned;dryRun=true;core.config.left=core.config.right=Mode::Mouse;
  if(kind.find("blocked")!=std::string::npos){owned=false;core.config.left=core.config.right=Mode::Deck;core.nativeOutputBlocked=true;}
  sizes={SIZE{200,200},SIZE{200,200}};handlePointer(nullptr,0,WM_POINTERDOWN,501,20,20,0);
 }
 unsigned long long sum=0;const auto g=standaloneGeneration.load();
 const auto cpu=threadTime();const auto start=std::chrono::steady_clock::now();
 for(int i=0;i<count;++i){cpuVirtualTick+=8;
  if(buttonPath){handleButtonPointer(h,1,WM_POINTERUPDATE,501,30+(i%3),20);sum+=buttonFeedbackContacts[1]+(unsigned)buttonFeedbackPressed[1]+screenButtonMask.load(std::memory_order_relaxed);}
  else {const float x=(i%2)?20.f:150.f;handlePointer(nullptr,0,WM_POINTERUPDATE,501,x,20,0);const auto& c=feedbackOnlyContacts.contacts[0];sum+=(unsigned)c.x+c.id+(unsigned)c.active;}
 }
 Result r;r.wallUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();r.threadCpuUs=(threadTime()-cpu)/10.0;r.sum=sum;r.cancels=standaloneGeneration.load()-g;r.pulses=pulses;r.dispatches=sinkCalls;
 if(buttonPath)handleButtonPointer(h,1,WM_POINTERUP,501,30,20);else handlePointer(nullptr,0,WM_POINTERUP,501,20,20,0);
 feedbackOnlyForegroundFixture=nullptr;setTouchHapticPulseSink(nullptr);return r;
}
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;const char* kinds[]={"button-owned-medium","button-owned-off","pad-owned-off","pad-owned-medium","pad-blocked-off"};
 constexpr int count=200000,rounds=7;bool ok=true;Json rows=Json::array();
 for(const auto* kind:kinds){measure(kind,10000);Json samples=Json::array();std::array<double,rounds> wall{};Result first{};bool stable=true;
  for(int i=0;i<rounds;++i){const auto r=measure(kind,count);wall[i]=r.wallUs;if(i==0)first=r;else stable&=r.sum==first.sum&&r.cancels==first.cancels&&r.pulses==first.pulses&&r.dispatches==first.dispatches;
   samples.push_back({{"wallUs",r.wallUs},{"threadCpuUs",r.threadCpuUs}});}
  std::sort(wall.begin(),wall.end());ok&=stable;
  rows.push_back({{"workload",kind},{"iterations",count},{"rounds",rounds},{"medianWallUs",wall[rounds/2]},{"minWallUs",wall.front()},{"maxWallUs",wall.back()},{"medianUsPerEvent",wall[rounds/2]/count},{"checksum",first.sum},{"cancellations",first.cancels},{"pulses",first.pulses},{"sinkCalls",first.dispatches},{"stable",stable},{"samples",samples}});
 }
 const Json report={{"ok",ok&&injectedCalls==0},{"scope","actual handlers using offline API stubs; isolated benchmark, not application CPU percentage"},{"realInputInjected",0},{"realWindowsCreated",0},{"hardwareWrites",0},{"rows",rows},{"notMeasured",Json::array({"Windows input delivery","GDI render/compositor","BUS contention","IPC and hardware rumble"})}};
 std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<report.dump(2);std::cout<<"TOUCH_CPU_MEASURED workloads="<<rows.size()<<" stable="<<ok<<"\n";return ok&&injectedCalls==0&&out.good()?0:3;
}
