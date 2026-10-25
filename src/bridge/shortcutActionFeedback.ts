/** Shortcut results for monitor consumers. This module never renders or performs an action. */
import { on } from './ipc';

export const SHORTCUT_FEEDBACK_EVENT = 'input.shortcutFeedback';
export type ShortcutFeedbackState = 'applied' | 'saved' | 'pending' | 'failed';
export interface ShortcutActionFeedback {
  schemaVersion: 1;
  id: string;
  actionId: string;
  origin: 'controller-shortcut' | 'updater' | 'monitor';
  state: ShortcutFeedbackState;
  title: string;
  message: string;
  occurredAtUnixMs: number;
  expiresAtUnixMs: number;
  powerGeneration: number;
  value?: { text: string; number?: number; unit?: string };
  presentation: { fullscreen: true; leftMonitor: true };
}

const visibleTransitions = new Set([
  'window.summon', 'window.hideToTray', 'os.openTouchKeyboard',
  'os.returnDesktop', 'os.switchProgram', 'input.frontendButton',
]);
export function shouldPresentShortcutFeedback(actionId: string, state: ShortcutFeedbackState): boolean {
  if (actionId === 'mouse.toggle') return false;
  // Failed requests have no visible outcome. Preserve their readable error.
  return state === 'failed' || (!visibleTransitions.has(actionId) && !actionId.startsWith('keyboard.'));
}

export function normalizeShortcutActionFeedback(raw: unknown, now = Date.now()): ShortcutActionFeedback | null {
  if (!raw || typeof raw !== 'object') return null;
  const value = raw as Partial<ShortcutActionFeedback>;
  const text = (item: unknown, max: number): item is string => typeof item === 'string' && item.length > 0 && item.length <= max && !/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/.test(item);
  if (value.schemaVersion !== 1 || (value.origin !== 'controller-shortcut' && !(value.origin === 'monitor' && ['monitor.dragLock','monitor.template'].includes(value.actionId??'') && value.state === 'applied') && !(value.origin === 'updater' && value.actionId === 'app.updateReady' && value.state === 'saved')) || !text(value.id, 128) || !text(value.actionId, 80) ||
      !['applied', 'saved', 'pending', 'failed'].includes(String(value.state)) || !text(value.title, 64) || !text(value.message, 256) ||
      !Number.isSafeInteger(value.occurredAtUnixMs) || !Number.isSafeInteger(value.expiresAtUnixMs) || !Number.isSafeInteger(value.powerGeneration)) return null;
  const occurredAtUnixMs = value.occurredAtUnixMs!, expiresAtUnixMs = value.expiresAtUnixMs!;
  if (value.powerGeneration! < 0 || occurredAtUnixMs > now + 1000 || expiresAtUnixMs <= now || expiresAtUnixMs <= occurredAtUnixMs || expiresAtUnixMs - occurredAtUnixMs > 10000) return null;
  if (!shouldPresentShortcutFeedback(value.actionId, value.state!)) return null;
  const result: ShortcutActionFeedback = {
    schemaVersion: 1, id: value.id, actionId: value.actionId, origin: value.origin!, state: value.state!,
    title: value.title, message: value.message, occurredAtUnixMs, expiresAtUnixMs, powerGeneration: value.powerGeneration!,
    presentation: { fullscreen: true, leftMonitor: true },
  };
  if (value.value && text(value.value.text, 96)) {
    result.value = { text: value.value.text };
    if (typeof value.value.number === 'number' && Number.isFinite(value.value.number)) result.value.number = value.value.number;
    if (text(value.value.unit, 16)) result.value.unit = value.value.unit;
  }
  return result;
}

/** One native subscription, bounded duplicate history, no timer and no replay after mounting. */
export function createShortcutFeedbackChannel(subscribe: (listener: (raw: unknown) => void) => () => void, clock = Date.now) {
  const listeners = new Set<(value: ShortcutActionFeedback) => void>();
  const seen = new Map<string, number>();
  let stop: (() => void) | null = null;
  return {
    subscribe(listener: (value: ShortcutActionFeedback) => void) {
      listeners.add(listener);
      if (!stop) stop = subscribe(raw => {
        const now = clock(), value = normalizeShortcutActionFeedback(raw, now);
        if (!value) return;
        for (const [id, expires] of seen) if (expires <= now) seen.delete(id);
        if (seen.has(value.id)) return;
        seen.set(value.id, value.expiresAtUnixMs);
        while (seen.size > 128) seen.delete(seen.keys().next().value!);
        for (const consume of [...listeners]) { try { consume(structuredClone(value)); } catch { /* Display failures cannot affect actions or other consumers. */ } }
      });
      let active = true;
      return () => {
        if (!active) return;
        active = false; listeners.delete(listener);
        if (!listeners.size) { stop?.(); stop = null; seen.clear(); }
      };
    },
  };
}
const feedbackChannel = createShortcutFeedbackChannel(listener => on(SHORTCUT_FEEDBACK_EVENT, listener));
export const onShortcutActionFeedback = feedbackChannel.subscribe;
