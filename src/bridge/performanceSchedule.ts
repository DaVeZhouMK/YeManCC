import { isControllerPersona, migrateControllerPersona } from './controllerPersona';
import { powerSourceMode } from './powerSource';
import { performanceConsumptionSignature, normalizeHyperThreadMode, normalizeControlPowerSchemes, type ControlPowerSchemes, defaultContinuousBank, normalizeContinuousControls, normalizeContinuousBank, patchContinuousBank, automaticCpuControls, manualCpuMinimum, continuousCoreAt, stepContinuousCore, toPowerCoreMode, type ContinuousCoreState,
  type ContinuousControls, type ContinuousSideControls, type ContinuousSidePatch, type ContinuousPerformanceIdentity as ControlsIdentity, type ContinuousPerformanceSnapshot as ControlsSnapshot } from './performanceControls';
import { detectGameCorePolicy, applyGameCorePolicy, clearGameCorePolicy } from './gameCorePolicy';
import { getPolicyGame, gamePolicyKey } from './gamePolicyTarget';
import { fs, powerLifecycle } from './api';
import { ensureGlobalFrameRates, applyIndependentFrameRates, publishFrameRatesChanged, frameRatePair, dedicatedFrameRatePair, dedicatedFrameRateRecord, frameRateSetting, type FrameRatePair } from './frameRateLimits';
import { getSettingsGeneration, getSettingsSectionRevision, assertSettingsGeneration, updateSettings, mergeSettings, readSettingsSection, replaceSettingsSection, saveSettingsSection } from './settingsRepository';
import { detectPolicyGame, isPolicyTargetCurrent } from './gamePolicyTarget';
import { validateGameTarget, isCurrentGameTarget, isGameStatusCurrent } from './gamedetect';
import {
  detectPowerMode, detectPerformanceTemplate,
  detectPowerModeReliable, applyManualPowerParams, getTdpHardwareMinimum, getTdpHardwareMaximum, getTdpExecutionCapability, getActiveScheme, PW, setActiveScheme,
  ensureRememberedYemanSchemeActive, ensureSelectedPowerScheme,
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
import { pauseContinuousFloatForTdpEdit,
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
  tdpValid?: false;
  /** Legacy migration scalar only: never bound/applied by a performance combination. */
  fpsTarget: number;
  cpuTarget: FloatProfile;
  tdpStrategy: TdpFloatStrategy;
}

export interface PerformanceScheduleConfig {
  /** Persistent global SMT preference; independent from game identity and AC/DC. */
  hyperThreadPolicy?: 'default' | 'off';
  /** One durable global choice per supply side, shared by every game. */
  powerSchemes?: ControlPowerSchemes;
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
  path?: string;
  identityGeneration?: number;
}

// Display snapshots may outlive admission. Every write validates the captured
// instance, including same-PID/birth path and native identity-generation changes.
function currentPerformanceTarget(target: PerformanceScheduleTarget): boolean {
  if (!isGameStatusCurrent()) return false;
  const game = getPolicyGame();
  if (!target.pid) return !game;
  return !!game && isCurrentGameTarget(target) &&
    (target.path || '').toLowerCase() === game.path.toLowerCase() &&
    target.identityGeneration === game.identityGeneration;
}
export type ContinuousPerformanceIdentity = ControlsIdentity & Pick<PerformanceScheduleTarget, 'path' | 'identityGeneration'>;
export type ContinuousPerformanceSnapshot = Omit<ControlsSnapshot, 'identity'> & { identity: ContinuousPerformanceIdentity };

const CONFIG_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\performance-schedule.json';
const configListeners = new Set<(config: PerformanceScheduleConfig) => void>();
const appliedListeners = new Set<(signature: string) => void>();
const appliedInvalidationListeners = new Set<() => void>();
export function onContinuousPerformanceApplied(listener: (signature: string) => void, onInvalidated?: () => void): () => void {
  appliedListeners.add(listener);
  if (onInvalidated) appliedInvalidationListeners.add(onInvalidated);
  return () => { appliedListeners.delete(listener); if (onInvalidated) appliedInvalidationListeners.delete(onInvalidated); };
}
function invalidateContinuousPerformanceApplied(): void {
  for (const listener of appliedInvalidationListeners) { try { listener(); } catch { /* receipt observers cannot block the writer */ } }
}
function publishContinuousPerformanceApplied(signature: string): void {
  for (const listener of appliedListeners) { try { listener(signature); } catch { /* observer cannot revoke completed work */ } }
}
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
    powerSchemes: normalizeControlPowerSchemes(undefined),
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
  const invalidTdp = raw.tdpValid === false || value !== undefined && (typeof raw.tdpMax !== 'number' || !Number.isFinite(raw.tdpMax) || raw.tdpMax < 2);
  const tdpMax = Math.max(2, Math.min(150, Math.round(invalidTdp ? fallback.tdpMax : raw.tdpMax ?? fallback.tdpMax)));
  const fpsRaw = Math.round((Number.isFinite(Number(raw.fpsTarget)) ? Number(raw.fpsTarget) : fallback.fpsTarget) / 5) * 5;
  // 0 = 不锁帧（真实数值 0），其余限制在 30~300
  const fpsTarget = fpsRaw <= 0 ? 0 : Math.max(30, Math.min(300, fpsRaw));
  return { ...(invalidTdp ? { tdpValid: false as const } : {}), cpuPreset, coreMode, tdpMax, fpsTarget, cpuTarget, tdpStrategy };
}

function normalizeConfig(value: unknown, fallbackControls?: ContinuousControls): PerformanceScheduleConfig {
  const fallback = defaultPerformanceScheduleConfig();
  const raw = value && typeof value === 'object' ? value as Partial<PerformanceScheduleConfig> : {};
  const activeRaw = raw.active && typeof raw.active === 'object' ? raw.active : {} as Record<PowerSide, ScheduleMode>;
  const profilesRaw = raw.profiles && typeof raw.profiles === 'object' ? raw.profiles : {} as PerformanceScheduleConfig['profiles'];
  const next = defaultPerformanceScheduleConfig();
  next.powerSchemes = normalizeControlPowerSchemes(raw.powerSchemes);
  next.hyperThreadPolicy = normalizeHyperThreadMode(raw.hyperThreadPolicy);
  if (raw.controls) next.controls = normalizeContinuousControls(raw.controls, fallbackControls);
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
  const fallbackControls = configCache?.controls;
  configCache = normalizeConfig(raw, fallbackControls);
  if (!raw.controls && !hasLegacyProfiles) configCache.controls = {version:1,ac:defaultContinuousBank('ac',template),dc:defaultContinuousBank('dc',template)};
  const stored = raw.controls;
  if (raw.configured === true && (fallbackControls || stored !== undefined || !hasLegacyProfiles) &&
      (!stored || typeof stored !== 'object' || Array.isArray(stored) || !stored.ac || !stored.dc)) {
    configCache.controls = normalizeContinuousControls({ac:stored?.ac ?? {},dc:stored?.dc ?? {}},fallbackControls);
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

// Global scheduling writes share one queue. A newer request invalidates an
// in-flight legacy adapter before its next hardware commit.
let scheduleOpQueue: Promise<void> = Promise.resolve();
let scheduleOpGen = 0;
let runningScheduleOpGen = 0;
let scheduleSuspended = false;
interface PreparedTdpEdit {
  apply(receipt: (value: NonNullable<ContinuousPerformanceSnapshot['tdpWriteReceipt']>) => void, admit?: () => boolean): Promise<boolean>;
  release(): void;
}
let activeCoreRule: { key: string; update(bank: ContinuousSideControls): Promise<void>;
  prepareTdp?(prior: ContinuousSideControls, next: ContinuousSideControls): Promise<PreparedTdpEdit | null>;
} | null = null;
const coreOwnerKey = (side: PowerSide, pid: number, birth: string, generation: number, path = '', identityGeneration?: number) => JSON.stringify([side,pid,birth,generation,path.toLowerCase(),identityGeneration]);

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
  const captured = await detectPolicyGame();
  const admitted = () => isGameStatusCurrent() && (captured ? isCurrentGameTarget(captured) : getPolicyGame() === null);
  return withScheduleOp(true, async () => {
    if (!admitted()) throw new SkipApplyError();
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
    if (!admitted()) throw new SkipApplyError();
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
  /** One App startup pass may reuse its latest successful intent; never pass this for wake/AC/DC recovery. */
  completedStartupIntent?: () => string;
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
  return withScheduleOp(true, () => restorePerformanceScheduleUnsafe(expectedSide, options));
}

async function restorePerformanceScheduleUnsafe(expectedSide?: PowerSide, options: PerformanceScheduleRestoreOptions = {}): Promise<PerformanceScheduleOwnership> {
    if (scheduleSuspended) return 'none';
    const side = await detectPowerModeReliable();
    if (!side || (expectedSide && side !== expectedSide)) throw new SkipApplyError();
    const master = await loadPerformanceSchedule();
    if (scheduleSuspended || master.suspended) return 'none';
    const game = await detectPolicyGame();
    if (!isGameStatusCurrent()) throw new SkipApplyError();
    const custom = await loadGameCustomConfig();
    if (!isGameStatusCurrent() || (game ? !isCurrentGameTarget(game) : getPolicyGame() !== null)) throw new SkipApplyError();
    const key = game ? gamePolicyKey(game) : '';
    const restoreManualScheme = async (snapshot: ContinuousPerformanceSnapshot) => {
      if (getFloatInfo().enabled) await disableFloat();
      await ensureSelectedPowerScheme(snapshot.powerScheme, async () => {
        return currentPerformanceTarget(snapshot.identity) && getSettingsGeneration() === snapshot.identity.generation &&
          (await detectPowerModeReliable()) === side &&
          normalizeControlPowerSchemes((await loadPerformanceSchedule()).powerSchemes)[side] === snapshot.powerScheme;
      });
    };
    const entry = key ? custom.entries[key] : undefined;
    if (entry && entry.enabled !== false) {
      const snapshot = await getContinuousPerformanceSnapshot('dedicated');
      if (!snapshot.bank) return 'none';
      if (!isGameStatusCurrent() || !isCurrentGameTarget(game!)) throw new SkipApplyError();
      if (options.automaticOnly && snapshot.bank.mode === 'manual') {
        await restoreManualScheme(snapshot);
        return 'manual';
      }
      const applied = await applyContinuousBankUnsafe(side, snapshot.bank, {
        ...snapshot.identity, policyTarget: true,
      }, snapshot.frameRates, undefined, undefined, options.completedStartupIntent);
      if (!applied) throw new SkipApplyError();
      return snapshot.bank.mode === 'auto' ? 'auto' : 'manual';
    }
    const config = await loadPerformanceSchedule();
    if (!config.configured) return 'none';
    const snapshot = await getContinuousPerformanceSnapshot('global');
    if (!snapshot.bank) return 'none';
    if ((options.automaticOnly || options.dedicatedOnly) && snapshot.bank.mode === 'manual') {
      // Supply/startup/wake can restore only the global plan choice. Saved
      // manual CPU/TDP parameters remain static until an explicit edit.
      await restoreManualScheme(snapshot);
      return 'manual';
    }
    if (options.dedicatedOnly === true) return 'none';
    if (options.automaticOnly && !config.enabled) {
      if (getFloatInfo().enabled) await disableFloat();
      return 'manual';
    }
    const applied = await applyContinuousBankUnsafe(side, snapshot.bank, undefined, snapshot.frameRates, undefined, undefined, options.completedStartupIntent);
    if (!applied) throw new SkipApplyError();
    return snapshot.bank.mode === 'auto' ? 'auto' : 'manual';
}

/** One explicit configuration transaction, consumed by the same runtime writer. */
export function applyPerformanceConfiguration(config: PerformanceScheduleConfig, frameRates: FrameRatePair): Promise<PerformanceScheduleOwnership> {
  return withScheduleOp(false, async () => {
    const generation = getSettingsGeneration();
    const snapshot = normalizeConfig(config), pair = frameRatePair(frameRates);
    const committed = scheduleWriteQueue.then(async () => {
      assertScheduleOpCurrent(); assertSettingsGeneration(generation);
      await ensureGlobalFrameRates();
      await updateSettings(settings => {
        assertScheduleOpCurrent(); assertSettingsGeneration(generation);
        // Performance and independent frame-rate intent become visible together.
        settings.performanceSchedule = snapshot as any;
        settings.rtss = { ...settings.rtss, frameRates: pair as any, templateDefaults: false };
      });
      configCache = snapshot;
      emitPerformanceSchedule(snapshot);
    });
    scheduleWriteQueue = committed.catch(() => {});
    await committed;
    assertScheduleOpCurrent(); assertSettingsGeneration(generation);
    return restorePerformanceScheduleUnsafe();
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
    if (!currentPerformanceTarget(snapshot.identity) || continuousIdentityKey(snapshot.identity) !== continuousIdentityKey(latest.identity)) return false;
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

import { SETTINGS_FILE, loadSettings } from './settingsRepository';
import { snapshotSettingsData } from './settingsSnapshot';
function gameCustomPath(): string { return SETTINGS_FILE.replace(/yeman-settings\.json$/i, 'game-custom.json'); }
const GAME_CUSTOM_MAX_BYTES = 4 * 1024 * 1024;

function defaultGameCustomConfig(): GameCustomConfig {
  return { version: 1, entries: {}, rtss: {} };
}

function customObject(value: unknown): value is Record<string, any> {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}
function repairCustomTypes(raw:Record<string,any>,template:Record<string,any>):Record<string,any>{
  const next={...raw};
  for(const [key,value] of Object.entries(template)){
    const supplied=next[key];
    if(customObject(value))next[key]=customObject(supplied)?repairCustomTypes(supplied,value):structuredClone(value);
    else if(value!==null && (typeof supplied!==typeof value || (typeof value==='number'&&!Number.isFinite(supplied))))next[key]=value;
  }
  return next;
}
function malformedCustomTypes(raw:Record<string,any>,template:Record<string,any>):boolean{
  return Object.entries(template).some(([key,value])=>{
    // TDP faults retain their profile owner and carry tdpValid=false; a higher
    // global fallback must not take over just because one numeric field broke.
    if(key==='tdp'||key==='tdpCeiling'||key==='tdpMax')return false;
    if(!Object.prototype.hasOwnProperty.call(raw,key)||value===null)return false;
    const supplied=raw[key];
    return customObject(value)?!customObject(supplied)||malformedCustomTypes(supplied,value)
      :typeof supplied!==typeof value||(typeof value==='number'&&!Number.isFinite(supplied));
  });
}
function savedCustomInputOverrides(value:unknown):value is NonNullable<GameCustomProfile['inputOverrides']>{
  return customObject(value)&&['outputTarget','gyroMotion'].every(key=>!(key in value)||customObject(value[key]));
}
function normalizeCustomControls(value:Record<string,any>,fallback?:ContinuousControls):ContinuousControls{
  return mergeSettings(value,normalizeContinuousControls(repairCustomTypes(value,normalizeContinuousControls(value,fallback)),fallback)) as ContinuousControls;
}
function normalizeCustomSide(value: unknown, fallback: ScheduleProfile): ScheduleProfile {
  const source = customObject(value) ? value : {};
  const owned = { ...source };
  if (typeof source.tdpMax !== 'number' || !Number.isFinite(source.tdpMax) || source.tdpMax < 2) owned.tdpValid = false;
  for (const field of ['tdpMax', 'fpsTarget']) {
    if (typeof owned[field] !== 'number' || !Number.isFinite(owned[field])) delete owned[field];
  }
  return { ...source, ...normalizeProfile(owned, fallback) };
}

function normalizeCustomProfile(raw: unknown, fallback: ScheduleProfile, fallbackControls?: ContinuousControls): GameCustomProfile {
  const r = customObject(raw) ? raw : {};
  // Optional fields emitted by this normalizer may be undefined in memory.
  // They serialize as absent; explicit null/wrong types remain malformed.
  const malformed=['ac','dc'].some(side=>side in r&&(!customObject(r[side])||malformedCustomTypes(r[side],fallback)))
    || ('controls' in r&&r.controls!==undefined&&(!customObject(r.controls)||malformedCustomTypes(r.controls,normalizeContinuousControls({}))))
    || ('inputOverrides' in r&&r.inputOverrides!==undefined&&!savedCustomInputOverrides(r.inputOverrides));
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
  const inputOverrides = savedCustomInputOverrides(r.inputOverrides) ? structuredClone(r.inputOverrides) : undefined;
  const savedTarget = inputOverrides?.outputTarget;
  if (savedTarget && 'persona' in savedTarget) {
    const persona = migrateControllerPersona(savedTarget.persona);
    if (persona !== savedTarget.persona) {
      savedTarget.persona = persona; savedTarget.descriptorHash = null; savedTarget.closure = 'UNENCLOSED';
    }
  }
  return {
    ...r, // Preserve extension fields while normalizing all owned fields below.
    controls: customObject(r.controls) ? normalizeCustomControls(r.controls,fallbackControls) : undefined,
    displayName: typeof r.displayName === 'string' ? r.displayName : '',
    // Legacy entries were implicitly active; only an explicit false disables them.
    enabled: !malformed && (typeof r.enabled === 'boolean' ? r.enabled : r.enabled === undefined),
    ac: normalizeCustomSide(r.ac, fallback),
    dc: normalizeCustomSide(r.dc, fallback),
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
  const raw = customObject(value) ? snapshotSettingsData(value) : {};
  const entriesRaw = customObject(raw.entries) ? raw.entries : {};
  const entries: Record<string, GameCustomProfile> = Object.create(null);
  for (const [key, val] of Object.entries(entriesRaw)) {
    if (!key || !customObject(val)) continue;
    const k = key.toLowerCase();
    // 仅大小写不同的重复键：保留第一个（先出现的配置优先），避免静默覆盖丢数据。
    if (!Object.prototype.hasOwnProperty.call(entries, k)) entries[k] = normalizeCustomProfile(val, fallback, gameCustomCacheGeneration === getSettingsGeneration() ? gameCustomCache?.entries[k]?.controls : undefined);
  }
  const rtssRaw = customObject(raw.rtss) ? raw.rtss : {};
  const rtss: Record<string, GameCustomRtssProfile> = Object.create(null);
  for (const [key, val] of Object.entries(rtssRaw)) {
    if (!key || !customObject(val)) continue;
    const k = key.toLowerCase();
    if (Object.prototype.hasOwnProperty.call(rtss, k)) continue;
    const r = val;
    const fps = (value: unknown) => typeof value === 'number' && Number.isFinite(value) ? value : 0;
    const optionalNumber = (value: unknown) => typeof value === 'number' && Number.isFinite(value) ? value : undefined;
    rtss[k] = {
      ...r, // Preserve fields owned by newer RTSS/dedicated config versions.
      enabled: (typeof r.enabled === 'boolean' ? r.enabled : r.enabled === undefined)
        && !['acFps','dcFps','acCeiling','dcCeiling','acLastFps','dcLastFps'].some(field=>field in r&&r[field]!==undefined&&(typeof r[field]!=='number'||!Number.isFinite(r[field]))),
      acFps: frameRateSetting(fps(r.acFps)).fps,
      dcFps: frameRateSetting(fps(r.dcFps)).fps,
      acCeiling: frameRateSetting({fps:fps(r.acFps),ceiling:optionalNumber(r.acCeiling),lastFps:optionalNumber(r.acLastFps)}).ceiling,
      dcCeiling: frameRateSetting({fps:fps(r.dcFps),ceiling:optionalNumber(r.dcCeiling),lastFps:optionalNumber(r.dcLastFps)}).ceiling,
      acLastFps: frameRateSetting({fps:fps(r.acFps),lastFps:optionalNumber(r.acLastFps)}).lastFps,
      dcLastFps: frameRateSetting({fps:fps(r.dcFps),lastFps:optionalNumber(r.dcLastFps)}).lastFps,
    };
  }
  // Old dedicated snapshots migrate once into an independent RTSS map; after
  // saving, selecting a different performance combination cannot rebind them.
  for (const [key,entry] of Object.entries(entries)) if(!rtss[key]) {
    rtss[key]=dedicatedFrameRateRecord({ac:frameRateSetting(entry.ac.fpsTarget),dc:frameRateSetting(entry.dc.fpsTarget)});
  }
  const restoreRaw = customObject(raw.inputRestore)
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
  const next = { ...raw, version: 1, entries, rtss } as GameCustomConfig;
  if (inputRestore) next.inputRestore = { ...restoreRaw, ...inputRestore };
  else delete next.inputRestore;
  return next;
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

const GAME_CUSTOM_IO_TIMEOUT_MS = 2000;
const GAME_CUSTOM_OPERATION_TIMEOUT_MS = 8000;
export interface GameCustomRecoveryState {
  status: 'ready' | 'runtime-fallback';
  reason: 'corrupt' | 'unsupported' | 'read-failed' | 'write-failed' | null;
  source: 'main' | 'backup' | 'defaults';
  persisted: boolean;
  message: string;
}
let gameCustomRecovery: GameCustomRecoveryState = { status:'ready',reason:null,source:'defaults',persisted:false,message:'' };
let gameCustomCacheGeneration = -1;
export function getGameCustomRecoveryState(): GameCustomRecoveryState { return { ...gameCustomRecovery }; }
class FutureGameCustomError extends Error {}
function assertGameCustomVersion(value: Record<string, any>): void {
  if ((typeof value.version === 'number' && value.version > 1) ||
      (typeof value.schemaVersion === 'number' && value.schemaVersion > 1)) {
    throw new FutureGameCustomError('游戏专属配置版本高于当前程序，原文件已保留');
  }
}
function parseStandaloneGameCustom(raw: string): GameCustomConfig | null {
  let parsed: unknown;
  try { parsed = JSON.parse(raw.charCodeAt(0) === 0xFEFF ? raw.slice(1) : raw); }
  catch { return null; }
  if (!customObject(parsed)) return null;
  assertGameCustomVersion(parsed);
  if ((parsed.version !== undefined && parsed.version !== 1) ||
      !customObject(parsed.entries) || (parsed.rtss !== undefined && !customObject(parsed.rtss))) return null;
  return normalizeGameCustom(parsed);
}
interface GameCustomOperation { generation:number; path:string; active:boolean; baseline?:string | null; verify?:() => void; }
interface GameCustomDocument { raw:string | null; value:GameCustomConfig | null; }
function checkGameCustom(operation: GameCustomOperation): void {
  assertSettingsGeneration(operation.generation);
  operation.verify?.();
  if (!operation.active || operation.path !== gameCustomPath()) throw new Error('游戏专属配置旧请求已取消或超时');
}
async function gameCustomIO<T>(operation: GameCustomOperation, promise:Promise<T>, label:string):Promise<T> {
  let timer:ReturnType<typeof setTimeout> | undefined;
  try {
    const result = await Promise.race([promise,new Promise<never>((_,reject)=>{
      timer=setTimeout(()=>reject(new Error(`游戏专属配置${label}超时`)),GAME_CUSTOM_IO_TIMEOUT_MS);
    })]);
    checkGameCustom(operation); return result;
  } finally { if(timer!==undefined)clearTimeout(timer); }
}
async function readStandaloneGameCustom(path:string, operation:GameCustomOperation):Promise<GameCustomDocument> {
  checkGameCustom(operation);
  const exists = await gameCustomIO(operation,fs.exists(path),'存在性读取');
  if (exists !== true && exists !== false) throw new Error('游戏专属配置存在性回执无效');
  if (!exists) return {raw:null,value:null};
  // A read rejection is never converted to missing, even if the file disappears.
  const raw = await gameCustomIO(operation,fs.readTextFile(path,GAME_CUSTOM_MAX_BYTES,{bounded:true,timeoutMs:GAME_CUSTOM_IO_TIMEOUT_MS}),'读取');
  if (typeof raw !== 'string' || new TextEncoder().encode(raw).byteLength > GAME_CUSTOM_MAX_BYTES)
    throw new Error('游戏专属配置超过大小限制或读取回执无效');
  return {raw,value:parseStandaloneGameCustom(raw)};
}
// A timed-out write may still be in native I/O. Do not admit another write to
// that path until it settles; a late receipt never publishes cache or listeners.
const gameCustomPendingWrites = new Map<string,Promise<void>>();
async function writeGameCustomText(operation:GameCustomOperation,path:string,content:string):Promise<void> {
  checkGameCustom(operation);
  if(gameCustomPendingWrites.has(path))throw new Error('游戏专属配置仍有未确认写入，请稍后重试');
  const pending=fs.writeTextFileAtomic(path,content);
  gameCustomPendingWrites.set(path,pending);
  void pending.then(()=>{if(gameCustomPendingWrites.get(path)===pending)gameCustomPendingWrites.delete(path);},()=>{if(gameCustomPendingWrites.get(path)===pending)gameCustomPendingWrites.delete(path);});
  await gameCustomIO(operation,pending,'写入');
  const observed=await gameCustomIO(operation,fs.readTextFile(path,GAME_CUSTOM_MAX_BYTES,{bounded:true,timeoutMs:GAME_CUSTOM_IO_TIMEOUT_MS}),'写后确认');
  if(observed!==content)throw new Error('游戏专属配置写后回执未确认实际内容');
}
let gameCustomArchiveSequence=0;
async function writeStandaloneGameCustom(config:GameCustomConfig, operation:GameCustomOperation = {
  generation:getSettingsGeneration(),path:gameCustomPath(),active:true,
}, beforeCommit?:()=>void):Promise<void> {
  checkGameCustom(operation);
  if([...gameCustomPendingWrites.keys()].some(path=>path===operation.path || path.startsWith(operation.path+'.')))throw new Error('游戏专属配置仍有未确认写入，请稍后重试');
  const main=await readStandaloneGameCustom(operation.path,operation);
  const backup=await readStandaloneGameCustom(operation.path+'.bak',operation);
  if(operation.baseline!==undefined && main.raw!==operation.baseline)throw new Error('游戏专属配置在编辑期间已变化，请重新读取后保存');
  // Reading BOTH first protects future backups and all unobserved originals.
  const previous=main.value || backup.value || defaultGameCustomConfig();
  const submitted=normalizeGameCustom(config);
  const next:any={...previous,...submitted};
  for(const field of ['entries','rtss'] as const){
    const retained:Record<string,any>=Object.create(null);
    for(const [key,value] of Object.entries(submitted[field])){
      const prior=Object.keys(previous[field]).find(k=>k.toLowerCase()===key.toLowerCase());
      retained[key]=mergeSettings(prior?previous[field][prior]:{},value);
    }
    next[field]=retained;
  }
  if(!submitted.inputRestore)delete next.inputRestore;
  else next.inputRestore=mergeSettings(previous.inputRestore || {},submitted.inputRestore);
  const content=JSON.stringify(next,null,2);
  if(new TextEncoder().encode(content).byteLength>GAME_CUSTOM_MAX_BYTES)throw new Error('游戏专属配置超过大小限制');
  const stamp=`${Date.now()}-${++gameCustomArchiveSequence}`;
  for(const [path,document] of [[operation.path,main],[operation.path+'.bak',backup]] as const){
    if(document.raw!==null && !document.value)
      await writeGameCustomText(operation,`${path}.corrupt-${stamp}`,document.raw);
  }
  if(main.value && main.raw!==null)await writeGameCustomText(operation,operation.path+'.bak',main.raw);
  // External/native edits during preservation invalidate this old submission.
  const current=await readStandaloneGameCustom(operation.path,operation);
  if(current.raw!==main.raw)throw new Error('游戏专属配置在保存前已变化，请重新读取后保存');
  checkGameCustom(operation);
  beforeCommit?.();
  await writeGameCustomText(operation,operation.path,content);
  for(const [key,value] of Object.entries(normalizeGameCustom(next)))
    Object.defineProperty(config,key,{value,enumerable:true,writable:true,configurable:true});
}
export function getGameCustomConfig(): GameCustomConfig {
  return gameCustomCache && gameCustomCacheGeneration===getSettingsGeneration()
    ? structuredClone(gameCustomCache):defaultGameCustomConfig();
}
async function loadGameCustomConfigUnlocked(operation:GameCustomOperation = {
  generation:getSettingsGeneration(),path:gameCustomPath(),active:true,
}):Promise<GameCustomConfig> {
  let source:GameCustomRecoveryState['source']='defaults';
  let reason:GameCustomRecoveryState['reason']='read-failed';
  try {
    const standalone=await readStandaloneGameCustom(operation.path,operation);
    operation.baseline=standalone.raw;
    const backup=await readStandaloneGameCustom(operation.path+'.bak',operation);
    let next=standalone.value || backup.value;
    source=standalone.value?'main':backup.value?'backup':'defaults';
    if(!next){
      if(standalone.raw!==null || backup.raw!==null){
        reason='corrupt';next=defaultGameCustomConfig();
      }else{
        next=normalizeGameCustom(await readSettingsSection('gameCustom'));checkGameCustom(operation);
        reason='write-failed';await writeStandaloneGameCustom(next,operation);
        await replaceSettingsSection('gameCustom',defaultGameCustomConfig() as any);checkGameCustom(operation);
      }
    }
    gameCustomCache=next;gameCustomCacheGeneration=operation.generation;
    if (reason !== 'corrupt' && gameCustomRecovery.status !== 'ready') invalidateContinuousPerformanceApplied();
    gameCustomRecovery={status:reason==='corrupt'?'runtime-fallback':'ready',reason:reason==='corrupt'?'corrupt':null,source,persisted:!!standalone.value || (standalone.raw===null&&backup.raw===null),message:reason==='corrupt'?'游戏专属配置损坏，使用空配置；有效编辑保存时将保留原件并修复':''};
    return structuredClone(next);
  }catch(error){
    checkGameCustom(operation);
    gameCustomCache=defaultGameCustomConfig();gameCustomCacheGeneration=operation.generation;
    gameCustomRecovery={status:'runtime-fallback',reason:error instanceof FutureGameCustomError?'unsupported':reason,source:'defaults',persisted:false,message:String((error as Error)?.message || error)};
    return structuredClone(gameCustomCache);
  }
}
let gameCustomWriteQueue:Promise<void>=Promise.resolve();
let gameCustomLoad:Promise<GameCustomConfig>|null=null;
let gameCustomLoadGeneration=-1;
async function runGameCustomOperation<T>(generation:number,action:(operation:GameCustomOperation)=>Promise<T>,loading=false):Promise<T>{
  assertSettingsGeneration(generation);
  // Resolve the native-owned root before any standalone I/O; never guess a root
  // after discovery/read failure, and keep migration separate from root policy.
  await loadSettings();assertSettingsGeneration(generation);
  const operation:GameCustomOperation={generation,path:gameCustomPath(),active:true};
  let timer:ReturnType<typeof setTimeout>|undefined;
  try{return await Promise.race([action(operation),new Promise<never>((_,reject)=>{
    timer=setTimeout(()=>reject(new Error('游戏专属配置操作超时')),GAME_CUSTOM_OPERATION_TIMEOUT_MS);
  })]);}catch(error){
    operation.active=false;assertSettingsGeneration(generation);
    gameCustomRecovery={status:'runtime-fallback',reason:error instanceof FutureGameCustomError?'unsupported':loading?'read-failed':'write-failed',source:'defaults',persisted:false,message:String((error as Error)?.message || error)};
    if(loading){gameCustomCache=defaultGameCustomConfig();gameCustomCacheGeneration=generation;return structuredClone(gameCustomCache) as T;}
    throw error;
  }finally{operation.active=false;if(timer!==undefined)clearTimeout(timer);}
}
export async function loadGameCustomConfig():Promise<GameCustomConfig>{
  const generation=getSettingsGeneration();
  if(!gameCustomLoad || gameCustomLoadGeneration!==generation){
    const run=gameCustomWriteQueue.then(()=>runGameCustomOperation(generation,loadGameCustomConfigUnlocked,true));
    gameCustomWriteQueue=run.then(()=>{},()=>{});gameCustomLoad=run;gameCustomLoadGeneration=generation;
  }
  const task=gameCustomLoad;
  try{return structuredClone(await task);}finally{if(gameCustomLoad===task)gameCustomLoad=null;}
}
export async function saveGameCustomConfig(config:GameCustomConfig,verify?:()=>void):Promise<void>{
  const generation=getSettingsGeneration();
  if(customObject(config))assertGameCustomVersion(config);
  const snapshot=normalizeGameCustom(config);
  const suppliedRtss=customObject(config?.rtss)?config.rtss:{};
  const needsFrames=Object.keys(snapshot.entries).filter(key=>!Object.keys(suppliedRtss).some(candidate=>candidate.toLowerCase()===key));
  const run=gameCustomWriteQueue.then(()=>runGameCustomOperation(generation,async operation=>{
    operation.verify=verify;checkGameCustom(operation);
    await ensureGlobalFrameRates();checkGameCustom(operation);
    const frames=frameRatePair({});
    for(const key of needsFrames)snapshot.rtss[key]=dedicatedFrameRateRecord(frames);
    await writeStandaloneGameCustom(snapshot,operation,()=>{
      checkGameCustom(operation);
      // Cancellation may stop a queued request up to the commit dispatch.
      // Finish receipt/cache publication once that allowed write has begun.
      operation.verify=undefined;
    });checkGameCustom(operation);
    gameCustomCache=snapshot;gameCustomCacheGeneration=generation;
    gameCustomRecovery={status:'ready',reason:null,source:'main',persisted:true,message:''};
    notifyGameCustomConfigChanged(snapshot);
  }));
  gameCustomWriteQueue=run.catch(()=>{});await run;
}

export type GameCustomProfileTarget = Pick<ContinuousPerformanceIdentity, 'exe' | 'pid' | 'processCreated' | 'generation' | 'path' | 'identityGeneration'>;
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
  options: { createOnly?: boolean; expectedAbsent?: boolean; expectedEnabled?: boolean; expectedInputRevision?: string; admit?: () => boolean } = {},
): Promise<void> {
  expected = { ...expected };
  const allowed = new Set(['padPersona', 'gyroOverride', 'gyroPreset', 'enabled', 'inputOverrides']);
  if (Object.keys(patch).some(key => !allowed.has(key))) throw new Error('专属输入编辑不能修改性能参数');
  if (patch.inputOverrides !== undefined && !validInputOverrides(patch.inputOverrides)) throw new Error('专属输入参数格式无效');
  if (patch.enabled !== undefined && typeof patch.enabled !== 'boolean') throw new Error('专属配置启用状态无效');
  for (const [key, values] of Object.entries({padPersona:['follow','disabled','steamdeck','dualsense-edge','elite'],gyroOverride:['follow','on','off','fps','racing','custom','steam'],gyroPreset:['fps','racing','custom','steam']})) {
    if (Object.prototype.hasOwnProperty.call(patch, key) && !values.includes((patch as Record<string, string>)[key])) throw new Error('专属输入参数无效');
  }
  const verify = () => {
    assertSettingsGeneration(expected.generation);
    if (!currentPerformanceTarget(expected) || options.admit?.() === false) throw new SkipApplyError();
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
  }, verify);
}

/** Compare only input-owned fields, allowing concurrent performance edits. */
export function gameProfileInputRevision(profile: GameCustomProfile | undefined): string {
  return JSON.stringify([profile?.padPersona, profile?.gyroOverride, profile?.gyroPreset, profile?.inputOverrides]);
}

/** Import both current power banks and independent FPS into one confirmed EXE record. */
export async function importGameCustomProfile(expected: GameCustomProfileTarget, expectedRevision: string,
  profile: GameCustomProfile, frames: FrameRatePair, checkpoint: () => void = () => {}): Promise<void> {
  expected = { ...expected };
  const seed = structuredClone(profile), pair = frameRatePair(frames);
  if (!seed.controls || seed.enabled !== true) throw new Error('专属配置必须导入当前连续性能参数');
  const verify = () => {
    checkpoint(); assertSettingsGeneration(expected.generation);
    if (!currentPerformanceTarget(expected)) throw new SkipApplyError();
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
  }, verify);
}

/** Delete one confirmed game record under the same queue as all profile writers. */
export async function removeGameCustomProfile(expected: GameCustomProfileTarget, expectedRevision: string): Promise<void> {
  expected = { ...expected };
  const verify = () => {
    assertSettingsGeneration(expected.generation);
    if (!currentPerformanceTarget(expected)) throw new SkipApplyError();
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
  }, verify);
}

/** Legacy migration clears ONLY the restore field under the existing config queue. */
export async function clearGameInputRestoreSnapshot(expected: GameInputRestoreSnapshot, admit?: () => boolean): Promise<void> {
  const generation=getSettingsGeneration();
  const run = gameCustomWriteQueue.then(()=>runGameCustomOperation(generation,async operation => {
    operation.verify = () => { if (admit?.() === false) throw new SkipApplyError(); };
    checkGameCustom(operation);
    const current = await loadGameCustomConfigUnlocked(operation);
    if (JSON.stringify(current.inputRestore) !== JSON.stringify(expected)) return;
    const next = { ...current, inputRestore: undefined };
    await writeStandaloneGameCustom(next,operation);
    checkGameCustom(operation);
    gameCustomCache = next;gameCustomCacheGeneration=generation;
    gameCustomRecovery={status:'ready',reason:null,source:'main',persisted:true,message:''};
    notifyGameCustomConfigChanged(next);
  }));
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
  const captured = getPolicyGame();
  const admitted = () => isGameStatusCurrent() && (captured ? isCurrentGameTarget(captured) : getPolicyGame() === null) &&
    (!expectedTarget || currentPerformanceTarget(expectedTarget));
  const run = customRtssOnlyQueue.then(async () => {
    if (!admitted()) return { applied: false, side: await detectPowerMode(), fps: 0 };
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
    if (!admitted()) return { applied: false, side, fps: 0 };
    await setRtssLimit(fps, admitted);
    const actual = await readRtssLimit();
    if (!admitted()) return { applied: false, side, fps: 0 };
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
export type { ContinuousControls, ContinuousSideControls, ContinuousSidePatch } from './performanceControls';

export function continuousControlsFromSchedule(config: PerformanceScheduleConfig, cpuProfiles?: Awaited<ReturnType<typeof loadCpuProfiles>>): ContinuousControls {
  if (config.controls) return normalizeContinuousControls(config.controls);
  const result = normalizeContinuousControls(undefined);
  if (!config.configured) return result;
  for (const side of ['ac', 'dc'] as const) {
    const legacy = config.profiles[side][config.active[side]], bank = result[side];
    bank.mode = config.enabled ? 'auto' : 'manual';
    bank.tdpValid = legacy.tdpValid;
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
    next[side].tdpValid = entry[side].tdpValid;
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
  const hyperThreadMode = scope === 'dedicated'
    ? normalizeHyperThreadMode(entry?.hyperThreadPolicyEnabled ? entry.hyperThreadPolicy : 'default')
    : normalizeHyperThreadMode(global.hyperThreadPolicy);
  const powerScheme = normalizeControlPowerSchemes(global.powerSchemes)[side ?? actualSide ?? 'ac'];
  return { powerScheme, frameRates, gameName: entry?.displayName || game?.name || exe, identity: { scope: owner, followEffective: scope === 'effective', side, actualSide, exe, pid: game?.pid || 0, processCreated: String(game?.processCreated || ''), path: game?.path || '', identityGeneration: game?.identityGeneration, generation,
      revision: JSON.stringify([!!global.suspended, powerScheme, bank, owner === 'dedicated' ? !!entry && entry.enabled !== false : global.configured, hyperThreadMode, side ? frameRates[side] : null]) },
    controls, side, actualSide, coreGroups: architecture?.groups, bank, hardwareMinimum: getTdpHardwareMinimum(), hardwareMaximum: Math.min(150, getTdpHardwareMaximum()),
    hybrid: architecture?.detected === true && architecture.heterogeneous,
    smtAvailable: architecture?.smtAvailable === true,
    hyperThreadMode, configured: owner === 'dedicated' ? !!entry : global.configured };
}

export async function updateContinuousPerformanceControls(expected: ContinuousPerformanceIdentity, patch: ContinuousSidePatch, admit?: () => boolean): Promise<ContinuousPerformanceSnapshot> {
  expected = { ...expected };
  return withScheduleOp(false, async () => {
    if (!currentPerformanceTarget(expected) || (admit && !admit())) throw new SkipApplyError();
    const current = await getContinuousPerformanceSnapshot(expected.followEffective ? 'effective' : expected.scope, expected.side ?? undefined);
    if (!expected.side || continuousIdentityKey(current.identity) !== continuousIdentityKey(expected)) throw new Error('供电、游戏或配置已变化，请重新调整');
    const verifyContext = () => {
      if (!currentPerformanceTarget(expected)) throw new SkipApplyError();
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
    if (patch.powerScheme !== undefined) {
      patchContinuousBank(current.controls[expected.side], patch, expected.side, current.powerScheme);
      if (current.bank?.mode !== 'manual' || Object.keys(patch).length !== 1) throw new Error('请在手动模式单独选择电源方案');
      if (patch.powerScheme === current.powerScheme) return current;
      await updateSettings(settings => {
        verifyContext();
        const latest = normalizeConfig(settings.performanceSchedule, current.controls), pair = normalizeControlPowerSchemes(latest.powerSchemes);
        if (pair[expected.side!] !== current.powerScheme) throw new Error('全局电源选择已变化，请重新读取');
        latest.powerSchemes = { ...pair, [expected.side!]: patch.powerScheme! };
        settings.performanceSchedule = latest as any;
      });
      assertScheduleOpCurrent(); verifyContext();
      const latest = await loadPerformanceSchedule();
      emitPerformanceSchedule(latest);
      const effective = await getContinuousPerformanceSnapshot('effective');
      if (effective.side === expected.side && effective.bank?.mode === 'manual' && !latest.suspended && !scheduleSuspended) {
        activeCoreRule = null;
        await disableFloat();
        verifyContext();
        await ensureSelectedPowerScheme(patch.powerScheme, async () => {
          verifyContext();
          return (await detectPowerModeReliable()) === expected.side &&
            normalizeControlPowerSchemes((await loadPerformanceSchedule()).powerSchemes)[expected.side!] === patch.powerScheme;
        });
      }
      return getContinuousPerformanceSnapshot(expected.followEffective ? 'effective' : expected.scope, expected.side);
    }
    const performanceRequested = Object.keys(patch).some(key => key !== 'hyperThreadMode' && key !== 'frameRate');
    const side = expected.side, bank = patchContinuousBank(current.controls[side], patch, side, current.powerScheme);
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
    const onlyTdp = !resumeRequested && current.configured && Object.keys(patch).every(key => key === 'tdp') &&
      JSON.stringify({ ...bank, tdp: 0 }) === JSON.stringify({ ...current.controls[side], tdp: 0 });
    const ownerKey = coreOwnerKey(side, expected.pid, expected.processCreated, expected.generation, expected.path, expected.identityGeneration);
    const tdpEdit = onlyTdp && side === (expected.actualSide ?? side) && activeCoreRule?.key === ownerKey
      ? await activeCoreRule.prepareTdp?.(current.controls[side], bank) ?? null : null;
    try {
    const globalForMigration = master;
    const legacyCpu = await loadCpuProfiles();
    const custom = await loadGameCustomConfig();
    const entry = expected.exe ? custom.entries[expected.exe] : undefined;
    const globalSmt = expected.scope === 'global' || expected.followEffective === true;
    if (patch.hyperThreadMode !== undefined && (!current.smtAvailable || (!globalSmt && !expected.exe) || !['default', 'on', 'off'].includes(patch.hyperThreadMode))) throw new Error('当前游戏或处理器不支持超线程策略');
    assertScheduleOpCurrent();
    if ((await detectPowerModeReliable()) !== (expected.actualSide ?? side) || getSettingsGeneration() !== expected.generation) throw new SkipApplyError();
    if (expected.scope === 'dedicated') {
      if (!entry) throw new Error('请先从顶部创建当前游戏的专属配置');
      if (performanceChanged || patch.frameRate) await mutateContinuousGameConfig(expected.generation, latest => {
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
      }, verifyContext);
    } else if (performanceChanged || patch.frameRate) {
      await updateSettings(settings => {
        verifyContext();
        assertSettingsGeneration(expected.generation);
        const latest = normalizeConfig(settings.performanceSchedule, current.controls);
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
      const mode = normalizeHyperThreadMode(patch.hyperThreadMode);
      if (globalSmt) {
        await updateSettings(settings => {
          verifyContext();
          const latest = normalizeConfig(settings.performanceSchedule, current.controls);
          if (normalizeHyperThreadMode(latest.hyperThreadPolicy) !== current.hyperThreadMode) throw new Error('全局超线程设置已变化，请重新读取');
          latest.hyperThreadPolicy = mode;
          settings.performanceSchedule = latest as any;
        });
        emitPerformanceSchedule(await loadPerformanceSchedule());
      } else {
        await mutateContinuousGameConfig(expected.generation, latest => {
          verifyContext();
          const previous = latest.entries[expected.exe];
          if (!previous) throw new Error('游戏专属配置已移除');
          if (normalizeHyperThreadMode(previous.hyperThreadPolicyEnabled ? previous.hyperThreadPolicy : 'default') !== current.hyperThreadMode) throw new Error('专属超线程设置已变化，请重新读取');
          latest.entries[expected.exe] = { ...previous, hyperThreadPolicy: mode, hyperThreadPolicyEnabled: mode === 'off' };
        }, verifyContext);
      }
      verifyContext();
      const latest = await loadGameCustomConfig();
      const activeEntry = latest.entries[expected.exe];
      const ownsSmt = globalSmt ? !activeEntry || activeEntry.enabled === false : activeEntry?.enabled !== false;
      if (expected.pid && ownsSmt) {
        if (!isPolicyTargetCurrent(expected)) throw new SkipApplyError();
        if (mode === 'off') {
          const applied = await applyGameCorePolicy(expected, 'default', mode);
          if (!applied.ok || !applied.applied) throw new Error(applied.error || '超线程策略未应用');
        } else if (!await clearGameCorePolicy(expected)) throw new Error('恢复游戏默认超线程失败');
      }
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
        const key = coreOwnerKey(side, expected.pid, expected.processCreated, expected.generation, expected.path, expected.identityGeneration);
        if (activeCoreRule?.key === key) await activeCoreRule.update(bank);
        // No runtime owner/receipt: save the rule without inventing hardware state or restarting a bank.
      } else {
        const frames = !tdpEdit && expected.scope === 'dedicated' ? dedicatedFrameRatePair(latestCustom.rtss[expected.exe], await ensureGlobalFrameRates()) : undefined;
        const applied = tdpEdit
          ? await tdpEdit.apply(receipt => { tdpWriteReceipt = receipt; }, admit)
          : await applyContinuousBankUnsafe(side, bank, expected.scope === 'dedicated' ? { ...expected, policyTarget: true } : undefined, frames, receipt => { tdpWriteReceipt = receipt; }, admit);
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
    } finally { tdpEdit?.release(); }
  });
}

async function applyContinuousBankUnsafe(side: PowerSide, requested: ContinuousSideControls,
  expectedTarget?: PerformanceScheduleTarget, frameRates?: FrameRatePair, onTdpWrite?: (receipt: NonNullable<ContinuousPerformanceSnapshot['tdpWriteReceipt']>) => void, admit?: () => boolean, completedStartupIntent?: () => string): Promise<boolean> {
  const bank = normalizeContinuousBank(requested, side), generation = getSettingsGeneration(), operation = runningScheduleOpGen;
  const game = await detectPolicyGame(), gameKey = game ? gamePolicyKey(game) : '';
  const tdpDataReady = () => bank.tdpValid !== false && (!game || gameCustomRecovery.status === 'ready');
  const selectedScheme = normalizeControlPowerSchemes((await loadPerformanceSchedule()).powerSchemes)[side];
  // Profile commits can precede the queued restore. Check the cached owner at
  // final dispatch and receipt, without rereading files or rebuilding controls.
  const cachedTdpOwnerCurrent = () => {
    if (game && gameCustomRecovery.status !== 'ready') return false;
    const entry = gameKey ? gameCustomCache?.entries[gameKey] : undefined;
    if (!expectedTarget) return !entry || entry.enabled === false;
    if (!entry || entry.enabled === false) return false;
    const cached = entry.controls?.[side];
    return cached ? cached.tdpValid !== false && cached.tdp === bank.tdp && cached.mode === bank.mode && cached.auto.saving === bank.auto.saving :
      entry[side].tdpValid !== false && Math.min(150, entry[side].tdpMax) === bank.tdp && entry[side].tdpStrategy === bank.auto.saving;
  };
  let fingerprint = JSON.stringify(bank);
  let receiptConfig: PerformanceScheduleConfig | null = null;
  let receiptFrames: FrameRatePair | undefined = frameRates;
  const appliedSignature = (): string => {
    if (!receiptConfig || !receiptFrames) return '';
    const owner = expectedTarget ? ['dedicated', gameKey] : ['global', receiptConfig.configured, receiptConfig.enabled];
    const identity = game ? JSON.stringify([game.pid, game.processCreated, game.path.toLowerCase(), game.identityGeneration ?? 0]) : '';
    return performanceConsumptionSignature(owner, identity, side, receiptFrames[side].fps, bank, normalizeControlPowerSchemes(receiptConfig.powerSchemes)[side]);
  };
  const notifyApplied = () => {
    const signature = appliedSignature();
    if (signature) publishContinuousPerformanceApplied(signature);
  };
  const checkpoint = async () => {
    if (!isGameStatusCurrent() || scheduleSuspended || (admit && !admit())) return false;
    if (generation !== getSettingsGeneration() || (await detectPowerModeReliable()) !== side) return false;
    if (game ? !isCurrentGameTarget(game) : getPolicyGame() !== null) return false;
    const now = getPolicyGame();
    if ((now?.pid || 0) !== (game?.pid || 0) || String(now?.processCreated || '') !== String(game?.processCreated || '')) return false;
    if (expectedTarget && (!isPolicyTargetCurrent(expectedTarget) || !currentPerformanceTarget(expectedTarget))) return false;
    const custom = await loadGameCustomConfig(), entry = gameKey ? custom.entries[gameKey] : undefined;
    const config = await loadPerformanceSchedule();
    if (bank.mode === 'manual' && normalizeControlPowerSchemes(config.powerSchemes)[side] !== selectedScheme) return false;
    receiptConfig = config;
    // Recheck after the document/power awaits as well as before them.
    if (!isGameStatusCurrent() || (admit && !admit()) ||
        (game ? !isCurrentGameTarget(game) : getPolicyGame() !== null) ||
        (expectedTarget && !currentPerformanceTarget(expectedTarget)) || config.suspended) return false;
    if (expectedTarget) return !!entry && entry.enabled !== false && JSON.stringify(continuousControlsForEntry(entry, config)[side]) === fingerprint;
    if (entry && entry.enabled !== false) return false;
    return config.configured && JSON.stringify((config.controls ? config.controls[side] : continuousControlsFromSchedule(config)[side])) === fingerprint;
  };
  assertScheduleOpCurrent();
  if (!await checkpoint()) return false;
  // Only the caller's current startup receipt may acknowledge an automatic bank.
  // Ordinary wake/supply restoration always executes its existing repair path.
  const completed = bank.mode === 'auto' ? completedStartupIntent?.() : undefined;
  if (completed && completed === appliedSignature()) return true;
  // A later failed rewrite cannot leave an earlier success acknowledged.
  invalidateContinuousPerformanceApplied();
  await disableFloat(); // drain old CPU writes before the new bank can own the hardware.
  if (!await checkpoint()) return false;
  const systemManaged = bank.mode === 'manual' && selectedScheme !== 'yeman';
  if (systemManaged) {
    activeCoreRule = null;
    // A TDP edit must not replay the selected overlay or CPU policy. Explicit
    // scheme/mode changes and restoration still use the original four-way switch.
    await ensureSelectedPowerScheme(selectedScheme, checkpoint);
  } else if (bank.mode === 'manual') {
    await ensureSelectedPowerScheme('yeman', checkpoint);
  } else {
    await ensureRememberedYemanSchemeActive();
  }
  if (!await checkpoint()) return false;
  const watts = Math.max(getTdpHardwareMinimum(), Math.min(150, getTdpHardwareMaximum(), bank.tdp));
  let acceptedWatts: number | null = null;
  await runOptionalPerformanceScheduleTdp(side, watts, async () => {
    if (!tdpDataReady()) throw new Error('TDP 原始数值或当前游戏配置不可用，保留上次硬件设置并暂停浮动；CPU 参数仍独立应用');
    const applied = await setTdp(side, watts, { apply: true, save: true, source: 'schedule',
      isCurrent: () => operation === scheduleOpGen && runningScheduleOpGen === operation && !scheduleSuspended && (!admit || admit()) && generation === getSettingsGeneration() &&
        isGameStatusCurrent() && (game ? isCurrentGameTarget(game) : getPolicyGame() === null) &&
        (powerSourceMode.value === null || powerSourceMode.value === side) &&
        (!expectedTarget || currentPerformanceTarget(expectedTarget)) && cachedTdpOwnerCurrent(),
      onAccepted: value => { acceptedWatts = value; } });
    if (!applied) acceptedWatts = null;
  });
  if (operation !== scheduleOpGen || runningScheduleOpGen !== operation) return false;
  onTdpWrite?.({ requested: watts, accepted: acceptedWatts, ok: acceptedWatts !== null, proof: acceptedWatts === null ? 'unconfirmed' : 'write-ack' });
  if (!await checkpoint()) return false;
  if (systemManaged) {
    setFloatRtssLinked(false);
    receiptFrames ??= await ensureGlobalFrameRates();
    const current = await checkpoint();
    if (current) notifyApplied();
    return current; // Windows owns CPU frequency/EPP/turbo/core policy.
  }
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
  let liveCheckpoint: (() => Promise<boolean>) | null = null;
  const applyCore = async (watts: number, immediate = false) => {
    const current = immediate || !liveCheckpoint ? checkpoint : liveCheckpoint;
    if (!architecture?.heterogeneous || !await current()) return;
    currentWatts = watts;
    const now = Date.now();
    const next = immediate || !coreState ? { mode: continuousCoreAt(bank, watts), changedAt: now } : stepContinuousCore(bank, coreState, watts, now);
    if (coreState?.mode === next.mode) { coreState = next; if (next.pending) holdContinuousFloatAfterCoreChange(); return; }
    const mode = toPowerCoreMode(next.mode);
    if (side === 'ac') await setCoreModeAc(mode); else await setCoreModeDc(mode);
    if (!await current()) return;
    await setActiveScheme(PW.YEMAN);
    const changed = coreState !== null;
    coreState = next;
    if (changed) holdContinuousFloatAfterCoreChange();
  };
  activeCoreRule = { key: coreOwnerKey(side, game?.pid || 0, String(game?.processCreated || ''), generation, game?.path, game?.identityGeneration),
    async update(next) {
      Object.assign(bank.auto, { coreSplit: next.auto.coreSplit, coreLow: next.auto.coreLow, coreHigh: next.auto.coreHigh });
      fingerprint = JSON.stringify(bank);
      updateContinuousFloatCoreRules(bank);
      const lifecycle = await powerLifecycle.get();
      if (lifecycle.phase === 'ready' && lifecycle.hardwareWritesAllowed && (await getActiveScheme()).toLowerCase() === PW.YEMAN.toLowerCase() &&
          currentWatts !== null && (!getFloatInfo().enabled || getFloatInfo().tdpApplied > 0) && await checkpoint()) await applyCore(currentWatts, true);
    } };
  const bankOwner = activeCoreRule;
  if (acceptedWatts !== null) await applyCore(acceptedWatts);
  if (!await checkpoint()) return false;
  receiptFrames = frameRates ?? await ensureGlobalFrameRates();
  const target = receiptFrames[side].fps;
  setFloatRtssLinked(false);
  // Admission above reconciles durable documents and actual power once.
  // The resident loop consumes that ownership snapshot. Existing settings/game
  // reads and commits invalidate it; no repeated disk/source queries per tick.
  let observedRevision = getSettingsSectionRevision('performanceSchedule');
  let observedCustom = gameCustomCache;
  let reconciliation: Promise<boolean> | null = null;
  let residentRevoked = false;
  const residentContextCurrent = () => {
    if (!isGameStatusCurrent() || residentRevoked || scheduleSuspended || (admit && !admit()) || generation !== getSettingsGeneration()) return false;
    if (game ? !isCurrentGameTarget(game) : getPolicyGame() !== null) return false;
    const current = getPolicyGame();
    return (current?.pid || 0) === (game?.pid || 0) && String(current?.processCreated || '') === String(game?.processCreated || '') &&
      (!expectedTarget || currentPerformanceTarget(expectedTarget));
  };
  const residentCheckpoint = Object.assign(async () => {
    if (!residentContextCurrent()) return false;
    const revision = getSettingsSectionRevision('performanceSchedule');
    if (revision === null) return false;
    if (revision === observedRevision && observedCustom === gameCustomCache) return true;
    if (!reconciliation) {
      const pending = checkpoint().then(current => {
        if (!current || !residentContextCurrent()) return false;
        observedRevision = getSettingsSectionRevision('performanceSchedule');
        observedCustom = gameCustomCache;
        return observedRevision !== null;
      });
      reconciliation = pending;
      void pending.finally(() => { if (reconciliation === pending) reconciliation = null; }).catch(() => {});
    }
    return await reconciliation && residentContextCurrent();
  }, { revoke: () => { residentRevoked = true; },
    isCurrent: () => residentContextCurrent() && observedRevision !== null &&
      getSettingsSectionRevision('performanceSchedule') === observedRevision && cachedTdpOwnerCurrent() });
  liveCheckpoint = residentCheckpoint;
  if (tdpDataReady() && bank.mode === 'auto' && bank.auto.saving !== 'none' && target > 0) await enableContinuousFloat({ ...bank, tdp: acceptedWatts ?? watts }, side, target, residentCheckpoint, applyCore,
    acceptedWatts === null ? undefined : { requested: watts, accepted: acceptedWatts });
  if (bankOwner && activeCoreRule === bankOwner) bankOwner.prepareTdp = async (prior, next) => {
    const editOperation = runningScheduleOpGen;
    if (!residentContextCurrent() || JSON.stringify(prior) !== fingerprint || !await checkpoint()) return null;
    const requiresFloat = bank.mode === 'auto' && bank.auto.saving !== 'none' && target > 0;
    const paused = requiresFloat ? await pauseContinuousFloatForTdpEdit(next.tdp) : null;
    if (requiresFloat && !paused) return null;
    try {
      // This explicit edit checks the actual active scheme once. The resident
      // loop keeps its original event-owned side and does not poll power here.
      if (!residentContextCurrent() || !await checkpoint() || (await getActiveScheme()).toLowerCase() !== PW.YEMAN.toLowerCase()) {
        paused?.release(); return null;
      }
      return {
        release: () => paused?.release(),
        async apply(receipt, editAdmit) {
          const editCurrent = () => editOperation === scheduleOpGen && runningScheduleOpGen === editOperation && residentContextCurrent() && (!editAdmit || editAdmit()) && (!paused || paused.current());
          if (!editCurrent()) return false;
          assertScheduleOpCurrent();
          bank.tdp = next.tdp;
          fingerprint = JSON.stringify(bank);
          if (!await checkpoint() || !editCurrent()) return false;
          assertScheduleOpCurrent();
          let accepted: number | null = null;
          const watts = Math.max(getTdpHardwareMinimum(), Math.min(150, getTdpHardwareMaximum(), bank.tdp));
          await runOptionalPerformanceScheduleTdp(side, watts, async () => {
            const ok = await setTdp(side, watts, { apply: true, save: true, source: 'schedule', isCurrent: () => editCurrent() && cachedTdpOwnerCurrent(), onAccepted: value => { accepted = value; } });
            if (!ok) accepted = null;
          });
          receipt({ requested: watts, accepted, ok: accepted !== null, proof: accepted === null ? 'unconfirmed' : 'write-ack' });
          if (!editCurrent() || !await checkpoint() || !editCurrent()) return false;
          currentWatts = accepted;
          paused?.accept(watts, accepted);
          if (accepted !== null) await applyCore(accepted, true);
          const current = await residentCheckpoint();
          if (current) notifyApplied();
          return current;
        },
      };
    } catch (error) { paused?.release(); throw error; }
  };
  const current = await residentCheckpoint();
  if (current) notifyApplied();
  return current;
}

export function continuousIdentityKey(identity: ContinuousPerformanceIdentity): string {
  return JSON.stringify([identity.scope, identity.side, identity.actualSide ?? identity.side, identity.exe, identity.pid, identity.processCreated, identity.path?.toLowerCase(), identity.identityGeneration, identity.generation, identity.revision]);
}

async function mutateContinuousGameConfig(generation: number, mutate: (latest: GameCustomConfig) => void, verify?: () => void): Promise<void> {
  const task=gameCustomWriteQueue.then(()=>runGameCustomOperation(generation,async operation=>{
    operation.verify=verify;checkGameCustom(operation);
    const latest=await loadGameCustomConfigUnlocked(operation);checkGameCustom(operation);
    mutate(latest);checkGameCustom(operation);
    await writeStandaloneGameCustom(latest,operation);checkGameCustom(operation);
    gameCustomCache=latest;gameCustomCacheGeneration=generation;
    gameCustomRecovery={status:'ready',reason:null,source:'main',persisted:true,message:''};
    notifyGameCustomConfigChanged(latest);
  }));
  gameCustomWriteQueue=task.catch(()=>{});
  return task;
}

export async function updateContinuousFrameRate(expected: ContinuousPerformanceIdentity, value: unknown): Promise<FrameRatePair> {
  const valueSnapshot = frameRateSetting(value);
  return (await updateContinuousPerformanceControls(expected, { frameRate: valueSnapshot })).frameRates;
}
