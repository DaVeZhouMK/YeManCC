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
const out=path.resolve(process.env.YMCC_FAN_PRESET_EVIDENCE_DIR||path.join(root,'../../Build/Validation/fan-view-preset-intent-20261016'));fs.mkdirSync(out,{recursive:true});
const fixture=path.join(root,'tools/fan_view_bridge_wake_fixture.ts');
const mocks={
  '@/bridge/fanHost':`export {fanHostLifecycle,resolveFanEntryAction} from ${JSON.stringify(fixture)};`,
  '@/bridge/topmon':'export const topMonitorData={value:{tempC:55}};',
  '@/bridge/fanFeature':`import ${JSON.stringify(fixture)};export const FAN_FORCE_PREVIEW=false;export function getFanFeatureSettings(){return {configured:true,preset:'balanced',presetCurves:window.__chain.curves,nodes:structuredClone(window.__chain.curves.balanced),motionEnabled:false};}export function getFanPresetCurve(n){return structuredClone(window.__chain.curves[n]);}export async function recordFanHandshake(){}export async function saveFanCurve(nodes,name){window.__chain.note('save-'+name,nodes);if(window.__chain.saveHold===name){window.__chain.saveReached=name;await new Promise(resolve=>window.__chain.releaseSave=resolve);if(window.__chain.saveThrow)throw new Error('fixture-save-failure');}}export function setFanDiagnosticLoggingEnabled(){}export function setFanControlActive(v){window.__chain.lastControlActive=v;}export function setFanNavigationDuty(v){window.__chain.lastNavDuty=v;}export async function setFanMotionEnabled(){}`,
  '@/bridge/settingsRepository':'export async function readSettingsSection(){return {};}export async function saveSettingsSection(){}',
};
const styles=[];
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:"import {createApp,h,ref,nextTick} from 'vue';import Page from './src/views/FanView.vue';import {observeFanMirrorDisplay} from './src/bridge/deckyFanDisplay';window.__fanMirrorPublish=observeFanMirrorDisplay;const visible=ref(true);window.__fanRemount=async()=>{visible.value=false;await nextTick();visible.value=true;await nextTick();};createApp({setup:()=>()=>visible.value?h(Page):null}).mount('#app');"},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'real-fan-chain-with-platform-boundaries',setup(b){
 b.onLoad({filter:/fanDiagnostics\.ts$/},a=>({loader:'ts',resolveDir:path.dirname(a.path),contents:fs.readFileSync(a.path,'utf8').replace('export function fanDiagnosticLog(event: string, details?: unknown): void {','export function fanDiagnosticLog(event: string, details?: unknown): void { if(window.__chain)(window.__chain.diagnostics??=[]).push({event,details});')}));
 if(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE)b.onLoad({filter:/src[\\/]bridge[\\/]fanHost\.ts$/},a=>({contents:fs.readFileSync(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE,'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));

  b.onResolve({filter:/.*/},a=>a.path in mocks?{path:a.path,namespace:'ui-boundary'}:undefined);
  b.onLoad({filter:/.*/,namespace:'ui-boundary'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:root}));
  b.onLoad({filter:/\.vue$/},a=>{const {descriptor,errors}=parse(fs.readFileSync(process.env.YMCC_FAN_PRESET_UI_BEFORE&&a.path.endsWith('FanView.vue')?process.env.YMCC_FAN_PRESET_UI_BEFORE:a.path,'utf8'),{filename:a.path});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8),code=compileScript(descriptor,{id,inlineTemplate:true});for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}return {contents:code.content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(a.path)};});
}}]});
const js=result.outputFiles[0].text,css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html=`<!doctype html><meta charset="utf-8"><title>Real Fan UI and lifecycle wake chain</title><style>${css}</style><div id="app"></div><script src="/chain.js"></script>`;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/chain.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/chain.js'?js:html);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));

const cases=[],errors=[];let browser,page;
async function snap(){return page.evaluate(()=>({button:document.querySelector('.fan-toggle')?.textContent,disabled:document.querySelector('.fan-toggle')?.disabled,status:document.querySelector('.fan-status-banner')?.textContent,trace:window.__chain.trace,diagnostics:window.__chain.diagnostics,state:window.__chain.lifecycle.state,recoveryActive:window.__chain.lifecycle.recoveryActive,hasIntent:window.__chain.lifecycle.hasControlIntent,active:window.__chain.lastControlActive}));}
async function check(name,run){if(process.env.YMCC_FAN_PRESET_CASE&&!name.includes(process.env.YMCC_FAN_PRESET_CASE))return;try{await run();cases.push({name,ok:true,snapshot:await snap()});console.log('PASS '+name);}catch(error){cases.push({name,ok:false,error:String(error),snapshot:await snap().catch(()=>null)});console.log('FAIL '+name+': '+String(error));}}
async function fresh(family,cold=true){await page.goto('http://127.0.0.1:'+server.address().port+'/?family='+family+'&cold='+(cold?'1':'0'));await page.mouse.up();await page.waitForSelector('.fan-toggle');await page.evaluate(()=>{window.__chain.setNative(1,'ready');window.__chain.lifecycle.setPowerGeneration(1);window.__chain.lifecycle.observePowerBoundary('resume-ready',1);
// The shared historical fixture leaves state=Stopped after Open and keeps the
// same backend object after launcher.stop. Model the real Host boundaries here.
const b=window.__chain.boundary,open=b.open.bind(b);b.open=async()=>{await open();b.state='Open';return b.snapshot();};
const launcher=window.__chain.lifecycle.launcher,stop=launcher.stop.bind(launcher);launcher.stop=async(...args)=>{await stop(...args);b.closed('Stopped');};
window.__chain.holdAt='Open';});await page.locator('.fan-toggle').click();await page.waitForFunction(()=>window.__chain.gateReached==='Open');}

async function preset(name) {
  const trigger=page.getByRole('button',{name:'转速预设',exact:true});
  await trigger.scrollIntoViewIfNeeded();
  await page.waitForTimeout(180); // Let a previous node popup close and scrolling settle before opening the next menu.
  await trigger.click();
  await page.getByRole('option',{name,exact:true}).click();
}
async function finishFirstOn() {
  await page.evaluate(()=>window.__chain.releaseGate());
  await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
  // Route remount removes the first input lock, without faking the control path.
  await page.evaluate(()=>window.__fanRemount());
  await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
}
async function waitIdle() {
  await page.waitForFunction(()=>!document.querySelector('.control-dropdown button')?.disabled);
  await page.waitForTimeout(100);
}
try {
  browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  page=await browser.newPage();page.setDefaultTimeout(5000);page.on('pageerror',e=>errors.push(e.message));
  for(const family of ['ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' preset during pending Open submits only the newest curve',async()=>{
    await fresh(family);await page.evaluate(()=>window.__fanRemount());
    await preset('暴力转速');
    await page.waitForFunction(()=>window.__chain.trace.some(x=>x.op==='save-aggressive'));
    await page.evaluate(()=>window.__chain.releaseGate());
    await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
    await waitIdle();
    const s=await snap(),writes=s.trace.filter(x=>x.op==='enable');
    assert.equal(writes.length,1,'pending preset must replace rather than write old balanced then new aggressive');
    assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.curves.aggressive));
  });
  await check('OFF while preset start waits prevents a delayed preset from re-enabling',async()=>{
    await fresh('ClawCG3EM');await finishFirstOn();
    const before=(await snap()).trace.filter(x=>x.op==='enable').length;
    await page.evaluate(()=>{window.__chain.gateReached='';window.__chain.holdAt='state';});
    await preset('暴力转速');
    await page.waitForFunction(()=>window.__chain.gateReached==='state');
    await page.locator('.fan-toggle').click();
    await page.waitForFunction(()=>window.__chain.diagnostics.some(x=>x.event==='ui.control-disable-begin'));
    await page.evaluate(()=>window.__chain.releaseGate());
    await waitIdle();
    const s=await snap();
    assert.equal(s.trace.filter(x=>x.op==='enable').length,before,'OFF must retire the preset continuation before a new bridge apply');
    assert.equal(s.hasIntent,false,'OFF remains terminal');
    assert.equal(s.button,'风扇未操控');
    assert.equal(s.active,false);
  });
  await check('older delayed preset save cannot overwrite or resubmit the newer preset',async()=>{
    await fresh('ROGAlly');await finishFirstOn();
    const before=(await snap()).trace.filter(x=>x.op==='enable').length;
    await page.evaluate(()=>window.__chain.saveHold='soft');
    await preset('轻柔转速');await page.waitForFunction(()=>window.__chain.saveReached==='soft');
    await preset('暴力转速');
    await page.waitForFunction(n=>window.__chain.trace.filter(x=>x.op==='enable').length===n,before+1);
    await waitIdle();await page.evaluate(()=>window.__chain.releaseSave());await waitIdle();
    const s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,before+1,'stale save completion must not submit another curve');
    assert.deepEqual(s.trace.filter(x=>x.op==='enable').at(-1).nodes,await page.evaluate(()=>window.__chain.curves.aggressive));
  });
  await check('reset while the original Open is pending still writes only the final selected curve',async()=>{
    await fresh('ROGAlly');await page.evaluate(()=>window.__fanRemount());
    await page.getByRole('button',{name:'节点 1 转速',exact:true}).click();
    await page.getByRole('option',{name:'80 %',exact:true}).click();
    await page.getByRole('button',{name:'重置转速',exact:true}).click();
    await page.waitForFunction(()=>window.__chain.trace.filter(x=>x.op==='save-balanced').length===2);
    await page.evaluate(()=>window.__chain.releaseGate());
    await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');await waitIdle();
    const writes=(await snap()).trace.filter(x=>x.op==='enable');assert.equal(writes.length,1);
    assert.deepEqual(writes[0].nodes,await page.evaluate(()=>window.__chain.curves.balanced));
  });
  await check('delayed reset save cannot attach to a later OFF then ON',async()=>{
    await fresh('GPDWin5');await finishFirstOn();
    await page.evaluate(()=>window.__chain.saveHold='balanced');
    await page.getByRole('button',{name:'重置转速',exact:true}).click();
    await page.waitForFunction(()=>window.__chain.saveReached==='balanced');
    await page.locator('.fan-toggle').click();
    await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇未操控'&&!document.querySelector('.fan-toggle').disabled);
    await page.locator('.fan-toggle').click();
    await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
    const before=(await snap()).trace.filter(x=>x.op==='enable').length;
    await page.evaluate(()=>window.__chain.releaseSave());await waitIdle();
    assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,before,'reset from the previous control epoch cannot write again');
  });
  await check('current preset save failure leaves hardware unchanged and next selection works',async()=>{
    await fresh('GPDWin5');await finishFirstOn();
    const before=(await snap()).trace.filter(x=>x.op==='enable').length;
    await page.evaluate(()=>window.__chain.saveHold='soft');await preset('轻柔转速');
    await page.waitForFunction(()=>window.__chain.saveReached==='soft');
    await page.evaluate(()=>{window.__chain.saveThrow=true;window.__chain.releaseSave();});await waitIdle();
    assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,before,'failed current save must not write hardware');
    await preset('暴力转速');
    await page.waitForFunction(n=>window.__chain.trace.filter(x=>x.op==='enable').length===n,before+1);
    assert.equal((await snap()).button,'风扇已开启');
  });
  for (const editor of ['dropdown','pointer','gamepad']) await check('node editing '+editor+' waits for persistence and a rejected save cannot write or claim success',async()=>{
    await fresh('GPDWin5');await finishFirstOn();
    const before=(await snap()).trace.filter(x=>x.op==='enable').length;
    await page.evaluate(()=>{window.__chain.saveHold='balanced';window.__chain.saveReached='';window.__chain.saveThrow=false;});
    if(editor==='dropdown'){
      await page.getByRole('button',{name:'节点 1 转速',exact:true}).click();
      await page.getByRole('option',{name:'80 %',exact:true}).click();
    } else if(editor==='pointer') {
      await page.locator('.chart-node-hit').nth(0).scrollIntoViewIfNeeded();
      const point=await page.locator('.chart-node-hit').nth(0).boundingBox();assert(point);
      await page.mouse.move(point.x+point.width/2,point.y+point.height/2);await page.mouse.down();
      await page.mouse.move(point.x+point.width/2,point.y+point.height/2-25);await page.mouse.up();
    } else {
      await page.locator('.chart-node-hit').nth(0).evaluate(button=>{
        button.click();
        button.dispatchEvent(new CustomEvent('gp:spatial-edit',{detail:{dx:0,dy:-10}}));
        button.click();
      });
    }
    await page.waitForFunction(()=>window.__chain.saveReached==='balanced');
    await page.waitForTimeout(100);
    const held=await snap();
    await page.evaluate(()=>{window.__chain.saveThrow=true;window.__chain.releaseSave();});await waitIdle();
    const rejected=await snap();
    assert.equal(held.trace.filter(x=>x.op==='enable').length,before,'held node save must not start a hardware curve write');
    assert.equal(rejected.trace.filter(x=>x.op==='enable').length,before,'rejected node save must not write hardware');
    assert.match(rejected.status,/保存失败/,'current save rejection must be visible');
    if(editor==='gamepad')assert(!/节点 1 已应用|节点 1 已保存/.test(held.status),'gamepad A must not claim completion while save is held');
    await page.evaluate(()=>{window.__chain.saveHold='';window.__chain.saveThrow=false;});
    await preset('暴力转速');
    await page.waitForFunction(n=>window.__chain.trace.filter(x=>x.op==='enable').length===n,before+1);
    assert.equal((await snap()).button,'风扇已开启','save failure cannot poison the next explicit curve');
  });

  for (const rejects of [false,true]) await check('node editing delayed '+(rejects?'rejection':'success')+' cannot overwrite a newer explicit preset',async()=>{
    await fresh('ROGAlly');await finishFirstOn();
    const before=(await snap()).trace.filter(x=>x.op==='enable').length;
    await page.evaluate(()=>{window.__chain.saveHold='balanced';window.__chain.saveReached='';window.__chain.saveThrow=false;});
    await page.getByRole('button',{name:'节点 1 转速',exact:true}).click();
    await page.getByRole('option',{name:'80 %',exact:true}).click();
    await page.waitForFunction(()=>window.__chain.saveReached==='balanced');
    await preset('暴力转速');
    await page.waitForFunction(n=>{const writes=window.__chain.trace.filter(x=>x.op==='enable');return writes.length>=n&&JSON.stringify(writes.at(-1).nodes)===JSON.stringify(window.__chain.curves.aggressive);},before+1);
    await page.evaluate(rejects=>{window.__chain.saveThrow=rejects;window.__chain.releaseSave();},rejects);await waitIdle();
    const s=await snap();assert.equal(s.trace.filter(x=>x.op==='enable').length,before+1);
    assert.deepEqual(s.trace.filter(x=>x.op==='enable').at(-1).nodes,await page.evaluate(()=>window.__chain.curves.aggressive));
    assert(!/保存失败/.test(s.status),'retired save failure cannot replace current successful state');
  });
  await check('node editing pending save does not block explicit OFF then ON, and its late completion cannot reapply',async()=>{
    await fresh('GPDWin5');await finishFirstOn();
    const before=(await snap()).trace.filter(x=>x.op==='enable').length;
    await page.evaluate(()=>{window.__chain.saveHold='balanced';window.__chain.saveReached='';window.__chain.saveThrow=false;});
    await page.getByRole('button',{name:'节点 1 转速',exact:true}).click();
    await page.getByRole('option',{name:'80 %',exact:true}).click();
    await page.waitForFunction(()=>window.__chain.saveReached==='balanced');
    await page.locator('.fan-toggle').click();
    await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇未操控'&&!document.querySelector('.fan-toggle').disabled);
    await page.locator('.fan-toggle').click();
    await page.waitForFunction(()=>document.querySelector('.fan-toggle').textContent==='风扇已开启');
    assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,before+1,'explicit ON must reach the owner without waiting on the pending settings save');
    await page.evaluate(()=>window.__chain.releaseSave());await waitIdle();
    assert.equal((await snap()).trace.filter(x=>x.op==='enable').length,before+1,'late node save must not submit a second curve after OFF/ON');
  });
  await check('node editing successful current save starts exactly one write only after its receipt',async()=>{
    await fresh('GPDWin5');await finishFirstOn();
    const before=(await snap()).trace.filter(x=>x.op==='enable').length;
    await page.evaluate(()=>{window.__chain.saveHold='balanced';window.__chain.saveReached='';window.__chain.saveThrow=false;});
    await page.getByRole('button',{name:'节点 1 转速',exact:true}).click();
    await page.getByRole('option',{name:'80 %',exact:true}).click();
    await page.waitForFunction(()=>window.__chain.saveReached==='balanced');
    await page.waitForTimeout(100);
    const held=await snap();
    assert.equal(held.trace.filter(x=>x.op==='enable').length,before,'successful current edit still waits for persistence');
    const saved=held.trace.filter(x=>x.op==='save-balanced').at(-1).nodes;
    await page.evaluate(()=>window.__chain.releaseSave());
    await page.waitForFunction(n=>window.__chain.trace.filter(x=>x.op==='enable').length===n,before+1);
    await waitIdle();
    const s=await snap();assert.deepEqual(s.trace.filter(x=>x.op==='enable').at(-1).nodes,saved);
    assert.equal(s.button,'风扇已开启');assert(!/保存失败/.test(s.status));
  });
  for(const editor of ['pointer','gamepad']) await check('uncommitted '+editor+' edit retires an older saved curve continuation without writing the moving curve',async()=>{
    await fresh('GPDWin5');await finishFirstOn();
    const before=(await snap()).trace.filter(x=>x.op==='enable').length;
    await page.evaluate(()=>{window.__chain.saveHold='balanced';window.__chain.saveReached='';window.__chain.saveThrow=false;});
    await page.getByRole('button',{name:'节点 1 转速',exact:true}).click();
    await page.getByRole('option',{name:'80 %',exact:true}).click();
    await page.waitForFunction(()=>window.__chain.saveReached==='balanced');
    const firstSaved=(await snap()).trace.filter(x=>x.op==='save-balanced').at(-1).nodes;
    await page.locator('.chart-node-hit').nth(0).scrollIntoViewIfNeeded();
    if(editor==='pointer'){
      const point=await page.locator('.chart-node-hit').nth(0).boundingBox();assert(point);
      await page.mouse.move(point.x+point.width/2,point.y+point.height/2);await page.mouse.down();
      await page.mouse.move(point.x+point.width/2,point.y+point.height/2-25);
    } else {
      await page.locator('.chart-node-hit').nth(0).evaluate(button=>{
        button.click();button.dispatchEvent(new CustomEvent('gp:spatial-edit',{detail:{dx:0,dy:-10}}));
      });
    }
    // The older persistence receipt arrives while the new edit has not been confirmed.
    await page.evaluate(()=>window.__chain.releaseSave());await waitIdle();
    const held=await snap();
    assert.equal(held.trace.filter(x=>x.op==='save-balanced').length,1,'moving/armed node has not been saved yet');
    assert.equal(held.trace.filter(x=>x.op==='enable').length,before,'old save must not write an uncommitted moving curve');
    await page.evaluate(()=>{window.__chain.saveHold='';window.__chain.saveReached='';});
    if(editor==='pointer')await page.mouse.up();
    else await page.locator('.chart-node-hit').nth(0).evaluate(button=>button.click());
    await page.waitForFunction(n=>window.__chain.trace.filter(x=>x.op==='enable').length>=n,before+1);
    await waitIdle();
    const done=await snap(),saves=done.trace.filter(x=>x.op==='save-balanced'),writes=done.trace.filter(x=>x.op==='enable');
    assert.equal(saves.length,2,'only confirmation saves the new curve');
    assert.notDeepEqual(saves.at(-1).nodes,firstSaved,'the newer confirmed curve differs from the older saved one');
    assert.equal(writes.length,before+1,'confirmation writes exactly once');
    assert.deepEqual(writes.at(-1).nodes,saves.at(-1).nodes,'only the newly saved curve is submitted');
    assert.equal(done.button,'风扇已开启');
  });
  for(const editor of ['pointer','gamepad']) await check('uncommitted '+editor+' no-op at the limit does not discard the current saved curve',async()=>{
    await fresh('GPDWin5');await finishFirstOn();
    const before=(await snap()).trace.filter(x=>x.op==='enable').length;
    await page.evaluate(()=>{window.__chain.saveHold='balanced';window.__chain.saveReached='';window.__chain.saveThrow=false;});
    await page.getByRole('button',{name:'节点 1 转速',exact:true}).click();
    await page.getByRole('option',{name:'100 %',exact:true}).click();
    await page.waitForFunction(()=>window.__chain.saveReached==='balanced');
    const saved=(await snap()).trace.filter(x=>x.op==='save-balanced').at(-1).nodes;
    await page.locator('.chart-node-hit').nth(0).scrollIntoViewIfNeeded();
    if(editor==='pointer'){
      const point=await page.locator('.chart-node-hit').nth(0).boundingBox();assert(point);
      await page.mouse.move(point.x+point.width/2,point.y+point.height/2);await page.mouse.down();
      await page.mouse.move(point.x+point.width/2,point.y+point.height/2-20);
    } else {
      await page.locator('.chart-node-hit').nth(0).evaluate(button=>{
        button.click();button.dispatchEvent(new CustomEvent('gp:spatial-edit',{detail:{dx:0,dy:-10}}));
      });
    }
    await page.evaluate(()=>window.__chain.releaseSave());
    await page.waitForFunction(n=>window.__chain.trace.filter(x=>x.op==='enable').length>=n,before+1);
    await waitIdle();
    const done=await snap(),saves=done.trace.filter(x=>x.op==='save-balanced'),writes=done.trace.filter(x=>x.op==='enable');
    assert.equal(saves.length,1,'unconfirmed no-op creates no new save');
    assert.equal(writes.length,before+1,'the unchanged saved curve remains eligible once');
    assert.deepEqual(writes.at(-1).nodes,saved,'the valid saved curve is the only new submission');
    // fresh() navigates before releasing an outstanding pointer, avoiding an unrelated confirmation write.
  });
  for(const action of ['ON','OFF']) await check('mirror '+action+' successful current control retires a previous page-only curve-save error',async()=>{
    await fresh('GPDWin5');await finishFirstOn();
    await page.evaluate(()=>{window.__chain.saveHold='balanced';window.__chain.saveReached='';window.__chain.saveThrow=false;});
    await page.getByRole('button',{name:'节点 1 转速',exact:true}).click();
    await page.getByRole('option',{name:'80 %',exact:true}).click();
    await page.waitForFunction(()=>window.__chain.saveReached==='balanced');
    await page.evaluate(()=>{window.__chain.saveThrow=true;window.__chain.releaseSave();});await waitIdle();
    assert.match((await snap()).status,/保存失败/,'the current save error must really be shown first');
    await page.evaluate(async action=>{
      const c=window.__chain;
      // Use the production lifecycle for the external action before publishing its real display event.
      if(action==='ON')await c.lifecycle.apply(c.curves.aggressive);else await c.lifecycle.disable();
      window.__fanMirrorPublish({preset:'aggressive',active:action==='ON',pending:false,notice:action==='ON'?'控制已开启':'风扇控制未开启'});
    },action);
    await waitIdle();
    const s=await snap();
    assert(!/保存失败/.test(s.status),'a retired local save error cannot cover the newer successful mirror action: '+s.status);
    assert.equal(s.hasIntent,action==='ON');
    assert.equal(s.button,action==='ON'?'风扇已开启':'风扇未操控');
    assert.equal(s.status,action==='ON'?'控制已开启':'风扇控制未开启');
    if(action==='ON')assert.deepEqual(s.trace.filter(x=>x.op==='enable').at(-1).nodes,await page.evaluate(()=>window.__chain.curves.aggressive));
  });
  await check('mirror error control: ordinary live telemetry does not erase a current unsaved-edit failure',async()=>{
    await fresh('GPDWin5');await finishFirstOn();
    await page.evaluate(()=>{window.__chain.saveHold='balanced';window.__chain.saveReached='';window.__chain.saveThrow=false;});
    await page.getByRole('button',{name:'节点 1 转速',exact:true}).click();
    await page.getByRole('option',{name:'80 %',exact:true}).click();
    await page.waitForFunction(()=>window.__chain.saveReached==='balanced');
    await page.evaluate(()=>{window.__chain.saveThrow=true;window.__chain.releaseSave();});await waitIdle();
    const stateReads=(await snap()).trace.filter(x=>x.op==='state').length;
    await page.waitForFunction(n=>window.__chain.trace.filter(x=>x.op==='state').length>n,stateReads,{timeout:6500});
    await waitIdle();const s=await snap();
    assert.match(s.status,/保存失败/,'existing live old curve is not proof that a failed new curve was saved');
    assert.equal(s.button,'风扇已开启');
  });
  const sourceBinding={uiSha256:createHash('sha256').update(fs.readFileSync(process.env.YMCC_FAN_PRESET_UI_BEFORE||path.join(root,'src/views/FanView.vue'))).digest('hex'),bridgeSha256:createHash('sha256').update(fs.readFileSync(process.env.YMCC_FAN_BRIDGE_SOURCE_BEFORE||path.join(root,'src/bridge/fanHost.ts'))).digest('hex'),fixtureSha256:createHash('sha256').update(fs.readFileSync(fixture)).digest('hex'),bundleSha256:createHash('sha256').update(js).digest('hex')};
  const summary={sourceBinding,ok:cases.every(x=>x.ok)&&errors.length===0,checks:cases.length,cases,pageErrors:errors,scope:'real FanView + real FanHostLifecycle + real Vue remount; controlled settings completion and inert adapter/native/launcher, no device/CPU comparison'};
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(summary,null,2));
  console.log(JSON.stringify({ok:summary.ok,checks:summary.checks,results:path.join(out,'results.json')}));
  if(!summary.ok)process.exitCode=1;
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r));}
