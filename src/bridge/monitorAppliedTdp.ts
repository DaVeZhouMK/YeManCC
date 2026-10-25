// The display consumes YMCC's common runtime receipt, never saved tdpMax,
// HWiNFO package power, or a second hardware collector. Shared 1 Hz latest-only.
import { tdpRuntimeStore, type TdpRuntimeSnapshot } from './tdpRuntimeState';
import { createLatestDisplayFeed } from './monitorDisplayFeed';
export interface AppliedTdpDeps {
  initial: TdpRuntimeSnapshot;
  observe(listener: (state: TdpRuntimeSnapshot) => void): () => void;
  now(): number;
  setTimer(fn: () => void, ms: number): unknown;
  clearTimer(id: unknown): void;
}
export function createAppliedTdpFeed(deps: AppliedTdpDeps) {
  const feed = createLatestDisplayFeed<TdpRuntimeSnapshot>(deps.initial, {
    now: deps.now, key: state => String(state.revision), unavailable: state => state.status !== 'applied',
    setTimer: deps.setTimer, clearTimer: deps.clearTimer,
  });
  // A new request does not erase the last acknowledged hardware write. Keep
  // that same-side receipt during a bounded in-flight update, never a saved
  // target. Failure, unknown state and AC/DC boundaries still clear at once.
  const pendingGraceMs = 5000;
  let users = 0, off: (() => void) | null = null;
  let confirmed: TdpRuntimeSnapshot | null = null, source = deps.initial;
  let pendingUntil: number | null = null, pendingTimer: unknown = null;
  function clearPending() {
    if (pendingTimer !== null) deps.clearTimer(pendingTimer);
    pendingTimer = null; pendingUntil = null;
  }
  function expirePending() {
    if (pendingUntil === null || deps.now() < pendingUntil) return;
    clearPending(); confirmed = null; feed.push(source);
  }
  function accept(state: TdpRuntimeSnapshot) {
    source = state;
    if (state.status === 'applied' && state.proof === 'write-ack' && (state.mode === 'ac' || state.mode === 'dc') &&
        typeof state.watts === 'number' && Number.isFinite(state.watts) && state.watts >= 2 && state.watts <= 200) {
      clearPending(); confirmed = state; feed.push(state); return;
    }
    if (state.status === 'pending' && confirmed !== null && state.mode === confirmed.mode) {
      if (pendingUntil === null) {
        pendingUntil = deps.now() + pendingGraceMs;
        pendingTimer = deps.setTimer(() => { pendingTimer = null; expirePending(); }, pendingGraceMs);
      }
      if (deps.now() < pendingUntil) { feed.push(confirmed); return; }
    }
    clearPending(); confirmed = null; feed.push(state);
  }
  return {
    advance() { expirePending(); feed.advance(); },
    subscribe(listener: (state: TdpRuntimeSnapshot) => void) {
      if (users++ === 0) off = deps.observe(accept);
      const stop = feed.subscribe(listener); let released = false;
      return () => {
        if (released) return;
        released = true; stop();
        if (--users === 0) { off?.(); off = null; clearPending(); confirmed = null; }
      };
    },
  };
}
const appliedTdpFeed = createAppliedTdpFeed({
  initial: tdpRuntimeStore.get(), observe: listener => tdpRuntimeStore.observe(listener), now: () => performance.now(),
  setTimer: (fn, ms) => setTimeout(fn, ms), clearTimer: id => clearTimeout(id as ReturnType<typeof setTimeout>),
});
export const subscribeMonitorTdpState = (listener: (state: TdpRuntimeSnapshot) => void): (() => void) => appliedTdpFeed.subscribe(listener);

export const advanceMonitorTdpDisplay = (): void => appliedTdpFeed.advance();
