// Shared snapshot facts for YMCC's internal readers and the HUD. This is a pure
// projection of the one admitted data source: no IO, timers, TTL or new samples.
import type { MonitorSnapshotView } from './monitorData';
import { fpsDisplayFresh } from './monitorFrameTime';
import { MAX_CORE_USAGE_ITEMS } from './monitorCoreTiles';

// Optional liveness evidence must be a real past millisecond stamp. Preserve
// numeric-string compatibility at wire admission, never coerce objects/bools.
export function monitorProofTimestamp(value: unknown, now: number): number | null {
  if (typeof value !== 'number' && typeof value !== 'string') return null;
  const stamp = typeof value === 'number' ? value : Number(value);
  return Number.isSafeInteger(stamp) && stamp > 0 && stamp <= now ? stamp : null;
}
export function monitorProofFresh(value: unknown, now: number, lifetimeMs: number): value is number {
  return typeof value === 'number' && Number.isSafeInteger(value) && value > 0
    && now >= value && now - value < lifetimeMs;
}

export function monitorNumericValue(value: unknown, allowNegative = false): number | null {
  return typeof value === 'number' && Number.isFinite(value) && (allowNegative || value >= 0) ? value : null;
}

type MonitorTop = MonitorSnapshotView['top'] | undefined;
// Small shared predicates also serve the full HUD without an intermediate
// facts/scalar object. Keep each channel's original timestamp and proof rules.
// Internal displays consume the already freshness-projected top ref. Its
// hwDown/validity flags remain authoritative; never turn an unavailable 0 into
// a live temperature or grant it a new timestamp. This function performs no IO.
export function monitorCpuTemperature(top: MonitorTop): number | null {
  if (!top || top.hwDown || top.cpuTempValid === false) return null;
  const value = monitorNumericValue(top.tempC);
  return value !== null && value <= 150 ? value : null;
}

export function monitorHardwareFresh(top: MonitorTop, now: number, hwDown: boolean | undefined): boolean {
  return !!top && !hwDown && !top.hwDown && top.ts > 0 && now - top.ts >= 0 && now - top.ts < 6000;
}
export function monitorCoreFresh(top: MonitorTop, now: number, topFresh: boolean): boolean {
  return top?.coreUsageTs === undefined ? topFresh :
    typeof top.coreUsageTs === 'number' && Number.isFinite(top.coreUsageTs) && top.coreUsageTs > 0 && now - top.coreUsageTs >= 0 && now - top.coreUsageTs < 6000;
}
export function monitorCpuHasProof(top: MonitorTop): boolean {
  return !!top && (top.cpuUsageValid !== undefined || top.cpuUsageTs !== undefined);
}
export function monitorCpuFresh(top: MonitorTop, now: number, topFresh: boolean, independentCpu: boolean): boolean {
  return independentCpu ? top?.cpuUsageValid === true && typeof top.cpuUsage === 'number' && Number.isFinite(top.cpuUsage) && top.cpuUsage >= 0 && top.cpuUsage <= 100 &&
    typeof top.cpuUsageTs === 'number' && Number.isSafeInteger(top.cpuUsageTs) && top.cpuUsageTs > 0 && now >= top.cpuUsageTs && now - top.cpuUsageTs < 6000 : topFresh;
}
export function monitorBatteryStatusFresh(top: MonitorTop, now: number): boolean {
  return top?.hasBattery === true && typeof top.batteryStatusTs === 'number'
    && Number.isFinite(top.batteryStatusTs) && top.batteryStatusTs > 0 && now - top.batteryStatusTs >= 0 && now - top.batteryStatusTs < 6000;
}
export function monitorBatteryPowerFresh(top: MonitorTop, now: number): boolean {
  return top?.hasBattery === true && top.batteryPowerValid === true && typeof top.batteryPowerTs === 'number'
    && Number.isFinite(top.batteryPowerTs) && top.batteryPowerTs > 0 && now - top.batteryPowerTs >= 0 && now - top.batteryPowerTs < 6000;
}
export function monitorBatteryTimeFresh(top: MonitorTop, now: number, batteryFresh: boolean): boolean {
  return batteryFresh && top?.batteryTimeValid === true && top.ac === 0 && top.chargeW < -.05
    && typeof top.batteryTimeTs === 'number' && Number.isFinite(top.batteryTimeTs) && top.batteryTimeTs > 0
    && now - top.batteryTimeTs >= 0 && now - top.batteryTimeTs < 6000;
}
export function monitorFpsLimitFresh(top: MonitorTop, now: number): boolean {
  return top?.fpsLimitValid === true && top.fpsLimitSource === 'rtss-global'
    && typeof top.fpsLimit === 'number' && Number.isFinite(top.fpsLimit) && top.fpsLimit >= 0 && top.fpsLimit <= 1000000
    && typeof top.fpsLimitTs === 'number' && Number.isSafeInteger(top.fpsLimitTs) && top.fpsLimitTs > 0
    && now - top.fpsLimitTs >= 0 && now - top.fpsLimitTs < 6000;
}
export function monitorFanFresh(top: MonitorTop, now: number): boolean {
  return top?.fanEnabled === true && top.fanSpeedValid === true && top.fanSpeedSource === 'ymcc-fan-target'
    && typeof top.fanSpeedPercent === 'number' && Number.isFinite(top.fanSpeedPercent) && top.fanSpeedPercent >= 0 && top.fanSpeedPercent <= 100
    && typeof top.fanSpeedTs === 'number' && Number.isSafeInteger(top.fanSpeedTs) && top.fanSpeedTs > 0 && now >= top.fanSpeedTs && now - top.fanSpeedTs < 6000;
}
export function monitorFileSystemFresh(top: MonitorTop, now: number): boolean {
  return top?.fileSystemSource === 'windows-filesystem-disk-activity' && typeof top.fileSystemTs === 'number' && Number.isSafeInteger(top.fileSystemTs) && top.fileSystemTs > 0 && now >= top.fileSystemTs && now - top.fileSystemTs < 6000;
}

interface MonitorSnapshotScalars {
  cpuUsage?: number | null;
  batteryPercent?: number | null;
  batteryPower?: number | null;
  batteryTime?: number | null;
  diskRead: number | null;
  diskWrite: number | null;
}

// Preserve each family's own proof, including legacy payloads. Internal scalar
// readers need neither per-core normalization nor frame-curve/memory formatting.
// A fresh result owns its scalar object; it never mutates or retains the source.
export function projectMonitorSnapshotSignals(snapshot: MonitorSnapshotView | null, now: number) {
  const top = snapshot?.top, fps = snapshot?.fps;
  const topFresh = monitorHardwareFresh(top, now, snapshot?.hwDown);
  const fpsFresh = fpsDisplayFresh(fps, now, snapshot?.hwDown === true);
  const coreFresh = monitorCoreFresh(top, now, topFresh);
  // Normalization preserves unavailable slots, so freshness depends on bounded
  // slot count here, exactly as in the existing display contract.
  const hasCoreSlots = coreFresh && Array.isArray(top?.coreUsage) && Math.min(top.coreUsage.length, MAX_CORE_USAGE_ITEMS) > 0;
  const independentCpu = monitorCpuHasProof(top);
  const cpuUsageFresh = monitorCpuFresh(top, now, topFresh, independentCpu);
  const batteryStatusFresh = monitorBatteryStatusFresh(top, now);
  const batteryFresh = monitorBatteryPowerFresh(top, now);
  const batteryTimeFresh = monitorBatteryTimeFresh(top, now, batteryFresh);
  const fpsLimitFresh = monitorFpsLimitFresh(top, now);
  const fanFresh = monitorFanFresh(top, now);
  const fileFresh = monitorFileSystemFresh(top, now);
  const values: MonitorSnapshotScalars = {
    diskRead: fileFresh ? monitorNumericValue(top!.fileReadMBps) : null,
    diskWrite: fileFresh ? monitorNumericValue(top!.fileWriteMBps) : null,
  };
  if (topFresh) {
    values.cpuUsage = monitorNumericValue(top!.cpuUsage);
    if (top!.hasBattery) {
      values.batteryPercent = monitorNumericValue(top!.batteryPercent);
      values.batteryPower = top!.batteryPowerValid === false ? null : monitorNumericValue(top!.chargeW, true);
      values.batteryTime = monitorNumericValue(top!.remainMin);
    }
  }
  if (independentCpu) values.cpuUsage = cpuUsageFresh ? top!.cpuUsage : null;
  if (top?.batteryStatusTs !== undefined) values.batteryPercent = batteryStatusFresh && top.batteryPercent >= 0 && top.batteryPercent <= 100 ? monitorNumericValue(top.batteryPercent) : null;
  if (top?.batteryPowerValid !== undefined) values.batteryPower = batteryFresh ? monitorNumericValue(top.chargeW, true) : null;
  if (top?.batteryTimeValid !== undefined) values.batteryTime = batteryTimeFresh ? monitorNumericValue(top.remainMin) : null;
  return { values, topFresh, fpsFresh, coreFresh, independentCpu, cpuUsageFresh, batteryStatusFresh, batteryFresh, batteryTimeFresh, fpsLimitFresh, fanFresh, fileFresh,
    stale: !topFresh && !fpsFresh && !cpuUsageFresh && !hasCoreSlots && !batteryFresh && !batteryStatusFresh && !fpsLimitFresh && !fanFresh && !(fileFresh && (values.diskRead !== null || values.diskWrite !== null)) };
}
