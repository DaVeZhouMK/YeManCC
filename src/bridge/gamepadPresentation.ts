/** Renderer-only gamepad state. Never gates native input, gyro or virtual output. */
export interface GamepadPresentation {
  readonly connected: boolean;
  readonly name: string;
  readonly buttons: readonly boolean[];
  readonly axes: readonly number[];
}

export const EMPTY_GAMEPAD_PRESENTATION: GamepadPresentation = Object.freeze({
  connected: false,
  name: '',
  buttons: Object.freeze([] as boolean[]),
  axes: Object.freeze([] as number[]),
});

type Snapshot = { connected?: unknown; name?: unknown; buttons?: unknown; axes?: unknown };

function axis(value: unknown): number {
  const n = Number(value);
  return Number.isFinite(n) ? n : 0;
}

/** Preserve array identity unless its normalized values change. No quantization. */
function updateValues<T>(previous: readonly T[], raw: unknown, normalize: (v: unknown) => T): readonly T[] {
  if (!Array.isArray(raw)) return previous.length === 0 ? previous : [];
  let changed = raw.length !== previous.length;
  if (!changed) {
    for (let i = 0; i < raw.length; ++i) {
      if (previous[i] !== normalize(raw[i])) { changed = true; break; }
    }
  }
  return changed ? raw.map(normalize) : previous;
}

export function reduceGamepadPresentation(previous: GamepadPresentation, payload: unknown): GamepadPresentation {
  const raw: Snapshot = payload && typeof payload === 'object' ? payload as Snapshot : {};
  const connected = Boolean(raw.connected);
  const name = String(raw.name || '手柄');
  const buttons = updateValues(previous.buttons, raw.buttons, Boolean);
  const axes = updateValues(previous.axes, raw.axes, axis);
  if (connected === previous.connected && name === previous.name && buttons === previous.buttons && axes === previous.axes) {
    return previous;
  }
  return { connected, name, buttons, axes };
}

/**
 * Keep only the latest snapshot while a cached/hidden view has no display demand.
 * Resuming publishes that snapshot immediately; visible changes are synchronous.
 * One-shot commands and native input lifecycle do not go through this presenter.
 */
export function createGamepadPresenter(publish: (state: GamepadPresentation) => void) {
  let state = EMPTY_GAMEPAD_PRESENTATION;
  let latest: unknown;
  let hasLatest = false;
  let visible = false;
  let disposed = false;
  const flush = () => {
    if (disposed || !visible || !hasLatest) return;
    const next = reduceGamepadPresentation(state, latest);
    if (next === state) return;
    state = next;
    publish(state);
  };
  return {
    accept(payload: unknown): void {
      if (disposed) return;
      latest = payload;
      hasLatest = true;
      flush();
    },
    setVisible(next: boolean): void {
      if (disposed || visible === next) return;
      visible = next;
      flush();
    },
    dispose(): void {
      disposed = true;
      latest = undefined;
      hasLatest = false;
    },
  };
}
