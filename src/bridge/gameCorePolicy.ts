import { invoke } from './ipc';
import type { SharedPowerWriteScope } from './powerLifecycleState';

/**
 * Per-process CPU policy.  This is deliberately separate from the global
 * power-plan core mode and from CCD-wide affinity.
 */
export type GameCorePolicyMode =
  | 'default'
  | 'only-big'
  | 'big-small'
  | 'only-small'
  | 'small-super-small'
  | 'all';

export type GameHyperThreadMode = 'default' | 'on' | 'off';

export interface GameCorePolicyCapabilities {
  detected: boolean;
  heterogeneous: boolean;
  smtAvailable: boolean;
  efficiencyClasses: number[];
  logical: number;
  physical: number;
  smtLogical: number;
  smtPhysical: number;
  source: string;
  groups?: { label: string; physical: number; logical: number }[];
}

export interface GameCorePolicyTarget {
  pid: number;
  processCreated: string;
}

export interface AppliedGameCorePolicy {
  ok: boolean;
  applied: boolean;
  mode: GameCorePolicyMode;
  hyperThreadMode: GameHyperThreadMode;
  cpuSetCount: number;
  affinityMask: string;
  efficiencyClasses: number[];
  error?: string;
}

export async function detectGameCorePolicy(signal?: AbortSignal): Promise<GameCorePolicyCapabilities | null> {
  try {
    const raw = await invoke<Partial<GameCorePolicyCapabilities>>('game.corePolicy.detect', undefined, { signal });
    if (!raw || typeof raw !== 'object') return null;
    const classes = Array.isArray(raw.efficiencyClasses)
      ? raw.efficiencyClasses.map(Number).filter(Number.isFinite)
      : [];
    return {
      detected: raw.detected === true,
      heterogeneous: raw.heterogeneous === true && classes.length >= 2,
      smtAvailable: raw.smtAvailable === true,
      efficiencyClasses: classes,
      logical: Math.max(0, Number(raw.logical) || 0),
      physical: Math.max(0, Number(raw.physical) || 0),
      smtLogical: Math.max(0, Number(raw.smtLogical) || 0),
      smtPhysical: Math.max(0, Number(raw.smtPhysical) || 0),
      groups: Array.isArray(raw.groups) ? raw.groups.filter(g => Number.isSafeInteger(g.physical) && g.physical > 0 && Number.isSafeInteger(g.logical) && g.logical >= g.physical)
        .map((g, index, all) => ({label: all.length === 1 ? '全核心' : index === 0 ? '大核' : index === all.length - 1 && all.length > 2 ? '低功耗核' : '小核', physical:g.physical, logical:g.logical})) : [],
      source: typeof raw.source === 'string' ? raw.source : 'none',
    };
  } catch {
    return null;
  }
}

// Both foreground and resident owners share the same submitted write. An
// explicit edit may try again; an automatic reconciliation needs a receipt or
// a different desired policy to proceed.
let writePending = false;
let uncertainIntent: string | undefined;
const CORE_POLICY_RECEIPT_TIMEOUT_MS = 10_000;

async function writeCorePolicy<T>(command: string, args: object, intent: string, automatic: boolean, busy: T): Promise<T> {
  if (writePending) return busy;
  if (automatic && uncertainIntent === intent) throw new Error('游戏 CPU 设置回执未确认');
  writePending = true;
  uncertainIntent = undefined;
  try {
    return await invoke<T>(command, args, { timeoutMs: CORE_POLICY_RECEIPT_TIMEOUT_MS });
  } catch (error) {
    uncertainIntent = intent;
    throw error;
  } finally {
    writePending = false;
  }
}

export async function applyGameCorePolicy(
  target: GameCorePolicyTarget,
  mode: GameCorePolicyMode,
  hyperThreadMode: GameHyperThreadMode,
  powerScope: SharedPowerWriteScope,
  automatic = false,
): Promise<AppliedGameCorePolicy> {
  return writeCorePolicy<AppliedGameCorePolicy>('game.corePolicy.apply', {
    ...target,
    mode,
    hyperThreadMode,
    powerScope,
  }, JSON.stringify([target.pid, target.processCreated, mode, hyperThreadMode]), automatic,
  { ok: false, applied: false, mode, hyperThreadMode, cpuSetCount: 0, affinityMask: '', efficiencyClasses: [] });
}

export async function clearGameCorePolicy(powerScope: SharedPowerWriteScope, target?: GameCorePolicyTarget, automatic = false): Promise<boolean> {
  const result = await writeCorePolicy<{ ok?: boolean }>('game.corePolicy.clear', { ...target, powerScope }, 'clear', automatic, { ok: false });
  return result?.ok === true;
}
