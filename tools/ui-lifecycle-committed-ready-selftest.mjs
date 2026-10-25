import assert from 'node:assert/strict';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {createServer} from 'node:http';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const req=createRequire(path.join(repo,'package.json'));
const {transformSync,build}=req('esbuild');
const args=process.argv.slice(2),unitOnly=args.includes('--unit-only');
const at=args.indexOf('--evidence-dir'),out=at>=0?path.resolve(args[at+1]):null;
if(at>=0&&!args[at+1])throw Error('Missing --evidence-dir');
if(out){const rel=path.relative(repo,out);if(rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep)))throw Error('Evidence must be outside source');mkdirSync(out,{recursive:true});}
const sha=p=>createHash('sha256').update(readFileSync(p)).digest('hex');
const inputs=['src/bridge/uiLifecycle.ts','src/App.vue','src/bridge/gyroPresentation.ts','src/components/NavRail.vue','tools/ui-lifecycle-committed-ready-selftest.mjs'];
const hashes=Object.fromEntries(inputs.map(p=>[p,sha(path.join(repo,p))]));
const code=transformSync(readFileSync(path.join(repo,inputs[0]),'utf8'),{loader:'ts',format:'cjs',target:'es2022'}).code;
const app=readFileSync(path.join(repo,'src/App.vue'),'utf8');
const cases=[],browserCases=[];
class TrackedTarget extends EventTarget{
 listeners=new Map();
 addEventListener(n,f,o){let s=this.listeners.get(n);if(!s)this.listeners.set(n,s=new Set());s.add(f);super.addEventListener(n,f,o);}
 removeEventListener(n,f,o){this.listeners.get(n)?.delete(f);super.removeEventListener(n,f,o);}
 count(){return [...this.listeners.values()].reduce((n,s)=>n+s.size,0);}
}
function environment(){
 const window=new TrackedTarget(),document=new TrackedTarget();document.visibilityState='visible';
 const module={exports:{}};const context=vm.createContext({window,document,module,exports:module.exports,Event,CustomEvent,Promise,console,setTimeout(){throw Error('UI reconciliation must own no timer');},setInterval(){throw Error('No poller allowed');}});
 vm.runInContext(code,context);const api=module.exports;
 return {api,window,document,event(n,g){window.dispatchEvent(new CustomEvent(n,{detail:g===undefined?{}:{generation:g}}));},state:()=>JSON.parse(JSON.stringify(api.getUiVisibilityState()))};
}
const flush=async()=>{for(let i=0;i<16;i++)await Promise.resolve();};
const snapshot=(generation=1,phase='ready',extra={})=>({generation,phase,inputReady:phase==='ready',hardwareWritesAllowed:phase==='ready',...extra});
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};}
function reader(){const pending=[];let calls=0;return {pending,get calls(){return calls;},read(){calls++;const d=deferred();pending.push(d);return d.promise;},async reply(v){assert.ok(pending.length,'Expected an outstanding read');pending.shift().resolve(v);await flush();}};}
async function boot(){const e=environment(),r=reader(),owner=e.api.startUiPowerReconciliation(()=>r.read());await flush();await r.reply(snapshot());return {...e,r,owner};}
function block(e,g=2){e.event('ipc:power.suspending',g);e.event('ipc:power.resuming',g);assert.equal(e.state().powerReady,false);}
async function test(name,fn){try{await fn();cases.push({name,pass:true});}catch(error){cases.push({name,pass:false,error:String(error.stack||error)});}}
await test('cold renderer binds the native committed generation',async()=>{const e=await boot();assert.equal(e.state().powerGeneration,1);assert.equal(e.state().visible,true);e.owner.stop();});
await test('mount reuses the already requested lifecycle promise',async()=>{const e=environment(),r=reader(),d=deferred(),owner=e.api.startUiPowerReconciliation(()=>r.read(),d.promise);await flush();assert.equal(r.calls,0);d.resolve(snapshot(4));await flush();assert.equal(e.state().powerGeneration,4);assert.equal(r.calls,0);owner.stop();});
await test('resume-ready with resuming snapshot cannot reopen UI',async()=>{const e=await boot();block(e);e.event('ipc:power.resume-ready',2);await flush();await e.r.reply(snapshot(2,'resuming'));assert.equal(e.state().visible,false);e.owner.stop();});
await test('missing resumed event recovers only on same-generation committed snapshot',async()=>{const e=await boot();let fabricated=0;e.window.addEventListener('ipc:power.resumed',()=>fabricated++);block(e);e.event('ipc:power.resume-ready',2);await flush();await e.r.reply(snapshot(2,'resuming'));e.owner.reconcile();await flush();await e.r.reply(snapshot(2));assert.equal(e.state().visible,true);assert.equal(fabricated,0);e.owner.stop();});
await test('show edge recovers a lost committed notification',async()=>{const e=await boot();block(e);e.event('ipc:window.hidden');e.event('ipc:window.shown');await flush();await e.r.reply(snapshot(2));assert.equal(e.state().visible,true);e.owner.stop();});
await test('old reply cannot reopen a newer suspend',async()=>{const e=await boot();block(e);e.event('ipc:power.resume-ready',2);await flush();e.event('ipc:power.suspending',3);await e.r.reply(snapshot(2));assert.equal(e.state().powerGeneration,3);assert.equal(e.state().visible,false);e.owner.stop();});
await test('same-generation repeated boundary invalidates the in-flight read',async()=>{const e=await boot();block(e);e.owner.reconcile();await flush();e.event('ipc:power.suspending',2);await e.r.reply(snapshot(2));assert.equal(e.state().powerReady,false);e.owner.stop();});
for(const g of [1,0,-1,1.5,NaN,Infinity,'2'])await test('snapshot generation rejected: '+String(g),async()=>{const e=await boot();block(e);e.owner.reconcile();await flush();await e.r.reply(snapshot(g));assert.equal(e.state().visible,false);assert.equal(e.state().powerGeneration,2);e.owner.stop();});
for(const extra of [{inputReady:false},{hardwareWritesAllowed:false},{inputReady:'true'},{hardwareWritesAllowed:undefined}])await test('phase Ready alone is insufficient: '+JSON.stringify(extra),async()=>{const e=await boot();block(e);e.owner.reconcile();await flush();await e.r.reply(snapshot(2,'ready',extra));assert.equal(e.state().powerReady,false);e.owner.stop();});
for(const value of [null,{},snapshot(2,'unknown')])await test('malformed or unknown snapshot stays closed: '+JSON.stringify(value),async()=>{const e=await boot();block(e);e.owner.reconcile();await flush();await e.r.reply(value);assert.equal(e.state().visible,false);e.owner.stop();});
await test('untagged final event is only a getter trigger',async()=>{const e=await boot();block(e);e.event('ipc:power.resumed');assert.equal(e.state().powerReady,false);await flush();await e.r.reply(snapshot(2,'resuming'));assert.equal(e.state().visible,false);e.event('ipc:window.focus');await flush();await e.r.reply(snapshot(2));assert.equal(e.state().visible,true);e.owner.stop();});
await test('old final notification cannot reopen a new generation',async()=>{const e=await boot();block(e,3);const n=e.r.calls;e.event('ipc:power.resumed',2);await flush();assert.equal(e.state().powerReady,false);assert.equal(e.r.calls,n);e.owner.stop();});
await test('read rejection needs another edge and does not retry forever',async()=>{const e=await boot();block(e);e.owner.reconcile();await flush();e.r.pending.shift().reject(Error('bounded native timeout'));await flush();const n=e.r.calls;await flush();assert.equal(e.r.calls,n);assert.equal(e.state().powerReady,false);e.event('ipc:window.focus');await flush();await e.r.reply(snapshot(2));assert.equal(e.state().visible,true);e.owner.stop();});
await test('synchronous getter exception is contained',async()=>{const e=environment();e.event('ipc:power.suspending',2);const owner=e.api.startUiPowerReconciliation(()=>{throw Error('reader throw');});await flush();assert.equal(e.state().powerReady,false);owner.stop();});
await test('many edges coalesce into one active read plus one follow-up',async()=>{const e=await boot();block(e);e.owner.reconcile();await flush();const n=e.r.calls;for(let i=0;i<100;i++)e.event('ipc:window.focus');assert.equal(e.r.calls,n);await e.r.reply(snapshot(2,'resuming'));assert.equal(e.r.calls,n+1);await e.r.reply(snapshot(2));await flush();assert.equal(e.r.calls,n+1);assert.equal(e.state().visible,true);e.owner.stop();});
await test('native hidden state stays hidden after committed Ready',async()=>{const e=await boot();block(e);e.event('ipc:window.hidden');e.owner.reconcile();await flush();await e.r.reply(snapshot(2));assert.equal(e.state().powerReady,true);assert.equal(e.state().visible,false);e.document.dispatchEvent(new Event('visibilitychange'));await flush();if(e.r.pending.length)await e.r.reply(snapshot(2));assert.equal(e.state().nativeWindowVisible,false);assert.equal(e.state().visible,false);e.owner.stop();});
await test('hidden document stays hidden after committed Ready',async()=>{const e=await boot();block(e);e.document.visibilityState='hidden';e.document.dispatchEvent(new Event('visibilitychange'));e.owner.reconcile();await flush();await e.r.reply(snapshot(2));assert.equal(e.state().visible,false);const n=e.r.calls;e.document.visibilityState='visible';e.document.dispatchEvent(new Event('visibilitychange'));await flush();assert.equal(e.r.calls,n);assert.equal(e.state().visible,true);e.owner.stop();});
await test('stop discards late reply, queued retry and late resume event',async()=>{const e=await boot();block(e);e.owner.reconcile();await flush();e.event('ipc:window.focus');e.owner.stop();e.owner.stop();const n=e.r.calls;await e.r.reply(snapshot(2));e.event('ipc:window.focus');e.event('ipc:power.resumed',2);await flush();assert.equal(e.state().powerReady,false);assert.equal(e.r.calls,n);});
await test('new root owner replaces the old reader without listener growth',async()=>{const e=environment(),r1=reader(),r2=reader(),n=e.window.count()+e.document.count();const one=e.api.startUiPowerReconciliation(()=>r1.read());await flush();const two=e.api.startUiPowerReconciliation(()=>r2.read());await flush();await r2.reply(snapshot(2));await r1.reply(snapshot(1));one.stop();assert.equal(e.state().powerGeneration,2);assert.equal(e.window.count()+e.document.count(),n);e.event('ipc:power.suspending',3);e.event('ipc:power.resume-ready',3);await flush();await r2.reply(snapshot(3));assert.equal(e.state().visible,true);two.stop();});
await test('late same-generation resuming cannot undo observed commit',async()=>{const e=await boot();block(e);e.owner.reconcile();await flush();await e.r.reply(snapshot(2));e.event('ipc:power.resuming',2);assert.equal(e.state().visible,true);e.owner.stop();});
await test('a missed entire generation binds newer authoritative committed state',async()=>{const e=await boot();block(e);e.owner.reconcile();await flush();await e.r.reply(snapshot(3));assert.equal(e.state().powerGeneration,3);assert.equal(e.state().visible,true);e.owner.stop();});
await test('even a newer reply cannot cross a newer local suspend boundary',async()=>{const e=await boot();block(e);e.owner.reconcile();await flush();e.event('ipc:power.suspending',4);await e.r.reply(snapshot(5));assert.equal(e.state().powerGeneration,4);assert.equal(e.state().visible,false);e.owner.stop();});
await test('App owns getter lifecycle and checks after commit',async()=>{assert.match(app,/startUiPowerReconciliation\(\s*\(\) => powerLifecycle\.get\(1500\), lifecycleAtMountPromise/);assert.match(app,/completeResume: \(resumeGeneration, meta\) => powerLifecycle\.completeResume\(resumeGeneration, meta\)\.then[\s\S]*?result\.ok && resumeLifecycleActive && latestResumeGeneration === resumeGeneration[\s\S]*?uiPowerReconciliation\?\.reconcile\(\)/);assert.match(app,/onUnmounted\(\(\) => \{\s*uiPowerReconciliation\?\.stop\(\);\s*uiPowerReconciliation = null/);assert.doesNotMatch(readFileSync(path.join(repo,inputs[0]),'utf8'),/setInterval|setTimeout|power\.resumeComplete|\binvoke\(/);});
if(!unitOnly&&!cases.some(c=>!c.pass)){
 const {parse,compileStyle}=req('vue/compiler-sfc');const css=[];const id='data-v-ui-power-selftest';
 for(const name of ['src/App.vue','src/components/NavRail.vue']){const {descriptor}=parse(readFileSync(path.join(repo,name),'utf8'),{filename:name});for(const s of descriptor.styles){const c=compileStyle({source:s.content,filename:name,id,scoped:!!s.scoped});assert.equal(c.errors.length,0);css.push(c.code);}}
 const entry=`import {startUiPowerReconciliation,onUiVisibilityChange,getUiVisibilityState} from './src/bridge/uiLifecycle';import {createGyroPresentationGate} from './src/bridge/gyroPresentation';
const queue=[];let reads=0,presented=0;const gate=createGyroPresentationGate({present:()=>presented++});gate.setActive(true);onUiVisibilityChange(s=>{document.querySelector('.app-stage').dataset.uiVisible=String(s.visible);gate.setVisible(s.visible);});const owner=startUiPowerReconciliation(()=>{reads++;return new Promise(resolve=>queue.push(resolve));});window.__powerFixture={event:(n,g)=>window.dispatchEvent(new CustomEvent(n,{detail:{generation:g}})),reply:v=>{if(!queue.length)throw Error('No pending read');queue.shift()(v);},push:()=>gate.push({},performance.now()),state:()=>({...getUiVisibilityState(),reads,presented,queued:queue.length,animation:getComputedStyle(document.querySelector('svg')).animationPlayState}),stop:()=>{owner.stop();gate.dispose();}};`;
 const bundle=await build({stdin:{contents:entry,loader:'ts',resolveDir:repo},bundle:true,write:false,format:'iife',platform:'browser'});
 const html=`<!doctype html><meta charset="utf-8"><style>${css.join('\n')}</style><div class="app-stage" ${id} data-ui-visible="true"><span class="nav-icon" ${id}><svg ${id} class="spinning" width="24" height="24"></svg></span></div><script src="/fixture.js"></script>`;
 const server=createServer((q,s)=>{s.setHeader('content-type',q.url==='/fixture.js'?'text/javascript':'text/html');s.end(q.url==='/fixture.js'?bundle.outputFiles[0].text:html);});await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const modules=process.env.CPU_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';const {chromium}=createRequire(path.resolve(modules,'_ui-power-selftest.cjs'))('playwright');let browser;
 try{
  browser=await chromium.launch({headless:true,executablePath:process.env.CPU_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  for(const height of [720,800]){
   const page=await browser.newPage({viewport:{width:1280,height}});const errors=[];page.on('pageerror',e=>errors.push(String(e)));await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>window.__powerFixture?.state().queued===1);
   const reply=async v=>{await page.evaluate(v=>window.__powerFixture.reply(v),v);await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));};
   await reply(snapshot());await page.evaluate(()=>window.__powerFixture.push());await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));let state=await page.evaluate(()=>window.__powerFixture.state());assert.equal(state.presented,1);assert.equal(state.animation,'running');
   await page.evaluate(()=>{const f=window.__powerFixture;f.event('ipc:power.suspending',2);f.event('ipc:power.resuming',2);f.event('ipc:power.resume-ready',2);f.push();});await page.waitForFunction(()=>window.__powerFixture.state().queued===1);await reply(snapshot(2,'resuming'));state=await page.evaluate(()=>window.__powerFixture.state());assert.equal(state.presented,1);assert.equal(state.animation,'paused');
   await page.evaluate(()=>window.__powerFixture.event('ipc:window.shown'));await page.waitForFunction(()=>window.__powerFixture.state().queued===1);await reply(snapshot(2));state=await page.evaluate(()=>window.__powerFixture.state());assert.equal(state.presented,2);assert.equal(state.animation,'running');assert.equal(state.visible,true);
   await page.evaluate(()=>{const f=window.__powerFixture;f.event('ipc:power.suspending',3);f.event('ipc:power.resume-ready',3);});await page.waitForFunction(()=>window.__powerFixture.state().queued===1);await page.evaluate(()=>window.__powerFixture.event('ipc:power.suspending',4));await reply(snapshot(3));state=await page.evaluate(()=>window.__powerFixture.state());assert.equal(state.visible,false);assert.equal(state.animation,'paused');
   await page.evaluate(()=>window.__powerFixture.stop());assert.deepEqual(errors,[]);browserCases.push({height,width:1280,pass:true,actualProductionModules:['uiLifecycle.ts','gyroPresentation.ts'],actualProductionCss:['App.vue','NavRail.vue'],fixtureBoundary:'CSS/RAF correctness; not whole-App or physical input',errors});await page.close();
  }
 }finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
 browserCases.push({name:'owned browser and server closed',pass:(!browser||!browser.isConnected())&&!server.listening});
}
for(const [name,h]of Object.entries(hashes))assert.equal(sha(path.join(repo,name)),h,'Source changed during tests: '+name);
const report={status:cases.every(c=>c.pass)&&browserCases.every(c=>c.pass)?'pass':'fail',unitCases:cases.length,browserCases,cases,sourceHashes:hashes,hardwareBoundary:'memory-only / no target IPC, devices or installed app',noPoller:true,physicalAcceptance:false};
if(out)writeFileSync(path.join(out,'ui-lifecycle-committed-ready-selftest.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({status:report.status,unitCases:cases.length,browserCases:browserCases.length,failures:cases.filter(c=>!c.pass)},null,2));if(report.status!=='pass')process.exitCode=1;