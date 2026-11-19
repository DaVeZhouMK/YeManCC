import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {verifyMouseBridge} from './mouse_mode_bridge_selftest.mjs';
import {verifyMouseSettingsShow} from './mouse_backend_settings_show_selftest.mjs';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {runBootMouseOwner} from './fixtures/mouse_boot_owner.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const rawArgs=process.argv.slice(2),inventoryFreshnessOnly=rawArgs.includes('--inventory-freshness-only'),providerCurrentnessOnly=rawArgs.includes('--policy-provider-currentness');
const bootOwnerOnly=rawArgs.includes('--boot-owner-currentness'),providerOnly=rawArgs.includes('--provider-only');
assert([inventoryFreshnessOnly,providerCurrentnessOnly,bootOwnerOnly,providerOnly].filter(Boolean).length<=1,'Mouse selections are mutually exclusive');
const flags=['--out','--inventory-freshness-only','--policy-provider-currentness','--boot-owner-currentness','--provider-only'];
for(const flag of rawArgs.filter(x=>x.startsWith('--')))assert(flags.includes(flag),'Unknown mouse option: '+flag);
const args=rawArgs.filter(a=>a==='--out'||!flags.includes(a));assert(args.length===1||(args.length===2&&args[0]==='--out'),'Usage: [--out] evidence-directory');
const out=path.resolve(args.at(-1));fs.mkdirSync(out,{recursive:true});
assert(!fs.existsSync(path.join(out,'mouse-mode-native-result.json')),'Evidence already exists');
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const inputs=['src/bridge/ipc.ts','src/bridge/steamDeckMouse.ts','src/bridge/gameproc.ts','native/main.cpp','native/mouse_backend_process.h','native/process_basic_candidates.h','native/process_inventory.h','native/mouse_mode_com_apartment.h','native/mouse_mode_policy.h','native/mouse_mode_foreground.h','native/mouse_mode_foreground_rules.h','native/mouse_mode_application_rules.h','native/game_application_catalog.h','native/game_library_blacklist_entries.h','deps/json/json.hpp','tools/mouse_mode_policy_selftest.cpp','tools/mouse_mode_policy_selftest.mjs','native/process_efficiency.h','native/process_efficiency_inventory.h','native/shortcut_feedback_native.h','tools/mouse_mode_control_fixture.cpp','tools/mouse_backend_fixture.cpp','tools/mouse_backend_process_selftest.cpp','tools/mouse_backend_settings_show_selftest.cpp','tools/mouse_backend_settings_show_selftest.mjs','tools/mouse_mode_native_selftest.mjs','tools/mouse_mode_bridge_selftest.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
if(providerCurrentnessOnly)inputs.push('tools/mouse_mode_provider_currentness_fixture.cpp');
inputs.push('native/settings_baseline.h','tools/fixtures/mouse_boot_owner.mjs','tools/mouse_mode_boot_owner_fixture.cpp','PowerControl/settings-defaults-v1.json');
const sourceHashes=Object.fromEntries(inputs.map(file=>[file,hash(path.join(root,file))]));
const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),feedback=fs.readFileSync(path.join(root,'native/shortcut_feedback_native.h'),'utf8');
const names=['ymSettingsBootWriteMatches','mouseModeRequestExpired','mouseModeBootOwnerCheck','mouseModeSaveBootIntent','ymccIsRogFamily','mouseAutoRulesParse','summonLoad','ipc_cmd_async','mouseModeSelectedBackend','mouseModeSteamDeckBlocked','mouseModeSteamDeckState','mouseModeGamebarAbsent','mouseModeGamebarModelRejection','mouseModeStateLocked','mouseModeGetState','mouseModeSetBackend','mouseModeApplyState','mouseModeReleaseJoyxoff','mouseModeToggleState','mousePolicyGet','toggleMouseMode'];
// summonLoad consumes the immutable mouse lists. Extract its actual declarations
// and parser rather than replacing the newer configuration path with a stub.
const utf8=extract(main,'U2W'),utf8Start=main.indexOf('static std::string W2U(const wchar_t*');
assert(utf8Start>=0&&utf8Start<main.indexOf(utf8.code),'Missing UTF-8 conversion source');
const utf8Code=main.slice(utf8Start,main.indexOf(utf8.code,utf8Start)+utf8.code.length);
assert(utf8Code.includes('W2U(const std::wstring&'),'Missing W2U overload');
const state=extract(main,'MouseAutoRulesState','struct');
const stateOwner=main.match(/^static std::atomic<std::shared_ptr<const MouseAutoRulesState>> g_mouseAutoRules[^\n]*;/m);
assert(stateOwner,'Missing immutable mouse rules owner');
const sourceBlock=(symbol,code)=>({symbol,kind:'source-block',code,sha256:createHash('sha256').update(code).digest('hex')});
const fragments=[sourceBlock('mouse-rules-utf8-conversions',utf8Code),state,sourceBlock('g_mouseAutoRules',stateOwner[0]),...names.map(name=>extract(main,name))];
fragments.push(...['nativeShortcutFeedback','nativeShortcutFeedbackFromResult'].map(name=>extract(feedback,name)));
const registrations=['summon.set','mouseMode.get','mouseMode.setBackend','mouseMode.set','mouseMode.toggle','mouseMode.releaseJoyxoff'].map(name=>{
 const start=main.indexOf('    ipc_on("'+name+'",');assert(start>=0,'Missing '+name);
 const end=main.indexOf('\n    });',start);assert(end>start,'Missing callback end '+name);
 return main.slice(start,end+'\n    });'.length);
}).join('\n');
assert(!/JOYXOFF_(VBS|BAT)|launchJoyxoffToggle/.test(main),'Legacy script route must be removed');
assert(main.includes('#include "mouse_backend_process.h"'),'Production process helper must be connected');
const production=fragments.map(row=>row.code).join('\n')+'\nstatic void registerMouseHandlers(){\n'+registrations+'\n}\n';
const fixture=fs.readFileSync(path.join(root,'tools/mouse_mode_control_fixture.cpp'),'utf8');
const preamble=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
function command(dir,label,body){fs.mkdirSync(dir,{recursive:true});const file=path.join(dir,label+'.cmd');fs.writeFileSync(file,preamble+'set "CL="\r\nset "_CL_="\r\nset "LINK="\r\nset "_LINK_="\r\n'+body+'\r\nexit /b %errorlevel%\r\n');const result=spawnSync('cmd.exe',['/d','/c',file],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(dir,label+'.stdout.log'),result.stdout||'');fs.writeFileSync(path.join(dir,label+'.stderr.log'),result.stderr||'');assert(!result.error&&result.status===0,'Compilation failed: '+file+' '+result.error);}
function compile(dir,source,executable,checked=false){const flags=checked?'/Od /RTC1 /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MDd':'/O2 /DNDEBUG /MD';command(dir,'compile','cl.exe /nologo /std:c++20 /EHsc /utf-8 /W4 /permissive- '+flags+' /I"'+root+'/native" /I"'+root+'/deps/json" "'+source+'" /Fo"'+dir+'/probe.obj" /Fe"'+executable+'" /sourceDependencies "'+dir+'/dependencies.json" /link user32.lib ole32.lib');}
function execute(file,args=[],timeout=20000){const result=spawnSync(file,args,{cwd:path.dirname(file),encoding:'utf8',windowsHide:true,timeout,maxBuffer:2*1024*1024});fs.writeFileSync(file+'.stdout.log',result.stdout||'');fs.writeFileSync(file+'.stderr.log',result.stderr||'');assert(!result.error&&!result.signal,'Execution must exit naturally: '+file);let report;try{report=JSON.parse(result.stdout.trim());}catch{throw Error('Invalid JSON output: '+result.stdout);}return {exitCode:result.status,report};}
if(bootOwnerOnly||providerOnly){
 let selected;
 if(bootOwnerOnly)selected=runBootMouseOwner(root,path.join(out,'boot-owner'),compile);
 else{
  const dir=path.join(out,'provider'),source=path.join(dir,'probe.cpp'),exe=path.join(dir,'probe.exe');fs.mkdirSync(dir,{recursive:true});
  fs.writeFileSync(source,fixture.replace('// PRODUCTION_FUNCTIONS',production),{flag:'wx'});compile(dir,source,exe,false);
  const result=execute(exe);assert.equal(result.exitCode,0,JSON.stringify(result));assert.equal(result.report.status,'PASS');selected=result;
 }
 for(const [file,expected]of Object.entries(sourceHashes))assert.equal(hash(path.join(root,file)),expected,'Source changed during selected test: '+file);
 const report={schemaVersion:1,status:'PASS',scope:bootOwnerOnly?'boot-owner-currentness-only':'provider-only',selected,sourceHashes,physicalController:'NOT_TESTED',installedBackend:'NOT_TOUCHED',defaultGroups:'NOT_RUN'};
 fs.writeFileSync(path.join(out,'mouse-mode-native-result.json'),JSON.stringify(report,null,2),{flag:'wx'});console.log(JSON.stringify({status:'PASS',scope:report.scope}));process.exit(0);
}
if(inventoryFreshnessOnly){
 const dir=path.join(out,'inventory-freshness'),exe=path.join(dir,'test.exe');
 command(dir,'compile','cl.exe /nologo /std:c++20 /EHsc /utf-8 /W4 /permissive- /O2 /DNDEBUG /MD /DYMCC_MOUSE_INVENTORY_FRESHNESS_FIXTURE /I"'+root+'" /I"'+root+'/native" /I"'+root+'/deps/json" "'+root+'/tools/mouse_backend_process_selftest.cpp" /Fo"'+dir+'/probe.obj" /Fe"'+exe+'" /sourceDependencies "'+dir+'/dependencies.json" /link user32.lib');
 const selected=execute(exe);assert(selected.exitCode===0&&selected.report.status==='PASS',JSON.stringify(selected));
 for(const [file,expected]of Object.entries(sourceHashes))assert.equal(hash(path.join(root,file)),expected,'Source changed during selected test: '+file);
 const report={schemaVersion:1,status:'PASS',scope:'inventory-freshness-only',sourceHashes,results:[selected],physicalController:'NOT_TESTED',installedBackend:'NOT_TOUCHED',defaultGroups:'NOT_RUN'};
 fs.writeFileSync(path.join(out,'mouse-mode-native-result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));process.exit(0);
}
if(providerCurrentnessOnly){
 const selectedSource=fs.readFileSync(path.join(root,'tools/mouse_mode_provider_currentness_fixture.cpp'),'utf8');
 const table=selectedSource.match(/static constexpr const char\* kMouseProviderCurrentnessCases\[\] = \{([\s\S]*?)\};/);assert(table,'Missing canonical mouse provider selection');
 const selectedScenarios=[...table[1].matchAll(/"([^"]+)"/g)].map(m=>m[1]);assert.equal(selectedScenarios.length,5);
 const marker=fixture.indexOf('// The lower provider regression');assert(marker>0,'Missing original inert provider leaf boundary');let ports=fixture.slice(0,marker);
 const readAnchor='static Result observe(const std::wstring&){',writeAnchor=' ++writes;if(enabled)++joyStarts;else ++joyStops;';
 assert.equal(ports.split(readAnchor).length,2);assert.equal(ports.split(writeAnchor).length,2);
 ports=ports.replace(readAnchor,readAnchor+'fixtureBeforeRead();').replace(writeAnchor,' fixtureWriteSubmitted(enabled);\n'+writeAnchor);
 const symbols=['ymccIsRogFamily','mouseModeSelectedBackend','mouseModeSteamDeckBlocked','mouseModeSteamDeckState','mouseModeGamebarAbsent','mouseModeGamebarModelRejection','mouseModeStateLocked','mouseModeGetState','mouseModeApplyState'];
 const selectedFragments=symbols.map(name=>extract(main,name));
 const source=path.join(out,'policy-provider-currentness','probe.cpp'),exe=path.join(out,'policy-provider-currentness','probe.exe');fs.mkdirSync(path.dirname(source),{recursive:true});
 fs.writeFileSync(source,selectedSource.replace('// ORIGINAL_PORTS',ports).replace('// ACTUAL_PROVIDER',selectedFragments.map(row=>row.code).join('\n')));
 compile(path.dirname(source),source,exe,false);const runs=[];
 for(const scenario of selectedScenarios){
  const raw=path.join(out,'policy-provider-currentness',scenario+'.json');assert(!fs.existsSync(raw),'Original mouse provider result already exists');
  const start=performance.now(),run=spawnSync(exe,[scenario,raw],{cwd:path.dirname(exe),windowsHide:true,encoding:'utf8',timeout:20000,maxBuffer:2*1024*1024});
  fs.writeFileSync(raw+'.stdout.log',run.stdout??'',{flag:'wx'});fs.writeFileSync(raw+'.stderr.log',run.stderr??'',{flag:'wx'});
  assert(!run.error&&!run.signal,'Original selected process failed to finish');const result=JSON.parse(fs.readFileSync(raw,'utf8'));
  assert.equal(run.status,0,JSON.stringify(result));assert.equal(result.scenario,scenario);assert.equal(result.status,'PASS_SELECTED');assert.equal(result.expiredOffSubmissions,0);assert.equal(result.runtimeWorkerJoined,true);assert.equal(result.realProviderOrProcessCalls,0);
  runs.push({scenario,exitCode:run.status,elapsedMs:performance.now()-start,result,raw:{path:raw,sha256:hash(raw),bytes:fs.statSync(raw).size}});
 }
 assert.deepEqual(runs.map(row=>row.scenario),selectedScenarios);
 for(const [file,expected]of Object.entries(sourceHashes))assert.equal(hash(path.join(root,file)),expected,'Source changed during selected test: '+file);
 const report={schemaVersion:1,status:'PASS',scope:'policy-provider-currentness-only',selectedScenarios,sourceHashes,fragments:selectedFragments.map(({code,...rest})=>rest),runs,physicalController:'NOT_TESTED',installedBackend:'NOT_TOUCHED',defaultGroups:'NOT_RUN'};
 fs.writeFileSync(path.join(out,'mouse-mode-native-result.json'),JSON.stringify(report,null,2),{flag:'wx'});console.log(JSON.stringify({status:'PASS',scope:report.scope,scenarios:selectedScenarios,defaultGroups:'NOT_RUN'}));process.exit(0);
}
const results=[{variant:'production-bridge',report:await verifyMouseBridge(root)}];
for(const configuration of ['Checked','Optimized']){
 const checked=configuration==='Checked';
 for(const variant of (checked ? ['current','guard-removed','early-success','migration-off-removed'] : ['current'])){
  const dir=path.join(out,configuration,variant);fs.mkdirSync(dir,{recursive:true});
  let body=production;
  if(variant==='guard-removed')body=body.replace(extract(body,'mouseModeSteamDeckBlocked').code,'static bool mouseModeSteamDeckBlocked(){return false;}');
  if(variant==='early-success')body=body.replace('if (!started.ok) return failure(started.reason, "JoyXoff 未能保持运行，启动未确认。");','if (!started.ok) return {{"ok",true},{"on",true}};');
  if(variant==='migration-off-removed')body=body.replace('if (backend == "steam") g_mousePolicyMode.store(0);','/* removed migration OFF */');
  const source=path.join(dir,'probe.cpp');fs.writeFileSync(source,fixture.replace('// PRODUCTION_FUNCTIONS',body));
  const exe=path.join(dir,'probe.exe');compile(dir,source,exe,checked);const result=execute(exe);
  const expected=variant==='guard-removed'?'steamdeck-entry-must-reject':variant==='early-success'?'start-failure-must-not-report-on':variant==='migration-off-removed'?'retired-steam-preference-migrates-off-without-input-io':null;
  assert(expected?result.exitCode===1&&result.report.error===expected:result.exitCode===0&&result.report.status==='PASS',variant+': '+JSON.stringify(result));
  const row={configuration,variant,expectedRejection:!!expected,...result};results.push(row);console.log(JSON.stringify(row));
 }
}
const processDir=path.join(out,'win32-process-boundary');fs.mkdirSync(processDir,{recursive:true});
const fixtureExe=path.join(processDir,'BackendFixture.exe');compile(processDir,path.join(root,'tools/mouse_backend_fixture.cpp'),fixtureExe);
const owned=path.join(processDir,'owned','Joyxoff.exe'),foreign=path.join(processDir,'foreign','Joyxoff.exe'),exits=path.join(processDir,'owned','ExitBackend.exe');
for(const target of [owned,foreign,exits]){fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(fixtureExe,target);}
const processTest=path.join(processDir,'process-test.exe');compile(processDir,path.join(root,'tools/mouse_backend_process_selftest.cpp'),processTest);
const processResult=execute(processTest,[owned,foreign,exits],20000);assert(processResult.exitCode===0&&processResult.report.status==='PASS',JSON.stringify(processResult));results.push({variant:'real-win32-owned-processes',...processResult});
results.push({variant:'settings-first-show-on-private-desktop',report:verifyMouseSettingsShow(root,path.join(out,'settings-first-show'))});
for(const [file,expected]of Object.entries(sourceHashes))assert.equal(hash(path.join(root,file)),expected,'Source changed during test: '+file);
const report={schemaVersion:1,status:'PASS',scope:'production-provider-consumers;policy-scheduling-explicit-boundary;isolated-Win32-processes;production-foreground-observer-fake-WinAPI',physicalController:'NOT_TESTED',installedBackend:'NOT_TOUCHED',sourceHashes,fragments:fragments.map(({code,...rest})=>rest),results};
fs.writeFileSync(path.join(out,'mouse-mode-native-result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({status:'PASS',results:results.length,physicalController:'NOT_TESTED'}));
