// Real production model/gate/Vue replay; synthetic unified publications, no acquisition.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {monitorHudVuePlugin} from './monitor_hud_vue_build.mjs';
const root=process.cwd(), req=createRequire(path.join(root,'package.json')), {build}=req('esbuild');
const out=path.resolve(process.env.YMCC_MONITOR_FPS_ENTRY_OUT || path.join(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/fps-family-entry-20261014/validation'));
fs.mkdirSync(out,{recursive:true});
const unitBundle=await build({stdin:{contents:"export * from './src/bridge/monitorHudFpsVisibility.ts';export * from './src/bridge/monitorHudModel.ts';",resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'node',format:'esm',alias:{'@':path.join(root,'src')}});
const model=await import('data:text/javascript;base64,'+Buffer.from(unitBundle.outputFiles[0].text).toString('base64'));
const start=1700000000000, checks=[];
function harness(){let now=start,id=0,maxTimers=0;const timers=new Map(),events=[];const gate=model.createHudFpsVisibilityGate({now:()=>now,emit:ready=>events.push({at:now,ready}),setTimer(fn,ms){const next=++id;timers.set(next,{fn,at:now+ms});maxTimers=Math.max(maxTimers,timers.size);return next},clearTimer:id=>timers.delete(id)});return {gate,timers,events,get now(){return now},get maxTimers(){return maxTimers},set(t){now=t},advance(t){now=t;for(const [key,timer] of [...timers])if(timer.at<=now){timers.delete(key);timer.fn()}}};}
function data(at, patch={}){return {...model.makeHudFixture(),source:'ymcc-uniform',stale:false,fpsObservedAt:at,fpsTarget:'session|generation|2077|created',freshUntil:{fps:at+5000},...patch};}
const run=(name,body)=>{body();checks.push(name)};
run('genuine FPS >=1 observes 3s before entry',()=>{const h=harness();for(const dt of [0,1000,2000,2999,3000]){h.set(start+dt);h.gate.accept(data(h.now,{values:{fps:1}}));assert.equal(h.gate.ready(),dt>=3000)}assert.equal(h.events.length,1);assert.equal(h.maxTimers,1);h.gate.dispose();assert.equal(h.timers.size,0)});
run('one cached success cannot enter after 3s, including repeat deliveries',()=>{const h=harness(),d=data(start);for(const dt of [0,1000,2000,3000,4999]){h.set(start+dt);h.gate.accept(d);assert.equal(h.gate.ready(),false)}h.advance(start+5000);assert.equal(h.gate.ready(),false);assert.equal(h.timers.size,0)});
run('5s loss deadline hides and returning FPS must requalify',()=>{const h=harness();for(let dt=0;dt<=3000;dt+=1000){h.set(start+dt);h.gate.accept(data(h.now))}h.advance(start+7999);assert(h.gate.ready());h.advance(start+8000);assert(!h.gate.ready());for(let dt=9000;dt<=12000;dt+=1000){h.set(start+dt);h.gate.accept(data(h.now));assert.equal(h.gate.ready(),dt>=12000)}assert.equal(h.maxTimers,1)});
run('target changes and proof regression restart entry',()=>{for(const variant of ['target','regression']){const h=harness();for(let dt=0;dt<=3000;dt+=1000){h.set(start+dt);h.gate.accept(data(h.now))}h.set(start+4000);h.gate.accept(data(variant==='regression'?start+2000:h.now,{fpsTarget:variant==='target'?'other-process':'session|generation|2077|created'}));assert(!h.gate.ready())}});
run('silent gap between accepts and clock reversal restart entry',()=>{const h=harness();for(let dt=0;dt<=3000;dt+=1000){h.set(start+dt);h.gate.accept(data(h.now))}h.set(start+9000);h.gate.accept(data(h.now));assert(!h.gate.ready());h.set(start+8000);h.gate.accept(data(h.now));assert(!h.gate.ready())});
run('missing, stale, malformed proofs never show; fixture previews remain immediate',()=>{for(const patch of [{values:{fps:null}},{values:{fps:0}},{values:{fps:.9}},{values:{fps:-1}},{stale:true},{fpsObservedAt:undefined},{fpsTarget:undefined},{fpsObservedAt:start+1},{freshUntil:{fps:start+7000}}]){const h=harness();h.gate.accept(data(start,patch));assert(!h.gate.ready());assert.equal(h.timers.size,0)}const h=harness();h.gate.accept(model.makeHudFixture());assert(h.gate.ready());h.gate.accept({...model.makeHudFixture(),values:{fps:null}});assert(!h.gate.ready())});
run('dispose clears owned timers and rejects queued callbacks',()=>{const h=harness();h.gate.accept(data(start));const callback=[...h.timers.values()][0].fn;h.gate.dispose();h.set(start+5000);callback();h.gate.accept(data(h.now));assert.equal(h.timers.size,0);assert.deepEqual(h.events,[])});
run('FPS keeps its selected shell; dependent statistics hide without qualified FPS',()=>{const fields=Object.fromEntries(model.HUD_METRICS.map(m=>[m.id,true]));for(const hide of [false,true]){const d=model.makeHudFixture();d.values.fps=0;for(const visible of [model.hudVisibleFields(fields,d,hide),model.hudVisibleFields(fields,model.makeHudFixture(),hide,false)]){for(const key of model.HUD_FPS_FAMILY)assert.equal(visible[key],key==='fps');assert.equal(visible.cpuPower,true)}const off=model.hudVisibleFields({...fields,fps:false},d,hide);assert.equal(off.fps,false)}assert.equal(model.hudMetricAvailable({...model.makeHudFixture(),values:{fps:null,fpsLimit:120}},'fpsLimit'),false)});
run('family dependencies update when standalone FPS switch is off; bounded proofs preserved',()=>{const fields=Object.fromEntries(model.HUD_METRICS.map(m=>[m.id,m.id==='fpsLimit']));const a=data(start),b=data(start+1000);assert(!model.sameHudData(a,b,fields));assert(!model.sameHudData(a,{...a,values:{...a.values,fps:null}},fields));const bounded=model.boundedHudData(a);assert.equal(bounded.fpsObservedAt,start);assert.equal(bounded.fpsTarget,a.fpsTarget)});
run('source handoff preserves qualification across second/millisecond timestamp ordering',()=>{
 const h=harness();for(let dt=0;dt<=3000;dt+=1000){h.set(start+dt);h.gate.accept(data(h.now,{fpsSource:'hwinfo-presented'}));}assert(h.gate.ready());
 h.set(start+4100);h.gate.accept(data(h.now,{fpsSource:'rtss'}));assert(h.gate.ready());
 h.set(start+4500);h.gate.accept(data(start+4000,{fpsSource:'hwinfo-presented'}));assert(h.gate.ready(),'return to fresh preferred HW must not restart 3s entry');
 assert.deepEqual(h.events.map(e=>e.ready),[true]);assert.equal(h.maxTimers,1);
 h.set(start+4600);h.gate.accept(data(start+3500,{fpsSource:'hwinfo-presented'}));assert(!h.gate.ready(),'a rollback within the same source still resets');
});
run('source proofs survive bounded projection and trigger family updates',()=>{
 const a=data(start,{fpsSource:'hwinfo-presented'}),b={...a,fpsSource:'rtss'};
 assert.equal(model.boundedHudData(a).fpsSource,'hwinfo-presented');assert.equal(model.boundedHudData(b).fpsSource,'rtss');
 assert.equal(model.boundedHudData({...a,fpsSource:'unknown'}).fpsSource,undefined);
 assert(!model.sameHudData(a,b,{fpsLimit:true}));
});
run('continuous nonzero HW FPS resumes after delayed delivery without a second entrance wait',()=>{
 for(const expiredProjection of [false,true]){
  const h=harness();for(let dt=0;dt<=3000;dt+=1000){h.set(start+dt);h.gate.accept(data(h.now,{fpsSource:'hwinfo-presented'}));}assert(h.gate.ready());
  h.advance(start+8000);assert(!h.gate.ready(),'expired data itself must stay hidden');
  if(expiredProjection)h.gate.accept({...data(h.now),values:{fps:null}});
  h.set(start+8200);h.gate.accept(data(start+6000,{fpsSource:'hwinfo-presented'}));
  assert(h.gate.ready(),'a newer fresh HW proof overlapping the old deadline proves producer continuity');
  h.advance(start+11000);assert(!h.gate.ready());h.set(start+13000);h.gate.accept(data(h.now,{fpsSource:'hwinfo-presented'}));assert(!h.gate.ready(),'an actual producer gap still requalifies');
  h.gate.dispose();assert.equal(h.timers.size,0);
 }
});
const browserEntry=String.raw`
import {mountMonitorHud} from './src/bridge/monitorHudRenderer.ts';
import {createHudAgeLease} from './src/bridge/monitorHudFreshness.ts';
import {HUD_LAYOUTS,HUD_METRICS,makeHudFixture,normalizeMonitorOutput,hudFromUniformSnapshot} from './src/bridge/monitorHudModel.ts';
import {HUD_FPS_FAMILY as family} from './src/bridge/monitorHudFpsVisibility.ts';
import {createApp,h,nextTick,shallowRef} from 'vue';
import MonitorHudTemplate from './src/components/MonitorHudTemplate.vue';
import MonitorHudPreviewView from './src/views/MonitorHudPreviewView.vue';
import {admitDesktopHudFrame} from './src/bridge/desktopHudProtocol.ts';
window.__time=1700000000000;Date.now=()=>window.__time;
let prefs=normalizeMonitorOutput(),current=hudFromUniformSnapshot(null),renderer=null,app=null,inlineAge=null,sizes=0;
const inlinePrefs=shallowRef(prefs),inlineData=shallowRef(current);
window.__layouts=HUD_LAYOUTS.map(v=>v.value);
window.__reset=async(layout,hide,showFPS)=>{renderer?.dispose();app?.unmount();inlineAge?.dispose();window.__time=1700000000000;sizes=0;
 prefs=normalizeMonitorOutput({ymcc:{layout,scale:1.4,hideUnavailable:hide,underlayPreset:'tifa',fields:Object.fromEntries(HUD_METRICS.map(m=>[m.id,['fps','fpsLimit','fps1','frametime','frametimeGraph','cpuPower'].includes(m.id)&&!(m.id==='fps'&&!showFPS)]))}});
 current=hudFromUniformSnapshot(null);inlinePrefs.value=prefs;inlineData.value=current;
 inlineAge=createHudAgeLease({now:()=>Date.now(),emit:v=>inlineData.value=v,setTimer:(fn,ms)=>window.setTimeout(fn,ms),clearTimer:id=>window.clearTimeout(id)});
 renderer=mountMonitorHud(document.querySelector('#output'),{preferences:prefs,data:current,onSize:()=>++sizes});
 app=createApp({setup:()=>()=>h(MonitorHudTemplate,{appearance:inlinePrefs.value.ymcc,fields:inlinePrefs.value.ymcc.fields,data:inlineData.value})});app.mount('#inline');await nextTick();};
window.__feed=async(dt,observation=dt,target=2077,fps=120,source='rtss')=>{const at=1700000000000+dt,seen=1700000000000+observation;window.__time=at;
 const packet={ts:at,fps,fpsSource:source,fpsObservedAt:seen,fpsValidUntil:seen+(source==='hwinfo-presented'?6000:5000),fpsPollPeriodMs:source==='hwinfo-presented'?3000:0,pid:target,processCreated:'created',fps1:82,fps1Source:'hwinfo-presented-1percent-low',fps1ObservedAt:seen,fps1ValidUntil:seen+5000,fps1PollPeriodMs:0,game:'game',gpu:50,packagePower:30,frametime:8.3,frameTimeSource:'rtss-current-frame',frameTimeTs:seen,frameTimeValidUntil:seen+5000,frameTimePollPeriodMs:0,frameTimes:[8,8.1,8.3]};
 const top={ts:at,tdpW:30,fpsLimit:120,fpsLimitValid:true,fpsLimitTs:at,fpsLimitSource:'rtss-global'};
 current=hudFromUniformSnapshot({session:1,generation:1,collectedAt:at,hwDown:false,top,fps:fps===null?null:packet},at);
 const frame=JSON.parse(JSON.stringify({type:'desktop-hud',protocol:1,hudId:1,nonce:'fps-entry-test-nonce',seq:1,expiresAt:at+6000,show:true,preferences:prefs,data:current}));
 const admitted=admitDesktopHudFrame(frame,1,'fps-entry-test-nonce',at,0);if(!admitted)throw Error('Desktop HUD admission rejected replay');
 renderer.update(prefs,admitted.data);inlineAge.accept(current);await nextTick();};
window.__shape=()=>['output','inline'].map(id=>{const panel=document.querySelector('#'+id+' [data-monitor-hud]');return {id,fpsSelected:prefs.ymcc.fields.fps,fpsText:panel.querySelector('[data-hud-metric="fps"] strong')?.textContent,family:family.filter(key=>panel.querySelector('[data-hud-metric="'+key+'"]')),width:panel.getBoundingClientRect().width,height:panel.getBoundingClientRect().height,cpu:!!panel.querySelector('[data-hud-metric="cpuPower"]'),images:panel.querySelectorAll('.hud-image-underlay').length,text:panel.textContent,readoutRect:(()=>{const r=panel.querySelector('[data-hud-metric="fps"]')?.getBoundingClientRect();return r?{left:r.left,top:r.top,right:r.right,bottom:r.bottom,width:r.width}:null})(),curveRect:(()=>{const r=panel.querySelector('[data-hud-metric="frametimeGraph"]')?.getBoundingClientRect();return r?{left:r.left,top:r.top,right:r.right,bottom:r.bottom,width:r.width}:null})()}});
window.__resetHardware=async()=>{await window.__reset('capsule',true,true);const selected=['fps','fpsLimit','frametimeGraph','cpuPower','cpuTemp','cpuClock','cpuCores','tdpLimit','gpuUsage','gpuBar','gpuPower','gpuClock','ramUsage','ramUsageBar','vramUsage','vramUsageBar'];prefs=normalizeMonitorOutput({...prefs,ymcc:{...prefs.ymcc,scale:.7,underlayPreset:'ac8-pink',fields:Object.fromEntries(HUD_METRICS.map(m=>[m.id,selected.includes(m.id)]))}});inlinePrefs.value=prefs;};
 window.__hardwareFeed=async(dt,healthy,session=1)=>{const at=1700000000000+dt;window.__time=at;
 const top={ts:healthy?at:1700000005000,hwDown:!healthy,cpuPowerValid:healthy,cpuTempValid:healthy,cpuClockValid:healthy,tdpW:healthy?100.8:0,tempC:healthy?86:0,freqMhz:healthy?5490:0,coreUsage:[9,21,8,32,11,6,4,21,29,9,4,17,4,8,10,12],coreUsageTs:at,gpuSelectionPresent:healthy,gpuPowerW:healthy?50.5:null,gpuUsagePct:healthy?4:null,gpuClockMhz:healthy?2800:null,gpuMemoryUsedMb:healthy?2400:null,gpuMemoryTotalMb:healthy?16000:null,gpuMemoryLoadPct:healthy?15:null,physicalMemoryUsedMb:healthy?68000:null,physicalMemoryTotalMb:healthy?100000:null,physicalMemoryLoadPct:healthy?68:null,fpsLimit:200,fpsLimitValid:true,fpsLimitTs:at,fpsLimitSource:'rtss-global'};
 const fps={ts:at,fps:200,fpsSource:'rtss',fpsObservedAt:at,fpsValidUntil:at+5000,fpsPollPeriodMs:0,pid:2077,processCreated:'134371879491230438',game:'fixture',frametime:5,frameTimeSource:'rtss-current-frame',frameTimeTs:at,frameTimeValidUntil:at+5000,frameTimePollPeriodMs:0,frameTimes:[5,5.1,4.8,5.5,5.2,4.9,5,5.7,5.1,4.9,5.2,5]};
 current=hudFromUniformSnapshot({session,generation:1,collectedAt:at,hwDown:!healthy,top,fps},at,{status:'applied',proof:'write-ack',mode:'ac',watts:120});renderer.update(prefs,current);inlineAge.accept(current);await nextTick();};
 let nativePreview=null,previewChannel=null,previewSequence=0;
 window.__openNativePreview=async()=>{window.__time=1700000000000;previewSequence=0;window.__previewCloseCalls=0;window.BroadcastChannel=class {constructor(){previewChannel=this;}postMessage(){}close(){this.onmessage=null;}};nativePreview=createApp(MonitorHudPreviewView);nativePreview.mount('#nativePreview');await nextTick();const p=normalizeMonitorOutput({enabled:true,output:'ymcc',ymcc:{hideUnavailable:true,fields:Object.fromEntries(HUD_METRICS.map(m=>[m.id,['cpuPower','gpuUsage','gpuBar','ramUsage','ramUsageBar'].includes(m.id)]))}});previewChannel.onmessage({data:{kind:'ymcc-hud-preview-appearance',token:'preview-robustness-channel',seq:++previewSequence,preferences:p}});await nextTick();};
 window.__previewFeed=async(dt,healthy)=>{const now=1700000000000+dt;window.__time=now;const data=hudFromUniformSnapshot({session:1,generation:1,collectedAt:now,hwDown:!healthy,top:{ts:healthy?now:1700000000000,hwDown:!healthy,tdpW:100.8,gpuSelectionPresent:healthy,gpuUsagePct:healthy?4:null,physicalMemoryUsedMb:6800,physicalMemoryTotalMb:10000}},now);previewChannel.onmessage({data:{kind:'ymcc-hud-preview',token:'preview-robustness-channel',seq:++previewSequence,data,expiresAt:healthy?now+6000:now}});await nextTick();};
 window.__previewText=()=>document.querySelector('#nativePreview [data-monitor-hud]')?.textContent??'';
 window.__closeNativePreview=()=>{nativePreview?.unmount();nativePreview=null;};
 window.__cleanup=()=>{renderer?.dispose();app?.unmount();inlineAge?.dispose();window.__closeNativePreview()};`;
// Avoid a nonexistent model re-export in the browser import.
const contents=browserEntry.replace('HUD_LAYOUTS,HUD_FPS_FAMILY,HUD_METRICS','HUD_LAYOUTS,HUD_METRICS');
const browserSourceHashes={};
const pinBrowserSources={name:'pin-production-hud-sources',setup(b){b.onLoad({filter:/\.(vue|ts)$/},a=>{if(a.path.startsWith(path.join(root,'src')+path.sep))browserSourceHashes[path.relative(root,a.path).replaceAll('\\','/')]=createHash('sha256').update(fs.readFileSync(a.path)).digest('hex');return undefined;});}};
const browserInputsUnchanged=()=>Object.entries(browserSourceHashes).every(([p,sha])=>createHash('sha256').update(fs.readFileSync(path.join(root,p))).digest('hex')===sha);
const bundle=await build({stdin:{contents:browserEntry,resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'browser',format:'iife',alias:{'@':path.join(root,'src')},plugins:[pinBrowserSources,{name:'native-preview-fixture-boundary',setup(b){b.onResolve({filter:/.*/},a=>{if(!a.importer.endsWith('MonitorHudPreviewView.vue'))return;const p=a.path.replaceAll('\\','/');if(p==='vue-router')return{path:'route',namespace:'preview-host'};if(/(?:^|\/)(api|ipc)(?:\.ts)?$/.test(p))return{path:p.includes('ipc')?'ipc':'api',namespace:'preview-host'};});b.onLoad({filter:/.*/,namespace:'preview-host'},a=>({loader:'ts',contents:a.path==='route'?'export const useRoute=()=>({query:{channel:"preview-robustness-channel",__ymccChild:"42"}});':a.path==='ipc'?'export const isNativeRuntime=true;':'export const windowApi={closeChild:async()=>{window.__previewCloseCalls=(window.__previewCloseCalls??0)+1;return true;}};'}));}},monitorHudVuePlugin(root)]});
const html='<!doctype html><meta charset="utf-8"><style>body{margin:0;background:#253041}#output,#inline{width:1920px}</style><div id="output"></div><div id="inline"></div><div id="nativePreview"></div><script>'+bundle.outputFiles[0].text.replaceAll('</script','<\\/script')+'</script>';
fs.writeFileSync(path.join(out,'replay.html'),html);
const server=createServer((_,res)=>{res.setHeader('Content-Type','text/html;charset=utf-8');res.end(html)});await new Promise(r=>server.listen(0,'127.0.0.1',r));
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_monitor-entry.cjs')('playwright');
let browser,ok=false;const scenarios=[],errors=[],hardwareGeometry=[];
try{browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:1920,height:1080}});page.on('pageerror',e=>errors.push(String(e)));await page.clock.install({time:new Date(start)});await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>window.__reset);
 const layouts=await page.evaluate(()=>window.__layouts);
 const shape=async()=>{await page.clock.runFor(60);return page.evaluate(()=>window.__shape())};
 const empty=shapes=>{for(const s of shapes){assert.deepEqual(s.family,s.fpsSelected?['fps']:[],JSON.stringify(s));if(s.fpsSelected)assert.equal(s.fpsText,'FPS');assert(s.cpu)}};
 for(const layout of layouts)for(const hide of [true,false])for(const showFPS of [true,false]){
   await page.evaluate(args=>window.__reset(...args),[layout,hide,showFPS]);await page.evaluate(()=>window.__feed(0,0,2077,null));let baseline=await shape();empty(baseline);
   for(const dt of [1000,2000,3000,3999]){await page.evaluate(dt=>window.__feed(dt),dt);const s=await shape();empty(s);for(let i=0;i<s.length;i++){assert(Math.abs(s[i].width-baseline[i].width)<.1);assert(Math.abs(s[i].height-baseline[i].height)<.1,JSON.stringify({layout,hide,showFPS,dt,baseline,actual:s}))}}
   await page.evaluate(()=>window.__feed(4000));let admitted=await shape();for(const s of admitted){assert(s.family.includes('fpsLimit'));assert(s.family.includes('fps1'));assert(s.family.includes('frametimeGraph'));assert.equal(s.family.includes('fps'),showFPS);assert.equal(s.images,1);if(showFPS){assert(s.curveRect.top>=s.readoutRect.bottom-.5,JSON.stringify({layout,...s}));assert(Math.abs(s.curveRect.left-s.readoutRect.left)<1,JSON.stringify({layout,...s}));assert(Math.abs(s.curveRect.width-s.readoutRect.width)<1,JSON.stringify({layout,...s}))}}
   await page.evaluate(()=>window.__feed(7000,4000));const held=await shape();for(let i=0;i<held.length;i++){assert.deepEqual(held[i].family,admitted[i].family);assert(Math.abs(held[i].width-admitted[i].width)<.1)}
   await page.evaluate(()=>window.__feed(9000,4000));empty(await shape());
   for(const dt of [10000,11000,12000]){await page.evaluate(dt=>window.__feed(dt),dt);empty(await shape())}
   await page.evaluate(()=>window.__feed(13000));for(const s of await shape())assert(s.family.includes('fpsLimit'));
   await page.evaluate(()=>window.__feed(14000,14000,2077,0));empty(await shape());
   await page.evaluate(()=>window.__feed(15000,15000,2077,.9));empty(await shape());
   for(const dt of [16000,17000,18000]){await page.evaluate(dt=>window.__feed(dt,dt,2077,1),dt);empty(await shape())}
   await page.evaluate(()=>window.__feed(19000,19000,2077,1));for(const s of await shape())assert(s.family.includes('fpsLimit'));
   await page.evaluate(()=>window.__feed(20000,20000,99));empty(await shape());
   scenarios.push({layout,hide,showFPS});
 }
 await page.evaluate(()=>window.__reset('capsule',true,true));for(const dt of [0,1000,2000,3000])await page.evaluate(dt=>window.__feed(dt),dt);for(const s of await shape())assert(s.family.includes('fpsLimit'));
 await page.evaluate(()=>{window.__time=1700000008000});await page.clock.runFor(5001);empty(await page.evaluate(()=>window.__shape()));checks.push('silent producer deadline removes family in output and inline preview');
 await page.evaluate(()=>window.__reset('capsule',true,true));
 for(const dt of [0,1000,2000,3000])await page.evaluate(dt=>window.__feed(dt,dt,2077,120,'hwinfo-presented'),dt);
 for(const s of await shape())assert(s.family.includes('fpsLimit'));
 for(const [dt,observed,source] of [[4100,4100,'rtss'],[4500,4000,'hwinfo-presented']]){
  await page.evaluate(args=>window.__feed(args[0],args[1],2077,120,args[2]),[dt,observed,source]);
  for(const s of await shape()){assert(s.family.includes('fpsLimit'),JSON.stringify({source,...s}));assert.equal(s.fpsText,'120')}
 }
 checks.push('actual Vue output and inline preview keep real FPS visible across RTSS-to-HW clock ordering');
 await page.evaluate(()=>window.__reset('capsule',true,true));
 for(const dt of [0,1000,2000,3000])await page.evaluate(dt=>window.__feed(dt,dt,2077,120,'hwinfo-presented'),dt);
 for(const s of await shape())assert(s.family.includes('fpsLimit'));
 await page.evaluate(()=>{window.__time=1700000009000});await page.clock.runFor(10000);
 for(const s of await page.evaluate(()=>window.__shape())){assert.deepEqual(s.family,['fps']);assert.equal(s.fpsText,'FPS')}
 await page.evaluate(()=>window.__feed(9200,6000,2077,120,'hwinfo-presented'));
 for(const s of await shape()){assert(s.family.includes('fpsLimit'),JSON.stringify(s));assert.equal(s.fpsText,'120')}
 checks.push('constant-positive HW stream resumes immediately after a delayed renderer delivery; expired pixels still withdraw');
 await page.evaluate(()=>window.__resetHardware());for(const dt of [0,1000,2000,3000,4000,5000])await page.evaluate(dt=>window.__hardwareFeed(dt,true),dt);
 const beforeHardware=await shape();hardwareGeometry.push({dt:5000,shapes:beforeHardware});await page.locator('#output [data-monitor-hud]').screenshot({path:path.join(out,'hardware-normal.png')});
 for(const dt of [6000,7000,8000]){await page.evaluate(dt=>window.__hardwareFeed(dt,false),dt);const actual=await shape();hardwareGeometry.push({dt,shapes:actual});for(let i=0;i<actual.length;i++){assert(actual[i].text.includes('100.8'),JSON.stringify(actual[i]));assert(actual[i].text.includes('86'));assert(actual[i].text.includes('50.5'));assert(Math.abs(actual[i].width-beforeHardware[i].width)<.5);assert(Math.abs(actual[i].height-beforeHardware[i].height)<.5,JSON.stringify({dt,before:beforeHardware[i],actual:actual[i]}))}}
 await page.locator('#output [data-monitor-hud]').screenshot({path:path.join(out,'hardware-brief-gap.png')});
 await page.evaluate(()=>window.__hardwareFeed(9000,true));for(const s of await shape())assert(s.text.includes('100.8'));
 for(const dt of [10000,11000,12000,13000,14000,15000])await page.evaluate(dt=>window.__hardwareFeed(dt,false),dt);
 for(const s of await shape()){assert(!s.text.includes('100.8'));assert(!s.text.includes('50.5'));assert(s.text.includes('120'))}
 await page.evaluate(()=>window.__hardwareFeed(16000,true));await shape();await page.evaluate(()=>window.__hardwareFeed(17000,false,2));for(const s of await shape())assert(!s.text.includes('100.8'));
 checks.push('real output and inline preview retain a brief whole-HW gap at fixed geometry, expire sustained loss and reject a new native session');
 fs.writeFileSync(path.join(out,'preview.html'),html+'<script>window.__resetHardware().then(async()=>{for(const dt of [0,1000,2000,3000])await window.__hardwareFeed(dt,true)});<\/script>');
 await page.evaluate(()=>window.__openNativePreview());await page.evaluate(()=>window.__previewFeed(0,true));await page.clock.runFor(60);assert((await page.evaluate(()=>window.__previewText())).includes('100.8'));
 await page.evaluate(()=>window.__previewFeed(1000,false));await page.clock.runFor(60);assert((await page.evaluate(()=>window.__previewText())).includes('100.8'),'native preview must honor the still-valid hardware receipt instead of a second whole-packet expiry');
 await page.evaluate(()=>{window.__time=1700000006000});await page.clock.runFor(6000);assert(!(await page.evaluate(()=>window.__previewText())).includes('100.8'),'native preview must expire a sustained producer loss');
 await page.evaluate(()=>window.__closeNativePreview());await page.clock.runFor(300001);assert.equal(await page.evaluate(()=>window.__previewCloseCalls),0,'unmounted preview must not run its later auto-close callback');
 checks.push('actual native preview uses per-family expiry during a short HW gap and clears owned deadlines on unmount');
 await page.evaluate(()=>window.__cleanup());assert.deepEqual(errors,[]);assert(browserInputsUnchanged(),'Production browser input sources changed during replay');ok=true;
}finally{await browser?.close();await new Promise(r=>server.close(r));fs.writeFileSync(path.join(out,'FPS-FAMILY-ENTRY.json'),JSON.stringify({ok,checks,scenarios,errors,hardwareGeometry,sourceHashes:browserSourceHashes,inputsUnchanged:browserInputsUnchanged(),scope:'Production shared model, pure gate, actual Vue output renderer and inline template. Synthetic YMCC packets and deterministic clock; no hardware IO, live-game acceptance or CPU benchmark.'},null,2));}
console.log(JSON.stringify({ok,checks:checks.length,browserScenarios:scenarios.length,out}));