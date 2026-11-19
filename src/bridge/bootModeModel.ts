/** Boot scenes are complete global presets. Game-specific records are never stored here. */
import { retireBootControllerSnapshots } from './bootModeSharedShortcuts';
import { screenTouchpadDefaults, normalizeScreenTouchpadSummon } from './screenTouchpadDefaults';
import { readInputApplicationProof, readInputApplicationRequest, sameInputApplication, type InputApplicationProof } from './inputApplicationReceipt';
import { equalSettingsDocuments } from './settingsBaseline';
const XBOX_DEFAULTS_VERSION = 1;
const AUDIO_DEFAULTS_VERSION = 1;
const CONTROLLER_DEFAULTS_VERSION = 2;
export const BOOT_MODE_IDS = ['global', 'xbox', 'steam', 'xbox-steam'] as const;
export type BootModeId = typeof BOOT_MODE_IDS[number];
export const BOOT_MODE_MODULES = ['performance', 'charge', 'input', 'gyro', 'touch', 'audioOptimization', 'frameGeneration', 'softwareLaunch', 'steamAppearance', 'steamWallpaper', 'steamDecky', 'monitor', 'display', 'background', 'sleep'] as const;
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
    /** Preparation of the selected next boot, distinct from runtime application. */
    preparation?: BootModePreparation;
    /** Legacy global modules whose original intent is absent. Display references
     * are not persisted here; acknowledged ordinary leaves may remain partial. */
    unresolvedGlobal?: BootModeModule[];
    global: BootModeSnapshot;
    activeSnapshot: BootModeSnapshot;
    scenes: Record<Exclude<BootModeId, 'global'>, BootModeScene>;
    presets: Partial<Record<BootModeId, Record<string, BootModeScene>>>;
}
export function sameBootModeEditableContent(left: BootModeDocument, right: BootModeDocument): boolean {
    const content = (doc: BootModeDocument) => [doc.global, doc.scenes, doc.presets, doc.unresolvedGlobal];
    return equalSettingsDocuments(content(left), content(right));
}
export const BOOT_MODULE_STATES = ['unchanged', 'disabled', 'saved', 'requested', 'waiting', 'active', 'failed', 'skipped'] as const;
export interface BootModuleReceipt {
    state: typeof BOOT_MODULE_STATES[number];
    detail?: string;
    /** Immutable intent of this startup, independent of future edited drafts. */
    requested?: BootConfig;
    proof?: InputApplicationProof;
}
export type BootModuleReceipts = Partial<Record<BootModeModule, BootModuleReceipt>>;
export const BOOT_PREPARATION_STEPS = ['legacyMouse', 'windows', 'steam', 'audioStartup', 'frameStartup'] as const;
export type BootPreparationStep = typeof BOOT_PREPARATION_STEPS[number];
export interface BootPreparationStepReceipt {
    state: 'waiting' | 'requested' | 'confirmed' | 'failed' | 'skipped';
    detail?: string;
    observed?: BootConfig;
}
export interface BootModePreparation {
    schema: 1;
    mode: BootModeId;
    enabled: boolean;
    revision: number;
    /** Missing boot identity stays unknown; preparation is never boot execution proof. */
    observedBoot: string | null;
    status: 'preparing' | 'prepared' | 'partial';
    steps: Record<BootPreparationStep, BootPreparationStepReceipt>;
}
export function normalizeBootPreparation(raw: unknown, revision: number): BootModePreparation | undefined {
    if (!object(raw) || raw.schema !== 1 || !isBootModeId(raw.mode) || typeof raw.enabled !== 'boolean' ||
        !Number.isSafeInteger(raw.revision) || Number(raw.revision) < 0 || Number(raw.revision) > revision ||
        !(raw.observedBoot === null || typeof raw.observedBoot === 'string' && raw.observedBoot.length > 0 && raw.observedBoot.length <= 500) ||
        !['preparing', 'prepared', 'partial'].includes(String(raw.status)) || !object(raw.steps) ||
        Object.keys(raw.steps).some(key => !(BOOT_PREPARATION_STEPS as readonly string[]).includes(key))) return;
    const steps = {} as BootModePreparation['steps'];
    for (const id of BOOT_PREPARATION_STEPS) {
        const row = raw.steps[id];
        if (!object(row) || !['waiting', 'requested', 'confirmed', 'failed', 'skipped'].includes(String(row.state)) ||
            row.detail !== undefined && typeof row.detail !== 'string' || row.observed !== undefined && !object(row.observed) ||
            row.state === 'confirmed' && (!object(row.observed) || Object.keys(row.observed).length === 0)) return;
        if (row.state === 'skipped' && !(raw.mode === 'global' && id !== 'steam')) return;
        if (row.state === 'confirmed') {
            const observed = row.observed as Record<string, unknown>;
            if (id === 'legacyMouse' && !['removed', 'not-present-or-unrelated-preserved'].includes(String(observed.legacyJoyxoff))) return;
            if (id === 'windows') {
                const xbox = raw.enabled && (raw.mode === 'xbox' || raw.mode === 'xbox-steam');
                if (raw.mode === 'global' || observed.startupValue !== (xbox ? 1 : 0) ||
                    observed.homeApp !== (xbox ? 'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App' : '') ||
                    !(observed.deviceForm === null || Number.isInteger(observed.deviceForm) && Number(observed.deviceForm) >= 0 && Number(observed.deviceForm) <= 0xffffffff) ||
                    raw.enabled && observed.deviceForm !== 46) return;
            }
            if (id === 'steam') {
                const enabled = raw.enabled && (raw.mode === 'steam' || raw.mode === 'xbox-steam'), service = observed.service, file = observed.file;
                if (observed.known !== true || observed.enabled !== enabled || observed.conflict !== false ||
                    observed.command !== (enabled ? 'recognized' : 'empty-or-absent') || !object(service) ||
                    ['known', 'installed', 'automatic', 'running'].some(key => typeof service[key] !== 'boolean') ||
                    enabled && !['known', 'installed', 'automatic', 'running'].every(key => service[key] === true) ||
                    !object(file) || file.known !== true || typeof file.exists !== 'boolean' || file.exists !== enabled ||
                    !Number.isSafeInteger(file.bytes) || Number(file.bytes) < 0 ||
                    (enabled ? file.canonical !== true || Number(file.bytes) === 0 || file.encoding !== 'utf-8' : file.bytes !== 0 || file.canonical !== false)) return;
            }
            if (id === 'audioStartup' && (observed.enabled !== false || observed.verification !== 'startup-shortcut-readback')) return;
            if (id === 'frameStartup' && (observed.enabled !== false || observed.verification !== 'startup-file-readback')) return;
        }
        steps[id] = { state: row.state as BootPreparationStepReceipt['state'],
            ...(typeof row.detail === 'string' ? { detail: row.detail.slice(0, 1000) } : {}),
            ...(object(row.observed) ? { observed: normalizeBootConfig(row.observed) } : {}) };
    }
    if (raw.status === 'prepared' && Object.values(steps).some(row => !['confirmed', 'skipped'].includes(row.state))) return;
    return { schema: 1, mode: raw.mode, enabled: raw.enabled, revision: Number(raw.revision),
        observedBoot: raw.observedBoot as string | null, status: raw.status as BootModePreparation['status'], steps };
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
    modules?: BootModuleReceipts;
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
    let modules: BootModuleReceipts | undefined;
    const unprovedInput: Array<"input" | "gyro"> = [];
    if (raw.modules !== undefined) {
        if (!object(raw.modules)) return;
        modules = {};
        for (const id of BOOT_MODE_MODULES) {
            const value = raw.modules[id];
            if (value === undefined) continue;
            if (!object(value) || !BOOT_MODULE_STATES.includes(value.state as BootModuleReceipt['state']) ||
                value.detail !== undefined && typeof value.detail !== 'string' ||
                value.requested !== undefined && !object(value.requested)) return;
            const proof = value.proof === undefined ? undefined : readInputApplicationProof(value.proof);
            if (value.proof !== undefined && (!['input', 'gyro'].includes(id) || !proof)) return;
            if (proof) {
                const request = readInputApplicationRequest(object(value.requested) ? value.requested.inputApplication : undefined);
                const expected = (id === 'input' ? request?.target : request?.gyro) ? 'active' : 'disabled';
                if (!request || proof.boot !== raw.boot || !sameInputApplication(request, proof) || value.state !== expected ||
                    (id === 'input' ? proof.inputState : proof.gyroState) !== expected) return;
            }
            const unproved = (id === 'input' || id === 'gyro') && (value.state === 'active' || value.state === 'disabled') && !proof;
            if (unproved) unprovedInput.push(id as 'input' | 'gyro');
            modules[id] = { state: unproved ? 'saved' : value.state as BootModuleReceipt['state'],
                ...(unproved ? { detail: '旧记录缺少本次配置的原生生效回执；仅确认配置已保存' } : typeof value.detail === 'string' ? { detail: value.detail.slice(0, 1000) } : {}),
                ...(value.requested !== undefined ? { requested: normalizeBootConfig(value.requested) } : {}), ...(proof ? { proof } : {}) };
        }
    }
    return {
        schema: 1, boot: observedBoot, mode: raw.mode,
        sourceRevision: Number(raw.sourceRevision), revision: Number(raw.revision),
        status: raw.status === 'failed' ? 'failed' : unprovedInput.length ? 'partial' : raw.status as BootStartupResult['status'],
        ...(modules ? { modules } : {}),
        ...Object.fromEntries(lists.map(key => {
            const values = (raw[key] as string[]).slice(0, 64).map(value => value.slice(0, 500));
            if (key === 'applied') return [key, values.filter(value => !unprovedInput.some(id => value === (id === 'input' ? '虚拟手柄' : '陀螺仪') || value === '全局手柄与陀螺仪'))];
            if (key === 'pending') {
                const waiting = unprovedInput.map(id => id === 'input' ? '手柄输出：等待本次配置的原生输出回执' : '陀螺仪：等待本次配置的原生传感器回执').filter(value => !values.includes(value));
                return [key, [...waiting, ...values].slice(0, 64)];
            }
            return [key, values];
        })) as Pick<BootStartupResult, 'applied' | 'skipped' | 'pending'>,
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
    { id: 'performance', label: '性能与充电', icon: 'gauge', modules: ['performance', 'charge'] },
    { id: 'optimization', label: '优化软件', icon: 'rocket', modules: ['audioOptimization', 'frameGeneration', 'softwareLaunch'] },
    { id: 'input', label: '手柄与陀螺仪', icon: 'gamepad', modules: ['input', 'gyro'] },
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
function normalizeTouchModule(value: BootConfig, seedMissing=false): BootConfig {
    const profiles = object(value.profiles) ? { ...value.profiles } : {};
    for(const persona of ['disabled','steamdeck','dualsense-edge','elite'] as const) {
        if(object(profiles[persona])) profiles[persona]=normalizeScreenTouchpadSummon(profiles[persona] as BootConfig);
        else if(seedMissing) {
            const defaults=screenTouchpadDefaults(persona);
            profiles[persona]={summonPosition:defaults.summonPosition,summonEnabled:defaults.summonEnabled,
                summonPlacementVersion:1,autoHideSeconds:defaults.autoHideSeconds};
        }
    }
    return { ...value, profiles };
}
function overrides(value: unknown, migrateTouch=true): Partial<BootModeSnapshot> {
    const out: Partial<BootModeSnapshot> = {};
    // Legacy fan snapshots are retired. The common startup preference and
    // original fan preset/curve table are independent of every mode bank.
    if (object(value))
        for (const id of BOOT_MODE_MODULES)
            if (object(value[id]))
                out[id] = id === 'touch' && migrateTouch ? normalizeTouchModule(normalizeBootConfig(value[id])) : normalizeBootConfig(value[id]);
    return out;
}
// Stored modules are partial intent, so missing fields stay missing. Reject
// malformed known fields before they reach Vue or a startup writer; never turn
// a corrupt object into fabricated OFF/default settings and persist it.
type BootShape = ((value: unknown) => boolean) | { [key: string]: BootShape };
const flag = (v: unknown) => v === null || typeof v === 'boolean';
const numeric = (v: unknown) => v === null || typeof v === 'number' && Number.isFinite(v);
const string = (v: unknown) => v === null || typeof v === 'string';
const choice = (...values: (string | number)[]) => (v: unknown) => v === null || values.includes(v as string | number);
const strings = (v: unknown) => Array.isArray(v) && v.every(item => typeof item === 'string');
const nullableRecord = (v: unknown) => v === null || object(v);
const core = choice('bigFirst', 'bigOnly', 'littleFirst', 'littleOnly');
const bank: BootShape = { mode: choice('auto', 'manual'), tdp: numeric, tdpCeiling: numeric,
    auto: { bias: numeric, saving: choice('none', 'small', 'medium', 'large', 'aggressive'), coreSplit: numeric, coreLow: core, coreHigh: core },
    manual: { frequencyMHz: numeric, unlimited: flag, aggressiveness: numeric, turbo: flag, coreMode: core } };
const controls: BootShape = { ac: bank, dc: bank };
const motion: BootShape = { enabled: flag, virtualPadLink: flag, preset: choice('fps', 'racing', 'custom', 'steam'),
    motionMode: choice('on', 'off', 'toggle', 'suppress'), motionTrigger: string, outputStick: choice('left', 'right'), outputAxis: choice('xy', 'x', 'y'),
    presets: { '*': nullableRecord }, calibration: nullableRecord };
const touchProfile: BootShape = { masterEnabled: flag, enabled: flag, specialEnabled: flag, rearEnabled: flag, summonEnabled: flag,
    summonPosition: choice('off','same','opposite','left','right'), summonPlacementVersion: numeric,
    specialMask: numeric, rearMask: numeric, layout: choice('off', 'single', 'dual'), standaloneSpecialMode: choice('off', 'steamdeck', 'ps5'),
    transparency: numeric, mouseSensitivity: numeric, scale: numeric, autoHideSeconds: numeric };
const monitorPanel: BootShape = { position: numeric, scale: numeric };
const moduleShapes: Record<BootModeModule, BootShape> = {
    performance: { powerSchemes: { ac: choice('yeman', 'besteff', 'bal', 'bestperf'), dc: choice('yeman', 'besteff', 'bal', 'bestperf') }, controls, schedule: { configured: flag, enabled: flag, controls }, frames: { ac: { fps: numeric, lastFps: numeric, ceiling: numeric }, dc: { fps: numeric, lastFps: numeric, ceiling: numeric } } },
    charge: { enabled: flag, limit: numeric },
    input: { persona: v => typeof v === 'string' && ['disabled', 'elite', 'steamdeck', 'dualsense-edge'].includes(v), mouse: choice('off', 'microsoft', 'joyxoff', 'steamdeck'),
        outputTarget: { persona: choice('disabled', 'elite', 'steamdeck', 'dualsense-edge'), buttonMappingEnabled: flag, gyroEnabled: flag } },
    gyro: { motion }, touch: { profiles: { '*': touchProfile } },
    audioOptimization: { enabled: flag }, frameGeneration: { enabled: flag },
    softwareLaunch: { paths: strings, timing: choice('scene', 'steam', 'manual'), delay: numeric },
    steamAppearance: { states: { '*': flag } }, steamWallpaper: { enabled: flag, download: flag, cpuThreshold: numeric }, steamDecky: { enabled: flag },
    monitor: { preferences: { enabled: flag, ymccEnabled: flag, rtssEnabled: flag, ymccTemplate: choice('A', 'B'), ymcc: monitorPanel, templates: { '*': monitorPanel } } },
    display: { brightness: numeric, resolution: string, hz: v => string(v) || numeric(v) }, background: { ui: { theme: string, backgroundOpacity: numeric, backgroundBlur: numeric, dynamicBackgroundEnabled: flag } },
    sleep: { config: { mode: choice('off', 'custom'), enabled: flag, pauseGameOnSleep: flag, retryOnEntryFailure: flag, retryOnSleepTimeout: flag, retryOnNonUserWake: flag, wakePasswordEnabled: flag, sleepPowerPlanOptimizationEnabled: flag, staticWakeAutoScanEnabled: flag },
        acButton: numeric, dcButton: numeric, hibernate: flag, hibernateSize: numeric, timeouts: v => v === null || object(v) && Object.values(v).every(numeric), steamAutoRecovery: flag },
};
function assertBootShape(value: unknown, shape: BootShape, path: string): void {
    if (typeof shape === 'function') { if (shape(value)) return; }
    else if (object(value)) {
        for (const [key, child] of Object.entries(value)) {
            if (['__proto__', 'prototype', 'constructor'].includes(key)) continue;
            const rule = Object.prototype.hasOwnProperty.call(shape, key) ? shape[key] : shape['*'];
            if (rule) assertBootShape(child, rule, path + '.' + key);
        }
        return;
    }
    throw new Error('开机模式数据格式异常：' + path + '。原配置已保留，请修复该字段或恢复备份后重新读取。');
}
function assertBootSnapshot(value: unknown, path: string): void {
    if (!object(value)) throw new Error('开机模式数据格式异常：' + path + '。原配置已保留。');
    for (const module of BOOT_MODE_MODULES)
        if (Object.prototype.hasOwnProperty.call(value, module)) assertBootShape(value[module], moduleShapes[module], path + '.' + module);
}
function assertStoredBootDocument(raw: Record<string, unknown>): void {
    if (raw.unresolvedGlobal !== undefined && (!Array.isArray(raw.unresolvedGlobal) ||
        raw.unresolvedGlobal.some(value => value !== 'fan' && !BOOT_MODE_MODULES.includes(value as BootModeModule)) ||
        new Set(raw.unresolvedGlobal).size !== raw.unresolvedGlobal.length))
        throw new Error('开机模式全局数据来源标记无效，原配置已保留');
    for (const key of ['current', 'boot']) if (raw[key] !== undefined && !isBootModeId(raw[key])) throw new Error('开机模式标识无效：' + key);
    if (raw.bootEnabled !== undefined && typeof raw.bootEnabled !== 'boolean') throw new Error('开机模式开关格式无效');
    if (raw.revision !== undefined && (!Number.isSafeInteger(raw.revision) || Number(raw.revision) < 0)) throw new Error('开机模式版本号无效');
    if (raw.preparation !== undefined) {
        const preparation = normalizeBootPreparation(raw.preparation, Number(raw.revision));
        if (!preparation || preparation.mode !== raw.boot || preparation.enabled !== (raw.bootEnabled !== false))
            throw new Error('开机准备回执无效，原配置已保留');
    }
    for (const key of ['global', 'activeSnapshot']) if (raw[key] !== undefined) assertBootSnapshot(raw[key], key);
    if (raw.scenes !== undefined) {
        assertBootShape(raw.scenes, {}, 'scenes');
        for (const id of ['xbox', 'steam', 'xbox-steam']) {
            const scene = (raw.scenes as Record<string, unknown>)[id];
            if (scene === undefined) continue;
            assertBootShape(scene, {}, 'scenes.' + id);
            for (const key of ['overrides', 'rememberedOverrides']) if ((scene as Record<string, unknown>)[key] !== undefined)
                assertBootSnapshot((scene as Record<string, unknown>)[key], 'scenes.' + id + '.' + key);
        }
    }
    if (raw.presets !== undefined) {
        assertBootShape(raw.presets, {}, 'presets');
        for (const id of BOOT_MODE_IDS) {
            const presets = (raw.presets as Record<string, unknown>)[id];
            if (presets === undefined) continue;
            assertBootShape(presets, {}, 'presets.' + id);
            for (const [name, entry] of Object.entries(presets as Record<string, unknown>).slice(0, 30)) {
                if (['__proto__', 'prototype', 'constructor'].includes(name)) continue;
                assertBootShape(entry, {}, 'presets.' + id + '.' + name);
                for (const key of ['overrides', 'rememberedOverrides']) if ((entry as Record<string, unknown>)[key] !== undefined)
                    assertBootSnapshot((entry as Record<string, unknown>)[key], 'presets.' + id + '.' + name + '.' + key);
            }
        }
    }
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
    assertBootSnapshot(baseline, 'baseline');
    const global = cloneBootValue(baseline), activeSnapshot = cloneBootValue(baseline);
    // A legacy document can itself be supplied as the normalization baseline.
    // Retire its fan module there too, before merging any saved mode fields.
    Reflect.deleteProperty(global, 'fan');
    Reflect.deleteProperty(activeSnapshot, 'fan');
    retireBootControllerSnapshots({ global, activeSnapshot });
    global.touch=normalizeTouchModule(global.touch,true);
    global.input = { ...global.input, persona: 'disabled',
        outputTarget: { ...normalizeBootConfig(global.input.outputTarget), persona: 'disabled', buttonMappingEnabled: false },
        ...(global.input.mouse === 'steamdeck' ? { mouse: 'off' } : {}) };
    global.audioOptimization = { ...global.audioOptimization, enabled: false };
    const xbox: BootModeScene = { name: '推荐预设', overrides: {} }, steam = steamRecommendedScene(), combined = steamRecommendedScene();
    xbox.overrides.audioOptimization = { enabled: false };
    combined.overrides.audioOptimization = { enabled: true };
    steam.overrides.audioOptimization = { enabled: true };
    return { schema: 1, xboxDefaultsVersion: XBOX_DEFAULTS_VERSION, controllerDefaultsVersion: CONTROLLER_DEFAULTS_VERSION, audioDefaultsVersion: AUDIO_DEFAULTS_VERSION, revision: 0, current: 'global', boot: 'global', bootEnabled: false, global, activeSnapshot, scenes: { xbox, steam, 'xbox-steam': combined }, presets: {} };
}
export function normalizeBootModeDocument(raw: unknown, baseline: BootModeSnapshot): BootModeDocument {
    if (object(raw) && raw.schema !== undefined && raw.schema !== 1)
        throw new Error('开机模式配置版本不受支持，原配置已保留，请使用兼容版本。');
    if (object(raw) && raw.schema === 1) assertStoredBootDocument(raw);
    const out = createBootModeDocument(baseline);
    if (!object(raw) || raw.schema !== 1) return out;
    const storedGlobal = object(raw.global) ? raw.global : {};
    const unresolved = BOOT_MODE_MODULES.filter(module => !Object.prototype.hasOwnProperty.call(storedGlobal, module) ||
        (raw.unresolvedGlobal as BootModeModule[] | undefined)?.includes(module));
    if (unresolved.length) out.unresolvedGlobal = unresolved;
    // A missing legacy global module cannot inherit an applied scene or an
    // editor's reference capture. Keep only its explicitly saved leaves.
    for (const module of unresolved) out.global[module] = {};
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
    if (raw.preparation !== undefined) out.preparation = normalizeBootPreparation(raw.preparation, out.revision);
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
        // Match the historical factory recipe, independent of evolving defaults.
        // Exact matching keeps every player-edited profile authoritative.
        const oldTouch = normalizeTouchModule({ profiles: { steamdeck: {
            masterEnabled: true, summonPosition: 'off', standaloneSpecialMode: 'off', specialMask: 3, rearMask: 0,
            summonEnabled: false, specialEnabled: true, rearEnabled: false, enabled: true, layout: 'dual',
            singleMode: 'mouse', leftMode: 'steamdeck', rightMode: 'steamdeck', transparency: 80,
            mouseSensitivity: 100, scale: 100, touchFeedback: 'medium', autoHideSeconds: 60,
        } } });
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
        if (!unresolved.includes('audioOptimization') && typeof out.global.audioOptimization.enabled !== 'boolean')
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
                presets[name.slice(0, 60)] = { name: name.slice(0, 60), overrides: overrides(entry.overrides),
                    ...(object(entry.rememberedOverrides) ? { rememberedOverrides: overrides(entry.rememberedOverrides) } : {}) };
            }
            out.presets[id] = presets;
        }
    const active = overrides(raw.activeSnapshot,false);
    // Requested mode defaults are not proof of an applied runtime. Missing
    // legacy receipt fields fall back to the observed baseline, so saving or
    // reopening an editor cannot reclassify a pending default as already run.
    out.activeSnapshot = cloneBootValue(baseline);
    Reflect.deleteProperty(out.activeSnapshot, 'fan');
    for (const [module, value] of Object.entries(active))
        out.activeSnapshot[module as BootModeModule] = mergeBootConfig(baseline[module as BootModeModule], value);
    retireBootControllerSnapshots(out);
    out.global.touch=normalizeTouchModule(out.global.touch,!unresolved.includes('touch'));
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
/** Disabling a scene falls back to the saved global preferences. */
export function startupBootMode(document: BootModeDocument): BootModeId {
    return document.bootEnabled ? document.boot : 'global';
}
export function resolveBootMode(document: BootModeDocument, id: BootModeId): BootModeSnapshot {
    if (!isBootModeId(id)) throw new Error('开机模式标识无效');
    const snapshot = cloneBootValue(document.global);
    Reflect.deleteProperty(snapshot, 'fan');
    if (id !== 'global')
        for (const module of BOOT_MODE_MODULES) {
            const value = document.scenes[id].overrides[module];
            if (value)
                snapshot[module] = mergeBootConfig(snapshot[module], value);
        }
    retireBootControllerSnapshots({ global: snapshot });
    return snapshot;
}
export function bootGlobalModuleUnresolved(document: BootModeDocument, module: BootModeModule): boolean {
    return document.unresolvedGlobal?.includes(module) === true;
}
/** Current values are references for an explicit edit, never recovered intent. */
export function previewBootMode(document: BootModeDocument, id: BootModeId, reference: BootModeSnapshot): BootModeSnapshot {
    const snapshot = resolveBootMode(document, id);
    for (const module of document.unresolvedGlobal ?? []) snapshot[module] = mergeBootConfig(reference[module] ?? {}, snapshot[module]);
    return snapshot;
}
function coversReference(reference: BootValue | BootConfig, intent: BootValue | undefined): boolean {
    if (Array.isArray(reference)) return Array.isArray(intent);
    if (object(reference)) return object(intent) && Object.keys(reference).every(key => coversReference(reference[key] as BootValue, intent[key] as BootValue));
    return intent !== undefined && intent !== null;
}
/** Complete independent intent remains usable. Missing inherited fields stay
 * unconfirmed, even when a runtime reference can preserve them unchanged. */
export function unresolvedBootModeModules(document: BootModeDocument, id: BootModeId, reference: BootModeSnapshot): BootModeModule[] {
    const intent = resolveBootMode(document, id);
    return (document.unresolvedGlobal ?? []).filter(module => id === 'global' ||
        !document.scenes[id].overrides[module] || !coversReference(reference[module], intent[module]));
}
export function adoptBootGlobalModule(document: BootModeDocument, module: BootModeModule, value: BootConfig): void {
    if (!BOOT_MODE_MODULES.includes(module)) throw new Error('全局栏目无效');
    assertBootShape(value, moduleShapes[module], 'global.' + module);
    if (!Object.keys(value).length) throw new Error('此栏目尚无可确认的数据');
    document.global[module] = cloneBootValue(value);
    const remaining = (document.unresolvedGlobal ?? []).filter(id => id !== module);
    if (remaining.length) document.unresolvedGlobal = remaining;
    else delete document.unresolvedGlobal;
}
export function setBootModeModuleScope(document: BootModeDocument, id: BootModeId, module: BootModeModule, own: boolean, reference?: BootModeSnapshot): void {
    if (id === 'global' || !BOOT_MODE_MODULES.includes(module))
        return;
    if (own) {
        if (!document.scenes[id].overrides[module])
            document.scenes[id].overrides[module] = cloneBootValue(document.scenes[id].rememberedOverrides?.[module] ??
                (reference && bootGlobalModuleUnresolved(document, module) ? previewBootMode(document, id, reference)[module] : document.global[module]));
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
export type BootDeviceCapabilities = Partial<Record<'hybridCores' | 'performance' | 'charge' | 'gyro' | 'hibernate', BootDeviceSupport>>;
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
