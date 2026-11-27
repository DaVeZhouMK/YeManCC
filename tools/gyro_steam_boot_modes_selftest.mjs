import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { runtimeBundle, outputDirectory, repo, require, fingerprints } from './boot_modes_fixture/harness.mjs';

const out = outputDirectory(process.argv.slice(2), 'gyro-steam-boot');
const vue = require('vue');
const { parse, compileScript, compileTemplate } = require('@vue/compiler-sfc');
const { transformSync } = require('esbuild');
const read = file => fs.readFileSync(path.join(repo, file), 'utf8');
const plain = value => JSON.parse(JSON.stringify(value));
const report = { suite: 'Steam gyro boot fields and unified transactions', passed: [], failed: [], hardwareOperations: 0, settings: 'existing in-memory boot runtime ports' };
async function check(name, run) {
  try { await run(); report.passed.push(name); console.log('PASS ' + name); }
  catch (error) { report.failed.push({ name, error: error.stack || String(error) }); console.error('FAIL ' + name + '\n' + error.stack); }
}
function evaluate(source, imports = {}) {
  const module = { exports: {} };
  vm.runInNewContext(transformSync(source, { loader: 'ts', format: 'cjs' }).code, {
    module, exports: module.exports, console, structuredClone,
    require(name) { if (name in imports) return imports[name]; throw Error('Unexpected import ' + name); },
  });
  return module.exports;
}
function canonical(motion) {
  assert.equal(motion.motionMode, 'off');
  assert.equal(motion.motionTrigger, null);
  assert.equal(motion.aimingSightsTrigger, null);
}
const gyro = evaluate(read('src/bridge/gyroPresetModel.ts'));
const cloneDeclaration = read('src/bridge/bootModeModel.ts').match(/^export const cloneBootValue = .*;$/m);
assert(cloneDeclaration, 'Actual boot clone declaration must be loaded');
const { cloneBootValue } = evaluate(cloneDeclaration[0]);
const { descriptor, errors } = parse(read('src/components/BootModeFields.vue'));
assert.deepEqual(errors, []);
const compiled = compileScript(descriptor, { id: 'steam-boot-fields' });
assert.deepEqual(compileTemplate({ source: descriptor.template.content, filename: 'BootModeFields.vue', id: 'steam-boot-fields' }).errors, []);
const banks = {
  fps: { motionMode: 'toggle', motionTrigger: 'RT', aimingSightsTrigger: 'LB', gyroMultiplier: 2.2 },
  racing: { motionMode: 'on', motionTrigger: '', outputStick: 'left', gyroMultiplier: 1.3 },
  custom: { motionMode: 'suppress', motionTrigger: 'RB', gyroMultiplier: 1.7 },
  steam: { motionMode: 'on', motionTrigger: 'LT', aimingSightsTrigger: 'RB', gyroMultiplier: 1.9, activePreset: 'fps' },
};
function fieldsFixture(motion) {
  const emitted = [], scope = vue.effectScope();
  const props = vue.reactive({ module: 'gyro', value: { motion: structuredClone(motion) }, disabled: false, persona: 'steamdeck', support: {}, apps: [] });
  const imports = {
    vue: { ...vue, onBeforeUnmount() {} },
    '@/bridge/gyroPresetModel': gyro,
    '@/bridge/bootModeModel': { cloneBootValue },
    '@/bridge/performanceControls': { CONTROL_POWER_SCHEMES: [], normalizeContinuousControls() { throw Error('Unexpected performance operation'); } },
    '@/bridge/screenTouchpads': { screenTouchpadDefaults: () => ({}), screenTouchpadProfile: value => value },
    '@/bridge/steamAppearance': { STEAM_APPEARANCE_THEMES: [] },
    '@/bridge/monitorHudModel': { switchMonitorHudTemplate() { throw Error('Unexpected monitor operation'); } },
  };
  for (const name of ['Dropdown', 'Slider', 'Toggle', 'ScreenTouchpadsSettings']) imports['./' + name + '.vue'] = { default: {} };
  const component = evaluate(compiled.content, imports).default;
  const state = scope.run(() => component.setup(props, { expose() {}, emit(event, value) { assert.equal(event, 'change'); emitted.push(plain(value)); props.value = value; } }));
  return { state, props, emitted, dispose: () => scope.stop() };
}
await check('Preset switching synchronizes both identities and excludes identity from remembered parameters', () => {
  const page = fieldsFixture({ preset: 'fps', activePreset: 'fps', enabled: true, ...banks.fps, presets: banks });
  try {
    page.state.set('motion.preset', 'steam');
    const motion = page.emitted.at(-1).motion;
    assert.equal(motion.preset, 'steam');
    assert.equal(motion.activePreset, 'steam');
    canonical(motion);
    assert.equal('activePreset' in motion.presets.fps, false);
    assert.deepEqual(motion.presets.racing, banks.racing);
    assert.deepEqual(motion.presets.custom, banks.custom);
    assert.equal(motion.outputMode, 'virtual-stick');
  } finally { page.dispose(); }
});
await check('Steam fields expose only power, link and preset, using effective identity', () => {
  const page = fieldsFixture({ preset: 'fps', activePreset: 'steam', motionMode: 'toggle', motionTrigger: 'LT' });
  try {
    assert.equal(page.state.get('motion.preset'), 'steam');
    assert.deepEqual(plain(page.state.fields.value).map(field => field.path), ['motion.enabled', 'motion.virtualPadLink', 'motion.preset']);
    assert.equal(page.emitted.length, 0);
  } finally { page.dispose(); }
});
await check('Stale Steam field events and power changes cannot restore local triggers', () => {
  const page = fieldsFixture({ preset: 'steam', activePreset: 'invalid', enabled: true, motionMode: 'on', motionTrigger: 'LT', aimingSightsTrigger: 'RB', presets: banks });
  try {
    for (const [field, value] of [['motion.motionMode', 'on'], ['motion.motionTrigger', 'LT'], ['motion.aimingSightsTrigger', 'RT'], ['motion.enabled', false], ['motion.virtualPadLink', false]]) {
      page.state.set(field, value);
      canonical(page.emitted.at(-1).motion);
      assert.deepEqual(page.emitted.at(-1).motion.presets.fps, banks.fps);
    }
  } finally { page.dispose(); }
});
await check('Returning from Steam restores FPS, racing and custom parameter banks without identity leakage', () => {
  const page = fieldsFixture({ ...banks.steam, preset: 'steam', activePreset: 'steam', enabled: true, presets: { ...banks, fps: { ...banks.fps, activePreset: 'steam' } } });
  try {
    for (const preset of ['fps', 'racing', 'custom']) {
      page.state.set('motion.preset', preset);
      const motion = page.emitted.at(-1).motion;
      assert.equal(motion.preset, preset);
      assert.equal(motion.activePreset, preset);
      assert.equal(motion.gyroMultiplier, banks[preset].gyroMultiplier);
      assert.equal(motion.motionMode, banks[preset].motionMode === 'suppress' ? 'on' : banks[preset].motionMode);
      assert.equal(motion.motionTrigger, banks[preset].motionMode === 'on' ? null : banks[preset].motionTrigger);
      page.state.set('motion.preset', 'steam');
      canonical(page.emitted.at(-1).motion);
    }
  } finally { page.dispose(); }
});
let runtimeNumber = 0;
async function runtime() { const directory = path.join(out, 'runtime-' + (++runtimeNumber)); fs.mkdirSync(directory, { recursive: true }); const result = await runtimeBundle(directory); result.ports.reset(); return result; }
const legacySteam = () => ({ enabled: true, outputMode: 'virtual-stick', preset: 'steam', activePreset: 'invalid', motionMode: 'toggle', motionTrigger: 'LT', aimingSightsTrigger: 'RB', gyroMultiplier: 2.4, presets: structuredClone(banks) });
await check('Unified draft save derives Steam startup intent from a valid fallback and preserves other banks', async () => {
  const { boot, ports } = await runtime();
  const context = await boot.loadBootModes({ editorOnly: true });
  const before = JSON.stringify(ports.settings.input);
  const document = context.document;
  document.global.input.persona = 'steamdeck';
  document.global.gyro = { motion: legacySteam() };
  const saved = await boot.saveBootModes(document, document.revision, context.initialSource);
  assert.equal(ports.settings.startupDesired.gyroPreset, 'steam');
  assert.equal(JSON.stringify(ports.settings.input), before);
  canonical(saved.global.gyro.motion);
  assert.deepEqual(saved.global.gyro.motion.presets.fps, banks.fps);
  assert.deepEqual(saved.global.gyro.motion.presets.racing, banks.racing);
  assert.deepEqual(saved.global.gyro.motion.presets.custom, banks.custom);
  assert.equal(ports.calls.filter(call => call.op === 'input.cas').length, 0);
});
await check('Unified save normalizes active, scene, remembered and named Steam snapshots without mutating the draft', async () => {
  const { boot, ports } = await runtime();
  const context = await boot.loadBootModes({ editorOnly: true });
  const document = context.document;
  document.scenes.steam.overrides.input = { ...document.global.input, persona: 'steamdeck', mouse: 'steamdeck' };
  document.scenes.steam.overrides.gyro = { motion: legacySteam() };
  document.scenes.steam.rememberedOverrides = { gyro: { motion: legacySteam() } };
  document.presets.steam = { legacy: { name: 'legacy', overrides: { gyro: { motion: legacySteam() } } } };
  const before = JSON.stringify(document);
  const saved = await boot.saveBootModes(document, document.revision, context.initialSource);
  for (const motion of [saved.scenes.steam.overrides.gyro.motion, saved.scenes.steam.rememberedOverrides.gyro.motion, saved.presets.steam.legacy.overrides.gyro.motion]) {
    canonical(motion);
    assert.equal(motion.activePreset, 'steam');
    assert.equal(motion.gyroMultiplier, 2.4);
    assert.deepEqual(motion.presets.fps, banks.fps);
  }
  assert.equal(JSON.stringify(document), before);
  const next = await boot.chooseBootMode('steam', saved, true);
  assert.equal(ports.settings.startupDesired.gyroPreset, 'steam');
  canonical(next.scenes.steam.overrides.gyro.motion);
});
await check('Actual startup application uses existing input CAS and stores canonical Steam activeSnapshot', async () => {
  const { boot, model, ports } = await runtime();
  const document = model.createBootModeDocument(await boot.captureBootModeSnapshot());
  document.scenes.steam.overrides.input = { ...document.global.input, persona: 'steamdeck', mouse: 'steamdeck' };
  document.scenes.steam.overrides.gyro = { motion: legacySteam() };
  ports.settings.extensions.bootModes = structuredClone(document);
  const otherProfiles = JSON.stringify([ports.settings.gameProfiles, ports.settings.games]);
  await boot.applyBootMode('steam', document, { startup: true });
  const calls = ports.calls.filter(call => call.op === 'input.cas');
  assert.equal(calls.length, 1);
  canonical(calls[0].value.patch.gyroMotion);
  assert.equal(calls[0].value.patch.gyroMotion.activePreset, 'steam');
  assert.equal(calls[0].value.patch.gyroMotion.enabled, true);
  assert.equal(calls[0].value.patch.gyroMotion.outputMode, 'virtual-stick');
  canonical(ports.settings.extensions.bootModes.activeSnapshot.gyro.motion);
  assert.deepEqual(ports.settings.input.gyroMotion.presets.fps, banks.fps);
  assert.equal(JSON.stringify([ports.settings.gameProfiles, ports.settings.games]), otherProfiles);
  boot.cancelBootModeDeferredMouse(); boot.cancelBootModeLaunches();
});
await check('Capture preserves the IMU lifecycle and does not restore inactive non-Steam tuning', async () => {
  const { boot, ports } = await runtime();
  for (const preset of ['fps', 'racing', 'custom']) {
    const motion = { ...legacySteam(), preset, activePreset: preset, motionMode: 'toggle', motionTrigger: 'RB', gyroMultiplier: 2.7 };
    ports.settings.input.gyroMotion = structuredClone(motion);
    const captured = await boot.captureBootModeSnapshot();
    assert.equal(captured.gyro.motion.motionMode, 'toggle');
    assert.equal(captured.gyro.motion.motionTrigger, 'RB');
    assert.equal(captured.gyro.motion.gyroMultiplier, 2.7);
    assert.deepEqual(captured.gyro.motion.presets[preset], motion.presets[preset]);
    assert.deepEqual(ports.settings.input.gyroMotion, motion);
  }
  ports.settings.input.gyroMotion = { ...legacySteam(), enabled: false, outputMode: 'disabled' };
  const captured = await boot.captureBootModeSnapshot();
  canonical(captured.gyro.motion);
  assert.equal(captured.gyro.motion.enabled, false);
  assert.equal(captured.gyro.motion.outputMode, 'disabled');
  assert.equal(ports.settings.input.gyroMotion.motionMode, 'toggle');
});
report.pass = report.failed.length === 0;
report.sourceHashes = fingerprints(['src/components/BootModeFields.vue', 'src/bridge/bootModes.ts', 'src/bridge/gyroPresetModel.ts', 'tools/gyro_steam_boot_modes_selftest.mjs', 'tools/boot_modes_fixture/harness.mjs', 'tools/boot_modes_fixture/runtimePorts.ts']);
fs.writeFileSync(path.join(out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({ suite: report.suite, passed: report.passed.length, failed: report.failed.length, evidence: path.join(out, 'report.json') }));
if (!report.pass) process.exitCode = 1;

