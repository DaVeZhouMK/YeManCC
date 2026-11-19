// Exact host/coordinator/router/clock/relay/desktop owner integration with inert IO.
// All native HWNDs, Steam statuses and snapshots here are synthetic test receipts.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=process.cwd(),out=path.resolve(process.env.YMCC_HUD_OPTIONAL_OUT||path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/global-followup-20261011/tests'));fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const mocks={
 // Transient notifications have separate consumer/renderer coverage; this fixture
 // isolates the persistent HUD data-demand and output-coordinator lifecycle.
 'src/bridge/monitorNoticeHost.ts':`export function startMonitorNoticeHost(){return {ready(){},update(){},stop(){}}}`,
 'src/bridge/monitorGameLogo.ts':"export const subscribeMonitorGameLogo=()=>()=>{};",
 'src/bridge/ipc.ts':"export const isNativeRuntime=true;export const invoke=(command,args,options)=>globalThis.__hudHostTest.invoke(command,args,options);export function on(name,fn){const f=globalThis.__hudHostTest;const listeners=f.events.get(name)||new Set();f.events.set(name,listeners);listeners.add(fn);return ()=>listeners.delete(fn)}",
 'src/bridge/settingsRepository.ts':"export function cachedBootStartupReceiptOwner(){return undefined}export async function loadSettings(){return structuredClone(globalThis.__hudHostTest.settings)}export async function updateSettings(edit){edit(globalThis.__hudHostTest.settings)}export async function readSettingsSection(name){const f=globalThis.__hudHostTest;f.settingsReads++;if(f.settingsReadFailures>0){--f.settingsReadFailures;throw Error('synthetic settings read failure')}const value=structuredClone(f.settings[name]||{});if(f.settingsReadGate){const wait=f.settingsReadGate;f.settingsReadGate=null;await wait}return value}export async function saveSettingsSection(name,patch){Object.assign(globalThis.__hudHostTest.settings[name],structuredClone(patch))}",
 'src/bridge/yeman.ts':"const f=()=>globalThis.__hudHostTest;export async function rtssRunning(){return f().rtss}export async function toggleRtss(show){if(!show)throw Error('RTSS must remain in background');++f().backendStarts;if(f().failRtssStart)throw Error('synthetic RTSS backend unavailable');f().rtss=show}export async function readOverlayLayout(){return f().layout}export async function setOverlayLayout(v){f().rtssLoads.push(v);if(v!=='off'&&f().failRtssLoad)throw Error('synthetic RTSS target load failure');f().layout=v==='off'?'Empty.ovl':({W:'YeManOBS-W-1.ovl',L:'YeManOBS-L-1.ovl',J:'YeManOBS-JJ-1.ovl'})[v];return {mode:f().rtss?'live':'offline'}}export async function readRtssZoom(){return 5}export async function detectCpuName(){return 'Fixture CPU'}",
 'src/bridge/monitorData.ts':"const f=()=>globalThis.__hudHostTest;export function acquireMonitorDemand(){f().demand++;let closed=false;return()=>{if(!closed){closed=true;f().demand--}}}export async function readMonitorSnapshot(){f().reads++;return f().snapshot}export function subscribeMonitorSnapshot(fn){fn(f().snapshot);return ()=>{}}export const subscribeMonitorHudSnapshot=subscribeMonitorSnapshot;export function subscribeMonitorTdpState(fn){fn(null);return ()=>{}}export function acceptPublishedMonitorSnapshot(){}export function isOwnedMonitorDisplaySnapshot(){return false}",
 'src/bridge/deckyMirrorExtensions.ts':"export function registerDeckyMirrorExtension(ext){globalThis.__hudHostTest.extension=ext;return()=>{globalThis.__hudHostTest.extension=null}}",
};
const args=process.argv.slice(2),startupOnly=args.includes('--startup-only'),withStartup=args.includes('--with-startup');
const selectionArgs=args.filter(v=>v.startsWith('--startup-case=')),selectedStartup=selectionArgs[0]?.slice('--startup-case='.length);
if(args.some(v=>v!=='--startup-only'&&v!=='--with-startup'&&!v.startsWith('--startup-case='))||new Set(args).size!==args.length||selectionArgs.length>1||startupOnly&&withStartup||selectionArgs.length&&(!selectedStartup||!startupOnly&&!withStartup)||withStartup&&!selectedStartup)throw Error('Unknown HUD fixture selection');
const visibilityNames=['visibility-terminal','visibility-frame-rejection','visibility-cadence','visibility-missing','visibility-hidden','visibility-transient','visibility-policy-hold','visibility-sleep','visibility-late-off','visibility-budget','visibility-healthy-reset','visibility-status-failure','visibility-foreign'];
const scopedVisibilityNames=['visibility-late-stop'];
const supervisionNames=['supervision-clock-expired-unowned','supervision-clock-expired','supervision-unavailable','supervision-aborted','supervision-exhaustion','supervision-late-off'];
const startupNames=[...visibilityNames,...supervisionNames,'read-retry','create-retry','unavailable-retry','exhaustion','off-cancels','output-switch','read-race-off','read-race-on','sleep-retry','native-terminal','stale-reconcile-off','healthy-cost','read-exhaustion','stop-pending','retry-native-pulse','late-create-off'];
const powerScopeNames=['power-scope-inflight','power-scope-retry-deadline','power-scope-exhausted','power-scope-completed-carrier','power-scope-stop'];
const pendingWakeClockNames=['power-wake-clock-pending-expired','power-wake-clock-pending-retained','power-wake-clock-pending-rejected','power-wake-clock-pending-stopped'];
const wakeClockNames=['power-wake-clock-lost','power-wake-clock-retained',...pendingWakeClockNames];
const startupSelection=selectedStartup?.split(',');
const bootReceiptNames=['boot-receipt-saved','boot-receipt-replaced','boot-monitor-replaced','boot-receipt-inflight-replaced'];
const selectableStartupNames=[...startupNames,...powerScopeNames,...scopedVisibilityNames,...wakeClockNames,...bootReceiptNames];
if(startupSelection&&(new Set(startupSelection).size!==startupSelection.length||startupSelection.some(name=>!selectableStartupNames.includes(name))))throw Error('Unknown HUD startup case');
const compiledInputs={};
const b=await build({metafile:true,stdin:{contents:"export {startMonitorHudHost} from './src/bridge/monitorHudHost.ts';export {normalizeMonitorOutput} from './src/bridge/monitorHudModel.ts';export {monitorOutputCoordinator,saveMonitorOutputPreferences} from './src/bridge/monitorOutput.ts';",resolveDir:root,loader:'ts'},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'inert-host-io',setup(build){build.onLoad({filter:/\.ts$/},args=>{const key=path.relative(root,args.path).replaceAll('\\','/');const sourcePath=key==='src/bridge/desktopHudOwner.ts'&&process.env.YMCC_HUD_DESKTOP_SOURCE||key==='src/bridge/monitorOutput.ts'&&process.env.YMCC_HUD_OUTPUT_SOURCE||key==='src/bridge/monitorOutputCoordinator.ts'&&process.env.YMCC_HUD_COORDINATOR_SOURCE||key==='src/bridge/monitorHudHost.ts'&&process.env.YMCC_HUD_HOST_SOURCE||key==='src/bridge/monitorHudClock.ts'&&process.env.YMCC_HUD_CLOCK_SOURCE||args.path;const contents=mocks[key]??fs.readFileSync(sourcePath,'utf8');compiledInputs[args.path]={kind:mocks[key]?'synthetic-port':'production',sourcePath:mocks[key]?undefined:sourcePath,sha256:crypto.createHash('sha256').update(contents).digest('hex')};return {contents,loader:'ts',resolveDir:path.dirname(args.path)}})}}]});
for(const input of Object.keys(b.metafile.inputs)){const resolved=path.resolve(root,input);if(!compiledInputs[resolved]&&fs.existsSync(resolved))compiledInputs[resolved]={kind:'production',sha256:crypto.createHash('sha256').update(fs.readFileSync(resolved)).digest('hex')};}fs.writeFileSync(path.join(out,'HOST-COMPILED-INPUTS.json'),JSON.stringify(compiledInputs,null,2));
const file=path.join(out,'host-under-test.mjs');fs.writeFileSync(file,b.outputFiles[0].text);const cases=[];let host;
const now=Date.now(),runId='a'.repeat(32),peer={connection:'1',runId,connected:true};
const game={appId:1091500,gameId:'1091500',browserGameId:'1091500',overlayTitle:'SP Overlay: 2/3/4',overlayPid:2,overlayBrowserId:3,ownerBrowserId:5,overlayBrowserType:4,overlayNativeAppId:1091500};
const f={settings:{rtss:{}},events:new Map(),calls:[],native:null,nextId:1,extension:null,rtss:true,layout:'YeManOBS-W-1.ovl',demand:0,reads:0,nativeFaults:0,steamShown:false,steamPresence:true,failSteamShow:false,snapshot:{top:{ts:now,cpuUsage:10,tempC:50,coreUsage:[10,20,30],hwDown:false},fps:{ts:now,fps:60,fps1:40,gpu:20},hwDown:false},emit(name,args){for(const fn of this.events.get(name)||[])fn(args)}};
f.settingsReads=0;f.settingsReadFailures=0;f.settingsReadGate=null;f.createFailures=0;f.nativeAvailable=true;f.power={known:true,phase:'ready',generation:1,hardwareWritesAllowed:true,inputReady:false,notificationPending:false,publicationRevision:1,observedNotificationSequence:1};
f.backendStarts=0;f.failRtssStart=false;f.rtssLoads=[];f.failRtssLoad=false;f.failNativeClose=false;f.delayCloseEvent=false;f.closedEvents=[];f.statusHolds=0;f.statusGate=null;
globalThis.__hudHostTest=f;globalThis.document=Object.assign(new EventTarget(),{visibilityState:'visible',createElement:()=>({content:{firstChild:null}})});
globalThis.window=Object.assign(new EventTarget(),{setTimeout,clearTimeout});
const request=status=>f.extension.request({...peer,request:{runId,command:'hud.status',status}});
f.invoke=async(command,args={})=>{
 f.calls.push([command,args]);
 if(command==='diagnostics.frontendTrace')return {ok:true};
 if(command==='power.lifecycle')return structuredClone(f.power);
 if(command==='desktopHud.status'){if(f.powerScopeHold&&host?.inspect().restoring){f.powerScopeHold=false;f.powerScopeEntered=true;await f.powerScopeGate;}if(f.statusHolds>0){--f.statusHolds;await f.statusGate;}if(f.statusFailures>0){--f.statusFailures;throw Error('synthetic visibility status failure')}return {available:f.nativeAvailable&&f.nativeFaults<3,id:f.native?.id??0,nonce:f.native?.nonce,rendererFailures:f.nativeFaults,recoveryBlocked:f.nativeFaults>=3,ready:!!f.native,armed:!!f.native,visible:!!f.native,layoutReady:true,controllerVisible:true,presentationAllowed:true,...f.visibilityStatus}};
 if(command==='desktopHud.create'){if(f.createFailures>0){--f.createFailures;throw Error('synthetic one-shot native creation failure')}assert(f.nativeFaults<3,'native process-lifetime recovery budget must not be bypassed');assert.equal(args.scope,'global');assert.equal(args.pid,undefined);const v={id:f.nextId++,nonce:args.nonce};f.native=v;queueMicrotask(()=>f.emit('desktopHud.ready',v));return v}
 if(command==='desktopHud.frame'){if(f.rejectMissingFrames&&!f.native)return false;queueMicrotask(()=>f.emit('desktopHud.painted',{id:args.hudId,nonce:args.nonce,seq:args.frame.seq}));return true}
 if(command==='desktopHud.close'){if(f.failNativeClose)throw Error('synthetic native close failure');const v=f.native;if(v){assert.equal(v.id,args.hudId);assert.equal(v.nonce,args.nonce);f.native=null;f.delayCloseEvent?f.closedEvents.push(v):queueMicrotask(()=>f.emit('desktopHud.closed',v))}return true}
 if(command==='monitor.hudPulse.set'){
  if(!f.pulseAdmission)return true;
  if((!args.enabled||args.update)&&args.nonce!==f.pulseNonce)return false;
  f.pulseNonce=args.enabled?args.nonce:null;return true;
 }
 if(command==='monitor.hudPulse.ack')return !f.pulseAdmission||f.pulseNonce===args.nonce;
 if(command==='deckySidebar.get')return {steamKnown:true,steamRunning:f.steamPresence};
 if(command==='steam.settings.get')return {ok:true,available:true,pending:false,via:'live',liveConfirmed:true,applicationConfirmed:true,position:0,detail:4,scale:1,saturation:1,opacity:.86};
 if(command==='steam.settings.set')return {ok:true,available:true,pending:false,via:'live',liveConfirmed:true,applicationConfirmed:true,...args.monitor};
 if(command==='deckySidebar.mirrorReply'){
  const frame=args.message;if(frame.type==='hud'){
   const shown=frame.show&&!f.failSteamShow;f.steamShown=shown;
   queueMicrotask(()=>f.extension?.request({...peer,request:{runId,command:'hud.ack',ok:!frame.show||!f.failSteamShow,status:{capable:true,game,shown,hiddenConfirmed:!shown,session:frame.session,operation:frame.operation,reason:shown||!frame.show?'':'synthetic create failure'}}}));
  }
  return true;
 }
 throw Error('Unexpected test IO '+command);
};
const m=await import('file:///'+file.replaceAll('\\','/')),flush=async(n=12)=>{for(let i=0;i<n;i++)await new Promise(r=>setImmediate(r))},check=(name,fn)=>{fn();cases.push(name)};
f.settings.rtss.monitorOutput=m.normalizeMonitorOutput();
try{
 const runStartup=async()=>{
 const startupResults=[];
 for(const name of (startupSelection?selectableStartupNames.filter(name=>startupSelection.includes(name)):startupNames)){
  console.error('STARTUP_CASE_BEGIN '+name);
  const originalNow=Date.now,originalSet=window.setTimeout,originalClear=window.clearTimeout;
  let currentNow=now,nextTimer=0;const timers=new Map();
  Date.now=()=>currentNow;
  window.setTimeout=(fn,ms)=>{const id=++nextTimer;timers.set(id,{fn,at:currentNow+ms,ms});return id};
  window.clearTimeout=id=>timers.delete(id);
  f.native=null;f.nativeFaults=0;f.nativeAvailable=true;f.createFailures=0;f.settingsReads=0;f.settingsReadFailures=0;f.settingsReadGate=null;
  f.rejectMissingFrames=false;f.visibilityStatus=undefined;f.statusFailures=0;f.statusHolds=0;f.statusGate=null;f.delayCloseEvent=false;f.failNativeClose=false;f.failSteamShow=false;f.failRtssLoad=false;f.failRtssStart=false;f.rtss=true;
  f.settings.rtss.monitorOutput=m.normalizeMonitorOutput({enabled:true,output:'ymcc',ymccCarrier:'desktop'});
  f.power={known:true,phase:'ready',generation:1,hardwareWritesAllowed:true,inputReady:false,notificationPending:false,publicationRevision:1,observedNotificationSequence:1};
  const callStart=f.calls.length,readsStart=f.reads;
  const creates=()=>f.calls.slice(callStart).filter(([cmd])=>cmd==='desktopHud.create').length;
  const advance=async(ms)=>{currentNow+=ms;for(const [id,t]of [...timers])if(t.at<=currentNow&&timers.delete(id))t.fn();await flush(48)};
  const start=async()=>{host=m.startMonitorHudHost();await host.ready();await flush(48)};
  let failure=null;const scopeObservations=[];
  const originalApply=m.monitorOutputCoordinator.apply;const powerScopeCurrents=[];
  if(powerScopeNames.includes(name))m.monitorOutputCoordinator.apply=(...args)=>{if(args[2])powerScopeCurrents.push(args[2]);return originalApply(...args);};
  try{
   if(wakeClockNames.includes(name)){
    f.pulseAdmission=true;f.pulseNonce=null;
    try{
     await start();assert(f.native);const first=f.native,oldNonce=host.inspect().clock.nonce;
     assert(oldNonce);assert.equal(f.pulseNonce,oldNonce);assert.equal(f.demand,1);
     const ackCount=()=>f.calls.slice(callStart).filter(([cmd,args])=>cmd==='monitor.hudPulse.ack'&&args.nonce===oldNonce).length;
     const priorAck=ackCount();
     if(pendingWakeClockNames.includes(name)){
      const invoke=f.invoke;let completeOld,completeWake;
      const oldReceipt=new Promise(resolve=>{completeOld=resolve;});
      const wakeGate=new Promise(resolve=>{completeWake=resolve;});
      let renewals=0;const ackSignals=[];
      f.invoke=async(command,args,options)=>{
       if(command==='monitor.hudPulse.ack'&&args.nonce===oldNonce){
        ackSignals.push(options?.signal);
        const index=++renewals,accepted=await invoke(command,args);
        if(index===1){assert(accepted,'first request was accepted by Native');return oldReceipt;}
        if(index===2){await wakeGate;return accepted;}
       }
       return invoke(command,args);
      };
      try{
       currentNow+=1000;f.emit('monitor.hudPulse',{nonce:oldNonce,snapshot:f.snapshot});await flush(48);
       assert.equal(ackCount(),priorAck+1,'exactly one old renewal remains in flight');
       f.power={...f.power,phase:'resuming',generation:2,publicationRevision:2,observedNotificationSequence:2,hardwareWritesAllowed:false};
       f.emit('power.lifecycleChanged',f.power);await flush(48);
       const expired=name==='power-wake-clock-pending-expired';
       // Browser timers do not run while hidden. Model Native expiring the
       // lease and the expiry event being lost, independently of the ACK.
       currentNow+=expired?8000:500;if(expired)f.pulseNonce=null;
       f.power={...f.power,phase:'ready',publicationRevision:3,hardwareWritesAllowed:true};
       f.emit('power.lifecycleChanged',f.power);await flush(96);
       const stopped=name==='power-wake-clock-pending-stopped',rejected=name==='power-wake-clock-pending-rejected';
       assert.equal(ackCount(),priorAck+2,'Ready must issue one fresh confirmation while the old ACK is pending');
       assert.equal(timers.size,1,'only the current confirmation has a deadline');
       assert.equal(ackSignals[0]?.aborted,true,'retired IPC is canceled instead of left outstanding');
       assert.equal(ackSignals[1]?.aborted,false,'the new IPC still owns the confirmation');
       if(stopped){host.stop();await flush(48);}
       completeOld(!rejected);await flush(96);
       if(!stopped){
        assert.equal(creates(),1,'the old ACK may not close or replace the carrier');
        assert.equal(timers.size,1,'the old ACK may not clear the current wake confirmation');
       }
       completeWake();await flush(96);
       if(stopped){
        assert.equal(ackCount(),priorAck+2,'stopped host must not renew from either late positive ACK');
        assert.equal(ackSignals[1]?.aborted,true,'stop cancels the current IPC');
        assert.equal(host.inspect().clock.users,0);assert.equal(f.pulseNonce,null);assert.equal(creates(),1);
       }else if(expired){
        assert.equal(creates(),2,'resume must restore HUD without a new old-clock pulse or a browser timeout');
        assert(f.native&&f.native.id!==first.id);assert.notEqual(host.inspect().clock.nonce,oldNonce);
        assert.equal(ackCount(),priorAck+2,'do not retry a dead nonce after the fresh confirmation rejects it');
        assert.equal(f.demand,1);
       }else{
        assert.equal(ackCount(),priorAck+2,'one fresh confirmation must follow the pre-sleep ACK');
        assert.equal(creates(),1);assert.equal(f.native,first);assert.equal(f.pulseNonce,oldNonce);assert.equal(f.demand,1);
       }
       const settledAcks=ackCount();currentNow+=60000;
       f.emit('power.lifecycleChanged',{...f.power,publicationRevision:4});await flush(48);
       assert.equal(ackCount(),settledAcks,'no renewal loop or extra confirmation from a publication-only update');
       assert.equal(timers.size,0,'wake confirmation leaves no timer behind');
      }finally{completeOld(false);completeWake();f.invoke=invoke;}
     }else{
     f.power={...f.power,phase:'resuming',generation:2,publicationRevision:2,observedNotificationSequence:2,hardwareWritesAllowed:false};
     f.emit('power.lifecycleChanged',f.power);await flush(48);
     assert.equal(ackCount(),priorAck,'no wake confirmation while shared power holds the carrier');
     if(name==='power-wake-clock-lost')f.pulseNonce=null; // Native expiry receipt and all subsequent old pulses are missing.
     f.power={...f.power,phase:'ready',publicationRevision:3,hardwareWritesAllowed:true};
     f.emit('power.lifecycleChanged',f.power);await flush(96);
     if(name==='power-wake-clock-lost'){
      assert.equal(creates(),2,'public Ready recovers an expired clock even when no expiry notification arrives');
      assert(f.native&&f.native.id!==first.id);assert.notEqual(host.inspect().clock.nonce,oldNonce);
      f.emit('monitor.hudPulse',{nonce:oldNonce,snapshot:null,expired:true});await flush(48);
      assert.equal(creates(),2,'late expiry cannot affect the replacement carrier');
     }else{
      assert.equal(creates(),1,'existing carrier survives a positive wake renewal');
      assert.equal(f.native,first);assert.equal(host.inspect().clock.nonce,oldNonce);
     }
     assert.equal(ackCount(),priorAck+1,'exactly one original nonce confirmation per wake');
     assert.equal(f.demand,1);assert.equal(f.pulseNonce,host.inspect().clock.nonce);
     f.emit('power.lifecycleChanged',{...f.power,publicationRevision:4});await flush(48);
     assert.equal(ackCount(),priorAck+1,'publication-only observations do not renew the clock');
     }
    }finally{f.pulseAdmission=false;f.pulseNonce=null;}
   }else if(supervisionNames.includes(name)){
    if(name==='supervision-unavailable')f.nativeAvailable=false;
    if(name==='supervision-exhaustion')f.createFailures=99;
    if(name==='supervision-aborted'||name==='supervision-late-off'||name==='supervision-clock-expired-unowned'){
     let abort=true;m.monitorOutputCoordinator.apply=(...args)=>{if(abort){abort=false;return Promise.reject(Error('MONITOR_OUTPUT_REQUEST_EXPIRED'))}return originalApply(...args)};
    }
    await start();
    if(name==='supervision-clock-expired-unowned'){
     const oldNonce=host.inspect().clock.nonce;assert(oldNonce);assert.equal(f.native,null);
     f.emit('monitor.hudPulse',{nonce:oldNonce,snapshot:null,expired:true});await flush(96);
     assert.equal(creates(),0,'no immediate spin/recreate on a failed supervision clock');
     await advance(3000);assert.equal(creates(),1);assert(f.native);assert.notEqual(host.inspect().clock.nonce,oldNonce);
    }else if(name==='supervision-clock-expired'){
     const oldNonce=host.inspect().clock.nonce,oldId=f.native.id;f.native=null;
     f.emit('monitor.hudPulse',{nonce:oldNonce,snapshot:null,expired:true});await flush(96);
     assert.equal(creates(),2,'native lease expiry restarts the current enabled monitor without a browser timer');
     assert(f.native&&f.native.id!==oldId);assert.notEqual(host.inspect().clock.nonce,oldNonce);
     f.emit('monitor.hudPulse',{nonce:oldNonce,snapshot:null,expired:true});await flush(48);assert.equal(creates(),2,'obsolete expiry cannot kill the replacement clock');
    }else{
    if(name==='supervision-unavailable'||name==='supervision-exhaustion'){await advance(1000);await advance(3000);}
    assert.equal(f.native,null,'synthetic startup leaves no descriptor');assert.equal(f.demand,0);
    const nonce=host.inspect().clock.nonce;assert(nonce,'enabled monitor retains a sensor-free native supervision lease without a window');
    const statuses=()=>f.calls.slice(callStart).filter(([cmd])=>cmd==='desktopHud.status').length;
    const pulse=async ms=>{await advance(ms);f.emit('monitor.hudPulse',{nonce:host.inspect().clock.nonce,snapshot:f.snapshot});await flush(96)};
    if(name==='supervision-late-off'){
     let release;f.statusGate=new Promise(resolve=>{release=resolve});f.statusHolds=1;await pulse(10000);
     const off=m.normalizeMonitorOutput({...f.settings.rtss.monitorOutput,enabled:false});await m.saveMonitorOutputPreferences(off);await m.monitorOutputCoordinator.apply(off);release();await flush(96);
     assert.equal(creates(),0);assert.equal(f.native,null);assert.equal(host.inspect().clock.users,0);
    }else{
     const base=statuses();await pulse(5999);assert.equal(statuses(),base,'no fast status loop during missing-owner supervision');
     if(name==='supervision-unavailable')f.nativeAvailable=true;
     await pulse(4001);await pulse(10000);
     if(name==='supervision-exhaustion'){assert.equal(creates(),3,'read-only supervision never refunds failed creation attempts');assert.equal(f.native,null);assert.equal(host.inspect().recovery.restoreAttempts,3);}
     else{assert.equal(creates(),1,'current enabled intent recovers without a manual toggle');assert(f.native);assert.equal(f.demand,1);}
    }
    }
   }else if(visibilityNames.includes(name)||scopedVisibilityNames.includes(name)){
    await start();assert(f.native);const first=f.native;
    const statuses=()=>f.calls.slice(callStart).filter(([cmd])=>cmd==='desktopHud.status').length;
    const pulse=async ms=>{await advance(ms);const nonce=host.inspect().clock.nonce;assert(nonce,'existing HUD clock remains owned');f.emit('monitor.hudPulse',{nonce,snapshot:f.snapshot});await flush(96)};
    const base=statuses(),reads=f.reads;
    if(name==='visibility-terminal'){
     f.nativeFaults=3;await pulse(10000);assert.equal(statuses(),base+1);await pulse(10000);await pulse(10000);
     assert.equal(statuses(),base+1,'native terminal failure must also stop visibility probing');assert.equal(creates(),1);assert.equal(host.inspect().desktop.visibilityHealth.repairs,0);
    }else if(name==='visibility-frame-rejection'){
     // Production native frame() rejects a vanished descriptor. A successful
     // close(id, nonce) when already absent sends no second closed event.
     f.rejectMissingFrames=true;f.native=null;await pulse(1000);
     assert.equal(creates(),2,'confirmed disappearance without a closed event must retire the successful host key');assert.equal(f.demand,1);assert.equal(host.inspect().desktop.visibilityHealth.repairs,0);
    }else if(name==='visibility-cadence'){

     await pulse(9999);assert.equal(statuses(),base);await pulse(1);assert.equal(statuses(),base+1);
     for(let i=0;i<9;i++)await pulse(1000);assert.equal(statuses(),base+1);await pulse(1000);assert.equal(statuses(),base+2);
     assert.equal(creates(),1);assert.equal(f.reads,reads,'visibility must not read sensors');assert.equal(timers.size,0,'no visibility timer');
    }else if(name==='visibility-missing'){
     f.native=null;await pulse(10000);assert.equal(creates(),1);await pulse(10000);
     assert.equal(creates(),2);assert(f.native&&f.native.id!==first.id);assert.equal(f.demand,1);
    }else if(name==='visibility-hidden'){
     f.visibilityStatus={visible:false};await pulse(10000);assert.equal(creates(),1);await pulse(10000);
     assert.equal(creates(),2);assert.equal(f.demand,1);assert.equal(host.inspect().desktop.visibilityHealth.repairs,1);
    }else if(name==='visibility-transient'){
     f.visibilityStatus={visible:false};await pulse(10000);f.visibilityStatus=undefined;await pulse(10000);await pulse(10000);
     assert.equal(creates(),1);assert.equal(host.inspect().desktop.visibilityHealth.failures,0);
    }else if(name==='visibility-policy-hold'){
     for(const reason of ['waiting-game','waiting-game-focus','radial-suspended']){f.visibilityStatus={visible:false,presentationAllowed:false,reason};await pulse(10000);await pulse(10000);}
     assert.equal(creates(),1);assert.equal(host.inspect().desktop.visibilityHealth.repairs,0);
    }else if(name==='visibility-sleep'){
     f.power={...f.power,phase:'resuming',hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2};f.emit('power.lifecycleChanged',f.power);await flush(48);
     const held=statuses();f.visibilityStatus={visible:false};await pulse(10000);await pulse(10000);
     assert.equal(statuses(),held,'shared sleep denies visibility probes');assert.equal(creates(),1);
    }else if(name==='visibility-late-off'){
     f.native=null;await pulse(10000);let release;f.statusGate=new Promise(resolve=>{release=resolve});f.statusHolds=1;
     await pulse(10000);assert.equal(host.inspect().desktop.visibilityHealth.checking,true);
     const disabled=m.normalizeMonitorOutput({...f.settings.rtss.monitorOutput,enabled:false});await m.saveMonitorOutputPreferences(disabled);
     const result=await m.monitorOutputCoordinator.apply(disabled);assert(result.ok);release();await flush(96);
     assert.equal(f.native,null);assert.equal(creates(),1);assert.equal(f.demand,0);
    }else if(name==='visibility-late-stop'){
     const nonce=host.inspect().clock.nonce;f.visibilityStatus={visible:false};await pulse(10000);
     let release;f.statusGate=new Promise(resolve=>{release=resolve});f.statusHolds=1;
     try{
      await pulse(10000);assert.equal(host.inspect().desktop.visibilityHealth.checking,true);assert.deepEqual(f.native,first);
      host.stop();await flush(48);
      assert.equal(f.native,null,'stop closes the live owned HUD without waiting for a visibility reply');assert.equal(f.demand,0);assert.equal(host.inspect().clock.users,0);
      release();f.emit('monitor.hudPulse',{nonce,snapshot:null,expired:true});await flush(96);
      assert.equal(creates(),1,'a late visibility reply or expired pulse must not recreate after stop');assert.equal(f.native,null);assert.equal(f.demand,0);assert.equal(timers.size,0);
     }finally{release();}
    }else if(name==='visibility-budget'){
     for(let i=0;i<3;i++){f.native=null;await pulse(10000);await pulse(10000);assert.equal(creates(),i+2);}
     const count=statuses();f.native=null;await pulse(10000);await pulse(10000);
     assert.equal(creates(),4);assert.equal(statuses(),count+2,'exhaustion keeps the same slow read-only check, never repeated creation');assert.equal(host.inspect().desktop.visibilityHealth.repairs,3);
    }else if(name==='visibility-healthy-reset'){
     for(let i=0;i<4;i++){f.native=null;await pulse(10000);await pulse(10000);assert.equal(creates(),i+2);await pulse(10000);assert.equal(host.inspect().desktop.visibilityHealth.repairs,0,'confirmed healthy visibility ends an incident, not the native renderer budget');}
    }else if(name==='visibility-status-failure'){
     f.statusFailures=2;await pulse(10000);await pulse(10000);assert.equal(creates(),1);assert.equal(host.inspect().desktop.visibilityHealth.repairs,0);
    }else if(name==='visibility-foreign'){
     f.visibilityStatus={id:first.id+99,nonce:'foreign-descriptor-token',visible:false};await pulse(10000);await pulse(10000);
     assert.equal(creates(),1);assert(f.native);assert.equal(f.calls.slice(callStart).filter(([cmd])=>cmd==='desktopHud.close').length,0);
    }
   }else if(name==='power-scope-inflight'||name==='power-scope-stop'){

    let release;f.powerScopeGate=new Promise(resolve=>{release=resolve});f.powerScopeHold=true;f.powerScopeEntered=false;
    try{
     await start();assert(f.powerScopeEntered,'hold occurs inside actual recovery apply');assert.equal(powerScopeCurrents.length,1);const oldCurrent=powerScopeCurrents[0];
     assert.equal(await oldCurrent(),true);const charged=host.inspect().recovery.restoreAttempts;assert.equal(charged,1);
     if(name==='power-scope-stop')host.stop();
     f.power={...f.power,publicationRevision:2,observedNotificationSequence:2};f.emit('power.lifecycleChanged',f.power);await flush(24);
     const oldStillCurrent=await oldCurrent();scopeObservations.push({stage:'new-ready-scope-before-old-return',oldStillCurrent,chargedBefore:charged,chargedAfter:host.inspect().recovery.restoreAttempts});
     assert.equal(oldStillCurrent,false,'same-generation notification retires the actual old HUD request');assert.equal(host.inspect().recovery.restoreAttempts,charged,'scope retirement cannot refund an existing attempt');
     release();release=null;await flush(96);
     if(name==='power-scope-stop'){assert.equal(creates(),0);assert.equal(f.native,null);assert.equal(f.demand,0);}
     else{const due=host.inspect().recovery.retryAt;assert.equal(due,currentNow+1000,'original obsolete-attempt settlement keeps its existing first retry delay');await advance(due-currentNow);assert.equal(creates(),1);assert(f.native);assert(powerScopeCurrents.length>=2);assert.equal(await powerScopeCurrents.at(-1)(),true);assert.equal(f.demand,1);}
    }finally{release?.();f.powerScopeHold=false;await flush(48);}
   }else if(name==='power-scope-retry-deadline'){
    f.createFailures=1;await start();const oldRetryAt=host.inspect().recovery.retryAt,charged=host.inspect().recovery.restoreAttempts;assert.equal(creates(),1);assert.equal(charged,1);assert.equal(oldRetryAt,currentNow+1000);
    f.power={...f.power,publicationRevision:2,observedNotificationSequence:2};f.emit('power.lifecycleChanged',f.power);await flush(48);
    scopeObservations.push({stage:'same-generation-existing-delay',oldRetryAt,newRetryAt:host.inspect().recovery.retryAt,chargedBefore:charged,chargedAfter:host.inspect().recovery.restoreAttempts});
    assert.equal(host.inspect().recovery.retryAt,oldRetryAt,'same Ready scope advance retains the original retry deadline');assert.equal(host.inspect().recovery.restoreAttempts,charged);assert.equal(creates(),1);
    await advance(999);assert.equal(creates(),1);await advance(1);assert.equal(creates(),2);assert(f.native);assert.equal(timers.size,0);
   }else if(name==='power-scope-exhausted'){
    f.createFailures=99;await start();await advance(1000);await advance(3000);assert.equal(creates(),3);assert.equal(host.inspect().recovery.restoreAttempts,3);
    for(const value of [{phase:'ready',hardwareWritesAllowed:true,publicationRevision:2,observedNotificationSequence:2},{phase:'resuming',hardwareWritesAllowed:false,publicationRevision:3,observedNotificationSequence:3},{phase:'ready',hardwareWritesAllowed:true,publicationRevision:4,observedNotificationSequence:4}]){f.power={...f.power,...value};f.emit('power.lifecycleChanged',f.power);await flush(48);}
    await advance(60000);scopeObservations.push({stage:'same-generation-no-refund',creates:creates(),charged:host.inspect().recovery.restoreAttempts});assert.equal(creates(),3);assert.equal(host.inspect().recovery.restoreAttempts,3);assert.equal(timers.size,0);assert.equal(f.native,null);
   }else if(name==='power-scope-completed-carrier'){
    await start();assert(f.native);const carrier={...f.native},priorApplies=powerScopeCurrents.length,oldCurrent=powerScopeCurrents.at(-1);assert.equal(await oldCurrent(),true);
    f.power={...f.power,publicationRevision:2,observedNotificationSequence:2};f.emit('power.lifecycleChanged',f.power);await flush(96);
    scopeObservations.push({stage:'retained-carrier-new-scope',oldCurrent:await oldCurrent(),priorApplies,newApplies:powerScopeCurrents.length,creates:creates()});
    assert.equal(await oldCurrent(),false);assert.equal(powerScopeCurrents.length,priorApplies,'completed painted carrier needs no new apply');assert.deepEqual(f.native,carrier);assert.equal(creates(),1);assert.equal(f.demand,1);
    const applies=powerScopeCurrents.length,calls=f.calls.length;for(let i=3;i<1003;i++){f.power={...f.power,publicationRevision:i};f.emit('power.lifecycleChanged',f.power);}await flush(48);
    assert.equal(powerScopeCurrents.length,applies,'publication-only observations do not reapply HUD');assert.equal(f.calls.length,calls);assert.equal(creates(),1);
   }else if(name==='read-retry'){
    f.settingsReadFailures=1;await start();assert.equal(f.settingsReads,1);assert.equal(f.native,null);
    await advance(1000);assert.equal(f.settingsReads,2);assert(f.native);assert.equal(f.demand,1);assert.equal(timers.size,0);
   }else if(name==='create-retry'){
    f.createFailures=1;await start();assert.equal(creates(),1);assert.equal(f.native,null);
    for(let i=0;i<1000;i++)f.emit('deckySidebar.updated',{steamKnown:true,steamRunning:false});await flush(48);
    assert.equal(creates(),1);await advance(1000);assert.equal(creates(),2);assert(f.native);assert.equal(timers.size,0);
   }else if(name==='unavailable-retry'){
    f.nativeAvailable=false;await start();assert.equal(creates(),0);assert.equal(f.demand,0);
    f.nativeAvailable=true;await advance(1000);assert.equal(creates(),1);assert(f.native);
   }else if(bootReceiptNames.includes(name)){
    const boot='windows-volatile-v2|{16605190-1234-4000-8000-222222222222}', mode='xbox-steam', optional=f.settings;
    const desired=m.normalizeMonitorOutput({enabled:true,output:'ymcc',ymccCarrier:'desktop'});
    f.settings={rtss:{monitorOutput:{...desired,enabled:false}},input:{revision:2,startupObservedBoot:boot},extensions:{
     bootModes:{schema:1,revision:5,boot:mode,bootEnabled:true,current:mode},bootModesAppliedBoot:boot,bootModesClaimWriteId:'fixture-claim',
     bootModesStartupResult:{schema:1,boot,mode,sourceRevision:4,revision:4,status:'partial',applied:[],pending:['claim'],skipped:[]},
    }};
    let release;
    try{
     f.nativeAvailable=false;await start();
     const write={origin:'boot-mode',bootContext:structuredClone({input:f.settings.input,extensions:f.settings.extensions})};
     await m.saveMonitorOutputPreferences(desired,write);
     const initial=await m.monitorOutputCoordinator.apply(desired,write);await flush(48);
     assert(!initial.ok);assert.equal(creates(),0);assert(host.inspect().recovery.retryAt,'the existing recovery must be pending');
     f.settings.extensions.bootModesActiveBoot=boot;
     f.settings.extensions.bootModesStartupResult={...f.settings.extensions.bootModesStartupResult,revision:5,
      pending:['monitor'],modules:{monitor:{state:'waiting',requested:structuredClone(desired)}}};
     if(name==='boot-receipt-replaced')f.settings.extensions.bootModesClaimWriteId='replacement-claim';
     if(name==='boot-monitor-replaced')f.settings.rtss.monitorOutput={...desired,enabled:false};
     if(name==='boot-receipt-inflight-replaced'){
      f.powerScopeGate=new Promise(resolve=>{release=resolve;});f.powerScopeHold=true;f.powerScopeEntered=false;
     }
     f.nativeAvailable=true;await advance(1000);
     if(name==='boot-receipt-inflight-replaced'){
      assert(f.powerScopeEntered,'the matching recovery must reach held carrier IO before replacement');
      f.settings.extensions.bootModesClaimWriteId='replacement-during-io';release();await flush(48);
     }
     if(name==='boot-receipt-saved'){
      assert.equal(creates(),1,'the matching HUD recovery was discarded after its own startup receipt was saved');assert(f.native);
     }else{
      assert.equal(creates(),0,'a retired boot monitor request reached the native carrier');assert.equal(f.native,null);
     }
    }finally{release?.();f.powerScopeHold=false;await flush(48);f.settings=optional;}
   }else if(name==='exhaustion'){
    f.createFailures=99;await start();await advance(1000);await advance(3000);
    assert.equal(creates(),3);assert.equal(f.native,null);assert.equal(f.demand,0);assert.equal(timers.size,0);assert.equal(host.inspect().clock.users,1,'only the sensor-free supervision lease remains');assert.equal(host.inspect().recovery.visibilityLease,true);
    f.emit('deckySidebar.updated',{steamKnown:true,steamRunning:false});await flush(48);
    const calls=f.calls.length;for(let i=0;i<1000;i++)f.emit('deckySidebar.updated',{steamKnown:true,steamRunning:false});await flush(48);await advance(60000);
    assert.equal(creates(),3);assert.equal(f.calls.length,calls);assert.equal(f.reads,readsStart);
   }else if(name==='off-cancels'||name==='output-switch'){
    f.createFailures=1;await start();const next=m.normalizeMonitorOutput({...f.settings.rtss.monitorOutput,...(name==='off-cancels'?{enabled:false}:{output:'rtss'})});
    await m.saveMonitorOutputPreferences(next);const result=await m.monitorOutputCoordinator.apply(next);await flush(48);assert(result.ok);
    await advance(30000);assert.equal(creates(),1);assert.equal(f.native,null);assert.equal(f.demand,0);assert.equal(timers.size,0);
   }else if(name==='read-race-off'||name==='read-race-on'){
    let release;f.settingsReadGate=new Promise(resolve=>{release=resolve});
    f.settings.rtss.monitorOutput.enabled=name==='read-race-off';host=m.startMonitorHudHost();const pending=host.ready();await flush(12);
    const next=m.normalizeMonitorOutput({...f.settings.rtss.monitorOutput,enabled:name==='read-race-on'});
    await m.saveMonitorOutputPreferences(next);const result=await m.monitorOutputCoordinator.apply(next);await flush(24);
    if(name==='read-race-off')assert(result.ok);else assert(!result.ok&&result.phase==='preflight');
    release();await pending;await flush(48);assert.equal(f.settings.rtss.monitorOutput.enabled,next.enabled);
    assert.equal(!!f.native,next.enabled);assert.equal(creates(),next.enabled?1:0);assert.equal(timers.size,0);
   }else if(name==='sleep-retry'){
    f.createFailures=1;await start();f.power={...f.power,phase:'resuming',generation:2,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2};
    f.emit('power.lifecycleChanged',f.power);await flush(24);await advance(10000);assert.equal(creates(),1);assert.equal(f.demand,0);assert.equal(timers.size,0);
    f.power={...f.power,phase:'ready',hardwareWritesAllowed:true,publicationRevision:3};f.emit('power.lifecycleChanged',f.power);await flush(48);
    assert.equal(creates(),2);assert(f.native);assert.equal(timers.size,0);assert.equal(f.power.inputReady,false);
   }else if(name==='native-terminal'){
    await start();const old=f.native;f.native=null;f.nativeFaults=3;f.emit('desktopHud.closed',old);await flush(48);
    assert.equal(creates(),1);assert.equal(f.demand,0);assert.equal(timers.size,0);const calls=f.calls.length;await advance(60000);assert.equal(f.calls.length,calls);
   }else if(name==='stale-reconcile-off'){
    await start();const old=f.native;f.native=null;let release;f.statusGate=new Promise(resolve=>{release=resolve});f.statusHolds=2;
    f.emit('desktopHud.closed',old);await flush(12);assert.equal(f.statusHolds,0);
    const next=m.normalizeMonitorOutput({...f.settings.rtss.monitorOutput,enabled:false});await m.saveMonitorOutputPreferences(next);
    const off=await m.monitorOutputCoordinator.apply(next);assert(off.ok);release();await flush(48);
    assert.equal(f.native,null);assert.equal(creates(),1);assert.equal(f.demand,0);assert.equal(timers.size,0);
   }else if(name==='healthy-cost'){
    await start();assert(f.native);const count=f.calls.length,reads=f.reads;
    assert.equal(timers.size,0);await advance(60000);assert.equal(f.calls.length,count);assert.equal(f.reads,reads);assert.equal(creates(),1);assert.equal(f.demand,1);
   }else if(name==='read-exhaustion'){
    f.settingsReadFailures=99;await start();await advance(1000);await advance(3000);assert.equal(f.settingsReads,3);assert.equal(f.native,null);assert.equal(timers.size,0);assert.equal(host.inspect().clock.users,0);
    const calls=f.calls.length;await advance(60000);assert.equal(f.calls.length,calls);assert.equal(f.settingsReads,3);
   }else if(name==='stop-pending'){
    let release;f.settingsReadGate=new Promise(resolve=>{release=resolve});host=m.startMonitorHudHost();const pending=host.ready();await flush(12);host.stop();await flush(24);release();await pending;await flush(48);
    assert.equal(creates(),0);assert.equal(f.native,null);assert.equal(f.demand,0);assert.equal(timers.size,0);
   }else if(name==='retry-native-pulse'){
    f.createFailures=1;await start();const nonce=host.inspect().clock.nonce;assert(nonce);
    currentNow+=1000;f.emit('monitor.hudPulse',{nonce,snapshot:null});await flush(48);
    assert.equal(creates(),2);assert(f.native);assert.equal(timers.size,0); // Browser timeouts never fired.
   }else if(name==='late-create-off'){
    const invoke=f.invoke;let release,late;
    const gate=new Promise(resolve=>{release=resolve});
    f.invoke=async(cmd,args)=>{if(cmd==='desktopHud.create'){late=await invoke(cmd,args);await gate;return late}return invoke(cmd,args)};
    host=m.startMonitorHudHost();await host.ready();await flush(24);assert(late);
    const disabled=m.normalizeMonitorOutput({...f.settings.rtss.monitorOutput,enabled:false});await m.saveMonitorOutputPreferences(disabled);
    const off=m.monitorOutputCoordinator.apply(disabled);await flush(12);release();const result=await off;await flush(48);
    assert(result.ok);assert.equal(f.native,null);assert.equal(creates(),1);assert.equal(f.demand,0);assert.equal(timers.size,0);f.invoke=invoke;
   }
  }catch(error){failure=String(error);}
  const state=host?.inspect();
  try{host?.stop();await flush(48);assert.equal(f.demand,0);assert.equal(f.native,null);assert.equal(timers.size,0);assert([...f.events.values()].every(s=>s.size===0));}catch(error){failure??=String(error);}
  finally{m.monitorOutputCoordinator.apply=originalApply;host=null;Date.now=originalNow;window.setTimeout=originalSet;window.clearTimeout=originalClear;}
  startupResults.push({name,ok:failure===null,error:failure,creates:creates(),settingsReads:f.settingsReads,sourceReads:f.reads-readsStart,host:state,...(powerScopeNames.includes(name)?{scopeObservations}:{} )});
  console.error('STARTUP_CASE_END '+name+' '+(failure===null?'PASS':failure));
 }
 const report={ok:startupResults.every(r=>r.ok),selected:startupResults.map(r=>r.name),cases:startupResults,actualAppChanged:false,
  scope:'Current production HUD host/coordinator/router/desktop owner/clock and shared-power source; inert settings/native leaf ports, controlled finite timers; no native pixels, installed runtime or CPU percentage claim.'};
 fs.writeFileSync(path.join(out,'HOST-STARTUP-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));if(!report.ok)process.exitCode=2;
 };
 if(!startupOnly){
 host=m.startMonitorHudHost();await host.ready();await flush();
 check('Actual host auto-restores saved global desktop without game or Steam availability checks',()=>{assert.equal(host.inspect().carrier,'desktop');assert(f.native);assert.equal(f.demand,1);assert.equal(f.reads,1);assert(!f.calls.some(([cmd])=>cmd.startsWith('steam.settings')||cmd==='deckySidebar.get'))});
 const createCount=()=>f.calls.filter(([cmd])=>cmd==='desktopHud.create').length;
 f.extension.connected(peer);request({capable:true,game,shown:false,hiddenConfirmed:true,session:null,operation:0,reason:''});await flush();
 check('Loading an available formal plugin cannot change default desktop output or recreate its window',()=>{assert.equal(host.inspect().carrier,'desktop');assert.equal(createCount(),1);assert(!f.steamShown)});
 const preferred=m.normalizeMonitorOutput({ymccCarrier:'steam-preferred'});await m.saveMonitorOutputPreferences(preferred);await flush();
 check('Carrier choice is a draft; preference persistence itself cannot migrate live output',()=>{assert.equal(host.inspect().carrier,'desktop');assert(!f.steamShown)});
 let result=await m.monitorOutputCoordinator.apply(preferred);await flush(24);
 check('Explicit Apply uses formal Steam relay and first-paint ACK through actual host; no stale default intent is queued',()=>{assert(result.ok);assert.equal(host.inspect().carrier,'steam');assert(f.steamShown);assert.equal(f.native,null);assert.equal(f.settings.rtss.monitorOutput.ymccCarrier,'steam-preferred');assert.equal(createCount(),1);assert.equal(f.demand,1);assert.equal(f.reads,2)});
 f.steamShown=false;f.steamPresence=false;f.emit('deckySidebar.updated',{steamKnown:true,steamRunning:false});await flush(24);
 check('Existing confirmed Steam-exit event restores global desktop without process polling, game binding or fallback fixtures',()=>{assert.equal(host.inspect().carrier,'desktop');assert(f.native);assert(!f.steamShown);assert.equal(f.demand,1);assert.equal(f.reads,3);assert.equal(createCount(),2)});
 const old=f.native;f.native=null;f.emit('desktopHud.closed',old);await flush(24);
 check('Unexpected native close triggers one bounded restore, while close events from migrations cannot produce a reopen loop',()=>{assert.equal(host.inspect().carrier,'desktop');assert(f.native);assert.equal(createCount(),3)});await flush(24);assert.equal(createCount(),3);
 f.steamPresence=true;f.emit('deckySidebar.updated',{steamKnown:true,steamRunning:true});f.extension.connected({...peer,connection:'2'});
 // Reconnect changes authenticated peer identity; subsequent replies use that exact peer.
 // Restore original peer for this fixture without admitting stale status from connection 2.
 f.extension.connected(peer);request({capable:true,game,shown:false,hiddenConfirmed:true,session:null,operation:0,reason:''});await flush(24);
 f.failSteamShow=true;result=await m.monitorOutputCoordinator.apply(preferred);await flush(24);
 check('Formal create failure is reclaimed and shown to the user as confirmed desktop recovery, not false Steam success',()=>{assert(result.ok);assert(result.reason.includes('恢复全局桌面'));assert.equal(host.inspect().carrier,'desktop');assert(f.native);assert(!f.steamShown);assert.equal(f.demand,1)});
 const off=m.normalizeMonitorOutput({...preferred,enabled:false});result=await m.monitorOutputCoordinator.apply(off);await flush(24);
 check('Master off closes whichever owned carrier is active, releases sole data demand, retains RTSS and does not restart',()=>{assert(result.ok);assert.equal(f.native,null);assert(!f.steamShown);assert.equal(f.demand,0);assert(f.rtss)});
 // Native Carrier spends its process-lifetime budget before emitting close.
 // Verify the real host stops reconciling at that native availability gate.
 result=await m.monitorOutputCoordinator.apply(m.normalizeMonitorOutput());await flush(24);assert(result.ok);
 const faultCreateStart=createCount();
 for(let fault=1;fault<=3;fault++){
  const crashed=f.native;assert(crashed);f.native=null;f.nativeFaults=fault;
  f.emit('desktopHud.closed',{...crashed,reason:'webview-process-failed-1'});await flush(36);
  if(fault<3){assert(f.native);assert.equal(createCount(),faultCreateStart+fault);}
  else{assert.equal(f.native,null);assert.equal(createCount(),faultCreateStart+2);assert.equal(f.demand,0);}
 }
 check('Renderer faults recover twice, then native availability blocks further host recreation and releases demand',()=>{assert.equal(createCount(),faultCreateStart+2);assert.equal(f.demand,0);assert.equal(host.inspect().desktop.owned,null)});
 const blockedCalls=f.calls.length,blockedReads=f.reads;
 for(let i=0;i<100;i++)f.emit('deckySidebar.updated',{steamKnown:true,steamRunning:false});
 await flush(60);const settledCalls=f.calls.length;await flush(60);
 check('Terminal renderer block cannot become a status/restore/sensor retry loop',()=>{assert.equal(createCount(),faultCreateStart+2);assert.equal(f.reads,blockedReads);assert.equal(f.demand,0);assert.equal(f.calls.length,settledCalls);assert(!f.calls.slice(blockedCalls).some(([cmd])=>cmd==='desktopHud.create'))});
 result=await m.monitorOutputCoordinator.apply(m.normalizeMonitorOutput());await flush(24);
 check('Explicit output apply also respects terminal native recovery gate',()=>{assert(!result.ok);assert.equal(createCount(),faultCreateStart+2);assert.equal(f.native,null);assert.equal(f.demand,0)});
 host.stop();await flush();host=null;check('Stopping the host removes all IO event listeners/extension and cannot leave acquisition or control leases',()=>{assert.equal(f.extension,null);assert.equal(f.demand,0);assert([...f.events.values()].every(s=>s.size===0))});
 const regressionFailures=[],switchTraces=[];
 const scenario=async(name,run)=>{try{await run();cases.push(name);}catch(error){regressionFailures.push({name,error:String(error)});}finally{host?.stop();await flush(24);host=null;f.statusHolds=0;f.statusGate=null;f.delayCloseEvent=false;f.closedEvents=[];f.failRtssLoad=false;f.failNativeClose=false;f.failRtssStart=false;f.rtss=true;}};
 async function freshHost(){
  f.native=null;f.nativeFaults=0;f.steamShown=false;f.steamPresence=false;f.failSteamShow=false;f.rtssLoads=[];
  f.settings.rtss.monitorOutput=m.normalizeMonitorOutput({output:'ymcc',enabled:true,ymccCarrier:'desktop'});
  host=m.startMonitorHudHost();await host.ready();await flush(24);assert(f.native,'fresh explicit desktop must be available');
  f.emit('deckySidebar.updated',{steamKnown:true,steamRunning:false});await flush(12);
 }
 await scenario('Saving an RTSS target withdraws old recovery intent without migrating output before explicit apply',async()=>{
  await freshHost();const existing=f.native,createdBefore=createCount(),start=f.rtssLoads.length;
  const selected=m.normalizeMonitorOutput({output:'rtss',enabled:true,rtssTemplate:'W'});await m.saveMonitorOutputPreferences(selected);await flush(24);
  assert.equal(f.native?.id,existing.id,'persistence alone cannot migrate the current live window');assert.equal(createCount(),createdBefore);assert.deepEqual(f.rtssLoads.slice(start),[]);
  f.native=null;f.emit('desktopHud.closed',{...existing,reason:'synthetic-close-between-save-and-apply'});await flush(48);
  switchTraces.push({name:'saved-target-before-explicit-apply',loads:f.rtssLoads.slice(start),customCreates:createCount()-createdBefore,ownedHud:f.native,demand:f.demand,host:host.inspect()});
  assert.equal(f.native,null,'old custom recovery is obsolete as soon as a different output has been saved');assert.equal(createCount(),createdBefore);assert.deepEqual(f.rtssLoads.slice(start),[]);assert.equal(f.demand,0);
  const applied=await m.monitorOutputCoordinator.apply(selected);await flush(24);assert(applied.ok);assert.equal(f.native,null);assert.equal(f.layout,'YeManOBS-W-1.ovl');assert.deepEqual(f.rtssLoads.slice(start),['W']);
 });
 await scenario('Failed RTSS selection cannot revive the previous custom HUD after a late close and Steam reconnect',async()=>{
  await freshHost();const old=f.native;const createdBefore=createCount(),start=f.rtssLoads.length;
  f.delayCloseEvent=true;f.failRtssLoad=true;
  const selected=m.normalizeMonitorOutput({output:'rtss',enabled:true,rtssTemplate:'W'});await m.saveMonitorOutputPreferences(selected);
  const failed=await m.monitorOutputCoordinator.apply(selected);await flush(24);
  assert(!failed.ok);assert.equal(failed.phase,'show-rtss');assert.equal(f.native,null);
  const late=f.closedEvents.splice(0);assert.equal(late.length,1);assert.equal(late[0].id,old.id);
  for(const event of late)f.emit('desktopHud.closed',event);f.extension.connected(peer);await flush(48);
  switchTraces.push({name:'failed-target-with-late-close',result:failed,loads:f.rtssLoads.slice(start),customCreates:createCount()-createdBefore,ownedHud:f.native,host:host.inspect()});
  assert.equal(f.native,null,'saved RTSS intent must not be replaced by a stale custom-output recovery');
  assert.equal(createCount(),createdBefore);assert.deepEqual(f.rtssLoads.slice(start),['W']);
  f.delayCloseEvent=false;f.failRtssLoad=false;const retry=await m.monitorOutputCoordinator.apply(selected);await flush(24);assert(retry.ok);assert.equal(f.native,null);assert.equal(f.layout,'YeManOBS-W-1.ovl');assert.deepEqual(f.rtssLoads.slice(start),['W','W'],'failed target followed by explicit retry has no automatic off or extra load');
 });
 await scenario('Unconfirmed master OFF cannot auto-enable the previous custom output after its eventual native close',async()=>{
  await freshHost();const closing=f.native,createdBefore=createCount();f.failNativeClose=true;
  const disabled=m.normalizeMonitorOutput({output:'ymcc',enabled:false,ymccCarrier:'desktop'});
  const failed=await m.monitorOutputCoordinator.apply(disabled);await flush(24);assert(!failed.ok);assert.equal(failed.phase,'hide-ymcc');
  f.native=null;f.emit('desktopHud.closed',{...closing,reason:'synthetic-late-close-after-unconfirmed-off'});await flush(48);
  switchTraces.push({name:'unconfirmed-off-with-eventual-close',result:failed,customCreates:createCount()-createdBefore,ownedHud:f.native,demand:f.demand,host:host.inspect()});
  assert.equal(f.native,null,'master OFF intent must remain off even when the initial close ACK failed');assert.equal(createCount(),createdBefore);assert.equal(f.demand,0);
  f.failNativeClose=false;const shown=await m.monitorOutputCoordinator.apply(m.normalizeMonitorOutput({output:'ymcc',enabled:true,ymccCarrier:'desktop'}));await flush(24);assert(shown.ok);assert(f.native);assert.equal(f.demand,1,'explicit re-enable remains usable');
 });
 await scenario('A pre-existing availability wait cannot reapply RTSS after an explicit RTSS switch already completed',async()=>{
  await freshHost();f.delayCloseEvent=true;const renewed=await m.monitorOutputCoordinator.apply(m.normalizeMonitorOutput({output:'ymcc',enabled:true,ymccCarrier:'desktop'}));await flush(24);assert(renewed.ok);f.delayCloseEvent=false;const earlierClosed=f.closedEvents.splice(0);assert.equal(earlierClosed.length,1);
  const crashed=f.native;f.native=null;let release;f.statusGate=new Promise(resolve=>{release=resolve});f.statusHolds=2;
  f.emit('desktopHud.closed',{...crashed,reason:'synthetic-unexpected-close'});await flush(12);assert.equal(f.statusHolds,0,'availability and reconciliation must both reach the held native query');
  const selected=m.normalizeMonitorOutput({output:'rtss',enabled:true,rtssTemplate:'L'}),start=f.rtssLoads.length;
  await m.saveMonitorOutputPreferences(selected);const applied=await m.monitorOutputCoordinator.apply(selected);await flush(24);assert(applied.ok);assert.deepEqual(f.rtssLoads.slice(start),['L']);
  for(const event of earlierClosed)f.emit('desktopHud.closed',event);release();await flush(48);
  switchTraces.push({name:'deferred-reconcile-after-successful-switch',result:applied,loads:f.rtssLoads.slice(start),ownedHud:f.native,host:host.inspect()});
  assert.equal(f.native,null);assert.deepEqual(f.rtssLoads.slice(start),['L'],'stale reconciliation must not run another full RTSS load');
 });
 await scenario('Appearance edits during a pending native availability query still restore the current custom output once',async()=>{
  await freshHost();const crashed=f.native,createdBefore=createCount();f.native=null;let release;f.statusGate=new Promise(resolve=>{release=resolve});f.statusHolds=2;
  f.emit('desktopHud.closed',{...crashed,reason:'synthetic-unexpected-close-before-appearance-edit'});await flush(12);assert.equal(f.statusHolds,0);
  const appearance=m.normalizeMonitorOutput({...f.settings.rtss.monitorOutput,ymcc:{...f.settings.rtss.monitorOutput.ymcc,opacity:.37}});await m.saveMonitorOutputPreferences(appearance);
  release();await flush(48);
  switchTraces.push({name:'current-custom-appearance-during-recovery',customCreates:createCount()-createdBefore,ownedHud:f.native,demand:f.demand,host:host.inspect()});
  assert(f.native,'canceling stale output intent must not cancel current custom recovery after a harmless appearance edit');assert.equal(createCount(),createdBefore+1);assert.equal(f.demand,1);
 });

 await scenario('OFF after failed backend start closes the actual owned desktop and releases data despite unconfirmed RTSS',async()=>{
  await freshHost();const old=f.native,createdBefore=createCount(),start=f.rtssLoads.length,starts=f.backendStarts;
  f.rtss=false;f.failRtssStart=true;const selected=m.normalizeMonitorOutput({output:'rtss',enabled:true});await m.saveMonitorOutputPreferences(selected);
  const failed=await m.monitorOutputCoordinator.apply(selected);await flush(24);assert(!failed.ok);assert.equal(failed.phase,'rtss-backend');assert(f.native,'failed preflight preserves current native window');
  const disabled=m.normalizeMonitorOutput({...selected,enabled:false});await m.saveMonitorOutputPreferences(disabled);const closed=await m.monitorOutputCoordinator.apply(disabled);await flush(24);
  switchTraces.push({name:'backend-failure-then-off',failed,closed,backendStarts:f.backendStarts-starts,loads:f.rtssLoads.slice(start),native:f.native,demand:f.demand,host:host.inspect()});
  assert.equal(f.native,null,'OFF must release the known native owner even when RTSS is unavailable');assert.equal(f.demand,0);assert(!closed.ok);assert.equal(closed.applied,'unknown','an offline RTSS receipt is not live confirmation');assert.equal(closed.phase,'hide-rtss');assert.equal(f.backendStarts-starts,1,'OFF cannot try to start the failed backend again');assert.equal(f.rtss,false);assert.deepEqual(f.rtssLoads.slice(start),['off']);
  f.emit('desktopHud.closed',{...old,reason:'late-owned-close-after-off'});f.extension.connected(peer);await flush(48);assert.equal(f.native,null);assert.equal(f.demand,0);assert.equal(createCount(),createdBefore);
 });

 await scenario('Registered native OFF closes before held capability IO and waits to report overall completion',async()=>{
  await freshHost();const closing=f.native,createdBefore=createCount();let release;f.statusGate=new Promise(resolve=>{release=resolve});f.statusHolds=100;
  try{
   const disabled=m.normalizeMonitorOutput({output:'rtss',enabled:false});await m.saveMonitorOutputPreferences(disabled);let settled=false;
   const pending=m.monitorOutputCoordinator.apply(disabled).then(result=>{settled=true;return result});await flush(24);
   const immediate={native:f.native,demand:f.demand,settled,heldQueries:100-f.statusHolds};
   release();const result=await pending;await flush(24);
   switchTraces.push({name:'owned-close-before-capability-IO',immediate,result,ownedHud:f.native,demand:f.demand,host:host.inspect()});
   assert.equal(immediate.native,null,'known owner must close before a held availability query can resolve');assert.equal(immediate.demand,0);assert.equal(immediate.settled,false,'a closed native owner alone is not full output confirmation');assert(immediate.heldQueries>0);
   assert(result.ok);assert.equal(result.applied,'off');assert.equal(f.native,null);assert.equal(f.demand,0);assert.equal(createCount(),createdBefore);
   f.emit('desktopHud.closed',{...closing,reason:'late-close-after-held-capability-query'});await flush(24);assert.equal(f.native,null);
  }finally{release();}
 });
 const report={ok:regressionFailures.length===0,passed:cases.length,failed:regressionFailures.length,failures:regressionFailures,switchTraces,cases,actualAppChanged:false,actualSteamWindow:false,scope:'Exact production host/coordinator/router/clock/relay/desktop owner with explicitly synthetic HWND, native cache and authenticated-plugin receipts. Not desktop/Steam pixel acceptance.'};fs.writeFileSync(path.join(out,'HOST-OPTIONAL-STEAM-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));if(regressionFailures.length)process.exitCode=2;
 }
 if(startupOnly||withStartup&&!process.exitCode)await runStartup();
}finally{host?.stop();await flush();delete globalThis.__hudHostTest;delete globalThis.window}
