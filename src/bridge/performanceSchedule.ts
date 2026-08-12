import { fs } from './api';
import { readSettingsSection, replaceSettingsSection, saveSettingsSection } from './settingsRepository';
import { detectGame } from './gamedetect';
import {
  PW,
  RESET_PROFILES,
  detectPowerMode,
  ensureRememberedYemanSchemeActive,
  applyPowerParams,
  readPowerParams,
  setCoreModeAc,
  setCoreModeDc,
  detectCoreArchitecture,
  rebuildYemanScheme,
  runResetProfile,
  setActiveScheme,
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
export type CoreMode = 'big' | 'only-big' | 'only-small';

export const CORE_MODE_OPTIONS = [
  { value: 'big' as CoreMode, label: '大核为主(推荐)', sub: 'Windows 混合架构' },
  { value: 'only-big' as CoreMode, label: '仅大核', sub: 'Windows 混合架构' },
  { value: 'only-small' as CoreMode, label: '仅小核', sub: 'Windows 混合架构' },
];

export interface ScheduleProfile {
  cpuPreset: CpuPreset;
  coreMode: CoreMode;
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

export type PerformanceScheduleWarningCode = 'tdp-unsupported' | 'tdp-apply-failed';

export interface PerformanceScheduleWarning {
  code: PerformanceScheduleWarningCode;
  side: PowerSide;
  watts: number;
  message: string;
  detail: string;
}

const CONFIG_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\performance-schedule.json';
const configListeners = new Set<(config: PerformanceScheduleConfig) => void>();
const warningListeners = new Set<(warning: PerformanceScheduleWarning) => void>();
const emittedWarningKeys = new Set<string>();
let lastWarning: PerformanceScheduleWarning | null = null;
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

function tdpErrorText(error: unknown): string {
  if (error instanceof Error) return error.message;
  return String(error ?? '未知 TDP 错误');
}

function compactTdpDetail(raw: string): string {
  const lines = raw
    .replace(/\r/g, '')
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean);
  return lines.slice(-5).join(' | ').slice(0, 600);
}

export function describePerformanceScheduleTdpError(
  error: unknown,
  side: PowerSide,
  watts: number,
): PerformanceScheduleWarning {
  const raw = tdpErrorText(error);
  const detail = compactTdpDetail(raw);
  const desktop17 = /Desktop-17h|Zen\s*[~～-]\s*Zen2|Castle\s*Peak|Threadripper/i.test(raw);
  const unsupported = /transport\s*=\s*unsupported|tdp_supported\s*=\s*false|TDP capability[^\n]*拒绝|capability\/transport[^\n]*拒绝|尚无已验证|未实现/i.test(raw);

  if (desktop17 && (unsupported || /\brc\s*=\s*6\b/i.test(raw))) {
    return {
      code: 'tdp-unsupported',
      side,
      watts,
      message: '当前 AMD Desktop-17h（Zen～Zen2/Threadripper）尚无已验证的 TDP 协议，已跳过 TDP；不会阻断 CPU 调度及自动/手动模式切换。',
      detail,
    };
  }

  return {
    code: 'tdp-apply-failed',
    side,
    watts,
    message: `TDP ${watts}W 下发失败，已跳过；CPU 调度及自动/手动模式切换会继续执行。${detail ? ` 详情：${detail}` : ''}`,
    detail,
  };
}

function publishPerformanceScheduleWarning(warning: PerformanceScheduleWarning): void {
  lastWarning = warning;
  const platformKey = warning.code === 'tdp-unsupported'
    ? warning.code
    : `${warning.code}:${warning.detail}`;
  if (emittedWarningKeys.has(platformKey)) return;
  emittedWarningKeys.add(platformKey);
  console.warn('[performance schedule]', warning.message, warning.detail);
  for (const listener of [...warningListeners]) listener({ ...warning });
}

export async function runOptionalPerformanceScheduleTdp(
  side: PowerSide,
  watts: number,
  apply: () => Promise<void>,
): Promise<PerformanceScheduleWarning | null> {
  try {
    await apply();
    return null;
  } catch (error) {
    if (error instanceof SkipApplyError) throw error;
    const warning = describePerformanceScheduleTdpError(error, side, watts);
    publishPerformanceScheduleWarning(warning);
    return warning;
  }
}

export function getPerformanceScheduleWarning(): PerformanceScheduleWarning | null {
  return lastWarning ? { ...lastWarning } : null;
}

export function onPerformanceScheduleWarning(
  listener: (warning: PerformanceScheduleWarning) => void,
): () => void {
  warningListeners.add(listener);
  if (lastWarning) listener({ ...lastWarning });
  return () => warningListeners.delete(listener);
}

// 配置重制的六档基线：性能调度统一由 FPS、TDP、CPU 浮动值三者组成。
// CPU 浮动值与 TDP 浮动幅度保持同档语义，避免选择档位后出现 CPU/TDP 两套方向。
export const DEFAULT_SCHEDULE_PROFILES: Record<ScheduleMode, ScheduleProfile> = {
  eco: {
    cpuPreset: 'balanced',
    coreMode: 'big',
    tdpMax: 12,
    fpsTarget: 30,
    cpuTarget: 'aggressive',
    tdpStrategy: 'aggressive',
  },
  balanced: {
    cpuPreset: 'turbo',
    coreMode: 'big',
    tdpMax: 16,
    fpsTarget: 45,
    cpuTarget: 'aggressive',
    tdpStrategy: 'aggressive',
  },
  medium: {
    cpuPreset: 'turbo',
    coreMode: 'big',
    tdpMax: 23,
    fpsTarget: 60,
    cpuTarget: 'perf',
    tdpStrategy: 'aggressive',
  },
  performance: {
    cpuPreset: 'elite',
    coreMode: 'big',
    tdpMax: 30,
    fpsTarget: 60,
    cpuTarget: 'perf',
    tdpStrategy: 'large',
  },
  elite: {
    cpuPreset: 'elite',
    coreMode: 'big',
    tdpMax: 55,
    fpsTarget: 60,
    cpuTarget: 'eco',
    tdpStrategy: 'medium',
  },
  extreme: {
    cpuPreset: 'extreme',
    coreMode: 'big',
    tdpMax: 120,
    fpsTarget: 120,
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
    active: { ac: 'elite', dc: 'medium' },
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
  const coreMode: CoreMode =
    raw.coreMode === 'big' || raw.coreMode === 'only-big' || raw.coreMode === 'only-small'
      ? raw.coreMode
      : fallback.coreMode;
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
  return { cpuPreset, coreMode, tdpMax, fpsTarget, cpuTarget, tdpStrategy };
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
    await saveSettingsSection('performanceSchedule', snapshot as any);
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
    configCache = normalizeConfig(await readSettingsSection('performanceSchedule'));
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

async function applyCpuPresetBaseline(preset: CpuPreset): Promise<void> {
  assertScheduleOpCurrent();
  await runResetProfile(cpuProfilePath(preset));
  // CPU 浮动旧版本曾错误写入最大处理器状态。基线应用时只恢复这个
  // 全局安全上限，动态浮动本身不再触碰该参数。
  const params = await readPowerParams();
  assertScheduleOpCurrent();
  if (params) await applyPowerParams(params);
  assertScheduleOpCurrent();
  await setActiveScheme();
}

async function applyCoreModeIfHybrid(side: PowerSide, mode: CoreMode): Promise<boolean> {
  assertScheduleOpCurrent();
  const architecture = await detectCoreArchitecture();
  // Unknown/uniform CPUs are read-only no-op: never write hybrid-only values
  // merely because a powercfg subgroup happens to exist.
  if (!architecture?.heterogeneous) return false;
  assertScheduleOpCurrent();
  if (side === 'ac') await setCoreModeAc(mode);
  else await setCoreModeDc(mode);
  return true;
}

// 全局操作串行化：所有调度入口（切档/纯手动/恢复/电源事件）共用此队列，
// 避免并发写配置/硬件。带 generation 令牌，进入手动模式时使在途切档请求立刻短路。
let scheduleOpQueue: Promise<void> = Promise.resolve();
let scheduleOpGen = 0;
let runningScheduleOpGen = 0;

async function withScheduleOp<T>(enabled: boolean, fn: () => Promise<T>): Promise<T> {
  // Every new scheduling request supersedes work that has not reached its
  // next commit point. `enabled` remains in the signature for call-site
  // compatibility; manual mode still uses the same invalidation path.
  void enabled;
  const gen = ++scheduleOpGen;
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
    runningScheduleOpGen = gen;
    return await fn();
  } finally {
    if (runningScheduleOpGen === gen) runningScheduleOpGen = 0;
    resolveNext!();
  }
}

function assertScheduleOpCurrent(): void {
  if (!runningScheduleOpGen || runningScheduleOpGen !== scheduleOpGen) {
    throw new SkipApplyError();
  }
}

export class SkipApplyError extends Error {
  constructor() { super('outdated'); }
}

/* legacy helper retained for compatibility history
async function ensureYemanSchemeActive(): Promise<void> {
  return ensureRememberedYemanSchemeActive();
  // legacy implementation retained below only as source context
  await ensureYemanScheme();
  if (await getActiveScheme() === PW.YEMAN) return;
  await setActiveScheme(PW.YEMAN);
  if (await getActiveScheme() !== PW.YEMAN) {
    throw new Error('切换野蛮系统电源后仍未生效');
  }
}

*/
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
  // Automatic scheduling owns YeMan power scheme. Restore that ownership
  // before writing CPU/TDP values so they always land on the active scheme.
  assertScheduleOpCurrent();
  await ensureRememberedYemanSchemeActive();
  current.configured = true;
  current.enabled = true;
  assertScheduleOpCurrent();
  current.active[side] = mode;
  const actualSide = await detectPowerMode();
  assertScheduleOpCurrent();
  if (actualSide !== side) {
    assertScheduleOpCurrent();
    await savePerformanceSchedule(current);
    return false;
  }

  const profile = current.profiles[side][mode];
  // CPU 控制轴互斥：cpuTarget!=='none' 时由「CPU 浮动值」接管 autofloat 的 CPU 主频降低策略；
  // cpuTarget==='none' 时由「CPU 挡位」(cpuPreset) 直接设置硬件 CPU 档位。
  // 两路不得叠加：cpuTarget!=='none' 不调用 runResetProfile，避免 preset 基线被压制层覆盖。
  const cpuTarget: FloatProfile = profile.cpuTarget;

  assertScheduleOpCurrent();
  if ((await detectPowerMode()) !== side) return false;

  if (getFloatInfo().enabled) {
    // 浮动调度已运行：保留守护、daemon、HWiNFO 与 RTSS 原始快照，只增量更新当前目标。
    // 热切档统一先下发 TDP，再重新下发 CPU：cpuTarget==='none' 时重设 cpuPreset；
    // 否则由 autofloat 用 CPU 浮动值接管内核频率。
    // 写硬件前二次校验电源侧：detectPowerMode 与下发之间若插拔电源，直接中止，
    // 避免 AC 配置被写到 DC（或反之）造成电池下跑极端档（2026-08-05 修复 TOCTOU）。
    if ((await detectPowerMode()) !== side) return false;
    assertScheduleOpCurrent();
    await runOptionalPerformanceScheduleTdp(side, profile.tdpMax, () =>
      setTdp(side, profile.tdpMax, { apply: false, save: true }));
    assertScheduleOpCurrent();
    await notifyTdpMaxChanged(profile.tdpMax);
    assertScheduleOpCurrent();
    if (cpuTarget === 'none') await applyCpuPresetBaseline(profile.cpuPreset);
    assertScheduleOpCurrent();
    await applyFloatSettings(profile.fpsTarget, cpuTarget, profile.tdpStrategy);
    assertScheduleOpCurrent();
    await applyCoreModeIfHybrid(side, profile.coreMode);
    assertScheduleOpCurrent();
    await savePerformanceSchedule(current);
    return true;
  }

  // 首次开启走完整初始化。
  if ((await detectPowerMode()) !== side) return false;
  // 新预设统一按 TDP 优先：先写入并应用当前侧 TDP，再套用 CPU 浮动/CPU 挡位。
  assertScheduleOpCurrent();
  await runOptionalPerformanceScheduleTdp(side, profile.tdpMax, () =>
    setTdp(side, profile.tdpMax, { apply: true, save: true }));
  assertScheduleOpCurrent();
  if (cpuTarget === 'none') await applyCpuPresetBaseline(profile.cpuPreset);
  assertScheduleOpCurrent();
  await enableFloat(
    profile.fpsTarget,
    cpuTarget,
    profile.tdpStrategy,
  );
  assertScheduleOpCurrent();
  await applyCoreModeIfHybrid(side, profile.coreMode);
  assertScheduleOpCurrent();
  await savePerformanceSchedule(current);
  return true;
}

export type PerformanceScheduleOwnership = 'none' | 'manual' | 'auto';

export async function disablePerformanceSchedule(config?: PerformanceScheduleConfig): Promise<void> {
  return withScheduleOp(false, async () => {
    const current = config ? normalizeConfig(config) : await loadPerformanceSchedule();
    current.configured = true;
    current.enabled = false;
    // 先停止现有 CPU 浮动控制；TDP/RTSS/监控资源保持交给后续 CPU 挡位逻辑接管。
    assertScheduleOpCurrent();
    if (getFloatInfo().enabled) await disableFloat();
    assertScheduleOpCurrent();
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
      assertScheduleOpCurrent();
      if (getFloatInfo().enabled) await disableFloat();
      assertScheduleOpCurrent();
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

// Startup needs one delayed hybrid-core refresh because a boot task/OEM policy
// can rewrite these three power-plan values after the first normal restore.
// This path is automatic-only and does not reapply TDP, CPU float, FPS, or
// manual settings.
export async function refreshPerformanceScheduleCoreMode(): Promise<boolean> {
  return withScheduleOp(true, async () => {
    assertScheduleOpCurrent();
    const config = await loadPerformanceSchedule();
    if (!config.configured || !config.enabled) return false;
    const game = await detectGame(true).catch(() => null);
    if (game) {
      const custom = await loadGameCustomConfig();
      const fromPath = game.path ? game.path.split(/[\\/]/).pop() : '';
      const key = (fromPath || game.name || '').toLowerCase().trim();
      const entry = key ? custom.entries[key.endsWith('.exe') ? key : `${key}.exe`] : undefined;
      if (entry) {
        const side = await detectPowerMode();
        return applyCoreModeIfHybrid(side, entry[side].coreMode);
      }
    }
    const side = await detectPowerMode();
    assertScheduleOpCurrent();
    return applyCoreModeIfHybrid(side, config.profiles[side][config.active[side]].coreMode);
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
const GAME_CUSTOM_BACKUP_PATH = `${GAME_CUSTOM_PATH}.bak`;
const GAME_CUSTOM_MAX_BYTES = 4 * 1024 * 1024;

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

async function readStandaloneGameCustom(path: string): Promise<GameCustomConfig | null> {
  try {
    const raw = await fs.readTextFile(path, GAME_CUSTOM_MAX_BYTES);
    return normalizeGameCustom(JSON.parse(raw));
  } catch {
    return null;
  }
}

async function writeStandaloneGameCustom(config: GameCustomConfig): Promise<void> {
  await fs.writeTextFileAtomic(GAME_CUSTOM_PATH, JSON.stringify(config, null, 2));
}

export function getGameCustomConfig(): GameCustomConfig {
  return gameCustomCache ? structuredClone(gameCustomCache) : defaultGameCustomConfig();
}

export async function loadGameCustomConfig(): Promise<GameCustomConfig> {
  if (gameCustomCache) return structuredClone(gameCustomCache);

  const standaloneExists = await fs.exists(GAME_CUSTOM_PATH).catch(() => false);
  if (standaloneExists) {
    const standalone = await readStandaloneGameCustom(GAME_CUSTOM_PATH);
    if (standalone) {
      gameCustomCache = standalone;
      return structuredClone(gameCustomCache);
    }

    // A malformed main file must not fall back to the legacy unified section:
    // doing so would resurrect entries the user deliberately removed. Prefer
    // the last known standalone backup, otherwise expose an empty config while
    // retaining the damaged file for diagnosis.
    const backup = await readStandaloneGameCustom(GAME_CUSTOM_BACKUP_PATH);
    gameCustomCache = backup ?? defaultGameCustomConfig();
    return structuredClone(gameCustomCache);
  }

  // One-time migration only. Once the standalone document exists, it is the
  // sole runtime source and the old unified section can never revive entries.
  let migrated: GameCustomConfig;
  try {
    migrated = normalizeGameCustom(await readSettingsSection('gameCustom'));
  } catch {
    migrated = defaultGameCustomConfig();
  }
  try {
    await writeStandaloneGameCustom(migrated);
    await replaceSettingsSection('gameCustom', defaultGameCustomConfig() as any);
  } catch {
    // Keep the migrated snapshot usable for this session. The next save or
    // startup will retry persistence instead of making the UI unusable.
  }
  gameCustomCache = migrated;
  return structuredClone(gameCustomCache);
}

let gameCustomWriteQueue: Promise<void> = Promise.resolve();

export async function saveGameCustomConfig(config: GameCustomConfig): Promise<void> {
  const snapshot = normalizeGameCustom(config);
  // 队列吞错保活（一次 I/O 失败不再卡死后续写入）；但 run 本身仍向调用方抛出写入失败，
  // 让 UI 能感知「保存失败」而非静默丢失（修复：首败后所有保存永久失效 —— 2026-08-05）。
  const run = gameCustomWriteQueue.then(async () => {
    // Preserve the previous standalone document before replacing it. This is
    // deliberately independent from yeman-settings.json so deleting an entry
    // cannot be undone by a later unified-settings merge.
    if (await fs.exists(GAME_CUSTOM_PATH).catch(() => false)) {
      const previous = await fs.readTextFile(GAME_CUSTOM_PATH, GAME_CUSTOM_MAX_BYTES);
      await fs.writeTextFileAtomic(GAME_CUSTOM_BACKUP_PATH, previous);
    }
    await writeStandaloneGameCustom(snapshot);
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

  assertScheduleOpCurrent();
  if ((await detectPowerMode()) !== side) return false;

  // 所有耗时硬件阶段前后都复核电源侧，避免插拔窗口中把旧 AC/DC 档位继续写入。
  if ((await detectPowerMode()) !== side) return false;
  if (getFloatInfo().enabled) {
    if ((await detectPowerMode()) !== side) return false;
    assertScheduleOpCurrent();
    await runOptionalPerformanceScheduleTdp(side, profile.tdpMax, () =>
      setTdp(side, profile.tdpMax, { apply: false, save: true }));
    assertScheduleOpCurrent();
    if ((await detectPowerMode()) !== side) return false;
    await notifyTdpMaxChanged(profile.tdpMax);
    assertScheduleOpCurrent();
    if (cpuTarget === 'none') await applyCpuPresetBaseline(profile.cpuPreset);
    assertScheduleOpCurrent();
    await applyFloatSettings(profile.fpsTarget, cpuTarget, profile.tdpStrategy);
    assertScheduleOpCurrent();
    await applyCoreModeIfHybrid(side, profile.coreMode);
    assertScheduleOpCurrent();
    return true;
  }
  if ((await detectPowerMode()) !== side) return false;
  assertScheduleOpCurrent();
  await runOptionalPerformanceScheduleTdp(side, profile.tdpMax, () =>
    setTdp(side, profile.tdpMax, { apply: true, save: true }));
  assertScheduleOpCurrent();
  if (cpuTarget === 'none') await applyCpuPresetBaseline(profile.cpuPreset);
  if ((await detectPowerMode()) !== side) return false;
  assertScheduleOpCurrent();
  await enableFloat(profile.fpsTarget, cpuTarget, profile.tdpStrategy);
  assertScheduleOpCurrent();
  await applyCoreModeIfHybrid(side, profile.coreMode);
  assertScheduleOpCurrent();
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
    assertScheduleOpCurrent();
    await ensureRememberedYemanSchemeActive();
    assertScheduleOpCurrent();
    const side = await detectPowerMode();
    return applyGameCustomProfilesUnsafe(side, acProfile, dcProfile);
  });
}
