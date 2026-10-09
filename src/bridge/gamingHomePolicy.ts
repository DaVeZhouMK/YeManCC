// Windows full-screen startup is user intent, not the presence of a YeMan task.
// These defaults were verified by the user on a ROG / 26300.9457. Never apply on read.
export const GAMING_HOME_DEFAULTS = Object.freeze({ deviceForm: 46, startup: 1 });
export const GAMING_OEM_KEY = 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OEM';
export const GAMING_CONFIG_KEY = 'Software\\Microsoft\\Windows\\CurrentVersion\\GamingConfiguration';
const BIOS_KEY = 'HARDWARE\\DESCRIPTION\\System\\BIOS';
export type GamingRegistryRoot = 'HKLM' | 'HKCU';

export interface GamingHomeState {
  deviceForm: number | null;
  startupValue: number | null;
  startupEnabled: boolean;
  homeApp: string | null;
}
export interface GamingHomeRegistryPort {
  read(root: GamingRegistryRoot, key: string, name: string): Promise<unknown>;
  write(root: GamingRegistryRoot, key: string, name: string, value: number): Promise<boolean>;
  remove(root: GamingRegistryRoot, key: string, name: string): Promise<boolean>;
  saveBackup(state: GamingHomeState): Promise<void>;
}

function dword(value: unknown, name: string): number | null {
  if (value == null) return null;
  if (typeof value !== 'number' || !Number.isInteger(value) || value < 0 || value > 0xffffffff) {
    throw new Error(name + ' 不是有效的 DWORD，停止修改；请先检查注册表类型。');
  }
  return value;
}

export async function readGamingHomeState(port: GamingHomeRegistryPort): Promise<GamingHomeState> {
  const [deviceForm, startup, homeApp] = await Promise.all([
    port.read('HKLM', GAMING_OEM_KEY, 'DeviceForm'),
    port.read('HKCU', GAMING_CONFIG_KEY, 'StartupToGamingHome'),
    port.read('HKCU', GAMING_CONFIG_KEY, 'GamingHomeApp'),
  ]);
  const startupValue = dword(startup, 'StartupToGamingHome');
  return {
    deviceForm: dword(deviceForm, 'DeviceForm'),
    startupValue,
    startupEnabled: startupValue === GAMING_HOME_DEFAULTS.startup,
    homeApp: typeof homeApp === 'string' ? homeApp : null,
  };
}

// Missing OEM identity may only be repaired on a recognized physical ROG Ally.
// Other handhelds with their OEM DeviceForm=46 already set are also accepted.
export function isRogGamingHandheld(manufacturer: unknown, product: unknown): boolean {
  return typeof manufacturer === 'string' && typeof product === 'string' &&
    /ASUS/i.test(manufacturer) && /ROG.*Ally|\bRC7[123][A-Z0-9_]*\b/i.test(product);
}

interface Change {
  root: GamingRegistryRoot;
  key: string;
  name: string;
  before: number | null;
  after: number;
}

export async function setGamingHomeStartup(
  port: GamingHomeRegistryPort, enabled: boolean,
): Promise<GamingHomeState> {
  const before = await readGamingHomeState(port);
  if (enabled && before.deviceForm !== GAMING_HOME_DEFAULTS.deviceForm) {
    const [manufacturer, product] = await Promise.all([
      port.read('HKLM', BIOS_KEY, 'SystemManufacturer'),
      port.read('HKLM', BIOS_KEY, 'SystemProductName'),
    ]);
    if (!isRogGamingHandheld(manufacturer, product)) {
      throw new Error('未确认是 ROG 掌机，拒绝把普通电脑标记为掌机；请先检查设备识别。');
    }
  }
  const changes: Change[] = [];
  if (enabled && before.deviceForm !== GAMING_HOME_DEFAULTS.deviceForm) {
    changes.push({ root: 'HKLM', key: GAMING_OEM_KEY, name: 'DeviceForm',
      before: before.deviceForm, after: GAMING_HOME_DEFAULTS.deviceForm });
  }
  const startup = enabled ? GAMING_HOME_DEFAULTS.startup : 0;
  if (before.startupValue !== startup) {
    changes.push({ root: 'HKCU', key: GAMING_CONFIG_KEY, name: 'StartupToGamingHome',
      before: before.startupValue, after: startup });
  }
  if (!changes.length) return before;

  // A failed backup must stop BEFORE any registry mutation.
  await port.saveBackup(before);
  const attempted: Change[] = [];
  try {
    for (const change of changes) {
      attempted.push(change);
      if (!await port.write(change.root, change.key, change.name, change.after) ||
          await port.read(change.root, change.key, change.name) !== change.after) {
        throw new Error(change.name + ' 写入或回读失败，请检查管理员权限。');
      }
    }
    const result = await readGamingHomeState(port);
    if (result.startupValue !== startup ||
        (enabled && result.deviceForm !== GAMING_HOME_DEFAULTS.deviceForm)) {
      throw new Error('全屏启动配置被其他程序覆盖，请检查开机联动。');
    }
    return result;
  } catch (error) {
    const rollbackFailures: string[] = [];
    for (const change of attempted.reverse()) {
      try {
        // Do not overwrite a different value written concurrently by another owner.
        if (await port.read(change.root, change.key, change.name) !== change.after) continue;
        const ok = change.before == null
          ? await port.remove(change.root, change.key, change.name)
          : await port.write(change.root, change.key, change.name, change.before);
        if (!ok || await port.read(change.root, change.key, change.name) !== change.before) {
          rollbackFailures.push(change.name);
        }
      } catch { rollbackFailures.push(change.name); }
    }
    const message = error instanceof Error ? error.message : String(error);
    throw new Error(message + (rollbackFailures.length
      ? '；回退未完成：' + rollbackFailures.join('、') + '，请使用修改前备份检查。' : ''));
  }
}
