import { fs, settingsStore } from './api';
import { snapshotSettingsData } from './settingsSnapshot';
import { createDefaultQuickApps } from './quickAppDefaults';
import { normalizeInputStartupPreferences } from './inputStartupPolicy';
import { computeInputConfigHash } from './inputConfigHash';
import { HC_MIN_GYRO_THRESHOLD_DPS } from './hcInputUtils';

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
      const location = await settingsStore.location();
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
}

export type SettingsCasResult =
  | { ok: true; revision: number; value: InputSettingsSnapshot }
  | { ok: false; reason: 'revision-conflict' | 'write-failed' | 'game-override-active' | 'cancelled'; currentRevision: number; value: InputSettingsSnapshot };

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
    videoBatteryPause: true,
    scheduleMonitor: true,
    steamChartAutoRefresh: 'none',
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
    retryOnNonUserWake: true,
    factMonitorEnabled: false,
    sleepPowerPlanOptimizationEnabled: true,
    wakePasswordEnabled: false,
    steamOverlayOffFix: false,
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
    ownership: { schemaVersion: 1, mode: 'ymcc-semantic', frontendSummonVirtualControl: false },
    diagnostics: { inputLoggingEnabled: false },
  },
  extensions: {},
};

let settingsGeneration = 0;
let cache: UnifiedSettings | null = null;
let cacheStamp: string | null = null;

export function getSettingsGeneration(): number { return settingsGeneration; }

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

export function normalizeSettings(raw: unknown): UnifiedSettings {
  const source = isObject(raw) ? raw : {};
  const merged = mergeSettings(DEFAULTS, source) as UnifiedSettings;
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
  input.appliedRevision = input.appliedRevision == null ? null : (Number.isSafeInteger(Number(input.appliedRevision)) ? Math.max(0, Math.floor(Number(input.appliedRevision))) : null);
  input.appliedConfigHash = typeof input.appliedConfigHash === 'string' ? input.appliedConfigHash : null;
  input.pendingConfigHash = typeof input.pendingConfigHash === 'string' ? input.pendingConfigHash : null;
  input.hostAcknowledgedRevision = input.hostAcknowledgedRevision == null ? null : (Number.isSafeInteger(Number(input.hostAcknowledgedRevision)) ? Math.max(0, Math.floor(Number(input.hostAcknowledgedRevision))) : null);
  input.hostAcknowledgedConfigHash = typeof input.hostAcknowledgedConfigHash === 'string' ? input.hostAcknowledgedConfigHash : null;
  const legacyApplyStatus = String(input.applyStatus);
  if (legacyApplyStatus === 'pending') input.applyStatus = 'saved-pending';
  else if (legacyApplyStatus === 'applied') input.applyStatus = 'active';
  else if (!['unknown', 'saved-pending', 'host-acknowledged', 'active', 'rejected', 'safe-stop'].includes(legacyApplyStatus)) input.applyStatus = 'unknown';
  const target = isObject(input.outputTarget) ? input.outputTarget : {};
  target.schemaVersion = Number.isSafeInteger(Number(target.schemaVersion)) ? Math.max(1, Math.floor(Number(target.schemaVersion))) : 1;
  target.revision = Number.isSafeInteger(Number(target.revision)) ? Math.max(0, Math.floor(Number(target.revision))) : 0;
  if (!['CLOSED', 'UNENCLOSED', 'NOT-APPLICABLE'].includes(String(target.closure))) target.closure = 'UNENCLOSED';
  if (!['disabled', 'dualshock4', 'xbox360', 'steamdeck', 'dualsense', 'elite', 'dualsense-edge'].includes(String(target.persona))) target.persona = 'disabled';
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
  if (!['virtual-stick', 'ds4-imu', 'disabled'].includes(String(motion.outputMode))) motion.outputMode = 'disabled';
  if (!['local-space', 'player-space', 'world-space', 'joystick-steering'].includes(String(motion.motionInput))) motion.motionInput = 'local-space';
  // HC Profile.SteeringAxis: Roll=0, Yaw=1, Auto=2 (default Roll).
  if (!['roll', 'yaw', 'auto'].includes(String(motion.steeringAxis))) motion.steeringAxis = 'roll';
  if (!['off', 'on', 'toggle'].includes(String(motion.motionMode))) motion.motionMode = 'off';
  motion.motionTrigger = typeof motion.motionTrigger === 'string' && motion.motionTrigger.trim() ? motion.motionTrigger : null;
  const finite = (value: unknown, fallback: number, min: number, max: number) => {
    const n = Number(value);
    return Number.isFinite(n) ? Math.min(max, Math.max(min, n)) : fallback;
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
    const key = Number(node[0]);
    const value = Number(node[1]);
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
    const ckey = Number(node[0]);
    const cvalue = Number(node[1]);
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
  return merged;
}

const normalize = normalizeSettings;

async function readSettingsText(path: string, maxBytes: number): Promise<string | null> {
  try {
    return await fs.readTextFile(path, maxBytes);
  } catch (e) {
    // Missing files can use migration/defaults. Existing unreadable files must
    // NOT be archived as corrupt or replaced by an empty document.
    if (await fs.exists(path)) {
      throw new Error(`读取配置文件失败：${path}；${(e as Error).message}`);
    }
    return null;
  }
}

async function readJson(path: string, maxBytes = 4 * 1024 * 1024): Promise<JsonObject | null> {
  const raw = await readSettingsText(path, maxBytes);
  if (raw === null) return null;
  try {
    // Only JSON/schema failures are corrupt; I/O errors are surfaced above.
    const normalized = raw.charCodeAt(0) === 0xFEFF ? raw.slice(1) : raw;
    const parsed = JSON.parse(normalized);
    return isObject(parsed) ? parsed : null;
  } catch {
    return null;
  }
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
  const value = Number(raw.trim());
  return Number.isFinite(value) ? value : null;
}

async function migrateLegacySettings(dir = settingsDir): Promise<UnifiedSettings> {
  let next = structuredClone(DEFAULTS);
  const set = (section: SettingsSection, value: unknown) => {
    if (isObject(value)) (next as any)[section] = mergeSettings((next as any)[section], value);
  };
  const ui = await readLegacyJson('ui-settings.json', dir);
  const summon = await readLegacyJson('summon.json', dir);
  const music = await readLegacyJson('music_player.json', dir);
  const schedule = await readLegacyJson('performance-schedule.json', dir);
  const control = await readLegacyJson('control-config.json', dir);
  const legacyTdp = await readLegacyNumber('tdp.txt', dir);
  const legacyFps = await readLegacyNumber('FPS-ac.txt', dir);
  const float = await readLegacyJson('autofloat.json', dir);
  const tdpApply = await readLegacyJson('tdp-auto-apply.json', dir);
  const cpuProfiles = await readLegacyJson('cpu_profiles.json', dir);
  const cpuAutostart = await readLegacyJson('cpu_autostart.json', dir);
  const cpuAutoEnable = await readLegacyJson('cpu_auto_enable.json', dir);
  const cpuLock = await readLegacyJson('cpu_lock.json', dir);
  const sleep = await readLegacyJson('Sleep\\sleepguard.json', dir);
  const apps = await readLegacyLaunchApps(dir);
  const boot = await readLegacyJson('boot_config.json', dir);
  const power = await readLegacyJson('yeman-power-scheme.json', dir);
  const tray = await readLegacyJson('tray_resident.json', dir);
  const autoclose = await readLegacyJson('autoclose.json', dir);
  const asset = await readLegacyJson('ui-background\\background.json', dir);
  const dynamic = await readLegacyJson('ui-background\\dynamic-online.json', dir);

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
    active: cpuProfiles?.active,
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

async function archiveCorruptMain(path: string): Promise<void> {
  if (await fs.exists(path)) {
    const stamp = new Date().toISOString().replace(/[-:TZ.]/g, '').slice(0, 14);
    const ok = await fs.rename(path, `${path}.corrupt-${stamp}-${Math.random().toString(36).slice(2, 8)}`);
    if (!ok) throw new Error(`无法保留损坏的配置文件：${path}`);
  }
}

function assertCompatibleSchema(document: JsonObject): void {
  const schema = Number(document.schemaVersion ?? 0);
  // Missing/schema 0 documents are legacy. Do not reinterpret an unknown or
  // future version as schema 1, including a recoverable backup document.
  if (!Number.isInteger(schema) || schema < 0 || schema > 1) {
    throw new Error(`配置版本 ${String(document.schemaVersion)} 无法由本程序兼容读取。原文件已保留，请使用匹配版本或选择备份后重置应用配置。`);
  }
}

/** Validate on explicit reads, not a timer: native checks a 100ns file stamp and
 * returns no JSON for an unchanged document. Coalesced callers own snapshots. */
export async function loadSettings(): Promise<UnifiedSettings> {
  if (!loadPromise) {
    const generation = settingsGeneration;
    const task = (async () => {
      if (!settingsLocationResolved) await resolveSettingsLocation();
      assertSettingsGeneration(generation);
      const path = SETTINGS_FILE;
      const backupPath = SETTINGS_BACKUP_FILE;
      let main: JsonObject | null;
      let stamp: string | null = null;
      let mainExists = false;
      if (settingsStore.read) {
        const result = await settingsStore.read(path, cache ? cacheStamp : null);
        assertSettingsGeneration(generation);
        if (result.unchanged && cache) return cache;
        stamp = result.stamp;
        mainExists = result.stamp !== 'missing';
        try {
          const raw = result.content || '';
          const parsed = JSON.parse(raw.charCodeAt(0) === 0xFEFF ? raw.slice(1) : raw);
          main = isObject(parsed) ? parsed : null;
        } catch { main = null; }
      } else {
        // Isolated test/backward-compatible API adapters have no metadata read.
        main = await readJson(path);
        mainExists = main !== null || await fs.exists(path);
      }
      assertSettingsGeneration(generation);
      let next: UnifiedSettings;
      if (main) {
        assertCompatibleSchema(main);
        const requiredSections: SettingsSection[] = [
          'ui', 'background', 'music', 'gamepad', 'performanceSchedule',
          'gameCustom', 'tdp', 'cpu', 'sleep', 'quickApps', 'startupDesired',
          'power', 'tray', 'autoclose', 'fan', 'input',
        ];
        const needsMigration = requiredSections.some(section => !Object.prototype.hasOwnProperty.call(main, section));
        const legacy = needsMigration ? await migrateLegacySettings() : DEFAULTS;
        assertSettingsGeneration(generation);
        next = normalize(mergeSettings(legacy, main));
        if (Number(main.schemaVersion) !== next.schemaVersion || needsMigration) {
          next = await writeSettings(next, main, generation, path, true);
          stamp = null;
        }
      } else {
        const backup = await readJson(backupPath);
        assertSettingsGeneration(generation);
        if (backup) assertCompatibleSchema(backup);
        else if (mainExists || await fs.exists(backupPath)) {
          // A genuine first run may initialize defaults. Existing invalid
          // files without a compatible backup require explicit user recovery;
          // preserve BOTH originals, never silently archive/reset them here.
          throw new Error('共享配置损坏或无法解析，且没有可兼容的备份。原文件已保留，请选择备份后重置应用配置。');
        }
        assertSettingsGeneration(generation);
        await archiveCorruptMain(path);
        assertSettingsGeneration(generation);
        next = backup ? normalize(backup) : await migrateLegacySettings();
        assertSettingsGeneration(generation);
        next = await writeSettings(next, {}, generation, path, true);
        stamp = null;
      }
      assertSettingsGeneration(generation);
      cache = next;
      cacheStamp = stamp;
      return next;
    })();
    loadPromise = task;
  }
  const task = loadPromise;
  try { return snapshotSettingsData(await task); }
  finally { if (loadPromise === task) loadPromise = null; }
}

class InputSettingsCommitConflict extends Error {
  constructor(readonly document: UnifiedSettings) { super('输入配置在提交前已变更'); }
}

async function writeSettings(
  next: UnifiedSettings,
  baseline: JsonObject,
  generation = settingsGeneration,
  path = SETTINGS_FILE,
  initialize = false,
  expectedInputRevision?: number,
): Promise<UnifiedSettings> {
  assertSettingsGeneration(generation);
  const content = JSON.stringify(next, null, 2);
  const result = await settingsStore.write(path, content, {
    baseline, initialize, ...(expectedInputRevision === undefined ? {} : { expectedInputRevision }),
  });
  assertSettingsGeneration(generation);
  if (typeof result === 'object' && !result.ok && result.reason === 'revision-conflict') {
    throw new InputSettingsCommitConflict(normalize(JSON.parse(result.content)));
  }
  if (!result || (typeof result === 'object' && !result.ok)) throw new Error(`统一配置写入失败: ${path}`);
  // Cache the document actually committed under the native lock, never the
  // pre-commit frontend snapshot (native writers may have advanced it).
  const committed = typeof result === 'object' ? JSON.parse(result.content) : await readJson(path);
  assertSettingsGeneration(generation);
  if (expectedInputRevision !== undefined && (!isObject(committed) || !isObject(committed.input))) {
    throw new Error("输入配置写入回执缺少可验证的实际文档");
  }
  cacheStamp = null;
  return normalize(committed || next);
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
    if (!settingsLocationResolved) await resolveSettingsLocation();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    const current = await loadSettings();
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
): Promise<SettingsCasResult> {
  if (!Number.isSafeInteger(expectedRevision) || expectedRevision < 0) {
    throw new Error('Invalid expected input revision');
  }
  const patch = snapshotSettingsData(value);
  let result: SettingsCasResult | undefined;
  const generation = settingsGeneration;
  const run = writeQueue.then(async () => {
    assertSettingsGeneration(generation);
    if (!settingsLocationResolved) await resolveSettingsLocation();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    // 裁决20 §3（2026-09-18）：CAS 必须对**磁盘现值**判定。native 侧（统一日志
    // 开关、测试命令、迁移）会推进 input.revision；只比对内存缓存会让缓存里的过期
    // 快照通过，随后的整文档写盘就把 input（persona/gyro 等）回退。磁盘不可读时
    // 才退回缓存。
    const onDisk = await readJson(SETTINGS_FILE).catch(() => null);
    if (onDisk) assertCompatibleSchema(onDisk);
    const current = onDisk ? normalize(onDisk) : await loadSettings();
    const snapshot = current.input;
    // Recheck at the queued commit boundary, after disk I/O, not only in the caller.
    if (isCurrent && !isCurrent()) {
      result = { ok: false, reason: 'cancelled', currentRevision: snapshot.revision, value: structuredClone(snapshot) };
      return;
    }
    const decision = evaluateInputCasWrite(snapshot, expectedRevision, patch, true);
    if (decision.ok === false) {
      // 单调接受：磁盘版本不旧于缓存时把它并入缓存，令下一次 UI 写入基于最新
      // 版本成功（冲突结果仍如实返回给调用方）。
      if (!cache || (current.input.revision ?? -1) >= (cache.input.revision ?? -1)) cache = current;
      result = decision;
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
      if (error instanceof InputSettingsCommitConflict) {
        cache = error.document; cacheStamp = null;
        result = { ok: false, reason: 'revision-conflict', currentRevision: error.document.input.revision,
          value: snapshotSettingsData(error.document.input) };
      } else result = evaluateInputCasWrite(snapshot, expectedRevision, patch, false);
      return;
    }
    cache = next;
    result = { ok: true, revision: next.input.revision, value: structuredClone(next.input) };
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
    if (!settingsLocationResolved) await resolveSettingsLocation();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    const current = await loadSettings();
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
    if (!settingsLocationResolved) await resolveSettingsLocation();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    const current = await loadSettings();
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
    if (!settingsLocationResolved) await resolveSettingsLocation();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    const current = await loadSettings();
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
    if (!settingsLocationResolved) await resolveSettingsLocation();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    const current = await loadSettings();
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
    if (!settingsLocationResolved) await resolveSettingsLocation();
    assertSettingsGeneration(generation);
    const path = SETTINGS_FILE;
    const current = await loadSettings();
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
}
