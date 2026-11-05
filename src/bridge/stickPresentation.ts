import { onActivated, onBeforeUnmount, onDeactivated, onMounted, shallowRef, watch } from 'vue';
import { createStickPresentation, type StickPoint } from './gyroPresentation';
import { isUiVisible, onUiVisibilityChange } from './uiLifecycle';

/** Shared by the gyro output and controller calibration drawings only. */
export function useStickPresentation(source: () => StickPoint | null, enabled: () => boolean,
    identity: () => string, durationMs: number, receivedAt?: () => number) {
  const point = shallowRef<StickPoint>({ x: 0, y: 0 });
  const display = createStickPresentation({ durationMs, present: value => { point.value = value; } });
  let mounted = false, active = false, visible = isUiVisible(), focused = true;
  let freshAfter = -Infinity;
  let stopVisibility: (() => void) | null = null;
  const reducedMotion = typeof window !== 'undefined' && typeof window.matchMedia === 'function'
    ? window.matchMedia('(prefers-reduced-motion: reduce)') : null;
  function receive(snap = false) {
    const value = source();
    const ready = mounted && active && visible && focused && enabled() && value !== null &&
      (!receivedAt || receivedAt() > freshAfter);
    display.setActive(ready);
    if (ready && value) display.push(value, snap || reducedMotion?.matches === true);
  }
  // Visibility restoration waits for the next real sample; it does not replay
  // an old cached position or fabricate a new source receipt.
  function visibility(value: boolean) {
    if (visible === value) return;
    visible = value; freshAfter = performance.now(); display.setActive(false);
  }
  function activate() { if (active) return; active = true; freshAfter = performance.now(); }
  function blur() { focused = false; freshAfter = performance.now(); display.setActive(false); }
  function focus() { if (focused) return; focused = true; freshAfter = performance.now(); }
  function deactivate() { active = false; display.setActive(false); }
  watch([source, enabled, identity, () => receivedAt?.() ?? 0], (current, previous) => {
    const changedSource = previous && current[2] !== previous[2];
    if (changedSource) {
      display.reset();
      if (receivedAt) freshAfter = performance.now();
    }
    receive(!!changedSource);
  }, { flush: 'sync' });
  onMounted(() => {
    mounted = true; active = true;
    stopVisibility = onUiVisibilityChange(value => visibility(value.visible));
    window.addEventListener('ipc:window.blur', blur); window.addEventListener('ipc:window.focus', focus);
    receive(true);
  });
  onActivated(activate);
  onDeactivated(deactivate);
  onBeforeUnmount(() => {
    mounted = false; active = false; stopVisibility?.(); stopVisibility = null;
    window.removeEventListener('ipc:window.blur', blur); window.removeEventListener('ipc:window.focus', focus);
    display.dispose();
  });
  return point;
}
