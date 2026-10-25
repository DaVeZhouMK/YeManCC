// Admission/geometry only. Sampling is owned by YMCC's existing native worker.
export const FRAME_TIME_SOURCE = 'hwinfo-presented-avg' as const;
export type FrameTimeSource = typeof FRAME_TIME_SOURCE | 'rtss-current-frame';
export const MONITOR_MISSING_GRACE_MS = 5000;
export const MONITOR_CURVE_DISPLAY_MS = 200;
// Feature detection only: presence of modern publisher metadata is not a
// validity, hardware freshness or GPU identity proof. Keep parser and consumer
// compatibility classification aligned without allocations or source reads.
type FpsMetadataPacket = { fpsSource?: unknown; fpsObservedAt?: unknown; fpsValidUntil?: unknown; fpsPollPeriodMs?: unknown; processCreated?: unknown };
export function hasExplicitFpsMetadata(packet: FpsMetadataPacket | null | undefined): boolean {
  return !!packet && (packet.fpsSource !== undefined || packet.fpsObservedAt !== undefined ||
    packet.fpsValidUntil !== undefined || packet.fpsPollPeriodMs !== undefined || packet.processCreated !== undefined);
}
export function monitorProducerLifetimeMs(period: unknown): number | null {
  return typeof period==='number' && Number.isSafeInteger(period) && period>=0 && period<=60000
    ? Math.max(MONITOR_MISSING_GRACE_MS,Math.min(6000,period+3000)) : null;
}
function observationDeadline(at: number, end: unknown, period: unknown, rtss: boolean): number {
  const lifetime=rtss ? period===0 ? MONITOR_MISSING_GRACE_MS : null : monitorProducerLifetimeMs(period);
  if(lifetime===null || typeof end!=='number' || !Number.isSafeInteger(end))return 0;
  if(end===at+lifetime)return end;
  // Older installed publishers used period+2s. Admit their original shorter
  // deadline without extending it locally or treating a reread as a sample.
  const oldLifetime=!rtss && typeof period==='number'
    ? Math.max(MONITOR_MISSING_GRACE_MS,Math.min(6000,period+2000)) : null;
  return oldLifetime!==null && end===at+oldLifetime ? end : 0;
}
// New publishers supply a real observation proof. Older publishers retain the
// old bounded compatibility window; display grace never changes control health.
export function fpsDisplayFresh(packet: { ts?: unknown; fps?: unknown; pid?: unknown; fpsSource?: unknown; fpsObservedAt?: unknown; fpsValidUntil?: unknown; fpsPollPeriodMs?: unknown } | null | undefined, now: number, hwDown: boolean): boolean {
  if (!packet || typeof packet.ts !== 'number' || !Number.isSafeInteger(packet.ts) || packet.ts <= 0 || packet.ts > now ||
      typeof packet.fps !== 'number' || !Number.isFinite(packet.fps) || packet.fps < 0 || packet.fps > 1000000 ||
      typeof packet.pid !== 'number' || !Number.isSafeInteger(packet.pid) || packet.pid <= 0) return false;
  if (packet.fpsSource !== undefined || packet.fpsObservedAt !== undefined || packet.fpsValidUntil !== undefined || packet.fpsPollPeriodMs !== undefined) {
    const ts = packet.fpsObservedAt;
    if ((packet.fpsSource !== 'rtss' && packet.fpsSource !== 'hwinfo-presented' && packet.fpsSource !== 'hwinfo-displayed') || typeof ts !== 'number' || !Number.isSafeInteger(ts) || ts<=0 || ts>packet.ts || ts>now) return false;
    if (packet.fpsValidUntil !== undefined || packet.fpsPollPeriodMs !== undefined)
      return now<observationDeadline(ts,packet.fpsValidUntil,packet.fpsPollPeriodMs,packet.fpsSource==='rtss');
    return now-ts < MONITOR_MISSING_GRACE_MS;
  }
  return !hwDown && now-packet.ts < 6000;
}
// Exact HWiNFO statistic with its own bounded producer proof. Missing values
// retain their last genuine proof only to its deadline; repeated delivery never renews it.
// A newer hardware LOW may follow the last RTSS frame. Its upper bound is the
// coherent cache collection time; three-argument callers keep the packet bound.
export function fps1DisplayFresh(packet: (FpsMetadataPacket & { ts?: unknown; fps1?: unknown; fps1Source?: unknown; fps1ObservedAt?: unknown; fps1ValidUntil?: unknown; fps1PollPeriodMs?: unknown }) | null | undefined, now: number, hwDown: boolean, snapshotAt?: number): boolean {
  if (!packet || typeof packet.fps1 !== 'number' || !Number.isFinite(packet.fps1) || packet.fps1 < 0 || packet.fps1 > 1000000) return false;
  // Modern frame receipts do not prove the age/source of a latched LOW. Only
  // genuinely old packets keep the caller-bounded legacy FPS compatibility.
  if (packet.fps1Source === undefined && packet.fps1ObservedAt === undefined && packet.fps1ValidUntil === undefined && packet.fps1PollPeriodMs === undefined) return !hwDown && !hasExplicitFpsMetadata(packet);
  if (packet.fps1Source !== 'hwinfo-presented-1percent-low' && packet.fps1Source !== 'hwinfo-displayed-1percent-low') return false;
  if (snapshotAt !== undefined && !(Number.isSafeInteger(snapshotAt) && snapshotAt>0 && snapshotAt<=now &&
    typeof packet.ts==='number' && Number.isSafeInteger(packet.ts) && packet.ts>0 && packet.ts<=snapshotAt)) return false;
  const at=packet.fps1ObservedAt,end=packet.fps1ValidUntil,upper=snapshotAt===undefined?packet.ts:snapshotAt;
  if (!(typeof at==='number' && Number.isSafeInteger(at) && at>0 && at<=now &&
    typeof packet.ts==='number' && typeof upper==='number' && at<=upper &&
    typeof end==='number' && Number.isSafeInteger(end) && now<end)) return false;
  return observationDeadline(at,end,packet.fps1PollPeriodMs,false)>now;
}
export function validFrameTime(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value) && value > 0 && value <= 10000;
}
// Trust only normalization-owned frozen histories. Being frozen externally is
// insufficient: a frozen accessor can still return a different measurement.
const ownedFrameTimes = new WeakSet<object>();
// The HUD boundary has a deliberately broader value contract. Its immutable
// arrays can serve geometry caches, but never bypass presented-frame validation.
const ownedHudFrameTimes = new WeakSet<object>();
function freezeFrameTimeValues(input: unknown): Array<number | null> {
  if (Array.isArray(input) && ownedFrameTimes.has(input)) return input as Array<number | null>;
  const count = Array.isArray(input) ? Math.min(input.length, 120) : 0;
  const start = Array.isArray(input) ? input.length - count : 0;
  const values = new Array<number | null>(count);
  for (let i=0;i<count;++i) { const value=input[start+i]; values[i]=validFrameTime(value)?value:null; }
  Object.freeze(values); ownedFrameTimes.add(values);
  return values;
}
// Preserve the HUD boundary's legacy first-120/nonnegative normalization for
// external data. Actual admitted histories already satisfy the stronger bounds.
export function boundedFrameTimeValues(input: unknown): Array<number | null> {
  if (!Array.isArray(input)) return Object.freeze([]) as unknown as Array<number | null>;
  if (ownedFrameTimes.has(input) || ownedHudFrameTimes.has(input)) return input as Array<number | null>;
  const count=Math.min(input.length,120), values=new Array<number | null>(count);
  // Preserve the existing sparse-array holes, while producing a plain array
  // without inheriting caller getters or Array species/iterator behavior.
  for(let i=0;i<count;++i) if(i in input) { const value=input[i]; values[i]=typeof value==='number'&&Number.isFinite(value)&&value>=0?value:null; }
  Object.freeze(values); ownedHudFrameTimes.add(values); return values;
}
export function presentedFrameTime(packet: { fps?: unknown; pid?: unknown; frametime?: unknown; frameTimeSource?: unknown; frameTimeTs?: unknown; frameTimeValidUntil?: unknown; frameTimePollPeriodMs?: unknown; frameTimes?: unknown } | null | undefined, freezeSamples = false) {
  const ts = packet?.frameTimeTs;
  if ((packet?.frameTimeSource !== FRAME_TIME_SOURCE && packet?.frameTimeSource !== 'rtss-current-frame') || typeof packet.fps !== 'number' || !Number.isFinite(packet.fps) || packet.fps <= 0 || packet.fps > 1000000 ||
      typeof packet.pid !== 'number' || !Number.isSafeInteger(packet.pid) || packet.pid <= 0 ||
      typeof ts !== 'number' || !Number.isSafeInteger(ts) || ts <= 0 || !validFrameTime(packet.frametime)) return null;
  const extended=packet.frameTimeValidUntil!==undefined || packet.frameTimePollPeriodMs!==undefined;
  const end=extended ? observationDeadline(ts,packet.frameTimeValidUntil,packet.frameTimePollPeriodMs,packet.frameTimeSource==='rtss-current-frame') : ts+MONITOR_MISSING_GRACE_MS;
  if (!end) return null;
  const samples = Array.isArray(packet.frameTimes) && ownedFrameTimes.has(packet.frameTimes)
    ? packet.frameTimes as Array<number | null>
    : freezeSamples ? freezeFrameTimeValues(packet.frameTimes)
    : Array.isArray(packet.frameTimes) ? packet.frameTimes.slice(-120).map(v => validFrameTime(v) ? v : null) : [];
  // A mismatched latest point must not draw an old application's history.
  const frameTimes = samples.length && samples[samples.length - 1] === packet.frametime ? samples : [];
  return { frametime: packet.frametime, frameTimeSource: packet.frameTimeSource as FrameTimeSource, frameTimeTs: ts, frameTimes, ...(extended ? {frameTimeValidUntil:end,frameTimePollPeriodMs:packet.frameTimePollPeriodMs as number} : {}) };
}
// Availability needs two adjacent valid points, not a generated SVG path.
export function hasFrameTimeCurve(times: Array<number | null>): boolean {
  let previousValid = false;
  for (let i=Math.max(0,times.length-120);i<times.length;++i) {
    const valid = validFrameTime(times[i]);
    if (valid && previousValid) return true;
    previousValid = valid;
  }
  return false;
}
export function frameTimeCurve(times: Array<number | null>): string[] {
  const start = Math.max(0, times.length - 120), count = times.length - start;
  let max = 16.7;
  for (let i=start;i<times.length;++i) if (validFrameTime(times[i])) max = Math.max(max, times[i]!);
  const paths: string[] = []; let segment: string[] = [];
  const flush = () => { if (segment.length >= 2) paths.push(segment.join(' ')); segment = []; };
  for (let i=0;i<count;++i) {
    const value=times[start+i]; if (!validFrameTime(value)) { flush(); continue; }
    segment.push(`${i * 240 / Math.max(1,count-1)},${38-Math.min(36,value/max*36)}`);
  }
  flush(); return paths;
}
// One cache per renderer, bounded to the 120 points actually drawn. Only our
// immutable arrays can skip comparisons; external/mutable arrays are reread.
export function createFrameTimeCurveCache() {
  let previous: Array<number | null> | null = null, paths: readonly string[] = [];
  let previousOwned: Array<number | null> | null = null;
  return (times: Array<number | null>): readonly string[] => {
    const immutable=ownedFrameTimes.has(times)||ownedHudFrameTimes.has(times);
    if(immutable && times===previousOwned) return paths;
    previousOwned=immutable?times:null;
    const start = Math.max(0, times.length - 120), count = times.length - start;
    let same = previous !== null && previous.length === count;
    for (let i=0;same && i<count;++i) if (!Object.is(previous![i], times[start+i])) same = false;
    if (same) return paths;
    previous = times.slice(start); paths = Object.freeze(frameTimeCurve(previous));
    return paths;
  };
}
