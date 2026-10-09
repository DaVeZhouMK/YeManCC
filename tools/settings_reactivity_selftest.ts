// Regression for structuredClone failures after saving Vue-backed settings.
// All IPC is memory-only and fail-closed: no real files, registry or hardware.
import { isProxy, reactive, readonly, ref, shallowReactive } from 'vue';
import './mock-shell';
import assert from 'node:assert/strict';
import { runInNewContext } from 'node:vm';

async function main(): Promise<void> {
  const files = new Map<string, string>();
  const commits: any[] = [];
  let failNextWrite = false;
  (globalThis as any).__mockShellDryRun = true;
  (globalThis as any).__mockIpcResponder = (cmd: string, args: any) => {
    if (cmd === 'settings.read') {
      const content = files.get(args.path) || '';
      const stamp = content || 'missing';
      return { stamp, unchanged: !!content && stamp === args.stamp, content };
    }
    if (cmd === 'fs.exists') return files.has(args.path);
    if (cmd === 'fs.readTextFile') {
      if (!files.has(args.path)) throw new Error('missing in-memory fixture');
      return files.get(args.path);
    }
    if (cmd === 'settings.write') {
      if (failNextWrite) { failNextWrite = false; return false; }
      const parsed = JSON.parse(args.content);
      files.set(args.path, args.content);
      commits.push(parsed);
      return true;
    }
    throw new Error('Forbidden non-fixture IPC: ' + cmd);
  };
  const repo = await import('../src/bridge/settingsRepository');
  repo.setSettingsDirectory('Q:\\YMCC-MEMORY-ONLY');
  const initial = repo.normalizeSettings({ tdp: { tdpMax: 37, fpsLimit: 75 } });
  files.set(repo.SETTINGS_FILE, JSON.stringify(initial));
  await repo.loadSettings();
  let passed = 0;
  function check(name: string, fn: () => void): void {
    fn(); passed++; console.log('PASS ' + name);
  }
  function assertDetached(value: any, seen = new Set<object>()): void {
    if (value === null || typeof value !== 'object' || seen.has(value)) return;
    assert.equal(isProxy(value), false);
    seen.add(value);
    Object.values(value).forEach(child => assertDetached(child, seen));
  }

  // Exact original chain: new object replaces a null field, gets cached, then
  // an unrelated scheduling write asks structuredClone to copy the whole cache.
  const identity = ref({ manufacturer: 'fixture', model: 'device', extra: { keep: true } });
  await repo.saveSettingsSection('fan', { deviceIdentity: identity.value });
  await repo.saveSettingsSection('performanceSchedule', { active: { ac: 'balanced' } });
  check('reactive device identity cannot poison subsequent schedule saves', () => {
    const persisted = JSON.parse(files.get(repo.SETTINGS_FILE)!);
    assert.equal(persisted.fan.deviceIdentity.model, 'device');
    assert.deepEqual(persisted.tdp, initial.tdp);
  });
  identity.value.model = 'changed-without-save';
  check('object save owns an independent snapshot', () => {
    assert.equal(commits.at(-1).fan.deviceIdentity.model, 'device');
  });
  let loaded = await repo.loadSettings();
  check('cache remains cloneable and detached after caller mutation', () => {
    assertDetached(loaded); structuredClone(loaded);
    assert.equal(loaded.fan.deviceIdentity.model, 'device');
  });

  const apps = ref([{ name: 'before', path: 'fixture.exe', args: ['a'], extra: { enabled: true } }]);
  const saveApps = repo.saveSettingsSection('quickApps', { apps: apps.value });
  apps.value[0].name = 'after'; apps.value[0].args.push('b'); apps.value.push({ name: 'late', path: '', args: [], extra: { enabled: false } });
  await saveApps;
  loaded = await repo.loadSettings();
  check('array and nested members are captured before the async write queue', () => {
    assert.deepEqual(loaded.quickApps.apps, [{ name: 'before', path: 'fixture.exe', args: ['a'], extra: { enabled: true } }]);
    assertDetached(loaded);
  });

  const mixed = shallowReactive({ child: readonly(reactive({ list: [reactive({ keep: 1 })] })) });
  const normalized = repo.normalizeSettings({ extensions: mixed });
  check('normalization unwraps nested readonly and shallow proxies', () => {
    assertDetached(normalized); structuredClone(normalized);
    assert.deepEqual(normalized.extensions.child.list, [{ keep: 1 }]);
  });
  const foreign = runInNewContext('({ nested: [] })');
  foreign.nested.push(reactive({ keep: true }));
  const foreignSnapshot = repo.normalizeSettings({ extensions: foreign });
  check('plain records from another realm unwrap their nested proxies', () => {
    assertDetached(foreignSnapshot); structuredClone(foreignSnapshot);
    assert.equal(foreignSnapshot.extensions.nested[0].keep, true);
  });
  const defaultA = repo.normalizeSettings({});
  defaultA.gamepad.feedback.lighting.color = '#abcdef';
  defaultA.input.gyroMotion.futureField = { poison: true };
  check('normalization never aliases or mutates shared defaults', () => {
    const defaultB = repo.normalizeSettings({});
    assert.notEqual(defaultB.gamepad.feedback.lighting.color, '#abcdef');
    assert.equal(defaultB.input.gyroMotion.futureField, undefined);
  });
  const base = reactive({ keep: { old: 1 }, list: [{ old: 1 }], nullable: { old: 1 } });
  const patch = reactive({ keep: { future: { enabled: true } }, list: [{ fresh: 2 }], nullable: null });
  const merged = repo.mergeSettings<any>(base, patch);
  check('merge preserves unknown fields, replaces arrays/null and detaches both inputs', () => {
    assert.deepEqual(merged, { keep: { old: 1, future: { enabled: true } }, list: [{ fresh: 2 }], nullable: null });
    assertDetached(merged);
    merged.keep.old = 9; merged.list[0].fresh = 9;
    assert.equal(base.keep.old, 1); assert.equal(patch.list[0].fresh, 2);
    assert.deepEqual(repo.mergeSettings({ count: 1 }, { count: undefined }), { count: undefined });
  });

  const replacement = reactive({ apps: [{ name: 'replacement' }], future: { keep: true } });
  const replacing = repo.replaceSettingsSection('quickApps', replacement);
  replacement.apps[0].name = 'late';
  await replacing;
  loaded = await repo.loadSettings();
  check('replace accepts proxies and captures a detached queued snapshot', () => {
    assert.equal(loaded.quickApps.apps[0].name, 'replacement');
    assert.equal(loaded.quickApps.future.keep, true); assertDetached(loaded);
  });
  await repo.replaceSettingsSection('quickApps', reactive({ apps: [] }));
  check('replacement does not retain retired unknown fields', () => {
    assert.equal(JSON.parse(files.get(repo.SETTINGS_FILE)!).quickApps.future, undefined);
  });
  const injected = reactive({ nested: [{ flag: true }] });
  await repo.updateSettings(settings => { settings.extensions.injected = injected; });
  injected.nested[0].flag = false;
  loaded = await repo.loadSettings();
  check('mutator writes are normalized once into independent cache and disk data', () => {
    assert.equal(loaded.extensions.injected.nested[0].flag, true);
    assertDetached(loaded); structuredClone(loaded);
  });

  const current = reactive(repo.normalizeSettings({}).input);
  const overlayPatch = reactive({ gameOverride: { profileId: 'A', values: { keep: true } } });
  const decision = repo.evaluateInputCas(current, current.revision, overlayPatch);
  check('pure input CAS accepts reactive current and replacement overlay', () => {
    assert.equal(decision.ok, true); assertDetached(decision.value);
    assert.equal(decision.value.gameOverride.profileId, 'A');
  });
  const conflict = repo.evaluateInputCas(current, current.revision + 1, overlayPatch);
  check('CAS conflict snapshots also accept reactive current', () => {
    assert.equal(conflict.ok, false); assertDetached(conflict.value);
  });
  const failedDecision = repo.evaluateInputCasWrite(current, current.revision, overlayPatch, false);
  check('failed CAS keeps the original detached revision', () => {
    assert.equal(failedDecision.ok, false); assert.equal(failedDecision.value.revision, current.revision);
    assertDetached(failedDecision.value);
  });
  loaded = await repo.loadSettings();
  const inputPatch = reactive({ diagnostics: { inputLoggingEnabled: false, future: { tag: 'submitted' } } });
  const writingInput = repo.compareAndSwapInputSettings(loaded.input.revision, inputPatch);
  inputPatch.diagnostics.future.tag = 'late';
  const inputResult = await writingInput;
  check('durable CAS snapshots patches before queued commit', () => {
    assert.equal(inputResult.ok, true); assert.equal(inputResult.value.diagnostics.future.tag, 'submitted');
    assertDetached(inputResult.value);
  });

  const beforeFailure = files.get(repo.SETTINGS_FILE);
  failNextWrite = true;
  await assert.rejects(repo.saveSettingsSection('fan', reactive({ deviceIdentity: { model: 'failed' } })), /统一配置写入失败/);
  loaded = await repo.loadSettings();
  check('failed persistence does not advance cache or change the document', () => {
    assert.equal(files.get(repo.SETTINGS_FILE), beforeFailure);
    assert.equal(loaded.fan.deviceIdentity.model, 'device');
  });
  await repo.saveSettingsSection('sleep', reactive({ future: { continued: true } }));
  check('write queue survives an earlier rejected write', () => {
    assert.equal(commits.at(-1).sleep.future.continued, true);
  });
  await assert.rejects(repo.saveSettingsSection('quickApps', { invalid: () => {} }), /clone/i);
  const cycle: any = {}; cycle.self = cycle;
  await assert.rejects(repo.saveSettingsSection('quickApps', { cycle }), /circular|cyclic/i);
  await repo.saveSettingsSection('quickApps', { apps: [] });
  check('unsupported and cyclic data fail without poisoning the queue', () => {
    assert.equal(commits.at(-1).quickApps.invalid, undefined);
    assert.equal(commits.at(-1).quickApps.cycle, undefined);
  });

  const schedule = await import('../src/bridge/performanceSchedule');
  const config = reactive(schedule.defaultPerformanceScheduleConfig());
  config.active.ac = 'medium'; config.profiles.ac.medium.tdpMax = 37;
  const saveSchedule = schedule.savePerformanceSchedule(schedule.snapshotPerformanceSchedule(config));
  config.active.ac = 'elite';
  await saveSchedule;
  check('real schedule bridge saves proxy-backed config after unrelated setting writes', () => {
    assert.equal(commits.at(-1).performanceSchedule.active.ac, 'medium');
    assert.equal(commits.at(-1).performanceSchedule.profiles.ac.medium.tdpMax, 37);
    assert.deepEqual(commits.at(-1).tdp, initial.tdp);
  });
  const orderA = repo.saveSettingsSection('sleep', reactive({ order: { value: 1 } }));
  const orderB = repo.saveSettingsSection('sleep', readonly(reactive({ order: { value: 2 } })));
  await Promise.all([orderA, orderB]);
  check('detached writes retain queue ordering', () => {
    assert.equal(commits.at(-2).sleep.order.value, 1);
    assert.equal(commits.at(-1).sleep.order.value, 2);
  });
  loaded = await repo.loadSettings(); loaded.tdp.tdpMax = 999;
  check('readers receive independent snapshots', () => { assert.equal(commits.at(-1).tdp.tdpMax, 37); });
  const reread = await repo.loadSettings();
  assert.equal(reread.tdp.tdpMax, 37);
  repo.clearSettingsCache();
  loaded = await repo.loadSettings();
  check('disk reload retains all user TDP values and remains cloneable', () => {
    assert.deepEqual(loaded.tdp, initial.tdp); assertDetached(loaded); structuredClone(loaded);
  });
  check('regression suite never issues shell or hardware writes', () => {
    assert.equal((globalThis as any).__mockShellCalls.length, 0);
    assert.equal((globalThis as any).__mockRegistryWrites.length, 0);
  });
  console.log('settings_reactivity_selftest: ' + passed + ' cases passed');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
