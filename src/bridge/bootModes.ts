/** Scenes replace the one global configuration; dedicated game profiles keep priority. */
import { loadSettings, updateSettings, compareAndSwapInputSettings, getSettingsGeneration, assertSettingsGeneration, type UnifiedSettings } from './settingsRepository';
import { cloneBootValue, normalizeBootConfig, normalizeBootModeDocument, normalizeBootStartupResult, isBootModeId, type BootStartupResult, resolveBootMode, executeBootModeSteps, BOOT_MODE_MODULES, type BootModeDocument, type BootModeId, type BootModeCategory, type BootModeSnapshot, type BootModeModule, type BootDeviceCapabilities, type BootApplyStep } from './bootModeModel';
import { normalizeContinuousControls } from './performanceControls';
import { continuousControlsFromSchedule, snapshotPerformanceSchedule, loadPerformanceSchedule, savePerformanceSchedule, restorePerformanceScheduleIfConfigured } from './performanceSchedule';
import { frameRatePair } from './frameRateModel';
import { normalizeMonitorOutput } from './monitorHudModel';
import { readMonitorOutputPreferences, saveMonitorOutputPreferences, monitorOutputCoordinator } from './monitorOutput';
import { screenTouchpadsGet, screenTouchpadsSet, screenTouchpadDefaults, type ScreenTouchpadPersona, type ScreenTouchpadConfig } from './screenTouchpads';
import { NATIVE_DEFAULT_SHORTCUTS, reconcileControllerShortcutRules, readControllerShortcutRules } from './controllerShortcutRules';
import { getMouseModeState, setMouseModeEnabled, setMouseModePolicy, setMouseBackend, type MouseModeState } from './gameproc';
import { batteryCharge } from './batteryCharge';
import { steamUi, type SteamUiState } from './steamUi';
import { steamAppearance } from './steamAppearance';
import { deckySidebar } from './deckySidebar';
import { getUiSettings, loadUiSettings, setUiSettings, type UiSettings } from './uiSettings';
import { applyTheme } from './theme';
import { setTrayResident } from './trayResident';
import { shell, display, proc, fs, registry, wakePassword } from './api';
import { getFanPresetCurve, saveFanPresetSelection } from './fanFeature';
import { fanHostLifecycle } from './fanHost';
import { getAutomaticSleepRecovery, setAutomaticSleepRecovery } from './sleepRecovery';
import { normalizeInputStartupPreferences } from './inputStartupPolicy';
import { readBootDisplay, applyBootBrightness } from './bootModePlatform';
import { invoke, on } from './ipc';
import {fxSound} from './fxSound';
import { summonSet, readGamingHomeConfiguration, writeGamingHomeStartup, steamMasterSet, joySet, steamAddonExists, steamAddonSet, STEAM_ADDONS, fxExists, fxSet, readHibernateState, readHibernateSize, readSleepTimeouts, setHibernate, setHibernateSize, setSleepTimeout, getPowerBtnIdx, setPowerBtnIdx, sleepGuardGet, sleepGuardSetConfig, getTdpExecutionCapability, detectCoreArchitecture, getSleepPowerPlanOptimizationEnabled, setSleepPowerPlanOptimizationEnabled, reactivateCurrentScheme, type PowerBtnIdx } from './yeman';
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
 * it settles, so manual retry cannot accumulate requests against a stuck owner.
 * Writes deliberately never use this helper: a timeout is not cancellation. */
async function readOnce<T>(read: () => Promise<T>, label: string): Promise<T> {
    const generation = getSettingsGeneration();
    if (readGeneration !== generation) { pendingReads.clear(); readGeneration = generation; }
    let pending = pendingReads.get(label) as Promise<T> | undefined;
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
}
let queue: Promise<unknown> = Promise.resolve();
const serialized = <T>(run: () => Promise<T>): Promise<T> => { const result = queue.then(run); queue = result.catch(() => { }); return result; };
const LABELS: Record<BootModeModule, string> = { performance: '性能调度', fan: '风扇', charge: '充电限制', input: '模拟鼠标', gyro: '陀螺仪', controller: '手柄自定义', touch: '屏幕触摸配置', audioOptimization: '掌机音质优化', frameGeneration: '小黄鸭联动', softwareLaunch: '快捷应用联动', steamAppearance: 'Steam 美化', steamWallpaper: 'Steam 动态壁纸', steamDecky: 'Decky', monitor: '监控', display: '画面', background: 'YMCC 背景', sleep: '睡眠优化' };
const BACKGROUND_KEYS = ['theme', 'dynamicBackgroundEnabled', 'backgroundOpacity', 'backgroundBlur', 'videoBatteryPause'] as const;
const backgroundSettings = (value: UiSettings) => Object.fromEntries(BACKGROUND_KEYS.map(key => [key, value[key]]));
const TOUCH_KEYS = Object.keys(screenTouchpadDefaults('disabled')) as Array<keyof ScreenTouchpadConfig>;
function touchConfig(value: Partial<ScreenTouchpadConfig>) { return Object.fromEntries(TOUCH_KEYS.filter(key => value[key] !== undefined).map(key => [key, value[key]])); }
interface BootModeSharedReads {
    charge: ReturnType<typeof optional<Awaited<ReturnType<typeof batteryCharge.get>>>>;
    hibernate: ReturnType<typeof optional<Awaited<ReturnType<typeof readHibernateState>>>>;
}
function sharedBootModeReads(): BootModeSharedReads {
    return { charge: optional(() => batteryCharge.get(), '充电限制状态'), hibernate: optional(readHibernateState, '系统休眠能力') };
}
export async function captureBootModeSnapshot(shared?: BootModeSharedReads): Promise<BootModeSnapshot> {
    const generation = getSettingsGeneration();
    const settings = await readOnce(loadSettings, '开机模式设置');
    const [config] = await Promise.all([readOnce(loadPerformanceSchedule, '性能调度设置'), readOnce(loadUiSettings, '界面设置')]);
    const [mouse, charge, monitor, hibernate, size, timeouts, acButton, dcButton, recovery, fx, scaling, appearance, decky, sleep, plan, screen, profiles] = await Promise.all([
        optional(getMouseModeState, '模拟鼠标状态'), shared?.charge ?? optional(() => batteryCharge.get(), '充电限制状态'), readOnce(readMonitorOutputPreferences, '监控设置'), shared?.hibernate ?? optional(readHibernateState, '系统休眠能力'), optional(readHibernateSize, '休眠文件大小'), optional(readSleepTimeouts, '睡眠时间'), optional(() => getPowerBtnIdx(true), '交流电电源按钮'), optional(() => getPowerBtnIdx(false), '电池电源按钮'), optional(getAutomaticSleepRecovery, 'Steam 睡眠恢复设置'), optional(fxExists, '掌机音质优化'), optional(() => steamAddonExists('steamscale'), '小黄鸭联动设置'), optional(() => steamAppearance.read(), 'Steam 美化设置'), optional(() => deckySidebar.get(), 'Decky 状态'), optional(sleepGuardGet, '睡眠优化设置'), optional(getSleepPowerPlanOptimizationEnabled, '睡眠电源优化'), optional(readBootDisplay, '画面模式'),
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
    const rules = reconcileControllerShortcutRules(readControllerShortcutRules(input.buttonMapping?.rules), settings.gamepad).rules;
    // A failed read stays unknown. It must not become a fabricated OFF value later applied to hardware.
    return cloneBootValue({
        performance: { schedule: config, controls: continuousControlsFromSchedule(config), frames: frameRatePair(settings.rtss.frameRates) },
        fan: { enabled: fanHostLifecycle.hasControlIntent, preset: settings.fan.preset || 'balanced' },
        charge: { enabled: charge ? charge.managed === true : null, limit: charge?.requestedLimit ?? charge?.currentLimit ?? null },
        input: { persona: input.outputTarget.persona, outputTarget: input.outputTarget, ownership: input.ownership, mouseBackend: baseBackend ?? null, mousePolicy: basePolicy ?? null, mouse: baseMouse },
        gyro: { motion: input.gyroMotion }, controller: { name: '当前手柄规则', rules },
        touch: { profiles: Object.fromEntries(profiles.filter(([, state]) => state).map(([persona, state]) => [persona, touchConfig(state!)])) },
        audioOptimization: { enabled: fx }, frameGeneration: { enabled: scaling }, softwareLaunch: { paths: [], timing: 'scene', delay: 0 },
        steamAppearance: { states: appearance ? Object.fromEntries(appearance.themes.map(item => [item.name, item.configured])) : {} },
        steamWallpaper: { enabled: getUiSettings().steamDynamicWallpaperEnabled, download: getUiSettings().steamWallpaperBackgroundDownload, cpuThreshold: getUiSettings().steamWallpaperDownloadCpuThreshold },
        steamDecky: { enabled: decky?.enabled ?? null }, monitor: { preferences: monitor },
        display: screen ?? { brightness: null, resolution: 'native', hz: 'native' }, background: { ui: backgroundSettings(getUiSettings()) },
        sleep: { config: { ...(sleep ?? { ...settings.sleep }), sleepPowerPlanOptimizationEnabled: plan }, acButton, dcButton, hibernate: hibernate?.enabledKnown ? hibernate.enabled : null, hibernateSize: size, timeouts, steamAutoRecovery: recovery?.ok ? recovery.enabled : null },
    } as unknown as BootModeSnapshot);
}
export async function readBootModeSupport(category?: BootModeCategory, shared?: BootModeSharedReads): Promise<BootDeviceCapabilities> {
    const generation = getSettingsGeneration();
    const performance = !category || category === 'performance', sleep = !category || category === 'sleep';
    const [core, tdp, charge, hib] = await Promise.all([
        performance ? optional(detectCoreArchitecture, 'CPU 核心架构') : null,
        performance ? optional(getTdpExecutionCapability, '性能调度能力') : null,
        performance ? shared?.charge ?? optional(() => batteryCharge.get(), '充电限制状态') : null,
        sleep ? shared?.hibernate ?? optional(readHibernateState, '系统休眠能力') : null,
    ]);
    assertSettingsGeneration(generation);
    return {
        ...(performance ? {
            hybridCores: { state: core?.detected ? (core.heterogeneous ? 'supported' : 'unsupported') : 'unknown', reason: core?.detected ? '按本机 CPU 架构判断' : '尚未确认 CPU 架构' },
            performance: { state: tdp?.ready ? 'supported' : 'unknown', reason: tdp?.error || '实际功耗范围由原性能调度接口校验' },
            charge: { state: charge?.supported ? 'supported' : charge?.kind === 'unsupported' ? 'unsupported' : 'unknown', reason: charge?.reason || '充电能力尚未确认', choices: charge?.choices },
            fan: { state: fanHostLifecycle.controlReady ? 'supported' : 'unknown', reason: '以本机风扇握手和写入能力为准' },
        } : {}),
        ...(!category || category === 'input' ? { gyro: { state: 'unknown', reason: '启用时由原输入接口确认设备与体感来源' } } : {}),
        ...(sleep ? { hibernate: { state: hib?.supportedKnown ? (hib.supported ? 'supported' : 'unsupported') : 'unknown', reason: '以系统 S4 支持状态为准' } } : {}),
    };
}
export function readStoredBootMode(settings: UnifiedSettings, baseline: BootModeSnapshot): BootModeDocument { return normalizeBootModeDocument(settings.extensions?.[MODULE_KEY], baseline); }
/** Overview reads durable preferences only. Unknown device values remain null;
 * this view must never substitute for the editor's live capture. */
function savedOverviewSnapshot(settings: UnifiedSettings): BootModeSnapshot {
    const input = settings.input, gamepad = settings.gamepad;
    const schedule = snapshotPerformanceSchedule(settings.performanceSchedule as any);
    const ui = { ...getUiSettings(), ...object(settings.ui) };
    const mouse = input.outputTarget.persona === 'steamdeck' ? 'steamdeck' : gamepad.mouseMode === 'off' ? 'off'
        : gamepad.mouseMode === 'on' ? gamepad.mouseBackend === 'gamebar' ? 'microsoft' : 'joyxoff' : null;
    return cloneBootValue({
        performance: { schedule, controls: continuousControlsFromSchedule(schedule), frames: frameRatePair(settings.rtss.frameRates) },
        fan: { enabled: null, preset: settings.fan.preset || 'balanced' },
        charge: { enabled: null, limit: null },
        input: { persona: input.outputTarget.persona, outputTarget: input.outputTarget, ownership: input.ownership,
            mouseBackend: gamepad.mouseBackend, mousePolicy: gamepad.mouseMode, mouse },
        gyro: { motion: input.gyroMotion },
        controller: { name: '当前手柄规则', rules: reconcileControllerShortcutRules(readControllerShortcutRules(input.buttonMapping?.rules), gamepad).rules },
        touch: { profiles: object(settings.screenTouchpads).profiles || {} },
        audioOptimization: { enabled: null }, frameGeneration: { enabled: null },
        softwareLaunch: { paths: [], timing: 'scene', delay: 0 }, steamAppearance: { states: {} },
        steamWallpaper: { enabled: ui.steamDynamicWallpaperEnabled, download: ui.steamWallpaperBackgroundDownload, cpuThreshold: ui.steamWallpaperDownloadCpuThreshold },
        steamDecky: { enabled: null }, monitor: { preferences: normalizeMonitorOutput(settings.rtss.monitorOutput) },
        display: { brightness: null, resolution: 'native', hz: 'native' }, background: { ui: backgroundSettings(ui) },
        sleep: { config: settings.sleep, acButton: null, dcButton: null, hibernate: null, hibernateSize: null, timeouts: null, steamAutoRecovery: null },
    } as unknown as BootModeSnapshot);
}
function reconcileBootModeSnapshot(document: BootModeDocument, settings: UnifiedSettings, baseline: BootModeSnapshot, observable: readonly BootModeModule[]) {
    const desired = resolveBootMode(document, document.current);
    const storedActive = object(object(settings.extensions?.[MODULE_KEY]).activeSnapshot);
    for (const module of observable) {
        // Reconcile only actual saved applied modules; a pending scene is not current state.
        if (!Object.keys(object(storedActive[module])).length)
            continue;
        if (module === 'input' && deferredMouse)
            continue;
        if (!same(desired[module], document.activeSnapshot[module]))
            continue;
        if (module === 'input' && baseline.input.mouse === null)
            baseline.input.mouse = desired.input.mouse;
        if (document.current === 'global' || !document.scenes[document.current].overrides[module])
            document.global[module] = cloneBootValue(baseline[module]);
        else
            document.scenes[document.current].overrides[module] = cloneBootValue(baseline[module]);
        document.activeSnapshot[module] = cloneBootValue(baseline[module]);
    }
}
export async function loadBootModes(options: { overviewOnly?: boolean; editorOnly?: boolean } = {}): Promise<BootModeContext> {
    const generation = getSettingsGeneration();
    // Browsing/editing presets reads saved data only. Runtime restore and an
    // explicit first startup selection retain the complete observation path.
    if (options.overviewOnly || options.editorOnly) {
        const settings = await readOnce(loadSettings, '开机模式设置'), raw = object(settings.extensions?.[MODULE_KEY]);
        const global = object(raw.global);
        const complete = raw.schema === 1 && BOOT_MODE_MODULES.every(module => global[module] && typeof global[module] === 'object' && !Array.isArray(global[module]));
        const baseline = complete
            ? Object.fromEntries(BOOT_MODE_MODULES.map(module => [module, normalizeBootConfig(global[module])])) as BootModeSnapshot
            : savedOverviewSnapshot(settings);
        const document = readStoredBootMode(settings, baseline);
        if (options.editorOnly) {
            const observed = savedOverviewSnapshot(settings);
            reconcileBootModeSnapshot(document, settings, observed, ['performance', 'input', 'gyro', 'controller', 'monitor', 'background', 'steamWallpaper']);
        }
        return { document, requiresCapture: !complete,
            startupResult: normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, object(settings.input).startupObservedBoot),
            baseline: cloneBootValue(document.global), support: {}, apps: Array.isArray(settings.quickApps.apps) ? cloneBootValue(settings.quickApps.apps) : [] };
    }
    return serialized(async () => {
        const shared = sharedBootModeReads();
        const [baseline, support] = await Promise.all([captureBootModeSnapshot(shared), readBootModeSupport(undefined, shared)]);
        const settings = await readOnce(loadSettings, '开机模式设置');
        assertSettingsGeneration(generation);
        const document = readStoredBootMode(settings, baseline);
        reconcileBootModeSnapshot(document, settings, baseline, ['performance', 'input', 'gyro', 'controller', 'touch', 'monitor', 'background', 'steamWallpaper']);
        return { document, startupResult: normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, object(settings.input).startupObservedBoot), baseline: cloneBootValue(document.global), support, apps: Array.isArray(settings.quickApps.apps) ? cloneBootValue(settings.quickApps.apps) : [] };
    });
}
export function saveBootModes(document: BootModeDocument, expectedRevision = document.revision): Promise<BootModeDocument> {
    return serialized(async () => {
        const generation = getSettingsGeneration();
        const boot = await optional(() => invoke<string>('bootMode.startup.identity', {}, { timeoutMs: 3000 }), 'Windows 启动标识');
        let saved!: BootModeDocument;
        await updateSettings(settings => {
            assertSettingsGeneration(generation);
            const current = readStoredBootMode(settings, document.activeSnapshot ?? document.global);
            if (current.revision !== expectedRevision)
                throw Error('开机模式已被其他窗口修改，请重新读取');
            saved = normalizeBootModeDocument(document, document.global);
            saved.revision = current.revision + 1;
            saved.activeSnapshot = cloneBootValue(current.activeSnapshot);
            saved.current = current.current;
            saved.boot = current.boot;
            saved.bootEnabled = current.bootEnabled;
            saved.bootNotice = current.bootNotice;
            // An explicit draft save also defers a not-yet-started automatic
            // restore. It must never adopt this newly edited template now.
            settings.extensions = { ...settings.extensions, [MODULE_KEY]: saved,
                ...(typeof boot === 'string' && boot ? { bootModesAppliedBoot: boot } : {}) };
            syncStartupInput(settings, saved);
        });
        return cloneBootValue(saved);
    });
}
function syncStartupInput(settings: UnifiedSettings, document: BootModeDocument) {
    const snapshot = resolveBootMode(document, document.boot);
    const preferences = normalizeInputStartupPreferences({ virtualGamepadPersona: document.bootEnabled ? snapshot.input.persona : 'disabled', gyroPreset: document.bootEnabled && object(snapshot.gyro.motion).enabled ? (object(snapshot.gyro.motion).activePreset ?? object(snapshot.gyro.motion).preset ?? 'custom') : 'off' });
    settings.startupDesired = { ...settings.startupDesired, ...preferences };
}
/** Boot preparation repairs stored settings; no live Xbox/Steam connection is a prerequisite. */
export async function bootModeConnections(_id: BootModeId): Promise<Array<{ label: string; connected: boolean; detail?: string }>> {
    return [];
}
/** Setting next boot never applies runtime settings, changes mouse ownership or launches applications. */
export function chooseBootMode(id: BootModeId, document: BootModeDocument, enabled = true): Promise<BootModeDocument> {
    return serialized(async () => {
        const generation = getSettingsGeneration();
        let copy = cloneBootValue(document);
        const boot = await optional(() => invoke<string>('bootMode.startup.identity', {}, { timeoutMs: 3000 }), 'Windows 启动标识');
        copy.boot = id;
        copy.bootEnabled = enabled;
        delete copy.bootNotice;
        await updateSettings(settings => {
            const latest = readStoredBootMode(settings, copy.global);
            if (latest.revision !== document.revision) {
                const startup = normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, boot);
                const requested = normalizeBootModeDocument(document, document.global);
                const sameTemplates = ['global', 'scenes', 'presets', 'controllerTemplates'].every(key =>
                    same((latest as any)[key], (requested as any)[key]));
                if (!startup || startup.status === 'failed' || startup.sourceRevision !== document.revision ||
                    startup.revision !== latest.revision || latest.current !== startup.mode ||
                    latest.boot !== document.boot || latest.bootEnabled !== document.bootEnabled || !sameTemplates)
                    throw Error('开机模式配置已变化');
                copy = cloneBootValue(latest);
                copy.boot = id; copy.bootEnabled = enabled; delete copy.bootNotice;
            }
            copy.current = latest.current;
            copy.revision = latest.revision + 1;
            // Selection only schedules the next Windows boot, including when the
            // page is used before startup restoration has completed.
            settings.extensions = { ...settings.extensions, [MODULE_KEY]: copy,
                ...(typeof boot === 'string' && boot ? { bootModesAppliedBoot: boot } : {}) };
            syncStartupInput(settings, copy);
        });
        const pending: string[] = [];
        const isCurrent = () => getSettingsGeneration() === generation;
        const writes: Array<[
            string,
            () => Promise<unknown>
        ]> = id === 'global' ? [] : [
            ['旧模拟鼠标开机入口', async () => {
                    const key = 'Software\\Microsoft\\Windows\\CurrentVersion\\Run';
                    const legacy = await registry.read('HKCU', key, 'Joyxoff');
                    if (typeof legacy === 'string' && /joyxoff\.exe(?:["\s]|$)/i.test(legacy)) {
                        await joySet(false);
                        const remaining = await registry.read('HKCU', key, 'Joyxoff');
                        if (remaining !== null && remaining !== undefined && remaining !== '')
                            throw Error('JoyXoff 开机入口尚未确认关闭');
                    }
                }],
            // All fixed non-global modes repair handheld identity. Only 2/4
            // enable Xbox's Windows startup flag; 3 uses advanced Steam instead.
            ['Windows 开机配置', () => writeGamingHomeStartup(enabled && (id === 'xbox' || id === 'xbox-steam'), enabled)],
            ['Steam 高级启动', () => steamMasterSet(enabled && (id === 'steam' || id === 'xbox-steam'), enabled)],
            ['音质优化开机联动', () => fxSet(false)],
            ['小黄鸭开机联动', () => steamAddonSet('steamscale', false)],
        ];
        for (const [label, write] of writes) {
            assertSettingsGeneration(generation);
            try {
                if (await write() === false)
                    pending.push(label + '待确认');
            }
            catch (error) {
                pending.push(label + '：' + text(error));
            }
        }
        if (pending.length) {
            copy.bootNotice = pending.join('；');
            await updateSettings(settings => {
                if (!isCurrent())
                    throw Error('设置位置已变化');
                const latest = readStoredBootMode(settings, copy.global);
                if (latest.revision !== copy.revision)
                    throw Error('启动配置已变化，请重新读取');
                settings.extensions = { ...settings.extensions, [MODULE_KEY]: copy };
            });
        }
        return copy;
    });
}
class BootMouseUnavailable extends Error {
}
function assertMouseState(state: MouseModeState, enabled: boolean) {
    if (state.controlAllowed === false)
        throw new BootMouseUnavailable(state.error || state.reason || '鼠标提供程序尚未连接');
    if (!state.ok || state.stateKnown === false || state.on !== enabled || state.pending)
        throw Error(state.error || state.reason || '模拟鼠标状态未确认');
}
async function stopMouse(isCurrent: () => boolean = () => true) {
    const state = await readOnce(getMouseModeState, '模拟鼠标状态');
    if (state.controlAllowed === false)
        throw new BootMouseUnavailable(state.reason || '模拟鼠标提供程序尚未连接');
    if (state.stateKnown === false)
        throw Error('原鼠标模拟状态未知，请重新检测');
    if (!isCurrent())
        throw Error('模拟鼠标所属模式已变化');
    if (state.on || state.mode !== 'off')
        assertMouseState(await setMouseModeEnabled(false), false);
}
async function applyMouse(value: any, isCurrent: () => boolean = () => true) {
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
    if (!isCurrent())
        throw Error('模拟鼠标所属模式已变化');
    if (target === 'off')
        return;
    const backendName = target === 'microsoft' ? 'gamebar' : 'joyxoff';
    const backend = await setMouseBackend(backendName);
    if (backend.controlAllowed === false)
        throw new BootMouseUnavailable(backend.error || backend.reason || '鼠标提供程序尚未连接');
    if (!backend.ok || backend.backend !== backendName)
        throw Error(backend.error || '模拟鼠标软件切换未确认');
    if (!isCurrent())
        throw Error('模拟鼠标所属模式已变化');
    assertMouseState(await setMouseModeEnabled(true), true);
}
async function restoreMouse(value: any) {
    const settings = await readOnce(loadSettings, '开机模式设置');
    if (settings.input.outputTarget.persona !== value.persona)
        throw Error('模拟鼠标所属输入配置已变化，未恢复旧状态');
    if (value.persona === 'steamdeck')
        return;
    await stopMouse();
    if (['gamebar', 'joyxoff'].includes(value.mouseBackend)) {
        const backend = await setMouseBackend(value.mouseBackend);
        if (backend.controlAllowed === false)
            throw new BootMouseUnavailable(backend.error || backend.reason || '原鼠标提供程序尚未连接');
        if (!backend.ok || backend.backend !== value.mouseBackend)
            throw Error('原模拟鼠标软件恢复未确认');
    }
    if (value.mousePolicy === 'auto') {
        const result = await setMouseModePolicy('auto');
        if (result.controlAllowed === false)
            throw new BootMouseUnavailable(result.error || result.reason || '原鼠标提供程序尚未连接');
        if (!result.ok || result.mode !== 'auto' || result.stateKnown === false)
            throw Error('原自动鼠标策略恢复未确认');
    }
    else
        await applyMouse(value);
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
                await applyMouse(input, current);
                if (!current())
                    return;
                await updateSettings(value => {
                    if (!current())
                        return;
                    const latest = readStoredBootMode(value, baseline);
                    if (latest.current !== request.mode || value.input.outputTarget.persona !== input.persona || latest.activeSnapshot.input.persona !== input.persona)
                        return;
                    latest.activeSnapshot.input = cloneBootValue(input);
                    value.extensions = { ...value.extensions, [MODULE_KEY]: latest };
                });
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
    const input = object(snapshot.input), rawMotion = object(object(snapshot.gyro).motion), motion = { ...rawMotion, ...(activate && rawMotion.enabled === true ? { outputMode: 'virtual-stick', unit: 'deg/s' } : {}) }, controller = object(snapshot.controller);
    return { outputTarget: { ...object(input.outputTarget), ...(activate ? { persona: input.persona, buttonMappingEnabled: input.persona !== 'disabled', gyroEnabled: rawMotion.enabled === true } : {}) }, ownership: input.ownership, gyroMotion: motion,
        buttonMapping: { ...settings.input.buttonMapping, rules: { ...object(settings.input.buttonMapping?.rules), schemaVersion: 2, closure: 'UNENCLOSED', shortcutRules: controller.rules } } };
}
async function writeBaseInput(snapshot: BootModeSnapshot, expected: number, isCurrent: () => boolean): Promise<number> {
    const settings = await readOnce(loadSettings, '开机模式设置');
    const result = await compareAndSwapInputSettings(expected, inputPatch(snapshot, settings), isCurrent);
    if (result.ok === false)
        throw Error('全局输入配置冲突或写入未确认：' + result.reason);
    return result.value.revision;
}
async function applySleep(value: any, before: any, receipt: BootModeReceipt) {
    const cfg = object(value.config), old = object(before.config);
    const guardKeys = ['mode', 'pauseGameOnSleep', 'retryOnEntryFailure', 'retryOnNonUserWake'] as const;
    const guard = Object.fromEntries(guardKeys.filter(key => cfg[key] !== undefined).map(key => [key, cfg[key]]));
    if (guardKeys.some(key => !same(cfg[key], old[key])))
        await sleepGuardSetConfig(guard);
    if (typeof cfg.wakePasswordEnabled === 'boolean' && cfg.wakePasswordEnabled !== old.wakePasswordEnabled) {
        const result = await wakePassword.set(cfg.wakePasswordEnabled);
        if (!result.ok || !result.known || result.enabled !== cfg.wakePasswordEnabled)
            throw Error('唤醒密码设置未确认');
    }
    if (typeof cfg.sleepPowerPlanOptimizationEnabled === 'boolean' && cfg.sleepPowerPlanOptimizationEnabled !== old.sleepPowerPlanOptimizationEnabled)
        await setSleepPowerPlanOptimizationEnabled(cfg.sleepPowerPlanOptimizationEnabled);
    let powerChanged = false;
    for (const side of ['ac', 'dc'] as const) {
        const key = side + 'Button', idx = value[key];
        if (Number.isInteger(idx) && idx >= 0 && idx <= 3 && idx !== before[key]) {
            await setPowerBtnIdx(side === 'ac', idx as PowerBtnIdx);
            if (await readOnce(() => getPowerBtnIdx(side === 'ac'), side === 'ac' ? '交流电电源按钮' : '电池电源按钮') !== idx)
                throw Error('电源按钮回读不一致');
            powerChanged = true;
        }
    }
    const hibernateState = await readOnce(readHibernateState, '系统休眠能力');
    const canHibernate = hibernateState.supportedKnown && hibernateState.supported;
    const hibernateChanged = value.hibernate !== before.hibernate || (value.hibernate === true && (value.hibernateSize !== before.hibernateSize || ['acHibernate', 'dcHibernate'].some(key => value.timeouts?.[key] !== before.timeouts?.[key])));
    if (!canHibernate && hibernateChanged)
        receipt.skipped.push('系统 S4 休眠：本机未确认支持');
    if (typeof value.hibernate === 'boolean' && value.hibernate !== before.hibernate) {
        if (canHibernate) {
            const result = await setHibernate(value.hibernate);
            if (result.exitCode !== 0)
                throw Error('系统休眠设置失败');
            const confirmed = await readOnce(readHibernateState, '系统休眠能力');
            if (!confirmed.enabledKnown || confirmed.enabled !== value.hibernate)
                throw Error('系统休眠回读未确认');
        }
    }
    if (canHibernate && value.hibernate === true && typeof value.hibernateSize === 'number' && value.hibernateSize !== before.hibernateSize) {
        await setHibernateSize(value.hibernateSize);
        if (await readOnce(readHibernateSize, '休眠文件大小') !== value.hibernateSize)
            throw Error('休眠文件大小回读不一致');
    }
    for (const side of ['ac', 'dc'])
        for (const kind of ['Screen', 'Sleep', 'Hibernate']) {
            const key = side + kind, minutes = value.timeouts?.[key];
            if (typeof minutes === 'number' && minutes >= 0 && minutes !== before.timeouts?.[key] && (kind !== 'Hibernate' || (canHibernate && value.hibernate === true))) {
                await setSleepTimeout(kind.toLowerCase() as 'screen' | 'sleep' | 'hibernate', side === 'ac', minutes);
                powerChanged = true;
            }
        }
    if (powerChanged)
        await reactivateCurrentScheme();
    if (typeof value.steamAutoRecovery === 'boolean' && value.steamAutoRecovery !== before.steamAutoRecovery) {
        const result = await setAutomaticSleepRecovery(value.steamAutoRecovery);
        if (!result.ok || result.enabled !== value.steamAutoRecovery)
            throw Error('Steam 自动恢复设置未确认');
    }
}
async function applyRuntimeModule(id: BootModeModule, value: any, before: any, receipt: BootModeReceipt): Promise<void> {
    const pendingBefore = receipt.pending.length, skippedBefore = receipt.skipped.length;
    if (id === 'performance') {
        const config = { ...value.schedule, controls: normalizeContinuousControls(value.controls), configured: true, enabled: true };
        await savePerformanceSchedule(config);
        await updateSettings(settings => { settings.rtss = { ...settings.rtss, frameRates: frameRatePair(value.frames), templateDefaults: false }; });
        await restorePerformanceScheduleIfConfigured();
    }
    else if (id === 'touch') {
        for (const persona of PERSONAS) {
            const config = value.profiles?.[persona];
            if (config && !same(config, before.profiles?.[persona])) {
                const result = await screenTouchpadsSet(touchConfig(config), persona);
                if (!result.ok && !result.settingsSaved)
                    throw Error('屏幕触摸配置未保存');
                if (result.settingsSaved && !result.ok)
                    receipt.pending.push('屏幕触摸：配置已保存，输入生效待确认');
            }
        }
    }
    else if (id === 'audioOptimization' || id === 'frameGeneration') {
        // These are scene launch selections. No unconditional OS startup link and no game-specific LS profile is rewritten.
        if (value.enabled === true) {
            if(id==='audioOptimization'){
                const result=await fxSound.setEnabled(true);if(!result.ok)receipt.pending.push('音质优化：'+result.reason);
            }else await launchOne(STEAM_ADDONS.find(app => app.key === 'steamscale')!.exe, receipt);
        }
    }
    else if (id === 'steamAppearance') {
        if (Object.keys(value.states || {}).length) {
            const state = await readOnce(() => steamAppearance.read(), 'Steam 美化设置'), result = await steamAppearance.apply(state, value.states);
            if (!result.ok)
                receipt.pending.push('Steam 美化：' + (result.message || '等待连接'));
        }
    }
    else if (id === 'steamWallpaper')
        await setUiSettings({ steamDynamicWallpaperEnabled: value.enabled, steamWallpaperBackgroundDownload: value.download, steamWallpaperDownloadCpuThreshold: Number(value.cpuThreshold) });
    else if (id === 'background') {
        const patch = Object.fromEntries(BACKGROUND_KEYS.filter(key => value.ui?.[key] !== undefined).map(key => [key, value.ui[key]]));
        await setUiSettings(patch);
        if (patch.theme)
            applyTheme(patch.theme as UiSettings['theme']);
    }
    else if (id === 'monitor') {
        const prefs = normalizeMonitorOutput(value.preferences);
        await saveMonitorOutputPreferences(prefs);
        const result = await monitorOutputCoordinator.apply(prefs);
        if (!result.ok)
            receipt.pending.push('监控：' + result.reason);
    }
    else if (id === 'display') {
        if (typeof value.brightness === 'number' && value.brightness !== before.brightness)
            await applyBootBrightness(value.brightness);
        if (value.resolution !== before.resolution || value.hz !== before.hz) {
            const available = await readOnce(() => display.getModes(), '可用显示模式'), active = available.modes.find(item => item.id === available.current);
            const mode = available.modes.find(item => (value.resolution === 'native' ? active && item.width === active.width && item.height === active.height : item.width + 'x' + item.height === value.resolution) && (value.hz === 'native' ? active && item.refresh === active.refresh : item.refresh === Number(value.hz)));
            if (!mode)
                receipt.skipped.push('显示模式：本机不支持该分辨率或刷新率');
            else {
                const result = await display.setMode(mode);
                if (result.id !== mode.id)
                    throw Error('显示模式回读未确认');
            }
        }
    }
    else if (id === 'fan') {
        const preset = ['soft', 'balanced', 'aggressive'].includes(value.preset) ? value.preset : 'balanced';
        const settings = await readOnce(loadSettings, '开机模式设置');
        // The original selector owns all per-preset curves and synchronizes the live feature store.
        await saveFanPresetSelection(preset, settings.fan);
        if (value.enabled === true) {
            const gate = await fanHostLifecycle.start();
            if (!gate.allowed || !gate.writeReady) {
                receipt.skipped.push('风扇：本机未确认支持');
                return;
            }
            await fanHostLifecycle.applyPreset(preset, getFanPresetCurve(preset));
        }
        else if (fanHostLifecycle.hasControlIntent || fanHostLifecycle.controlReady)
            await fanHostLifecycle.close();
    }
    else if (id === 'charge') {
        if (typeof value.enabled !== 'boolean') {
            receipt.skipped.push('充电限制：预设能力尚未确认');
            return;
        }
        const state = await readOnce(() => batteryCharge.get(), '充电限制状态');
        if (value.enabled) {
            if (!state.supported || !state.choices.includes(Number(value.limit))) {
                receipt.skipped.push('充电限制：本机或档位不支持');
                return;
            }
            const result = await batteryCharge.set(state.fence, Number(value.limit));
            if (!result.ok)
                throw Error(result.reason || '充电上限未确认');
        }
        else if (state.managed) {
            const result = await batteryCharge.release();
            if (!result.ok)
                throw Error(result.reason || '充电限制释放未确认');
        }
    }
    else if (id === 'sleep')
        await applySleep(value, before, receipt);
    else if (id === 'steamDecky') {
        if (typeof value.enabled !== 'boolean') {
            receipt.skipped.push('Decky：连接状态尚未确认');
            return;
        }
        const state = await deckySidebar.setEnabled(value.enabled);
        if (state.enabled !== value.enabled)
            throw Error('Decky 开关未确认');
        if (value.enabled && !(state.phase === 'loader-started' && state.mirrorConnected && state.powerReady !== false))
            receipt.pending.push('Decky：已启用，等待 Steam 连接');
    }
    if (receipt.pending.length === pendingBefore && receipt.skipped.length === skippedBefore)
        receipt.applied.push(LABELS[id]);
}
async function launchOne(path: string, receipt: BootModeReceipt, isCurrent: () => boolean = () => true, beforeExecute?: () => Promise<boolean>) {
    if (!isCurrent())
        return;
    if (!await readOnce(() => fs.exists(path), '应用文件 ' + path)) {
        receipt.skipped.push('未安装：' + path.replace(/^.*[\\/]/, ''));
        return;
    }
    if ((await readOnce(() => proc.findExact(path), '应用进程 ' + path)).found)
        return;
    if (beforeExecute && !await beforeExecute()) {
        receipt.skipped.push('应用已从全局快捷库删除');
        return;
    }
    if (!isCurrent())
        return;
    if (!await shell.execute(path, []))
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
async function scheduleLaunches(snapshot: BootModeSnapshot, receipt: BootModeReceipt, generation: number) {
    cancelBootModeLaunches();
    const epoch = launchEpoch, launch = object(snapshot.softwareLaunch), isCurrent = () => epoch === launchEpoch && generation === getSettingsGeneration();
    const run = async () => {
        if (!isCurrent())
            return;
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
                await launchOne(String(app.path), receipt, isCurrent, async () => {
                    const latest = await readOnce(loadSettings, '开机模式设置');
                    const apps = Array.isArray(latest.quickApps.apps) ? latest.quickApps.apps : [];
                    return apps.some((entry: any) => applicationKey(String(entry?.path ?? '')) === key);
                });
            }
            catch (error) {
                receipt.pending.push('应用启动：' + text(error));
            }
        }
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
    if (launch.timing === 'manual' || !(launch.paths as unknown[])?.length)
        return;
    if (launch.timing === 'steam') {
        receipt.pending.push(steamPending);
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
                await publishReceipt(receipt);
            });
        };
        const observe = async () => {
            if (!isCurrent() || dispatched) return;
            reading = true; dirty = false;
            try {
                const state = await optional(() => steamUi.read({ automatic: true }), 'Steam 连接状态');
                if (isCurrent() && steamConnected(state)) {
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
interface StartupContext {
    boot: string; generation: number; mode: BootModeId;
    sourceRevision: number; revision: number; committed: boolean; savedResult?: string;
}
const startupOwners = new WeakMap<BootModeReceipt, StartupContext>();
async function rememberStartupResult(result: BootStartupResult, owner: StartupContext): Promise<boolean> {
    if (getSettingsGeneration() !== owner.generation) return false;
    const encoded = JSON.stringify(result);
    if (owner.savedResult === encoded) return true;
    let accepted = false;
    await updateSettings(settings => {
        assertSettingsGeneration(owner.generation);
        if (settings.extensions?.bootModesAppliedBoot !== owner.boot) return;
        const active = object(settings.extensions?.[MODULE_KEY]);
        if (result.status !== 'failed' && active.current !== owner.mode) return;
        // A later startup operation (or independent newer result) owns its own
        // evidence; an old asynchronous completion cannot replace that record.
        const previous = normalizeBootStartupResult(settings.extensions?.bootModesStartupResult, owner.boot);
        if (previous && previous.sourceRevision > owner.sourceRevision) return;
        settings.extensions = { ...settings.extensions, bootModesStartupResult: cloneBootValue(result) };
        accepted = true;
    });
    if (accepted) owner.savedResult = encoded;
    return accepted;
}
async function publishReceipt(receipt: BootModeReceipt): Promise<void> {
    const owner = startupOwners.get(receipt);
    if (owner) {
        const result: BootStartupResult = { schema: 1, boot: owner.boot, mode: owner.mode,
            sourceRevision: owner.sourceRevision, revision: owner.revision,
            status: receipt.pending.length || receipt.skipped.length ? 'partial' : 'applied',
            applied: [...receipt.applied], skipped: [...receipt.skipped], pending: [...receipt.pending] };
        try { if (!await rememberStartupResult(result, owner)) return; }
        catch (error) {
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
async function applyBootModeInQueue(id: BootModeId, document: BootModeDocument, mouseEpoch: number, owner?: StartupContext): Promise<{document: BootModeDocument; receipt: BootModeReceipt}> {
    cancelBootModeLaunches();
    const generation = getSettingsGeneration(), beforeSettings = await readOnce(loadSettings, '开机模式设置'), before = await captureBootModeSnapshot(), next = resolveBootMode(document, id), receipt: BootModeReceipt = { mode: id, applied: [], skipped: [], pending: [] };
    if (owner) startupOwners.set(receipt, owner);
    if (next.display.resolution === 'native')
        next.display.resolution = document.global.display.resolution;
    if (next.display.hz === 'native')
        next.display.hz = document.global.display.hz;
    const current = readStoredBootMode(beforeSettings, document.global);
    if (current.revision !== document.revision)
        throw Error('开机模式已变化，请重新读取');
    const isCurrent = () => getSettingsGeneration() === generation;
    // Native-only metadata is not input intent. No dedicated game profile is written here.
    const inputChanged = !same(inputPatch(before, beforeSettings, false), inputPatch(next, beforeSettings));
    const mouseChanged = before.input.persona !== next.input.persona || (next.input.mouse !== null && (before.input.mouse !== next.input.mouse || (before.input.mousePolicy !== null && before.input.mousePolicy !== (next.input.mouse === 'off' ? 'off' : 'on'))));
    const steps: BootApplyStep[] = [];
    let inputRevision = beforeSettings.input.revision;
    let deferMouse = false;
    if (mouseChanged)
        steps.push({ id: 'mouse-stop', apply: async () => {
                try {
                    await stopMouse();
                }
                catch (error) {
                    if (!(error instanceof BootMouseUnavailable))
                        throw error;
                    receipt.pending.push('原模拟鼠标：' + text(error));
                }
            }, rollback: () => restoreMouse(before.input) });
    if (inputChanged)
        steps.push({ id: 'global-input', apply: async () => { inputRevision = await writeBaseInput(next, inputRevision, isCurrent); receipt.applied.push('全局手柄与陀螺仪'); }, rollback: async () => {
                const result = await compareAndSwapInputSettings(inputRevision, { outputTarget: beforeSettings.input.outputTarget, ownership: beforeSettings.input.ownership, gyroMotion: beforeSettings.input.gyroMotion, buttonMapping: beforeSettings.input.buttonMapping }, isCurrent);
                if (result.ok === false)
                    throw Error('旧输入配置恢复遇到新版本：' + result.reason);
                inputRevision = result.value.revision;
            } });
    if (!same(before.controller, next.controller))
        steps.push({ id: 'controller-native-flags', apply: () => writeNativeFlags(next), rollback: () => writeNativeFlags(before) });
    if (mouseChanged && next.input.mouse !== null)
        steps.push({ id: 'mouse-start', apply: async () => {
                try {
                    await applyMouse(next.input, isCurrent);
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
            }, rollback: stopMouse });
    else if (mouseChanged)
        receipt.pending.push('模拟鼠标：目标配置未知，已关闭旧模拟鼠标');
    const saved = cloneBootValue(document);
    saved.current = id;
    saved.revision++;
    saved.activeSnapshot = cloneBootValue(before);
    for (const module of ['input', 'gyro', 'controller', 'softwareLaunch'] as const)
        saved.activeSnapshot[module] = cloneBootValue(next[module]);
    steps.push({ id: 'save-current-mode', apply: () => updateSettings(settings => {
            const latest = readStoredBootMode(settings, document.global);
            if (latest.revision !== document.revision)
                throw Error('模式切换期间配置已变化');
            if (deferMouse)
                saved.activeSnapshot.input.mouse = null;
            settings.extensions = { ...settings.extensions, [MODULE_KEY]: saved };
        }), rollback: async () => {
            await updateSettings(settings => {
                const latest = readStoredBootMode(settings, document.global);
                if (latest.revision !== saved.revision || latest.current !== id)
                    return;
                settings.extensions = { ...settings.extensions, [MODULE_KEY]: beforeSettings.extensions?.[MODULE_KEY] };
            });
        } });
    await executeBootModeSteps(steps, isCurrent);
    if (owner) owner.committed = true;
    // Commit input/mouse and selected mode first. Optional hardware/software failures have explicit
    // receipts and cannot cause a later input error to replay an old hardware snapshot.
    for (const module of BOOT_MODE_MODULES) {
        if (['input', 'gyro', 'controller', 'softwareLaunch', 'audioOptimization', 'frameGeneration'].includes(module) || same(before[module], next[module]))
            continue;
        assertSettingsGeneration(generation);
        const latest = readStoredBootMode(await readOnce(loadSettings, '开机模式设置'), document.global);
        if (latest.revision !== saved.revision || latest.current !== id)
            throw Error('当前模式已被其他窗口切换，后续操作已停止');
        const pendingCount = receipt.pending.length, skippedCount = receipt.skipped.length;
        try {
            await applyRuntimeModule(module, next[module], before[module], receipt);
            if (receipt.pending.length === pendingCount && receipt.skipped.length === skippedCount)
                saved.activeSnapshot[module] = cloneBootValue(next[module]);
        }
        catch (error) {
            receipt.pending.push(LABELS[module] + '：' + text(error));
        }
    }
    await updateSettings(settings => {
        const latest = readStoredBootMode(settings, document.global);
        if (latest.revision !== saved.revision || latest.current !== id)
            throw Error('当前模式已变化');
        settings.extensions = { ...settings.extensions, [MODULE_KEY]: saved };
    });
    cancelBootModeLaunches();
    assertSettingsGeneration(generation);
    for (const module of ['audioOptimization', 'frameGeneration'] as const) {
        try {
            await applyRuntimeModule(module, next[module], before[module], receipt);
        }
        catch (error) {
            receipt.pending.push(LABELS[module] + '：' + text(error));
        }
    }
    await scheduleLaunches(next, receipt, generation);
    if (deferMouse && mouseEpoch === deferredMouseEpoch)
        deferBootMouse(next.input, receipt, generation, saved.global);
    await publishReceipt(receipt);
    return { document: saved, receipt };
}
async function writeNativeFlags(snapshot: BootModeSnapshot) {
    const flags: Record<string, boolean> = Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map(item => [item.defaultKey, false]));
    for (const rule of Array.isArray(snapshot.controller.rules) ? snapshot.controller.rules : []) {
        const value = object(rule);
        if (value.origin === 'native-default' && typeof value.defaultKey === 'string' && Object.prototype.hasOwnProperty.call(flags, value.defaultKey))
            flags[value.defaultKey] = value.enabled === true;
    }
    const actual = await summonSet(flags);
    if (Object.entries(flags).some(([key, value]) => (actual as unknown as Record<string, unknown>)[key] !== value))
        throw Error('原生快捷键开关同步未确认');
    await updateSettings(settings => { settings.gamepad = { ...settings.gamepad, ...flags }; });
    if (typeof window !== 'undefined') {
        window.dispatchEvent(new CustomEvent('ipc:gamepad.settings', { detail: actual }));
        window.dispatchEvent(new CustomEvent('ipc:controller-shortcuts.updated'));
    }
}
let startupRestore: Promise<void> | null = null;
export function restoreSelectedBootMode(): Promise<void> {
    return startupRestore ??= serialized(async () => {
        const generation = getSettingsGeneration(), boot = await invoke<string>('bootMode.startup.identity', {}, { timeoutMs: 3000 });
        if (!boot || typeof boot !== 'string') throw Error('Windows 启动标识尚未确认，开机模式暂不自动应用');
        let shouldApply = false, owner: StartupContext | undefined, claimedRaw: unknown;
        await updateSettings(settings => {
            assertSettingsGeneration(generation);
            if (settings.extensions?.bootModesAppliedBoot === boot) return;
            settings.extensions = { ...settings.extensions, bootModesAppliedBoot: boot };
            shouldApply = true;
            const raw = object(settings.extensions?.[MODULE_KEY]);
            if (raw.schema === 1 && raw.bootEnabled !== false && isBootModeId(raw.boot)) {
                const revision = Number.isSafeInteger(raw.revision) && raw.revision >= 0 ? raw.revision : 0;
                owner = { boot, generation, mode: raw.boot, sourceRevision: revision, revision, committed: false };
                claimedRaw = cloneBootValue(raw);
            }
        });
        // A disabled/unconfigured boot never probes devices or adopts a mode
        // that another window selected after this startup claim.
        if (!shouldApply || !owner) return;
        try {
            const baseline = await captureBootModeSnapshot(), document = normalizeBootModeDocument(claimedRaw, baseline);
            owner.revision = document.revision + 1;
            cancelBootModeDeferredMouse();
            await applyBootModeInQueue(owner.mode, document, deferredMouseEpoch, owner);
            await updateSettings(value => {
                assertSettingsGeneration(generation);
                const active = object(value.extensions?.[MODULE_KEY]);
                if (value.extensions?.bootModesAppliedBoot === boot && active.current === owner!.mode)
                    value.extensions = { ...value.extensions, bootModesActiveBoot: boot };
            });
        } catch (error) {
            if (owner) {
                const result: BootStartupResult = { schema: 1, boot, mode: owner.mode, sourceRevision: owner.sourceRevision,
                    revision: owner.committed ? owner.revision : owner.sourceRevision,
                    status: owner.committed ? 'partial' : 'failed', applied: [], skipped: [], pending: [text(error)], error: text(error) };
                try {
                    if (await rememberStartupResult(result, owner) && typeof window !== 'undefined')
                        window.dispatchEvent(new CustomEvent('boot-mode:changed', { detail: {mode: owner.mode, applied: [], skipped: [], pending: [text(error)]} }));
                } catch (recordError) { console.warn('开机失败结果保存未确认：', text(recordError)); }
            }
            throw error;
        }
    });
}
/** Resume and renderer reattach use the applied snapshot, never an edited next-boot draft. */
export async function readBootModeFanIntent(scope: 'startup' | 'current'): Promise<boolean | null> {
    const settings = await readOnce(loadSettings, '开机模式设置'), raw = object(settings.extensions?.[MODULE_KEY]);
    if (raw.schema !== 1)
        return null;
    const activeFan = object(object(raw.activeSnapshot).fan).enabled;
    // The snapshot is authoritative only for the Windows boot that applied it.
    const boot = await optional(() => invoke<string>('bootMode.startup.identity', {}, { timeoutMs: 3000 }), 'Windows 启动标识');
    if (!boot || typeof boot !== 'string')
        return null;
    if (settings.extensions?.bootModesActiveBoot === boot)
        return typeof activeFan === 'boolean' ? activeFan : null;
    // A consumed/disabled startup or newly scheduled scene cannot become a
    // startup fan request just because YMCC was reopened in the same boot.
    if (settings.extensions?.bootModesAppliedBoot === boot)
        return null;
    if (scope === 'current')
        return null;
    if (raw.bootEnabled === false)
        return null;
    const own = raw.boot !== 'global' ? object(object(raw.scenes)[raw.boot]).overrides : undefined;
    const value = object(object(own).fan ?? object(raw.global).fan).enabled;
    return typeof value === 'boolean' ? value : null;
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
