// Real SleepGuardView/Toggle/focus helpers; all Steam/settings operations are isolated mocks.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild');
const {parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_steam-overlay-confirm-selftest.cjs'))('playwright');
const out=process.env.YMCC_STEAM_OVERLAY_BROWSER_OUT||path.resolve(root,'../../Build/Validation/steam-overlay-confirm-20261008/browser');
fs.mkdirSync(out,{recursive:true});
const cases=[],screenshots=[],errors=[],styles=[];
const source=fs.readFileSync(path.join(root,'src/views/SleepGuardView.vue'),'utf8');
const native=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
const reference=fs.readFileSync(path.join(root,'src/views/PerformanceScheduleView.vue'),'utf8');
const resetCss=s=>s.slice(s.indexOf('.reset-confirm {'),s.lastIndexOf('</style>')).trim();
assert.equal(resetCss(source),resetCss(reference));
for(const token of ['steamOverlayFixGet','steamOverlayFix.updated','SteamOverlayFixStatus'])assert(!source.includes(token));
assert(source.includes('steamOverlayOffFix: false'));
assert(fs.readFileSync(path.join(root,'src/bridge/settingsRepository.ts'),'utf8').includes('steamOverlayOffFix: false'));
assert(fs.readFileSync(path.join(root,'src/bridge/yeman.ts'),'utf8').includes('steamOverlayOffFix: r.steamOverlayOffFix === true'));
assert(native.includes('kSteamOverlayFixDefaultEnabled = false'));
const observer=native.slice(native.indexOf('static void steamSettingsObserverStart() {'),native.indexOf('static void steamSettingsObserverStart() {')+1200);
assert(!observer.includes('steamOverlayFixKick'));
assert(!observer.includes('g_sofPending'));
const kick=native.slice(native.indexOf('static void steamOverlayFixKick(const char* reason) {'),native.indexOf('// SteamDeck desktop right-stick sensitivity.'));
assert(!kick.includes('beginWindow')&&!kick.includes('SetTimer')&&!kick.includes('Sleep('));
const overlayAttempt=native.slice(native.indexOf('static void sofAttemptLocked('),native.indexOf('static void steamOverlayFixKick(const char* reason) {'));
assert(!overlayAttempt.includes('Sleep(')&&!overlayAttempt.includes('for (int'));
const single=native.slice(native.indexOf('if (a.size() == 1 && a.contains("steamOverlayOffFix"))'),native.indexOf('if (a.size() == 1 && a.contains("steamOverlayOffFix"))')+680);
assert(single.indexOf('ymSettingsPatchSection')<single.indexOf('steamOverlayFixKick'));
cases.push('Source contract: every default is off; no Steam status/polling/startup/session apply; persist before queue; dialog CSS exactly matches configuration reset');

const mocks={
  '@/bridge/yeman':`
    export const PW={YEMAN:'1cb8b882-a900-4b9f-9bac-99d151e64441'};
    export async function sleepGuardGet(){const f=window.__fixture;f.guardReads++;const snapshot={...f.guard};if(f.failRead)throw Error('read unavailable');if(f.holdRead){f.holdRead=false;await new Promise(resolve=>f.releaseRead=resolve);}return snapshot;}
    export async function sleepGuardSet(on){window.__fixture.otherWrites.push({enabled:on});window.__fixture.guard.enabled=on;}
    export async function sleepGuardSetConfig(patch){const f=window.__fixture;f.saves.push(patch);if(f.holdSave)await new Promise(resolve=>f.releaseSave=resolve);if(f.failSave)throw Error('settings save unavailable');Object.assign(f.guard,patch);if('steamOverlayOffFix' in patch)sessionStorage.setItem('steam-overlay-choice',JSON.stringify(patch.steamOverlayOffFix));}
    export const getPowerBtnIdx=async()=>2,setPowerBtnIdx=async()=>{},getActiveScheme=async()=>PW.YEMAN,reactivateCurrentScheme=async()=>{};
    export const readHibernateState=async()=>({enabledKnown:true,enabled:true,supportedKnown:true,supported:true}),setHibernate=async()=>({exitCode:0}),readHibernateSize=async()=>50,setHibernateSize=async()=>{},readTotalMemoryGB=async()=>16;
    export const readSleepTimeouts=async()=>({acScreen:0,dcScreen:0,acSleep:0,dcSleep:0,acHibernate:0,dcHibernate:0}),setSleepTimeout=async()=>{};
    export const getSleepPowerPlanOptimizationEnabled=async()=>true,setSleepPowerPlanOptimizationEnabled=async()=>{};
    export async function steamOverlayFixGet(){throw Error('forbidden live Steam getter');}
  `,
  '@/bridge/api':`export const wakePassword={async set(){throw Error('unrelated power setting touched')}};`,
};
const bundle=await build({
  stdin:{resolveDir:root,loader:'ts',contents:`
    import {createApp,ref,h,KeepAlive} from 'vue';import Page from './src/views/SleepGuardView.vue';
    const preference=JSON.parse(sessionStorage.getItem('steam-overlay-choice')||'false');
    window.__fixture={guardReads:0,saves:[],otherWrites:[],holdSave:false,failSave:false,holdRead:false,failRead:false,guard:{enabled:true,mode:'custom',suspended:0,pauseGameOnSleep:true,retryOnEntryFailure:true,retryOnNonUserWake:true,steamOverlayOffFix:preference,joyXoffAutoClose:true,wakePasswordEnabled:false}};
    const refreshKey=ref(0);window.__refresh=()=>refreshKey.value++;
    let app;window.__mount=()=>{const shown=ref(true);window.__shown=v=>shown.value=v;app=createApp({setup:()=>()=>h(KeepAlive,null,()=>shown.value?h(Page):h('div',{id:'hidden-page'},'Other page'))});app.provide('globalRefreshKey',refreshKey);app.mount('#app')};window.__remount=()=>{app.unmount();window.__mount()};window.__mount();
  `},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},
  plugins:[{name:'steam-overlay-confirm-fixture',setup(b){
    b.onResolve({filter:/.*/},args=>args.path in mocks?{path:args.path,namespace:'fixture'}:null);
    b.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
    b.onLoad({filter:/\.vue$/},args=>{
      const {descriptor,errors}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});assert.deepEqual(errors,[]);
      const id='data-v-'+createHash('sha256').update(args.path).digest('hex').slice(0,8);
      const compiled=compileScript(descriptor,{id,inlineTemplate:true});
      for(const style of descriptor.styles){const css=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.deepEqual(css.errors,[]);styles.push(css.code);}
      return {contents:compiled.content.replace('export default','const _component =')+'\n_component.__scopeId='+JSON.stringify(id)+';export default _component;',loader:'ts',resolveDir:path.dirname(args.path)};
    });
  }}],
});
const js=bundle.outputFiles[0].text;
const css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html='<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>Steam修复确认验证</title><style>'+css+'\nbody{margin:0;background:var(--bg-solid);color:var(--text);font-family:"Microsoft YaHei",system-ui}#app{max-width:620px;margin:auto;padding:16px}</style><main id="app"></main><script src="/fixture.js"></script></html>';
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');response.end(request.url==='/fixture.js'?js:html);});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try{
  browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page=await browser.newPage({viewport:{width:680,height:960}});page.on('pageerror',e=>errors.push(e.message));
  await page.goto('http://127.0.0.1:'+server.address().port);
  const row=page.locator('.toggle-row').filter({has:page.locator('.toggle-label',{hasText:/^Steam大屏唤醒手柄卡死修复$/})});
  const button=row.locator('button.switch');
  const dialog=page.locator('.steam-overlay-confirm:not([inert])');
  const checked=()=>row.getAttribute('aria-checked');
  const writes=()=>page.evaluate(()=>window.__fixture.saves.length);
  const waitChoice=v=>page.waitForFunction(expected=>document.querySelectorAll('.toggle-label')&&[...document.querySelectorAll('.toggle-row')].find(n=>n.textContent.includes('Steam大屏唤醒手柄卡死修复')).getAttribute('aria-checked')===String(expected),v);
  const closed=()=>page.locator('.steam-overlay-confirm').waitFor({state:'detached'});
  async function open(target){await button.click();await dialog.waitFor({state:'visible'});assert.equal(await dialog.locator('.rc-title').textContent(),target?'确认开启':'确认关闭');assert.equal(await dialog.locator('.rc-desc').textContent(),target?'影响Steam监控和触摸板转盘显示':'学习版手柄唤醒容易卡界面');assert.deepEqual(await dialog.locator('button').allTextContents(),['取消',target?'确认开启':'确认关闭']);assert.equal(await dialog.getAttribute('role'),'alertdialog');assert.equal(await dialog.getAttribute('aria-modal'),'true');assert.equal(await dialog.getAttribute('data-gp-modal'),'');await page.waitForFunction(()=>document.activeElement?.textContent==='取消');}
  async function focusRestored(){await page.waitForFunction(()=>document.activeElement?.matches('button.switch'));assert(await button.evaluate(n=>n===document.activeElement));}
  await button.waitFor({state:'visible'});await page.waitForFunction(()=>window.__fixture.guardReads>=1);
  assert.equal(await checked(),'false');assert.equal(await row.locator('.toggle-desc').textContent(),'Steam设置-游戏中-Steam叠加画面关闭');assert.equal(await writes(),0);
  cases.push('First-run default off, exact subtitle, mount reads memory only and does not apply Steam settings');
  await page.waitForTimeout(450);await page.evaluate(()=>{window.__remount();window.__refresh()});await page.waitForFunction(()=>window.__fixture.guardReads>=3);assert.equal(await writes(),0);
  cases.push('Idle/remount/global refresh send no writes or Steam live getter calls');

  await open(true);assert.equal(await checked(),'false');assert.equal(await writes(),0);await dialog.getByRole('button',{name:'取消',exact:true}).click();await closed();await focusRestored();assert.equal(await checked(),'false');assert.equal(await writes(),0);
  cases.push('Click only opens enable confirmation; cancel is default focus; cancel leaves memory untouched and restores switch focus');
  await open(true);await page.keyboard.press('Escape');await closed();await focusRestored();assert.equal(await writes(),0);
  await open(true);const back=await page.evaluate(()=>{const event=new Event('ipc:gamepad-back',{cancelable:true});window.dispatchEvent(event);return event.defaultPrevented;});assert.equal(back,true);await closed();await focusRestored();assert.equal(await writes(),0);
  await open(true);await page.mouse.click(4,4);await closed();await focusRestored();assert.equal(await writes(),0);
  cases.push('Escape, controller B and outside pointer cancel without side effects');

  await page.evaluate(()=>window.__fixture.holdSave=true);await open(true);await dialog.getByRole('button',{name:'确认开启',exact:true}).click();await page.waitForFunction(()=>typeof window.__fixture.releaseSave==='function');assert.equal(await button.isDisabled(),true);assert.equal(await checked(),'false');await row.click();assert.equal(await writes(),1);assert.deepEqual(await page.evaluate(()=>window.__fixture.saves),[{steamOverlayOffFix:true}]);
  await page.evaluate(()=>{window.__fixture.holdSave=false;window.__fixture.releaseSave();delete window.__fixture.releaseSave});await waitChoice(true);await closed();await focusRestored();
  cases.push('Confirm enable submits only {steamOverlayOffFix:true}; disabled while awaiting ACK, no duplicate writes, UI changes only after success');
  await open(false);assert.equal(await checked(),'true');await dialog.getByRole('button',{name:'取消',exact:true}).click();await closed();assert.equal(await checked(),'true');assert.equal(await writes(),1);
  cases.push('Disable shows the exact learning-version warning and cancel/confirm-disable buttons; cancel preserves enabled state');
  await button.focus();await page.keyboard.press('Space');await dialog.waitFor({state:'visible'});await dialog.getByRole('button',{name:'确认关闭',exact:true}).focus();assert.equal(await dialog.getByRole('button',{name:'确认关闭',exact:true}).getAttribute('data-gp-group'),'steam-overlay-confirm');await page.keyboard.press('Enter');await waitChoice(false);await closed();assert.deepEqual(await page.evaluate(()=>window.__fixture.saves),[{steamOverlayOffFix:true},{steamOverlayOffFix:false}]);
  cases.push('Real buttons are keyboard/controller-focus reachable; confirm disable submits only its own false parameter');

  await page.evaluate(()=>window.__fixture.failSave=true);await open(true);await dialog.getByRole('button',{name:'确认开启',exact:true}).click();await page.waitForFunction(()=>document.querySelector('.msg')?.textContent.includes('settings save unavailable'));await closed();assert.equal(await checked(),'false');assert.equal(await button.isDisabled(),false);await focusRestored();await open(true);await dialog.getByRole('button',{name:'取消',exact:true}).click();await closed();
  cases.push('Save failure keeps original state, reports error, re-enables switch and allows reopening/cancelling');
  await page.evaluate(()=>{window.__fixture.failSave=false;window.__fixture.holdRead=true;window.__refresh()});await page.waitForFunction(()=>typeof window.__fixture.releaseRead==='function');await open(true);await dialog.getByRole('button',{name:'确认开启',exact:true}).click();await waitChoice(true);await page.evaluate(()=>{window.__fixture.releaseRead();delete window.__fixture.releaseRead});await closed();await page.waitForTimeout(80);assert.equal(await checked(),'true');
  cases.push('A delayed pre-save refresh snapshot cannot overwrite the newly confirmed parameter');

  await page.reload();await waitChoice(true);assert.equal(await writes(),0);await page.evaluate(()=>window.__refresh());await page.waitForFunction(()=>window.__fixture.guardReads>=2);assert.equal(await checked(),'true');assert.equal(await writes(),0);await page.waitForTimeout(500);assert.equal(await writes(),0);
  cases.push('Reload restores an existing enabled preference but never auto-applies, forces off, detects Steam, or retries it');
  await open(false);await page.evaluate(()=>window.__shown(false));await page.locator('#hidden-page').waitFor();await closed();assert.equal(await page.evaluate(()=>{const e=new Event('ipc:gamepad-back',{cancelable:true});window.dispatchEvent(e);return e.defaultPrevented;}),false);
  for(let i=0;i<3;i++){await page.evaluate(()=>window.__shown(true));await button.waitFor({state:'visible'});await page.evaluate(()=>window.__shown(false));await page.locator('#hidden-page').waitFor();}
  await page.evaluate(()=>window.__shown(true));await button.waitFor({state:'visible'});await open(false);const backCalls=await page.evaluate(()=>{const e=new Event('ipc:gamepad-back',{cancelable:true});let calls=0;e.preventDefault=()=>{calls++;Event.prototype.preventDefault.call(e)};window.dispatchEvent(e);return {calls,prevented:e.defaultPrevented}});assert.deepEqual(backCalls,{calls:1,prevented:true});await closed();assert.equal(await writes(),0);
  cases.push('KeepAlive deactivation removes dialog/listeners; hidden page does not intercept B and repeated activations never duplicate handlers');

  const timeout=page.getByRole('button',{name:'屏幕、睡眠和休眠超时',exact:false});await timeout.click();await page.waitForFunction(()=>document.querySelector('.timeout-body:not([inert])'));await open(false);await page.evaluate(()=>window.dispatchEvent(new Event('ipc:gamepad-back',{cancelable:true})));await closed();assert(await page.locator('.timeout-body:not([inert])').isVisible());await page.evaluate(()=>window.dispatchEvent(new Event('ipc:gamepad-back',{cancelable:true})));await page.locator('.timeout-body').waitFor({state:'detached'});
  cases.push('Controller B cancels the foreground confirmation first, without also collapsing the open timeout card');

  for(const viewport of [{width:420,height:800},{width:680,height:960},{width:1280,height:720}]){
    await page.setViewportSize(viewport);
    for(const target of [false,true]){
      assert.equal(await checked(),String(!target));await open(target);await page.waitForTimeout(250);
      const rect=await dialog.boundingBox();assert(rect);assert(rect.x>=7&&rect.y>=0&&rect.x+rect.width<=viewport.width-7&&rect.y+rect.height<=viewport.height,JSON.stringify({viewport,rect}));
      for(const b of await dialog.locator('button').all()){assert(await b.isVisible());const r=await b.boundingBox();assert(r.y>=rect.y&&r.y+r.height<=rect.y+rect.height+1);}
      const shot=path.join(out,`steam-overlay-${target?'enable':'disable'}-${viewport.width}x${viewport.height}.png`);await page.screenshot({path:shot});screenshots.push(shot);
      await dialog.getByRole('button',{name:target?'确认开启':'确认关闭',exact:true}).click();await waitChoice(target);await closed();
    }
  }
  cases.push('Enable/disable popups stay within safe viewport and both buttons remain visible at 420x800, 680x960 and 1280x720; six screenshots recorded');
  await page.evaluate(()=>{sessionStorage.removeItem('steam-overlay-choice');window.__fixture.guard.steamOverlayOffFix=false;window.__fixture.failRead=true;window.__remount()});await button.waitFor({state:'visible'});assert.equal(await checked(),'false');const beforeIdle=await writes();await page.waitForTimeout(500);assert.equal(await writes(),beforeIdle);
  cases.push('Failed remembered-parameter read keeps first-run default off; no automatic writes after manual changes or idle');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.otherWrites),[]);assert.deepEqual(errors,[]);
  const report={suite:'Sleep Steam overlay confirmation UI browser',passed:cases.length,cases,screenshots,pageErrors:errors,scope:'Real SleepGuardView, Toggle and controller focus/safe placement; mocked parameter store only, zero real Steam/installed settings changes'};
  fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
