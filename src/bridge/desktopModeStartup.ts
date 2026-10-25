import { loadSettings, updateSettings, getSettingsGeneration, assertSettingsGeneration } from './settingsRepository';
import { taskExists, toggleTask } from './yeman';

export const DESKTOP_MODE_BOOT_TASK = '桌面模式-开机设置为桌面模式';
export const DESKTOP_MODE_BOOT_CHANGED_EVENT = 'desktop-mode-startup:changed';
let pending: Promise<unknown> = Promise.resolve();
function serialized<T>(run: () => Promise<T>): Promise<T> {
    const next = pending.then(run);
    pending = next.catch(() => undefined);
    return next;
}
async function commit(enabled: boolean, generation: number): Promise<void> {
    const before = await taskExists(DESKTOP_MODE_BOOT_TASK);
    assertSettingsGeneration(generation);
    const changed = before !== enabled;
    try {
        if (changed) await toggleTask(DESKTOP_MODE_BOOT_TASK, enabled);
        assertSettingsGeneration(generation);
        await updateSettings(settings => {
            assertSettingsGeneration(generation);
            settings.startupDesired = { ...settings.startupDesired, desktopModeOnBoot: enabled };
        });
    } catch (error) {
        if (changed && getSettingsGeneration() === generation) {
            try { await toggleTask(DESKTOP_MODE_BOOT_TASK, before); }
            catch { throw new Error('桌面模式开机设置未保存，任务回退未确认，请检查开关状态。'); }
        }
        throw error;
    }
    if (typeof window !== 'undefined')
        window.dispatchEvent(new CustomEvent(DESKTOP_MODE_BOOT_CHANGED_EVENT, { detail: { enabled } }));
}
/** First app startup only: prepare the next-logon task; never run its script now. */
export function ensureDesktopModeStartupDefault(): Promise<void> {
    const generation = getSettingsGeneration();
    return serialized(async () => {
        const settings = await loadSettings();
        assertSettingsGeneration(generation);
        // An explicit ON or OFF is also the migration receipt. Page reads and
        // later renderer/app restarts never silently replace the user's choice.
        if (typeof settings.startupDesired.desktopModeOnBoot === 'boolean') return;
        await commit(true, generation);
    });
}
export function setDesktopModeStartup(enabled: boolean): Promise<void> {
    const generation = getSettingsGeneration();
    return serialized(() => commit(enabled, generation));
}
