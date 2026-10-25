// Exact host/coordinator/router/clock/relay/desktop owner integration with inert IO.
// All native HWNDs, Steam statuses and snapshots here are synthetic test receipts.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=process.cwd(),out=path.resolve(process.env.YMCC_HUD_OPTIONAL_OUT||path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/global-followup-20261011/tests'));fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const mocks={
 // Transient notifications have separate consumer/renderer coverage; this fixture
 // isolates the persistent HUD data-demand and output-coordinator lifecycle.
 'src/bridge/monitorNoticeHost.ts':`export function startMonitorNoticeHost(){return {ready(){},update(){},stop(){}}}`,
 'src/bridge/monitorGameLogo.ts':"export const subscribeMonitorGameLogo=()=>()=>{};",
 'src/bridge/ipc.ts':"export const isNativeRuntime=true;export const invoke=(command,args)=>globalThis.__hudHostTest.invoke(command,args);export function on(name,fn){const f=globalThis.__hudHostTest;const listeners=f.events.get(name)||new Set();f.events.set(name,listeners);listeners.add(fn);return ()=>listeners.delete(fn)}",
 'src/bridge/settingsRepository.ts':"export async function updateSettings(edit){edit(globalThis.__hudHostTest.settings)}export async function readSettingsSection(name){return structuredClone(globalThis.__hudHostTest.settings[name]||{})}export async function saveSettingsSection(name,patch){Object.assign(globalThis.__hudHostTest.settings[name],structuredClone(patch))}",
 'src/bridge/yeman.ts':"const f=()=>globalThis.__hudHostTest;export async function rtssRunning(){return f().rtss}export async function toggleRtss(show){if(!show)throw Error('RTSS must remain in background');f().rtss=show}export async function readOverlayLayout(){return f().layout}export async function setOverlayLayout(v){f().layout=v==='off'?'Empty.ovl':({W:'YeManOBS-W-1.ovl',L:'YeManOBS-L-1.ovl',J:'YeManOBS-JJ-1.ovl'})[v];return {mode:'live'}}export async function readRtssZoom(){return 5}export async function detectCpuName(){return 'Fixture CPU'}",
 'src/bridge/monitorData.ts':"const f=()=>globalThis.__hudHostTest;export function acquireMonitorDemand(){f().demand++;let closed=false;return()=>{if(!closed){closed=true;f().demand--}}}export async function readMonitorSnapshot(){f().reads++;return f().snapshot}export function subscribeMonitorSnapshot(fn){fn(f().snapshot);return ()=>{}}export const subscribeMonitorHudSnapshot=subscribeMonitorSnapshot;export function subscribeMonitorTdpState(fn){fn(null);return ()=>{}}export function acceptPublishedMonitorSnapshot(){}export function isOwnedMonitorDisplaySnapshot(){return false}",
 'src/bridge/deckyMirrorExtensions.ts':"export function registerDeckyMirrorExtension(ext){globalThis.__hudHostTest.extension=ext;return()=>{globalThis.__hudHostTest.extension=null}}",
};
const b=await build({stdin:{contents:"export {startMonitorHudHost} from './src/bridge/monitorHudHost.ts';export {normalizeMonitorOutput} from './src/bridge/monitorHudModel.ts';export {monitorOutputCoordinator,saveMonitorOutputPreferences} from './src/bridge/monitorOutput.ts';",resolveDir:root,loader:'ts'},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'inert-host-io',setup(build){build.onLoad({filter:/\.ts$/},args=>{const key=path.relative(root,args.path).replaceAll('\\','/');return mocks[key]?{contents:mocks[key],loader:'ts',resolveDir:path.dirname(args.path)}:undefined})}}]});
const file=path.join(out,'host-under-test.mjs');fs.writeFileSync(file,b.outputFiles[0].text);const cases=[];let host;
const now=Date.now(),runId='a'.repeat(32),peer={connection:'1',runId,connected:true};
const game={appId:1091500,gameId:'1091500',browserGameId:'1091500',overlayTitle:'SP Overlay: 2/3/4',overlayPid:2,overlayBrowserId:3,ownerBrowserId:5,overlayBrowserType:4,overlayNativeAppId:1091500};
const f={settings:{rtss:{}},events:new Map(),calls:[],native:null,nextId:1,extension:null,rtss:true,layout:'YeManOBS-W-1.ovl',demand:0,reads:0,nativeFaults:0,steamShown:false,steamPresence:true,failSteamShow:false,snapshot:{top:{ts:now,cpuUsage:10,tempC:50,coreUsage:[10,20,30],hwDown:false},fps:{ts:now,fps:60,fps1:40,gpu:20},hwDown:false},emit(name,args){for(const fn of this.events.get(name)||[])fn(args)}};
globalThis.__hudHostTest=f;globalThis.document=Object.assign(new EventTarget(),{visibilityState:'visible',createElement:()=>({content:{firstChild:null}})});
globalThis.window=Object.assign(new EventTarget(),{setTimeout,clearTimeout});
const request=status=>f.extension.request({...peer,request:{runId,command:'hud.status',status}});
f.invoke=async(command,args={})=>{
 f.calls.push([command,args]);
 if(command==='desktopHud.status')return {available:f.nativeFaults<3,id:f.native?.id??0,nonce:f.native?.nonce,rendererFailures:f.nativeFaults,recoveryBlocked:f.nativeFaults>=3};
 if(command==='desktopHud.create'){assert(f.nativeFaults<3,'native process-lifetime recovery budget must not be bypassed');assert.equal(args.scope,'global');assert.equal(args.pid,undefined);const v={id:f.nextId++,nonce:args.nonce};f.native=v;queueMicrotask(()=>f.emit('desktopHud.ready',v));return v}
 if(command==='desktopHud.frame'){queueMicrotask(()=>f.emit('desktopHud.painted',{id:args.hudId,nonce:args.nonce,seq:args.frame.seq}));return true}
 if(command==='desktopHud.close'){const v=f.native;if(v){assert.equal(v.id,args.hudId);assert.equal(v.nonce,args.nonce);f.native=null;queueMicrotask(()=>f.emit('desktopHud.closed',v))}return true}
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
 const report={passed:cases.length,cases,actualAppChanged:false,actualSteamWindow:false,scope:'Exact production host/coordinator/router/clock/relay/desktop owner with explicitly synthetic HWND, native cache and authenticated-plugin receipts. Not desktop/Steam pixel acceptance.'};fs.writeFileSync(path.join(out,'HOST-OPTIONAL-STEAM-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{host?.stop();await flush();delete globalThis.__hudHostTest;delete globalThis.window}
