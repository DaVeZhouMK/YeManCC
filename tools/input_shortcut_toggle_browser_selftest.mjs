// Real shortcut editor + schema rendered in Chromium; native storage/recorder mocked.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const req = createRequire(path.join(root, 'package.json'));
const { build } = req('esbuild'), { parse, compileScript, compileStyle } = req('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_input-shortcut-test.cjs'))('playwright');
const out = path.resolve(process.env.YMCC_SHORTCUT_BROWSER_OUT || path.join(root, '../../Build/Validation/input-shortcut-toggles-20261006'));
fs.mkdirSync(out, { recursive: true });
const styles = [], errors = [], cases = [], screenshots = [];
const mocks = {
  '@/bridge/ipc': "export const isNativeRuntime=true;export function on(){return ()=>{}}",
  '@/bridge/yeman': "export async function summonGet(){return structuredClone(window.__fixture.gamepad)};export async function summonSet(value){Object.assign(window.__fixture.gamepad,value);return summonGet()};export async function oemKeysGet(){return {supported:false,familyId:'fixture',keys:[]}};export async function setShortcutRecording(active){window.__fixture.recordings.push(active);return {active}}",
  '@/bridge/settingsRepository': "import {evaluateInputCasWrite} from './src/bridge/settingsRepository.ts';export async function loadSettings(){return structuredClone(window.__fixture.settings)};export async function compareAndSwapInputSettings(revision,patch){const f=window.__fixture;f.patches.push(JSON.parse(JSON.stringify(patch)));const result=evaluateInputCasWrite(f.settings.input,revision,patch,!f.failSave);if(result.ok)f.settings.input=result.value;return result}",
  '@/bridge/api': "export const fs={};export const settingsStore={}",
  '@/gamepad/focus': "export function focusGamepadElement(n){n?.focus()}",
  '@/components/GamepadVisualizer.vue': "import {h} from 'vue';export default {render(){return h('div',{'data-fixture-mirror':true},'输入镜像')}}",
  // Native dropdown mechanics are independently covered; expose its real
  // editor props/options through a semantic select for deterministic choices.
  '@/components/Dropdown.vue': "import {h} from 'vue';export default {props:['modelValue','options','disabled'],emits:['update:modelValue'],setup(p,{emit}){return ()=>h('select',{value:p.modelValue,disabled:p.disabled,onChange:e=>emit('update:modelValue',e.target.value)},p.options.map(o=>h('option',{value:o.value},o.label)))}}",
};
const result = await build({stdin:{resolveDir:root,loader:'ts',contents: `
  import {createApp} from 'vue';import Page from './src/views/ControllerShortcutEditorView.vue';
  import {normalizeSettings} from './src/bridge/settingsRepository.ts';
  import {NATIVE_DEFAULT_SHORTCUTS,newControllerShortcutRule,defaultParameters} from './src/bridge/controllerShortcutRules.ts';
  const initial=normalizeSettings({startupDesired:{virtualGamepadPersona:'disabled',gyroPreset:'off'},input:{outputTarget:{persona:'elite',buttonMappingEnabled:true,gyroEnabled:true,futureTarget:{keep:true}},gyroMotion:{enabled:true,preset:'racing',presets:{fps:{gyroMultiplier:1.7}}},buttonMapping:{rules:{futureMapping:{keep:true},shortcutRules:[{...newControllerShortcutRule('user-switch','input.gyroToggle'),params:{preset:'fps'},futureRule:{keep:true}},...['app.exit','performance.autoAdjust','performance.editMode','performance.switchMode'].map(id=>({...newControllerShortcutRule('hidden-'+id,id),inputs:[],params:{...defaultParameters(id),legacyMarker:id}}))]}}}});
  window.__fixture={settings:initial,gamepad:Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map(d=>[d.defaultKey,false])),patches:[],closes:0,recordings:[]};
  let app;window.__mount=()=>{app=createApp(Page,{onClose:()=>window.__fixture.closes++});app.mount('#app')};window.__remount=()=>{app.unmount();window.__mount()};window.__mount();
`},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'shortcut-editor-fixture',setup(b){
  b.onResolve({filter:/.*/},args=>{
    if(args.path==='./api'&&args.importer.endsWith('settingsRepository.ts'))return {path:'@/bridge/api',namespace:'fixture'};
    if(args.path in mocks)return {path:args.path,namespace:'fixture'};
  });
  b.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
  b.onLoad({filter:/\.vue$/},args=>{
    const {descriptor,errors}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});assert.deepEqual(errors,[]);
    const id='data-v-'+createHash('sha256').update(args.path).digest('hex').slice(0,8);
    const compiled=compileScript(descriptor,{id,inlineTemplate:true});
    for(const style of descriptor.styles){const css=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.deepEqual(css.errors,[]);styles.push(css.code);}
    return {contents:compiled.content.replace('export default','const _component =')+'\n_component.__scopeId='+JSON.stringify(id)+';export default _component;',loader:'ts',resolveDir:path.dirname(args.path)};
  });
}}]});
const js=result.outputFiles[0].text;
const css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html='<!doctype html><meta charset="utf-8"><title>Input shortcut editor validation</title><style>'+css+'\nbody{margin:0;background:var(--bg-solid);font-family:system-ui}select{max-width:100%;background:var(--bg-panel);color:var(--text);border:1px solid var(--accent);border-radius:6px;min-height:30px}</style><main id="app"></main><script src="/fixture.js"></script>';
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');response.end(request.url==='/fixture.js'?js:html)});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try{
  browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page=await browser.newPage({viewport:{width:1280,height:800}});page.on('pageerror',error=>errors.push(error.message));
  await page.goto('http://127.0.0.1:'+server.address().port);
  await page.waitForFunction(()=>document.querySelector('.action-trigger-grid select')?.value==='input.gyroToggle');
  const original=await page.evaluate(()=>structuredClone(window.__fixture.settings));
  const action=page.locator('.action-trigger-grid select').first();
  for(const id of ['input.gyroToggle','input.virtualGamepadToggle']){
    const option=action.locator('option[value="'+id+'"]');assert.equal(await option.count(),1);assert(!(await option.textContent()).includes('待接管'));
  }
  cases.push('Both runtime toggle actions are selectable and available in the real editor');
  const hiddenIds=['app.exit','performance.autoAdjust','performance.editMode','performance.switchMode'];
  for(const id of hiddenIds)assert.equal(await action.locator('option[value="'+id+'"]').count(),0);
  assert.equal((await page.locator('.rule-list .rule-row').count()),1);
  assert(!(await page.locator('.editor-layout').textContent()).includes('待接管'));
  const hiddenBefore=await page.evaluate(()=>window.__fixture.settings.input.buttonMapping.rules.shortcutRules.filter(r=>r.id.startsWith('rule-hidden-')));
  assert.equal(hiddenBefore.length,4);
  cases.push('All pending actions are absent from selector/list, retained only as inaccessible saved rules');
  const parameter=page.locator('.parameter-grid select');
  assert.deepEqual(await parameter.locator('option').allTextContents(),['FPS射击','赛车','自定义','Steam']);
  assert.equal(await parameter.inputValue(),'fps');assert((await page.locator('.rule-editor-card').textContent()).includes('不记忆'));
  cases.push('Gyro parameter has exactly four presets and displays runtime-only/global-and-dedicated explanation');
  assert.equal(await page.evaluate(()=>window.__fixture.patches.length),0);
  cases.push('Opening editor/reading presets never saves or toggles input');
  for(const preset of ['fps','racing','custom','steam']){
    await parameter.selectOption(preset);assert.equal(await parameter.inputValue(),preset);
  }
  await parameter.selectOption('racing');await page.getByRole('button',{name:'应用规则',exact:true}).click();

  await page.waitForFunction(()=>window.__fixture.patches.length===1);
  const saved=await page.evaluate(()=>window.__fixture.settings);
  assert.deepEqual(saved.input.outputTarget,original.input.outputTarget);assert.deepEqual(saved.input.gyroMotion,original.input.gyroMotion);assert.deepEqual(saved.startupDesired,original.startupDesired);
  assert.equal(saved.input.buttonMapping.rules.shortcutRules[0].params.preset,'racing');
  assert.deepEqual(saved.input.buttonMapping.rules.futureMapping,{keep:true});
  cases.push('Saving gyro shortcut stores only rule/preset and preserves output, gyro, startup and unknown mapping fields');
  await action.selectOption('input.virtualGamepadToggle');
  assert.deepEqual(await parameter.locator('option').allTextContents(),['SteamDeck','PS5','Xbox']);
  assert.equal(await parameter.inputValue(),'steamdeck');
  for(const persona of ['steamdeck','dualsense-edge','elite']){await parameter.selectOption(persona);assert.equal(await parameter.inputValue(),persona);}
  cases.push('Virtual toggle exposes existing three enable personas without resurrecting hidden legacy presets');
  await parameter.selectOption('dualsense-edge');await page.getByRole('button',{name:'应用规则',exact:true}).click();
  await page.waitForFunction(()=>window.__fixture.patches.length===2);
  assert.deepEqual(await page.evaluate(()=>window.__fixture.settings.input.outputTarget),original.input.outputTarget);
  assert.equal(await page.evaluate(()=>window.__fixture.settings.input.buttonMapping.rules.shortcutRules[0].params.preset),'dualsense-edge');
  await page.evaluate(()=>window.__remount());await page.waitForFunction(()=>document.querySelector('.action-trigger-grid select')?.value==='input.virtualGamepadToggle');
  assert.equal(await parameter.inputValue(),'dualsense-edge');
  cases.push('Saved virtual rule/preset reloads faithfully without changing active persona');
  await page.evaluate(()=>{const f=window.__fixture;f.settings.input.gameOverride={identity:'dedicated-game',sessionId:'fixture-session',outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true},gyroMotion:{enabled:true,preset:'steam'}};window.__remount()});
  await page.waitForFunction(()=>document.querySelector('.action-trigger-grid select')?.value==='input.virtualGamepadToggle');
  const overlayBefore=await page.evaluate(()=>JSON.stringify(window.__fixture.settings.input.gameOverride));
  assert.equal(await action.isDisabled(),false);await action.selectOption('input.gyroToggle');await parameter.selectOption('custom');await page.getByRole('button',{name:'应用规则',exact:true}).click();
  await page.waitForFunction(()=>window.__fixture.patches.length===3);
  assert.equal(await page.evaluate(()=>JSON.stringify(window.__fixture.settings.input.gameOverride)),overlayBefore);
  assert.deepEqual(await page.evaluate(()=>window.__fixture.settings.input.outputTarget),original.input.outputTarget);
  assert.deepEqual(await page.evaluate(()=>window.__fixture.settings.input.gyroMotion),original.input.gyroMotion);
  cases.push('Dedicated game ownership does not block shortcut editing and saving never rewrites its overlay/base');
  await page.evaluate(()=>window.__fixture.failSave=true);await parameter.selectOption('steam');await page.getByRole('button',{name:'应用规则',exact:true}).click();
  await page.waitForFunction(()=>window.__fixture.patches.length===4);
  assert.equal(await page.evaluate(()=>window.__fixture.settings.input.buttonMapping.rules.shortcutRules[0].params.preset),'custom');
  cases.push('Failed rule save cannot overwrite saved preset or runtime/global/game intent');
  await page.getByRole('button',{name:'键盘 + 鼠标',exact:true}).click();
  await page.getByRole('button',{name:'开始录制',exact:true}).click();
  await page.waitForFunction(()=>window.__fixture.recordings.at(-1)===true);
  await page.keyboard.down('Control');await page.keyboard.down('K');await page.keyboard.up('K');await page.keyboard.up('Control');
  await page.waitForFunction(()=>window.__fixture.recordings.at(-1)===false);
  assert.deepEqual(await page.evaluate(()=>window.__fixture.recordings.slice(-2)),[true,false]);
  assert.equal(await page.evaluate(()=>window.__fixture.patches.length),4);
  cases.push('Native keyboard/mouse recording arms and releases shortcut suppression without saving or executing input intent');
  await page.evaluate(()=>{window.__fixture.failSave=false;window.__remount()});
  await page.waitForFunction(()=>document.querySelector('.action-trigger-grid select')?.value==='input.gyroToggle');
  await action.selectOption('input.frontendButton');
  assert.deepEqual(await parameter.locator('option').allTextContents(),['Steam','PS','Xbox','Gamebar【Win+G】']);
  assert.equal(await parameter.inputValue(),'steam');
  for(const button of ['steam','ps','xbox','gamebar']){await parameter.selectOption(button);assert.equal(await parameter.inputValue(),button)}
  assert.equal(await page.evaluate(()=>window.__fixture.patches.length),4);
  await page.getByRole('button',{name:'应用规则',exact:true}).click();
  await page.waitForFunction(()=>window.__fixture.patches.length===5);
  assert.equal(await page.evaluate(()=>window.__fixture.settings.input.buttonMapping.rules.shortcutRules[0].params.button),'gamebar');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.settings.input.buttonMapping.rules.shortcutRules.filter(r=>r.id.startsWith('rule-hidden-'))),hiddenBefore);
  assert.deepEqual(await page.evaluate(()=>window.__fixture.settings.input.outputTarget),original.input.outputTarget);
  assert.deepEqual(await page.evaluate(()=>window.__fixture.settings.input.gyroMotion),original.input.gyroMotion);
  cases.push('Frontend button exposes all four parameters; selecting never writes and saving retains hidden rules/global input intent');
  await page.evaluate(()=>window.__remount());
  await page.waitForFunction(()=>document.querySelector('.action-trigger-grid select')?.value==='input.frontendButton');
  assert.equal(await parameter.inputValue(),'gamebar');
  cases.push('Frontend button parameter round-trips after real editor reload');
  await page.getByRole('button',{name:'恢复默认快捷键',exact:true}).click();
  assert.equal(await page.evaluate(()=>window.__fixture.patches.length),5);
  await page.getByRole('button',{name:'应用规则',exact:true}).click();
  await page.waitForFunction(()=>window.__fixture.patches.length===6);
  assert.deepEqual(await page.evaluate(()=>window.__fixture.settings.input.buttonMapping.rules.shortcutRules.filter(r=>r.id.startsWith('rule-hidden-'))),hiddenBefore);
  cases.push('Restore visible defaults preserves inaccessible legacy actions and waits for explicit Apply');
  await page.evaluate(()=>{const f=window.__fixture;f.gamepad=Object.fromEntries(Object.keys(f.gamepad).map(k=>[k,false]));f.settings.input.buttonMapping.rules.shortcutRules=f.settings.input.buttonMapping.rules.shortcutRules.filter(r=>r.id.startsWith('rule-hidden-'));window.__remount()});
  await page.waitForFunction(()=>document.querySelector('.empty-editor')!==null);
  assert.equal(await page.locator('.rule-row').count(),0);
  assert((await page.locator('.rule-list-card').count())===0 || !(await page.locator('.rule-list-card').textContent()).includes('退出软件'));
  await page.getByRole('button',{name:'+ 新建规则',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('.action-trigger-grid select')!==null);
  assert.equal(await page.evaluate(()=>window.__fixture.patches.length),6);
  cases.push('Hidden-only saved rules show empty editor; user can create visible rule without exposing or deleting hidden data');
  await action.selectOption('input.frontendButton');await parameter.selectOption('xbox');
  for(const [width,height] of [[1280,800],[580,800],[420,800]]){
    await page.setViewportSize({width,height});await page.waitForTimeout(50);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
    cases.push('Editor actions/preset hint have no horizontal overflow '+width+'x'+height);
    const image=path.join(out,'shortcut-gyro-editor-'+width+'x'+height+'.png');
    // Chromium can briefly reject capture after Teleport/viewport resizing.
    for(let attempt=0;attempt<3;attempt++){try{await page.screenshot({path:image});break}catch(error){if(attempt===2)throw error;await page.waitForTimeout(150)}}
    screenshots.push(image);
  }
  assert.deepEqual(errors,[]);
  const report={suite:'Input runtime shortcut editor browser',passed:cases.length,cases,screenshots,pageErrors:errors,scope:'Real editor/schema/Vue; low-level native/CAS and Dropdown rendering mocked; no user files or devices touched'};
  fs.writeFileSync(path.join(out,'editor-browser-results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
