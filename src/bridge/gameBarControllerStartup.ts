import { registry } from './api';
import { loadSettings, saveSettingsSection, getSettingsGeneration, assertSettingsGeneration, getSettingsRecoveryState } from './settingsRepository';

// Only the current user's controller launch shortcut. Game Bar/Win+G remain available.
export const GAME_BAR_CONTROLLER_PATH = 'Software\\Microsoft\\GameBar';
export const GAME_BAR_CONTROLLER_VALUE = 'UseNexusForGameBarEnabled';
export const GAME_BAR_CONTROLLER_CHANGED_EVENT = 'game-bar-controller-startup:changed';
let pending: Promise<unknown> = Promise.resolve();
let startupGeneration = -1;
let startupFlight: Promise<void> | undefined;
let manualRevision = 0;
function serialized<T>(run: () => Promise<T>): Promise<T> {
    const next = pending.then(run);
    pending = next.catch(() => undefined);
    return next;
}
function notify(enabled: boolean): void {
    if (typeof window !== 'undefined') window.dispatchEvent(new CustomEvent(GAME_BAR_CONTROLLER_CHANGED_EVENT, { detail: { enabled } }));
}
async function rawState(): Promise<0 | 1 | null> {
    const value: unknown = await registry.read('HKCU', GAME_BAR_CONTROLLER_PATH, GAME_BAR_CONTROLLER_VALUE);
    if (value === 0 || value === 1) return value;
    if (value === null) return null;
    throw new Error('Game Bar 手柄入口的 Windows 设置无效。');
}
/** Read only. An absent value uses Windows' controller-shortcut default. */
export async function readGameBarControllerEnabled(): Promise<boolean> {
    return (await rawState()) !== 0;
}
async function apply(enabled: boolean, current: () => void): Promise<void> {
    current();
    const desired = enabled ? 1 : 0;
    const before = await rawState();
    current();
    if (before === desired) return;
    const written = await registry.write('HKCU', GAME_BAR_CONTROLLER_PATH, GAME_BAR_CONTROLLER_VALUE, desired);
    current();
    if (written !== true) throw new Error('Windows 未确认 Game Bar 手柄入口设置写入。');
    const after = await rawState();
    current();
    if (after !== desired) throw new Error('Game Bar 手柄入口设置回读不一致，请检查 Windows 设置。');
}
/** One startup pass per settings generation; no wake hook, timer or registry polling. */
export function ensureGameBarControllerStartup(): Promise<void> {
    const generation = getSettingsGeneration();
    if (startupGeneration === generation && startupFlight) return startupFlight;
    startupGeneration = generation;
    const revision = manualRevision;
    startupFlight = serialized(async () => {
        const current = () => {
            assertSettingsGeneration(generation);
            if (revision !== manualRevision) throw new Error('Game Bar 启动设置已由用户操作接管。');
        };
        current();
        const settings = await loadSettings();
        current();
        if (!getSettingsRecoveryState().persisted) throw new Error('配置尚未确认，暂不修改 Game Bar 手柄入口。');
        const choice = settings.startupDesired.gameBarControllerOnBoot;
        if (choice !== undefined && typeof choice !== 'boolean') throw new Error('Game Bar 开机偏好无效。');
        await apply(choice === true, current); // Missing preference defaults OFF, including existing installs.
        current();
        notify(choice === true);
    });
    return startupFlight;
}
export function setGameBarControllerEnabled(enabled: boolean): Promise<void> {
    if (typeof enabled !== 'boolean') return Promise.reject(new Error('Game Bar 手柄入口必须为开启或关闭。'));
    const generation = getSettingsGeneration();
    ++manualRevision; // Revoke an older startup read before the explicit operation is queued.
    return serialized(async () => {
        const current = () => assertSettingsGeneration(generation);
        current();
        await loadSettings();
        current();
        if (!getSettingsRecoveryState().persisted) throw new Error('配置尚未确认，无法保存 Game Bar 手柄入口。');
        // Durable intent first. A Windows failure is reported and receives no background retry.
        await saveSettingsSection('startupDesired', { gameBarControllerOnBoot: enabled });
        current();
        await apply(enabled, current);
        current();
        notify(enabled);
    });
}
