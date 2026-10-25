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
const out=path.resolve(process.env.YMCC_FAN_WAKE_EVIDENCE_DIR||path.join(root,'../../Build/Validation/fan-view-bridge-single-switch-20261006'));fs.mkdirSync(out,{recursive:true});
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
  b.onLoad({filter:/\.vue$/},a=>{const {descriptor,errors}=parse(fs.readFileSync(process.env.YMCC_FAN_WAKE_UI_BEFORE&&a.path.endsWith('FanView.vue')?process.env.YMCC_FAN_WAKE_UI_BEFORE:a.path,'utf8'),{filename:a.path});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8),code=compileScript(descriptor,{id,inlineTemplate:true});for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}return {contents:code.content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(a.path)};});
}}]});
const js=result.outputFiles[0].text,css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html=`<!doctype html><meta charset="utf-8"><title>Real Fan UI and lifecycle wake chain</title><style>${css}body{background:var(--bg-solid);color:var(--text);margin:0;padding:16px;}#app{max-width:760px;margin:auto}</style><div id="app"></div><script src="/chain.js"></script>`;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/chain.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/chain.js'?js:html);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const cases=[],errors=[];let browser,page;
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
 const url=(family)=>'http://127.0.0.1:'+server.address().port+'/?family='+family;
 const ops=()=>page.evaluate(()=>window.__chain.trace.map(t=>t.op));
 const success=async()=>{
  await page.waitForFunction(()=>window.__chain.boundary.writes===true&&!window.__chain.lifecycle.recoveryActive,null,{timeout:15000});
  // A fixture writer callback is not UI confirmation: wait for the actual
  // same-session state read to promote the button before testing a user OFF.
  await page.waitForFunction(()=>document.querySelector('.fan-toggle').getAttribute('aria-pressed')==='true',null,{timeout:6500});
 };
 const enabled=()=>page.waitForFunction(()=>!document.querySelector('.fan-toggle').disabled,null,{timeout:18000});
 const duplicateOnly=process.env.YMCC_FAN_WAKE_DUPLICATE_UI_ONLY==='1';
 const crossGenerationOnly=process.env.YMCC_FAN_WAKE_CROSS_GENERATION_ONLY==='1';
 if(crossGenerationOnly){
  const crossCases=[];
  for(const family of ['ClawCG3EM','ROGAlly','GPDWin5']){
   await page.goto(url(family));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();let error=null,evidence=null;
   try{
    await page.evaluate(async()=>{const f=window.__chain;Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});document.dispatchEvent(new Event('visibilitychange'));await f.sleep(2);f.setNative(2,'ready');await f.boundary.resume({generation:2});f.lifecycle.observePowerBoundary('resume-ready',2);const original=f.boundary.getState.bind(f.boundary);let held=false;f.crossReadEntered=false;f.boundary.getState=async(...args)=>{const state=await original(...args);if(!held){held=true;f.crossReadEntered=true;await new Promise(resolve=>f.crossReleaseRead=resolve);}return state;};window.__crossOld=f.lifecycle.resume().then(()=> 'accepted',e=>String(e));});
    await page.waitForFunction(()=>window.__chain.crossReadEntered);
    await page.evaluate(()=>{const f=window.__chain;window.__crossLatest=(async()=>{await f.sleep(3);f.setNative(3,'ready');await f.boundary.resume({generation:3});await f.autoWake();})();});
    await page.waitForFunction(()=>window.__chain.lifecycle.coordinatorSnapshot.wakeReadyGeneration===3);
    await page.evaluate(()=>window.__chain.crossReleaseRead());
    await page.evaluate(()=>Promise.all([window.__crossOld,window.__crossLatest]));
    evidence=await page.evaluate(()=>({hidden:document.hidden,state:window.__chain.lifecycle.state,writes:window.__chain.boundary.writes,hasIntent:window.__chain.lifecycle.hasControlIntent,recoveryActive:window.__chain.lifecycle.recoveryActive,trace:window.__chain.trace}));
    assert.equal(evidence.hidden,true);assert.equal(evidence.writes,true,'the newer valid wake must recover without the hidden page polling or another click');assert.equal(evidence.state,'ready');assert.equal(evidence.trace.filter(x=>x.op==='enable'&&x.generation===2).length,0,'retired prior wake wrote after the next sleep');assert.equal(evidence.trace.filter(x=>x.op==='enable'&&x.generation===3).length,1,'new wake must replay exactly once');assert.equal(evidence.trace.filter(x=>x.op==='launch').length,1,'new wake must reuse the same Host owner');
    await page.evaluate(()=>{delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));});await success();await enabled();await page.locator('.fan-toggle').click();await enabled();assert.equal(await page.evaluate(()=>window.__chain.lifecycle.hasControlIntent),false,'same-button OFF must still complete after overlapping wake recovery');
   }catch(e){error=String(e);}finally{await page.evaluate(()=>{window.__chain.crossReleaseRead?.();delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));}).catch(()=>{});}
   crossCases.push({family,ok:error===null,error,evidence});fs.writeFileSync(path.join(out,'cross-generation-results.json'),JSON.stringify({ok:crossCases.every(x=>x.ok),checks:crossCases.length,cases:crossCases},null,2));cases.push(family+': held old wake -> next wake while page hidden -> one current replay -> same-button OFF');
  }
  assert.deepEqual(crossCases.filter(x=>!x.ok).map(x=>({family:x.family,error:x.error})),[]);
 }else if(duplicateOnly){
  const duplicateResults=[];
  const snapshot=()=>page.evaluate(()=>{const button=document.querySelector('.fan-toggle');return {text:button.textContent,pressed:button.getAttribute('aria-pressed'),busy:button.getAttribute('aria-busy'),disabled:button.disabled,notice:document.querySelector('.fan-status-banner').textContent,active:window.__chain.lastControlActive,writes:window.__chain.boundary.writes,intentRevision:window.__chain.lifecycle.controlIntentRevision,cancellationRevision:window.__chain.lifecycle.controlCancellationRevision,trace:window.__chain.trace};});
  for(const family of ['ClawCG3EM','ROGAlly','GPDWin5']){
   await page.goto(url(family));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
   await page.evaluate(async()=>{await window.__chain.sleep(2);await window.__chain.autoWake();});await success();await enabled();
   const before=await snapshot();assert.equal(before.pressed,'true');assert.equal(before.disabled,false);
   await page.evaluate(()=>{const f=window.__chain,original=f.boundary.getState.bind(f.boundary);let resolve;const held=new Promise(r=>resolve=r);f.duplicateUiReads=0;f.releaseDuplicateUiRead=()=>{f.boundary.getState=original;resolve();};f.boundary.getState=async(...args)=>{f.duplicateUiReads++;await held;return original(...args);};});
   let error=null,after,reads,clickBoundary;
   try{
    await page.evaluate(()=>{for(const name of ['ipc:fan.resume-ready','ipc:power.resume-ready','ipc:power.resumed'])window.dispatchEvent(new CustomEvent(name,{detail:{generation:2}}));});
    after=await snapshot();reads=await page.evaluate(()=>window.__chain.duplicateUiReads);
    await page.locator('.fan-toggle').click();await page.waitForFunction(previous=>window.__chain.lifecycle.controlCancellationRevision>previous.cancellationRevision||window.__chain.lifecycle.controlIntentRevision>previous.intentRevision,before,{timeout:3000});clickBoundary=await snapshot();
    assert.equal(after.pressed,'true','a late same-generation wake must not revoke confirmed control');assert.equal(after.text,'风扇已开启');assert.equal(after.busy,'false');assert.equal(after.disabled,false,'a repeated edge cannot restart button protection');
    assert.equal(after.active,true,'same-generation event cannot revoke navigation control');assert.equal(after.writes,true);assert.equal(after.cancellationRevision,before.cancellationRevision);assert.equal(after.intentRevision,before.intentRevision);
    assert.equal(after.trace.filter(t=>t.op==='enable').length,before.trace.filter(t=>t.op==='enable').length,'late notifications cannot reapply the current curve');
    assert.equal(reads,0,'already confirmed duplicate edge must not add a state query');
    assert(clickBoundary.cancellationRevision>before.cancellationRevision,'the first click after a duplicate must cancel control, not restage ON');
   }catch(e){error=String(e);}finally{await page.evaluate(()=>window.__chain.releaseDuplicateUiRead());}
   if(!error){await enabled();assert.equal(await page.evaluate(()=>window.__chain.lifecycle.hasControlIntent),false,'the first following click must turn OFF, not restage ON');assert.equal((await ops()).filter(op=>op==='enable').length,before.trace.filter(t=>t.op==='enable').length,'OFF after duplicate events must not write another curve');}
   duplicateResults.push({family,ok:error===null,error,before,after,reads,clickBoundary});fs.writeFileSync(path.join(out,'duplicate-results.json'),JSON.stringify({ok:duplicateResults.every(x=>x.ok),checks:duplicateResults.length,cases:duplicateResults},null,2));
   cases.push(family+': confirmed wake + late fan/shared aliases retains ON, no new query/replay, next click is OFF');
  }
  assert.deepEqual(duplicateResults.filter(x=>!x.ok).map(x=>({family:x.family,error:x.error})),[]);
 }else{
 const pendingFirstOnly=process.env.YMCC_FAN_WAKE_PENDING_FIRST_ONLY==='1';
 if(pendingFirstOnly){
  for(const family of ['ROGAlly','ClawA2VM','ClawCG3EM','GPDWin5','AYANEOAIR','OneXPlayer2','LokiMax6800U','LegionGoTablet']){
   await page.goto(url(family)+'&cold=1');await enabled();
   await page.evaluate(()=>{window.__chain.holdAt='Open';window.__chain.setNative(1,'ready');window.__chain.native.resumeReady=false;});
   await page.locator('.fan-toggle').click();
   await page.waitForFunction(()=>window.__chain.gateReached==='Open');
   assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),false,'fixture must reproduce initial ON before any accepted write');
   await page.evaluate(()=>{window.__pendingFirstSleep=window.__chain.sleep(2);});
   await page.evaluate(()=>window.__chain.releaseGate?.());
   await page.evaluate(()=>window.__pendingFirstSleep);
   assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),false,'sleep must cancel the old operation without writing');
   await page.evaluate(()=>window.__chain.autoWake());
   await success();
   const trace=await page.evaluate(()=>window.__chain.trace);
   assert.equal(trace.filter(t=>t.op==='enable').length,1,'one original click must recover exactly one accepted curve');
   assert.equal(trace.find(t=>t.op==='enable').generation,2,'only current wake generation may write');
   assert.equal(trace.filter(t=>t.op==='launch').length,1,'same resident owner, no second Host');
   assert(trace.some(t=>t.op==='Open-superseded'),'original Open must actually reject on sleep');
   assert.equal(await page.evaluate(()=>window.__chain.boundary.snapshot().oemPhysicalOwnershipConfirmed),false);
   cases.push(family+': first ON interrupted before Open -> confirmed wake -> one current-generation write without another click');
  }
  for(const family of ['ROGAlly','ClawCG3EM','GPDWin5']){
   await page.goto(url(family));await enabled();await page.locator('.fan-toggle').click();await success();
   await page.evaluate(async()=>{await window.__chain.sleep(1);window.__chain.slowResumeMs=1200;window.__wakeStatusTask=window.__chain.autoWake();});
   await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='正在恢复…');
   const progress=await page.evaluate(()=>{const button=document.querySelector('.fan-toggle');return{text:button.textContent,busy:button.getAttribute('aria-busy'),pressed:button.getAttribute('aria-pressed'),pending:button.classList.contains('fan-toggle-pending'),opacity:getComputedStyle(button).opacity,notice:document.querySelector('.fan-status-banner').textContent,writes:window.__chain.boundary.writes};});
   assert.match(progress.notice,/正在.*(恢复|接管|连接|重建)/,'banner must describe the same recovery operation as the switch');
   assert.equal(progress.busy,'true');assert.equal(progress.pressed,'false','recovery must not claim live hardware control');assert.equal(progress.pending,true);assert.equal(progress.opacity,'1','pending switch must not look like a grey OFF');assert.equal(progress.writes,false);
   assert.equal(await page.locator('.fan-toggle').isDisabled(),true,'wake has a five-second click guard');
   if(family==='ClawCG3EM')await page.screenshot({path:path.join(out,'wake-recovery-visible.png'),fullPage:true});
   if(family==='ClawCG3EM'){
    // Observe the guard before success() waits for the independent five-second
    // telemetry confirmation. Waiting for success first would sample too late.
    await page.waitForTimeout(3000);
    assert.equal(await page.locator('.fan-toggle').isDisabled(),true,'early completion cannot unlock the first five seconds');
    assert.equal(await page.locator('.fan-toggle').evaluate(button=>getComputedStyle(button).opacity),'1','protected recovery/ON must remain bright');
    await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:power.resumed',{detail:{generation:window.__chain.native.generation}})));
    await page.waitForFunction(()=>!document.querySelector('.fan-toggle').disabled,null,{timeout:2300});
   }
   await page.evaluate(()=>window.__wakeStatusTask);await success();
   assert.equal(await page.locator('.fan-toggle').textContent(),'风扇已开启');
   assert.equal(await page.locator('.fan-toggle').getAttribute('aria-busy'),'false');
   assert.equal(await page.locator('.fan-toggle').evaluate(button=>button.classList.contains('fan-toggle-pending')),false);
   await enabled();

   cases.push(family+': wake banner and visible recovering switch agree, no false ON; actual receipt completes both');
  }
  for(const family of ['ROGAlly','ClawCG3EM','GPDWin5']){
   await page.goto(url(family)+'&cold=1');await enabled();await page.evaluate(()=>{window.__chain.holdAt='Open';window.__chain.setNative(1,'ready');window.__chain.native.resumeReady=false;});
   await page.locator('.fan-toggle').click();await page.waitForFunction(()=>window.__chain.gateReached==='Open');
   await page.evaluate(()=>{window.__pendingFirstOff=window.__chain.lifecycle.disable().catch(()=>undefined);window.__pendingFirstSleep=window.__chain.sleep(2);});
   await page.evaluate(()=>window.__chain.releaseGate?.());
   await page.evaluate(async()=>{await window.__pendingFirstOff;await window.__pendingFirstSleep;await window.__chain.autoWake();});
   await page.waitForTimeout(300);
   assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length),0,'explicit OFF must cancel even the retained first ON');
   assert.equal(await page.evaluate(()=>window.__chain.lifecycle.hasControlIntent),false,'OFF cannot become a future auto-start');
   cases.push(family+': explicit OFF cancels pending first ON before wake with zero later writes');
  }
 }
 if(!pendingFirstOnly){
 const coldOnly=process.env.YMCC_FAN_WAKE_COLD_ONLY==='1';
  const physicalOnly=process.env.YMCC_FAN_WAKE_PHYSICAL_ONLY==='1';
 const terminalOnly=process.env.YMCC_FAN_WAKE_REMOTE_TERMINAL_ONLY==='1';
 if(terminalOnly){
  for(const family of ['ROGAlly','ClawA2VM']){
   await page.goto(url(family));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
   const baseline=await page.evaluate(()=>({local:window.__chain.lifecycle.state,lease:window.__chain.lifecycle.currentLease,trace:window.__chain.trace.length}));
   assert.equal(baseline.local,'ready');assert(baseline.lease,'fixture must retain the live local lease');
   await page.evaluate(async()=>{
    // No notification or telemetry sync: the bridge still owns its old Ready,
    // lease and active curve, while the authenticated Host has faulted.
    window.__chain.boundary.terminalFault=true;window.__chain.boundary.writes=false;
    await window.__chain.lifecycle.apply(window.__chain.curves.aggressive);
   });
   const observed=await page.evaluate(()=>({state:window.__chain.lifecycle.state,writes:window.__chain.boundary.writes,trace:window.__chain.trace}));
   const trace=observed.trace.slice(baseline.trace);
   assert.equal(observed.state,'ready');assert.equal(observed.writes,true,'same explicit adjustment must restore control after a remote fault');
   assert.equal(trace.filter(t=>t.op==='Close').length,1,'one Close owner');
   assert.equal(trace.filter(t=>t.op==='stop').length,1,'predecessor exits once');
   assert.equal(trace.filter(t=>t.op==='launch').length,1,'one successor');
   assert.equal(trace.filter(t=>t.op==='enable').length,1,'one accepted write after recovery');
   assert(trace.find(t=>t.op==='enable').nodes.some(n=>n.tempC===40&&n.dutyPercent===30),'latest requested curve survives replacement');
   cases.push(family+': stale local Ready+lease+curve with remote FaultLocked -> same apply -> one Close/exit/successor -> requested curve');
  }
  for(const cancel of ['off','sleep']){
   await page.goto(url('ROGAlly'));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
   await page.evaluate(()=>{
    window.__chain.boundary.terminalFault=true;window.__chain.boundary.writes=false;window.__chain.holdAt='Close';
    window.__terminalPending=window.__chain.lifecycle.apply(window.__chain.curves.aggressive).then(()=> 'accepted',e=>String(e.message));
   });
   await page.waitForFunction(()=>window.__chain.gateReached==='Close');
   await page.evaluate(cancel=>{
    if(cancel==='off')window.__terminalCancel=window.__chain.lifecycle.disable().catch(()=>undefined);
    else {window.__chain.setNative(1,'suspending');window.__chain.lifecycle.setPowerGeneration(1);window.__chain.lifecycle.observePowerBoundary('suspending',1);}
    window.__chain.releaseGate();
   },cancel);
   const outcome=await page.evaluate(async()=>{const result=await window.__terminalPending;await window.__terminalCancel;return result;});
   assert.match(outcome,/FAN_CONTROL_INTENT_SUPERSEDED/,'OFF/new sleep must cancel the original manual recovery');
   const trace=await page.evaluate(()=>window.__chain.trace);
   assert.equal(trace.filter(t=>t.op==='enable').length,1,'cancelled recovery never writes a second curve');
   assert.equal(trace.filter(t=>t.op==='launch').length,1,'cancelled recovery cannot launch a successor');
   assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),false);
   cases.push('ROGAlly: '+cancel+' while terminal Close is pending cancels successor launch and old curve');
  }
  // The newest explicit curve must continue after an older terminal Close,
  // even though that Close retires the Host PID captured by the new request.
  for(const [held,action] of [['Close','apply'],['Close','preset'],['launch','apply'],['launch','preset']]){
   await page.goto(url('ROGAlly'));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
   await page.evaluate(held=>{
    window.__chain.boundary.terminalFault=true;window.__chain.boundary.writes=false;window.__chain.holdAt=held;
    window.__olderIntent=window.__chain.lifecycle.apply(window.__chain.curves.aggressive).then(()=> 'accepted',e=>String(e.message));
   },held);
   await page.waitForFunction(held=>window.__chain.gateReached===held,held);
   await page.evaluate(action=>{
    const curve=window.__chain.curves.soft;
    window.__latestIntent=(action==='apply'?window.__chain.lifecycle.apply(curve):window.__chain.lifecycle.applyPreset('soft',curve)).then(()=> 'accepted',e=>String(e.message));
    window.__chain.releaseGate();
   },action);
   const outcome=await page.evaluate(async()=>({old:await window.__olderIntent,latest:await window.__latestIntent}));
   assert.match(outcome.old,/FAN_CONTROL_INTENT_SUPERSEDED/,'latest curve retires the older continuation');
   assert.equal(outcome.latest,'accepted','one newest '+action+' must continue after predecessor Close without a second click');
   const state=await page.evaluate(()=>({trace:window.__chain.trace,state:window.__chain.lifecycle.state,writes:window.__chain.boundary.writes}));
   assert.equal(state.state,'ready');assert.equal(state.writes,true);
   assert.equal(state.trace.filter(t=>t.op==='Close').length,1,'new intent must not duplicate virtual Close');
   assert.equal(state.trace.filter(t=>t.op==='launch').length,2,'one successor serves the latest intent');
   const writes=state.trace.filter(t=>t.op==='enable');assert.equal(writes.length,2,'only initial and newest curves are written');
   assert(writes[1].nodes.some(n=>n.tempC===70&&n.dutyPercent===20),'only the requested soft curve reaches the successor');
   cases.push('new '+action+' while terminal '+held+' waits -> old request cancelled -> one successor writes latest curve');
  }
  // An unconfirmed exact exit is not authority to adopt/launch; a later
  // explicit retry must finish that same exit and reuse its confirmed Close.
  await page.goto(url('ROGAlly'));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
  await page.evaluate(()=>{
   window.__chain.boundary.terminalFault=true;window.__chain.boundary.writes=false;window.__chain.holdAt='Close';window.__chain.stopFailures=10;
   window.__olderIntent=window.__chain.lifecycle.apply(window.__chain.curves.aggressive).then(()=> 'accepted',e=>String(e.message));
  });
  await page.waitForFunction(()=>window.__chain.gateReached==='Close');
  await page.evaluate(()=>{
   window.__latestIntent=window.__chain.lifecycle.apply(window.__chain.curves.soft).then(()=> 'accepted',e=>String(e.message));window.__chain.releaseGate();
  });
  const unconfirmed=await page.evaluate(async()=>({old:await window.__olderIntent,latest:await window.__latestIntent}));
  assert.notEqual(unconfirmed.latest,'accepted','a failed stop is never a receipt for the new writer');
  let stoppedTrace=await page.evaluate(()=>window.__chain.trace);
  assert.equal(stoppedTrace.filter(t=>t.op==='launch').length,1,'unconfirmed exit cannot spawn a successor');
  assert.equal(stoppedTrace.filter(t=>t.op==='Close').length,1,'confirmed Close must not repeat while waiting for exact exit');
  await page.evaluate(async()=>{window.__chain.stopFailures=0;await window.__chain.lifecycle.apply(window.__chain.curves.soft);});
  stoppedTrace=await page.evaluate(()=>window.__chain.trace);
  assert.equal(stoppedTrace.filter(t=>t.op==='Close').length,1,'later retry reuses the same Close receipt');
  assert.equal(stoppedTrace.filter(t=>t.op==='launch').length,2,'confirmed exit permits exactly one successor');
  assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),true);
  cases.push('failed exact exit blocks successor; later single manual apply completes exit without repeated Close');
  for(const cancel of ['off','sleep']){
   await page.goto(url('ROGAlly'));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
   await page.evaluate(()=>{
    window.__chain.boundary.terminalFault=true;window.__chain.boundary.writes=false;window.__chain.holdAt='Close';
    window.__olderIntent=window.__chain.lifecycle.apply(window.__chain.curves.aggressive).then(()=> 'accepted',e=>String(e.message));
   });
   await page.waitForFunction(()=>window.__chain.gateReached==='Close');
   await page.evaluate(cancel=>{
    window.__latestIntent=window.__chain.lifecycle.apply(window.__chain.curves.soft).then(()=> 'accepted',e=>String(e.message));
    if(cancel==='off')window.__latestCancel=window.__chain.lifecycle.disable().catch(()=>undefined);
    else {window.__chain.setNative(1,'suspending');window.__chain.lifecycle.setPowerGeneration(1);window.__chain.lifecycle.observePowerBoundary('suspending',1);}
    window.__chain.releaseGate();
   },cancel);
   const outcome=await page.evaluate(async()=>({old:await window.__olderIntent,latest:await window.__latestIntent,cancel:await window.__latestCancel}));
   assert.match(outcome.old,/FAN_CONTROL_INTENT_SUPERSEDED/);assert.match(outcome.latest,/FAN_CONTROL_INTENT_SUPERSEDED/,'joined startup cannot revive an intent cancelled by '+cancel);
   const trace=await page.evaluate(()=>window.__chain.trace);
   assert.equal(trace.filter(t=>t.op==='enable').length,1);assert.equal(trace.filter(t=>t.op==='launch').length,1);
   assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),false);
   cases.push('latest curve joins terminal startup; later '+cancel+' cancels both requests and prevents successor writes');
  }
 }
 if(!terminalOnly){
 if(!coldOnly&&!physicalOnly){
 for(const family of ['ROGAlly','ClawA2VM']) {
  await page.goto(url(family));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
  await page.evaluate(()=>{window.__chain.holdAt='heartbeat';window.__chain.heartbeatFailure=true;window.__heartbeat=window.__chain.lifecycle.heartbeat().catch(()=>{});});
  await page.waitForFunction(()=>window.__chain.gateReached==='heartbeat');
  await page.locator('.fan-toggle').click();await enabled();
  assert.equal(await page.locator('.fan-toggle').innerText(),'风扇未操控','off remains actionable while the renewal is pending');
  await page.evaluate(()=>window.__chain.releaseGate?.());
  await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent);
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length),1,'late failed heartbeat must not write the cancelled curve');
  assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),false,'off remains off after lease handoff');
  await page.evaluate(()=>{window.__chain.heartbeatFailure=false;});
  await page.locator('.fan-toggle').click();await success();await enabled();
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length),2,'next explicit on must make exactly one new write');
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='launch').length),1,'off/on continues on the single resident owner');
  assert.equal(await page.evaluate(()=>window.__chain.boundary.snapshot().oemPhysicalOwnershipConfirmed),false);
  cases.push(family+' held heartbeat -> actionable off -> no old-curve rewrite -> same-switch on works');
 }
 for(const family of ['ROGAlly','ClawA2VM','GPDWin5','AYANEOAIR','OneXPlayer2','LokiMax6800U','GamingZone','LegionGoTablet']){
  await page.goto(url(family));await enabled();assert.deepEqual(await ops(),[],family+': mounting must not launch, probe or write');
  assert.equal(await page.locator('.fan-toggle').textContent(),'风扇未操控');
  assert.equal(await page.locator('.fan-close').count(),0);
  assert.equal(await page.locator('.fan-status-banner span').count(),0);
  await page.locator('.fan-toggle').click();await success();await enabled();
  await page.evaluate(()=>window.__chain.sleep(1));await page.evaluate(()=>window.__chain.autoWake());await success();
  await page.evaluate(()=>window.__chain.sleep(2,false));
  if(await page.locator('.fan-toggle').getAttribute('aria-pressed')==='true'){
   await page.locator('.fan-toggle').click();await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇未操控');await enabled();
  }
  await page.locator('.fan-toggle').click();await success();await enabled();
  const trace=await page.evaluate(()=>window.__chain.trace.filter(t=>t.generation===2));
  assert.equal(trace.filter(t=>t.op==='resume').length,1,family+': missed S0 edge requires one Host resume');
  for(const op of ['fanManualWake','Open','OpenEvents','acquire','enable'])assert(trace.some(t=>t.op===op),family+': missing '+op);
  const n=await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length);
  await page.locator('.fan-graph').count();
  await page.evaluate(async()=>window.__chain.lifecycle.apply(window.__chain.curves.aggressive));
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length),n+1,family+': subsequent curve must write');
  await page.locator('.fan-toggle').click();await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent);
  assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),false,family+': close stops curve control');
  assert.equal(await page.locator('.fan-toggle').textContent(),'风扇未操控');
  cases.push(family+': real UI/bridge cold enable -> auto wake -> missed-edge manual wake -> next curve -> close');
 }
 // Authenticated Host terminal state only; the hardware remains available.
 for(const family of ['ROGAlly','ClawA2VM']){
  await page.goto(url(family));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
  await page.evaluate(()=>{
    // Durable awake generation with a lost renderer edge; preserve the on-intent
    // so the first binary-switch click really is an explicit cancellation.
    window.__chain.setNative(1,'ready');
    window.__chain.lifecycle.setPowerGeneration(1);
    window.__chain.lifecycle.observePowerBoundary('resume-ready',1);
    window.__chain.boundary.terminalFault=true;window.__chain.boundary.writes=false;
  });
  // Cancel the old on-intent, then make one explicit off->on request.
  await page.locator('.fan-toggle').click();await enabled();
  assert.equal(await page.locator('.fan-toggle').textContent(),'风扇未操控','failed release must leave the control switch actionable');
  await page.locator('.fan-toggle').click();await success();await enabled();
  const trace=await page.evaluate(()=>window.__chain.trace);
  assert.equal(trace.filter(t=>t.op==='Close').length,1,'one explicit terminal retry must own one virtual Close');
  assert.equal(trace.filter(t=>t.op==='stop').length,1,'the predecessor must stop before replacement');
  assert.equal(trace.filter(t=>t.op==='launch').length,2,'one explicit retry must launch exactly one successor');
  assert.equal(await page.evaluate(()=>window.__chain.boundary.snapshot().oemPhysicalOwnershipConfirmed),false,'physical OEM proof must not be invented or required');
  await page.locator('.fan-toggle').click();await enabled();
  cases.push(family+' awake generation1 terminal fault -> actionable off/on -> one Close and successor -> curve effective');
 }
 // FAN-941: real clicks during a delayed device boundary; no synthetic guard events.
 for(const [family,heldAt] of [['ROGAlly','Open'],['ROGAlly','acquire'],['ClawA2VM','acquire']]){
  await page.goto(url(family));await enabled();
  await page.evaluate(at=>{window.__chain.holdAt=at;},heldAt);
  await page.locator('.fan-toggle').click();
  await page.waitForFunction(at=>window.__chain.gateReached===at,heldAt);
  assert.equal(await page.locator('.fan-toggle').isDisabled(),true,'initial device admission remains protected');
  await page.evaluate(()=>{window.__externalOff=window.__chain.lifecycle.disable();});
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length),0,'held boundary must not write');
  await page.evaluate(()=>window.__chain.releaseGate());await page.evaluate(()=>window.__externalOff);await enabled();
  await page.waitForFunction(()=>window.__chain.trace.some(t=>t.op==='restore')&&!window.__chain.lifecycle.hasControlIntent);
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length),0,'cancelled before the writer: no late enable');
  assert.equal(await page.locator('.fan-toggle').textContent(),'风扇未操控','late completion must not revive an old UI intent');
  await page.locator('.fan-toggle').click();await success();await enabled();
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length),1,'the next explicit click must write exactly once');
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='launch').length),1,'retry must reuse the single owned Host');
  await page.locator('.fan-toggle').click();await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent);await enabled();
  cases.push(family+' explicit external OFF during '+heldAt+' -> zero late writes -> manual re-enable on one owner');
 }
 await page.goto(url('ROGAlly'));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
 await page.evaluate(async()=>{await window.__chain.sleep(1);window.__chain.slowResumeMs=10500;});
 await page.locator('.fan-toggle').click();await enabled();
 await success();const slow=await page.evaluate(()=>window.__chain.trace.filter(t=>t.generation===1));
 assert.equal(slow.filter(t=>t.op==='resume').length,1,'slow ten-second rebuild must stay single-owner');
 assert.equal(slow.filter(t=>t.op==='enable').length,1,'a slow admitted request must not duplicate curve writes');
 cases.push('ROG slow 10.5s rebuild: actionable single switch, one resume and one curve write');
 await page.locator('.fan-toggle').click();await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent);
 await page.goto(url('ROGAlly'));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
 await page.evaluate(async()=>{await window.__chain.sleep(1);window.__chain.slowResumeMs=1000;});
 await page.locator('.fan-toggle').click();await page.evaluate(()=>window.__chain.lifecycle.disable());
 await page.waitForTimeout(1800);assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),false,'closing during a rebuild must not be followed by a late curve write');
 assert.equal(await page.evaluate(()=>window.__chain.lifecycle.hasControlIntent),false,'close must cancel the intent');
 cases.push('ROG close during rebuild cancels intent and rejects any late control write');
 await page.goto(url('ROGAlly'));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
 await page.evaluate(async()=>{await window.__chain.sleep(1);window.__chain.setNative(1,'suspending');});
 await page.locator('.fan-toggle').click();await enabled();
 await page.waitForTimeout(700);
 assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),false,'actual Suspending must not admit a curve');
 assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.generation===1&&['Open','OpenEvents','resume','enable'].includes(t.op)).length),0,'actual Suspending must perform no HC session operation');
 assert.equal(await page.evaluate(()=>window.__chain.lifecycle.hasControlIntent),true,'sleep rejection must keep the requested control intent');
 await page.evaluate(()=>window.__chain.setNative(1,'ready'));await success();await enabled();
 assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.generation===1&&t.op==='enable').length),1,'the retained request must recover once after actual sleep ends');
 cases.push('ROG real Suspending blocks HC operations, keeps the button and intent, then automatically rearms after safe wake');
 await page.goto(url('ROGAlly'));await enabled();assert.equal(await page.evaluate(()=>window.__chain.lifecycle.hasControlIntent),false);
 await page.evaluate(()=>window.__chain.sleep(1,false));await page.locator('.fan-toggle').click();await success();await enabled();
 assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),true,'first-ever enable after a missed sleep must actually enable');
 cases.push('First manual enable after unseen sleep without prior control intent');
 await page.goto(url('ROGAlly'));await enabled();
 await page.evaluate(async()=>{await window.__chain.sleep(1);window.__chain.setNative(1,'suspending');});
 await page.locator('.fan-toggle').click();await enabled();await page.waitForTimeout(500);
 assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),false,'first enable during true Suspending must perform no write');
 await page.evaluate(()=>window.__chain.setNative(1,'ready'));await success();await enabled();
 assert.equal(await page.evaluate(()=>window.__chain.boundary.writes),true,'first enable retains its request through actual sleep without prior control intent');
 cases.push('First manual enable during true Suspending, then safe wake, without prior intent');
 }
 // FAN956: actual DOM first click after S4, with exact cold Stopped/zero receipt.
 if(!physicalOnly) for(const family of ['ClawCG3EM','ROGAlly']){
  await page.goto(url(family)+'&cold=1');await enabled();
  await page.evaluate(()=>{window.__chain.setNative(2,'ready');window.__chain.lifecycle.setPowerGeneration(2);window.__chain.lifecycle.observePowerBoundary('resume-ready',2);});
  await page.locator('.fan-toggle').click();await success();await enabled();
  const trace=await page.evaluate(()=>window.__chain.trace);
  for(const op of ['launch','Open','OpenEvents','acquire','enable'])assert.equal(trace.filter(t=>t.op===op).length,1,family+': one click must complete exactly one '+op);
  assert.equal(trace.filter(t=>t.op==='resume').length,0,'new Host cannot resume a sleep it never observed');
  assert.equal(await page.locator('.fan-toggle').textContent(),'风扇已开启','only after current Host confirmation');
  await page.locator('.fan-toggle').click();await enabled();
  cases.push('FAN956 '+family+' cold Stopped after S4: first real click -> actual HC Open/Events/lease/enable once, no fictitious resume');
 }
 if(!coldOnly){
 for(const family of ['ClawCG3EM','ROGAlly','GPDWin5']){
  await page.goto(url(family));await enabled();await page.locator('.fan-toggle').click();await success();await enabled();
  for(const generation of [3,4]){
   await page.evaluate(g=>window.__chain.filteredPhysicalCycle(g),generation);await success();await enabled();
   const state=await page.evaluate(()=>({shared:window.__chain.sharedGeneration,phase:window.__chain.sharedPhase,
    fan:window.__chain.native.generation,trace:window.__chain.trace}));
   assert.equal(state.shared,2,'Fan must not advance shared sleep generation');assert.equal(state.phase,'suspended','Fan must not commit shared game/input recovery');
   assert.equal(state.fan,generation,'new physical sleep has a new Fan generation');
   assert.equal(state.trace.filter(t=>t.generation===generation&&t.op==='resume').length,1,'one real Host resume per physical cycle');
   assert.equal(state.trace.filter(t=>t.generation===generation&&t.op==='enable').length,1,'retained user curve actually replays once');
   await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation:2}})));
   assert.equal(await page.locator('.fan-toggle').getAttribute('aria-pressed'),'true','old shared edge cannot hide current Fan control');
  }
  await page.locator('.fan-toggle').click();await enabled();assert.equal(await page.evaluate(()=>window.__chain.lifecycle.hasControlIntent),false);
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='launch').length),1,'private cycles do not launch multiple Hosts');
  cases.push('FAN956 '+family+': two filtered physical cycles, shared generation/phase unchanged, actual curve replay and same-button OFF');
 }
 }
 } // normal full/cold/physical modes
 } // existing full suite; first-start interruption has its own mode
 } // late-duplicate UI mode vs existing wake suites
 assert.deepEqual(errors,[]);
 const report={passed:cases.length,cases,pageErrors:errors,scope:'real FanView.vue DOM handlers + real FanHostLifecycle/coordinator/timers; Host adapter, native power IPC and launcher are fixtures; no physical proof'};
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}catch(error){fs.writeFileSync(path.join(out,'completed-before-failure.json'),JSON.stringify(cases,null,2));if(page){const failure=await page.evaluate(()=>({trace:window.__chain.trace,localState:window.__chain.lifecycle.state,coordinator:window.__chain.lifecycle.coordinatorSnapshot,recoveryActive:window.__chain.lifecycle.recoveryActive,hasIntent:window.__chain.lifecycle.hasControlIntent,native:window.__chain.native,ui:document.body.innerText}));fs.writeFileSync(path.join(out,'failure.json'),JSON.stringify(failure,null,2));console.error(JSON.stringify(failure,null,2));}throw error;}finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}