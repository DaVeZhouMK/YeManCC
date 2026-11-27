import {
  getCurrentGame, isCurrentGameTarget, isGameStatusInitialized, isGameStatusPowerCurrent,
  observeGameStatusStrict, refreshGameStatusStrict, subscribeGameStatus, subscribeGameTargetReadiness, type DetectedGame, type GameStatusListener,
} from './gamedetect';
export { samePolicyGame, gamePolicyKey } from './gamePolicyHysteresis';
export { isGameStatusCurrent as isPolicyGameCurrent, subscribeGameTargetReadiness as subscribePolicyGameReadiness } from './gamedetect';

// Compatibility facade only: policy consumers project the same authoritative
// target as the top bar. Behaviour-specific debounce must not retain a second PID.
const listeners = new Set<GameStatusListener>();
let started = false;
let pending = new Set<GameStatusListener>();
let notifying = false;
let stopDeliveryRecheck: (() => void) | null = null;
let deliveryRecheckQueued = false;

function notify(cb: GameStatusListener, game: DetectedGame | null): void {
  try { cb(game ? { ...game } : null); }
  catch { /* A broken observer cannot starve another policy consumer. */ }
}

function releaseDeliveryRecheck(): void {
  const stop = stopDeliveryRecheck;
  stopDeliveryRecheck = null;
  stop?.();
}

function recheckAfterReady(): void {
  if (stopDeliveryRecheck) return;
  stopDeliveryRecheck = subscribeGameTargetReadiness(ready => {
    if (!ready || deliveryRecheckQueued) return;
    deliveryRecheckQueued = true;
    // A changed game publishes synchronously after readiness. Let it replace
    // the interrupted delivery before resuming a same-game notification.
    queueMicrotask(() => { deliveryRecheckQueued = false; deliverPending(); });
  });
}

function deliverPending(): void {
  if (notifying) return;
  notifying = true;
  try {
    while (pending.size) {
      const delivery = pending;
      const snapshot = getCurrentGame();
      for (const cb of [...delivery]) {
        if (pending !== delivery) break;
        if (!delivery.has(cb)) continue;
        if (!isGameStatusPowerCurrent()) { recheckAfterReady(); return; }
        delivery.delete(cb);
        notify(cb, snapshot);
      }
    }
  } finally { notifying = false; }
  releaseDeliveryRecheck();
}
function ensureStarted(): void {
  if (started) return;
  started = true;
  subscribeGameStatus(() => {
    // Initial unknown is not a successful empty target. The shared detector owns
    // initialization and its single background retry/poll, including null results.
    if (isGameStatusInitialized()) {
      pending = new Set(listeners);
      deliverPending();
    }
  });
}
export function isPolicyGameInitialized(): boolean { return isGameStatusInitialized(); }
export function getPolicyGame(): DetectedGame | null {
  return getCurrentGame();
}
export function isPolicyTargetCurrent(target: { pid: number; processCreated: string }): boolean {
  return isCurrentGameTarget(target);
}
export async function detectPolicyGame(mode: 'default' | 'observe' = 'default'): Promise<DetectedGame | null> {
  ensureStarted();
  if (mode === 'observe') await observeGameStatusStrict();
  else if (!isGameStatusInitialized()) {
    // The shared detector alone owns/coalesces reads and retires their power
    // scope. A facade Promise must not keep an earlier scope installed.
    await refreshGameStatusStrict();
  }
  // Read after await: a newer event/selection may have superseded that reply.
  return getPolicyGame();
}
// Explicit recovery refreshes the authority; startup can share passive discovery.
export async function refreshPolicyGameStrict(mode: 'force' | 'observe' = 'force'): Promise<DetectedGame | null> {
  ensureStarted();
  if (mode === 'observe') await observeGameStatusStrict();
  else await refreshGameStatusStrict();
  return getPolicyGame();
}
export function subscribePolicyGameStatus(cb: GameStatusListener): () => void {
  ensureStarted();
  listeners.add(cb);
  if (isGameStatusInitialized()) {
    if (isGameStatusPowerCurrent()) { pending.delete(cb); notify(cb, getPolicyGame()); }
    else { pending.add(cb); deliverPending(); }
  }
  if (!isGameStatusInitialized()) void detectPolicyGame('observe').catch(() => {});
  return () => {
    listeners.delete(cb);
    pending.delete(cb);
    if (!pending.size) releaseDeliveryRecheck();
  };
}
