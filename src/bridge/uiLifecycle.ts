// Shared frontend-only lifecycle. Native power, input and scheduling work do
// not depend on this state; it only gates work whose purpose is to refresh UI.

export interface UiVisibilityState {
  visible: boolean;
  documentVisible: boolean;
  nativeWindowVisible: boolean;
  powerReady: boolean;
}

const UI_VISIBILITY_EVENT = 'ui:visibility';
let nativeWindowVisible = true;
let powerReady = true;
let lastVisible: boolean | null = null;
let initialized = false;

function documentVisible(): boolean {
  return typeof document === 'undefined' || document.visibilityState === 'visible';
}

export function getUiVisibilityState(): UiVisibilityState {
  const doc = documentVisible();
  return {
    visible: doc && nativeWindowVisible && powerReady,
    documentVisible: doc,
    nativeWindowVisible,
    powerReady,
  };
}

export function isUiVisible(): boolean {
  return getUiVisibilityState().visible;
}

function emitIfChanged(force = false): void {
  if (typeof window === 'undefined') return;
  const state = getUiVisibilityState();
  if (!force && lastVisible === state.visible) return;
  lastVisible = state.visible;
  window.dispatchEvent(new CustomEvent(UI_VISIBILITY_EVENT, { detail: state }));
}

function initialize(): void {
  if (initialized || typeof window === 'undefined') return;
  initialized = true;
  document.addEventListener('visibilitychange', () => emitIfChanged());
  for (const event of ['ipc:window.hidden', 'ipc:window.minimized']) {
    window.addEventListener(event, () => {
      nativeWindowVisible = false;
      emitIfChanged();
    });
  }
  for (const event of ['ipc:window.shown', 'ipc:window.restored', 'ipc:window.maximized', 'ipc:window.summoned']) {
    window.addEventListener(event, () => {
      nativeWindowVisible = true;
      emitIfChanged();
    });
  }
  for (const event of ['ipc:power.suspending', 'ipc:power.resuming']) {
    window.addEventListener(event, () => {
      powerReady = false;
      emitIfChanged();
    });
  }
  window.addEventListener('ipc:power.resumed', () => {
    powerReady = true;
    emitIfChanged();
  });
  emitIfChanged(true);
}

initialize();

export function setNativeWindowVisible(visible: boolean): void {
  initialize();
  nativeWindowVisible = visible;
  emitIfChanged();
}

export function onUiVisibilityChange(handler: (state: UiVisibilityState) => void): () => void {
  initialize();
  if (typeof window === 'undefined') return () => {};
  const listener = ((event: CustomEvent<UiVisibilityState>) => handler(event.detail)) as EventListener;
  window.addEventListener(UI_VISIBILITY_EVENT, listener);
  handler(getUiVisibilityState());
  return () => window.removeEventListener(UI_VISIBILITY_EVENT, listener);
}
