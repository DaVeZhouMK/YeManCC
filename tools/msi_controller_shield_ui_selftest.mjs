// Pure in-memory IPC/Vue contract test. No OEM, services, tasks, registry or
// device operations; compile only the actual MSI bridge/UI slices from source.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(root, 'package.json'));
const vue = require('vue');
const { parse, compileScript } = require('vue/compiler-sfc');
const { transformSync } = require('esbuild');
const bridgeSource = fs.readFileSync(path.join(root, 'src/bridge/yeman.ts'), 'utf8');
const settingsSource = fs.readFileSync(path.join(root, 'src/views/SettingsView.vue'), 'utf8');
const cases = [];
async function check(name, run) { await run(); cases.push(name); }
const plain = value => JSON.parse(JSON.stringify(value));
function slice(source, start, end) {
  const from = source.indexOf(start);
  const to = source.indexOf(end, from + start.length);
  assert.ok(from >= 0 && to > from, `Missing source boundary: ${start}`);
  return source.slice(from, to);
}
function evaluate(source, imports = {}, globals = {}) {
  const module = { exports: {} };
  vm.runInNewContext(transformSync(source, { loader: 'ts', format: 'cjs' }).code, {
    module, exports: module.exports, Error,
    require(name) {
      if (name === 'vue') return vue;
      if (name in imports) return imports[name];
      throw Error(`Unexpected import (real bridge blocked): ${name}`);
    },
    ...globals,
  }, { timeout: 1000 });
  return module.exports;
}
const bridgeSlice = slice(bridgeSource, 'export interface MsiControllerShieldState', '// ── 更新加速器');
const stateSlice = slice(settingsSource, '// ── 微星可选前端屏蔽', '// ── 更新加速器');
const toggleSlice = slice(settingsSource, 'async function onMsiControllerShieldToggle(', 'async function onUpdateAccelToggle(');
const settingsParsed = parse(settingsSource, { filename: 'SettingsView.vue' });
assert.equal(settingsParsed.errors.length, 0);
function templateEntries(node, ancestors = []) {
  return [{ node, ancestors }, ...(node.children || []).flatMap(child => templateEntries(child, [...ancestors, node]))];
}
const cardEntries = templateEntries(settingsParsed.descriptor.template.ast).filter(({ node }) =>
  node.type === 1 && node.tag === 'section' && node.props.some(prop => prop.name === 'class' && prop.value?.content.split(/\s+/).includes('msi-controller-shield-card')));
assert.equal(cardEntries.length, 1, 'Settings must mount exactly one real MSI shield card');
const cardEntry = cardEntries[0];
assert.ok(cardEntry.ancestors.some(node => node.tag === 'template' && node.props?.some(prop => prop.type === 7 && prop.name === 'slot' && prop.arg?.content === 'device')),
  'MSI shield card must remain in the actual settings device slot');
assert.ok(cardEntry.node.props.some(prop => prop.type === 7 && prop.name === 'if' && prop.exp?.content === 'msiControllerShield.supported'),
  'The native supported capability must gate the real card');
const cardSlice = cardEntry.node.loc.source;

function snapshot(patch = {}) {
  return {
    ok: true, supported: true, family: 'MsiClaw', deviceClass: 'handheld',
    enabled: false, effective: false, applied: false, pending: false,
    lastOutcome: 'applied-or-restored', lastOutcomeKnown: true, lastOutcomeOk: true,
    requestedEpoch: 10, completedEpoch: 10, shortcutOwnerActive: false, isolationOwnerActive: patch.enabled === true || patch.shortcutOwnerActive === true,
    mode: 'msi-controller-only', reason: 'safe-read',
    targetTasks: ['MSI_Center_M_Server', 'MSI_Center_M_Updater'],
    targetServices: [], targetProcesses: ['MSI_Center_M_Server', 'MSI Center M'],
    protectedServices: ['MSI Foundation Service'],
    protectedPaths: ['YMCC', 'InputHost', 'HidHide', 'physical-controller-driver', 'fan-thermal-telemetry'],
    servicePolicy: 'preserve-unverified-dependencies', controllerOnlyServiceIdentified: false,
    protectedServiceObservation: { name: 'MSI Foundation Service', running: true, controllerOnlyVerified: false },
    serviceInventory: { observedRequestedEpoch: 8, timestamp: '2026-10-13T08:00:00Z', observed: [{ name: 'MSI Foundation Service', state: 'running' }], controllerOnlyVerified: false },
    physicalControllerVerified: false,
    ...patch,
  };
}
const pending = (patch = {}) => snapshot({ enabled: true, effective: true, applied: false,
  pending: true, lastOutcome: 'pending', lastOutcomeKnown: false, lastOutcomeOk: false,
  requestedEpoch: 11, completedEpoch: 10, ...patch });
function port() {
  const calls = [], gets = [], sets = [];
  const bridge = evaluate(bridgeSlice, {}, {
    async invoke(command, args, options) {
      calls.push({ command, args: plain(args), options: options && plain(options) });
      assert.ok(command === 'msiControllerShield.get' || command === 'msiControllerShield.set');
      const queue = command.endsWith('.get') ? gets : sets;
      assert.ok(queue.length, `Unexpected IPC call: ${command}`);
      const response = queue.shift();
      if (response instanceof Error) throw response;
      return typeof response === 'function' ? await response() : structuredClone(response);
    },
  });
  return { bridge, calls, gets, sets };
}
function element(type, text = '') {
  return { type, text, props: {}, parent: null, children: [],
    get textContent() { return (this.type === '#comment' ? '' : this.text) + this.children.map(node => node.textContent).join(''); },
  };
}
const renderer = vue.createRenderer({
  createElement: type => element(type), createText: text => element('#text', text), createComment: text => element('#comment', text),
  setText: (node, text) => { node.text = text; },
  setElementText: (node, text) => { node.text = text; node.children = []; },
  parentNode: node => node.parent,
  nextSibling: node => node.parent?.children[node.parent.children.indexOf(node) + 1] || null,
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
  patchProp: (node, key, _old, value) => { node.props[key] = value; },
});
const toggleStub = {
  props: ['modelValue', 'disabled', 'color'], emits: ['update:modelValue'],
  setup(props, { emit }) {
    return () => vue.h('button', { 'data-test': 'shield-toggle', disabled: props.disabled,
      onClick: () => { if (!props.disabled) emit('update:modelValue', !props.modelValue); } }, '屏蔽请求开关');
  },
};
const iconStub = { setup: () => () => vue.h('span') };
const componentSource = `<script setup lang="ts">
import { ref, computed } from 'vue';
import Toggle from '@/components/Toggle.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import { msiControllerShieldGet, msiControllerShieldSet, type MsiControllerShieldState } from '@/bridge/yeman';
const errMsg = ref('');
${stateSlice}
${toggleSlice}
defineExpose({ msiControllerShield, msiShieldBusy, msiShieldStatus, msiShieldSnapshotCurrent,
  msiShieldExecutionStatus, refreshMsiControllerShield, onMsiControllerShieldToggle });
</script><template>${cardSlice}</template>`;
const { descriptor, errors } = parse(componentSource, { filename: 'MsiControllerShieldSelftest.vue' });
assert.equal(errors.length, 0);
const compiledUi = compileScript(descriptor, { id: 'msi-controller-shield-selftest', inlineTemplate: true,
  templateOptions: { compilerOptions: { hoistStatic: false } } }).content;
function harness(p = port()) {
  const component = evaluate(compiledUi, {
    '@/bridge/yeman': p.bridge,
    '@/components/Toggle.vue': { __esModule: true, default: toggleStub },
    '@/components/InlineIcon.vue': { __esModule: true, default: iconStub },
  }).default;
  const container = element('root');
  const app = renderer.createApp(component);
  const ui = app.mount(container);
  function nodes(node = container) { return [node, ...node.children.flatMap(child => nodes(child))]; }
  return { p, ui, app, container, text: () => container.textContent, nodes,
    shieldToggle: () => nodes().find(node => node.props['data-test'] === 'shield-toggle'),
    async read(response) { p.gets.push(response); await ui.refreshMsiControllerShield(); await vue.nextTick(); },
    async set(enabled, response) { p.sets.push(response); await ui.onMsiControllerShieldToggle(enabled); await vue.nextTick(); },
  };
}

await check('get/set preserve identical coherent snapshot and all safety evidence', async () => {
  const p = port(), raw = snapshot({ enabled: true, effective: true, applied: true, shortcutOwnerActive: true });
  p.gets.push(raw); p.sets.push(raw);
  const get = await p.bridge.msiControllerShieldGet();
  const set = await p.bridge.msiControllerShieldSet(true);
  assert.deepEqual(plain(get), raw);
  assert.deepEqual(plain(set), raw);
  assert.deepEqual(p.calls, [
    { command: 'msiControllerShield.get', args: {}, options: undefined },
    { command: 'msiControllerShield.set', args: { enabled: true }, options: { timeoutMs: 5000 } },
  ]);
});
await check('normalization requires explicit ok and does not invent dependency or owner evidence', async () => {
  const p = port();
  for (const raw of [null, {}, { enabled: true }, { ok: 'true', supported: 1 }]) {
    p.gets.push(raw); p.sets.push(raw);
    for (const state of [await p.bridge.msiControllerShieldGet(), await p.bridge.msiControllerShieldSet(true)]) {
      assert.equal(state.ok, false);
      assert.equal(state.applied, false);
      assert.equal(state.shortcutOwnerActive, undefined);
      assert.equal(state.isolationOwnerActive, undefined);
      assert.equal(state.servicePolicy, undefined);
      assert.equal(state.controllerOnlyServiceIdentified, undefined);
      assert.ok(state.reason);
    }
  }
});
await check('arrays/optional observation objects normalize safely without losing nested evidence', async () => {
  const p = port();
  p.gets.push(snapshot({ targetServices: ['observed-only', 1, null], targetProcesses: 'not-an-array',
    protectedServiceObservation: [], serviceInventory: null }));
  const state = await p.bridge.msiControllerShieldGet();
  assert.deepEqual(plain(state.targetServices), ['observed-only']);
  assert.deepEqual(plain(state.targetProcesses), []);
  assert.equal(state.protectedServiceObservation, undefined);
  assert.equal(state.serviceInventory, undefined);
});
await check('get transport failures are failures; set transport failures propagate', async () => {
  const p = port(); p.gets.push(new Error('offline')); p.sets.push(new Error('write-timeout'));
  const state = await p.bridge.msiControllerShieldGet();
  assert.equal(state.ok, false); assert.equal(state.reason, 'offline');
  await assert.rejects(p.bridge.msiControllerShieldSet(false), /write-timeout/);
});
await check('loading makes no enabled assumption; only a supported MSI card is visible', async () => {
  const h = harness();
  assert.equal(h.ui.msiControllerShield.enabled, false);
  assert.equal(h.shieldToggle(), undefined);
  await h.read(snapshot({ supported: false, family: 'AsusRogAlly' }));
  assert.equal(h.shieldToggle(), undefined);
  await h.read(snapshot());
  assert.ok(h.shieldToggle());
  assert.match(h.text(), /微星屏蔽 关闭/);
  h.app.unmount();
});
await check('known native admission keeps the initial failed read recoverable without claiming execution', async () => {
  const h = harness();
  await h.read({ ok: false, supported: true, reason: 'snapshot-unavailable' });
  assert.ok(h.shieldToggle());
  assert.equal(h.ui.msiShieldSnapshotCurrent, false);
  assert.equal(h.ui.msiShieldExecutionStatus, '总体 待确认');
  assert.match(h.text(), /读取失败，请重新进入设置/);
  assert.doesNotMatch(h.text(), /总体 OK/);
  await h.read(snapshot({ enabled: true, applied: true }));
  assert.equal(h.ui.msiShieldExecutionStatus, '总体 OK（屏蔽已执行）');
  assert.equal(h.ui.msiShieldStatus, '');
  assert.deepEqual(h.p.calls.map(call => call.command), ['msiControllerShield.get', 'msiControllerShield.get']);
  h.app.unmount();
});
await check('unknown initial transport failure never invents MSI support and a later entry can recover', async () => {
  const h = harness();
  await h.read(new Error('initial-offline'));
  assert.equal(h.shieldToggle(), undefined);
  await h.read(snapshot());
  assert.ok(h.shieldToggle());
  await h.read(snapshot({ supported: false, family: 'MsiClaw', deviceClass: 'desktop' }));
  assert.equal(h.shieldToggle(), undefined);
  h.app.unmount();
});
await check('pending, retrying and mismatched epochs stay in progress', async () => {
  const h = harness();
  for (const state of [pending(), pending({ lastOutcome: 'retrying' }), snapshot({ enabled: true, applied: true, requestedEpoch: 11 })]) {
    await h.read(state);
    assert.equal(h.ui.msiShieldExecutionStatus, '总体 执行中');
    assert.match(h.text(), /按键效果 待验证/);
    assert.doesNotMatch(h.text(), /总体 OK/);
  }
  h.app.unmount();
});
await check('retry exhaustion is a failure even with pending and applied set', async () => {
  const h = harness(); await h.read(pending({ lastOutcome: 'retry-exhausted', applied: true }));
  assert.equal(h.ui.msiShieldExecutionStatus, '总体 执行失败');
  h.app.unmount();
});
await check('page reload completes a pending operation without a manual refresh button or write', async () => {
  const h = harness(); await h.read(pending());
  const count = h.p.calls.length;
  await Promise.resolve(); await vue.nextTick();
  assert.equal(h.p.calls.length, count);
  h.p.gets.push(snapshot({ enabled: true, effective: true, applied: true }));
  await h.ui.refreshMsiControllerShield(); await vue.nextTick();
  assert.equal(h.p.calls.length, count + 1);
  assert.equal(h.p.calls.at(-1).command, 'msiControllerShield.get');
  assert.equal(h.ui.msiShieldExecutionStatus, '总体 OK（屏蔽已执行）');
  assert.match(h.text(), /按键效果 待验证/);
  h.app.unmount();
});
await check('completed toggle is concise with no duplicate success message', async () => {
  const h = harness(); await h.read(snapshot());
  await h.set(true, snapshot({ enabled: true, effective: true, applied: true }));
  assert.equal(h.ui.msiShieldExecutionStatus, '总体 OK（屏蔽已执行）');
  assert.equal(h.ui.msiShieldStatus, '');
  assert.match(h.text(), /微星屏蔽 开启/);
  h.app.unmount();
});
await check('generic completion or explicitly unknown/failed outcome never implies applied', async () => {
  const h = harness();
  for (const patch of [{ applied: false }, { lastOutcomeKnown: false }, { lastOutcomeOk: false }]) {
    await h.read(snapshot({ enabled: true, applied: true, ...patch }));
    assert.equal(h.ui.msiShieldExecutionStatus, '总体 待确认');
  }
  h.app.unmount();
});
await check('off completes only with applied false and both owners released', async () => {
  const h = harness(); await h.read(snapshot({ enabled: true, applied: true }));
  await h.set(false, snapshot());
  assert.equal(h.ui.msiShieldExecutionStatus, '总体 OK（屏蔽已关闭）');
  assert.match(h.text(), /前端未运行时可手动启动/);
  await h.read(snapshot({ applied: true }));
  assert.equal(h.ui.msiShieldExecutionStatus, '总体 待确认');
  assert.doesNotMatch(h.text(), /前端未运行时可手动启动/);
  h.app.unmount();
});
await check('remaining shortcut or isolation ownership is visible without claiming complete recovery', async () => {
  const h = harness();
  for (const patch of [{ shortcutOwnerActive: true }, { isolationOwnerActive: true }]) {
    await h.read(snapshot(patch));
    assert.equal(h.ui.msiShieldExecutionStatus, '总体 仍在屏蔽');
    assert.doesNotMatch(h.text(), /总体 OK|前端未运行时可手动启动/);
  }
  await h.read(snapshot({ shortcutOwnerActive: true }));
  assert.match(h.text(), /专用键接管 开启/);
  h.app.unmount();
});
await check('missing ownership or completion remains unknown', async () => {
  const h = harness();
  for (const state of [snapshot({ shortcutOwnerActive: undefined }), snapshot({ isolationOwnerActive: undefined }), { ok: true, supported: true, enabled: false }]) {
    await h.read(state);
    assert.equal(h.ui.msiShieldExecutionStatus, '总体 待确认');
  }
  assert.match(h.text(), /专用键接管 待确认/);
  h.app.unmount();
});
await check('read failures retain card and requested switch but hide stale success', async () => {
  const h = harness();
  for (const error of [{ ok: false, reason: 'read-failed' }, new Error('read-offline')]) {
    await h.read(snapshot({ enabled: true, applied: true }));
    await h.read(error);
    assert.equal(h.ui.msiControllerShield.enabled, true);
    assert.ok(h.shieldToggle());
    assert.equal(h.ui.msiShieldSnapshotCurrent, false);
    assert.equal(h.ui.msiShieldExecutionStatus, '总体 待确认');
    assert.equal(h.ui.msiShieldStatus, '读取失败，请重新进入设置');
    assert.match(h.text(), /专用键接管 待确认/);
    assert.equal(h.ui.msiShieldBusy, false);
  }
  h.app.unmount();
});
await check('rejected writes use saved request only when supported, never infer inverse request', async () => {
  const h = harness();
  for (const supported of [true, false]) for (const enabled of [true, false]) {
    await h.read(snapshot({ enabled: !enabled }));
    await h.set(enabled, { ok: false, supported, enabled, reason: 'settings-revision-conflict' });
    assert.equal(h.ui.msiControllerShield.enabled, supported ? enabled : !enabled);
    assert.equal(h.ui.msiShieldSnapshotCurrent, false);
    assert.equal(h.ui.msiShieldExecutionStatus, '总体 待确认');
    assert.equal(h.ui.msiShieldStatus, '设置失败，请重新进入设置');
    assert.ok(h.shieldToggle());
  }
  h.app.unmount();
});
await check('mismatched successful response uses readback and reports unchanged switch', async () => {
  const h = harness(); await h.read(snapshot());
  await h.set(true, snapshot());
  assert.equal(h.ui.msiControllerShield.enabled, false);
  assert.equal(h.ui.msiShieldStatus, '开关未更新，请重新进入设置');
  assert.equal(h.ui.msiShieldExecutionStatus, '总体 待确认');
  h.app.unmount();
});
await check('set timeout retains request until a later successful refresh', async () => {
  const h = harness(); await h.read(snapshot()); await h.set(true, new Error('write-timeout'));
  assert.equal(h.ui.msiControllerShield.enabled, false);
  assert.equal(h.ui.msiShieldSnapshotCurrent, false);
  assert.equal(h.ui.msiShieldStatus, '设置失败，请重新进入设置');
  await h.read(snapshot({ enabled: true, effective: true, applied: true }));
  assert.equal(h.ui.msiShieldExecutionStatus, '总体 OK（屏蔽已执行）');
  h.app.unmount();
});
await check('refresh and toggle serialize while a request is in flight', async () => {
  const h = harness(); await h.read(snapshot());
  let finish;
  h.p.sets.push(() => new Promise(resolve => { finish = resolve; }));
  const inflight = h.ui.onMsiControllerShieldToggle(true);
  await vue.nextTick();
  const count = h.p.calls.length;
  assert.equal(h.shieldToggle().props.disabled, true);
  await h.ui.refreshMsiControllerShield(); await h.ui.onMsiControllerShieldToggle(false);
  assert.equal(h.p.calls.length, count);
  finish(snapshot({ enabled: true, applied: true })); await inflight;
  assert.equal(h.ui.msiShieldBusy, false);
  h.app.unmount();
});
await check('older successful IPC without epoch metadata still supports execution status', async () => {
  const h = harness(); const state = snapshot({ enabled: true, applied: true });
  for (const field of ['requestedEpoch', 'completedEpoch', 'lastOutcomeKnown', 'lastOutcomeOk']) delete state[field];
  await h.read(state);
  assert.equal(h.ui.msiShieldExecutionStatus, '总体 OK（屏蔽已执行）');
  assert.match(h.text(), /基础服务 待确认/);
  h.app.unmount();
});
await check('service state requires matching operation and a known service-state read', async () => {
  const h = harness();
  const observation = { observedRequestedEpoch: 10, stateRead: true, serviceState: 1 };
  await h.read(snapshot({ enabled: true, applied: true, protectedServiceObservation: observation }));
  assert.match(h.text(), /基础服务 关闭（执行时）/);
  await h.read(snapshot({ protectedServiceObservation: { ...observation, serviceState: 4 } }));
  assert.match(h.text(), /基础服务 开启（执行时）/);
  for (const patch of [{ observedRequestedEpoch: 9 }, { observedRequestedEpoch: undefined }, { stateRead: false }]) {
    await h.read(snapshot({ protectedServiceObservation: { ...observation, ...patch } }));
    assert.match(h.text(), /基础服务 待确认/);
  }
  await h.read(pending({ protectedServiceObservation: observation }));
  assert.match(h.text(), /基础服务 待确认/);
  h.app.unmount();
});
await check('physical-controller report alone never verifies OEM key suppression', async () => {
  const h = harness(); await h.read(snapshot({ enabled: true, applied: true, physicalControllerVerified: true }));
  assert.match(h.text(), /按键效果 待验证/);
  assert.doesNotMatch(h.text(), /按键效果 OK|Steam.*通过/);
  h.app.unmount();
});
await check('the entire shield card stays short and excludes implementation prose', async () => {
  const h = harness(); await h.read(snapshot({ enabled: true, applied: true }));
  const text = h.text().replace(/\s+/g, ' ').trim();
  assert.ok(text.length <= 150, text);
  assert.equal(h.nodes().filter(node => node.type === 'p').length, 3);
  assert.equal(h.nodes().some(node => node.type === 'details'), false);
  assert.equal(h.nodes().filter(node => node.type === 'button').length, 1);
  assert.doesNotMatch(text, /刷新状态/);
  assert.doesNotMatch(text, /lease|owner|effective|applied|inventory|Foundation|请求 ≠/);
  assert.match(text, /重启 无需/);
  h.app.unmount();
});
await check('refresh keeps detailed evidence in state without fresh service queries', async () => {
  const h = harness(); const raw = snapshot(); await h.read(raw);
  assert.deepEqual(plain(h.ui.msiControllerShield.serviceInventory), raw.serviceInventory);
  assert.deepEqual(plain(h.ui.msiControllerShield.protectedServiceObservation), raw.protectedServiceObservation);
  assert.deepEqual(h.p.calls.map(call => call.command), ['msiControllerShield.get']);
  h.app.unmount();
});
await check('source integration uses manual getter with no polling or unrelated input writes', async () => {
  assert.equal((settingsSource.match(/await msiControllerShieldGet\(\)/g) || []).length, 1);
  assert.match(slice(settingsSource, 'async function load()', 'async function saveAutoClose('), /await refreshMsiControllerShield\(\);/);
  assert.doesNotMatch(stateSlice + toggleSlice + cardSlice, /setInterval|setTimeout|doubleB|LB\+RB|shell\.|registry\.|invoke\(/);
  const parsed = parse(settingsSource, { filename: 'SettingsView.vue' });
  assert.equal(parsed.errors.length, 0);
  compileScript(parsed.descriptor, { id: 'settings-full-syntax-check', inlineTemplate: true });
});
console.log('PASS msi_controller_shield_ui_selftest: ' + cases.length + ' cases (mock IPC/Vue only)');
