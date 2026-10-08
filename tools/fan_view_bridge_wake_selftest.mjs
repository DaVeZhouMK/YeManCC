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
  b.onLoad({filter:/\.vue$/},a=>{const {descriptor,errors}=parse(fs.readFileSync(a.path,'utf8'),{filename:a.path});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8),code=compileScript(descriptor,{id,inlineTemplate:true});for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}return {contents:code.content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(a.path)};});
}}]});
const js=result.outputFiles[0].text,css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html=`<!doctype html><meta charset="utf-8"><title>Real Fan UI and lifecycle wake chain</title><style>${css}</style><div id="app"></div><script src="/chain.js"></script>`;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/chain.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/chain.js'?js:html);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const cases=[],errors=[];let browser,page;
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
 const url=(family)=>'http://127.0.0.1:'+server.address().port+'/?family='+family;
 const ops=()=>page.evaluate(()=>window.__chain.trace.map(t=>t.op));
 const success=()=>page.waitForFunction(()=>window.__chain.boundary.writes===true&&!window.__chain.lifecycle.recoveryActive,null,{timeout:15000});
 const enabled=()=>page.waitForFunction(()=>!document.querySelector('.fan-toggle').disabled,null,{timeout:4000});
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
  await page.waitForFunction(at=>window.__chain.gateReached===at,heldAt);await enabled();
  await page.locator('.fan-toggle').click();await enabled();
  assert.equal(await page.locator('.fan-toggle').textContent(),'风扇未操控',family+': pending cleanup must release the only switch');
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length),0,'held boundary must not write');
  await page.evaluate(()=>window.__chain.releaseGate());
  await page.waitForFunction(()=>window.__chain.trace.some(t=>t.op==='restore')&&!window.__chain.lifecycle.hasControlIntent);
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length),0,'cancelled before the writer: no late enable');
  assert.equal(await page.locator('.fan-toggle').textContent(),'风扇未操控','late completion must not revive an old UI intent');
  await page.locator('.fan-toggle').click();await success();await enabled();
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='enable').length),1,'the next explicit click must write exactly once');
  assert.equal(await page.evaluate(()=>window.__chain.trace.filter(t=>t.op==='launch').length),1,'retry must reuse the single owned Host');
  await page.locator('.fan-toggle').click();await page.waitForFunction(()=>!window.__chain.lifecycle.hasControlIntent);await enabled();
  cases.push(family+' cancel during '+heldAt+' -> zero late writes -> same switch and owner re-enable');
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
 await page.locator('.fan-toggle').click();await enabled();await page.locator('.fan-toggle').click();
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
 assert.deepEqual(errors,[]);
 const report={passed:cases.length,cases,pageErrors:errors,scope:'real FanView.vue DOM handlers + real FanHostLifecycle/coordinator/timers; Host adapter, native power IPC and launcher are fixtures; no physical proof'};
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}catch(error){if(page){const failure=await page.evaluate(()=>({trace:window.__chain.trace,localState:window.__chain.lifecycle.state,coordinator:window.__chain.lifecycle.coordinatorSnapshot,recoveryActive:window.__chain.lifecycle.recoveryActive,hasIntent:window.__chain.lifecycle.hasControlIntent,native:window.__chain.native,ui:document.body.innerText}));fs.writeFileSync(path.join(out,'failure.json'),JSON.stringify(failure,null,2));console.error(JSON.stringify(failure,null,2));}throw error;}finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}