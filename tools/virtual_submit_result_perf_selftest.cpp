#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <new>
#include <random>
#include <string>
#include <vector>
#include "json.hpp"
#include "input_frame_submission.h"
using json=nlohmann::json;
#define NOINLINE __declspec(noinline)
static thread_local bool counting=false;
static thread_local unsigned long long allocationCount=0,allocationBytes=0;
static void account(size_t n){if(counting){++allocationCount;allocationBytes+=n;}}
void* operator new(size_t n){void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();account(n);return p;}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t a){void* p=_aligned_malloc(n?n:1,size_t(a));if(!p)throw std::bad_alloc();account(n);return p;}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{_aligned_free(p);}void operator delete[](void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void* p,size_t,std::align_val_t)noexcept{_aligned_free(p);}void operator delete[](void* p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
#include "generated.hpp"
static unsigned assertions=0,cases=0;
static std::vector<std::string> failures;
static void check(bool ok,const char* s){++assertions;if(!ok)failures.push_back(s);}
static bool exact(const json& a,const json& b){
 if(a.type()!=b.type()||a.size()!=b.size())return false;
 if(a.is_number_float())return std::bit_cast<std::uint64_t>(a.get<double>())==std::bit_cast<std::uint64_t>(b.get<double>());
 if(a.is_object()||a.is_array()){auto x=a.begin(),y=b.begin();for(;x!=a.end();++x,++y)if((a.is_object()&&x.key()!=y.key())||!exact(x.value(),y.value()))return false;return true;}
 return a==b;
}
static std::string dump(const json& j){try{return "ok:"+j.dump();}catch(const json::exception& e){return "error:"+std::to_string(e.id);}}
static void shapeContract(const json& value){
 check(value.is_object(),"actual submit result is a JSON object");
 if(value.is_object()) {
  const auto nestedObject=[&](const char* key){const auto at=value.find(key);return at!=value.end()&&at->is_object();};
  check(nestedObject("frameProof"),"actual frameProof is an object");
  check(nestedObject("rightStick"),"actual rightStick is an object");
  check(nestedObject("gyroDps"),"actual gyroDps is an object");
 }
}
static void parity(const Input& v){++cases;
#ifdef YMCC_RESULT_SHAPE_ONLY
 // One actual builder execution. Shape-only has no independent reference implementation.
 auto current=candidate(v);shapeContract(current);
#else
 auto a=baseline(v);const auto pc=personaCalls,qc=proofCalls,fc=g_commonFrameEvidenceFailures;auto b=candidate(v);
 shapeContract(b);
 check(exact(a,b),"type/number-bit/object parity");check(dump(a)==dump(b),"serialized byte/exception parity");
 check(pc==personaCalls&&qc==proofCalls&&fc==g_commonFrameEvidenceFailures,"helper/failure-count parity");
#endif
}

static unsigned long long sink=0;
using Builder=json(*)(const Input&);
static double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
static json measure(){
 Input a,b;a.receipt={{"runId","real-diagnostic-fixture"},{"epoch",3},{"hostAckAccepted",true},{"steamConsumptionProven",false}};b=a;b.persona="dualsense";b.accepted=false;b.semanticNeutral=true;
 std::array<Input,3> table{Input{},a,b};
 json allocations=json::array();for(const auto& v:table){json row;for(const auto fn:{baseline,candidate}){allocationCount=allocationBytes=0;counting=true;{auto j=fn(v);sink+=j.size();}counting=false;const char* n=fn==baseline?"baseline":"candidate";row[n]={{"requests",allocationCount},{"bytes",allocationBytes}};}check(row["candidate"]["requests"]<row["baseline"]["requests"],"candidate fewer allocations");check(row["candidate"]["bytes"]<row["baseline"]["bytes"],"candidate fewer requested bytes");allocations.push_back(row);}
 for(unsigned i=0;i<256;++i){auto a=baseline(table[i%3]);auto b=candidate(table[i%3]);sink+=a.size()+b.size();}
 constexpr unsigned frames=2048,rounds=9;json batches=json::array();std::vector<double> ratios,oldTimes,newTimes,cycleRatios;
 unsigned long long totalOldCpu100ns=0,totalNewCpu100ns=0;unsigned knownCpuPairs=0;
 const auto cpuTicks=[](const FILETIME& f){ULARGE_INTEGER n{};n.LowPart=f.dwLowDateTime;n.HighPart=f.dwHighDateTime;return n.QuadPart;};
 for(unsigned round=0;round<rounds;++round){
  double oldNs=0,newNs=0;unsigned long long oldCycles=0,newCycles=0,oldCpu=0,newCpu=0;bool bothCycles=true,bothCpu=true;
  for(unsigned order=0;order<2;++order){
   bool useCandidate=((round+order)&1)!=0;Builder fn=useCandidate?candidate:baseline;
   FILETIME created{},exited{},kernelStart{},userStart{},kernelEnd{},userEnd{};ULONG64 cycleStart=0,cycleEnd=0;
   const bool cpuStartKnown=GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelStart,&userStart)!=FALSE;
   const bool cycleStartKnown=QueryThreadCycleTime(GetCurrentThread(),&cycleStart)!=FALSE;
   auto start=std::chrono::steady_clock::now();
   for(unsigned i=0;i<frames;++i){auto j=fn(table[i%3]);sink+=j.size()+j.at("frameProof").at("sampleSequence").get<unsigned long long>();}
   auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/frames;
   const bool cycleEndKnown=QueryThreadCycleTime(GetCurrentThread(),&cycleEnd)!=FALSE;
   const bool cpuEndKnown=GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelEnd,&userEnd)!=FALSE;
   const bool cyclesKnown=cycleStartKnown&&cycleEndKnown&&cycleEnd>=cycleStart;
   const auto beforeCpu=cpuTicks(kernelStart)+cpuTicks(userStart),afterCpu=cpuTicks(kernelEnd)+cpuTicks(userEnd);
   const bool cpuKnown=cpuStartKnown&&cpuEndKnown&&afterCpu>=beforeCpu;
   bothCycles=bothCycles&&cyclesKnown;bothCpu=bothCpu&&cpuKnown;
   if(useCandidate){newNs=ns;newCycles=cyclesKnown?cycleEnd-cycleStart:0;newCpu=cpuKnown?afterCpu-beforeCpu:0;}
   else {oldNs=ns;oldCycles=cyclesKnown?cycleEnd-cycleStart:0;oldCpu=cpuKnown?afterCpu-beforeCpu:0;}
  }
  oldTimes.push_back(oldNs);newTimes.push_back(newNs);ratios.push_back(newNs/oldNs);
  if(bothCycles&&oldCycles)cycleRatios.push_back(double(newCycles)/oldCycles);
  if(bothCpu){++knownCpuPairs;totalOldCpu100ns+=oldCpu;totalNewCpu100ns+=newCpu;}
  json row={{"baselineNsPerFrame",oldNs},{"candidateNsPerFrame",newNs},{"ratio",newNs/oldNs},
   {"baselineThreadCyclesPerFrame",bothCycles?json(double(oldCycles)/frames):json(nullptr)},
   {"candidateThreadCyclesPerFrame",bothCycles?json(double(newCycles)/frames):json(nullptr)},
   {"baselineThreadCpuUsPerFrame",bothCpu?json(double(oldCpu)/10/frames):json(nullptr)},
   {"candidateThreadCpuUsPerFrame",bothCpu?json(double(newCpu)/10/frames):json(nullptr)}};
  batches.push_back(std::move(row));
 }
 return {{"allocations",allocations},{"batches",batches},{"medianBaselineNsPerFrame",median(oldTimes)},{"medianCandidateNsPerFrame",median(newTimes)},
  {"medianPairedRatio",median(ratios)},{"medianPairedCycleRatio",cycleRatios.empty()?json(nullptr):json(median(cycleRatios))},
  {"cpuKnownPairs",knownCpuPairs},{"baselineThreadCpuUsPerFrame",knownCpuPairs?json(double(totalOldCpu100ns)/10/(knownCpuPairs*frames)):json(nullptr)},
  {"candidateThreadCpuUsPerFrame",knownCpuPairs?json(double(totalNewCpu100ns)/10/(knownCpuPairs*frames)):json(nullptr)},
  {"cpuWindowIncludes", "two clock/cycle reads per batch; no cost subtraction; GetThreadTimes may be quantized"},
  {"rounds",rounds},{"framesPerRound",frames}};
}
int main(){
 Input v;
 for(unsigned mask=0;mask<256;++mask){v=Input{};v.accepted=mask&1;v.hostActive=mask&2;v.firstFrame=mask&4;v.targetLeft=mask&8;v.semanticNeutral=mask&16;v.useMotion=mask&32;v.analogAdmitted=mask&64;v.effUseMotion=mask&128;v.frameSubmission.rejected=mask%3==0;v.frameSubmission.sourceNeutralized=mask%5==0;v.frameSubmission.reason=ymcc::input::frameSubmission::Verdict(mask%7);parity(v);}
 for(const auto& persona:std::vector<std::string>{"xbox360","steamdeck","dualsense","微星😀","",std::string(1024,'z'),std::string("a\0b",3),std::string("\xc3\x28",2)}){v=Input{};v.persona=persona;v.receipt={{"persona",persona},{"sampleSequence",0xffffffffffffffffULL}};parity(v);}
 for(double x:{0.,-0.,1.,-1.,32767.,-32768.,std::numeric_limits<double>::max(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){v=Input{};v.cgx=v.cgy=v.cgz=v.outX=v.outY=v.baseX=v.baseY=v.contributionX=v.contributionY=v.stickNorm=v.weightFactor=v.gyroWeight=x;v.motionContribution.shortX=x;parity(v);}
 std::mt19937_64 rng(20261018);for(unsigned i=0;i<1000;++i){v=Input{};v.sampleSequence=rng();v.cgx=double(int(rng()%32768)-16384)/11;v.lx=float(int(rng()%65536)-32768)/32768;v.motionContribution.shortX=v.cgx;v.motionContribution.mapped=rng()&1;v.motionContribution.triggered=rng()&1;v.motionContribution.velocityMode=rng()&1;if(i%2)v.receipt={{"runId","甲"},{"epoch",rng()},{"hostAckAccepted",bool(rng()&1)}};parity(v);}
 auto before=candidate(Input{}),after=candidate(Input{});
 if(before.is_object()&&after.is_object()) {
  before["layoutManager"]["physicalStick"]["x"]=991;check(after["layoutManager"]["physicalStick"]["x"]!=991,"frame ownership is independent");
 }
 json perf=nullptr;
#ifndef YMCC_RESULT_SHAPE_ONLY

 perf=measure();check(perf["medianPairedRatio"].get<double>()<.95,"paired median timing improves at least five percent");check(!perf["medianPairedCycleRatio"].is_null()&&perf["medianPairedCycleRatio"].get<double>()<.95,"paired CPU cycles improve at least five percent");
#endif
 json result={{"passed",failures.empty()},{"cases",cases},{"assertions",assertions},{"failures",failures},{"performance",perf},{"sink",sink},{"realDeviceAccess",false},{"entireSubmitPadExecuted",false},{"allocationCountingSeparateFromTiming",true}};
 std::cout<<result.dump(2)<<std::endl;return failures.empty()?0:1;
}
