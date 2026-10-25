// Actual unified hub, model and both HUD protocols. No sensor IO or timers.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';

const root=process.cwd();
const out=path.resolve(process.env.YMCC_PRODUCER_TIME_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/producer-time-bound-20261016');
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
const before=JSON.parse(fs.readFileSync(path.join(out,'before/RESULT.json'),'utf8')).results;
const after=JSON.parse(fs.readFileSync(path.join(out,'after/RESULT.json'),'utf8')).results;
const checks=[],now=1700000000000;
const noIO=()=>{throw Error('Unexpected sensor IO or timer')};
function model(top,at=now){
  const hub=m.createMonitorDataHub({now:()=>at,invoke:noIO,readTextFile:noIO,setTimer:noIO,clearTimer(){}});
  const view=hub.admitDisplayPublication({schema:1,available:true,session:1,generation:1,seq:1,collectedAt:at,topActive:true,fpsActive:false,hwDown:top.hwDown,top:JSON.stringify(top),fps:null});
  assert(view);return m.hudFromUniformSnapshot(view,at,null,false);
}
function check(name,fn){fn();checks.push(name)}
const preferences=m.normalizeMonitorOutput({ymcc:{fields:m.hudDataPresetFields('detailed')}});
for(const [name,result]of Object.entries(after)){
  if(!name.startsWith('invalid-')&&name!=='overflow-wraps-near-now')check(name+' preserves complete ordinary display data',()=>assert.deepEqual(model(before[name].top),model(result.top)));
  else check(name+' cannot refresh invalid hardware readings',()=>{const data=model(result.top);assert.equal(data.values.cpuPower??null,null);assert.equal(data.values.cpuTemp??null,null);assert.equal(result.top.ts,1699999999000);assert.equal(result.top.hwDown,true)});
  check(name+' preserves fresh independently collected Windows CPU data',()=>{const data=model(result.top);assert.equal(data.values.cpuUsage,25);assert.deepEqual(data.coreUsage,[20,30])});
  check(name+' preserves both child protocol validations',()=>{
    const data=model(result.top);
    const desktop=m.admitDesktopHudFrame({type:'desktop-hud',protocol:1,hudId:1,nonce:'producer-time-protocol-token',seq:1,expiresAt:now+6000,show:true,preferences,data},1,'producer-time-protocol-token',now,0);
    const other=m.admitHudFrame({type:'hud',protocol:1,runId:'b'.repeat(32),session:'producer-time-protocol-token',seq:1,operation:1,expiresAt:now+6000,show:false,game:null,preferences,data},'b'.repeat(32),now,0);
    assert(desktop&&other);assert.deepEqual(desktop.data,m.boundedHudData(data));assert.deepEqual(other.data,m.boundedHudData(data));
  });
}
check('frozen overflow falsely exposed data through the actual hub/model',()=>{const data=model(before['overflow-wraps-near-now'].top);assert.equal(data.values.cpuPower,30);assert.equal(data.values.cpuTemp,65)});
check('valid data recovers after the bad source is rejected',()=>{const data=model(after.current.top);assert.equal(data.values.cpuPower,30);assert.equal(data.values.cpuTemp,65)});
const result={ok:true,checks:checks.length,cases:checks,hardwareReads:false,newTimers:false,actualUnifiedHubAndProtocols:true,sourceSha256:Object.fromEntries(sources.map(n=>[n,crypto.createHash('sha256').update(fs.readFileSync(path.join(root,'src/bridge',n+'.ts'))).digest('hex')]))};
fs.writeFileSync(path.join(out,'TIME-CONSUMER-RESULT.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({ok:true,checks:checks.length},null,2));
