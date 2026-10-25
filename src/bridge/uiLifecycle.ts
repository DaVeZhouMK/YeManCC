// Presentation only. Native remains the sole power/device/output authority.
// Hiding a view pauses its display; it never disables a user's background feature.
export interface UiVisibilityState {
  visible: boolean;
  documentVisible: boolean;
  nativeWindowVisible: boolean;
  powerReady: boolean;
  powerGeneration: number;
}
const UI_VISIBILITY_EVENT = 'ui:visibility';
let nativeWindowVisible = true;
let powerReady = true;
let powerGeneration = 0;
let committedGeneration = 0;
let powerObservation = 0;
let managedPowerReads = false;
let requestPowerRead: (() => void) | null = null;
let stopPowerRead: (() => void) | null = null;

export interface UiCommittedPowerSnapshot {
  phase: 'ready' | 'suspending' | 'suspended' | 'resuming';
  generation: number;
  inputReady: boolean;
  hardwareWritesAllowed: boolean;
}
let lastVisible: boolean | null = null;
let initialized = false;
function documentVisible(): boolean {
  return typeof document === 'undefined' || document.visibilityState === 'visible';
}
export function getUiVisibilityState(): UiVisibilityState {
  const doc = documentVisible();
  return { visible: doc && nativeWindowVisible && powerReady, documentVisible: doc,
    nativeWindowVisible, powerReady, powerGeneration };
}
export function isUiVisible(): boolean { return getUiVisibilityState().visible; }
function emitIfChanged(force = false): void {
  if (typeof window === 'undefined') return;
  const state = getUiVisibilityState();
  if (!force && lastVisible === state.visible) return;
  lastVisible = state.visible;
  window.dispatchEvent(new CustomEvent(UI_VISIBILITY_EVENT, { detail: state }));
}
function eventGeneration(event: Event): number | null {
  const value = (event as CustomEvent<{generation?: unknown}>).detail?.generation;
  return typeof value === 'number' && Number.isSafeInteger(value) && value > 0 ? value : null;
}
function observePowerEvent(event: Event, ready: boolean): void {
  const generation = eventGeneration(event);
  if (generation !== null) {
    if (generation < powerGeneration || (!ready && generation <= committedGeneration)) return;
    powerGeneration = generation;
    if (ready && !managedPowerReads) committedGeneration = Math.max(committedGeneration, generation);
  }
  ++powerObservation;
  if (ready && managedPowerReads) {
    // A final notification is a read trigger, not a substitute for the
    // current native committed state (including legacy untagged events).
    requestNativePowerSnapshot();
    return;
  }
  powerReady = ready;
  emitIfChanged();
}
// A hidden/recreated WebView can miss the one-shot resumed event. Reconcile
// only on existing lifecycle/show/focus edges, using the native read-only state.
// No polling, wake/commit call, input restart, or hardware write is added here.
function requestNativePowerSnapshot(): void {
  requestPowerRead?.();
}
function initialize(): void {
  if (initialized || typeof window === 'undefined') return;
  initialized = true;
  document.addEventListener('visibilitychange', () => {
    emitIfChanged();
    if (documentVisible() && !powerReady) requestNativePowerSnapshot();
  });
  for (const name of ['ipc:window.hidden', 'ipc:window.minimized']) {
    window.addEventListener(name, () => { nativeWindowVisible = false; emitIfChanged(); });
  }
  for (const name of ['ipc:window.shown', 'ipc:window.restored', 'ipc:window.maximized', 'ipc:window.summoned']) {
    window.addEventListener(name, () => {
      nativeWindowVisible = true;
      emitIfChanged();
      if (!powerReady) requestNativePowerSnapshot();
    });
  }
  window.addEventListener('ipc:window.focus', () => { if (!powerReady) requestNativePowerSnapshot(); });
  for (const name of ['ipc:power.suspending', 'ipc:power.resuming']) {
    window.addEventListener(name, event => observePowerEvent(event, false));
  }
  window.addEventListener('ipc:power.resumed', event => observePowerEvent(event, true));
  // resume-ready alone is not a commit; ask the existing native owner.
  window.addEventListener('ipc:power.resume-ready', requestNativePowerSnapshot);
  emitIfChanged(true);
  requestNativePowerSnapshot();
}
initialize();
export function setNativeWindowVisible(visible: boolean): void {
  initialize(); nativeWindowVisible = visible; emitIfChanged();
  if (visible && !powerReady) requestNativePowerSnapshot();
}
export function onUiVisibilityChange(handler: (state: UiVisibilityState) => void): () => void {
  initialize();
  if (typeof window === 'undefined') return () => {};
  const listener = ((event: CustomEvent<UiVisibilityState>) => handler(event.detail)) as EventListener;
  window.addEventListener(UI_VISIBILITY_EVENT, listener);
  handler(getUiVisibilityState());
  return () => window.removeEventListener(UI_VISIBILITY_EVENT, listener);
}

/** Root-owned, event-driven presentation reconciliation. The reader is the
 * existing timeout-bounded powerLifecycle.get; it never commits native power
 * or writes hardware. One active read and one coalesced follow-up are allowed.
 */
export function startUiPowerReconciliation(
  readSnapshot: () => Promise<UiCommittedPowerSnapshot | null>,
  initialSnapshot?: Promise<UiCommittedPowerSnapshot | null>,
): { reconcile: () => void; stop: () => void } {
  initialize();
  if (typeof window === 'undefined' || typeof document === 'undefined') {
    return { reconcile: () => {}, stop: () => {} };
  }
  stopPowerRead?.();
  managedPowerReads = true;
  let stopped = false;
  let inFlight = false;
  let pending = false;
  let initial = initialSnapshot;
  function reconcile(): void {
    if (stopped) return;
    if (inFlight) { pending = true; return; }
    inFlight = true;
    const observation = powerObservation;
    const generation = powerGeneration;
    void Promise.resolve().then(() => {
      const first = initial;
      initial = undefined;
      return first ?? readSnapshot();
    }).then(value => {
      if (stopped || observation !== powerObservation || generation !== powerGeneration) return;
      if (!value || !Number.isSafeInteger(value.generation) || value.generation <= 0) return;
      // A newer native generation can be missed together with its events.
      // The observation/version check above already rejects a reply superseded
      // by a newer local event; only older native generations are stale here.
      if (value.generation < generation) return;
      if (!['ready', 'suspending', 'suspended', 'resuming'].includes(value.phase)) return;
      // Preserve the same-generation terminal commit and the existing event
      // generation guard. An older read may not downgrade a committed Ready.
      if (value.phase !== 'ready' && value.generation <= committedGeneration) return;
      powerGeneration = value.generation;
      powerReady = value.phase === 'ready' && value.inputReady === true &&
        value.hardwareWritesAllowed === true;
      if (powerReady) committedGeneration = Math.max(committedGeneration, powerGeneration);
      emitIfChanged();
    }).catch(() => {
      // Timeout/failure is unknown. Another lifecycle/show edge may retry.
    }).finally(() => {
      inFlight = false;
      if (!stopped && pending) { pending = false; reconcile(); }
    });
  }
  function stop(): void {
    if (stopped) return;
    stopped = true;
    pending = false;
    initial = undefined;
    if (requestPowerRead === reconcile) requestPowerRead = null;
    if (stopPowerRead === stop) stopPowerRead = null;
  }
  requestPowerRead = reconcile;
  stopPowerRead = stop;
  reconcile();
  return { reconcile, stop };
}
