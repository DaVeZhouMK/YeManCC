// Current-contract production hub/core/output-entry comparison with inert dependencies.
// The 2026-10-14 snapshot is preserved historically; this baseline is frozen
// before owned-array reuse, so battery and process-identity policies agree.
// Work counters only: no installed app, hardware, Steam/game or CPU sampling.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {createServer} from 'node:http';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild');
const task=path.resolve(process.env.YMCC_MONITOR_HOTPATH_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-owned-arrays-20261016'),baseline=path.resolve(process.env.YMCC_MONITOR_HOTPATH_BASELINE||path.join(task,'before')),out=path.join(task,'validation');fs.mkdirSync(out,{recursive:true});
const blank=()=>({topDecodes:0,fpsDecodes:0,layoutSignatures:0});globalThis.__monitorHotOps={before:blank(),after:blank()};
const instrumentation=side=>({name:'offline-hotpaths-'+side,setup(b){
 b.onResolve({filter:/^(?:\.{1,2}\/|@\/)/},args=>{
  if(['./ipc','./api'].includes(args.path))return {path:args.path,namespace:'inert-core'};
  let p=args.path.startsWith('@/')?path.join(side==='before'?baseline:root,'src',args.path.slice(2)):path.resolve(args.resolveDir,args.path);
  const resolve=v=>[v,v+'.ts',v+'.vue',v+'.mjs',path.join(v,'index.ts')].find(f=>fs.existsSync(f)&&fs.statSync(f).isFile());
  if(side==='before'&&p.startsWith(baseline)&&!resolve(p))p=path.join(root,path.relative(baseline,p));
  const file=resolve(p);return file?{path:file}:undefined;
 });
 b.onLoad({filter:/.*/,namespace:'inert-core'},args=>({contents:args.path==='./ipc'?'export const isNativeRuntime=false;export async function invoke(){throw Error("Unexpected runtime IO")};export function on(){return()=>{}}':'export const fs={};export const windowApi={};export const shell={};',loader:'ts'}));
 b.onLoad({filter:/monitorData\.ts$/},args=>{
  let text=fs.readFileSync(args.path,'utf8');
  text=text.replace('function parseTopPayload(raw: string): TopMonData | null {',`function parseTopPayload(raw: string): TopMonData | null { globalThis.__monitorHotOps.${side}.topDecodes++;`);
  text=text.replace('function parseFpsPayload(raw: string): MonitorFpsPayload | null {',`function parseFpsPayload(raw: string): MonitorFpsPayload | null { globalThis.__monitorHotOps.${side}.fpsDecodes++;`);
  return {contents:text,loader:'ts',resolveDir:path.dirname(args.path)};
 });
}});
async function pure(side){const base=side==='before'?baseline:root;
 const names=['monitorData','monitorCoreTiles','monitorHudModel'];if(side==='after')names.push('monitorHudTransportLease','monitorPayloadDecoder');
 const entry=names.map(n=>`export * from ${JSON.stringify(path.join(base,'src/bridge/'+n+'.ts'))};`).join('\n');
 const bundle=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'node',format:'esm',plugins:[instrumentation(side)]});return import('data:text/javascript;base64,'+Buffer.from(bundle.outputFiles[0].text).toString('base64'));
}
const before=await pure('before'),after=await pure('after'),checks=[],operations={};
const drain=async()=>{for(let i=0;i<20;i++)await Promise.resolve()};
function harness(mod){let now=100000,response=null;const calls=[],files=new Map(),timers=new Map();let id=0;
 const hub=mod.createMonitorDataHub({now:()=>now,invoke:async(cmd,args)=>{calls.push([cmd,args]);if(cmd==='monitor.snapshot'){if(response instanceof Error)throw response;return response;}return {ok:true,cleaned:true,session:1,epoch:1};},readTextFile:async p=>files.get(p)??null,setTimer:(fn,ms)=>{timers.set(++id,{fn,at:now+ms});return id},clearTimer:id=>timers.delete(id)});
 return {hub,calls,files,timers,setNow:t=>now=t,respond:r=>response=r,now:()=>now};
}
const top=(ts=100000)=>({ts,tdpW:30,freqMhz:4200,tempC:75,cpuPowerValid:true,cpuClockValid:true,cpuTempValid:true,cpuUsage:44,ac:0,hasBattery:true,batteryPercent:76,batteryStatusTs:ts,chargeW:-40,batteryPowerValid:true,batteryPowerTs:ts,batteryPowerSource:'hwinfo',batteryPowerEstimated:false,remainMin:90,batteryTimeValid:true,batteryTimeTs:ts,batteryTimeSource:'energy-power',coreUsage:Array.from({length:128},(_,i)=>i%101),coreLabels:Array.from({length:128},(_,i)=>'CPU '+i),coreUsageTs:ts,gpuName:'GPU selected',gpuSelectionPresent:true,gpuSelectionPinned:true,gpuPowerW:110,gpuUsagePct:90,gpuTempC:68,gpuClockMhz:2400,physicalMemoryUsedMb:18000,physicalMemoryAvailableMb:14000,physicalMemoryLoadPct:56.25,gpuMemoryUsedMb:7000,gpuMemoryAvailableMb:9000,gpuMemoryLoadPct:43.75,fanEnabled:true,fanSpeedValid:true,fanSpeedPercent:76,fanSpeedTs:ts,fanSpeedSource:'ymcc-fan-target',fileReadMBps:3,fileWriteMBps:1,fileSystemTs:ts,fileSystemSource:'windows-filesystem-disk-activity',fpsLimit:120,fpsLimitValid:true,fpsLimitTs:ts,fpsLimitSource:'rtss-global',hwDown:false});
const fps=(ts=100000)=>({ts,fps:117,fps1:89,pid:2077,processCreated:'123456789',game:'Fixture',gpu:90,packagePower:30,fpsSource:'rtss',fpsObservedAt:ts,fpsValidUntil:ts+5000,fpsPollPeriodMs:0,frametime:8,frameTimeSource:'rtss-current-frame',frameTimeTs:ts,frameTimeValidUntil:ts+5000,frameTimePollPeriodMs:0,frameTimes:[8,9,8],fps1Source:'hwinfo-presented-1percent-low',fps1ObservedAt:Math.floor((ts)/1000)*1000,fps1ValidUntil:Math.floor((ts)/1000)*1000+5000,fps1PollPeriodMs:1000});
const publication=(t,f,n=1,extra={})=>({schema:1,available:true,session:1,relayEpoch:1,generation:1,seq:n,collectedAt:100000,heartbeatAt:100000,hwinfoOkAt:100000,top:t===null?null:typeof t==='string'?t:JSON.stringify(t),fps:f===null?null:typeof f==='string'?f:JSON.stringify(f),topActive:true,fpsActive:true,hwDown:false,...extra});
let left=harness(before),right=harness(after);
globalThis.__monitorHotOps={before:blank(),after:blank()};
for(let i=0;i<200;i++){const packet=publication(top(),fps(),i+1);const a=left.hub.admitDisplayPublication(packet),b=right.hub.admitDisplayPublication(packet);assert.deepEqual(b,a);if(i===0){for(const view of [a,b]){view.top.tdpW=999;for(const [array,value]of [[view.top.coreUsage,999],[view.top.coreLabels,'poison'],[view.fps.frameTimes,999]]){assert(Object.isFrozen(array));assert.throws(()=>{array[0]=value;},TypeError);}}}}
operations.unchangedPayloads=structuredClone(globalThis.__monitorHotOps);assert.equal(operations.unchangedPayloads.before.topDecodes,1);assert.equal(operations.unchangedPayloads.before.fpsDecodes,1);assert.equal(operations.unchangedPayloads.after.topDecodes,1);assert.equal(operations.unchangedPayloads.after.fpsDecodes,1);
checks.push('200 actual hub publications match baseline; unchanged payloads parse once; owned scalar changes and rejected immutable-array writes cannot contaminate cache');
left=harness(before);right=harness(after);globalThis.__monitorHotOps={before:blank(),after:blank()};
for(let i=0;i<200;i++){const tick=100000+i*200;left.setNow(tick);right.setNow(tick);const packet=publication(top(100000+Math.floor(i/5)*1000),fps(tick),i+1,{collectedAt:tick});assert.deepEqual(right.hub.admitDisplayPublication(packet),left.hub.admitDisplayPublication(packet));}
operations.fastCurvePackets=structuredClone(globalThis.__monitorHotOps);assert.equal(operations.fastCurvePackets.after.topDecodes,40);assert.equal(operations.fastCurvePackets.after.fpsDecodes,200);
checks.push('200ms changing FPS and 1Hz hardware: all actual readings match, hardware parses 40 rather than 200 times');
left=harness(before);right=harness(after);let n=0;let seed=0x67a143;const random=()=>{seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;return seed>>>0};
for(let i=0;i<600;i++){
 const t=top(),f=fps();const bad=[null,'',false,'invalid',Infinity,-1,101,{},[],undefined][random()%10];
 const key=['batteryPercent','batteryPowerValid','gpuSelectionPresent','fanSpeedPercent','coreUsageTs','fileReadMBps','cpuVoltageV'][random()%7];t[key]=bad;
 if(i%5===0)f.fpsValidUntil={unknown:i};if(i%7===0)f.fps1Source={untrusted:['x']};
 const packet=publication(i%19===0?'bad-json':t,i%17===0?'[]':f,++n);
 for(let repeat=0;repeat<3;repeat++){const a=left.hub.admitDisplayPublication(packet),b=right.hub.admitDisplayPublication(packet);assert.deepEqual(b,a);if(a&&b){assert.deepEqual(after.hudFromUniformSnapshot(b,100000),before.hudFromUniformSnapshot(a,100000));if(b.fps&&typeof b.fps.fpsValidUntil==='object'&&b.fps.fpsValidUntil){a.fps.fpsValidUntil.poison=b.fps.fpsValidUntil.poison=true;}}}
}
for(const change of [{session:2,seq:1},{session:1,seq:9999},{session:2,seq:0},{session:2,generation:0,seq:9999},{session:3,seq:1}]){const packet=publication(top(),fps(),1,change);assert.deepEqual(right.hub.admitDisplayPublication(packet),left.hub.admitDisplayPublication(packet));assert.equal(right.hub.session(),left.hub.session());}
checks.push('1800 malformed/legacy/nested-metadata admissions and session/generation changes preserve complete parsed/model output');
left=harness(before);right=harness(after);const oldPacket=publication(top(),fps());assert.deepEqual(right.hub.admitDisplayPublication(oldPacket),left.hub.admitDisplayPublication(oldPacket));
const stopA=left.hub.acquire('floor',{top:true,fps:true}),stopB=right.hub.acquire('floor',{top:true,fps:true});await drain();stopA();stopB();await drain();
for(let i=0;i<20;i++){const packet={...oldPacket,seq:i+2};assert.deepEqual(right.hub.admitDisplayPublication(packet),left.hub.admitDisplayPublication(packet));}
const nextSession={...oldPacket,session:2,seq:1};const restored=right.hub.admitDisplayPublication(nextSession);assert.deepEqual(restored,left.hub.admitDisplayPublication(nextSession));assert(restored.top);assert.equal(restored.top.coreUsage[0],0);
checks.push('stop floors clear private output copies; identical bytes in a new native session restore the correct original data');
for(const mode of ['snapshot','files']){
 left=harness(before);right=harness(after);const releaseLeft=left.hub.acquire('read',{top:true,fps:true}),releaseRight=right.hub.acquire('read',{top:true,fps:true});await drain();const packet=publication(top(),fps());for(const h of [left,right]){
  h.respond(mode==='files'?new Error('unknown:monitor.snapshot'):packet);h.files.set('C:\\fixture\\topmon.json',packet.top);h.files.set('C:\\fixture\\fps-status.json',packet.fps);
 }
 // Explicit PC_DIR seam is not needed for the file fixture: path suffix lookup.
 if(mode==='files')for(const h of [left,right]){const saved=h.files;h.files.get=p=>p.endsWith('topmon.json')?packet.top:p.endsWith('fps-status.json')?packet.fps:null;}
 for(let i=0;i<10;i++){const a=await left.hub.read(),b=await right.hub.read();assert.deepEqual(b,a);assert(a&&b);b.top.coreUsage[0]=a.top.coreUsage[0]=999;}
 assert.equal(left.hub.transport(),right.hub.transport());releaseLeft();releaseRight();await drain();
}
checks.push('snapshot reads and legacy four-file fallback use the existing single hub and retain owned return values');
let parseCount=0;const decoder=after.createMonitorPayloadDecoder({parse:raw=>{parseCount++;try{return JSON.parse(raw)}catch{return null}},copy:value=>({...value}),cacheable:()=>true});
assert.deepEqual(decoder('{"x":1}'),{x:1});assert.deepEqual(decoder('{"x":1}'),{x:1});assert.equal(parseCount,1);assert.equal(decoder('invalid'),null);assert.deepEqual(decoder('{"x":1}'),{x:1});assert.equal(parseCount,3);
const long='{"x":1,"padding":"'+'a'.repeat(65537)+'"}';decoder(long);decoder(long);assert.equal(parseCount,5);checks.push('invalid or over-65,536-character strings never install an unbounded decoded cache');
const mixed=[0,100,null,NaN,Infinity,-1,101,42,'50',false,undefined];
for(let i=0;i<1500;i++){const data=Array.from({length:random()%300},()=>mixed[random()%mixed.length]);if(i%4===0)delete data[random()%Math.max(1,data.length)];assert.deepEqual(after.coreUsageValues(data),before.coreUsageValues(data));for(const stale of [false,true])assert.deepEqual(after.coreTileUsage(data,stale),before.coreTileUsage(data,stale));}
for(const raw of [null,{},'bad',undefined,[],Array(128).fill(42)])for(const stale of [false,true])assert.deepEqual(after.coreTileUsage(raw,stale),before.coreTileUsage(raw,stale));
const counts={};for(const side of ['before','after']){let reads=0;const data=new Proxy(Array(128).fill(42),{get(target,key,receiver){if(typeof key==='string'&&/^\d+$/.test(key))reads++;return Reflect.get(target,key,receiver)}});for(let i=0;i<200;i++)(side==='before'?before:after).coreTileUsage(data);counts[side]=reads;}operations.coreElementReads=counts;assert.equal(counts.before,3200);assert.equal(counts.after,3200);
checks.push('1500 core histories including holes/invalid values match; only 16 visible elements are read by tile presentation');
const prefs=after.normalizeMonitorOutput();const oldSignature=y=>JSON.stringify([y.position,y.crosshair,y.layout,y.scale,y.gap,y.fields]);
assert(after.sameHudLayoutAppearance(prefs.ymcc,structuredClone(prefs.ymcc)));
for(const metric of after.HUD_METRICS){const next=structuredClone(prefs.ymcc);next.fields[metric.id]=!next.fields[metric.id];assert.equal(after.sameHudLayoutAppearance(prefs.ymcc,next),oldSignature(prefs.ymcc)===oldSignature(next));}
for(const [key,value]of Object.entries({position:2,crosshair:'dot',layout:'bar',scale:1.4,gap:12,opacity:.1,underlayOpacity:.1,radius:1,saturation:.3})){const next={...prefs.ymcc,[key]:value};assert.equal(after.sameHudLayoutAppearance(prefs.ymcc,next),oldSignature(prefs.ymcc)===oldSignature(next));}
checks.push('direct layout comparison exactly matches previous placement/field JSON signatures');
function fakeTime(){let now=100000,id=0;const timers=new Map();let arms=0,cancels=0,expired=0;const fired=[];
 return {now:()=>now,clock:t=>now=t,setTimer(fn,ms){arms++;timers.set(++id,{fn,at:now+ms});return id},clearTimer(i){cancels++;timers.delete(i)},expire(){expired++;fired.push(now)},advance(end){let safety=10000;while(timers.size){const pair=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];if(pair[1].at>end)break;if(--safety===0)throw Error('timer loop');now=pair[1].at;timers.delete(pair[0]);pair[1].fn();}now=end;},stats:()=>({arms,cancels,expired,pending:timers.size,fired}),timers};
}
const times={before:fakeTime(),after:fakeTime()};let oldTimer=null;const leases={before:{accept(end){if(oldTimer!==null)times.before.clearTimer(oldTimer);oldTimer=times.before.setTimer(()=>times.before.expire(),Math.max(1,end-times.before.now()));},dispose(){if(oldTimer!==null)times.before.clearTimer(oldTimer)}},after:after.createHudTransportLease(times.after)};
for(let i=0;i<200;i++)for(const side of ['before','after']){const t=100000+i*200;times[side].advance(t);leases[side].accept(t+6000);assert.equal(times[side].stats().pending,1);}
for(const side of ['before','after'])times[side].advance(145800);assert.deepEqual(times.after.stats().fired,times.before.stats().fired);operations.transportTimers={before:times.before.stats(),after:times.after.stats()};assert.equal(times.before.stats().arms,200);assert(times.after.stats().arms<10);
const f=fakeTime(),lease=after.createHudTransportLease(f);assert(lease.accept(106000));assert(lease.accept(101000.5));assert.equal(f.stats().pending,1);f.advance(101000.5);assert.equal(f.stats().expired,1);assert.equal(lease.accept(NaN),false);assert.equal(lease.accept(110000),false);assert(lease.accept(102000));lease.dispose();f.advance(110000);assert.equal(f.stats().expired,1);assert.equal(f.stats().pending,0);
checks.push('200 fast packets keep one expiry timer; exact silence/shortened/fractional deadline and disposal behavior retained');
// Simulate callbacks already queued by the host before cancellation. A stale
// wake must not close a renewed/shortened lease or revive a disposed output.
const queued=fakeTime(),queuedLease=after.createHudTransportLease(queued);
assert(queuedLease.accept(106000));const staleWake=[...queued.timers.values()][0].fn;
assert(queuedLease.accept(102000));staleWake();assert.equal(queued.stats().expired,0);assert.equal(queued.stats().pending,1);
const disposedWake=[...queued.timers.values()][0].fn;queuedLease.dispose();queued.clock(110000);disposedWake();staleWake();
assert.equal(queued.stats().expired,0);assert.equal(queued.stats().pending,0);assert.equal(queuedLease.accept(111000),false);
checks.push('already-queued cancelled wake cannot expire the current lease; disposal rejects late callbacks and renewals');
const htmls={};
for(const side of ['before','after']){
 const full=path.join(side==='before'?baseline:root,'src/monitorHudOutputEntry.ts');
 const entry=`import ${JSON.stringify(full)};import {normalizeMonitorOutput,makeHudFixture} from './src/bridge/monitorHudModel';
 window.send=async(seq,patch={})=>{let p=normalizeMonitorOutput({ymcc:{...patch}}),data=makeHudFixture();data={...data,source:'ymcc-uniform'};const frame={type:'desktop-hud',protocol:1,hudId:1,nonce:'offline-hud-nonce-token',seq,expiresAt:Date.now()+6000,show:true,preferences:p,data};window.__event('desktopHud.frame',frame);for(let i=0;i<20;i++)await Promise.resolve();};
 window.sendRaw=async(raw)=>{window.__event('desktopHud.frame',raw);for(let i=0;i<20;i++)await Promise.resolve();};window.ready=true;`;
 const plugin={name:'inert-output-entry',setup(b){
 b.onResolve({filter:/\.css$/},a=>({path:a.path,namespace:'empty-style'}));b.onLoad({filter:/.*/,namespace:'empty-style'},()=>({contents:'',loader:'js'}));
 b.onResolve({filter:/bridge\/ipc$/},()=>({path:'ipc',namespace:'inert-entry'}));
 b.onResolve({filter:/bridge\/monitorHudRenderer$/},()=>({path:'renderer',namespace:'inert-entry'}));
 b.onLoad({filter:/.*/,namespace:'inert-entry'},a=>({contents:a.path==='ipc'?`export const isNativeRuntime=true;export function on(name,cb){window.__listeners[name]=cb;return()=>delete window.__listeners[name]};export async function invoke(cmd,args){window.__calls.push({cmd,args});return cmd==='desktopHud.ready'?{id:1,width:1920,height:1080}:true}`:`export function mountMonitorHud(el,o){window.__mounted++;const size=()=>o.onSize?.(500,200);size();return {update(p,d){window.__updates++;},measure(){window.__measures++;size();},setViewport(){},dispose(){window.__disposed++;}};}`,loader:'ts'}));
 b.onResolve({filter:/^vue$/},()=>({path:req.resolve('vue/dist/vue.runtime.esm-bundler.js')}));
 b.onResolve({filter:/^(?:\.{1,2}\/|@\/)/},a=>{let p=a.path.startsWith('@/')?path.join(root,'src',a.path.slice(2)):path.resolve(a.resolveDir,a.path);const find=v=>[v,v+'.ts',v+'.vue'].find(f=>fs.existsSync(f));if(side==='before'&&p.startsWith(baseline)&&!find(p))p=path.join(root,path.relative(baseline,p));const found=find(p);return found?{path:found}:undefined;});
 b.onLoad({filter:/monitorHudOutputEntry\.ts$/},a=>{let text=fs.readFileSync(a.path,'utf8');text=text.replace('const shape=JSON.stringify(',`globalThis.__monitorHotOps.${side}.layoutSignatures++;const shape=JSON.stringify(`);return {contents:text,loader:'ts',resolveDir:path.dirname(a.path)}});
 }};
 const bundle=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'browser',format:'iife',plugins:[plugin]});
 const virtual=`window.__listeners={};window.__calls=[];window.__mounted=window.__updates=window.__measures=window.__disposed=0;window.__event=(name,data)=>window.__listeners[name]?.(data);globalThis.__monitorHotOps={before:${JSON.stringify(blank())},after:${JSON.stringify(blank())}};let t=100000,tid=0,arms=0,cancels=0;const timers=new Map();Date.now=()=>t;window.setTimeout=(fn,ms)=>{arms++;timers.set(++tid,{fn,at:t+ms});return tid};window.clearTimeout=id=>{cancels++;timers.delete(id)};window.step=end=>{let budget=10000;while(timers.size){const p=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];if(p[1].at>end)break;if(--budget===0)throw Error('timer loop');t=p[1].at;timers.delete(p[0]);p[1].fn();}t=end;};window.state=()=>({calls:window.__calls,mounted:window.__mounted,updates:window.__updates,measures:window.__measures,disposed:window.__disposed,visible:document.getElementById('hud').style.visibility,arms,cancels,pending:timers.size,signatures:globalThis.__monitorHotOps.${side}.layoutSignatures});`;
 htmls[side]=`<!doctype html><html><body><div id="hud"></div><script>${virtual}</script><script>${bundle.outputFiles[0].text.replaceAll('</script','<\\/script')}</script></body></html>`;fs.writeFileSync(path.join(out,side+'-entry.html'),htmls[side]);
}
const server=createServer((request,response)=>{response.setHeader('Content-Type','text/html;charset=utf-8');response.end(htmls[request.url?.includes('before')?'before':'after']);});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules', {chromium}=createRequire(path.join(runtime,'_monitor-hotpaths.cjs'))('playwright');let browser;const browserCases=[],errors=[];
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const pages={};
 for(const side of ['before','after']){const page=pages[side]=await browser.newPage();page.on('pageerror',error=>errors.push(side+': '+error));await page.goto('http://127.0.0.1:'+server.address().port+'/'+side+'?hudId=1&nonce=offline-hud-nonce-token');await page.waitForFunction(()=>window.ready);}
 const equal=async()=>{const a=await pages.before.evaluate(()=>window.state()),b=await pages.after.evaluate(()=>window.state());for(const key of ['calls','mounted','updates','measures','disposed','visible','pending'])assert.deepEqual(b[key],a[key],key);return {before:a,after:b};};
 for(let i=0;i<200;i++){for(const side of ['before','after'])await pages[side].evaluate(async i=>{window.step(100000+i*200);await window.send(i+1)},i);}
 const states=await equal();operations.actualOutputEntry={before:{arms:states.before.arms,cancels:states.before.cancels,layoutSignatures:states.before.signatures},after:{arms:states.after.arms,cancels:states.after.cancels,layoutSignatures:states.after.signatures}};assert.equal(states.before.signatures,0);assert.equal(states.after.signatures,0);assert(states.before.arms<10);assert(states.after.arms<10);browserCases.push('200 packets through actual entry: same layout commands/updates/visible state, existing bounded timer arms and no layout JSON signature');
 let seq=201;for(const patch of [{position:2},{crosshair:'dot'},{layout:'bar'},{scale:1.4},{gap:12},{opacity:.1},{fields:{cpuPower:false}}]){const next=seq++;for(const side of ['before','after'])await pages[side].evaluate(([n,p])=>window.send(n,p),[next,patch]);await equal();browserCases.push('actual placement/composition change '+JSON.stringify(patch));}
 for(const raw of [null,{}, {type:'desktop-hud',protocol:1,hudId:1,nonce:'wrong',seq:999}, {type:'desktop-hud',protocol:1,hudId:1,nonce:'offline-hud-nonce-token',seq:1}]){for(const side of ['before','after'])await pages[side].evaluate(raw=>window.sendRaw(raw),raw);await equal();}browserCases.push('foreign, old and malformed packets never renew transport or reach render/layout');
 for(const side of ['before','after'])await pages[side].evaluate(()=>window.step(145800));const closed=await equal();assert.equal(closed.after.visible,'hidden');assert.equal(closed.after.calls.filter(c=>c.cmd==='desktopHud.close').length,1);browserCases.push('after last accepted packet: exact silent deadline hides and closes once');
 for(const side of ['before','after'])await pages[side].evaluate(()=>window.dispatchEvent(new Event('pagehide')));await equal();assert.deepEqual(errors,[]);browserCases.push('pagehide releases timers/listeners/renderer');
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
const result={ok:true,checks,browserCases,operations,scope:'Complete current/baseline unified hub, core helpers, layout comparison, transport lease and output entry. Explicit synthetic publications, inert native renderer/IPC and virtual clocks; no live collectors, installed application, game or CPU/timing measurement.'};fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({ok:true,checks:checks.length,browserCases:browserCases.length,operations},null,2));