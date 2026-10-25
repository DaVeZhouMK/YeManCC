// Display-only gate: no reads, no polling, one latest value and at most one timer.
// Control/autofloat reads intentionally retain their own existing cadence.
import type { MonitorSnapshotView } from './monitorData';
export const MONITOR_DISPLAY_INTERVAL_MS = 1000;
export interface DisplayFeedDeps<T> {
  intervalMs?: number;
  now: () => number; // monotonic clock, not the snapshot's wall-clock timestamps
  key: (value: T) => string;
  unavailable?: (value: T) => boolean;
  setTimer: (fn: () => void, ms: number) => unknown;
  clearTimer: (id: unknown) => void;
}
export function createLatestDisplayFeed<T>(initial: T, deps: DisplayFeedDeps<T>) {
  const interval = Math.max(200, Math.min(1000, deps.intervalMs ?? MONITOR_DISPLAY_INTERVAL_MS));
  let latest = initial, latestKey = deps.key(initial), sentKey = latestKey;
  let latestDown = deps.unavailable?.(initial) === true, sentDown = latestDown;
  let lastSent = -Infinity, timer: unknown = null, timerAt = 0, token = 0, dispatching = false, disposed = false;
  // One identity per registration: an old disposer/batch must never own a
  // later registration of the same callback. No wrapper call on publication.
  type Subscription = { readonly listener: (value: T) => void };
  const listeners = new Set<Subscription>();
  // Reuse the ordered batch until membership changes. A dispatch keeps its
  // own batch if a callback invalidates this cache by adding/removing owners.
  let listenerBatch: readonly Subscription[] | null = null;
  const cancel = () => { ++token; if (timer !== null) deps.clearTimer(timer); timer = null; timerAt = 0; };
  const notify = (listener: (value: T) => void, value: T) => { try { listener(value); } catch { /* isolate display consumers */ } };
  function arm(at: number) {
    if (timer !== null && timerAt <= at) return;
    cancel(); timerAt = at; const own = ++token;
    timer = deps.setTimer(() => { if (disposed || own !== token) return; timer = null; timerAt = 0; flush(); }, Math.max(1, Math.ceil(at - deps.now())));
  }
  function pending() {
    if (disposed || !listeners.size || latestKey === sentKey) return;
    const now = deps.now(), urgent = latestDown && !sentDown;
    if (Number.isFinite(now)) arm(now + (urgent ? 1 : Math.max(1, interval - (now - lastSent))));
  }
  function flush() {
    if (disposed || !listeners.size || latestKey === sentKey) { if (timer !== null) cancel(); return; }
    if (dispatching) { pending(); return; }
    const now = deps.now(), urgent = latestDown && !sentDown;
    if (!Number.isFinite(now)) return;
    const remaining = interval - (now - lastSent);
    if (!urgent && remaining > 0) { arm(now + Math.ceil(remaining)); return; }
    cancel(); const value = latest;
    sentKey = latestKey; sentDown = latestDown;
    // First outage clears immediately. Further unavailable frames share the
    // normal 1Hz budget; an outage cannot open another fresh-data budget.
    if (!urgent) lastSent = now;
    dispatching = true;
    try {
      // A coherent publication for existing consumers. Newly added consumers
      // get their initial cache once, not a second live-Set iteration replay.
      for (const subscription of (listenerBatch ??= Array.from(listeners))) {
        if (disposed) break;
        if (listeners.has(subscription)) notify(subscription.listener, value);
      }
    } finally { dispatching = false; }
    pending();
  }
  return {
    push(value: T) {
      if (disposed) return;
      const key = deps.key(value); if (key === latestKey) return;
      latest = value; latestKey = key; latestDown = deps.unavailable?.(value) === true;
      if (!listeners.size) return;
      if (latestKey === sentKey) { if (timer !== null) cancel(); return; }
      flush();
    },
    subscribe(listener: (value: T) => void) {
      if (disposed) return () => {};
      const wasEmpty = !listeners.size, subscription = { listener }; listeners.add(subscription); listenerBatch = null;
      if (wasEmpty) { sentKey = latestKey; sentDown = latestDown; lastSent = latestDown ? -Infinity : deps.now(); }
      const wasDispatching = dispatching; dispatching = true;
      try { notify(listener, latest); } finally { dispatching = wasDispatching; }
      if (!dispatching) pending();
      let released = false;
      return () => { if (released) return; released = true; listeners.delete(subscription); listenerBatch = null; if (!listeners.size && timer !== null) cancel(); };
    },
    dispose() { disposed = true; cancel(); listeners.clear(); listenerBatch = null; },
    pending: () => timer !== null,
    advance: () => flush(),
  };
}
export function monitorSnapshotDisplayKey(value: MonitorSnapshotView | null): string {
  // Public callers may reuse a mutable view; never memoize its identity.
  if (value === null) return 'unavailable';
  const key = [value.session, value.generation, value.seq, value.source,
    value.hwDown, value.top?.hwDown, value.top?.ts, value.top?.cpuUsage, value.top?.cpuUsageValid, value.top?.cpuUsageTs, value.top?.coreUsageTs, value.fps?.ts, value.topActive, value.fpsActive,
    value.top?.batteryStatusTs, value.top?.batteryPowerTs, value.top?.batteryTimeTs,
    value.top?.batteryPowerValid, value.top?.batteryTimeValid, value.top?.batteryPowerEstimated, value.top?.batteryTimeEstimated,
    value.top?.fpsLimit, value.top?.fpsLimitValid, value.top?.fpsLimitTs, value.top?.fpsLimitSource,
    value.top?.cpuPowerValid, value.top?.cpuClockValid, value.top?.cpuTempValid,
    value.top?.cpuVoltageV, value.top?.gpuVoltageV,
    value.top?.gpuName, value.top?.gpuSelectionPinned, value.top?.gpuSelectionPresent, value.top?.gpuUsagePct, value.top?.gpuTempC, value.top?.gpuPowerW, value.top?.gpuClockMhz,
    value.top?.physicalMemoryUsedMb,value.top?.physicalMemoryAvailableMb,value.top?.physicalMemoryTotalMb,value.top?.physicalMemoryLoadPct,
    value.top?.gpuMemorySource,value.top?.gpuMemoryUsedMb,value.top?.gpuMemoryAvailableMb,value.top?.gpuMemoryTotalMb,value.top?.gpuMemoryLoadPct,
    value.top?.networkDownMBps,value.top?.networkUpMBps,value.top?.fileReadMBps,value.top?.fileWriteMBps,value.top?.fileSystemTs,value.top?.fileSystemSource,
    value.top?.fanEnabled, value.top?.fanSpeedPercent, value.top?.fanSpeedValid, value.top?.fanSpeedTs, value.top?.fanSpeedSource,
    value.top?.batteryPercent, value.top?.chargeW, value.top?.remainMin,
    value.top?.tdpW, value.top?.tempC, value.top?.freqMhz, value.top?.hasBattery, value.top?.ac,
    value.top?.batteryPowerSource, value.top?.batteryPowerWindowMs, value.top?.batteryTimeSource,
    value.fps?.fps, value.fps?.fps1, value.fps?.gpu, value.fps?.packagePower, value.fps?.game, value.fps?.pid,
    value.fps?.processCreated, value.fps?.fpsSource, value.fps?.fpsObservedAt, value.fps?.fpsValidUntil, value.fps?.fpsPollPeriodMs,
    value.fps?.hardwareObservedAt, value.fps?.hardwareValidUntil, value.fps?.hardwarePollPeriodMs,
    value.fps?.fps1Source, value.fps?.fps1ObservedAt, value.fps?.fps1ValidUntil, value.fps?.fps1PollPeriodMs,
    value.fps?.frametime, value.fps?.frameTimeSource, value.fps?.frameTimeTs, value.fps?.frameTimeValidUntil, value.fps?.frameTimePollPeriodMs].join('|');
  // Modern publications have an authoritative advancing sequence. The old
  // file/seq-zero path also needs bounded array contents, not only timestamps.
  if (value.source !== 'files' && value.seq > 0) return key;
  const arrayKey = (input: unknown, limit: number, tail = false): string => {
    if (!Array.isArray(input)) return '';
    const count=Math.min(input.length,limit), start=tail?input.length-count:0;
    let text=String(count);
    for(let i=0;i<count;++i){const item=input[start+i];text+='|'+(typeof item==='string'?JSON.stringify(item.slice(0,24)):typeof item==='number'&&Number.isFinite(item)?String(item):'');}
    return text;
  };
  return key+'|cores:'+arrayKey(value.top?.coreUsage,16)+'|labels:'+arrayKey(value.top?.coreLabels,16)+'|frames:'+arrayKey(value.fps?.frameTimes,120,true);
}
