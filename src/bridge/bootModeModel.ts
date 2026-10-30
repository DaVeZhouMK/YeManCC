/** Boot scenes are complete global presets. Game-specific records are never stored here. */
import { screenTouchpadDefaults } from './screenTouchpadDefaults';
const XBOX_DEFAULTS_VERSION = 1;
const AUDIO_DEFAULTS_VERSION = 1;
const CONTROLLER_DEFAULTS_VERSION = 2;
export const BOOT_MODE_IDS = ['global', 'xbox', 'steam', 'xbox-steam'] as const;
export type BootModeId = typeof BOOT_MODE_IDS[number];
export const BOOT_MODE_MODULES = ['performance', 'fan', 'charge', 'input', 'gyro', 'controller', 'touch', 'audioOptimization', 'frameGeneration', 'softwareLaunch', 'steamAppearance', 'steamWallpaper', 'steamDecky', 'monitor', 'display', 'background', 'sleep'] as const;
export type BootModeModule = typeof BOOT_MODE_MODULES[number];
export type BootValue = null | boolean | number | string | BootValue[] | {
    [key: string]: BootValue;
};
export type BootConfig = {
    [key: string]: BootValue;
};
export type BootModeSnapshot = Record<BootModeModule, BootConfig>;
export interface BootModeScene {
    name: string;
    overrides: Partial<BootModeSnapshot>;
    /** Inactive own values survive choosing follow-global, without applying. */
    rememberedOverrides?: Partial<BootModeSnapshot>;
}
export interface BootModeDocument {
    schema: 1;
    xboxDefaultsVersion?: number;
    controllerDefaultsVersion?: number;
    audioDefaultsVersion?: number;
    revision: number;
    current: BootModeId;
    boot: BootModeId;
    bootEnabled: boolean;
    bootNotice?: string;
    global: BootModeSnapshot;
    activeSnapshot: BootModeSnapshot;
    scenes: Record<Exclude<BootModeId, 'global'>, BootModeScene>;
    presets: Partial<Record<BootModeId, Record<string, BootModeScene>>>;
    controllerTemplates: Record<string, BootConfig>;
}
export interface BootStartupResult {
    schema: 1;
    boot: string;
    mode: BootModeId;
    sourceRevision: number;
    revision: number;
    status: 'applied' | 'partial' | 'failed';
    applied: string[];
    skipped: string[];
    pending: string[];
    error?: string;
}
/** A saved result is evidence only for the boot actually observed by native. */
export function normalizeBootStartupResult(raw: unknown, observedBoot: unknown): BootStartupResult | undefined {
    if (!object(raw) || raw.schema !== 1 || typeof observedBoot !== 'string' || !observedBoot ||
        raw.boot !== observedBoot || !isBootModeId(raw.mode) || !['applied', 'partial', 'failed'].includes(String(raw.status)) ||
        !Number.isSafeInteger(raw.sourceRevision) || Number(raw.sourceRevision) < 0 ||
        !Number.isSafeInteger(raw.revision) || Number(raw.revision) < Number(raw.sourceRevision)) return;
    const lists = ['applied', 'skipped', 'pending'] as const;
    if (lists.some(key => !Array.isArray(raw[key]) || !(raw[key] as unknown[]).every(value => typeof value === 'string'))) return;
    return {
        schema: 1, boot: observedBoot, mode: raw.mode,
        sourceRevision: Number(raw.sourceRevision), revision: Number(raw.revision),
        status: raw.status as BootStartupResult['status'],
        ...Object.fromEntries(lists.map(key => [key, (raw[key] as string[]).slice(0, 64).map(value => value.slice(0, 500))])) as Pick<BootStartupResult, 'applied' | 'skipped' | 'pending'>,
        ...(typeof raw.error === 'string' ? { error: raw.error.slice(0, 1000) } : {}),
    };
}
export function bootStartupStatus(result: BootStartupResult | undefined): string {
    if (!result) return '';
    const mode = { global: '全局', xbox: 'Xbox', steam: 'Steam', 'xbox-steam': 'Xbox + Steam' }[result.mode];
    if (result.status === 'failed') return '本次 ' + mode + ' 预设未完成：' + (result.error || result.pending[0] || '请检查诊断日志');
    const details = [...result.pending, ...result.skipped];
    const suffix = details.length ? ' · ' + details.slice(0, 2).join('；') + (details.length > 2 ? ' 等' + details.length + '项' : '') : '';
    return '本次 ' + mode + ' 预设' + (result.status === 'partial' ? '部分完成' : '已处理') + suffix;
}
export const BOOT_MODE_CATEGORIES = [
    { id: 'performance', label: '性能与散热', icon: 'gauge', modules: ['performance', 'fan', 'charge'] },
    { id: 'optimization', label: '优化软件', icon: 'rocket', modules: ['audioOptimization', 'frameGeneration', 'softwareLaunch'] },
    { id: 'input', label: '手柄与陀螺仪', icon: 'gamepad', modules: ['input', 'gyro'] },
    { id: 'controller', label: '手柄自定义', icon: 'gamepad', modules: ['controller'] },
    { id: 'touch', label: '触摸板与屏幕按键', icon: 'fullscreen', modules: ['touch'] },
    { id: 'steam', label: 'Steam 大屏', icon: 'steam', modules: ['steamAppearance', 'steamWallpaper', 'steamDecky'] },
    { id: 'sleep', label: '睡眠优化', icon: 'sleep', modules: ['sleep'] },
    { id: 'display', label: '监控与画面', icon: 'monitor', modules: ['monitor', 'display', 'background'] },
] as const;
export type BootModeCategory = typeof BOOT_MODE_CATEGORIES[number]['id'];
export const BOOT_MODE_LABELS: Record<BootModeId, string> = { global: '全局', xbox: 'Xbox 全屏模式', steam: 'Windows + Steam 大屏', 'xbox-steam': 'Xbox 全屏模式 + Steam 大屏' };
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);
export const cloneBootValue = <T>(value: T): T => JSON.parse(JSON.stringify(value)) as T;
export function isBootModeId(value: unknown): value is BootModeId { return BOOT_MODE_IDS.includes(value as BootModeId); }
function clean(value: unknown, depth = 0): BootValue | undefined {
    if (depth > 20)
        return undefined;
    if (value === null || typeof value === 'boolean')
        return value as null | boolean;
    if (typeof value === 'number')
        return Number.isFinite(value) ? value : undefined;
    if (typeof value === 'string')
        return value.slice(0, 65536);
    if (Array.isArray(value))
        return value.slice(0, 1024).map(item => clean(item, depth + 1)).filter((v): v is BootValue => v !== undefined);
    if (object(value)) {
        const out: BootConfig = {};
        for (const [key, v] of Object.entries(value).slice(0, 1024)) {
            if (['__proto__', 'prototype', 'constructor'].includes(key))
                continue;
            const next = clean(v, depth + 1);
            if (next !== undefined)
                out[key] = next;
        }
        return out;
    }
    return undefined;
}
export function normalizeBootConfig(value: unknown): BootConfig { return object(value) ? clean(value) as BootConfig : {}; }
function overrides(value: unknown): Partial<BootModeSnapshot> {
    const out: Partial<BootModeSnapshot> = {};
    if (object(value))
        for (const id of BOOT_MODE_MODULES)
            if (object(value[id]))
                out[id] = normalizeBootConfig(value[id]);
    return out;
}
function sameFactoryValue(a: unknown, b: unknown): boolean {
    if (a === b) return true;
    if (Array.isArray(a) || Array.isArray(b))
        return Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((v, i) => sameFactoryValue(v, b[i]));
    if (!object(a) || !object(b)) return false;
    const keys = Object.keys(a);
    return keys.length === Object.keys(b).length && keys.every(key => Object.prototype.hasOwnProperty.call(b, key) && sameFactoryValue(a[key], b[key]));
}
function steamRecommendedScene(): BootModeScene {
    return { name: '推荐预设', overrides: {
        touch: { profiles: { disabled: normalizeBootConfig({ ...screenTouchpadDefaults('disabled'), standaloneSpecialMode: 'steamdeck' }) } },
    } };
}
export function createBootModeDocument(baseline: BootModeSnapshot): BootModeDocument {
    const global = cloneBootValue(baseline);
    global.input = { ...global.input, persona: 'disabled',
        outputTarget: { ...normalizeBootConfig(global.input.outputTarget), persona: 'disabled', buttonMappingEnabled: false },
        ...(global.input.mouse === 'steamdeck' ? { mouse: 'off' } : {}) };
    global.audioOptimization = { ...global.audioOptimization, enabled: false };
    const xbox: BootModeScene = { name: '推荐预设', overrides: {} }, steam = steamRecommendedScene(), combined = steamRecommendedScene();
    xbox.overrides.audioOptimization = { enabled: false };
    combined.overrides.audioOptimization = { enabled: true };
    steam.overrides.audioOptimization = { enabled: true };
    return { schema: 1, xboxDefaultsVersion: XBOX_DEFAULTS_VERSION, controllerDefaultsVersion: CONTROLLER_DEFAULTS_VERSION, audioDefaultsVersion: AUDIO_DEFAULTS_VERSION, revision: 0, current: 'global', boot: 'global', bootEnabled: false, global, activeSnapshot: cloneBootValue(baseline), scenes: { xbox, steam, 'xbox-steam': combined }, presets: {}, controllerTemplates: {} };
}
export function normalizeBootModeDocument(raw: unknown, baseline: BootModeSnapshot): BootModeDocument {
    const out = createBootModeDocument(baseline);
    if (!object(raw) || raw.schema !== 1)
        return out;
    // Fill inherited legacy Xbox modules once. Explicit edited modules stay
    // authoritative; the version also preserves a later choice to follow global.
    const legacyXboxDefaults = !Number.isSafeInteger(raw.xboxDefaultsVersion) || Number(raw.xboxDefaultsVersion) < XBOX_DEFAULTS_VERSION;
    out.xboxDefaultsVersion = legacyXboxDefaults ? XBOX_DEFAULTS_VERSION : Number(raw.xboxDefaultsVersion);
    const legacyControllerDefaults = !Number.isSafeInteger(raw.controllerDefaultsVersion) || Number(raw.controllerDefaultsVersion) < CONTROLLER_DEFAULTS_VERSION;
    out.controllerDefaultsVersion = legacyControllerDefaults ? CONTROLLER_DEFAULTS_VERSION : Number(raw.controllerDefaultsVersion);
    const legacyAudioDefaults = !Number.isSafeInteger(raw.audioDefaultsVersion) || Number(raw.audioDefaultsVersion) < AUDIO_DEFAULTS_VERSION;
    out.audioDefaultsVersion = legacyAudioDefaults ? AUDIO_DEFAULTS_VERSION : Number(raw.audioDefaultsVersion);
    out.bootEnabled = raw.bootEnabled !== false;
    if (typeof raw.bootNotice === 'string')
        out.bootNotice = raw.bootNotice.slice(0, 1000);
    out.revision = Number.isSafeInteger(raw.revision) && Number(raw.revision) >= 0 ? Number(raw.revision) : 0;
    if (isBootModeId(raw.current))
        out.current = raw.current;
    if (isBootModeId(raw.boot))
        out.boot = raw.boot;
    // Existing modules are saved intent, including deliberately absent fields.
    // A live capture may describe another running mode; never merge its fields
    // into stored templates during reads, saves or startup restoration.
    for (const [module, value] of Object.entries(overrides(raw.global)))
        out.global[module as BootModeModule] = cloneBootValue(value);
    for (const id of ['xbox', 'steam', 'xbox-steam'] as const) {
        const scene = object(raw.scenes) ? raw.scenes[id] : undefined;
        if (object(scene))
            out.scenes[id] = { name: typeof scene.name === 'string' ? scene.name.slice(0, 60) : '推荐预设',
                overrides: { ...(legacyXboxDefaults ? out.scenes[id].overrides : {}), ...overrides(scene.overrides) },
                ...(object(scene.rememberedOverrides) ? { rememberedOverrides: overrides(scene.rememberedOverrides) } : {}) };
    }
    // Retire only the exact old SteamDeck factory recipe. Saved custom modules
    // and explicit follow-global choices must never become forced defaults.
    if (legacyControllerDefaults) {
        const oldInput = { persona: 'steamdeck', mouse: 'steamdeck', outputTarget: { persona: 'steamdeck', buttonMappingEnabled: true } };
        const oldTouch = { profiles: { steamdeck: normalizeBootConfig(screenTouchpadDefaults('steamdeck')) } };
        const unversioned = !Number.isSafeInteger(raw.controllerDefaultsVersion) || Number(raw.controllerDefaultsVersion) < 1;
        for (const id of ['xbox', 'steam', 'xbox-steam'] as const) {
            const own = out.scenes[id].overrides;
            if (sameFactoryValue(own.input, oldInput)) delete own.input;
            if (id === 'xbox') {
                if (sameFactoryValue(own.touch, oldTouch)) delete own.touch;
            } else if (sameFactoryValue(own.touch, oldTouch) || (unversioned && !own.touch)) {
                own.touch = cloneBootValue(steamRecommendedScene().overrides.touch!);
            }
        }
    }
    // Adopt new defaults once for legacy inherited modules. Explicit audio
    // choices and a later follow-global choice remain authoritative.
    if (legacyAudioDefaults) {
        if (typeof out.global.audioOptimization.enabled !== 'boolean')
            out.global.audioOptimization.enabled = false;
        for (const id of ['xbox', 'steam', 'xbox-steam'] as const) {
            const audio = out.scenes[id].overrides.audioOptimization;
            if (typeof audio?.enabled !== 'boolean')
                out.scenes[id].overrides.audioOptimization = { ...audio, enabled: id !== 'xbox' };
        }
    }
    if (object(raw.presets))
        for (const id of BOOT_MODE_IDS) {
            const entries = raw.presets[id];
            if (!object(entries))
                continue;
            const presets: Record<string, BootModeScene> = {};
            for (const [name, entry] of Object.entries(entries).slice(0, 30)) {
                if (!name.trim() || !object(entry) || ['__proto__', 'constructor', 'prototype'].includes(name))
                    continue;
                presets[name.slice(0, 60)] = { name: name.slice(0, 60), overrides: overrides(entry.overrides) };
            }
            out.presets[id] = presets;
        }
    if (object(raw.controllerTemplates))
        for (const [name, value] of Object.entries(raw.controllerTemplates).slice(0, 30))
            if (name.trim() && !['__proto__', 'constructor', 'prototype'].includes(name))
                out.controllerTemplates[name.slice(0, 60)] = normalizeBootConfig(value);
    const active = overrides(raw.activeSnapshot);
    // Requested mode defaults are not proof of an applied runtime. Missing
    // legacy receipt fields fall back to the observed baseline, so saving or
    // reopening an editor cannot reclassify a pending default as already run.
    out.activeSnapshot = cloneBootValue(baseline);
    for (const [module, value] of Object.entries(active))
        out.activeSnapshot[module as BootModeModule] = mergeBootConfig(baseline[module as BootModeModule], value);
    return out;
}
/** Fill missing fields from the global baseline; arrays remain complete snapshots. */
export function mergeBootConfig(baseline: BootConfig, override: BootConfig): BootConfig {
    const result = cloneBootValue(baseline);
    for (const [key, value] of Object.entries(override)) {
        if (['__proto__', 'constructor', 'prototype'].includes(key))
            continue;
        result[key] = object(value) && object(result[key])
            ? mergeBootConfig(result[key] as BootConfig, value as BootConfig) : cloneBootValue(value);
    }
    return result;
}
export function resolveBootMode(document: BootModeDocument, id: BootModeId): BootModeSnapshot {
    const snapshot = cloneBootValue(document.global);
    if (id !== 'global')
        for (const module of BOOT_MODE_MODULES) {
            const value = document.scenes[id].overrides[module];
            if (value)
                snapshot[module] = mergeBootConfig(snapshot[module], value);
        }
    return snapshot;
}
export function setBootModeModuleScope(document: BootModeDocument, id: BootModeId, module: BootModeModule, own: boolean): void {
    if (id === 'global')
        return;
    if (own) {
        if (!document.scenes[id].overrides[module])
            document.scenes[id].overrides[module] = cloneBootValue(document.scenes[id].rememberedOverrides?.[module] ?? document.global[module]);
    }
    else {
        const previous = document.scenes[id].overrides[module];
        if (previous) (document.scenes[id].rememberedOverrides ??= {})[module] = cloneBootValue(previous);
        delete document.scenes[id].overrides[module];
    }
}
export function mutableBootModeModule(document: BootModeDocument, id: BootModeId, module: BootModeModule): BootConfig {
    if (id === 'global')
        return document.global[module];
    const own = document.scenes[id].overrides[module];
    if (!own) throw new Error('此项跟随全局，请在全局页面修改，或选择模式单独设置');
    return own;
}
export type BootSupportState = 'unknown' | 'supported' | 'unsupported';
export interface BootDeviceSupport {
    state: BootSupportState;
    reason: string;
    choices?: number[];
}
export type BootDeviceCapabilities = Partial<Record<'hybridCores' | 'performance' | 'fan' | 'charge' | 'gyro' | 'hibernate', BootDeviceSupport>>;
export interface BootApplyStep {
    id: string;
    apply(): Promise<void>;
    rollback(): Promise<void>;
}
export interface BootRollbackResult {
    step: string;
    ok: boolean;
    error?: string;
}
export class BootModeApplyError extends Error {
    constructor(message: string, readonly failedStep: string, readonly rollback: BootRollbackResult[]) { super(message); this.name = 'BootModeApplyError'; }
}
/** Serial caller owns the transaction. A failing partially applied step is rolled back too. */
export async function executeBootModeSteps(steps: readonly BootApplyStep[], isCurrent: () => boolean = () => true): Promise<void> {
    const entered: BootApplyStep[] = [];
    let current = '';
    try {
        for (const step of steps) {
            if (!isCurrent())
                throw new Error('配置或设备状态已改变');
            current = step.id;
            entered.push(step);
            await step.apply();
            if (!isCurrent())
                throw new Error('配置或设备状态已改变');
        }
    }
    catch (error) {
        const receipts: BootRollbackResult[] = [];
        for (const step of entered.reverse()) {
            if (!isCurrent()) {
                receipts.push({ step: step.id, ok: false, error: "配置代次已改变，未向新位置写入旧配置" });
                continue;
            }
            try {
                await step.rollback();
                receipts.push({ step: step.id, ok: true });
            }
            catch (rollback) {
                receipts.push({ step: step.id, ok: false, error: String(rollback) });
            }
        }
        const failed = receipts.filter(receipt => !receipt.ok);
        const message = String(error instanceof Error ? error.message : error);
        throw new BootModeApplyError(message + (failed.length ? '；部分状态恢复待确认：' + failed.map(row => row.step).join('、') : ''), current, receipts);
    }
}
