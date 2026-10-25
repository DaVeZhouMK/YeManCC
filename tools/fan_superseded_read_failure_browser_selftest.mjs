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
const out=path.resolve(process.env.YMCC_FAN_STALE_FAILURE_EVIDENCE_DIR||path.join(root,'../../../_scratch/fan-stale-read-failure-'+Date.now()));fs.mkdirSync(out,{recursive:true});
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
  b.onLoad({filter:/\.vue$/},a=>{const {descriptor,errors}=parse(fs.readFileSync(a.path,'utf8'),{filename:a.path});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8),code=compileScript(descriptor,{id,inlineTemplate:true});for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}return {contents:code.content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(a.path)};});
}}]});
const js=result.outputFiles[0].text,css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html=`<!doctype html><meta charset="utf-8"><title>Real Fan UI and lifecycle wake chain</title><style>${css}</style><div id="app"></div><script src="/chain.js"></script>`;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/chain.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/chain.js'?js:html);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));

const errors=[],cases=[];let browser,page;
try {
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(()=>{
  const original=window.setInterval.bind(window),clear=window.clearInterval.bind(window),ticks=new Map();
  window.__fanPageObservationTicks=ticks;
  window.setInterval=(handler,ms,...args)=>{const id=original(handler,ms,...args);if(ms===5000&&typeof handler==='function')ticks.set(id,()=>handler(...args));return id;};
  window.clearInterval=id=>{ticks.delete(id);clear(id);};
 });
 const snapshot=()=>page.evaluate(()=>({text:document.querySelector('.fan-toggle').textContent,pressed:document.querySelector('.fan-toggle').getAttribute('aria-pressed'),disabled:document.querySelector('.fan-toggle').disabled,recoveryRequests:window.__readFailure.recoveryRequests,enableCount:window.__chain.trace.filter(t=>t.op==='enable').length,hasIntent:window.__chain.lifecycle.hasControlIntent,writes:window.__chain.boundary.writes,ui:document.body.innerText}));
 for(const family of ['ROGAlly','ClawCG3EM','GPDWin5']){
  await page.goto('http://127.0.0.1:'+server.address().port+'/?family='+family);
  await page.waitForSelector('.fan-toggle');await page.locator('.fan-toggle').click();
  await page.waitForFunction(()=>document.querySelector('.fan-toggle').getAttribute('aria-pressed')==='true'&&!document.querySelector('.fan-toggle').disabled,null,{timeout:7000});
  await page.evaluate(()=>{
   const f=window.__chain,original=f.boundary.getState.bind(f.boundary);let hold=true;let reject;
   const state=window.__readFailure={entered:false,recoveryRequests:0,reject:null};
   f.boundary.getState=async()=>{if(hold){hold=false;state.entered=true;return new Promise((_,r)=>{reject=r;state.reject=()=>reject(new Error('fixture delayed previous telemetry timeout'));});}return original();};
   const recover=f.lifecycle.requestRecoveryAfterObservedHostState.bind(f.lifecycle);
   f.lifecycle.requestRecoveryAfterObservedHostState=(reason)=>{state.recoveryRequests++;return recover(reason);};
   for(const tick of window.__fanPageObservationTicks.values())tick();
  });
  await page.waitForFunction(()=>window.__readFailure.entered);
  await page.evaluate(async()=>window.__chain.lifecycle.apply(window.__chain.curves.aggressive));
  const before=await snapshot();assert.equal(before.pressed,'true');assert.equal(before.writes,true);
  await page.evaluate(()=>window.__readFailure.reject());await page.waitForTimeout(150);
  const after=await snapshot();let ok=true,error='';
  try {assert.equal(after.recoveryRequests,0,'obsolete GET error triggered a fresh recovery after later control committed');assert.equal(after.pressed,'true','obsolete GET error revoked confirmed UI');assert.equal(after.text,'风扇已开启');assert.equal(after.enableCount,before.enableCount,'obsolete GET error replayed the curve');assert.equal(after.writes,true);assert.equal(after.hasIntent,true);}
  catch(e){ok=false;error=String(e);}
  cases.push({family,ok,error,before,after});
  if(ok){await page.locator('.fan-toggle').click();await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent);}
 }
 // A current error still belongs to this observation and must retain recovery.
 await page.goto('http://127.0.0.1:'+server.address().port+'/?family=ROGAlly');
 await page.waitForSelector('.fan-toggle');await page.locator('.fan-toggle').click();
 await page.waitForFunction(()=>document.querySelector('.fan-toggle').getAttribute('aria-pressed')==='true'&&!document.querySelector('.fan-toggle').disabled,null,{timeout:7000});
 await page.evaluate(()=>{
  const f=window.__chain,original=f.boundary.getState.bind(f.boundary);let hold=true;
  const state=window.__readFailure={entered:false,recoveryRequests:0,reject:null};
  f.boundary.getState=async()=>{if(hold){hold=false;state.entered=true;return new Promise((_,reject)=>{state.reject=()=>reject(new Error('fixture current telemetry timeout'));});}return original();};
  const recover=f.lifecycle.requestRecoveryAfterObservedHostState.bind(f.lifecycle);
  f.lifecycle.requestRecoveryAfterObservedHostState=(reason)=>{state.recoveryRequests++;return recover(reason);};
  for(const tick of window.__fanPageObservationTicks.values())tick();
 });
 await page.waitForFunction(()=>window.__readFailure.entered);await page.evaluate(()=>window.__readFailure.reject());
 await page.waitForFunction(()=>window.__readFailure.recoveryRequests>0);await page.waitForTimeout(150);
 const current=await snapshot();assert.equal(current.recoveryRequests,1,'current failure must request its existing recovery owner');assert.equal(current.hasIntent,true);assert.equal(current.disabled,false,'current read error cannot permanently lock the manual switch');
 cases.push({family:'ROGAlly current-error responsibility',ok:true,after:current});
 assert.deepEqual(errors,[]);
 const final={ok:cases.every(c=>c.ok)&&errors.length===0,checks:cases.length,cases,pageErrors:errors,hardwareCalls:0,scope:'actual FanView and actual FanHostLifecycle; held old telemetry error plus newer committed control; Host/native/platform fixtures; no CPU benchmark'};
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(final,null,2));console.log(JSON.stringify({ok:final.ok,checks:final.checks,failed:cases.filter(c=>!c.ok).map(c=>c.family+': '+c.error),evidenceRoot:out}));if(!final.ok)process.exitCode=1;
} finally {if(browser)await browser.close();await new Promise(r=>server.close(r));}
