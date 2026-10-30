import { snapshotSettingsData } from './settingsSnapshot';

type RecordValue = Record<string, any>;
const object = (value: unknown): RecordValue => value && typeof value === 'object' && !Array.isArray(value) ? value as RecordValue : {};

/** The ordinary input configuration is the only global shortcut source. */
export function globalShortcutContainer(settings: RecordValue): RecordValue {
    return snapshotSettingsData(object(object(object(settings.input).buttonMapping).rules));
}

export function hasStoredGlobalShortcuts(settings: RecordValue): boolean {
    const rules = globalShortcutContainer(settings);
    return Array.isArray(rules.shortcutRules) && (rules.shortcutRulesInitialized === true || rules.shortcutRules.length > 0);
}

export function hasModeShortcutOverride(settings: RecordValue): boolean {
    const input = object(settings.input), own = object(input.bootModeShortcuts);
    return own.schema === 1 && ['xbox', 'steam', 'xbox-steam'].includes(own.mode) &&
        Array.isArray(own.rules) && own.boot === String(input.startupObservedBoot || '');
}

/** A mode's applied output is separate from the durable global rule list. */
export function effectiveShortcutContainer(settings: RecordValue): RecordValue {
    const global = globalShortcutContainer(settings);
    return hasModeShortcutOverride(settings)
        ? { ...global, shortcutRules: snapshotSettingsData(settings.input.bootModeShortcuts.rules) } : global;
}

/** Retire the old competing global source, retaining exactly one inert backup.
 * Called at the normal settings boundary, never from a live-device capture. */
export function retireBootGlobalShortcuts(settings: RecordValue): void {
    const extensions = object(settings.extensions), modes = object(extensions.bootModes), global = object(modes.global);
    if (modes.schema !== 1 || !Object.prototype.hasOwnProperty.call(global, 'controller')) return;
    if (!Object.prototype.hasOwnProperty.call(extensions, 'bootModeGlobalShortcutsBackup')) {
        extensions.bootModeGlobalShortcutsBackup = { schema: 1, revision: modes.revision ?? 0,
            controller: snapshotSettingsData(global.controller) };
    }
    delete global.controller;
}

/** Shortcut-array writes are ordinary global edits, including an empty list. */
export function prepareGlobalShortcutEdit(patch: RecordValue): RecordValue {
    const rules = object(object(patch.buttonMapping).rules);
    if (!Array.isArray(rules.shortcutRules)) return patch;
    return { ...patch, buttonMapping: { ...patch.buttonMapping,
        rules: { ...rules, shortcutRulesInitialized: true } } };
}
