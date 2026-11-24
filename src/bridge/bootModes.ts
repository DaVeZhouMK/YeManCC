import { normalizeScreenTouchpadSummon } from './screenTouchpadDefaults';
/** Scenes replace the one global configuration; dedicated game profiles keep priority. */
import { loadSettings, updateSettings, updateSettingsForInputReceipt, compareAndSwapInputSettings, getSettingsRecoveryState, getSettingsGeneration, assertSettingsGeneration, type UnifiedSettings, type InputSettingsSnapshot } from './settingsRepository';
import { inputApplicationRequest, readInputApplicationRequest, inputApplicationFromGamepad, sameInputApplication, type InputApplicationProof, type InputApplicationRequest } from './inputApplicationReceipt';
import { cloneBootValue, normalizeBootConfig, normalizeBootModeDocument, normalizeBootStartupResult, previewBootMode, unresolvedBootModeModules, mergeBootConfig, isBootModeId, type BootStartupResult, type BootModuleReceipt, type BootModuleReceipts, resolveBootMode, executeBootModeSteps, BOOT_MODE_MODULES, type BootModeDocument, type BootModeId, type BootModeCategory, type BootModeSnapshot, type BootModeModule, type BootDeviceCapabilities, type BootApplyStep } from './bootModeModel';
import { BOOT_PREPARATION_STEPS, normalizeBootPreparation, sameBootModeEditableContent, startupBootMode, type BootModePreparation, type BootPreparationStep, type BootConfig } from './bootModeModel';
import { normalizeContinuousControls, normalizeControlPowerSchemes, performanceConsumptionSignature } from './performanceControls';
import { continuousControlsFromSchedule, snapshotPerformanceSchedule, loadPerformanceSchedule, applyPerformanceConfiguration, onContinuousPerformanceApplied, type ContinuousPerformanceApplicationContext } from './performanceSchedule';
import { getPolicyGame } from './gamePolicyTarget';
import { isGameStatusCurrent } from './gamedetect';
import { frameRatePair } from './frameRateModel';
import { normalizeMonitorOutput } from './monitorHudModel';
import { readMonitorOutputPreferences, saveMonitorOutputPreferences, monitorOutputCoordinator, onMonitorOutputApplied } from './monitorOutput';
import { screenTouchpadsGet, screenTouchpadsSaveProfiles, screenTouchpadDefaults, type ScreenTouchpadPersona, type ScreenTouchpadConfig } from './screenTouchpads';
import { getMouseModeState, setMouseModeEnabled, setMouseModePolicy, setMouseBackend, type MouseModeState, type MouseBootContext } from './gameproc';
import { batteryCharge } from './batteryCharge';
import { steamUi, type SteamUiState } from './steamUi';
import { steamAppearance } from './steamAppearance';
import { deckySidebar } from './deckySidebar';
import { getUiSettings, loadUiSettings, setUiSettings, type UiSettings } from './uiSettings';
import { applyTheme } from './theme';
import { setTrayResident } from './trayResident';
import { shell, display, proc, fs } from './api';
import { normalizeInputStartupPreferences } from './inputStartupPolicy';
import { gyroMotionParamsForPreset } from './gyroPresetModel';
import { readBootDisplay, applyBootBrightness, readBootSteamStartup } from './bootModePlatform';
import { invoke, on } from './ipc';
import {fxSound} from './fxSound';
import { equalSettingsDocuments } from './settingsBaseline';
import { computeInputConfigHash } from './inputConfigHash';
import {bootStartupReceiptOwner, projectBootPreferenceExtensions, type BootStartupReceiptOwner, type BootPreferenceWrite} from './bootModeGlobalPreferences';
import { readGamingHomeConfiguration, writeGamingHomeStartup, steamMasterSet, steamAddonExists, STEAM_ADDONS, fxExists, getTdpExecutionCapability, detectCoreArchitecture } from './yeman';
const PERSONAS = ['disabled', 'elite', 'steamdeck', 'dualsense-edge'] as const;
const MODULE_KEY = 'bootModes';
const object = (value: unknown): Record<string, any> => value && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, any> : {};
const same = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);
const text = (error: unknown) => error instanceof Error ? error.message : String(error);
const applicationKey = (path: string) => path.trim().replace(/\//g, '\\').toLowerCase();
const steamConnected = (state: SteamUiState | null | undefined) => state?.known === true && state.running && state.pid > 0 && !!state.birth && state.birth !== '0';
const READ_TIMEOUT_MS = 4000;
const pendingReads = new Map<string, Promise<unknown>>();
let readGeneration = -1;
/** Read-only observations have a deadline. A timed-out request is reused until
 * it settles unless the observation is scoped to a single owner. A scoped
 * caller waits for a new opportunity instead of reusing another owner's read.
 * Writes deliberately never use this helper: a timeout is not cancellation. */
async function readOnce<T>(read: () => Promise<T>, label: string, reusePending = true): Promise<T> {
    const generation = getSettingsGeneration();
    if (readGeneration !== generation) { pendingReads.clear(); readGeneration = generation; }
    let pending = pendingReads.get(label) as Promise<T> | undefined;
    if (pending && !reusePending) throw Error(label + '读取尚未结束，请稍后重试');
    if (!pending) {
        pending = Promise.resolve().then(read);
        pendingReads.set(label, pending);
        const observed = pending;
        const forget = () => { if (pendingReads.get(label) === observed) pendingReads.delete(label); };
        void pending.then(forget, forget);
    }
    let timer: ReturnType<typeof setTimeout> | undefined;
    try {
        const result = await Promise.race([pending, new Promise<T>((_, reject) => {
            timer = setTimeout(() => reject(Error(label + '读取超时，请稍后重试')), READ_TIMEOUT_MS);
        })]);
        assertSettingsGeneration(generation);
        return result;
    } finally { if (timer !== undefined) clearTimeout(timer); }
}
async function optional<T>(read: () => Promise<T>, label: string): Promise<T | null> {
    try { return await readOnce(read, label); }
    catch { return null; }
}
export interface BootModeContext {
    startupResult?: BootStartupResult;
    requiresCapture?: boolean;
    initialSource?: BootModeInitialSource;
    document: BootModeDocument;
    baseline: BootModeSnapshot;
    support: BootDeviceCapabilities;
    apps: Array<{
        id?: string;
        name: string;
        path: string;
    }>;
}
export interface BootModeReceipt {
    mode: BootModeId;
    applied: string[];
    skipped: string[];
    pending: string[];
    modules?: BootModuleReceipts;
}
function recordModule(receipt: BootModeReceipt, id: BootModeModule, state: BootModuleReceipt['state'], detail?: string, requested?: BootModuleReceipt['requested']): void {
    receipt.modules ??= {};
    receipt.modules[id] = { state, ...(detail ? { detail } : {}), ...(requested ? { requested: cloneBootValue(requested) } : {}) };
}
let queue: Promise<unknown> = Promise.resolve();
const serialized = <T>(run: () => Promise<T>): Promise<T> => { const result = queue.then(run); queue = result.catch(() => { }); return result; };
const LABELS: Record<BootModeModule, string> = { performance: '性能调度', charge: '充电限制', input: '模拟鼠标', gyro: '陀螺仪', touch: '屏幕触摸配置', audioOptimization: '掌机音质优化', frameGeneration: '小黄鸭联动', softwareLaunch: '快捷应用联动', steamAppearance: 'Steam 美化', steamWallpaper: 'Steam 动态壁纸', steamDecky: 'Decky', monitor: '监控', display: '画面', background: 'YMCC 背景' };
const INPUT_PROOF_WAITING = '手柄输出：等待本次配置的原生输出回执';
const GYRO_PROOF_WAITING = '陀螺仪：等待本次配置的原生传感器回执';
const inputProofPending = (module: 'input' | 'gyro') => module === 'input' ? INPUT_PROOF_WAITING : GYRO_PROOF_WAITING;
const inputProofLabel = (module: 'input' | 'gyro') => module === 'input' ? '虚拟手柄' : LABELS.gyro;
function recordInputRequest(receipt: BootModeReceipt, input: InputSettingsSnapshot, excluded: ReadonlySet<BootModeModule> = new Set()): void {
    const request = inputApplicationRequest(input);
    for (const module of ['input', 'gyro'] as const) {
        if (excluded.has(module)) continue;
        recordModule(receipt, module, 'saved', '配置已保存；等待本次修订的原生生效回执',
            normalizeBootConfig({ inputApplication: request,
                ...(module === 'input' ? { outputTarget: input.outputTarget } : { motion: input.gyroMotion }) }));
        if (!receipt.pending.includes(inputProofPending(module))) receipt.pending.push(inputProofPending(module));
    }
}
const BACKGROUND_KEYS = ['theme', 'dynamicBackgroundEnabled', 'backgroundOpacity', 'backgroundBlur', 'videoBatteryPause'] as const;
function bootInputOwnership(value: unknown): Record<string, any> {
    const ownership = { ...object(value) };
    delete ownership.externalControllerPriority;
    delete ownership.fseWakeReconnect;
    return ownership;
}
const backgroundSettings = (value: UiSettings) => Object.fromEntries(BACKGROUND_KEYS.map(key => [key, value[key]]));
const TOUCH_KEYS = Object.keys(screenTouchpadDefaults('disabled')) as Array<keyof ScreenTouchpadConfig>;
function touchConfig(value: Partial<ScreenTouchpadConfig>) { const next=normalizeScreenTouchpadSummon(value);return Object.fromEntries(TOUCH_KEYS.filter(key => next[key] !== undefined).map(key => [key, next[key]])); }
interface BootModeSharedReads {
    charge: ReturnType<typeof optional<Awaited<ReturnType<typeof batteryCharge.get>>>>;
}
function sharedBootModeReads(): BootModeSharedReads {
    return { charge: optional(() => batteryCharge.get(), '充电限制状态') };
}
export async function captureBootModeSnapshot(shared?: BootModeSharedReads, observe?: (stage: string) => void, options?: { skipAppearance?: boolean }): Promise<BootModeSnapshot> {
    const generation = getSettingsGeneration();
    observe?.('settings');
    const settings = await readOnce(loadSettings, '开机模式设置');
    observe?.('preferences');
    const [config] = await Promise.all([readOnce(loadPerformanceSchedule, '性能调度设置'), readOnce(loadUiSettings, '界面设置')]);
    observe?.('owners');
    const [mouse, charge, monitor, fx, scaling, appearance, decky, screen, profiles] = await Promise.all([
        optional(getMouseModeState, '模拟鼠标状态'), shared?.charge ?? optional(() => batteryCharge.get(), '充电限制状态'), readOnce(readMonitorOutputPreferences, '监控设置'), optional(fxExists, '掌机音质优化'), optional(() => steamAddonExists('steamscale'), '小黄鸭联动设置'),
        options?.skipAppearance ? null : optional(() => steamAppearance.read(), 'Steam 美化设置'),
        optional(() => deckySidebar.get(), 'Decky 状态'), optional(readBootDisplay, '画面模式'),
        Promise.all(PERSONAS.map(async (persona) => [persona, await optional(() => screenTouchpadsGet(persona), '屏幕触摸配置 ' + persona)] as const)),
    ]);
    assertSettingsGeneration(generation);
    const input = settings.input;
    // Effective Steam ownership may belong to a dedicated game. Capture the base route only.
    const delegated = input.outputTarget.persona === 'steamdeck';
    const masked = mouse?.controlAllowed === false || mouse?.provider === 'steam';
    const baseBackend = masked ? settings.gamepad.mouseBackend : mouse?.backend;
    const basePolicy = masked ? settings.gamepad.mouseMode : mouse?.mode;
    const baseMouse = delegated ? 'steamdeck' : masked
        ? basePolicy === 'off' ? 'off' : basePolicy === 'on' && ['gamebar', 'joyxoff'].includes(baseBackend)
            ? baseBackend === 'gamebar' ? 'microsoft' : 'joyxoff' : null
        : mouse?.stateKnown === false ? null : mouse ? mouse.on ? mouse.backend === 'gamebar' ? 'microsoft' : 'joyxoff' : 'off' : null;
    // A failed read stays unknown. It must not become a fabricated OFF value later applied to hardware.
    const chargeLimit = charge?.ok === true ? charge.requestedLimit ?? charge.currentLimit ?? null : null;
    const chargeEnabled = charge?.ok !== true ? null : charge.managed === false ? false : typeof chargeLimit === 'number' ? chargeLimit < 100 : null;
    return cloneBootValue({
        performance: { schedule: config, powerSchemes: normalizeControlPowerSchemes(config.powerSchemes), controls: continuousControlsFromSchedule(config), frames: frameRatePair(settings.rtss.frameRates) },
        charge: { enabled: chargeEnabled, limit: chargeLimit },
        input: { persona: input.outputTarget.persona, outputTarget: input.outputTarget, ownership: bootInputOwnership(input.ownership), mouseBackend: baseBackend ?? null, mousePolicy: basePolicy ?? null, mouse: baseMouse },
        gyro: { motion: input.gyroMotion },
        touch: { profiles: Object.fromEntries(profiles.filter(([, state]) => state).map(([persona, state]) => [persona, touchConfig(state!)])) },
        audioOptimization: { enabled: fx }, frameGeneration: { enabled: scaling }, softwareLaunch: { paths: [], timing: 'scene', delay: 0 },
        steamAppearance: { states: appearance ? Object.fromEntries(appearance.themes.map(item => [item.name, item.configured])) : {} },
        steamWallpaper: { enabled: getUiSettings().steamDynamicWallpaperEnabled, download: getUiSettings().steamWallpaperBackgroundDownload, cpuThreshold: getUiSettings().steamWallpaperDownloadCpuThreshold },
        steamDecky: { enabled: decky?.enabled ?? null }, monitor: { preferences: monitor },
        display: screen ?? { brightness: null, resolution: 'native', hz: 'native' }, background: { ui: backgroundSettings(getUiSettings()) },
    } as unknown as BootModeSnapshot);
}
export async function readBootModeSupport(category?: BootModeCategory, shared?: BootModeSharedReads): Promise<BootDeviceCapabilities> {
    const generation = getSettingsGeneration();
    const performance = !category || category === 'performance';
    const [core, tdp, charge] = await Promise.all([
        performance ? optional(detectCoreArchitecture, 'CPU 核心架构') : null,
        performance ? optional(getTdpExecutionCapability, '性能调度能力') : null,
        performance ? shared?.charge ?? optional(() => batteryCharge.get(), '充电限制状态') : null,
    ]);
    assertSettingsGeneration(generation);
    return {
        ...(performance ? {
            hybridCores: { state: core?.detected ? (core.heterogeneous ? 'supported' : 'unsupported') : 'unknown', reason: core?.detected ? '按本机 CPU 架构判断' : '尚未确认 CPU 架构' },
            performance: { state: tdp?.ready ? 'supported' : 'unknown', reason: tdp?.error || '实际功耗范围由原性能调度接口校验' },
            charge: { state: charge?.supported ? 'supported' : charge?.kind === 'unsupported' ? 'unsupported' : 'unknown', reason: charge?.reason || '充电能力尚未确认', choices: charge?.choices },
        } : {}),
        ...(!category || category === 'input' ? { gyro: { state: 'unknown', reason: '启用时由原输入接口确认设备与体感来源' } } : {}),
    };
}
/** Opaque renderer-local provenance for the first unsaved mode document.
 * Neither its token nor its captured settings are serialized into preferences. */
export interface BootModeInitialSource { readonly generation: number; }
const initialSourceSnapshots = new WeakMap<BootModeInitialSource, Record<string, unknown>>();
const initialDocumentSources = new WeakMap<BootModeDocument, BootModeInitialSource>();
function firstModeSourceValues(settings: UnifiedSettings): Record<string, unknown> {
    const values: Record<string, unknown> = {};
    const paths = ['performanceSchedule', 'rtss/frameRates', 'rtss/templateDefaults', 'rtss/monitorOutput',
        'startupDesired/virtualGamepadPersona', 'startupDesired/gyroPreset',
        'gamepad/mouseMode', 'gamepad/mouseBackend', 'screenTouchpads/profiles',
        'extensions/deckySidebar/enabled', 'extensions/bootModes', 'ui/theme', 'ui/dynamicBackgroundEnabled',
        'ui/backgroundOpacity', 'ui/backgroundBlur', 'ui/videoBatteryPause', 'ui/steamDynamicWallpaperEnabled',
        'ui/steamWallpaperBackgroundDownload', 'ui/steamWallpaperDownloadCpuThreshold'];
    for (const path of paths) {
        let value: any = settings, present = true;
        for (const key of path.split('/')) {
            if (!value || typeof value !== 'object' || !Object.prototype.hasOwnProperty.call(value, key)) { present = false; break; }
            value = value[key];
        }
        values[path] = present ? { present: true, value: cloneBootValue(value) } : { present: false };
    }
    const input = cloneBootValue(object(settings.input));
    for (const key of ['appliedRevision', 'appliedConfigHash', 'pendingConfigHash', 'hostAcknowledgedRevision', 'hostAcknowledgedConfigHash', 'applyStatus']) delete input[key];
    values.input = input;
    return values;
}
function assertInitialModeSource(settings: UnifiedSettings, source?: BootModeInitialSource): void {
    if (!source && object(settings.extensions?.[MODULE_KEY]).schema === 1) return;
    const expected = source && initialSourceSnapshots.get(source);
    if (!expected || source!.generation !== getSettingsGeneration() || !equalSettingsDocuments(expected, firstModeSourceValues(settings)))
        throw Error('首次开机模式草稿来源已变化或无法确认，请重新读取；原配置与草稿已保留');
}
export function readStoredBootMode(settings: UnifiedSettings, baseline: BootModeSnapshot): BootModeDocument {
    const saved = settings.extensions?.[MODULE_KEY];
    if (saved !== undefined && saved !== null && (typeof saved !== 'object' || Array.isArray(saved)))
        throw Error('开机模式配置格式无效，原配置已保留');
    const raw = object(saved);
    const document = normalizeBootModeDocument(raw, baseline);
    // Ordinary virtual toggles stay temporary. Only explicit startup choices
    // seed a new draft or fill absent legacy startup fields; saved OFF wins.
    const fresh = raw.schema !== 1;
    if (fresh) {
        // Existing durable choices win over temporary native state on every
        // first-document entry, including the full editor and startup restore.
        const durable = savedOverviewSnapshot(settings, baseline.performance.schedule as any);
        for (const module of ['input', 'gyro', 'background', 'steamWallpaper', 'monitor'] as const)
            document.global[module] = cloneBootValue(durable[module]);
        const profiles = object(object(settings.screenTouchpads).profiles);
        const savedProfiles = Object.fromEntries(PERSONAS.filter(persona => profiles[persona] && typeof profiles[persona] === 'object' && !Array.isArray(profiles[persona]))
            .map(persona => [persona, touchConfig(profiles[persona])]));
        document.global.touch = mergeBootConfig(document.global.touch, { profiles: savedProfiles });
        const decky = object(settings.extensions?.deckySidebar).enabled;
        if (typeof decky === 'boolean') document.global.steamDecky.enabled = decky;
    }
    const startup = normalizeInputStartupPreferences(settings.startupDesired);
    const input = document.global.input;
    if (fresh || !Object.prototype.hasOwnProperty.call(input, 'persona') && Object.prototype.hasOwnProperty.call(settings.startupDesired ?? {}, 'virtualGamepadPersona')) {
        const persona = startup.virtualGamepadPersona;
        input.persona = persona;
        input.outputTarget = { ...object(input.outputTarget), persona,
            buttonMappingEnabled: persona !== 'disabled', gyroEnabled: persona !== 'disabled' && startup.gyroPreset !== 'off' };
        if (persona === 'steamdeck') input.mouse = 'steamdeck';
        else if (input.mouse === 'steamdeck') input.mouse = settings.gamepad.mouseMode === 'off' ? 'off'
            : settings.gamepad.mouseBackend === 'gamebar' ? 'microsoft' : 'joyxoff';
    }
    const motion = object(document.global.gyro.motion);
    if (fresh || !Object.prototype.hasOwnProperty.call(motion, 'enabled') && Object.prototype.hasOwnProperty.call(settings.startupDesired ?? {}, 'gyroPreset'))
        motion.enabled = input.persona !== 'disabled' && startup.gyroPreset !== 'off';
    if (fresh || (!Object.prototype.hasOwnProperty.call(motion, 'preset') && !Object.prototype.hasOwnProperty.call(motion, 'activePreset') && Object.prototype.hasOwnProperty.call(settings.startupDesired ?? {}, 'gyroPreset'))) {
        const preset = startup.gyroPreset;
        if (preset !== 'off') Object.assign(motion, gyroMotionParamsForPreset(preset, motion.presets), { preset, activePreset: preset, outputMode: 'virtual-stick' });
    }
    document.global.gyro = { ...document.global.gyro, motion };
    if (fresh) {
        const source = Object.freeze({ generation: getSettingsGeneration() });
        initialSourceSnapshots.set(source, firstModeSourceValues(settings));
        initialDocumentSources.set(document, source);
    }
    return document;
}
/** Overview reads durable preferences only. Unknown device values remain null;
 * this view must never substitute for the editor's live capture. */
function savedOverviewSnapshot(settings: UnifiedSettings, schedule = snapshotPerformanceSchedule(settings.performanceSchedule as any)): BootModeSnapshot {
    const input = settings.input, gamepad = settings.gamepad;
    const ui = { ...getUiSettings(), ...object(settings.ui) };
    const mouse = input.outputTarget.persona === 'steamdeck' ? 'steamdeck' : gamepad.mouseMode === 'off' ? 'off'
        : gamepad.mouseMode === 'on' ? gamepad.mouseBackend === 'gamebar' ? 'microsoft' : 'joyxoff' : null;
    return cloneBootValue({
        performance: { schedule, powerSchemes: normalizeControlPowerSchemes(schedule.powerSchemes), controls: continuousControlsFromSchedule(schedule), frames: frameRatePair(settings.rtss.frameRates) },
        charge: { enabled: null, limit: null },
        input: { persona: input.outputTarget.persona, outputTarget: input.outputTarget, ownership: bootInputOwnership(input.ownership),
            mouseBackend: gamepad.mouseBackend, mousePolicy: gamepad.mouseMode, mouse },
        gyro: { motion: input.gyroMotion },
        touch: { profiles: object(settings.screenTouchpads).profiles || {} },
        audioOptimization: { enabled: null }, frameGeneration: { enabled: null },
        softwareLaunch: { paths: [], timing: 'scene', delay: 0 }, steamAppearance: { states: {} },
        steamWallpaper: { enabled: ui.steamDynamicWallpaperEnabled, download: ui.steamWallpaperBackgroundDownload, cpuThreshold: ui.steamWallpaperDownloadCpuThreshold },
        steamDecky: { enabled: null }, monitor: { preferences: normalizeMonitorOutput(settings.rtss.monitorOutput) },
        display: { brightness: null, resolution: 'native', hz: 'native' }, background: { ui: backgroundSettings(ui) },
    } as unknown as BootModeSnapshot);
}
function assertBootSettingsReady(): void {
    const state = getSettingsRecoveryState();
    if (state.status !== 'ready' || !state.persisted)
        throw new Error('开机模式配置尚未安全读取：' + (state.message || '请重新读取后再选择或保存。'));
}
function nextBootRevision(revision: number): number {
    if (!Number.isSafeInteger(revision) || revision < 0 || !Number.isSafeInteger(revision + 1))
        throw new Error('开机模式版本号无效或已超出范围，未保存。');
    return revision + 1;
}
/** A new mode document must resolve first-install defaults through the same
 * hardware-aware reader as the performance page. Saved mode intent is never reseeded. */
async function initialPerformanceSchedule(settings: UnifiedSettings) {
    if (object(settings.extensions?.[MODULE_KEY]).schema === 1) return undefined;
    const schedule = await readOnce(loadPerformanceSchedule, '开机模式初始性能设置');
    const latest = await readOnce(loadSettings, '开机模式初始配置');
    if (!equalSettingsDocuments(settings.performanceSchedule, latest.performanceSchedule) ||
        !equalSettingsDocuments(settings.extensions?.[MODULE_KEY], latest.extensions?.[MODULE_KEY]))
        throw Error('性能配置或开机模式已变化，初始配置未保存，请重新读取');
    return schedule;
}
function storedOverviewBank(settings: UnifiedSettings): BootModeSnapshot | undefined {
    const raw = object(settings.extensions?.[MODULE_KEY]), global = object(raw.global);
    if (raw.schema !== 1 || Array.isArray(raw.unresolvedGlobal) && raw.unresolvedGlobal.some(module => module !== 'fan') ||
        !BOOT_MODE_MODULES.every(module => global[module] && typeof global[module] === 'object' && !Array.isArray(global[module]))) return;
    return Object.fromEntries(BOOT_MODE_MODULES.map(module => [module, normalizeBootConfig(global[module])])) as BootModeSnapshot;
}
export async function loadBootModes(options: { overviewOnly?: boolean; editorOnly?: boolean } = {}): Promise<BootModeContext> {
    const generation = getSettingsGeneration();
    // A saved preset is user intent, not a mirror of current runtime values.
    // Browsing, editor reloads and mode changes never adopt live settings into
    // it. Only a new document is seeded from capture; edits require explicit save.
    // Full capture still serves live capability reads and startup comparison.
    if (options.overviewOnly || options.editorOnly) {
        const settings = await readOnce(loadSettings, '开机模式设置');
        assertSettingsGeneration(generation); assertBootSettingsReady();
        const bank = storedOverviewBank(settings);
        const baseline = bank ?? savedOverviewSnapshot(settings, await initialPerformanceSchedule(settings));
        assertSettingsGeneration(generation);
        const document = readStoredBootMode(settings, baseline);
        return { document, initialSource: initialDocumentSources.get(document), requiresCapture: !bank,
            startupResult: normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, object(settings.input).startupObservedBoot),
            baseline: previewBootMode(document, 'global', baseline), support: {}, apps: Array.isArray(settings.quickApps.apps) ? cloneBootValue(settings.quickApps.apps) : [] };
    }
    return serialized(async () => {
        const shared = sharedBootModeReads();
        const [baseline, support] = await Promise.all([captureBootModeSnapshot(shared), readBootModeSupport(undefined, shared)]);
        const settings = await readOnce(loadSettings, '开机模式设置');
        assertSettingsGeneration(generation); assertBootSettingsReady();
        const initialSchedule = await initialPerformanceSchedule(settings);
        assertSettingsGeneration(generation);
        const document = readStoredBootMode(settings, baseline);
        if (initialSchedule) document.global.performance = savedOverviewSnapshot(settings, initialSchedule).performance;
        return { document, initialSource: initialDocumentSources.get(document), startupResult: normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, object(settings.input).startupObservedBoot), baseline: previewBootMode(document, 'global', baseline), support, apps: Array.isArray(settings.quickApps.apps) ? cloneBootValue(settings.quickApps.apps) : [] };
    });
}
export function saveBootModes(document: BootModeDocument, expectedRevision = document.revision, source?: BootModeInitialSource, editorSource?: BootModeDocument): Promise<BootModeDocument> {
    const initialSource = source ?? initialDocumentSources.get(document);
    const editorBaseline = editorSource && cloneBootValue(editorSource);
    const generation = getSettingsGeneration();
    return serialized(async () => {
        assertSettingsGeneration(generation);
        await readOnce(loadSettings, '开机模式设置'); assertSettingsGeneration(generation); assertBootSettingsReady();
        const boot = await optional(() => invoke<string>('bootMode.startup.identity', {}, { timeoutMs: 3000 }), 'Windows 启动标识');
        assertSettingsGeneration(generation);
        let saved!: BootModeDocument;
        await updateSettings(settings => {
            assertSettingsGeneration(generation);
            assertInitialModeSource(settings, initialSource);
            const current = readStoredBootMode(settings, document.activeSnapshot ?? document.global);
            // Recovery or replacement can reuse a revision from before the editor opened.
            if (current.revision !== expectedRevision || editorBaseline &&
                (editorBaseline.revision !== expectedRevision || !sameBootModeEditableContent(current, editorBaseline)))
                throw Error('开机模式已被其他窗口修改，请重新读取');
            saved = normalizeBootModeDocument(document, document.global);
            saved.revision = nextBootRevision(current.revision);
            saved.activeSnapshot = cloneBootValue(current.activeSnapshot);
            saved.current = current.current;
            saved.boot = current.boot;
            saved.bootEnabled = current.bootEnabled;
            saved.bootNotice = current.bootNotice;
            if (current.preparation) saved.preparation = cloneBootValue(current.preparation);
            else delete saved.preparation;
            // An explicit draft save also defers a not-yet-started automatic
            // restore. It must never adopt this newly edited template now.
            settings.extensions = { ...settings.extensions, [MODULE_KEY]: saved,
                ...(typeof boot === 'string' && boot ? { bootModesAppliedBoot: boot, bootModesDeferredBoot: boot } : {}) };
            syncStartupInput(settings, saved);
        });
        return cloneBootValue(saved);
    });
}
function syncStartupInput(settings: UnifiedSettings, document: BootModeDocument) {
    const snapshot = resolveBootMode(document, startupBootMode(document));
    const preferences = normalizeInputStartupPreferences({ virtualGamepadPersona: snapshot.input.persona, gyroPreset: object(snapshot.gyro.motion).enabled ? (object(snapshot.gyro.motion).activePreset ?? object(snapshot.gyro.motion).preset ?? 'custom') : 'off' });
    settings.startupDesired = { ...settings.startupDesired, ...preferences };
}
/** Boot preparation repairs stored settings; no live Xbox/Steam connection is a prerequisite. */
export async function bootModeConnections(_id: BootModeId): Promise<Array<{ label: string; connected: boolean; detail?: string }>> {
    return [];
}
/** Setting next boot never applies runtime settings, changes mouse ownership or launches applications. */
export function chooseBootMode(id: BootModeId, document: BootModeDocument, enabled = true, source?: BootModeInitialSource): Promise<BootModeDocument> {
    const initialSource = source ?? initialDocumentSources.get(document);
    const generation = getSettingsGeneration();
    return serialized(async () => {
        if (!isBootModeId(id) || typeof enabled !== 'boolean') throw new Error('开机模式选择无效');
        assertSettingsGeneration(generation);
        await readOnce(loadSettings, '开机模式设置'); assertSettingsGeneration(generation); assertBootSettingsReady();
        const requested = normalizeBootModeDocument(document, document.global);
        const boot = await optional(() => invoke<string>('bootMode.startup.identity', {}, { timeoutMs: 3000 }), 'Windows 启动标识');
        assertSettingsGeneration(generation);
        let copy!: BootModeDocument;
        await updateSettings(settings => {
            assertSettingsGeneration(generation);
            assertInitialModeSource(settings, initialSource);
            const hasStoredDocument = object(settings.extensions?.[MODULE_KEY]).schema === 1;
            const baseline = hasStoredDocument ? storedOverviewBank(settings) ?? savedOverviewSnapshot(settings) : requested.global;
            const latest = readStoredBootMode(settings, baseline);
            if (latest.revision !== document.revision) {
                const startup = normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, boot);
                const sameTemplates = (['global', 'scenes', 'presets', 'unresolvedGlobal'] as const).every(key =>
                    equalSettingsDocuments(latest[key], requested[key]));
                if (!startup || startup.status === 'failed' || startup.sourceRevision !== document.revision ||
                    startup.revision !== latest.revision || latest.current !== startup.mode ||
                    latest.boot !== document.boot || latest.bootEnabled !== document.bootEnabled || !sameTemplates)
                    throw Error('开机模式配置已变化');
            }
            // Only the first selection may materialize a template from the captured bank.
            // Existing templates and same-revision runtime acknowledgements belong to the stored document.
            copy = hasStoredDocument ? latest : requested;
            copy.current = latest.current;
            copy.activeSnapshot = cloneBootValue(latest.activeSnapshot);
            copy.boot = id;
            copy.bootEnabled = enabled;
            copy.revision = nextBootRevision(latest.revision);
            copy.preparation = { schema: 1, mode: id, enabled, revision: copy.revision,
                observedBoot: typeof boot === 'string' && boot ? boot : null, status: 'preparing',
                steps: Object.fromEntries(BOOT_PREPARATION_STEPS.map(step => [step,
                    { state: id === 'global' && step !== 'steam' ? 'skipped' : 'waiting' }])) as BootModePreparation['steps'] };
            copy.bootNotice = '开机准备处理中；尚未确认全部启动配置';
            // Selection only schedules the next Windows boot, including when the
            // page is used before startup restoration has completed.
            settings.extensions = { ...settings.extensions, [MODULE_KEY]: copy,
                ...(typeof boot === 'string' && boot ? { bootModesAppliedBoot: boot, bootModesDeferredBoot: boot } : {}) };
            syncStartupInput(settings, copy);
        });
        const preparation = cloneBootValue(copy.preparation!);
        // Ordinary edits keep this receipt; restoring a file can replace it at
        // the same revision. Never acquire a fresh native context for its replacement.
        let publishedPreparation = normalizeBootPreparation(copy.preparation, copy.revision);
        const ownsPreparation = (latest: BootModeDocument) => getSettingsGeneration() === generation &&
            latest.boot === id && latest.bootEnabled === enabled && !!publishedPreparation &&
            equalSettingsDocuments(latest.preparation, publishedPreparation);
        const currentPreparation = async () => {
            assertSettingsGeneration(generation);
            const settings = await readOnce(loadSettings, '开机准备所属配置');
            const latest = readStoredBootMode(settings, copy.global);
            if (!ownsPreparation(latest)) throw Error('开机准备所属配置已变化，旧操作已停止');
            return settings;
        };
        const writePreparedSteamStartup = async (on: boolean) => {
            assertSettingsGeneration(generation);
            const settings = await readOnce(loadSettings, 'Steam 开机准备所属配置');
            if (!ownsPreparation(readStoredBootMode(settings, copy.global)))
                throw Error('开机准备所属配置已变化，旧 Steam 写入已停止');
            await steamMasterSet(on, true, { origin: 'boot-mode', bootContext: mouseBootContext(settings) });
        };
        const pending: string[] = [];
        const xbox = enabled && (id === 'xbox' || id === 'xbox-steam');
        const steam = enabled && (id === 'steam' || id === 'xbox-steam');
        const verifySteam = async (): Promise<BootConfig> => {
            const observed = await readOnce(readBootSteamStartup, 'Steam 开机准备');
            return assertSteamStartup(observed, steam);
        };
        const retireLegacyStartup = async (step: 'legacyMouse' | 'audioStartup' | 'frameStartup') => {
            const result = await invoke<BootConfig | null>('bootMode.legacyStartup.retire', {
                step, bootContext: mouseBootContext(await currentPreparation()),
            });
            if (result?.removed !== true && result?.removed !== false) throw Error('旧开机入口清理未确认');
            return result.removed;
        };
        const writes: Array<[
            BootPreparationStep,
            string,
            () => Promise<unknown>,
            (() => Promise<BootConfig>)?
        ]> = id === 'global' ? [
            // Mode 1 still leaves Windows/Xbox and ordinary global preferences
            // alone, but its logon route must not inherit advanced Steam startup.
            ['steam', 'Steam 高级启动', () => writePreparedSteamStartup(false), verifySteam],
        ] : [
            ['legacyMouse', '旧模拟鼠标开机入口', async () => ({
                legacyJoyxoff: await retireLegacyStartup('legacyMouse') ? 'removed' : 'not-present-or-unrelated-preserved',
            })],
            // All fixed non-global modes repair handheld identity. Only 2/4
            // select Xbox AND enable its sign-in flag immediately; 3 clears both now.
            // These are pre-boot OS settings, never deferred to runtime restoration.
            ['windows', 'Windows 开机配置', () => writeGamingHomeStartup(xbox, enabled, xbox ? 'xbox' : 'none',
                async () => mouseBootContext(await currentPreparation())), async () => {
                const observed = await readOnce(readGamingHomeConfiguration, 'Windows 开机准备');
                if (observed.startupValue !== (xbox ? 1 : 0) ||
                    observed.homeApp !== (xbox ? 'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App' : '') ||
                    enabled && observed.deviceForm !== 46) throw Error('Windows 主应用、登录标志或设备形态回读未确认');
                return { deviceForm: observed.deviceForm, startupValue: observed.startupValue, homeApp: observed.homeApp };
            }],
            ['steam', 'Steam 高级启动', () => writePreparedSteamStartup(steam), verifySteam],
            ['audioStartup', '音质优化开机联动', async () => { await retireLegacyStartup('audioStartup'); }, async () => {
                if (await readOnce(() => fxExists({ strict: true }), '音质优化开机入口')) throw Error('音质优化开机入口仍存在');
                return { enabled: false, verification: 'startup-shortcut-readback' };
            }],
            ['frameStartup', '小黄鸭开机联动', async () => { await retireLegacyStartup('frameStartup'); }, async () => {
                if (await readOnce(() => steamAddonExists('steamscale'), '小黄鸭开机入口')) throw Error('小黄鸭开机入口仍存在');
                return { enabled: false, verification: 'startup-file-readback' };
            }],
        ];
        for (let index = 0; index < writes.length; index++) {
            const [step, label, write, observe] = writes[index];
            await currentPreparation();
            let returned = false;
            try {
                const result = await write();
                if (result === false) throw Error('原模块未确认写入');
                returned = true;
                await currentPreparation();
                const observed = observe ? await observe() : object(result);
                if (!Object.keys(observed).length) throw Error('原模块尚未提供准备回执');
                preparation.steps[step] = { state: 'confirmed', observed };
            }
            catch (error) {
                const detail = text(error);
                preparation.steps[step] = { state: returned ? 'waiting' : 'failed', detail };
                pending.push(label + '：' + detail);
            }
            // A dispatched old owner may finish. Its completion is not permission
            // to run later owners or replace a newer selection's durable evidence.
            await currentPreparation();
            if (index === writes.length - 1) preparation.status = pending.length ? 'partial' : 'prepared';
            await updateSettings(settings => {
                const latest = readStoredBootMode(settings, copy.global);
                if (!ownsPreparation(latest)) throw Error('开机准备所属配置已变化，旧回执未覆盖新选择');
                copy = cloneBootValue(latest);
                copy.preparation = cloneBootValue(preparation);
                if (pending.length) copy.bootNotice = pending.join('；');
                else if (preparation.status === 'preparing') copy.bootNotice = '开机准备处理中；尚未确认全部启动配置';
                else delete copy.bootNotice;
                settings.extensions = { ...settings.extensions, [MODULE_KEY]: copy };
            });
            publishedPreparation = normalizeBootPreparation(copy.preparation, copy.revision);
        }
        return copy;
    });
}
class BootMouseUnavailable extends Error {
}
function assertSteamStartup(observed: Awaited<ReturnType<typeof readBootSteamStartup>>, enabled: boolean): BootConfig {
    if (observed.enabled !== enabled || observed.conflict || !observed.file.known ||
        (enabled ? !observed.file.exists || !observed.file.canonical : observed.file.exists) || enabled &&
        !(observed.service.known && observed.service.installed && observed.service.automatic && observed.service.running))
        throw Error('Steam 开机命令或服务回读未确认');
    return observed;
}
function assertMouseState(state: MouseModeState, enabled: boolean) {
    if (state.controlAllowed === false)
        throw new BootMouseUnavailable(state.error || state.reason || '鼠标提供程序尚未连接');
    if (!state.ok || state.stateKnown === false || state.on !== enabled || state.pending)
        throw Error(state.error || state.reason || '模拟鼠标状态未确认');
}
type MouseOwnerCheck = ((settings?: UnifiedSettings) => boolean | Promise<boolean>) & { bootContext?: () => MouseBootContext; dispatchCurrent?: () => boolean };
async function requireBootActionOwner(owner?:MouseOwnerCheck):Promise<void> {
    if(owner&&(!await owner()||owner.dispatchCurrent?.()===false))throw Error('开机模块所属配置已变化，旧操作已停止');
}
async function ownedBootAction<T>(owner:MouseOwnerCheck|undefined,action:()=>Promise<T>):Promise<T> {
    await requireBootActionOwner(owner);
    const result=await action();
    await requireBootActionOwner(owner);
    return result;
}
function bootPreferenceWrite(owner?:MouseOwnerCheck):BootPreferenceWrite {
    const bootContext=owner?.bootContext?.();
    return {origin:'boot-mode',...(bootContext?{bootContext}:{})};
}
function mouseBootContext(settings: UnifiedSettings): MouseBootContext {
    return { input: cloneBootValue(settings.input), extensions: cloneBootValue(projectBootPreferenceExtensions(settings.extensions)) };
}
async function stopMouse(isCurrent: MouseOwnerCheck = () => true) {
    const state = await readOnce(getMouseModeState, '模拟鼠标状态');
    if (state.controlAllowed === false)
        throw new BootMouseUnavailable(state.reason || '模拟鼠标提供程序尚未连接');
    if (state.stateKnown === false)
        throw Error('原鼠标模拟状态未知，请重新检测');
    if (!await isCurrent())
        throw Error('模拟鼠标所属模式已变化');
    if (state.on || state.mode !== 'off')
        assertMouseState(await setMouseModeEnabled(false, isCurrent.bootContext?.()), false);
}
async function applyMouse(value: any, isCurrent: MouseOwnerCheck = () => true) {
    const target = value.mouse;
    if (target === null || target === undefined)
        throw Error('目标模拟鼠标配置尚未确认');
    if (!['off', 'microsoft', 'joyxoff', 'steamdeck'].includes(target))
        throw Error('未知模拟鼠标配置');
    if (target === 'steamdeck' && value.persona !== 'steamdeck')
        throw Error('SteamDeck 鼠标需要 SteamDeck 虚拟手柄');
    // Steam owns its desktop layout. YMCC cannot truthfully verify or switch that mapping.
    if (value.persona === 'steamdeck')
        throw new BootMouseUnavailable('由 Steam 管理，桌面鼠标布局状态未确认');
    await stopMouse(isCurrent);
    if (!await isCurrent())
        throw Error('模拟鼠标所属模式已变化');
    if (target === 'off')
        return;
    const backendName = target === 'microsoft' ? 'gamebar' : 'joyxoff';
    const backend = await setMouseBackend(backendName, isCurrent.bootContext?.());
    if (backend.controlAllowed === false)
        throw new BootMouseUnavailable(backend.error || backend.reason || '鼠标提供程序尚未连接');
    if (!backend.ok || backend.backend !== backendName)
        throw Error(backend.error || '模拟鼠标软件切换未确认');
    if (!await isCurrent())
        throw Error('模拟鼠标所属模式已变化');
    assertMouseState(await setMouseModeEnabled(true, isCurrent.bootContext?.()), true);
}
async function restoreMouse(value: any, isCurrent: MouseOwnerCheck = () => true) {
    const settings = await readOnce(loadSettings, '开机模式设置');
    if (!await isCurrent(settings) || settings.input.outputTarget.persona !== value.persona)
        throw Error('模拟鼠标所属输入配置已变化，未恢复旧状态');
    if (value.persona === 'steamdeck')
        return;
    await stopMouse(isCurrent);
    if (!await isCurrent()) throw Error('模拟鼠标所属模式已变化，未恢复旧状态');
    if (['gamebar', 'joyxoff'].includes(value.mouseBackend)) {
        const backend = await setMouseBackend(value.mouseBackend, isCurrent.bootContext?.());
        if (backend.controlAllowed === false)
            throw new BootMouseUnavailable(backend.error || backend.reason || '原鼠标提供程序尚未连接');
        if (!backend.ok || backend.backend !== value.mouseBackend)
            throw Error('原模拟鼠标软件恢复未确认');
    }
    if (!await isCurrent()) throw Error('模拟鼠标所属模式已变化，未恢复旧状态');
    if (value.mousePolicy === 'auto') {
        const result = await setMouseModePolicy('auto', isCurrent.bootContext?.());
        if (result.controlAllowed === false)
            throw new BootMouseUnavailable(result.error || result.reason || '原鼠标提供程序尚未连接');
        if (!result.ok || result.mode !== 'auto' || result.stateKnown === false)
            throw Error('原自动鼠标策略恢复未确认');
    }
    else
        await applyMouse(value, isCurrent);
}
const MOUSE_WAITING = '模拟鼠标：预设已保存，等待提供程序连接';
interface DeferredBootMouse {
    generation: number;
    mode: BootModeId;
    input: BootModeSnapshot['input'];
    receipt: BootModeReceipt;
    stop: Array<() => void>;
    queued: boolean;
}
let deferredMouse: DeferredBootMouse | null = null;
let deferredMouseEpoch = 0;
export function cancelBootModeDeferredMouse(): void {
    deferredMouseEpoch++;
    const previous = deferredMouse;
    deferredMouse = null;
    previous?.stop.forEach(stop => stop());
}
function deferBootMouse(input: BootModeSnapshot['input'], receipt: BootModeReceipt, generation: number, baseline: BootModeSnapshot): void {
    cancelBootModeDeferredMouse();
    const request: DeferredBootMouse = { generation, mode: receipt.mode, input: cloneBootValue(input), receipt, stop: [], queued: false };
    deferredMouse = request;
    const current = () => deferredMouse === request && getSettingsGeneration() === generation;
    const wake = () => {
        if (request.queued || !current()) {
            if (deferredMouse === request && !current())
                cancelBootModeDeferredMouse();
            return;
        }
        request.queued = true;
        void serialized(async () => {
            try {
                if (!current())
                    return;
                const settings = await readOnce(loadSettings, '开机模式设置');
                const document = readStoredBootMode(settings, baseline);
                // Edited boot drafts describe a future launch, not this in-flight startup handoff.
                if (!current() || document.current !== request.mode || settings.input.outputTarget.persona !== input.persona || document.activeSnapshot.input.persona !== input.persona) {
                    if (deferredMouse === request)
                        cancelBootModeDeferredMouse();
                    return;
                }
                const state = await readOnce(getMouseModeState, '模拟鼠标状态');
                if (!current() || state.controlAllowed === false || state.stateKnown === false || state.pending)
                    return;
                const identity = { revision: settings.input.revision, hash: computeInputConfigHash(settings.input) };
                const expected = mouseBootContext(settings);
                let lost = false, verified = settings;
                const owns = (value?: UnifiedSettings) => {
                    if (value && (value.input.revision !== identity.revision || computeInputConfigHash(value.input) !== identity.hash ||
                        !equalSettingsDocuments(mouseBootContext(value).extensions, expected.extensions))) lost = true;
                    return current() && !lost;
                };
                const guarded: MouseOwnerCheck = async value => {
                    if (!owns()) return false;
                    const latest = value ?? await readOnce(loadSettings, '待接续鼠标所属配置');
                    assertBootSettingsReady();
                    if (!owns(latest)) return false;
                    verified = latest;
                    return true;
                };
                guarded.bootContext = () => mouseBootContext(verified);
                await applyMouse(input, guarded);
                if (!current())
                    return;
                const committed = await updateSettingsForInputReceipt(identity, value => {
                    if (!owns(value)) return false;
                    const latest = readStoredBootMode(value, baseline);
                    if (latest.current !== request.mode || value.input.outputTarget.persona !== input.persona || latest.activeSnapshot.input.persona !== input.persona)
                        return false;
                    latest.activeSnapshot.input = cloneBootValue(input);
                    value.extensions = { ...value.extensions, [MODULE_KEY]: latest };
                    return true;
                }, owns, { origin: 'boot-mode' });
                if (!committed) {
                    if (deferredMouse === request) cancelBootModeDeferredMouse();
                    return;
                }
                if (!current())
                    return;
                receipt.pending = receipt.pending.filter(row => row !== MOUSE_WAITING && !row.startsWith('原模拟鼠标：'));
                if (!receipt.applied.includes('模拟鼠标'))
                    receipt.applied.push('模拟鼠标');
                cancelBootModeDeferredMouse();
                await publishReceipt(receipt);
            }
            catch (error) {
                if (!current() || error instanceof BootMouseUnavailable)
                    return;
                receipt.pending = receipt.pending.filter(row => row !== MOUSE_WAITING);
                receipt.pending.push('模拟鼠标：' + text(error));
                cancelBootModeDeferredMouse();
                await publishReceipt(receipt);
            }
            finally {
                request.queued = false;
            }
        });
    };
    request.stop.push(on('mouseMode.changed', wake), on('gamepad.mouse-toggle', wake));
    // Close the gap between the failed apply and subscription without a timer or poller.
    wake();
}
function inputPatch(snapshot: BootModeSnapshot, settings: UnifiedSettings, activate = true): Record<string, any> {
    const input = object(snapshot.input), rawMotion = object(object(snapshot.gyro).motion), motion = { ...rawMotion, ...(activate && rawMotion.enabled === true ? { outputMode: 'virtual-stick', unit: 'deg/s' } : {}) };
    return { outputTarget: { ...object(input.outputTarget), ...(activate ? { persona: input.persona, buttonMappingEnabled: input.persona !== 'disabled', gyroEnabled: rawMotion.enabled === true } : {}) }, ...(input.ownership === undefined ? {} : { ownership: bootInputOwnership(input.ownership) }), gyroMotion: motion,
        bootModeShortcuts: activate ? null : settings.input.bootModeShortcuts ?? null };
}
async function writeBaseInput(snapshot: BootModeSnapshot, expected: number, isCurrent: (settings?: UnifiedSettings) => boolean): Promise<InputSettingsSnapshot> {
    const settings = await readOnce(loadSettings, '开机模式设置');
    const result = await compareAndSwapInputSettings(expected, inputPatch(snapshot, settings, true), isCurrent, { origin: 'boot-mode' });
    if (result.ok === false)
        throw Error('全局输入配置冲突或写入未确认：' + result.reason);
    return result.value;
}
async function applyRuntimeModule(id: BootModeModule, value: any, before: any, receipt: BootModeReceipt, actionOwner?: MouseOwnerCheck, preservePerformanceState = false): Promise<void> {
    await requireBootActionOwner(actionOwner);
    const pendingBefore = receipt.pending.length, skippedBefore = receipt.skipped.length;
    recordModule(receipt, id, 'requested');
    if (id === 'performance') {
        const config = { ...value.schedule, powerSchemes: normalizeControlPowerSchemes(value.powerSchemes ?? value.schedule?.powerSchemes), controls: normalizeContinuousControls(value.controls ?? (value.schedule ? continuousControlsFromSchedule(snapshotPerformanceSchedule(value.schedule)) : undefined)),
            configured: preservePerformanceState ? value.schedule?.configured === true : true,
            enabled: preservePerformanceState ? value.schedule?.configured === true && value.schedule?.enabled !== false : true };
        recordModule(receipt, id, 'requested', undefined, normalizeBootConfig(value));
        const owner = startupOwners.get(receipt);
        const startup = owner?.committed ? { boot: owner.boot, generation: owner.generation, mode: owner.mode,
            sourceRevision: owner.sourceRevision, revision: owner.revision, writeId: owner.writeId ?? null } : undefined;
        const result = await ownedBootAction(actionOwner,()=>applyPerformanceConfiguration(config, frameRatePair(value.frames), { withReceipt: true, ...bootPreferenceWrite(actionOwner), ...(startup ? { startup } : {}) }));
        if (result.scope === 'dedicated') {
            recordModule(receipt, id, 'saved', '全局性能配置已保存；当前游戏专属配置优先', receipt.modules?.[id]?.requested);
            return;
        }
        if (!config.configured || !config.enabled) {
            recordModule(receipt, id, 'disabled', '沿用全局性能调度的未接管或关闭状态', receipt.modules?.[id]?.requested);
            return;
        }
        if (!result.applied || result.ownership === 'none' || result.scope === 'none') {
            const applied = result.application;
            const detail = applied ? `性能调度：${applied.cpu === 'applied' ? 'CPU 已应用' : applied.cpu === 'windows-managed' ? 'CPU 由 Windows 管理' : 'CPU 未确认'}；${applied.tdp.ok ? 'TDP 已确认' : 'TDP 未确认'}，等待完整应用回执` :
                '性能调度：配置已保存，等待原性能 owner 确认应用';
            receipt.pending.push(detail);
            recordModule(receipt, id, 'waiting', detail, receipt.modules?.[id]?.requested);
            return;
        }
    }
    else if (id === 'touch') {
        const patches: Partial<Record<ScreenTouchpadPersona, Partial<ScreenTouchpadConfig>>> = {};
        const expected: Partial<Record<ScreenTouchpadPersona, Partial<ScreenTouchpadConfig>>> = {};
        for (const persona of PERSONAS) {
            const config = value.profiles?.[persona];
            if (config && !same(config, before.profiles?.[persona])) {
                const desired=touchConfig(config),previous=touchConfig(before.profiles?.[persona]||{});
                const patch=Object.fromEntries(Object.entries(desired).filter(([key,value])=>!same(value,previous[key])));
                if(!Object.keys(patch).length)continue;
                patches[persona] = patch;
                expected[persona] = previous;
            }
        }
        if (Object.keys(patches).length) {
            const settings = await readOnce(loadSettings, '开机模式设置');
            const mode = object(settings.extensions?.[MODULE_KEY]);
            const owner = startupOwners.get(receipt);
            if (mode.current !== receipt.mode || owner && mode.revision !== owner.revision)
                throw Error('开机模式已变化，未提交触摸预设');
            const boot = object(settings.input).startupObservedBoot;
            if (typeof boot !== 'string' || !boot) throw Error('Windows 启动标识尚未确认，未提交触摸预设');
            const result = await ownedBootAction(actionOwner,()=>screenTouchpadsSaveProfiles(patches, expected,
                { boot, mode: receipt.mode, revision: mode.revision, ...(actionOwner?.bootContext?.() ?? mouseBootContext(settings)) }));
            if (result.settingsSaved !== true) throw Error((result.persona ? result.persona + '：' : '') + (result.reason || '触摸预设未保存'));
            if (result.ok === true && result.activeApplication === 'applied' && result.applicationConfirmed === true) {
                recordModule(receipt, id, 'active', '原生已确认当前触摸档位的配置，不代表实体输入已验证');
                receipt.applied.push(LABELS[id]);
                return;
            }
            if (!result.ok || result.activeApplication === 'pending' || result.activeApplication === 'applied' || result.applicationConfirmed === true)
                receipt.pending.push('屏幕触摸：整组已保存，当前档位生效待确认（' + result.reason + '）');
        }
    }
    else if (id === 'audioOptimization' || id === 'frameGeneration') {
        // These are scene launch selections. No unconditional OS startup link and no game-specific LS profile is rewritten.
        if (value.enabled === false) {
            recordModule(receipt, id, 'disabled', '本预设不启动此软件，不代表已关闭现有进程');
            return;
        }
        if (value.enabled !== true) {
            receipt.skipped.push(LABELS[id] + '：预设选择尚未确认');
        }
        if (value.enabled === true) {
            if (actionOwner && (!await actionOwner() || actionOwner.dispatchCurrent?.() === false)) throw Error('开机软件所属配置已变化，旧请求已停止');
            if(id==='audioOptimization'){
                const result=await fxSound.setEnabled(true, undefined, actionOwner?.bootContext?.());
                if (actionOwner && !await actionOwner()) throw Error('开机软件所属配置已变化，旧回执未采用');
                if(!result.ok || !result.state?.known || !result.state.running || !result.state.enabled || !result.state.selected)
                    receipt.pending.push('音质优化：'+(result.reason || '实际音效输出未确认'));
            }else {
                await launchOne(STEAM_ADDONS.find(app => app.key === 'steamscale')!.exe, receipt, actionOwner);
                if (actionOwner && !await actionOwner()) throw Error('开机软件所属配置已变化，旧回执未采用');
                const pending = receipt.pending.slice(pendingBefore), skipped = receipt.skipped.slice(skippedBefore);
                recordModule(receipt, id, pending.length ? 'waiting' : skipped.length ? 'skipped' : 'requested',
                    [...pending, ...skipped].join('；') || '仅请求启动程序；本回执不证明游戏插帧已生效');
                return;
            }
        }
    }
    else if (id === 'steamAppearance') {
        if (!Object.keys(value.states || {}).length) {
            recordModule(receipt, id, 'unchanged', '本预设未指定 Steam 外观修改');
            return;
        }
        const result = await ownedBootAction(actionOwner, () => steamAppearance.applyScene(value.states, bootPreferenceWrite(actionOwner), async read => {
            // A timed-out observation may still be running. Its result belongs
            // to that scene request and must not admit a different write.
            const current = await readOnce(read, 'Steam 美化预设', false);
            await requireBootActionOwner(actionOwner);
            return current;
        }));
        if (!result.ok)
            receipt.pending.push('Steam 美化：' + (result.message || '等待连接'));
    }
    else if (id === 'steamWallpaper')
        await ownedBootAction(actionOwner,()=>setUiSettings({ steamDynamicWallpaperEnabled: value.enabled, steamWallpaperBackgroundDownload: value.download, steamWallpaperDownloadCpuThreshold: Number(value.cpuThreshold) }, bootPreferenceWrite(actionOwner)));
    else if (id === 'background') {
        const patch = Object.fromEntries(BACKGROUND_KEYS.filter(key => value.ui?.[key] !== undefined).map(key => [key, value.ui[key]]));
        await ownedBootAction(actionOwner,()=>setUiSettings(patch, bootPreferenceWrite(actionOwner)));
        if (patch.theme)
            applyTheme(patch.theme as UiSettings['theme']);
    }
    else if (id === 'monitor') {
        const prefs = normalizeMonitorOutput(value.preferences);
        recordModule(receipt, id, 'waiting', '监控输出等待原 owner 回执', prefs as unknown as BootModuleReceipt['requested']);
        await ownedBootAction(actionOwner,()=>saveMonitorOutputPreferences(prefs, bootPreferenceWrite(actionOwner)));
        // The coordinator verifies ownership around its actual output and ACK.
        // Preserve that completed proof; the next module checks ownership again.
        await requireBootActionOwner(actionOwner);
        const result = await monitorOutputCoordinator.apply(prefs,bootPreferenceWrite(actionOwner));
        if (!result.ok || result.applied !== (prefs.enabled ? prefs.output : 'off'))
            receipt.pending.push('监控：' + result.reason);
    }
    else if (id === 'display') {
        if (typeof value.brightness === 'number' && value.brightness !== before.brightness) {
            const brightness = await ownedBootAction(actionOwner,()=>applyBootBrightness(value.brightness,bootPreferenceWrite(actionOwner)));
            if (brightness?.applied !== true) receipt.pending.push('屏幕亮度：' + (brightness?.reason || '原电源计划生效尚未确认'));
        }
        if (value.resolution !== before.resolution || value.hz !== before.hz) {
            const available = await readOnce(() => display.getModes(), '可用显示模式'), active = available.modes.find(item => item.id === available.current);
            const mode = available.modes.find(item => (value.resolution === 'native' ? active && item.width === active.width && item.height === active.height : item.width + 'x' + item.height === value.resolution) && (value.hz === 'native' ? active && item.refresh === active.refresh : item.refresh === Number(value.hz)));
            if (!mode)
                receipt.skipped.push('显示模式：本机不支持该分辨率或刷新率');
            else {
                const result = await ownedBootAction(actionOwner,()=>display.setMode(mode, bootPreferenceWrite(actionOwner)));
                if (result.ok !== true || result.id !== mode.id || result.width !== mode.width || result.height !== mode.height || result.refresh !== mode.refresh || result.orientation !== mode.orientation)
                    receipt.pending.push('显示模式：实际回读与预设尚未确认一致');
            }
        }
    }
    else if (id === 'charge') {
        if (typeof value.enabled !== 'boolean') {
            receipt.skipped.push('充电限制：预设能力尚未确认');
            recordModule(receipt, id, 'skipped', '预设能力尚未确认');
            return;
        }
        const state = await readOnce(() => batteryCharge.get(), '充电限制状态');
        if (state.ok !== true) {
            const detail = '充电限制：设备状态读取尚未确认';
            receipt.pending.push(detail); recordModule(receipt, id, 'waiting', detail); return;
        }
        const confirmed = (result: Awaited<ReturnType<typeof batteryCharge.set>>, limit: number) => {
            if (result.requestedLimit !== limit) return false;
            if (result.verification === 'readback') return result.managed === true && result.currentLimit === limit;
            if (result.verification === 'acknowledged') return result.managed === true;
            if (result.verification === 'software-active') return result.managed === true && limit < 100;
            return result.verification === 'unmanaged' && result.managed === false && limit === 100;
        };
        if (value.enabled) {
            if (!state.supported || !state.choices.includes(Number(value.limit))) {
                receipt.skipped.push('充电限制：本机或档位不支持');
                recordModule(receipt, id, 'skipped', '本机或档位不支持');
                return;
            }
            const result = await ownedBootAction(actionOwner,()=>batteryCharge.set(state.fence, Number(value.limit), bootPreferenceWrite(actionOwner)));
            if (!result.ok)
                throw Error(result.reason || '充电上限未确认');
            if (!confirmed(result, Number(value.limit))) receipt.pending.push('充电限制：设备回执与所选上限未确认一致');
        }
        else if (state.managed) {
            // Release is only the software-bypass owner operation. Firmware
            // limits use their existing fenced 100% route to remove the cap.
            const software = state.kind === 'software-bypass-threshold';
            if (!software && state.currentLimit === 100 && state.requestedLimit === 100) {
                recordModule(receipt, id, 'disabled', '设备已为100%，本预设不限充');
                return;
            }
            if (!software && (!state.supported || !state.choices.includes(100))) {
                receipt.skipped.push('充电限制：本机未确认解除上限的档位');
                recordModule(receipt, id, 'skipped', '本机未确认解除上限的档位');
                return;
            }
            const result = await ownedBootAction(actionOwner,()=>software ? batteryCharge.release(bootPreferenceWrite(actionOwner))
                : batteryCharge.set(state.fence, 100, bootPreferenceWrite(actionOwner)));
            if (!result.ok)
                throw Error(result.reason || '充电限制释放未确认');
            if (!confirmed(result, 100)) receipt.pending.push('充电限制：解除上限的设备回执尚未确认');
            else { recordModule(receipt, id, 'disabled', '原设备控制入口已确认解除充电上限'); return; }
        }
        else { recordModule(receipt, id, 'disabled', '设备当前没有YMCC限充控制，不改写外部设置'); return; }
    }
    else if (id === 'steamDecky') {
        if (typeof value.enabled !== 'boolean') {
            receipt.skipped.push('Decky：连接状态尚未确认');
            recordModule(receipt, id, 'skipped', '连接状态尚未确认');
            return;
        }
        const state = await ownedBootAction(actionOwner,()=>deckySidebar.setEnabled(value.enabled, bootPreferenceWrite(actionOwner)));
        if (state.enabled !== value.enabled)
            throw Error('Decky 开关未确认');
        if (value.enabled && !(state.phase === 'loader-started' && state.mirrorConnected && state.powerReady !== false))
            receipt.pending.push('Decky：已启用，等待 Steam 连接');
    }
    const pending = receipt.pending.slice(pendingBefore), skipped = receipt.skipped.slice(skippedBefore);
    if (pending.length || skipped.length) {
        recordModule(receipt, id, pending.length ? 'waiting' : 'skipped', [...pending, ...skipped].join('；'), receipt.modules?.[id]?.requested);
    } else if (['background', 'steamWallpaper', 'touch'].includes(id)) {
        recordModule(receipt, id, 'saved', '配置已由原保存入口接收；不以保存回执证明运行效果');
    } else if (id === 'steamAppearance') {
        recordModule(receipt, id, 'requested', '外观应用已提交；视觉效果由原 Steam owner 确认');
    } else {
        const disabled = id === 'steamDecky' && value.enabled === false
            || id === 'monitor' && normalizeMonitorOutput(value.preferences).enabled === false;
        recordModule(receipt, id, disabled ? 'disabled' : 'active',
            disabled ? '原模块关闭操作已返回' : '原模块操作返回成功', receipt.modules?.[id]?.requested);
        if (disabled) receipt.applied = receipt.applied.filter(label => label !== LABELS[id]);
        else receipt.applied.push(LABELS[id]);
    }
}
async function launchOne(path: string, receipt: BootModeReceipt, isCurrent: MouseOwnerCheck = () => true, beforeExecute?: () => Promise<boolean>) {
    if (!await isCurrent())
        return;
    if (!await readOnce(() => fs.exists(path), '应用文件 ' + path)) {
        receipt.skipped.push('未安装：' + path.replace(/^.*[\\/]/, ''));
        return;
    }
    if (!await isCurrent()) return;
    if ((await readOnce(() => proc.findExact(path), '应用进程 ' + path)).found)
        return;
    if (beforeExecute && !await beforeExecute()) {
        receipt.skipped.push('应用已从全局快捷库删除');
        return;
    }
    if (!await isCurrent() || isCurrent.dispatchCurrent?.() === false)
        return;
    if (!await shell.execute(path, [], isCurrent.bootContext?.()))
        receipt.pending.push('应用启动待确认：' + path.replace(/^.*[\\/]/, ''));
}
let launchEpoch = 0, launchTimer: ReturnType<typeof setTimeout> | null = null, stopLaunchSteam: (() => void) | null = null;
export function cancelBootModeLaunches() {
    launchEpoch++;
    if (launchTimer !== null) {
        clearTimeout(launchTimer);
        launchTimer = null;
    }
    stopLaunchSteam?.();
    stopLaunchSteam = null;
}
async function scheduleLaunches(snapshot: BootModeSnapshot, receipt: BootModeReceipt, generation: number, actionOwner: MouseOwnerCheck = () => true) {
    cancelBootModeLaunches();
    const epoch = launchEpoch, launch = object(snapshot.softwareLaunch), isCurrent = () => epoch === launchEpoch && generation === getSettingsGeneration();
    const currentLaunchOwner: MouseOwnerCheck = async () => {
        if (!isCurrent()) return false;
        const owned = await actionOwner();
        return owned && isCurrent();
    };
    currentLaunchOwner.bootContext = actionOwner.bootContext;
    currentLaunchOwner.dispatchCurrent = () => isCurrent() && actionOwner.dispatchCurrent?.() !== false;
    const run = async () => {
        if (!await currentLaunchOwner())
            return;
        const pendingBefore = receipt.pending.length, skippedBefore = receipt.skipped.length;
        const settings = await readOnce(loadSettings, '开机模式设置'), library = Array.isArray(settings.quickApps.apps) ? settings.quickApps.apps : [];
        const visited = new Set<string>();
        for (const path of Array.isArray(launch.paths) ? launch.paths : []) {
            if (!isCurrent()) return;
            const key = applicationKey(String(path));
            if (visited.has(key)) continue;
            visited.add(key);
            const app = library.find((entry: any) => applicationKey(String(entry?.path ?? '')) === key);
            if (!app) {
                receipt.skipped.push('应用已从全局快捷库删除');
                continue;
            }
            try {
                await launchOne(String(app.path), receipt, currentLaunchOwner, async () => {
                    const latest = await readOnce(loadSettings, '开机模式设置');
                    const apps = Array.isArray(latest.quickApps.apps) ? latest.quickApps.apps : [];
                    return apps.some((entry: any) => applicationKey(String(entry?.path ?? '')) === key);
                });
            }
            catch (error) {
                receipt.pending.push('应用启动：' + text(error));
            }
        }
        if (isCurrent()) recordModule(receipt, 'softwareLaunch', receipt.pending.length > pendingBefore ? 'waiting' : receipt.skipped.length > skippedBefore ? 'skipped' : 'requested',
            '已处理所选应用启动请求；启动命令返回不代表应用业务已就绪');
    };
    const clearPending = (message: string) => { receipt.pending = receipt.pending.filter(row => row !== message); };
    const steamPending = '快捷应用：等待 Steam 连接';
    const dispatch = async () => {
        stopLaunchSteam?.();
        stopLaunchSteam = null;
        clearPending(steamPending);
        const delay = Math.max(0, Math.min(30, Number(launch.delay) || 0));
        if (delay) {
            const waiting = '快捷应用：将在 ' + delay + ' 秒后按所选模式启动';
            receipt.pending.push(waiting);
            recordModule(receipt, 'softwareLaunch', 'waiting', waiting);
            launchTimer = setTimeout(() => {
                launchTimer = null;
                void run().then(async () => {
                    if (isCurrent()) {
                        clearPending(waiting);
                        await publishReceipt(receipt);
                    }
                }).catch(async error => {
                    if (isCurrent()) {
                        clearPending(waiting);
                        receipt.pending.push('快捷应用：' + text(error));
                        recordModule(receipt, 'softwareLaunch', 'failed', text(error));
                        await publishReceipt(receipt);
                    }
                });
            }, delay * 1000);
        }
        else {
            await run();
            if (isCurrent())
                await publishReceipt(receipt);
        }
    };
    if (launch.timing === 'manual' || !(launch.paths as unknown[])?.length) {
        recordModule(receipt, 'softwareLaunch', launch.paths?.length ? 'saved' : 'disabled', launch.paths?.length ? '仅保存手动启动清单，本次开机不执行' : '没有选择开机联动应用');
        return;
    }
    if (launch.timing === 'steam') {
        receipt.pending.push(steamPending);
        recordModule(receipt, 'softwareLaunch', 'waiting', steamPending);
        let reading = false, dirty = false, queued = false, dispatched = false;
        // Subscribe before the first snapshot. An event received during a read
        // latches one later observation; it must not be discarded as "busy".
        // Follow-ups share the startup queue and are never awaited from its own
        // transaction, so input/result commits cannot race the app handoff.
        const enqueue = () => {
            if (!isCurrent() || dispatched) return;
            if (reading) { dirty = true; return; }
            if (queued) return;
            queued = true;
            void serialized(async () => {
                queued = false;
                await observe();
            }).catch(async error => {
                if (!isCurrent()) return;
                stopLaunchSteam?.(); stopLaunchSteam = null;
                clearPending(steamPending);
                receipt.pending.push('快捷应用：' + text(error));
                recordModule(receipt, 'softwareLaunch', 'failed', text(error));
                await publishReceipt(receipt);
            });
        };
        const observe = async () => {
            if (!isCurrent() || dispatched) return;
            reading = true; dirty = false;
            try {
                const state = await optional(() => steamUi.read({ automatic: true }), 'Steam 连接状态');
                // A newer notification revokes this reply, even when it says running.
                if (isCurrent() && !dirty && steamConnected(state)) {
                    dispatched = true;
                    await dispatch();
                }
            } finally {
                reading = false;
                if (dirty && isCurrent() && !dispatched) enqueue();
            }
        };
        stopLaunchSteam = steamUi.subscribe(enqueue);
        await observe();
    }
    else
        await dispatch();
}
/** Failure-only observations of the original awaits, with constant retained space.
 * This clock never controls admission, retries or business deadlines. */
function bootStartupTiming() {
    const now = () => typeof performance === 'undefined' ? Date.now() : performance.now();
    const started = now();
    let stage = 'queue', since = started;
    const slow: Array<[string, number]> = [];
    const elapsed = (end: number, begin: number) => Math.min(86400000, Math.max(0, Math.round(end - begin)));
    const retain = (name: string, ms: number) => {
        slow.push([name, ms]); slow.sort((a, b) => b[1] - a[1]); slow.length = Math.min(slow.length, 3);
    };
    return {
        mark(next: string) {
            try { const time = now(); retain(stage, elapsed(time, since)); stage = next; since = time; }
            catch { /* Observation cannot change the original await. */ }
        },
        failure() {
            try {
                const time = now();
                const rows = [...slow, [stage, elapsed(time, since)] as [string, number]].sort((a, b) => b[1] - a[1]).slice(0, 3);
                return ' · boot-stages:' + JSON.stringify({ at: stage, elapsedMs: elapsed(time, started), slow: rows });
            } catch { return ''; }
        },
    };
}
interface StartupContext {
    boot: string; generation: number; mode: BootModeId;
    sourceRevision: number; revision: number; writeId: string; committed: boolean; savedResult?: string; activeBootMarked?: boolean; markActive?: () => Promise<boolean>; timing?: ReturnType<typeof bootStartupTiming>;
}
const startupOwners = new WeakMap<BootModeReceipt, StartupContext>();
let startupReceipt: BootModeReceipt | null = null;
const inputModules = ['input', 'gyro'] as const;
function inputModuleConfirmed(module: 'input' | 'gyro', request: InputApplicationRequest, proof: InputApplicationProof): boolean {
    return (module === 'input' ? proof.inputState : proof.gyroState) ===
        ((module === 'input' ? request.target : request.gyro) ? 'active' : 'disabled');
}
function bootPerformanceIntent(value: any) {
    return { schedule: snapshotPerformanceSchedule({ ...value.schedule,
        powerSchemes: normalizeControlPowerSchemes(value.powerSchemes ?? value.schedule?.powerSchemes),
        controls: normalizeContinuousControls(value.controls ?? (value.schedule ? continuousControlsFromSchedule(snapshotPerformanceSchedule(value.schedule)) : undefined)), configured: true, enabled: true }), frames: frameRatePair(value.frames) };
}
function bootPerformanceIntentMatches(settings: UnifiedSettings, requested: any): boolean {
    return !!requested && same(bootPerformanceIntent(requested), {
        schedule: snapshotPerformanceSchedule(settings.performanceSchedule as any), frames: frameRatePair(settings.rtss.frameRates) });
}
function syncStartupReceipt(confirmed: BootStartupResult, claim: BootStartupReceiptOwner): void {
    const receipt = startupReceipt, owner = receipt && startupOwners.get(receipt);
    if (receipt && owner && owner.boot === confirmed.boot && owner.mode === confirmed.mode &&
        owner.sourceRevision === confirmed.sourceRevision && owner.revision === confirmed.revision && owner.writeId === claim.writeId) {
        receipt.modules = cloneBootValue(confirmed.modules);
        receipt.pending = [...confirmed.pending]; receipt.applied = [...confirmed.applied];
        owner.savedResult = JSON.stringify(confirmed);
    }
    if (typeof window !== 'undefined') window.dispatchEvent(new CustomEvent('boot-mode:changed', { detail: cloneBootValue(confirmed) }));
}
/** Observe the original owner's ACKs. Never replay a scene or probe on a timer. */
export function observeBootModeRuntimeReceipts(): () => void {
    let active = true;
    let performanceVersion = 0;
    let performanceProof: { signature: string; context: ContinuousPerformanceApplicationContext; side: 'ac' | 'dc'; version: number } | undefined;
    let performanceQueued = false, performanceDirty = false;
    const generation = getSettingsGeneration();
    let proof: InputApplicationProof | undefined, proofKey = '', temporary: boolean | undefined,
        shortcutVersion = 0, shortcutKey = '', queued = false, dirty = false;
    const current = () => active && generation === getSettingsGeneration();
    const retiredPerformanceReceipt = Symbol('retired performance startup receipt');
    const observePerformance = async (observed: NonNullable<typeof performanceProof>) => {
        const { signature, context, side, version } = observed;
        const accepts = (settings: UnifiedSettings): BootStartupResult | undefined => {
            if (!current() || performanceProof !== observed || version !== performanceVersion || !isGameStatusCurrent()) return;
            const writeId = settings.extensions?.bootModesClaimWriteId;
            if (context.writeId === null ? writeId !== undefined : writeId !== context.writeId) return;
            const result = normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, settings.input.startupObservedBoot);
            const mode = object(settings.extensions?.[MODULE_KEY]), row = result?.modules?.performance;
            if (!result || result.status === 'failed' || result.boot !== context.boot || result.mode !== context.mode ||
                result.sourceRevision !== context.sourceRevision || result.revision !== context.revision ||
                settings.extensions?.bootModesAppliedBoot !== result.boot || settings.extensions?.bootModesActiveBoot !== result.boot ||
                mode.schema !== 1 || mode.current !== result.mode || !Number.isSafeInteger(mode.revision) || mode.revision < result.revision ||
                !row?.requested || !['saved', 'waiting', 'requested', 'failed'].includes(row.state) ||
                !bootPerformanceIntentMatches(settings, row.requested)) return;
            const { schedule, frames } = bootPerformanceIntent(row.requested);
            if (schedule.suspended || !schedule.configured || !schedule.enabled) return;
            const game = getPolicyGame(), identity = game ? JSON.stringify([game.pid, game.processCreated,
                game.path.toLowerCase(), game.identityGeneration ?? 0]) : '';
            const expected = performanceConsumptionSignature(['global', schedule.configured, schedule.enabled], identity,
                side, frames[side].fps, continuousControlsFromSchedule(schedule)[side], normalizeControlPowerSchemes(schedule.powerSchemes)[side]);
            return expected === signature ? result : undefined;
        };
        const before = accepts(await readOnce(loadSettings, '开机性能回执'));
        if (!before) return;
        let confirmed: BootStartupResult | undefined;
        await updateSettings(settings => {
            const result = accepts(settings);
            if (!result) throw retiredPerformanceReceipt;
            result.modules!.performance = { ...result.modules!.performance!, state: 'active', detail: '原性能 owner 已确认本次启动的全局配置应用' };
            result.pending = result.pending.filter(row => !row.startsWith('性能调度：'));
            if (!result.applied.includes(LABELS.performance)) result.applied.push(LABELS.performance);
            result.status = result.pending.length || result.skipped.length ? 'partial' : 'applied';
            settings.extensions = { ...settings.extensions, bootModesStartupResult: result };
            confirmed = result;
        }, { origin: 'runtime' });
        if (confirmed && current() && performanceProof === observed && version === performanceVersion) {
            performanceProof = undefined;
            syncStartupReceipt(confirmed, context);
        }
    };
    const enqueuePerformance = () => {
        if (!current() || !performanceProof) return;
        performanceDirty = true;
        if (performanceQueued) return;
        performanceQueued = true;
        void serialized(async () => {
            try {
                while (performanceDirty && current()) {
                    performanceDirty = false;
                    const observed = performanceProof;
                    if (observed) await observePerformance(observed);
                }
            } finally { performanceQueued = false; }
        }).catch(error => { if (error !== retiredPerformanceReceipt) console.warn('性能开机回执保存未确认：', text(error)); });
    };
    const stopPerformance = onContinuousPerformanceApplied((signature, context) => {
        const version = ++performanceVersion;
        performanceProof = undefined;
        if (!context || context.generation !== generation || !current() ||
            context.writeId !== null && (typeof context.writeId !== 'string' || !context.writeId)) return;
        try {
            const parsed = JSON.parse(signature);
            if (!Array.isArray(parsed) || parsed[0]?.[0] !== 'global' || !['ac', 'dc'].includes(parsed[2])) return;
            performanceProof = { signature, context: Object.freeze({ ...context }), side: parsed[2], version };
        } catch { return; }
        enqueuePerformance();
    }, () => { ++performanceVersion; performanceProof = undefined; performanceDirty = false; });
    const eligible = () => current() && temporary === false;
    const acceptsInput = (settings: UnifiedSettings, observed: InputApplicationProof): BootStartupResult | undefined => {
        if (!eligible()) return;
        const result = normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, settings.input.startupObservedBoot);
        const mode = object(settings.extensions?.[MODULE_KEY]);
        if (!result || result.status === 'failed' || result.boot !== observed.boot ||
            settings.extensions?.bootModesAppliedBoot !== result.boot || mode.schema !== 1 || mode.current !== result.mode ||
            !Number.isSafeInteger(mode.revision) || mode.revision < result.revision ||
            !sameInputApplication(inputApplicationRequest(settings.input), observed)) return;
        return result;
    };
    const observeInput = async () => {
        const observed = proof;
        if (!observed || !eligible()) return;
        const settings = await readOnce(loadSettings, '开机输入回执');
        const claim = bootStartupReceiptOwner(settings), before = acceptsInput(settings, observed);
        if (!claim || !before || proof !== observed || !inputModules.some(module => {
            const row = before.modules?.[module], request = readInputApplicationRequest(row?.requested?.inputApplication);
            const enabled = module === 'input' ? request?.target : request?.gyro;
            return request && sameInputApplication(request, observed) && inputModuleConfirmed(module, request, observed) &&
                !(row?.state === (enabled ? 'active' : 'disabled') && row.proof && sameInputApplication(request, row.proof));
        })) return;
        let confirmed: BootStartupResult | undefined;
        const accepted = await updateSettingsForInputReceipt({ revision: observed.inputRevision, hash: observed.inputConfigHash }, settings => {
            const result = acceptsInput(settings, observed);
            if (!result || proof !== observed || result.boot !== before.boot || result.mode !== before.mode ||
                result.sourceRevision !== before.sourceRevision || result.revision !== before.revision) return false;
            let changed = false;
            for (const module of inputModules) {
                const row = result.modules?.[module], request = readInputApplicationRequest(row?.requested?.inputApplication);
                if (!row || !request || !sameInputApplication(request, observed) || !inputModuleConfirmed(module, request, observed)) continue;
                const enabled = module === 'input' ? request.target : request.gyro;
                if (row.state === (enabled ? 'active' : 'disabled') && row.proof && sameInputApplication(request, row.proof)) continue;
                result.modules![module] = { ...row, state: enabled ? 'active' : 'disabled', proof: observed,
                    detail: enabled ? '原生已确认本次配置的本地输出；不代表游戏消费' : '原生已确认本次配置关闭' };
                result.pending = result.pending.filter(value => value !== inputProofPending(module));
                if (enabled && !result.applied.includes(inputProofLabel(module))) result.applied.push(inputProofLabel(module));
                changed = true;
            }
            if (!changed) return false;
            const input = settings.input;
            input.pendingConfigHash = observed.inputConfigHash;
            if (observed.inputState === 'active') {
                input.hostAcknowledgedRevision = observed.inputRevision; input.hostAcknowledgedConfigHash = observed.inputConfigHash;
                const request = inputApplicationRequest(input);
                if (inputModules.every(module => inputModuleConfirmed(module, request, observed))) {
                    input.appliedRevision = observed.inputRevision; input.appliedConfigHash = observed.inputConfigHash; input.applyStatus = 'active';
                } else input.applyStatus = 'host-acknowledged';
            } else if (observed.inputState === 'disabled') {
                input.hostAcknowledgedRevision = null; input.hostAcknowledgedConfigHash = null; input.applyStatus = 'safe-stop';
            }
            result.status = result.pending.length || result.skipped.length ? 'partial' : 'applied';
            settings.extensions = { ...settings.extensions, bootModesStartupResult: result };
            confirmed = result;
            return true;
        }, settings => {
            if (!eligible() || proof !== observed) return false;
            if (!settings) return true;
            const owner = bootStartupReceiptOwner(settings);
            return owner?.boot === claim.boot && owner.writeId === claim.writeId;
        }, { origin: 'boot-mode' });
        if (accepted && confirmed && current()) syncStartupReceipt(confirmed, claim);
    };
    const enqueueInput = () => {
        if (!current()) return;
        dirty = true;
        if (queued) return;
        queued = true;
        void serialized(async () => {
            try { while (dirty && current()) { dirty = false; await observeInput(); } }
            finally { queued = false; }
        }).catch(error => { console.warn('输入开机回执保存未确认：', text(error)); });
    };
    const acceptShortcut = (raw: unknown) => {
        if (!current()) return;
        ++shortcutVersion;
        const value = object(raw);
        if (typeof value.active !== 'boolean' || !(PERSONAS as readonly unknown[]).includes(value.persona) ||
            typeof value.virtualEnabled !== 'boolean' || typeof value.gyroEnabled !== 'boolean') {
            temporary = undefined; proof = undefined; proofKey = ''; shortcutKey = ''; return;
        }
        const key = JSON.stringify([value.active, value.persona, value.virtualEnabled, value.gyroEnabled]);
        if (shortcutKey && shortcutKey !== key) { proof = undefined; proofKey = ''; }
        shortcutKey = key; temporary = value.active;
        enqueueInput();
    };
    const stopInput = on('gamepad.state', raw => {
        if (!current()) return;
        const next = inputApplicationFromGamepad(raw), key = next ? JSON.stringify(next) : '';
        if (key === proofKey) return;
        proof = next; proofKey = key;
        enqueueInput();
    });
    const stopShortcut = on('input.shortcutRuntime', acceptShortcut);
    const initialVersion = shortcutVersion;
    void optional(() => invoke('input.shortcutRuntime.get', {}), '临时手柄所有权').then(value => {
        if (current() && shortcutVersion === initialVersion) acceptShortcut(value);
    });
    const startupChanged = () => { enqueueInput(); enqueuePerformance(); };
    if (typeof window !== 'undefined') window.addEventListener('boot-mode:changed', startupChanged);
    const retiredMonitorReceipt = Symbol('retired monitor startup receipt');
    const stop = onMonitorOutputApplied((preferences, ack, intent) => {
        const claim = intent?.startupReceiptOwner;
        if (!current() || !claim || !ack.ok || ack.applied !== (preferences.enabled ? preferences.output : 'off')) return;
        const target = normalizeMonitorOutput(preferences);
        const accepts = (settings: UnifiedSettings): BootStartupResult | undefined => {
            const owner = bootStartupReceiptOwner(settings);
            if (!current() || !owner || owner.boot !== claim.boot || owner.writeId !== claim.writeId) return;
            const result = normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, object(settings.input).startupObservedBoot);
            const mode = object(settings.extensions?.[MODULE_KEY]);
            const monitor = result?.modules?.monitor;
            if (!result || result.status === 'failed' || settings.extensions?.bootModesAppliedBoot !== result.boot ||
                mode.schema !== 1 || mode.current !== result.mode || !Number.isSafeInteger(mode.revision) || mode.revision < result.revision ||
                !monitor?.requested || !['waiting', 'requested', 'failed'].includes(monitor.state) ||
                !same(normalizeMonitorOutput(monitor.requested), target) ||
                !same(normalizeMonitorOutput(settings.rtss.monitorOutput), target)) return;
            return result;
        };
        return serialized(async () => {
            const before = accepts(await readOnce(loadSettings, '开机处理结果'));
            if (!before) return;
            let confirmed: BootStartupResult | undefined;
            await updateSettings(settings => {
                const result = accepts(settings);
                if (!result || result.boot !== before.boot || result.mode !== before.mode ||
                    result.sourceRevision !== before.sourceRevision || result.revision !== before.revision) throw retiredMonitorReceipt;
                result.modules!.monitor = { ...result.modules!.monitor!, state: target.enabled ? 'active' : 'disabled',
                    detail: target.enabled ? '原监控 owner 已确认本次启动的目标输出' : '原监控 owner 已确认本次启动关闭输出' };
                result.pending = result.pending.filter(row => !row.startsWith('监控：'));
                if (!target.enabled) result.applied = result.applied.filter(label => label !== LABELS.monitor);
                else if (!result.applied.includes(LABELS.monitor)) result.applied.push(LABELS.monitor);
                result.status = result.pending.length || result.skipped.length ? 'partial' : 'applied';
                settings.extensions = { ...settings.extensions, bootModesStartupResult: result };
                confirmed = result;
            }, { origin: 'runtime' });
            if (!confirmed || !current()) return;
            syncStartupReceipt(confirmed, claim);
        }).catch(error => { if (error !== retiredMonitorReceipt) console.warn('监控开机回执保存未确认：', text(error)); });
    });
    return () => {
        active = false; ++performanceVersion; performanceProof = undefined; performanceDirty = false; proof = undefined; dirty = false; stop(); stopInput(); stopShortcut(); stopPerformance();
        if (typeof window !== 'undefined') window.removeEventListener('boot-mode:changed', startupChanged);
    };
}
async function rememberStartupResult(result: BootStartupResult, owner: StartupContext): Promise<boolean> {
    if (getSettingsGeneration() !== owner.generation) return false;
    const ownsResult = (settings: UnifiedSettings): boolean => {
        if (settings.extensions?.bootModesAppliedBoot !== owner.boot || settings.input.startupObservedBoot !== owner.boot ||
            settings.extensions.bootModesClaimWriteId !== owner.writeId) return false;
        const active = object(settings.extensions?.[MODULE_KEY]);
        // A late v1 publisher cannot replace evidence owned by a newer format.
        if (active.schema !== 1 || object(settings.extensions?.bootModesStartupResult).schema > 1) return false;
        if (result.status !== 'failed' && active.current !== owner.mode) return false;
        const previous = normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, owner.boot);
        return !previous || previous.sourceRevision < owner.sourceRevision || previous.sourceRevision === owner.sourceRevision && previous.revision <= result.revision;
    };
    const settings = await readOnce(loadSettings, '开机处理结果所属配置');
    assertSettingsGeneration(owner.generation);
    if (!ownsResult(settings)) return false;
    const encoded = JSON.stringify(result);
    if (owner.savedResult === encoded && !['waiting', 'requested', 'failed'].includes(result.modules?.monitor?.state ?? '') &&
        !['saved', 'waiting', 'requested', 'failed'].includes(result.modules?.performance?.state ?? '') &&
        !inputModules.some(module => ['saved', 'waiting', 'requested', 'failed'].includes(result.modules?.[module]?.state ?? ''))) {
        // A local memo cannot acknowledge a record another renderer replaced.
        return JSON.stringify(settings.extensions?.bootModesStartupResult) === encoded;
    }
    let accepted = false;
    const remember = (settings: UnifiedSettings): boolean => {
        assertSettingsGeneration(owner.generation);
        if (!ownsResult(settings)) return false;
        // A later startup operation (or independent newer result) owns its own
        // evidence; an old asynchronous completion cannot replace that record.
        const previous = normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, owner.boot);
        const storedPerformance = previous?.modules?.performance, incomingPerformance = result.modules?.performance;
        if (previous?.sourceRevision === result.sourceRevision && previous.revision === result.revision && previous.mode === result.mode &&
            storedPerformance?.state === 'active' && storedPerformance.requested && incomingPerformance?.requested &&
            same(storedPerformance.requested, incomingPerformance.requested) && bootPerformanceIntentMatches(settings, storedPerformance.requested)) {
            result.modules!.performance = cloneBootValue(storedPerformance);
            result.pending = result.pending.filter(row => !row.startsWith('性能调度：'));
            if (!result.applied.includes(LABELS.performance)) result.applied.push(LABELS.performance);
        }
        // Another renderer may have consumed the monitor ACK while this owner
        // still awaited mouse/apps. A later publication cannot undo that proof.
        const monitor = previous?.modules?.monitor, incoming = result.modules?.monitor;
        if (previous?.sourceRevision === result.sourceRevision && previous.revision === result.revision && previous.mode === result.mode &&
            monitor?.requested && monitor.state === (normalizeMonitorOutput(monitor.requested).enabled ? 'active' : 'disabled') && incoming?.requested &&
            same(normalizeMonitorOutput(monitor.requested), normalizeMonitorOutput(incoming.requested)) &&
            same(normalizeMonitorOutput(monitor.requested), normalizeMonitorOutput(settings.rtss.monitorOutput))) {
            result.modules!.monitor = cloneBootValue(monitor);
            result.pending = result.pending.filter(row => !row.startsWith('监控：'));
            if (monitor.state === 'disabled') result.applied = result.applied.filter(label => label !== LABELS.monitor);
            else if (!result.applied.includes(LABELS.monitor)) result.applied.push(LABELS.monitor);
            if (result.status !== 'failed') result.status = result.pending.length || result.skipped.length ? 'partial' : 'applied';
        }
        const currentInput = inputApplicationRequest(settings.input);
        for (const module of inputModules) {
            const incoming = result.modules?.[module], stored = previous?.modules?.[module];
            const request = readInputApplicationRequest(incoming?.requested?.inputApplication);
            if (!incoming || !request) continue;
            if (incoming.proof && (incoming.proof.boot !== result.boot || !sameInputApplication(currentInput, incoming.proof) ||
                !sameInputApplication(request, incoming.proof) || !inputModuleConfirmed(module, request, incoming.proof))) {
                delete incoming.proof; incoming.state = 'saved'; incoming.detail = '配置已保存；等待本次修订的原生生效回执';
                if (!result.pending.includes(inputProofPending(module))) result.pending.push(inputProofPending(module));
                result.applied = result.applied.filter(label => label !== inputProofLabel(module));
            }
            if (previous?.sourceRevision === result.sourceRevision && previous.revision === result.revision && previous.mode === result.mode &&
                stored?.proof?.boot === result.boot && same(stored.requested, incoming.requested) &&
                sameInputApplication(currentInput, stored.proof) && sameInputApplication(request, stored.proof) &&
                inputModuleConfirmed(module, request, stored.proof) &&
                stored.state === ((module === 'input' ? request.target : request.gyro) ? 'active' : 'disabled')) {
                result.modules![module] = cloneBootValue(stored);
                result.pending = result.pending.filter(value => value !== inputProofPending(module));
                if (stored.state === 'active' && !result.applied.includes(inputProofLabel(module))) result.applied.push(inputProofLabel(module));
            }
        }
        if (result.status !== 'failed') result.status = result.pending.length || result.skipped.length ? 'partial' : 'applied';
        settings.extensions = { ...settings.extensions, bootModesStartupResult: cloneBootValue(result) };
        return true;
    };
    if (inputModules.some(module => readInputApplicationRequest(result.modules?.[module]?.requested?.inputApplication))) {
        accepted = await updateSettingsForInputReceipt(inputApplicationRequest(settings.input), remember,
            () => getSettingsGeneration() === owner.generation);
    } else await updateSettings(settings => { accepted = remember(settings); });
    if (accepted) owner.savedResult = JSON.stringify(result);
    return accepted;
}
async function publishReceipt(receipt: BootModeReceipt): Promise<void> {
    const owner = startupOwners.get(receipt);
    if (owner) {
        const result: BootStartupResult = { schema: 1, boot: owner.boot, mode: owner.mode,
            sourceRevision: owner.sourceRevision, revision: owner.revision,
            status: receipt.pending.length || receipt.skipped.length ? 'partial' : 'applied',
            ...(receipt.modules ? { modules: cloneBootValue(receipt.modules) } : {}),
            applied: [...receipt.applied], skipped: [...receipt.skipped], pending: [...receipt.pending] };
        try {
            if (!await rememberStartupResult(result, owner)) return;
            receipt.modules = cloneBootValue(result.modules);
            receipt.pending = [...result.pending]; receipt.applied = [...result.applied];
        }
        catch (error) {
            if (getSettingsGeneration() !== owner.generation) return;
            const message = '开机处理结果：保存未确认 · ' + text(error);
            if (!receipt.pending.includes(message)) receipt.pending.push(message);
            console.warn('开机处理结果保存未确认：', text(error));
        }
    }
    if (typeof window !== 'undefined')
        window.dispatchEvent(new CustomEvent('boot-mode:changed', { detail: cloneBootValue(receipt) }));
}
/** Startup restoration only. Immediate switching is deferred; see the preserved candidate record. */
export function applyBootMode(id: BootModeId, document: BootModeDocument, options: {
    startup?: boolean;
} = {}): Promise<{
    document: BootModeDocument;
    receipt: BootModeReceipt;
}> {
    if (options.startup !== true)
        return Promise.reject(Error('开机模式需重启后生效，不支持即时切换'));
    // A new startup restore cancels any older pending mouse handoff.
    cancelBootModeDeferredMouse();
    const mouseEpoch = deferredMouseEpoch;
    return serialized(() => applyBootModeInQueue(id, document, mouseEpoch));
}
async function applyBootModeInQueue(id: BootModeId, document: BootModeDocument, mouseEpoch: number, owner?: StartupContext, startupPending: string[] = []): Promise<{document: BootModeDocument; receipt: BootModeReceipt}> {
    cancelBootModeLaunches();
    document = normalizeBootModeDocument(document, document.global);
    owner?.timing?.mark('apply:settings');
    const generation = getSettingsGeneration(), beforeSettings = await readOnce(loadSettings, '开机模式设置'), before = await captureBootModeSnapshot(undefined, owner?.timing ? stage => owner.timing!.mark('apply-capture:' + stage) : undefined), next = resolveBootMode(document, id), receipt: BootModeReceipt = { mode: id, applied: [], skipped: [], pending: [...startupPending],
        modules: Object.fromEntries(BOOT_MODE_MODULES.map(module => [module, same(before[module], next[module]) ? { state: 'unchanged' as const } : { state: 'waiting' as const, detail: '尚未处理本次启动配置' }])) };
    if (owner) { startupOwners.set(receipt, owner); startupReceipt = receipt; }
    if (next.display.resolution === 'native')
        next.display.resolution = document.global.display.resolution ?? before.display.resolution;
    if (next.display.hz === 'native')
        next.display.hz = document.global.display.hz ?? before.display.hz;
    const current = readStoredBootMode(beforeSettings, document.global);
    if (current.revision !== document.revision)
        throw Error('开机模式已变化，请重新读取');
    const unresolved = new Set(unresolvedBootModeModules(document, id, before));
    // Preserve unspecified runtime fields without promoting them to global intent.
    for (const module of document.unresolvedGlobal ?? []) next[module] = mergeBootConfig(before[module], next[module]);
    const markUnresolved = () => {
        for (const module of unresolved) {
            const message = LABELS[module] + '：旧配置缺少全局数据，请在全局中确认；未指定字段保持当前值';
            if (!receipt.pending.includes(message)) receipt.pending.push(message);
            if (receipt.modules?.[module]?.state !== 'failed') recordModule(receipt, module, 'waiting', message);
        }
    };
    markUnresolved();
    const inputClaim = (settings: UnifiedSettings) =>
        [settings.extensions?.bootModes, settings.extensions?.bootModesAppliedBoot, settings.extensions?.bootModesActiveBoot,
            settings.extensions?.bootModesStartupResult, settings.input.startupObservedBoot, settings.extensions?.bootModesClaimWriteId];
    const claimedInputOwner = inputClaim(beforeSettings);
    if (owner && claimedInputOwner[5] !== owner.writeId) throw Error('开机预设领取归属已变化');
    let inputOwnerLost = false;
    let inputStage = 'initial', inputOwnerDiagnostic = '';
    let verifiedMouseSettings = beforeSettings;
    const ownerError = (message: string) => Error(message + (inputOwnerDiagnostic ? ' · ' + inputOwnerDiagnostic : ''));
    const isCurrent = (settings?: UnifiedSettings, allowPublishedMarker = false) => {
        const actualGeneration = getSettingsGeneration();
        const actual = settings ? inputClaim(settings) : undefined;
        if (actual && allowPublishedMarker && owner && actual[2] === owner.boot) actual[2] = claimedInputOwner[2];
        const matches = !settings || equalSettingsDocuments(actual, claimedInputOwner) &&
            settings.input.revision === inputRevision && computeInputConfigHash(settings.input) === inputHash;
        if (!inputOwnerLost && (!matches || actualGeneration !== generation)) {
            inputOwnerLost = true;
            // Capture the first failed comparison from this existing read only.
            // No fresh probe, retry, raw settings payload or owner relaxation.
            try {
                const names = ['mode', 'appliedBoot', 'activeBoot', 'startupResult', 'observedBoot', 'claimWriteId'];
                const expectedMode = object(claimedInputOwner[0]), actualMode = object(actual?.[0]);
                const fields = actual ? names.filter((_, index) => !equalSettingsDocuments(actual[index], claimedInputOwner[index])) : [];
                if (settings && settings.input.revision !== inputRevision) fields.push('inputRevision');
                if (settings && computeInputConfigHash(settings.input) !== inputHash) fields.push('inputHash');
                if (actualGeneration !== generation) fields.push('generation');
                const modeParts = ['schema', 'revision', 'current', 'boot', 'bootEnabled', 'preparation', 'global', 'scenes', 'presets', 'activeSnapshot']
                    .filter(key => !equalSettingsDocuments(expectedMode[key], actualMode[key]));
                inputOwnerDiagnostic = 'boot-owner-diff:' + JSON.stringify({ stage: inputStage, fields,
                    modeParts: fields.includes('mode') ? modeParts : [],
                    globalModules: fields.includes('mode') ? BOOT_MODE_MODULES.filter(module => !equalSettingsDocuments(object(expectedMode.global)[module], object(actualMode.global)[module])) : [],
                    modeRevision: [expectedMode.revision ?? null, actualMode.revision ?? null],
                    inputRevision: [inputRevision, settings?.input.revision ?? null], generation: [generation, actualGeneration] });
            } catch { /* Diagnostics cannot change the original rejection. */ }
        }
        return !inputOwnerLost && actualGeneration === generation;
    };
    const currentInputOwner: MouseOwnerCheck = async settings => {
        if (!isCurrent()) return false;
        try {
            const current = settings ?? await readOnce(loadSettings, '开机输入所属配置');
            assertBootSettingsReady();
            if (!isCurrent(current)) return false;
            verifiedMouseSettings = current;
            return true;
        } catch (error) { inputOwnerLost = true; throw error; }
    };
    currentInputOwner.bootContext = () => mouseBootContext(verifiedMouseSettings);
    const softwareSettingsCurrent = (settings?: UnifiedSettings, allowPublishedMarker = false) => {
        if (!isCurrent()) return false;
        if (!settings) return true;
        // Same-request ACKs may advance while the original startup is awaiting
        // completion; retain its exact mode, input and Windows boot ownership.
        const result = owner && normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, owner.boot);
        if (owner && result && result.mode === owner.mode && result.sourceRevision === owner.sourceRevision && result.revision === owner.revision)
            claimedInputOwner[3] = cloneBootValue(settings.extensions?.bootModesStartupResult);
        if (owner?.activeBootMarked) claimedInputOwner[2] = owner.boot;
        return isCurrent(settings, allowPublishedMarker);
    };
    const currentSoftwareOwner: MouseOwnerCheck = async () => {
        if (!isCurrent()) return false;
        try {
            const settings = await readOnce(loadSettings, '开机软件所属配置');
            if (!softwareSettingsCurrent(settings)) return false;
            return currentInputOwner(settings);
        } catch (error) { inputOwnerLost = true; throw error; }
    };
    currentSoftwareOwner.bootContext = currentInputOwner.bootContext;
    currentSoftwareOwner.dispatchCurrent = isCurrent;
    const requireInputOwner = async () => {
        if (!await currentInputOwner()) throw ownerError('开机输入所属模式已变化，旧操作已停止');
    };
    // Native-only metadata is not input intent. No dedicated game profile is written here.
    const inputChanged = !same(inputPatch(before, beforeSettings, false), inputPatch(next, beforeSettings, true));
    const mouseChanged = before.input.persona !== next.input.persona || (next.input.mouse !== null && (before.input.mouse !== next.input.mouse || (before.input.mousePolicy !== null && before.input.mousePolicy !== (next.input.mouse === 'off' ? 'off' : 'on'))));
    const steps: BootApplyStep[] = [];
    let inputRevision = beforeSettings.input.revision;
    let inputHash = computeInputConfigHash(beforeSettings.input);
    recordInputRequest(receipt, beforeSettings.input, unresolved);
    let deferMouse = false;
    if (mouseChanged)
        steps.push({ id: 'mouse-stop', apply: async () => {
                try {
                    await stopMouse(currentInputOwner);
                }
                catch (error) {
                    if (!(error instanceof BootMouseUnavailable))
                        throw error;
                    receipt.pending.push('原模拟鼠标：' + text(error));
                }
            }, rollback: () => restoreMouse(before.input, currentInputOwner) });
    if (inputChanged)
        steps.push({ id: 'global-input', apply: async () => {
                const savedInput = await writeBaseInput(next, inputRevision, isCurrent);
                inputRevision = savedInput.revision;
                inputHash = computeInputConfigHash(savedInput);
                recordInputRequest(receipt, savedInput, unresolved);
            }, rollback: async () => {
                const result = await compareAndSwapInputSettings(inputRevision, { outputTarget: beforeSettings.input.outputTarget, ownership: bootInputOwnership(beforeSettings.input.ownership), gyroMotion: beforeSettings.input.gyroMotion, bootModeShortcuts: null }, isCurrent, { origin: 'boot-mode' });
                if (result.ok === false)
                    throw Error('旧输入配置恢复遇到新版本：' + result.reason);
                inputRevision = result.value.revision;
                inputHash = computeInputConfigHash(result.value);
            } });
    if (mouseChanged && next.input.mouse !== null)
        steps.push({ id: 'mouse-start', apply: async () => {
                try {
                    await applyMouse(next.input, currentInputOwner);
                    receipt.applied.push('模拟鼠标');
                }
                catch (error) {
                    if (!(error instanceof BootMouseUnavailable))
                        throw error;
                    if (next.input.persona === 'steamdeck')
                        receipt.pending.push('模拟鼠标：由 Steam 管理，桌面鼠标布局状态未确认');
                    else {
                        deferMouse = true;
                        receipt.pending.push(MOUSE_WAITING);
                    }
                }
            }, rollback: () => stopMouse(currentInputOwner) });
    else if (mouseChanged)
        receipt.pending.push('模拟鼠标：目标配置未知，已关闭旧模拟鼠标');
    const saved = cloneBootValue(document);
    saved.current = id;
    saved.revision = nextBootRevision(saved.revision);
    saved.activeSnapshot = cloneBootValue(before);
    for (const module of ['input', 'gyro', 'softwareLaunch'] as const)
        saved.activeSnapshot[module] = cloneBootValue(next[module]);
    steps.push({ id: 'save-current-mode', apply: async () => {
        const committed = await updateSettingsForInputReceipt({ revision: inputRevision, hash: inputHash }, settings => {
            if (!isCurrent(settings)) throw ownerError('开机输入所属模式已变化，未保存旧模式');
            const latest = readStoredBootMode(settings, document.global);
            if (latest.revision !== document.revision)
                throw Error('模式切换期间配置已变化');
            if (deferMouse)
                saved.activeSnapshot.input.mouse = null;
            settings.extensions = { ...settings.extensions, [MODULE_KEY]: saved };
            return true;
        }, isCurrent, { origin: 'boot-mode' });
        if (!committed) throw ownerError('开机输入所属配置已变化，未提交旧模式');
        claimedInputOwner[0] = cloneBootValue(saved);
        }, rollback: async () => {
            const restored = await updateSettingsForInputReceipt({ revision: inputRevision, hash: inputHash }, settings => {
                if (!isCurrent(settings)) throw ownerError('开机输入所属模式已变化，未恢复旧模式');
                const latest = readStoredBootMode(settings, document.global);
                if (latest.revision !== saved.revision || latest.current !== id)
                    return false;
                settings.extensions = { ...settings.extensions, [MODULE_KEY]: beforeSettings.extensions?.[MODULE_KEY] };
                return true;
            }, isCurrent, { origin: 'boot-mode' });
            if (restored) claimedInputOwner[0] = beforeSettings.extensions?.[MODULE_KEY];
        } });
    owner?.timing?.mark('apply:input-mode');
    await executeBootModeSteps(steps.map(step => ({
        id: step.id,
        apply: async () => { inputStage = step.id + ':before'; await requireInputOwner(); inputStage = step.id + ':write'; await step.apply(); inputStage = step.id + ':after'; await requireInputOwner(); },
        rollback: async () => { inputStage = step.id + ':rollback-before'; await requireInputOwner(); inputStage = step.id + ':rollback-write'; await step.rollback(); inputStage = step.id + ':rollback-after'; await requireInputOwner(); },
    })), isCurrent);
    if (owner) owner.committed = true;
    // Commit input/mouse and selected mode first. Optional hardware/software failures have explicit
    // receipts and cannot cause a later input error to replay an old hardware snapshot.
    for (const module of BOOT_MODE_MODULES) {
        // Saved CPU/TDP values are not proof that the original runtime owner
        // restored them in this Windows boot (notably a manual global bank).
        const activatePerformance = !!owner && module === 'performance' && !unresolved.has(module);
        if (['input', 'gyro', 'softwareLaunch', 'audioOptimization', 'frameGeneration'].includes(module) || same(before[module], next[module]) && !activatePerformance)
            continue;
        assertSettingsGeneration(generation);
        owner?.timing?.mark('module:' + module + ':read');
        const latestSettings = await readOnce(loadSettings, '开机模式设置');
        const latest = readStoredBootMode(latestSettings, document.global);
        if (latest.revision !== saved.revision || latest.current !== id) {
            inputStage = 'module:' + module + ':read';
            isCurrent(latestSettings); // Describe this already rejected read; do not change the gate.
            throw ownerError('当前模式已被其他窗口切换，后续操作已停止');
        }
        owner?.timing?.mark('module:' + module + ':apply');
        const pendingCount = receipt.pending.length, skippedCount = receipt.skipped.length;
        try {
            const inheritedPerformance = module === 'performance' && (id === 'global' || !Object.prototype.hasOwnProperty.call(object(document.scenes[id]?.overrides), 'performance'));
            await applyRuntimeModule(module, next[module], before[module], receipt, currentSoftwareOwner, !!owner && inheritedPerformance);
            if (receipt.pending.length === pendingCount && receipt.skipped.length === skippedCount && receipt.modules?.[module]?.state !== 'unchanged')
                saved.activeSnapshot[module] = cloneBootValue(next[module]);
        }
        catch (error) {
            receipt.pending.push(LABELS[module] + '：' + text(error));
            recordModule(receipt, module, 'failed', text(error), receipt.modules?.[module]?.requested);
        }
    }
    owner?.timing?.mark('apply:snapshot');
    const snapshotCommitted = await updateSettingsForInputReceipt({ revision: inputRevision, hash: inputHash }, settings => {
        if (!isCurrent(settings)) throw ownerError('当前输入所属模式已变化，未保存旧快照');
        const latest = readStoredBootMode(settings, document.global);
        if (latest.revision !== saved.revision || latest.current !== id)
            throw Error('当前模式已变化');
        settings.extensions = { ...settings.extensions, [MODULE_KEY]: saved };
        return true;
    }, isCurrent, { origin: 'boot-mode' });
    if (!snapshotCommitted) throw ownerError('当前输入所属配置已变化，未提交旧快照');
    claimedInputOwner[0] = cloneBootValue(saved);
    cancelBootModeLaunches();
    assertSettingsGeneration(generation);
    for (const module of ['audioOptimization', 'frameGeneration'] as const) {
        if (unresolved.has(module)) continue;
        owner?.timing?.mark('module:' + module + ':apply');
        if (!await currentSoftwareOwner()) throw Error('开机软件所属模式已变化，旧操作已停止');
        try {
            await applyRuntimeModule(module, next[module], before[module], receipt, currentSoftwareOwner);
        }
        catch (error) {
            receipt.pending.push(LABELS[module] + '：' + text(error));
            recordModule(receipt, module, 'failed', text(error));
        }
        if (!await currentSoftwareOwner()) throw Error('开机软件所属模式已变化，旧操作已停止');
    }
    owner?.timing?.mark('apply:publish');
    if (!unresolved.has('softwareLaunch')) await scheduleLaunches(next, receipt, generation, currentSoftwareOwner);
    markUnresolved();
    if (deferMouse && mouseEpoch === deferredMouseEpoch)
        deferBootMouse(next.input, receipt, generation, saved.global);
    await publishReceipt(receipt);
    if (owner) owner.markActive = async () => {
        let mayHavePublished = false;
        const readPublication = async () => {
            const value = await readOnce(loadSettings, '开机完成标记回读');
            assertSettingsGeneration(generation); assertBootSettingsReady();
            if (getSettingsRecoveryState().source !== 'main' || value.input.startupObservedBoot !== owner.boot ||
                value.extensions?.bootModesClaimWriteId !== owner.writeId || !softwareSettingsCurrent(value, true)) return 'stale';
            return value.extensions.bootModesActiveBoot === owner.boot ? 'published' : 'pending';
        };
        for (let attempt = 0; attempt < 2; attempt++) {
            let submitted = false;
            let accepted: boolean;
            try {
                accepted = await updateSettingsForInputReceipt({ revision: inputRevision, hash: inputHash }, settings => {
                    if (settings.extensions?.bootModesActiveBoot === owner.boot) return false;
                    submitted = true;
                    settings.extensions = { ...settings.extensions, bootModesActiveBoot: owner.boot };
                    return true;
                }, settings => softwareSettingsCurrent(settings, mayHavePublished), { origin: 'boot-mode' });
            } catch (error) {
                if (!submitted && !mayHavePublished) throw error;
                mayHavePublished = true;
                const observed = await readPublication();
                if (observed === 'published') return true;
                if (observed !== 'pending' || attempt === 1) throw error;
                continue;
            }
            if (accepted) return true;
            return mayHavePublished && await readPublication() === 'published';
        }
        return false;
    };
    return { document: saved, receipt };
}
let startupRestore: Promise<void> | null = null;
function unconfirmedStartupClaim(boot: string, mode: BootModeId, revision: number): BootStartupResult {
    return { schema: 1, boot, mode, sourceRevision: revision, revision, status: 'partial',
        applied: [], skipped: [], pending: ['本次开机预设已领取，应用结果尚未确认'] };
}
export function restoreSelectedBootMode(): Promise<void> {
    if (startupRestore) return startupRestore;
    const generation = getSettingsGeneration();
    const timing = bootStartupTiming();
    return startupRestore = serialized(async () => {
        assertSettingsGeneration(generation);
        timing.mark('identity');
        const boot = await invoke<string>('bootMode.startup.identity', {}, { timeoutMs: 3000 });
        assertSettingsGeneration(generation);
        if (!boot || typeof boot !== 'string') throw Error('Windows 启动标识尚未确认，开机模式暂不自动应用');
        timing.mark('settings');
        const settings = await readOnce(loadSettings, '开机模式设置');
        assertSettingsGeneration(generation); assertBootSettingsReady();
        readStoredBootMode(settings, savedOverviewSnapshot(settings));
        timing.mark('initial-performance');
        const initialSchedule = await initialPerformanceSchedule(settings), initialSource = cloneBootValue(settings.performanceSchedule);
        timing.mark('claim');
        type StartupClaim = { owner: StartupContext; document: BootModeDocument; writeId: string; result: unknown;
            input: { revision: number; hash: string } };
        let claimed: StartupClaim | undefined;
        const belongsToBoot = (value: UnifiedSettings) => !value.input.startupObservedBoot || value.input.startupObservedBoot === boot;
        const prepareClaim = (settings: UnifiedSettings, attempt: { value?: StartupClaim }): boolean => {
            assertSettingsGeneration(generation);
            const raw = object(settings.extensions?.[MODULE_KEY]);
            // Old builds claimed an unconfigured boot without applying global.
            // Materialize that missing document once; a real same-boot selection
            // or an already applied document still must not be replayed.
            const preparation = normalizeBootPreparation(raw.preparation, Number(raw.revision));
            // A previous native build may already have migrated v1 without a deferral marker.
            const migration = object(settings.extensions?.bootModesIdentityMigration);
            const legacyLoader = typeof migration.from === 'string'
                ? /^windows-loader-v1\|\{[0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}\}\|([0-9]{1,10})$/.exec(migration.from)
                : null;
            const migratedThisBoot = boot.startsWith('windows-volatile-v2|') && migration.to === boot &&
                settings.extensions?.bootModesAppliedBoot === boot && migration.policy === 'preserve-current-until-next-boot' &&
                legacyLoader !== null && Number(legacyLoader[1]) <= 0xffffffff;
            // Saving updates the document revision without replacing valid preparation.
            const deferredThisBoot = settings.extensions?.bootModesDeferredBoot === boot || migratedThisBoot ||
                preparation?.observedBoot === boot && preparation.mode === raw.boot && preparation.enabled === (raw.bootEnabled !== false);
            const storedResult = settings.extensions?.bootModesStartupResult;
            const currentResult = normalizeBootStartupResult(storedResult, boot);
            const oldDisabledClaim = raw.schema === 1 && raw.bootEnabled === false &&
                settings.extensions?.bootModesGlobalClaimBoot !== boot && settings.extensions?.bootModesActiveBoot !== boot &&
                !deferredThisBoot && !currentResult;
            if (raw.schema === 1 && settings.extensions?.bootModesAppliedBoot === boot && !oldDisabledClaim) {
                // An older global claimant can leave neither a result nor an active marker.
                // Record the uncertainty for this boot without replaying any side effects.
                if (!deferredThisBoot && raw.bootEnabled === false && settings.extensions.bootModesGlobalClaimBoot === boot &&
                    settings.extensions.bootModesActiveBoot !== boot && settings.input.startupObservedBoot === boot && storedResult === undefined) {
                    const document = readStoredBootMode(settings, savedOverviewSnapshot(settings));
                    settings.extensions.bootModesStartupResult = unconfirmedStartupClaim(boot, 'global', document.revision);
                    return true;
                }
                return false;
            }
            // Recheck at the original transaction boundary: an edit made while
            // detection was pending must not be replaced by an older default bank.
            if (raw.schema !== 1 && (!initialSchedule || !equalSettingsDocuments(settings.performanceSchedule, initialSource)))
                throw Error('性能配置已变化，初始开机配置未保存，请重新读取');
            const document = readStoredBootMode(settings, savedOverviewSnapshot(settings, raw.schema === 1 ? undefined : initialSchedule));
            if (raw.schema !== 1) {
                if (currentResult) document.revision = nextBootRevision(Math.max(document.revision, currentResult.revision, currentResult.sourceRevision));
            }
            const mode = startupBootMode(document);
            const writeId = crypto.randomUUID();
            const result = !currentResult && (storedResult === undefined || object(storedResult).schema === 1)
                ? unconfirmedStartupClaim(boot, mode, document.revision) : storedResult;
            attempt.value = { document: cloneBootValue(document), writeId, result: cloneBootValue(result),
                input: { revision: settings.input.revision, hash: computeInputConfigHash(settings.input) },
                owner: { boot, generation, mode, sourceRevision: document.revision, writeId,
                    revision: document.revision, committed: false, timing } };
            // Persist the claim and its diagnostic receipt together, before any scene writes.
            // Keep existing same-boot evidence and records owned by a newer schema.
            settings.extensions = { ...settings.extensions, bootModesAppliedBoot: boot, bootModesClaimWriteId: writeId,
                ...(mode === 'global' ? { bootModesGlobalClaimBoot: boot } : {}),
                ...(result !== undefined ? { bootModesStartupResult: result } : {}),
                ...(raw.schema !== 1 ? { [MODULE_KEY]: cloneBootValue(document) } : {}) };
            return true;
        };
        const claimStillCurrent = (value: UnifiedSettings, claim: StartupClaim): boolean =>
            belongsToBoot(value) && value.extensions?.bootModesAppliedBoot === boot &&
            value.extensions.bootModesClaimWriteId === claim.writeId &&
            value.input.revision === claim.input.revision && computeInputConfigHash(value.input) === claim.input.hash &&
            equalSettingsDocuments(readStoredBootMode(value, savedOverviewSnapshot(value)), claim.document);
        const readClaimSettings = async (label: string) => {
            const value = await readOnce(loadSettings, label);
            assertSettingsGeneration(generation); assertBootSettingsReady();
            return value;
        };
        let unacknowledged: StartupClaim | undefined;
        const recoverClaim = (value: UnifiedSettings, candidate?: StartupClaim): StartupClaim | undefined => {
            if (getSettingsRecoveryState().source !== 'main') return;
            const writeId = value.extensions?.bootModesClaimWriteId;
            if (!writeId) return;
            const recorded = candidate?.writeId === writeId ? candidate : unacknowledged?.writeId === writeId ? unacknowledged : undefined;
            if (!recorded) return;
            if (!claimStillCurrent(value, recorded) || value.extensions.bootModesActiveBoot === boot ||
                !equalSettingsDocuments(value.extensions.bootModesStartupResult, recorded.result))
                throw Error('开机领取后的配置已变化，本次未重放预设');
            return recorded;
        };
        // A conflict is still pre-apply; re-evaluate once on fresh settings.
        let snapshot = settings;
        for (let attempt = 0; attempt < 2; attempt++) {
            assertSettingsGeneration(generation); assertBootSettingsReady();
            if (!belongsToBoot(snapshot)) return;
            const candidate: { value?: StartupClaim } = {};
            let noChange = false;
            let accepted: boolean;
            try {
                accepted = await updateSettingsForInputReceipt({ revision: snapshot.input.revision, hash: computeInputConfigHash(snapshot.input) }, value => {
                    noChange = !prepareClaim(value, candidate);
                    return !noChange;
                }, value => {
                    assertSettingsGeneration(generation);
                    return !value || belongsToBoot(value);
                }, { origin: 'boot-mode' });
            } catch (error) {
                if (!candidate.value) throw error;
                // A lost acknowledgement is not proof of a failed write. Only
                // this request's durable write id can authorize continuation.
                const value = await readClaimSettings('开机领取回执回读');
                if (getSettingsRecoveryState().source !== 'main') throw error;
                if (!belongsToBoot(value)) return;
                claimed = recoverClaim(value, candidate.value);
                if (claimed) break;
                if (attempt === 1) throw error;
                unacknowledged = candidate.value;
                snapshot = value;
                continue;
            }
            if (accepted) { claimed = candidate.value; break; }
            // The first native write may finish during the retry. The retry's
            // no-op or CAS conflict cannot authorize a different claimant.
            if (unacknowledged) {
                const value = await readClaimSettings('开机领取回执回读');
                if (!belongsToBoot(value)) return;
                claimed = recoverClaim(value);
                if (claimed) break;
            }
            if (noChange) return;
            if (attempt === 1) throw Error('开机配置持续变化，本次未领取预设');
            snapshot = await readClaimSettings('开机领取冲突回读');
        }
        if (!claimed) return;
        const claim = claimed, { owner, document: claimedRaw } = claim;
        const startupPending: string[] = [];
        timing.mark('steam-startup');
        try {
            const requireClaim = async () => {
                const value = await readClaimSettings('开机启动路径所属配置');
                if (!claimStillCurrent(value, claim))
                    throw Error('开机模式已变化，旧启动路径处理已停止');
                return value;
            };
            // Repair retained logon commands as well as next-boot preparation.
            // The BAT checks the same intent before it can launch Steam.
            try {
                await requireClaim();
                const enabled = owner.mode === 'steam' || owner.mode === 'xbox-steam';
                const observed = await readOnce(readBootSteamStartup, 'Steam 开机路径');
                const settings = await requireClaim();
                let confirmed = false;
                try { assertSteamStartup(observed, enabled); confirmed = true; } catch { /* Repair the original entry below. */ }
                if (!confirmed) {
                    await steamMasterSet(enabled, true, { origin: 'boot-mode', bootContext: mouseBootContext(settings) });
                    await requireClaim();
                    assertSteamStartup(await readOnce(readBootSteamStartup, 'Steam 开机路径回读'), enabled);
                }
            } catch (error) { startupPending.push('Steam 开机路径：' + text(error)); }
            await requireClaim();
            // The claimed appearance is already saved intent. The apply
            // capture replaces the intermediate active snapshot before commit.
            const baseline = await captureBootModeSnapshot(undefined, stage => timing.mark('capture:' + stage), { skipAppearance: true }), document = normalizeBootModeDocument(claimedRaw, baseline);
            owner.revision = document.revision + 1;
            cancelBootModeDeferredMouse();
            await applyBootModeInQueue(owner.mode, document, deferredMouseEpoch, owner, startupPending);
            timing.mark('active-boot');
            const markActive = owner.markActive;
            delete owner.markActive;
            const activeBootMarked = await markActive?.();
            if (!activeBootMarked) throw Error('开机模式归属已变化，未确认本次启动状态');
            owner.activeBootMarked = true;
        } catch (error) {
            if (owner) {
                const completed = startupReceipt && startupOwners.get(startupReceipt) === owner ? startupReceipt : null;
                const failure = text(error) + timing.failure();
                const result: BootStartupResult = { schema: 1, boot, mode: owner.mode, sourceRevision: owner.sourceRevision,
                    revision: owner.committed ? owner.revision : owner.sourceRevision,
                    status: owner.committed ? 'partial' : 'failed', applied: [...completed?.applied ?? []], skipped: [...completed?.skipped ?? []],
                    pending: [...new Set([...startupPending, ...completed?.pending ?? [], failure])], error: failure,
                    ...(completed?.modules ? { modules: cloneBootValue(completed.modules) } : {}) };
                try {
                    if (await rememberStartupResult(result, owner) && typeof window !== 'undefined')
                        window.dispatchEvent(new CustomEvent('boot-mode:changed', { detail: cloneBootValue(result) }));
                } catch (recordError) { console.warn('开机失败结果保存未确认：', text(recordError)); }
            }
            throw error;
        }
    });
}
/** Retired mode hook. Fan startup and wake use the common startupDesired preference. */
export async function readBootModeFanIntent(_scope: 'startup' | 'current'): Promise<null> {
    return null;
}
export async function migrateBootTrayPreference(): Promise<void> {
    const generation = getSettingsGeneration(), settings = await readOnce(loadSettings, '开机模式设置');
    if (settings.extensions?.bootTrayMigration === 1)
        return;
    await setTrayResident(false);
    assertSettingsGeneration(generation);
    await updateSettings(value => { value.extensions = { ...value.extensions, bootTrayMigration: 1 }; });
}
export async function addBootSharedApplication(name: string, path: string): Promise<void> {
    const cleanName = name.trim(), cleanPath = path.trim();
    if (!cleanName || !cleanPath)
        throw Error('请输入应用名称和路径');
    if (cleanName.length > 120 || cleanPath.length > 2048 || !(/^(?:[a-z]:[\\/]|\\\\)/i.test(cleanPath)) || /[\r\n\0]/.test(cleanPath))
        throw Error('请选择有效的本地应用路径');
    await updateSettings(settings => {
        const apps = Array.isArray(settings.quickApps.apps) ? settings.quickApps.apps : [];
        if (!apps.some((item: any) => applicationKey(String(item.path)) === applicationKey(cleanPath)))
            settings.quickApps = { ...settings.quickApps, apps: [...apps, { id: crypto.randomUUID(), name: cleanName, path: cleanPath }] };
    });
}
