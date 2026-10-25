// OFFLINE performance gate: real display feed/model/HUD, synthetic data only.
// Own headless Edge. Never connects to YMCC, HWiNFO, Steam, RTSS or a game.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { performance } from 'node:perf_hooks';
const root = process.cwd(), req = createRequire(path.join(root, 'package.json'));
const { build } = req('esbuild'), { parse, compileScript, compileStyle } = req('vue/compiler-sfc');
const out = path.resolve(root, '../../../Build/Tasks/YMCC-Steam-HUD-Probe/templates-tdp-content-20261010/performance');
fs.mkdirSync(out, { recursive: true });
const seconds = Number(process.env.YMCC_CPU_SECONDS || 20), rounds = Number(process.env.YMCC_CPU_ROUNDS || 2);
assert(seconds >= 15 && rounds >= 2, 'Acceptance needs >=15 seconds and >=2 repeats; do not lower for a passing result');
const budget = { nodeMapAndSerializeP95Ms: 1, browserWholeProcessSingleCorePercent: 1, displayIntervalMs: 1000 };
const workloads = [
  { id: 'idle', label: 'Static common HUD baseline', fields: 'default', cores: 16, frames: 24, period: 0 },
  { id: 'minimal', label: '3 fields: FPS / CPU temperature / GPU usage', fields: ['fps','cpuTemp','gpuUsage'], cores: 0, frames: 0, period: 1000 },
  { id: 'common', label: '15 default fields + 16 core load bars', fields: 'default', cores: 16, frames: 24, period: 1000 },
  { id: 'full', label: '29 fields + 16 cores + 120 frame samples', fields: 'all', cores: 16, frames: 120, period: 1000 },
  { id: 'maximum', label: '29 fields + 128 cores/clocks + 120 frame samples', fields: 'all', cores: 128, frames: 120, period: 1000 },
  { id: 'bar-common', label: 'Horizontal bar / 15 defaults / content fit observer', fields: 'default', layout: 'bar', cores: 16, frames: 24, period: 1000 },
  { id: 'bar-full', label: 'Horizontal bar / all 29 fields / content fit observer', fields: 'all', layout: 'bar', cores: 16, frames: 120, period: 1000 },
  { id: 'burst', label: '200Hz upstream event burst, common HUD limited to 1Hz', fields: 'default', cores: 16, frames: 24, period: 5 },
];
const entry = `
import {createApp, h, shallowRef} from 'vue';
import Hud from './src/components/MonitorHudTemplate.vue';
import {makeHudFixture,normalizeMonitorOutput,HUD_METRICS,boundedHudData} from './src/bridge/monitorHudModel.ts';
import {createLatestDisplayFeed} from './src/bridge/monitorDisplayFeed.ts';
const workloads=${JSON.stringify(workloads)}, role=new URL(location.href).searchParams.get('role');
const data=shallowRef(makeHudFixture()),prefs=shallowRef(normalizeMonitorOutput());
let inputTimer=null,expiry=null,feed=null,off=null,input=0;
const channel=new BroadcastChannel('offline-ymcc-cpu');
const stats={role,sent:0,received:0,inputs:0,sentAt:[],lastInput:0};
function fixture(w,seq){
 const d=makeHudFixture();
 for(const key of Object.keys(d.values))if(typeof d.values[key]==='number')d.values[key]+=((seq%13)-6)*.03;
 d.values.fps=100+seq%31;d.values.cpuTemp=50+seq%24;d.values.gpuUsage=50+seq%49;
 d.coreUsage=Array.from({length:w.cores},(_,i)=>(i*13+seq*7)%101);
 d.coreClocks=Array.from({length:w.cores},(_,i)=>3+((i+seq)%31)/20);
 d.frameTimes=Array.from({length:w.frames},(_,i)=>6+((i+seq)%31)/3);
 return d;
}
if(role==='child'){
 createApp({setup:()=>()=>h(Hud,{data:data.value,fields:prefs.value.ymcc.fields,appearance:prefs.value.ymcc})}).mount('#app');
 channel.onmessage=({data:p})=>{
  if(p.kind==='appearance'){prefs.value=normalizeMonitorOutput(p.preferences);return;}
  if(p.kind!=='data'||p.data?.source!=='fixture')return;
  data.value=boundedHudData(p.data);stats.received++;
  if(expiry!==null)clearTimeout(expiry);
  expiry=setTimeout(()=>{data.value={...data.value,stale:true}},6000);
 };
}else{
 window.__setWorkload=id=>{
  if(inputTimer!==null)clearInterval(inputTimer);off?.();feed?.dispose();
  const w=workloads.find(w=>w.id===id);if(!w)throw Error('Unknown workload');
  const p=normalizeMonitorOutput();p.ymcc.layout=w.layout||'corner';
  if(w.fields!=='default')for(const m of HUD_METRICS)p.ymcc.fields[m.id]=w.fields==='all'||w.fields.includes(m.id);
  channel.postMessage({kind:'appearance',preferences:p});input=0;stats.sentAt=[];
  feed=createLatestDisplayFeed(0,{now:()=>performance.now(),key:String,setTimer:(fn,ms)=>setTimeout(fn,ms),clearTimer:id=>clearTimeout(id)});
  off=feed.subscribe(seq=>{stats.sent++;stats.lastInput=seq;stats.sentAt.push(performance.now());if(stats.sentAt.length>64)stats.sentAt.shift();channel.postMessage({kind:'data',data:fixture(w,seq)})});
  if(w.period)inputTimer=setInterval(()=>{stats.inputs++;feed.push(++input)},w.period);
 };
}
window.__cpuStats=()=>({...stats});
window.__cpuStop=()=>{if(inputTimer!==null)clearInterval(inputTimer);if(expiry!==null)clearTimeout(expiry);off?.();feed?.dispose();channel.close()};
`;
const styles = [];
const bundle = await build({ stdin: { resolveDir: root, loader: 'ts', contents: entry }, bundle: true, platform: 'browser', format: 'iife', minify: true, write: false,
  define: { 'process.env.NODE_ENV': '"production"', __VUE_OPTIONS_API__: 'true', __VUE_PROD_DEVTOOLS__: 'false', __VUE_PROD_HYDRATION_MISMATCH_DETAILS__: 'false' },
  plugins: [{ name: 'hud-sfc', setup(b) {
    b.onResolve({ filter: /^@\// }, a => {const p=path.join(root,'src',a.path.slice(2));return {path:fs.existsSync(p)?p:p+'.ts'}});
    b.onLoad({filter:/\.vue$/}, a => {
      const {descriptor}=parse(fs.readFileSync(a.path,'utf8')), id='cpu-hud';
      const compiled=compileScript(descriptor,{id,inlineTemplate:true});
      for(const style of descriptor.styles)styles.push(compileStyle({source:style.content,filename:a.path,id:'data-v-'+id,scoped:style.scoped}).code);
      return {contents:compiled.content.replace('export default', 'const __component =')+`\n__component.__scopeId='data-v-${id}';export default __component;`,loader:'ts',resolveDir:path.dirname(a.path)};
    });
  } }] });
const html = '<!doctype html><meta charset="utf-8"><title>OFFLINE CPU gate — fixture only</title><style>body{background:#263042;margin:0;padding:20px}'+styles.join('\n')+'</style><div id="app"></div><script>'+bundle.outputFiles[0].text.replaceAll('</script','<\\/script')+'</script>';
fs.writeFileSync(path.join(out,'cpu-fixture.html'),html);
const nodeBundle = await build({stdin:{loader:'ts',resolveDir:root,contents:`export * from './src/bridge/monitorHudModel.ts'`},bundle:true,platform:'node',format:'esm',write:false});
const modelFile=path.join(out,'cpu-model.mjs');fs.writeFileSync(modelFile,nodeBundle.outputFiles[0].text);
const model=await import('file:///'+modelFile.replaceAll('\\','/'));
const snapshot={schema:1,session:1,generation:1,seq:1,hwDown:false,top:{ts:100000,tdpW:28.4,tempC:65,cpuUsage:43,freqMhz:4350,gpuPowerW:112,gpuClockMhz:2480,hasBattery:true,batteryPercent:76,chargeW:-32,remainMin:94,hwDown:false},fps:{ts:100000,fps:117,fps1:89,gpu:86}};
const nodeResults=[];
for(const workload of ['uniform-map-serialize','fixture-3-fields','fixture-15-fields-16cores','fixture-29-fields-128cores-120frames']){
 const fixture=model.makeHudFixture();
 if(workload.includes('3-fields')){fixture.values={fps:117,cpuTemp:65,gpuUsage:86};fixture.coreUsage=[];fixture.coreClocks=[];fixture.frameTimes=[];}
 if(workload.includes('128cores')){fixture.coreUsage=Array.from({length:128},(_,i)=>i%101);fixture.coreClocks=Array(128).fill(4.35);fixture.frameTimes=Array(120).fill(8.5);}
 const sample=()=>JSON.stringify(model.boundedHudData(workload==='uniform-map-serialize'?model.hudFromUniformSnapshot(snapshot,100000):fixture));
 for(let i=0;i<3000;i++)sample();
 const batchCpu=[],batchWall=[],startCpu=process.cpuUsage(),startWall=performance.now();let bytes=0;
 const perBatch=5000;
 for(let b=0;b<100;b++){const cpu=process.cpuUsage(),wall=performance.now();for(let i=0;i<perBatch;i++)bytes=sample().length;const delta=process.cpuUsage(cpu);batchCpu.push((delta.user+delta.system)/1000/perBatch);batchWall.push((performance.now()-wall)/perBatch);}
 const delta=process.cpuUsage(startCpu),elapsed=performance.now()-startWall;
 const percentile=(a,p)=>a.slice().sort((a,b)=>a-b)[Math.ceil(a.length*p)-1];
 nodeResults.push({workload,samples:100*perBatch,samplesPerBatch:perBatch,packetBytes:bytes,cpuMsPerSample:(delta.user+delta.system)/1000/(100*perBatch),cpuBatchP95MsPerSample:percentile(batchCpu,.95),wallBatchP95MsPerSample:percentile(batchWall,.95),elapsedMs:elapsed});
}
const runtime='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_monitor-perf.cjs'))('playwright');
const server=createServer((req,res)=>{res.setHeader('Content-Type','text/html;charset=utf-8');res.end(html)});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const url='http://127.0.0.1:'+server.address().port+'/',browserResults=[],errors=[],discardedSamples=[];
let browser,report;
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const context=await browser.newContext({viewport:{width:1920,height:1080}}),child=await context.newPage(),owner=await context.newPage();
 for(const page of [owner,child])page.on('pageerror',e=>errors.push(e.message));
 await child.goto(url+'?role=child');await owner.goto(url+'?role=owner');
 assert.equal(errors.length,0,errors.join('\n'));
 const system=await browser.newBrowserCDPSession();
 const sessions=await Promise.all([owner,child].map(p=>context.newCDPSession(p)));
 for(const cdp of sessions)await cdp.send('Performance.enable');
 const sample=async()=>{
  const [info,...metrics]=await Promise.all([system.send('SystemInfo.getProcessInfo'),...sessions.map(c=>c.send('Performance.getMetrics'))]);
  assert(info.processInfo.some(p=>p.type==='renderer'), 'Renderer process CPU counters required');
  for(const p of info.processInfo)assert(Number.isFinite(p.cpuTime)&&p.cpuTime>=0,'Unavailable CPU counter must not be reported as zero');
  return {clock:performance.now(),processes:info.processInfo,taskSeconds:metrics.reduce((sum,m)=>sum+(m.metrics.find(v=>v.name==='TaskDuration')?.value??NaN),0)};
 };
 for(let round=1;round<=rounds;round++)for(const w of workloads){
  await owner.evaluate(id=>window.__setWorkload(id),w.id);await child.waitForFunction(()=>document.querySelector('[data-monitor-hud]'));
  await new Promise(r=>setTimeout(r,4500));
  if(w.cores){const shape=await child.locator('.hud-core-bars').evaluate(el=>{const r=el.getBoundingClientRect();return {width:r.width,height:r.height,tracks:[...el.children].map(c=>{const b=c.getBoundingClientRect();return {left:b.left,right:b.right}}),left:r.left,right:r.right}});assert.equal(shape.tracks.length,w.cores);assert(shape.tracks.every(t=>t.left>=shape.left-1&&t.right<=shape.right+1),'Every core track must stay within the natural HUD width');}

  let statsBefore,receiveBefore,before,after,statsAfter,receiveAfter,perProcess,ms;
  for(let attempt=1;attempt<=3;attempt++){
   statsBefore=await owner.evaluate(()=>window.__cpuStats());receiveBefore=await child.evaluate(()=>window.__cpuStats());before=await sample();
   await new Promise(r=>setTimeout(r,seconds*1000));after=await sample();
   statsAfter=await owner.evaluate(()=>window.__cpuStats());receiveAfter=await child.evaluate(()=>window.__cpuStats());
   const stable=before.processes.length===after.processes.length&&before.processes.every(p=>after.processes.some(v=>v.id===p.id));
   if(!stable){discardedSamples.push({round,workload:w.id,attempt,reason:'Process set changed; accounting incomplete, not a zero-CPU result',before:before.processes,after:after.processes});console.log('Discarding unstable process window: '+w.id+' #'+attempt);assert(attempt<3,'CPU counters did not stabilize');continue;}
   ms=after.clock-before.clock;perProcess=[];
   for(const p of after.processes){const prev=before.processes.find(v=>v.id===p.id);assert(p.cpuTime>=prev.cpuTime);perProcess.push({id:p.id,type:p.type,cpuMs:(p.cpuTime-prev.cpuTime)*1000})}
   break;
  }
  const totalCpuMs=perProcess.reduce((s,p)=>s+p.cpuMs,0),rendererCpuMs=perProcess.filter(p=>p.type==='renderer').reduce((s,p)=>s+p.cpuMs,0);
  const sent=statsAfter.sent-statsBefore.sent,received=receiveAfter.received-receiveBefore.received;
  if(w.period){assert(sent>=Math.floor(seconds)-2&&sent<=Math.ceil(seconds)+1,'Must measure a real dynamic 1Hz display workload');assert(Math.abs(received-sent)<=1,'Renderer must actually receive the dynamic packets');for(let i=1;i<statsAfter.sentAt.length;i++)assert(statsAfter.sentAt[i]-statsAfter.sentAt[i-1]>=995,'Data sends exceeded 1Hz (5ms scheduler tolerance)')}
  else assert.equal(sent,0);
  const row={round,workload:w.id,label:w.label,elapsedMs:ms,inputs:statsAfter.inputs-statsBefore.inputs,sent,received,totalCpuMs,totalCpuMsPerSecond:totalCpuMs/(ms/1000),totalSingleCorePercent:totalCpuMs/ms*100,rendererSingleCorePercent:rendererCpuMs/ms*100,taskMsPerSecond:(after.taskSeconds-before.taskSeconds)*1000/(ms/1000),perProcess};
  browserResults.push(row);console.log(JSON.stringify(row));
  if(round===1)await child.screenshot({path:path.join(out,'cpu-'+w.id+'.png'),fullPage:true});
 }
 assert.equal(errors.length,0,errors.join('\n'));
 const idle=Math.max(...browserResults.filter(r=>r.workload==='idle').map(r=>r.totalSingleCorePercent));
 report={suite:'Offline YMCC HUD low-CPU gate',date:'2026-10-10',budget,accepted:nodeResults.every(r=>r.cpuBatchP95MsPerSample<=budget.nodeMapAndSerializeP95Ms&&r.wallBatchP95MsPerSample<=budget.nodeMapAndSerializeP95Ms)&&browserResults.every(r=>r.totalSingleCorePercent<=budget.browserWholeProcessSingleCorePercent),environment:{os:os.platform(),cpu:os.cpus()[0].model,logicalCpuCount:os.cpus().length,browser:await browser.version(),viewport:'1920x1080',secondsPerSample:seconds,rounds},nodeResults,browserResults,idleMaxSingleCorePercent:idle,scope:'Synthetic dynamic source with production latest-only gate, real BroadcastChannel transport, bounded packet admission and production Vue HUD. Node uniform mapping uses an in-memory real-schema snapshot. CPU includes all processes of our own headless browser, not the user desktop. NOT real hardware acquisition/native WebView2/Steam/Decky/2077 end-to-end acceptance.',discardedSamples,actualUnifiedHardwareCpuAcceptance:false,actualGameCpuAcceptance:false,counters:'CDP SystemInfo.getProcessInfo cumulative CPU seconds delta, summed across all browser processes. Percent = CPU delta / wall delta * 100, single-core equivalent; NOT divided by logical core count. CDP TaskDuration is supplemental. Node p95 is across 5000-sample batches (larger batches mitigate coarse Windows CPU counters); not a single-sample p95.',errors};
 fs.writeFileSync(path.join(out,'CPU-PERFORMANCE.json'),JSON.stringify(report,null,2));
 console.log(JSON.stringify({accepted:report.accepted,reportPath:path.join(out,'CPU-PERFORMANCE.json'),nodeResults},null,2));
 assert(report.accepted,'Hard low-CPU gate failed; do not loosen it or claim acceptance');
 await owner.evaluate(()=>window.__cpuStop());await child.evaluate(()=>window.__cpuStop());
}catch(e){fs.writeFileSync(path.join(out,'CPU-PERFORMANCE-FAILED.json'),JSON.stringify({error:String(e),budget,nodeResults,browserResults,discardedSamples,errors,accepted:false},null,2));throw e;}
finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
