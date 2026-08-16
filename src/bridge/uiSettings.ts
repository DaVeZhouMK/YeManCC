import { readSettingsSection, saveSettingsSection } from './settingsRepository';

export interface UiSettings {
  theme: 'blue-black' | 'red-black' | 'cyberpunk';
  dynamicBackgroundEnabled: boolean;
  backgroundOpacity: number;
  backgroundBlur: number;
  videoBatteryPause: boolean;
  scheduleMonitor: boolean;
  steamChartAutoRefresh: 'none' | 'steam' | 'steamdeck';
}

const DEFAULTS: UiSettings = {
  theme: 'blue-black',
  dynamicBackgroundEnabled: false,
  backgroundOpacity: 0.64,
  backgroundBlur: 0,
  videoBatteryPause: true,
  scheduleMonitor: true,
  steamChartAutoRefresh: 'none',
};

let settings: UiSettings = { ...DEFAULTS };
let loaded = false;
let loadPromise: Promise<void> | null = null;
let writeQueue: Promise<void> = Promise.resolve();

function normalize(input: Partial<UiSettings>): UiSettings {
  const theme = input.theme === 'red-black' || input.theme === 'cyberpunk' ? input.theme : 'blue-black';
  const opacity = Number(input.backgroundOpacity);
  const blur = Number(input.backgroundBlur);
  return {
    theme,
    dynamicBackgroundEnabled: input.dynamicBackgroundEnabled === true,
    backgroundOpacity: Number.isFinite(opacity) ? Math.min(0.8, Math.max(0.2, Math.round(opacity * 100) / 100)) : DEFAULTS.backgroundOpacity,
    backgroundBlur: Number.isFinite(blur) ? Math.min(24, Math.max(0, Math.round(blur))) : DEFAULTS.backgroundBlur,
    videoBatteryPause: input.videoBatteryPause !== false,
    scheduleMonitor: input.scheduleMonitor !== false,
    steamChartAutoRefresh: input.steamChartAutoRefresh === 'steam' || input.steamChartAutoRefresh === 'steamdeck'
      ? input.steamChartAutoRefresh : 'none',
  };
}

function legacySettings(): Partial<UiSettings> {
  const theme = localStorage.getItem('yeman.ui.theme');
  const dynamic = localStorage.getItem('yeman.ui.dynamic-background-enabled');
  const opacity = localStorage.getItem('yeman.ui.background-opacity');
  const blur = localStorage.getItem('yeman.ui.background-blur');
  const pause = localStorage.getItem('yeman.ui.video-battery-pause');
  const monitor = localStorage.getItem('yeman.ui.schedule-monitor');
  return {
    ...(theme ? { theme: theme === 'cyber-red' ? 'red-black' : theme as UiSettings['theme'] } : {}),
    ...(dynamic !== null ? { dynamicBackgroundEnabled: dynamic === 'true' } : {}),
    ...(opacity !== null ? { backgroundOpacity: Number(opacity) } : {}),
    ...(blur !== null ? { backgroundBlur: Number(blur) } : {}),
    ...(pause !== null ? { videoBatteryPause: pause !== 'false' } : {}),
    ...(monitor !== null ? { scheduleMonitor: monitor !== 'false' } : {}),
  };
}

function clearLegacySettings(): void {
  for (const key of [
    'yeman.ui.theme',
    'yeman.ui.dynamic-background-enabled',
    'yeman.ui.background-opacity',
    'yeman.ui.background-blur',
    'yeman.ui.video-battery-pause',
    'yeman.ui.schedule-monitor',
  ]) localStorage.removeItem(key);
}

export function getUiSettings(): UiSettings {
  return { ...settings };
}

export function getUiSetting<K extends keyof UiSettings>(key: K): UiSettings[K] {
  return settings[key];
}

export async function loadUiSettings(): Promise<void> {
  if (loaded) return;
  if (loadPromise) return loadPromise;
  loadPromise = (async () => {
    let fromDisk: Partial<UiSettings> | null = null;
    try {
      fromDisk = await readSettingsSection<Partial<UiSettings>>('ui');
    } catch {
      fromDisk = null;
    }
    const migrated = !fromDisk ? legacySettings() : {};
    settings = normalize({ ...(fromDisk || {}), ...migrated });
    loaded = true;
    if (!fromDisk) await writeUiSettings(settings);
    clearLegacySettings();
    window.dispatchEvent(new CustomEvent('ui-settings:loaded', { detail: getUiSettings() }));
  })();
  try {
    await loadPromise;
  } finally {
    loadPromise = null;
  }
}

export function setUiSettings(patch: Partial<UiSettings>): Promise<void> {
  writeQueue = writeQueue.catch(() => {}).then(async () => {
    await loadUiSettings();
    const next = normalize({ ...settings, ...patch });
    await saveSettingsSection('ui', next);
    settings = next;
    window.dispatchEvent(new CustomEvent('ui-settings:changed', { detail: getUiSettings() }));
  });
  return writeQueue;
}

async function writeUiSettings(value: UiSettings): Promise<void> {
  await saveSettingsSection('ui', value);
}

void loadUiSettings().catch(() => {
  loaded = true;
});
