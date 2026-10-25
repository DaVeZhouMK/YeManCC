// Control consumes only Displayed. HUD Presented/RTSS fallback never becomes a control receipt.
import type { MonitorSnapshotView } from './monitorData';
export interface DisplayedFramerate {
  readonly source: 'hwinfo-displayed';
  readonly fps: number;
  readonly fps1: number | null;
  readonly pid: number;
  readonly processCreated: string;
  readonly game: string;
  readonly observedAt: number;
  readonly validUntil: number;
  readonly pollPeriodMs: number;
}
const stamp = (v: unknown): v is number => typeof v === 'number' && Number.isSafeInteger(v) && v > 0;
const reading = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v) && v >= 0 && v <= 1000000;
export function decodeDisplayedFramerate(input: unknown): Readonly<DisplayedFramerate> | null {
  if (!input || typeof input !== 'object' || Array.isArray(input)) return null;
  const x = input as Partial<DisplayedFramerate>;
  if (x.source !== 'hwinfo-displayed' || !reading(x.fps) || !stamp(x.pid) || x.pid > 0xffffffff ||
      typeof x.processCreated !== 'string' || !/^[1-9][0-9]{0,19}$/.test(x.processCreated) ||
      (x.processCreated.length === 20 && x.processCreated > '18446744073709551615') ||
      typeof x.game !== 'string' || !x.game.length || x.game.length > 512 ||
      !stamp(x.observedAt) || !stamp(x.validUntil) || x.validUntil <= x.observedAt ||
      typeof x.pollPeriodMs !== 'number' || !Number.isSafeInteger(x.pollPeriodMs) || x.pollPeriodMs < 0 || x.pollPeriodMs > 60000 ||
      x.validUntil - x.observedAt > Math.max(5000, Math.max(500, x.pollPeriodMs) + 3000)) return null;
  return Object.freeze({ source: x.source, fps: x.fps, fps1: reading(x.fps1) ? x.fps1 : null,
    pid: x.pid, processCreated: x.processCreated, game: x.game, observedAt: x.observedAt,
    validUntil: x.validUntil, pollPeriodMs: x.pollPeriodMs });
}
export function displayedFramerateForControl(view: MonitorSnapshotView | null, now: number): Readonly<DisplayedFramerate> | null {
  const top = view?.top, value = top?.displayedFramerate;
  if (!top || !value || view?.hwDown || top.hwDown || !stamp(now) || !stamp(top.ts) || now < top.ts || now - top.ts >= 6000 ||
      value.source !== 'hwinfo-displayed' || !reading(value.fps) || !stamp(value.observedAt) ||
      value.observedAt > top.ts || value.observedAt > now || !stamp(value.validUntil) || now >= value.validUntil ||
      !stamp(view?.collectedAt) || value.observedAt > view!.collectedAt) return null;
  return value;
}
