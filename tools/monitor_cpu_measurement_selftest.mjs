// Production native CPU sampler/publisher and unified consumer, with every
// Windows sampling API replaced by deterministic counters. No CPU measurement.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import cp from 'node:child_process';import crypto from 'node:crypto';import {createRequire} from 'node:module';
const root=process.cwd(),task=path.resolve(process.env.YMCC_MONITOR_CPU_MEASUREMENT_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/cpu-data-audit-20261015'),out=path.join(task,'cpu-measurement');fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync('native/main.cpp','utf8'),prior=fs.readFileSync(path.join(task,'before/native/main.cpp'),'utf8');
const cut=(s,a,b)=>{const i=s.indexOf(a),j=s.indexOf(b,i);assert(i>=0&&j>i,a);return s.slice(i,j);};
const sampler=cut(source,'static double nativeCpuUsagePct(','static bool nativeBatteryStatus('),beforeSampler=cut(prior,'static double nativeCpuUsagePct(','static bool nativeBatteryStatus(');
const structure=cut(source,'struct NativeMonitorHw {','\n};')+'\n};',publisher=cut(source,'static std::string nativeMonitorTopPayloadText(','static void nativeMonitorLoop()');
const lane=cut(source,'        json coreUsage = json::array(); bool cpuUsageValid = false;','        const auto fpsLimit =');
const priorLane=cut(prior,'        json coreUsage;','        const auto fpsLimit =');
const r=root.replaceAll('\\','/'),base=path.join(task,'before/native/monitor_cpu_deltas.h').replaceAll('\\','/');
const code=String.raw`
#include <windows.h>
#include <vector>
#include <optional>
#include <limits>
#include <cmath>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <string>
#include "${r}/deps/json/json.hpp"
#include "${r}/native/monitor_gpu_selection.h"
#include "${r}/native/monitor_filesystem_rates.h"
#include "${r}/native/monitor_battery_energy.h"
#include "${r}/native/monitor_rtss_global.h"
#include "${r}/native/monitor_fan_receipt.h"
#include "${r}/native/monitor_freshness.h"
using json=nlohmann::json;
struct FakeTimes{long long idle=0,kernel=0,user=0;};
static std::vector<FakeTimes> ntRows;static FakeTimes aggregate;static bool ntEnabled=false,systemOK=false;static ULONGLONG tick=0;static int ntCalls=0,systemCalls=0,checks=0;
static void check(bool ok,const char* name){++checks;if(!ok){std::cerr<<"FAIL "<<name<<"\n";std::exit(1);}}
static LONG NTAPI fakeQuery(ULONG code,PVOID data,ULONG size,PULONG written){++ntCalls;check(code==8,"same existing Windows counter API");if(!ntEnabled)return -1;struct Row{LARGE_INTEGER idle,kernel,user,reserved[2];ULONG reserved2;};check(size>=ntRows.size()*sizeof(Row),"bounded fake buffer");auto* p=static_cast<Row*>(data);for(size_t i=0;i<ntRows.size();++i){p[i]={};p[i].idle.QuadPart=ntRows[i].idle;p[i].kernel.QuadPart=ntRows[i].kernel;p[i].user.QuadPart=ntRows[i].user;}*written=static_cast<ULONG>(ntRows.size()*sizeof(Row));return 0;}
static FARPROC WINAPI fakeGetProcAddress(HMODULE,LPCSTR){return reinterpret_cast<FARPROC>(&fakeQuery);}
static HMODULE WINAPI fakeGetModuleHandleW(LPCWSTR){return nullptr;}
static BOOL WINAPI fakeGetSystemTimes(LPFILETIME idle,LPFILETIME kernel,LPFILETIME user){++systemCalls;if(!systemOK)return FALSE;auto put=[](FILETIME* p,unsigned long long value){ULARGE_INTEGER n;n.QuadPart=value;p->dwLowDateTime=n.LowPart;p->dwHighDateTime=n.HighPart;};put(idle,aggregate.idle);put(kernel,aggregate.kernel);put(user,aggregate.user);return TRUE;}
static ULONGLONG WINAPI fakeGetTickCount64(){return tick;}
#define GetProcAddress fakeGetProcAddress
#define GetModuleHandleW fakeGetModuleHandleW
#define GetSystemTimes fakeGetSystemTimes
#define GetTickCount64 fakeGetTickCount64
namespace before {
#include "${base}"
${beforeSampler}
static double demandLane(bool top){${priorLane}return cpuUsage;}
}
namespace after {
#include "${r}/native/monitor_cpu_deltas.h"
${sampler}
static double demandLane(bool top){${lane}return cpuUsage;}
}
#undef GetProcAddress
#undef GetModuleHandleW
#undef GetSystemTimes
#undef GetTickCount64
${structure}
${publisher}
int main(int argc,char**argv){check(argc==2,"output path");json observations=json::array();json cores;bool valid=true;
 auto read=[&](const char* name){valid=true;const auto value=after::nativeCpuUsagePct(&cores,&valid);observations.push_back({{"name",name},{"value",value},{"valid",valid},{"cores",cores}});return value;};
 tick=1000;systemOK=false;read("read-failure");check(!valid&&cores.empty(),"API failure is unknown");
 systemOK=true;aggregate={100,100,100};read("fallback-first");check(!valid,"first aggregate baseline is unknown");
 tick=2000;aggregate={200,200,100};check(read("fallback-real-zero")==0&&valid&&cores.empty(),"measured idle zero valid, no invented core tiles");
 tick=3000;aggregate={220,300,100};check(std::abs(read("fallback-load")-80)<1e-9&&valid,"aggregate delta 80 percent");
 tick=4000;aggregate={1,1,1};read("fallback-regression");check(!valid,"aggregate counter rollback invalidates");
 tick=15000;aggregate={10,30,10};read("fallback-resume-gap");check(!valid,"long gap rebaselines aggregate");
 tick=16000;read("fallback-zero-interval");check(!valid,"no time delta is not measured idle");
 tick=17000;systemOK=false;read("fallback-failure-resets");check(!valid,"failed fallback resets baseline");
 tick=18000;systemOK=true;aggregate={30,60,20};read("fallback-recovery-first");check(!valid,"recovery begins with unknown baseline");
 tick=19000;ntEnabled=true;ntRows={{100,100,100},{100,100,100}};read("nt-first");check(!valid&&cores==json::array({nullptr,nullptr}),"Windows first per-core snapshot unknown");
 tick=20000;ntRows={{180,200,100},{100,200,100}};check(std::abs(read("nt-load")-60)<1e-9&&valid,"one NT snapshot supplies aggregate 60");check(cores==json::array({20.0,100.0}),"same valid cores 20/100");
 tick=21000;ntRows={{1,1,1},{150,300,100}};read("nt-partial");check(!valid&&cores[0].is_null()&&cores[1]==50.0,"partial cores do not claim whole CPU aggregate");
 tick=22000;ntRows={{10,100,1},{250,400,100}};read("nt-complete-recovery");check(valid,"all fresh core deltas restore aggregate");
 tick=23000;ntRows={{100,200,100}};read("nt-topology-change");check(!valid,"topology change rebaselines");
 const int oldQ=ntCalls;for(int i=0;i<200;i++)before::demandLane(false);const int oldDemandCalls=ntCalls-oldQ;check(oldDemandCalls==200,"baseline FPS-only CPU work reproduced");const int q=ntCalls,s=systemCalls;for(int i=0;i<200;i++)after::demandLane(false);const int currentDemandCalls=ntCalls-q;check(currentDemandCalls==0&&systemCalls==s,"FPS-only 200 iterations perform zero CPU API calls");after::demandLane(true);check(ntCalls==q+1,"top demand keeps exactly one CPU snapshot");
 // Old fallback resumes as a load, while the new sampler requires a recent pair.
 ntEnabled=false;systemOK=true;tick=30000;aggregate={100,100,100};before::nativeCpuUsagePct();tick=50000;aggregate={100,200,100};const auto oldGap=before::nativeCpuUsagePct();check(oldGap==100,"baseline long-gap synthetic load reproduced");
 constexpr long long now=1700000000000LL;NativeMonitorHw hw;json publications=json::array();
 auto publish=[&](const char* name,double load,bool known,long long ts,bool healthy=false){auto top=json::parse(nativeMonitorTopPayloadText(hw,true,false,-1,-1,load,healthy?now:0,healthy,json::array(),0,{},0,0,{},{},{},{},known,ts));publications.push_back({{"name",name},{"top",top}});return top;};
 auto p=publish("independent-load",80,true,now);check(p["cpuUsageValid"]==true&&p["cpuUsageTs"]==now&&p["hwDown"]==true,"Windows CPU proof survives HWiNFO outage");
 p=publish("independent-zero",0,true,now);check(p["cpuUsageValid"]==true&&p["cpuUsage"]==0,"publisher retains real zero");
 p=publish("warmup-or-failure",0,false,now);check(p["cpuUsageValid"]==false&&p["cpuUsageTs"]==0,"publisher marks invalid zero unknown");
 p=publish("invalid-load",101,true,now);check(p["cpuUsageValid"]==false,"out-of-range CPU cannot claim valid proof");
 p=publish("future-proof",50,true,now+1);p=publish("expired-proof",50,true,now-6000);
 publications.push_back({{"name","legacy-top"},{"top",json::parse(nativeMonitorTopPayloadText(hw,true,false,-1,-1,25,now,true))}});
 json result={{"ok",true},{"checks",checks},{"observations",observations},{"publications",publications},{"fpsOnlyCpuApiCalls",currentDemandCalls},{"beforeFpsOnlyCpuApiCalls",oldDemandCalls},{"beforeLongGapLoad",oldGap}};std::ofstream(argv[1])<<result.dump(2);std::cout<<"CPU_NATIVE_MEASUREMENT_VALIDITY_PASS "<<checks<<" checks; injected counters only\n";
}
`;
fs.writeFileSync(path.join(out,'cpu-measurement.cpp'),code);const cmd=path.join(out,'build-cpu-measurement.cmd');fs.writeFileSync(cmd,`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /DNOMINMAX "${out}/cpu-measurement.cpp" /Fo"${out}/cpu-measurement.obj" /Fe"${out}/cpu-measurement.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/cpu-measurement.exe" "${out}/NATIVE-CPU.json"\r\nexit /b %errorlevel%\r\n`);
try{const log=cp.execFileSync('cmd.exe',['/d','/c',cmd],{cwd:root,windowsHide:true,encoding:'utf8',stdio:'pipe'});fs.writeFileSync(path.join(out,'COMPILE-RUN.log'),log);}catch(error){fs.writeFileSync(path.join(out,'COMPILE-RUN.log'),String(error.stdout??'')+String(error.stderr??''));throw error;}
const native=JSON.parse(fs.readFileSync(path.join(out,'NATIVE-CPU.json'),'utf8'));assert.equal(native.ok,true);
const {build}=createRequire(path.join(root,'package.json'))('esbuild'),mocks={'./ipc':'export const isNativeRuntime=false;export function on(){return()=>{}}export async function invoke(){throw Error("unexpected IO")}', './api':'export const fs={};export const windowApi={};','./monitorAppliedTdp':'export const advanceMonitorTdpDisplay=()=>{};export function subscribeMonitorTdpState(){return()=>{}}'};
const bundle=await build({stdin:{resolveDir:root,loader:'ts',contents:`export * from './src/bridge/monitorData.ts';export * from './src/bridge/monitorHudModel.ts';export * from './src/bridge/monitorHudFreshness.ts';export * from './src/bridge/monitorTopProjection.ts';export * from './src/bridge/monitorDisplayFeed.ts';export * from './src/bridge/desktopHudProtocol.ts';export * from './src/bridge/monitorHudProtocol.ts'`},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'cpu-no-io',setup(b){b.onResolve({filter:/.*/},args=>mocks[args.path]?{path:args.path,namespace:'inert'}:undefined);b.onLoad({filter:/.*/,namespace:'inert'},args=>({contents:mocks[args.path],loader:'ts'}));}}]});const m=await import('data:text/javascript;base64,'+Buffer.from(bundle.outputFiles[0].text).toString('base64'));const now=1700000000000,checks=[];
function admit(top){return m.createMonitorDataHub({now:()=>now,invoke:async()=>{throw Error('no native')},readTextFile:async()=>{throw Error('no files')},setTimer:()=>1,clearTimer(){}}).admitDisplayPublication({schema:1,available:true,session:1,generation:1,seq:1,collectedAt:now,topActive:true,fpsActive:false,hwDown:top.hwDown,top:JSON.stringify(top),fps:null});}
const prefs=m.normalizeMonitorOutput({enabled:true,output:'ymcc'});
for(const {name,top}of native.publications){const view=admit(top);assert(view);const data=m.hudFromUniformSnapshot(view,now),expected=name==='independent-load'?80:name==='independent-zero'?0:name==='legacy-top'?25:null;assert.equal(data.values.cpuUsage??null,expected,name);assert.equal(m.hudMetricAvailable(data,'cpuUsage'),expected!==null,name);assert.equal(m.hudMetricAvailable(data,'cpuUsageBar'),expected!==null,name+' graph');
 const desktop=m.admitDesktopHudFrame({type:'desktop-hud',protocol:1,hudId:1,nonce:'cpu-validity-test-nonce',seq:1,expiresAt:now+6000,show:true,preferences:prefs,data},1,'cpu-validity-test-nonce',now,0),steam=m.admitHudFrame({type:'hud',protocol:1,runId:'a'.repeat(32),session:'cpu-validity-test-nonce',seq:1,operation:1,expiresAt:now+6000,show:false,game:null,preferences:prefs,data},'a'.repeat(32),now,0);assert(desktop&&steam);for(const wire of [desktop.data,steam.data])assert.equal(wire.values.cpuUsage??null,expected,name+' wire');
 if(name==='independent-load'){assert.equal(data.stale,false);assert(m.monitorSnapshotHasFreshData(view,now));assert.equal(m.projectTopMonitorView(view,now).cpuUsage,80);assert.equal(data.freshUntil.cpuUsage,now+6000);assert.equal(m.ageHudData(data,now+5999).values.cpuUsage,80);assert.equal(m.ageHudData(data,now+6000).values.cpuUsage,null);const mixed={...data,values:{...data.values,cpuTemp:65},freshUntil:{...data.freshUntil,hardware:now+1000}};const aged=m.ageHudData(mixed,now+1000);assert.equal(aged.values.cpuTemp,null);assert.equal(aged.values.cpuUsage,80);}
 checks.push('actual native '+name+' through sole facade, graphic/numeric availability and both display protocols');}
// Stopping top collection retires the Windows proof even if other independent
// fields keep this same packet alive. A newer CPU observation remains usable.
let stopNow=now;const stopHub=m.createMonitorDataHub({now:()=>stopNow,invoke:async command=>command==='monitor.takeover'?{ok:true,cleaned:true,session:1,epoch:0}:{ok:true},readTextFile:async()=>{throw Error('no file IO')},setTimer:()=>1,clearTimer(){}});
const releaseTop=stopHub.acquire('cpu-measurement-stop',{top:true});for(let i=0;i<30;i++)await Promise.resolve();releaseTop();for(let i=0;i<30;i++)await Promise.resolve();stopNow+=100;
const keep={ts:0,hwDown:true,cpuUsage:80,cpuUsageValid:true,cpuUsageTs:now,fileReadMBps:4,fileWriteMBps:0,fileSystemTs:stopNow,fileSystemSource:'windows-filesystem-disk-activity'};
const packet={schema:1,available:true,session:1,generation:1,seq:1,collectedAt:stopNow,topActive:false,fpsActive:false,hwDown:true,top:JSON.stringify(keep),fps:null};
let stopped=stopHub.admitDisplayPublication(packet);assert(stopped?.top);assert.equal(stopped.top.cpuUsageValid,false);assert.equal(m.hudFromUniformSnapshot(stopped,stopNow).values.cpuUsage,null);assert.equal(m.hudFromUniformSnapshot(stopped,stopNow).values.diskRead,4);
stopped=stopHub.admitDisplayPublication({...packet,seq:2,top:JSON.stringify({...keep,cpuUsageTs:stopNow})});assert.equal(m.hudFromUniformSnapshot(stopped,stopNow).values.cpuUsage,80);
checks.push('stop floor invalidates only old CPU proof; newer independent Windows observation and filesystem remain correct');
const good=native.publications.find(x=>x.name==='independent-load').top;
for(const patch of [{cpuUsageValid:false},{cpuUsageValid:'true'},{cpuUsage:null},{cpuUsage:'80'},{cpuUsage:101},{cpuUsage:-1},{cpuUsageTs:now+.5},{cpuUsageTs:now+1},{cpuUsageTs:now-6000},{cpuUsageTs:0},{cpuUsageTs:undefined}])assert.equal(m.hudFromUniformSnapshot(admit({...good,...patch}),now).values.cpuUsage,null);
checks.push('false/malformed value and missing/future/expired independent proof never become measured CPU zero');
const legacy=m.hudFromUniformSnapshot(admit({ts:now,hwDown:false,cpuUsage:37}),now);assert.equal(legacy.values.cpuUsage,37);assert.equal(m.ageHudData(legacy,now+6000).values.cpuUsage,null);checks.push('legacy packets retain existing HWiNFO-bound aging without an invented Windows timestamp');
const keyView=admit(good),changed={...keyView,top:{...keyView.top,cpuUsage:81}};assert.notEqual(m.monitorSnapshotDisplayKey(keyView),m.monitorSnapshotDisplayKey(changed));checks.push('same legacy sequence/timestamp cannot conceal a changed Windows CPU value in display key');
const hash=s=>crypto.createHash('sha256').update(s).digest('hex');const result={ok:true,nativeChecks:native.checks,checks,operations:{fpsOnlyCpuSnapshots:{before:native.beforeFpsOnlyCpuApiCalls,after:native.fpsOnlyCpuApiCalls}},scope:'Actual extracted native sampler/top publisher, fully injected Windows APIs; uniform hub/model/expiry and desktop/Steam protocols. No real CPU sampling, benchmarking, live hardware/app/game IO.',sourcePins:{sampler:hash(sampler),publisher:hash(publisher),structure:hash(structure),demandLane:hash(lane)}};fs.writeFileSync(path.join(task,'CPU-MEASUREMENT-SELFTEST.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));