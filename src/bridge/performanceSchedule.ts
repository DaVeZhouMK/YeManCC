import { fs } from './api';
import { detectGame } from './gamedetect';
import {
  RESET_PROFILES,
  detectPowerMode,
  runResetProfile,
  setTdp,
} from './yeman';
import {
  applyFloatSettings,
  disableFloat,
  enableFloat,
  getFloatInfo,
  notifyTdpMaxChanged,
  type FloatProfile,
  type TdpFloatStrategy,
} from './autofloat';

export type PowerSide = 'ac' | 'dc';
export type ScheduleMode = 'eco' | 'balanced' | 'medium' | 'performance' | 'elite' | 'extreme';
export type AutoScheduleMode = ScheduleMode;
export type CpuPreset = 'balanced' | 'turbo' | 'elite' | 'extreme';

export interface ScheduleProfile {
  cpuPreset: CpuPreset;
  tdpMax: number;
  fpsTarget: number;
  cpuTarget: FloatProfile;
  tdpStrategy: TdpFloatStrategy;
}

export interface PerformanceScheduleConfig {
  version: 2;
  configured: boolean;
  enabled: boolean;
  active: Record<PowerSide, ScheduleMode>;
  profiles: Record<PowerSide, Record<ScheduleMode, ScheduleProfile>>;
}

const CONFIG_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\performance-schedule.json';
const configListeners = new Set<(config: PerformanceScheduleConfig) => void>();
const AUTO_MODES: ScheduleMode[] = [
  'eco',
  'balanced',
  'medium',
  'performance',
  'elite',
  'extreme',
];
const MODE_ORDER = AUTO_MODES;
let gamepadModeCooldownUntil = 0;

// 配置重制的四档基线：性能调度统一由 FPS、TDP、CPU 浮动值三者组成。
// CPU 浮动值与 TDP 浮动幅度保持同档语义，避免选择档位后出现 CPU/TDP 两套方向。
export const DEFAULT_SCHEDULE_PROFILES: Record<ScheduleMode, ScheduleProfile> = {
  eco: {
    cpuPreset: 'balanced',
    tdpMax: 12,
    fpsTarget: 30,
    cpuTarget: 'aggressive',
    tdpStrategy: 'aggressive',
  },
  balanced: {
    cpuPreset: 'turbo',
    tdpMax: 16,
    fpsTarget: 45,
    cpuTarget: 'aggressive',
    tdpStrategy: 'aggressive',
  },
  medium: {
    cpuPreset: 'turbo',
    tdpMax: 23,
    fpsTarget: 60,
    cpuTarget: 'perf',
    tdpStrategy: 'large',
  },
  performance: {
    cpuPreset: 'elite',
    tdpMax: 35,
    fpsTarget: 90,
    cpuTarget: 'bal',
    tdpStrategy: 'medium',
  },
  elite: {
    cpuPreset: 'elite',
    tdpMax: 55,
    fpsTarget: 120,
    cpuTarget: 'eco',
    tdpStrategy: 'small',
  },
  extreme: {
    cpuPreset: 'extreme',
    tdpMax: 120,
    fpsTarget: 200,
    cpuTarget: 'none',
    tdpStrategy: 'none',
  },
};

function cloneProfiles(): Record<ScheduleMode, ScheduleProfile> {
  return Object.fromEntries(
    AUTO_MODES.map((mode) => [mode, { ...DEFAULT_SCHEDULE_PROFILES[mode] }]),
  ) as Record<ScheduleMode, ScheduleProfile>;
}

export function defaultPerformanceScheduleConfig(): PerformanceScheduleConfig {
  return {
    version: 2,
    configured: false,
    enabled: false,
    active: { ac: 'elite', dc: 'performance' },
    profiles: {
      ac: cloneProfiles(),
      dc: cloneProfiles(),
    },
  };
}

export function resetPerformanceScheduleProfiles(config: PerformanceScheduleConfig): PerformanceScheduleConfig {
  const defaults = defaultPerformanceScheduleConfig();
  return {
    ...config,
    active: { ...defaults.active },
    profiles: {
      ac: cloneProfiles(),
      dc: cloneProfiles(),
    },
  };
}

function isMode(value: unknown): value is ScheduleMode {
  return AUTO_MODES.includes(value as ScheduleMode);
}

function normalizeProfile(value: unknown, fallback: ScheduleProfile): ScheduleProfile {
  const raw = value && typeof value === 'object' ? value as Partial<ScheduleProfile> : {};
  const cpuPreset: CpuPreset =
    raw.cpuPreset === 'balanced' || raw.cpuPreset === 'turbo' ||
    raw.cpuPreset === 'elite' || raw.cpuPreset === 'extreme'
      ? raw.cpuPreset
      : fallback.cpuPreset;
  const cpuTarget: FloatProfile =
    raw.cpuTarget === 'none' || raw.cpuTarget === 'eco' || raw.cpuTarget === 'bal' ||
    raw.cpuTarget === 'perf' || raw.cpuTarget === 'aggressive'
      ? raw.cpuTarget
      : fallback.cpuTarget;
  const tdpStrategy: TdpFloatStrategy =
    raw.tdpStrategy === 'none' || raw.tdpStrategy === 'small' || raw.tdpStrategy === 'medium' ||
    raw.tdpStrategy === 'large' || raw.tdpStrategy === 'aggressive'
      ? raw.tdpStrategy
      : fallback.tdpStrategy;
  const tdpMax = Math.max(2, Math.min(200, Math.round(Number(raw.tdpMax) || fallback.tdpMax)));
  const fpsRaw = Math.round((Number.isFinite(Number(raw.fpsTarget)) ? Number(raw.fpsTarget) : fallback.fpsTarget) / 5) * 5;
  // 0 = 不锁帧（真实数值 0），其余限制在 30~300
  const fpsTarget = fpsRaw <= 0 ? 0 : Math.max(30, Math.min(300, fpsRaw));
  return { cpuPreset, tdpMax, fpsTarget, cpuTarget, tdpStrategy };
}

function normalizeConfig(value: unknown): PerformanceScheduleConfig {
  const fallback = defaultPerformanceScheduleConfig();
  const raw = value && typeof value === 'object' ? value as Partial<PerformanceScheduleConfig> : {};
  const activeRaw = raw.active && typeof raw.active === 'object' ? raw.active : {} as Record<PowerSide, ScheduleMode>;
  const profilesRaw = raw.profiles && typeof raw.profiles === 'object' ? raw.profiles : {} as PerformanceScheduleConfig['profiles'];
  const next = defaultPerformanceScheduleConfig();
  next.configured = raw.configured === true;
  next.enabled = raw.enabled === true || (raw.enabled === undefined && raw.configured === true);
  next.active.ac = isMode(activeRaw.ac) ? activeRaw.ac : fallback.active.ac;
  next.active.dc = isMode(activeRaw.dc) ? activeRaw.dc : fallback.active.dc;
  for (const side of ['ac', 'dc'] as const) {
    for (const mode of AUTO_MODES) {
      next.profiles[side][mode] = normalizeProfile(
        profilesRaw[side]?.[mode],
        fallback.profiles[side][mode],
      );
    }
  }
  return next;
}

let configCache: PerformanceScheduleConfig | null = null;
let scheduleWriteQueue: Promise<void> = Promise.resolve();

function queueScheduleWrite(config: PerformanceScheduleConfig): Promise<void> {
  const snapshot = normalizeConfig(config);
  const next = scheduleWriteQueue.then(async () => {
    await fs.writeTextFileAtomic(CONFIG_PATH, JSON.stringify(snapshot, null, 2));
    configCache = snapshot;
    emitPerformanceSchedule(snapshot);
  });
  scheduleWriteQueue = next.catch(() => {});
  return next;
}

function emitPerformanceSchedule(config: PerformanceScheduleConfig): void {
  const snapshot = structuredClone(config);
  for (const listener of [...configListeners]) listener(snapshot);
  // 非浏览器上下文（测试/后台进程）下没有 window，需守卫（2026-08-05 修复）
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent('performance-schedule:changed', { detail: snapshot }));
  }
}

export function onPerformanceScheduleChanged(
  listener: (config: PerformanceScheduleConfig) => void,
): () => void {
  configListeners.add(listener);
  if (configCache) listener(structuredClone(configCache));
  return () => configListeners.delete(listener);
}

export async function loadPerformanceSchedule(): Promise<PerformanceScheduleConfig> {
  if (configCache) return structuredClone(configCache);
  try {
    const raw = await fs.readTextFile(CONFIG_PATH, 65536);
    configCache = normalizeConfig(JSON.parse(raw));
  } catch {
    configCache = defaultPerformanceScheduleConfig();
  }
  return structuredClone(configCache);
}

export function snapshotPerformanceSchedule(
  config: PerformanceScheduleConfig,
): PerformanceScheduleConfig {
  return normalizeConfig(config);
}

export async function savePerformanceSchedule(config: PerformanceScheduleConfig): Promise<void> {
  await queueScheduleWrite(config);
}

function cpuProfilePath(preset: CpuPreset): string {
  const match = RESET_PROFILES.find((item) => item.id === preset);
  if (!match) throw new Error('未找到对应的 CPU 挡位');
  return match.path;
}

// 全局操作串行化：所有调度入口（切档/纯手动/恢复/电源事件）共用此队列，
// 避免并发写配置/硬件。带 generation 令牌，进入手动模式时使在途切档请求立刻短路。
let scheduleOpQueue: Promise<void> = Promise.resolve();
let scheduleOpGen = 0;

async function withScheduleOp<T>(enabled: boolean, fn: () => Promise<T>): Promise<T> {
  if (!enabled) scheduleOpGen++;
  const gen = scheduleOpGen;
  let resolveNext: (() => void) | null = null;
  const ticket = new Promise<void>((r) => { resolveNext = r; });
  const prev = scheduleOpQueue;
  // 队列链始终存活：前一个操作失败（fn 抛异常）时 catch 吞掉该次失败，
  // 保证后续操作不会被永久阻塞（修复：单次异常打挂整个调度系统 —— 2026-08-05）。
  // fn 本身的错误仍会向调用方抛出（见下方 return await fn()），仅队列不断链。
  scheduleOpQueue = scheduleOpQueue.catch(() => {}).then(() => ticket);
  await prev;
  try {
    if (gen !== scheduleOpGen) throw new SkipApplyError();
    return await fn();
  } finally {
    resolveNext!();
  }
}

export class SkipApplyError extends Error {
  constructor() { super('outdated'); }
}

export async function applyPerformanceSchedule(
  side: PowerSide,
  mode: ScheduleMode,
  config?: PerformanceScheduleConfig,
): Promise<boolean> {
  return withScheduleOp(true, async () => {
    return applyPerformanceScheduleUnsafe(side, mode, config);
  });
}

async function applyPerformanceScheduleUnsafe(
  side: PowerSide,
  mode: ScheduleMode,
  config?: PerformanceScheduleConfig,
): Promise<boolean> {
  const current = config ? normalizeConfig(config) : await loadPerformanceSchedule();
  current.configured = true;
  current.enabled = true;
  current.active[side] = mode;
  // 无论配置来自磁盘还是调用方，都把 configured/enabled/active 的变更落盘：
  // restore 路径此前只改内存不保存，导致「启动时自动恢复」静默失败
  // （下次启动仍是 configured=false 的旧态 —— 2026-08-05 修复）。
  await savePerformanceSchedule(current);

  const actualSide = await detectPowerMode();
  if (actualSide !== side) return false;

  const profile = current.profiles[side][mode];
  // CPU 控制轴互斥：cpuTarget!=='none' 时由「CPU 浮动值」接管 autofloat 的 CPU 主频降低策略；
  // cpuTarget==='none' 时由「CPU 挡位」(cpuPreset) 直接设置硬件 CPU 档位。
  // 两路不得叠加：cpuTarget!=='none' 不调用 runResetProfile，避免 preset 基线被压制层覆盖。
  const cpuTarget: FloatProfile = profile.cpuTarget;

  if (getFloatInfo().enabled) {
    // 浮动调度已运行：保留守护、daemon、HWiNFO 与 RTSS 原始快照，只增量更新当前目标。
    // 热切档也需重新下发 CPU：cpuTarget==='none' 时重设 cpuPreset；否则由 autofloat 用 CPU 浮动值接管内核频率。
    // 写硬件前二次校验电源侧：detectPowerMode 与下发之间若插拔电源，直接中止，
    // 避免 AC 配置被写到 DC（或反之）造成电池下跑极端档（2026-08-05 修复 TOCTOU）。
    if ((await detectPowerMode()) !== side) return false;
    if (cpuTarget === 'none') {
      await runResetProfile(cpuProfilePath(profile.cpuPreset));
    }
    await setTdp(side, profile.tdpMax, { apply: false, save: true });
    await notifyTdpMaxChanged(profile.tdpMax);
    await applyFloatSettings(
      profile.fpsTarget,
      cpuTarget,
      profile.tdpStrategy,
    );
    return true;
  }

  // 首次开启走完整初始化。
  if ((await detectPowerMode()) !== side) return false;
  if (cpuTarget === 'none') {
    await runResetProfile(cpuProfilePath(profile.cpuPreset));
  }
  await setTdp(side, profile.tdpMax, { apply: true, save: true });
  await enableFloat(
    profile.fpsTarget,
    cpuTarget,
    profile.tdpStrategy,
  );
  return true;
}

export type PerformanceScheduleOwnership = 'none' | 'manual' | 'auto';

export async function disablePerformanceSchedule(config?: PerformanceScheduleConfig): Promise<void> {
  return withScheduleOp(false, async () => {
    const current = config ? normalizeConfig(config) : await loadPerformanceSchedule();
    current.configured = true;
    current.enabled = false;
    // 先关闭现有浮动并恢复其原始 CPU/TDP/RTSS，再落盘纯手动状态。
    if (getFloatInfo().enabled) await disableFloat();
    await savePerformanceSchedule(current);
  });
}

export async function getPerformanceScheduleOwnership(): Promise<PerformanceScheduleOwnership> {
  const config = await loadPerformanceSchedule();
  if (!config.configured) return 'none';
  return config.enabled ? 'auto' : 'manual';
}

export async function restorePerformanceScheduleIfConfigured(): Promise<PerformanceScheduleOwnership> {
  return withScheduleOp(true, async () => {
    const config = await loadPerformanceSchedule();
    if (!config.configured) return 'none';
    if (!config.enabled) {
      if (getFloatInfo().enabled) await disableFloat();
      return 'manual';
    }

    // 系统级恢复必须独立于性能调度页是否激活：启动、唤醒、AC/DC 切换时，
    // 当前游戏存在自定义档位则优先恢复自定义；否则才应用普通自动档位。
    const game = await detectGame(true).catch(() => null);
    if (game) {
      const custom = await loadGameCustomConfig();
      const fromPath = game.path ? game.path.split(/[\\/]/).pop() : '';
      const key = (fromPath || game.name || '').toLowerCase().trim();
      const entry = key ? custom.entries[key.endsWith('.exe') ? key : `${key}.exe`] : undefined;
      if (entry) {
        const side = await detectPowerMode();
        await applyGameCustomProfilesUnsafe(side, entry.ac, entry.dc);
        return 'auto';
      }
    }

    const side = await detectPowerMode();
    const mode = config.active[side];
    await applyPerformanceScheduleUnsafe(side, mode, config);
    return 'auto';
  });
}

export async function applyPerformanceScheduleForCurrentPower(): Promise<boolean> {
  return withScheduleOp(true, async () => {
    const config = await loadPerformanceSchedule();
    const side = await detectPowerMode();
    return applyPerformanceScheduleUnsafe(side, config.active[side], config);
  });
}

// 手柄 Start+上下：自动模式在当前 AC/DC 侧循环已编辑的标准档位。
// 这里复用正常档位应用链路，确保切换会真正下发 CPU/TDP/浮动设置并落盘，
// 不走“重制预设”或只改界面状态的旁路。
export async function cyclePerformanceScheduleMode(direction: 1 | -1): Promise<{
  applied: boolean;
  side: PowerSide;
  mode?: ScheduleMode;
  blocked?: 'disabled' | 'custom' | 'cooldown' | 'boundary';
}> {
  return withScheduleOp(true, async () => {
    const config = await loadPerformanceSchedule();
    const side = await detectPowerMode();
    if (!config.configured || !config.enabled) return { applied: false, side, blocked: 'disabled' };
    const game = await detectGame(true).catch(() => null);
    if (game) {
      const custom = await loadGameCustomConfig();
      const fromPath = game.path ? game.path.split(/[\\/]/).pop() : '';
      const key = (fromPath || game.name || '').toLowerCase().trim();
      const entry = key ? custom.entries[key.endsWith('.exe') ? key : `${key}.exe`] : undefined;
      if (entry) return { applied: false, side, blocked: 'custom' };
    }
    if (Date.now() < gamepadModeCooldownUntil) {
      return { applied: false, side, mode: config.active[side], blocked: 'cooldown' };
    }
    const current = MODE_ORDER.indexOf(config.active[side]);
    const nextIndex = Math.max(0, Math.min(MODE_ORDER.length - 1, current + direction));
    if (nextIndex === current) {
      return { applied: false, side, mode: config.active[side], blocked: 'boundary' };
    }
    const next = MODE_ORDER[nextIndex];
    const applied = await applyPerformanceScheduleUnsafe(side, next, config);
    if (applied) gamepadModeCooldownUntil = Date.now() + 3000;
    return { applied, side, mode: next };
  });
}

// ──────────────────────────────────────────────────────────────────────────
// 自定义游戏模式：按识别到的游戏 exe 名存储一套专属 AC/DC 性能档位（覆盖自动模式）。
// ──────────────────────────────────────────────────────────────────────────

export interface GameCustomProfile {
  displayName: string;
  ac: ScheduleProfile;
  dc: ScheduleProfile;
}

export interface GameCustomConfig {
  version: 1;
  entries: Record<string, GameCustomProfile>;
}

const GAME_CUSTOM_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\game-custom.json';

function defaultGameCustomConfig(): GameCustomConfig {
  return { version: 1, entries: {} };
}

function normalizeCustomProfile(raw: unknown, fallback: ScheduleProfile): GameCustomProfile {
  const r = raw && typeof raw === 'object' ? (raw as Record<string, unknown>) : {};
  return {
    displayName: typeof r.displayName === 'string' ? r.displayName : '',
    ac: normalizeProfile(r.ac, fallback),
    dc: normalizeProfile(r.dc, fallback),
  };
}

function normalizeGameCustom(value: unknown): GameCustomConfig {
  const fallback = defaultPerformanceScheduleConfig().profiles.ac.performance;
  const raw = value && typeof value === 'object' ? (value as Record<string, unknown>) : {};
  const entriesRaw = raw.entries && typeof raw.entries === 'object' ? (raw.entries as Record<string, unknown>) : {};
  const entries: Record<string, GameCustomProfile> = {};
  for (const [key, val] of Object.entries(entriesRaw)) {
    if (!key) continue;
    const k = key.toLowerCase();
    // 仅大小写不同的重复键：保留第一个（先出现的配置优先），避免静默覆盖丢数据
    // （2026-08-05 修复）。
    if (!(k in entries)) entries[k] = normalizeCustomProfile(val, fallback);
  }
  return { version: 1, entries };
}

let gameCustomCache: GameCustomConfig | null = null;

export function getGameCustomConfig(): GameCustomConfig {
  return gameCustomCache ? structuredClone(gameCustomCache) : defaultGameCustomConfig();
}

export async function loadGameCustomConfig(): Promise<GameCustomConfig> {
  if (gameCustomCache) return structuredClone(gameCustomCache);
  try {
    const raw = await fs.readTextFile(GAME_CUSTOM_PATH, 131072);
    gameCustomCache = normalizeGameCustom(JSON.parse(raw));
  } catch {
    gameCustomCache = defaultGameCustomConfig();
  }
  return structuredClone(gameCustomCache);
}

let gameCustomWriteQueue: Promise<void> = Promise.resolve();

export async function saveGameCustomConfig(config: GameCustomConfig): Promise<void> {
  const snapshot = normalizeGameCustom(config);
  // 队列吞错保活（一次 I/O 失败不再卡死后续写入）；但 run 本身仍向调用方抛出写入失败，
  // 让 UI 能感知「保存失败」而非静默丢失（修复：首败后所有保存永久失效 —— 2026-08-05）。
  const run = gameCustomWriteQueue.then(async () => {
    await fs.writeTextFileAtomic(GAME_CUSTOM_PATH, JSON.stringify(snapshot, null, 2));
    gameCustomCache = snapshot;
  });
  gameCustomWriteQueue = run.catch(() => {});
  await run;
}

export function getCustomProfileFor(
  config: GameCustomConfig,
  exeName: string,
  side: PowerSide,
): ScheduleProfile | null {
  const entry = config.entries[exeName.toLowerCase()];
  return entry ? entry[side] : null;
}

async function applyGameCustomProfilesUnsafe(
  side: PowerSide,
  acProfile: ScheduleProfile,
  dcProfile: ScheduleProfile,
): Promise<boolean> {
  const profile = side === 'ac' ? acProfile : dcProfile;
  const cpuTarget: FloatProfile = profile.cpuTarget;

  // 所有耗时硬件阶段前后都复核电源侧，避免插拔窗口中把旧 AC/DC 档位继续写入。
  if ((await detectPowerMode()) !== side) return false;
  if (getFloatInfo().enabled) {
    if (cpuTarget === 'none') {
      await runResetProfile(cpuProfilePath(profile.cpuPreset));
      if ((await detectPowerMode()) !== side) return false;
    }
    await setTdp(side, profile.tdpMax, { apply: false, save: true });
    if ((await detectPowerMode()) !== side) return false;
    await notifyTdpMaxChanged(profile.tdpMax);
    await applyFloatSettings(profile.fpsTarget, cpuTarget, profile.tdpStrategy);
    return true;
  }
  if (cpuTarget === 'none') {
    await runResetProfile(cpuProfilePath(profile.cpuPreset));
    if ((await detectPowerMode()) !== side) return false;
  }
  await setTdp(side, profile.tdpMax, { apply: true, save: true });
  if ((await detectPowerMode()) !== side) return false;
  await enableFloat(profile.fpsTarget, cpuTarget, profile.tdpStrategy);
  return true;
}

// 应用自定义档位：绕开自动档位选择，直接把该游戏的 AC/DC 配置下发到当前电源侧。
export async function applyGameCustomProfiles(
  acProfile: ScheduleProfile,
  dcProfile: ScheduleProfile,
): Promise<boolean> {
  const schedule = await loadPerformanceSchedule();
  if (!schedule.enabled) return false;
  return withScheduleOp(true, async () => {
    const side = await detectPowerMode();
    return applyGameCustomProfilesUnsafe(side, acProfile, dcProfile);
  });
}
