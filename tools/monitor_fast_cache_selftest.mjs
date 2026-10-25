// Actual native cache publishers; no app, hardware reads, CPU usage measurements or game focus.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
const root=process.cwd(),task=path.resolve(process.env.YMCC_MONITOR_FAST_TASK||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-fast-cache-pass-20261016');
const out=path.join(task,'native-tests');fs.mkdirSync(out,{recursive:true});
const load=p=>fs.readFileSync(p,'utf8').replaceAll('\r\n','\n');
const before=load(path.join(task,'before/main.cpp')),after=load('native/main.cpp');
function extract(source,signature){
 const start=source.indexOf(signature);if(start<0)throw Error('missing '+signature);
 const masked=source.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>' '.repeat(m.length));
 const open=masked.indexOf('{',start);let depth=0;
 for(let i=open;i<masked.length;i++){if(masked[i]==='{')depth++;else if(masked[i]==='}'&&!--depth)return source.slice(start,i+1);}
 throw Error('unclosed '+signature);
}
function variant(source,name){
 const functions=['static void monitorSnapshotInvalidate()','static void monitorSnapshotOnModeChange(','static unsigned long long monitorSnapshotGeneration()','static void monitorSnapshotPublish('].map(s=>extract(source,s)).join('\n');
 const hasFast=source.includes('static void monitorSnapshotPublishFrames(');
 const fast=hasFast?extract(source,'static void monitorSnapshotPublishFrames('):'';
 const hasBorrowed=source.includes('static void monitorSnapshotPublishFramesBorrowed(');
 const borrowed=hasBorrowed?extract(source,'static void monitorSnapshotPublishFramesBorrowed('):'';
 const call=hasBorrowed?'monitorSnapshotPublishFramesBorrowed(generation,at,frame);':hasFast?'monitorSnapshotPublishFrames(generation,at,std::move(frame));':'monitorSnapshotPublish(generation,laneTop,true,laneHealthy,at,g_monitorSnapshot.topText,std::move(frame));';
 return `namespace ${name} {\nstatic std::mutex g_monitorSnapshotMx;\nstatic std::atomic<unsigned long long> g_monitorSnapshotOversizeDrops{0};\n${extract(source,'struct MonitorSnapshotState')} ;\nstatic MonitorSnapshotState g_monitorSnapshot;\nstatic std::function<void()> barrier;\nstatic void monitorTestBarrierPause(int){if(barrier)barrier();}\n${functions}\n${fast}\n${borrowed}\nstatic bool laneTop=true,laneHealthy=true;\nstatic void fast(std::uint64_t generation,long long at,std::string frame){${call}}\n}\n`;
}
const prelude=String.raw`
#include <atomic>
#include <cstdlib>
#include <new>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <future>
#include <iostream>
#include <fstream>
#include "json.hpp"
#include "monitor_snapshot_contract.h"
using json=nlohmann::json;
static std::atomic<bool> track{false};static std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t n){if(track.load())++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
`;
const checks=String.raw`
int main(int argc,char**argv){if(argc!=2)return 3;json checks=json::array();int failed=0;auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"ok",ok}});if(!ok)++failed;};
 using namespace current;
 const std::string top=std::string(8192,'T');
 previous::monitorSnapshotPublish(0,true,true,true,100000,top,"{\"fps\":120}");
 monitorSnapshotPublish(0,true,true,true,100000,top,"{\"fps\":120}");
 allocations=0;track=true;for(int i=1;i<=10;++i)previous::fast(0,100000+i*100,"{\"fps\":121}");track=false;const auto oldAllocs=allocations.load();
 allocations=0;track=true;for(int i=1;i<=10;++i)fast(0,100000+i*100,"{\"fps\":121}");track=false;const auto newAllocs=allocations.load();
 check("baseline reproduces HWiNFO proof being advanced by frame-only updates",previous::g_monitorSnapshot.hwinfoOkAt==101000);
 check("frame-only updates preserve genuine HWiNFO proof timestamp",g_monitorSnapshot.hwinfoOkAt==100000);
 check("frame-only updates retain identical hardware text",g_monitorSnapshot.topText==top);
 check("frame-only updates keep hardware text allocation/storage",newAllocs==0&&oldAllocs>=10);
 check("frame refresh still advances publication and FPS heartbeat",g_monitorSnapshot.valid&&g_monitorSnapshot.seq==11&&g_monitorSnapshot.collectedAt==101000&&g_monitorSnapshot.heartbeatAt==101000&&g_monitorSnapshot.fpsText=="{\"fps\":121}");
 check("frame-only update never changes healthy hardware state",!g_monitorSnapshot.hwDown);
 const auto stored=g_monitorSnapshot.topText.data();fast(0,101100,"{\"fps\":122}");check("fast update preserves the existing top buffer identity",g_monitorSnapshot.topText.data()==stored);
 laneHealthy=false;monitorSnapshotPublish(0,true,true,false,102000,top,"{\"fps\":100}");fast(0,102100,"{\"fps\":101}");
 check("RTSS success cannot heal unhealthy HWiNFO",g_monitorSnapshot.hwDown&&g_monitorSnapshot.hwinfoOkAt==0);
 laneHealthy=true;laneTop=false;monitorSnapshotPublish(0,false,true,true,103000,"ignored","{}");fast(0,103100,"{\"fps\":90}");check("FPS-only mode never fabricates top payload",g_monitorSnapshot.topText.empty()&&g_monitorSnapshot.hwinfoOkAt==103000);
 const auto stoppedGen=monitorSnapshotGeneration();monitorSnapshotOnModeChange(false,true,false,false,false);const auto stoppedSeq=g_monitorSnapshot.seq;fast(stoppedGen,104000,"late");
 check("stopping both modes fences late fast frames",!g_monitorSnapshot.valid&&g_monitorSnapshot.fpsText.empty()&&g_monitorSnapshot.seq==stoppedSeq);
 const auto invalidGen=monitorSnapshotGeneration();fast(invalidGen,104100,"not-published-yet");check("fast frames cannot resurrect an invalid cache",!g_monitorSnapshot.valid&&g_monitorSnapshot.fpsText.empty()&&g_monitorSnapshot.seq==stoppedSeq);
 monitorSnapshotOnModeChange(true,true,true,true,true);const auto activeGen=monitorSnapshotGeneration();monitorSnapshotPublish(activeGen,true,true,true,105000,top,"{}");
 monitorSnapshotOnModeChange(true,false,false,false,true);const auto topStopSeq=g_monitorSnapshot.seq;fast(activeGen,105100,"late-top");check("stopping only top also fences the old fast lane",g_monitorSnapshot.topText.empty()&&g_monitorSnapshot.seq==topStopSeq);
 const auto resumedGen=monitorSnapshotGeneration();monitorSnapshotPublish(resumedGen,false,true,true,106000,"","{}");fast(resumedGen,106100,"");check("game-exit empty FPS immediately clears frame payload without renewing hardware proof",g_monitorSnapshot.fpsText.empty()&&g_monitorSnapshot.hwinfoOkAt==106000&&g_monitorSnapshot.heartbeatAt==106100);
 monitorSnapshotPublish(resumedGen,true,true,true,107000,top,"{}");
 std::promise<void> waiting,release;auto released=release.get_future().share();barrier=[&]{waiting.set_value();released.wait();};
 std::thread delayed([&]{fast(resumedGen,107100,"late-barrier");});waiting.get_future().wait();monitorSnapshotInvalidate();const auto invalidatedSeq=g_monitorSnapshot.seq;release.set_value();delayed.join();barrier={};
 check("actual interleaved invalidate during fast publish cannot restore stale data",!g_monitorSnapshot.valid&&g_monitorSnapshot.topText.empty()&&g_monitorSnapshot.fpsText.empty()&&g_monitorSnapshot.hwinfoOkAt==0&&g_monitorSnapshot.seq==invalidatedSeq);
 const auto nextGen=monitorSnapshotGeneration();monitorSnapshotPublish(nextGen,true,true,true,108000,top,"{}");fast(nextGen,108100,"fresh");check("fresh full publication resumes frame updates after restart",g_monitorSnapshot.valid&&g_monitorSnapshot.fpsText=="fresh"&&g_monitorSnapshot.hwinfoOkAt==108000);
 json result={{"ok",failed==0},{"failures",failed},{"checks",checks},{"tenFastUpdatesTopTextAllocations",{{"before",oldAllocs},{"after",newAllocs}}},{"measuredCpuUsage",false},{"hardwareIo",false},{"installedProcessChanged",false}};std::ofstream(argv[1])<<result.dump(2);std::cout<<result.dump(2)<<"\n";return failed?2:0;
}
`;
fs.writeFileSync(out+'/fast-cache.cpp',prelude+variant(before,'previous')+variant(after,'current')+checks);
fs.writeFileSync(out+'/build.cmd',`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 "${out}/fast-cache.cpp" /I"${root}/deps/json" /I"${root}/native" /Fo"${out}/fast-cache.obj" /Fe"${out}/fast-cache.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/fast-cache.exe" "${out}/RESULT.json"\r\nexit /b %errorlevel%\r\n`);
const run=spawnSync('cmd.exe',['/d','/c',out+'/build.cmd'],{cwd:out,encoding:'utf8',timeout:45000,maxBuffer:2e6});
fs.writeFileSync(out+'/BUILD.log',(run.stdout||'')+(run.stderr||''));
fs.writeFileSync(out+'/SOURCE-PINS.json',JSON.stringify({before:crypto.createHash('sha256').update(before).digest('hex'),after:crypto.createHash('sha256').update(after).digest('hex'),fastFunctionPresent:after.includes('static void monitorSnapshotPublishFrames(')},null,2));
const worker=after.slice(after.indexOf('static void nativeMonitorLoop()'),after.indexOf('static void nativeMonitorSetMode(')),fastLane=worker.slice(worker.indexOf('if (fps && game.pid && g_monitorFastFrames'));
const publisher=after.includes('static void monitorSnapshotPublishFrames(')?extract(after,'static void monitorSnapshotPublishFrames('):'';
const wiring={oneFullPublication:(worker.match(/monitorSnapshotPublish\(/g)||[]).length===1,oneFramePublication:(worker.match(/monitorSnapshotPublishFrames(?:Borrowed)?\(/g)||[]).length===2&&fastLane.includes("monitorSnapshotPublishFrames(iterGeneration,static_cast<long long>(frameNow),{});"),frameLaneNoFullPublication:!fastLane.includes('monitorSnapshotPublish('),noCachedHardwareTextCopy:!worker.includes('cachedTopText'),framePublisherPreservesHardwareProof:!/(hwinfoOkAt|hwDown|monitorReadHWiNFO|nativeCpuUsagePct|WriteFile|ofstream|appendNativeLifecycleLog)/.test(publisher),frameOverflowOnlyReleasesWholeTopText:publisher.includes("monitorsnapshot::textFits(g_monitorSnapshot.topText, fpsText)")&&publisher.includes("monitorsnapshot::discard(g_monitorSnapshot.topText)"),generationAndValidityFenced:publisher.includes('g_monitorSnapshot.generation != expectedGeneration || !g_monitorSnapshot.valid'),noNewCollectionThread:((worker.match(/monitorReadHWiNFO\(/g)||[]).length===1)&&!publisher.includes('std::thread'),fastLaneDoesNotRecordSnapshots:!fastLane.includes('monitorRecordDetailedSnapshot(')};
fs.writeFileSync(out+'/WIRING.json',JSON.stringify({ok:Object.values(wiring).every(Boolean),checks:wiring},null,2));
console.log(((run.stdout||'')+(run.stderr||'')).slice(-6500));console.log(JSON.stringify({wiring},null,2));if(run.status!==0||!Object.values(wiring).every(Boolean))process.exitCode=2;
