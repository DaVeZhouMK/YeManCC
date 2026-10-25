// Actual preview owner logic with memory-only child-window/channel APIs. This does
// NOT create a HWND and must not be reported as native-window pixel acceptance.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild');
const out=path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/templates-tdp-content-20261010');fs.mkdirSync(out,{recursive:true});
const mocks={ './api':'export const windowApi={}', './ipc':'export const isNativeRuntime=false;export function on(){return ()=>{}}', './monitorData':'export function subscribeMonitorTdpState(){return ()=>{}}export function acquireMonitorDemand(){return ()=>{}}export async function readMonitorSnapshot(){return null}export function subscribeMonitorSnapshot(){return ()=>{}}' };
const bundle=await build({stdin:{loader:'ts',resolveDir:root,contents:`export {createHudPreviewOwner,HUD_PREVIEW_MAX_MS} from './src/bridge/monitorHudPreview.ts';export {normalizeMonitorOutput} from './src/bridge/monitorHudModel.ts'`},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'memory-only-native',setup(b){b.onResolve({filter:/.*/},a=>mocks[a.path]&&a.importer.endsWith('monitorHudPreview.ts')?{path:a.path,namespace:'fake'}:null);b.onLoad({filter:/.*/,namespace:'fake'},a=>({contents:mocks[a.path],loader:'ts'}))}}]});
const file=path.join(out,'native-preview-owner-under-test.mjs');fs.writeFileSync(file,bundle.outputFiles[0].text);const m=await import('file:///'+file.replaceAll('\\','/')+'?'+Date.now());const cases=[];
const check=(name,fn)=>{fn();cases.push(name)};
function harness(options={}){
 const calls=[],channels=[],timers=new Map();let listener=null,tdpListener=null,tdpUsers=0,closed=null,now=100000,acquired=0,subscriptions=0,events=0,nextTimer=1,token=0;
 const snapshot={schema:1,session:1,generation:1,seq:1,collectedAt:99990,hwDown:false,top:{ts:99950,tdpW:26,tempC:62,cpuUsage:35,freqMhz:4400,gpuPowerW:105,gpuClockMhz:2300,hasBattery:false,hwDown:false},fps:{ts:99990,pid:77,processCreated:"123456",fps:69,fps1:48,gpu:80}};
 const deps={available:()=>options.available!==false,token:()=> 'preview-owned-token-'+String(++token).padStart(6,'0'),now:()=>now,
  async createChild(args){calls.push(['create',args]);if(options.throwCreate)throw Error('create-failed');if(options.deferCreate)return await options.deferCreate;
   if(options.ack!==false)queueMicrotask(()=>channels.at(-1).onmessage?.({data:{kind:'ymcc-hud-preview-ready',token:'preview-owned-token-'+String(token).padStart(6,'0')}}));return 23},
  async closeChild(id){calls.push(['close',id]);return true},
  channel(name){const c={name,packets:[],onmessage:null,closed:false,postMessage(p){assert.equal(c.closed,false);c.packets.push(structuredClone(p))},close(){c.closed=true}};channels.push(c);return c},
  subscribe(cb){listener=cb;subscriptions++;cb(snapshot);return()=>{listener=null;subscriptions--}},
  subscribeTdpState(cb){tdpListener=cb;tdpUsers++;cb({mode:null,status:'unknown',watts:null,proof:null,revision:0,confirmedAt:null});return()=>{tdpListener=null;tdpUsers--}},
  acquire(){if(options.throwAcquire)throw Error('acquire-failed');acquired++;return()=>{acquired--}},
  async readOnce(){calls.push(['read-once']);if(options.throwRead)throw Error('read-failed');return snapshot},
  childClosed(cb){closed=cb;events++;return()=>{closed=null;events--}},
  setTimer(fn,ms){const id=nextTimer++;timers.set(id,{fn,ms});return id},clearTimer(id){timers.delete(id)},baseUrl:()=> 'https://app.localhost/index.html#/rtss',
 };
 const owner=m.createHudPreviewOwner(deps);return{owner,calls,channels,timers,snapshot,send(value){listener?.(value)},sendTdp(value){tdpListener?.(value)},tdpUsers:()=>tdpUsers,closed(id){closed?.(id)},counts:()=>({acquired,subscriptions,events}),fire(ms){const timer=[...timers].find(([,t])=>t.ms===ms);assert(timer);timers.delete(timer[0]);timer[1].fn()},setNow(v){now=v}};
}
{
 const h=harness({available:false});await assert.rejects(h.owner.open(m.normalizeMonitorOutput()),/没有 YMCC 原生窗口接口/);
 check('No native capability => no browser fallback, child, snapshot read or demand',()=>{assert.equal(h.calls.length,0);assert.equal(h.channels.length,0);assert.equal(h.counts().acquired,0)});
}
{
 const h=harness();const id=await h.owner.open(m.normalizeMonitorOutput());
 check('Own native child is created at dedicated lightweight route with random channel token',()=>{assert.equal(id,23);const args=h.calls[0][1];assert.match(args.url,/#\/monitor\/hud-preview\?channel=preview-owned-token-/);assert(args.title.includes('非 Steam'));assert.equal(args.width,660)});
 check('Only one initial unified snapshot read; no hardware reader or polling API exists',()=>{assert.equal(h.calls.filter(c=>c[0]==='read-once').length,1);assert.equal(h.counts().acquired,1)});
 check('Ready requires child renderer handshake, not just a returned child ID',()=>assert.equal(h.owner.isOpen(),true));
 check('All packets are explicit uniform data and do not use fixture values',()=>{for(const packet of h.channels[0].packets.filter(p=>p.kind==='ymcc-hud-preview')){assert.equal(packet.data.source,'ymcc-uniform');assert.equal(packet.data.values.fps,69);assert.equal(packet.data.values.gpuTemp,null);assert.equal(packet.data.coreUsage.length,0)}});
 const count=h.calls.filter(c=>c[0]==='create').length;const prefs=m.normalizeMonitorOutput({ymcc:{opacity:.35}});h.owner.update(prefs);
 check('Appearance updates reuse child and keep independent transparent background parameters',()=>{assert.equal(h.calls.filter(c=>c[0]==='create').length,count);assert.equal(h.channels[0].packets.at(-1).preferences.ymcc.opacity,.35)});
 const packets=h.channels[0].packets;check('Packet sequence strictly increases',()=>{for(let i=1;i<packets.length;i++)assert(packets[i].seq>packets[i-1].seq)});
 h.setNow(101000);h.send({...h.snapshot,seq:2,fps:{...h.snapshot.fps,fps:71}});check('Passive unified snapshot event updates window; not a second acquisition loop',()=>assert.equal(h.channels[0].packets.at(-1).data.values.fps,71));
 h.closed(9999);check('Other windows close events cannot release this preview',()=>assert(h.owner.isOpen()));
 h.owner.close();
 check('Close releases only owned child/demand/subscriber/event/channel/timers',()=>{assert.deepEqual(h.calls.filter(c=>c[0]==='close'),[['close',23]]);assert.deepEqual(h.counts(),{acquired:0,subscriptions:0,events:0});assert.equal(h.timers.size,0);assert(h.channels[0].closed)});
}
{
 const h=harness({ack:false});const pending=h.owner.open(m.normalizeMonitorOutput());await new Promise(r=>setImmediate(r));assert.equal(h.owner.isOpen(),false);h.fire(15000);await assert.rejects(pending,/握手/);
 check('Missing handshake times out, closes own native descriptor and releases demand',()=>{assert.deepEqual(h.calls.filter(c=>c[0]==='close'),[['close',23]]);assert.equal(h.counts().acquired,0)});
}
{
 const h=harness();await h.owner.open(m.normalizeMonitorOutput());h.fire(m.HUD_PREVIEW_MAX_MS);
 check('Independent five-minute parent deadline cleans resources without app/RTSS close',()=>{assert.equal(h.owner.isOpen(),false);assert.equal(h.counts().acquired,0);assert.deepEqual(h.calls.filter(c=>c[0]==='close'),[['close',23]])});
}
for(const fail of ['throwCreate','throwAcquire','throwRead']){
 const h=harness({[fail]:true});await assert.rejects(h.owner.open(m.normalizeMonitorOutput()));
 check('Own-resource cleanup after '+fail,()=>{assert.deepEqual(h.counts(),{acquired:0,subscriptions:0,events:0});assert.equal(h.timers.size,0);assert.equal(h.owner.isOpen(),false)});
}
{
 let resolve;const deferred=new Promise(r=>resolve=r),h=harness({deferCreate:deferred});const pending=h.owner.open(m.normalizeMonitorOutput());h.owner.dispose();resolve(27);await assert.rejects(pending);
 check('Delayed native create after owner disposal closes exact late child, without orphan or takeover',()=>{assert.deepEqual(h.calls.filter(c=>c[0]==='close'),[['close',27]]);assert.equal(h.counts().acquired,0)});
}
{
 const h=harness();await h.owner.open(m.normalizeMonitorOutput());h.setNow(106000);h.send({...h.snapshot,seq:2});
 check('Aged real snapshot clears numbers, not simulated zero/retained old readings',()=>{const packet=h.channels[0].packets.at(-1);assert(packet.data.stale);assert.equal(packet.data.values.fps,undefined)});h.owner.dispose();
}
{
 const h=harness();await h.owner.open(m.normalizeMonitorOutput());h.setNow(101000);h.send({...h.snapshot,fps:{...h.snapshot.fps,ts:90000}});
 check('Old FPS timestamp cannot expire still-fresh CPU in the preview packet',()=>{const packet=h.channels[0].packets.at(-1);assert.equal(packet.data.values.cpuTemp,62);assert.equal(packet.data.values.fps,undefined);assert(packet.expiresAt>100000)});h.owner.dispose();
}
{
 const h=harness();await h.owner.open(m.normalizeMonitorOutput());
 const before=h.channels[0].packets.filter(p=>p.kind==='ymcc-hud-preview').length,reads=h.calls.filter(c=>c[0]==='read-once').length;
 for(let i=0;i<100;i++)h.owner.update(m.normalizeMonitorOutput({ymcc:{opacity:i/100}}));
 check('100 appearance changes carry no data, no remap/freshness and no additional unified reads',()=>{assert.equal(h.channels[0].packets.filter(p=>p.kind==='ymcc-hud-preview').length,before);assert.equal(h.calls.filter(c=>c[0]==='read-once').length,reads);assert(!('data' in h.channels[0].packets.at(-1)))});
 for(let i=2;i<=201;i++)h.send({...h.snapshot,seq:i});
 check('Burst owner input holds one timer and sends no extra sample before 1000ms',()=>{assert.equal(h.channels[0].packets.filter(p=>p.kind==='ymcc-hud-preview').length,before);assert.equal([...h.timers.values()].filter(t=>t.ms===1000).length,1)});
 h.setNow(101000);h.fire(1000);
 check('Owner coalesces the burst into one data packet at 1Hz',()=>assert.equal(h.channels[0].packets.filter(p=>p.kind==='ymcc-hud-preview').length,before+1));
 h.owner.dispose();check('Disposal cancels the bounded sample timer as well as handshake/lifetime',()=>assert.equal(h.timers.size,0));
}
{
 const h=harness();await h.owner.open(m.normalizeMonitorOutput());
 h.setNow(101000);h.sendTdp({mode:'ac',status:'applied',watts:45,proof:'write-ack',revision:1,confirmedAt:101000});
 check('Native preview receives independent current-side YMCC TDP with no extra snapshot read',()=>{const p=h.channels[0].packets.at(-1);assert.equal(p.data.values.tdpLimit,45);assert.equal(p.data.values.cpuPower,26);assert.equal(h.calls.filter(c=>c[0]==='read-once').length,1);assert.equal(h.tdpUsers(),1)});
 h.sendTdp({mode:'dc',status:'unknown',watts:null,proof:null,revision:2,confirmedAt:null});
 check('Power-side invalidation clears child TDP immediately instead of retaining AC number',()=>assert.equal(h.channels[0].packets.at(-1).data.values.tdpLimit,null));
 h.sendTdp({mode:'dc',status:'applied',watts:18,proof:'write-ack',revision:3,confirmedAt:101000});
 check('Confirmed DC updates share the existing 1Hz packet budget',()=>{assert.equal(h.channels[0].packets.at(-1).data.values.tdpLimit,null);assert.equal([...h.timers.values()].filter(t=>t.ms===1000).length,1)});
 h.setNow(102000);h.fire(1000);assert.equal(h.channels[0].packets.at(-1).data.values.tdpLimit,18);
 h.owner.dispose();check('Owned child cleanup also removes TDP binding and pending timers',()=>{assert.equal(h.tdpUsers(),0);assert.equal(h.timers.size,0)});
}
const app=fs.readFileSync('src/App.vue','utf8'),main=fs.readFileSync('src/main.ts','utf8'),child=fs.readFileSync('src/views/MonitorHudPreviewView.vue','utf8');
check('Preview route skips main shell hardware/gamepad/autostart startup and sidebar preloads',()=>{assert(app.includes("['/button-mapping/shortcuts', '/monitor/hud-preview']"));assert(main.includes("['button-mapping-shortcuts', 'monitor-hud-preview']"))});
check('Child source has no collector, snapshot polling, main-window close or fixture fallback',()=>{assert(!child.includes('makeHudFixture'));assert(!child.includes('readMonitorSnapshot'));assert(!child.includes('acquireMonitorDemand'));assert(!child.includes("'window.close'"));assert(child.includes('windowApi.closeChild(childId)'));assert(child.includes("raw.data?.source !== 'ymcc-uniform'"))});
// Exercise passive subscription through the actual singleton wrapper as well.
globalThis.__previewHubTest={calls:[],now:Date.now(),available:true};
const hubBundle=await build({stdin:{loader:'ts',resolveDir:root,contents:`export {subscribeMonitorSnapshot,readMonitorSnapshot} from './src/bridge/monitorData.ts'`},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'mock-hub-ipc',setup(b){b.onResolve({filter:/^\.\/(ipc|api)$/},a=>a.importer.endsWith('monitorData.ts')?{path:a.path,namespace:'hub-fake'}:null);b.onLoad({filter:/.*/,namespace:'hub-fake'},a=>({loader:'ts',contents:a.path==='./api'?`export const fs={async readTextFile(){throw Error('No legacy-file read in preview subscription test')}}`:`export async function invoke(cmd){const f=globalThis.__previewHubTest;f.calls.push(cmd);if(cmd==='monitor.snapshot')return {schema:1,session:1,relayEpoch:1,generation:1,seq:1,collectedAt:f.now,available:f.available,topActive:false,fpsActive:false,hwDown:false,top:JSON.stringify({ts:f.now,tdpW:21,tempC:58}),fps:JSON.stringify({ts:f.now,fps:77})};if(cmd==='monitor.takeover')return {ok:true,cleaned:true};return {ok:true}}`}))}}]});
const hubFile=path.join(out,'passive-snapshot-under-test.mjs');fs.writeFileSync(hubFile,hubBundle.outputFiles[0].text);const hub=await import('file:///'+hubFile.replaceAll('\\','/')+'?'+Date.now());let observations=[];
const off=hub.subscribeMonitorSnapshot(value=>observations.push(value)),bad=hub.subscribeMonitorSnapshot(()=>{throw Error('isolated display error')});
check('Passive subscription itself issues no native calls or polling',()=>assert.equal(globalThis.__previewHubTest.calls.length,0));
await Promise.all([hub.readMonitorSnapshot(),hub.readMonitorSnapshot()]);
check('Subscribers share one in-flight singleton read and duplicate replies publish once',()=>{assert.equal(globalThis.__previewHubTest.calls.filter(x=>x==='monitor.snapshot').length,1);assert.equal(observations.length,2);assert.equal(observations[1].fps.fps,77)});
const later=[];const laterOff=hub.subscribeMonitorSnapshot(v=>later.push(v));check('Late subscriber receives existing snapshot without another native read',()=>{assert.equal(later.length,1);assert.equal(later[0].fps.fps,77)});
globalThis.__previewHubTest.available=false;await hub.readMonitorSnapshot();check('Unavailable snapshot publishes null instead of preserving old data',()=>assert.equal(observations.at(-1),null));off();bad();laterOff();check('Unsubscription does not stop another consumer/collector',()=>assert.equal(globalThis.__previewHubTest.calls.filter(x=>x==='monitor.start'||x==='monitor.stop').length,0));delete globalThis.__previewHubTest;
const report={suite:'YMCC native desktop preview ownership',passed:cases.length,cases,actualNativeWindowCreated:false,actualPixelAcceptance:false,scope:'Production owner with inert HWND/channel/demand APIs and static child admission checks; no installed app or desktop control'};fs.writeFileSync(path.join(out,'NATIVE-PREVIEW-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));