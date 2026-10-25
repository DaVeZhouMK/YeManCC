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
const out=path.resolve(process.env.YMCC_FAN_MIRROR_CURVE_EVIDENCE_DIR||path.join(root,'../../Build/Validation/fan-mirror-pending-curve-20261016'));fs.mkdirSync(out,{recursive:true});
const fixture=path.join(root,'tools/fan_view_bridge_wake_fixture.ts');
const mocks={
  '@/bridge/fanHost':`export {fanHostLifecycle,resolveFanEntryAction} from ${JSON.stringify(fixture)};`,
  '@/bridge/topmon':'export const topMonitorData={value:{tempC:55}};',
  '@/bridge/fanFeature':`import ${JSON.stringify(fixture)};export const FAN_FORCE_PREVIEW=false;export function getFanFeatureSettings(){return {configured:true,preset:'balanced',presetCurves:window.__chain.curves,nodes:structuredClone(window.__chain.curves.balanced),motionEnabled:false};}export function getFanPresetCurve(n){return structuredClone(window.__chain.curves[n]);}export async function recordFanHandshake(){}export async function saveFanCurve(nodes,name){window.__chain.note('save-'+name,nodes);if(window.__chain.saveHold===name){window.__chain.saveReached=name;await new Promise(resolve=>window.__chain.releaseSave=resolve);if(window.__chain.saveThrow)throw new Error('fixture-save-failure');}}export function setFanDiagnosticLoggingEnabled(){}export function setFanControlActive(v){window.__chain.lastControlActive=v;}export function setFanNavigationDuty(v){window.__chain.lastNavDuty=v;}export async function setFanMotionEnabled(){}`,
  '@/bridge/settingsRepository':'export async function readSettingsSection(){return {};}export async function saveSettingsSection(){}',
};
const styles=[];
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:"import {FanApiError} from './src/bridge/fanApi';window.__selfHealApiError=FanApiError;import {createApp,h,ref,nextTick} from 'vue';import Page from './src/views/FanView.vue';import {createFanMirrorActions} from './src/bridge/deckyFanActions';import {isFanMirrorUiBusy,observeFanMirrorDisplay} from './src/bridge/deckyFanDisplay';window.__createFanMirrorActions=createFanMirrorActions;window.__makeMirror=()=>{const fx=window.__chain;window.__mirror=createFanMirrorActions({owner:fx.lifecycle,enabled:()=>fx.lastControlActive,currentPreset:()=>fx.savedMirrorPreset??'balanced',curve:n=>fx.curves[n],save:async(nodes,preset)=>{fx.note('mirror-save-'+preset,nodes);if(fx.modelPresetPersistence){fx.savedMirrorPreset=preset;fx.curves[preset]=nodes.map(n=>({...n}));}if(fx.blockPendingSave){fx.pendingSaveEntered=true;await new Promise(resolve=>fx.releasePendingSave=resolve);}await fx.waitAt('mirror-save');if(fx.rejectPresetSave)throw Error('fixture-preset-save-failure');},updateActive:v=>fx.lastControlActive=v,updateDuty:v=>fx.lastNavDuty=v,power:async()=>{fx.note('mirror-power-read');if(fx.blockFirstMirrorPower){fx.blockFirstMirrorPower=false;fx.firstMirrorPowerEntered=true;await new Promise(resolve=>fx.releaseFirstMirrorPower=resolve);if(fx.rejectFirstMirrorPower)throw Error('fixture-pending-power-failure');}return {...fx.native,generation:fx.sharedMirrorGeneration??fx.native.generation};},featureAllowed:()=>true,uiBusy:isFanMirrorUiBusy,display:v=>{(fx.mirrorDisplays??=[]).push(v);observeFanMirrorDisplay(v);}});const originalExecute=window.__mirror.execute.bind(window.__mirror);window.__mirror.execute=async(...args)=>{fx.mirrorExecPending=(fx.mirrorExecPending||0)+1;try{return await originalExecute(...args);}finally{fx.mirrorExecPending--;fx.note('mirror-execute-settled');}};};const visible=ref(true);window.__fanSetVisible=async(v)=>{visible.value=v;await nextTick();};window.__fanRemount=async()=>{visible.value=false;await nextTick();visible.value=true;await nextTick();};createApp({setup:()=>()=>visible.value?h(Page):null}).mount('#app');"},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'real-fan-chain-with-platform-boundaries',setup(b){
 b.onLoad({filter:/fanDiagnostics\.ts$/},a=>({loader:'ts',resolveDir:path.dirname(a.path),contents:fs.readFileSync(a.path,'utf8').replace('export function fanDiagnosticLog(event: string, details?: unknown): void {','export function fanDiagnosticLog(event: string, details?: unknown): void { if(window.__chain)(window.__chain.diagnostics??=[]).push({event,details});')}));
 if(process.env.YMCC_FAN_MIRROR_SOURCE_BEFORE)b.onLoad({filter:/src[\\/]bridge[\\/]deckyFanActions\.ts$/},a=>({contents:fs.readFileSync(process.env.YMCC_FAN_MIRROR_SOURCE_BEFORE,'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));
 if(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE)b.onLoad({filter:/src[\\/]bridge[\\/]fanHost\.ts$/},a=>({contents:fs.readFileSync(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE,'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));

  b.onResolve({filter:/.*/},a=>a.path in mocks?{path:a.path,namespace:'ui-boundary'}:undefined);
  b.onLoad({filter:/.*/,namespace:'ui-boundary'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:root}));
  b.onLoad({filter:/\.vue$/},a=>{const {descriptor,errors}=parse(fs.readFileSync(process.env.YMCC_FAN_MIRROR_CURVE_UI_BEFORE&&a.path.endsWith('FanView.vue')?process.env.YMCC_FAN_MIRROR_CURVE_UI_BEFORE:a.path,'utf8'),{filename:a.path});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8),code=compileScript(descriptor,{id,inlineTemplate:true});for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}return {contents:code.content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(a.path)};});
}}]});
const js=result.outputFiles[0].text,css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html=`<!doctype html><meta charset="utf-8"><title>Real Fan UI and lifecycle wake chain</title><style>${css}</style><div id="app"></div><script src="/chain.js"></script>`;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/chain.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/chain.js'?js:html);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));

const cases=[],errors=[];let browser,page;
async function snap(){return page.evaluate(()=>({button:document.querySelector('.fan-toggle')?.textContent,disabled:document.querySelector('.fan-toggle')?.disabled,status:document.querySelector('.fan-status-banner')?.textContent,trace:window.__chain.trace,diagnostics:window.__chain.diagnostics,state:window.__chain.lifecycle.state,recoveryActive:window.__chain.lifecycle.recoveryActive,hasIntent:window.__chain.lifecycle.hasControlIntent,active:window.__chain.lastControlActive,mirrorPending:window.__mirror?.pendingIntent,mirrorDisplays:window.__chain.mirrorDisplays,checkpoints:window.__chain.checkpoints,mirrorContract:window.__chain.mirrorContractResult,selectionResult:window.__chain.selectionResult,pendingSaveRejections:window.__chain.pendingSaveRejections}));}
async function check(name,run){if(process.env.YMCC_FAN_MIRROR_CURVE_CASE&&!name.includes(process.env.YMCC_FAN_MIRROR_CURVE_CASE))return;try{await run();cases.push({name,ok:true,snapshot:await snap()});console.log('PASS '+name);}catch(error){cases.push({name,ok:false,error:String(error),snapshot:await snap().catch(()=>null)});console.log('FAIL '+name+': '+String(error));}}
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
  const start=fx.lifecycle.start.bind(fx.lifecycle);fx.lifecycle.start=async(...args)=>{fx.note('public-start-request');const gate=await start(...args);fx.note('public-start-completed');return gate;};
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

async function beginHeldOpen(family) {
 await prepare(family,'Open');
 const result=await request('fan.setEnabled',{enabled:true});assert.equal(result.pending,true);
 await page.waitForFunction(()=>window.__chain.gateReached==='Open');
}
async function waitForCompletedControl() {
 await page.waitForFunction(()=>!window.__mirror.pendingIntent&&window.__chain.boundary.writes&&window.__chain.lifecycle.hasControlIntent);
 await page.waitForTimeout(60);
}
try {
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage();page.setDefaultTimeout(5000);page.on('pageerror',e=>errors.push(e.message));
 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' preset selected during HC Open is the only curve written',async()=>{
  await beginHeldOpen(family);
  assert.equal((await snap()).mirrorPending,true,'mirror must still own the original pending ON at Open');
  const result=await request('fan.setPreset',{preset:'aggressive'});assert.equal(result.pending,true);
  await page.evaluate(()=>window.__chain.releaseGate());await waitForCompletedControl();
  const s=await snap(),writes=s.trace.filter(x=>x.op==='enable');
  assert.equal(writes.length,1,'known pending preset must replace the unsubmitted old curve');
  assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.curves.aggressive));
  assert.equal(s.trace.filter(x=>x.op==='Open').length,1);assert.equal(s.trace.filter(x=>x.op==='OpenEvents').length,1);assert.equal(s.trace.filter(x=>x.op==='launch').length,1);
 });
 await check('two ordered preset saves during one Open apply only the last user selection',async()=>{
  await beginHeldOpen('ROGAlly');
  await request('fan.setPreset',{preset:'soft'});await request('fan.setPreset',{preset:'aggressive'});
  await page.evaluate(()=>window.__chain.releaseGate());await waitForCompletedControl();
  const writes=(await snap()).trace.filter(x=>x.op==='enable');assert.equal(writes.length,1);assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.curves.aggressive));
 });
 await check('OFF after staging a new preset cancels every old and pending curve, then first ON works',async()=>{
  await beginHeldOpen('ClawCG3EM');await request('fan.setPreset',{preset:'aggressive'});
  await request('fan.setEnabled',{enabled:false});await page.evaluate(()=>window.__chain.releaseGate());
  await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent);
  await page.waitForTimeout(100);assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,0);
  await newOnWorks();assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,1);
 });
 await check('ordinary single ON with no new preset keeps exactly one original enable',async()=>{
  await beginHeldOpen('GPDWin5');await page.evaluate(()=>window.__chain.releaseGate());await waitForCompletedControl();
  const s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,1);assert.deepEqual(s.trace.filter(x=>x.op==='enable')[0].nodes,await page.evaluate(()=>window.__chain.curves.balanced));
 });

 await check('a later explicit curve from the original owner is not overwritten by older mirror pending preset',async()=>{
  await beginHeldOpen('ClawCG3EM');await request('fan.setPreset',{preset:'aggressive'});
  await page.evaluate(()=>{const fx=window.__chain;fx.foreignCurve=fx.curves.soft.map(n=>({...n,dutyPercent:65}));window.__newerOwnerApply=fx.lifecycle.apply(fx.foreignCurve).catch(e=>({error:String(e)}));});
  await page.evaluate(()=>window.__chain.releaseGate());await page.evaluate(()=>window.__newerOwnerApply);
  await page.waitForFunction(()=>window.__chain.boundary.writes);await page.waitForFunction(()=>!window.__mirror.pendingIntent);await page.waitForTimeout(80);
  const s=await snap(),writes=s.trace.filter(x=>x.op==='enable');assert.equal(writes.length,1,'retired mirror selection cannot re-enter after a newer original-owner intent');
  assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.foreignCurve));
 });
 async function heldStagedSave(family){
   await prepare(family,'handshake');await beginOn();
   await page.evaluate(()=>{window.__chain.blockPendingSave=true;window.__stagedSave=window.__mirror.request('fan.setPreset',{preset:'aggressive'},{generation:1,checkpoint(){}}).then(x=>x,e=>({error:String(e)}));});
   await page.waitForFunction(()=>window.__chain.pendingSaveEntered);await page.evaluate(()=>window.__chain.releaseGate());
   await page.waitForFunction(()=>{const t=window.__chain.trace,i=t.findIndex(x=>x.op==='public-start-completed');return i>=0&&t.slice(i+1).some(x=>x.op==='mirror-power-read');});
   await page.evaluate(async()=>{for(let i=0;i<20;i++)await Promise.resolve();});const snapshot=await snap();assert.equal(snapshot.trace.filter(x=>x.op==='enable').length,0,'staged save is not committed yet');assert.equal(snapshot.mirrorPending,true);await recordPhase('original-start-complete-staged-save-pending');
  }
  async function releaseStagedSave(){await page.evaluate(()=>window.__chain.releasePendingSave());await page.evaluate(()=>window.__stagedSave);await page.waitForFunction(()=>window.__chain.mirrorExecPending===0);await page.waitForTimeout(40);await recordPhase('old-staged-save-settled');}
  for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' OFF while accepted ON awaits staged preset save cannot late-enable',async()=>{
   await heldStagedSave(family);await request('fan.setEnabled',{enabled:false});await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent);await recordPhase('OFF-complete-before-save');await releaseStagedSave();const before=await snap();assert.equal(before.trace.filter(x=>x.op==='enable').length,0,'a retired staged preset re-enabled after OFF');assert.equal(before.hasIntent,false);await newOnWorks();assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,1,'fresh explicit ON must remain reachable');
  });
  for(const action of ['disable','close'])await check('external '+action+' after start completion retires the staged-save writer',async()=>{
   await heldStagedSave('ROGAlly');await page.evaluate(action=>window.__chain.lifecycle[action](),action);await recordPhase(action+'-complete-before-save');await releaseStagedSave();const state=await snap();assert.equal(state.trace.filter(x=>x.op==='enable').length,0,'late save wrote after '+action);assert.equal(state.hasIntent,false);if(action==='close')assert.equal(state.trace.filter(x=>x.op==='launch').length,1,'late save launched an unrequested successor');await newOnWorks();
  });
  await check('mirror stop after start completion retires its unsubmitted staged curve',async()=>{
   await heldStagedSave('GPDWin5');await page.evaluate(()=>window.__mirror.stop());await releaseStagedSave();assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,0,'stopped mirror continuation still wrote');await newOnWorks();
  });
  await check('a newer original-owner curve cannot be overwritten by a staged save after start completion',async()=>{
   await heldStagedSave('ROGAlly');await page.evaluate(async()=>{const f=window.__chain;f.foreignCurve=f.curves.soft.map(n=>({...n,dutyPercent:65}));await f.lifecycle.start({manualRecovery:true,reason:'fixture-newer-page-control'});await f.lifecycle.apply(f.foreignCurve);});await recordPhase('newer-original-curve-before-save');await releaseStagedSave();const state=await snap(),writes=state.trace.filter(x=>x.op==='enable');assert.equal(writes.length,1,'old staged curve overwrote a newer explicit control');assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.foreignCurve));assert.equal(state.hasIntent,true);
  });
  await check('OFF then fresh ON while old staged save waits cannot append the retired curve',async()=>{
   await heldStagedSave('ClawCG3EM');await request('fan.setEnabled',{enabled:false});await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent);await newOnWorks();await recordPhase('fresh-ON-before-old-save');await releaseStagedSave();const writes=(await snap()).trace.filter(x=>x.op==='enable');assert.equal(writes.length,1,'late old save changed the newly enabled control');assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.curves.balanced));
  });
await check('a staged save from an expired shared mirror generation cannot write, while a fresh packet can',async()=>{
   await heldStagedSave('ROGAlly');await page.evaluate(()=>window.__chain.sharedMirrorGeneration=2);await releaseStagedSave();assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,0,'expired mirror generation forwarded its old staged curve');
   const result=await page.evaluate(()=>window.__mirror.request('fan.setEnabled',{enabled:true},{generation:2,checkpoint(){}}));assert.equal(result.pending,true);await waitForCompletedControl();assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,1,'fresh mirror packet must still reach the unchanged Fan owner');
  });
 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' first mirror ON interrupted at handshake survives sleep without another click',async()=>{
  await prepare(family,'handshake');await beginOn();
  await recordPhase('first-ON-at-handshake');
  await page.evaluate(()=>{window.__mirrorSleep=window.__chain.sleep(2);});
  await page.evaluate(()=>window.__chain.releaseGate());await page.evaluate(()=>window.__mirrorSleep);
  await page.waitForFunction(()=>!window.__chain.mirrorExecPending);
  await recordPhase('first-ON-suspended');
  assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,0,'sleep fence must retire the pre-sleep writer');
  await page.evaluate(async()=>{const f=window.__chain;f.setNative(2,'ready');await f.boundary.resume({generation:2});await f.autoWake();});
  await page.waitForFunction(()=>window.__chain.boundary.writes&&window.__chain.lifecycle.hasControlIntent);
  await recordPhase('first-ON-auto-restored');
  const s=await snap(),writes=s.trace.filter(x=>x.op==='enable');
  assert.equal(writes.length,1,'one accepted ON must recover once, without an extra mirror writer');
  assert.equal(writes[0].generation,2,'old startup cannot write through the real sleep boundary');
  assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.curves.balanced));
  await page.evaluate(()=>window.__mirror.request('fan.setEnabled',{enabled:false},{generation:2,checkpoint(){}}));
  await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent&&!window.__chain.mirrorExecPending);
  await page.evaluate(()=>window.__mirror.request('fan.setEnabled',{enabled:true},{generation:2,checkpoint(){}}));
  await waitForCompletedControl();assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,2,'OFF then fresh ON must remain reachable');
 });
 await check('first mirror ON canceled before wake stays OFF until a fresh explicit ON',async()=>{
  await prepare('ClawCG3EM','handshake');await beginOn();
  await page.evaluate(()=>{window.__mirrorSleep=window.__chain.sleep(2);});
  await page.evaluate(()=>window.__chain.releaseGate());await page.evaluate(()=>window.__mirrorSleep);
  await page.waitForFunction(()=>!window.__chain.mirrorExecPending);
  await page.evaluate(async()=>{const f=window.__chain;f.setNative(2,'ready');await f.boundary.resume({generation:2});await window.__mirror.request('fan.setEnabled',{enabled:false},{generation:2,checkpoint(){}});});
  await page.waitForFunction(()=>!window.__chain.mirrorExecPending&&!window.__chain.lifecycle.hasControlIntent);
  await page.evaluate(()=>window.__chain.autoWake());
  await recordPhase('OFF-still-terminal-after-wake');
  assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,0,'retained first ON cannot survive a later explicit OFF');
  await page.evaluate(()=>window.__mirror.request('fan.setEnabled',{enabled:true},{generation:2,checkpoint(){}}));
  await waitForCompletedControl();assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,1);
 });
 await check('wake without an accepted ON does not invent a Host or a curve writer',async()=>{
  await prepare('ROGAlly','handshake');await page.evaluate(async()=>{await window.__chain.sleep(2);await window.__chain.autoWake();});
  const s=await snap();assert.equal(s.hasIntent,false);assert.equal(s.trace.filter(x=>['launch','handshake','enable','Open','OpenEvents'].includes(x.op)).length,0);
 });
 await check('duplicate ON while first handshake waits does not restage intent or restart its owner',async()=>{
  await prepare('GPDWin5','handshake');await beginOn();
  const firstRevision=await page.evaluate(()=>window.__chain.lifecycle.controlIntentRevision);
  for(let i=0;i<3;i++)assert.equal((await request('fan.setEnabled',{enabled:true})).pending,true);
  assert.equal(await page.evaluate(()=>window.__chain.lifecycle.controlIntentRevision),firstRevision);
  assert.equal((await snap()).trace.filter(x=>x.op==='public-start-request').length,1);
  await page.evaluate(()=>window.__chain.releaseGate());await waitForCompletedControl();assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,1);
 });
 await check('mirror contract: optimistic ON intent cannot dedupe its own unconfirmed write',async()=>{
  await prepare('ROGAlly','handshake');
  const result=await page.evaluate(async()=>{
   let active=false,intent=false,releaseStart;const calls=[],displays=[];
   const owner={get hasControlIntent(){return intent;},controlReady:true,state:'ready',recoveryActive:false,
    stagePendingControlCurve(nodes){intent=true;calls.push({op:'stage',nodes:structuredClone(nodes)});},
    async start(){calls.push({op:'start'});await new Promise(resolve=>releaseStart=resolve);return {allowed:true,writeReady:true};},
    async apply(nodes){calls.push({op:'apply',nodes:structuredClone(nodes)});intent=true;return {state:'Ready'};},
    async applyPreset(){throw Error('unexpected preset');},async disable(){intent=false;return {state:'AwaitingControl'};}};
   const action=window.__createFanMirrorActions({owner,enabled:()=>active,currentPreset:()=> 'balanced',curve:p=>window.__chain.curves[p],save:async()=>{},updateActive:v=>active=v,updateDuty:()=>{},power:async()=>({generation:1}),featureAllowed:()=>true,uiBusy:()=>false,display:v=>displays.push(v)});
   const context={generation:1,checkpoint(){}};
   const accepted=await action.request('fan.setEnabled',{enabled:true},context);
   for(let i=0;i<20;i++)await Promise.resolve();
   const during={accepted,calls:structuredClone(calls),pending:action.pendingIntent,lastDisplay:displays.at(-1)};
   releaseStart?.();for(let i=0;i<40;i++)await Promise.resolve();
   const repeated=await action.request('fan.setEnabled',{enabled:true},context);
   const direct=await action.execute('fan.setEnabled',{enabled:true},context);
   const result={during,after:{calls,pending:action.pendingIntent,lastDisplay:displays.at(-1)},repeated,direct};window.__chain.mirrorContractResult=result;return result;
  });
  assert.equal(result.during.accepted.pending,true);assert.equal(result.during.accepted.applied,false);
  assert.deepEqual(result.during.calls.map(x=>x.op),['stage','start']);assert.equal(result.during.pending,true);assert.equal(result.during.lastDisplay.pending,true);
  assert.deepEqual(result.after.calls.map(x=>x.op),['stage','start','apply']);assert.equal(result.after.pending,false);
  assert.equal(result.repeated.applied,true);assert.equal(result.direct.applied,true);
 });
 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' older ON awaiting its first power read cannot replace a newer original-owner curve',async()=>{
  await prepare(family,'Open');
  await page.evaluate(()=>{const f=window.__chain;f.blockFirstMirrorPower=true;window.__initialMirrorOn=window.__mirror.request('fan.setEnabled',{enabled:true},{generation:1,checkpoint(){}}).then(result=>({ok:true,result}),error=>({ok:false,error:String(error)}));});
  await page.waitForFunction(()=>window.__chain.firstMirrorPowerEntered);
  await page.evaluate(()=>{const f=window.__chain;f.foreignCurve=f.curves.aggressive.map(n=>({...n}));f.lifecycle.stagePendingControlCurve(f.foreignCurve);window.__newerOriginal=(async()=>{await f.lifecycle.start({manualRecovery:true,reason:'fixture-newer-original-control'});return f.lifecycle.apply(f.foreignCurve);})().then(()=>({ok:true}),error=>({ok:false,error:String(error)}));});
  await page.waitForFunction(()=>window.__chain.gateReached==='Open');
  await recordPhase('newer-original-Open-before-old-power');
  await page.evaluate(()=>window.__chain.releaseFirstMirrorPower());await page.evaluate(()=>window.__initialMirrorOn.catch(error=>({error:String(error)})));
  await page.evaluate(()=>window.__chain.releaseGate());const newerResult=await page.evaluate(()=>window.__newerOriginal);
  await page.waitForFunction(()=>!window.__chain.mirrorExecPending);await page.waitForTimeout(40);
  await recordPhase('old-power-settled-after-newer-original');
  assert.equal(newerResult.ok,true,'older mirror power receipt canceled newer original control: '+newerResult.error);
  const writes=(await snap()).trace.filter(x=>x.op==='enable');assert.equal(writes.length,1,'older mirror must not add an obsolete default-curve write');
  assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.foreignCurve));
  await request('fan.setEnabled',{enabled:false});await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent&&!window.__chain.mirrorExecPending);await newOnWorks();
 });
 await check('a newer original curve completed during an old ON power read is adopted without duplicate enable',async()=>{
  await prepare('ROGAlly','none');
  await page.evaluate(()=>{const f=window.__chain;f.blockFirstMirrorPower=true;window.__initialMirrorOn=window.__mirror.request('fan.setEnabled',{enabled:true},{generation:1,checkpoint(){}}).then(result=>({ok:true,result}),error=>({ok:false,error:String(error)}));});
  await page.waitForFunction(()=>window.__chain.firstMirrorPowerEntered);
  await page.evaluate(async()=>{const f=window.__chain;f.foreignCurve=f.curves.aggressive.map(n=>({...n}));await f.lifecycle.start({manualRecovery:true,reason:'fixture-newer-completed-original'});await f.lifecycle.apply(f.foreignCurve);});
  await page.evaluate(()=>window.__chain.releaseFirstMirrorPower());const result=await page.evaluate(()=>window.__initialMirrorOn);
  assert.equal(result.ok,true);assert.equal(result.result.applied,true);
  const s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,1);assert.equal(s.trace.filter(x=>x.op==='public-start-request').length,1);assert.equal(s.mirrorPending,false);
 });
 await check('older OFF awaiting its first power read cannot cancel a newer original-owner ON',async()=>{
  await prepare('GPDWin5','Open');
  await page.evaluate(()=>{const f=window.__chain;f.blockFirstMirrorPower=true;window.__initialMirrorOff=window.__mirror.request('fan.setEnabled',{enabled:false},{generation:1,checkpoint(){}}).then(result=>({ok:true,result}),error=>({ok:false,error:String(error)}));});
  await page.waitForFunction(()=>window.__chain.firstMirrorPowerEntered);
  await page.evaluate(()=>{const f=window.__chain;f.foreignCurve=f.curves.aggressive.map(n=>({...n}));f.lifecycle.stagePendingControlCurve(f.foreignCurve);window.__newerOriginal=(async()=>{await f.lifecycle.start({manualRecovery:true,reason:'fixture-newer-original-after-old-OFF'});return f.lifecycle.apply(f.foreignCurve);})().then(()=>({ok:true}),error=>({ok:false,error:String(error)}));});
  await page.waitForFunction(()=>window.__chain.gateReached==='Open');
  await page.evaluate(()=>window.__chain.releaseFirstMirrorPower());await page.evaluate(()=>window.__initialMirrorOff);
  await page.evaluate(()=>window.__chain.releaseGate());const newerResult=await page.evaluate(()=>window.__newerOriginal);
  await page.waitForFunction(()=>!window.__chain.mirrorExecPending);await page.waitForTimeout(40);
  assert.equal(newerResult.ok,true,'older OFF canceled a newer explicit ON: '+newerResult.error);
  const s=await snap();assert.equal(s.hasIntent,true);assert.equal(s.trace.filter(x=>x.op==='enable').length,1);assert.equal(s.trace.filter(x=>x.op==='restore'||x.op==='release').length,0,'old OFF performed hardware cleanup after newer ON');
  await request('fan.setEnabled',{enabled:false});await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent&&!window.__chain.mirrorExecPending);await newOnWorks();
 });
 for(const command of ['fan.setChoice','fan.setPreset'])for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' '+command+' save cannot later replace a newer original-owner manual curve',async()=>{
  await prepare(family,'none');
  if(command==='fan.setPreset')await page.evaluate(async()=>{const f=window.__chain;await f.lifecycle.start({manualRecovery:true,reason:'fixture-active-before-preset'});await f.lifecycle.apply(f.curves.balanced);});
  await page.evaluate(command=>{const f=window.__chain;f.modelPresetPersistence=true;f.blockPendingSave=true;window.__savedSelection=window.__mirror.request(command,command==='fan.setChoice'?{choice:'aggressive'}:{preset:'aggressive'},{generation:1,checkpoint(){}}).then(result=>({ok:true,result}),error=>({ok:false,error:String(error)}));},command);
  await page.waitForFunction(()=>window.__chain.pendingSaveEntered);
  await page.evaluate(async()=>{const f=window.__chain;f.foreignCurve=f.curves.soft.map(n=>({...n,dutyPercent:65}));f.lifecycle.stagePendingControlCurve(f.foreignCurve);await f.lifecycle.start({manualRecovery:true,reason:'fixture-newer-page-without-settings-save'});await f.lifecycle.apply(f.foreignCurve);});
  await recordPhase('newer-manual-curve-before-selection-save');
  await page.evaluate(()=>window.__chain.releasePendingSave());const selection=await page.evaluate(async()=>{const result=await window.__savedSelection;window.__chain.selectionResult=result;return result;});
  await page.waitForFunction(()=>!window.__chain.mirrorExecPending);await page.waitForTimeout(40);
  await recordPhase('old-selection-save-settled');
  const s=await snap(),writes=s.trace.filter(x=>x.op==='enable'),expected=command==='fan.setPreset'?2:1;
  assert.equal(writes.length,expected,'old saved selection added a curve after a newer manual one');
  assert.deepEqual(writes.at(-1).nodes,await page.evaluate(()=>window.__chain.foreignCurve));
  assert.equal(s.hasIntent,true);assert.equal(selection.ok,true);assert.equal(selection.result.saved,true);
  if(command==='fan.setPreset')assert.equal(selection.result.applied,false,'a saved obsolete preset cannot claim hardware application');
  await request('fan.setEnabled',{enabled:false});await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent&&!window.__chain.mirrorExecPending);await newOnWorks();
 });
 await check('ordinary cold saved choice enables its selected curve once without changing write admission',async()=>{
  await prepare('GPDWin5','none');await page.evaluate(()=>window.__chain.modelPresetPersistence=true);
  const result=await request('fan.setChoice',{choice:'aggressive'});assert.equal(result.saved,true);assert.equal(result.pending,true);await waitForCompletedControl();
  const writes=(await snap()).trace.filter(x=>x.op==='enable');assert.equal(writes.length,1);assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.curves.aggressive));
 });
 await check('ordinary active saved preset still applies its selected curve exactly once',async()=>{
  await prepare('ClawCG3EM','none');await request('fan.setEnabled',{enabled:true});await waitForCompletedControl();await page.evaluate(()=>window.__chain.modelPresetPersistence=true);
  const result=await request('fan.setPreset',{preset:'aggressive'});assert.equal(result.saved,true);assert.equal(result.applied,true);
  const writes=(await snap()).trace.filter(x=>x.op==='enable');assert.equal(writes.length,2);assert.deepEqual(writes.at(-1).nodes,await page.evaluate(()=>window.__chain.curves.aggressive));
 });
 for(const action of ['disable','close'])await check('direct preset save cannot restore control after original-owner '+action,async()=>{
  await prepare('ROGAlly','none');await request('fan.setEnabled',{enabled:true});await waitForCompletedControl();
  await page.evaluate(()=>{const f=window.__chain;f.modelPresetPersistence=true;f.blockPendingSave=true;window.__savedSelection=window.__mirror.request('fan.setPreset',{preset:'aggressive'},{generation:1,checkpoint(){}}).then(result=>({ok:true,result}),error=>({ok:false,error:String(error)}));});
  await page.waitForFunction(()=>window.__chain.pendingSaveEntered);await page.evaluate(action=>window.__chain.lifecycle[action](),action);
  await recordPhase('direct-preset-canceled-before-save');
  await page.evaluate(()=>window.__chain.releasePendingSave());await page.evaluate(()=>window.__savedSelection);await page.waitForFunction(()=>!window.__chain.mirrorExecPending);
  const s=await snap();assert.equal(s.hasIntent,false);assert.equal(s.trace.filter(x=>x.op==='enable').length,1);assert.equal(s.trace.filter(x=>x.op==='launch').length,1,'a late preset save launched an unwanted successor');await newOnWorks();
 });
 await check('failed direct preset save performs no curve write and does not keep the mirror busy',async()=>{
  await prepare('GPDWin5','none');await request('fan.setEnabled',{enabled:true});await waitForCompletedControl();await page.evaluate(()=>{window.__chain.modelPresetPersistence=true;window.__chain.rejectPresetSave=true;});
  const failed=await page.evaluate(()=>window.__mirror.request('fan.setPreset',{preset:'aggressive'},{generation:1,checkpoint(){}}).then(result=>({ok:true,result}),error=>({ok:false,error:String(error)})));
  assert.equal(failed.ok,false);assert.match(failed.error,/fixture-preset-save-failure/);assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,1);
  await page.evaluate(()=>window.__chain.rejectPresetSave=false);const retried=await request('fan.setPreset',{preset:'aggressive'});assert.equal(retried.applied,true);assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,2);
 });
 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' old cold choice cannot supersede newer original manual ON still in HC Open',async()=>{
  await prepare(family,'Open');
  await page.evaluate(()=>{const f=window.__chain;f.modelPresetPersistence=true;f.blockPendingSave=true;window.__oldColdChoice=window.__mirror.request('fan.setChoice',{choice:'aggressive'},{generation:1,checkpoint(){}}).then(result=>({ok:true,result}),error=>({ok:false,error:String(error)}));});
  await page.waitForFunction(()=>window.__chain.pendingSaveEntered);
  await page.evaluate(()=>{const f=window.__chain;f.foreignCurve=f.curves.soft.map(n=>({...n,dutyPercent:65}));f.lifecycle.stagePendingControlCurve(f.foreignCurve);window.__newerManualChoice=(async()=>{await f.lifecycle.start({manualRecovery:true,reason:'fixture-newer-manual-ON-during-choice-save'});return f.lifecycle.apply(f.foreignCurve);})().then(()=>({ok:true}),error=>({ok:false,error:String(error)}));});
  await page.waitForFunction(()=>window.__chain.gateReached==='Open');
  await recordPhase('newer-manual-Open-before-old-choice-save');
  await page.evaluate(()=>window.__chain.releasePendingSave());const selected=await page.evaluate(async()=>{const result=await window.__oldColdChoice;window.__chain.selectionResult=result;return result;});
  await page.evaluate(()=>window.__chain.releaseGate());const manual=await page.evaluate(()=>window.__newerManualChoice);
  await page.waitForFunction(()=>!window.__chain.mirrorExecPending);await page.waitForTimeout(40);
  await recordPhase('old-cold-choice-save-settled');
  const state=await snap(),writes=state.trace.filter(x=>x.op==='enable');
  assert.equal(manual.ok,true,'old cold choice retired the newer original ON: '+manual.error);
  assert.equal(writes.length,1,'old cold choice queued an extra enable');assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.foreignCurve));
  assert.equal(selected.ok,true);assert.equal(selected.result.saved,true);assert.equal(selected.result.applied,false,'old saved choice cannot claim to have applied its curve');
  await request('fan.setEnabled',{enabled:false});await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent&&!window.__chain.mirrorExecPending);await newOnWorks();
 });
 await check('cold choice accepted before a held power read also yields to newer pending manual control',async()=>{
  await prepare('ROGAlly','Open');
  await page.evaluate(()=>{const f=window.__chain;f.modelPresetPersistence=true;f.blockFirstMirrorPower=true;window.__oldColdChoice=window.__mirror.request('fan.setChoice',{choice:'aggressive'},{generation:1,checkpoint(){}}).then(result=>({ok:true,result}),error=>({ok:false,error:String(error)}));});
  await page.waitForFunction(()=>window.__chain.firstMirrorPowerEntered);
  await page.evaluate(()=>{const f=window.__chain;f.foreignCurve=f.curves.soft.map(n=>({...n,dutyPercent:65}));f.lifecycle.stagePendingControlCurve(f.foreignCurve);window.__newerManualChoice=(async()=>{await f.lifecycle.start({manualRecovery:true,reason:'fixture-newer-ON-during-choice-power'});return f.lifecycle.apply(f.foreignCurve);})().then(()=>({ok:true}),error=>({ok:false,error:String(error)}));});
  await page.waitForFunction(()=>window.__chain.gateReached==='Open');
  await page.evaluate(()=>window.__chain.releaseFirstMirrorPower());const selected=await page.evaluate(()=>window.__oldColdChoice);
  await page.evaluate(()=>window.__chain.releaseGate());const manual=await page.evaluate(()=>window.__newerManualChoice);
  await page.waitForFunction(()=>!window.__chain.mirrorExecPending);await page.waitForTimeout(40);
  assert.equal(manual.ok,true,'choice receipt after power replaced a newer manual request: '+manual.error);
  const s=await snap(),writes=s.trace.filter(x=>x.op==='enable');assert.equal(writes.length,1);assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.foreignCurve));
  assert.equal(selected.ok,true);assert.equal(selected.result.saved,true);assert.equal(selected.result.applied,false);
 });
 await check('cold choice save failure does not start a Host and the next explicit ON remains reachable',async()=>{
  await prepare('GPDWin5','none');await page.evaluate(()=>{window.__chain.modelPresetPersistence=true;window.__chain.rejectPresetSave=true;});
  const selected=await page.evaluate(()=>window.__mirror.request('fan.setChoice',{choice:'aggressive'},{generation:1,checkpoint(){}}).then(result=>({ok:true,result}),error=>({ok:false,error:String(error)})));
  assert.equal(selected.ok,false);assert.match(selected.error,/fixture-preset-save-failure/);const before=await snap();assert.equal(before.hasIntent,false);assert.equal(before.trace.filter(x=>['launch','Open','enable'].includes(x.op)).length,0);
  await page.evaluate(()=>window.__chain.rejectPresetSave=false);await newOnWorks();assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,1);
 });
 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])for(const outcome of ['power-ready','power-error','off'])await check(family+' failed staged save is observed during '+outcome+' and manual ON stays reachable',async()=>{
  await beginHeldOpen(family);
  await page.evaluate(outcome=>{const fx=window.__chain;fx.modelPresetPersistence=true;fx.rejectPresetSave=true;fx.blockFirstMirrorPower=true;fx.rejectFirstMirrorPower=outcome==='power-error';fx.pendingSaveRejections=[];window.addEventListener('unhandledrejection',event=>{fx.pendingSaveRejections.push(String(event.reason));event.preventDefault();});window.__failedPendingPreset=window.__mirror.request('fan.setPreset',{preset:'aggressive'},{generation:1,checkpoint(){}}).then(result=>({ok:true,result}),error=>({ok:false,error:String(error)}));},outcome);
  await page.waitForFunction(()=>window.__chain.firstMirrorPowerEntered);
  if(outcome==='off')await page.evaluate(()=>{window.__pendingSaveOff=window.__chain.lifecycle.disable().then(()=>({ok:true}),error=>({ok:false,error:String(error)}));});
  // Deliberate browser task turn with native read still held, not a performance timing.
  await page.waitForTimeout(80);
  await page.evaluate(()=>window.__chain.releaseFirstMirrorPower());
  const failure=await page.evaluate(()=>window.__failedPendingPreset);
  await page.evaluate(()=>window.__chain.releaseGate());if(outcome==='off'){const off=await page.evaluate(()=>window.__pendingSaveOff);assert.equal(off.ok,true);}
  await page.waitForFunction(()=>!window.__chain.mirrorExecPending);
  const rejections=await page.evaluate(()=>window.__chain.pendingSaveRejections);
  assert.equal(failure.ok,false);assert.match(failure.error,outcome==='power-ready'?/fixture-preset-save-failure/:outcome==='power-error'?/fixture-pending-power-failure/:/FAN_MIRROR_INTENT_CANCELED/);
  await page.evaluate(()=>{window.__chain.rejectPresetSave=false;window.__chain.rejectFirstMirrorPower=false;});await newOnWorks();
  assert.equal(rejections.length,0,'save rejection escaped before the public request could await it: '+rejections.join('|'));
 });
 await check('Self-heal UI: exhausted unreadable state exposes one-click retry and does not promise endless recovery',async()=>{
  await fresh('ClawCG3EM');
  await page.evaluate(()=>{const fx=window.__chain,host=fx.boundary;
    fx.selfHealEnable=host.enable.bind(host);fx.selfHealState=host.getState.bind(host);
    host.enable=async()=>{fx.note('self-heal-refused-enable');throw new window.__selfHealApiError('LEASE_INVALID',409,'LEASE_INVALID');};
    host.getState=async()=>{fx.note('self-heal-unreadable-state');throw Error('HTTP request timed out');};fx.releaseGate();});
  await page.waitForFunction(()=>window.__chain.lifecycle.recoveryNeedsAttention,{},{timeout:10000});
  await page.waitForFunction(()=>document.querySelector('.fan-status-banner')?.textContent?.includes('风扇自动恢复未完成'),{},{timeout:10000});
  const exhausted=await snap();assert.equal(exhausted.recoveryActive,false);assert.equal(exhausted.disabled,false);assert.doesNotMatch(exhausted.status,/正在恢复/);
  await page.evaluate(()=>{const fx=window.__chain;fx.boundary.enable=fx.selfHealEnable;fx.boundary.getState=fx.selfHealState;});
  await page.locator('.fan-toggle').click();await page.waitForFunction(()=>window.__chain.boundary.writes && window.__chain.lastControlActive && window.__chain.lifecycle.state==='ready');
  const recovered=await snap();assert.equal(recovered.state,'ready');assert.equal(recovered.recoveryActive,false);
 });
 await check('Cleanup self-heal UI: one ON survives pending cleanup and retired OEM-unknown owner',async()=>{
  await fresh('ClawCG3EM');await page.evaluate(()=>window.__chain.releaseGate());
  await page.waitForFunction(()=>window.__chain.boundary.writes&&window.__chain.lastControlActive);
  await page.waitForFunction(()=>!document.querySelector('.fan-toggle')?.disabled);
  await page.locator('.fan-toggle').click();await page.waitForFunction(()=>!window.__chain.lastControlActive);
  await page.waitForFunction(()=>!document.querySelector('.fan-toggle')?.disabled);
  const before=await snap();
  await page.evaluate(()=>{const fx=window.__chain,b=fx.boundary,raw=b.snapshot.bind(b),start=fx.lifecycle.launcher.start.bind(fx.lifecycle.launcher);
    fx.cleanupPending=true;b.state='AwaitingCloseCleanup';b.opened=b.eventsOpened=b.writes=false;b.lease=null;b.closeComplete=false;b.restored=false;
    b.snapshot=()=>({...raw(),hcCloseCleanupPending:fx.cleanupPending});
    fx.lifecycle.launcher.start=async(...args)=>{const result=await start(...args);b.state='AwaitingControl';b.terminalFault=false;b.opened=b.eventsOpened=b.writes=b.closeComplete=b.restored=false;b.lease=null;b.resumeGeneration=0;return result;};
  });
  await page.locator('.fan-toggle').click();await page.waitForFunction(()=>window.__chain.lifecycle.recoveryActive,{},{timeout:6000});
  const waiting=await snap();assert.match(waiting.status,/恢复|重新接管/);assert.equal(waiting.trace.filter(x=>x.op==='enable').length,before.trace.filter(x=>x.op==='enable').length);
  await page.evaluate(()=>{const fx=window.__chain;fx.cleanupPending=false;fx.boundary.closed('Stopped');fx.boundary.terminalFault=true;});
  await page.waitForFunction(()=>window.__chain.boundary.writes&&window.__chain.lastControlActive&&window.__chain.lifecycle.state==='ready'&&!window.__chain.lifecycle.recoveryActive,{},{timeout:15000});
  const settled=await snap();assert.equal(settled.trace.filter(x=>x.op==='enable').length,before.trace.filter(x=>x.op==='enable').length+1,'cleanup recovery required another click or wrote twice');
  assert.equal(settled.trace.filter(x=>x.op==='Close').length,before.trace.filter(x=>x.op==='Close').length,'completed predecessor Close was replayed');
 });
 await check('Terminal refusal UI: resolved permission allows one fresh ON after the fixed lock',async()=>{
  await fresh('ClawCG3EM');
  await page.evaluate(()=>{const fx=window.__chain,b=fx.boundary;fx.permissionEnable=b.enable.bind(b);b.enable=async()=>{fx.note('permission-denied-enable');throw new window.__selfHealApiError('permission rejected',403,'API_PERMISSION_DENIED');};fx.releaseGate();});
  await page.waitForFunction(()=>window.__chain.lifecycle.recoveryNeedsAttention,{},{timeout:8000});
  await page.waitForFunction(()=>!document.querySelector('.fan-toggle')?.disabled,{},{timeout:8000});
  const denied=await snap();assert.equal(denied.recoveryActive,false);assert.match(denied.button,/未操控/);assert.doesNotMatch(denied.status,/正在恢复/);assert.equal(denied.trace.filter(x=>x.op==='enable').length,0);
  await page.evaluate(()=>{const fx=window.__chain;fx.boundary.enable=fx.permissionEnable;});
  await page.locator('.fan-toggle').click();
  await page.waitForFunction(()=>window.__chain.boundary.writes&&window.__chain.lastControlActive&&window.__chain.lifecycle.state==='ready'&&!window.__chain.lifecycle.recoveryNeedsAttention,{},{timeout:10000});
  const recovered=await snap();assert.equal(recovered.trace.filter(x=>x.op==='enable').length,1,'fresh manual ON duplicated a write or required a second click');
 });
    assert.deepEqual(errors,[]);
 const sourceBinding={gateSha256:createHash('sha256').update(fs.readFileSync(import.meta.filename)).digest('hex'),uiSha256:createHash('sha256').update(fs.readFileSync(process.env.YMCC_FAN_MIRROR_CURVE_UI_BEFORE||path.join(root,'src/views/FanView.vue'))).digest('hex'),bridgeSha256:createHash('sha256').update(fs.readFileSync(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE||path.join(root,'src/bridge/fanHost.ts'))).digest('hex'),fixtureSha256:createHash('sha256').update(fs.readFileSync(fixture)).digest('hex'),mirrorSha256:createHash('sha256').update(fs.readFileSync(process.env.YMCC_FAN_MIRROR_SOURCE_BEFORE||path.join(root,'src/bridge/deckyFanActions.ts'))).digest('hex'),bundleSha256:createHash('sha256').update(js).digest('hex')};
 const summary={sourceBinding,ok:cases.every(x=>x.ok),checks:cases.length,cases,pageErrors:errors,scope:'actual mirror actions + original FanHostLifecycle/coordinator + Vue environment; inert HC Open/native/launcher/settings, ordered settings completions, no device/CPU benchmark'};
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify({ok:summary.ok,checks:summary.checks,results:path.join(out,'results.json')}));if(!summary.ok)process.exitCode=1;
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
