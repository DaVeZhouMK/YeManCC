import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {extract,sha} from '../../../fixtures/msi_owned_return_retired_io/source-binding.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';

export function prepareSensorPowerSource(main,template,providerHeader){
 const bindings=[],take=name=>{
  if(!['inputCaptureSensorThreadPostPower','inputCaptureResumeSensorsForPower'].includes(name))return extract(main,'function',name,'native/main.cpp',bindings);
  const declaration=new RegExp('^static (?:bool|void) '+name+'\\([^;{}]*?initiatingPower=','gm');
  const hits=[...main.matchAll(declaration)];
  assert.equal(hits.length,1,name+': exact default argument declaration');
  const at=hits[0].index+hits[0][0].length;
  assert.equal(main.slice(at,at+4),'{}) ');
  const view=main.slice(0,at)+'  '+main.slice(at+2);
  const code=extract(view,'function',name,'native/main.cpp',bindings);
  const relativeAt=at-hits[0].index;
  assert(relativeAt>0&&relativeAt<code.length&&code.slice(relativeAt,relativeAt+2)==='  ');
  const original=code.slice(0,relativeAt)+'{}'+code.slice(relativeAt+2);
  assert.equal(original,main.slice(hits[0].index,hits[0].index+original.length));
  Object.assign(bindings.at(-1),{sha256:sha(original),parseOnlyDefaultArgumentMask:true,bodyBytePreserved:true});
  return original;
 };
 const line=anchor=>{const rows=main.split('\n').filter(row=>row.startsWith(anchor));assert.equal(rows.length,1,'Unique sensor declaration '+anchor);return rows[0];};
 const power=main.match(/^enum class PowerLifecycle\s*:\s*uint8_t\s*\{[^\n]+\};/m);assert(power);
 const timerAt=main.indexOf('if(w==kSleepPowerRedeliveryTimer){');assert(timerAt>=0&&timerAt===main.lastIndexOf('if(w==kSleepPowerRedeliveryTimer){'));
 const timerSignature='static LRESULT fixtureActualTimer() ',timerTail=main.slice(timerAt);
 const timerBody=extract(timerSignature+timerTail.slice(timerTail.indexOf('{')),'function','fixtureActualTimer','native/main.cpp',[]).slice(timerSignature.length);
 bindings.push({symbol:'sleep-power-redelivery-timer',kind:'case',file:'native/main.cpp',beginUtf8Byte:Buffer.byteLength(main.slice(0,timerAt)),sha256:sha(timerBody),bodyBytePreserved:true});
 const candidate=main.includes('static void inputCaptureSensorThreadRetryPowerWake(');
 const proactive=main.includes('static void inputCaptureSensorThreadRecoverReturned('),structure=name=>extract(main,'struct',name,'native/main.cpp',bindings);
 const providerStopped=extract(providerHeader,'method','stopped','native/msi_legacy_sensor.h',bindings);
 const recoveryGlobals=proactive?['SensorThreadOwnerStart','SensorThreadRecoveryRequest','SensorThreadRecoveryBudget'].map(structure).join('\n')+'\n'+['static std::atomic<unsigned long long> g_sensorThreadOwnerSerial','static std::atomic<unsigned long long> g_sensorThreadRecoveryPendingSerial','static std::atomic<std::shared_ptr<const SensorThreadRecoveryRequest>> g_sensorThreadRecoveryRequest','static SensorThreadRecoveryBudget g_sensorThreadRecoveryBudget;'].map(line).join('\n'):'';
 const wndAt=main.indexOf('static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {');
 const ingressAt=proactive?main.indexOf('    if(g_sensorThreadRecoveryPendingSerial.load(std::memory_order_acquire) &&',wndAt):-1;
 const ingressEnd=proactive?main.indexOf('inputCaptureSensorThreadRecoverReturned(h);',ingressAt)+'inputCaptureSensorThreadRecoverReturned(h);'.length:-1;
 assert(!proactive||(wndAt>=0&&ingressAt>wndAt&&ingressEnd>ingressAt),'Original sensor window ingress');
 const wndPrefix=proactive?main.slice(ingressAt,ingressEnd):'';
 if(proactive)bindings.push({symbol:'sensor-window-recovery-ingress',kind:'caller-fragment',file:'native/main.cpp',beginUtf8Byte:Buffer.byteLength(main.slice(0,ingressAt)),endUtf8ByteExclusive:Buffer.byteLength(main.slice(0,ingressEnd)),sha256:sha(wndPrefix),bodyBytePreserved:true});
 const live=main.includes('static bool inputCaptureSensorResumeScopeCurrent(');
 const publicScope=main.includes('static bool inputCaptureStartSensorsForCurrentPower(');
 // Compile the exact original init ordering as well as the complete current helper.
 // The pinned old source keeps its original Post(Start), then late paused-state store.
 const init=take('inputCaptureInit'),initBegin=init.indexOf('    if(!inputCaptureSensorThreadEnsure())'),
  initEnd=init.indexOf('    msiNativeForegroundUiEvent(true);',initBegin);
 assert(initBegin>=0&&initEnd>initBegin,'Original sensor init caller boundaries');
 const initOrder=init.slice(initBegin,initEnd),initAt=main.indexOf(init)+initBegin;
 assert(initOrder.includes('g_inputCaptureReady = true;'),'Original init readiness publication');
 assert(publicScope?initOrder.includes('inputCaptureStartSensorsForCurrentPower()'):initOrder.includes('inputCaptureSensorThreadPost(SensorThreadRequest::Start)'),'Original current/old Start call');
 bindings.push({symbol:'inputCaptureInit-sensor-start-order',kind:'caller-fragment',file:'native/main.cpp',
  startLine:main.slice(0,initAt).split('\n').length,endLine:main.slice(0,initAt+initOrder.length).split('\n').length,
  startUtf8Byte:Buffer.byteLength(main.slice(0,initAt)),endUtf8ByteExclusive:Buffer.byteLength(main.slice(0,initAt+initOrder.length)),sha256:sha(initOrder),bodyBytePreserved:true});
 const parts={CAPTURE_READY:main.includes('static std::atomic<bool> g_inputCaptureReady{')?line('static std::atomic<bool> g_inputCaptureReady{'):line('static bool g_inputCaptureReady ='),LIVE_COMMAND:live?'1':'0',ACTUAL_RESUBSCRIBE:take('inputCaptureResubscribeWinRtSensorsImpl').replace('static void inputCaptureResubscribeWinRtSensorsImpl()','static void productionInputCaptureResubscribeWinRtSensorsImpl()'),RECOVERY_GLOBALS:recoveryGlobals,RECOVERY_HELPERS:proactive?['inputCaptureSensorThreadRecoveryLog',...(live?['inputCaptureSensorResumeScopeCurrent']:[]),'inputCaptureSensorThreadNoteFailure','inputCaptureSensorThreadResourcesRetained'].map(take).join('\n'):'',RECOVER:proactive?take('inputCaptureSensorThreadRecoverReturned'):'',WINDOW_INGRESS:wndPrefix,SHARED_RECOVER:proactive?'inputCaptureSensorThreadRecoverReturned(h);':'',RECOVERY_PENDING:proactive?'return g_sensorThreadRecoveryPendingSerial.load();':'return 0;',OWNER_SERIAL:proactive?'return g_sensorThreadOwnerSerial.load();':'return threadsCreated.load();',RESET_RECOVERY:proactive?'g_sensorThreadRecoveryRequest.store({});g_sensorThreadRecoveryPendingSerial=0;g_sensorThreadRecoveryBudget={};g_sensorThreadOwnerSerial=0;':'',PROVIDER_STOPPED:providerStopped,POWER_ENUM:power[0],MESSAGES:['#define WM_POWER_RESUME_COMMIT ','#define WM_POWER_INPUT_RECOVER ','static constexpr UINT kSleepPowerForwardMessage=','static constexpr UINT kSharedPowerStateMessage=','static constexpr UINT_PTR kSleepPowerRedeliveryTimer='].map(line).join('\n'),GLOBALS:[line('using SensorThreadRequest ='),line('static std::mutex g_sensorThreadMx;'),line('static ymcc::sensors::ThreadMailbox g_sensorThreadMailbox;'),line('static HANDLE g_sensorThread ='),line('static HANDLE g_sensorThreadWake ='),...(candidate?[line('static ymcc::sensors::ThreadMailbox::Handle g_sensorThreadPowerWakePending;')]:[])].join('\n'),PREDICATES:[...(proactive?['inputCaptureSensorCaptureGeneration']:[]),'inputCaptureSensorPowerMayRun','inputCaptureSensorStopRequested',...(main.includes('static bool inputCaptureSensorPowerWorkWanted() {')?['inputCaptureSensorPowerWorkWanted']:[])].map(take).join('\n'),OWNERS:['inputCaptureSensorThreadProc','inputCaptureSensorThreadPostPower',...(candidate?['inputCaptureSensorThreadRetryPowerWake']:[]),'inputCaptureSensorThreadPost','inputCaptureSensorThreadEnsure','inputCaptureSensorThreadReap','inputCaptureSensorThreadShutdown'].map(take).join('\n'),POWER:['inputCaptureSuspendSensorsForPower','inputCaptureResumeSensorsForPower'].map(take).join('\n'),ARM:take('sleepPowerArmRedelivery'),TIMER:'if(w==kSleepPowerRedeliveryTimer)'+timerBody,DEBT:candidate?'return g_sensorThreadPowerWakePending;':'return {};',CLEAR_DEBT:candidate?'g_sensorThreadPowerWakePending.reset();':'',REPAIR_DEFINED:candidate?'1':'0'};
 parts.SENSOR_ACTIVE_POWER=publicScope?line('static std::atomic<std::shared_ptr<ymcc::inputHost::sharedPower::Scope>> g_sensorActivePower'):
  'static std::atomic<std::shared_ptr<ymcc::inputHost::sharedPower::Scope>> g_sensorActivePower; // Fixture visibility only for the pinned old source.';
 if(main.includes('static bool inputCaptureSensorPowerAdmitted('))parts.PREDICATES=take('inputCaptureSensorPowerAdmitted')+'\n'+parts.PREDICATES;
 if(publicScope)parts.START_CURRENT=take('inputCaptureStartSensorsForCurrentPower');
 else{
  const callAt=initOrder.indexOf('inputCaptureSensorThreadPost(SensorThreadRequest::Start)');
  assert(callAt>=0,'Pinned old init has its actual blocking Start');
  const begin=initOrder.lastIndexOf('\n',callAt)+1,storeAt=initOrder.indexOf('g_inputCaptureSensorsSuspended.store(',callAt),
   end=initOrder.indexOf(';',storeAt)+1;
  assert(storeAt>callAt&&end>storeAt,'Pinned old Start precedes its actual late suspended store');
  const oldStart=initOrder.slice(begin,end),at=initAt+begin;
  bindings.push({symbol:'inputCaptureInit-original-start-and-late-store',kind:'caller-fragment',file:'native/main.cpp',
   startLine:main.slice(0,at).split('\n').length,endLine:main.slice(0,at+oldStart.length).split('\n').length,
   startUtf8Byte:Buffer.byteLength(main.slice(0,at)),endUtf8ByteExclusive:Buffer.byteLength(main.slice(0,at+oldStart.length)),sha256:sha(oldStart),bodyBytePreserved:true});
  parts.START_CURRENT='static bool inputCaptureStartSensorsForCurrentPower(){\n'+oldStart+'\n    return true;\n}';
 }
 parts.INIT_START_ORDER=initOrder;
 let code=template;for(const [name,value]of Object.entries(parts)){const marker='@@'+name+'@@';assert.equal(code.split(marker).length,2,'Unique fixture marker '+name);code=code.replace(marker,()=>value);}assert(!/@@\w+@@/.test(code));
 return {code,bindings,publicScopeAvailable:publicScope,compiledParts:Object.entries(parts).map(([symbol,value])=>({symbol,sha256:sha(value)})),scope:'Byte-preserved complete production STA Proc, mailbox, sender, Ensure/Post/Reap/Shutdown, sensor power admission and Suspend/Resume, and existing real Win32 timer body. Real COM STA, thread/event/window/message queue. Public Scope/RevocationMirrors are production headers; Reader fields are explicit coherent memory ports. Full current Start helper calls original blocking Post, and exact init sensor-start fragment executes (old-source order retained by feature detection). Actual resubscribe callbacks are retained by inert sensor ports. Only provider/metadata/selection/Host/hardware leaves inert;standby-entry handoff is an inactive port and throws if entered. API terminal fault gates do not replace queue algorithms.'};
}
export function runSensorPowerFixture({repo,out,selectedCase}){
 const started=performance.now(),templateText=fs.readFileSync(path.join(repo,'tools/ymcc_sim/global/sensor-power-consumer/power-wake.cpp.in'),'utf8'),caseNames=[...templateText.matchAll(/cases.emplace_back\("([^"]+)"/g)].map(m=>m[1]);assert(caseNames.length>0&&new Set(caseNames).size===caseNames.length,'SENSOR_POWER_UNIQUE_CASES');const suffix=selectedCase?.startsWith('from:'),caseName=suffix?selectedCase.slice(5):selectedCase;assert(selectedCase==null||(typeof selectedCase==='string'&&caseNames.includes(caseName)),'SENSOR_POWER_UNKNOWN_OR_EMPTY_CASE');const selectedCases=selectedCase==null?caseNames:suffix?caseNames.slice(caseNames.indexOf(caseName)):[caseName];assert(!fs.existsSync(out),'SENSOR_POWER_FRESH_OUTPUT_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const save=(name,data)=>fs.writeFileSync(path.join(out,name),typeof data==='string'?data:JSON.stringify(data,null,2)+'\n',{flag:'wx'});
 const publicHeaders=['input_host_shared_power.h','sleep_power_state.h','sleep_power_observation.h','sleep_power_revocation.h'];
 const sourcePaths=[...publicHeaders.map(name=>'native/'+name),'native/msi_legacy_sensor.h','native/main.cpp','native/sensor_thread_mailbox.h','native/sleep_power_dispatch.h','deps/json/json.hpp','tools/ymcc_sim/global/sensor-power-consumer/run.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/ymcc_sim/global/sensor-power-consumer/power-wake.mjs','tools/ymcc_sim/global/sensor-power-consumer/power-wake.cpp.in','tools/fixtures/msi_owned_return_retired_io/source-binding.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
 const bytes=new Map(sourcePaths.map(relative=>[relative,fs.readFileSync(path.join(repo,relative))]));
 const prepared=prepareSensorPowerSource(bytes.get('native/main.cpp').toString('utf8'),bytes.get('tools/ymcc_sim/global/sensor-power-consumer/power-wake.cpp.in').toString('utf8'),bytes.get('native/msi_legacy_sensor.h').toString('utf8'));
 save('fixture.cpp',prepared.code);for(const name of ['sensor_thread_mailbox.h','sleep_power_dispatch.h',...publicHeaders])save(name,bytes.get('native/'+name).toString('utf8'));save('json.hpp',bytes.get('deps/json/json.hpp').toString('utf8'));
 save('TASK.json',{originalParentId:'sleep.gyro-power',originalEntry:'tools/ymcc_sim/global/sensor-power-consumer/run.mjs',selection:'power-wake',selectedCases,selectedCase:selectedCase??null,bindings:prepared.bindings,compiledParts:prepared.compiledParts,sourceHashes:[...bytes].map(([relative,value])=>({relative,bytes:value.length,sha256:sha(value)})),compiledSha256:sha(prepared.code),scope:prepared.scope,publicScopeAvailable:prepared.publicScopeAvailable,noFaultInjectCommandSeam:true});
 const vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat',q=value=>{assert(!/["%\r\n]/.test(value));return '"'+value+'"';};
 const exe=path.join(out,'fixture.exe'),command=calibrationToolchainPreamble(vc,{boundInheritedPath:true})+'set VSLANG=1033\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /sdl /O2 /MT /DNDEBUG /I'+q(out)+' '+q(path.join(out,'fixture.cpp'))+' /Fo'+q(path.join(out,'fixture.obj'))+' /Fe'+q(exe)+' /sourceDependencies '+q(path.join(out,'dependencies.json'))+' /link /VERBOSE:LIB ole32.lib user32.lib\r\nexit /b %errorlevel%\r\n';save('build.cmd',command);
 const processes=[];
 const call=(name,program,args,timeout)=>{const begin=performance.now(),r=spawnSync(program,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout,maxBuffer:8*1024*1024});const row={name,program,args,pid:r.pid,exitCode:r.status,signal:r.signal,error:r.error?.message,timeoutMs:timeout,elapsedMs:performance.now()-begin};save(name+'-PROCESS.json',row);save(name+'.stdout.log',r.stdout??'');save(name+'.stderr.log',r.stderr??'');processes.push(row);assert(!r.error&&!r.signal,'SENSOR_POWER_PROCESS_INTERRUPTED');return r;};
 const build=call('compile',process.env.ComSpec??'C:/Windows/System32/cmd.exe',['/d','/c',path.join(out,'build.cmd')],45000);assert.equal(build.status,0,'SENSOR_POWER_COMPILE_FAILED');
 const run=call('execute',exe,selectedCase?[selectedCase]:[],15000);assert([0,1,2].includes(run.status),'SENSOR_POWER_UNEXPECTED_EXIT');
 const behavior=run.status===2?null:JSON.parse(run.stdout);if(run.status===0)assert.deepEqual(behavior.cases,selectedCases,'SENSOR_POWER_EXACT_SELECTION');const result={schemaVersion:1,passed:run.status===0,status:run.status===0?'PASS_SCOPED_ACTUAL_STA':'FAIL_SCOPED_ACTUAL_STA',behavior,processes,elapsedMs:performance.now()-started,selectedCase:selectedCase??null,defaultPowerProfilesExecuted:false,fullParentExecuted:false,actualDevices:false};save('RESULT.json',result);save('consumer-result.json',result);console.log(JSON.stringify({status:result.status,cases:behavior?.cases.length,assertions:behavior?.assertions,failures:behavior?.failures}));if(run.status!==0)process.exitCode=run.status;return result;
}
