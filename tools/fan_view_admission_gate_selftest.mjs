import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import {createRequire} from 'node:module';import {createServer} from 'node:http';import {createHash} from 'node:crypto';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_fan-view-admission.cjs'))('playwright');
const evidence=path.resolve(process.env.YMCC_FAN_ADMISSION_EVIDENCE_DIR||path.join(root,'../../../_scratch/fan-view-admission-'+Date.now()));fs.mkdirSync(evidence,{recursive:true});
const curve='[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}]';
const mocks={
 '@/bridge/topmon':'export const topMonitorData={value:{tempC:55}};',
 '@/bridge/fanFeature':`const c=${curve};export const FAN_FORCE_PREVIEW=false;export function getFanFeatureSettings(){return {configured:true,preset:'balanced',nodes:c.map(n=>({...n})),motionEnabled:false,presetCurves:{soft:c,balanced:c,aggressive:c}};}export function getFanPresetCurve(){return c.map(n=>({...n}));}export async function recordFanHandshake(){}export async function saveFanCurve(){}export function setFanDiagnosticLoggingEnabled(){}export function setFanControlActive(v){window.__ui.globalActive=v;}export function setFanNavigationDuty(v){window.__ui.duty=v;}export async function setFanMotionEnabled(){}`,
 '@/bridge/fanHost':`export const fanHostLifecycle={state:'stopped',phase:'idle',controlReady:false,currentLease:null,recoveryActive:false,hasControlIntent:false,processId:null,startStage:'',coordinatorSnapshot:{powerGeneration:6},controlCancellationRevision:0,
 async start(){const f=window.__ui;f.calls.push('start');if(f.holdStart){this.state='starting';this.startStage='launcher-enter';await new Promise(r=>f.releaseStart=r);}if(f.fail==='start')throw Error('FAN_MANUAL_NATIVE_WAIT_DEADLINE');this.state='awaiting-control';this.controlReady=true;this.processId=98600;return {allowed:true,writeReady:true};},
 async apply(){const f=window.__ui;f.calls.push('apply');if(f.hold)await new Promise(r=>f.release=r);if(f.fail==='apply')throw Error('IPC command timed out: http.request');this.hasControlIntent=true;this.state='ready';f.live=true;return this.getState();},
 async applyPreset(){window.__ui.calls.push('preset');return this.getState();},
 async disable(){this.controlCancellationRevision++;const f=window.__ui;f.calls.push('disable');this.hasControlIntent=false;this.recoveryActive=false;f.live=false;if(f.holdDisable)await new Promise(r=>f.releaseDisable=r);if(f.fail==='disable')throw Error('HOST_CLOSE_TIMEOUT');this.state='awaiting-control';return this.getState();},
 async getState(){const f=window.__ui;f.calls.push('state');if(f.fail==='state')throw Error('HOST_STATE_TIMEOUT');return {state:f.live?'Ready':'AwaitingControl',hardwareWritesEnabled:f.live,unknownState:false,hcCloseCleanupPending:false,oemRestoreConfirmed:!f.live};},
 needsManualRecoveryForControl(){return false;},async needsManualRecoveryForControlAsync(){return false;},async requestRecoveryAfterObservedHostState(){return false;},stagePendingControlCurve(){this.hasControlIntent=true;},stageRecoveryCurve(){return false;}
};window.__owner=fanHostLifecycle;export function resolveFanEntryAction(){return 'idle';}`,
 '@/bridge/fanCurve':'export function normalizeFanNodes(v){return v.map(n=>({...n}));}',
 '@/bridge/fanDiagnostics':'export function fanDiagnosticLog(){};',
 '@/bridge/settingsRepository':'export async function readSettingsSection(){return {};}export async function saveSettingsSection(){};',
 '@/bridge/ipc':'export function emitNativeEvent(){};',
 '@/gamepad/focus':'export function focusGamepadElement(){}export function getGamepadPopupPlacement(){return {};}',
};
const entry=`import {createApp,h,ref,nextTick,KeepAlive} from 'vue';import Page from './src/views/FanView.vue';import {isFanMirrorUiBusy,onFanMirrorUiGate} from './src/bridge/deckyFanDisplay';
const f=window.__ui={calls:[],live:false,hold:false,fail:'',release:null,globalActive:false,cachedBusy:false,gateEvents:[],timeouts:new Map(),intervals:new Map(),intervalFns:new Map(),timerFires:[],timerClears:[]};
f.now=performance.now();f.stageEvaluations=0;Object.defineProperty(performance,'now',{value:()=>f.now,configurable:true});Date.now=()=>1792140000000+f.now;
const set=window.setTimeout.bind(window),clear=window.clearTimeout.bind(window),setI=window.setInterval.bind(window),clearI=window.clearInterval.bind(window);
window.setTimeout=(fn,ms,...args)=>{let id;const run=()=>{f.timeouts.delete(id);f.timerFires.push(ms);fn(...args);};id=set(run,ms);f.timeouts.set(id,{ms,run,at:f.now+ms});return id;};
window.clearTimeout=id=>{if(f.timeouts.has(id))f.timerClears.push(f.timeouts.get(id).ms);f.timeouts.delete(id);clear(id);};
window.setInterval=(fn,ms,...args)=>{const id=setI(fn,ms,...args);f.intervals.set(id,ms);f.intervalFns.set(id,fn);return id;};window.clearInterval=id=>{f.intervals.delete(id);f.intervalFns.delete(id);clearI(id);};
f.fireIntervals=ms=>{for(const [id,value] of [...f.intervals])if(value===ms)f.intervalFns.get(id)?.();};
f.fireTimers=ms=>{for(const [id,t] of [...f.timeouts])if(t.ms===ms){clear(id);t.run();}};
f.advance=async ms=>{const target=f.now+ms;for(let i=0;i<1000;i++){const due=[...f.timeouts].filter(([,t])=>t.at<=target).sort((a,b)=>a[1].at-b[1].at)[0];if(!due)break;f.now=Math.max(f.now,due[1].at);clear(due[0]);due[1].run();await f.idle();}f.now=target;await f.idle();};
f.idle=async()=>{for(let i=0;i<6;i++){await Promise.resolve();await nextTick();}};
f.observe=()=>({directBusy:isFanMirrorUiBusy(),cachedBusy:f.cachedBusy,gateEvents:[...f.gateEvents],calls:[...f.calls],timeout2000:[...f.timeouts.values()].filter(v=>v.ms===2000).length,interval250:[...f.intervals.values()].filter(v=>v===250).length,buttonDisabled:document.querySelector('.fan-toggle')?.disabled,buttonText:document.querySelector('.fan-toggle')?.textContent,globalActive:f.globalActive,notice:document.querySelector('.fan-status-banner')?.textContent});
onFanMirrorUiGate(()=>{f.cachedBusy=isFanMirrorUiBusy();f.gateEvents.push(f.cachedBusy);});const shown=ref(true);const cached=new URLSearchParams(location.search).get('cache')==='1';const app=createApp({setup:()=>()=>cached?h(KeepAlive,null,{default:()=>shown.value?h(Page):null}):(shown.value?h(Page):null)});f.deactivate=async()=>{shown.value=false;await f.idle();};f.activate=async()=>{shown.value=true;await f.idle();};app.mount('#app');f.unmount=()=>app.unmount();f.remount=async()=>{shown.value=false;await f.idle();shown.value=true;await f.idle();};`;
const styles=[],sourceOverride=process.env.YMCC_FAN_VIEW_SOURCE_OVERRIDE;
const output=await build({stdin:{resolveDir:root,contents:entry,loader:'ts'},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'actual-view-and-mirror-gate-with-boundaries',setup(b){
 b.onResolve({filter:/.*/},a=>a.path in mocks?{path:a.path,namespace:'fan-ui-fixture'}:undefined);b.onLoad({filter:/.*/,namespace:'fan-ui-fixture'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:root}));
 b.onLoad({filter:/\.vue$/},a=>{let raw=sourceOverride&&a.path.endsWith(path.join('views','FanView.vue'))?fs.readFileSync(sourceOverride,'utf8'):fs.readFileSync(a.path,'utf8');if(a.path.endsWith(path.join('views','FanView.vue')))raw=raw.replace('const enableStageText = computed(() => {','const enableStageText = computed(() => { window.__ui.stageEvaluations++;');const {descriptor,errors}=parse(raw,{filename:a.path});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8);for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}const code=compileScript(descriptor,{id,inlineTemplate:true});return {contents:code.content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(a.path)};});
}}]});
const js=output.outputFiles[0].text;fs.writeFileSync(path.join(evidence,'fixture.js'),js);
const html='<!doctype html><meta charset="utf-8"><title>Fan page and mirror admission gate</title><style>'+fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n')+'\nbody{background:var(--bg-solid);margin:0;padding:16px;}#app{max-width:760px;margin:0 auto;}</style><div id="app"></div><script src="/fixture.js"></script>';
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/fixture.js'?js:html);});await new Promise(r=>server.listen(0,'127.0.0.1',r));
const errors=[],rows=[];let browser,page;async function fresh(mode={}){await page.goto('http://127.0.0.1:'+server.address().port+(mode.keepAlive?'?cache=1':''));await page.waitForSelector('.fan-toggle');await page.evaluate(async values=>{Object.assign(window.__ui,values);await window.__ui.idle();},mode);}
async function snapshot(){return page.evaluate(()=>window.__ui.observe());}
async function click(){await page.locator('.fan-toggle').click();await page.evaluate(()=>window.__ui.idle());}
async function expiry(){await page.evaluate(async()=>{await window.__ui.advance(4000);});}
async function check(name,work){try{const detail=await work();rows.push({name,ok:true,detail});console.log('PASS '+name);}catch(e){rows.push({name,ok:false,error:String(e),state:await snapshot().catch(()=>null)});console.log('FAIL '+name+': '+String(e));}}
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
 await check('slow successful submission releases page AND cached mirror gate when background task settles',async()=>{await fresh({hold:true});await click();await expiry();let s=await snapshot();assert.equal(s.buttonDisabled,false);assert.equal(s.buttonText,'正在开启…');assert.equal(s.directBusy,true,'live background task retains mirror conflict gate');await page.evaluate(async()=>{window.__ui.release();await window.__ui.idle();});s=await snapshot();assert.equal(s.directBusy,false);assert.equal(s.cachedBusy,false,'plain promise retirement must notify cached consumer');assert.equal(s.buttonText,'风扇已开启');return s;});
 await check('slow failed submission clears mirror busy and next manual click reaches original owner',async()=>{await fresh({hold:true,fail:'apply'});await click();await expiry();await page.evaluate(async()=>{window.__ui.release();await window.__ui.idle();});let s=await snapshot();assert.equal(s.directBusy,false);assert.equal(s.cachedBusy,false);assert.equal(s.buttonDisabled,false);await page.evaluate(()=>{window.__ui.hold=false;window.__ui.fail='';});await click();s=await snapshot();assert.equal(s.calls.filter(v=>v==='apply').length,2);assert.equal(s.buttonText,'风扇已开启');return s;});
 await check('fast completed submissions leave no unused two-second admission timeout or busy ticker',async()=>{await fresh();await click();let s=await snapshot();assert.equal(s.timeout2000,0);assert.equal(s.interval250,0);await expiry();await click();await expiry();await click();s=await snapshot();assert.equal(s.timeout2000,0);assert.equal(s.calls.filter(v=>v==='apply').length,2);return s;});
 await check('external explicit OFF still retires a pending submission and cannot revive it',async()=>{await fresh({hold:true});await click();await expiry();await page.evaluate(async()=>{await window.__owner.disable();window.dispatchEvent(new CustomEvent('fan:guard-state',{detail:{active:false,state:'awaiting-control'}}));window.__ui.fireIntervals(250);await window.__ui.idle();});let s=await snapshot();assert.equal(s.calls.filter(v=>v==='disable').length,1);assert.equal(s.directBusy,false,'retired epoch is not a live UI mutation');assert.equal(s.cachedBusy,false);assert.equal(s.buttonDisabled,false);await page.evaluate(async()=>{window.__ui.release();await window.__ui.idle();});s=await snapshot();assert.equal(s.buttonText,'风扇未操控');assert.equal(s.globalActive,false);return s;});
 for(const phase of ['start','state'])await check(phase+' failure returns to a reachable switch and no orphan admission timer',async()=>{await fresh({fail:phase});await click();await expiry();const s=await snapshot();assert.equal(s.buttonDisabled,false);assert.equal(s.directBusy,false);assert.equal(s.cachedBusy,false);assert.equal(s.timeout2000,0);assert.equal(s.globalActive,false);return s;});
 await check('direct curve reset settles both plain owners and releases cached mirror without switch-lock timer',async()=>{await fresh();await click();await expiry();let s=await snapshot();assert.equal(s.buttonText,'风扇已开启');await page.evaluate(()=>{window.__ui.hold=true;});await page.getByRole('button',{name:'重置转速',exact:true}).click();await page.evaluate(()=>window.__ui.idle());s=await snapshot();assert.equal(s.directBusy,true);assert.equal(s.cachedBusy,true);await page.evaluate(async()=>{window.__ui.release();await window.__ui.idle();});s=await snapshot();assert.equal(s.calls.filter(v=>v==='apply').length,2);assert.equal(s.directBusy,false);assert.equal(s.cachedBusy,false,'curve promise retirement must notify after both owners settle');assert.equal(s.timeout2000,0);assert.equal(s.interval250,0);return s;});
 await check('mount and unmount remain read-only and deregister mirror blocker',async()=>{await fresh();let s=await snapshot();assert.equal(s.calls.length,0);await page.evaluate(async()=>{window.__ui.unmount();await window.__ui.idle();});s=await snapshot();assert.equal(s.directBusy,false);assert.equal(s.cachedBusy,false);assert.equal(s.interval250,0);return s;});
 await check('superseded telemetry retires without clearing current intent or holding the manual switch',async()=>{
   await fresh();await click();await expiry();
   await page.evaluate(async()=>{window.__owner.getState=async options=>{window.__ui.staleReadOption=options?.discardSuperseded===true;return null;};window.dispatchEvent(new CustomEvent('fan:guard-state',{detail:{active:true,state:'ready'}}));await window.__ui.idle();});
   const s=await snapshot();assert.equal(await page.evaluate(()=>window.__ui.staleReadOption),true);assert.equal(s.buttonText,'正在恢复…');assert.match(s.notice,/正在.*(恢复|接管)/);assert.equal(s.buttonDisabled,false);assert.equal(s.directBusy,false);assert.equal(s.globalActive,false,'retiring a read does not manufacture a hardware receipt');assert.equal(s.calls.filter(v=>v==='apply').length,1);assert.equal(s.calls.filter(v=>v==='disable').length,0);return s;
 });

 await check('first start shows reactive preparation and handshake stages beyond the admission budget',async()=>{
  await fresh({holdStart:true});await click();await expiry();
  let s=await snapshot();assert.equal(s.buttonText,'正在开启…');assert.equal(s.buttonDisabled,false);assert.equal(s.globalActive,false);assert.equal(s.interval250,1);
  await page.evaluate(async()=>{window.__ui.fireIntervals(250);await window.__ui.idle();});assert.match((await snapshot()).notice,/启动风扇服务/);
  await page.evaluate(async()=>{window.__owner.startStage='runtime-manifest-resolved';window.__ui.fireIntervals(250);await window.__ui.idle();});s=await snapshot();assert.match(s.notice,/准备运行环境/);await page.screenshot({path:path.join(evidence,'first-start-preparation.png'),fullPage:true});
  await page.evaluate(async()=>{window.__owner.state='handshaking';window.__ui.fireIntervals(250);await window.__ui.idle();});s=await snapshot();assert.match(s.notice,/握手.*机型/);await page.screenshot({path:path.join(evidence,'first-start-handshake.png'),fullPage:true});
  await page.evaluate(async()=>{window.__ui.hold=true;window.__ui.holdStart=false;window.__owner.startStage='';window.__ui.releaseStart();await window.__ui.idle();window.__ui.fireIntervals(250);await window.__ui.idle();});s=await snapshot();assert.match(s.notice,/握手成功.*接管/);await page.screenshot({path:path.join(evidence,'first-start-takeover.png'),fullPage:true});
  await page.evaluate(async()=>{window.__ui.release();await window.__ui.idle();});s=await snapshot();assert.equal(s.buttonText,'风扇已开启');assert.equal(s.interval250,0);return s;
 });
 await check('repeat clicks after both old timers expire cannot cancel the initial submission',async()=>{
  await fresh({hold:true});await click();await expiry();
  await page.evaluate(async()=>{for(let i=0;i<8;i++)document.querySelector('.fan-toggle').dispatchEvent(new MouseEvent('click',{bubbles:true}));await window.__ui.idle();});
  let s=await snapshot();assert.equal(s.calls.filter(x=>x==='apply').length,1);assert.equal(s.calls.filter(x=>x==='disable').length,0);assert.equal(s.buttonText,'正在开启…');assert.equal(s.buttonDisabled,true,'the first new click starts its own finite repeat guard');await expiry();assert.equal((await snapshot()).buttonDisabled,false);
  await page.evaluate(async()=>{window.__ui.release();await window.__ui.idle();});s=await snapshot();assert.equal(s.buttonText,'风扇已开启');return s;
 });
 await check('early failure retains exactly four seconds before the next manual ON',async()=>{
  await fresh({fail:'start'});await click();let s=await snapshot();assert.equal(s.buttonDisabled,true);await expiry();assert.equal((await snapshot()).buttonDisabled,false);assert.equal(s.buttonText,'风扇未操控');assert.equal(s.interval250,0);
  await page.evaluate(()=>{window.__ui.fail='';});await click();s=await snapshot();assert.equal(s.buttonText,'风扇已开启');return s;
 });
 for(const direction of ['on-fast','on-failed','on-never-returns','off-fast','off-failed','off-never-returns'])await check(direction+' has an exact four-second lock, including route remount',async()=>{
  await fresh();if(direction.startsWith('off')){await click();await expiry();}
  await page.evaluate(dir=>{const f=window.__ui;f.hold=dir==='on-never-returns';f.holdDisable=dir==='off-never-returns';f.fail=dir.endsWith('failed')?(dir.startsWith('off')?'disable':'start'):'';},direction);
  await click();let s=await snapshot();assert.equal(s.buttonDisabled,true,'all outcomes keep the initial repeat guard');
  await page.evaluate(async()=>{for(let i=0;i<6;i++)document.querySelector('.fan-toggle').dispatchEvent(new MouseEvent('click',{bubbles:true}));await window.__ui.advance(1999);});
  assert.equal((await snapshot()).buttonDisabled,true);await page.evaluate(()=>window.__ui.remount());assert.equal((await snapshot()).buttonDisabled,true,'remount cannot bypass the remaining action guard');
  await page.evaluate(()=>window.__ui.advance(2000));assert.equal((await snapshot()).buttonDisabled,true,'still guarded at 3999ms');
  await page.evaluate(()=>window.__ui.advance(1));s=await snapshot();assert.equal(s.buttonDisabled,false,'must become clickable at 4000ms even if the old task never returns');
  assert.equal(s.calls.filter(v=>v==='disable').length,direction.startsWith('off')?1:0,'repeat clicks do not reverse ON or repeat OFF');
  if(direction==='on-never-returns'){await click();s=await snapshot();assert.equal(s.calls.filter(v=>v==='apply').length,1,'post-deadline ON keeps the original owner');assert.equal(s.calls.filter(v=>v==='disable').length,0);await page.evaluate(async()=>{window.__ui.release();await window.__ui.idle();});}
  return s;
 });
 await check('unchanged startup stage does not reevaluate its visible message at four hertz',async()=>{
  await fresh({holdStart:true});await click();await expiry();await page.evaluate(async()=>{window.__ui.fireIntervals(250);await window.__ui.idle();});
  const before=await page.evaluate(()=>window.__ui.stageEvaluations);
  await page.evaluate(async()=>{for(let i=0;i<12;i++){await window.__ui.advance(250);window.__ui.fireIntervals(250);await window.__ui.idle();}});
  const after=await page.evaluate(()=>window.__ui.stageEvaluations);assert(after-before<=1,'unchanged stage should only cross the six-second fallback threshold once');
  await page.evaluate(async()=>{window.__owner.startStage='runtime-manifest-resolved';window.__ui.fireIntervals(250);await window.__ui.idle();});assert.match((await snapshot()).notice,/准备运行环境/);
  return {evaluations:after-before,...await snapshot()};
 });
 for(const direction of ['on','off'])for(const boundary of ['visibility','keep-alive'])await check(direction+' foreground '+boundary+' resynchronizes an expired guard when timer callbacks were deferred',async()=>{
  await fresh({keepAlive:boundary==='keep-alive'});if(direction==='off'){await click();await expiry();}await click();assert.equal((await snapshot()).buttonDisabled,true);
  if(boundary==='visibility'){
   await page.evaluate(async()=>{Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});document.dispatchEvent(new Event('visibilitychange'));window.__ui.now+=3999;Object.defineProperty(document,'hidden',{configurable:true,get:()=>false});document.dispatchEvent(new Event('visibilitychange'));await window.__ui.idle();});
  }else await page.evaluate(async()=>{await window.__ui.deactivate();window.__ui.now+=3999;await window.__ui.activate();});
  assert.equal((await snapshot()).buttonDisabled,true,'foreground before deadline must preserve the last millisecond');
  if(boundary==='visibility'){
   await page.evaluate(async()=>{Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});document.dispatchEvent(new Event('visibilitychange'));window.__ui.now+=2;Object.defineProperty(document,'hidden',{configurable:true,get:()=>false});document.dispatchEvent(new Event('visibilitychange'));await window.__ui.idle();});
  }else await page.evaluate(async()=>{await window.__ui.deactivate();window.__ui.now+=2;await window.__ui.activate();});
  const s=await snapshot();assert.equal(s.buttonDisabled,false,'expired protection cannot wait for the deferred old timer');assert.equal(s.calls.filter(v=>v==='disable').length,direction==='off'?1:0,'observation does not change control intent');return s;
 });
 await check('hidden pending page stops the progress ticker and resumes only when visible',async()=>{
  await fresh({hold:true});await click();await expiry();
  await page.evaluate(async()=>{Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});document.dispatchEvent(new Event('visibilitychange'));await window.__ui.idle();});let s=await snapshot();assert.equal(s.interval250,0);
  await page.evaluate(async()=>{Object.defineProperty(document,'hidden',{configurable:true,get:()=>false});document.dispatchEvent(new Event('visibilitychange'));await window.__ui.idle();});s=await snapshot();assert.equal(s.interval250,1);
  await page.evaluate(async()=>{window.__ui.release();await window.__ui.idle();});s=await snapshot();assert.equal(s.interval250,0);return s;
 });
}catch(e){errors.push(String(e));}finally{await browser?.close();await new Promise(r=>server.close(r));}
const result={ok:rows.every(r=>r.ok)&&rows.length===24&&!errors.length,checks:rows.length,rows,errors,sourceSha256:createHash('sha256').update(fs.readFileSync(sourceOverride||path.join(root,'src/views/FanView.vue'))).digest('hex'),displayModuleSha256:createHash('sha256').update(fs.readFileSync(path.join(root,'src/bridge/deckyFanDisplay.ts'))).digest('hex'),bundleSha256:createHash('sha256').update(js).digest('hex'),hardwareCalls:0,scope:'actual-rendered-FanView+actual-deckyFanDisplay; controlled admission/lock timers; fake platform/settings/Host; no native/HC/real Host/CPU comparison'};fs.writeFileSync(path.join(evidence,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({ok:result.ok,checks:result.checks,failed:rows.filter(r=>!r.ok).map(r=>r.name),errors,hardwareCalls:0,evidenceRoot:evidence}));if(!result.ok)process.exitCode=1;
