import {
  getCurrentGame, isCurrentGameTarget, isGameStatusInitialized,
  refreshGameStatusStrict, subscribeGameStatus, type DetectedGame, type GameStatusListener,
} from './gamedetect';
export { samePolicyGame, gamePolicyKey } from './gamePolicyHysteresis';

// Compatibility facade only: policy consumers project the same authoritative
// target as the top bar. Behaviour-specific debounce must not retain a second PID.
const listeners = new Set<GameStatusListener>();
let started = false;
let ready: Promise<DetectedGame | null> | null = null;
const notifications: (DetectedGame | null)[] = [];
let notifying = false;

function publish(game: DetectedGame | null): void {
  notifications.push(game ? { ...game } : null);
  if (notifying) return;
  notifying = true;
  try {
    while (notifications.length) {
      const snapshot = notifications.shift()!;
      for (const cb of [...listeners]) {
        if (!listeners.has(cb)) continue;
        try { cb(snapshot ? { ...snapshot } : null); }
        catch { /* A broken observer cannot starve another policy consumer. */ }
      }
    }
  } finally { notifying = false; }
}
function ensureStarted(): void {
  if (started) return;
  started = true;
  subscribeGameStatus((game) => {
    // Initial unknown is not a successful empty target. The shared detector owns
    // initialization and its single background retry/poll, including null results.
    if (isGameStatusInitialized()) publish(game);
  });
}
export function isPolicyGameInitialized(): boolean { return isGameStatusInitialized(); }
export function getPolicyGame(): DetectedGame | null {
  return getCurrentGame();
}
export function isPolicyTargetCurrent(target: { pid: number; processCreated: string }): boolean {
  return isCurrentGameTarget(target);
}
export async function detectPolicyGame(): Promise<DetectedGame | null> {
  ensureStarted();
  if (!isGameStatusInitialized()) {
    if (!ready) {
      const attempt = refreshGameStatusStrict();
      ready = attempt;
      void attempt.finally(() => { if (ready === attempt) ready = null; }).catch(() => {});
    }
    await ready;
  }
  // Read after await: a newer event/selection may have superseded that reply.
  return getPolicyGame();
}
// Explicit recovery refreshes the authority; it never elects or replays an old PID.
export async function refreshPolicyGameStrict(): Promise<DetectedGame | null> {
  ensureStarted();
  await refreshGameStatusStrict();
  return getPolicyGame();
}
export function subscribePolicyGameStatus(cb: GameStatusListener): () => void {
  ensureStarted();
  listeners.add(cb);
  if (isGameStatusInitialized()) {
    try { cb(getPolicyGame()); }
    catch { /* Initial notification has the same isolation as later broadcasts. */ }
  }
  void detectPolicyGame().catch(() => {});
  return () => { listeners.delete(cb); };
}
