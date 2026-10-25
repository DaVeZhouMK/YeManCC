// Actual production native-preview factory, owner and unified singleton hub.
// All child windows, BroadcastChannels, IPC and timers are inert test boundaries.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {createRequire} from 'node:module';import {pathToFileURL} from 'node:url';
const root=process.cwd(),out=path.resolve(process.env.YMCC_PREVIEW_DEMAND_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/upstream-entry-preview-demand-20261016');fs.mkdirSync(out,{recursive:true});const {build}=createRequire(root+'/package.json')('esbuild');
const oldBC=globalThis.BroadcastChannel,oldWindow=globalThis.window;
const io=globalThis.__ymccPreviewDemand={};
class InertChannel{constructor(name){this.state=globalThis.__ymccPreviewCurrent;this.name=name;this.onmessage=null;this.closed=false;this.state.channels.set(name,this)}postMessage(data){assert(!this.closed);this.state.packets.push(structuredClone(data))}close(){this.closed=true;this.state.channels.delete(this.name)}}
globalThis.BroadcastChannel=InertChannel;
const checks=[],work={};const drain=async()=>{for(let i=0;i<80;i++)await Promise.resolve()};const check=(name,fn)=>{fn();checks.push(name)};
try{
 for(const side of ['before','after']){
  const f=io[side]={events:new Map(),channels:new Map(),packets:[],timers:new Map(),timerId:0,native:new Set(),creates:0,closes:0,seq:0,calls:[],actual:{top:false,fps:false}};
  const ref='globalThis.__ymccPreviewDemand.'+side;
  f.createChild=async options=>{const id=++f.creates;f.native.add(id);const token=new URLSearchParams(options.url.split('?').at(-1)).get('channel');queueMicrotask(()=>f.channels.get('ymcc-hud-preview-'+token)?.onmessage?.({data:{kind:'ymcc-hud-preview-ready',token}}));return id};
  f.closeChild=async id=>{f.closes++;f.native.delete(id);return true};
  f.invoke=async(command,args={})=>{f.calls.push({command,args});if(command==='monitor.start'||command==='monitor.stop'){if(args.top)f.actual.top=command==='monitor.start';if(args.fps)f.actual.fps=command==='monitor.start';return{ok:true}}if(command==='monitor.snapshot'&&f.holdRead)return new Promise(resolve=>{f.pendingRead=resolve});if(command==='monitor.snapshot')return{schema:1,available:false,session:1,generation:1,relayEpoch:0,seq:++f.seq,topActive:f.actual.top,fpsActive:f.actual.fps};if(command==='monitor.takeover')return{ok:true,cleaned:true};if(command==='monitor.releaseRelay')return{ok:true};throw Error('Unexpected IPC '+command)};
  const mocks={
   './api':`export const fs={readTextFile(){throw Error('No compatibility files')}};export const windowApi={createChild:options=>${ref}.createChild(options),closeChild:id=>${ref}.closeChild(id)};`,
   './ipc':`export const isNativeRuntime=true;export const invoke=(command,args)=>${ref}.invoke(command,args);export function on(name,fn){const f=${ref};const listeners=f.events.get(name)||new Set();f.events.set(name,listeners);listeners.add(fn);return()=>listeners.delete(fn)}`,
   './monitorAppliedTdp':`export function advanceMonitorTdpDisplay(){}export function subscribeMonitorTdpState(fn){fn({mode:'ac',status:'applied',watts:30,proof:'write-ack',revision:1,confirmedAt:Date.now()});return()=>{}}`,
   './monitorGameLogo':`export function subscribeMonitorGameLogo(){throw Error('No game-logo acquisition selected')}`,
  };
  const b=await build({stdin:{resolveDir:root,loader:'ts',contents:`export * from './src/bridge/monitorHudPreview.ts';export {HUD_METRICS,normalizeMonitorOutput} from './src/bridge/monitorHudModel.ts';export {acquireMonitorDemand,monitorDemandConfirmed,acceptPublishedMonitorSnapshot} from './src/bridge/monitorData.ts';`},bundle:true,format:'esm',platform:'node',write:false,plugins:[{name:'inert-preview-boundaries',setup(b){b.onResolve({filter:/.*/},a=>mocks[a.path]?{path:a.path,namespace:'fake'}:undefined);b.onLoad({filter:/.*/,namespace:'fake'},a=>({contents:mocks[a.path],loader:'ts'}));if(side==='before')b.onLoad({filter:/monitorHudPreview\.ts$/},a=>({contents:fs.readFileSync(out+'/before/src/bridge/monitorHudPreview.ts','utf8'),resolveDir:path.dirname(a.path),loader:'ts'}));}}]});
  const file=out+'/'+side+'-preview-under-test.mjs';fs.writeFileSync(file,b.outputFiles[0].text);
  globalThis.__ymccPreviewCurrent=f;
  globalThis.window={location:{href:'https://app.localhost/index.html#/rtss'},setTimeout(fn,ms){const id=++f.timerId;f.timers.set(id,{fn,ms});return id},clearTimeout:id=>f.timers.delete(id)};
  const m=await import(pathToFileURL(file)),fields=(...ids)=>Object.fromEntries(m.HUD_METRICS.map(v=>[v.id,ids.includes(v.id)]));
  let prefs=m.normalizeMonitorOutput({enabled:true,output:'ymcc',ymcc:{fields:fields('cpuTemp'),underlay:'none'}});
  const owner=m.createNativeHudPreviewOwner();const id=await owner.open(prefs);await drain();const count=cmd=>f.calls.filter(c=>c.command===cmd).length;
  work[side]={initial:{flags:{...f.actual},snapshotReads:count('monitor.snapshot'),nativeCreates:f.creates}};
  check(side+': production preview opens with its saved fields and one initial cache read',()=>{assert(owner.isOpen());assert.equal(count('monitor.snapshot'),1);assert.equal(f.creates,1);assert.deepEqual(f.actual,side==='after'?{top:true,fps:false}:{top:true,fps:true})});
  if(side==='after'){
   const before={calls:f.calls.length,creates:f.creates,packets:f.packets.length};for(let i=0;i<200;i++)owner.update(prefs);await drain();
   check('200 same-field preview updates add no sensor command, cache read or child-window creation',()=>{assert.equal(f.calls.length,before.calls);assert.equal(f.creates,before.creates)});
   async function update(...ids){prefs=m.normalizeMonitorOutput({...prefs,ymcc:{...prefs.ymcc,fields:fields(...ids)}});owner.update(prefs);await drain()}
   await update('fps');check('live preview CPU-to-FPS selection reaches the actual shared hub',()=>{assert.deepEqual(f.actual,{top:false,fps:true});assert.equal(f.creates,1);assert.equal(count('monitor.snapshot'),1)});
   await update('fpsLimit');check('preview frame limit preserves hardware value and FPS-family dependencies',()=>assert.deepEqual(f.actual,{top:true,fps:true}));
   await update('clock','tdpLimit');check('clock and applied YMCC TDP preview do not demand any native sensor channel',()=>{assert.deepEqual(f.actual,{top:false,fps:false});assert(owner.isOpen());const data=f.packets.filter(v=>v.kind==='ymcc-hud-preview').at(-1)?.data;assert.equal(data.values.tdpLimit,30);assert.equal(typeof data.values.clock,'string')});
   const auto=m.acquireMonitorDemand('autofloat-preview-test',{fps:true});await drain();await update('cpuTemp');check('preview shares rather than overrides the independent autofloat demand',()=>assert.deepEqual(f.actual,{top:true,fps:true}));
   await update('clock','tdpLimit');check('software-only preview leaves another owner FPS request intact',()=>assert.deepEqual(f.actual,{top:false,fps:true}));auto();await drain();
   await update('cpuTemp');
   const diskStarts=count('monitor.start'),diskStops=count('monitor.stop');
   await update('diskRead');check('preview resource-only change enables filesystem while top/fps stay unchanged',()=>{assert.deepEqual(f.actual,{top:true,fps:false});assert.equal(count('monitor.start'),diskStarts+1);assert.equal(count('monitor.stop'),diskStops);assert.equal(f.calls.filter(c=>c.command==='monitor.start').at(-1).args.fileSystem,true)});
   await update('cpuTemp');check('preview resource-only release disables filesystem without stopping top',()=>{assert.deepEqual(f.actual,{top:true,fps:false});assert.equal(count('monitor.start'),diskStarts+2);assert.equal(count('monitor.stop'),diskStops);assert.equal(f.calls.filter(c=>c.command==='monitor.start').at(-1).args.fileSystem,false)});
   m.acceptPublishedMonitorSnapshot({schema:1,available:false,session:1,generation:1,relayEpoch:0,seq:++f.seq,topActive:true,fpsActive:false});await drain();
   check('unavailable real-cache publication cannot remove the preview selected collection',()=>assert.deepEqual(f.actual,{top:true,fps:false}));
   const reused=await owner.open(prefs);await drain();check('reopening an already-ready preview reuses its owned window, source read and demand',()=>{assert.equal(reused,id);assert.equal(f.creates,1);assert.equal(count('monitor.snapshot'),1)});
   owner.close();await drain();check('manual close releases the current replacement lease and all preview resources',()=>{assert.deepEqual(f.actual,{top:false,fps:false});assert.equal(f.native.size,0);assert.equal(f.channels.size,0);assert.equal(f.timers.size,0);assert([...f.events.values()].every(s=>s.size===0))});
   await owner.open(prefs);await drain();const timeout=[...f.timers.values()].find(v=>v.ms===m.HUD_PREVIEW_MAX_MS);assert(timeout);timeout.fn();await drain();check('preview time budget also releases current demand and ownership',()=>{assert(!owner.isOpen());assert.deepEqual(f.actual,{top:false,fps:false});assert.equal(f.native.size,0);assert.equal(f.timers.size,0)});
  }
  if(side==='after'){
   const pure=m.createNativeHudPreviewOwner(),startCount=count('monitor.start');prefs=m.normalizeMonitorOutput({...prefs,ymcc:{...prefs.ymcc,fields:fields()}});await pure.open(prefs);await drain();
   check('image-only preview creates no native sensor start command',()=>{assert.equal(count('monitor.start'),startCount);assert.deepEqual(f.actual,{top:false,fps:false})});pure.dispose();await drain();
   const waiting=m.createNativeHudPreviewOwner();prefs=m.normalizeMonitorOutput({...prefs,ymcc:{...prefs.ymcc,fields:fields('cpuTemp')}});f.holdRead=true;const opening=waiting.open(prefs);await drain();assert(f.pendingRead);const beforeRead=count('monitor.snapshot');prefs=m.normalizeMonitorOutput({...prefs,ymcc:{...prefs.ymcc,fields:fields('fps')}});waiting.update(prefs);await drain();
   check('field update while initial cache read is pending replaces acquisition without a second read',()=>{assert.deepEqual(f.actual,{top:false,fps:true});assert.equal(count('monitor.snapshot'),beforeRead)});
   const complete=f.pendingRead;f.pendingRead=null;f.holdRead=false;complete({schema:1,available:false,session:1,generation:1,relayEpoch:0,seq:++f.seq,topActive:false,fpsActive:true});await opening;waiting.dispose();await drain();
   check('disposing the preview after a delayed initial cache read releases the latest field demand',()=>assert.deepEqual(f.actual,{top:false,fps:false}));
   let fail=false,leases=0,disposers=0;const directChannel={onmessage:null,postMessage:data=>f.packets.push(structuredClone(data)),close(){}};
   const controlled=m.createHudPreviewOwner({available:()=>true,token:()=> 'preview-demand-failure-token',now:Date.now,monotonicNow:()=>performance.now(),channel:()=>directChannel,
    createChild:async()=>{queueMicrotask(()=>directChannel.onmessage?.({data:{kind:'ymcc-hud-preview-ready',token:'preview-demand-failure-token'}}));return 999},closeChild:async()=>true,
    subscribe:fn=>{fn(null);return()=>{}},subscribeTdpState:fn=>{fn({status:'unknown',watts:null,mode:null,proof:null,revision:0,confirmedAt:null});return()=>{}},readOnce:async()=>null,childClosed:()=>()=>{},setTimer:window.setTimeout,clearTimer:window.clearTimeout,baseUrl:()=>window.location.href,
    acquire(need){if(fail)throw Error('fixture replacement rejected');leases++;const off=m.acquireMonitorDemand('ymcc-failure-preview',need);return()=>{disposers++;off()}}
   });
   const cpu=m.normalizeMonitorOutput({...prefs,ymcc:{...prefs.ymcc,fields:fields('cpuTemp')}}),fps=m.normalizeMonitorOutput({...prefs,ymcc:{...prefs.ymcc,fields:fields('fps')}});await controlled.open(cpu);await drain();fail=true;const packets=f.packets.length;
   check('replacement failure keeps the prior preview demand and appearance',()=>{assert.throws(()=>controlled.update(fps),/fixture replacement rejected/);assert.equal(disposers,0);assert.deepEqual(f.actual,{top:true,fps:false});assert.equal(f.packets.length,packets)});
   fail=false;controlled.update(fps);await drain();check('failed replacement can recover and dispose without a leaked prior lease',()=>{assert.equal(leases,2);assert.equal(disposers,1);assert.deepEqual(f.actual,{top:false,fps:true})});controlled.dispose();await drain();assert.equal(leases,disposers);
   const realCreate=f.createChild;f.createChild=async()=>{throw Error('fixture child creation rejected')};const rejected=m.createNativeHudPreviewOwner();await assert.rejects(rejected.open(cpu),/fixture child creation rejected/);f.createChild=realCreate;rejected.dispose();await drain();
   check('native child creation failure releases the selected lease, passive bindings and channel',()=>{assert.deepEqual(f.actual,{top:false,fps:false});assert.equal(f.channels.size,0);assert.equal(f.timers.size,0);assert([...f.events.values()].every(v=>v.size===0))});

  }
  owner.dispose();await drain();check(side+': final disposer leaves no channels, native mock windows, listeners or owned timers',()=>{assert.equal(f.native.size,0);assert.equal(f.channels.size,0);assert.equal(f.timers.size,0);assert([...f.events.values()].every(s=>s.size===0));assert.deepEqual(f.actual,{top:false,fps:false})});
 }
 const result={ok:true,checks:checks.length,cases:checks,work,realChildWindows:false,hardwareReads:false,sourceSha256:Object.fromEntries(['src/bridge/monitorHudPreview.ts','src/bridge/monitorHudModel.ts','src/bridge/monitorData.ts'].map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex')])),scope:'Frozen/current production native-preview factory and owner, with actual unified singleton hub; synthetic child/window/channel/native IPC edges. No upstream process control or CPU measurement.'};fs.writeFileSync(out+'/PREVIEW-DEMAND-RESULT.json',JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({ok:true,checks:checks.length,work},null,2));
}finally{globalThis.BroadcastChannel=oldBC;if(oldWindow===undefined)delete globalThis.window;else globalThis.window=oldWindow;delete globalThis.__ymccPreviewDemand;delete globalThis.__ymccPreviewCurrent}
