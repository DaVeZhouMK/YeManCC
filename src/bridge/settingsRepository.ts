import { fs, settingsStore } from './api';

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

export function setSettingsDirectory(dir: string): void {
  settingsDir = dir.replace(/\//g, '\\').replace(/[\\]+$/, '');
  SETTINGS_FILE = `${settingsDir}\\yeman-settings.json`;
  SETTINGS_BACKUP_FILE = `${SETTINGS_FILE}.bak`;
  clearSettingsCache();
}

export type JsonObject = Record<string, any>;
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
  | 'autoclose';

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
  },
  quickApps: { apps: [] },
  startupDesired: {
    bootControlCenter: true,
    rtss: false,
    energyStar: false,
    memoryCleanup: false,
    steamCommunity: false,
  },
  power: { scheme: {} },
  tray: { resident: false },
  autoclose: { enabled: false, procs: [] },
  extensions: {},
};

let cache: UnifiedSettings | null = null;
let loadPromise: Promise<UnifiedSettings> | null = null;
let writeQueue: Promise<void> = Promise.resolve();

function isObject(value: unknown): value is JsonObject {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

/** Deep merge objects, replace arrays/primitives, and retain unknown keys. */
export function mergeSettings<T>(base: T, patch: any): T {
  if (!isObject(base) || !isObject(patch)) return (patch === undefined ? base : patch) as T;
  const out: JsonObject = { ...(base as JsonObject) };
  for (const [key, value] of Object.entries(patch)) {
    if (isObject(value) && isObject(out[key])) out[key] = mergeSettings(out[key], value);
    else out[key] = value;
  }
  return out as T;
}

function normalize(raw: unknown): UnifiedSettings {
  const source = isObject(raw) ? raw : {};
  const merged = mergeSettings(DEFAULTS, source) as UnifiedSettings;
  merged.schemaVersion = Number.isFinite(Number(merged.schemaVersion))
    ? Math.max(1, Math.floor(Number(merged.schemaVersion)))
    : 1;
  if (typeof merged.baselineId !== 'string' || !merged.baselineId) {
    merged.baselineId = DEFAULTS.baselineId;
  }
  return merged;
}

async function readJson(path: string, maxBytes = 4 * 1024 * 1024): Promise<JsonObject | null> {
  try {
    const raw = await fs.readTextFile(path, maxBytes);
    // Windows PowerShell/编辑器常写 UTF-8 BOM；JSON.parse 不接受前导
    // U+FEFF，统一在共享设置读取边界去掉，避免整份设置回退为默认值。
    const normalized = raw.charCodeAt(0) === 0xFEFF ? raw.slice(1) : raw;
    const parsed = JSON.parse(normalized);
    return isObject(parsed) ? parsed : null;
  } catch {
    return null;
  }
}

async function readLegacyJson(name: string): Promise<JsonObject | null> {
  return readJson(`${settingsDir}\\${name}`);
}

async function readLegacyNumber(name: string): Promise<number | null> {
  try {
    const raw = await fs.readTextFile(`${settingsDir}\\${name}`, 128);
    const value = Number(raw.trim());
    return Number.isFinite(value) ? value : null;
  } catch {
    return null;
  }
}

async function migrateLegacySettings(): Promise<UnifiedSettings> {
  let next = structuredClone(DEFAULTS);
  const set = (section: SettingsSection, value: unknown) => {
    if (isObject(value)) next[section] = mergeSettings(next[section], value);
  };
  const ui = await readLegacyJson('ui-settings.json');
  const summon = await readLegacyJson('summon.json');
  const music = await readLegacyJson('music_player.json');
  const schedule = await readLegacyJson('performance-schedule.json');
  const control = await readLegacyJson('control-config.json');
  const legacyTdp = await readLegacyNumber('tdp.txt');
  const legacyFps = await readLegacyNumber('FPS-ac.txt');
  const float = await readLegacyJson('autofloat.json');
  const tdpApply = await readLegacyJson('tdp-auto-apply.json');
  const cpuProfiles = await readLegacyJson('cpu_profiles.json');
  const cpuAutostart = await readLegacyJson('cpu_autostart.json');
  const cpuAutoEnable = await readLegacyJson('cpu_auto_enable.json');
  const cpuLock = await readLegacyJson('cpu_lock.json');
  const sleep = await readLegacyJson('Sleep\\sleepguard.json');
  const apps = await readLegacyJson('launch_apps.json');
  const boot = await readLegacyJson('boot_config.json');
  const power = await readLegacyJson('yeman-power-scheme.json');
  const tray = await readLegacyJson('tray_resident.json');
  const autoclose = await readLegacyJson('autoclose.json');
  const asset = await readLegacyJson('ui-background\\background.json');
  const dynamic = await readLegacyJson('ui-background\\dynamic-online.json');

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
  if (Array.isArray(apps)) set('quickApps', { apps });
  else if (isObject(apps)) set('quickApps', apps);
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

async function archiveCorruptMain(): Promise<void> {
  try {
    if (await fs.exists(SETTINGS_FILE)) {
      const stamp = new Date().toISOString().replace(/[-:TZ.]/g, '').slice(0, 14);
      await fs.rename(SETTINGS_FILE, `${SETTINGS_FILE}.corrupt-${stamp}`);
    }
  } catch {
    // A read-only directory must not prevent backup recovery or app startup.
  }
}

export async function loadSettings(): Promise<UnifiedSettings> {
  if (cache) return structuredClone(cache);
  if (!loadPromise) {
    loadPromise = (async () => {
      const mainExists = await fs.exists(SETTINGS_FILE).catch(() => false);
      const main = await readJson(SETTINGS_FILE);
      if (main) {
        // A native startup call may have created only its own section before
        // WebView2 loaded.  Merge missing sections from the legacy files once;
        // values already present in the unified file always win.
        const legacy = await migrateLegacySettings();
        cache = normalize(mergeSettings(legacy, main));
        const requiredSections: SettingsSection[] = [
          'ui', 'background', 'music', 'gamepad', 'performanceSchedule',
          'gameCustom', 'tdp', 'cpu', 'sleep', 'quickApps', 'startupDesired',
          'power', 'tray', 'autoclose',
        ];
        if (Number(main.schemaVersion) !== cache.schemaVersion ||
            requiredSections.some((section) => !Object.prototype.hasOwnProperty.call(main, section))) {
          await writeSettings(cache);
        }
        return structuredClone(cache);
      }
      // Preserve a malformed main document before recovering from backup or
      // migrating legacy files. This makes a failed upgrade diagnosable.
      if (mainExists) await archiveCorruptMain();
      const backup = await readJson(SETTINGS_BACKUP_FILE);
      if (backup) {
        cache = normalize(backup);
        await writeSettings(cache);
        return structuredClone(cache);
      }
      const migrated = await migrateLegacySettings();
      cache = migrated;
      await writeSettings(cache);
      return structuredClone(cache);
    })();
  }
  try {
    return await loadPromise;
  } finally {
    loadPromise = null;
  }
}

async function writeSettings(next: UnifiedSettings): Promise<void> {
  const content = JSON.stringify(next, null, 2);
  const ok = await settingsStore.write(SETTINGS_FILE, content);
  if (!ok) throw new Error(`统一配置写入失败: ${SETTINGS_FILE}`);
}

export async function readSettingsSection<T extends JsonObject = JsonObject>(section: SettingsSection): Promise<T> {
  const settings = await loadSettings();
  return structuredClone((settings[section] || {}) as T);
}

export async function saveSettingsSection(section: SettingsSection, value: JsonObject): Promise<void> {
  const run = writeQueue.then(async () => {
    const current = await loadSettings();
    const next = normalize(current);
    next[section] = mergeSettings(next[section], value);
    await writeSettings(next);
    cache = next;
  });
  writeQueue = run.catch(() => {});
  await run;
}

/** Replace one section instead of recursively merging it.
 *
 * This is intentionally narrow: standalone user documents use it to retire
 * a legacy section after a one-time migration. Normal feature saves continue
 * to use saveSettingsSection so unknown fields remain forward-compatible.
 */
export async function replaceSettingsSection(section: SettingsSection, value: JsonObject): Promise<void> {
  const run = writeQueue.then(async () => {
    const current = await loadSettings();
    const next = normalize(current);
    next[section] = structuredClone(value);
    await writeSettings(next);
    cache = next;
  });
  writeQueue = run.catch(() => {});
  await run;
}

export async function updateSettings(mutator: (settings: UnifiedSettings) => void): Promise<void> {
  const run = writeQueue.then(async () => {
    const current = await loadSettings();
    const next = normalize(current);
    mutator(next);
    await writeSettings(normalize(next));
    cache = normalize(next);
  });
  writeQueue = run.catch(() => {});
  await run;
}

export function clearSettingsCache(): void {
  cache = null;
}
