// Real controller page/card in Chromium; native, storage and recording mocked.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
const root=process.cwd(),req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_controller-ui-selftest.cjs'))('playwright');
const out=process.env.YMCC_UI_BROWSER_OUT || 'G:/YeManCC-Work/Mainline/Build/Validation/controller-shortcut-ui-merge-20261005';
fs.mkdirSync(out,{recursive:true});
const styles=[];
const mocks={
  '@/bridge/steamDeckMouse':`export const STEAM_DECK_MOUSE_DEFAULT=100;const get=()=>window.__fixture.mouseState??={ok:true,available:true,percent:100,pending:false,steamRunning:true};export async function steamDeckMouseGet(){return structuredClone(get())};export async function steamDeckMouseSet(percent){const f=window.__fixture;f.mouseWrites=(f.mouseWrites||0)+1;(f.mouseTargets??=[]).push(percent);if(f.mouseDelay)await new Promise(r=>setTimeout(r,f.mouseDelay));f.mouseState=f.mouseLive?{...get(),percent,pending:false,liveAvailable:true,appliedLive:true}:{...get(),pending:true,desiredPercent:percent};return structuredClone(f.mouseState)};export function steamDeckMouseMessage(){return '布局不可用'}`,
  '@/bridge/yeman':`export async function summonGet(){return structuredClone(window.__fixture.gamepad)}; export async function summonSet(p){window.__fixture.nativeWrites++;Object.assign(window.__fixture.gamepad,p);return summonGet()}`,
  '@/bridge/gameproc':`export async function closeJoyxoffIfRunning(){}; export async function getGameInputRedistState(){return {mouseInterfaceAvailable:true}};export async function installGameInputRedist(){};export async function setMouseBackend(backend){return {ok:true,backend,on:true}}`,
  '@/bridge/settingsRepository':`import {evaluateInputCasWrite} from ${JSON.stringify(path.join(root,'src/bridge/settingsRepository.ts'))};export async function loadSettings(){return structuredClone(window.__fixture.settings)};export async function compareAndSwapInputSettings(revision,patch){const f=window.__fixture;f.writes++;const r=evaluateInputCasWrite(f.settings.input,revision,patch,true);if(r.ok)f.settings.input=r.value;return r}`,
  '@/bridge/gameInputOverride':`export function getGameInputOverrideState(){return {locked:false,identity:''}};export function subscribeGameInputOverrideState(cb){cb(getGameInputOverrideState());return ()=>{}}`,
  '@/bridge/ipc':`export function on(name,cb){const events=window.__fixture.events??={};events[name]=cb;return ()=>{if(events[name]===cb)delete events[name]}};export async function invoke(){return {}}`,
  '@/bridge/api':`export const fs={},settingsStore={},shell={open:async()=>{}}`,
  '@/gamepad/focus':`export function focusGamepadElement(node){node?.focus()};export function getGamepadPopupPlacement(){return {style:{},above:false}}`,
  '@/components/ControllerFeedbackCapabilitiesCard.vue':`export default {render(){return null}}`,
  '@/views/ControllerShortcutEditorView.vue':`import {h} from 'vue';export default {emits:['close'],setup(_,ctx){return ()=>h('section',{role:'dialog','aria-label':'editor fixture','data-editor-fixture':true},[h('button',{onClick:()=>ctx.emit('close')},'关闭编辑器')])}}`,
};
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:`
import {createApp} from 'vue';import Page from './src/views/ButtonMappingView.vue';
import {normalizeSettings} from './src/bridge/settingsRepository.ts';
import {NATIVE_DEFAULT_SHORTCUTS,nativeDefaultRule,newControllerShortcutRule} from './src/bridge/controllerShortcutRules.ts';
const gamepad={enabled:true,mouseBackend:'joyxoff',...Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map(d=>[d.defaultKey,true]))};
const custom={...newControllerShortcutRule('custom-oem','keyboard.numeric'),inputs:[{source:'oem',code:'m1'}],params:{key:'F8'}};
window.__fixture={gamepad,writes:0,nativeWrites:0,settings:normalizeSettings({input:{outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,oemRearMap:{m1:'left',m2:'right',unknown:'both'}},buttonMapping:{rules:{schemaVersion:2,shortcutRules:[...NATIVE_DEFAULT_SHORTCUTS.map(d=>nativeDefaultRule(d.defaultKey)),custom]}}}})};
createApp(Page).mount('#app');`},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'controller-ui-fixture',setup(b){
  b.onResolve({filter:/.*/},args=>{if(args.path in mocks)return {path:args.path,namespace:'fixture'};
    if(args.path==='./api'&&args.importer.endsWith('settingsRepository.ts'))return {path:'@/bridge/api',namespace:'fixture'};});
  b.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
  b.onLoad({filter:/\.vue$/},args=>{
    const {descriptor,errors}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});assert.equal(errors.length,0);
    const id='data-v-'+createHash('sha256').update(args.path).digest('hex').slice(0,8);
    const compiled=compileScript(descriptor,{id,inlineTemplate:true});
    for(const style of descriptor.styles){const css=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}
    return {contents:compiled.content.replace('export default','const _component =')+`\n_component.__scopeId=${JSON.stringify(id)};export default _component;`,loader:'ts',resolveDir:path.dirname(args.path)};
  });
}}]});
const js=result.outputFiles[0].text;
const css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html=`<!doctype html><meta charset="utf-8"><title>Controller UI isolated validation</title><style>${css}\nbody{background:var(--bg-solid);margin:0;padding:16px;}#app{max-width:760px;margin:0 auto;}[data-editor-fixture]{position:fixed;inset:20%;z-index:500;background:var(--bg-panel);padding:30px;border:1px solid var(--accent)}</style><main id="app"></main><script src="/fixture.js"></script>`;
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');response.end(request.url==='/fixture.js'?js:html);});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const errors=[],cases=[],screenshots=[];let browser;
try{
  browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page=await browser.newPage();page.on('pageerror',error=>errors.push(error.message));
  for(const [width,height] of [[1280,720],[1280,800],[1920,1080],[580,800],[420,800]]){
    await page.setViewportSize({width,height});await page.goto('http://127.0.0.1:'+server.address().port);
    await page.waitForSelector('.shortcut-toggle');
    const layout=await page.evaluate(()=>{const title=document.querySelector('.shortcuts-card .card-title').getBoundingClientRect(),button=document.querySelector('.shortcut-editor-open').getBoundingClientRect(),head=document.querySelector('.shortcuts-card .card-head').getBoundingClientRect();
      return {title:{left:title.left,right:title.right,top:title.top,bottom:title.bottom},button:{left:button.left,right:button.right,top:button.top,bottom:button.bottom},head:{left:head.left,right:head.right},overflow:document.documentElement.scrollWidth>innerWidth,writes:window.__fixture.writes,editors:document.querySelectorAll('.shortcut-editor-open').length,duplicate:document.querySelectorAll('.rear-map-block,.editor-entry-card,.card-subtitle').length};});
    assert.equal(layout.editors,1);assert.equal(layout.duplicate,0);assert.equal(layout.writes,0);assert(!layout.overflow);
    assert(layout.button.left>layout.title.right);assert(Math.min(layout.title.bottom,layout.button.bottom)>Math.max(layout.title.top,layout.button.top));
    assert(layout.button.right<=layout.head.right+1);cases.push(`header inline/right/no overflow ${width}x${height}`);
    if(width===1280&&height===720){const p=path.join(out,'controller-shortcuts-1280x720.png');await page.screenshot({path:p,fullPage:true});screenshots.push(p);}
  }
  const range=page.getByRole('slider',{name:'SteamDeck鼠标灵敏度'});
  assert.equal(await range.count(),1);assert.equal(await range.getAttribute('min'),'1');assert.equal(await range.getAttribute('max'),'300');assert.equal(await range.inputValue(),'100');
  assert.equal(await range.getAttribute('step'),'any');assert.equal(await page.locator('.sensitivity-footer').count(),0);
  assert.equal(await page.getByRole('button',{name:'恢复100%',exact:true}).count(),0);
  assert(!(await page.locator('.steamdeck-mouse-bubble').textContent()).includes('默认100%'));
  cases.push('range/default/readback unchanged; removed footer and reset button');
  const backend=page.locator('.mouse-backend-buttons button');assert.deepEqual(await backend.allTextContents(),['微软鼠标','JoyXoff','SteamDeck鼠标']);
  assert.equal(await backend.nth(2).isDisabled(),true);await backend.nth(2).evaluate(n=>n.focus());assert.equal(await backend.nth(2).evaluate(n=>n===document.activeElement),false);
  cases.push('exactly three backends Microsoft/JoyXoff/SteamDeck mouse; SteamDeck indicator remains disabled and unfocusable');
  await range.evaluate(n=>{n.value='137';n.dispatchEvent(new Event('input',{bubbles:true}));n.dispatchEvent(new Event('change',{bubbles:true}));});await page.waitForFunction(()=>window.__fixture.mouseWrites===1);
  assert.equal(await page.locator('.sensitivity-hint').textContent(),'已排队135% 重新启动Steam后应用');assert.equal(await range.inputValue(),'135');assert.equal(await page.evaluate(()=>window.__fixture.writes),0);
  await range.evaluate(n=>{n.value='100';n.dispatchEvent(new Event('input',{bubbles:true}));n.dispatchEvent(new Event('change',{bubbles:true}));});await page.waitForFunction(()=>window.__fixture.mouseWrites===2);assert.equal(await range.inputValue(),'100');
  assert.equal(await page.locator('.sensitivity-hint').textContent(),'已排队100% 重新启动Steam后应用');
  cases.push('real slider snaps user changes to 5% grid and uses exact shortened queued text');
  await page.evaluate(()=>{const f=window.__fixture;f.mouseLive=true;f.mouseState={ok:true,available:true,percent:100,pending:false,steamRunning:true,liveAvailable:true};f.events['steamDeckMouse.updated'](f.mouseState);});
  await range.evaluate(n=>{for(const value of ['119','130','137']){n.value=value;n.dispatchEvent(new Event('input',{bubbles:true}));}});
  await page.waitForFunction(()=>window.__fixture.mouseWrites===3);await page.waitForFunction(()=>document.querySelector('.sensitivity-hint').textContent.includes('已通过 Steam 实时应用并保存'));
  assert.equal(await range.inputValue(),'135');assert.deepEqual(await page.evaluate(()=>window.__fixture.mouseTargets),[135,100,135]);
  cases.push('realtime drag debounce sends only final value and distinguishes live save from queue');
  await page.evaluate(()=>{window.__fixture.mouseDelay=250;});
  await range.evaluate(n=>{n.value='200';n.dispatchEvent(new Event('input',{bubbles:true}));n.dispatchEvent(new Event('change',{bubbles:true}));});await page.waitForFunction(()=>window.__fixture.mouseWrites===4);assert.equal(await range.isDisabled(),false);
  await range.evaluate(n=>{for(const value of ['220','250']){n.value=value;n.dispatchEvent(new Event('input',{bubbles:true}));n.dispatchEvent(new Event('change',{bubbles:true}));}});
  await page.waitForFunction(()=>window.__fixture.mouseWrites===5&&window.__fixture.mouseState.percent===250);assert.equal(await range.inputValue(),'250');assert.deepEqual(await page.evaluate(()=>window.__fixture.mouseTargets),[135,100,135,200,250]);
  cases.push('realtime drag remains enabled during request and serializes latest value without old response rollback');
  await page.evaluate(()=>{window.__fixture.mouseDelay=0;});
  await range.press('ArrowRight');await page.waitForFunction(()=>window.__fixture.mouseWrites===6);assert.equal(await range.inputValue(),'255');
  await range.press('ArrowLeft');await page.waitForFunction(()=>window.__fixture.mouseWrites===7);assert.equal(await range.inputValue(),'250');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.mouseTargets.slice(-2)),[255,250]);
  cases.push('actual keyboard interaction moves exactly 5% each direction');
  await page.evaluate(()=>{const f=window.__fixture;f.mouseState={...f.mouseState,percent:137,pending:false};f.events['steamDeckMouse.updated'](f.mouseState);});
  assert.equal(await range.inputValue(),'137');await page.waitForTimeout(230);assert.equal(await page.evaluate(()=>window.__fixture.mouseWrites),7);
  await range.press('ArrowRight');await page.waitForFunction(()=>window.__fixture.mouseWrites===8);assert.equal(await range.inputValue(),'140');
  cases.push('existing 137% displays unchanged and only snaps on explicit user action');
  await range.evaluate(n=>{n.value='1';n.dispatchEvent(new Event('input',{bubbles:true}));n.dispatchEvent(new Event('change',{bubbles:true}));});await page.waitForFunction(()=>window.__fixture.mouseWrites===9);assert.equal(await range.inputValue(),'1');
  await range.evaluate(n=>{n.value='300';n.dispatchEvent(new Event('input',{bubbles:true}));n.dispatchEvent(new Event('change',{bubbles:true}));});await page.waitForFunction(()=>window.__fixture.mouseWrites===10);assert.equal(await range.inputValue(),'300');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.mouseTargets.slice(-2)),[1,300]);
  cases.push('range endpoints 1% and 300% stay selectable with 5% grid');
  const open=page.locator('.shortcut-editor-open');await open.focus();assert.equal(await open.evaluate(n=>n===document.activeElement),true);
  await open.press('Enter');await page.waitForSelector('[data-editor-fixture]');assert.equal(await page.locator('[data-editor-fixture]').count(),1);
  assert.equal(await page.evaluate(()=>window.__fixture.writes),0);cases.push('merged entry keyboard focus/confirm opens exactly one editor with no save');
  await page.getByRole('button',{name:'关闭编辑器',exact:true}).click();await page.waitForSelector('[data-editor-fixture]',{state:'detached'});cases.push('existing parent close wiring removes editor');
  const before=await page.evaluate(()=>JSON.stringify(window.__fixture.settings.input.outputTarget.oemRearMap));
  await page.locator('.shortcut-toggle').first().click();await page.waitForFunction(()=>window.__fixture.nativeWrites===1);
  assert.equal(await page.evaluate(()=>window.__fixture.writes),1);assert.equal(await page.evaluate(()=>JSON.stringify(window.__fixture.settings.input.outputTarget.oemRearMap)),before);
  assert(!(await page.locator('.status-line').textContent()).includes('保存失败'));cases.push('real browser mixed custom/default save succeeds and retains OEM map');
  await page.reload();await page.waitForSelector('.shortcut-toggle');
  await page.locator('.persona-btn').filter({hasText:'PS5'}).click();await page.waitForSelector('.steamdeck-mouse-bubble',{state:'detached'});
  assert.equal(await page.locator('.mouse-backend-buttons button').nth(1).isDisabled(),false);
  assert.equal(await page.locator('.mouse-backend-buttons button').nth(2).isDisabled(),true);
  cases.push('switching away hides SteamDeck slider and restores non-Steam backend selection');
  assert.deepEqual(errors,[]);
  const report={suite:'Controller shortcut UI browser',passed:cases.length,cases,screenshots,pageErrors:errors,scope:'real Vue DOM and CSS, mocked native/storage and editor body; no hardware'};
  fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
