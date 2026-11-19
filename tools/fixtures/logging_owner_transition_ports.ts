// Explicit external ports for the logging-only complete-engine fixture.
// No device, app, files, browser navigation or native admission implementation.
const state = () => (globalThis as any).__ownerFixturePorts;
export const ROUTES = [{path:'/fixture',title:'fixture'}];
export const performancePanelTarget = () => null;
export const EDIT_REPEAT_BASE_MS = 150;
export const editRepeatInterval = () => 150;
export const isRadialTestingEnabled = () => false;
export const loadPerformanceSchedule = async () => ({enabled:false});
export const summonGet = async () => ({});
export function watchMouseModeState(callback: (value: {on:boolean}) => void) {
  state().business.push(['mouse-watch']); callback({on:false});
  return () => state().business.push(['mouse-unwatch']);
}
export const isUiVisible = () => state().visible;
export function getUiVisibilityState() {
  if (state().throwSnapshot) throw Error('fixture-diagnostic-snapshot');
  return {visible:state().visible,nativeWindowVisible:state().visible,powerReady:true};
}
export const onUiVisibilityChange = (fn: (value:{visible:boolean}) => void) => {state().visibility=fn;return () => {};};
export const focusGamepadElement = () => {};
export const setGamepadFocused = () => {};
export const recoverGamepadFocus = () => {};
export const gamepadSerialPending = () => 0;
export const enqueueGamepadTaskDetached = (fn: () => unknown) => {state().business.push(['queue']);fn();};
export const residentNavigationRoutes = (routes:unknown[]) => routes;
export const spatialNavigationTarget = () => null;
export const createSpatialMeasurements = () => ({});
export const withGamepadActivation = (_el: unknown, fn: () => unknown) => fn();
