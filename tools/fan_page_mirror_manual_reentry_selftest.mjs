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
const out=path.resolve(process.env.YMCC_FAN_UNMOUNT_EVIDENCE_DIR||path.join(root,'../../Build/Validation/fan-view-unmounted-cancel-20261016'));fs.mkdirSync(out,{recursive:true});
const fixture=path.join(root,'tools/fan_view_bridge_wake_fixture.ts');
const mocks={
  '@/bridge/fanHost':`export {fanHostLifecycle,resolveFanEntryAction} from ${JSON.stringify(fixture)};`,
  '@/bridge/topmon':'export const topMonitorData={value:{tempC:55}};',
  '@/bridge/fanFeature':`import ${JSON.stringify(fixture)};export const FAN_FORCE_PREVIEW=false;export function getFanFeatureSettings(){return {configured:true,preset:'balanced',presetCurves:window.__chain.curves,nodes:structuredClone(window.__chain.curves.balanced),motionEnabled:false};}export function getFanPresetCurve(n){return structuredClone(window.__chain.curves[n]);}export async function recordFanHandshake(){}export async function saveFanCurve(nodes,name){window.__chain.note('save-'+name,nodes);if(window.__chain.saveHold===name){window.__chain.saveReached=name;await new Promise(resolve=>window.__chain.releaseSave=resolve);if(window.__chain.saveThrow)throw new Error('fixture-save-failure');}}export function setFanDiagnosticLoggingEnabled(){}export function setFanControlActive(v){window.__chain.lastControlActive=v;}export function setFanNavigationDuty(v){window.__chain.lastNavDuty=v;}export async function setFanMotionEnabled(){}`,
  '@/bridge/settingsRepository':'export async function readSettingsSection(){return {};}export async function saveSettingsSection(){}',
};
const styles=[];
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:"import {createApp,h,ref,nextTick} from 'vue';import Page from './src/views/FanView.vue';import {createFanMirrorActions} from './src/bridge/deckyFanActions';import {isFanMirrorUiBusy,observeFanMirrorDisplay} from './src/bridge/deckyFanDisplay';window.__makeMirror=()=>{const fx=window.__chain;window.__mirror=createFanMirrorActions({owner:fx.lifecycle,enabled:()=>fx.lastControlActive,currentPreset:()=> 'balanced',curve:n=>fx.curves[n],save:async()=>{},updateActive:v=>fx.lastControlActive=v,updateDuty:v=>fx.lastNavDuty=v,power:async()=>({...fx.native}),featureAllowed:()=>true,uiBusy:isFanMirrorUiBusy,display:observeFanMirrorDisplay});};const visible=ref(true);window.__fanSetVisible=async(v)=>{visible.value=v;await nextTick();};window.__fanRemount=async()=>{visible.value=false;await nextTick();visible.value=true;await nextTick();};createApp({setup:()=>()=>visible.value?h(Page):null}).mount('#app');"},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'real-fan-chain-with-platform-boundaries',setup(b){
 b.onLoad({filter:/fanDiagnostics\.ts$/},a=>({loader:'ts',resolveDir:path.dirname(a.path),contents:fs.readFileSync(a.path,'utf8').replace('export function fanDiagnosticLog(event: string, details?: unknown): void {','export function fanDiagnosticLog(event: string, details?: unknown): void { if(window.__chain)(window.__chain.diagnostics??=[]).push({event,details});')}));
 if(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE)b.onLoad({filter:/src[\\/]bridge[\\/]fanHost\.ts$/},a=>({contents:fs.readFileSync(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE,'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));

  b.onResolve({filter:/.*/},a=>a.path in mocks?{path:a.path,namespace:'ui-boundary'}:undefined);
  b.onLoad({filter:/.*/,namespace:'ui-boundary'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:root}));
  b.onLoad({filter:/\.vue$/},a=>{const {descriptor,errors}=parse(fs.readFileSync(process.env.YMCC_FAN_UNMOUNT_UI_BEFORE&&a.path.endsWith('FanView.vue')?process.env.YMCC_FAN_UNMOUNT_UI_BEFORE:a.path,'utf8'),{filename:a.path});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8),code=compileScript(descriptor,{id,inlineTemplate:true});for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}return {contents:code.content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(a.path)};});
}}]});
const js=result.outputFiles[0].text,css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html=`<!doctype html><meta charset="utf-8"><title>Real Fan UI and lifecycle wake chain</title><style>${css}</style><div id="app"></div><script src="/chain.js"></script>`;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/chain.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/chain.js'?js:html);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));

const cases=[],errors=[];let browser,page;
async function snap(){return page.evaluate(()=>({button:document.querySelector('.fan-toggle')?.textContent,disabled:document.querySelector('.fan-toggle')?.disabled,status:document.querySelector('.fan-status-banner')?.textContent,trace:window.__chain.trace,diagnostics:window.__chain.diagnostics,state:window.__chain.lifecycle.state,recoveryActive:window.__chain.lifecycle.recoveryActive,hasIntent:window.__chain.lifecycle.hasControlIntent,active:window.__chain.lastControlActive}));}
async function check(name,run){if(process.env.YMCC_FAN_UNMOUNT_CASE&&!name.includes(process.env.YMCC_FAN_UNMOUNT_CASE))return;try{await run();cases.push({name,ok:true,snapshot:await snap()});console.log('PASS '+name);}catch(error){cases.push({name,ok:false,error:String(error),snapshot:await snap().catch(()=>null)});console.log('FAIL '+name+': '+String(error));}}
async function fresh(family,cold=true){await page.goto('http://127.0.0.1:'+server.address().port+'/?family='+family+'&cold='+(cold?'1':'0'));await page.waitForSelector('.fan-toggle');await page.evaluate(()=>{window.__chain.setNative(1,'ready');window.__chain.lifecycle.setPowerGeneration(1);window.__chain.lifecycle.observePowerBoundary('resume-ready',1);
// The shared historical fixture leaves state=Stopped after Open and keeps the
// same backend object after launcher.stop. Model the real Host boundaries here.
const b=window.__chain.boundary,open=b.open.bind(b);b.open=async()=>{await open();b.state='Open';return b.snapshot();};
const launcher=window.__chain.lifecycle.launcher,stop=launcher.stop.bind(launcher);launcher.stop=async(...args)=>{await stop(...args);b.closed('Stopped');};
window.__chain.holdAt='Open';});await page.locator('.fan-toggle').click();await page.waitForFunction(()=>window.__chain.gateReached==='Open');}


async function heldFirstOn(family,at) {
 await page.goto('http://127.0.0.1:'+server.address().port+'/?family='+family+'&cold=1');
 await page.waitForSelector('.fan-toggle');
 await page.evaluate(at=>{
  const fx=window.__chain,b=fx.boundary;
  fx.setNative(1,'ready');fx.lifecycle.setPowerGeneration(1);fx.lifecycle.observePowerBoundary('resume-ready',1);
  const open=b.open.bind(b);b.open=async()=>{await open();b.state='Open';return b.snapshot();};
  const handshake=b.handshake.bind(b);b.handshake=async()=>{const reply=await handshake();await fx.waitAt('handshake');return reply;};
  const launcher=fx.lifecycle.launcher,stop=launcher.stop.bind(launcher);launcher.stop=async(...a)=>{await stop(...a);b.closed('Stopped');};
  const disable=fx.lifecycle.disable.bind(fx.lifecycle);fx.lifecycle.disable=(...args)=>{fx.note('public-disable-admitted');return disable(...args);};fx.holdAt=at;fx.holdAt='handshake';window.__makeMirror();
 },at);
 await page.locator('.fan-toggle').click();await page.waitForFunction(at=>window.__chain.gateReached===at,at);
}
try {
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage();page.setDefaultTimeout(5000);page.on('pageerror',e=>errors.push(e.message));
 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' inner-page manual ON remains reachable while mirror ON waits at handshake',async()=>{
  await page.goto('http://127.0.0.1:'+server.address().port+'/?family='+family+'&cold=1');await page.waitForSelector('.fan-toggle');
  await page.evaluate(()=>{
   const fx=window.__chain,b=fx.boundary;fx.setNative(1,'ready');fx.lifecycle.setPowerGeneration(1);fx.lifecycle.observePowerBoundary('resume-ready',1);
   const open=b.open.bind(b);b.open=async()=>{await open();b.state='Open';return b.snapshot();};
   const handshake=b.handshake.bind(b);b.handshake=async()=>{const out=await handshake();await fx.waitAt('handshake');return out;};
   const launcher=fx.lifecycle.launcher,stop=launcher.stop.bind(launcher);launcher.stop=async(...args)=>{await stop(...args);b.closed('Stopped');};fx.holdAt='handshake';window.__makeMirror();
  });
  const mirror=await page.evaluate(()=>window.__mirror.request('fan.setEnabled',{enabled:true},{generation:1,checkpoint(){}}));assert.equal(mirror.pending,true);
  await page.waitForFunction(()=>window.__chain.gateReached==='handshake');
  const before=await snap();assert.equal(before.disabled,false,'inner switch must remain clickable during mirror admission');
  await page.locator('.fan-toggle').click();
  await page.evaluate(()=>window.__chain.releaseGate());
  await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
  const after=await snap();assert.equal(after.trace.filter(x=>x.op==='enable').length,1,'page click must reuse one owner and one write');assert.equal(after.trace.filter(x=>x.op==='launch').length,1,'page click must not launch a second Host');
 });
 assert.deepEqual(errors,[]);const summary={ok:cases.every(x=>x.ok),checks:cases.length,cases,pageErrors:errors,scope:'real FanView + real mirror actions + real FanHostLifecycle; inert adapter/native/launcher; no device/CPU benchmark'};fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify({ok:summary.ok,checks:summary.checks,results:path.join(out,'results.json')}));if(!summary.ok)process.exitCode=1;
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
