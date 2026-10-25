import { isControllerPersona, migrateControllerPersona } from './controllerPersona';
import { powerSourceMode } from './powerSource';
import { defaultContinuousBank, normalizeContinuousControls, normalizeContinuousBank, patchContinuousBank, automaticCpuControls, manualCpuMinimum, continuousCoreAt, stepContinuousCore, toPowerCoreMode, type ContinuousCoreState,
  type ContinuousControls, type ContinuousSideControls, type ContinuousSidePatch, type ContinuousPerformanceIdentity, type ContinuousPerformanceSnapshot } from './performanceControls';
import { detectGameCorePolicy, applyGameCorePolicy } from './gameCorePolicy';
import { getPolicyGame, gamePolicyKey } from './gamePolicyTarget';
import { fs, powerLifecycle } from './api';
import { ensureGlobalFrameRates, applyIndependentFrameRates, publishFrameRatesChanged, frameRatePair, dedicatedFrameRatePair, dedicatedFrameRateRecord, frameRateSetting, type FrameRatePair } from './frameRateLimits';
import { getSettingsGeneration, assertSettingsGeneration, updateSettings, mergeSettings, readSettingsSection, replaceSettingsSection, saveSettingsSection } from './settingsRepository';
import { detectPolicyGame, isPolicyTargetCurrent } from './gamePolicyTarget';
import { validateGameTarget, isCurrentGameTarget } from './gamedetect';
import {
  detectPowerMode, detectPerformanceTemplate,
  detectPowerModeReliable, applyManualPowerParams, getTdpHardwareMinimum, getTdpHardwareMaximum, getTdpExecutionCapability, getActiveScheme, PW, setActiveScheme,
  ensureRememberedYemanSchemeActive,
  setCoreModeAc,
  setCoreModeDc,
  detectCoreArchitecture,
  rebuildYemanScheme,
  setTdp,
  setRtssLimit,
  readRtssLimit,
  switchScheme,
} from './yeman';
import { loadCpuProfiles } from './cpuProfiles';
import {
  enableContinuousFloat,
  disableFloat,
  getFloatInfo,
  type FloatProfile,
  type TdpFloatStrategy,
  cancelPendingRtssSync,
  setFloatRtssLinked,
  updateContinuousFloatCoreRules,
  holdContinuousFloatAfterCoreChange,
} from './autofloat';

export type PowerSide = 'ac' | 'dc';
export type ScheduleMode = 'eco' | 'balanced' | 'medium' | 'performance' | 'elite' | 'extreme';
export type AutoScheduleMode = ScheduleMode;
export type CpuPreset = 'balanced' | 'turbo' | 'elite' | 'extreme';
export type CoreMode = 'big' | 'only-big' | 'small' | 'only-small';

export const CORE_MODE_OPTIONS = [
  { value: 'big' as CoreMode, label: '大核为主(推荐)', sub: 'Windows 混合架构' },
  { value: 'only-big' as CoreMode, label: '仅大核', sub: 'Windows 混合架构' },
  { value: 'only-small' as CoreMode, label: '仅小核', sub: 'Windows 混合架构' },
];

export interface ScheduleProfile {
  controls?: ContinuousSideControls;
  cpuPreset: CpuPreset;
  coreMode: CoreMode;
  tdpMax: number;
  /** Legacy migration scalar only: never bound/applied by a performance combination. */
  fpsTarget: number;
  cpuTarget: FloatProfile;
  tdpStrategy: TdpFloatStrategy;
}

export interface PerformanceScheduleConfig {
  controls?: ContinuousControls;
  version: 2;
  configured: boolean;
  enabled: boolean;
  /** Explicit global OFF includes dedicated, AC/DC, boot and wake recovery. Presets remain stored. */
  suspended?: boolean;
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

export interface PerformanceScheduleTarget {
  /** Automatic owners validate the shared confirmed lease, not transient foreground. */
  policyTarget?: boolean;
  pid: number;
  processCreated: string;
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
    raw.coreMode === 'big' || raw.coreMode === 'only-big' || raw.coreMode === 'small' || raw.coreMode === 'only-small'
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
  const tdpMax = Math.max(2, Math.min(150, Math.round(Number(raw.tdpMax) || fallback.tdpMax)));
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
  if (raw.controls) next.controls = normalizeContinuousControls(raw.controls);
  next.suspended = raw.suspended === true;
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
  const generation = getSettingsGeneration();
  const next = scheduleWriteQueue.then(async () => {
    await ensureGlobalFrameRates(); // freeze legacy FPS before a combination/active mode changes
    await saveSettingsSection('performanceSchedule', snapshot as any, generation);
    configCache = snapshot;
    emitPerformanceSchedule(snapshot);
  });
  scheduleWriteQueue = next.catch(() => {});
  return next;
}

function emitPerformanceSchedule(config: PerformanceScheduleConfig): void {
  const snapshot = structuredClone(config);
  for (const listener of [...configListeners]) {
    try { listener(structuredClone(snapshot)); } catch { /* Observer failure is not a persistence failure. */ }
  }
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
  const raw = await readSettingsSection<any>('performanceSchedule');
  const hasLegacyProfiles = ['ac','dc'].some(side => raw.profiles?.[side] && Object.keys(raw.profiles[side]).length > 0);
  const template = !raw.controls && !hasLegacyProfiles && typeof detectPerformanceTemplate === 'function' ? await detectPerformanceTemplate() : 'portable';
  configCache = normalizeConfig(raw);
  if (!raw.controls && !hasLegacyProfiles) configCache.controls = {version:1,ac:defaultContinuousBank('ac',template),dc:defaultContinuousBank('dc',template)};
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

// Global scheduling writes share one queue. A newer request invalidates an
// in-flight legacy adapter before its next hardware commit.
let scheduleOpQueue: Promise<void> = Promise.resolve();
let scheduleOpGen = 0;
let runningScheduleOpGen = 0;
let scheduleSuspended = false;
let activeCoreRule: { key: string; update(bank: ContinuousSideControls): Promise<void> } | null = null;
const coreOwnerKey = (side: PowerSide, pid: number, birth: string, generation: number) => JSON.stringify([side,pid,birth,generation]);

const POWER_TRANSITION_WRITE_BLOCKED = /hardware writes (?:are|were) blocked (?:during|before) power transition/i;
const POWER_GATE_RETRY_DELAY_MS = 250;
const POWER_GATE_RETRY_PROBES = 4;
const POWER_LIFECYCLE_QUERY_TIMEOUT_MS = 500;

function isPowerTransitionWriteBlocked(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error ?? '');
  return POWER_TRANSITION_WRITE_BLOCKED.test(message);
}
async function waitForPowerWritesReady(): Promise<boolean> {
  for (let attempt = 0; attempt < POWER_GATE_RETRY_PROBES; attempt += 1) {
    const state = await powerLifecycle.get(POWER_LIFECYCLE_QUERY_TIMEOUT_MS).catch(() => null);
    if (state?.phase === 'ready' && state.hardwareWritesAllowed) return true;
    if (attempt + 1 < POWER_GATE_RETRY_PROBES) {
      await new Promise<void>((resolve) => setTimeout(resolve, POWER_GATE_RETRY_DELAY_MS));
    }
  }
  return false;
}
async function withScheduleOp<T>(_enabled: boolean, fn: () => Promise<T>): Promise<T> {
  const gen = ++scheduleOpGen;
  let resolveNext: (() => void) | null = null;
  const ticket = new Promise<void>((resolve) => { resolveNext = resolve; });
  const prev = scheduleOpQueue;
  scheduleOpQueue = scheduleOpQueue.catch(() => {}).then(() => ticket);
  await prev;
  try {
    if (gen !== scheduleOpGen) throw new SkipApplyError();
    runningScheduleOpGen = gen;
    try {
      return await fn();
    } catch (error) {
      if (!isPowerTransitionWriteBlocked(error) || !(await waitForPowerWritesReady())) throw error;
      if (gen !== scheduleOpGen) throw new SkipApplyError();
      return await fn();
    }
  } finally {
    if (runningScheduleOpGen === gen) runningScheduleOpGen = 0;
    resolveNext!();
  }
}
export function isPerformanceScheduleBusy(): boolean { return runningScheduleOpGen !== 0; }
function assertScheduleOpCurrent(): void {
  if (!runningScheduleOpGen || runningScheduleOpGen !== scheduleOpGen) throw new SkipApplyError();
}
export class SkipApplyError extends Error {
  constructor() { super('outdated'); }
}

/**
 * Compatibility adapter for old callers. The old six-tier implementation was
 * removed from the write path; callers are translated to the canonical
 * continuous bank and use the same owner/queue/checkpoint as the new panel.
 */
export async function applyPerformanceSchedule(
  side: PowerSide,
  mode: ScheduleMode,
  config?: PerformanceScheduleConfig,
): Promise<boolean> {
  return withScheduleOp(true, async () => {
    const current = config ? normalizeConfig(config) : await loadPerformanceSchedule();
    const migrated = current.controls ? normalizeContinuousControls(current.controls) :
      continuousControlsFromSchedule({ ...current, active: { ...current.active, [side]: mode } }, await loadCpuProfiles());
    // The canonical apply path checkpoints the persisted owner before every
    // hardware write. Persist the migrated continuous bank first so this
    // compatibility adapter cannot be rejected as an unconfigured global
    // request or briefly write hardware from a bank the runtime cannot reload.
    const next = {
      ...current,
      configured: true,
      enabled: true,
      suspended: false,
      active: { ...current.active, [side]: mode },
      controls: migrated,
    };
    await savePerformanceSchedule(next);
    scheduleSuspended = false;
    return applyContinuousBankUnsafe(side, migrated[side], undefined, await ensureGlobalFrameRates());
  });
}

export type PerformanceScheduleOwnership = 'none' | 'manual' | 'auto';

export interface PerformanceScheduleRestoreOptions {
  /** Only inspect/apply an active game-custom owner; never take over global manual mode. */
  dedicatedOnly?: boolean;
  /** Passive lifecycle/game refreshes must not replay saved manual CPU/TDP values. */
  automaticOnly?: boolean;
}

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

/** An explicit user action. The simulator may request this, but draft/reset never calls it. */
export async function stopPerformanceScheduleAndUseWindowsBalanced(): Promise<void> {
  // Close admission immediately, before waiting behind any older operation.
  scheduleSuspended = true;
  activeCoreRule = null;
  return withScheduleOp(false, async () => {
    await disableFloat();
    assertScheduleOpCurrent();
    const config = await loadPerformanceSchedule();
    await savePerformanceSchedule({ ...config, suspended: true, enabled: false });
    assertScheduleOpCurrent();
    await switchScheme('bal');
    assertScheduleOpCurrent();
    if ((await getActiveScheme()).toLowerCase() !== PW.WIN_BAL.toLowerCase())
      throw new Error('调度已关闭，但 Windows 平衡尚未确认，请重试切换');
  });
}

export async function getPerformanceScheduleOwnership(): Promise<PerformanceScheduleOwnership> {
  const [config, custom, side, game] = await Promise.all([
    loadPerformanceSchedule(), loadGameCustomConfig(), detectPowerModeReliable(), detectPolicyGame(),
  ]);
  if (!side || scheduleSuspended || config.suspended) return 'none';
  const entry = game ? custom.entries[gamePolicyKey(game)] : undefined;
  if (entry && entry.enabled !== false) return continuousControlsForEntry(entry, config)[side].mode;
  if (!config.configured) return 'none';
  if (!config.enabled) return 'manual';
  return continuousControlsFromSchedule(config)[side].mode;
}

export async function restorePerformanceScheduleIfConfigured(
  expectedSide?: PowerSide,
  options: PerformanceScheduleRestoreOptions = {},
): Promise<PerformanceScheduleOwnership> {
  if (scheduleSuspended) return 'none';
  return withScheduleOp(true, async () => {
    const side = await detectPowerModeReliable();
    if (!side || (expectedSide && side !== expectedSide)) throw new SkipApplyError();
    const master = await loadPerformanceSchedule();
    if (scheduleSuspended || master.suspended) return 'none';
    const game = await detectPolicyGame();
    const custom = await loadGameCustomConfig();
    const key = game ? gamePolicyKey(game) : '';
    const entry = key ? custom.entries[key] : undefined;
    if (entry && entry.enabled !== false) {
      const snapshot = await getContinuousPerformanceSnapshot('dedicated');
      if (!snapshot.bank) return 'none';
      if (options.automaticOnly && snapshot.bank.mode === 'manual') {
        if (getFloatInfo().enabled) await disableFloat();
        return 'manual';
      }
      const applied = await applyContinuousBankUnsafe(side, snapshot.bank, {
        pid: game!.pid, processCreated: game!.processCreated, policyTarget: true,
      }, snapshot.frameRates);
      if (!applied) throw new SkipApplyError();
      return snapshot.bank.mode === 'auto' ? 'auto' : 'manual';
    }
    // When Windows is currently owned by a non-YMCC scheme, callers may ask
    // for dedicated-only recovery. Never let the global bank take over that
    // scheme; the global YMCC default resumes only after the scheme owner is
    // confirmed as YMCC again.
    if (options.dedicatedOnly === true) return 'none';
    const config = await loadPerformanceSchedule();
    if (!config.configured) return 'none';
    const snapshot = await getContinuousPerformanceSnapshot('global');
    if (!snapshot.bank) return 'none';
    if (options.automaticOnly && (!config.enabled || snapshot.bank.mode === 'manual')) {
      // Release a previous automatic owner once, without restoring its CPU/TDP snapshot.
      if (getFloatInfo().enabled) await disableFloat();
      return 'manual';
    }
    const applied = await applyContinuousBankUnsafe(side, snapshot.bank, undefined, snapshot.frameRates);
    if (!applied) throw new SkipApplyError();
    return snapshot.bank.mode === 'auto' ? 'auto' : 'manual';
  });
}

export async function applyPerformanceScheduleForCurrentPower(): Promise<boolean> {
  try {
    const owner = await restorePerformanceScheduleIfConfigured(undefined, { automaticOnly: true });
    return owner === 'auto';
  } catch (error) {
    if (error instanceof SkipApplyError) return false;
    throw error;
  }
}

export async function refreshPerformanceScheduleCoreMode(): Promise<boolean> {
  if (scheduleSuspended) return false;
  return withScheduleOp(true, async () => {
    const snapshot = await getContinuousPerformanceSnapshot();
    if (scheduleSuspended || (await loadPerformanceSchedule()).suspended || !snapshot.side || !snapshot.bank || !snapshot.configured || snapshot.bank.mode !== 'auto') return false;
    const architecture = await detectCoreArchitecture();
    if (!architecture?.heterogeneous) return false;
    const lifecycle = await powerLifecycle.get();
    if (lifecycle.phase !== 'ready' || !lifecycle.hardwareWritesAllowed) return false;
    if ((await getActiveScheme()).toLowerCase() !== PW.YEMAN.toLowerCase()) return false;
    const latest = await getContinuousPerformanceSnapshot();
    assertScheduleOpCurrent();
    if (continuousIdentityKey(snapshot.identity) !== continuousIdentityKey(latest.identity)) return false;
    const float = getFloatInfo();
    const watts = float.enabled && float.tdpApplied > 0 ? float.tdpApplied : snapshot.bank.tdp;
    const mode = toPowerCoreMode(continuousCoreAt(snapshot.bank, watts));
    if (snapshot.side === 'ac') await setCoreModeAc(mode); else await setCoreModeDc(mode);
    // The delayed boot check refreshes core policy only. Reapplying the whole
    // bank here resets the live float experiment and replays TDP/CPU writes.
    assertScheduleOpCurrent();
    if ((await getActiveScheme()).toLowerCase() === PW.YEMAN.toLowerCase()) await setActiveScheme(PW.YEMAN);
    return true;
  }).catch(() => false);
}

/** Controller repeats use the panel/mirror CAS path. Each admitted delta is saved
 * in the current bank; the old mode-switch cooldown must not drop held-button steps. */
export async function adjustContinuousPerformanceTdp(delta: number): Promise<{
  applied: boolean; side: PowerSide; tdp?: number; blocked?: 'disabled' | 'cooldown' | 'boundary';
}> {
  if (!Number.isInteger(delta) || delta === 0 || Math.abs(delta) > 20) throw new Error('TDP 调节步长无效');
  // Do not wrap this in withScheduleOp: updateContinuousPerformanceControls
  // owns that queue. Nesting an operation here waits on its own unreleased ticket.
  const snapshot = await getContinuousPerformanceSnapshot('effective');
  const side = snapshot.side;
  if (!side) throw new SkipApplyError();
  if (!snapshot.bank) return { applied: false, side, blocked: 'disabled' };
  const upper = Math.min(snapshot.hardwareMaximum, snapshot.bank.tdpCeiling);
  const next = Math.max(snapshot.hardwareMinimum, Math.min(upper, snapshot.bank.tdp + delta));
  if (next === snapshot.bank.tdp) return { applied: false, side, blocked: 'boundary' };
  const updated = await updateContinuousPerformanceControls(snapshot.identity, { tdp: next });
  return { applied: updated.bank?.tdp === next, side, tdp: updated.bank?.tdp };
}

export interface GameCustomProfile {
  controls?: ContinuousControls;
  displayName: string;
  // Persisted separately from the profile definition: saved does not mean active.
  enabled: boolean;
  // ac/dc 保留用于一次性读取旧配置；正式运行只使用 controls。
  ac: ScheduleProfile;
  dc: ScheduleProfile;
  smtIndependent?: boolean;
  hyperThreadPolicyEnabled?: boolean;
  hyperThreadPolicy?: 'default' | 'on' | 'off';
  // 2026-09-29 用户裁决：顶部专属菜单可覆盖全局手柄人格与陀螺仪开关。
  // 'follow' = 不干预，沿用 YMCC「手柄 / 陀螺仪」页面设置；其余值优先于全局。
  // 追加在末尾，缺失的旧条目回落 'follow'，因此旧配置文件完全兼容。
  padPersona?: 'follow' | 'disabled' | 'steamdeck' | 'dualsense-edge' | 'elite';
  // 2026-09-30 用户裁决：陀螺仪下拉直接用陀螺仪页的四个预设——选预设 = 启用陀螺仪
  // 并展开该预设参数。'on' 为旧档值（下拉不再提供，仅在当前生效时如实显示）。
  gyroOverride?: 'follow' | 'off' | 'on' | 'fps' | 'racing' | 'custom' | 'steam';
  // Remember the original preset while gyroOverride is off; same EXE record, no sidebar store.
  gyroPreset?: 'fps' | 'racing' | 'custom' | 'steam';
  /** Per-game input tuning; applied as an overlay, never copied into the base. */
  inputOverrides?: { outputTarget?: Record<string, unknown>; gyroMotion?: Record<string, any> };
}

export interface GameCustomRtssProfile {
  enabled: boolean;
  acFps: number;
  dcFps: number;
  acCeiling?: number; dcCeiling?: number;
  acLastFps?: number; dcLastFps?: number;
}

/**
 * Legacy-only snapshot from the old destructive override implementation.
 * Migrated once into the base; new game overlays never create this field.
 */
export interface GameInputRestoreSnapshot {
  persona: string;
  buttonMappingEnabled: boolean;
  gyroEnabled: boolean;
  motionEnabled: boolean;
  // 2026-09-30：陀螺仪预设覆盖会改写 gyroMotion 顶层参数，恢复时整对象写回
  // （深合并即可还原全部字段；预设覆盖不新增 schema 之外的键）。
  gyroMotion?: Record<string, unknown>;
}

export interface GameCustomConfig {
  version: 1;
  entries: Record<string, GameCustomProfile>;
  // Independent RTSS-only linkage. This never activates the full custom
  // TDP/CPU/core/automatic-schedule profile.
  rtss: Record<string, GameCustomRtssProfile>;
  // 旧版覆写本机设置留下的恢复快照；新版本仅用于一次性迁移，不再创建。
  inputRestore?: GameInputRestoreSnapshot;
}

const GAME_CUSTOM_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\game-custom.json';
const GAME_CUSTOM_BACKUP_PATH = `${GAME_CUSTOM_PATH}.bak`;
const GAME_CUSTOM_MAX_BYTES = 4 * 1024 * 1024;

function defaultGameCustomConfig(): GameCustomConfig {
  return { version: 1, entries: {}, rtss: {} };
}

function normalizeCustomProfile(raw: unknown, fallback: ScheduleProfile): GameCustomProfile {
  const r = raw && typeof raw === 'object' ? (raw as Record<string, unknown>) : {};
  const hyperThreadModes = ['default', 'on', 'off'] as const;
  const hyperThreadPolicy = hyperThreadModes.includes(r.hyperThreadPolicy as typeof hyperThreadModes[number])
    ? r.hyperThreadPolicy as typeof hyperThreadModes[number]
    : 'default';
  const padPersonas = ['follow', 'disabled', 'steamdeck', 'dualsense-edge', 'elite'] as const;
  const gyroOverrides = ['follow', 'on', 'off', 'fps', 'racing', 'custom', 'steam'] as const;
  const padPersona = padPersonas.includes(r.padPersona as typeof padPersonas[number])
    ? r.padPersona as typeof padPersonas[number]
    : migrateControllerPersona(r.padPersona) !== 'disabled' ? migrateControllerPersona(r.padPersona) : 'follow';
  const gyroOverride = gyroOverrides.includes(r.gyroOverride as typeof gyroOverrides[number])
    ? r.gyroOverride as typeof gyroOverrides[number]
    : 'follow';
  const inputOverrides = validInputOverrides(r.inputOverrides, true) ? structuredClone(r.inputOverrides) : undefined;
  const savedTarget = inputOverrides?.outputTarget;
  if (savedTarget && 'persona' in savedTarget) {
    const persona = migrateControllerPersona(savedTarget.persona);
    if (persona !== savedTarget.persona) {
      savedTarget.persona = persona; savedTarget.descriptorHash = null; savedTarget.closure = 'UNENCLOSED';
    }
  }
  return {
    ...r, // Preserve extension fields while normalizing all owned fields below.
    ...(r.controls ? { controls: normalizeContinuousControls(r.controls) } : {}),
    displayName: typeof r.displayName === 'string' ? r.displayName : '',
    // Legacy entries were implicitly active; only an explicit false disables them.
    enabled: r.enabled !== false,
    ac: normalizeProfile(r.ac, fallback),
    dc: normalizeProfile(r.dc, fallback),
    smtIndependent: r.smtIndependent === true,
    hyperThreadPolicyEnabled: r.hyperThreadPolicyEnabled === true,
    hyperThreadPolicy,
    padPersona,
    inputOverrides,
    gyroOverride,
    gyroPreset: ['fps','racing','custom','steam'].includes(r.gyroOverride as string) ? r.gyroOverride as GameCustomProfile['gyroPreset']
      : ['fps','racing','custom','steam'].includes(r.gyroPreset as string) ? r.gyroPreset as GameCustomProfile['gyroPreset'] : undefined,
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
    // 仅大小写不同的重复键：保留第一个（先出现的配置优先），避免静默覆盖丢数据。
    if (!(k in entries)) entries[k] = normalizeCustomProfile(val, fallback);
  }
  const rtssRaw = raw.rtss && typeof raw.rtss === 'object'
    ? (raw.rtss as Record<string, unknown>)
    : {};
  const rtss: Record<string, GameCustomRtssProfile> = {};
  for (const [key, val] of Object.entries(rtssRaw)) {
    if (!key) continue;
    const k = key.toLowerCase();
    if (k in rtss) continue;
    const r = val && typeof val === 'object' ? (val as Record<string, unknown>) : {};
    rtss[k] = {
      ...r, // Preserve fields owned by newer RTSS/dedicated config versions.
      enabled: r.enabled !== false,
      acFps: frameRateSetting(Number(r.acFps) || 0).fps,
      dcFps: frameRateSetting(Number(r.dcFps) || 0).fps,
      acCeiling: frameRateSetting({fps:Number(r.acFps)||0,ceiling:r.acCeiling,lastFps:r.acLastFps}).ceiling,
      dcCeiling: frameRateSetting({fps:Number(r.dcFps)||0,ceiling:r.dcCeiling,lastFps:r.dcLastFps}).ceiling,
      acLastFps: frameRateSetting({fps:Number(r.acFps)||0,lastFps:r.acLastFps}).lastFps,
      dcLastFps: frameRateSetting({fps:Number(r.dcFps)||0,lastFps:r.dcLastFps}).lastFps,
    };
  }
  // Old dedicated snapshots migrate once into an independent RTSS map; after
  // saving, selecting a different performance combination cannot rebind them.
  for (const [key,entry] of Object.entries(entries)) if(!rtss[key]) {
    rtss[key]=dedicatedFrameRateRecord({ac:frameRateSetting(entry.ac.fpsTarget),dc:frameRateSetting(entry.dc.fpsTarget)});
  }
  const restoreRaw = raw.inputRestore && typeof raw.inputRestore === 'object'
    ? (raw.inputRestore as Record<string, unknown>)
    : null;
  const restoredPersona = migrateControllerPersona(restoreRaw?.persona);
  const inputRestore: GameInputRestoreSnapshot | undefined = restoreRaw && (isControllerPersona(restoreRaw.persona) || restoredPersona !== 'disabled')
    ? {
        persona: restoredPersona,
        buttonMappingEnabled: restoreRaw.buttonMappingEnabled === true,
        gyroEnabled: restoreRaw.gyroEnabled === true,
        motionEnabled: restoreRaw.motionEnabled === true,
      }
    : undefined;
  return inputRestore ? { version: 1, entries, rtss, inputRestore } : { version: 1, entries, rtss };
}

let gameCustomCache: GameCustomConfig | null = null;

type GameCustomConfigListener = (config: GameCustomConfig) => void;
const gameCustomConfigListeners = new Set<GameCustomConfigListener>();

function notifyGameCustomConfigChanged(config: GameCustomConfig): void {
  for (const listener of gameCustomConfigListeners) {
    try {
      listener(structuredClone(config));
    } catch {
      // A stale UI listener must never make a successfully persisted config fail.
    }
  }
}

/** Subscribe to successful dedicated-profile saves within this renderer session. */
export function onGameCustomConfigChanged(listener: GameCustomConfigListener): () => void {
  gameCustomConfigListeners.add(listener);
  return () => gameCustomConfigListeners.delete(listener);
}

function parseStandaloneGameCustom(raw: string): GameCustomConfig | null {
  try {
    const parsed = JSON.parse(raw.charCodeAt(0) === 0xFEFF ? raw.slice(1) : raw);
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed) ||
        !parsed.entries || typeof parsed.entries !== 'object' || Array.isArray(parsed.entries)) return null;
    return normalizeGameCustom(parsed);
  } catch { return null; }
}
async function readStandaloneGameCustom(path: string): Promise<GameCustomConfig | null> {
  let raw: string;
  try { raw = await fs.readTextFile(path, GAME_CUSTOM_MAX_BYTES); }
  catch (error) {
    if (await fs.exists(path)) throw new Error(`读取游戏配置失败：${path}；${(error as Error).message}`);
    return null;
  }
  return parseStandaloneGameCustom(raw);
}

async function writeStandaloneGameCustom(config: GameCustomConfig): Promise<void> {
  let previous: Record<string, any> = {};
  if (await fs.exists(GAME_CUSTOM_PATH)) {
    const raw = await fs.readTextFile(GAME_CUSTOM_PATH, GAME_CUSTOM_MAX_BYTES);
    if (parseStandaloneGameCustom(raw)) previous = JSON.parse(raw.charCodeAt(0) === 0xFEFF ? raw.slice(1) : raw);
  }
  const next = { ...previous, ...config };
  // Known maps are replacements so deletions remain durable. For retained
  // entries only, recursively retain forward-compatible opaque fields.
  for (const field of ['entries', 'rtss'] as const) {
    const submitted = config[field] || {};
    const retained: Record<string, any> = {};
    for (const [key, value] of Object.entries(submitted)) {
      const priorKey = Object.keys(previous[field] || {}).find(candidate => candidate.toLowerCase() === key.toLowerCase());
      retained[key] = mergeSettings(priorKey ? previous[field][priorKey] : {}, value);
    }
    next[field] = retained;
  }
  if (!config.inputRestore) delete next.inputRestore;
  else next.inputRestore = mergeSettings(previous.inputRestore || {}, config.inputRestore);
  await fs.writeTextFileAtomic(GAME_CUSTOM_PATH, JSON.stringify(next, null, 2));
}

export function getGameCustomConfig(): GameCustomConfig {
  return gameCustomCache ? structuredClone(gameCustomCache) : defaultGameCustomConfig();
}

async function loadGameCustomConfigUnlocked(): Promise<GameCustomConfig> {

  const standaloneExists = await fs.exists(GAME_CUSTOM_PATH);
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
  const migrated = normalizeGameCustom(await readSettingsSection('gameCustom'));
  await writeStandaloneGameCustom(migrated);
  await replaceSettingsSection('gameCustom', defaultGameCustomConfig() as any);
  gameCustomCache = migrated;
  return structuredClone(gameCustomCache);
}

let gameCustomWriteQueue: Promise<void> = Promise.resolve();
let gameCustomLoad: Promise<GameCustomConfig> | null = null;

export async function loadGameCustomConfig(): Promise<GameCustomConfig> {
  if (!gameCustomLoad) {
    // Cold migration/read participates in the file queue: an older load can
    // neither overwrite a successful save nor publish a stale cached snapshot.
    const run = gameCustomWriteQueue.then(loadGameCustomConfigUnlocked);
    gameCustomWriteQueue = run.then(() => {}, () => {});
    gameCustomLoad = run;
  }
  const task = gameCustomLoad;
  try { return structuredClone(await task); }
  finally { if (gameCustomLoad === task) gameCustomLoad = null; }
}

export async function saveGameCustomConfig(config: GameCustomConfig): Promise<void> {
  await ensureGlobalFrameRates();
  const defaultFrames = frameRatePair({});
  const snapshot = normalizeGameCustom(config);
  for(const key of Object.keys(config.entries))if(!config.rtss?.[key])snapshot.rtss[key]=dedicatedFrameRateRecord(defaultFrames);
  // 队列吞错保活（一次 I/O 失败不再卡死后续写入）；但 run 本身仍向调用方抛出写入失败，
  // 让 UI 能感知「保存失败」而非静默丢失（修复：首败后所有保存永久失效 —— 2026-08-05）。
  const run = gameCustomWriteQueue.then(async () => {
    // Preserve the previous standalone document before replacing it. This is
    // deliberately independent from yeman-settings.json so deleting an entry
    // cannot be undone by a later unified-settings merge.
    if (await fs.exists(GAME_CUSTOM_PATH)) {
      const previous = await fs.readTextFile(GAME_CUSTOM_PATH, GAME_CUSTOM_MAX_BYTES);
      if (parseStandaloneGameCustom(previous)) {
        await fs.writeTextFileAtomic(GAME_CUSTOM_BACKUP_PATH, previous);
      } else {
        // Never overwrite the last valid backup with corrupt main bytes.
        await fs.writeTextFileAtomic(`${GAME_CUSTOM_PATH}.corrupt-${Date.now()}`, previous);
      }
    }
    await writeStandaloneGameCustom(snapshot);
    gameCustomCache = snapshot;
    notifyGameCustomConfigChanged(snapshot);
  });
  gameCustomWriteQueue = run.catch(() => {});
  await run;
}

export type GameCustomProfileTarget = Pick<ContinuousPerformanceIdentity, 'exe' | 'pid' | 'processCreated' | 'generation'>;
export type GameCustomProfileFieldPatch = Partial<Pick<GameCustomProfile, 'padPersona' | 'gyroOverride' | 'gyroPreset' | 'enabled' | 'inputOverrides'>>;
function validInputOverrides(value: unknown, saved = false): value is NonNullable<GameCustomProfile['inputOverrides']> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const record = value as Record<string, unknown>;
  return Object.keys(record).every(key => ['outputTarget', 'gyroMotion'].includes(key)) &&
    Object.values(record).every(section => !!section && typeof section === 'object' && !Array.isArray(section)) &&
    (saved || !record.outputTarget || !('persona' in (record.outputTarget as Record<string, unknown>)) ||
      isControllerPersona((record.outputTarget as Record<string, unknown>).persona));
}

/** Field-scoped writes share the file queue with performance edits and controller TDP changes. */
export async function patchGameCustomProfile(
  expected: GameCustomProfileTarget, patch: GameCustomProfileFieldPatch, seed?: GameCustomProfile,
  options: { createOnly?: boolean; expectedAbsent?: boolean; expectedEnabled?: boolean; expectedInputRevision?: string } = {},
): Promise<void> {
  const allowed = new Set(['padPersona', 'gyroOverride', 'gyroPreset', 'enabled', 'inputOverrides']);
  if (Object.keys(patch).some(key => !allowed.has(key))) throw new Error('专属输入编辑不能修改性能参数');
  if (patch.inputOverrides !== undefined && !validInputOverrides(patch.inputOverrides)) throw new Error('专属输入参数格式无效');
  if (patch.enabled !== undefined && typeof patch.enabled !== 'boolean') throw new Error('专属配置启用状态无效');
  for (const [key, values] of Object.entries({padPersona:['follow','disabled','steamdeck','dualsense-edge','elite'],gyroOverride:['follow','on','off','fps','racing','custom','steam'],gyroPreset:['fps','racing','custom','steam']})) {
    if (Object.prototype.hasOwnProperty.call(patch, key) && !values.includes((patch as Record<string, string>)[key])) throw new Error('专属输入参数无效');
  }
  const verify = () => {
    assertSettingsGeneration(expected.generation);
    const game = getPolicyGame();
    if (!expected.exe || !game || gamePolicyKey(game) !== expected.exe || game.pid !== expected.pid ||
        String(game.processCreated || '') !== expected.processCreated) throw new Error('当前游戏或配置已变化，请重新调整');
  };
  verify();
  await mutateContinuousGameConfig(expected.generation, latest => {
    verify();
    const previous = latest.entries[expected.exe];
    if (options.expectedAbsent && previous) throw new Error('专属配置已变化，请重新启用');
    if (options.createOnly && previous) return;
    if (options.expectedEnabled !== undefined && (!previous || (previous.enabled !== false) !== options.expectedEnabled))
      throw new Error('专属配置启用状态已变化，请重新调整');
    if (options.expectedInputRevision !== undefined && gameProfileInputRevision(previous) !== options.expectedInputRevision)
      throw new Error('专属输入参数已变化，请重新调整');
    const base = previous || seed;
    if (!base) throw new Error('请先创建当前游戏的专属配置');
    latest.entries[expected.exe] = { ...base, ...patch };
  });
}

/** Compare only input-owned fields, allowing concurrent performance edits. */
export function gameProfileInputRevision(profile: GameCustomProfile | undefined): string {
  return JSON.stringify([profile?.padPersona, profile?.gyroOverride, profile?.gyroPreset, profile?.inputOverrides]);
}

/** Import both current power banks and independent FPS into one confirmed EXE record. */
export async function importGameCustomProfile(expected: GameCustomProfileTarget, expectedRevision: string,
  profile: GameCustomProfile, frames: FrameRatePair, checkpoint: () => void = () => {}): Promise<void> {
  const seed = structuredClone(profile), pair = frameRatePair(frames);
  if (!seed.controls || seed.enabled !== true) throw new Error('专属配置必须导入当前连续性能参数');
  const verify = () => {
    checkpoint(); assertSettingsGeneration(expected.generation);
    const game = getPolicyGame();
    if (!expected.exe || !game || gamePolicyKey(game) !== expected.exe || game.pid !== expected.pid ||
        String(game.processCreated || '') !== expected.processCreated) throw new Error('当前游戏已变化，请重新设为专属');
  };
  verify();
  await mutateContinuousGameConfig(expected.generation, latest => {
    verify();
    if (JSON.stringify([latest.entries[expected.exe] ?? null, latest.rtss[expected.exe] ?? null]) !== expectedRevision)
      throw new Error('专属配置已变化，请重新设为专属');
    latest.entries[expected.exe] = seed;
    latest.rtss[expected.exe] = dedicatedFrameRateRecord(pair);
  });
}

/** Delete one confirmed game record under the same queue as all profile writers. */
export async function removeGameCustomProfile(expected: GameCustomProfileTarget, expectedRevision: string): Promise<void> {
  const verify = () => {
    assertSettingsGeneration(expected.generation);
    const game = getPolicyGame();
    if (!game || gamePolicyKey(game) !== expected.exe || game.pid !== expected.pid ||
        String(game.processCreated || '') !== expected.processCreated) throw new Error('当前游戏已变化，请重新确认解除专属');
  };
  verify();
  await mutateContinuousGameConfig(expected.generation, latest => {
    verify();
    const profile = latest.entries[expected.exe];
    if (!profile || JSON.stringify([profile, latest.rtss[expected.exe]]) !== expectedRevision)
      throw new Error('专属配置已变化，请重新确认解除专属');
    delete latest.entries[expected.exe];
    delete latest.rtss[expected.exe];
  });
}

/** Legacy migration clears ONLY the restore field under the existing config queue. */
export async function clearGameInputRestoreSnapshot(expected: GameInputRestoreSnapshot): Promise<void> {
  const run = gameCustomWriteQueue.then(async () => {
    const current = await loadGameCustomConfigUnlocked();
    if (JSON.stringify(current.inputRestore) !== JSON.stringify(expected)) return;
    const next = { ...current, inputRestore: undefined };
    await writeStandaloneGameCustom(next);
    gameCustomCache = next;
    notifyGameCustomConfigChanged(next);
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
  return entry && entry.enabled !== false ? entry[side] : null;
}

/**
 * Compatibility projection for older readers. Formal runtime consumers use
 * continuousControlsForEntry; this helper never writes hardware or selects a
 * named mode.
 */
export function resolveGameCustomProfiles(
  entry: GameCustomProfile,
  schedule: PerformanceScheduleConfig,
): { ac: ScheduleProfile; dc: ScheduleProfile } {
  return {
    ac: { ...entry.ac, ...(entry.controls ? { controls: normalizeContinuousControls(entry.controls).ac } : {}) },
    dc: { ...entry.dc, ...(entry.controls ? { controls: normalizeContinuousControls(entry.controls).dc } : {}) },
  };
}

export function getCustomRtssFor(
  config: GameCustomConfig,
  exeName: string,
): GameCustomRtssProfile | null {
  return config.rtss[exeName.toLowerCase()] ?? null;
}

/**
 * Apply only the RTSS/FPS part of a game-custom profile.
 *
 * This deliberately does not enable the performance schedule, switch the
 * YeMan scheme, touch TDP/CPU/core mode, or start/stop autofloat.
 */
let customRtssOnlyQueue: Promise<void> = Promise.resolve();

export async function applyGameCustomRtssOnly(
  acProfile: ScheduleProfile,
  dcProfile: ScheduleProfile,
  expectedTarget?: PerformanceScheduleTarget,
): Promise<{ applied: boolean; side: PowerSide; fps: number }> {
  const run = customRtssOnlyQueue.then(async () => {
    if (expectedTarget) {
      const current = await validateGameTarget(expectedTarget);
      if (!current || current.pid !== expectedTarget.pid ||
          String(current.processCreated) !== String(expectedTarget.processCreated)) {
        return { applied: false, side: await detectPowerMode(), fps: 0 };
      }
    }
    // RTSS-only is an explicit one-shot owner. Drop a stale debounced
    // autofloat write before committing the requested FPS; this does not
    // enable/disable autofloat or touch its CPU/TDP/core state.
    cancelPendingRtssSync();
    const side = await detectPowerMode();
    const {effectiveFrameRates}=await import('./frameRateLimits');
    const fps=(await effectiveFrameRates()).pair[side].fps;
    if (expectedTarget && !isCurrentGameTarget(expectedTarget)) return { applied: false, side, fps: 0 };
    await setRtssLimit(fps);
    const actual = await readRtssLimit();
    if (actual !== fps) throw new Error(`RTSS FPS 回读不一致：期望 ${fps}，实际 ${actual}`);
    return { applied: true, side, fps };
  });
  customRtssOnlyQueue = run.then(() => undefined, () => undefined);
  return run;
}
// 应用自定义档位：绕开自动档位选择，直接把该游戏的 AC/DC 配置下发到当前电源侧。
/**
 * Compatibility adapter for the retired top-menu profile writer. The top menu
 * now edits ContinuousPerformancePanel directly; this function remains only
 * for old integrations and never contains a second TDP/CPU algorithm.
 */
export async function applyGameCustomProfiles(
  acProfile: ScheduleProfile,
  dcProfile: ScheduleProfile,
  expectedTarget?: PerformanceScheduleTarget,
  frameRates?: FrameRatePair,
): Promise<boolean> {
  const maxAttempts = 3;
  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    try {
      return await withScheduleOp(true, async () => {
        assertScheduleOpCurrent();
        const global = await loadPerformanceSchedule();
        const entry: GameCustomProfile = {
          displayName: 'legacy-profile-adapter',
          enabled: true,
          ac: acProfile,
          dc: dcProfile,
        };
        const controls = continuousControlsForEntry(entry, global);
        if (acProfile.controls) controls.ac = normalizeContinuousBank(acProfile.controls, 'ac', controls.ac);
        if (dcProfile.controls) controls.dc = normalizeContinuousBank(dcProfile.controls, 'dc', controls.dc);
        const side = await detectPowerModeReliable();
        if (!side) return false;
        const target = expectedTarget ? { ...expectedTarget, policyTarget: expectedTarget.policyTarget ?? true } : undefined;
        return applyContinuousBankUnsafe(side, controls[side], target, frameRates ?? await ensureGlobalFrameRates());
      });
    } catch (error) {
      if (!(error instanceof SkipApplyError) || attempt === maxAttempts - 1) throw error;
      await new Promise<void>((resolve) => setTimeout(resolve, 120));
    }
  }
  return false;
}

// Continuous controls share the established queues, settings store and hardware paths.
export type { ContinuousControls, ContinuousSideControls, ContinuousSidePatch, ContinuousPerformanceIdentity, ContinuousPerformanceSnapshot } from './performanceControls';

export function continuousControlsFromSchedule(config: PerformanceScheduleConfig, cpuProfiles?: Awaited<ReturnType<typeof loadCpuProfiles>>): ContinuousControls {
  if (config.controls) return normalizeContinuousControls(config.controls);
  const result = normalizeContinuousControls(undefined);
  if (!config.configured) return result;
  for (const side of ['ac', 'dc'] as const) {
    const legacy = config.profiles[side][config.active[side]], bank = result[side];
    bank.mode = config.enabled ? 'auto' : 'manual';
    bank.tdp = Math.min(150, legacy.tdpMax); bank.tdpCeiling = Math.min(150, Math.max(45, bank.tdp));
    bank.auto.saving = legacy.tdpStrategy;
    const values = cpuProfiles?.profiles[config.enabled ? legacy.cpuPreset : cpuProfiles.active];
    if (values) {
      const frequency = side === 'ac' ? values.acFreq : values.dcFreq;
      bank.manual.frequencyMHz = frequency === 0 ? bank.manual.frequencyMHz : frequency;
      bank.manual.unlimited = frequency === 0;
      bank.manual.aggressiveness = side === 'ac' ? values.acAggr : values.dcAggr;
      bank.manual.turbo = side === 'ac' ? values.acTurbo : values.dcTurbo;
      const oldMax = legacy.cpuTarget === 'none' ? frequency : { eco:4500,bal:3500,perf:3500,aggressive:3000 }[legacy.cpuTarget];
      bank.auto.bias = oldMax === 0 ? 0 : Math.max(0,Math.min(100,Math.round((1-(oldMax/1000-.8)/4.2)*20)*5));
    }
    bank.manual.coreMode = legacy.coreMode === 'only-small' ? 'littleOnly' : legacy.coreMode === 'only-big' ? 'bigOnly' : legacy.coreMode === 'small' ? 'littleFirst' : 'bigFirst';
    bank.auto.coreLow = bank.auto.coreHigh = bank.manual.coreMode;
  }
  return normalizeContinuousControls(result);
}

export function continuousControlsForEntry(entry: GameCustomProfile | undefined, global: PerformanceScheduleConfig, cpuProfiles?: Awaited<ReturnType<typeof loadCpuProfiles>>): ContinuousControls {
  if (entry?.controls) return normalizeContinuousControls(entry.controls);
  const next = continuousControlsFromSchedule(global, cpuProfiles);
  if (entry) for (const side of ['ac', 'dc'] as const) {
    next[side].mode = 'auto'; // Old dedicated entries always owned the automatic controller.
    const values = cpuProfiles?.profiles[entry[side].cpuPreset];
    if (values) {
      const frequency = side === 'ac' ? values.acFreq : values.dcFreq;
      next[side].manual.frequencyMHz = frequency || next[side].manual.frequencyMHz;
      next[side].manual.unlimited = frequency === 0;
      next[side].manual.aggressiveness = side === 'ac' ? values.acAggr : values.dcAggr;
      next[side].manual.turbo = side === 'ac' ? values.acTurbo : values.dcTurbo;
      const oldMax = entry[side].cpuTarget === 'none' ? frequency : { eco:4500,bal:3500,perf:3500,aggressive:3000 }[entry[side].cpuTarget];
      next[side].auto.bias = oldMax === 0 ? 0 : Math.max(0,Math.min(100,Math.round((1-(oldMax/1000-.8)/4.2)*20)*5));
    }
    next[side].manual.coreMode = entry[side].coreMode === 'only-small' ? 'littleOnly' : entry[side].coreMode === 'only-big' ? 'bigOnly' : entry[side].coreMode === 'small' ? 'littleFirst' : 'bigFirst';
    next[side].auto.coreLow = next[side].auto.coreHigh = next[side].manual.coreMode;
    next[side].tdp = Math.min(150, entry[side].tdpMax);
    next[side].tdpCeiling = Math.min(150, Math.max(next[side].tdpCeiling, next[side].tdp));
    next[side].auto.saving = entry[side].tdpStrategy;
  }
  return normalizeContinuousControls(next);
}

export async function getContinuousPerformanceSnapshot(scope: 'effective' | 'global' | 'dedicated' = 'effective', editSide?: PowerSide): Promise<ContinuousPerformanceSnapshot> {
  const generation = getSettingsGeneration();
  const [global, custom, actualSide, game, architecture, capability, legacyCpu] = await Promise.all([
    loadPerformanceSchedule(), loadGameCustomConfig(), detectPowerModeReliable(), detectPolicyGame(),
    detectGameCorePolicy(), getTdpExecutionCapability().catch(() => null), loadCpuProfiles(),
  ]);
  assertSettingsGeneration(generation);
  const exe = game ? gamePolicyKey(game) : '', entry = exe ? custom.entries[exe] : undefined;
  const owner = scope === 'effective' ? entry && entry.enabled !== false ? 'dedicated' : 'global' : scope;
  const controls = owner === 'dedicated' ? continuousControlsForEntry(entry, global, legacyCpu) : continuousControlsFromSchedule(global, legacyCpu);
  const side = editSide ?? actualSide;
  const bank = side ? controls[side] : null;
  const frameRates = owner === 'dedicated' ? dedicatedFrameRatePair(custom.rtss[exe], await ensureGlobalFrameRates()) : await ensureGlobalFrameRates();
  const hyperThreadMode = entry?.hyperThreadPolicyEnabled ? entry.hyperThreadPolicy || 'default' : 'default';
  return { frameRates, gameName: entry?.displayName || game?.name || exe, identity: { scope: owner, followEffective: scope === 'effective', side, actualSide, exe, pid: game?.pid || 0, processCreated: String(game?.processCreated || ''), generation,
      revision: JSON.stringify([!!global.suspended, bank, owner === 'dedicated' ? !!entry && entry.enabled !== false : global.configured, hyperThreadMode, side ? frameRates[side] : null]) },
    controls, side, actualSide, coreGroups: architecture?.groups, bank, hardwareMinimum: getTdpHardwareMinimum(), hardwareMaximum: Math.min(150, getTdpHardwareMaximum()),
    hybrid: architecture?.detected === true && architecture.heterogeneous,
    smtAvailable: !!game && architecture?.detected === true && architecture.smtAvailable,
    hyperThreadMode, configured: owner === 'dedicated' ? !!entry : global.configured };
}

export async function updateContinuousPerformanceControls(expected: ContinuousPerformanceIdentity, patch: ContinuousSidePatch, admit?: () => boolean): Promise<ContinuousPerformanceSnapshot> {
  return withScheduleOp(false, async () => {
    if (admit && !admit()) throw new SkipApplyError();
    const current = await getContinuousPerformanceSnapshot(expected.followEffective ? 'effective' : expected.scope, expected.side ?? undefined);
    if (!expected.side || continuousIdentityKey(current.identity) !== continuousIdentityKey(expected)) throw new Error('供电、游戏或配置已变化，请重新调整');
    const verifyContext = () => {
      if (admit && !admit()) throw new SkipApplyError();
      assertSettingsGeneration(expected.generation);
      const game = getPolicyGame();
      if (expected.followEffective && gameCustomCache) {
        const entry = gameCustomCache.entries[expected.exe];
        const owner = entry && entry.enabled !== false ? 'dedicated' : 'global';
        if (owner !== expected.scope) throw new SkipApplyError();
      }
      if ((powerSourceMode.value !== null && powerSourceMode.value !== (expected.actualSide ?? expected.side)) ||
          (game?.pid || 0) !== expected.pid || String(game?.processCreated || '') !== expected.processCreated ||
          (game ? gamePolicyKey(game) : '') !== expected.exe) throw new SkipApplyError();
    };
    verifyContext();
    const performanceRequested = Object.keys(patch).some(key => key !== 'hyperThreadMode' && key !== 'frameRate');
    const side = expected.side, bank = patchContinuousBank(current.controls[side], patch, side);
    bank.tdpCeiling = Math.max(current.hardwareMinimum, Math.min(current.hardwareMaximum, bank.tdpCeiling));
    bank.tdp = Math.max(current.hardwareMinimum, Math.min(bank.tdpCeiling, bank.tdp));
    bank.auto.coreSplit = Math.min(bank.auto.coreSplit, bank.tdpCeiling);
    const master = await loadPerformanceSchedule();
    const resumeRequested = patch.mode !== undefined && (master.suspended === true || scheduleSuspended);
    const withoutCore = (value: ContinuousSideControls) => JSON.stringify({ ...value, auto: { ...value.auto, coreSplit: 0, coreLow: 'bigFirst', coreHigh: 'bigFirst' } });
    const coreOnly = !resumeRequested && current.configured && withoutCore(bank) === withoutCore(current.controls[side]);
    const performanceChanged = performanceRequested && (resumeRequested || !current.configured || JSON.stringify(bank) !== JSON.stringify(current.controls[side]));
    // Focusing/releasing a slider or selecting the already active mode is not a new setting.
    if (!performanceChanged && patch.hyperThreadMode === undefined && !patch.frameRate) return current;
    const globalForMigration = await loadPerformanceSchedule();
    const legacyCpu = await loadCpuProfiles();
    const custom = await loadGameCustomConfig();
    const entry = expected.exe ? custom.entries[expected.exe] : undefined;
    if (patch.hyperThreadMode !== undefined && (!current.smtAvailable || !expected.exe || !['default', 'on', 'off'].includes(patch.hyperThreadMode))) throw new Error('当前游戏或处理器不支持超线程策略');
    assertScheduleOpCurrent();
    if ((await detectPowerModeReliable()) !== (expected.actualSide ?? side) || getSettingsGeneration() !== expected.generation) throw new SkipApplyError();
    if (expected.scope === 'dedicated') {
      if (!entry) throw new Error('请先从顶部创建当前游戏的专属配置');
      await mutateContinuousGameConfig(expected.generation, latest => {
        verifyContext();
        const previous = latest.entries[expected.exe];
        if (expected.followEffective && previous?.enabled === false) throw new SkipApplyError();
        if (!previous || JSON.stringify(continuousControlsForEntry(previous, globalForMigration, legacyCpu)[side]) !== JSON.stringify(current.controls[side])) throw new Error('专属参数已变化，请重新调整');
        if (performanceChanged) latest.entries[expected.exe] = { ...previous,
          controls: { ...continuousControlsForEntry(previous, globalForMigration, legacyCpu), [side]: bank } };
        if (patch.frameRate) {
          const pair = dedicatedFrameRatePair(latest.rtss[expected.exe], current.frameRates);
          if (JSON.stringify(pair[side]) !== JSON.stringify(current.frameRates[side])) throw new Error('帧率参数已变化，请重新调整');
          pair[side] = frameRateSetting(patch.frameRate, pair[side]); latest.rtss[expected.exe] = dedicatedFrameRateRecord(pair);
        }
      });
    } else if (performanceChanged || patch.frameRate) {
      await updateSettings(settings => {
        verifyContext();
        assertSettingsGeneration(expected.generation);
        const latest = normalizeConfig(settings.performanceSchedule);
        const raw = settings.performanceSchedule as any;
        if (!raw.controls && !['ac','dc'].some(s => raw.profiles?.[s] && Object.keys(raw.profiles[s]).length)) latest.controls = globalForMigration.controls;
        const prior = continuousControlsFromSchedule(latest, legacyCpu);
        if (JSON.stringify(prior[side]) !== JSON.stringify(current.controls[side])) throw new Error('全局参数已变化，请重新调整');
        if (performanceChanged) {
          latest.controls = { ...prior, [side]: bank }; latest.configured = true; if (!coreOnly) latest.enabled = true; if (resumeRequested) latest.suspended = false;
          settings.performanceSchedule = latest as any;
        }
        if (patch.frameRate) {
          const pair = frameRatePair(settings.rtss.templateDefaults === true ? current.frameRates : settings.rtss.frameRates);
          if (JSON.stringify(pair[side]) !== JSON.stringify(current.frameRates[side])) throw new Error('帧率参数已变化，请重新调整');
          pair[side] = frameRateSetting(patch.frameRate, pair[side]); settings.rtss.frameRates = pair as any; settings.rtss.templateDefaults = false;
        }
      });
      if (performanceChanged) emitPerformanceSchedule(await loadPerformanceSchedule());
    }
    if (patch.hyperThreadMode !== undefined) {
      const latest = await loadGameCustomConfig();
      const prior = latest.entries[expected.exe];
      // Global performance settings may still store an EXE-only SMT preference.
      const global = await loadPerformanceSchedule();
      const base = prior || { displayName: expected.exe, enabled: false, controls: continuousControlsFromSchedule(global, legacyCpu),
        ac: { ...global.profiles.ac[global.active.ac] }, dc: { ...global.profiles.dc[global.active.dc] } };
      await mutateContinuousGameConfig(expected.generation, latest => {
        verifyContext();
        latest.entries[expected.exe] = { ...(latest.entries[expected.exe] || base), smtIndependent: true,
          hyperThreadPolicy: patch.hyperThreadMode, hyperThreadPolicyEnabled: patch.hyperThreadMode !== 'default' };
      });
      if (!isPolicyTargetCurrent(expected)) throw new SkipApplyError();
      const applied = await applyGameCorePolicy(expected, 'default', patch.hyperThreadMode);
      if (!applied.ok || !applied.applied) throw new Error(applied.error || '超线程策略未应用');
    }
    assertScheduleOpCurrent();
    const latestCustom = await loadGameCustomConfig(), activeEntry = expected.exe ? latestCustom.entries[expected.exe] : undefined;
    let tdpWriteReceipt: ContinuousPerformanceSnapshot['tdpWriteReceipt'];
    const owns = expected.scope === 'dedicated' ? activeEntry?.enabled !== false : !activeEntry || activeEntry.enabled === false;
    if (resumeRequested) {
      if (expected.scope === 'dedicated') await savePerformanceSchedule({ ...(await loadPerformanceSchedule()), suspended: false, enabled: true });
      scheduleSuspended = false;
    }
    if (owns && performanceChanged && side === (expected.actualSide ?? side) && !scheduleSuspended && !(await loadPerformanceSchedule()).suspended) {
      if (coreOnly) {
        const key = coreOwnerKey(side, expected.pid, expected.processCreated, expected.generation);
        if (activeCoreRule?.key === key) await activeCoreRule.update(bank);
        // No runtime owner/receipt: save the rule without inventing hardware state or restarting a bank.
      } else {
        const frames = expected.scope === 'dedicated' ? dedicatedFrameRatePair(latestCustom.rtss[expected.exe], await ensureGlobalFrameRates()) : undefined;
        const applied = await applyContinuousBankUnsafe(side, bank, expected.scope === 'dedicated' ? { ...expected, policyTarget: true } : undefined, frames, receipt => { tdpWriteReceipt = receipt; }, admit);
        if (!applied) throw new SkipApplyError();
      }
    }
    let frameRateApplied: boolean | undefined;
    if (patch.frameRate) {
      publishFrameRatesChanged((await getContinuousPerformanceSnapshot(expected.followEffective ? 'effective' : expected.scope, side)).frameRates);
      frameRateApplied = await applyIndependentFrameRates();
    }
    const snapshot = await getContinuousPerformanceSnapshot(expected.followEffective ? 'effective' : expected.scope, side);
    return { ...snapshot, ...(patch.frameRate ? { frameRateApplied } : {}), ...(tdpWriteReceipt ? { tdpWriteReceipt } : {}) };
  });
}

async function applyContinuousBankUnsafe(side: PowerSide, requested: ContinuousSideControls,
  expectedTarget?: PerformanceScheduleTarget, frameRates?: FrameRatePair, onTdpWrite?: (receipt: NonNullable<ContinuousPerformanceSnapshot['tdpWriteReceipt']>) => void, admit?: () => boolean): Promise<boolean> {
  const bank = normalizeContinuousBank(requested, side), generation = getSettingsGeneration();
  const game = await detectPolicyGame(), gameKey = game ? gamePolicyKey(game) : '';
  let fingerprint = JSON.stringify(bank);
  const checkpoint = async () => {
    if (scheduleSuspended || (admit && !admit())) return false;
    if (generation !== getSettingsGeneration() || (await detectPowerModeReliable()) !== side) return false;
    const now = getPolicyGame();
    if ((now?.pid || 0) !== (game?.pid || 0) || String(now?.processCreated || '') !== String(game?.processCreated || '')) return false;
    if (expectedTarget && !isPolicyTargetCurrent(expectedTarget)) return false;
    const custom = await loadGameCustomConfig(), entry = gameKey ? custom.entries[gameKey] : undefined;
    const config = await loadPerformanceSchedule();
    if (config.suspended) return false;
    if (expectedTarget) return !!entry && entry.enabled !== false && JSON.stringify((entry.controls ? entry.controls[side] : continuousControlsForEntry(entry, config)[side])) === fingerprint;
    if (entry && entry.enabled !== false) return false;
    return config.configured && JSON.stringify((config.controls ? config.controls[side] : continuousControlsFromSchedule(config)[side])) === fingerprint;
  };
  assertScheduleOpCurrent();
  if (!await checkpoint()) return false;
  await disableFloat(); // drain old CPU writes before the new bank can own the hardware.
  if (!await checkpoint()) return false;
  await ensureRememberedYemanSchemeActive();
  if (!await checkpoint()) return false;
  const watts = Math.max(getTdpHardwareMinimum(), Math.min(150, getTdpHardwareMaximum(), bank.tdp));
  let acceptedWatts: number | null = null;
  await runOptionalPerformanceScheduleTdp(side, watts, async () => {
    const applied = await setTdp(side, watts, { apply: true, save: true, source: 'schedule',
      onAccepted: value => { acceptedWatts = value; } });
    if (!applied) acceptedWatts = null;
  });
  onTdpWrite?.({ requested: watts, accepted: acceptedWatts, ok: acceptedWatts !== null, proof: acceptedWatts === null ? 'unconfirmed' : 'write-ack' });
  if (!await checkpoint()) return false;
  const automatic = automaticCpuControls(bank);
  const freq = bank.mode === 'auto' ? automatic.maxMHz : bank.manual.unlimited ? 0 : bank.manual.frequencyMHz;
  const aggr = bank.mode === 'auto' ? automatic.aggressiveness : bank.manual.aggressiveness;
  const turbo = bank.mode === 'auto' ? automatic.turbo : bank.manual.turbo;
  const minState = bank.mode === 'auto' ? automatic.minState : manualCpuMinimum(bank);
  await applyManualPowerParams({ acFreq: freq, dcFreq: freq, acAggr: aggr, dcAggr: aggr, acTurbo: turbo, dcTurbo: turbo,
    acMinState: minState, dcMinState: minState, sides: [side], restoreMaxState: false });
  if (!await checkpoint()) return false;
  await setActiveScheme(PW.YEMAN);
  const architecture = await detectCoreArchitecture();
  let coreState: ContinuousCoreState | null = null;
  let currentWatts: number | null = acceptedWatts;
  const applyCore = async (watts: number, immediate = false) => {
    if (!architecture?.heterogeneous || !await checkpoint()) return;
    currentWatts = watts;
    const now = Date.now();
    const next = immediate || !coreState ? { mode: continuousCoreAt(bank, watts), changedAt: now } : stepContinuousCore(bank, coreState, watts, now);
    if (coreState?.mode === next.mode) { coreState = next; if (next.pending) holdContinuousFloatAfterCoreChange(); return; }
    const mode = toPowerCoreMode(next.mode);
    if (side === 'ac') await setCoreModeAc(mode); else await setCoreModeDc(mode);
    if (!await checkpoint()) return;
    await setActiveScheme(PW.YEMAN);
    const changed = coreState !== null;
    coreState = next;
    if (changed) holdContinuousFloatAfterCoreChange();
  };
  activeCoreRule = { key: coreOwnerKey(side, game?.pid || 0, String(game?.processCreated || ''), generation),
    async update(next) {
      Object.assign(bank.auto, { coreSplit: next.auto.coreSplit, coreLow: next.auto.coreLow, coreHigh: next.auto.coreHigh });
      fingerprint = JSON.stringify(bank);
      updateContinuousFloatCoreRules(bank);
      const lifecycle = await powerLifecycle.get();
      if (lifecycle.phase === 'ready' && lifecycle.hardwareWritesAllowed && (await getActiveScheme()).toLowerCase() === PW.YEMAN.toLowerCase() &&
          currentWatts !== null && (!getFloatInfo().enabled || getFloatInfo().tdpApplied > 0) && await checkpoint()) await applyCore(currentWatts, true);
    } };
  if (acceptedWatts !== null) await applyCore(acceptedWatts);
  if (!await checkpoint()) return false;
  const target = (frameRates ?? await ensureGlobalFrameRates())[side].fps;
  setFloatRtssLinked(false);
  if (bank.mode === 'auto' && bank.auto.saving !== 'none' && target > 0) await enableContinuousFloat({ ...bank, tdp: acceptedWatts ?? watts }, side, target, checkpoint, applyCore,
    acceptedWatts === null ? undefined : { requested: watts, accepted: acceptedWatts });
  return checkpoint();
}

export function continuousIdentityKey(identity: ContinuousPerformanceIdentity): string {
  return JSON.stringify([identity.scope, identity.side, identity.actualSide ?? identity.side, identity.exe, identity.pid, identity.processCreated, identity.generation, identity.revision]);
}

async function mutateContinuousGameConfig(generation: number, mutate: (latest: GameCustomConfig) => void): Promise<void> {
  const task = gameCustomWriteQueue.then(async () => {
    assertSettingsGeneration(generation);
    const latest = await loadGameCustomConfigUnlocked();
    mutate(latest);
    assertSettingsGeneration(generation);
    if (await fs.exists(GAME_CUSTOM_PATH)) {
      const previous = await fs.readTextFile(GAME_CUSTOM_PATH, GAME_CUSTOM_MAX_BYTES);
      if (parseStandaloneGameCustom(previous)) await fs.writeTextFileAtomic(GAME_CUSTOM_BACKUP_PATH, previous);
    }
    await writeStandaloneGameCustom(latest);
    gameCustomCache = latest;
    notifyGameCustomConfigChanged(latest);
  });
  gameCustomWriteQueue = task.catch(() => {});
  return task;
}

export async function updateContinuousFrameRate(expected: ContinuousPerformanceIdentity, value: unknown): Promise<FrameRatePair> {
  const valueSnapshot = frameRateSetting(value);
  return (await updateContinuousPerformanceControls(expected, { frameRate: valueSnapshot })).frameRates;
}
