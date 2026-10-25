// Browser timer callbacks deliberately NEVER fired. The production display
// gate, cache admission and both carrier producers must progress from native ticks.
// No CPU sampling, hardware, external process, game or installed runtime touched.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild'),out=path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/desktop-output-20261011');
const b=await build({stdin:{resolveDir:root,loader:'ts',contents:`export * from './src/bridge/monitorDisplayFeed.ts';export * from './src/bridge/monitorAppliedTdp.ts';export * from './src/bridge/steamHudRelay.ts';export * from './src/bridge/desktopHudOwner.ts';export * from './src/bridge/monitorHudModel.ts';export {createMonitorDataHub} from './src/bridge/monitorData.ts'`},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'inert-native-for-cache-admission',setup(b){b.onResolve({filter:/^\.\/(ipc|api)$/},a=>a.importer.endsWith('monitorData.ts')?{path:a.path,namespace:'fake'}:null);b.onLoad({filter:/.*/,namespace:'fake'},a=>({loader:'ts',contents:a.path==='./ipc'?'export async function invoke(){throw Error("Native I/O forbidden")}':'export const fs={async readTextFile(){throw Error("File I/O forbidden")}}'}));}}]});
const file=path.join(out,'background-clock-under-test.mjs');fs.writeFileSync(file,b.outputFiles[0].text);const m=await import('file:///'+file.replaceAll('\\','/')+'?'+Date.now());const cases=[],check=(n,f)=>{f();cases.push(n)},flush=async()=>{for(let i=0;i<20;i++)await Promise.resolve();};
{
 let now=0;const timers=new Map(),values=[];let id=0;const f=m.createLatestDisplayFeed(0,{now:()=>now,key:String,setTimer:(fn,ms)=>{timers.set(++id,{fn,ms});return id},clearTimer:id=>timers.delete(id)});const off=f.subscribe(v=>values.push(v));f.push(1);now=1000;f.advance();
 check('Latest display gate flushes on a native heartbeat even if the browser callback is never executed',()=>{assert.deepEqual(values,[0,1]);assert.equal(timers.size,0)});
 f.push(2);now=1500;f.advance();assert.deepEqual(values,[0,1]);now=2000;f.advance();check('Native heartbeat does not bypass the 1000ms latest-only budget',()=>assert.deepEqual(values,[0,1,2]));off();
}
{
 const calls=[];const hub=m.createMonitorDataHub({invoke:async cmd=>{calls.push(cmd);throw Error('No IO')},readTextFile:async()=>{throw Error('No files')},now:()=>10000});
 const raw={schema:1,session:1,relayEpoch:1,generation:2,seq:4,available:true,collectedAt:9900,heartbeatAt:9900,hwinfoOkAt:9900,hwDown:false,topActive:true,fpsActive:true,top:JSON.stringify({ts:9900,tdpW:28.4,tempC:65,freqMhz:4300}),fps:JSON.stringify({ts:9900,fps:123,fps1:88,gpu:79})};
 const view=hub.admitDisplayPublication(raw);check('Native cache publication uses existing uniform parser without a snapshot request, files or hardware calls',()=>{assert.equal(view.top.tdpW,28.4);assert.equal(view.fps.fps,123);assert.deepEqual(calls,[])});
 assert.equal(hub.admitDisplayPublication({...raw,seq:3}),undefined);assert.equal(hub.admitDisplayPublication({...raw,schema:999}),undefined);
 check('Out-of-order/unknown cache publication is ignored, not substituted or treated as health down',()=>assert.deepEqual(calls,[]));
 assert.equal(hub.admitDisplayPublication({...raw,seq:5,available:false}),null);check('Authoritative current native unavailable state clears the display',()=>assert.deepEqual(calls,[]));
}
{
 let now=10000,acquired=0;const frames=[],timers=new Map();let id=0;
 const game={appId:1091500,gameId:'1091500',browserGameId:'1091500',overlayTitle:'SP Overlay: 77/88/3',overlayPid:77,overlayBrowserId:88,ownerBrowserId:99,overlayBrowserType:3,overlayNativeAppId:1091500};
 const peer={connection:'1',runId:'a'.repeat(32),connected:true};
 const relay=m.createSteamHudRelay({externalClock:true,now:()=>now,session:()=> 'background-steam-session-00001',reply:async(c,frame)=>{if(frame.type==='hud')frames.push(frame);return true},sample:()=>m.hudFromUniformSnapshot(null,now),acquire:()=>{acquired++;return()=>acquired--},setTimer:(fn,ms)=>{timers.set(++id,{fn,ms});return id},clearTimer:id=>timers.delete(id)});
 relay.connected(peer);relay.request({...peer,request:{command:'hud.status',runId:peer.runId,status:{capable:true,game,shown:false,hiddenConfirmed:true,session:null,operation:0,reason:''}}});
 const p=relay.setVisible(true,m.normalizeMonitorOutput());await flush();const f=frames[0];relay.request({...peer,request:{command:'hud.ack',runId:peer.runId,ok:true,status:{capable:true,game,shown:true,hiddenConfirmed:false,session:f.session,operation:f.operation,reason:''}}});assert((await p).confirmed);
 check('Steady Steam display uses no browser refresh timer in native-clock mode',()=>assert.equal(timers.size,0));
 for(let i=0;i<10;i++){now+=1000;relay.advance();await flush();}
 check('Steam leases/data keep refreshing while every browser timer callback is suppressed',()=>{assert.equal(frames.length,11);assert.equal(acquired,1);assert.equal(timers.size,0)});relay.dispose();
}
{
 let now=10000,listener,acquired=0,nextTimer=0;const timers=new Map(),frames=[];
 const owner=m.createDesktopHudOwner({externalClock:true,target:()=>({pid:100,processCreated:'123456789'}),nonce:()=> 'background-native-session-00001',now:()=>now,sample:()=>m.hudFromUniformSnapshot(null,now),acquire:()=>{acquired++;return()=>acquired--},observe:cb=>{listener=cb;return()=>{}},setTimer:(fn,ms)=>{timers.set(++nextTimer,{fn,ms});return nextTimer},clearTimer:id=>timers.delete(id),invoke:async(cmd,args)=>{
 if(cmd==='desktopHud.status')return {available:true,id:0};if(cmd==='desktopHud.create'){queueMicrotask(()=>listener('desktopHud.ready',{id:1,nonce:args.nonce}));return {id:1,nonce:args.nonce}}if(cmd==='desktopHud.frame'){frames.push(args.frame);queueMicrotask(()=>listener('desktopHud.painted',{id:1,nonce:args.nonce,seq:args.frame.seq}));return true}if(cmd==='desktopHud.close')return true;throw Error('Unexpected '+cmd)}});
 assert(await owner.available());assert((await owner.setVisible(true,m.normalizeMonitorOutput())).confirmed);
 for(let i=0;i<10;i++){now+=1000;owner.advance();await flush();}
 check('Desktop data/deadman refresh uses native ticks, with no browser refresh timer and one shared demand',()=>{assert.equal(frames.length,11);assert.equal(acquired,1);assert.equal(timers.size,0)});await owner.dispose();assert.equal(acquired,0);
}
const native=fs.readFileSync('native/main.cpp','utf8');const start=native.indexOf('static void monitorHudPulseTick()'),end=native.indexOf('static ymcc::desktophud::Carrier',start),clock=native.slice(start,end);
check('Native display clock reads ONLY cached monitorSnapshotServeFn and has no sensor/process enumeration API',()=>{assert(clock.includes('monitorSnapshotServeFn()'));assert(!/monitorReadHWiNFO|CreateToolhelp32Snapshot|ShellExecute|CreateProcess/.test(clock));assert(clock.includes('KillTimer'));assert(clock.includes('g_monitorHudPulseDeadline'))});
const entry=fs.readFileSync('src/monitorHudOutputEntry.ts','utf8');check('Dedicated HUD document has no main App/router/scheduler/hardware acquisition import',()=>{
 const imports=Array.from(entry.matchAll(/\bfrom\s+['"]([^'"]+)['"]/g),match=>match[1]);
 assert(imports.length>0);assert(!imports.some(module=>/(?:^|\/)(?:App(?:\.vue)?|router|scheduler|monitorData|yeman)(?:[.\/]|$)/.test(module)));
});
const report={passed:cases.length,cases,scope:'Exact production admission/feed/carrier functions with inert native calls and NO executed browser timer callbacks. Native cache-only clock statically checked. Actual minimized main-WebView integration still requires runtime acceptance; no CPU measurement.'};fs.writeFileSync(path.join(out,'BACKGROUND-CLOCK-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
