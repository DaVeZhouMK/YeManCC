import { invoke } from './ipc';
import { display, registry, shell } from './api';
import { PW, detectPowerModeReliable, getActiveScheme, setAcValueIndex, setDcValueIndex } from './yeman';
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
const VIDEO = '7516b95f-f776-4464-8c53-06167f40cc99', BRIGHTNESS = 'aded5e82-b909-4619-9949-f5d71dac0bcb';
const brightnessPath = `SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\${PW.YEMAN}\\${VIDEO}\\${BRIGHTNESS}`;
export async function readBootDisplay() {
    const [modes, power] = await Promise.all([display.getModes(), detectPowerModeReliable()]);
    const current = modes.modes.find(mode => mode.id === modes.current);
    let brightness: number | null = null;
    if (power) {
        const raw = await registry.read('HKLM', brightnessPath, power === 'ac' ? 'ACSettingIndex' : 'DCSettingIndex').catch(() => null);
        if (raw !== null && Number.isFinite(Number(raw)))
            brightness = Math.max(0, Math.min(100, Number(raw)));
    }
    return { brightness, resolution: current ? `${current.width}x${current.height}` : 'native', hz: current ? String(current.refresh) : 'native' };
}
export async function applyBootBrightness(percent: number): Promise<void> {
    if (!Number.isFinite(percent) || percent < 0 || percent > 100)
        throw Error('亮度范围无效');
    const power = await detectPowerModeReliable();
    if (!power)
        throw Error('当前供电状态未确认');
    // Same fixed power plan as YMCC's physical brightness shortcut; no arbitrary plan target.
    const result = await (power === 'ac' ? setAcValueIndex : setDcValueIndex)(VIDEO, BRIGHTNESS, String(Math.round(percent)));
    if (result.exitCode !== 0)
        throw Error('亮度写入失败');
    const readback = await registry.read('HKLM', brightnessPath, power === 'ac' ? 'ACSettingIndex' : 'DCSettingIndex');
    if (Number(readback) !== Math.round(percent))
        throw Error('亮度回读不一致');
    if ((await getActiveScheme()).toLowerCase() !== PW.YEMAN)
        throw Error('亮度已保存到野蛮系统电源，等待该电源计划启用');
    const active = await shell.run('powercfg', ['/setactive', PW.YEMAN], 5000);
    if (active.exitCode !== 0)
        throw Error('亮度已保存，电源计划重新应用未确认');
}
