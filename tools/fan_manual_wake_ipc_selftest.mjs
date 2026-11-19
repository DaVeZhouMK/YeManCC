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
assert(appSuspendObserver,'actual root Fan suspend observer missing');
const autoStartState=appSource.indexOf('let fanAutoStartChain:');
const autoStartEnd=appSource.indexOf('const router = useRouter();',autoStartState);
assert(autoStartState>=0&&autoStartEnd>autoStartState,'actual root Fan auto state missing');
const autoStartSource=appSource.slice(autoStartState,autoStartEnd);
const appReadyObserver=appSource.match(/function onFanResumeReady\(resumeGeneration: number\): void \{[\s\S]*?\n\}/)?.[0];
assert(autoStartSource.includes('fanHostLifecycle.start(')&&appReadyObserver,'actual root auto/ready observer missing');
const viewPath=process.argv[4]?path.resolve(process.argv[4]):path.join(project,'src/views/FanView.vue');
const viewSource=fs.readFileSync(viewPath,'utf8');
const viewFunction=name=>{const match=viewSource.match(new RegExp('(?:async )?function '+name+'\\([\\s\\S]*?\\n\\}'));assert(match,'actual page function missing: '+name);return match[0];};
const pageFunctions=['onFanPowerSuspending','ensureSupported','confirmFanHostBeforeWrite','commitControlCurve','publishControlIntent','hostHasLiveControl'].map(viewFunction).join('\n');
const observerModule=`export function makeFanSuspendObserver(fanHostLifecycle: any, setFanControlActive: (active: boolean) => void) { ${appSuspendObserver}; return onFanPhysicalSuspending; }
export function makeFanWakeApp(fanHostLifecycle: any, curve: any[], settings: {on:boolean}) {
 const aiFanMockActive=false, latestResumeGeneration=4;
 const readSettingsSection=async<T>()=>({fanControl:settings.on}), readBootModeFanIntent=async()=>null;
 const getFanFeatureSettings=()=>({featureEnabled:true,preset:'balanced'}),getFanPresetCurve=()=>curve;
 const fanDiagnosticLog=()=>{};
 ${autoStartSource}
 ${appReadyObserver}
 return {ensureFanAutoStart,onFanResumeReady};
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
const files=['src/App.vue','src/views/FanView.vue','src/bridge/api.ts','src/bridge/fanHost.ts','src/bridge/ipc.ts','src/bridge/hardwareCoordinator.ts','tools/fixtures/fan_no_io_host.ts','tools/fan_manual_wake_ipc_selftest.mjs'];
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const binding=files.map(p=>({path:p==='src/views/FanView.vue'?viewPath:selected(p),sha256:hash(p==='src/views/FanView.vue'?viewPath:selected(p))}));
const bundle=await build({stdin:{contents:"export {powerLifecycle} from './src/bridge/api';export {HttpFanApiAdapter} from './src/bridge/fanApi';export {FanHostLifecycle} from './src/bridge/fanHost';export {NoIoHost} from './tools/fixtures/fan_no_io_host';export {rejectAllPending} from './src/bridge/ipc';",resolveDir:project,sourcefile:'fan-manual-wake-ipc-entry.ts'},bundle:true,write:false,format:'iife',globalName:'FanTest',platform:'browser',target:'es2022',define:{'import.meta.env':'{}'},plugins:beforeRoot?[{name:'bound-before-image',setup(b){b.onLoad({filter:/[\\/]src[\\/]bridge[\\/](?:api|fanHost)\.ts$/},a=>({contents:fs.readFileSync(selected(path.relative(project,a.path)),'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));}}]:[]});
const code=bundle.outputFiles[0].text;
function fixture(mode='ack',factoryType='HandheldCompanion.Devices.OneXPlayerX1Mini',qualifiedReceipts=false){
 let now=0,id=0,listener,manuals=0,heldReply,rootObserver,releaseCleanup,appWake,pageApi,pageVisible=false;const settings={on:true};
 const timers=new Map(),calls=[];
 let native={generation:3,phase:'ready',resumeReady:true,inputReady:true,hardwareWritesAllowed:true,hibernateAvailable:true};
 const timer=(fn,ms,interval=false)=>{const key=++id,delay=Math.max(1,Number(ms)||1);timers.set(key,{fn,due:now+delay,period:interval?delay:0});return key;};
 const clear=key=>timers.delete(key),win=new EventTarget();
 win.setTimeout=(fn,ms)=>timer(fn,ms);win.clearTimeout=clear;win.setInterval=(fn,ms)=>timer(fn,ms,true);win.clearInterval=clear;
 const deliver=(msg,result,error)=>listener?.({data:error?{id:msg.id,error}:{id:msg.id,result}});
 win.chrome={webview:{addEventListener:(_t,fn)=>listener=fn,postMessage(msg){calls.push(msg);
  if(msg.cmd==='fanLog.write'){queueMicrotask(()=>deliver(msg,{ok:true}));return;}
  if(msg.cmd==='power.fanLifecycle'){const result={...native};queueMicrotask(()=>deliver(msg,result));return;}
  if(msg.cmd==='http.request'&&mode.startsWith('native-owner')){
   const route=new URL(msg.args.url).pathname;
   if(route==='/api/resume')throw Error('renderer /api/resume must not be posted');
   const body=typeof msg.args.body==='string'?JSON.parse(msg.args.body):{};
   const names={'/api/state':'getState','/api/open':'open','/api/open-events':'openEvents','/api/acquire-control':'acquireControl','/api/enable':'enable','/api/preset':'applyPreset','/api/heartbeat':'heartbeat','/api/disable':'disable','/api/release-control':'releaseControl','/api/close':'close','/api/shutdown':'shutdown'};
   const fn=names[route];if(!fn)throw Error('unexpected inert HTTP route '+route);
   void Promise.resolve(host[fn](body)).then(value=>{if(route==='/api/acquire-control'){value.leaseId='safe-'+String(value.generation).padStart(32,'0');host.lease={...value};}return deliver(msg,{status:200,body:JSON.stringify({ok:true,...(route==='/api/acquire-control'||route==='/api/heartbeat'?{lease:value}:{state:value})}),headers:''});});
   return;
  }
  if(msg.cmd==='power.fanManualWake'){
   manuals++;assert.equal(msg.args.generation,native.generation,'manual call must bind the current generation');
   const observedWakeOnly=msg.args.observedWakeOnly===true;
   if(mode.startsWith('native-owner-deferred-close')&&!observedWakeOnly&&manuals===1){
    native={...native,phase:'resuming',resumeReady:true,hardwareWritesAllowed:false};
    releaseCleanup=()=>{native={...native,phase:'ready',resumeReady:true,hardwareWritesAllowed:true};};
    queueMicrotask(()=>deliver(msg,{ok:true,generation:native.generation,phase:'ready',admitted:true,resumeScheduled:false,resumeDisposition:'awaiting-power'}));
    return;
   }
   if (mode.startsWith('native-owner-deferred-close-') && msg.args.continueReadyOnly === true && manuals === 2) {
    if(mode.endsWith('-lost'))return;
    // A new Windows notice crosses the renderer Ready read and the Native gate.
    native={...native,phase:'resuming',hardwareWritesAllowed:false,notificationPending:true};
    releaseCleanup=()=>{native={...native,phase:'ready',hardwareWritesAllowed:true,notificationPending:false};};
    if(mode.endsWith('-capture'))queueMicrotask(()=>deliver(msg,{ok:true,generation:native.generation,phase:'ready',admitted:true,resumeScheduled:false,resumeDisposition:'awaiting-power'}));
    else queueMicrotask(()=>deliver(msg,{ok:false,generation:native.generation,phase:mode.endsWith('-resuming')?native.phase:'ready',admitted:false,reason:'wake_not_ready'}));
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
  URL,TextEncoder,TextDecoder,crypto:webcrypto,performance:{now:()=>now},structuredClone,queueMicrotask});
 vm.runInContext(code,context,{timeout:5000});vm.runInContext(observerBundle.outputFiles[0].text,context,{timeout:5000});const api=context.FanTest;
 const notifySuspend=(detail,type='ipc:fan.suspending')=>{rootObserver({detail});if(pageVisible)pageApi.onFanPowerSuspending(new context.CustomEvent(type,{detail}));};
 const observe=p=>{const result={status:'pending',value:undefined,error:null};void p.then(v=>{result.status='fulfilled';result.value=v;},e=>{result.status='rejected';result.error=String(e);});return result;};
 async function flush(){for(let n=0;n<80;n++)await Promise.resolve();await new Promise(setImmediate);}
 async function advance(ms){await flush();const target=now+ms;for(let n=0;n<1000;n++){const row=[...timers.entries()].filter(([,t])=>t.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];if(!row)break;const [key,t]=row;now=t.due;if(t.period)t.due+=t.period;else timers.delete(key);t.fn();await flush();}now=target;await flush();}
 let life,host,starts=0;
 const curve=[{tempC:0,dutyPercent:20},{tempC:40,dutyPercent:35},{tempC:70,dutyPercent:65},{tempC:100,dutyPercent:100}];
 async function boot(coldWake=false){host=new api.NoIoHost();host.factoryType=factoryType;host.generation=3;host.attempt=1;host.powerState='On';host.completeResume=true;
  if(qualifiedReceipts){let writtenCurve=null;const snapshot=host.snapshot.bind(host),enable=host.enable.bind(host);
   host.snapshot=()=>({...snapshot(),...(writtenCurve?{activeCurve:structuredClone(writtenCurve),lastControlWriteEvidence:'transported'}:{})});
   host.enable=async(nodes)=>{writtenCurve=structuredClone(nodes);return enable();};
  }
  life=new api.FanHostLifecycle({enabled:true,adapter:host,readNativeActivity:async()=>null,
   launcher:{start:async()=>({pid:111501+starts++,executable:'inert.exe'}),stop:async()=>{}},onControlDisplay(){}});
  rootObserver=context.FanRootObserver.makeFanSuspendObserver(life,()=>{});appWake=context.FanRootObserver.makeFanWakeApp(life,curve,settings);
  life.setPowerGeneration(3);life.observePowerBoundary('resume-ready',3);await life.start();
  if(coldWake){native={...native,generation:4,phase:'resuming',resumeReady:false,hardwareWritesAllowed:false};life.setPowerGeneration(4);life.observePowerBoundary('resuming',4);host.calls=[];pageApi=context.FanRootObserver.makeFanPage(life,curve);return;}
  await life.apply(curve);
  native={...native,generation:4,phase:'suspended',resumeReady:false,hardwareWritesAllowed:false};
  life.setPowerGeneration(4);life.observePowerBoundary('suspending',4);await life.suspend();await host.suspend();host.generation=4;host.lease=null;host.calls=[];
 }
 async function begin(){const pending=observe(mode.startsWith('wake-cleanup')?life.start({manualRecovery:true}).then(()=>life.apply(curve)):life.apply(curve));for(let n=0;n<20&&!calls.some(c=>c.cmd==='power.fanManualWake')&&pending.status==='pending';n++)await flush();return pending;}
 async function done(){const close=life?.close().catch(()=>{});api.rejectAllPending('fixture cleanup');await flush();if(close)await close;timers.clear();}
 return {api,calls,timers,flush,advance,observe,boot,begin,done,curve,get host(){return host;},get life(){return life;},get starts(){return starts;},get manuals(){return manuals;},get native(){return native;},auto(reason='boot'){return observe(appWake.ensureFanAutoStart(reason,reason==='resume'?4:0));},rootReady(generation=4){appWake.onFanResumeReady(generation);},setPreference(on){settings.on=on;},setNative(v){native={...native,...v};},pageBegin(){pageVisible=true;return observe(pageApi.begin());},get page(){return pageApi.snapshot();},pageSuspend(detail,type='ipc:fan.suspending'){pageApi.onFanPowerSuspending(new context.CustomEvent(type,{detail}));},rootSuspend(detail,type){notifySuspend(detail,type);},releaseCleanup(){releaseCleanup?.();},lateReply(){heldReply?.();}};
}
const results=[];
async function check(name,run){if(nativeOwnerCancelOnly&&!name.startsWith('native-owner cancel '))return;if(nativeOwnerRearmOnly&&!name.startsWith('native-owner real HTTP'))return;if(nativeOwnerOnly&&!name.startsWith('native-owner '))return;if(sharedReadyOnly&&!name.startsWith('shared-ready '))return;if(backgroundReadyOnly&&!name.startsWith('background-ready '))return;try{const facts=await run();results.push({name,ok:true,...(facts?{facts}:{})});console.log('PASS '+name);}catch(error){results.push({name,ok:false,error:String(error)});console.log('FAIL '+name+': '+error);}}
await check('manual-wake API retires a missing acknowledgement within three seconds',async()=>{const f=fixture('missing-committed');try{f.setNative({generation:4,phase:'suspended'});const p=f.observe(f.api.powerLifecycle.manualFanWake(4));await f.advance(2999);assert.equal(p.status,'pending');await f.advance(1);assert.equal(p.status,'rejected','missing manual wake acknowledgement never settled');assert.match(p.error,/IPC command timed out: power.fanManualWake/);assert.equal(f.manuals,1);assert.equal(f.timers.size,0);}finally{await f.done();}});
await check('same click observes a committed wake after lost reply and completes actual Open/Events/acquire/enable',async()=>{const f=fixture('missing-committed');try{await f.boot();const p=await f.begin();assert.equal(f.manuals,1);assert.equal(p.status,'pending');await f.advance(4001);assert.equal(p.status,'fulfilled',p.error??'same click remains blocked');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);assert.equal(f.manuals,1,'unknown native mutation is never blindly repeated');assert.equal(f.starts,1,'same exact Host owns the operation');f.lateReply();await f.flush();assert.equal(f.host.calls.filter(c=>c==='enable').length,1);return {manualRequests:f.manuals,starts:f.starts,trace:f.host.calls};}finally{await f.done();}});
await check('normal acknowledged rescue retains the same one-click path',async()=>{const f=fixture();try{await f.boot();const p=await f.begin();await f.flush();assert.equal(p.status,'fulfilled',p.error??'normal manual rescue pending');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.starts,1);}finally{await f.done();}});
for(const factoryType of ['HandheldCompanion.Devices.MSI.ClawA1M','HandheldCompanion.Devices.MSI.ClawA2VM','HandheldCompanion.Devices.MSI.ClawBZ2EM','HandheldCompanion.Devices.MSI.ClawCG3EM','HandheldCompanion.Devices.XboxROGAllyX','HandheldCompanion.Devices.OneXPlayerX1Mini','HandheldCompanion.Devices.LegionGoTablet'])await check('first cold wake click survives its own deferred Close notification: '+factoryType,async()=>{const f=fixture('wake-cleanup',factoryType);try{await f.boot(true);const revision=f.life.controlIntentRevision;const p=await f.begin();await f.flush();assert.equal(f.manuals,1,JSON.stringify({status:p.status,error:p.error,calls:f.host.calls,native:f.native,life:f.life.state}));assert.equal(f.life.controlIntentRevision,revision,'cleanup invalidated the current explicit intent');assert.equal(p.status,'pending',p.error??'same click was cancelled');assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'),'cleanup ACK must not grant hardware writes');f.releaseCleanup();await f.advance(1200);assert.equal(p.status,'fulfilled',p.error??'same click did not continue');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);assert.equal(f.manuals,1);assert.equal(f.starts,1);return {outcome:p.status,manualRequests:f.manuals,starts:f.starts,trace:f.host.calls};}finally{await f.done();}});
for(const cancel of ['off','new-sleep','same-generation-sleep','legacy-sleep','wrong-scope'])await check('deferred Close catchup cannot revive '+cancel,async()=>{const f=fixture('wake-cleanup');try{await f.boot(true);const p=await f.begin();await f.flush();if(cancel==='off'){const off=f.observe(f.life.disable());await f.advance(1200);assert.equal(off.status,'fulfilled',off.error??'OFF did not settle');}else{const generation=cancel==='new-sleep'?5:4;f.setNative({generation,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});f.rootSuspend({generation,scope:cancel==='wrong-scope'?'global':'fan-only',...(cancel==='legacy-sleep'?{}:{resumeCatchup:cancel==='wrong-scope'})});}f.releaseCleanup();await f.advance(4001);assert.notEqual(p.status,'pending');assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'),'cancelled catchup wrote');}finally{await f.done();}});
for(const reason of ['boot','resume'])await check('actual authorized auto '+reason+' survives its wake Close catchup without a second user click',async()=>{const f=fixture('wake-cleanup');try{await f.boot(true);const p=f.auto(reason);await f.flush();assert.equal(f.manuals,1);assert.equal(p.status,'pending');assert(!f.host.calls.includes('enable'));f.releaseCleanup();await f.advance(1200);assert.equal(p.status,'fulfilled',p.error??'auto startup stuck');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.starts,1);return {manualRequests:f.manuals,starts:f.starts,trace:f.host.calls};}finally{await f.done();}});
for(const revoke of ['preference','off'])await check('actual authorized auto rechecks '+revoke+' before writing after cleanup',async()=>{const f=fixture('wake-cleanup');try{await f.boot(true);const p=f.auto();await f.flush();if(revoke==='preference')f.setPreference(false);else{const off=f.observe(f.life.disable());await f.advance(1200);assert.equal(off.status,'fulfilled',off.error??'OFF blocked');}f.releaseCleanup();await f.advance(1200);assert.notEqual(p.status,'pending');assert(!f.host.calls.includes('enable'),'automatic startup ignored revoke');}finally{await f.done();}});
await check('a delayed cleanup event before actual native ready still preserves the first start',async()=>{const f=fixture('wake-cleanup-held-event');try{await f.boot(true);const p=await f.begin();await f.flush();assert.equal(p.status,'pending');const revision=f.life.controlIntentRevision;f.rootSuspend({generation:4,scope:'fan-only',resumeCatchup:true});assert.equal(f.life.controlIntentRevision,revision);f.releaseCleanup();await f.advance(1200);assert.equal(p.status,'fulfilled',p.error??'delayed event cancelled current start');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);}finally{await f.done();}});
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
await check('background-ready retained private cycle resumes and renews without opening the fan page',async()=>{
 const f=fixture('ack','HandheldCompanion.Devices.GPDWin5',true);
 try{
  await f.boot();
  // Field tuple: the Fan device cycle advances while the public cycle stays
  // unchanged. Only the actual root callback runs; no page or manual request.
  f.setNative({generation:4,sharedGeneration:3,phase:'ready',resumeReady:true,
   hardwareWritesAllowed:true,inputReady:false,known:true,notificationPending:false,
   scope:'fan-only',physicalWakeObserved:true,sharedPowerCommitted:false});
  await f.host.resume({generation:4});
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

async function nativeBoot(f){const p=f.observe(f.boot());for(let n=0;n<100&&p.status==='pending';n++)await f.advance(100);if(p.status!=='fulfilled')console.log(JSON.stringify({diagnostic:'boot',status:p.status,error:p.error,state:f.life.state,host:f.host.calls,ipc:f.calls.map(x=>({cmd:x.cmd,url:x.args?.url}))}));assert.equal(p.status,'fulfilled',p.error??'boot pending');}
async function nativeDone(f){const p=f.observe(f.done());for(let n=0;n<800&&p.status==='pending';n++)await f.advance(100);if(p.status==='pending')console.log(JSON.stringify({diagnostic:'cleanup',state:f.life.state,host:f.host.calls,ipc:f.calls.map(x=>({cmd:x.cmd,url:x.args?.url}))}));assert.equal(p.status,'fulfilled',p.error??'cleanup pending');}
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
for (const point of ['gate','resuming','capture','held','lost','new-sleep']) await check('native-owner real HTTP first click handles second Ready '+point,async()=>{
 const f=fixture('native-owner-deferred-close-'+point);try{
  await nativeBoot(f);f.life.adapter=new f.api.HttpFanApiAdapter('http://127.0.0.1:8765','0123456789abcdef'.repeat(4));
  const click=await f.begin();await f.advance(400);
  assert.equal(f.manuals,1);assert.equal(click.status,'pending');
  f.releaseCleanup();
  for(let i=0;i<16&&f.manuals<2&&click.status==='pending';i++)await f.advance(100);
  assert.equal(f.manuals,2);
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
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({ok:report.ok,checks:report.checks,failed:report.failed,scope:report.scope}));process.exitCode=report.ok?0:1;
