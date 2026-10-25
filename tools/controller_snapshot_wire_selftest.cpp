#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
static thread_local bool trackAlloc=false;
static thread_local uint64_t allocationRequests=0,allocationBytes=0;
static void allocated(size_t n){if(trackAlloc){++allocationRequests;allocationBytes+=n;}}
void* operator new(size_t n){void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();allocated(n);return p;}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t a){void* p=_aligned_malloc(n?n:1,static_cast<size_t>(a));if(!p)throw std::bad_alloc();allocated(n);return p;}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void* p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
#include "baseline.inc"
#include "candidate.inc"
#include "measured.inc"
struct Trace{
 std::vector<std::pair<int,std::wstring>> posts;
 std::vector<int> resolutions;
 std::vector<std::string> scopes,feedback,diagnostics;
 unsigned gates=0,conversions=0,dumps=0,postCalls=0;
 uint64_t owned=0,checks=0,failures=0;
 std::string error;
};
static size_t checks=0,cases=0;
static std::vector<std::string> failures;
static void check(bool yes,const std::string& label){++checks;if(!yes&&failures.size()<200)failures.push_back(label);}
static Trace emit(bool useCandidate,const std::string& ev,const json& data){
 Trace t;
 try{if(useCandidate){if(ev=="gyro.telemetry")candidate::ipc_emit_owned_gyro_telemetry(ev,data);else candidate::ipc_emit(ev,data);}else{if(ev=="gyro.telemetry")baseline::ipc_emit_owned_gyro_telemetry(ev,data);else baseline::ipc_emit(ev,data);}}
 catch(const json::exception& e){t.error=std::to_string(e.id)+":"+e.what();}
 catch(const std::exception& e){t.error=e.what();}
 if(useCandidate){t.posts=candidate::posts;t.resolutions=candidate::resolutions;t.scopes=candidate::scopeEvents;t.feedback=candidate::feedbackEvents;t.diagnostics=candidate::diagnosticEvents;t.gates=candidate::gateCalls;t.conversions=candidate::conversions;t.dumps=candidate::deliveryDumps;t.postCalls=candidate::postCalls;t.owned=candidate::g_c17aOwnedEvents.load();t.checks=candidate::g_c17aParityChecks.load();t.failures=candidate::g_c17aParityFailures.load();}
 else{t.posts=baseline::posts;t.resolutions=baseline::resolutions;t.scopes=baseline::scopeEvents;t.feedback=baseline::feedbackEvents;t.diagnostics=baseline::diagnosticEvents;t.gates=baseline::gateCalls;t.conversions=baseline::conversions;t.dumps=baseline::deliveryDumps;t.postCalls=baseline::postCalls;t.owned=baseline::g_c17aOwnedEvents.load();t.checks=baseline::g_c17aParityChecks.load();t.failures=baseline::g_c17aParityFailures.load();}
 return t;
}
static void compare(const std::string& ev,const json& data,int targets,bool logging,bool deadOnly=false,unsigned mask=16,int postThrow=-1,bool failedHresult=false,int wideFailure=-1){
 ++cases;baseline::reset(targets,logging,deadOnly);candidate::reset(targets,logging,deadOnly);
 baseline::throwAtPost=postThrow;candidate::throwAtPost=postThrow;
 baseline::failConversionAt=wideFailure;candidate::failConversionAt=wideFailure;
 if(mask<16){baseline::scriptedGate=candidate::scriptedGate=true;baseline::gateMask=candidate::gateMask=mask;}
 if(failedHresult&&targets>0){baseline::g_view->result=E_FAIL;candidate::g_view->result=E_FAIL;}
 const auto before=data;
 const auto a=emit(false,ev,data),b=emit(true,ev,data);
 check(a.posts==b.posts,wideFailure<0?"receiver-wire-order/freshness":"conversion-failure-retry");check(a.resolutions==b.resolutions,"live-target-resolution-order");
 check(a.scopes==b.scopes&&a.feedback==b.feedback&&a.diagnostics==b.diagnostics,"observers-and-other-events");
 check(a.error==b.error,"error-point");check(a.gates==b.gates&&a.owned==b.owned&&a.checks==b.checks&&a.failures==b.failures,"diagnostic-gates-and-counters");
 check(a.postCalls==b.postCalls,"delivery-attempt-count");check(data==before,"caller-payload-preserved");
 if((ev=="gamepad.state"||ev=="gyro.telemetry")&&postThrow<0&&wideFailure<0&&b.error.empty()){
  check(b.conversions==(b.posts.empty()?0u:1u),"one-conversion-per-event");check(b.dumps==(b.posts.empty()?0u:1u),"one-delivery-serialization-per-event");
 }else if(ev!="gamepad.state"&&ev!="gyro.telemetry")check(a.conversions==b.conversions&&a.dumps==b.dumps,"other-event-work-unchanged");
}
static json payload(bool wide){
 json j=json::object();j["connected"]=true;j["sequence"]=7u;j["source"]="physical-current";j["axes"]={-0.0,0.125,-0.25,1.0};j["buttons"]=0x1000;
 for(int i=0;i<(wide?32:6);++i)j["field"+std::to_string(i)]={{"value",i},{"name",wide?std::string("手柄状态/☃/")+std::string(24,'x'):std::string("ready")}};
 return j;
}
static double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
static void directEmit(bool c,const std::string& ev,const json& j){if(c){if(ev=="gyro.telemetry")candidate_measured::ipc_emit_owned_gyro_telemetry(ev,j);else candidate_measured::ipc_emit(ev,j);}else{if(ev=="gyro.telemetry")baseline_measured::ipc_emit_owned_gyro_telemetry(ev,j);else baseline_measured::ipc_emit(ev,j);}}
static json measure(const std::string& ev,const json& j,int targets,bool logging){
 baseline_measured::reset(targets,logging);candidate_measured::reset(targets,logging);baseline_measured::observing=candidate_measured::observing=false;
 std::array<uint64_t,2> requests{},bytes{};
 for(int c=0;c<2;++c){allocationRequests=allocationBytes=0;trackAlloc=true;for(int n=0;n<8;++n)directEmit(c!=0,ev,j);trackAlloc=false;requests[static_cast<size_t>(c)]=allocationRequests/8;bytes[static_cast<size_t>(c)]=allocationBytes/8;}
 for(int n=0;n<16;++n){directEmit(false,ev,j);directEmit(true,ev,j);}
 std::vector<double> oldNs,newNs,ratios,cycleRatios;json pairs=json::array();int faster=0;
 constexpr int iterations=128;
 for(int pair=0;pair<11;++pair){std::array<double,2> times{};std::array<ULONG64,2> cycles{};bool known=true;
  for(int arm=0;arm<2;++arm){const int c=(pair+arm)&1;ULONG64 first=0,last=0;const bool firstOk=QueryThreadCycleTime(GetCurrentThread(),&first)!=FALSE;const auto begin=std::chrono::steady_clock::now();
   for(int n=0;n<iterations;++n)directEmit(c!=0,ev,j);
   const auto end=std::chrono::steady_clock::now();const bool lastOk=QueryThreadCycleTime(GetCurrentThread(),&last)!=FALSE;
   times[static_cast<size_t>(c)]=std::chrono::duration<double,std::nano>(end-begin).count()/iterations;
   if(!firstOk||!lastOk||last<first)known=false;else cycles[static_cast<size_t>(c)]=last-first;
  }
  oldNs.push_back(times[0]);newNs.push_back(times[1]);ratios.push_back(times[1]/times[0]);if(times[1]<times[0])++faster;
  if(known&&cycles[0])cycleRatios.push_back(static_cast<double>(cycles[1])/static_cast<double>(cycles[0]));
  pairs.push_back({{"baselineNs",times[0]},{"candidateNs",times[1]},{"cyclesKnown",known},{"baselineCycles",cycles[0]},{"candidateCycles",cycles[1]}});
 }
 const double ratio=median(ratios),a=median(oldNs),b=median(newNs);const bool hot=(ev=="gamepad.state"||ev=="gyro.telemetry")&&targets>1;
 const bool alloc=hot?(requests[1]<requests[0]&&bytes[1]<bytes[0]):(requests[1]<=requests[0]&&bytes[1]<=bytes[0]);
 const bool timing=hot?(ratio<=.95&&faster>=9&&cycleRatios.size()==11&&median(cycleRatios)<=.95):((ratio<=1.05)||(b-a<=100));
 return {{"event",ev},{"targets",targets},{"logging",logging},{"baselineMedianNs",a},{"candidateMedianNs",b},{"pairedMedianRatio",ratio},{"pairedMedianCycleRatio",cycleRatios.empty()?json(nullptr):json(median(cycleRatios))},{"fasterPairs",faster},{"baselineRequests",requests[0]},{"candidateRequests",requests[1]},{"baselineBytes",bytes[0]},{"candidateBytes",bytes[1]},{"hotMultitarget",hot},{"accepted",alloc&&timing},{"pairs",pairs}};
}
int main(int argc,char** argv){
 if(argc<2)return 2;bool correctnessOnly=false,negative=false,negativeConversion=false;for(int i=2;i<argc;++i){correctnessOnly|=std::string(argv[i])=="--correctness-only";negative|=std::string(argv[i])=="--expect-negative";negativeConversion|=std::string(argv[i])=="--expect-conversion-negative";}
 std::vector<json> inputs{payload(false),payload(true),json::object(),nullptr,42,json::array({1,2}),json{{"invalid",std::string("\xC0\x80",2)}},json{{"float",std::numeric_limits<double>::infinity()}}};
 for(const auto& ev:{"gamepad.state","gyro.telemetry","other.snapshot","power.suspending","power.resumed","power.sourceChanged","gamepad.brightness","gamepad.mouse-toggle"})
  for(const auto& j:inputs)for(int targets:{0,1,2,4})for(bool log:{false,true})for(bool dead:{false,true})compare(ev,j,targets,log,dead);
 for(unsigned mask=0;mask<16;++mask)for(int targets:{0,1,4})for(bool dead:{false,true})compare("gyro.telemetry",payload(false),targets,false,dead,mask);
 for(int postThrow:{1,2,4})for(const auto& ev:{"gamepad.state","gyro.telemetry"})compare(ev,payload(true),4,false,false,16,postThrow);
 for(const auto& ev:{"gamepad.state","gyro.telemetry","power.suspending"})compare(ev,payload(true),4,false,false,16,-1,true);
 for(int failedCall:{1,2})for(const auto& ev:{"gamepad.state","gyro.telemetry"})for(bool log:{false,true})compare(ev,payload(true),4,log,false,16,-1,false,failedCall);
 for(const auto& ev:{"gamepad.state","gyro.telemetry"})for(bool partial:{false,true}){
  baseline::reset(4,false);candidate::reset(4,false);
  if(partial){baseline::failConversionAt=candidate::failConversionAt=2;baseline::partialConversionFailure=candidate::partialConversionFailure=true;}
  else {baseline::failConversionCalls={1,3};candidate::failConversionCalls={1,3};}
  const auto a=emit(false,ev,payload(true)),b=emit(true,ev,payload(true));
  ++cases;check(a.posts==b.posts&&a.resolutions==b.resolutions&&a.error==b.error,"partial-or-repeated-conversion-failure-retry");
 }
 for(const auto& ev:{"gamepad.state","gyro.telemetry"})for(bool remove:{false,true}){
  baseline::reset(4,false);candidate::reset(4,false);
  baseline::postHook=[=](int id){if(id==0){if(remove)baseline::childStorage[0].view.reset();else baseline::childStorage[0].view=std::make_shared<baseline::ICoreWebView2>(baseline::ICoreWebView2{91});}};
  candidate::postHook=[=](int id){if(id==0){if(remove)candidate::childStorage[0].view.reset();else candidate::childStorage[0].view=std::make_shared<candidate::ICoreWebView2>(candidate::ICoreWebView2{91});}};
  const auto a=emit(false,ev,payload(false)),b=emit(true,ev,payload(false));
  ++cases;check(a.posts==b.posts&&a.resolutions==b.resolutions,"target-replaced-or-removed-during-broadcast");
 }
 // Independent consecutive-event assertion rejects accidental cross-frame reuse.
 baseline::reset(2,false);candidate::reset(2,false);
 const auto first=emit(true,"gamepad.state",json{{"sequence",100}});const auto second=emit(true,"gamepad.state",json{{"sequence",101}});
 const bool freshAcrossEvents=first.posts.size()==2&&second.posts.size()==4&&first.posts.front().second!=second.posts.back().second;
 check(freshAcrossEvents,"consecutive-frame-freshness");
 json rows=json::array();bool performanceAccepted=true;
 if(!correctnessOnly&&failures.empty())for(const auto& ev:{"gamepad.state","gyro.telemetry","other.snapshot"})for(bool wide:{false,true})for(int targets:{1,2,4})for(bool log:{false,true}){auto row=measure(ev,payload(wide),targets,log);row["shape"]=wide?"wide-unicode":"normal";performanceAccepted=performanceAccepted&&row["accepted"].get<bool>();rows.push_back(std::move(row));}
 const bool negativeDetected=(negative&&!freshAcrossEvents)||(negativeConversion&&std::find(failures.begin(),failures.end(),"conversion-failure-retry")!=failures.end());
 json result={{"correctnessPassed",failures.empty()},{"cases",cases},{"checks",checks},{"failures",failures},{"negativeControlDetected",negativeDetected},{"performanceAccepted",correctnessOnly?json(nullptr):json(performanceAccepted&&!rows.empty())},{"rows",rows},{"hardwareOrWebViewExecuted",false},{"baselineSink",baseline::sink},{"candidateSink",candidate::sink}};
 std::ofstream(argv[1],std::ios::binary)<<result.dump(2);std::cout<<"cases="<<cases<<" checks="<<checks<<" failures="<<failures.size()<<" rows="<<rows.size()<<"\n";
 return (negative||negativeConversion)?(negativeDetected?0:1):(failures.empty()?0:1);
}