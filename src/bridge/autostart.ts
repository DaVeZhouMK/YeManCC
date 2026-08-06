// autostart.ts — CPU 核心控制(CCD) / CPU 降压(Undervolt) 的「30秒自行启用」持久化与开机套用
//
// 设计：
//  - 开关状态 + 当前选中的档位 记录到 C:\SOFT\YeMan\PowerControl\cpu_autostart.json
//  - 打开 YeManCC 30 秒后（由 App.vue 触发），按记录状态自行套用 CCD / 降压。
//  - 30 秒延迟是开机保护：避免降压过猛（如 risk 档）在开机瞬间直接死机。
//  - 纯前端实现，读写走 native fs.* IPC，无需重编译壳。

import { fs } from './api';
import { setCcdMode, setUndervolt, uvPresetValue } from './cpuctl';

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
  try {
    const s = await fs.readTextFile(FILE);
    if (!s || !s.trim()) return defaultConfig();
    const j = JSON.parse(s);
    return {
      ccd: {
        enabled: !!(j?.ccd?.enabled),
        mode: Number(j?.ccd?.mode ?? 0),
      },
      uv: {
        enabled: !!(j?.uv?.enabled),
        preset: typeof j?.uv?.preset === 'string' ? j.uv.preset : 'off',
        vendor: j?.uv?.vendor === 'amd' || j?.uv?.vendor === 'intel' ? j.uv.vendor : '',
      },
    };
  } catch {
    return defaultConfig();
  }
}

let updateQueue = Promise.resolve();

async function updateConfig(
  mutate: (cfg: CpuAutostart) => void,
): Promise<void> {
  const next = updateQueue.then(async () => {
    const cfg = await readCpuAutostart();
    mutate(cfg);
    await fs.writeTextFileAtomic(FILE, JSON.stringify(cfg, null, 2));
  });
  updateQueue = next.catch(() => {});
  return next;
}

// 更新 CCD 部分（enabled + 当前选中的 mode），其余部分保持不变
export async function writeCcdAutostart(enabled: boolean, mode: number): Promise<void> {
  await updateConfig((cfg) => {
    cfg.ccd.enabled = enabled;
    cfg.ccd.mode = mode;
  });
}

// 更新 降压 部分（enabled + 当前选中的 preset + vendor），其余部分保持不变
export async function writeUvAutostart(enabled: boolean, preset: string, vendor: UvVendor): Promise<void> {
  await updateConfig((cfg) => {
    cfg.uv.enabled = enabled;
    cfg.uv.preset = preset;
    cfg.uv.vendor = vendor;
  });
}

// 由 App.vue 在启动 30 秒后调用：按记录状态自行启用 CCD / 降压（不抛错，单条失败不影响另一条）
export async function applyCpuAutostart(): Promise<void> {
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
