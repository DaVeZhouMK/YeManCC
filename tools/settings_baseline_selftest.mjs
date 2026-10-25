import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { compileSettingsConsumer, createSettingsFixture } from './fixtures/settings_baseline.mjs';

const root = process.cwd();
const contract = JSON.parse(fs.readFileSync(path.join(root, 'PowerControl/settings-baseline.json'), 'utf8'));
const defaults = JSON.parse(fs.readFileSync(path.join(root, 'PowerControl', contract.defaultsFile), 'utf8'));
const code = await compileSettingsConsumer(root);
const cases = [];
async function check(name, run) {
  try { await run(); cases.push({ name, passed: true }); console.log('PASS ' + name); }
  catch (error) { cases.push({ name, passed: false, error: error.stack }); console.error('FAIL ' + name + ': ' + error.message); }
}
const detached = value => JSON.parse(JSON.stringify(value));
const old = () => ({ schemaVersion: 1, ui: { theme: 'user-theme', custom: { keep: true } }, input: { revision: 17 }, extensions: { manualModule: { version: 'user' } }, future: { keep: true } });
await check('frozen v1 defaults match the actual normalizer and all catalogued sections', () => {
  const f = createSettingsFixture(code);
  assert.deepEqual(detached(f.repo.normalizeSettings({})), defaults);
  for (const { name } of contract.sections) assert.equal(typeof defaults[name], 'object');
  assert.equal(f.state.calls.length, 0);
});
await check('legacy 035 imports missing sections and commits receipt with an atomic delta', async () => {
  const original = old(), f = createSettingsFixture(code, original);
  f.seed(f.state.directory + '\\music_player.json', { volume: 0.42, folder: 'user-music' });
  f.seed(f.state.directory + '\\ui-settings.json', { theme: 'stale-mirror' });
  const loaded = await f.repo.loadSettings();
  assert.equal(loaded.music.volume, 0.42); assert.equal(loaded.ui.theme, 'user-theme');
  assert.equal(loaded.input.revision, 17); assert.equal(loaded.extensions.manualModule.version, 'user');
  assert.equal(loaded.future.keep, true); assert.equal(f.state.writes.length, 1);
  assert.equal(f.state.writes[0].initialize, false); assert.deepEqual(detached(f.state.writes[0].baseline), original);
  assert.equal(loaded.configurationBaseline.revision, contract.revision);
});
await check('first input CAS completes legacy migration before publishing a completion receipt', async () => {
  const f = createSettingsFixture(code, old());
  f.seed(f.state.directory + '\\music_player.json', { folder: 'legacy-user-folder', volume: 0.42 });
  const decision = await f.repo.compareAndSwapInputSettings(17, { diagnostics: { inputLoggingEnabled: true } });
  assert.equal(decision.ok, true); assert.equal(f.document().input.revision, 18);
  assert.equal(f.document().music.folder, 'legacy-user-folder'); assert.equal(f.document().configurationBaseline.legacyImported, true);
  assert.equal(f.state.writes.length, 2);
});
await check('036 and 037 repeated startups never import old mirrors again or rewrite healthy JSON', async () => {
  const f = createSettingsFixture(code, old()); await f.repo.loadSettings();
  f.seed(f.state.directory + '\\music_player.json', { folder: 'obsolete-mirror', volume: 0.99 });
  const after = f.text(), calls = f.state.calls.length;
  for (let cycle = 0; cycle < 5; cycle++) { f.repo.clearSettingsCache(); await f.repo.loadSettings(); }
  assert.equal(f.text(), after); assert.equal(f.state.writes.length, 1);
  assert(!f.state.calls.slice(calls).some(c => c.cmd === 'fs.readTextFile'));
});
await check('missing section after baseline uses current defaults without resurrecting deleted preferences', async () => {
  const current = detached(defaults); delete current.music;
  const f = createSettingsFixture(code, current);
  f.seed(f.state.directory + '\\music_player.json', { folder: 'obsolete-music', volume: 0.99 });
  const loaded = await f.repo.loadSettings();
  assert.equal(loaded.music.folder, defaults.music.folder); assert.equal(loaded.music.volume, defaults.music.volume);
  assert(!f.state.calls.some(c => c.cmd === 'fs.readTextFile')); assert.equal(f.state.writes.length, 1);
});
await check('RTSS and nested malformed known shapes are repaired once and persist across restarts', async () => {
  const current = detached(defaults); current.rtss = null; current.ui = [];
  current.input.outputTarget = false; current.input.diagnostics = 'old'; current.cpu.profiles = 5;
  current.future = { untouched: ['user'] };
  const f = createSettingsFixture(code, current); await f.repo.loadSettings();
  assert.equal(typeof f.document().rtss, 'object'); assert(!Array.isArray(f.document().ui));
  assert.equal(f.document().future.untouched[0], 'user');
  f.repo.clearSettingsCache(); await f.repo.loadSettings(); assert.equal(f.state.writes.length, 1);
});
await check('reordered object keys cause no startup write or module CAS revision change', async () => {
  const current = Object.fromEntries(Object.entries(defaults).reverse());
  current.input = Object.fromEntries(Object.entries(current.input).reverse());
  const f = createSettingsFixture(code, current); await f.repo.loadSettings();
  assert.equal(f.state.writes.length, 0); assert.equal(f.document().input.revision, current.input.revision);
});
await check('unknown fields including baseline extensions and native receipts survive saves', async () => {
  const current = detached(defaults); current.configurationBaseline.note = { keep: true };
  current.domainLogs = { virtual: true }; current.steamDeckMouse = { revision: 27 };
  const f = createSettingsFixture(code, current); await f.repo.saveSettingsSection('music', { volume: 0.33 });
  assert.equal(f.document().configurationBaseline.note.keep, true); assert.equal(f.document().domainLogs.virtual, true);
  assert.equal(f.document().steamDeckMouse.revision, 27); assert.equal(f.document().input.revision, current.input.revision);
});
await check('future or malformed baseline cannot read, CAS, rewrite or fall back to stale mirrors', async () => {
  for (const stamp of [null, [], 'future', { id: 'other', revision: 1, legacyImported: true },
    { id: contract.id, revision: 2, legacyImported: true }, { id: contract.id, revision: '1', legacyImported: true },
    { id: contract.id, revision: 1, legacyImported: false }]) {
    const current = detached(defaults); current.configurationBaseline = stamp;
    const f = createSettingsFixture(code, current), before = f.text();
    await assert.rejects(f.repo.loadSettings(), /配置基准/);
    await assert.rejects(f.repo.compareAndSwapInputSettings(0, {}), /配置基准/);
    assert.equal(f.text(), before); assert.equal(f.state.writes.length, 0); assert.equal(f.state.renames.length, 0);
    assert(!f.state.calls.some(c => c.cmd === 'fs.readTextFile' && f.key(c.args.path) !== f.key(f.state.file)));
  }
});
await check('failed first migration preserves originals and retries without a false completion marker', async () => {
  const f = createSettingsFixture(code, old()), before = f.text();
  f.state.writeFailure = true; await assert.rejects(f.repo.loadSettings(), /interrupted/);
  assert.equal(f.text(), before); assert.equal(f.state.writes.length, 0);
  f.state.writeFailure = false; const loaded = await f.repo.loadSettings();
  assert.equal(loaded.configurationBaseline.legacyImported, true); assert.equal(f.state.writes.length, 1);
});
await check('transient read failure has no defaults overwrite and can self-recover on the next explicit read', async () => {
  const f = createSettingsFixture(code, defaults), before = f.text(); f.state.readFailure = true;
  await assert.rejects(f.repo.loadSettings(), /read error/); assert.equal(f.text(), before);
  f.state.readFailure = false; await f.repo.loadSettings(); assert.equal(f.state.writes.length, 0);
});
await check('compatible baseline backup restores and closes legacy import without touching standalone game settings', async () => {
  const f = createSettingsFixture(code, '{broken-main', defaults);
  const game = '{"version":7,"user-game":{"preserve":true}}';f.seed(f.state.directory + '\\game-custom.json', game);
  f.seed(f.state.directory + '\\music_player.json', { volume: 0.99 });
  await f.repo.loadSettings(); assert.equal(f.document().music.volume, defaults.music.volume);
  assert.equal(f.text(f.state.directory + '\\game-custom.json'), game); assert.equal(f.state.renames.length, 1);
  assert([...f.state.files.values()].includes('{broken-main'));
});
await check('recovering malformed compatible backup commits repairs once using its original delta baseline', async () => {
  const backup=detached(defaults);backup.rtss=null;backup.cpu.profiles='old-shape';backup.future={preserve:true};
  const f=createSettingsFixture(code,'{broken-main',backup);await f.repo.loadSettings();
  assert.equal(f.state.writes[0].initialize,false);assert.deepEqual(detached(f.state.writes[0].baseline),backup);
  assert.equal(typeof f.document().rtss,'object');assert.equal(typeof f.document().cpu.profiles,'object');
  assert.equal(f.document().future.preserve,true);f.repo.clearSettingsCache();await f.repo.loadSettings();assert.equal(f.state.writes.length,1);
});
await check('future backup and two corrupt originals remain untouched for explicit recovery', async () => {
  const future = detached(defaults); future.configurationBaseline.revision++;
  for (const backup of [future, '{broken-backup']) {
    const f = createSettingsFixture(code, '{broken-main', backup), before = [...f.state.files];
    await assert.rejects(f.repo.loadSettings(), /配置基准|损坏/);
    assert.deepEqual([...f.state.files], before); assert.equal(f.state.writes.length, 0); assert.equal(f.state.renames.length, 0);
  }
});
await check('a delayed migration commit cannot publish into a newly selected configuration directory', async () => {
  const f = createSettingsFixture(code, old()); let release;
  f.state.beforeWrite = () => new Promise(resolve => { release = resolve; });
  const loading = f.repo.loadSettings();
  for (let i = 0; i < 200 && !release; i++) await new Promise(resolve => setTimeout(resolve, 1));
  assert.equal(typeof release, 'function'); f.repo.setSettingsDirectory('R:\\different-owned-fixture'); release();
  await assert.rejects(loading, /目录已切换|旧请求已取消/);
  assert.equal(f.repo.SETTINGS_FILE, 'R:\\different-owned-fixture\\yeman-settings.json');
  assert.equal(f.state.writes.length, 1); assert.equal(f.state.writes[0].path, f.state.file);
});
await check('concurrent startup readers share one migration and receive detached snapshots', async () => {
  const f = createSettingsFixture(code, old());
  const [a,b,c] = await Promise.all([f.repo.loadSettings(), f.repo.loadSettings(), f.repo.loadSettings()]);
  a.music.volume = 0.88; assert.notEqual(b.music.volume, 0.88); assert.notEqual(c.music.volume, 0.88);
  assert.equal(f.state.writes.length, 1);
});
const result = { status: cases.every(c => c.passed) ? 'PASS' : 'FAIL', passed: cases.filter(c => c.passed).length, failed: cases.filter(c => !c.passed).length, cases, actualConsumers: ['settingsRepository.ts', 'api.ts', 'ipc.ts'], nativeStorage: 'memory-only; atomic/delta semantics tested separately by native fixture', realConfigurationIO: false, hardwareOperations: false, productVersionChanged: false };
if (process.env.YMCC_BASELINE_REPORT) fs.writeFileSync(process.env.YMCC_BASELINE_REPORT, JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({status:result.status,passed:result.passed,failed:result.failed})); if (result.failed) process.exitCode = 1;
