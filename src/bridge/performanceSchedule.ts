import { isControllerPersona, migrateControllerPersona } from './controllerPersona';
import { powerSourceMode } from './powerSource';
import { beginSharedPowerLifecycleReadback, getSharedPowerLifecycle, isSharedPowerLifecycleAdmitted, onSharedPowerLifecycle, sharedPowerWriteScope, type SharedPowerLifecycleSnapshot, type SharedPowerWriteScope } from './powerLifecycleState';
import { performanceConsumptionSignature, normalizeHyperThreadMode, normalizeControlPowerSchemes, type ControlPowerSchemes, defaultContinuousBank, normalizeContinuousControls, normalizeContinuousBank, patchContinuousBank, automaticCpuControls, manualCpuMinimum, continuousCoreAt, stepContinuousCore, toPowerCoreMode, type ContinuousCoreState,
  type ContinuousControls, type ContinuousSideControls, type ContinuousSidePatch, type ContinuousPerformanceIdentity as ControlsIdentity, type ContinuousPerformanceSnapshot as ControlsSnapshot } from './performanceControls';
import { detectGameCorePolicy, applyGameCorePolicy, clearGameCorePolicy } from './gameCorePolicy';
import { getPolicyGame, gamePolicyKey } from './gamePolicyTarget';
import { fs, powerLifecycle } from './api';
import { loadGlobalFrameRates, ensureGlobalFrameRates, applyIndependentFrameRates, publishFrameRatesChanged, frameRatePair, dedicatedFrameRatePair, dedicatedFrameRateRecord, frameRateSetting, type FrameRatePair } from './frameRateLimits';
import { loadSettings, validateSettingsRead, getSettingsGeneration, getSettingsSectionRevision, assertSettingsGeneration, updateSettings, rememberOrdinaryPerformancePreferences, mergeSettings, readSettingsSection, replaceSettingsSection, saveSettingsSection, type UnifiedSettings } from './settingsRepository';
import {bootStartupReceiptOwner,captureBootPreferenceWrite,type BootPreferenceWrite} from './bootModeGlobalPreferences';
import { detectPolicyGame, isPolicyTargetCurrent } from './gamePolicyTarget';
import { validateGameTarget, isCurrentGameTarget, isGameStatusCurrent } from './gamedetect';
import {
  detectPowerMode, detectPerformanceTemplate,
  detectPowerModeReliable, applyManualPowerParams, getTdpHardwareMinimum, getTdpHardwareMaximum, getTdpExecutionCapability, getActiveScheme, PW, setActiveScheme,
  ensureRememberedYemanSchemeActive, ensureSelectedPowerScheme,
  setCoreModeForPowerScope,
  setActiveSchemeForPowerScope,
  detectCoreArchitecture,
  rebuildYemanScheme,
  setTdp,
  setRtssLimit,
  readRtssLimit,
  switchScheme,
} from './yeman';
import { loadCpuProfiles } from './cpuProfiles';
import { pauseContinuousFloatForTdpEdit,
  reassertContinuousFloatTdp,
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
  /** Resolved defaults still follow the existing battery source until an explicit save. */
  templateDefaults?: true;
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
export interface ContinuousPerformanceApplication {
  tdp: NonNullable<ContinuousPerformanceSnapshot['tdpWriteReceipt']>;
  cpu: 'applied' | 'windows-managed' | 'unconfirmed';
}

const CONFIG_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\performance-schedule.json';
const configListeners = new Set<(config: PerformanceScheduleConfig) => void>();
export interface ContinuousPerformanceApplicationContext {
  readonly generation: number;
  readonly boot: string;
  readonly writeId: string | null;
  readonly mode: 'global' | 'xbox' | 'steam' | 'xbox-steam';
  readonly sourceRevision: number;
  readonly revision: number;
}
// One explicit boot operation identity, never a hardware/configuration cache.
// A later common restore may use it only while the original claim still matches.
let startupApplicationContext: ContinuousPerformanceApplicationContext | undefined;
const appliedListeners = new Set<(signature: string, context?: ContinuousPerformanceApplicationContext) => void>();
const appliedInvalidationListeners = new Set<() => void>();
let rememberedManualIntent = '';
let completedPerformanceIntent: { signature: string; generation: number } | undefined;
// Only an original partial bank can join a later TDP ACK with its completed CPU part.
let pendingPerformanceApplication: { acceptTdp(receipt: ContinuousPerformanceApplication['tdp'], current: () => boolean): Promise<void> } | undefined;
export function onContinuousPerformanceApplied(listener: (signature: string, context?: ContinuousPerformanceApplicationContext) => void, onInvalidated?: () => void): () => void {
  appliedListeners.add(listener);
  if (onInvalidated) appliedInvalidationListeners.add(onInvalidated);
  return () => { appliedListeners.delete(listener); if (onInvalidated) appliedInvalidationListeners.delete(onInvalidated); };
}
function invalidateContinuousPerformanceApplied(): void {
  rememberedManualIntent = '';
  completedPerformanceIntent = undefined;
  pendingPerformanceApplication = undefined;
  for (const listener of appliedInvalidationListeners) { try { listener(); } catch { /* receipt observers cannot block the writer */ } }
}
function publishContinuousPerformanceApplied(signature: string, context?: ContinuousPerformanceApplicationContext): void {
  pendingPerformanceApplication = undefined;
  completedPerformanceIntent = { signature, generation: getSettingsGeneration() };
  for (const listener of appliedListeners) { try { listener(signature, context); } catch { /* observer cannot revoke completed work */ } }
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

function hasLegacyScheduleProfiles(raw: Partial<PerformanceScheduleConfig>): boolean {
  return (['ac', 'dc'] as const).some(side => raw.profiles?.[side] && Object.keys(raw.profiles[side]).length > 0);
}

function normalizeConfig(value: unknown, fallbackControls?: ContinuousControls): PerformanceScheduleConfig {
  const fallback = defaultPerformanceScheduleConfig();
  const raw = value && typeof value === 'object' ? value as Partial<PerformanceScheduleConfig> : {};
  const activeRaw = raw.active && typeof raw.active === 'object' ? raw.active : {} as Record<PowerSide, ScheduleMode>;
  const profilesRaw = raw.profiles && typeof raw.profiles === 'object' ? raw.profiles : {} as PerformanceScheduleConfig['profiles'];
  const next = defaultPerformanceScheduleConfig();
  if (raw.templateDefaults === true && raw.controls === undefined && !hasLegacyScheduleProfiles(raw)) next.templateDefaults = true;
  next.powerSchemes = normalizeControlPowerSchemes(raw.powerSchemes);
  next.hyperThreadPolicy = normalizeHyperThreadMode(raw.hyperThreadPolicy);
  if (raw.controls) next.controls = normalizeContinuousControls(raw.controls, fallbackControls);
  else if (fallbackControls && !hasLegacyScheduleProfiles(raw)) next.controls = normalizeContinuousControls(fallbackControls);
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
let configCacheOwner = '';
let scheduleWriteQueue: Promise<void> = Promise.resolve();

/** Compare canonical owner snapshots so normalization and an unchanged scene
 * bank cannot become an ordinary global edit. */
function performancePreferenceSnapshot(config: PerformanceScheduleConfig, frames: FrameRatePair) {
  const controls = continuousControlsFromSchedule(config);
  return { schedule: { ...structuredClone(config), controls: structuredClone(controls) }, controls,
    powerSchemes: normalizeControlPowerSchemes(config.powerSchemes), frames: frameRatePair(frames) };
}
function rememberGlobalPerformance(settings: any, before: ReturnType<typeof performancePreferenceSnapshot>, config: PerformanceScheduleConfig, options: BootPreferenceWrite = {}): void {
  if (settings.extensions?.bootModes === undefined) return;
  const frames = settings.rtss.templateDefaults === true ? before.frames : settings.rtss.frameRates;
  rememberOrdinaryPerformancePreferences(settings, before, performancePreferenceSnapshot(config, frames), options);
}

function queueScheduleWrite(config: PerformanceScheduleConfig, options: BootPreferenceWrite = {}): Promise<void> {
  const snapshot = normalizeConfig(config), submittedOrigin = { origin: options.origin };
  const generation = getSettingsGeneration();
  const next = scheduleWriteQueue.then(async () => {
    await ensureGlobalFrameRates(); // freeze legacy FPS before a combination/active mode changes
    const reference = await loadPerformanceSchedule();
    await updateSettings(settings => {
      assertSettingsGeneration(generation);
      const current = normalizeConfig(settings.performanceSchedule, reference.controls);
      const before = performancePreferenceSnapshot(current, settings.rtss.frameRates);
      settings.performanceSchedule = snapshot as any;
      rememberGlobalPerformance(settings, before, snapshot, submittedOrigin);
    }, submittedOrigin);
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

export async function loadPerformanceSchedule(captureStartup?: (context: ContinuousPerformanceApplicationContext | undefined, settings: UnifiedSettings) => void): Promise<PerformanceScheduleConfig> {
  const generation = getSettingsGeneration(), settings = await loadSettings();
  assertSettingsGeneration(generation);
  const raw = settings.performanceSchedule as any;
  const owner = JSON.stringify([generation, settings.extensions?.bootModes?.current ?? null, settings.extensions?.bootModesActiveBoot ?? null]);
  const hasLegacyProfiles = hasLegacyScheduleProfiles(raw);
  const stored = raw.controls;
  const freshDefaults = raw.templateDefaults === true && stored === undefined && !hasLegacyProfiles;
  const incomplete = !stored || typeof stored !== 'object' || Array.isArray(stored) || !stored.ac || !stored.dc;
  const needsTemplate = incomplete && !hasLegacyProfiles && typeof detectPerformanceTemplate === 'function';
  const template = needsTemplate ? await detectPerformanceTemplate() : 'portable';
  if (needsTemplate) {
    // Template detection crosses an asynchronous native boundary. Recheck the
    // original source before publishing its result into the shared display cache.
    const latest = await loadSettings();
    assertSettingsGeneration(generation);
    const latestOwner = JSON.stringify([generation, latest.extensions?.bootModes?.current ?? null, latest.extensions?.bootModesActiveBoot ?? null]);
    if (latestOwner !== owner || JSON.stringify(latest.performanceSchedule) !== JSON.stringify(raw))
      throw new Error('性能配置或所属模式已变化，旧机型读取已取消');
  }
  // Retain the last display value for damaged data only within the same applied
  // scene and settings generation. It never authorizes a missing hardware value.
  const retained = configCacheOwner === owner ? configCache?.controls : undefined;
  const deviceControls: ContinuousControls = { version: 1, ac: defaultContinuousBank('ac', template), dc: defaultContinuousBank('dc', template) };
  const fallbackControls = retained ?? deviceControls;
  configCache = normalizeConfig(raw, fallbackControls);
  configCacheOwner = owner;
  if (!raw.controls && !hasLegacyProfiles) configCache.controls = deviceControls;
  if (!freshDefaults && raw.configured === true && (retained || stored !== undefined || !hasLegacyProfiles) && incomplete) {
    configCache.controls = normalizeContinuousControls({ac:stored?.ac ?? {},dc:stored?.dc ?? {}},fallbackControls);
  }
  // Bind completion to the same settings snapshot as this bank. A later event
  // consumer must still validate the full durable request; no extra read here.
  const startup = settings.extensions?.bootModesStartupResult, mode = settings.extensions?.bootModes;
  const claim = captureStartup ? bootStartupReceiptOwner(settings) : undefined;
  const validStartup = claim && mode?.schema === 1 && startup?.schema === 1 && startup.boot === claim.boot &&
    startup.boot === settings.extensions?.bootModesActiveBoot && startup.mode === mode?.current &&
    ['global', 'xbox', 'steam', 'xbox-steam'].includes(startup.mode) && startup.status !== 'failed' &&
    Number.isSafeInteger(startup.sourceRevision) && startup.sourceRevision >= 0 &&
    Number.isSafeInteger(startup.revision) && startup.revision >= startup.sourceRevision;
  captureStartup?.(validStartup ? Object.freeze({ generation, boot: claim.boot, writeId: claim.writeId, mode: startup.mode,
    sourceRevision: startup.sourceRevision, revision: startup.revision }) : undefined, settings);
  return structuredClone(configCache);
}

export function snapshotPerformanceSchedule(
  config: PerformanceScheduleConfig,
): PerformanceScheduleConfig {
  return normalizeConfig(config);
}

export async function savePerformanceSchedule(config: PerformanceScheduleConfig, options: BootPreferenceWrite = {}): Promise<void> {
  await queueScheduleWrite(config, options);
}

// Global scheduling writes share one queue. A newer request invalidates an
// in-flight legacy adapter before its next hardware commit.
let scheduleOpQueue: Promise<void> = Promise.resolve();
let scheduleOpGen = 0;
let runningScheduleOpGen = 0;
let pendingScheduleOps = 0;
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
let schedulePowerRetry: AbortController | null = null;

function isPowerTransitionWriteBlocked(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error ?? '');
  return POWER_TRANSITION_WRITE_BLOCKED.test(message);
}
function matchesSchedulePower(scope: SharedPowerWriteScope, state: SharedPowerLifecycleSnapshot | null): boolean {
  return !!state && !state.scopeReadbackPending && state.generation === scope[0] &&
    state.suspendSerial === scope[1] && state.observedNotificationSequence === scope[2];
}
async function waitForPowerWritesReady(scope: SharedPowerWriteScope, operation: number): Promise<boolean> {
  const controller = new AbortController(), { signal } = controller;
  const current = () => !signal.aborted && operation === scheduleOpGen && matchesSchedulePower(scope, getSharedPowerLifecycle());
  if (!current()) return false;
  schedulePowerRetry = controller;
  const stop = onSharedPowerLifecycle(state => {
    if (!matchesSchedulePower(scope, state)) controller.abort();
  }, { observeNotificationSequence: true, observeSuspendSerial: true });
  try {
    for (let attempt = 0; current() && attempt < POWER_GATE_RETRY_PROBES; attempt += 1) {
      const accept = beginSharedPowerLifecycleReadback();
      const state = await powerLifecycle.get(POWER_LIFECYCLE_QUERY_TIMEOUT_MS, signal).catch(() => null);
      if (!current()) return false;
      accept(state);
      if (!current()) return false;
      if (matchesSchedulePower(scope, state) && isSharedPowerLifecycleAdmitted(state) &&
          isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle())) return true;
      if (attempt + 1 < POWER_GATE_RETRY_PROBES) {
        await new Promise<void>(resolve => {
          const timer = setTimeout(finish, POWER_GATE_RETRY_DELAY_MS);
          function finish() {
            clearTimeout(timer);
            signal.removeEventListener('abort', finish);
            resolve();
          }
          signal.addEventListener('abort', finish, { once: true });
        });
      }
    }
    return false;
  } finally {
    if (schedulePowerRetry === controller) schedulePowerRetry = null;
    stop();
  }
}
async function withScheduleOp<T>(_enabled: boolean, fn: () => Promise<T>, retryBlockedWrite = true): Promise<T> {
  const scope = retryBlockedWrite ? sharedPowerWriteScope(getSharedPowerLifecycle()) : null;
  const gen = ++scheduleOpGen;
  schedulePowerRetry?.abort();
  pendingScheduleOps += 1;
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
      if (!scope || !isPowerTransitionWriteBlocked(error) || !(await waitForPowerWritesReady(scope, gen))) throw error;
      if (gen !== scheduleOpGen) throw new SkipApplyError();
      const state = getSharedPowerLifecycle();
      if (!matchesSchedulePower(scope, state) || !isSharedPowerLifecycleAdmitted(state)) throw error;
      return await fn();
    }
  } finally {
    pendingScheduleOps -= 1;
    if (runningScheduleOpGen === gen) runningScheduleOpGen = 0;
    resolveNext!();
  }
}
export function isPerformanceScheduleBusy(): boolean { return pendingScheduleOps > 0; }
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
  /** Compatibility callers retain auto-only behavior unless restoreRemembered is explicit. */
  automaticOnly?: boolean;
  /** Lifecycle/owner changes restore remembered manual CPU/TDP once as well. */
  restoreRemembered?: boolean;
  /** A committed wake must repair hardware even if the remembered intent is unchanged. */
  forceRemembered?: boolean;
  /** One App startup pass may reuse its latest successful intent; never pass this for wake/AC/DC recovery. */
  completedStartupIntent?: () => string;
  /** A queued restore belongs to its original shared power observation. */
  ownsPowerRequest?: () => boolean;
  /** Release retired observations without interrupting writes or their receipts. */
  powerReadSignal?: AbortSignal;
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
  invalidateContinuousPerformanceApplied();
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

export interface PerformanceRestoreReceipt {
  ownership: PerformanceScheduleOwnership;
  scope: 'none' | 'global' | 'dedicated';
  applied: boolean;
  application?: ContinuousPerformanceApplication;
  reusedCompletedIntent?: true;
}
export function restorePerformanceScheduleIfConfigured(expectedSide: PowerSide | undefined, options: PerformanceScheduleRestoreOptions & { withReceipt: true }): Promise<PerformanceRestoreReceipt>;
export function restorePerformanceScheduleIfConfigured(expectedSide?: PowerSide, options?: PerformanceScheduleRestoreOptions): Promise<PerformanceScheduleOwnership>;
export async function restorePerformanceScheduleIfConfigured(expectedSide?: PowerSide, options: PerformanceScheduleRestoreOptions & { withReceipt?: true } = {}): Promise<PerformanceScheduleOwnership | PerformanceRestoreReceipt> {
  const none: PerformanceRestoreReceipt = { ownership: 'none', scope: 'none', applied: false };
  if (scheduleSuspended) return options.withReceipt ? none : 'none';
  const requestCurrent = options.ownsPowerRequest;
  const signal = requestCurrent ? options.powerReadSignal : undefined;
  let accepted = false, finished = false;
  // The admission predicate can outlive this queued request in the resident controller.
  const current = () => !signal?.aborted && requestCurrent?.() !== false;
  const admitted = () => accepted || (!finished && current());
  const scoped = requestCurrent ? { ...options, ownsPowerRequest: admitted } : options;
  const check = () => { if (!admitted()) throw new SkipApplyError(); };
  let retire!: () => void;
  const retired = signal && new Promise<never>((_, reject) => { retire = () => reject(new SkipApplyError()); });
  if (retired) {
    void retired.catch(() => {});
    signal!.addEventListener('abort', retire, { once: true });
  }
  const reads: WakePerformanceAttempt | undefined = retired && {
    admitted, check, beginActions: check,
    async read(work) {
      if (accepted) return work();
      check();
      const value = await Promise.race([work(signal), retired]);
      check();
      return value;
    },
  };
  const work = withScheduleOp(true, async () => {
    check();
    let receipt: PerformanceRestoreReceipt | undefined;
    const ownership = await restorePerformanceScheduleUnsafe(expectedSide, scoped, (scope, actual, application, reused) => {
      receipt = { ownership: actual, scope, applied: reused === true || application?.tdp.ok === true && application.cpu !== 'unconfirmed',
        ...(application ? { application: structuredClone(application) } : {}), ...(reused ? { reusedCompletedIntent: true } : {}) };
    }, undefined, reads);
    check();
    accepted = true;
    return options.withReceipt ? receipt ?? { ...none, ownership } : ownership;
  }, !requestCurrent);
  return requestCurrent ? work.finally(() => { finished = true; signal?.removeEventListener('abort', retire); }) : work;
}

export type WakePerformanceResult = 'settled' | 'retry-on-ready' | 'retry-on-game-ready' | 'unconfirmed';

class WakeGameTargetPending extends SkipApplyError {}

interface WakePerformanceAttempt {
  admitted: () => boolean;
  check: () => void;
  read<T>(work: (signal?: AbortSignal) => Promise<T>): Promise<T>;
  beginActions: () => void;
}

function readWakePerformance<T>(wake: WakePerformanceAttempt | undefined, work: (signal?: AbortSignal) => Promise<T>): Promise<T> {
  return wake ? wake.read(work) : work();
}

export async function restorePerformanceScheduleAfterWake(expectedSide: PowerSide, dedicatedOnly: boolean,
  ownsWake: () => boolean): Promise<WakePerformanceResult> {
  const origin = getSharedPowerLifecycle();
  if (!ownsWake()) return 'retry-on-ready';
  if (!origin || !isSharedPowerLifecycleAdmitted(origin)) return 'unconfirmed';
  const controller = new AbortController();
  const current = () => {
    const state = getSharedPowerLifecycle();
    return !controller.signal.aborted && ownsWake() && isSharedPowerLifecycleAdmitted(state) &&
      state?.generation === origin.generation && state.suspendSerial === origin.suspendSerial &&
      state.observedNotificationSequence === origin.observedNotificationSequence;
  };
  let retire!: () => void;
  const retired = new Promise<never>((_, reject) => { retire = () => reject(new SkipApplyError()); });
  void retired.catch(() => {});
  const off = onSharedPowerLifecycle(() => {
    if (!current()) { controller.abort(); retire(); }
  }, { observeNotificationSequence: true, observeSuspendSerial: true });
  let beganActions = false;
  let accepted = false;
  let finished = false;
  const wake: WakePerformanceAttempt = {
    // After successful handoff the resident policy uses its normal owner checks.
    admitted: () => accepted || (!finished && current()),
    check() {
      if (!wake.admitted()) throw new SkipApplyError();
    },
    async read(work) {
      if (accepted) return work(); // Resident checks no longer belong to this wake attempt.
      wake.check();
      const value = await Promise.race([work(controller.signal), retired]);
      wake.check();
      return value;
    },
    beginActions() {
      wake.check();
      beganActions = true;
    },
  };
  try {
    return await withScheduleOp<WakePerformanceResult>(true, async () => {
      if (!wake.admitted()) return 'retry-on-ready';
      await restorePerformanceScheduleUnsafe(expectedSide, {
        dedicatedOnly, automaticOnly: true, restoreRemembered: true, forceRemembered: true,
      }, undefined, undefined, wake);
      if (!current()) return beganActions ? 'unconfirmed' : 'retry-on-ready';
      accepted = true;
      return 'settled';
    }, false);
  } catch (error) {
    if (!beganActions && !current()) return 'retry-on-ready';
    if (!beganActions && error instanceof WakeGameTargetPending) return 'retry-on-game-ready';
    if (!(error instanceof SkipApplyError)) console.error('[wake performance restore]', error);
    return 'unconfirmed';
  } finally {
    finished = true;
    off();
  }
}

async function restorePerformanceScheduleUnsafe(expectedSide?: PowerSide, options: PerformanceScheduleRestoreOptions = {},
  accepted?: (scope: 'global' | 'dedicated', ownership: Exclude<PerformanceScheduleOwnership, 'none'>, application?: ContinuousPerformanceApplication, reused?: true) => void,
  startup?: ContinuousPerformanceApplicationContext, wake?: WakePerformanceAttempt,
): Promise<PerformanceScheduleOwnership> {
    if (scheduleSuspended) return 'none';
    const admitted = wake?.admitted ?? options.ownsPowerRequest;
    const side = await readWakePerformance(wake, () => detectPowerModeReliable());
    if (admitted?.() === false || !side || (expectedSide && side !== expectedSide)) throw new SkipApplyError();
    const master = await loadPerformanceSchedule();
    wake?.check();
    if (scheduleSuspended || master.suspended) return 'none';
    const requireGameReadiness = () => {
      if (options.ownsPowerRequest?.() === false) throw new SkipApplyError();
      if (isGameStatusCurrent()) return;
      if (wake) throw new WakeGameTargetPending();
      throw new SkipApplyError();
    };
    const game = await readWakePerformance(wake, () => detectPolicyGame()).catch(error => {
      wake?.check();
      if (wake) requireGameReadiness();
      throw error;
    });
    requireGameReadiness();
    const custom = await loadGameCustomConfig();
    wake?.check();
    requireGameReadiness();
    if (game ? !isCurrentGameTarget(game) : getPolicyGame() !== null) throw new SkipApplyError();
    const key = game ? gamePolicyKey(game) : '';
    const restoreManualScheme = async (snapshot: ContinuousPerformanceSnapshot) => {
      if (options.ownsPowerRequest?.() === false) throw new SkipApplyError();
      if (getFloatInfo().enabled) await disableFloat();
      await ensureSelectedPowerScheme(snapshot.powerScheme, async () => {
        return options.ownsPowerRequest?.() !== false && currentPerformanceTarget(snapshot.identity) && getSettingsGeneration() === snapshot.identity.generation &&
          (await detectPowerModeReliable()) === side &&
          normalizeControlPowerSchemes((await loadPerformanceSchedule()).powerSchemes)[side] === snapshot.powerScheme;
      });
    };
    const entry = key ? custom.entries[key] : undefined;
    if (entry && entry.enabled !== false) {
      const snapshot = await getContinuousPerformanceSnapshot('dedicated', undefined, undefined, 'ensure', undefined, wake);
      if (!snapshot.bank) return 'none';
      requireGameReadiness();
      if (!isCurrentGameTarget(game!)) throw new SkipApplyError();
      if (options.automaticOnly && !options.restoreRemembered && snapshot.bank.mode === 'manual') {
        await restoreManualScheme(snapshot);
        return 'manual';
      }
      let application: ContinuousPerformanceApplication | undefined, reused: true | undefined;
      const applied = await applyContinuousBankUnsafe(side, snapshot.bank, {
        ...snapshot.identity, policyTarget: true,
      }, snapshot.frameRates, undefined, admitted, snapshot.bank.mode === 'manual' && options.restoreRemembered
        ? options.forceRemembered ? undefined : () => rememberedManualIntent : options.completedStartupIntent, value => { application = value; reused = value === undefined ? true : undefined; }, startup, wake);
      if (!applied) throw new SkipApplyError();
      const ownership = snapshot.bank.mode === 'auto' ? 'auto' : 'manual';
      accepted?.('dedicated', ownership, application, reused);
      return ownership;
    }
    const config = await loadPerformanceSchedule();
    wake?.check();
    if (!config.configured) return 'none';
    const snapshot = await getContinuousPerformanceSnapshot('global', undefined, undefined, 'ensure', undefined, wake);
    if (!snapshot.bank) return 'none';
    if ((options.automaticOnly || options.dedicatedOnly) && !options.restoreRemembered && snapshot.bank.mode === 'manual') {
      // Legacy passive callers without restoreRemembered only restore the plan.
      await restoreManualScheme(snapshot);
      return 'manual';
    }
    if (options.dedicatedOnly === true && !options.restoreRemembered) return 'none';
    if (options.automaticOnly && !options.restoreRemembered && !config.enabled) {
      if (options.ownsPowerRequest?.() === false) throw new SkipApplyError();
      if (getFloatInfo().enabled) await disableFloat();
      return 'manual';
    }
    let application: ContinuousPerformanceApplication | undefined, reused: true | undefined;
    const applied = await applyContinuousBankUnsafe(side, snapshot.bank, undefined, snapshot.frameRates, undefined, admitted,
      snapshot.bank.mode === 'manual' && options.restoreRemembered
        ? options.forceRemembered ? undefined : () => rememberedManualIntent : options.completedStartupIntent, value => { application = value; reused = value === undefined ? true : undefined; }, startup, wake);
    if (!applied) throw new SkipApplyError();
    const ownership = snapshot.bank.mode === 'auto' ? 'auto' : 'manual';
    accepted?.('global', ownership, application, reused);
    return ownership;
}

export interface PerformanceConfigurationReceipt {
  saved: true;
  ownership: PerformanceScheduleOwnership;
  scope: 'none' | 'global' | 'dedicated';
  applied: boolean;
  application?: ContinuousPerformanceApplication;
}
/** One explicit configuration transaction, consumed by the same runtime writer. */
interface PerformanceConfigurationOptions extends BootPreferenceWrite { withReceipt: true; startup?: ContinuousPerformanceApplicationContext }
export function applyPerformanceConfiguration(config: PerformanceScheduleConfig, frameRates: FrameRatePair): Promise<PerformanceScheduleOwnership>;
export function applyPerformanceConfiguration(config: PerformanceScheduleConfig, frameRates: FrameRatePair, options: PerformanceConfigurationOptions): Promise<PerformanceConfigurationReceipt>;
export function applyPerformanceConfiguration(config: PerformanceScheduleConfig, frameRates: FrameRatePair, options?: PerformanceConfigurationOptions): Promise<PerformanceScheduleOwnership | PerformanceConfigurationReceipt> {
  const startup = options?.startup ? Object.freeze({ ...options.startup }) : undefined;
  const preferenceWrite=captureBootPreferenceWrite({origin:'boot-mode',...(options?.bootContext?{bootContext:options.bootContext}:{})});
  return withScheduleOp(false, async () => {
    const generation = getSettingsGeneration();
    if (startup && (startup.generation !== generation || !startup.boot || !['global', 'xbox', 'steam', 'xbox-steam'].includes(startup.mode) ||
        !Number.isSafeInteger(startup.sourceRevision) || startup.sourceRevision < 0 || !Number.isSafeInteger(startup.revision) || startup.revision < startup.sourceRevision)) throw new SkipApplyError();
    const snapshot = normalizeConfig(config), pair = frameRatePair(frameRates);
    const committed = scheduleWriteQueue.then(async () => {
      assertScheduleOpCurrent(); assertSettingsGeneration(generation);
      await ensureGlobalFrameRates();
      await updateSettings(settings => {
        assertScheduleOpCurrent(); assertSettingsGeneration(generation);
        // Performance and independent frame-rate intent become visible together.
        settings.performanceSchedule = snapshot as any;
        settings.rtss = { ...settings.rtss, frameRates: pair as any, templateDefaults: false };
      }, preferenceWrite);
      configCache = snapshot;
      emitPerformanceSchedule(snapshot);
    });
    scheduleWriteQueue = committed.catch(() => {});
    await committed;
    assertScheduleOpCurrent(); assertSettingsGeneration(generation);
    if (startup) startupApplicationContext = startup;
    let application: { scope: 'global' | 'dedicated'; ownership: Exclude<PerformanceScheduleOwnership, 'none'>; receipt?: ContinuousPerformanceApplication } | undefined;
    const ownership = await restorePerformanceScheduleUnsafe(undefined, {}, (scope, actual, receipt) => {
      application = { scope, ownership: actual, receipt };
    }, startup);
    // Observe the already accepted original writer; do not infer application
    // from the preceding durable save or perform a second application/read.
    return options?.withReceipt ? { saved: true, ownership, scope: application?.scope ?? 'none',
      applied: !!application && application.ownership === ownership && application.receipt?.tdp.ok === true && application.receipt.cpu !== 'unconfirmed',
      ...(application?.receipt ? { application: structuredClone(application.receipt) } : {}) } : ownership;
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

// Only scalar lookups from already admitted documents. No normalization, file
// read, power query or CPU validation belongs in the final hardware check.
function cachedTdpMaximum(side: PowerSide, displayedFallback = false): number | null {
  if (scheduleSuspended || !configCache || configCache.suspended || gameCustomRecovery.status !== 'ready' ||
      gameCustomCacheGeneration !== getSettingsGeneration()) return null;
  const game = getPolicyGame();
  if (!isGameStatusCurrent() && Object.values(gameCustomCache?.entries ?? {}).some(entry => entry.enabled !== false)) return null;
  const entry = game && isGameStatusCurrent() ? gameCustomCache?.entries[gamePolicyKey(game)] : undefined;
  const dedicated = !!entry && entry.enabled !== false;
  if (!dedicated && !configCache.configured) return null;
  const controls = dedicated ? entry.controls?.[side] : configCache.controls?.[side];
  const legacy = dedicated ? entry[side] : configCache.profiles[side][configCache.active[side]];
  const watts = controls ? controls.tdp : legacy.tdpMax;
  return (displayedFallback || (controls ?? legacy).tdpValid !== false) && Number.isFinite(watts) && watts >= 2 ? watts : null;
}

/** Invalid-source recovery alone may use the same bounded value shown by the
 * shared editor. The invalid flag still pauses floating and is never cleared. */
export function getInvalidPerformanceTdpTarget(): number | null {
  const side = powerSourceMode.value;
  if (!side || !configCache) return null;
  const game = getPolicyGame(), entry = game ? gameCustomCache?.entries[gamePolicyKey(game)] : undefined;
  const dedicated = !!entry && entry.enabled !== false;
  const bank = dedicated ? entry.controls?.[side] ?? entry[side] : configCache.controls?.[side] ?? configCache.profiles[side][configCache.active[side]];
  if (bank?.tdpValid !== false) return null;
  const target = cachedTdpMaximum(side, true);
  return target !== null && target >= getTdpHardwareMinimum() && target <= Math.min(150, getTdpHardwareMaximum()) ? target : null;
}

/** Exceptional slow maintenance uses cached UI intent, never the full restore
 * pipeline. setTdp retains its native final power/owner and range admission. */
export async function reconcileInvalidPerformanceTdp(admit: () => boolean): Promise<boolean> {
  if (isPerformanceScheduleBusy() || !admit() || getInvalidPerformanceTdpTarget() === null) return false;
  return withScheduleOp(false, async () => {
    const side = powerSourceMode.value, target = getInvalidPerformanceTdpTarget();
    if (!side || target === null || !admit()) return false;
    const float = getFloatInfo();
    if (float.enabled && float.tdpOwnerCurrent !== false) return false;
    const generation = getSettingsGeneration(), revision = getSettingsSectionRevision('performanceSchedule'), custom = gameCustomCache;
    const game = getPolicyGame(), operation = runningScheduleOpGen;
    const current = () => admit() && operation === scheduleOpGen && runningScheduleOpGen === operation &&
      generation === getSettingsGeneration() && revision !== null && revision === getSettingsSectionRevision('performanceSchedule') &&
      custom === gameCustomCache && powerSourceMode.value === side &&
      (game ? isCurrentGameTarget(game) : getPolicyGame() === null) && getInvalidPerformanceTdpTarget() === target;
    if (!current()) return false;
    let accepted: number | null = null;
    const applied = await setTdp(side, target, { apply: true, save: false, source: 'schedule', maxWatts: target,
      currentMaxWatts: () => current() ? target : null, isCurrent: current, onAccepted: watts => { accepted = watts; } });
    return applied && current() && accepted !== null && Number.isFinite(accepted) && accepted >= getTdpHardwareMinimum() && accepted <= target;
  });
}

/** TDP-only recovery: no CPU, scheme, FPS or float restart. The existing queue
 * and final dispatch admission remain the only hardware writer. An unknown
 * game may use global TDP only when no enabled dedicated owner can conflict. */
export async function reconcilePerformanceScheduleTdp(admit: () => boolean = () => true): Promise<boolean> {
  if (scheduleSuspended || isPerformanceScheduleBusy() || !admit()) return false;
  const origin = getSharedPowerLifecycle();
  const powerCurrent = () => {
    const state = getSharedPowerLifecycle();
    return admit() && (origin ? isSharedPowerLifecycleAdmitted(state) && state?.generation === origin.generation &&
      state.suspendSerial === origin.suspendSerial && state.observedNotificationSequence === origin.observedNotificationSequence : !state);
  };
  return withScheduleOp(false, async () => {
    const generation = getSettingsGeneration(), operation = runningScheduleOpGen;
    if (!powerCurrent()) return false;
    const acceptReadback = beginSharedPowerLifecycleReadback();
    const lifecycle = await powerLifecycle.get().catch(() => null);
    if (!powerCurrent() || operation !== scheduleOpGen) return false;
    acceptReadback(lifecycle);
    if (!powerCurrent() || !lifecycle || !isSharedPowerLifecycleAdmitted(lifecycle) ||
        (origin && (lifecycle.generation !== origin.generation ||
          (lifecycle.suspendSerial !== undefined && lifecycle.suspendSerial !== origin.suspendSerial) ||
          (lifecycle.observedNotificationSequence !== undefined && lifecycle.observedNotificationSequence !== origin.observedNotificationSequence)))) return false;
    const side = await detectPowerModeReliable();
    if (!side) return false;
    const config = await loadPerformanceSchedule(), revision = getSettingsSectionRevision('performanceSchedule');
    const custom = await loadGameCustomConfig();
    if (scheduleSuspended || config.suspended || gameCustomRecovery.status !== 'ready') return false;
    const gameReady = isGameStatusCurrent(), game = gameReady ? await detectPolicyGame() : null;
    const hasDedicated = () => Object.values(gameCustomCache?.entries ?? {}).some(entry => entry.enabled !== false);
    if (!gameReady && hasDedicated()) return false;
    const key = game ? gamePolicyKey(game) : '';
    const entry = key ? custom.entries[key] : undefined;
    const dedicated = !!entry && entry.enabled !== false;
    if (!dedicated && !config.configured) return false;
    const bank = (dedicated ? continuousControlsForEntry(entry, config) : continuousControlsFromSchedule(config))[side];
    if (bank.tdpValid === false || !Number.isFinite(bank.tdp) || bank.tdp < getTdpHardwareMinimum() || bank.tdp > getTdpHardwareMaximum()) return false;
    const current = () => powerCurrent() && operation === scheduleOpGen && runningScheduleOpGen === operation &&
      generation === getSettingsGeneration() && revision !== null && getSettingsSectionRevision('performanceSchedule') === revision &&
      !scheduleSuspended && configCache?.suspended !== true &&
      powerSourceMode.value === side && gameCustomRecovery.status === 'ready' &&
      (gameReady ? isGameStatusCurrent() && (game ? isCurrentGameTarget(game) : getPolicyGame() === null) : !hasDedicated()) &&
      (() => {
        const latestEntry = key ? gameCustomCache?.entries[key] : undefined;
        const latestDedicated = !!latestEntry && latestEntry.enabled !== false;
        if (latestDedicated !== dedicated || !configCache || !dedicated && !configCache.configured) return false;
        const latest = dedicated ? latestEntry?.controls?.[side] : configCache.controls?.[side];
        return cachedTdpMaximum(side) === bank.tdp && (!latest || latest.mode === bank.mode && latest.auto.saving === bank.auto.saving);
      })();
    if (!current()) return false;
    const pendingApplication = pendingPerformanceApplication;
    if (getFloatInfo().enabled) {
      const result = await reassertContinuousFloatTdp();
      if (result === true) return current();
      // Reassert drains existing work. Only a lost cached lease may yield to
      // the saved TDP below; busy edits and live transport failures keep their
      // original owner/backoff and must not cause a second write.
      if (getFloatInfo().tdpOwnerCurrent !== false || !current()) return false;
    }
    let accepted: number | null = null;
    const applied = await setTdp(side, bank.tdp, { apply: true, save: false, source: 'schedule', maxWatts: bank.tdp, currentMaxWatts: () => cachedTdpMaximum(side),
      isCurrent: current, onAccepted: watts => { accepted = watts; } });
    const confirmed = applied && current() && accepted !== null && Number.isFinite(accepted) && accepted >= getTdpHardwareMinimum() && accepted <= bank.tdp;
    if (confirmed) await pendingApplication?.acceptTdp({ requested: bank.tdp, accepted, ok: true, proof: 'write-ack' }, current);
    return confirmed && current();
  });
}

export async function refreshPerformanceScheduleCoreMode(origin: SharedPowerWriteScope): Promise<boolean> {
  const matches = (power: SharedPowerLifecycleSnapshot | null) => {
    const scope = sharedPowerWriteScope(power);
    return !!scope && scope[0] === origin[0] && scope[1] === origin[1] && scope[2] === origin[2];
  };
  const current = () => !scheduleSuspended && matches(getSharedPowerLifecycle());
  if (!current()) return false;
  return withScheduleOp(true, async () => {
    if (!current()) return false;
    const snapshot = await getContinuousPerformanceSnapshot();
    if (scheduleSuspended || (await loadPerformanceSchedule()).suspended || !snapshot.side || !snapshot.bank || !snapshot.configured || snapshot.bank.mode !== 'auto') return false;
    const architecture = await detectCoreArchitecture();
    if (!architecture?.heterogeneous || !current()) return false;
    const acceptReadback = beginSharedPowerLifecycleReadback();
    const lifecycle = await powerLifecycle.get();
    assertScheduleOpCurrent();
    if (!current()) return false;
    acceptReadback(lifecycle);
    if (!current() || !matches(lifecycle)) return false;
    if ((await getActiveScheme()).toLowerCase() !== PW.YEMAN.toLowerCase()) return false;
    const latest = await getContinuousPerformanceSnapshot();
    assertScheduleOpCurrent();
    if (!current() || !currentPerformanceTarget(snapshot.identity) || continuousIdentityKey(snapshot.identity) !== continuousIdentityKey(latest.identity)) return false;
    const float = getFloatInfo();
    const watts = float.enabled && float.tdpApplied > 0 ? float.tdpApplied : snapshot.bank.tdp;
    const mode = toPowerCoreMode(continuousCoreAt(snapshot.bank, watts));
    await setCoreModeForPowerScope(snapshot.side, mode, origin);
    // The delayed boot check refreshes core policy only. Reapplying the whole
    // bank here resets the live float experiment and replays TDP/CPU writes.
    if (!current()) return false;
    const activeScheme = await getActiveScheme();
    assertScheduleOpCurrent();
    if (!current()) return false;
    if (activeScheme.toLowerCase() === PW.YEMAN.toLowerCase()) await setActiveSchemeForPowerScope(PW.YEMAN, origin);
    return current();
  }, false).catch(() => false);
}

/** Controller repeats use the panel/mirror CAS path. Each admitted delta is saved
 * in the current bank; the old mode-switch cooldown must not drop held-button steps. */
interface ScopedPerformanceEdit {
  admit(): boolean;
  read<T>(work: () => Promise<T>): Promise<T>;
  confirm(): Promise<void>;
}

export async function adjustContinuousPerformanceTdp(delta: number, edit?: ScopedPerformanceEdit): Promise<{
  applied: boolean; side: PowerSide; tdp?: number; blocked?: 'disabled' | 'cooldown' | 'boundary';
}> {
  if (!Number.isInteger(delta) || delta === 0 || Math.abs(delta) > 20) throw new Error('TDP 调节步长无效');
  // Do not wrap this in withScheduleOp: updateContinuousPerformanceControls
  // owns that queue. Nesting an operation here waits on its own unreleased ticket.
  const snapshot = edit
    ? await edit.read(() => getContinuousPerformanceSnapshot('effective', undefined, undefined, 'read-only'))
    : await getContinuousPerformanceSnapshot('effective');
  if (edit && !edit.admit()) throw new SkipApplyError();
  const side = snapshot.side;
  if (!side) throw new SkipApplyError();
  if (!snapshot.bank || snapshot.bank.tdpValid === false) return { applied: false, side, blocked: 'disabled' };
  const upper = Math.min(snapshot.hardwareMaximum, snapshot.bank.tdpCeiling);
  const next = Math.max(snapshot.hardwareMinimum, Math.min(upper, snapshot.bank.tdp + delta));
  if (next === snapshot.bank.tdp) return { applied: false, side, blocked: 'boundary' };
  const updated = await updateContinuousPerformanceControls(snapshot.identity, { tdp: next }, edit?.admit, edit);
  const proof = updated.tdpWriteReceipt;
  return { applied: proof?.ok === true && proof.proof === 'write-ack' && proof.requested === next && proof.accepted === next,
    side, tdp: updated.bank?.tdp };
}

/** Compatibility/current-mode actions still save through the sole effective owner. */
export async function setContinuousPerformanceTdp(watts: number): Promise<{ applied: boolean; side: PowerSide; tdp?: number }> {
  if (!Number.isFinite(watts) || watts < 2 || watts > 150) throw new Error('TDP 目标无效');
  const snapshot = await getContinuousPerformanceSnapshot('effective');
  if (!snapshot.side || !snapshot.bank) throw new SkipApplyError();
  if (watts < snapshot.hardwareMinimum) throw new Error('所选 TDP 低于设备可写下限');
  const next = Math.min(snapshot.hardwareMaximum, snapshot.bank.tdpCeiling, watts);
  const updated = await updateContinuousPerformanceControls(snapshot.identity, { tdp: next });
  const proof = updated.tdpWriteReceipt;
  return { applied: proof?.ok === true && proof.proof === 'write-ack' && proof.requested === next && proof.accepted === next,
    side: snapshot.side, tdp: updated.bank?.tdp };
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
  // Legacy disk compatibility only: game input overrides are no longer applied.
  // Input ownership belongs exclusively to global settings and the four boot modes.
  // 追加在末尾，缺失的旧条目回落 'follow'，因此旧配置文件完全兼容。
  padPersona?: 'follow' | 'disabled' | 'steamdeck' | 'dualsense-edge' | 'elite';
  // 2026-09-30 用户裁决：陀螺仪下拉直接用陀螺仪页的四个预设——选预设 = 启用陀螺仪
  // 并展开该预设参数。'on' 为旧档值（下拉不再提供，仅在当前生效时如实显示）。
  gyroOverride?: 'follow' | 'off' | 'on' | 'fps' | 'racing' | 'custom' | 'steam';
  // Remember the original preset while gyroOverride is off; same EXE record, no sidebar store.
  gyroPreset?: 'fps' | 'racing' | 'custom' | 'steam';
  /** Legacy input data retained for compatibility, never applied or edited. */
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
  /** Global opt-in; absent/legacy configurations remain manual. */
  autoMemory?: boolean;
  version: 1;
  entries: Record<string, GameCustomProfile>;
  // Independent RTSS-only linkage. This never activates the full custom
  // TDP/CPU/core/automatic-schedule profile.
  rtss: Record<string, GameCustomRtssProfile>;
  // 旧版覆写本机设置留下的恢复快照；新版本仅用于一次性迁移，不再创建。
  inputRestore?: GameInputRestoreSnapshot;
}

import { SETTINGS_FILE } from './settingsRepository';
import { snapshotSettingsData } from './settingsSnapshot';
function gameCustomPath(): string { return SETTINGS_FILE.replace(/yeman-settings\.json$/i, 'game-custom.json'); }
const GAME_CUSTOM_MAX_BYTES = 4 * 1024 * 1024;

function defaultGameCustomConfig(): GameCustomConfig {
  return { version: 1, entries: {}, rtss: {}, autoMemory: false };
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
  const next = { ...raw, version: 1, entries, rtss, autoMemory: raw.autoMemory === true } as GameCustomConfig;
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
const gameCustomDisplaySources=new WeakMap<GameCustomConfig,GameCustomConfig>();
function copyGameCustomRead(source:GameCustomConfig,access:'ensure'|'read-only'):GameCustomConfig {
  const copy=structuredClone(source);
  if(access==='read-only')gameCustomDisplaySources.set(copy,source);
  return copy;
}
// Owners replace the normalized cache rather than editing it after publication.
// Only a detached read from the exact current source can skip the comparison.
export function gameCustomDisplayMatches(document:GameCustomConfig): boolean {
  const current=gameCustomCacheGeneration===getSettingsGeneration()?gameCustomCache:null;
  const source=gameCustomDisplaySources.get(document);
  if(source && source===current)return true;
  // A different read can replace the cache with equivalent contents. Keep the
  // original full-document check for that case and for untracked copies.
  return JSON.stringify(current??defaultGameCustomConfig())===JSON.stringify(source??document);
}
async function loadGameCustomConfigUnlocked(operation:GameCustomOperation = {
  generation:getSettingsGeneration(),path:gameCustomPath(),active:true,
}, access:'ensure'|'read-only'='ensure',onTiming?:GameCustomReadTiming):Promise<GameCustomConfig> {
  let source:GameCustomRecoveryState['source']='defaults';
  let reason:GameCustomRecoveryState['reason']='read-failed';
  try {
    const mainStarted=onTiming?Date.now():0;
    let standalone:GameCustomDocument;
    try{standalone=await readStandaloneGameCustom(operation.path,operation);}
    finally{if(onTiming)finishGameCustomReadTiming(onTiming,'main',mainStarted);}
    operation.baseline=standalone.raw;
    const backupStarted=onTiming?Date.now():0;
    let backup:GameCustomDocument;
    try{backup=await readStandaloneGameCustom(operation.path+'.bak',operation);}
    finally{if(onTiming)finishGameCustomReadTiming(onTiming,'backup',backupStarted);}
    if(access==='read-only')checkGameCustom(operation);
    let next=standalone.value || backup.value;
    source=standalone.value?'main':backup.value?'backup':'defaults';
    if(!next){
      if(standalone.raw!==null || backup.raw!==null){
        reason='corrupt';next=defaultGameCustomConfig();
      }else{
        next=normalizeGameCustom(await readSettingsSection('gameCustom'));checkGameCustom(operation);
        // A Decky display may observe legacy data without creating a file or
        // clearing the legacy section. Ordinary YMCC/action loads still migrate.
        if(access==='ensure'){
          reason='write-failed';await writeStandaloneGameCustom(next,operation);
          await replaceSettingsSection('gameCustom',defaultGameCustomConfig() as any);checkGameCustom(operation);
        }
      }
    }
    gameCustomCache=next;gameCustomCacheGeneration=operation.generation;
    if (reason !== 'corrupt' && gameCustomRecovery.status !== 'ready') invalidateContinuousPerformanceApplied();
    gameCustomRecovery={status:reason==='corrupt'?'runtime-fallback':'ready',reason:reason==='corrupt'?'corrupt':null,source,persisted:!!standalone.value || (access==='ensure'&&standalone.raw===null&&backup.raw===null),message:reason==='corrupt'?'游戏专属配置损坏，使用空配置；有效编辑保存时将保留原件并修复':''};
    return copyGameCustomRead(next,access);
  }catch(error){
    checkGameCustom(operation);
    gameCustomCache=defaultGameCustomConfig();gameCustomCacheGeneration=operation.generation;
    gameCustomRecovery={status:'runtime-fallback',reason:error instanceof FutureGameCustomError?'unsupported':reason,source:'defaults',persisted:false,message:String((error as Error)?.message || error)};
    return copyGameCustomRead(gameCustomCache,access);
  }
}
let gameCustomWriteQueue:Promise<void>=Promise.resolve();
interface GameCustomReadResult { value:GameCustomConfig; recovery:GameCustomRecoveryState; }
interface GameCustomReadSettlement { recovery?:GameCustomRecoveryState; }
export type GameCustomDisplayReadStage = 'queue' | 'settings' | 'main' | 'backup';
type GameCustomReadTiming = (stage:GameCustomDisplayReadStage,elapsedMs:number)=>void;
function finishGameCustomReadTiming(onTiming:GameCustomReadTiming,stage:GameCustomDisplayReadStage,started:number):void{
  try{onTiming(stage,Math.max(0,Date.now()-started));}catch{/* Diagnostics do not change the owner result. */}
}
let gameCustomLoad:Promise<GameCustomReadResult>|null=null;
let gameCustomLoadGeneration=-1;
async function runGameCustomOperation<T>(generation:number,action:(operation:GameCustomOperation)=>Promise<T>,loading=false,readSettlement?:GameCustomReadSettlement,onTiming?:GameCustomReadTiming,verifyDisplay?:()=>void,access:'ensure'|'read-only'='ensure'):Promise<T>{
  assertSettingsGeneration(generation);
  // Resolve the native-owned root before any standalone I/O; never guess a root
  // after discovery/read failure, and keep migration separate from root policy.
  const started=onTiming?Date.now():0;
  try{
    if(access==='read-only')await validateSettingsRead();
    else await loadSettings();
    assertSettingsGeneration(generation);
  }
  finally{if(onTiming)finishGameCustomReadTiming(onTiming,'settings',started);}
  verifyDisplay?.();
  const operation:GameCustomOperation={generation,path:gameCustomPath(),active:true,verify:verifyDisplay};
  let timer:ReturnType<typeof setTimeout>|undefined;
  try{const result=await Promise.race([action(operation),new Promise<never>((_,reject)=>{
    timer=setTimeout(()=>reject(new Error('游戏专属配置操作超时')),GAME_CUSTOM_OPERATION_TIMEOUT_MS);
  })]);
    if(readSettlement)readSettlement.recovery={...gameCustomRecovery};
    return result;
  }catch(error){
    operation.active=false;assertSettingsGeneration(generation);
    // A retired Decky read is not a failed configuration or recovery source.
    verifyDisplay?.();
    gameCustomRecovery={status:'runtime-fallback',reason:error instanceof FutureGameCustomError?'unsupported':loading?'read-failed':'write-failed',source:'defaults',persisted:false,message:String((error as Error)?.message || error)};
    if(loading){gameCustomCache=defaultGameCustomConfig();gameCustomCacheGeneration=generation;
      if(readSettlement)readSettlement.recovery={...gameCustomRecovery};
      return copyGameCustomRead(gameCustomCache,access) as T;}
    throw error;
  }finally{operation.active=false;if(timer!==undefined)clearTimeout(timer);}
}
export async function loadGameCustomConfig(access:'ensure'|'read-only'='ensure',options:{requireReady?:boolean;onTiming?:GameCustomReadTiming;displayCurrent?:()=>boolean}={}):Promise<GameCustomConfig>{
  const generation=getSettingsGeneration(),requireReady=options.requireReady===true;
  const onTiming=access==='read-only'?options.onTiming:undefined;
  // Every coalesced read retains its own outcome. A later healthy read must
  // never promote an earlier display fallback into a runtime configuration.
  const read=async(verifyDisplay?:()=>void):Promise<GameCustomReadResult>=>{
    const settled:GameCustomReadSettlement={};
    const value=await runGameCustomOperation(generation,operation=>loadGameCustomConfigUnlocked(operation,access,onTiming),true,settled,onTiming,verifyDisplay,access);
    return {value,recovery:settled.recovery!};
  };
  const consume=(result:GameCustomReadResult)=>{
    if(requireReady&&result.recovery.status!=='ready')throw new Error('游戏专属配置尚未完成可靠读取');
    // A read-only run has one consumer and already returned a detached copy.
    // Coalesced ensure callers still need separate copies of the shared result.
    return access==='read-only'?result.value:structuredClone(result.value);
  };
  if(access==='read-only'){
    // Keep the original file/CAS queue and deadlines. Never coalesce a display
    // with an ensure load, whose migration is an ordinary owner's operation.
    const queued=onTiming?Date.now():0,displayCurrent=options.displayCurrent;
    const verifyDisplay=displayCurrent?()=>{if(!displayCurrent())throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');}:undefined;
    const run=gameCustomWriteQueue.then(()=>{
      if(onTiming)finishGameCustomReadTiming(onTiming,'queue',queued);
      // Keep the display lease through settings and each file boundary, not
      // just the queue. Already-dispatched reads settle on their original deadline.
      verifyDisplay?.();
      return read(verifyDisplay);
    });
    gameCustomWriteQueue=run.then(()=>{},()=>{});
    return consume(await run);
  }
  if(!gameCustomLoad || gameCustomLoadGeneration!==generation){
    const run=gameCustomWriteQueue.then(()=>read());
    gameCustomWriteQueue=run.then(()=>{},()=>{});gameCustomLoad=run;gameCustomLoadGeneration=generation;
  }
  const task=gameCustomLoad;
  try{return consume(await task);}finally{if(gameCustomLoad===task)gameCustomLoad=null;}
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

/** Persist only the global opt-in, keeping every game's settings under its owner queue. */
export async function setGameCustomAutoMemory(enabled: boolean, generation = getSettingsGeneration()): Promise<void> {
  if (typeof enabled !== 'boolean') throw new Error('专属自动记忆状态无效');
  const verify = () => assertSettingsGeneration(generation);
  verify();
  await mutateContinuousGameConfig(generation, latest => { latest.autoMemory = enabled; }, verify);
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
  const allowed = new Set(['enabled']);
  if (Object.keys(patch).some(key => !allowed.has(key))) throw new Error('游戏专属不再支持手柄和陀螺仪配置');
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
  delete seed.padPersona; delete seed.gyroOverride; delete seed.gyroPreset; delete seed.inputOverrides;
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

/** Shared manual/automatic creation: copy performance and FPS, never input settings. */
export async function createGameCustomProfile(
  expected: GameCustomProfileTarget, displayName: string,
  options: { automatic?: boolean; admit?: () => boolean } = {},
): Promise<boolean> {
  expected = { ...expected };
  const verify = () => {
    assertSettingsGeneration(expected.generation);
    if (!currentPerformanceTarget(expected) || options.admit?.() === false) throw new SkipApplyError();
    const game = getPolicyGame();
    if (!game || !expected.exe || gamePolicyKey(game) !== expected.exe) throw new SkipApplyError();
  };
  verify();
  const current = await loadGameCustomConfig('ensure', { requireReady: true });
  verify();
  if (current.entries[expected.exe]) return false;
  if (options.automatic && current.autoMemory !== true) throw new SkipApplyError();
  const schedule = await loadPerformanceSchedule();
  verify();
  const source = await getContinuousPerformanceSnapshot('global');
  verify();
  if (!currentPerformanceTarget(source.identity) || source.identity.exe !== expected.exe ||
      source.identity.pid !== expected.pid || source.identity.processCreated !== expected.processCreated)
    throw new SkipApplyError();
  const profile: GameCustomProfile = {
    displayName, enabled: true, controls: structuredClone(source.controls),
    ac: structuredClone(schedule.profiles.ac[schedule.active.ac]),
    dc: structuredClone(schedule.profiles.dc[schedule.active.dc]),
    hyperThreadPolicyEnabled: source.hyperThreadMode !== 'default', hyperThreadPolicy: source.hyperThreadMode,
  };
  // The ordinary file queue rechecks absence and the opt-in at the real commit.
  // Concurrent manual adds/removals must never be replaced by a late auto-create.
  await mutateContinuousGameConfig(expected.generation, latest => {
    verify();
    if (latest.entries[expected.exe]) throw new SkipApplyError();
    if (options.automatic && latest.autoMemory !== true) throw new SkipApplyError();
    latest.entries[expected.exe] = profile;
    if (!latest.rtss[expected.exe]) latest.rtss[expected.exe] = dedicatedFrameRateRecord(source.frameRates);
  }, verify);
  return true;
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

// Optional Decky observation only: promise-settlement waits, including any owner
// queue/native round trip. Parallel durations overlap and are not additive.
export type ContinuousPerformanceReadStage = 'performanceSchedule'|'performanceGameConfig'|'performancePowerMode'|'performanceGame'|'performanceCore'|'performanceTdpCapability'|'performanceCpuProfiles'|'performanceFrameRates';
export async function getContinuousPerformanceSnapshot(scope: 'effective' | 'global' | 'dedicated' = 'effective', editSide?: PowerSide,
  onTiming?: (stage:ContinuousPerformanceReadStage,elapsedMs:number)=>void,
  frameRateAccess: 'ensure' | 'read-only' = 'ensure',
  displayReads?: {generation:number;schedule?:PerformanceScheduleConfig|Promise<PerformanceScheduleConfig>;custom?:GameCustomConfig|Promise<GameCustomConfig>;globalFrameRates:Promise<FrameRatePair>},
  wake?: WakePerformanceAttempt, gameReadMode: 'default' | 'observe' = 'default'): Promise<ContinuousPerformanceSnapshot> {

  const observe=<T>(stage:ContinuousPerformanceReadStage,task:Promise<T>):Promise<T>=>{
    if(!onTiming)return task;
    const started=Date.now();
    const settled=()=>{try{onTiming(stage,Math.max(0,Date.now()-started));}catch{/* A diagnostic cannot change the owner receipt. */}};
    return task.then(value=>{settled();return value;},error=>{settled();throw error;});
  };
  const generation = getSettingsGeneration();
  // Only this internal read-only display may share its already-read original
  // config/FPS. Commands ignore these inputs and keep their fresh owner reads.
  const display=frameRateAccess === 'read-only' ? displayReads : undefined;
  if(display)assertSettingsGeneration(display.generation);
  // The same display may share an in-flight owner read. Plain data is copied
  // immediately; promised data is copied at settlement, never cached or reused
  // by a command. This keeps independent device readers parallel with file IO.
  const displayRead=<T>(value:T|Promise<T>):Promise<T> => typeof (value as Promise<T>).then==='function'
    ? Promise.resolve(value).then(snapshot=>structuredClone(snapshot)) : Promise.resolve(structuredClone(value as T));
  const globalRead=observe('performanceSchedule',display?.schedule ? displayRead(display.schedule) : loadPerformanceSchedule());
  const customRead=observe('performanceGameConfig',display?.custom ? displayRead(display.custom) : loadGameCustomConfig(frameRateAccess));
  const gameRead=observe('performanceGame',readWakePerformance(wake, () => detectPolicyGame(wake ? 'default' : gameReadMode)));
  // Modern Decky controls already contain CPU values. Read legacy profiles only
  // when the selected original conversion actually consumes them; never cache.
  // Ordinary YMCC/action reads retain their original eager CPU-owner read.
  const cpuRead=frameRateAccess==='read-only'
    ? Promise.all([globalRead,customRead,gameRead]).then(([config,custom,game])=>{
      const entry=game?custom.entries[gamePolicyKey(game)]:undefined;
      const dedicated=scope==='dedicated'||scope==='effective'&&!!entry&&entry.enabled!==false;
      const needed=dedicated&&entry ? !entry.controls : !config.controls&&config.configured;
      return observe('performanceCpuProfiles',needed?loadCpuProfiles():Promise.resolve(undefined));
    })
    : observe('performanceCpuProfiles',loadCpuProfiles());
  const reads = [
    globalRead, customRead, observe('performancePowerMode',readWakePerformance(wake, () => detectPowerModeReliable())), gameRead,
    observe('performanceCore',readWakePerformance(wake, detectGameCorePolicy)),
    observe('performanceTdpCapability',readWakePerformance(wake, () => getTdpExecutionCapability().catch(() => null))), cpuRead,
  ] as const;
  const [global, custom, actualSide, game, architecture, capability, legacyCpu] = await Promise.all(reads).catch(async error => {
    // Config owners may already be repairing persistent data. Let them finish.
    if (wake) await Promise.allSettled(reads);
    throw error;
  });
  wake?.check();
  assertSettingsGeneration(generation);
  const exe = game ? gamePolicyKey(game) : '', entry = exe ? custom.entries[exe] : undefined;
  const owner = scope === 'effective' ? entry && entry.enabled !== false ? 'dedicated' : 'global' : scope;
  const controls = owner === 'dedicated' ? continuousControlsForEntry(entry, global, legacyCpu) : continuousControlsFromSchedule(global, legacyCpu);
  const side = editSide ?? actualSide;
  const bank = side ? controls[side] : null;
  // Decky display must not join the FPS writer queue or persist a legacy
  // migration. Explicit actions retain the original ensure/save boundary.
  const globalFrameRates = await observe('performanceFrameRates',frameRateAccess === 'read-only' ? display?.globalFrameRates ?? loadGlobalFrameRates() : ensureGlobalFrameRates());
  wake?.check();
  if(display)assertSettingsGeneration(display.generation);
  const frameRates = owner === 'dedicated' ? dedicatedFrameRatePair(custom.rtss[exe], globalFrameRates) : globalFrameRates;
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

export async function updateContinuousPerformanceControls(expected: ContinuousPerformanceIdentity, patch: ContinuousSidePatch, admit?: () => boolean,
  edit?: ScopedPerformanceEdit): Promise<ContinuousPerformanceSnapshot> {
  expected = { ...expected };
  const read = <T>(work: () => Promise<T>) => edit ? edit.read(work) : work();
  const snapshot = () => getContinuousPerformanceSnapshot(expected.followEffective ? 'effective' : expected.scope, expected.side ?? undefined,
    undefined, edit ? 'read-only' : 'ensure');
  const smtPowerScope = patch.hyperThreadMode === undefined ? null : sharedPowerWriteScope(getSharedPowerLifecycle());
  return withScheduleOp(false, async () => {
    if (!currentPerformanceTarget(expected) || (admit && !admit())) throw new SkipApplyError();
    const current = await read(snapshot);
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
        const before = performancePreferenceSnapshot(latest, settings.rtss.frameRates);
        latest.powerSchemes = { ...pair, [expected.side!]: patch.powerScheme! };
        settings.performanceSchedule = latest as any;
        rememberGlobalPerformance(settings, before, latest);
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
    if (patch.tdp !== undefined && patch.tdp < current.hardwareMinimum ||
        patch.tdpCeiling !== undefined && patch.tdpCeiling < current.hardwareMinimum)
      throw new Error('所选 TDP 低于设备可写下限，不会擅自提高瓦数');
    const side = expected.side, bank = patchContinuousBank(current.controls[side], patch, side, current.powerScheme);
    bank.tdpCeiling = Math.max(current.hardwareMinimum, Math.min(current.hardwareMaximum, bank.tdpCeiling));
    bank.tdp = Math.min(bank.tdpCeiling, bank.tdp);
    if (bank.tdp < current.hardwareMinimum) bank.tdpValid = false;
    bank.auto.coreSplit = Math.min(bank.auto.coreSplit, bank.tdpCeiling);
    const master = await read(() => loadPerformanceSchedule());
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
    const legacyCpu = await read(() => loadCpuProfiles());
    const custom = await read(() => loadGameCustomConfig(edit ? 'read-only' : 'ensure', { requireReady: !!edit }));
    const entry = expected.exe ? custom.entries[expected.exe] : undefined;
    const globalSmt = expected.scope === 'global' || expected.followEffective === true;
    if (patch.hyperThreadMode !== undefined && (!current.smtAvailable || (!globalSmt && !expected.exe) || !['default', 'on', 'off'].includes(patch.hyperThreadMode))) throw new Error('当前游戏或处理器不支持超线程策略');
    assertScheduleOpCurrent();
    if ((await read(() => detectPowerModeReliable())) !== (expected.actualSide ?? side) || getSettingsGeneration() !== expected.generation) throw new SkipApplyError();
    if (edit) { await edit.confirm(); assertScheduleOpCurrent(); verifyContext(); }
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
        const prior = continuousControlsFromSchedule(latest, legacyCpu);
        latest.controls = prior;
        const before = performancePreferenceSnapshot(latest, settings.rtss.templateDefaults === true ? current.frameRates : settings.rtss.frameRates);
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
        rememberGlobalPerformance(settings, before, latest);
      });
      if (performanceChanged) emitPerformanceSchedule(await read(() => loadPerformanceSchedule()));
    }
    if (patch.hyperThreadMode !== undefined) {
      const mode = normalizeHyperThreadMode(patch.hyperThreadMode);
      if (globalSmt) {
        await updateSettings(settings => {
          verifyContext();
          const latest = normalizeConfig(settings.performanceSchedule, current.controls);
          if (normalizeHyperThreadMode(latest.hyperThreadPolicy) !== current.hyperThreadMode) throw new Error('全局超线程设置已变化，请重新读取');
          const before = performancePreferenceSnapshot(latest, settings.rtss.frameRates);
          latest.hyperThreadPolicy = mode;
          settings.performanceSchedule = latest as any;
          rememberGlobalPerformance(settings, before, latest);
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
        assertScheduleOpCurrent();
        verifyContext();
        const currentPower = sharedPowerWriteScope(getSharedPowerLifecycle());
        if (!isPolicyTargetCurrent(expected) || !smtPowerScope || !currentPower ||
            smtPowerScope.some((value, index) => value !== currentPower[index])) throw new SkipApplyError();
        if (mode === 'off') {
          const applied = await applyGameCorePolicy(expected, 'default', mode, smtPowerScope);
          if (!applied.ok || !applied.applied) throw new Error(applied.error || '超线程策略未应用');
        } else if (!await clearGameCorePolicy(smtPowerScope, expected)) throw new Error('恢复游戏默认超线程失败');
      }
    }
    assertScheduleOpCurrent();
    const latestCustom = await read(() => loadGameCustomConfig(edit ? 'read-only' : 'ensure', { requireReady: !!edit })), activeEntry = expected.exe ? latestCustom.entries[expected.exe] : undefined;
    let tdpWriteReceipt: ContinuousPerformanceSnapshot['tdpWriteReceipt'];
    const owns = expected.scope === 'dedicated' ? activeEntry?.enabled !== false : !activeEntry || activeEntry.enabled === false;
    if (resumeRequested) {
      if (expected.scope === 'dedicated') await savePerformanceSchedule({ ...(await loadPerformanceSchedule()), suspended: false, enabled: true }, { origin: 'runtime' });
      scheduleSuspended = false;
    }
    if (owns && performanceChanged && side === (expected.actualSide ?? side) && !scheduleSuspended && !(await read(() => loadPerformanceSchedule())).suspended) {
      if (edit) { await edit.confirm(); assertScheduleOpCurrent(); verifyContext(); }
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
      // The setting is already committed. A retired action must not start a new runtime apply.
      frameRateApplied = (!admit || admit()) && await applyIndependentFrameRates(admit);
    }
    const updated = await read(snapshot);
    return { ...updated, ...(patch.frameRate ? { frameRateApplied } : {}), ...(tdpWriteReceipt ? { tdpWriteReceipt } : {}) };
    } finally { tdpEdit?.release(); }
  }, !edit);
}

async function applyContinuousBankUnsafe(side: PowerSide, requested: ContinuousSideControls,
  expectedTarget?: PerformanceScheduleTarget, frameRates?: FrameRatePair, onTdpWrite?: (receipt: NonNullable<ContinuousPerformanceSnapshot['tdpWriteReceipt']>) => void, admit?: () => boolean, completedStartupIntent?: () => string,
  onCompleted?: (application?: ContinuousPerformanceApplication) => void, startup?: ContinuousPerformanceApplicationContext,
  wake?: WakePerformanceAttempt): Promise<boolean> {
  const bank = normalizeContinuousBank(requested, side), generation = getSettingsGeneration(), operation = runningScheduleOpGen;
  const bankPower = sharedPowerWriteScope(getSharedPowerLifecycle());
  const game = await readWakePerformance(wake, () => detectPolicyGame()), gameKey = game ? gamePolicyKey(game) : '';
  const tdpDataReady = () => bank.tdpValid !== false && (!game || gameCustomRecovery.status === 'ready');
  const selectedScheme = normalizeControlPowerSchemes((await loadPerformanceSchedule()).powerSchemes)[side];
  let tdpCheckpointRevision: number | null = null;
  // Profile commits can precede the queued restore. Check the cached owner at
  // final dispatch and receipt, without rereading files or rebuilding controls.
  const cachedTdpOwnerCurrent = () => {
    if (tdpCheckpointRevision === null || getSettingsSectionRevision('performanceSchedule') !== tdpCheckpointRevision) return false;
    if (game && gameCustomRecovery.status !== 'ready') return false;
    const entry = gameKey ? gameCustomCache?.entries[gameKey] : undefined;
    if (!expectedTarget) {
      if (entry && entry.enabled !== false || !configCache || configCache.suspended || !configCache.configured) return false;
      const cached = configCache.controls?.[side];
      return cachedTdpMaximum(side) === bank.tdp && (!cached || cached.mode === bank.mode && cached.auto.saving === bank.auto.saving);
    }
    if (!entry || entry.enabled === false) return false;
    const cached = entry.controls?.[side];
    return cached ? cached.tdpValid !== false && cached.tdp === bank.tdp && cached.mode === bank.mode && cached.auto.saving === bank.auto.saving :
      entry[side].tdpValid !== false && Math.min(150, entry[side].tdpMax) === bank.tdp && entry[side].tdpStrategy === bank.auto.saving;
  };
  const cachedCpuPolicyCurrent = () => {
    const entry = gameKey ? gameCustomCache?.entries[gameKey] : undefined;
    if (expectedTarget ? !entry || entry.enabled === false : entry && entry.enabled !== false) return false;
    const current = expectedTarget ? entry?.controls?.[side] : configCache?.controls?.[side];
    if (!current) return true; // Legacy profiles were reconciled before admission.
    return current.cpuValid !== false && current.mode === bank.mode && (bank.mode === 'auto' ? current.auto.bias === bank.auto.bias && current.auto.saving === bank.auto.saving :
      current.manual.frequencyMHz === bank.manual.frequencyMHz && current.manual.unlimited === bank.manual.unlimited &&
      current.manual.aggressiveness === bank.manual.aggressiveness && current.manual.turbo === bank.manual.turbo);
  };
  const cachedCpuOwnerCurrent = () => operation === scheduleOpGen && runningScheduleOpGen === operation && !scheduleSuspended &&
    generation === getSettingsGeneration() && admit?.() !== false && isGameStatusCurrent() &&
    (powerSourceMode.value === null || powerSourceMode.value === side) &&
    (game ? isCurrentGameTarget(game) : getPolicyGame() === null) && cachedCpuPolicyCurrent();
  let fingerprint = JSON.stringify(bank);
  let receiptConfig: PerformanceScheduleConfig | null = null;
  let receiptStartup: ContinuousPerformanceApplicationContext | undefined;
  let startupCaptured = false, startupStillCurrent = true, startupUsesClaim = false;
  const rememberedStartup = startupApplicationContext;
  let receiptFrames: FrameRatePair | undefined = frameRates;
  let tdpApplication: ContinuousPerformanceApplication['tdp'] | undefined, cpuConfirmed = false;
  const appliedSignature = (): string => {
    if (!receiptConfig || !receiptFrames) return '';
    const owner = expectedTarget ? ['dedicated', gameKey] : ['global', receiptConfig.configured, receiptConfig.enabled];
    const identity = game ? JSON.stringify([game.pid, game.processCreated, game.path.toLowerCase(), game.identityGeneration ?? 0]) : '';
    return performanceConsumptionSignature(owner, identity, side, receiptFrames[side].fps, bank, normalizeControlPowerSchemes(receiptConfig.powerSchemes)[side]);
  };
  const notifyApplied = () => {
    if (!tdpApplication) return;
    const application: ContinuousPerformanceApplication = { tdp: { ...tdpApplication },
      cpu: systemManaged ? 'windows-managed' : cpuConfirmed ? 'applied' : 'unconfirmed' };
    onCompleted?.(structuredClone(application));
    // Optional TDP failure must not stop independent CPU work, nor acknowledge
    // an all-bank completion that the startup/resident consumers will reuse.
    if (!application.tdp.ok || application.cpu === 'unconfirmed') { invalidateContinuousPerformanceApplied(); return; }
    const signature = appliedSignature();
    if (signature) {
      rememberedManualIntent = bank.mode === 'manual' && bank.cpuValid !== false && bank.tdpValid !== false ? signature : '';
      publishContinuousPerformanceApplied(signature, startupStillCurrent ? receiptStartup : undefined);
    }
  };
  const finishApplication = (current: boolean) => {
    if (!current) return false;
    notifyApplied();
    if (tdpApplication && !tdpApplication.ok && (systemManaged || cpuConfirmed)) {
      const revision = getSettingsSectionRevision('performanceSchedule'), signature = appliedSignature();
      const pending: NonNullable<typeof pendingPerformanceApplication> = { async acceptTdp(receipt: ContinuousPerformanceApplication['tdp'], current: () => boolean): Promise<void> {
        const owns = () => pendingPerformanceApplication === pending && current() && revision !== null &&
          revision === getSettingsSectionRevision('performanceSchedule') && generation === getSettingsGeneration();
        if (!receipt.ok || !owns() || !await checkpoint() || !owns()) return;
        const globalFrames = await loadGlobalFrameRates();
        const latestFrames = expectedTarget ? dedicatedFrameRatePair(gameCustomCache?.rtss[gameKey], globalFrames) : globalFrames;
        if (!owns() || latestFrames[side].fps !== receiptFrames?.[side].fps || appliedSignature() !== signature) return;
        tdpApplication = { ...receipt };
        notifyApplied();
      } };
      pendingPerformanceApplication = pending;
    }
    return true;
  };
  const checkpoint = async () => {
    if (!isGameStatusCurrent() || scheduleSuspended || (admit && !admit())) return false;
    if (generation !== getSettingsGeneration() || (await readWakePerformance(wake, () => detectPowerModeReliable())) !== side) return false;
    if (game ? !isCurrentGameTarget(game) : getPolicyGame() !== null) return false;
    const now = getPolicyGame();
    if ((now?.pid || 0) !== (game?.pid || 0) || String(now?.processCreated || '') !== String(game?.processCreated || '')) return false;
    if (expectedTarget && (!isPolicyTargetCurrent(expectedTarget) || !currentPerformanceTarget(expectedTarget))) return false;
    const custom = await loadGameCustomConfig(), entry = gameKey ? custom.entries[gameKey] : undefined;
    const config = await loadPerformanceSchedule((context, settings) => {
      tdpCheckpointRevision = getSettingsSectionRevision('performanceSchedule');
      const claim = bootStartupReceiptOwner(settings);
      const matchesClaim = (candidate?: ContinuousPerformanceApplicationContext) => !!candidate && candidate.generation === generation &&
        settings.extensions?.bootModes?.schema === 1 && (settings.extensions?.bootModesStartupResult?.schema ?? 1) === 1 &&
        !!claim && candidate.boot === claim.boot && candidate.writeId === claim.writeId &&
        candidate.mode === settings.extensions?.bootModes?.current && Number.isSafeInteger(settings.extensions?.bootModes?.revision) &&
        settings.extensions!.bootModes.revision >= candidate.revision;
      if (!startupCaptured) {
        const remembered = matchesClaim(rememberedStartup) && (!context || context.sourceRevision <= rememberedStartup!.sourceRevision && context.revision <= rememberedStartup!.revision) ? rememberedStartup : undefined;
        receiptStartup = startup ?? remembered ?? context; startupUsesClaim = !!startup || !!remembered; startupCaptured = true;
      }
      if (startupUsesClaim) {
        if (!matchesClaim(receiptStartup) || context && (context.boot !== receiptStartup!.boot || context.mode !== receiptStartup!.mode ||
            context.generation !== receiptStartup!.generation || context.writeId !== receiptStartup!.writeId ||
            context.sourceRevision > receiptStartup!.sourceRevision || context.revision > receiptStartup!.revision)) startupStillCurrent = false;
      } else if (JSON.stringify(context) !== JSON.stringify(receiptStartup)) startupStillCurrent = false;
    });
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
  // Automatic startup may reuse its receipt; remembered manual broadcasts may
  // reuse the same completed intent. A committed wake omits this receipt.
  const completed = completedStartupIntent?.();
  if (completed && completed === appliedSignature() && completed === completedPerformanceIntent?.signature && completedPerformanceIntent.generation === generation) {
    onCompleted?.(); // Reuse only this service's existing complete application witness.
    return true;
  }
  wake?.beginActions();
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
    await ensureRememberedYemanSchemeActive(wake && { current: wake.admitted, read: wake.read });
  }
  if (!await checkpoint()) return false;
  const watts = Math.max(getTdpHardwareMinimum(), Math.min(150, getTdpHardwareMaximum(), bank.tdp));
  let acceptedWatts: number | null = null;
  await runOptionalPerformanceScheduleTdp(side, watts, async () => {
    if (!tdpDataReady()) throw new Error('TDP 原始数值或当前游戏配置不可用，保留上次硬件设置并暂停浮动；CPU 参数仍独立应用');
    const applied = await setTdp(side, watts, { apply: true, save: true, source: 'schedule', maxWatts: bank.tdp, currentMaxWatts: () => cachedTdpMaximum(side),
      isCurrent: () => operation === scheduleOpGen && runningScheduleOpGen === operation && !scheduleSuspended && (!admit || admit()) && generation === getSettingsGeneration() &&
        isGameStatusCurrent() && (game ? isCurrentGameTarget(game) : getPolicyGame() === null) &&
        (powerSourceMode.value === null || powerSourceMode.value === side) &&
        (!expectedTarget || currentPerformanceTarget(expectedTarget)) && cachedTdpOwnerCurrent(),
      onAccepted: value => { acceptedWatts = value; } });
    if (!applied) acceptedWatts = null;
  });
  if (operation !== scheduleOpGen || runningScheduleOpGen !== operation) return false;
  tdpApplication = { requested: watts, accepted: acceptedWatts, ok: acceptedWatts !== null, proof: acceptedWatts === null ? 'unconfirmed' : 'write-ack' };
  onTdpWrite?.({ ...tdpApplication });
  if (!await checkpoint()) return false;
  if (systemManaged) {
    setFloatRtssLinked(false);
    receiptFrames ??= await ensureGlobalFrameRates();
    const current = await checkpoint();
    return finishApplication(current); // Windows owns CPU frequency/EPP/turbo/core policy.
  }
  const automatic = automaticCpuControls(bank);
  const freq = bank.mode === 'auto' ? automatic.maxMHz : bank.manual.unlimited ? 0 : bank.manual.frequencyMHz;
  const aggr = bank.mode === 'auto' ? automatic.aggressiveness : bank.manual.aggressiveness;
  const turbo = bank.mode === 'auto' ? automatic.turbo : bank.manual.turbo;
  const minState = bank.mode === 'auto' ? automatic.minState : manualCpuMinimum(bank);
  if (bank.cpuValid !== false) {
    const cpuApplied = await applyManualPowerParams({ acFreq: freq, dcFreq: freq, acAggr: aggr, dcAggr: aggr, acTurbo: turbo, dcTurbo: turbo,
      acMinState: minState, dcMinState: minState, sides: [side], restoreMaxState: false }, { maxMHz: freq, isCurrent: cachedCpuOwnerCurrent });
    if (!cpuApplied || !cachedCpuOwnerCurrent()) return false;
    cpuConfirmed = true;
  }
  if (!await checkpoint()) return false;
  await setActiveScheme(PW.YEMAN);
  const architecture = await detectCoreArchitecture();
  let coreState: ContinuousCoreState | null = null;
  let currentWatts: number | null = acceptedWatts;
  let liveCheckpoint: (() => Promise<boolean>) | null = null;
  const corePower = () => {
    const current = sharedPowerWriteScope(getSharedPowerLifecycle());
    return bankPower && current && current[0] === bankPower[0] && current[1] === bankPower[1] ? current : null;
  };
  const applyCore = async (watts: number, immediate = false, origin = corePower()) => {
    const current = immediate || !liveCheckpoint ? checkpoint : liveCheckpoint;
    const powerCurrent = () => origin !== null && corePower()?.[2] === origin[2];
    if (!architecture?.heterogeneous || !origin || !powerCurrent() || !await current() || !powerCurrent()) return;
    currentWatts = watts;
    const now = Date.now();
    const next = immediate || !coreState ? { mode: continuousCoreAt(bank, watts), changedAt: now } : stepContinuousCore(bank, coreState, watts, now);
    if (coreState?.mode === next.mode) { coreState = next; if (next.pending) holdContinuousFloatAfterCoreChange(); return; }
    const mode = toPowerCoreMode(next.mode);
    const previous = coreState;
    let confirmed = false;
    try {
      await setCoreModeForPowerScope(side, mode, origin);
      if (!powerCurrent() || !await current() || !powerCurrent()) return;
      await setActiveSchemeForPowerScope(PW.YEMAN, origin);
      if (!powerCurrent()) return;
      coreState = next;
      confirmed = true;
      if (previous) holdContinuousFloatAfterCoreChange();
    } catch (error) {
      if (powerCurrent() && !isPowerTransitionWriteBlocked(error) &&
          (error instanceof Error ? error.message : String(error)) !== 'power policy write scope expired') throw error;
    } finally {
      // The batch may have written before the activation was revoked.
      if (!confirmed && coreState === previous) coreState = null;
    }
  };
  activeCoreRule = { key: coreOwnerKey(side, game?.pid || 0, String(game?.processCreated || ''), generation, game?.path, game?.identityGeneration),
    async update(next) {
      Object.assign(bank.auto, { coreSplit: next.auto.coreSplit, coreLow: next.auto.coreLow, coreHigh: next.auto.coreHigh });
      fingerprint = JSON.stringify(bank);
      updateContinuousFloatCoreRules(bank);
      const origin = corePower();
      if (!origin) return;
      const acceptReadback = beginSharedPowerLifecycleReadback();
      const lifecycle = await powerLifecycle.get();
      const observed = sharedPowerWriteScope(lifecycle);
      acceptReadback(lifecycle);
      if (observed && observed.every((value, index) => value === origin[index]) && (await getActiveScheme()).toLowerCase() === PW.YEMAN.toLowerCase() &&
          currentWatts !== null && (!getFloatInfo().enabled || getFloatInfo().tdpApplied > 0)) await applyCore(currentWatts, true, origin);
    } };
  const bankOwner = activeCoreRule;
  if (acceptedWatts !== null) await applyCore(acceptedWatts, false, bankPower);
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
  }, { revoke: () => { residentRevoked = true; }, tdpMaximum: () => cachedTdpMaximum(side),
    isCurrent: () => residentContextCurrent() && observedRevision !== null &&
      getSettingsSectionRevision('performanceSchedule') === observedRevision && cachedTdpOwnerCurrent() && cachedCpuPolicyCurrent() });
  liveCheckpoint = residentCheckpoint;
  if (bank.cpuValid !== false && tdpDataReady() && bank.mode === 'auto' && bank.auto.saving !== 'none' && target > 0) await enableContinuousFloat({ ...bank, tdp: acceptedWatts ?? watts }, side, target, residentCheckpoint, applyCore,
    acceptedWatts === null ? undefined : { requested: watts, accepted: acceptedWatts }, receipt => {
      // Complete the missing initial TDP part once. Healthy float writes do not
      // re-read settings or republish the already completed startup witness.
      if (tdpApplication?.ok || !residentCheckpoint.isCurrent()) return;
      tdpApplication = { ...receipt, ok: true, proof: 'write-ack' };
      notifyApplied();
    });
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
          const coreOrigin = corePower();
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
            const ok = await setTdp(side, watts, { apply: true, save: true, source: 'schedule', maxWatts: bank.tdp, currentMaxWatts: () => cachedTdpMaximum(side), isCurrent: () => editCurrent() && cachedTdpOwnerCurrent(), onAccepted: value => { accepted = value; } });
            if (!ok) accepted = null;
          });
          tdpApplication = { requested: watts, accepted, ok: accepted !== null, proof: accepted === null ? 'unconfirmed' : 'write-ack' };
          receipt({ ...tdpApplication });
          if (!editCurrent() || !await checkpoint() || !editCurrent()) return false;
          currentWatts = accepted;
          paused?.accept(watts, accepted);
          if (accepted !== null) await applyCore(accepted, true, coreOrigin);
          const current = await residentCheckpoint();
          if (current) notifyApplied();
          return current;
        },
      };
    } catch (error) { paused?.release(); throw error; }
  };
  const current = await residentCheckpoint();
  return finishApplication(current);
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
