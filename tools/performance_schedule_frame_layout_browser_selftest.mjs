// Real performance-schedule page and shared frame controls; native/storage are memory fixtures.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
const root = process.cwd();
const require = createRequire(path.join(root, 'package.json'));
const { build } = require('esbuild');
const { parse, compileScript, compileStyle } = require('vue/compiler-sfc');
const moduleRoot = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(moduleRoot, '_schedule-frames.cjs'))('playwright');
const out = process.env.YMCC_SCHEDULE_FRAME_OUT || path.resolve(root, '../../Build/Validation/schedule-frame-layout-20261009/schedule-browser');
fs.mkdirSync(out, { recursive: true });
const mocks = {
  '@/bridge/frameRateLimits': `import {frameRatePair,frameRateSetting} from './src/bridge/frameRateModel.ts';export {frameRatePair};const f=()=>window.__fixture;const listeners=new Set();export async function loadGlobalFrameRates(){return frameRatePair(f().global)};export function onFrameRatesChanged(cb){listeners.add(cb);f().frameSubscribers=listeners.size;return ()=>{listeners.delete(cb);f().frameSubscribers=listeners.size}};export async function saveGlobalFrameRate(side,value){if(f().failGlobal)throw Error('simulated save failure');f().global[side]=frameRateSetting(value);f().frameWrites.push({side,value:structuredClone(value)});for(const cb of listeners)cb(frameRatePair(f().global));return frameRatePair(f().global)};export async function applyIndependentFrameRates(){f().frameApplies++;return true;}`,
  '@/bridge/yeman': `export const FPS_CEILINGS=[0,30,60,90,120,200,300],FPS_MIN=20,TDP_CEILINGS=[20,35,55,75,120,200],TDP_MIN=2;export async function readPowerParams(){return {acTdp:25,dcTdp:12}};export async function detectCoreArchitecture(){return {heterogeneous:true}};export async function ensureRememberedYemanSchemeActive(){return true};export async function rebuildYemanScheme(){return true};`,
  '@/bridge/performanceSchedule': `const f=()=>window.__fixture,copy=value=>JSON.parse(JSON.stringify(value));export class SkipApplyError extends Error{};export const CORE_MODE_OPTIONS=[{value:'all',label:'全部核心'}];export function defaultPerformanceScheduleConfig(){return copy(f().schedule)};export const snapshotPerformanceSchedule=copy;export async function loadPerformanceSchedule(){return copy(f().schedule)};export async function loadGameCustomConfig(){return {version:1,entries:{},rtss:{}}};export function onGameCustomConfigChanged(){return ()=>{}};export function getPerformanceScheduleWarning(){return null};export function onPerformanceScheduleWarning(){return ()=>{}};export async function applyPerformanceSchedule(side,mode){f().performanceApplies.push({side,mode});return true};export async function savePerformanceSchedule(value){f().schedule=copy(value)};export async function disablePerformanceSchedule(value){f().schedule=copy({...value,enabled:false})};export async function resetPerformanceScheduleProfiles(){return copy(f().schedule)};`,
  '@/bridge/autofloat': `export const FLOAT_PROFILES=Object.fromEntries(['none','eco','bal','perf','aggressive'].map(key=>[key,{min:1000,max:3000}]));export const TDP_FLOAT_STRATEGY_ORDER=['none','small','medium','large','aggressive'];export const TDP_FLOAT_EXECUTION_LABELS={none:'无下降',small:'小浮动',medium:'中幅浮动',large:'大幅浮动',aggressive:'激进浮动'};export const getTdpTarget=value=>value;export const getFloatInfo=()=>({enabled:true,target:120,tdpApplied:25,status:{packagePower:23}});export const onFloatUpdate=()=>()=>{};`,
  '@/bridge/cpuProfiles': `const defaults=()=>({version:1,active:'balanced',profiles:Object.fromEntries(['balanced','turbo','elite','extreme'].map(key=>[key,{acFreq:0,dcFreq:0,acAggr:80,dcAggr:60,acTurbo:true,dcTurbo:true}]))});export const defaultCpuProfilesConfig=defaults;export const loadCpuProfiles=async()=>defaults();`,
  '@/bridge/gamePolicyTarget': `export const isPolicyGameInitialized=()=>true,getPolicyGame=()=>null,detectPolicyGame=async()=>null;export function subscribePolicyGameStatus(callback){callback(null);return ()=>{}};`,
  '@/bridge/gamedetect': `export const refreshGameStatus=async()=>null;`,
  '@/bridge/topmon': `import {ref} from 'vue';export const topMonitorData=ref({ac:1,fps:110,fps1:95,tdpW:23,freqMhz:3200,cpuUsage:35,gpuPowerW:12,gpuClockMhz:1800});`,
  '@/bridge/powerSource': `import {ref} from 'vue';export const powerSourceMode=ref('ac');`,
  '@/bridge/uiSettings': `export const getUiSetting=key=>window.__fixture.ui[key];export const loadUiSettings=async()=>window.__fixture.ui;export async function setUiSettings(value){Object.assign(window.__fixture.ui,value)};`,
  '@/scheduler': `export const registerScheduledTask=()=>()=>{};`,
  '@/robust/startupReadiness': `export const initialPageWork={track:async work=>await work()};`,
};
const entry = `import {createApp} from 'vue';import Page from './src/views/PerformanceScheduleView.vue';
const profiles=Object.fromEntries(['eco','balanced','medium','performance','elite','extreme'].map((mode,index)=>[mode,{cpuPreset:'balanced',coreMode:'all',tdpMax:12+index*5,fpsTarget:120,cpuTarget:'none',tdpStrategy:'none'}]));
window.__fixture={global:{ac:{fps:120,ceiling:120,lastFps:120},dc:{fps:60,ceiling:60,lastFps:60}},schedule:{enabled:true,configured:true,active:{ac:'performance',dc:'balanced'},profiles:{ac:profiles,dc:structuredClone(profiles)}},ui:{scheduleMonitor:true},frameWrites:[],frameApplies:0,performanceApplies:[],failGlobal:false,frameSubscribers:0};let app=createApp(Page);app.mount('#app');window.__unmount=()=>app.unmount();`;
const styles = [];
const built = await build({ stdin: { contents: entry, resolveDir: root, loader: 'ts' }, bundle: true, write: false, platform: 'browser', format: 'iife', alias: { '@': path.join(root, 'src') }, define: { 'process.env.NODE_ENV': '"production"' }, plugins: [{ name: 'schedule-frame-fixture', setup(builder) {
  builder.onResolve({ filter: /.*/ }, args => args.path in mocks ? { path: args.path, namespace: 'fixture' } : undefined);
  builder.onLoad({ filter: /.*/, namespace: 'fixture' }, args => ({ contents: mocks[args.path], loader: 'ts', resolveDir: root }));
  builder.onLoad({ filter: /\.vue$/ }, args => {
    const { descriptor, errors } = parse(fs.readFileSync(args.path, 'utf8'), { filename: args.path });
    assert.deepEqual(errors, []);
    const id = 'data-v-' + createHash('sha256').update(args.path).digest('hex').slice(0, 8);
    const script = compileScript(descriptor, { id, inlineTemplate: true });
    for (const style of descriptor.styles) {
      const result = compileStyle({ source: style.content, filename: args.path, id, scoped: style.scoped });
      assert.deepEqual(result.errors, []);
      styles.push(result.code);
    }
    return { contents: script.content.replace('export default', 'const _component=') + '\n_component.__scopeId=' + JSON.stringify(id) + ';export default _component;', loader: 'ts', resolveDir: path.dirname(args.path) };
  });
} }] });
const css = fs.readFileSync('src/styles/tokens.css', 'utf8') + '\n' + styles.join('\n');
const html = '<!doctype html><meta charset="utf-8"><title>Schedule frame layout</title><style>' + css + '\nbody{margin:0;background:var(--bg-solid);color:var(--text);font-family:system-ui}#app{max-width:720px;box-sizing:border-box;margin:auto;padding:14px}</style><main id="app"></main><script src="/fixture.js"></script>';
const server = createServer((request, response) => { response.setHeader('Content-Type', request.url === '/fixture.js' ? 'text/javascript' : 'text/html; charset=utf-8'); response.end(request.url === '/fixture.js' ? built.outputFiles[0].text : html); });
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const cases = [], screenshots = [], errors = [];
let browser;
try {
  browser = await chromium.launch({ headless: true, executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  const page = await browser.newPage({ viewport: { width: 720, height: 1050 } });
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('http://127.0.0.1:' + server.address().port);
  await page.waitForFunction(() => document.querySelector('.schedule-card .mode-picker button')?.disabled === false);
  assert.equal(await page.locator('.rtss-frame-limit').count(), 0);
  assert.equal(await page.getByText('全局帧率上限', { exact: true }).count(), 0);
  assert.equal(await page.locator('.schedule-card .frame-rate-side').count(), 0);
  assert.deepEqual(await page.locator('.schedule-card .side-name strong').allTextContents(), ['AC', 'DC']);
  assert.deepEqual(await page.locator('.schedule-card input[type=range]').evaluateAll(nodes => nodes.map(node => node.value)), ['120', '60']);
  assert.equal(await page.locator('.schedule-card .slider-label').filter({ hasText: '帧率上限' }).count(), 2);
  assert.equal(await page.evaluate(() => window.__fixture.frameWrites.length), 0);
  cases.push('One automatic-optimization card contains exactly one AC and one DC frame pair with no repeated power labels or old heading');
  for (const width of [720, 580, 420]) {
    await page.setViewportSize({ width, height: 1050 });
    const layout = await page.evaluate(() => {
      const rect = node => { const r = node.getBoundingClientRect(); return { x: r.x, y: r.y, width: r.width, height: r.height, right: r.right, bottom: r.bottom }; };
      return { overflow: document.documentElement.scrollWidth > innerWidth, monitor: rect(document.querySelector('.monitor-chart')), card: rect(document.querySelector('.schedule-card')), rows: [...document.querySelectorAll('.power-mode-row')].map(row => ({ row: rect(row), mode: rect(row.querySelector('.mode-picker')), frames: rect(row.querySelector('.mode-frame-controls')), slider: rect(row.querySelector('.slider')), dropdown: rect(row.querySelector('.frame-rate-controls .dd-trigger')) })) };
    });
    assert.equal(layout.overflow, false);
    assert(layout.card.y >= layout.monitor.bottom);
    assert.equal(layout.rows.length, 2);
    for (const row of layout.rows) {
      assert(row.frames.y >= row.mode.bottom);
      assert(row.row.height >= 100);
      assert(row.slider.width >= 100);
      assert(row.dropdown.right <= row.row.right);
    }
    assert(Math.abs(layout.rows[0].dropdown.x - layout.rows[1].dropdown.x) <= 1);
    const screenshot = path.join(out, 'schedule-frames-' + width + '.png');
    await page.screenshot({ path: screenshot, fullPage: true });
    screenshots.push(screenshot);
    cases.push('Monitor remains above optimization; taller AC/DC rows contain matching-width frames underneath without overflow at ' + width + 'px');
  }
  const ac = page.locator('.power-mode-row.ac input[type=range]');
  const dc = page.locator('.power-mode-row.dc input[type=range]');
  const initialSchedule = await page.evaluate(() => JSON.stringify(window.__fixture.schedule));
  await ac.focus();
  await ac.press('ArrowLeft');
  await page.waitForFunction(() => window.__fixture.global.ac.fps === 110);
  assert.equal(await dc.inputValue(), '60');
  assert.equal(await page.evaluate(() => JSON.stringify(window.__fixture.schedule)), initialSchedule);
  assert.equal(await page.evaluate(() => window.__fixture.performanceApplies.length), 0);
  cases.push('Embedded AC slider saves only AC frame state without changing DC or replaying CPU/TDP');
  await page.getByRole('button', { name: '插电锁帧上限', exact: true }).click();
  await page.getByRole('option', { name: '不锁帧', exact: true }).click();
  await page.waitForFunction(() => window.__fixture.global.ac.fps === 0);
  assert.equal(await ac.isDisabled(), true);
  assert.equal(await dc.isDisabled(), false);
  await page.getByRole('button', { name: '插电锁帧上限', exact: true }).click();
  await page.getByRole('option', { name: '120 FPS', exact: true }).click();
  await page.waitForFunction(() => window.__fixture.global.ac.fps === 110);
  cases.push('Embedded ceiling dropdown turns off and restores AC locking without affecting DC');
  await page.evaluate(() => window.__fixture.failGlobal = true);
  await dc.focus();
  await dc.press('ArrowLeft');
  await page.waitForSelector('.schedule-frame-error');
  await page.waitForFunction(() => document.querySelector('.power-mode-row.dc input').value === '60');
  assert.equal(await dc.inputValue(), '60');
  await page.evaluate(() => window.__fixture.failGlobal = false);
  cases.push('Embedded frame-save failure is shown in the merged card and restores the saved value');
  await page.getByRole('button', { name: '切换为手动模式', exact: true }).click();
  await page.waitForSelector('.power-mode-row.frames-only');
  assert.equal(await page.locator('.schedule-card .mode-picker').count(), 0);
  assert.equal(await page.locator('.schedule-card input[type=range]').count(), 2);
  assert.equal(await dc.isDisabled(), false);
  await dc.focus();
  await dc.press('ArrowLeft');
  await page.waitForFunction(() => window.__fixture.global.dc.fps === 50);
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
  const manualImage = path.join(out, 'schedule-frames-manual-420.png');
  await page.screenshot({ path: manualImage, fullPage: true });
  screenshots.push(manualImage);
  cases.push('Manual mode hides performance selectors but keeps both independent frame controls usable');
  await page.getByRole('button', { name: '切换为自动模式', exact: true }).click();
  await page.waitForSelector('.schedule-card .mode-picker');
  assert.deepEqual(await page.locator('.schedule-card input[type=range]').evaluateAll(nodes => nodes.map(node => node.value)), ['110', '50']);
  await page.getByRole('button', { name: '监控已打开', exact: true }).click();
  assert.equal(await page.locator('.monitor-chart').count(), 0);
  assert.equal(await page.locator('.schedule-card input[type=range]').count(), 2);
  cases.push('Returning to auto and hiding monitoring preserve both stored frame limits');
  await page.evaluate(() => window.__unmount());
  assert.equal(await page.evaluate(() => window.__fixture.frameSubscribers), 0);
  assert.deepEqual(errors, []);
  cases.push('Real page renders without browser errors and removes its single frame subscription on unmount');
  const report = { suite: 'Performance schedule merged AC/DC frames browser', passed: cases.length, cases, screenshots, pageErrors: errors, scope: 'Actual performance-schedule Vue page and shared frame controls; native/storage mocked; no installed files or hardware changes' };
  fs.writeFileSync(path.join(out, 'results.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report, null, 2));
} finally {
  if (browser) await browser.close();
  await new Promise(resolve => server.close(resolve));
}
