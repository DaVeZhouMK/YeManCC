import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import vm from 'node:vm';

// Exercise the actual Steam page setup and KeepAlive lifecycle. Renderless
// children and fake bridge ports keep the test off disk, Steam and devices.
const require = createRequire(import.meta.url);
const vue = require('vue');
const { parse, compileScript } = require('vue/compiler-sfc');
const { transformSync } = require('esbuild');
const sourceUrl = process.argv[2] ?? new URL('../src/views/SteamView.vue', import.meta.url);
const source = readFileSync(sourceUrl, 'utf8');
const { descriptor, errors } = parse(source, { filename: 'SteamView.vue' });
assert.deepEqual(errors, []);
const code = transformSync(compileScript(descriptor, { id: 'addon-visibility' }).content,
  { loader: 'ts', format: 'cjs' }).code;
const readBudget = readFileSync(new URL('../src/bridge/steamReadBudget.ts', import.meta.url), 'utf8');
const budgetModule = { exports: {} };
vm.runInNewContext(transformSync(readBudget, { loader: 'ts', format: 'cjs' }).code, { module: budgetModule });
const renderer = vue.createRenderer({
  createElement: () => ({}), createComment: () => ({}), createText: () => ({}),
  insert: (node, parent) => { node.parent = parent; }, remove: node => { node.parent = null; },
  parentNode: node => node.parent, nextSibling: () => null,
  patchProp() {}, setText() {}, setElementText() {},
});
const stub = { default: { render: () => null }, __esModule: true };
const flush = async () => { for (let i = 0; i < 8; ++i) await vue.nextTick(); };
function fixture(initialVisible) {
  const refresh = vue.ref(0), active = vue.ref(true), page = vue.ref(null);
  let visible = initialVisible, visibility, resolved = 0, timers = 0;
  const jobs = [], counts = { list: 0, probe: 0, status: 0, action: 0 };
  const ports = {
    vue,
    '@/bridge/uiSettings': { getUiSetting: () => false },
    '@/bridge/sharedGameMedia': { steamWallpaperEnabled: () => false,
      sharedGameMedia: { preparationDescription: () => '', subscribe: () => () => {} } },
    '@/bridge/gameMediaDownloads': { backgroundDownloadEnabled: () => false,
      gameMediaDownloads: { state: {}, subscribe: () => () => {} } },
    '@/bridge/deckySidebar': { deckyConnectionSummary: () => '', deckySidebarCanRepair: () => false,
      deckySidebar: { get: async () => ({ enabled: false, revision: 1 }), subscribe: () => () => {} } },
    '@/bridge/api': {},
    '@/bridge/uiLifecycle': { isUiVisible: () => visible,
      getUiVisibilityState: () => ({ documentVisible: visible, nativeWindowVisible: visible }),
      onUiVisibilityChange: fn => { visibility = fn; fn({ visible }); return () => { visibility = undefined; }; } },
    '@/bridge/steamReadBudget': budgetModule.exports,
    '@/bridge/steamUi': { steamUi: { subscribe: () => () => {}, read: async () => {
      ++counts.status; return { known: true, running: false, mode: 'stopped', pid: 0, birth: '0', reason: '' };
    } } },
    '@/bridge/customSteamLibrary': { readCustomSteamLibrarySummary: async () => null },
    '@/bridge/yeman': { STEAM_ADDONS: [{ key: 'a' }, { key: 'b' }],
      steamCustomAddons: () => { ++counts.list; return new Promise(resolve => jobs.push(resolve)); },
      steamAddonExists: async () => { ++counts.probe; return true; },
      launchSteam: async () => { ++counts.action; } },
  };
  const target = { exports: {} }, events = { addEventListener() {}, removeEventListener() {}, dispatchEvent() {} };
  vm.runInNewContext(code, { module: target, require: id => {
    if (id.endsWith('.vue')) return stub;
    assert(Object.hasOwn(ports, id), `unexpected import: ${id}`);
    return ports[id];
  }, document: events, window: events, Event: class {}, performance: { now: () => 0 },
  setTimeout: () => ++timers, clearTimeout() {} });
  const component = target.exports.default;
  component.render = () => null;
  const other = { render: () => null };
  const app = renderer.createApp({ render: () => vue.h(vue.KeepAlive, null,
    () => active.value ? vue.h(component, { ref: page }) : vue.h(other)) });
  app.provide('globalRefreshKey', refresh);
  app.mount({});
  return { counts,
    visible(next) { visible = next; visibility?.({ visible }); },
    async refresh(n) { for (let i = 0; i < n; ++i) { ++refresh.value; await flush(); } },
    async complete(id) { assert(jobs[resolved], 'expected a pending addon list'); jobs[resolved++]([{ id }]); await flush(); },
    async active(next) { active.value = next; await flush(); },
    names: () => Array.from(page.value.$.setupState.customAddons, a => a.id),
    unmount: () => app.unmount(),
  };
}

const f = fixture(false);
await f.refresh(20);
const hidden = { ...f.counts };
if (hidden.list || hidden.probe || hidden.status || hidden.action) {
  f.unmount();
  console.log(JSON.stringify({ hiddenMountAnd20Refreshes: hidden }));
}
assert.deepEqual(hidden, { list: 0, probe: 0, status: 0, action: 0 }, 'hidden Steam page dispatched reads');
f.visible(true); await flush();
assert.equal(f.counts.list, 1);
assert.equal(f.counts.probe, 2);
await f.refresh(20);
assert.equal(f.counts.list, 1, 'overlapping global refreshes must not read concurrently');
f.visible(false); f.visible(true);
await f.complete('old');
assert.deepEqual(f.names(), [], 'retired visibility epoch must not publish');
assert.equal(f.counts.list, 2, 'exactly one trailing read after the old one completes');
await f.complete('fresh');
assert.deepEqual(f.names(), ['fresh']);
await f.active(false);
await f.refresh(5);
assert.equal(f.counts.list, 2);
f.visible(false);
await f.active(true);
assert.equal(f.counts.list, 2, 'activation while hidden must remain static');
f.visible(true);
assert.equal(f.counts.list, 3);
f.unmount();
await f.complete('unmounted');
assert.equal(f.counts.list, 3, 'late completion must not restart a closed owner');
assert.equal(f.counts.action, 0);
console.log(JSON.stringify({ passed: true, hiddenMountAnd20Refreshes: hidden, final: f.counts }));
