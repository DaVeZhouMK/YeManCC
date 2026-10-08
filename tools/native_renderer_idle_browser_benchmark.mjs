/** Actual production engine + IPC/lifecycle/serial queue in Chromium.
 * Native traffic is memory-only; no native EXE, hardware, or full YMCC CPU is exercised.
 */
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {createServer} from 'node:http';
const repo=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const req=createRequire(resolve(repo,'package.json'));const {build}=req('esbuild');
const modules=process.env.CPU_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(resolve(modules,'_engine-bench.cjs'))('playwright');
const out=resolve(repo,'Build/Validation/CPU-Native-Renderer-20261003');mkdirSync(out,{recursive:true});
const before=resolve(out,'before/src/gamepad/engine.ts'),after=resolve(repo,'src/gamepad/engine.ts');
const browserBuilds={};
for(const [label,engine] of [['before',before],['after',after]]){
 const fixture=`import {ref} from 'vue';import {startGamepad,stopGamepad} from 'engine-under-test';import './src/bridge/ipc';
 const router={currentRoute:ref({path:'/schedule',fullPath:'/schedule'}),push:async(path)=>{router.currentRoute.value={path,fullPath:path};}};
 let stop=startGamepad({router}),seq=0,timer=null;
 const send=(event,data)=>{for(const listener of window.__nativeListeners)listener({data:{event,data}});};
 window.__engineApi={send,snapshot:(connected=true)=>send('gamepad.state',{connected,name:'memory-only fixture',buttons:[],axes:[0,0,0,0]}),
 action:(action='confirm',id=++seq)=>send('gamepad.ui-input',{action,uiSeq:id,provenance:{session:'renderer-cpu-fixture'}}),
 start:()=>{window.__rafCalls=0;window.__messages=0;timer=setInterval(()=>{send('gamepad.state',{connected:true,axes:[Math.sin(++window.__messages),0,0,0],buttons:[]});},8);},
 stop:()=>{if(timer)clearInterval(timer);timer=null;return {rafCalls:window.__rafCalls,messages:window.__messages};},
 dispose:()=>{stop();stopGamepad();},route:()=>router.currentRoute.value.path};`;
 const result=await build({stdin:{contents:fixture,loader:'js',resolveDir:repo},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':resolve(repo,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'actual-engine-memory-only',setup(b){
  b.onResolve({filter:/^engine-under-test$/},()=>({path:engine}));
  b.onLoad({filter:/[/\\]engine\.ts$/},args=>args.path===engine?{contents:readFileSync(engine,'utf8'),loader:'ts',resolveDir:resolve(repo,'src/gamepad')}:undefined);
  // Route component factories are never mounted by the engine fixture.
  b.onLoad({filter:/\.vue$/},()=>({contents:'export default {};',loader:'js'}));
 }}]});browserBuilds[label]=result.outputFiles[0].text;
}
const bootstrap=`window.__nativeListeners=[];window.__rafCalls=0;window.__messages=0;window.__clicks=0;
 const originalRaf=window.requestAnimationFrame.bind(window);window.requestAnimationFrame=(fn)=>originalRaf((t)=>{window.__rafCalls++;fn(t);});
 window.chrome={webview:{addEventListener:(name,fn)=>window.__nativeListeners.push(fn),postMessage:(request)=>{if(typeof request.id==='number')queueMicrotask(()=>{let result=request.cmd==='fs.read_text'?'{}':{};for(const fn of window.__nativeListeners)fn({data:{id:request.id,result}});});}}};`;
const server=createServer((req,res)=>{const label=req.url.includes('before')?'before':'after';res.setHeader('Content-Type',req.url.endsWith('.js')?'text/javascript':'text/html');res.end(req.url.endsWith('.js')?browserBuilds[label]:`<!doctype html><meta charset="utf-8"><main class="app-content"><button id="one" onclick="window.__clicks++">One</button><button id="two" onclick="window.__clicks++">Two</button></main><script>${bootstrap}</script><script src="/${label}.js"></script>`);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;const windows=[],correctness=[],errors=[];
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.CPU_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 for(let repeat=1;repeat<=3;repeat++){
  for(const label of repeat%2?['before','after']:['after','before']){
   const page=await browser.newPage();page.on('pageerror',e=>errors.push({label,error:String(e)}));
   await page.goto(`http://127.0.0.1:${server.address().port}/${label}`);await page.waitForFunction(()=>window.__engineApi);await page.waitForTimeout(100);
   const cdp=await page.context().newCDPSession(page);await cdp.send('Performance.enable');
   const metrics=async()=>Object.fromEntries((await cdp.send('Performance.getMetrics')).metrics.map(m=>[m.name,m.value]));
   const first=await metrics(),start=performance.now();await page.evaluate(()=>window.__engineApi.start());await page.waitForTimeout(4000);
   const counts=await page.evaluate(()=>window.__engineApi.stop());const last=await metrics(),seconds=(performance.now()-start)/1000;
   windows.push({repeat,label,seconds,...counts,taskSeconds:last.TaskDuration-first.TaskDuration,rendererTaskCores:(last.TaskDuration-first.TaskDuration)/seconds});
   if(label==='before')assert.ok(counts.rafCalls>100,'baseline must reproduce persistent empty frame loop');else assert.equal(counts.rafCalls,0,'native snapshots must not pump empty RAF callbacks');
   if(repeat===1){
    const check=async(name,fn)=>{await fn();correctness.push({label,name,status:'PASS'});};
    await page.focus('#one');
    await check('native-confirm-event-remains-immediate',async()=>{await page.evaluate(()=>window.__engineApi.action());await page.waitForTimeout(60);assert.equal(await page.evaluate(()=>window.__clicks),1);});
    await check('one-shot-action-burst-not-coalesced',async()=>{await page.evaluate(()=>{for(let i=0;i<25;i++)window.__engineApi.action();});await page.waitForTimeout(100);assert.equal(await page.evaluate(()=>window.__clicks),26);});
    await check('queued-action-cancelled-by-native-hidden',async()=>{await page.evaluate(()=>{window.__engineApi.action();window.__engineApi.send('window.hidden',{});});await page.waitForTimeout(60);assert.equal(await page.evaluate(()=>window.__clicks),26);});
    await check('native-shown-restores-event-actions',async()=>{await page.evaluate(()=>{window.__engineApi.send('window.shown',{});window.__engineApi.action();});await page.waitForTimeout(60);assert.equal(await page.evaluate(()=>window.__clicks),27);});
    await check('child-input-owner-suppresses-frontend-action',async()=>{await page.evaluate(()=>{window.__engineApi.send('gamepad.input-owner',{owner:'custom-steam-library'});window.__engineApi.action();});await page.waitForTimeout(60);assert.equal(await page.evaluate(()=>window.__clicks),27);await page.evaluate(()=>window.__engineApi.send('gamepad.input-owner',{owner:'ymcc-frontend'}));});
    await check('test-mode-suppresses-semantic-action',async()=>{await page.evaluate(()=>{window.__engineApi.send('gamepad.testmode',true);window.__engineApi.action();});await page.waitForTimeout(60);assert.equal(await page.evaluate(()=>window.__clicks),27);await page.evaluate(()=>window.__engineApi.send('gamepad.testmode',false));});
    await check('native-page-next-works-without-input-polling',async()=>{await page.evaluate(()=>window.__engineApi.action('page-next'));await page.waitForTimeout(120);assert.equal(await page.evaluate(()=>window.__engineApi.route()),'/fan');});
    await check('stop-engine-cancels-late-semantic-action',async()=>{await page.evaluate(()=>{window.__engineApi.dispose();window.__engineApi.action();});await page.waitForTimeout(60);assert.equal(await page.evaluate(()=>window.__clicks),27);});
   }
   await page.close();
  }
 }
 assert.deepEqual(errors,[]);
 const average=label=>{const w=windows.filter(x=>x.label===label);return w.reduce((sum,x)=>sum+x.rendererTaskCores,0)/w.length;};
 const evidence={status:'PASS',capturedAtUtc:new Date().toISOString(),scope:'actual native-only renderer engine + IPC/lifecycle/serial queue with 125Hz memory-native snapshots; renderer TaskDuration only, not complete YMCC/WebView2 GPU/input CPU',browserVersion:browser.version(),beforeSha256:createHash('sha256').update(readFileSync(before)).digest('hex'),afterSha256:createHash('sha256').update(readFileSync(after)).digest('hex'),means:{before:average('before'),after:average('after'),reductionPercent:(1-average('after')/average('before'))*100},windows,correctness,errors};
 writeFileSync(resolve(out,'native-renderer-idle-ab-evidence.json'),JSON.stringify(evidence,null,2));console.log(JSON.stringify({status:evidence.status,means:evidence.means,windows,correctness},null,2));
}finally{await browser?.close();await new Promise(r=>server.close(r));}