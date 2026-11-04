import { snapshotSettingsData } from './settingsSnapshot';

type RecordValue = Record<string, any>;
const object = (value: unknown): RecordValue => value && typeof value === 'object' && !Array.isArray(value) ? value as RecordValue : {};

/** The ordinary input configuration is the only global shortcut source. */
export function globalShortcutContainer(settings: RecordValue): RecordValue {
    return snapshotSettingsData(object(object(object(settings.input).buttonMapping).rules));
}

export function hasStoredGlobalShortcuts(settings: RecordValue): boolean {
    const rules = globalShortcutContainer(settings);
    // An explicit ownership marker survives a damaged/missing array.
    // Do not heal it by silently re-enabling factory shortcuts.
    return rules.shortcutRulesInitialized === true || (Array.isArray(rules.shortcutRules) && rules.shortcutRules.length > 0);
}

/** Compatibility for existing editor/card callers after mode shortcuts retired.
 * This never reactivates the retired mode document. */
export function hasModeShortcutOverride(_settings: RecordValue): boolean { return false; }

/** Retired mode records never compete with ordinary global shortcuts. */
export function effectiveShortcutContainer(settings: RecordValue): RecordValue {
    return globalShortcutContainer(settings);
}

/** Remove controller editing/calibration from a mode document only.
 * The ordinary input.buttonMapping and input.axisCalibration are untouched. */
export function retireBootControllerSnapshots(document: RecordValue): void {
    const strip = (snapshot: RecordValue) => {
        for (const key of ['controller', 'buttonMapping', 'axisCalibration', 'controllerCalibration', 'calibration']) delete snapshot[key];
        const input = object(snapshot.input);
        for (const key of ['controller', 'buttonMapping', 'axisCalibration', 'controllerCalibration', 'calibration', 'bootModeShortcuts', 'shortcutRules']) delete input[key];
        const target = object(input.outputTarget);
        for (const key of ['controller', 'buttonMapping', 'axisCalibration', 'controllerCalibration', 'calibration', 'shortcutRules']) delete target[key];
    };
    strip(document); strip(object(document.global)); strip(object(document.activeSnapshot));
    for (const scene of Object.values(object(document.scenes))) {
        strip(object(object(scene).overrides)); strip(object(object(scene).rememberedOverrides));
    }
    for (const presets of Object.values(object(document.presets)))
        for (const scene of Object.values(object(presets))) {
            strip(object(object(scene).overrides)); strip(object(object(scene).rememberedOverrides));
        }
    delete document.controllerTemplates;
}

/** Settings read/save migration; deleting mode data must not rewrite global input. */
export function retireBootGlobalShortcuts(settings: RecordValue): void {
    const input = object(settings.input);
    if (Object.prototype.hasOwnProperty.call(input, 'bootModeShortcuts')) input.bootModeShortcuts = null;
    const extensions = object(settings.extensions), modes = object(extensions.bootModes);
    if (modes.schema === 1) retireBootControllerSnapshots(modes);
    delete extensions.bootModeGlobalShortcutsBackup;
}

/** Shortcut-array writes are ordinary global edits, including an empty list. */
export function prepareGlobalShortcutEdit(patch: RecordValue): RecordValue {
    const rules = object(object(patch.buttonMapping).rules);
    if (!Array.isArray(rules.shortcutRules)) return patch;
    return { ...patch, buttonMapping: { ...patch.buttonMapping,
        rules: { ...rules, shortcutRulesInitialized: true } } };
}
