import type { InputSettingsSnapshot } from '@/bridge/settingsRepository';
import type { GameCustomProfile } from '@/bridge/performanceSchedule';

/** Legacy profile values are retained only for reading older documents. */
export type PadPersonaOverride = NonNullable<GameCustomProfile['padPersona']>;
export type GyroOverride = NonNullable<GameCustomProfile['gyroOverride']>;
export function isPadPersonaOverride(value: unknown): value is PadPersonaOverride {
  return typeof value === 'string' && ['follow', 'disabled', 'steamdeck', 'dualsense-edge', 'elite'].includes(value);
}
export function isGyroOverride(value: unknown): value is GyroOverride {
  return typeof value === 'string' && ['follow', 'on', 'off', 'fps', 'racing', 'custom', 'steam'].includes(value);
}

export interface GameInputOverrideState { locked: boolean; identity: string }
const unlocked: GameInputOverrideState = Object.freeze({ locked: false, identity: '' });
export function getGameInputOverrideState(): GameInputOverrideState { return unlocked; }
export function subscribeGameInputOverrideState(cb: (state: GameInputOverrideState) => void): () => void {
  cb(unlocked);
  return () => {};
}
/** Global settings, including the selected boot mode, exclusively own input. */
export function effectiveInputPersona(input: InputSettingsSnapshot): string {
  return String(input.outputTarget?.persona ?? 'disabled');
}
export function buildGameInputOverlay(_input: InputSettingsSnapshot, _profile: GameCustomProfile | undefined,
  _identity: string, _sessionId: string): Record<string, unknown> | null {
  return null;
}
// Compatibility exports: no subscriptions, legacy restore or overlay writes.
export function startGameInputOverrideWatch(): void {}
export function stopGameInputOverrideWatch(): void {}
