// Real Fan manual-wake IPC and lifecycle. Native power facts and Host/process
// operations are inert; all deadlines use a virtual monotonic clock.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { webcrypto, createHash } from 'node:crypto';
import { build } from 'esbuild';
const project=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]);
fs.mkdirSync(out,{recursive:true});
const nativeStartupReplayPath=process.env.YMCC_FAN_STARTUP_REPLAY?path.resolve(process.env.YMCC_FAN_STARTUP_REPLAY):null;
const nativeStartupReplayBytes=nativeStartupReplayPath?fs.readFileSync(nativeStartupReplayPath):null;
const nativeStartupReplay=nativeStartupReplayBytes?JSON.parse(nativeStartupReplayBytes):null;
const nativeStartupWire=nativeStartupReplay?.behavior?.coldFanReplay;
if(nativeStartupReplay)assert(nativeStartupWire,'the Native replay must include behavior.coldFanReplay');
const nativeRawReplayPath=process.env.YMCC_FAN_RAW_RECEIPT_REPLAY?path.resolve(process.env.YMCC_FAN_RAW_RECEIPT_REPLAY):null;
const nativeRawReplayBytes=nativeRawReplayPath?fs.readFileSync(nativeRawReplayPath):null;
const nativeRawReplay=nativeRawReplayBytes?JSON.parse(nativeRawReplayBytes):null;
const nativeRawWire=nativeRawReplay?.behavior?.rawReceiptFanReplay;
if(nativeRawReplay)assert(nativeRawReplay.passed===true&&nativeRawWire?.ordinary&&nativeRawWire?.replacementSuspend,
 'the passed Native receipt replay must contain both ordered startup routes');
const nativeStartupOffPath=process.env.YMCC_FAN_STARTUP_OFF_REPLAY?path.resolve(process.env.YMCC_FAN_STARTUP_OFF_REPLAY):null;
const nativeStartupOffBytes=nativeStartupOffPath?fs.readFileSync(nativeStartupOffPath):null;
const nativeStartupOff=nativeStartupOffBytes?JSON.parse(nativeStartupOffBytes):null;
const nativeStartupOffWire=nativeStartupOff?.behavior?.coldFanOffReplay;
if(nativeStartupOff)assert(nativeStartupOff.passed===true&&nativeStartupOffWire,'the passed Native OFF replay must include behavior.coldFanOffReplay');
const nativeCloseWirePath=process.env.YMCC_FAN_CLOSE_REPLAY;
const nativeCloseWireBytes=nativeCloseWirePath?fs.readFileSync(nativeCloseWirePath):null;
const nativeCloseWire=nativeCloseWireBytes?JSON.parse(nativeCloseWireBytes).coldFanReplay:null;
if(nativeCloseWireBytes)assert(nativeCloseWire?.afterClose?.fanWakeGeneration,'Native Close replay must contain its public grant');
const nativeS4Path=process.env.YMCC_FAN_S4_REPLAY?path.resolve(process.env.YMCC_FAN_S4_REPLAY):null;
const nativeS4Bytes=nativeS4Path?fs.readFileSync(nativeS4Path):null;
const nativeS4Lines=nativeS4Bytes?nativeS4Bytes.toString('utf8').split(/\r?\n/).filter(line=>line.startsWith('S4_RETAINED_FAN_WIRE ')):[];
if(nativeS4Bytes){
 assert.match(nativeS4Bytes.toString('utf8'),/^checks=\d+ failures=0 selected_suite=(all|s4-wire-only) /m);
 assert.equal(nativeS4Lines.length,1,'Native gate must emit one original S4 wire');
}
const nativeS4Wire=nativeS4Bytes?JSON.parse(nativeS4Lines[0].slice('S4_RETAINED_FAN_WIRE '.length)):null;
const beforeRoot=process.argv[3]?path.resolve(process.argv[3]):null;
const sharedReadyOnly=process.argv[5]==='--shared-ready-only';
const backgroundReadyOnly=process.argv[5]==='--background-ready-only';
const nativeOwnerRearmOnly=process.argv[5]==='--native-owner-rearm-only';
const nativeOwnerCancelOnly=process.argv[5]==='--native-owner-cancel-only';
const nativeOwnerOnly=process.argv[5]==='--native-owner-only'||nativeOwnerRearmOnly||nativeOwnerCancelOnly;
if(process.argv.length>6||(process.argv[5]&&!sharedReadyOnly&&!backgroundReadyOnly&&!nativeOwnerOnly))throw Error('Invalid explicit Fan wake IPC selection');
const selected=p=>beforeRoot&&['api.ts','fanHost.ts'].includes(path.basename(p))?path.join(beforeRoot,path.basename(p).replace('.ts','.before.ts')):path.join(project,p);
const appSource=fs.readFileSync(path.join(project,'src/App.vue'),'utf8');
const appSuspendObserver=appSource.match(/function onFanPhysicalSuspending\(e: Event\): void \{[\s\S]*?\n\}/)?.[0];
const appPublicSuspendPath=['powerEventGeneration','notePowerTransitionGeneration','onPowerSuspending']
 .map(name=>{const node=appSource.match(new RegExp('function '+name+'\\([^\\n]+\\n[\\s\\S]*?\\n\\}'))?.[0];assert(node,'actual public suspend function missing: '+name);return node;}).join('\n');
assert(appSuspendObserver,'actual root Fan suspend observer missing');
const autoStartState=appSource.indexOf('let fanAutoStartChain:');
const autoStartEnd=appSource.indexOf('const router = useRouter();',autoStartState);
assert(autoStartState>=0&&autoStartEnd>autoStartState,'actual root Fan auto state missing');
const autoStartSource=appSource.slice(autoStartState,autoStartEnd);
const appReadyObserver=appSource.match(/function onFanResumeReady\([^\n]+\n[\s\S]*?\n\}/)?.[0];
const appCommittedObserver=appSource.match(/function requestFanCommittedResume\([^\n]+\n[\s\S]*?\n\}/)?.[0];
const appSharedFanObserver=appSource.match(/function onSharedFanWake\(state: SharedPowerLifecycleSnapshot\): void \{[\s\S]*?\n\}/)?.[0];
const appFanWakeSubscription=appSource.match(/onSharedPowerLifecycle\(onSharedFanWake,\s*(\{[^}]*\})\)/)?.[1];
assert(appFanWakeSubscription && appSharedFanObserver,'actual shared Fan wake subscription missing');
assert(autoStartSource.includes('fanHostLifecycle.start(')&&appReadyObserver&&appCommittedObserver,'actual root auto/ready observer missing');
const viewPath=process.argv[4]?path.resolve(process.argv[4]):path.join(project,'src/views/FanView.vue');
const viewSource=fs.readFileSync(viewPath,'utf8');
const viewFunction=name=>{const match=viewSource.match(new RegExp('(?:async )?function '+name+'\\([\\s\\S]*?\\n\\}'));assert(match,'actual page function missing: '+name);return match[0];};
const pageFunctions=['onFanPowerSuspending','ensureSupported','confirmFanHostBeforeWrite','commitControlCurve','publishControlIntent','hostHasLiveControl'].map(viewFunction).join('\n');
const observerModule=`export function makeFanWakeApp(fanHostLifecycle: any, curve: any[], settings: {on:boolean; readBarrier?:Promise<void>}, sharedPower: any) {
 const aiFanMockActive=false, resumeLifecycleActive=true;
 let latestResumeGeneration=4, resumeRetryTimer=null, resumeVideoTimer=null;
 const resumeRetryCounts=new Map(),resumeQueue={invalidate(){}},tdpRuntimeStore={invalidatePower(){}};
 const suspendBackgroundVideo=()=>{};
 const {getSharedPowerLifecycle,isSharedPowerLifecycleAdmitted,onSharedPowerLifecycle,invoke}=sharedPower;
 const readSettingsSection=async<T>()=>{if(settings.readBarrier)await settings.readBarrier;return {fanControl:settings.on};}, readBootModeFanIntent=async()=>null;
 const getFanFeatureSettings=()=>({featureEnabled:true,preset:'balanced'}),getFanPresetCurve=()=>curve;
 const fanDiagnosticLog=()=>{};
 const setFanControlActive=()=>{};
 ${autoStartSource}
 ${appReadyObserver}
 ${appCommittedObserver}
 ${appSuspendObserver}
 ${appPublicSuspendPath}
 ${appSharedFanObserver}
 return {ensureFanAutoStart,onFanResumeReady,requestFanCommittedResume,onFanPhysicalSuspending,onPowerSuspending,watchSharedWake(){
  return sharedPower.onSharedPowerLifecycle(onSharedFanWake,${appFanWakeSubscription});
 }};
}
export function makeFanPage(fanHostLifecycle:any, curve:any[]) {
 const ref=(value:any)=>({value}), FAN_FORCE_PREVIEW=false;
 let controlCommitEpoch=1, fanPowerGeneration=fanHostLifecycle.coordinatorSnapshot.powerGeneration;
 let fanSuspendBoundary=false,fanWakeProtectionDeadline=0,fanControlSwitchProtectionDeadline=0;
 let controlCommitNodes=curve;
 const controlCommitPending=ref(true),controlActive=ref(true),recovering=ref(true),closing=ref(false),busy=ref(true),connecting=ref(true);
 const supported=ref(false),controlReady=ref(false),startAttempted=ref(false),hostState=ref('stopped'),statusMessage=ref('');
 const hardwareControlConfirmed=ref(false),nodes=ref(curve),selectedPreset=ref('balanced'),expectedDuty=ref(65);
 const syncControlSwitchLock=()=>{},setFanControlActive=()=>{},setFanNavigationDuty=()=>{},stopTelemetry=()=>{},startTelemetry=()=>{};
 const adoptConfirmedControlSupport=()=>{},refreshTelemetry=async()=>{},recordFanHandshake=async()=>{},fanDiagnosticLog=()=>{},readableFanError=(e:any)=>String(e);
 ${pageFunctions}
 return {onFanPowerSuspending,begin(){fanHostLifecycle.stagePendingControlCurve(curve);return commitControlCurve(controlCommitEpoch,fanHostLifecycle.controlCancellationRevision);},snapshot(){return {controlCommitEpoch,controlActive:controlActive.value,recovering:recovering.value,statusMessage:statusMessage.value,hardwareControlConfirmed:hardwareControlConfirmed.value,fanSuspendBoundary,fanWakeProtectionDeadline,fanControlSwitchProtectionDeadline};}};
}`;
const observerBundle=await build({stdin:{contents:observerModule,loader:'ts'},bundle:true,write:false,format:'iife',globalName:'FanRootObserver',platform:'browser',target:'es2022'});
const files=['src/App.vue','src/views/FanView.vue','src/bridge/api.ts','src/bridge/fanHost.ts','src/bridge/ipc.ts','src/bridge/hardwareCoordinator.ts','src/bridge/powerLifecycleState.ts','tools/fixtures/fan_no_io_host.ts','tools/fan_manual_wake_ipc_selftest.mjs'];
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const binding=files.map(p=>({path:p==='src/views/FanView.vue'?viewPath:selected(p),sha256:hash(p==='src/views/FanView.vue'?viewPath:selected(p))}));
const bundle=await build({stdin:{contents:"export {powerLifecycle} from './src/bridge/api';export {FanApiError,HttpFanApiAdapter} from './src/bridge/fanApi';export {FanHostLifecycle} from './src/bridge/fanHost';export {NoIoHost} from './tools/fixtures/fan_no_io_host';export {invoke,rejectAllPending} from './src/bridge/ipc';export {onSharedPowerLifecycle,getSharedPowerLifecycle,isSharedPowerLifecycleAdmitted} from './src/bridge/powerLifecycleState';",resolveDir:project,sourcefile:'fan-manual-wake-ipc-entry.ts'},bundle:true,write:false,format:'iife',globalName:'FanTest',platform:'browser',target:'es2022',define:{'import.meta.env':'{}'},plugins:beforeRoot?[{name:'bound-before-image',setup(b){b.onLoad({filter:/[\\/]src[\\/]bridge[\\/](?:api|fanHost)\.ts$/},a=>({contents:fs.readFileSync(selected(path.relative(project,a.path)),'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));}}]:[]});
const code=bundle.outputFiles[0].text;
function fixture(mode='ack',factoryType='HandheldCompanion.Devices.OneXPlayerX1Mini',qualifiedReceipts=false,readActivity=async()=>null){
 let now=0,id=0,listener,manuals=0,heldReply,rootObserver,releaseCleanup,appWake,pageApi,pageVisible=false;
 let holdStartupRecheck=false;
 let holdFanRead=false, heldFanRead, failedFanReads=0;const settings={on:true,readBarrier:null};
 const timers=new Map(),calls=[];
 const nativeOffReplay=mode.startsWith('startup-native-off-');
 const nativeRawRoute=mode.startsWith('startup-native-raw-')?nativeRawWire[mode.slice('startup-native-raw-'.length)]:null;
 const offDocument=nativeOffReplay?nativeStartupOffWire.off.offDocument:{generation:'7',navigationId:'12'};
 let native={generation:3,phase:'ready',resumeReady:true,inputReady:true,hardwareWritesAllowed:true,hibernateAvailable:true};
 let shared={generation:3,phase:'ready',known:true,hardwareWritesAllowed:true,publicationRevision:1,observedNotificationSequence:10,notificationPending:false};
 if(nativeOffReplay)shared={...shared,observedNotificationSequence:0};
 if(nativeRawRoute)shared={...nativeRawRoute.priorReady};
 const timer=(fn,ms,interval=false)=>{const key=++id,delay=Math.max(1,Number(ms)||1);timers.set(key,{fn,due:now+delay,period:interval?delay:0});return key;};
 const clear=key=>timers.delete(key),win=new EventTarget();
 win.setTimeout=(fn,ms)=>timer(fn,ms);win.clearTimeout=clear;win.setInterval=(fn,ms)=>timer(fn,ms,true);win.clearInterval=clear;
 const deliver=(msg,result,error)=>listener?.({data:error?{id:msg.id,error}:{id:msg.id,result}});
 const publishFanWake=patch=>listener?.({data:{event:'power.lifecycleChanged',data:{...shared,...patch,fanWakeOnly:true}}});
 const hintPowerScope=(generation,suspendSerial,notificationSequence)=>listener?.({data:{event:'power.resumed',data:{
  generation,readbackScope:{suspendSerial,notificationSequence}}}});
 win.chrome={webview:{addEventListener:(_t,fn)=>listener=fn,postMessage(msg){calls.push(msg);
  if(msg.cmd==='fanLog.write'){queueMicrotask(()=>deliver(msg,{ok:true}));return;}
  if(msg.cmd==='window.renderContext'){queueMicrotask(()=>deliver(msg,{
   generation:offDocument.generation,navigationId:'987',documentNavigationId:offDocument.navigationId}));return;}
  if(msg.cmd==='power.fanCancelStartupWake'){
   assert.deepEqual({...msg.args.offDocument},offDocument);
   if(nativeOffReplay){assert.equal(msg.args.offSequence,nativeStartupOffWire.off.offSequence);return;}
   const reply={ok:true,offSequence:msg.args.offSequence,state:'applied'};
   if(mode.startsWith('startup-merged-lost-'))return;
   if(mode.startsWith('startup-merged-held-off')){
    heldReply=()=>deliver(msg,mode==='startup-merged-held-off-failed'?{ok:false}:reply);return;
   }
   queueMicrotask(()=>deliver(msg,reply));return;
  }
  if(msg.cmd==='power.fanStartupOffStatus'){
   const original=calls.filter(c=>c.cmd==='power.fanCancelStartupWake').at(-1);
   assert.deepEqual(msg.args,original.args,'status must observe the original OFF identity');
   if(nativeOffReplay){
    const wire=nativeStartupOffWire;
    const reply=mode==='startup-native-off-stale-document'?wire.staleStatusAfterReload:
     mode==='startup-native-off-applied'?wire.appliedReadback:
     shared.fanStartupWakeBoundary===wire.retiredCandidate.fanStartupWakeBoundary?wire.retiredReadback:wire.missingReadback;
    queueMicrotask(()=>deliver(msg,reply));return;
   }
   const state=mode==='startup-merged-held-off-failed'?'waiting_for_new_boundary':
    mode==='startup-merged-lost-before-reload'?'waiting_for_new_boundary':
    mode==='startup-merged-lost-unreceived'?(shared.suspendSerial===9?'retired':'waiting_for_new_boundary'):
    mode==='startup-merged-lost-superseded'?'superseded':'applied';
   const reply={ok:true,offSequence:msg.args.offSequence+(mode==='startup-merged-lost-wrong-sequence'?1:0),state};
   if(mode==='startup-merged-lost-stale-offer'){heldReply=()=>deliver(msg,reply);return;}
   queueMicrotask(()=>deliver(msg,reply));return;
  }
  if(msg.cmd==='power.lifecycle'){
   const result={...shared};
   if(holdStartupRecheck){holdStartupRecheck=false;heldReply=()=>deliver(msg,result);return;}
   queueMicrotask(()=>deliver(msg,result));return;
  }
  if(msg.cmd==='power.fanClaimStartupWake'){
   if(nativeOffReplay){
    if(mode==='startup-native-off-reloaded'){
     const offer=nativeStartupOffWire.reloadSuccessor;
     assert.deepEqual({...msg.args},{generation:offer.fanStartupWakeGeneration,sharedGeneration:offer.generation,
      suspendSerial:offer.suspendSerial,notificationSequence:offer.observedNotificationSequence,boundary:offer.fanStartupWakeBoundary});
     queueMicrotask(()=>deliver(msg,{ok:true}));return;
    }
    const {off,appliedCandidate,retiredCandidate}=nativeStartupOffWire;
    const offer=mode==='startup-native-off-applied'?appliedCandidate:retiredCandidate;
    assert.deepEqual({...msg.args,offDocument:{...msg.args.offDocument}}, {...off,generation:offer.fanStartupWakeGeneration,
     sharedGeneration:offer.generation,suspendSerial:offer.suspendSerial,notificationSequence:offer.observedNotificationSequence,boundary:offer.fanStartupWakeBoundary});
    queueMicrotask(()=>deliver(msg,{ok:true}));return;
   }
   if(nativeRawRoute||mode==='startup-native-wire'||mode==='startup-native-wire-late-retry'||mode.startsWith('startup-native-wire-commit')){
    const wire=nativeRawRoute??(mode==='startup-native-wire'?nativeStartupWire:mode==='startup-native-wire-late-retry'
     ?nativeStartupWire.lateRetry:nativeStartupWire.commitWithoutNewNotification);
    assert.deepEqual({...msg.args},wire.claim);
    if(mode==='startup-native-wire-commit-held-read'&&calls.filter(c=>c.cmd===msg.cmd).length===1){
     holdStartupRecheck=true;queueMicrotask(()=>deliver(msg,{ok:false}));return;
    }
    queueMicrotask(()=>deliver(msg,wire.accepted));return;
   }
   if(mode.startsWith('startup-merged-')){
    const ok=mode!=='startup-merged-held-off-current'&&
     !(mode==='startup-merged-reload-queued-hint'&&msg.args.generation===9)&&msg.args.boundary===shared.fanStartupWakeBoundary&&
     msg.args.sharedGeneration===shared.generation&&msg.args.suspendSerial===shared.suspendSerial&&
     msg.args.notificationSequence===shared.observedNotificationSequence;
    queueMicrotask(()=>deliver(msg,{ok}));return;
   }
   const first=calls.filter(c=>c.cmd===msg.cmd).length===1;
   if(mode==='startup-claim-held-denial'||mode==='startup-claim-held-read'){
    if(first){
     shared={...shared,observedNotificationSequence:12};
     heldReply=()=>{holdStartupRecheck=mode==='startup-claim-held-read';deliver(msg,{ok:false});};return;
    }
    const ok=shared.notificationPending===false&&msg.args.notificationSequence===shared.observedNotificationSequence;
    queueMicrotask(()=>deliver(msg,{ok}));return;
   }
   if(first&&mode==='startup-claim-lost-reply-claimed'){
    shared={...shared,fanStartupWakeGeneration:0};return;
   }
   if(first&&mode==='startup-claim-lost-reply-unclaimed')return;
   if(first&&mode==='startup-claim-denied-once')shared={...shared,observedNotificationSequence:12};
   queueMicrotask(()=>deliver(msg,{ok:!(first&&mode==='startup-claim-denied-once')}));return;
  }
  if(msg.cmd==='power.fanLifecycle'){
   if(failedFanReads>0){failedFanReads--;queueMicrotask(()=>deliver(msg,null,'inert Fan IPC read failure'));return;}
   const result={...native};
   if(holdFanRead){holdFanRead=false;heldFanRead=()=>deliver(msg,result);return;}
   queueMicrotask(()=>deliver(msg,result));return;
  }
  if(msg.cmd==='http.request'&&mode.startsWith('native-owner')){
   const route=new URL(msg.args.url).pathname;
   if(route==='/api/resume')throw Error('renderer /api/resume must not be posted');
   const body=typeof msg.args.body==='string'?JSON.parse(msg.args.body):{};
   if(route==='/api/cancel-control-intent'){
    host.calls.push('cancel-control-intent');
    queueMicrotask(()=>deliver(msg,{status:200,body:JSON.stringify({ok:true,controlIntentCancelled:true,state:host.snapshot()}),headers:''}));
    return;
   }
   const names={'/api/state':'getState','/api/open':'open','/api/open-events':'openEvents','/api/acquire-control':'acquireControl','/api/enable':'enable','/api/preset':'applyPreset','/api/heartbeat':'heartbeat','/api/disable':'disable','/api/restore':'restoreOem','/api/release-control':'releaseControl','/api/close':'close','/api/shutdown':'shutdown'};
   const fn=names[route];if(!fn)throw Error('unexpected inert HTTP route '+route);
   void Promise.resolve(host[fn](body)).then(value=>{if(route==='/api/acquire-control'){value.leaseId='safe-'+String(value.generation).padStart(32,'0');host.lease={...value};}return deliver(msg,{status:200,body:JSON.stringify({ok:true,...(route==='/api/acquire-control'||route==='/api/heartbeat'?{lease:value}:{state:value})}),headers:''});},error=>{
    if(error?.code!=='FAN_PUBLIC_POWER_SUPERSEDED'&&!(route==='/api/heartbeat'&&error?.code==='LEASE_INVALID'))throw error;
    deliver(msg,{status:409,body:JSON.stringify({ok:false,error:{code:error.code,message:error.message}}),headers:''});
   });
   return;
  }
  if(msg.cmd==='power.fanManualWake'){
   manuals++;assert.equal(msg.args.generation,native.generation,'manual call must bind the current generation');
   const observedWakeOnly=msg.args.observedWakeOnly===true;
   if(mode==='native-owner-wait-evidence'&&!observedWakeOnly&&manuals<=2){
    assert.notEqual(msg.args.continueReadyOnly,true,'evidence can allow the original click to request its unstarted Close');
    const waiting=manuals===1;
    native={...native,phase:waiting?'suspended':'resuming',resumeReady:!waiting,hardwareWritesAllowed:false};
    releaseCleanup=()=>{native={...native,phase:waiting?'resuming':'ready',resumeReady:!waiting,physicalWakeObserved:true,hardwareWritesAllowed:!waiting};};
    queueMicrotask(()=>deliver(msg,{ok:true,generation:native.generation,phase:native.phase,admitted:true,
      reason:waiting?'awaiting_wake_evidence':'already_ready',resumeScheduled:false,resumeDisposition:'awaiting-power'}));
    return;
   }
   if(mode==='native-owner-ack-before-new-sleep'&&!observedWakeOnly&&manuals===1){
    const generation=native.generation;
    native={...native,generation:generation+1,phase:'suspended',resumeReady:false,hardwareWritesAllowed:false};
    queueMicrotask(()=>deliver(msg,{ok:true,generation,phase:'ready',admitted:true,resumeScheduled:true,resumeDisposition:'registered'}));
    return;
   }
   if(mode.startsWith('native-owner-deferred-close')&&!observedWakeOnly&&manuals===1){
    native={...native,phase:'resuming',resumeReady:true,hardwareWritesAllowed:false};
    releaseCleanup=()=>{native={...native,phase:'ready',resumeReady:true,hardwareWritesAllowed:true};};
    queueMicrotask(()=>deliver(msg,{ok:true,generation:native.generation,phase:'ready',admitted:true,resumeScheduled:false,resumeDisposition:'awaiting-power'}));
    return;
   }
   if (mode.startsWith('native-owner-deferred-close-') && msg.args.continueReadyOnly === true && manuals === 2) {
    if(mode.endsWith('-lost'))return;
    if(mode.endsWith('-suspending-cleared')||mode.endsWith('-suspending-held')){
     native={...native,phase:'suspending',hardwareWritesAllowed:false,notificationPending:true};
     releaseCleanup=()=>{native={...native,phase:'ready',hardwareWritesAllowed:true,notificationPending:false};};
     queueMicrotask(()=>deliver(msg,{ok:false,generation:native.generation,phase:'suspending',admitted:false,reason:'power_suspending'}));
     return;
    }
    // A new Windows notice crosses the renderer Ready read and the Native gate.
    const suspended=mode.endsWith('-suspended');
    native={...native,phase:suspended?'suspended':'resuming',hardwareWritesAllowed:false,notificationPending:!mode.endsWith('-settled-suspended')};
    releaseCleanup=()=>{native={...native,phase:'ready',hardwareWritesAllowed:true,notificationPending:false};};
    if(mode.endsWith('-capture'))queueMicrotask(()=>deliver(msg,{ok:true,generation:native.generation,phase:'ready',admitted:true,resumeScheduled:false,resumeDisposition:'awaiting-power'}));
    else queueMicrotask(()=>deliver(msg,{ok:false,generation:native.generation,phase:suspended||mode.endsWith('-resuming')?native.phase:'ready',admitted:false,reason:'wake_not_ready'}));
    return;
   }
   const rejected=native.phase==='suspending'||mode==='reject'||(observedWakeOnly&&(!native.resumeReady||!native.hardwareWritesAllowed||native.notificationPending))
    ||(msg.args.continueReadyOnly===true&&(native.phase!=='ready'||!native.hardwareWritesAllowed||native.notificationPending));
   if(!observedWakeOnly&&mode!=='missing-unexecuted'&&!rejected)native={...native,phase:'ready',resumeReady:true,hardwareWritesAllowed:true};
   const result={ok:!rejected,generation:native.generation,phase:native.phase,admitted:!rejected,reason:observedWakeOnly?(rejected?'wake_not_ready':'observed_wake'):(rejected?'suspend_in_progress':'manual_fan_wake')};
   if(mode.startsWith('native-owner')&&!rejected){
    // Native dedicated F5/device leaf is inert; the original Host fixture supplies its completion.
    if(mode!=='native-owner-unexecuted')void host.resume({generation:native.generation});
    if(mode==='native-owner-missing'||mode==='native-owner-unexecuted'){heldReply=()=>deliver(msg,result);return;}
   }
   if(mode.startsWith('missing')){heldReply=()=>deliver(msg,result);return;}
   if(mode.startsWith('wake-cleanup')){
    native={...native,phase:'resuming',resumeReady:true,hardwareWritesAllowed:false};
    queueMicrotask(()=>{if(mode!=='wake-cleanup-held-event')notifySuspend({generation:native.generation,scope:'fan-only',resumeCatchup:true});deliver(msg,{...result,phase:'ready'});});
    releaseCleanup=()=>{native={...native,phase:'ready',resumeReady:true,hardwareWritesAllowed:true};};return;
   }
   queueMicrotask(()=>deliver(msg,result));return;
  }
  throw Error('unexpected IPC '+msg.cmd);
 }}};
 const context=vm.createContext({window:win,document:Object.assign(new EventTarget(),{hidden:false,visibilityState:'visible',createElement:()=>({style:{},content:{}}),querySelector:()=>null}),console,Event,EventTarget,
  CustomEvent:class extends Event{constructor(t,i){super(t);this.detail=i?.detail;}},setTimeout:win.setTimeout,clearTimeout:clear,setInterval:win.setInterval,clearInterval:clear,
   URL,TextEncoder,TextDecoder,AbortController,crypto:webcrypto,performance:{now:()=>now},structuredClone,queueMicrotask});
 vm.runInContext(code,context,{timeout:5000});vm.runInContext(observerBundle.outputFiles[0].text,context,{timeout:5000});const api=context.FanTest;
 const notifySuspend=(detail,type='ipc:fan.suspending')=>{rootObserver({detail});if(pageVisible)pageApi.onFanPowerSuspending(new context.CustomEvent(type,{detail}));};
 const observe=p=>{const result={status:'pending',value:undefined,error:null};void p.then(v=>{result.status='fulfilled';result.value=v;},e=>{result.status='rejected';result.error=String(e);});return result;};
 async function flush(){for(let n=0;n<80;n++)await Promise.resolve();await new Promise(setImmediate);}
 async function advance(ms){await flush();const target=now+ms;for(let n=0;n<1000;n++){const row=[...timers.entries()].filter(([,t])=>t.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];if(!row)break;const [key,t]=row;now=t.due;if(t.period)t.due+=t.period;else timers.delete(key);t.fn();await flush();}now=target;await flush();}
 let life,host,starts=0;
 let retireWakeBeforeSpawn=mode==='lost-host-before-spawn';
 const curve=[{tempC:0,dutyPercent:20},{tempC:40,dutyPercent:35},{tempC:70,dutyPercent:65},{tempC:100,dutyPercent:100}];
 async function boot(coldWake=false,startHost=true){host=new api.NoIoHost();host.factoryType=factoryType;host.generation=startHost?3:0;host.attempt=startHost?1:0;host.powerState='On';host.completeResume=true;
  if(qualifiedReceipts){let writtenCurve=null;const snapshot=host.snapshot.bind(host),enable=host.enable.bind(host);
   host.snapshot=()=>({...snapshot(),...(writtenCurve?{activeCurve:structuredClone(writtenCurve),lastControlWriteEvidence:'transported'}:{})});
   host.enable=async(nodes)=>{writtenCurve=structuredClone(nodes);return enable();};
  }
  life=new api.FanHostLifecycle({enabled:true,adapter:host,readNativeActivity:()=>readActivity(),
   launcher:{start:async(_config,assertCurrent)=>{
    if(starts===1&&retireWakeBeforeSpawn){
     retireWakeBeforeSpawn=false;
     // The coordinator knows the old grant is gone before the renderer does.
     native={...native,phase:'resuming',notificationPending:true,hardwareWritesAllowed:false,resumeReady:false};
    }
    await assertCurrent?.('spawn');
    return {pid:111501+starts++,executable:'inert.exe'};
   },stop:async()=>{}},onControlDisplay(){}});
  appWake=context.FanRootObserver.makeFanWakeApp(life,curve,settings,api);rootObserver=appWake.onFanPhysicalSuspending;
  if(mode!=='crash-fresh-mount'){life.setPowerGeneration(3);life.observePowerBoundary('resume-ready',3);}if(startHost)await life.start();
  if(coldWake){native={...native,generation:4,phase:'resuming',resumeReady:false,hardwareWritesAllowed:false};if(mode!=='crash-fresh-mount'){life.setPowerGeneration(4);life.observePowerBoundary('resuming',4);}host.calls=[];pageApi=context.FanRootObserver.makeFanPage(life,curve);return;}
  await life.apply(curve);
  native={...native,generation:4,phase:'suspended',resumeReady:false,hardwareWritesAllowed:false};
  life.setPowerGeneration(4);life.observePowerBoundary('suspending',4);await life.suspend();await host.suspend();host.generation=4;host.lease=null;host.calls=[];
 }
 async function begin(){const pending=observe(mode.startsWith('wake-cleanup')?life.start({manualRecovery:true}).then(()=>life.apply(curve)):life.apply(curve));for(let n=0;n<20&&!calls.some(c=>c.cmd==='power.fanManualWake')&&pending.status==='pending';n++)await flush();return pending;}
 async function done(){const close=life?.close().catch(()=>{});api.rejectAllPending('fixture cleanup');await flush();if(close)await close;timers.clear();}
 const holdStartupPreference=()=>{
  let release,fail;settings.readBarrier=new Promise((resolve,reject)=>{release=resolve;fail=reject;});
  return error=>{settings.readBarrier=null;if(error)fail(error);else release();};
 };
 return {api,calls,timers,flush,advance,observe,boot,begin,done,curve,publishFanWake,hintPowerScope,get host(){return host;},get life(){return life;},get starts(){return starts;},get manuals(){return manuals;},get native(){return native;},auto(reason='boot'){return observe(appWake.ensureFanAutoStart(reason,reason==='resume'?4:0));},rootReady(generation=4){appWake.onFanResumeReady(generation);},rootCommittedReady(generation=4){appWake.requestFanCommittedResume(generation);},rootPublicSuspend(generation){appWake.onPowerSuspending({detail:{generation}});},watchSharedWake(){return appWake.watchSharedWake();},publishShared(patch){shared={...shared,...patch};listener?.({data:{event:'power.lifecycleChanged',data:{...shared}}});},setPreference(on){settings.on=on;},setNative(v){native={...native,...v};},pageBegin(){pageVisible=true;return observe(pageApi.begin());},get page(){return pageApi.snapshot();},pageSuspend(detail,type='ipc:fan.suspending'){pageApi.onFanPowerSuspending(new context.CustomEvent(type,{detail}));},rootSuspend(detail,type){notifySuspend(detail,type);},releaseCleanup(){releaseCleanup?.();},lateReply(){heldReply?.();},failFanReads(count){failedFanReads=count;},holdNextFanRead(){holdFanRead=true;},releaseFanRead(){assert(heldFanRead,'a real Fan read must be in flight');const reply=heldFanRead;heldFanRead=undefined;reply();},holdStartupPreference};
}
const results=[];
async function check(name,run){if(nativeOwnerCancelOnly&&!name.startsWith('native-owner cancel '))return;if(nativeOwnerRearmOnly&&!name.startsWith('native-owner real HTTP'))return;if(nativeOwnerOnly&&!name.startsWith('native-owner '))return;if(sharedReadyOnly&&!name.startsWith('shared-ready '))return;if(backgroundReadyOnly&&!name.startsWith('background-ready '))return;try{const facts=await run();results.push({name,ok:true,...(facts?{facts}:{})});console.log('PASS '+name);}catch(error){results.push({name,ok:false,error:String(error)});console.log('FAIL '+name+': '+error);}}
await check('manual-wake API retires a missing acknowledgement within three seconds',async()=>{const f=fixture('missing-committed');try{f.setNative({generation:4,phase:'suspended'});const p=f.observe(f.api.powerLifecycle.manualFanWake(4));await f.advance(2999);assert.equal(p.status,'pending');await f.advance(1);assert.equal(p.status,'rejected','missing manual wake acknowledgement never settled');assert.match(p.error,/IPC command timed out: power.fanManualWake/);assert.equal(f.manuals,1);assert.equal(f.timers.size,0);}finally{await f.done();}});
await check('same click observes a committed wake after lost reply and completes actual Open/Events/acquire/enable',async()=>{const f=fixture('missing-committed');try{await f.boot();const p=await f.begin();assert.equal(f.manuals,1);assert.equal(p.status,'pending');await f.advance(4001);assert.equal(p.status,'fulfilled',p.error??'same click remains blocked');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);assert.equal(f.manuals,1,'unknown native mutation is never blindly repeated');assert.equal(f.starts,1,'same exact Host owns the operation');f.lateReply();await f.flush();assert.equal(f.host.calls.filter(c=>c==='enable').length,1);return {manualRequests:f.manuals,starts:f.starts,trace:f.host.calls};}finally{await f.done();}});
await check('normal acknowledged rescue retains the same one-click path',async()=>{const f=fixture();try{await f.boot();const p=await f.begin();await f.flush();assert.equal(p.status,'fulfilled',p.error??'normal manual rescue pending');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.starts,1);}finally{await f.done();}});
for(const factoryType of ['HandheldCompanion.Devices.MSI.ClawA1M','HandheldCompanion.Devices.MSI.ClawA2VM','HandheldCompanion.Devices.MSI.ClawBZ2EM','HandheldCompanion.Devices.MSI.ClawCG3EM','HandheldCompanion.Devices.XboxROGAllyX','HandheldCompanion.Devices.OneXPlayerX1Mini','HandheldCompanion.Devices.LegionGoTablet'])await check('first cold wake click survives its own deferred Close notification: '+factoryType,async()=>{const f=fixture('wake-cleanup',factoryType);try{await f.boot(true);const revision=f.life.controlIntentRevision;const p=await f.begin();await f.flush();assert.equal(f.manuals,1,JSON.stringify({status:p.status,error:p.error,calls:f.host.calls,native:f.native,life:f.life.state}));assert.equal(f.life.controlIntentRevision,revision,'cleanup invalidated the current explicit intent');assert.equal(p.status,'pending',p.error??'same click was cancelled');assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'),'cleanup ACK must not grant hardware writes');f.releaseCleanup();await f.advance(1200);assert.equal(p.status,'fulfilled',p.error??'same click did not continue');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);assert.equal(f.manuals,1);assert.equal(f.starts,1);return {outcome:p.status,manualRequests:f.manuals,starts:f.starts,trace:f.host.calls};}finally{await f.done();}});
for(const cancel of ['off','new-sleep','same-generation-sleep','legacy-sleep','wrong-scope'])await check('deferred Close catchup cannot revive '+cancel,async()=>{const f=fixture('wake-cleanup');try{await f.boot(true);const p=await f.begin();await f.flush();if(cancel==='off'){const off=f.observe(f.life.disable());await f.advance(1200);assert.equal(off.status,'fulfilled',off.error??'OFF did not settle');}else{const generation=cancel==='new-sleep'?5:4;f.setNative({generation,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});f.rootSuspend({generation,scope:cancel==='wrong-scope'?'global':'fan-only',...(cancel==='legacy-sleep'?{}:{resumeCatchup:cancel==='wrong-scope'})});}f.releaseCleanup();await f.advance(4001);assert.notEqual(p.status,'pending');assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'),'cancelled catchup wrote');}finally{await f.done();}});
await check('actual authorized auto boot survives its wake Close catchup without a second user click',async()=>{const f=fixture('wake-cleanup');try{await f.boot(true);const p=f.auto();await f.flush();assert.equal(f.manuals,1);assert.equal(p.status,'pending');assert(!f.host.calls.includes('enable'));f.releaseCleanup();await f.advance(1200);assert.equal(p.status,'fulfilled',p.error??'auto startup stuck');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.starts,1);return {manualRequests:f.manuals,starts:f.starts,trace:f.host.calls};}finally{await f.done();}});
await check('authorized automatic resume waits for public Close completion without a manual wake request',async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);let stop;
 try{
  await f.boot(true);await f.host.suspend();f.host.calls=[];
  stop=f.watchSharedWake();await f.flush();
  f.setNative({known:true,generation:4,phase:'resuming',resumeReady:true,hardwareWritesAllowed:false});
  f.publishFanWake({fanWakeGeneration:4,publicationRevision:2});await f.advance(1600);
  assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,4);
  assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'));
  assert.equal(f.manuals,0);
  await f.host.resume({generation:4});
  f.setNative({phase:'ready',hardwareWritesAllowed:true});await f.advance(5000);
  assert.equal(f.host.calls.filter(v=>v==='enable').length,1);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  return {trace:f.host.calls,manualRequests:f.manuals,starts:f.starts};
 }finally{stop?.();await f.done();}
});
for(const revoke of ['preference','off'])await check('actual authorized auto rechecks '+revoke+' before writing after cleanup',async()=>{const f=fixture('wake-cleanup');try{await f.boot(true);const p=f.auto();await f.flush();if(revoke==='preference')f.setPreference(false);else{const off=f.observe(f.life.disable());await f.advance(1200);assert.equal(off.status,'fulfilled',off.error??'OFF blocked');}f.releaseCleanup();await f.advance(1200);assert.notEqual(p.status,'pending');assert(!f.host.calls.includes('enable'),'automatic startup ignored revoke');}finally{await f.done();}});
await check('delayed Close catchup preserves the first start before and after its actual Ready',async()=>{
 const f=fixture('wake-cleanup-held-event');
 try{
  await f.boot(true);const p=await f.begin();await f.flush();assert.equal(p.status,'pending');
  const revision=f.life.controlIntentRevision;
  const catchup={generation:4,scope:'fan-only',resumeCatchup:true};
  f.rootSuspend(catchup);
  assert.equal(f.life.controlIntentRevision,revision);
  f.releaseCleanup();await f.advance(1200);
  assert.equal(p.status,'fulfilled',p.error??'delayed event cancelled current start');
  assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
  const ready=f.life.coordinatorSnapshot;
  assert.equal(ready.phase,'ready');assert.equal(ready.wakeReadyGeneration,4);
  const calls=[...f.host.calls];
  f.rootSuspend(catchup);await f.flush();
  assert.deepEqual(f.life.coordinatorSnapshot,ready,'late Close catchup revoked already admitted Ready');
  assert.equal(f.life.controlReady,true);
  assert.deepEqual(f.host.calls,calls,'late catchup must not reopen or replay the confirmed write');
 }finally{await f.done();}
});
await check('duplicate catchup while Close is pending has no second native request or write',async()=>{const f=fixture('wake-cleanup');try{await f.boot(true);const p=await f.begin();await f.flush();const revision=f.life.controlIntentRevision;for(let n=0;n<4;n++)f.rootSuspend({generation:4,scope:'fan-only',resumeCatchup:true});assert.equal(f.life.controlIntentRevision,revision);assert(!f.host.calls.includes('enable'));f.releaseCleanup();await f.advance(1200);assert.equal(p.status,'fulfilled',p.error??'duplicate catchup blocked start');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.manuals,1);assert.equal(f.starts,1);}finally{await f.done();}});
await check('stale cleanup generation cannot cancel a current pending start',async()=>{const f=fixture('wake-cleanup');try{await f.boot(true);const p=await f.begin();await f.flush();const revision=f.life.controlIntentRevision;f.rootSuspend({generation:3,scope:'fan-only',resumeCatchup:true});assert.equal(f.life.controlIntentRevision,revision);f.releaseCleanup();await f.advance(1200);assert.equal(p.status,'fulfilled',p.error??'stale catchup blocked start');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);}finally{await f.done();}});
await check('new generation even with catchup marker cancels the old request before writes',async()=>{const f=fixture('wake-cleanup');try{await f.boot(true);const p=await f.begin();await f.flush();f.setNative({generation:5,phase:'resuming',resumeReady:false,hardwareWritesAllowed:false});f.rootSuspend({generation:5,scope:'fan-only',resumeCatchup:true});f.releaseCleanup();await f.advance(4001);assert.notEqual(p.status,'pending');assert(!f.host.calls.includes('enable'));}finally{await f.done();}});
await check('repeated actual root ready notices do not duplicate a successful current write',async()=>{const f=fixture('wake-cleanup',undefined,true);try{await f.boot(true);const p=await f.begin();f.releaseCleanup();await f.advance(1200);assert.equal(p.status,'fulfilled',p.error??'initial recovery failed');f.rootReady();f.rootReady();await f.advance(1200);assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'root ready replayed control');assert.equal(f.starts,1);return {trace:[...f.host.calls],receipt:structuredClone(f.host.snapshot()),fixtureEvidence:'curve copied only at inert writer invocation; no physical hardware proof'};}finally{await f.done();}});
for(const factoryType of ['HandheldCompanion.Devices.MSI.ClawA1M','HandheldCompanion.Devices.MSI.ClawA2VM','HandheldCompanion.Devices.MSI.ClawBZ2EM','HandheldCompanion.Devices.MSI.ClawCG3EM','HandheldCompanion.Devices.XboxROGAllyX','HandheldCompanion.Devices.OneXPlayerX1Mini','HandheldCompanion.Devices.LegionGoTablet'])await check('actual page commit survives native Close catchup: '+factoryType,async()=>{const f=fixture('wake-cleanup',factoryType,true);try{await f.boot(true);const p=f.pageBegin();await f.flush();f.releaseCleanup();await f.advance(1200);assert.equal(p.status,'fulfilled',p.error??'page commit failed');assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'actual page dropped the original commit before apply');assert.equal(f.page.controlCommitEpoch,1,'cleanup retired the page intent');assert.equal(f.page.hardwareControlConfirmed,true,'page did not confirm the write');assert.equal(f.starts,1);return {page:f.page,trace:[...f.host.calls]};}finally{await f.done();}});
for(const cancel of ['off','new-sleep','same-generation-sleep','legacy-sleep','wrong-scope'])await check('actual page pending commit cannot revive '+cancel,async()=>{const f=fixture('wake-cleanup',undefined,true);try{await f.boot(true);const p=f.pageBegin();await f.flush();if(cancel==='off'){const off=f.observe(f.life.disable());await f.advance(1200);assert.equal(off.status,'fulfilled',off.error??'OFF blocked');}else{const generation=cancel==='new-sleep'?5:4;f.setNative({generation,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});f.rootSuspend({generation,scope:cancel==='wrong-scope'?'global':'fan-only',...(cancel==='legacy-sleep'?{}:{resumeCatchup:cancel==='wrong-scope'})});assert.equal(f.page.controlCommitEpoch,2,'real sleep must retire the page epoch');}f.releaseCleanup();await f.advance(4001);assert.notEqual(p.status,'pending');assert(!f.host.calls.includes('enable'),'cancelled page commit wrote');return {page:f.page};}finally{await f.done();}});
await check('shared power sleep is never exempted by Fan catchup-shaped fields',async()=>{const f=fixture('wake-cleanup',undefined,true);try{await f.boot(true);f.pageSuspend({generation:4,scope:'fan-only',resumeCatchup:true},'ipc:power.suspending');assert.equal(f.page.controlCommitEpoch,2);assert.equal(f.page.fanSuspendBoundary,true);}finally{await f.done();}});
await check('duplicate root ready with incomplete Host receipts keeps the existing write fallback',async()=>{const f=fixture('wake-cleanup');try{await f.boot(true);const p=await f.begin();f.releaseCleanup();await f.advance(1200);assert.equal(p.status,'fulfilled');f.rootReady();f.rootReady();await f.advance(1200);assert.equal(f.host.calls.filter(c=>c==='enable').length,2,'missing write evidence must not become a no-write success');return {trace:[...f.host.calls],classification:'expected fallback under deliberately incomplete inert Host evidence'};}finally{await f.done();}});
await check('a lost unexecuted request cannot fabricate a ready/write fact',async()=>{const f=fixture('missing-unexecuted');try{await f.boot();const p=await f.begin();await f.advance(4001);const requestAt=f.calls.findIndex(c=>c.cmd==='power.fanManualWake');assert(requestAt>=0&&f.calls.slice(requestAt+1).some(c=>c.cmd==='power.fanLifecycle'),'native facts must be reread after the unknown result');assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'),'still-suspended native cannot grant writes');assert.equal(f.native.phase,'suspended');return {outcome:p.status,trace:f.host.calls};}finally{await f.done();}});
await check('OFF during a lost acknowledgement cancels the old write, and the next manual ON remains reachable',async()=>{const f=fixture('missing-committed');try{await f.boot();const p=await f.begin();const off=f.observe(f.life.disable());await f.advance(4001);assert.notEqual(p.status,'pending','old native observer still owns the queue');assert.equal(off.status,'fulfilled',off.error??'OFF still queued');assert(!f.host.calls.includes('enable'),'cancelled intent cannot write after timeout');f.lateReply();await f.flush();assert(!f.host.calls.includes('enable'),'late native success cannot revive OFF');const next=f.observe(f.life.apply(f.curve));await f.advance(4001);assert.equal(next.status,'fulfilled',next.error??'next manual control pending');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.starts,1);}finally{await f.done();}});
await check('a new sleep generation retires the delayed old manual request',async()=>{const f=fixture('missing-committed');try{await f.boot();const p=await f.begin();f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});f.life.setPowerGeneration(5);f.life.observePowerBoundary('suspending',5);await f.advance(4001);assert.notEqual(p.status,'pending','old-generation IPC observer still owns the request');assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'));f.lateReply();await f.flush();assert(!f.host.calls.includes('enable'));assert.equal(f.starts,1);}finally{await f.done();}});
await check('actual suspending native still rejects the same manual request with zero writes',async()=>{const f=fixture('reject');try{await f.boot();f.setNative({phase:'suspending'});const p=await f.begin();await f.advance(4001);assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'));assert.equal(f.native.phase,'suspending');return {outcome:p.status,manualRequests:f.manuals};}finally{await f.done();}});
await check('idle HC Open admission rereads native after a missing promotion acknowledgement',async()=>{const f=fixture('missing-committed');try{await f.boot();f.life.observePowerBoundary('resume-ready',4);f.setNative({phase:'resuming',resumeReady:true});f.host.stateName='AwaitingControl';f.host.powerState='On';f.host.opened=false;f.host.events=false;f.host.lease=null;const probe={...f.host.snapshot(),powerOperationOutcome:'await-user'};const context=f.life.admissionContext('idle-open-ipc-check',f.life.controlIntentRevisionValue);context.manualRecovery=true;const p=f.observe(f.life.completeHcOpenForAdmission(probe,context));await f.flush();assert.equal(f.manuals,1);await f.advance(4001);assert.equal(p.status,'fulfilled',p.error??'idle HC Open still pending');assert.equal(f.host.calls.filter(c=>c==='open').length,1);assert.equal(f.host.calls.filter(c=>c==='events').length,1);assert(!f.host.calls.includes('enable')&&!f.host.calls.includes('acquire'),'Open admission alone cannot establish writes');}finally{await f.done();}});
await check('unbound session retirement reaches the original Close after a missing promotion acknowledgement',async()=>{const f=fixture('missing-committed');let release,previousClose,p;try{await f.boot();f.host.generation=0;f.host.attempt=0;f.host.stateName='Open';f.host.powerState='On';f.host.opened=true;f.host.events=false;f.host.lease=null;previousClose=f.host.close.bind(f.host);f.host.close=async()=>{f.host.calls.push('held-close-enter');await new Promise(r=>release=r);throw Error('fixture ends at real Close admission');};p=f.observe(f.life.restartUnboundHcSessionForManualControl({manualRecovery:true},true));await f.flush();assert.equal(f.manuals,1);await f.advance(4001);assert(f.host.calls.includes('held-close-enter'),p.error??'unbound retirement did not reach Close');assert.equal(f.starts,1,'no replacement until original owner has closed/exited');assert(!f.host.calls.includes('enable')&&!f.host.calls.includes('acquire'));}finally{if(previousClose)f.host.close=previousClose;if(release)release();if(p)await f.flush();await f.done();}});
for(const b of binding)assert.equal(hash(b.path),b.sha256,'source changed during gate: '+b.path);
for(const kind of ['unknown','pending','stale'])await check('shared-ready '+kind+' event cannot open a private recovery generation',async()=>{
 const f=fixture('ack',undefined,true);try{await f.boot();f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,...(kind==='unknown'?{known:false}:kind==='pending'?{known:true,notificationPending:true}:{generation:3})});const before=f.life.coordinatorSnapshot;f.rootReady(4);await f.advance(2000);assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,before.wakeReadyGeneration,'raw Ready bypassed current shared power');assert(!f.host.calls.includes('enable'),'unadmitted notification replayed curve');return {trace:[...f.host.calls],before,after:f.life.coordinatorSnapshot};}finally{await f.done();}
});
await check('shared-ready valid device receipt resumes once without input readiness',async()=>{
 const f=fixture('ack',undefined,true);try{await f.boot();f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:false,known:true,notificationPending:false});await f.host.resume({generation:4});f.rootReady(4);await f.advance(2000);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(v=>v==='enable').length,1);f.rootReady(4);await f.advance(2000);assert.equal(f.host.calls.filter(v=>v==='enable').length,1,'duplicate current notification replayed confirmed curve');return {trace:[...f.host.calls],state:f.life.state};}finally{await f.done();}
});
for(const superseded of [false,true])await check('shared-ready '+(superseded?'new Fan sleep retires the older auto-start preference read':'confirmed Fan-only generation can auto-start without a public UI resume'),async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop,releasePreference;
 try{
  await f.boot(true);
  assert.equal(f.life.hasControlIntent,false);
  stop=f.watchSharedWake();
  f.publishShared({suspendSerial:12});await f.flush();
  f.setNative({generation:5,sharedGeneration:3,suspendSerial:12,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,
   inputReady:false,known:true,notificationPending:false,scope:'fan-only',physicalWakeObserved:true,sharedPowerCommitted:false});
  await f.host.resume({generation:5});
  if(superseded)releasePreference=f.holdStartupPreference();
  f.publishFanWake({fanWakeGeneration:5});await f.advance(2000);
  assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,5,'the accepted Fan wake must be confirmed before auto start');
  if(superseded){
   assert(!f.host.calls.includes('enable'),'preference read did not pause the accepted request');
   f.setNative({generation:6,phase:'suspended',resumeReady:false,hardwareWritesAllowed:false});
   f.rootSuspend({generation:6,scope:'fan-only'});
   releasePreference();await f.advance(2000);
   assert.equal(f.life.coordinatorSnapshot.powerGeneration,6);
   assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'),'stale auto-start wrote after the new Fan sleep');
   assert.equal(f.manuals,0,'stale auto-start tried to promote the new sleep');
   return {trace:[...f.host.calls],manualRequests:f.manuals};
  }
  assert.equal(f.host.calls.filter(call=>call==='enable').length,1,'validated Fan wake must use the armed preference even when the public UI generation differs');
  assert.equal(f.manuals,0,'automatic work must join the accepted wake without requesting manual promotion');
  assert.equal(f.api.getSharedPowerLifecycle().generation,3,'Fan recovery must not advance the public generation');
  f.publishFanWake({fanWakeGeneration:5});await f.advance(2000);
  assert.equal(f.host.calls.filter(call=>call==='enable').length,1);
  return {trace:[...f.host.calls],manualRequests:f.manuals};
 }finally{releasePreference?.();stop?.();await f.done();}
});
for(const [stage,outcome] of [['before-start','continue'],['before-apply','continue'],['before-start','off']])
await check('shared-ready startup preference rechecks permission revoked while reading settings: '+stage+' '+outcome,async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop,releasePreference;
 try{
  await f.boot(true);await f.host.resume({generation:4});f.host.calls=[];
  stop=f.watchSharedWake();await f.flush();
  f.setNative({known:true,generation:4,sharedGeneration:3,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  if(stage==='before-start')releasePreference=f.holdStartupPreference();
  else{
   const start=f.life.start.bind(f.life);
   f.life.start=async options=>{
    const gate=await start(options);
    releasePreference=f.holdStartupPreference();
    return gate;
   };
  }
  f.publishFanWake({fanWakeGeneration:4,publicationRevision:2});await f.flush();
  assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,4);
  assert.equal(typeof releasePreference,'function','the selected settings read was not reached');
  f.setNative({phase:'resuming',resumeReady:false,hardwareWritesAllowed:false,notificationPending:true});
  releasePreference();await f.advance(1600);
  const writes=()=>f.host.calls.filter(v=>v==='enable').length;
  assert.equal(writes(),0,'the startup preference used a revoked wake grant: '+JSON.stringify(f.host.calls));
  assert(!f.host.calls.includes('acquire'),'the startup preference acquired during a revoked grant');
  assert.equal(f.manuals,0,'automatic startup submitted a manual wake');
  if(outcome==='off')await f.life.disable();
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,notificationPending:false});
  await f.advance(5000);
  assert.equal(writes(),outcome==='continue'?1:0,'unexpected continuation of automatic startup');
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  return {stage,outcome,writes:writes(),starts:f.starts,manualRequests:f.manuals};
 }finally{releasePreference?.();stop?.();await f.done();}
});

for(const [stage,outcome] of [['start','continue'],['apply','continue'],['apply','off'],['apply','preference-off'],['start','new-sleep'],['apply','timeout'],['apply','preference-read-timeout']])
await check('shared-ready startup survives a queued public grant refusal: '+stage+' '+outcome,async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop,releasePreference;
 try{
  await f.boot(true);await f.host.resume({generation:4});f.host.calls=[];
  stop=f.watchSharedWake();await f.flush();
  f.setNative({known:true,generation:4,sharedGeneration:3,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  const withdraw=()=>f.setNative({phase:'resuming',resumeReady:false,hardwareWritesAllowed:false,notificationPending:true});
  let submissions=0;
  if(stage==='start'){
   const probe=f.host.getState.bind(f.host);
   f.host.getState=async (...args)=>{
    if(++submissions===1)withdraw();
    return probe(...args);
   };
  }else{
   const apply=f.life.apply.bind(f.life);
   f.life.apply=(...args)=>{
    if(++submissions===1)withdraw();
    return apply(...args);
   };
  }
  f.publishFanWake({fanWakeGeneration:4,publicationRevision:2});await f.advance(1600);
  assert.equal(submissions,1,'the pending grant must not submit repeated work');
  assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'));
  assert.equal(f.manuals,0);
  if(outcome==='off')await f.life.disable();
  if(outcome==='preference-off')f.setPreference(false);
  if(outcome==='new-sleep')f.rootSuspend({generation:5,scope:'fan-only'});
  if(outcome==='timeout')await f.advance(20000);
  if(outcome==='preference-read-timeout')releasePreference=f.holdStartupPreference();
  f.setNative({generation:outcome==='new-sleep'?5:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,notificationPending:false});
  if(releasePreference){await f.advance(20000);releasePreference();}
  await f.advance(5000);
  const writes=f.host.calls.filter(v=>v==='enable').length;
  assert.equal(writes,outcome==='continue'?1:0,'only the current request may resume after the public grant returns');
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  return {stage,outcome,writes,submissions,starts:f.starts,manualRequests:f.manuals};
 }finally{releasePreference?.();stop?.();await f.done();}
});

await check('shared-ready startup never resubmits after an uncertain Host write',async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);let stop;
 try{
  await f.boot(true);await f.host.resume({generation:4});f.host.calls=[];
  stop=f.watchSharedWake();await f.flush();
  f.setNative({known:true,generation:4,sharedGeneration:3,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  let submissions=0;
  f.host.enable=async()=>{
   submissions++;
   f.setNative({phase:'resuming',resumeReady:false,hardwareWritesAllowed:false,notificationPending:true});
   throw new f.api.FanApiError('inert write may have reached a channel',409,'FAN_PUBLIC_POWER_SUPERSEDED');
  };
  f.publishFanWake({fanWakeGeneration:4,publicationRevision:2});await f.advance(1600);
  assert.equal(submissions,1);
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,notificationPending:false});
  await f.advance(5000);
  assert.equal(submissions,1,'a Host failure was incorrectly treated as an unsent write');
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  return {submissions,starts:f.starts,manualRequests:f.manuals};
 }finally{stop?.();await f.done();}
});

await check('shared-ready manual start does not inherit a revoked automatic startup',async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 try{
  await f.boot(true);await f.host.resume({generation:4});
  f.life.observePowerBoundary('resume-ready',4);
  f.setNative({known:true,generation:4,phase:'resuming',resumeReady:false,hardwareWritesAllowed:false,notificationPending:true});
  // The first reachability read belongs to automatic startup. Restore Native
  // permission when the independently queued manual startup reaches its probe.
  let probes=0;
  const probe=f.host.getState.bind(f.host);
  f.host.getState=async (...args)=>{
   if(++probes===2)f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,notificationPending:false});
   return probe(...args);
  };
  f.holdNextFanRead();
  const auto=f.observe(f.life.start({manualRecovery:true,publishedWakeGeneration:4,reason:'authorized-auto-resume'}));
  await f.flush();assert.equal(auto.status,'pending');
  const manual=f.observe(f.life.start({manualRecovery:true,reason:'fixture-user-on'}));
  f.releaseFanRead();await f.flush();
  assert.equal(auto.status,'rejected');assert.match(auto.error,/FAN_AUTOMATIC_NATIVE_WAKE_PENDING/);
  assert(!f.host.calls.includes('enable'));
  await f.advance(5000);
  assert.equal(manual.status,'fulfilled',manual.error??'manual startup did not settle');
  assert(probes>=2,'the explicit click did not reach its own admission');
  await f.life.apply(f.curve);assert.equal(f.host.calls.filter(v=>v==='enable').length,1);
  assert.equal(f.starts,1);
 }finally{await f.done();}
});

await check('shared-ready Fan-only receipt is bound to the public suspend serial',async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot();
  f.setNative({generation:4,sharedGeneration:3,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,
   inputReady:false,known:true,notificationPending:false,scope:'fan-only',physicalWakeObserved:true,sharedPowerCommitted:false});
  await f.host.resume({generation:4});
  stop=f.watchSharedWake();
  f.publishShared({suspendSerial:12});await f.flush();
  f.publishShared({suspendSerial:13});await f.flush();
  const fanReads=()=>f.calls.filter(v=>v.cmd==='power.fanLifecycle').length;
  const before=fanReads();
  f.publishFanWake({suspendSerial:12,fanWakeGeneration:4});await f.advance(2000);
  assert.equal(fanReads(),before,'old suspend serial reached the Fan wake observer');
  assert(!f.host.calls.includes('enable'),'old suspend serial replayed the retained curve');
  f.publishFanWake({suspendSerial:13,fanWakeGeneration:4});await f.advance(2000);
  assert.equal(f.life.state,'ready');
  assert.equal(f.host.calls.filter(v=>v==='enable').length,1);
  assert.equal(f.api.getSharedPowerLifecycle().suspendSerial,13);
  assert.equal(f.api.getSharedPowerLifecycle().fanWakeGeneration,undefined,'Fan-only delivery changed public authority');
  return {staleSerial:12,currentSerial:13,trace:[...f.host.calls]};
 }finally{stop?.();await f.done();}
});
for(const field of ['suspendSerial','observedNotificationSequence']){
 await check('shared-ready in-flight Fan read cannot cross public '+field,async()=>{
  const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
  const admitted=[];
  let stop,stopAdmitted;
  const base={sharedGeneration:3,suspendSerial:12,observedNotificationSequence:10,known:true,notificationPending:false};
  const next={[field]:base[field]+1};
  try{
   await f.boot();await f.host.resume({generation:4});
   stop=f.watchSharedWake();stopAdmitted=f.life.onWakeAdmitted(generation=>admitted.push(generation));
   f.publishShared({suspendSerial:12});await f.flush();
   f.setNative({...base,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
   f.holdNextFanRead();
   f.publishFanWake({fanWakeGeneration:4});await f.flush();
   // The next Ready coalesces the intervening boundary without a Fan event.
   f.publishShared(next);await f.flush();
   f.setNative({...next,phase:'resuming',resumeReady:false,hardwareWritesAllowed:false});
   f.releaseFanRead();await f.advance(2000);
   assert.equal(f.api.getSharedPowerLifecycle()[field],next[field]);
   assert.deepEqual(admitted,[],'retired Fan read granted wake admission');
   assert(!f.host.calls.includes('enable'),'retired Fan read replayed the curve');
   f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
   f.publishFanWake({fanWakeGeneration:4});await f.advance(2000);
   assert.deepEqual(admitted,[4]);
   assert.equal(f.host.calls.filter(call=>call==='enable').length,1);
   assert.equal(f.life.state,'ready');assert.equal(f.manuals,0);
   f.publishFanWake({fanWakeGeneration:4});await f.advance(2000);
   assert.deepEqual(admitted,[4]);
   return {field,old:base[field],current:next[field],wakeAdmissions:admitted.length,nativeManualRequests:f.manuals};
  }finally{stopAdmitted?.();stop?.();await f.done();}
 });
}
for(const hold of ['unknown','pending'])await check('shared-ready in-flight Fan reply cannot clear a public '+hold+' hold at equal ordinals',async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 const admitted=[];
 let stop,stopAdmitted;
 const base={sharedGeneration:3,suspendSerial:12,observedNotificationSequence:10,publicationRevision:1,known:true,notificationPending:false};
 try{
  await f.boot();await f.host.resume({generation:4});
  stop=f.watchSharedWake();stopAdmitted=f.life.onWakeAdmitted(generation=>admitted.push(generation));
  f.publishShared({suspendSerial:12});await f.flush();
  f.setNative({...base,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  f.holdNextFanRead();f.publishFanWake({fanWakeGeneration:4});await f.flush();
  // The retained Fan reply describes the previous Ready, even if Native has
  // not advanced any ordinal in the denial yet.
  f.publishShared(hold==='unknown'?{known:false}:{notificationPending:true});await f.flush();
  f.releaseFanRead();await f.advance(2000);
  assert.deepEqual(admitted,[],'stale Fan reply granted wake admission during a public hold');
  assert.equal(f.host.calls.filter(c=>c==='enable').length,0);
  f.setNative({publicationRevision:2});
  f.publishShared({known:true,notificationPending:false,publicationRevision:2,fanWakeGeneration:4});await f.advance(2000);
  assert.deepEqual(admitted,[4],'current public delivery did not continue');
  assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);
  assert.equal(f.manuals,0);
  return {hold,admitted,activeControlWrites:1};
 }finally{stopAdmitted?.();stop?.();await f.done();}
});
await check('shared-ready fresh Fan read does not require a matching public broadcast',async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot();await f.host.resume({generation:4});
  stop=f.watchSharedWake();
  f.publishShared({suspendSerial:12});await f.flush();
  f.setNative({sharedGeneration:3,suspendSerial:13,observedNotificationSequence:11,known:true,notificationPending:false,
   phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  f.publishFanWake({fanWakeGeneration:4,suspendSerial:13,observedNotificationSequence:11});await f.advance(2000);
  assert.equal(f.host.calls.filter(call=>call==='enable').length,1);
  assert.equal(f.life.state,'ready');assert.equal(f.manuals,0);
  assert.equal(f.api.getSharedPowerLifecycle().suspendSerial,12,'Fan advanced the public suspend scope');
  assert.equal(f.api.getSharedPowerLifecycle().observedNotificationSequence,10,'Fan advanced the public notification sequence');
  return {fanSuspendSerial:13,publicSuspendSerial:12,activeControlWrites:1};
 }finally{stop?.();await f.done();}
});
await check('shared-ready newer coherent Fan proof can precede public recovery',async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot();await f.host.resume({generation:4});
  stop=f.watchSharedWake();
  f.publishShared({suspendSerial:12});await f.flush();
  f.publishShared({notificationPending:true});await f.flush();
  f.setNative({sharedGeneration:3,suspendSerial:12,observedNotificationSequence:10,publicationRevision:2,
   known:true,notificationPending:false,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  f.publishFanWake({fanWakeGeneration:4,publicationRevision:2,notificationPending:false});await f.advance(2000);
  assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);
  assert.equal(f.api.getSharedPowerLifecycle().notificationPending,true,'Fan fact cleared the public hold');
  assert.equal(f.api.getSharedPowerLifecycle().publicationRevision,1);
  assert.equal(f.manuals,0);
  return {fanPublication:2,publicPublication:1,activeControlWrites:1};
 }finally{stop?.();await f.done();}
});
await check('background-ready retained private cycle resumes and renews without opening the fan page',async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 try{
  await f.boot();
  // Field tuple: the Fan device cycle advances while the public cycle stays
  // unchanged. Only the actual root callback runs; no page or manual request.
  f.setNative({generation:4,sharedGeneration:3,phase:'ready',resumeReady:false,
   hardwareWritesAllowed:true,inputReady:false,known:true,notificationPending:false,
   scope:'fan-only',physicalWakeObserved:true,sharedPowerCommitted:false});
  const context=f.life.admissionContext('raw-wake-compensation',f.life.controlIntentRevisionValue,4);
  const observed=await f.life.observeCurrentNativeWakeForControl(context);
  assert.equal(observed,false,'a paired fact alone cannot qualify as automatic recovery');
  f.rootReady(4);await f.advance(150);
  assert(!f.host.calls.some(v=>v==='acquire'||v==='enable'),'unpublished wake was allowed to take over');
  await f.host.resume({generation:4});
  f.setNative({resumeReady:true});
  f.rootReady(4);await f.advance(2000);
  assert.equal(f.life.state,'ready','background App observer did not restore retained control');
  assert.equal(f.host.calls.filter(v=>v==='acquire').length,1);
  assert.equal(f.host.calls.filter(v=>v==='enable').length,1);
  f.rootReady(4);await f.advance(20000);
  assert.equal(f.host.calls.filter(v=>v==='enable').length,1,'duplicate wake replayed a confirmed curve');
  assert(f.host.calls.filter(v=>v==='heartbeat').length>=4,'background lease cadence did not restart');
  assert.equal(f.manuals,0,'background recovery required a manual wake');
  assert.equal(f.starts,1,'background recovery launched another Host');
  assert.equal(f.life.coordinatorSnapshot.powerGeneration,4);
  assert.equal(f.native.sharedGeneration,3,'Fan recovery rewrote the public cycle');
  return {trace:[...f.host.calls],fanGeneration:4,sharedGeneration:3,
   fanPageCreated:false,fanPageActivated:false,manualRequests:f.manuals,starts:f.starts,
   virtualElapsedMs:22000,physicalOrWallClockTimingProven:false};
 }finally{await f.done();}
});

for (const outcome of ['admitted','off','revoked-before-spawn']) await check('background-ready lost Host waits for the coordinator: '+outcome,async()=>{
 const f=fixture(outcome==='revoked-before-spawn'?'lost-host-before-spawn':'ack','HandheldCompanion.Devices.GPDWin5',true);
 try{
  await f.boot();
  f.setPreference(false); // Exercise retained-curve recovery, not a new preference start.
  f.rootSuspend({generation:4,scope:'fan-only'});
  f.setNative({generation:4,sharedGeneration:3,phase:'resuming',resumeReady:false,
   hardwareWritesAllowed:false,known:true,notificationPending:false,physicalWakeObserved:true});
  assert.equal(await f.life.requestRecoveryAfterObservedHostState('lost-host-wake'),true);
  f.life.process=null; // The original process is gone; the launcher is inert.
  await f.advance(1200);
  const earlyLaunches=f.starts-1;
  assert.equal(earlyLaunches,0,'unpublished physical wake started another Host');
  assert(!f.host.calls.some(v=>v==='handshake'||v==='acquire'||v==='enable'));
  if(outcome==='off')await f.life.disable();
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  if(outcome==='revoked-before-spawn'){
   await f.advance(2000);
   assert.equal(f.native.notificationPending,true,'replacement preflight did not reach the revocation');
   assert.equal(f.starts-1,0,'a grant withdrawn during preflight still spawned a Host');
   assert(!f.host.calls.some(v=>v==='handshake'||v==='acquire'||v==='enable'));
   f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,notificationPending:false});
  }
  // Let the existing owner read the public grant. The inert Host must not
  // report an already rebuilt session while its process is still missing.
  await f.advance(4000);
  const restarts=f.starts-1;
  const writes=f.host.calls.filter(v=>v==='enable').length;
  const facts=JSON.stringify({restarts,writes,state:f.life.state,recovery:f.life.recoveryState,trace:f.host.calls});
  assert.equal(restarts,outcome==='off'?0:1,'the current wake must own the only replacement launch: '+facts);
  assert.equal(writes,outcome==='off'?0:1,'the current wake must own the only restored curve: '+facts);
  return {earlyLaunches,restarts,writes};
 }finally{await f.done();}
});

for (const boundary of ['recovers', 'off', 'new-sleep', 'sleep-during-read', 'still-unknown', 'wrong-generation', 'ipc-recovers', 'ipc-off', 'ipc-still-failing']) {
 await check('background-ready one unavailable coordinator read: '+boundary,async()=>{
  const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
  try{
   await f.boot();
   await f.host.resume({generation:4});
   // Match Native's unavailable reply: it carries no generation or phase.
   f.setNative({generation:undefined,phase:undefined,known:false,hardwareWritesAllowed:false,resumeReady:false});
   if(boundary.startsWith('ipc-'))f.failFanReads(boundary==='ipc-still-failing'?2:1);
   const readCount=()=>f.calls.filter(v=>v.cmd==='power.fanLifecycle').length;
   const start=readCount();
   f.rootReady(4);
   await f.flush();
   assert.equal(readCount()-start,1);
   assert(!f.host.calls.includes('acquire'),'an unavailable fact granted a lease');
   assert(!f.host.calls.includes('enable'),'an unavailable fact granted a write');
   if(boundary==='off'||boundary==='ipc-off')await f.life.close();
   if(boundary==='new-sleep')f.rootSuspend({generation:5,scope:'fan-only'});
   const beforeRetry=readCount();
   if(boundary!=='still-unknown')f.setNative({known:true,generation:boundary==='wrong-generation'?5:4,
    phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:false,notificationPending:false,sharedGeneration:3});
   if(boundary==='sleep-during-read'){
    f.holdNextFanRead();
    await f.advance(100);
    f.rootSuspend({generation:5,scope:'fan-only'});
    f.releaseFanRead();
   }
   await f.advance(6000);
   const writes=f.host.calls.filter(v=>v==='enable').length;
   const leases=f.host.calls.filter(v=>v==='acquire').length;
   const recovers=boundary==='recovers'||boundary==='ipc-recovers';
   assert.equal(writes,recovers?1:0,'wrong number of recovery writes');
   assert.equal(leases,recovers?1:0,'wrong number of recovery leases');
   if(!recovers)assert.equal(readCount()-beforeRetry,
    boundary==='off'||boundary==='ipc-off'||boundary==='new-sleep'?0:1,'the retained notification escaped its read budget');
   assert.equal(f.manuals,0);
   assert.equal(f.starts,1);
   return {boundary,writes,leases,coordinatorReadsAfterFirst:readCount()-beforeRetry};
  }finally{await f.done();}
 });
}

for(const [intent,boundary] of [
 ...['ready','off','new-sleep','settled-without-grant','expired','wrong-generation'].map(boundary=>['startup-preference',boundary]),
 ...['ready','off'].map(boundary=>['retained-curve',boundary]),
]){
 await check('background-ready '+intent+' during pending coordinator notification: '+boundary,async()=>{
  const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
  let stop;try{
   await f.boot(intent==='startup-preference');await f.host.resume({generation:4});f.host.calls=[];
   assert.equal(f.life.hasControlIntent,intent==='retained-curve');
   stop=f.watchSharedWake();await f.flush();
   const pending=boundary!=='settled-without-grant';
   f.setNative({known:true,generation:boundary==='wrong-generation'?5:4,sharedGeneration:3,phase:'resuming',resumeReady:false,
    hardwareWritesAllowed:false,notificationPending:pending});
   const reads=()=>f.calls.filter(v=>v.cmd==='power.fanLifecycle').length;
   const initialReads=reads();
   f.publishFanWake({fanWakeGeneration:4,publicationRevision:2});
   await f.flush();
   assert.equal(reads()-initialReads,1);
   await f.advance(2000);
   assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'));
   if(boundary==='off')await f.life.disable();
   if(boundary==='new-sleep')f.rootSuspend({generation:5,scope:'fan-only'});
   if(boundary==='expired')await f.advance(20000);
   // No second wake delivery: the first request owns the readback.
   f.setNative({generation:['new-sleep','wrong-generation'].includes(boundary)?5:4,phase:'ready',resumeReady:true,
    hardwareWritesAllowed:true,notificationPending:false});
   await f.advance(2000);
   const writes=f.host.calls.filter(v=>v==='enable').length;
   assert.equal(writes,boundary==='ready'?1:0,'pending notice must resume only the current request');
   if(boundary==='settled-without-grant'||boundary==='wrong-generation')assert.equal(reads()-initialReads,1,'a refusal must not be polled');
   assert.equal(f.starts,1);
   assert.equal(f.manuals,0);
   return {boundary,writes,reads:reads()-initialReads,starts:f.starts};
  }finally{stop?.();await f.done();}
 });
}

for(const [intent,boundary] of [
 ['startup-preference','ready'], ['retained-curve','ready'],
 ['retained-curve','off'], ['startup-preference','newer-native'],
]){
 await check('background-ready '+intent+' pending receipt ahead of renderer: '+boundary,async()=>{
  const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
  let stop;try{
   await f.boot(intent==='startup-preference');
   const generation=f.life.coordinatorSnapshot.powerGeneration+1;
   await f.host.resume({generation});f.host.calls=[];
   stop=f.watchSharedWake();await f.flush();
   f.setNative({known:true,generation,sharedGeneration:3,phase:'resuming',resumeReady:false,
    hardwareWritesAllowed:false,notificationPending:true});
   f.publishFanWake({fanWakeGeneration:generation,publicationRevision:2});
   await f.advance(1500);
   assert.equal(f.life.coordinatorSnapshot.powerGeneration,generation-1,'pending delivery advanced the renderer');
   assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'),'pending delivery acquired control');
   if(boundary==='off')await f.life.disable();
   if(boundary==='newer-native'){
    f.setNative({generation:generation+1,phase:'suspended',notificationPending:true});
    await f.advance(2500);
   }
   f.setNative({generation:boundary==='newer-native'?generation+1:generation,phase:'ready',resumeReady:true,
    hardwareWritesAllowed:true,notificationPending:false});
   await f.advance(5000);
   const writes=f.host.calls.filter(v=>v==='enable').length;
   assert.equal(writes,boundary==='ready'?1:0,'a pending wake must follow only its own published generation');
   assert.equal(f.life.coordinatorSnapshot.powerGeneration,boundary==='ready'?generation:generation-1);
   assert.equal(f.starts,1);
   assert.equal(f.manuals,0);
   return {intent,boundary,generation,writes,starts:f.starts};
  }finally{stop?.();await f.done();}
 });
}

for (const boundary of ['close-returned', 'pending-then-close', 'off', 'new-sleep', 'expired', 'no-grant']) {
 await check('background-ready retained curve waits for an in-flight Close: '+boundary,async()=>{
  const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
  let stop;
  try{
   await f.boot();f.setPreference(false);f.rootPublicSuspend(4);
   stop=f.watchSharedWake();await f.flush();
   f.publishShared({generation:4,phase:'ready',hardwareWritesAllowed:true,publicationRevision:2});
   const closing={known:true,generation:4,sharedGeneration:4,phase:'resuming',resumeReady:false,
    hardwareWritesAllowed:false,physicalWakeObserved:true,notificationPending:false};
   f.setNative({...closing,notificationPending:boundary==='pending-then-close'});
   const reads=()=>f.calls.filter(c=>c.cmd==='power.fanLifecycle').length;
   const initialReads=reads();
   f.rootCommittedReady(4);await f.flush();
   assert.equal(reads()-initialReads,1);
   f.setNative(closing);
   await f.advance(1000);
   assert(!f.host.calls.some(c=>c==='open'||c==='acquire'||c==='enable'));
   if(boundary==='off')await f.life.disable();
   if(boundary==='new-sleep'){
    f.setNative({generation:5,phase:'suspending',physicalWakeObserved:false});
    f.rootSuspend({generation:5,scope:'fan-only'});
   }
   if(boundary==='expired')await f.advance(20000);
   if(boundary!=='new-sleep'){
    await f.host.resume({generation:4});
    f.setNative({phase:'ready',resumeReady:boundary!=='no-grant',hardwareWritesAllowed:true});
   }
   // The coordinator's eventual Fan grant is available in the readback only.
   await f.advance(4000);
   const writes=f.host.calls.filter(c=>c==='enable').length;
   assert.equal(writes,['close-returned','pending-then-close'].includes(boundary)?1:0);
   assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes);
   assert.equal(f.life.recoveryActive,false);
   assert.equal(f.starts,1);assert.equal(f.manuals,0);
   return {boundary,writes,reads:reads()-initialReads,starts:f.starts,manualRequests:f.manuals};
  }finally{stop?.();await f.done();}
 });
}

for(const boundary of ['reattach-first','wake-first','off','new-sleep'])await check('background-ready retained curve after renderer reattach: '+boundary,async()=>{
 const replies=[];
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true,()=>new Promise(resolve=>replies.push(resolve)));
 let stop;
 try{
  await f.boot(true);f.setPreference(false);
  const power=nativeCloseWire?.afterClose??{generation:3,fanWakeGeneration:4,publicationRevision:2,
   observedNotificationSequence:10,phase:'ready',known:true,hardwareWritesAllowed:true,notificationPending:false};
  const generation=power.fanWakeGeneration;
  await f.host.resume({generation});f.host.calls=[];
  f.setNative({known:true,generation,sharedGeneration:power.generation,phase:'ready',
   resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true,notificationPending:false});
  // No event subscriber exists yet. A new document sees this grant on its first read.
  f.publishShared(power);stop=f.watchSharedWake();await f.flush();
  assert.equal(replies.length,1,'the public grant must reach the wake activity read');
  const reattach=f.observe(f.life.reattachAfterRendererRestart());await f.flush();
  assert.equal(replies.length,2,'the mount hook must read the same retained intent');
  const snapshot={active:true,recoverable:false,revision:71,curve:f.curve};
  const wakeFirst=boundary==='wake-first';
  replies[wakeFirst?0:1](snapshot);await f.flush();
  if(!wakeFirst){
   assert.equal(reattach.status,'fulfilled');assert.equal(reattach.value,false,'the resumed Host has no active curve to reattach');
   assert(f.life.hasControlIntent);assert(!f.host.calls.includes('enable'));
   if(boundary==='off')await f.life.disable();
   if(boundary==='new-sleep'){
    f.setNative({generation:generation+1,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
    f.rootSuspend({generation:generation+1,scope:'fan-only'});
   }
  }
  replies[wakeFirst?1:0](snapshot);await f.advance(2000);
  const expected=boundary==='reattach-first'||wakeFirst?1:0;
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,expected,'the original wake must restore only the current Native curve');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,expected);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);assert.equal(replies.length,2,'handoff needs no additional activity read');
  return {boundary,generation,writes,activityReads:replies.length};
 }finally{stop?.();await f.done();}
});

for (const boundary of ['ready', 'off', 'new-sleep', 'preference-disabled', 'unpublished', 'expired']) {
 await check('background-ready startup preference after missed Fan reads: '+boundary,async()=>{
  const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
  let stop;try{
   await f.boot(true);await f.host.resume({generation:4});f.host.calls=[];
   assert.equal(f.life.hasControlIntent,false);
   stop=f.watchSharedWake();await f.flush();
   f.setNative({known:false,generation:undefined,phase:undefined,hardwareWritesAllowed:false,resumeReady:false});
   f.failFanReads(2);
   const readsBefore=f.calls.filter(v=>v.cmd==='power.fanLifecycle').length;
   f.publishFanWake({fanWakeGeneration:4,publicationRevision:2});
   await f.advance(100);
   assert.equal(f.calls.filter(v=>v.cmd==='power.fanLifecycle').length-readsBefore,2);
   assert(!f.host.calls.includes('enable'),'unavailable facts allowed control');
   if(boundary==='off')await f.life.disable();
   if(boundary==='new-sleep')f.rootSuspend({generation:5,scope:'fan-only'});
   if(boundary==='preference-disabled')f.setPreference(false);
   if(boundary==='expired')await f.advance(20000);
   f.setNative({known:true,phase:'ready',generation:boundary==='new-sleep'?5:4,
    sharedGeneration:3,resumeReady:boundary!=='unpublished',hardwareWritesAllowed:boundary!=='unpublished'});
   await f.advance(2000);
   const writes=f.host.calls.filter(v=>v==='enable').length;
   assert.equal(writes,boundary==='ready'?1:0,'startup preference must follow this wake exactly once');
   assert.equal(f.starts,1,'wake observation did not need another Host');
   assert.equal(f.manuals,0,'automatic continuation should use the public receipt');
   return {boundary,writes,starts:f.starts,reads:f.calls.filter(v=>v.cmd==='power.fanLifecycle').length-readsBefore};
  }finally{stop?.();await f.done();}
 });
}

async function nativeBoot(f){const p=f.observe(f.boot());for(let n=0;n<100&&p.status==='pending';n++)await f.advance(100);if(p.status!=='fulfilled')console.log(JSON.stringify({diagnostic:'boot',status:p.status,error:p.error,state:f.life.state,host:f.host.calls,ipc:f.calls.map(x=>({cmd:x.cmd,url:x.args?.url}))}));assert.equal(p.status,'fulfilled',p.error??'boot pending');}
for(const boundary of ['resume','redelivery','read','off','new-sleep','pending','stale']) {
 await check('background-ready without the private notification: '+boundary,async()=>{
  const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
  let stop,stopOrdinary;
  try{
   await f.boot();
   // The parent stays in public generation 3 throughout. Fan has its own
   // completed Close and physical wake in generation 4. No page or F5 IPC event.
   f.rootSuspend({generation:4,scope:'fan-only'});
   let ordinaryNotices=0;
   if(boundary!=='read'){
    stop=f.watchSharedWake();stopOrdinary=f.api.onSharedPowerLifecycle(()=>++ordinaryNotices);await f.flush();
   }
   const ordinaryBefore=ordinaryNotices;
   const readBefore=f.calls.filter(c=>c.cmd==='power.fanLifecycle').length;
   const publicReadsBefore=f.calls.filter(c=>c.cmd==='power.lifecycle').length;
   if(boundary==='off')await f.life.disable();
   await f.host.resume({generation:4});
   f.setNative({known:true,generation:4,sharedGeneration:3,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
   if(boundary==='new-sleep')f.holdNextFanRead();
   if(boundary==='read'){
    f.publishShared({fanWakeGeneration:4,publicationRevision:2});stop=f.watchSharedWake();
   }else{
    if(boundary==='redelivery'){
     // Both first posts were lost while this background subscriber already existed.
     await f.advance(150);
     assert.equal(f.calls.filter(c=>c.cmd==='power.fanLifecycle').length,readBefore);
     assert.equal(f.host.calls.filter(c=>c==='enable').length,0);
    }
    // Native can capture fresher unrelated fields; only Fan may consume this projection.
    f.publishFanWake({fanWakeGeneration:4,publicationRevision:1,inputReady:true,
      observedNotificationSequence:boundary==='stale'?9:10,notificationPending:boundary==='pending'});
   }
   if(boundary==='new-sleep'){
    await f.flush();
    f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
    f.rootSuspend({generation:5,scope:'fan-only'});
    f.releaseFanRead();
   }
   await f.advance(2000);
   const positive=boundary==='resume'||boundary==='redelivery'||boundary==='read';
   assert.equal(f.host.calls.filter(c=>c==='acquire').length,positive?1:0,'unsafe or missing lease');
   assert.equal(f.host.calls.filter(c=>c==='enable').length,positive?1:0,'unsafe or missing curve replay');
   assert.equal(ordinaryNotices,ordinaryBefore,'Fan projection leaked into ordinary power observers');
   if(boundary==='redelivery'){
    const readsAfterWake=f.calls.filter(c=>c.cmd==='power.fanLifecycle').length;
    for(let i=0;i<2;i++)f.publishFanWake({fanWakeGeneration:4,publicationRevision:1});
    await f.flush();
    assert.equal(f.calls.filter(c=>c.cmd==='power.fanLifecycle').length,readsAfterWake,'duplicate resend read the confirmed Fan gate again');
    assert.equal(f.calls.filter(c=>c.cmd==='power.lifecycle').length,publicReadsBefore,'Fan recovery did not need public polling');
    assert.equal(ordinaryNotices,ordinaryBefore,'duplicate resends reached ordinary observers');
   }
   f.publishShared({publicationRevision:3});await f.advance(2000);
   assert.equal(f.host.calls.filter(c=>c==='enable').length,positive?1:0,'duplicate shared observation replayed the curve');
   assert.equal(f.manuals,0);
   assert.equal(f.starts,1);
   if(['off','pending','stale'].includes(boundary))assert.equal(f.calls.filter(c=>c.cmd==='power.fanLifecycle').length,readBefore,'cancelled, stale or pending wake read the Fan gate');
   assert.equal(f.native.sharedGeneration,3);
   return {boundary,trace:[...f.host.calls],fanGeneration:4,sharedGeneration:3,manualRequests:f.manuals,fanPageCreated:false};
  }finally{stopOrdinary?.();stop?.();await f.done();}
 });
}
for (const boundary of ['ready', 'single-delivery', 'unpublished', 'off', 'new-sleep']) await check('background-ready scoped delivery during a superseded Fan read: '+boundary,async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try {
  await f.boot();
  f.rootSuspend({generation:4,scope:'fan-only'});
  f.publishShared({phase:'suspended',hardwareWritesAllowed:false});
  stop=f.watchSharedWake();await f.flush();
  assert.equal(f.api.getSharedPowerLifecycle()?.phase,'suspended');
  const receipt={fanWakeGeneration:4,publicationRevision:2};
  f.failFanReads(1);
  const reads=()=>f.calls.filter(c=>c.cmd==='power.fanLifecycle').length;
  const before=reads();
  f.publishFanWake(receipt);await f.flush();
  assert.equal(reads()-before,1,'first wake read must still be in its retry delay');
  f.rootSuspend({generation:4,scope:'fan-only',resumeCatchup:true});
  if(boundary!=='single-delivery')f.publishFanWake({...receipt,publicationRevision:3});
  assert.equal(reads()-before,1,'duplicate must join the in-flight read');
  assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'));
  if(boundary==='off')await f.life.disable();
  if(boundary==='new-sleep')f.rootSuspend({generation:5,scope:'fan-only'});
  const generation=boundary==='new-sleep'?5:4;
  await f.host.resume({generation});
  f.setNative({known:true,generation,sharedGeneration:3,phase:'ready',resumeReady:boundary!=='unpublished',
   hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
  await f.advance(2000);
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,['ready','single-delivery'].includes(boundary)?1:0,'the scoped receipt must follow its own published wake only');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes);
  assert.equal(f.api.getSharedPowerLifecycle()?.phase,'suspended','Fan must not advance public Ready');
  assert.equal(f.starts,1);
  assert.equal(f.manuals,0);
  return {boundary,writes,starts:f.starts,manualRequests:f.manuals};
 } finally {stop?.();await f.done();}
});

for(const boundary of ['backoff','in-flight','off','new-sleep','expired']) await check('background-ready pending startup receipt survives same-generation Close catchup: '+boundary,async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try {
  await f.boot(true);
  f.rootSuspend({generation:4,scope:'fan-only'});
  assert.equal(f.life.hasControlIntent,false,'this wake belongs only to the startup preference');
  f.publishShared({phase:'suspended',hardwareWritesAllowed:false});
  stop=f.watchSharedWake();await f.flush();
  const reads=()=>f.calls.filter(c=>c.cmd==='power.fanLifecycle').length;
  const before=reads();
  f.setNative({known:true,generation:4,sharedGeneration:3,phase:'resuming',resumeReady:false,
   hardwareWritesAllowed:false,notificationPending:true});
  f.publishFanWake({fanWakeGeneration:4,publicationRevision:2});
  await f.flush();
  assert.equal(reads()-before,1,'first read must observe notification pending');
  if(boundary==='in-flight') {
   // Capture an apparently writable reply before catchup. The read began in the
   // old local observation; only a new read may authorize the wake after it.
   f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,notificationPending:false});
   f.holdNextFanRead();await f.advance(250);
   assert.equal(reads()-before,2,'the next IPC must already be in flight');
   f.setNative({phase:'resuming',resumeReady:false,hardwareWritesAllowed:false,notificationPending:true});
  }
  f.rootSuspend({generation:4,scope:'fan-only',resumeCatchup:true});
  if(boundary==='in-flight')f.releaseFanRead();
  await f.advance(1500);
  assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'),'catchup did not grant a write');
  if(boundary==='off')await f.life.disable();
  if(boundary==='new-sleep')f.rootSuspend({generation:5,scope:'fan-only'});
  if(boundary==='expired')await f.advance(15000);
  const generation=boundary==='new-sleep'?5:4;
  await f.host.resume({generation});
  f.setNative({known:true,generation,sharedGeneration:3,phase:'ready',resumeReady:true,
   hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
  // No second delivery, manual retry, or publication of global Ready.
  await f.advance(2000);
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,['backoff','in-flight'].includes(boundary)?1:0,'only the original current wake may start the saved curve');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes);
  assert.equal(f.api.getSharedPowerLifecycle()?.phase,'suspended');
  assert.equal(f.starts,1);
  assert.equal(f.manuals,0);
  return {boundary,writes,reads:reads()-before,starts:f.starts,manualRequests:f.manuals};
 } finally {stop?.();await f.done();}
});

for(const boundary of ['on','off-before-sleep','off-after-sleep','off-after-sleep-duplicate']) await check('background-ready public suspend binds cancellation before the Fan notice: '+boundary,async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try {
  await f.boot(true);
  if(boundary==='off-before-sleep')await f.life.disable();
  f.rootPublicSuspend(4);
  f.publishShared({generation:4,phase:'suspended',hardwareWritesAllowed:false});
  stop=f.watchSharedWake();await f.flush();
  if(boundary.startsWith('off-after-'))await f.life.disable();
  if(boundary.endsWith('-duplicate'))f.rootPublicSuspend(4);
  await f.host.resume({generation:4});
  f.setNative({known:true,generation:4,sharedGeneration:4,phase:'ready',resumeReady:true,
   hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
  f.publishFanWake({fanWakeGeneration:4,publicationRevision:2});
  await f.advance(2000);
  const starts=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(starts,boundary.startsWith('off-after-')?0:1,'a late Fan notice must respect OFF for its own wake only');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,starts);
  assert.equal(f.api.getSharedPowerLifecycle()?.phase,'suspended');
  assert.equal(f.starts,1);
  assert.equal(f.manuals,0);
  return {boundary,writes:starts,hostLaunches:f.starts,manualRequests:f.manuals};
 } finally {stop?.();await f.done();}
});

for(const boundary of ['ready','preference-off','off-before-candidate','off-during-read','missing-boundary','next-sleep','pending-clears','denied-once','lost-reply-claimed','lost-reply-unclaimed','off-after-claim','preference-error-new-offer','preference-error-no-offer','preference-error-off','preference-error-new-sleep']) await check('background-ready cold public startup offer: '+boundary,async()=>{
 const mode=boundary==='denied-once'||boundary.startsWith('lost-reply-')?'startup-claim-'+boundary:'ack';
 const f=fixture(mode,'HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try {
  await f.boot(true);
  if(boundary!=='missing-boundary')f.rootPublicSuspend(4);
  if(boundary==='preference-off')f.setPreference(false);
  if(boundary==='off-before-candidate')await f.life.disable();
  const preferenceError=boundary.startsWith('preference-error-');
  const release=boundary==='off-during-read'||boundary==='next-sleep'||preferenceError?f.holdStartupPreference():null;
  f.publishShared({generation:4,phase:'suspended',hardwareWritesAllowed:false,suspendSerial:3});
  stop=f.watchSharedWake();await f.flush();
  const offer={fanStartupWakeGeneration:5,publicationRevision:2,observedNotificationSequence:11};
  const claimCalls=()=>f.calls.filter(c=>c.cmd==='power.fanClaimStartupWake');
  const assertNoWrite=()=>{assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'));assert.equal(f.starts,1);};
  f.publishShared({...offer,notificationPending:boundary==='pending-clears'});await f.flush();
  assertNoWrite();
  if(boundary==='pending-clears'){
   assert.equal(claimCalls().length,0);
   f.publishShared({...offer,notificationPending:false,publicationRevision:3});await f.flush();
  }
  if(preferenceError&&boundary!=='preference-error-no-offer'){
   f.publishShared({...offer,publicationRevision:3,observedNotificationSequence:12});await f.flush();
   assert.equal(claimCalls().length,0,'the newer offer arrived during the original settings read');
  }
  if(boundary==='off-during-read'||boundary==='preference-error-off')await f.life.disable();
  if(boundary==='next-sleep'||boundary==='preference-error-new-sleep'){
   f.rootPublicSuspend(6);
   f.publishShared({generation:6,suspendSerial:4,fanStartupWakeGeneration:0,publicationRevision:4,observedNotificationSequence:13});
  }
  release?.(preferenceError?Error('inert startup preference read failed'):undefined);await f.flush();
  if(boundary.startsWith('lost-reply-')) {
   await f.advance(3100);
  } else if(boundary==='ready'){
   f.publishShared({...offer,publicationRevision:3,observedNotificationSequence:12});await f.flush();
  }
  const claimed=['ready','pending-clears','denied-once','lost-reply-claimed','lost-reply-unclaimed','off-after-claim','preference-error-new-offer'].includes(boundary);
  const expectedClaims=claimed?(['denied-once','lost-reply-unclaimed'].includes(boundary)?2:1):0;
  assert.equal(claimCalls().length,expectedClaims,'a claim can retry only after a fresh current offer');
  assertNoWrite();
  if(claimed){
   assert.equal(claimCalls()[0].args.generation,5);
   assert.equal(claimCalls()[0].args.sharedGeneration,4);
   assert.equal(claimCalls()[0].args.suspendSerial,3);
   assert.equal(claimCalls()[0].args.notificationSequence,boundary==='preference-error-new-offer'?12:11);
   if(boundary==='denied-once')assert.equal(claimCalls()[1].args.notificationSequence,12,'retry must bind the fresh sequence');
   if(boundary==='off-after-claim')await f.life.disable();
   await f.host.resume({generation:5});
   f.setNative({known:true,generation:5,sharedGeneration:4,phase:'ready',resumeReady:true,
    hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
   f.publishShared({fanStartupWakeGeneration:0,fanWakeGeneration:5,publicationRevision:4,observedNotificationSequence:13});
   await f.advance(2000);
  }
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,claimed&&boundary!=='off-after-claim'?1:0,'writes require a matching final Fan grant and current intent');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes);
  assert.equal(f.starts,1);
  assert.equal(f.manuals,0);
  assert.equal(f.api.getSharedPowerLifecycle()?.phase,'suspended');
  return {boundary,claims:claimCalls().length,writes,hostLaunches:f.starts,manualRequests:f.manuals};
 } finally {stop?.();await f.done();}
});

for(const boundary of ['fresh-offer','pending-clears','off','during-read-fresh-offer','during-read-pending-clears','during-read-off']) await check('background-ready cold claim retry rechecks after preferences: '+boundary,async()=>{
 const duringRead=boundary.startsWith('during-read-');
 const pending=boundary.endsWith('pending-clears'),off=boundary.endsWith('off');
 const f=fixture(duringRead?'startup-claim-held-read':'startup-claim-held-denial','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot(true);
  f.rootPublicSuspend(4);
  f.publishShared({generation:4,phase:'suspended',hardwareWritesAllowed:false,suspendSerial:3});
  stop=f.watchSharedWake();await f.flush();
  const claims=()=>f.calls.filter(c=>c.cmd==='power.fanClaimStartupWake').map(c=>c.args.notificationSequence);
  f.publishShared({fanStartupWakeGeneration:5,publicationRevision:2,observedNotificationSequence:11});await f.flush();
  assert.deepEqual(claims(),[11]);
  const release=duringRead?null:f.holdStartupPreference();
  const publicReads=()=>f.calls.filter(c=>c.cmd==='power.lifecycle').length;
  const readsBeforeDenial=publicReads();
  f.lateReply();await f.flush();
  assert.equal(publicReads(),readsBeforeDenial+(duringRead?1:0));
  assert.deepEqual(claims(),[11]);
  f.publishShared({publicationRevision:3,observedNotificationSequence:13,notificationPending:pending});
  if(off)await f.life.disable();
  if(release)release();else f.lateReply();
  await f.flush();
  const beforeClear=claims();
  assert.deepEqual(beforeClear,pending||off?[11]:[11,13],'a retry must not spend its final submission on a superseded receipt');
  if(pending){
   f.publishShared({publicationRevision:4,observedNotificationSequence:14,notificationPending:false});await f.flush();
   assert.deepEqual(claims(),[11,14]);
  }
  assert.equal(f.host.calls.filter(c=>c==='enable'||c==='acquire').length,0,'a claim still is not write permission');
  if(!off){
   await f.host.resume({generation:5});
   f.setNative({known:true,generation:5,sharedGeneration:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
   f.publishShared({fanStartupWakeGeneration:0,fanWakeGeneration:5,publicationRevision:5,observedNotificationSequence:15});
   await f.advance(2000);
  }
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,off?0:1);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes);
  assert.equal(f.starts,1);
  assert.equal(f.manuals,0);
  return {boundary,claims:claims(),beforeClear,writes,hostLaunches:f.starts,manualRequests:f.manuals};
 }finally{stop?.();await f.done();}
});

 for(const boundary of ['current-readback','off','current-before-old-reply','off-before-current']) await check('background-ready startup claim retry waits for the shared scope readback: '+boundary,async()=>{
  const f=fixture('startup-claim-held-read','HandheldCompanion.Devices.GPDWin5',true);
  let stop;
  try{
   await f.boot(true);
   f.rootPublicSuspend(4);
   f.publishShared({generation:4,phase:'suspended',hardwareWritesAllowed:false,suspendSerial:3});
   stop=f.watchSharedWake();await f.flush();
   const claims=()=>f.calls.filter(c=>c.cmd==='power.fanClaimStartupWake').map(c=>c.args.notificationSequence);
   f.publishShared({fanStartupWakeGeneration:5,publicationRevision:2,observedNotificationSequence:11});await f.flush();
   assert.deepEqual(claims(),[11]);
   f.lateReply();await f.flush(); // Retry read holds the old sequence 12 reply.
   f.hintPowerScope(4,3,13);await f.flush();
   const fenced=f.api.getSharedPowerLifecycle();
   assert.equal(fenced?.scopeReadbackPending,true);
   assert.equal(fenced?.fanStartupWakeGeneration,undefined);
    const oldReturnsFirst=boundary==='current-readback'||boundary==='off';
    if(oldReturnsFirst){f.lateReply();await f.flush();}
   assert.deepEqual(claims(),[11],'the old read must not spend the final Native claim');
    const off=boundary.startsWith('off');
    if(off)await f.life.disable();
   f.publishShared({publicationRevision:3,observedNotificationSequence:13});await f.flush();
    assert.deepEqual(claims(),off?[11]:[11,13],'a current shared fact must not wait for the old IPC reply or its timeout');
    if(!oldReturnsFirst){f.lateReply();await f.flush();}
    assert.deepEqual(claims(),off?[11]:[11,13],'a retired reply must not submit another claim');
   assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'));
   await f.host.resume({generation:5});
   f.setNative({known:true,generation:5,sharedGeneration:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
   f.publishShared({fanStartupWakeGeneration:0,fanWakeGeneration:5,publicationRevision:4,observedNotificationSequence:14});
   await f.advance(2000);
    const writes=off?0:1;
   assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes);
   assert.equal(f.host.calls.filter(c=>c==='enable').length,writes);
   assert.equal(f.manuals,0);
   return {boundary,claims:claims(),writes,manualRequests:f.manuals};
  }finally{stop?.();await f.done();}
 });

 for(const boundary of ['authorized','off-after-claim']) await check('background-ready public startup with no prior Host: '+boundary,async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot(true,false);
  f.rootPublicSuspend(4);
  f.publishShared({generation:4,phase:'suspended',hardwareWritesAllowed:false,suspendSerial:3});
  stop=f.watchSharedWake();await f.flush();
  f.publishShared({fanStartupWakeGeneration:5,publicationRevision:2,observedNotificationSequence:11});await f.flush();
  assert.equal(f.calls.filter(c=>c.cmd==='power.fanClaimStartupWake').length,1);
  assert.equal(f.starts,0,'an offer and claim cannot launch the first Host');
  assert(!f.host.calls.some(c=>['handshake','open','acquire','enable'].includes(c)));
  if(boundary==='off-after-claim')await f.life.disable();
  f.setNative({known:true,generation:5,sharedGeneration:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
  f.publishShared({fanStartupWakeGeneration:0,fanWakeGeneration:5,publicationRevision:3,observedNotificationSequence:12});
  await f.advance(2400);
  const writes=f.host.calls.filter(c=>c==='enable').length;
  const expected=boundary==='authorized'?1:0;
  const facts=JSON.stringify({state:f.life.state,starts:f.starts,writes,trace:f.host.calls});
  assert.equal(f.starts,expected,facts);
  assert.equal(writes,expected,facts);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes,facts);
  assert.equal(f.manuals,0);
  return {boundary,hostLaunches:f.starts,writes,manualRequests:f.manuals};
 }finally{stop?.();await f.done();}
});

for(const boundary of ['authorized','held-off-before-sleep','held-off-current','held-off-failed','off-after-claim','missing-boundary']) await check('background-ready native boundary without a suspend publication: '+boundary,async()=>{
 const f=fixture('startup-merged-'+boundary,'HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot(true,false);
  stop=f.watchSharedWake();await f.flush();
  const offCalls=()=>f.calls.filter(c=>c.cmd==='power.fanCancelStartupWake').length;
  const claimCalls=()=>f.calls.filter(c=>c.cmd==='power.fanClaimStartupWake');
  const heldOff=boundary.startsWith('held-off-');
  if(heldOff){await f.life.disable();assert.equal(offCalls(),1);}
  const nativeBoundary=boundary==='missing-boundary'?undefined:'134400000000000047';
  f.publishShared({generation:4,phase:'ready',suspendSerial:3,fanStartupWakeGeneration:5,fanStartupWakeBoundary:nativeBoundary,
   publicationRevision:2,observedNotificationSequence:12});await f.flush();
  assert.equal(f.api.getSharedPowerLifecycle()?.fanStartupWakeBoundary,nativeBoundary);
  if(heldOff){assert.equal(claimCalls().length,0,'claim crossed an uncertain OFF');f.lateReply();await f.flush();}
  const mayClaim=boundary!=='held-off-failed'&&boundary!=='missing-boundary';
  assert.equal(claimCalls().length,mayClaim?(boundary==='held-off-current'?2:1):0);
  for(const {args} of claimCalls())assert.deepEqual({...args, ...(args.offDocument?{offDocument:{...args.offDocument}}:{})},{generation:5,sharedGeneration:4,suspendSerial:3,notificationSequence:12,boundary:nativeBoundary,
   ...(heldOff?{offSequence:1,offDocument:{generation:'7',navigationId:'12'}}:{})});
  assert.equal(f.starts,0);assert(!f.host.calls.some(c=>c==='acquire'||c==='enable'));
  if(boundary==='off-after-claim'){await f.life.disable();assert.equal(offCalls(),1);}
  if(mayClaim&&boundary!=='held-off-current'){
   f.setNative({known:true,generation:5,sharedGeneration:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
   f.publishShared({fanStartupWakeGeneration:0,fanStartupWakeBoundary:'',fanWakeGeneration:5,publicationRevision:3,observedNotificationSequence:13});
   await f.advance(2400);
  }
  const expected=boundary==='authorized'||boundary==='held-off-before-sleep'?1:0;
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,expected);assert.equal(f.starts,expected);assert.equal(f.manuals,0);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,expected);
  return {boundary,claims:claimCalls().length,offCalls:offCalls(),hostLaunches:f.starts,writes};
 }finally{stop?.();await f.done();}
});

for(const branch of ['dropped-reply','ack','unreceived','superseded','wrong-sequence','stale-offer']) await check('background-ready later independent sleep after one lost OFF reply: '+branch,async()=>{
 const f=fixture('startup-merged-'+(branch==='ack'?'ack-off':'lost-'+branch),'HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot(true,false);stop=f.watchSharedWake();await f.flush();
  await f.life.disable();await f.advance(3100);await f.advance(60000);
  const calls=cmd=>f.calls.filter(c=>c.cmd==='power.'+cmd);
  const claims=()=>calls('fanClaimStartupWake');
  const off=calls('fanCancelStartupWake')[0];
  assert(off);assert.equal(calls('fanStartupOffStatus').length,branch==='ack'?0:1,'at most one read can precede the next candidate');
  f.publishShared({generation:8,phase:'ready',suspendSerial:8,fanStartupWakeGeneration:9,fanStartupWakeBoundary:'134400000010000001',publicationRevision:15,observedNotificationSequence:26});
  await f.flush();
  assert.equal(calls('fanStartupOffStatus').length,branch==='ack'?0:branch==='dropped-reply'?1:2);
  const authorized=branch==='dropped-reply'||branch==='ack';
  assert.equal(claims().length,authorized?1:0,'OFF status did not constrain startup claim');
  assert.equal(f.starts,0);assert.equal(f.manuals,0);
  if(branch==='stale-offer'){
   f.publishShared({generation:9,phase:'suspending',suspendSerial:9,fanStartupWakeGeneration:0,fanStartupWakeBoundary:'',publicationRevision:16,observedNotificationSequence:27});
   f.lateReply();await f.flush();assert.equal(claims().length,0,'a delayed OFF status revived an expired offer');
  }else if(branch==='unreceived'){
   f.publishShared({generation:9,phase:'ready',suspendSerial:9,fanStartupWakeGeneration:10,fanStartupWakeBoundary:'134400000020000001',publicationRevision:16,observedNotificationSequence:27});
   await f.flush();assert.equal(claims().length,1,'a later Native boundary did not retire the sealed OFF');
   assert.equal(claims()[0].args.generation,10);
   assert.equal(calls('fanStartupOffStatus').length,3);
  }
  assert.equal(calls('fanCancelStartupWake').length,1,'historical OFF was replayed');
  if(claims().length){
   const {args}=claims()[0];
   assert.equal(args.offSequence,off.args.offSequence);assert.deepEqual(args.offDocument,off.args.offDocument);
   f.setNative({known:true,generation:args.generation,sharedGeneration:args.sharedGeneration,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
   f.publishShared({fanStartupWakeGeneration:0,fanStartupWakeBoundary:'',fanWakeGeneration:args.generation,publicationRevision:18,observedNotificationSequence:29});
   await f.advance(2400);
  }
  assert.equal(f.starts,claims().length);assert.equal(f.host.calls.filter(c=>c==='enable').length,claims().length);
  return {branch,claims:claims().length,offRequests:1,statusReads:calls('fanStartupOffStatus').length,hostLaunches:f.starts};
 }finally{stop?.();await f.done();}
});

for(const branch of ['lost-before-reload','ack-before-reload','manual-before-timeout']) await check('background-ready OFF timeout can establish its Native boundary before reload: '+branch,async()=>{
 const manual=branch==='manual-before-timeout';
 const f=fixture('startup-merged-'+(manual?'lost-before-reload':branch),'HandheldCompanion.Devices.GPDWin5',true);
 try{
  await f.boot(true,false);
  await f.life.disable();await f.flush();
  const calls=cmd=>f.calls.filter(c=>c.cmd==='power.'+cmd);
  const off=calls('fanCancelStartupWake');
  assert.equal(off.length,1);assert.equal(calls('fanStartupOffStatus').length,0);
  let manualResult;
  if(manual){
   f.setNative({known:true,generation:4,sharedGeneration:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,notificationPending:false});
   manualResult=f.observe(f.life.apply(f.curve));
   await f.advance(1200);
  }
  // No candidate or wake observer has run in this document. Establish the
  // original OFF identity while IPC still addresses this document.
  await f.advance(3100);
  const status=calls('fanStartupOffStatus');
  assert.equal(status.length,branch==='lost-before-reload'?1:0,'no Native OFF observation before the old document retires');
  if(status.length)assert.deepEqual(status[0].args,off[0].args);
  await f.advance(60000);
  assert.equal(calls('fanStartupOffStatus').length,status.length,'OFF recovery added polling');
  assert.equal(calls('fanCancelStartupWake').length,1);assert.equal(calls('fanClaimStartupWake').length,0);
  assert.equal(f.starts,manual?1:0);
  assert.equal(f.host.calls.filter(c=>c==='enable').length,manual?1:0);
  if(manual)assert.equal(manualResult.status,'fulfilled',manualResult.error??'new manual intent must remain reachable');
  else {assert.equal(f.manuals,0);assert.deepEqual(f.host.calls,[]);}
  return {branch,statusReads:status.length,offRequests:1,hostLaunches:f.starts};
 }finally{await f.done();}
});

for(const branch of ['held','queued-hint']) await check('background-ready replacement WebView uses the Native OFF boundary: '+branch,async()=>{
 const f=fixture('startup-merged-reload-'+branch,'HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot(true,false);stop=f.watchSharedWake();await f.flush();
  const claims=()=>f.calls.filter(c=>c.cmd==='power.fanClaimStartupWake');
  const assertNoControl=()=>{assert.equal(f.starts,0);assert.equal(f.manuals,0);assert(!f.host.calls.some(c=>c==='acquire'||c==='enable'));};
  // Reload creates an App without any local history of the previous document's OFF.
  const blocked={generation:8,phase:'ready',suspendSerial:8,fanStartupWakeGeneration:0,fanStartupWakeBoundary:'',
   publicationRevision:15,observedNotificationSequence:26};
  if(branch==='queued-hint')f.publishShared({...blocked,fanStartupWakeGeneration:9,fanStartupWakeBoundary:'134400000010000001'});
  else f.publishShared(blocked);
  await f.flush();assertNoControl();
  const staleRequests=branch==='queued-hint'?2:0;
  assert.equal(claims().length,staleRequests);
  f.publishShared({...blocked,publicationRevision:16,observedNotificationSequence:27});await f.flush();
  assertNoControl();assert.equal(claims().length,staleRequests);
  f.publishShared({generation:9,phase:'ready',suspendSerial:9,fanStartupWakeGeneration:10,fanStartupWakeBoundary:'134400000020000001',
   publicationRevision:17,observedNotificationSequence:28});
  await f.flush();assertNoControl();
  assert.deepEqual(claims().map(c=>c.args.generation),[...Array(staleRequests).fill(9),10]);
  assert(claims().every(c=>!('offSequence' in c.args)&&!('offDocument' in c.args)));
  assert(!f.calls.some(c=>c.cmd==='power.fanCancelStartupWake'||c.cmd==='power.fanStartupOffStatus'));
  f.setNative({known:true,generation:10,sharedGeneration:9,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
  f.publishShared({fanStartupWakeGeneration:0,fanStartupWakeBoundary:'',fanWakeGeneration:10,publicationRevision:18,observedNotificationSequence:29});
  await f.advance(2400);
  assert.equal(f.starts,1);assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);
  assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.manuals,0);
  return {branch,claims:claims().map(c=>c.args.generation),hostLaunches:f.starts,writes:1};
 }finally{stop?.();await f.done();}
});

if(nativeStartupOffWire?.reloadSuccessor)for(const key of ['reloadedAfterSeal','afterSealedClose','afterPublishedSeal']) await check('background-ready original Native replacement WebView: '+key,async()=>{
 const wire=nativeStartupOffWire,f=fixture('startup-native-off-reloaded','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot(true,false);stop=f.watchSharedWake();await f.flush();
  const count=cmd=>f.calls.filter(c=>c.cmd==='power.'+cmd).length;
  f.publishShared(wire[key]);await f.flush();
  assert.equal(f.api.getSharedPowerLifecycle()?.publicationRevision,wire[key].publicationRevision);
  assert.equal(count('fanClaimStartupWake'),0);
  if(key==='reloadedAfterSeal'){
   f.publishShared(wire.reloadSuccessor);await f.flush();
   assert.equal(f.api.getSharedPowerLifecycle()?.fanStartupWakeGeneration,wire.reloadSuccessor.fanStartupWakeGeneration);
   assert.equal(count('fanClaimStartupWake'),1);
  }
  assert.equal(count('fanStartupOffStatus'),0);assert.equal(count('fanCancelStartupWake'),0);
  assert.equal(f.starts,0);assert.equal(f.manuals,0);assert.deepEqual(f.host.calls,[]);
  return {key,claims:count('fanClaimStartupWake'),hostLaunches:f.starts};
 }finally{stop?.();await f.done();}
});

if(nativeStartupOffWire)for(const branch of ['applied','unreceived']) await check('background-ready original Native OFF status replay: '+branch,async()=>{
 const wire=nativeStartupOffWire,f=fixture('startup-native-off-'+branch,'HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot(true,false);stop=f.watchSharedWake();await f.flush();
  await f.life.disable();await f.advance(3100);
  const count=cmd=>f.calls.filter(c=>c.cmd==='power.'+cmd).length;
  if(branch==='unreceived'){
   f.publishShared(wire.blockedCandidate);await f.flush();
   assert.equal(count('fanStartupOffStatus'),2);
   assert.equal(count('fanClaimStartupWake'),0);
  }
  f.publishShared(branch==='applied'?wire.appliedCandidate:wire.retiredCandidate);await f.flush();
  assert.equal(count('fanStartupOffStatus'),branch==='applied'?1:3);
  assert.equal(count('fanClaimStartupWake'),1);
  assert.equal(count('fanCancelStartupWake'),1);
  assert.equal(f.starts,0);assert.equal(f.manuals,0);assert.deepEqual(f.host.calls,[]);
  return {branch,claims:count('fanClaimStartupWake'),statusReads:count('fanStartupOffStatus'),hostLaunches:f.starts};
 }finally{stop?.();await f.done();}
});

if(nativeStartupOffWire?.staleStatusAfterReload)await check('background-ready old document cannot claim after Native rejects its OFF lookup',async()=>{
 const wire=nativeStartupOffWire,f=fixture('startup-native-off-stale-document','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  assert.deepEqual(wire.staleStatusAfterReload,{ok:false});
  await f.boot(true,false);stop=f.watchSharedWake();await f.flush();
  await f.life.disable();await f.advance(3100);
  const count=cmd=>f.calls.filter(c=>c.cmd==='power.'+cmd).length;
  assert.equal(count('fanStartupOffStatus'),1);
  f.publishShared(wire.newDocumentBoundary);await f.flush();
  assert.equal(count('fanStartupOffStatus'),2);
  assert.equal(count('fanCancelStartupWake'),1);
  assert.equal(count('fanClaimStartupWake'),0);
  assert.equal(f.starts,0);assert.equal(f.manuals,0);assert.deepEqual(f.host.calls,[]);
  return {statusReads:2,offRequests:1,claims:0,hostLaunches:0};
 }finally{stop?.();await f.done();}
});

if(nativeS4Wire)for(const outcome of ['resume','off','new-suspend']) await check('background-ready retained Win5 curve uses original S4 wire: '+outcome,async()=>{
 const wire=nativeS4Wire,f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  assert.equal(wire.source,'kernel-s4-wake');
  await f.boot();f.publishShared(wire.prior);stop=f.watchSharedWake();await f.flush();
  const writes=()=>f.host.calls.filter(c=>c==='enable').length;
  const acquires=()=>f.host.calls.filter(c=>c==='acquire').length;
  f.setNative(wire.closeInFlightFan);f.publishShared(wire.closeInFlight);await f.advance(2400);
  assert.equal(writes(),0);assert.equal(acquires(),0);
  assert.equal(wire.closeInFlightFan.hardwareWritesAllowed,false);
  if(outcome==='off'){
   f.setPreference(false);
   await f.life.disable();
  }
  if(outcome==='new-suspend'){
   f.setNative(wire.nextSuspendFan);f.publishShared(wire.nextSuspend);await f.flush();
   assert.equal(f.api.getSharedPowerLifecycle()?.suspendSerial,wire.nextSuspend.suspendSerial);
  }else f.setNative(wire.readyFan);
  assert.equal(wire.delivered.fanWakeOnly,true);
  f.publishFanWake(wire.delivered);await f.advance(2400);
  f.publishFanWake(wire.delivered);await f.advance(2400);
  const expected=outcome==='resume'?1:0;
  assert.equal(writes(),expected);assert.equal(acquires(),expected);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  if(expected)assert.deepEqual(JSON.parse(JSON.stringify(f.host.snapshot().activeCurve)),f.curve);
  return {outcome,writes:writes(),acquires:acquires(),extraHostLaunches:f.starts-1,manuals:f.manuals};
 }finally{stop?.();await f.done();}
});

for(const [route,wire] of [['merged',nativeStartupWire],['late retry',nativeStartupWire?.lateRetry],...Object.entries(nativeRawWire??{})]){
 if(!wire)continue;
 const ordered=wire===nativeRawWire?.[route];
 const boundaries=wire.afterOffReadback?['authorized','off-before-final','off-readback']:['authorized','off-before-final'];
 if(route==='merged'&&wire.nativeWindowRetry)boundaries.push('window-off-then-next-wake','window-new-suspend-then-next-wake');
 for(const boundary of boundaries) await check('background-ready original Native '+route+' startup replay: '+boundary,async()=>{
 const mode=ordered?'startup-native-raw-'+route:route==='merged'?'startup-native-wire':'startup-native-wire-late-retry';
 const f=fixture(mode,'HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot(true,false);
  const noLaunch=()=>{assert.equal(f.starts,0);assert(!f.host.calls.some(c=>c==='acquire'||c==='enable'));};
  const claims=()=>f.calls.filter(c=>c.cmd==='power.fanClaimStartupWake');
  if(boundary.startsWith('window-')){
   // Replay the actual negative window delivery before a distinct, later wake.
   // No retained offer from the retired scope is supplied to the renderer.
   const retry=wire.nativeWindowRetry;
   const retired=boundary==='window-off-then-next-wake'?retry.afterOff:retry.newSuspend;
   assert.deepEqual(retry.candidate,wire.candidate);
   assert(retired.suspendSerial<wire.priorReady.suspendSerial);
   assert.equal(retired.fanStartupWakeGeneration,0);
   assert.equal(retired.fanWakeGeneration,0);
   if(boundary==='window-new-suspend-then-next-wake')assert.equal(retired.hardwareWritesAllowed,false);
   stop=f.watchSharedWake();await f.flush();
   f.publishShared(retired);await f.advance(2400);
   assert.equal(f.api.getSharedPowerLifecycle()?.suspendSerial,retired.suspendSerial);
   assert.equal(claims().length,0);assert.equal(f.manuals,0);noLaunch();
  }
  f.publishShared(wire.priorReady);
  if(!stop)stop=f.watchSharedWake();
  await f.flush();
  assert.equal(f.api.getSharedPowerLifecycle()?.suspendSerial,wire.priorReady.suspendSerial);
   if(ordered){
    assert(wire.suspendReceipt.sequence<wire.resumeReceipt.sequence);
    if(route==='replacementSuspend')assert(wire.oldSuspendReceipt.sequence<wire.suspendReceipt.sequence);
    for(const phase of ['suspended','resuming']){
     const fact=wire[phase];
     assert.equal(fact.hardwareWritesAllowed,false);
     assert.equal(fact.observedNotificationSequence,wire.resumeReceipt.sequence);
     f.publishShared(fact);await f.advance(2400);
     assert.equal(f.api.getSharedPowerLifecycle()?.phase,phase);
     assert.equal(claims().length,0);assert.equal(f.manuals,0);noLaunch();
    }
   }
  f.publishShared(wire.candidate);await f.flush();
  assert.equal(f.api.getSharedPowerLifecycle()?.fanStartupWakeBoundary,wire.claim.boundary);
  assert.equal(claims().length,1);assert.deepEqual({...claims()[0].args},wire.claim);
  assert.equal(wire.accepted.ok,true);noLaunch();
  assert.equal(wire.closeInFlight.hardwareWritesAllowed,true);
  assert.equal(wire.closeInFlight.fanWakeGeneration,0);
  f.publishShared(wire.closeInFlight);await f.flush();noLaunch();
  if(boundary==='off-before-final')await f.life.disable();
  const final=boundary==='off-readback'?wire.afterOffReadback:wire.finalPermit;
  const granted=final.fanWakeGeneration===wire.claim.generation;
  f.setNative({known:true,generation:wire.claim.generation,sharedGeneration:final.generation,phase:granted?'ready':'resuming',
   resumeReady:granted,hardwareWritesAllowed:granted,inputReady:false,notificationPending:final.notificationPending});
  f.publishShared(final);await f.advance(2400);
  const expected=boundary==='authorized'||boundary.startsWith('window-')?1:0,writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,expected);assert.equal(f.starts,expected);assert.equal(f.manuals,0);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,expected);
  assert.equal(claims().length,1);
   return {boundary,claims:claims().length,hostLaunches:f.starts,writes,fanGrant:final.fanWakeGeneration,
    ...(ordered?{suspendReceipt:wire.suspendReceipt.sequence,resumeReceipt:wire.resumeReceipt.sequence,
      classificationInput:wire.classificationInput}: {})};
 }finally{stop?.();await f.done();}
});
}

if(nativeStartupWire?.commitWithoutNewNotification)for(const boundary of ['ready','revision-only','off','new-sleep']) await check('background-ready original Native same-notification commit during preference read: '+boundary,async()=>{
 const wire=nativeStartupWire.commitWithoutNewNotification,f=fixture('startup-native-wire-commit','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot(true,false);f.publishShared(wire.priorReady);stop=f.watchSharedWake();await f.flush();
  const release=f.holdStartupPreference();
  const count=cmd=>f.calls.filter(c=>c.cmd==='power.'+cmd).length;
  f.publishShared(wire.resumingCandidate);await f.flush();
  const next=boundary==='revision-only'?{...wire.resumingCandidate,publicationRevision:wire.candidate.publicationRevision}:wire.candidate;
  f.publishShared(next);await f.flush();
  assert.equal(f.api.getSharedPowerLifecycle()?.publicationRevision,next.publicationRevision);
  assert.equal(count('fanClaimStartupWake'),0);assert.equal(f.starts,0);
  if(boundary==='off')await f.life.disable();
  if(boundary==='new-sleep')f.publishShared({generation:next.generation+1,phase:'suspending',hardwareWritesAllowed:false,
   suspendSerial:next.suspendSerial+1,fanStartupWakeGeneration:0,fanStartupWakeBoundary:'',
   publicationRevision:next.publicationRevision+1,observedNotificationSequence:next.observedNotificationSequence+1});
  release(Error('inert startup preference read failed'));await f.flush();
  const expected=boundary==='ready'?1:0;
  assert.equal(count('fanClaimStartupWake'),expected,'only a new admitted publication for the same candidate can recover this read');
  assert.equal(f.starts,0);assert.deepEqual(f.host.calls,[]);
  if(expected){
   const final=wire.finalPermit;
   f.setNative({known:true,generation:final.fanWakeGeneration,sharedGeneration:final.generation,phase:'ready',
    resumeReady:true,hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
   f.publishShared(final);await f.advance(2400);
  }
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,expected);assert.equal(f.starts,expected);assert.equal(f.manuals,0);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,expected);
  return {boundary,claims:count('fanClaimStartupWake'),hostLaunches:f.starts,writes};
 }finally{stop?.();await f.done();}
});

if(nativeStartupWire?.commitWithoutNewNotification)for(const off of [false,true]) await check(`background-ready original Native same-notification commit before old retry read replies: ${off?'off':'ready'}`,async()=>{
 const wire=nativeStartupWire.commitWithoutNewNotification;
 const f=fixture('startup-native-wire-commit-held-read','HandheldCompanion.Devices.GPDWin5',true);
 let stop;
 try{
  await f.boot(true,false);f.publishShared(wire.priorReady);stop=f.watchSharedWake();await f.flush();
  const claims=()=>f.calls.filter(c=>c.cmd==='power.fanClaimStartupWake').map(c=>c.args.notificationSequence);
  const reads=()=>f.calls.filter(c=>c.cmd==='power.lifecycle').length;
  const before=reads();
  f.publishShared(wire.resumingCandidate);await f.flush();
  const sequence=wire.resumingCandidate.observedNotificationSequence;
  assert.equal(wire.candidate.observedNotificationSequence,sequence);
  assert.equal(reads(),before+1,'the rejected claim is waiting on its old readback');
  assert.deepEqual(claims(),[sequence]);
  if(off)await f.life.disable();
  f.publishShared(wire.candidate);await f.flush();
  const expected=off?[sequence]:[sequence,sequence];
  assert.deepEqual(claims(),expected,'current Ready must supersede an unresolved resuming read without changing its notification sequence');
  f.lateReply();await f.flush();
  assert.deepEqual(claims(),expected,'no duplicate claim from obsolete readback');
  assert.equal(f.starts,0,'claim alone never launches a Host');
  const final=wire.finalPermit;
  f.setNative({known:true,generation:final.fanWakeGeneration,sharedGeneration:final.generation,phase:'ready',
   resumeReady:true,hardwareWritesAllowed:true,inputReady:false,notificationPending:false});
  f.publishShared(final);await f.advance(2400);
  const writes=off?0:1;
  assert.equal(f.host.calls.filter(c=>c==='enable').length,writes);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes);
  assert.equal(f.manuals,0);
  return {off,claims:claims(),writes};
 }finally{stop?.();await f.done();}
});

async function nativeDone(f){const p=f.observe(f.done());for(let n=0;n<800&&p.status==='pending';n++)await f.advance(100);if(p.status==='pending')console.log(JSON.stringify({diagnostic:'cleanup',state:f.life.state,host:f.host.calls,ipc:f.calls.map(x=>({cmd:x.cmd,url:x.args?.url}))}));assert.equal(p.status,'fulfilled',p.error??'cleanup pending');}
await check('native-owner real HTTP current sleep read vetoes a delayed old manual wake ACK',async()=>{
 const f=fixture('native-owner-ack-before-new-sleep');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  const click=await f.begin();await f.flush();
  assert.equal(f.manuals,1);
  assert.equal(f.native.generation,5);
  assert.notEqual(f.life.coordinatorSnapshot.wakeReadyGeneration,4,'old ACK overrode the Native read of a new sleep');
  for(let n=0;n<160&&click.status==='pending';n++)await f.advance(100);
  assert.equal(click.status,'rejected');
  assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
  assert.equal(f.manuals,1,'old click cannot submit another Native request');
 }finally{await nativeDone(f);}
});
await check('native-owner real HTTP rearm uses one original Native command and no renderer resume',async()=>{
 const f=fixture('native-owner');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  const pending=f.begin();let p=await pending;for(let n=0;n<160&&p.status==='pending';n++){await f.advance(100);await f.flush();}
  if(p.status!=='fulfilled')console.log(JSON.stringify({diagnostic:'rearm',status:p.status,error:p.error,state:f.life.state,native:f.native,host:f.host.calls,ipc:f.calls.map(x=>({cmd:x.cmd,url:x.args?.url}))}));assert.equal(p.status,'fulfilled',p.error??'real rearm did not finish');
  assert.equal(f.manuals,1);
  assert.notEqual(f.calls.find(x=>x.cmd==='power.fanManualWake').args.observedWakeOnly,true,'explicit click must retain manual wake authority');
  assert(!f.calls.some(x=>x.cmd==='http.request'&&x.args.url.endsWith('/api/resume')));
  assert(f.host.calls.includes('enable'),'actual apply did not reach inert Host enable');
  return {nativeCommands:f.manuals,rendererResumePosts:0};
 }finally{await nativeDone(f);}
});
for(const terminal of ['continue','new-sleep','off','deadline'])await check('native-owner real HTTP missing wake evidence: '+terminal,async()=>{
 const f=fixture('native-owner-wait-evidence');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  const click=await f.begin();await f.advance(600);
  assert.equal(click.status,'pending','same-generation Suspended must keep the click pending');
  assert.equal(f.manuals,1);
  assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
  if(terminal==='new-sleep'){
   f.setNative({generation:5,phase:'suspending',resumeReady:false});
   f.rootSuspend({generation:5,scope:'fan-only'});await f.advance(1200);
   assert.notEqual(click.status,'pending');
   f.setNative({generation:5,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});await f.advance(1200);
   assert.equal(f.manuals,1);assert(!f.host.calls.includes('enable'));
   return;
  }
  if(terminal==='off'||terminal==='deadline'){
   if(terminal==='off'){
    const off=f.observe(f.life.disable());
    for(let i=0;i<30&&off.status==='pending';i++)await f.advance(100);
    assert.equal(off.status,'fulfilled',off.error??'OFF was blocked by wake wait');
   }
   for(let i=0;i<160&&click.status==='pending';i++)await f.advance(100);
   assert.equal(click.status,'rejected');
   if(terminal==='deadline')assert.match(String(click.error),/FAN_MANUAL_NATIVE_WAIT_DEADLINE/);
   const snapshotReads=()=>f.calls.filter(x=>x.cmd==='power.fanLifecycle').length;
   const reads=snapshotReads();
   f.releaseCleanup();await f.advance(2300);
   assert.equal(snapshotReads(),reads,'a retired intent kept polling');
   assert.equal(f.manuals,1,'late paired wake revived the retired click');
   assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
   return;
  }
  f.releaseCleanup();for(let i=0;i<20&&f.manuals<2;i++)await f.advance(100);
  assert.equal(f.manuals,2);assert.equal(click.status,'pending');
  assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
  f.releaseCleanup();for(let i=0;i<160&&click.status==='pending';i++)await f.advance(100);
  assert.equal(click.status,'fulfilled',click.error??'the current wake did not continue the first click');
  assert.deepEqual(f.calls.filter(x=>x.cmd==='power.fanManualWake').map(x=>x.args.continueReadyOnly===true),[false,false,true]);
  assert.equal(f.host.calls.filter(call=>call==='enable').length,1);
  assert(!f.calls.some(x=>x.cmd==='http.request'&&x.args.url.endsWith('/api/resume')));
 }finally{await nativeDone(f);}
});
await check('native-owner real HTTP first click continues after native Close and Ready',async()=>{
 const f=fixture('native-owner-deferred-close');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  const click=await f.begin();await f.advance(400);
  assert.equal(f.manuals,1);assert.equal(click.status,'pending');
  assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)),'pending Native cleanup started Host work');
  f.releaseCleanup();
  for(let n=0;n<160&&click.status==='pending';n++)await f.advance(100);
  assert.equal(click.status,'fulfilled',click.error??'same click never continued');
  assert.equal(f.manuals,2,'same click must submit one Native request after Close and Ready');
  assert.deepEqual(f.calls.filter(x=>x.cmd==='power.fanManualWake').map(x=>x.args.continueReadyOnly===true),[false,true]);
  assert.equal(f.host.calls.filter(call=>call==='enable').length,1);
  assert.equal(f.starts,1);
  assert(!f.calls.some(x=>x.cmd==='http.request'&&x.args.url.endsWith('/api/resume')));
 }finally{await nativeDone(f);}
});
 for (const point of ['gate','resuming','pending-suspended','settled-suspended','capture','held','lost','new-sleep','suspending-cleared','suspending-held']) await check('native-owner real HTTP first click handles second Ready '+point,async()=>{
 const f=fixture('native-owner-deferred-close-'+point);try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  const click=await f.begin();await f.advance(400);
  assert.equal(f.manuals,1);assert.equal(click.status,'pending');
  f.releaseCleanup();
  for(let i=0;i<16&&f.manuals<2&&click.status==='pending';i++)await f.advance(100);
  assert.equal(f.manuals,2);
   if(point==='suspending-held'){
    for(let i=0;i<160&&click.status==='pending';i++)await f.advance(100);
    assert.equal(click.status,'rejected','the continued click must stop at actual Suspending');
    f.releaseCleanup();await f.advance(500);
    assert.equal(f.manuals,2,'Ready resurrected a click stopped by a real suspend');
    assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
    return;
   }
   if(point==='suspending-cleared'){
    assert.equal(f.native.phase,'suspending');
    // The request was refused before registration; the transition clears before the next bounded read.
    f.setNative({phase:'resuming'});
   }
  if(point==='settled-suspended'){
   assert.equal(f.native.phase,'suspended');assert.equal(f.native.notificationPending,false);
   await f.advance(500);
   assert.equal(click.status,'rejected');assert.match(String(click.error),/FAN_MANUAL_WAKE_NOT_READY:changed-native-boundary/);
   f.releaseCleanup();await f.advance(500);
   assert.equal(f.manuals,2,'late Ready resurrected a settled suspend');
   assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
   return;
  }
  if(point==='lost'){
   for(let i=0;i<160&&click.status==='pending';i++)await f.advance(100);
   assert.equal(click.status,'rejected');assert.match(String(click.error),/FAN_RESUME_WAIT_DEADLINE/);
   assert.equal(f.manuals,2,'unknown result cannot refund the continuation');
   assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
   return;
  }
  assert.equal(f.native.notificationPending,true);
  await f.advance(500);
  assert.equal(click.status,'pending');assert.equal(f.manuals,2,'pending notification must block another dispatch');
  assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
  if(point==='new-sleep'){
   f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
   f.rootSuspend({generation:5,scope:'fan-only'});
   f.releaseCleanup();await f.advance(1200);
   assert.equal(click.status,'rejected');assert.equal(f.manuals,2);
   assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
   return;
  }
  if(point==='held'){
   for(let i=0;i<160&&click.status==='pending';i++)await f.advance(100);
   assert.equal(click.status,'rejected');assert.match(String(click.error),/FAN_MANUAL_NATIVE_WAIT_DEADLINE/);
   f.releaseCleanup();await f.advance(500);
   assert.equal(f.manuals,2,'late Ready resurrected the expired click');
   assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
   return;
  }
  f.releaseCleanup();
  for(let i=0;i<160&&click.status==='pending';i++)await f.advance(100);
  assert.equal(click.status,'fulfilled',click.error??'same click did not survive the second Ready race');
  assert.deepEqual(f.calls.filter(x=>x.cmd==='power.fanManualWake').map(x=>x.args.continueReadyOnly===true),[false,true,true]);
  for(const call of ['resume','acquire','enable'])assert.equal(f.host.calls.filter(x=>x===call).length,1,call);
  assert.equal(f.starts,1);
  assert(!f.calls.some(x=>x.cmd==='http.request'&&x.args.url.endsWith('/api/resume')));
 }finally{await nativeDone(f);}
});
await check('native-owner real HTTP new sleep cancels the first click waiting for Native Close',async()=>{
 const f=fixture('native-owner-deferred-close');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  const click=await f.begin();await f.advance(400);
  assert.equal(click.status,'pending');assert.equal(f.manuals,1);
  f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
  f.rootSuspend({generation:5,scope:'fan-only'});
  f.releaseCleanup();await f.advance(1200);
  assert.equal(click.status,'rejected');assert.equal(f.manuals,1);
  assert(!f.host.calls.some(call=>['resume','acquire','enable'].includes(call)));
 }finally{await nativeDone(f);}
});
await check('native-owner real HTTP committed wake offers observation only and preserves manual rescue',async()=>{
 const f=fixture('native-owner');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  const p=f.observe(f.life.resumeAfterCommittedPowerWake(4));
  for(let n=0;n<160&&p.status==='pending';n++)await f.advance(100);
  assert.equal(p.status,'fulfilled',p.error??'committed wake remained pending');
  const requests=()=>f.calls.filter(x=>x.cmd==='power.fanManualWake');
  assert.equal(requests().length,1);
  assert.equal(requests()[0].args.observedWakeOnly,true,'automatic wake must not create a manual operation');
  assert(!f.calls.some(x=>x.cmd==='http.request'&&x.args.url.endsWith('/api/resume')));
  const context=f.life.admissionContext('same-generation-user-click',f.life.controlIntentRevisionValue,4);
  const click=f.observe(f.life.requestNativeResumeOnce(context));await f.flush();assert.equal(click.status,'fulfilled');
  assert.equal(requests().length,2,'automatic receipt cannot swallow a later explicit request');
  assert.notEqual(requests()[1].args.observedWakeOnly,true);
  return {observedWakeCommands:1,explicitCommands:1,rendererResumePosts:0};
 }finally{await nativeDone(f);}
});
for(const boundary of ['republish','off','republish-without-physical-pair','committed-without-physical-pair'])await check('native-owner real HTTP wake loses its public grant during the Host read: '+boundary,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5');let release;try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.calls=[];
  const getState=f.host.getState.bind(f.host);
  f.host.getState=async(...args)=>{
   const snapshot=await getState(...args);
   if(!release)await new Promise(resolve=>{release=resolve;});
   return snapshot;
  };
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true});
  if(boundary==='committed-without-physical-pair')f.rootCommittedReady();else f.rootReady();
  await f.flush();assert(release,'the wake replay must reach the real Host state request');
  f.setNative({resumeReady:false,physicalWakeObserved:!boundary.endsWith('-without-physical-pair')});
  release();await f.advance(1500);
  assert(!f.host.calls.some(c=>c==='acquire'||c==='enable'),'replay used the retired grant after its Host state read: '+f.host.calls.join(','));
  assert(f.life.recoveryActive,'withholding the grant must retain the original intent');
  if(boundary==='off'){
   const off=f.observe(f.life.disable());await f.advance(1000);assert.equal(off.status,'fulfilled',off.error??'OFF pending');
  }
  f.setNative({resumeReady:true});await f.advance(8000);
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,boundary==='off'?0:1,'one restored curve without a second notification, unless OFF');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);assert.equal(f.life.recoveryActive,false);
  return {boundary,writes,starts:f.starts,manualRequests:f.manuals};
 }finally{release?.();await nativeDone(f);}
});
await check('native-owner real HTTP ordinary wake loses its public grant while acquiring the lease',async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5');let release;try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.calls=[];
  const acquire=f.host.acquireControl.bind(f.host);
  f.host.acquireControl=async (...args)=>{
   const lease=await acquire(...args);
   if(!release)await new Promise(resolve=>{release=resolve;});
   return lease;
  };
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true});
  f.rootReady();await f.flush();
  assert(release,'ordinary recovery must reach its lease request');
  f.setNative({resumeReady:false});release();await f.advance(1500);
  assert(!f.host.calls.includes('enable'),'lease response did not supply a new public grant');
  assert(f.life.recoveryActive);
  f.setNative({resumeReady:true});await f.advance(8000);
  assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);assert.equal(f.life.recoveryActive,false);
  return {writes:1,acquires:1,starts:f.starts,manualRequests:f.manuals};
 }finally{release?.();await nativeDone(f);}
});
for(const outcome of ['republish','off','new-sleep'])await check('native-owner real HTTP telemetry recovery rechecks its public grant after acquisition: '+outcome,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5');let release;try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.calls=[];
  const acquire=f.host.acquireControl.bind(f.host);
  f.host.acquireControl=async (...args)=>{
   const lease=await acquire(...args);
   if(!release)await new Promise(resolve=>{release=resolve;});
   return lease;
  };
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true});
  assert.equal(await f.life.requestRecoveryAfterObservedHostState('telemetry-host-not-ready'),true);
  await f.advance(600);
  assert(release,'telemetry recovery must reach the lease request');
  f.setNative({resumeReady:false});release();await f.advance(1500);
  assert(!f.host.calls.includes('enable'),'telemetry wrote with the retired grant: '+f.host.calls.join(','));
  assert(f.life.recoveryActive,'the current intent should remain queued');
  if(outcome==='off'){
   const off=f.observe(f.life.disable());await f.advance(1000);assert.equal(off.status,'fulfilled',off.error??'OFF pending');
  }else if(outcome==='new-sleep'){
   f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false,physicalWakeObserved:false});
   f.rootSuspend({generation:5,scope:'fan-only'});
  }
  if(outcome!=='new-sleep')f.setNative({resumeReady:true});
  await f.advance(8000);
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,outcome==='republish'?1:0);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  if(outcome==='republish')assert.equal(f.life.recoveryActive,false);
  return {outcome,writes,acquires:1,starts:f.starts,manualRequests:f.manuals};
 }finally{release?.();await nativeDone(f);}
});
await check('ordinary wake loses its public grant just before launching a replacement Host',async()=>{
 const f=fixture('lost-host-before-spawn','HandheldCompanion.Devices.GPD.GPDWin5',true);try{
  await nativeBoot(f);
  await f.host.resume({generation:4});f.host.calls=[];
  f.life.process=null;f.life.setState('stopped'); // Detach the inert predecessor, as if it exited.
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true});
  f.rootReady();await f.advance(1500);
  assert.equal(f.native.notificationPending,true,'the actual launcher must reach pre-spawn revocation: '+JSON.stringify({state:f.life.state,recovery:f.life.recoveryState,trace:f.host.calls}));
  assert.equal(f.starts,1,'ordinary replay launched a replacement on a retired grant');
  assert(!f.host.calls.some(c=>c==='acquire'||c==='enable'));
  assert(f.life.recoveryActive,'withheld launch must retain the curve');
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,notificationPending:false});
  await f.advance(8000);
  assert.equal(f.starts,2);assert.equal(f.manuals,0);
  assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);
  assert.equal(f.life.recoveryActive,false);
  return {writes:1,restarts:f.starts-1,manualRequests:f.manuals};
 }finally{await nativeDone(f);}
});
for(const entry of ['notification','telemetry'])for(const boundary of ['newer-host-attempt','same-attempt','off'])await check('native-owner real HTTP revoked enable reconciles before recovery: '+entry+' '+boundary,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5',true);try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.calls=[];
  const enable=f.host.enable.bind(f.host);let refused=false;
  f.host.enable=async(...args)=>{
   if(refused)return enable(...args);
   refused=true;f.host.calls.push('enable-rejected');
    f.setNative({resumeReady:false,hardwareWritesAllowed:entry==='telemetry'});
   throw Object.assign(new Error('original public scope retired'),{code:'FAN_PUBLIC_POWER_SUPERSEDED'});
  };
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true});
   if(entry==='notification')f.rootReady();
   else assert.equal(await f.life.requestRecoveryAfterObservedHostState('telemetry-host-not-ready'),true);
   await f.advance(800);
  assert(refused&&f.life.recoveryActive,'the refused write must retain read-only observation');
  if(boundary==='off'){
   const off=f.observe(f.life.disable());await f.advance(1000);assert.equal(off.status,'fulfilled',off.error??'OFF pending');
  }
  // A new admitted operation may discard the prior lease. Removing the lease
  // with the same attempt still cannot prove the old enable retired.
  f.host.lease=null;
  if(boundary!=='same-attempt')await f.host.resume({generation:4});
  await f.advance(1200);
  assert(!f.host.calls.includes('enable'),'a fresh Host result without a public grant cannot replay');
  f.setNative({resumeReady:true,hardwareWritesAllowed:true});await f.advance(10000);
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,boundary==='newer-host-attempt'?1:0,'only a newer completed Host attempt plus the current public grant can replay');
  assert.equal(f.host.calls.filter(c=>c==='enable-rejected').length,1);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
   return {entry,boundary,writes,starts:f.starts,manualRequests:f.manuals};
 }finally{await nativeDone(f);}
});
for(const outcome of ['regrant','off','new-sleep'])await check('native-owner real HTTP self-heal handoff waits for a newer public wake: '+outcome,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5',true);try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.calls=[];
  const enable=f.host.enable.bind(f.host);let refused=false;
  f.host.enable=async(...args)=>{
   if(refused)return enable(...args);
   refused=true;f.host.calls.push('enable-rejected');
   f.setNative({resumeReady:false,hardwareWritesAllowed:false});
   throw Object.assign(new Error('original public scope retired'),{code:'FAN_PUBLIC_POWER_SUPERSEDED'});
  };
  f.setNative({known:true,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true});
  f.rootReady();await f.advance(800);
  assert(refused&&f.life.recoveryActive,'the rejected wake must leave its real self-heal owner');
  f.host.lease=null;await f.host.resume({generation:5});f.host.calls=[];
  // Only the newer Native snapshot is observed. No wake notification, no click.
  f.setNative({generation:5,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,physicalWakeObserved:false});
  await f.advance(2800);
  assert.equal(f.life.coordinatorSnapshot.powerGeneration,5,'self-heal did not hand off the newer generation');
  assert(!f.host.calls.some(c=>['open','events','acquire','enable'].includes(c)),
   'a newer Ready snapshot without public wake permission replayed the curve');
  assert(f.life.recoveryActive,'the handoff discarded the retained curve');
  if(outcome==='off'){
   const off=f.observe(f.life.disable());await f.advance(800);assert.equal(off.status,'fulfilled',off.error??'OFF did not settle');
  }else if(outcome==='new-sleep'){
   f.setNative({generation:6,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
   f.rootSuspend({generation:6,scope:'fan-only'});
  }
  f.setNative({generation:5,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  await f.advance(8000);
  const count=name=>f.host.calls.filter(c=>c===name).length;
  assert.equal(count('acquire'),Number(outcome==='regrant'));
  assert.equal(count('enable'),Number(outcome==='regrant'));
  assert.equal(count('open'),0);assert.equal(count('events'),0);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  if(outcome==='regrant'){
   const heartbeat=f.host.heartbeat.bind(f.host);let expired=false;
   f.host.heartbeat=async()=>{
    if(expired)return heartbeat();
    expired=true;f.host.lease=null;
    f.setNative({resumeReady:false});
    throw Object.assign(new Error('expired lease'),{code:'LEASE_INVALID'});
   };
   await f.advance(5500);assert(expired,'the resumed curve did not restart maintenance');
   assert.equal(count('acquire'),1,'a later lease failure forgot the read-back wake admission');
   assert.equal(count('enable'),1);
   f.setNative({resumeReady:true});await f.advance(7000);
   assert.equal(count('acquire'),2);assert.equal(count('enable'),2);
  }
  return {outcome,acquires:count('acquire'),writes:count('enable'),starts:f.starts,manualRequests:f.manuals};
 }finally{await nativeDone(f);}
});
for(const boundary of ['ready','off','new-sleep','unpublished-physical','unpublished-without-pair'])await check('native-owner real HTTP committed wake retains a curve across delayed native permission: '+boundary,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.calls=[];
  f.setNative({phase:'resuming',resumeReady:true,hardwareWritesAllowed:false});
  const p=f.observe(f.life.resumeAfterCommittedPowerWake(4));await f.advance(100);
  assert.notEqual(p.status,'pending','original attempt did not hand off');
  assert(!f.host.calls.some(c=>c==='acquire'||c==='enable'),'Host readiness alone allowed writes');
  const retained=f.life.recoveryActive;
  if(boundary==='off'){
   const off=f.observe(f.life.disable());await f.advance(1000);assert.equal(off.status,'fulfilled',off.error??'OFF pending');
  }else if(boundary==='new-sleep'){
   f.setNative({generation:5,phase:'suspending',resumeReady:false});
   f.life.setPowerGeneration(5);f.life.observePowerBoundary('suspending',5);
  }else if(boundary.startsWith('unpublished-')){
   // The previous public grant is retired. The Host and local coordinator
   // still look Ready; a fresh pairing alone is not the next auto grant.
   f.setNative({phase:'ready',hardwareWritesAllowed:true,resumeReady:false,physicalWakeObserved:boundary==='unpublished-physical'});
   await f.advance(2000);
   assert(!f.host.calls.some(c=>c==='acquire'||c==='enable'),'resident Ready bypassed the new public grant');
   assert(f.life.recoveryActive,'withholding the grant discarded the retained curve');
   f.setNative({resumeReady:true});
  }
  if(boundary!=='new-sleep')f.setNative({phase:'ready',hardwareWritesAllowed:true});
  await f.advance(8000);
  const enables=f.host.calls.filter(c=>c==='enable').length;
  const restored=boundary==='ready'||boundary.startsWith('unpublished-');
  assert.equal(enables,restored?1:0,'incorrect replay without a second wake event or a click');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,enables);
  assert(retained,'the same wake must retain its curve while Native is still resuming');
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  if(restored)assert.equal(f.life.recoveryActive,false,'successful write did not retire the recovery owner');
  return {boundary,enables,retained,starts:f.starts,manualRequests:f.manuals,attempt:p.status};
 }finally{await nativeDone(f);}
});
for(const [route,outcome] of [['idle','regrant'],['idle','off'],['idle','new-sleep'],['half-open','regrant'],['source','regrant']])await check('native-owner real HTTP committed wake rechecks after '+route+' Host Open: '+outcome,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5');let release;try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.opened=f.host.events=false;f.host.calls=[];
  const snapshot=f.host.snapshot.bind(f.host),open=f.host.open.bind(f.host),events=f.host.openEvents.bind(f.host);
  const sourceReceipt=(sourceFanGeneration,sourceFanClosed)=>({sourceFanMode:true,sourceFanOwnerEntered:true,
   sourceFanGeneration,sourceFanClosed,sourceFanClosePending:false,sourceFanAutomaticReturned:sourceFanClosed,
   hardwareWritesObserved:sourceFanClosed,recoveryClaimEvidence:{sourceFanGeneration,
    ...(sourceFanClosed?{oemReleaseActionKind:'proven-by-source-auto-register-and-terminal-close',oemReleaseActionClass:'release-action-proven'}:{})}});
  if(route==='source'){
   f.life.syncRemoteSessionState({...snapshot(),...sourceReceipt(41,false)});
   f.host.generation=0;f.host.attempt=0;f.host.powerState='Unknown';
  }
  f.host.snapshot=()=>({...snapshot(),...(route==='source'?sourceReceipt(f.host.opened?42:41,!f.host.opened)
   :route==='half-open'?{powerOperationOutcome:'restored',recoveryReadbackRequired:true}:{powerOperationOutcome:'await-user'})});
  f.host.open=async()=>{
   await open();f.host.stateName='Open';
   await new Promise(resolve=>{release=resolve;});return f.host.snapshot();
  };
  f.host.openEvents=async()=>{f.host.stateName='Ready';return events();};
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true});f.rootCommittedReady();
  await f.flush();assert(release,'actual committed wake did not reach the original Open');
  f.setNative({resumeReady:false,physicalWakeObserved:false});
  const off=outcome==='off'?f.observe(f.life.disable()):null;
  if(outcome==='new-sleep'){
   f.setNative({generation:5,phase:'suspending',hardwareWritesAllowed:false});f.rootSuspend({generation:5,scope:'fan-only'});
  }
  release();await f.advance(1800);
  if(off)assert.equal(off.status,'fulfilled',off.error??'OFF did not settle');
  assert(!f.host.calls.some(c=>['events','acquire','enable'].includes(c)),'retired wake reached OpenEvents after Open');
  if(outcome==='regrant')assert(f.life.recoveryActive,'Open boundary did not retain the requested curve');
  f.setNative({generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});await f.advance(8000);
  const counts=Object.fromEntries(['open','events','acquire','enable'].map(c=>[c,f.host.calls.filter(x=>x===c).length]));
  const writes=outcome==='regrant'?1:0;
  assert.deepEqual(counts,{open:1,events:writes,acquire:writes,enable:writes});
  assert.equal(f.starts,1);assert.equal(f.manuals,0);assert(!f.life.recoveryActive);return counts;
 }finally{release?.();await nativeDone(f);}
});
for(const outcome of ['awake','regrant','off','new-sleep','scope-replaced'])await check('native-owner real HTTP AC replay behind heartbeat keeps public wake authority: '+outcome,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5',true);let release;try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});
  f.setNative({known:true,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true});f.rootReady();
  await f.advance(1800);assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'initial wake did not apply');
  f.host.calls=[];
  const heartbeat=f.host.heartbeat.bind(f.host);
  f.host.heartbeat=async()=>{await new Promise(resolve=>{release=resolve;});return heartbeat();};
  const renewing=f.observe(f.life.heartbeat());await f.flush();assert(release,'heartbeat must own the write queue');
  let current=true;
  f.observe(f.life.reapplyActiveCurve('ac-changed',()=>current));await f.flush();
  if(outcome==='scope-replaced')current=false;
  if(outcome!=='awake'&&outcome!=='scope-replaced')f.setNative({resumeReady:false,physicalWakeObserved:false});
  f.host.heartbeat=heartbeat;release();await f.advance(1800);
  assert.equal(renewing.status,'fulfilled');
  const count=name=>f.host.calls.filter(c=>c===name).length;
  assert.equal(count('enable'),Number(outcome==='awake'),'AC replay accepted an expired public wake while queued');
  if(outcome==='scope-replaced'){
   assert.equal(count('acquire'),0);assert(!f.life.recoveryActive,'an old AC scope must not create recovery work');
   const next=f.observe(f.life.reapplyActiveCurve('ac-changed',()=>true));await f.advance(1800);
   assert.equal(next.status,'fulfilled');assert.equal(count('enable'),1);assert.equal(count('acquire'),0);
   return {outcome,oldWrites:0,currentWrites:count('enable')};
  }
  if(outcome!=='awake'){
   assert.equal(count('acquire'),0);
   assert(f.life.recoveryActive,'the pending power-side curve was dropped');
   if(outcome==='off'){
    const off=f.observe(f.life.disable());await f.advance(800);assert.equal(off.status,'fulfilled',off.error??'OFF did not settle');
   }else if(outcome==='new-sleep'){
    f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
    f.rootSuspend({generation:5,scope:'fan-only'});
   }
   f.setNative({generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});await f.advance(9500);
  }
  assert.equal(count('enable'),Number(outcome==='awake'||outcome==='regrant'));
  assert.equal(count('acquire'),0);assert.equal(count('open'),0);assert.equal(count('events'),0);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  return {outcome,writes:count('enable'),acquires:count('acquire'),starts:f.starts};
 }finally{release?.();await nativeDone(f);}
});
for(const outcome of ['awake','regrant','ready-regrant','lease-regrant','late-regrant','late-off','late-new-sleep','late-manual','off','new-sleep','manual'])await check('background-ready unclean exit uses current public wake: '+outcome,async()=>{
 let record=null;
 const f=fixture('ack','HandheldCompanion.Devices.GPD.GPDWin5',true,async()=>record);
 const late=outcome.startsWith('late-'), action=late?outcome.slice(5):outcome;
 let stop;
 try{
  await f.boot(true,false);f.setPreference(false);
  f.setNative({known:true,generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true});
  if(late){stop=f.watchSharedWake();await f.flush();f.publishFanWake({fanWakeGeneration:4,publicationRevision:2});}
  else f.rootReady();
  await f.advance(1200);
  assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,4,'the mount notification did not admit the public wake');
  assert.equal(f.starts,0);assert(!f.life.hasControlIntent);
  record={recoverable:true,active:false,revision:71,curve:f.curve};
  if(outcome==='lease-regrant'){
   const acquire=f.host.acquireControl.bind(f.host);
   f.host.acquireControl=async()=>{const reply=await acquire();f.setNative({phase:'resuming',resumeReady:false,hardwareWritesAllowed:false});return reply;};
  }else if(outcome!=='awake'){
   f.setNative(outcome==='ready-regrant'?{resumeReady:false}:{phase:'resuming',resumeReady:false,hardwareWritesAllowed:false});
  }
  const restored=f.observe(f.life.recoverAfterUncleanExit());await f.advance(1200);
  const count=name=>f.host.calls.filter(c=>c===name).length;
  assert.equal(f.starts,Number(outcome==='awake'||outcome==='lease-regrant'),'a retained crash curve launched a Host without a current wake grant');
  assert.equal(count('enable'),Number(outcome==='awake'),`early curve: ${f.host.calls.join(',')}`);
  if(outcome==='lease-regrant'){
   assert.equal(count('acquire'),1,`missing dispatched lease: ${f.host.calls.join(',')}`);
  }
  if(late){
   await f.advance(15000);
   assert.equal(restored.status,'fulfilled','foreground recovery remained pending past its deadline');
   assert.equal(restored.value,false);
   assert.equal(f.starts,0,'deadline alone launched a Host without a grant');
   assert.equal(count('enable'),0);
   const reads=()=>f.calls.filter(m=>m.cmd==='power.fanLifecycle').length;
   const idleReads=reads();await f.advance(60000);
   assert.equal(reads(),idleReads,'an expired recovery polled instead of waiting for public wake');
  }
  if(outcome!=='awake'){
   if(action==='off'){
    const off=f.observe(f.life.disable());await f.advance(500);assert.equal(off.status,'fulfilled',off.error??'OFF did not settle');
   }else if(action==='new-sleep'){
    f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
    f.rootSuspend({generation:5,scope:'fan-only'});
   }else if(action==='manual'){
    f.setNative({phase:'ready',hardwareWritesAllowed:true});
    const newest=f.curve.map(node=>({...node,dutyPercent:70}));
    const click=f.observe(f.life.apply(newest));await f.advance(2000);
    assert.equal(click.status,'fulfilled',click.error??'explicit ON was stuck behind the crash wait');
    assert.deepEqual(f.host.snapshot().activeCurve,newest);
   }
   f.setNative({generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
   if(late){f.publishFanWake({fanWakeGeneration:4,publicationRevision:3});f.publishFanWake({fanWakeGeneration:4,publicationRevision:4});}
   await f.advance(2500);
  }
  const expected=Number(action!=='off'&&action!=='new-sleep');
  assert.equal(f.starts,expected,'the valid public grant did not start exactly one Host');assert.equal(count('acquire'),expected,`final leases: ${f.host.calls.join(',')}`);
  assert.equal(count('enable'),expected,`final writes: ${f.host.calls.join(',')}`);
  assert.equal(restored.status,'fulfilled',restored.error??'crash recovery never settled');
  assert.equal(restored.value,!!expected&&action!=='manual'&&outcome!=='lease-regrant'&&!late);
  if(action!=='manual')assert.equal(f.manuals,0);
  return {outcome,starts:f.starts,writes:count('enable')};
 }finally{stop?.();await f.done();}
});
for(const outcome of ['grant','fresh-grant','boot-opt-in','off','new-sleep','fresh-unreadable-grant','fresh-unreadable-off',
 'fresh-unbound-grant','fresh-unbound-off','fresh-unbound-new-sleep'])await check('background-ready mount with an unreceived public wake keeps crash intent: '+outcome,async()=>{
 const f=fixture(outcome.startsWith('fresh-')?'crash-fresh-mount':'ack','HandheldCompanion.Devices.GPD.GPDWin5',true,async()=>({recoverable:true,active:false,revision:73,curve:f.curve}));
 let stop;
 try{
  await f.boot(true,false); f.setPreference(false);
  f.setNative({known:true,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,physicalWakeObserved:true});
  stop=f.watchSharedWake(); await f.flush();
  if(outcome.startsWith('fresh-unreadable-'))f.failFanReads(1);
  if(outcome.startsWith('fresh-unbound-'))f.failFanReads(2);
  const restored=f.observe(f.life.reattachAfterRendererRestart()); await f.advance(16000);
  assert.equal(f.starts,0,'mount launched a Host before the first public wake grant');
  assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'),'mount wrote before the first public wake grant');
  assert.notEqual(restored.status,'pending','mount blocked past its bounded wait');
  assert(f.life.hasDeferredCrashRecovery,'the same crash intent was not kept for the public wake');
  if(outcome==='boot-opt-in'){
   f.setPreference(true); const auto=f.auto();await f.advance(2000);
   assert.equal(auto.status,'fulfilled');assert.equal(f.starts,0,'boot preference took over a pending crash curve');
   assert.equal(f.manuals,0,'boot preference requested a manual wake for the pending crash curve');
  }
  if(outcome.endsWith('off')){
   const off=f.observe(f.life.disable()); await f.advance(500); assert.equal(off.status,'fulfilled',off.error??'OFF did not settle');
  }else if(outcome.endsWith('new-sleep')){
   f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
   f.rootSuspend({generation:5,scope:'fan-only'});
  }
  f.setNative({generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  f.publishFanWake({fanWakeGeneration:4,publicationRevision:2}); await f.advance(2500);
  const expected=Number(!outcome.endsWith('off')&&!outcome.endsWith('new-sleep')), count=name=>f.host.calls.filter(c=>c===name).length;
  assert.equal(f.starts,expected,'the later public grant did not start exactly one authorised Host');
  assert.equal(count('acquire'),expected); assert.equal(count('enable'),expected); assert.equal(f.manuals,0);
  if(expected)assert.deepEqual(f.host.snapshot().activeCurve,f.curve);
  return {outcome,starts:f.starts,writes:count('enable')};
 }finally{stop?.();await f.done();}
});
await check('background-ready fresh ordinary crash retries one unavailable power read without a wake',async()=>{
 const f=fixture('crash-fresh-mount','HandheldCompanion.Devices.GPD.GPDWin5',true,
  async()=>({recoverable:true,active:false,revision:73,curve:f.curve}));
 try{
  await f.boot(true,false); f.setPreference(false);
  f.setNative({known:true,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,physicalWakeObserved:false});
  f.failFanReads(1);
  const restored=f.observe(f.life.reattachAfterRendererRestart()); await f.advance(2500);
  assert.equal(restored.status,'fulfilled',restored.error??'ordinary crash did not settle');
  assert.equal(restored.value,true); assert.equal(f.starts,1); assert.equal(f.manuals,0);
  assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
  assert.deepEqual(f.host.snapshot().activeCurve,f.curve);
 }finally{await f.done();}
});
for(const boundary of ['wake','off','next-sleep','stale-native','record-lost'])await check('background-ready verified wake crosses the crash recovery read: '+boundary,async()=>{
 let activityReads=0,releaseRecord;
 const record=()=>({recoverable:true,active:false,revision:73,curve:f.curve});
 const reread=new Promise(resolve=>{releaseRecord=resolve;});
 const f=fixture('crash-fresh-mount','HandheldCompanion.Devices.GPD.GPDWin5',true,
  async()=>{const read=++activityReads;return read<=2?record():read===3?null:await reread;});
 let stop;
 try{
  await f.boot(true,false);f.setPreference(false);stop=f.watchSharedWake();await f.flush();
  f.setNative({known:true,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,physicalWakeObserved:false});
  f.holdNextFanRead();
  const restored=f.observe(f.life.reattachAfterRendererRestart());await f.flush();
  assert.equal(restored.status,'pending');assert(!f.life.hasDeferredCrashRecovery);
  // The actual App observer confirms the wake. The wake owner has no usable
  // activity record while the original mount owns a previously read curve.
  f.setNative({generation:4,known:true,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true,
   sharedGeneration:4,suspendSerial:6,observedNotificationSequence:11});
  f.publishShared({generation:4,suspendSerial:6,observedNotificationSequence:11,publicationRevision:2,
   resumeReady:true,fanWakeGeneration:4});await f.advance(500);
  assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,4,'real shared wake must already be verified');
  assert.equal(f.starts,0,'wake must have finished without a usable activity record');
  if(boundary==='off'){
   const off=f.observe(f.life.disable());await f.advance(500);assert.equal(off.status,'fulfilled',off.error);
  }
  if(boundary==='next-sleep'){
   f.publishShared({generation:5,suspendSerial:7,observedNotificationSequence:12,publicationRevision:3,phase:'suspending',
    resumeReady:false,hardwareWritesAllowed:false,fanWakeGeneration:0});
   f.setNative({generation:5,sharedGeneration:5,suspendSerial:7,observedNotificationSequence:12,
    phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});f.rootPublicSuspend(5);
  }
  if(boundary==='stale-native')f.setNative({observedNotificationSequence:10});
  releaseRecord(boundary==='record-lost'?null:record());f.releaseFanRead();await f.advance(18000);
  const expected=Number(boundary==='wake');
  assert.equal(restored.status,'fulfilled',restored.error??'crash recovery did not settle');
  assert.equal(f.starts,expected,'current verified wake must carry the single original crash intent');
  assert.equal(f.host.calls.filter(c=>c==='enable').length,expected);assert.equal(f.manuals,0);
  if(expected)assert.deepEqual(f.host.snapshot().activeCurve,f.curve);
  return {boundary,starts:f.starts,activityReads};
 }finally{stop?.();await f.done();}
});
for(const [holdRead,boundary] of [[1,'wake'],[2,'wake'],[1,'off'],[2,'new-sleep'],[1,'record-lost']])await check(`background-ready verified wake crosses activity read ${holdRead}: ${boundary}`,async()=>{
 let activityReads=0,release;
 const held=new Promise(resolve=>{release=resolve;});
 const record=()=>({recoverable:true,active:false,revision:73,curve:f.curve});
 const f=fixture('crash-fresh-mount','HandheldCompanion.Devices.GPD.GPDWin5',true,
  async()=>{const read=++activityReads;return read===holdRead?await held:read===holdRead+1||boundary==='record-lost'&&read>holdRead?null:record();});
 let stop;
 try{
  await f.boot(true,false);f.setPreference(false);stop=f.watchSharedWake();await f.flush();
  const restored=f.observe(f.life.reattachAfterRendererRestart());await f.flush();
  assert.equal(activityReads,holdRead,'the selected activity read must already be in flight');
  assert.equal(restored.status,'pending');
  f.setNative({generation:4,known:true,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true,
   sharedGeneration:4,suspendSerial:6,observedNotificationSequence:11});
  f.publishShared({generation:4,suspendSerial:6,observedNotificationSequence:11,publicationRevision:2,
   resumeReady:true,fanWakeGeneration:4});await f.advance(500);
  assert.equal(activityReads,holdRead+1,'the wake owner read before the held activity reply');
  assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,4);assert.equal(f.starts,0);
  if(boundary==='off'){
   const off=f.observe(f.life.disable());await f.advance(500);assert.equal(off.status,'fulfilled',off.error);
  }
  if(boundary==='new-sleep'){
   f.publishShared({generation:5,suspendSerial:7,observedNotificationSequence:12,publicationRevision:3,phase:'suspending',
    resumeReady:false,hardwareWritesAllowed:false,fanWakeGeneration:0});
   f.setNative({generation:5,sharedGeneration:5,suspendSerial:7,observedNotificationSequence:12,
    phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});f.rootPublicSuspend(5);
  }
  release(record());await f.advance(18000);
  const expected=Number(boundary==='wake');
  assert.equal(restored.status,'fulfilled',restored.error??'crash recovery did not settle');
  assert.equal(f.starts,expected,'current verified wake must retain the original crash intent');
  assert.equal(f.host.calls.filter(c=>c==='enable').length,expected);assert.equal(f.manuals,0);
  if(expected)assert.deepEqual(f.host.snapshot().activeCurve,f.curve);
  return {holdRead,boundary,starts:f.starts,activityReads};
 }finally{stop?.();await f.done();}
});
for(const boundary of ['ready-first','ready-already-cached','no-public-ready','off','new-sleep','stale-native'])await check('background-ready crash registration catches the current shared fact: '+boundary,async()=>{
 const f=fixture('crash-fresh-mount','HandheldCompanion.Devices.GPD.GPDWin5',true,
  async()=>({recoverable:true,active:false,revision:73,curve:f.curve}));
 let stop;
 try{
  await f.boot(true,false);f.setPreference(false);stop=f.watchSharedWake();await f.flush();
  const ready={known:true,phase:'ready',resumeReady:false,notificationPending:false,hardwareWritesAllowed:true,
   publicationRevision:2,observedNotificationSequence:11};
  const nativeReady={known:true,phase:'ready',resumeReady:false,notificationPending:false,hardwareWritesAllowed:true,
   physicalWakeObserved:false,sharedGeneration:4,suspendSerial:6,observedNotificationSequence:11};
  f.publishShared({generation:4,suspendSerial:6,known:false,phase:'ready',hardwareWritesAllowed:false});
  if(boundary==='ready-already-cached')f.publishShared(ready);
  f.setNative({...nativeReady,observedNotificationSequence:9});
  f.holdNextFanRead();
  const restored=f.observe(f.life.reattachAfterRendererRestart());await f.flush();
  assert.equal(f.starts,0);assert.equal(f.life.hasDeferredCrashRecovery,false,'public delivery must precede deferred registration');
  if(boundary!=='no-public-ready'&&boundary!=='ready-already-cached')f.publishShared(ready);
  if(boundary==='new-sleep')f.publishShared({suspendSerial:7,observedNotificationSequence:12,publicationRevision:3});
  if(boundary==='off'){
   const off=f.observe(f.life.disable());await f.flush();assert.equal(off.status,'fulfilled',off.error);
  }
  f.setNative(boundary==='stale-native'?{...nativeReady,observedNotificationSequence:10}:nativeReady);
  f.failFanReads(1);f.releaseFanRead();await f.advance(3000);
  const expected=Number(boundary==='ready-first'||boundary==='ready-already-cached');
  const reads=()=>f.calls.filter(c=>c.cmd==='power.fanLifecycle').length;
  const readsAfterAttempt=reads();await f.advance(60000);
  assert.equal(restored.status,'fulfilled',restored.error??'crash recovery did not settle');
  assert.equal(f.starts,expected,'a current coordinated Ready must start once even without a later publication');
  assert.equal(f.host.calls.filter(c=>c==='enable').length,expected);
  assert.equal(f.manuals,0);assert.equal(reads(),readsAfterAttempt,'idle crash recovery must not poll');
  if(expected)assert.deepEqual(f.host.snapshot().activeCurve,f.curve);
  return {boundary,reads:readsAfterAttempt,starts:f.starts};
 }finally{stop?.();await f.done();}
});
for(const boundary of ['ordinary','duplicate','off','new-sleep','native-held','stale-native','off-during-read','hold-during-open','hold-during-receipt'])await check('background-ready ordinary public readback revisits an unreadable crash: '+boundary,async()=>{
 const f=fixture('crash-fresh-mount','HandheldCompanion.Devices.GPD.GPDWin5',true,
  async()=>({recoverable:true,active:false,revision:73,curve:f.curve}));
 let stop;
 try{
  await f.boot(true,false);f.setPreference(false);
  stop=f.watchSharedWake();await f.flush();
  f.publishShared({generation:4,suspendSerial:6,known:false,phase:'ready',hardwareWritesAllowed:false});await f.flush();
  f.setNative({known:true,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,physicalWakeObserved:false,
   sharedGeneration:4,suspendSerial:6,observedNotificationSequence:11});
  f.failFanReads(2);
  const restored=f.observe(f.life.reattachAfterRendererRestart());await f.advance(1200);
  assert.equal(restored.status,'fulfilled');assert.equal(restored.value,false);
  assert(f.life.hasDeferredCrashRecovery);assert.equal(f.starts,0);
  if(boundary==='off'){
   const off=f.observe(f.life.disable());await f.advance(500);assert.equal(off.status,'fulfilled',off.error);
  }
  if(boundary==='native-held')f.setNative({resumeReady:false,hardwareWritesAllowed:false,physicalWakeObserved:true});
  let release,entered=false;
  if(boundary==='off-during-read')f.holdNextFanRead();
  if(boundary==='hold-during-open'||boundary==='hold-during-receipt'){
   const method=boundary==='hold-during-open'?'open':'enable';
   const original=f.host[method].bind(f.host), barrier=new Promise(resolve=>{release=resolve;});
   f.host[method]=async(...args)=>{const receipt=await original(...args);entered=true;await barrier;return receipt;};
  }
  f.publishShared({known:true,phase:'ready',resumeReady:false,notificationPending:false,hardwareWritesAllowed:true,
   ...(boundary==='new-sleep'?{suspendSerial:7}:{}),publicationRevision:2,observedNotificationSequence:11});
  if(boundary==='duplicate')f.publishShared({publicationRevision:3,observedNotificationSequence:11});
  if(boundary==='stale-native')f.publishShared({publicationRevision:3,observedNotificationSequence:12});
  if(boundary==='off-during-read'){
   await f.flush();const off=f.observe(f.life.disable());f.releaseFanRead();
   await f.advance(500);assert.equal(off.status,'fulfilled',off.error);
  }
  if(release){
   await f.advance(1200);assert(entered,'expected a real Host call in flight');
   f.publishShared({known:false,notificationPending:true,publicationRevision:3,observedNotificationSequence:12});
   release();
  }
  await f.advance(2500);
  const expected=Number(boundary==='ordinary'||boundary==='duplicate');
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(f.starts,expected+Number(!!release),'ordinary readback should start only under its current scope');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,expected+Number(boundary==='hold-during-receipt'));
  assert.equal(writes,expected+Number(boundary==='hold-during-receipt'));assert.equal(f.manuals,0);
  if(expected)assert.deepEqual(f.host.snapshot().activeCurve,f.curve);
  else assert.equal(f.life.hasActiveControlIntent(),false,'old readback must not publish or retain its local curve');
  if(boundary==='off-during-read'){
   const manual=await f.begin();await f.advance(1800);
   assert.equal(manual.status,'fulfilled',manual.error??'explicit ON after cancelled readback did not settle');
   assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
  }
  return {boundary,writes,manualAfterOff:boundary==='off-during-read'};
 }finally{stop?.();await f.done();}
});
for(const outcome of ['awake','regrant','off','new-sleep'])await check('native-owner real HTTP renderer lease handoff keeps public wake authority: '+outcome,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5',true);let release;try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});
  f.setNative({known:true,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true});f.rootReady();
  await f.advance(1800);assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'initial wake did not apply');
  // Host can revoke its temporary resume lease and leave the curve active.
  f.host.lease=null;f.host.calls=[];
  const getState=f.host.getState.bind(f.host);
  f.host.getState=async()=>{const state={...await getState(),hardwareWritesEnabled:true,hardwareWrites:true,hardwareWritesObserved:true};await new Promise(resolve=>{release=resolve;});return state;};
  const attached=f.observe(f.life.reattachAfterRendererRestart());await f.flush();assert(release,'the authenticated Host read must be in flight');
  if(outcome!=='awake')f.setNative({resumeReady:false});
  f.host.getState=getState;release();await f.advance(1800);
  assert.equal(attached.status,'fulfilled',attached.error??'reattach did not settle');
  const count=name=>f.host.calls.filter(c=>c===name).length;
  assert.equal(count('acquire'),Number(outcome==='awake'),'Ready Host acquired a new lease without current public wake permission');
  assert.equal(count('enable'),0);assert.equal(attached.value,outcome==='awake');
  if(outcome!=='awake'){
   assert(f.life.recoveryActive,'pending control must be handed back to background recovery');
   if(outcome==='off'){
    const off=f.observe(f.life.disable());await f.advance(800);assert.equal(off.status,'fulfilled',off.error??'OFF did not settle');
   }else if(outcome==='new-sleep'){
    f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
    f.rootSuspend({generation:5,scope:'fan-only'});
   }
   f.setNative({generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});await f.advance(9500);
  }
  assert.equal(count('acquire'),Number(outcome==='awake'||outcome==='regrant'));
  assert.equal(count('enable'),Number(outcome==='regrant'));
  assert.equal(count('open'),0);assert.equal(count('events'),0);assert.equal(f.starts,1);assert.equal(f.manuals,0);
  return {outcome,acquires:count('acquire'),writes:count('enable')};
 }finally{release?.();await nativeDone(f);}
});
for(const outcome of ['grant','off','new-sleep'])await check('native-owner real HTTP renderer lease waits for the first public wake: '+outcome,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5',true);
 let stop, getState;
 try{
  await nativeBoot(f); f.setPreference(false);
  f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});
  // A resident Host may have completed its HC handshake with the curve
  // retained while the renderer still has to recover its client lease.
  f.host.stateName='Ready'; f.host.lease=null; f.host.calls=[];
  getState=f.host.getState;
  f.host.getState=async()=>({...await getState.call(f.host),hardwareWritesEnabled:true,hardwareWrites:true,hardwareWritesObserved:true});
  f.setNative({known:true,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,physicalWakeObserved:true});
  stop=f.watchSharedWake(); await f.flush();
  const attached=f.observe(f.life.reattachAfterRendererRestart()); await f.advance(1800);
  const count=name=>f.host.calls.filter(c=>c===name).length;
  assert.equal(attached.status,'fulfilled',attached.error??'reattach held the queue');
  assert.equal(count('acquire'),0,'renderer lease requested before the first public wake');
  assert.equal(count('enable'),0);
  if(outcome==='off'){
   const off=f.observe(f.life.disable()); await f.advance(800); assert.equal(off.status,'fulfilled',off.error??'OFF did not settle');
  }else if(outcome==='new-sleep'){
   f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
   f.rootSuspend({generation:5,scope:'fan-only'});
  }
  f.setNative({generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  f.publishFanWake({fanWakeGeneration:4,publicationRevision:2}); await f.advance(9500);
  const expected=Number(outcome==='grant');
  assert.equal(count('acquire'),expected); assert(count('enable')<=expected,'unexpected repeated or retired curve write');
  assert.equal(count('open'),0); assert.equal(count('events'),0); assert.equal(f.starts,1); assert.equal(f.manuals,0);
  return {outcome,acquires:count('acquire'),writes:count('enable')};
 }finally{stop?.();if(getState)f.host.getState=getState;await nativeDone(f);}
});
for(const outcome of ['awake','regrant','off','new-sleep'])await check('native-owner real HTTP expired lease respects current public wake: '+outcome,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});
  f.setNative({known:true,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,physicalWakeObserved:true});f.rootCommittedReady();
  await f.advance(1800);assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'original wake did not establish control');
  f.host.calls=[];
  const heartbeat=f.host.heartbeat.bind(f.host);let expired=false;
  f.host.heartbeat=async()=>{
   if(expired)return heartbeat();
   expired=true;f.host.lease=null;f.host.calls.push('heartbeat-expired');
   if(outcome!=='awake')f.setNative({phase:'ready',resumeReady:false,physicalWakeObserved:true,hardwareWritesAllowed:true});
   throw Object.assign(new Error('expired lease'),{code:'LEASE_INVALID'});
  };
  await f.advance(5000);assert(expired,'the normal maintenance tick did not run');
  if(outcome!=='awake'){
   assert(!f.host.calls.some(c=>['open','events','acquire','enable'].includes(c)),
    'an expired lease reached the writer without the current public wake');
   assert(f.life.recoveryActive,'the expired lease did not hand the curve to the existing recovery owner');
   if(outcome==='off'){
    const off=f.observe(f.life.disable());await f.advance(800);assert.equal(off.status,'fulfilled',off.error??'OFF did not settle');
   }else if(outcome==='new-sleep'){
    f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
    f.rootSuspend({generation:5,scope:'fan-only'});
   }
   f.setNative({generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
   await f.advance(8000);
  }
  const trace=f.host.calls, count=name=>trace.filter(c=>c===name).length;
  const repaired=outcome==='awake'||outcome==='regrant';
  assert.equal(count('acquire'),Number(repaired));assert.equal(count('enable'),Number(repaired));
  assert.equal(count('open'),0);assert.equal(count('events'),0);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  return {outcome,acquires:count('acquire'),writes:count('enable'),starts:f.starts,manualRequests:f.manuals};
 }finally{await nativeDone(f);}
});
await check('native-owner real HTTP delayed native permission preserves the newest explicit curve',async()=>{
 const f=fixture('native-owner-deferred-close','HandheldCompanion.Devices.GPD.GPDWin5');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.calls=[];
  const writes=[],enable=f.host.enable.bind(f.host);
  f.host.enable=async request=>{writes.push(request.nodes);return enable();};
  f.setNative({phase:'resuming',resumeReady:true,hardwareWritesAllowed:false});
  const wake=f.observe(f.life.resumeAfterCommittedPowerWake(4));await f.advance(100);
  assert.notEqual(wake.status,'pending');assert(f.life.recoveryActive);
  const latest=f.curve.map((node,i)=>({...node,dutyPercent:i===2?82:node.dutyPercent}));
  const apply=f.observe(f.life.apply(latest));await f.advance(1000);
  assert.equal(f.native.hardwareWritesAllowed,false,'fixture released the native gate prematurely');
  assert.equal(apply.status,'pending',apply.error??'manual request settled without write permission');
  assert.deepEqual(writes,[],'wrote a curve before native permission');
  assert(!f.host.calls.includes('acquire'),'acquired a lease before native permission');
  f.releaseCleanup();await f.advance(8000);
  assert.equal(apply.status,'fulfilled',apply.error??'the new curve still needs another click');
  assert.deepEqual(writes,[latest],'old or duplicate curve writes after the gate opens');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);
  assert.equal(f.starts,1);assert.equal(f.life.recoveryActive,false);
  return {writes:writes.length,customDuty:writes[0][2].dutyPercent,starts:f.starts};
 }finally{await nativeDone(f);}
});
for(const boundary of ['ready','off','new-sleep','expired'])await check('native-owner real HTTP committed wake survives an unavailable Native write fact: '+boundary,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.calls=[];
  f.setNative({known:false,reason:'power-context-changing',generation:undefined,phase:undefined,resumeReady:false,hardwareWritesAllowed:false});
  const wake=f.observe(f.life.resumeAfterCommittedPowerWake(4));
  await f.advance(1500);
  assert.notEqual(wake.status,'pending','the first committed wake attempt must settle');
  assert(!f.host.calls.some(c=>c==='resume'||c==='acquire'||c==='enable'),'changing Native facts must block Host resume and writes');
  if(boundary==='off'){
   const off=f.observe(f.life.disable());await f.advance(1000);assert.equal(off.status,'fulfilled',off.error??'OFF pending');
  }else if(boundary==='new-sleep'){
   f.setNative({known:true,generation:5,phase:'suspending',resumeReady:false});
   f.rootSuspend({generation:5,scope:'fan-only'});
  }else if(boundary==='expired'){
   await f.advance(20000);
   assert.equal(f.life.recoveryActive,false,'unavailable Native facts kept the read loop alive after the deadline');
   const duplicate=f.observe(f.life.resumeAfterCommittedPowerWake(4));await f.advance(1500);
   assert.equal(duplicate.status,'fulfilled');
   assert.equal(f.life.recoveryActive,false,'duplicate publication reopened the same expired read budget');
  }
  if(boundary!=='new-sleep')f.setNative({known:true,generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  await f.advance(8000);
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,boundary==='ready'?1:0,'incorrect replay without a second wake event or a click');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  if(boundary==='ready')assert.equal(f.life.recoveryActive,false);
  return {boundary,writes,starts:f.starts,manualRequests:f.manuals};
 }finally{await nativeDone(f);}
});
for(const boundary of ['ready','off'])await check('native-owner real HTTP public Ready joins an in-flight Fan hint: '+boundary,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5');let stop;try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.calls=[];
  stop=f.watchSharedWake();await f.flush();
  f.setNative({known:false,reason:'power-context-changing',generation:undefined,phase:undefined,resumeReady:false,hardwareWritesAllowed:false});
  const readCount=()=>f.calls.filter(c=>c.cmd==='power.fanLifecycle').length;
  const before=readCount();
  f.holdNextFanRead();f.rootReady();await f.flush();
  assert.equal(readCount(),before+1,'the original Fan hint must own the first read');
  f.publishShared({generation:4,publicationRevision:2,observedNotificationSequence:11});await f.flush();
  f.rootCommittedReady();f.rootCommittedReady();await f.flush();
  assert.equal(readCount(),before+1,'the public Ready must join the existing read');
  if(boundary==='off'){
   const off=f.observe(f.life.disable());await f.advance(1000);assert.equal(off.status,'fulfilled',off.error??'OFF pending');
  }
  f.releaseFanRead();await f.advance(1500);
  assert(!f.host.calls.some(c=>['resume','acquire','enable'].includes(c)),'no Host work without current Fan proof');
  f.setNative({known:true,generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  await f.advance(8000);
  const writes=f.host.calls.filter(c=>c==='enable').length;
  assert.equal(writes,boundary==='ready'?1:0,'the merged wake must recover without a third notification, unless cancelled');
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,writes);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);assert.equal(f.life.recoveryActive,false);
  return {boundary,writes,starts:f.starts,manualRequests:f.manuals};
 }finally{stop?.();await nativeDone(f);}
});
for(const hold of ['native unavailable','public pending'])await check('native-owner real HTTP first manual curve waits during '+hold,async()=>{
 const f=fixture('native-owner','HandheldCompanion.Devices.GPD.GPDWin5');let stop;try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  await f.host.resume({generation:4});f.host.calls=[];
  const localReady=f.life.observePowerBoundary.bind(f.life);
  localReady('resume-ready',4);
  if(hold==='public pending'){
   stop=f.watchSharedWake();f.publishShared({suspendSerial:12});await f.flush();
   f.publishShared({notificationPending:true});await f.flush();
   f.setNative({sharedGeneration:3,suspendSerial:12,observedNotificationSequence:10,publicationRevision:1,
    known:true,notificationPending:false,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  }else f.setNative({known:false,reason:'power-context-changing',generation:undefined,phase:undefined,resumeReady:false,hardwareWritesAllowed:false});
  const request=f.observe(f.life.start({manualRecovery:true}).then(()=>f.life.apply(f.curve)));
  await f.advance(1500);
  assert.equal(request.status,'pending',request.error??'manual request did not wait for native');
  assert(!f.host.calls.some(c=>c==='acquire'||c==='enable'));
  f.setNative({known:true,generation:4,phase:'ready',resumeReady:true,hardwareWritesAllowed:true});
  if(hold==='public pending')f.publishShared({notificationPending:false,publicationRevision:2});
  await f.advance(8000);
  assert.equal(request.status,'fulfilled',request.error??'manual request did not continue');
  assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
  assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);
  assert.equal(f.starts,1);assert.equal(f.manuals,0);
  return {hold,writes:1,manualRequests:f.manuals};
 }finally{stop?.();await nativeDone(f);}
});
await check('native-owner real HTTP explicit zero-work rejection leaves the same wake eligible',async()=>{
 const f=fixture('native-owner');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765');
  const context=f.life.admissionContext('observed-wake-pending',f.life.controlIntentRevisionValue,4);
  f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,notificationPending:true});
  const early=f.observe(f.life.requestNativeResumeOnce(context,true));await f.flush();
  assert.equal(early.value?.reason,'wake_not_ready');assert.equal(f.manuals,1);
  f.setNative({notificationPending:false});
  const settled=f.observe(f.life.requestNativeResumeOnce(context,true));await f.flush();
  assert.equal(settled.value?.reason,'observed_wake');
  const duplicate=f.observe(f.life.requestNativeResumeOnce(context,true));await f.flush();
  assert.equal(duplicate.value?.ok,true);
  const requests=f.calls.filter(x=>x.cmd==='power.fanManualWake');
  assert.equal(requests.length,2);assert(requests.every(x=>x.args.observedWakeOnly===true));
  assert(!f.calls.some(x=>x.cmd==='http.request'&&x.args.url.endsWith('/api/resume')));
  return {explicitRejects:1,observedWakeCommands:2,manualCommands:0,rendererResumePosts:0};
 }finally{await nativeDone(f);}
});
for(const observedWakeOnly of [false,true])await check('native-owner concurrent callers and missing '+(observedWakeOnly?'observation':'manual')+' ACK reuse one original command',async()=>{
 const f=fixture('native-owner-missing');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765');
  const context=f.life.admissionContext('native-owner-fixture',f.life.controlIntentRevisionValue,4);
  if(observedWakeOnly)f.setNative({phase:'ready',resumeReady:true,hardwareWritesAllowed:true,notificationPending:false});
  const a=f.observe(f.life.requestNativeResumeOnce(context,observedWakeOnly)),b=f.observe(f.life.requestNativeResumeOnce(context,observedWakeOnly));
  await f.advance(3000);assert.equal(a.status,'fulfilled');assert.equal(b.status,'fulfilled');assert.equal(a.value,null);assert.equal(b.value,null);assert.equal(f.manuals,1);
  const c=f.observe(f.life.requestNativeResumeOnce(context,observedWakeOnly));await f.flush();assert.equal(c.status,'fulfilled');assert.equal(f.manuals,1,'lost ACK refunded a command');
  return {nativeCommands:f.manuals,unknownAcknowledgementRetained:true};
 }finally{await nativeDone(f);}
});
await check('native-owner new explicit intent may request recovery again',async()=>{
 const f=fixture('native-owner');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765');
  const a=f.observe(f.life.requestNativeResumeOnce(f.life.admissionContext('native-owner-fixture',f.life.controlIntentRevisionValue,4)));await f.flush();assert.equal(a.status,'fulfilled');
  f.life.controlIntentRevisionValue+=1;const b=f.observe(f.life.requestNativeResumeOnce(f.life.admissionContext('native-owner-new-intent',f.life.controlIntentRevisionValue,4)));await f.flush();assert.equal(b.status,'fulfilled');assert.equal(f.manuals,2);
  return {nativeCommands:2,distinctExplicitIntents:2};
 }finally{await nativeDone(f);}
});

for(const boundary of ['close','new-sleep'])await check('native-owner cancel '+boundary+' retires queued Native request before dispatch',async()=>{
 const f=fixture('native-owner');try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765');
  const context=f.life.admissionContext('native-owner-cancel-fixture',f.life.controlIntentRevisionValue,4);
  const p=f.observe(f.life.requestNativeResumeOnce(context));
  if(boundary==='close')f.life.cancelControlForClose();
  else{f.life.setPowerGeneration(5);f.life.observePowerBoundary('suspending',5);}
  await f.flush();assert.equal(p.status,'fulfilled');assert.equal(p.value,null);
  assert.equal(f.manuals,0,'retired owner emitted a Native recovery command');
  assert(!f.calls.some(x=>x.cmd==='http.request'&&x.args.url.endsWith('/api/resume')));
  return {boundary,nativeCommands:0,rendererResumePosts:0};
 }finally{await nativeDone(f);}
});
const report={ok:results.every(r=>r.ok),checks:results.length,failed:results.filter(r=>!r.ok).length,hardwareCalls:0,cpuBenchmark:false,scope:'actual App wake/auto observers + FanView suspend/start/commit functions + Fan wake API/shared IPC/public lifecycle; inert UI sinks, native power facts, process and Host; declared software write receipts only; no real native operation, HC, sleep or hardware',sourceBinding:binding,results};
if(nativeStartupReplayBytes)report.nativeStartupReplay={path:nativeStartupReplayPath,sha256:createHash('sha256').update(nativeStartupReplayBytes).digest('hex'),scope:'recorded public wire and claim reply from compiled Native fixture; Fan-specific reads and Host remain simulated'};
if(nativeRawReplayBytes)report.nativeRawReceiptReplay={path:nativeRawReplayPath,sha256:createHash('sha256').update(nativeRawReplayBytes).digest('hex'),scope:'ordered Native receipt and actual public projection under controlled S0 inputs; Fan-specific reads and Host remain simulated'};
if(nativeStartupOffBytes)report.nativeStartupOffReplay={path:nativeStartupOffPath,sha256:createHash('sha256').update(nativeStartupOffBytes).digest('hex'),scope:'recorded Native candidate and OFF status replies; claims and Host transport are inert'};
if(nativeCloseWireBytes)report.nativeCloseReplay={path:path.resolve(nativeCloseWirePath),sha256:createHash('sha256').update(nativeCloseWireBytes).digest('hex'),scope:'recorded public state after Close; Fan IPC and Host are inert'};
if(nativeS4Bytes)report.nativeS4Replay={path:nativeS4Path,sha256:createHash('sha256').update(nativeS4Bytes).digest('hex'),scope:'actual Native Fan notification and power readbacks after controlled S4 input and common worker receipt; Host and device operations are inert'};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({ok:report.ok,checks:report.checks,failed:report.failed,scope:report.scope}));process.exitCode=report.ok?0:1;
