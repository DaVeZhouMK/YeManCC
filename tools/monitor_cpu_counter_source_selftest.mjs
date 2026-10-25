// Actual Windows sampler and delta helper, deterministic API inputs and allocation
// counts. No live sensors, game/application control or CPU percentage/timing test.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_CPU_COUNTER_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/cpu-counter-source-pass-20261016').replaceAll('\\','/');fs.mkdirSync(out,{recursive:true});
const cut=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);assert(i>=0&&j>i,a);return s.slice(i,j)};
const baselineSource=fs.existsSync(out+'/before/native/main.cpp')?out+'/before/native/main.cpp':root+'/tools/fixtures/monitor_cpu_counter_before_20261016.inc';
const baselineHeader=fs.existsSync(out+'/before/native/monitor_cpu_deltas.h')?out+'/before/native/monitor_cpu_deltas.h':root+'/tools/fixtures/monitor_cpu_counter_deltas_before_20261016.h';
const source=fs.readFileSync('native/main.cpp','utf8'),before=fs.readFileSync(baselineSource,'utf8'),header=fs.readFileSync('native/monitor_cpu_deltas.h');
const sampler=s=>cut(s,'static double nativeCpuUsagePct(','static bool nativeBatteryStatus('),lane=s=>cut(s,'        json coreUsage = json::array(); bool cpuUsageValid = false;','        const auto fpsLimit =');
const beforeSampler=sampler(before),afterSampler=sampler(source),beforeLane=lane(before),afterLane=lane(source);
assert.equal(afterLane,beforeLane,'Windows collection remains in the existing top-demand lane');
const code=String.raw`
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "__ROOT__/deps/json/json.hpp"
using json=nlohmann::json;
static bool tracking=false;static std::size_t allocations=0;
void* operator new(std::size_t n){if(tracking)++allocations;auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p) noexcept{std::free(p);}void operator delete[](void*p) noexcept{std::free(p);}
void operator delete(void*p,std::size_t) noexcept{std::free(p);}void operator delete[](void*p,std::size_t) noexcept{std::free(p);}
struct RawTimes{std::int64_t idle=0,kernel=0,user=0;};
static std::vector<RawTimes> ntRows;static RawTimes aggregate;static bool ntOK=true,systemOK=true;static long reportedBytes=-1;static ULONGLONG tick=0;static unsigned ntCalls=0,systemCalls=0,clockCalls=0;
static LONG NTAPI fakeQuery(ULONG code,PVOID data,ULONG size,PULONG written){
 ++ntCalls;if(code!=8)throw std::runtime_error("different Windows API class");if(!ntOK)return -1;
 struct Row{LARGE_INTEGER idle,kernel,user,reserved[2];ULONG reserved2;};static_assert(sizeof(Row)==48);
 auto*p=static_cast<Row*>(data);for(std::size_t i=0;i<(std::min)(ntRows.size(),std::size_t(size/sizeof(Row)));++i){p[i]={};p[i].idle.QuadPart=ntRows[i].idle;p[i].kernel.QuadPart=ntRows[i].kernel;p[i].user.QuadPart=ntRows[i].user;}
 *written=reportedBytes<0?static_cast<ULONG>(ntRows.size()*sizeof(Row)):static_cast<ULONG>(reportedBytes);return 0;
}
static FARPROC WINAPI fakeGetProcAddress(HMODULE,LPCSTR){return reinterpret_cast<FARPROC>(&fakeQuery);}
static FARPROC WINAPI fakeMissingProc(HMODULE,LPCSTR){return nullptr;}
static HMODULE WINAPI fakeGetModuleHandleW(LPCWSTR){return nullptr;}
static BOOL WINAPI fakeGetSystemTimes(LPFILETIME idle,LPFILETIME kernel,LPFILETIME user){++systemCalls;if(!systemOK)return FALSE;auto put=[](FILETIME*p,std::uint64_t v){ULARGE_INTEGER t;t.QuadPart=v;p->dwLowDateTime=t.LowPart;p->dwHighDateTime=t.HighPart;};put(idle,aggregate.idle);put(kernel,aggregate.kernel);put(user,aggregate.user);return TRUE;}
static ULONGLONG WINAPI fakeGetTickCount64(){++clockCalls;return tick;}
#define GetProcAddress fakeGetProcAddress
#define GetModuleHandleW fakeGetModuleHandleW
#define GetSystemTimes fakeGetSystemTimes
#define GetTickCount64 fakeGetTickCount64
namespace baseline{
#include "__BASELINE_HEADER__"
__BEFORE__
static json demand(bool top){__BEFORE_LANE__return {{"total",cpuUsage},{"valid",cpuUsageValid},{"cores",coreUsage}};}
}
namespace candidate{
#include "__ROOT__/native/monitor_cpu_deltas.h"
__AFTER__
static json demand(bool top){__AFTER_LANE__return {{"total",cpuUsage},{"valid",cpuUsageValid},{"cores",coreUsage}};}
}
#undef GetProcAddress
#define GetProcAddress fakeMissingProc
namespace baselineMissing{namespace ymcc=baseline::ymcc;
__BEFORE__
}
namespace candidateMissing{namespace ymcc=candidate::ymcc;
__AFTER__
}
#undef GetProcAddress
#undef GetModuleHandleW
#undef GetSystemTimes
#undef GetTickCount64
using Native=double(*)(json*,bool*);
struct Observation{json packet;std::size_t allocs;};
static unsigned checks=0,comparedPackets=0;
static void check(bool ok,const char*name){++checks;if(!ok)throw std::runtime_error(name);}
static Observation observe(Native f,bool cores=true,bool validity=true){
 json slots="sentinel";bool valid=true;const auto oldNt=ntCalls,oldSys=systemCalls,oldClock=clockCalls;allocations=0;tracking=true;
 const double total=f(cores?&slots:nullptr,validity?&valid:nullptr);tracking=false;const auto n=allocations;
 return {{{"total",total},{"valid",validity?json(valid):json(nullptr)},{"cores",cores?slots:json(nullptr)},{"ntCalls",ntCalls-oldNt},{"systemCalls",systemCalls-oldSys},{"clockCalls",clockCalls-oldClock}},n};
}
static json compare(const char*name,bool cores=true,bool validity=true,bool missing=false){const auto a=observe(missing?baselineMissing::nativeCpuUsagePct:baseline::nativeCpuUsagePct,cores,validity),b=observe(missing?candidateMissing::nativeCpuUsagePct:candidate::nativeCpuUsagePct,cores,validity);check(a.packet.dump()==b.packet.dump(),name);++comparedPackets;return b.packet;}
template<class S>static json serialized(const S&s){json cores=json::array();for(const auto&v:s.cores)cores.push_back(v?json(*v):json(nullptr));return {{"total",s.total},{"valid",s.totalValid},{"cores",cores}};}
int main(int argc,char**argv){
 try{
  check(argc==2,"result path required");json cases=json::array();auto save=[&](const char*n,const json&p){cases.push_back({{"name",n},{"packet",p}});};
  tick=1000;ntRows={{100,100,100},{100,100,100}};aggregate={100,100,100};save("first-unknown",compare("first frame equivalence"));
  tick=2000;ntRows={{180,200,100},{100,200,100}};auto p=compare("same two-processor weighted load");check(p["total"]==60&&p["valid"]==true&&p["cores"]==json::array({20,100}),"actual expected weighted total and core values");save("weighted",p);
  tick=3000;ntRows={{280,300,100},{200,300,100}};p=compare("genuine zero remains valid");check(p["total"]==0&&p["valid"]==true,"genuine zero not missing");
  tick=4000;compare("zero delta keeps slots unknown");
  tick=5000;ntRows[1].idle=1;ntRows[0]={300,400,100};compare("partial regression preserves individual invalidity");
  tick=11000;ntRows={{310,410,110},{2,320,101}};compare("exact six second boundary");
  tick=17001;ntRows={{320,420,120},{3,330,102}};p=compare("long sleep gap rebaselines");check(!p["valid"].get<bool>(),"long gap is unknown");
  tick=18001;ntRows={{330,430,130},{4,340,103}};compare("fresh post-wake observation");
  tick=18001;ntRows[0].kernel+=100;compare("same timestamp rebaselines");tick=100;ntRows[0].kernel+=100;compare("backward timestamp rebaselines");
  tick=0;ntRows[0].kernel+=100;compare("unspecified timestamp compatibility");
  for(std::size_t n:{1,16,32,64,128}){ntRows.assign(n,{100,100,100});tick+=1000;compare("topology change has exact slot count");for(auto&v:ntRows){v.idle+=30;v.kernel+=100;v.user+=20;}tick+=1000;p=compare("stable topology fresh counters");check(p["cores"].size()==n,"all actual logical processor slots retained");}
  systemOK=true;aggregate={100,100,100};tick+=1000;ntRows.back().user=-1;p=compare("negative native counter rejects complete snapshot before commit");check(p["cores"].empty(),"invalid native counters use aggregate fallback without invented core values");
  ntRows.back().user=1000;tick+=1000;compare("return to native counters starts new baseline");
  for(long b:{0L,1L,47L,49L,48L*129}){reportedBytes=b;tick+=1000;aggregate.kernel+=100;compare("zero misaligned and oversized return lengths");}reportedBytes=-1;
  ntRows.assign(129,{100,100,100});tick+=1000;compare("129 logical processors uses existing bounded fallback");
  ntOK=false;systemOK=false;tick+=1000;p=compare("both APIs unavailable reset source history");check(p["total"]==0&&!p["valid"].get<bool>()&&p["cores"].empty(),"no APIs produces no measured load");
  systemOK=true;aggregate={100,100,100};tick+=1000;compare("first aggregate fallback is unknown");aggregate={150,200,100};tick+=1000;p=compare("fresh aggregate fallback");check(p["total"]==50&&p["valid"]==true&&p["cores"].empty(),"fallback aggregate value and empty core list preserved");
  aggregate.kernel+=100;tick+=1000;compare("null core destination",false);aggregate.kernel+=100;tick+=1000;compare("null validity destination",true,false);
  tick+=1000;compare("missing NtQuery entrypoint",true,true,true);aggregate.kernel+=100;tick+=1000;compare("missing NtQuery steady aggregate",true,true,true);
  const auto n0=ntCalls,s0=systemCalls,c0=clockCalls;const auto a=baseline::demand(false),b=candidate::demand(false);check(a==b&&ntCalls==n0&&systemCalls==s0&&clockCalls==c0,"FPS-only demand performs zero CPU sampling APIs");
  baseline::ymcc::cpusample::Deltas old;candidate::ymcc::cpusample::Deltas current,indexed,aggregateOnly;std::mt19937_64 rng(20261016);std::uint64_t stamp=0;unsigned pureCases=0;std::size_t readerCalls=0;
  for(unsigned step=0;step<2000;++step){
   const std::size_t counts[]={0,1,2,16,32,64,128,129};const auto count=counts[(step/8)%8];
   std::vector<baseline::ymcc::cpusample::Times> x(count);std::vector<candidate::ymcc::cpusample::Times> y(count);
   for(std::size_t i=0;i<count;++i){const auto t=step*1000ULL+i*10;auto idle=t/2,kernel=t,user=t/3;if(step%31==0)idle=UINT64_MAX;if(step%37==0){kernel=UINT64_MAX;user=UINT64_MAX;}if(step%41==0)kernel=0;x[i]={idle,kernel,user};y[i]={idle,kernel,user};}
   stamp=step%47==0?0:stamp+(step%19==0?6001:step%23==0?0:1000);
   const auto prior=serialized(old.accept(x,stamp)),next=serialized(current.accept(y,stamp));check(prior.dump()==next.dump(),"original vector API retains complete canonical sample");
   std::size_t reads=0;const auto direct=indexed.acceptIndexed(count,stamp,[&](std::size_t i) noexcept {++reads;return y[i];});readerCalls+=reads;
   check(serialized(direct).dump()==prior.dump(),"indexed native admission equals original helper sample");check(reads==(count>0&&count<=128?count:0),"exactly one read per accepted item and none for rejected count");
   const auto sum=aggregateOnly.acceptIndexed(count,stamp,[&](std::size_t i) noexcept {return y[i];},false);check(sum.cores.empty()&&sum.total==prior["total"].get<double>()&&sum.totalValid==prior["valid"].get<bool>(),"omitting core materialization preserves aggregate validity and value");++pureCases;
  }
  json work=json::array();
  auto workload=[&](const char*name,std::size_t count,bool fallback,bool output,bool missing){ntOK=!fallback;systemOK=true;reportedBytes=-1;ntRows.assign(count,{100000,100000,100000});aggregate={100000,100000,100000};for(unsigned i=0;i<3;++i){tick+=1000;compare("allocation workload warmup",output,true,missing);}std::size_t oldAlloc=0,newAlloc=0;unsigned nq=0,sq=0;for(unsigned i=0;i<300;++i){for(auto&v:ntRows){v.idle+=10;v.kernel+=100;v.user+=20;}aggregate.idle+=10;aggregate.kernel+=100;aggregate.user+=20;tick+=1000;const auto x=observe(missing?baselineMissing::nativeCpuUsagePct:baseline::nativeCpuUsagePct,output),y=observe(missing?candidateMissing::nativeCpuUsagePct:candidate::nativeCpuUsagePct,output);check(x.packet.dump()==y.packet.dump(),"every workload packet and OS call count remains identical");oldAlloc+=x.allocs;newAlloc+=y.allocs;nq+=y.packet["ntCalls"].get<unsigned>();sq+=y.packet["systemCalls"].get<unsigned>();++comparedPackets;}check(newAlloc<oldAlloc,"actual steady sampler allocations strictly decrease");work.push_back({{"name",name},{"samples",300},{"processorCount",count},{"coreOutput",output},{"beforeAllocations",oldAlloc},{"afterAllocations",newAlloc},{"queryCalls",nq},{"fallbackCalls",sq}});};
  for(std::size_t n:{1,16,32,64,128})workload("native-with-core-output",n,false,true,false);
  workload("native-aggregate-only",32,false,false,false);workload("fallback-with-core-destination",1,true,true,false);workload("fallback-without-core-destination",1,true,false,false);workload("unavailable-entrypoint-fallback",1,true,true,true);
  json result={{"ok",true},{"checks",checks},{"nativePacketComparisons",comparedPackets},{"pureCounterComparisons",pureCases},{"indexedReaderCalls",readerCalls},{"cases",cases},{"workloads",work},{"cpuPercentageOrTimingMeasured",false},{"liveHardwareRead",false}};std::ofstream(argv[1])<<result.dump(2);std::cout<<"CPU counter source equivalence PASS "<<checks<<" checks\n";
 }catch(const std::exception&e){tracking=false;std::cerr<<e.what()<<"\n";return 1;}
}
`.replaceAll('__ROOT__',root).replaceAll('__OUT__',out).replace('__BASELINE_HEADER__',baselineHeader).replaceAll('__BEFORE__',beforeSampler).replaceAll('__AFTER__',afterSampler).replace('__BEFORE_LANE__',beforeLane).replace('__AFTER_LANE__',afterLane);
fs.writeFileSync(out+'/cpu-counter-source.cpp',code);const bat=out+'/compile-counter-source.bat';fs.writeFileSync(bat,`@echo off\r\ncall "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\ncl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 "${out}/cpu-counter-source.cpp" /Fo"${out}/cpu-counter-source.obj" /Fe"${out}/cpu-counter-source.exe"\r\nexit /b %ERRORLEVEL%\r\n`.replaceAll('\\r\\n','\r\n'));
const build=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/s','/c','""'+bat+'""'],{windowsVerbatimArguments:true,windowsHide:true,encoding:'utf8',timeout:45000,maxBuffer:4e6});fs.writeFileSync(out+'/COMPILE.log',(build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,build.stdout+build.stderr);
const run=spawnSync(out+'/cpu-counter-source.exe',[out+'/COUNTER-SOURCE-RESULT.json'],{windowsHide:true,encoding:'utf8',timeout:20000});fs.writeFileSync(out+'/RUN.log',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,run.stdout+run.stderr);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');assert.equal(sampler(fs.readFileSync('native/main.cpp','utf8')),afterSampler);assert.equal(sha(fs.readFileSync('native/monitor_cpu_deltas.h')),sha(header));
const result=JSON.parse(fs.readFileSync(out+'/COUNTER-SOURCE-RESULT.json','utf8'));result.sourcePins={beforeSampler:sha(beforeSampler),afterSampler:sha(afterSampler),beforeHeader:sha(fs.readFileSync(baselineHeader)),afterHeader:sha(header),demandLane:sha(afterLane)};result.actualProductionSamplerAndHelper=true;result.baselineFiles={source:baselineSource,header:baselineHeader};fs.writeFileSync(out+'/COUNTER-SOURCE-RESULT.json',JSON.stringify(result,null,2));console.log(JSON.stringify({ok:result.ok,checks:result.checks,nativePacketComparisons:result.nativePacketComparisons,pureCounterComparisons:result.pureCounterComparisons,workloads:result.workloads},null,2));
