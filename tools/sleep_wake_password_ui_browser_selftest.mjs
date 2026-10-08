// Real SleepGuardView/Toggle + real wakePassword API; all OS writes are isolated mocks.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild');
const {parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_sleep-wake-password-selftest.cjs'))('playwright');
const out=process.env.YMCC_WAKE_PASSWORD_BROWSER_OUT||path.resolve(root,'../../Build/Validation/sleep-wake-password-20261007/browser');
fs.mkdirSync(out,{recursive:true});
const cases=[],screenshots=[],errors=[],styles=[];
const pageSource=fs.readFileSync(path.join(root,'src/views/SleepGuardView.vue'),'utf8');
const native=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
const domain=fs.readFileSync(path.join(root,'native/sleep_wake_password.h'),'utf8');
assert(!pageSource.includes('wakePassword.get('));
assert(!native.includes('ipc_on("power.wakePassword.get"'));
assert.equal((native.match(/sleep_wake_password::apply\(/g)||[]).length,1);
assert(!domain.includes('SCHEME_CURRENT')||domain.indexOf('SCHEME_CURRENT')<domain.indexOf('namespace'));
for(const planToken of ['WIN_BAL','GUID_BALANCED_POWER_SCHEME','381b4222'])assert(!domain.includes(planToken));
assert(native.includes('{"wakePasswordEnabled", currentSleep.value("wakePasswordEnabled", false)}'));
cases.push('Source contract: only one user-action setter, no system-read getter or automatic wake-password path; unrelated sleep saves preserve preference');

const mocks={
  '@/bridge/yeman':`
    export const PW={YEMAN:'1cb8b882-a900-4b9f-9bac-99d151e64441'};
    export async function sleepGuardGet(){window.__fixture.guardReads++;return {...window.__fixture.guard,wakePasswordEnabled:window.__fixture.preference};}
    export async function sleepGuardSet(on){window.__fixture.guardWrites.push({enabled:on});window.__fixture.guard.enabled=on;}
    export async function sleepGuardSetConfig(patch){window.__fixture.guardWrites.push(patch);Object.assign(window.__fixture.guard,patch);}
    export const getPowerBtnIdx=async()=>2,setPowerBtnIdx=async()=>{},getActiveScheme=async()=>PW.YEMAN,reactivateCurrentScheme=async()=>{};
    export const readHibernateState=async()=>({enabledKnown:true,enabled:true,supportedKnown:true,supported:true}),setHibernate=async()=>({exitCode:0}),readHibernateSize=async()=>50,setHibernateSize=async()=>{},readTotalMemoryGB=async()=>16;
    export const readSleepTimeouts=async()=>({acScreen:0,dcScreen:0,acSleep:0,dcSleep:0,acHibernate:0,dcHibernate:0}),setSleepTimeout=async()=>{};
    export const getSleepPowerPlanOptimizationEnabled=async()=>true,setSleepPowerPlanOptimizationEnabled=async()=>{};
  `,
  '@/bridge/ipc':`
    export async function invoke(method,args){
      const f=window.__fixture;f.invokes.push({method,args});
      if(method!=='power.wakePassword.set')throw Error('unexpected extra native IPC: '+method);
      if(f.hold)await new Promise(resolve=>f.release=resolve);
      if(f.throwSet)throw Error('power writes blocked during suspend');
      if(f.failure){const bad=f.failure;f.failure=null;return bad;}
      f.ac=f.dc=Number(args.enabled);f.preference=args.enabled;
      sessionStorage.setItem('wake-password-preference',JSON.stringify(f.preference));
      return {ok:true,known:true,enabled:args.enabled,mixed:false,ac:f.ac,dc:f.dc,win32Error:0,rollbackOk:true,preferenceSaved:!f.failPreferenceSave};
    }
  `,
  '@/gamepad/focus':`export const focusGamepadElement=n=>n?.focus();export function getGamepadPopupPlacement(){return {style:{},above:false}};`,
};
const bundle=await build({
  stdin:{resolveDir:root,loader:'ts',contents:`
    import {createApp,ref} from 'vue';import Page from './src/views/SleepGuardView.vue';
    const preference=JSON.parse(sessionStorage.getItem('wake-password-preference')||'false');
    window.__fixture={preference,ac:Number(preference),dc:Number(preference),invokes:[],guardWrites:[],guardReads:0,guard:{enabled:true,mode:'custom',suspended:0,pauseGameOnSleep:true,retryOnEntryFailure:true,retryOnNonUserWake:true,steamOverlayOffFix:true,joyXoffAutoClose:true}};
    const refreshKey=ref(0);window.__refresh=()=>refreshKey.value++;
    let app;window.__mount=()=>{app=createApp(Page);app.provide('globalRefreshKey',refreshKey);app.mount('#app')};window.__remount=()=>{app.unmount();window.__mount()};window.__mount();
  `},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},
  plugins:[{name:'sleep-wake-password-fixture',setup(b){
    b.onResolve({filter:/.*/},args=>{
      if(args.path==='./ipc'&&args.importer.endsWith('api.ts'))return {path:'@/bridge/ipc',namespace:'fixture'};
      return args.path in mocks?{path:args.path,namespace:'fixture'}:null;
    });
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
const html='<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>睡眠唤醒密码验证</title><style>'+css+'\nbody{margin:0;background:var(--bg-solid);color:var(--text);font-family:"Microsoft YaHei",system-ui}#app{max-width:620px;margin:auto;padding:16px}</style><main id="app"></main><script src="/fixture.js"></script></html>';
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');response.end(request.url==='/fixture.js'?js:html);});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try{
  browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page=await browser.newPage({viewport:{width:680,height:960}});page.on('pageerror',e=>errors.push(e.message));
  await page.goto('http://127.0.0.1:'+server.address().port);
  const row=page.locator('.toggle-row').filter({has:page.locator('.toggle-label',{hasText:/^唤醒需要密码$/})});
  const button=row.locator('button.switch');
  await button.waitFor({state:'visible'});
  const checked=()=>row.getAttribute('aria-checked');
  assert.equal(await checked(),'false');assert.equal(await row.locator('.toggle-desc').textContent(),'无效可能是组策略锁定了');
  const labels=await page.locator('.card').filter({has:page.locator('.card-title',{hasText:'睡眠操作'})}).locator('.toggle-label').allTextContents();
  assert.deepEqual(labels.slice(0,2),['唤醒需要密码','睡眠时暂停游戏']);
  assert.equal(await page.evaluate(()=>window.__fixture.invokes.length),0);
  cases.push('Default is off; exact title/description appear immediately above pause-game; page mount sends zero wake-password system IPC');
  await button.focus();assert.equal(await button.evaluate(node=>document.activeElement===node),true);
  cases.push('The switch uses a real focusable button for keyboard/controller activation');
  const shot=path.join(out,'sleep-wake-password-default.png');await page.screenshot({path:shot,fullPage:true});screenshots.push(shot);

  await page.waitForTimeout(500);
  await page.evaluate(()=>{window.__remount();window.__refresh();});
  await page.waitForFunction(()=>window.__fixture.guardReads>=3);
  assert.equal(await page.evaluate(()=>window.__fixture.invokes.length),0);
  cases.push('Idle, page remount and global refresh do not read/write/activate the wake-password power setting');
  await button.focus();await page.keyboard.press('Space');
  await page.waitForFunction(()=>window.__fixture.ac===1&&window.__fixture.dc===1);
  assert.equal(await checked(),'true');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.invokes),[{method:'power.wakePassword.set',args:{enabled:true}}]);
  assert.equal(await page.evaluate(()=>window.__fixture.guardWrites.length),0);
  cases.push('Only a user Space/A-style click sends enabled=true, unifies AC/DC and does not modify the sleep guard');
  await button.click();await page.waitForFunction(()=>window.__fixture.ac===0&&window.__fixture.dc===0);
  assert.equal(await checked(),'false');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.invokes.at(-1)),{method:'power.wakePassword.set',args:{enabled:false}});
  cases.push('User disable sends enabled=false for AC/DC=0 without a current/Balanced scheme argument');

  await page.evaluate(()=>{window.__fixture.hold=true;});await button.click();
  await page.waitForFunction(()=>typeof window.__fixture.release==='function');
  assert.equal(await button.isDisabled(),true);assert.equal(await checked(),'false');
  const waitingCalls=await page.evaluate(()=>window.__fixture.invokes.length);
  await row.click();assert.equal(await page.evaluate(()=>window.__fixture.invokes.length),waitingCalls);
  await page.evaluate(()=>{window.__fixture.hold=false;window.__fixture.release();delete window.__fixture.release;});
  await page.waitForFunction(()=>window.__fixture.ac===1&&window.__fixture.dc===1);
  assert.equal(await checked(),'true');
  cases.push('Pending write disables duplicate clicks and the switch changes only after verified ACK');
  await page.reload();await button.waitFor({state:'visible'});await page.waitForFunction(()=>window.__fixture.guardReads===1);
  assert.equal(await checked(),'true');assert.equal(await page.evaluate(()=>window.__fixture.invokes.length),0);
  cases.push('Saved user choice survives F5 via existing sleep configuration only; restarting the page does not reapply it');
  await button.click();await page.waitForFunction(()=>window.__fixture.dc===0);

  await page.evaluate(()=>{window.__fixture.failure={ok:false,known:true,enabled:false,mixed:false,ac:0,dc:0,win32Error:5,stage:'write-dc',rollbackOk:true};});
  await button.click();await page.waitForFunction(()=>document.querySelector('.msg')?.textContent.includes('唤醒密码设置失败'));
  assert.equal(await checked(),'false');assert((await page.locator('.msg').textContent()).includes('组策略'));
  cases.push('Failed DC/policy write keeps original UI state and reports failure instead of pretending success');
  await page.evaluate(()=>{window.__fixture.failure={ok:false,known:false,enabled:false,mixed:false,ac:0,dc:0,win32Error:2,stage:'read',rollbackOk:true};});
  await button.click();await page.waitForFunction(()=>document.querySelector('.msg')?.textContent.includes('错误 2'));
  assert.equal(await checked(),'false');assert.equal(await button.isDisabled(),false);
  cases.push('Missing/unreadable fixed plan reports error and leaves a retryable user switch; no automatic fallback to another plan');
  await page.evaluate(()=>{window.__fixture.throwSet=true;});await button.click();
  await page.waitForFunction(()=>document.querySelector('.msg')?.textContent.includes('power writes blocked'));
  assert.equal(await checked(),'false');await page.evaluate(()=>{window.__fixture.throwSet=false;});
  cases.push('Power-transition/transport exception re-enables controls without automatic retries or false state');
  await page.evaluate(()=>{window.__fixture.failPreferenceSave=true;});await button.click();
  await page.waitForFunction(()=>window.__fixture.dc===1);
  assert.equal(await checked(),'true');assert((await page.locator('.msg').textContent()).includes('状态记忆保存失败'));
  cases.push('Hardware success plus preference-save failure reports only the memory failure, not a false power-setting failure');
  await page.evaluate(()=>{window.__fixture.failPreferenceSave=false;});
  const pause=page.locator('.toggle-row').filter({has:page.locator('.toggle-label',{hasText:/^睡眠时暂停游戏$/})}).locator('button');
  const callsBeforePause=await page.evaluate(()=>window.__fixture.invokes.length);
  await pause.click();await page.waitForFunction(()=>window.__fixture.guard.pauseGameOnSleep===false);
  await page.evaluate(()=>window.__refresh());
  await page.waitForFunction(()=>window.__fixture.guardReads>=2);
  assert.equal(await checked(),'true');assert.equal(await page.evaluate(()=>window.__fixture.invokes.length),callsBeforePause);
  cases.push('Other sleep switches and refresh preserve the remembered wake-password choice without touching its power setting');
  await page.waitForTimeout(700);assert.equal(await page.evaluate(()=>window.__fixture.invokes.length),callsBeforePause);
  cases.push('No wake-password polling, deferred reactivation, scheduled retry or idle work after the user action');
  assert.deepEqual(errors,[]);
  const report={suite:'Sleep wake password UI browser',passed:cases.length,cases,screenshots,pageErrors:errors,scope:'Real SleepGuardView + Toggle + wakePassword API; native invocation/storage mocked, no installed power-plan writes'};
  fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
