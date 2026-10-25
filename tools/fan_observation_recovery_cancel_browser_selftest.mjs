import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
const root=process.cwd(), req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'), {parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_fan-wake-chain.cjs'))('playwright');
const out=path.resolve(process.env.YMCC_FAN_OBSERVATION_CANCEL_EVIDENCE_DIR||path.join(root,'../../../_scratch/fan-observation-cancel-'+Date.now()));fs.mkdirSync(out,{recursive:true});
const fixture=path.join(root,'tools/fan_view_bridge_wake_fixture.ts');
const mocks={
  '@/bridge/fanHost':`export {fanHostLifecycle,resolveFanEntryAction} from ${JSON.stringify(fixture)};`,
  '@/bridge/topmon':'export const topMonitorData={value:{tempC:55}};',
  '@/bridge/fanFeature':`import ${JSON.stringify(fixture)};export const FAN_FORCE_PREVIEW=false;export function getFanFeatureSettings(){return {configured:true,preset:'balanced',presetCurves:window.__chain.curves,nodes:structuredClone(window.__chain.curves.balanced),motionEnabled:false};}export function getFanPresetCurve(n){return structuredClone(window.__chain.curves[n]);}export async function recordFanHandshake(){}export async function saveFanCurve(){}export function setFanDiagnosticLoggingEnabled(){}export function setFanControlActive(v){window.__chain.lastControlActive=v;}export function setFanNavigationDuty(v){window.__chain.lastNavDuty=v;}export async function setFanMotionEnabled(){}`,
  '@/bridge/settingsRepository':'export async function readSettingsSection(){return {};}export async function saveSettingsSection(){}',
};
const styles=[];
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:"import {createApp} from 'vue';import Page from './src/views/FanView.vue';createApp(Page).mount('#app');"},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'real-fan-chain-with-platform-boundaries',setup(b){
 if(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE)b.onLoad({filter:/src[\\/]bridge[\\/]fanHost\.ts$/},a=>({contents:fs.readFileSync(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE,'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));

  b.onResolve({filter:/.*/},a=>a.path in mocks?{path:a.path,namespace:'ui-boundary'}:undefined);
  b.onLoad({filter:/.*/,namespace:'ui-boundary'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:root}));
  b.onLoad({filter:/\.vue$/},a=>{const pageSource=process.env.YMCC_FAN_VIEW_SOURCE_BEFORE&&/[\\/]FanView\.vue$/.test(a.path)?process.env.YMCC_FAN_VIEW_SOURCE_BEFORE:a.path;const {descriptor,errors}=parse(fs.readFileSync(pageSource,'utf8'),{filename:a.path});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8),code=compileScript(descriptor,{id,inlineTemplate:true});for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}return {contents:code.content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(a.path)};});
}}]});
const js=result.outputFiles[0].text,css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html=`<!doctype html><meta charset="utf-8"><title>Real Fan UI and lifecycle wake chain</title><style>${css}</style><div id="app"></div><script src="/chain.js"></script>`;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/chain.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/chain.js'?js:html);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));


const errors=[],cases=[];let browser,page;
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(()=>{const original=window.setInterval.bind(window),clear=window.clearInterval.bind(window),ticks=new Map();window.__fanPageObservationTicks=ticks;window.setInterval=(fn,ms,...args)=>{const id=original(fn,ms,...args);if(ms===5000&&typeof fn==='function')ticks.set(id,()=>fn(...args));return id;};window.clearInterval=id=>{ticks.delete(id);clear(id);};});
 const snapshot=()=>page.evaluate(()=>({pressed:document.querySelector('.fan-toggle').getAttribute('aria-pressed'),text:document.querySelector('.fan-toggle').textContent,disabled:document.querySelector('.fan-toggle').disabled,notice:document.querySelector('.fan-status-banner')?.textContent,globalActive:window.__chain.lastControlActive,writes:window.__chain.boundary.writes,intent:window.__chain.lifecycle.hasControlIntent,recovery:window.__chain.lifecycle.recoveryActive,enables:window.__chain.trace.filter(t=>t.op==='enable').length}));
 for(const shape of ['error','not-ready'])for(const action of ['new-manual','new-bridge-curve','off','hide']){
  await page.goto('http://127.0.0.1:'+server.address().port+'/?family=ROGAlly&cold=1');await page.waitForSelector('.fan-toggle');
  await page.evaluate(()=>{const f=window.__chain;f.setNative(6,'ready');f.lifecycle.setPowerGeneration(6);f.lifecycle.observePowerBoundary('resume-ready',6);});
  await page.locator('.fan-toggle').click();await page.waitForFunction(()=>document.querySelector('.fan-toggle').getAttribute('aria-pressed')==='true'&&!document.querySelector('.fan-toggle').disabled,null,{timeout:7000});
  await page.evaluate(shape=>{
   const f=window.__chain,original=f.boundary.getState.bind(f.boundary),readPower=f.lifecycle.readNativePowerState.bind(f.lifecycle);let read=true,native=true;
   const test=window.__observationCancel={entered:false,release:null};
   f.boundary.getState=async()=>{if(read){read=false;if(shape==='error')throw new Error('fixture current GET error');return {...f.boundary.snapshot(),hardwareWritesEnabled:false,hardwareWrites:false};}return original();};
   f.lifecycle.readNativePowerState=async()=>{if(native){native=false;test.entered=true;return new Promise(resolve=>{test.release=()=>resolve({...f.native});});}return readPower();};
   for(const tick of window.__fanPageObservationTicks.values())tick();
  },shape);
  await page.waitForFunction(()=>window.__observationCancel.entered);
  if(action==='new-manual'){
   await page.locator('.fan-toggle').click();await page.waitForFunction(()=>document.querySelector('.fan-toggle').getAttribute('aria-pressed')==='true',null,{timeout:7000});
  }else if(action==='new-bridge-curve'){
   await page.evaluate(async()=>window.__chain.lifecycle.apply(window.__chain.curves.aggressive));
  }else if(action==='off'){
   await page.evaluate(async()=>{await window.__chain.lifecycle.disable();window.dispatchEvent(new CustomEvent('fan:guard-state',{detail:{active:false,state:'awaiting-control'}}));});
  }else{
   await page.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'));});
  }
  const before=await snapshot();await page.evaluate(()=>window.__observationCancel.release());await page.waitForTimeout(120);const after=await snapshot();
  let ok=true,error='';try{
   assert.equal(after.pressed,before.pressed,'late observation changed current control button');assert.equal(after.globalActive,before.globalActive,'late observation changed the current global control mirror');assert.equal(after.notice,before.notice,'late observation overwrote the current notice');
   assert.equal(after.enables,before.enables,'obsolete observation reissued a curve');
   if(action==='new-manual'||action==='new-bridge-curve'){assert.equal(after.writes,true);if(action==='new-manual')assert.equal(after.pressed,'true');assert.equal(after.recovery,false,'obsolete observation created an owner after newer manual success');}
   if(action==='off'){assert.equal(after.intent,false);assert.equal(after.writes,false);assert.equal(after.recovery,false,'OFF was followed by a newly registered recovery');}
  }catch(e){ok=false;error=String(e);}
  cases.push({shape,action,ok,error,before,after});
 }
 assert.deepEqual(errors,[]);const result={ok:cases.every(c=>c.ok)&&!errors.length,checks:cases.length,cases,pageErrors:errors,hardwareCalls:0,scope:'actual FanView + actual bridge; held native read after current failed/not-ready GET; actual manual DOM or public OFF/visibility; inert Host and native; no device or CPU benchmark'};
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({ok:result.ok,checks:result.checks,failed:cases.filter(c=>!c.ok).map(c=>c.shape+'/'+c.action+': '+c.error),evidenceRoot:out}));if(!result.ok)process.exitCode=1;
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
