// Data-only fixture. No product entrypoint, source acquisition, calibration I/O,
// Host, device, driver, service or WebView methods are executed.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "windows_source_view.h"
#include "msi_motion_capabilities.h"
#include "hc_model_motion_catalog.h"
#include "hc_default_motion_catalog.h"
#include "hc_legacy_motion_catalog.h"
using json=nlohmann::json;
static thread_local bool countAlloc=false;
static thread_local std::uint64_t requests=0,requestedBytes=0;
void* operator new(std::size_t n){if(countAlloc){++requests;requestedBytes+=n;}if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
#include "metadata-extracted.h"
static unsigned checks=0,cases=0;
static std::vector<std::string> failures;
static void check(bool value,const char* reason){++checks;if(!value)failures.emplace_back(reason);}
static json metaValue(const InputCaptureSensorMeta& m){return {m.gyroDeviceId,m.accelDeviceId,m.gyroFamily,m.accelFamily,m.gyroHcLegacyFields,m.accelHcLegacyFields,m.gyroIntervalMs,m.accelIntervalMs,m.gyroPresent,m.accelPresent};}
static json value(const Projection& p){return {{"gyroProvider",p.gyroProvider},{"accelProvider",p.accelProvider},{"pair",p.pair},{"current",p.current},{"calibration",p.calibration},{"recipe",p.recipe},{"capability",p.capability}};}
struct Scene {
 std::string name;WindowsSensorView view;std::shared_ptr<FixtureSink> source;
 std::shared_ptr<std::atomic<std::uint64_t>> choice;std::shared_ptr<std::atomic<bool>> lease;
 bool hot=false,productionReachable=true;
};
static Scene scene(int kind){
 Scene s;s.name=std::array<const char*,8>{"sso-default","long-hid-default","unicode-long-default","missing-default","private-current-synthetic","private-retired-synthetic","retired-default","long-family-default"}[kind];
 s.source=std::make_shared<FixtureSink>();s.choice=std::make_shared<std::atomic<std::uint64_t>>(9);s.lease=std::make_shared<std::atomic<bool>>(true);
 auto& m=s.view.metadata;m.gyroFamily=m.accelFamily="winrt";m.gyroPresent=m.accelPresent=true;m.gyroIntervalMs=8;m.accelIntervalMs=4;
 m.gyroDeviceId="same";m.accelDeviceId="same";
 if(kind!=0&&kind!=3){std::string stem="\\\\?\\HID#VID_1234&PID_5678#"+std::string(kind==2?256:48,'x');if(kind==2)stem+="设备/陀螺仪/传感器";m.gyroDeviceId=stem+"#{gyro-class}";m.accelDeviceId=stem+"#{accel-class}";}
 if(kind==7){m.gyroFamily=std::string(96,'w');m.accelFamily=std::string(80,'a');m.gyroHcLegacyFields=true;}
 s.view.capture=ymcc::windows_motion::retain(s.source,7);
 const auto choice=ymcc::windows_motion::Stamp{s.choice,9};
 s.view.privateSource=kind==4||kind==5;s.productionReachable=!s.view.privateSource;
 s.view.processingOwnerAccepted=s.view.privateSource;
 s.view.submission=ymcc::windows_motion::guarded(s.view.capture,choice,s.view.privateSource?s.lease:std::shared_ptr<const std::atomic<bool>>{});
 s.view.choice=choice;s.view.coherent=true;
 for(auto& part:s.view.parts){part.usable=true;part.generation=7;part.sequence=2;part.receipt=100;part.timestamp100ns=1000;}
 if(kind==3){m={};s.view.coherent=false;for(auto& part:s.view.parts)part.usable=false;}
 if(kind==5)s.lease->store(false);if(kind==6)s.source->activeBindingGeneration.store(8);
 s.hot=s.productionReachable&&kind!=0&&kind!=3;
 return s;
}
static void context(const Scene& s){
 g_inputCaptureSink=s.source;const auto& r=ymcc::hc_model_motion::recipes[0];
 g_machineIdentity={std::string(r.deviceClass),"fixture-data",std::string(r.gyroMatrixId),std::string(r.accelMatrixId)};
 g_hcMotionSelector={std::string(r.gyroMatrixId),std::string(r.accelMatrixId),std::string(r.sourceSha256)};
}
static void correctness(){
 for(int k=0;k<8;++k)for(unsigned flags=0;flags<64;++flags){
  ++cases;auto s=scene(k);context(s);g_realStickTest.enabled=(flags&1)!=0;g_realStickTest.bound=(flags&2)!=0;
  Inputs f;f.live=(flags&4)!=0;f.detail=(flags&8)!=0;f.wanted=(flags&16)!=0;f.finite=(flags&32)!=0;
  const auto original=metaValue(s.view.metadata);g_sensorMeta={};g_sensorMeta.gyroDeviceId="GLOBAL-MUST-NOT-BE-USED";
  auto old=baseline(s.view,f);auto now=candidate(s.view,f);
  check(value(old)==value(now),"source-extracted complete local projection/consumption");check(metaValue(s.view.metadata)==original,"frame metadata unchanged");
  check(!t_windowsProcessingMeta&&!t_windowsProcessingStamp&&!t_windowsProcessingView,"scope restored after frame");
  const auto previous=value(now);s.view.metadata.gyroDeviceId="NEXT-FRAME";s.view.metadata.accelFamily="next-family";g_sensorMeta=s.view.metadata;
  auto later=candidate(s.view,f);(void)later;check(value(now)==previous,"owned provider/JSON payload survives later frame/source changes");
 }
 // Actual getter fallback remains untouched; optimization only applies under the actual scope.
 ++cases;g_sensorMeta=scene(2).view.metadata;check(metaValue(inputCaptureSensorMetaSnapshot())==metaValue(g_sensorMeta),"unscoped original getter fallback");
 auto outer=scene(1),inner=scene(2);context(outer);
 {
  ymcc::sensors::view::ProcessingScope<InputCaptureSensorMeta> scope(outer.view,t_windowsProcessingMeta,t_windowsProcessingStamp,&t_windowsProcessingView);
  check(t_windowsProcessingMeta==&outer.view.metadata&&t_windowsProcessingView==&outer.view,"actual scope pins exact same local metadata");
  const auto old=inputCaptureSensorMetaSnapshot();const auto& borrow=outer.view.metadata;
  g_sensorMeta={};check(metaValue(old)==metaValue(borrow),"global change is invisible to same-frame metadata");
  try {ymcc::sensors::view::ProcessingScope<InputCaptureSensorMeta> nested(inner.view,t_windowsProcessingMeta,t_windowsProcessingStamp,&t_windowsProcessingView);check(t_windowsProcessingMeta==&inner.view.metadata,"nested scope exact target");throw 7;}catch(int){}
  check(t_windowsProcessingMeta==&outer.view.metadata&&t_windowsProcessingView==&outer.view,"nested exceptional restore");
  outer.source->activeBindingGeneration.store(8);
  check(!ymcc::sensors::view::current(outer.view),"real atomic source retirement still visible");
  check(metaValue(old)==metaValue(borrow),"source retirement does not change local metadata");
  check(!inputCaptureWindowsSourceCalibrationAdmitted(true,true,true,true,true,outer.view.submission,7,borrow),"retired source cannot train through metadata borrow");
  // Deliberate alias mutation is a negative sentinel, NOT an allowed production operation.
  outer.view.metadata.gyroDeviceId="INTENTIONAL-MUTATION";
  check(metaValue(old)!=metaValue(borrow),"negative sentinel detects changed intermediate visible value");
  check(metaValue(inputCaptureSensorMetaSnapshot())!=metaValue(g_sensorMeta),"negative no-scope/global-source implementation would be detected");
 }
 check(!t_windowsProcessingMeta&&!t_windowsProcessingStamp&&!t_windowsProcessingView,"outer scope restored");
 json out={{"passed",failures.empty()},{"cases",cases},{"checks",checks},{"failures",failures},{"productExecuted",false},{"privateCasesSyntheticOnly",true}};std::cout<<out.dump()<<'\n';
}
static volatile std::uint64_t sink=0;
static std::uint64_t digest(const Projection& p){return p.gyroProvider.size()+3*p.accelProvider.size()+7*p.recipe.size()+11*p.capability.size()+13*p.pair+17*p.current+19*p.calibration;}
struct Sample {double ns=0;std::uint64_t cycles=0,cpu100ns=0;bool cyclesKnown=false,cpuKnown=false;};
static std::uint64_t ft(const FILETIME& x){return (std::uint64_t(x.dwHighDateTime)<<32)|x.dwLowDateTime;}
static Sample arm(Scene& s,const Inputs& f,bool newer){
 ULONG64 c0=0,c1=0;FILETIME create{},exit{},k0{},u0{},k1{},u1{};
 const bool tk0=GetThreadTimes(GetCurrentThread(),&create,&exit,&k0,&u0)!=FALSE;
 const bool ck0=QueryThreadCycleTime(GetCurrentThread(),&c0)!=FALSE;
 const auto start=std::chrono::steady_clock::now();std::uint64_t d=0;
 for(int i=0;i<128;++i){auto out=newer?candidate(s.view,f):baseline(s.view,f);d+=digest(out);}
 const auto end=std::chrono::steady_clock::now();const bool ck1=QueryThreadCycleTime(GetCurrentThread(),&c1)!=FALSE;
 const bool tk1=GetThreadTimes(GetCurrentThread(),&create,&exit,&k1,&u1)!=FALSE;sink=d;
 Sample r;r.ns=std::chrono::duration<double,std::nano>(end-start).count()/128.;r.cyclesKnown=ck0&&ck1&&c1>=c0;r.cycles=r.cyclesKnown?c1-c0:0;
 r.cpuKnown=tk0&&tk1&&ft(k1)+ft(u1)>=ft(k0)+ft(u0);r.cpu100ns=r.cpuKnown?ft(k1)+ft(u1)-ft(k0)-ft(u0):0;return r;
}
static double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
static json sampleValue(const Sample& s){return {{"nsPerProjection",s.ns},{"cycles",s.cyclesKnown?json(s.cycles):json(nullptr)},{"threadCpu100ns",s.cpuKnown?json(s.cpu100ns):json(nullptr)}};}
static void performance(){
 json rows=json::array();bool accepted=true;
 for(int campaign=0;campaign<2;++campaign)for(int k=0;k<8;++k)for(bool detail:{false,true}){
  auto s=scene(k);context(s);g_realStickTest={true,true};Inputs f;f.detail=detail;
  for(int i=0;i<16;++i){auto a=baseline(s.view,f);auto b=candidate(s.view,f);sink=digest(a)+digest(b);}
  std::uint64_t counts[2]{},bytes[2]{};
  for(int newer=0;newer<2;++newer){requests=requestedBytes=0;countAlloc=true;for(int i=0;i<8;++i){auto out=newer?candidate(s.view,f):baseline(s.view,f);sink=digest(out);}countAlloc=false;counts[newer]=requests/8;bytes[newer]=requestedBytes/8;}
  std::vector<double> elapsedRatios,cycleRatios,oldNs,newNs;unsigned faster=0;bool known=true;json pairs=json::array();
  for(int pair=0;pair<11;++pair){Sample a,b;if((pair+campaign)%2){b=arm(s,f,true);a=arm(s,f,false);}else{a=arm(s,f,false);b=arm(s,f,true);}pairs.push_back({{"order",(pair+campaign)%2?"BA":"AB"},{"baseline",sampleValue(a)},{"candidate",sampleValue(b)}});oldNs.push_back(a.ns);newNs.push_back(b.ns);elapsedRatios.push_back(b.ns/a.ns);faster+=b.ns<a.ns;known=known&&a.cyclesKnown&&b.cyclesKnown&&a.cycles!=0;if(a.cyclesKnown&&b.cyclesKnown&&a.cycles)cycleRatios.push_back(double(b.cycles)/a.cycles);}
  const double wall=median(elapsedRatios),cycle=known?median(cycleRatios):0;
  const bool allocations=s.hot?(counts[1]<counts[0]&&bytes[1]<bytes[0]):(counts[1]<=counts[0]&&bytes[1]<=bytes[0]);
  const bool timing=s.hot?(known&&wall<=0.95&&cycle<=0.95&&faster>=9):(known&&((wall<=1.05)||(median(newNs)-median(oldNs)<=100))&&cycle<=1.05);
  const bool pass=allocations&&timing;accepted=accepted&&pass;
  rows.push_back({{"campaign",campaign+1},{"scene",s.name},{"detailReceipt",detail},{"productionReachable",s.productionReachable},{"hotLongMetadata",s.hot},{"accepted",pass},{"allocationPassed",allocations},{"cpuTimePassed",timing},{"baselineRequests",counts[0]},{"candidateRequests",counts[1]},{"baselineBytes",bytes[0]},{"candidateBytes",bytes[1]},{"pairedMedianElapsedRatio",wall},{"pairedMedianCycleRatio",known?json(cycle):json(nullptr)},{"fasterPairs",faster},{"pairs",pairs}});
 }
 std::cout<<json{{"passed",accepted},{"rows",rows},{"scope","source-extracted local metadata projection and consumers; not whole Native or whole frame"},{"privateCasesSyntheticOnly",true}}.dump()<<'\n';
}
int main(int argc,char** argv){try{if(argc!=2)return 2;const std::string mode=argv[1];if(mode=="--correctness"){correctness();return failures.empty()?0:1;}if(mode=="--performance"){performance();return 0;}return 2;}catch(const std::exception& e){std::cout<<json{{"passed",false},{"exception",e.what()}}.dump()<<'\n';return 3;}}
