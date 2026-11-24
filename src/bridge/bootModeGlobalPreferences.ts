import {computeInputConfigHash} from './inputConfigHash';
import {equalSettingsDocuments} from './settingsBaseline';

/** Transient request constraint. Never persisted or promoted to a new setting. */
export interface BootPreferenceContext {input:Record<string,unknown>;extensions:Record<string,unknown>;}
/** The origin belongs to one submitted write, never to a process-wide async scope. */
export interface BootPreferenceWrite { origin?: 'ordinary' | 'boot-mode' | 'runtime'; bootContext?: BootPreferenceContext; }
export interface BootStartupReceiptOwner { readonly boot:string; readonly writeId:string|null; }
type RecordValue = Record<string, any>;
const record = (value: unknown): value is RecordValue => !!value && typeof value === 'object' && !Array.isArray(value);
const same = (a: unknown, b: unknown): boolean => JSON.stringify(a) === JSON.stringify(b);
const copy = <T>(value: T): T => structuredClone(value);
const at = (value: any, path: readonly string[]): any => path.reduce((item, key) => record(item) ? item[key] : undefined, value);
const bootOwnerKeys=['bootModes','bootModesAppliedBoot','bootModesActiveBoot','bootModesStartupResult','bootModesClaimWriteId'];

export function projectBootPreferenceExtensions(extensions?:RecordValue):RecordValue {
  return Object.fromEntries(bootOwnerKeys.filter(key=>extensions?.[key]!==undefined).map(key=>[key,extensions![key]]));
}

export function bootStartupReceiptOwner(settings:Pick<BootPreferenceContext,'input'>&{extensions?:BootPreferenceContext['extensions']}):BootStartupReceiptOwner|undefined {
  const boot=settings.input.startupObservedBoot,extensions=settings.extensions,writeId=extensions?.bootModesClaimWriteId;
  if(typeof boot!=='string'||!boot||extensions?.bootModesAppliedBoot!==boot)return undefined;
  if(writeId===undefined)return {boot,writeId:null}; // A known legacy claim, not an unobserved application.
  if(typeof writeId==='string'&&writeId)return {boot,writeId};
}

export function captureBootPreferenceWrite(options:BootPreferenceWrite):BootPreferenceWrite {
  if(options.bootContext===undefined)return {origin:options.origin};
  const context=copy(options.bootContext);
  if(options.origin!=='boot-mode'||!record(context)||!record(context.input)||!record(context.extensions)||
     !Number.isSafeInteger(context.input.revision)||Number(context.input.revision)<0||
     !record(context.extensions.bootModes)||context.extensions.bootModes.schema!==1||
     Object.keys(context.extensions).some(key=>!bootOwnerKeys.includes(key)))throw Error('INVALID_BOOT_PREFERENCE_CONTEXT');
  return {origin:options.origin,bootContext:context};
}

export function assertBootPreferenceOwner(settings:RecordValue,options:BootPreferenceWrite):void {
  const expected=options.bootContext;
  if(!expected)return;
  if(!record(settings.input)||settings.input.revision!==expected.input.revision||
     computeInputConfigHash(settings.input)!==computeInputConfigHash(expected.input)||
     !equalSettingsDocuments(projectBootPreferenceExtensions(settings.extensions),projectBootPreferenceExtensions(expected.extensions)))throw Error('BOOT_PREFERENCE_REQUEST_EXPIRED');
}

const paths: Array<{ from: string[]; to: string[]; booleanOnly?: boolean }> = [
  ...['theme', 'dynamicBackgroundEnabled', 'backgroundOpacity', 'backgroundBlur', 'videoBatteryPause']
    .map(key => ({ from: ['ui', key], to: ['background', 'ui', key] })),
  { from: ['ui', 'steamDynamicWallpaperEnabled'], to: ['steamWallpaper', 'enabled'] },
  { from: ['ui', 'steamWallpaperBackgroundDownload'], to: ['steamWallpaper', 'download'] },
  { from: ['ui', 'steamWallpaperDownloadCpuThreshold'], to: ['steamWallpaper', 'cpuThreshold'] },
  { from: ['extensions', 'deckySidebar', 'enabled'], to: ['steamDecky', 'enabled'], booleanOnly: true },
  { from: ['rtss', 'monitorOutput'], to: ['monitor', 'preferences'] },
  { from: ['screenTouchpads', 'profiles'], to: ['touch', 'profiles'] },
];

const isSavedGyroParameter = (key: string): boolean =>
  !['enabled', 'virtualPadLink', '__proto__', 'prototype', 'constructor'].includes(key);

/** Close the reverse of the first-document source check: an ordinary write
 * calculated without a global template must not land after one is created. */
export function guardFirstBootPreferenceWrite(before: RecordValue, next: RecordValue, options: BootPreferenceWrite): boolean {
  if (options.origin && options.origin !== 'ordinary') return false;
  const mode = before.extensions?.bootModes;
  if (mode != null && (!record(mode) || mode.schema !== undefined) ||
      !equalSettingsDocuments(mode, next.extensions?.bootModes)) return false;
  if (paths.some(({ from, booleanOnly }) => {
    const value = at(next, from);
    return (!booleanOnly || typeof value === 'boolean') && !equalSettingsDocuments(at(before, from), value);
  })) return true;
  if (!equalSettingsDocuments(before.performanceSchedule, next.performanceSchedule) ||
      !equalSettingsDocuments(before.rtss?.frameRates, next.rtss?.frameRates) ||
      !equalSettingsDocuments(before.rtss?.templateDefaults, next.rtss?.templateDefaults)) return true;
  const oldMotion = before.input?.gyroMotion, motion = next.input?.gyroMotion;
  return record(oldMotion) && record(motion) && Object.keys(motion).some(key =>
    isSavedGyroParameter(key) && !equalSettingsDocuments(oldMotion[key], motion[key]));
}

function assign(root: RecordValue, path: readonly string[], value: unknown): void {
  let node = root;
  for (const key of path.slice(0, -1)) {
    if (!record(node[key])) node[key] = {};
    node = node[key];
  }
  node[path[path.length - 1]] = copy(value);
}

/** Only explicit theme choices from an acknowledged ordinary owner enter global. */
export function rememberOrdinaryAppearancePreferences(settings: RecordValue, states: Record<string, boolean>): boolean {
  const document = settings.extensions?.bootModes;
  if (document === undefined) return false;
  if (!record(document) || document.schema !== 1 || !record(document.global))
    throw new Error('开机模式配置无法确认，未覆盖全局外观偏好');
  const appearance = document.global.steamAppearance;
  if (appearance !== undefined && (!record(appearance) || appearance.states !== undefined && !record(appearance.states)))
    throw new Error('开机全局外观配置无效，原值已保留');
  const previous = appearance?.states || {};
  if (Object.entries(states).every(([name, enabled]) => previous[name] === enabled)) return false;
  if (!Number.isSafeInteger(document.revision) || document.revision < 0 || !Number.isSafeInteger(document.revision + 1))
    throw new Error('开机模式版本号无效，原全局外观偏好已保留');
  document.global.steamAppearance = { ...appearance, states: { ...previous, ...states } };
  document.revision++;
  return true;
}

/** Apply only the ordinary writer's changed leaves, not its entire effective scene. */
function delta(target: RecordValue, path: string[], before: unknown, next: unknown): boolean {
  if (same(before, next)) return false;
  if (next === undefined) {
    const parent = at(target, path.slice(0, -1)), key = path[path.length - 1];
    if (!record(parent) || !Object.prototype.hasOwnProperty.call(parent, key)) return false;
    delete parent[key];
    return true;
  }
  if (record(before) && record(next)) {
    let changed = false;
    for (const key of new Set([...Object.keys(before), ...Object.keys(next)])) {
      if (['__proto__', 'prototype', 'constructor'].includes(key)) continue;
      changed = delta(target, [...path, key], before[key], next[key]) || changed;
    }
    return changed;
  }
  if (same(at(target, path), next)) return false;
  assign(target, path, next);
  return true;
}

/** An ordinary performance owner supplies its before/after preference snapshot
 * inside the existing settings transaction. Only changed leaves enter global;
 * unchanged values may belong to an applied independent scene. */
export function rememberOrdinaryPerformancePreferences(
  settings: RecordValue, before: RecordValue, next: RecordValue, options: BootPreferenceWrite = {},
): boolean {
  if (options.origin && options.origin !== 'ordinary') return false;
  const document = settings.extensions?.bootModes;
  if (document === undefined) return false;
  if (!record(document) || document.schema !== 1 || !record(document.global) ||
      document.global.performance !== undefined && !record(document.global.performance))
    throw new Error('开机全局性能配置无法确认，原配置已保留');
  const global = copy(document.global);
  let changed = false;
  for (const key of new Set([...Object.keys(before), ...Object.keys(next)])) {
    if (!['schedule', 'controls', 'powerSchemes', 'frames'].includes(key)) continue;
    changed = delta(global, ['performance', key], before[key], next[key]) || changed;
  }
  if (!changed) return false;
  if (!Number.isSafeInteger(document.revision) || document.revision < 0 || !Number.isSafeInteger(document.revision + 1))
    throw new Error('开机模式版本号无效，普通性能变更未保存');
  settings.extensions = { ...settings.extensions, bootModes: { ...document, revision: document.revision + 1, global } };
  return true;
}

/** Explicit section patches own only their supplied leaves, including a value
 * already active in an independent scene. Never recapture unmentioned fields. */
function rememberExplicitLeaves(target: RecordValue, path: string[], saved: unknown, intent: unknown): boolean {
  if (record(intent) && record(saved)) {
    let changed = false;
    for (const key of Object.keys(intent)) {
      if (['__proto__', 'prototype', 'constructor'].includes(key) || !Object.prototype.hasOwnProperty.call(saved, key)) continue;
      changed = rememberExplicitLeaves(target, [...path, key], saved[key], intent[key]) || changed;
    }
    return changed;
  }
  if (saved === undefined || same(at(target, path), saved)) return false;
  assign(target, path, saved);
  return true;
}

/** Extend the original durable transaction; no extra document, IO or live capture. */
export function rememberOrdinaryBootPreferences(before: RecordValue, next: RecordValue, options: BootPreferenceWrite = {}, intent?: { section: string; value: RecordValue }): boolean {
  if (options.origin && options.origin !== 'ordinary') return false;
  const previous = before.extensions?.bootModes, document = next.extensions?.bootModes;
  // Explicit mode edits and startup receipts already own their revision/transaction.
  if (!record(document) || document.schema !== 1 || !record(document.global) || !same(previous, document)) return false;
  const global = copy(document.global);
  let changed = false;
  for (const path of paths) {
    const value = at(next, path.from);
    if (path.booleanOnly && typeof value !== 'boolean') continue;
    changed = delta(global, path.to, at(before, path.from), value) || changed;
    if (intent?.section === path.from[0]) {
      const explicit = at(intent.value, path.from.slice(1));
      if (explicit !== undefined) changed = rememberExplicitLeaves(global, path.to, value, explicit) || changed;
    }
  }
  // Enabling a virtual pad is intentionally temporary. Persist only changes to
  // its saved gyro parameters, never promote runtime ON/link state to next boot.
  const oldMotion = before.input?.gyroMotion, motion = next.input?.gyroMotion;
  if (record(oldMotion) && record(motion)) for (const key of Object.keys(motion)) {
    if (!isSavedGyroParameter(key)) continue;
    changed = delta(global, ['gyro', 'motion', key], oldMotion[key], motion[key]) || changed;
  }
  if (!changed) return false;
  if (!Number.isSafeInteger(document.revision) || document.revision < 0 || !Number.isSafeInteger(document.revision + 1))
    throw new Error('开机模式版本号无效，普通全局变更未保存');
  next.extensions = { ...next.extensions, bootModes: { ...document, revision: document.revision + 1, global } };
  return true;
}
