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
 'src/bridge/ipc.ts':"export const isNativeRuntime=true;export const invoke=(command,args)=>globalThis.__hudHostTest.invoke(command,args);export function on(name,fn){const f=globalThis.__hudHostTest;const listeners=f.events.get(name)||new Set();f.events.set(name,listeners);listeners.add(fn);return ()=>listeners.delete(fn)}",
 'src/bridge/settingsRepository.ts':"export async function updateSettings(edit){edit(globalThis.__hudHostTest.settings)}export async function readSettingsSection(name){return structuredClone(globalThis.__hudHostTest.settings[name]||{})}export async function saveSettingsSection(name,patch){Object.assign(globalThis.__hudHostTest.settings[name],structuredClone(patch))}",
 'src/bridge/yeman.ts':"const f=()=>globalThis.__hudHostTest;export async function rtssRunning(){return f().rtss}export async function toggleRtss(show){if(!show)throw Error('RTSS must remain in background');++f().backendStarts;if(f().failRtssStart)throw Error('synthetic RTSS backend unavailable');f().rtss=show}export async function readOverlayLayout(){return f().layout}export async function setOverlayLayout(v){f().rtssLoads.push(v);if(v!=='off'&&f().failRtssLoad)throw Error('synthetic RTSS target load failure');f().layout=v==='off'?'Empty.ovl':({W:'YeManOBS-W-1.ovl',L:'YeManOBS-L-1.ovl',J:'YeManOBS-JJ-1.ovl'})[v];return {mode:f().rtss?'live':'offline'}}export async function readRtssZoom(){return 5}export async function detectCpuName(){return 'Fixture CPU'}",
 'src/bridge/monitorData.ts':"const f=()=>globalThis.__hudHostTest;export function acquireMonitorDemand(){f().demand++;let closed=false;return()=>{if(!closed){closed=true;f().demand--}}}export async function readMonitorSnapshot(){f().reads++;return f().snapshot}export function subscribeMonitorSnapshot(fn){fn(f().snapshot);return ()=>{}}export const subscribeMonitorHudSnapshot=subscribeMonitorSnapshot;export function subscribeMonitorTdpState(fn){fn(null);return ()=>{}}export function acceptPublishedMonitorSnapshot(){}export function isOwnedMonitorDisplaySnapshot(){return false}",
 'src/bridge/deckyMirrorExtensions.ts':"export function registerDeckyMirrorExtension(ext){globalThis.__hudHostTest.extension=ext;return()=>{globalThis.__hudHostTest.extension=null}}",
};
const compiledInputs={};
const b=await build({metafile:true,stdin:{contents:"export {startMonitorHudHost} from './src/bridge/monitorHudHost.ts';export {normalizeMonitorOutput} from './src/bridge/monitorHudModel.ts';export {monitorOutputCoordinator,saveMonitorOutputPreferences} from './src/bridge/monitorOutput.ts';",resolveDir:root,loader:'ts'},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'inert-host-io',setup(build){build.onLoad({filter:/\.ts$/},args=>{const key=path.relative(root,args.path).replaceAll('\\','/');const sourcePath=key==='src/bridge/monitorOutput.ts'&&process.env.YMCC_HUD_OUTPUT_SOURCE||key==='src/bridge/monitorOutputCoordinator.ts'&&process.env.YMCC_HUD_COORDINATOR_SOURCE||key==='src/bridge/monitorHudHost.ts'&&process.env.YMCC_HUD_HOST_SOURCE||args.path;const contents=mocks[key]??fs.readFileSync(sourcePath,'utf8');compiledInputs[args.path]={kind:mocks[key]?'synthetic-port':'production',sourcePath:mocks[key]?undefined:sourcePath,sha256:crypto.createHash('sha256').update(contents).digest('hex')};return {contents,loader:'ts',resolveDir:path.dirname(args.path)}})}}]});
for(const input of Object.keys(b.metafile.inputs)){const resolved=path.resolve(root,input);if(!compiledInputs[resolved]&&fs.existsSync(resolved))compiledInputs[resolved]={kind:'production',sha256:crypto.createHash('sha256').update(fs.readFileSync(resolved)).digest('hex')};}fs.writeFileSync(path.join(out,'HOST-COMPILED-INPUTS.json'),JSON.stringify(compiledInputs,null,2));
const file=path.join(out,'host-under-test.mjs');fs.writeFileSync(file,b.outputFiles[0].text);const cases=[];let host;
const now=Date.now(),runId='a'.repeat(32),peer={connection:'1',runId,connected:true};
const game={appId:1091500,gameId:'1091500',browserGameId:'1091500',overlayTitle:'SP Overlay: 2/3/4',overlayPid:2,overlayBrowserId:3,ownerBrowserId:5,overlayBrowserType:4,overlayNativeAppId:1091500};
const f={settings:{rtss:{}},events:new Map(),calls:[],native:null,nextId:1,extension:null,rtss:true,layout:'YeManOBS-W-1.ovl',demand:0,reads:0,nativeFaults:0,steamShown:false,steamPresence:true,failSteamShow:false,snapshot:{top:{ts:now,cpuUsage:10,tempC:50,coreUsage:[10,20,30],hwDown:false},fps:{ts:now,fps:60,fps1:40,gpu:20},hwDown:false},emit(name,args){for(const fn of this.events.get(name)||[])fn(args)}};
f.backendStarts=0;f.failRtssStart=false;f.rtssLoads=[];f.failRtssLoad=false;f.failNativeClose=false;f.delayCloseEvent=false;f.closedEvents=[];f.statusHolds=0;f.statusGate=null;
globalThis.__hudHostTest=f;globalThis.document=Object.assign(new EventTarget(),{visibilityState:'visible',createElement:()=>({content:{firstChild:null}})});
globalThis.window=Object.assign(new EventTarget(),{setTimeout,clearTimeout});
const request=status=>f.extension.request({...peer,request:{runId,command:'hud.status',status}});
f.invoke=async(command,args={})=>{
 f.calls.push([command,args]);
 if(command==='desktopHud.status'){if(f.statusHolds>0){--f.statusHolds;await f.statusGate;}return {available:f.nativeFaults<3,id:f.native?.id??0,nonce:f.native?.nonce,rendererFailures:f.nativeFaults,recoveryBlocked:f.nativeFaults>=3}};
 if(command==='desktopHud.create'){assert(f.nativeFaults<3,'native process-lifetime recovery budget must not be bypassed');assert.equal(args.scope,'global');assert.equal(args.pid,undefined);const v={id:f.nextId++,nonce:args.nonce};f.native=v;queueMicrotask(()=>f.emit('desktopHud.ready',v));return v}
 if(command==='desktopHud.frame'){queueMicrotask(()=>f.emit('desktopHud.painted',{id:args.hudId,nonce:args.nonce,seq:args.frame.seq}));return true}
 if(command==='desktopHud.close'){if(f.failNativeClose)throw Error('synthetic native close failure');const v=f.native;if(v){assert.equal(v.id,args.hudId);assert.equal(v.nonce,args.nonce);f.native=null;f.delayCloseEvent?f.closedEvents.push(v):queueMicrotask(()=>f.emit('desktopHud.closed',v))}return true}
 if(command==='monitor.hudPulse.set'||command==='monitor.hudPulse.ack')return true;
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
}finally{host?.stop();await flush();delete globalThis.__hudHostTest;delete globalThis.window}
