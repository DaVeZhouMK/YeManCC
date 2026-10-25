// Display admission only. Consume genuine YMCC proofs; never sample or renew them.
import type { HudData } from './monitorHudModel';
export const HUD_FPS_ENTRY_MS = 3000;
export const HUD_FPS_FAMILY = ['fps', 'fpsLimit', 'fps1', 'frametime', 'frametimeGraph'] as const;
export function createHudFpsVisibilityGate(deps: {
  now(): number; emit(ready: boolean): void;
  setTimer(fn: () => void, ms: number): unknown; clearTimer(id: unknown): void;
}) {
  let latest: HudData | null = null, ready = false, qualified = false, target = '', startedAt = 0, revision = 0;
  let firstObservation = 0, lastObservation = 0, lastDeadline = 0;
  let hardwareObservation = 0, displayedObservation = 0, rtssObservation = 0;
  let timer: unknown = null, timerAt = 0, token = 0, disposed = false;
  const cancel = () => { ++token; if (timer !== null) deps.clearTimer(timer); timer = null; timerAt = 0; };
  const publish = (next: boolean) => { if (next !== ready) { ready = next; deps.emit(next); } };
  const clearTracking = () => { cancel(); qualified = false; target = ''; startedAt = firstObservation = lastObservation = lastDeadline = hardwareObservation = displayedObservation = rtssObservation = 0; };
  const reset = () => { clearTracking(); publish(false); };
  const withdraw = () => { cancel(); publish(false); };
  function refresh() {
    if (disposed || !latest) return;
    const ownRevision = revision;
    const now = deps.now(), fps = latest.values.fps;
    const valid = !latest.stale && typeof fps === 'number' && Number.isFinite(fps) && fps >= 1 && fps <= 1000000;
    const retainLineage = () => qualified && Number.isFinite(now) && now >= startedAt && now < lastDeadline + 6000;
    if (!valid || !Number.isFinite(now)) {
      // A missing/aged projection is not evidence of a producer gap. Keep only
      // the qualification history, never an expired value. A later fresh proof
      // must still match the target, source order and preceding deadline.
      // An explicit zero/sub-one/invalid reading still restarts entrance.
      const missing = fps == null || (latest.stale && typeof fps === 'number' && Number.isFinite(fps) && fps >= 1 && fps <= 1000000);
      if (missing && retainLineage()) withdraw(); else reset();
      return;
    }
    // Offline style fixtures have no game lifetime and keep immediate previews.
    if (latest.source === 'fixture') { clearTracking(); publish(true); return; }
    const at = latest.fpsObservedAt, end = latest.freshUntil?.fps, nextTarget = latest.fpsTarget;
    if (typeof at !== 'number' || !Number.isSafeInteger(at) || at <= 0 || at > now ||
        typeof end !== 'number' || !Number.isSafeInteger(end) || end > now + 6000 || end <= at || end > at + 6000 ||
        typeof nextTarget !== 'string' || !nextTarget.length) { reset(); return; }
    if (end <= now) {
      // A background renderer may run this timer before its queued fresh frame.
      // Withdraw the old pixels, but do not erase an already qualified lineage.
      if (retainLineage() && nextTarget === target && at === lastObservation && end === lastDeadline) withdraw();
      else reset();
      return;
    }
    // Delivery time is not a producer gap. A newer genuine observation whose
    // interval overlaps the previous proof keeps qualification, even if a
    // background renderer delivers it after the earlier deadline.
    // HW timestamps are whole seconds; RTSS observations use milliseconds.
    // Switching back to fresh HW can legitimately move behind the last RTSS
    // time. Compare each known source with its own previous observation.
    // Legacy/unknown sources retain the conservative global-order check.
    const provider = latest.fpsSource;
    const previousObservation = provider === 'hwinfo-presented' ? hardwareObservation : provider === 'hwinfo-displayed' ? displayedObservation : provider === 'rtss' ? rtssObservation : lastObservation;
    if (target !== nextTarget || at < previousObservation || now < startedAt ||
        (lastDeadline > 0 && now >= lastDeadline && at > lastDeadline)) {
      reset();
      // Notifications may synchronously release/reset the gate or accept a
      // newer game. The interrupted refresh must not overwrite that state.
      if (disposed || revision !== ownRevision) return;
      target = nextTarget; startedAt = now; firstObservation = at;
    }
    lastObservation = at; lastDeadline = end;
    if (provider === 'hwinfo-presented') hardwareObservation = at;
    else if (provider === 'hwinfo-displayed') displayedObservation = at;
    else if (provider === 'rtss') rtssObservation = at;
    // A timer/transport repeat of one cached success cannot satisfy entrance.
    qualified = qualified || (now - startedAt >= HUD_FPS_ENTRY_MS && at - firstObservation >= HUD_FPS_ENTRY_MS);
    publish(qualified);
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
