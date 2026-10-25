import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
const root=process.cwd(),req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_fan-nav.cjs'))('playwright');
const out=path.resolve(process.env.YMCC_FAN_NAV_EVIDENCE_DIR||'G:/YeManCC-Work/_scratch/FAN-971-ROG-SIDEBAR-WAKE-20261013/browser');
fs.mkdirSync(out,{recursive:true});
const fixture=path.join(root,'tools/fan_view_bridge_wake_fixture.ts');
const normalized=p=>p.replaceAll('\\','/');
const mocks={
 [normalized(path.join(root,'src/bridge/settingsRepository.ts'))]:`export async function readSettingsSection(section){if(section==='fan')return {featureEnabled:true,configured:true,preset:'balanced',motionEnabled:true,diagnosticLoggingEnabled:false,nodes:window.__chain.curves.balanced,presetCurves:window.__chain.curves};return {};}export async function saveSettingsSection(){};`,
 [normalized(path.join(root,'src/bridge/topmon.ts'))]:`import {ref} from 'vue';export const topMonitorData=ref({tempC:55});window.__monitorTemperature=value=>{topMonitorData.value={tempC:value}};`,
 [normalized(path.join(root,'src/bridge/performanceSchedule.ts'))]:`export async function loadPerformanceSchedule(){return {enabled:false}};export function onPerformanceScheduleChanged(){return ()=>{}};`,
 [normalized(path.join(root,'src/router.ts'))]:`export const ROUTES=[{path:'/tdp',title:'TDP',icon:'tdp'},{path:'/fan',title:'风扇控制',icon:'fan'},{path:'/button-mapping',title:'控制器',icon:'gamepad'}];`,
};
const code=`import {createApp,h,KeepAlive,defineComponent} from 'vue';
import {createRouter,createMemoryHistory} from 'vue-router';
import FanView from './src/views/FanView.vue';import NavRail from './src/components/NavRail.vue';
import * as navigation from './src/bridge/fanFeature';
import {fanHostLifecycle} from ${JSON.stringify(fixture)};
window.__navigation=navigation;
window.__fanControlDisplay=(active,curve)=>{(window.__displayEvents??=[]).push({active,curve});const consume=navigation['setFanNavigationControl'];if(consume)consume(active,curve)};
const Dummy=defineComponent({name:'Dummy',setup:()=>()=>h('div',{id:'not-fan-page'},'Other page')});
const routes=['/tdp','/fan','/button-mapping'].map(path=>({path,component:Dummy}));
const router=createRouter({history:createMemoryHistory(),routes});
const Shell=defineComponent({setup:()=>()=>h('div',[h(NavRail),h(KeepAlive,null,{default:()=>router.currentRoute.value.path==='/fan'?h(FanView):h(Dummy)})])});
await navigation.initializeFanFeature();await router.push('/tdp');await router.isReady();
createApp(Shell).use(router).mount('#app');window.__go=p=>router.push(p);window.__testReady=true;`;
const styles=[];
const built=await build({stdin:{resolveDir:root,loader:'ts',contents:code},bundle:true,write:false,format:'esm',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'fan-nav-platform-boundaries',setup(b){
 b.onResolve({filter:/.*/},a=>{
  if(a.path==='@/bridge/fanHost')return {path:'fixture-exports',namespace:'mock'};
  let p=a.path.startsWith('@/')?path.join(root,'src',a.path.slice(2)):a.path.startsWith('.')?path.resolve(a.resolveDir,a.path):a.path;
  if(!p.endsWith('.ts'))p+='.ts';p=normalized(p);
  if(p in mocks)return {path:p,namespace:'mock'};
 });
 b.onLoad({filter:/.*/,namespace:'mock'},a=>({contents:a.path==='fixture-exports'?`export {fanHostLifecycle,resolveFanEntryAction} from ${JSON.stringify(fixture)};`:mocks[a.path],loader:'ts',resolveDir:root}));
 if(process.env.YMCC_FAN_NAV_SOURCE_BEFORE)b.onLoad({filter:/src[\\/]bridge[\\/]fan(Host|Feature)\.ts$/},a=>({contents:fs.readFileSync(path.join(process.env.YMCC_FAN_NAV_SOURCE_BEFORE,path.relative(root,a.path)),'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));
 b.onLoad({filter:/\.vue$/},a=>{
  const before=process.env.YMCC_FAN_NAV_SOURCE_BEFORE&&a.path.endsWith('NavRail.vue')?path.join(process.env.YMCC_FAN_NAV_SOURCE_BEFORE,path.relative(root,a.path)):null;
  const source=fs.readFileSync(before||a.path,'utf8'),{descriptor,errors}=parse(source,{filename:a.path});assert.equal(errors.length,0);
  const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8),compiled=compileScript(descriptor,{id,inlineTemplate:true});
  for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code)}
  return {contents:compiled.content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(a.path)};
 });
}}]});
const js=built.outputFiles[0].text,html=`<!doctype html><meta charset="utf-8"><style>${styles.join('\n')}</style><div id="app"></div><script>window.__liveIntervals=new Map();const realSet=window.setInterval.bind(window),realClear=window.clearInterval.bind(window);window.setInterval=(fn,ms,...args)=>{const id=realSet(fn,ms,...args);window.__liveIntervals.set(id,ms);return id};window.clearInterval=id=>{window.__liveIntervals.delete(id);realClear(id)};</script><script type="module" src="/test.js"></script>`;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/test.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/test.js'?js:html)});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const cases=[],errors=[];let browser,page,failed;
const test=async(name,fn)=>{await fn();cases.push(name)};
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
 for(const family of ['ROGAlly','ClawCG3EM','GPDWin5']){
  await page.goto('http://127.0.0.1:'+server.address().port+'/?family='+family);await page.waitForFunction(()=>window.__testReady);
  const glyph=page.locator('.nav-item').filter({hasText:'风扇控制'}).locator('svg');
  const spins=()=>glyph.evaluate(el=>el.classList.contains('spinning'));
  const spinReady=()=>page.waitForFunction(()=>[...document.querySelectorAll('.nav-item')].find(e=>e.textContent.includes('风扇控制'))?.querySelector('svg')?.classList.contains('spinning'),null,{timeout:1200});
  await test(family+': cold navigation has no Host/HTTP/control and does not spin',async()=>{assert.deepEqual(await page.evaluate(()=>window.__chain.trace),[]);assert.equal(await spins(),false);assert.equal(await page.locator('.fan-page').count(),0)});
  await test(family+': background start without FanView spins after actual enable receipt',async()=>{await page.evaluate(()=>window.__chain.lifecycle.apply(window.__chain.curves.balanced));await spinReady();assert.equal(await page.locator('.fan-page').count(),0);assert.equal(await page.evaluate(()=>window.__navigation.fanControlActive.value),true)});
  await test(family+': committed curve and existing monitor drive speed without new requests',async()=>{const before=await page.evaluate(()=>window.__chain.trace.length);assert.equal(await page.evaluate(()=>window.__navigation.fanNavigationDuty.value),33);await page.evaluate(()=>window.__monitorTemperature(80));await page.waitForFunction(()=>window.__navigation.fanNavigationDuty.value===60);assert.equal(await page.evaluate(()=>window.__chain.trace.length),before);assert(Math.abs(await glyph.evaluate(e=>parseFloat(e.style.getPropertyValue('--fan-spin-duration')))-1.12)<1e-9)});
  await test(family+': unchanged receipt deduplicates display and adds no timer',async()=>{const before=await page.evaluate(()=>window.__displayEvents.length);assert.deepEqual(await page.evaluate(()=>[...window.__liveIntervals.values()]),[5000]);await page.evaluate(async()=>{for(let i=0;i<10;i++)await window.__chain.lifecycle.getState()});assert.equal(await page.evaluate(()=>window.__displayEvents.length),before);assert.deepEqual(await page.evaluate(()=>[...window.__liveIntervals.values()]),[5000])});
  await test(family+': inactive UI and mirror setters cannot erase current receipt',async()=>{await page.evaluate(()=>{window.__navigation.setFanControlActive(false);window.__navigation.setFanNavigationDuty(0)});assert.equal(await spins(),true);assert.equal(await page.evaluate(()=>window.__navigation.fanNavigationDuty.value),60)});
  await test(family+': sleep stops glyph immediately and wake resumes on another page',async()=>{await page.evaluate(()=>window.__chain.sleep(1));assert.equal(await spins(),false);await page.evaluate(()=>window.__chain.autoWake());await spinReady();assert.equal(await page.locator('.fan-page').count(),0)});
  await test(family+': missed suspend edge still retires display on resuming',async()=>{await page.evaluate(()=>{const l=window.__chain.lifecycle;l.observePowerBoundary('resuming',l.coordinatorSnapshot.powerGeneration)});assert.equal(await spins(),false);await page.evaluate(async()=>{const l=window.__chain.lifecycle;l.observePowerBoundary('resume-ready',l.coordinatorSnapshot.powerGeneration);await l.resume()});await spinReady()});
  await test(family+': motion preference is preserved over wake and does not write hardware',async()=>{const before=await page.evaluate(()=>window.__chain.trace.length);await page.evaluate(()=>window.__navigation.setFanMotionEnabled(false));assert.equal(await spins(),false);assert.equal(await page.evaluate(()=>window.__chain.trace.length),before);await page.evaluate(()=>window.__chain.sleep(2));await page.evaluate(()=>window.__chain.autoWake());assert.equal(await spins(),false);await page.evaluate(()=>window.__navigation.setFanMotionEnabled(true));await spinReady()});
  await test(family+': KeepAlive/page switching preserves glyph DOM and animation',async()=>{await glyph.evaluate(e=>window.__originalGlyph=e);await page.evaluate(()=>window.__go('/fan'));await page.waitForFunction(()=>document.querySelector('.fan-page'));await page.evaluate(()=>window.__go('/tdp'));await page.waitForFunction(()=>document.querySelector('#not-fan-page'));await spinReady();assert.equal(await glyph.evaluate(e=>e===window.__originalGlyph),true);await page.evaluate(()=>window.__chain.sleep(3));assert.equal(await spins(),false);await page.evaluate(()=>window.__chain.autoWake());await spinReady();assert.equal(await page.evaluate(()=>window.__navigation.fanControlActive.value),true)});
  await test(family+': OFF stays off after late read and one manual ON remains reachable',async()=>{await page.evaluate(()=>{window.__chain.holdAt='state';window.__heldState=window.__chain.lifecycle.getState()});await page.waitForFunction(()=>window.__chain.gateReached==='state');await page.evaluate(()=>{window.__off=window.__chain.lifecycle.disable()});assert.equal(await spins(),false);await page.evaluate(()=>window.__chain.releaseGate());await page.evaluate(async()=>{await window.__heldState;await window.__off});assert.equal(await spins(),false);await page.evaluate(()=>window.__chain.lifecycle.apply(window.__chain.curves.aggressive));await spinReady();assert.equal(await page.evaluate(()=>window.__navigation.fanNavigationDuty.value),Math.round(75+(100-75)*(80-70)/30))});
  await test(family+': existing inactive receipt cannot show Ready-only handshake as active',async()=>{await page.evaluate(async()=>{window.__chain.boundary.writes=false;await window.__chain.lifecycle.getState()});assert.equal(await spins(),false);await page.evaluate(()=>window.__chain.lifecycle.apply(window.__chain.curves.balanced));await spinReady();await page.evaluate(()=>window.__chain.lifecycle.close());assert.equal(await spins(),false)});
 }
 assert.deepEqual(errors,[]);
 const app=fs.readFileSync(path.join(root,'src/bridge/fanHost.ts'),'utf8');
 assert(app.includes('onControlDisplay: setFanNavigationControl'),'production singleton binds the same display consumer');
 await page.screenshot({path:path.join(out,'navigation-closed.png')});
}catch(e){failed=e;console.error(e.stack)}finally{
 const result={ok:!failed,passed:cases.length,cases,browserErrors:errors,failure:failed?.message,hardware:'fixture-only-no-real-Host-or-device',productionUi:['NavRail.vue','FanView.vue','fanFeature.ts','fanHost.ts'],sourceBefore:process.env.YMCC_FAN_NAV_SOURCE_BEFORE||null};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));
 await browser?.close();await new Promise(r=>server.close(r));console.log(JSON.stringify({ok:result.ok,passed:result.passed,failure:result.failure}));
}
if(failed)process.exitCode=1;
