// Production HUD host, coordinator, sample source, clock and unified demand hub.
// Native windows/receipts are synthetic. No hardware, process or filesystem IO.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
const root=process.cwd();
const out=path.resolve(process.env.YMCC_HUD_DEMAND_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hud-demand-closure-20261016');
fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const mocks={
 // Transient notifications have separate consumer/renderer coverage; this fixture
 // isolates the persistent HUD data-demand and output-coordinator lifecycle.
 'src/bridge/monitorNoticeHost.ts':`export function startMonitorNoticeHost(){return {ready(){},update(){},stop(){}}}`,
 'src/bridge/ipc.ts':`export const isNativeRuntime=true;
  export const invoke=(command,args)=>globalThis.__hudDemandHost.invoke(command,args);
  export function on(name,fn){const f=globalThis.__hudDemandHost;const listeners=f.events.get(name)||new Set();f.events.set(name,listeners);listeners.add(fn);return()=>listeners.delete(fn)}`,
 'src/bridge/api.ts':`export const fs={readTextFile(){throw Error('No compatibility file reads')}};export const windowApi={};`,
 'src/bridge/settingsRepository.ts':`export async function updateSettings(edit){edit(globalThis.__hudDemandHost.settings)}export async function readSettingsSection(name){return structuredClone(globalThis.__hudDemandHost.settings[name]||{})}
  export async function saveSettingsSection(name,patch){Object.assign(globalThis.__hudDemandHost.settings[name],structuredClone(patch))}`,
 'src/bridge/yeman.ts':`const f=()=>globalThis.__hudDemandHost;export async function rtssRunning(){return true}
  export async function toggleRtss(show){assertRtss(show)}function assertRtss(show){if(!show)throw Error('RTSS must remain in background')}
  export async function readOverlayLayout(){return f().layout}
  export async function setOverlayLayout(v){f().layout=v==='off'?'Empty.ovl':({W:'YeManOBS-W-1.ovl',L:'YeManOBS-L-1.ovl',J:'YeManOBS-JJ-1.ovl'})[v];return {mode:'live'}}
  export async function readRtssZoom(){return 5}export async function detectCpuName(){return 'Fixture CPU'}`,
 'src/bridge/monitorAppliedTdp.ts':`export function advanceMonitorTdpDisplay(){}
  export function subscribeMonitorTdpState(fn){fn(null);return()=>{}}`,
 'src/bridge/monitorGameLogo.ts':`export function subscribeMonitorGameLogo(){throw Error('No logo acquisition in this fixture')}`,
 'src/bridge/deckyMirrorExtensions.ts':`export function registerDeckyMirrorExtension(ext){globalThis.__hudDemandHost.extension=ext;return()=>{globalThis.__hudDemandHost.extension=null}}`,
};
const f={settings:{rtss:{}},events:new Map(),calls:[],native:null,nextId:1,extension:null,layout:'Empty.ovl',actual:{top:false,fps:false},seq:0,
 emit(name,args){for(const fn of this.events.get(name)||[])fn(args)}};
const timers=new Map();let timerId=0;
globalThis.__hudDemandHost=f;
globalThis.document=Object.assign(new EventTarget(),{visibilityState:'visible',createElement(){return {content:{}}}});
globalThis.window=Object.assign(new EventTarget(),{setTimeout(fn,ms){const id=++timerId;timers.set(id,{fn,ms});return id},clearTimeout(id){timers.delete(id)}});
f.invoke=async(command,args={})=>{
 f.calls.push({command,args});
 if(command==='desktopHud.status')return {available:true,id:f.native?.id??0,nonce:f.native?.nonce};
 if(command==='desktopHud.create'){assert.equal(args.scope,'global');const v={id:f.nextId++,nonce:args.nonce};f.native=v;queueMicrotask(()=>f.emit('desktopHud.ready',v));return v}
 if(command==='desktopHud.frame'){queueMicrotask(()=>f.emit('desktopHud.painted',{id:args.hudId,nonce:args.nonce,seq:args.frame.seq}));return true}
 if(command==='desktopHud.close'){const v=f.native;if(v){assert.equal(args.hudId,v.id);assert.equal(args.nonce,v.nonce);f.native=null;queueMicrotask(()=>f.emit('desktopHud.closed',v))}return true}
 if(command==='monitor.hudPulse.set'||command==='monitor.hudPulse.ack')return true;
 if(command==='monitor.start'||command==='monitor.stop'){if(args.top)f.actual.top=command==='monitor.start';if(args.fps)f.actual.fps=command==='monitor.start';return {ok:true}}
 if(command==='monitor.snapshot')return {schema:1,available:false,session:1,generation:1,relayEpoch:0,seq:++f.seq,topActive:f.actual.top,fpsActive:f.actual.fps};
 if(command==='monitor.takeover')return {ok:true,cleaned:true};
 if(command==='monitor.release')return {ok:true};
 throw Error('Unexpected IO '+command);
};
const b=await build({stdin:{resolveDir:root,loader:'ts',contents:`export {startMonitorHudHost} from './src/bridge/monitorHudHost.ts';
 export {normalizeMonitorOutput,HUD_METRICS} from './src/bridge/monitorHudModel.ts';
 export {monitorOutputCoordinator,saveMonitorOutputPreferences} from './src/bridge/monitorOutput.ts';
 export {acquireMonitorDemand,monitorDemandConfirmed} from './src/bridge/monitorData.ts';`},bundle:true,platform:'node',format:'esm',write:false,
 plugins:[{name:'inert-edges',setup(b){b.onLoad({filter:/\.ts$/},a=>{const k=path.relative(root,a.path).replaceAll('\\','/');return mocks[k]?{contents:mocks[k],loader:'ts',resolveDir:path.dirname(a.path)}:undefined})}}]});
const file=path.join(out,'host-demand-under-test.mjs');fs.writeFileSync(file,b.outputFiles[0].text);
const m=await import(pathToFileURL(file));
const flush=async()=>{for(let i=0;i<35;i++)await new Promise(r=>setImmediate(r))};
const cases=[];const check=(name,fn)=>{fn();cases.push(name)};
const flags=(...ids)=>Object.fromEntries(m.HUD_METRICS.map(v=>[v.id,ids.includes(v.id)]));
let prefs=m.normalizeMonitorOutput({enabled:true,output:'ymcc',ymcc:{fields:flags('cpuTemp'),underlay:'none'}}),host,offAuto;
f.settings.rtss.monitorOutput=prefs;
try{
 host=m.startMonitorHudHost();await host.ready();await flush();
 const originalWindow={...f.native},originalClock=host.inspect().clock.nonce;if(process.env.YMCC_HUD_DEMAND_DEBUG)console.log(JSON.stringify({inspect:host.inspect(),calls:f.calls.map(c=>({command:c.command,...(c.command==='monitor.hudPulse.set'?c.args:{})}))},null,2));
 const count=cmd=>f.calls.filter(c=>c.command===cmd).length;
 const controlCalls=()=>f.calls.filter(c=>c.command==='monitor.hudPulse.set'&&c.args.enabled===true&&!c.args.update).length;
 const initialClockStarts=controlCalls();
 check('saved CPU-only fields reach the actual host and native hub before its first start',()=>{assert(f.native);assert.deepEqual(f.actual,{top:true,fps:false});assert.equal(host.inspect().sampleUsers,1);assert.equal(count('monitor.snapshot'),1);assert.equal(host.inspect().clock.users,1)});
 async function save(...ids){prefs=m.normalizeMonitorOutput({...prefs,ymcc:{...prefs.ymcc,fields:flags(...ids)}});await m.saveMonitorOutputPreferences(prefs);await flush()}
 await save('fps');
 check('live CPU-to-FPS change reaches existing hub without replacing the HWND, clock nonce or source read',()=>{assert.deepEqual(f.actual,{top:false,fps:true});assert.deepEqual(f.native,originalWindow);assert.equal(host.inspect().clock.nonce,originalClock);assert.equal(controlCalls(),initialClockStarts);assert.equal(count('monitor.snapshot'),1)});
 await save('fpsLimit');
 check('live frame-cap selection requests both required native channels',()=>assert.deepEqual(f.actual,{top:true,fps:true}));
 await save('clock','tdpLimit');
 check('software-only preferences release both sensor channels while retaining the visible HUD clock',()=>{assert.deepEqual(f.actual,{top:false,fps:false});assert.equal(host.inspect().clock.enabled,true);assert.equal(host.inspect().clock.nonce,originalClock);assert(f.native)});
 offAuto=m.acquireMonitorDemand('autofloat-fixture',{fps:true});await flush();
 await save('cpuTemp');
 check('another owner keeps FPS alive while the product HUD requests only top',()=>assert.deepEqual(f.actual,{top:true,fps:true}));
 await save('clock','tdpLimit');
 check('product HUD preferences cannot stop an independent FPS consumer',()=>assert.deepEqual(f.actual,{top:false,fps:true}));
 offAuto();offAuto=null;await flush();
 check('independent FPS disposer stops its own channel after product HUD no longer needs it',()=>assert.deepEqual(f.actual,{top:false,fps:false}));
 await save('cpuTemp');
 const before={window:{...f.native},clock:host.inspect().clock.nonce,starts:count('monitor.start'),stops:count('monitor.stop'),reads:count('monitor.snapshot'),clockStarts:controlCalls()};
 for(let i=0;i<100;i++)await m.saveMonitorOutputPreferences(prefs);await flush();
 check('100 persisted unchanged selections cannot restart native collection or the HUD clock',()=>{assert.equal(count('monitor.start'),before.starts);assert.equal(count('monitor.stop'),before.stops);assert.equal(count('monitor.snapshot'),before.reads);assert.equal(controlCalls(),before.clockStarts);assert.deepEqual(f.native,before.window);assert.equal(host.inspect().clock.nonce,before.clock)});
 const off=m.normalizeMonitorOutput({...prefs,enabled:false});const result=await m.monitorOutputCoordinator.apply(off);await flush();
 check('master-off closes the actual owned carrier and its sensors without a replacement read',()=>{assert(result.ok);assert.equal(f.native,null);assert.deepEqual(f.actual,{top:false,fps:false});assert.equal(host.inspect().sampleUsers,0);assert.equal(count('monitor.snapshot'),1)});
 host.stop();host=null;await flush();
 check('host teardown removes all event listeners, source/clock leases and guarded IO timers',()=>{assert.equal(f.extension,null);assert([...f.events.values()].every(s=>s.size===0));assert.equal(timers.size,0)});
 // A global mouse-status setting must survive a carrier recovery; retaining only
 // template appearance in activePreferences used to restore its old default.
 prefs=m.normalizeMonitorOutput({...prefs,enabled:true,mouseStatusEnabled:true});f.settings.rtss.monitorOutput=prefs;
 host=m.startMonitorHudHost();await host.ready();await flush();
 await m.saveMonitorOutputPreferences({...prefs,mouseStatusEnabled:false});await flush();
 await new Promise(resolve=>setTimeout(resolve,1010));
 f.emit('monitor.hudPulse',{nonce:host.inspect().clock.nonce,snapshot:{schema:1,available:false,session:1,generation:1,seq:++f.seq,topActive:f.actual.top,fpsActive:f.actual.fps}});await flush();
 check('mouse-status toggle reaches an existing actual host carrier frame',()=>assert.equal(f.calls.filter(c=>c.command==='desktopHud.frame').at(-1).args.frame.preferences.mouseStatusEnabled,false));
 const priorCarrier={...f.native};f.native=null;f.emit('desktopHud.closed',priorCarrier);await flush();
 check('mouse-status OFF survives actual host carrier close and automatic recovery',()=>{assert(f.native&&f.native.id!==priorCarrier.id);assert.equal(f.calls.filter(c=>c.command==='desktopHud.frame').at(-1).args.frame.preferences.mouseStatusEnabled,false)});
 host.stop();host=null;await flush();
 check('mouse-status recovery scenario releases its owned listeners and timers',()=>{assert([...f.events.values()].every(s=>s.size===0));assert.equal(timers.size,0)});
 const resultFile={ok:true,checks:cases.length,cases,nativeFlags:{...f.actual},hardwareReads:false,realWindows:false,sourceSha256:Object.fromEntries(['src/bridge/monitorHudHost.ts','src/bridge/monitorHudModel.ts','src/bridge/monitorHudSampleSource.ts','src/bridge/monitorHudClock.ts','src/bridge/monitorData.ts','src/bridge/monitorOutput.ts'].map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex')])),scope:'Actual product host/coordinator/router/desktop owner/sample/clock/unified hub; inert IPC edges, synthetic native descriptors, existing native control protocol. No deployment or HWiNFO acceptance.'};
 fs.writeFileSync(path.join(out,'HOST-DEMAND-RESULT.json'),JSON.stringify(resultFile,null,2)+'\n');console.log(JSON.stringify({ok:true,checks:cases.length,cases},null,2));
}finally{offAuto?.();host?.stop();await flush();delete globalThis.__hudDemandHost;delete globalThis.window}
