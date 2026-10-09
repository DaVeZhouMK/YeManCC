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
async function check(name, run) { await run(); cases.push(name); }
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
const hash = load('src/bridge/inputConfigHash.ts');
const hc = load('src/bridge/hcInputUtils.ts');
const repo = load('src/bridge/settingsRepository.ts', { './settingsSnapshot': load('src/bridge/settingsSnapshot.ts'), './api': {}, './inputStartupPolicy': load('src/bridge/inputStartupPolicy.ts'), './inputConfigHash': hash, './hcInputUtils': hc, './quickAppDefaults': load('src/bridge/quickAppDefaults.ts') });
const A = { pid: 101, processCreated: '1001', name: 'a.exe', path: 'C:\\Games\\a.exe', ts: 0 };
const B = { pid: 202, processCreated: '2002', name: 'b.exe', path: 'C:\\Games\\b.exe', ts: 0 };
const C = { pid: 303, processCreated: '3003', name: 'c.exe', path: 'C:\\Games\\c.exe', ts: 0 };
const base = () => structuredClone(repo.normalizeSettings({ input: { outputTarget: { persona: 'disabled', buttonMappingEnabled: false, gyroEnabled: false }, gyroMotion: { enabled: false, outputMode: 'disabled', activePreset: 'racing', preset: 'racing', motionMode: 'toggle', motionTrigger: 'RB', gyroMultiplier: 2, presets: { fps: { motionMode: 'suppress', motionTrigger: 'LT', gyroMultiplier: 1.7 }, racing: { motionInput: 'joystick-steering' } } } } }).input);
const profile = (pad = 'follow', gyro = 'follow') => ({ enabled: true, displayName: 'Game', padPersona: pad, gyroOverride: gyro, ac: { cpuPreset: 'turbo' }, dc: { cpuPreset: 'balanced' } });
await check('首次进入立即生效；9.999秒保持，10秒确认退出', () => {
  const s = new hysteresis.GamePolicyHysteresis(); assert(s.observe(A, 0));
  assert(!s.observe(null, 100)); assert(!s.observe(null, 10099)); assert.equal(s.current.pid, 101);
  assert(s.observe(null, 10100)); assert.equal(s.current, null);
});
await check('原游戏在期限内回归，取消退出和A→B→A', () => {
  const s = new hysteresis.GamePolicyHysteresis(); s.observe(A, 0); s.observe(B, 100);
  assert(!s.observe(A, 9999)); assert.equal(s.missingSince, null); assert(!s.observe(A, 20000));
});
await check('候选频繁变化不重置失去原游戏的计时；确认最新目标', () => {
  const s = new hysteresis.GamePolicyHysteresis(); s.observe(A, 0); s.observe(null, 100);
  s.observe(B, 8000); s.observe(C, 10099); assert(s.observe(C, 10100)); assert.equal(s.current.pid, 303);
});
await check('PID复用算新目标；标题和来源变化不算新策略', () => {
  const s = new hysteresis.GamePolicyHysteresis(); s.observe(A, 0);
  assert(!s.observe({ ...A, source: 'whitelist', title: 'new' }, 20)); assert.equal(s.missingSince, null);
  assert(!s.observe({ ...A, processCreated: '9999' }, 100)); assert.equal(s.missingSince, 100);
});
async function flush() { for (let i = 0; i < 35; i++) { await Promise.resolve(); await vue.nextTick(); } }
async function fixture(options = {}) {
  let now = 0, timerSeq = 0, raw = options.raw === undefined ? A : options.raw;
  const timers = new Map(), scheduled = new Map(), rawListeners = new Set(), configListeners = new Set(), scheduleListeners = new Set();
  const calls = { cas: [], performance: [], core: [], clearCore: 0, conflicts: options.conflicts || 0, writeFailures: options.writeFailures || 0 };
  let settings = options.input || base();
  let config = { version: 1, entries: { 'a.exe': profile('steamdeck', 'fps'), 'b.exe': profile('follow', 'off') }, ...options.config };
  let locked = { locked: false, identity: '' }, strictFailure = !!options.strictFailure, resolveBlockedConfig = null, resolveBlockedCas = null, clearRestoreFailures = options.clearRestoreFailures || 0;
  const clock = {
    performance: { now: () => now },
    setTimeout(fn, ms) { const id = ++timerSeq; timers.set(id, { at: now + ms, fn }); return id; },
    clearTimeout(id) { timers.delete(id); },
  };
  const detector = {
    subscribeGameStatus(cb) { rawListeners.add(cb); cb(raw); return () => rawListeners.delete(cb); },
    async refreshGameStatusStrict() { if (strictFailure) throw Error('transport'); return raw; },
  };
  const target = load('src/bridge/gamePolicyTarget.ts', { './gamedetect': detector, './gamePolicyHysteresis': hysteresis }, clock);
  const scheduler = { registerScheduledTask(name, ms, run, opts) { scheduled.set(name, { ms, run, opts }); return () => scheduled.delete(name); } };
  const repository = {
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
    async loadGameCustomConfig() { if (resolveBlockedConfig) await new Promise(resolve => { resolveBlockedConfig.resolve = resolve; }); return structuredClone(config); },
    onGameCustomConfigChanged(cb) { configListeners.add(cb); return () => configListeners.delete(cb); },
    async clearGameInputRestoreSnapshot(expected) { if (clearRestoreFailures > 0) { clearRestoreFailures--; throw Error('simulated clear failure'); } if (JSON.stringify(expected) === JSON.stringify(config.inputRestore)) { delete config.inputRestore; for (const cb of configListeners) cb(config); } },
    async loadPerformanceSchedule() { return { configured: true, enabled: true, active: { ac: 'balanced', dc: 'balanced' }, profiles: { ac: {}, dc: {} } }; },
    resolveGameCustomProfiles(entry) { return { ac: entry.ac, dc: entry.dc }; },
    async restorePerformanceScheduleIfConfigured() { calls.performance.push(target.getPolicyGame()?.pid || null); return 'auto'; },
    onPerformanceScheduleChanged(cb) { scheduleListeners.add(cb); return () => scheduleListeners.delete(cb); },
  };
  const input = load('src/bridge/gameInputOverride.ts', {
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
  async function emit(game) { raw = game; for (const cb of rawListeners) cb(game); await flush(); }
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
    async retry() { for (const task of scheduled.values()) await task.run(); await flush(); },
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
await check('整个策略共用10秒目标；短暂识别丢失没有CPU/手柄/核心重放', async () => {
  const f = await fixture(); const before = JSON.stringify(f.calls); const overlay = JSON.stringify(f.settings.gameOverride);
  await f.emit(null); await f.tick(9999); assert.equal(JSON.stringify(f.calls), before); assert.equal(JSON.stringify(f.settings.gameOverride), overlay);
  await f.emit(A); await f.tick(20000); assert.equal(JSON.stringify(f.calls), before); assert(f.locked.locked); f.stop();
});
await check('持续退出10秒统一恢复普通策略、清除overlay和页面锁', async () => {
  const f = await fixture(); await f.emit(null); await f.tick(10000);
  assert.equal(f.target.getPolicyGame(), null); assert.equal(f.settings.gameOverride, null); assert(!f.locked.locked);
  assert.deepEqual(f.calls.performance, [101, null]); assert.deepEqual(f.settings.outputTarget, base().outputTarget);
  assert.deepEqual(f.settings.gyroMotion, base().gyroMotion); f.stop();
});
await check('A→B延迟确认，B的follow从本机基线合成而不是继承A', async () => {
  const f = await fixture(); await f.emit(B); await f.tick(9999); assert.equal(f.settings.gameOverride.outputTarget.persona, 'steamdeck');
  await f.tick(1); assert.equal(f.settings.gameOverride.outputTarget.persona, 'disabled'); assert.equal(f.settings.gameOverride.gyroMotion.activePreset, 'racing');
  assert.deepEqual(f.calls.performance, [101, 202]); f.stop();
});
await check('截止时IPC失败保留原策略，成功新鲜确认后才恢复', async () => {
  const f = await fixture(); await f.emit(null); f.strictFailure = true; await f.tick(10000);
  assert.equal(f.target.getPolicyGame().pid, 101); assert(f.locked.locked);
  f.strictFailure = false; await f.tick(1250); assert.equal(f.target.getPolicyGame(), null); assert(!f.locked.locked); f.stop();
});
await check('首次IPC失败且后续仍无游戏时也能初始化并清理旧会话overlay', async () => {
  const initial = base(); initial.gameOverride = {identity:'old',sessionId:'old-owner',outputTarget:{persona:'elite'},gyroMotion:{enabled:true}};
  const f = await fixture({raw:null,strictFailure:true,input:initial});
  assert(!f.target.isPolicyGameInitialized()); assert.equal(f.calls.cas.length,0);
  f.strictFailure=false; await f.tick(1250);
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
  await f.retry(); assert(f.settings.gameOverride); await f.emit(null); f.failWrites(3); await f.tick(10000);
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
await check('大小核/超线程也保留10秒，回归不反复恢复', async () => {
  const f = await fixture({ config: { entries: { 'a.exe': { ...profile('steamdeck','fps'), corePolicyEnabled: true, corePolicyMode: 'only-big', hyperThreadPolicyEnabled: true, hyperThreadPolicy: 'off' } } } });
  assert.deepEqual(f.calls.core, [[101,'only-big','off']]); const clears = f.calls.clearCore;
  await f.emit(B); await f.tick(9999); assert.equal(f.calls.clearCore, clears); await f.emit(A); await f.tick(10000); assert.equal(f.calls.clearCore, clears); assert.equal(f.calls.core.length, 1); f.stop();
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
  const durable = load('src/bridge/settingsRepository.ts', { './settingsSnapshot': load('src/bridge/settingsSnapshot.ts'), './inputStartupPolicy': load('src/bridge/inputStartupPolicy.ts'), './inputConfigHash': hash, './hcInputUtils': hc, './quickAppDefaults': load('src/bridge/quickAppDefaults.ts'),
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
await check('真实顶部保存函数只编辑手柄/陀螺仪时不重复应用核心策略', async () => {
  const source = fs.readFileSync(path.join(root,'src/components/GameCustomProfilePanel.vue'),'utf8');
  const begin = source.indexOf('async function saveGameCustomPatch('), end = source.indexOf('\nfunction selectCorePolicyMode',begin);
  assert(begin >= 0 && end > begin);
  let core = 0, saves = 0; const identity = { value:'already-applied' };
  const config = {value:{entries:{'a.exe':profile('steamdeck','fps')}}};
  const globals = { schedule:{value:{}}, busy:{value:false},props:{game:A},key:{value:'a.exe'},loadRevision:0,
    tryAcquireQuickAction:()=>()=>{},entry:{value:config.value.entries['a.exe']},
    makeEntry:current=>({...current}),selectedModes:{value:{}},config,syncCorePolicyFromEntry(){},
    saveGameCustomConfig:async next=>{saves++;},lastCorePolicyIdentity:identity,
    applyCurrentCorePolicy:async()=>{core++;},corePolicyApplied:{value:true},announce(){},emit(){} };
  const fn = evaluate('export '+source.slice(begin,end),{},globals).saveGameCustomPatch;
  await fn({padPersona:'elite'},'pad'); await fn({gyroOverride:'off'},'gyro');
  assert.equal(saves,2); assert.equal(core,0); assert.equal(identity.value,'already-applied');
  await fn({corePolicyMode:'only-big'},'core'); assert.equal(core,1);
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
    '@/bridge/settingsRepository': { loadSettings:async()=>({input:structuredClone(settings)}),compareAndSwapInputSettings:async(revision,patch)=>{ writes++;const result=repo.evaluateInputCas(settings,revision,patch);if(result.ok)settings=result.value;return result;} },
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
  const perf=fs.readFileSync(path.join(root,'src/bridge/performanceSchedule.ts'),'utf8');assert(perf.includes('if (expectedTarget.policyTarget) return isPolicyTargetCurrent(expectedTarget)'));
});
console.log(JSON.stringify({passed:cases.length,cases},null,2));



