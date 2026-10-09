import { refreshGameStatusStrict, subscribeGameStatus, type DetectedGame, type GameStatusListener } from './gamedetect';
import { GamePolicyHysteresis, GAME_POLICY_RELEASE_DELAY_MS, gamePolicyKey } from './gamePolicyHysteresis';
export { samePolicyGame, gamePolicyKey } from './gamePolicyHysteresis';

const state = new GamePolicyHysteresis();
const listeners = new Set<GameStatusListener>();
let started = false;
let ready: Promise<DetectedGame | null> | null = null;
let timer: ReturnType<typeof setTimeout> | null = null;
let confirming = false;
let initialRetryTimer: ReturnType<typeof setTimeout> | null = null;
let observationEpoch = 0;

function armConfirmation(): void {
  if (timer !== null) clearTimeout(timer);
  timer = null;
  if (state.missingSince === null || confirming) return;
  const remaining = GAME_POLICY_RELEASE_DELAY_MS - (performance.now() - state.missingSince);
  timer = setTimeout(() => {
    timer = null;
    confirming = true;
    const epoch = observationEpoch;
    // A timer alone is not evidence: confirm with a fresh successful native read.
    void refreshGameStatusStrict().then((game) => {
      if (epoch === observationEpoch) observe(game);
    }).catch(() => {
      // Transport failure is not an exit. Keep the lease; retry after one poll.
    }).finally(() => {
      confirming = false;
      if (state.missingSince !== null) {
        timer = setTimeout(() => { timer = null; armConfirmation(); }, 1250);
      }
    });
  }, Math.max(0, remaining));
}
function observe(game: DetectedGame | null): void {
  observationEpoch += 1;
  const previousKey = gamePolicyKey(state.current);
  const changed = state.observe(game, performance.now());
  armConfirmation();
  if (changed || previousKey !== gamePolicyKey(state.current)) for (const cb of [...listeners]) cb(state.current);
}
function ensureStarted(): void {
  if (started) return;
  started = true;
  let initialCallback = true;
  subscribeGameStatus((game) => {
    // subscribeGameStatus's initial cache can be null/stale after renderer restart.
    if (initialCallback) { initialCallback = false; return; }
    observe(game);
  });
}
export function isPolicyGameInitialized(): boolean { return state.initialized; }
export function getPolicyGame(): DetectedGame | null { return state.current; }
export function isPolicyTargetCurrent(target: { pid: number; processCreated: string }): boolean {
  const game = state.current;
  return !!game && game.pid === target.pid && game.processCreated === target.processCreated;
}
export async function detectPolicyGame(): Promise<DetectedGame | null> {
  ensureStarted();
  if (!ready) {
    ready = refreshGameStatusStrict().then((game) => {
      if (initialRetryTimer !== null) clearTimeout(initialRetryTimer);
      initialRetryTimer = null;
      observe(game); return state.current;
    }).catch((error) => {
      ready = null;
      // A later successful null poll emits no change from the raw null cache.
      // Retry initialization explicitly so stale overlays can still be cleared.
      if (initialRetryTimer === null) initialRetryTimer = setTimeout(() => {
        initialRetryTimer = null; void detectPolicyGame().catch(() => {});
      }, 1250);
      throw error;
    });
  }
  await ready;
  return state.current;
}
export function subscribePolicyGameStatus(cb: GameStatusListener): () => void {
  ensureStarted();
  listeners.add(cb);
  if (state.initialized) cb(state.current);
  void detectPolicyGame().catch(() => {});
  return () => { listeners.delete(cb); };
}
