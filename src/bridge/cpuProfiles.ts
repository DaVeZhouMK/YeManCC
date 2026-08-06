import { fs } from './api';
import type { PowerParams } from './yeman';

export const CPU_PROFILES_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\cpu_profiles.json';

export type CpuProfileId = 'balanced' | 'turbo' | 'elite' | 'extreme';
export type CpuProfileValues = Omit<PowerParams, 'sides'>;

export interface CpuProfilesConfig {
  version: 1;
  active: CpuProfileId;
  profiles: Record<CpuProfileId, CpuProfileValues>;
}

export const CPU_PROFILE_META: ReadonlyArray<{
  id: CpuProfileId;
  label: string;
  path: string;
}> = [
  { id: 'balanced', label: '平衡', path: 'C:\\SOFT\\YeMan\\PowerControl\\TDP\\Performance.vbs' },
  { id: 'turbo', label: '高性能', path: 'C:\\SOFT\\YeMan\\PowerControl\\TDP\\Turbo.vbs' },
  { id: 'elite', label: '精睿', path: 'C:\\SOFT\\YeMan\\PowerControl\\TDP\\Elite.vbs' },
  { id: 'extreme', label: '极致', path: 'C:\\SOFT\\YeMan\\PowerControl\\TDP\\Extreme.vbs' },
] as const;

const DEFAULT_PROFILES: Record<CpuProfileId, CpuProfileValues> = {
  balanced: {
    acFreq: 2000,
    dcFreq: 2000,
    acTurbo: false,
    dcTurbo: false,
    acAggr: 10,
    dcAggr: 10,
  },
  turbo: {
    acFreq: 4000,
    dcFreq: 3000,
    acTurbo: true,
    dcTurbo: true,
    acAggr: 50,
    dcAggr: 30,
  },
  elite: {
    acFreq: 4500,
    dcFreq: 4000,
    acTurbo: true,
    dcTurbo: true,
    acAggr: 70,
    dcAggr: 50,
  },
  extreme: {
    acFreq: 0,
    dcFreq: 0,
    acTurbo: true,
    dcTurbo: true,
    acAggr: 100,
    dcAggr: 100,
  },
};

function cloneValues(values: CpuProfileValues): CpuProfileValues {
  return { ...values };
}

export function defaultCpuProfilesConfig(): CpuProfilesConfig {
  return {
    version: 1,
    active: 'balanced',
    profiles: {
      balanced: cloneValues(DEFAULT_PROFILES.balanced),
      turbo: cloneValues(DEFAULT_PROFILES.turbo),
      elite: cloneValues(DEFAULT_PROFILES.elite),
      extreme: cloneValues(DEFAULT_PROFILES.extreme),
    },
  };
}

function isProfileId(value: unknown): value is CpuProfileId {
  return CPU_PROFILE_META.some((item) => item.id === value);
}

function clampNumber(value: unknown, fallback: number, min: number, max: number): number {
  const n = Number(value);
  return Number.isFinite(n) ? Math.max(min, Math.min(max, Math.round(n))) : fallback;
}

function normalizeValues(value: unknown, fallback: CpuProfileValues): CpuProfileValues {
  const raw = value && typeof value === 'object' ? value as Partial<CpuProfileValues> : {};
  return {
    acFreq: clampNumber(raw.acFreq, fallback.acFreq, 0, 7200),
    dcFreq: clampNumber(raw.dcFreq, fallback.dcFreq, 0, 7200),
    acTurbo: typeof raw.acTurbo === 'boolean' ? raw.acTurbo : fallback.acTurbo,
    dcTurbo: typeof raw.dcTurbo === 'boolean' ? raw.dcTurbo : fallback.dcTurbo,
    acAggr: clampNumber(raw.acAggr, fallback.acAggr, 0, 100),
    dcAggr: clampNumber(raw.dcAggr, fallback.dcAggr, 0, 100),
  };
}

function normalizeConfig(value: unknown): CpuProfilesConfig {
  const fallback = defaultCpuProfilesConfig();
  const raw = value && typeof value === 'object' ? value as Partial<CpuProfilesConfig> : {};
  const profilesRaw = raw.profiles && typeof raw.profiles === 'object'
    ? raw.profiles as Partial<Record<CpuProfileId, CpuProfileValues>>
    : {};
  return {
    version: 1,
    active: isProfileId(raw.active) ? raw.active : fallback.active,
    profiles: {
      balanced: normalizeValues(profilesRaw.balanced, fallback.profiles.balanced),
      turbo: normalizeValues(profilesRaw.turbo, fallback.profiles.turbo),
      elite: normalizeValues(profilesRaw.elite, fallback.profiles.elite),
      extreme: normalizeValues(profilesRaw.extreme, fallback.profiles.extreme),
    },
  };
}

let cache: CpuProfilesConfig | null = null;
let writeQueue: Promise<void> = Promise.resolve();

export async function cpuProfilesFileExists(): Promise<boolean> {
  return fs.exists(CPU_PROFILES_FILE);
}

export async function loadCpuProfiles(): Promise<CpuProfilesConfig> {
  if (cache) return structuredClone(cache);
  try {
    const text = await fs.readTextFile(CPU_PROFILES_FILE, 65536);
    cache = normalizeConfig(JSON.parse(text));
  } catch {
    cache = defaultCpuProfilesConfig();
  }
  return structuredClone(cache);
}

export async function saveCpuProfiles(config: CpuProfilesConfig): Promise<void> {
  const nextConfig = normalizeConfig(config);
  const content = JSON.stringify(nextConfig, null, 2);
  const write = writeQueue.then(() => fs.writeTextFileAtomic(CPU_PROFILES_FILE, content));
  writeQueue = write.catch(() => {});
  await write;
  cache = nextConfig;
}

export function getCpuProfileMeta(id: CpuProfileId) {
  return CPU_PROFILE_META.find((item) => item.id === id) ?? CPU_PROFILE_META[0];
}
