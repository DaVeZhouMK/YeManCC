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
const out=path.resolve(process.env.YMCC_FAN_MIRROR_CANCEL_EVIDENCE_DIR||path.join(root,'../../Build/Validation/fan-mirror-owner-cancel-20261016'));fs.mkdirSync(out,{recursive:true});
const fixture=path.join(root,'tools/fan_view_bridge_wake_fixture.ts');
const mocks={
  '@/bridge/fanHost':`export {fanHostLifecycle,resolveFanEntryAction} from ${JSON.stringify(fixture)};`,
  '@/bridge/topmon':'export const topMonitorData={value:{tempC:55}};',
  '@/bridge/fanFeature':`import ${JSON.stringify(fixture)};export const FAN_FORCE_PREVIEW=false;export function getFanFeatureSettings(){return {configured:true,preset:'balanced',presetCurves:window.__chain.curves,nodes:structuredClone(window.__chain.curves.balanced),motionEnabled:false};}export function getFanPresetCurve(n){return structuredClone(window.__chain.curves[n]);}export async function recordFanHandshake(){}export async function saveFanCurve(nodes,name){window.__chain.note('save-'+name,nodes);if(window.__chain.saveHold===name){window.__chain.saveReached=name;await new Promise(resolve=>window.__chain.releaseSave=resolve);if(window.__chain.saveThrow)throw new Error('fixture-save-failure');}}export function setFanDiagnosticLoggingEnabled(){}export function setFanControlActive(v){window.__chain.lastControlActive=v;}export function setFanNavigationDuty(v){window.__chain.lastNavDuty=v;}export async function setFanMotionEnabled(){}`,
  '@/bridge/settingsRepository':'export async function readSettingsSection(){return {};}export async function saveSettingsSection(){}',
};
const styles=[];
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:"import {createApp,h,ref,nextTick} from 'vue';import Page from './src/views/FanView.vue';import {createFanMirrorActions} from './src/bridge/deckyFanActions';import {isFanMirrorUiBusy,observeFanMirrorDisplay} from './src/bridge/deckyFanDisplay';window.__makeMirror=()=>{const fx=window.__chain;window.__mirror=createFanMirrorActions({owner:fx.lifecycle,enabled:()=>fx.lastControlActive,currentPreset:()=> 'balanced',curve:n=>fx.curves[n],save:async(nodes,preset)=>{fx.note('mirror-save-'+preset,nodes);await fx.waitAt('mirror-save');},updateActive:v=>fx.lastControlActive=v,updateDuty:v=>fx.lastNavDuty=v,power:async()=>({...fx.native}),featureAllowed:()=>true,uiBusy:isFanMirrorUiBusy,display:v=>{(fx.mirrorDisplays??=[]).push(v);observeFanMirrorDisplay(v);}});};const visible=ref(true);window.__fanSetVisible=async(v)=>{visible.value=v;await nextTick();};window.__fanRemount=async()=>{visible.value=false;await nextTick();visible.value=true;await nextTick();};createApp({setup:()=>()=>visible.value?h(Page):null}).mount('#app');"},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'real-fan-chain-with-platform-boundaries',setup(b){
 b.onLoad({filter:/fanDiagnostics\.ts$/},a=>({loader:'ts',resolveDir:path.dirname(a.path),contents:fs.readFileSync(a.path,'utf8').replace('export function fanDiagnosticLog(event: string, details?: unknown): void {','export function fanDiagnosticLog(event: string, details?: unknown): void { if(window.__chain)(window.__chain.diagnostics??=[]).push({event,details});')}));
 if(process.env.YMCC_FAN_MIRROR_SOURCE_BEFORE)b.onLoad({filter:/src[\\/]bridge[\\/]deckyFanActions\.ts$/},a=>({contents:fs.readFileSync(process.env.YMCC_FAN_MIRROR_SOURCE_BEFORE,'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));
 if(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE)b.onLoad({filter:/src[\\/]bridge[\\/]fanHost\.ts$/},a=>({contents:fs.readFileSync(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE,'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));

  b.onResolve({filter:/.*/},a=>a.path in mocks?{path:a.path,namespace:'ui-boundary'}:undefined);
  b.onLoad({filter:/.*/,namespace:'ui-boundary'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:root}));
  b.onLoad({filter:/\.vue$/},a=>{const {descriptor,errors}=parse(fs.readFileSync(process.env.YMCC_FAN_MIRROR_CANCEL_UI_BEFORE&&a.path.endsWith('FanView.vue')?process.env.YMCC_FAN_MIRROR_CANCEL_UI_BEFORE:a.path,'utf8'),{filename:a.path});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8),code=compileScript(descriptor,{id,inlineTemplate:true});for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}return {contents:code.content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(a.path)};});
}}]});
const js=result.outputFiles[0].text,css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html=`<!doctype html><meta charset="utf-8"><title>Real Fan UI and lifecycle wake chain</title><style>${css}</style><div id="app"></div><script src="/chain.js"></script>`;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/chain.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/chain.js'?js:html);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));

const cases=[],errors=[];let browser,page;
async function snap(){return page.evaluate(()=>({button:document.querySelector('.fan-toggle')?.textContent,disabled:document.querySelector('.fan-toggle')?.disabled,status:document.querySelector('.fan-status-banner')?.textContent,trace:window.__chain.trace,diagnostics:window.__chain.diagnostics,state:window.__chain.lifecycle.state,recoveryActive:window.__chain.lifecycle.recoveryActive,hasIntent:window.__chain.lifecycle.hasControlIntent,active:window.__chain.lastControlActive,mirrorPending:window.__mirror?.pendingIntent,mirrorDisplays:window.__chain.mirrorDisplays,checkpoints:window.__chain.checkpoints}));}
async function check(name,run){if(process.env.YMCC_FAN_MIRROR_CANCEL_CASE&&!name.includes(process.env.YMCC_FAN_MIRROR_CANCEL_CASE))return;try{await run();cases.push({name,ok:true,snapshot:await snap()});console.log('PASS '+name);}catch(error){cases.push({name,ok:false,error:String(error),snapshot:await snap().catch(()=>null)});console.log('FAIL '+name+': '+String(error));}}
async function fresh(family,cold=true){await page.goto('http://127.0.0.1:'+server.address().port+'/?family='+family+'&cold='+(cold?'1':'0'));await page.waitForSelector('.fan-toggle');await page.evaluate(()=>{window.__chain.setNative(1,'ready');window.__chain.lifecycle.setPowerGeneration(1);window.__chain.lifecycle.observePowerBoundary('resume-ready',1);
// The shared historical fixture leaves state=Stopped after Open and keeps the
// same backend object after launcher.stop. Model the real Host boundaries here.
const b=window.__chain.boundary,open=b.open.bind(b);b.open=async()=>{await open();b.state='Open';return b.snapshot();};
const launcher=window.__chain.lifecycle.launcher,stop=launcher.stop.bind(launcher);launcher.stop=async(...args)=>{await stop(...args);b.closed('Stopped');};
window.__chain.holdAt='Open';});await page.locator('.fan-toggle').click();await page.waitForFunction(()=>window.__chain.gateReached==='Open');}



async function prepare(family,hold='handshake') {
 await page.goto('http://127.0.0.1:'+server.address().port+'/?family='+family+'&cold=1');await page.waitForSelector('.fan-toggle');
 await page.evaluate(async({hold})=>{
  await window.__fanSetVisible(false);const fx=window.__chain,b=fx.boundary;
  fx.setNative(1,'ready');fx.lifecycle.setPowerGeneration(1);fx.lifecycle.observePowerBoundary('resume-ready',1);
  const open=b.open.bind(b);b.open=async()=>{await open();b.state='Open';return b.snapshot();};
  const handshake=b.handshake.bind(b);b.handshake=async()=>{const response=await handshake();await fx.waitAt('handshake');return response;};
  const launcher=fx.lifecycle.launcher,stop=launcher.stop.bind(launcher);launcher.stop=async(...args)=>{await stop(...args);b.closed('Stopped');};
  const start=fx.lifecycle.start.bind(fx.lifecycle);fx.lifecycle.start=(...args)=>{fx.note('public-start-request');return start(...args);};
  window.__fanControlDisplay=active=>fx.lastControlActive=active;
  fx.holdAt=hold;window.__makeMirror();
 },{hold});
}
async function recordPhase(phase) {
 await page.evaluate(phase=>{const fx=window.__chain;(fx.checkpoints??=[]).push({phase,enableCalls:fx.trace.filter(x=>x.op==='enable').length,startRequests:fx.trace.filter(x=>x.op==='public-start-request').length,launches:fx.trace.filter(x=>x.op==='launch').length,hasIntent:fx.lifecycle.hasControlIntent,mirrorPending:window.__mirror.pendingIntent});},phase);
}
async function request(command,args) {return page.evaluate(({command,args})=>window.__mirror.request(command,args,{generation:1,checkpoint(){}}),{command,args});}
async function beginOn() {
 const admitted=await request('fan.setEnabled',{enabled:true});assert.equal(admitted.pending,true);
 await page.waitForFunction(()=>window.__chain.gateReached==='handshake');
}
async function stopOwner(kind) {await page.evaluate(kind=>{window.__externalCancel=window.__chain.lifecycle[kind]();},kind);}
async function settleOwner() {
 await page.evaluate(()=>window.__chain.releaseGate());await page.evaluate(()=>window.__externalCancel);
 await page.waitForFunction(()=>!window.__chain.lifecycle.startPromise);
 await page.waitForTimeout(250);
}
async function newOnWorks() {
 await request('fan.setEnabled',{enabled:true});await page.waitForFunction(()=>window.__chain.boundary.writes&&window.__chain.lifecycle.hasControlIntent);
 await page.waitForFunction(()=>!window.__mirror.pendingIntent);
}
try {
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage();page.setDefaultTimeout(5000);page.on('pageerror',e=>errors.push(e.message));
 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])for(const kind of ['disable','close'])await check(family+' external '+kind+' cancels the mirror handshake continuation',async()=>{
  await prepare(family);await beginOn();await stopOwner(kind);await settleOwner();
  await recordPhase('after-external-cancel-before-next-on');let s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,0,'a canceled mirror command may not create a new apply');assert.equal(s.hasIntent,false);assert.equal(s.mirrorPending,false);
  await newOnWorks();s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,1,'the first next mirror ON must work once');
 });
 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' first new mirror ON is admitted while canceled startup is still waiting',async()=>{
  await prepare(family);await beginOn();await stopOwner('disable');
  await request('fan.setEnabled',{enabled:true});
  await recordPhase('after-new-on-before-old-handshake-release');assert.equal((await snap()).trace.filter(x=>x.op==='public-start-request').length,2,'new request must not dedupe against canceled waiting state');
  await settleOwner();await page.waitForFunction(()=>window.__chain.boundary.writes);await page.waitForFunction(()=>!window.__mirror.pendingIntent);
  assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,1,'only current ON may write');assert.equal((await snap()).trace.filter(x=>x.op==='launch').length,1,'both API start requests retain one resident Host');
 });
 await check('cold preset-choice save cannot create a new ON after external disable',async()=>{
  await prepare('ROGAlly','mirror-save');
  await page.evaluate(()=>{window.__choice=window.__mirror.request('fan.setChoice',{choice:'aggressive'},{generation:1,checkpoint(){}}).catch(e=>({error:String(e)}));});
  await page.waitForFunction(()=>window.__chain.gateReached==='mirror-save');await stopOwner('disable');await page.evaluate(()=>window.__externalCancel);
  await page.evaluate(()=>window.__chain.releaseGate());await page.evaluate(()=>window.__choice);await page.waitForTimeout(200);
  await recordPhase('canceled-continuation-before-next-on');const s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,0);assert.equal(s.trace.filter(x=>x.op==='public-start-request').length,0,'canceled save must not launch');assert.equal(s.hasIntent,false);
  await newOnWorks();
 });
 await check('direct mirror execute checks external Close before its delayed start returns',async()=>{
  await prepare('GPDWin5');
  await page.evaluate(()=>{window.__direct=window.__mirror.execute('fan.setEnabled',{enabled:true},{generation:1,checkpoint(){}}).catch(e=>({error:String(e)}));});
  await page.waitForFunction(()=>window.__chain.gateReached==='handshake');await stopOwner('close');await settleOwner();await page.evaluate(()=>window.__direct);
  await recordPhase('canceled-continuation-before-next-on');const s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,0);assert.equal(s.hasIntent,false);await newOnWorks();
 });

 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' ordinary ON dedupe and own OFF receipt remain valid, then ON works once',async()=>{
  await prepare(family,'');await newOnWorks();
  const before=(await snap()).trace.filter(x=>x.op==='enable').length;
  const duplicate=await request('fan.setEnabled',{enabled:true});assert.equal(duplicate.applied,true);
  assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,before,'live ON does not repeat the writer');
  await request('fan.setEnabled',{enabled:false});
  await page.waitForFunction(()=>window.__chain.mirrorDisplays?.at(-1)?.notice==='风扇控制已关闭');
  assert.equal((await snap()).hasIntent,false);assert.equal((await snap()).mirrorPending,false);
  await newOnWorks();assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,before+1);
 });
 assert.deepEqual(errors,[]);
 const sourceBinding={uiSha256:createHash('sha256').update(fs.readFileSync(path.join(root,'src/views/FanView.vue'))).digest('hex'),bridgeSha256:createHash('sha256').update(fs.readFileSync(path.join(root,'src/bridge/fanHost.ts'))).digest('hex'),fixtureSha256:createHash('sha256').update(fs.readFileSync(fixture)).digest('hex'),mirrorSha256:createHash('sha256').update(fs.readFileSync(process.env.YMCC_FAN_MIRROR_SOURCE_BEFORE||path.join(root,'src/bridge/deckyFanActions.ts'))).digest('hex'),bundleSha256:createHash('sha256').update(js).digest('hex')};
 const summary={sourceBinding,ok:cases.every(x=>x.ok),checks:cases.length,cases,pageErrors:errors,scope:'actual mirror actions + FanHostLifecycle/coordinator + display + Vue mount/unmount; adapter/native/launcher/settings are inert boundaries, no device or CPU benchmark'};
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify({ok:summary.ok,checks:summary.checks,results:path.join(out,'results.json')}));if(!summary.ok)process.exitCode=1;
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
