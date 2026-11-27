// Global display switch: change only enabled in the original settings repository.
import type { MonitorOutputPreferences } from './monitorHudModel';
import type { MonitorOutputApplyResult } from './monitorOutputCoordinator';
import type { PowerLifecycleState } from './api';
import type { RelayMutationContext } from './deckyMirrorRelay';
import type { SharedPowerLifecycleSnapshot } from './powerLifecycleState';
export function createMonitorMirrorActions(deps: {
  allowed: () => boolean;
  power: (context: RelayMutationContext) => Promise<PowerLifecycleState>;
  read: () => Promise<MonitorOutputPreferences>;
  save: (enabled: boolean) => Promise<void>;
  scope: (snapshot?: SharedPowerLifecycleSnapshot) => string | null;
  observePower: (listener: (snapshot: SharedPowerLifecycleSnapshot) => void) => () => void;
  observePreferences: (listener: (preferences: MonitorOutputPreferences) => void) => () => void;
  apply: (preferences: MonitorOutputPreferences, current: () => boolean) => Promise<MonitorOutputApplyResult>;
}) {
  let retire: (() => void) | undefined;
  function continueSaved(preferences: MonitorOutputPreferences) {
    const saved = JSON.stringify(preferences);
    let live = true, reading = false, stopObserving: (() => void) | undefined, stopPreferences: (() => void) | undefined;
    const stop = () => {
      live = false;
      stopObserving?.();
      stopPreferences?.();
      if (retire === stop) retire = undefined;
    };
    retire = stop;
    const resume = async () => {
      const scope = deps.scope();
      if (!live || reading || !scope || !deps.allowed()) return;
      reading = true;
      const current = () => live && deps.allowed() && deps.scope() === scope;
      let applying = false;
      try {
        const latest = await deps.read();
        if (!current()) return;
        if (JSON.stringify(latest) !== saved) { stop(); return; }
        applying = true;
        await deps.apply(latest, current);
      } catch { /* A failed display operation is settled by its original owner. */ }
      finally {
        reading = false;
        // A changed scope can retire a read, but never replay a submitted apply.
        if (applying || current()) stop();
        else if (live && deps.scope()) void resume();
      }
    };
    stopPreferences = deps.observePreferences(latest => { if (JSON.stringify(latest) !== saved) stop(); });
    stopObserving = deps.observePower(snapshot => { if (deps.scope(snapshot)) void resume(); });
    if (!live) { stopObserving(); stopPreferences(); }
  }
  return { stop: () => retire?.(), async execute(args: Record<string, unknown>, context: RelayMutationContext) {
    if (Object.keys(args).length !== 1 || typeof args.enabled !== 'boolean') throw Error('MONITOR_MIRROR_INVALID_REQUEST');
    const checkpoint = () => { context.checkpoint(); if (!deps.allowed()) throw Error('MONITOR_MIRROR_NOT_READY'); };
    const gate = async () => {
      checkpoint(); const power = await deps.power(context); checkpoint();
      if (power.phase !== 'ready' || !power.hardwareWritesAllowed || power.generation !== context.generation) throw Error('MONITOR_MIRROR_POWER_NOT_READY');
    };
    await gate(); const current = await deps.read(); checkpoint();
    if (typeof context.controlAdmission?.monitor !== 'string' || JSON.stringify(current) !== context.controlAdmission.monitor) throw Error('MONITOR_MIRROR_SOURCE_CHANGED');
    if (current.enabled === args.enabled) return { saved: false, applied: false, notice: '监控总开关未改变' };
    // The repository merges this one field at its serialized commit boundary.
    // Concurrent appearance/output edits must never be replaced by an old snapshot.
    await gate();
    const scope = deps.scope();
    // Do not tie a committed setting to the sidebar's visibility or connection.
    if (!scope) throw Error('MONITOR_MIRROR_POWER_NOT_READY');
    await deps.save(args.enabled);
    retire?.();
    try {
      const committed = await deps.read();
      if (committed.enabled !== args.enabled) return { saved: true, applied: false, pending: true, notice: '监控总开关已保存；较新的监控选择由 YMCC 处理' };
      if (!deps.allowed()) throw Error('YMCC 监控入口已结束');
      const current = () => deps.allowed() && deps.scope() === scope;
      if (!current()) {
        continueSaved(committed);
        return { saved: true, applied: false, pending: true, notice: '监控总开关已保存；等待当前电源状态确认后应用' };
      }
      const result = await deps.apply(committed, current);
      return { saved: true, applied: result.ok, pending: !result.ok,
        notice: result.ok ? args.enabled ? '监控总开关已开启' : '监控总开关已关闭' : '监控总开关已保存；显示状态待确认：' + result.reason };
    } catch (error) {
      return { saved: true, applied: false, pending: true, notice: '监控总开关已保存；显示状态待确认：' + (error instanceof Error ? error.message : String(error)) };
    }
  } };
}
