// Retained CCD/undervolt autostart implementation; inactive during UI retirement.
//
// 设计：
//  - 开关状态 + 当前选中的档位 记录到 C:\SOFT\YeMan\PowerControl\cpu_autostart.json
//  - 打开 YeManCC 30 秒后（由 App.vue 触发），按记录状态自行套用 CCD / 降压。
//  - 30 秒延迟是开机保护：避免降压过猛（如 risk 档）在开机瞬间直接死机。
//  - 纯前端实现，读写走 native fs.* IPC，无需重编译壳。

import { setCcdMode, setUndervolt, uvPresetValue } from './cpuctl';
import { LEGACY_CCD_UNDERVOLT_ACTIVE } from './legacyCpuFeaturePolicy';
import { readSettingsSection, saveSettingsSection } from './settingsRepository';

const FILE = 'C:\\SOFT\\YeMan\\PowerControl\\cpu_autostart.json';

export type UvVendor = 'amd' | 'intel' | '';
export interface CpuAutostart {
  ccd: { enabled: boolean; mode: number };
  uv: { enabled: boolean; preset: string; vendor: UvVendor };
}

function defaultConfig(): CpuAutostart {
  return {
    ccd: { enabled: false, mode: 0 },
    uv: { enabled: false, preset: 'off', vendor: '' },
  };
}

export async function readCpuAutostart(): Promise<CpuAutostart> {
  if (!LEGACY_CCD_UNDERVOLT_ACTIVE) return defaultConfig();
  try {
    const j = await readSettingsSection<any>('cpu');
    const a = j.autostart;
    if (!a || typeof a !== 'object') return defaultConfig();
    return {
      ccd: {
        enabled: !!(a?.ccd?.enabled),
        mode: Number(a?.ccd?.mode ?? 0),
      },
      uv: {
        enabled: !!(a?.uv?.enabled),
        preset: typeof a?.uv?.preset === 'string' ? a.uv.preset : 'off',
        vendor: a?.uv?.vendor === 'amd' || a?.uv?.vendor === 'intel' ? a.uv.vendor : '',
      },
    };
  } catch {
    return defaultConfig();
  }
}

// Each save owns only its branch. Native merges the branch under its lock,
// so a simultaneous CCD/UV save cannot replay a stale whole autostart object.
export async function writeCcdAutostart(enabled: boolean, mode: number): Promise<void> {
  if (!LEGACY_CCD_UNDERVOLT_ACTIVE) return;
  await saveSettingsSection('cpu', { autostart: { ccd: { enabled, mode } } });
}
export async function writeUvAutostart(enabled: boolean, preset: string, vendor: UvVendor): Promise<void> {
  if (!LEGACY_CCD_UNDERVOLT_ACTIVE) return;
  await saveSettingsSection('cpu', { autostart: { uv: { enabled, preset, vendor } } });
}

// 由 App.vue 在启动 30 秒后调用：按记录状态自行启用 CCD / 降压（不抛错，单条失败不影响另一条）
export async function applyCpuAutostart(): Promise<void> {
  if (!LEGACY_CCD_UNDERVOLT_ACTIVE) return;
  const cfg = await readCpuAutostart();
  if (cfg.ccd.enabled) {
    try {
      const mode = Number(cfg.ccd.mode);
      if (Number.isInteger(mode) && mode >= 0) await setCcdMode(mode);
    } catch {
      /* 忽略单条失败 */
    }
  }
  if (cfg.uv.enabled) {
    try {
      await setUndervolt(uvPresetValue(cfg.uv.vendor, cfg.uv.preset));
    } catch {
      /* 忽略单条失败 */
    }
  }
}
