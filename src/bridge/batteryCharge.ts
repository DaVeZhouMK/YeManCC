import { invoke } from './ipc';
import { reportFrontendError } from '@/robust/frontendDiagnostics';
import type { DropdownOption } from '@/components/Dropdown.vue';
export interface BatteryChargeState {
  schema: 1; ok: boolean; supported: boolean; batteryPresent: boolean;
  model: string; family: string; kind: string; choices: number[];
  currentLimit: number | null; requestedLimit: number | null; managed: boolean;
  verification: string; fence: string; reason: string;
}
const percent = (value: unknown): value is number => Number.isInteger(value) && Number(value) >= 0 && Number(value) <= 100;
export function parseBatteryChargeState(value: unknown): BatteryChargeState {
  if (!value || typeof value !== 'object') throw new Error('充电状态格式异常');
  const v = value as Record<string, unknown>;
  if (v.schema !== 1 || ['ok', 'supported', 'batteryPresent', 'managed'].some(k => typeof v[k] !== 'boolean') ||
      ['model', 'family', 'kind', 'verification', 'fence', 'reason'].some(k => typeof v[k] !== 'string') ||
      !Array.isArray(v.choices) || v.choices.length > 21 || v.choices.some(n => !percent(n)) ||
      new Set(v.choices).size !== v.choices.length ||
      (v.currentLimit !== null && !percent(v.currentLimit)) || (v.requestedLimit !== null && !percent(v.requestedLimit)) ||
      (v.supported && (v.choices.length === 0 || !/^[A-Fa-f0-9]{64}$/.test(String(v.fence))))) throw new Error('充电状态未能确认，请重新读取');
  if (v.kind === 'firmware-fixed' && v.choices.some(n => n !== 80 && n !== 100)) throw new Error('养护模式档位异常');
  return v as unknown as BatteryChargeState;
}
export function batteryChargeOptions(state: BatteryChargeState): DropdownOption[] {
  const options: DropdownOption[] = state.choices.filter(value => value >= 50).map(value => ({ value, label: `${value}%` }));
  if (state.currentLimit !== null && !options.some(option => option.value === state.currentLimit)) options.unshift({value: state.currentLimit, label: `${state.currentLimit}%（当前设备值）`, disabled: true});
  return options;
}
export function batteryChargeStatus(state: BatteryChargeState): string {
  if (!state.supported && state.model && !state.batteryPresent) return '不支持（未检测到内置电池）';
  if (!state.supported && state.kind === 'unsupported' && state.model) return '此设备暂不支持充电上限';
  if (!state.ok || !state.supported) return state.verification === 'unconfirmed' ? '当前设置未确认' : '当前不可用';
  if (state.kind === 'software-bypass-threshold') return state.managed && state.verification === 'software-active'
    ? `当前目标：${state.requestedLimit}%` : '当前：100%';
  if (state.verification === 'acknowledged') return `当前设置：${state.requestedLimit}%（已接受）`;
  if (state.currentLimit !== null) return `当前：${state.currentLimit}%`;
  return '当前上限未知';
}
const request = async (operation: string, args: Record<string, unknown> = {}) => {
  try { return parseBatteryChargeState(await invoke<unknown>(`battery.charge.${operation}`, args, { timeoutMs: 35000 })); }
  catch (error) {
    // Retain evidence even when an old/missing native command never reaches BatteryHost.
    try { reportFrontendError(`battery-charge.${operation}`, error); } catch { /* Logging cannot mask the request failure. */ }
    throw error;
  }
};
export const batteryCharge = {
  get: () => request('get'), refresh: () => request('refresh'),
  set: (fence: string, target: number) => request('set', { fence, target }),
  release: () => request('release'),
};
