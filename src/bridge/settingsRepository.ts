import { migrateControllerPersona } from './controllerPersona';
import { fs, settingsStore } from './api';
import { snapshotSettingsData } from './settingsSnapshot';
import { globalShortcutContainer, prepareGlobalShortcutEdit, retireBootGlobalShortcuts } from './bootModeSharedShortcuts';
import { SETTINGS_BASELINE, assertSupportedSettingsBaseline, hasCompletedSettingsBaseline, stampSettingsBaseline, equalSettingsDocuments, type ConfigurationBaseline } from './settingsBaseline';
import { createDefaultQuickApps } from './quickAppDefaults';
import { normalizeInputStartupPreferences } from './inputStartupPolicy';
import { computeInputConfigHash } from './inputConfigHash';
import { HC_MIN_GYRO_THRESHOLD_DPS } from './hcInputUtils';
import type { VirtualStartProtectionState } from './virtualOutputStatus';
import type { AxisCalibrationConfig } from './controllerCalibrationContract';

/**
 * One durable user-settings document for the whole application.
 *
 * The repository deliberately owns the read/modify/write transaction.  Feature
 * modules only update their section, so a future module cannot accidentally
 * erase another module's settings or fields added by a newer build.
 */
const DEFAULT_SETTINGS_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
let settingsDir = DEFAULT_SETTINGS_DIR;
export let SETTINGS_FILE = `${settingsDir}\\yeman-settings.json`;
export let SETTINGS_BACKUP_FILE = `${SETTINGS_FILE}.bak`;

let settingsLocationResolved = false;
let settingsLocationExplicit = false;
let settingsLocationPromise: Promise<void> | null = null;

function bindSettingsDirectory(dir: string): void {
  settingsDir = dir.replace(/\//g, '\\').replace(/[\\]+$/, '');
  SETTINGS_FILE = settingsDir + '\\yeman-settings.json';
  SETTINGS_BACKUP_FILE = SETTINGS_FILE + '.bak';
}

export function setSettingsDirectory(dir: string): void {
  // Explicit test/adaptor directories supersede a late native discovery.
  settingsLocationExplicit = true;
  settingsLocationResolved = true;
  const nextDir = dir.replace(/\//g, '\\').replace(/[\\]+$/, '');
  if (nextDir.toLowerCase() === settingsDir.toLowerCase()) return;
  bindSettingsDirectory(nextDir);
  clearSettingsCache();
}

async function resolveSettingsLocation(): Promise<void> {
  if (settingsLocationResolved) return;
  if (!settingsStore.location) {
    // Legacy isolated adapters intentionally do not provide native discovery.
    settingsLocationResolved = true;
    return;
  }
  if (!settingsLocationPromise) {
    const generation = getSettingsGeneration();
    const task = (async () => {
      const location = await boundedSettingsIO(settingsStore.location(), '配置位置读取');
      assertSettingsGeneration(generation);
      if (settingsLocationExplicit) return;
      const dir = typeof location?.directory === 'string'
        ? location.directory.replace(/\//g, '\\').replace(/[\\]+$/, '') : '';
      const file = typeof location?.file === 'string' ? location.file.replace(/\//g, '\\') : '';
      if (!/^(?:[a-z]:\\|\\\\[^\\]+\\[^\\]+)/i.test(dir + '\\') ||
          file.toLowerCase() !== (dir + '\\yeman-settings.json').toLowerCase()) {
        throw new Error('无法获取有效的原生配置位置');
      }
      // Initial binding precedes ALL settings I/O; it is not a user directory
      // switch and must not invalidate import-time queued module submissions.
      bindSettingsDirectory(dir);
      settingsLocationResolved = true;
    })();
    settingsLocationPromise = task;
  }
  const task = settingsLocationPromise;
  try { await task; }
  finally { if (settingsLocationPromise === task) settingsLocationPromise = null; }
}

export type JsonObject = Record<string, any>;
export type InputApplyStatus = 'unknown' | 'saved-pending' | 'host-acknowledged' | 'active' | 'rejected' | 'safe-stop';
export type SettingsSection =
  | 'ui'
  | 'background'
  | 'music'
  | 'gamepad'
  | 'performanceSchedule'
  | 'gameCustom'
  | 'tdp'
  | 'rtss'
  | 'cpu'
  | 'sleep'
  | 'quickApps'
  | 'startupDesired'
  | 'power'
  | 'tray'
  | 'autoclose'
  | 'fan'
  | 'input';

export interface InputSettingsSnapshot extends JsonObject {
  schemaVersion: number;
  revision: number;
  appliedRevision: number | null;
  appliedConfigHash: string | null;
  pendingConfigHash: string | null;
  hostAcknowledgedRevision: number | null;
  hostAcknowledgedConfigHash: string | null;
  applyStatus: InputApplyStatus;
  outputTarget: JsonObject;
  buttonMapping: JsonObject;
  gyroMotion: JsonObject;
  ownership: JsonObject;
  diagnostics: JsonObject;
  axisCalibration?: AxisCalibrationConfig & { deviceKey?: string };
}

export type SettingsCasResult =
  | { ok: true; revision: number; value: InputSettingsSnapshot; globalShortcutRules?: JsonObject }
  | { ok: false; reason: 'revision-conflict' | 'write-failed' | 'game-override-active' | 'cancelled' | 'virtual-start-protection-active'; currentRevision: number; value: InputSettingsSnapshot; startProtection?: VirtualStartProtectionState; globalShortcutRules?: JsonObject };

/** Pure CAS decision used by the durable writer and S-15 fixtures. */
export function evaluateInputCas(
  current: InputSettingsSnapshot,
  expectedRevision: number,
  patch: JsonObject,
): SettingsCasResult {
  if (current.revision !== expectedRevision) {
    return { ok: false, reason: 'revision-conflict', currentRevision: current.revision, value: snapshotSettingsData(current) };
  }
  // Coupled pages/shortcuts cannot mutate the base while a dedicated overlay owns input.
  if (current.gameOverride && !('gameOverride' in patch) && ('outputTarget' in patch || 'gyroMotion' in patch)) {
    return { ok: false, reason: 'game-override-active', currentRevision: current.revision, value: snapshotSettingsData(current) };
  }
  const next = mergeSettings(current, patch) as InputSettingsSnapshot;
  // Overlay is a replacement, NOT deep merge: B/follow must not inherit A's parameters.
  if ('gameOverride' in patch) next.gameOverride = snapshotSettingsData(patch.gameOverride);
  next.schemaVersion = 1;
  next.revision = expectedRevision + 1;
  // A successful CAS is only a durable user-config save. It has not reached
  // InputHost, so it must begin T10 in saved-pending rather than "applied".
  next.pendingConfigHash = computeInputConfigHash(next);
  next.hostAcknowledgedRevision = null;
  next.hostAcknowledgedConfigHash = null;
  next.applyStatus = 'saved-pending';
  return { ok: true, revision: next.revision, value: structuredClone(next) };
}

/**
 * Pure write-outcome model for the input CAS transaction.  A durable write
 * failure must leave the caller on the pre-write snapshot so UI code cannot
 * report success (or advance its in-memory revision) before persistence.
 */
export function evaluateInputCasWrite(
  current: InputSettingsSnapshot,
  expectedRevision: number,
  patch: JsonObject,
  writeSucceeded: boolean,
): SettingsCasResult {
  const decision = evaluateInputCas(current, expectedRevision, patch);
  if (!decision.ok || writeSucceeded) return decision;
  return {
    ok: false,
    reason: 'write-failed',
    currentRevision: current.revision,
    value: snapshotSettingsData(current),
  };
}

export interface UnifiedSettings extends JsonObject {
  configurationBaseline?: ConfigurationBaseline;
  schemaVersion: number;
  baselineId: string;
  appVersionWritten?: string;
  ui: JsonObject;
  background: JsonObject;
  music: JsonObject;
  gamepad: JsonObject;
  performanceSchedule: JsonObject;
  gameCustom: JsonObject;
  tdp: JsonObject;
  rtss: JsonObject;
  cpu: JsonObject;
  sleep: JsonObject;
  quickApps: JsonObject;
  startupDesired: JsonObject;
  power: JsonObject;
  tray: JsonObject;
  autoclose: JsonObject;
  fan: JsonObject;
  input: InputSettingsSnapshot;
  extensions: JsonObject;
}

const DEFAULTS: UnifiedSettings = {
  schemaVersion: 1,
  baselineId: '2026-08-09-user-default',
  ui: {
    theme: 'blue-black',
    dynamicBackgroundEnabled: true,
    backgroundOpacity: 0.7,
    backgroundBlur: 0,
    backgroundPositionX: 50,
    backgroundPositionY: 50,
    videoBatteryPause: true,
    scheduleMonitor: true,
    steamChartAutoRefresh: 'none',
    gestureSummonEnabled: false,
  },
  background: { asset: {}, dynamic: {} },
  music: { folder: '', mode: 'random', volume: 0.11 },
  gamepad: {
    enabled: true,
    bDoubleMinimize: true,
    tdpShortcut: true,
    fpsShortcut: true,
    killGame: true,
    openKeyboard: true,
    returnDesktop: true,
    mouseToggle: true,
    mouseBackend: 'joyxoff',
    // HC ControllerPage parity: vibration strength multiplier plus device Aura
    // lighting (effect / colors / speed / brightness). Persisted here, applied
    // through native gamepad.setLed / gamepad.vibrate.
    feedback: {
      vibrationStrength: 70,
      lighting: {
        enabled: false,
        off: false,
        mode: 'solid',
        presetId: '',
        color: '#2ea6ff',
        color2: '#9d7cff',
        speed: 55,
        brightness: 65,
      },
    },
  },
  performanceSchedule: {
    version: 2,
    configured: true,
    enabled: true,
    active: { ac: 'elite', dc: 'medium' },
    profiles: {},
  },
  gameCustom: { version: 1, entries: {} },
  rtss: {
    templateDefaults: true,
    frameRates: {
      ac: { fps: 120, ceiling: 120, lastFps: 120 },
      dc: { fps: 60, ceiling: 60, lastFps: 60 },
    },
  },
  tdp: {
    tdpMax: 120,
    fpsLimit: 120,
    float: { target: 120, profile: 'none', tdpStrategy: 'none' },
    autoApply: { boot: true, wake: true },
  },
  cpu: {
    profiles: {},
    active: 'balanced',
    autostart: {
      ccd: { enabled: true, mode: 0 },
      uv: { enabled: true, preset: 'off', vendor: 'amd' },
    },
    autoEnable: { mode: 'dc' },
    lock: {},
  },
  sleep: {
    mode: 'custom',
    pauseGameOnSleep: true,
    retryOnEntryFailure: true,
    retryOnSleepTimeout: true,
    retryOnNonUserWake: false,
    factMonitorEnabled: false,
    sleepPowerPlanOptimizationEnabled: true,
    staticWakeAutoScanEnabled: true,
    wakePasswordEnabled: false,
    steamOverlayOffFix: false, // deprecated/abandoned; normalized off, no UI
    autoWakeRecovery: true,
  },
  quickApps: createDefaultQuickApps(),
  startupDesired: {
    virtualGamepadPersona: 'disabled',
    gyroPreset: 'off',
    bootControlCenter: true,
    fanControl: false,
    rtss: false,
    energyStar: false,
    memoryCleanup: false,
    steamCommunity: false,
  },
  power: { scheme: {} },
  tray: { resident: false },
  autoclose: { enabled: false, procs: [] },
  fan: {
    // Formal fan integration is eligible by default; the UI remains hidden
    // until a successful handshake or remembered supported device identity.
    featureEnabled: true,
    configured: false,
    deviceIdentity: null,
    preset: 'balanced',
    motionEnabled: true,
    diagnosticLoggingEnabled: false,
  },
  input: {
    schemaVersion: 1,
    revision: 0,
    appliedRevision: null,
    appliedConfigHash: null,
    pendingConfigHash: null,
    hostAcknowledgedRevision: null,
    hostAcknowledgedConfigHash: null,
    applyStatus: 'unknown',
    outputTarget: {
      schemaVersion: 1,
      revision: 0,
      closure: 'UNENCLOSED',
      persona: 'disabled',
      descriptorHash: null,
      buttonMappingEnabled: false,
      gyroEnabled: false,
      visibilityPolicy: 'hidden',
      // 背键映射 Phase A（背键映射A任务书 2026-09-14 S5）：默认 off。
      backMapping: false,
    },
    buttonMapping: { schemaVersion: 1, revision: 0, closure: 'UNENCLOSED', profileId: 'default', rules: {} },
    bootModeShortcuts: null,
    gyroMotion: {
      schemaVersion: 1, revision: 0, closure: 'UNENCLOSED', enabled: false,
      provider: null, unit: null, calibrationId: null,
      // E7 回退（第七十七批 E-①，2026-09-12 用户批准）：outputMode 默认恢复 'disabled'
      // （E7 曾改为 'virtual-stick'，实测引入惯性体感，用户裁决回退）。
      // 'disabled' = 未配置输出即无映射（HC actionType=Disabled 等价，fail-closed）。
      outputMode: 'disabled',
      motionInput: 'local-space', motionMode: 'off', motionTrigger: null,
      steeringAxis: 'roll',
      gyroMultiplier: 1, accelerometerMultiplier: 1, gyroThreshold: 2000, motionSensitivityX: 1.5, motionSensitivityY: 1.5,
      gyroWeight: 1.8, velocityMode: 'default', velocityScale: 1,
      innerDeadzone: 1, outerDeadzone: 0, deadzone: 1, antiDeadzone: 20, outputShape: 'default', invertHorizontal: false, invertVertical: false,
      aimingSightsMultiplier: 0.3, aimingSightsTrigger: '',
      // 用户裁决（2026-09-30）：无数据时默认预设为 FPS 射击（原缺省为「自定义」）。
      preset: 'fps', outputStick: 'right', outputAxis: 'xy', calibration: null, autoCalibrate: true,
      // 用户裁决（2026-09-29）：虚拟手柄联动默认开启。
      virtualPadLink: true,
    },
    ownership: { schemaVersion: 1, mode: 'ymcc-semantic', frontendSummonVirtualControl: false, frontendIsolationEnabled: true, externalControllerPriority: false },
    diagnostics: { inputLoggingEnabled: false },
  },
  extensions: {},
};

let settingsGeneration = 0;
let cache: UnifiedSettings | null = null;
let cacheStamp: string | null = null;

const SETTINGS_IO_TIMEOUT_MS = 3000;
const SETTINGS_LOAD_TIMEOUT_MS = 8000;
const SETTINGS_MAX_BYTES = 4 * 1024 * 1024;
export interface SettingsRecoveryState {
  status: 'ready' | 'runtime-fallback';
  reason: 'corrupt' | 'unsupported' | 'read-failed' | 'write-failed' | null;
  source: 'main' | 'backup' | 'defaults' | 'cache';
  persisted: boolean;
  message: string;
}
let recoveryState: SettingsRecoveryState = {
  status: 'ready', reason: null, source: 'main', persisted: false, message: '',
};

/** Runtime availability is separate from a durable save/Host acknowledgement. */
export function getSettingsRecoveryState(): SettingsRecoveryState {
  return { ...recoveryState };
}

async function boundedSettingsIO<T>(operation: Promise<T>, label: string, timeout = SETTINGS_IO_TIMEOUT_MS): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  try {
    return await Promise.race([operation, new Promise<never>((_, reject) => {
      timer = setTimeout(() => reject(new Error(`${label}超时`)), timeout);
    })]);
  } finally { if (timer !== undefined) clearTimeout(timer); }
}

function runtimeSettings(candidate: UnifiedSettings = normalizeSettings(DEFAULTS)): UnifiedSettings {
  const next = snapshotSettingsData(candidate);
  next.startupDesired = { ...next.startupDesired, virtualGamepadPersona: 'disabled', gyroPreset: 'off', fanControl: false, rtss: false, energyStar: false, memoryCleanup: false, steamCommunity: false };
  next.performanceSchedule.enabled = false;
  next.tdp.autoApply = { ...next.tdp.autoApply, boot: false, wake: false };
  next.cpu.autoEnable = { ...next.cpu.autoEnable, mode: 'off' };
  next.cpu.autostart.ccd.enabled = false;
  next.cpu.autostart.uv.enabled = false;
  next.fan.featureEnabled = false;
  next.fan.motionEnabled = false;
  next.sleep.autoWakeRecovery = false;
  next.sleep.staticWakeAutoScanEnabled = false;
  next.sleep.sleepPowerPlanOptimizationEnabled = false;
  next.input.outputTarget.persona = 'disabled';
  next.input.outputTarget.buttonMappingEnabled = false;
  next.input.outputTarget.gyroEnabled = false;
  next.input.outputTarget.descriptorHash = null;
  next.input.outputTarget.closure = 'UNENCLOSED';
  next.input.gyroMotion.enabled = false;
  next.input.gyroMotion.outputMode = 'disabled';
  next.input.gyroMotion.motionMode = 'off';
  next.input.appliedRevision = null;
  next.input.appliedConfigHash = null;
  next.input.pendingConfigHash = null;
  next.input.hostAcknowledgedRevision = null;
  next.input.hostAcknowledgedConfigHash = null;
  next.input.applyStatus = 'safe-stop';
  return next;
}

class UnsupportedSettingsError extends Error {}

export function getSettingsGeneration(): number { return settingsGeneration; }

// Resident consumers compare the already observed section. Explicit reads and
// successful commits replace cache; a steady controller never stats the file.
// Section revisions avoid invalidation from unrelated input/logging changes.
const observedSectionRevisions = new Map<SettingsSection, { document: UnifiedSettings; json: string; revision: number }>();
export function getSettingsSectionRevision(section: SettingsSection): number | null {
  if (!cache) return null;
  const previous = observedSectionRevisions.get(section);
  if (previous?.document === cache) return previous.revision;
  const json = JSON.stringify(cache[section] ?? {});
  const revision = previous ? previous.revision + (previous.json === json ? 0 : 1) : 1;
  observedSectionRevisions.set(section, { document: cache, json, revision });
  return revision;
}


export function assertSettingsGeneration(generation: number): void {
  if (generation !== settingsGeneration) throw new Error('配置缓存已失效或目录已切换，旧请求已取消');
}
let loadPromise: Promise<UnifiedSettings> | null = null;
let writeQueue: Promise<void> = Promise.resolve();

function isObject(value: unknown): value is JsonObject {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

/** Deep merge detached data, replace arrays/primitives, and retain unknown keys. */
export function mergeSettings<T>(base: T, patch: any): T {
  // Neither defaults nor caller-owned reactive records/arrays may enter cache
  // by reference. Copy both inputs once, then merge only detached data.
  return mergeSettingsSnapshots(snapshotSettingsData(base), snapshotSettingsData(patch));
}

function mergeSettingsSnapshots<T>(base: T, patch: any): T {
  if (!isObject(base) || !isObject(patch)) return (patch === undefined ? base : patch) as T;
  const out: JsonObject = { ...(base as JsonObject) };
  for (const [key, value] of Object.entries(patch)) {
    const merged = isObject(value) && Object.prototype.hasOwnProperty.call(out, key) && isObject(out[key])
      ? mergeSettingsSnapshots(out[key], value) : value;
    Object.defineProperty(out, key, { value: merged, enumerable: true, configurable: true, writable: true });
  }
  return out as T;
}

/** Explicit old FPS values need migration; injected defaults are not user intent. */
function hasLegacyFrameRates(source: JsonObject): boolean {
  if (isObject(source.tdp) && typeof source.tdp.fpsLimit === 'number' && Number.isFinite(source.tdp.fpsLimit)) return true;
  const profiles = isObject(source.performanceSchedule) && isObject(source.performanceSchedule.profiles)
    ? source.performanceSchedule.profiles : {};
  return Object.values(profiles).some((side) => isObject(side) && Object.values(side).some((profile) =>
    isObject(profile) && typeof profile.fpsTarget === 'number' && Number.isFinite(profile.fpsTarget)));
}

export function normalizeSettings(raw: unknown): UnifiedSettings {
  const source = isObject(raw) ? raw : {};
  assertSupportedSettingsBaseline(source);
  const merged = mergeSettings(DEFAULTS, source) as UnifiedSettings;
  function repairShape(value: JsonObject, template: JsonObject): void {
    for (const [key, expected] of Object.entries(template)) {
      if (isObject(expected)) {
        if (!isObject(value[key])) value[key] = structuredClone(expected);
        else repairShape(value[key], expected);
      } else if (Array.isArray(expected)) {
        if (!Array.isArray(value[key])) value[key] = structuredClone(expected);
      } else if (expected !== null && (typeof value[key] !== typeof expected ||
          (typeof expected === 'number' && !Number.isFinite(value[key])))) {
        value[key] = expected;
      }
    }
  }
  repairShape(merged as unknown as JsonObject, DEFAULTS as unknown as JsonObject);
  if (!isObject(merged.sleep)) merged.sleep = structuredClone(DEFAULTS.sleep);
  // Preserve an explicit legacy choice; missing preferences stay off.
  if (isObject(source.sleep) && typeof source.sleep.retryOnNonUserWake !== 'boolean' &&
      typeof source.sleep.resleepEnabled === 'boolean')
    merged.sleep.retryOnNonUserWake = source.sleep.resleepEnabled;
  // Older builds coupled automatic wake scans to power-plan repair. Preserve
  // that saved choice once, then keep the new independent switch authoritative.
  if (!isObject(source.sleep) || typeof source.sleep.staticWakeAutoScanEnabled !== 'boolean')
    merged.sleep.staticWakeAutoScanEnabled = merged.sleep.sleepPowerPlanOptimizationEnabled !== false;
  // This abandoned key can survive older imports, but can never become active.
  merged.sleep.steamOverlayOffFix = false;
  if (typeof merged.sleep.autoWakeRecovery !== 'boolean') merged.sleep.autoWakeRecovery = false;
  // An explicit saved frame pair predates the fresh-template marker unless stated otherwise.
  if (isObject(source.rtss) && isObject(source.rtss.frameRates) && source.rtss.templateDefaults === undefined) merged.rtss.templateDefaults = false;
  const savedFrames = isObject(source.rtss) && isObject(source.rtss.frameRates) ? source.rtss.frameRates : null;
  if (!savedFrames && hasLegacyFrameRates(source)) {
    // Do not let new AC/DC defaults hide a user's older scalar/profile FPS.
    delete merged.rtss.frameRates;
  }
  const startup = isObject(merged.startupDesired) ? merged.startupDesired : {};
  merged.startupDesired = { ...startup, ...normalizeInputStartupPreferences(startup) };
  merged.schemaVersion = Number.isFinite(Number(merged.schemaVersion))
    ? Math.max(1, Math.floor(Number(merged.schemaVersion)))
    : 1;
  if (typeof merged.baselineId !== 'string' || !merged.baselineId) {
    merged.baselineId = DEFAULTS.baselineId;
  }
  if (!isObject(merged.input)) merged.input = structuredClone(DEFAULTS.input);
  const input = merged.input as Partial<InputSettingsSnapshot>;
  input.schemaVersion = Number.isSafeInteger(Number(input.schemaVersion)) ? Math.max(1, Math.floor(Number(input.schemaVersion))) : 1;
  input.revision = Number.isSafeInteger(Number(input.revision)) ? Math.max(0, Math.floor(Number(input.revision))) : 0;
  input.appliedRevision = input.appliedRevision == null ? null : ((typeof input.appliedRevision === 'number' && Number.isSafeInteger(input.appliedRevision)) ? Math.max(0, Math.floor(Number(input.appliedRevision))) : null);
  input.appliedConfigHash = typeof input.appliedConfigHash === 'string' ? input.appliedConfigHash : null;
  input.pendingConfigHash = typeof input.pendingConfigHash === 'string' ? input.pendingConfigHash : null;
  input.hostAcknowledgedRevision = input.hostAcknowledgedRevision == null ? null : ((typeof input.hostAcknowledgedRevision === 'number' && Number.isSafeInteger(input.hostAcknowledgedRevision)) ? Math.max(0, Math.floor(Number(input.hostAcknowledgedRevision))) : null);
  input.hostAcknowledgedConfigHash = typeof input.hostAcknowledgedConfigHash === 'string' ? input.hostAcknowledgedConfigHash : null;
  const legacyApplyStatus = String(input.applyStatus);
  if (legacyApplyStatus === 'pending') input.applyStatus = 'saved-pending';
  else if (legacyApplyStatus === 'applied') input.applyStatus = 'active';
  else if (!['unknown', 'saved-pending', 'host-acknowledged', 'active', 'rejected', 'safe-stop'].includes(legacyApplyStatus)) input.applyStatus = 'unknown';
  const ownership = isObject(input.ownership) ? input.ownership : structuredClone(DEFAULTS.input.ownership);
  ownership.frontendIsolationEnabled = ownership.frontendIsolationEnabled !== false;
  ownership.externalControllerPriority = ownership.externalControllerPriority === true;
  input.ownership = ownership;
  const target = isObject(input.outputTarget) ? input.outputTarget : {};
  target.schemaVersion = Number.isSafeInteger(Number(target.schemaVersion)) ? Math.max(1, Math.floor(Number(target.schemaVersion))) : 1;
  target.revision = Number.isSafeInteger(Number(target.revision)) ? Math.max(0, Math.floor(Number(target.revision))) : 0;
  if (!['CLOSED', 'UNENCLOSED', 'NOT-APPLICABLE'].includes(String(target.closure))) target.closure = 'UNENCLOSED';
  const previousPersona = target.persona;
  target.persona = migrateControllerPersona(previousPersona);
  if (previousPersona !== target.persona) {
    target.descriptorHash = null; target.closure = 'UNENCLOSED';
    input.appliedRevision = null; input.appliedConfigHash = null; input.pendingConfigHash = null;
    input.hostAcknowledgedRevision = null; input.hostAcknowledgedConfigHash = null; input.applyStatus = 'saved-pending';
  }
  if (isObject(input.gameOverride)) {
    const override = input.gameOverride;
    const savedTargets = [override.outputTarget, isObject(override.baseline) ? override.baseline.outputTarget : undefined];
    for (const savedTarget of savedTargets) {
      if (!isObject(savedTarget) || !('persona' in savedTarget)) continue;
      const nextPersona = migrateControllerPersona(savedTarget.persona);
      if (nextPersona === savedTarget.persona) continue;
      savedTarget.persona = nextPersona; savedTarget.descriptorHash = null; savedTarget.closure = 'UNENCLOSED';
      input.appliedRevision = null; input.appliedConfigHash = null; input.pendingConfigHash = null;
      input.hostAcknowledgedRevision = null; input.hostAcknowledgedConfigHash = null; input.applyStatus = 'saved-pending';
    }
  }
  if (!['hidden', 'project-only', 'unavailable'].includes(String(target.visibilityPolicy))) target.visibilityPolicy = 'hidden';
  target.buttonMappingEnabled = target.buttonMappingEnabled === true;
  target.gyroEnabled = target.gyroEnabled === true;
  // 背键映射 Phase A（背键映射A任务书 2026-09-14 S5）：默认 false；
  // 仅 persona==steamdeck 且物理机≠真 Deck 时 native 门控生效。
  target.backMapping = target.backMapping === true;
  // GP-FAMILY-CAPABILITY-4 C4-D2：通用 OEM→rear 映射（可空、默认空）。
  // 键 = canonical OEM 键名（nativeKeyboardShortcutMaskBit 的 bit20+ 段），
  // 值 = left/right/both/off；非法值丢弃，无有效项则删除字段（未配置 = 零行为变化）。
  if (isObject(target.oemRearMap)) {
    const oemRearMap: JsonObject = {};
    for (const [key, value] of Object.entries(target.oemRearMap as JsonObject)) {
      if (typeof value !== 'string') continue;
      const norm = value.trim().toLowerCase();
      if (norm === 'left' || norm === 'right' || norm === 'both' || norm === 'off') oemRearMap[key] = norm;
    }
    if (Object.keys(oemRearMap).length > 0) target.oemRearMap = oemRearMap;
    else delete target.oemRearMap;
  } else {
    delete target.oemRearMap;
  }
  target.descriptorHash = typeof target.descriptorHash === 'string' ? target.descriptorHash : null;
  input.outputTarget = target;
  const mapping = isObject(input.buttonMapping) ? input.buttonMapping : {};
  mapping.schemaVersion = Number.isSafeInteger(Number(mapping.schemaVersion)) ? Math.max(1, Math.floor(Number(mapping.schemaVersion))) : 1;
  mapping.revision = Number.isSafeInteger(Number(mapping.revision)) ? Math.max(0, Math.floor(Number(mapping.revision))) : 0;
  if (!['CLOSED', 'UNENCLOSED', 'NOT-APPLICABLE'].includes(String(mapping.closure))) mapping.closure = 'UNENCLOSED';
  mapping.profileId = typeof mapping.profileId === 'string' && mapping.profileId.trim() ? mapping.profileId : 'default';
  if (!isObject(mapping.rules)) mapping.rules = {};
  input.buttonMapping = mapping;
  const motion = isObject(input.gyroMotion) ? input.gyroMotion : {};
  const motionSource = isObject((source as any).input?.gyroMotion) ? (source as any).input.gyroMotion : {};
  motion.schemaVersion = Number.isSafeInteger(Number(motion.schemaVersion)) ? Math.max(1, Math.floor(Number(motion.schemaVersion))) : 1;
  motion.revision = Number.isSafeInteger(Number(motion.revision)) ? Math.max(0, Math.floor(Number(motion.revision))) : 0;
  if (!['CLOSED', 'UNENCLOSED', 'NOT-APPLICABLE'].includes(String(motion.closure))) motion.closure = 'UNENCLOSED';
  motion.enabled = motion.enabled === true;
  motion.provider = typeof motion.provider === 'string' && motion.provider.trim() ? motion.provider : null;
  motion.unit = typeof motion.unit === 'string' && motion.unit.trim() ? motion.unit : null;
  motion.calibrationId = typeof motion.calibrationId === 'string' && motion.calibrationId.trim() ? motion.calibrationId : null;
  if (!['virtual-stick', 'disabled'].includes(String(motion.outputMode))) motion.outputMode = 'disabled';
  if (!['local-space', 'player-space', 'world-space', 'joystick-steering'].includes(String(motion.motionInput))) motion.motionInput = 'local-space';
  // HC Profile.SteeringAxis: Roll=0, Yaw=1, Auto=2 (default Roll).
  if (!['roll', 'yaw', 'auto'].includes(motion.steeringAxis)) motion.steeringAxis = 'roll';
  if (!['off', 'on', 'toggle'].includes(String(motion.motionMode))) motion.motionMode = 'off';
  motion.motionTrigger = typeof motion.motionTrigger === 'string' && motion.motionTrigger.trim() ? motion.motionTrigger : null;
  const finite = (value: unknown, fallback: number, min: number, max: number) => {
    return typeof value === 'number' && Number.isFinite(value)
      ? Math.min(max, Math.max(min, value)) : fallback;
  };
  // HC ProfilesPage motion controls expose both producer multipliers as
  // 0.1..3.0; do not widen the durable schema to a YMCC-only 0..10 range.
  motion.gyroMultiplier = finite(motion.gyroMultiplier, 1, 0.1, 3);
  motion.accelerometerMultiplier = finite(motion.accelerometerMultiplier, 1, 0.1, 3);
  // HC GamepadMotion clamps threshold-calibration input to minGyro=124 but
  // does not impose a 2000-dps maximum; 2000 is its default calibration value.
  motion.gyroThreshold = finite(motion.gyroThreshold, 2000, HC_MIN_GYRO_THRESHOLD_DPS, Number.MAX_VALUE);
  motion.motionSensitivityX = finite(motion.motionSensitivityX, 1.5, 0.1, 3);
  motion.motionSensitivityY = finite(motion.motionSensitivityY, 1.5, 0.1, 3);
  // HC profiles persist this as a SortedDictionary. Its editor creates 49
  // nodes at 1/48 increments, but ProcessMotion works with any valid set of
  // at least two ordered nodes. Sort here to retain that storage semantic and
  // reject duplicate/invalid keys. Absence selects HC's default 49 nodes at
  // 0.5 in the native mapper.
  const sensitivityArray = Array.isArray(motion.motionSensitivityArray) ? motion.motionSensitivityArray : null;
  const normalizedSensitivityArray = sensitivityArray?.map((node: unknown) => {
    if (!Array.isArray(node) || node.length !== 2) return null;
    if (typeof node[0] !== 'number' || typeof node[1] !== 'number') return null;
    const key = node[0];
    const value = node[1];
    return Number.isFinite(key) && Number.isFinite(value) && key >= 0 && key <= 1 && value >= 0 && value <= 1
      ? [key, value] as [number, number]
      : null;
  });
  if (normalizedSensitivityArray && normalizedSensitivityArray.length >= 2 && normalizedSensitivityArray.every((node) => node !== null)) {
    const ordered = (normalizedSensitivityArray as [number, number][]).sort((left, right) => left[0] - right[0]);
    if (ordered.every((node, index) => index === 0 || node[0] > ordered[index - 1][0])) motion.motionSensitivityArray = ordered;
    else delete motion.motionSensitivityArray;
  } else {
    delete motion.motionSensitivityArray;
  }
  // 用户裁决（2026-09-29）：默认 1.8，上限放宽到 3（原 HC 口径 1.0..2.0）。
  motion.gyroWeight = finite(motion.gyroWeight, 1.8, 1, 3);
  // HC AxisActions.ResponseCurvePoints (AxisActions.cs:36-44): optional 0..1
  // control points. Absence means the linear identity curve (no-op); when
  // present, require at least two finite ordered points with strictly ascending
  // keys, mirroring the motionSensitivityArray retention rules.
  const responseCurve = Array.isArray(motion.responseCurvePoints) ? motion.responseCurvePoints : null;
  const normalizedResponseCurve = responseCurve?.map((node: unknown) => {
    if (!Array.isArray(node) || node.length !== 2) return null;
    if (typeof node[0] !== 'number' || typeof node[1] !== 'number') return null;
    const ckey = node[0];
    const cvalue = node[1];
    return Number.isFinite(ckey) && Number.isFinite(cvalue) && ckey >= 0 && ckey <= 1 && cvalue >= 0 && cvalue <= 1
      ? [ckey, cvalue] as [number, number]
      : null;
  });
  if (normalizedResponseCurve && normalizedResponseCurve.length >= 2 && normalizedResponseCurve.every((node) => node !== null)) {
    const orderedCurve = (normalizedResponseCurve as [number, number][]).sort((left, right) => left[0] - right[0]);
    if (orderedCurve.every((node, index) => index === 0 || node[0] > orderedCurve[index - 1][0])) motion.responseCurvePoints = orderedCurve;
    else delete motion.responseCurvePoints;
  } else {
    delete motion.responseCurvePoints;
  }
  if (!['default', 'velocity'].includes(String(motion.velocityMode))) motion.velocityMode = 'default';
  motion.velocityScale = finite(motion.velocityScale, 1, 0, 10);
  // Legacy alias: old snapshots could persist `deadzone` (inner deadzone) without
  // `innerDeadzone`. Promote it BEFORE clamping so an inner-only old value is not
  // zeroed (UI read fallback GyroMotionView.vue:258). Native consumes innerDeadzone
  // only (main.cpp:5239); the alias stays a durable-schema compat shim.
  // DEFAULTS already merged innerDeadzone:1, so presence must be judged on the
  // original source object with an own-property check.
  if (!('innerDeadzone' in motionSource) && typeof motionSource.deadzone === 'number' &&
      Number.isFinite(motionSource.deadzone)) motion.innerDeadzone = motionSource.deadzone;
  motion.innerDeadzone = Math.round(finite(motion.innerDeadzone, 1, 0, 25));
  motion.outerDeadzone = Math.round(finite(motion.outerDeadzone, 0, 0, 25));
  motion.deadzone = motion.innerDeadzone;
  // Inner/outer stay 0..25%. Anti-deadzone default 20%, max 40% (user 2026-09-16).
  motion.antiDeadzone = Math.round(finite(motion.antiDeadzone, 20, 0, 40));
  // HC SettingsMode1/Profile sliders: SteeringMaxAngle 10..80 degrees,
  // SteeringPower 0.2..5, and SteeringDeadzone 0..5 degrees.
  motion.steeringMaxAngle = finite(motion.steeringMaxAngle, 30, 10, 80);
  motion.steeringPower = finite(motion.steeringPower, 1, 0.2, 5);
  motion.steeringDeadzone = finite(motion.steeringDeadzone, 0, 0, 5);
  if (!['default', 'circle', 'cross', 'square'].includes(String(motion.outputShape))) motion.outputShape = 'default';
  // HC AimingSights: multiplier 0..2 optional, trigger a control-name string or null.
  // 用户裁决（2026-09-29）：默认 0.3（30% 变慢）。
  motion.aimingSightsMultiplier = finite(motion.aimingSightsMultiplier, 0.3, 0, 2);
  motion.aimingSightsTrigger = typeof motion.aimingSightsTrigger === 'string' && motion.aimingSightsTrigger.trim() ? motion.aimingSightsTrigger.trim() : null;
  motion.invertHorizontal = motion.invertHorizontal === true;
  motion.invertVertical = motion.invertVertical === true;
  // 用户裁决（2026-09-30）：缺省/非法值一律回落 FPS（无数据默认预设 = FPS）；
  // 'off' 是历史关闭档，仍原样保留（视图侧回读为上次激活预设）。
  if (!['off', 'fps', 'racing', 'custom', 'steam'].includes(String(motion.preset))) motion.preset = 'fps';
  if (!['left', 'right'].includes(String(motion.outputStick))) motion.outputStick = 'right';
  if (!['xy', 'x'].includes(String(motion.outputAxis))) motion.outputAxis = 'xy';
  motion.autoCalibrate = motion.autoCalibrate !== false;
  // 用户裁决（2026-09-29）：虚拟手柄联动默认 true（旧配置缺省即视为开启）。
  motion.virtualPadLink = motion.virtualPadLink !== false;
  if (isObject(motion.presets)) {
    for (const preset of Object.values(motion.presets)) {
      if (isObject(preset)) {
        preset.autoCalibrate = preset.autoCalibrate !== false;
        delete preset.gyroMode;
      }
    }
  }
  // Legacy dead fields: axisOrder / axisMatrix were never consumed by native or
  // mapper (T22-BUS-P75) and have no HC equivalent; drop them so a stale config
  // cannot masquerade as an axis-order customization.
  if ('axisOrder' in motion) delete (motion as Record<string, unknown>).axisOrder;
  if ('axisMatrix' in motion) delete (motion as Record<string, unknown>).axisMatrix;
  input.gyroMotion = motion;
  retireBootGlobalShortcuts(merged);
  stampSettingsBaseline(merged);
  return merged;
}

const normalize = normalizeSettings;

async function readSettingsText(path: string, maxBytes: number): Promise<string | null> {
  // Establish absence before reading. A failed/timed-out read must stay a fault,
  // even if a later exists check would report false (deletion/permission race).
  const exists = await boundedSettingsIO(fs.exists(path), '配置存在性读取');
  if (exists !== true && exists !== false) throw new Error(`配置存在性读取回执无效：${path}`);
  if (!exists) return null;
  try {
    const raw = await boundedSettingsIO(fs.readTextFile(path, maxBytes), '配置读取');
    if (typeof raw !== 'string' || new TextEncoder().encode(raw).byteLength > maxBytes) throw new Error('配置超过大小限制或读取回执无效');
    return raw;
  } catch (e) {
    throw new Error(`读取配置文件失败：${path}；${(e as Error).message}`);
  }
}

function parseSettingsDocument(raw: string): JsonObject | null {
  try {
    const parsed = JSON.parse(raw.charCodeAt(0) === 0xFEFF ? raw.slice(1) : raw);
    if (!isObject(parsed)) return null;
    const pending: Array<{ value: object; depth: number }> = [{ value: parsed, depth: 0 }];
    while (pending.length) {
      const { value, depth } = pending.pop()!;
      if (depth > 64) return null;
      for (const child of Object.values(value)) {
        if (child !== null && typeof child === 'object') pending.push({ value: child, depth: depth + 1 });
      }
    }
    return parsed;
  } catch { return null; }
}

async function readJson(path: string, maxBytes = 4 * 1024 * 1024): Promise<JsonObject | null> {
  const raw = await readSettingsText(path, maxBytes);
  if (raw === null) return null;
  // Only bounded JSON/document failures are corrupt; I/O faults stay separate.
  return parseSettingsDocument(raw);
}

async function readLegacyJson(name: string, dir: string): Promise<JsonObject | null> {
  return readJson(`${dir}\\${name}`);
}

async function readLegacyLaunchApps(dir: string): Promise<JsonObject | null> {
  // Older versions persisted a bare array, rejected by object-only readJson.
  const raw = await readSettingsText(`${dir}\\launch_apps.json`, 4 * 1024 * 1024);
  if (raw === null) return null;
  try {
    const parsed: unknown = JSON.parse(raw.charCodeAt(0) === 0xFEFF ? raw.slice(1) : raw);
    if (Array.isArray(parsed)) return { apps: parsed };
    return isObject(parsed) ? parsed : null;
  } catch {
    return null;
  }
}

async function readLegacyNumber(name: string, dir: string): Promise<number | null> {
  const raw = await readSettingsText(`${dir}\\${name}`, 128);
  if (raw === null) return null;
  if (!raw.trim()) return null;
  const value = Number(raw.trim());
  return Number.isFinite(value) ? value : null;
}

async function migrateLegacySettings(dir = settingsDir, check: () => void = () => {}): Promise<UnifiedSettings> {
  check();
  let next = structuredClone(DEFAULTS);
  const set = (section: SettingsSection, value: unknown) => {
    if (isObject(value)) (next as any)[section] = mergeSettings((next as any)[section], value);
  };
  const ui = await readLegacyJson('ui-settings.json', dir);
  check();
  const summon = await readLegacyJson('summon.json', dir);
  check();
  const music = await readLegacyJson('music_player.json', dir);
  check();
  const schedule = await readLegacyJson('performance-schedule.json', dir);
  check();
  const control = await readLegacyJson('control-config.json', dir);
  check();
  const legacyTdp = await readLegacyNumber('tdp.txt', dir);
  check();
  const legacyFps = await readLegacyNumber('FPS-ac.txt', dir);
  check();
  if (hasLegacyFrameRates({ tdp: control, performanceSchedule: schedule }) || legacyFps !== null) {
    delete next.rtss.frameRates;
  }
  const float = await readLegacyJson('autofloat.json', dir);
  check();
  const tdpApply = await readLegacyJson('tdp-auto-apply.json', dir);
  check();
  const cpuProfiles = await readLegacyJson('cpu_profiles.json', dir);
  check();
  const cpuAutostart = await readLegacyJson('cpu_autostart.json', dir);
  check();
  const cpuAutoEnable = await readLegacyJson('cpu_auto_enable.json', dir);
  check();
  const cpuLock = await readLegacyJson('cpu_lock.json', dir);
  check();
  const sleep = await readLegacyJson('Sleep\\sleepguard.json', dir);
  check();
  const apps = await readLegacyLaunchApps(dir);
  check();
  const boot = await readLegacyJson('boot_config.json', dir);
  check();
  const power = await readLegacyJson('yeman-power-scheme.json', dir);
  check();
  const tray = await readLegacyJson('tray_resident.json', dir);
  check();
  const autoclose = await readLegacyJson('autoclose.json', dir);
  check();
  const asset = await readLegacyJson('ui-background\\background.json', dir);
  check();
  const dynamic = await readLegacyJson('ui-background\\dynamic-online.json', dir);
  check();

  set('ui', ui);
  set('gamepad', summon);
  set('music', music);
  set('performanceSchedule', schedule);
  set('tdp', {
    ...(control || {}),
    ...(control ? {} : legacyTdp == null ? {} : { tdpMax: legacyTdp }),
    ...(control ? {} : legacyFps == null ? {} : { fpsLimit: legacyFps }),
    ...(float ? { float } : {}),
    ...(tdpApply ? { autoApply: tdpApply } : {}),
  });
  set('cpu', {
    profiles: cpuProfiles?.profiles || {},
    ...(cpuProfiles?.active === undefined ? {} : { active: cpuProfiles.active }),
    ...(cpuAutostart ? { autostart: cpuAutostart } : {}),
    ...(cpuAutoEnable ? { autoEnable: cpuAutoEnable } : {}),
    ...(cpuLock ? { lock: cpuLock } : {}),
  });
  set('sleep', sleep);
  set('quickApps', apps);
  set('startupDesired', boot ? { bootControlCenter: boot.bootOn === true } : undefined);
  if (power) set('power', { scheme: power });
  set('tray', tray);
  set('autoclose', autoclose);
  set('background', {
    ...(asset ? { asset } : {}),
    ...(dynamic ? { dynamic } : {}),
  });
  return normalize(next);
}

function assertCompatibleSchema(document: JsonObject): void {
  try { assertSupportedSettingsBaseline(document); }
  catch (error) { throw new UnsupportedSettingsError((error as Error).message); }
  const rawSchema = Object.prototype.hasOwnProperty.call(document, 'schemaVersion') ? document.schemaVersion : 0;
  const schema = typeof rawSchema === 'number' ? rawSchema : NaN;
  // Missing/schema 0 documents are legacy. Do not reinterpret an unknown or
  // future version as schema 1, including a recoverable backup document.
  if (!Number.isInteger(schema) || schema < 0 || schema > 1) {
    throw new UnsupportedSettingsError(`配置版本 ${String(document.schemaVersion)} 无法由本程序兼容读取。原文件已保留，请使用匹配版本或选择备份后重置应用配置。`);
  }
}

/** Validate on explicit reads, not a timer: native checks a 100ns file stamp and
 * returns no JSON for an unchanged document. Coalesced callers own snapshots. */
export async function loadSettings(): Promise<UnifiedSettings> {
  if (!loadPromise) {
    const generation = settingsGeneration;
    const cacheAtReadStart = cache;
    const trustedCache = recoveryState.persisted ? cache : null;
    let active = true;
    let reason: SettingsRecoveryState['reason'] = 'read-failed';
    let source: SettingsRecoveryState['source'] = 'defaults';
    let candidate: UnifiedSettings | undefined;
    const check = () => {
      assertSettingsGeneration(generation);
      if (!active) throw new Error('配置读取已超时，旧结果已取消');
    };
    const read = (async () => {
      if (!settingsLocationResolved) await resolveSettingsLocation();
      check();
      const path = SETTINGS_FILE;
      const backupPath = SETTINGS_BACKUP_FILE;
      let main: JsonObject | null;
      let stamp: string | null = null;
      let mainExists = false;
      if (settingsStore.read) {
        const result = await boundedSettingsIO(settingsStore.read(path, cache ? cacheStamp : null), '共享配置读取');
        check();
        if (!result || typeof result.stamp !== 'string') throw new Error('共享配置读取回执无效');
        const readFault = (result as { readFault?: unknown }).readFault;
        const recoverable = readFault === 'content-too-large' || readFault === 'invalid-text-encoding' || readFault === 'invalid-document';
        if (readFault !== undefined && !recoverable) throw new Error(`共享配置读取失败：${String(readFault)}`);
        if (result.unchanged && cache && recoveryState.persisted && cacheStamp !== null) {
          recoveryState = { status: 'ready', reason: null, source: 'main', persisted: true, message: '' };
          return cache;
        }
        stamp = result.stamp;
        mainExists = result.stamp !== 'missing';
        const raw = result.content;
        if (typeof raw !== 'string' || new TextEncoder().encode(raw).byteLength > SETTINGS_MAX_BYTES) {
          throw new Error('共享配置超过大小限制或读取回执无效');
        }
        main = recoverable ? null : parseSettingsDocument(raw);
      } else {
        main = await readJson(path);
        mainExists = main !== null || await boundedSettingsIO(fs.exists(path), '配置存在性读取');
      }
      check();
      if (cache && cache !== cacheAtReadStart) return cache;
      let next: UnifiedSettings;
      if (main) {
        assertCompatibleSchema(main);
        source = 'main';
        const missingSection = SETTINGS_BASELINE.sections.some(({ name }) =>
          !Object.prototype.hasOwnProperty.call(main, name));
        const importLegacy = !hasCompletedSettingsBaseline(main) && missingSection;
        const legacy = importLegacy ? await migrateLegacySettings(settingsDir, check) : DEFAULTS;
        check();
        next = normalize(importLegacy ? mergeSettings(legacy, main) : main);
        candidate = next;
        if (!equalSettingsDocuments(main, next)) {
          reason = 'write-failed';
          next = await writeSettings(next, main, generation, path, false, undefined, check);
          stamp = null;
        }
      } else {
        let backup: JsonObject | null = null;
        let backupExists = false;
        if (settingsStore.read) {
          const result = await boundedSettingsIO(settingsStore.read(backupPath, null), '共享配置备份读取');
          check();
          if (!result || typeof result.stamp !== 'string') throw new Error('共享配置备份回执无效');
          backupExists = result.stamp !== 'missing';
          const fault = (result as { readFault?: unknown }).readFault;
          const recoverable = fault === 'content-too-large' || fault === 'invalid-text-encoding' || fault === 'invalid-document';
          if (fault !== undefined && !recoverable) throw new Error(`共享配置备份读取失败：${String(fault)}`);
          if (typeof result.content !== 'string' || new TextEncoder().encode(result.content).byteLength > SETTINGS_MAX_BYTES)
            throw new Error('共享配置备份超过大小限制或读取回执无效');
          if (!recoverable) {
            backup = parseSettingsDocument(result.content);
          }
        } else {
          backup = await readJson(backupPath);
          backupExists = backup !== null || await boundedSettingsIO(fs.exists(backupPath), '备份存在性读取');
        }
        check();
        if (backup) assertCompatibleSchema(backup);
        const corrupt = !backup && (mainExists || backupExists);
        check();
        source = backup ? 'backup' : 'defaults';
        // initialize is a native recovery request, never permission to overwrite
        // unseen/future files. Native must archive unusable originals under its
        // lock before committing safe defaults; known size/encoding faults can be
        // preserved, while access faults never reach here. A refusal stays retryable.
        next = corrupt ? runtimeSettings() : backup ? normalize(backup) : await migrateLegacySettings(settingsDir, check);
        check();
        candidate = next;
        reason = corrupt ? 'corrupt' : 'write-failed';
        // Native owns preservation and the atomic commit under its lock. Never
        // rename/delete the main document before asking native to recover it.
        next = await writeSettings(next, backup || {}, generation, path, backup === null, undefined, check);
        stamp = null;
      }
      check();
      if (cache && cache !== cacheAtReadStart) return cache;
      cache = next;
      cacheStamp = stamp;
      recoveryState = { status: 'ready', reason: null, source, persisted: true, message: '' };
      return next;
    })();
    const task = boundedSettingsIO(read, '配置加载', SETTINGS_LOAD_TIMEOUT_MS).catch(error => {
      active = false;
      assertSettingsGeneration(generation);
      if (cache && cache !== cacheAtReadStart) return cache;
      if (error instanceof UnsupportedSettingsError) reason = 'unsupported';
      const next = reason === 'read-failed' && trustedCache ? trustedCache : runtimeSettings(candidate);
      recoveryState = { status: 'runtime-fallback', reason,
        source: next === trustedCache ? 'cache' : candidate ? source : 'defaults',
        persisted: next === trustedCache, message: String((error as Error)?.message || error) };
      cache = next;
      cacheStamp = null; // A fallback is never an unchanged native receipt.
      return next;
    });
    loadPromise = task;
  }
  const task = loadPromise;
  try { return snapshotSettingsData(await task); }
  finally { if (loadPromise === task) loadPromise = null; }
}

export class VirtualStartProtectionActive extends Error {
  readonly reason = 'virtual-start-protection-active';
  constructor(readonly document: UnifiedSettings, readonly startProtection?: VirtualStartProtectionState) {
    super('手柄切换处理中，请稍候；可切回本机手柄。');
  }
}

class InputSettingsCommitConflict extends Error {
  constructor(readonly document: UnifiedSettings) { super('输入配置在提交前已变更'); }
}

/** Verify the requested delta against the actual receipt, allowing unrelated
 * native/concurrent fields to advance. Input CAS has its stricter hash check. */
function settingsDeltaCommitted(baseline: unknown, submitted: unknown, committed: unknown, root = true): boolean {
  if (equalSettingsDocuments(baseline, submitted)) return true;
  if (!isObject(submitted)) return equalSettingsDocuments(submitted, committed);
  if (!isObject(committed)) return false;
  const before = isObject(baseline) ? baseline : {};
  const nativeOwned = (key: string) => root && (key === 'input' || key === 'domainLogs');
  for (const key of Object.keys(before)) {
    if (!nativeOwned(key) && !Object.prototype.hasOwnProperty.call(submitted, key) &&
        Object.prototype.hasOwnProperty.call(committed, key)) return false;
  }
  return Object.entries(submitted).every(([key, value]) => nativeOwned(key) ||
    equalSettingsDocuments(before[key], value) ||
    (Object.prototype.hasOwnProperty.call(committed, key) && settingsDeltaCommitted(before[key], value, committed[key], false)));
}

const inputSettingsListeners=new Set<() => void>();
/** Observe the existing native CAS receipt without issuing a second durable write. */
export function acceptNativeInputSettingsCommit(value: unknown, revision: number, hash: string): boolean {
  if (!isObject(value) || !Number.isSafeInteger(revision) || revision < 0 || value.revision !== revision ||
      typeof hash !== 'string' || computeInputConfigHash(value) !== hash) return false;
  if (cache && cache.input.revision > revision) return false;
  if (cache) {
    const next = normalize({ ...cache, input: snapshotSettingsData(value) });
    if (computeInputConfigHash(next.input) !== hash) return false;
    cache = next;
  }
  cacheStamp = null;
  for (const listener of inputSettingsListeners) { try { listener(); } catch {} }
  return true;
}

export function onInputSettingsChanged(listener:()=>void):()=>void {inputSettingsListeners.add(listener);return()=>{inputSettingsListeners.delete(listener);};}
async function writeSettings(
  next: UnifiedSettings,
  baseline: JsonObject,
  generation = settingsGeneration,
  path = SETTINGS_FILE,
  initialize = false,
  expectedInputRevision?: number,
  validatedRead?: () => void,
): Promise<UnifiedSettings> {
  assertSettingsGeneration(generation);
  if (!settingsLocationResolved || (!validatedRead && recoveryState.status === 'runtime-fallback' &&
      (recoveryState.reason === 'unsupported' || recoveryState.reason === 'read-failed'))) {
    throw new Error(recoveryState.message || '尚未读取可写的配置文档');
  }
  try {
    validatedRead?.();
    const content = JSON.stringify(next, null, 2);
    const result = await boundedSettingsIO(settingsStore.write(path, content, {
      baseline, initialize, ...(expectedInputRevision === undefined ? {} : { expectedInputRevision }),
    }), '统一配置写入');
    assertSettingsGeneration(generation);
    validatedRead?.();
    if (typeof result === 'object' && !result.ok && result.reason === 'revision-conflict') {
      throw new InputSettingsCommitConflict(normalize(JSON.parse(result.content)));
    }
    if (typeof result === 'object' && !result.ok && result.reason === 'virtual-start-protection-active') {
      throw new VirtualStartProtectionActive(normalize(JSON.parse(result.content)), result.startProtection);
    }
    if (!result || (typeof result === 'object' && !result.ok)) throw new Error(`统一配置写入失败: ${path}`);
    // Cache the document actually committed under the native lock, never the
    // pre-commit frontend snapshot (native writers may have advanced it).
    const committed = typeof result === 'object' ? JSON.parse(result.content) : await readJson(path);
    assertSettingsGeneration(generation);
    validatedRead?.();
    if (!isObject(committed) || (expectedInputRevision !== undefined && !isObject(committed.input))) {
      throw new Error("输入配置写入回执缺少可验证的实际文档");
    }
    cacheStamp = null;
    assertCompatibleSchema(committed);
    if (!initialize && expectedInputRevision === undefined && !settingsDeltaCommitted(baseline, next, committed)) {
      throw new Error('配置写入回执未确认请求的更改');
    }
    const normalized=normalize(committed);
    recoveryState = { status: 'ready', reason: null, source: 'main', persisted: true, message: '' };
    if(JSON.stringify(normalized.input)!==JSON.stringify(baseline.input))for(const listener of inputSettingsListeners){try{listener();}catch{}}
    return normalized;
  } catch (error) {
    assertSettingsGeneration(generation);
    validatedRead?.();
    if (!(error instanceof InputSettingsCommitConflict) && !(error instanceof VirtualStartProtectionActive)) {
      recoveryState = { status: 'runtime-fallback', reason: 'write-failed',
        source: cache ? 'cache' : 'defaults', persisted: recoveryState.persisted,
        message: String((error as Error)?.message || error) };
      cacheStamp = null;
    }
    throw error;
  }
}

export async function readSettingsSection<T extends JsonObject = JsonObject>(section: SettingsSection): Promise<T> {
  const settings = await loadSettings();
  return structuredClone((settings[section] || {}) as T);
}

export async function saveSettingsSection(section: SettingsSection, value: JsonObject, expectedGeneration = settingsGeneration): Promise<void> {
  assertSettingsGeneration(expectedGeneration);
  if (section === 'input') throw new Error('input section requires compareAndSwapInputSettings');
  const snapshot = snapshotSettingsData(value); // Capture at submission, not after queue wait.
  const generation = settingsGeneration;
  const run = writeQueue.then(async () => {
    assertSettingsGeneration(generation);
    const current = await loadSettings();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    const next = normalize(current);
    (next as any)[section] = mergeSettings((next as any)[section], snapshot);
    cache = await writeSettings(next, current, generation, path);
  });
  writeQueue = run.catch(() => {});
  await run;
}

/** Atomically replace the shared input snapshot if its revision is unchanged. */
export async function compareAndSwapInputSettings(
  expectedRevision: number,
  value: JsonObject,
  isCurrent?: () => boolean,
  options: { globalShortcuts?: boolean } = {},
): Promise<SettingsCasResult> {
  if (!Number.isSafeInteger(expectedRevision) || expectedRevision < 0) {
    throw new Error('Invalid expected input revision');
  }
  const patch = snapshotSettingsData(value);
  let result: SettingsCasResult | undefined;
  const generation = settingsGeneration;
  const run = writeQueue.then(async () => {
    assertSettingsGeneration(generation);
    if (!settingsLocationResolved) await loadSettings();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    // 裁决20 §3（2026-09-18）：CAS 必须对**磁盘现值**判定。native 侧（统一日志
    // 开关、测试命令、迁移）会推进 input.revision；只比对内存缓存会让缓存里的过期
    // 快照通过，随后的整文档写盘就把 input（persona/gyro 等）回退。磁盘不可读时
    // 才退回缓存。
    let onDisk = await readJson(path).catch(() => null);
    assertSettingsGeneration(generation);
    try { if (onDisk) assertCompatibleSchema(onDisk); } catch { onDisk = null; }
    // A first CAS must complete legacy import before it can publish the receipt.
    const diskReady = onDisk && hasCompletedSettingsBaseline(onDisk);
    const current = diskReady ? normalize(onDisk) : await loadSettings();
    assertSettingsGeneration(generation);
    const snapshot = current.input;
    if (!onDisk || (!diskReady && recoveryState.status !== 'ready')) {
      result = { ok: false, reason: 'write-failed', currentRevision: snapshot.revision, value: snapshotSettingsData(snapshot) };
      return;
    }
    if (diskReady) recoveryState = { status: 'ready', reason: null, source: 'main', persisted: true, message: '' };
    const globalPatch = prepareGlobalShortcutEdit(patch);
    // Recheck at the queued commit boundary, after disk I/O, not only in the caller.
    if (isCurrent && !isCurrent()) {
      result = { ok: false, reason: 'cancelled', currentRevision: snapshot.revision, value: structuredClone(snapshot) };
      return;
    }
    const decision = evaluateInputCasWrite(snapshot, expectedRevision, globalPatch, true);
    if (decision.ok === false) {
      // 单调接受：磁盘版本不旧于缓存时把它并入缓存，令下一次 UI 写入基于最新
      // 版本成功（冲突结果仍如实返回给调用方）。
      if (!cache || (current.input.revision ?? -1) >= (cache.input.revision ?? -1)) cache = current;
      result = options.globalShortcuts ? { ...decision, globalShortcutRules: globalShortcutContainer(current) } : decision;
      return;
    }
    const next = normalize(current);
    next.input = decision.value;
    try {
      const committed = await writeSettings(next, current, generation, path, false, expectedRevision);
      // A native/legacy reply must prove this input transaction was committed.
      // Never report a stale-input drop as a successful shortcut save.
      if (committed.input.revision !== decision.revision ||
          computeInputConfigHash(committed.input) !== computeInputConfigHash(decision.value)) {
        throw new InputSettingsCommitConflict(committed);
      }
      Object.assign(next, committed);
    } catch (error) {
      if (error instanceof InputSettingsCommitConflict || error instanceof VirtualStartProtectionActive) {
        cache = error.document; cacheStamp = null;
        result = { ok: false, reason: error instanceof VirtualStartProtectionActive ? error.reason : 'revision-conflict',
          currentRevision: error.document.input.revision, value: snapshotSettingsData(error.document.input),
          ...(options.globalShortcuts ? { globalShortcutRules: globalShortcutContainer(error.document) } : {}),
          ...(error instanceof VirtualStartProtectionActive ? { startProtection: error.startProtection } : {}) };
      } else result = evaluateInputCasWrite(snapshot, expectedRevision, patch, false);
      return;
    }
    cache = next;
    result = { ok: true, revision: next.input.revision, value: structuredClone(next.input),
      ...(options.globalShortcuts ? { globalShortcutRules: globalShortcutContainer(next) } : {}) };
  });
  writeQueue = run.catch(() => {});
  await run;
  return result!;
}

/** Record an exact Host ACK. This is not an applied revision yet. */
export async function recordInputHostAcknowledgement(
  configRevision: number,
  configHash: string,
): Promise<boolean> {
  if (!Number.isSafeInteger(configRevision) || configRevision < 0 || !configHash) throw new Error('Invalid input Host acknowledgement');
  let accepted = false;
  const generation = settingsGeneration;
  const run = writeQueue.then(async () => {
    assertSettingsGeneration(generation);
    const current = await loadSettings();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    if (current.input.revision !== configRevision || current.input.pendingConfigHash !== configHash) return;
    const next = normalize(current);
    next.input.hostAcknowledgedRevision = configRevision;
    next.input.hostAcknowledgedConfigHash = configHash;
    next.input.applyStatus = 'host-acknowledged';
    cache = await writeSettings(next, current, generation, path);
    accepted = true;
  });
  writeQueue = run.catch(() => {});
  await run;
  return accepted;
}

/**
 * Advance appliedRevision only after the Host has ACKed and a first
 * same-generation valid frame has been published. Callers must provide the
 * exact durable hash from the T10 request; stale ACKs cannot overwrite newer
 * settings.
 */
export async function recordInputAppliedRevision(
  configRevision: number,
  configHash: string,
): Promise<boolean> {
  if (!Number.isSafeInteger(configRevision) || configRevision < 0 || !configHash) throw new Error('Invalid input activation');
  let accepted = false;
  const generation = settingsGeneration;
  const run = writeQueue.then(async () => {
    assertSettingsGeneration(generation);
    const current = await loadSettings();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    const input = current.input;
    if (input.revision !== configRevision || input.pendingConfigHash !== configHash ||
        input.hostAcknowledgedRevision !== configRevision || input.hostAcknowledgedConfigHash !== configHash ||
        input.applyStatus !== 'host-acknowledged') return;
    const next = normalize(current);
    next.input.appliedRevision = configRevision;
    next.input.appliedConfigHash = configHash;
    next.input.applyStatus = 'active';
    cache = await writeSettings(next, current, generation, path);
    accepted = true;
  });
  writeQueue = run.catch(() => {});
  await run;
  return accepted;
}

/** Keep a failed/unsafe application visibly non-active without changing the last active revision. */
export async function recordInputActivationFailure(
  configRevision: number,
  configHash: string,
  status: Extract<InputApplyStatus, 'rejected' | 'safe-stop'>,
): Promise<boolean> {
  if (!Number.isSafeInteger(configRevision) || configRevision < 0 || !configHash) throw new Error('Invalid input activation failure');
  let accepted = false;
  const generation = settingsGeneration;
  const run = writeQueue.then(async () => {
    assertSettingsGeneration(generation);
    const current = await loadSettings();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    if (current.input.revision !== configRevision || current.input.pendingConfigHash !== configHash) return;
    const next = normalize(current);
    next.input.applyStatus = status;
    cache = await writeSettings(next, current, generation, path);
    accepted = true;
  });
  writeQueue = run.catch(() => {});
  await run;
  return accepted;
}

/** Replace one section instead of recursively merging it.
 *
 * This is intentionally narrow: standalone user documents use it to retire
 * a legacy section after a one-time migration. Normal feature saves continue
 * to use saveSettingsSection so unknown fields remain forward-compatible.
 */
export async function replaceSettingsSection(section: SettingsSection, value: JsonObject): Promise<void> {
  if (section === 'input') throw new Error('input section requires compareAndSwapInputSettings');
  const snapshot = snapshotSettingsData(value);
  const generation = settingsGeneration;
  const run = writeQueue.then(async () => {
    assertSettingsGeneration(generation);
    const current = await loadSettings();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    const next = normalize(current);
    (next as any)[section] = snapshot;
    cache = await writeSettings(next, current, generation, path);
  });
  writeQueue = run.catch(() => {});
  await run;
}

export async function updateSettings(mutator: (settings: UnifiedSettings) => void): Promise<void> {
  const generation = settingsGeneration;
  const run = writeQueue.then(async () => {
    assertSettingsGeneration(generation);
    const current = await loadSettings();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    const next = normalize(current);
    mutator(next);
    const snapshot = normalize(next);
    cache = await writeSettings(snapshot, current, generation, path);
  });
  writeQueue = run.catch(() => {});
  await run;
}

/** Only explicit recovery UI may call this; never reset on load/read failure. */
export async function resetApplicationSettings(): Promise<{ backups: string[]; protectedDataRecovered: boolean }> {
  const generation = getSettingsGeneration();
  if (!settingsLocationResolved) await resolveSettingsLocation();
  assertSettingsGeneration(generation);
  if (!settingsStore.reset) throw new Error('当前后端不支持安全配置恢复');
  const result = await settingsStore.reset(SETTINGS_FILE, normalize({}));
  assertSettingsGeneration(generation);
  if (!result?.ok) throw new Error('配置恢复没有成功，原设置未清理');
  clearSettingsCache();
  return result;
}

export function clearSettingsCache(): void {
  settingsGeneration++;
  cache = null;
  cacheStamp = null;
  loadPromise = null;
  settingsLocationPromise = null;
  recoveryState = { status: 'ready', reason: null, source: 'main', persisted: false, message: '' };
}
