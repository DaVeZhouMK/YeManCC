// Measurement freshness, separate from transport/HWND leases. Pure consumption.
import type { HudData, HudScalarId } from './monitorHudModel';
import { fpsDisplayFresh } from './monitorFrameTime';
export const HUD_FRESH_FAMILIES = ['hardware','cpuUsage','fps','fps1','cores','batteryStatus','batteryPower','batteryTime','fpsLimit','fan','fileSystem','gpu','frametime'] as const;
export type HudFreshFamily = typeof HUD_FRESH_FAMILIES[number];
export type HudFreshUntil = Partial<Record<HudFreshFamily, number>>;
const hardware: HudScalarId[] = ['cpuVoltage','gpuVoltage','cpuPower','cpuTemp','cpuPeak','cpuClock','gpuPower','gpuClock','gpuTemp','vramUsed','vramTotal','vramAvailable','vramUsage','ramUsed','ramTotal','ramAvailable','ramUsage','netDown','netUp'];
export function boundedHudFreshUntil(raw: unknown): HudFreshUntil {
  const result: HudFreshUntil = {};
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return result;
  for (const key of HUD_FRESH_FAMILIES) {
    const value = (raw as HudFreshUntil)[key];
    if (typeof value === 'number' && Number.isSafeInteger(value) && value > 0) result[key] = value;
  }
  return result;
}
export function ageHudData(data: HudData, now: number): HudData {
  if (data.freshUntil === undefined) return data; // Compatible legacy packets/explicit fixtures.
  const alive = (key: HudFreshFamily) => {
    const end = data.freshUntil?.[key];
    return typeof end === 'number' && Number.isSafeInteger(end) && end > now && end <= now + 6000;
  };
  // Most admitted frames are already fresh or already cleared. Copy only on
  // the first actual field change; never mutate the publisher's values.
  let values = data.values;
  const clear = (keys: HudScalarId[]) => { for (const key of keys) if (values[key] !== null) {
    if (values === data.values) values = { ...data.values };
    values[key] = null;
  } };
  if (!alive('hardware')) clear(hardware);
  if (!(Object.prototype.hasOwnProperty.call(data.freshUntil, 'cpuUsage') ? alive('cpuUsage') : alive('hardware'))) clear(['cpuUsage']);
  if (!alive('fps')) clear(['fps','fps1','frametime']);
  if (data.freshUntil.fps1 !== undefined && !alive('fps1')) clear(['fps1']);
  const frameAlive = alive('fps') && (data.freshUntil.frametime === undefined || alive('frametime'));
  if (!frameAlive) clear(['frametime']);
  if (Object.prototype.hasOwnProperty.call(data.freshUntil, 'gpu') ? !alive('gpu') : !alive('fps')) clear(['gpuUsage']);
  if (!alive('fpsLimit')) clear(['fpsLimit']);
  if (!alive('fan')) clear(['fanSpeed']);
  if (!alive('fileSystem')) clear(['diskRead','diskWrite']);
  if (!alive('batteryStatus')) clear(['batteryPercent']);
  const powerAlive = alive('batteryPower');
  const watts = values.batteryPower, minutes = values.batteryTime;
  const timeAlive = powerAlive && alive('batteryTime') && typeof watts === 'number' && Number.isFinite(watts) && watts < -.05
    && typeof minutes === 'number' && Number.isFinite(minutes) && minutes >= 0 && minutes <= 10080;
  if (!powerAlive) clear(['batteryPower']);
  if (!timeAlive) clear(['batteryTime']);
  const stale = data.stale || !HUD_FRESH_FAMILIES.some(alive);
  const batteryPowerEstimated = powerAlive && data.batteryPowerEstimated === true;
  const batteryTimeEstimated = timeAlive && data.batteryTimeEstimated === true;
  const coreUsage = alive('cores') || data.coreUsage?.length === 0 ? data.coreUsage : [];
  const coreLabels = alive('cores') || data.coreLabels?.length === 0 ? data.coreLabels : [];
  const coreClocks = alive('hardware') || data.coreClocks?.length === 0 ? data.coreClocks : [];
  const frameTimes = frameAlive || data.frameTimes?.length === 0 ? data.frameTimes : [];
  if (values === data.values && stale === data.stale && batteryPowerEstimated === data.batteryPowerEstimated &&
    batteryTimeEstimated === data.batteryTimeEstimated && coreUsage === data.coreUsage && coreLabels === data.coreLabels &&
    coreClocks === data.coreClocks && frameTimes === data.frameTimes) return data;
  return { ...data, values, stale, batteryPowerEstimated, batteryTimeEstimated, coreUsage, coreLabels, coreClocks, frameTimes };
}
// A temporary whole-HW miss must not replace a still-valid display receipt
// with nulls. Retain only the existing proof/deadline in the same native session.
// Fresh HW with an individually missing sensor, session changes and legacy
// unbound packets do not inherit values. Control/health never sees this lease.
function retainHardwareOnGap(previous: HudData | null, next: HudData, now: number): HudData {
  const end=previous?.freshUntil?.hardware;
  if (!previous || previous.source!=='ymcc-uniform' || next.source!=='ymcc-uniform' ||
      !Number.isSafeInteger(next.hardwareSession) || !(next.hardwareSession!>0) || previous.hardwareSession!==next.hardwareSession ||
      next.freshUntil?.hardware!==undefined || typeof end!=='number' || !Number.isSafeInteger(end) || end<=now || end>now+6000) return next;
  let values=next.values;
  const retain=(key: HudScalarId)=>{if(previous.values[key]!=null){if(values===next.values)values={...next.values};values[key]=previous.values[key];}};
  for(const key of hardware)retain(key);
  const gpuEnd=previous.freshUntil?.gpu;
  const gpu=next.freshUntil?.gpu===undefined && typeof gpuEnd==='number' && Number.isSafeInteger(gpuEnd) && gpuEnd>now && gpuEnd<=now+6000;
  if(gpu)retain('gpuUsage');
  if(values===next.values)return next;
  return {...next,stale:false,values,cpuName:next.cpuName||previous.cpuName,gpuName:gpu?(next.gpuName||previous.gpuName):next.gpuName,
    freshUntil:{...next.freshUntil,hardware:end,...(gpu?{gpu:gpuEnd}:{})}};
}
export function createHudAgeLease(deps: { now(): number; emit(value: HudData): void; setTimer(fn: () => void, ms: number): unknown; clearTimer(id: unknown): void }) {
  let latest: HudData | null = null, latestAt=-Infinity, revision=0, timer: unknown = null, timerAt = 0, token = 0, disposed = false;
  const cancel = () => { ++token; if (timer !== null) deps.clearTimer(timer); timer = null; timerAt = 0; };
  function schedule(now: number) {
    let end = Infinity;
    // Fixed whitelist, no Object.values/filter arrays or unknown-key wakes.
    for (const family of HUD_FRESH_FAMILIES) {
      const value = latest?.freshUntil?.[family];
      if (typeof value === 'number' && Number.isSafeInteger(value) && value > now && value <= now + 6000 && value < end) end = value;
    }
    if (!Number.isFinite(now) || end === Infinity) { if (timer !== null) cancel(); return; }
    // An earlier pending wake is safe for newer data. A newly EARLIER field
    // deadline must preempt it; transport repeats must not keep rearming it.
    if (timer !== null && timerAt > now && timerAt <= end) return;
    cancel(); timerAt = end; const own = ++token;
    timer = deps.setTimer(() => { if (disposed || own !== token) return; timer = null; timerAt = 0; refresh(); }, Math.max(1, Math.ceil(end - now)));
  }
  function refresh() {
    if (disposed || !latest) return;
    // Commit expiry before callbacks so a later clock correction cannot
    // inherit values that were already withdrawn. Reentrant accepts own latest.
    const now = deps.now(), ownRevision=revision; latestAt = now; latest = ageHudData(latest, now); deps.emit(latest);
    if (!disposed && revision===ownRevision) schedule(now);
  }
  return { accept(data: HudData) { if (disposed) return; ++revision; const now=deps.now(); latest = now>=latestAt?retainHardwareOnGap(latest,data,now):data; latestAt=now; refresh(); },
    dispose() { disposed = true; ++revision; cancel(); latest = null; }, pending: () => timer !== null };
}

// Health-down does not invalidate independent Windows channels or open a new
// high-frequency display budget. Check existing proofs only, never acquire.
function computeMonitorSnapshotHasFreshData(view: import('./monitorData').MonitorSnapshotView | null, now: number): boolean {
  if (!view) return false;
  const fresh = (ts: unknown) => typeof ts === 'number' && Number.isFinite(ts) && ts > 0 && ts <= now && now - ts < 6000;
  const top = view.top;
  if ((!view.hwDown && !top?.hwDown && fresh(top?.ts)) || fpsDisplayFresh(view.fps, now, view.hwDown)) return true;
  if (top?.cpuUsageValid === true && typeof top.cpuUsage === 'number' && Number.isFinite(top.cpuUsage) && top.cpuUsage >= 0 && top.cpuUsage <= 100 && fresh(top.cpuUsageTs)) return true;
  if (fresh(top?.coreUsageTs) && Array.isArray(top?.coreUsage)) {
    for (let i=0; i<Math.min(16,top.coreUsage.length); ++i) if (typeof top.coreUsage[i] === 'number' && Number.isFinite(top.coreUsage[i]) && top.coreUsage[i]! >= 0 && top.coreUsage[i]! <= 100) return true;
  }
  if (top?.fpsLimitValid === true && top.fpsLimitSource === 'rtss-global' && typeof top.fpsLimit === 'number'
    && Number.isFinite(top.fpsLimit) && top.fpsLimit >= 0 && top.fpsLimit <= 1000000 && fresh(top.fpsLimitTs)) return true;
  if (top?.fileSystemSource==='windows-filesystem-disk-activity' && fresh(top.fileSystemTs) && [top.fileReadMBps,top.fileWriteMBps].some(v=>typeof v==='number'&&Number.isFinite(v)&&v>=0&&v<=1e6))return true;
  if (top?.fanEnabled === true && top.fanSpeedValid === true && top.fanSpeedSource === 'ymcc-fan-target' && typeof top.fanSpeedPercent === 'number'
    && Number.isFinite(top.fanSpeedPercent) && top.fanSpeedPercent >= 0 && top.fanSpeedPercent <= 100 && fresh(top.fanSpeedTs)) return true;
  if (top?.hasBattery !== true || typeof top.batteryPercent !== 'number' || !Number.isFinite(top.batteryPercent) || top.batteryPercent < 0 || top.batteryPercent > 100) return false;
  return fresh(top.batteryStatusTs) || (top.batteryPowerValid === true && Number.isFinite(top.chargeW) && Math.abs(top.chargeW) <= 250 && fresh(top.batteryPowerTs));
}


// Public callers may update the same mutable view within one clock tick.
// Production sharing is scoped to publisher-owned immutable views in monitorData.
export function monitorSnapshotHasFreshData(view: import('./monitorData').MonitorSnapshotView | null, now: number): boolean {
  return computeMonitorSnapshotHasFreshData(view, now);
}
