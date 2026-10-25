import { invoke } from './ipc';

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

export async function detectGameCorePolicy(): Promise<GameCorePolicyCapabilities | null> {
  try {
    const raw = await invoke<Partial<GameCorePolicyCapabilities>>('game.corePolicy.detect');
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

export async function applyGameCorePolicy(
  target: GameCorePolicyTarget,
  mode: GameCorePolicyMode,
  hyperThreadMode: GameHyperThreadMode,
): Promise<AppliedGameCorePolicy> {
  return invoke<AppliedGameCorePolicy>('game.corePolicy.apply', {
    ...target,
    mode,
    hyperThreadMode,
  });
}

export async function clearGameCorePolicy(target?: GameCorePolicyTarget): Promise<boolean> {
  const result = await invoke<{ ok?: boolean }>('game.corePolicy.clear', target ?? {});
  return result?.ok === true;
}
