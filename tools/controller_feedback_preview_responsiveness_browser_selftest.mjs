// Actual feedback/Slider and GamepadVisualizer consumers. All IPC/settings stay in memory.
// Usage: node tools/controller_feedback_preview_responsiveness_browser_selftest.mjs <fresh-evidence-dir> [--baseline]
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createServer } from 'node:http';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
assert(process.argv[2], 'Pass a fresh evidence directory');
const out = path.resolve(process.argv[2]), baseline = process.argv.includes('--baseline');
const sourceOverrides = new Map();
for (const [option, relative] of [['--card-source', 'src/components/ControllerFeedbackCapabilitiesCard.vue'], ['--artwork-source', 'src/components/ControllerVectorArtwork.ts']]) {
  const index = process.argv.indexOf(option);
  if (index >= 0) { assert(process.argv[index + 1], option + ' needs a path'); sourceOverrides.set(path.join(repo, relative), path.resolve(process.argv[index + 1])); }
}
assert(!fs.existsSync(out), 'Retain prior results; use a fresh directory');
assert(!out.startsWith(repo + path.sep), 'Keep evidence outside source');
fs.mkdirSync(out, { recursive: true });
const req = createRequire(path.join(repo, 'package.json'));
const { build, stop } = req('esbuild');
const { parse, compileScript, compileStyle } = req('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_feedback-preview.cjs'))('playwright');
const styles = [], errors = [], checks = [], measurements = [], sources = new Map();
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function source(file) {
  file = sourceOverrides.get(file) || file;
  const bytes = fs.readFileSync(file);
  sources.set(file, sha(bytes));
  return bytes.toString('utf8');
}
source(fileURLToPath(import.meta.url));
const ipc = `const listeners=new Map();
export function on(name,fn){let set=listeners.get(name);if(!set)listeners.set(name,set=new Set());set.add(fn);return()=>set.delete(fn)};
window.__emit=(name,state)=>{for(const fn of listeners.get(name)||[])fn(state)};
window.__listenerCount=()=>[...listeners.values()].reduce((n,set)=>n+set.size,0);
export async function invoke(cmd,args={}){window.f.calls.push({cmd,args:structuredClone(args)});
if(cmd==='gamepad.feedbackCapabilities')return structuredClone(window.f.caps);
if(cmd==='gamepad.vibrate'){window.f.vibrations.push(args.strength);return {ok:true}};
if(cmd==='gamepad.setLed')throw Error('Unexpected hardware lighting request');
if(cmd==='clipboard.writeText')return {};throw Error('Unexpected IPC: '+cmd)};
export function emitNativeEvent(name,args){window.f.calls.push({cmd:name,args})};`;
const mocks = {
  '@/bridge/ipc': ipc,
  '@/router': `export const ROUTES=[{path:'/button-mapping',title:'Controller'}];`,
  '@/bridge/performanceSchedule': `export async function loadPerformanceSchedule(){return {enabled:false}};`,
  '@/bridge/yeman': `export async function summonGet(){return {enabled:true}};`,
  '@/robust/frontendDiagnostics': `export function reportFrontendTrace(){};`,
  '@/bridge/settingsRepository': `export async function loadSettings(){return structuredClone(window.f.settings)};
export async function saveSettingsSection(section,value){window.f.saved.push({section,value:JSON.parse(JSON.stringify(value))});window.f.settings[section]=JSON.parse(JSON.stringify(value))};`,
};
const entry = `import {createApp,h,ref,nextTick,KeepAlive} from 'vue';
import Card from './src/components/ControllerFeedbackCapabilitiesCard.vue';
import Visualizer from './src/components/GamepadVisualizer.vue';
import Slider from './src/components/Slider.vue';
import {OEM_EMPTY_SNAPSHOT} from './src/bridge/controllerShortcutRules';
import {CONTROLLER_DIAGRAMS} from './src/bridge/controllerDeviceDiagram';
import {EDIT_REPEAT_BASE_MS} from './src/gamepad/sliderRepeat';
import {responsivenessAudit} from './src/gamepad/engine';window.__engine=responsivenessAudit;
window.__nodeReads=0;
// Instrument the actual declarative model in memory; count renderer reads without editing product sources.
function count(nodes){for(const node of nodes){const attrs=node.attrs;Object.defineProperty(node,'attrs',{get(){window.__nodeReads++;return attrs}});count(node.children||[])}}
for(const model of Object.values(CONTROLLER_DIAGRAMS))count(model.nodes);
let app;window.__tick=nextTick;window.__tdpRepeat=EDIT_REPEAT_BASE_MS;
window.unmount=async()=>{app?.unmount();app=null;await nextTick()};
window.mount=async(mode,persona='steamdeck',keepAlive=false)=>{await window.unmount();
window.f={calls:[],vibrations:[],saved:[],caps:{ledAvailable:false,brightnessAvailable:false,ledEffects:[],vibrationAvailable:true,physicalPrimarySlot:0},settings:{gamepad:{feedback:{vibrationStrength:30,lighting:{enabled:false,off:false,mode:'solid',presetId:'',color:'#2ea6ff',color2:'#9d7cff',speed:55,brightness:65}}}}};
const shown=ref(true),reference=ref(30),highlight=ref([]),oem=ref(OEM_EMPTY_SNAPSHOT);window.highlight=async v=>{highlight.value=v;await nextTick()};window.catalog=async v=>{oem.value=v;await nextTick()};window.show=async value=>{shown.value=value;await nextTick()};
app=createApp({setup(){return()=>{const card=()=>h(Card);return mode==='card'?(keepAlive?h(KeepAlive,null,()=>shown.value?card():null):card()):mode==='preview'?h(Visualizer,{settings:{},persona,showTestMode:false,minimumPressFeedback:true,matchSteamDeckHeight:true,highlightButtons:highlight.value,oemKeys:oem.value}):h(Slider,{modelValue:reference.value,'onUpdate:modelValue':v=>reference.value=v,min:0,max:100,step:1,label:'TDP reference',accelerate:true,repeatIntervalMs:EDIT_REPEAT_BASE_MS})}}});
app.mount('#app');await nextTick();await nextTick()};
window.mount('card');`;
const plugin = { name: 'feedback-preview-fixture', setup(b) {
  b.onResolve({ filter: /.*/ }, a => {
    if (a.path === './ipc' && a.importer.endsWith('api.ts')) return { path: '@/bridge/ipc', namespace: 'memory' };
    if (a.path in mocks) return { path: a.path, namespace: 'memory' };
  });
  b.onLoad({ filter: /.*/, namespace: 'memory' }, a => ({ contents: mocks[a.path], loader: 'ts', resolveDir: repo }));
  b.onLoad({ filter: /\.vue$/ }, a => {
    const { descriptor, errors } = parse(source(a.path), { filename: a.path });
    assert.deepEqual(errors, []);
    const id = 'data-v-' + sha(a.path).slice(0, 8);
    const script = compileScript(descriptor, { id, inlineTemplate: true });
    for (const style of descriptor.styles) {
      const css = compileStyle({ source: style.content, filename: a.path, id, scoped: style.scoped });
      assert.deepEqual(css.errors, []); styles.push(css.code);
    }
    return { contents: script.content.replace('export default', 'const Component =') + '\nComponent.__scopeId=' + JSON.stringify(id) + ';export default Component;', loader: 'ts', resolveDir: path.dirname(a.path) };
  });
  b.onLoad({ filter: /\.(ts|json)$/ }, a => {
    let contents=source(a.path);
    if(a.path.replaceAll('\\','/').endsWith('/src/gamepad/engine.ts')) contents+=`\nconst auditOptions={router:{currentRoute:{value:{path:'/button-mapping'}},push:async()=>{}}} as any;
export const responsivenessAudit={command:(action:NativeUiAction)=>dispatchNativeUiAction(auditOptions,action),editing:()=>sliderEditMode,reset:()=>{sliderEditMode=false;resetSliderCadence()},repeat:(now:number,right:boolean)=>microAdjustSlider(now,!right,right,false,false)};`;
    return {contents,loader:a.path.endsWith('.json')?'json':'ts',resolveDir:path.dirname(a.path)};
  });
} };
let browser, server, failure;
function check(value, name, detail) { checks.push({ name, pass: Boolean(value), detail }); }
try {
  const bundle = await build({ stdin: { contents: entry, resolveDir: repo, loader: 'ts' }, bundle: true, write: false, platform: 'browser', format: 'iife', loader: { '.png': 'dataurl' }, alias: { '@': path.join(repo, 'src') }, define: { 'process.env.NODE_ENV': '"production"' }, plugins: [plugin] });
  const tokenCss = source(path.join(repo, 'src/styles/tokens.css'));
  const html = '<!doctype html><meta charset="utf-8"><style>' + tokenCss + styles.join('\n') + 'body{background:#101823;color:#e8edf3;font:14px system-ui;margin:24px}#app{max-width:900px;margin:auto}.card{background:#172333;border:1px solid #435163;padding:20px;border-radius:12px}</style><main id="app"></main><script src="/fixture.js"></script>';
  fs.writeFileSync(path.join(out, 'fixture.js'), bundle.outputFiles[0].contents);
  fs.writeFileSync(path.join(out, 'index.html'), html);
  server = createServer((request, response) => {
    response.setHeader('Content-Type', request.url === '/fixture.js' ? 'text/javascript; charset=utf-8' : 'text/html; charset=utf-8');
    response.end(request.url === '/fixture.js' ? bundle.outputFiles[0].contents : html);
  });
  for (let attempt = 0; ; attempt++) {
    try { await new Promise((resolve, reject) => { server.once('error', reject); server.listen(crypto.randomInt(49152, 65536), '127.0.0.1', resolve); }); break; }
    catch (error) { if (attempt >= 31 || !['EADDRINUSE', 'EACCES'].includes(error.code)) throw error; }
  }
  browser = await chromium.launch({ headless: true, executablePath: process.env.YMCC_UI_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('http://127.0.0.1:' + server.address().port);
  async function mount(mode, persona, keepAlive) {
    await page.evaluate(async args => window.mount(...args), [mode, persona, keepAlive]);
    if (mode === 'card') await page.getByRole('slider', { name: '震动强度', exact: true }).waitFor({ state: 'visible' });
  }
  for (const height of [720, 800]) {
    await page.setViewportSize({ width: 1280, height });
    await mount('reference');
    const reference = page.getByRole('slider');
    await reference.focus(); await page.keyboard.down('ArrowRight'); await page.waitForTimeout(1000); await page.keyboard.up('ArrowRight');
    const referenceDelta = Number(await reference.inputValue()) - 30;
    await mount('card');
    const slider = page.getByRole('slider', { name: '震动强度', exact: true });
    await page.waitForFunction(() => !document.querySelector('.rumble-row input').disabled && document.querySelector('.rumble-row input').value === '30');
    check(await slider.getAttribute('data-gp-repeat-interval-ms') === String(await page.evaluate(() => window.__tdpRepeat)), 'rumble uses the TDP repeat interval @' + height);
    await slider.evaluate(input => {
      window.__strengthKeyup = null;
      // This listener is registered after the production Slider's keyup handler.
      // Read the preview and the actual repository commit from this same keyup turn.
      input.addEventListener('keyup', async event => {
        const valueDuringKeyup = Number(input.value), at = performance.now();
        await window.__tick();
        window.__strengthKeyup = { key: event.key, at, valueDuringKeyup, previewValue: Number(input.value), commitStrength: window.f.saved.at(-1)?.value.feedback.vibrationStrength, commitCount: window.f.saved.length };
      }, { once: true });
    });
    await slider.focus(); await page.keyboard.down('ArrowRight'); await page.waitForTimeout(1000);
    const heldValue = Number(await slider.inputValue());
    const writesDuringHold = await page.evaluate(() => window.f.saved.length);
    await page.keyboard.up('ArrowRight');
    await page.waitForFunction(() => window.__strengthKeyup !== null);
    const release = await page.evaluate(() => ({ value: Number(document.querySelector('.rumble-row input').value), saved: structuredClone(window.f.saved), keyup: structuredClone(window.__strengthKeyup) }));
    const releasedValue = release.value, saved = release.saved;
    check(Math.abs(heldValue - 30 - referenceDelta) <= 3, 'one-second rumble hold follows TDP cadence @' + height, { referenceDelta, rumbleDelta: heldValue - 30 });
    check(writesDuringHold === 0 && saved.length === 1 && saved[0].value.feedback.vibrationStrength === releasedValue && release.keyup.previewValue === releasedValue && release.keyup.commitStrength === releasedValue && release.keyup.commitCount === 1, 'hold previews immediately and persists once on release @' + height, { writesDuringHold, writesAfterRelease: saved.length, heldValue, releasedValue, keyup: release.keyup });
    await page.waitForTimeout(150);
    const stableRelease = await page.evaluate(() => ({ value: Number(document.querySelector('.rumble-row input').value), commitStrength: window.f.saved.at(-1)?.value.feedback.vibrationStrength, commitCount: window.f.saved.length }));
    check(stableRelease.value === releasedValue && stableRelease.commitStrength === release.keyup.commitStrength && stableRelease.commitCount === release.keyup.commitCount, 'release stops the repeat timer @' + height, { releasedValue, keyup: release.keyup, stableRelease });
    await slider.evaluate(el => { el.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 7 })); for (let n = 31; n <= 90; n++) { el.value = String(n); el.dispatchEvent(new Event('input', { bubbles: true })); } });
    check(await page.evaluate(() => window.f.saved.length) === saved.length, '60 pointer samples do not queue settings writes @' + height);
    await slider.evaluate(el => { el.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 7 })); el.dispatchEvent(new Event('change', { bubbles: true })); });
    const pointerSaved = await page.evaluate(() => window.f.saved);
    check(pointerSaved.length === saved.length + 1 && pointerSaved.at(-1).value.feedback.vibrationStrength === 90, 'pointer release commits final value once @' + height, { writes: pointerSaved.length });
    await slider.evaluate(async el => { el.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 8 })); el.value = '15'; el.dispatchEvent(new Event('input', { bubbles: true })); await window.__tick(); el.dispatchEvent(new PointerEvent('pointercancel', { bubbles: true, pointerId: 8 })); await window.__tick(); });
    check(await slider.inputValue() === '90' && await page.evaluate(() => window.f.saved.length) === pointerSaved.length, 'pointer cancellation restores the committed strength @' + height);
    await page.getByRole('button', { name: '立即震动', exact: true }).click(); await page.waitForTimeout(180);
    check(JSON.stringify(await page.evaluate(() => window.f.vibrations)) === '[90,90]', 'explicit test retains both pulses at final strength @' + height);
    await page.screenshot({ path: path.join(out, 'rumble-' + height + '.png') });
    await mount('card');
    const gamepadSlider = page.getByRole('slider', { name: '震动强度', exact: true });
    await gamepadSlider.focus();
    await page.evaluate(async () => {
      window.__engine.reset(); window.__engine.command('nav-right'); await window.__tick();
    });
    check(await gamepadSlider.inputValue() === '30', 'gamepad focus alone does not edit @' + height);
    await gamepadSlider.focus();
    await page.evaluate(async () => {
      window.__engine.command('confirm'); window.__engine.command('slider-increase'); await window.__tick();
    });
    check(await gamepadSlider.inputValue() === '31' && await page.evaluate(() => window.f.saved.length) === 0, 'production engine A arms one precise unsaved step @' + height);
    await page.evaluate(async () => { window.__engine.command('back'); await window.__tick(); });
    check(await gamepadSlider.inputValue() === '30' && await page.evaluate(() => window.f.saved.length) === 0, 'production engine B cancels the strength draft @' + height);
    await gamepadSlider.focus();
    await page.evaluate(async () => {
      window.__engine.command('confirm'); window.__engine.command('slider-increase'); await window.__tick();
      window.__engine.command('confirm'); await window.__tick();
    });
    check(await gamepadSlider.inputValue() === '31' && await page.evaluate(() => window.f.saved.length === 1 && window.f.saved[0].value.feedback.vibrationStrength === 31), 'production engine A commits the final strength once @' + height);
    await mount('card', 'steamdeck', true);
    const cached = page.getByRole('slider', { name: '震动强度', exact: true });
    await cached.focus(); await page.keyboard.down('ArrowRight'); await page.waitForTimeout(120); await page.evaluate(() => window.show(false));
    const before = await page.evaluate(() => window.f.saved.length); await page.waitForTimeout(150); await page.keyboard.up('ArrowRight');
    check(await page.evaluate(() => window.f.saved.length) === before, 'deactivation cancels repeat without late writes @' + height);
    check(await page.evaluate(() => window.f.calls.filter(c => c.cmd === 'gamepad.slider-edit').at(-1)?.args.active === false), 'deactivation releases the native slider cadence hint @' + height);
    await page.evaluate(() => window.show(true)); await cached.focus();
    await page.evaluate(async () => { window.__engine.reset(); window.__engine.command('confirm'); window.__engine.command('slider-increase'); await window.__tick(); await window.show(false); });
    check(await page.evaluate(() => !window.__engine.editing() && window.f.saved.length === 0 && window.f.calls.filter(c => c.cmd === 'gamepad.slider-edit').at(-1)?.args.active === false), 'cached page cancels engine ownership and draft @' + height);
    await mount('card'); await gamepadSlider.focus();
    await page.evaluate(async () => { window.__engine.reset(); window.__engine.command('confirm'); window.__engine.command('slider-increase'); await window.__tick(); await window.unmount(); });
    check(await page.evaluate(() => !window.__engine.editing() && window.f.saved.length === 0 && window.f.calls.filter(c => c.cmd === 'gamepad.slider-edit').at(-1)?.args.active === false), 'unmount releases engine ownership without committing @' + height);
  }
  for (const persona of ['steamdeck', 'dualsense-edge', 'elite']) {
    await mount('preview', persona);
    const result = await page.evaluate(async persona => {
      const inputs = [], frames = 120;
      const packet = axes => ({ connected: true, name: 'memory physical source', buttons: Array(18).fill(false), axes });
      window.__emit('gamepad.state', packet([0, 0, 0, 0])); await window.__tick(); window.__nodeReads = 0;
      const start = performance.now();
      for (let i = 0; i < frames; i++) {
        const axes = [Math.sin(i * .13), Math.cos(i * .13), -Math.sin(i * .13), -Math.cos(i * .13)];
        const at = performance.now(); window.__emit('gamepad.state', packet(axes)); await window.__tick();
        inputs.push(performance.now() - at);
      }
      const elapsed = performance.now() - start, nodeReads = window.__nodeReads;
      const axes = [.123456789, -.234567891, -.345678912, .456789123];
      window.__emit('gamepad.state', packet(axes)); await window.__tick();
      const coords = () => [...document.querySelectorAll('[data-stick]')].map(n => { const m = n.transform.baseVal.consolidate()?.matrix; return { stick: +n.dataset.stick, x: m?.e, y: m?.f }; }).sort((a, b) => a.stick - b.stick);
      const drawn = coords();
      window.dispatchEvent(new CustomEvent('ipc:window.hidden'));
      window.__emit('gamepad.state', packet([1, 1, -1, -1])); await window.__tick(); const hidden = coords();
      window.dispatchEvent(new CustomEvent('ipc:window.shown')); await window.__tick(); const resumed = coords();
      inputs.sort((a, b) => a - b);
      return { persona, frames, elapsedMs: elapsed, nodeReads, p95ConsumerMs: inputs[Math.floor(inputs.length * .95)], axes, drawn, hidden, resumed, listeners: window.__listenerCount() };
    }, persona);
    measurements.push(result);
    check(result.nodeReads <= result.frames * 2, persona + ' axis-only updates rebuild at most two vector nodes', {nodeReads:result.nodeReads,frames:result.frames});
    const models = JSON.parse(source(path.join(repo, 'src/bridge/controllerDiagramData.json')));
    const model = models[persona === 'dualsense-edge' ? 'ps5' : persona === 'elite' ? 'xbox' : 'steamdeck'];
    const nodes = model.nodes.flatMap(function walk(n) { return [n, ...n.children.flatMap(walk)]; });
    const fillState = async code => page.locator('.device-vector [data-fill-for~="' + code + '"]').evaluateAll(nodes => nodes.map(n => ({ active: n.getAttribute('data-fill-active'), live: n.getAttribute('data-fill-live') })));
    await page.evaluate(async () => { await window.highlight([0]); });
    check((await fillState('a')).some(n => n.active === 'true' && n.live === 'false'), persona + ' mapping still paints cached nodes');
    await page.evaluate(async () => { const buttons = Array(18).fill(false); buttons[0] = true; buttons[10] = true; window.__emit('gamepad.state', { connected: true, buttons, axes: [.5, .25, 0, 0] }); await window.__tick(); });
    check((await fillState('a')).some(n => n.live === 'true') && (await fillState('leftThumb')).some(n => n.live === 'true'), persona + ' live fill reaches buttons and nested stick children');
    await page.evaluate(async () => { window.__emit('gamepad.state', { connected: true, buttons: Array(18).fill(false), axes: [0, 0, 0, 0] }); await window.__tick(); });
    await page.waitForTimeout(210);
    check((await fillState('a')).some(n => n.active === 'true' && n.live === 'false') && (await fillState('leftThumb')).every(n => n.active === null), persona + ' pulse expiry restores mapping and clears live stick fill');
    await page.evaluate(() => window.highlight([]));
    check((await fillState('a')).every(n => n.active === null), persona + ' clearing mapping removes cached fill');
    if (nodes.some(n => n.availabilityFor === 'L4')) {
      await page.evaluate(() => window.catalog({ supported: true, keys: [{ keyId: 'rearLeft', label: 'Rear left', backIndex: 1, captureImplemented: true }] }));
      check(await page.locator('[data-rear-control="L4"][data-available="true"]').count() > 0, persona + ' rear availability updates cached artwork');
      await page.evaluate(() => window.catalog({ supported: false, keys: [] }));
      check(await page.locator('[data-rear-control="L4"][data-available="false"]').count() > 0, persona + ' removing rear availability restores dimming');
    }
    const cdp = await page.context().newCDPSession(page), trace = [];
    cdp.on('Tracing.dataCollected', event => trace.push(...event.value));
    await cdp.send('Tracing.start', { categories: 'devtools.timeline', transferMode: 'ReportEvents' });
    const paced = await page.evaluate(async () => {
      const intervals = []; let previous;
      for (let i = 0; i < 90; i++) {
        const at = await new Promise(requestAnimationFrame); if (previous !== undefined) intervals.push(at - previous); previous = at;
        window.__emit('gamepad.state', { connected: true, buttons: Array(18).fill(false), axes: [Math.sin(i * .1), Math.cos(i * .1), -Math.sin(i * .1), -Math.cos(i * .1)] });
        await window.__tick();
      }
      intervals.sort((a, b) => a - b); return { frames: 90, p95FrameIntervalMs: intervals[Math.floor(intervals.length * .95)], maxFrameIntervalMs: Math.max(...intervals) };
    });
    const completed = new Promise(resolve => cdp.once('Tracing.tracingComplete', resolve));
    await cdp.send('Tracing.end'); await completed;
    fs.writeFileSync(path.join(out, persona + '-render-trace.json'), JSON.stringify({ traceEvents: trace }));
    const tasks = trace.filter(e => e.ph === 'X' && e.name === 'Paint');
    result.pacedRendering = { ...paced, paintEvents: tasks.length, paintDurationMs: tasks.reduce((n, e) => n + (e.dur || 0) / 1000, 0) };
    await cdp.detach();
    for (const point of result.drawn) {
      const node = nodes.find(n => n.stick === point.stick);
      check(Math.abs(point.x - result.axes[point.stick * 2] * node.travel[0]) < 1e-6 && Math.abs(point.y + result.axes[point.stick * 2 + 1] * node.travel[1]) < 1e-6, persona + ' preserves precise axis endpoints, travel and Y inversion for stick ' + point.stick);
    }
    check(JSON.stringify(result.drawn) === JSON.stringify(result.hidden), persona + ' hidden view freezes display');
    check(result.resumed.every(p => Math.sign(p.x) === (p.stick === 0 ? 1 : -1) && Math.sign(p.y) === (p.stick === 0 ? -1 : 1)), persona + ' resumes the latest snapshot');
    check(await page.evaluate(() => window.f.saved.length === 0 && window.f.calls.length === 0), persona + ' preview does not issue settings or hardware writes');
    await page.screenshot({ path: path.join(out, persona + '-preview.png') });
    await page.evaluate(() => window.unmount());
    check(await page.evaluate(() => window.__listenerCount()) === 0, persona + ' releases IPC subscriptions on unmount');
  }
  check(errors.length === 0, 'no browser errors', errors);
} catch (error) { failure = error.stack || String(error); }
finally {
  await browser?.close();
  if (server?.listening) await new Promise(resolve => server.close(resolve));
  stop();
  const bindings = [...sources].map(([file, hash]) => ({ path: path.relative(repo, file).replaceAll('\\', '/'), sha256: hash, unchanged: fs.existsSync(file) && sha(fs.readFileSync(file)) === hash }));
  const inputsUnchanged = bindings.every(item => item.unchanged);
  const pass = !failure && errors.length === 0 && inputsUnchanged && checks.every(item => item.pass);
  const result = { pass, baseline, inputsUnchanged, hardwareIo: false, checks, measurements, errors, failure, boundaries: ['Memory IPC/settings only; no installed application or physical controller is operated.', 'Consumer timings measure JavaScript/Vue flushes. Paced frame intervals and DevTools Paint traces cover this browser fixture; neither establishes native packet cadence, installed WebView or physical latency.', 'The TDP reference uses the actual Slider and shared cadence constant; the full page/gamepad engine release gate is separate.'] };
  fs.writeFileSync(path.join(out, 'source-bindings.json'), JSON.stringify(bindings, null, 2));
  fs.writeFileSync(path.join(out, 'results.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify({ pass, baseline, inputsUnchanged, checks: checks.length, failures: checks.filter(c => !c.pass).map(c => c.name), measurements, failure, out }, null, 2));
  if (failure || !inputsUnchanged || (!baseline && !pass)) process.exitCode = 1;
}