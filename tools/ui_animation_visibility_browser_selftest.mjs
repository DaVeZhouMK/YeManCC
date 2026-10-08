/** Real Chromium correctness checks for production CSS + real UI lifecycle.
 * This is not a whole-App / WebView2 CPU benchmark and performs no hardware I/O.
 */
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
const repo=resolve(dirname(fileURLToPath(import.meta.url)), '..');
const req=createRequire(resolve(repo,'package.json'));
const { build }=req('esbuild');
const { parse,compileScript,compileStyle }=req('vue/compiler-sfc');
const moduleRoot=process.env.CPU_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium }=createRequire(resolve(moduleRoot,'_ui-animation-test.cjs'))('playwright');
const appPath=resolve(repo,'src/App.vue'),app=readFileSync(appPath,'utf8');
// Check the production source wiring as well as the browser/CSS behavior below.
assert.match(app,/const uiVisible = ref\(isUiVisible\(\)\)/);
assert.match(app,/stopUiVisibility = onUiVisibilityChange\(\(\{ visible \}\) => \{\s*uiVisible\.value = visible;/);
assert.match(app,/stopUiVisibility\?\.\(\)/);
const rootTag=app.match(/<div class="app-stage"[^>]*>/)?.[0];
assert.ok(rootTag?.includes(':data-ui-visible="uiVisible"'));
const {descriptor}=parse(app,{filename:appPath});
const scope='data-v-ui-animation-test';
const css=[];
for(const style of descriptor.styles){
 const result=compileStyle({source:style.content,filename:appPath,id:scope,scoped:style.scoped});
 assert.equal(result.errors.length,0);css.push(result.code);
}
for(const file of ['src/components/NavRail.vue','src/components/TopMonitorBar.vue']){
 const {descriptor:component}=parse(readFileSync(resolve(repo,file),'utf8'));
 for(const style of component.styles){const result=compileStyle({source:style.content,filename:file,id:'test',scoped:false});assert.equal(result.errors.length,0);css.push(result.code);}
}
// Same root attribute and lifecycle wiring; no normal App startup or API mocks.
const fixture=`<script setup lang="ts">
import {ref,onMounted,onUnmounted,nextTick} from 'vue';
import {isUiVisible,onUiVisibilityChange} from './src/bridge/uiLifecycle';
const uiVisible=ref(isUiVisible());
const scalerStyle={width:'720px',height:'420px',zoom:1,position:'static'};
let stopUiVisibility: (()=>void)|null=null;
window.__listenerUpdates=0;
onMounted(()=>{stopUiVisibility=onUiVisibilityChange(({visible})=>{uiVisible.value=visible;window.__listenerUpdates++;});});
onUnmounted(()=>{stopUiVisibility?.();stopUiVisibility=null;});
window.__fixture={flush:nextTick};
</script><template>${rootTag}
 <span class="nav-icon"><svg class="spinning" width="24" height="24"><path d="M2 2L20 20"/></svg></span>
 <div class="pseudo-test"></div>
</div></template>`;
const {descriptor:fixtureDescriptor}=parse(fixture,{filename:'ui-fixture.vue'});
let fixtureScript=compileScript(fixtureDescriptor,{id:scope,inlineTemplate:true}).content;
fixtureScript=fixtureScript.replace('export default','const __sfc__ =')+`\n__sfc__.__scopeId=${JSON.stringify(scope)}; export default __sfc__;`;
const bundle=await build({stdin:{contents:`import {createApp} from 'vue';import Component from 'ui-test-component';window.__app=createApp(Component);window.__app.mount('#app');`,loader:'js',resolveDir:repo},bundle:true,write:false,platform:'browser',format:'iife',define:{'process.env.NODE_ENV':'"production"'},alias:{'@':resolve(repo,'src')},plugins:[{name:'production-style-fixture',setup(b){b.onResolve({filter:/^ui-test-component$/},()=>({path:'ui-test-component',namespace:'fixture'}));b.onLoad({filter:/.*/,namespace:'fixture'},()=>({contents:fixtureScript,loader:'ts',resolveDir:repo}));}}]});
css.push('.pseudo-test::before {content:""; display:block; width:10px; height:10px; animation:nav-fan-spin 1.1s linear infinite;}');
const html=`<!doctype html><meta charset="utf-8"><style>${css.join('\n')}</style><div id="app"></div><script src="/fixture.js"></script>`;
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript':'text/html');response.end(request.url==='/fixture.js'?bundle.outputFiles[0].text:html);});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const checks=[],errors=[];let browser;
const output=resolve(repo,'Build/Validation/CPU-Frontend-20261003');mkdirSync(output,{recursive:true});
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.CPU_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage();page.on('pageerror',e=>errors.push(String(e)));
 await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>window.__fixture);await page.waitForTimeout(100);
 const read=()=>page.evaluate(()=>({documentVisible:document.visibilityState==='visible',visible:document.querySelector('.app-stage').dataset.uiVisible,icon:getComputedStyle(document.querySelector('.spinning')).animationPlayState,pseudo:getComputedStyle(document.querySelector('.pseudo-test'),'::before').animationPlayState,times:document.querySelector('.app-stage').getAnimations({subtree:true}).map(a=>Number(a.currentTime)),updates:window.__listenerUpdates}));
 const send=async(name)=>{await page.evaluate(async(name)=>{window.dispatchEvent(new CustomEvent(name));await window.__fixture.flush();},name);await page.waitForTimeout(30);};
 let state=await read();assert.equal(state.visible,'true');assert.equal(state.icon,'running');assert.equal(state.pseudo,'running');assert.ok(state.times.length>=2);checks.push('initial-visible-animation-running');
 await send('ipc:window.hidden');state=await read();assert.equal(state.documentVisible,true);assert.equal(state.visible,'false');assert.equal(state.icon,'paused');assert.equal(state.pseudo,'paused');
 const paused=state.times;await page.waitForTimeout(300);const stopped=await read();assert.equal(stopped.times.length,paused.length);for(let i=0;i<paused.length;i++)assert.ok(Math.abs(stopped.times[i]-paused[i])<1,'hidden animation clock must stop');checks.push('native-hidden-pauses-svg-and-pseudo-with-document-still-visible');
 await send('ipc:window.shown');await page.waitForTimeout(120);state=await read();assert.equal(state.icon,'running');for(let i=0;i<paused.length;i++)assert.ok(state.times[i]>paused[i]+50,'resume must continue old clock, not restart');checks.push('native-shown-resumes-original-phase');
 for(const [off,on] of [['ipc:window.minimized','ipc:window.restored'],['ipc:window.hidden','ipc:window.maximized'],['ipc:window.hidden','ipc:window.summoned']]){await send(off);assert.equal((await read()).icon,'paused');await send(on);assert.equal((await read()).icon,'running');checks.push(off+'->'+on);}
 await send('ipc:power.suspending');assert.equal((await read()).icon,'paused');await send('ipc:window.shown');assert.equal((await read()).icon,'paused');await send('ipc:power.resuming');assert.equal((await read()).icon,'paused');await send('ipc:power.resumed');assert.equal((await read()).icon,'running');checks.push('power-not-ready-cannot-resume-from-window-shown');
 await page.evaluate(async()=>{Object.defineProperty(document,'visibilityState',{configurable:true,get:()=>window.__docVisibility});window.__docVisibility='hidden';document.dispatchEvent(new Event('visibilitychange'));await window.__fixture.flush();});assert.equal((await read()).icon,'paused');
 await page.evaluate(async()=>{window.__docVisibility='visible';document.dispatchEvent(new Event('visibilitychange'));await window.__fixture.flush();});assert.equal((await read()).icon,'running');checks.push('document-hidden-and-visible');
 await page.evaluate(()=>window.__app.unmount());const updates=await page.evaluate(()=>window.__listenerUpdates);await send('ipc:window.hidden');await send('ipc:window.shown');assert.equal(await page.evaluate(()=>window.__listenerUpdates),updates);checks.push('disposed-component-does-not-receive-visibility-updates');assert.deepEqual(errors,[]);
 const evidence={status:'PASS',capturedAtUtc:new Date().toISOString(),scope:'real uiLifecycle + production App/NavRail CSS + source wiring checks in a minimal Vue fixture; not whole YMCC or CPU measurement',browserVersion:browser.version(),appSha256:createHash('sha256').update(app).digest('hex'),checks,errors};
 writeFileSync(resolve(output,'ui-animation-visibility-evidence.json'),JSON.stringify(evidence,null,2));console.log(JSON.stringify(evidence,null,2));
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}