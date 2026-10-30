// Windows full-screen startup is user intent, not the presence of a YeMan task.
// Explicit user requests may apply these defaults on any machine. Never apply on read.
// Verified against Microsoft.GamingApp's installed Application Id and gamingApp extension.
export const XBOX_GAMING_HOME_AUMID = 'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App';
export type GamingHomeSelection = 'xbox' | 'none';
export const GAMING_HOME_DEFAULTS = Object.freeze({ deviceForm: 46, startup: 1 });
export const GAMING_OEM_KEY = 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OEM';
export const GAMING_CONFIG_KEY = 'Software\\Microsoft\\Windows\\CurrentVersion\\GamingConfiguration';
export type GamingRegistryRoot = 'HKLM' | 'HKCU';

export interface GamingHomeState {
  deviceForm: number | null;
  startupValue: number | null;
  startupEnabled: boolean;
  homeApp: string | null;
}
export interface GamingHomeRegistryPort {
  read(root: GamingRegistryRoot, key: string, name: string): Promise<unknown>;
  write(root: GamingRegistryRoot, key: string, name: string, value: number | string): Promise<boolean>;
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
  if (homeApp != null && typeof homeApp !== 'string')
    throw new Error('GamingHomeApp 不是有效的字符串，停止修改；请先检查注册表类型。');
  return {
    deviceForm: dword(deviceForm, 'DeviceForm'),
    startupValue,
    startupEnabled: startupValue === GAMING_HOME_DEFAULTS.startup,
    homeApp: typeof homeApp === 'string' ? homeApp : null,
  };
}

interface Change {
  root: GamingRegistryRoot;
  key: string;
  name: string;
  before: number | string | null;
  after: number | string;
}

export async function setGamingHomeStartup(
  port: GamingHomeRegistryPort, enabled: boolean, repairHandheld = enabled, selection?: GamingHomeSelection,
): Promise<GamingHomeState> {
  if (selection !== undefined && selection !== 'xbox' && selection !== 'none')
    throw new Error('无效的 Xbox 主应用选择。');
  const before = await readGamingHomeState(port);
  const changes: Change[] = [];
  if (repairHandheld && before.deviceForm !== GAMING_HOME_DEFAULTS.deviceForm) {
    changes.push({ root: 'HKLM', key: GAMING_OEM_KEY, name: 'DeviceForm',
      before: before.deviceForm, after: GAMING_HOME_DEFAULTS.deviceForm });
  }
  const startup = enabled ? GAMING_HOME_DEFAULTS.startup : 0;
  const homeApp = selection === undefined ? undefined : selection === 'xbox' ? XBOX_GAMING_HOME_AUMID : '';
  const startupChange: Change = { root: 'HKCU', key: GAMING_CONFIG_KEY, name: 'StartupToGamingHome',
    before: before.startupValue, after: startup };
  // Disable sign-in first when clearing the home; enable only after Xbox is selected.
  if (!enabled && before.startupValue !== startup) changes.push(startupChange);
  if (homeApp !== undefined && before.homeApp !== homeApp)
    changes.push({ root: 'HKCU', key: GAMING_CONFIG_KEY, name: 'GamingHomeApp', before: before.homeApp, after: homeApp });
  if (enabled && before.startupValue !== startup) changes.push(startupChange);
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
        (repairHandheld && result.deviceForm !== GAMING_HOME_DEFAULTS.deviceForm) ||
        (homeApp !== undefined && result.homeApp !== homeApp)) {
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
