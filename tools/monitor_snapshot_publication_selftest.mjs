// Execute extracted production publish/serve/file-commit functions with only
// in-memory file sinks. No real app, devices, file relays, games or CPU timing.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const root=process.cwd(),task=path.resolve(process.env.YMCC_MONITOR_PUBLICATION_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/snapshot-boundary-repair-20261016');fs.mkdirSync(task,{recursive:true});
const before=fs.readFileSync(path.join(task,'before/native/main.cpp'),'utf8'),after=fs.readFileSync('native/main.cpp','utf8');
function extract(source,signature){const start=source.indexOf(signature);assert(start>=0,signature);const tail=source.slice(start),masked=tail.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>' '.repeat(m.length));let depth=0;for(let i=masked.indexOf('{');i<masked.length;i++){if(masked[i]==='{')depth++;if(masked[i]==='}'&&!--depth)return tail.slice(0,i+1);}throw Error('missing end '+signature);}
const signatures=['static void monitorWriteSnapshotGuarded(','static void monitorSnapshotPublish(','static void monitorSnapshotPublishFrames(','static json monitorSnapshotServeFn()'];
const stateStart=after.indexOf('struct MonitorSnapshotState {'),stateEnd=after.indexOf('\n};',stateStart);assert(stateStart>=0&&stateEnd>stateStart);const state=after.slice(stateStart,stateEnd+3);
const prelude=String.raw`
#include <string>
#include <string_view>
#include <atomic>
#include <mutex>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>
#include "monitor_snapshot_contract.h"
#include "json.hpp"
using json=nlohmann::json;
`;
const stubs=String.raw`
static std::mutex g_monitorSnapshotMx,g_nativeMonitorMx,g_monitorRelayMx;
static MonitorSnapshotState g_monitorSnapshot;
static std::atomic<unsigned long long> g_monitorRelayEpoch{0},g_monitorSnapshotServes{0},g_monitorSnapshotOversizeDrops{0},g_monitorRelayCommits{0};
static std::atomic<bool> g_monitorFileRelayOff{false};
static bool g_nativeMonitorTop=false,g_nativeMonitorFps=false,invalidateAtBarrier=false;
static const std::wstring MONITOR_TOP_JSON=L"topmon.json",MONITOR_FPS_JSON=L"fps-status.json";
static std::map<std::wstring,std::string> files;static unsigned writes=0;
static void monitorTestBarrierPause(int point){if(point==1&&invalidateAtBarrier){++g_monitorSnapshot.generation;invalidateAtBarrier=false;}}
static unsigned long long nativeMonitorSessionId(){return 77;}
static bool DeleteFileW(const wchar_t* name){files.erase(name);return true;}
static bool sgWriteMonitorSnapshotDurable(const std::wstring& p,const std::string& s){files[p]=s;++writes;return true;}
static bool sgWriteMonitorSnapshotTransient(const std::wstring& p,const std::string& s){files[p]=s;++writes;return true;}
static std::string doc(std::size_t n){return "{\"p\":\""+std::string(n-8,'x')+"\"}";}
static void reset(){g_monitorSnapshot={};g_monitorSnapshot.generation=7;g_monitorSnapshotOversizeDrops=0;g_monitorSnapshotServes=0;g_monitorRelayCommits=0;g_monitorFileRelayOff=false;g_nativeMonitorTop=g_nativeMonitorFps=true;files.clear();writes=0;invalidateAtBarrier=false;}
`;
const tests=String.raw`
static json run(){json cases=json::array();auto check=[&](const char* name,bool ok){cases.push_back({{"name",name},{"ok",ok}});};
using namespace ymcc::monitorsnapshot;
reset();files[MONITOR_TOP_JSON]="old";monitorWriteSnapshotGuarded(MONITOR_TOP_JSON,false,doc(topMaxBytes+1));check("completed oversized top document is neither written nor left stale",writes==0&&!files.count(MONITOR_TOP_JSON));
reset();files[MONITOR_FPS_JSON]="old";monitorWriteSnapshotGuarded(MONITOR_FPS_JSON,true,doc(fpsMaxBytes+1));check("oversized FPS file is neither written nor left stale",writes==0&&!files.count(MONITOR_FPS_JSON));
reset();for(const auto& file:{MONITOR_TOP_JSON,MONITOR_FPS_JSON})monitorWriteSnapshotGuarded(file,true,doc(topMaxBytes));check("exact 16KiB documents remain whole valid JSON",writes==2&&files.size()==2&&json::parse(files[MONITOR_TOP_JSON]).is_object());
reset();g_monitorFileRelayOff=true;files[MONITOR_TOP_JSON]="sentinel";monitorWriteSnapshotGuarded(MONITOR_TOP_JSON,false,doc(topMaxBytes+1));check("takeover blocks all file edits and drop counting",writes==0&&files[MONITOR_TOP_JSON]=="sentinel"&&g_monitorSnapshotOversizeDrops==0&&g_monitorRelayCommits==0);
reset();monitorWriteSnapshotGuarded(L"heartbeat",true,"123");check("non-payload relay files retain the original commit semantics",writes==1&&files[L"heartbeat"]=="123");
reset();monitorSnapshotPublish(7,true,true,true,1000,doc(topMaxBytes),doc(combinedMaxBytes-topMaxBytes));check("exact combined limit retains both complete JSON families",g_monitorSnapshot.topText.size()==topMaxBytes&&g_monitorSnapshot.fpsText.size()==combinedMaxBytes-topMaxBytes);
const auto topPtr=g_monitorSnapshot.topText.data();monitorSnapshotPublishFrames(7,1100,doc(combinedMaxBytes-topMaxBytes));check("ordinary frame publication retains hardware text without a copy",g_monitorSnapshot.topText.data()==topPtr&&g_monitorSnapshot.hwinfoOkAt==1000&&!g_monitorSnapshot.hwDown);
monitorSnapshotPublishFrames(7,1200,doc(combinedMaxBytes-topMaxBytes+1));check("fast-frame combined limit bounds retained cache before any serve",g_monitorSnapshot.topText.empty()&&g_monitorSnapshot.fpsText.size()==combinedMaxBytes-topMaxBytes+1&&g_monitorSnapshotOversizeDrops==1);
check("frame combined overflow never renews hardware health time",g_monitorSnapshot.hwinfoOkAt==1000&&!g_monitorSnapshot.hwDown&&g_monitorSnapshot.heartbeatAt==1200);
reset();monitorSnapshotPublish(7,true,true,true,1000,doc(topMaxBytes),doc(fpsMaxBytes));check("full publication enforces combined limit and retains FPS",g_monitorSnapshot.topText.empty()&&g_monitorSnapshot.fpsText.size()==fpsMaxBytes&&g_monitorSnapshotOversizeDrops==1);
reset();monitorSnapshotPublish(7,true,true,true,1000,doc(topMaxBytes+1),doc(32));check("oversized top cannot erase an independent FPS family",g_monitorSnapshot.topText.empty()&&g_monitorSnapshot.fpsText==doc(32));
reset();monitorSnapshotPublish(7,true,true,true,1000,doc(32),doc(fpsMaxBytes+1));check("oversized FPS cannot erase an independent hardware family",g_monitorSnapshot.topText==doc(32)&&g_monitorSnapshot.fpsText.empty());
check("rejected document capacity is actually released",g_monitorSnapshot.fpsText.capacity()<1024);
reset();std::string top="{}",fps="{}";top.reserve(1024*1024);fps.reserve(1024*1024);monitorSnapshotPublish(7,true,true,true,1000,std::move(top),std::move(fps));check("small text cannot retain megabytes through reserved capacity",g_monitorSnapshot.topText=="{}"&&g_monitorSnapshot.fpsText=="{}"&&g_monitorSnapshot.topText.capacity()<1024&&g_monitorSnapshot.fpsText.capacity()<1024);
std::string reserve="{}";reserve.reserve(1024*1024);monitorSnapshotPublishFrames(7,1100,std::move(reserve));check("fast FPS publication compacts oversized reserve",g_monitorSnapshot.fpsText=="{}"&&g_monitorSnapshot.fpsText.capacity()<1024);
reset();monitorSnapshotPublish(7,true,true,true,1000,doc(32),doc(32));const auto saved=g_monitorSnapshot;const auto drops=g_monitorSnapshotOversizeDrops.load();invalidateAtBarrier=true;monitorSnapshotPublishFrames(7,1100,doc(fpsMaxBytes+1));check("invalidated frame publication cannot advance cache or drop counts",g_monitorSnapshot.topText==saved.topText&&g_monitorSnapshot.fpsText==saved.fpsText&&g_monitorSnapshot.seq==saved.seq&&g_monitorSnapshot.collectedAt==saved.collectedAt&&g_monitorSnapshotOversizeDrops==drops);
reset();monitorSnapshotPublishFrames(7,1000,doc(32));check("frame cannot revive an unavailable cache",!g_monitorSnapshot.valid&&g_monitorSnapshot.seq==0);
reset();monitorSnapshotPublish(7,true,true,true,1000,doc(topMaxBytes+1),doc(fpsMaxBytes+1));monitorSnapshotPublish(7,true,true,true,2000,doc(24),doc(24));auto reply=monitorSnapshotServeFn();check("a valid next publication recovers complete independent documents",reply["top"]==doc(24)&&reply["fps"]==doc(24)&&g_monitorSnapshot.collectedAt==2000);
const auto count=g_monitorSnapshotOversizeDrops.load();for(int i=0;i<50;++i)monitorSnapshotServeFn();check("reads cannot multiply producer drop counts or modify source",g_monitorSnapshotOversizeDrops==count&&g_monitorSnapshot.topText==doc(24)&&g_monitorSnapshot.fpsText==doc(24));
reset();monitorSnapshotPublish(7,false,false,true,1000,doc(topMaxBytes+1),doc(fpsMaxBytes+1));check("inactive families retain no text and do not count drops",g_monitorSnapshot.topText.empty()&&g_monitorSnapshot.fpsText.empty()&&g_monitorSnapshotOversizeDrops==0&&g_monitorSnapshot.heartbeatAt==0);
reset();monitorSnapshotPublish(7,true,true,true,1000,doc(32),doc(32));g_monitorSnapshot.topText=doc(topMaxBytes);g_monitorSnapshot.fpsText=doc(fpsMaxBytes);reply=monitorSnapshotServeFn();check("defensive IPC boundary bounds a corrupted combined cache",!reply.contains("top")&&reply["fps"]==doc(fpsMaxBytes)&&reply["snapshotOversize"]==true&&g_monitorSnapshotOversizeDrops==0);
return cases;}
`;
const parts={before:{},after:{}};for(const [side,source] of Object.entries({before,after}))for(const sig of signatures){if(side==='after'&&sig==='static void monitorSnapshotPublishFrames('&&source.includes('static void monitorSnapshotPublishFramesBorrowed('))parts[side]['static void monitorSnapshotPublishFramesBorrowed(']=extract(source,'static void monitorSnapshotPublishFramesBorrowed(');parts[side][sig]=extract(source,sig);}
const code=prelude+'\n'+state+'\n'+Object.entries(parts).map(([name,functions])=>'namespace '+name+'{\n'+stubs+'\n'+Object.values(functions).join('\n')+'\n'+tests+'\n}').join('\n')+String.raw`
int main(int argc,char**argv){if(argc!=2)return 2;const auto a=before::run(),b=after::run();unsigned failed=0;for(const auto& c:b)if(!c["ok"].get<bool>()){++failed;std::cerr<<"FAIL "<<c["name"]<<"\n";}json result={{"ok",failed==0},{"before",a},{"after",b}};std::ofstream(argv[1])<<result.dump(2);std::cout<<"Publication scenarios: "<<b.size()<<", "<<failed<<" failed\n";return failed?2:0;}
`;
fs.writeFileSync(path.join(task,'publication.cpp'),code);const script=path.join(task,'build-publication.cmd');fs.writeFileSync(script,`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 "${task}/publication.cpp" /I"${root}/native" /I"${root}/deps/json" /Fo"${task}/publication.obj" /Fe"${task}/publication.exe"\r\nif errorlevel 1 exit /b 1\r\n"${task}/publication.exe" "${task}/PUBLICATION-RESULT.json"\r\nexit /b %errorlevel%\r\n`);
const run=spawnSync('cmd.exe',['/d','/c',script],{cwd:task,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:2e6});fs.writeFileSync(path.join(task,'PUBLICATION-BUILD.log'),(run.stdout||'')+(run.stderr||''));console.log((run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,'native fixture');
const result=JSON.parse(fs.readFileSync(path.join(task,'PUBLICATION-RESULT.json'),'utf8'));const reproduced=result.before.filter(c=>!c.ok).map(c=>c.name);assert(reproduced.includes('completed oversized top document is neither written nor left stale'));assert(reproduced.includes('fast-frame combined limit bounds retained cache before any serve'));assert(reproduced.includes('small text cannot retain megabytes through reserved capacity'));assert(result.after.every(c=>c.ok));
fs.writeFileSync(path.join(task,'SOURCE-PINS.json'),JSON.stringify({functions:Object.fromEntries(Object.entries(parts).map(([side,fns])=>[side,Object.fromEntries(Object.entries(fns).map(([sig,text])=>[sig,crypto.createHash('sha256').update(text).digest('hex')]))])),header:crypto.createHash('sha256').update(fs.readFileSync('native/monitor_snapshot_contract.h')).digest('hex')},null,2));console.log(JSON.stringify({ok:true,scenarios:result.after.length,reproduced,scope:'Actual extracted production functions; fake in-memory file sinks, no real writes/app/hardware/CPU measurement.'},null,2));
