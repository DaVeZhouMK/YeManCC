/** Boot intent is independent of the buttons controlling the current session. */
export type StartupPadPersona = 'disabled' | 'steamdeck' | 'dualsense-edge' | 'elite';
export type StartupGyroPreset = 'off' | 'fps' | 'racing' | 'custom' | 'steam';
export interface InputStartupPreferences {
  virtualGamepadPersona: StartupPadPersona;
  gyroPreset: StartupGyroPreset;
}
export const STARTUP_PAD_OPTIONS = [
  { value: 'disabled', label: '关闭虚拟手柄' },
  { value: 'steamdeck', label: 'SteamDeck' },
  { value: 'dualsense-edge', label: 'PS5' },
  { value: 'elite', label: 'Xbox' },
] as const;
export const STARTUP_GYRO_OPTIONS = [
  { value: 'off', label: '关闭陀螺仪' },
  { value: 'fps', label: 'FPS射击' },
  { value: 'racing', label: '赛车' },
  { value: 'custom', label: '自定义' },
  { value: 'steam', label: 'Steam' },
] as const;
export function normalizeInputStartupPreferences(value: unknown): InputStartupPreferences {
  const raw = value && typeof value === 'object' && !Array.isArray(value)
    ? value as Record<string, unknown> : {};
  const persona = STARTUP_PAD_OPTIONS.some(option => option.value === raw.virtualGamepadPersona)
    ? raw.virtualGamepadPersona as StartupPadPersona : 'disabled';
  const gyro = persona !== 'disabled' && STARTUP_GYRO_OPTIONS.some(option => option.value === raw.gyroPreset)
    ? raw.gyroPreset as StartupGyroPreset : 'off';
  return { virtualGamepadPersona: persona, gyroPreset: gyro };
}
