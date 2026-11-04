import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
// This optional leaf remains part of controller.lossless-toggle-owner.
const scriptDirectory=path.dirname(fileURLToPath(import.meta.url));
const arguments_=process.argv.slice(2);
function valueOf(flag){const i=arguments_.indexOf(flag);if(i<0)return undefined;assert.ok(arguments_[i+1]&&!arguments_[i+1].startsWith('--'),'Missing '+flag+' value');assert.equal(arguments_.lastIndexOf(flag),i,'Duplicate '+flag);return arguments_[i+1];}
const root=path.resolve(valueOf('--source-root')||process.env.YMCC_LOSSLESS_SOURCE_ROOT||path.join(scriptDirectory,'..'));
assert.ok(fs.existsSync(path.join(root,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')),'Supply YMCC_LOSSLESS_SOURCE_ROOT or --source-root when this copy lives in PrivateFixtures');
const {calibrationToolchainPreamble}=await import(pathToFileURL(path.join(root,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')).href);
if(arguments_.includes('--launch-session')){
 await launchSessionOnly();
}else if(arguments_.includes('--discovery')){
 await discoveryOnly();
}else{
const out=path.resolve(process.argv[2]||path.join(root,'../../../Build/Validation/Lossless-Toggle-Xbox-20261022/owner'));
fs.mkdirSync(out,{recursive:true});
const inputs=['native/main.cpp','native/decky_sidebar_diagnostics.h','src/bridge/quickapp.ts','src/bridge/losslessProfiles.ts','src/bridge/losslessProgramState.ts','src/bridge/losslessScaling.ts','src/bridge/deckyLosslessState.ts','tools/lossless_scaling_selftest.mjs','tools/lossless_toggle_owner_selftest.mjs','src/bridge/api.ts','src/bridge/ipc.ts','native/lossless_launch.h','native/lossless_launch_contract.h','native/lossless_scale.h','native/lossless_scale_contract.h','native/lossless_window_refresh.h','native/lossless_runtime.h','native/process_basic_candidates.h','native/process_inventory.h','native/game_process_inventory.h','tools/lossless_discovery_fixture.h','src/bridge/losslessRuntimeState.ts','native/process_efficiency.h','tools/lossless_launch_selftest.cpp','tools/fixtures/lossless_close_probe.ps1','tools/fixtures/lossless_close_owned.cs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
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

}

async function discoveryOnly(){
 const totalStart=performance.now(),startedUtc=new Date().toISOString();
 const valuedFlags=new Set(['--source-root','--candidate-dir','--out']);
 const positional=[];
 for(let i=0;i<arguments_.length;i++){
  const argument=arguments_[i];
  if(argument==='--discovery'){assert.equal(arguments_.filter(v=>v===argument).length,1,'Duplicate --discovery');continue;}
  if(valuedFlags.has(argument)){valueOf(argument);++i;continue;}
  assert.ok(!argument.startsWith('--'),'Unsupported discovery option '+argument);positional.push(argument);
 }
 assert.ok(positional.length<=1,'Only the original output-directory positional argument is allowed');
 assert.ok(!(positional.length&&valueOf('--out')),'Conflicting output paths');
 const candidateArgument=valueOf('--candidate-dir');assert.ok(candidateArgument,'--discovery requires an explicit --candidate-dir');
 const candidate=fs.realpathSync(path.resolve(candidateArgument));
 const out=path.resolve(valueOf('--out')||positional[0]||path.join(scriptDirectory,'DiscoveryExecution'));
 const native=path.join(out,'native');
 assert.ok(!fs.existsSync(path.join(out,'lossless-discovery-result.json'))&&!fs.existsSync(path.join(native,'build.cmd')),'Use a fresh output directory; preserve previous FAIL/UNKNOWN receipts');
 fs.mkdirSync(native,{recursive:true});
 const candidateRuntime=path.join(candidate,'lossless_runtime.h'),candidateHelper=path.join(candidate,'process_basic_candidates.h');
 const fixture=path.join(scriptDirectory,'lossless_discovery_fixture.h'),cpp=path.join(scriptDirectory,'lossless_launch_selftest.cpp');
 const pin=p=>{const real=fs.realpathSync(p),bytes=fs.readFileSync(real);return {path:real,bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};};
 const normalize=p=>path.resolve(p).replaceAll('\\','/').toLowerCase();
 const requiredInputs=[fileURLToPath(import.meta.url),cpp,fixture,candidateRuntime,candidateHelper,path.join(candidate,'process_inventory.h'),path.join(candidate,'game_process_inventory.h'),
  path.join(root,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),path.join(root,'native/lossless_scale.h'),
  path.join(root,'native/lossless_scale_contract.h'),path.join(root,'native/lossless_launch_contract.h'),path.join(root,'native/lossless_window_refresh.h'),process.execPath];
 const inputStart=performance.now(),inputsBefore=requiredInputs.map(pin),identityMs=performance.now()-inputStart;
 const result={pass:false,canonicalId:'controller.lossless-toggle-owner',selection:'discovery',startedUtc,
  root,candidate,out,inputsBefore,actualOSInput:false,actualLsControl:false,actualYmccControl:false,
  normalBusinessExecuted:false,fullFixtureValidation:'NOT_RUN',timings:{identityMs},processes:[],
  scope:'Public candidate discoverRuntime; fake process/name/session/identity APIs only; original defaults not executed'};
 const save=()=>fs.writeFileSync(path.join(out,'lossless-discovery-result.json'),JSON.stringify(result,null,2)+'\n');
 const q=p=>{assert.ok(!/["%\r\n]/.test(p),'Unsupported build path');return '"'+p.replaceAll('/','\\')+'"';};
 function execute(label,command,args,timeoutMs){
  const start=performance.now(),utc=new Date().toISOString();
  const run=spawnSync(command,args,{cwd:native,encoding:'utf8',windowsHide:true,timeout:timeoutMs,maxBuffer:32*1024*1024});
  const receipt={label,command,args,cwd:native,startedUtc:utc,endedUtc:new Date().toISOString(),durationMs:performance.now()-start,
   timeoutMs,pid:run.pid??null,status:run.status,signal:run.signal,error:run.error?String(run.error):null};
  fs.writeFileSync(path.join(native,label+'.stdout.log'),run.stdout||'');fs.writeFileSync(path.join(native,label+'.stderr.log'),run.stderr||'');
  fs.writeFileSync(path.join(native,label+'.process.json'),JSON.stringify(receipt,null,2)+'\n');result.processes.push(receipt);save();
  return run;
 }
 try{
  // No executable commands or alternative toolchains can be supplied by fixture arguments.
  const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
  result.vcvars=pin(vcvars);
  const exe=path.join(native,'contract.exe');
  const build=calibrationToolchainPreamble(vcvars)+'\r\nwhere cl > '+q(path.join(native,'compiler-paths.txt'))+'\r\nif errorlevel 1 exit /b 1\r\nwhere link >> '+q(path.join(native,'compiler-paths.txt'))+'\r\nif errorlevel 1 exit /b 1\r\n'+
   'cl /nologo /std:c++20 /EHsc /utf-8 /O1 /W4 /showIncludes /DYMCC_LOSSLESS_DISCOVERY_ONLY=1 /I'+q(candidate)+' /I'+q(path.join(root,'native'))+' /I'+q(scriptDirectory)+' '+q(cpp)+
   ' /Fe'+q(exe)+' /Fo'+q(path.join(native,'contract.obj'))+' /link user32.lib\r\nexit /b %errorlevel%\r\n';
  fs.writeFileSync(path.join(native,'build.cmd'),build);result.buildCommand=pin(path.join(native,'build.cmd'));save();
  const compiled=execute('compile','cmd.exe',['/d','/c',path.join(native,'build.cmd')],20000);
  const includePaths=[];
  for(const line of ((compiled.stdout||'')+'\n'+(compiled.stderr||'')).split(/\r?\n/)){
   const match=line.match(/(?:[a-z]:[\\/]|\\\\)[^\r\n]*$/i);
   if(match){const file=match[0].trim();if(fs.existsSync(file)&&fs.statSync(file).isFile())includePaths.push(fs.realpathSync(file));}
  }
  result.compilerInputs=[...new Map(includePaths.map(p=>[normalize(p),p])).values()].map(pin);
  const compilerList=path.join(native,'compiler-paths.txt');
  if(fs.existsSync(compilerList))result.compilerTools=fs.readFileSync(compilerList,'utf8').split(/\r?\n/).filter(p=>p&&fs.existsSync(p)).map(pin);
  assert.equal(compiled.status,0,'Native discovery compile failed; raw logs retained');
  const declared=new Set(inputsBefore.map(p=>normalize(p.path)));
  result.undeclaredProjectInputs=result.compilerInputs.filter(p=>{const n=normalize(p.path);return (n.startsWith(normalize(root)+'/')||n.startsWith(normalize(candidate)+'/')||n.startsWith(normalize(scriptDirectory)+'/'))&&!declared.has(n);});
  assert.deepEqual(result.undeclaredProjectInputs,[],'Compiler consumed undeclared project input');
  const included=new Set(result.compilerInputs.map(p=>normalize(p.path)));
  for(const required of [candidateRuntime,candidateHelper,path.join(candidate,'process_inventory.h'),fixture,path.join(root,'native/lossless_scale.h')])
   assert.ok(included.has(normalize(fs.realpathSync(required))),'Compiler did not consume required actual input: '+required);
  if(normalize(candidateRuntime)!==normalize(path.join(root,'native/lossless_runtime.h')))
   assert.ok(!included.has(normalize(path.join(root,'native/lossless_runtime.h'))),'Shared runtime header shadowed candidate');
  result.executable=pin(exe);save();
  const inventoryOnly=process.env.YMCC_INVENTORY_TARGETED==='1';
  result.subselection=inventoryOnly?'inventory-boundaries':'complete-discovery';
  const normal=execute('discovery',exe,inventoryOnly?['--inventory-only']:[],5000);
  assert.equal(normal.status,0,'Public discoverRuntime scenarios failed');
  result.native=JSON.parse((normal.stdout||'').trim());
  assert.equal(result.native.pass,true);assert.equal(result.native.publicDiscoverRuntimeExecuted,true);
  assert.equal(result.native.canonicalId,result.canonicalId);assert.equal(result.native.normalBusinessExecuted,false);
  assert.equal(result.native.comparisons,inventoryOnly?0:25);assert.equal(result.native.executions,inventoryOnly?4:70);assert.equal(result.native.rows.length,inventoryOnly?4:70);
  for(const row of result.native.rows){assert.equal(row.metrics.forbidden,0);assert.equal(row.metrics.acquired,row.metrics.closed);}
  // One same-binary targeted negative; no repeat of the normal groups or normal owner.
  const negative=execute('legacy-no-toolhelp',exe,['--legacy-no-toolhelp'],5000);
  assert.equal(negative.status,1,'Legacy no-Toolhelp negative must reject with actual exit1');
  result.negative=JSON.parse((negative.stdout||'').trim());
  assert.equal(result.negative.pass,false);assert.equal(result.negative.assertion,'NO_TOOLHELP_FAST_PATH');
  assert.equal(result.negative.selection,'legacy-no-toolhelp');assert.equal(result.negative.executions,1);assert.equal(result.negative.comparisons,0);
  assert.equal(result.negative.metrics.toolhelp,1);assert.equal(result.negative.metrics.forbidden,0);assert.equal(result.negative.outstandingFakeHandles,0);
  assert.equal(result.negative.metrics.acquired,result.negative.metrics.closed);
  result.inputsAfter=inputsBefore.map(p=>pin(p.path));
  result.changedInputs=result.inputsAfter.filter((p,i)=>p.sha256!==inputsBefore[i].sha256);
  result.compilerInputsAfter=result.compilerInputs.map(p=>pin(p.path));
  result.changedCompilerInputs=result.compilerInputsAfter.filter((p,i)=>p.sha256!==result.compilerInputs[i].sha256);
  assert.deepEqual(result.changedInputs,[]);assert.deepEqual(result.changedCompilerInputs,[]);
  result.pass=true;
 }catch(error){result.error=String(error);result.stack=error?.stack;}
 finally{
  // Failed compile/semantic runs retain source identity checks as well as raw failure.
  try{
   result.inputsAfter??=inputsBefore.map(p=>pin(p.path));
   result.changedInputs??=result.inputsAfter.filter((p,i)=>p.sha256!==inputsBefore[i].sha256);
   result.compilerInputsAfter??=(result.compilerInputs||[]).map(p=>pin(p.path));
   result.changedCompilerInputs??=result.compilerInputsAfter.filter((p,i)=>p.sha256!==result.compilerInputs[i].sha256);
   if(result.changedInputs.length||result.changedCompilerInputs.length){result.pass=false;result.error??='Actual input identity changed';}
  }catch(error){result.pass=false;result.identityAfterError=String(error);}
  result.endedUtc=new Date().toISOString();result.timings.totalMs=performance.now()-totalStart;
  result.timings.compilationMs=result.processes.filter(p=>p.label==='compile').reduce((sum,p)=>sum+p.durationMs,0);
  result.timings.nativeExecutionMs=result.processes.filter(p=>p.label!=='compile').reduce((sum,p)=>sum+p.durationMs,0);
  save();
 }
 console.log(JSON.stringify({pass:result.pass,selection:result.selection,checks:result.native?.checks,executions:result.native?.executions,error:result.error,out}));
 process.exitCode=result.pass?0:1;
}


async function launchSessionOnly(){
 const totalStart=performance.now(),startedUtc=new Date().toISOString();
 const positional=[],valuedFlags=new Set(['--source-root','--out']);
 for(let i=0;i<arguments_.length;i++){
  const argument=arguments_[i];
  if(argument==='--launch-session'){assert.equal(arguments_.filter(v=>v===argument).length,1,'Duplicate --launch-session');continue;}
  if(valuedFlags.has(argument)){valueOf(argument);++i;continue;}
  assert.ok(!argument.startsWith('--'),'Unsupported launch-session option '+argument);positional.push(argument);
 }
 assert.ok(positional.length<=1,'Only one output-directory positional argument is allowed');
 assert.ok(!(positional.length&&valueOf('--out')),'Conflicting output paths');
 const out=path.resolve(valueOf('--out')||positional[0]||path.join(root,'../../../Testing/YMCC/Workspace/LosslessLaunchSession'));
 const native=path.join(out,'native'),resultPath=path.join(out,'lossless-launch-session-result.json');
 assert.ok(!fs.existsSync(resultPath)&&!fs.existsSync(path.join(native,'build.cmd')),'Use a fresh output directory; preserve previous FAIL/UNKNOWN receipts');
 fs.mkdirSync(native,{recursive:true});
 const pin=file=>{const actual=fs.realpathSync(file),bytes=fs.readFileSync(actual);return {path:actual,bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};};
 const normalize=p=>path.resolve(p).replaceAll('\\','/').toLowerCase();
 const cpp=path.join(root,'tools/lossless_launch_selftest.cpp'),header=path.join(root,'native/lossless_launch.h');
 const requiredInputs=[fileURLToPath(import.meta.url),cpp,header,path.join(root,'native/lossless_launch_contract.h'),path.join(root,'native/process_inventory.h'),path.join(root,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),process.execPath];
 const result={pass:false,status:'INCOMPLETE',canonicalId:'controller.lossless-toggle-owner',selection:'launch-session',startedUtc,
  root,out,argv:process.argv.slice(),cwd:process.cwd(),inputsBefore:[],processes:[],
  defaultOwner:'NOT_RUN',discovery:'NOT_RUN',fullFixtureValidation:'NOT_RUN',actualOSInput:false,actualLsControl:false,actualYmccControl:false,
  scope:'Original YMCC_LOSSLESS_LAUNCH_SESSION_ONLY branch; production NativePorts and Store with fake WinAPI; no product actions'};
 const save=()=>fs.writeFileSync(resultPath,JSON.stringify(result,null,2)+'\n');
 const q=p=>{assert.ok(!/["%\r\n]/.test(p),'Unsupported build path');return '"'+p.replaceAll('/','\\')+'"';};
 function execute(label,command,args,timeoutMs){
  const start=performance.now(),utc=new Date().toISOString();
  const run=spawnSync(command,args,{cwd:native,encoding:'utf8',windowsHide:true,timeout:timeoutMs,maxBuffer:16*1024*1024});
  const receipt={label,command,args,cwd:native,startedUtc:utc,endedUtc:new Date().toISOString(),durationMs:performance.now()-start,
   timeoutMs,pid:run.pid??null,status:run.status,signal:run.signal,error:run.error?String(run.error):null};
  fs.writeFileSync(path.join(native,label+'.stdout.log'),run.stdout||'');fs.writeFileSync(path.join(native,label+'.stderr.log'),run.stderr||'');
  fs.writeFileSync(path.join(native,label+'.process.json'),JSON.stringify(receipt,null,2)+'\n');result.processes.push(receipt);save();return run;
 }
 save();
 try{
  result.inputsBefore=requiredInputs.map(pin);result.sourceSnapshots=[];
  for(const input of result.inputsBefore){
   if(normalize(input.path)===normalize(process.execPath))continue;
   const relative=normalize(input.path)===normalize(fileURLToPath(import.meta.url))?path.join('entry',path.basename(input.path)):path.relative(root,input.path);
   assert.ok(!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'Source snapshot escaped selected root');
   const target=path.join(out,'sources',relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(input.path,target);
   const snapshot=pin(target);assert.equal(snapshot.sha256,input.sha256,'Source changed while preserving original bytes');
   result.sourceSnapshots.push({original:input.path,...snapshot});
  }
  save();
  const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
  result.vcvars=pin(vcvars);
  const exe=path.join(native,'contract.exe');
  const build=calibrationToolchainPreamble(vcvars)+'\r\nwhere cl > '+q(path.join(native,'compiler-paths.txt'))+'\r\nif errorlevel 1 exit /b 1\r\nwhere link >> '+q(path.join(native,'compiler-paths.txt'))+'\r\nif errorlevel 1 exit /b 1\r\n'+
   'cl /nologo /std:c++20 /EHsc /utf-8 /MT /Od /showIncludes /DYMCC_LOSSLESS_LAUNCH_SESSION_ONLY=1 /I'+q(path.join(root,'native'))+' '+q(cpp)+
   ' /Fe'+q(exe)+' /Fo'+q(path.join(native,'contract.obj'))+' /link kernel32.lib\r\nexit /b %errorlevel%\r\n';
  fs.writeFileSync(path.join(native,'build.cmd'),build);result.buildCommand=pin(path.join(native,'build.cmd'));save();
  const compiled=execute('compile','cmd.exe',['/d','/c',path.join(native,'build.cmd')],20000);
  const includePaths=[];
  for(const line of ((compiled.stdout||'')+'\n'+(compiled.stderr||'')).split(/\r?\n/)){
   const match=line.match(/(?:[a-z]:[\\/]|\\\\)[^\r\n]*$/i);
   if(match){const file=match[0].trim();if(fs.existsSync(file)&&fs.statSync(file).isFile())includePaths.push(fs.realpathSync(file));}
  }
  result.compilerInputs=[...new Map(includePaths.map(file=>[normalize(file),file])).values()].map(pin);
  const compilerList=path.join(native,'compiler-paths.txt');
  if(fs.existsSync(compilerList))result.compilerTools=fs.readFileSync(compilerList,'utf8').split(/\r?\n/).filter(file=>file&&fs.existsSync(file)).map(pin);
  assert.equal(compiled.status,0,'Native launch-session compile failed; raw logs retained');
  const included=new Set(result.compilerInputs.map(input=>normalize(input.path))),declared=new Set(result.inputsBefore.map(input=>normalize(input.path)));
  for(const required of [header,path.join(root,'native/lossless_launch_contract.h'),path.join(root,'native/process_inventory.h')])
   assert.ok(included.has(normalize(fs.realpathSync(required))),'Compiler did not consume required actual input: '+required);
  result.undeclaredProjectInputs=result.compilerInputs.filter(input=>normalize(input.path).startsWith(normalize(root)+'/')&&!declared.has(normalize(input.path)));
  assert.deepEqual(result.undeclaredProjectInputs,[],'Compiler consumed undeclared project input');
  assert.ok(result.compilerTools?.some(tool=>path.basename(tool.path).toLowerCase()==='cl.exe'),'Actual compiler path missing');
  assert.ok(result.compilerTools?.some(tool=>path.basename(tool.path).toLowerCase()==='link.exe'),'Actual linker path missing');
  result.executable=pin(exe);save();
  const selected=execute('launch-session',exe,[],5000);
  result.native=JSON.parse((selected.stdout||'').trim());save();
  assert.equal(selected.status,0,'Original launch-session scenarios failed');
  assert.equal(result.native.checks,17);assert.equal(result.native.failures,0);
  assert.deepEqual(result.native.failedAssertions,[]);assert.equal(result.native.realApplicationsStarted,0);
  assert.deepEqual(result.native.cases.map(row=>row.name),['prefilter-session-reuse','same-session-control','held-session-denied']);
  assert.deepEqual(result.native.cases.map(row=>row.checks),[6,5,6]);
  assert.ok(result.native.cases.every(row=>row.failures===0&&row.heldSessionQueries>=1),'Incomplete launch-session boundary coverage');
  result.inputsAfter=requiredInputs.map(pin);
  result.changedInputs=result.inputsAfter.filter((input,index)=>input.sha256!==result.inputsBefore[index].sha256);
  result.compilerInputsAfter=result.compilerInputs.map(input=>pin(input.path));
  result.changedCompilerInputs=result.compilerInputsAfter.filter((input,index)=>input.sha256!==result.compilerInputs[index].sha256);
  result.compilerToolsAfter=result.compilerTools.map(input=>pin(input.path));
  result.changedCompilerTools=result.compilerToolsAfter.filter((input,index)=>input.sha256!==result.compilerTools[index].sha256);
  assert.deepEqual(result.changedInputs,[],'Declared source/tool changed during launch-session test');
  assert.deepEqual(result.changedCompilerInputs,[],'Actual compiler input changed during launch-session test');
  assert.deepEqual(result.changedCompilerTools,[],'Actual compiler/linker changed during launch-session test');
  result.pass=true;result.status='PASS';result.selectedCanonicalLeaves=1;result.cases=3;result.checks=result.native.checks;
 }catch(error){result.status='FAIL';result.error=String(error instanceof Error?error.stack:error);}
 finally{result.endedUtc=new Date().toISOString();result.totalMs=performance.now()-totalStart;save();}
 console.log(JSON.stringify({pass:result.pass,status:result.status,canonicalId:result.canonicalId,selection:result.selection,cases:result.cases??0,checks:result.checks??0,error:result.error,out}));
 if(!result.pass)process.exitCode=1;
}
