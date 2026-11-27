import assert from 'node:assert/strict';import test from 'node:test';import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {verifyMode,read,hash} from './contract.mjs';
import {verifyRequestTimeline} from './timeline.mjs';
import {ownedJobCompleted} from './run.mjs';

test('owned fan Job requires assigned natural zero for its exact child and deadline',()=>{
 const deadline=1000,stdout='YMCC_SIM_OWNED_JOB childPid=19\r\n',job={status:'EXITED',exitCode:0,rootExitCode:0,jobAssigned:true,jobDisposed:true,
  childExitedBeforeReturn:true,zeroObservedBeforeDispose:true,activeProcessesFinal:0,terminationRequested:false,cleanupError:null,deadlineExpired:false,deadlineEpochMs:deadline,childPid:19};
 assert(ownedJobCompleted(job,stdout,deadline));
 for(const delta of [{jobAssigned:false},{childExitedBeforeReturn:false},{zeroObservedBeforeDispose:false},{activeProcessesFinal:1},{terminationRequested:true},
  {cleanupError:'query failed'},{deadlineExpired:true},{childPid:20},{deadlineEpochMs:999}])assert(!ownedJobCompleted({...job,...delta},stdout,deadline));
 assert(!ownedJobCompleted(job,stdout+stdout,deadline));assert(!ownedJobCompleted(null,stdout,deadline));
});

function emptyWakeTimeline(){
 const ready={native:{generation:3,phase:'ready',resumeReady:true,hardwareWritesAllowed:true},coordinator:{powerGeneration:3,wakeReadyGeneration:3}};
 const wake={event:'committed-wake-settled',...structuredClone(ready),intent:false};
 const late={event:'late-auto-settled',...structuredClone(ready),intent:false,submitted:false,offRetired:true,ok:true};
 const send={event:'request-issued',request:1,path:'/api/state'},receive={event:'response-received',request:1,path:'/api/state',status:200};
 const timeline=[{event:'sleep-observed'},{event:'disable-completed'},{event:'client-wake-notice',...structuredClone(ready)},wake,send,receive,late,{event:'cleanup-completed'}];
 const sequence=()=>timeline.forEach((row,i)=>{row.sequence=i+1;row.at=i;});
 sequence();
 return {output:{mode:'empty-apply',ok:true,error:'',timelineSchemaVersion:1,timeline},node:{requests:[{path:'/api/state',status:200}]},wake,late,send,receive,ready,sequence};
}
test('empty wake timeline permits read-only observations after OFF',()=>{
 const {output,node}=emptyWakeTimeline(), receipt=verifyRequestTimeline(output,node);
 assert.equal(receipt.status,'PASS');assert.equal(receipt.newControlsAfterOff,0);
});
test('empty wake timeline rejects missing, repeated or displaced terminal observations',()=>{
 const missing=emptyWakeTimeline();missing.wake.event='native-read';
 assert.throws(()=>verifyRequestTimeline(missing.output,missing.node),/EXPLICIT_EMPTY_WAKE_EVENT/);
 const repeat=emptyWakeTimeline();repeat.output.timeline.splice(4,0,structuredClone(repeat.late));repeat.sequence();
 assert.throws(()=>verifyRequestTimeline(repeat.output,repeat.node),/EXPLICIT_EMPTY_WAKE_EVENT/);
 const reversed=emptyWakeTimeline();reversed.wake.event='late-auto-settled';reversed.late.event='committed-wake-settled';
 assert.throws(()=>verifyRequestTimeline(reversed.output,reversed.node),/EXPLICIT_EMPTY_WAKE_ORDER/);
});
test('empty wake timeline rejects stale or unavailable write permission',()=>{
 for(const update of [row=>{row.native.generation=2;},row=>{row.native.hardwareWritesAllowed=false;},row=>{row.coordinator.wakeReadyGeneration=2;}]){
  const {output,node,late}=emptyWakeTimeline();update(late);
  assert.throws(()=>verifyRequestTimeline(output,node),/EXPLICIT_EMPTY_WAKE_GENERATION/);
 }
});
test('empty wake timeline rejects retired callbacks, intent replay and control sends',()=>{
 for(const update of [r=>{r.wake.intent=true;},r=>{r.late.intent=true;},r=>{r.late.submitted=true;},r=>{r.late.offRetired=false;}]){
  const input=emptyWakeTimeline();update(input);
  assert.throws(()=>verifyRequestTimeline(input.output,input.node),/EXPLICIT_EMPTY_WAKE_REPLAY_NOT_RETIRED/);
 }
 const {output,node,send,receive,ready,sequence}=emptyWakeTimeline();
 output.timeline.splice(4,0,{event:'native-read',...structuredClone(ready)});
 Object.assign(send,ready,{path:'/api/enable'});receive.path=node.requests[0].path='/api/enable';sequence();
 assert.throws(()=>verifyRequestTimeline(output,node),/EXPLICIT_EMPTY_WAKE_REPLAY_NOT_RETIRED/);
});
const spec=read(path.join(import.meta.dirname,'spec.json')),privateRoot=path.dirname(spec.fault.preExecutionPrediction);
function originalInputs(mode='allowed',fault=false){const directory=path.join(privateRoot,fault?'fault-allowed':'original-'+mode),managed=read(path.join(directory,'result.json')),boundary=read(path.join(directory,'managed-runtime-boundary.json')),node=read(path.join(directory,'node/node-runtime-boundary.json')),execution=read(path.join(directory,'owned-managed-execution.json')),originalOutput=JSON.parse(fs.readFileSync(path.join(directory,'bridge-host-chain.out'),'utf8').trim());
 if(fault){const projected=new TextDecoder('gbk').decode(Buffer.from(spec.fault.expectedTsError,'utf8'));assert.equal(originalOutput.error,projected,'independent byte-encoding projection must match the retained original CSharp text');originalOutput.error=spec.fault.expectedTsError;}
 return{value:{managed,boundary,node,originalOutput,execution},options:{mode,fault,hostSha256:managed.hostAssemblySha256}};
}
function verify(value,options){return verifyMode(value,spec,options);}
function clone(value){return structuredClone(value);}
test('five actually executed original modes and exact one semantic fault remain accepted',()=>{for(const mode of spec.modes){const{value,options}=originalInputs(mode.mode);assert.equal(verify(value,options).checks,mode.checks);}const{value,options}=originalInputs('allowed',true);assert.equal(verify(value,options).failedChecks[0].name,spec.fault.expectedManagedFailure);});
test('managed missing or reordered original checks are rejected',()=>{const{value,options}=originalInputs();value.managed.checks.reverse();assert.throws(()=>verify(value,options),/ORIGINAL_MANAGED_CHECK_ORDER/);});
test('an unrelated original managed assertion failure is not semantic fault detection',()=>{const{value,options}=originalInputs('allowed',true);value.managed.checks.find(row=>row.name===spec.fault.expectedManagedFailure).passed=true;value.managed.checks[0].passed=false;assert.throws(()=>verify(value,options),/EXACT_ORIGINAL_FAULT_CHECK/);});
test('launch or timeout endpoint cannot stand in for original behavioral fault',()=>{const{value,options}=originalInputs('allowed',true);value.execution.naturalExit=false;assert.throws(()=>verify(value,options),/OWNED_ORIGINAL_CHILD_ENDPOINT/);});
test('runtime boundary denials cannot stand in for semantic fault',()=>{const{value,options}=originalInputs('allowed',true);value.node.denied.push({kind:'FORBIDDEN'});assert.throws(()=>verify(value,options),/NODE_BOUNDARY_DENIAL_OR_TIMER/);});
test('actual HC or native attempts prevent acceptance',()=>{const{value,options}=originalInputs();value.boundary.targetNativeAttempts.push('kernel32');assert.throws(()=>verify(value,options),/MANAGED_BOUNDARY_DENIAL/);});
test('wrong actual loaded Host identity is rejected',()=>{const{value,options}=originalInputs();value.boundary.loadedTargetAssemblies.find(row=>path.basename(row.path)==='YeManFanHost.dll').sha256='0'.repeat(64);assert.throws(()=>verify(value,options),/ACTUAL_LOADED_HOST_IDENTITY/);});
test('an unrelated network connection is rejected',()=>{const{value,options}=originalInputs();value.node.connections[0].host='192.0.2.1';assert.throws(()=>verify(value,options),/OWNED_LOOPBACK_CONNECTIONS/);});
test('missing explicit cancel feedback is rejected',()=>{const{value,options}=originalInputs();value.node.requests[1].response.controlIntentCancelled=false;assert.throws(()=>verify(value,options),/DISABLE_CANCEL_REAL_HTTP_SEAM/);});
test('sleeping or stale native may not enter any control write endpoint',()=>{const{value,options}=originalInputs('sleep');const row=clone(value.node.requests[2]);row.path='/api/enable';value.node.requests.splice(2,0,row);value.node.requests.forEach((request,index)=>request.sequence=index+1);assert.throws(()=>verify(value,options),/FORBIDDEN_CONTROL_AFTER_REFUSAL_OR_FAULT/);});
test('the exact original TS suspended feedback is required for the source fault',()=>{const{value,options}=originalInputs('allowed',true);value.originalOutput.error='compiler failed';assert.throws(()=>verify(value,options),/ORIGINAL_TS_FAULT_FEEDBACK/);});
test('original listener failure and retained timeout resources prevent acceptance',()=>{const{value,options}=originalInputs();value.managed.checks.find(row=>row.name==='cancel-http-listener-stops-within-bound').passed=false;assert.throws(()=>verify(value,options));const another=originalInputs();another.value.node.activeResources.push('Timeout');assert.throws(()=>verify(another.value,another.options),/NODE_BOUNDARY_DENIAL_OR_TIMER/);});

// Strict alternative for a current Host whose original mock resume already finished before client startup.
function existingReadySessionInputs(){
 const input=originalInputs(),requests=input.value.node.requests;
 input.value.node.requests=requests.filter(row=>!['/api/open','/api/open-events'].includes(row.path));input.value.node.requests.forEach((row,i)=>row.sequence=i+1);
 const acquire=input.value.node.requests.find(row=>row.path==='/api/acquire-control'),state=input.value.node.requests.filter(row=>row.path==='/api/state'&&row.sequence<acquire.sequence).at(-1);
 assert(state);state.response.state={...state.response.state,state:'Ready',powerState:'On',openCalled:true,openEventsCalled:true,controlAccepting:true,unknownState:false,hcCloseCleanupPending:false,powerOperationGeneration:2,resumePhaseGeneration:2,powerOperationStatus:'completed',lease:null};return input;
}
test('already-ready session needs exact actual pre-acquire state and retains one acquire/enable',()=>{const{value,options}=existingReadySessionInputs();assert.equal(verify(value,options).controlSessionPath,'existing-ready-session');});
test('no-open admission rejects missing or stale actual session proof',()=>{for(const change of ['missing','generation','open-events','unknown','cleanup']){const{value,options}=existingReadySessionInputs(),acquire=value.node.requests.find(row=>row.path==='/api/acquire-control'),row=value.node.requests.filter(r=>r.path==='/api/state'&&r.sequence<acquire.sequence).at(-1);if(change==='missing')delete row.response.state;else if(change==='generation')row.response.state.powerOperationGeneration=1;else if(change==='open-events')row.response.state.openEventsCalled=false;else if(change==='unknown')row.response.state.unknownState=true;else row.response.state.hcCloseCleanupPending=true;assert.throws(()=>verify(value,options),/EXISTING_READY_SESSION_NOT_PROVEN/);}});
test('unpaired open cannot borrow a ready snapshot',()=>{const{value,options}=existingReadySessionInputs();const row=structuredClone(value.node.requests.find(r=>r.path==='/api/state'));row.path='/api/open';value.node.requests.splice(3,0,row);value.node.requests.forEach((r,i)=>r.sequence=i+1);assert.throws(()=>verify(value,options),/UNPAIRED_OR_REPEATED_SESSION_OPEN/);});
test('already-ready evidence never permits a sleeping-mode control',()=>{const{value,options}=originalInputs('sleep'),ready=existingReadySessionInputs();value.node.requests=ready.value.node.requests;assert.throws(()=>verify(value,options),/FORBIDDEN_CONTROL_AFTER_REFUSAL_OR_FAULT/);});

test('ready reply from another session cannot prove this acquire',()=>{const{value,options}=existingReadySessionInputs(),acquire=value.node.requests.find(r=>r.path==='/api/acquire-control'),proof=value.node.requests.filter(r=>r.path==='/api/state'&&r.sequence<acquire.sequence).at(-1);proof.sessionTokenSha256='f'.repeat(64);assert.throws(()=>verify(value,options),/EXISTING_READY_SESSION_NOT_PROVEN/);});
test('future ready response cannot cure an unproven pre-acquire session',()=>{const{value,options}=existingReadySessionInputs(),acquire=value.node.requests.find(r=>r.path==='/api/acquire-control'),before=value.node.requests.filter(r=>r.path==='/api/state'&&r.sequence<acquire.sequence).at(-1),future=structuredClone(before);before.response.state.controlAccepting=false;value.node.requests.splice(value.node.requests.indexOf(acquire)+1,0,future);value.node.requests.forEach((r,i)=>r.sequence=i+1);assert.throws(()=>verify(value,options),/EXISTING_READY_SESSION_NOT_PROVEN/);});
