// Explicit subprojection of controller.unified-standard-input. Importing is inert.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract} from '../fixtures/msi_owned_return_retired_io/source-binding.mjs';
import {calibrationToolchainPreamble} from '../fixtures/hc_reviewed_calibration/toolchain.mjs';
import {startOwnedRawProducer,rawProgramLauncherDeclaration} from '../ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs';

export const commonHidRuntimeProjection=Object.freeze({
 originalCanonicalId:'controller.unified-standard-input',argument:'--common-hid-runtime-only',
 defaultParentChanged:false,moduleMinimumChanged:false,newGlobalTestIds:[],
 sources:['tools/input_frame_submission_selftest.mjs','tools/lib/common_hid_runtime_fixture.mjs',
  'tools/fixtures/common_hid_runtime/runtime_fixture.cpp.in','tools/fixtures/common_hid_runtime/common_hid_runtime_cases.inc.in',
  'tools/fixtures/common_hid_runtime/common_hid_os_leaves.inc','tools/fixtures/common_hid_runtime/inspect_cases.inc',
  'tools/fixtures/msi_owned_return_retired_io/source-binding.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs',
  'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs','tools/ymcc_sim/global/host-process.ps1',
  'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json','native/main.cpp','deps/json/json.hpp'],
 headers:['background_controller_ui_runtime.h','background_controller_ui.h','xinput_identity_refresh_policy.h',
  'controller_hid_source_runtime.h','controller_hid_source.h','controller_hid_source_lane.h','sleep_power_state.h','touch_haptic_dispatch.h','input_host_diagnostic_dispatch.h']
});
export const win5KeptWakeCase='win5-kept-wake';
const win5Selector='--isolation-hid-win5-kept-wake';
const win5Inputs=Object.freeze({
 sources:['tools/input_frame_submission_selftest.mjs','tools/lib/common_hid_runtime_fixture.mjs',
  'tools/background_controller_ui_runtime_selftest.cpp','deps/json/json.hpp',
  'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/msi_owned_return_retired_io/source-binding.mjs',
  'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs','tools/ymcc_sim/global/host-process.ps1',
  'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'],
 headers:['background_controller_ui_runtime.h','background_controller_ui.h','xinput_identity_refresh_policy.h',
  'controller_hid_source_runtime.h','controller_hid_source.h','controller_hid_source_lane.h','controller_isolation_hid_reader.h',
  'controller_isolation_hid_runtime.h','sleep_power_state.h','sleep_power_dispatch.h',
  'virtual_wake_execution_runtime.h','virtual_recovery_intent.h','game_input_override.h']
});
export const commonHidRuntimeInputs=runtimeCase=>runtimeCase===win5KeptWakeCase?win5Inputs:commonHidRuntimeProjection;
export const commonHidRuntimeCases=Object.freeze(['all','observation','business','inspection-early','inspection-early-business','off-fse','source-binding',win5KeptWakeCase]);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=file=>{const b=fs.readFileSync(file);return {path:fs.realpathSync(file),bytes:b.length,sha256:sha(b)};};
const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const q=s=>{assert(!/["%\r\n]/.test(s),'UNSUPPORTED_COMMAND_PATH');return '"'+s+'"';};
const norm=s=>s.replaceAll('\\','/');

function prepareWin5KeptWake(inc,sourceBytes,generated,bindings){
 const put=(relative,content)=>{
  const file=path.join(inc,relative);fs.mkdirSync(path.dirname(file),{recursive:true});
  fs.writeFileSync(file,content,{flag:'wx'});generated.push(pin(file));return file;
 };
 const slice=(name,label,start,end)=>{
  const source=sourceBytes.get('native/'+name).toString('utf8').replace(/\r\n/g,'\n');
  assert.equal(source.split(start).length,2,'UNIQUE_WIN5_PROJECTION_START '+label);
  const tail=source.slice(source.indexOf(start));const stop=end===null?tail.trimEnd().lastIndexOf('\n}'):tail.indexOf(end);
  assert(stop>0&&(end!==null?tail.split(end).length===2:tail.trimEnd().endsWith('\n}')&&tail.trimEnd().slice(stop)==='\n}'),'WIN5_PROJECTION_END '+label);
  const value=tail.slice(0,stop)+'\n';bindings.push({file:'native/'+name,projection:label,sha256:sha(value),lineEndings:'LF'});return value;
 };
 const cpp='tools/background_controller_ui_runtime_selftest.cpp';
 const fixture=put(cpp,sourceBytes.get(cpp));
 const projected=['controller_hid_source_runtime.h','controller_isolation_hid_runtime.h','virtual_wake_execution_runtime.h'];
 for(const name of win5Inputs.headers.filter(name=>!projected.includes(name)))put('native/'+name,sourceBytes.get('native/'+name));
 put('deps/json/json.hpp',sourceBytes.get('deps/json/json.hpp'));
 const publish=slice('virtual_wake_execution_runtime.h','win5-wake-publish','static const char* publishWin5WakeReader(', '\nstatic bool complete(const Work& w,');
 const observe=slice('controller_isolation_hid_runtime.h','observe','static std::uint64_t readSleepBoundary=0;',null);
 put('observe.inc',publish+observe);
 put('raw-fence.inc',slice('controller_hid_source_runtime.h','raw-fence','namespace ymccCommonHid {\n// Called under the existing read mutex.','\n\nstatic bool commonControllerHidRead('));
 return fixture;
}

const rawCleanupReserveMs=5000;
const win5ParentReturnReserveMs=1000;
export function commonHidProducerBudget(started,now,stage,inheritedDeadlineEpochMs){
 const originalBudgetMs=stage==='compile'?180000:60000;
 assert(inheritedDeadlineEpochMs===undefined||Number.isSafeInteger(inheritedDeadlineEpochMs)&&inheritedDeadlineEpochMs>0,'COMMON_HID_INHERITED_DEADLINE_REQUIRED');
 const parentEnd=Math.min(started+300000,inheritedDeadlineEpochMs===undefined?Infinity:inheritedDeadlineEpochMs-win5ParentReturnReserveMs);
 const parentBudgetMs=parentEnd-started,parentRemainingBeforeMs=parentEnd-now;
 assert(parentRemainingBeforeMs>rawCleanupReserveMs,'PROJECTION_BUDGET_EXHAUSTED');
 return {parentBudgetMs,parentRemainingBeforeMs,originalBudgetMs,deadlineEpochMs:now+Math.min(originalBudgetMs,parentRemainingBeforeMs),cleanupReserveMs:rawCleanupReserveMs};
}

export async function runCommonHidRuntime({repo,out,mainFile=path.join(repo,'native/main.cpp'),headerRoot=path.join(repo,'native'),prepareOnly=false,runtimeCase='all',inheritedDeadlineEpochMs}) {
 const started=Date.now(),run=path.join(out,new Date(started).toISOString().replace(/[:.]/g,'-')+'-'+crypto.randomBytes(3).toString('hex'));
 fs.mkdirSync(run,{recursive:true});
 const write=(name,value)=>fs.writeFileSync(path.join(run,name),typeof value==='string'?value:JSON.stringify(value,null,2)+'\n',{flag:'wx'});
 const finish=value=>{write(value.passed===false?'FAILURE.json':prepareOnly?'PREPARED.json':'RESULT.json',value);fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'LATEST.json'),JSON.stringify(value,null,2)+'\n');return value;};
 let phase='source-freeze';const executions=[],sources=[],generated=[],bindings=[],adaptations=[];
 try {
  assert(commonHidRuntimeCases.includes(runtimeCase),'UNKNOWN_COMMON_HID_RUNTIME_CASE');
  const win5=runtimeCase===win5KeptWakeCase,inputs=commonHidRuntimeInputs(runtimeCase);
  if(win5&&inheritedDeadlineEpochMs!==undefined){phase='inherited-budget';commonHidProducerBudget(started,Date.now(),'compile',inheritedDeadlineEpochMs);phase='source-freeze';}
  const runtimeArgs=[win5?win5Selector:runtimeCase==='source-binding'?'--source-binding':runtimeCase==='off-fse'?'--off-fse':runtimeCase.startsWith('inspection-early')?'--'+runtimeCase:runtimeCase==='observation'?'--observation-only':runtimeCase==='business'?'--business-only':'--inspect-only'];
  const here=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
  const templateRoot=path.join(here,'fixtures/common_hid_runtime'),inc=path.join(run,'source');fs.mkdirSync(inc);
  const selected=name=>fs.existsSync(path.join(headerRoot,name))?path.join(headerRoot,name):path.join(repo,'native',name);
  const sourceBytes=new Map();
  const take=(label,file)=>{const real=fs.realpathSync(file),b=fs.readFileSync(real);sourceBytes.set(label,b);sources.push({name:label,path:real,bytes:b.length,sha256:sha(b)});return b.toString('utf8');};
  for(const name of inputs.sources)take(name,name==='native/main.cpp'?mainFile:name.startsWith('tools/fixtures/common_hid_runtime/')?path.join(templateRoot,path.basename(name)):name==='tools/lib/common_hid_runtime_fixture.mjs'?fileURLToPath(import.meta.url):path.join(repo,name));
  let fixture=path.join(inc,'fixture.cpp');
  if(win5){
   for(const name of inputs.headers)take('native/'+name,path.join(headerRoot,name));
   fixture=prepareWin5KeptWake(inc,sourceBytes,generated,bindings);
   write('SOURCE-BINDING.json',{case:runtimeCase,headerRoot,sources,bindings,generated,mainFileConsumed:false,
    boundary:'Original isolation-reader selector; actual shared Dispatch and extracted reader/Raw fences; power, Host permit and HID I/O are simulated.'});
  }else{
  const main=sourceBytes.get('native/main.cpp').toString('utf8').replace(/\r\n/g,'\n');
  const receipt=extract(main,'struct','CommonControllerHidRead','native/main.cpp',bindings);
  const snapshot=extract(main,'struct','InputCaptureGamepadSnapshot','native/main.cpp',bindings);
  const registration=extract(main,'function','gamepadRawInputRegistrationEvidence','native/main.cpp',bindings);
  for(const name of commonHidRuntimeProjection.headers) {
   const original=take('native/'+name,selected(name));let content=original;
   if(name==='controller_hid_source_runtime.h'&&runtimeCase!=='source-binding') {
    // One exact OS ancestry boundary only; inspect and all other product bodies remain intact.
    const before='!sameXusbSource(path.data(),slotPath)',after='!commonHidFakeSameXusbSource(path.data(),slotPath)';
    assert.equal(content.split(before).length,2,'UNIQUE_INSPECT_SOURCE_BINDING_CALL_REQUIRED');
    content=content.replace(before,after);assert.equal(content.split(after).length,2);
    assert.equal(content.replace(after,before),original,'WHOLE_RUNTIME_REVERSE_ADAPTATION');
    adaptations.push({file:'native/'+name,before,after,originalSha256:sha(original),generatedSha256:sha(content),wholeFileInverseExact:true});
   }
   fs.writeFileSync(path.join(inc,name),content,{flag:'wx'});generated.push(pin(path.join(inc,name)));
  }
  const token=(text,key,value)=>{assert.equal(text.split(key).length,2,'UNIQUE_TEMPLATE_TOKEN '+key);return text.replace(key,()=>value);};
  let tu=sourceBytes.get('tools/fixtures/common_hid_runtime/runtime_fixture.cpp.in').toString('utf8');
  tu=token(tu,'@@JSON_HEADER@@',norm(path.join(repo,'deps/json/json.hpp')));
  tu=token(tu,'@@ACTUAL_SNAPSHOT@@',snapshot);
  tu=token(tu,'@@ACTUAL_REGISTRATION_EVIDENCE@@',registration);
  let cases=sourceBytes.get('tools/fixtures/common_hid_runtime/common_hid_runtime_cases.inc.in').toString('utf8');
  cases=token(cases,'@@ACTUAL_READ_RECEIPT@@',receipt);
  for(const [name,content] of [['fixture.cpp',tu],['common_hid_runtime_cases.inc',cases],
   ...['common_hid_os_leaves.inc','inspect_cases.inc'].map(n=>[n,sourceBytes.get('tools/fixtures/common_hid_runtime/'+n)])]) {
   fs.writeFileSync(path.join(inc,name),content,{flag:'wx'});generated.push(pin(path.join(inc,name)));
  }
  write('SOURCE-BINDING.json',{...commonHidRuntimeProjection,mainFile,headerRoot,sources,bindings,adaptations,generated,
   typedAbi:'CommonControllerHidRead and InputCaptureGamepadSnapshot extracted verbatim from actual main; Sample/Frame/Device come from actual current headers; SDK HIDP structs are unmodified.',
   osBoundary:runtimeCase==='source-binding'?'RawInput/HidP/CFG and clock leaves are typed memory ports; actual sameXusbSource, interfaceNode, propertyString and inspect bodies are unchanged.':'RawInput/HidP/CFG and clock leaves fake; only inspect sameXusbSource call gets an exact reversible generated adapter.',
   actualSourceBindingFunction:runtimeCase==='source-binding',bindingBooleanSubstituted:runtimeCase!=='source-binding',
   legacy211Executed:false,stageObserverInterfaces:'Strings supplied by parent; observer reason/period controls await actual header readback.'});
  }
  if(win5||runtimeCase==='source-binding')assert(generated.every(row=>row.path.length<=259),'MSVC_GENERATED_INCLUDE_PATH_TOO_LONG_USE_SHORTER_EVIDENCE_ROOT');
  if(prepareOnly)return finish({status:'PREPARED_NOT_COMPILED_NOT_EXECUTED',run,...commonHidRuntimeProjection,bindings,generated,actualCompiles:0,actualExecutions:0,noDeviceIo:true,sourceReady:false,elapsedMs:Date.now()-started});

  phase='tool-closure';
  if(win5&&inheritedDeadlineEpochMs!==undefined)commonHidProducerBudget(started,Date.now(),'compile',inheritedDeadlineEpochMs);
  const native=readJson(path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'));
  const powershell=process.env.YMCC_SIM_RAW_PROGRAM_PWSH??'G:/YeManCC-Work/Environment/Toolchains/PowerShell/7.4.6/pwsh.exe';
  const launcher=rawProgramLauncherDeclaration({...process.env,YMCC_SIM_RAW_PROGRAM_PWSH:powershell});
  const vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
  const command=path.join(process.env.SystemRoot??'C:/Windows','System32/cmd.exe');
  const toolFiles=[process.execPath,command,vc,...launcher.files,...native.files.map(r=>r.path)];
  const tools=[...new Map(toolFiles.map(p=>[path.resolve(p).toLowerCase(),pin(p)])).values()];
  write('TOOL-CLOSURE.json',tools);
  const ownerTimeout=win5&&inheritedDeadlineEpochMs!==undefined?Math.min(5000,commonHidProducerBudget(started,Date.now(),'compile',inheritedDeadlineEpochMs).parentRemainingBeforeMs-rawCleanupReserveMs):5000;
  const owner=spawnSync(powershell,['-NoLogo','-NoProfile','-NonInteractive','-Command','([Diagnostics.Process]::GetProcessById('+process.pid+')).StartTime.ToUniversalTime().Ticks.ToString()'],{windowsHide:true,encoding:'utf8',timeout:ownerTimeout});
  write('OWNER-IDENTITY-PROCESS.json',{program:pin(powershell),arguments:['parent process start ticks'],exitCode:owner.status,stdout:owner.stdout,stderr:owner.stderr,ownedJob:false,scope:'Read-only launcher identity query; no business execution'});
  assert.equal(owner.status,0);const ownerStartTicks=owner.stdout.trim();assert(/^\d{17,20}$/.test(ownerStartTicks));
  const frozen=[...sources,...generated].map(({path,sha256})=>({path,sha256}));
  const unique=rows=>[...new Map(rows.map(row=>[path.resolve(row.path).toLowerCase(),row])).values()];
  const invoke=async(id,program,args,cwd,extraSources=[],extraTools=[])=>{
   const allocatedAtEpochMs=Date.now();
   const budget=commonHidProducerBudget(started,allocatedAtEpochMs,id,win5?inheritedDeadlineEpochMs:undefined);
   const producer=startOwnedRawProducer({id,program,args,cwd,env:process.env,host:path.join(repo,'tools/ymcc_sim/global/host-process.ps1'),powershell,ownerStartTicks,
    directory:path.join(run,id+'-owned'),stdoutPath:path.join(run,id+'.stdout.log'),stderrPath:path.join(run,id+'.stderr.log'),
    allocatedAtEpochMs,parentStartedAtEpochMs:started,...budget,
    sourceInputs:unique([...frozen,...extraSources]),toolArtifacts:unique([...tools,...extraTools]),
    toolCleanupPolicy:id==='compile'?{kind:'MSVC_VCTIP_AFTER_ROOT_EXIT',maximumProcesses:1,program:pin(path.join(path.dirname(native.native.compiler),'vctip.exe'))}:null});
   const proof=await producer.wait();executions.push(proof);write(id+'-PROCESS.json',proof);
   assert.equal(proof.rawExecution?.job?.activeProcessesFinal,0,'JOB_ZERO_REQUIRED');
   assert(id==='compile'?['NATURAL','DECLARED_TOOL_FINALIZATION'].includes(proof.rawExecution?.completion?.kind):proof.rawExecution?.completion?.kind==='NATURAL','QUALIFIED_COMPLETION_REQUIRED');
   assert.equal(proof.error,null,'OWNED_PROCESS_COLLECTION_FAILED');assert.equal(proof.exitCode,0,'OWNED_PROCESS_FAILED '+id);
   return proof;
  };
  phase='compile';
  const build=path.join(run,'build');fs.mkdirSync(build);const exe=path.join(build,'fixture.exe'),batch=path.join(build,'build.cmd');
  const win5Flags=String.raw`/DYMCC_ISOLATION_HID_OBSERVE_INCLUDE=\"observe.inc\" /DYMCC_ISOLATION_HID_RAW_FENCE_INCLUDE=\"raw-fence.inc\" `;
  const compileCommand=calibrationToolchainPreamble(vc)+'set VSLANG=1033\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 '+(win5?'/showIncludes '+win5Flags:'/WX /permissive- /sdl /O2 /MT /DNDEBUG /showIncludes '+(runtimeCase==='source-binding'?'/DYMCC_COMMON_HID_SOURCE_BINDING_FIXTURE=1 ':''))+q(fixture)+' /I'+q(inc)+(win5?' /I'+q(path.join(inc,'deps/json')):'')+' /Fo'+q(path.join(build,'fixture.obj'))+' /Fe'+q(exe)+' /link '+(win5?'/INCREMENTAL:NO ':'')+'/VERBOSE:LIB\r\nexit /b %errorlevel%\r\n';
  fs.writeFileSync(batch,compileCommand,{flag:'wx'});write('COMMAND-CLOSURE.json',{compile:{program:pin(command),arguments:['/d','/c',batch],batch:pin(batch)},run:{arguments:runtimeArgs}});
  await invoke('compile',command,['/d','/c',batch],build,[pin(batch)]);
  phase='actual-include-closure';
  const buildOutput=fs.readFileSync(path.join(run,'compile.stdout.log'),'utf8'),dependencies=new Map(),libraries=new Map();
  for(const line of buildOutput.split(/\r?\n/)) {
   const match=line.match(/([A-Za-z]:[\\/].+?)(?:\([^)]*\))?\s*$/);if(!match)continue;
   const file=match[1].trim().replace(/:$/,'');if(!fs.existsSync(file)||!fs.statSync(file).isFile())continue;
   (file.toLowerCase().endsWith('.lib')?libraries:dependencies).set(path.resolve(file).toLowerCase(),pin(file));
  }
  assert(dependencies.size>20,'ACTUAL_INCLUDE_GRAPH_REQUIRED');
  if(win5)for(const name of ['observe.inc','raw-fence.inc'])assert(dependencies.has(path.join(inc,name).toLowerCase()),'WIN5_REQUIRED_COMPILED_INCLUDE '+name);
  write('ACTUAL-COMPILER-GRAPH.json',{sources:[pin(fixture),...dependencies.values()],libraries:[...libraries.values()],objects:[pin(path.join(build,'fixture.obj'))],executable:pin(exe)});
  // Add every actual compiler input to runtime admission, preserving its exact bytes.
  for(const row of [...dependencies.values(),...libraries.values()])assert.equal(pin(row.path).sha256,row.sha256);
  phase='execute-new-projection';
  await invoke('runtime',exe,runtimeArgs,build,[...dependencies.values(),...libraries.values()],[pin(exe)]);
  const raw=JSON.parse(fs.readFileSync(path.join(run,'runtime.stdout.log'),'utf8'));
  if(win5){
   assert.equal(raw.selectedCase,win5Selector,'WIN5_EXACT_ORIGINAL_SELECTOR');assert.equal(raw.physicalInput,false);
   assert(Number.isSafeInteger(raw.checks)&&raw.checks>=87,'WIN5_ORIGINAL_ASSERTIONS_REQUIRED');
   phase='final-source-readback';const readback=sources.map(row=>({...row,current:pin(row.path)}));write('FINAL-SOURCE-READBACK.json',readback);
   for(const row of readback)assert.equal(row.current.sha256,row.sha256,'PROJECTION_INPUT_CHANGED '+row.name);
   return finish({status:'PASS_SELECTED_SCOPE',passed:true,run,originalCanonicalId:commonHidRuntimeProjection.originalCanonicalId,projection:commonHidRuntimeProjection.argument,
    cases:[win5KeptWakeCase],results:{Release:{passed:true,cases:1,assertions:raw.checks}},actualCompiles:1,actualExecutions:1,
    legacy211Executed:false,defaultParentExecuted:false,noDeviceIo:true,wholeRuntimeAcceptance:false,inheritedDeadlineEpochMs:inheritedDeadlineEpochMs??null,elapsedMs:Date.now()-started});
  }
  assert.equal(raw.projection,'common-hid-runtime');assert.equal(raw.originalCanonicalId,commonHidRuntimeProjection.originalCanonicalId);
  assert.equal(raw.legacy211Executed,false);assert.equal(raw.physicalInput,false);
  const expectedAll=['win5-15-inspect-capture-read','rog16-inspect-capture-read',
   ...['device-info-read','not-gamepad-collection','device-name-size','device-name-read','source-binding-unproven','xinput-capabilities-unavailable','xinput-subtype-unsupported','preparsed-size','preparsed-read','hid-caps-read','hid-caps-bounds','button-caps-read','value-caps-read','descriptor-unsupported','data-capacity'].map(s=>'inspection/'+s),
   'inspection/domain-off','inspection/diagnostic-throw','observation/1s-change-10s-wait','observation/steady-selected-unallowed',
   'observation/throw-last-error','observation/domain-off-api-count','inspection/2s-retry-10s-diagnostic'];
  const expectedBinding=['same-devnode-no-xusb','case-insensitive-full-id','same-node-different-id','different-node-same-id','same-vidpid-different-node-id','shared-hub-is-not-source',
   ...['selected','raw'].flatMap(side=>[side+'-interface-query-failed',side+'-locate-failed',side+'-empty-id',side+'-property-type-invalid']),
   'original-xusb-ancestor','original-alias-xusb-ancestor','xusb-other-root-common-hub','xusb-device-id-query-failed','xusb-raw-parent-query-failed','unknown-interface',
   'post-binding/xinput-capabilities-unavailable','post-binding/descriptor-unsupported','rog16-inspect-capture-read','current-identity-unknown','stale-power-generation'].map(name=>'source-binding/'+name);
  const expected=runtimeCase==='source-binding'?expectedBinding:runtimeCase==='off-fse'?["off-fse/rog16-press-release", "off-fse/target-toggle-held-no-replay", "off-fse/refused/external-priority", "off-fse/refused/physical-return", "off-fse/refused/not-fse", "off-fse/refused/capture-disabled", "off-fse/refused/unknown-identity", "off-fse/registration-retires-batch", "off-fse/parser-rejection-observed", "off-fse/domain-off-original-api-order", "off-fse/win5-refresh-gap-and-fresh-rebind"]:runtimeCase.startsWith('inspection-early')?expectedAll.slice(2,10).map(s=>s.replace('inspection/','early-inspection/')):runtimeCase==='observation'?expectedAll.slice(-5):runtimeCase==='business'?expectedAll.slice(0,17):expectedAll;
  assert.deepEqual(raw.cases,expected,'EXACT_NEW_PROJECTION_SET');
  phase='final-source-readback';
  const readback=sources.map(row=>({...row,current:pin(row.path)}));write('FINAL-SOURCE-READBACK.json',readback);
  const changed=readback.filter(row=>row.current.sha256!==row.sha256);
  // main is shared: the exact two bound definitions, rather than unrelated edits, govern this projection.
  for(const row of changed) {
   assert.equal(row.name,'native/main.cpp','PROJECTION_INPUT_CHANGED '+row.name);
   const live=fs.readFileSync(mainFile,'utf8').replace(/\r\n/g,'\n');
   for(const binding of bindings)assert.equal(sha(extract(live,binding.kind,binding.symbol,binding.file,[])),binding.sha256,'BOUND_MAIN_ABI_CHANGED');
  }
  return finish({status:'PASS_SELECTED_SCOPE',passed:true,run,originalCanonicalId:commonHidRuntimeProjection.originalCanonicalId,projection:commonHidRuntimeProjection.argument,
   results:{Release:{passed:true,cases:raw.cases.length,assertions:raw.checks}},cases:raw.cases,executions:executions.map(p=>({id:p.id,exitCode:p.exitCode,natural:p.rawExecution.completion.kind,activeProcessesFinal:p.rawExecution.job.activeProcessesFinal})),
   actualCompiles:1,actualExecutions:1,legacy211Executed:false,defaultParentExecuted:false,noDeviceIo:true,wholeRuntimeAcceptance:false,mainUnrelatedChanges:changed,elapsedMs:Date.now()-started});
 } catch(error) {
  const result=finish({status:'FAIL',passed:false,run,phase,error:String(error.stack??error),executions,elapsedMs:Date.now()-started,noDeviceIo:true,defaultParentExecuted:false});
  process.exitCode=1;return result;
 }
}
