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
const out=path.resolve(process.env.YMCC_FAN_POSTWRITE_EVIDENCE_DIR||path.join(root,'../../../_scratch/fan-observation-cancel-'+Date.now()));fs.mkdirSync(out,{recursive:true});
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
try {
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
 // Keep all timeout/queue logic real. The two resident 5s observations can be
 // invoked explicitly so their unrelated timing cannot hide the stale reply.
 await page.addInitScript(()=>{const old=window.setInterval.bind(window),clear=window.clearInterval.bind(window),ticks=new Map();window.__testTicks=ticks;window.setInterval=(fn,ms,...args)=>{if(ms===5000){const id=old(()=>{},600000);ticks.set(id,()=>fn(...args));return id;}return old(fn,ms,...args);};window.clearInterval=id=>{ticks.delete(id);clear(id);};});
 const snap=()=>page.evaluate(()=>({pressed:document.querySelector('.fan-toggle').getAttribute('aria-pressed'),text:document.querySelector('.fan-toggle').textContent,disabled:document.querySelector('.fan-toggle').disabled,notice:document.querySelector('.fan-status-banner')?.textContent,globalActive:window.__chain.lastControlActive,writes:window.__chain.boundary.writes,intent:window.__chain.lifecycle.hasControlIntent,recovery:window.__chain.lifecycle.recoveryActive,state:window.__chain.lifecycle.state,trace:window.__chain.trace.map(x=>x.op)}));
 const plans=[];
 for(const family of ['ROGAlly','ClawCG3EM'])for(const shape of ['resolve','reject'])for(const action of ['heartbeat-failure','new-curve'])plans.push({family,shape,action});
 for(const shape of ['resolve','reject'])for(const action of ['unchanged','newer-read','power-with-ui','off'])plans.push({family:'ClawCG3EM',shape,action});
 for(const {family,shape,action} of plans) {
  await page.goto('http://127.0.0.1:'+server.address().port+'/?family='+family+'&cold=1');await page.waitForFunction(()=>document.querySelector('.fan-toggle')&&!document.querySelector('.fan-toggle').disabled);
  await page.evaluate(()=>{const f=window.__chain;f.setNative(6,'ready');f.boundary.resumeGeneration=6;f.lifecycle.setPowerGeneration(6);f.lifecycle.observePowerBoundary('resume-ready',6);});
  await page.evaluate(()=>{
   const f=window.__chain,original=f.boundary.getState.bind(f.boundary),test=window.__post={entered:false,resolve:null,reject:null};let captured=false;
   f.boundary.getState=async()=>{if(!captured&&f.boundary.writes){captured=true;const snapshot=f.boundary.snapshot();test.entered=true;return new Promise((resolve,reject)=>{test.resolve=()=>resolve(snapshot);test.reject=()=>reject(new Error('fixture old postwrite state timeout'));});}return original();};
  });
  await page.locator('.fan-toggle').click();await page.waitForFunction(()=>window.__post.entered,null,{timeout:6000});
  if(action==='heartbeat-failure')await page.evaluate(async()=>{const f=window.__chain;f.boundary.closed('Stopped');f.boundary.terminalFault=true;f.heartbeatFailure=true;window.__heartbeatOutcome=await f.lifecycle.heartbeat().then(()=> 'accepted',e=>String(e));});
  if(action==='new-curve')await page.evaluate(async()=>window.__chain.lifecycle.apply(window.__chain.curves.aggressive));
  if(action==='newer-read')await page.evaluate(()=>window.__chain.lifecycle.getState());
  if(action==='off')await page.evaluate(async()=>{await window.__chain.lifecycle.disable();window.dispatchEvent(new CustomEvent('fan:guard-state',{detail:{active:false}}));});
  if(action==='power-with-ui'){await page.evaluate(()=>window.__chain.sleep(7));await page.evaluate(()=>window.__chain.autoWake());}
  const before=await snap();await page.evaluate(shape=>window.__post[shape](),shape);await page.waitForTimeout(80);const after=await snap();
  const failures=[];const check=(fn)=>{try{fn();}catch(e){failures.push(String(e));}};
  check(()=>assert.equal(after.trace.filter(x=>x==='launch').length,1,'confirmation must not launch a second process'));
  if(action==='heartbeat-failure')check(()=>assert(after.writes||after.pressed==='false','old Ready falsely established control after current heartbeat readback revoked writes'));
  if(action==='new-curve'||action==='newer-read'||(action==='unchanged'&&shape==='resolve')){
   check(()=>assert.equal(after.writes,true));check(()=>assert.equal(after.pressed,'true','current successful control must not be replaced by obsolete confirmation failure or left pending'));
   check(()=>assert.equal(after.globalActive,true));check(()=>assert.equal(after.trace.filter(x=>x==='enable').length,action==='new-curve'?2:1,'observation must not rewrite a curve'));
   check(()=>assert.doesNotMatch(after.notice,/失败|未完成|请.*重试/));
  }
  if(action==='unchanged'&&shape==='reject'){check(()=>assert.equal(after.pressed,'false','current unavailable confirmation cannot claim success'));check(()=>assert.match(after.notice,/失败|未完成|重试/));}
  if(action==='off'){check(()=>assert.equal(after.pressed,'false'));check(()=>assert.equal(after.intent,false));check(()=>assert.equal(after.writes,false));check(()=>assert.equal(after.recovery,false));}
  if(action==='power-with-ui'){check(()=>assert.equal(after.text,'正在恢复…'));check(()=>assert.equal(after.disabled,true));check(()=>assert.equal(after.trace.filter(x=>x==='enable').length,2,'one wake replay only'));}
  let reachability=null;
  if(action==='heartbeat-failure'&&shape==='resolve'){
   const started=Date.now();await page.waitForFunction(()=>!document.querySelector('.fan-toggle').disabled,null,{timeout:4500});
   check(()=>assert(Date.now()-started<4500,'old confirmation must not extend the fixed click deadline'));
   await page.evaluate(()=>{window.__chain.heartbeatFailure=false;window.__chain.fault=false;window.__chain.boundary.terminalFault=false;});
   await page.locator('.fan-toggle').click();
   await page.waitForFunction(()=>window.__chain.boundary.writes&&document.querySelector('.fan-toggle').getAttribute('aria-pressed')==='true',null,{timeout:12000});
   reachability=await snap();check(()=>assert(reachability.trace.filter(x=>x==='launch').length-reachability.trace.filter(x=>x==='stop').length<=1,'manual retry must retain a single live owner'));
  }
  cases.push({family,shape,action,ok:failures.length===0,failures,before,after,reachability});
 }
 const result={ok:cases.every(c=>c.ok)&&errors.length===0,checks:cases.length,cases,pageErrors:errors,hardwareCalls:0,scope:'actual FanView + actual lifecycle; held adapter postwrite GET; public heartbeat/apply/OFF/state/power; inert Host/native; no device or CPU benchmark',sourceSha256:{view:createHash('sha256').update(fs.readFileSync(process.env.YMCC_FAN_VIEW_SOURCE_BEFORE||path.join(root,'src/views/FanView.vue'))).digest('hex'),bridge:createHash('sha256').update(fs.readFileSync(path.join(root,'src/bridge/fanHost.ts'))).digest('hex')}};
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(result,null,2));
 console.log(JSON.stringify({ok:result.ok,checks:result.checks,failures:cases.filter(c=>!c.ok).map(c=>({family:c.family,shape:c.shape,action:c.action,failures:c.failures})),pageErrors:errors,evidenceRoot:out}));if(!result.ok)process.exitCode=1;
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
