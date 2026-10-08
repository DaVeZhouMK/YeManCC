/** Real GyroMotionView + all real child SFCs / KeepAlive / IPC / UI lifecycle.
 * Memory IPC only. No product/native Host, physical sensor, controller, EC or
 * calibration is run. CDP TaskDuration is renderer evidence, not total YMCC CPU.
 */
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
const repo = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const req = createRequire(resolve(repo,'package.json'));
const {build}=req('esbuild');
const {parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const moduleRoot=process.env.CPU_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(resolve(moduleRoot,'_gyro-bench.cjs'))('playwright');
const evidenceRoot=resolve(repo,'Build/Validation/CPU-Gyro-Presentation-20261004');
const out=resolve(process.env.CPU_GYRO_BENCH_OUT || resolve(evidenceRoot,'runs',new Date().toISOString().replace(/[:.]/g,'-')));
const before=resolve(process.env.CPU_GYRO_BENCH_BEFORE || resolve(evidenceRoot,'before/src/views/GyroMotionView.vue')), after=resolve(repo,'src/views/GyroMotionView.vue');
const rate=Number(process.env.CPU_GYRO_BENCH_RATE_HZ||12.5),noSensor=process.env.CPU_GYRO_BENCH_NO_SENSOR==='1';
assert.ok(rate>0&&rate<=250,'invalid synthetic rate');
if(!existsSync(before))throw new Error('exact saved before SFC required; do not invent/rebuild the baseline');
mkdirSync(out,{recursive:true});
const bootstrap=`
window.__nativeListeners=[];window.__commands=[];window.__benchCounts={messages:0,vueUpdates:0,rafRequests:0,timerStarts:0,timerClears:0,lateTimerStarts:0};
window.__disposed=false;window.__gyroElement=null;window.__fixtureNoSensor=${noSensor};
const raf=requestAnimationFrame.bind(window);window.requestAnimationFrame=(fn)=>{window.__benchCounts.rafRequests++;return raf(fn)};
const timeout=window.setTimeout.bind(window),clear=window.clearTimeout.bind(window),tracked=new Map();
window.setTimeout=(fn,ms,...args)=>{const id=timeout(fn,ms,...args);if(ms>0&&ms<=1100){window.__benchCounts.timerStarts++;tracked.set(id,true);if(window.__disposed)window.__benchCounts.lateTimerStarts++}return id};
window.clearTimeout=id=>{if(tracked.has(id)){window.__benchCounts.timerClears++;tracked.delete(id)}return clear(id)};
window.chrome={webview:{addEventListener:(_,fn)=>window.__nativeListeners.push(fn),postMessage:r=>{
 window.__commands.push({cmd:r.cmd,args:r.args});let result,error;
 if(r.cmd==='fs.exists')result=/yeman-settings\\.json$/.test(r.args.path);
 else if(r.cmd==='fs.readTextFile')result=/yeman-settings\\.json$/.test(r.args.path)?JSON.stringify(window.__settings):'';
 else if(r.cmd==='settings.write'){window.__settings=JSON.parse(r.args.content);result=true;}
 else if(r.cmd==='gyro.calibrate.status')result={status:'not-calibrated',trusted:false};
 else if(r.cmd==='gyro.calibrate')result={ok:true};
 else if(r.cmd==='diagnostics.frontendError')result=true;
 else error='MEMORY_FIXTURE_NOT_IMPLEMENTED:'+r.cmd;
 queueMicrotask(()=>{for(const fn of window.__nativeListeners)fn(new MessageEvent('message',{data:error?{id:r.id,error}:{id:r.id,result}}))});
}}};`;
const fixture=component=>`
import {createApp,defineComponent,h,ref,KeepAlive,nextTick} from 'vue';
import Component from ${JSON.stringify(component)};
import {normalizeSettings} from './src/bridge/settingsRepository';
import {on} from './src/bridge/ipc';
import './src/bridge/uiLifecycle';
window.__settings=normalizeSettings({schemaVersion:1});
let route=ref('gyro'),mounted=ref(true),timer=null,sequence=0;
const send=(event,data)=>{for(const fn of window.__nativeListeners)fn(new MessageEvent('message',{data:{event,data}}))};
on('gyro.telemetry',data=>window.dispatchEvent(new CustomEvent('input:motion-telemetry',{detail:data})));
const Empty=defineComponent({render:()=>h('div','other route')});
const Third=defineComponent({render:()=>h('div','evict cached gyro')});
createApp({render:()=>mounted.value?h(KeepAlive,{max:2},{default:()=>route.value==='gyro'?h(Component,{key:'gyro'}):route.value==='other'?h(Empty,{key:'other'}):h(Third,{key:'third'})}):h('div','disposed')}).mount('#app');
const payload=(x=1,sensor=true)=>({sequence:++sequence,timestampUtc:sensor?new Date(1790985600000+sequence*8).toISOString():'',sensorPresent:sensor,gyro:{x:sensor?x:0,y:sensor?x/2:0,z:sensor?-x/3:0},accel:{x:0,y:0,z:sensor?1:0},motionContribution:{x:sensor?Math.sin(sequence/13)*.6:0,y:sensor?Math.cos(sequence/13)*.6:0},hostFrame:{hostActive:false,hostSubmission:'unavailable',lifecycle:'fixture'},motionAdmission:'unavailable'});
window.__gyroApi={
 frame:(x=1,sensor=true)=>send('gyro.telemetry',payload(x,sensor)),
 malformed:()=>send('gyro.telemetry',null),
 send,
 activate:async value=>{route.value=value;await nextTick()},
 visibility:value=>send(value?'window.shown':'window.hidden',{}),
 documentVisibility:value=>{Object.defineProperty(document,'visibilityState',{configurable:true,get:()=>value?'visible':'hidden'});document.dispatchEvent(new Event('visibilitychange'))},
 start:rate=>{if(timer)clearInterval(timer);window.__benchCounts={messages:0,vueUpdates:0,rafRequests:0,timerStarts:0,timerClears:0,lateTimerStarts:0};const push=()=>{send('gyro.telemetry',payload(Math.sin(sequence/11)*50,!window.__fixtureNoSensor));window.__benchCounts.messages++};push();timer=setInterval(push,1000/rate)},
 stop:async()=>{if(timer)clearInterval(timer);timer=null;await nextTick();return {...window.__benchCounts}},
 element:()=>window.__gyroElement,
 dispose:async()=>{if(timer)clearInterval(timer);timer=null;mounted.value=false;await nextTick();window.__disposed=true},
 flush:nextTick,
};
`;
const builds={};
for(const [label,path] of [['before',before],['after',after]]){
 const css=[];
 const built=await build({stdin:{contents:fixture(path),resolveDir:repo,loader:'js'},bundle:true,write:false,format:'iife',platform:'browser',define:{'process.env.NODE_ENV':'"production"'},alias:{'@':resolve(repo,'src')},nodePaths:[resolve(repo,'node_modules')],plugins:[{name:'real-sfc-with-update-counter',setup(build){build.onLoad({filter:/\.vue$/},args=>{
  const source=readFileSync(args.path,'utf8'),id=createHash('sha1').update(args.path).digest('hex').slice(0,8);
  const {descriptor,errors}=parse(source,{filename:args.path});if(errors.length)throw errors[0];
  let script=compileScript(descriptor,{id,inlineTemplate:true}).content;
  if(args.path===path){
   script="import {onUpdated as __benchUpdated,onMounted as __benchMounted,getCurrentInstance as __benchInstance} from 'vue';\n"+script;
   const expression=/setup\(__props[^)]*\)\s*\{/;
   assert.match(script,expression,'production root counter injection anchor missing');
   script=script.replace(expression,"$&\nconst __benchSelf=__benchInstance();__benchMounted(()=>{window.__gyroElement=__benchSelf.subTree.el});__benchUpdated(()=>{window.__benchCounts.vueUpdates++});");
  }
  for(const style of descriptor.styles){const result=compileStyle({source:style.content,filename:args.path,id:'data-v-'+id,scoped:style.scoped});if(result.errors.length)throw result.errors[0];css.push(result.code)}
  script=script.replace('export default','const __sfc__ =')+'\n__sfc__.__scopeId='+JSON.stringify('data-v-'+id)+';export default __sfc__;';
  return {contents:script,loader:'ts',resolveDir:dirname(args.path)};
 })}}]});
 builds[label]={js:built.outputFiles[0].text,css:css.join('\n')};writeFileSync(resolve(out,`browser-${label}.js`),builds[label].js);
}
const server=createServer((request,response)=>{
 const label=request.url?.startsWith('/before')?'before':'after';
 if(request.url?.endsWith('.js')){response.setHeader('Content-Type','text/javascript');response.end(builds[label].js);return}
 response.setHeader('Content-Type','text/html');response.end(`<html><head><style>body{margin:0;background:#141923;color:#ddd;font:13px Arial}.page{padding:14px}${builds[label].css}</style></head><body><div id="app"></div><script>${bootstrap}</script><script src="/${label}.js"></script></body></html>`);
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;const windows=[],correctness=[],errors=[];
const seconds=Number(process.env.CPU_GYRO_BENCH_SECONDS||4),repeats=Number(process.env.CPU_GYRO_BENCH_REPEATS||3);
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.CPU_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',args:['--disable-background-networking','--disable-component-update']});
 const browserSession=await browser.newBrowserCDPSession();
 const processInfo=async()=>{try{return (await browserSession.send('SystemInfo.getProcessInfo')).processInfo}catch{return null}};
 const ready=async(label)=>{const page=await browser.newPage({viewport:{width:1100,height:850}});page.on('pageerror',error=>errors.push({label,error:String(error)}));await page.goto(`http://127.0.0.1:${server.address().port}/${label}`);await page.waitForFunction(()=>window.__gyroApi&&window.__gyroElement);await page.waitForTimeout(350);return page};
 for(let repeat=1;repeat<=repeats;repeat++)for(const kind of ['active','cached','hidden'])for(const label of repeat%2?['before','after']:['after','before']){
  const page=await ready(label);
  await page.evaluate(noSensor=>window.__gyroApi.frame(noSensor?0:1,!noSensor),noSensor);await page.waitForTimeout(50);
  if(kind==='cached')await page.evaluate(()=>window.__gyroApi.activate('other'));
  if(kind==='hidden')await page.evaluate(()=>window.__gyroApi.visibility(false));
  const cdp=await page.context().newCDPSession(page);await cdp.send('Performance.enable');
  const metrics=async()=>Object.fromEntries((await cdp.send('Performance.getMetrics')).metrics.map(m=>[m.name,m.value]));
  const initial=await metrics(),processBefore=await processInfo(),start=performance.now();
  await page.evaluate(rate=>window.__gyroApi.start(rate),rate);await page.waitForTimeout(seconds*1000);
  const counts=await page.evaluate(()=>window.__gyroApi.stop());await page.waitForTimeout(35);
  const last=await metrics(),processAfter=await processInfo(),elapsedSeconds=(performance.now()-start)/1000;
  const beforeMap=new Map(processBefore?.map(p=>[p.id,p])||[]);
  const processDeltas=processAfter?.filter(p=>beforeMap.has(p.id)).map(p=>({id:p.id,type:p.type,cpuSeconds:p.cpuTime-beforeMap.get(p.id).cpuTime,cpuCores:(p.cpuTime-beforeMap.get(p.id).cpuTime)/elapsedSeconds}))||null;
  const row={repeat,label,kind,syntheticRateHz:rate,syntheticSensorEvidence:!noSensor,elapsedSeconds,...counts,rendererTaskCores:(last.TaskDuration-initial.TaskDuration)/elapsedSeconds,rendererScriptCores:(last.ScriptDuration-initial.ScriptDuration)/elapsedSeconds,browserProcessDeltas:processDeltas,processDeltaScope:'auxiliary headless Edge process counters, NOT YMCC/WebView2 total'};
  assert.ok(counts.messages>=seconds*rate*.7,'synthetic source throttled; invalid comparison');
  if(label==='after'&&kind!=='active'){assert.equal(counts.rafRequests,0,`${kind} gyro page scheduled presentation RAF`);assert.ok(counts.vueUpdates<=2,`${kind} should only have bounded presence/config housekeeping updates`)}
  if(label==='before'&&kind!=='active')assert.ok(counts.rafRequests>seconds*rate*.7,'baseline did not reproduce offscreen presentation loop');
  windows.push(row);console.log(JSON.stringify(row));await page.close();
 }
 for(const label of ['before','after']){
  const page=await ready(label);
  const check=async(name,fn)=>{await fn();correctness.push({label,name,status:'PASS'})};
  const text=()=>page.evaluate(()=>window.__gyroElement.querySelector('.chip b').textContent);
  await check('visible-XYZ-real-SFC',async()=>{await page.evaluate(()=>window.__gyroApi.frame(12.3,true));await page.waitForTimeout(60);assert.equal(await text(),'12.3')});
  await check('burst-preserves-final-frame',async()=>{await page.evaluate(()=>{for(let i=1;i<=100;i++)window.__gyroApi.frame(i,true)});await page.waitForTimeout(60);assert.equal(await text(),'100.0')});
  await check('cached-page-source-continues-but-rendering-gated',async()=>{await page.evaluate(()=>window.__gyroApi.activate('other'));await page.evaluate(()=>window.__gyroApi.frame(17.1,true));await page.waitForTimeout(70);assert.equal(await text(),label==='after'?'100.0':'17.1')});
  await check('activation-restores-only-latest',async()=>{await page.evaluate(()=>{window.__gyroApi.frame(18,true);window.__gyroApi.frame(19,true)});await page.evaluate(()=>window.__gyroApi.activate('gyro'));await page.waitForTimeout(70);assert.equal(await text(),'19.0')});
  await check('native-hidden-does-not-lose-latest',async()=>{await page.evaluate(()=>{window.__gyroApi.visibility(false);window.__gyroApi.frame(23,true)});await page.waitForTimeout(60);assert.equal(await text(),label==='after'?'19.0':'23.0');await page.evaluate(()=>window.__gyroApi.visibility(true));await page.waitForTimeout(60);assert.equal(await text(),'23.0')});
  await check('document-hidden-resume-latest',async()=>{await page.evaluate(()=>{window.__gyroApi.documentVisibility(false);window.__gyroApi.frame(24,true)});await page.waitForTimeout(60);assert.equal(await text(),label==='after'?'23.0':'24.0');await page.evaluate(()=>window.__gyroApi.documentVisibility(true));await page.waitForTimeout(60);assert.equal(await text(),'24.0')});
  await check('stale-cached-sensor-receipt-cannot-be-fresh-on-resume',async()=>{await page.evaluate(()=>window.__gyroApi.activate('other'));await page.evaluate(()=>window.__gyroApi.frame(25,true));await page.waitForTimeout(1300);await page.evaluate(()=>window.__gyroApi.activate('gyro'));await page.waitForTimeout(70);const state=await page.evaluate(()=>window.__gyroElement.querySelector('.gyro-power-btn').textContent.trim());assert.equal(state,'陀螺仪未识别')});
  await check('zero-no-sensor-and-malformed-frame-not-connected',async()=>{await page.evaluate(()=>{window.__gyroApi.frame(0,false);window.__gyroApi.malformed()});await page.waitForTimeout(60);assert.equal(await page.evaluate(()=>window.__gyroElement.querySelector('.gyro-power-btn').textContent.trim()),'陀螺仪未识别')});
  await check('connection-expiry-is-one-second-after-last-proven-receipt',async()=>{await page.evaluate(()=>window.__gyroApi.frame(30,true));await page.waitForTimeout(500);await page.evaluate(()=>window.__gyroApi.frame(31,true));await page.waitForTimeout(650);assert.equal(await page.evaluate(()=>window.__gyroElement.querySelector('.gyro-power-btn').textContent.trim()),'陀螺仪已关闭');await page.waitForTimeout(500);assert.equal(await page.evaluate(()=>window.__gyroElement.querySelector('.gyro-power-btn').textContent.trim()),'陀螺仪未识别')});
  await check('calibration-result-consumer-not-paused-by-page-cache',async()=>{await page.getByRole('button',{name:'展开',exact:true}).click();await page.getByRole('button',{name:'开始校准',exact:true}).click();await page.waitForTimeout(50);await page.evaluate(()=>window.__gyroApi.activate('other'));await page.evaluate(()=>window.__gyroApi.send('gyro.calibrate.result',{ok:true,saved:true,confidence:1,weight:1,offset:{x:0,y:0,z:0}}));await page.waitForTimeout(70);assert.equal(await page.evaluate(()=>window.__gyroElement.querySelector('.calib-btn').textContent.trim()),'开始校准');assert.ok(await page.evaluate(()=>window.__gyroElement.querySelector('.calib-already-calibrated')))});
  await check('eviction-removes-telemetry-listener',async()=>{await page.evaluate(()=>window.__gyroApi.activate('third'));const count=await page.evaluate(()=>window.__benchCounts.rafRequests);await page.evaluate(()=>window.__gyroApi.frame(40,true));await page.waitForTimeout(70);assert.equal(await page.evaluate(()=>window.__benchCounts.rafRequests),count)});
  await page.close();
  const queued=await ready(label);
  await queued.evaluate(async()=>{window.__gyroApi.frame(50,true);await window.__gyroApi.dispose()});await queued.waitForTimeout(70);
  const late=await queued.evaluate(()=>window.__benchCounts.lateTimerStarts);
  if(label==='after')assert.equal(late,0,'queued presentation callback recreated sensor timer after unmount');else assert.ok(late>0,'baseline failed to reproduce late RAF/timer after unmount');
  correctness.push({label,name:'unmount-cancels-pending-RAF-and-no-late-sensor-timer',status:'PASS',lateTimerStarts:late});await queued.close();
 }
 assert.deepEqual(errors,[]);
 const means=Object.fromEntries(['active','cached','hidden'].map(kind=>{const mean=label=>{const rows=windows.filter(r=>r.kind===kind&&r.label===label);return rows.reduce((s,r)=>s+r.rendererTaskCores,0)/rows.length};return[kind,{before:mean('before'),after:mean('after'),reductionPercent:(1-mean('after')/mean('before'))*100}]}));
 const evidence={status:'PASS',capturedAtUtc:new Date().toISOString(),scope:'real GyroMotionView and all real child SFCs in KeepAlive; real IPC/UI lifecycle, memory-native synthetic telemetry at recorded rate; no native hardware, no entire YMCC CPU claim',browserVersion:browser.version(),syntheticRateHz:rate,syntheticSensorEvidence:!noSensor,nativeCadenceSource:"native/main.cpp g_inputCaptureLastUi >= 80ms; 12.5Hz normal cadence",beforeSha256:createHash('sha256').update(readFileSync(before)).digest('hex'),afterSha256:createHash('sha256').update(readFileSync(after)).digest('hex'),means,windows,correctness,errors};
 writeFileSync(resolve(out,'gyro-presentation-browser-ab-evidence.json'),JSON.stringify(evidence,null,2));console.log(JSON.stringify({status:evidence.status,means,correctness},null,2));
}catch(error){writeFileSync(resolve(out,'browser-attempt-error.json'),JSON.stringify({capturedAtUtc:new Date().toISOString(),error:String(error),windows,correctness,errors},null,2));throw error}
finally{await browser?.close();await new Promise(resolve=>server.close(resolve))}