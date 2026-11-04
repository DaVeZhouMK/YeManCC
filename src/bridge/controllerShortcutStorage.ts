import { actionById, normalizeControllerShortcutRule, type ControllerShortcutRule, type ControllerShortcutFieldEdits } from './controllerShortcutRules';
import { snapshotSettingsData } from './settingsSnapshot';

const stable = (value: unknown): string => {
  if (Array.isArray(value)) return '[' + value.map(stable).join(',') + ']';
  if (value && typeof value === 'object') return '{' + Object.entries(value).filter(([,v])=>v!==undefined).sort(([a],[b])=>a.localeCompare(b)).map(([k,v])=>JSON.stringify(k)+':'+stable(v)).join(',') + '}';
  return JSON.stringify(value) ?? 'undefined';
};
const editableFields = new Set(['id', 'origin', 'defaultKey', 'enabled', 'actionId', 'inputs', 'trigger', 'holdMs', 'doubleWindowMs', 'intervalMs', 'params']);
const object = (value: unknown): value is Record<string, unknown> => Boolean(value && typeof value === 'object' && !Array.isArray(value));
/** Keep untouched stored records exact, including unknown actions, metadata and future input forms.
 * Only fields the editor changed may replace their original stored values.
 * On a CAS retry, pass the latest raw document and its normalized rules.
 */
export function preserveControllerShortcutRecords(rawContainer: unknown, baseline: readonly ControllerShortcutRule[], edited: readonly ControllerShortcutRule[], explicitFields?: ControllerShortcutFieldEdits): unknown[] {
  const raw = object(rawContainer) && Array.isArray(rawContainer.shortcutRules) ? rawContainer.shortcutRules : [];
  const before = new Map(baseline.map(rule=>[rule.id,rule]));
  const after = new Map(edited.map(rule=>[rule.id,rule]));
  const visited = new Set<string>();
  const result: unknown[] = [];
  for (const [index, record] of raw.entries()) {
    if (!object(record)) { result.push(snapshotSettingsData(record)); continue; }
    // An opaque row can share an id with an editable row; it is still not
    // owned by that editor row and must never be edited/deleted through it.
    const normalized = normalizeControllerShortcutRule(record, index);
    if (!normalized) { result.push(snapshotSettingsData(record)); continue; }
    const id = normalized.id;
    const base = before.get(id), next = after.get(id);
    if (!base) { result.push(snapshotSettingsData(record)); continue; }
    visited.add(id);
    if (!next) continue; // explicit deletion of an editor-owned record
    const stored = snapshotSettingsData(record);
    for (const key of new Set([...Object.keys(base), ...Object.keys(next)])) {
      if (!editableFields.has(key)) continue; // Preserve outer metadata the editor does not own.
      const previous = (base as unknown as Record<string, unknown>)[key], value = (next as unknown as Record<string, unknown>)[key];
      const explicit = explicitFields?.get(id)?.has(key as keyof ControllerShortcutRule) === true;
      if (stable(previous) === stable(value) && !explicit) continue;
      if (key === 'params' && object(previous) && object(value)) {
        const params = object(stored.params) ? snapshotSettingsData(stored.params) : {};
        const ownedParameters = new Set(actionById(base.actionId)?.parameters?.map(parameter => parameter.id));
        for (const parameter of new Set([...Object.keys(previous),...Object.keys(value)])) {
          if(stable(previous[parameter])===stable(value[parameter])&&!explicit)continue;
          if (value[parameter] === undefined) {
            // Changing/resetting an output can retire its known schema fields;
            // it does not grant ownership of extensions the editor cannot edit.
            if (ownedParameters.has(parameter)) delete params[parameter];
          } else params[parameter] = snapshotSettingsData(value[parameter]);
        }
        stored.params=params;
      } else if (value === undefined) delete stored[key];
      else stored[key] = snapshotSettingsData(value);
    }
    result.push(stored);
  }
  for(const rule of edited)if(!visited.has(rule.id))result.push(snapshotSettingsData(rule));
  return result;
}
