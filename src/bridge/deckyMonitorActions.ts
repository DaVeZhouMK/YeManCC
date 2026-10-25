// Global display switch: change only enabled in the original settings repository.
import type { MonitorOutputPreferences } from './monitorHudModel';
import type { MonitorOutputApplyResult } from './monitorOutputCoordinator';
import type { PowerLifecycleState } from './api';
import type { RelayMutationContext } from './deckyMirrorRelay';
export function createMonitorMirrorActions(deps: {
  allowed: () => boolean;
  power: () => Promise<PowerLifecycleState>;
  read: () => Promise<MonitorOutputPreferences>;
  save: (enabled: boolean) => Promise<void>;
  apply: (preferences: MonitorOutputPreferences) => Promise<MonitorOutputApplyResult>;
}) {
  return { async execute(args: Record<string, unknown>, context: RelayMutationContext) {
    if (Object.keys(args).length !== 1 || typeof args.enabled !== 'boolean') throw Error('MONITOR_MIRROR_INVALID_REQUEST');
    const checkpoint = () => { context.checkpoint(); if (!deps.allowed()) throw Error('MONITOR_MIRROR_NOT_READY'); };
    const gate = async () => {
      checkpoint(); const power = await deps.power(); checkpoint();
      if (power.phase !== 'ready' || !power.hardwareWritesAllowed || power.generation !== context.generation) throw Error('MONITOR_MIRROR_POWER_NOT_READY');
    };
    await gate(); const current = await deps.read(); checkpoint();
    if (typeof context.controlAdmission?.monitor !== 'string' || JSON.stringify(current) !== context.controlAdmission.monitor) throw Error('MONITOR_MIRROR_SOURCE_CHANGED');
    if (current.enabled === args.enabled) return { saved: false, applied: false, notice: '监控总开关未改变' };
    // The repository merges this one field at its serialized commit boundary.
    // Concurrent appearance/output edits must never be replaced by an old snapshot.
    await gate(); await deps.save(args.enabled);
    try {
      const committed = await deps.read();
      if (committed.enabled !== args.enabled) return { saved: true, applied: false, pending: true, notice: '监控总开关已保存；较新的监控选择由 YMCC 处理' };
      if (!deps.allowed()) throw Error('YMCC 监控入口已结束');
      // Accepted display intent outlives QAM visibility, just like the fan switch.
      const result = await deps.apply(committed);
      return { saved: true, applied: result.ok, pending: !result.ok,
        notice: result.ok ? args.enabled ? '监控总开关已开启' : '监控总开关已关闭' : '监控总开关已保存；显示状态待确认：' + result.reason };
    } catch (error) {
      return { saved: true, applied: false, pending: true, notice: '监控总开关已保存；显示状态待确认：' + (error instanceof Error ? error.message : String(error)) };
    }
  } };
}
