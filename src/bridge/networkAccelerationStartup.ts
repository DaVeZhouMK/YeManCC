import {
    loadSettings, updateSettings, getSettingsGeneration,
    assertSettingsGeneration, getSettingsRecoveryState,
} from './settingsRepository';
import { taskExists, toggleTask } from './yeman';

export const NETWORK_ACCELERATION_BOOT_TASK = 'Steamcommunity_302';
export const NETWORK_ACCELERATION_BOOT_CHANGED_EVENT = 'network-acceleration-startup:changed';
let pending: Promise<unknown> = Promise.resolve();
function serialized<T>(run: () => Promise<T>): Promise<T> {
    const next = pending.then(run);
    pending = next.catch(() => undefined);
    return next;
}
async function commit(enabled: boolean, generation: number): Promise<void> {
    const before = await taskExists(NETWORK_ACCELERATION_BOOT_TASK);
    assertSettingsGeneration(generation);
    const changed = before !== enabled;
    try {
        if (changed) await toggleTask(NETWORK_ACCELERATION_BOOT_TASK, enabled);
        assertSettingsGeneration(generation);
        if (await taskExists(NETWORK_ACCELERATION_BOOT_TASK) !== enabled)
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
        if (changed && getSettingsGeneration() === generation) {
            try { await toggleTask(NETWORK_ACCELERATION_BOOT_TASK, before); }
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
        if (typeof settings.startupDesired.networkAccelerationOnBoot === 'boolean') return;
        await commit(true, generation);
    });
}
export function setNetworkAccelerationStartup(enabled: boolean): Promise<void> {
    const generation = getSettingsGeneration();
    return serialized(() => commit(enabled, generation));
}
