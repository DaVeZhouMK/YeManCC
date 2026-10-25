// Real Vue controller UI + real settings repository; storage and IPC stay in memory.
// Run: node tools/external_controller_priority_ui_browser_selftest.mjs
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';

const root = process.env.YMCC_CONTROLLER_SOURCE_ROOT;
assert(root && path.isAbsolute(root), 'Explicit current source root required');
const require = createRequire(path.join(root, 'package.json'));
const { build } = require('esbuild');
const { parse, compileScript, compileStyle } = require('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_external-priority-test.cjs'))('playwright');
const styles = [];
const mocks = {
  '@/bridge/api': `
    const clone = v => structuredClone(v);
    export const shell = { open: async () => { throw new Error('External launch forbidden'); } };
    export const fs = {
      exists: async () => false,
      readTextFile: async () => {
        const f = window.__fixture;
        if (f.throwRead) throw new Error('Read failed');
        return JSON.stringify(f.settings);
      },
    };
    export const settingsStore = {
      read: async () => {
        const f = window.__fixture;
        if (f.throwRead) throw new Error('Read failed');
        const content = JSON.stringify(f.settings);
        if (f.delayRead) {
          f.delayRead = false;
          await new Promise(resolve => f.releaseRead = resolve);
        }
        return { content, stamp: String(JSON.parse(content).input.revision), unchanged: false };
      },
      write: async (_path, content, options) => {
        const f = window.__fixture;
        f.writes.push({ next: JSON.parse(content), expected: options.expectedInputRevision });
        if (f.delayWrite) await new Promise(resolve => f.releaseWrite = resolve);
        if (f.failWrite) return { ok: false };
        if (f.conflicts > 0) {
          f.conflicts--;
          f.settings.input.revision++;
          f.settings.input.outputTarget.persona = 'elite';
          f.settings.input.ownership.concurrentField = { preserved: true };
          return { ok: false, reason: 'revision-conflict', content: JSON.stringify(f.settings) };
        }
        if (f.settings.input.revision !== options.expectedInputRevision) {
          return { ok: false, reason: 'revision-conflict', content: JSON.stringify(f.settings) };
        }
        f.settings = JSON.parse(content);
        const committedContent = JSON.stringify(f.settings);
        if (f.afterCommit) {
          f.settings.input.revision++;
          f.settings.input.ownership.externalControllerPriority = false;
          f.settings.input.outputTarget.persona = 'elite';
        }
        return { ok: true, content: committedContent };
      },
    };`,
  '@/bridge/ipc': `
    export function on(name, cb) {
      const callbacks = window.__fixture.events[name] ??= new Set();
      callbacks.add(cb); return () => callbacks.delete(cb);
    }
    export async function invoke(method) {
      window.__fixture.requests.push(method);
      if (method === 'input.shortcutRuntime.get') return window.__fixture.shortcut ?? { active: false };
      if (method === 'input.shortcutRuntime.clear') { window.__fixture.shortcut = null; return { ok: true }; }
      throw new Error('Unexpected Native request: ' + method);
    }`,
  '@/bridge/gameInputOverride': `
    const callbacks = new Set();
    export function getGameInputOverrideState() { return { locked: !!window.__fixture.settings.input.gameOverride }; }
    export function subscribeGameInputOverrideState(cb) { callbacks.add(cb); return () => callbacks.delete(cb); }
    export function notifyOverride() { for (const cb of callbacks) cb(getGameInputOverrideState()); }`,
  '@/bridge/gameproc': `export async function closeJoyxoffIfRunning() { throw new Error('Process control forbidden'); }`,
};
for (const component of ['SteamDeckMouseSensitivity', 'ScreenTouchpadsSettings', 'ControllerShortcutsCard', 'ControllerFeedbackCapabilitiesCard']) {
  mocks[`@/components/${component}.vue`] = 'export default { render() { return null; } };';
}
mocks['@/views/ControllerShortcutEditorView.vue'] = 'export default { render() { return null; } };';

mocks['@/bridge/performanceSchedule'] = `
 export async function loadGameCustomConfig(){return {schemaVersion:1,entries:{}};}
 export function onGameCustomConfigChanged(){return ()=>{};}
 export function gameProfileInputRevision(){return 0;}
 export async function patchGameCustomProfile(){throw Error('Per-game profile writes outside this suite');}
 export async function removeGameCustomProfile(){throw Error('Per-game profile removal outside this suite');}
 export async function restorePerformanceScheduleIfConfigured(){throw Error('Hardware schedule outside this suite');}
`;
mocks['@/bridge/gamePolicyTarget'] = `
 export async function detectPolicyGame(){return null;}
 export function getPolicyGame(){return null;}
 export function gamePolicyKey(){return '';}
 export function subscribePolicyGameStatus(cb){(window.__fixture.ownerListeners ??= new Set()).add(cb);return()=>window.__fixture.ownerListeners.delete(cb);}
`;
mocks['@/bridge/gamedetect'] = 'export function detectedGameName(){return null;}';
mocks['@/bridge/gameInputOverride'] += '\nexport function buildGameInputOverlay(){throw Error("Per-game overlay outside this suite");}';
mocks['@/bridge/gameInputOverride'] = mocks['@/bridge/gameInputOverride'].replace('for (const cb of callbacks) cb(getGameInputOverrideState());','for (const cb of callbacks) cb(getGameInputOverrideState()); for (const cb of window.__fixture.ownerListeners ?? []) cb();');

const entry = `
  import { createApp } from 'vue';
  import Page from './src/views/ButtonMappingView.vue';
  import { normalizeSettings, evaluateInputCas, evaluateInputCasWrite, compareAndSwapInputSettings,
    setSettingsDirectory, loadSettings, onInputSettingsChanged } from './src/bridge/settingsRepository';
  import { externalControllerPriorityPresentation, virtualOutputPresentation } from './src/bridge/virtualOutputStatus';
  import { notifyOverride } from '@/bridge/gameInputOverride';
  const settings = normalizeSettings({ input: {
    revision: 7,
    outputTarget: { persona: 'steamdeck', buttonMappingEnabled: true, futureTarget: { keep: true } },
    ownership: { mode: 'ymcc-semantic', frontendSummonVirtualControl: true, futureOwnership: { keep: 'yes' } },
    gameOverride: { identity: 'fixture-game', outputTarget: { persona: 'dualsense-edge' }, futureGame: 42 },
  } });
  window.__fixture = { settings, writes: [], requests: [], events: {}, conflicts: 0 };
  setSettingsDirectory('C:/fixture-no-device');
  window.__emit = (name, payload) => { if (name === 'input.shortcutRuntime') window.__fixture.shortcut = payload; for (const cb of window.__fixture.events[name] ?? []) cb(payload); };
  window.__audit = { normalizeSettings, evaluateInputCas, evaluateInputCasWrite, compareAndSwapInputSettings,
    externalControllerPriorityPresentation, virtualOutputPresentation, notifyOverride, loadSettings };
  const app = createApp(Page); app.mount('#app');
  window.__unmount = () => app.unmount();
`;
const built = await build({
  stdin: { contents: entry, loader: 'ts', resolveDir: root },
  bundle: true, write: false, platform: 'browser', format: 'iife',
  alias: { '@': path.join(root, 'src') }, define: { 'process.env.NODE_ENV': '"production"' },
  plugins: [{ name: 'external-priority-memory-fixture', setup(builder) {
    builder.onResolve({ filter: /.*/ }, args => {
      if (args.path in mocks) return { path: args.path, namespace: 'fixture' };
      const provider = {'./performanceSchedule':'@/bridge/performanceSchedule','./gamePolicyTarget':'@/bridge/gamePolicyTarget','./gamedetect':'@/bridge/gamedetect','./gameInputOverride':'@/bridge/gameInputOverride'}[args.path];
      if(provider) return {path:provider,namespace:'fixture'};
      if (args.path === './api' && args.importer.endsWith('settingsRepository.ts')) return { path: '@/bridge/api', namespace: 'fixture' };
    });
    builder.onLoad({ filter: /.*/, namespace: 'fixture' }, args => ({ contents: mocks[args.path], loader: 'ts', resolveDir: root }));
    builder.onLoad({ filter: /\.vue$/ }, args => {
      const { descriptor, errors } = parse(fs.readFileSync(args.path, 'utf8'), { filename: args.path });
      assert.deepEqual(errors, []);
      const id = 'data-v-' + createHash('sha256').update(args.path).digest('hex').slice(0, 8);
      const compiled = compileScript(descriptor, { id, inlineTemplate: true });
      for (const style of descriptor.styles) {
        const result = compileStyle({ source: style.content, filename: args.path, id, scoped: style.scoped });
        assert.deepEqual(result.errors, []); styles.push(result.code);
      }
      return { contents: compiled.content.replace('export default', 'const _component =') + '\n_component.__scopeId=' + JSON.stringify(id) + ';export default _component;', loader: 'ts', resolveDir: path.dirname(args.path) };
    });
  } }],
});
const js = built.outputFiles[0].text;
const css = fs.readFileSync(path.join(root, 'src/styles/tokens.css'), 'utf8') + '\n' + styles.join('\n');
const html = '<!doctype html><meta charset="utf-8"><style>' + css + '\nbody{margin:0;background:var(--bg-solid);color:var(--text);font-family:system-ui}#app{padding:16px;max-width:800px;margin:auto}</style><main id="app"></main><script src="/fixture.js"></script>';
const server = createServer((request, response) => {
  response.setHeader('Content-Type', request.url === '/fixture.js' ? 'text/javascript; charset=utf-8' : 'text/html; charset=utf-8');
  response.end(request.url === '/fixture.js' ? js : html);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const cases = [], errors = [];
let browser;
try {
  browser = await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page=await browser.newPage({viewport:{width:1280,height:720}});
  page.on('pageerror',error=>errors.push(error.message));
  const toggle=page.getByRole('switch',{name:'外置优先',exact:true});
  const idle=()=>page.waitForFunction(()=>{const t=document.querySelector('.controller-unified-header [role=switch]');return t&&!t.disabled;});
  const ready=async()=>{await page.goto('http://127.0.0.1:'+server.address().port);await idle();};
  const saved=()=>page.evaluate(()=>structuredClone(window.__fixture.settings.input));
  const checked=async value=>{try{await page.waitForFunction(value=>document.querySelector('.controller-unified-header [role=switch]')?.getAttribute('aria-checked')===String(value),value,{timeout:3000});}catch(error){console.error('CURRENT_UI_STATE '+JSON.stringify(await page.evaluate(()=>({input:window.__fixture.settings.input,writes:window.__fixture.writes,title:document.querySelector('.controller-unified-header [role=switch]')?.title,checked:document.querySelector('.controller-unified-header [role=switch]')?.getAttribute('aria-checked')}))));assert.equal(await toggle.getAttribute('aria-checked'),String(value));throw error;}assert.equal(await toggle.getAttribute('aria-checked'),String(value));};
  const emit=async value=>{await page.evaluate(value=>window.__emit('gamepad.state',value),value);await page.waitForTimeout(20);};
  const status=page.locator('.controller-channel').first().locator('strong');
  await ready();const baseline=await saved();
  await checked(false);assert.equal(await page.evaluate(()=>window.__fixture.writes.length),0);
  assert.match(await toggle.getAttribute('title'),/状态待确认/);
  cases.push('current header Toggle and real gameInputEditor hydrate without writes; missing native receipt remains pending');
  for(const [width,height]of [[1280,720],[1280,800],[420,700],[900,700]]){
    await page.setViewportSize({width,height});const box=await toggle.boundingBox();assert(box&&box.width>=40&&box.height>=30&&box.x>=0&&box.x+box.width<=width);
  }
  cases.push('actual current Toggle remains fully visible at both handheld sizes and narrow/wide widths');
  await toggle.focus();await page.keyboard.press('Space');await idle();await checked(true);
  const committed=await saved();assert.deepEqual(committed.outputTarget,baseline.outputTarget);assert.deepEqual(committed.gyroMotion,baseline.gyroMotion);assert.deepEqual(committed.gameOverride,baseline.gameOverride);
  const beforeOwnership=structuredClone(baseline.ownership);delete beforeOwnership.externalControllerPriority;
  const afterOwnership=structuredClone(committed.ownership);delete afterOwnership.externalControllerPriority;assert.deepEqual(afterOwnership,beforeOwnership);assert.equal(committed.revision,baseline.revision+1);
  cases.push('keyboard activation persists only narrow priority intent through actual repository CAS and preserves all other domains');
  const effective={enabled:true,state:'active',reason:'fixture',externalPresent:true,builtinHidden:true,virtualSuspended:true};
  const suppressed={state:'external-priority',requestedPersona:'steamdeck',steamRunning:false};
  await emit({externalPriority:effective,virtualOutput:suppressed});assert.match(await toggle.getAttribute('title'),/外接手柄直通/);assert.equal(await status.textContent(),'外接优先暂停');assert.equal((await saved()).outputTarget.persona,'steamdeck');
  cases.push('verified native policy is shown by actual header and ControllerChannelStatus before Steam warnings; persona intent is preserved');
  await emit({externalPriority:{...effective,state:'degraded',builtinHidden:false},virtualOutput:suppressed});assert.match(await toggle.getAttribute('title'),/尚未确认/);assert(!String(await toggle.getAttribute('title')).startsWith('外接手柄直通'));
  await emit({externalPriority:{...effective,externalPresent:false,state:'recovering'},virtualOutput:{...suppressed,state:'recovering',steamRunning:true}});assert.match(await toggle.getAttribute('title'),/正在恢复本机输出/);assert.equal(await status.textContent(),'正在恢复');
  await emit({});assert.match(await toggle.getAttribute('title'),/状态待确认/);
  cases.push('degraded, disconnect recovery and absent policy observations never claim confirmed passthrough');
  await ready();await page.evaluate(()=>window.__fixture.delayWrite=true);await toggle.click();await page.waitForFunction(()=>typeof window.__fixture.releaseWrite==='function');assert.equal(await toggle.isDisabled(),true);await checked(false);assert.match(await toggle.getAttribute('title'),/正在保存/);
  await page.evaluate(()=>document.querySelector('.controller-unified-header [role=switch]').click());assert.equal(await page.evaluate(()=>window.__fixture.writes.length),1);
  await page.evaluate(()=>{window.__fixture.delayWrite=false;window.__fixture.releaseWrite();});await idle();await checked(true);
  cases.push('pending persistence retains old checked value, blocks duplicate writes and shows saving through actual Toggle title');
  await ready();await page.evaluate(()=>window.__fixture.failWrite=true);await toggle.click();await idle();await checked(false);assert.match(await toggle.getAttribute('title'),/保存失败/);assert.equal((await saved()).revision,baseline.revision);
  await page.evaluate(()=>window.__fixture.failWrite=false);await toggle.click();await idle();await checked(true);
  cases.push('durable write failure keeps prior value/revision, announces failure and permits explicit retry');
  await ready();await page.evaluate(()=>window.__fixture.throwRead=true);await toggle.click();await idle();await checked(false);assert.match(await toggle.getAttribute('title'),/保存失败/);await page.evaluate(()=>window.__fixture.throwRead=false);await toggle.click();await idle();await checked(true);
  cases.push('actual repository read exception stays visible and does not leave policy busy');
  await ready();await page.evaluate(()=>{const input=window.__fixture.settings.input;input.revision++;input.outputTarget.persona='elite';input.ownership.foreignUpdate={keep:'external'};});await toggle.click();await idle();await checked(true);
  const rebased=await saved();assert.equal(rebased.outputTarget.persona,'elite');assert.deepEqual(rebased.ownership.foreignUpdate,{keep:'external'});assert.equal(rebased.revision,baseline.revision+2);
  cases.push('stale CAS rebases only priority intent and preserves concurrent persona and ownership');
  await ready();await page.evaluate(()=>window.__fixture.conflicts=1);await toggle.click();await idle();await checked(true);const conflict=await saved();assert.equal(conflict.outputTarget.persona,'elite');assert.deepEqual(conflict.ownership.concurrentField,{preserved:true});assert.equal(await page.evaluate(()=>window.__fixture.writes.length),2);
  cases.push('one commit conflict retries against actual persisted document without replacing unrelated fields');
  await ready();await page.evaluate(()=>window.__fixture.conflicts=2);await toggle.click();await idle();await checked(false);assert.equal(await page.evaluate(()=>window.__fixture.writes.length),2);assert.match(await toggle.getAttribute('title'),/其他操作更新/);await toggle.click();await idle();await checked(true);
  cases.push('repeated commit conflicts stop after one retry and leave the switch usable');
  await ready();await page.evaluate(()=>{const input=window.__fixture.settings.input;input.revision++;input.ownership.externalControllerPriority=true;});await toggle.click();await idle();await checked(true);assert.equal(await page.evaluate(()=>window.__fixture.writes.length),0);
  cases.push('a concurrent write already satisfying priority does not generate a redundant write');
  await ready();await page.evaluate(()=>{window.__fixture.delayRead=true;window.__audit.notifyOverride();});await page.waitForFunction(()=>typeof window.__fixture.releaseRead==='function');await toggle.click();await page.waitForFunction(()=>window.__fixture.settings.input.ownership.externalControllerPriority);await page.evaluate(()=>window.__fixture.releaseRead());await idle();await checked(true);
  cases.push('late real gameInputEditor hydration cannot roll back a newer saved priority');
  await ready();await page.evaluate(()=>window.__fixture.afterCommit=true);await toggle.click();await idle();await checked(false);assert.equal((await saved()).revision,baseline.revision+2);assert.equal((await saved()).outputTarget.persona,'elite');
  await page.evaluate(()=>window.__fixture.afterCommit=false);await toggle.click();await idle();await checked(true);assert.equal((await saved()).outputTarget.persona,'elite');
  cases.push('post-commit concurrent update wins over older successful reply and the next save preserves persona');
  await ready();await page.evaluate(async()=>{await window.__audit.compareAndSwapInputSettings(window.__fixture.settings.input.revision,{ownership:{externalControllerPriority:true}});});await page.waitForFunction(()=>document.querySelector('.controller-unified-header [role=switch]').getAttribute('aria-checked')==='true');
  cases.push('independent repository CAS refreshes actual cached controller page through the existing notification path');
  const observations=await page.evaluate(()=>{const policy=window.__audit.externalControllerPriorityPresentation,base={enabled:true,state:'active',externalPresent:true,builtinHidden:true,virtualSuspended:true};return{valid:policy(base,true),missing:policy(null,true),mismatch:policy({...base,enabled:false},true),unknown:policy({...base,state:'unknown'},true),notHidden:policy({...base,builtinHidden:false},true),notSuspended:policy({...base,virtualSuspended:false},true),holding:policy({...base,state:'holding'},true),suspended:policy({...base,state:'suspended'},true),offPending:policy({...base,enabled:false},false)};});
  assert.equal(observations.valid.tone,'on');for(const name of ['missing','mismatch','unknown','holding','suspended','offPending'])assert.equal(observations[name].tone,'pending');assert.equal(observations.notHidden.tone,'warning');assert.equal(observations.notSuspended.tone,'warning');
  cases.push('actual status consumer requires complete hidden and suspended receipts; lifecycle and unknown states remain pending');
  const normalization=await page.evaluate(()=>[undefined,null,'true',1,false,true].map(value=>{const row=window.__audit.normalizeSettings({input:{ownership:{externalControllerPriority:value,future:{retained:true}}}});return{value:row.input.ownership.externalControllerPriority,retained:row.input.ownership.future.retained};}).concat([null,'invalid',[]].map(ownership=>({value:window.__audit.normalizeSettings({input:{ownership}}).input.ownership.externalControllerPriority,retained:true}))));
  assert.deepEqual(normalization.map(row=>row.value),[false,false,false,false,false,true,false,false,false]);assert(normalization.every(row=>row.retained));
  cases.push('strict original boolean ownership normalization rejects malformed flags and preserves extensions');
  await ready();await page.evaluate(()=>window.__emit('input.shortcutRuntime',{active:true,persona:'dualsense-edge',virtualEnabled:true}));await toggle.click();await idle();await checked(true);assert.equal((await saved()).outputTarget.persona,'steamdeck');assert.equal(await page.locator('.persona-btn.active').textContent(),'PS5');assert.equal(await page.evaluate(()=>window.__fixture.requests.filter(name=>name==='input.shortcutRuntime.clear').length),0);
  cases.push('priority CAS preserves runtime shortcut persona without copying it to persisted target or clearing the runtime');
  await ready();await page.evaluate(()=>{delete window.__fixture.settings.input.gameOverride;window.__fixture.delayWrite=true;});await toggle.click();await page.waitForFunction(()=>typeof window.__fixture.releaseWrite==='function');const ps5=page.getByRole('button',{name:'PS5',exact:true});assert.equal(await ps5.isDisabled(),true);await page.evaluate(()=>{window.__fixture.delayWrite=false;window.__fixture.releaseWrite();});await idle();await checked(true);await ps5.click();await page.waitForFunction(()=>window.__fixture.settings.input.outputTarget.persona==='dualsense-edge');await idle();assert.equal((await saved()).ownership.externalControllerPriority,true);assert.equal(await page.locator('.persona-btn.active').textContent(),'PS5');
  cases.push('manual target selection serializes behind priority persistence through actual gameInputEditor and preserves ownership');
  const presentation=await page.evaluate(()=>{const f=window.__audit.virtualOutputPresentation,paused={state:'external-priority',requestedPersona:'steamdeck',steamRunning:false};return{paused:f(paused,true,'steamdeck'),busy:f(paused,true,'steamdeck',true),mismatch:f(paused,true,'elite')};});assert.equal(presentation.paused.text,'虚拟手柄-外接优先暂停');assert.equal(presentation.busy.tone,'pending');assert.equal(presentation.mismatch.tone,'pending');
  cases.push('real virtual output status retains busy and mismatched-target precedence before paused-source or Steam observations');
  assert.deepEqual(errors,[]);assert((await page.evaluate(()=>window.__fixture.requests)).every(method=>['input.shortcutRuntime.get','input.shortcutRuntime.clear'].includes(method)));
  await page.evaluate(()=>window.__unmount());assert.equal(await page.evaluate(()=>Array.from(window.__fixture.events['gamepad.state']).length),0);
  cases.push('unmount removes actual native state listener and all observed IPC remains inside reviewed memory routes');
  const engine=fs.readFileSync(path.join(root,'src/gamepad/engine.ts'),'utf8');const focusFunction=engine.slice(engine.indexOf('function focusFirst(): boolean {'),engine.indexOf('// A custom dropdown menu'));const focusHookIntegrated=focusFunction.includes('gpInitialFocus')||focusFunction.includes('data-gp-initial-focus');
  console.log(JSON.stringify({suite:'External controller priority UI',fixtureSchemaVersion:2,legacyLayoutCompatibility:'INCOMPATIBLE_UI_STRUCTURE_RETAINED_FAILURE',passed:cases.length,cases,focusHookIntegrated,scope:'Actual current ButtonMappingView, Toggle, ControllerChannelStatus, settingsRepository and gameInputEditor. Native transport, storage and external game provider are inert; full gamepad engine is not executed.'},null,2));
} finally {if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
