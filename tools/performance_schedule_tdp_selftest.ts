import './mock-shell';
import { mkdtempSync, rmSync } from 'fs';
import { tmpdir } from 'os';
import { join } from 'path';

const YEMAN = '1cb8b882-a900-4b9f-9bac-99d151e64441';
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
  (globalThis as any).__mockShellDryRun = true;
  (globalThis as any).__mockShellResponder = (program: string, args: string[]) => {
    const exe = program.toLowerCase();
    if (exe.endsWith('yemantdpctl.exe')) {
      return { exitCode: 6, stdout: unsupportedLog, stderr: '' };
    }
    if (exe === 'powercfg' && args[0]?.toLowerCase() === '/getactivescheme') {
      return { exitCode: 0, stdout: `Power Scheme GUID: ${YEMAN} (YeMan)`, stderr: '' };
    }
    return undefined;
  };
  (globalThis as any).__mockIpcResponder = (cmd: string, args: any) => {
    if (cmd === 'power.activeScheme') return YEMAN;
    if (cmd === 'power.lifecycle') {
      return { phase: 'ready', generation: 1, hardwareWritesAllowed: true, inputReady: true, hibernateAvailable: true };
    }
    if (cmd === 'sys.info') {
      return { cpuName: 'AMD Ryzen Threadripper 3970X', physicalCores: 32, logicalProcs: 64, totalMemoryBytes: 1, acLine: 1, powerMode: 'ac', hasBattery: false };
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
    const schedule = await import('../src/bridge/performanceSchedule');
    settings.setSettingsDirectory(dir);
    yeman.setPowerControlDir(dir);
    autofloat.setAutofloatPowerControlDir(dir);

    const warnings: string[] = [];
    const offWarning = schedule.onPerformanceScheduleWarning((warning) => warnings.push(warning.message));
    const config = schedule.defaultPerformanceScheduleConfig();
    config.configured = true;
    config.enabled = true;
    config.active.ac = 'balanced';

    const autoApplied = await schedule.applyPerformanceSchedule('ac', 'balanced', config);
    assert(autoApplied, 'TDP rc=6 不应让手动→自动切换返回失败');
    assert(autofloat.getFloatInfo().enabled, 'TDP rc=6 后 CPU 浮动仍应启动');
    assert((await schedule.getPerformanceScheduleOwnership()) === 'auto', '自动模式必须独立持久化');
    const warning = schedule.getPerformanceScheduleWarning();
    assert(warning?.code === 'tdp-unsupported', 'Desktop-17h 应给出明确的不支持提示');
    assert(warning.message.includes('不会阻断 CPU 调度'), '提示必须明确 CPU 调度仍继续');
    const writes = (globalThis as any).__mockRegistryWrites as Array<{ name: string }>;
    assert(writes.some((item) => item.name === 'ACSettingIndex'), 'TDP 失败后 AC CPU 参数仍应写入');
    assert(writes.some((item) => item.name === 'DCSettingIndex'), 'TDP 失败后 DC CPU 参数仍应写入');

    await schedule.disablePerformanceSchedule(config);
    assert(!autofloat.getFloatInfo().enabled, '自动→手动必须停止 CPU 浮动控制');
    assert((await schedule.getPerformanceScheduleOwnership()) === 'manual', '自动→手动必须独立持久化');

    const autoAppliedAgain = await schedule.applyPerformanceSchedule('ac', 'balanced', config);
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
