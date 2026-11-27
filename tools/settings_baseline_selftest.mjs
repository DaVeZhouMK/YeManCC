import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';
import { compileSettingsConsumer, createSettingsFixture } from './fixtures/settings_baseline.mjs';

export async function runSettingsBaselineChecks(options = {}) {
const root = options.root || process.cwd();
const contract = JSON.parse(fs.readFileSync(path.join(root, 'PowerControl/settings-baseline.json'), 'utf8'));
const defaults = JSON.parse(fs.readFileSync(path.join(root, 'PowerControl', contract.defaultsFile), 'utf8'));
const code = options.compiled || await compileSettingsConsumer(root);
let nativeScope;
const createFixture = (...args) => nativeScope ? nativeScope.createFixture(...args) : (options.createFixture || createSettingsFixture)(...args);
const cases = [], definitions = [];
async function check(name, run) {
  definitions.push({ name, run });
}
const detached = value => JSON.parse(JSON.stringify(value));
const inactive = value => {
  assert.equal(value.input.outputTarget.persona, 'disabled');
  assert.equal(value.input.outputTarget.buttonMappingEnabled, false);
  assert.equal(value.input.outputTarget.gyroEnabled, false);
  assert.equal(value.input.gyroMotion.enabled, false);
  assert.equal(value.input.applyStatus, 'safe-stop');
  for (const name of ['appliedRevision','appliedConfigHash','pendingConfigHash','hostAcknowledgedRevision','hostAcknowledgedConfigHash']) assert.equal(value.input[name], null);
  assert.equal(value.performanceSchedule.enabled, false);
  assert.equal(value.startupDesired.virtualGamepadPersona, 'disabled');
  assert.equal(value.startupDesired.fanControl, false);
  assert.equal(value.fan.featureEnabled, false);
  assert.equal(value.tdp.autoApply.boot, false);
  assert.equal(value.tdp.autoApply.wake, false);
};
const old = () => ({ schemaVersion: 1, ui: { theme: 'user-theme', custom: { keep: true } }, input: { revision: 17 }, extensions: { manualModule: { version: 'user' } }, future: { keep: true } });
await check('common Steam overlay startup preserves missing ON and explicit OFF through legacy and current four-mode storage', async () => {
  const rows = [];
  assert.equal(defaults.startupDesired.steamOverlayOnBoot, true);
  for (const mode of ['global', 'xbox', 'steam', 'xbox-steam']) for (const completed of [false, true]) for (const preference of ['missing', 'off', 'on']) {
    const initial = detached(defaults); if (!completed) delete initial.configurationBaseline;
    if (preference === 'missing') delete initial.startupDesired.steamOverlayOnBoot;
    else initial.startupDesired.steamOverlayOnBoot = preference === 'on';
    initial.startupDesired.overlayFuture = { keep: 'ordinary-user-value' };
    initial.extensions.bootModes = { schema: 1, revision: 9, current: mode, boot: mode, bootEnabled: true,
      global: { background: { theme: 'mode-original' } }, scenes: {} };
    const expectedModes = detached(initial.extensions.bootModes), fixture = createFixture(code, initial), expected = preference !== 'off';
    const loaded = await fixture.repo.loadSettings();
    assert.equal(loaded.startupDesired.steamOverlayOnBoot, expected); assert.equal(fixture.document().startupDesired.steamOverlayOnBoot, expected);
    assert.deepEqual(detached(fixture.document().extensions.bootModes), expectedModes);
    await fixture.repo.saveSettingsSection('startupDesired', { steamOverlayOnBoot: false });
    fixture.repo.clearSettingsCache(); const restarted = await fixture.repo.loadSettings();
    assert.equal(restarted.startupDesired.steamOverlayOnBoot, false);
    assert.deepEqual(detached(restarted.startupDesired.overlayFuture), { keep: 'ordinary-user-value' });
    assert.deepEqual(detached(restarted.extensions.bootModes), expectedModes);
    rows.push({ mode, completed, preference, missingOrSavedValue: expected, savedOffSurvivesRestart: true, modeBankUnchanged: true, unrelatedValuePreserved: true });
  }
  if (process.env.YMCC_BASELINE_OVERLAY_ROWS) fs.writeFileSync(process.env.YMCC_BASELINE_OVERLAY_ROWS, JSON.stringify(rows, null, 2), { flag: 'wx' });
});
await check('frozen v1 defaults match the actual normalizer and all catalogued sections', () => {
  const f = createFixture(code);
  assert.deepEqual(detached(f.repo.normalizeSettings({})), defaults);
  for (const { name } of contract.sections) assert.equal(typeof defaults[name], 'object');
  assert.equal(f.state.calls.length, 0);
});
await check('a repeated TDP limit skips only a healthy unchanged settings commit', async () => {
  const f = createFixture(code, defaults);
  const { tdpMax } = (await f.repo.loadSettings()).tdp;
  const writes = f.state.writes.length, calls = f.state.calls.length;
  await f.repo.saveSettingsSection('tdp', { tdpMax });
  assert(f.state.calls.slice(calls).some(({ cmd }) => cmd === 'settings.read'));
  assert.equal(f.state.writes.length, writes);

  const changed = f.document();
  changed.tdp.tdpMax = tdpMax + 1;
  f.seed(f.state.file, changed);
  await f.repo.saveSettingsSection('tdp', { tdpMax });
  assert.equal(f.state.writes.length, writes + 1);
  assert.equal(f.document().tdp.tdpMax, tdpMax);

  await f.repo.saveSettingsSection('tdp', { tdpMax, fpsLimit: changed.tdp.fpsLimit });
  assert.equal(f.state.writes.length, writes + 2, 'the fast path is limited to a single TDP field');
});
await check('legacy 035 imports missing sections and commits receipt with an atomic delta', async () => {
  const original = old(), f = createFixture(code, original);
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
  const overlayRows=[];
  for(const mode of ['global','xbox','steam','xbox-steam'])for(const kind of ['ordinary','runtime','unguarded-boot','boot','rollback','stale','cancelled','native-overlay-race','native-boot-race','write-failed','legacy-before-cas']){
    const initial=detached(defaults);initial.input.revision=17;
    initial.input.gameOverride={sessionId:'current-game',gamePath:'C:/games/preserve.exe',outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:false},sentinel:{preserve:true}};
    initial.gameProfiles={'C:/games/preserve.exe':{input:{persona:'steamdeck'},future:{preserve:true}}};
    initial.extensions.bootModes={schema:1,revision:3,current:mode,boot:mode,bootEnabled:true,global:{},scenes:{}};
    const fixture=createFixture(code,initial),loaded=await fixture.repo.loadSettings(),disk=detached(fixture.document()),count=fixture.state.writes.length;
    assert.equal(loaded.input.gameOverride,null,'legacy input overlay must not own the normalized input');
    assert.equal(disk.input.gameOverride,null,'legacy input owner must be retired on load');
    if(kind==='legacy-before-cas'){
      const newer=fixture.document();newer.input.gameOverride=detached(initial.input.gameOverride);fixture.seed(fixture.state.file,newer);
    }
    const guard=current=>current.input.revision===loaded.input.revision&&JSON.stringify(current.extensions.bootModes)===JSON.stringify(loaded.extensions.bootModes);
    if(kind==='native-overlay-race'||kind==='native-boot-race')fixture.state.beforeWrite=()=>{
      fixture.state.beforeWrite=null;const newer=fixture.document();
      if(kind==='native-overlay-race')newer.input.gameOverride={...newer.input.gameOverride,sessionId:'new-game',sentinel:{newOwner:true}};
      else newer.extensions.bootModes.boot=mode==='global'?'steam':'global';
      fixture.seed(fixture.state.file,newer);
    };
    if(kind==='write-failed')fixture.state.writeFailure=true;
    const origin=['ordinary','runtime'].includes(kind)?kind:'boot-mode';
    const decision=await fixture.repo.compareAndSwapInputSettings(kind==='stale'?16:17,{outputTarget:{persona:'dualsense-edge',buttonMappingEnabled:true,gyroEnabled:false},gyroMotion:{enabled:false}},kind==='unguarded-boot'?undefined:kind==='cancelled'?()=>false:guard,{origin});
    if(['ordinary','runtime','unguarded-boot','boot','rollback','legacy-before-cas'].includes(kind)){
      assert.equal(decision.ok,true,`${mode}/${kind}: legacy input overlay must not block edits`);
      assert.equal(fixture.state.writes.length,count+1);
      assert.equal(fixture.document().input.outputTarget.persona,'dualsense-edge');
      assert.equal(fixture.document().input.gameOverride,null);
      assert.deepEqual(detached(fixture.document().gameProfiles),disk.gameProfiles);
      assert.deepEqual(detached(fixture.document().extensions.bootModes),disk.extensions.bootModes);
      assert.equal(fixture.document().input.applyStatus,'saved-pending');assert.equal(fixture.document().input.hostAcknowledgedRevision,null);
      if(kind==='rollback'){
        const rollback=await fixture.repo.compareAndSwapInputSettings(decision.revision,{outputTarget:loaded.input.outputTarget,gyroMotion:loaded.input.gyroMotion},current=>current.input.revision===decision.revision,{origin:'boot-mode'});
        assert.equal(rollback.ok,true,'boot rollback must not restore a retired overlay');
        assert.deepEqual(detached(fixture.document().input.outputTarget),detached(loaded.input.outputTarget));
        assert.equal(fixture.document().input.gameOverride,null);
      }
    }else{
      assert.equal(decision.ok,false,`${mode}/${kind}: stale or failed request must not commit`);assert.equal(decision.reason,kind==='cancelled'?'cancelled':kind==='write-failed'?'write-failed':'revision-conflict');
      assert.equal(fixture.state.writes.length,count,'rejected input request must not commit');
      if(kind==='native-overlay-race')assert.equal(fixture.document().input.gameOverride.sessionId,'new-game');
      else if(kind==='native-boot-race')assert.notEqual(fixture.document().extensions.bootModes.boot,mode);
      else assert.deepEqual(detached(fixture.document()),disk);
    }
    overlayRows.push({mode,kind,ok:decision.ok,reason:decision.reason??null,legacyOverlayRetired:fixture.document().input.gameOverride===null});
  }
  if(process.env.YMCC_OVERLAY_CAS_ROWS)fs.writeFileSync(process.env.YMCC_OVERLAY_CAS_ROWS,JSON.stringify(overlayRows,null,2)+'\n',{flag:'wx'});

  const f = createFixture(code, old());
  f.seed(f.state.directory + '\\music_player.json', { folder: 'legacy-user-folder', volume: 0.42 });
  const decision = await f.repo.compareAndSwapInputSettings(17, { diagnostics: { inputLoggingEnabled: true } });
  assert.equal(decision.ok, true); assert.equal(f.document().input.revision, 18);
  assert.equal(f.document().music.folder, 'legacy-user-folder'); assert.equal(f.document().configurationBaseline.legacyImported, true);
  assert.equal(f.state.writes.length, 2);
});
await check('036 and 037 repeated startups never import old mirrors again or rewrite healthy JSON', async () => {
  const f = createFixture(code, old()); await f.repo.loadSettings();
  f.seed(f.state.directory + '\\music_player.json', { folder: 'obsolete-mirror', volume: 0.99 });
  const after = f.text(), calls = f.state.calls.length;
  for (let cycle = 0; cycle < 5; cycle++) { f.repo.clearSettingsCache(); await f.repo.loadSettings(); }
  assert.equal(f.text(), after); assert.equal(f.state.writes.length, 1);
  assert(!f.state.calls.slice(calls).some(c => c.cmd === 'fs.readTextFile'));
});
await check('missing section after baseline uses current defaults without resurrecting deleted preferences', async () => {
  const current = detached(defaults); delete current.music;
  const f = createFixture(code, current);
  f.seed(f.state.directory + '\\music_player.json', { folder: 'obsolete-music', volume: 0.99 });
  const loaded = await f.repo.loadSettings();
  assert.equal(loaded.music.folder, defaults.music.folder); assert.equal(loaded.music.volume, defaults.music.volume);
  assert(!f.state.calls.some(c => c.cmd === 'fs.readTextFile')); assert.equal(f.state.writes.length, 1);
});
await check('RTSS and nested malformed known shapes are repaired once and persist across restarts', async () => {
  const current = detached(defaults); current.rtss = null; current.ui = [];
  current.input.outputTarget = false; current.input.diagnostics = 'old'; current.cpu.profiles = 5;
  current.future = { untouched: ['user'] };
  const f = createFixture(code, current); await f.repo.loadSettings();
  assert.equal(typeof f.document().rtss, 'object'); assert(!Array.isArray(f.document().ui));
  assert.equal(f.document().future.untouched[0], 'user');
  f.repo.clearSettingsCache(); await f.repo.loadSettings(); assert.equal(f.state.writes.length, 1);
});
await check('reordered object keys cause no startup write or module CAS revision change', async () => {
  const current = Object.fromEntries(Object.entries(defaults).reverse());
  current.input = Object.fromEntries(Object.entries(current.input).reverse());
  const f = createFixture(code, current); await f.repo.loadSettings();
  assert.equal(f.state.writes.length, 0); assert.equal(f.document().input.revision, current.input.revision);
});
await check('unknown fields including baseline extensions and native receipts survive saves', async () => {
  const current = detached(defaults); current.configurationBaseline.note = { keep: true };
  current.domainLogs = { virtual: true }; current.steamDeckMouse = { revision: 27 };
  const f = createFixture(code, current); await f.repo.saveSettingsSection('music', { volume: 0.33 });
  assert.equal(f.document().configurationBaseline.note.keep, true); assert.equal(f.document().domainLogs.virtual, true);
  assert.equal(f.document().steamDeckMouse.revision, 27); assert.equal(f.document().input.revision, current.input.revision);
});
await check('future or malformed baseline cannot read, CAS, rewrite or fall back to stale mirrors', async () => {
  for (const stamp of [null, [], 'future', { id: 'other', revision: 1, legacyImported: true },
    { id: contract.id, revision: 2, legacyImported: true }, { id: contract.id, revision: '1', legacyImported: true },
    { id: contract.id, revision: 1, legacyImported: false }]) {
    const current = detached(defaults); current.configurationBaseline = stamp;
    current.input.outputTarget.persona = 'xbox'; current.input.outputTarget.buttonMappingEnabled = true;
    current.input.outputTarget.gyroEnabled = true; current.input.gyroMotion.enabled = true;
    current.input.applyStatus = 'active'; current.input.appliedRevision = 0;
    current.performanceSchedule.enabled = true; current.startupDesired.fanControl = true;
    const f = createFixture(code, current), before = f.text();
    inactive(await f.repo.loadSettings());
    const recovery = f.repo.getSettingsRecoveryState();
    assert.equal(recovery.status, 'runtime-fallback'); assert.equal(recovery.reason, 'unsupported');
    assert.equal(recovery.persisted, false); assert.match(recovery.message, /配置基准/);
    const decision = await f.repo.compareAndSwapInputSettings(0, {});
    assert.equal(decision.ok, false); assert.equal(decision.reason, 'write-failed');
    await assert.rejects(f.repo.saveSettingsSection('music', { volume: 0.31 }), /配置基准/);
    assert.equal(f.text(), before); assert.equal(f.state.writes.length, 0); assert.equal(f.state.renames.length, 0);
    assert(!f.state.calls.some(c => c.cmd === 'fs.readTextFile' && f.key(c.args.path) !== f.key(f.state.file)));
  }
});
await check('failed first migration preserves originals and retries without a false completion marker', async () => {
  const f = createFixture(code, old()), before = f.text();
  f.state.writeFailure = true; inactive(await f.repo.loadSettings());
  const recovery = f.repo.getSettingsRecoveryState();
  assert.equal(recovery.status, 'runtime-fallback'); assert.equal(recovery.reason, 'write-failed');
  assert.equal(recovery.persisted, false); assert.match(recovery.message, /interrupted/);
  assert.equal(f.text(), before); assert.equal(f.state.writes.length, 0);
  assert.equal(Object.hasOwn(f.document(), 'configurationBaseline'), false);
  f.state.writeFailure = false; const loaded = await f.repo.loadSettings();
  assert.equal(loaded.configurationBaseline.legacyImported, true); assert.equal(f.state.writes.length, 1);
  assert.equal(f.repo.getSettingsRecoveryState().status, 'ready');
});
await check('transient read failure has no defaults overwrite and can self-recover on the next explicit read', async () => {
  const f = createFixture(code, defaults), before = f.text(); f.state.readFailure = true;
  inactive(await f.repo.loadSettings());
  const recovery = f.repo.getSettingsRecoveryState();
  assert.equal(recovery.status, 'runtime-fallback'); assert.equal(recovery.reason, 'read-failed');
  assert.equal(recovery.persisted, false); assert.match(recovery.message, /read error/);
  assert.equal(f.text(), before);
  const decision = await f.repo.compareAndSwapInputSettings(0, {});
  assert.equal(decision.ok, false); assert.equal(decision.reason, 'write-failed');
  await assert.rejects(f.repo.saveSettingsSection('music', { volume: 0.31 }), /read error/);
  f.state.readFailure = false; await f.repo.loadSettings(); assert.equal(f.state.writes.length, 0);
  assert.equal(f.repo.getSettingsRecoveryState().status, 'ready');
});
await check('compatible baseline backup restores and closes legacy import without touching standalone game settings', async () => {
  const f = createFixture(code, '{broken-main', defaults);
  const game = '{"version":7,"user-game":{"preserve":true}}';f.seed(f.state.directory + '\\game-custom.json', game);
  f.seed(f.state.directory + '\\music_player.json', { volume: 0.99 });
  await f.repo.loadSettings(); assert.equal(f.document().music.volume, defaults.music.volume);
  assert.equal(f.text(f.state.directory + '\\game-custom.json'), game); assert.equal(f.state.renames.length, 0);
  assert.equal(f.repo.getSettingsRecoveryState().status, 'ready');
  assert.equal(f.repo.getSettingsRecoveryState().source, 'backup');
  assert.equal(f.text(f.state.file + '.bak'), JSON.stringify(defaults));
  assert([...f.state.files.values()].includes('{broken-main'));
  assert(!f.state.calls.some(c => c.cmd === 'fs.readTextFile' && f.key(c.args.path).endsWith('music_player.json')));
});
await check('recovering malformed compatible backup commits repairs once using its original delta baseline', async () => {
  const backup=detached(defaults);backup.rtss=null;backup.cpu.profiles='old-shape';backup.future={preserve:true};
  const f=createFixture(code,'{broken-main',backup);await f.repo.loadSettings();
  assert.equal(f.state.writes[0].initialize,false);assert.deepEqual(detached(f.state.writes[0].baseline),backup);
  assert.equal(typeof f.document().rtss,'object');assert.equal(typeof f.document().cpu.profiles,'object');
  assert.equal(f.document().future.preserve,true);f.repo.clearSettingsCache();await f.repo.loadSettings();assert.equal(f.state.writes.length,1);
  assert.equal(f.text(f.state.file + '.bak'), JSON.stringify(backup));
  assert([...f.state.files.values()].includes('{broken-main')); assert.equal(f.state.renames.length, 0);
});
await check('future backup and two corrupt originals remain untouched for explicit recovery', async () => {
  const future = detached(defaults); future.configurationBaseline.revision++;
  for (const backup of [future, '{broken-backup']) {
    const f = createFixture(code, '{broken-main', backup), before = [...f.state.files];
    const loaded = await f.repo.loadSettings(); inactive(loaded);
    const recovery = f.repo.getSettingsRecoveryState();
    if (typeof backup !== 'string') {
      assert.equal(recovery.status, 'runtime-fallback'); assert.equal(recovery.reason, 'unsupported');
      assert.equal(recovery.persisted, false); assert.match(recovery.message, /配置基准/);
      assert.deepEqual([...f.state.files], before); assert.equal(f.state.writes.length, 0);
    } else {
      assert.equal(recovery.status, 'ready'); assert.equal(recovery.source, 'defaults'); assert.equal(recovery.persisted, true);
      assert.equal(f.state.writes.length, 1); assert.equal(f.state.writes[0].initialize, true);
      for (const [target, raw] of before) {
        assert([...f.state.files].some(([name, content]) => name.startsWith(target + '.before-auto-recovery-') && content === raw), 'native recovery must archive exact original: ' + target);
      }
      inactive(f.document());
    }
    assert.equal(f.state.renames.length, 0);
  }
});
await check('a delayed migration commit cannot publish into a newly selected configuration directory', async () => {
  const f = createFixture(code, old()); let release;
  f.state.beforeWrite = () => new Promise(resolve => { release = resolve; });
  const loading = f.repo.loadSettings();
  for (let i = 0; i < 200 && !release; i++) await new Promise(resolve => setTimeout(resolve, 1));
  assert.equal(typeof release, 'function'); f.repo.setSettingsDirectory('R:\\different-owned-fixture'); release();
  await assert.rejects(loading, /目录已切换|旧请求已取消/);
  assert.equal(f.repo.SETTINGS_FILE, 'R:\\different-owned-fixture\\yeman-settings.json');
  assert.equal(f.state.writes.length, 1); assert.equal(f.state.writes[0].path, f.state.file);
});
await check('concurrent startup readers share one migration and receive detached snapshots', async () => {
  const f = createFixture(code, old());
  const [a,b,c] = await Promise.all([f.repo.loadSettings(), f.repo.loadSettings(), f.repo.loadSettings()]);
  a.music.volume = 0.88; assert.notEqual(b.music.volume, 0.88); assert.notEqual(c.music.volume, 0.88);
  assert.equal(f.state.writes.length, 1);
});
const selected = options.selectedNames || [];
assert.equal(new Set(selected).size, selected.length, 'duplicate exact baseline selection');
for (const name of selected) assert(definitions.some(row => row.name === name), 'unknown exact baseline case: ' + name);
const scheduled = selected.length ? definitions.filter(row => selected.includes(row.name)) : definitions;
assert(scheduled.length, 'empty baseline selection');
if(options.nativeFilesDir){
  assert(!options.createFixture&&!options.nativeStorage,'Native file mode cannot silently mix an injected/memory backend');
  const {prepareBaselineNative}=await import('./fixtures/settings_baseline_native.mjs');
  nativeScope=prepareBaselineNative(root,options.nativeFilesDir);
}
const businessStarted=performance.now();
let finalResult;
try {
for (const { name, run } of scheduled) {
  try { await run(); cases.push({ name, passed: true }); console.log('PASS ' + name); }
  catch (error) { cases.push({ name, passed: false, error: error.stack }); console.error('FAIL ' + name + ': ' + error.message); }
}
const businessElapsedMs=performance.now()-businessStarted;
const result={ status: cases.every(c => c.passed) ? 'PASS' : 'FAIL', passed: cases.filter(c => c.passed).length, failed: cases.filter(c => !c.passed).length, cases, selectedNames: selected, declaredCases: definitions.length, actualConsumers: ['settingsRepository.ts', 'api.ts', 'ipc.ts'], nativeStorage: nativeScope ? 'original public locked native writer; owned Win32 files; InputHost isolated' : options.nativeStorage || 'memory-only; atomic/delta semantics tested separately by native fixture', realConfigurationIO: !!nativeScope, hardwareOperations: false, productVersionChanged: false,
  ...(nativeScope?{native:nativeScope.evidence,businessElapsedMs,businessLimitMs:12000,installedConfigurationIO:false}: {}) };
if(nativeScope){
  if(businessElapsedMs>12000){result.status='FAIL';result.businessLimitExceeded=true;}
  nativeScope.recordResult(result);
}
finalResult=result;return result;
} finally {
  if(nativeScope){const closing=await nativeScope.close();if(finalResult){finalResult.nativeClosing=closing;if(!closing.sourceUnchanged)finalResult.status='FAIL';}}
}
}
if (process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url) {
  const selectedNames = [];let nativeFilesDir;
  for (let i = 2; i < process.argv.length; i += 2) {
    assert(['--case','--native-files'].includes(process.argv[i]),'expected --case <exact name> or --native-files <new owned directory>');
    assert(process.argv[i + 1] && !process.argv[i + 1].startsWith('--'), 'missing exact baseline case');
    if(process.argv[i]==='--case')selectedNames.push(process.argv[i+1]);
    else {assert(nativeFilesDir===undefined,'duplicate native file mode');nativeFilesDir=process.argv[i+1];}
  }
  const result = await runSettingsBaselineChecks({ selectedNames,nativeFilesDir });
  if (process.env.YMCC_BASELINE_REPORT) fs.writeFileSync(process.env.YMCC_BASELINE_REPORT, JSON.stringify(result,null,2)+'\n', { flag: 'wx' });
  console.log(JSON.stringify({status:result.status,passed:result.passed,failed:result.failed})); if (result.failed||result.status!=='PASS') process.exitCode = 1;
}
