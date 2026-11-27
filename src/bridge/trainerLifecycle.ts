import { beginSharedPowerLifecycleReadback, getSharedPowerLifecycle, onSharedPowerLifecycle, requestSharedPowerLifecycleReconcile, requestSharedPowerLifecycleReadyConfirmation, isSharedPowerLifecycleAdmitted } from './powerLifecycleState';
import { powerLifecycle } from './api';

// Shared cancellation for trainer preparation, launch and owned CE sessions.
// Shared power owns ordering/reconciliation; this adapter only retires trainer work.
let epoch = 0;
let lastPowerPhase: string | null = null, lastPowerGeneration = 0;
let lastPowerKnown: boolean | null = null, lastPowerAdmitted: boolean | null = null;
let lastPowerNotificationSequence: number | undefined;
let lastPowerSuspendSerial: number | undefined;
let stopPower: (() => void) | null = null;
const observers = new Set<(event: string) => void>();
const readyObservers = new Map<(event: string) => void, () => void>();
const pendingPowerReads = new Set<() => void>();
const events = ['ipc:webview.recovering', 'pagehide'];
let recoveryPending = false, recoveryRevision = 0;
let stopConfirmation: (() => void) | null = null;
function notifyInvalidated(type: string): boolean {
  const owner = ++epoch, revision = recoveryRevision;
  for (const retire of pendingPowerReads) retire();
  pendingPowerReads.clear();
  const current = () => owner === epoch && revision === recoveryRevision;
  for (const observer of [...observers]) {
    if (!current()) break;
    if (!observers.has(observer)) continue;
    try { observer(type); } catch { /* One owner cannot block another's cancellation. */ }
  }
  return current();
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
  const owner = epoch, revision = recoveryRevision;
  for (const [observer, ready] of [...readyObservers]) {
    if (owner !== epoch || revision !== recoveryRevision ||
        !isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle())) break;
    if (readyObservers.get(observer) !== ready) continue;
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
      const scopeChanged = snapshot.observedNotificationSequence !== lastPowerNotificationSequence ||
        snapshot.suspendSerial !== lastPowerSuspendSerial;
      lastPowerPhase = snapshot.phase; lastPowerGeneration = snapshot.generation; lastPowerKnown = known; lastPowerAdmitted = admitted;
      lastPowerNotificationSequence = snapshot.observedNotificationSequence;
      lastPowerSuspendSerial = snapshot.suspendSerial;
      if (admitted) {
        if (recoveryPending) return; // Only this recovery's fresh read may resume trainer work.
        // A nested native fact can retire this Ready while cancellation is fanning out.
        if ((changed || scopeChanged) && !initial &&
            !notifyInvalidated(changed ? 'ipc:power.resumed' : 'ipc:power.observationChanged')) return;
        notifyReady();
      } else if (changed || scopeChanged) notifyInvalidated(snapshot.phase === 'ready' ? 'ipc:power.pending' : 'ipc:power.' + snapshot.phase);
    }, { observeNotificationSequence: true, observeSuspendSerial: true });
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
      lastPowerSuspendSerial = undefined;
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
  const scope = getSharedPowerLifecycle();
  const denied = () => new TrainerPowerChangedError('电源状态未就绪或已改变，本次修改器操作已取消，请恢复完成后重新打开');
  // A direct read can validate a public scope, but only the shared confirmation can release WebView recovery.
  if (recoveryPending || (scope && !isSharedPowerLifecycleAdmitted(scope))) throw denied();
  // A retired read must not hold the worker queue. Keep its late result
  // attached to the original IPC promise, without granting it a new scope.
  let retire!: () => void;
  const retired = new Promise<never>((_, reject) => { retire = () => reject(denied()); });
  pendingPowerReads.add(retire);
  const acceptReadback = beginSharedPowerLifecycleReadback({ confirmReady: true });
  const read = async () => powerLifecycle.get(3000);
  const state = await Promise.race([retired, read()]).finally(() => pendingPowerReads.delete(retire));
  if (expectedEpoch !== epoch || recoveryPending) throw denied();
  // A preflight can discover a missed boundary; current Ready still belongs to the shared read.
  acceptReadback(state);
  if (expectedEpoch !== epoch || recoveryPending || !isSharedPowerLifecycleAdmitted(state)) throw denied();
  const current = getSharedPowerLifecycle();
  const matches = (observed: typeof scope) => !observed || (
    isSharedPowerLifecycleAdmitted(observed) && observed.generation === state.generation &&
    (observed.observedNotificationSequence === undefined || state.observedNotificationSequence === observed.observedNotificationSequence) &&
    (observed.suspendSerial === undefined || state.suspendSerial === observed.suspendSerial));
  if (!Number.isSafeInteger(state.generation) || state.generation <= 0 ||
    expectedGeneration !== undefined && expectedGeneration !== state.generation ||
    (scope && !current) || !matches(scope) || !matches(current)) {
    throw denied();
  }
  return state.generation;
}
