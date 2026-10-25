// Actual unified hub, model and both HUD protocols. No sensor IO or timers.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';

const root=process.cwd();
const out=path.resolve(process.env.YMCC_HWINFO_PD_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-pd-budget-20261016');
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const sources=['monitorData','monitorHudModel','monitorHudFreshness','desktopHudProtocol','monitorHudProtocol'];
const mocks={
  './ipc':'export const isNativeRuntime=false;export const invoke=()=>{throw Error("Unexpected IPC")};export const on=()=>()=>{};',
  './api':'export const fs={};export const windowApi={};',
  './monitorAppliedTdp':'export const advanceMonitorTdpDisplay=()=>{};export const subscribeMonitorTdpState=()=>()=>{};'
};
const bundle=await build({stdin:{resolveDir:root,loader:'ts',contents:sources.map(n=>'export * from '+JSON.stringify('./src/bridge/'+n+'.ts')+';').join('\n')},bundle:true,platform:'node',format:'esm',write:false,alias:{'@':path.join(root,'src')},plugins:[{name:'no-io',setup(b){
  b.onResolve({filter:/.*/},a=>mocks[a.path]?{path:a.path,namespace:'mock'}:undefined);
  b.onLoad({filter:/.*/,namespace:'mock'},a=>({contents:mocks[a.path],loader:'ts'}));
}}]});
const modulePath=path.join(out,'consumer.mjs');
fs.writeFileSync(modulePath,bundle.outputFiles[0].text);
const m=await import(pathToFileURL(modulePath).href);
const raw=JSON.parse(fs.readFileSync(path.join(out,'PD-DATA-EQUIVALENCE-RESULT.json'),'utf8'));
const checks=[],now=1700000000000;
const noIO=()=>{throw Error('Unexpected sensor IO or timer')};
function model(top,at=now){
  const hub=m.createMonitorDataHub({now:()=>at,invoke:noIO,readTextFile:noIO,setTimer:noIO,clearTimer(){}});
  const view=hub.admitDisplayPublication({schema:1,available:true,session:1,generation:1,seq:1,collectedAt:at,topActive:true,fpsActive:false,hwDown:top.hwDown,top:JSON.stringify(top),fps:null});
  assert(view);return m.hudFromUniformSnapshot(view,at,null,false);
}
function check(name,fn){fn();checks.push(name)}
const preferences=m.normalizeMonitorOutput({ymcc:{fields:m.hudDataPresetFields('detailed')}});
for(const [name,packet]of Object.entries(raw.packets)){
  check(name+' preserves every model value through the sole unified hub',()=>assert.deepEqual(model(packet.before),model(packet.after)));
  check(name+' preserves both output protocols',()=>{
    const data=model(packet.after);
    const desktop=m.admitDesktopHudFrame({type:'desktop-hud',protocol:1,hudId:1,nonce:'unused-pd-protocol-token',seq:1,expiresAt:now+6000,show:true,preferences,data},1,'unused-pd-protocol-token',now,0);
    const other=m.admitHudFrame({type:'hud',protocol:1,runId:'b'.repeat(32),session:'unused-pd-protocol-token',seq:1,operation:1,expiresAt:now+6000,show:false,game:null,preferences,data},'b'.repeat(32),now,0);
    assert(desktop&&other);assert.deepEqual(desktop.data,m.boundedHudData(data));assert.deepEqual(other.data,m.boundedHudData(data));
  });
}
check('NVIDIA ordinary readings remain visible at their actual values',()=>{
  const d=model(raw.packets['all-port-families-and-instances'].after);
  for(const [key,value]of Object.entries({gpuClock:2.475,gpuPower:82,gpuTemp:65,gpuUsage:95,ramUsed:8,ramUsage:25,netDown:12,cpuPower:30,diskRead:123.5,diskWrite:45.25,batteryPower:-42,batteryTime:60,batteryPercent:50}))assert.equal(d.values[key],value,key);
});
check('USB-C observer removal cannot refresh expired ordinary readings',()=>{
  const d=model(raw.packets['all-port-families-and-instances'].after),end=d.freshUntil.hardware;
  assert(end>now);const aged=m.ageHudData(d,end);
  for(const key of ['gpuPower','gpuClock','gpuTemp','gpuUsage','gpuVoltage','ramUsed','ramUsage'])assert.equal(aged.values[key],null,key);
});
check('preserved FileSystem readings still expire at their own original deadline',()=>{const d=model(raw.packets['all-port-families-and-instances'].after),end=d.freshUntil.fileSystem;assert(end>now);const expired=m.ageHudData(d,end);assert.equal(expired.values.diskRead,null);assert.equal(expired.values.diskWrite,null)});
check('port observer removal does not renew old battery source proofs',()=>{const d=model(raw.packets['all-port-families-and-instances'].after),expired=m.ageHudData(d,now+6000);assert.equal(expired.values.batteryPower,null);assert.equal(expired.values.batteryTime,null);assert.equal(expired.values.batteryPercent,null)});
const result={ok:true,checks:checks.length,cases:checks,hardwareReads:false,newTimers:false,actualUnifiedHubAndProtocols:true,sourceSha256:Object.fromEntries(sources.map(n=>[n,crypto.createHash('sha256').update(fs.readFileSync(path.join(root,'src/bridge',n+'.ts'))).digest('hex')]))};
fs.writeFileSync(path.join(out,'PD-CONSUMER-RESULT.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({ok:true,checks:checks.length},null,2));
