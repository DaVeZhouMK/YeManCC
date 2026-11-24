import {
    loadSettings, updateSettings, getSettingsGeneration,
    assertSettingsGeneration, getSettingsRecoveryState,
} from './settingsRepository';
import { createTask } from './yeman';
import { invoke } from './ipc';

export const NETWORK_ACCELERATION_BOOT_TASK = 'Steamcommunity_302';
export const NETWORK_ACCELERATION_BOOT_CHANGED_EVENT = 'network-acceleration-startup:changed';
type BootTasks = { ok: boolean; present: number; enabled: number; win32Error?: number; rollbackOk?: boolean };
async function bootTasks(args: Record<string, boolean | number> = {}): Promise<BootTasks> {
    const state = await invoke<BootTasks>('updateAccel.bootTask', args);
    if (!state.ok) {
        const rollback = state.rollbackOk === false ? '，任务回退未确认' : '';
        throw new Error(`网络加速开机任务校验失败（${state.win32Error ?? '未知错误'}${rollback}）`);
    }
    return state;
}
export async function readNetworkAccelerationStartup(): Promise<boolean> {
    return (await bootTasks()).enabled !== 0;
}
let pending: Promise<unknown> = Promise.resolve();
function serialized<T>(run: () => Promise<T>): Promise<T> {
    const next = pending.then(run);
    pending = next.catch(() => undefined);
    return next;
}
async function commit(enabled: boolean, generation: number): Promise<void> {
    const before = await bootTasks();
    assertSettingsGeneration(generation);
    let applied: BootTasks | undefined;
    let created = false;
    try {
        if (enabled && !before.present) {
            if (!await createTask(NETWORK_ACCELERATION_BOOT_TASK)) throw new Error('创建网络加速开机任务失败。');
            created = true;
            assertSettingsGeneration(generation);
        }
        applied = await bootTasks({ enabled });
        if ((applied.enabled !== 0) !== enabled)
            throw new Error('网络加速开机任务状态未确认，请重试。');
        assertSettingsGeneration(generation);
        await updateSettings(settings => {
            assertSettingsGeneration(generation);
            settings.startupDesired = {
                ...settings.startupDesired,
                networkAccelerationOnBoot: enabled,
                steamCommunity: enabled,
            };
        });
    } catch (error) {
        if ((created || (applied && applied.enabled !== before.enabled)) && getSettingsGeneration() === generation) {
            try {
                if (applied) await bootTasks({ restore: before.enabled, expectedPresent: applied.present, expectedEnabled: applied.enabled });
                else await bootTasks({ restore: 0, expectedPresent: 2, expectedEnabled: 2 });
            }
            catch { throw new Error('网络加速开机设置未保存，任务回退未确认，请检查开关状态。'); }
        }
        throw error;
    }
    if (typeof window !== 'undefined')
        window.dispatchEvent(new CustomEvent(NETWORK_ACCELERATION_BOOT_CHANGED_EVENT, { detail: { enabled } }));
}
/** Prepare next logon once; task execution remains with the existing startup/wake owners. */
export function ensureNetworkAccelerationStartupDefault(): Promise<void> {
    const generation = getSettingsGeneration();
    return serialized(async () => {
        const settings = await loadSettings();
        assertSettingsGeneration(generation);
        // A failed read cannot authorize changing the machine's startup tasks.
        if (getSettingsRecoveryState().status !== 'ready') return;
        // The legacy steamCommunity mirror was always false and did not record
        // manual task changes. Only this receipt distinguishes a saved choice.
        const saved = settings.startupDesired.networkAccelerationOnBoot;
        if (typeof saved === 'boolean') {
            const current = await bootTasks();
            assertSettingsGeneration(generation);
            // An installer may have registered V15 after the user opted out,
            // or left both logon owners active after an upgrade.
            if ((!saved && current.enabled) || (saved && current.enabled === 3))
                await commit(saved, generation);
            return;
        }
        await commit(true, generation);
    });
}
export function setNetworkAccelerationStartup(enabled: boolean): Promise<void> {
    const generation = getSettingsGeneration();
    return serialized(() => commit(enabled, generation));
}
