import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {extract} from '../../../fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const moduleDir=path.dirname(fileURLToPath(import.meta.url)),repo=path.resolve(moduleDir,'../../../..');
const args=process.argv.slice(2),outputArgument=args.shift();let selectedVariants=null;
if(args.length){if(args.length!==2||args[0]!=='--variants')throw Error('Expected evidence directory and optional --variants name[,name]');selectedVariants=args[1].split(',');if(selectedVariants.some(v=>!v)||new Set(selectedVariants).size!==selectedVariants.length)throw Error('Empty or duplicate variant selection');}
const out=path.resolve(outputArgument||path.join(repo,'../../../Testing/YMCC/Evidence/SensorPowerConsumer',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid));
fs.mkdirSync(out,{recursive:true});
if(selectedVariants?.includes('gyro-cadence')) {
 if(selectedVariants.length!==1)throw Error('gyro-cadence is an independent targeted consumer selection');
 const {runCadenceConsumer}=await import('./cadence.mjs');runCadenceConsumer({repo,out});process.exit(0);
}

const sha=b=>createHash('sha256').update(b).digest('hex');
const sourceBytes=fs.readFileSync(path.join(repo,'native/main.cpp')),source=sourceBytes.toString('utf8');
const symbols=['inputCaptureSensorPowerMayRun','inputCaptureSensorStopRequested','inputCaptureConfigureVirtualGyroWake','inputCaptureVirtualGyroHostMayWake','inputCaptureUnsubscribeWinRtSensorsImpl','inputCaptureStopWinRtSensorsImpl','inputCaptureResubscribeWinRtSensorsImpl','inputCaptureNoteVirtualGyroReady','inputCaptureVirtualGyroWakeBusy','inputCaptureVirtualGyroWakeStep','inputCaptureSignalSensorPowerPause','inputCaptureRediscoverMissingWindowsSensors','inputCaptureSensorThreadProc','inputCaptureSensorThreadPostPower','inputCaptureSuspendSensorsForPower','inputCaptureResumeSensorsForPower','inputCaptureSessionDisplayPowerChanged','suspendResumeSubscriptionCallback','inputHostNoteVirtualGyroAcceptedLocked'];
const bindings=symbols.map(s=>extract(source,s));
const fixture=fs.readFileSync(path.join(moduleDir,'fixture.cpp.in'),'utf8').replace(/^\uFEFF/,'');
function once(text,before,after,label){assert.equal(text.split(before).length,2,'Unique source anchor: '+label);return text.replace(before,()=>after);}
let template=once(fixture,'// PRODUCTION_FUNCTIONS',bindings.map(x=>x.code).join('\n\n'),'production functions');
const frameBinding=extract(source,'InputFrameSubmission','struct');
const readCurrentSource=fs.readFileSync(path.join(repo,'native/virtual_switch_verification_runtime.h'),'utf8');
const readCurrentBinding=extract(readCurrentSource,'readCurrent');
template=once(template,'// PRODUCTION_FRAME_TYPE',frameBinding.code,'actual frame type');
template=once(template,'// PRODUCTION_READ_CURRENT',readCurrentBinding.code,'actual source-read validator');
const header=fs.readFileSync(path.join(repo,'native/sensor_virtual_link_wake.h'),'utf8');
const originalPower=fixture.slice(fixture.indexOf('// ORIGINAL_POWER_CASES_BEGIN'),fixture.indexOf('// ORIGINAL_POWER_CASES_END'));
const powerNames=[...originalPower.matchAll(/check\("([^"]+)"/g)].map(m=>m[1]);
assert.equal(powerNames.length,31,'Retain all 31 original assertions');assert.equal(new Set(powerNames).size,31);
const virtualScenarios=[...fixture.matchAll(/beginVirtualCase\("([^"]+)",selected\)/g)].map(m=>m[1]);
assert(virtualScenarios.length>0&&new Set(virtualScenarios).size===virtualScenarios.length,'Unique explicit virtual scenarios');
const actor=bindings.find(b=>b.symbol==='inputCaptureSensorThreadProc').code;
assert(actor.includes('g_virtualGyroWake.waitMs(')&&actor.includes('inputCaptureVirtualGyroWakeStep();'),'Actual STA wait and idle step required');
assert(bindings.find(b=>b.symbol==='inputCaptureUnsubscribeWinRtSensorsImpl').code.includes('g_virtualGyroWake.cancelPending();'),'Actual unsubscribe cancellation required');
fs.writeFileSync(path.join(out,'main.snapshot.cpp'),sourceBytes);fs.writeFileSync(path.join(out,'sensor_virtual_link_wake.source.h'),header);fs.writeFileSync(path.join(out,'virtual_switch_verification_runtime.source.h'),readCurrentSource);
const files=['native/main.cpp','native/sensor_virtual_link_wake.h','native/input_frame_submission.h','native/input_host_recovery_health.h','native/virtual_output_status.h','native/virtual_switch_verification_runtime.h','native/sensor_thread_mailbox.h','native/sensor_device_change.h','native/sensor_inventory_cache.h','native/sensor_inventory.h','native/windows_motion_submission.h','native/sleep_notification_evidence.h','deps/json/json.hpp','tools/ymcc_sim/global/sensor-power-consumer/fixture.cpp.in','tools/ymcc_sim/global/sensor-power-consumer/run.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
const frozenRaw=new Map([['native/main.cpp',sourceBytes],['native/sensor_virtual_link_wake.h',Buffer.from(header)],['native/virtual_switch_verification_runtime.h',Buffer.from(readCurrentSource)]]);
const sourceHashes=files.map(p=>({path:p,sha256:sha(frozenRaw.get(p)||fs.readFileSync(path.join(repo,p)))}));
const result={schemaVersion:1,ok:false,status:'RUNNING',sourceHashes,bindings:[...bindings.map(b=>({...b,file:'native/main.cpp'})),{...frameBinding,file:'native/main.cpp'},{...readCurrentBinding,file:'native/virtual_switch_verification_runtime.h'}].map(({code,...b})=>b),runs:[],hardwareIo:false,normalYmccStarted:false,
 scope:{originalPowerAssertions:31,virtualScenarios,actualConfigureNoteBusyStep:true,actualStaWait:true,configurationPorts:['effective enable argument','game override owner admission'],actualHostMayWake:true,actualAcceptedSourceAck:true,hostSnapshotPorts:['frame Current snapshot','health Context snapshot','latest Source snapshot'],unusedFrameTypePorts:['touchhaptic SourceToken','game_feedback Sample','opaque legion_motion Sample'],clock:'fixed only in direct method cases; real Windows clock in STA deadline case',hostAcceptanceBoundary:'Actual accepted-frame ACK helper, frame/source decisions and lineage validator execute with captured configuration; Host IPC, source snapshot collection and successful-start callers are not executed',sensorCache:'explicit memory samples and generation counters; not physical sensor freshness',newThreads:'one private STA in the deadline case; no new production thread'}};
const save=()=>fs.writeFileSync(path.join(out,'consumer-result.json'),JSON.stringify(result,null,2)+'\n');save();
try {
 const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 // Existing faults keep exactly the original 31 assertions. New mutations run
 // only their named virtual case, without repeating the real-time STA case.
 const modes=[
  {name:'current',expectedPass:true,scenarios:[...virtualScenarios,'power']},
  {name:'missing-early-power-pause',args:['--power-only'],scenarios:['power'],requiredFailure:'OS callback revokes admission before GUI exists',code:s=>once(s,'if (type == PBT_APMSUSPEND) inputCaptureSignalSensorPowerPause();','if (type == PBT_APMSUSPEND) {}','early power pause')},
  {name:'missing-display-off-pause',args:['--power-only'],scenarios:['power'],requiredFailure:'display OFF immediately revokes binding',code:s=>once(s,'if (state == 0) inputCaptureSuspendSensorsForPower();','if (state == 0) {}','display OFF pause')},
  {name:'resume-while-off',args:['--power-only'],scenarios:['power'],requiredFailure:'automatic resume while display OFF stays paused',code:s=>once(s,'display != 0 && (phase == PowerLifecycle::Ready','true && (phase == PowerLifecycle::Ready','resume admission')},
  {name:'virtual-ignore-link-switch',args:['--case','disabled'],scenarios:['disabled'],assertions:3,requiredFailure:'virtual link OFF rejects the ready notification',header:s=>once(s,'enabled_.store(enabled,std::memory_order_release);','enabled_.store(true,std::memory_order_release);','ignore effective link switch')},
  {name:'virtual-ignore-old-configuration',args:['--case','reconfigured'],scenarios:['reconfigured'],assertions:3,requiredFailure:'virtual link reconfiguration invalidates the pending old configuration',header:s=>once(s,'if(context_==context && enabled_.load(std::memory_order_relaxed)==enabled)return;','if(pending_ && context_!=context)return;\n        if(context_==context && enabled_.load(std::memory_order_relaxed)==enabled)return;','ignore configuration replacement')},
  {name:'virtual-refresh-healthy',args:['--case','healthy'],scenarios:['healthy'],assertions:4,requiredFailure:'virtual link healthy pair never resubscribes',code:s=>once(s,'if(fresh){log(ticket->verification?"samples-resumed":"already-streaming",true);return;}','if(false && fresh){log(ticket->verification?"samples-resumed":"already-streaming",true);return;}','refresh an already healthy source')},
 ];
 const requestedModes=selectedVariants?selectedVariants.map(name=>{const mode=modes.find(m=>m.name===name);if(!mode)throw Error('Unknown variant: '+name);return mode;}):modes;
 result.selectedVariants=requestedModes.map(m=>m.name);result.completeOriginalConsumer=requestedModes.length===modes.length;save();
 for(const mode of requestedModes){
  const {name}=mode,dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});
  const code=mode.code?mode.code(template):template,compiledHeader=mode.header?mode.header(header):header;
  if(name!=='current')assert(code!==template||compiledHeader!==header,'Fault must change a real compiled production input: '+name);
  fs.writeFileSync(path.join(dir,'fixture.cpp'),code);fs.writeFileSync(path.join(dir,'sensor_virtual_link_wake.h'),compiledHeader);
  const row={name,expectedPass:mode.expectedPass===true,args:mode.args||[],expectedScenarios:mode.scenarios,requiredFailure:mode.requiredFailure,compiledSourceSha256:sha(code),compiledHeaderSha256:sha(compiledHeader),passed:false};result.runs.push(row);save();
  const batch=calibrationToolchainPreamble(vcvars)+`cd /d "${dir}"\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O1 /DUNICODE /D_UNICODE fixture.cpp /I"${path.join(repo,'deps/json')}" /I"${path.join(repo,'native')}" /Fe:fixture.exe /link user32.lib ole32.lib >compile.log 2>&1\r\nexit /b %errorlevel%\r\n`;
  fs.writeFileSync(path.join(dir,'compile.cmd'),batch);
  let started=Date.now();const c=spawnSync('cmd.exe',['/d','/c',path.join(dir,'compile.cmd')],{encoding:'utf8',windowsHide:true,timeout:120000});
  row.build={elapsedMs:Date.now()-started,timeoutMs:120000,exitCode:c.status,signal:c.signal,spawnError:c.error?String(c.error):null};save();
  if(c.status!==0||c.error)throw Error(name+' compile failed: '+(c.error||'')+'\n'+fs.readFileSync(path.join(dir,'compile.log'),'utf8').slice(-6000));
  started=Date.now();const run=spawnSync(path.join(dir,'fixture.exe'),row.args,{encoding:'utf8',windowsHide:true,timeout:30000});
  row.execution={elapsedMs:Date.now()-started,timeoutMs:30000,exitCode:run.status,signal:run.signal,spawnError:run.error?String(run.error):null};
  fs.writeFileSync(path.join(dir,'stdout.json'),run.stdout||'');fs.writeFileSync(path.join(dir,'stderr.txt'),run.stderr||'');save();
  if(run.error||![0,1].includes(run.status))throw Error(name+' execution failed: '+(run.error||run.status));
  const report=JSON.parse((run.stdout||'').trim());row.report=report;row.exitCode=run.status;row.exeSha256=sha(fs.readFileSync(path.join(dir,'fixture.exe')));save();
  assert.deepEqual(report.executedScenarios,mode.scenarios,'Exact executed scenario set: '+name);
  assert(report.cases.length>0&&report.cases.every(c=>mode.scenarios.includes(c.scenario)),'No undeclared scenario assertions');
  assert.equal(new Set(report.cases.map(c=>c.name)).size,report.cases.length,'Unique actual assertion names');
  if(mode.scenarios.includes('power'))assert.deepEqual(report.cases.filter(c=>c.scenario==='power').map(c=>c.name),powerNames,'Exact original 31 assertion set');
  for(const scenario of mode.scenarios)assert(report.cases.some(c=>c.scenario===scenario),'Selected scenario must execute assertions: '+scenario);
  if(mode.assertions!==undefined)assert.equal(report.cases.length,mode.assertions,'Exact targeted mutation assertion count');
  row.passed=row.expectedPass?run.status===0&&report.ok===true&&report.cases.every(c=>c.passed===true):run.status===1&&report.ok===false&&report.cases.some(c=>c.name===mode.requiredFailure&&c.passed===false);
  save();console.log(name,row.passed?'PASS':'FAIL',report.cases.length,'checks',report.cases.filter(c=>!c.passed).map(c=>c.name));
  if(!row.passed)throw Error('Unexpected behavior: '+name);
 }
 result.sourceCurrentness=sourceHashes.map(x=>({...x,currentSha256:sha(fs.readFileSync(path.join(repo,x.path)))}));
 result.sourceDrift=result.sourceCurrentness.filter(x=>x.sha256!==x.currentSha256);result.wholeMainUnchanged=!result.sourceDrift.some(x=>x.path==='native/main.cpp');save();
 if(result.sourceDrift.length)throw Error('Source changed during test: '+result.sourceDrift.map(x=>x.path).join(', '));
 result.ok=true;result.status=result.completeOriginalConsumer?'PASS':'TARGETED_PASS';save();
}catch(e){result.error=e.stack;result.status='FAIL';save();console.error(e.stack);process.exitCode=1;}
