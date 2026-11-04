import {selectedNativeTools,HOST_ENVIRONMENT_SOURCE_INPUTS} from './ymcc_sim/global/release-host-environment.mjs';
import {getNativeLaunchNoIoToolDeclaration} from './ymcc_sim/global/decky-update-recovery/native-launch-noio/index.mjs';
// Actual native manual-repair orchestrator and production TS bridge. Memory-only
// session/mode/resource/dispatch ports; never starts Steam or bypasses its checks.
// Default/imported execution keeps all native + UI cases. Explicit CLI
// --native-case "exact name" (repeatable) runs only those native cases; its
// receipt is never completeRegression. Selection is never inherited from env.
import {performance} from 'node:perf_hooks';import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';import {createRequire} from 'node:module';
export async function retainedSessionRepairTests(root,out,options={}){
 const selectedCases=options.nativeCases??null;if(selectedCases!==null){assert(Array.isArray(selectedCases)&&selectedCases.length>0,"Native case selection must not be empty");assert(selectedCases.every(x=>typeof x==="string"&&x.length>0),"Native case names must be nonempty exact strings");assert.equal(new Set(selectedCases).size,selectedCases.length,"Duplicate native case selection");}
 fs.mkdirSync(out,{recursive:true});const require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild'),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
 const rels=[...HOST_ENVIRONMENT_SOURCE_INPUTS,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/index.mjs','tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json','native/decky_sidebar_manual_repair.h','native/decky_sidebar_automatic_repair.h','native/steam_ui_epoch.h','native/decky_update_recovery.h','native/steam_session_observer.h','native/steam_window_event_budget.h','src/bridge/deckySidebar.ts','tools/decky_retained_session_repair_selftest.mjs','tools/ymcc_sim/global/decky-update-recovery/field-record-retained-session-rogx-20261023.json'];rels.push('tools/decky_sidebar_broker_selftest.cpp');const pins=Object.fromEntries(rels.map(p=>[p,sha(fs.readFileSync(path.join(root,p)))]));
 const nativeSeen=new Set(rels);function addHeader(rel){if(nativeSeen.has(rel))return;nativeSeen.add(rel);rels.push(rel);for(const match of fs.readFileSync(path.join(root,rel),'utf8').matchAll(/#include\s+"([^"]+)"/g)){const next=path.posix.normalize(path.posix.join(path.posix.dirname(rel),match[1]));if(next.startsWith('native/')&&fs.existsSync(path.join(root,next)))addHeader(next);}}
 for(const p of ['native/decky_binding_worker_entry.h','native/decky_context_cleanup_worker.h','native/settings_baseline.h','native/steam_ui_mode_observer.h','native/decky_sidebar_diagnostics.h','native/decky_sidebar_broker.h','native/decky_sidebar_runtime.h'])addHeader(p);
 for(const p of [...rels].filter(p=>p.startsWith('native/'))){for(const match of fs.readFileSync(path.join(root,p),'utf8').matchAll(/#include\s+"([^"]+)"/g)){const next=path.posix.normalize(path.posix.join(path.posix.dirname(p),match[1]));if(next.startsWith('native/')&&fs.existsSync(path.join(root,next)))addHeader(next);}}
 for(const p of rels)if(!pins[p])pins[p]=sha(fs.readFileSync(path.join(root,p)));
 const frozen=path.join(out,'source/native');fs.mkdirSync(frozen,{recursive:true});for(const p of rels.filter(p=>p.startsWith('native/')))fs.copyFileSync(path.join(root,p),path.join(frozen,path.basename(p)));
 const override=process.env.YMCC_RETAINED_REPAIR_HEADER_OVERRIDE;if(override)fs.copyFileSync(override,path.join(frozen,'decky_sidebar_manual_repair.h'));
 const managedOverride=process.env.YMCC_MANAGED_CONTINUATION_HEADER_OVERRIDE;if(managedOverride)fs.copyFileSync(managedOverride,path.join(frozen,'decky_update_recovery.h'));
 const cleanupOverride=process.env.YMCC_FINAL_CLEANUP_HEADER_OVERRIDE;if(cleanupOverride)fs.copyFileSync(cleanupOverride,path.join(frozen,'decky_sidebar_automatic_repair.h'));
 const workerOverride=process.env.YMCC_CONTEXT_CLEANUP_WORKER_OVERRIDE;if(workerOverride)fs.copyFileSync(workerOverride,path.join(frozen,'decky_context_cleanup_worker.h'));
 const runtimeOverride=process.env.YMCC_RUNTIME_SCHEDULER_HEADER_OVERRIDE;if(runtimeOverride)fs.copyFileSync(runtimeOverride,path.join(frozen,'decky_sidebar_runtime.h'));
 // Compile the actual native dependency callback as well as the shared
 // orchestrators. Other main.cpp regions are not this consumer's inputs.
 const extractDispatch=()=>{
  const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
  const start='[](const auto& session,const auto& args,const auto& current)->json{';
  const end='\n        },\n        []{return currentPowerGeneration();},';
  const a=main.indexOf(start),b=main.indexOf(end,a);
  assert(a>=0&&b>a&&main.indexOf(start,a+start.length)<0,'Actual typed cleanup dependency callback moved; review the original consumer');
  return main.slice(a,b+'\n        }'.length);
 };
 const extractLifecycleWriter=()=>{
  const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
  const a=main.indexOf('static json deckySidebarManagedLifecycleRecord('),b=main.indexOf('\nstatic bool deckySidebarRunContextCleanupWorker(',a);
  assert(a>=0&&b>a,'Actual native lifecycle record writer moved; review original consumer');return main.slice(a,b);
 };
 const extractLifecycleService=()=>{
  const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
  const a=main.indexOf('static void deckySidebarServiceManagedReadback(){'),b=main.indexOf('\nstatic void deckySidebarObserveAutomaticRepair(){',a);
  assert(a>=0&&b>a,'Actual native lifecycle service moved; review original consumer');return main.slice(a,b);
 };
 const extractManagedReadyState=()=>{
  const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
  const a=main.indexOf('struct DeckyManagedReadyWitness {'),b=main.indexOf('\nstatic bool deckySidebarAcquireManagedBackend(',a);
  assert(a>=0&&b>a,'Actual managed ready state/queue moved');return main.slice(a,b);
 };
 const productionManagedReadyState=extractManagedReadyState(),readyStateOverride=process.env.YMCC_MANAGED_READY_STATE_OVERRIDE;
 const compiledManagedReadyState=readyStateOverride?fs.readFileSync(readyStateOverride,'utf8'):productionManagedReadyState;
 fs.writeFileSync(path.join(out,'actual-main-managed-ready-state.json'),JSON.stringify({path:'native/main.cpp',sha256:sha(Buffer.from(productionManagedReadyState)),source:productionManagedReadyState},null,2)+'\n');
 const extractStableModeCallback=()=>{
  const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
  const marker='modeObserver.stableBigPicture = ';const a=main.indexOf(marker),b=main.indexOf('\n    g_steamModeObserver.initialize(',a);
  assert(a>=0&&b>a,'Actual stable shared-mode callback moved');const expression=main.slice(a+marker.length,b).trim();assert(expression.endsWith(';'));return expression.slice(0,-1);
 };
 const productionStableModeCallback=extractStableModeCallback(),stableModeOverride=process.env.YMCC_STABLE_MODE_CALLBACK_OVERRIDE;
 const compiledStableModeCallback=stableModeOverride?fs.readFileSync(stableModeOverride,'utf8'):productionStableModeCallback;
 fs.writeFileSync(path.join(out,'actual-main-stable-mode-callback.json'),JSON.stringify({path:'native/main.cpp',sha256:sha(Buffer.from(productionStableModeCallback)),source:productionStableModeCallback},null,2)+'\n');
 const extractManualReceiptCallback=()=>{
  const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
  const start='[](const auto& session,const json& mode,unsigned long long power,unsigned long long manualEpoch){';
  const a=main.indexOf(start),b=main.indexOf('\n        }};',a);
  assert(a>=0&&b>a&&main.indexOf(start,a+1)<0,'Actual manual provenance callback moved');return main.slice(a,b+'\n        }'.length);
 };
 const extractParentHandleSnapshot=()=>{
  const origin=fs.readFileSync(path.join(root,'tools/decky_binding_runtime_selftest.cpp'),'utf8').replace(/\r\n/g,'\n');
  const a=origin.indexOf('inline Json handleTable(){'),b=origin.indexOf('\ninline DWORD handles()',a);
  assert(a>=0&&b>a,'Original parent-handle diagnostic moved; do not introduce a duplicate implementation');return origin.slice(a,b);
 };
 const productionParentHandleSnapshot=extractParentHandleSnapshot();
 fs.writeFileSync(path.join(out,'parent-handle-snapshot-source.json'),JSON.stringify({path:'tools/decky_binding_runtime_selftest.cpp',sha256:sha(Buffer.from(productionParentHandleSnapshot)),source:productionParentHandleSnapshot,scope:'Exact existing self-process handle snapshot helper; no external process reads or new permanent diagnostic runner'},null,2)+'\n');
 const productionManualReceiptCallback=extractManualReceiptCallback();
 fs.writeFileSync(path.join(out,'actual-main-manual-receipt.json'),JSON.stringify({path:'native/main.cpp',sha256:sha(Buffer.from(productionManualReceiptCallback)),source:productionManualReceiptCallback},null,2)+'\n');
 const extractLifecycleChildGate=()=>{
  const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
  const a=main.indexOf('        if(request.lifecycle){'),b=main.indexOf('\n        deps.mode=deps.freshMode;',a);
  assert(a>=0&&b>a&&main.indexOf('        if(request.lifecycle){',a+1)<0,'Actual private lifecycle child gate moved');return main.slice(a,b);
 };
 const productionLifecycleChildGate=extractLifecycleChildGate(),childGateOverride=process.env.YMCC_LIFECYCLE_CHILD_GATE_OVERRIDE;
 const compiledLifecycleChildGate=childGateOverride?fs.readFileSync(childGateOverride,'utf8'):productionLifecycleChildGate;
 fs.writeFileSync(path.join(out,'actual-main-lifecycle-child-gate.json'),JSON.stringify({path:'native/main.cpp',sha256:sha(Buffer.from(productionLifecycleChildGate)),compiledSha256:sha(Buffer.from(compiledLifecycleChildGate)),source:productionLifecycleChildGate,compiled:compiledLifecycleChildGate,scope:'Actual child lifecycle branch; current mode/build and query-only owned backend PID/birth/path/session proof precede settings metadata IO. Explicit temporary override is mutation-only.'},null,2)+'\n');
 const guardMain=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
 const guardAt=guardMain.indexOf('    try{deckySidebarServiceManagedLifecycle();}catch(...)',guardMain.indexOf('static void deckySidebarObserveAutomaticRepair(){'));
 const guardEnd=guardMain.indexOf('\n',guardAt);assert(guardAt>=0&&guardEnd>guardAt,'Actual managed observer catch moved');
 const productionManagedObserverGuard=guardMain.slice(guardAt,guardEnd);
 fs.writeFileSync(path.join(out,'actual-main-managed-observer-guard.json'),JSON.stringify({path:'native/main.cpp',source:productionManagedObserverGuard,sha256:sha(Buffer.from(productionManagedObserverGuard)),scope:'Exact existing catch expression only. Not full Runtime/Automatic observe integration; tested power level is explicit Ready.'},null,2)+'\n');
 const productionLifecycleService=extractLifecycleService(),lifecycleServiceOverride=process.env.YMCC_LIFECYCLE_SERVICE_OVERRIDE;
 const serviceInput=lifecycleServiceOverride?fs.readFileSync(lifecycleServiceOverride,'utf8'):productionLifecycleService;
 let compiledLifecycleService=serviceInput;
 for(const [from,to]of [['ymcc::deckymirror::frontendBuildId','fixtureFrontendBuildId'],['ymcc::deckyrepair::cleanupworker::dispatch','fixtureLifecycleDispatch'],['GetTickCount64()','fixtureTick()']]){
  const count=compiledLifecycleService.split(from).length-1;assert(from==='GetTickCount64()'?count===8:count===2,'Lifecycle service IO boundary moved: '+from);compiledLifecycleService=compiledLifecycleService.replaceAll(from,to);
 }
 fs.writeFileSync(path.join(out,'actual-main-lifecycle-service.json'),JSON.stringify({path:'native/main.cpp',sourceSha256:sha(Buffer.from(productionLifecycleService)),compiledSha256:sha(Buffer.from(compiledLifecycleService)),source:productionLifecycleService,compiled:compiledLifecycleService,adaptedIo:['frontend build prefix read','private worker dispatch'],realPrivateWorkerExecutedSeparately:true},null,2)+'\n');
 const productionLifecycleWriter=extractLifecycleWriter(),lifecycleWriterOverride=process.env.YMCC_LIFECYCLE_WRITER_OVERRIDE;
 const compiledLifecycleWriter=lifecycleWriterOverride?fs.readFileSync(lifecycleWriterOverride,'utf8'):productionLifecycleWriter;
 fs.writeFileSync(path.join(out,'actual-main-lifecycle-writer.json'),JSON.stringify({path:'native/main.cpp',sha256:sha(Buffer.from(productionLifecycleWriter)),source:productionLifecycleWriter},null,2)+'\n');
 const productionCleanupDispatch=extractDispatch(),dispatchOverride=process.env.YMCC_CLEANUP_DISPATCH_OVERRIDE;
 const compiledCleanupDispatch=dispatchOverride?fs.readFileSync(dispatchOverride,'utf8'):productionCleanupDispatch;
 fs.writeFileSync(path.join(out,'actual-main-cleanup-dispatch.json'),JSON.stringify({path:'native/main.cpp',sourceSha256:sha(Buffer.from(productionCleanupDispatch)),compiledSha256:sha(Buffer.from(compiledCleanupDispatch)),override:dispatchOverride?{path:dispatchOverride,sha256:sha(fs.readFileSync(dispatchOverride))}:null,productionSource:productionCleanupDispatch},null,2)+'\n');
 const frameFixtureRel='tools/decky_binding_client_selftest.cpp';pins[frameFixtureRel]=sha(fs.readFileSync(path.join(root,frameFixtureRel)));
 const frameFixture=fs.readFileSync(path.join(root,frameFixtureRel),'utf8').replace(/\r\n/g,'\n');
 const writerAt=frameFixture.indexOf('inline bool writeRaw(ChildChannel& channel,');
 const writerEnd=frameFixture.indexOf('\ninline bool tryRunChild(',writerAt);assert(writerAt>=0&&writerEnd>writerAt);
 const rawWriter=frameFixture.slice(writerAt,writerEnd);
 assert.equal(rawWriter.split('channel.pipeHandle()').length-1,2);
 const compiledRawWriter=rawWriter.replace('writeRaw(ChildChannel& channel,','writeRaw(HANDLE pipe,').replaceAll('channel.pipeHandle()','pipe');
 fs.writeFileSync(path.join(out,'actual-original-raw-frame-writer.json'),JSON.stringify({path:frameFixtureRel,source:rawWriter,sourceSha256:sha(Buffer.from(rawWriter)),compiled:compiledRawWriter,compiledSha256:sha(Buffer.from(compiledRawWriter)),scope:'Existing borrowed pipe raw writer only; argument adapted to already-authenticated ChildChannel endpoint. Actual production EventReader/dispatch are unchanged.'},null,2)+'\n');
 const extractNativeMirrorReply=()=>{
  const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
  const marker='    ipc_on("deckySidebar.mirrorReply", ';const a=main.indexOf(marker),end=main.indexOf('\n    });',a);
  assert(a>=0&&end>a&&main.indexOf(marker,a+1)<0,'Actual mirrorReply entry moved');return main.slice(a+marker.length,end)+'\n    }';
 };
 const productionNativeMirrorReply=extractNativeMirrorReply(),nativeMirrorReplyOverride=process.env.YMCC_NATIVE_MIRROR_REPLY_OVERRIDE;
 const compiledNativeMirrorReply=nativeMirrorReplyOverride?fs.readFileSync(nativeMirrorReplyOverride,'utf8'):productionNativeMirrorReply;
 fs.writeFileSync(path.join(out,'actual-main-native-mirror-reply.json'),JSON.stringify({path:'native/main.cpp',source:productionNativeMirrorReply,sha256:sha(Buffer.from(productionNativeMirrorReply)),compiled:compiledNativeMirrorReply,compiledSha256:sha(Buffer.from(compiledNativeMirrorReply)),override:nativeMirrorReplyOverride?{path:nativeMirrorReplyOverride,sha256:sha(fs.readFileSync(nativeMirrorReplyOverride))}:null,scope:'Exact native IPC lambda, business snapshot/Runtime ownership are ports; broker/socket actual.'},null,2)+'\n');
 const brokerFixture=fs.readFileSync(path.join(root,'tools/decky_sidebar_broker_selftest.cpp'),'utf8').replace(/\r\n/g,'\n');
 const reqAt=brokerFixture.indexOf('static std::string request('),protoAt=brokerFixture.indexOf('\nstatic int protocolTests(){',reqAt);
 const sockAt=brokerFixture.indexOf('struct LoopbackSocket {'),progressAt=brokerFixture.indexOf('\n    void allowProgress(){',sockAt);
 assert(reqAt>=0&&protoAt>reqAt&&sockAt>=0&&progressAt>sockAt,'Original loopback helper moved');
 const waitAt=brokerFixture.indexOf('static bool waitUntil(');assert(waitAt>=0&&sockAt>waitAt,'Original broker completion wait moved');
 const originalBrokerClient=brokerFixture.slice(reqAt,protoAt)+'\n'+brokerFixture.slice(waitAt,sockAt)+'\n'+brokerFixture.slice(sockAt,progressAt)+'\n};';
 fs.writeFileSync(path.join(out,'actual-original-broker-client.json'),JSON.stringify({path:'tools/decky_sidebar_broker_selftest.cpp',source:originalBrokerClient,sha256:sha(Buffer.from(originalBrokerClient)),scope:'Original handshake/masked/send/read only; excludes unrelated backpressure method.'},null,2)+'\n');
 const recoveryTimeoutMain=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
 const recoveryTimeoutAt=recoveryTimeoutMain.indexOf('static DWORD deckySidebarConnectionRecoveryTimeout(){'),recoveryTimeoutEnd=recoveryTimeoutMain.indexOf('\nstatic void deckySidebarObserveAutomaticRepair(){',recoveryTimeoutAt);
 assert(recoveryTimeoutAt>=0&&recoveryTimeoutEnd>recoveryTimeoutAt,'Original Runtime recovery timeout callback moved');
 const productionRecoveryTimeout=recoveryTimeoutMain.slice(recoveryTimeoutAt,recoveryTimeoutEnd),compiledRecoveryTimeout=productionRecoveryTimeout.replace('GetTickCount64()','fixtureTick()');
 assert.equal(productionRecoveryTimeout.split('GetTickCount64()').length-1,1,'Clock port count changed');
 fs.writeFileSync(path.join(out,'actual-main-recovery-timeout.json'),JSON.stringify({source:productionRecoveryTimeout,sha256:sha(Buffer.from(productionRecoveryTimeout)),compiled:compiledRecoveryTimeout,compiledSha256:sha(Buffer.from(compiledRecoveryTimeout)),adaptedIo:['clock']},null,2)+'\n');
 const backendMain=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
 const backendAt=backendMain.indexOf('static bool deckySidebarAcquireManagedBackend('),backendEnd=backendMain.indexOf('\nstatic void deckySidebarServiceManagedReadback(){',backendAt);
 assert(backendAt>=0&&backendEnd>backendAt,'Original backend proof source moved');
 const productionBackendProof=backendMain.slice(backendAt,backendEnd),backendProofOverride=process.env.YMCC_MANAGED_BACKEND_PROOF_OVERRIDE;
 const compiledBackendProof=backendProofOverride?fs.readFileSync(backendProofOverride,'utf8'):productionBackendProof;
 assert.equal(compiledBackendProof.split('GetExtendedTcpTable(').length-1,2,'OS query boundary changed');assert.equal(compiledBackendProof.split('==1337').length-1,1,'Listener port boundary changed');
 const classifierBackendProof=compiledBackendProof.replaceAll('GetExtendedTcpTable(','fixtureGetExtendedTcpTable(').replace('==1337','==backendPort');
 fs.writeFileSync(path.join(out,'actual-main-backend-proof.json'),JSON.stringify({path:'native/main.cpp',source:productionBackendProof,sha256:sha(Buffer.from(productionBackendProof)),compiledProductSource:compiledBackendProof,compiledProductSha256:sha(Buffer.from(compiledBackendProof)),classifierSource:classifierBackendProof,classifierSha256:sha(Buffer.from(classifierBackendProof)),adaptedIo:['ephemeral loopback test port','OS query fault wrapper; default calls actual GetExtendedTcpTable'],override:backendProofOverride?{path:backendProofOverride,sha256:sha(fs.readFileSync(backendProofOverride))}:null},null,2)+'\n');
 const native=String.raw`#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "decky_sidebar_automatic_repair.h"
#include "decky_binding_worker_entry.h"
#include "decky_sidebar_runtime.h"
#include <fstream>
#include <iostream>
#include <set>
#include <map>
#include "settings_baseline.h"
#include "steam_ui_mode_observer.h"
#include "decky_sidebar_diagnostics.h"
#include "decky_sidebar_broker.h"
using Json=nlohmann::json;using json=nlohmann::json;using namespace ymcc::deckyrepair;

namespace actualBrokerClient {
using namespace ymcc::deckymirror;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
${originalBrokerClient}
}
namespace jointresource {
${productionParentHandleSnapshot}
// Diagnostic-only ordinary child, contained by the existing private worker Job.
// Its process creation uses plain Win32, not Decky Owner/Client/protocol code.
static int plainProcessCreationControl(const std::filesystem::path& output){
 Json report={{"ok",false},{"control","plain-win32-create-suspended-close"},{"writerPid",GetCurrentProcessId()},{"rows",Json::array()}};
 try{
  wchar_t image[32768]{};const DWORD length=GetModuleFileNameW(nullptr,image,32768);if(!length||length>=32768)throw std::runtime_error("control image");
  for(unsigned n=0;n<3;++n){
   const auto before=handleTable();DWORD countBefore=0;GetProcessHandleCount(GetCurrentProcess(),&countBefore);
   std::wstring command=L"\""+std::wstring(image,length)+L"\" --inert-never-resumed";
   STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
   if(!CreateProcessW(image,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&start,&process))throw std::runtime_error("plain control create");
   struct Close{HANDLE p,t;~Close(){if(t)CloseHandle(t);if(p)CloseHandle(p);}} close{process.hProcess,process.hThread};BOOL inJob=FALSE;
   if(!IsProcessInJob(process.hProcess,nullptr,&inJob)||!inJob||!TerminateProcess(process.hProcess,0)||WaitForSingleObject(process.hProcess,2000)!=WAIT_OBJECT_0)throw std::runtime_error("plain control owned child cleanup");
   CloseHandle(close.t);close.t=nullptr;CloseHandle(close.p);close.p=nullptr;
   const auto after=handleTable();DWORD countAfter=0;GetProcessHandleCount(GetCurrentProcess(),&countAfter);
   report["rows"].push_back({{"index",n},{"childPid",process.dwProcessId},{"childStoppedBeforeSnapshot",true},{"childInheritedFixtureJob",inJob!=FALSE},{"handlesBefore",countBefore},{"handlesAfter",countAfter},{"before",before},{"after",after}});
  }
  report["ok"]=true;
 }catch(const std::exception& error){report["error"]=error.what();}
 std::ofstream stream(output,std::ios::binary);stream<<report.dump(2);if(!stream)return 3;return report.value("ok",false)?0:2;
}
static Json runPlainProcessControl(){
 wchar_t image[32768]{};const DWORD length=GetModuleFileNameW(nullptr,image,32768);if(!length||length>=32768)throw std::runtime_error("control launcher image");
 const auto output=std::filesystem::path(std::wstring(image,length)).parent_path()/L"plain-process-control.json";
 std::wstring command=L"\""+std::wstring(image,length)+L"\" --joint-plain-process-control \""+output.wstring()+L"\"";
 STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
 if(!CreateProcessW(image,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process))throw std::runtime_error("control launcher create");
 struct Close{HANDLE p,t;~Close(){if(t)CloseHandle(t);if(p)CloseHandle(p);}} close{process.hProcess,process.hThread};
 if(WaitForSingleObject(process.hProcess,5000)!=WAIT_OBJECT_0)throw std::runtime_error("control launcher wait");DWORD exit=99;if(!GetExitCodeProcess(process.hProcess,&exit)||exit)throw std::runtime_error("control launcher exit");
 std::ifstream input(output,std::ios::binary);auto report=Json::parse(input);if(!report.value("ok",false))throw std::runtime_error("control report");return report;
}
}
namespace fspath=std::filesystem;
// Inert file IO ports for the ACTUAL main.cpp writer below. No product file,
// Steam API, account, setting or real atomic write is accessed by these ports.
static const std::wstring YM_SETTINGS_FILE=L"__ymcc_lifecycle_fixture_main_absent.json",YM_SETTINGS_BACKUP_FILE=L"__ymcc_lifecycle_fixture_backup_absent.json";
static std::map<std::wstring,std::string> fixtureSettings;static unsigned fixtureWrites=0,fixtureFailWrite=0,fixtureReads=0;
struct SettingsFileGuard {};
static std::string ymSettingsReadRaw(const std::wstring& path){++fixtureReads;return fixtureSettings[path];}
static json ymSettingsReadUnlocked(){return ymcc::settingsBaseline::parseDocument(ymSettingsReadRaw(YM_SETTINGS_FILE));}
static bool ymSettingsHasCompatibleWriteBase(const std::string& raw,const json& value){return ymcc::settingsBaseline::supported(ymcc::settingsBaseline::parseDocument(raw))&&ymcc::settingsBaseline::supported(value);}
static bool ymSettingsPreserveMainBeforeWrite(const std::string& raw){fixtureSettings[YM_SETTINGS_BACKUP_FILE]=raw;return true;}
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& raw){++fixtureWrites;if(fixtureWrites==fixtureFailWrite)return false;fixtureSettings[path]=raw;return true;}
${compiledLifecycleWriter}
static ymcc::deckyupdate::ManualBaseline g_deckyManualBaseline;
static const auto actualMainManualReceiptCallback=${productionManualReceiptCallback};
// Test-only image path under this executable's owned evidence directory. The
// backend control is created suspended and never executes any instruction.
static std::wstring fixtureBackendRoot(){wchar_t image[32768]{};const auto length=GetModuleFileNameW(nullptr,image,32768);if(!length||length>=32768)throw std::runtime_error("fixture backend path");return (fspath::path(std::wstring(image,length)).parent_path()/L"native-backend-proof").wstring();}
static Json actualMainLifecycleChildGate(const cleanupworker::Request& request,const Json& fresh,const std::string& build,bool admitted=true){
 const auto& session=request.identity.steam;const auto current=[&]{return admitted;};
 Dependencies deps;deps.freshMode=[&](const auto&){return fresh;};deps.installed=[&]{return build;};deps.loader=ymcc::deckyidentity::version;
 const std::wstring POWER_CONTROL_DIR=fixtureBackendRoot();
${compiledLifecycleChildGate}
 return {{"ok",false},{"mutated",false},{"reason","fixture-not-lifecycle"}};
}
namespace actualRawReply {
using namespace ymcc::deckybinding;
${compiledRawWriter}
// Runs only in this generated inert test image, after the production private
// entry has authenticated its inherited channel and parsed the actual request.
// Framing cases1..8 corrupt before business. JSON cases9..33 first execute
// the actual readonly gate/record selector, then alter only serialized bytes.
static bool tryReply(const Json& raw,int& exit){
 const auto state=raw.value("state",Json::object());const auto kind=state.value("fixtureRawReplyKind",0u);if(!kind)return false;
 const auto name=L"Local\\YMCC-ActualRawReply-"+std::to_wstring(raw.at("steam").at("pid").get<DWORD>())+L"-"+std::to_wstring(kind);
 detail::Handle entered{OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str())};if(!entered.value){exit=91;return true;}
 int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv||argc!=4){if(argv)LocalFree(argv);exit=92;return true;}
 const auto pipe=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(_wcstoui64(argv[2],nullptr,10)));LocalFree(argv);
 std::vector<std::uint8_t> bytes;std::uint32_t length=0;std::string body;
 if(kind>=9&&kind<=33){
  fixtureSettings[YM_SETTINGS_FILE]=state.at("fixtureMain").dump();fixtureSettings[YM_SETTINGS_BACKUP_FILE]=state.at("fixtureBackup").dump();fixtureReads=fixtureWrites=fixtureFailWrite=0;
  const auto request=ymcc::deckyrepair::cleanupworker::parse(raw);
  auto actual=actualMainLifecycleChildGate(request,state.at("fixtureMode"),request.args.at("frontendBuildId").get<std::string>(),true);
  if(!actual.value("ok",false)||actual.value("mutated",true)||fixtureWrites||!fixtureReads){exit=95;return true;}
  actual["fixtureSettingsReads"]=fixtureReads;actual["fixtureSettingsWrites"]=fixtureWrites;
  actual["fixtureSavedMain"]=ymcc::settingsBaseline::parseDocument(fixtureSettings[YM_SETTINGS_FILE]);actual["fixtureSavedBackup"]=ymcc::settingsBaseline::parseDocument(fixtureSettings[YM_SETTINGS_BACKUP_FILE]);
  const auto prefix=std::string("{\"schema\":")+raw.at("schema").dump()+",\"id\":"+raw.at("id").dump()+",\"generation\":"+raw.at("generation").dump()+",\"detail\":";
  if(kind==18)actual["ok"]=1;if(kind==19)actual["ok"]="true";if(kind==20)actual["ok"]=nullptr;
  if(kind==21)actual["mutated"]=0;if(kind==22)actual["mutated"]="false";if(kind==23)actual["mutated"]=nullptr;
  if(kind==24)actual["uncertain"]=0;if(kind==25)actual["uncertain"]="false";if(kind==26)actual["uncertain"]=nullptr;
  if(kind==27)actual["reason"]=0;if(kind==28)actual["reason"]=Json::object();if(kind==29)actual["reason"]=nullptr;
  if(kind==30)actual.erase("ok");if(kind==31)actual.erase("mutated");
  if(kind==32){actual["ok"]=false;actual["mutated"]=false;actual["uncertain"]=false;actual["reason"]="fixture-legitimate-failure";}
  if(kind==33){actual.erase("uncertain");actual.erase("reason");}
  auto detail=actual.dump();
  if(kind==9)detail.insert(1,R"("uncertain":true,"uncertain":false,)");
  if(kind==10)detail.insert(1,R"("mutated":true,)");
  if(kind==11){body=prefix+R"({"ok":false,"mutated":true,"uncertain":true},"detail":)"+detail+"}";}
  if(kind==12){auto ledger=actual.at("ledger").dump();ledger.insert(1,R"("status":"reserved",)");auto without=actual;without.erase("ledger");detail=without.dump();detail.pop_back();detail+=",\"ledger\":"+ledger+"}";}
  if(kind==13)detail.insert(1,R"("uncertain":true,"uncer\u0074ain":false,)");
  if(kind==14||kind==15){const unsigned count=kind==14?64:16;std::string probe(count,'[');probe+='0';probe.append(count,']');detail.insert(detail.size()-1,",\"probe\":"+probe);}
  if(kind==16)detail.insert(detail.size()-1,R"(,"probe":{"status":"ready","items":[{"name":"one"},{"name":"two"}]})");
  if(kind==17)detail.insert(1,R"("ok":true,)");
  if(body.empty())body=prefix+detail+"}";
  length=static_cast<std::uint32_t>(body.size());
 }

 if(kind>=9&&kind<=33){}
 else if(kind==1)length=0;
 else if(kind==2)length=maxFrameBytes+1;
 else if(kind==3||kind==4)length=8;
 else if(kind==6){body="{";length=1;}
 else if(kind==7){body="{}";length=2;}
 else if(kind==8){body=Json{{"schema",raw.at("schema")},{"id",raw.at("id")},{"generation",raw.at("generation")},{"detail",{{"ok",true},{"mutated",false},{"uncertain",true},{"ledger",state.at("fixtureMain").at("deckyContextCleanup")}}}}.dump();length=static_cast<std::uint32_t>(body.size());}
 else if(kind!=5){exit=93;return true;}
 if(kind!=5){bytes.resize(sizeof(length));std::memcpy(bytes.data(),&length,sizeof(length));}
 if(kind==3)bytes.resize(2);else if(kind==4){const std::string partial="abc";bytes.insert(bytes.end(),partial.begin(),partial.end());}
 else bytes.insert(bytes.end(),body.begin(),body.end());
 SetEvent(entered.value); // Framing entry or successful actual readonly inspection above.
 exit=bytes.empty()||writeRaw(pipe,bytes.data(),static_cast<DWORD>(bytes.size()))?0:94;return true;
}
}
struct ModeObservationPort {std::function<Json()> read;Json snapshot(const ymcc::steamsession::Session&)const{return read();}};
struct NativeRuntimePort {std::function<Json()> read;Json snapshot()const{return read();}};
static ModeObservationPort g_steamModeObserver;static NativeRuntimePort g_deckySidebarRuntime;
static Json deckySidebarStateJson(const Json& state){return state;}
static const auto actualMainCleanupDispatch=${compiledCleanupDispatch};
namespace lifecycleServiceFixture {
struct RuntimeState {bool enabled=true,steamKnown=true,steamRunning=true;DWORD pid=0;std::string phase="frontend-attached",reason="retained-frontend-connected";};
struct RuntimePort {RuntimeState state;std::atomic<unsigned> connectionWakes{0};std::function<bool()> ownerWake;RuntimeState snapshot()const{return state;}bool ownsLiveProcess(HANDLE process)const{return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;}bool requestConnectionCheck(){if(ownerWake&&!ownerWake())return false;++connectionWakes;return true;}bool requestLaunchAdmissionRefresh(){return !state.pid;}};
struct SessionPort {ymcc::steamsession::Session value;auto sessionSnapshot()const{return value;}};
struct ModePort {Json value;ymcc::steamui::ModeObserver* actual=nullptr;mutable ymcc::steamsession::Session lastRequested;mutable std::function<void()> afterSnapshot;Json snapshot(const ymcc::steamsession::Session& session)const{lastRequested=session;auto result=actual?actual->snapshot(session):value;auto callback=afterSnapshot;afterSnapshot={};if(callback)callback();return result;}};
enum class PowerLifecycle{Ready,Suspended};
static AutomaticRepair g_deckyAutomaticRepair;
static unsigned long long tick=1000;static auto fixtureTick(){return tick;}
static RuntimePort g_deckySidebarRuntime;static SessionPort g_steamSettingsObserver;static ModePort g_steamModeObserver;
static std::atomic<bool> g_exitRequested{false};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static unsigned long long power=1;static auto currentPowerGeneration(){return power;}
static ymcc::deckyupdate::Recovery g_deckyUpdateRecovery;
static ymcc::deckyupdate::ManualBaseline g_deckyManualBaseline;
struct BrokerPort {Json value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};ymcc::deckymirror::Broker* actual=nullptr;Json status()const{return actual?actual->status():value;}bool reply(const std::string& conn,const Json& message){return actual&&actual->reply(conn,message);}};
struct MirrorCostPort{std::atomic<unsigned> replies{0};void reply(const Json&,ULONGLONG){++replies;}};static MirrorCostPort g_deckyCostContext;
static BrokerPort g_deckyMirrorBroker;
${compiledManagedReadyState}
static bool backendReady=false;static unsigned backendReads=0;static DWORD fixtureBackendPid=61;static unsigned long long fixtureBackendBirth=1234;
static std::function<void()> afterBackendRead;static bool backendAbsenceCertified=true;
static bool deckySidebarAcquireManagedBackend(DeckyManagedBackendLease& value,bool* absent=nullptr){if(absent)*absent=false;++backendReads;const auto callback=afterBackendRead;if(callback)callback();if(!backendReady){if(absent)*absent=backendAbsenceCertified;return false;}value.pid=fixtureBackendPid;value.birth=fixtureBackendBirth;if(fixtureBackendPid!=61){value.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,fixtureBackendPid);if(!value.value)return false;}return true;} // actual Runtime Job/OS listener checks are separate IO boundaries
static const std::wstring POWER_CONTROL_DIR=L"inert-lifecycle-service";
static Json ledger,responseOverride,lastPrivateState;static bool currentAfterReply=false;
static bool useActualPrivateChild=false,holdActualPrivateChild=false,dropNextConfirmReply=false;static Json privateMain,privateBackup,privateResults=Json::array(),privateModeOverride;static std::string build;static unsigned fileReads=0,dispatches=0;static std::function<void()> beforeReply,afterPrivateReply,afterDroppedConfirm;static Json lostConfirmReplyOverride,ownedInspectReplyOverride;static std::string ownedInspectBirthOverride;static unsigned rawReplyKind=0;static unsigned postSnapshotRaceReads=0;static bool postSnapshotRaceRan=false;
static std::string fixtureFrontendBuildId(const std::wstring&){++fileReads;return build;}
static Json deckySidebarStateJson(const RuntimeState& state){return {{"enabled",state.enabled},{"pid",state.pid},{"phase",state.phase},{"reason",state.reason},{"steamKnown",true},{"steamRunning",true},{"revision",7u}};}
static bool captureManagedGuard=false;static Json managedGuardDiagnostics=Json::array();
static void deckySidebarDiagnostic(const char* event,const Json& detail,bool=false){if(captureManagedGuard&&std::string(event)=="managed-lifecycle")managedGuardDiagnostics.push_back(ymcc::deckydiag::record(event,detail));}
static Json fixtureLifecycleDispatch(const ymcc::steamsession::Session& steam,bool fromUi,const Json& args,const Json& privateState,const std::function<bool()>& current,DWORD window,const char* schema){
 ++dispatches;lastPrivateState=privateState;if(!current())throw std::runtime_error("service fixture entered non-current boundary");
 if(useActualPrivateChild){
  auto isolated=privateState;isolated["fixtureActualLifecycle"]=true;isolated["fixtureMode"]=privateModeOverride.is_object()?privateModeOverride:(g_steamModeObserver.actual?g_steamModeObserver.actual->snapshot(steam):g_steamModeObserver.value);
  isolated["fixtureMain"]=privateMain;isolated["fixtureBackup"]=privateBackup;isolated["fixtureHold"]=holdActualPrivateChild;
  auto transportedArgs=args;if(args.at("task")=="inspect"&&!ownedInspectBirthOverride.empty())transportedArgs["backendBirth"]=ownedInspectBirthOverride;
  const auto rawKind=args.at("task")=="inspect"?rawReplyKind:0u;
  ymcc::deckybinding::detail::Handle rawEntered;
  if(rawKind){const auto name=L"Local\\YMCC-ActualRawReply-"+std::to_wstring(steam.pid)+L"-"+std::to_wstring(rawKind);
   rawEntered.value=CreateEventW(nullptr,TRUE,FALSE,name.c_str());if(!rawEntered.value)throw std::runtime_error("raw reply event unavailable");isolated["fixtureRawReplyKind"]=rawKind;}
  const auto result=cleanupworker::dispatch(steam,fromUi,transportedArgs,isolated,current,window,schema);
  const bool rawObserved=rawKind&&WaitForSingleObject(rawEntered.value,0)==WAIT_OBJECT_0;
  if(afterPrivateReply)afterPrivateReply();
  privateResults.push_back({{"task",args.at("task")},{"modeEpoch",args.at("modeEpoch")},{"transportedArgs",transportedArgs},{"result",result},{"rawReplyKind",rawKind},{"rawReplyPreBusinessChildEntered",rawObserved&&rawKind<=8},{"rawReplyAfterActualReadonlyInspect",rawObserved&&rawKind>=9}});currentAfterReply=current();
  if(dropNextConfirmReply&&args.at("task")=="confirm"){
   dropNextConfirmReply=false;privateMain=result.at("fixtureSavedMain");privateBackup=result.at("fixtureSavedBackup");
   if(afterDroppedConfirm)afterDroppedConfirm();
   auto lost=Json{{"ok",false},{"mutated",true},{"uncertain",true},{"reason","decky-cleanup-worker-read-failed"},{"cleanupWorkerDestroyed",result.at("cleanupWorkerDestroyed")},{"cleanupWorkerHandles",result.at("cleanupWorkerHandles")},{"cleanupWorkerProcesses",result.at("cleanupWorkerProcesses")},{"cleanupWorkerReplyReceived",false}};
   if(lostConfirmReplyOverride.is_object())lost.update(lostConfirmReplyOverride);
   privateResults.back()["deliveredResult"]=lost;privateResults.back()["deliveryFault"]="confirm-ack-dropped-after-owned-return";return lost;
  }
  if(args.at("task")=="inspect"&&ownedInspectReplyOverride.is_object()){auto delivered=result;delivered.update(ownedInspectReplyOverride);privateResults.back()["deliveredResult"]=delivered;privateResults.back()["deliveryFault"]="inspect-reply-overridden-after-owned-return";return delivered;}
  return result;
 }
 if(beforeReply)beforeReply();currentAfterReply=current();if(responseOverride.is_object())return responseOverride;return {{"ok",true},{"mutated",false},{"ledger",ledger}};
}
${compiledLifecycleService}
static const auto actualNativeMirrorReply=${compiledNativeMirrorReply};
static void actualManagedObserverGuard(){
${productionManagedObserverGuard}
}
static const auto actualStableModeCallback=${compiledStableModeCallback};
}

namespace backendClassifierFixture {
using Json=nlohmann::json;using DeckyManagedBackendLease=lifecycleServiceFixture::DeckyManagedBackendLease;
static ymcc::deckysidebar::Runtime g_deckySidebarRuntime; // Unstarted production owner: parent listeners are foreign.
static unsigned short backendPort=0;static unsigned queryFault=0,queryCalls=0;static DWORD tableBytes4=MAXDWORD,tableBytes6=MAXDWORD;static Json lastObservation;
static DWORD fixtureGetExtendedTcpTable(PVOID table,PDWORD bytes,BOOL sort,ULONG family,TCP_TABLE_CLASS type,ULONG reserved){
 ++queryCalls;
 if((queryFault==1&&family==AF_INET&&!table)||(queryFault==2&&family==AF_INET6&&!table)||(queryFault==3&&family==AF_INET&&table)||(queryFault==4&&family==AF_INET6&&table))return ERROR_ACCESS_DENIED;
 if(queryFault==5&&family==AF_INET6&&table)return ERROR_INSUFFICIENT_BUFFER;
 if(queryFault==6&&family==AF_INET&&!table){*bytes=0;return NO_ERROR;}
 if(queryFault==7&&family==AF_INET&&!table){*bytes=16*1024*1024+1;return ERROR_INSUFFICIENT_BUFFER;}
 const DWORD supplied=*bytes;const auto code=::GetExtendedTcpTable(table,bytes,sort,family,type,reserved);
 if(!table&&queryFault>=13&&queryFault<=16&&(code==NO_ERROR||code==ERROR_INSUFFICIENT_BUFFER)){
  const DWORD minimum=family==AF_INET?static_cast<DWORD>(offsetof(MIB_TCPTABLE_OWNER_PID,table)+sizeof(MIB_TCPROW_OWNER_PID)):static_cast<DWORD>(offsetof(MIB_TCP6TABLE_OWNER_PID,table)+sizeof(MIB_TCP6ROW_OWNER_PID));
  if(*bytes<minimum)*bytes=minimum; // Safe physical storage even in bounds-removal mutants.
 }
 if(code==NO_ERROR&&table&&queryFault>=10){
  const DWORD header=family==AF_INET?static_cast<DWORD>(offsetof(MIB_TCPTABLE_OWNER_PID,table)):static_cast<DWORD>(offsetof(MIB_TCP6TABLE_OWNER_PID,table));
  if(queryFault==10&&family==AF_INET)*bytes=0;
  if((queryFault==11&&family==AF_INET)||(queryFault==12&&family==AF_INET6))*bytes=header-1;
  if((queryFault==13&&family==AF_INET)||(queryFault==14&&family==AF_INET6)){if(family==AF_INET)static_cast<MIB_TCPTABLE_OWNER_PID*>(table)->dwNumEntries=1;else static_cast<MIB_TCP6TABLE_OWNER_PID*>(table)->dwNumEntries=1;*bytes=header;}
  if(queryFault==15&&family==AF_INET)*bytes=supplied+1;
  if(queryFault==16&&family==AF_INET){static_cast<MIB_TCPTABLE_OWNER_PID*>(table)->dwNumEntries=MAXDWORD;}
  if(queryFault==17){if(family==AF_INET)static_cast<MIB_TCPTABLE_OWNER_PID*>(table)->dwNumEntries=0;else static_cast<MIB_TCP6TABLE_OWNER_PID*>(table)->dwNumEntries=0;*bytes=header;}
 }
 if(table&&code==NO_ERROR){if(family==AF_INET)tableBytes4=*bytes;else tableBytes6=*bytes;}
 if(code==NO_ERROR&&table&&queryFault>=8){const auto port=[](DWORD value){return static_cast<unsigned short>(((value&0xff)<<8)|((value>>8)&0xff));};
  if(queryFault==8&&family==AF_INET6){auto* t=static_cast<MIB_TCP6TABLE_OWNER_PID*>(table);for(DWORD i=0;i<t->dwNumEntries;++i)if(port(t->table[i].dwLocalPort)==backendPort)t->table[i].dwOwningPid=GetCurrentProcessId()+1;}
  if(queryFault==9&&family==AF_INET){auto* t=static_cast<MIB_TCPTABLE_OWNER_PID*>(table);for(DWORD i=0;i<t->dwNumEntries;++i)if(port(t->table[i].dwLocalPort)==backendPort)t->table[i].dwOwningPid=0;}
 }
 return code;
}
${classifierBackendProof}
static void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Socket{SOCKET value=INVALID_SOCKET;~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}};
static Json observe(const char* label,unsigned fault){queryFault=fault;queryCalls=0;tableBytes4=tableBytes6=MAXDWORD;bool absent=true;bool ok=false;DWORD owner=0,openedPid=0;unsigned long long birth=0;
 {DeckyManagedBackendLease lease;ok=::backendClassifierFixture::deckySidebarAcquireManagedBackend(lease,&absent);owner=lease.pid;birth=lease.birth;openedPid=lease.value?GetProcessId(lease.value):0;}
 lastObservation={{"label",label},{"fault",fault},{"ok",ok},{"absent",absent},{"queryCalls",queryCalls},{"owner",owner},{"openedPid",openedPid},{"birth",std::to_string(birth)},{"returnedTableBytes4",tableBytes4},{"returnedTableBytes6",tableBytes6}};return lastObservation;
}
static Json run(){
 WSADATA data{};need(WSAStartup(MAKEWORD(2,2),&data)==0,"backend classifier Winsock initialization");struct Wsa{~Wsa(){WSACleanup();}}wsa;
 Socket four,six;unsigned reservations=0;
 for(;reservations<8;++reservations){if(four.value!=INVALID_SOCKET)closesocket(four.value);if(six.value!=INVALID_SOCKET)closesocket(six.value);four.value=six.value=INVALID_SOCKET;
  unsigned random=0;need(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&random),sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"classifier port selection");backendPort=static_cast<unsigned short>(49152+random%16384);need(backendPort!=1337,"classifier must not bind installed port");
  four.value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);six.value=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);need(four.value!=INVALID_SOCKET&&six.value!=INVALID_SOCKET,"classifier loopback socket creation");BOOL one=TRUE;
  need(setsockopt(four.value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&one),sizeof(one))==0&&setsockopt(six.value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&one),sizeof(one))==0&&setsockopt(six.value,IPPROTO_IPV6,IPV6_V6ONLY,reinterpret_cast<const char*>(&one),sizeof(one))==0,"classifier exclusive sockets");
  sockaddr_in addr4{};addr4.sin_family=AF_INET;addr4.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr4.sin_port=htons(backendPort);
  sockaddr_in6 addr6{};addr6.sin6_family=AF_INET6;addr6.sin6_addr=in6addr_loopback;addr6.sin6_port=htons(backendPort);
  if(bind(four.value,reinterpret_cast<sockaddr*>(&addr4),sizeof(addr4))==0&&bind(six.value,reinterpret_cast<sockaddr*>(&addr6),sizeof(addr6))==0)break;
 }
 need(reservations<8,"classifier could not reserve both high loopback ports");Json rows=Json::array();
 const auto cold=observe("complete-tables-no-listener",0);need(!cold.at("ok").get<bool>()&&cold.at("absent").get<bool>(),"complete OS tables did not certify a genuinely absent listener");rows.push_back(cold);
 DWORD before=0;need(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"classifier handle baseline");
 for(unsigned fault=1;fault<=7;++fault){const auto row=observe("os-table-failure",fault);rows.push_back(row);need(!row.at("ok").get<bool>()&&!row.at("absent").get<bool>(),"backend proof API failure reported absence");}
 for(unsigned fault=10;fault<=16;++fault){const auto row=observe("successful-incomplete-table",fault);rows.push_back(row);need(!row.at("ok").get<bool>()&&!row.at("absent").get<bool>(),"successful incomplete listener table reported absence");}
 const auto empty=observe("valid-minimum-empty-header",17);rows.push_back(empty);need(!empty.at("ok").get<bool>()&&empty.at("absent").get<bool>(),"valid minimum empty table was rejected");
 need(listen(four.value,SOMAXCONN)==0&&listen(six.value,SOMAXCONN)==0,"classifier local listen");
 const auto foreign=observe("real-unowned-parent-listener",0);rows.push_back(foreign);need(!foreign.at("ok").get<bool>()&&!foreign.at("absent").get<bool>()&&foreign.at("openedPid")==GetCurrentProcessId(),"unowned listener was accepted or reported absent");
 for(unsigned fault:{8u,9u}){const auto row=observe("owner-table-fault",fault);rows.push_back(row);need(!row.at("ok").get<bool>()&&!row.at("absent").get<bool>(),"conflicting or zero owner reported absence");}
 queryFault=0;DWORD after=0;need(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"backend classifier leaked request-owned handles");
 return {{"kind","actual-os-backend-absence-classification"},{"rows",rows},{"queries",rows.size()},{"reservedPort",backendPort},{"reservationAttempts",reservations+1},{"coldApiInitializationObservedSeparately",true},{"requestHandlesBefore",before},{"requestHandlesAfter",after},{"ownedLoaderPositiveNotExercised",true},{"scope","Actual source function, OS IPv4/IPv6 tables, actual parent listener and production Runtime foreign-owner rejection. Only port and explicit query/data faults are adapted; no installed port or application accessed."}};
}
}

int main(int argc,char** argv){
 if(argc==3&&std::string(argv[1])=="--joint-plain-process-control")return jointresource::plainProcessCreationControl(std::filesystem::path(argv[2]));
 int workerExit=0;
 if(ymcc::deckybinding::entry::tryRunCommandLine(GetCommandLineW(),workerExit,[](const Json& raw,HANDLE cancel,const auto& emit,int& exit){
  if(actualRawReply::tryReply(raw,exit))return true;
  return ymcc::deckyrepair::cleanupworker::tryRun(raw,cancel,emit,exit,[](const auto& request,HANDLE cancel)->Json{
   if(request.lifecycle&&request.state.value("fixturePlainProcessControl",false))return Json{{"ok",true},{"mutated",false},{"fixtureWorkerPid",GetCurrentProcessId()},{"plainWin32Control",jointresource::runPlainProcessControl()}};
   if(request.lifecycle&&request.state.value("fixtureActualLifecycle",false)){
    fixtureSettings[YM_SETTINGS_FILE]=request.state.at("fixtureMain").dump();fixtureSettings[YM_SETTINGS_BACKUP_FILE]=request.state.at("fixtureBackup").dump();fixtureReads=fixtureWrites=fixtureFailWrite=0;
    bool admitted=true;
    if(request.state.value("fixtureHold",false)){
     const auto name=L"Local\\YMCC-Managed-Joint-"+std::to_wstring(request.identity.steam.pid);
     HANDLE entered=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());if(!entered)throw std::runtime_error("joint fixture event absent");SetEvent(entered);CloseHandle(entered);
     WaitForSingleObject(cancel,INFINITE);admitted=false;
    }
    auto result=actualMainLifecycleChildGate(request,request.state.at("fixtureMode"),request.args.at("frontendBuildId").template get<std::string>(),admitted);
    result["fixtureWorkerPid"]=GetCurrentProcessId();result["fixtureWorkerBirth"]=std::to_string(cleanupworker::processBirth(GetCurrentProcess()));
    result["fixtureSettingsReads"]=fixtureReads;result["fixtureSettingsWrites"]=fixtureWrites;result["fixtureNativeHost"]=request.state.at("cleanupNativeHost");
    result["fixtureSavedMain"]=ymcc::settingsBaseline::parseDocument(fixtureSettings[YM_SETTINGS_FILE]);result["fixtureSavedBackup"]=ymcc::settingsBaseline::parseDocument(fixtureSettings[YM_SETTINGS_BACKUP_FILE]);
    return result;
   }
   if(request.lifecycle)return Json{{"ok",true},{"mutated",false},{"fixtureTask",request.args.at("task")},{"nativeHost",request.state.at("cleanupNativeHost")}};
   if(request.args.at("modeEpoch")=="hang-document:1"){
    const auto name=L"Local\\YMCC-Cleanup-Hang-"+std::to_wstring(request.identity.steam.pid);
    HANDLE entered=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());if(entered){SetEvent(entered);CloseHandle(entered);}Sleep(INFINITE);
   }
   if(request.args.at("modeEpoch")=="slow-document:1")Sleep(4000);
   return {{"ok",true},{"mutated",true},{"requestDispatched",true},{"verifiedAccount",request.identity.steam.account},{"fromUi",request.identity.liveUiAccount}}; // inert child only, no Steam API
  });
 }))return workerExit;
 std::set<std::string> selectedCases;for(int i=2;i<argc;++i)selectedCases.insert(argv[i]);
 Json declaredCases=Json::array(),cases=Json::array(),upgradeActions=Json::array(),cleanupActions=Json::array(),workerResults=Json::array();auto check=[&](const char* name,auto run){declaredCases.push_back(name);if(argc>2&&!selectedCases.contains(name))return;try{run();cases.push_back({{"name",name},{"pass",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"pass",false},{"error",e.what()}});}};
 auto need=[](bool value,const char* reason){if(!value)throw std::runtime_error(reason);};
 const auto initial=Json{{"enabled",true},{"pid",0u},{"steamKnown",true},{"steamRunning",true},{"phase","unavailable"},{"reason","mirror-bootstrap-session-changed"},{"revision",7u}};
 check("field failure preserves manual availability without admitting other failures",[&]{need(repairable(initial),"ROGX session rejection hides manual repair");for(const auto& [key,value]:std::vector<std::pair<std::string,Json>>{{"enabled",false},{"pid",5u},{"steamKnown",false},{"steamRunning",false},{"phase","failed"},{"reason","resource-hash-mismatch"}}){auto bad=initial;bad[key]=value;need(!repairable(bad),"unrelated failure admitted");}});
 ymcc::steamsession::Session session;session.root=L"C:\\FixtureSteam";session.pid=41;session.created=123456;session.account=9;
 const std::string build(64,'a');const auto residue=Json{{"protocol",1},{"active",true},{"bridgePresent",true},{"frontendBuildId",build},{"loaderVersion","v3.3.0"}};
 const Json initialMode={{"known",true},{"running",true},{"pid",41u},{"birth","123456"},{"mode","bigpicture"},{"deckyFrontendLoaded",true},{"deckyUpdateResidue",residue}};
 auto state=initial,mode=initialMode;auto currentSession=session;bool ending=false;std::string resourceFailure;unsigned freshReads=0,cachedReads=0,preflights=0,transports=0,refreshes=0,consents=0;unsigned long long now=1000;std::function<void()> beforeAdmit;
 Dependencies deps;deps.session=[&]{return currentSession;};deps.runtime=[&]{return state;};deps.mode=[&](const auto&){++cachedReads;return Json{};};deps.freshMode=[&](const auto&){++freshReads;return mode;};deps.installed=[&]{return build;};deps.loader="v3.3.0";deps.preflight=[&]{++preflights;return resourceFailure;};deps.exiting=[&]{return ending;};deps.consent=[&](const auto&){++consents;return true;};deps.now=[&]{return now;};deps.dispatch=[&](const auto& selected,const auto& action,const auto& admit){++transports;if(beforeAdmit)beforeAdmit();if(!(selected==session)||!admit())return Json{{"ok",false},{"mutated",false},{"reason","final-admission-refused"}};need(action.at("userInitiated")==true&&action.at("operation")=="decky.repairConnection","explicit operation changed");++refreshes;return Json{{"ok",true},{"mutated",true}};};
 const Json args={{"userInitiated",true},{"pid",41u},{"birth","123456"},{"revision",7u}};
 auto reset=[&]{state=initial;mode=initialMode;currentSession=session;ending=false;resourceFailure.clear();beforeAdmit={};now+=30001;};
 check("explicit repair freshly validates current context and dispatches once",[&]{reset();ManualRepair r;need(r.request(args,deps).value("ok",false),"manual repair refused");need(freshReads==1&&cachedReads==0&&transports==1&&refreshes==1&&consents==1,"fresh check or one dispatch missing");need(!r.request(args,deps).value("ok",true)&&refreshes==1,"repeat refreshed twice");});
 check("field loaded330 installed331 explicit repair pins observed document version",[&]{reset();deps.loader="v3.3.1";ManualRepair r;const auto actions=transports;const auto original=deps.dispatch;deps.dispatch=[&](const auto& selected,const auto& action,const auto& admit){need(action.at("loaderVersion")=="v3.3.0","manual repair must pin the actual old document version");return original(selected,action,admit);};const auto result=r.request(args,deps);deps.dispatch=original;deps.loader="v3.3.0";need(result.value("ok",false)&&transports==actions+1,"installed331/loaded330 context deadlocked manual recovery");});
 check("loaded330 installed331 never grants retained or automatic refresh authority",[&]{reset();deps.loader="v3.3.1";ymcc::deckyupdate::Recovery recovery;need(!recovery.allowsRetainedFrontend(session,mode,build,deps.loader),"old Loader granted retained authority");ManualRepair r;auto automatic=args;automatic.erase("userInitiated");automatic["automatic"]=true;const auto actions=transports;need(r.request(automatic,deps,Origin::Automatic).value("reason",std::string{})=="steam-ui-refresh-requires-user"&&transports==actions,"upgrade automatically rebuilt UI");deps.loader="v3.3.0";});
 check("manual repair refuses newer malformed and noncanonical loaded versions",[&]{for(const auto* version:{"v3.3.2","v3.4.0","v0.0.0","v03.3.0","v3.3.999999999999999999999","bad"}){reset();deps.loader="v3.3.1";mode["deckyUpdateResidue"]["loaderVersion"]=version;ManualRepair r;const auto actions=transports;need(!r.request(args,deps).value("ok",true)&&transports==actions,"foreign or newer Loader context admitted");}deps.loader="v3.3.0";});
 check("automatic path never rebuilds Steam after this failure",[&]{reset();ManualRepair r;const auto reads=freshReads,actions=transports,checks=preflights;auto automatic=args;automatic.erase("userInitiated");automatic["automatic"]=true;auto result=r.request(automatic,deps,Origin::Automatic);need(result.value("reason",std::string{})=="steam-ui-refresh-requires-user"&&freshReads==reads&&transports==actions&&preflights==checks,"automatic refresh or IO admitted");});
 check("future stable upgrades use numeric observed-document pinning",[&]{for(const auto& versions:std::vector<std::pair<std::string,std::string>>{{"v3.3.1","v3.3.0"},{"v3.4.0","v3.3.1"},{"v4.0.0","v3.4.0"},{"v3.10.0","v3.9.0"}}){reset();deps.loader=versions.first;mode["deckyUpdateResidue"]["loaderVersion"]=versions.second;const auto action=target(session,mode,build,deps.loader);need(!action.is_null()&&action.at("loaderVersion")==versions.second,"future upgrade lost actual document identity");upgradeActions.push_back({{"installedLoader",versions.first},{"loadedLoader",versions.second},{"action",action}});ymcc::deckyupdate::Recovery recovery;need(!recovery.allowsRetainedFrontend(session,mode,build,deps.loader),"mixed future version auto rebound");}deps.loader="v3.3.0";});
 check("upgrade alternation never trusts stale frontend or Loader receipts",[&]{reset();ymcc::deckyupdate::Recovery recovery;mode["deckyUpdateResidue"]["loaderVersion"]="v3.3.1";need(recovery.allowsRetainedFrontend(session,mode,build,"v3.3.1"),"matching new context refused");need(!recovery.allowsRetainedFrontend(session,mode,build,"v3.3.0"),"old installed receipt accepted newer document");mode["deckyUpdateResidue"]["frontendBuildId"]=std::string(64,'b');need(!recovery.allowsRetainedFrontend(session,mode,build,"v3.3.1"),"old build receipt accepted different new frontend");mode["deckyUpdateResidue"]["frontendBuildId"]=build;auto foreign=session;++foreign.created;need(!recovery.allowsRetainedFrontend(foreign,mode,build,"v3.3.1"),"stale new-process receipt trusted");recovery.veto();need(!recovery.allowsRetainedFrontend(session,mode,build,"v3.3.1"),"OFF lost across alternating update state");});
 check("manual old-document refresh acknowledges dispatch without claiming connection",[&]{reset();deps.loader="v3.3.1";ManualRepair r;const auto before=state;const auto result=r.request(args,deps);deps.loader="v3.3.0";need(result.value("ok",false)&&state==before&&state.at("phase")=="unavailable","request acknowledgement fabricated a ready backend");});
 check("stale revision pid and birth reject before context read",[&]{for(const auto& [key,value]:std::vector<std::pair<std::string,Json>>{{"revision",6u},{"pid",42u},{"birth","old"},{"userInitiated",false}}){reset();ManualRepair r;auto bad=args;bad[key]=value;const auto reads=freshReads,actions=transports;need(!r.request(bad,deps).value("ok",true)&&freshReads==reads&&transports==actions,"stale request admitted");}});
 check("OFF sleeping and exit veto manual recovery",[&]{for(int n=0;n<3;++n){reset();ManualRepair r;const auto actions=transports;if(n==0)state["enabled"]=false;else ending=true;need(!r.request(args,deps).value("ok",true)&&transports==actions,"OFF or exit admitted");}});
 check("fresh foreign Steam context and official version remain rejected",[&]{for(int n=0;n<4;++n){reset();ManualRepair r;const auto actions=transports;if(n==0)mode["birth"]="other";if(n==1)mode["mode"]="desktop";if(n==2)mode["deckyUpdateResidue"]["loaderVersion"]="v0.0.0";if(n==3)mode["deckyFrontendLoaded"]=false;need(!r.request(args,deps).value("ok",true)&&transports==actions,"unverified context admitted");}});
 check("resource and foreign port failures preserve no-dispatch",[&]{for(const auto* reason:{"resource-hash-mismatch","port-1337-in-use"}){reset();resourceFailure=reason;ManualRepair r;const auto actions=transports;need(r.request(args,deps).value("reason",std::string{})==reason&&transports==actions,"preflight bypassed");}});
 check("OFF or exact Steam identity change during dispatch cancels refresh",[&]{for(int n=0;n<4;++n){reset();unsigned long long generation=1;deps.powerGeneration=[&]{return generation;};ManualRepair r;const auto actions=refreshes;beforeAdmit=[&,n]{if(n==0)state["enabled"]=false;if(n==1)++currentSession.created;if(n==2)state["revision"]=8u;if(n==3)++generation;};const auto result=r.request(args,deps);deps.powerGeneration={};need(!result.value("ok",true)&&refreshes==actions,"late identity or completed power transition refreshed");}});
 check("uncertain result never grants an automatic or duplicate replay",[&]{reset();ManualRepair r;const auto original=deps.dispatch;deps.dispatch=[&](const auto&,const auto&,const auto& admit)->Json{++transports;need(admit(),"uncertain dispatch refused");++refreshes;throw std::runtime_error("reply lost");};const auto actions=transports;const auto result=r.request(args,deps);need(!result.value("ok",true)&&result.value("mutated",false)&&result.value("uncertain",false),"uncertain mutation concealed");need(!r.request(args,deps).value("ok",true)&&transports==actions+1,"uncertain retry replayed");deps.dispatch=original;});

 // Actual FinalCleanup/AutomaticRepair, memory ports only. The durable ledger
 // outlives orchestrator reconstruction, just as it outlives a YMCC restart.
 unsigned gentle=0,cleanupDispatches=0,reservations=0;Json ledger;bool cleanupPermission=true;
 const auto oldDispatch=deps.dispatch;
 const auto arm=[&]{reset();mode["modeEpoch"]="current-document:1";mode["observedAccount"]=session.account;mode["deckyAccountWatchAvailable"]=true;ledger=nullptr;cleanupPermission=true;gentle=cleanupDispatches=reservations=0;
  deps.mode=[&](const auto&){++cachedReads;return mode;};
  deps.cleanupAllowed=[&](const auto& s){return cleanupPermission&&cleanupAvailable(ledger,s);};
  deps.reserveCleanup=[&](const auto& s){if(!cleanupAvailable(ledger,s))return false;++reservations;ledger={{"schemaVersion",1},{"steamSession",ymcc::deckyupdate::ticket(s)}};return true;};
  deps.gentleRecovery=[&](const auto&){++gentle;};
  deps.dispatch=[&](const auto&,const auto& action,const auto& admit)->Json{++transports;if(beforeAdmit)beforeAdmit();if(!admit())return {{"ok",false},{"mutated",false},{"reason","final-admission-refused"}};
   need(action.at("operation")=="decky.cleanupConnection"&&action.at("cleanupAuthorized")==true&&action.at("expectedModeEpoch")==mode.at("modeEpoch"),"final cleanup lost typed context pin");
   ++cleanupDispatches;cleanupActions.push_back({{"action",action}});return {{"ok",true},{"mutated",true},{"requestDispatched",true}};};};
 const auto escalate=[&](AutomaticRepair& a,ManualRepair& r){a.observe(r,deps);now+=30000;a.observe(r,deps);now+=30000;a.observe(r,deps);};
 check("stale installed context receives two real gentle requests then one final cleanup",[&]{arm();AutomaticRepair a;ManualRepair r;const auto started=now;
  a.observe(r,deps);need(gentle==1&&cleanupDispatches==0&&a.nextTimeout(now)==30000,"initial gentle grace missing");
  for(int i=0;i<100;++i)a.observe(r,deps);need(gentle==1&&cleanupDispatches==0,"event storm advanced grace");
  now=started+29999;a.observe(r,deps);need(gentle==1,"early attempt admitted");now=started+30000;a.observe(r,deps);need(gentle==2&&cleanupDispatches==0,"second gentle missing");
  now=started+59999;a.observe(r,deps);need(cleanupDispatches==0,"final cleanup skipped second grace");now=started+60000;a.observe(r,deps);
  need(cleanupDispatches==1&&reservations==1&&a.nextTimeout(now)==0xfffffffful,"cleanup missing or polling remained");
  for(int i=0;i<100;++i){now+=30000;a.observe(r,deps);}need(cleanupDispatches==1&&reservations==1,"cleanup repeated");
  AutomaticRepair rebooted;ManualRepair freshRepair;escalate(rebooted,freshRepair);need(cleanupDispatches==1,"YMCC restart refilled durable cleanup fuse");});
 check("original producer without final cleanup policy retains automatic refusal",[&]{arm();deps.cleanupAllowed={};AutomaticRepair a;ManualRepair r;escalate(a,r);need(cleanupDispatches==0&&gentle==0&&reservations==0,"legacy automatic origin gained UI authority");});
 check("cleanup ledger corruption or same Steam process never authorizes a destructive refill",[&]{arm();const auto corruptEmpty=Json::parse("{}");for(const auto& bad:std::vector<Json>{corruptEmpty,{{"schemaVersion",2},{"steamSession","41:123456"}},{{"schemaVersion",1},{"steamSession","bad"}},{{"schemaVersion",1},{"steamSession","41:123456"}},{{"schemaVersion",1},{"steamSession","41:123456"},{"extra",true}}})need(!cleanupAvailable(bad,session),"corrupt or spent cleanup ledger admitted");auto next=session;++next.created;need(cleanupAvailable(Json{{"schemaVersion",1},{"steamSession","41:123456"}},next),"new Steam process inherited stale fuse");});
 check("backend recovery during either grace cancels final cleanup without consuming durable fuse",[&]{for(int step=0;step<2;++step){arm();AutomaticRepair a;ManualRepair r;a.observe(r,deps);if(step){now+=30000;a.observe(r,deps);}state["phase"]="loader-started";state["pid"]=99u;now+=30000;a.observe(r,deps);need(cleanupDispatches==0&&reservations==0&&a.nextTimeout(now)==0xfffffffful,"healthy backend cleaned");}});
 check("OFF exit uncertain power or foreign Steam cancels cleanup during grace",[&]{for(int n=0;n<4;++n){arm();AutomaticRepair a;ManualRepair r;a.observe(r,deps);if(n==0)state["enabled"]=false;if(n==1)ending=true;if(n==2)cleanupPermission=false;if(n==3)++currentSession.created;now+=60000;a.observe(r,deps);need(cleanupDispatches==0&&reservations==0,"invalid lifecycle crossed cleanup boundary");}});
 check("desktop newer official Loader and missing account-context proof do not escalate",[&]{for(int n=0;n<4;++n){arm();if(n==0)mode["mode"]="desktop";if(n==1)mode["deckyUpdateResidue"]["loaderVersion"]="v4.0.0";if(n==2)mode["known"]=false;if(n==3)mode["birth"]="other";AutomaticRepair a;ManualRepair r;escalate(a,r);need(cleanupDispatches==0&&reservations==0&&gentle==0,"unverified or newer context cleaned");}});
 check("resource corruption and foreign backend block final cleanup without deleting data",[&]{for(const auto* failure:{"resource-hash-mismatch","port-1337-in-use"}){arm();resourceFailure=failure;AutomaticRepair a;ManualRepair r;escalate(a,r);need(cleanupDispatches==0&&reservations==0&&gentle==0,"preflight bypassed during final cleanup");}});
 check("fresh document change on final read cancels cleanup",[&]{arm();AutomaticRepair a;ManualRepair r;a.observe(r,deps);now+=30000;a.observe(r,deps);const auto original=deps.freshMode;deps.freshMode=[&](const auto&)->Json{auto fresh=mode;fresh["modeEpoch"]="replacement-document:1";return fresh;};now+=30000;a.observe(r,deps);deps.freshMode=original;need(cleanupDispatches==0&&reservations==0,"cached document dispatched final cleanup");});
 check("lost cleanup reply stays uncertain and spends durable fuse across restart",[&]{arm();deps.dispatch=[&](const auto&,const auto&,const auto& admit)->Json{need(admit(),"uncertain cleanup not admitted");++cleanupDispatches;throw std::runtime_error("reply lost");};AutomaticRepair a;ManualRepair r;escalate(a,r);need(cleanupDispatches==1&&reservations==1&&a.snapshot(now).value("uncertain",false),"uncertain cleanup hidden");AutomaticRepair rebooted;ManualRepair retry;escalate(rebooted,retry);need(cleanupDispatches==1,"uncertain cleanup replayed after restart");});
 check("OFF after durable reservation vetoes actual final dispatch",[&]{arm();beforeAdmit=[&]{state["enabled"]=false;};AutomaticRepair a;ManualRepair r;escalate(a,r);need(cleanupDispatches==0&&reservations==1,"late OFF reset Steam context");});
 check("new document during gentle recovery restarts the full grace without refreshing old target",[&]{arm();AutomaticRepair a;ManualRepair r;a.observe(r,deps);now+=30000;a.observe(r,deps);mode["modeEpoch"]="replacement-document:1";now+=30000;a.observe(r,deps);need(cleanupDispatches==0&&gentle==3,"new document inherited old grace");});

 check("actual same-image cleanup child returns receipt and releases all owned resources",[&]{
  arm();auto own=session;own.pid=GetCurrentProcessId();auto request=Json{{"finalCleanup",true},{"pid",own.pid},{"birth",std::to_string(own.created)},{"revision",7u},{"modeEpoch","current-document:1"}};
  const auto result=cleanupworker::dispatch(own,false,request,state,[]{return true;});workerResults.push_back({{"kind","receipt"},{"result",result}});
  need(result.value("cleanupWorkerReplyReceived",false),"actual parsed private reply was not distinguished from transport loss");
  need(result.value("ok",false)&&result.value("cleanupWorkerDestroyed",false)&&result.value("cleanupWorkerHandles",99u)==0&&result.value("cleanupWorkerProcesses",99u)==0,"same-image final cleanup owner leaked or lost receipt");
  // Current installed-upgrade field: the reserved page-cleanup ledger refuses
  // without UI, then the separate Big Picture tier is requested five seconds later.
  // Drive the existing request API on both old and candidate headers. Never call
  // the candidate-only three-argument AttemptBudget::reserve in a RED control.
  unsigned long long originNow=100000;ManualRepair originRepair,manualFirst;ManualRepair* active=&originRepair;
  auto originMode=initialMode;originMode["pid"]=own.pid;originMode["birth"]=std::to_string(own.created);originMode["modeEpoch"]="origin-document:1";
  originMode["observedAccount"]=own.account;originMode["deckyAccountWatchAvailable"]=true;
  const auto hostTicket=ymcc::deckyupdate::ticket(own);
  const auto reserved=ymcc::deckyupdate::managed::record(own,"origin-document",hostTicket,build,"v3.3.0","reserved");
  need(ymcc::deckyupdate::managed::valid(reserved),"origin fixture must use a real valid schema2 reserved record");
  Json durableMain={{"deckyContextCleanup",reserved}},durableBackup=durableMain;
  const auto originalContext=durableMain.at("deckyContextCleanup");
  auto page=request;page["modeEpoch"]="origin-document:1";
  auto reopen=page;reopen.erase("finalCleanup");reopen["reopenBigPicture"]=true;
  auto manual=page;manual.erase("finalCleanup");manual.erase("modeEpoch");manual["userInitiated"]=true;
  unsigned pages=0,reopens=0,independentReservations=0,childRuns=0,manualRuns=0;bool manualInFlightProbe=false;
  Dependencies originDeps;originDeps.session=[&]{return own;};originDeps.runtime=[&]{return initial;};
  originDeps.mode=[&](const auto&){return originMode;};originDeps.freshMode=originDeps.mode;
  originDeps.installed=[&]{return build;};originDeps.loader="v3.3.0";originDeps.preflight=[]{return std::string{};};
  originDeps.exiting=[]{return false;};originDeps.consent=[](const auto&){return true;};originDeps.cleanupAllowed=originDeps.consent;originDeps.now=[&]{return originNow;};
  const auto waiting=[&](const Json& value){return !value.value("ok",true)&&value.value("reason",std::string{})=="decky-repair-wait";};
  const auto refusal=[](){return Json{{"ok",false},{"reason","decky-cleanup-budget-unavailable"},{"mutated",false},{"uncertain",false}};};
  originDeps.dispatch=[&](const auto&,const auto& action,const auto& admit)->Json{
   need(admit()&&action.at("userInitiated")==true,"origin manual dispatch lost original authority");++manualRuns;
   if(manualInFlightProbe){need(waiting(active->request(page,originDeps,Origin::FinalCleanup)),"Manual in flight admitted FinalCleanup");need(waiting(active->request(reopen,originDeps,Origin::ReopenBigPicture)),"Manual in flight admitted Reopen");}
   return Json{{"ok",true},{"mutated",true}};
  };
  originDeps.cleanupWorker=[&](const auto& selected,const auto& action,const auto& admit)->Json{
   need(admit(),"origin cleanup callback lost current authority");
   if(action.value("finalCleanup",false)){
    ++pages;need(!cleanupDocumentAvailable(durableMain,selected,"origin-document")&&!cleanupDocumentAvailable(durableBackup,selected,"origin-document"),"reserved context ledger unexpectedly available");
    return refusal(); // Exactly the nonmutating child/ledger refusal reported in the field.
   }
   need(action.value("reopenBigPicture",false),"origin changed the private child operation");++reopens;
   const auto beforePage=pages,beforeReopen=reopens,beforeManual=manualRuns;
   need(waiting(active->request(manual,originDeps)),"Reopen in flight admitted Manual");
   need(waiting(active->request(page,originDeps,Origin::FinalCleanup)),"Reopen in flight admitted FinalCleanup");
   need(waiting(active->request(reopen,originDeps,Origin::ReopenBigPicture)),"Reopen in flight admitted duplicate Reopen");
   need(pages==beforePage&&reopens==beforeReopen&&manualRuns==beforeManual,"nested origin entered another dispatch");
   if(!cleanupDocumentAvailable(durableMain,selected,"origin-document","deckyBigPictureRecovery")||!cleanupDocumentAvailable(durableBackup,selected,"origin-document","deckyBigPictureRecovery"))return refusal();
   durableMain["deckyBigPictureRecovery"]=reserved;durableBackup["deckyBigPictureRecovery"]=reserved;++independentReservations;++childRuns;
   return cleanupworker::dispatch(selected,false,action,initial,admit); // Real existing same-image child, inert Steam ports.
  };
  need(!durableMain.contains("deckyBigPictureRecovery")&&!durableBackup.contains("deckyBigPictureRecovery"),"independent recovery ledger must start absent");
  const auto deniedPage=originRepair.request(page,originDeps,Origin::FinalCleanup);
  workerResults.push_back({{"kind","origin-final-refused"},{"now",originNow},{"result",deniedPage},{"pageCallbacks",pages},{"childRuns",childRuns}});
  need(deniedPage.value("reason",std::string{})=="decky-cleanup-budget-unavailable"&&!deniedPage.value("mutated",true)&&!deniedPage.value("uncertain",true)&&pages==1&&childRuns==0,"FinalCleanup did not reproduce the nonmutating reserved-ledger refusal");
  originNow+=5000;const auto reopened=originRepair.request(reopen,originDeps,Origin::ReopenBigPicture);
  workerResults.push_back({{"kind","origin-reopen-after-five-seconds"},{"now",originNow},{"result",reopened},{"reopenCallbacks",reopens},{"reservations",independentReservations},{"childRuns",childRuns}});
  need(reopened.value("ok",false)&&reopened.value("cleanupWorkerReplyReceived",false)&&reopened.value("cleanupWorkerDestroyed",false)&&reopened.value("cleanupWorkerHandles",99u)==0&&reopened.value("cleanupWorkerProcesses",99u)==0&&reopens==1&&independentReservations==1&&childRuns==1,"FinalCleanup refusal incorrectly blocked independent Reopen after five seconds");
  ++originNow;need(waiting(originRepair.request(reopen,originDeps,Origin::ReopenBigPicture))&&reopens==1,"same-origin Reopen cooldown was bypassed");
  need(waiting(originRepair.request(page,originDeps,Origin::FinalCleanup))&&pages==1,"same-origin FinalCleanup cooldown was bypassed");
  originNow=130000;need(originRepair.request(page,originDeps,Origin::FinalCleanup).value("reason",std::string{})=="decky-cleanup-budget-unavailable"&&pages==2,"FinalCleanup 30-second boundary changed or durable quota refunded");
  originNow=135000;need(originRepair.request(reopen,originDeps,Origin::ReopenBigPicture).value("reason",std::string{})=="decky-cleanup-budget-unavailable"&&reopens==2&&independentReservations==1&&childRuns==1,"Reopen cooldown expiry refunded the independent durable reservation");
  originNow=135001;need(originRepair.request(manual,originDeps).value("ok",false)&&manualRuns==1,"prior origins consumed the initial Manual slot");
  originNow=160000;need(originRepair.request(page,originDeps,Origin::FinalCleanup).value("reason",std::string{})=="decky-cleanup-budget-unavailable"&&pages==3,"later FinalCleanup did not enter its own slot");
  need(waiting(originRepair.request(manual,originDeps))&&manualRuns==1,"cross-origin activity cleared Manual cooldown");
  originNow=165000;need(waiting(originRepair.request(manual,originDeps))&&manualRuns==1,"Manual cooldown shortened below thirty seconds");
  originNow=165001;need(originRepair.request(manual,originDeps).value("ok",false)&&manualRuns==2,"Manual exact thirty-second boundary changed");
  --originNow;need(waiting(originRepair.request(manual,originDeps))&&manualRuns==2,"backwards clock replenished Manual cooldown");
  active=&manualFirst;manualInFlightProbe=true;originNow=200000;
  need(manualFirst.request(manual,originDeps).value("ok",false)&&manualRuns==3,"fresh Manual owner did not retain global in-flight exclusion");
  need(durableMain.at("deckyContextCleanup")==originalContext&&durableBackup.at("deckyContextCleanup")==originalContext&&durableMain.at("deckyBigPictureRecovery")==reserved&&durableBackup.at("deckyBigPictureRecovery")==reserved,"origin switching or Manual activity mutated/refunded durable reservations");
  workerResults.push_back({{"kind","origin-cooldown-boundaries"},{"pageCallbacks",pages},{"reopenCallbacks",reopens},{"reservations",independentReservations},{"childRuns",childRuns},{"manualRuns",manualRuns},{"durableBothReserved",true}});
 });
 check("stuck cleanup callback is destroyed within original 3200ms stop boundary",[&]{
  arm();auto own=session;own.pid=GetCurrentProcessId();auto request=Json{{"finalCleanup",true},{"pid",own.pid},{"birth",std::to_string(own.created)},{"revision",7u},{"modeEpoch","hang-document:1"}};
  const auto name=L"Local\\YMCC-Cleanup-Hang-"+std::to_wstring(own.pid);HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,name.c_str());std::atomic<bool> keep{true},ran{false};
  std::thread off([&]{ran=WaitForSingleObject(entered,2000)==WAIT_OBJECT_0;keep=false;cleanupworker::Active::cancel();});
  const auto started=GetTickCount64();const auto result=cleanupworker::dispatch(own,false,request,state,[&]{return keep.load();});const auto elapsed=GetTickCount64()-started;
  off.join();CloseHandle(entered);need(result.contains("cleanupWorkerReplyReceived")&&!result.at("cleanupWorkerReplyReceived").get<bool>(),"actual cancelled private worker incorrectly reported a parsed reply");workerResults.push_back({{"kind","stuck"},{"result",result},{"entered",ran.load()},{"elapsedMs",elapsed}});need(ran&&elapsed<3200&&result.value("cleanupWorkerDestroyed",false)&&result.value("cleanupWorkerHandles",99u)==0&&result.value("cleanupWorkerProcesses",99u)==0,"stuck final-cleanup callback escaped original stop deadline or leaked its child Job");
  // Runtime's explicit cancel hook must independently stop the bounded call;
  // a lagging upstream state predicate can still report true after cancellation.
  arm();own=session;own.pid=GetCurrentProcessId();request["pid"]=own.pid;
  std::atomic<bool> allow{true},cancelEntered{false},cancelFallback{false};
  HANDLE cancelEvent=CreateEventW(nullptr,TRUE,FALSE,name.c_str()),cancelDone=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  std::thread explicitCancel([&]{cancelEntered=WaitForSingleObject(cancelEvent,2000)==WAIT_OBJECT_0;cleanupworker::Active::cancel();
   if(WaitForSingleObject(cancelDone,2000)==WAIT_TIMEOUT){cancelFallback=true;allow=false;}});
  const auto cancelStarted=GetTickCount64();const auto cancelResult=cleanupworker::dispatch(own,false,request,state,[&]{return allow.load();});const auto cancelElapsed=GetTickCount64()-cancelStarted;
  SetEvent(cancelDone);explicitCancel.join();CloseHandle(cancelEvent);CloseHandle(cancelDone);
  workerResults.push_back({{"kind","explicit-cancel-upstream-still-current"},{"result",cancelResult},{"entered",cancelEntered.load()},{"emergencyPredicateChangeRequired",cancelFallback.load()},{"elapsedMs",cancelElapsed}});
  need(cancelEntered&&!cancelFallback&&cancelElapsed<3200&&!cancelResult.value("ok",true)&&cancelResult.value("uncertain",false)&&cancelResult.value("cleanupWorkerDestroyed",false)&&cancelResult.value("cleanupWorkerHandles",99u)==0&&cancelResult.value("cleanupWorkerProcesses",99u)==0,"explicit cleanup cancellation required a later upstream predicate change");
  // Same existing child, this time cancellation must come from the production
  // ManualRepair power-generation predicate while the Ready level stays true.
  arm();currentSession.pid=GetCurrentProcessId();mode["pid"]=currentSession.pid;mode["modeEpoch"]="hang-document:1";
  g_steamModeObserver.read=[&]{return mode;};g_deckySidebarRuntime.read=[&]{return state;};
  std::atomic<unsigned long long> generation{1};std::atomic<bool> powerEntered{false},fallback{false};
  const auto originalExit=deps.exiting;deps.exiting=[&]{return fallback.load();}; // Emergency fixture escape only; healthy Ready remains false.
  deps.powerGeneration=[&]{return generation.load();};deps.cleanupWorker=actualMainCleanupDispatch;
  const auto powerName=L"Local\\YMCC-Cleanup-Hang-"+std::to_wstring(currentSession.pid);
  HANDLE powerEvent=CreateEventW(nullptr,TRUE,FALSE,powerName.c_str()),done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  std::thread transition([&]{powerEntered=WaitForSingleObject(powerEvent,2000)==WAIT_OBJECT_0;++generation;
   if(WaitForSingleObject(done,2000)==WAIT_TIMEOUT){fallback=true;cleanupworker::Active::cancel();}});
  const Json action={{"finalCleanup",true},{"pid",currentSession.pid},{"birth",std::to_string(currentSession.created)},{"revision",state.at("revision")},{"modeEpoch",mode.at("modeEpoch")}};
  ManualRepair repair;const auto powerStarted=GetTickCount64();const auto powerResult=repair.request(action,deps,Origin::FinalCleanup);const auto powerElapsed=GetTickCount64()-powerStarted;
  SetEvent(done);transition.join();CloseHandle(powerEvent);CloseHandle(done);deps.cleanupWorker={};deps.powerGeneration={};deps.exiting=originalExit;
  workerResults.push_back({{"kind","actual-main-power-generation-cancel"},{"result",powerResult},{"entered",powerEntered.load()},{"emergencyCancelRequired",fallback.load()},{"elapsedMs",powerElapsed}});
  need(powerEntered&&!fallback&&powerElapsed<3200&&powerResult.value("reason",std::string{})=="decky-cleanup-cancelled-unconfirmed"&&powerResult.value("cleanupWorkerDestroyed",false)&&powerResult.value("cleanupWorkerHandles",99u)==0&&powerResult.value("cleanupWorkerProcesses",99u)==0,"completed power transition failed to cancel actual native cleanup child without emergency fallback");});
 check("silent stuck cleanup is bounded by the original worker deadline and fully reclaimed",[&]{
  arm();auto own=session;own.pid=GetCurrentProcessId();auto request=Json{{"finalCleanup",true},{"pid",own.pid},{"birth",std::to_string(own.created)},{"revision",7u},{"modeEpoch","hang-document:1"}};
  const auto started=GetTickCount64();const auto result=cleanupworker::dispatch(own,false,request,state,[]{return true;},250);const auto elapsed=GetTickCount64()-started;
  workerResults.push_back({{"kind","deadline"},{"result",result},{"elapsedMs",elapsed}});
  need(elapsed<3200&&result.value("reason",std::string{})=="decky-cleanup-worker-deadline"&&result.value("cleanupWorkerDestroyed",false)&&result.value("cleanupWorkerHandles",99u)==0&&result.value("cleanupWorkerProcesses",99u)==0,"silent cleanup exceeded original 3200ms boundary or kept resources");});
 check("production recovery window accepts a real reply after the old 3.2 second cutoff",[&]{
  arm();auto own=session;own.pid=GetCurrentProcessId();auto request=Json{{"finalCleanup",true},{"pid",own.pid},{"birth",std::to_string(own.created)},{"revision",7u},{"modeEpoch","slow-document:1"}};
  const auto started=GetTickCount64();const auto result=cleanupworker::dispatch(own,false,request,state,[]{return true;});const auto elapsed=GetTickCount64()-started;
  workerResults.push_back({{"kind","late-recovery-reply"},{"result",result},{"elapsedMs",elapsed},{"defaultRecoveryWindowMs",cleanupworker::recoveryWindowMs}});
  need(elapsed>=4000&&result.value("ok",false)&&result.value("cleanupWorkerDestroyed",false)&&cleanupworker::recoveryWindowMs==30000,"old tight recovery window discarded a valid delayed reply");});
 check("slow recovery does not consume the following connection confirmation window",[&]{
  arm();const auto forward=deps.dispatch;deps.dispatch=[&](const auto& selected,const auto& action,const auto& admit){auto result=forward(selected,action,admit);now+=30000;return result;};
  AutomaticRepair a;ManualRepair r;escalate(a,r);need(a.snapshot(now).at("phase")=="recovering","slow IO immediately expired recovery confirmation");
  now+=29999;need(a.snapshot(now).at("phase")=="recovering","confirmation window shortened by cleanup IO");++now;need(a.snapshot(now).at("phase")=="manual-required","missing confirmation stayed healthy forever");});
 check("field own mode refresh pending preserves attempts and original not-before grace",[&]{
  arm();AutomaticRepair a;ManualRepair r;
  deps.gentleRecovery=[&](const auto&){++gentle;state["phase"]="waiting-manual-steam";state["reason"]="steam-ui-state-pending";state["revision"]=state.at("revision").get<unsigned>()+1;mode["mode"]="unknown";};
  const auto settled=[&]{state["phase"]=initial.at("phase");state["reason"]=initial.at("reason");mode["mode"]="bigpicture";};
  a.observe(r,deps);a.observe(r,deps);need(a.nextTimeout(now)==0xfffffffful,"pending shared read busy-polls cleanup");
  now+=10000;settled();a.observe(r,deps);need(gentle==1&&cleanupDispatches==0&&a.nextTimeout(now)==20000,"own pending refilled attempts or skipped the first grace");
  now+=20000;a.observe(r,deps);need(gentle==2&&cleanupDispatches==0,"second admitted gentle step missing");a.observe(r,deps);
  now+=10000;settled();a.observe(r,deps);need(gentle==2&&cleanupDispatches==0,"own second pending reset or skipped grace");now+=20000;a.observe(r,deps);
  need(gentle==2&&cleanupDispatches==1&&reservations==1,"actual field sequence never reaches bounded final cleanup");});
 check("new power generation or explicit OFF ends the gentle episode without refilling final fuse",[&]{
  arm();unsigned long long generation=1;deps.powerGeneration=[&]{return generation;};AutomaticRepair a;ManualRepair r;a.observe(r,deps);now+=30000;++generation;a.observe(r,deps);
  need(gentle==2&&cleanupDispatches==0,"new power generation consumed pre-suspend attempts");a.endEpisode();a.observe(r,deps);need(gentle==3&&cleanupDispatches==0,"explicit OFF did not close prior gentle episode");deps.powerGeneration={};
  bool stoppedWait=true,stoppedStatus=true,availableAfterNoop=true,uncertainNotReplayed=true;
  for(unsigned outcome=0;outcome<5;++outcome){arm();AutomaticRepair inflight;ManualRepair repair;
   deps.cleanupWorker=[&,outcome](const auto&,const auto&,const auto&)->Json{
    if(outcome==3)inflight.confirmed(currentSession);else{state["enabled"]=false;inflight.endEpisode();}
    if(outcome==0)return {{"ok",false},{"mutated",false},{"awaitCurrentIdentity",true},{"reason","decky-cleanup-account-unconfirmed"}};
    return {{"ok",outcome!=2&&outcome!=4},{"mutated",outcome!=4},{"uncertain",outcome==2||outcome==4},{"awaitCurrentIdentity",true},{"cleanupNotStarted",true},{"reason","decky-cleanup-account-unconfirmed"}};
   };
   escalate(inflight,repair);deps.cleanupWorker={};
   stoppedWait=stoppedWait&&inflight.nextTimeout(now)==0xfffffffful;
   stoppedStatus=stoppedStatus&&inflight.snapshot(now).value("phase",std::string{})=="idle";
   state["enabled"]=true;const auto previous=gentle;
   if(outcome==0){escalate(inflight,repair);availableAfterNoop=availableAfterNoop&&cleanupDispatches==1&&reservations==1;}
   else{currentSession.account=10;mode["observedAccount"]=10u;escalate(inflight,repair);uncertainNotReplayed=uncertainNotReplayed&&gentle==previous&&reservations==0;}
  }
  arm();generation=1;deps.powerGeneration=[&]{return generation;};AutomaticRepair power;ManualRepair repair;
  deps.cleanupWorker=[&](const auto&,const auto&,const auto&)->Json{++generation;return {{"ok",false},{"mutated",false},{"awaitCurrentIdentity",true},{"reason","decky-cleanup-account-unconfirmed"}};};
  escalate(power,repair);deps.cleanupWorker={};deps.powerGeneration={};
  const bool powerWaitStopped=power.nextTimeout(now)==0xfffffffful&&power.snapshot(now).value("phase",std::string{})=="idle";
  need(stoppedWait,"late pre-reservation result rearmed a closed recovery episode wait");
  need(stoppedStatus,"late recovery result overwrote OFF or authenticated ready status");
  need(availableAfterNoop,"closed unreserved identity wait poisoned the next valid recovery");
  need(uncertainNotReplayed,"closed dispatched or uncertain cleanup replenished same-process budget");
  need(powerWaitStopped,"completed power generation change revived the prior cleanup wait");
  // OFF can arrive after the final phase is published but before the shared
  // request creates any child or durable reservation. This proven no-op must
  // not poison the next explicitly enabled recovery in the same Steam process.
  arm();AutomaticRepair beforeChild;ManualRepair shared;bool cancelledBeforeChild=false;
  const auto offAtPublishedFinal=[&]{if(!cancelledBeforeChild&&beforeChild.snapshot(now).value("reason",std::string{})=="decky-stale-context-cleanup"){
   cancelledBeforeChild=true;state["enabled"]=false;beforeChild.endEpisode();}};
  beforeChild.observe(shared,deps,offAtPublishedFinal);now+=30000;beforeChild.observe(shared,deps,offAtPublishedFinal);now+=30000;beforeChild.observe(shared,deps,offAtPublishedFinal);
  const bool stayedClosed=beforeChild.nextTimeout(now)==0xfffffffful&&beforeChild.snapshot(now).value("phase",std::string{})=="idle"&&reservations==0;
  state["enabled"]=true;escalate(beforeChild,shared);
  need(cancelledBeforeChild&&stayedClosed,"pre-child OFF did not close the original episode");
  need(cleanupDispatches==1&&reservations==1,"proven pre-child cancellation permanently spent the same-Steam recovery opportunity");});
 check("offline effective-account switch starts a full new gentle episode",[&]{
  arm();currentSession.account=0;mode["deckyAccountWatchAvailable"]=true;mode["observedAccount"]=9u;
  AutomaticRepair a;ManualRepair r;a.observe(r,deps);now+=30000;a.observe(r,deps);
  need(gentle==2&&cleanupDispatches==0,"offline identity could not receive two gentle checks");
  mode["observedAccount"]=10u;now+=30000;a.observe(r,deps);
  need(gentle==3&&a.snapshot(now).value("gentleAttempts",0u)==1&&cleanupDispatches==0&&reservations==0,"new offline account inherited old account final-cleanup attempts");
  now+=29999;a.observe(r,deps);need(gentle==3&&cleanupDispatches==0,"new account lost its first full grace window");
  ++now;a.observe(r,deps);need(gentle==4&&a.snapshot(now).value("gentleAttempts",0u)==2,"new account second gentle check missing");});
 check("unconfirmed offline account never spends a gentle or final cleanup allowance",[&]{
  for(int n=0;n<5;++n){arm();currentSession.account=0;mode["deckyAccountWatchAvailable"]=true;mode["observedAccount"]=9u;
   if(n==0)mode["deckyAccountWatchAvailable"]=false;if(n==1)mode.erase("observedAccount");if(n==2)mode["observedAccount"]=0u;if(n==3)mode["observedAccount"]="9";if(n==4)mode["observedAccount"]=4294967296ull;
   AutomaticRepair a;ManualRepair r;escalate(a,r);need(gentle==0&&cleanupDispatches==0&&reservations==0&&a.nextTimeout(now)==0xfffffffful,"uncorroborated offline identity spent recovery allowance");}});
 check("registry account and verified current UI conflict park instead of accumulating failures",[&]{
  arm();mode["observedAccount"]=10u;mode["deckyAccountWatchAvailable"]=true;AutomaticRepair a;ManualRepair r;escalate(a,r);
  need(gentle==0&&cleanupDispatches==0&&reservations==0,"account transition accumulated cleanup failures under stale registry identity");
  currentSession.account=10;now+=30000;a.observe(r,deps);need(gentle==1&&a.snapshot(now).value("gentleAttempts",0u)==1,"verified converged account did not restart its gentle episode");});
 check("effective-account switch cannot replenish an already spent same-Steam final fuse",[&]{
  arm();AutomaticRepair a;ManualRepair r;escalate(a,r);need(cleanupDispatches==1&&reservations==1,"same-Steam spent-fuse control missing");
  currentSession.account=0;mode["observedAccount"]=10u;mode["deckyAccountWatchAvailable"]=true;escalate(a,r);
  need(cleanupDispatches==1&&reservations==1,"offline account switch refunded same-Steam final-cleanup fuse");
  AutomaticRepair reconstructed;ManualRepair reconstructedShared;const auto previous=gentle;escalate(reconstructed,reconstructedShared);
  need(cleanupDispatches==1&&reservations==1&&gentle==previous,"reconstructing YMCC recovery state replenished the persisted same-Steam final fuse");
  // The durable budget belongs to the verified Steam process, not a lifetime
  // RAM latch. New process/PID-reuse signals still require fresh UI identity
  // and BOTH full gentle windows; an old mode-cache entry cannot spend either.
  for(unsigned reusedPid=0;reusedPid<2;++reusedPid){
   arm();AutomaticRepair replaced;ManualRepair repair;escalate(replaced,repair);
   const auto priorGentle=gentle;const auto priorReservations=reservations;
   currentSession.account=10;if(!reusedPid)++currentSession.pid;++currentSession.created;
   replaced.observe(repair,deps);
   need(gentle==priorGentle&&reservations==priorReservations&&replaced.nextTimeout(now)==0xfffffffful,"new Steam accepted old mode identity or spent its recovery grace");
   mode["pid"]=currentSession.pid;mode["birth"]=std::to_string(currentSession.created);mode["observedAccount"]=10u;mode["modeEpoch"]="replacement-document:1";
   replaced.observe(repair,deps);
   need(gentle==priorGentle+1&&replaced.snapshot(now).value("gentleAttempts",0u)==1&&cleanupDispatches==1,"verified replacement Steam inherited the old spent RAM latch");
   now+=29999;replaced.observe(repair,deps);need(gentle==priorGentle+1&&cleanupDispatches==1,"replacement Steam lost the first full grace");
   ++now;replaced.observe(repair,deps);need(gentle==priorGentle+2&&cleanupDispatches==1,"replacement Steam second gentle check missing");
   now+=29999;replaced.observe(repair,deps);need(cleanupDispatches==1,"replacement Steam lost the second full grace");
   ++now;replaced.observe(repair,deps);need(cleanupDispatches==2&&reservations==priorReservations+1&&ledger.at("steamSession")==ymcc::deckyupdate::ticket(currentSession),"verified replacement Steam failed to consume exactly its own new slot");
   currentSession.account=0;mode["observedAccount"]=11u;escalate(replaced,repair);
   need(cleanupDispatches==2&&reservations==priorReservations+1,"account change refilled replacement Steam final-cleanup slot");
  }});
 check("same effective account preserves original gentle debt across registry UI source handoff",[&]{
  arm();currentSession.account=0;mode["deckyAccountWatchAvailable"]=true;mode["observedAccount"]=9u;AutomaticRepair a;ManualRepair r;a.observe(r,deps);
  now+=10000;currentSession.account=9;a.observe(r,deps);need(gentle==1&&a.nextTimeout(now)==20000,"registry resumed same account but refilled recovery debt");
  now+=10000;currentSession.account=0;a.observe(r,deps);need(gentle==1&&a.nextTimeout(now)==10000,"registry zero erased same UI account grace");
  now+=10000;currentSession.account=9;a.observe(r,deps);need(gentle==2&&a.snapshot(now).value("gentleAttempts",0u)==2,"same identity handoff never reached second gentle recovery");
  for(unsigned direction=0;direction<2;++direction){
   arm();currentSession.account=direction?9:0;AutomaticRepair pending;ManualRepair repair;pending.observe(repair,deps);now+=10000;
   state["phase"]="waiting-manual-steam";state["reason"]="steam-ui-state-pending";mode["mode"]="unknown";currentSession.account=direction?0:9;
   pending.observe(repair,deps);need(pending.nextTimeout(now)==0xfffffffful&&gentle==1&&cleanupDispatches==0,"pending handoff dispatched or busy-polled");
   state["phase"]=initial.at("phase");state["reason"]=initial.at("reason");mode["mode"]="bigpicture";pending.observe(repair,deps);
   need(gentle==1&&pending.nextTimeout(now)==20000,"pending registry/UI handoff erased same effective-account debt or grace");
   now+=20000;pending.observe(repair,deps);need(gentle==2&&cleanupDispatches==0,"pending same-account handoff lost its second gentle step");
  }});
 check("actual native dependency callback carries verified offline account through the existing child protocol",[&]{
  arm();currentSession.pid=GetCurrentProcessId();currentSession.account=0;mode["pid"]=currentSession.pid;mode["observedAccount"]=10u;mode["deckyAccountWatchAvailable"]=true;
  g_steamModeObserver.read=[&]{return mode;};g_deckySidebarRuntime.read=[&]{return state;};
  const Json action={{"finalCleanup",true},{"pid",currentSession.pid},{"birth",std::to_string(currentSession.created)},{"revision",state.at("revision")},{"modeEpoch",mode.at("modeEpoch")}};
  deps.cleanupWorker=actualMainCleanupDispatch;ManualRepair r;const auto result=r.request(action,deps,Origin::FinalCleanup);
  workerResults.push_back({{"kind","actual-main-offline-dispatch"},{"result",result}});
  need(result.value("ok",false)&&result.value("verifiedAccount",0u)==10&&result.value("fromUi",false)&&result.value("cleanupWorkerDestroyed",false),"actual native callback lost verified offline account or protocol child ownership");deps.cleanupWorker={};});
 check("new offline account receives both full grace windows before actual native child dispatch",[&]{
  arm();currentSession.pid=GetCurrentProcessId();currentSession.account=0;mode["pid"]=currentSession.pid;mode["observedAccount"]=9u;mode["deckyAccountWatchAvailable"]=true;
  g_steamModeObserver.read=[&]{return mode;};g_deckySidebarRuntime.read=[&]{return state;};Json actualResult;unsigned actualDispatches=0;
  deps.cleanupWorker=[&](const auto& selected,const auto& action,const auto& current){++actualDispatches;return actualResult=actualMainCleanupDispatch(selected,action,current);};
  AutomaticRepair a;ManualRepair r;a.observe(r,deps);now+=30000;a.observe(r,deps);mode["observedAccount"]=10u;now+=30000;a.observe(r,deps);
  need(actualDispatches==0&&gentle==3,"new account used old account grace before actual dispatch");now+=30000;a.observe(r,deps);need(actualDispatches==0&&gentle==4,"new account skipped its second full grace");now+=30000;a.observe(r,deps);
  workerResults.push_back({{"kind","actual-main-new-account-after-grace"},{"result",actualResult}});
  need(actualDispatches==1&&actualResult.value("ok",false)&&actualResult.value("verifiedAccount",0u)==10&&actualResult.value("fromUi",false)&&actualResult.value("cleanupWorkerDestroyed",false),"actual native dispatch did not carry current account after both recovery grace windows");deps.cleanupWorker={};});
 check("missing current UI witness never spends native final-cleanup reservation",[&]{
  arm();mode["deckyAccountWatchAvailable"]=false;ManualRepair r;
  const Json args={{"finalCleanup",true},{"pid",session.pid},{"birth",std::to_string(session.created)},{"revision",state.at("revision")},{"modeEpoch",mode.at("modeEpoch")}};
  const auto result=r.request(args,deps,Origin::FinalCleanup);need(!result.value("ok",true)&&reservations==0&&cleanupDispatches==0,"unready current-user callback consumed durable cleanup fuse");
  mode["deckyAccountWatchAvailable"]=true;need(r.request(args,deps,Origin::FinalCleanup).value("ok",false)&&reservations==1&&cleanupDispatches==1,"later verified identity could not use original unspent reservation");});
 check("known registry account with an unconfirmed UI witness parks automatic gentle attempts",[&]{
  arm();mode["deckyAccountWatchAvailable"]=false;AutomaticRepair a;ManualRepair r;escalate(a,r);
  need(gentle==0&&reservations==0&&cleanupDispatches==0&&a.nextTimeout(now)==0xfffffffful,"registry identity alone spent UI-refresh recovery attempts");});
 check("fresh UI witness withdrawal before reservation can resume only after reverified identity",[&]{
  for(unsigned switched=0;switched<2;++switched){arm();const auto original=deps.freshMode;bool witness=false;
   deps.freshMode=[&](const auto&)->Json{auto fresh=mode;fresh["deckyAccountWatchAvailable"]=witness;return fresh;};
   AutomaticRepair a;ManualRepair r;escalate(a,r);const bool untouched=reservations==0&&cleanupDispatches==0;
   const auto waitingGentle=gentle;for(unsigned i=0;i<8;++i)a.observe(r,deps);
   bool bounded=gentle==waitingGentle&&reservations==0&&cleanupDispatches==0&&a.nextTimeout(now)==30000;
   witness=true;
   if(switched){currentSession.account=10;mode["observedAccount"]=10u;}
   else{now+=29999;a.observe(r,deps);bounded=bounded&&gentle==waitingGentle&&cleanupDispatches==0;++now;}
   a.observe(r,deps);now+=30000;a.observe(r,deps);now+=30000;a.observe(r,deps);
   const bool recovered=reservations==1&&cleanupDispatches==1;deps.freshMode=original;
   need(untouched,"withdrawn UI proof spent cleanup fuse before a valid dispatch");need(bounded,"stale cached identity caused immediate repeated cleanup attempts");need(recovered,"proven pre-reservation identity withdrawal poisoned later verified recovery");}});
 check("uncertain result carrying an identity-wait hint cannot refund a possible cleanup",[&]{
  for(unsigned signal=0;signal<3;++signal){arm();deps.cleanupWorker=[signal](const auto&,const auto&,const auto&)->Json{return {{"ok",false},{"mutated",signal!=1},{"uncertain",signal!=2},{"awaitCurrentIdentity",true},{"reason","decky-cleanup-account-unconfirmed"}};};
   AutomaticRepair a;ManualRepair r;escalate(a,r);const auto count=gentle;currentSession.account=10;mode["observedAccount"]=10u;escalate(a,r);
   const bool stayedSpent=gentle==count;deps.cleanupWorker={};need(stayedSpent,"uncertain cleanup refunded its budget because of an identity hint");}});
 deps.dispatch=oldDispatch;
 check("native managed receipt continues ordinary same-Steam hosts only after completed backend lifecycle",[&]{
  using namespace ymcc::deckyupdate;
  const auto oldHost=std::string("501:10001"),newHost=std::string("502:10002"),thirdHost=std::string("503:10003");
  auto observed=initialMode;observed["modeEpoch"]="managed-document-a:1";observed["deckyAccountWatchAvailable"]=true;observed["observedAccount"]=9u;
  auto disk=managed::completed(Json(),session,"managed-document-a",oldHost,build,"v3.3.0");
  need(managed::valid(disk)&&disk.at("status")=="ready","healthy initial native baseline missing");
  Recovery host;need(host.continueManaged(session,observed,disk,disk,newHost,build,"v3.3.0")&&host.allows(session),"ordinary same-build host cannot continue native-completed lifecycle");
  unsigned gentleCalls=0,dispatchCalls=0;unsigned long long tick=1000;auto runtime=initial;auto seen=observed;
  Dependencies d;d.session=[&]{return session;};d.runtime=[&]{return runtime;};d.mode=[&](const auto&){return seen;};d.freshMode=d.mode;d.installed=[&]{return build;};d.loader="v3.3.0";d.exiting=[]{return false;};d.now=[&]{return tick;};d.preflight=[]{return std::string{};};d.consent=[&](const auto& s){return host.allows(s);};d.cleanupAllowed=d.consent;
  d.gentleRecovery=[&](const auto&){++gentleCalls;};d.reserveCleanup=[&](const auto& s){const auto doc=managed::document(seen.at("modeEpoch"));if(!cleanupAvailable(disk,s,doc))return false;disk=managed::record(s,doc,newHost,build,"v3.3.0","reserved");return true;};
  d.dispatch=[&](const auto&,const auto& action,const auto& admit){need(action.at("operation")=="decky.cleanupConnection"&&admit(),"native continuation bypassed actual final-cleanup admission");++dispatchCalls;return Json{{"ok",true},{"mutated",true}};};
  AutomaticRepair automatic;ManualRepair repair;automatic.observe(repair,d);tick+=30000;automatic.observe(repair,d);tick+=30000;automatic.observe(repair,d);
  need(gentleCalls==2&&dispatchCalls==1&&disk.at("status")=="reserved","continued host did not perform two gentle attempts and one durable reservation");
  Recovery uncertain;need(!uncertain.continueManaged(session,seen,disk,disk,thirdHost,build,"v3.3.0"),"uncertain reservation refilled after host restart");
  need(managed::completed(disk,session,"managed-document-a",newHost,build,"v3.3.0").is_null(),"old document snapshot completed its own pending reservation");
  need(managed::completed(disk,session,"managed-document-b",thirdHost,build,"v3.3.0").is_null(),"different host fabricated the previous native completion");
  disk=managed::completed(disk,session,"managed-document-b",newHost,build,"v3.3.0");seen["modeEpoch"]="managed-document-b:1";
  need(uncertain.continueManaged(session,seen,disk,disk,thirdHost,build,"v3.3.0"),"verified second host completion did not enable next ordinary lifecycle");
  for(unsigned n=0;n<5;++n){tick+=30000;automatic.observe(repair,d);}need(dispatchCalls==1,"completed receipt refilled the current host automatic fuse");
  // Complete three recovery cycles in one native host and Steam process.
  // Automatic/ManualRepair and lifecycle service/actual private child/writer are
  // real. UI rebuild and durable reservation IO remain explicit fixture ports.
  {namespace f=lifecycleServiceFixture;auto live=session;live.pid=GetCurrentProcessId();live.created=cleanupworker::processBirth(GetCurrentProcess());need(live.created!=0,"same-host fixture birth missing");
   wchar_t ownImage[32768]{};const auto ownLength=GetModuleFileNameW(nullptr,ownImage,32768);need(ownLength&&ownLength<32768,"same-host fixture image path missing");
   const auto backendDir=fspath::path(fixtureBackendRoot())/L"decky";fspath::create_directories(backendDir);const auto backendImage=backendDir/L"PluginLoader_noconsole.exe";
   need(CopyFileW(ownImage,backendImage.c_str(),FALSE)!=FALSE,"same-host suspended control copy failed");
   struct BackendControl{HANDLE job=nullptr,process=nullptr,thread=nullptr;bool stop(){if(job){CloseHandle(job);job=nullptr;}bool stopped=true;if(process){stopped=WaitForSingleObject(process,2000)==WAIT_OBJECT_0;if(!stopped){TerminateProcess(process,0);stopped=WaitForSingleObject(process,2000)==WAIT_OBJECT_0;}CloseHandle(process);process=nullptr;}if(thread){CloseHandle(thread);thread=nullptr;}f::fixtureBackendPid=61;f::fixtureBackendBirth=1234;return stopped;}~BackendControl(){stop();}}control;
   control.job=CreateJobObjectW(nullptr,nullptr);need(control.job!=nullptr,"same-host test backend Job missing");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(SetInformationJobObject(control.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"same-host Job containment failed");
   STARTUPINFOW backendStartup{};backendStartup.cb=sizeof(backendStartup);PROCESS_INFORMATION backendProcess{};std::wstring backendCommand=L"\""+backendImage.wstring()+L"\" --inert-never-resumed";
   need(CreateProcessW(backendImage.c_str(),backendCommand.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&backendStartup,&backendProcess)!=FALSE,"same-host suspended backend process failed");
   control.process=backendProcess.hProcess;control.thread=backendProcess.hThread;need(AssignProcessToJobObject(control.job,control.process)!=FALSE,"same-host suspended backend not in test Job");
   f::fixtureBackendPid=backendProcess.dwProcessId;f::fixtureBackendBirth=cleanupworker::processBirth(control.process);need(f::fixtureBackendBirth!=0,"same-host suspended backend birth unavailable");
   const auto nativeHost=cleanupworker::nativeHost(),loader=std::string(ymcc::deckyidentity::version);const auto baseline=Json{{"schemaVersion",1},{"extensions",{{"deckySidebar",{{"enabled",true}}}}},{"businessOpaque","preserve"}};
   auto view=initialMode;view["pid"]=live.pid;view["birth"]=std::to_string(live.created);view["modeEpoch"]="samehost-recovery-0:1";view["deckyAccountWatchAvailable"]=true;view["observedAccount"]=live.account;view["deckyUpdateResidue"]["loaderVersion"]=loader;
   // Existing schema operation, two strictly separated inspect shapes. An
   // owned readback is admitted only with a live-backend-shaped state; confirm
   // cannot borrow the old no-backend inspect shape.
   const auto shapeArgs=Json{{"task","inspect"},{"modeEpoch","shape-proof:1"},{"frontendBuildId",build},{"loaderVersion",loader},{"backendPid",f::fixtureBackendPid},{"backendBirth",std::to_string(f::fixtureBackendBirth)}};
   const auto shapeState=Json{{"enabled",true},{"pid",f::fixtureBackendPid},{"phase","loader-started"},{"cleanupNativeHost",nativeHost}};
   need(cleanupworker::lifecycleRequest(shapeArgs,shapeState),"owned read-only inspect request was rejected by actual private schema");
   auto plainArgs=shapeArgs,plainState=shapeState;plainArgs["backendPid"]=0u;plainArgs["backendBirth"]="0";plainState["pid"]=0u;
   need(cleanupworker::lifecycleRequest(plainArgs,plainState),"old retained no-backend inspect shape changed");
   plainArgs["task"]="confirm";need(!cleanupworker::lifecycleRequest(plainArgs,plainState),"write confirmation borrowed no-backend read-only admission");
   for(unsigned invalid=0;invalid<5;++invalid){auto badArgs=shapeArgs,badState=shapeState;if(invalid==0)badState["enabled"]=false;if(invalid==1)badState["phase"]="frontend-attached";if(invalid==2)badState["pid"]=0u;if(invalid==3)badArgs["backendBirth"]="0";if(invalid==4)badArgs["task"]="read-and-repair";
    need(!cleanupworker::lifecycleRequest(badArgs,badState),"owned read-only inspect admitted malformed or unowned shape");}
   auto cycleDisk=managed::record(live,"samehost-recovery-0",nativeHost,build,loader,"ready");unsigned cycleGentle=0,cycleDispatch=0,cycleReserve=0;bool cyclePremature=false;
   f::g_steamSettingsObserver.value=live;f::g_steamModeObserver.value=view;f::g_deckySidebarRuntime.state={};f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=2100;f::tick=1000;f::build=build;f::backendReady=true;f::beforeReply={};f::responseOverride=nullptr;f::useActualPrivateChild=true;f::holdActualPrivateChild=false;f::privateResults=Json::array();f::dispatches=0;
   f::g_deckyUpdateRecovery.userEnable(live);f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};
   Dependencies cycle;cycle.session=[&]{return live;};cycle.runtime=[&]{return f::deckySidebarStateJson(f::g_deckySidebarRuntime.state);};cycle.mode=[&](const auto&){return view;};cycle.freshMode=cycle.mode;cycle.installed=[&]{return build;};cycle.loader=loader;cycle.exiting=[]{return false;};cycle.now=[&]{return f::tick;};cycle.preflight=[]{return std::string{};};cycle.consent=[](const auto&){return true;};cycle.cleanupAllowed=cycle.consent;cycle.powerGeneration=[&]{return f::power;};
   cycle.gentleRecovery=[&](const auto&){++cycleGentle;};cycle.reserveCleanup=[&](const auto& selected){const auto doc=managed::document(view.at("modeEpoch"));if(!cleanupAvailable(cycleDisk,selected,doc))return false;++cycleReserve;cycleDisk=managed::record(selected,doc,nativeHost,build,loader,"reserved");return true;};
   cycle.dispatch=[&](const auto&,const auto& action,const auto& admit)->Json{need(action.at("operation")=="decky.cleanupConnection"&&admit(),"same-host final request bypassed original typed admission");++cycleDispatch;const auto premature=managed::record(live,"premature-completion",nativeHost,build,loader,"ready");cyclePremature=f::g_deckyAutomaticRepair.completedManaged(live,premature,"premature-completion",nativeHost,build,loader,f::g_deckyAutomaticRepair.completionEpoch());return {{"ok",true},{"mutated",true},{"requestDispatched",true}};};
   ManualRepair sameHostRepair;
   for(unsigned round=0;round<3;++round){f::g_deckySidebarRuntime.state={};f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);
    need(cycleGentle==round*2+1&&cycleDispatch==round,"same-host next recovery did not receive its first gentle request after certified completion");
    f::tick+=29999;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);need(cycleGentle==round*2+1,"same-host recovery skipped its first original grace");
    ++f::tick;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);need(cycleGentle==round*2+2&&cycleDispatch==round,"same-host recovery skipped second gentle request");
    f::tick+=30000;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);need(!cyclePremature,"in-flight final dispatch accepted a premature completion");need(cycleDispatch==round+1&&cycleReserve==round+1&&cycleDisk.at("status")=="reserved","same-host recovery lost its original durable reservation");
    const auto spentDoc=managed::document(view.at("modeEpoch"));f::g_deckyAutomaticRepair.confirmed(live);
    const auto completedDoc="samehost-recovery-"+std::to_string(round+1);const auto candidate=managed::record(live,completedDoc,nativeHost,build,loader,"ready");
    const auto originalCompletionEpoch=f::g_deckyAutomaticRepair.completionEpoch();
    // Closing the epoch is destructive: place it LAST so account/root/birth
    // refusals must run under a fresh token instead of borrowing stale-epoch rejection.
    for(unsigned ordinal=0;ordinal<13;++ordinal){const auto invalid=ordinal==12?9u:(ordinal>=9?ordinal+1:ordinal);auto bad=candidate;auto effective=live;auto doc=completedDoc;auto epoch=originalCompletionEpoch;
     if(invalid==0)bad=nullptr;if(invalid==1)bad["status"]="reserved";if(invalid==2){doc=spentDoc;bad=managed::record(live,spentDoc,nativeHost,build,loader,"ready");}
     if(invalid==3)bad["host"]="999:12345";if(invalid==4)bad["steamSession"]="888:12345";if(invalid==5)bad["frontendBuildId"]=std::string(64,'b');
     if(invalid==6)bad["loaderVersion"]="v3.3.0";if(invalid==7)bad["document"]="wrong-completion";if(invalid==8)bad["extra"]=true;
     if(invalid==9){f::g_deckyAutomaticRepair.endEpisode();epoch=originalCompletionEpoch;}if(invalid==10)effective.account=10;if(invalid==11)effective.root=L"other-root";if(invalid==12){++effective.created;bad=managed::record(effective,doc,nativeHost,build,loader,"ready");}
     need(!f::g_deckyAutomaticRepair.completedManaged(effective,bad,doc,nativeHost,build,loader,epoch),"invalid or stale native record reopened same-host final cleanup");
    }
    for(unsigned n=0;n<256;++n){++f::tick;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);}need(cycleGentle==round*2+2&&cycleDispatch==round+1,"plain authenticated ready refunded a spent same-host recovery");
    view["modeEpoch"]="samehost-recovery-"+std::to_string(round+1)+":1";f::g_steamModeObserver.value=view;f::g_deckySidebarRuntime.state.pid=f::fixtureBackendPid;f::g_deckySidebarRuntime.state.phase="loader-started";
    f::privateMain=baseline;f::privateMain["deckyContextCleanup"]=cycleDisk;f::privateBackup=f::privateMain;
    f::dropNextConfirmReply=round==1;
    f::deckySidebarQueueManagedReady(live,f::fixtureBackendPid,std::string(32,'c'),"1");f::deckySidebarServiceManagedLifecycle();if(round==1){f::tick+=1000;f::deckySidebarServiceManagedLifecycle();}
    if(round==1){need(f::privateResults.size()==3&&f::privateResults.back().at("task")=="inspect","lost successful completion reply was not reconciled by read-only owned inspection");
     need(f::privateResults.back().at("result").value("fixtureSettingsWrites",99u)==0,"completion reply readback replayed native settings writes");}
    need(f::privateResults.size()==round+1+(round>=1?1u:0u),"same-host owned-ready completion did not execute expected original private work");const auto result=f::privateResults.back().at("result");
    need(result.value("ok",false)&&result.value("cleanupWorkerDestroyed",false)&&result.value("cleanupWorkerHandles",99u)==0&&result.value("cleanupWorkerProcesses",99u)==0,"same-host private completion failed or retained owned resources");
    const auto left=result.at("fixtureSavedMain"),right=result.at("fixtureSavedBackup");need(left==right&&left.at("businessOpaque")=="preserve"&&left.at("deckyContextCleanup").at("status")=="ready"&&left.at("deckyContextCleanup").at("document")!=spentDoc,"same-host native completion failed its different-document matching-copy proof");cycleDisk=left.at("deckyContextCleanup");
   }
   const auto threeCycleReplies=f::privateResults,threeCycleLedger=cycleDisk;const auto threeCycleGentle=cycleGentle,threeCycleDispatch=cycleDispatch,threeCycleReserve=cycleReserve;
   // Corrupt/partial/foreign history and lost readback results cannot refund
   // the old mutation. Synthetic Steam birth isolates each same original case
   // row; only the backend's PID/birth/path/session proof is actual Windows IO.
   for(unsigned terminalVariant=0;terminalVariant<3;++terminalVariant){
    const auto beforeDoc="broker-before-recovery-"+std::to_string(terminalVariant),afterDoc="broker-after-recovery-"+std::to_string(terminalVariant);
    const bool expiredOriginal=terminalVariant!=0,lateReadable=terminalVariant!=2;
    live.created+=500000;view["pid"]=live.pid;view["birth"]=std::to_string(live.created);view["modeEpoch"]=beforeDoc+":1";
    cycleDisk=managed::record(live,beforeDoc,nativeHost,build,loader,"ready");
    f::g_steamSettingsObserver.value=live;f::g_steamModeObserver.value=view;f::g_steamModeObserver.afterSnapshot={};f::g_steamModeObserver.actual=nullptr;
    f::g_deckySidebarRuntime.state={};f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=4100;f::tick=1000;f::build=build;
    f::g_deckyUpdateRecovery.userEnable(live);f::afterPrivateReply={};f::afterDroppedConfirm={};f::ownedInspectReplyOverride=nullptr;f::rawReplyKind=0;
    f::privateResults=Json::array();f::captureManagedGuard=false;
    const auto gentleBefore=cycleGentle,dispatchBefore=cycleDispatch;
    f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);f::tick+=30000;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);f::tick+=30000;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);
    need(cycleGentle==gentleBefore+2&&cycleDispatch==dispatchBefore+1,"broker producer joint skipped original graces/reservation");
    view["modeEpoch"]=afterDoc+":1";f::g_steamModeObserver.value=view;f::g_deckySidebarRuntime.state.pid=f::fixtureBackendPid;f::g_deckySidebarRuntime.state.phase="loader-started";
    f::privateMain=baseline;f::privateMain["deckyContextCleanup"]=cycleDisk;f::privateBackup=f::privateMain;
    ymcc::deckymirror::Broker actualBroker;std::atomic<unsigned> scenario{0},requests{0};std::atomic<bool> queuedAll{true};unsigned long long revision=0;
    ymcc::deckymirror::Config config{[&](const std::string& connection,const Json& request){
      const auto selectedScenario=scenario.load();Json snapshot={{"revision",++revision},{"runId",request.at("runId")},{"ready",true},{"provenance",{{"kind","ymcc-native"},{"pid",GetCurrentProcessId()}}}};
      if(selectedScenario==0)snapshot["ready"]=false;if(selectedScenario==1)snapshot["provenance"]["pid"]=GetCurrentProcessId()+1u;if(selectedScenario==2)snapshot["provenance"]["kind"]="foreign-provider";
      const Json args={{"connection",connection},{"message",{{"type","snapshot"},{"runId",request.at("runId")},{"snapshot",snapshot}}}};
      if(!f::actualNativeMirrorReply(args).get<bool>())queuedAll=false;++requests;
    },{}};
    need(actualBroker.start(config),"actual producer joint broker failed start");f::g_deckyMirrorBroker.actual=&actualBroker;
    struct StopBroker{ymcc::deckymirror::Broker& owner;~StopBroker(){f::g_deckyMirrorBroker.actual=nullptr;owner.stop();}}stopBroker{actualBroker};
    const auto boot=actualBroker.bootstrap();actualBrokerClient::LoopbackSocket first(boot);const auto firstConnection=actualBroker.status().at("connection").get<std::string>();
    const auto priorReady=f::g_deckyManagedReady;const auto costBefore=f::g_deckyCostContext.replies.load();Json negativeRows=Json::array();
    for(unsigned denied=0;denied<5;++denied){
      if(denied==3)f::g_deckySidebarRuntime.state.enabled=false;if(denied==4)f::g_deckySidebarRuntime.state.pid=0;
      scenario.store(denied);first.sendFrame(Json{{"command","snapshot"},{"runId",boot.at("runId")}}.dump());const auto response=Json::parse(first.readFrame());need(actualBrokerClient::waitUntil([&]{return requests.load()==denied+1;},1000),"native producer did not complete negative reply callback");
      need(response.value("type",std::string{})=="snapshot"&&response.at("snapshot").at("revision")==denied+1,"actual broker authenticated negative request/reply failed");
      need(f::deckySidebarSameManagedReady(priorReady,f::g_deckyManagedReady)&&priorReady.finished==f::g_deckyManagedReady.finished&&f::privateResults.empty(),"unready/foreign/inactive producer queued native ready or private work");
      negativeRows.push_back({{"variant",denied},{"wireReplyReceived",true},{"readyUnchanged",true},{"privateDispatches",0}});
      f::g_deckySidebarRuntime.state.enabled=true;f::g_deckySidebarRuntime.state.pid=f::fixtureBackendPid;
    }
    scenario=5;first.sendFrame(Json{{"command","snapshot"},{"runId",boot.at("runId")}}.dump());const auto nativeReply=Json::parse(first.readFrame());need(actualBrokerClient::waitUntil([&]{return requests.load()==6;},1000),"native producer did not complete current ready callback");
    need(nativeReply.at("snapshot").value("ready",false)&&f::g_deckyManagedReady.connection==firstConnection&&!f::g_deckyManagedReady.finished,"actual broker/native producer failed to queue current ready");
    const auto readyBeforeStale=f::g_deckyManagedReady;auto stale=Json{{"connection","not-current"},{"message",nativeReply}};need(!f::actualNativeMirrorReply(stale).get<bool>(),"native producer bypassed actual broker connection rejection");
    stale["connection"]=firstConnection;stale["message"]["runId"]=std::string(32,'0');need(!f::actualNativeMirrorReply(stale).get<bool>(),"native producer bypassed actual broker run rejection");
    need(f::deckySidebarSameManagedReady(readyBeforeStale,f::g_deckyManagedReady)&&readyBeforeStale.finished==f::g_deckyManagedReady.finished,"stale producer reply changed managed ready");
    f::dropNextConfirmReply=true;f::actualManagedObserverGuard();
    need(f::privateResults.size()==1&&f::privateResults.front().at("task")=="confirm"&&f::privateResults.front().at("result").value("fixtureSettingsWrites",99u)==2&&f::g_deckyManagedReadbackActive,"actual native producer did not enter retired-write/readback chain");
    const auto opened=f::g_deckyManagedReadback.opened,observation=f::g_deckyManagedReadback.observation;
    if(expiredOriginal){f::tick=opened+30001;f::actualManagedObserverGuard();
    need(!f::g_deckyManagedReadbackActive&&f::privateResults.size()==1,"expired readback did not retire without a write replay");
    workerResults.push_back({{"kind","expired-readback-before-fresh-native-ready"},{"opened",opened},{"now",f::tick},{"originalObservation",observation},{"readbackActive",f::g_deckyManagedReadbackActive.load()},{"nativeReplies",f::privateResults},{"durableMain",f::privateMain.at("deckyContextCleanup")},{"durableBackup",f::privateBackup.at("deckyContextCleanup")}});
    }
    first.close();actualBrokerClient::LoopbackSocket second(boot);second.sendFrame(Json{{"command","snapshot"},{"runId",boot.at("runId")}}.dump());const auto reconnectedReply=Json::parse(second.readFrame());need(actualBrokerClient::waitUntil([&]{return requests.load()==7;},1000),"native producer did not complete reconnected ready callback");
    const auto secondConnection=actualBroker.status().at("connection").get<std::string>();
    need(firstConnection!=secondConnection,"terminal recovery did not use a genuinely new authenticated connection");
    if(!expiredOriginal)need(f::g_deckyManagedReadbackActive&&f::g_deckyManagedReadback.origin.connection==secondConnection&&f::g_deckyManagedReadback.opened==opened&&f::g_deckyManagedReadback.observation==observation,"active native-ready handoff refilled original readonly window");
    const auto readyState=f::g_deckySidebarRuntime.state;f::g_deckySidebarRuntime.state={};cycleDisk=f::privateMain.at("deckyContextCleanup");
    const auto gentleAtReceipt=cycleGentle;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);need(cycleGentle==gentleAtReceipt,"native producer generic confirmed refunded spent lifecycle before ledger readback");f::g_deckySidebarRuntime.state=readyState;
    if(lateReadable){
    if(!expiredOriginal)f::tick=opened+1000;
    f::actualManagedObserverGuard();
    workerResults.push_back({{"kind","readback-after-fresh-native-ready"},{"terminalVariant",terminalVariant},{"now",f::tick},{"readbackActive",f::g_deckyManagedReadbackActive.load()},{"readyFinished",f::g_deckyManagedReady.finished},{"nativeReplies",f::privateResults},{"sameBackendPid",f::g_deckySidebarRuntime.state.pid==f::fixtureBackendPid},{"actualNewBrokerConnection",firstConnection!=secondConnection}});
    need(f::privateResults.size()==2&&f::privateResults.back().at("task")=="inspect"&&f::privateResults.back().at("result").value("fixtureSettingsReads",0u)>0&&f::privateResults.back().at("result").value("fixtureSettingsWrites",99u)==0&&!f::g_deckyManagedReadbackActive,"fresh authenticated native ready after expired readback left completed durable recovery unreconciled");
    if(expiredOriginal)need(f::g_deckyManagedReadback.opened>opened&&f::g_deckyManagedReadback.observation!=observation&&f::g_deckyManagedReadback.lateUsed,"late reconciliation did not retain singleton distinct readonly authority");
    else need(f::g_deckyManagedReadback.opened==opened&&f::g_deckyManagedReadback.observation==observation&&!f::g_deckyManagedReadback.lateUsed,"active handoff minted terminal readonly allowance");
    cycleDisk=f::privateMain.at("deckyContextCleanup");f::g_deckySidebarRuntime.state={};f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);need(cycleGentle==gentleAtReceipt+1,"actual producer/readback did not permit next gentle lifecycle");
    }else{
      const auto goodMode=f::g_steamModeObserver.value;f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value["known"]=false;f::g_steamModeObserver.value.erase("modeEpoch");
      f::actualManagedObserverGuard();const auto lateOpened=f::g_deckyManagedReadback.opened,lateObservation=f::g_deckyManagedReadback.observation;
      need(f::g_deckyManagedReadbackActive&&f::g_deckyManagedReadback.awaitingMode&&f::g_deckyManagedReadback.lateUsed&&lateOpened>opened&&lateObservation!=observation&&f::privateResults.size()==1,"pending late proof did not park singleton readonly budget without IO");
      second.sendFrame(Json{{"command","snapshot"},{"runId",boot.at("runId")}}.dump());Json::parse(second.readFrame());need(actualBrokerClient::waitUntil([&]{return requests.load()==8;},1000),"duplicate late native-ready callback did not finish");
      need(f::g_deckyManagedReadback.opened==lateOpened&&f::g_deckyManagedReadback.observation==lateObservation&&f::privateResults.size()==1,"duplicate native-ready refilled singleton late budget or dispatched IO");
      f::tick=lateOpened+30001;f::actualManagedObserverGuard();need(!f::g_deckyManagedReadbackActive,"singleton late observation did not expire");
      f::g_steamModeObserver.value=goodMode;second.close();actualBrokerClient::LoopbackSocket third(boot);third.sendFrame(Json{{"command","snapshot"},{"runId",boot.at("runId")}}.dump());Json::parse(third.readFrame());need(actualBrokerClient::waitUntil([&]{return requests.load()==9;},1000),"third native-ready callback did not finish");
      f::actualManagedObserverGuard();
      workerResults.push_back({{"kind","singleton-late-readback-exhaustion"},{"nativeRequests",requests.load()},{"readbackActive",f::g_deckyManagedReadbackActive.load()},{"lateUsed",f::g_deckyManagedReadback.lateUsed},{"lateOpened",lateOpened},{"lateObservation",lateObservation},{"finalOpened",f::g_deckyManagedReadback.opened},{"finalObservation",f::g_deckyManagedReadback.observation},{"nativeReplies",f::privateResults}});
      need(!f::g_deckyManagedReadbackActive&&f::g_deckyManagedReadback.lateUsed&&f::g_deckyManagedReadback.opened==lateOpened&&f::g_deckyManagedReadback.observation==lateObservation&&f::privateResults.size()==1,"additional authenticated sockets refilled spent singleton late readonly allowance");
      f::g_deckySidebarRuntime.state={};cycleDisk=f::privateMain.at("deckyContextCleanup");f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);need(cycleGentle==gentleAtReceipt,"expired late proof refunded original uncertain write without native inspection");third.close();
    }
    second.close();f::g_deckyMirrorBroker.actual=nullptr;actualBroker.stop();const auto stopped=actualBroker.status();need(!stopped.value("active",true)&&!stopped.value("connected",true),"actual joint broker did not stop");
    need(queuedAll&&requests==(lateReadable?7u:9u)&&f::g_deckyCostContext.replies==costBefore+(lateReadable?7u:9u),"native producer did not consume declared actual authenticated replies");
    workerResults.push_back({{"kind","actual-broker-terminal-readback-native-ready"},{"terminalVariant",terminalVariant},{"requests",requests.load()},{"acceptedSnapshots",requests.load()},{"deniedReadyRows",negativeRows},{"staleConnectionAndRunRefused",true},{"realConnectionChanged",true},{"originalObservationExpired",expiredOriginal},{"lateReadonlyInspectionSucceeded",expiredOriginal&&lateReadable},{"lateAllowanceExhaustedWithoutRearming",!lateReadable},{"activeOriginalWindowUnchanged",!expiredOriginal},{"lateOriginalWriteNotRearmed",true},{"nativeReplies",f::privateResults},{"originalConfirmNotReplayed",true},{"nextGentleAllowedOnlyAfterNativeReadback",true},{"brokerStopped",true},{"scope","Actual loopback broker/handshake/native IPC producer/queue/child/selector. Snapshot business provider, Steam/account/mode/time/Runtime ownership are explicit ports; not installed/physical acceptance."}});
   }

   for(unsigned modeVariant=0;modeVariant<2;++modeVariant){
    const bool cancelBeforeSettled=modeVariant==1;
    const auto beforeDoc="mode-joint-before-"+std::to_string(modeVariant),afterDoc="mode-joint-after-"+std::to_string(modeVariant);
    live.created+=500000;view["pid"]=live.pid;view["birth"]=std::to_string(live.created);view["modeEpoch"]=beforeDoc+":1";
    cycleDisk=managed::record(live,beforeDoc,nativeHost,build,loader,"ready");
    f::g_steamSettingsObserver.value=live;f::g_steamModeObserver.value=view;f::g_steamModeObserver.afterSnapshot={};f::g_steamModeObserver.actual=nullptr;
    f::g_deckySidebarRuntime.state={};f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=5100+modeVariant;f::tick=1000;f::build=build;
    f::g_deckyUpdateRecovery.userEnable(live);f::afterPrivateReply={};f::afterDroppedConfirm={};f::beforeReply={};f::responseOverride=nullptr;f::ownedInspectReplyOverride=nullptr;f::privateModeOverride=nullptr;f::rawReplyKind=0;f::dropNextConfirmReply=false;
    f::privateResults=Json::array();f::captureManagedGuard=false;f::useActualPrivateChild=true;f::holdActualPrivateChild=false;
    const auto gentleBefore=cycleGentle,dispatchBefore=cycleDispatch;
    f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);f::tick+=30000;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);f::tick+=30000;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);
    need(cycleGentle==gentleBefore+2&&cycleDispatch==dispatchBefore+1,"actual mode joint skipped original recovery reservation");
    view["modeEpoch"]=afterDoc+":1";f::g_steamModeObserver.value=view;const auto full=view;
    f::g_deckySidebarRuntime.state.pid=f::fixtureBackendPid;f::g_deckySidebarRuntime.state.phase="loader-started";
    f::privateMain=baseline;f::privateMain["deckyContextCleanup"]=cycleDisk;f::privateBackup=f::privateMain;
    f::deckySidebarEndManagedModeWait();f::g_deckySidebarRuntime.connectionWakes=0;f::dispatches=f::fileReads=f::backendReads=0;
    Json jointRow={{"kind","actual-mode-broker-native-ready-private-child"},{"modeVariant",modeVariant},{"cancelBeforeSettled",cancelBeforeSettled}};
    const auto gentleAtReady=cycleGentle;DWORD handlesBeforeMode=0;need(GetProcessHandleCount(GetCurrentProcess(),&handlesBeforeMode)!=FALSE,"actual mode joint initial handle count unavailable");
    {
      ymcc::steamui::ModeObserver observer;ymcc::deckymirror::Broker actualBroker;ymcc::deckysidebar::Runtime runtimeOwner;
      HANDLE ownerParked=CreateEventW(nullptr,TRUE,FALSE,nullptr),ownerServiced=CreateEventW(nullptr,FALSE,FALSE,nullptr);
      HANDLE initial=CreateEventW(nullptr,TRUE,FALSE,nullptr),entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr),settled=CreateEventW(nullptr,FALSE,FALSE,nullptr);
      need(initial&&entered&&release&&settled&&ownerParked&&ownerServiced,"actual mode/Broker/Runtime joint events unavailable");
      std::atomic<bool> ownerArmed{false};std::atomic<unsigned> ownerCalls{0},ownerActiveCalls{0},ownerPreflights{0};std::atomic<DWORD> ownerWait{INFINITE};
      struct StopJoint{ymcc::steamui::ModeObserver& mode;ymcc::deckymirror::Broker& broker;ymcc::deckysidebar::Runtime& owner;HANDLE initial,entered,release,settled,parked,serviced;~StopJoint(){SetEvent(release);mode.shutdown();owner.setEnabled(false);f::g_deckySidebarRuntime.ownerWake={};f::g_steamModeObserver.actual=nullptr;f::g_deckyMirrorBroker.actual=nullptr;broker.stop();CloseHandle(initial);CloseHandle(entered);CloseHandle(release);CloseHandle(settled);CloseHandle(parked);CloseHandle(serviced);}}stopJoint{observer,actualBroker,runtimeOwner,initial,entered,release,settled,ownerParked,ownerServiced};
      ymcc::deckysidebar::Config ownerConfig;ownerConfig.executable=L"NEVER-LAUNCH.exe";ownerConfig.home=L"NEVER-WRITE";
      ownerConfig.preflight=[&]{++ownerPreflights;return std::string("test-no-launch");};
      ownerConfig.launchAdmission=[]{return ymcc::deckysidebar::LaunchAdmission{false,"decky-context-already-loaded",false};};
      ownerConfig.connectionRecovery=[&]{++ownerCalls;if(ownerArmed.load(std::memory_order_acquire)){f::actualManagedObserverGuard();++ownerActiveCalls;SetEvent(ownerServiced);}};
      ownerConfig.connectionRecoveryTimeout=[&]{const DWORD wait=f::deckySidebarConnectionRecoveryTimeout();ownerWait=wait;SetEvent(ownerParked);return wait;};
      runtimeOwner.initialize(std::move(ownerConfig));runtimeOwner.observeSteam(1);need(runtimeOwner.setEnabled(true),"actual Runtime scheduler enable failed");
      need(WaitForSingleObject(ownerParked,1500)==WAIT_OBJECT_0&&ownerCalls==1&&ownerWait==INFINITE,"actual Runtime did not park without polling before native ready");
      need(runtimeOwner.snapshot().phase=="waiting-manual-steam"&&runtimeOwner.snapshot().pid==0&&ownerPreflights==0,"scheduler fixture acquired forbidden Loader authority");
      f::g_deckySidebarRuntime.ownerWake=[&]{return runtimeOwner.requestConnectionCheck();};
      std::atomic<unsigned> modeReads{0},stableCallbacks{0},nativeRequests{0};std::atomic<bool> nativeQueued{false};
      ymcc::steamui::ModeObserverConfig modeConfig;modeConfig.debounceMs=1;modeConfig.minimumGapMs=1;modeConfig.failureGapMs=30;
      modeConfig.read=[&](const auto&){const auto n=++modeReads;if(n==2){SetEvent(entered);WaitForSingleObject(release,1500);}return full;};
      modeConfig.changed=[&](const auto&,const Json&){SetEvent(initial);};
      modeConfig.stableBigPicture=[&](const auto& observed){f::actualStableModeCallback(observed);++stableCallbacks;SetEvent(settled);};
      f::g_steamModeObserver.actual=&observer;
      need(observer.initialize(std::move(modeConfig)),"actual mode/Broker joint observer initialize failed");observer.observeSteam(live);observer.setDemand(ymcc::steamui::ModeConsumer::Decky,true);
      need(WaitForSingleObject(initial,1500)==WAIT_OBJECT_0,"actual joint initial verified mode not published");
      need(observer.requestRefresh(live)&&WaitForSingleObject(entered,1500)==WAIT_OBJECT_0,"actual joint pending mode read did not enter");
      ymcc::deckymirror::Config brokerConfig{[&](const std::string& connection,const Json& request){
        const Json snapshot={{"revision",1},{"runId",request.at("runId")},{"ready",true},{"provenance",{{"kind","ymcc-native"},{"pid",GetCurrentProcessId()}}}};
        nativeQueued=f::actualNativeMirrorReply({{"connection",connection},{"message",{{"type","snapshot"},{"runId",request.at("runId")},{"snapshot",snapshot}}}}).get<bool>();++nativeRequests;
      },{}};
      need(actualBroker.start(brokerConfig),"actual mode joint broker start failed");f::g_deckyMirrorBroker.actual=&actualBroker;
      const auto boot=actualBroker.bootstrap();actualBrokerClient::LoopbackSocket socket(boot);socket.sendFrame(Json{{"command","snapshot"},{"runId",boot.at("runId")}}.dump());const auto wire=Json::parse(socket.readFrame());
      need(actualBrokerClient::waitUntil([&]{return nativeRequests.load()==1;},1000)&&nativeQueued&&wire.at("snapshot").value("ready",false),"actual native producer did not finish its authenticated pending-mode reply");
      need(f::g_deckyManagedModePending.load()&&f::g_deckyManagedReady.initialProof&&!f::g_deckyManagedReadbackActive&&f::g_deckySidebarRuntime.connectionWakes==0&&f::privateResults.empty()&&f::backendReads==0&&f::fileReads==0,"actual pending shared reader lost first native-ready or performed private IO");
      f::actualManagedObserverGuard();need(f::privateResults.empty()&&f::g_deckySidebarRuntime.connectionWakes==0,"pending actual shared cache bypassed private admission");
      if(cancelBeforeSettled){f::g_deckyUpdateRecovery.veto();f::deckySidebarEndManagedModeWait();}
      ownerArmed.store(true,std::memory_order_release);SetEvent(release);need(WaitForSingleObject(settled,1500)==WAIT_OBJECT_0&&stableCallbacks==1,"actual joint stable callback did not finish after gated read");
      if(!cancelBeforeSettled){
        need(f::g_deckySidebarRuntime.connectionWakes==1&&!f::g_deckyManagedModePending.load()&&!f::g_deckyManagedReady.initialProof,"actual stable callback lost first-ready wake before private confirmation");
        need(WaitForSingleObject(ownerServiced,1500)==WAIT_OBJECT_0&&ownerActiveCalls==1,"actual Runtime wake lost ready confirmation before private IO");need(f::privateResults.size()==1&&f::privateResults.front().at("task")=="confirm"&&f::privateResults.front().at("result").value("ok",false)&&f::privateResults.front().at("result").value("fixtureSettingsWrites",0u)==2&&f::g_deckyManagedReady.finished,"actual shared reader did not reach one real private native confirmation");
        const auto& committed=f::privateResults.front().at("result");const auto committedMain=committed.at("fixtureSavedMain"),committedBackup=committed.at("fixtureSavedBackup");
        need(committedMain==committedBackup&&committedMain.at("businessOpaque")=="preserve"&&committedMain.at("deckyContextCleanup").at("status")=="ready"&&committedMain.at("deckyContextCleanup").at("document")==afterDoc,"actual shared proof did not establish matching native ready records");
        // Persisted settings are an explicit fixture port. Advance its parent
        // objects only from the actual private child's committed receipt.
        f::privateMain=committedMain;f::privateBackup=committedBackup;
        for(unsigned again=0;again<2;++again){need(observer.requestRefresh(live)&&WaitForSingleObject(settled,1500)==WAIT_OBJECT_0,"actual joint stable repeat did not settle");}
        need(ownerActiveCalls==1,"repeated settled callbacks woke the real Runtime again");
        need(f::g_deckySidebarRuntime.connectionWakes==1&&f::privateResults.size()==1,"unchanged actual shared callbacks repeated native confirmation");
      }else{
        need(ownerActiveCalls==0&&f::g_deckySidebarRuntime.connectionWakes==0&&f::privateResults.empty()&&f::g_deckyManagedReady.finished&&!f::g_deckyManagedModePending.load()&&f::privateMain.at("deckyContextCleanup").at("status")=="reserved","late actual shared callback revived cancelled native-ready authority");
      }
      jointRow["modeReads"]=modeReads.load();jointRow["stableCallbacks"]=stableCallbacks.load();jointRow["nativeRequests"]=nativeRequests.load();jointRow["connectionWakes"]=f::g_deckySidebarRuntime.connectionWakes.load();jointRow["nativeReplies"]=f::privateResults;
      need(ownerPreflights==0&&runtimeOwner.snapshot().pid==0,"scheduler joint launched a Loader");
      jointRow["runtimeOwnerCalls"]=ownerCalls.load();jointRow["runtimeActiveCalls"]=ownerActiveCalls.load();jointRow["runtimeWait"]=ownerWait.load();jointRow["runtimeNoLoaderLaunch"]=true;
      jointRow["sourceCallbackAndPrivateChildActual"]=true;jointRow["noSecondNativeReadyReceipt"]=true;jointRow["scope"]="Actual local Broker/native lambda/shared ModeObserver worker/cache/stable callback, production Runtime worker/wait/wake and source recovery timeout callback, then existing private child. Shared read provider, Steam session, backend Runtime ownership/state, config objects/time remain explicit ports; scheduler Runtime has explicitly refused launch authority. Not complete Runtime/backend integration or installed/physical acceptance.";
      socket.close();
    }
    DWORD handlesAfterMode=0;need(GetProcessHandleCount(GetCurrentProcess(),&handlesAfterMode)!=FALSE,"actual mode joint final handle count unavailable");
    jointRow["parentHandlesBefore"]=handlesBeforeMode;jointRow["parentHandlesAfter"]=handlesAfterMode;
    need(handlesAfterMode==handlesBeforeMode,"actual shared observer/Broker joint retained parent handles after stop");
    f::g_deckySidebarRuntime.state={};cycleDisk=f::privateMain.at("deckyContextCleanup");f::g_deckyUpdateRecovery.userEnable(live);f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);
    need(cycleGentle==gentleAtReady+(cancelBeforeSettled?0u:1u),"actual mode joint completed or refunded lifecycle without the correct private result");
    jointRow["nextGentleOnlyAfterNativeConfirmation"]=true;jointRow["modeAndBrokerStopped"]=true;workerResults.push_back(jointRow);
   }
   Json lostReplyControls=Json::array();
   for(const unsigned variant:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u,18u,19u,20u,21u,22u,23u,24u,25u,26u,27u,28u,29u,30u,31u,32u,33u,34u,35u,36u,37u,38u,39u,40u,41u,42u,43u,44u,45u,46u,47u,48u,49u,50u,51u,52u,53u,54u,55u,56u,57u,58u,59u,60u,61u,62u,63u,64u,65u,66u,67u,68u,69u,70u,71u,72u,73u,74u,75u,76u,77u,78u,79u,80u,81u,82u,83u,84u,85u,86u,87u,17u}){live.created+=1000+variant;live.account=9;view["pid"]=live.pid;view["birth"]=std::to_string(live.created);const auto origin="lost-control-"+std::to_string(variant);view["modeEpoch"]=origin+":1";
    cycleDisk=managed::record(live,origin,nativeHost,build,loader,"ready");f::g_steamSettingsObserver.value=live;f::g_steamModeObserver.value=view;f::g_deckySidebarRuntime.state={};f::g_deckyUpdateRecovery.userEnable(live);f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=2300+variant;f::tick=1000;f::build=build;
    f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};f::privateResults=Json::array();f::afterDroppedConfirm={};f::afterPrivateReply={};f::lostConfirmReplyOverride=nullptr;f::ownedInspectReplyOverride=nullptr;f::ownedInspectBirthOverride.clear();f::rawReplyKind=0;f::captureManagedGuard=variant>=44;f::managedGuardDiagnostics=Json::array();f::g_steamModeObserver.afterSnapshot={};f::postSnapshotRaceReads=0;f::postSnapshotRaceRan=false;
    const auto beforeGentle=cycleGentle,beforeDispatch=cycleDispatch;
    f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);f::tick+=30000;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);f::tick+=30000;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);
    need(cycleGentle==beforeGentle+2&&cycleDispatch==beforeDispatch+1,"lost-reply control did not begin with original graces and one reservation");
    view["modeEpoch"]=origin+"-ready:1";f::g_steamModeObserver.value=view;f::g_deckySidebarRuntime.state.pid=f::fixtureBackendPid;f::g_deckySidebarRuntime.state.phase="loader-started";
    f::privateMain=baseline;f::privateMain["deckyContextCleanup"]=cycleDisk;f::privateBackup=f::privateMain;f::dropNextConfirmReply=true;
    f::afterDroppedConfirm=[&,variant]{auto& left=f::privateMain["deckyContextCleanup"];auto& right=f::privateBackup["deckyContextCleanup"];
     if(variant==0)right["status"]="reserved";if(variant==1){left["status"]="reserved";right=left;}if(variant==2){left["host"]="999:12345";right=left;}if(variant==3){left=nullptr;right=nullptr;}
     if(variant==4){left["document"]="foreign-readback-document";right=left;}if(variant==5){left["frontendBuildId"]=std::string(64,'b');right=left;}
     if(variant==6)f::g_deckyAutomaticRepair.endEpisode();if(variant==7)f::g_deckyMirrorBroker.value["connection"]="2";
     if(variant==8)f::ownedInspectReplyOverride={{"ok",false},{"mutated",true},{"uncertain",true},{"reason","decky-cleanup-worker-read-failed"},{"cleanupWorkerReplyReceived",false},{"cleanupWorkerReadKind","ended"},{"cleanupWorkerReadError",static_cast<DWORD>(ERROR_BROKEN_PIPE)}};
     if(variant==9)f::ownedInspectReplyOverride={{"ok",true},{"mutated",true},{"ledger",left}};
     if(variant==10)f::lostConfirmReplyOverride={{"cleanupWorkerDestroyed",false}};
     if(variant==11){f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");}
     if(variant==12)f::privateBackup["businessOpaque"]="older-backup-is-preserved";
     if(variant==13)f::ownedInspectBirthOverride="1";
     if(variant==14)f::ownedInspectReplyOverride={{"ok",true},{"mutated",false},{"uncertain",true},{"ledger",left}};
     if(variant==18||variant==20){f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");}
     if(variant==20)f::g_steamSettingsObserver.value.account=0;
     if(variant==19)f::ownedInspectReplyOverride={{"ok",false},{"mutated",true},{"uncertain",true},{"reason","decky-cleanup-worker-read-failed"},{"cleanupWorkerReplyReceived",false},{"cleanupWorkerReadKind","ended"},{"cleanupWorkerReadError",static_cast<DWORD>(ERROR_BROKEN_PIPE)}};
     if(variant==21)f::g_steamModeObserver.value["modeEpoch"]="foreign-readback-document:1";
     if(variant==22)f::build=std::string(64,'b');
     if(variant==23)f::ownedInspectReplyOverride={{"ok",false},{"mutated",true},{"uncertain",true},{"reason","decky-cleanup-worker-read-failed"},{"cleanupWorkerReplyReceived",false},{"protocolError",101}};
     if(variant==24)f::ownedInspectReplyOverride={{"ok",false},{"mutated",true},{"uncertain",true},{"reason","decky-cleanup-worker-read-failed"}};
     if(variant>=26&&variant<=33)f::rawReplyKind=variant-25;
     if(variant>=35&&variant<=59)f::rawReplyKind=variant-26;
     if(variant==34)f::ownedInspectReplyOverride={{"ok",false},{"mutated",true},{"uncertain",true},{"reason","decky-cleanup-worker-read-failed"},{"cleanupWorkerReplyReceived",false}};
     if(variant==25)f::afterPrivateReply=[&]{if(f::privateResults.size()==1)f::tick+=2000;};
     if(variant>=60)f::afterPrivateReply=[&,variant]{if(f::privateResults.size()==1){
      // First current() follows actual dispatch return. The SECOND one is the
      // service's final completion check. Interleave immediately after that
      // real mode snapshot, not before the initial native authority read.
      f::g_steamModeObserver.afterSnapshot=[&,variant]{++f::postSnapshotRaceReads;
       f::g_steamModeObserver.afterSnapshot=[&,variant]{++f::postSnapshotRaceReads;f::postSnapshotRaceRan=true;
        if(variant==60)++f::power;
        if(variant==61)f::g_exitRequested=true;
        if(variant==62)f::g_deckySidebarRuntime.state.enabled=false;
        if(variant==63){f::g_deckyMirrorBroker.value["connected"]=false;f::g_deckyMirrorBroker.value["connection"]="";}
        if(variant==64){f::g_deckyMirrorBroker.value["connection"]="2";f::deckySidebarQueueManagedReady(live,f::fixtureBackendPid,std::string(32,'c'),"2");}
        if(variant==65)f::g_deckyUpdateRecovery.veto();
        if(variant==66)f::actualStableModeCallback(live); // Positive same-authority callback.
        if(variant==67)f::g_powerLifecycle=f::PowerLifecycle::Suspended;
        if(variant==68)++f::g_deckySidebarRuntime.state.pid;
        if(variant==69)f::g_deckySidebarRuntime.state.phase="frontend-attached";
        if(variant==70)f::g_deckySidebarRuntime.state.steamKnown=false;
        if(variant==71)f::g_deckySidebarRuntime.state.steamRunning=false;
        if(variant==72)f::g_deckyMirrorBroker.value["active"]=false;
        if(variant==73)f::g_deckyMirrorBroker.value["runId"]=std::string(32,'d');
        if(variant==74)f::deckySidebarEndManagedReadback();
        if(variant==75){std::lock_guard lock(f::g_deckyManagedReadyMutex);f::g_deckyManagedReady.finished=false;}
        if(variant==76){f::g_deckyMirrorBroker.value["runId"]=std::string(32,'d');f::g_deckyMirrorBroker.value["connection"]="1";f::deckySidebarQueueManagedReady(live,f::fixtureBackendPid,std::string(32,'d'),"1");}
        if(variant==77){for(unsigned receipt=0;receipt<512;++receipt){const auto conn=std::to_string(receipt+2);f::g_deckyMirrorBroker.value["connection"]=conn;f::deckySidebarQueueManagedReady(live,f::fixtureBackendPid,std::string(32,'c'),conn);}}
        if(variant==78){f::tick=f::g_deckyManagedReadback.opened+30001;f::g_deckyMirrorBroker.value["connection"]="2";f::deckySidebarQueueManagedReady(live,f::fixtureBackendPid,std::string(32,'c'),"2");}
        if(variant==79)f::g_deckyMirrorBroker.value["connection"]="2"; // No new native-ready receipt.
        if(variant==80){auto offline=live;offline.account=0;f::g_steamSettingsObserver.value=offline;f::g_deckyMirrorBroker.value["connection"]="2";f::deckySidebarQueueManagedReady(offline,f::fixtureBackendPid,std::string(32,'c'),"2");}
        if(variant>=81){
         if(variant!=82&&variant!=86){f::g_steamModeObserver.value["known"]=false;f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");}
         if(variant==82||variant==83||variant==86){f::g_steamSettingsObserver.value.account=0;f::g_steamModeObserver.value["deckyAccountWatchAvailable"]=false;f::g_steamModeObserver.value.erase("observedAccount");}
         f::g_deckyMirrorBroker.value["connection"]="2";
         if(variant==83)f::g_steamModeObserver.afterSnapshot=[&]{f::g_steamModeObserver.value=view;}; // Proof settles after queue snapshot, before publication.
         f::deckySidebarQueueManagedReady(f::g_steamSettingsObserver.value,f::fixtureBackendPid,std::string(32,'c'),"2");
        }
       };};
     }};
     if(variant>=15&&variant<=17)f::afterPrivateReply=[&,variant]{if(f::privateResults.size()==1){if(variant==15)f::g_deckyAutomaticRepair.endEpisode();if(variant==16)f::g_deckyUpdateRecovery.veto();if(variant==17){need(TerminateProcess(control.process,0)&&WaitForSingleObject(control.process,2000)==WAIT_OBJECT_0,"inert backend death control failed");}}};
    };
    f::deckySidebarQueueManagedReady(live,f::fixtureBackendPid,std::string(32,'c'),"1");if(variant>=44)f::actualManagedObserverGuard();else f::deckySidebarServiceManagedLifecycle();
    if(variant==11||variant==20){need(f::privateResults.size()==1,"pending proof launched inspection before becoming current");
     const auto opened=f::tick;for(unsigned storm=0;storm<512;++storm){f::actualStableModeCallback(live);f::deckySidebarServiceManagedLifecycle();}
     need(f::privateResults.size()==1,"pending callback storm launched private IO");
     f::afterDroppedConfirm={};f::g_steamModeObserver.value=view;f::g_steamSettingsObserver.value=live;
     const auto wakes=f::g_deckySidebarRuntime.connectionWakes.load();for(unsigned storm=0;storm<512;++storm)f::actualStableModeCallback(live);
     need(f::g_deckySidebarRuntime.connectionWakes==wakes+1,"recovered proof callback did not coalesce its read-only wake");
     f::tick=opened+8000;f::deckySidebarServiceManagedLifecycle();
     need(f::privateResults.size()==2&&f::privateResults.back().at("task")=="inspect","pending lost-confirm proof never resumed read-only inspection after recovery");}
    if(variant==18){f::tick+=30001;f::deckySidebarServiceManagedLifecycle();f::g_steamModeObserver.value=view;f::actualStableModeCallback(live);f::deckySidebarServiceManagedLifecycle();
     need(!f::g_deckyManagedReadbackActive&&f::privateResults.size()==1,"late proof refilled the expired read-only window");}
    const bool inspectExpected=variant<=5||variant==8||variant==9||(variant>=11&&variant<=17)||variant==19||variant==20||variant>=23;
    // Even refusal controls must advance the real borrowed worker slot;
    // otherwise a wrongly queued inspection could escape the assertion.
    if(variant!=11&&variant!=20&&variant!=18){f::tick+=variant==25?29999:1000;if(variant>=44)f::actualManagedObserverGuard();else f::deckySidebarServiceManagedLifecycle();}
    need(f::privateResults.size()==(inspectExpected?2u:1u)&&f::privateResults.front().at("task")=="confirm","lost-reply guard repeated the write or ignored current/worker-return fencing");
    const auto confirmed=f::privateResults.front().at("result");need(confirmed.value("fixtureSettingsWrites",99u)==2,"lost-reply control did not really execute actual child native record write");
    if(inspectExpected&&(variant<26||variant==34)){const auto inspected=f::privateResults.back().at("result");need(f::privateResults.back().at("task")=="inspect"&&inspected.value("fixtureSettingsWrites",99u)==0&&inspected.value("cleanupWorkerHandles",99u)==0&&inspected.value("cleanupWorkerProcesses",99u)==0,"owned readback wrote config or retained private resources");
     need(inspected.at("fixtureSavedMain")==f::privateMain&&inspected.at("fixtureSavedBackup")==f::privateBackup,"readback altered native or business data");
     if(variant==13)need(inspected.value("reason",std::string{})=="decky-managed-backend-changed"&&inspected.value("fixtureSettingsReads",99u)==0,"owned inspection skipped backend birth/path verification before settings access");}
    if(variant>=26&&variant<=33){const auto& trace=f::privateResults.back();const auto inspected=trace.at("result");
     need(trace.value("rawReplyPreBusinessChildEntered",false),"actual malformed response never entered its pre-business child path");
     need(inspected.value("cleanupWorkerDestroyed",false)&&inspected.value("cleanupWorkerHandles",99u)==0&&inspected.value("cleanupWorkerProcesses",99u)==0,"raw framing fault retained owned worker resources");
     need(!inspected.value("cleanupWorkerReplyReceived",true)||variant==33,"malformed raw response was marked as a parsed detail");
     if(variant<=29){const DWORD expected=variant<=27?ERROR_BAD_LENGTH:ERROR_HANDLE_EOF;
      need(inspected.value("reason",std::string{})=="decky-cleanup-worker-frame-invalid"&&inspected.value("cleanupWorkerReadError",0u)==expected&&inspected.value("cleanupWorkerReadKind",std::string{})=="failed",
       "actual malformed frame was collapsed into retryable undelivered transport");
      need(!f::g_deckyManagedReadbackActive,"actual malformed frame retained a readonly retry slot");}
     if(variant==30)need(inspected.value("reason",std::string{})=="decky-cleanup-worker-read-failed"&&inspected.value("cleanupWorkerReadError",0u)==ERROR_BROKEN_PIPE&&inspected.value("cleanupWorkerReadKind",std::string{})=="ended"&&f::g_deckyManagedReadbackActive,"clean pre-frame EOF lost its distinct bounded readonly retry");
     if(variant==31)need(inspected.contains("protocolError")&&!f::g_deckyManagedReadbackActive,"actual malformed JSON did not stop readonly continuation");
     if(variant==32)need(inspected.value("reason",std::string{})=="decky-cleanup-worker-reply-invalid"&&!f::g_deckyManagedReadbackActive,"actual invalid envelope did not stop readonly continuation");
     if(variant==33)need(inspected.value("cleanupWorkerReplyReceived",false)&&inspected.value("uncertain",false)&&!f::g_deckyManagedReadbackActive,"actual parsed uncertain detail was treated as an undelivered reply");
     const auto logged=ymcc::deckydiag::record("managed-lifecycle",inspected);
     if(inspected.contains("cleanupWorkerReadError"))need(logged.contains("cleanupWorkerReadError")&&logged.contains("cleanupWorkerReadKind")&&logged.at("cleanupWorkerReadError")==inspected.at("cleanupWorkerReadError")&&logged.at("cleanupWorkerReadKind")==inspected.at("cleanupWorkerReadKind"),"actual framing classification was dropped by diagnostics");
     auto poison=inspected;poison["cleanupWorkerReadKind"]="private-payload";poison["cleanupWorkerReadError"]=-1;poison["payload"]="private-payload";
     const auto filtered=ymcc::deckydiag::record("managed-lifecycle",poison);need(!filtered.contains("cleanupWorkerReadKind")&&!filtered.contains("cleanupWorkerReadError")&&!filtered.contains("payload"),"framing diagnostics admitted untyped or private content");}
    if(variant>=35&&variant<=43){auto& trace=f::privateResults.back();const auto inspected=trace.at("result");
     trace["parentRecoveryAfterRead"]=f::g_deckyAutomaticRepair.snapshot(f::tick);
     need(trace.value("rawReplyAfterActualReadonlyInspect",false),"JSON reply fault did not first execute actual readonly native inspection");
     need(inspected.value("cleanupWorkerDestroyed",false)&&inspected.value("cleanupWorkerHandles",99u)==0&&inspected.value("cleanupWorkerProcesses",99u)==0,"JSON reply fault leaked its owned worker");
     if(variant==41||variant==42){
      need(inspected.value("ok",false)&&inspected.value("cleanupWorkerReplyReceived",false)&&inspected.value("fixtureSettingsReads",0u)>0&&inspected.value("fixtureSettingsWrites",99u)==0,
       "bounded nested or sibling-key readonly response was rejected");
      need(inspected.at("fixtureSavedMain")==f::privateMain&&inspected.at("fixtureSavedBackup")==f::privateBackup,"normal JSON control changed native settings");
     }else need(!inspected.value("cleanupWorkerReplyReceived",true)&&inspected.value("reason",std::string{})=="decky-cleanup-worker-reply-invalid"&&!f::g_deckyManagedReadbackActive,
       "ambiguous cleanup reply was accepted despite duplicate keys or excessive depth");}
    if(variant>=44&&variant<=59){
     const auto first= f::privateResults.back();const bool pending=f::g_deckyManagedReadbackActive.load();const auto timeout=f::deckySidebarConnectionRecoveryTimeout();
     const auto firstParent=f::g_deckyAutomaticRepair.snapshot(f::tick);const auto firstDiagnostics=f::managedGuardDiagnostics;
     f::tick+=2000;f::actualManagedObserverGuard();
     auto& trace=f::privateResults.back();const auto inspected=trace.at("result");
     trace["parentRecoveryAfterRead"]=f::g_deckyAutomaticRepair.snapshot(f::tick);
     trace["typeGuardObservation"]={{"pendingAfterFirst",pending},{"borrowedTimeoutAfterFirst",timeout},{"firstParent",firstParent},{"firstDiagnostics",firstDiagnostics},{"diagnosticsAfterSecondSlot",f::managedGuardDiagnostics},{"privateRepliesAfterSecondSlot",f::privateResults.size()}};
     need(trace.value("rawReplyAfterActualReadonlyInspect",false),"typed reply case did not execute actual native readonly inspection");
     bool caught=false;for(const auto& log:f::managedGuardDiagnostics)if(log.value("reason",std::string{})=="decky-managed-proof-unavailable")caught=true;
     need(!caught&&!pending&&!f::g_deckyManagedReadbackActive&&f::privateResults.size()==2&&timeout==INFINITE,
      "ill-typed cleanup reply escaped actual observer catch and retained or retried readonly work");
     need(inspected.value("cleanupWorkerDestroyed",false)&&inspected.value("cleanupWorkerHandles",99u)==0&&inspected.value("cleanupWorkerProcesses",99u)==0,"typed reply fault leaked owned resources");
     if(variant<=57)need(!inspected.value("cleanupWorkerReplyReceived",true)&&inspected.value("reason",std::string{})=="decky-cleanup-worker-reply-invalid",
      "ill-typed cleanup detail was accepted as a trusted parsed reply");
     else{need(inspected.value("cleanupWorkerReplyReceived",false)&&inspected.value("fixtureSettingsReads",0u)>0&&inspected.value("fixtureSettingsWrites",99u)==0,"valid typed cleanup detail lost readonly completion");
      if(variant==58)need(!inspected.value("ok",true)&&inspected.value("reason",std::string{})=="fixture-legitimate-failure","valid failure receipt was rejected as malformed");
      if(variant==59)need(inspected.value("ok",false)&&trace.at("parentRecoveryAfterRead").value("phase",std::string{})=="idle","optional absent fields blocked a valid successful readonly receipt");}}
    if(variant>=60){auto& trace=f::privateResults.back();const auto inspected=trace.at("result");
     const auto after=f::g_deckyAutomaticRepair.snapshot(f::tick);
     trace["postSnapshotRace"]={{"variant",variant},{"hookRan",f::postSnapshotRaceRan},{"modeReadCountBeforeFinalMutation",f::postSnapshotRaceReads},
      {"parentRecoveryAfterRead",after},{"readyFinished",f::g_deckyManagedReady.finished},{"power",f::power},{"peerConnected",f::g_deckyMirrorBroker.value.value("connected",false)},
      {"peerConnection",f::g_deckyMirrorBroker.value.value("connection",std::string{})},{"exitRequested",f::g_exitRequested.load()},{"vetoed",f::g_deckyUpdateRecovery.vetoed()}};
     need(f::postSnapshotRaceRan&&f::postSnapshotRaceReads==2,"post-snapshot callback did not interleave at final actual readback proof");
     need(inspected.value("ok",false)&&inspected.value("fixtureSettingsReads",0u)>0&&inspected.value("fixtureSettingsWrites",99u)==0&&inspected.value("cleanupWorkerDestroyed",false)&&inspected.value("cleanupWorkerHandles",99u)==0&&inspected.value("cleanupWorkerProcesses",99u)==0,
      "post-snapshot race did not use a genuine returned zero-write native inspection");
     need(after.value("phase",std::string{})==(variant==66?"idle":variant==78?"manual-required":"recovering"),
      "authority changed after mode snapshot but stale readback completed the old lifecycle");}
    if(variant==64||variant==76||variant==77||variant==80){
     const auto originalOpened=f::g_deckyManagedReadback.opened,originalObservation=f::g_deckyManagedReadback.observation;
     f::privateResults.back()["reconnectBeforeNextRead"]={{"active",f::g_deckyManagedReadbackActive.load()},{"timeout",f::deckySidebarConnectionRecoveryTimeout()},
      {"opened",f::g_deckyManagedReadback.opened},{"serial",f::g_deckyManagedReadback.serial},{"observation",f::g_deckyManagedReadback.observation},{"readyConnection",f::g_deckyManagedReady.connection},{"readyFinished",f::g_deckyManagedReady.finished}};
     need(f::g_deckyManagedReadbackActive&&f::deckySidebarConnectionRecoveryTimeout()==2000,
      "new authenticated ready lost bounded readonly observation after old connection was superseded");
     need(originalOpened+1000==f::tick&&f::g_deckyManagedReadback.origin.connection==f::g_deckyManagedReady.connection&&f::g_deckyManagedReady.finished,
      "reconnect moved original opening time or restored write authority");
     if(variant==64){
      // Directly consume the production admission helper with declared memory
      // context ports. Keep each unrelated predicate valid, including ready
      // publication, so foreign-origin checks are not hidden behind mismatch.
      const auto savedItem=f::g_deckyManagedReadback;const auto savedReady=f::g_deckyManagedReady;
      const auto savedSession=f::g_steamSettingsObserver.value;const auto savedRuntime=f::g_deckySidebarRuntime.state;
      const auto savedPeer=f::g_deckyMirrorBroker.value;const auto savedMode=f::g_steamModeObserver.value;const auto savedPower=f::power;
      Json guarded=Json::array();
      for(unsigned guard=0;guard<10;++guard){
       auto next=savedReady;next.finished=false;next.connection="guard-new";auto proof=savedMode;
       f::g_deckyMirrorBroker.value=savedPeer;f::g_deckyMirrorBroker.value["connection"]=next.connection;
       if(guard==0){next.document="guard-foreign-document";proof["modeEpoch"]=next.document+":1";}
       if(guard==1){next.session.account=0;next.account=10;proof["observedAccount"]=10u;}
       if(guard==2){++next.power;f::power=next.power;}
       if(guard==3){++next.loaderPid;f::g_deckySidebarRuntime.state.pid=next.loaderPid;}
       if(guard==4)next.session.root=L"C:\\OtherAuthenticatedSteam";
       if(guard==5)f::g_deckyMirrorBroker.value["connection"]="guard-unmatched";
       if(guard==6)proof["deckyUpdateResidue"]["frontendBuildId"]=std::string(64,'b');
       if(guard==7)next.initialProof=true;
       if(guard==8){next.connection=savedReady.connection;f::g_deckyMirrorBroker.value["connection"]=next.connection;}
       if(guard==9)f::deckySidebarEndManagedReadback(); // Explicit cancellation is not natural expiry.
       f::g_steamSettingsObserver.value=next.session;f::g_deckyManagedReady=next;
       const auto admissionSerial=f::g_deckyManagedReadback.serial;const bool accepted=f::deckySidebarAdoptManagedReadbackReady(next,proof);
       guarded.push_back({{"guard",guard},{"adopted",accepted},{"serialAfter",f::g_deckyManagedReadback.serial},{"openedAfter",f::g_deckyManagedReadback.opened},{"observationAfter",f::g_deckyManagedReadback.observation}});
       f::privateResults.back()["readbackAdoptionGuardControls"]=guarded;
       need(!accepted&&f::g_deckyManagedReadback.serial==admissionSerial&&f::g_deckyManagedReadback.opened==savedItem.opened&&f::g_deckyManagedReadback.observation==savedItem.observation,
        "foreign incomplete duplicate or inactive ready adopted the existing readonly lifecycle");
       f::g_deckyManagedReadback=savedItem;f::g_deckyManagedReadbackActive=true;f::g_deckyManagedReady=savedReady;
       f::g_steamSettingsObserver.value=savedSession;f::g_deckySidebarRuntime.state=savedRuntime;f::g_deckyMirrorBroker.value=savedPeer;f::power=savedPower;
      }
     }
     const auto serialBeforeDuplicate=f::g_deckyManagedReadback.serial;
     for(unsigned repeat=0;repeat<256;++repeat){f::deckySidebarQueueManagedReady(f::g_steamSettingsObserver.value,f::fixtureBackendPid,f::g_deckyMirrorBroker.value.at("runId"),f::g_deckyMirrorBroker.value.at("connection"));f::actualManagedObserverGuard();}
     need(f::privateResults.size()==2&&f::g_deckyManagedReadback.opened==originalOpened&&f::g_deckyManagedReadback.observation==originalObservation&&f::g_deckyManagedReadback.serial==serialBeforeDuplicate&&f::deckySidebarConnectionRecoveryTimeout()==2000,
      "duplicate ready storm refilled or dispatched reconnected readonly observation");
     bool pendingLog=false;for(const auto& log:f::managedGuardDiagnostics)if(log.value("readbackPending",false))pendingLog=true;
     need(pendingLog,"superseded old inspection hid pending new authenticated observation in diagnostics");
     f::afterPrivateReply={};f::g_steamModeObserver.afterSnapshot={};f::tick+=2000;f::actualManagedObserverGuard();
     need(f::privateResults.size()==3&&f::privateResults.front().at("task")=="confirm"&&f::privateResults[1].at("task")=="inspect"&&f::privateResults.back().at("task")=="inspect",
      "same-context reconnect replayed confirmation or never inspected current native history");
     const auto reconciled=f::privateResults.back().at("result");need(reconciled.value("ok",false)&&reconciled.value("fixtureSettingsWrites",99u)==0&&reconciled.value("cleanupWorkerDestroyed",false)&&reconciled.value("cleanupWorkerHandles",99u)==0&&reconciled.value("cleanupWorkerProcesses",99u)==0,
      "reconnected readonly reconciliation mutated settings or retained worker resources");
     need(f::g_deckyAutomaticRepair.snapshot(f::tick).value("phase",std::string{})=="idle"&&!f::g_deckyManagedReadbackActive,
      "authenticated same-context reconnect could not complete original spent lifecycle by readonly proof");}
    if(variant==78){f::afterPrivateReply={};f::g_steamModeObserver.afterSnapshot={};
     f::privateResults.back()["lateReconnectBeforeExpiryCheck"]={{"active",f::g_deckyManagedReadbackActive.load()},{"timeout",f::deckySidebarConnectionRecoveryTimeout()},{"opened",f::g_deckyManagedReadback.opened},{"tick",f::tick}};
     f::actualManagedObserverGuard();need(!f::g_deckyManagedReadbackActive&&f::privateResults.size()==2,
      "late authenticated ready refilled the spent original readonly window");}
    if(variant==79)need(!f::g_deckyManagedReadbackActive&&f::privateResults.size()==2,"peer identity without new native-ready proof adopted old readonly work");
    if(variant>=81){
     const auto opened=f::g_deckyManagedReadback.opened,observation=f::g_deckyManagedReadback.observation;
     f::privateResults.back()["pendingReadyHandoff"]={{"active",f::g_deckyManagedReadbackActive.load()},{"opened",opened},{"observation",observation},
      {"originDocument",f::g_deckyManagedReadback.origin.document},{"originAccount",f::g_deckyManagedReadback.origin.account},{"readyFinished",f::g_deckyManagedReady.finished},
      {"connection",f::g_deckyManagedReadback.origin.connection},{"timeout",f::deckySidebarConnectionRecoveryTimeout()},{"awaitingMode",f::g_deckyManagedReadback.awaitingMode},{"restoredBeforePublication",variant==83}};
     need(f::g_deckyManagedReadbackActive&&f::g_deckyManagedReadback.origin.connection=="2"&&f::g_deckyManagedReadback.origin.document==origin+"-ready"&&f::g_deckyManagedReadback.origin.account==9&&f::g_deckyManagedReady.finished,
      "pending authenticated ready discarded original readonly identity before shared proof settled");
     if(variant==83)need(!f::g_deckyManagedReadback.awaitingMode,"proof settled before pending handoff registration but publish-recheck lost its wake");
     f::afterPrivateReply={};f::g_steamModeObserver.afterSnapshot={};
     if(variant==87){for(unsigned repeat=0;repeat<512;++repeat){f::deckySidebarQueueManagedReady(f::g_steamSettingsObserver.value,f::fixtureBackendPid,std::string(32,'c'),"2");f::actualStableModeCallback(live);f::actualManagedObserverGuard();}
      need(f::privateResults.size()==2&&f::g_deckyManagedReadback.opened==opened&&f::g_deckyManagedReadback.observation==observation,
       "pending new-ready callback storm dispatched IO or refilled readonly observation");}
     f::g_steamModeObserver.value=view;
     if(variant==85)f::g_steamModeObserver.value["modeEpoch"]="foreign-document-after-pending:1";
     if(variant==86)f::g_steamModeObserver.value["observedAccount"]=10u;
     f::tick=opened+(variant==84?30001:8000);const auto wakesBeforeProof=f::g_deckySidebarRuntime.connectionWakes.load();
     for(unsigned callback=0;callback<256;++callback)f::actualStableModeCallback(f::g_steamSettingsObserver.value);
     const auto wakesAfterProof=f::g_deckySidebarRuntime.connectionWakes.load();
     if(variant==81||variant==82||variant==87)need(wakesAfterProof==wakesBeforeProof+1,"pending handoff positive proof did not coalesce one existing Runtime wake");
     if(variant==83||variant==85||variant==86)need(wakesAfterProof==wakesBeforeProof,"settled or foreign handoff proof emitted repeated Runtime wakes");
     f::privateResults.back()["pendingProofWake"]={{"callbacks",256},{"wakesBefore",wakesBeforeProof},{"wakesAfter",wakesAfterProof},{"opened",opened},{"observation",observation},{"at",f::tick}};
     f::actualManagedObserverGuard();
     if(variant==84||variant==85||variant==86)need(f::privateResults.size()==2&&!f::g_deckyManagedReadbackActive,
      "expired or foreign proof reopened readonly handoff or replayed confirmation");
     else{need(f::privateResults.size()==3&&f::privateResults.back().at("task")=="inspect"&&f::privateResults.back().at("result").value("fixtureSettingsWrites",99u)==0&&f::privateResults.back().at("result").value("cleanupWorkerDestroyed",false)&&f::privateResults.back().at("result").value("cleanupWorkerHandles",99u)==0&&f::privateResults.back().at("result").value("cleanupWorkerProcesses",99u)==0,
       "same-context pending new-ready proof never resumed original readonly observation");
      need(f::g_deckyAutomaticRepair.snapshot(f::tick).value("phase",std::string{})=="idle"&&!f::g_deckyManagedReadbackActive,
       "late positive proof failed to complete pending readonly handoff");}
    }
    if(variant==34)need(!f::g_deckyManagedReadbackActive,"unclassified undelivered failure retained a readonly retry without typed clean EOF");
    if(variant==9||variant==14||variant==23||variant==24)need(!f::g_deckyManagedReadbackActive,"parsed mutating uncertain or malformed inspector reply retained read authority");
    if(variant==30){f::rawReplyKind=0;f::tick+=2000;f::deckySidebarServiceManagedLifecycle();
     need(f::privateResults.size()==3&&f::privateResults.back().at("task")=="inspect"&&f::privateResults.back().at("result").value("fixtureSettingsWrites",99u)==0,"actual clean pre-frame EOF could not recover by safe readonly inspection");}
    if(variant==8){f::ownedInspectReplyOverride=nullptr;f::tick+=2000;f::deckySidebarServiceManagedLifecycle();
     need(f::privateResults.size()==3&&f::privateResults.back().at("task")=="inspect"&&f::privateResults.back().at("result").value("fixtureSettingsWrites",99u)==0,"lost readonly delivery did not retry a readonly inspection within its original window");}
    if(variant==19){const auto opened=f::g_deckyManagedReadback.opened;
     for(const unsigned offset:{3000u,8000u,20000u,30000u}){f::tick=opened+offset;f::deckySidebarServiceManagedLifecycle();}
     need(f::privateResults.size()==6&&!f::g_deckyManagedReadbackActive,"repeated readonly loss exceeded original five bounded read slots");
     for(const auto& row:f::privateResults)need(row.at("task")=="confirm"||row.at("result").value("fixtureSettingsWrites",99u)==0,"readonly retry repeated a write");
     f::ownedInspectReplyOverride=nullptr;f::tick=opened+31000;for(unsigned storm=0;storm<256;++storm){f::actualStableModeCallback(live);f::deckySidebarServiceManagedLifecycle();}
     need(f::privateResults.size()==6,"callback storm replenished spent readonly observation budget");}
    // Restore proof after explicit closure/pending. Only known undelivered
    // readonly transport can retry; an uncertain write never repeats.
    f::afterDroppedConfirm={};f::afterPrivateReply={};f::lostConfirmReplyOverride=nullptr;f::ownedInspectReplyOverride=nullptr;f::ownedInspectBirthOverride.clear();f::rawReplyKind=0;f::g_steamModeObserver.afterSnapshot={};f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::g_steamModeObserver.value=view;f::g_steamSettingsObserver.value=live;f::build=build;f::g_deckyMirrorBroker.value["active"]=true;f::g_deckyMirrorBroker.value["runId"]=std::string(32,'c');f::g_deckyMirrorBroker.value["connected"]=true;f::g_deckyMirrorBroker.value["connection"]="1";f::g_deckyUpdateRecovery.userEnable(live);f::g_deckySidebarRuntime.state={};cycleDisk=f::privateMain.at("deckyContextCleanup");
    f::tick+=30000;f::g_deckyAutomaticRepair.observe(sameHostRepair,cycle);
    need(cycleGentle==((variant==8||variant==11||variant==12||variant==20||variant==25||variant==30||variant==41||variant==42||variant==59||variant==64||variant==66||variant==76||variant==77||variant==80||variant==81||variant==82||variant==83||variant==87)?beforeGentle+3:beforeGentle+2),"untrusted or cancelled readback unlocked the next destructive lifecycle");
    f::g_deckySidebarRuntime.state.pid=f::fixtureBackendPid;f::g_deckySidebarRuntime.state.phase="loader-started";
    const auto rows=f::privateResults.size();for(unsigned repeat=0;repeat<32;++repeat){++f::tick;f::deckySidebarServiceManagedLifecycle();}
    need(f::privateResults.size()==rows,"lost readback outcome replayed a private write or inspection");
    lostReplyControls.push_back({{"variant",variant},{"inspectExpected",inspectExpected},{"businessBackupOnlyDifferencePositive",variant==12},{"nextGentleAllowed",variant==8||variant==11||variant==12||variant==20||variant==25||variant==30||variant==41||variant==42||variant==59||variant==64||variant==66||variant==76||variant==77||variant==80||variant==81||variant==82||variant==83||variant==87},{"privateReplies",f::privateResults}});
   }
   workerResults.push_back({{"kind","lost-confirm-readback-controls"},{"rows",lostReplyControls},{"scope","Actual private child write+read-only inspect; memory disk corruption/lost replies/native context ports, not real Steam/disk faults. Pending-parent proof and one lost readonly delivery recover within a fixed original window; explicit uncertain/mutating outcomes stay unresolved without mutation replay."}});
   need(control.stop(),"never-resumed backend control was not terminated by its test Job");
   workerResults.push_back({{"kind","same-host-three-cycles"},{"lostConfirmReplies",1},{"readOnlyReadbacks",1},{"backendControlStopped",true},{"backendControlEverResumed",false},{"backendControlOwnedHandles",0},{"gentleRequests",threeCycleGentle},{"cleanupDispatches",threeCycleDispatch},{"durableReservations",threeCycleReserve},{"actualPrivateReplies",threeCycleReplies},{"finalLedger",threeCycleLedger},{"scope","Actual orchestrators/service/private child/writer; modeled UI rebuild/reservation/files/account/backend proof"}});
   f::useActualPrivateChild=false;f::holdActualPrivateChild=false;f::captureManagedGuard=false;
  }
  // Join the ACTUAL service -> private protocol/Job/child -> ACTUAL child gate
  // -> ACTUAL record selector -> native continuation. Steam/mode/files remain
  // explicit fixture ports; this is not an installed host/Steam restart claim.
  namespace f=lifecycleServiceFixture;auto own=session;own.pid=GetCurrentProcessId();own.created=cleanupworker::processBirth(GetCurrentProcess());
  need(own.created!=0,"joint fixture could not bind its actual parent process birth");
  f::useActualPrivateChild=true;f::privateResults=Json::array();
  DWORD parentBefore=0,parentAfter=0,afterFirstCreation=0;GetProcessHandleCount(GetCurrentProcess(),&parentBefore);
  for(unsigned variant=0;variant<6;++variant){
   const auto doc="joint-managed-"+std::to_string(variant);auto actualMode=initialMode;
   actualMode["pid"]=own.pid;actualMode["birth"]=std::to_string(own.created);actualMode["modeEpoch"]=doc+":1";actualMode["observedAccount"]=own.account;actualMode["deckyAccountWatchAvailable"]=true;actualMode["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
   auto ready=managed::record(own,doc,oldHost,build,ymcc::deckyidentity::version,"ready");
   f::privateMain={{"schemaVersion",1},{"deckyContextCleanup",ready},{"businessOpaque","preserve"}};f::privateBackup=f::privateMain;
   if(variant==1)f::privateBackup["deckyContextCleanup"]["document"]="other-durable-document";
   if(variant==2){f::privateMain["deckyContextCleanup"]["status"]="reserved";f::privateBackup=f::privateMain;}
   f::g_steamSettingsObserver.value=own;f::g_steamModeObserver.value=actualMode;f::g_deckySidebarRuntime.state={};f::g_deckyUpdateRecovery.userEnable({});
   f::power=400+variant;f::tick=1000;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::g_exitRequested=false;f::build=build;f::beforeReply={};f::responseOverride=nullptr;f::dispatches=f::fileReads=0;f::holdActualPrivateChild=variant>=3;
   const auto handlesBeforeRow=jointresource::handleTable();DWORD handleCountBeforeRow=0;GetProcessHandleCount(GetCurrentProcess(),&handleCountBeforeRow);
   std::atomic<bool> entered{false};HANDLE event=nullptr;std::thread closer;
   if(variant>=3){const auto name=L"Local\\YMCC-Managed-Joint-"+std::to_wstring(own.pid);event=CreateEventW(nullptr,TRUE,FALSE,name.c_str());need(event!=nullptr,"joint cancellation event unavailable");closer=std::thread([&]{entered=WaitForSingleObject(event,2000)==WAIT_OBJECT_0;f::g_exitRequested=true;});}
   const auto begin=GetTickCount64();f::deckySidebarServiceManagedLifecycle();const auto elapsed=GetTickCount64()-begin;
   if(closer.joinable())closer.join();if(event)CloseHandle(event);
   need(f::dispatches==1&&f::privateResults.size()==variant+1,"actual native service did not perform exactly one private child dispatch");
   const auto& result=f::privateResults.back().at("result");
   need(result.value("cleanupWorkerDestroyed",false)&&result.value("cleanupWorkerHandles",99u)==0&&result.value("cleanupWorkerProcesses",99u)==0,"joint continuation leaked its actual private child handles or Job subtree");
   if(variant<3)need(result.value("fixtureWorkerPid",0u)!=0&&result.value("fixtureWorkerPid",0u)!=own.pid&&ymcc::deckyupdate::validTicket(std::to_string(result.value("fixtureWorkerPid",0u))+":"+result.value("fixtureWorkerBirth",std::string{})),"joint continuation did not return an actual different private process identity");
   if(variant==0)need(f::g_deckyUpdateRecovery.allows(own)&&result.value("ok",false)&&result.value("fixtureSettingsWrites",99u)==0&&result.value("fixtureSettingsReads",0u)>=3&&result.value("fixtureNativeHost",std::string{})==cleanupworker::nativeHost(),"actual child selector failed healthy same-Steam native continuation");
   else{need(!result.value("ok",true),"actual private child accepted conflicting/pending/cancelled continuation proof");
    need(!f::g_deckyUpdateRecovery.allows(own),"conflicting/pending/cancelled actual private receipt granted continuation");}
   if(variant==1)need(result.value("reason",std::string{})=="decky-managed-history-unconfirmed","actual child did not reject conflicting durable copies");
   if(variant==2)need(result.value("reason",std::string{})=="decky-managed-continuation-unconfirmed","actual child treated pending reservation as completed");
   if(variant>=3)need(entered&&!f::currentAfterReply&&elapsed<3200&&result.value("uncertain",false),"exit during real private confirmation lost cancellation/uncertainty or exceeded original drain boundary");
   const auto handlesAfterRow=jointresource::handleTable();DWORD handleCountAfterRow=0;GetProcessHandleCount(GetCurrentProcess(),&handleCountAfterRow);
   if(!variant)afterFirstCreation=handleCountAfterRow;
   else need(handleCountBeforeRow==afterFirstCreation&&handleCountAfterRow==afterFirstCreation,"repeated private continuation grew parent handles after the explicitly recorded cold first creation");
   f::privateResults.back()["parentHandlesBefore"]=handleCountBeforeRow;f::privateResults.back()["parentHandlesAfter"]=handleCountAfterRow;
   f::privateResults.back()["parentTableBefore"]=handlesBeforeRow;f::privateResults.back()["parentTableAfter"]=handlesAfterRow;
   f::privateResults.back()["elapsedMs"]=elapsed;f::privateResults.back()["variant"]=variant;f::privateResults.back()["continuationAdmitted"]=f::g_deckyUpdateRecovery.allows(own);
   f::g_exitRequested=false;
  }
  // A fresh ordinary grandchild performs no private-channel/Owner/Decky
  // initialization before its own cold Win32 control snapshots. It and all
  // never-resumed children inherit the existing exact private worker Job.
  const auto controlArgs=Json{{"task","inspect"},{"modeEpoch","joint-control:1"},{"frontendBuildId",build},{"loaderVersion",ymcc::deckyidentity::version},{"backendPid",0u},{"backendBirth","0"}};
  auto controlState=initial;controlState["fixturePlainProcessControl"]=true;
  const auto calibration=cleanupworker::dispatch(own,false,controlArgs,controlState,[]{return true;},cleanupworker::recoveryWindowMs,cleanupworker::lifecycleSchema);
  need(calibration.value("ok",false)&&calibration.value("cleanupWorkerDestroyed",false)&&calibration.value("cleanupWorkerHandles",99u)==0&&calibration.value("cleanupWorkerProcesses",99u)==0&&calibration.at("plainWin32Control").at("rows").size()==3,"plain Win32 control did not finish inside the original private worker subtree");
  GetProcessHandleCount(GetCurrentProcess(),&parentAfter);
  need(parentAfter==afterFirstCreation,"plain Win32 control dispatch left additional parent handles after the original first-creation observation");
  workerResults.push_back({{"kind","plain-win32-process-creation-control"},{"result",calibration},{"scope","Fresh ordinary child plain Win32 control; no resource waiver or whole-parent natural-zero claim"}});
  workerResults.push_back({{"kind","joint-private-continuation"},{"rows",f::privateResults},{"parentHandlesBefore",parentBefore},{"parentHandlesAfter",parentAfter},{"parentHandleScope","Observed only; exact child ownership asserted, no new whole-parent natural-zero claim"}});
  f::useActualPrivateChild=f::holdActualPrivateChild=false;f::g_exitRequested=false;
 });
 check("native managed continuation rejects OFF legacy uncertainty stale documents and mismatched durable copies",[&]{
  using namespace ymcc::deckyupdate;
  auto observed=initialMode;observed["modeEpoch"]="managed-proof:1";
  const auto ready=managed::record(session,"managed-proof","501:10001",build,"v3.3.0","ready");
  const Json legacy={{"schemaVersion",1},{"steamSession",ticket(session)}};
  need(managed::completed(legacy,session,"managed-proof","502:10002",build,"v3.3.0").is_null(),"legacy spent record was migrated without completion proof");
  need(!cleanupAvailable(legacy,session,"managed-proof"),"legacy cleanup slot refunded");
  auto different=session;++different.created;
  need(managed::valid(managed::completed(legacy,different,"managed-new-steam","502:10002",build,"v3.3.0")),"positively different Steam lifecycle could not establish a native healthy baseline");
  for(unsigned n=0;n<9;++n){Recovery r;auto view=observed,left=ready,right=ready;std::string owner="502:10002";
   if(n==0)r.veto();if(n==1)left["status"]="reserved";if(n==2)right["document"]="other";if(n==3)view["modeEpoch"]="other:1";
   if(n==4)view["deckyUpdateResidue"]["loaderVersion"]="v3.3.1";if(n==5)left=legacy;if(n==6)owner="501:10001";if(n==7)left=nullptr;if(n==8)view["mode"]="desktop";
   need(!r.continueManaged(session,view,left,right,owner,build,"v3.3.0"),"invalid or stale native lifecycle authority admitted");}
  auto entry=observed;entry["modeEpoch"]="managed-proof:99";
  const auto reserved=managed::record(session,"managed-proof","502:10002",build,"v3.3.0","reserved");
  need(!cleanupAvailable(reserved,session,managed::document(entry.at("modeEpoch"))),"mode entry refilled a spent document");
  auto account=session;account.account=10;need(!cleanupAvailable(reserved,account,"managed-proof"),"account switch refilled a spent document");
 });
 check("explicit manual dispatch can establish a new owned-ready baseline after legacy same-Steam history",[&]{
  using namespace ymcc::deckyupdate;
  auto runtime=initial;auto view=initialMode;view["modeEpoch"]="manual-legacy-before:1";view["deckyAccountWatchAvailable"]=true;view["observedAccount"]=9u;
  auto current=session;unsigned long long generation=51,dispatchTick=1000;unsigned callbacks=0;bool stop=false,doAdmit=true;Json receipt={{"ok",true},{"mutated",true},{"requestDispatched",true}};
  const Json request={{"userInitiated",true},{"pid",session.pid},{"birth",std::to_string(session.created)},{"revision",runtime.at("revision")}};
  Dependencies d;d.session=[&]{return current;};d.runtime=[&]{return runtime;};d.freshMode=[&](const auto&){return view;};d.mode=d.freshMode;d.installed=[&]{return build;};d.loader="v3.3.0";d.preflight=[]{return std::string{};};d.exiting=[&]{return stop;};d.now=[&]{return dispatchTick;};
  d.consent=[](const auto&){return true;};d.powerGeneration=[&]{return generation;};
  d.manualGeneration=[]{return g_deckyManualBaseline.epoch();};
  d.manualDispatched=[&](const auto& selected,const Json& fresh,unsigned long long power,unsigned long long manualEpoch){++callbacks;actualMainManualReceiptCallback(selected,fresh,power,manualEpoch);};
  d.dispatch=[&](const auto&,const Json&,const auto& admit){if(doAdmit&&!admit())return Json{{"ok",false},{"mutated",false}};return receipt;};
  const Json legacy={{"schemaVersion",1},{"steamSession",ticket(session)}};
  g_deckyManualBaseline.clear();ManualRepair manual;const auto result=manual.request(request,d);
  need(result.value("ok",false)&&callbacks==1,"actual acknowledged explicit repair did not capture its fresh native origin");
  need(managed::completed(legacy,session,"manual-legacy-after","502:10002",build,"v3.3.0").is_null(),"legacy history changed before explicit origin and actual owned-ready proof");
  const auto original=g_deckyManualBaseline.origin(session,"manual-legacy-after",generation);
  need(original=="manual-legacy-before","manual dispatch lost its actual starting document");
  need(g_deckyManualBaseline.origin(session,"manual-legacy-before",generation).empty(),"same document or mode entry fabricated repair completion");
  auto other=session;++other.created;need(g_deckyManualBaseline.origin(other,"manual-legacy-after",generation).empty(),"foreign Steam inherited manual origin");
  other=session;++other.account;need(g_deckyManualBaseline.origin(other,"manual-legacy-after",generation).empty(),"different account inherited manual origin");
  need(g_deckyManualBaseline.origin(session,"manual-legacy-after",generation+1).empty(),"completed sleep generation inherited manual origin");
  const auto completed=managed::completed(legacy,session,"manual-legacy-after","502:10002",build,"v3.3.0",original);
  need(managed::valid(completed)&&completed.at("status")=="ready","actual new explicit-repair lifecycle could not establish legacy baseline");
  const auto pending=managed::record(session,"manual-legacy-before","503:10003",build,"v3.3.0","reserved");
  need(managed::completed(pending,session,"manual-legacy-after","502:10002",build,"v3.3.0",original).is_null(),"manual legacy migration refunded a foreign pending v2 cleanup");
  const auto config=Json{{"schemaVersion",1},{"deckyContextCleanup",legacy},{"opaqueUserSetting","preserve"}};
  fixtureSettings[YM_SETTINGS_FILE]=config.dump();fixtureSettings[YM_SETTINGS_BACKUP_FILE]=config.dump();fixtureWrites=fixtureFailWrite=fixtureReads=0;
  cleanupworker::Request native;native.lifecycle=true;native.identity.steam=session;native.state={{"cleanupNativeHost","502:10002"},{"manualRecoveryDocument",original}};
  native.args={{"task","confirm"},{"modeEpoch","manual-legacy-after:1"},{"frontendBuildId",build},{"loaderVersion","v3.3.0"}};
  const auto committed=deckySidebarManagedLifecycleRecord(native,[]{return true;});
  need(committed.value("ok",false)&&fixtureWrites==2,"actual native writer did not commit new manual legacy baseline to both copies");
  auto saved=ymSettingsReadUnlocked();need(saved.at("opaqueUserSetting")=="preserve"&&saved.at("deckyContextCleanup")==completed,"manual baseline changed business settings or lost owned-ready identity");
  auto freshView=view;freshView["modeEpoch"]="manual-legacy-after:1";Recovery next;
  need(next.continueManaged(session,freshView,completed,completed,"504:10004",build,"v3.3.0"),"next ordinary host could not continue the newly completed explicit manual lifecycle");
  g_deckyManualBaseline.clear();need(g_deckyManualBaseline.origin(session,"manual-legacy-after",generation).empty(),"OFF retained manual legacy provenance");
  for(unsigned n=0;n<7;++n){receipt={{"ok",true},{"mutated",true},{"requestDispatched",true}};doAdmit=true;callbacks=0;g_deckyManualBaseline.clear();stop=false;generation=60;
   if(n==0)receipt["ok"]=false;if(n==1)receipt["mutated"]=false;if(n==2)receipt.erase("requestDispatched");if(n==3)receipt["uncertain"]=true;if(n==4)doAdmit=false;
   const auto dispatch=d.dispatch;d.dispatch=[&,n](const auto& selected,const Json& action,const auto& admit){const auto response=dispatch(selected,action,admit);if(n==5)++generation;if(n==6)stop=true;return response;};
   ManualRepair r;const auto outcome=r.request(request,d);(void)outcome;d.dispatch=dispatch;
   need(callbacks==0&&g_deckyManualBaseline.origin(session,"manual-legacy-after",60).empty(),"unadmitted/unacknowledged/uncertain/OFF/power-changing dispatch seeded legacy completion authority");}
  stop=false;g_deckyManualBaseline.clear();generation=88;doAdmit=true;receipt={{"ok",true},{"mutated",true},{"requestDispatched",true}};
  const auto normalDispatch=d.dispatch;
  d.dispatch=[&](const auto& selected,const Json& action,const auto& admit){const auto acknowledged=normalDispatch(selected,action,admit);g_deckyManualBaseline.clear();return acknowledged;};
  ManualRepair lateManual;const auto acknowledged=lateManual.request(request,d);d.dispatch=normalDispatch;
  need(acknowledged.value("ok",false)&&g_deckyManualBaseline.origin(session,"manual-legacy-after",generation).empty(),"OFF before a late positive callback rearmed old explicit manual baseline provenance");
  runtime["enabled"]=false;const auto offResult=lateManual.request(request,d);
  need(!offResult.value("ok",true)&&g_deckyManualBaseline.origin(session,"manual-legacy-after",generation).empty(),"disabled runtime captured a new manual baseline");
  runtime["enabled"]=true;dispatchTick+=30001;const auto freshAcknowledged=lateManual.request(request,d);
  need(freshAcknowledged.value("ok",false)&&g_deckyManualBaseline.origin(session,"manual-legacy-after",generation)=="manual-legacy-before","OFF fencing permanently blocked a later fresh explicit admitted manual repair");
  g_deckyManualBaseline.clear();
  ManualBaseline ordered;const auto oldEpoch=ordered.epoch();ordered.clear();
  ordered.capture(session,"newer-manual-origin",generation,ordered.epoch());
  ordered.capture(session,"old-manual-origin",generation,oldEpoch);
  need(ordered.origin(session,"newer-ready-document",generation)=="newer-manual-origin","late old callback overwrote a newer current manual baseline");
  // Verify the actual production service forwards RAM-only native origin over
  // its private worker state; a modeled writer call alone cannot catch this wire.
  namespace f=lifecycleServiceFixture;f::g_steamSettingsObserver.value=session;f::g_steamModeObserver.value=view;
  auto& readyView=f::g_steamModeObserver.value;readyView["modeEpoch"]="manual-legacy-after:1";readyView["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
  f::g_deckySidebarRuntime.state={};f::g_deckySidebarRuntime.state.pid=61;f::g_deckySidebarRuntime.state.phase="loader-started";
  f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=77;f::tick=1000;f::build=build;f::backendReady=true;f::beforeReply={};f::responseOverride=nullptr;f::dispatches=f::fileReads=0;f::lastPrivateState=nullptr;
  f::g_deckyManagedReady={session,61,77,"manual-legacy-after",session.account,std::string(32,'c'),"1"};f::g_deckyManualBaseline.clear();f::g_deckyManualBaseline.capture(session,original,77,f::g_deckyManualBaseline.epoch());
  f::deckySidebarServiceManagedLifecycle();
  need(f::dispatches==1&&f::lastPrivateState.value("manualRecoveryDocument",std::string{})==original,"actual native service dropped explicit-manual origin before the private confirmation child");
  readyView["modeEpoch"]="manual-legacy-after-race:1";f::g_deckyManagedReady.document="manual-legacy-after-race";
  f::g_deckyManualBaseline.capture(session,original,77,f::g_deckyManualBaseline.epoch());f::currentAfterReply=true;
  f::beforeReply=[]{f::g_deckyManualBaseline.clear();};f::deckySidebarServiceManagedLifecycle();
  need(!f::currentAfterReply,"OFF cleared native manual origin but left its already-dispatched private confirmation admitted");
  f::beforeReply={};f::g_deckyManualBaseline.clear();
 });
 check("actual native lifecycle writer preserves business settings and rejects partial or legacy history",[&]{
  using namespace ymcc::deckyupdate;
  const auto original=Json{{"schemaVersion",1},{"extensions",{{"deckySidebar",{{"enabled",true}}}}},{"userOpaque",{{"game",7},{"value","keep"}}}};
  cleanupworker::Request request;request.lifecycle=true;request.identity.steam=session;request.state={{"cleanupNativeHost","502:10002"}};
  request.args={{"task","confirm"},{"modeEpoch","managed-writer:1"},{"frontendBuildId",build},{"loaderVersion","v3.3.0"},{"backendPid",61u},{"backendBirth","1234"}};
  auto resetFiles=[&](const Json& doc){fixtureSettings[YM_SETTINGS_FILE]=doc.dump();fixtureSettings[YM_SETTINGS_BACKUP_FILE]=doc.dump();fixtureWrites=fixtureFailWrite=0;};
  resetFiles(original);auto result=deckySidebarManagedLifecycleRecord(request,[]{return true;});
  need(result.value("ok",false)&&fixtureWrites==2,"actual native healthy receipt failed double-copy write");
  const auto saved=ymSettingsReadUnlocked();auto business=saved;business.erase("deckyContextCleanup");need(business==original,"native receipt changed user configuration");
  need(fixtureSettings[YM_SETTINGS_FILE]==fixtureSettings[YM_SETTINGS_BACKUP_FILE],"native ready receipt copies disagree");
  request.args["task"]="inspect";result=deckySidebarManagedLifecycleRecord(request,[]{return true;});need(result.value("ok",false)&&result.at("ledger")==saved.at("deckyContextCleanup")&&fixtureWrites==2,"read-only continuation inspection wrote settings or lost receipt");
  request.args["task"]="confirm";
  for(unsigned n=0;n<5;++n){auto bad=original;if(n==0)bad["deckyContextCleanup"]={{"schemaVersion",1},{"steamSession",ticket(session)}};
   if(n==1)bad["deckyContextCleanup"]=nullptr;if(n==2)bad["extensions"]["deckySidebar"]["enabled"]=false;
   if(n==3)bad["deckyContextCleanup"]=managed::record(session,"managed-writer","502:10002",build,"v3.3.0","reserved");
   if(n==4)bad["deckyContextCleanup"]=managed::record(session,"before-writer","503:10003",build,"v3.3.0","reserved");
   resetFiles(bad);result=deckySidebarManagedLifecycleRecord(request,[]{return true;});need(!result.value("ok",true)&&!fixtureWrites,"legacy/unknown/OFF/same-document/foreign-host history was changed");}
  auto pending=original;pending["deckyContextCleanup"]=managed::record(session,"before-writer","502:10002",build,"v3.3.0","reserved");
  resetFiles(pending);fixtureFailWrite=2;result=deckySidebarManagedLifecycleRecord(request,[]{return true;});
  const auto left=ymSettingsReadUnlocked().at("deckyContextCleanup"),right=ymcc::settingsBaseline::parseDocument(ymSettingsReadRaw(YM_SETTINGS_BACKUP_FILE)).at("deckyContextCleanup");
  need(!result.value("ok",true)&&result.value("uncertain",false)&&left!=right,"partial durable completion reported a certified success");
  request.args["task"]="inspect";const auto partialRead=deckySidebarManagedLifecycleRecord(request,[]{return true;});request.args["task"]="confirm";
  need(!partialRead.value("ok",true),"actual private inspector trusted a primary-only completion while backup remained reserved");
  auto observed=initialMode;observed["modeEpoch"]="managed-writer:1";Recovery next;
  need(!next.continueManaged(session,observed,left,right,"503:10003",build,"v3.3.0"),"partial completion refilled recovery on next host");
  resetFiles(pending);result=deckySidebarManagedLifecycleRecord(request,[]{return true;});need(result.value("ok",false)&&fixtureWrites==2,"actual writer could not finish the exact current host reservation on a different document");
 });
 check("actual native lifecycle service rejects late document account OFF and power replies without IO storms",[&]{
  namespace f=lifecycleServiceFixture;using namespace ymcc::deckyupdate;
  for(unsigned n=0;n<6;++n){
   auto selected=session;selected.created+=100+n;f::g_steamSettingsObserver.value=selected;
   f::g_steamModeObserver.value=initialMode;auto& view=f::g_steamModeObserver.value;
   const auto doc="service-document-"+std::to_string(n);view["modeEpoch"]=doc+":1";view["birth"]=std::to_string(selected.created);view["deckyAccountWatchAvailable"]=true;view["observedAccount"]=9u;
   f::g_deckySidebarRuntime.state={};f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=100+n;
   f::g_deckyUpdateRecovery.userEnable({});f::fileReads=f::dispatches=0;f::build=build;
   f::ledger=managed::record(selected,doc,"501:10001",build,"v3.3.0","ready");
   // Production's compiled identity is 3.3.1. The mode/record must match it,
   // rather than changing a source/version guard to fit an inert fixture.
   view["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;f::ledger["loaderVersion"]=ymcc::deckyidentity::version;
   f::beforeReply=[&,n]{if(n==1)view["modeEpoch"]="late-other-document:1";if(n==2)view["observedAccount"]=10u;if(n==3)f::g_deckySidebarRuntime.state.enabled=false;if(n==4)++f::power;if(n==5)view["mode"]="desktop";};
   f::deckySidebarServiceManagedLifecycle();const bool admitted=f::g_deckyUpdateRecovery.allows(selected);
   need(admitted==(n==0),"late changed document/account/OFF/power/mode reply granted native continuation");
   const auto calls=f::dispatches,reads=f::fileReads;
   if(n==0)for(unsigned storm=0;storm<256;++storm)f::deckySidebarServiceManagedLifecycle();
   need(calls==1&&f::dispatches==1&&f::fileReads==reads,"healthy event storm repeated native inspection or disk IO");
  }
  f::beforeReply={};
 });
 check("actual owned backend confirmation borrows bounded Runtime waits after an early ready receipt",[&]{
  namespace f=lifecycleServiceFixture;
  struct RestoreBackendAbsencePort{~RestoreBackendAbsencePort(){f::backendAbsenceCertified=true;f::afterBackendRead={};}}restoreBackendAbsencePort;
  Json actualBackendClassification;try{actualBackendClassification=backendClassifierFixture::run();}catch(...){workerResults.push_back({{"kind","backend-classifier-failure-observation"},{"observation",backendClassifierFixture::lastObservation}});throw;}workerResults.push_back(actualBackendClassification);
  auto setup=[&](unsigned serial){auto selected=session;selected.created+=500+serial;f::g_steamSettingsObserver.value=selected;
   auto& view=f::g_steamModeObserver.value;view=initialMode;const auto doc="slow-ready-"+std::to_string(serial);
   view["birth"]=std::to_string(selected.created);view["modeEpoch"]=doc+":1";view["deckyAccountWatchAvailable"]=true;view["observedAccount"]=9u;view["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
   f::g_deckySidebarRuntime.state={};f::g_deckySidebarRuntime.state.pid=61;f::g_deckySidebarRuntime.state.phase="loader-started";
   f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=200+serial;f::tick=1000;f::dispatches=f::fileReads=f::backendReads=0;f::backendReady=false;f::build=build;f::beforeReply={};f::responseOverride=nullptr;
   f::g_deckyManagedReady={selected,61,f::power,doc,selected.account,std::string(32,'c'),"1"};return doc;};
  setup(0);f::deckySidebarServiceManagedLifecycle();
  need(f::backendReads==1&&f::dispatches==0&&f::deckySidebarConnectionRecoveryTimeout()==1000,"early ready without listener did not schedule the first owned-backend confirmation");
  for(unsigned n=0;n<256;++n)f::deckySidebarServiceManagedLifecycle();
  need(f::backendReads==1&&f::fileReads==0&&f::dispatches==0,"pre-deadline event storm repeated OS listener/disk/child IO");
  f::tick=2000;f::deckySidebarServiceManagedLifecycle();need(f::backendReads==2&&f::deckySidebarConnectionRecoveryTimeout()==2000,"missing listener did not retain the original next proof deadline");
  f::backendReady=true;f::tick=4000;f::deckySidebarServiceManagedLifecycle();
  need(f::dispatches==1&&f::fileReads==1&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"late owned backend did not record success and return Runtime to an infinite idle wait");
  for(unsigned n=0;n<256;++n)f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1&&f::backendReads==3&&f::fileReads==1,"successful confirmation became steady-state polling");
  setup(1);for(const auto at:{1000ull,2000ull,4000ull,9000ull,21000ull,31000ull}){f::tick=at;f::deckySidebarServiceManagedLifecycle();}
  need(f::backendReads==6&&f::dispatches==0&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"absent backend escaped the bounded thirty-second proof window");
  for(unsigned n=0;n<256;++n){f::g_steamModeObserver.value["modeEpoch"]="slow-ready-1:"+std::to_string(n+2);f::tick+=1000;f::deckySidebarServiceManagedLifecycle();}
  need(f::backendReads==6&&f::dispatches==0,"mode entry events refilled an exhausted document confirmation window");
  // An active native-ready origin owns ONE budget, even while upgrade metadata
  // alternates or arrives in pieces. Fingerprint strings are not a new ready.
  setup(310);f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};
  f::deckySidebarServiceManagedLifecycle();
  for(unsigned storm=0;storm<256;++storm){auto& residue=f::g_steamModeObserver.value["deckyUpdateResidue"];
   residue["frontendBuildId"]=std::string(64,storm%2?'a':'b');residue["loaderVersion"]=storm%2?ymcc::deckyidentity::version:"v3.3.0";
   f::deckySidebarServiceManagedLifecycle();}
  need(f::backendReads==1&&f::fileReads==0&&f::dispatches==0&&f::deckySidebarConnectionRecoveryTimeout()==1000,
   "active fingerprint changes refilled one native-ready origin proof budget");
  for(const auto at:{2000ull,4000ull,9000ull,21000ull,31000ull}){f::tick=at;
   f::g_steamModeObserver.value["deckyUpdateResidue"]["frontendBuildId"]=std::string(64,at%3?'b':'a');
   f::deckySidebarServiceManagedLifecycle();}
  need(f::backendReads==6&&f::dispatches==0&&f::g_deckyManagedReady.finished&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,
   "upgrade metadata changes extended the original thirty-second native-ready deadline");
  f::g_steamModeObserver.value["deckyUpdateResidue"]["frontendBuildId"]=build;
  f::g_steamModeObserver.value["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;f::backendReady=true;
  for(unsigned storm=0;storm<256;++storm){++f::tick;f::deckySidebarServiceManagedLifecycle();}
  need(f::backendReads==6&&f::dispatches==0,"expired origin was reopened by restored exact upgrade fingerprints");
  // A genuinely new authenticated transport receipt has its own proof window,
  // even on the same Steam process/document/owned backend after old expiry.
  const auto sameNative=f::g_steamSettingsObserver.value;f::g_deckyMirrorBroker.value["connection"]="2";
  f::backendReady=false;f::deckySidebarQueueManagedReady(sameNative,61,std::string(32,'c'),"2");f::deckySidebarServiceManagedLifecycle();
  need(f::backendReads==7&&f::dispatches==0&&f::deckySidebarConnectionRecoveryTimeout()==1000,
   "new authenticated ready origin could not obtain its own bounded proof window");
  f::g_deckyMirrorBroker.value["connection"]="1";
  setup(2);f::deckySidebarServiceManagedLifecycle();f::g_deckySidebarRuntime.state.enabled=false;f::deckySidebarServiceManagedLifecycle();
  need(f::deckySidebarConnectionRecoveryTimeout()==INFINITE&&f::dispatches==0,"OFF left an owned-backend proof wake armed");
  setup(3);f::deckySidebarServiceManagedLifecycle();f::g_powerLifecycle=f::PowerLifecycle::Suspended;f::deckySidebarServiceManagedLifecycle();
  need(f::deckySidebarConnectionRecoveryTimeout()==INFINITE&&f::dispatches==0,"sleep retained a pending proof wake or dispatched cleanup");
  f::g_powerLifecycle=f::PowerLifecycle::Ready;
  // Execute the actual private-child branch, not only its modeled reply. All
  // these identity failures must return before even reading native settings.
  cleanupworker::Request privateRequest;privateRequest.lifecycle=true;privateRequest.identity.steam=session;
  privateRequest.state={{"cleanupNativeHost","502:10002"}};
  privateRequest.args={{"task","confirm"},{"modeEpoch","private-proof:1"},{"frontendBuildId",build},{"loaderVersion",ymcc::deckyidentity::version},{"backendPid",0u},{"backendBirth","0"}};
  auto privateMode=initialMode;privateMode["modeEpoch"]="private-proof:1";privateMode["deckyAccountWatchAvailable"]=true;privateMode["observedAccount"]=9u;privateMode["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
  for(unsigned n=0;n<6;++n){auto proof=privateMode;auto selectedBuild=build;
   if(n==0)proof["modeEpoch"]="private-proof:2";
   if(n==1)proof["deckyAccountWatchAvailable"]=false;
   if(n==2)proof["observedAccount"]=10u;
   if(n==3)proof["mode"]="desktop";
   if(n==4)selectedBuild=std::string(64,'b');
   fixtureReads=fixtureWrites=0;const auto noWrite=actualMainLifecycleChildGate(privateRequest,proof,selectedBuild,n!=5);
   need(!noWrite.value("ok",true)&&noWrite.contains("mutated")&&!noWrite.at("mutated").get<bool>()&&
    noWrite.value("reason",std::string{})=="decky-managed-context-changed"&&fixtureReads==0&&fixtureWrites==0,
    "actual private child context refusal touched settings or lost its exact no-write receipt");}
  // Only the private worker's explicit pre-write context refusal may retain
  // this observational proof window. It cannot refund a cleanup reservation.
  setup(4);f::backendReady=true;
  f::responseOverride={{"ok",false},{"mutated",false},{"reason","decky-managed-context-changed"}};
  f::deckySidebarServiceManagedLifecycle();
  need(f::dispatches==1&&f::deckySidebarConnectionRecoveryTimeout()==1000,"proven pre-write context withdrawal permanently consumed the native completion observation");
  const auto fileCount=f::fileReads;
  for(unsigned n=0;n<256;++n)f::deckySidebarServiceManagedLifecycle();
  need(f::dispatches==1&&f::fileReads==fileCount,"certified pre-write refusal retried before its original proof deadline");
  f::responseOverride=nullptr;f::tick=2000;f::g_steamModeObserver.value["modeEpoch"]="slow-ready-4:2";
  f::deckySidebarServiceManagedLifecycle();need(f::dispatches==2&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"fresh exact document proof could not finish its original pending confirmation window");
  // Certified pre-settings refusal can arrive just before the parent cache
  // loses proof. The old current() requirement spent this safe observation.
  for(unsigned missing=0;missing<2;++missing){setup(90+missing);f::backendReady=true;f::g_deckyManagedModePending=false;const auto full=f::g_steamModeObserver.value;
   f::responseOverride={{"ok",false},{"mutated",false},{"reason","decky-managed-context-changed"}};
   f::beforeReply=[missing]{if(missing==0){f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");}else{f::g_steamModeObserver.value["deckyAccountWatchAvailable"]=false;f::g_steamModeObserver.value.erase("observedAccount");}};
   f::deckySidebarServiceManagedLifecycle();
   need(f::dispatches==1&&!f::currentAfterReply&&f::g_deckyManagedModePending.load()&&!f::g_deckyManagedReady.finished&&f::deckySidebarConnectionRecoveryTimeout()==1000,"certified no-write reply lost its original confirmation while parent proof was pending");
   const auto reads=f::fileReads,backend=f::backendReads;
   for(unsigned storm=0;storm<256;++storm)f::deckySidebarServiceManagedLifecycle();
   need(f::dispatches==1&&f::fileReads==reads&&f::backendReads==backend,"parent pending-proof storm replayed private metadata work");
   f::beforeReply={};f::responseOverride=nullptr;f::tick=2000;f::g_steamModeObserver.value=full;f::actualStableModeCallback(f::g_steamSettingsObserver.value);f::deckySidebarServiceManagedLifecycle();
   need(f::dispatches==2&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"fresh same-context proof could not finish certified no-write continuation");
  }
  // Actual private child refuses before settings IO; parent proof is withdrawn
  // only after its framed reply and owned resource return. Later success is an
  // explicit transport port, not an actual new settings write or Steam action.
  {setup(100);auto live=session;live.pid=GetCurrentProcessId();live.created=cleanupworker::processBirth(GetCurrentProcess());need(live.created!=0,"private no-write fixture birth missing");
   auto full=f::g_steamModeObserver.value;full["pid"]=live.pid;full["birth"]=std::to_string(live.created);f::g_steamSettingsObserver.value=live;f::g_steamModeObserver.value=full;
   f::g_deckyManagedReady.session=live;f::g_deckyManagedModePending=false;f::backendReady=true;f::beforeReply={};f::responseOverride=nullptr;f::useActualPrivateChild=true;f::holdActualPrivateChild=false;
   f::privateMain={{"schemaVersion",1},{"extensions",{{"deckySidebar",{{"enabled",true}}}}}};f::privateBackup=f::privateMain;f::privateResults=Json::array();
   f::privateModeOverride=full;f::privateModeOverride["modeEpoch"]="slow-ready-100:2";
   f::afterPrivateReply=[]{f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");};
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1&&!f::currentAfterReply&&f::g_deckyManagedModePending.load()&&!f::g_deckyManagedReady.finished,"actual certified child refusal was lost after parent proof withdrawal");
   need(f::privateResults.size()==1,"actual no-write child receipt missing");const auto actual=f::privateResults.back().at("result");
   need(actual.value("reason",std::string{})=="decky-managed-context-changed"&&!actual.value("mutated",true)&&!actual.value("uncertain",false)&&actual.value("fixtureSettingsReads",99u)==0&&actual.value("fixtureSettingsWrites",99u)==0&&actual.value("fixtureWorkerPid",0u)!=live.pid&&actual.value("cleanupWorkerDestroyed",false)&&actual.value("cleanupWorkerHandles",99u)==0&&actual.value("cleanupWorkerProcesses",99u)==0,"actual no-write reply did not certify pre-settings refusal and owned cleanup");
   workerResults.push_back({{"kind","actual-private-certified-no-write-parent-pending"},{"result",actual},{"scope","Actual private Owner/pipe/schema/child context refusal; parent after-reply withdrawal and later success transport are explicit fixture ports"}});
   f::afterPrivateReply={};f::privateModeOverride=nullptr;f::useActualPrivateChild=false;f::g_steamModeObserver.value=full;f::g_steamModeObserver.value["modeEpoch"]="slow-ready-100:2";f::tick=2000;f::actualStableModeCallback(live);f::deckySidebarServiceManagedLifecycle();
   need(f::dispatches==2&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"actual no-write reply could not resume original bounded proof admission");
  }
  for(unsigned direction=0;direction<2;++direction){setup(110+direction);auto before=f::g_steamSettingsObserver.value;before.account=direction?9:0;auto after=before;after.account=direction?0:9;
   f::g_steamSettingsObserver.value=before;f::g_deckyManagedReady.session=before;f::g_deckyManagedModePending=false;f::backendReady=true;
   f::responseOverride={{"ok",false},{"mutated",false},{"reason","decky-managed-context-changed"}};f::beforeReply=[after]{f::g_steamSettingsObserver.value=after;};
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1&&!f::currentAfterReply&&!f::g_deckyManagedReady.finished&&f::deckySidebarConnectionRecoveryTimeout()==1000,"certified no-write source handoff lost the original effective-account window");
   f::beforeReply={};f::responseOverride=nullptr;f::tick=2000;f::deckySidebarServiceManagedLifecycle();need(f::dispatches==2,"fresh compatible source proof failed no-write continuation");
  }
  // Restore old visible state after each terminal context; none may replay.
  for(unsigned variant=0;variant<18;++variant){setup(120+variant);f::g_deckyManagedModePending=false;f::backendReady=true;const auto original=f::g_steamModeObserver.value;const auto originalSession=f::g_steamSettingsObserver.value;const auto powerBefore=f::power;
   if(variant==2)f::g_deckyManualBaseline.capture(originalSession,"before-no-write-origin",powerBefore,f::g_deckyManualBaseline.epoch());
   f::responseOverride={{"ok",false},{"mutated",false},{"reason","decky-managed-context-changed"}};
   f::beforeReply=[variant]{if(variant==0)f::g_deckyUpdateRecovery.veto();if(variant==1)f::g_deckyAutomaticRepair.endEpisode();if(variant==2)f::g_deckyManualBaseline.clear();if(variant==3)++f::power;
    if(variant==4)++f::g_steamSettingsObserver.value.created;if(variant==5){f::g_steamSettingsObserver.value.account=10;f::g_steamModeObserver.value["observedAccount"]=10u;}if(variant==6)f::g_steamModeObserver.value["observedAccount"]=10u;
    if(variant==7)f::g_steamModeObserver.value["modeEpoch"]="foreign-no-write-document:1";if(variant==8)f::g_deckyMirrorBroker.value["runId"]=std::string(32,'d');if(variant==9)f::g_deckyMirrorBroker.value["connection"]="2";if(variant==10)f::g_deckyMirrorBroker.value["connected"]=false;
    if(variant==11)++f::g_deckySidebarRuntime.state.pid;if(variant==12)f::g_deckySidebarRuntime.state.enabled=false;if(variant==13)f::g_steamModeObserver.value["deckyFrontendLoaded"]=false;if(variant==14)f::g_steamModeObserver.value["deckyUpdateResidue"]["frontendBuildId"]=std::string(64,'b');
    if(variant==15)f::g_steamModeObserver.value["mode"]="desktop";if(variant==16)f::deckySidebarEndManagedModeWait();if(variant==17)f::g_powerLifecycle=f::PowerLifecycle::Suspended;};
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1&&f::g_deckyManagedReady.finished&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"certified no-write exception retained foreign or closed lifecycle proof");
   f::beforeReply={};f::responseOverride=nullptr;f::g_deckyUpdateRecovery.userEnable({});f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=powerBefore;f::g_steamSettingsObserver.value=originalSession;f::g_steamModeObserver.value=original;f::g_deckySidebarRuntime.state={};f::g_deckySidebarRuntime.state.pid=61;f::g_deckySidebarRuntime.state.phase="loader-started";f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};f::tick+=1000;
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"restoring old UI/source replayed a terminal no-write context");f::g_deckyManualBaseline.clear();
  }
  {setup(150);f::g_deckyManagedModePending=false;f::backendReady=true;f::responseOverride={{"ok",false},{"mutated",false},{"reason","decky-managed-context-changed"}};f::beforeReply=[]{f::deckySidebarEndManagedModeWait();};
   f::deckySidebarServiceManagedLifecycle();f::beforeReply={};f::responseOverride=nullptr;f::build=std::string(64,'b');f::g_steamModeObserver.value["deckyUpdateResidue"]["frontendBuildId"]=f::build;f::tick+=1000;
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"retired ready witness was reused for a new frontend fingerprint without a new ready receipt");
   f::g_deckyMirrorBroker.value["connection"]="2";f::deckySidebarQueueManagedReady(f::g_steamSettingsObserver.value,61,std::string(32,'c'),"2");f::deckySidebarServiceManagedLifecycle();need(f::dispatches==2,"new authenticated ready origin could not confirm its new frontend context");f::g_deckyMirrorBroker.value["connection"]="1";
  }
  // A pending cache cannot refill the original deadline. At the old final
  // boundary exactly one last observation is allowed, then certified refusals
  // remain terminal even if mode entries and window hints keep arriving.
  {setup(160);f::g_deckyManagedModePending=false;f::backendReady=true;const auto full=f::g_steamModeObserver.value;
   f::responseOverride={{"ok",false},{"mutated",false},{"reason","decky-managed-context-changed"}};f::beforeReply=[]{f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");};
   f::deckySidebarServiceManagedLifecycle();f::beforeReply={};f::tick=31000;f::g_steamModeObserver.value=full;f::actualStableModeCallback(f::g_steamSettingsObserver.value);f::deckySidebarServiceManagedLifecycle();
   need(f::dispatches==2&&f::g_deckyManagedReady.finished&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"pending no-write confirmation refilled its original thirty-second proof window");
   for(unsigned hint=0;hint<256;++hint){f::tick+=1000;f::g_steamModeObserver.value["modeEpoch"]="slow-ready-160:"+std::to_string(hint+2);f::actualStableModeCallback(f::g_steamSettingsObserver.value);f::deckySidebarServiceManagedLifecycle();}
   need(f::dispatches==2,"entry storms replayed exhausted certified no-write proof");f::responseOverride=nullptr;
  }
  for(unsigned uncertain=0;uncertain<4;++uncertain){setup(170+uncertain);f::g_deckyManagedModePending=false;f::backendReady=true;const auto full=f::g_steamModeObserver.value;
   f::responseOverride={{"ok",false},{"mutated",uncertain==0},{"reason","decky-managed-context-changed"}};if(uncertain==1)f::responseOverride["uncertain"]=true;if(uncertain==2)f::responseOverride.erase("mutated");if(uncertain==3)f::responseOverride["reason"]="decky-managed-commit-unconfirmed";
   f::beforeReply=[]{f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");};f::deckySidebarServiceManagedLifecycle();
   need(f::dispatches==1&&f::g_deckyManagedReady.finished&&!f::g_deckyManagedModePending.load(),"pending parent proof converted uncertain or possibly written reply into retry permission");
   f::beforeReply={};f::responseOverride=nullptr;f::tick=2000;f::g_steamModeObserver.value=full;f::actualStableModeCallback(f::g_steamSettingsObserver.value);f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"fresh UI proof replayed an uncertain private operation");
  }
  for(unsigned n=0;n<4;++n){setup(5+n);f::backendReady=true;
   f::responseOverride={{"ok",false},{"mutated",n==0},{"reason","decky-managed-context-changed"}};
   if(n==1)f::responseOverride["uncertain"]=true;
   if(n==2)f::responseOverride.erase("mutated");
   if(n==3)f::responseOverride["reason"]="decky-managed-commit-unconfirmed";
   f::deckySidebarServiceManagedLifecycle();f::responseOverride=nullptr;f::tick+=30000;f::deckySidebarServiceManagedLifecycle();
   need(f::dispatches==1&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"uncertain or post-write refusal replayed the native metadata dispatch");}
  f::responseOverride=nullptr;
  // Actual shared ModeObserver returns the same verified Big Picture document
  // after a temporary pending cache. No second native ready snapshot is sent.
  for(unsigned early=0;early<4;++early){auto selected=session;selected.created+=1200+early;auto full=initialMode;full["birth"]=std::to_string(selected.created);full["modeEpoch"]="ready-mode-return-"+std::to_string(early)+":1";
   full["deckyAccountWatchAvailable"]=true;full["observedAccount"]=selected.account;full["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
   f::g_steamSettingsObserver.value=selected;f::g_deckySidebarRuntime.state={};f::g_deckySidebarRuntime.state.pid=61;f::g_deckySidebarRuntime.state.phase="loader-started";
   f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=700+early;f::tick=1000;f::build=build;f::backendReady=true;f::beforeReply={};f::responseOverride=nullptr;f::dispatches=f::fileReads=0;f::g_deckySidebarRuntime.connectionWakes=0;f::useActualPrivateChild=false;
   ymcc::steamui::ModeObserver observer;
   HANDLE initial=CreateEventW(nullptr,TRUE,FALSE,nullptr),entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr),settled=CreateEventW(nullptr,FALSE,FALSE,nullptr);
   need(initial&&entered&&release&&settled,"shared mode test events unavailable");
   struct Stop{ymcc::steamui::ModeObserver& owner;HANDLE first,entered,release,settled;~Stop(){SetEvent(release);owner.shutdown();f::g_steamModeObserver.actual=nullptr;CloseHandle(first);CloseHandle(entered);CloseHandle(release);CloseHandle(settled);}}stop{observer,initial,entered,release,settled};
   std::atomic<unsigned> reads{0},stable{0};ymcc::steamui::ModeObserverConfig config;config.debounceMs=1;config.minimumGapMs=1;config.failureGapMs=30;
   config.read=[&](const auto&){const auto n=++reads;if(n==2){SetEvent(entered);WaitForSingleObject(release,1500);}auto proof=full;if(early==2&&n==1){proof["deckyAccountWatchAvailable"]=false;proof.erase("observedAccount");}return proof;};
   config.changed=[&](const auto&,const Json&){SetEvent(initial);};
   config.stableBigPicture=[&](const auto& observed){f::actualStableModeCallback(observed);++stable;SetEvent(settled);};
   f::g_steamModeObserver.actual=&observer;
   need(observer.initialize(std::move(config)),"actual shared mode observer did not initialize");observer.observeSteam(selected);observer.setDemand(ymcc::steamui::ModeConsumer::Decky,true);
   need(WaitForSingleObject(initial,1500)==WAIT_OBJECT_0,"initial verified mode was not delivered");
   if(early!=1){f::deckySidebarQueueManagedReady(selected,61,std::string(32,'c'),"1");
    need(f::g_deckySidebarRuntime.connectionWakes==((early==0||early==3)?1u:0u),"native ready queue crossed missing initial account proof");}
   need(observer.requestRefresh(selected)&&WaitForSingleObject(entered,1500)==WAIT_OBJECT_0,"shared pending mode read did not reach controlled window");
   if(early==1)f::deckySidebarQueueManagedReady(selected,61,std::string(32,'c'),"1");
   if(early==1||early==2)need(f::g_deckyManagedModePending.load()&&f::g_deckySidebarRuntime.connectionWakes==0,"early native-ready notification was discarded while initial shared proof was pending");
   if(early==3)f::g_steamModeObserver.afterSnapshot=[&]{SetEvent(release);need(WaitForSingleObject(settled,1500)==WAIT_OBJECT_0&&stable==1,"controlled actual stable callback did not finish before Park registration");};
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==0,"pending mode dispatched native completion without proof");
   if(early!=3){SetEvent(release);need(WaitForSingleObject(settled,1500)==WAIT_OBJECT_0&&stable==1,"shared mode did not settle through actual stable callback");}
   if(early==3)need(f::g_deckySidebarRuntime.connectionWakes==2&&!f::g_deckyManagedModePending.load(),"shared proof settled before parking and lost its only confirmation wake");
   need(f::g_deckySidebarRuntime.connectionWakes==((early==0||early==3)?2u:1u),"same verified shared mode did not wake the parked owned-backend ready confirmation");
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"fresh shared proof did not finish original native confirmation without another ready snapshot");
   for(unsigned n=0;n<16;++n){need(observer.requestRefresh(selected)&&WaitForSingleObject(settled,1500)==WAIT_OBJECT_0,"stable shared read did not finish");}
   need(f::g_deckySidebarRuntime.connectionWakes==((early==0||early==3)?2u:1u)&&f::dispatches==1,"healthy unchanged mode events created extra native confirmation demand");
  }
  // Initial proof is observational RAM state only. Repeated ready receipts
  // cannot spend IO, wake the owner or move it to another known identity.
  for(unsigned n=0;n<14;++n){auto selected=session;selected.created+=1700+n;selected.account=(n==0||n==13)?0:9;
   auto full=initialMode;const auto doc="initial-proof-"+std::to_string(n);full["birth"]=std::to_string(selected.created);full["modeEpoch"]=doc+":1";
   full["deckyAccountWatchAvailable"]=true;full["observedAccount"]=9u;full["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
   auto pending=full;pending["deckyAccountWatchAvailable"]=false;pending.erase("observedAccount");if(n!=2){pending["mode"]="unknown";pending.erase("modeEpoch");}if(n==13){pending["deckyAccountWatchAvailable"]=true;pending["observedAccount"]=9u;}
   f::g_steamSettingsObserver.value=selected;f::g_steamModeObserver.value=pending;f::g_deckySidebarRuntime.state={};f::g_deckySidebarRuntime.state.pid=61;f::g_deckySidebarRuntime.state.phase="loader-started";
   f::g_deckyUpdateRecovery.userEnable({});f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=900+n;f::tick=1000;f::build=build;f::backendReady=true;f::beforeReply={};f::responseOverride=nullptr;f::dispatches=f::fileReads=f::backendReads=0;f::g_deckySidebarRuntime.connectionWakes=0;
   f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};
   for(unsigned repeated=0;repeated<256;++repeated)f::deckySidebarQueueManagedReady(selected,61,std::string(32,'c'),"1");
   need(f::g_deckyManagedModePending.load()&&f::g_deckyManagedReady.initialProof&&f::g_deckySidebarRuntime.connectionWakes==0&&f::dispatches==0&&f::fileReads==0&&f::backendReads==0,"initial-proof ready storm performed IO or discarded its only authenticated receipt");
   if(n==2){f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");
    for(unsigned repeated=0;repeated<256;++repeated)f::deckySidebarQueueManagedReady(selected,61,std::string(32,'c'),"1");
    need(f::g_deckyManagedReady.document==doc,"initial-proof ready lost its positive document hint during repeated pending receipts");}
   if(n==13){f::g_steamModeObserver.value["deckyAccountWatchAvailable"]=false;f::g_steamModeObserver.value.erase("observedAccount");
    for(unsigned repeated=0;repeated<256;++repeated)f::deckySidebarQueueManagedReady(selected,61,std::string(32,'c'),"1");
    need(f::g_deckyManagedReady.account==9,"initial-proof ready lost its positive account hint during repeated pending receipts");}
   f::g_steamModeObserver.value=full;
   if(n==2)f::g_steamModeObserver.value["modeEpoch"]="replacement-initial-document:1";
   if(n==13)f::g_steamModeObserver.value["observedAccount"]=10u;
   if(n==3)f::g_steamModeObserver.value["observedAccount"]=10u;
   if(n==4)++f::power;
   if(n==5)f::g_deckyMirrorBroker.value["runId"]=std::string(32,'d');
   if(n==6)f::g_deckyMirrorBroker.value["connection"]="2";
   if(n==7)f::g_deckySidebarRuntime.state.enabled=false;
   if(n==8)f::g_deckyUpdateRecovery.veto();
   if(n==9)++f::g_steamSettingsObserver.value.created;
   if(n==10)f::g_steamModeObserver.value["mode"]="desktop";
   if(n==11)++f::g_deckySidebarRuntime.state.pid;
   if(n==12)f::g_powerLifecycle=f::PowerLifecycle::Suspended;
   f::actualStableModeCallback(f::g_steamSettingsObserver.value);
   if(n<2){need(f::g_deckySidebarRuntime.connectionWakes==1&&!f::g_deckyManagedModePending.load()&&!f::g_deckyManagedReady.initialProof&&f::g_deckyManagedReady.document==doc&&f::g_deckyManagedReady.account==9,"first current offline/document proof did not complete its original ready witness");
    f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"completed initial proof did not reach original native confirmation");}
   else need(f::g_deckySidebarRuntime.connectionWakes==0&&!f::g_deckyManagedModePending.load()&&f::dispatches==0&&f::fileReads==0&&f::backendReads==0,"foreign lifecycle evidence adopted an unbound ready witness");
  }
  // A real stable completion may happen after the queue snapshots pending but
  // before it publishes the witness. The queue must close that lost-wake gap.
  {auto selected=session;selected.created+=1900;auto full=initialMode;full["birth"]=std::to_string(selected.created);full["modeEpoch"]="initial-publish-race:1";
   full["deckyAccountWatchAvailable"]=true;full["observedAccount"]=9u;full["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
   f::g_steamSettingsObserver.value=selected;f::g_steamModeObserver.value=full;f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");
   f::g_deckySidebarRuntime.state={};f::g_deckySidebarRuntime.state.pid=61;f::g_deckySidebarRuntime.state.phase="loader-started";f::g_deckySidebarRuntime.connectionWakes=0;
   f::g_deckyUpdateRecovery.userEnable({});f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=1000;f::tick=1000;f::build=build;f::backendReady=true;f::dispatches=0;f::g_deckyManagedModePending=false;
   f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};
   f::g_steamModeObserver.afterSnapshot=[&]{f::g_steamModeObserver.value=full;f::actualStableModeCallback(selected);};
   f::deckySidebarQueueManagedReady(selected,61,std::string(32,'c'),"1");
   need(f::g_deckySidebarRuntime.connectionWakes==1&&!f::g_deckyManagedModePending.load(),"initial proof settled before publication and lost its only confirmation wake");
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"publish-race proof never reached native confirmation");
  }
  // Registry availability is not an account switch: the same positive
  // CurrentUser, Steam process, document and authenticated peer must continue.
  for(unsigned direction=0;direction<2;++direction){auto before=session;before.created+=2200+direction;before.account=direction?9:0;
   auto after=before;after.account=direction?0:9;auto full=initialMode;const auto doc="source-handoff-"+std::to_string(direction);
   full["birth"]=std::to_string(before.created);full["modeEpoch"]=doc+":1";full["deckyAccountWatchAvailable"]=true;full["observedAccount"]=9u;full["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
   f::g_steamSettingsObserver.value=before;f::g_steamModeObserver.value=full;f::g_deckySidebarRuntime.state={};f::g_deckySidebarRuntime.state.pid=61;f::g_deckySidebarRuntime.state.phase="loader-started";f::g_deckySidebarRuntime.connectionWakes=0;
   f::g_deckyUpdateRecovery.userEnable({});f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=1200+direction;f::tick=1000;f::build=build;f::backendReady=true;f::beforeReply={};f::responseOverride=nullptr;f::dispatches=f::fileReads=f::backendReads=0;
   f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};
   f::deckySidebarQueueManagedReady(before,61,std::string(32,'c'),"1");need(f::g_deckySidebarRuntime.connectionWakes==1,"initial offline ready did not queue its observation");
   f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");f::deckySidebarServiceManagedLifecycle();
   need(f::g_deckyManagedModePending.load()&&f::dispatches==0,"pre-handoff mode proof did not park");
   f::g_steamSettingsObserver.value=after;
   for(unsigned repeated=0;repeated<256;++repeated)f::deckySidebarQueueManagedReady(after,61,std::string(32,'c'),"1");
   need(f::g_deckyManagedModePending.load()&&f::g_deckyManagedReady.document==doc&&f::g_deckyManagedReady.account==9&&f::g_deckySidebarRuntime.connectionWakes==1,"registry-source ready storm lost identity or woke before current proof");
   f::g_steamModeObserver.value=full;f::actualStableModeCallback(after);
   need(f::g_deckySidebarRuntime.connectionWakes==2&&!f::g_deckyManagedModePending.load()&&f::g_deckyManagedReady.session==after,"same effective account registry-source handoff lost pending native ready");
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"same effective account handoff failed original owned-backend confirmation");
  }
  auto setupSource=[&](unsigned serial,DWORD registry){auto selected=session;selected.created+=2400+serial;selected.account=registry;
   auto full=initialMode;full["birth"]=std::to_string(selected.created);full["modeEpoch"]="source-case-"+std::to_string(serial)+":1";
   full["deckyAccountWatchAvailable"]=true;full["observedAccount"]=9u;full["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
   f::g_steamSettingsObserver.value=selected;f::g_steamModeObserver.value=full;f::g_deckySidebarRuntime.state={};f::g_deckySidebarRuntime.state.pid=61;f::g_deckySidebarRuntime.state.phase="loader-started";f::g_deckySidebarRuntime.connectionWakes=0;
   f::g_deckyUpdateRecovery.userEnable({});f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=1400+serial;f::tick=1000;f::build=build;f::backendReady=true;f::beforeReply={};f::responseOverride=nullptr;f::dispatches=f::fileReads=f::backendReads=0;
   f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};return std::make_pair(selected,full);};
  // Central F1: worker captured raw S0, then native authority becomes S1
  // after its pending mode snapshot but before Park's register/recheck. Cover
  // both source directions and proof settling before/after registration.
  for(unsigned ordering=0;ordering<4;++ordering){const auto direction=ordering%2;const bool settled=ordering>=2;auto [before,full]=setupSource(70+ordering,direction?9:0);auto after=before;after.account=direction?0:9;
   f::deckySidebarQueueManagedReady(before,61,std::string(32,'c'),"1");
   f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");
   f::g_steamModeObserver.afterSnapshot=[&,after]{f::g_steamSettingsObserver.value=after;if(settled)f::g_steamModeObserver.value=full;};
   f::deckySidebarServiceManagedLifecycle();
   need(!f::g_deckyManagedReady.finished&&f::dispatches==0&&f::g_steamModeObserver.lastRequested==after&&f::g_deckySidebarRuntime.connectionWakes==(settled?2u:1u)&&f::g_deckyManagedModePending.load()!=settled,"stale raw Park recheck retired a compatible native-ready witness");
   if(!settled){f::g_steamModeObserver.value=full;f::actualStableModeCallback(after);}
   need(f::g_deckySidebarRuntime.connectionWakes==2&&!f::g_deckyManagedModePending.load()&&f::g_deckyManagedReady.session==after,"current-source stable proof did not resume after a stale raw Park recheck");
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"stale raw Park handoff never finished original native confirmation");
   for(unsigned repeat=0;repeat<256;++repeat)f::actualStableModeCallback(after);
   need(f::g_deckySidebarRuntime.connectionWakes==2&&f::dispatches==1,"completed stale-raw handoff created ongoing observation demand");
  }
  // Authority can change again while Resume reads the shared snapshot. Do not
  // consume the old mode value: retain compatible pending state for its own
  // proof, or retire a genuinely foreign identity; private IO is not entered.
  for(unsigned variant=0;variant<4;++variant){const auto direction=variant%2;const bool foreign=variant>=2;auto [before,full]=setupSource(80+variant,direction?9:0);auto after=before;after.account=foreign?10:(direction?0:9);
   f::deckySidebarQueueManagedReady(before,61,std::string(32,'c'),"1");f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");f::deckySidebarServiceManagedLifecycle();
   f::g_steamModeObserver.value=full;f::g_steamModeObserver.afterSnapshot=[after]{f::g_steamSettingsObserver.value=after;};f::actualStableModeCallback(before);
   need(f::g_deckySidebarRuntime.connectionWakes==1&&f::dispatches==0&&f::g_deckyManagedModePending.load()!=foreign,"source changed during Resume snapshot but stale proof was consumed");
   if(!foreign){f::actualStableModeCallback(after);need(f::g_deckySidebarRuntime.connectionWakes==2&&f::g_deckyManagedReady.session==after,"compatible second source snapshot failed to resume its own proof");f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"second-source own proof did not finish original confirmation");}
   else{f::g_steamModeObserver.value["observedAccount"]=10u;f::actualStableModeCallback(after);need(f::g_deckySidebarRuntime.connectionWakes==1&&f::dispatches==0,"foreign second source revived the retired receipt");}
  }
  // The hint is never authority, and re-reading authority cannot turn a real
  // process/account/document/power/peer/OFF conflict into a compatible handoff.
  for(unsigned variant=0;variant<12;++variant){auto [before,full]=setupSource(90+variant,0);auto after=before;after.account=9;
   f::deckySidebarQueueManagedReady(before,61,std::string(32,'c'),"1");f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");
   f::g_steamModeObserver.afterSnapshot=[&,variant]{f::g_steamSettingsObserver.value=after;f::g_steamModeObserver.value=full;
    if(variant==0){f::g_steamSettingsObserver.value.account=10;f::g_steamModeObserver.value["observedAccount"]=10u;}
    if(variant==1)f::g_steamSettingsObserver.value.root=L"foreign-stale-root";if(variant==2)++f::g_steamSettingsObserver.value.pid;if(variant==3)++f::g_steamSettingsObserver.value.created;
    if(variant==4)f::g_steamModeObserver.value["modeEpoch"]="foreign-stale-document:1";if(variant==5)++f::power;if(variant==6)f::g_deckyMirrorBroker.value["runId"]=std::string(32,'d');
    if(variant==7)f::g_deckyMirrorBroker.value["connection"]="2";if(variant==8)f::g_deckyUpdateRecovery.veto();if(variant==9)f::g_powerLifecycle=f::PowerLifecycle::Suspended;
    if(variant==10)++f::g_deckySidebarRuntime.state.pid;if(variant==11)f::g_deckySidebarRuntime.state.enabled=false;};
   f::deckySidebarServiceManagedLifecycle();need(f::g_deckySidebarRuntime.connectionWakes==1&&f::dispatches==0&&f::fileReads==0&&f::backendReads==0&&!f::g_deckyManagedModePending.load(),"stale raw hint adopted genuinely foreign current native authority");
  }
  // A ready snapshot can carry a native source captured immediately before
  // Queue runs. Same-process compatible raw provenance must not discard this
  // sole authenticated receipt; mismatched positive/native origins still deny.
  for(unsigned direction=0;direction<2;++direction){auto [before,full]=setupSource(110+direction,direction?9:0);auto after=before;after.account=direction?0:9;
   f::g_steamSettingsObserver.value=after;f::deckySidebarQueueManagedReady(before,61,std::string(32,'c'),"1");
   need(f::g_deckySidebarRuntime.connectionWakes==1&&f::g_deckyManagedReady.session==after&&f::g_deckyManagedReady.account==9,"stale native source argument discarded the only compatible ready receipt before Queue");
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"compatible stale Queue source did not complete original confirmation");
  }
  // A known positive account in the captured source cannot be laundered
  // through current registry zero into a different verified UI account.
  {auto [before,full]=setupSource(115,9);auto after=before;after.account=0;
   f::g_steamSettingsObserver.value=after;f::g_steamModeObserver.value["observedAccount"]=10u;const auto prior=f::g_deckyManagedReady;
   f::deckySidebarQueueManagedReady(before,61,std::string(32,'c'),"1");
   need(f::g_deckySidebarRuntime.connectionWakes==0&&f::dispatches==0&&f::deckySidebarSameManagedReady(prior,f::g_deckyManagedReady),"positive source hint crossed registry zero into another UI account");
  }
  {auto [before,full]=setupSource(116,9);auto after=before;after.account=0;f::g_steamSettingsObserver.value=after;
   f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");f::g_steamModeObserver.value["deckyAccountWatchAvailable"]=false;f::g_steamModeObserver.value.erase("observedAccount");
   f::deckySidebarQueueManagedReady(before,61,std::string(32,'c'),"1");need(f::g_deckyManagedModePending.load()&&f::g_deckyManagedReady.account==9&&f::g_deckySidebarRuntime.connectionWakes==0,"positive source hint was erased by missing current account proof");
   f::g_steamModeObserver.value=full;f::g_steamModeObserver.value["observedAccount"]=10u;f::actualStableModeCallback(after);
   need(!f::g_deckyManagedModePending.load()&&f::g_deckySidebarRuntime.connectionWakes==0&&f::dispatches==0,"initial proof rebound a retained positive source hint to another account");
  }
  for(unsigned variant=0;variant<11;++variant){auto [before,full]=setupSource(120+variant,9);auto after=before;after.account=0;f::g_steamSettingsObserver.value=after;auto hint=before;DWORD pid=61;auto run=std::string(32,'c'),connection=std::string("1");
   if(variant==0){f::g_steamSettingsObserver.value.account=10;f::g_steamModeObserver.value["observedAccount"]=10u;}if(variant==1)hint.root=L"foreign-queued-root";if(variant==2)++hint.pid;if(variant==3)++hint.created;
   if(variant==4)++pid;if(variant==5)run=std::string(32,'d');if(variant==6)connection="2";if(variant==7)f::g_deckyUpdateRecovery.veto();if(variant==8)f::g_powerLifecycle=f::PowerLifecycle::Suspended;if(variant==9)f::g_exitRequested=true;if(variant==10)f::g_deckySidebarRuntime.state.enabled=false;
   const auto prior=f::g_deckyManagedReady;const auto pending=f::g_deckyManagedModePending.load();f::deckySidebarQueueManagedReady(hint,pid,run,connection);
   need(f::g_deckySidebarRuntime.connectionWakes==0&&f::dispatches==0&&f::deckySidebarSameManagedReady(prior,f::g_deckyManagedReady)&&f::g_deckyManagedModePending.load()==pending,"stale Queue hint bypassed native source/owned backend/peer/lifecycle admission");
  }
  // An initially unbound offline receipt may learn its first account later.
  // A positive registry/UI disagreement still cannot bind that empty witness.
  for(unsigned conflict=0;conflict<2;++conflict){auto [before,full]=setupSource(5+conflict,0);auto after=before;after.account=9;
   f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");f::g_steamModeObserver.value["deckyAccountWatchAvailable"]=false;f::g_steamModeObserver.value.erase("observedAccount");
   f::deckySidebarQueueManagedReady(before,61,std::string(32,'c'),"1");need(f::g_deckyManagedReady.account==0&&f::g_deckyManagedModePending.load()&&f::g_deckySidebarRuntime.connectionWakes==0,"initially unknown account receipt gained premature proof");
   f::g_steamSettingsObserver.value=after;f::g_steamModeObserver.value=full;
   if(conflict){f::g_steamModeObserver.value["observedAccount"]=10u;f::actualStableModeCallback(after);
    need(f::g_deckyManagedReady.initialProof&&f::g_deckySidebarRuntime.connectionWakes==0&&f::dispatches==0,"positive registry/UI disagreement bound an unknown-account receipt");f::g_steamModeObserver.value=full;}
   f::actualStableModeCallback(after);need(f::g_deckyManagedReady.session==after&&f::g_deckyManagedReady.account==9&&!f::g_deckyManagedReady.initialProof&&f::g_deckySidebarRuntime.connectionWakes==1,"first positive account proof failed across registry handoff");
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"first positive account handoff never reached native confirmation");
  }
  // Changing registry provenance neither refills the original proof schedule nor
  // bypasses a pending first observation of an already owned backend listener.
  for(unsigned expiry=0;expiry<2;++expiry){auto [selected,full]=setupSource(10+expiry,0);f::backendReady=false;
   f::deckySidebarQueueManagedReady(selected,61,std::string(32,'c'),"1");f::deckySidebarServiceManagedLifecycle();
   need(f::backendReads==1&&f::deckySidebarConnectionRecoveryTimeout()==1000,"source budget initial proof did not borrow original deadline");
   f::tick=1100;for(unsigned n=0;n<32;++n){f::g_steamSettingsObserver.value.account=n%2?0:9;f::deckySidebarServiceManagedLifecycle();}
   need(f::backendReads==1&&f::dispatches==0&&f::deckySidebarConnectionRecoveryTimeout()==900,"registry provenance oscillation refilled or discarded the original proof deadline");
   f::g_steamSettingsObserver.value.account=9;f::tick=expiry?31000:2000;f::backendReady=!expiry;f::deckySidebarServiceManagedLifecycle();
   need(f::backendReads==2&&f::dispatches==(expiry?0u:1u)&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"source handoff did not honor original success or exhausted thirty-second boundary");
   for(unsigned n=0;n<32;++n){f::g_steamSettingsObserver.value.account=n%2?0:9;f::tick+=1000;f::deckySidebarServiceManagedLifecycle();}
   need(f::backendReads==2&&f::dispatches==(expiry?0u:1u),"completed source handoff proof was reopened by registry changes");
  }
  // A real account change or any other lifecycle disagreement still retires
  // the pending receipt. Registry zero cannot stand in for a matching user.
  for(unsigned n=0;n<12;++n){auto [before,full]=setupSource(30+n,n==0?9:0);auto after=before;after.account=n==0?10:9;
   f::deckySidebarQueueManagedReady(before,61,std::string(32,'c'),"1");f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");f::deckySidebarServiceManagedLifecycle();
   f::g_steamSettingsObserver.value=after;f::g_steamModeObserver.value=full;
   if(n==0||n==11)f::g_steamModeObserver.value["observedAccount"]=10u;
   if(n==1)++f::g_steamSettingsObserver.value.created;
   if(n==2)++f::g_steamSettingsObserver.value.pid;
   if(n==3)f::g_steamSettingsObserver.value.root=L"other-steam-root";
   if(n==4)++f::power;
   if(n==5)f::g_steamModeObserver.value["modeEpoch"]="other-source-document:1";
   if(n==6)f::g_deckyMirrorBroker.value["runId"]=std::string(32,'d');
   if(n==7)f::g_deckyMirrorBroker.value["connection"]="2";
   if(n==8)++f::g_deckySidebarRuntime.state.pid;
   if(n==9)f::g_deckyUpdateRecovery.veto();
   if(n==10)f::g_powerLifecycle=f::PowerLifecycle::Suspended;
   f::actualStableModeCallback(f::g_steamSettingsObserver.value);
   need(f::g_deckySidebarRuntime.connectionWakes==1&&f::dispatches==0&&!f::g_deckyManagedModePending.load(),"registry-source handoff adopted a different account/process/document/power/peer");
  }
  // Before dispatch, source handoff may preserve an observational witness.
  // During private IO, raw session cancellation remains strict and no reply loss
  // or uncertain mutation gains a replay just because effective account matches.
  for(unsigned direction=0;direction<2;++direction){auto [before,full]=setupSource(60+direction,direction?9:0);auto after=before;after.account=direction?0:9;
   f::deckySidebarQueueManagedReady(before,61,std::string(32,'c'),"1");f::currentAfterReply=true;
   f::beforeReply=[after]{f::g_steamSettingsObserver.value=after;};f::responseOverride={{"ok",false},{"mutated",false},{"uncertain",true},{"reason","decky-cleanup-cancelled-unconfirmed"}};
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1&&!f::currentAfterReply&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"in-flight source change weakened the exact private cancellation predicate");
   f::beforeReply={};f::responseOverride=nullptr;f::tick+=1000;f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"in-flight source change replayed uncertain private confirmation");
  }

  // A successful private write may return after the exact frontend evidence
  // changed. current() must retire that reply without replaying it; unrelated
  // appearance changes keep the matching completion valid. Full real Runtime /
  // Job / listener / private-writer recovery is exercised by the focused evidence.
  for(unsigned changedProof=0;changedProof<4;++changedProof){auto [selected,full]=setupSource(900+changedProof,9);
   f::useActualPrivateChild=false;f::deckySidebarQueueManagedReady(selected,61,std::string(32,'c'),"1");
   f::responseOverride={{"ok",true},{"mutated",true},{"cleanupWorkerDestroyed",true},{"cleanupWorkerHandles",0u},{"cleanupWorkerProcesses",0u}};
   f::beforeReply=[&,changedProof]{
    if(changedProof==0){f::build=std::string(64,'b');f::g_steamModeObserver.value["deckyUpdateResidue"]["frontendBuildId"]=f::build;}
    if(changedProof==1)f::g_steamModeObserver.value["deckyUpdateResidue"]["loaderVersion"]="v9.9.9";
    if(changedProof==2)f::g_steamModeObserver.value["deckyFrontendLoaded"]=false;
    if(changedProof==3)f::g_steamModeObserver.value["unrelatedAppearanceRevision"]=17u;
   };
   f::currentAfterReply=false;f::deckySidebarServiceManagedLifecycle();
   need(f::dispatches==1&&f::currentAfterReply==(changedProof==3)&&f::g_deckyManagedReady.finished&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"returned private success ignored changed exact frontend/Loader/load evidence or rejected an unrelated appearance change");
   workerResults.push_back({{"kind","post-reply-exact-frontend-proof"},{"changedProof",changedProof},{"currentAfterReply",f::currentAfterReply},{"dispatches",f::dispatches},{"scope","Actual native current() and service; explicit successful private reply/mode/build IO ports. Full actual owned chain is separate focused evidence."}});
   f::beforeReply={};f::responseOverride=nullptr;f::g_steamModeObserver.value=full;f::build=build;f::tick+=1000;f::deckySidebarServiceManagedLifecycle();
   need(f::dispatches==1,"restored cached frontend evidence replayed a retired private write");
  }

  // The native-ready candidate is bound to effective account and authenticated
  // broker identity, even for offline registry account zero. No stale re-entry
  // may spend another proof or wake the native owner for a different identity.
  for(unsigned n=0;n<7;++n){auto selected=session;selected.created+=1400+n;selected.account=0;
   auto full=initialMode;const auto doc="parked-identity-"+std::to_string(n);full["birth"]=std::to_string(selected.created);full["modeEpoch"]=doc+":1";
   full["deckyAccountWatchAvailable"]=true;full["observedAccount"]=9u;full["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
   f::g_steamSettingsObserver.value=selected;f::g_steamModeObserver.value=full;f::g_deckySidebarRuntime.state={};f::g_deckySidebarRuntime.state.pid=61;f::g_deckySidebarRuntime.state.phase="loader-started";
   f::g_deckyUpdateRecovery.userEnable({});f::g_exitRequested=false;f::g_powerLifecycle=f::PowerLifecycle::Ready;f::power=800+n;f::tick=1000;f::build=build;f::backendReady=true;f::beforeReply={};f::responseOverride=nullptr;f::dispatches=0;f::g_deckySidebarRuntime.connectionWakes=0;
   f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};
   f::deckySidebarQueueManagedReady(selected,61,std::string(32,'c'),"1");need(f::g_deckySidebarRuntime.connectionWakes==1,"offline fresh ready candidate failed to queue");
   f::g_steamModeObserver.value["mode"]="unknown";f::g_steamModeObserver.value.erase("modeEpoch");f::deckySidebarServiceManagedLifecycle();
   need(f::g_deckyManagedModePending.load()&&f::dispatches==0,"owned ready candidate did not park without current mode proof");
   f::g_steamModeObserver.value=full;
   if(n==0)f::g_steamModeObserver.value["observedAccount"]=10u;
   if(n==1)f::g_steamModeObserver.value["modeEpoch"]="other-document:1";
   if(n==2)++f::power;
   if(n==3)f::g_deckyMirrorBroker.value["runId"]=std::string(32,'d');
   if(n==4)f::g_deckyMirrorBroker.value["connection"]="2";
   if(n==5)f::g_deckyUpdateRecovery.veto();
   if(n==6)f::g_powerLifecycle=f::PowerLifecycle::Suspended;
   f::actualStableModeCallback(selected);
   need(f::g_deckySidebarRuntime.connectionWakes==1&&f::dispatches==0&&!f::g_deckyManagedModePending.load(),"foreign account/document/power/peer/OFF resumed old ready confirmation demand");
  }
  f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};
  f::g_powerLifecycle=f::PowerLifecycle::Ready;f::g_deckyUpdateRecovery.userEnable({});
  // Exercise the actual private-dispatch cancellation predicate when the
  // authenticated peer changes after admission, before the modeled reply.
  for(unsigned n=0;n<3;++n){setup(40+n);f::backendReady=true;f::currentAfterReply=true;
   f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};
   f::beforeReply=[n]{if(n==0)f::g_deckyMirrorBroker.value["runId"]=std::string(32,'d');if(n==1)f::g_deckyMirrorBroker.value["connection"]="2";if(n==2)f::g_deckyMirrorBroker.value["connected"]=false;};
   f::responseOverride={{"ok",false},{"mutated",false},{"reason","decky-managed-context-changed"}};
   f::deckySidebarServiceManagedLifecycle();
   need(f::dispatches==1&&!f::currentAfterReply,"late authenticated peer change left private confirmation admitted");
   f::g_deckyMirrorBroker.value={{"active",true},{"connected",true},{"runId",std::string(32,'c')},{"connection","1"}};f::beforeReply={};f::responseOverride=nullptr;f::tick+=1000;
   f::deckySidebarServiceManagedLifecycle();need(f::dispatches==1,"late peer refusal replayed the old private confirmation");
  }

  // Same original case, distinct missing-stage orderings; original limits remain.
  for(unsigned lateVariant=0;lateVariant<10;++lateVariant){
   setup(9001+lateVariant);auto selected=f::g_steamSettingsObserver.value;f::g_deckyUpdateRecovery.userEnable(selected);
   f::g_deckyManagedReady={};f::g_deckyManagedModePending=false;f::g_deckySidebarRuntime.connectionWakes=0;
   f::useActualPrivateChild=false;f::privateModeOverride=nullptr;f::responseOverride=nullptr;f::beforeReply={};f::afterBackendRead={};f::backendAbsenceCertified=lateVariant<7||lateVariant==9;
   ymcc::deckymirror::Broker probeBroker;std::atomic<unsigned> readyRequests{0};std::atomic<bool> lastNativeAccepted{false};
   struct StopProbe{ymcc::deckymirror::Broker& broker;~StopProbe(){f::afterBackendRead={};f::g_deckyMirrorBroker.actual=nullptr;broker.stop();}}stopProbe{probeBroker};
   ymcc::deckymirror::Config probeConfig{[&](const std::string& connection,const Json& request){
    const auto revision=readyRequests.load()+1;const Json snapshot={{"revision",revision},{"runId",request.at("runId")},{"ready",true},{"provenance",{{"kind","ymcc-native"},{"pid",GetCurrentProcessId()}}}};
    lastNativeAccepted=f::actualNativeMirrorReply({{"connection",connection},{"message",{{"type","snapshot"},{"runId",request.at("runId")},{"snapshot",snapshot}}}}).get<bool>();++readyRequests;
   },{}};
   need(probeBroker.start(probeConfig),"late initial-proof native probe Broker start failed");f::g_deckyMirrorBroker.actual=&probeBroker;
   const auto boot=probeBroker.bootstrap();actualBrokerClient::LoopbackSocket socket(boot);
   const auto deliver=[&](){const auto expected=readyRequests.load()+1;socket.sendFrame(Json{{"command","snapshot"},{"runId",boot.at("runId")}}.dump());const auto reply=Json::parse(socket.readFrame());need(actualBrokerClient::waitUntil([&]{return readyRequests.load()==expected;},1000)&&lastNativeAccepted&&reply.at("snapshot").value("ready",false),"fresh actual native-ready probe was not authenticated or delivered");return reply;};
   const auto first=deliver();need(f::g_deckySidebarRuntime.connectionWakes==1,"initial real native-ready failed to wake its proof owner");
   if(lateVariant==4){
    f::backendReady=true;f::responseOverride={{"ok",false},{"mutated",true},{"uncertain",true},{"reason","decky-cleanup-worker-read-failed"}};f::deckySidebarServiceManagedLifecycle();
    need(f::dispatches==1&&f::g_deckyManagedReady.finished,"uncertain original confirm did not retire its native-ready origin");
    for(unsigned i=0;i<3;++i){deliver();f::deckySidebarServiceManagedLifecycle();}
    workerResults.push_back({{"kind","late-backend-proof-uncertain-terminal"},{"nativeRequests",readyRequests.load()},{"wakes",f::g_deckySidebarRuntime.connectionWakes.load()},{"backendReads",f::backendReads},{"dispatches",f::dispatches}});
    need(f::g_deckySidebarRuntime.connectionWakes==1&&f::dispatches==1&&f::backendReads==1,"new native revisions replayed an uncertain original confirmation");f::responseOverride=nullptr;socket.close();continue;
   }
   if(lateVariant==3)f::afterBackendRead=[&]{if(f::backendReads==6){f::afterBackendRead={};deliver();}};
   for(const auto at:{1000ull,2000ull,4000ull,9000ull,21000ull,31000ull}){
    if(lateVariant==9&&at==31000)f::backendAbsenceCertified=false;
    f::tick=at;f::deckySidebarServiceManagedLifecycle();
    if(lateVariant==2&&at==1000){f::tick=1001;deliver();f::deckySidebarServiceManagedLifecycle();need(f::backendReads==1&&f::g_deckySidebarRuntime.connectionWakes==1&&f::deckySidebarConnectionRecoveryTimeout()==999,"active native revision refilled or advanced its initial proof window");}
   }
   const auto oldWakes=f::g_deckySidebarRuntime.connectionWakes.load();
   if(lateVariant==3){
    need(readyRequests==2&&oldWakes==2&&!f::g_deckyManagedReady.finished,"native-ready publication gap lost its one late proof wake");
   }else need(f::backendReads==6&&f::dispatches==0&&f::g_deckyManagedReady.finished&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"probe initial proof did not naturally exhaust the original bounded window");
   if(lateVariant>=7){
    f::backendReady=true;deliver();f::deckySidebarServiceManagedLifecycle();
    workerResults.push_back({{"kind","unconfirmed-backend-does-not-grant-late"},{"lateVariant",lateVariant},{"nativeRequests",readyRequests.load()},{"nativeWakes",f::g_deckySidebarRuntime.connectionWakes.load()},{"backendReads",f::backendReads},{"dispatches",f::dispatches},{"absenceCertified",f::backendAbsenceCertified}});
    need(f::g_deckySidebarRuntime.connectionWakes==1&&f::backendReads==6&&f::dispatches==0,"unconfirmed or rejected backend gained a late allowance");socket.close();continue;
   }
   if(lateVariant==6){
    f::deckySidebarEndManagedModeWait();f::backendReady=true;deliver();f::deckySidebarServiceManagedLifecycle();
    workerResults.push_back({{"kind","late-backend-proof-explicit-cancel"},{"nativeRequests",readyRequests.load()},{"wakes",f::g_deckySidebarRuntime.connectionWakes.load()},{"backendReads",f::backendReads},{"dispatches",f::dispatches}});
    need(f::g_deckySidebarRuntime.connectionWakes==1&&f::backendReads==6&&f::dispatches==0,"cancelled initial proof gained late permission from a new snapshot");socket.close();continue;
   }
   if(lateVariant==5){const auto full=f::g_steamModeObserver.value;f::g_steamModeObserver.value["observedAccount"]=10u;deliver();f::deckySidebarServiceManagedLifecycle();need(f::g_deckySidebarRuntime.connectionWakes==1&&f::backendReads==6&&f::dispatches==0,"foreign account snapshot spent late native proof");f::g_steamModeObserver.value=full;}
   if(lateVariant==1){
    deliver();for(const auto at:{31001ull,32001ull,34001ull,39001ull,51001ull,61001ull}){f::tick=at;f::deckySidebarServiceManagedLifecycle();}
    need(f::backendReads==12&&f::dispatches==0&&f::g_deckyManagedReady.finished&&f::deckySidebarConnectionRecoveryTimeout()==INFINITE,"late backend proof did not preserve a separate bounded window");
    f::backendReady=true;for(unsigned i=0;i<2;++i){deliver();f::deckySidebarServiceManagedLifecycle();}
    workerResults.push_back({{"kind","late-backend-native-window-exhausted"},{"nativeRequests",readyRequests.load()},{"wakes",f::g_deckySidebarRuntime.connectionWakes.load()},{"backendReads",f::backendReads},{"dispatches",f::dispatches}});
    need(f::backendReads==12&&f::dispatches==0&&f::g_deckySidebarRuntime.connectionWakes==2,"further native revisions reopened an exhausted late allowance");socket.close();continue;
   }
   f::backendReady=true;f::tick=31001;if(lateVariant!=3)deliver();f::deckySidebarServiceManagedLifecycle();
   workerResults.push_back({{"kind","fresh-native-ready-after-initial-proof-expiry"},{"lateVariant",lateVariant},{"authenticatedRequests",readyRequests.load()},{"firstRevision",first.at("snapshot").at("revision")},{"nativeWakes",f::g_deckySidebarRuntime.connectionWakes.load()},{"backendReads",f::backendReads},{"dispatches",f::dispatches},{"readyWitnessFinished",f::g_deckyManagedReady.finished},{"recoveryTimeout",f::deckySidebarConnectionRecoveryTimeout()},{"scope","Actual Broker/native queue and service; backend-listener/Runtime ownership/session/dispatch/time are explicit ports."}});
   need(f::dispatches==1&&f::backendReads==7,"fresh native-ready after natural initial proof expiry lost bounded late backend confirmation");
   for(unsigned i=0;i<2;++i){deliver();f::deckySidebarServiceManagedLifecycle();}
   need(f::dispatches==1&&f::backendReads==7&&f::g_deckySidebarRuntime.connectionWakes==2,"successful late proof became repeated confirmation work");socket.close();
  }

 });
 check("actual private lifecycle child uses existing Job ownership and cancels before any product action",[&]{
  auto own=session;own.pid=GetCurrentProcessId();const auto life=Json{{"task","inspect"},{"modeEpoch","managed-private:1"},{"frontendBuildId",build},{"loaderVersion","v3.3.0"},{"backendPid",0u},{"backendBirth","0"}};
  const auto result=cleanupworker::dispatch(own,false,life,initial,[]{return true;},cleanupworker::recoveryWindowMs,cleanupworker::lifecycleSchema);
  need(result.value("ok",false)&&result.value("fixtureTask",std::string{})=="inspect"&&ymcc::deckyupdate::validTicket(result.value("nativeHost",std::string{}))&&result.value("cleanupWorkerDestroyed",false)&&result.value("cleanupWorkerHandles",99u)==0&&result.value("cleanupWorkerProcesses",99u)==0,"private lifecycle request did not return through actual owned child and release resources");
  const auto cancelled=cleanupworker::dispatch(own,false,life,initial,[]{return false;},cleanupworker::recoveryWindowMs,cleanupworker::lifecycleSchema);
  need(!cancelled.value("ok",true)&&!cancelled.value("mutated",true),"cancelled lifecycle request entered its child");
 });
 for(const auto& name:selectedCases)if(std::find(declaredCases.begin(),declaredCases.end(),name)==declaredCases.end())cases.push_back({{"name",name},{"pass",false},{"error","unknown native case selection"}});
 if(cases.empty())cases.push_back({{"name","selection"},{"pass",false},{"error","empty native case selection"}});
 const auto passed=std::count_if(cases.begin(),cases.end(),[](const auto& r){return r.at("pass")==true;});const Json result={{"pass",passed==static_cast<int>(cases.size())},{"cases",cases},{"count",cases.size()},{"passed",passed},{"realSteamActions",0},{"hardwareWrites",0},{"selection",{{"mode",argc>2?"exact-native-cases":"full"},{"availableCount",declaredCases.size()},{"requested",selectedCases},{"completeNativeCoverage",argc==2}}},{"upgradeActions",upgradeActions},{"cleanupActions",cleanupActions},{"workerResults",workerResults},{"jointPrivateTrace",lifecycleServiceFixture::privateResults}};std::ofstream out(argv[1]);out<<result.dump(2)<<'\n';std::cout<<result.dump()<<std::endl;return result.at("pass")==true?0:2;}
`;
 const availableCases=[...native.matchAll(/check\("([^"\n]+)",\[&\]/g)].map(match=>match[1]);assert(availableCases.length>0,"Missing native case catalog");
 const selection={mode:selectedCases===null?'full':'exact-native-cases',availableCount:availableCases.length,requested:selectedCases??availableCases,completeNativeCoverage:selectedCases===null};
 fs.writeFileSync(path.join(out,'selection.json'),JSON.stringify(selection,null,2)+'\n');
 if(selectedCases!==null)for(const name of selectedCases)assert(availableCases.includes(name),'Unknown native case: '+name);
 const cpp=path.join(out,'retained-repair.cpp');fs.writeFileSync(cpp,native);const declaration=getNativeLaunchNoIoToolDeclaration();assert(!declaration.blockedReason,declaration.blockedReason);const tools=selectedNativeTools().native;const compilerPin={path:tools.compiler,sha256:sha(fs.readFileSync(tools.compiler))};
 const env=Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]));Object.assign(env,{PATH:path.dirname(tools.compiler)+';'+process.env.SystemRoot+'/System32',INCLUDE:tools.include.join(';'),LIB:tools.lib.join(';'),CL:'',_CL_:'',LINK:'',_LINK_:''});
 const sdkSupplemental=[];for(const name of ['ws2tcpip.h','iphlpapi.h','iphlpapi.lib']){const dirs=name.endsWith('.h')?tools.include:tools.lib;const file=dirs.map(d=>path.join(d,name)).find(p=>fs.existsSync(p));assert(file,'Actual IP Helper SDK input missing '+name);sdkSupplemental.push({path:file,sha256:sha(fs.readFileSync(file)),bytes:fs.statSync(file).size});}
 fs.writeFileSync(path.join(out,'backend-sdk-supplemental-inputs.json'),JSON.stringify({rows:sdkSupplemental,scope:'Known actual added SDK header/import library pinned before/after compile. Original tool declaration remains independently validated; exact whole SDK/include closure is not claimed.'},null,2)+'\n');
 const exe=path.join(out,'retained-repair.exe'),buildStartedAtUtc=new Date().toISOString(),buildStart=performance.now();const build=spawnSync(tools.compiler,['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE','/I'+frozen,'/I'+path.join(root,'deps/json'),cpp,'/Fo'+path.join(out,'retained-repair.obj'),'/Fe'+exe,'/link','advapi32.lib','user32.lib','shell32.lib','winhttp.lib','iphlpapi.lib','ws2_32.lib','bcrypt.lib'],{cwd:out,env,encoding:'utf8',windowsHide:true,timeout:60000});const buildElapsedMs=performance.now()-buildStart,buildEndedAtUtc=new Date().toISOString();fs.writeFileSync(path.join(out,'compile.log'),(build.stdout||'')+(build.stderr||'')+(build.error?.stack??''));fs.writeFileSync(path.join(out,'compile-process.json'),JSON.stringify({compiler:compilerPin,toolInputCount:declaration.artifactInputs.length,startedAtUtc:buildStartedAtUtc,endedAtUtc:buildEndedAtUtc,elapsedMs:buildElapsedMs,pid:build.pid??null,exitCode:build.status,signal:build.signal,error:build.error?.message??null,timeoutMs:60000},null,2)+'\n');assert.equal(build.status,0,'Native manual recovery compile failed: '+(build.error?.message??'')+(build.stdout??'')+(build.stderr??''));
 for(const row of sdkSupplemental)assert.equal(sha(fs.readFileSync(row.path)),row.sha256,'IP Helper SDK input changed during compile');
 const nativeStartedAtUtc=new Date().toISOString(),nativeStart=performance.now();const nativeRun=spawnSync(exe,[path.join(out,'native.json'),...(selectedCases??[])],{encoding:'utf8',windowsHide:true,timeout:10000});const nativeElapsedMs=performance.now()-nativeStart,nativeEndedAtUtc=new Date().toISOString();fs.writeFileSync(path.join(out,'native.log'),(nativeRun.stdout||'')+(nativeRun.stderr||''));fs.writeFileSync(path.join(out,'native-process.json'),JSON.stringify({executable:exe,sha256:sha(fs.readFileSync(exe)),args:[path.join(out,'native.json'),...(selectedCases??[])],startedAtUtc:nativeStartedAtUtc,endedAtUtc:nativeEndedAtUtc,elapsedMs:nativeElapsedMs,pid:nativeRun.pid??null,exitCode:nativeRun.status,signal:nativeRun.signal,error:nativeRun.error?.message??null,timeoutMs:10000},null,2)+'\n');assert.equal(nativeRun.status,0,'Actual native manual recovery regression failed');
 const ui=[];if(selectedCases===null){
 const module={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'src/bridge/deckySidebar.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,require:name=>{assert.equal(name,'./ipc');return {invoke(){throw Error('Unexpected real IPC')},on(){throw Error('Unexpected listener')}}}});const api=module.exports;
 const field=JSON.parse(fs.readFileSync(path.join(root,'tools/ymcc_sim/global/decky-update-recovery/field-record-retained-session-rogx-20261023.json'),'utf8'));assert.equal(field.id,'decky-retained-session-repair-rogx-20261023');const state=field.observed.runtime;assert.equal(api.deckySidebarCanRepair(state),true);ui.push('exact field failure preserves enabled manual repair');assert.match(api.deckyConnectionSummary(state).detail,/手动修复.*验证/);assert(!api.deckyConnectionSummary(state).label.includes('已连接'));ui.push('failure explains fresh validation and cannot claim connected');
 for(const bad of [{enabled:false},{pid:6},{steamKnown:false},{steamRunning:false},{phase:'failed'},{reason:'resource-hash-mismatch'}])assert.equal(api.deckySidebarCanRepair({...state,...bad}),false);ui.push('OFF foreign failure ownership and unknown Steam retain rejection');
 const backend=api.deckyConnectionSummary({...state,phase:'frontend-attached',reason:'retained-frontend-connected',recovery:{phase:'manual-required',attempted:false,reason:'steam-ui-refresh-requires-user'}});assert.match(backend.detail,/后台未连接/);ui.push('missing backend asks for manual repair instead of claiming started');
 }
 assert.equal(sha(Buffer.from(extractNativeMirrorReply())),sha(Buffer.from(productionNativeMirrorReply)),'Native producer changed during regression');
 assert.equal(sha(Buffer.from(extractManagedReadyState())),sha(Buffer.from(productionManagedReadyState)),'Actual managed ready state/queue changed during regression');
 assert.equal(sha(Buffer.from(extractStableModeCallback())),sha(Buffer.from(productionStableModeCallback)),'Actual stable shared-mode callback changed during regression');
 assert.equal(sha(Buffer.from(extractParentHandleSnapshot())),sha(Buffer.from(productionParentHandleSnapshot)),'Original parent handle helper changed during regression');
 assert.equal(sha(Buffer.from(extractManualReceiptCallback())),sha(Buffer.from(productionManualReceiptCallback)),'Actual native manual provenance callback changed during regression');
 assert.equal(sha(Buffer.from(extractLifecycleChildGate())),sha(Buffer.from(productionLifecycleChildGate)),'Actual main lifecycle child gate changed during native regression');
 assert.equal(sha(Buffer.from(extractLifecycleService())),sha(Buffer.from(productionLifecycleService)),'Actual main lifecycle service changed during native regression');
 assert.equal(sha(Buffer.from(extractLifecycleWriter())),sha(Buffer.from(productionLifecycleWriter)),'Actual main lifecycle writer changed during native regression');
 assert.equal(sha(Buffer.from(extractDispatch())),sha(Buffer.from(productionCleanupDispatch)),'Actual main cleanup dependency changed during native regression');
 for(const [p,pin]of Object.entries(pins))assert.equal(sha(fs.readFileSync(path.join(root,p))),pin,'Source changed during regression '+p);const n=JSON.parse(fs.readFileSync(path.join(out,'native.json'),'utf8'));assert.equal(sha(fs.readFileSync(tools.compiler)),compilerPin.sha256,'Manual recovery compiler changed during regression');const result={pass:true,completeRegression:selectedCases===null,selection,compiler:compilerPin,toolInputCount:declaration.artifactInputs.length,nativeCases:n.count,uiCases:ui.length,ui,sourceHashes:pins,sourceSegments:[{path:'native/main.cpp',consumer:'backendProof',sha256:sha(Buffer.from(productionBackendProof)),compiledSha256:sha(Buffer.from(compiledBackendProof)),classifierSha256:sha(Buffer.from(classifierBackendProof)),adaptedIo:['ephemeral port','query fault wrapper']},{path:'native/main.cpp',consumer:'connectionRecoveryTimeout',sha256:sha(Buffer.from(productionRecoveryTimeout)),compiledSha256:sha(Buffer.from(compiledRecoveryTimeout)),adaptedIo:['clock']},{path:'native/main.cpp',consumer:'nativeMirrorReplyProducer',sha256:sha(Buffer.from(productionNativeMirrorReply)),compiledSha256:sha(Buffer.from(compiledNativeMirrorReply))},{path:'tools/decky_sidebar_broker_selftest.cpp',consumer:'originalBrokerLoopbackClient',sha256:sha(Buffer.from(originalBrokerClient)),compiledSha256:sha(Buffer.from(originalBrokerClient))},{path:'native/main.cpp',consumer:'managedReadyState',sha256:sha(Buffer.from(productionManagedReadyState)),compiledSha256:sha(Buffer.from(compiledManagedReadyState))},{path:'native/main.cpp',consumer:'stableModeCallback',sha256:sha(Buffer.from(productionStableModeCallback)),compiledSha256:sha(Buffer.from(compiledStableModeCallback))},{path:'tools/decky_binding_runtime_selftest.cpp',consumer:'parentHandleSnapshot',sha256:sha(Buffer.from(productionParentHandleSnapshot)),compiledSha256:sha(Buffer.from(productionParentHandleSnapshot)),scope:'Exact existing helper slice only, not the full Runtime suite'},{path:'native/main.cpp',consumer:'manualReceiptCallback',sha256:sha(Buffer.from(productionManualReceiptCallback)),compiledSha256:sha(Buffer.from(productionManualReceiptCallback))},{path:'native/main.cpp',consumer:'lifecycleChildGate',sha256:sha(Buffer.from(productionLifecycleChildGate)),compiledSha256:sha(Buffer.from(compiledLifecycleChildGate)),scope:'Actual mode/build/backend identity branch before settings metadata IO'},{path:'native/main.cpp',consumer:'lifecycleService',sha256:sha(Buffer.from(productionLifecycleService)),compiledSha256:sha(Buffer.from(compiledLifecycleService)),adaptedIo:['frontendBuildId','private dispatch']},{path:'native/main.cpp',consumer:'lifecycleWriter',sha256:sha(Buffer.from(productionLifecycleWriter)),compiledSha256:sha(Buffer.from(compiledLifecycleWriter))},{path:'native/main.cpp',consumer:'cleanupWorker',sha256:sha(Buffer.from(productionCleanupDispatch)),compiledSha256:sha(Buffer.from(compiledCleanupDispatch))}],realSteamActions:0,hardwareWrites:0,settingsWrites:0,automaticUiRebuilds:0,finalCleanupActions:n.cleanupActions.length,sourceOverrides:Object.fromEntries([["backendProof",backendProofOverride],["runtimeScheduler",runtimeOverride],["nativeMirrorReplyProducer",nativeMirrorReplyOverride],["managedReadyState",readyStateOverride],["stableModeCallback",stableModeOverride],["lifecycleChildGate",childGateOverride],["lifecycleService",lifecycleServiceOverride],["managed",managedOverride],["lifecycleWriter",lifecycleWriterOverride],["manual",override],["automatic",cleanupOverride],["cleanupWorker",workerOverride],["cleanupDispatch",dispatchOverride]].filter(([,p])=>p).map(([kind,p])=>[kind,{path:p,sha256:sha(fs.readFileSync(p))}])),scope:selectedCases===null?'Actual ManualRepair compiled and TS consumer executed; memory session/mode/preflight/dispatch, no installed or physical acceptance':'EXACT NATIVE CASES ONLY; UI and other native cases NOT_RUN; no full regression or installed acceptance'};fs.writeFileSync(path.join(out,'retained-session-repair.json'),JSON.stringify(result,null,2)+'\n');return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===import.meta.filename){
 assert(process.argv[2]&&!process.argv[2].startsWith('--'),'Evidence output directory required');const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]);let nativeCases;
 for(let i=3;i<process.argv.length;++i){assert.equal(process.argv[i],'--native-case','Unknown CLI argument');assert(i+1<process.argv.length&&!process.argv[i+1].startsWith('--'),'--native-case requires an exact nonempty name');(nativeCases??=[]).push(process.argv[++i]);}
 const result=await retainedSessionRepairTests(root,out,nativeCases?{nativeCases}:{});console.log('Retained session repair PASS mode='+result.selection.mode+' native='+result.nativeCases+' ui='+result.uiCases);
}
