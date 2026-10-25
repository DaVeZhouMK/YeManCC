import { invoke, isNativeRuntime } from './ipc';
import { screenTouchpadDefaults } from './screenTouchpadDefaults';
export { screenTouchpadDefaults, SCREEN_TOUCHPAD_DEFAULT } from './screenTouchpadDefaults';

export type ScreenTouchpadLayout = 'off' | 'dual' | 'single';
export type ScreenAutoHideSeconds = 0 | 15 | 30 | 45 | 60 | 120;
export type TouchFeedbackMode = 'off' | 'minimal' | 'medium' | 'strong';
export type StandaloneSpecialMode = 'off' | 'steamdeck' | 'ps5';
export type ScreenSummonPosition = 'off' | 'left' | 'right';
export type SingleTouchpadMode = 'dualsense' | 'wasd' | 'arrows' | 'mouse';
export type LeftTouchpadMode = 'steamdeck' | 'dualsense' | 'wasd' | 'arrows' | 'mouse' | 'off';
export type RightTouchpadMode = 'steamdeck' | 'dualsense' | 'mouse' | 'off';
export interface ScreenTouchpadConfig {
  /** Master gate for this persona; layout and all mappings stay saved while off. */
  masterEnabled: boolean;
  enabled: boolean; // Legacy/native compatibility; layout is the UI authority.
  layout: ScreenTouchpadLayout;
  singleMode: SingleTouchpadMode;
  leftMode: LeftTouchpadMode;
  rightMode: RightTouchpadMode;
  transparency: number;
  autoHideSeconds: ScreenAutoHideSeconds;
  mouseSensitivity: number;
  scale: number;
  touchFeedback: TouchFeedbackMode;
  summonPosition: ScreenSummonPosition;
  standaloneSpecialMode: StandaloneSpecialMode; // Dedicated-key pair in the disabled profile only.
  specialMask: number;
  rearMask: number;
  // Legacy fields remain in the wire contract for old saved profiles.
  summonEnabled: boolean;
  specialEnabled: boolean;
  rearEnabled: boolean;
}
export type ScreenTouchpadPersona = 'disabled' | 'steamdeck' | 'dualsense-edge' | 'elite';
export function screenTouchpadProfile(persona: string): ScreenTouchpadPersona {
  if (persona === 'steamdeck') return 'steamdeck';
  if (persona === 'dualsense-edge') return 'dualsense-edge';
  if (persona === 'elite') return 'elite';
  return 'disabled';
}
export interface ScreenTouchpadState extends ScreenTouchpadConfig {
  persona?: ScreenTouchpadPersona; steamDeckAvailable?: boolean; ps5Available?: boolean;
  ok: boolean; available: boolean; visible: boolean;
  events?: number; injectedInputs?: number; repaints?: number;
  autoHidden?: boolean;
  error?: number; reason?: string; settingsSaved?: boolean;
  standaloneSpecialStatus?: string; standaloneSpecialRequests?: number; standaloneSpecialCompleted?: number;
}
export const STANDALONE_SPECIAL_MODES = [
  { value: 'off', label: '关闭' }, { value: 'steamdeck', label: '开启Steam全部' }, { value: 'ps5', label: '开启PS5全部' },
] as const;
export const SCREEN_SUMMON_POSITIONS = [
  { value: 'off', label: '关闭' }, { value: 'left', label: '左侧呼出' }, { value: 'right', label: '右侧呼出' },
] as const;
export const SCREEN_AUTO_HIDE_OPTIONS = [
  { value: 0, label: '关闭' }, { value: 15, label: '15秒' }, { value: 30, label: '30秒' },
  { value: 45, label: '45秒' }, { value: 60, label: '60秒' }, { value: 120, label: '120秒' },
] as const;
export const TOUCH_FEEDBACK_MODES = [
  { value: 'off', label: '关闭' },
  { value: 'minimal', label: '最小震动反馈' },
  { value: 'medium', label: '中震动反馈' },
  { value: 'strong', label: '强震动反馈' },
] as const;
export const SCREEN_TOUCHPAD_LAYOUTS = [
  { value: 'off', label: '关闭' }, { value: 'dual', label: '双触摸板' }, { value: 'single', label: '单触摸板' },
] as const;
export const SINGLE_TOUCHPAD_MODES = [
  { value: 'dualsense', label: 'PS5触摸板 (Steam内设置)' },
  { value: 'wasd', label: '键盘 W / A / S / D' }, { value: 'arrows', label: '键盘 ↑ / ← / ↓ / →' },
  { value: 'mouse', label: '模拟鼠标' },
] as const;
export const LEFT_TOUCHPAD_MODES = [
  { value: 'steamdeck', label: 'SteamDeck 左触摸板 (Steam内设置)' },
  { value: 'dualsense', label: 'PS5 左触摸板 (Steam内设置)' },
  { value: 'wasd', label: '键盘 W / A / S / D' },
  { value: 'arrows', label: '键盘 ↑ / ← / ↓ / →' },
  { value: 'mouse', label: '模拟鼠标' },
  { value: 'off', label: '关闭左侧区域' },
] as const;
export const RIGHT_TOUCHPAD_MODES = [
  { value: 'steamdeck', label: 'SteamDeck 右触摸板 (Steam内设置)' },
  { value: 'dualsense', label: 'PS5 右触摸板 (Steam内设置)' },
  { value: 'mouse', label: '模拟鼠标' },
  { value: 'off', label: '关闭右侧区域' },
] as const;
export function validateScreenTouchpadPatch(patch: Partial<ScreenTouchpadConfig>): boolean {
  const entries = Object.entries(patch);
  return entries.every(([key, value]) => {
    switch (key) {
      case 'layout': return SCREEN_TOUCHPAD_LAYOUTS.some(v => v.value === value);
      case 'autoHideSeconds': return SCREEN_AUTO_HIDE_OPTIONS.some(option => option.value === value);
      case 'touchFeedback': return TOUCH_FEEDBACK_MODES.some(v => v.value === value);
      case 'singleMode': return SINGLE_TOUCHPAD_MODES.some(v => v.value === value);
      case 'masterEnabled': case 'enabled': case 'summonEnabled': case 'specialEnabled': case 'rearEnabled': return typeof value === 'boolean';
      case 'summonPosition': return value === 'off' || value === 'left' || value === 'right';
      case 'standaloneSpecialMode': return STANDALONE_SPECIAL_MODES.some(mode => mode.value === value);
      case 'specialMask': return Number.isInteger(value) && Number(value) >= 0 && Number(value) <= 3;
      case 'rearMask': return Number.isInteger(value) && Number(value) >= 0 && Number(value) <= 15;
      case 'leftMode': return LEFT_TOUCHPAD_MODES.some(v => v.value === value);
      case 'rightMode': return RIGHT_TOUCHPAD_MODES.some(v => v.value === value);
      case 'transparency': return Number.isInteger(value) && Number(value) >= 0 && Number(value) <= 100;
      case 'scale': return Number.isInteger(value) && Number(value) >= 50 && Number(value) <= 200 && Number(value) % 5 === 0;
      case 'mouseSensitivity': return Number.isInteger(value) && Number(value) >= 10 && Number(value) <= 300;
      default: return false;
    }
  });
}
const preview: Partial<Record<ScreenTouchpadPersona, ScreenTouchpadState>> = {};
function previewState(persona: ScreenTouchpadPersona): ScreenTouchpadState {
  return preview[persona] ??= { ...screenTouchpadDefaults(persona), persona, ok: true, available: true,
    steamDeckAvailable: false, ps5Available: false, visible: false, reason: 'preview-only' };
}
export function screenTouchpadsGet(persona: ScreenTouchpadPersona = 'steamdeck'): Promise<ScreenTouchpadState> {
  return isNativeRuntime ? invoke<ScreenTouchpadState>('screenTouchpads.get', { persona }) : Promise.resolve({ ...previewState(persona) });
}
export function screenTouchpadsSet(patch: Partial<ScreenTouchpadConfig>, persona: ScreenTouchpadPersona = 'steamdeck'): Promise<ScreenTouchpadState> {
  if (!validateScreenTouchpadPatch(patch) || (persona !== 'steamdeck' && (patch.leftMode === 'steamdeck' || patch.rightMode === 'steamdeck')) || (persona !== 'dualsense-edge' && (patch.singleMode === 'dualsense' || patch.leftMode === 'dualsense' || patch.rightMode === 'dualsense')) ||
      (persona === 'disabled' && ((patch.specialEnabled === true) || (patch.specialMask !== undefined && Number(patch.specialMask) !== 0))) ||
      (!['steamdeck','dualsense-edge'].includes(persona) && ((patch.rearEnabled === true) || (patch.rearMask !== undefined && Number(patch.rearMask) !== 0))))
    return Promise.reject(new Error('触摸板设置超出允许范围'));
  if (!isNativeRuntime) { const next = { ...previewState(persona), ...patch };
    if (patch.layout !== undefined) next.enabled = patch.layout !== 'off';
    else if (patch.enabled !== undefined) next.layout = patch.enabled ? 'dual' : 'off';
    preview[persona] = next; return Promise.resolve({ ...preview[persona]! }); }
  return invoke<ScreenTouchpadState>('screenTouchpads.set', { persona, config: patch });
}
