/** Current user-selectable virtual outputs. Historical values are accepted only by migration. */
export const CONTROLLER_PERSONA_OPTIONS = [
  { value: 'disabled', label: '关闭虚拟手柄' },
  { value: 'steamdeck', label: 'SteamDeck' },
  { value: 'dualsense-edge', label: 'PS5' },
  { value: 'elite', label: 'Xbox' },
] as const;
export type ControllerPersona = typeof CONTROLLER_PERSONA_OPTIONS[number]['value'];
export type VirtualOutputPersona = Exclude<ControllerPersona, 'disabled'>;
export const VIRTUAL_OUTPUT_PERSONAS = CONTROLLER_PERSONA_OPTIONS
  .filter((option): option is typeof CONTROLLER_PERSONA_OPTIONS[number] & { value: VirtualOutputPersona } => option.value !== 'disabled')
  .map(option => option.value);
export function isVirtualOutputPersona(value: unknown): value is VirtualOutputPersona {
  return typeof value === 'string' && (VIRTUAL_OUTPUT_PERSONAS as readonly string[]).includes(value);
}
export function isControllerPersona(value: unknown): value is ControllerPersona {
  return value === 'disabled' || isVirtualOutputPersona(value);
}
/** One-way migration at settings read boundaries; never admits a retired wire identity. */
export function migrateControllerPersona(value: unknown): ControllerPersona {
  if (value === 'xbox360') return 'elite';
  if (value === 'dualshock4' || value === 'dualsense') return 'dualsense-edge';
  return isControllerPersona(value) ? value : 'disabled';
}
