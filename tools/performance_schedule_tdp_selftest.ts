import { reactive } from 'vue';
import './mock-shell';
import { mkdtempSync, rmSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';
import { automaticCpuControls } from '../src/bridge/performanceControls';

const YEMAN = '1cb8b882-a900-4b9f-9bac-99d151e64441';
const CPU_MAX_STATE = 'bc5038f7-23e0-4960-96da-33abaf5935ec';
const CPU_FREQ0 = '75b0ae3f-bce0-45a7-8c89-c9611c25e100';
const CPU_BOOST = 'be337238-0d82-4146-a960-4f3749d470c7';
const CPU_SCHED0 = '36687f9e-e3a5-4dbf-b1dc-15eb381c6863';
const CPU_MIN0 = '893dee8e-2bef-41e0-89c6-b55d0929964c';
const CPU_THROTTLE = '3b04d4fd-1cc7-4f23-ab1c-d1337819c4bb';
const unsupportedLog = [
  '2026-08-11 12:51:07 ensure_pawnio enter',
  '2026-08-11 12:51:07 pawnio_present=True,no action needed',
  '2026-08-11 12:51:07 [family] Desktop-17h(Zen~Zen2) power_feature=pbo_ppt_tdc_edc transport=unsupported verified=False',
  '2026-08-11 12:51:07 [family] Desktop-17h(Zen~Zen2) TDP capability 未验证 -> 拒绝写入',
  'rc=6',
].join('\n');

function assert(condition: unknown, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

async function main(): Promise<void> {
  const dir = mkdtempSync(join(tmpdir(), 'yeman-schedule-tdp-'));
  let powerLifecycleStates: Array<'ready' | 'resuming'> = [];
  let blockedProfileAttempts = 0;
  let blockNextProfileWrite = false;
  let failNextRequiredProfileWrite = false;
  let templateScriptCalls = 0;
  const powerValues = new Map<string, number>();
  (globalThis as any).__mockShellDryRun = true;
  (globalThis as any).__mockShellResponder = (program: string, args: string[]) => {
    const exe = program.toLowerCase();
    if (exe === 'cscript.exe') templateScriptCalls++;
    if (exe.endsWith('yemantdpctl.exe')) {
      return { exitCode: 6, stdout: unsupportedLog, stderr: '' };
    }
    if (exe === 'powercfg' && args[0]?.toLowerCase() === '/getactivescheme') {
      return { exitCode: 0, stdout: `Power Scheme GUID: ${YEMAN} (YeMan)`, stderr: '' };
    }
    return undefined;
  };
  (globalThis as any).__mockIpcResponder = (cmd: string, args: any) => {
    if (cmd === 'registry.writePowerBatch') {
      const entries = Array.isArray(args?.entries) ? args.entries : [];
      const isFixedProfile = entries.some((entry: any) => entry.setting === CPU_MAX_STATE);
      if (isFixedProfile) {
        blockedProfileAttempts++;
        if (blockNextProfileWrite) {
          blockNextProfileWrite = false;
          throw new Error('hardware writes are blocked during power transition');
        }
      }
      const writes = (globalThis as any).__mockRegistryWrites as Array<{
        root: string; path: string; name: string; value: any;
      }>;
      for (const entry of entries) {
        const value = Number(entry.value);
        writes.push({
          root: 'HKLM',
          path: `SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\${args.scheme}\\${args.subGroup}\\${entry.setting}`,
          name: args.valueName,
          value,
        });
        powerValues.set(`${args.valueName}|${entry.setting}`, value);
      }
      if (isFixedProfile && failNextRequiredProfileWrite) {
        failNextRequiredProfileWrite = false;
        return { ok: false, written: entries.length - 1, failed: [{ setting: CPU_FREQ0, code: 5 }] };
      }
      return { ok: true, written: entries.length, failed: [] };
    }
    // The offline fixture must not consult the execution host's real registry.
    if (cmd === 'registry.read' && args?.name === 'VendorIdentifier') return 'AuthenticAMD';
    if (cmd === 'registry.read' && String(args?.path || '').includes('\\User\\PowerSchemes\\')) {
      const setting = String(args?.path || '').split('\\').pop() || '';
      return powerValues.get(`${args?.name}|${setting}`) ?? 0;
    }
    if (cmd === 'power.activeScheme') return YEMAN;
    if (cmd === 'power.lifecycle') {
      const phase = powerLifecycleStates.shift() ?? 'ready';
      return { phase, generation: 1, hardwareWritesAllowed: phase === 'ready', inputReady: phase === 'ready', hibernateAvailable: true };
    }
    if (cmd === 'sys.info') {
      return { cpuName: 'AMD Ryzen Threadripper 3970X', physicalCores: 32, logicalProcs: 64, totalMemoryBytes: 1, acLine: 0, powerMode: 'dc', hasBattery: true };
    }
    if (cmd === 'cpu.architecture') {
      return { detected: true, heterogeneous: false, efficiencyClasses: [0], source: 'cpu-set', logical: 64, physical: 32 };
    }
    if (cmd === 'game.detect') return null;
    if (cmd === 'tdpDaemon.request') {
      if (args?.op === 'ping') return { version: 1, requestId: 'test', ok: true, rc: 0 };
      if (args?.op === 'set') return { version: 1, requestId: 'test', ok: false, rc: 6, error: unsupportedLog };
      return { version: 1, requestId: 'test', ok: true, rc: 0 };
    }
    if (cmd === 'tdpDaemon.start' || cmd === 'monitor.setFpsEnabled') return { ok: true };
    return undefined;
  };

  try {
    const settings = await import('../src/bridge/settingsRepository');
    const yeman = await import('../src/bridge/yeman');
    const autofloat = await import('../src/bridge/autofloat');
    const cpuProfiles = await import('../src/bridge/cpuProfiles');
    const schedule = await import('../src/bridge/performanceSchedule');
    const frameRates = await import('../src/bridge/frameRateLimits');
    settings.setSettingsDirectory(dir);
    yeman.setPowerControlDir(dir);
    autofloat.setAutofloatPowerControlDir(dir);

    const savedCpuProfiles = cpuProfiles.defaultCpuProfilesConfig();
    savedCpuProfiles.profiles.balanced = {
      acFreq: 6200,
      dcFreq: 3600,
      acTurbo: true,
      dcTurbo: true,
      acAggr: 91,
      dcAggr: 77,
    };
    await cpuProfiles.saveCpuProfiles(savedCpuProfiles);

    const warnings: string[] = [];
    const offWarning = schedule.onPerformanceScheduleWarning((warning) => warnings.push(warning.message));
    const config = schedule.defaultPerformanceScheduleConfig();
    config.configured = true;
    config.enabled = true;
    // Simulate the affected class of machine: a battery device currently on DC.
    config.active.dc = 'balanced';
    config.profiles.dc.balanced.cpuPreset = 'balanced';

    // Reactive saves in unrelated pages must not poison the shared settings
    // cache used by real CPU/TDP mode application. Hardware remains mocked.
    await settings.saveSettingsSection('fan', { deviceIdentity: reactive({ manufacturer: 'fixture', model: 'test' }) });
    await settings.saveSettingsSection('quickApps', { apps: reactive([{ name: 'fixture', path: 'fixture.exe' }]) });

    await frameRates.saveGlobalFrameRate('ac',120);
    await frameRates.saveGlobalFrameRate('dc',60);
    const autoApplied = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(autoApplied, 'TDP rc=6 不应让手动→自动切换返回失败');
    assert(autofloat.getFloatInfo().enabled, 'TDP rc=6 后 CPU 浮动仍应启动');
    assert((await schedule.getPerformanceScheduleOwnership()) === 'auto', '自动模式必须独立持久化');
    const warning = schedule.getPerformanceScheduleWarning();
    assert(warning?.code === 'tdp-unsupported', 'Desktop-17h 应给出明确的不支持提示');
    assert(warning.message.includes('不会阻断 CPU 调度'), '提示必须明确 CPU 调度仍继续');
    const writes = (globalThis as any).__mockRegistryWrites as Array<{
      root: string; path: string; name: string; value: any;
    }>;
    assert(!writes.some((item) => item.name === 'ACSettingIndex'), '当前是DC；TDP失败也不能改写AC记忆参数');
    assert(writes.some((item) => item.name === 'DCSettingIndex'), 'TDP 失败后 DC CPU 参数仍应写入');
    assert(!writes.some((item) => item.path.endsWith(`\\${CPU_MAX_STATE}`)), 'CPU 浮动接管时不得重复应用固定 CPU 挡位');

    // Deprecated per-combination FPS (including zero) cannot stop floating or rebind independent FPS.
    config.profiles.dc.balanced.fpsTarget = 0;
    config.profiles.dc.balanced.cpuTarget = 'aggressive';
    config.profiles.dc.balanced.tdpStrategy = 'aggressive';
    writes.length = 0;
    const noLockApplied = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(noLockApplied, '不锁帧档位仍应完成自动模式应用');
    assert(autofloat.getFloatInfo().enabled, '组合旧帧率为0也不能关闭CPU/TDP浮动');
    assert(autofloat.getFloatInfo().target === 60, '优化参考目标必须读取独立DC帧率，不读取组合旧帧率');
    config.profiles.dc.balanced.cpuTarget='none';writes.length=0;
    await schedule.applyPerformanceSchedule('dc','balanced',config);
    assert(writes.length >= 12, `连续 CPU 参数至少应写入当前 DC 侧的必需字段，实际=${writes.length}`);
    assert(writes.every((item) => item.name === 'DCSettingIndex'), 'DC 自动参数不得覆盖 AC 保存值');
    const valueFor = (setting: string) => writes.find((item) => item.path.endsWith(`\\${setting}`))?.value;
    const canonicalCpu = automaticCpuControls(schedule.continuousControlsFromSchedule(await schedule.loadPerformanceSchedule()).dc);
    assert(valueFor(CPU_FREQ0) === canonicalCpu.maxMHz, '自动模式必须应用连续 bank 计算出的 DC 主频');
    assert(valueFor(CPU_BOOST) === (canonicalCpu.turbo ? 2 : 0), '自动模式必须同步连续 bank 计算出的 DC 睿频');
    assert(typeof valueFor(CPU_SCHED0) === 'number', '自动模式必须写入 DC 积极性');
    assert(valueFor(CPU_MIN0) === canonicalCpu.minState, '自动模式必须同步连续 bank 计算出的最小处理器状态');
    assert(valueFor(CPU_MAX_STATE) === undefined, '连续自动模式不应回写旧固定最大处理器状态');
    assert(typeof valueFor(CPU_THROTTLE) === 'number', '自动模式必须同步频率对应的节流状态');
    assert(templateScriptCalls === 0, '自动模式不得运行任何 CPU 模板脚本');

    config.profiles.dc.balanced.fpsTarget = 45;
    const relockApplied = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(relockApplied, '从不锁帧切回有效目标后应能重新应用自动浮动');
    assert(autofloat.getFloatInfo().enabled, '性能组合应用不受旧帧率字段控制');
    assert(autofloat.getFloatInfo().target===60, '切换旧组合帧率不能改变独立DC值');
    assert((await frameRates.loadGlobalFrameRates()).ac.fps===120, 'DC模式不得改写独立AC帧率');

    await schedule.disablePerformanceSchedule(config);
    assert(!autofloat.getFloatInfo().enabled, '自动→手动必须停止 CPU 浮动控制');
    assert((await schedule.getPerformanceScheduleOwnership()) === 'manual', '自动→手动必须独立持久化');

    // Retry only after native reports that the transition has returned to Ready.
    config.profiles.dc.balanced.fpsTarget = 0;
    config.profiles.dc.balanced.cpuPreset = 'balanced';
    config.profiles.dc.balanced.cpuTarget = 'none';
    config.profiles.dc.balanced.tdpStrategy = 'none';
    powerLifecycleStates = ['ready', 'resuming', 'ready'];
    blockedProfileAttempts = 0;
    blockNextProfileWrite = true;
    const recoveredApply = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(recoveredApply, '短暂电源写入闸门应在 native Ready 后自动重试一次');
    assert(blockedProfileAttempts === 0, `连续性能路径不应回写旧固定 CPU 挡位，实际=${blockedProfileAttempts}`);

    failNextRequiredProfileWrite = true;
    const noLegacyProfileFailure = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(noLegacyProfileFailure, '连续性能路径不应被旧固定 CPU 挡位失败分支阻断');
    assert(templateScriptCalls === 0, '连续性能路径不得回退运行模板脚本');

    const autoAppliedAgain = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(autoAppliedAgain, '相同 TDP 不支持错误再次出现时仍不得阻断自动模式');
    assert(warnings.length === 1, '同一平台兼容性提示每次运行只应发布一次');
    await schedule.disablePerformanceSchedule(config);
    offWarning();
    // MSI G3E OEM isolation: real bridge with a fake native endpoint only.
    // No WMI/driver/module/registry/hardware call is made by these cases.
    powerLifecycleStates = []; // A separate OEM fixture starts in a known Ready epoch.
    const circuit = await import('../src/bridge/tdpCircuitBreaker');
    const previousResponder = (globalThis as any).__mockIpcResponder;
    const oemCalls: any[] = [];
    const nonOemCalls: string[] = [];
    let oemResult: any = null; // null = typed OEM-owned acceptance fixture
    let firmwareAcceptedCeiling = 35;
    const runtime = await import('../src/bridge/tdpRuntimeState');
    let capabilityFailure = false;
    const validOemCapability = { cpuFamily: 'intel-arc-g3-extreme', provider: 'msi-g3e-wmi', selected: true, ready: true, minPL1: 8, maxPL1: 200, machineMaximumKnown: false, rangeSource: 'application-request-domain-NOT-machine-maximum', generation: 1, deadlineTick: 8000 };
    let oemCapability: any = validOemCapability;
    let genericG3eFixture = false;
    let oemPowerSide: 'ac' | 'dc' = 'dc';
    let nativeGeneration = 1;
    let fixtureMonitor = false, monitorSeq = 0, fixtureFps = 90, fixtureGame: string | null = 'g3e-fixture.exe';
    const observations: any[] = [];
    const genericG3eCalls: any[] = [];
    (globalThis as any).__mockIpcResponder = (cmd: string, args: any) => {
      if (cmd === 'tdp.capability') {
        if (capabilityFailure) throw new Error('fixture capability transport failure');
        return oemCapability;
      }
      if (cmd === 'tdp.observe') { observations.push({ event: args.event, ...args.detail }); return true; }
      if (fixtureMonitor && cmd === 'monitor.snapshot') {
        const now = Date.now();
        return { available: true, schema: 1, session: 900001, generation: 1, seq: ++monitorSeq, collectedAt: now,
          heartbeatAt: now, hwinfoOkAt: now, topActive: true, fpsActive: true, hwDown: false,
          top: JSON.stringify({ ts: now, ac: oemPowerSide === 'ac' ? 1 : 0 }),
          fps: JSON.stringify({ ts: now, fps: fixtureFps, fps1: fixtureFps, gpu: 0, game: fixtureGame, pid: 9876 }) };
      }
      if (fixtureMonitor && (cmd === 'monitor.start' || cmd === 'monitor.stop')) return { ok: true };
      if (cmd === 'game.detect') return fixtureGame ? {pid:9876,name:fixtureGame,path:'C:\\Fixture\\'+fixtureGame,title:'Fixture',processCreated:'12345'} : null;
      if (cmd === 'sys.info') return { cpuName: 'Intel(R) Arc(TM) G3 Extreme', physicalCores: 16, logicalProcs: 16, totalMemoryBytes: 1, acLine: oemPowerSide === 'ac' ? 1 : 0, powerMode: oemPowerSide, hasBattery: true };
      if (cmd === 'power.lifecycle') return { ...previousResponder(cmd, args), generation: nativeGeneration };
      if (cmd === 'tdpMsi.set') {
        assert(Number.isInteger(args.watts) && args.watts >= 8 && args.watts <= 200, 'every OEM request is an application packet value, not a CPU/model ceiling');
        assert(args.generation === nativeGeneration, 'every OEM write must use the current native generation');
        oemCalls.push(args);
        if (oemResult instanceof Error) throw oemResult;
        if (oemResult !== null) return oemResult;
        const acceptedPL1W = Math.min(args.watts, firmwareAcceptedCeiling);
        return { ok: true, rc: 0, readbackMatched: acceptedPL1W === args.watts,
          acceptedVerified: true, acceptedPL1W, outcome: acceptedPL1W === args.watts ? 'applied' : 'oem-adjusted' };
      }
      if (cmd === 'tdpDaemon.start' || cmd === 'tdpDaemon.request' || (cmd === 'shell.run' && String(args?.program).toLowerCase().endsWith('yemantdpctl.exe'))) nonOemCalls.push(cmd);
      if (genericG3eFixture && cmd === 'fs.stat' && String(args?.path).endsWith('float-active')) throw new Error('fixture float owner absent');
      if (genericG3eFixture && cmd === 'shell.run' && String(args?.program).toLowerCase().endsWith('yemantdpctl.exe')) {
        genericG3eCalls.push(args);
        return { exitCode: 0, stdout: 'fixture generic Intel write receipt', stderr: '' };
      }
      return previousResponder(cmd, args);
    };
    await (await import('../src/bridge/gamedetect')).refreshGameStatusStrict();
    yeman.setPowerControlDir(dir);
    circuit.resetTdpCapabilityState();
    runtime.tdpRuntimeStore.powerMode('dc');
    assert(await yeman.setTdp('ac', 20, { apply: true, save: false, vendor: 'amd', source: 'ui' }), 'G3E uses native actual vendor, not stale saved AMD');
    assert(oemCalls.length === 1 && oemCalls[0].watts === 20 && oemCalls[0].generation === 1, 'same native OEM request contains watts and live epoch');
    assert(oemCalls[0].explicitRetry === false, 'diagnostic source ui is not a retry authorization');
    assert(await yeman.ensureTdpDaemon(), 'MSI requires no PawnIO daemon');
    assert(await yeman.resumeTdpDaemonAfterWake(true), 'MSI wake does not write ceiling/start helper');
    await yeman.stopTdpDaemon();
    assert(nonOemCalls.length === 0, 'MSI set/lifecycle never call daemon/MSR CLI');
    assert(autofloat.getTdpTarget(8, 'medium') === 8, 'MSI float respects provider minimum without changing strategy');
    const savedBefore = await yeman.readTdp('ac');
    for (const invalid of [-2, 0, 1, 8.5, 35.1, 201, NaN, Infinity]) {
      let rejected = false;
      try { await yeman.setTdp('ac', invalid, { apply: true, save: true, source: 'startup' }); }
      catch { rejected = true; }
      assert(rejected && oemCalls.length === 1, `MSI target ${invalid} rejected before save/write`);
    }
    assert(await yeman.readTdp('ac') === savedBefore, 'invalid inputs never save or write');
    for (const intent of [2, 7, 55, 120, 200]) {
      await yeman.setTdp('ac', intent, { apply: true, save: true, source: 'schedule' });
      const effective = Math.max(8, Math.min(35, intent));
      assert(oemCalls[oemCalls.length - 1].watts === Math.max(8, intent), 'portable intent is offered once without a software 35W cap');
      assert(await yeman.readTdp('ac') === intent, 'effective target must not silently replace saved portable intent');
      const receipt = observations.filter(r => r.event === 'tdp.target-resolved').pop();
      assert(receipt.requestedW === intent && receipt.effectiveW === Math.max(8, intent) && runtime.tdpRuntimeStore.get().watts === effective, 'offered intent and OEM-accepted setting stay separate');
    }
    assert(yeman.formatTdpTarget(120) === '120W', 'unknown machine maximum does not introduce a waiting-status label into the existing frontend');
    for (const strategy of ['none', 'small', 'medium', 'large', 'aggressive'] as const) {
      const target = autofloat.getTdpTarget(120, strategy);
      assert(target >= 8 && target <= 120, 'preview without a receipt is a request estimate, not a hardware maximum');
      assert(autofloat.getTdpTarget(35, strategy) <= 35, 'runtime float uses the OEM-accepted baseline for ratios');
    }
    // Replay both AC/DC banks through the canonical continuous schedule.
    const modelConfig = schedule.defaultPerformanceScheduleConfig();
    modelConfig.configured = true;
    modelConfig.enabled = true;
    const modelWarnings: string[] = [];
    const offModelWarning = schedule.onPerformanceScheduleWarning(w => { if (w.code === 'tdp-apply-failed') modelWarnings.push(w.message); });
    const independentBefore = JSON.stringify(await frameRates.loadGlobalFrameRates());
    for (const side of ['ac', 'dc'] as const) {
      oemPowerSide = side;
      assert(await schedule.applyPerformanceSchedule(side, 'balanced', modelConfig), 'each canonical AC/DC bank can cold/hot switch');
      const saved = await schedule.loadPerformanceSchedule();
      const expectedBank = schedule.continuousControlsFromSchedule({ ...modelConfig, active: { ...modelConfig.active, [side]: 'balanced' } })[side];
      assert(saved.controls?.[side].tdp === expectedBank.tdp, 'continuous bank TDP intent is preserved');
      const state = autofloat.getFloatInfo();
      assert(state.tdpMax >= 8 && state.tdpMax <= 35 && state.tdpApplied >= 8 && state.tdpApplied <= 35, 'float stays inside the admitted OEM range');
    }
    assert(modelWarnings.length === 0, 'canonical bank application does not create false TDP-apply-failed warnings');
    assert(JSON.stringify(await frameRates.loadGlobalFrameRates()) === independentBefore, 'canonical schedule application does not change independent FPS');
    // Real timer-driven float loop: idle recovery/lower/FPS recovery cannot
    // feed the exported 41W floor or 120W baseline back into the OEM endpoint.
    fixtureMonitor = true;
    autofloat.setAutofloatPowerControlDir(dir);
    const floatBank = schedule.continuousControlsFromSchedule(await schedule.loadPerformanceSchedule()).dc;
    floatBank.tdp = 35; floatBank.auto.saving = 'aggressive';
    await autofloat.enableContinuousFloat(floatBank, 'dc', 60, async () => true, async () => {}, { requested: 120, accepted: 35 });
    await yeman.saveTdp('dc', 120);
    const waitForApplied = async (test: (watts: number) => boolean): Promise<void> => {
      for (let attempt = 0; attempt < 100; ++attempt) {
        if (test(autofloat.getFloatInfo().tdpApplied)) return;
        await new Promise(resolve => setTimeout(resolve, 100));
      }
      throw new Error('timer-driven admitted float target did not converge: '+JSON.stringify(autofloat.getFloatInfo()));
    };
    await waitForApplied(w => w >= 8 && w < 35);
    fixtureFps = 10;
    await waitForApplied(w => w === 35);
    fixtureFps = 90;
    await waitForApplied(w => w >= 8 && w < 35);
    fixtureGame = null;
    await waitForApplied(w => w === 35);
    const beforeDisable = oemCalls.length;
    await autofloat.disableFloat();
    fixtureMonitor = false;
    assert(oemCalls.length === beforeDisable, 'disableFloat never restores an OEM limit');
    offModelWarning();
    assert(await yeman.adjustTdp(-1) === 34, 'manual hotkey decreases from current effective limit, not from old 120W intent');
    assert(await yeman.setTdpCurrentMode(120) === 35, 'manual explicit target returns actual admitted watts');
    assert(nonOemCalls.length === 0, 'full model flow never starts a helper/daemon or replays MSR');
    nativeGeneration = 1;
    oemCapability = validOemCapability;
    yeman.setPowerControlDir(dir);
    for (const bad of [
      { ok: false, rc: 6, readbackMatched: false, error: 'fixture partial unknown' },
      { ok: true, rc: 0, readbackMatched: false },
      new Error('fixture native timeout'),
    ]) {
      circuit.resetTdpCapabilityState();
      oemResult = bad;
      let rejected = false;
      try { await yeman.setTdp('dc', 25, { apply: true, save: false, source: 'autofloat' }); }
      catch { rejected = true; }
      assert(rejected && nonOemCalls.length === 0, 'OEM partial/mismatch/timeout never replays CLI/MSR');
      assert(oemCalls[oemCalls.length - 1].explicitRetry === false, 'autofloat cannot clear native partial latch');
    }
    oemResult = null;
    for (const outcome of ['unverified', 'oem-rejected']) {
      circuit.resetTdpCapabilityState();
      oemResult = { ok: false, rc: 0, readbackMatched: false, outcome, note: 'fixture OEM refusal or missing readback' };
      assert(!(await yeman.setTdp('dc', 120, { apply: true, save: false })), 'OEM refusal/unknown readback does not throw a user-visible failure');
      assert(runtime.tdpRuntimeStore.get().status === 'unknown' && runtime.tdpRuntimeStore.get().watts === null, 'no readback is unknown, never requested/stale watts');
    }
    oemResult = null;
    firmwareAcceptedCeiling = 65;
    circuit.resetTdpCapabilityState();
    await yeman.setTdp('dc', 120, { apply: true, save: false });
    assert(oemCalls.at(-1).watts === 120 && runtime.tdpRuntimeStore.get().watts === 65, 'same OEM channel may accept above the former 35W software policy');
    firmwareAcceptedCeiling = 35;
    for (const invalidCapability of [
      { ...validOemCapability, generation: 2 }, // current native lifecycle is generation 1
      { ...validOemCapability, deadlineTick: -1 },
      { ...validOemCapability, minPL1: 0 },
      { ...validOemCapability, selected: false },
      { ...validOemCapability, ready: false, error: 'fixture vendor not verified' },
      { provider: 'default', selected: false, ready: false, minPL1: 2, maxPL1: 200 },
    ]) {
      oemCapability = invalidCapability;
      yeman.setPowerControlDir(dir);
      circuit.resetTdpCapabilityState();
      const before = oemCalls.length;
      let rejected = false;
      try { await yeman.setTdp('ac', 20, { apply: true, save: false }); } catch { rejected = true; }
      assert(rejected && oemCalls.length === before && nonOemCalls.length === 0, 'stale epoch/invalid capability rejected before every hardware route');
    }
    oemCapability = validOemCapability;
    yeman.setPowerControlDir(dir);
    const staleSettingsProbe = yeman.getTdpExecutionCapability().then(() => false, () => true);
    yeman.setPowerControlDir(dir);
    assert(await staleSettingsProbe, 'in-flight descriptor cannot cross a settings directory generation');
    const supersededProbe = yeman.getTdpExecutionCapability(true).then(() => false, () => true);
    const currentProbe = yeman.getTdpExecutionCapability(true);
    assert(await supersededProbe, 'superseded descriptor cannot overwrite the current capability cache');
    assert((await currentProbe).selected, 'newest valid descriptor owns the cache');
    circuit.resetTdpCapabilityState();
    await yeman.setTdp('ac', 25, { apply: true, save: false, source: 'unspecified', retryMsi: true });
    assert(oemCalls[oemCalls.length - 1].explicitRetry === true, 'retry permission is a separate explicit option');
    capabilityFailure = true;
    yeman.setPowerControlDir(dir);
    let rejectedCapability = false;
    try { await yeman.setTdp('ac', 20, { apply: true, save: false }); } catch { rejectedCapability = true; }
    assert(rejectedCapability && nonOemCalls.length === 0, 'failed capability lookup never defaults to MSR');
    // Same CPU in ONEXPLAYER/another brand: shared limits/vendor, never MSI ACPI.
    capabilityFailure = false;
    genericG3eFixture = true;
    // Different machine, same CPU: consume its channel descriptor, never the MSI range.
    oemCapability = { cpuFamily: 'intel-arc-g3-extreme', provider: 'default', selected: false, ready: true, minPL1: 5, maxPL1: 45, machineMaximumKnown: true, rangeSource: 'FAKE-only-channel-range-not-real-ONEXPLAYER-proof' };
    yeman.setPowerControlDir(dir);
    circuit.resetTdpCapabilityState();
    const beforeGeneric = oemCalls.length;
    assert(await yeman.setTdp('ac', 20, { apply: true, save: false, vendor: 'amd' }), 'non-MSI G3E retains generic Intel transport');
    assert(oemCalls.length === beforeGeneric && genericG3eCalls.length === 1, 'non-MSI G3E never uses MSI WMI blocks or shift');
    assert(genericG3eCalls[0].args.join(' ') === 'set 20 --vendor intel', 'G3E CPU identity overrides stale AMD settings on generic transport too');
    assert(yeman.getTdpHardwareMinimum() === 5 && yeman.getTdpHardwareMaximum() === 45, 'same CPU never forces the other machine to MSI 8..35W');
    assert(autofloat.getTdpTarget(120, 'none') === 45, 'float consumes the selected channel ceiling, not a CPU catalog ceiling');
    assert(await yeman.setTdp('ac', 55, { apply: true, save: true }), 'same CPU policy resolves legacy intent on the generic transport');
    assert(genericG3eCalls.length === 2 && genericG3eCalls[1].args.join(' ') === 'set 45 --vendor intel' && oemCalls.length === beforeGeneric, 'non-MSI G3E never receives an MSI OEM recipe');
    assert(await yeman.readTdp('ac') === 55, 'non-MSI G3E also preserves portable intent');
    oemCapability = { ...oemCapability, maxPL1: 65 };
    yeman.setPowerControlDir(dir);
    circuit.resetTdpCapabilityState();
    await yeman.setTdp('ac', 120, { apply: true, save: true });
    assert(genericG3eCalls.at(-1).args.join(' ') === 'set 65 --vendor intel', 'a larger admitted channel range must not be truncated by an old CPU/35W cap');
    assert(await yeman.readTdp('ac') === 120 && oemCalls.length === beforeGeneric, 'different machine limits do not inherit MSI writes or migrate intent');
    (globalThis as any).__mockIpcResponder = previousResponder;
    yeman.setPowerControlDir(dir);
    circuit.resetTdpCapabilityState();
    assert(!(await yeman.getTdpExecutionCapability()).selected, 'ordinary CPU retains the existing route');
    assert(yeman.getTdpHardwareMinimum() === 2, 'ordinary CPU minimum is not narrowed by the G3E fix');
    assert(yeman.resolveTdpTarget(120).effectiveW === 120, 'ordinary CPU legacy mode is not narrowed by the MSI fix');
    assert(autofloat.getTdpTarget(120, 'none') === 120, 'ordinary CPU float maximum remains unchanged');
    console.log('G3E complete model flow: PASS (AC/DC continuous banks, timer-driven float and low-level TDP transport; fake native only)');
    console.log('G3E CPU/OEM isolation: PASS (fake native only, not hardware acceptance)');
    console.log('performance schedule TDP isolation: PASS');
  } finally {
    await (await import('../src/bridge/autofloat')).disableFloat().catch(()=>{});
    const scheduler = await import('../src/scheduler');
    for (const name of scheduler.getScheduledTaskNames()) scheduler.unregisterScheduledTask(name);
    rmSync(dir, { recursive: true, force: true });
    delete (globalThis as any).__mockShellResponder;
    delete (globalThis as any).__mockIpcResponder;
    delete (globalThis as any).__mockShellDryRun;
  }
}

// The resident game-target singleton intentionally stays alive; this isolated process ends after cleanup.
void main().then(() => process.exit(0)).catch((error) => {
  console.error(error);
  process.exitCode = 1;
  process.exit(1); // A failed isolated suite must not hang on resident mock polling.
});
