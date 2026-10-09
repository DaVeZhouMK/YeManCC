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