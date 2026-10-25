// Actual native start/stop, mode capture and resource sample condition; no product/hardware calls.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_FS_RESOURCE_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/unified-filesystem-demand-20261017').replaceAll('\\','/');fs.mkdirSync(out+'/native',{recursive:true});
const proposed=process.argv.includes('--proposed'),sourceFile=proposed?out+'/proposed/native/main.cpp':root+'/native/main.cpp',original=fs.readFileSync(sourceFile,'utf8'),source=original.replaceAll('\r\n','\n');
function extract(signature){const start=source.indexOf(signature);assert(start>=0,signature);const brace=source.indexOf('{',start);let depth=0;for(let i=brace;i<source.length;i++){if(source[i]==='{')++depth;else if(source[i]==='}'&&--depth===0)return source.slice(start,i+1)}throw Error(signature)}
const state=extract('struct MonitorSnapshotState')+';',mode=extract('static void nativeMonitorSetMode('),modeChange=extract('static void monitorSnapshotOnModeChange('),serve=extract('static json monitorSnapshotServeFn(');
function registration(name){const start=source.indexOf('    ipc_on("'+name+'",');assert(start>0,name);const end=source.indexOf('\n    });',start);assert(end>start);return source.slice(start,end+'\n    });'.length)}
const worker=extract('static void nativeMonitorLoop()');const reset=worker.split('\n').find(x=>x.includes('if (!top || !fileSystemDemand) fileSystemReader.reset();'));assert(reset,'resource reset is on the actual worker');
const sampling='fileSystemDemand ? fileSystemReader.sample(GetTickCount64(),static_cast<long long>(nowEpochMs)) : ymcc::monitorfs::Rates{}';assert.equal(worker.split(sampling).length,2,'exactly one conditional native sample entry');
const cpp=String.raw`
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include "__ROOT__/deps/json/json.hpp"
#include "__ROOT__/native/monitor_snapshot_contract.h"
using json=nlohmann::json;
static std::mutex g_nativeMonitorMx,g_monitorSnapshotMx;
static bool g_nativeMonitorTop=false,g_nativeMonitorFps=false,g_nativeMonitorStop=false;
static bool g_nativeMonitorFileSystem=true,g_nativeMonitorFileSystemExplicit=false;
static std::condition_variable g_nativeMonitorCv;
static std::thread g_nativeMonitorThread;
static void nativeMonitorLoop(){}
static std::atomic<unsigned long long> g_monitorRelayEpoch{0},g_monitorSnapshotServes{0};
static unsigned long long nativeMonitorSessionId(){return 7;}
__STATE__
static MonitorSnapshotState g_monitorSnapshot;
__MODE_CHANGE__
__MODE__
__SERVE__
static std::map<std::string,std::function<json(const json&)>> handlers;
template<class F>static void ipc_on(const char*name,F fn){handlers[name]=std::move(fn);}
static void bind(){__START__
__STOP__}
namespace ymcc::monitorfs{struct Rates{double readMBps=-1,writeMBps=-1;long long ts=0;};}
static unsigned long long tick=100000;static unsigned long long GetTickCount64(){return ++tick;}
struct Reader{unsigned samples=0,closes=0;bool open=false;void reset(){if(open)++closes;open=false;}ymcc::monitorfs::Rates sample(unsigned long long,long long epoch){++samples;open=true;return {10,20,epoch};}};
static ymcc::monitorfs::Rates iteration(Reader&fileSystemReader){
 bool top=false,fileSystemDemand=true;{std::lock_guard<std::mutex>lock(g_nativeMonitorMx);top=g_nativeMonitorTop;fileSystemDemand=g_nativeMonitorFileSystem;}
 __RESET__
 if(!top)return {};
 const double nowEpochMs=100000;
 return __SAMPLE__;
}
int main(int argc,char**argv){
 struct Join{~Join(){if(g_nativeMonitorThread.joinable())g_nativeMonitorThread.join();}}join;
 bind();json names=json::array();auto check=[&](const char*name,bool ok){if(!ok)throw std::runtime_error(name);names.push_back(name);};
 const auto start=[&](json args){return handlers.at("monitor.start")(args);};const auto stop=[&](json args){return handlers.at("monitor.stop")(args);};
 g_monitorSnapshot.valid=true;g_monitorSnapshot.topText="{}";g_monitorSnapshot.fpsText="{}";
 Reader reader;
 start({{"top",true},{"fileSystem",false}});for(int i=0;i<60;++i)iteration(reader);
 check("internal-only 60 top iterations make zero provider samples",reader.samples==0&&!reader.open);
 check("internal-only top remains active",g_nativeMonitorTop);
 auto view=monitorSnapshotServeFn();check("explicit disabled resource readback",view.at("topActive")==true&&view.at("fileSystemActive")==false);
 start({{"top",true},{"fileSystem",true}});for(int i=0;i<60;++i)iteration(reader);
 check("disk consumer gets existing one-sample-per-iteration cadence",reader.samples==60&&reader.open);
 check("explicit resource acknowledgement is live mode proof",monitorSnapshotServeFn().at("fileSystemActive")==true);
 start({{"top",true},{"fileSystem",false}});iteration(reader);
 check("last disk consumer release closes query without stopping top",reader.samples==60&&reader.closes==1&&!reader.open&&g_nativeMonitorTop);
 for(int i=0;i<60;++i)iteration(reader);check("disabled resource stays closed",reader.samples==60&&reader.closes==1);
 start({{"top",true}});iteration(reader);
 check("unhinted old frontend retains full data",reader.samples==61&&reader.open&&g_nativeMonitorFileSystem);
 check("unhinted old wire envelope preserves its fields",!monitorSnapshotServeFn().contains("fileSystemActive"));
 start({{"top",true},{"fileSystem",false}});start({{"fps",true},{"fileSystem",true}});
 check("FPS-only caller cannot override top resource owner",g_nativeMonitorTop&&g_nativeMonitorFps&&!g_nativeMonitorFileSystem);
 stop({{"fps",true}});check("FPS stop preserves independent top demand",g_nativeMonitorTop&&!g_nativeMonitorFps&&!g_nativeMonitorFileSystem);
 start({{"top",true},{"fileSystem",true}});iteration(reader);stop({{"top",true}});iteration(reader);
 check("top stop closes supplement even when stored request was true",!g_nativeMonitorTop&&!reader.open&&reader.closes==2);
 check("stopped resource readback cannot report active",monitorSnapshotServeFn().at("fileSystemActive")==false);
 for(const auto&bad:json::array({nullptr,0,1,"false",json::array(),json::object()})){
  const auto generation=g_monitorSnapshot.generation;bool rejected=false;try{start({{"top",true},{"fileSystem",bad}});}catch(const json::exception&){rejected=true;}
  check("malformed optional hint rejected before mode change",rejected&&g_monitorSnapshot.generation==generation&&!g_nativeMonitorTop);
 }
 start({{"top",true},{"fileSystem",false}});g_monitorSnapshot.valid=false;view=monitorSnapshotServeFn();
 check("unavailable envelope still acknowledges actual resource configuration",view.at("available")==false&&view.at("fileSystemActive")==false);
 std::atomic<bool>failed=false;std::thread writer([&]{for(int i=0;i<2000;++i)nativeMonitorSetMode(true,false,true,i%2!=0,true);});
 for(int i=0;i<2000;++i){const auto row=monitorSnapshotServeFn();if(!row.contains("fileSystemActive")||row.at("topActive")!=true)failed=true;}
 writer.join();check("mode/readback lock order remains safe under concurrent changes",!failed);
 json result={{"ok",true},{"cases",names},{"providerWork",{{"internalTopIterations",120},{"internalProviderSamples",0},{"diskTopIterations",60},{"diskProviderSamples",60}}},{"raceReads",2000},{"raceModeChanges",2000}};
 std::ofstream(argv[1])<<result.dump(2);std::cout<<"PASS native filesystem demand checks="<<names.size()<<"\n";
}
`;
const code=cpp.replaceAll('__ROOT__',root).replace('__STATE__',state).replace('__MODE_CHANGE__',modeChange).replace('__MODE__',mode).replace('__SERVE__',serve).replace('__START__',registration('monitor.start')).replace('__STOP__',registration('monitor.stop')).replace('__RESET__',reset).replace('__SAMPLE__',sampling);
fs.writeFileSync(out+'/native/native-resource.cpp',code);
const cmd='@echo off\r\ncall "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 "'+out+'/native/native-resource.cpp" /Fo"'+out+'/native/native-resource.obj" /Fe"'+out+'/native/native-resource.exe"\r\nif errorlevel 1 exit /b 1\r\n"'+out+'/native/native-resource.exe" "'+out+'/native/RESULT.json"\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(out+'/native/run.cmd',cmd);
const run=spawnSync('cmd.exe',['/d','/c',out+'/native/run.cmd'],{encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:3e6});fs.writeFileSync(out+'/native/BUILD-RUN.log',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,run.stdout+run.stderr);const result=JSON.parse(fs.readFileSync(out+'/native/RESULT.json'));result.sourceSha256=crypto.createHash('sha256').update(original).digest('hex');result.scope='Actual native IPC registrations, mode setter, snapshot serve, and exact worker resource condition; inert filesystem provider and no-op collector thread. No real process/hardware access or whole-process CPU measurement.';fs.writeFileSync(out+'/native/RESULT.json',JSON.stringify(result,null,2));console.log(JSON.stringify({ok:true,cases:result.cases.length,providerWork:result.providerWork,raceReads:result.raceReads,raceModeChanges:result.raceModeChanges},null,2));
