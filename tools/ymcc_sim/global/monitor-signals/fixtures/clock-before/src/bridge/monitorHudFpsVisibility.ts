// Display admission only. Consume genuine YMCC proofs; never sample or renew them.
import type { HudData } from './monitorHudModel';
export const HUD_FPS_ENTRY_MS = 3000;
export const HUD_FPS_FAMILY = ['fps', 'fpsLimit', 'fps1', 'frametime', 'frametimeGraph'] as const;
export function createHudFpsVisibilityGate(deps: {
  now(): number; emit(ready: boolean): void;
  setTimer(fn: () => void, ms: number): unknown; clearTimer(id: unknown): void;
}) {
  let latest: HudData | null = null, ready = false, target = '', startedAt = 0, revision = 0;
  let firstObservation = 0, lastObservation = 0, lastDeadline = 0;
  let timer: unknown = null, timerAt = 0, token = 0, disposed = false;
  const cancel = () => { ++token; if (timer !== null) deps.clearTimer(timer); timer = null; timerAt = 0; };
  const publish = (next: boolean) => { if (next !== ready) { ready = next; deps.emit(next); } };
  const clearTracking = () => { cancel(); target = ''; startedAt = firstObservation = lastObservation = lastDeadline = 0; };
  const reset = () => { clearTracking(); publish(false); };
  function refresh() {
    if (disposed || !latest) return;
    const ownRevision = revision;
    const now = deps.now(), fps = latest.values.fps;
    const valid = !latest.stale && typeof fps === 'number' && Number.isFinite(fps) && fps >= 1 && fps <= 1000000;
    if (!valid || !Number.isFinite(now)) { reset(); return; }
    // Offline style fixtures have no game lifetime and keep immediate previews.
    if (latest.source === 'fixture') { clearTracking(); publish(true); return; }
    const at = latest.fpsObservedAt, end = latest.freshUntil?.fps, nextTarget = latest.fpsTarget;
    if (typeof at !== 'number' || !Number.isSafeInteger(at) || at <= 0 || at > now ||
        typeof end !== 'number' || !Number.isSafeInteger(end) || end <= now || end > now + 6000 || end <= at || end > at + 6000 ||
        typeof nextTarget !== 'string' || !nextTarget.length) { reset(); return; }
    if (target !== nextTarget || at < lastObservation || now < startedAt || (lastDeadline > 0 && now >= lastDeadline)) {
      reset();
      // Notifications may synchronously release/reset the gate or accept a
      // newer game. The interrupted refresh must not overwrite that state.
      if (disposed || revision !== ownRevision) return;
      target = nextTarget; startedAt = now; firstObservation = at;
    }
    lastObservation = at; lastDeadline = end;
    // A timer/transport repeat of one cached success cannot satisfy entrance.
    publish(ready || (now - startedAt >= HUD_FPS_ENTRY_MS && at - firstObservation >= HUD_FPS_ENTRY_MS));
    if (disposed || revision !== ownRevision) return;
    if (timer !== null && timerAt > now && timerAt <= end) return;
    cancel(); timerAt = end; const own = ++token;
    timer = deps.setTimer(() => { if (disposed || own !== token) return; timer = null; timerAt = 0; refresh(); }, Math.max(1, Math.ceil(end - now)));
  }
  return {
    accept(data: HudData) { if (!disposed) { ++revision; latest = data; refresh(); } },
    reset() { ++revision; latest = null; reset(); },
    dispose() { disposed = true; ++revision; latest = null; cancel(); },
    ready: () => ready, pending: () => timer !== null,
  };
}
