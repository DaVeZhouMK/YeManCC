import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(root, 'package.json'));
const vue = require('vue');
const { parse, compileScript, compileTemplate } = require('@vue/compiler-sfc');
const { transformSync } = require('esbuild');
const { renderToString } = createRequire(require.resolve('vue'))('@vue/server-renderer');
const plain = value => JSON.parse(JSON.stringify(value));
let checks = 0;
async function check(name, run) { await run(); checks++; console.log('PASS ' + name); }
function evaluate(source, imports = {}, globals = {}) {
  const module = { exports: {} };
  vm.runInNewContext(transformSync(source, { loader: 'ts', format: 'cjs' }).code, {
    module, exports: module.exports, console, structuredClone, performance: { now: () => 100 },
    require(name) { if (name in imports) return imports[name]; throw Error('Unexpected import ' + name); },
    ...globals,
  });
  return module.exports;
}
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const model = evaluate(read('src/bridge/gyroPresetModel.ts'));
const personas = evaluate(read('src/bridge/controllerPersona.ts'));
const targetPolicy = evaluate(read('src/bridge/virtualTargetPolicy.ts'));
const virtualOutputStatus = evaluate(read('src/bridge/virtualOutputStatus.ts'), { './virtualTargetPolicy': targetPolicy });
const contracts = evaluate(read('src/bridge/inputContracts.ts'), { './controllerPersona': personas });
const presentation = evaluate(read('src/bridge/gyroPresentation.ts'));
const { descriptor, errors } = parse(read('src/views/GyroMotionView.vue'));
assert.deepEqual(errors, []);
const script = compileScript(descriptor, { id: 'gyro-steam-preset' });
const template = compileTemplate({ source: descriptor.template.content, filename: 'GyroMotionView.vue', id: 'gyro-steam-preset' });
assert.deepEqual(template.errors, []);
const render = evaluate(template.code, { vue }).render;
function canonical(value) {
  assert.equal(value.motionMode, 'off');
  assert.equal(value.motionTrigger, null);
  assert.equal(value.aimingSightsTrigger, null);
}
for (const motionMode of ['on', 'off', 'toggle', 'suppress']) {
  await check('Steam preset normalizes legacy ' + motionMode + ' without mutating memory', () => {
    const presets = { steam: { motionMode, motionTrigger: ' LT ', aimingSightsTrigger: 'RB', gyroWeight: 2.2, gyroMultiplier: 1.7, gyroMode: 'legacy' }, fps: { motionMode: 'toggle', motionTrigger: 'RT', gyroWeight: 1.4 } };
    const before = JSON.stringify(presets);
    const params = model.gyroMotionParamsForPreset('steam', presets);
    canonical(params);
    assert.equal(params.gyroWeight, 2.2);
    assert.equal(params.gyroMultiplier, 1.7);
    assert.equal('gyroMode' in params, false);
    assert.equal(JSON.stringify(presets), before);
  });
}
await check('Steam default and explicit enable preserve power/output lifecycle', () => {
  canonical(model.gyroMotionParamsForPreset('steam', null));
  for (const bank of [undefined, { steam: { motionMode: 'toggle', motionTrigger: 'LB', aimingSightsTrigger: 'RT', gyroMultiplier: 2.1 } }]) {
    const motion = { activePreset: 'steam', preset: 'fps', enabled: false, outputMode: 'disabled', motionMode: 'on', motionTrigger: 'LT', aimingSightsTrigger: 'RB', gyroMultiplier: 1.8, presets: bank };
    const before = JSON.stringify(motion);
    const next = model.gyroMotionForEnable(motion);
    canonical(next);
    assert.equal(next.enabled, true);
    assert.equal(next.outputMode, 'virtual-stick');
    assert.equal(next.preset, 'steam');
    assert.equal(next.activePreset, 'steam');
    assert.equal(next.gyroMultiplier, bank ? 2.1 : 1.8);
    assert.equal(JSON.stringify(motion), before);
  }
});
await check('Effective preset prefers valid activePreset and falls back to preset', () => {
  for (const activePreset of [undefined, null, 'invalid', 'off']) assert.equal(model.isSteamControlledGyro({ activePreset, preset: 'steam' }), true);
  for (const activePreset of ['fps', 'racing', 'custom']) assert.equal(model.isSteamControlledGyro({ activePreset, preset: 'steam' }), false);
  assert.equal(model.isSteamControlledGyro({ activePreset: 'steam', preset: 'custom' }), true);
});
await check('Non-Steam trigger encoding and remembered tuning stay independent', () => {
  for (const preset of ['fps', 'racing', 'custom']) for (const [motionMode, mode, trigger] of [['on', 'on', null], ['off', 'off', 'RB'], ['toggle', 'toggle', 'RB'], ['suppress', 'on', 'RB']]) {
    const params = model.gyroMotionParamsForPreset(preset, { [preset]: { motionMode, motionTrigger: ' RB ', aimingSightsTrigger: 'LT', gyroMultiplier: 2.4 } });
    assert.equal(params.motionMode, mode);
    assert.equal(params.motionTrigger, trigger);
    assert.equal(params.aimingSightsTrigger, 'LT');
    assert.equal(params.gyroMultiplier, 2.4);
  }
});

const componentNames = ['GyroInputOutputPreview', 'GameProfileRemoveDialog', 'GyroSimulationWindow', 'GameProfileModeBanner', 'IntegratedPage', 'UsbipUpgradeStatus', 'SegButton', 'Slider', 'Toggle', 'Dropdown', 'InlineIcon'];
function fixture(input) {
  let savedInput = structuredClone(input), ticket = 0;
  const writes = [], timers = new Map();
  const scope = vue.effectScope();
  const components = Object.fromEntries(componentNames.map(name => [name, {
    name, inheritAttrs: false,
    setup(_, { attrs, slots }) { return () => vue.h('div', { 'data-component': name, 'data-label': attrs.label, 'data-admitted': attrs.admitted, 'data-connected': attrs.connected }, slots.default?.()); },
  }]));
  const editor = () => ({ input: structuredClone(savedInput), target: { exe: '', pid: 0, processCreated: '', generation: 1 }, ownerEpoch: 1, gameName: '' });
  const imports = {
    vue: { ...vue, onMounted() {}, onActivated() {}, onDeactivated() {}, onBeforeUnmount() {} },
    '@/bridge/controllerPersona': personas,
    '@/bridge/gyroPresetModel': model,
    '@/bridge/inputContracts': contracts,
    '@/bridge/gyroPresentation': presentation,
    '@/bridge/virtualOutputStatus': virtualOutputStatus,
    '@/bridge/gameInputEditor': { readGameInputEditor: async () => editor(), onGameInputEditorChanged: () => () => {} },
    '@/bridge/gyroPresetSave': { async saveGyroEditorPatch(owner, patch, isCurrent) {
      assert.equal(isCurrent(), true);
      writes.push(plain(patch));
      savedInput = { ...savedInput, ...plain(patch), revision: savedInput.revision + 1, outputTarget: { ...savedInput.outputTarget, ...plain(patch.outputTarget) } };
      return { editor: editor(), result: { ok: true, value: structuredClone(savedInput), revision: savedInput.revision } };
    } },
    '@/bridge/ipc': { on: () => () => {}, setGyroTelemetryInterest() {}, async invoke(method) {
      if (method === 'input.shortcutRuntime.get') return { active: false };
      if (method === 'input.shortcutRuntime.clear') return {};
      throw Error('Unexpected IPC ' + method);
    } },
    '@/bridge/uiLifecycle': { isUiVisible: () => true, onUiVisibilityChange: () => () => {} },
    '@/bridge/performanceSchedule': { loadGameCustomConfig() { throw Error('Unexpected game IO'); }, removeGameCustomProfile() { throw Error('Unexpected game IO'); }, restorePerformanceScheduleIfConfigured() { throw Error('Unexpected game IO'); } },
    '@/bridge/quickActionLock': { tryAcquireQuickAction: () => null },
    '@/gamepad/focus': { focusGamepadElement() {} },
  };
  for (const name of componentNames) imports['@/components/' + name + '.vue'] = { default: components[name] };
  const component = evaluate(script.content, imports, { window: { setTimeout(callback) { timers.set(++ticket, callback); return ticket; }, clearTimeout(id) { timers.delete(id); } } }).default;
  const state = scope.run(() => component.setup({}, { expose() {} }));
  return { state, writes, timers, dispose: () => scope.stop(), html: () => renderToString(vue.createSSRApp({ components, setup: () => ({ ...state }), render })) };
}
function input(motion) {
  return { revision: 1, outputTarget: { persona: 'steamdeck', buttonMappingEnabled: true, gyroEnabled: true }, gyroMotion: { enabled: true, provider: 'fixture', outputMode: 'virtual-stick', ...motion } };
}
for (const motionMode of ['on', 'off', 'toggle', 'suppress']) {
  await check('Actual page loads/saves legacy Steam ' + motionMode + ' as off/null', async () => {
    const other = { motionMode: 'toggle', motionTrigger: 'RB', aimingSightsTrigger: 'LT', gyroMultiplier: 2.2, autoCalibrate: true };
    const page = fixture(input({ preset: 'steam', activePreset: 'steam', motionMode, motionTrigger: 'LT', aimingSightsTrigger: 'RB', presets: { steam: { motionMode, motionTrigger: 'LT', aimingSightsTrigger: 'RB', gyroMultiplier: 1.9 }, fps: other } }));
    try {
      await page.state.load();
      assert.equal(page.writes.length, 0);
      assert.equal(page.timers.size, 0);
      assert.equal(page.state.motionMode.value, 'off');
      assert.equal(page.state.motionTrigger.value, '');
      assert.equal(page.state.aimingSightsTrigger.value, '');
      await page.state.saveNow();
      assert.equal(page.writes.length, 1);
      canonical(page.writes[0].gyroMotion);
      canonical(page.writes[0].gyroMotion.presets.steam);
      assert.deepEqual(page.writes[0].gyroMotion.presets.fps, other);
      assert.equal(page.writes[0].gyroMotion.enabled, true);
      assert.equal(page.writes[0].gyroMotion.outputMode, 'virtual-stick');
    } finally { page.dispose(); }
  });
}
await check('Actual page switches independent banks and resets Steam without LT fallback', async () => {
  const page = fixture(input({ preset: 'fps', activePreset: 'fps', motionMode: 'toggle', motionTrigger: 'RT', aimingSightsTrigger: 'LB', gyroMultiplier: 2.3 }));
  try {
    await page.state.load();
    const before = plain(page.state.capturePresetParams());
    page.state.applyPreset('steam');
    page.state.ensureMotionTrigger();
    canonical(page.state.capturePresetParams());
    page.state.markCustom();
    assert.equal(page.state.preset.value, 'steam');
    assert.equal(page.state.activePreset.value, 'steam');
    page.state.resetGyroDefaults();
    canonical(page.state.capturePresetParams());
    await page.state.saveNow();
    page.state.applyPreset('fps');
    assert.deepEqual(plain(page.state.capturePresetParams()), before);
    assert.equal(page.state.outputMode.value, 'virtual-stick');
  } finally { page.dispose(); }
});
await check('Actual page hides local controls and shares the same settings in simulation', async () => {
  const page = fixture(input({ preset: 'fps', activePreset: 'steam', motionMode: 'on' }));
  try {
    await page.state.load();
    assert.equal(page.state.preset.value, 'steam');
    page.state.advancedOpen.value = true;
    page.state.simulationOpen.value = true;
    page.state.simulationAdmitted.value = true;
    const html = await page.html();
    assert(html.includes('请在 Steam 内设置触发方式'));
    for (const label of ['触发方式', '触发键', '变速触发键', '输入方式', '陀螺仪权重【倍率】', '灵敏度', '输出形状', '水平反转']) assert(!html.includes('>' + label + '<') && !html.includes('data-label="' + label + '"'), label);
    assert(html.includes('静态校准'));
    assert(html.includes('检测体感来源'));
    assert(html.includes('data-component="GyroSimulationWindow" data-admitted="false"'));
    assert.equal(descriptor.template.content.match(/<Teleport\b/g).length, 1);
    assert(descriptor.template.content.includes(':to="simulationSettings || \'body\'"'));
    page.state.applyPreset('custom');
    assert((await page.html()).includes('变速触发键'));
  } finally { page.dispose(); }
});
await check('Steam mapped=false keeps proven stationary IMU connected', async () => {
  const page = fixture(input({ preset: 'steam', activePreset: 'invalid', motionMode: 'on' }));
  try {
    await page.state.load();
    assert.equal(page.state.preset.value, 'steam');
    const raw = { sequence: 1, timestampUtc: '2026-11-24T00:00:00Z', sensorPresent: true, gyro: { x: 0, y: 0, z: 0 }, accel: { x: 0, y: 0, z: 0 }, motionManager: { mapped: false, triggered: false, velocityMode: false, gyroShort: { x: 0, y: 0 } }, motionAdmission: 'safe-zero' };
    const normalized = contracts.normalizeGyroTelemetry(raw);
    assert(normalized);
    assert.equal(normalized.motionManager.mapped, false);
    assert.equal(page.state.hasGyroSensorEvidence(normalized), true);
    page.state.telemetry({ detail: raw });
    page.state.applyTelemetry(normalized, 100);
    assert.equal(page.state.gyroConnected.value, true);
    assert.equal(page.state.stickPreviewAdmitted.value, false);
    assert.equal(page.state.simulationAdmitted.value, false);
    assert.equal(page.state.gyroPowerState.value, 'on');
  } finally { page.dispose(); }
});
console.log(JSON.stringify({ suite: 'Steam gyro preset/page', passed: checks, settingsWrites: 'in-memory only', hardwareOperations: 0 }));

