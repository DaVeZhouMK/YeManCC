import { CONTROLLER_SHORTCUT_ACTIONS, type ControllerShortcutRule } from './controllerShortcutRules';

export type KeyboardOutputKey = { actionId: string; code: string };
export type KeyboardOutputStep = { keys: KeyboardOutputKey[]; holdMs: number } | { delayMs: number };
export type KeyboardOutputSequence = { version: 1; steps: KeyboardOutputStep[]; mode: 'once' | 'toggle'; repeatCount: number; intervalMs: number };
export const KEYBOARD_OUTPUT_HOLD_MS = 80;
export const KEYBOARD_SEQUENCE_LIMIT = 32;
export function keyboardOutputKeyValid(key: KeyboardOutputKey): boolean {
  return !!key && key.actionId.startsWith('keyboard.') && !!CONTROLLER_SHORTCUT_ACTIONS.find(a => a.id === key.actionId)?.parameters?.find(p => p.id === 'key')?.options?.some(o => o.value === key.code);
}
const int = (n: unknown, lo: number, hi: number): n is number => typeof n === 'number' && Number.isInteger(n) && n >= lo && n <= hi;
export function parseKeyboardSequence(value: unknown): KeyboardOutputSequence | null {
  try {
    if (typeof value !== 'string' || value.length > 16384) return null;
    const s = JSON.parse(value) as KeyboardOutputSequence;
    if (!s || s.version !== 1 || !['once', 'toggle'].includes(s.mode) || !int(s.repeatCount, 1, 100) || !int(s.intervalMs, 100, 60000) || !Array.isArray(s.steps) || !s.steps.length || s.steps.length > KEYBOARD_SEQUENCE_LIMIT) return null;
    let keys = false;
    for (const step of s.steps) {
      if (!step || typeof step !== 'object') return null;
      if ('delayMs' in step) { if ('keys' in step || !int(step.delayMs, 10, 60000)) return null; }
      else {
        if (!Array.isArray(step.keys) || !step.keys.length || step.keys.length > 4 || !int(step.holdMs, 40, 2000) || !step.keys.every(keyboardOutputKeyValid) || new Set(step.keys.map(k => k.code)).size !== step.keys.length) return null;
        const expanded = new Set(step.keys.map(k => k.code)); if (step.keys.some(k => k.actionId === 'keyboard.symbol')) expanded.add('ShiftLeft'); if (expanded.size > 4) return null;
        keys = true;
      }
    }
    return keys ? s : null;
  } catch { return null; }
}
export function keyboardSequenceFor(rule: Pick<ControllerShortcutRule, 'actionId' | 'params'>): KeyboardOutputSequence {
  return parseKeyboardSequence(rule.params.keySequence) || { version: 1, steps: [{ keys: [{ actionId: rule.actionId, code: String(rule.params.key || 'KeyA') }], holdMs: KEYBOARD_OUTPUT_HOLD_MS }], mode: 'once', repeatCount: 1, intervalMs: 150 };
}
export function keyboardOutputKeyLabel(key: KeyboardOutputKey): string {
  return CONTROLLER_SHORTCUT_ACTIONS.find(a => a.id === key.actionId)?.parameters?.find(p => p.id === 'key')?.options?.find(o => o.value === key.code)?.label || key.code;
}
export function keyboardSequenceLabel(rule: Pick<ControllerShortcutRule, 'actionId' | 'params'>): string {
  const sequence = keyboardSequenceFor(rule), first = sequence.steps.find(s => 'keys' in s);
  const label = first && 'keys' in first ? first.keys.map(keyboardOutputKeyLabel).join(' + ') : '未选按键';
  return label + (sequence.steps.length > 1 ? ` · ${sequence.steps.length} 步` : '') + (sequence.mode === 'toggle' ? ' · 循环' : sequence.repeatCount > 1 ? ` · ${sequence.repeatCount} 次` : '');
}
