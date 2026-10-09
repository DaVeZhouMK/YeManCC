import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';

// Render the production Vue components with an in-memory host. Every process,
// filesystem and timer bridge is mocked: this test never starts/stops Steam.
const require = createRequire(path.join(process.cwd(), 'package.json'));
const vue = require('vue');
const { parse, compileScript } = require('vue/compiler-sfc');
const { transformSync } = require('esbuild');
const root = process.cwd();
const source = fs.readFileSync(path.join(root, 'src/views/SteamView.vue'), 'utf8');
const cases = [];
function check(name, action) {
  action();
  cases.push(name);
}
function evaluate(code, imports = {}, globals = {}) {
  const module = { exports: {} };
  vm.runInNewContext(code, {
    module, exports: module.exports,
    require(name) {
      if (name === 'vue') return vue;
      if (Object.hasOwn(imports, name)) return imports[name];
      throw new Error(`Unexpected import: ${name}`);
    },
    ...globals,
  });
  return module.exports;
}
function component(file, imports = {}, globals = {}) {
  const { descriptor, errors } = parse(fs.readFileSync(path.join(root, file), 'utf8'), { filename: file });
  assert.equal(errors.length, 0);
  const compiled = compileScript(descriptor, { id: 'steam-inline-selftest', inlineTemplate: true, templateOptions: { compilerOptions: { hoistStatic: false } } });
  return evaluate(transformSync(compiled.content, { loader: 'ts', format: 'cjs' }).code, imports, globals).default;
}
const Toggle = component('src/components/Toggle.vue');
const spatialCode = transformSync(fs.readFileSync(path.join(root, 'src/gamepad/spatial.ts'), 'utf8'), { loader: 'ts', format: 'cjs' }).code;
const { spatialNavigationTarget } = evaluate(spatialCode);

function element(type, text = '') {
  return {
    type, text, props: {}, children: [], parent: null, dataset: {},
    get textContent() { return this.text + this.children.map(child => child.textContent).join(''); },
  };
}
const renderer = vue.createRenderer({
  createElement: type => element(type),
  createText: text => element('#text', text),
  createComment: text => element('#comment', text),
  setText: (node, text) => { node.text = text; },
  setElementText: (node, text) => { node.text = text; node.children = []; },
  parentNode: node => node.parent,
  nextSibling: node => node.parent?.children[node.parent.children.indexOf(node) + 1] ?? null,
  insert(node, parent, anchor = null) {
    if (node.parent) node.parent.children.splice(node.parent.children.indexOf(node), 1);
    node.parent = parent;
    const index = anchor ? parent.children.indexOf(anchor) : -1;
    if (index < 0) parent.children.push(node); else parent.children.splice(index, 0, node);
  },
  remove(node) {
    if (node.parent) node.parent.children.splice(node.parent.children.indexOf(node), 1);
    node.parent = null;
  },
  patchProp(node, key, previous, next) {
    node.props[key] = next;
    if (key === 'data-gp-row') node.dataset.gpRow = next == null ? undefined : String(next);
    if (key === 'data-gp-col') node.dataset.gpCol = next == null ? undefined : String(next);
  },
});
function descendants(node) { return [node, ...node.children.flatMap(descendants)]; }
async function flush() {
  for (let i = 0; i < 12; i++) { await Promise.resolve(); await vue.nextTick(); }
}
async function fixture(initialRunning = false) {
  const container = element('root');
  const counts = { detect: 0, launch: 0, stop: 0, ordinary: 0 };
  const state = { live: initialRunning, detectFailure: false, launchFailure: false, stopFailure: false };
  const intervals = new Map();
  const timeouts = new Map();
  const listeners = new Map();
  let now = 0, timer = 0;
  const addons = [
    { key: 'addon-a', name: 'Addon A', exe: 'mock-a.exe' },
    { key: 'addon-b', name: 'Addon B', exe: 'mock-b.exe' },
  ];
  const imports = {
    '@/components/Toggle.vue': { __esModule: true, default: Toggle },
    '@/components/InlineIcon.vue': { __esModule: true, default: { props: ['name'], setup: props => () => vue.h('span', { 'data-icon': props.name }) } },
    '@/bridge/api': { dialog: { openFile: async () => null }, shell: {
      open: async () => { counts.ordinary++; },
      execute: async () => { throw new Error('Unexpected program launch'); },
    } },
    '@/bridge/uiLifecycle': { isUiVisible: () => true, onUiVisibilityChange: () => () => {} },
    '@/bridge/yeman': {
      STEAM_ADDONS: addons,
      steamAddonExists: async () => false,
      steamCustomAddons: async () => [{ id: 'custom', name: 'Custom', exe: 'mock-c.exe', enabled: true }],
      steamRunning: async () => {
        counts.detect++;
        if (state.detectFailure) throw new Error('Mock detection failed');
        return state.live;
      },
      launchSteam: async () => { counts.launch++; if (state.launchFailure) throw new Error('Mock launch failed'); },
      steamStop: async () => { counts.stop++; if (state.stopFailure) throw new Error('Mock close failed'); },
    },
    '@/bridge/customSteamLibrary': { readCustomSteamLibrarySummary: async () => null, launchCustomSteamLibrary: async () => ({ ok: true }) },
  };
  const globals = {
    window: {
      addEventListener: (name, handler) => listeners.set(name, handler),
      removeEventListener: name => listeners.delete(name),
    },
    Date: { now: () => now },
    setInterval: (fn, ms) => { const id = ++timer; intervals.set(id, { fn, ms }); return id; },
    clearInterval: id => intervals.delete(id),
    setTimeout: (fn, ms) => { const id = ++timer; timeouts.set(id, { fn, ms }); return id; },
    clearTimeout: id => timeouts.delete(id),
  };
  const app = renderer.createApp(component('src/views/SteamView.vue', imports, globals));
  // Existing addon state initialization is async; only this known mount-time
  // warning is tolerated. All other Vue warnings fail the regression test.
  app.config.warnHandler = message => {
    if (!message.includes('Invalid prop: type check failed for prop "modelValue". Expected Boolean, got Undefined')) {
      throw new Error(message);
    }
  };
  app.provide('globalRefreshKey', vue.ref(0));
  app.mount(container);
  await flush();
  const all = () => descendants(container);
  const find = className => all().find(node => String(node.props.class ?? '').split(' ').includes(className));
  const controls = () => all().filter(node => node.type === 'button' && !node.props.disabled);
  return {
    state, counts, intervals, listeners, container, find, controls,
    status: () => find('steam-state-card'), power: () => find('steam-power-button'),
    async click(node) { await node.props.onClick(); await flush(); },
    async poll(elapsed = 2000) { now += elapsed; for (const { fn } of [...intervals.values()]) await fn(); await flush(); },
    destroy() { app.unmount(); assert.equal(intervals.size, 0); },
  };
}

check('removed popup and ordinary-launch route', () => {
  assert.doesNotMatch(source, /steamLaunchPopup|steam-launch-popup|data-gp-modal|launchBigPicture|FromPopup/);
});
const f = await fixture();
check('exactly two parallel Steam buttons with stable row/columns', () => {
  const buttons = f.find('states-row').children.filter(node => node.type === 'button');
  assert.equal(buttons.length, 2);
  assert.deepEqual(buttons.map(node => node.dataset), [{ gpRow: '0', gpCol: '0' }, { gpRow: '0', gpCol: '1' }]);
  assert.match(f.status().textContent, /Steam未启动/);
  assert.match(f.power().textContent, /开启 Steam/);
  assert.equal(f.listeners.has('ipc:gamepad-back'), false);
});
check('every visual row is horizontally reachable without wrapping or activation', () => {
  const before = { ...f.counts };
  const controls = f.controls();
  const rows = [...new Set(controls.map(node => node.dataset.gpRow))];
  for (const row of rows) {
    const targets = controls.filter(node => node.dataset.gpRow === row).sort((a, b) => Number(a.dataset.gpCol) - Number(b.dataset.gpCol));
    assert.equal(spatialNavigationTarget(controls, targets[0], { dx: -1, dy: 0 }), null);
    assert.equal(spatialNavigationTarget(controls, targets.at(-1), { dx: 1, dy: 0 }), null);
    for (let i = 0; i + 1 < targets.length; i++) {
      assert.equal(spatialNavigationTarget(controls, targets[i], { dx: 1, dy: 0 }), targets[i + 1]);
      assert.equal(spatialNavigationTarget(controls, targets[i + 1], { dx: -1, dy: 0 }), targets[i]);
    }
  }
  const library = f.find('custom-library-open-button');
  assert.equal(spatialNavigationTarget(controls, f.power(), { dx: 0, dy: 1 }), library);
  assert.equal(spatialNavigationTarget(controls, library, { dx: 0, dy: -1 }), f.status());
  assert.equal(spatialNavigationTarget(controls, f.status(), { dx: 0, dy: -1 }), null);
  const bottom = controls.at(-1);
  assert.equal(spatialNavigationTarget(controls, bottom, { dx: 0, dy: 1 }), null);
  assert.deepEqual(f.counts, before);
});
f.state.live = true;
await f.click(f.status());
check('status button detects external Steam start without switching power', () => {
  assert.match(f.status().textContent, /运行中/);
  assert.match(f.power().textContent, /关闭 Steam/);
  assert.equal(f.counts.launch + f.counts.stop, 0);
});
f.state.live = false;
await f.click(f.status());
await f.click(f.power());
check('开启 Steam uses linked launch only and waits for the real process', () => {
  assert.equal(f.counts.launch, 1);
  assert.equal(f.counts.ordinary, 0);
  assert.match(f.status().textContent, /未启动/);
  assert.equal(f.power().props.disabled, true);
  assert.equal(f.status().props.disabled, true);
  assert.equal(f.intervals.size, 1);
  assert.equal([...f.intervals.values()][0].ms, 2000);
  assert.equal(f.controls().some(node => node.dataset.gpRow === '0'), false);
});
await f.click(f.power());
check('busy switch cannot issue duplicate launches', () => assert.equal(f.counts.launch, 1));
f.state.live = true;
await f.poll();
check('confirmed running process updates both buttons and releases busy state', () => {
  assert.match(f.status().textContent, /运行中/);
  assert.match(f.power().textContent, /关闭 Steam/);
  assert.equal(f.power().props.disabled, false);
  assert.equal(f.intervals.size, 0);
});
await f.click(f.power());
check('关闭 Steam uses the existing stop path and does not fake an exited process', () => {
  assert.equal(f.counts.stop, 1);
  assert.equal(f.counts.launch, 1);
  assert.match(f.status().textContent, /正在检测退出/);
  assert.equal(f.power().props.disabled, true);
});
f.state.live = false;
await f.poll();
check('confirmed exit restores 开启 Steam and horizontal reachability', () => {
  assert.match(f.power().textContent, /开启 Steam/);
  assert.match(f.status().textContent, /未启动/);
  assert.equal(f.power().props.disabled, false);
  assert.equal(spatialNavigationTarget(f.controls(), f.status(), { dx: 1, dy: 0 }), f.power());
});
f.state.detectFailure = true;
await f.click(f.power());
check('failed detection blocks launch and preserves the last running state', () => {
  assert.equal(f.counts.launch, 1);
  assert.match(f.find('err-bar').textContent, /状态验证失败/);
  assert.equal(f.power().props.disabled, false);
});
f.state.detectFailure = false;
f.state.launchFailure = true;
await f.click(f.power());
check('launch error releases the direct switch', () => {
  assert.match(f.find('err-bar').textContent, /Steam 启动失败/);
  assert.equal(f.power().props.disabled, false);
});
f.destroy();
const running = await fixture(true);
running.state.stopFailure = true;
await running.click(running.power());
check('already-running Steam offers close; close errors keep a retryable switch', () => {
  assert.match(running.power().textContent, /关闭 Steam/);
  assert.equal(running.counts.launch, 0);
  assert.match(running.find('err-bar').textContent, /关闭 Steam 失败/);
  assert.equal(running.power().props.disabled, false);
});
running.state.stopFailure = false;
await running.click(running.power());
await running.poll(20000);
check('exit timeout keeps the detected running state and allows retry', () => {
  assert.match(running.power().textContent, /关闭 Steam/);
  assert.match(running.find('err-bar').textContent, /仍检测到运行进程/);
  assert.equal(running.power().props.disabled, false);
  assert.equal(running.intervals.size, 0);
});
running.destroy();
console.log(JSON.stringify({ suite: 'Steam inline power UI', passed: cases.length, cases, realSteamOperations: 0 }, null, 2));
