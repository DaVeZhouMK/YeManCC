import './mock-shell';
import { mkdtempSync, rmSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';

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
    assert(writes.some((item) => item.name === 'ACSettingIndex'), 'TDP 失败后 AC CPU 参数仍应写入');
    assert(writes.some((item) => item.name === 'DCSettingIndex'), 'TDP 失败后 DC CPU 参数仍应写入');
    assert(!writes.some((item) => item.path.endsWith(`\\${CPU_MAX_STATE}`)), 'CPU 浮动接管时不得重复应用固定 CPU 挡位');

    // 0 = 不锁帧：自动档位不得继续启动或继承 CPU/TDP 浮动。
    config.profiles.dc.balanced.fpsTarget = 0;
    config.profiles.dc.balanced.cpuTarget = 'aggressive';
    config.profiles.dc.balanced.tdpStrategy = 'aggressive';
    writes.length = 0;
    const noLockApplied = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(noLockApplied, '不锁帧档位仍应完成自动模式应用');
    assert(!autofloat.getFloatInfo().enabled, '不锁帧不得启动 CPU/TDP 浮动');
    assert(autofloat.getFloatInfo().target === 0, '不锁帧必须保留真实目标值 0');
    assert(writes.length === 12, `固定 CPU 挡位应只写当前 DC 侧 12 项，实际=${writes.length}`);
    assert(writes.every((item) => item.name === 'DCSettingIndex'), 'DC 自动挡位不得覆盖 AC 保存值');
    const valueFor = (setting: string) => writes.find((item) => item.path.endsWith(`\\${setting}`))?.value;
    assert(valueFor(CPU_FREQ0) === 3600, '自动模式必须应用用户保存的 DC 主频');
    assert(valueFor(CPU_BOOST) === 2, '自动模式必须应用用户保存的 DC 睿频');
    assert(valueFor(CPU_SCHED0) === 23, '自动模式必须应用用户保存的 DC 积极性');
    assert(valueFor(CPU_MIN0) === 77, '自动模式必须同步用户保存的最小处理器状态');
    assert(valueFor(CPU_MAX_STATE) === 100, '自动模式必须恢复最大处理器状态 100%');
    assert(valueFor(CPU_THROTTLE) === 2, '自动模式必须同步频率对应的节流状态');
    assert(templateScriptCalls === 0, '自动模式不得运行任何 CPU 模板脚本');

    config.profiles.dc.balanced.fpsTarget = 45;
    const relockApplied = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(relockApplied, '从不锁帧切回有效目标后应能重新应用自动浮动');
    assert(autofloat.getFloatInfo().enabled, '有效 FPS 目标应重新启动浮动');

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
    assert(blockedProfileAttempts === 2, `CPU 挡位应重试一次，实际=${blockedProfileAttempts}`);

    failNextRequiredProfileWrite = true;
    let strictFailureObserved = false;
    try {
      await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    } catch (error) {
      strictFailureObserved = String(error).includes('CPU 挡位参数写入失败');
    }
    assert(strictFailureObserved, '固定 CPU 主类参数失败不得被吞掉或显示假成功');
    assert(templateScriptCalls === 0, '失败重试也不得回退运行模板脚本');

    const autoAppliedAgain = await schedule.applyPerformanceSchedule('dc', 'balanced', config);
    assert(autoAppliedAgain, '相同 TDP 不支持错误再次出现时仍不得阻断自动模式');
    assert(warnings.length === 1, '同一平台兼容性提示每次运行只应发布一次');
    await schedule.disablePerformanceSchedule(config);
    offWarning();
    console.log('performance schedule TDP isolation: PASS');
  } finally {
    rmSync(dir, { recursive: true, force: true });
    delete (globalThis as any).__mockShellResponder;
    delete (globalThis as any).__mockIpcResponder;
    delete (globalThis as any).__mockShellDryRun;
  }
}

void main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
