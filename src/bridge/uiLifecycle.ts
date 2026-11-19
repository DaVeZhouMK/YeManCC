import { onSharedPowerLifecycle, requestSharedPowerLifecycleReconcile, type SharedPowerLifecycleSnapshot } from './powerLifecycleState';

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
export function isUiVisible(): boolean { return documentVisible() && nativeWindowVisible && powerReady; }
/** Presentation Ready is independent of device/input/hardware admission. */
export function isUiPowerReady(state: SharedPowerLifecycleSnapshot | null | undefined): boolean {
  return !!state && state.known !== false && state.phase === 'ready' && state.notificationPending !== true;
}
function emitIfChanged(force = false): void {
  if (typeof window === 'undefined') return;
  const state = getUiVisibilityState();
  if (!force && lastVisible === state.visible) return;
  lastVisible = state.visible;
  window.dispatchEvent(new CustomEvent(UI_VISIBILITY_EVENT, { detail: state }));
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

/** Presentation subscribes to the one native power source. Legacy reader
 * arguments remain signature-compatible but never create another power reader.
 */
export function startUiPowerReconciliation(
  _readSnapshot?: () => Promise<UiCommittedPowerSnapshot | null>,
  _initialSnapshot?: Promise<UiCommittedPowerSnapshot | null>,
): { reconcile: () => void; stop: () => void } {
  initialize();
  if (typeof window === 'undefined' || typeof document === 'undefined') {
    return { reconcile: () => {}, stop: () => {} };
  }
  stopPowerRead?.();
  let stopped = false;
  const release = onSharedPowerLifecycle(state => {
    if (stopped) return;
    powerGeneration = state.generation;
    // Presentation follows system Ready; device input readiness is not a UI gate.
    powerReady = isUiPowerReady(state);
    emitIfChanged();
  });
  function reconcile(): void {
    if (!stopped) requestSharedPowerLifecycleReconcile('ui-lifecycle');
  }
  function stop(): void {
    if (stopped) return;
    stopped = true; release();
    if (requestPowerRead === reconcile) requestPowerRead = null;
    if (stopPowerRead === stop) stopPowerRead = null;
  }
  requestPowerRead = reconcile; stopPowerRead = stop;
  reconcile();
  return { reconcile, stop };
}
