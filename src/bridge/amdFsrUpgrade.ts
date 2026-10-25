import { shallowRef } from 'vue';
import { invoke } from './ipc';

export interface AmdFsrAdapter {
  id: string; name: string; driverVersion: string; fence: string;
  amd: true; hardwareCandidate: boolean; supported: boolean; enabled: boolean | null;
  upgradeRecommended: boolean; automaticAttempted: boolean; reason: string; gpuType: 'integrated' | 'discrete' | 'unknown';
}
export interface AmdFsrSnapshot {
  schema: 1; ok: boolean; reason: string; defaultEnabled: boolean;
  versionConfirmed: false; adapters: AmdFsrAdapter[];
}
const object = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v);
export function parseAmdFsrSnapshot(value: unknown): AmdFsrSnapshot | null {
  if (!object(value) || value.schema !== 1 || typeof value.ok !== 'boolean' || typeof value.reason !== 'string' ||
      typeof value.defaultEnabled !== 'boolean' || value.versionConfirmed !== false ||
      !Array.isArray(value.adapters) || value.adapters.length > 32) return null;
  const ids = new Set<string>();
  for (const a of value.adapters) {
    if (!object(a) || a.amd !== true || typeof a.hardwareCandidate !== 'boolean' || typeof a.supported !== 'boolean' ||
        typeof a.upgradeRecommended !== 'boolean' || typeof a.automaticAttempted !== 'boolean' ||
        !['integrated', 'discrete', 'unknown'].includes(String(a.gpuType)) ||
        (a.enabled !== null && typeof a.enabled !== 'boolean')) return null;
    for (const key of ['id', 'name', 'driverVersion', 'fence', 'reason'])
      if (typeof a[key] !== 'string' || a[key].length > 8192) return null;
    if (!a.id || ids.has(a.id as string) || (a.supported && !a.fence) ||
        (a.enabled !== null && !a.supported) || (a.supported && a.gpuType !== 'integrated') ||
        a.gpuType === 'discrete' || (a.upgradeRecommended && (!a.hardwareCandidate || a.supported))) return null;
    ids.add(a.id as string);
  }
  return value as unknown as AmdFsrSnapshot;
}
export const amdFsrState = shallowRef<AmdFsrSnapshot | null>(null);
export function amdFsrStatus(a: AmdFsrAdapter): string {
  if (a.enabled === true) return '驱动全局开关已开启';
  if (a.enabled === false) return '驱动全局开关已关闭';
  if (a.upgradeRecommended) return '当前核显或驱动未提供此能力，可检查 AMD 显卡驱动更新';
  if (a.reason === 'not_supported') return '当前显卡或驱动不支持 FSR 全局升级';
  return '当前驱动状态未确认';
}
export function amdFsrError(reason: string): string {
  switch (reason) {
    case 'policy_unreadable': return '无法读取首次开启记录，请检查配置文件。';
    case 'policy_save_failed': return '无法保存首次开启记录，未继续设置显卡驱动。';
    case 'policy_busy': return '另一个请求正在使用显卡设置，请稍后重新检测。';
    case 'adapter_changed': return '显卡或驱动已变化，请重新检测后再设置。';
    case 'enable_unconfirmed': return '首次开启未确认；可检查驱动后手动开启。';
    default: return '设置结果未确认，请重新检测当前状态。';
  }
}
export const AMD_FSR_DRIVER_URL = 'https://www.amd.com/en/support/download/drivers.html';

/** Created only by the visible Settings/device consumer. No app-start hook,
 * polling, focus refresh, or persistent background driver ownership. */
export function createAmdFsrPage() {
  const owner = `fsr-${crypto.randomUUID()}`;
  let active = false, epoch = 0;
  let abort: AbortController | undefined;
  let pending = false;
  const current = (ticket: number) => active && epoch === ticket;
  async function request(action: 'get' | 'initialize' | 'set', args: Record<string, unknown>, ticket: number) {
    if (!current(ticket)) return null;
    try {
      const raw = await invoke<unknown>(`gpu.fsr.${action}`, { ...args, owner }, { timeoutMs: 20000, signal: abort?.signal });
      if (!current(ticket)) return null;
      const state = parseAmdFsrSnapshot(raw);
      if (!state) throw new Error('AMD FSR 驱动接口返回无效结果');
      amdFsrState.value = state;
      return state;
    } catch (error) {
      if (!current(ticket)) return null;
      const old = amdFsrState.value;
      if (old) amdFsrState.value = {
        ...old, ok: false, reason: 'transport_unconfirmed',
        adapters: old.adapters.map(a => ({ ...a, enabled: null, reason: 'state_unreadable', upgradeRecommended: false })),
      };
      throw error;
    }
  }
  async function operate(action: 'get' | 'set', args: Record<string, unknown> = {}) {
    if (!active || pending) return null;
    pending = true;
    const ticket = epoch;
    try { return await request(action, args, ticket); }
    finally { if (current(ticket)) pending = false; }
  }
  return {
    async enter() {
      if (active) return null;
      active = true; pending = true; const ticket = ++epoch;
      abort = new AbortController(); amdFsrState.value = null;
      try {
        const ready = await invoke<{ok: boolean}>('gpu.fsr.page', { owner, active: true }, { timeoutMs: 2000, signal: abort.signal });
        if (!current(ticket)) return null;
        if (ready?.ok !== true) throw new Error('AMD FSR 设备页尚未就绪');
        // Default remains ON once, but its first opportunity is THIS visible
        // page visit. Opening the app or other settings tabs does nothing.
        return await request('initialize', {}, ticket);
      } finally { if (current(ticket)) pending = false; }
    },
    read: () => operate('get'),
    set: (a: AmdFsrAdapter, enabled: boolean) => operate('set', { id: a.id, fence: a.fence, enabled }),
    close() {
      if (!active) return;
      active = false; pending = false; ++epoch;
      abort?.abort(); abort = undefined; amdFsrState.value = null;
      // Metadata-only native cancellation runs on the UI owner, independently
      // of a stalled/queued driver request. No write is replayed on timeout.
      void invoke('gpu.fsr.page', { owner, active: false }, { timeoutMs: 2000 }).catch(error => {
        console.warn('[AMD FSR] 页面取消未确认', error);
      });
    },
  };
}
