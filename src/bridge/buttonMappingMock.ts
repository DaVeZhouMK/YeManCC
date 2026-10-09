/** B1/T3 fixture-only mapper. No reader, report writer, or device access. */
export type VirtualControl = 'a' | 'b' | 'x' | 'y' | 'lb' | 'rb' | 'back' | 'start' | 'ls' | 'rs' | 'dpad-up' | 'dpad-down' | 'dpad-left' | 'dpad-right';
export type EmitAction =
  | { type: 'button'; control: VirtualControl }
  | { type: 'trigger'; control: 'left' | 'right'; threshold: number };
export type ButtonAction =
  | EmitAction
  | { type: 'shift'; modifier: string; physical: string; emit: EmitAction }
  | { type: 'chord'; keys: string[]; emit: EmitAction }
  | { type: 'hold'; physical: string; durationMs: number; emit: EmitAction }
  | { type: 'toggle'; physical: string; emit: EmitAction };
export type ButtonRule = { closure: 'CLOSED'; action: ButtonAction };
export type ButtonMappingResult =
  | { ok: true; buttons: ReadonlySet<VirtualControl>; triggers: { left: number; right: number } }
  | { ok: false; reason: 'unclosed-config' | 'invalid-rule' };
export interface ButtonMappingSession {
  holdStartedAt: Record<string, number>;
  toggleOn: Record<string, boolean>;
  toggleHeld: Record<string, boolean>;
}

export function emptyButtonSession(): ButtonMappingSession {
  return { holdStartedAt: {}, toggleOn: {}, toggleHeld: {} };
}

function applyEmit(emit: EmitAction | undefined, buttons: Set<VirtualControl>, triggers: { left: number; right: number }): boolean {
  if (!emit) return false;
  if (emit.type === 'button') { buttons.add(emit.control); return true; }
  if ((emit.control === 'left' || emit.control === 'right') && Number.isFinite(emit.threshold) && emit.threshold >= 0 && emit.threshold <= 1) {
    triggers[emit.control] = Math.max(triggers[emit.control], emit.threshold);
    return true;
  }
  return false;
}

function isEmit(action: unknown): action is EmitAction {
  return !!action && typeof action === 'object' && ((action as EmitAction).type === 'button' || (action as EmitAction).type === 'trigger');
}

/** T3 session mapper: shift/chord/hold/toggle plus SAFE_STOP negatives. */
export function evaluateButtonMapping(
  rules: Record<string, unknown>,
  pressed: ReadonlySet<string>,
  nowMs = 0,
  session: ButtonMappingSession = emptyButtonSession(),
): { result: ButtonMappingResult; session: ButtonMappingSession } {
  const next: ButtonMappingSession = {
    holdStartedAt: { ...session.holdStartedAt },
    toggleOn: { ...session.toggleOn },
    toggleHeld: { ...session.toggleHeld },
  };
  const buttons = new Set<VirtualControl>();
  const triggers = { left: 0, right: 0 };

  for (const [id, raw] of Object.entries(rules)) {
    const rule = raw as Partial<ButtonRule> | undefined;
    if (!rule) continue;
    if (rule.closure !== 'CLOSED') return { result: { ok: false, reason: 'unclosed-config' }, session: next };
    const action = rule.action as ButtonAction | undefined;
    if (!action || typeof action !== 'object' || !('type' in action)) return { result: { ok: false, reason: 'invalid-rule' }, session: next };

    if (action.type === 'button' || action.type === 'trigger') {
      if (pressed.has(id) && !applyEmit(action, buttons, triggers)) return { result: { ok: false, reason: 'invalid-rule' }, session: next };
      continue;
    }
    if (action.type === 'shift') {
      if (!action.modifier || !action.physical || !isEmit(action.emit)) return { result: { ok: false, reason: 'invalid-rule' }, session: next };
      if (pressed.has(action.modifier) && pressed.has(action.physical)) applyEmit(action.emit, buttons, triggers);
      continue;
    }
    if (action.type === 'chord') {
      if (!Array.isArray(action.keys) || action.keys.length < 2 || !isEmit(action.emit)) return { result: { ok: false, reason: 'invalid-rule' }, session: next };
      if (action.keys.every((key) => pressed.has(key))) applyEmit(action.emit, buttons, triggers);
      continue;
    }
    if (action.type === 'hold') {
      if (!action.physical || !Number.isFinite(action.durationMs) || action.durationMs < 0 || !isEmit(action.emit)) return { result: { ok: false, reason: 'invalid-rule' }, session: next };
      if (pressed.has(action.physical)) {
        if (next.holdStartedAt[id] === undefined) next.holdStartedAt[id] = nowMs;
        if (nowMs - next.holdStartedAt[id] >= action.durationMs) applyEmit(action.emit, buttons, triggers);
      } else {
        delete next.holdStartedAt[id];
      }
      continue;
    }
    if (action.type === 'toggle') {
      if (!action.physical || !isEmit(action.emit)) return { result: { ok: false, reason: 'invalid-rule' }, session: next };
      const down = pressed.has(action.physical);
      if (down && !next.toggleHeld[id]) next.toggleOn[id] = !next.toggleOn[id];
      next.toggleHeld[id] = down;
      if (next.toggleOn[id]) applyEmit(action.emit, buttons, triggers);
      continue;
    }
    return { result: { ok: false, reason: 'invalid-rule' }, session: next };
  }
  return { result: { ok: true, buttons, triggers }, session: next };
}

export function mapButtons(rules: Record<string, unknown>, pressed: ReadonlySet<string>): ButtonMappingResult {
  return evaluateButtonMapping(rules, pressed).result;
}
