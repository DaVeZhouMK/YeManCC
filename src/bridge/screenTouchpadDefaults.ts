/** Canonical screen controls shared by live settings and boot presets; no IPC. */
import type { ScreenTouchpadConfig, ScreenTouchpadPersona } from './screenTouchpads';

export const SCREEN_TOUCHPAD_DEFAULT: ScreenTouchpadConfig = {
  masterEnabled: true, summonPosition: 'opposite', summonPlacementVersion: 1, standaloneSpecialMode: 'steamdeck', specialMask: 0, rearMask: 0,
  summonEnabled: true, specialEnabled: false, rearEnabled: false,
  enabled: false, layout: 'off', singleMode: 'mouse', leftMode: 'steamdeck', rightMode: 'steamdeck', transparency: 80, mouseSensitivity: 100, scale: 100, touchFeedback: 'medium', autoHideSeconds: 15,
};

export function screenTouchpadDefaults(persona: ScreenTouchpadPersona): ScreenTouchpadConfig {
  // Presets seed missing profiles only; persisted user settings remain authoritative.
  if (persona === 'steamdeck') return { ...SCREEN_TOUCHPAD_DEFAULT, enabled: true, layout: 'dual', specialMask: 3, specialEnabled: true };
  if (persona === 'dualsense-edge') return { ...SCREEN_TOUCHPAD_DEFAULT, enabled: true, layout: 'single',
    leftMode: 'dualsense', rightMode: 'dualsense', singleMode: 'dualsense', specialMask: 3, specialEnabled: true };
  return { ...SCREEN_TOUCHPAD_DEFAULT, leftMode: 'wasd', rightMode: 'mouse',
    ...(persona === 'elite' ? { specialMask: 1, specialEnabled: true } : {}) };
}

/** Effective editor view only; keep the stored partial preset unchanged. */
export function resolveScreenTouchpadPreset(persona: ScreenTouchpadPersona, saved: Partial<ScreenTouchpadConfig>): ScreenTouchpadConfig {
  const has = (key: keyof ScreenTouchpadConfig) => Object.prototype.hasOwnProperty.call(saved, key);
  const next = { ...screenTouchpadDefaults(persona), ...normalizeScreenTouchpadSummon(saved) };
  // Match native ProfileBank's legacy mask-only interpretation. Explicit OFF,
  // false and zero are intent, not missing values to replace with defaults.
  if (!has('standaloneSpecialMode') && (has('specialMask') || has('specialEnabled'))) next.standaloneSpecialMode = 'off';
  if (has('specialMask')) next.specialEnabled = next.specialMask !== 0;
  else if (has('specialEnabled')) next.specialMask = !next.specialEnabled ? 0 : next.specialMask || 3;
  if (has('rearMask')) next.rearEnabled = next.rearMask !== 0;
  else if (has('rearEnabled')) next.rearMask = !next.rearEnabled ? 0 : next.rearMask || 15;
  // An existing pre-layout profile was dual or disabled, just as in native.
  if (!has('layout')) next.layout = saved.enabled === true ? 'dual' : 'off';
  next.enabled = next.layout !== 'off';
  return next;
}

/** Old absolute/disabled positions migrate once; later player edits remain saved. */
export function normalizeScreenTouchpadSummon<T extends object>(config: T) {
  const input = config as Partial<ScreenTouchpadConfig>;
  const relative = input.summonPosition === 'same' || input.summonPosition === 'opposite';
  const position = input.summonPlacementVersion === 1 || relative
    ? input.summonPosition === 'off' ? 'off' : input.summonPosition === 'same' ? 'same' : 'opposite'
    : 'opposite';
  return { ...config, summonPosition: position, summonPlacementVersion: 1 as const, summonEnabled: position !== 'off' };
}
