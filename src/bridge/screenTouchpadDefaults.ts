/** Canonical screen controls shared by live settings and boot presets; no IPC. */
import type { ScreenTouchpadConfig, ScreenTouchpadPersona } from './screenTouchpads';

export const SCREEN_TOUCHPAD_DEFAULT: ScreenTouchpadConfig = {
  masterEnabled: true, summonPosition: 'opposite', summonPlacementVersion: 1, standaloneSpecialMode: 'off', specialMask: 0, rearMask: 0,
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

/** Old absolute/disabled positions migrate once; later player edits remain saved. */
export function normalizeScreenTouchpadSummon<T extends object>(config: T) {
  const input = config as Partial<ScreenTouchpadConfig>;
  const relative = input.summonPosition === 'same' || input.summonPosition === 'opposite';
  const position = input.summonPlacementVersion === 1 || relative
    ? input.summonPosition === 'off' ? 'off' : input.summonPosition === 'same' ? 'same' : 'opposite'
    : 'opposite';
  return { ...config, summonPosition: position, summonPlacementVersion: 1 as const, summonEnabled: position !== 'off' };
}
