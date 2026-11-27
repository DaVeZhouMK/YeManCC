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
const originalInputOwners=process.argv.includes('--input-owner-originals');
const originalInputOwnerNames=["后台覆盖确认后注销重试；两分钟空闲不再读输入或专属配置","权威输入提交事件刷新follow覆盖；自身CAS通知合并并回到零重试","未解决的覆盖仍有后台重试；归还后晚到通知不能重新登记","专属覆盖只写overlay；页面本机设置和预设快照逐字节不变","IPC失败不能制造退出；只有成功权威空结果才恢复","首次IPC失败且后续仍无游戏时也能初始化并清理旧会话overlay","CAS冲突可重试；输入字段接管期间原子拒绝本机写入","overlay写入/清除失败保留页面锁与本机值，常驻重试成功后才解锁","本机persona不支持陀螺仪时，专属不能错误开启虚拟输出","旧版inputRestore先修复本机快照，再进入独立overlay","停止订阅后，在飞旧异步任务不再落盘","overlay原子替换并参与配置哈希；退出不需要写回本机","queued CAS在磁盘读取后复核取消条件，旧任务不落盘","关闭订阅时已进入CAS队列的overlay提交被取消","旧快照清理失败重试不再覆写已修复后的本机新编辑","已禁用条目、同值覆盖和无效会话的输入接管边界","同会话native开机基线不被旧游戏恢复快照覆盖"];
const inputOwnerOnly = process.argv.includes('--input-owner-only')||originalInputOwners;
const detectionOnly = process.argv.includes('--detection-only');
const inputRuntimeOnly = process.argv.includes('--input-runtime-only');
const uiOnly = process.argv.includes('--ui-only');
assert([detectionOnly, inputRuntimeOnly, uiOnly, inputOwnerOnly].filter(Boolean).length <= 1, 'Select exactly one bounded scope');
const scope = inputOwnerOnly ? 'input-owner-only' : uiOnly ? 'ui-only' : inputRuntimeOnly ? 'input-runtime-only' : detectionOnly ? 'detection-only' : 'default';
const fromIndex=process.argv.indexOf('--case-from'),fromCase=fromIndex<0?null:process.argv[fromIndex+1];
assert(fromIndex<0||inputRuntimeOnly&&typeof fromCase==='string'&&fromCase.length>0,'--case-from requires an exact input-runtime case name');
const exactIndex=process.argv.indexOf('--case'),exactCase=exactIndex<0?null:process.argv[exactIndex+1];
assert(exactIndex<0||(detectionOnly||inputOwnerOnly)&&typeof exactCase==='string'&&exactCase.length>0&&!exactCase.startsWith('--')&&fromIndex<0&&process.argv.lastIndexOf('--case')===exactIndex,'--case requires one exact detection-only case name and cannot combine with --case-from');
assert(!originalInputOwners||exactCase===null,'Original input group cannot combine with an exact case');
assert(!inputOwnerOnly||exactCase!==null||originalInputOwners,'Input owner scope requires exact case or original group');
let exactMatches=0;
let fromReached=!fromCase;
const notRun=[];
const uiEvidence = [];
const scopeDetails = uiOnly ? { uiFixtures:uiEvidence, scope, completeRegression:false, notExecutedDomains:['input-runtime', 'detection', 'native-wire'] } : {};
// Selection uses the current original section, never a case-name match or an assertion bypass.
let currentSection = 'input-runtime';
const cases = [];
const inputOwnerEvidence=[];
async function check(name, run) {
  if(originalInputOwners&&!originalInputOwnerNames.includes(name)){notRun.push(name);return;}
  if(exactCase!==null){if(name!==exactCase){notRun.push(name);return;}assert.equal(++exactMatches,1,'Duplicate exact case name');}
  if(!fromReached){if(name===fromCase)fromReached=true;else{notRun.push(name);return;}}
  if (uiOnly && currentSection !== 'ui') return;
  try { await run(); cases.push(name); }
  catch(error) { console.error(JSON.stringify({...scopeDetails,failedCase:name,passedBeforeFailure:cases.length,passedCases:cases,error:String(error)},null,2));throw error; }
}
function evaluate(source, imports = {}, globals = {}) {
  const module = { exports: {} };
  const transformed=transformSync(source, {loader:'ts',format:'cjs'}).code;
  if(process.env.YMCC_C125_INPUT_DIR){const directory=process.env.YMCC_C125_INPUT_DIR;fs.mkdirSync(directory,{recursive:true});const stem=path.join(directory,String(fs.readdirSync(directory).length/2).padStart(4,'0'));fs.writeFileSync(stem+'.input.ts',source,{flag:'wx'});fs.writeFileSync(stem+'.output.cjs',transformed,{flag:'wx'});}
  vm.runInNewContext(transformed, {
    module, exports: module.exports, console, structuredClone, performance, TextEncoder, setTimeout, clearTimeout, queueMicrotask, AbortController, AbortSignal,
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
const nativeWireEvidence={status:'NOT_RUN',reason:uiOnly ? '--ui-only excludes native-wire, including an explicitly supplied evidence path' : 'Explicit --native-wire-evidence was not supplied'};
if (!detectionOnly) {
const hash = load('src/bridge/inputConfigHash.ts');
const hc = load('src/bridge/hcInputUtils.ts');
const personas = load('src/bridge/controllerPersona.ts');
const baseline = load('src/bridge/settingsBaseline.ts', { '../../PowerControl/settings-baseline.json': JSON.parse(fs.readFileSync(path.join(root,'PowerControl/settings-baseline.json'),'utf8')) });
const shortcuts = load('src/bridge/bootModeSharedShortcuts.ts', { './settingsSnapshot': load('src/bridge/settingsSnapshot.ts') });
const repo = load('src/bridge/settingsRepository.ts', { './controllerPersona': personas, './bootModeGlobalPreferences': load('src/bridge/bootModeGlobalPreferences.ts',{'./inputConfigHash':hash,'./settingsBaseline':baseline}), './bootModeSharedShortcuts': shortcuts, './settingsBaseline': baseline, './settingsSnapshot': load('src/bridge/settingsSnapshot.ts'), './api': {}, './inputStartupPolicy': load('src/bridge/inputStartupPolicy.ts', { './controllerPersona': personas }), './inputConfigHash': hash, './hcInputUtils': hc, './quickAppDefaults': load('src/bridge/quickAppDefaults.ts') });
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
  const timers = new Map(), scheduled = new Map(), rawListeners = new Set(), configListeners = new Set(), scheduleListeners = new Set(), inputListeners = new Set(), readinessListeners = new Set(), performanceListeners = new Set();
  const reads = { input: 0, config: 0 };
  let quickBusy = !!options.quickBusy, powerPhase = options.powerPhase || 'ready', coreProbeFailures = options.coreProbeFailures || 0;
  const calls = { cas: [], performance: [], core: [], clearCore: 0, conflicts: options.conflicts || 0, writeFailures: options.writeFailures || 0 };
  if (options.wakeTdpReplay) calls.tdp = [];
  let settings = options.input || base();
  let config = { version: 1, entries: { 'a.exe': profile('steamdeck', 'fps'), 'b.exe': profile('follow', 'off') }, ...options.config };
  let rawCurrent = null, rawInitialized = false;
  let locked = { locked: false, identity: '' }, strictFailure = !!options.strictFailure, resolveBlockedConfig = null, resolveBlockedCas = null, heldConfig = null, heldCas = null, clearRestoreFailures = options.clearRestoreFailures || 0;
  let settingsGate=null,heldSettings=null,sessionGate=null,heldSession=null;let sessionCalls=0;const casAdmissions=[];
  if(options.holdSession){let release;const promise=new Promise(resolve=>release=resolve);heldSession=sessionGate={promise,release,value:'retired-session'};}
  const clock = {
    performance: { now: () => now },
    setTimeout(fn, ms) { const id = ++timerSeq; timers.set(id, { at: now + ms, fn }); return id; },
    clearTimeout(id) { timers.delete(id); },
  };
  const detector = {
    getCurrentGame() { return rawCurrent ? { ...rawCurrent } : null; },
    isGameStatusInitialized() { return rawInitialized; },
    isGameStatusPowerCurrent() { return true; },
    isGameStatusCurrent() { return rawInitialized && !strictFailure; },
    subscribeGameTargetReadiness(cb) { readinessListeners.add(cb); cb(detector.isGameStatusCurrent()); return () => readinessListeners.delete(cb); },
    isCurrentGameTarget(target) { return !!rawCurrent && rawCurrent.pid===target.pid && rawCurrent.processCreated===target.processCreated; },
    subscribeGameStatus(cb) { rawListeners.add(cb); cb(detector.getCurrentGame()); return () => rawListeners.delete(cb); },
    async refreshGameStatusStrict() {
      if (strictFailure) throw Error('transport');
      rawCurrent=raw ? { ...raw } : null; rawInitialized=true;
      for(const cb of readinessListeners) cb(detector.isGameStatusCurrent());
      for(const cb of rawListeners) cb(detector.getCurrentGame());
      return detector.getCurrentGame();
    },
  };
  const target = load('src/bridge/gamePolicyTarget.ts', { './gamedetect': detector, './gamePolicyHysteresis': hysteresis }, clock);
  const scheduler = { registerScheduledTask(name, ms, run, opts) { scheduled.set(name, { ms, run, opts }); return () => scheduled.delete(name); } };
  const repository = {
    mergeSettings: repo.mergeSettings,
    onInputSettingsChanged(cb) { inputListeners.add(cb); return () => inputListeners.delete(cb); },
    async loadSettings() { reads.input++;if(settingsGate){const gate=settingsGate;settingsGate=null;await gate.promise;}return { input: structuredClone(settings) }; },
    async compareAndSwapInputSettings(revision, patch, isCurrent, options) {
      casAdmissions.push({revision,patch:structuredClone(patch),current:isCurrent,options:structuredClone(options)});
      if (resolveBlockedCas) {const gate=resolveBlockedCas;resolveBlockedCas=null;await gate.promise;}
      if (isCurrent && !isCurrent()) return { ok: false, reason: 'cancelled', value: structuredClone(settings) };
      if (calls.writeFailures > 0) { calls.writeFailures--; return repo.evaluateInputCasWrite(settings, revision, patch, false); }
      if (calls.conflicts > 0) { calls.conflicts--; settings.revision++; return { ok: false, reason: 'revision-conflict', value: structuredClone(settings) }; }
      const result = repo.evaluateInputCas(settings, revision, patch);
      if (result.ok) { settings = result.value; calls.cas.push(structuredClone(patch)); for (const cb of inputListeners) cb(); }
      return result;
    },
  };
  const perf = {
    isPerformanceScheduleBusy: () => false,
    async loadGameCustomConfig() { reads.config++; if (resolveBlockedConfig) {const gate=resolveBlockedConfig;resolveBlockedConfig=null;await gate.promise;} return structuredClone(config); },
    onGameCustomConfigChanged(cb) { configListeners.add(cb); return () => configListeners.delete(cb); },
    async clearGameInputRestoreSnapshot(expected) { if (clearRestoreFailures > 0) { clearRestoreFailures--; throw Error('simulated clear failure'); } if (JSON.stringify(expected) === JSON.stringify(config.inputRestore)) { delete config.inputRestore; for (const cb of configListeners) cb(config); } },
    async loadPerformanceSchedule() { return { configured: true, enabled: true, active: { ac: 'balanced', dc: 'balanced' }, profiles: { ac: {}, dc: {} } }; },
    resolveGameCustomProfiles(entry) { return { ac: entry.ac, dc: entry.dc }; },
    async restorePerformanceScheduleIfConfigured(_side, options) { calls.performance.push(target.getPolicyGame()?.pid || null); return options?.withReceipt ? { ownership:'auto',scope:'global',applied:true } : 'auto'; },
    onContinuousPerformanceApplied(cb) { performanceListeners.add(cb); return () => performanceListeners.delete(cb); },
    onPerformanceScheduleChanged(cb) { scheduleListeners.add(cb); return () => scheduleListeners.delete(cb); },
  };

  const powerListeners=new Map();let nativePower={phase:'ready',generation:100,known:true,hardwareWritesAllowed:true,inputReady:true,notificationPending:false,publicationRevision:1,observedNotificationSequence:1};
  if(options.wakeTdpReplay)nativePower.suspendSerial=7;
  const powerModule=load('src/bridge/powerLifecycleState.ts',{'./ipc':{invoke:()=>{throw Error('Use original fixture power read');},on:()=>()=>{}}},clock);
  const sharedPower=powerModule.createSharedPowerLifecycleSource({read:async()=>({...nativePower}),listen:(name,listener)=>{if(!powerListeners.has(name))powerListeners.set(name,new Set());powerListeners.get(name).add(listener);return()=>powerListeners.get(name).delete(listener);},clock});
  const stopPowerProbe=sharedPower.onSharedPowerLifecycle(()=>{});
  let actualConfigService=null,configPort=null;
  const configMonitors=new Set();
  if(options.actualConfig){
    const file='Q:\\C129\\PowerControl\\game-custom.json',store=new Map([[file,JSON.stringify(config)]]);const originalConfig=structuredClone(config);
    configPort={store,file,fault:null,writes:[],setFault(kind){this.fault=kind;store.set(file,kind==='corrupt'?'{corrupt':kind==='future'?JSON.stringify({...originalConfig,version:99}):JSON.stringify(originalConfig));},recover(){this.fault=null;const next=structuredClone(originalConfig);next.entries['a.exe'].padPersona='elite';store.set(file,JSON.stringify(next));},get readState(){return actualConfigService.getGameCustomRecoveryState();}};
    const frameModel=load('src/bridge/frameRateModel.ts');
    const serviceRepository={...repository,loadSettings:async()=>({input:structuredClone(settings),performanceSchedule:{configured:false,enabled:false},extensions:{}}),SETTINGS_FILE:'Q:\\C129\\PowerControl\\yeman-settings.json',getSettingsGeneration:()=>1,assertSettingsGeneration:g=>assert.equal(g,1),getSettingsSectionRevision:()=>1,readSettingsSection:async()=>({}),replaceSettingsSection:async()=>{throw Error('Unexpected config migration');},saveSettingsSection:async()=>{throw Error('Unexpected durable user write');},updateSettings:async()=>{throw Error('Unexpected settings write');},rememberOrdinaryPerformancePreferences(){}};
    const coreModel=load('src/bridge/performanceControls.ts');
    actualConfigService=load('src/bridge/performanceSchedule.ts',{
      './powerLifecycleState':{...sharedPower,isSharedPowerLifecycleAdmitted:powerModule.isSharedPowerLifecycleAdmitted},
      './settingsSnapshot':load('src/bridge/settingsSnapshot.ts'),'./controllerPersona':personas,'./performanceControls':coreModel,'./powerSource':{powerSourceMode:vue.ref('ac')},'./gamePolicyTarget':target,'./gamedetect':detector,
      './gameCorePolicy':{},'./api':{powerLifecycle:{get:async()=>({...nativePower})},fs:{exists:async p=>store.has(p),readTextFile:async p=>{if(configPort.fault==='read'&&p===file)throw Error('Owned config read denied after wake');if(configPort.fault==='hold'&&p===file){const raw=store.get(p);return new Promise(resolve=>configPort.releaseRead=()=>resolve(raw));}return store.get(p);},writeTextFileAtomic:async(p,v)=>{configPort.writes.push({path:p,content:v});store.set(p,v);}}},
      './settingsRepository':{...serviceRepository,validateSettingsRead:async()=>{}},'./bootModeGlobalPreferences':load('src/bridge/bootModeGlobalPreferences.ts',{'./inputConfigHash':hash,'./settingsBaseline':baseline}),
      './frameRateLimits':{...frameModel,ensureGlobalFrameRates:async()=>frameModel.frameRatePair({}),loadGlobalFrameRates:async()=>frameModel.frameRatePair({})},
      './yeman':{getTdpHardwareMinimum:()=>2,getTdpHardwareMaximum:()=>150,detectPerformanceTemplate:async()=> 'portable'},'./cpuProfiles':{loadCpuProfiles:async()=>({profiles:{}})},'./autofloat':{},
    },clock);
    Object.assign(perf,{loadGameCustomConfig:actualConfigService.loadGameCustomConfig,onGameCustomConfigChanged:actualConfigService.onGameCustomConfigChanged,clearGameInputRestoreSnapshot:actualConfigService.clearGameInputRestoreSnapshot,
      loadPerformanceSchedule:actualConfigService.loadPerformanceSchedule,continuousControlsFromSchedule:actualConfigService.continuousControlsFromSchedule,continuousControlsForEntry:actualConfigService.continuousControlsForEntry,
      reconcilePerformanceScheduleTdp:async()=>{if(options.wakeTdpReplay)calls.tdp.push(nativePower.suspendSerial);return true;},getInvalidPerformanceTdpTarget:()=>null,reconcileInvalidPerformanceTdp:async()=>false});
  }

  const broadcastPower=async changes=>{nativePower={...nativePower,...changes};for(const cb of [...powerListeners.get('power.lifecycleChanged')||[]])cb({...nativePower});await flush();};
  const input = evaluate(fs.readFileSync(path.join(root,'src/bridge/gameInputOverride.ts'),'utf8')+'\nexport const __inputState=()=>({active,running,pending,dirty,sessionId,epoch});', {
    './powerLifecycleState':{...sharedPower,isSharedPowerLifecycleAdmitted:powerModule.isSharedPowerLifecycleAdmitted},'./powerResume':load('src/bridge/powerResume.ts'),
    './gyroPresetModel':load('src/bridge/gyroPresetModel.ts'),
    '@/bridge/settingsRepository': repository, '@/bridge/gamePolicyTarget': target,
    './gamedetect': detector, '@/bridge/ipc': { invoke: async () => {sessionCalls++;if(sessionGate){const gate=sessionGate;sessionGate=null;await gate.promise;return gate.value;}return 'native-session-1';} }, '@/scheduler': scheduler, '@/bridge/performanceSchedule': perf,
  }, clock);
  input.subscribeGameInputOverrideState(state => { locked = state; });
  const runtime = inputOwnerOnly&&!options.actualConfigResident?{startGamePolicyRuntime(){},stopGamePolicyRuntime(){}}:evaluate(fs.readFileSync(path.join(root,'src/bridge/gamePolicyRuntime.ts'),'utf8')+'\nexport const __configResidentState=()=>({active,running,dirty,tdpPending,appliedPerformance,appliedFrames,appliedCore});', {
    './gamePolicyTarget': target, './gamedetect': detector, './performanceControls': load('src/bridge/performanceControls.ts'), './performanceSchedule': perf, '@/scheduler': scheduler,
    './powerSource': {powerSourceMode:vue.ref('ac')}, './yeman':{detectPowerMode:async()=> 'ac'}, './autofloat':{setFloatRtssLinked(){}},
    './frameRateLimits':{loadGlobalFrameRates:async()=>({ac:{fps:60,lastFps:60,ceiling:60},dc:{fps:30,lastFps:30,ceiling:30}}),onFrameRatesChanged:()=>()=>{},dedicatedFrameRatePair:(_,global)=>global,applyIndependentFrameRates:async()=>true},
    './api': { powerLifecycle: { get: async () => ({ phase: powerPhase, hardwareWritesAllowed: powerPhase === 'ready' }) } },
    ...(options.actualConfigResident?{'./powerLifecycleState':{...sharedPower,isSharedPowerLifecycleAdmitted:powerModule.isSharedPowerLifecycleAdmitted},'./powerResume':load('src/bridge/powerResume.ts'),'./monitorData':{subscribeMonitorSnapshot:cb=>{configMonitors.add(cb);return()=>configMonitors.delete(cb);}},'./monitorSnapshotSignals':load('src/bridge/monitorSnapshotSignals.ts',{'./monitorFrameTime':load('src/bridge/monitorFrameTime.ts'),'./monitorCoreTiles':load('src/bridge/monitorCoreTiles.ts')}),'./autofloat':{setFloatRtssLinked(){},getFloatInfo:()=>({enabled:false})},'./powerSource':{powerSourceMode:vue.ref('ac'),powerSourceBatteryPresent:vue.ref(true)}}:{}),
    './quickActionLock': { isQuickActionBusy: () => quickBusy },
    './gameCorePolicy': {
      detectGameCorePolicy: async () => { if (coreProbeFailures > 0) { coreProbeFailures--; throw Error('probe unavailable'); } return { heterogeneous: true, smtAvailable: true }; },
      applyGameCorePolicy: async (game, mode, hyper) => { calls.core.push([game.pid, mode, hyper]); return { ok: true, applied: true }; },
      clearGameCorePolicy: async () => { calls.clearCore++; return true; },
    },
  }, options.wakeTdpReplay?{...clock,Date:class extends Date{static now(){return now;}}}:clock);
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
    target, input, calls, reads, inputListeners, clock, scheduled, emit, tick, repository, sharedPower,broadcastPower,get power(){return {...nativePower};},restartInput(){input.startGameInputOverrideWatch();},
    casAdmissions,actualConfig:configPort,actualConfigService,residentState:()=>runtime.__configResidentState?.(),get sessionCalls(){return sessionCalls;},
    state:()=>input.__inputState(),async pokeRetry(){for(const task of [...scheduled.values()])void task.run();await flush();},
    async blockSettings(){let release;const promise=new Promise(resolve=>release=resolve);heldSettings=settingsGate={promise,release};},async unblockSettings(){heldSettings?.release();heldSettings=null;settingsGate=null;await flush();},
    holdSession(value='retired-session'){let release;const promise=new Promise(resolve=>release=resolve);heldSession=sessionGate={promise,release,value};},async unblockSession(){heldSession?.release();heldSession=null;sessionGate=null;await flush();},
    async setReady(ready){strictFailure=!ready;for(const cb of [...readinessListeners])cb(ready);await flush();},
    stopInput(){input.stopGameInputOverrideWatch();},
    assertClosed(){assert.equal(inputListeners.size+configListeners.size+readinessListeners.size,0);assert.equal(scheduled.size,0);assert.equal(sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);assert.equal(timers.size,0);},
    async pollRetries() { for (const task of [...scheduled.values()]) await task.run(); await flush(); },
    async nativeInputCommit(patch) { settings = repo.mergeSettings(settings, patch); settings.revision++; for (const cb of inputListeners) cb(); await flush(); },
    get settings() { return settings; }, get config() { return config; }, get locked() { return locked; },
    failWrites(count) { calls.writeFailures=count; },
    set quickBusy(value) { quickBusy=value; },
    set powerPhase(value) { powerPhase=value; },
    set strictFailure(value) { strictFailure = value; },
    async changeEntry(key, entry) { config.entries[key] = entry; for (const cb of configListeners) cb(config); await flush(); },
    async retry() { await detector.refreshGameStatusStrict().catch(()=>{}); for (const task of scheduled.values()) await task.run(); await flush(); },
    async blockCas() { let release;const promise=new Promise(resolve=>release=resolve);heldCas=resolveBlockedCas={promise,release}; },
    async unblockCas() { const blocked = heldCas;heldCas=null;resolveBlockedCas = null; blocked?.release?.(); await flush(); },
    async blockConfig() { let release;const promise=new Promise(resolve=>release=resolve);heldConfig=resolveBlockedConfig={promise,release}; },
    async unblockConfig() { const blocked = heldConfig;heldConfig=null;resolveBlockedConfig = null; blocked?.release?.(); await flush(); },
    stop() { input.stopGameInputOverrideWatch(); runtime.stopGamePolicyRuntime();stopPowerProbe(); },
  };
}
await check('后台覆盖确认后注销重试；两分钟空闲不再读输入或专属配置', async () => {
  for (const raw of [null, A]) {
    const f = await fixture({ raw });
    assert(!f.scheduled.has('game-input-overlay-retry'));
    assert(!f.scheduled.has('game-policy-runtime-retry'));
    const before = { ...f.reads };
    for (let i = 0; i < 60; i++) await f.pollRetries();
    assert.deepEqual(f.reads, before);
    f.stop(); assert.equal(f.inputListeners.size, 0);
  }
});
await check('权威输入提交事件刷新follow覆盖；自身CAS通知合并并回到零重试', async () => {
  const f = await fixture({ config: { entries: { 'a.exe': profile('follow', 'on') } } });
  assert.equal(f.settings.gameOverride.outputTarget.persona, 'disabled');
  await f.nativeInputCommit({
    outputTarget: { persona: 'elite', buttonMappingEnabled: true },
    gyroMotion: { gyroMultiplier: 4 },
  });
  assert.equal(f.settings.gameOverride.outputTarget.persona, 'elite');
  assert.equal(f.settings.gameOverride.gyroMotion.gyroMultiplier, 4);
  assert(!f.scheduled.has('game-input-overlay-retry'));
  const before = { ...f.reads }; await f.pollRetries(); assert.deepEqual(f.reads, before);
  f.stop();
});
await check('未解决的覆盖仍有后台重试；归还后晚到通知不能重新登记', async () => {
  const f = await fixture({ writeFailures: 3 });
  assert(f.scheduled.has('game-input-overlay-retry'));
  const late = [...f.inputListeners];
  await f.pollRetries(); assert(f.settings.gameOverride); assert(!f.scheduled.has('game-input-overlay-retry'));
  f.stop(); const before = { ...f.reads };
  for (const cb of late) cb(); await flush();
  assert.deepEqual(f.reads, before); assert(!f.scheduled.has('game-input-overlay-retry'));
});
await check('策略忙碌与电源未就绪保留后台重试；解除后确认并注销', async () => {
  for (const options of [{quickBusy:true}, {powerPhase:'resuming'}, {coreProbeFailures:1}]) {
    const f = await fixture(options);
    assert(f.scheduled.has('game-policy-runtime-retry'));
    f.quickBusy=false; f.powerPhase='ready'; await f.pollRetries();
    assert(!f.scheduled.has('game-policy-runtime-retry'));
    const before = JSON.stringify(f.calls); const reads = { ...f.reads };
    for (let i=0;i<60;i++) await f.pollRetries();
    assert.equal(JSON.stringify(f.calls),before); assert.deepEqual(f.reads,reads); f.stop();
  }
});
await check('已确认策略遇新专属变化时重新登记；停止后的旧retry不复活', async () => {
  const f = await fixture(); assert(!f.scheduled.has('game-policy-runtime-retry'));
  f.quickBusy=true; await f.changeEntry('a.exe', {...profile('elite','off'), ac:{cpuPreset:'balanced'}});
  assert(f.scheduled.has('game-policy-runtime-retry'));
  const late = [...f.scheduled.values()]; f.stop();
  const before=JSON.stringify(f.calls), reads={...f.reads};
  for (const task of late) await task.run(); await flush();
  assert.equal(JSON.stringify(f.calls),before); assert.deepEqual(f.reads,reads); assert.equal(f.scheduled.size,0);
});
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
  assert(!f.config.inputRestore); assert.deepEqual(f.settings.outputTarget, original.outputTarget); assert.deepEqual(f.settings.gyroMotion, original.gyroMotion); assert(f.locked.locked);
  assert(f.casAdmissions.some(a=>a.patch.legacyGameRestoreId));assert(f.casAdmissions.every(a=>a.options?.inputRuntimePower?.generation===100&&a.options.inputRuntimePower.observedNotificationSequence===1),'legacy restore or overlay dropped public runtime power');f.stop();
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
  const durable = load('src/bridge/settingsRepository.ts', { './controllerPersona': personas, './bootModeGlobalPreferences': load('src/bridge/bootModeGlobalPreferences.ts',{'./inputConfigHash':hash,'./settingsBaseline':baseline}), './bootModeSharedShortcuts': shortcuts, './settingsBaseline': baseline, './settingsSnapshot': load('src/bridge/settingsSnapshot.ts'), './inputStartupPolicy': load('src/bridge/inputStartupPolicy.ts', { './controllerPersona': personas }), './inputConfigHash': hash, './hcInputUtils': hc, './quickAppDefaults': load('src/bridge/quickAppDefaults.ts'),
    './api': { fs: { exists: async () => true, readTextFile: async () => new Promise(resolve => { releaseRead = resolve; }) },
      settingsStore: { write: async () => { writes++; return true; } } } });
  durable.setSettingsDirectory(String.raw`C:\SOFT\YeMan\PowerControl`);
  const document = repo.normalizeSettings({ input: base() }); baseline.stampSettingsBaseline(document);
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

if(inputOwnerOnly)await check('公共睡眠配置：同一读结果隔离显示降级与运行拒绝且有效备份空配置可用',async()=>{
 const f=await fixture({actualConfig:true});try{
  f.stopInput();await flush();const service=f.actualConfigService;f.actualConfig.setFault('read');
  const display=service.loadGameCustomConfig(),options={requireReady:true},runtime=service.loadGameCustomConfig('ensure',options);options.requireReady=false;
  const shown=await display;assert.equal(Object.keys(shown.entries).length,0);await assert.rejects(runtime,/可靠读取/);assert.equal(service.getGameCustomRecoveryState().status,'runtime-fallback');assert.equal(f.actualConfig.writes.length,0);
  f.actualConfig.setFault('corrupt');const good={version:1,entries:{'a.exe':profile('elite','off')},rtss:{}};const main=f.actualConfig.store.get(f.actualConfig.file);f.actualConfig.store.set(f.actualConfig.file+'.bak',JSON.stringify(good));
  const recovered=await service.loadGameCustomConfig('read-only',{requireReady:true});assert.equal(recovered.entries['a.exe'].padPersona,'elite');assert.equal(service.getGameCustomRecoveryState().source,'backup');assert.equal(f.actualConfig.store.get(f.actualConfig.file),main);assert.equal(f.actualConfig.writes.length,0);
  f.actualConfig.fault=null;f.actualConfig.store.set(f.actualConfig.file,JSON.stringify({version:1,entries:{},rtss:{}}));const empty=await service.loadGameCustomConfig('ensure',{requireReady:true});assert.equal(Object.keys(empty.entries).length,0);assert.equal(service.getGameCustomRecoveryState().status,'ready');
  f.actualConfig.fault='hold';f.actualConfig.store.set(f.actualConfig.file,JSON.stringify({version:1,entries:{'a.exe':profile('steamdeck','fps')},rtss:{}}));const delayed=service.loadGameCustomConfig('ensure',{requireReady:true}).then(value=>({value}),error=>({error}));await flush();assert.equal(typeof f.actualConfig.releaseRead,'function');await f.tick(2000);assert((await delayed).error,'original2s read timeout promoted fallback to runtime');f.actualConfig.recover();const fresh=await service.loadGameCustomConfig('ensure',{requireReady:true});assert.equal(fresh.entries['a.exe'].padPersona,'elite');f.actualConfig.releaseRead();await flush();assert.equal(service.getGameCustomConfig().entries['a.exe'].padPersona,'elite','late timed-out read replaced successor config');assert.equal(service.getGameCustomRecoveryState().status,'ready');
  inputOwnerEvidence.push({kind:'actual-service-coalesced-readiness',pass:true,displayFallbackAllowed:true,runtimeFallbackRejected:true,optionSnapshot:true,validBackupAllowed:true,validEmptyAllowed:true,originalReadTimeoutMs:2000,lateReadCannotPublish:true});
 }finally{f.stop();await flush();f.assertClosed();}
});

if(inputOwnerOnly)await check('公共睡眠配置：真实配置读取失败不能清除输入或确认常驻策略完成',async()=>{
 const flag=process.argv.find(s=>s.startsWith('--config-recovery-boundary=')),only=flag?.split('=')[1],kinds=['input-read','input-corrupt','input-future','resident-read','resident-corrupt','resident-future'];assert(!only||only==='resident-all'||kinds.includes(only),'Unknown config recovery boundary');
 for(const kind of only==='resident-all'?kinds.filter(k=>k.startsWith('resident')):only?[only]:kinds){
  const f=await fixture({actualConfig:true,actualConfigResident:kind.startsWith('resident'),config:{entries:{'a.exe':{...profile('steamdeck','fps'),hyperThreadPolicyEnabled:true,hyperThreadPolicy:'off'}}}});
  try{
   await flush();await f.pokeRetry();await flush();assert.equal(f.settings.gameOverride?.outputTarget.persona,'steamdeck','actual config source did not establish initial overlay');
   if(kind.startsWith('resident')){assert(f.residentState().appliedCore&&f.residentState().appliedFrames,'actual resident did not establish healthy policy');assert.equal(f.calls.core.at(-1)?.[2],'off');}
   const prior=JSON.stringify(f.settings.gameOverride),priorCalls=JSON.stringify(f.calls);f.actualConfig.setFault(kind.split('-')[1]);
   await f.broadcastPower({notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});await f.broadcastPower({generation:101,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});await flush();await f.pokeRetry();await flush();
   assert.equal(f.actualConfig.readState.status,'runtime-fallback');assert.equal(JSON.stringify(f.settings.gameOverride),prior,'failed config read cleared the admitted input overlay');
   assert(f.locked.locked);assert(f.state().dirty,'failed config read permanently acknowledged input work');assert(f.scheduled.has('game-input-overlay-retry'),'failed config read removed original retry');
   if(kind.startsWith('resident')){assert.equal(JSON.stringify(f.calls),priorCalls,'failed config read dispatched or acknowledged a resident policy');assert(f.residentState().dirty||f.residentState().tdpPending,'failed config read cleared resident pending work');assert(f.scheduled.has('game-policy-runtime-retry'),'failed config read removed resident original retry');}
   f.actualConfig.recover();await f.pokeRetry();await flush();await f.pokeRetry();await flush();assert.equal(f.actualConfig.readState.status,'ready');assert.equal(f.settings.gameOverride.outputTarget.persona,'elite','original retry did not recover without a new config event');assert(!f.scheduled.has('game-input-overlay-retry'));assert.equal(f.actualConfig.writes.length,0);
   inputOwnerEvidence.push({kind,actualConfigService:true,pass:true,originalRetryMs:2000,finalInput:f.state(),finalResident:f.residentState()});
  }finally{f.stop();await flush();f.assertClosed();}
 }
});

if(inputOwnerOnly)await check('resident TDP repeats bounded checks only for a new committed sleep scope',async()=>{
 const entry=profile('steamdeck','fps');entry.ac.tdpMax=25;entry.dc.tdpMax=15;
 const f=await fixture({actualConfig:true,actualConfigResident:true,wakeTdpReplay:true,config:{entries:{'a.exe':entry}}});
 try{
  await f.pokeRetry();await f.tick(4000);await f.pokeRetry();await f.tick(8000);await f.pokeRetry();
  const settled=f.calls.tdp.slice();
  assert(settled.length>=2&&settled.every(serial=>serial===7),JSON.stringify({settled,resident:f.residentState()}));
  assert.equal(f.residentState().tdpPending,false);
  const same={publicationRevision:2,observedNotificationSequence:2};
  await f.broadcastPower(same);await f.pokeRetry();
  assert.deepEqual(f.calls.tdp,settled,'notification-only updates cannot reopen healthy hardware checks');
  await f.broadcastPower({publicationRevision:3,suspendSerial:8});await f.pokeRetry();
  assert.deepEqual(f.calls.tdp,[...settled,8],'same-generation committed Ready must verify TDP after a new physical sleep');
  await f.broadcastPower({publicationRevision:4});await f.pokeRetry();
  assert.deepEqual(f.calls.tdp,[...settled,8]);
  await f.tick(4000);await f.pokeRetry();await f.tick(8000);await f.pokeRetry();
  assert.deepEqual(f.calls.tdp,[...settled,8,8,8]);assert.equal(f.residentState().tdpPending,false);
  await f.broadcastPower({publicationRevision:5,suspendSerial:9,notificationPending:true,hardwareWritesAllowed:false});await f.pokeRetry();
  assert.deepEqual(f.calls.tdp,[...settled,8,8,8],'a new serial cannot start hardware checks before public admission');
  await f.broadcastPower({publicationRevision:6,notificationPending:false,hardwareWritesAllowed:true});await f.pokeRetry();
  assert.deepEqual(f.calls.tdp,[...settled,8,8,8,9],'commit after a denied snapshot must retain the new serial');
  await f.broadcastPower({publicationRevision:7,suspendSerial:8});await f.pokeRetry();
  assert.deepEqual(f.calls.tdp,[...settled,8,8,8,9],'an old Ready cannot reopen its checks');
  inputOwnerEvidence.push({kind:'resident-tdp-public-suspend-boundary',checksBySuspendSerial:f.calls.tdp,hardwareIo:false});
 }finally{f.stop();await flush();f.assertClosed();}
});

if(inputOwnerOnly)await check('公共睡眠输入覆盖：旧配置读取未归还时同游戏新Ready可接续',async()=>{
 const f=await fixture();await f.blockConfig();const before=f.calls.cas.length;await f.changeEntry('a.exe',profile('elite','off'));const blocked=f.reads.config;
 try{await f.broadcastPower({notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});assert.equal(f.calls.cas.length,before);await f.broadcastPower({generation:101,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});assert(f.reads.config>blocked,'public Ready remained blocked by retired input config read');assert.equal(f.settings.gameOverride.outputTarget.persona,'elite');}
 finally{await f.unblockConfig();await flush();f.stop();}
});

if(inputOwnerOnly)await check('公共睡眠输入覆盖：配置设置CAS和旧finally生命周期边界',async()=>{
 const rows=[];
 const boundaryKinds=['held-settings','held-cas','old-finally','stop-restart','same-target-ready','metadata-sequence','late-session'];
 const boundaries=process.argv.filter(v=>v.startsWith('--input-owner-boundary='));assert(boundaries.length<=1,'Duplicate input owner boundary');const selected=boundaries[0]?.slice('--input-owner-boundary='.length);assert(!boundaries.length||boundaryKinds.includes(selected),'Unknown input owner boundary');
 for(const kind of selected?[selected]:boundaryKinds){
  const f=await fixture(kind==='late-session'?{holdSession:true}:{});let pending=false;const original=f.settings.gameOverride?.outputTarget.persona;
  try{
   if(kind==='late-session'){
    // A second actual module lifetime starts without a cached session.
    assert(f.state().running);const oldCalls=f.sessionCalls;
    await f.broadcastPower({notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});await f.broadcastPower({generation:101,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});assert(f.sessionCalls>oldCalls,'new public session read blocked by retired session');assert.equal(f.state().sessionId,'native-session-1');await f.unblockSession();assert.equal(f.state().sessionId,'native-session-1','late old session overwrote current session');
   }else{
    if(kind==='held-settings')await f.blockSettings();else if(kind==='held-cas')await f.blockCas();else await f.blockConfig();
    await f.changeEntry('a.exe',profile('elite','off'));assert(f.state().running);const before=f.calls.cas.length;
    if(kind==='stop-restart'){f.stopInput();assert.equal(f.state().running,false,'stopped input kept old owner running');f.restartInput();await flush();}
    else if(kind==='same-target-ready'){await f.setReady(false);assert.equal(f.state().running,false);await f.setReady(true);}
    else if(kind==='metadata-sequence'){await f.broadcastPower({publicationRevision:2,observedNotificationSequence:2});}
    else{
     await f.broadcastPower({notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});assert.equal(f.calls.cas.length,before,'public denial changed overlay');assert.equal(f.settings.gameOverride.outputTarget.persona,original);assert(!f.scheduled.has('game-input-overlay-retry'),'held public power retained overlay poll');
     if(kind==='old-finally')await f.blockSettings();await f.broadcastPower({generation:101,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});
    }
    if(kind==='old-finally'){
     assert(f.state().running,'successor did not own held settings');await f.unblockConfig();assert(f.state().running,'old input finally cleared successor owner');const calls=f.reads.input;await f.pokeRetry();assert.equal(f.reads.input,calls,'old finally allowed duplicate input successor');await f.unblockSettings();
    }else if(kind==='held-cas'){
     assert.equal(f.settings.gameOverride.outputTarget.persona,'elite');const currentCas=f.calls.cas.length;assert.equal(f.casAdmissions[1].current(),false,'old CAS current revived after Ready');await f.unblockCas();assert.equal(f.calls.cas.length,currentCas,'retired CAS committed after newer overlay');
    }else{assert.equal(f.settings.gameOverride.outputTarget.persona,'elite','admitted successor did not finish before old read');await f.unblockSettings();await f.unblockConfig();}
   }
   await flush();await f.pollRetries();assert(!f.scheduled.has('game-input-overlay-retry'),'healthy input kept private retry');const reads={...f.reads},writes=f.calls.cas.length;for(let n=0;n<20;n++)await f.pollRetries();assert.deepEqual(f.reads,reads);assert.equal(f.calls.cas.length,writes);rows.push({kind,pass:true,writes,sessionCalls:f.sessionCalls,state:f.state()});
  }finally{await f.unblockConfig();await f.unblockSettings();await f.unblockCas();await f.unblockSession();f.stop();f.assertClosed();}
 }
 inputOwnerEvidence.push({name:'input-public-owner-boundaries',rows});
});
if (!inputRuntimeOnly && !inputOwnerOnly) {
currentSection = 'ui';
// Actual compiled pages with in-memory renderer: no WebView, real hardware, process or disk calls.
function element(type, text = '') { return { type, text, style:{display:''}, props: {}, children: [], parent: null, get textContent() { return this.text + this.children.map(n => n.textContent).join(''); } }; }
const uiBody=element('body');
const renderer = vue.createRenderer({
  querySelector:selector=>{assert.equal(selector,'body');return uiBody;},
  createElement: type => element(type), createText: text => element('#text',text), createComment: text => element('#comment',text),
  setText: (node,text) => { node.text = text; }, setElementText: (node,text) => { node.text = text; node.children = []; },
  parentNode: node => node.parent, nextSibling: node => node.parent?.children[node.parent.children.indexOf(node)+1] || null,
  insert(node,parent,anchor = null) { if(node.parent)node.parent.children.splice(node.parent.children.indexOf(node),1); node.parent = parent; const i=anchor?parent.children.indexOf(anchor):-1; if(i<0)parent.children.push(node);else parent.children.splice(i,0,node); },
  remove(node) { if(node.parent)node.parent.children.splice(node.parent.children.indexOf(node),1); node.parent = null; },
  patchProp(node,key,prev,next) { node.props[key] = next; },
});
const descendants = node => [node, ...node.children.flatMap(descendants)];
async function pageFixture(file, initial = base()) {
  // Stateful memory ports replace persistence/native ownership only. Editor,
  // snapshot unwrapping, CAS evaluation, overlay and gyro save stay real.
  let settings = structuredClone(initial), game = null, config = { entries:{}, rtss:{} };
  const settingsGeneration = 1, inputListeners = new Set(), gameListeners = new Set(), configListeners = new Set();
  const globalCas = [], dedicatedWrites = [], refusals = [], ipcCalls = [], traces = [];
  let joy = 0, now = 0, seq = 0, casGate = null, pendingCas = 0;
  const timers = new Map(), ipcListeners = new Map();
  const window = { addEventListener(){}, removeEventListener(){}, dispatchEvent(){},
    setTimeout(fn,ms){const id=++seq;timers.set(id,{fn,at:now+Math.max(0,Number(ms)||0)});return id;},
    clearTimeout(id){timers.delete(id);} };
  const document = { addEventListener(){}, removeEventListener(){}, querySelector:()=>null, activeElement:null };
  const clock = {window,document,performance:{now:()=>now},setTimeout:window.setTimeout,clearTimeout:window.clearTimeout,
    requestAnimationFrame:fn=>window.setTimeout(fn,16),cancelAnimationFrame:window.clearTimeout};
  const subscribe = (listeners,cb) => { listeners.add(cb); return ()=>listeners.delete(cb); };
  const snapshots = load('src/bridge/settingsSnapshot.ts');
  const copy = snapshots.snapshotSettingsData;
  const settingsPort = {
    loadSettings:async()=>({input:copy(settings)}),
    getSettingsGeneration:()=>settingsGeneration,
    assertSettingsGeneration(value){assert.equal(value,settingsGeneration,'stale settings generation');},
    mergeSettings:repo.mergeSettings, VirtualStartProtectionActive:repo.VirtualStartProtectionActive,
    onInputSettingsChanged:cb=>subscribe(inputListeners,cb),
    async compareAndSwapInputSettings(revision,patch,isCurrent=()=>true){
      const attempt={revision,patch:copy(patch),status:'PENDING'};globalCas.push(attempt);
      if(casGate){const gate=casGate;casGate=null;pendingCas++;await gate.promise;pendingCas--;}
      if(!isCurrent()){attempt.status='CANCELLED';refusals.push('global-owner-cancelled');return {ok:false,reason:'cancelled',revision:settings.revision,value:copy(settings)};}
      const result=repo.evaluateInputCas(settings,revision,patch);
      attempt.status=result.ok?'COMMITTED':result.reason;
      if(result.ok){settings=copy(result.value);for(const cb of [...inputListeners])cb();}
      return result;
    },
  };
  const policyPort = {
    detectPolicyGame:async()=>copy(game),getPolicyGame:()=>copy(game),gamePolicyKey:hysteresis.gamePolicyKey,
    isPolicyGameInitialized:()=>true,subscribePolicyGameStatus:cb=>subscribe(gameListeners,cb),
  };
  // Extract the exact real function, erase its type with the existing evaluator;
  // this does not start performanceSchedule or run its service test a second time.
  const scheduleSource=fs.readFileSync(path.join(root,'src/bridge/performanceSchedule.ts'),'utf8');
  const revisionFunction=scheduleSource.match(/^export function gameProfileInputRevision\([^]*?^\}/m);
  assert(revisionFunction,'real gameProfileInputRevision source must exist');
  const {gameProfileInputRevision}=evaluate(revisionFunction[0]);
  const assertTarget = expected => {
    if(expected.generation!==settingsGeneration || expected.exe!==hysteresis.gamePolicyKey(game) ||
       expected.pid!==(game?.pid||0) || expected.processCreated!==String(game?.processCreated||'')){
      refusals.push('dedicated-stale-target');throw Error('当前游戏已变化');
    }
  };
  const perfPort = {
    gameProfileInputRevision,loadGameCustomConfig:async()=>copy(config),
    onGameCustomConfigChanged:cb=>subscribe(configListeners,cb),
    async patchGameCustomProfile(expected,fields,_unused,options){
      assertTarget(expected);
      const entry=config.entries[expected.exe];
      if(!entry || options?.expectedEnabled!==true || entry.enabled===false ||
          options.expectedInputRevision!==gameProfileInputRevision(entry)){
        refusals.push('dedicated-revision-conflict');throw Error('专属输入配置已变化');
      }
      assert(Object.keys(fields).every(key=>['padPersona','gyroOverride','gyroPreset','inputOverrides'].includes(key)),'input editor write scope');
      config.entries[expected.exe]=repo.mergeSettings(entry,fields);
      dedicatedWrites.push({target:copy(expected),fields:copy(fields)});
      for(const cb of [...configListeners])cb(copy(config));
    },
    async removeGameCustomProfile(){throw Error('Profile removal is outside these six cases');},
    async restorePerformanceScheduleIfConfigured(){throw Error('Performance scheduling is outside these six cases');},
    async clearGameInputRestoreSnapshot(){throw Error('Legacy restore is outside the UI scope');},
  };
  const pageIpc = {
    setGyroTelemetryInterest:()=>{},
    getLastGamepadState:()=>null,
    on:(name,cb)=>{if(!ipcListeners.has(name))ipcListeners.set(name,new Set());return subscribe(ipcListeners.get(name),cb);},
    async invoke(name,args){
      ipcCalls.push({name,args:copy(args)});
      switch(name){
        case 'oem.keys.status':return null;
        case 'oem.keys.get':return {familyId:'unknown',supported:false,keys:[]};
        case 'input.shortcutRuntime.get':return {active:false};
        case 'input.shortcutRuntime.clear':return {ok:true}; // memory ACK, never native admission
        case 'gyro.calibrate.status':return {status:'not-calibrated',trusted:false};
        default:throw Error('Unexpected UI IPC '+name);
      }
    },
  };
  const overlay = load('src/bridge/gameInputOverride.ts', {
    './gyroPresetModel':load('src/bridge/gyroPresetModel.ts'),
    '@/bridge/settingsRepository':settingsPort,'@/bridge/gamePolicyTarget':policyPort,
    '@/bridge/performanceSchedule':perfPort,'@/bridge/ipc':pageIpc,
    '@/scheduler':{registerScheduledTask(){throw Error('Resident overlay scheduling is outside UI-only');}},
  },clock);
  const editor = load('src/bridge/gameInputEditor.ts', {
    './settingsSnapshot':snapshots,'./settingsRepository':settingsPort,'./performanceSchedule':perfPort,
    './gamePolicyTarget':policyPort,'./gameInputOverride':overlay,
    './gamedetect':{detectedGameName:target=>target?.title||target?.name||''}, // explicit in-memory identity label
  },clock);
  const virtualTargetPolicy=load('src/bridge/virtualTargetPolicy.ts');
  const radialAvailability=load('src/radial/availability.ts');
  const radialTestingState=load('src/radial/testingState.ts',{'./availability':radialAvailability});
  const controllerShortcutRules=load('src/bridge/controllerShortcutRules.ts',{'./controllerPersona':personas,'../radial/testingState':radialTestingState});
  const mousePresentation=load('src/bridge/mouseModePresentation.ts');
  const imports = {
    '@/bridge/controllerPersona':personas,'@/bridge/settingsRepository':settingsPort,'@/bridge/ipc':pageIpc,
    '@/bridge/performanceSchedule':perfPort,'@/bridge/gameInputEditor':editor,
    '@/bridge/gyroPresetSave':load('src/bridge/gyroPresetSave.ts',{'./gameInputEditor':editor}),
    '@/bridge/virtualTargetPolicy':virtualTargetPolicy,
    '@/bridge/virtualTargetPolicyState':load('src/bridge/virtualTargetPolicyState.ts',{'./ipc':pageIpc,'./virtualTargetPolicy':virtualTargetPolicy}),
    '@/bridge/virtualOutputStatus':load('src/bridge/virtualOutputStatus.ts',{'./virtualTargetPolicy':virtualTargetPolicy}),
    '@/bridge/controllerShortcutRules':controllerShortcutRules,
    '@/bridge/controllerFeatureStatus':load('src/bridge/controllerFeatureStatus.ts',{'./controllerShortcutRules':controllerShortcutRules}),
    '@/bridge/virtualStartProtectionView':load('src/bridge/virtualStartProtectionView.ts'),
    '@/bridge/inputContracts':load('src/bridge/inputContracts.ts',{'./controllerPersona':personas}),
    '@/bridge/gamepadEnvironmentNotice':load('src/bridge/gamepadEnvironmentNotice.ts',{'./hidHideSetupPrompt':load('src/bridge/hidHideSetupPrompt.ts')}),
    '@/bridge/mouseModePresentation':mousePresentation,
    '@/bridge/mouseModeState':load('src/bridge/mouseModeState.ts',{'./mouseModePresentation':mousePresentation}),
    '@/bridge/quickActionLock':load('src/bridge/quickActionLock.ts'),
    '@/gamepad/focus':load('src/gamepad/focus.ts',{},clock),
    '@/bridge/gameproc':{closeJoyxoffIfRunning:async()=>{joy++;}}, // process boundary: counter only
    '@/robust/frontendDiagnostics':{reportFrontendTrace:(...args)=>traces.push(copy(args))},
    '@/bridge/uiLifecycle':{isUiVisible:()=>true,onUiVisibilityChange:()=>()=>{}},
    '@/bridge/gyroPresentation':load('src/bridge/gyroPresentation.ts',{},clock),
    '@/bridge/gyroAxisFormat':load('src/bridge/gyroAxisFormat.ts'),
  };
  // Explicit child boundaries: layout/preview stubs forward all named/default
  // slots and props, so real page controls mount. No browser, focus or sensor claim.
  const layout={inheritAttrs:false,setup:(_,{attrs,slots})=>()=>vue.h('div',attrs,Object.values(slots).filter(slot=>typeof slot==='function').flatMap(slot=>slot()))};
  const child={inheritAttrs:false,props:['modelValue','disabled','label'],emits:['update:modelValue'],
    setup:(props,{emit,attrs})=>()=>vue.h('button',{...attrs,label:props.label,disabled:props.disabled,'data-stub-control':true,'data-stub-model':props.modelValue,onTestUpdate:value=>emit('update:modelValue',value)},String(props.modelValue??''))};
  for(const name of ['ScreenTouchpadsSettings','ControllerShortcutsCard','ControllerFeedbackCapabilitiesCard','SteamDeckMouseSensitivity','SegButton','Slider','Toggle','Dropdown'])imports['@/components/'+name+'.vue']={__esModule:true,default:child};
  for(const name of ['ControllerChannelStatus','GameProfileRemoveDialog','UsbipUpgradeStatus','ControllerCalibrationCard','GyroInputOutputPreview','GyroSimulationWindow','GyroStatusPreview'])imports['@/components/'+name+'.vue']={__esModule:true,default:layout};
  imports['@/views/ControllerShortcutEditorView.vue']={__esModule:true,default:layout};
  imports['@/components/InlineIcon.vue']={__esModule:true,default:{setup:()=>()=>vue.h('span')}};
  function component(file,bindings=imports){
    const {descriptor}=parse(fs.readFileSync(path.join(root,file),'utf8'),{filename:file});
    const compiled=compileScript(descriptor,{id:'game-policy-test',inlineTemplate:true,templateOptions:{compilerOptions:{hoistStatic:false}}});
    return evaluate(compiled.content,bindings,clock).default;
  }
  imports['@/components/IntegratedPage.vue']={__esModule:true,default:component('src/components/IntegratedPage.vue')};
  imports['@/components/GameProfileModeBanner.vue']={__esModule:true,default:component('src/components/GameProfileModeBanner.vue',{
    './OverflowGameName.vue':{__esModule:true,default:{props:['name'],setup:props=>()=>vue.h('span',{'data-stub-game-name':true},props.name)}},
  })};
  const container=element('root'),app=renderer.createApp(component(file));app.mount(container);await flush();
  async function enterDedicated(target=A,entry=profile()){
    game={...target,title:target.pid===A.pid?'Game A':'Game B'};
    const exe=hysteresis.gamePolicyKey(game);
    if(!config.entries[exe])config.entries[exe]={...copy(entry),displayName:game.title};
    for(const cb of [...gameListeners])cb(copy(game));
    for(const cb of [...configListeners])cb(copy(config));
    await flush();
  }
  async function enterGlobal(){game=null;for(const cb of [...gameListeners])cb(null);await flush();}
  return {
    container,editor,perfPort,overlay,globalCas,dedicatedWrites,refusals,ipcCalls,traces,
    get settings(){return settings;},get config(){return config;},get timerCount(){return timers.size;},get pendingCas(){return pendingCas;},
    get writes(){return globalCas.filter(row=>row.status==='COMMITTED').length;},get joy(){return joy;},
    enterDedicated,enterGlobal,
    holdNextGlobalCas(){assert(!casGate);let release;const promise=new Promise(resolve=>{release=resolve;});casGate={promise};return async()=>{release();await flush();};},
    async tick(ms){const end=now+ms;let calls=0;while(true){const due=[...timers].filter(([,t])=>t.at<=end).sort((a,b)=>a[1].at-b[1].at)[0];if(!due)break;assert(++calls<100,'bounded memory timer drain');const[id,timer]=due;now=timer.at;timers.delete(id);timer.fn();await flush();}now=end;await flush();},
    unmount(){app.unmount();assert.equal(timers.size,0,'page timers disposed');assert([...ipcListeners.values()].every(set=>set.size===0),'page IPC subscriptions disposed');uiEvidence.push({page:file,globalCas:copy(globalCas),dedicatedWrites:copy(dedicatedWrites),refusals:copy(refusals),joy,ipcMethods:ipcCalls.map(call=>call.name),timersAfterUnmount:timers.size});},
  };
}
await check('真实控制器页面直接编辑游戏专属；公用不变且旧owner写入被拒绝', async()=>{
  const f=await pageFixture('src/views/ButtonMappingView.vue'),baseBytes=JSON.stringify(f.settings);
  await f.enterDedicated();
  assert(f.container.textContent.includes('专属模式'));assert(f.container.textContent.includes('Game A'));
  assert(descendants(f.container).some(n=>n.props['aria-label']==='当前编辑专属游戏配置'));
  const buttons=()=>descendants(f.container).filter(n=>n.type==='button'&&n.props.class?.includes('persona-btn'));
  assert(buttons().length>=4);assert(buttons().every(n=>!n.props.disabled));
  assert(buttons().find(n=>n.textContent==='关闭虚拟手柄').props.class.includes('active'));
  buttons().find(n=>n.textContent==='SteamDeck').props.onClick();await flush();
  assert.equal(f.config.entries['a.exe'].padPersona,'steamdeck');assert.equal(f.dedicatedWrites.length,1);
  assert.equal(f.writes,0);assert.equal(f.globalCas.length,0);assert.equal(JSON.stringify(f.settings),baseBytes);assert.equal(f.joy,1);
  assert(buttons().find(n=>n.textContent==='SteamDeck').props.class.includes('active'));
  buttons().find(n=>n.textContent==='关闭虚拟手柄').props.onClick();await flush();
  const off=await f.editor.readGameInputEditor();assert.equal(off.input.outputTarget.persona,'disabled');
  assert.equal(off.input.outputTarget.gyroEnabled,false);assert.equal(off.input.gyroMotion.enabled,false);
  assert.equal(f.dedicatedWrites.length,2);assert.equal(JSON.stringify(f.settings),baseBytes);
  await assert.rejects(f.perfPort.patchGameCustomProfile(off.target,{},undefined,{expectedEnabled:true,expectedInputRevision:'stale'}),/专属输入配置已变化/);
  await f.enterDedicated(B);
  await assert.rejects(f.editor.saveGameInputEditor(off,{outputTarget:{persona:'elite'}}),/当前编辑对象已变化/);
  await assert.rejects(f.perfPort.patchGameCustomProfile(off.target,{},undefined,{expectedEnabled:true,expectedInputRevision:f.perfPort.gameProfileInputRevision(off.profile)}),/当前游戏已变化/);
  assert.equal(f.dedicatedWrites.length,2);assert(f.dedicatedWrites.every(row=>row.target.exe==='a.exe'&&row.target.pid===A.pid));
  assert.equal(f.globalCas.length,0);assert.equal(JSON.stringify(f.settings),baseBytes);
  await f.enterGlobal();assert(!f.container.textContent.includes('专属模式'));assert(buttons().every(n=>!n.props.disabled));f.unmount();
});
await check('真实陀螺仪页面直接细调同一专属；公用预设不变且载入不自动保存', async()=>{
  const initial=base();initial.outputTarget.persona='steamdeck';initial.outputTarget.buttonMappingEnabled=true;
  const f=await pageFixture('src/views/GyroMotionView.vue',initial),baseBytes=JSON.stringify(f.settings);
  await f.enterDedicated();await f.tick(5000);
  assert(f.container.textContent.includes('专属模式'));assert(f.container.textContent.includes('Game A'));
  const nodes=descendants(f.container),power=nodes.filter(n=>n.type==='button'&&n.props.class?.includes('gyro-power-btn'));
  assert.equal(power.length,2);assert(!power[0].textContent.includes('已开启'));assert(!power[1].props.disabled);
  const controls=nodes.filter(n=>n.props['data-stub-control']);assert(controls.length>4);assert(controls.some(n=>!n.props.disabled));
  assert.equal(f.writes,0);assert.equal(f.dedicatedWrites.length,0);
  const slider=controls.find(n=>n.props.label==='陀螺仪权重【倍率】');assert(slider);assert(!slider.props.disabled);
  slider.props.onTestUpdate(2.5);await flush();assert(f.timerCount>0);assert.equal(f.dedicatedWrites.length,0);
  await f.tick(5000);assert.equal(f.dedicatedWrites.length,1);assert.equal(f.globalCas.length,0);
  const saved=f.config.entries['a.exe'];assert.equal(saved.inputOverrides.gyroMotion.gyroWeight,2.5);
  assert.equal(saved.inputOverrides.gyroMotion.presets.racing.gyroWeight,2.5);
  const effective=await f.editor.readGameInputEditor();assert.equal(effective.input.gyroMotion.gyroWeight,2.5);
  assert.equal(effective.input.gyroMotion.enabled,false);assert.equal(JSON.stringify(f.settings),baseBytes);
  assert.equal(f.dedicatedWrites[0].target.exe,'a.exe');assert.equal(f.dedicatedWrites[0].target.pid,A.pid);
  await f.enterGlobal();await f.tick(5000);assert.equal(f.dedicatedWrites.length,1);assert.equal(f.writes,0);assert.equal(JSON.stringify(f.settings),baseBytes);f.unmount();
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
await check('开机手柄开启但gyro关闭：陀螺仪内页载入及专属切换不自动联动开启或保存', async () => {
  const initial = base(); initial.outputTarget.persona = 'steamdeck'; initial.outputTarget.buttonMappingEnabled = true;
  initial.gyroMotion.enabled = false; initial.gyroMotion.virtualPadLink = true;
  const f=await pageFixture('src/views/GyroMotionView.vue',initial);await f.tick(5000);
  const power=()=>descendants(f.container).filter(n=>n.type==='button'&&n.props.class?.includes('gyro-power-btn'));
  assert(!power()[0].textContent.includes('已开启')); assert.equal(f.settings.gyroMotion.enabled,false); assert.equal(f.writes,0);
  await f.enterDedicated();assert(!power()[0].textContent.includes('已开启'));
  assert.equal((await f.editor.readGameInputEditor()).input.gyroMotion.enabled,false);
  await f.enterGlobal();await f.tick(5000);assert.equal(f.settings.gyroMotion.enabled,false);assert.equal(f.writes,0);
  assert.equal(f.globalCas.length,0);assert.equal(f.dedicatedWrites.length,0);f.unmount();
});
await check('owner切换取消陀螺仪排队保存；晚到公用CAS拒绝且回归后不重放', async()=>{
  const f=await pageFixture('src/views/GyroMotionView.vue'),baseBytes=JSON.stringify(f.settings);
  const slider=()=>descendants(f.container).find(n=>n.props['data-stub-control']&&n.props.label==='陀螺仪权重【倍率】');
  assert(slider());slider().props.onTestUpdate(2.5);await flush();assert(f.timerCount>0);assert.equal(f.writes,0);
  await f.enterDedicated();await f.tick(5000);assert.equal(f.writes,0);assert.equal(f.dedicatedWrites.length,0);
  await f.enterGlobal();await f.tick(5000);assert.equal(f.writes,0);assert.equal(f.globalCas.length,0);
  // Let the next original queued callback enter the real editor/global CAS, then
  // move owner while persistence is held; commit must re-check isCurrent.
  const release=f.holdNextGlobalCas();slider().props.onTestUpdate(2.7);await flush();await f.tick(3000);
  assert.equal(f.pendingCas,1);assert.equal(f.globalCas.length,1);assert.equal(f.writes,0);
  await f.enterDedicated(B);await release();await f.tick(5000);
  assert.equal(f.globalCas[0].status,'CANCELLED');assert(f.refusals.includes('global-owner-cancelled'));
  assert.equal(f.writes,0);assert.equal(f.dedicatedWrites.length,0);assert.equal(JSON.stringify(f.settings),baseBytes);
  await f.enterGlobal();await f.tick(5000);assert.equal(f.globalCas.length,1);assert.equal(f.writes,0);assert.equal(f.dedicatedWrites.length,0);f.unmount();
});
await check('常驻轮询/重试不随窗口隐藏暂停；原始终止游戏动作未改成延迟目标',()=>{
  const detect=fs.readFileSync(path.join(root,'src/bridge/gamedetect.ts'),'utf8');assert(detect.includes('{ pauseWhenHidden: false, runImmediately: true }'));
  const quick=fs.readFileSync(path.join(root,'src/components/GameQuickActions.vue'),'utf8');assert(quick.includes(':game="policyGame"'));// Validation moved behind ensureTarget -> captureAndLockGameTarget. Keep
  // the same immediate/current-identity invariant instead of a stale direct-import token.
  const close=quick.match(/^async function closeCurrentGame\(\)[^]*?^\}/m)?.[0];assert(close);
  assert.match(close,/target = await ensureTarget\(\);\s*assertActionTarget\(target\);[^]*?const result = await closeGame\(target.pid, target.name, target.processCreated, target\)/);
  assert(!/setTimeout|setInterval|registerScheduledTask/.test(close));
  assert(quick.includes("await captureAndLockGameTarget(candidate, 'action')"));
  const session=fs.readFileSync(path.join(root,'src/bridge/gameQuickSession.ts'),'utf8');
  assert(session.includes('return validateLockedGameTarget()'));
  assert(session.includes('const detected = await validateGameTarget(current)'));
  assert(session.includes('lockGeneration !== current.lockGeneration'));
  assert(session.includes('!isGameTargetSame(current, detected) || !isCurrentGameTarget(detected)'));
  const perf=fs.readFileSync(path.join(root,'src/bridge/performanceSchedule.ts'),'utf8');assert(perf.includes('isPolicyTargetCurrent(expectedTarget)')); assert(perf.includes('ContinuousPerformancePanel'));
});

}
}

if (!inputRuntimeOnly && !uiOnly && !inputOwnerOnly) {
currentSection = 'detection';
// Actual shared detector + IPC timeout + scheduler + policy owner. All native
// process replies and browser time are inert; no game or device is accessed.
async function flushDetection() { for(let i=0;i<100;i++) await Promise.resolve(); }
function detectionFixture(detectorSource = null, { sharedTransport = false } = {}) {
  let now = 0, seq = 0, visible = false;
  const timers = new Map(), events = new Map(), documentEvents = new Map(), bridgeListeners = [], messages = [];
  const eventTarget = handlers => ({
    addEventListener(name, cb) { if (!handlers.has(name)) handlers.set(name, new Set()); handlers.get(name).add(cb); },
    removeEventListener(name, cb) { handlers.get(name)?.delete(cb); },
    dispatchEvent(event) { for (const cb of [...handlers.get(event.type) || []]) cb(event); return true; },
  });
  const win = eventTarget(events), doc = { ...eventTarget(documentEvents), visibilityState:'hidden' };
  const f = { native:null, hold:false, messages, timers, events, scheduler:null, detector:null, policy:null, schedules:[] };
  f.menuWindow=win;
  f.abortRecords=[];class ReadAbortController extends AbortController {constructor(){super();const signal=this.signal,record={signal,callbacks:new Set()},add=signal.addEventListener.bind(signal),remove=signal.removeEventListener.bind(signal);f.abortRecords.push(record);signal.addEventListener=(type,listener,options)=>{if(type==='abort')record.callbacks.add(listener);return add(type,listener,options);};signal.removeEventListener=(type,listener,options)=>{if(type==='abort')record.callbacks.delete(listener);return remove(type,listener,options);};}}
  f.readAbortController=ReadAbortController;
  const clock = {AbortController:ReadAbortController,crypto:globalThis.crypto,performance:{now:()=>now},setTimeout(fn,ms){const id=++seq;timers.set(id,{at:now+ms,fn});return id;},clearTimeout(id){timers.delete(id);},
    window:win,document:doc,CustomEvent:class{constructor(type,opts){this.type=type;this.detail=opts?.detail;}},queueMicrotask};
  Object.assign(win,{setTimeout:clock.setTimeout,clearTimeout:clock.clearTimeout,chrome:{webview:{
    addEventListener(_name,cb){bridgeListeners.push(cb);},
    postMessage(msg){messages.push(structuredClone(msg));if(f.pauseFilePort?.(msg))return;if(f.metadata && msg.cmd==='gameSteam.context.get')return;if(msg.cmd!=='game.detect')throw Error('Unexpected native IO '+msg.cmd);if(f.hold)return;
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
  f.loadGameControl=(detector=f.detector)=>{
    const api=load('src/bridge/api.ts',{'./ipc':ipc,'./steamInstallationFacts':{createSteamInstallationReader:()=>()=>{}},'./bootModeGlobalPreferences':{captureBootPreferenceWrite(options){assert.equal(options.bootContext,undefined);return {origin:options.origin};}}},clock);
    return load('src/bridge/gameproc.ts',{'./ipc':ipc,'./api':api,'./gamedetect':detector,'./powerLifecycleState':{...f.powerModule,...f.sharedPower}},clock);
  };
  f.power={phase:'ready',generation:100,known:true,hardwareWritesAllowed:true,inputReady:true,notificationPending:false,publicationRevision:1,observedNotificationSequence:1};
  const powerModule=load('src/bridge/powerLifecycleState.ts',{'./ipc':{invoke:()=>{throw Error('Unexpected shared singleton read');},on:()=>()=>{}}},clock);
  f.sharedPower=powerModule.createSharedPowerLifecycleSource({read:async()=>{const state={...f.power};return f.powerReadHook?f.powerReadHook(state):state;},listen:sharedTransport?ipc.on:(name,listener)=>{if(!events.has('public:'+name))events.set('public:'+name,new Set());events.get('public:'+name).add(listener);return()=>events.get('public:'+name).delete(listener);},clock:{setTimeout:clock.setTimeout,clearTimeout:clock.clearTimeout}});
  f.stopPowerProbe=f.sharedPower.onSharedPowerLifecycle(()=>{});f.powerModule=powerModule;
  f.broadcastPower=state=>{f.power={...state};if(sharedTransport)f.emitNative('power.lifecycleChanged',state);else for(const cb of [...events.get('public:power.lifecycleChanged')||[]])cb({...state});};
  const uiPower=load('src/bridge/uiLifecycle.ts',{'./ipc':ipc,'./powerLifecycleState':f.sharedPower},clock);
  const scheduler=load('src/scheduler.ts',{'./bridge/uiLifecycle':{...uiPower,isUiVisible:()=>visible,onUiVisibilityChange:()=>()=>{}},'./bridge/powerLifecycleState':f.sharedPower},clock);
  f.scheduler={...scheduler,registerScheduledTask(...args){f.schedules.push({name:args[0],ms:args[1],options:args[3]});return scheduler.registerScheduledTask(...args);}};
  f.detector=evaluate(detectorSource??fs.readFileSync(path.join(root,'src/bridge/gamedetect.ts'),'utf8'),{'./ipc':ipc,'./powerLifecycleState':{...f.sharedPower,isSharedPowerLifecycleAdmitted:powerModule.isSharedPowerLifecycleAdmitted},'@/scheduler':f.scheduler,'../../PowerControl/GameTitlePolicy.json':JSON.parse(fs.readFileSync(path.join(root,'PowerControl/GameTitlePolicy.json'),'utf8'))},clock);
  f.policy=load('src/bridge/gamePolicyTarget.ts',{'./gamedetect':f.detector,'./gamePolicyHysteresis':hysteresis},clock);
  f.loadMetadata=()=>{f.metadata=true;return load('src/bridge/gameSteamContext.ts',{'./gamedetect':f.detector,'./ipc':ipc,'./gameSteamContextModel':load('src/bridge/gameSteamContextModel.ts')},clock).gameSteamContext;};
  return f;
}
const TMNT={pid:11752,processCreated:'134372974230246308',name:'tmnt',title:"Teenage Mutant Ninja Turtles: Shredder's Revenge",path:'E:\\GAME\\Teenage Mutant Ninja Turtles Shredders Revenge Radical Reptiles\\TMNT.exe',source:'whitelist',whitelistRule:'tmnt',ts:1792823823460};
function blockDisplayScheduler(f) { return f.scheduler.registerScheduledTask('inert-slow-policy',2000,()=>new Promise(()=>{}),{runImmediately:true,pauseWhenHidden:false}); }
function quickMenuControlFixture(f, { actualSuspend = false, ensureTarget = async () => f.detector.getCurrentGame() } = {}) {
 const raw=fs.readFileSync(path.join(root,'src/components/GameQuickActions.vue'),'utf8');
 const ts=require('typescript'),descriptor=parse(raw).descriptor;
 const ast=ts.createSourceFile('GameQuickActions.ts',descriptor.scriptSetup.content,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
 const names=new Set(['refreshMenuState','flushMenuStateRefresh','refreshControlState','retireControlStateRead','togglePause','closeCurrentGame','onTargetReadiness','onWakeGameRecovery','onSuspendedStateSync']);
 const states=new Set(['disposed','menuStateEpoch','taskViewSessionEpoch','controlStateRead','resumePreflight','targetEpoch','losslessReadEpoch','menuStateRefreshPending','stopTargetReadiness','stopPolicyGame','stopWakeGameRecovery','wakeGameRecoveryRevision']);
 const state=ast.statements.filter(ts.isVariableStatement).flatMap(n=>[...n.declarationList.declarations]).filter(d=>states.has(d.name.getText(ast))).map(d=>'let '+d.getText(ast)+';');
 const calls=ast.statements.filter(ts.isExpressionStatement).filter(n=>ts.isCallExpression(n.expression)).filter(n=>{
  const call=n.expression,name=call.expression.getText(ast),first=call.arguments[0]?.getText(ast);
  return ['onMounted','onBeforeUnmount'].includes(name)||name==='watch'&&(first==='() => props.open'||first==='disabledForAction');
 });
 const funcs=ast.statements.filter(n=>ts.isFunctionDeclaration(n)&&names.has(n.name?.text));
 assert(calls.some(n=>n.expression.expression.getText(ast)==='onMounted')&&funcs.length>=4,'extract actual menu lifecycle and control reads');
 const props=vue.reactive({open:true}),busy=vue.ref(false),pauseBusy=vue.ref(false),closeBusy=vue.ref(false),paused=vue.ref(false),reads=[],stops=[];
 const disabledForAction=vue.computed(()=>busy.value||pauseBusy.value);
 let unmount,hold=null,holdCommand='fs.exists',stateSuspended=true;
 const original=JSON.stringify({root:TMNT.pid,processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
 const gameControl=f.loadGameControl();
 f.pauseFilePort=msg=>{
  if(msg.cmd==='game.resume')return true;
  if(msg.cmd==='game.pauseRecord.settle'){
   const actual=stateSuspended?original:null;
   const answer=actual===msg.args.raw||actual===msg.args.expected?'committed':'changed';
   if(answer==='committed')stateSuspended=msg.args.raw!==null;
   queueMicrotask(()=>f.reply(msg.id,answer));return true;
  }
  if(!['fs.exists','fs.readTextFile'].includes(msg.cmd))return false;
  assert(msg.args.path.endsWith('quickapp_suspended.json')||msg.args.path.endsWith('manual-suspended\\'+TMNT.pid+'.txt'));
  const send=()=>f.reply(msg.id,msg.cmd==='fs.exists'?stateSuspended:original);
  if(hold&&msg.cmd===holdCommand){const pending=hold;hold=null;void pending.then(value=>f.reply(msg.id,value));}
  else queueMicrotask(send);
  return true;
 };
 const source=[...state,...funcs.map(n=>n.getText(ast)),...calls.map(n=>n.getText(ast)),'export {togglePause,closeCurrentGame};'].join(String.fromCharCode(10));
 const noop=()=>{};
 const action=evaluate(source,{}, {props,paused,pauseBusy,closeBusy,locked:{value:null},disabledForAction,
  nextTick:vue.nextTick,watch:(...args)=>stops.push(vue.watch(...args)),onMounted:callback=>callback(),onBeforeUnmount:callback=>{unmount=callback;},
  window:f.menuWindow,AbortController:f.readAbortController,
  getCurrentGame:f.detector.getCurrentGame,isGameStatusCurrent:f.detector.isGameStatusCurrent,subscribeGameTargetReadiness:f.detector.subscribeGameTargetReadiness,
  onSharedPowerLifecycle:f.sharedPower.onSharedPowerLifecycle,isSharedPowerLifecycleAdmitted:f.powerModule.isSharedPowerLifecycleAdmitted,
  getSharedPowerLifecycle:f.sharedPower.getSharedPowerLifecycle,sharedPowerWriteScope:f.powerModule.sharedPowerWriteScope,
  // Use the authoritative identity stream directly so the app's permanent
  // policy subscriber is not part of this owned menu's cleanup check.
  subscribePolicyGameStatus:f.detector.subscribeGameStatus,isTargetCurrent:target=>target?f.detector.isCurrentGameTarget(target):f.detector.getCurrentGame()===null,
  targetGame:{get value(){return f.detector.getCurrentGame();}},getLockedGameTarget:()=>null,syncCurrentTarget:noop,
  hasSuspendedState:(pid,birth,signal)=>{const read={pid,birth,signal,settled:false};reads.push(read);return gameControl.hasSuspendedState(pid,birth,signal).finally(()=>{read.settled=true;});},
  resumeGame:gameControl.resumeGame,suspendGame:actualSuspend?gameControl.suspendGame:()=>{throw Error('unexpected suspend');},status:noop,
  closeGame:gameControl.closeGame,releaseTarget:noop,refreshGameStatusStrict:f.detector.refreshGameStatusStrict,
  ensureTarget,assertActionTarget:target=>assert(f.detector.isCurrentGameTarget(target)),isActionTargetCurrent:f.detector.isCurrentGameTarget,
  taskViewTouch:{cancel:noop},trainerMenuOpen:{value:false},trainerCancelRequested:{value:false},trainerStateRevision:{value:0},
  stopLosslessObservation:noop,stopLosslessState:noop,stopTrainerState:noop,retainLosslessObservation:()=>noop,onLosslessScalingChanged:()=>noop,
  subscribeGameTrainerState:()=>noop,refreshLosslessState:noop,refreshSpeedState:noop,closeLosslessMenu:noop,fsrResolve:null,
  onGamepadBack:noop,resizeOptiDialog:noop,onTargetLost:noop,refreshGame:noop,QUICKAPP_SUSPENDED_EVENT:'quickapp:suspended-state',
 });
 return {props,busy,paused,pauseBusy,closeBusy,reads,togglePause:action.togglePause,closeCurrentGame:action.closeCurrentGame,closeGame:gameControl.closeGame,setSuspended(value){stateSuspended=value;},holdRead(promise,cmd='fs.exists'){hold=promise;holdCommand=cmd;},close(){unmount();for(const stop of stops)stop();}};
}
await check('non-Steam quick menu rereads pause after coalesced Ready without a feature receipt',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f),seen=[];
 f.broadcastPower({...f.power,suspendSerial:0});f.native=versionedGame(TMNT,10,10);
 const stop=f.detector.subscribeGameStatus(game=>seen.push(game));let menu,releaseOld,releaseClosed;
 try{
  await f.detector.refreshGameStatusStrict();await flushDetection();assert(f.detector.isGameStatusCurrent());
  menu=quickMenuControlFixture(f);await flushDetection();
  assert.equal(menu.paused.value,true);assert.equal(menu.reads.length,1);
  menu.holdRead(new Promise(resolve=>{releaseOld=resolve;}));
  f.signal('quickapp:suspended-state');await flushDetection();assert.equal(menu.reads.length,2);
  const identityPublications=seen.length,previous={...f.power};
  menu.busy.value=true;menu.setSuspended(false);
  f.native=versionedGame(TMNT,12,10);
  f.broadcastPower({...previous,publicationRevision:2,suspendSerial:1});await f.advance(1250);
  assert(f.detector.isGameStatusCurrent());assert.equal(seen.length,identityPublications);
  assert.equal(menu.reads.length,2,'the recovered read must wait for the busy menu');
  assert.equal(menu.reads[1].settled,true,'public readiness loss retained the old pause read');
  assert.equal(menu.reads[1].signal?.aborted,true);
  const beforeLate=f.messages.length;
  releaseOld(false);await flushDetection();
  assert.equal(f.messages.length,beforeLate,'old exists reply started another file query');
  assert.equal(menu.paused.value,true,'old pause read overwrote the current target before the successor read');
  menu.busy.value=false;await flushDetection();
  assert.equal(menu.reads.length,3,'same game and public Ready must reread once without a feature receipt');
  assert.equal(menu.paused.value,false);
  assert(menu.reads.every(row=>row.pid===TMNT.pid&&row.birth===TMNT.processCreated));
  menu.props.open=false;await flushDetection();
  f.native=versionedGame(TMNT,13,10);
  f.broadcastPower({...f.power,publicationRevision:3,suspendSerial:2});await f.advance(1250);
  assert.equal(menu.reads.length,3,'a closed menu did not release its read demand');
  menu.setSuspended(true);menu.holdRead(new Promise(resolve=>{releaseClosed=resolve;}),'fs.readTextFile');
  menu.props.open=true;await flushDetection();assert.equal(menu.reads.length,4);assert.equal(menu.reads[3].settled,false);
  assert.equal(f.messages.at(-1).cmd,'fs.readTextFile');
  menu.props.open=false;await flushDetection();
  assert.equal(menu.reads[3].settled,true,'closing the menu retained the old record read');
  assert.equal(menu.reads[3].signal?.aborted,true);
  releaseClosed(JSON.stringify({root:TMNT.pid,processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]}));await flushDetection();
  assert.equal(menu.paused.value,false,'late file content changed the closed menu');
 }finally{releaseOld?.(false);releaseClosed?.('');menu?.close();stop();block();f.stopPowerProbe();}
 await flushDetection();
 assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);
 assert.equal(f.timers.size,0);
 assert(f.abortRecords.every(r=>r.callbacks.size===0),'pause status retained an abort listener');
});
await check('non-Steam menu receives completed wake from the public snapshot',async()=>{
 const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);
 const initial={...f.power,suspendSerial:0};
 f.broadcastPower(initial);f.native=versionedGame(TMNT,10,10);
 const off=f.detector.subscribeGameStatus(()=>{});let menu,releaseOld;
 const commands=cmd=>f.messages.filter(m=>m.cmd===cmd);
 try{
  await f.detector.refreshGameStatusStrict();menu=quickMenuControlFixture(f);await flushDetection();assert(menu.paused.value);
  menu.holdRead(new Promise(resolve=>{releaseOld=resolve;}),'fs.readTextFile');
  f.native=versionedGame(TMNT,11,10);
  const ready={...initial,publicationRevision:2,suspendSerial:1,observedNotificationSequence:2};
  f.broadcastPower(ready);await flushDetection();
  const old=menu.reads.at(-1);assert(old&&!old.settled);
  const observed=menu.reads.length;
  menu.setSuspended(false);
  const completed={...ready,gameWakeRecoveryRevision:1};
  f.broadcastPower(completed);await flushDetection();
  assert(old.signal.aborted,'a lost private completion must retire the in-flight old pause read');
  assert.equal(menu.reads.length,observed+1);
  assert.equal(menu.paused.value,false);
  releaseOld(JSON.stringify({root:TMNT.pid,processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]}));
  f.broadcastPower(ready);f.broadcastPower(completed);await flushDetection();
  assert.equal(menu.reads.length,observed+1,'old and duplicate snapshots must not reread');
  assert.equal(menu.paused.value,false);

  const pending={...completed,publicationRevision:3,suspendSerial:2,observedNotificationSequence:3,
   phase:'resuming',hardwareWritesAllowed:false,notificationPending:true,gameWakeRecoveryRevision:2};
  f.broadcastPower(pending);await flushDetection();
  assert.equal(menu.reads.length,observed+1,'a terminal notice cannot grant current read admission');
  f.native=versionedGame(TMNT,12,10);
  f.broadcastPower({...pending,publicationRevision:4,phase:'ready',hardwareWritesAllowed:true,notificationPending:false});
  await f.detector.refreshGameStatusStrict();await flushDetection();
  assert.equal(menu.reads.length,observed+2,'the current Ready observes the retained change once');
  assert.equal(commands('game.resume').length,0);
  assert.equal(commands('game.pauseRecord.settle').length,0);
 }finally{releaseOld?.('');menu?.close();off();block();f.stopPowerProbe();}
 await flushDetection();assert.equal(f.timers.size,0);assert(f.abortRecords.every(r=>r.callbacks.size===0));
});
await check('non-Steam pause button cannot reuse an older click after target validation',async()=>{
 for(const boundary of ['coalesced-ready','notification-only','metadata-only']){
  const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);
  const initial={...f.power,suspendSerial:0};f.broadcastPower(initial);f.native=versionedGame(TMNT,10,10);
  const stop=f.detector.subscribeGameStatus(()=>{});let menu,release;
  const commands=()=>f.messages.filter(msg=>msg.cmd==='game.resume'||msg.cmd==='game.suspend');
  const settled=()=>f.messages.filter(msg=>msg.cmd==='game.pauseRecord.settle');
  try{
   await f.detector.refreshGameStatusStrict();
   const target=f.detector.getCurrentGame();let pending=new Promise(resolve=>{release=()=>resolve(target);});
   menu=quickMenuControlFixture(f,{ensureTarget:async()=>{const waiting=pending;pending=null;return waiting?await waiting:f.detector.getCurrentGame();}});
   await flushDetection();assert(menu.paused.value);
   const file=f.pauseFilePort;f.pauseFilePort=msg=>{
    if(msg.cmd!=='game.resume')return file(msg);
    queueMicrotask(()=>f.reply(msg.id,{resumed:1,failedPids:[],stalePids:[]}));return true;
   };
   const old=menu.togglePause();await flushDetection();assert(menu.pauseBusy.value);assert.equal(commands().length,0);
   f.native=versionedGame(TMNT,11,10);
   f.broadcastPower({...initial,publicationRevision:2,suspendSerial:boundary==='coalesced-ready'?1:0,
     observedNotificationSequence:boundary==='metadata-only'?1:2});
   await f.detector.refreshGameStatusStrict();await flushDetection();
   assert(f.detector.isCurrentGameTarget(target),'same PID, birth and identity remain current');
   release();await old;await flushDetection();
   assert.equal(menu.pauseBusy.value,false,boundary+' must release the old button action');
   const valid=boundary==='metadata-only';
   assert.equal(commands().length,valid?1:0,boundary+' must not submit a click from an older public scope');
   assert.equal(settled().length,valid?1:0,boundary+' must not settle an unsent command');
   if(!valid){assert(menu.paused.value);await menu.togglePause();await flushDetection();}
   assert.equal(commands().length,1,boundary+' current explicit resume submits once');
   assert.equal(settled().length,1);assert.equal(menu.paused.value,false);
  }finally{release?.();menu?.close();stop();block();f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);assert(f.abortRecords.every(record=>record.callbacks.size===0));
 }
});
for(const stage of ['menu target validation','bridge target validation']){
 await check(`non-Steam close retires a click across ${stage}`,async()=>{
  for(const changed of [true,false]){
   const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);
   const initial={...f.power,suspendSerial:0};f.broadcastPower(initial);f.native=versionedGame(TMNT,10,10);
   const stop=f.detector.subscribeGameStatus(()=>{});let menu,release;
   const closePosts=()=>f.messages.filter(msg=>msg.cmd==='game.close');
   try{
    await f.detector.refreshGameStatusStrict();
    const target=f.detector.getCurrentGame();assert(target);let entered=false;
    const held=new Promise(resolve=>{release=resolve;});
    const delayOnce=async value=>{if(!entered){entered=true;await held;}return value;};
    let click;
    if(stage==='menu target validation'){
     menu=quickMenuControlFixture(f,{ensureTarget:()=>delayOnce(f.detector.getCurrentGame())});
     click=menu.closeCurrentGame;
    }else{
     const game=f.loadGameControl({...f.detector,detectGame:async (...args)=>delayOnce(await f.detector.detectGame(...args))});
     click=()=>game.closeGame(target.pid,target.name,target.processCreated,target);
    }
    const file=f.pauseFilePort;f.pauseFilePort=msg=>{
     if(msg.cmd!=='game.close')return file?.(msg);
     queueMicrotask(()=>f.reply(msg.id,{ok:false,attempted:false,exited:false}));return true;
    };
    const old=click();await flushDetection();assert(entered);assert.equal(closePosts().length,0);
    f.native=versionedGame(TMNT,11,10);
    f.broadcastPower({...initial,publicationRevision:2,suspendSerial:Number(changed),observedNotificationSequence:changed?2:1});
    await f.detector.refreshGameStatusStrict();await flushDetection();
    assert(f.detector.isCurrentGameTarget(target),'the same game is still the current target');
    release();await old;await flushDetection();
    if(menu)assert.equal(menu.closeBusy.value,false,'closing state is released');
    assert.equal(closePosts().length,changed?0:1,'only a current power scope can close a game');
    if(changed){await click();await flushDetection();assert.equal(closePosts().length,1,'a fresh click can close the same game');}
    assert.equal(f.messages.filter(msg=>msg.cmd==='game.pauseRecord.settle').length,0);
   }finally{release?.();menu?.close();stop();block();f.stopPowerProbe();}
   await flushDetection();assert.equal(f.timers.size,0);
  }
 });
}
await check('non-Steam resume retires unsent preflight and waits for a submitted receipt',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f);f.broadcastPower({...f.power,suspendSerial:0});f.native=versionedGame(TMNT,10,10);
 const stop=f.detector.subscribeGameStatus(()=>{});let menu,releaseOld,releaseTimed,releaseUnreadable;
 const resumeMessages=()=>f.messages.filter(msg=>msg.cmd==='game.resume');
 const recordChanges=()=>f.messages.filter(msg=>msg.cmd==='game.pauseRecord.settle'||msg.cmd==='fs.writeTextFileAtomic');
 const payload=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
 try{
  await f.detector.refreshGameStatusStrict();menu=quickMenuControlFixture(f);await flushDetection();assert(menu.paused.value);
  menu.holdRead(new Promise(resolve=>{releaseOld=resolve;}));
  const first=menu.togglePause();await flushDetection();assert(menu.pauseBusy.value);assert.equal(f.messages.at(-1).cmd,'fs.exists');
  f.native=versionedGame(TMNT,11,10);f.broadcastPower({...f.power,publicationRevision:2,observedNotificationSequence:2,suspendSerial:1});await flushDetection();
  assert(!menu.pauseBusy.value,'a coalesced Ready retained the unsent resume preflight');await first;
  assert(menu.paused.value);assert.equal(resumeMessages().length,0);
  releaseOld(true);await flushDetection();assert.equal(resumeMessages().length,0,'an old file reply dispatched resume');

  menu.holdRead(new Promise(resolve=>{releaseTimed=resolve;}),'fs.readTextFile');
  const timed=menu.togglePause();await flushDetection();assert.equal(f.messages.at(-1).cmd,'fs.readTextFile');
  await f.advance(4999);assert(menu.pauseBusy.value);await f.advance(1);
  assert(!menu.pauseBusy.value,'missing record read kept the control queue busy past its budget');await timed;
  assert(menu.paused.value);assert.equal(resumeMessages().length,0);
  releaseTimed(payload);await flushDetection();assert.equal(resumeMessages().length,0);

  menu.holdRead(new Promise(resolve=>{releaseUnreadable=resolve;}),'fs.readTextFile');
  const unreadable=menu.togglePause();await flushDetection();
  const unreadableRequest=f.messages.at(-1);assert.equal(unreadableRequest.cmd,'fs.readTextFile');
  f.fail(unreadableRequest.id,'fixture sharing denied');await unreadable;
  assert(!menu.pauseBusy.value);assert(menu.paused.value);
  assert.equal(resumeMessages().length,0);assert.equal(recordChanges().length,0);
  releaseUnreadable(payload);await flushDetection();assert.equal(resumeMessages().length,0);

  const submitted=menu.togglePause();await flushDetection();assert.equal(resumeMessages().length,1);
  const request=resumeMessages()[0];assert.deepEqual(request.args.pids,[{pid:TMNT.pid,processCreated:TMNT.processCreated}]);
  f.native=versionedGame(TMNT,12,10);f.broadcastPower({...f.power,publicationRevision:3,observedNotificationSequence:3,suspendSerial:2});await flushDetection();
  assert(menu.pauseBusy.value,'power change released an already submitted command');
  await menu.togglePause();assert.equal(resumeMessages().length,1);
  const file=f.pauseFilePort;
  f.pauseFilePort=msg=>msg.cmd==='game.resume.status'||file(msg);
  const result={resumed:1,failedPids:[],stalePids:[]};
  f.reply(request.id,result);await submitted;await flushDetection();
  assert(menu.paused.value);assert.equal(recordChanges().length,0);
  const receipts=f.messages.filter(msg=>msg.cmd==='game.resume.status');assert.equal(receipts.length,1);
  assert.equal(receipts[0].args.requestId,request.args.requestId);
  f.reply(receipts[0].id,{requestId:request.args.requestId,state:'completed',result});await flushDetection();
  assert(!menu.pauseBusy.value);assert(!menu.paused.value);assert.equal(resumeMessages().length,1);
  assert.equal(recordChanges().length,1);assert.equal(recordChanges()[0].cmd,'game.pauseRecord.settle');assert.equal(recordChanges()[0].args.raw,null);
 }finally{releaseOld?.(true);releaseTimed?.(payload);releaseUnreadable?.(payload);for(const request of resumeMessages())f.reply(request.id,{resumed:1});menu?.close();stop();block();f.stopPowerProbe();}
 await flushDetection();assert.equal(f.timers.size,0);assert(f.abortRecords.every(record=>record.callbacks.size===0));
});
await check('non-Steam resume resolves a lost reply by receipt without replay',async()=>{
 for(const outcome of ['ready','completed-event','changed-record','unknown','expired']){
  const f=detectionFixture();f.broadcastPower({...f.power,suspendSerial:0});f.native=versionedGame(TMNT,10,10);
  const stop=f.detector.subscribeGameStatus(()=>{});let menu;
  const requests=command=>f.messages.filter(msg=>msg.cmd===command);
  const result={resumed:1,failedPids:[],stalePids:[]};
  try{
   await f.detector.refreshGameStatusStrict();menu=quickMenuControlFixture(f);await flushDetection();assert(menu.paused.value);
   const file=f.pauseFilePort;let state='running',different=false;
   f.pauseFilePort=msg=>{
    if(msg.cmd==='game.resume.status'){
     const response={requestId:msg.args.requestId,state};
     if(state==='completed')response.result=result;
     if(state==='expired'){response.state='completed';response.error='expired before execution';}
     queueMicrotask(()=>f.reply(msg.id,response));return true;
    }
    if(different&&msg.cmd==='game.pauseRecord.settle'){
     queueMicrotask(()=>f.reply(msg.id,'changed'));return true;
    }
    return file(msg);
   };
   const first=menu.togglePause();await flushDetection();
   const [submitted]=requests('game.resume');assert(submitted);assert(menu.pauseBusy.value);
   await f.advance(7999);assert(menu.pauseBusy.value);await f.advance(1);
   assert(!menu.pauseBusy.value,'lost reply kept the menu busy');await first;
   assert.equal(requests('game.resume.status').length,1);assert.equal(requests('game.pauseRecord.settle').length,0);
   await menu.togglePause();assert.equal(requests('game.resume').length,1);assert.equal(requests('game.resume.status').length,2);
   state=outcome==='unknown'||outcome==='expired'?outcome:'completed';
   different=outcome==='changed-record';
   if(outcome==='ready'){
    f.native=versionedGame(TMNT,11,10);f.broadcastPower({...f.power,publicationRevision:2,observedNotificationSequence:2,suspendSerial:1});
   } else if(outcome==='completed-event'||outcome==='changed-record'){
    f.emitNative('game.resume.completed',{requestId:'other-request'});await flushDetection();
    assert.equal(requests('game.resume.status').length,2);
    f.emitNative('game.resume.completed',{requestId:submitted.args.requestId});
   } else await menu.togglePause();
   await flushDetection();
   assert.equal(requests('game.resume').length,1);
   assert.deepEqual(requests('game.resume.status').map(row=>row.args.requestId),Array(3).fill(submitted.args.requestId));
   f.reply(submitted.id,result);await flushDetection();
   const cleaned=outcome==='ready'||outcome==='completed-event';
   assert.equal(requests('game.pauseRecord.settle').length,Number(cleaned||outcome==='changed-record'),outcome);
   assert.equal(menu.paused.value,!cleaned,outcome);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{menu?.close();stop();f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);
 }
 for(const operation of ['write','delete'])for(const committedBeforeLoss of [false,true]){
  const f=detectionFixture();f.broadcastPower({...f.power,suspendSerial:0});f.native=versionedGame(TMNT,10,10);
  const stop=f.detector.subscribeGameStatus(()=>{});let menu;
  const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid,33],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated},{pid:33,processCreated:'321'}]});
  const requests=cmd=>f.messages.filter(msg=>msg.cmd===cmd);
  let observed=saved,attempts=0,writes=0;
  try{
   await f.detector.refreshGameStatusStrict();menu=quickMenuControlFixture(f);await flushDetection();
   const file=f.pauseFilePort;
   f.pauseFilePort=msg=>{
    if(msg.cmd==='game.resume'){
     queueMicrotask(()=>f.reply(msg.id,{resumed:1,failedPids:operation==='write'?[33]:[],stalePids:[]}));return true;
    }
    if(msg.cmd==='game.pauseRecord.settle'){
     attempts++;assert.equal(msg.args.expected,saved);
     if(attempts>1||committedBeforeLoss){
      if(observed!==msg.args.raw){assert.equal(observed,msg.args.expected);observed=msg.args.raw;writes++;}
     }
     if(attempts>1)queueMicrotask(()=>f.reply(msg.id,'committed'));
     return true;
    }
    if(msg.cmd==='fs.readTextFile'){queueMicrotask(()=>f.reply(msg.id,observed));return true;}
    if(msg.cmd==='fs.exists'){queueMicrotask(()=>f.reply(msg.id,observed!==null));return true;}
    return file(msg);
   };
   const first=menu.togglePause();await flushDetection();assert(menu.pauseBusy.value);assert.equal(attempts,1);
   await f.advance(4999);assert(menu.pauseBusy.value);await f.advance(1);await first;
   assert(!menu.pauseBusy.value);assert.equal(writes,Number(committedBeforeLoss));
   if(committedBeforeLoss){
    f.native=versionedGame(TMNT,11,10);f.broadcastPower({...f.power,publicationRevision:2,observedNotificationSequence:2,suspendSerial:1});
   }else await menu.togglePause();
   await flushDetection();
   assert.equal(requests('game.resume').length,1);assert.equal(requests('game.resume.status').length,0);
   const commits=requests('game.pauseRecord.settle');assert.equal(commits.length,2);assert.deepEqual(commits[1].args,commits[0].args);
   assert.equal(writes,1);assert.equal(observed,commits[0].args.raw);
   if(operation==='write')assert.deepEqual(JSON.parse(observed).processes,[{pid:33,processCreated:'321'}]);
   else assert.equal(observed,null);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{menu?.close();stop();f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);
 }
 // The request outlives both the menu and game detection. A replacement Ready
 // can arrive while the original status query still holds the old result.
 for(const interrupted of [false,true]){
  const f=detectionFixture(),game=f.loadGameControl();
  const req=cmd=>f.messages.filter(m=>m.cmd===cmd);
  const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
  const initial={...f.power,suspendSerial:0};let disk=saved,held;
  try{
   f.broadcastPower(initial);
   await flushDetection();
   f.pauseFilePort=msg=>{
    const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
    if(msg.cmd==='game.resume')return true;
    if(msg.cmd==='fs.exists'){reply(disk!==null);return true;}
    if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
    if(msg.cmd==='game.resume.status'){
     const i=req('game.resume.status').length,requestId=msg.args.requestId;
     if(i===1)reply({requestId,state:'running'});
     else if(interrupted&&i===2)held=msg;
     else reply({requestId,state:'completed',result:{resumed:1,failedPids:[],stalePids:[]}});
     return true;
    }
    if(msg.cmd==='game.pauseRecord.settle'){
     assert.equal(msg.args.expected,disk);assert.equal(msg.args.raw,null);disk=null;reply('committed');return true;
    }
    return false;
   };
   const first=game.resumeGame();await flushDetection();await f.advance(8000);await first;
   assert.equal(req('game.resume.status').length,1);
   f.broadcastPower({...initial,publicationRevision:2});await flushDetection();
   assert.equal(req('game.resume.status').length,1,'publication-only update consumed a retry');
   const firstReady={...initial,publicationRevision:3,observedNotificationSequence:2,suspendSerial:1};
   f.broadcastPower(firstReady);await flushDetection();
   assert.equal(req('game.resume.status').length,2);
   if(interrupted){
    assert(held);
    const nextReady={...firstReady,publicationRevision:4,observedNotificationSequence:3,suspendSerial:2};
    f.broadcastPower(nextReady);f.broadcastPower(firstReady);await flushDetection();
    assert.equal(req('game.resume.status').length,3,'new Ready must not wait for the retired IPC reply');
    assert.equal(f.timers.size,0,'the retired status read must release its timeout');
    f.reply(held.id,{requestId:held.args.requestId,state:'running'});await flushDetection();
    assert.equal(req('game.resume.status').length,3,'new Ready was lost behind an old read');
   }
   assert.equal(req('game.resume').length,1);assert.equal(req('game.pauseRecord.settle').length,1);assert.equal(disk,null);
   const requestId=req('game.resume')[0].args.requestId;
   assert(req('game.resume.status').every(m=>m.args.requestId===requestId));
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
   assert.equal(req('game.detect').length,0);
  }finally{f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);assert.equal(f.events.get('public:power.lifecycleChanged')?.size??0,0);
 }
});
await check('non-Steam resume checks one receipt after both deliveries are lost',async()=>{
 for(const scenario of ['lost','new-boundary','budget']){
  const f=detectionFixture(),game=f.loadGameControl();
  const calls=cmd=>f.messages.filter(m=>m.cmd===cmd);
  const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
  const initial={...f.power,suspendSerial:0};let disk=saved,nativeDone=false;
  f.broadcastPower(initial);
  await flushDetection();
  f.pauseFilePort=msg=>{
   const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
   if(msg.cmd==='game.resume')return true;
   if(msg.cmd==='fs.exists'){reply(disk!==null);return true;}
   if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
   if(msg.cmd==='game.resume.status'){
    const requestId=msg.args.requestId;
    reply(nativeDone?{requestId,state:'completed',result:{resumed:1,failedPids:[],stalePids:[]}}:{requestId,state:'running'});return true;
   }
   if(msg.cmd==='game.pauseRecord.settle'){
    assert.equal(msg.args.expected,disk);assert.equal(msg.args.raw,null);disk=null;reply('committed');return true;
   }
   return false;
  };
  try{
   const first=game.resumeGame();await flushDetection();await f.advance(8000);await first;
   assert.equal(calls('game.resume.status').length,1);
   if(scenario==='new-boundary'){
    f.broadcastPower({...initial,phase:'suspended',hardwareWritesAllowed:false,publicationRevision:2,suspendSerial:1});
    nativeDone=true;await f.advance(10000);
    assert.equal(calls('game.resume.status').length,1,'suspension must stop the status retry');
    assert.equal(calls('game.pauseRecord.settle').length,0);
    f.broadcastPower({...initial,publicationRevision:3,suspendSerial:1,observedNotificationSequence:2});
   }else if(scenario==='budget'){
    await f.advance(20000);
    assert.equal(calls('game.resume.status').length,4,'only three fallback reads');
    assert.equal(calls('game.pauseRecord.settle').length,0);
    assert.equal(f.timers.size,0,'exhausted fallback must not keep a timer');
    nativeDone=true;
    f.broadcastPower({...initial,publicationRevision:3,suspendSerial:1,observedNotificationSequence:2});
   }else{
    nativeDone=true;await f.advance(500);
   }
   await flushDetection();
   assert.equal(disk,null,scenario+' must settle without replaying the original command');
   assert.equal(calls('game.resume').length,1);
   assert(calls('game.resume.status').every(m=>m.args.requestId===calls('game.resume')[0].args.requestId));
   assert.equal(calls('game.pauseRecord.settle').length,1);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);assert.equal(f.events.get('public:power.lifecycleChanged')?.size??0,0);
 }
});
await check('non-Steam lost resume reply waits for public readmission',async()=>{
 for(const boundary of ['suspended','hint-first']){
  const f=detectionFixture(null,{sharedTransport:true}),game=f.loadGameControl();
  const calls=cmd=>f.messages.filter(m=>m.cmd===cmd);
  const initial={...f.power,suspendSerial:0};
  const next={...initial,publicationRevision:3,suspendSerial:1,observedNotificationSequence:2};
  const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
  let disk=saved;
  try{
   f.broadcastPower(initial);await flushDetection();
   f.pauseFilePort=msg=>{
    const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
    if(msg.cmd==='game.resume')return true;
    if(msg.cmd==='fs.exists'){reply(disk!==null);return true;}
    if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
    if(msg.cmd==='game.resume.status'){
     reply({requestId:msg.args.requestId,state:'completed',result:{resumed:1,failedPids:[],stalePids:[]}});return true;
    }
    if(msg.cmd!=='game.pauseRecord.settle')return false;
    assert.equal(msg.args.expected,saved);assert.equal(msg.args.raw,null);disk=null;reply('committed');return true;
   };
   const first=game.resumeGame();await flushDetection();
   assert.equal(calls('game.resume').length,1);
   if(boundary==='suspended')f.broadcastPower({...next,publicationRevision:2,phase:'suspended',hardwareWritesAllowed:false});
   else f.emitNative('power.resumed',{generation:next.generation,readbackScope:{suspendSerial:1,notificationSequence:2}});
   await f.advance(8000);assert.equal((await first).ok,false);
   assert.equal(calls('game.resume.status').length,0,boundary+' must not start a read when the command times out');
   assert.equal((await game.resumeGame()).ok,false);
   assert.equal(calls('game.resume.status').length,0,'a repeat click must leave the same request waiting for Ready');
   assert.equal(calls('game.pauseRecord.settle').length,0);assert.equal(disk,saved);
   f.emitNative('game.resume.completed',{requestId:calls('game.resume')[0].args.requestId});await flushDetection();
   assert.equal(calls('game.resume.status').length,0,'completion alone cannot retire the wait');
   f.broadcastPower(next);await flushDetection();
   assert.equal(calls('game.resume').length,1);assert.equal(calls('game.resume.status').length,1);
   assert.equal(calls('game.resume.status')[0].args.requestId,calls('game.resume')[0].args.requestId);
   assert.equal(calls('game.pauseRecord.settle').length,1);assert.equal(disk,null);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);
 }
});
await check('non-Steam resume completion releases the missing command reply',async()=>{
 for(const boundary of ['current','suspended']){
  const f=detectionFixture(null,{sharedTransport:true}),game=f.loadGameControl();
  const calls=cmd=>f.messages.filter(m=>m.cmd===cmd);
  const initial={...f.power,suspendSerial:0};
  const current={...initial,publicationRevision:3,suspendSerial:1,observedNotificationSequence:2};
  const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
  const completed={resumed:1,failedPids:[],stalePids:[]};
  let disk=saved,settled=0,answer;
  try{
   f.broadcastPower(initial);await flushDetection();
   f.menuWindow.addEventListener(game.QUICKAPP_SUSPENDED_EVENT,()=>settled++);
   f.pauseFilePort=msg=>{
    const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
    if(msg.cmd==='game.resume')return true;
    if(msg.cmd==='fs.exists'){reply(disk!==null);return true;}
    if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
    if(msg.cmd==='game.resume.status'){
     reply({requestId:msg.args.requestId,state:'completed',result:completed});return true;
    }
    if(msg.cmd!=='game.pauseRecord.settle')return false;
    assert.equal(msg.args.expected,saved);assert.equal(msg.args.raw,null);disk=null;reply('committed');return true;
   };
   const first=game.resumeGame().then(value=>{answer=value;return value;});await flushDetection();
   const command=calls('game.resume')[0];assert(command);
   f.emitNative('game.resume.completed',{requestId:'another-command'});await flushDetection();
   assert.equal(answer,undefined);assert.equal(calls('game.resume.status').length,0);
   if(boundary==='suspended')f.broadcastPower({...current,publicationRevision:2,phase:'suspended',hardwareWritesAllowed:false});
   f.emitNative('game.resume.completed',{requestId:command.args.requestId});
   f.emitNative('game.resume.completed',{requestId:command.args.requestId});await flushDetection();
   assert.equal(answer?.ok,boundary==='current','matched completion must release the command wait before its timeout');
   if(boundary==='suspended'){
    assert.equal(calls('game.resume.status').length,0);assert.equal(calls('game.pauseRecord.settle').length,0);assert.equal(disk,saved);
    f.broadcastPower(current);await flushDetection();
   }
   assert.equal(calls('game.resume.status').length,1);
   assert.equal(calls('game.resume.status')[0].args.requestId,command.args.requestId);
   assert.equal(disk,null);assert.equal(settled,1);
   f.reply(command.id,completed);f.emitNative('game.resume.completed',{requestId:command.args.requestId});await first;await flushDetection();
   assert.equal(calls('game.resume').length,1);assert.equal(calls('game.pauseRecord.settle').length,1);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);
 }
});
await check('non-Steam completion retires its held pre-completion status read',async()=>{
 for(const scenario of ['held','late-running','suspended']){
  const f=detectionFixture(null,{sharedTransport:true}),game=f.loadGameControl();
  const calls=cmd=>f.messages.filter(m=>m.cmd===cmd);
  const initial={...f.power,suspendSerial:0};
  const next={...initial,publicationRevision:3,suspendSerial:1,observedNotificationSequence:2};
  const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
  const completed={resumed:1,failedPids:[],stalePids:[]};
  let disk=saved,settled=0;
  try{
   f.broadcastPower(initial);await flushDetection();
   f.menuWindow.addEventListener(game.QUICKAPP_SUSPENDED_EVENT,()=>settled++);
   f.pauseFilePort=msg=>{
    const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
    if(msg.cmd==='game.resume')return true;
    if(msg.cmd==='fs.exists'){reply(disk!==null);return true;}
    if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
    if(msg.cmd==='game.resume.status'){
     if(calls('game.resume.status').length>1)reply({requestId:msg.args.requestId,state:'completed',result:completed});
     return true;
    }
    if(msg.cmd==='game.pauseRecord.settle'){
     assert.equal(msg.args.expected,saved);assert.equal(msg.args.raw,null);
     disk=null;reply('committed');return true;
    }
    return false;
   };
   const original=game.resumeGame();await flushDetection();await f.advance(8000);
   const [old]=calls('game.resume.status');assert(old,'lost original reply reached the held receipt read');
   const notice={requestId:old.args.requestId};
   f.emitNative('game.resume.completed',{requestId:'other-request'});await flushDetection();
   assert.equal(calls('game.resume.status').length,1,'a foreign completion cannot retire or repeat the old read');
   assert.equal(f.timers.size,1,'foreign completion leaves the original read pending');
   const [oldTimeout]=f.timers.keys();
   if(scenario==='suspended')f.broadcastPower({...next,publicationRevision:2,phase:'suspended',hardwareWritesAllowed:false});
   if(scenario==='late-running')f.reply(old.id,{...notice,state:'running'});
   f.emitNative('game.resume.completed',notice);f.emitNative('game.resume.completed',notice);await flushDetection();
   if(scenario==='suspended'){
    assert.equal(calls('game.resume.status').length,1,'no follow-up while suspended');
    assert.equal(disk,saved);assert.equal(f.timers.has(oldTimeout),false,'suspend releases the old IPC timeout');
    f.broadcastPower(next);await flushDetection();
   }
   assert.equal(calls('game.resume.status').length,2,scenario+' must reread before the retired 2 second timeout');
   assert.equal(f.timers.size,0,'the held read and retry timer must both be gone');
   assert(calls('game.resume.status').every(m=>m.args.requestId===notice.requestId));
   assert.equal(disk,null);assert.equal(settled,1);
   f.reply(old.id,{...notice,state:'unknown'});
   f.emitNative('game.resume.completed',notice);await original;await flushDetection();
   assert.equal(calls('game.resume').length,1);assert.equal(calls('game.resume.status').length,2);
   assert.equal(calls('game.pauseRecord.settle').length,1);assert.equal(settled,1);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);assert(f.abortRecords.every(r=>r.callbacks.size===0));
 }
});
await check('non-Steam resume retires a status reply across the public power boundary',async()=>{
 for(const change of ['suspended','coalesced','same-scope-return']){
  const f=detectionFixture(),game=f.loadGameControl();
  const calls=cmd=>f.messages.filter(m=>m.cmd===cmd);
  const initial={...f.power,suspendSerial:0};
  const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
  let disk=saved,settled=0;
  const completed={resumed:1,failedPids:[],stalePids:[]};
  try{
   f.broadcastPower(initial);await flushDetection();
   f.menuWindow.addEventListener(game.QUICKAPP_SUSPENDED_EVENT,()=>settled++);
   f.pauseFilePort=msg=>{
    const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
    if(msg.cmd==='game.resume'||msg.cmd==='game.resume.status')return true;
    if(msg.cmd==='fs.exists'){reply(disk!==null);return true;}
    if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
    if(msg.cmd!=='game.pauseRecord.settle')return false;
    assert.equal(msg.args.expected,saved);assert.equal(msg.args.raw,null);disk=null;reply('committed');return true;
   };
   const first=game.resumeGame();await flushDetection();await f.advance(8000);
   const old=calls('game.resume.status').at(-1);assert(old);
   const oldWait=f.abortRecords.find(record=>record.callbacks.size>0);assert(oldWait);
   const ready={...initial,publicationRevision:3,...(change==='same-scope-return'?{}:{suspendSerial:1,observedNotificationSequence:2})};
   if(change!=='coalesced')f.broadcastPower({...initial,phase:'suspended',hardwareWritesAllowed:false,publicationRevision:2});
   if(change!=='suspended')f.broadcastPower(ready);
   await flushDetection();
   assert.equal(oldWait.signal.aborted,true,'the retired read must release its IPC wait');
   assert.equal(oldWait.callbacks.size,0,'the retired read must clear its abort listener');
   assert.equal(calls('game.resume.status').length,change==='suspended'?1:2,'only current Ready can start a successor read');
   f.reply(old.id,{requestId:old.args.requestId,state:'completed',result:completed});await first;await flushDetection();
   assert.equal(calls('game.pauseRecord.settle').length,0,change+' must not let a retired status read settle the record');
   assert.equal(settled,0);assert.equal(disk,saved);
   if(change==='suspended'){
    assert.equal(calls('game.resume.status').length,1,'no extra read while suspended');
    f.broadcastPower(ready);await flushDetection();
   }
   assert.equal(calls('game.resume.status').length,2);
   const current=calls('game.resume.status').at(-1);
   assert.equal(current.args.requestId,old.args.requestId);
   f.reply(current.id,{requestId:current.args.requestId,state:'completed',result:completed});await flushDetection();
   assert.equal(calls('game.resume').length,1);assert.equal(calls('game.pauseRecord.settle').length,1);
   assert.equal(disk,null);assert.equal(settled,1);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);
 }
});
await check('non-Steam resume rechecks its late command reply before settling',async()=>{
 for(const change of ['coalesced','hint-first','same-scope']){
  const f=detectionFixture(null,{sharedTransport:true}),game=f.loadGameControl();
  const calls=cmd=>f.messages.filter(m=>m.cmd===cmd);
  const initial={...f.power,suspendSerial:0};
  const current={...initial,publicationRevision:2,suspendSerial:1,observedNotificationSequence:2};
  const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
  const completed={resumed:1,failedPids:[],stalePids:[]};
  let disk=saved,settled=0;
  try{
   f.broadcastPower(initial);await flushDetection();
   f.menuWindow.addEventListener(game.QUICKAPP_SUSPENDED_EVENT,()=>settled++);
   f.pauseFilePort=msg=>{
    const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
    if(msg.cmd==='game.resume'||msg.cmd==='game.resume.status')return true;
    if(msg.cmd==='fs.exists'){reply(disk!==null);return true;}
    if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
    if(msg.cmd!=='game.pauseRecord.settle')return false;
    assert.equal(msg.args.expected,saved);assert.equal(msg.args.raw,null);
    disk=null;reply('committed');return true;
   };
   const first=game.resumeGame();await flushDetection();
   const command=calls('game.resume').at(-1);assert(command);
   if(change==='coalesced')f.broadcastPower(current);
   if(change==='hint-first')f.emitNative('power.resumed',{generation:current.generation,
    readbackScope:{suspendSerial:current.suspendSerial,notificationSequence:current.observedNotificationSequence}});
   if(change==='same-scope')f.broadcastPower({...initial,publicationRevision:2});
   f.reply(command.id,completed);const firstReply=await first;await flushDetection();
   if(change!=='same-scope'){
    assert.equal(calls('game.pauseRecord.settle').length,0,change+' cannot settle from an old command reply');
    assert.equal(firstReply.ok,false);assert.equal(disk,saved);assert.equal(settled,0);
    if(change==='hint-first'){
     assert.equal(calls('game.resume.status').length,0,'an unconfirmed scope cannot start a receipt read');
     f.broadcastPower(current);await flushDetection();
    }
    assert.equal(calls('game.resume.status').length,1);
    const receipt=calls('game.resume.status').at(-1);assert.equal(receipt.args.requestId,command.args.requestId);
    f.reply(receipt.id,{requestId:command.args.requestId,state:'completed',result:completed});await flushDetection();
   }else{
    assert.equal(firstReply.ok,true);assert.equal(calls('game.resume.status').length,0);
   }
   assert.equal(calls('game.resume').length,1);assert.equal(calls('game.pauseRecord.settle').length,1);
   assert.equal(disk,null);assert.equal(settled,1);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);
 }
});
await check('non-Steam resume rechecks a late settlement in the current public scope',async()=>{
 const f=detectionFixture(),game=f.loadGameControl();
 const calls=cmd=>f.messages.filter(m=>m.cmd===cmd);
 const initial={...f.power,suspendSerial:0};
 const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
 let disk=saved,settled=0;
 try{
  f.broadcastPower(initial);await flushDetection();
  f.menuWindow.addEventListener(game.QUICKAPP_SUSPENDED_EVENT,()=>settled++);
  f.pauseFilePort=msg=>{
   const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
   if(msg.cmd==='game.resume'){reply({resumed:1,failedPids:[],stalePids:[]});return true;}
   if(msg.cmd==='fs.exists'){reply(disk!==null);return true;}
   if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
   if(msg.cmd!=='game.pauseRecord.settle')return false;
   assert.equal(msg.args.expected,saved);assert.equal(msg.args.raw,null);
   if(calls('game.pauseRecord.settle').length===1)disk=null;
   return true;
  };
  const first=game.resumeGame();await flushDetection();
  const held=calls('game.pauseRecord.settle').at(-1);assert(held);
  f.broadcastPower({...initial,publicationRevision:2,suspendSerial:1,observedNotificationSequence:2});await flushDetection();
  assert.equal(calls('game.pauseRecord.settle').length,1,'do not overlap compare-and-set calls');
  f.reply(held.id,'committed');await first;await flushDetection();
  assert.equal(settled,0,'a retired commit reply cannot publish the current pause state');
  assert.equal(calls('game.pauseRecord.settle').length,2,'new scope must verify the original settlement again');
  f.reply(calls('game.pauseRecord.settle').at(-1).id,'committed');await flushDetection();
  assert.equal(calls('game.resume').length,1);assert.equal(calls('game.resume.status').length,0);
  assert.equal(disk,null);assert.equal(settled,1);
  assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
 }finally{f.stopPowerProbe();}
 await flushDetection();assert.equal(f.timers.size,0);
});
await check('non-Steam resume settles the original pause record after a lost settlement reply',async()=>{
 for(const scenario of ['reply-lost','new-boundary','changed-record','budget']){
  const f=detectionFixture(),game=f.loadGameControl();
  const initial={...f.power,suspendSerial:0};
  const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
  const replacement=JSON.stringify({root:999,pids:[999]});
  let disk=saved,writes=0,settlements=0,invalidations=0,attempts=0;
  const calls=cmd=>f.messages.filter(m=>m.cmd===cmd);
  try{
   f.broadcastPower(initial);await flushDetection();
   f.menuWindow.addEventListener(game.QUICKAPP_SUSPENDED_EVENT,event=>{
    if(typeof event.detail?.suspended==='boolean')settlements++;
    else invalidations++;
   });
   f.pauseFilePort=msg=>{
    const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
    if(msg.cmd==='game.resume'){reply({resumed:1,failedPids:[],stalePids:[]});return true;}
    if(msg.cmd==='fs.exists'){reply(disk!==null);return true;}
    if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
    if(msg.cmd!=='game.pauseRecord.settle')return false;
    ++attempts;
    assert.equal(msg.args.expected,saved);assert.equal(msg.args.raw,null);
    if(scenario==='budget'&&attempts<5){reply('unavailable');return true;}
    if(attempts===1&&scenario==='new-boundary')return true;
    if(disk!==null&&disk!==saved){reply('changed');return true;}
    if(disk!==null){disk=null;++writes;}
    if(attempts>1)reply('committed');
    return true;
   };
   const first=game.resumeGame();await flushDetection();
   if(scenario!=='budget')await f.advance(5000);
   assert.equal((await first).ok,false);
   assert.equal(attempts,1);
   assert.equal(settlements,0);
   if(scenario==='new-boundary'){
    f.broadcastPower({...initial,phase:'suspended',hardwareWritesAllowed:false,publicationRevision:2,suspendSerial:1});
    await f.advance(20000);assert.equal(attempts,1,'a suspended owner must not retry settlement');
    f.broadcastPower({...initial,publicationRevision:3,suspendSerial:1,observedNotificationSequence:2});
   }else if(scenario==='budget'){
    await f.advance(20000);assert.equal(attempts,4,'only three fallback settlements');
    assert.equal(f.timers.size,0);assert.equal(writes,0);
    f.broadcastPower({...initial,publicationRevision:2,suspendSerial:1,observedNotificationSequence:2});
   }else{
    if(scenario==='changed-record')disk=replacement;
    await f.advance(500);
   }
   await flushDetection();
   assert.equal(attempts,scenario==='budget'?5:2,scenario+' must finish the original settlement without another click');
   assert.equal(disk,scenario==='changed-record'?replacement:null);
   assert.equal(writes,1);assert.equal(settlements,Number(scenario!=='changed-record'));
   assert.equal(invalidations,Number(scenario==='changed-record'));
   assert.equal(calls('game.resume').length,1);assert.equal(calls('game.resume.status').length,0);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);assert.equal(f.events.get('public:power.lifecycleChanged')?.size??0,0);
 }
});
await check('non-Steam pause-record conflict refreshes only the current menu state',async()=>{
 for(const replacement of ['cleared-by-wake','another-game','new-pause-for-current-game']){
  const f=detectionFixture(null,{sharedTransport:true});
  const initial={...f.power,suspendSerial:0};
  f.broadcastPower(initial);f.native=versionedGame(TMNT,10,10);
  const stop=f.detector.subscribeGameStatus(()=>{});let menu;
  const calls=cmd=>f.messages.filter(msg=>msg.cmd===cmd);
  const rootOwner={pid:TMNT.pid,processCreated:TMNT.processCreated};
  const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid,33],processes:[rootOwner,{pid:33,processCreated:'321'}],ts:1});
  let disk=saved;
  try{
   await f.detector.refreshGameStatusStrict();menu=quickMenuControlFixture(f);await flushDetection();assert(menu.paused.value);
   const base=f.pauseFilePort;
   f.pauseFilePort=msg=>{
    const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
    if(msg.cmd==='game.resume'||msg.cmd==='game.pauseRecord.settle')return true;
    if(msg.cmd==='fs.exists'){
     reply(msg.args.path.endsWith('quickapp_suspended.json')&&disk!==null);return true;
    }
    if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
    return base(msg);
   };
   const action=menu.togglePause();await flushDetection();
   const [submitted]=calls('game.resume');assert(submitted);
   f.reply(submitted.id,{resumed:1,failedPids:[TMNT.pid],stalePids:[]});await flushDetection();
   const [oldCommit]=calls('game.pauseRecord.settle');assert(oldCommit?.args.raw);
   f.native=versionedGame(TMNT,11,10);
   f.broadcastPower({...initial,publicationRevision:2,suspendSerial:1,observedNotificationSequence:2});
   f.reply(oldCommit.id,'unavailable');await action;await flushDetection();
   const currentCommit=calls('game.pauseRecord.settle').at(-1);
   assert.notEqual(currentCommit?.id,oldCommit.id);
   assert.deepEqual(currentCommit.args,oldCommit.args,'retry must compare the exact original pause record');
   assert(menu.paused.value,'the new scope observed the old record before native settlement completed');
   const readsBefore=menu.reads.length;
   disk=replacement==='cleared-by-wake'?null:replacement==='another-game'?
    JSON.stringify({root:999,pids:[999],processes:[{pid:999,processCreated:'654'}],ts:2}):
    JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[rootOwner],ts:2});
   const retained=disk;
   f.reply(currentCommit.id,'changed');await flushDetection();
   assert.equal(menu.reads.length,readsBefore+1,replacement+' must trigger one current-menu read');
   assert.equal(menu.paused.value,replacement==='new-pause-for-current-game',replacement);
   f.reply(oldCommit.id,'committed');await flushDetection();
   assert.equal(menu.reads.length,readsBefore+1,'late older settlement cannot refresh the menu again');
   assert.equal(disk,retained);
   assert.equal(calls('game.resume').length,1);
   assert.equal(calls('game.pauseRecord.settle').length,2);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{menu?.close();stop();f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);assert(f.abortRecords.every(r=>r.callbacks.size===0));
 }
});
await check('non-Steam reopened menu can reconcile a committed pause-record deletion after retries exhaust',async()=>{
 for(const action of ['pause','close']){
 const f=detectionFixture(),block=blockDisplayScheduler(f);
 f.broadcastPower({...f.power,suspendSerial:0});f.native=versionedGame(TMNT,10,10);
 const stop=f.detector.subscribeGameStatus(()=>{});
 const calls=cmd=>f.messages.filter(msg=>msg.cmd===cmd);
 const saved=JSON.stringify({root:TMNT.pid,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}]});
 let menu,disk=saved,settlements=0;
 try{
  await f.detector.refreshGameStatusStrict();menu=quickMenuControlFixture(f,{actualSuspend:true});
  f.menuWindow.addEventListener('quickapp:suspended-state',()=>settlements++);
  f.pauseFilePort=msg=>{
   const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
   if(msg.cmd==='fs.exists'){reply(disk!==null);return true;}
   if(msg.cmd==='fs.readTextFile'){reply(disk);return true;}
   if(msg.cmd==='game.resume'){reply({resumed:1,failedPids:[],stalePids:[]});return true;}
   if(msg.cmd==='game.suspend'){reply({paused:false});return true;}
   if(msg.cmd==='game.close'){reply({ok:false,attempted:false,exited:false});return true;}
   if(msg.cmd!=='game.pauseRecord.settle')return false;
   assert.equal(msg.args.expected,saved);assert.equal(msg.args.raw,null);
   if(calls(msg.cmd).length===1)disk=null; // Committed, but the acknowledgement was lost.
   if(calls(msg.cmd).length>4)reply('committed');else if(calls(msg.cmd).length>1)reply('unavailable');
   return true;
  };
  await flushDetection();assert(menu.paused.value);
  const first=menu.togglePause();await flushDetection();await f.advance(5000);await first;
  await f.advance(20000);assert.equal(calls('game.pauseRecord.settle').length,4);
  assert.equal(settlements,0);assert.equal(disk,null);assert.equal(f.timers.size,0);
  menu.props.open=false;await flushDetection();menu.props.open=true;await flushDetection();
  assert(!menu.paused.value,'reopened menu reads the already deleted record');
  const click=action==='pause'?menu.togglePause:()=>{
   const target=f.detector.getCurrentGame();assert(target);
   return menu.closeGame(target.pid,target.name,target.processCreated,target);
  };
  const firstClick=await click();await flushDetection();
  assert.equal(calls('game.pauseRecord.settle').length,5,'manual control must reconcile the same committed deletion');
  assert.equal(calls('game.resume').length,1);assert.equal(calls('game.resume.status').length,0);
  assert.equal(calls('game.suspend').length,0,'the reconciliation click must not suspend');
  assert.equal(calls('game.close').length,0,'the reconciliation click must not close');
  if(action==='close')assert.equal(firstClick?.ok,false,'settlement must not masquerade as a successful close');
  assert.equal(settlements,1);assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  await click();await flushDetection();
  assert.equal(calls(action==='pause'?'game.suspend':'game.close').length,1,'a fresh action can proceed after the old receipt settles');
 }finally{menu?.close();stop();block();f.stopPowerProbe();}
 await flushDetection();assert.equal(f.timers.size,0);assert.equal(f.events.get('public:power.lifecycleChanged')?.size??0,0);
 }
});
await check('non-Steam resume retries a rejected post without querying or replaying it',async()=>{
 for(const original of [null,'unreadable pause record']){
  const f=detectionFixture();f.broadcastPower({...f.power,suspendSerial:0});f.native=versionedGame(TMNT,10,10);
  const stop=f.detector.subscribeGameStatus(()=>{});let menu;
  try{
   await f.detector.refreshGameStatusStrict();menu=quickMenuControlFixture(f);await flushDetection();
   let saved=original,failBeforePost=true;
   const base=f.pauseFilePort;
   f.pauseFilePort=msg=>{
    if(msg.cmd==='game.resume.status')throw Error('a rejected post has no native receipt');
    if(msg.cmd==='game.resume'){
     if(failBeforePost)throw Error('post refused locally');
     assert(Array.isArray(msg.args.pids) && msg.args.pids.length===0);
     menu.setSuspended(false);
     queueMicrotask(()=>f.reply(msg.id,{resumed:1,failedPids:[],stalePids:[]}));return true;
    }
    const inScope=msg.args?.path?.endsWith('quickapp_suspended.json');
    if(msg.cmd==='game.pauseRecord.settle'){
     assert(saved===msg.args.expected||saved===msg.args.raw);saved=msg.args.raw;
     queueMicrotask(()=>f.reply(msg.id,'committed'));return true;
    }
    if(inScope && ['fs.exists','fs.readTextFile'].includes(msg.cmd)){
     let result=true;
     if(msg.cmd==='fs.exists')result=saved!==null;
     if(msg.cmd==='fs.readTextFile')result=saved;
     queueMicrotask(()=>f.reply(msg.id,result));return true;
    }
    return base(msg);
   };
   const resumes=()=>f.messages.filter(msg=>msg.cmd==='game.resume');
   await menu.togglePause();assert(menu.paused.value && !menu.pauseBusy.value);
   assert.equal(resumes().length,1);
   assert.equal(JSON.parse(saved).recoveryPending,true);
   assert.equal(JSON.parse(saved).recoveryOriginalRaw,original??undefined);
   assert.equal(f.messages.some(msg=>msg.cmd==='game.resume.status'),false);
   failBeforePost=false;
   await menu.togglePause();await flushDetection();assert(!menu.paused.value && !menu.pauseBusy.value);
   assert.equal(resumes().length,2);
   assert.notEqual(resumes()[0].args.requestId,resumes()[1].args.requestId);
   assert.equal(saved,null);
   assert.equal(f.events.get('ipc:game.resume.completed')?.size??0,0);
  }finally{menu?.close();stop();f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);
 }
});
await check('non-Steam manual pause releases a lost reply without replaying the command',async()=>{
 for(const outcome of ['lost','late-after-suspend','confirmed']){
  const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);
  const ready={...f.power,suspendSerial:0};
  f.broadcastPower(ready);f.native=versionedGame(TMNT,10,10);
  const stop=f.detector.subscribeGameStatus(()=>{});
  let menu,marker=false,completion=false;
  const calls=cmd=>f.messages.filter(m=>m.cmd===cmd);
  const receipt={paused:true,pids:[TMNT.pid],processes:[{pid:TMNT.pid,processCreated:TMNT.processCreated}],okCount:1,failCount:0};
  try{
   await f.detector.refreshGameStatusStrict();menu=quickMenuControlFixture(f,{actualSuspend:true});
   f.pauseFilePort=msg=>{
    const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
    const isMarker=msg.args.path?.includes('manual-suspended');
    if(msg.cmd==='fs.exists'){reply(isMarker&&marker);return true;}
    if(msg.cmd==='fs.readTextFile'&&isMarker){reply(`pid=${TMNT.pid}|created=${TMNT.processCreated}|state=suspended`);return true;}
    if(msg.cmd==='game.suspend'){marker=true;if(outcome==='confirmed')reply(receipt);return true;}
    if(msg.cmd==='game.resume'){
     assert(marker);marker=false;reply({resumed:1,failedPids:[],stalePids:[]});return true;
    }
    if(msg.cmd==='game.pauseRecord.settle'){reply('committed');return true;}
    if(msg.cmd==='fs.writeTextFileAtomic')return true; // The old renderer can lose a second acknowledgement here.
    return false;
   };
   await flushDetection();assert.equal(menu.paused.value,false);
   const first=menu.togglePause().then(()=>{completion=true;});await flushDetection();
   const [command]=calls('game.suspend');assert(command);
   if(outcome==='late-after-suspend'){
    f.broadcastPower({...ready,phase:'suspended',hardwareWritesAllowed:false,publicationRevision:2,suspendSerial:1});
    await flushDetection();assert(menu.pauseBusy.value,'the native operation keeps its original wait');
    marker=false; // Public recovery has already retired the marker for this case.
    f.broadcastPower({...ready,publicationRevision:3,suspendSerial:1,observedNotificationSequence:2});
    f.reply(command.id,receipt);await flushDetection();
   }
   if(outcome==='lost'){
    await f.advance(7999);assert(menu.pauseBusy.value);
    await f.advance(1);
   }
   assert(completion&&!menu.pauseBusy.value,outcome+' must leave the manual game queue usable');
   await first;await flushDetection();
   assert.equal(calls('fs.writeTextFileAtomic').length,0,'the WebView cannot publish an old pause record');
   assert.equal(menu.paused.value,outcome!=='late-after-suspend');
   if(outcome==='lost'){
    f.reply(command.id,receipt);await flushDetection();
    await menu.togglePause();await flushDetection();
    assert.equal(calls('game.resume').length,1,'the next click can recover the original Native marker');
    assert(!menu.paused.value);
   }
   assert.equal(calls('game.suspend').length,1,'the original pause must never be replayed');
  }finally{menu?.close();stop();block();f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);
 }
});
await check('non-Steam manual resume selects the current marker over an old menu record',async()=>{
 for(const test of ['old-root','reused-pid','no-current-marker','wrong-state','sleep-during-read','lost-reply']){
  const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);
  const ready={...f.power,suspendSerial:0};f.broadcastPower(ready);f.native=versionedGame(TMNT,10,10);
  const stop=f.detector.subscribeGameStatus(()=>{}),calls=cmd=>f.messages.filter(msg=>msg.cmd===cmd);
  const otherPid=test==='old-root'?TMNT.pid+1:TMNT.pid;
  const disk=JSON.stringify({root:otherPid,pids:[otherPid],processes:[{pid:otherPid,processCreated:'1'}]});
  const target={pid:TMNT.pid,processCreated:TMNT.processCreated};
  let menu,marker=true,hold=false,read,requestId;
  const markerText=()=>`pid=${target.pid}|created=${test==='no-current-marker'?'1':target.processCreated}|state=${test==='wrong-state'?'pending':'suspended'}`;
  const result={resumed:1,failedPids:[],stalePids:[]};
  try{
   await f.detector.refreshGameStatusStrict();menu=quickMenuControlFixture(f,{actualSuspend:true});
   f.pauseFilePort=msg=>{
    const reply=value=>queueMicrotask(()=>f.reply(msg.id,value));
    const isMarker=msg.args.path?.endsWith(`manual-suspended\\${target.pid}.txt`);
    if(msg.cmd==='fs.exists'){reply(!isMarker||marker);return true;}
    if(msg.cmd==='fs.readTextFile'){
     if(isMarker&&hold)read=msg;else reply(isMarker?markerText():disk);
     return true;
    }
    if(msg.cmd==='game.resume'){
     assert(marker);
     requestId=msg.args.requestId;marker=false;
     if(test!=='lost-reply')reply(result);
     return true;
    }
    if(msg.cmd==='game.resume.status'){assert.equal(msg.args.requestId,requestId);reply({requestId,state:'completed',result});return true;}
    if(msg.cmd==='game.pauseRecord.settle')throw Error('must preserve the unrelated menu record');
    return false;
   };
   await flushDetection();
   if(test==='no-current-marker'||test==='wrong-state'){
    assert.equal(menu.paused.value,false,`${test} must not show Continue`);
    const stale=await f.loadGameControl().resumeGame(undefined,f.detector.getCurrentGame());
    assert.equal(stale.ok,false);assert.equal(calls('game.resume').length,0);continue;
   }
   assert.equal(menu.paused.value,true,`${test}: old menu record must not mask the exact current pause marker`);
   hold=test==='sleep-during-read';
   const click=menu.togglePause();await flushDetection();
   if(hold){
    assert(read,'click is waiting for the marker read');
    f.broadcastPower({...ready,phase:'suspended',hardwareWritesAllowed:false,publicationRevision:2,suspendSerial:1,observedNotificationSequence:2});
    await flushDetection();hold=false;
    f.reply(read.id,markerText());
    await click;await flushDetection();
    assert.equal(calls('game.resume').length,0,'a newer sleep retires the preflight before native dispatch');
    f.broadcastPower({...ready,publicationRevision:3,suspendSerial:1,observedNotificationSequence:3});
    await flushDetection();await menu.togglePause();
   }
   if(test==='lost-reply'){
    assert.equal(calls('game.resume').length,1);assert(menu.pauseBusy.value);
    await f.advance(8000);
    assert.equal(calls('game.resume.status').length,1);
   }
   await click;await flushDetection();
   assert.equal(calls('game.resume').length,1);assert.equal(calls('game.suspend').length,0);
   assert.deepEqual(calls('game.resume')[0].args.pids,[target]);
   assert.equal(calls('game.pauseRecord.settle').length,0);
   assert.equal(menu.pauseBusy.value,false,`${test}: the menu queue must be available`);
   assert.equal(menu.paused.value,false,`${test}: the original marker must be settled`);
  }finally{menu?.close();stop();block();f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);
 }
});
await check('non-Steam close preserves the pause record until follow-up recovery settles',async()=>{
 for(const outcome of ['busy','partial','complete']){
  const f=detectionFixture();f.broadcastPower({...f.power,suspendSerial:0});f.native=versionedGame(TMNT,10,10);
  const stop=f.detector.subscribeGameStatus(()=>{});let cleared=0;
  try{
   await f.detector.refreshGameStatusStrict();const target=f.detector.getCurrentGame();
   const game=f.loadGameControl();const processes=[{pid:target.pid,processCreated:target.processCreated}];
   f.menuWindow.addEventListener('quickapp:suspended-state',()=>{cleared++;});
   let recoveryReturned=false;
   f.pauseFilePort=msg=>{
    if(msg.cmd==='game.detect')return false;
    const values={'game.close':{ok:true,attempted:true,exited:true},'fs.exists':true,
      'fs.readTextFile':JSON.stringify({root:target.pid,processes}),'game.pauseRecord.settle':'committed',
      'game.resume':{resumed:0,failedPids:outcome==='partial'?[target.pid]:[],stalePids:outcome==='complete'?[target.pid]:[]}};
    assert(Object.hasOwn(values,msg.cmd),msg.cmd);
    queueMicrotask(()=>{
     if(msg.cmd==='game.resume')recoveryReturned=true;
     if(msg.cmd==='game.resume'&&outcome==='busy')f.fail(msg.id,'fixture recovery busy');
     else f.reply(msg.id,values[msg.cmd]);
    });
    return true;
   };
   const result=await game.closeGame(target.pid,target.name,target.processCreated,target);assert(result.ok,outcome);
   const requests=f.messages.filter(msg=>msg.cmd==='game.resume');
   assert.equal(requests.length,1,outcome);assert.deepEqual(requests[0].args.pids,processes);assert(recoveryReturned,outcome);
   assert.equal(f.messages.filter(msg=>msg.cmd==='game.pauseRecord.settle').length,Number(outcome==='complete'),outcome);
   assert.equal(cleared,Number(outcome==='complete'),outcome);
  }finally{stop();f.stopPowerProbe();}
  await flushDetection();assert.equal(f.timers.size,0);
 }
});
await check('Xbox隐藏且共享调度器等待其他任务时，原生目标通知主动更新顶部与策略缓存',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f),seen=[];const stop=f.detector.subscribeGameStatus(g=>seen.push(g));await f.policy.detectPolicyGame();assert.equal(f.policy.getPolicyGame(),null);
 f.native={...TMNT};f.signal('ipc:game.target.changed',{pid:999999,path:'untrusted event data'});await flushDetection();assert(seen.at(-1),'a committed native target must exist before checking its PID');assert.equal(seen.at(-1).pid,TMNT.pid);assert.equal(f.policy.getPolicyGame().pid,TMNT.pid);assert(f.messages.every(m=>m.cmd==='game.detect'&&!m.args.pid));stop();block();
});
await check('没有原生推送的旧壳也继续按1250ms后台发现游戏',async()=>{
 const f=detectionFixture(),seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g));await flushDetection();f.native={...TMNT};await f.advance(1250);assert.equal(seen.at(-1).pid,TMNT.pid);stop();assert.equal(f.timers.size,0);
});
await check('退出Xbox全屏恢复窗口或文档时自动校对，用户无需手动刷新',async()=>{
 for(const event of ['ipc:window.shown','ipc:window.restored','ipc:window.summoned','ipc:window.focus','ipc:power.resumed','document-visible']){
  const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f),seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g));
  try{
   await flushDetection();f.native={...TMNT};
   if(event==='document-visible')f.show();
   else if(event==='ipc:power.resumed'){
    f.power={...f.power,generation:101,publicationRevision:2,observedNotificationSequence:2};
    f.emitNative('power.resumed',{generation:101});
   }else f.signal(event);
   await flushDetection();assert.equal(seen.at(-1).pid,TMNT.pid,event);
  }finally{stop();block();f.stopPowerProbe();}
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
 const f=detectionFixture(),block=blockDisplayScheduler(f);await f.policy.detectPolicyGame();let host,replySequence=0;
 const waiting=new Set();
 const stopPolicy=f.policy.subscribePolicyGameStatus(g=>{if(host){host.state.game=g;host.gameChanged();}});
 const waitSnapshot=(stage,action,accept)=>new Promise((resolve,reject)=>{
  const after=replySequence;
  const waiter={after,accept,finish(reply,error){clearTimeout(timer);waiting.delete(waiter);if(error)reject(error);else resolve(reply);}};
  const timer=setTimeout(()=>waiter.finish(null,new assert.AssertionError({message:'actual host snapshot completion missing: '+stage,
   actual:{stage,replies:host?.replies.length??0,diagnostics:host?.diagnostics??[],counts:host?.counts??{},detectorRequests:f.messages.map(row=>({id:row.id,cmd:row.cmd})),latest:host?.replies.at(-1)?.message},expected:'new actual snapshot after this operation'})),1000);
  waiting.add(waiter);
  try{Promise.resolve(action()).catch(error=>waiter.finish(null,error));}catch(error){waiter.finish(null,error);}
 });
 try{
  host=await createMirrorHostFixture({policyRefresh:()=>f.policy.refreshPolicyGameStrict(),policyReadiness:f.detector,invoke:async(command,args)=>{
   if(command==='deckySidebar.mirrorAttach')return {connected:false};
   if(command!=='deckySidebar.mirrorReply')throw Error('Unexpected completion-fixture IPC: '+command);
   const sequence=++replySequence;
   for(const waiter of [...waiting]){
    if(sequence<=waiter.after||args.message?.type!=='snapshot')continue;
    try{if(waiter.accept(args.message.snapshot))waiter.finish(args.message.snapshot);}catch(error){waiter.finish(null,error);}
   }
   return true;
  }});host.state.game=null;host.ready();
  const runId='c'.repeat(32),peer={connection:'1',runId,connected:true};
  f.native={...TMNT};let latest=await waitSnapshot('initial shared target',()=>host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}}),snapshot=>snapshot.ready===true&&snapshot.game!==null);
  assert.equal(latest.game.label,TMNT.title);assert(latest.game.identity.includes(String(TMNT.pid)));assert.equal(host.counts.gameReconciles,1);
  const next={...TMNT,pid:21003,processCreated:'134372974230246500',title:'Next inert game',path:'D:/Inert/Next.exe'};f.native=next;
  latest=await waitSnapshot('native target change',()=>f.signal('ipc:game.target.changed'),snapshot=>snapshot.game?.identity.includes(String(next.pid)));
  assert.equal(latest.game.label,next.title);assert(latest.game.identity.includes(String(next.pid)));assert.equal(host.counts.gameReconciles,1);assert.equal(host.counts.saves,0);
  const identity=latest.game.identity;f.hold=true;
  latest=await waitSnapshot('shared target outage',async()=>{const fault=f.detector.refreshGameStatusStrict(),rejected=assert.rejects(fault);await flushDetection();f.fail(f.messages.at(-1).id,'inert actual shared-target outage');await rejected;},snapshot=>snapshot.actions?.game===false);
  assert.equal(latest.actions.game,false);assert.equal(latest.game.identity,identity);
  f.hold=false;latest=await waitSnapshot('shared target recovery',()=>f.advance(500),snapshot=>snapshot.actions?.game===true);
  assert.equal(latest.actions.game,true);assert.equal(latest.game.identity,identity);assert.equal(host.counts.gameReconciles,1);assert.equal(host.counts.saves,0);host.stop();assert.equal(host.readinessSubscriberCount(),0);
 }finally{
  for(const waiter of [...waiting])waiter.finish(null,Error('Host snapshot fixture ended before completion'));
  host?.stop();stopPolicy();block();
 }
});

await check('反向去掉IPC超时会留下永久等待，后续自动轮询无法再发现游戏',async()=>{
 const source=fs.readFileSync(path.join(root,'src/bridge/gamedetect.ts'),'utf8');const timeoutOptions=', { timeoutMs: DETECTION_TIMEOUT_MS, signal: owner.cancellation.signal }';assert.equal(source.split(timeoutOptions).length,2);
 const f=detectionFixture(source.replace(timeoutOptions,', { signal: owner.cancellation.signal }')),seen=[];f.hold=true;const stop=f.detector.subscribeGameStatus(g=>seen.push(g));await flushDetection();f.native={...TMNT};await f.advance(20000);assert.equal(f.messages.length,1);assert.equal(seen.at(-1),null);stop();
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



await check('旧轮游戏校验不等待新轮显式选择的回包',async()=>{
 const outcomes=[];
 for(const boundary of ['before-observe','during-admit','same-scope']){
  const f=detectionFixture(null,{sharedTransport:true});
  const initial={...f.power,suspendSerial:0};f.broadcastPower(initial);f.native=versionedGame(TMNT,10,10);
  await f.detector.refreshGameStatusStrict();
  const session=load('src/bridge/gameQuickSession.ts',{'./gamedetect':f.detector});
  const locked=session.lockGameTarget(f.detector.getCurrentGame());
  const start=f.messages.length; f.hold=true;
  let done=false,selection;
  const old=session.validateLockedGameTarget().then(target=>{done=true;return target;});
  const replace=()=>{
   f.broadcastPower({...initial,publicationRevision:2,
    suspendSerial:boundary==='same-scope'?0:1,observedNotificationSequence:boundary==='same-scope'?1:2});
   selection=f.detector.refreshGameStatusStrict(TMNT.pid);
  };
  if(boundary==='before-observe')replace();else queueMicrotask(replace);
  await flushDetection();
  assert(selection);assert.equal(f.messages.length,start+1);
  const request=f.messages.at(-1);
  assert.equal(request.args.pid,TMNT.pid);
  assert.equal(request.args.expectedPowerSuspendSerial,boundary==='same-scope'?0:1);
  // Keep the independent new selection pending while checking the old waiter.
  const doneBeforeReply=done;
  f.native=versionedGame(TMNT,11,10);
  f.reply(request.id,f.native);
  const result=await old;
  assert((await selection)?.pid===TMNT.pid);
  outcomes.push({boundary,doneBeforeReply,result:result?.lockGeneration??null});
  assert.equal(session.getLockedGameTarget()?.lockGeneration,locked.lockGeneration);
  f.hold=false;
  assert.equal((await session.validateLockedGameTarget())?.lockGeneration,locked.lockGeneration);
  f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);
 }
 assert.deepEqual(outcomes,[
  {boundary:'before-observe',doneBeforeReply:true,result:null},
  {boundary:'during-admit',doneBeforeReply:true,result:null},
  {boundary:'same-scope',doneBeforeReply:false,result:1},
 ]);
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
  const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const stop=f.detector.subscribeGameStatus(()=>{});f.hold=true;
  f.signal('ipc:game.target.changed',{generation:11});await flushDetection();f.fail(f.messages.at(-1).id,'inert outage');await flushDetection();const start=f.messages.length;
  if(cancel){stop();assert.equal(f.timers.size,0,'last unsubscribe must cancel the owned recovery timer immediately');await f.advance(30000);assert.equal(f.messages.length,start);block();f.stopPowerProbe();assert.equal(f.timers.size,0);continue;}
  for(const delay of [500,1500,4000]){await f.advance(delay-1);assert.equal(f.messages.length,start+[500,1500,4000].indexOf(delay));await f.advance(1);assert.equal(f.messages.length,start+[500,1500,4000].indexOf(delay)+1);f.fail(f.messages.at(-1).id,'inert outage');await flushDetection();}
  assert.equal(f.messages.length,start+3);await f.advance(60000);assert.equal(f.messages.length,start+3,'exhausted recovery must not become a second periodic poller');assert(!f.detector.isGameStatusCurrent());
  f.native=versionedGame(NEXT_GAME,11);f.hold=false;let confirmReady;
  f.powerReadHook=state=>new Promise(resolve=>{confirmReady=()=>resolve(state);});
  try{
   f.emitNative('power.resumed',{generation:f.power.generation});await flushDetection();
   assert.equal(typeof confirmReady,'function');assert.equal(f.messages.length,start+3,'unconfirmed wake started a game scan');
   confirmReady();await flushDetection();assert.equal(f.messages.length,start+4,'same-scope confirmation must allow exactly one recovery scan');
   assert.equal(f.detector.getCurrentGame()?.pid,NEXT_GAME.pid);assert(f.detector.isGameStatusCurrent());
  }finally{confirmReady?.();stop();block();f.stopPowerProbe();assert.equal(f.timers.size,0);}
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


await check('公共睡眠：仅订阅目标准入时也能接续识别，且不启动轮询',async()=>{
 for(const nextGeneration of [100,101]){
  const f=detectionFixture(),ready=[];f.native=versionedGame();
  const stop=f.detector.subscribeGameTargetReadiness(value=>ready.push(value));
  try{
   await flushDetection();assert.equal(f.messages.length,0,'readiness alone must not start cold discovery');
   await f.detector.refreshGameStatusStrict();assert(f.detector.isGameStatusCurrent());
   f.hold=true;const old=f.detector.refreshGameStatusStrict(TMNT.pid),rejected=assert.rejects(old);await flushDetection();const message=f.messages.at(-1),before=f.messages.length;
   f.broadcastPower({...f.power,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});
   await rejected;assert.equal(f.messages.length,before);assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid);assert(!f.detector.isGameStatusCurrent());
   f.native=versionedGame(TMNT,11,10);f.hold=false;
   f.broadcastPower({...f.power,generation:nextGeneration,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});
   await flushDetection();await f.advance(100);
   assert.equal(f.messages.length,before+1,'only readiness observers still require a successor read after public Ready');
   assert.deepEqual({...f.messages.at(-1).args},{automatic:true,expectedPowerGeneration:nextGeneration,expectedPowerNotificationSequence:2});
   assert(f.detector.isGameStatusCurrent());assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid);assert.deepEqual(ready,[false,true,false,true]);
   f.reply(message.id,versionedGame(NEXT_GAME,12));await flushDetection();
   assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid,'retired preferred target must not replace the successor');
   await f.advance(10000);assert.equal(f.messages.length,before+1);assert.equal(f.schedules.length,0);
  }finally{stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);}
 }
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
  const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const stop=f.detector.subscribeGameStatus(()=>{});
  for(let i=0;i<200;i++){
   const action=next()%6;
   if(action===0||action===3){generation++;identity++;f.native=versionedGame(next()%2?TMNT:NEXT_GAME,generation,identity);if(action===0)f.signal('ipc:game.target.changed',{generation});else assert.equal((await f.detector.refreshGameStatusStrict(f.native.pid))?.pid,f.native.pid);}
   else if(action===1)f.signal('ipc:game.target.changed',{generation:Math.max(1,generation-1)});
   else if(action===2||action===5){f.hold=true;const read=f.detector.refreshGameStatusStrict(),rejected=assert.rejects(read);await flushDetection();const request=f.messages.at(-1);if(action===2)f.fail(request.id,'inert seeded outage');else f.reply(request.id,{...f.native,processCreated:'invalid-birth'});await rejected;f.hold=false;assert(!f.detector.isGameStatusCurrent());}
   else await f.advance(next()%151);
   await flushDetection();
   if(f.detector.isGameStatusCurrent()){const actual=f.detector.getCurrentGame();assert.equal(actual?.pid,f.native.pid,'seed '+seed+' step '+i);assert.equal(actual?.identityGeneration,f.native.identityGeneration);}
  }
  f.emitNative('power.resumed',{generation:f.power.generation});await f.advance(4500);assert(f.detector.isGameStatusCurrent(),'seed '+seed+' must recover');assert.equal(f.detector.getCurrentGame()?.pid,f.native.pid);assert.equal(f.detector.getCurrentGame()?.identityGeneration,f.native.identityGeneration);assert.equal(f.schedules.filter(x=>x.name==='game-status').length,1);assert(f.messages.length<=400,'mixed failures must keep a bounded read budget');stop();block();f.stopPowerProbe();assert.equal(f.timers.size,0);
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
  const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);f.native=versionedGame();await f.detector.refreshGameStatusStrict();const stop=f.detector.subscribeGameStatus(()=>{});f.hold=true;
  f.signal('ipc:game.target.changed',{generation:11});await flushDetection();const count=f.messages.length,pending=f.messages.at(-1);
  const retained=foreground?f.detector.refreshGameStatusStrict().then(game=>({game}),error=>({error})):null;stop();
  if(replyKind!=='none'){
   f.reply(pending.id,replyKind==='fresh'?versionedGame(NEXT_GAME,11):versionedGame());await flushDetection();
   if(foreground){assert.equal(f.messages.length,count+1,'a foreground action retains its shared observation after UI unsubscribe');f.reply(f.messages.at(-1).id,versionedGame(NEXT_GAME,11));const result=await retained;assert(!result.error,'a foreground reader must not be cancelled with the background observer');assert.equal(result.game?.pid,NEXT_GAME.pid);}
   else{assert.equal(f.messages.length,count,'an unobserved background request must not start another stale-snapshot read');assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid,'a background return after stop must not publish a new target');}
  }
  const expected=count+(foreground?1:0);await f.advance(8000);assert.equal(f.timers.size,0);assert.equal(f.messages.length,expected);
  f.reply(pending.id,versionedGame(NEXT_GAME,11));f.emitNative('power.resumed',{generation:f.power.generation});await f.advance(10000);assert.equal(f.timers.size,0);assert.equal(f.messages.length,expected);assert.equal(f.detector.getCurrentGame()?.pid,foreground?NEXT_GAME.pid:TMNT.pid);block();f.stopPowerProbe();
 }
});
await check('公共睡眠撤销立即退还游戏识别准入且旧显示不冒退出',async()=>{
 const f=detectionFixture();f.native=versionedGame();const seen=[],ready=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g)),stopReady=f.detector.subscribeGameTargetReadiness(v=>ready.push(v));await flushDetection();assert(f.detector.isGameStatusCurrent());const before=f.messages.length,current=f.detector.getCurrentGame();
 f.broadcastPower({...f.power,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});await flushDetection();assert.equal(f.detector.isGameStatusCurrent(),false,'public sleep denial left old game readiness admitted');assert.equal(ready.at(-1),false);assert.equal(f.detector.getCurrentGame().pid,current.pid);await f.advance(5000);assert.equal(f.messages.length,before,'held public sleep emitted game discovery IO');
 f.broadcastPower({...f.power,generation:101,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});await flushDetection();await f.advance(1250);assert(f.detector.isGameStatusCurrent());assert.equal(seen.filter(g=>g===null).length,1,'sleep was published as a game exit');stop();stopReady();f.stopPowerProbe();assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);
});
await check('非 Steam：第一次 Ready 被下一次睡眠打断后继续识别同一游戏',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f),seen=[],ready=[];
 f.native=versionedGame(TMNT,10,10);
 const stop=f.policy.subscribePolicyGameStatus(game=>seen.push(game));
 const stopReady=f.detector.subscribeGameTargetReadiness(value=>ready.push(value));
 try{
  await flushDetection();
  assert.equal(f.policy.getPolicyGame()?.source,'whitelist');
  assert.equal(f.policy.getPolicyGame()?.pid,TMNT.pid);
  assert.equal(f.messages.length,1);
  f.broadcastPower({...f.power,phase:'suspending',hardwareWritesAllowed:false,notificationPending:true,publicationRevision:2,observedNotificationSequence:2});
  await f.advance(5000);
  assert.equal(f.messages.length,1);
  f.hold=true;
  f.broadcastPower({...f.power,phase:'ready',generation:101,hardwareWritesAllowed:true,notificationPending:false,publicationRevision:3});
  await f.advance(200);
  assert.equal(f.messages.length,2);
  const interrupted=f.messages.at(-1);
  assert.equal(interrupted.args.expectedPowerGeneration,101);
  f.broadcastPower({...f.power,phase:'suspending',hardwareWritesAllowed:false,notificationPending:true,publicationRevision:4,observedNotificationSequence:3});
  f.reply(interrupted.id,noGame(11,11));
  await f.advance(5000);
  assert.equal(f.messages.length,2);
  assert.equal(f.policy.getPolicyGame()?.pid,TMNT.pid,'retired empty reply became a game exit');
  assert.equal(f.detector.isGameStatusCurrent(),false);
  f.hold=false;
  f.native=versionedGame(TMNT,12,10);
  f.broadcastPower({...f.power,phase:'ready',generation:102,hardwareWritesAllowed:true,notificationPending:false,publicationRevision:5});
  await f.advance(200);
  assert.equal(f.messages.length,3,'second Ready must refresh the selected game once');
  assert.deepEqual({...f.messages.at(-1).args},{automatic:true,expectedPowerGeneration:102,expectedPowerNotificationSequence:3});
  assert.equal(f.policy.getPolicyGame()?.pid,TMNT.pid);
  assert.equal(f.policy.getPolicyGame()?.processCreated,TMNT.processCreated);
  assert(f.detector.isGameStatusCurrent());
  assert.deepEqual(ready,[false,true,false,true]);
  assert.equal(seen.filter(game=>game===null).length,0,'power hold was reported as a game exit');
 }finally{stopReady();stop();block();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);}
});
await check('游戏关联读取跟随公共目标确认，迟到的旧电源通知不取消新读取',async()=>{
 const f=detectionFixture(null,{sharedTransport:true}),reader=f.loadMetadata();let old,newRead;
 const reads=()=>f.messages.filter(message=>message.cmd==='gameSteam.context.get');
 try{
  f.native=versionedGame(TMNT,10,10);await f.detector.refreshGameStatusStrict();
  old=reader.read();await flushDetection();assert.equal(reads().length,1);
  const oldId=reads()[0].id,oldSignal=f.abortRecords.at(-1).signal;
  const suspended={...f.power,phase:'suspended',hardwareWritesAllowed:false,inputReady:false,publicationRevision:2,observedNotificationSequence:2};
  f.broadcastPower(suspended);await flushDetection();
  assert(oldSignal.aborted);assert.equal((await old).game,null);assert.equal(f.detector.isGameStatusCurrent(),false);assert.equal(reads().length,1);
  f.native=versionedGame(TMNT,11,10);
  f.broadcastPower({...suspended,phase:'ready',generation:101,hardwareWritesAllowed:true,inputReady:true,publicationRevision:3});
  await f.advance(300);assert(f.detector.isGameStatusCurrent());
  newRead=reader.read();await flushDetection();assert.equal(reads().length,2);
  const fresh=reads()[1],freshSignal=f.abortRecords.at(-1).signal;
  f.emitNative('power.resuming',{generation:100});
  f.emitNative('power.lifecycleChanged',suspended);
  f.emitNative('power.resumed',{generation:100});
  f.reply(oldId,null);await flushDetection();
  assert(f.detector.isGameStatusCurrent(),'old notices changed the accepted target');
  assert.equal(freshSignal.aborted,false,'late old power notice aborted a current context read');
  const game={...fresh.args.expectedGame,rulesEpoch:'1',powerGeneration:'101',processValidated:false};
  f.reply(fresh.id,{schemaVersion:1,game,steam:{status:'unknown',reason:'no-association',processValidated:false,currentSource:false}});
  const result=await newRead;assert.equal(result.game?.pid,TMNT.pid);assert.equal(result.game?.powerGeneration,'101');assert.equal(result.steam.reason,'no-association');
  assert.equal(reads().length,2,'power observations must not request metadata themselves');
 }finally{reader.dispose();await Promise.allSettled([old,newRead]);f.stopPowerProbe();await flushDetection();assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);assert.equal(f.timers.size,0);}
});
await check('旧唤醒和重复Ready不触发已确认游戏的额外扫描',async()=>{
 const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);
 f.native=versionedGame(TMNT,10,10);await f.detector.refreshGameStatusStrict();
 const stop=f.detector.subscribeGameStatus(()=>{});
 try{
  const oldGeneration=f.power.generation;
  f.broadcastPower({...f.power,generation:oldGeneration+1,publicationRevision:2,observedNotificationSequence:2});
  await f.advance(200);assert(f.detector.isGameStatusCurrent());
  const calls=f.messages.length;
  for(const generation of [oldGeneration,oldGeneration+1,oldGeneration+1]){
   f.emitNative('power.resumed',{generation});await f.advance(110);
   assert(f.detector.isGameStatusCurrent());
   assert.equal(f.messages.length,calls,'compatibility wake caused another scan of a confirmed target');
  }
 }finally{stop();block();f.stopPowerProbe();assert.equal(f.timers.size,0);}
});
await check('公共新Ready不等旧识别读取且迟到成功失败不能污染后继',async()=>{
 for(const strict of [false,true]){const f=detectionFixture();f.native=versionedGame();const stop=f.detector.subscribeGameStatus(()=>{});await flushDetection();f.hold=true;const old=f.detector.refreshGameStatusStrict(strict?TMNT.pid:0);old.catch(()=>{});await flushDetection();const message=f.messages.at(-1),before=f.messages.length;
 f.broadcastPower({...f.power,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});f.native=versionedGame({...TMNT,pid:22222,processCreated:'2222200',path:'C:/new.exe'},12,12);f.hold=false;f.broadcastPower({...f.power,generation:101,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});await flushDetection();await f.advance(1250);assert(f.messages.length>before,'new Ready joined the old detection request');assert.equal(f.messages.at(-1).args.pid,undefined,'wake replayed a retired preferred PID');assert.equal(f.messages.at(-1).args.expectedPowerGeneration,101);assert.equal(f.messages.at(-1).args.expectedPowerNotificationSequence,2);assert.equal(f.detector.getCurrentGame().pid,22222);assert(f.detector.isGameStatusCurrent());f.reply(message.id,versionedGame(TMNT,10,10));await assert.rejects(old);await flushDetection();assert.equal(f.detector.getCurrentGame().pid,22222);assert(f.detector.isGameStatusCurrent());f.fail(message.id,'old failure');await flushDetection();assert(f.detector.isGameStatusCurrent());stop();f.stopPowerProbe();assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);}
});
await check('公共同代通知序号变化退休旧识别且拒绝缓存准入',async()=>{
 const f=detectionFixture();f.native=versionedGame();const stop=f.detector.subscribeGameStatus(()=>{});await flushDetection();f.hold=true;const old=f.detector.refreshGameStatusStrict();old.catch(()=>{});await flushDetection();const message=f.messages.at(-1),before=f.messages.length;
 f.broadcastPower({...f.power,publicationRevision:2,observedNotificationSequence:2});assert.equal(f.detector.isGameStatusCurrent(),false,'new sequence retained cached game admission');f.hold=false;f.native=versionedGame(TMNT,11,10);await f.detector.refreshGameStatusStrict();assert(f.messages.length>before,'same generation sequence joined old detection');assert(f.detector.isGameStatusCurrent());f.fail(message.id,'old retired read');await assert.rejects(old);assert(f.detector.isGameStatusCurrent());stop();f.stopPowerProbe();assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);
});

await check('game detection replaces an interrupted read on coalesced Ready',async()=>{
 const f=detectionFixture(),block=blockDisplayScheduler(f),seen=[],ready=[];
 f.broadcastPower({...f.power,suspendSerial:0});f.native=versionedGame(TMNT,10,10);
 const stop=f.policy.subscribePolicyGameStatus(game=>seen.push(game));
 const stopReady=f.detector.subscribeGameTargetReadiness(value=>ready.push(value));
 try{
  await flushDetection();assert(f.detector.isGameStatusCurrent());
  assert.equal(seen.at(-1)?.pid,TMNT.pid);
  const identityPublications=seen.length;
  f.hold=true;
  const old=f.detector.refreshGameStatusStrict(),rejected=assert.rejects(old);
  await flushDetection();const pending=f.messages.at(-1),before=f.messages.length;
  assert(f.detector.isGameStatusCurrent(),'ordinary refresh should preserve the last confirmed game before sleep');
  const previous={...f.power};
  f.hold=false;f.native=versionedGame(TMNT,12,10);
  f.broadcastPower({...previous,publicationRevision:2,suspendSerial:1});
  assert.equal(f.detector.isGameStatusCurrent(),false,'coalesced Ready retained the interrupted game observation');
  assert.equal(f.policy.getPolicyGame()?.pid,TMNT.pid);
  assert.equal(pending.args.expectedPowerSuspendSerial,0);
  await f.advance(1250);
  assert.equal(f.messages.length,before+1,'new Ready did not run exactly one successor read');
  assert.equal(f.messages.at(-1).args.expectedPowerSuspendSerial,1);
  assert.equal(f.messages.at(-1).args.automatic,true,'new Ready did not recover through the background reader');
  assert(f.detector.isGameStatusCurrent());
  f.reply(pending.id,noGame(11,11));await rejected;
  f.broadcastPower({...previous,publicationRevision:3});
  await flushDetection();
  assert(f.detector.isGameStatusCurrent());
  assert.equal(f.messages.length,before+1);
  assert.deepEqual(ready,[false,true,false,true]);
  assert.equal(seen.length,identityPublications,'interrupted read changed the published game identity');
  assert.equal(f.policy.getPolicyGame()?.pid,TMNT.pid);
 }finally{stopReady();stop();block();f.stopPowerProbe();}
});

await check('late native game target notification recovers readiness after coalesced Ready without polling',async()=>{
 const f=detectionFixture(null,{sharedTransport:true});
 f.broadcastPower({...f.power,suspendSerial:0});f.native=versionedGame(TMNT,10,10);
 await f.detector.refreshGameStatusStrict();
 const readiness=[],stop=f.detector.subscribeGameTargetReadiness(value=>readiness.push(value));
 try{
   const beforeReady=f.messages.length;
   f.native=versionedGame(TMNT,11,10);
   f.broadcastPower({...f.power,publicationRevision:2,observedNotificationSequence:2,suspendSerial:1});
   await f.advance(100);
   assert.equal(f.messages.length,beforeReady+1);
   assert(f.detector.isGameStatusCurrent());

   const beforeHint=f.messages.length,hint={generation:12,pid:NEXT_GAME.pid,path:NEXT_GAME.path};
   f.hold=true;
   f.emitNative('game.target.changed',hint);
   f.emitNative('game.target.changed',hint);
   assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid,'unconfirmed hint must preserve the displayed target');
   assert.equal(f.detector.isGameStatusCurrent(),false,'the hint cannot grant readiness by itself');
   await f.advance(100);
   assert.equal(f.messages.length,beforeHint+1,'late native notification must schedule one shared recheck without a poll');
   const request=f.messages.at(-1);
   assert.equal(request.cmd,'game.detect');
   assert.deepEqual({...request.args},{automatic:true,expectedPowerGeneration:100,expectedPowerNotificationSequence:2,expectedPowerSuspendSerial:1});
   assert.equal(f.detector.isGameStatusCurrent(),false);
   f.reply(request.id,versionedGame(TMNT,12,10));
   await flushDetection();
   assert.deepEqual(readiness,[true,false,true,false,true]);
   assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid);
   f.emitNative('game.target.changed',hint);
   await f.advance(10000);
   assert.equal(f.messages.length,beforeHint+1,'confirmed generation must not trigger another read');
   assert.equal(f.schedules.length,0);
 }finally{stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);}
});

await check('game detection rereads shared power after a native denial without a wake event',async()=>{
 const f=detectionFixture(null,{sharedTransport:true});
 const initial={...f.power,suspendSerial:1};
 f.broadcastPower(initial);f.native=versionedGame(TMNT);
 await f.detector.refreshGameStatusStrict();
 const readiness=[],stop=f.detector.subscribeGameTargetReadiness(value=>readiness.push(value));
 try{
  assert.equal(f.scheduler.hasScheduledTask('game-status'),false);
  const before=f.sharedPower.getSharedPowerLifecycleDiagnostics().counters.readStarted;
  const suspended={...initial,phase:'suspended',generation:101,suspendSerial:2,inputReady:false,
    hardwareWritesAllowed:false,notificationPending:true,publicationRevision:2,observedNotificationSequence:2};
  f.power=suspended;f.hold=true;
  const lost=f.detector.refreshGameStatusStrict();await flushDetection();
  const denied=f.messages.at(-1);assert.equal(denied.args.expectedPowerSuspendSerial,1);
  f.fail(denied.id,'GAME_TARGET_POWER_ADMISSION_UNAVAILABLE');await assert.rejects(lost,/GAME_TARGET_POWER_ADMISSION_UNAVAILABLE/);
  await flushDetection();
  assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().counters.readStarted,before+1,'native denial must request an authoritative public read');
  assert.equal(f.sharedPower.getSharedPowerLifecycle()?.phase,'suspended');
  assert.equal(f.detector.getCurrentGame()?.source,'whitelist');
  assert.deepEqual(readiness,[true,false]);
  f.emitNative('power.lifecycleChanged',initial);await flushDetection();
  assert.equal(f.sharedPower.getSharedPowerLifecycle()?.generation,101,'late old Ready cannot reopen the gate');
  const retryAt=f.messages.length;
  f.power={...suspended,phase:'ready',inputReady:true,hardwareWritesAllowed:true,notificationPending:false,publicationRevision:3,observedNotificationSequence:3};
  f.hold=false;
  await f.advance(999);assert.equal(f.messages.length,retryAt);
  await f.advance(1);assert.equal(f.messages.length,retryAt+1);
  const resumed=f.messages.at(-1);
  assert.equal(resumed.args.automatic,true);assert.equal(resumed.args.pid,undefined);
  assert.equal(resumed.args.expectedPowerGeneration,101);assert.equal(resumed.args.expectedPowerSuspendSerial,2);
  assert.equal(resumed.args.expectedPowerNotificationSequence,3);
  assert.equal(f.detector.getCurrentGame()?.processCreated,TMNT.processCreated);
  assert(f.detector.isGameStatusCurrent());assert.deepEqual(readiness,[true,false,true]);
 }finally{stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);}
});

await check('game detection deduplicates native power rejection without resetting its retry budget',async()=>{
 const f=detectionFixture(null,{sharedTransport:true});
 f.native=versionedGame(TMNT);await f.detector.refreshGameStatusStrict();
 const stop=f.detector.subscribeGameTargetReadiness(()=>{});
 try{
  const before=f.sharedPower.getSharedPowerLifecycleDiagnostics().counters.readStarted;
  const first=f.messages.length;f.hold=true;
  const trigger=f.detector.refreshGameStatusStrict();await flushDetection();
  f.fail(f.messages.at(-1).id,'GAME_TARGET_POWER_ADMISSION_EXPIRED');await assert.rejects(trigger,/GAME_TARGET_POWER_ADMISSION_EXPIRED/);
  await flushDetection();
  assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().counters.readStarted,before+1);
  for(const [i,delay] of [500,1500,4000].entries()){
   await f.advance(delay-1);assert.equal(f.messages.length,first+i+1);
   await f.advance(1);assert.equal(f.messages.length,first+i+2);
   f.fail(f.messages.at(-1).id,'GAME_TARGET_POWER_ADMISSION_EXPIRED');await flushDetection();
  }
  await f.advance(60000);
  assert.equal(f.messages.length,first+4);
  assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().counters.readStarted,before+1);
  assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid);assert.equal(f.detector.isGameStatusCurrent(),false);
 }finally{stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);}
});

await check('game detection renews exhausted recovery only for a new sleep scope',async()=>{
 const f=detectionFixture(null,{sharedTransport:true});
 const current=versionedGame(TMNT,10,10),stale=versionedGame(TMNT,9,9);
 f.broadcastPower({...f.power,suspendSerial:0});f.native=current;
 await f.detector.refreshGameStatusStrict();
 const readiness=[],stop=f.detector.subscribeGameTargetReadiness(value=>readiness.push(value));
 try{
  assert.equal(f.scheduler.hasScheduledTask('game-status'),false);
  f.native=stale;
  const before=f.messages.length;
  f.broadcastPower({...f.power,publicationRevision:2,observedNotificationSequence:2});
  await flushDetection();
  await f.advance(10000);
  assert.equal(f.messages.length,before+12,'the initial observation exhausts its three timed recovery reads');
  assert.equal(f.detector.isGameStatusCurrent(),false);
  assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid);

  const exhausted=f.messages.length;
  f.broadcastPower({...f.power,publicationRevision:3,observedNotificationSequence:3});
  await flushDetection();
  await f.advance(10000);
  assert.equal(f.messages.length,exhausted+3,'another notification for the same scope must not refund the timer budget');

  const next=f.messages.length;
  f.broadcastPower({...f.power,publicationRevision:4,suspendSerial:1});
  await flushDetection();
  assert.equal(f.messages.length,next+3,'coalesced Ready runs one current observation even if it receives stale replies');
  assert.equal(f.detector.isGameStatusCurrent(),false);
  f.native=current;
  await f.advance(499);
  assert.equal(f.messages.length,next+3);
  await f.advance(1);
  assert(f.detector.isGameStatusCurrent(),'new sleep scope must recover from its first failed observation without a poll or user action');
  assert.equal(f.messages.length,next+4);
  assert(f.messages.slice(next).every(m=>m.args.automatic===true&&m.args.pid===undefined&&m.args.expectedPowerSuspendSerial===1));
  assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid);
  assert.deepEqual(readiness,[true,false,true]);
 }finally{stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);}
});

await check('non-Steam detection rechecks an unconfirmed target on public game recovery completion',async()=>{
 for(const outcome of ['confirmed','unavailable','closed','superseded','in-flight','in-flight-confirmed','in-flight-revoked']){
  const f=detectionFixture(null,{sharedTransport:true}),readiness=[];
  const target=versionedGame(TMNT,10,10);
  f.broadcastPower({...f.power,suspendSerial:0});f.native=target;
  await f.detector.refreshGameStatusStrict();
  const stop=f.detector.subscribeGameTargetReadiness(value=>readiness.push(value));
  try{
   let ready={...f.power,generation:101,suspendSerial:1,observedNotificationSequence:2,publicationRevision:2};
   f.native={...target,observationState:'pending'};
   const start=f.messages.length;
   f.broadcastPower(ready);await flushDetection();await f.advance(20000);
   assert.equal(f.messages.length,start+4,'exhaust the existing three retry slots');
   assert.equal(f.scheduler.hasScheduledTask('game-status'),false);
   assert.equal(f.detector.isGameStatusCurrent(),false);
   assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid,'unknown is not a game exit');
   let pending;
   if(outcome.startsWith('in-flight')){
    f.hold=true;
    ready={...ready,observedNotificationSequence:3,publicationRevision:3};
    f.broadcastPower(ready);await flushDetection();
    assert.equal(f.messages.length,start+5);
    pending=f.messages.at(-1);
   }
   const exhausted=f.messages.length;
   const completed={...ready,gameWakeRecoveryRevision:1};
   if(outcome==='closed')stop();
   const suspend=()=>f.broadcastPower({...ready,generation:102,suspendSerial:2,
    observedNotificationSequence:ready.observedNotificationSequence+1,publicationRevision:ready.publicationRevision+1,
    phase:'suspended',notificationPending:true,hardwareWritesAllowed:false,inputReady:false});
   if(outcome==='superseded')suspend();
   if(outcome==='confirmed')f.native=target;
   f.broadcastPower(completed);await flushDetection();
   if(pending){
    assert.equal(f.messages.length,exhausted,'join the current read before using the completion hint');
    if(outcome==='in-flight-revoked')suspend();
    f.reply(pending.id,outcome==='in-flight'?{...target,observationState:'pending'}:target);await flushDetection();
    f.hold=false;f.native=target;
    await f.advance(3999);assert.equal(f.messages.length,exhausted,'preserve the backoff');
    await f.advance(1);
   }
   const allowed=outcome==='confirmed'||outcome==='unavailable'||outcome==='in-flight';
   assert.equal(f.messages.length,exhausted+Number(allowed),`terminal projection should recheck only with current demand and admission: ${outcome}`);
   if(allowed)assert.deepEqual(f.messages.at(-1).args,{automatic:true,expectedPowerGeneration:101,
     expectedPowerSuspendSerial:1,expectedPowerNotificationSequence:ready.observedNotificationSequence});
   f.broadcastPower(ready);f.broadcastPower(completed);
   await f.advance(60000);
   assert.equal(f.messages.length,exhausted+Number(allowed),'no duplicate read or renewed retry budget');
   assert.deepEqual(readiness,outcome==='confirmed'||outcome==='in-flight'||outcome==='in-flight-confirmed'?[true,false,true]:[true,false]);
  }finally{stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);}
 }
});

await check('non-Steam recovery completion rechecks a preterminal cold game observation',async()=>{
 for(const outcome of ['restored','empty','different-target','new-suspend','unsubscribe']){
  const f=detectionFixture(null,{sharedTransport:true}),readiness=[];
  const initial={...f.power,suspendSerial:0},restored=versionedGame(TMNT,12,12);
  f.broadcastPower(initial);f.native=noGame(10,10);
  await f.detector.refreshGameStatusStrict();
  const stop=f.detector.subscribeGameTargetReadiness(value=>readiness.push(value));
  try{
   const ready={...initial,generation:101,suspendSerial:1,publicationRevision:2,observedNotificationSequence:2};
   const completed={...ready,gameWakeRecoveryRevision:1};
   const before=f.messages.length;f.hold=true;
   f.broadcastPower(ready);await flushDetection();
   assert.equal(f.messages.length,before+1);
   const older=f.messages.at(-1);
   f.broadcastPower(completed);await flushDetection();
   assert.equal(f.messages.length,before+1,'do not overlap the native read that is already in flight');
   if(outcome==='new-suspend')f.broadcastPower({...completed,phase:'suspended',generation:102,suspendSerial:2,
    publicationRevision:3,observedNotificationSequence:3,hardwareWritesAllowed:false,notificationPending:true});
   if(outcome==='unsubscribe')stop();
   const oldObservation=outcome==='different-target'?versionedGame(NEXT_GAME,11,11):noGame(11,11);
   f.reply(older.id,{...oldObservation,observationState:'complete'});await flushDetection();
   const admitted=outcome!=='new-suspend'&&outcome!=='unsubscribe';
   assert.equal(f.detector.getCurrentGame(),null,`no stale target changes the cold cache: ${outcome}`);
   assert.deepEqual(readiness,[true,false],`preterminal reply must not certify the current wake: ${outcome}`);
   assert.equal(f.messages.length,before+Number(admitted)+1,`current demand receives one recheck: ${outcome}`);
   if(admitted){
    const later=f.messages.at(-1);
    assert.deepEqual(later.args,older.args,'recheck the same admitted scope, without selecting the old PID');
    f.reply(later.id,outcome==='empty'?noGame(11,11):restored);await flushDetection();
    assert.equal(f.detector.getCurrentGame()?.pid,outcome==='empty'?undefined:TMNT.pid);
    assert.deepEqual(readiness,[true,false,true]);
    f.broadcastPower(ready);f.broadcastPower(completed);
   }
   await f.advance(60000);
   assert.equal(f.messages.length,before+Number(admitted)+1,'no duplicate read or resurrected demand');
  }finally{stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);}
 }
});

await check('non-Steam recovery completion fences an older empty game observation',async()=>{
 for(const outcome of ['restored','empty','new-suspend','unsubscribe','different-target','different-target-new-suspend']){
  const f=detectionFixture(null,{sharedTransport:true}),readiness=[];
  const retained=versionedGame(TMNT,10,10),restored=versionedGame(TMNT,12,12);
  f.broadcastPower({...f.power,suspendSerial:0});f.native=retained;
  await f.detector.refreshGameStatusStrict();
  const stop=f.detector.subscribeGameTargetReadiness(value=>readiness.push(value));
  try{
   const ready={...f.power,generation:101,suspendSerial:1,publicationRevision:2,observedNotificationSequence:2};
   const completed={...ready,gameWakeRecoveryRevision:1};
   const before=f.messages.length;f.hold=true;
   f.broadcastPower(ready);await flushDetection();
   assert.equal(f.messages.length,before+1);
   const older=f.messages.at(-1);
   f.broadcastPower(completed);await flushDetection();
   assert.equal(f.messages.length,before+1,'completion must not start a concurrent scan');
   if(outcome==='new-suspend'||outcome==='different-target-new-suspend')f.broadcastPower({...completed,phase:'suspended',generation:102,suspendSerial:2,
    publicationRevision:3,observedNotificationSequence:3,hardwareWritesAllowed:false,notificationPending:true});
   if(outcome==='unsubscribe')stop();
   const olderObservation=outcome.startsWith('different-target')?versionedGame(NEXT_GAME,11,11):noGame(11,11);
   f.reply(older.id,{...olderObservation,observationState:'complete'});await flushDetection();
   const admitted=outcome==='restored'||outcome==='empty'||outcome==='different-target';
   assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid,`do not publish a change from before completion: ${outcome}`);
   assert.equal(f.messages.length,before+(admitted?2:1),`the current recovery gets one recheck: ${outcome}`);
   assert.deepEqual(readiness,[true,false]);
   if(admitted){
    const later=f.messages.at(-1);
    assert.deepEqual(later.args,older.args,'retry the same admitted automatic scope');
    f.reply(later.id,outcome==='empty'?{...noGame(11,11),observationState:'complete'}:restored);await flushDetection();
    assert.equal(f.detector.getCurrentGame()?.pid,outcome==='empty'?undefined:TMNT.pid);
    assert.deepEqual(readiness,[true,false,true]);
    f.broadcastPower(completed);await f.advance(60000);
    assert.equal(f.messages.length,before+2,'accept the new observation and deduplicate the completion');
   }else{
    await f.advance(60000);assert.equal(f.messages.length,before+1);
   }
  }finally{stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);}
 }
});

await check('non-Steam completion observed by current power readback survives a lost push',async()=>{
 const f=detectionFixture(null,{sharedTransport:true}),readiness=[],revisions=[],ordinary=[];
 const target=versionedGame(TMNT,10,10);
 f.broadcastPower({...f.power,suspendSerial:0});f.native=target;
 await f.detector.refreshGameStatusStrict();
 const stop=f.detector.subscribeGameTargetReadiness(value=>readiness.push(value));
 const offOrdinary=f.sharedPower.onSharedPowerLifecycle(state=>ordinary.push(state));
 const offRevision=f.sharedPower.onSharedPowerLifecycle(state=>revisions.push(state.gameWakeRecoveryRevision??0),{observeGameWakeRecovery:true});
 try{
  const ready={...f.power,generation:101,suspendSerial:1,observedNotificationSequence:2,publicationRevision:2};
  const completed={...ready,gameWakeRecoveryRevision:1};
  f.native={...target,observationState:'pending'};
  f.broadcastPower(ready);await flushDetection();await f.advance(20000);
  assert.deepEqual(readiness,[true,false]);
  const exhausted=f.messages.length,business=ordinary.length,at=f.sharedPower.getSharedPowerLifecycleDiagnostics();
  const accept=f.sharedPower.beginSharedPowerLifecycleReadback();
  accept({...completed,suspendSerial:0,gameWakeRecoveryRevision:9});
  accept({...completed,known:false,hardwareWritesAllowed:false,gameWakeRecoveryRevision:9});
  accept({...completed,hardwareWritesAllowed:false,gameWakeRecoveryRevision:9});
  await flushDetection();
  assert.equal(f.messages.length,exhausted,'old scope and denied receipts cannot signal current recovery');
  assert.deepEqual(revisions,[0,0]);
  f.native=target;accept({...completed,fanWakeGeneration:99});await flushDetection();
  assert.equal(f.messages.length,exhausted+1,'same power revision can carry a new game completion');
  assert.deepEqual(readiness,[true,false,true]);
  assert.deepEqual(revisions,[0,0,1]);
  assert.equal(ordinary.length,business,'completion does not replay ordinary Ready');
  assert.deepEqual(structuredClone(f.sharedPower.getSharedPowerLifecycle()),completed,'readback supplies only the completion field');
  assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().counters.readStarted,at.counters.readStarted,'no separate public read');
  accept(completed);f.broadcastPower(completed);await f.advance(60000);
  assert.equal(f.messages.length,exhausted+1,'readback and delayed push deduplicate');
  assert.deepEqual(revisions,[0,0,1]);
  const blocked={...completed,publicationRevision:3,hardwareWritesAllowed:false};
  f.broadcastPower(blocked);await flushDetection();
  f.sharedPower.beginSharedPowerLifecycleReadback()({...blocked,hardwareWritesAllowed:true,gameWakeRecoveryRevision:2});
  await flushDetection();
  assert.deepEqual(structuredClone(f.sharedPower.getSharedPowerLifecycle()),blocked,'the direct read cannot open a denied snapshot or advance its completion revision');
  assert.equal(f.messages.length,exhausted+1);
 }finally{offRevision();offOrdinary();stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);}
});

if(detectionOnly&&exactCase==='game detection recovers after a stale reply to a new public observation')await check('game detection recovers after a stale reply to a new public observation',async()=>{
 const f=detectionFixture();
 const current=versionedGame(TMNT,10,10), stale=versionedGame(TMNT,9,9);
 f.native=current;
 await f.detector.refreshGameStatusStrict();
 const readiness=[];
 const stop=f.detector.subscribeGameTargetReadiness(ready=>readiness.push(ready));
 try{
  assert.deepEqual(readiness,[true]);
  assert.equal(f.scheduler.hasScheduledTask('game-status'),false);
  f.native=stale;
  const before=f.messages.length;
  f.broadcastPower({...f.power,publicationRevision:2,observedNotificationSequence:2});
  await flushDetection();
  assert.equal(f.detector.getCurrentGame()?.processCreated,TMNT.processCreated,'a stale reply must preserve the displayed game');
  assert.deepEqual(readiness,[true,false]);
  assert.equal(f.detector.isGameStatusCurrent(),false);
  const beforeRetry=f.messages.length;
  f.native=current;
  await f.advance(499);
  assert.equal(f.messages.length,beforeRetry,'recovery ran before its original delay');
  await f.advance(1);
  assert(f.detector.isGameStatusCurrent(),'a stale reply must leave the new power observation eligible for autonomous recovery');
  assert.deepEqual(readiness,[true,false,true]);
  assert.equal(f.messages.length,before+4,'three old replies and the existing recovery read are the only new reads');
  assert(f.messages.slice(before).every(m=>m.args.automatic===true&&m.args.pid===undefined&&m.args.expectedPowerGeneration===100&&m.args.expectedPowerNotificationSequence===2));
  assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid);
  assert.equal(f.detector.getCurrentGame()?.generation,10);
  f.native=stale;
  const stable=f.messages.length;
  assert.equal((await f.detector.refreshGameStatusStrict())?.generation,10,'the same-power cache stays eligible for stale fallback');
  assert(f.detector.isGameStatusCurrent());
  assert.equal(f.messages.length,stable+1);
  assert.deepEqual(readiness,[true,false,true]);
 }finally{stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);}
});

if(detectionOnly&&exactCase==='game profile auto memory resumes after queued work loses public admission')await check('game profile auto memory resumes after queued work loses public admission',async()=>{
 for(const [boundary,coalesced,sameScope] of [['queued',false],['before-commit',false],['after-commit',false],['before-commit',true],['before-commit',false,true],['restoring',false,true]]){
  const f=detectionFixture(),power={...f.sharedPower,isSharedPowerLifecycleAdmitted:f.powerModule.isSharedPowerLifecycleAdmitted};
  if(coalesced)f.power.suspendSerial=7;
  const first=coalesced?7:100,next=sameScope?first:coalesced?8:101;
  const scope=()=>coalesced?f.power.suspendSerial:f.power.generation;
  const target=load('src/bridge/gameCustomProfileTarget.ts',{'./gamePolicyHysteresis':hysteresis});
  const listeners=new Set(),attempts=[],writes=[],writeAttempts=[],restores=[];
  let config={autoMemory:false,entries:{},rtss:{}},release;
  const hold=new Promise(resolve=>{release=resolve;});
  const publish=(patch={})=>{config={...config,...patch};for(const cb of listeners)cb(structuredClone(config));};
  const auto=load('src/bridge/gameProfileAutoMemory.ts',{
   './gamePolicyTarget':f.policy,'./gamedetect':f.detector,'./settingsRepository':{getSettingsGeneration:()=>1},
   './powerLifecycleState':power,'./gameCustomProfileTarget':target,'./performanceSchedule':{
    loadGameCustomConfig:async()=>structuredClone(config),
    onGameCustomConfigChanged(cb){listeners.add(cb);return()=>listeners.delete(cb);},
    async createGameCustomProfile(identity,_name,{automatic,admit}){
     assert(automatic&&admit());assert.equal(identity.pid,TMNT.pid);assert.equal(identity.processCreated,TMNT.processCreated);
     const attempt=scope();attempts.push(attempt);const attemptNumber=attempts.length;
     if(config.entries[identity.exe])return false;
     if(attempts.length===1&&boundary==='before-commit')await hold;
     if(!admit())return false;
     writes.push(attempt);writeAttempts.push(attemptNumber);publish({entries:{[identity.exe]:true}});
     if(attempts.length===1&&boundary==='after-commit')await hold;
     return true;
    },
    async restorePerformanceScheduleIfConfigured(_side,options){
     if(boundary==='restoring'){
      await hold;
      if(options?.ownsPowerRequest?.()===false)return;
     }
     restores.push(scope());
    },
   },
  });
  try{
   f.native=versionedGame();await f.detector.refreshGameStatusStrict();
   auto.startGameProfileAutoMemory();await flushDetection();
   assert(f.detector.isGameStatusCurrent());assert.equal(writes.length,0);
   publish({autoMemory:true});publish();
   assert(auto.getGameProfileAutoMemoryDiagnostics().pending);
   if(boundary!=='queued'){
    await flushDetection();assert.deepEqual(attempts,[first]);assert(auto.getGameProfileAutoMemoryDiagnostics().busy);
   }
   if(coalesced){
    f.broadcastPower({...f.power,publicationRevision:2,suspendSerial:next});
    await flushDetection();await f.detector.refreshGameStatusStrict();
   }else{
   f.broadcastPower({...f.power,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:sameScope?f.power.observedNotificationSequence:2});
   await flushDetection();
   assert(!f.detector.isGameStatusCurrent());assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid);
   assert.deepEqual(writes,['after-commit','restoring'].includes(boundary)?[first]:[]);assert.deepEqual(restores,[]);
   if(boundary==='queued')assert.deepEqual(attempts,[],'revoked queued work reached the write port');
   f.broadcastPower({...f.power,generation:next,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});
   }
   await flushDetection();await f.advance(1250);
   assert(f.detector.isGameStatusCurrent());assert.equal(f.detector.getCurrentGame()?.processCreated,TMNT.processCreated);
   if(boundary!=='queued')assert.deepEqual(attempts,[first],'successor overlapped an unresolved write');
   release();await flushDetection();
   const committedBeforeSleep=['after-commit','restoring'].includes(boundary);
   assert.deepEqual(writes,committedBeforeSleep?[first]:[next],'same game did not resume after zero-work public revocation');
   assert.deepEqual(writeAttempts,[committedBeforeSleep||boundary==='queued'?1:2],'retired attempt borrowed a later Ready');
   assert.deepEqual(attempts,boundary==='queued'?[next]:[first,next]);
   assert.deepEqual(restores,committedBeforeSleep?[]:[next]);
   assert(!auto.getGameProfileAutoMemoryDiagnostics().pending&&!auto.getGameProfileAutoMemoryDiagnostics().busy);
   const settled=attempts.length;
   publish({entries:{}});await flushDetection();
   assert.equal(attempts.length,settled,'config-only deletion re-created a handled profile');
   publish({autoMemory:false});publish({autoMemory:true});publish({autoMemory:false});await flushDetection();
   assert.equal(attempts.length,settled,'queued work ignored opt-out');
   publish({autoMemory:true});auto.stopGameProfileAutoMemory();await flushDetection();
   assert.equal(attempts.length,settled,'queued work survived stop');
  }finally{release();auto.stopGameProfileAutoMemory();f.stopPowerProbe();await flushDetection();assert.equal(listeners.size,0);}
 }
});

await check('game profile auto memory rechecks configuration after an interrupted public scope',async()=>{
 for(const scenario of ['rejected','coalesced-stale-success','stopped','config-update','current-failure']){
  const f=detectionFixture(),power={...f.sharedPower,isSharedPowerLifecycleAdmitted:f.powerModule.isSharedPowerLifecycleAdmitted};
  const policyStops=[];
  const policy=load('src/bridge/gamePolicyTarget.ts',{'./gamePolicyHysteresis':hysteresis,'./gamedetect':{
   ...f.detector,subscribeGameStatus(cb){const stop=f.detector.subscribeGameStatus(cb);policyStops.push(stop);return stop;},
  }});
  const target=load('src/bridge/gameCustomProfileTarget.ts',{'./gamePolicyHysteresis':hysteresis});
  const listeners=new Set(),reads=[],writes=[],restores=[];
  let settle,fail;
  const old=new Promise((resolve,reject)=>{settle=resolve;fail=reject;});
  const config=autoMemory=>({autoMemory,entries:{},rtss:{}});
  const auto=load('src/bridge/gameProfileAutoMemory.ts',{
   './gamePolicyTarget':policy,'./gamedetect':f.detector,'./settingsRepository':{getSettingsGeneration:()=>1},
   './powerLifecycleState':power,'./gameCustomProfileTarget':target,'./performanceSchedule':{
    loadGameCustomConfig:async()=>{
     reads.push(f.power.suspendSerial);
     return reads.length===1?old:config(scenario==='rejected');
    },
    onGameCustomConfigChanged(cb){listeners.add(cb);return()=>listeners.delete(cb);},
    async createGameCustomProfile(identity,_name,{automatic,admit}){
     assert(automatic&&admit());assert.equal(identity.pid,TMNT.pid);assert.equal(identity.processCreated,TMNT.processCreated);
     writes.push(identity.pid);return true;
    },
    async restorePerformanceScheduleIfConfigured(_side,options){assert(options.ownsPowerRequest());restores.push(true);},
   },
  });
  try{
   f.broadcastPower({...f.power,suspendSerial:7});
   f.native=versionedGame();await f.detector.refreshGameStatusStrict();
   auto.startGameProfileAutoMemory();await flushDetection();assert.deepEqual(reads,[7]);
   if(scenario==='current-failure'){
    fail(Error('the current read failed'));await flushDetection();
    assert.deepEqual(reads,[7]);assert.equal(auto.getGameProfileAutoMemoryDiagnostics().lastError,'the current read failed');
    continue;
   }
   if(scenario==='coalesced-stale-success')f.broadcastPower({...f.power,suspendSerial:8,publicationRevision:2});
   else{
    f.broadcastPower({...f.power,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2});
    await flushDetection();assert.deepEqual(reads,[7]);
    f.broadcastPower({...f.power,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});
   }
   await flushDetection();await f.detector.refreshGameStatusStrict();
   assert(f.detector.isGameStatusCurrent());assert.deepEqual(reads,[7],'the current read cannot overlap the original operation');
   if(scenario==='stopped')auto.stopGameProfileAutoMemory();
   if(scenario==='config-update')for(const listener of listeners)listener(config(false));
   if(scenario==='rejected')fail(Error('retired pre-sleep configuration read'));
   else settle(config(true));
   await flushDetection();
   const successor=!['stopped','config-update'].includes(scenario);
   assert.deepEqual(reads,successor?[7,f.power.suspendSerial]:[7],`${scenario}: the retired operation must release exactly one required current read`);
   assert.deepEqual(writes,scenario==='rejected'?[TMNT.pid]:[],`${scenario}: only the current opted-in game may be created`);
   assert.deepEqual(restores,scenario==='rejected'?[true]:[]);
   assert.equal(auto.getGameProfileAutoMemoryDiagnostics().lastError,'');
  }finally{
   settle(config(false));auto.stopGameProfileAutoMemory();for(const stop of policyStops)stop();f.stopPowerProbe();await flushDetection();
   assert.equal(listeners.size,0);assert.equal(f.timers.size,0);assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);
  }
 }
});

await check('game profile auto memory retries a failed creation only after public readmission',async()=>{
 for(const scenario of ['same-scope','merged','off','already-exists','restore-failed','reply-lost']){
  const f=detectionFixture(null,{sharedTransport:true}),initial={...f.power,suspendSerial:7};
  const power={...f.sharedPower,isSharedPowerLifecycleAdmitted:f.powerModule.isSharedPowerLifecycleAdmitted};
  const target=load('src/bridge/gameCustomProfileTarget.ts',{'./gamePolicyHysteresis':hysteresis});
  const listeners=new Set(),attempts=[],writes=[];
  const config={autoMemory:true,entries:{},rtss:{}};
  let restored=0;
  const auto=load('src/bridge/gameProfileAutoMemory.ts',{
   './gamePolicyTarget':f.policy,'./gamedetect':f.detector,'./settingsRepository':{getSettingsGeneration:()=>1},
   './powerLifecycleState':power,'./gameCustomProfileTarget':target,'./performanceSchedule':{
    loadGameCustomConfig:async()=>structuredClone(config),
    onGameCustomConfigChanged(cb){listeners.add(cb);return()=>listeners.delete(cb);},
    async createGameCustomProfile(identity,_name,{automatic,admit}){
     assert(automatic&&admit());assert.equal(identity.pid,TMNT.pid);assert.equal(identity.processCreated,TMNT.processCreated);
     attempts.push(f.power.suspendSerial);
     if(attempts.length===1){
      if(scenario==='already-exists')return false;
      if(scenario==='reply-lost'){writes.push(identity.pid);throw Error('creation reply was lost');}
      if(scenario!=='restore-failed')throw Error('configuration observation unavailable before commit');
     }
     if(scenario==='reply-lost'&&writes.length>0)return false;
     writes.push(identity.pid);return true;
    },
    async restorePerformanceScheduleIfConfigured(_side,{ownsPowerRequest}){
     assert(ownsPowerRequest());++restored;
     if(scenario==='restore-failed')throw Error('restore failed after profile creation');
    },
   },
  });
  try{
   f.broadcastPower(initial);f.native=versionedGame();await f.detector.refreshGameStatusStrict();
   auto.startGameProfileAutoMemory();await flushDetection();
   assert.deepEqual(attempts,[7]);assert(!auto.getGameProfileAutoMemoryDiagnostics().busy,'the first attempt is already settled');
   for(const cb of listeners)cb(structuredClone(config));
   f.broadcastPower(initial);await flushDetection();assert.deepEqual(attempts,[7],'same-scope observations cannot create a retry loop');
   const merged=scenario==='merged';
   const ready={...initial,publicationRevision:3,suspendSerial:merged?8:7};
   if(!merged){
    f.broadcastPower({...initial,phase:'suspending',hardwareWritesAllowed:false,inputReady:false,publicationRevision:2});
    await flushDetection();assert.deepEqual(attempts,[7]);
   }
   if(scenario==='off')for(const cb of listeners)cb({...config,autoMemory:false});
   f.broadcastPower(ready);await flushDetection();await f.advance(1250);
   assert(f.detector.isGameStatusCurrent(),'the same game has been revalidated');
   const shouldRetry=scenario==='same-scope'||merged||scenario==='reply-lost';
   assert.deepEqual(attempts,shouldRetry?[7,ready.suspendSerial]:[7],`${scenario}: only unconfirmed creation can retry after readmission`);
   f.broadcastPower(ready);await flushDetection();
   assert.equal(attempts.length,shouldRetry?2:1,'duplicate Ready must not repeat creation');
   assert.deepEqual(writes,shouldRetry||scenario==='restore-failed'?[TMNT.pid]:[]);
   assert.equal(restored,scenario==='reply-lost'?0:writes.length);
  }finally{auto.stopGameProfileAutoMemory();f.stopPowerProbe();await flushDetection();assert.equal(listeners.size,0);}
 }
});

await check('公共睡眠恢复时策略首次识别不保留退休初始化Promise',async()=>{
 const f=detectionFixture();f.hold=true;const old=f.policy.detectPolicyGame();old.catch(()=>{});await flushDetection();assert.equal(f.messages.length,1);const first=f.messages[0];let next=null,nextError=null,completed=false;let newer;
 try{
  f.broadcastPower({...f.power,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});
  f.native=versionedGame(NEXT_GAME,12);f.hold=false;f.broadcastPower({...f.power,generation:101,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});
  newer=f.policy.detectPolicyGame().then(value=>{next=value;completed=true;},error=>{nextError=error;completed=true;});await flushDetection();
  assert(f.detector.isGameStatusInitialized()&&f.detector.isGameStatusCurrent(),'actual public detector failed to recover its new authority');assert.equal(f.detector.getCurrentGame().pid,NEXT_GAME.pid);
  assert.equal(completed,true,'policy facade retained retired initialization Promise');assert.equal(nextError,null,'policy facade attached to retired initialization failure');assert.equal(next.pid,NEXT_GAME.pid);assert.equal(f.messages.length,2,'policy facade created duplicate discovery');assert(f.messages.every(m=>m.args.pid===undefined),'policy initialization re-elected a captured PID');
 }finally{f.fail(first.id,'retired old initialization');await old.catch(()=>{});if(newer)await newer;f.stopPowerProbe();}
});
await check('公共通知序号立即归还旧识别等待且后继不依赖私有轮询',async()=>{
 const f=detectionFixture();f.native=versionedGame();const stop=f.detector.subscribeGameStatus(()=>{});await flushDetection();f.hold=true;let retired=false;
 const old=f.detector.refreshGameStatusStrict().then(()=>({ok:true}),error=>{retired=true;return{error};});await flushDetection();const message=f.messages.at(-1),record=f.abortRecords.at(-1);let completed;
 try{
  f.hold=false;f.native=versionedGame(NEXT_GAME,12);f.broadcastPower({...f.power,publicationRevision:2,observedNotificationSequence:2});await flushDetection();
  assert.equal(record.signal.aborted,true,'same-generation notification retained the old game IPC signal');assert.equal(record.callbacks.size,0);assert.equal(retired,true,'same-generation notification retained local game wait');assert(![...f.timers.values()].some(t=>t.at===8000));
  await f.advance(1250);assert(f.detector.isGameStatusCurrent());assert.equal(f.detector.getCurrentGame().pid,NEXT_GAME.pid);completed=f.detector.getCurrentGame();
  f.reply(message.id,versionedGame(TMNT,1));f.fail(message.id,'late retired IO');await flushDetection();assert.equal(f.detector.getCurrentGame().pid,completed.pid);assert(f.detector.isGameStatusCurrent());
 }finally{f.fail(message.id,'fixture terminal');await old;stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);assert(f.abortRecords.every(r=>r.callbacks.size===0));}
});

await check('公共Ready早于识别发出时旧请求不得借新代次选游戏',async()=>{
 for(const [name,boundary,preferredPid] of [
  ['coalesced-wake',{generation:101,observedNotificationSequence:2,suspendSerial:2},TMNT.pid],
  ['same-generation-notification',{observedNotificationSequence:2},TMNT.pid],
  ['same-generation-suspend',{suspendSerial:2},TMNT.pid],
  ['ordinary-read',{generation:101,observedNotificationSequence:2,suspendSerial:2},0],
 ]){
  const f=detectionFixture(null,{sharedTransport:true});
  const before={...f.power,suspendSerial:1};f.broadcastPower(before);
  f.native=versionedGame();await f.detector.refreshGameStatusStrict();
  const readiness=[],stop=f.detector.subscribeGameTargetReadiness(ready=>readiness.push(ready));
  const first=f.messages.length;
  const old=f.detector.refreshGameStatusStrict(preferredPid).then(value=>({value}),error=>({error}));
  const oldSignal=f.abortRecords.at(-1).signal;
  try{
   assert.equal(f.messages.length,first,`${name}: request already reached Native`);
   f.native=versionedGame(NEXT_GAME,11);
   f.broadcastPower({...before,...boundary,publicationRevision:2});
   await flushDetection();
   const sent=f.messages.slice(first);
   assert.deepEqual(sent.map(m=>[m.args.automatic,m.args.pid]),[[true,undefined]],`${name}: queued read borrowed the new power scope`);
   assert((await old).error,`${name}: revoked caller received a successful observation`);
   assert(oldSignal.aborted,`${name}: queued read retained power ownership`);
   assert.equal(sent[0].args.expectedPowerGeneration,f.power.generation);
   assert.equal(sent[0].args.expectedPowerNotificationSequence,f.power.observedNotificationSequence);
   assert.equal(sent[0].args.expectedPowerSuspendSerial,f.power.suspendSerial);
   assert.equal(f.detector.getCurrentGame().pid,NEXT_GAME.pid);
   assert(f.detector.isCurrentGameTarget(f.native));
   assert.deepEqual(readiness.slice(-3),[true,false,true]);
  }finally{stop();f.stopPowerProbe();await flushDetection();}
  assert.equal(f.timers.size,0);assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);
  assert(f.abortRecords.every(r=>r.callbacks.size===0));
 }
});

await check('冷启动识别只绑定首份公共电源事实',async()=>{
 for(const changed of [false,true]){
  const f=detectionFixture(null,{sharedTransport:true});
  f.powerReadHook=()=>new Promise(()=>{});
  f.native=versionedGame();
  const old=f.detector.refreshGameStatusStrict(TMNT.pid).then(value=>({value}),error=>({error}));
  const signal=f.abortRecords.at(-1).signal;
  try{
   await flushDetection();
   assert.equal(f.sharedPower.getSharedPowerLifecycle(),null);
   assert.equal(f.messages.length,0);
   f.broadcastPower({...f.power,suspendSerial:1});
   if(changed)f.broadcastPower({...f.power,suspendSerial:2,publicationRevision:2});
   const result=await old;
   if(changed){
    assert(result.error,'cold request borrowed a later power scope');
    assert(signal.aborted);
    assert.equal(f.messages.length,0);
    assert.equal((await f.detector.refreshGameStatusStrict()).pid,TMNT.pid);
    assert.equal(f.messages[0].args.expectedPowerSuspendSerial,2);
   }else{
    assert.equal(result.value.pid,TMNT.pid);
    assert.equal(f.messages[0].args.expectedPowerSuspendSerial,1);
    assert(!signal.aborted);
   }
  }finally{f.stopPowerProbe();await flushDetection();}
  assert.equal(f.timers.size,0);assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);
  assert(f.abortRecords.every(r=>r.callbacks.size===0));
 }
});

await check('公共撤销归还旧识别IPC等待和timer但不伪造Native取消',async()=>{
 for(const preferred of [0,TMNT.pid]){
  const f=detectionFixture();f.native=versionedGame();const stop=f.detector.subscribeGameStatus(()=>{});await flushDetection();f.hold=true;let settled=false,rejected=false;const old=f.detector.refreshGameStatusStrict(preferred).then(()=>{settled=true;},()=>{settled=true;rejected=true;});await flushDetection();const message=f.messages.at(-1),before=f.messages.length;
  f.broadcastPower({...f.power,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});await flushDetection();
  try{assert.equal(settled,true,'public revocation retained local game IPC wait');assert.equal(rejected,true);assert(![...f.timers.values()].some(t=>t.at===8000),'revoked game IPC timeout retained its private request');assert.equal(f.messages.length,before,'public cancellation sent a native command');f.reply(message.id,versionedGame());await flushDetection();assert.equal(f.detector.isGameStatusCurrent(),false);}
  finally{f.fail(message.id,'fixture release old IO only');await old;stop();f.stopPowerProbe();}
 }
});

await check('game status fanout resumes only the undelivered observers after a new power scope',async()=>{
 for(const outcome of ['same-target','replacement','readiness-revokes']){
  const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);
  f.broadcastPower({...f.power,suspendSerial:1});f.native=noGame(1);
  await f.detector.refreshGameStatusStrict();
  const first=[],second=[],removed=[],readiness=[],revokesInReadiness=outcome==='readiness-revokes';
  const revoke=()=>{
   f.hold=true;
   f.broadcastPower({...f.power,suspendSerial:2,observedNotificationSequence:2,publicationRevision:2});
  };
  const stopA=f.detector.subscribeGameStatus(game=>{
   if(!game)return;
   first.push([game.pid,f.detector.isGameStatusCurrent()]);
   if(!revokesInReadiness&&first.length===1)revoke();
  });
  const stopB=f.detector.subscribeGameStatus(game=>{if(game)second.push([game.pid,f.detector.isGameStatusCurrent()]);});
  const stopC=f.detector.subscribeGameStatus(game=>{if(game)removed.push(game.pid);});
  const stopReady=f.detector.subscribeGameTargetReadiness(value=>{
   readiness.push(value);
   if(revokesInReadiness&&value&&readiness.length===3)revoke();
  });
  try{
   f.native=versionedGame(TMNT,10,10);
   await assert.rejects(f.detector.refreshGameStatusStrict(TMNT.pid),/power admission expired/);
   await f.advance(100);
   assert.deepEqual(first,revokesInReadiness?[]:[[TMNT.pid,true]]);
   assert.deepEqual(second,[],'an old target must not reach a later status observer after a newer suspend');
   assert.deepEqual(removed,[]);
   assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid,'retain the most recent display snapshot');
   assert.equal(f.messages.length,3,'only one current automatic read may replace the old delivery');
   const next=f.messages.at(-1);
   assert.equal(next.args.automatic,true);assert.equal(next.args.expectedPowerSuspendSerial,2);
   assert.equal(next.args.expectedPowerNotificationSequence,2);assert.equal(next.args.pid,undefined);
   stopC();
   const current=outcome==='replacement'?NEXT_GAME:TMNT;
   f.hold=false;f.native=versionedGame(current,11,outcome==='replacement'?11:10);
   f.reply(next.id,f.native);await flushDetection();
   assert.deepEqual(first,outcome==='replacement'?[[TMNT.pid,true],[NEXT_GAME.pid,true]]:[[TMNT.pid,true]]);
   assert.deepEqual(second,[[current.pid,true]],'the current target must reach a skipped observer once');
   assert.deepEqual(removed,[]);
   assert.deepEqual(readiness,[true,false,true,false,true]);
   f.broadcastPower({...f.power,publicationRevision:3});await f.advance(1000);
   assert.equal(second.length,1);assert.equal(f.messages.length,3,'same-scope publication must not reopen delivery');
  }finally{
   stopReady();stopC();stopB();stopA();block();f.stopPowerProbe();
   for(const {id} of f.messages)f.fail(id,'fixture cleanup');
   await flushDetection();assert.equal(f.timers.size,0);
  }
 }
});

await check('非 Steam：策略分发遇到新电源边界后只补交未收到的目标',async()=>{
 for(const [field,replacement] of [['suspendSerial',false],['observedNotificationSequence',false],['suspendSerial',true]]){
  const f=detectionFixture(null,{sharedTransport:true}),block=blockDisplayScheduler(f);
  f.broadcastPower({...f.power,suspendSerial:1});
  f.native=noGame(1);
  await f.policy.detectPolicyGame();
  const delivered=[],later=[],joining=[],unsubscribed=[];
  let join;
  const first=f.policy.subscribePolicyGameStatus(game=>{
   if(!game)return;
   delivered.push([game.pid,game.processCreated,f.detector.isGameStatusCurrent()]);
   if(delivered.length!==1)return;
   f.hold=true;
   f.broadcastPower({...f.power,[field]:2,publicationRevision:2});
   join=f.policy.subscribePolicyGameStatus(target=>{if(target)joining.push([target.pid,f.detector.isGameStatusCurrent()]);});
  });
  const second=f.policy.subscribePolicyGameStatus(game=>{if(game)later.push([game.pid,f.detector.isGameStatusCurrent()]);});
  const removed=f.policy.subscribePolicyGameStatus(game=>{if(game)unsubscribed.push(game.pid);});
  try{
   f.native=versionedGame(TMNT,10,10);
   await assert.rejects(f.detector.refreshGameStatusStrict(TMNT.pid),/power admission expired/);
   await flushDetection();
   assert.deepEqual(delivered,[[TMNT.pid,TMNT.processCreated,true]]);
   assert.deepEqual({later,joining},{later:[],joining:[]},`${field}: invalidated target reached a policy listener`);
   assert.equal(f.policy.getPolicyGame()?.pid,TMNT.pid,'keep the display while policy authority waits');
   assert.equal(f.messages.length,3);
   const next=f.messages.at(-1);
   assert.equal(next.args.pid,undefined,'automatic recovery repeated the explicit selection');
   assert.equal(next.args.automatic,true);
   removed();
   const current=replacement?NEXT_GAME:TMNT;
   f.hold=false;f.native=versionedGame(current,11,replacement?11:10);
   f.reply(next.id,f.native);await flushDetection();
   assert.deepEqual(later,[[current.pid,true]]);
   assert.deepEqual(joining,[[current.pid,true]]);
   assert.deepEqual(unsubscribed,[],'a stopped subscriber cannot receive the deferred status');
   assert.deepEqual(delivered.map(([pid])=>pid),replacement?[TMNT.pid,NEXT_GAME.pid]:[TMNT.pid],
    'the first subscriber must not reapply the same game or miss its replacement');
   assert.equal(f.messages.length,3,'delivery recovery must not start another read');
  }finally{
   join?.();removed();second();first();block();f.stopPowerProbe();
   for(const msg of f.messages)f.fail(msg.id,'fixture settlement');
   await flushDetection();
  }
 }
});
await check('非 Steam：识别发布中失去公共电源范围时只交付后继就绪',async()=>{
 for(const [field,preferredPid] of [['suspendSerial',TMNT.pid],['observedNotificationSequence',0]]){
  const f=detectionFixture(null,{sharedTransport:true});
  f.broadcastPower({...f.power,suspendSerial:1});
  f.native=versionedGame(TMNT);
  let armed=true;
  const readiness=[];
  const revoke=f.detector.subscribeGameTargetReadiness(ready=>{
   if(!ready||!armed)return;
   armed=false;
   f.hold=true;
   f.broadcastPower({...f.power,[field]:2,publicationRevision:2});
  });
  const observe=f.detector.subscribeGameTargetReadiness(ready=>readiness.push([ready,f.detector.isGameStatusCurrent()]));
  try{
   const result=await f.detector.refreshGameStatusStrict(preferredPid).then(value=>({value}),error=>({error}));
   await flushDetection();
   assert.equal(armed,false,'first subscriber did not retire the active power scope');
   assert.deepEqual({returnedRetiredTarget:!result.error,staleReadiness:readiness.filter(([v,current])=>v&&!current).length},
    {returnedRetiredTarget:false,staleReadiness:0},`${field}: power revoked during publication`);
   assert.equal(f.detector.isGameStatusCurrent(),false);
   assert.equal(f.detector.getCurrentGame()?.pid,TMNT.pid,'revocation must not synthesize game exit');
   assert.equal(f.messages.length,2,'current scope must obtain exactly one independent read');
   const [first,next]=f.messages;
   assert.equal(first.args.pid,preferredPid||undefined);
   assert.equal(next.args.pid,undefined,'public recovery replayed an old explicit PID');
   assert.equal(next.args.automatic,true);
   assert.equal(next.args[field==='suspendSerial'?'expectedPowerSuspendSerial':'expectedPowerNotificationSequence'],2);
   f.reply(next.id,versionedGame(TMNT,11,10));await flushDetection();
   assert.deepEqual(readiness.filter(([v])=>v),[[true,true]]);
   assert.equal(f.detector.getCurrentGame()?.processCreated,TMNT.processCreated);
   assert.equal(f.detector.isGameStatusCurrent(),true);
  }finally{
   observe();revoke();f.stopPowerProbe();
   for(const msg of f.messages)f.fail(msg.id,'fixture settlement');
   await flushDetection();
   assert.equal(f.timers.size,0);
   assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);
   assert(f.abortRecords.every(record=>record.callbacks.size===0));
  }
 }
});

await check('公共识别等待64轮撤销恢复归还本侧监听和timer且迟到失败不退款',async()=>{
 const f=detectionFixture();f.native=versionedGame();const seen=[],stop=f.detector.subscribeGameStatus(g=>seen.push(g));await flushDetection();let held=0;
 for(let n=0;n<64;n++){
  f.hold=true;const previous=f.detector.getCurrentGame(),read=f.detector.refreshGameStatusStrict().then(()=>({ok:true}),error=>({error}));await flushDetection();const message=f.messages.at(-1),record=f.abortRecords.at(-1),reads=f.messages.length;assert.equal(record.callbacks.size,1,'original IPC did not own exactly one cancellation listener');
  f.broadcastPower({...f.power,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:f.power.publicationRevision+1,observedNotificationSequence:f.power.observedNotificationSequence+1});await flushDetection();const result=await read;assert(result.error,'public cancellation manufactured a successful detection');assert(record.signal.aborted);assert.equal(record.callbacks.size,0,'retired IPC kept abort listener');assert.equal(f.messages.length,reads,'local cancellation issued Native IO');assert(![...f.timers.values()].some(t=>t.at===8000),'retired request kept its original8s timer');assert.equal(f.detector.getCurrentGame().pid,previous.pid);assert(!f.detector.isGameStatusCurrent());held++;
  f.hold=false;f.native=versionedGame(n%2?TMNT:NEXT_GAME,20+n);f.broadcastPower({...f.power,generation:101+n,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:f.power.publicationRevision+1});await flushDetection();await f.advance(1250);assert(f.detector.isGameStatusCurrent());const current=f.detector.getCurrentGame();f.reply(message.id,versionedGame(previous,1,1));f.fail(message.id,'late retired failure');await flushDetection();assert.equal(f.detector.getCurrentGame().pid,current.pid);assert(f.detector.isGameStatusCurrent());assert(f.abortRecords.every(r=>r.callbacks.size===0),'completed IPC left cancellation listeners');
 }
 assert.equal(held,64);const count=f.messages.length;stop();f.stopPowerProbe();await flushDetection();assert.equal(f.timers.size,0);assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);assert(f.abortRecords.every(r=>r.callbacks.size===0));await f.advance(20000);assert.equal(f.messages.length,count,'last consumer shutdown revived discovery');
});

if(detectionOnly&&exactCase==='非 Steam：首份电源快照一直不可用时释放手动识别，恢复后可重试')await check('非 Steam：首份电源快照一直不可用时释放手动识别，恢复后可重试',async()=>{
 const f=detectionFixture();f.stopPowerProbe();
 f.native=versionedGame(TMNT,10,10);
 f.powerReadHook=async()=>{throw Error('inert Native power read unavailable');};
 let first=null;
 const held=f.detector.refreshGameStatusStrict(TMNT.pid).then(game=>{first={game};},error=>{first={error};});
 await flushDetection();
 try{
  assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,1);assert.equal(f.messages.length,0);
  await f.advance(7999);assert.equal(first,null,'the request retired before its waiting budget');
  await f.advance(1);
  assert(first?.error,'a cold power observation failure must release the game selection wait');
  assert.equal(f.messages.length,0,'an unavailable power snapshot cannot start game discovery');
  assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);assert.equal(f.timers.size,0);
  f.powerReadHook=null;
  const current=await f.detector.refreshGameStatusStrict(TMNT.pid);
  assert.equal(current?.source,'whitelist');assert.equal(current?.pid,TMNT.pid);assert.equal(current?.processCreated,TMNT.processCreated);
  assert.equal(f.messages.length,1);assert.equal(f.messages[0].args.expectedPowerGeneration,f.power.generation);
  assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);assert.equal(f.timers.size,0);
 }finally{
  f.powerReadHook=null;f.broadcastPower(f.power);await held;
 }
});

if(detectionOnly&&exactCase==='非 Steam：首次电源状态返回只下发当前游戏选择')await check('非 Steam：首次电源状态返回只下发当前游戏选择',async()=>{
 const f=detectionFixture();f.stopPowerProbe();
 f.native=versionedGame(NEXT_GAME,11,11);
 let allowPower;
 f.powerReadHook=()=>new Promise(resolve=>{allowPower=resolve;});
 const prior=f.detector.refreshGameStatusStrict(TMNT.pid);
 await flushDetection();
 const current=f.detector.refreshGameStatusStrict(NEXT_GAME.pid);
 await flushDetection();
 assert.equal(f.messages.length,0);assert.equal(typeof allowPower,'function');
 allowPower({...f.power});
 const results=await Promise.all([prior,current]);
 assert.deepEqual(f.messages.map(message=>message.args.pid),[NEXT_GAME.pid],'an older selection cannot enter Native after admission changed hands');
 assert(results.every(game=>game?.pid===NEXT_GAME.pid&&game.processCreated===NEXT_GAME.processCreated));
 assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);assert.equal(f.timers.size,0);
});

if(detectionOnly&&exactCase==='公共睡眠输入覆盖：实际检测器和真实Repository在同游戏恢复后接续')await check('公共睡眠输入覆盖：实际检测器和真实Repository在同游戏恢复后接续',async()=>{
 const f=detectionFixture();f.native=versionedGame();
 const {compileSettingsConsumer,createSettingsFixture}=await import('./fixtures/settings_baseline.mjs');
 const compiled=await compileSettingsConsumer(root,{onCompiled:result=>{if(process.env.YMCC_C125_INPUT_DIR){const dir=path.dirname(process.env.YMCC_C125_INPUT_DIR);fs.writeFileSync(path.join(dir,'input-repository.metafile.json'),JSON.stringify(result.metafile,null,2),{flag:'wx'});fs.writeFileSync(path.join(dir,'input-repository.cjs'),result.outputFiles[0].text,{flag:'wx'});}}});
 const memory=createSettingsFixture(compiled,{input:{outputTarget:{persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false},gyroMotion:{enabled:false}}});await memory.repo.loadSettings();
 const config={version:1,entries:{'tmnt.exe':{enabled:true,padPersona:'steamdeck',gyroOverride:'off'}},rtss:{}};const configListeners=new Set();let releaseRead,held=false;const blocked=new Promise(resolve=>releaseRead=resolve);let reads=0;
 const service={loadGameCustomConfig:async()=>{reads++;if(held){held=false;await blocked;}return structuredClone(config);},onGameCustomConfigChanged:cb=>{configListeners.add(cb);return()=>configListeners.delete(cb);},clearGameInputRestoreSnapshot:async()=>{throw Error('No legacy restore in actual detector composition');}};
 const input=evaluate(fs.readFileSync(path.join(root,'src/bridge/gameInputOverride.ts'),'utf8'),{'./gyroPresetModel':load('src/bridge/gyroPresetModel.ts'),'@/bridge/settingsRepository':memory.repo,'./gamedetect':f.detector,'@/bridge/gamePolicyTarget':{getPolicyGame:f.detector.getCurrentGame,isPolicyGameInitialized:f.detector.isGameStatusInitialized,subscribePolicyGameStatus:f.detector.subscribeGameStatus,gamePolicyKey:hysteresis.gamePolicyKey},'@/bridge/ipc':{invoke:async(command)=>{assert.equal(command,'input.gameOverride.session');return 'actual-input-session';}},'@/scheduler':f.scheduler,'@/bridge/performanceSchedule':service,'./powerLifecycleState':{...f.sharedPower,isSharedPowerLifecycleAdmitted:f.powerModule.isSharedPowerLifecycleAdmitted},'./powerResume':load('src/bridge/powerResume.ts')},{AbortController,AbortSignal});
 try{
  input.startGameInputOverrideWatch();await flushDetection();assert.equal((await memory.repo.loadSettings()).input.gameOverride.outputTarget.persona,'steamdeck');assert(f.detector.isGameStatusCurrent());
  held=true;config.entries['tmnt.exe'].padPersona='elite';for(const cb of [...configListeners])cb();await flushDetection();const before=reads;
  f.broadcastPower({...f.power,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});await flushDetection();assert(!f.detector.isGameStatusCurrent());assert.equal((await memory.repo.loadSettings()).input.gameOverride.outputTarget.persona,'steamdeck');
  f.broadcastPower({...f.power,generation:101,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});await flushDetection();await f.advance(1250);assert(f.detector.isGameStatusCurrent());assert(reads>before,'actual detector same identity recovery did not resume input');assert.equal((await memory.repo.loadSettings()).input.gameOverride.outputTarget.persona,'elite','actual repository overlay not updated before retired read');
  const revision=(await memory.repo.loadSettings()).input.revision;releaseRead();await flushDetection();assert.equal((await memory.repo.loadSettings()).input.revision,revision,'late actual composition read rewrote overlay');
  const overlayWrites=memory.state.writes.filter(a=>a.expectedInputRevision!==undefined);assert(overlayWrites.length>=2);assert(overlayWrites.every(a=>a.inputRuntimePower&&Number.isSafeInteger(a.inputRuntimePower.generation)&&Number.isSafeInteger(a.inputRuntimePower.observedNotificationSequence)),'actual input/repository/API dropped runtime public power');assert.equal(overlayWrites.at(-1).inputRuntimePower.generation,101);assert.equal(overlayWrites.at(-1).inputRuntimePower.observedNotificationSequence,2);
 }finally{releaseRead();input.stopGameInputOverrideWatch();f.stopPowerProbe();await flushDetection();assert.equal(f.sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);assert.equal(f.timers.size,0);assert.equal(configListeners.size,0);}
});
if(detectionOnly&&exactCase==='公共睡眠输入覆盖：Native撤销仅取消原CAS并保留普通持久设置')await check('公共睡眠输入覆盖：Native撤销仅取消原CAS并保留普通持久设置',async()=>{
 const {compileSettingsConsumer,createSettingsFixture}=await import('./fixtures/settings_baseline.mjs');
 const compiled=await compileSettingsConsumer(root,{onCompiled:result=>{if(process.env.YMCC_C125_INPUT_DIR){const dir=path.dirname(process.env.YMCC_C125_INPUT_DIR);fs.writeFileSync(path.join(dir,'input-repository.metafile.json'),JSON.stringify(result.metafile,null,2),{flag:'wx'});fs.writeFileSync(path.join(dir,'input-repository.cjs'),result.outputFiles[0].text,{flag:'wx'});}}});
 const files=new Map(),writes=[];let cancel=false;
 const storage={directory:'Q:\\ymcc-runtime-power-memory\\PowerControl',seed:(file,raw)=>files.set(file,raw),snapshot:()=>new Map(files),read:async args=>({stamp:files.get(args.path)||'missing',unchanged:false,content:files.get(args.path)||''}),write:async args=>{writes.push(structuredClone(args));if(cancel&&args.inputRuntimePower)return {ok:false,reason:'input-runtime-power-superseded',content:''};files.set(args.path,args.content);return {ok:true,content:args.content};}};
 const f=createSettingsFixture(compiled,{input:{outputTarget:{persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false},gyroMotion:{enabled:false}}},undefined,{storage});await f.repo.loadSettings();
 const before=await f.repo.loadSettings(),raw=f.text(),power={generation:7,observedNotificationSequence:19};cancel=true;
 const pending=f.repo.compareAndSwapInputSettings(before.input.revision,{gameOverride:{identity:'held-native-power'}},()=>true,{inputRuntimePower:power});power.generation=99;
 const refused=await pending;assert.equal(refused.ok,false);assert.equal(refused.reason,'cancelled');assert.equal(f.text(),raw);assert.equal(f.repo.getSettingsRecoveryState().status,'ready','native power retirement incorrectly poisoned durable settings');assert.equal(writes.at(-1).inputRuntimePower.generation,7,'queued runtime request power was not snapshotted');
 const ordinary=await f.repo.compareAndSwapInputSettings(before.input.revision,{outputTarget:{persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false}},()=>true);assert(ordinary.ok,'ordinary durable CAS was gated by runtime cancellation');assert(!Object.hasOwn(writes.at(-1),'inputRuntimePower'));
 cancel=false;const resumed=await f.repo.compareAndSwapInputSettings(ordinary.revision,{gameOverride:{identity:'successor-current'}},()=>true,{inputRuntimePower:{generation:8,observedNotificationSequence:20}});assert(resumed.ok);assert.equal(writes.at(-1).inputRuntimePower.generation,8);assert.equal(f.repo.getSettingsRecoveryState().status,'ready');
});

const nativeWireFlag=process.argv.indexOf('--native-wire-evidence');
currentSection = 'native-wire';

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
}
assert(fromReached,'Unknown --case-from input-runtime case');
assert(exactCase===null||exactMatches===1,'Unknown --case detection-only case');
console.log(JSON.stringify({scope,...scopeDetails,passed:cases.length,cases,nativeWireEvidence,...(inputOwnerOnly?{inputOwnerEvidence}:{}),...(exactCase!==null?{selection:{exactCase,selected:exactMatches,notRun}}:fromCase?{selection:{fromCase,notRun}}:{})},null,2));



