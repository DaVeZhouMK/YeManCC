import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.argv[2]||path.join(root,'../../../Build/Validation/Lossless-Toggle-Xbox-20261022/owner'));
fs.mkdirSync(out,{recursive:true});
const inputs=['native/main.cpp','native/decky_sidebar_diagnostics.h','src/bridge/quickapp.ts','src/bridge/losslessProfiles.ts','src/bridge/losslessScaling.ts','src/bridge/deckyLosslessState.ts','tools/lossless_scaling_selftest.mjs','tools/lossless_toggle_owner_selftest.mjs','src/bridge/api.ts','src/bridge/ipc.ts','native/lossless_launch.h','native/lossless_launch_contract.h','native/lossless_scale.h','native/lossless_scale_contract.h','native/lossless_window_refresh.h','native/lossless_runtime.h','src/bridge/losslessRuntimeState.ts','native/process_efficiency.h','tools/lossless_launch_selftest.cpp','tools/fixtures/lossless_close_probe.ps1','tools/fixtures/lossless_close_owned.cs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
const hash=name=>createHash('sha256').update(fs.readFileSync(path.join(root,name))).digest('hex');
const sourceHashes=Object.fromEntries(inputs.map(name=>[name,hash(name)]));
const run=spawnSync(process.execPath,['tools/lossless_scaling_selftest.mjs'],{cwd:root,encoding:'utf8',windowsHide:true,timeout:30000,
 env:{...process.env,YMCC_DECKY_ARTIFACT_ROOT:out}});
fs.writeFileSync(path.join(out,'owner-stdout.log'),run.stdout||'');fs.writeFileSync(path.join(out,'owner-stderr.log'),(run.stderr||'')+(run.error?String(run.error):''));
let result={pass:false,sourceHashes,actualOSInput:false,actualLsControl:false,scope:'Actual original owner under memory fs/process/shell boundaries; no physical fullscreen proof'};
try{
 assert.equal(run.status,0,'Owner test failed; see stdout/stderr');
 const evidence=JSON.parse(fs.readFileSync(path.join(out,'validation/MAINLINE20-LOSSLESS-OWNER.json'),'utf8'));
 assert.equal(evidence.pass,true);assert.equal(evidence.realSettingsWrites,0);assert.equal(evidence.actualLSControlActions,0);
 const native=path.join(out,'native');fs.mkdirSync(native,{recursive:true});
 const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
 // Consume exactly the six real registrations, preserving adjacent parallel IPC modules.
 const entries=['detect','watch','ensureRunning','getState','ensureScaling','resetFailed'].map(name=>{
   const begin=main.indexOf('    ipc_on("lossless.'+name+'",');
   const end=main.indexOf('\n    });',begin);
   assert(begin>=0&&end>begin,'Native Lossless registration required: '+name);
   return main.slice(begin,end+8);
 });
 const entry=entries.join('\n');
 assert(main.includes('#include "lossless_launch.h"'));
 assert(main.includes('if (cmd == "lossless.ensureRunning" || cmd == "lossless.getState" || cmd == "lossless.ensureScaling" || cmd == "lossless.detect" || cmd == "lossless.watch" || cmd == "lossless.resetFailed") return true;'));
 const consumer=path.join(native,'consumer.cpp');
 fs.writeFileSync(consumer,`#include <atomic>
#include <iostream>
#include <map>
#include <functional>
#include "lossless_launch.h"
#include "lossless_scale.h"
#include "lossless_runtime.h"
#include "process_efficiency.h"
#include "json/json.hpp"
#include "decky_sidebar_diagnostics.h"
using json=nlohmann::json;
static std::atomic<bool> g_exitRequested{false};
static unsigned long long currentPowerGeneration(){return 1;}
static bool hardwareWriteAllowed(){return false;}
static std::wstring U2W(const std::string& s){return std::wstring(s.begin(),s.end());}
static std::string W2U(const std::wstring& s){return std::string(s.begin(),s.end());}
static void ipc_emit(const char*,const json&){}
static std::vector<std::pair<std::string,json>> diagnostics;
static void deckySidebarDiagnostic(const char* event,const json& detail,bool){diagnostics.emplace_back(event,ymcc::deckydiag::record(event,detail));}
static std::map<std::string,std::function<json(const json&)>> handlers;
template<class F>static void ipc_on(const char* name,F f){handlers[name]=f;}
static void install(){
`+entry+`}
int main(){
 install();if(handlers.size()!=6)return 1;
 const auto telemetry=ymcc::deckydiag::frontendRecord(json{{"event","lossless-action-finished"},{"completed",true},{"applied",false},{"pending",true},{"ok",false},{"stage","partial"},{"rawStatus","private"}});
 const auto retained=ymcc::deckydiag::record("lossless-action-finished",telemetry);
 if(retained.at("completed")!=true||retained.at("applied")!=false||retained.at("pending")!=true||retained.contains("rawStatus"))return 11;
 for(const auto& entry:handlers){try{entry.second(json{{"exe","x"},{"extra",1}});return 2;}catch(const std::exception&){} }
 const auto launch=handlers.at("lossless.ensureRunning")(json{{"exe","C:/Fixture/LosslessScaling.exe"}});
 if(launch.at("ok")!=false||launch.at("started")!=false||launch.at("reason")!="context-unavailable")return 3;
 auto request=json{{"exe","C:/Fixture/LosslessScaling.exe"},{"pid",71},{"processCreated","123"},{"gamePath","C:/Games/Game.exe"},{"gamePid",110},{"gameProcessCreated","4567"},{"profileTitle","Game"}};
 const auto scale=handlers.at("lossless.ensureScaling")(request);
 if(scale.at("ok")!=false||scale.at("invoked")!=false||scale.at("reason")!="context-unavailable")return 4;
 auto pathOnly=request;pathOnly.erase("gamePid");pathOnly.erase("gameProcessCreated");
 const auto optional=handlers.at("lossless.ensureScaling")(pathOnly);if(optional.at("reason")!="context-unavailable"||optional.at("invoked")!=false)return 7;
 auto reapply=request;reapply["reapply"]=true;const auto explicitResult=handlers.at("lossless.ensureScaling")(reapply);
 if(explicitResult.at("stopInvoked")!=false||explicitResult.at("restarted")!=false||diagnostics.back().first!="lossless-scale-receipt"||diagnostics.back().second.at("reapply")!=true)return 9;
 reapply["reapply"]=1;try{handlers.at("lossless.ensureScaling")(reapply);return 10;}catch(const std::exception&){}
 auto refresh=request;refresh["refreshWindow"]=true;const auto refreshed=handlers.at("lossless.ensureScaling")(refresh);
 if(refreshed.at("refreshAttempted")!=false||refreshed.at("windowRefreshed")!=false||diagnostics.back().second.at("refreshWindow")!=true)return 12;
 if(refreshed.at("refreshMethod")!=3||refreshed.at("refreshMethodName")!="foreground-request"||refreshed.at("refreshApiAccepted")!=false||refreshed.at("refreshFrameAccepted")!=false||refreshed.at("refreshRedrawAccepted")!=false||refreshed.at("refreshTopAccepted")!=false||refreshed.at("refreshForegroundAccepted")!=false||diagnostics.back().second.at("refreshExperiment")!=false)return 15;
 const auto watchdog=json{{"exe","C:/Fixture/LosslessScaling.exe"},{"pid",71},{"processCreated","123"},{"retryAttempt",0u},{"trigger","scaling-ipc-timeout"}};
 const auto stopped=handlers.at("lossless.resetFailed")(watchdog);if(stopped.at("ok")!=false||stopped.at("failureResetAttempted")!=false||stopped.at("failureResetReason")!="failure-reset-context-changed")return 21;
 auto invalidWatchdog=watchdog;invalidWatchdog["retryAttempt"]=2;try{handlers.at("lossless.resetFailed")(invalidWatchdog);return 22;}catch(const std::exception&){}
 auto ordinaryReset=watchdog;ordinaryReset["trigger"]="scaling-paused";try{handlers.at("lossless.resetFailed")(ordinaryReset);return 25;}catch(const std::exception&){}
 auto missingTrigger=watchdog;missingTrigger.erase("trigger");try{handlers.at("lossless.resetFailed")(missingTrigger);return 26;}catch(const std::exception&){}
 auto retry=request;retry["retryAttempt"]=1u;handlers.at("lossless.ensureScaling")(retry);retry["retryAttempt"]=2;try{handlers.at("lossless.ensureScaling")(retry);return 23;}catch(const std::exception&){}
 auto reset=request;reset["resetOnFailure"]=true;const auto resetResult=handlers.at("lossless.ensureScaling")(reset);
 if(resetResult.at("failureResetRequested")!=false||resetResult.at("failureResetClosed")!=false)return 18;
 reset["resetOnFailure"]=1;try{handlers.at("lossless.ensureScaling")(reset);return 19;}catch(const std::exception&){}
 const auto resetLog=ymcc::deckydiag::record("lossless-scale-receipt",json{{"failureResetRequested",true},{"failureResetAttempted",true},{"failureResetTerminated",true},{"failureResetClosed",true},{"failureResetReason","failure-reset-closed"},{"failureResetError",0u},{"rawProcessPath","private"}});
 if(resetLog.at("failureResetClosed")!=true||resetLog.at("failureResetReason")!="failure-reset-closed"||resetLog.contains("rawProcessPath"))return 20;
 refresh["refreshWindow"]=1;try{handlers.at("lossless.ensureScaling")(refresh);return 13;}catch(const std::exception&){}
 const auto refreshLog=ymcc::deckydiag::record("lossless-scale-receipt",json{{"refreshMethod",2u},{"refreshMethodName","frame-changed"},{"refreshApiAccepted",true},{"refreshFrameAccepted",true},{"refreshRedrawAccepted",true},{"refreshExperiment",false},{"refreshHelperCreated",true},{"refreshHelperDestroyed",true},{"refreshLeftGame",true},{"refreshReturnRequested",true},{"refreshReturned",true},{"refreshElapsedMs",120ull},{"refreshReason","window-refresh-cancelled"},{"rawStatus","private"}});
 if(refreshLog.at("refreshHelperDestroyed")!=true||refreshLog.at("refreshElapsedMs")!=120||refreshLog.contains("rawStatus"))return 14;
 if(refreshLog.at("refreshMethod")!=2||refreshLog.at("refreshMethodName")!="frame-changed"||refreshLog.at("refreshApiAccepted")!=true||refreshLog.at("refreshFrameAccepted")!=true||refreshLog.at("refreshRedrawAccepted")!=true||refreshLog.at("refreshExperiment")!=false)return 16;
 const auto policyLog=ymcc::deckydiag::record("lossless-reset-receipt",json{{"failureResetTrigger","scaling-ipc-timeout"},{"failureResetPolicy","ipc-timeout-only"}});
 if(policyLog.at("failureResetTrigger")!="scaling-ipc-timeout"||policyLog.at("failureResetPolicy")!="ipc-timeout-only")return 27;
 const auto malformedPolicyLog=ymcc::deckydiag::record("lossless-reset-receipt",json{{"failureResetTrigger",true},{"failureResetPolicy",43}});
 if(malformedPolicyLog.contains("failureResetTrigger")||malformedPolicyLog.contains("failureResetPolicy"))return 28;
 const auto foregroundLog=ymcc::deckydiag::record("lossless-scale-receipt",json{{"refreshMethod",3u},{"refreshMethodName","foreground-request"},{"refreshTopAccepted",true},{"refreshForegroundAccepted",false},{"refreshApiAccepted",false}});
 if(foregroundLog.at("refreshMethod")!=3||foregroundLog.at("refreshMethodName")!="foreground-request"||foregroundLog.at("refreshTopAccepted")!=true||foregroundLog.at("refreshForegroundAccepted")!=false)return 24;
 const auto invalidRefreshLog=ymcc::deckydiag::record("lossless-scale-receipt",json{{"refreshMethod",99u},{"refreshMethodName","private title"},{"refreshApiAccepted","true"}});
 if(invalidRefreshLog.contains("refreshMethod")||invalidRefreshLog.contains("refreshMethodName")||invalidRefreshLog.contains("refreshApiAccepted"))return 17;
 auto partial=request;partial.erase("gamePid");try{handlers.at("lossless.ensureScaling")(partial);return 8;}catch(const std::exception&){}
 request.erase("gamePath");request.erase("gamePid");request.erase("gameProcessCreated");request.erase("profileTitle");
 g_exitRequested=true;const auto state=handlers.at("lossless.getState")(request);
 if(state.at("ok")!=false)return 5;
 for(const auto* key:{"pid","processCreated"}){auto bad=request;bad[key]=key==std::string("pid")?json(0):json("bad");try{handlers.at("lossless.getState")(bad);return 6;}catch(const std::exception&){} }
 std::cout<<"native-entry-pass:6 registrations; no real UIA or application control";return 0;
}
`);
 const q=value=>'"'+value.replaceAll('/','\\')+'"';
 const build=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+
  'cl /nologo /std:c++17 /EHsc /utf-8 /O1 /I'+q(path.join(root,'native'))+' '+q(path.join(root,'tools/lossless_launch_selftest.cpp'))+' /Fe'+q(path.join(native,'contract.exe'))+' /Fo'+q(path.join(native,'contract.obj'))+' /link user32.lib\r\nif errorlevel 1 exit /b 1\r\n'+
  'cl /nologo /std:c++20 /EHsc /utf-8 /O1 /I'+q(path.join(root,'native'))+' /I'+q(path.join(root,'deps'))+' /I'+q(path.join(root,'deps/json'))+' '+q(consumer)+' /Fe'+q(path.join(native,'entry.exe'))+' /Fo'+q(path.join(native,'entry.obj'))+' /link user32.lib shell32.lib advapi32.lib\r\nif errorlevel 1 exit /b 1\r\n';
 fs.writeFileSync(path.join(native,'build.cmd'),build);
 const compile=spawnSync('cmd.exe',['/d','/c',path.join(native,'build.cmd')],{cwd:native,encoding:'utf8',windowsHide:true,timeout:20000});
 fs.writeFileSync(path.join(native,'build.log'),(compile.stdout||'')+(compile.stderr||''));assert.equal(compile.status,0,'Native consumer compile failed');
 const inert=path.join(native,'inert/LosslessScaling.exe');fs.mkdirSync(path.dirname(inert),{recursive:true});fs.writeFileSync(inert,'Inert file for injected creator; never executed.');
 const contract=spawnSync(path.join(native,'contract.exe'),[inert],{encoding:'utf8',windowsHide:true,timeout:5000});
 fs.writeFileSync(path.join(native,'contract.log'),(contract.stdout||'')+(contract.stderr||''));assert.equal(contract.status,0,'Native quiet-launch contract failed');
 const nativeContract=JSON.parse(contract.stdout);assert.equal(nativeContract.realApplicationsStarted,0);
 const entryRun=spawnSync(path.join(native,'entry.exe'),[],{encoding:'utf8',windowsHide:true,timeout:5000});assert.equal(entryRun.status,0,'Actual native registration failed');
 fs.writeFileSync(path.join(native,'entry.log'),(entryRun.stdout||'')+(entryRun.stderr||''));
 const close=spawnSync('powershell.exe',['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(root,'tools/fixtures/lossless_close_probe.ps1'),'-ScriptDirectory',path.join(out,'Build/LS-Owner-Scripts'),'-EvidenceDirectory',native],{encoding:'utf8',windowsHide:true,timeout:12000});
 fs.writeFileSync(path.join(native,'close.log'),(close.stdout||'')+(close.stderr||''));assert.equal(close.status,0,'Actual close consumer failed');
 const closeConsumer=JSON.parse(fs.readFileSync(path.join(native,'close-consumer.json'),'utf8').replace(/^\uFEFF/,''));assert.equal(closeConsumer.pass,true);assert.equal(closeConsumer.actualLsActions,0);
 const current=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');assert(entries.every(entry=>current.includes(entry)),'Native launch registration changed during the check');
 result.nativeContract=nativeContract;result.nativeEntryPassed=true;result.closeConsumer=closeConsumer;
 const changedInputs=inputs.filter(name=>hash(name)!==sourceHashes[name]);assert.deepEqual(changedInputs,[]);
 result={...result,pass:true,cases:evidence.cases,results:evidence.results,changedInputs};
}catch(error){result.error=String(error);}
fs.writeFileSync(path.join(out,'lossless-owner-result.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({pass:result.pass,cases:result.cases,error:result.error}));process.exitCode=result.pass?0:1;
