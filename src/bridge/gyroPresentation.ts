import type { GyroTelemetryV1 } from './inputContracts';

export interface GyroPresentationOptions {
  present(frame: GyroTelemetryV1, receivedAt: number): void;
  requestFrame?: (callback: () => void) => number;
  cancelFrame?: (ticket: number) => void;
}

/** Renderer-only latest-frame queue. It never changes native sampling, motion
 * admission, calibration, configuration or Host ownership. Receipt time stays
 * with the frame so restoring a cached page cannot invent fresh sensor evidence.
 */
export function createGyroPresentationGate(options: GyroPresentationOptions) {
  const request = options.requestFrame ?? ((callback: () => void) => requestAnimationFrame(callback));
  const cancel = options.cancelFrame ?? ((ticket: number) => cancelAnimationFrame(ticket));
  let active = false;
  let visible = true;
  let disposed = false;
  let ticket: number | null = null;
  let generation = 0;
  let latest: { frame: GyroTelemetryV1; receivedAt: number } | null = null;

  function pause() {
    generation++;
    if (ticket !== null) cancel(ticket);
    ticket = null;
  }
  function schedule() {
    if (disposed || !active || !visible || !latest || ticket !== null) return;
    const ownGeneration = ++generation;
    ticket = request(() => {
      // A canceled callback must not clear a newer frame's ticket or resurrect
      // presentation after deactivation/eviction, even in a late callback race.
      if (ownGeneration !== generation) return;
      ticket = null;
      if (disposed || !active || !visible) return;
      const next = latest;
      latest = null;
      if (next) options.present(next.frame, next.receivedAt);
    });
  }
  return {
    push(frame: GyroTelemetryV1, receivedAt: number) {
      if (disposed) return;
      latest = { frame, receivedAt };
      schedule();
    },
    setActive(value: boolean) {
      if (disposed) return;
      active = value;
      if (!active) pause(); else schedule();
    },
    setVisible(value: boolean) {
      if (disposed) return;
      visible = value;
      if (!visible) pause(); else schedule();
    },
    dispose() {
      if (disposed) return;
      disposed = true;
      active = false;
      latest = null;
      pause();
    },
  };
}
export interface StickPoint { x: number; y: number }
export interface StickPresentationOptions {
  present(point: StickPoint): void;
  durationMs: number;
  now?: () => number;
  requestFrame?: (callback: () => void) => number;
  cancelFrame?: (ticket: number) => void;
}

/** Finite display interpolation between received stick positions. Never predicts,
 * amplifies, normalizes radius, edits calibration, or feeds controller output.
 * Numeric readouts keep the original samples. Idle/hidden views own no RAF loop.
 */
export function createStickPresentation(options: StickPresentationOptions) {
  const now = options.now ?? (() => performance.now());
  const request = options.requestFrame ?? (callback => requestAnimationFrame(callback));
  const cancel = options.cancelFrame ?? (ticket => cancelAnimationFrame(ticket));
  const duration = Math.max(0, Math.min(100, Number.isFinite(options.durationMs) ? options.durationMs : 0));
  let active = false, disposed = false, initialized = false;
  let ticket: number | null = null, generation = 0, started = 0;
  let from: StickPoint = { x: 0, y: 0 }, target: StickPoint = { x: 0, y: 0 };
  const unit = (v: number) => Number.isFinite(v) ? Math.max(-1, Math.min(1, v)) : 0;
  const same = (a: StickPoint, b: StickPoint) => a.x === b.x && a.y === b.y;
  function position(time: number): StickPoint {
    const progress = duration ? Math.max(0, Math.min(1, (time - started) / duration)) : 1;
    return progress === 1 ? { ...target } : {
      x: from.x + (target.x - from.x) * progress,
      y: from.y + (target.y - from.y) * progress,
    };
  }
  function stop() { generation++; if (ticket !== null) cancel(ticket); ticket = null; }
  function schedule() {
    if (!active || disposed || ticket !== null) return;
    const ownGeneration = generation;
    ticket = request(() => {
      if (ownGeneration !== generation || !active || disposed) return;
      ticket = null;
      const time = now(), point = position(time);
      options.present(point);
      if (time - started < duration && !same(from, target)) schedule();
    });
  }
  return {
    push(value: StickPoint, snap = false) {
      if (disposed || !active) return;
      const next = { x: unit(value.x), y: unit(value.y) };
      if (!initialized || snap || !duration) {
        stop(); initialized = true; from = target = next; started = now(); options.present({ ...next }); return;
      }
      if (same(next, target)) return;
      const time = now(); from = position(time); target = next; started = time;
      schedule();
    },
    setActive(value: boolean) {
      if (disposed || active === value) return;
      active = value;
      if (!value) { stop(); initialized = false; from = target = { x: 0, y: 0 }; options.present({ x: 0, y: 0 }); }
    },
    reset() { if (disposed) return; stop(); initialized = false; from = target = { x: 0, y: 0 }; options.present({ x: 0, y: 0 }); },
    dispose() { if (disposed) return; active = false; stop(); disposed = true; initialized = false; },
  };
}

/** Native AxisModifiers + gyroWeight contribution, before physical-stick merge
 * and foreground neutralization. A preview is not a Host/game readback.
 */
export function gyroStickPreviewSample(frame: GyroTelemetryV1, target: string): StickPoint | null {
  const point = frame.layoutContribution;
  if (frame.outputKind !== 'native-canonical-pipe-frame' || frame.motionAdmission !== 'hc-motion-admitted' ||
      frame.targetStick !== target || !Number.isFinite(Date.parse(frame.timestampUtc)) || !point ||
      !Number.isFinite(point.x) || !Number.isFinite(point.y)) return null;
  return { x: Math.max(-1, Math.min(1, point.x)), y: Math.max(-1, Math.min(1, point.y)) };
}
