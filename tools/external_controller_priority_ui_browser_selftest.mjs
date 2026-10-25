// Real Vue controller UI + real settings repository; storage and IPC stay in memory.
// Run: node tools/external_controller_priority_ui_browser_selftest.mjs
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
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
  browser = await chromium.launch({ headless: true, executablePath: process.env.YMCC_UI_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  const page = await browser.newPage({ viewport: { width: 900, height: 700 } });
  page.on('pageerror', error => errors.push(error.message));
  const ready = async () => {
    await page.goto('http://127.0.0.1:' + server.address().port);
    await page.waitForFunction(() => !document.querySelector('.external-priority-toggle')?.disabled);
  };
  await ready();
  const toggle = page.getByRole('switch', { name: '外接手柄优先', exact: true });
  const outputStatus = page.locator('.persona-capabilities span').first();
  const saved = () => page.evaluate(() => structuredClone(window.__fixture.settings.input));
  const idle = () => page.waitForFunction(() => !document.querySelector('.external-priority-toggle').disabled);
  const assertChecked = async value => assert.equal(await toggle.getAttribute('aria-checked'), String(value));
  const emit = async value => page.evaluate(value => window.__emit('gamepad.state', value), value);
  const baseline = await saved();

  assert.equal(baseline.ownership.externalControllerPriority, false);
  await assertChecked(false);
  assert.equal(await page.locator('.persona-btn').first().isDisabled(), true);
  assert.equal(await toggle.isDisabled(), false);
  assert.match(await toggle.getAttribute('title'), /状态待确认/);
  assert.equal(await page.locator('.state-chip').textContent(), '已请求');
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 0);
  const dimensions = await page.evaluate(() => {
    const toggle = document.querySelector('.external-priority-toggle');
    const chip = document.querySelector('.state-chip');
    const a = toggle.getBoundingClientRect(), b = chip.getBoundingClientRect();
    return { before: toggle.nextElementSibling === chip, left: a.right <= b.left, aligned: Math.abs(a.y - b.y) < 2, height: a.height };
  });
  assert(dimensions.before && dimensions.left && dimensions.aligned && dimensions.height <= 28);
  cases.push('small switch immediately left of requested chip; default off and usable during game override');

  const normalization = await page.evaluate(() => {
    const { normalizeSettings } = window.__audit;
    return [undefined, null, 'true', 1, false, true].map(value => {
      const settings = normalizeSettings({ input: { ownership: { externalControllerPriority: value, future: { retained: true } } } });
      return { value: settings.input.ownership.externalControllerPriority, retained: settings.input.ownership.future.retained };
    }).concat([null, 'invalid', []].map(ownership => ({ value: normalizeSettings({ input: { ownership } }).input.ownership.externalControllerPriority, retained: true })));
  });
  assert.deepEqual(normalization.map(item => item.value), [false, false, false, false, false, true, false, false, false]);
  assert(normalization.every(item => item.retained));
  cases.push('strict boolean normalization defaults malformed/legacy ownership to false and preserves extensions');

  await toggle.focus();
  await toggle.press('Space');
  await idle();
  await assertChecked(true);
  const firstSave = await saved();
  assert.equal(firstSave.revision, baseline.revision + 1);
  assert.deepEqual(firstSave.outputTarget, baseline.outputTarget);
  assert.deepEqual(firstSave.gyroMotion, baseline.gyroMotion);
  assert.deepEqual(firstSave.gameOverride, baseline.gameOverride);
  assert.deepEqual(firstSave.ownership, { ...baseline.ownership, externalControllerPriority: true });
  assert.match(await toggle.getAttribute('title'), /状态待确认/);
  assert.equal(await page.locator('.state-chip').textContent(), '已请求');
  cases.push('keyboard activation performs durable ownership-only CAS without mutating target, gyro, game override, or other ownership');

  const effective = { enabled: true, state: 'active', reason: 'external-controller-priority', externalPresent: true, builtinHidden: true, virtualSuspended: true };
  const suppressed = { state: 'external-priority', reason: 'external-controller-priority', requestedPersona: 'steamdeck', steamRunning: false };
  await emit({ externalPriority: effective, virtualOutput: suppressed });
  assert.equal(await outputStatus.textContent(), '虚拟手柄-外接优先暂停');
  assert.match(await toggle.getAttribute('title'), /外接手柄直通/);
  assert.match(await toggle.getAttribute('title'), /本机物理手柄已隐藏/);
  assert.match(await toggle.getAttribute('title'), /断开后恢复已请求模式/);
  assert.equal(await page.locator('.persona-btn.active').textContent(), 'SteamDeck');
  assert.equal((await saved()).outputTarget.persona, 'steamdeck');
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 1);
  const descriptionId = await toggle.getAttribute('aria-describedby');
  assert.match(await page.locator('#' + descriptionId).textContent(), /外接手柄直通/);
  cases.push('verified external takeover is presented as intentionally suspended before Steam warnings; runtime events never save persona');

  await emit({ externalPriority: { ...effective, builtinHidden: false, state: 'degraded', reason: 'hide-unverified' }, virtualOutput: suppressed });
  assert.match(await toggle.getAttribute('title'), /尚未确认/);
  assert(!((await toggle.getAttribute('title')).startsWith('外接手柄直通')));
  await emit({ externalPriority: { ...effective, externalPresent: false, state: 'recovering' }, virtualOutput: { ...suppressed, state: 'recovering', steamRunning: true } });
  assert.match(await toggle.getAttribute('title'), /正在恢复本机输出/);
  assert.equal(await outputStatus.textContent(), '虚拟手柄-正在恢复');
  const running = { state: 'active', requestedPersona: 'steamdeck', boundPersona: 'steamdeck', hostAlive: true, prepared: true, neutralized: true, frameAccepted: true, sourceLive: true, suppressionState: 'verified', steamRunning: true };
  await emit({ externalPriority: { ...effective, externalPresent: false, builtinHidden: false, virtualSuspended: false, state: 'waiting' }, virtualOutput: running });
  assert.match(await toggle.getAttribute('title'), /等待外接手柄/);
  assert.equal(await outputStatus.textContent(), '虚拟手柄-成功开启');
  assert.equal((await saved()).outputTarget.persona, 'steamdeck');
  await emit({ virtualOutput: running });
  assert.match(await toggle.getAttribute('title'), /状态待确认/);
  cases.push('unverified hiding never claims takeover; disconnect restores requested mode and missing policy receipts remain pending');

  await page.evaluate(() => window.__emit('input.shortcutRuntime', { active: true, persona: 'dualsense-edge', virtualEnabled: true }));
  await toggle.click(); await idle(); await assertChecked(false);
  assert.equal(await page.locator('.persona-btn.active').textContent(), 'PS5');
  assert.equal((await saved()).outputTarget.persona, 'steamdeck');
  assert.equal(await page.evaluate(() => window.__fixture.requests.filter(method => method === 'input.shortcutRuntime.clear').length), 0);
  cases.push('policy saves preserve runtime shortcut persona and do not clear shortcut runtime');

  await ready();
  await page.evaluate(() => window.__fixture.delayWrite = true);
  await toggle.click();
  await page.waitForFunction(() => typeof window.__fixture.releaseWrite === 'function');
  assert.equal(await toggle.isDisabled(), true);
  assert.equal(await toggle.getAttribute('aria-busy'), 'true');
  await assertChecked(false);
  assert.match(await toggle.getAttribute('title'), /正在保存/);
  await page.evaluate(() => document.querySelector('.external-priority-toggle').click());
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 1);
  await page.evaluate(() => { window.__fixture.delayWrite = false; window.__fixture.releaseWrite(); });
  await idle(); await assertChecked(true);
  cases.push('busy switch keeps durable old value, blocks duplicate writes, exposes saving status and recovers after completion');

  await ready();
  await page.evaluate(() => window.__fixture.failWrite = true);
  await toggle.click(); await idle(); await assertChecked(false);
  assert.match(await toggle.getAttribute('title'), /保存失败，请重试/);
  assert.equal((await saved()).revision, baseline.revision);
  await page.evaluate(() => window.__fixture.failWrite = false);
  await toggle.click(); await idle(); await assertChecked(true);
  cases.push('durable CAS failure leaves old value and revision, announces failure, and permits retry');

  await ready();
  await page.evaluate(() => window.__fixture.throwRead = true);
  await toggle.click(); await idle(); await assertChecked(false);
  assert.match(await toggle.getAttribute('title'), /保存失败，请重试/);
  await page.evaluate(() => window.__fixture.throwRead = false);
  await toggle.click(); await idle(); await assertChecked(true);
  cases.push('thrown repository read error is visible and does not leave switch stuck busy');

  await ready();
  await page.evaluate(() => {
    const input = window.__fixture.settings.input;
    input.revision++; input.outputTarget.persona = 'dualsense';
    input.ownership.foreignUpdate = { keep: 'external' };
  });
  await toggle.click(); await idle(); await assertChecked(true);
  const rebased = await saved();
  assert.equal(rebased.outputTarget.persona, 'dualsense');
  assert.deepEqual(rebased.ownership.foreignUpdate, { keep: 'external' });
  assert.equal(rebased.revision, baseline.revision + 2);
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 1);
  assert.equal(await page.locator('.persona-btn.active').textContent(), 'DualSense (PS5)（旧档）');
  cases.push('stale read revision rebases only policy intent and preserves concurrent persona plus ownership');

  await ready();
  await page.evaluate(() => window.__fixture.conflicts = 1);
  await toggle.click(); await idle(); await assertChecked(true);
  const committed = await saved();
  assert.equal(committed.outputTarget.persona, 'elite');
  assert.deepEqual(committed.ownership.concurrentField, { preserved: true });
  assert.equal(committed.revision, baseline.revision + 2);
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 2);
  assert.equal(await page.locator('.persona-btn.active').textContent(), 'Xbox');
  cases.push('commit-lock revision conflict retries once against actual committed document without overwriting concurrent fields');

  await ready();
  await page.evaluate(() => window.__fixture.conflicts = 2);
  await toggle.click(); await idle(); await assertChecked(false);
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 2);
  assert.match(await toggle.getAttribute('title'), /其他操作更新，请重试/);
  await toggle.click(); await idle(); await assertChecked(true);
  cases.push('repeated concurrent conflicts stop after bounded retry and keep switch usable for explicit retry');

  await ready();
  await page.evaluate(() => {
    const input = window.__fixture.settings.input;
    input.revision++; input.ownership.externalControllerPriority = true;
  });
  await toggle.click(); await idle(); await assertChecked(true);
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 0);
  cases.push('conflict already satisfying requested policy does not produce redundant write');

  await ready();
  await page.evaluate(() => { window.__fixture.delayRead = true; window.__audit.notifyOverride(); });
  await page.waitForFunction(() => typeof window.__fixture.releaseRead === 'function');
  await toggle.click();
  await page.waitForFunction(() => window.__fixture.settings.input.ownership.externalControllerPriority);
  await page.evaluate(() => window.__fixture.releaseRead());
  await idle(); await assertChecked(true);
  assert.equal((await saved()).revision, baseline.revision + 1);
  cases.push('late settings hydration from before save cannot roll persisted policy or page revision back');

  await ready();
  await page.evaluate(() => window.__fixture.afterCommit = true);
  await toggle.click(); await idle();
  await page.waitForFunction(() => document.querySelector('.persona-btn.active').textContent === 'Xbox');
  await assertChecked(false);
  assert.equal((await saved()).revision, baseline.revision + 2);
  await page.evaluate(() => window.__fixture.afterCommit = false);
  await toggle.click(); await idle(); await assertChecked(true);
  assert.equal((await saved()).outputTarget.persona, 'elite');
  cases.push('newer post-commit settings notification survives an older successful CAS reply and next save preserves new persona');

  await ready();
  await page.evaluate(async () => {
    await window.__audit.compareAndSwapInputSettings(window.__fixture.settings.input.revision, { ownership: { externalControllerPriority: true } });
  });
  await page.waitForFunction(() => document.querySelector('.external-priority-toggle').getAttribute('aria-checked') === 'true');
  cases.push('independent repository writes refresh cached controller page through existing settings notifications');

  await ready();
  await page.evaluate(() => { delete window.__fixture.settings.input.gameOverride; window.__audit.notifyOverride(); });
  await page.waitForFunction(() => !document.querySelector('.persona-btn').disabled);
  const focusContract = await page.evaluate(() => {
    const eligible = Array.from(document.querySelectorAll('button:not([disabled]):not([data-gp-ignore])'));
    const preferred = eligible.find(el => el.getAttribute('data-gp-initial-focus') === 'true');
    const target = preferred ?? eligible[0]; target.focus();
    return { first: eligible[0].textContent.trim(), preferredCount: eligible.filter(el => el.hasAttribute('data-gp-initial-focus')).length, focused: document.activeElement.textContent.trim(), usableToggle: eligible[0].getAttribute('role') === 'switch' };
  });
  assert.equal(focusContract.preferredCount, 1);
  assert.equal(focusContract.focused, '关闭虚拟手柄');
  assert(focusContract.usableToggle && focusContract.first === '外接手柄优先');
  // This only validates the proposed one-line engine contract. It does not pretend
  // the out-of-scope shared engine already consumes the attribute.
  const engine = fs.readFileSync(path.join(root, 'src/gamepad/engine.ts'), 'utf8');
  const focusFunction = engine.slice(engine.indexOf('function focusFirst(): boolean {'), engine.indexOf('// A custom dropdown menu'));
  const focusHookIntegrated = focusFunction.includes('gpInitialFocus') || focusFunction.includes('data-gp-initial-focus');
  cases.push('declared initial focus hook selects close-virtual button while switch stays gamepad eligible (hook integration reported separately)');

  await toggle.click(); await idle(); await assertChecked(true);
  await page.evaluate(() => window.__fixture.delayWrite = true);
  await page.getByRole('button', { name: 'PS5', exact: true }).click();
  await page.waitForFunction(() => typeof window.__fixture.releaseWrite === 'function');
  assert.equal(await toggle.isDisabled(), true);
  assert.equal(await toggle.getAttribute('aria-busy'), 'false');
  await page.evaluate(() => { window.__fixture.delayWrite = false; window.__fixture.releaseWrite(); });
  await idle();
  assert.equal((await saved()).outputTarget.persona, 'dualsense-edge');
  assert.equal((await saved()).ownership.externalControllerPriority, true);
  assert.equal(await page.locator('.persona-btn.active').textContent(), 'PS5');
  cases.push('manual persona CAS serializes against policy switch and retains enabled ownership priority');

  const statusCases = await page.evaluate(() => {
    const { virtualOutputPresentation: virtual, externalControllerPriorityPresentation: policy } = window.__audit;
    const effective = { enabled: true, state: 'active', reason: 'external-controller-priority', externalPresent: true, builtinHidden: true, virtualSuspended: true };
    const paused = { state: 'external-priority', reason: 'external-controller-priority', requestedPersona: 'steamdeck', steamRunning: false };
    return {
      paused: virtual(paused, true, 'steamdeck'),
      busy: virtual(paused, true, 'steamdeck', true),
      mismatch: virtual(paused, true, 'elite'),
      disabled: virtual(paused, false, 'disabled', false, effective),
      missing: policy(null, true),
      mismatchPolicy: policy({ ...effective, enabled: false }, true),
      unknown: policy({ ...effective, state: 'unknown' }, true),
      degraded: policy({ ...effective, state: 'degraded' }, true),
      takingOver: policy({ ...effective, state: 'taking-over' }, true),
      suspended: policy({ ...effective, state: 'suspended' }, true),
      notSuspended: policy({ ...effective, virtualSuspended: false }, true),
      disabledRecovering: policy({ ...effective, enabled: false }, false),
      disabledStable: policy({ ...effective, enabled: false, builtinHidden: false, virtualSuspended: false }, false),
    };
  });
  assert.equal(statusCases.paused.text, '虚拟手柄-外接优先暂停'); assert.equal(statusCases.paused.tone, 'off');
  assert.equal(statusCases.busy.tone, 'pending'); assert.equal(statusCases.mismatch.tone, 'pending');
  assert.equal(statusCases.disabled.text, '虚拟手柄-未开启'); assert.match(statusCases.disabled.detail, /外接手柄直通/);
  for (const key of ['missing', 'mismatchPolicy', 'unknown', 'disabledRecovering']) assert.equal(statusCases[key].tone, 'pending');
  for (const key of ['degraded', 'notSuspended']) assert.equal(statusCases[key].tone, 'warning');
  for (const key of ['takingOver', 'suspended']) assert.equal(statusCases[key].tone, 'pending');
  cases.push('port refresh pending and suspend never claim confirmed external passthrough');
  assert.equal(statusCases.disabledStable.tone, 'off');
  cases.push('presentation guards preserve busy/mismatched target precedence and separate saved intent from verified policy');

  for (const width of [420, 900]) {
    await page.setViewportSize({ width, height: 700 });
    const bounds = await page.evaluate(() => {
      const card = document.querySelector('.virtual-card').getBoundingClientRect();
      const toggle = document.querySelector('.external-priority-toggle').getBoundingClientRect();
      const chip = document.querySelector('.state-chip').getBoundingClientRect();
      return { left: toggle.right <= chip.left, contained: toggle.left >= card.left && chip.right <= card.right, aligned: Math.abs(toggle.top - chip.top) < 2 };
    });
    assert(bounds.left && bounds.contained && bounds.aligned, 'header layout at ' + width);
  }
  cases.push('narrow and wide header layout keeps toggle left of state chip inside card');

  assert.deepEqual(errors, []);
  assert((await page.evaluate(() => window.__fixture.requests)).every(method => ['input.shortcutRuntime.get', 'input.shortcutRuntime.clear'].includes(method)));
  await page.evaluate(() => window.__unmount());
  assert.equal(await page.evaluate(() => Array.from(window.__fixture.events['gamepad.state']).length), 0);
  console.log(JSON.stringify({ suite: 'External controller priority UI', passed: cases.length, cases, focusHookIntegrated,
    requiredFocusHook: "focusFirst(): els.find(el => el.dataset.gpInitialFocus === 'true') ?? els[0]",
    scope: 'real Vue, CSS, repository CAS and in-memory IPC/storage; no Native/Host/device execution; no build outputs' }, null, 2));
} finally {
  if (browser) await browser.close();
  await new Promise(resolve => server.close(resolve));
}
