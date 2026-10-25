// Independent proof for the two HWiNFO metrics carried beside FPS. The FPS
// envelope may be refreshed by RTSS; it is never a hardware observation.
export interface FpsHardwareProof {
  ts?: unknown;
  fpsSource?: unknown;
  fpsObservedAt?: unknown;
  fpsPollPeriodMs?: unknown;
  hardwareObservedAt?: unknown;
  hardwareValidUntil?: unknown;
  hardwarePollPeriodMs?: unknown;
}
const stamp = (value: unknown): value is number => typeof value === 'number' && Number.isSafeInteger(value) && value > 0;
const period = (value: unknown): value is number => typeof value === 'number' && Number.isSafeInteger(value) && value >= 0 && value <= 60000;
function lifetime(ms: number): number { return Math.max(5000, Math.max(500, ms) + 3000); }
export function hasFpsHardwareProof(value: FpsHardwareProof): boolean {
  return value.hardwareObservedAt !== undefined || value.hardwareValidUntil !== undefined || value.hardwarePollPeriodMs !== undefined;
}
export function fpsHardwareProofFresh(value: FpsHardwareProof, now: number, hwDown = false, stoppedAt = 0): boolean {
  if (hwDown || !Number.isSafeInteger(now) || now <= 0) return false;
  if (hasFpsHardwareProof(value)) {
    const at = value.hardwareObservedAt, end = value.hardwareValidUntil, ms = value.hardwarePollPeriodMs;
    return stamp(at) && stamp(end) && period(ms) && at > stoppedAt && at <= now &&
      end > at && end - at <= lifetime(ms) && now < end;
  }
  // Pre-proof native HWiNFO FPS still supplies its original observation. An
  // older RTSS packet cannot prove when its copied hardware values were read.
  if (value.fpsSource === 'rtss') return false;
  const modern = value.fpsSource !== undefined || value.fpsObservedAt !== undefined;
  if (modern && value.fpsSource !== 'hwinfo-presented' && value.fpsSource !== 'hwinfo-displayed') return false;
  const at = modern ? value.fpsObservedAt : value.ts, ms = modern ? value.fpsPollPeriodMs ?? 0 : 0;
  return stamp(at) && period(ms) && at > stoppedAt && at <= now && now - at < lifetime(ms);
}
