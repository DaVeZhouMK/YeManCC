// Offline native parser and actual high-frequency recorder; no hardware reads.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const root=process.cwd(),task=path.resolve(process.env.YMCC_GPU_MEMORY_EVIDENCE_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/amd-vram-three-machine-20261016'),out=task+'/evidence';fs.mkdirSync(out,{recursive:true});
// Regenerate the current native parser/publisher fixture; a retained historical
// generated file must not silently test a different production revision.
const parserOut=task+'/parser';fs.mkdirSync(parserOut,{recursive:true});
const generation=spawnSync(process.execPath,['tools/monitor_memory_publication_selftest.mjs'],{cwd:root,env:{...process.env,YMCC_MONITOR_MEMORY_OUT:parserOut},windowsHide:true,encoding:'utf8',timeout:30000,maxBuffer:3e6});
assert.equal(generation.status,0,generation.stdout+generation.stderr);
const main=fs.readFileSync('native/main.cpp','utf8'),generated=fs.readFileSync(parserOut+'/memory-publication.cpp','utf8'),prefix=generated.slice(0,generated.indexOf('int main(int argc,char**argv)'));
const sig='static void monitorRecordDetailedSnapshot(long long nowMs,unsigned long long pollTime,unsigned long long periodMs,bool periodKnown,bool readable,bool healthy,const ymcc::monitorgpu::Device* selectedGpu,const ymcc::monitorgpu::Readings& gpuReadings){',start=main.indexOf(sig),end=main.indexOf('\n// Fan lifecycle evidence',start);assert(start>=0&&end>start);const hook=main.slice(start,end);
const stateStart=main.indexOf('struct MonitorSnapshotState {'),stateEnd=main.indexOf('\n};',stateStart);assert(stateStart>=0&&stateEnd>stateStart);
const state=main.slice(stateStart,stateEnd+3);
const captureStart=main.indexOf('struct MonitorDetailSnapshotCapture {');assert(captureStart>0&&captureStart<start);
const capture=main.slice(captureStart,start).replace('monitorSnapshotRecordForDetail(', 'actualMonitorSnapshotRecordForDetail(');
const harness=String.raw`
#include <atomic>
#include <functional>
#include "${root}/native/monitor_detail_log.h"
#include "${root}/native/monitor_snapshot_contract.h"
#include <mutex>
#include "${root}/native/monitor_gpu_memory_evidence.h"
static bool failEvidenceAllocation=false;static bool countEvidenceAllocations=false;static std::size_t evidenceAllocations=0;
void* operator new(std::size_t n){if(failEvidenceAllocation){failEvidenceAllocation=false;throw std::bad_alloc();}if(countEvidenceAllocations)++evidenceAllocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static bool detailOn=false;static uint64_t detailTick=1000;static unsigned cacheReads=0;
static json detailCache;static std::vector<std::string> saved;static std::vector<std::function<void()>>jobs;
static bool domainLogEnabled(const char*){return detailOn;}
static std::string nativeLifecycleTimestamp(){return "2026-10-16T10:00:00.000";}
struct EvidenceState {
 ymcc::monitordetail::Queue queue;
 EvidenceState():queue({[]{return detailOn;},[](auto f){jobs.push_back(std::move(f));return true;},[](auto line){saved.emplace_back(line);return true;},[]{},[]{}}){}
 json stats()const{return queue.stats();}
};
static EvidenceState& monitorDetailState(){static EvidenceState s;return s;}
#define GetTickCount64() detailTick
#define GetCurrentProcessId() 7
` .replaceAll('${root}',root.replaceAll('\\','/'));
const directCapture=String.raw`
// Install fixture state, then execute the exact production direct-cache reader.
// Only the cache producer and clock are inert; the recorder/limits/queue remain
// the production functions below.
__STATE__
static MonitorSnapshotState g_monitorSnapshot;
static std::mutex g_nativeMonitorMx,g_monitorSnapshotMx;
static bool g_nativeMonitorTop=true,g_nativeMonitorFps=false;
static std::atomic<unsigned long long> g_monitorRelayEpoch{0},g_monitorSnapshotServes{0};
static unsigned long long nativeMonitorSessionId(){return 1;}
__CAPTURE__
static MonitorDetailSnapshotCapture monitorSnapshotRecordForDetail(long long now,unsigned pid,json counters){
 ++cacheReads;
 g_monitorSnapshot.valid=detailCache.value("available",false);
 g_monitorSnapshot.generation=detailCache.value("generation",0ULL);g_monitorSnapshot.seq=detailCache.value("seq",0ULL);
 g_monitorSnapshot.collectedAt=detailCache.value("collectedAt",0LL);g_monitorSnapshot.heartbeatAt=detailCache.value("heartbeatAt",0LL);
 g_monitorSnapshot.hwinfoOkAt=detailCache.value("hwinfoOkAt",0LL);g_monitorSnapshot.hwDown=detailCache.value("hwDown",false);
 g_monitorSnapshot.topText=detailCache.value("top",std::string{});
 g_monitorSnapshot.fpsText=detailCache.contains("fps")&&detailCache["fps"].is_string()?detailCache["fps"].get<std::string>():std::string{};
 return actualMonitorSnapshotRecordForDetail(now,pid,std::move(counters));
}
`.replace('__STATE__',state).replace('__CAPTURE__',capture);
const tests=String.raw`
int main(int argc,char**argv){assert(argc==2);json checks=json::array(),examples=json::object();unsigned failures=0;
 auto check=[&](const char*name,bool ok){checks.push_back({{"name",name},{"ok",ok}});if(!ok){++failures;std::cout<<"FAIL "<<name<<"\n";}};
 auto capture=[&](const std::vector<Row>& rows,bool diagnostic=true){auto data=buffer(rows);NativeMonitorHw hw;bool unstable=false;check("actual unified parser accepts stable synthetic rows",monitorParseHWiNFOView(data.data(),data.size(),hw,unstable,diagnostic)&&!unstable);return hw;};
 auto rawTop=[&](const NativeMonitorHw& hw){return json::parse(nativeMonitorTopPayloadText(hw,true,false,-1,-1,0,1700000000000LL,true,json::array(),0,{},0,0,{},{},{},{}));};
 std::vector<Row> amd={{"GPU D3D Memory Dedicated","MB",14.734375,1},{"GPU D3D Memory Dynamic","MB",34.13671875,1},{"GPU Memory Usage","MB",100,1},{"GPU Core Load","%",20,1}};
 auto hw=capture(amd);auto e=ymcc::monitordetail::gpuMemoryEvidence(hw.gpuReadings.best(),true,hw.gpuReadings);examples["local-AMD-original-label-pattern"]=e;
 check("AMD actual used reading remains 100 MiB with missing capacity",e["rawUsedMb"]==100&&e["rawTotalMb"].is_null()&&e["normalizedLoadPct"].is_null()&&e["status"]=="used-without-capacity");
 check("D3D allocation rows are captured from the same existing group",e["d3dDedicatedUsedMb"]==14.734375&&e["d3dDynamicUsedMb"]==34.13671875&&e["d3dEvidenceCaptured"]==true&&e["sensorIndex"]==1);
 check("D3D does not stand in for physical memory capacity",rawTop(hw)["gpuMemoryTotalMb"].is_null()&&rawTop(hw)["gpuMemoryLoadPct"].is_null()&&rawTop(hw)["gpuMemoryUsedMb"]==100);
 auto off=capture(amd,false);auto offEvidence=ymcc::monitordetail::gpuMemoryEvidence(off.gpuReadings.best(),true,off.gpuReadings);
 check("detail off never collects extra D3D rows",offEvidence["d3dEvidenceCaptured"]==false&&offEvidence["d3dDedicatedUsedMb"].is_null());
 check("diagnostic gate does not change any published monitoring values",rawTop(off)==rawTop(hw));
 auto only=capture({{"GPU D3D Memory Dedicated","MB",4096,2},{"GPU D3D Memory Dynamic","MB",12288,2}});
 check("D3D-only rows never create or select a GPU",only.gpuReadings.size()==0&&ymcc::monitordetail::gpuMemoryEvidence(only.gpuReadings.best(),true,only.gpuReadings)["selected"]==false);
 auto both=amd;both.insert(both.end(),{{"GPU Memory Allocated","MB",4096,2},{"GPU Memory Available","MB",12288,2},{"GPU D3D Memory Dedicated","MB",3500,2},{"GPU D3D Memory Dynamic","MB",50,2},{"GPU Power","W",50,2},{"GPU Memory Usage","%",25,2}});auto two=capture(both);
 auto n=ymcc::monitordetail::gpuMemoryEvidence(two.gpuReadings.best(),true,two.gpuReadings);examples["NVIDIA-coherent-pattern"]=n;
 check("selected dedicated GPU keeps its own complete memory tuple",n["sensorIndex"]==2&&n["rawUsedMb"]==4096&&n["normalizedTotalMb"]==16384&&n["normalizedLoadPct"]==25&&n["usedReading"]=="GPU Memory Allocated");
 check("evidence never borrows adjacent AMD allocation counters",n["d3dDedicatedUsedMb"]==3500&&n["d3dDynamicUsedMb"]==50);
 check("freshness flag is truthful even when raw old counters are retained",ymcc::monitordetail::gpuMemoryEvidence(two.gpuReadings.best(),false,two.gpuReadings)["fresh"]==false);
 {
  auto copy=two.gpuReadings;const auto before=ymcc::monitordetail::gpuMemoryEvidence(copy.best(),true,copy);
  two.gpuReadings.retainMemoryEvidence([](uint32_t){return ymcc::monitormemory::D3DMemory{1,2};});
  check("copied native packets keep their own immutable diagnostic payload",ymcc::monitordetail::gpuMemoryEvidence(copy.best(),true,copy)==before);
  two.gpuReadings=copy;
  auto worst=*copy.best();worst.key.name.fill('\x01');worst.key.name.back()=0;
  check("a foreign device cannot claim another packet's D3D evidence",ymcc::monitordetail::gpuMemoryEvidence(&worst,true,copy)["d3dEvidenceCaptured"]==false);
  const auto bounded=ymcc::monitordetail::gpuMemoryEvidence(&worst,true,copy).dump(-1,' ',false,json::error_handler_t::replace);
  check("escaped maximum sensor name stays inside the diagnostic byte budget",bounded.size()<2048);
 }
 auto bad=capture({{"GPU Memory Usage","MB",0,1},{"GPU D3D Memory Dedicated","%",80,1},{"GPU D3D Memory Dynamic","MB",-1,1}});auto b=ymcc::monitordetail::gpuMemoryEvidence(bad.gpuReadings.best(),true,bad.gpuReadings);
 check("real zero survives while invalid D3D units/negative values stay unknown",b["rawUsedMb"]==0&&b["d3dDedicatedUsedMb"].is_null()&&b["d3dDynamicUsedMb"].is_null());
 {ymcc::monitormemory::Readings r(true);check("out-of-bound diagnostic group is handled without storage growth",r.add(4096,MonitorFoldedText(std::string_view("GPU D3D Memory Dedicated")),MonitorFoldedText(std::string_view("MB")),10,true)&&r.diagnosticD3D(4096).dedicated<0);}
 {ymcc::monitormemory::Readings r;check("disabled diagnostic labels keep the previous unhandled return",!r.add(1,MonitorFoldedText(std::string_view("GPU D3D Memory Dedicated")),MonitorFoldedText(std::string_view("MB")),10,true));}
 {ymcc::monitorgpu::StickySelection selected;const auto* device=selected.select(two.gpuReadings,true);check("diagnostic fields do not change sticky power-based selection",device&&device->sensorIndex==2);}
 {
  const auto data=buffer(both);bool unstable=false;NativeMonitorHw warm;
  monitorParseHWiNFOView(data.data(),data.size(),warm,unstable,false);
  NativeMonitorHw measured;evidenceAllocations=0;countEvidenceAllocations=true;
  const bool accepted=monitorParseHWiNFOView(data.data(),data.size(),measured,unstable,false);countEvidenceAllocations=false;
  check("warmed detail-off parser adds no heap allocation",accepted&&evidenceAllocations==0);
  NativeMonitorHw observed;evidenceAllocations=0;countEvidenceAllocations=true;
  const bool captured=monitorParseHWiNFOView(data.data(),data.size(),observed,unstable,true);countEvidenceAllocations=false;
  check("enabled evidence uses one bounded packet-owned block",captured&&evidenceAllocations==1);
  // Force a new numeric payload: an identical cache hit does not allocate.
  auto failureRows=both;failureRows[0].value+=.25;const auto failureData=buffer(failureRows);
  NativeMonitorHw allocationFailure;failEvidenceAllocation=true;
  const bool survived=monitorParseHWiNFOView(failureData.data(),failureData.size(),allocationFailure,unstable,true);
  check("diagnostic allocation failure cannot break unified collection",survived&&!allocationFailure.gpuReadings.hasMemoryEvidence()&&rawTop(allocationFailure)==rawTop(measured));
  auto exceptionHw=capture(amd,false);exceptionHw.gpuReadings.retainMemoryEvidence([](uint32_t)->ymcc::monitormemory::D3DMemory{throw std::runtime_error("diagnostic-only");});
  check("diagnostic callback failure cannot alter primary readings",!exceptionHw.gpuReadings.hasMemoryEvidence()&&rawTop(exceptionHw)==rawTop(off));

 }
 detailCache={{"schema",1},{"available",true},{"collectedAt",1700000000000LL},{"session",1},{"generation",1},{"seq",1},{"top",rawTop(hw).dump()},{"fps",nullptr}};
 monitorRecordDetailedSnapshot(1700000000000LL,1700000000ULL,1000,true,true,true,hw.gpuReadings.best(),hw.gpuReadings);
 check("detail gate off returns before copying cache or scheduling work",cacheReads==0&&jobs.empty()&&saved.empty());
 detailOn=true;monitorRecordDetailedSnapshot(1700000000000LL,1700000000ULL,1000,true,true,true,hw.gpuReadings.best(),hw.gpuReadings);
 check("enabled evidence uses the existing asynchronous queue",cacheReads==1&&jobs.size()==1&&saved.empty());
 for(unsigned i=0;i<100;++i)monitorRecordDetailedSnapshot(1700000000000LL,1700000000ULL,1000,true,true,true,hw.gpuReadings.best(),hw.gpuReadings);
 check("same-second retries do not copy or serialize diagnostic evidence",cacheReads==1&&jobs.size()==1);
 auto job=std::move(jobs.front());jobs.clear();job();const auto row=json::parse(saved.back());
 check("actual recorder routes memory proof to the high-frequency record",row["event"]=="monitor-data-snapshot"&&row["lane"]=="high-frequency"&&row["gpuMemoryEvidence"]==e);
 check("diagnostic evidence never enters the HUD/cache payload",row["snapshot"]["top"]==detailCache["top"]&&!json::parse(row["snapshot"]["top"].get<std::string>()).contains("d3dDedicatedUsedMb"));
 check("evidence and full record remain well inside existing byte caps",e.dump().size()<2048&&saved.back().size()+1<=32768);
 check("logging never calls another capacity reader",row["gpuMemoryEvidence"]["capacityQueried"]==false&&row["gpuMemoryEvidence"]["source"]=="existing-hwinfo-row-pass");
 detailTick=2000;detailCache["collectedAt"]=1700000001000LL;
 monitorRecordDetailedSnapshot(1700000000000LL,1700000000ULL,1000,true,true,true,hw.gpuReadings.best(),hw.gpuReadings);
 auto replacedJob=std::move(jobs.front());jobs.clear();replacedJob();
 check("a replaced unified cache cannot receive another iteration's memory evidence",json::parse(saved.back())["gpuMemoryEvidence"]["omitted"]==true);
 auto defaultHw=capture(amd,false);defaultHw.gpuReadings.retainMemoryEvidence([](uint32_t){return ymcc::monitormemory::D3DMemory{};});
 check("missing same-group D3D evidence stays unknown",ymcc::monitordetail::gpuMemoryEvidence(defaultHw.gpuReadings.best(),true,defaultHw.gpuReadings)["d3dDedicatedUsedMb"].is_null());
 json result={{"ok",failures==0},{"checks",checks},{"examples",examples},{"recordBytes",saved.back().size()+1},{"evidenceBytes",e.dump().size()},{"hardwareReads",false},{"newThreads",false},{"newPollingTimers",false}};std::ofstream(argv[1])<<result.dump(2);std::cout<<"GPU memory diagnostic: "<<checks.size()<<" checks, "<<failures<<" failed\n";return failures?2:0;
}
`;
fs.writeFileSync(out+'/evidence.cpp',prefix+harness+'\n'+directCapture+'\n'+hook+'\n'+tests);fs.writeFileSync(out+'/build.cmd',`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 "${out}/evidence.cpp" /Fo"${out}/evidence.obj" /Fe"${out}/evidence.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/evidence.exe" "${task}/EVIDENCE-RESULT.json"\r\nexit /b %errorlevel%\r\n`);
const run=spawnSync('cmd.exe',['/d','/c',out+'/build.cmd'],{encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:3e6});fs.writeFileSync(out+'/BUILD.log',(run.stdout||'')+(run.stderr||''));console.log(((run.stdout||'')+(run.stderr||'')).slice(-4500));if(run.status!==0)process.exitCode=2;
