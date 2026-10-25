import { invoke } from './ipc';
export type DedicatedMemoryProvider = 'intel-gmm' | 'rog-apu' | 'rog-vram';
export type DedicatedMemoryVerification = 'none' | 'pending_restart' | 'verified' | 'default_read' | 'unconfirmed' | 'driver_changed' | 'not_effective';
export interface DedicatedMemoryOption { id: string; raw: number; capacityMiB: number | null; removeOverride: boolean }
export interface DedicatedMemorySnapshot {
  schema: 1; ok: boolean; available: true; settingRoute: 'known-model'; reason: string;
  provider: DedicatedMemoryProvider; adapterName: string; driverVersion: string;
  pnpId: string; luid: string; capacityRead: true; dedicatedBytes: number; selected: string;
  options: DedicatedMemoryOption[]; fence: string; verification: DedicatedMemoryVerification;
  pendingOption?: string;
}
const object = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v);
const uint32 = (v: unknown): v is number => typeof v === 'number' && Number.isInteger(v) && v >= 0 && v <= 0xffffffff;
export function parseDedicatedMemorySnapshot(value: unknown, requireSuccessfulRead = true): DedicatedMemorySnapshot | null {
  if (!object(value) || value.schema !== 1 || value.available !== true || value.settingRoute !== 'known-model' || typeof value.ok !== 'boolean' ||
      (requireSuccessfulRead && !value.ok) || typeof value.reason !== 'string' ||
      (typeof value.provider !== 'string' || !['rog-apu','rog-vram'].includes(value.provider)) ||
      (typeof value.verification !== 'string' || !['none','pending_restart','verified','default_read','unconfirmed','driver_changed','not_effective'].includes(value.verification)) ||
      value.capacityRead !== true || (value.dedicatedBytes === 0 && value.provider !== 'intel-gmm') ||
      typeof value.dedicatedBytes !== 'number' || !Number.isSafeInteger(value.dedicatedBytes) || value.dedicatedBytes < 0 ||
      !Array.isArray(value.options) || value.options.length < 2 || value.options.length > 64) return null;
  for (const key of ['adapterName','driverVersion','pnpId','luid','selected','fence'] as const)
    if (typeof value[key] !== 'string' || !value[key] || value[key].length > 8192) return null;
  const ids = new Set<string>();
  for (const option of value.options) {
    if (!object(option) || typeof option.id !== 'string' || !option.id || ids.has(option.id) || !uint32(option.raw) ||
        typeof option.removeOverride !== 'boolean' ||
        (option.capacityMiB !== null && (!uint32(option.capacityMiB) || option.capacityMiB === 0))) return null;
    ids.add(option.id);
  }
  if (!ids.has(value.selected as string) ||
      (value.pendingOption !== undefined && typeof value.pendingOption !== 'string')) return null;
  return value as unknown as DedicatedMemorySnapshot;
}
export function formatDedicatedCapacity(bytes: number): string {
  if (!Number.isSafeInteger(bytes) || bytes < 0) return '—';
  const sizeMiB = bytes / (1024 * 1024);
  return sizeMiB < 1024 ? `${Number(sizeMiB.toFixed(1))} MiB` : `${Number((sizeMiB / 1024).toFixed(2))} GiB`;
}
export function dedicatedOptionLabel(option: DedicatedMemoryOption): string {
  return option.capacityMiB === null ? '自动' : formatDedicatedCapacity(option.capacityMiB * 1024 * 1024);
}
export function dedicatedConfiguredCapacityMismatch(state: DedicatedMemorySnapshot): boolean {
  const selected = state.options.find(o => o.id === state.selected);
  return state.capacityRead && selected?.capacityMiB != null &&
    state.dedicatedBytes !== selected.capacityMiB * 1024 * 1024;
}
export function dedicatedMemoryStatus(state: DedicatedMemorySnapshot): string {
  switch (state.verification) {
    case 'pending_restart': return '请重启';
    case 'verified': return '已生效';
    case 'not_effective': return '未生效';
    case 'driver_changed':
    case 'unconfirmed': return '未确认';
    default: return dedicatedConfiguredCapacityMismatch(state) ? '未确认' : '';
  }
}
export const gpuDedicatedMemory = {
  read: () => invoke<unknown>('gpu.dedicatedMemory.get', {}, { timeoutMs: 10000 }),
  save: (fence: string, option: string) => invoke<unknown>('gpu.dedicatedMemory.set', { fence, option, confirmed: true }, { timeoutMs: 15000 }),
};
