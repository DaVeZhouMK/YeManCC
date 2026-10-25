// Unified native packet -> YMCC facade -> HUD. Offline fixtures only.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
const root=process.cwd();
const task=path.resolve(process.env.YMCC_AMD_CSV_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/d3d-sparse-groups-20261016');
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
const view=(m,top,at=now)=>{const hub=m.createMonitorDataHub({now:()=>at,invoke:noIO,readTextFile:noIO,setTimer:noIO,clearTimer(){}});const v=hub.admitDisplayPublication({schema:1,available:true,session:1,generation:1,seq:1,collectedAt:at,topActive:true,fpsActive:false,hwDown:false,top:JSON.stringify(top),fps:null});assert(v);return v;};
for(const [name,packet]of Object.entries(native.examples))test(name+' source and values survive unified consumption',()=>{const v=view(after,packet);for(const key of ['gpuMemorySource','gpuMemoryUsedMb','gpuMemoryTotalMb','gpuMemoryAvailableMb','gpuMemoryLoadPct'])assert.equal(v.top[key],packet[key]);const d=after.hudFromUniformSnapshot(v,now);assert.equal(d.values.vramUsed,packet.gpuMemoryUsedMb===null?null:packet.gpuMemoryUsedMb/1024);});
const top=native.examples['csv-1'];
for(const source of ['hwinfo-d3d-dedicated','hwinfo-d3d-shared']){
 test(source+' forbids mixing unrelated capacity or percentage',()=>{const v=view(after,{...top,gpuMemorySource:source,gpuMemoryTotalMb:8192,gpuMemoryAvailableMb:7000,gpuMemoryLoadPct:35});assert.equal(v.top.gpuMemoryUsedMb,906);for(const k of ['gpuMemoryTotalMb','gpuMemoryAvailableMb','gpuMemoryLoadPct'])assert.equal(v.top[k],null);});
 test(source+' requires a selected GPU and valid numeric value',()=>{for(const selected of [undefined,false,null,1,'true']){const v=view(after,{...top,gpuMemorySource:source,gpuSelectionPresent:selected});assert.equal(v.top.gpuMemorySource,'unavailable');assert.equal(v.top.gpuMemoryUsedMb,null);}for(const n of [-1,null,NaN,Infinity,'0',false])assert.equal(view(after,{...top,gpuMemorySource:source,gpuMemoryUsedMb:n}).top.gpuMemoryUsedMb,null);assert.equal(view(after,{...top,gpuMemorySource:source,gpuMemoryUsedMb:0}).top.gpuMemoryUsedMb,0);});
 test(source+' cannot renew an old producer observation',()=>{const d=after.hudFromUniformSnapshot(view(after,{...top,gpuMemorySource:source}),now),end=d.freshUntil.hardware;assert(end>now);assert.equal(after.ageHudData(d,end).values.vramUsed,null);assert.equal(d.values.vramUsed,906/1024);const stale=after.hudFromUniformSnapshot(view(after,{...top,gpuMemorySource:source},end),end);assert.equal(stale.values.vramUsed,undefined);});
}
test('same amount with a source transition invalidates the display key',()=>{const a=view(after,{...top,gpuMemorySource:'hwinfo-d3d-dedicated'}),b=view(after,{...top,gpuMemorySource:'hwinfo-d3d-shared'});assert.notEqual(after.monitorSnapshotDisplayKey(a),after.monitorSnapshotDisplayKey(b));});
test('untrusted new sources cannot fabricate a VRAM reading',()=>{for(const source of ['unavailable','unknown',null,false,42]){const v=view(after,{...top,gpuMemorySource:source});assert.equal(v.top.gpuMemoryUsedMb,null);assert.equal(v.top.gpuMemorySource,'unavailable');}});
const replay={};for(const machine of ['Win5','ROGXbox2025'])test(machine+' legacy archive packets remain exactly compatible',()=>{const rows=fs.readFileSync(archive+'/input/'+machine+'/monitor-detail.log','utf8').trim().split(/\r?\n/).map(JSON.parse);for(const row of rows){const project=m=>{const hub=m.createMonitorDataHub({now:()=>row.observedAtMs,invoke:noIO,readTextFile:noIO,setTimer:noIO,clearTimer(){}});const v=hub.admitDisplayPublication(row.snapshot);assert(v);return{view:v,hud:m.hudFromUniformSnapshot(v,row.observedAtMs)}};assert.deepEqual(project(after),project(before));}replay[machine]=rows.length;});
const result={ok:true,checks:checks.length,cases:checks,archiveReplay:replay,hardwareReads:false,newTimers:false,percentageInvented:false};fs.writeFileSync(task+'/CONSUMER-RESULT.json',JSON.stringify(result,null,2));console.log(JSON.stringify({ok:true,checks:checks.length,archiveReplay:replay}));
