import { getSharedPowerLifecycle, onSharedPowerLifecycle, requestSharedPowerLifecycleReconcile, requestSharedPowerLifecycleReadyConfirmation, isSharedPowerLifecycleAdmitted } from './powerLifecycleState';
import { powerLifecycle } from './api';

// Shared cancellation for trainer preparation, launch and owned CE sessions.
// Shared power owns ordering/reconciliation; this adapter only retires trainer work.
let epoch = 0;
let lastPowerPhase: string | null = null, lastPowerGeneration = 0;
let lastPowerKnown: boolean | null = null, lastPowerAdmitted: boolean | null = null;
let lastPowerNotificationSequence: number | undefined;
let stopPower: (() => void) | null = null;
const observers = new Set<(event: string) => void>();
const readyObservers = new Map<(event: string) => void, () => void>();
const events = ['ipc:webview.recovering', 'pagehide'];
let recoveryPending = false, recoveryRevision = 0;
let stopConfirmation: (() => void) | null = null;
function notifyInvalidated(type: string): void {
  ++epoch;
  for (const observer of [...observers]) {
    try { observer(type); } catch { /* One owner cannot block another's cancellation. */ }
  }
}
function invalidate(event: Event): void {
  const revision = ++recoveryRevision;
  recoveryPending = event.type === 'ipc:webview.recovering'; stopConfirmation?.(); stopConfirmation = null;
  notifyInvalidated(event.type);
  if (event.type !== 'ipc:webview.recovering' || !observers.size || revision !== recoveryRevision) return;
  stopConfirmation = requestSharedPowerLifecycleReadyConfirmation(state => {
    if (!observers.size || revision !== recoveryRevision || !isSharedPowerLifecycleAdmitted(state)) return;
    recoveryPending = false; stopConfirmation = null; notifyReady();
  });
}
function notifyReady(): void {
  for (const ready of [...readyObservers.values()]) {
    try { ready(); } catch { /* One owner cannot block another's recovery. */ }
  }
}
export function getTrainerLifecycleEpoch(): number { return epoch; }
export function subscribeTrainerLifecycle(observer: (event: string) => void, onConfirmedReady?: () => void): () => void {
  if (!observers.size) {
    if (typeof window !== 'undefined') for (const event of events) window.addEventListener(event, invalidate);
    // Subscribe before adding a new owner: an initial Ready replay must not
    // cancel an operation that is only now registering its cancellation hook.
    stopPower = onSharedPowerLifecycle(snapshot => {
      const initial = lastPowerPhase === null;
      const known = snapshot.known !== false, admitted = isSharedPowerLifecycleAdmitted(snapshot);
      const changed = snapshot.phase !== lastPowerPhase || snapshot.generation !== lastPowerGeneration || known !== lastPowerKnown || admitted !== lastPowerAdmitted;
      const scopeChanged = snapshot.observedNotificationSequence !== lastPowerNotificationSequence;
      lastPowerPhase = snapshot.phase; lastPowerGeneration = snapshot.generation; lastPowerKnown = known; lastPowerAdmitted = admitted;
      lastPowerNotificationSequence = snapshot.observedNotificationSequence;
      if (admitted) {
        if (recoveryPending) return; // Only this recovery's fresh read may resume trainer work.
        // A newer observation retires old work without inventing an OS wake.
        if ((changed || scopeChanged) && !initial) notifyInvalidated(changed ? 'ipc:power.resumed' : 'ipc:power.observationChanged');
        notifyReady();
      } else if (changed || scopeChanged) notifyInvalidated(snapshot.phase === 'ready' ? 'ipc:power.pending' : 'ipc:power.' + snapshot.phase);
    }, { observeNotificationSequence: true });
  }
  observers.add(observer);
  if (onConfirmedReady) readyObservers.set(observer, onConfirmedReady);
  const power = getSharedPowerLifecycle();
  if (!recoveryPending && onConfirmedReady && isSharedPowerLifecycleAdmitted(power)) {
    try { onConfirmedReady(); } catch { /* Preserve the other registered owners. */ }
  }
  requestSharedPowerLifecycleReconcile('trainer-lifecycle');
  return () => {
    observers.delete(observer);
    readyObservers.delete(observer);
    if (!observers.size) {
      ++recoveryRevision; stopConfirmation?.(); stopConfirmation = null; recoveryPending = false;
      lastPowerPhase = null; lastPowerGeneration = 0; lastPowerKnown = null; lastPowerAdmitted = null; lastPowerNotificationSequence = undefined;
      stopPower?.(); stopPower = null;
      if (typeof window !== 'undefined') for (const event of events) window.removeEventListener(event, invalidate);
    }
  };
}
export class TrainerPowerChangedError extends Error {
  constructor(message: string) { super(message); this.name = 'TrainerPowerChangedError'; }
}
export async function assertTrainerPowerReady(expectedEpoch: number, expectedGeneration?: number): Promise<number> {
  if (expectedEpoch !== epoch) throw new TrainerPowerChangedError('电源或页面状态已改变，本次修改器操作已取消，请重新打开');
  const notificationSequence = getSharedPowerLifecycle()?.observedNotificationSequence;
  const state = await powerLifecycle.get(3000);
  if (expectedEpoch !== epoch || !isSharedPowerLifecycleAdmitted(state) ||
    !Number.isSafeInteger(state.generation) || state.generation <= 0 ||
    expectedGeneration !== undefined && expectedGeneration !== state.generation ||
    notificationSequence !== undefined && state.observedNotificationSequence !== notificationSequence) {
    throw new TrainerPowerChangedError('电源状态未就绪或已改变，本次修改器操作已取消，请恢复完成后重新打开');
  }
  return state.generation;
}
