// Single-thread local cost fixture. OS calls read only this thread's counters
// and process-name snapshots; no product, Host, service or device entry point.
#include "common.hpp"
#include <TlHelp32.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
#error R124 primary comparison must be diagnostic-off
#endif
static volatile std::uint64_t observableSink=0;
static std::array<WorkItem,64> work;
static unsigned checks=0;
static void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
using Step=bool(*)(const WorkItem*,Output*);
static void makeWork(){
 for(unsigned i=0;i<work.size();++i){auto& w=work[i];
  if(i%4!=0)w.pad={static_cast<WORD>((i*977u)^0x8123u),static_cast<BYTE>(i*17u),static_cast<BYTE>(i*31u),static_cast<SHORT>(i*1009u),static_cast<SHORT>(-static_cast<int>(i*787u)),static_cast<SHORT>(i*317u),static_cast<SHORT>(-static_cast<int>(i*557u))};
  w.proof={0,10000+i,10001+i,17,23,31,41};
 }
}
static bool samePad(const XINPUT_GAMEPAD& a,const XINPUT_GAMEPAD& b){return a.wButtons==b.wButtons&&a.bLeftTrigger==b.bLeftTrigger&&a.bRightTrigger==b.bRightTrigger&&a.sThumbLX==b.sThumbLX&&a.sThumbLY==b.sThumbLY&&a.sThumbRX==b.sThumbRX&&a.sThumbRY==b.sThumbRY;}
static void validate(){
 baselineApi.reset();currentApi.reset();Output a,b;
 for(unsigned i=0;i<work.size();++i){
  require(baselineApi.pipeline(&work[i],&a),"baseline pipeline success");require(currentApi.pipeline(&work[i],&b),"current pipeline success");
  require(samePad(a.sample.pad,work[i].pad)&&samePad(a.sample.pad,b.sample.pad),"identical seven-field business output");
  require(b.producer.sequence==i+1&&b.sample.readAttemptSequence==i+1,"producer ordinal returned and applied");
  require(b.sample.readStartedTick==work[i].proof.startedTick&&b.sample.readCompletedTick==work[i].proof.completedTick,"producer read ticks applied");
  require(b.sample.readPowerGeneration==17&&b.sample.readTopologyGeneration==41&&b.sample.readMsiModeGeneration==23&&b.sample.readMsiSourceRevision==31&&b.sample.readContextStable,"matching actual apply path");
 }
 require(baselineApi.counts().seqlock==128&&currentApi.counts().seqlock==128&&currentApi.counts().producer==64,"actual atomic progress after validation");
}
static std::uint64_t ft(const FILETIME& t){return (static_cast<std::uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
static std::uint64_t cpu100ns(){FILETIME c,e,k,u;require(GetThreadTimes(GetCurrentThread(),&c,&e,&k,&u)!=0,"GetThreadTimes failed");return ft(k)+ft(u);}
static std::uint64_t cycles(){ULONG64 n=0;require(QueryThreadCycleTime(GetCurrentThread(),&n)!=0,"QueryThreadCycleTime failed");return n;}
static json compilers(){
 const wchar_t* names[]={L"cl.exe",L"c1xx.exe",L"c2.exe",L"link.exe",L"msbuild.exe",L"clang.exe",L"clang++.exe",L"clang-cl.exe",L"gcc.exe",L"g++.exe",L"ninja.exe",L"dotnet.exe"};
 json found=json::array();HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
 if(h==INVALID_HANDLE_VALUE)return json{{"snapshotError",GetLastError()}};
 PROCESSENTRY32W p{};p.dwSize=sizeof(p);
 if(Process32FirstW(h,&p))do{for(const auto name:names)if(_wcsicmp(name,p.szExeFile)==0){std::string s;for(auto c=p.szExeFile;*c;++c)s.push_back(static_cast<char>(*c));found.push_back({{"pid",p.th32ProcessID},{"name",s}});break;}}while(Process32NextW(h,&p));
 CloseHandle(h);return found;
}
static std::uint64_t loop(Step step,std::uint64_t n,Output& o,bool& allOk){
 std::uint64_t acc=0x9e3779b97f4a7c15ULL;
 for(std::uint64_t i=0;i<n;++i){const auto& w=work[i&63];const bool ok=step(&w,&o);allOk&=ok;
  // Same accumulator on both variants. The opaque separate-TU operation call
  // also makes every output/proof store observable to a potential caller.
  acc=(acc<<7)|(acc>>57);
  acc^=static_cast<std::uint64_t>(w.pad.wButtons)+o.sample.pad.wButtons+(static_cast<std::uint64_t>(o.sample.pad.bLeftTrigger)<<16)+(static_cast<std::uint64_t>(o.sample.pad.bRightTrigger)<<24);
  acc+=static_cast<std::uint16_t>(o.sample.pad.sThumbLX)+3ULL*static_cast<std::uint16_t>(o.sample.pad.sThumbLY)+5ULL*static_cast<std::uint16_t>(o.sample.pad.sThumbRX)+7ULL*static_cast<std::uint16_t>(o.sample.pad.sThumbRY)+static_cast<unsigned>(ok);
 }
 return acc;
}
static void seed(const BenchApi& api,Output& o){api.reset();require(api.pipeline(&work[0],&o),"seed pipeline failed");}
static void warm(const BenchApi& api,Step step){Output o;seed(api,o);bool ok=true;observableSink=loop(step,5000,o,ok);require(ok,"warmup failed");}
static json measure(const BenchApi& api,Step step,const char* stage,std::uint64_t n,int pair,int order,bool calibration){
 Output o;seed(api,o);const auto before=api.counts();const auto beforeCompiler=compilers();const auto coreBefore=GetCurrentProcessorNumber();
 LARGE_INTEGER frequency,start,end;require(QueryPerformanceFrequency(&frequency)!=0,"QPC frequency unavailable");
 const auto cycle0=cycles(),cpu0=cpu100ns();QueryPerformanceCounter(&start);
 bool ok=true;const auto digest=loop(step,n,o,ok);
 QueryPerformanceCounter(&end);const auto cpu1=cpu100ns(),cycle1=cycles();const auto coreAfter=GetCurrentProcessorNumber();
 observableSink=digest;
 const auto after=api.counts();const auto afterCompiler=compilers();
 const double wallNs=static_cast<double>(end.QuadPart-start.QuadPart)*1.0e9/static_cast<double>(frequency.QuadPart);
 require(ok,"a measured operation failed");require(cycle1>=cycle0&&cpu1>=cpu0,"non-monotonic counters");require(wallNs<1.0e9,"batch exceeded the one-second bound; do not accept these measurements");
 const bool writes=std::string(stage)=="publish"||std::string(stage)=="pipeline";
 require(after.seqlock-before.seqlock==(writes?2*n:0),"production seqlock progress mismatch");
 if(&api==&currentApi)require(after.producer-before.producer==(writes?n:0),"producer proof progress mismatch");
 return {{"stage",stage},{"variant",api.name},{"iterations",n},{"pair",pair},{"order",order},{"calibration",calibration},
  {"threadCycles",cycle1-cycle0},{"cyclesPerOperation",static_cast<double>(cycle1-cycle0)/n},{"threadCpuNs",(cpu1-cpu0)*100},{"wallNs",wallNs},{"wallNsPerOperation",wallNs/n},
  {"digest",std::to_string(digest)},{"seqlockBefore",before.seqlock},{"seqlockAfter",after.seqlock},{"producerBefore",before.producer},{"producerAfter",after.producer},
  {"observedProofSequence",o.producer.sequence},{"observedAppliedSequence",o.sample.readAttemptSequence},{"observedStable",o.sample.readContextStable},
  {"coreBefore",coreBefore},{"coreAfter",coreAfter},{"compilerProcessesBefore",beforeCompiler},{"compilerProcessesAfter",afterCompiler}};
}
int main(){try{
 makeWork();validate();json rows=json::array(),pilots=json::array();constexpr int pairs=12;
 struct Stage {const char* name;Step a,b;};
 const Stage stages[]={{"publish",baselineApi.publish,currentApi.publish},{"snapshot-with-requested-proof",baselineApi.snapshot,currentApi.snapshot},{"pipeline",baselineApi.pipeline,currentApi.pipeline}};
 for(const auto& stage:stages){warm(baselineApi,stage.a);warm(currentApi,stage.b);
  auto pa=measure(baselineApi,stage.a,stage.name,8192,-1,0,true),pb=measure(currentApi,stage.b,stage.name,8192,-1,1,true);pilots.push_back(pa);pilots.push_back(pb);
  const double per=std::max(pa["wallNsPerOperation"].get<double>(),pb["wallNsPerOperation"].get<double>());
  const auto n=static_cast<std::uint64_t>(std::clamp(20000000.0/std::max(per,1.0),4096.0,500000.0));
  for(int p=0;p<pairs;++p){json a,b;if(p%2==0){a=measure(baselineApi,stage.a,stage.name,n,p,0,false);b=measure(currentApi,stage.b,stage.name,n,p,1,false);}else{b=measure(currentApi,stage.b,stage.name,n,p,0,false);a=measure(baselineApi,stage.a,stage.name,n,p,1,false);}
   require(a["digest"]==b["digest"],"paired variants produced different observable business digests");rows.push_back(a);rows.push_back(b);
  }
 }
 warm(currentApi,currentApi.apply);auto pilot=measure(currentApi,currentApi.apply,"apply-current-only",8192,-1,0,true);pilots.push_back(pilot);
 const auto applyN=static_cast<std::uint64_t>(std::clamp(20000000.0/std::max(pilot["wallNsPerOperation"].get<double>(),1.0),4096.0,500000.0));
 for(int p=0;p<pairs;++p)rows.push_back(measure(currentApi,currentApi.apply,"apply-current-only",applyN,p,0,false));
 std::cout<<json({{"passed",true},{"diagnostic",false},{"checks",checks},{"pairCountPerABStage",pairs},{"warmupOperationsPerVariantStage",5000},{"batchTargetWallMs",20},{"batchHardAcceptanceBoundMs",1000},
  {"baselineFieldSubsetBytes",baselineApi.stateBytes},{"currentFieldSubsetBytes",currentApi.stateBytes},{"proofTypeBytes",sizeof(MsiCanonicalReadProof)},
  {"measurementThreadId",GetCurrentThreadId()},{"measurementProcessId",GetCurrentProcessId()},{"singleMeasurementThread",true},{"observableSink",std::to_string(observableSink)},
  {"calibration",pilots},{"batches",rows}}).dump(2)<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}