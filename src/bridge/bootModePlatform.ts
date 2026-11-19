import { invoke } from './ipc';
import { display, registry, shell } from './api';
import { PW, detectPowerModeReliable, getActiveScheme, setAcValueIndex, setDcValueIndex } from './yeman';
import {loadSettings} from './settingsRepository';
import {captureBootPreferenceWrite,assertBootPreferenceOwner,type BootPreferenceWrite} from './bootModeGlobalPreferences';
export interface BootPlatformState {
    known: boolean;
    supported: boolean;
    active: boolean;
    activeKnown?: boolean;
    reason?: string;
    win32Error?: number;
}
export const bootPlatform = {
    read: () => invoke<BootPlatformState>('bootMode.platform.read', {}, { timeoutMs: 3000 }),
    set: (state: BootPlatformState, active: boolean) => invoke<BootPlatformState & {
        ok: boolean;
        dispatched?: boolean;
        reason?: string;
    }>('bootMode.platform.set', { active, expectedActive: state.active, userInitiated: true }, { timeoutMs: 3000 }),
};
/** Read the existing native command/service owner; no repair or launch on read. */
export async function readBootSteamStartup() {
    const value = await invoke<{ known: boolean; enabled: boolean; conflict: boolean;
        file?: { known: boolean; exists: boolean | null; bytes: number | null; canonical: boolean; encoding: string };
        service?: { known: boolean; installed: boolean; automatic: boolean; running: boolean } }>('steam.startup.get', {}, { timeoutMs: 3000 });
    if (value?.known !== true || typeof value.enabled !== 'boolean' || typeof value.conflict !== 'boolean')
        throw Error('Steam 开机命令读取未确认');
    const service = value.service;
    const file = value.file, fileKnown = file?.known === true && typeof file.exists === 'boolean' &&
        Number.isSafeInteger(file.bytes) && Number(file.bytes) >= 0 && typeof file.canonical === 'boolean' &&
        ['unknown', 'utf-8', 'utf-16le'].includes(file.encoding) && (file.exists || file.bytes === 0);
    return { known: true, enabled: value.enabled, conflict: value.conflict,
        command: value.enabled ? 'recognized' : value.conflict ? 'conflict' : 'empty-or-absent',
        file: { known: fileKnown, exists: fileKnown ? file!.exists : null, bytes: fileKnown ? file!.bytes : null,
            canonical: fileKnown && file!.canonical, encoding: fileKnown ? file!.encoding : 'unknown' },
        service: { known: service?.known === true, installed: service?.installed === true,
            automatic: service?.automatic === true, running: service?.running === true } };
}
const VIDEO = '7516b95f-f776-4464-8c53-06167f40cc99', BRIGHTNESS = 'aded5e82-b909-4619-9949-f5d71dac0bcb';
const brightnessPath = `SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\${PW.YEMAN}\\${VIDEO}\\${BRIGHTNESS}`;
function brightnessValue(raw: unknown): number | null {
    const value = typeof raw === 'number' || typeof raw === 'string' && raw.trim() !== '' ? Number(raw) : NaN;
    return Number.isInteger(value) && value >= 0 && value <= 100 ? value : null;
}
export async function readBootDisplay() {
    const [modes, power] = await Promise.all([display.getModes(), detectPowerModeReliable()]);
    const current = modes.modes.find(mode => mode.id === modes.current);
    let brightness: number | null = null;
    if (power && (await getActiveScheme()).toLowerCase() === PW.YEMAN) {
        const raw = await registry.read('HKLM', brightnessPath, power === 'ac' ? 'ACSettingIndex' : 'DCSettingIndex').catch(() => null);
        brightness = brightnessValue(raw);
    }
    return { brightness, resolution: current ? `${current.width}x${current.height}` : 'native', hz: current ? String(current.refresh) : 'native' };
}
export async function applyBootBrightness(percent: number,options:BootPreferenceWrite={}): Promise<{ saved: boolean; applied: boolean; reason?: string }> {
    if (!Number.isFinite(percent) || percent < 0 || percent > 100)
        throw Error('亮度范围无效');
    const submitted=captureBootPreferenceWrite(options);
    const current=async()=>{if(submitted.bootContext)assertBootPreferenceOwner(await loadSettings(),submitted);};
    const power = await detectPowerModeReliable();
    if (!power)
        throw Error('当前供电状态未确认');
    await current();
    // Same fixed power plan as YMCC's physical brightness shortcut; no arbitrary plan target.
    const result = await (power === 'ac' ? setAcValueIndex : setDcValueIndex)(VIDEO, BRIGHTNESS, String(Math.round(percent)),undefined,submitted);
    await current();
    if (result.exitCode !== 0)
        throw Error('亮度写入失败');
    const readback = await registry.read('HKLM', brightnessPath, power === 'ac' ? 'ACSettingIndex' : 'DCSettingIndex');
    await current();
    if (brightnessValue(readback) !== Math.round(percent))
        throw Error('亮度回读不一致');
    const scheme=await getActiveScheme();
    await current();
    if (scheme.toLowerCase() !== PW.YEMAN)
        return { saved: true, applied: false, reason: '亮度已保存到野蛮系统电源，等待该电源计划启用' };
    const active = await shell.run('powercfg', ['/setactive', PW.YEMAN], 5000,undefined,undefined,submitted.bootContext);
    await current();
    if (active.exitCode !== 0)
        return { saved: true, applied: false, reason: '亮度已保存，电源计划重新应用未确认' };
    return { saved: true, applied: true };
}
