/** Genuine product input consumers behind an isolated WebView/system IO boundary.
 * Product files are bundled unchanged. The C++ mailbox is the production header.
 * OEM diagnostic observations never authorize actions or prove OEM suppression.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import readline from 'node:readline';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { VirtualClock } from '../fan-log-regression.mjs';
import {beginInputIdentity,finishInputIdentity,recordInputSetupFailure,finishInputConsumerIdentity,save} from './identity.mjs';
import {compileInputMailbox} from './native-identity.mjs';
import {compileInputProduct} from './product-identity.mjs';
import {fileHash,need} from './inputs.mjs';
const here = path.dirname(fileURLToPath(import.meta.url));
const defaultRepo = path.resolve(here, '../../../..');
const sha256 = data => crypto.createHash('sha256').update(data).digest('hex');
const clone = value => structuredClone(value);
const EPOCH = Date.UTC(2026, 9, 18, 14, 0, 0);
export const INPUT_SIGNAL_CONTRACT = Object.freeze({
  schemaVersion: 1, requestSchemaVersion: 1, feedbackSchemaVersion: 1,
  requestEvent: 'input.shortcutAction', feedbackEvent: 'input.shortcutFeedback',
  requiredRequestFields: ['schemaVersion','id','actionId','params','issuedAtUnixMs','expiresAtUnixMs','powerGeneration','inputRevision'],
  maxRequestLifetimeMs: 30000, maxFeedbackLifetimeMs: 10000, maxPending: 16,
  provenance: ['archive-literal','source-derived-fixture'],
  hcVersionPinned: false, physicalControllerFeedbackClosed: false,
});
const staticSources = ['native/shortcut_action_runtime.h','native/shortcut_action_protocol.h',
  'native/msi_frontkey_observation_runtime.h','src/bridge/controllerPressFeedback.ts'];
function sourceRecord(repoRoot, relative) {
  return { path: relative, sha256: sha256(fs.readFileSync(path.join(repoRoot, relative))) };
}
export function evaluateInputCompatibility(input = {}) {
  const expected = input.expectedContract ?? {};
  const differences = [];
  for (const field of ['schemaVersion','requestSchemaVersion','feedbackSchemaVersion']) {
    if (expected[field] !== undefined && expected[field] !== INPUT_SIGNAL_CONTRACT[field])
      differences.push({ field, expected: expected[field], actual: INPUT_SIGNAL_CONTRACT[field] });
  }
  const baseline = input.baseline ?? {}, candidate = input.candidate ?? {};
  for (const field of ['hcVersion','runtimeRoot','sharedRuntimeRoot','hostPayloadRoot','payloadIdentity']) {
    if ((baseline[field] !== undefined || candidate[field] !== undefined) && baseline[field] !== candidate[field])
      differences.push({ field, baseline: baseline[field], candidate: candidate[field], validation: 'NOT_EXECUTED_BY_INPUT_CONSUMER' });
  }
  return { status: differences.length ? 'BLOCKED_COMPATIBILITY_DIFFERENCE' : 'SCHEMA_COMPATIBLE',
    contract: INPUT_SIGNAL_CONTRACT, baseline: clone(baseline), candidate: clone(candidate), differences,
    hcValidation: 'NOT_EXECUTED_BY_INPUT_CONSUMER', versionPolicy: 'caller-supplied;no-version-pinning;runtime-connectivity-gate-required' };
}
async function compileProduct(repoRoot,session) {
  const entry = `export {startControllerShortcutRuntime} from './src/bridge/controllerShortcutRuntime';
export {onShortcutActionFeedback,normalizeShortcutActionFeedback} from './src/bridge/shortcutActionFeedback';
export {readShortcutActionRequest,parseUiShortcutParameters} from './src/bridge/controllerShortcutActions';
export {evaluateControllerFeedback} from './src/bridge/controllerFeedbackCapabilities';
export {createPressFeedback} from './src/bridge/controllerPressFeedback';
export {startMonitorNoticeHost} from './src/bridge/monitorNoticeHost';`;
  return compileInputProduct(session,entry);
}
async function compileMailbox(repoRoot,workDir,session) {return compileInputMailbox(session);}
async function openMailbox(compiled,session) {
  need(fileHash(compiled.executable)===compiled.executableSha256,'INPUT_NATIVE_EXECUTABLE_CHANGED_BEFORE_START');
  const startedAt=new Date().toISOString(),child=spawn(compiled.executable,[],{stdio:['pipe','pipe','pipe'],windowsHide:true});
  const stdout=[],stderr=[],index=session.nativeExecutions.length+1;let launchError=null;
  child.stdout.on('data',data=>stdout.push(Buffer.from(data)));child.stderr.on('data',data=>stderr.push(Buffer.from(data)));
  const terminal=new Promise(resolve=>{child.once('error',error=>{launchError={code:error.code??null,message:error.message};});child.once('close',(exitCode,signal)=>{const prefix=path.join(session.directory,'mailbox-'+index),out=Buffer.concat(stdout),err=Buffer.concat(stderr);fs.writeFileSync(prefix+'.stdout.log',out);fs.writeFileSync(prefix+'.stderr.log',err);const record={pid:child.pid,executable:compiled.executable,executableSha256:fileHash(compiled.executable),startedAt,endedAt:new Date().toISOString(),exitCode,signal,launchError,naturalExit:exitCode!==null&&!signal&&!launchError,stdout:{path:prefix+'.stdout.log',sha256:fileHash(prefix+'.stdout.log')},stderr:{path:prefix+'.stderr.log',sha256:fileHash(prefix+'.stderr.log')}};save(prefix+'.process.json',record);session.nativeExecutions.push(record);resolve(record);});});
  const waiting = [];let failure;
  const fail = () => { failure = Error('MEMORY_MAILBOX_CLOSED');for(const item of waiting.splice(0))item.reject(failure); };
  child.on('error',fail); child.on('exit',fail);child.stderr.resume();
  const reader = readline.createInterface({input:child.stdout});
  reader.on('line',line=>{ const item=waiting.shift();if(!item)return;try { const data=JSON.parse(line);if(data?.adapterError)throw Error(data.adapterError);item.resolve(data); } catch(error){item.reject(error);} });
  return { call(data) { if(failure)return Promise.reject(failure);return new Promise((resolve,reject)=>{waiting.push({resolve,reject});child.stdin.write(JSON.stringify(data)+'\n');}); },
    async close() { child.stdin.end();const record=await terminal;reader.close();need(record.naturalExit&&record.exitCode===0&&record.executableSha256===compiled.executableSha256,'INPUT_NATIVE_CHILD_IDENTITY_OR_EXIT'); } };
}
function eventTarget() {
  const listeners = new Map();
  return { addEventListener(type,fn){if(!listeners.has(type))listeners.set(type,new Set());listeners.get(type).add(fn);},
    removeEventListener(type,fn){listeners.get(type)?.delete(fn);},
    dispatchEvent(event){for(const fn of [...(listeners.get(event.type)??[])])fn(event);return true;},
    listenerCount(type){return listeners.get(type)?.size??0;} };
}
/** Public injection interface. Never connects to the running YMCC or reads user settings. */
export async function createInputSignalHarness(options={}) {
  const repoRoot = fs.realpathSync(options.repoRoot ?? defaultRepo);
  const workDir = path.resolve(options.workDir ?? path.join(process.cwd(),'work','ymcc-input-signals'));
  const ownsIdentity=!options.identitySession,identitySession=options.identitySession??beginInputIdentity(repoRoot,workDir);
  const compiled = await compileProduct(repoRoot,identitySession), native = await compileMailbox(repoRoot,workDir,identitySession);
  const mailbox = await openMailbox(native,identitySession), clock = new VirtualClock();
  const epoch = options.epochUnixMs ?? EPOCH;
  const violations=[], commands=[], trace=[], feedback=[], displayFailures=[];
  const state={generation:1,revision:1,epoch:1,phase:'ready',hardwareWritesAllowed:true,visible:true,minimized:false,taskViewRequests:0,launchRequests:0,stopped:false};
  const behavior={taskView:true,minimize:true,execute:true,dropFeedback:false,dropCompleteResponse:false,failDisplay:false,...options.behavior};
  const webviewListeners=new Set(), window=eventTarget(), document=eventTarget();
  document.visibilityState='visible';document.documentElement={dataset:{theme:'blue-black'}};
  const noticeSurface={id:0,nonce:'',frames:[],hidden:false,closed:false};
  document.createElement=()=>({style:{},setAttribute(){},removeAttribute(){}});
  const wall=()=>epoch+clock.now;
  const nativeCall=data=>mailbox.call({tick:clock.now,unixMs:wall(),...data});
  const updateContext=()=>nativeCall({op:'context',generation:state.generation,revision:state.revision,epoch:state.epoch,ready:state.phase==='ready'&&state.hardwareWritesAllowed});
  const send=message=>{for(const listener of [...webviewListeners])listener({data:clone(message)});};
  const emit=(event,data)=>{trace.push({stage:'input',event,at:clock.now,data:clone(data)});send({event,data});};
  const held=new Map();
  const memorySettings=Object.fromEntries(['ui','background','music','gamepad','performanceSchedule','gameCustom','tdp','cpu','sleep','quickApps','startupDesired','power','tray','autoclose','fan','input'].map(key=>[key,{}]));
  Object.assign(memorySettings,{schemaVersion:1,quickApps:{apps:[{name:'Memory fixture',path:'Z:\\YMCC-MEMORY-ONLY\\fixture.exe'}]}});
  const settingsDirectory='Z:\\YMCC-MEMORY-ONLY';
  async function handle(message) {
    if(!message.cmd)throw Error('UNEXPECTED_FRONTEND_NATIVE_EVENT');
    const record={cmd:message.cmd,args:clone(message.args??{}),at:clock.now};commands.push(record);
    trace.push({stage:'consumer',...record});
    if(behavior.holdCommand===message.cmd)await new Promise(resolve=>held.set(message.cmd,resolve));
    let result;
    switch(message.cmd) {
      case 'input.shortcutAction.claim': {const wire=await nativeCall({op:'claim',id:message.args.id});result={ok:!!wire?.id};break;}
      case 'input.shortcutAction.complete': {
        const wire=await nativeCall({op:'complete',id:message.args.id,outcome:message.args.outcome});
        record.feedback=clone(wire);result={ok:!!wire?.id};
        if(wire?.id&&!behavior.dropFeedback)emit('input.shortcutFeedback',wire);
        if(behavior.dropCompleteResponse){record.responseDropped=true;return;}
        if(behavior.rejectCompleteResponse)throw Error('COMPLETE_RESPONSE_LOST_FIXTURE');
        break;
      }
      case 'power.lifecycle': result={generation:state.generation,phase:state.phase,hardwareWritesAllowed:state.hardwareWritesAllowed,inputReady:state.phase==='ready',resumeReady:state.phase==='ready',hibernateAvailable:false};break;
      case 'shell.taskView':state.taskViewRequests++;result=behavior.taskView;break;
      case 'window.getState':result={visible:state.visible,minimized:state.minimized};break;
      case 'window.minimize':result=behavior.minimize;if(result){state.minimized=true;state.visible=false;}break;
      case 'shell.execute':state.launchRequests++;result=behavior.execute;break;
      case 'monitorNotice.status':result={id:noticeSurface.id,nonce:noticeSurface.nonce,available:true};break;
      case 'monitorNotice.create':noticeSurface.id=1;noticeSurface.nonce=message.args.nonce;result={id:1,nonce:message.args.nonce};emit('monitorNotice.ready',result);break;
      case 'monitorNotice.frame':noticeSurface.frames.push(clone(message.args.frame));result=behavior.noticeFrame!==false;break;
      case 'monitorNotice.hide':noticeSurface.hidden=true;result=true;break;
      case 'monitorNotice.close':noticeSurface.closed=true;noticeSurface.id=0;result=true;break;
      case 'settings.location':result={directory:settingsDirectory,file:settingsDirectory+'\\yeman-settings.json',backup:settingsDirectory+'\\yeman-settings.json.bak'};break;
      case 'settings.read':assert.equal(message.args.path,settingsDirectory+'\\yeman-settings.json');result={stamp:'memory:1',unchanged:false,content:JSON.stringify(memorySettings)};break;
      default: violations.push({stage:'system-io',cmd:message.cmd});throw Error('UNREVIEWED_SYSTEM_IO');
    }
    if(message.cmd==='input.shortcutAction.claim'&&behavior.dropClaimResponse){record.responseDropped=true;return;}
    record.result=clone(result);trace.push({stage:'state',cmd:message.cmd,result:clone(result),snapshot:clone(state)});
    send({id:message.id,result});
  }
  window.chrome={webview:{addEventListener(type,listener){assert.equal(type,'message');webviewListeners.add(listener);},postMessage(message){
    void handle(message).catch(error=>{send({id:message.id,error:error.message});});
  }}};
  class ClockDate extends Date {constructor(...args){super(...(args.length?args:[wall()]));}static now(){return wall();}}
  class CustomEvent {constructor(type,options={}){this.type=type;this.detail=options.detail;}}
  Object.assign(window,{setTimeout:clock.setTimeout,clearTimeout:clock.clearTimeout,setInterval:clock.setInterval,clearInterval:clock.clearInterval});
  const blocked=name=>()=>{violations.push({stage:'system-io',cmd:name});throw Error('FORBIDDEN_SYSTEM_IO');};
  const context=vm.createContext({window,document,CustomEvent,Date:ClockDate,performance:{now:()=>clock.now},
    setTimeout:clock.setTimeout,clearTimeout:clock.clearTimeout,setInterval:clock.setInterval,clearInterval:clock.clearInterval,
    queueMicrotask,structuredClone,URL,URLSearchParams,TextEncoder,TextDecoder,crypto:{randomUUID:()=> '00000000-0000-4000-8000-000000000001'},console:{log(){},warn(){},error(){}},
    fetch:blocked('fetch'),localStorage:{getItem:blocked('localStorage.getItem'),setItem:blocked('localStorage.setItem')},
  },{codeGeneration:{strings:false,wasm:false}});
  new vm.Script(compiled.code,{filename:'unmodified-product-input.bundle.js'}).runInContext(context,{timeout:5000});
  const product=context.YMCCInputSignals;
  const stopThrowing=product.onShortcutActionFeedback(value=>{if(behavior.failDisplay){displayFailures.push(value.id);throw Error('DISPLAY_FAILURE_FIXTURE');}});
  const stopFeedback=product.onShortcutActionFeedback(value=>{feedback.push(clone(value));trace.push({stage:'feedback',consumer:'onShortcutActionFeedback',value:clone(value),at:clock.now});});
  const runtime=product.startControllerShortcutRuntime();
  const monitorHost=behavior.enableNoticeHost?product.startMonitorNoticeHost():null;
  monitorHost?.ready({popupEnabled:true,ymcc:{opacity:.8}});
  await updateContext();
  async function settle() {
    // Native pipe replies require event-loop turns; virtual time remains frozen.
    for(let i=0;i<5000;i++) {
      let done=false;runtime.idle().then(()=>{done=true;});
      await clock.drain();await new Promise(resolve=>setImmediate(resolve));
      if(done)return;
      await new Promise(resolve=>setTimeout(resolve,1));
    }
    throw Error('INPUT_RUNTIME_DID_NOT_SETTLE');
  }
  return {clock,product,state,behavior,commands,trace,feedback,displayFailures,violations,monitorHost,noticeSurface,
    sources:[...new Map([...compiled.sources,...staticSources.map(p=>sourceRecord(repoRoot,p)),sourceRecord(repoRoot,'deps/json/json.hpp')].map(s=>[s.path,s])).values()],
    executedProductModules:compiled.sources.map(s=>({ ...s,execution:'loaded-in-vm;branch-coverage-is-scenario-specific'})).concat([{...sourceRecord(repoRoot,'native/shortcut_action_protocol.h'),execution:'compiled-and-executed-native-mailbox'}]),
    nativeAdapter:native.adapter,identitySession,
    async issue(actionId='os.switchProgram',params={}) {return nativeCall({op:'issue',actionId,params});},
    emit,settle,idle:()=>runtime.idle(),
    async inject(request){emit('input.shortcutAction',request);await settle();},
    async invalidate(event='power.pending') {
      state.generation++;state.phase=event==='power.resumed'?'ready':'pending';state.hardwareWritesAllowed=state.phase==='ready';
      await updateContext();emit(event,{generation:state.generation});
    },
    async sourceChanged() {state.epoch++;await updateContext();},
    release(command){const resolve=held.get(command);held.delete(command);if(resolve)resolve();},
    async waitCommand(command,count=1){for(let i=0;i<5000;i++){if(commands.filter(c=>c.cmd===command).length>=count)return;await new Promise(resolve=>setTimeout(resolve,1));}throw Error('EXPECTED_COMMAND_NOT_OBSERVED:'+command);},
    async stop(){runtime.stop();state.stopped=true;},
    listenerCount:()=>window.listenerCount('ipc:input.shortcutAction'),
    async close(){if(this.closePromise)return this.closePromise;this.closePromise=(async()=>{runtime.stop();monitorHost?.stop();for(const resolve of held.values())resolve();stopThrowing();stopFeedback();await mailbox.close();if(ownsIdentity)finishInputIdentity(identitySession);})();return this.closePromise;},
  };
}







const SOURCE_CHANGES = [
  {path:'src/bridge/controllerShortcutActions.ts',beforeSha256:'e1275ce00947b691ff06ce50c3d9d6f31f11d6eb52b1a4ec308542d5e61c86ce',reason:'Completion delivery failure no longer retries an already executed action result as failed.'},
  {path:'src/bridge/controllerShortcutRuntime.ts',beforeSha256:'84e32f06b8cb1d4b6250985b9a8f1306ed0e81df7ff425ccf745cf6d37dc23d3',reason:'Claim and complete receipts have bounded 3000ms IPC waits, releasing the serialized queue after lost responses.'},
];
const BASE_GAPS = [
  {stage:'native-source',code:'PHYSICAL_OEM_INPUT_NOT_EXECUTED',detail:'WMI/HID source capture and native serial dispatcher are upstream of the injected WebView boundary; runtime.h is statically traced, mailbox protocol.h actually executes.'},
  {stage:'physical-write',code:'OEM_DEFAULT_ACTION_SUPPRESSION_NOT_VERIFIED',detail:'MSI immediate/settled records are read-only diagnostics; dispatch acceptance is not a suppression receipt.'},
  {stage:'physical-feedback',code:'CONTROLLER_OUTPUT_PATH_UNENCLOSED',detail:'Production evaluateControllerFeedback keeps rumble/haptic/light unavailable; no HIDMaestro output callback or physical route is forged.'},
  {stage:'system-io',code:'OUTSTANDING_SYSTEM_CALL_NOT_CANCELLABLE',detail:'Invalidation prevents later product writes after an outstanding reply returns. Settings reads or shell.execute may still await a boundary reply without their own timeout; this task bounds claim/complete, not all system APIs or physical rollback.'},
  {stage:'runtime-compatibility',code:'HC_ABI_GATE_EXTERNAL',detail:'This input consumer accepts HC identities as contract inputs; actual runtime/COM ABI verification belongs to runtime-connectivity.'},
];
function nativeLogGaps(repoRoot) {
  const text=fs.readFileSync(path.join(repoRoot,'native/main.cpp'),'utf8');
  const expression='{"actionId", rule.ownAction.empty() ? rule.inputAction : rule.ownAction}';
  const count=text.split(expression).length-1;
  return count?[{stage:'native-logging',code:'UI_ACTION_ID_MISSING_FROM_OEM_DIAGNOSTICS',source:'native/main.cpp',sourceSha256:sha256(text),occurrences:count,remediation:'Parent-owned native/main.cpp proposal; not applied or counted as a passing product change by this task.'}]:[];
}
function environmentHashes(repoRoot) {
  return ['tools/ymcc_sim/global/input-signals/input-signal-consumer.mjs','tools/ymcc_sim/global/input-signals/input-signal-consumer.test.mjs','tools/ymcc_sim/global/input-signals/native-mailbox.cpp','tools/ymcc_sim/global/fan-log-regression.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].map(p=>sourceRecord(repoRoot,p));
}
function sourceChanges(repoRoot) {return SOURCE_CHANGES.map(c=>({...c,afterSha256:sourceRecord(repoRoot,c.path).sha256}));}
function snapshot(h) {
  return {input:h.trace.filter(t=>t.stage==='input'),consumer:h.trace.filter(t=>t.stage==='consumer'),
    state:{snapshot:clone(h.state),noticeHost:h.monitorHost?clone(h.monitorHost.inspect()):null,noticeSurface:clone(h.noticeSurface),transitions:h.trace.filter(t=>t.stage==='state')},
    feedback:{presentation:{consumer:h.monitorHost?'monitorNoticeHost':null,frameRequests:h.commands.filter(c=>c.cmd==='monitorNotice.frame').length,acceptedFrames:h.commands.filter(c=>c.cmd==='monitorNotice.frame'&&c.result===true).length,physicalDisplayVerified:false},nativeResults:h.commands.filter(c=>c.cmd==='input.shortcutAction.complete').map(c=>({requestId:c.args.id,outcome:clone(c.args.outcome),wire:clone(c.feedback??{}),ack:clone(c.result??null),responseDropped:c.responseDropped===true})),
      consumed:clone(h.feedback),displayFailures:clone(h.displayFailures)}};
}
function origin(value) {
  if(!value||typeof value!=='object')return null;
  const out={};for(const field of ['sha256','bytes','mtimeUtc','stable'])if(value[field]!==undefined)out[field]=value[field];return out;
}
const OBSERVATION_BOOLS=['configured','actionDispatchAccepted','takeoverApplied','contextStableAcrossRead','readOnly','registryMutated','hardwareWriteIssued','defaultActionSuppressionVerified','modeWriterProven'];
const OBSERVATION_INTS=['code','sequence','ruleEpoch','powerGeneration','sourceGeneration','capturedAt','requestPowerGeneration','vendorIntent','beginTick','endTick','observationEpisodes','episodeBudget','snapshotBudget','queueDrops','expired'];
function archiveObservation(event) {
  const literal={};for(const field of OBSERVATION_BOOLS)if(typeof event[field]==='boolean')literal[field]=event[field];
  for(const field of OBSERVATION_INTS)if(Number.isSafeInteger(event[field])&&event[field]>=0)literal[field]=event[field];
  for(const field of ['phase','origin','signalPolicy','signalFieldContainer'])if(typeof event[field]==='string'&&event[field].length<=80)literal[field]=event[field];
  return {provenance:'archive-literal',event:event.event,memberId:event.memberId,line:event.line,archiveEventId:event.id,
    input:literal,consumer:{status:'DIAGNOSTIC_ONLY',entry:'native/msi_frontkey_observation_runtime.h',controlConsumerExecuted:false},
    state:{actionDispatchAccepted:literal.actionDispatchAccepted??null,defaultActionSuppressionVerified:literal.defaultActionSuppressionVerified??null,hardwareWriteIssued:literal.hardwareWriteIssued??null},
    feedback:{status:'MISSING_ACTUAL_SUPPRESSION_RECEIPT',physicalSuppressionConfirmed:false}};
}
/** Takes the existing log-corpus white-listed events without reconstructing missing requests.
 * To explicitly supplement missing raw archive requests, pass a SEPARATE event:
 * {event:'fixture.shortcutAction',provenance:'source-derived-fixture',actionId,params}.
 */
export async function replayInputSignals(options={}) {
  const repoRoot=fs.realpathSync(options.repoRoot??defaultRepo),events=options.events??[];
  assert(Array.isArray(events)&&events.length<=20000,'INPUT_EVENTS_LIMIT_OR_TYPE_INVALID');
  const compatibility=evaluateInputCompatibility(options.compatibility);
  const evidence=[],gaps=[],executed=new Map();let h;
  const getHarness=async()=>{if(!h){h=await createInputSignalHarness(options);for(const s of h.executedProductModules)executed.set(s.path,s);}return h;};
  try {
    for(const event of events) {
      if(!event||typeof event!=='object'){gaps.push({stage:'input',code:'INVALID_INPUT_EVENT'});continue;}
      if(event.event==='msi-oem-suppression-observation') {evidence.push(archiveObservation(event));continue;}
      if(event.event==='fixture.shortcutAction') {
        if(event.provenance!=='source-derived-fixture'){gaps.push({stage:'input',code:'FIXTURE_PROVENANCE_REQUIRED'});continue;}
        if(compatibility.differences.length){gaps.push({stage:'compatibility',code:'FIXTURE_EXECUTION_BLOCKED'});continue;}
        const live=await getHarness(),start=live.trace.length,feedbackAt=live.feedback.length,commandsAt=live.commands.length;
        if(!live.product.parseUiShortcutParameters(String(event.actionId??'os.switchProgram'),event.params??{})){gaps.push({stage:'input',code:'INVALID_FIXTURE_ACTION_PARAMETERS'});continue;}
        const request=await live.issue(event.actionId??'os.switchProgram',event.params??{});
        if(!request?.id){gaps.push({stage:'input',code:'NATIVE_FIXTURE_REQUEST_REJECTED'});continue;}
        await live.inject(request);
        const delta=live.trace.slice(start),commands=live.commands.slice(commandsAt),complete=commands.find(c=>c.cmd==='input.shortcutAction.complete');
        evidence.push({provenance:'source-derived-fixture',input:{event:'input.shortcutAction',request},
          consumer:{entry:'src/bridge/controllerShortcutRuntime.ts',commands:commands.map(c=>c.cmd)},state:clone(live.state),
          feedback:{native:clone(complete?.feedback??null),consumed:clone(live.feedback.slice(feedbackAt)),
            status:!complete?'MISSING_COMPLETION':!complete.feedback?.id?'COMPLETION_REJECTED':complete.feedback.actionId==='os.switchProgram'&&complete.feedback.state!=='failed'?'INTENTIONALLY_NOT_PRESENTED':live.feedback.length>feedbackAt?'CONSUMED':'MISSING_FEEDBACK_CONSUMER'},trace:clone(delta)});
        continue;
      }
      if(event.event==='input.shortcutAction') {
        if(compatibility.differences.length){gaps.push({stage:'compatibility',code:'RAW_EXECUTION_BLOCKED'});continue;}
        const live=await getHarness(),request=event.data,valid=live.product.readShortcutActionRequest(request);
        const before=live.commands.length;live.emit(event.event,request);await live.settle();
        const commands=live.commands.slice(before),claim=commands.find(c=>c.cmd==='input.shortcutAction.claim');
        evidence.push({provenance:event.provenance==='source-derived-fixture'?'source-derived-fixture':'archive-literal',
          input:{event:event.event,validRequest:!!valid},consumer:{entry:'controllerShortcutRuntime',commands:commands.map(c=>c.cmd)},state:clone(live.state),
          feedback:{status:!valid?'INVALID_REQUEST_REJECTED':claim?.result?.ok===false?'NATIVE_CLAIM_REJECTED_UNISSUED_REQUEST':'MISSING_NATIVE_ISSUANCE_RECEIPT'}});
        gaps.push({stage:valid?'claim':'input',code:valid?'RAW_NATIVE_MAILBOX_ENTRY_UNAVAILABLE':'RAW_REQUEST_FIELDS_INVALID_OR_MISSING'});continue;
      }
      if(event.domain==='input'||event.event?.startsWith('shortcut-action')) {
        evidence.push({provenance:'archive-literal',input:{event:event.event,memberId:event.memberId,line:event.line,archiveEventId:event.id},
          consumer:{status:'UPSTREAM_NATIVE_SIGNAL_NOT_EXECUTED',entry:event.event==='shortcut-action-request'?'native/shortcut_action_runtime.h':'native/main.cpp'},
          state:{status:'UNKNOWN_FROM_THIS_RECORD'},feedback:{status:'MISSING_RAW_REQUEST_AND_TUPLE_BOUND_RECEIPT'}});
      }
    }
    if(h?.violations.length)gaps.push(...h.violations.map(v=>({stage:v.stage,code:'UNREVIEWED_SYSTEM_IO',cmd:v.cmd})));
    const originals=evidence.filter(e=>e.provenance==='archive-literal'),fixtures=evidence.filter(e=>e.provenance==='source-derived-fixture');
    const observationCount=originals.filter(e=>e.consumer.status==='DIAGNOSTIC_ONLY').length;
    if(originals.length)gaps.push({stage:'input',code:'ARCHIVE_CONTROL_CLOSURE_UNPROVEN',count:originals.length});
    const result={schemaVersion:1,kind:'input-signal-replay',status:compatibility.differences.length?'BLOCKED':'INCOMPLETE',verdict:compatibility.differences.length?'BLOCKED_COMPATIBILITY_DIFFERENCE':'INCOMPLETE_COVERAGE',
      sourceArchive:origin(options.sourceArchive),sourceChanges:sourceChanges(repoRoot),testEnvironmentHashes:environmentHashes(repoRoot),
      sourceHashes:h?.sources??staticSources.concat(SOURCE_CHANGES.map(s=>s.path),'src/bridge/shortcutActionFeedback.ts','src/bridge/controllerFeedbackCapabilities.ts').map(p=>sourceRecord(repoRoot,p)),
      executedProductModules:[...executed.values()],inputConsumerStateFeedback:evidence,compatibility,
      summary:{inputEvents:evidence.length,archiveLiteral:originals.length,sourceDerivedFixtures:fixtures.length,oemDiagnosticObservations:observationCount,
        acceptedOemDiagnostics:originals.filter(e=>e.input.actionDispatchAccepted===true).length,actionsExecuted:h?(h.state.taskViewRequests+h.state.launchRequests):0},
      boundary:{realOsSleep:false,deviceWrites:false,userSettingsRead:false,runningYmccContacted:false,nativeMailbox:'production-header-in-owned-memory-helper'},
      remainingGaps:[...gaps,...BASE_GAPS,...nativeLogGaps(repoRoot)]};
    if(h){await h.close();const inputIdentity=finishInputIdentity(h.identitySession);return{...result,inputIdentity,sourceHashes:[...new Map([...result.sourceHashes,...inputIdentity.sourceHashes].map(row=>[row.path,row])).values()],toolArtifacts:inputIdentity.toolArtifacts};}return result;
  } finally {if(h)await h.close();}
}
export async function runInputSignalRegressions(options={}) {
  const repoRoot=fs.realpathSync(options.repoRoot??defaultRepo),cases=[],sources=new Map(),executed=new Map();
  const workDir=path.resolve(options.workDir??path.join(process.cwd(),'work','ymcc-input-signals')),identitySession=beginInputIdentity(repoRoot,workDir);
  const run=async(id,check,behavior={})=>{
    let h,semanticStarted=false;try {h=await createInputSignalHarness({...options,workDir,identitySession,behavior});semanticStarted=true;await check(h);assert.equal(h.violations.length,0,'UNEXPECTED_IO');
      cases.push({id,status:'PASS',provenance:'source-derived-fixture',inputConsumerStateFeedback:snapshot(h)});
    } catch(error) {if(!semanticStarted)recordInputSetupFailure(identitySession,error);cases.push({id,status:'FAIL',execution:{phase:semanticStarted?'scenario':'setup',semanticExecuted:semanticStarted},code:String(error.message).slice(0,400),...(h?{inputConsumerStateFeedback:snapshot(h)}:{})});}
    finally {if(h){for(const s of h.sources)sources.set(s.path,s);for(const s of h.executedProductModules)executed.set(s.path,s);await h.close();}}
  };
  const quickParams={appPath:'Z:\\YMCC-MEMORY-ONLY\\fixture.exe'};
  await run('valid-request-to-real-feedback',async h=>{await h.inject(await h.issue('app.launchQuickApp',quickParams));assert.equal(h.state.launchRequests,1);assert.equal(h.feedback[0]?.state,'applied');assert.equal(h.noticeSurface.frames[0]?.feedback.state,'applied');assert.equal(h.monitorHost.inspect().state.current.state,'applied');},{enableNoticeHost:true});
  await run('duplicate-id-executes-once',async h=>{const r=await h.issue('app.launchQuickApp',quickParams);await h.inject(r);await h.inject(r);assert.equal(h.state.launchRequests,1);assert.equal(h.feedback.length,1);});
  await run('expired-request-never-claimed',async h=>{const r=await h.issue();await h.clock.advance(30000);await h.inject(r);assert.equal(h.commands.length,0);});
  await run('duplicate-burst-no-retry-or-timer-storm',async h=>{const r=await h.issue();for(let i=0;i<100;i++)h.emit('input.shortcutAction',r);await h.settle();assert.equal(h.state.taskViewRequests,1);assert.equal(h.commands.filter(c=>c.cmd==='input.shortcutAction.complete').length,1);assert.equal(h.clock.tasks.size,0);});
  await run('native-mailbox-bounded-at-sixteen',async h=>{for(let i=0;i<16;i++)assert((await h.issue()).id);assert.equal(await h.issue(),null);assert.equal(h.commands.length,0);});
  await run('expiry-while-awaiting-system-reply-retires-request',async h=>{h.emit('input.shortcutAction',await h.issue('app.launchQuickApp',quickParams));await h.waitCommand('settings.read');await h.clock.advance(30000);h.release('settings.read');await h.settle();assert.equal(h.state.launchRequests,0);assert.equal(h.commands.filter(c=>c.cmd==='input.shortcutAction.complete').length,0);},{holdCommand:'settings.read'});
  await run('invalid-fields-never-claimed',async h=>{const r=await h.issue();await h.inject({...r,schemaVersion:2});await h.inject({...r,params:[],id:'invalid-params'});assert.equal(h.commands.length,0);});
  await run('native-source-guard-denies-claim',async h=>{const r=await h.issue();await h.sourceChanged();await h.inject(r);assert.equal(h.state.taskViewRequests,0);assert(h.commands.some(c=>c.cmd==='input.shortcutAction.claim'&&c.result.ok===false));assert.equal(h.feedback.length,0);});
  for(const event of ['power.pending','power.suspending','power.resumed'])await run(event+'-retires-inflight-and-queued',async h=>{
    const first=await h.issue(),second=await h.issue();h.emit('input.shortcutAction',first);h.emit('input.shortcutAction',second);
    await h.waitCommand('power.lifecycle');await h.invalidate(event);h.release('power.lifecycle');await h.settle();
    assert.equal(h.state.taskViewRequests,0);assert.equal(h.commands.filter(c=>c.cmd==='input.shortcutAction.complete').length,0);
  },{holdCommand:'power.lifecycle'});
  await run('stale-power-generation-rejected',async h=>{const r=await h.issue();await h.invalidate('power.resumed');await h.inject(r);assert.equal(h.state.taskViewRequests,0);});
  await run('action-failure-to-failed-feedback',async h=>{await h.inject(await h.issue());assert.equal(h.state.taskViewRequests,1);assert.equal(h.feedback[0]?.state,'failed');},{taskView:false});
  await run('visible-switch-success-is-intentionally-silent',async h=>{await h.inject(await h.issue());assert.equal(h.commands.find(c=>c.cmd==='input.shortcutAction.complete').feedback.state,'applied');assert.equal(h.feedback.length,0);});
  await run('display-exception-cannot-break-action-or-other-consumer',async h=>{await h.inject(await h.issue('app.launchQuickApp',quickParams));assert.equal(h.state.launchRequests,1);assert.equal(h.feedback.length,1);assert.equal(h.displayFailures.length,1);},{failDisplay:true});
  await run('native-notice-surface-failure-cannot-repeat-action',async h=>{await h.inject(await h.issue('app.launchQuickApp',quickParams));assert.equal(h.state.launchRequests,1);assert.equal(h.feedback[0]?.state,'applied');assert.equal(h.noticeSurface.frames.length,1);},{enableNoticeHost:true,noticeFrame:false});
  await run('feedback-duplicate-and-expired-rejected',async h=>{await h.inject(await h.issue('app.launchQuickApp',quickParams));const wire=h.commands.find(c=>c.cmd==='input.shortcutAction.complete').feedback;
    h.emit('input.shortcutFeedback',wire);h.emit('input.shortcutFeedback',{...wire,id:'other:result',expiresAtUnixMs:wire.occurredAtUnixMs-1});assert.equal(h.feedback.length,1);});
  await run('stop-unsubscribes-and-rejects-later-input',async h=>{const r=await h.issue();await h.stop();await h.inject(r);assert.equal(h.commands.length,0);assert.equal(h.listenerCount(),0);});
  await run('stop-retires-inflight-and-queued-input',async h=>{const a=await h.issue(),b=await h.issue();h.emit('input.shortcutAction',a);h.emit('input.shortcutAction',b);await h.waitCommand('power.lifecycle');await h.stop();h.release('power.lifecycle');await h.settle();assert.equal(h.state.taskViewRequests,0);assert.equal(h.commands.filter(c=>c.cmd==='input.shortcutAction.complete').length,0);},{holdCommand:'power.lifecycle'});
  await run('completion-error-does-not-send-false-failure',async h=>{await h.inject(await h.issue());const complete=h.commands.filter(c=>c.cmd==='input.shortcutAction.complete');assert.equal(complete.length,1,'completion transport failure must not retry an applied action as failed');assert.equal(complete[0].args.outcome.state,'applied');},{rejectCompleteResponse:true});
  await run('completion-response-loss-bounded-and-queue-recovers',async h=>{h.emit('input.shortcutAction',await h.issue());await h.waitCommand('input.shortcutAction.complete');
    for(let i=0;i<20;i++)await new Promise(resolve=>setImmediate(resolve));await h.clock.advance(3000);await h.settle();
    assert.equal(h.commands.filter(c=>c.cmd==='input.shortcutAction.complete').length,1);h.behavior.dropCompleteResponse=false;await h.inject(await h.issue());assert.equal(h.state.taskViewRequests,2);},{dropCompleteResponse:true});
  await run('claim-response-loss-bounded-without-action',async h=>{h.emit('input.shortcutAction',await h.issue());await h.waitCommand('input.shortcutAction.claim');
    for(let i=0;i<20;i++)await new Promise(resolve=>setImmediate(resolve));await h.clock.advance(3000);await h.settle();assert.equal(h.state.taskViewRequests,0);h.behavior.dropClaimResponse=false;await h.inject(await h.issue());assert.equal(h.state.taskViewRequests,1);},{dropClaimResponse:true});
  await run('missing-feedback-is-observable-never-physical-success',async h=>{await h.inject(await h.issue('app.launchQuickApp',quickParams));assert.equal(h.state.launchRequests,1);assert.equal(h.commands.find(c=>c.cmd==='input.shortcutAction.complete').feedback.state,'applied');assert.equal(h.feedback.length,0);},{dropFeedback:true});
  await run('power.pending-clears-real-notice-state',async h=>{await h.inject(await h.issue('app.launchQuickApp',quickParams));assert(h.monitorHost.inspect().state.current);await h.invalidate('power.pending');assert.equal(h.monitorHost.inspect().state.current,null);assert.equal(h.state.launchRequests,1);assert(h.commands.some(c=>c.cmd==='monitorNotice.hide'));},{enableNoticeHost:true});
  await run('physical-feedback-settings-cannot-forge-closure',async h=>{const forged={outputCallbackBound:true,physicalRouteClosed:true,feedbackClosed:true};for(const persona of ['elite','steamdeck','dualsense-edge']){const view=h.product.evaluateControllerFeedback(forged,true,persona);assert.equal(view.allClosed,false);assert(view.capabilities.every(c=>c.closure==='UNENCLOSED'&&c.availability==='unavailable'));}});
  await run('press-feedback-minimum-visibility-and-reset',async h=>{const seen=[],press=h.product.createPressFeedback(keys=>seen.push([...keys]));press.accept(['a']);await h.clock.advance(20);press.accept([]);assert.equal(seen.length,1);await h.clock.advance(159);assert.equal(seen.length,1);await h.clock.advance(1);assert.deepEqual(seen,[['a'],[]]);press.accept(['b']);press.reset();await h.clock.advance(1000);assert.deepEqual(seen.at(-1),[]);});
  const compatibility=evaluateInputCompatibility(options.compatibility);
  const failed=cases.filter(c=>c.status!=='PASS').length;
  const result={schemaVersion:1,kind:'input-signal-regressions',status:failed?'FAIL':compatibility.differences.length?'BLOCKED':'PASS',verdict:failed?'SOFTWARE_CHAIN_FAILURE':compatibility.differences.length?'BLOCKED_COMPATIBILITY_DIFFERENCE':'SOFTWARE_CHAIN_PASS_PHYSICAL_GAPS_REMAIN',
    summary:{total:cases.length,passed:cases.length-failed,failed},sourceChanges:sourceChanges(repoRoot),testEnvironmentHashes:environmentHashes(repoRoot),sourceHashes:[...sources.values()],
    executedProductModules:[...executed.values()],inputConsumerStateFeedback:cases,compatibility,remainingGaps:[...BASE_GAPS,...nativeLogGaps(repoRoot)],
    boundary:{realOsSleep:false,deviceWrites:false,userSettingsRead:false,runningYmccContacted:false,virtualClock:true}};
  save(path.join(identitySession.directory,'consumer-scenarios.json'),result);
  return finishInputConsumerIdentity(identitySession,result);
}
async function main() {
  const args=process.argv.slice(2);let output,planPath,workDir,compatibilityPath;
  for(let i=0;i<args.length;i++){const key=args[i],value=args[++i];if(key==='--output')output=value;else if(key==='--events')planPath=value;else if(key==='--work-dir')workDir=value;else if(key==='--compatibility')compatibilityPath=value;else throw Error('UNKNOWN_INPUT_CONSUMER_ARGUMENT');}
  const compatibility=compatibilityPath?JSON.parse(fs.readFileSync(compatibilityPath,'utf8')):undefined;
  const plan=planPath?JSON.parse(fs.readFileSync(planPath,'utf8')):null;
  const result=plan?await replayInputSignals({events:plan.events,sourceArchive:plan.sourceArchive,workDir,compatibility}):await runInputSignalRegressions({workDir,compatibility});
  if(output){fs.mkdirSync(path.dirname(path.resolve(output)),{recursive:true});fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n');}
  console.log(JSON.stringify({kind:result.kind,status:result.status,summary:result.summary,output:output??null}));
  if(result.status==='FAIL'||result.status==='BLOCKED')process.exitCode=1;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await main();












