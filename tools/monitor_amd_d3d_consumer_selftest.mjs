// Unified native packet -> YMCC facade -> HUD. Offline fixtures only.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
const root=process.cwd();
const task=path.resolve(process.env.YMCC_AMD_USED_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/amd-d3d-used-source-20261016');
const archive=path.resolve(process.env.YMCC_AMD_ARCHIVE_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/amd-vram-three-machine-20261016');
const {build}=createRequire(root+'/package.json')('esbuild');
const noIO=()=>{throw Error('test attempted external IO or a timer');};
const modules=['monitorData','monitorHudModel','monitorHudFreshness','monitorDisplayFeed'];
async function load(frozen){
 const mocks={'./ipc':'export const isNativeRuntime=false;export const invoke=()=>{throw Error("no IPC")};export const on=()=>()=>{};',
 './api':'export const fs={};export const windowApi={};',
 './monitorAppliedTdp':'export const advanceMonitorTdpDisplay=()=>{};export const subscribeMonitorTdpState=()=>()=>{};'};
 const result=await build({stdin:{resolveDir:root,loader:'ts',contents:modules.map(n=>'export * from '+JSON.stringify('./src/bridge/'+n+'.ts')+';').join('\n')},
 bundle:true,platform:'node',format:'esm',write:false,alias:{'@':path.join(root,'src')},
 plugins:[{name:'frozen-or-current-no-io',setup(b){
  b.onResolve({filter:/.*/},a=>mocks[a.path]?{path:a.path,namespace:'mock'}:undefined);
  b.onLoad({filter:/.*/,namespace:'mock'},a=>({contents:mocks[a.path],loader:'ts'}));
  if(frozen)b.onLoad({filter:/monitor(Data|DisplayFeed)\.ts$/},a=>({contents:fs.readFileSync(task+'/before/src/bridge/'+path.basename(a.path),'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));
 }}]});
 const file=task+'/'+(frozen?'consumer-before':'consumer-after')+'.mjs';
 fs.writeFileSync(file,result.outputFiles[0].text);return import(pathToFileURL(file).href);
}
const before=await load(true),after=await load(false),now=1700000000000,checks=[];
function test(name,fn){fn();checks.push(name);}
const native=JSON.parse(fs.readFileSync(task+'/native/after.json','utf8'));
const beforeNative=JSON.parse(fs.readFileSync(task+'/native/before.json','utf8'));
const view=(m,top,at=now,extra={})=>{
 const hub=m.createMonitorDataHub({now:()=>at,invoke:noIO,readTextFile:noIO,setTimer:noIO,clearTimer(){}});
 const result=hub.admitDisplayPublication({schema:1,available:true,session:1,generation:1,seq:1,collectedAt:at,topActive:true,fpsActive:false,hwDown:false,top:JSON.stringify(top),fps:null,...extra});assert(result);return result;
};
const top=native.cases['local-AMD-original-rows'];assert(top);
test('same-slot dedicated source and used value survive unified parsing',()=>{
 const v=view(after,top);assert.equal(v.top.gpuMemorySource,'hwinfo-d3d-dedicated');assert.equal(v.top.gpuMemoryUsedMb,14.734375);
 const d=after.hudFromUniformSnapshot(v,now);assert.equal(d.values.vramUsed,14.734375/1024);
 for(const key of ['vramTotal','vramAvailable','vramUsage'])assert.equal(d.values[key],null);
});
test('dedicated-used metadata forbids mixing an unrelated capacity or percentage',()=>{
 const v=view(after,{...top,gpuMemoryAvailableMb:7000,gpuMemoryTotalMb:8192,gpuMemoryLoadPct:37});
 assert.equal(v.top.gpuMemoryUsedMb,14.734375);for(const key of ['gpuMemoryAvailableMb','gpuMemoryTotalMb','gpuMemoryLoadPct'])assert.equal(v.top[key],null);
 const d=after.hudFromUniformSnapshot(v,now);assert.equal(after.hudPercentValue(d,'vramUsage'),null);assert(!after.hudMetricAvailable(d,'vramUsageBar'));
});
for(const source of ['unavailable','unknown','windows-shared',null,false,42])test('untrusted source clears all VRAM channels: '+String(source),()=>{
 const v=view(after,{...top,gpuMemorySource:source,gpuMemoryTotalMb:8192,gpuMemoryAvailableMb:8000,gpuMemoryLoadPct:25});
 assert.equal(v.top.gpuMemorySource,'unavailable');for(const key of ['gpuMemoryUsedMb','gpuMemoryAvailableMb','gpuMemoryTotalMb','gpuMemoryLoadPct'])assert.equal(v.top[key],null);
});
for(const selected of [false,undefined,null,1,'true'])test('explicit new source requires selected-card proof: '+String(selected),()=>{
 const v=view(after,{...top,gpuSelectionPresent:selected});assert.equal(v.top.gpuMemorySource,'unavailable');assert.equal(v.top.gpuMemoryUsedMb,null);
});
test('real dedicated zero is retained; missing/invalid values cannot coerce to zero',()=>{
 assert.equal(view(after,{...top,gpuMemoryUsedMb:0}).top.gpuMemoryUsedMb,0);
 for(const value of [null,-1,'12',false,NaN,Infinity])assert.equal(view(after,{...top,gpuMemoryUsedMb:value}).top.gpuMemoryUsedMb,null);
});
test('all corrected native cases survive the sole consumer with same effective values',()=>{
 for(const packet of Object.values(native.cases)){
  const v=view(after,packet);for(const key of ['gpuMemorySource','gpuMemoryUsedMb','gpuMemoryAvailableMb','gpuMemoryTotalMb','gpuMemoryLoadPct'])assert.equal(v.top[key],packet[key]);
 }
});
test('all original no-source native packets preserve exact legacy facade and HUD shape',()=>{
 for(const packet of Object.values(beforeNative.cases)){
  assert(!Object.hasOwn(packet,'gpuMemorySource'));const a=view(before,packet),b=view(after,packet);
  assert.deepEqual(b,a);assert(!Object.hasOwn(b.top,'gpuMemorySource'));assert.deepEqual(after.hudFromUniformSnapshot(b,now),before.hudFromUniformSnapshot(a,now));
 }
});
test('same numeric value with a changed source invalidates the display key',()=>{
 const a=view(after,{...top,gpuMemorySource:'hwinfo'}),b=view(after,top);assert.notEqual(after.monitorSnapshotDisplayKey(a),after.monitorSnapshotDisplayKey(b));
});
test('repeated cached numeric content never renews old hardware timestamps',()=>{
 const d=after.hudFromUniformSnapshot(view(after,top),now),deadline=d.freshUntil.hardware;
 assert(deadline>now);assert.equal(after.ageHudData(d,deadline-1).values.vramUsed,14.734375/1024);
 const expired=after.ageHudData(d,deadline);assert.equal(expired.values.vramUsed,null);assert.equal(d.values.vramUsed,14.734375/1024);
 const repeated=after.hudFromUniformSnapshot(view(after,top,deadline),deadline);assert.equal(repeated.values.vramUsed,undefined); // Whole stale top packet is absent, not renewed.
});
test('hardware-down clears both effective values and their source at the native boundary',()=>{
 const packet=native.cases['hardware-down'];assert(packet);assert.equal(packet.gpuMemorySource,'unavailable');assert.equal(packet.gpuMemoryUsedMb,null);
 const d=after.hudFromUniformSnapshot(view(after,packet),now);assert.equal(d.values.vramUsed,null);
});
const replay={};
for(const machine of ['Win5','ROGXbox2025'])test(machine+' archived top packets remain unchanged; no invented dedicated values or capacity',()=>{
 const rows=fs.readFileSync(archive+'/input/'+machine+'/monitor-detail.log','utf8').trim().split(/\r?\n/).map(JSON.parse);
 for(const row of rows){const project=m=>{const hub=m.createMonitorDataHub({now:()=>row.observedAtMs,invoke:noIO,readTextFile:noIO,setTimer:noIO,clearTimer(){}});const v=hub.admitDisplayPublication(row.snapshot);assert(v);return{view:v,hud:m.hudFromUniformSnapshot(v,row.observedAtMs)}};
  const a=project(before),b=project(after);
  // The earlier frame-hardware proof fix intentionally retires copied GPU /
  // power in legacy RTSS packets. Preserve exact comparison for every other
  // archived field and the entire HUD; this round changes no VRAM values.
  if(a.view.fps?.fpsSource==='rtss'&&a.view.fps.hardwareObservedAt===undefined){
    assert.equal(b.view.fps.gpu,null);assert.equal(b.view.fps.packagePower,-1);
    a.view.fps.gpu=null;a.view.fps.packagePower=-1;
  }
  assert.deepEqual(b,a);assert(!Object.hasOwn(b.view.top,'gpuMemorySource'));assert.equal(b.hud.values.vramUsage,null);
 }replay[machine]=rows.length;
});
const result={ok:true,checks:checks.length,cases:checks,archiveReplay:replay,hardwareReads:false,newTimers:false,capacityOrPercentageInvented:false};
fs.writeFileSync(task+'/CONSUMER-RESULT.json',JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
