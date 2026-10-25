import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const require = createRequire(path.join(process.cwd(), 'package.json'));
const vue = require('vue');
const { parse, compileScript } = require('vue/compiler-sfc');
const { transformSync } = require('esbuild');
const root = process.cwd();
const cases = [];
async function check(name, run) {
  try { await run(); cases.push(name); }
  catch(error) { console.error(JSON.stringify({failedCase:name,passedBeforeFailure:cases.length,passedCases:cases,error:String(error)},null,2));throw error; }
}
function evaluate(source, imports = {}, globals = {}) {
  const module = { exports: {} };
  vm.runInNewContext(transformSync(source, { loader: 'ts', format: 'cjs' }).code, {
    module, exports: module.exports, console, structuredClone, performance, TextEncoder,
    require(name) { if (name === 'vue') return vue; if (name in imports) return imports[name]; throw Error('Unexpected import ' + name); },
    ...globals,
  });
  return module.exports;
}
function load(file, imports, globals) { return evaluate(fs.readFileSync(path.join(root, file), 'utf8'), imports, globals); }
const hysteresis = load('src/bridge/gamePolicyHysteresis.ts');
await check('策略租约比较包含可选身份代次与路径；旧mock及路径大小写保持兼容',()=>{
  const a={pid:101,processCreated:'1001',name:'a.exe',path:'C:/Games/A.exe',ts:0,generation:10,identityGeneration:4};
  assert(hysteresis.samePolicyGame(a,{...a,generation:11,path:'c:/games/a.EXE'}));
  assert(!hysteresis.samePolicyGame(a,{...a,identityGeneration:5}));
  assert(!hysteresis.samePolicyGame(a,{...a,path:'D:/Games/A.exe'}));
  assert(!hysteresis.samePolicyGame(a,{...a,processCreated:'1002'}));
  assert(hysteresis.samePolicyGame(a,{...a,identityGeneration:undefined,path:''}));
  assert(hysteresis.samePolicyGame({...a,identityGeneration:undefined},a));
  assert(hysteresis.samePolicyGame(null,null));assert(!hysteresis.samePolicyGame(a,null));
});
const detectionOnly = process.argv.includes('--detection-only');
if (!detectionOnly) {
const hash = load('src/bridge/inputConfigHash.ts');
const hc = load('src/bridge/hcInputUtils.ts');
const personas = load('src/bridge/controllerPersona.ts');
const baseline = load('src/bridge/settingsBaseline.ts', { '../../PowerControl/settings-baseline.json': JSON.parse(fs.readFileSync(path.join(root,'PowerControl/settings-baseline.json'),'utf8')) });
const repo = load('src/bridge/settingsRepository.ts', { './controllerPersona': personas, './settingsBaseline': baseline, './settingsSnapshot': load('src/bridge/settingsSnapshot.ts'), './api': {}, './inputStartupPolicy': load('src/bridge/inputStartupPolicy.ts', { './controllerPersona': personas }), './inputConfigHash': hash, './hcInputUtils': hc, './quickAppDefaults': load('src/bridge/quickAppDefaults.ts') });
const A = { pid: 101, processCreated: '1001', name: 'a.exe', path: 'C:\\Games\\a.exe', ts: 0 };
const B = { pid: 202, processCreated: '2002', name: 'b.exe', path: 'C:\\Games\\b.exe', ts: 0 };
const C = { pid: 303, processCreated: '3003', name: 'c.exe', path: 'C:\\Games\\c.exe', ts: 0 };
const base = () => structuredClone(repo.normalizeSettings({ input: { outputTarget: { persona: 'disabled', buttonMappingEnabled: false, gyroEnabled: false }, gyroMotion: { enabled: false, outputMode: 'disabled', activePreset: 'racing', preset: 'racing', motionMode: 'toggle', motionTrigger: 'RB', gyroMultiplier: 2, presets: { fps: { motionMode: 'suppress', motionTrigger: 'LT', gyroMultiplier: 1.7 }, racing: { motionInput: 'joystick-steering' } } } } }).input);
const profile = (pad = 'follow', gyro = 'follow') => ({ enabled: true, displayName: 'Game', padPersona: pad, gyroOverride: gyro, ac: { cpuPreset: 'turbo' }, dc: { cpuPreset: 'balanced' } });
await check('首次进入和权威退出立即投影；等待10秒不会重放旧目标', () => {
  const s = new hysteresis.GamePolicyHysteresis(); assert(s.observe(A, 0));
  assert(s.observe(null, 100)); assert.equal(s.current, null); assert.equal(s.missingSince, null);
  assert(!s.observe(null, 10099)); assert(!s.observe(null, 10100));
});
await check('A→B→A每次直接跟随唯一源；没有保留旧PID的计时', () => {
  const s = new hysteresis.GamePolicyHysteresis(); s.observe(A, 0);
  assert(s.observe(B, 100)); assert.equal(s.current.pid, B.pid);
  assert(s.observe(A, 9999)); assert.equal(s.current.pid, A.pid); assert.equal(s.missingSince, null); assert(!s.observe(A, 20000));
});
await check('连续候选和退出均直接投影最新权威目标；模型不另选游戏', () => {
  const s = new hysteresis.GamePolicyHysteresis(); s.observe(A, 0);
  assert(s.observe(null, 100)); assert.equal(s.current, null);
  assert(s.observe(B, 8000)); assert.equal(s.current.pid, B.pid);
  assert(s.observe(C, 10099)); assert.equal(s.current.pid, C.pid); assert.equal(s.missingSince, null);
});
await check('PID复用立即换身份；标题和来源变化不算进程切换且输入对象不能串改', () => {
  const s = new hysteresis.GamePolicyHysteresis(); s.observe(A, 0);
  const metadata={ ...A, source: 'whitelist', title: 'new' };
  assert(!s.observe(metadata, 20)); assert.equal(s.missingSince, null); metadata.pid=999; assert.equal(s.current.pid,A.pid);
  assert(s.observe({ ...A, processCreated: '9999' }, 100)); assert.equal(s.current.processCreated,'9999'); assert.equal(s.missingSince, null);
});
async function flush() { for (let i = 0; i < 35; i++) { await Promise.resolve(); await vue.nextTick(); } }
async function fixture(options = {}) {
  let now = 0, timerSeq = 0, raw = options.raw === undefined ? A : options.raw;
  const timers = new Map(), scheduled = new Map(), rawListeners = new Set(), configListeners = new Set(), scheduleListeners = new Set();
  const calls = { cas: [], performance: [], core: [], clearCore: 0, conflicts: options.conflicts || 0, writeFailures: options.writeFailures || 0 };
  let settings = options.input || base();
  let config = { version: 1, entries: { 'a.exe': profile('steamdeck', 'fps'), 'b.exe': profile('follow', 'off') }, ...options.config };
  let rawCurrent = null, rawInitialized = false;
  let locked = { locked: false, identity: '' }, strictFailure = !!options.strictFailure, resolveBlockedConfig = null, resolveBlockedCas = null, clearRestoreFailures = options.clearRestoreFailures || 0;
  const clock = {
    performance: { now: () => now },
    setTimeout(fn, ms) { const id = ++timerSeq; timers.set(id, { at: now + ms, fn }); return id; },
    clearTimeout(id) { timers.delete(id); },
  };
  const detector = {
    getCurrentGame() { return rawCurrent ? { ...rawCurrent } : null; },
    isGameStatusInitialized() { return rawInitialized; },
    isCurrentGameTarget(target) { return !!rawCurrent && rawCurrent.pid===target.pid && rawCurrent.processCreated===target.processCreated; },
    subscribeGameStatus(cb) { rawListeners.add(cb); cb(detector.getCurrentGame()); return () => rawListeners.delete(cb); },
    async refreshGameStatusStrict() {
      if (strictFailure) throw Error('transport');
      rawCurrent=raw ? { ...raw } : null; rawInitialized=true;
      for(const cb of rawListeners) cb(detector.getCurrentGame());
      return detector.getCurrentGame();
    },
  };
  const target = load('src/bridge/gamePolicyTarget.ts', { './gamedetect': detector, './gamePolicyHysteresis': hysteresis }, clock);
  const scheduler = { registerScheduledTask(name, ms, run, opts) { scheduled.set(name, { ms, run, opts }); return () => scheduled.delete(name); } };
  const repository = {
    mergeSettings: repo.mergeSettings,
    async loadSettings() { return { input: structuredClone(settings) }; },
    async compareAndSwapInputSettings(revision, patch, isCurrent) {
      if (resolveBlockedCas) await new Promise(resolve => { resolveBlockedCas.resolve = resolve; });
      if (isCurrent && !isCurrent()) return { ok: false, reason: 'cancelled', value: structuredClone(settings) };
      if (calls.writeFailures > 0) { calls.writeFailures--; return repo.evaluateInputCasWrite(settings, revision, patch, false); }
      if (calls.conflicts > 0) { calls.conflicts--; settings.revision++; return { ok: false, reason: 'revision-conflict', value: structuredClone(settings) }; }
      const result = repo.evaluateInputCas(settings, revision, patch);
      if (result.ok) { settings = result.value; calls.cas.push(structuredClone(patch)); }
      return result;
    },
  };
  const perf = {
    isPerformanceScheduleBusy: () => false,
    async loadGameCustomConfig() { if (resolveBlockedConfig) await new Promise(resolve => { resolveBlockedConfig.resolve = resolve; }); return structuredClone(config); },
    onGameCustomConfigChanged(cb) { configListeners.add(cb); return () => configListeners.delete(cb); },
    async clearGameInputRestoreSnapshot(expected) { if (clearRestoreFailures > 0) { clearRestoreFailures--; throw Error('simulated clear failure'); } if (JSON.stringify(expected) === JSON.stringify(config.inputRestore)) { delete config.inputRestore; for (const cb of configListeners) cb(config); } },
    async loadPerformanceSchedule() { return { configured: true, enabled: true, active: { ac: 'balanced', dc: 'balanced' }, profiles: { ac: {}, dc: {} } }; },
    resolveGameCustomProfiles(entry) { return { ac: entry.ac, dc: entry.dc }; },
    async restorePerformanceScheduleIfConfigured() { calls.performance.push(target.getPolicyGame()?.pid || null); return 'auto'; },
    onPerformanceScheduleChanged(cb) { scheduleListeners.add(cb); return () => scheduleListeners.delete(cb); },
  };
  const input = load('src/bridge/gameInputOverride.ts', {
    './gyroPresetModel':load('src/bridge/gyroPresetModel.ts'),
    '@/bridge/settingsRepository': repository, '@/bridge/gamePolicyTarget': target,
    '@/bridge/ipc': { invoke: async () => 'native-session-1' }, '@/scheduler': scheduler, '@/bridge/performanceSchedule': perf,
  }, clock);
  input.subscribeGameInputOverrideState(state => { locked = state; });
  const runtime = load('src/bridge/gamePolicyRuntime.ts', {
    './gamePolicyTarget': target, './performanceSchedule': perf, '@/scheduler': scheduler,
    './powerSource': {powerSourceMode:vue.ref('ac')}, './yeman':{detectPowerMode:async()=> 'ac'}, './autofloat':{setFloatRtssLinked(){}},
    './frameRateLimits':{loadGlobalFrameRates:async()=>({ac:{fps:60,lastFps:60,ceiling:60},dc:{fps:30,lastFps:30,ceiling:30}}),onFrameRatesChanged:()=>()=>{},dedicatedFrameRatePair:(_,global)=>global,applyIndependentFrameRates:async()=>true},
    './api': { powerLifecycle: { get: async () => ({ phase: 'ready', hardwareWritesAllowed: true }) } },
    './quickActionLock': { isQuickActionBusy: () => false },
    './gameCorePolicy': {
      detectGameCorePolicy: async () => ({ heterogeneous: true, smtAvailable: true }),
      applyGameCorePolicy: async (game, mode, hyper) => { calls.core.push([game.pid, mode, hyper]); return { ok: true, applied: true }; },
      clearGameCorePolicy: async () => { calls.clearCore++; return true; },
    },
  }, clock);
  async function emit(game) { raw = game; rawCurrent=game ? { ...game } : null; rawInitialized=true; for (const cb of rawListeners) cb(detector.getCurrentGame()); await flush(); }
  async function tick(ms) {
    const end = now + ms; let limit = 100;
    while (true) {
      const due = [...timers.entries()].filter(([, t]) => t.at <= end).sort((a, b) => a[1].at - b[1].at)[0];
      if (!due) break; assert(--limit > 0, 'timer storm'); now = due[1].at; timers.delete(due[0]); due[1].fn(); await flush();
    }
    now = end; await flush();
  }
  input.startGameInputOverrideWatch(); runtime.startGamePolicyRuntime(); await flush();
  return {
    target, input, calls, clock, scheduled, emit, tick, repository,
    get settings() { return settings; }, get config() { return config; }, get locked() { return locked; },
    failWrites(count) { calls.writeFailures=count; },
    set strictFailure(value) { strictFailure = value; },
    async changeEntry(key, entry) { config.entries[key] = entry; for (const cb of configListeners) cb(config); await flush(); },
    async retry() { await detector.refreshGameStatusStrict().catch(()=>{}); for (const task of scheduled.values()) await task.run(); await flush(); },
    async blockCas() { resolveBlockedCas = {}; },
    async unblockCas() { const blocked = resolveBlockedCas; resolveBlockedCas = null; blocked?.resolve?.(); await flush(); },
    async blockConfig() { resolveBlockedConfig = {}; },
    async unblockConfig() { const blocked = resolveBlockedConfig; resolveBlockedConfig = null; blocked?.resolve?.(); await flush(); },
    stop() { input.stopGameInputOverrideWatch(); runtime.stopGamePolicyRuntime(); },
  };
}
await check('专属覆盖只写overlay；页面本机设置和预设快照逐字节不变', async () => {
  const f = await fixture();
  assert.equal(f.settings.outputTarget.persona, 'disabled'); assert.equal(f.settings.gyroMotion.enabled, false);
  assert.equal(f.settings.gameOverride.outputTarget.persona, 'steamdeck'); assert.equal(f.settings.gameOverride.gyroMotion.enabled, true);
  assert.equal(f.settings.gameOverride.gyroMotion.motionTrigger, 'LT'); assert.equal(f.settings.gameOverride.gyroMotion.motionMode, 'on');
  assert.deepEqual(f.settings.outputTarget, base().outputTarget); assert.deepEqual(f.settings.gyroMotion, base().gyroMotion);
  assert(f.locked.locked); assert(f.calls.cas.every(patch => !patch.outputTarget && !patch.gyroMotion)); f.stop();
});
await check('策略与唯一源即时退出及回归；本机配置不改且等待不会重放', async () => {
  const f = await fixture(); await f.emit(null);
  assert.equal(f.target.getPolicyGame(),null); assert.equal(f.settings.gameOverride,null); assert(!f.locked.locked);
  assert.deepEqual(f.settings.outputTarget,base().outputTarget); assert.deepEqual(f.settings.gyroMotion,base().gyroMotion);
  const exited=JSON.stringify(f.calls); await f.tick(9999); assert.equal(JSON.stringify(f.calls),exited);
  await f.emit(A); assert.equal(f.target.getPolicyGame().pid,A.pid); assert(f.locked.locked);
  const returned=JSON.stringify(f.calls); await f.tick(20000); assert.equal(JSON.stringify(f.calls),returned); f.stop();
});
await check('权威退出立即统一恢复普通策略、清除overlay和页面锁', async () => {
  const f = await fixture(); await f.emit(null);
  assert.equal(f.target.getPolicyGame(), null); assert.equal(f.settings.gameOverride, null); assert(!f.locked.locked);
  assert.deepEqual(f.calls.performance, [101, null]); assert.deepEqual(f.settings.outputTarget, base().outputTarget);
  assert.deepEqual(f.settings.gyroMotion, base().gyroMotion); f.stop();
});
await check('A→B立即同源；B的follow从本机基线合成而不是继承A', async () => {
  const f = await fixture(); await f.emit(B); assert.equal(f.target.getPolicyGame().pid,B.pid);
  assert.equal(f.settings.gameOverride.outputTarget.persona, 'disabled'); assert.equal(f.settings.gameOverride.gyroMotion.activePreset, 'racing');
  assert.deepEqual(f.calls.performance, [101, 202]); const applied=JSON.stringify(f.calls);
  await f.tick(10000); assert.equal(JSON.stringify(f.calls),applied); f.stop();
});
await check('IPC失败不能制造退出；只有成功权威空结果才恢复', async () => {
  const f = await fixture(); f.strictFailure = true; await assert.rejects(f.target.refreshPolicyGameStrict(),/transport/); await f.tick(10000);
  assert.equal(f.target.getPolicyGame().pid, 101); assert(f.locked.locked);
  f.strictFailure = false; await f.emit(null); assert.equal(f.target.getPolicyGame(), null); assert(!f.locked.locked); f.stop();
});
await check('首次IPC失败且后续仍无游戏时也能初始化并清理旧会话overlay', async () => {
  const initial = base(); initial.gameOverride = {identity:'old',sessionId:'old-owner',outputTarget:{persona:'elite'},gyroMotion:{enabled:true}};
  const f = await fixture({raw:null,strictFailure:true,input:initial});
  assert(!f.target.isPolicyGameInitialized()); assert.equal(f.calls.cas.length,0);
  f.strictFailure=false; await f.retry();
  assert(f.target.isPolicyGameInitialized()); assert.equal(f.settings.gameOverride,null); assert(!f.locked.locked); f.stop();
});
await check('CAS冲突可重试；输入字段接管期间原子拒绝本机写入', async () => {
  const f = await fixture({ conflicts: 1 }); assert(f.locked.locked);
  const r = repo.evaluateInputCas(f.settings, f.settings.revision, { outputTarget: { persona: 'elite' } });
  assert(!r.ok); assert.equal(r.reason, 'game-override-active');
  const allowed = repo.evaluateInputCas(f.settings, f.settings.revision, { diagnostics: { verbose: true } }); assert(allowed.ok); f.stop();
});
await check('overlay写入/清除失败保留页面锁与本机值，常驻重试成功后才解锁', async () => {
  const f=await fixture({writeFailures:3}); assert(f.locked.locked); assert.equal(f.calls.cas.length,0);
  assert.deepEqual(f.settings.outputTarget,base().outputTarget);
  await f.retry(); assert(f.settings.gameOverride); f.failWrites(3); await f.emit(null); await f.tick(10000);
  assert(f.settings.gameOverride); assert(f.locked.locked); await f.retry();
  assert.equal(f.settings.gameOverride,null); assert(!f.locked.locked); f.stop();
});
await check('本机persona不支持陀螺仪时，专属不能错误开启虚拟输出', async () => {
  const f = await fixture(); const overlay = f.input.buildGameInputOverlay(base(), profile('disabled', 'fps'), 'A', 'S');
  assert.equal(overlay.outputTarget.persona, 'disabled'); assert.equal(overlay.gyroMotion.enabled, false); f.stop();
});
await check('专属配置显式改动立即生效；输入改动不重放CPU/核心', async () => {
  const f = await fixture(); const before = f.calls.performance.length, core = f.calls.clearCore;
  await f.changeEntry('a.exe', profile('elite', 'off'));
  assert.equal(f.settings.gameOverride.outputTarget.persona, 'elite'); assert.equal(f.calls.performance.length, before); assert.equal(f.calls.clearCore, core);
  await f.changeEntry('a.exe', profile()); assert.equal(f.settings.gameOverride, null); assert(!f.locked.locked); f.stop();
});
await check('旧核心策略不再写入；独立超线程随唯一身份切换且等待不重放', async () => {
  const f = await fixture({ config: { entries: { 'a.exe': { ...profile('steamdeck','fps'), corePolicyEnabled: true, corePolicyMode: 'only-big', hyperThreadPolicyEnabled: true, hyperThreadPolicy: 'off' } } } });
  assert.deepEqual(f.calls.core, [[101,'default','off']]); const clears = f.calls.clearCore;
  await f.emit(B); assert.equal(f.target.getPolicyGame().pid,B.pid); assert.equal(f.calls.clearCore, clears+1); await f.emit(A); assert.equal(f.calls.core.length,2); const applied=JSON.stringify(f.calls); await f.tick(10000); assert.equal(JSON.stringify(f.calls),applied); f.stop();
});
await check('旧版inputRestore先修复本机快照，再进入独立overlay', async () => {
  const original = base(); const overwritten = structuredClone(original);
  overwritten.outputTarget = { ...original.outputTarget, persona: 'steamdeck', buttonMappingEnabled: true, gyroEnabled: true };
  overwritten.gyroMotion = { ...original.gyroMotion, enabled: true, activePreset: 'fps' };
  const restore = { persona: 'disabled', buttonMappingEnabled: false, gyroEnabled: false, motionEnabled: false, gyroMotion: original.gyroMotion };
  const f = await fixture({ input: overwritten, config: { inputRestore: restore } }); await f.retry();
  assert(!f.config.inputRestore); assert.deepEqual(f.settings.outputTarget, original.outputTarget); assert.deepEqual(f.settings.gyroMotion, original.gyroMotion); assert(f.locked.locked); f.stop();
});
await check('停止订阅后，在飞旧异步任务不再落盘', async () => {
  const f = await fixture(); await f.blockConfig(); const count = f.calls.cas.length;
  const promise = f.retry(); await flush(); f.stop(); await f.unblockConfig(); await promise;
  assert.equal(f.calls.cas.length, count);
});
await check('overlay原子替换并参与配置哈希；退出不需要写回本机', async () => {
  const current = base(); const old = { identity: 'A', outputTarget: { persona: 'steamdeck' }, gyroMotion: { enabled: true, uniqueOld: 42 } };
  const r1 = repo.evaluateInputCas(current, current.revision, { gameOverride: old }); assert(r1.ok);
  const r2 = repo.evaluateInputCas(r1.value, r1.revision, { gameOverride: { identity: 'B', outputTarget: { persona: 'elite' }, gyroMotion: { enabled: false } } }); assert(r2.ok);
  assert(!('uniqueOld' in r2.value.gameOverride.gyroMotion)); assert.notEqual(r1.value.pendingConfigHash, r2.value.pendingConfigHash);
  const r3 = repo.evaluateInputCas(r2.value, r2.revision, { gameOverride: null }); assert(r3.ok); assert.deepEqual(r3.value.outputTarget, current.outputTarget);
});
await check('queued CAS在磁盘读取后复核取消条件，旧任务不落盘', async () => {
  let current = true, releaseRead, writes = 0;
  const durable = load('src/bridge/settingsRepository.ts', { './controllerPersona': personas, './settingsBaseline': baseline, './settingsSnapshot': load('src/bridge/settingsSnapshot.ts'), './inputStartupPolicy': load('src/bridge/inputStartupPolicy.ts', { './controllerPersona': personas }), './inputConfigHash': hash, './hcInputUtils': hc, './quickAppDefaults': load('src/bridge/quickAppDefaults.ts'),
    './api': { fs: { readTextFile: async () => new Promise(resolve => { releaseRead = resolve; }) },
      settingsStore: { write: async () => { writes++; return true; } } } });
  const document = repo.normalizeSettings({ input: base() });
  const pending = durable.compareAndSwapInputSettings(document.input.revision, { gameOverride: { identity:'A' } }, () => current);
  await flush(); assert(releaseRead); current = false; releaseRead(JSON.stringify(document));
  const result = await pending; assert.equal(result.reason, 'cancelled'); assert.equal(writes, 0);
});
await check('关闭订阅时已进入CAS队列的overlay提交被取消', async () => {
  const f = await fixture(); await f.blockCas(); const count = f.calls.cas.length;
  await f.changeEntry('a.exe', profile('elite', 'off')); f.stop(); await f.unblockCas();
  assert.equal(f.calls.cas.length, count); assert.equal(f.settings.gameOverride.outputTarget.persona, 'steamdeck');
});
await check('旧快照清理失败重试不再覆写已修复后的本机新编辑', async () => {
  const original = base(), overwritten = structuredClone(original);
  overwritten.outputTarget.persona = 'steamdeck'; overwritten.gyroMotion.enabled = true;
  const restore = { persona:'disabled', buttonMappingEnabled:false, gyroEnabled:false, motionEnabled:false, gyroMotion:original.gyroMotion };
  const f = await fixture({ input:overwritten, config:{inputRestore:restore}, clearRestoreFailures:1 });
  assert(f.config.inputRestore); assert.equal(f.settings.gameOverride, null);
  const edited = await f.repository.compareAndSwapInputSettings(f.settings.revision, { gyroMotion:{gyroMultiplier:4} }); assert(edited.ok);
  await f.retry(); assert(!f.config.inputRestore); assert.equal(f.settings.gyroMotion.gyroMultiplier, 4); assert(f.locked.locked); f.stop();
});
await check('已禁用条目、同值覆盖和无效会话的输入接管边界', async () => {
  const f = await fixture();
  assert.equal(f.input.buildGameInputOverlay(base(), {...profile('elite','on'),enabled:false}, 'A', 'S'), null);
  assert.equal(f.input.buildGameInputOverlay(base(), profile('elite','on'), 'A', ''), null);
  await f.changeEntry('a.exe', profile('disabled','off')); assert(f.locked.locked);
  assert.deepEqual(f.settings.gameOverride.outputTarget, base().outputTarget);
  assert.equal(f.input.effectiveInputPersona({...f.settings,gameOverride:{sessionId:'old-session',outputTarget:{persona:'elite'}}}), 'disabled');
  f.stop();
});
await check('真实顶部入口复用连续性能面板且不再保留旧核心策略保存函数', async () => {
  const source = fs.readFileSync(path.join(root,'src/components/GameCustomProfilePanel.vue'),'utf8');
  assert.match(source, /<ContinuousPerformancePanel[^>]+scope=\"dedicated\"/);
  assert(!source.includes('saveGameCustomPatch('));
  assert(!source.includes('selectCorePolicyMode'));
  assert(!source.includes('corePolicyMode'));
  assert(!source.includes('selectedModes'));
});
await check('同会话native开机基线不被旧游戏恢复快照覆盖', async () => {
  const initial = base(); initial.outputTarget.persona = 'elite'; initial.outputTarget.buttonMappingEnabled = true;
  initial.gyroMotion.enabled = false; initial.startupAppliedSession = 'native-session-1';
  const restore = {persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true,motionEnabled:true,gyroMotion:{...base().gyroMotion,enabled:true}};
  const f = await fixture({raw:null,input:initial,config:{inputRestore:restore}});
  assert.deepEqual(f.settings.outputTarget,initial.outputTarget); assert.deepEqual(f.settings.gyroMotion,initial.gyroMotion);
  assert.equal(f.settings.startupAppliedSession,'native-session-1');assert(!f.config.inputRestore);f.stop();
});
// Actual compiled pages with in-memory renderer: no WebView, real hardware, process or disk calls.
function element(type, text = '') { return { type, text, props: {}, children: [], parent: null, get textContent() { return this.text + this.children.map(n => n.textContent).join(''); } }; }
const renderer = vue.createRenderer({
  createElement: type => element(type), createText: text => element('#text',text), createComment: text => element('#comment',text),
  setText: (node,text) => { node.text = text; }, setElementText: (node,text) => { node.text = text; node.children = []; },
  parentNode: node => node.parent, nextSibling: node => node.parent?.children[node.parent.children.indexOf(node)+1] || null,
  insert(node,parent,anchor = null) { if(node.parent)node.parent.children.splice(node.parent.children.indexOf(node),1); node.parent = parent; const i=anchor?parent.children.indexOf(anchor):-1; if(i<0)parent.children.push(node);else parent.children.splice(i,0,node); },
  remove(node) { if(node.parent)node.parent.children.splice(node.parent.children.indexOf(node),1); node.parent = null; },
  patchProp(node,key,prev,next) { node.props[key] = next; },
});
const descendants = node => [node, ...node.children.flatMap(descendants)];
async function pageFixture(file, initial = base()) {
  const source = fs.readFileSync(path.join(root,file),'utf8'); const { descriptor } = parse(source, { filename:file });
  const compiled = compileScript(descriptor, { id: 'game-policy-test', inlineTemplate:true, templateOptions:{ compilerOptions:{ hoistStatic:false } } });
  let settings = structuredClone(initial), state = { locked:false, identity:'' }, callback, writes=0, joy=0, now=0, seq=0;
  const timers = new Map();
  const window = { addEventListener(){}, removeEventListener(){}, dispatchEvent(){}, setTimeout(fn,ms){const id=++seq;timers.set(id,{fn,at:now+ms});return id;},clearTimeout(id){timers.delete(id);} };
  const child = { props:['modelValue','disabled'], emits:['update:modelValue'], setup:(props,{emit})=>()=>vue.h('button', { disabled:props.disabled, 'data-stub-control':true, 'data-stub-model':props.modelValue, onTestUpdate:value=>emit('update:modelValue',value) }, String(props.modelValue ?? '')) };
  const imports = {
    '@/bridge/settingsRepository': { onInputSettingsChanged:()=>()=>{},loadSettings:async()=>({input:structuredClone(settings)}),compareAndSwapInputSettings:async(revision,patch)=>{ writes++;const result=repo.evaluateInputCas(settings,revision,patch);if(result.ok)settings=result.value;return result;} },
    '@/bridge/gameInputOverride': { getGameInputOverrideState:()=>state,subscribeGameInputOverrideState(cb){callback=cb;cb(state);return()=>{};} },
    '@/bridge/ipc': { on:()=>()=>{}, invoke:async()=>({status:'not-calibrated'}) }, '@/bridge/api':{shell:{open:async()=>{}}},
    '@/bridge/gameproc': { closeJoyxoffIfRunning:async()=>{joy++;} },
    '@/bridge/inputContracts':{ normalizeGyroTelemetry:()=>null }, '@/bridge/gyroMotionMapperMock':{mapGyroSample:()=>({x:0,y:0})},
    '@/bridge/virtualReportAssembler':{assembleCanonicalFrame:()=>({})},
    // R17: this newly imported helper is pure presentation; use its actual implementation.
    '@/bridge/virtualOutputStatus':load('src/bridge/virtualOutputStatus.ts'),
    '@/bridge/controllerShortcutRules':load('src/bridge/controllerShortcutRules.ts'),
    '@/bridge/yeman':{oemKeysGet:async()=>({familyId:'unknown',supported:false,keys:[]})},
    '@/bridge/uiLifecycle':{isUiVisible:()=>true,onUiVisibilityChange:()=>()=>{}},
    '@/bridge/gyroPresentation':load('src/bridge/gyroPresentation.ts',{}, {
      requestAnimationFrame:fn=>window.setTimeout(fn,16), cancelAnimationFrame:id=>window.clearTimeout(id),
    }),
  };
  for(const name of ['ScreenTouchpadsSettings','InlineIcon','ControllerShortcutsCard','ControllerFeedbackCapabilitiesCard','SteamDeckMouseSensitivity','SegButton','Slider','Toggle','Dropdown'])imports['@/components/'+name+'.vue']={__esModule:true,default:child};
  imports['@/views/ControllerShortcutEditorView.vue']={__esModule:true,default:child};
  imports['@/components/InlineIcon.vue']={__esModule:true,default:{setup:()=>()=>vue.h('span')}};
  const component=evaluate(compiled.content,imports,{window,document:{addEventListener(){},removeEventListener(){}},requestAnimationFrame:()=>{}}).default;
  const container=element('root');const app=renderer.createApp(component);app.mount(container);await flush();
  return { container, get settings(){return settings;}, get timerCount(){return timers.size;}, get writes(){return writes;},get joy(){return joy;}, async lock(){settings.gameOverride={identity:'A',sessionId:'S',outputTarget:{persona:'steamdeck'},gyroMotion:{enabled:true}};state={locked:true,identity:'A'};callback(state);await flush();}, async unlock(){settings.gameOverride=null;state={locked:false,identity:''};callback(state);await flush();}, async tick(ms){now+=ms;for(const[id,timer]of [...timers])if(timer.at<=now){timers.delete(id);timer.fn();}await flush();},unmount(){app.unmount();} };
}
await check('真实控制器页面显示本机选项，接管时提示和禁用；旁路点击无副作用', async()=>{
  const f=await pageFixture('src/views/ButtonMappingView.vue');await f.lock();
  assert(f.container.textContent.includes('游戏专属配置生效中'));
  const buttons=descendants(f.container).filter(n=>n.type==='button'&&n.props.class?.includes('persona-btn'));
  assert(buttons.length>=4);assert(buttons.every(n=>n.props.disabled));assert(buttons.find(n=>n.textContent==='关闭虚拟手柄').props.class.includes('active'));
  buttons.find(n=>n.textContent==='SteamDeck').props.onClick();await flush();assert.equal(f.writes,0);assert.equal(f.joy,0);
  await f.unlock();assert(!f.container.textContent.includes('游戏专属配置生效中'));assert(descendants(f.container).filter(n=>n.props.class?.includes('persona-btn')).every(n=>!n.props.disabled));f.unmount();
});
await check('真实陀螺仪页面保留本机状态并锁住开关、联动、预设和参数；不自动保存', async()=>{
  const f=await pageFixture('src/views/GyroMotionView.vue');await f.lock();assert(f.container.textContent.includes('游戏专属配置生效中'));
  const nodes=descendants(f.container);const power=nodes.filter(n=>n.type==='button'&&n.props.class?.includes('gyro-power-btn'));
  assert.equal(power.length,2);assert(power.every(n=>n.props.disabled));assert(!power[0].textContent.includes('已开启'));
  const controls=nodes.filter(n=>n.props['data-stub-control']);assert(controls.length>4);assert(controls.every(n=>n.props.disabled));
  power.forEach(n=>n.props.onClick());await f.tick(5000);assert.equal(f.writes,0);
  await f.unlock();await f.tick(5000);assert.equal(f.writes,0);f.unmount();
});
await check('运行手柄实际关闭→开启保留联动，关闭联动时不启gyro；切人格不重启已关闭gyro',async()=>{
  for(const linked of [true,false]) {
    const initial=base();initial.gyroMotion.virtualPadLink=linked;const memory=JSON.stringify(initial.gyroMotion.presets);
    const f=await pageFixture('src/views/ButtonMappingView.vue',initial);
    const click=async label=>{descendants(f.container).find(n=>n.type==='button'&&n.props.class?.includes('persona-btn')&&n.textContent===label).props.onClick();await flush();};
    await click('SteamDeck');assert.equal(f.settings.gyroMotion.enabled,linked);assert.equal(f.settings.outputTarget.gyroEnabled,linked);assert.equal(JSON.stringify(f.settings.gyroMotion.presets),memory);
    await click('关闭虚拟手柄');assert.equal(f.settings.gyroMotion.enabled,false);assert.equal(f.settings.outputTarget.gyroEnabled,false);f.unmount();
  }
  const initial=base();initial.outputTarget.persona='elite';initial.outputTarget.buttonMappingEnabled=true;initial.gyroMotion.virtualPadLink=true;
  const f=await pageFixture('src/views/ButtonMappingView.vue',initial);descendants(f.container).find(n=>n.type==='button'&&n.props.class?.includes('persona-btn')&&n.textContent==='PS5').props.onClick();await flush();
  assert.equal(f.settings.gyroMotion.enabled,false);assert.equal(f.settings.outputTarget.gyroEnabled,false);f.unmount();
});
await check('开机手柄开启但gyro关闭：陀螺仪内页加载/解锁不自动联动开启或保存', async () => {
  const initial = base(); initial.outputTarget.persona = 'steamdeck'; initial.outputTarget.buttonMappingEnabled = true;
  initial.gyroMotion.enabled = false; initial.gyroMotion.virtualPadLink = true;
  const f=await pageFixture('src/views/GyroMotionView.vue',initial);await f.tick(5000);
  const power=descendants(f.container).filter(n=>n.type==='button'&&n.props.class?.includes('gyro-power-btn'));
  assert(!power[0].textContent.includes('已开启')); assert.equal(f.settings.gyroMotion.enabled,false); assert.equal(f.writes,0);
  await f.lock();await f.unlock();await f.tick(5000);assert.equal(f.settings.gyroMotion.enabled,false);assert.equal(f.writes,0);f.unmount();
});
await check('接管前已排队的陀螺仪自动保存被取消，解锁也不重放', async()=>{
  const f=await pageFixture('src/views/GyroMotionView.vue');
  const slider=descendants(f.container).find(n=>n.props['data-stub-control'] && n.props.label==='陀螺仪权重【倍率】');
  assert(slider); slider.props.onTestUpdate(2.5); await flush(); assert(f.timerCount>0);
  assert.equal(f.writes,0); await f.lock(); await f.tick(5000); assert.equal(f.writes,0);
  await f.unlock(); await f.tick(5000); assert.equal(f.writes,0); f.unmount();
});
await check('常驻轮询/重试不随窗口隐藏暂停；原始终止游戏动作未改成延迟目标',()=>{
  const detect=fs.readFileSync(path.join(root,'src/bridge/gamedetect.ts'),'utf8');assert(detect.includes('{ pauseWhenHidden: false, runImmediately: true }'));
  const quick=fs.readFileSync(path.join(root,'src/components/GameQuickActions.vue'),'utf8');assert(quick.includes(':game="policyGame"'));assert(quick.includes('validateLockedGameTarget'));
  const perf=fs.readFileSync(path.join(root,'src/bridge/performanceSchedule.ts'),'utf8');assert(perf.includes('isPolicyTargetCurrent(expectedTarget)')); assert(perf.includes('ContinuousPerformancePanel'));
});

}

// Actual shared detector + IPC timeout + scheduler + policy owner. All native
// process replies and browser time are inert; no game or device is accessed.
async function flushDetection() { for(let i=0;i<100;i++) await Promise.resolve(); }
function detectionFixture(detectorSource = null) {
  let now = 0, seq = 0, visible = false;
  const timers = new Map(), events = new Map(), documentEvents = new Map(), bridgeListeners = [], messages = [];
  const eventTarget = handlers => ({
    addEventListener(name, cb) { if (!handlers.has(name)) handlers.set(name, new Set()); handlers.get(name).add(cb); },
    removeEventListener(name, cb) { handlers.get(name)?.delete(cb); },
    dispatchEvent(event) { for (const cb of [...handlers.get(event.type) || []]) cb(event); return true; },
  });
  const win = eventTarget(events), doc = { ...eventTarget(documentEvents), visibilityState:'hidden' };
  const f = { native:null, hold:false, messages, timers, events, scheduler:null, detector:null, policy:null, schedules:[] };
  const clock = {performance:{now:()=>now},setTimeout(fn,ms){const id=++seq;timers.set(id,{at:now+ms,fn});return id;},clearTimeout(id){timers.delete(id);},
    window:win,document:doc,CustomEvent:class{constructor(type,opts){this.type=type;this.detail=opts?.detail;}},queueMicrotask};
  Object.assign(win,{setTimeout:clock.setTimeout,clearTimeout:clock.clearTimeout,chrome:{webview:{
    addEventListener(_name,cb){bridgeListeners.push(cb);},
    postMessage(msg){messages.push(structuredClone(msg));if(msg.cmd!=='game.detect')throw Error('Unexpected native IO '+msg.cmd);if(f.hold)return;
      const value=msg.args?.pid && f.native?.valid!==false && msg.args.pid!==f.native?.pid?null:structuredClone(f.native);
      queueMicrotask(()=>f.reply(msg.id,value));},
  }}});
  f.reply=(id,result)=>{for(const cb of bridgeListeners)cb({data:{id,result}});};
  f.fail=(id,error)=>{for(const cb of bridgeListeners)cb({data:{id,error}});};
  f.signal=(name,detail={})=>win.dispatchEvent({type:name,detail});
  f.emitNative=(event,data={})=>{for(const cb of bridgeListeners)cb({data:{event,data}});};
  f.show=()=>{visible=true;doc.visibilityState='visible';doc.dispatchEvent({type:'visibilitychange'});};
  f.advance=async(ms)=>{const end=now+ms;for(let guard=0;guard<1000;guard++){const due=[...timers].filter(([,t])=>t.at<=end).sort((a,b)=>a[1].at-b[1].at)[0];if(!due)break;now=Math.max(now,due[1].at);timers.delete(due[0]);due[1].fn();await flushDetection();}now=end;await flushDetection();};
  const ipc=load('src/bridge/ipc.ts',{},clock);
  const scheduler=load('src/scheduler.ts',{'./bridge/uiLifecycle':{isUiVisible:()=>visible,onUiVisibilityChange:()=>()=>{}}},clock);
  f.scheduler={...scheduler,registerScheduledTask(...args){f.schedules.push({name:args[0],ms:args[1],options:args[3]});return scheduler.registerScheduledTask(...args);}};
  f.detector=evaluate(detectorSource??fs.readFileSync(path.join(root,'src/bridge/gamedetect.ts'),'utf8'),{'./ipc':ipc,'@/scheduler':f.scheduler,'../../PowerControl/GameTitlePolicy.json':JSON.parse(fs.readFileSync(path.join(root,'PowerControl/GameTitlePolicy.json'),'utf8'))},clock);
  f.policy=load('src/bridge/gamePolicyTarget.ts',{'./gamedetect':f.detector,'./gamePolicyHysteresis':hysteresis},clock);
  return f;
}
const TMNT={pid:11752,processCreated:'134372974230246308',name:'tmnt',title:"Teenage Mutant Ninja Turtles: Shredder's Revenge",path:'E:\\GAME\\Teenage Mutant Ninja Turtles Shredders Revenge Radical Reptiles\\TMNT.exe',source:'whitelist',whitelistRule:'tmnt',ts:1792823823460};
function blockDisplayScheduler(f) { return f.scheduler.registerScheduledTask('inert-slow-policy',2000,()=>new Promise(()=>{}),{runImmediately:true,pauseWhenHidden:false}); }
await check('Xbox隐藏且共享调度器等待其他任务时，原生目标通知主动更新顶部与策略缓存',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f),seen=[];const stop=f.detector.subscribeGameStatus(g=>seen.push(g));await f.policy.detectPolicyGame();assert.equal(f.policy.getPolicyGame(),null);
 f.native={...TMNT};f.signal('ipc:game.target.changed',{pid:999999,path:'untrusted event data'});await flushDetection();assert(seen.at(-1),'a committed native target must exist before checking its PID');assert.equal(seen.at(-1).pid,TMNT.pid);assert.equal(f.policy.getPolicyGame().pid,TMNT.pid);assert(f.messages.every(m=>m.cmd==='game.detect'&&!m.args.pid));stop();block();
});
await check('没有原生推送的旧壳也继续按1250ms后台发现游戏',async()=>{
 const f=detectionFixture(),seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g));await flushDetection();f.native={...TMNT};await f.advance(1250);assert.equal(seen.at(-1).pid,TMNT.pid);stop();assert.equal(f.timers.size,0);
});
await check('退出Xbox全屏恢复窗口或文档时自动校对，用户无需手动刷新',async()=>{
 for(const event of ['ipc:window.shown','ipc:window.restored','ipc:window.summoned','ipc:window.focus','ipc:power.resumed','document-visible']){
  const f=detectionFixture(),block=blockDisplayScheduler(f),seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g));await flushDetection();f.native={...TMNT};if(event==='document-visible')f.show();else f.signal(event);await flushDetection();assert.equal(seen.at(-1).pid,TMNT.pid,event);stop();block();
 }
});
await check('识别IPC无回执在8秒内释放共享等待，下一轮自动恢复且迟到回执不能覆盖新结果',async()=>{
 const f=detectionFixture(),seen=[];f.hold=true;const stop=f.detector.subscribeGameStatus(g=>seen.push(g));await flushDetection();assert.equal(f.messages.length,1);const stale=f.messages[0];
 await f.advance(7999);assert.equal(f.messages.length,1);f.hold=false;f.native={...TMNT};await f.advance(1251);assert(seen.at(-1),'a committed native target must exist before checking its PID');assert.equal(seen.at(-1).pid,TMNT.pid);assert.equal(f.messages.length,2);
 f.reply(stale.id,{...TMNT,pid:1688,processCreated:'134372971316238043'});await flushDetection();assert(seen.at(-1),'a committed native target must exist before checking its PID');assert.equal(seen.at(-1).pid,TMNT.pid);stop();
});
await check('同一进程的完整标题晚到也通知顶部和策略订阅，不需要换游戏',async()=>{
 const f=detectionFixture();f.native={...TMNT,title:''};const policySeen=[];f.policy.subscribePolicyGameStatus(g=>policySeen.push(g));await flushDetection();assert.equal(f.policy.getPolicyGame().title,'');
 f.native={...TMNT};f.signal('ipc:game.target.changed');await flushDetection();assert.equal(policySeen.at(-1).title,TMNT.title);assert.equal(f.policy.getPolicyGame().pid,TMNT.pid);
});
await check('连续切换5款游戏均在100ms内广播唯一身份而不写死名称或PID',async()=>{
 const f=detectionFixture(),seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g));await f.policy.detectPolicyGame();
 for(const[idx,title]of ['Cyberpunk 2077','Crusader Kings III','ELDEN RING','Stardew Valley','God of War'].entries()){
  const g={...TMNT,title,name:'inert-'+idx,path:'D:\\Inert\\'+idx+'\\game.exe',pid:21000+idx,processCreated:String(134372974230246400n+BigInt(idx))};f.native=g;f.signal('ipc:game.target.changed');await f.advance(100);assert.equal(seen.at(-1).pid,g.pid);assert.equal(f.policy.getPolicyGame().pid,g.pid,title);
 }stop();
});
await check('原生通知突发时合并同一识别请求且不信任通知中的PID',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f),stop=f.detector.subscribeGameStatus(()=>{});await flushDetection();f.native={...TMNT};f.hold=true;const before=f.messages.length;
 for(let i=0;i<30;i++)f.signal('ipc:game.target.changed',{pid:i+5});await flushDetection();assert.equal(f.messages.length,before+1);f.reply(f.messages.at(-1).id,TMNT);await flushDetection();stop();block();
});
await check('严格指定PID失败时仍返回空，不借自动校对改选别的游戏',async()=>{
 const f=detectionFixture();f.native={...TMNT};const result=await f.detector.refreshGameStatusStrict(99999);assert.equal(result,null);assert.equal(f.messages.at(-1).args.pid,99999);
});
await check('普通旧请求在严格PID刷新之后完成不能覆盖新目标',async()=>{
 const f=detectionFixture();f.hold=true;const seen=[],block=blockDisplayScheduler(f),stop=f.detector.subscribeGameStatus(g=>seen.push(g));const oldRead=f.detector.refreshGameStatus();await flushDetection();const old=f.messages[0];
 const strict=f.detector.refreshGameStatusStrict(TMNT.pid);await flushDetection();f.reply(f.messages.at(-1).id,TMNT);await strict;f.reply(old.id,null);await oldRead;await flushDetection();assert(seen.at(-1),'a committed native target must exist before checking its PID');assert.equal(seen.at(-1).pid,TMNT.pid);stop();block();
});
await check('订阅全部撤销后原生事件不再产生识别IO',async()=>{
 const f=detectionFixture(),stop=f.detector.subscribeGameStatus(()=>{});await flushDetection();stop();const before=f.messages.length;f.signal('ipc:game.target.changed');await flushDetection();assert.equal(f.messages.length,before);assert.equal(f.timers.size,0);
});


await check('严格指定PID等待时自动通知合并等待，不并发改选其他游戏',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f),seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g));await flushDetection();f.hold=true;
 const strict=f.detector.refreshGameStatusStrict(99999);await flushDetection();const count=f.messages.length,request=f.messages.at(-1);
 f.signal('ipc:game.target.changed');await flushDetection();assert.equal(f.messages.length,count);f.reply(request.id,null);await strict;await flushDetection();assert.equal(seen.at(-1),null);stop();block();
});

await check('严格PID查询失败时后台校对保留旧状态并吸收传输错误，手动调用仍收到错误',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f),seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g));f.native={...TMNT};await f.detector.refreshGameStatusStrict();
 f.hold=true;const strict=f.detector.refreshGameStatusStrict(99999),rejected=assert.rejects(strict,/inert transport failure/);await flushDetection();const joined=f.detector.refreshGameStatus();f.signal('ipc:game.target.changed');
 f.fail(f.messages.at(-1).id,'inert transport failure');await rejected;assert.equal((await joined).pid,TMNT.pid);await flushDetection();assert.equal(seen.at(-1).pid,TMNT.pid);stop();block();
});
await check('单个策略订阅抛错不会阻断Decky及修改器共享状态通知',async()=>{
 const f=detectionFixture();await f.policy.detectPolicyGame();f.policy.subscribePolicyGameStatus(g=>{if(g)throw Error('inert broken observer');});const seen=[];f.policy.subscribePolicyGameStatus(g=>seen.push(g));
 f.native={...TMNT};f.signal('ipc:game.target.changed');await flushDetection();assert.equal(seen.at(-1).pid,TMNT.pid);
});
await check('实际Decky入口通过同一检测器恢复空缓存，随后原生切换通知直接推送新身份',async()=>{
 const {createMirrorHostFixture}=await import('./fixtures/decky_mirror_host_fixture.mjs');
 const f=detectionFixture(),block=blockDisplayScheduler(f);await f.policy.detectPolicyGame();let host;
 f.policy.subscribePolicyGameStatus(g=>{if(host){host.state.game=g;host.gameChanged();}});
 host=await createMirrorHostFixture({policyRefresh:()=>f.policy.refreshPolicyGameStrict(),policyReadiness:f.detector});host.state.game=null;host.ready();
 const runId='c'.repeat(32),peer={connection:'1',runId,connected:true};
 const read=async()=>{host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flushDetection();const reply=host.replies.filter(r=>r.message.type==='snapshot').at(-1);assert(reply,'actual host must publish its shared-target snapshot');return reply.message.snapshot;};
 f.native={...TMNT};await read();await flushDetection();let latest=host.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
 assert.equal(latest.game.label,TMNT.title);assert(latest.game.identity.includes(String(TMNT.pid)));assert.equal(host.counts.gameReconciles,1);
 const next={...TMNT,pid:21003,processCreated:'134372974230246500',title:'Next inert game',path:'D:/Inert/Next.exe'};f.native=next;f.signal('ipc:game.target.changed');await flushDetection();
 latest=host.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;assert.equal(latest.game.label,next.title);assert(latest.game.identity.includes(String(next.pid)));assert.equal(host.counts.gameReconciles,1);assert.equal(host.counts.saves,0);
 const identity=latest.game.identity;f.hold=true;const fault=f.detector.refreshGameStatusStrict(),rejected=assert.rejects(fault);await flushDetection();f.fail(f.messages.at(-1).id,'inert actual shared-target outage');await rejected;await flushDetection();latest=host.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;assert.equal(latest.actions.game,false);assert.equal(latest.game.identity,identity);
 f.hold=false;await f.advance(500);await flushDetection();latest=host.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;assert.equal(latest.actions.game,true);assert.equal(latest.game.identity,identity);assert.equal(host.counts.gameReconciles,1);assert.equal(host.counts.saves,0);host.stop();assert.equal(host.readinessSubscriberCount(),0);block();
});

await check('反向去掉IPC超时会留下永久等待，后续自动轮询无法再发现游戏',async()=>{
 const source=fs.readFileSync(path.join(root,'src/bridge/gamedetect.ts'),'utf8');assert(source.includes(', { timeoutMs: DETECTION_TIMEOUT_MS }'));
 const f=detectionFixture(source.replace(', { timeoutMs: DETECTION_TIMEOUT_MS }','')),seen=[];f.hold=true;const stop=f.detector.subscribeGameStatus(g=>seen.push(g));await flushDetection();f.native={...TMNT};await f.advance(20000);assert.equal(f.messages.length,1);assert.equal(seen.at(-1),null);stop();
});
await check('反向去掉原生通知处理可复现隐藏时调度被占用后顶部仍为空',async()=>{
 const source=fs.readFileSync(path.join(root,'src/bridge/gamedetect.ts'),'utf8'),binding="window.addEventListener('ipc:game.target.changed',";assert(source.includes(binding));
 const f=detectionFixture(source.replace(binding,"window.addEventListener('inert-disabled-target-change',")),block=blockDisplayScheduler(f),seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g));await flushDetection();f.native={...TMNT};f.signal('ipc:game.target.changed');await flushDetection();assert.equal(seen.at(-1),null);stop();block();
});


function versionedGame(game=TMNT,generation=10,identityGeneration=generation){return {...game,generation,identityGeneration};}
function noGame(generation,identityGeneration=generation){return {valid:false,pid:0,generation,identityGeneration};}
const NEXT_GAME={...TMNT,pid:21003,processCreated:'134372974230246500',title:'Next inert game',path:'D:/Inert/Next.exe'};

await check('初始化未知不广播空目标；成功的版本化空回包才初始化且后台只有一个任务',async()=>{
 const f=detectionFixture(),seen=[];f.hold=true;f.policy.subscribePolicyGameStatus(g=>seen.push(g));await flushDetection();
 assert(!f.detector.isGameStatusInitialized());assert(!f.policy.isPolicyGameInitialized());assert.equal(seen.length,0);assert.equal(f.messages.length,1);
 const first=f.messages[0];f.fail(first.id,'inert initialization failure');await flushDetection();assert(!f.policy.isPolicyGameInitialized());assert.equal(seen.length,0);
 f.hold=false;f.native=noGame(2);await f.advance(1250);assert(f.policy.isPolicyGameInitialized());assert.deepEqual(seen,[null]);assert.equal(f.schedules.filter(x=>x.name==='game-status').length,1);
});
await check('版本化A→B无10秒策略分叉；直接detect强制读也广播给所有消费者',async()=>{
 const f=detectionFixture(),seen=[],policySeen=[];f.native=versionedGame();f.detector.subscribeGameStatus(g=>seen.push(g));f.policy.subscribePolicyGameStatus(g=>policySeen.push(g));await flushDetection();
 f.native=versionedGame(NEXT_GAME,11);const next=await f.detector.detectGame(true);assert.equal(next.pid,NEXT_GAME.pid);assert.equal(seen.at(-1).pid,NEXT_GAME.pid);assert.equal(policySeen.at(-1).pid,NEXT_GAME.pid);assert.equal(f.policy.getPolicyGame().identityGeneration,11);
 const reads=f.messages.length;assert.equal((await f.detector.detectGame()).pid,NEXT_GAME.pid);assert.equal((await f.policy.detectPolicyGame()).pid,NEXT_GAME.pid);assert.equal(f.messages.length,reads);
});
await check('同PID新birth立即广播且旧身份验证只读当前源，绝不带旧PID改选',async()=>{
 const f=detectionFixture();f.native=versionedGame();await f.policy.detectPolicyGame();const old=f.policy.getPolicyGame(),seen=[];f.policy.subscribePolicyGameStatus(g=>seen.push(g));
 f.native=versionedGame({...TMNT,processCreated:'134372974230246999'},11);f.signal('ipc:game.target.changed',{generation:11,pid:999999});await flushDetection();
 assert.equal(seen.at(-1).processCreated,f.native.processCreated);assert(!f.policy.isPolicyTargetCurrent(old));assert.equal(await f.detector.validateGameTarget(old),null);assert(f.messages.every(m=>!m.args?.pid));
});
await check('身份代次改变时同PID同birth也广播一次，消费缓存不会重复枚举',async()=>{
 const f=detectionFixture();f.native=versionedGame();await f.policy.detectPolicyGame();const seen=[];f.policy.subscribePolicyGameStatus(g=>seen.push(g));
 f.native=versionedGame(TMNT,11,11);f.signal('ipc:game.target.changed',{generation:11});await flushDetection();assert.deepEqual(seen.map(g=>g.identityGeneration),[10,11]);
 await f.detector.detectGame();await f.policy.detectPolicyGame();assert.equal(seen.length,2);assert(!f.detector.isCurrentGameTarget(versionedGame()));
});
await check('带版本的无游戏墓碑立即清空所有投影；迟到旧有效回包不能复活',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.policy.detectPolicyGame();const seen=[];f.policy.subscribePolicyGameStatus(g=>seen.push(g));f.hold=true;
 const oldRead=f.detector.refreshGameStatusStrict();await flushDetection();const old=f.messages.at(-1);const clear=f.detector.refreshGameStatusStrict(99999);await flushDetection();f.reply(f.messages.at(-1).id,noGame(12));await clear;
 f.reply(old.id,versionedGame(TMNT,10));assert.equal(await oldRead,null);await flushDetection();assert.equal(f.detector.getCurrentGame(),null);assert.equal(f.policy.getPolicyGame(),null);assert.equal(seen.at(-1),null);assert.equal(seen.filter(g=>g===null).length,1);block();
});
await check('版本化会话拒绝裸null、无版本及半对代次；传输/协议失败不伪装退出',async()=>{
 const f=detectionFixture();f.native=versionedGame();await f.policy.detectPolicyGame();const seen=[];f.policy.subscribePolicyGameStatus(g=>seen.push(g));
 for(const invalid of [null,{...TMNT},{...TMNT,generation:11},{...TMNT,identityGeneration:11},noGame(11,12),{valid:false,pid:11752,generation:11,identityGeneration:11}]){
  f.native=invalid;await assert.rejects(f.policy.refreshPolicyGameStrict());assert.equal(f.policy.getPolicyGame().pid,TMNT.pid);assert.equal(seen.length,1);
  assert.equal(f.detector.getCurrentGame().generation,10);assert.equal(f.policy.getPolicyGame().identityGeneration,10);
  assert(!f.detector.isGameStatusCurrent());assert(!f.detector.isCurrentGameTarget(versionedGame()));assert(!f.policy.isPolicyTargetCurrent(versionedGame()));
  f.native=versionedGame();await f.policy.refreshPolicyGameStrict();assert(f.detector.isGameStatusCurrent());assert(f.policy.isPolicyTargetCurrent(versionedGame()));assert.equal(seen.length,1);
 }
 f.hold=true;const strict=f.policy.refreshPolicyGameStrict(),rejected=assert.rejects(strict,/inert transport failure/);await flushDetection();f.fail(f.messages.at(-1).id,'inert transport failure');await rejected;assert.equal(f.policy.getPolicyGame().pid,TMNT.pid);assert.equal(seen.length,1);
 assert(!f.detector.isGameStatusCurrent());assert(!f.policy.isPolicyTargetCurrent(versionedGame()));f.hold=false;f.native=versionedGame();await f.policy.refreshPolicyGameStrict();assert(f.detector.isGameStatusCurrent());assert(f.policy.isPolicyTargetCurrent(versionedGame()));assert.equal(seen.length,1);
});
await check('版本与身份代次单调；同版本冲突和不换代次的身份变化不能授权',async()=>{
 const f=detectionFixture();f.native=versionedGame();await f.policy.detectPolicyGame();const seen=[];f.policy.subscribePolicyGameStatus(g=>seen.push(g));
 for(const invalid of [versionedGame(NEXT_GAME,10,10),versionedGame({...TMNT,title:'Conflicting same-version metadata'},10,10),versionedGame(TMNT,11,9),versionedGame(NEXT_GAME,11,10),noGame(11,10)]){
  f.native=invalid;await assert.rejects(f.detector.refreshGameStatusStrict());assert.equal(f.policy.getPolicyGame().pid,TMNT.pid);assert.equal(f.policy.getPolicyGame().identityGeneration,10);assert.equal(seen.length,1);
 }
 f.native=versionedGame({...TMNT,title:'Metadata only'},11,10);await f.detector.detectGame(true);assert.equal(f.policy.getPolicyGame().title,'Metadata only');assert.equal(f.policy.getPolicyGame().identityGeneration,10);
});
await check('inFlight期间原生新代次使旧回包失效；补读唯一源后才发布并返回新游戏',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.policy.detectPolicyGame();const seen=[];f.policy.subscribePolicyGameStatus(g=>seen.push(g));f.hold=true;
 const read=f.detector.refreshGameStatusStrict();await flushDetection();const stale=f.messages.at(-1),before=f.messages.length;
 f.signal('ipc:game.target.changed',{generation:11,pid:999999,path:'untrusted signal'});await flushDetection();assert.equal(f.messages.length,before);
 f.native=versionedGame(NEXT_GAME,11);f.hold=false;f.reply(stale.id,versionedGame());const result=await read;await flushDetection();assert.equal(result.pid,NEXT_GAME.pid);assert.deepEqual(seen.map(g=>g.pid),[TMNT.pid,NEXT_GAME.pid]);assert.equal(f.messages.length,before+1);assert(f.messages.every(m=>!m.args?.pid));block();
});
await check('事件水位超过连续回包时最多补读两次，失败保留旧快照但不授权操作',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.policy.detectPolicyGame();const old=f.policy.getPolicyGame();f.hold=true;
 const read=f.detector.refreshGameStatusStrict(),rejected=assert.rejects(read);await flushDetection();const start=f.messages.length;f.signal('ipc:game.target.changed',{generation:20});
 for(let i=0;i<3;i++){const request=f.messages.at(-1);f.reply(request.id,versionedGame(TMNT,10+i,10));await flushDetection();}
 await rejected;assert.equal(f.messages.length,start+2);assert.equal(f.policy.getPolicyGame().pid,TMNT.pid);assert(!f.detector.isCurrentGameTarget(old));block();
});
await check('晚到旧版本和旧显式选择不覆盖较新目标；等待旧读的调用也得到新权威结果',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.policy.detectPolicyGame();const seen=[];f.policy.subscribePolicyGameStatus(g=>seen.push(g));f.hold=true;
 const old=f.detector.refreshGameStatusStrict();await flushDetection();const stale=f.messages.at(-1);
 const choose=f.detector.refreshGameStatusStrict(NEXT_GAME.pid);await flushDetection();f.reply(f.messages.at(-1).id,versionedGame(NEXT_GAME,11));await choose;
 f.reply(stale.id,versionedGame());assert.equal((await old).pid,NEXT_GAME.pid);assert.deepEqual(seen.map(g=>g.pid),[TMNT.pid,NEXT_GAME.pid]);block();
});
await check('读者和订阅者的修改/抛错不能串改唯一源或阻断后续消费者',async()=>{
 const f=detectionFixture();f.native=versionedGame();await f.policy.detectPolicyGame();const rawSeen=[],policySeen=[];
 f.detector.subscribeGameStatus(g=>{if(g){g.pid=9;g.path='mutated';throw Error('inert raw observer');}});
 f.detector.subscribeGameStatus(g=>rawSeen.push(g));
 f.policy.subscribePolicyGameStatus(g=>{if(g){g.pid=8;g.path='mutated';throw Error('inert policy observer');}});f.policy.subscribePolicyGameStatus(g=>policySeen.push(g));
 const raw=f.detector.getCurrentGame(),policy=f.policy.getPolicyGame(),read=await f.policy.detectPolicyGame();raw.pid=7;policy.pid=6;read.pid=5;
 f.native=versionedGame(NEXT_GAME,11);await f.detector.detectGame(true);assert.equal(rawSeen.at(-1).pid,NEXT_GAME.pid);assert.equal(policySeen.at(-1).pid,NEXT_GAME.pid);assert.equal(f.policy.getPolicyGame().path,NEXT_GAME.path);
});
await check('多个订阅者和策略兼容入口只登记一个1250ms后台任务，不产生第二PID轮询',async()=>{
 const f=detectionFixture();f.native=versionedGame();const stops=[];for(let i=0;i<12;i++){stops.push(f.detector.subscribeGameStatus(()=>{}));stops.push(f.policy.subscribePolicyGameStatus(()=>{}));}await flushDetection();
 await Promise.all(Array.from({length:12},()=>f.policy.detectPolicyGame()));assert.equal(f.messages.length,1);assert.deepEqual(f.schedules.map(x=>[x.name,x.ms]),[['game-status',1250]]);assert(f.schedules[0].options.pauseWhenHidden===false);
 for(const stop of stops)stop();
 // The first observer can synchronously add another observer in its initial
 // callback. The inner registration starts the same poll; the outer must not
 // require size===1 or start a duplicate after the inner owner is installed.
 const reentrant=detectionFixture(),outerSeen=[],innerSeen=[];reentrant.native=versionedGame();let initial=true,stopInner;
 const stopOuter=reentrant.detector.subscribeGameStatus(g=>{
  outerSeen.push(g);
  if(initial){initial=false;stopInner=reentrant.detector.subscribeGameStatus(next=>innerSeen.push(next));}
 });
 await flushDetection();assert.equal(reentrant.schedules.filter(x=>x.name==='game-status').length,1);assert.equal(reentrant.messages.length,1);
 assert.equal(outerSeen.at(-1).pid,TMNT.pid);assert.equal(innerSeen.at(-1).pid,TMNT.pid);
 stopOuter();assert(reentrant.timers.size>0);reentrant.native=versionedGame(NEXT_GAME,11);await reentrant.advance(1250);
 assert.equal(reentrant.messages.length,2);assert.equal(innerSeen.at(-1).pid,NEXT_GAME.pid);assert.equal(reentrant.schedules.filter(x=>x.name==='game-status').length,1);
 stopInner();assert.equal(reentrant.timers.size,0);const reads=reentrant.messages.length;reentrant.signal('ipc:game.target.changed',{generation:12});await flushDetection();assert.equal(reentrant.messages.length,reads);
 // Revert only the old admission condition in memory: the original defect must
 // miss the poll with the same real scheduler and nested initial callback.
 const detectorSource=fs.readFileSync(path.join(root,'src/bridge/gamedetect.ts'),'utf8'),condition='if (listeners.size > 0 && !stopPollSchedule)';assert(detectorSource.includes(condition));
 const broken=detectionFixture(detectorSource.replace(condition,'if (listeners.size === 1 && !stopPollSchedule)'));let entered=false,stopBrokenInner;
 const stopBrokenOuter=broken.detector.subscribeGameStatus(()=>{if(!entered){entered=true;stopBrokenInner=broken.detector.subscribeGameStatus(()=>{});}});
 await flushDetection();assert.equal(broken.schedules.length,0);assert.equal(broken.messages.length,0);assert.equal(broken.timers.size,0);stopBrokenInner();stopBrokenOuter();
});
await check('已有已知目标时首次策略订阅只消费缓存，不另做初始化查询',async()=>{
 const f=detectionFixture();f.native=versionedGame();f.detector.subscribeGameStatus(()=>{});await flushDetection();const before=f.messages.length,seen=[];f.policy.subscribePolicyGameStatus(g=>seen.push(g));await flushDetection();
 assert.equal(f.messages.length,before);assert.equal(seen[0].pid,TMNT.pid);assert(f.policy.isPolicyGameInitialized());
});
await check('广播回调重入读取/选择时订阅顺序和代次单调，晚回包不能把B变回A',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=noGame(1);await f.policy.detectPolicyGame();const seen=[];let nested;
 f.policy.subscribePolicyGameStatus(g=>{if(g?.pid===TMNT.pid&&!nested){f.native=versionedGame(NEXT_GAME,11);nested=f.detector.refreshGameStatusStrict(NEXT_GAME.pid);}});
 f.policy.subscribePolicyGameStatus(g=>{if(g)seen.push([g.pid,g.generation,g.identityGeneration]);});
 f.native=versionedGame();await f.detector.detectGame(true);await nested;await flushDetection();assert.deepEqual(seen,[[TMNT.pid,10,10],[NEXT_GAME.pid,11,11]]);assert.equal(f.policy.getPolicyGame().pid,NEXT_GAME.pid);block();
});
await check('原始快捷session的A验证晚回包不能覆盖新B锁，校验不传旧PID',async()=>{
 const f=detectionFixture();f.native=versionedGame();await f.policy.detectPolicyGame();const session=load('src/bridge/gameQuickSession.ts',{'./gamedetect':f.detector});session.lockGameTarget(f.detector.getCurrentGame());f.hold=true;
 const validation=session.validateLockedGameTarget();await flushDetection();const old=f.messages.at(-1);assert(!old.args?.pid);
 const selection=f.detector.refreshGameStatusStrict(NEXT_GAME.pid);await flushDetection();f.reply(f.messages.at(-1).id,versionedGame(NEXT_GAME,11));await selection;session.lockGameTarget(f.detector.getCurrentGame());
 f.reply(old.id,versionedGame());assert.equal(await validation,null);assert.equal(session.getLockedGameTarget().pid,NEXT_GAME.pid);assert.equal(session.getLockedGameTarget().identityGeneration,11);
});
await check('原始快捷session新B校验不复用旧A在途校验，旧finally不能丢弃新验证',async()=>{
 const f=detectionFixture();f.native=versionedGame();await f.policy.detectPolicyGame();
 const session=load('src/bridge/gameQuickSession.ts',{'./gamedetect':f.detector});session.lockGameTarget(f.detector.getCurrentGame());f.hold=true;
 const validationA=session.validateLockedGameTarget();await flushDetection();const requestA=f.messages.at(-1);
 const selectB=f.detector.refreshGameStatusStrict(NEXT_GAME.pid);await flushDetection();f.reply(f.messages.at(-1).id,versionedGame(NEXT_GAME,11));await selectB;
 const lockedB=session.lockGameTarget(f.detector.getCurrentGame()),before=f.messages.length;
 const validationB=session.validateLockedGameTarget();await flushDetection();
 assert.equal(f.messages.length,before+1,'B lock owns a separate validation, not the old A promise');const requestB=f.messages.at(-1);assert(!requestB.args?.pid);
 f.reply(requestA.id,versionedGame());assert.equal(await validationA,null);assert.equal(session.getLockedGameTarget().lockGeneration,lockedB.lockGeneration);
 const joinedB=session.validateLockedGameTarget();await flushDetection();assert.equal(f.messages.length,before+1,'old A finally cannot discard B validation');
 f.reply(requestB.id,versionedGame(NEXT_GAME,12,11));const [resultB,resultJoinedB]=await Promise.all([validationB,joinedB]);
 assert.equal(resultB.pid,NEXT_GAME.pid);assert.equal(resultJoinedB.lockGeneration,lockedB.lockGeneration);assert.equal(session.getLockedGameTarget().identityGeneration,11);
});
await check('原始快捷session的A验证晚回包不能复活已解锁目标，旧失败也不能清空B锁',async()=>{
 for(const fail of [false,true]){
  const f=detectionFixture();f.native=versionedGame();await f.policy.detectPolicyGame();const session=load('src/bridge/gameQuickSession.ts',{'./gamedetect':f.detector});session.lockGameTarget(f.detector.getCurrentGame());f.hold=true;
  const validation=session.validateLockedGameTarget();await flushDetection();const old=f.messages.at(-1);session.unlockGameTarget();
  if(fail){const selection=f.detector.refreshGameStatusStrict(NEXT_GAME.pid);await flushDetection();f.reply(f.messages.at(-1).id,versionedGame(NEXT_GAME,11));await selection;session.lockGameTarget(f.detector.getCurrentGame());f.fail(old.id,'inert old validation failed');}
  else f.reply(old.id,versionedGame());
  assert.equal(await validation,null);assert.equal(session.getLockedGameTarget()?.pid??null,fail?NEXT_GAME.pid:null);
  assert(f.detector.isGameStatusCurrent());assert(f.detector.isCurrentGameTarget(f.detector.getCurrentGame()));
 }
});



await check('风暴：1000个尚未发出的交替显式选择只发送最后一项且全部调用收敛',async()=>{
 const f=detectionFixture();f.hold=true;const reads=[];
 for(let i=0;i<1000;i++)reads.push(f.detector.refreshGameStatusStrict(i%2?NEXT_GAME.pid:TMNT.pid));
 await flushDetection();assert.equal(f.messages.length,1,'unissued superseded selections must not perform native IO');assert.equal(f.messages[0].args.pid,NEXT_GAME.pid);
 f.reply(f.messages[0].id,versionedGame(NEXT_GAME,11));const results=await Promise.all(reads);assert(results.every(g=>g?.pid===NEXT_GAME.pid));assert(f.detector.isGameStatusCurrent());assert.equal(f.timers.size,0);
});
await check('自愈：共享调度器被占用时目标通知读取失败会有界重试并恢复新目标',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g));f.hold=true;
 f.signal('ipc:game.target.changed',{generation:11});await flushDetection();f.fail(f.messages.at(-1).id,'inert event read failed');await flushDetection();assert(!f.detector.isGameStatusCurrent());
 const before=f.messages.length;f.native=versionedGame(NEXT_GAME,11);f.hold=false;await f.advance(500);assert.equal(f.messages.length,before+1,'a failed event read must recover without another event or scheduler slot');assert.equal(seen.at(-1)?.pid,NEXT_GAME.pid);assert(f.detector.isGameStatusCurrent());stop();block();assert.equal(f.timers.size,0);
});
await check('风暴：1000个跨微任务目标通知保留首读并合并尾部校对到最终代次',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g)),before=f.messages.length;
 for(let i=0;i<1000;i++){f.native=versionedGame(i%2?NEXT_GAME:TMNT,11+i);f.signal('ipc:game.target.changed',{generation:11+i});await flushDetection();}
 assert(f.messages.length-before<=1,'paced notifications in the same 100ms window must share an observation budget');assert(!f.detector.isGameStatusCurrent());await f.advance(100);
 assert.equal(f.messages.length,before+2);assert.equal(seen.at(-1)?.pid,NEXT_GAME.pid);assert.equal(seen.at(-1)?.generation,1010);assert(f.detector.isGameStatusCurrent());stop();block();assert.equal(f.timers.size,0);
});
await check('自愈：没有新事件时额外重试最多三次，退避后恢复且取消订阅清空恢复timer',async()=>{
 for(const cancel of [false,true]){
  const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const stop=f.detector.subscribeGameStatus(()=>{});f.hold=true;
  f.signal('ipc:game.target.changed',{generation:11});await flushDetection();f.fail(f.messages.at(-1).id,'inert outage');await flushDetection();const start=f.messages.length;
  if(cancel){stop();assert.equal(f.timers.size,0,'last unsubscribe must cancel the owned recovery timer immediately');await f.advance(30000);assert.equal(f.messages.length,start);block();assert.equal(f.timers.size,0);continue;}
  for(const delay of [500,1500,4000]){await f.advance(delay-1);assert.equal(f.messages.length,start+[500,1500,4000].indexOf(delay));await f.advance(1);assert.equal(f.messages.length,start+[500,1500,4000].indexOf(delay)+1);f.fail(f.messages.at(-1).id,'inert outage');await flushDetection();}
  assert.equal(f.messages.length,start+3);await f.advance(60000);assert.equal(f.messages.length,start+3,'exhausted recovery must not become a second periodic poller');assert(!f.detector.isGameStatusCurrent());
  f.native=versionedGame(NEXT_GAME,11);f.hold=false;f.signal('ipc:power.resumed');await f.advance(4000);assert.equal(f.detector.getCurrentGame()?.pid,NEXT_GAME.pid);assert(f.detector.isGameStatusCurrent());stop();block();assert.equal(f.timers.size,0);
 }
});


await check('自愈：同目标失败与恢复只广播准入变化，不重复分发游戏配置',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const targets=[],ready=[];
 const stop=f.detector.subscribeGameStatus(g=>targets.push(g));const stopReady=f.detector.subscribeGameTargetReadiness(value=>ready.push(value));
 const stopBroken=f.detector.subscribeGameTargetReadiness(()=>{throw Error('inert readiness listener');});f.hold=true;const read=f.detector.refreshGameStatusStrict(),rejected=assert.rejects(read);await flushDetection();f.fail(f.messages.at(-1).id,'inert outage');await rejected;
 assert.deepEqual(ready,[true,false]);assert.equal(targets.length,1);f.hold=false;f.native=versionedGame(TMNT,11,10);await f.advance(500);assert.deepEqual(ready,[true,false,true]);assert.equal(targets.length,1);
 const before=f.messages.length;for(let i=0;i<1000;i++){f.signal('ipc:game.target.changed',{generation:11});await flushDetection();}assert.equal(f.messages.length,before);assert.equal(f.timers.size,0,'duplicate committed notifications must not leave a trailing read');await f.advance(1000);assert.equal(f.messages.length,before);assert.deepEqual(ready,[true,false,true]);assert.equal(targets.length,1);
 stopReady();stopBroken();stop();block();assert.equal(f.timers.size,0);
});
await check('风暴：故障期间1000次通知不能绕过退避，手动校验仍可立即恢复',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const stop=f.detector.subscribeGameStatus(()=>{});f.hold=true;
 f.signal('ipc:game.target.changed',{generation:11});await flushDetection();f.fail(f.messages.at(-1).id,'inert outage');await flushDetection();const before=f.messages.length;
 for(let i=0;i<1000;i++){f.signal('ipc:game.target.changed',{generation:11});await flushDetection();}assert.equal(f.messages.length,before);await f.advance(499);assert.equal(f.messages.length,before);
 f.hold=false;f.native=versionedGame(NEXT_GAME,11);const next=await f.detector.refreshGameStatusStrict();assert.equal(next.pid,NEXT_GAME.pid);assert(f.detector.isGameStatusCurrent());await f.advance(1000);assert.equal(f.messages.length,before+1);stop();block();assert.equal(f.timers.size,0);
});
await check('自愈：显式选择在途即撤销旧准入，旧请求失败不扣新恢复预算',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const stop=f.detector.subscribeGameStatus(()=>{}),ready=[];const stopReady=f.detector.subscribeGameTargetReadiness(v=>ready.push(v));f.hold=true;
 const old=f.detector.refreshGameStatusStrict(),oldRejected=assert.rejects(old);await flushDetection();const oldMessage=f.messages.at(-1);const choose=f.detector.refreshGameStatusStrict(NEXT_GAME.pid);assert(!f.detector.isGameStatusCurrent());await flushDetection();
 f.reply(f.messages.at(-1).id,versionedGame(NEXT_GAME,11));await choose;f.fail(oldMessage.id,'inert superseded failure');await oldRejected;assert(f.detector.isGameStatusCurrent());assert.deepEqual(ready,[true,false,true]);const before=f.messages.length;await f.advance(10000);assert.equal(f.messages.length,before);stopReady();stop();block();assert.equal(f.timers.size,0);
});


await check('风暴：稳定目标十秒只用原1250ms发现兜底，心跳不重发配置或准入通知',async()=>{
 const f=detectionFixture();f.native=versionedGame();const targets=[],ready=[],stop=f.detector.subscribeGameStatus(g=>targets.push(g)),stopReady=f.detector.subscribeGameTargetReadiness(v=>ready.push(v));await flushDetection();assert.equal(f.messages.length,1);
 f.native=versionedGame(TMNT,100,10);await f.advance(10000);assert.equal(f.messages.length,9);assert.equal(targets.filter(Boolean).length,1);assert.deepEqual(ready,[false,true]);assert.equal(f.detector.getCurrentGame()?.generation,100);assert.equal(f.schedules.length,1);stopReady();stop();assert.equal(f.timers.size,0);
});
await check('风暴：在途旧选择遇1000次新选择，仅最终选择发出且迟到旧回包收敛到新目标',async()=>{
 const f=detectionFixture();f.hold=true;const old=f.detector.refreshGameStatusStrict(TMNT.pid);await flushDetection();const oldMessage=f.messages[0];const readers=[];
 for(let i=0;i<1000;i++)readers.push(f.detector.refreshGameStatusStrict(i%2?NEXT_GAME.pid:TMNT.pid));await flushDetection();assert.equal(f.messages.length,2);assert.equal(f.messages.at(-1).args.pid,NEXT_GAME.pid);
 f.reply(f.messages.at(-1).id,versionedGame(NEXT_GAME,11));await flushDetection();f.reply(oldMessage.id,versionedGame(TMNT,10));const latest=await Promise.all(readers);assert(latest.every(g=>g?.pid===NEXT_GAME.pid));assert.equal((await old)?.pid,NEXT_GAME.pid);assert.equal(f.detector.getCurrentGame()?.pid,NEXT_GAME.pid);assert(f.detector.isGameStatusCurrent());assert.equal(f.timers.size,0);
});
await check('自愈：普通读取遇旧壳通知会补读新目标，恢复timer在卸载后不复活',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.native={...TMNT};await f.detector.refreshGameStatusStrict();const stop=f.detector.subscribeGameStatus(()=>{});f.hold=true;const read=f.detector.refreshGameStatusStrict();await flushDetection();const stale=f.messages.at(-1);
 f.signal('ipc:game.target.changed');f.reply(stale.id,{...TMNT});await flushDetection();assert.equal(f.messages.length,3);f.reply(f.messages.at(-1).id,{...NEXT_GAME});assert.equal((await read)?.pid,NEXT_GAME.pid);
 f.signal('ipc:game.target.changed');await f.advance(100);const pending=f.messages.at(-1);stop();f.fail(pending.id,'inert late failure after last unsubscribe');await flushDetection();const before=f.messages.length;await f.advance(60000);assert.equal(f.messages.length,before);block();assert.equal(f.timers.size,0);
});


await check('风暴：六个固定种子的切换/断线/坏回包/旧通知混合后均收敛且不残留恢复任务',async()=>{
 for(const seed of [1,7,42,258,2026,0x5eed]){
  let rng=seed,generation=10,identity=10;const next=()=>{rng=(Math.imul(rng,1664525)+1013904223)>>>0;return rng;};
  const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const stop=f.detector.subscribeGameStatus(()=>{});
  for(let i=0;i<200;i++){
   const action=next()%6;
   if(action===0||action===3){generation++;identity++;f.native=versionedGame(next()%2?TMNT:NEXT_GAME,generation,identity);if(action===0)f.signal('ipc:game.target.changed',{generation});else assert.equal((await f.detector.refreshGameStatusStrict(f.native.pid))?.pid,f.native.pid);}
   else if(action===1)f.signal('ipc:game.target.changed',{generation:Math.max(1,generation-1)});
   else if(action===2||action===5){f.hold=true;const read=f.detector.refreshGameStatusStrict(),rejected=assert.rejects(read);await flushDetection();const request=f.messages.at(-1);if(action===2)f.fail(request.id,'inert seeded outage');else f.reply(request.id,{...f.native,processCreated:'invalid-birth'});await rejected;f.hold=false;assert(!f.detector.isGameStatusCurrent());}
   else await f.advance(next()%151);
   await flushDetection();
   if(f.detector.isGameStatusCurrent()){const actual=f.detector.getCurrentGame();assert.equal(actual?.pid,f.native.pid,'seed '+seed+' step '+i);assert.equal(actual?.identityGeneration,f.native.identityGeneration);}
  }
  f.signal('ipc:power.resumed');await f.advance(4500);assert(f.detector.isGameStatusCurrent(),'seed '+seed+' must recover');assert.equal(f.detector.getCurrentGame()?.pid,f.native.pid);assert.equal(f.detector.getCurrentGame()?.identityGeneration,f.native.identityGeneration);assert.equal(f.schedules.filter(x=>x.name==='game-status').length,1);assert(f.messages.length<=400,'mixed failures must keep a bounded read budget');stop();block();assert.equal(f.timers.size,0);
 }
});


await check('自愈：WebView传输恢复事件在空闲或请求在途均撤销准入，旧回执不能污染恢复目标',async()=>{
 for(const pending of [false,true]){
  const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const stop=f.detector.subscribeGameStatus(()=>{});let rejected,old;
  if(pending){f.hold=true;const read=f.detector.refreshGameStatusStrict();rejected=assert.rejects(read,/inert bridge restart/);await flushDetection();old=f.messages.at(-1);}
  f.emitNative('webview.recovering',{reason:'inert bridge restart'});if(rejected)await rejected;await flushDetection();assert(!f.detector.isGameStatusCurrent(),'known bridge recovery must immediately revoke cached admission');
  const before=f.messages.length;f.native=versionedGame(NEXT_GAME,11);f.hold=false;await f.advance(500);assert.equal(f.messages.length,before+1);assert.equal(f.detector.getCurrentGame()?.pid,NEXT_GAME.pid);assert(f.detector.isGameStatusCurrent());if(old){f.reply(old.id,versionedGame());await flushDetection();assert.equal(f.detector.getCurrentGame()?.pid,NEXT_GAME.pid);}stop();block();assert.equal(f.timers.size,0);
 }
});


await check('自愈：最后订阅撤销时在途观察至多八秒释放且不能重建后台恢复',async()=>{
 for(const [replyKind,foreground] of [['none',false],['stale',false],['fresh',false],['stale',true]]){
  const f=detectionFixture(),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const stop=f.detector.subscribeGameStatus(()=>{});f.hold=true;
  f.signal('ipc:game.target.changed',{generation:11});await flushDetection();const count=f.messages.length,pending=f.messages.at(-1);
  const retained=foreground?f.detector.refreshGameStatusStrict().then(game=>({game}),error=>({error})):null;stop();
  if(replyKind!=='none'){
   f.reply(pending.id,replyKind==='fresh'?versionedGame(NEXT_GAME,11):versionedGame());await flushDetection();
   if(foreground){assert.equal(f.messages.length,count+1,'a foreground action retains its shared observation after UI unsubscribe');f.reply(f.messages.at(-1).id,versionedGame(NEXT_GAME,11));const result=await retained;assert(!result.error,'a foreground reader must not be cancelled with the background observer');assert.equal(result.game?.pid,NEXT_GAME.pid);}
   else{assert.equal(f.messages.length,count,'an unobserved background request must not start another stale-snapshot read');assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid,'a background return after stop must not publish a new target');}
  }
  const expected=count+(foreground?1:0);await f.advance(8000);assert.equal(f.timers.size,0);assert.equal(f.messages.length,expected);
  f.reply(pending.id,versionedGame(NEXT_GAME,11));f.signal('ipc:power.resumed');await f.advance(10000);assert.equal(f.timers.size,0);assert.equal(f.messages.length,expected);assert.equal(f.detector.getCurrentGame()?.pid,foreground?NEXT_GAME.pid:TMNT.pid);block();
 }
});const nativeWireFlag=process.argv.indexOf('--native-wire-evidence');
const nativeWireEvidence={status:'NOT_RUN',reason:'Explicit --native-wire-evidence was not supplied'};
if(nativeWireFlag>=0){
 const file=process.argv[nativeWireFlag+1];nativeWireEvidence.path=file??null;
 if(!file||!fs.existsSync(file))nativeWireEvidence.reason='Requested native evidence does not exist';
 else await check('实际MSVC原生序列化valid与tombstone经IPC到唯一策略；旧valid晚到不能复活',async()=>{
  const evidence=JSON.parse(fs.readFileSync(file,'utf8')),positive=evidence.variants?.find(x=>x.name==='positive')??evidence.positive;
  assert.equal(evidence.passed,true);assert.equal(positive?.passed,true);assert.equal(positive?.result?.passed,true);
  const samples=positive.result.wireSamples;assert.equal(samples?.length,2);const valid=samples.find(x=>x.valid===true),empty=samples.find(x=>x.valid===false);
  assert(valid&&empty);assert(empty.generation>valid.generation);assert(empty.identityGeneration>valid.identityGeneration);
  const f=detectionFixture(),block=blockDisplayScheduler(f),seen=[];f.native=structuredClone(valid);f.policy.subscribePolicyGameStatus(g=>seen.push(g));await flushDetection();
  for(const target of [f.detector.getCurrentGame(),f.policy.getPolicyGame()])for(const key of ['pid','processCreated','path','generation','identityGeneration'])assert.equal(target[key],valid[key]);
  f.hold=true;const old=f.detector.refreshGameStatusStrict();await flushDetection();const pending=f.messages.at(-1);f.signal('ipc:game.target.changed',{generation:empty.generation});
  f.native=structuredClone(empty);f.hold=false;f.reply(pending.id,structuredClone(valid));assert.equal(await old,null);await flushDetection();
  assert.equal(f.detector.getCurrentGame(),null);assert.equal(f.policy.getPolicyGame(),null);assert(f.detector.isGameStatusCurrent());assert.deepEqual(seen.map(x=>x?.pid??null),[valid.pid,null]);
  f.reply(pending.id,structuredClone(valid));await flushDetection();f.native=structuredClone(valid);assert.equal(await f.detector.refreshGameStatusStrict(),null);assert.equal(f.policy.getPolicyGame(),null);assert.equal(seen.length,2);block();
  nativeWireEvidence.status='PASS';delete nativeWireEvidence.reason;nativeWireEvidence.nativeChecks=positive.result.checks;nativeWireEvidence.nativeReaderCases=positive.result.readerCases;nativeWireEvidence.sampleGenerations=samples.map(x=>[x.generation,x.identityGeneration]);
 });
}
console.log(JSON.stringify({passed:cases.length,cases,nativeWireEvidence},null,2));



