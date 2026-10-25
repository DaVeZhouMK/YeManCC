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
  const disable=fx.lifecycle.disable.bind(fx.lifecycle);fx.lifecycle.disable=(...args)=>{fx.note('public-disable-admitted');return disable(...args);};fx.holdAt=at;window.__makeMirror();
 },at);
 await page.locator('.fan-toggle').click();await page.waitForFunction(at=>window.__chain.gateReached===at,at);
}
try {
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage();page.setDefaultTimeout(5000);page.on('pageerror',e=>errors.push(e.message));
 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])for(const at of ['handshake','Open'])await check(family+' mirror OFF after unmount cancels held '+at+' with no late write',async()=>{
  await heldFirstOn(family,at);await page.evaluate(()=>window.__fanSetVisible(false));
  await page.evaluate(async()=>{await window.__mirror.request('fan.setEnabled',{enabled:false},{generation:1,checkpoint(){}});});
  await page.waitForFunction(()=>window.__chain.trace.some(x=>x.op==='public-disable-admitted'));
  await page.evaluate(()=>window.__chain.releaseGate());
  await page.waitForFunction(()=>!window.__chain.lifecycle.startPromise);
  await page.waitForTimeout(300);
  const s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,0,'unmounted old page may not create a fresh apply after mirror OFF');
  assert.equal(s.hasIntent,false,'OFF must stay terminal');assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),false);
  await page.evaluate(()=>window.__fanSetVisible(true));await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇未操控');
  await page.locator('.fan-toggle').click();await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
  assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,1,'the first subsequent manual click still works');
 });

 await check('newer page curve waiting behind Open is still retired by mirror OFF after unmount',async()=>{
  await heldFirstOn('ClawCG3EM','Open');await page.evaluate(()=>window.__fanRemount());
  await page.getByRole('button',{name:'节点 1 转速',exact:true}).click();await page.getByRole('option',{name:'80 %',exact:true}).click();
  await page.waitForFunction(()=>window.__chain.diagnostics.some(x=>x.event==='ui.curve-edited'));
  await page.evaluate(()=>window.__fanSetVisible(false));
  await page.evaluate(()=>window.__mirror.request('fan.setEnabled',{enabled:false},{generation:1,checkpoint(){}}));
  await page.waitForFunction(()=>window.__chain.trace.some(x=>x.op==='public-disable-admitted'));
  await page.evaluate(()=>window.__chain.releaseGate());await page.waitForTimeout(400);
  const s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,0,'a queued UI continuation must carry its original cancellation generation');
  assert.equal(s.hasIntent,false);
 });
 await check('public Close cancels an unmounted page waiting for initial handshake',async()=>{
  await heldFirstOn('ROGAlly','handshake');await page.evaluate(()=>window.__fanSetVisible(false));
  await page.evaluate(()=>{window.__externalClose=window.__chain.lifecycle.close();});
  await page.evaluate(()=>window.__chain.releaseGate());await page.evaluate(()=>window.__externalClose);await page.waitForTimeout(300);
  const s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,0);assert.equal(s.hasIntent,false);
  await page.evaluate(()=>window.__fanSetVisible(true));await page.waitForSelector('.fan-toggle');await page.locator('.fan-toggle').click();
  await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
 });

 await check('old page reset save cannot attach to a newer mirror OFF then ON while unmounted',async()=>{
  await heldFirstOn('GPDWin5','Open');await page.evaluate(()=>window.__chain.releaseGate());
  await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
  await page.evaluate(()=>window.__fanRemount());await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
  await page.evaluate(()=>window.__chain.saveHold='balanced');await page.getByRole('button',{name:'重置转速',exact:true}).click();
  await page.waitForFunction(()=>window.__chain.saveReached==='balanced');await page.evaluate(()=>window.__fanSetVisible(false));
  await page.evaluate(()=>window.__mirror.request('fan.setEnabled',{enabled:false},{generation:1,checkpoint(){}}));
  await page.waitForFunction(()=>!window.__chain.boundary.writes&&!window.__chain.lifecycle.hasControlIntent);
  await page.evaluate(()=>window.__mirror.request('fan.setEnabled',{enabled:true},{generation:1,checkpoint(){}}));
  await page.waitForFunction(()=>window.__chain.boundary.writes);
  const before=(await snap()).trace.filter(x=>x.op==='enable').length;
  await page.evaluate(()=>window.__chain.releaseSave());await page.waitForTimeout(400);
  assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,before,'old save is not an intent in the new mirror control cycle');
 });

 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' one manual ON after mirror OFF is admitted before the canceled handshake settles',async()=>{
  await heldFirstOn(family,'handshake');await page.evaluate(()=>window.__fanSetVisible(false));
  await page.evaluate(()=>window.__mirror.request('fan.setEnabled',{enabled:false},{generation:1,checkpoint(){}}));
  await page.waitForFunction(()=>window.__chain.trace.some(x=>x.op==='public-disable-admitted'));
  await page.evaluate(()=>window.__fanSetVisible(true));
  await page.waitForSelector('.fan-toggle');
  assert.equal((await snap()).button,'风扇未操控','canceled page task is not an active ON or a cancel button');
  await page.locator('.fan-toggle').click();
  await page.evaluate(()=>window.__chain.releaseGate());
  await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
  assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,1,'only the new explicit ON writes');
 });
 assert.deepEqual(errors,[]);
 const sourceBinding={uiSha256:createHash('sha256').update(fs.readFileSync(process.env.YMCC_FAN_UNMOUNT_UI_BEFORE||path.join(root,'src/views/FanView.vue'))).digest('hex'),bridgeSha256:createHash('sha256').update(fs.readFileSync(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE||path.join(root,'src/bridge/fanHost.ts'))).digest('hex'),fixtureSha256:createHash('sha256').update(fs.readFileSync(fixture)).digest('hex'),mirrorSha256:createHash('sha256').update(fs.readFileSync(path.join(root,'src/bridge/deckyFanActions.ts'))).digest('hex'),bundleSha256:createHash('sha256').update(js).digest('hex')};
 const summary={sourceBinding,ok:cases.every(x=>x.ok),checks:cases.length,cases,pageErrors:errors,scope:'real FanView + real FanHostLifecycle/coordinator + real mirror actions/display/blocker + Vue unmount; adapter/native/launcher/settings are inert boundaries, no device'};
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify({ok:summary.ok,checks:summary.checks,results:path.join(out,'results.json')}));if(!summary.ok)process.exitCode=1;
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
