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
const steamReadBudget = evaluate(transformSync(fs.readFileSync(path.join(root, 'src/bridge/steamReadBudget.ts'), 'utf8'), { loader: 'ts', format: 'cjs' }).code);
const spatialCode = transformSync(fs.readFileSync(path.join(root, 'src/gamepad/spatial.ts'), 'utf8'), { loader: 'ts', format: 'cjs' }).code;
const { spatialNavigationTarget } = evaluate(spatialCode);

function element(type, text = '') {
  return {
    type, text, props: {}, children: [], parent: null, dataset: {},
    closest() { return null; }, // This fixture uses explicit rows, with no nearest-geometry ancestor.
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
async function fixture(initialRunning = false, keepAlive = false) {
  const container = element('root');
  const counts = { detect: 0, launch: 0, stop: 0, switch: 0, ordinary: 0 };
  const state = { live: initialRunning, mode: 'bigpicture', target: null, readDeferred:false, readRelease:null, detectFailure: false, launchFailure: false, stopFailure: false, uiVisible: true, launchDeferred: false, launchRelease: null };
  const intervals = new Map();
  const timeouts = new Map();
  const listeners = new Map();
  const documentListeners = new Map();
  let now = 0, timer = 0, visibilityHandler = null;
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
    '@/bridge/uiLifecycle': { isUiVisible: () => state.uiVisible && state.powerReady !== false, getUiVisibilityState: () => ({documentVisible:state.uiVisible,nativeWindowVisible:state.uiVisible,powerReady:state.powerReady!==false}), onUiVisibilityChange: handler => { visibilityHandler = handler; return () => { visibilityHandler = null; }; } },
    '@/bridge/steamReadBudget': steamReadBudget,
    '@/bridge/steamUi': { steamUi: {
      read: async options => { counts.detect++; counts.automatic = (counts.automatic || 0) + (options?.automatic ? 1 : 0); if(state.readDeferred)await new Promise(resolve=>{state.readRelease=resolve}); if (state.detectFailure) throw new Error('Mock detection failed');
        return {known:true,running:state.live,mode:state.live?state.mode:'stopped',pid:state.live?123:0,birth:state.live?'456':'0',reason:''}; },
      switchMode: async (_,target) => { counts.switch++; if(state.stopFailure)throw new Error('Mock switch failed');state.target=target; },
      subscribe: handler => { listeners.set('ipc:steam.uiStateChanged',handler);return()=>listeners.delete('ipc:steam.uiStateChanged'); },
    } },
    '@/bridge/yeman': {
      STEAM_ADDONS: addons,
      steamAddonExists: async () => false,
      steamCustomAddons: async () => [{ id: 'custom', name: 'Custom', exe: 'mock-c.exe', enabled: true }],
      steamRunning: async () => {
        counts.detect++;
        if (state.detectFailure) throw new Error('Mock detection failed');
        return state.live;
      },
      launchSteam: async () => { counts.launch++; if (state.launchDeferred) await new Promise(resolve => { state.launchRelease = resolve; }); if (state.launchFailure) throw new Error('Mock launch failed'); },
      steamStop: async () => { counts.stop++; if (state.stopFailure) throw new Error('Mock close failed'); },
    },
    '@/bridge/customSteamLibrary': { readCustomSteamLibrarySummary: async () => null, launchCustomSteamLibrary: async () => ({ ok: true }) },
    '@/components/SteamAppearanceCard.vue': { __esModule: true, default: { setup: () => () => vue.h('div') } },
    '@/bridge/steamAppearance': { steamAppearance: { setLinked: async () => ({ ok: true }) } },
    '@/bridge/deckySidebar': {
      deckySidebar: { get: async () => ({ enabled: false, phase: 'disabled', revision: 1 }), subscribe: () => () => {} },
      deckySidebarDescription: () => '', deckySidebarSetupFailure: () => '',
    },
  };
  const globals = {
    document: { addEventListener: (name, handler) => documentListeners.set(name, handler), removeEventListener: name => documentListeners.delete(name) },
    window: {
      addEventListener: (name, handler) => listeners.set(name, handler),
      removeEventListener: name => listeners.delete(name),
      dispatchEvent() {},
    },
    Event: class { constructor(type) { this.type = type; } },
    Date: { now: () => now },
    performance: { now: () => now },
    setInterval: (fn, ms) => { const id = ++timer; intervals.set(id, { fn, ms }); return id; },
    clearInterval: id => intervals.delete(id),
    setTimeout: (fn, ms) => { const id = ++timer; timeouts.set(id, { fn, ms, due: now + ms }); return id; },
    clearTimeout: id => timeouts.delete(id),
  };
  const Steam = component('src/views/SteamView.vue', imports, globals);
  const showing = vue.ref(true), Other = { name: 'OtherFixture', setup: () => () => vue.h('div') };
  const app = renderer.createApp(keepAlive ? { setup: () => () => vue.h(vue.KeepAlive, null, { default: () => showing.value ? vue.h(Steam) : vue.h(Other) }) } : Steam);
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
    state, counts, intervals, timeouts, listeners, documentListeners, container, find, controls,
    async events() { for(let i=0;i<5;i++)listeners.get('ipc:steam.uiStateChanged')?.(); await flush(); },
    async eventTimers(){for(const [id,entry]of [...timeouts])if(entry.fn.name === 'runSteamObservation'){now = Math.max(now, entry.due); timeouts.delete(id);await entry.fn();}await flush();},
    async advance(ms){now += ms; for(const [id,entry]of [...timeouts])if(entry.due <= now){timeouts.delete(id);entry.fn();}await flush();},
    async visibility(visible) { state.uiVisible = visible; visibilityHandler?.({ visible }); await flush(); },
    async powerGate(ready) { state.powerReady = ready; visibilityHandler?.({ visible: state.uiVisible && ready }); await flush(); },
    async activate(visible) { showing.value = visible; await flush(); },
    status: () => find('steam-state-card'), power: () => find('steam-power-button'),
    async click(node) { await node.props.onClick(); await flush(); },
    async poll(elapsed = 4000) { now += elapsed; for (const { fn } of [...intervals.values()]) await fn(); await flush(); },
    destroy() { app.unmount(); assert.equal(intervals.size, 0); assert.equal(documentListeners.size, 0); assert.equal(timeouts.size, 0); },
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
  assert.match(f.power().textContent, /点击启动为Steam桌面/);
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
  assert.equal([...f.intervals.values()][0].ms, 4000);
  assert.equal(f.controls().some(node => node.dataset.gpRow === '0'), false);
});
await f.click(f.power());
check('busy switch cannot issue duplicate launches', () => assert.equal(f.counts.launch, 1));
f.state.live = true;
await f.poll();
check('confirmed running process updates both buttons and releases busy state', () => {
  assert.match(f.status().textContent, /运行中/);
  assert.match(f.power().textContent, /点击启动为Steam桌面/);
  assert.equal(f.power().props.disabled, false);
  assert.equal(f.intervals.size, 0);
});
await f.click(f.power());
check('running Steam changes only UI mode, never stops or relaunches the process', () => {
  assert.equal(f.counts.stop, 0); assert.equal(f.counts.switch, 1); assert.equal(f.state.target, 'desktop');
  assert.equal(f.counts.launch, 1);
  assert.match(f.status().textContent, /大屏Steam运行中/);
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
const retry = await fixture(false);
await retry.click(retry.power());
await retry.poll(40000);
check('startup timeout keeps the actual non-running state and releases the switch', () => {
  assert.equal(retry.counts.launch, 1);
  assert.equal(retry.intervals.size, 0);
  assert.equal(retry.power().props.disabled, false);
  assert.match(retry.power().textContent, /开启 Steam/);
  assert.match(retry.find('err-bar').textContent, /暂未检测到运行进程/);
});
await retry.click(retry.power());
retry.state.live = true;
await retry.poll();
check('retry after an unconfirmed startup uses the same linked route and waits for real detection', () => {
  assert.equal(retry.counts.launch, 2);
  assert.equal(retry.counts.ordinary, 0);
  assert.equal(retry.intervals.size, 0);
  assert.match(retry.power().textContent, /点击启动为Steam桌面/);
});
retry.destroy();

const running = await fixture(true);
running.state.stopFailure = true;
await running.click(running.power());
check('already-running Steam offers a mode switch; errors keep a retryable switch', () => {
  assert.match(running.power().textContent, /点击启动为Steam桌面/);
  assert.equal(running.counts.launch, 0);
  assert.match(running.find('err-bar').textContent, /Steam 模式切换失败/);
  assert.equal(running.power().props.disabled, false);
});
running.state.stopFailure = false;
await running.click(running.power());
await running.poll(40000);
check('mode timeout keeps the detected running mode and allows retry', () => {
  assert.match(running.power().textContent, /点击启动为Steam桌面/);
  assert.match(running.find('err-bar').textContent, /尚未确认目标界面/);
  assert.equal(running.power().props.disabled, false);
  assert.equal(running.intervals.size, 0);
});
running.state.mode='desktop'; await running.click(running.status());
check('desktop running shows the requested opposite action',()=>{assert.match(running.status().textContent,/桌面Steam运行中/);assert.match(running.power().textContent,/点击启动为Steam大屏/)});
await running.click(running.power());
check('desktop-to-big-picture issues one idempotent mode request and no launch/stop',()=>{assert.equal(running.state.target,'bigpicture');assert.equal(running.counts.launch,0);assert.equal(running.counts.stop,0);assert.equal(running.power().props.disabled,true)});
running.state.mode='bigpicture';await running.poll();
check('only actual mode readback confirms the transition',()=>{assert.match(running.status().textContent,/大屏Steam运行中/);assert.equal(running.power().props.disabled,false);assert.equal(running.intervals.size,0)});
running.state.mode='unknown';await running.click(running.status());const switches=running.counts.switch;await running.click(running.power());
check('unknown mode retries detection without killing or guessing',()=>{assert.match(running.power().textContent,/重新识别Steam模式/);assert.equal(running.counts.switch,switches);assert.equal(running.counts.stop,0);assert.equal(running.power().props.disabled,false)});
running.state.mode='desktop';await running.click(running.status());running.state.mode='bigpicture';await running.click(running.power());
check('an external change that already reaches clicked target never toggles it back',()=>{assert.equal(running.counts.switch,switches);assert.equal(running.power().props.disabled,false)});
const readsBefore=running.counts.detect;await running.events();await running.eventTimers();
check('shared Steam window events coalesce into one visible read without idle polling',()=>{assert.equal(running.counts.detect,readsBefore+1);assert.equal(running.intervals.size,0);assert.equal(running.counts.stop,0)});
await running.visibility(false);const hiddenReads=running.counts.detect;await running.events();await running.eventTimers();
check('hidden page ignores Steam UI event reads',()=>assert.equal(running.counts.detect,hiddenReads));
running.destroy();
const hidden = await fixture(false);
await hidden.click(hidden.power()); await hidden.visibility(false);
check('Hiding YMCC during startup confirmation releases Steam controls and removes finite polling', () => { assert.equal(hidden.power().props.disabled,false); assert.equal(hidden.intervals.size,0); });
await hidden.visibility(true); await hidden.click(hidden.power()); hidden.state.live=true; await hidden.poll();
check('Steam power can retry after the hidden-window cancellation without reopening YMCC', () => { assert.equal(hidden.counts.launch,2); assert.equal(hidden.power().props.disabled,false); });
hidden.destroy();
const routed = await fixture(false,true);await routed.click(routed.power());await routed.activate(false);assert.equal(routed.intervals.size,0);await routed.activate(true);
check('KeepAlive page deactivation cannot leave startup busy permanently locked on return', () => { assert.equal(routed.power().props.disabled,false); });
await routed.click(routed.power());routed.state.live=true;await routed.poll();await routed.click(routed.power());await routed.activate(false);await routed.activate(true);
check('KeepAlive cancellation also releases an in-progress Steam-mode confirmation', () => { assert.equal(routed.power().props.disabled,false); assert.equal(routed.intervals.size,0); });routed.destroy();
const wake=await fixture(true);wake.state.detectFailure=true;await wake.click(wake.status());
check('temporary wake read failure retains last known running state with an honest stale marker',()=>{assert.match(wake.status().textContent,/运行中.*待复核/);assert.equal(wake.counts.launch,0);assert.equal(wake.counts.switch,0)});
wake.state.detectFailure=false;wake.state.powerReady=false;await wake.events();await wake.eventTimers();
check('Steam status can recover while unrelated power/input recovery is not ready',()=>{assert.match(wake.status().textContent,/大屏Steam运行中/);assert.doesNotMatch(wake.status().textContent,/待复核/);assert.equal(wake.counts.launch,0);assert.equal(wake.counts.switch,0)});
wake.listeners.get('ipc:power.resume-ready')?.();await wake.eventTimers();
check('resume-ready performs only an observational Steam refresh, never a remembered launch',()=>{assert.equal(wake.counts.launch,0);assert.equal(wake.counts.switch,0);assert.equal(wake.counts.stop,0)});wake.destroy();
const latePower=await fixture(true);latePower.state.mode='desktop';await latePower.events();await latePower.powerGate(false);await latePower.eventTimers();
check('late global power-gate loss does not discard a queued read-only wake event',()=>{assert.match(latePower.status().textContent,/桌面Steam运行中/);assert.equal(latePower.counts.launch,0);assert.equal(latePower.counts.switch,0)});latePower.destroy();
const slowWake=await fixture(true);slowWake.state.mode='desktop';slowWake.state.readDeferred=true;await slowWake.events();await slowWake.eventTimers();assert(slowWake.state.readRelease);await slowWake.powerGate(false);slowWake.state.readRelease();await flush();
check('late global power-gate loss does not invalidate an in-flight observational wake reply',()=>{assert.match(slowWake.status().textContent,/桌面Steam运行中/);assert.equal(slowWake.counts.launch,0);assert.equal(slowWake.counts.switch,0);assert.equal(slowWake.intervals.size,0)});slowWake.destroy();
const documentWake=await fixture(true);documentWake.state.uiVisible=false;documentWake.state.powerReady=false;
const beforeHiddenWake=documentWake.counts.detect;documentWake.listeners.get('ipc:power.resume-ready')?.();await documentWake.eventTimers();
check('hidden document does not query Steam on a wake edge',()=>assert.equal(documentWake.counts.detect,beforeHiddenWake));
documentWake.state.uiVisible=true;documentWake.state.mode='desktop';documentWake.documentListeners.get('visibilitychange')?.();await documentWake.eventTimers();
check('late document visibility restores Steam state even when aggregate powerReady stays false',()=>{assert.match(documentWake.status().textContent,/桌面Steam运行中/);assert.equal(documentWake.counts.detect,beforeHiddenWake+1);assert.equal(documentWake.counts.launch,0);assert.equal(documentWake.counts.switch,0);assert.equal(documentWake.intervals.size,0)});
documentWake.state.live=false;await documentWake.click(documentWake.status());
check('a confirmed Steam process exit still clears a previously retained running state',()=>assert.match(documentWake.status().textContent,/Steam未启动/));documentWake.state.detectFailure=true;await documentWake.click(documentWake.status());check('a stale last-confirmed stopped state is also explicitly marked pending verification',()=>assert.match(documentWake.status().textContent,/未启动.*待复核/));documentWake.destroy();
const restart=await fixture(true);await restart.visibility(false);await restart.visibility(true);await restart.events();await restart.eventTimers();
check('mount, cached view return, and repeated shown events never replay the Steam action',()=>{assert.equal(restart.counts.launch,0);assert.equal(restart.counts.switch,0);assert.equal(restart.counts.stop,0)});restart.destroy();
const modeLate=await fixture(true);modeLate.state.readDeferred=true;const modeFlight=modeLate.click(modeLate.power());await flush();await modeLate.visibility(false);modeLate.state.readRelease();await modeFlight;
check('late mode preflight after hiding cannot send a switch or keep controls locked',()=>{assert.equal(modeLate.counts.switch,0);assert.equal(modeLate.power().props.disabled,false);assert.equal(modeLate.intervals.size,0)});modeLate.destroy();
const late = await fixture(false);late.state.launchDeferred=true;const inFlight=late.click(late.power());await flush();assert.equal(late.counts.launch,1);await late.visibility(false);late.state.launchRelease();await inFlight;
check('Late original launcher completion after UI cancellation cannot re-arm an invisible busy loop', () => { assert.equal(late.power().props.disabled,false); assert.equal(late.intervals.size,0); });late.destroy();
const storm = await fixture(true);
const initialDetect = storm.counts.detect;
for (let i = 0; i < 1000; i++) storm.listeners.get('ipc:steam.uiStateChanged')?.();
await storm.advance(4999);
check('1000 window events cannot bypass the 5s automatic read gap', () => assert.equal(storm.counts.detect, initialDetect));
await storm.advance(1);
check('1000 events collapse to exactly one trailing read, without an interval', () => { assert.equal(storm.counts.detect, initialDetect + 1); assert.equal(storm.intervals.size, 0); });
storm.state.detectFailure = true;
for (let i=0;i<3;i++) { await storm.events(); await storm.eventTimers(); }
const exhaustedReads = storm.counts.detect;
for (let i=0;i<1000;i++) storm.listeners.get('ipc:steam.uiStateChanged')?.();
await storm.advance(60000);
check('three automatic failures stop discovery despite continued events', () => { assert.equal(storm.counts.detect, exhaustedReads); assert.equal(storm.timeouts.size, 0); });
storm.state.detectFailure = false;
await storm.click(storm.status());
check('manual state click renews an exhausted read budget', () => assert.equal(storm.counts.detect, exhaustedReads + 1));
await storm.events(); await storm.advance(4999);
check('manual reads also defer the next automatic read by five seconds', () => assert.equal(storm.counts.detect, exhaustedReads + 1));
await storm.visibility(false);
check('hiding cancels the trailing observation without starting a poll', () => { assert.equal(storm.timeouts.size,0); assert.equal(storm.intervals.size,0); }); storm.destroy();
const hard = await fixture(true);
await hard.click(hard.power());
// Initial manual preflight and confirmation have completed. Now hold the next read.
hard.state.readDeferred = true;
const pendingPoll = hard.poll(4000); await flush();
assert(hard.state.readRelease);
await hard.advance(36000);
check('40s hard deadline releases buttons even while the status RPC is pending', () => { assert.equal(hard.intervals.size,0); assert.equal(hard.power().props.disabled,false); });
hard.state.readRelease(); await pendingPoll;
check('late RPC completion cannot resurrect the expired confirmation timer', () => assert.equal(hard.intervals.size,0)); hard.destroy();
console.log(JSON.stringify({ suite: 'Steam inline power UI', passed: cases.length, cases, realSteamOperations: 0 }, null, 2));
