/** Test-only clock for the original schedule consumer inside its CommonJS VM.
 * Install after awaiting production disableFloat(), then enable the real loop.
 * Await production disableFloat() and inspect its timers before restore(). The
 * final fixture cleanup is not evidence that production disposed its resources.
 * performance (shared with the host) and setImmediate are deliberately untouched.
 */
type TimerCallback = (...args: any[]) => unknown;
type TimeoutHost = {
  setTimeout: (callback: TimerCallback, ms?: number, ...args: unknown[]) => unknown;
  clearTimeout: (handle: unknown) => void;
};
type TimerScope = TimeoutHost & {
  setInterval: TimeoutHost['setTimeout'];
  clearInterval: TimeoutHost['clearTimeout'];
  setImmediate: (callback: () => void) => unknown;
  window?: TimeoutHost;
};
type FixtureHandle = {
  clear(): FixtureHandle;
  close(): FixtureHandle;
  ref(): FixtureHandle;
  unref(): FixtureHandle;
  hasRef(): boolean;
};
type Timer = {
  id: number;
  due: number;
  period: number | null;
  callback: TimerCallback;
  args: unknown[];
  handle: FixtureHandle;
};
type SavedProperty = {
  target: object;
  name: string;
  descriptor: PropertyDescriptor | undefined;
};

// Keep recognizing completed/cleared handles, including after a later install.
// A virtual handle must never reach the VM's real clear functions.
const virtualClears = new WeakMap<object, () => void>();
let clockInstalled = false;
const MAX_CALLBACKS_PER_ADVANCE = 10000;

export function installFixtureClock(): {
  now(): number;
  advanceBy(ms: number): Promise<void>;
  pendingIntervals(): number;
  pendingTimeouts(): number;
  readonly callbacks: number;
  restore(): void;
} {
  if (clockInstalled) throw new Error('Fixture clock is already installed');
  const scope = globalThis as unknown as TimerScope;
  const shellWindow = scope.window;
  const originalSetImmediate = scope.setImmediate;
  const originalClearTimeout = scope.clearTimeout;
  const originalClearInterval = scope.clearInterval;
  const originalWindowClearTimeout = shellWindow?.clearTimeout;
  const date = Date;
  const originalDateNow = date.now;
  if (typeof originalSetImmediate !== 'function' || !shellWindow ||
      typeof shellWindow.setTimeout !== 'function' ||
      typeof originalWindowClearTimeout !== 'function') {
    throw new Error('Fixture clock requires the VM timers and initialized mock-shell window');
  }

  let wallNow = Reflect.apply(originalDateNow, date, []) as number;
  let active = true;
  let advancing = false;
  let nextId = 1;
  let totalCallbacks = 0;
  const timers = new Map<number, Timer>();
  const saved: SavedProperty[] = [];
  const callbackErrors: unknown[] = [];

  function requireActive(): void {
    if (!active) throw new Error('Fixture clock has been restored');
  }

  function finiteNonnegative(value: number): void {
    if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) {
      throw new RangeError('Fixture clock milliseconds must be finite and nonnegative');
    }
  }

  function addTime(start: number, ms: number): number {
    const result = start + ms;
    if (!Number.isFinite(result) || result < start) {
      throw new RangeError('Fixture clock time overflow');
    }
    return result;
  }

  function replace(target: object, name: string, value: unknown): void {
    const descriptor = Object.getOwnPropertyDescriptor(target, name);
    const replacement = descriptor && 'value' in descriptor
      ? { ...descriptor, value }
      : { configurable: descriptor?.configurable ?? true,
          enumerable: descriptor?.enumerable ?? true, writable: true, value };
    Object.defineProperty(target, name, replacement);
    saved.push({ target, name, descriptor });
  }

  function restore(): void {
    if (!active) return;
    active = false;
    timers.clear();
    let failed = false;
    let failure: unknown;
    // Restore full descriptors, retaining the exact original function objects.
    // Attempt every restoration even if a caller changed a descriptor meanwhile.
    while (saved.length) {
      const property = saved.pop()!;
      try {
        if (property.descriptor) {
          Object.defineProperty(property.target, property.name, property.descriptor);
        } else if (!Reflect.deleteProperty(property.target, property.name)) {
          throw new Error('Cannot restore fixture clock property: ' + property.name);
        }
      } catch (error) {
        if (!failed) { failed = true; failure = error; }
      }
    }
    clockInstalled = false;
    if (failed) throw failure;
  }

  function register(callback: TimerCallback, ms: number, args: unknown[], interval: boolean): FixtureHandle {
    requireActive();
    if (typeof callback !== 'function') throw new TypeError('Fixture timer callback must be a function');
    finiteNonnegative(ms);
    const due = addTime(wallNow, ms);
    const id = nextId++;
    let referenced = true;
    const clear = () => { timers.delete(id); };
    const handle: FixtureHandle = {
      clear() { clear(); return handle; },
      close() { clear(); return handle; },
      ref() { referenced = true; return handle; },
      unref() { referenced = false; return handle; },
      hasRef() { return timers.has(id) && referenced; },
    };
    virtualClears.set(handle, clear);
    // Zero-delay timers stay at this instant; the callback cap also bounds a
    // zero-period interval or a callback repeatedly scheduling zero-delay work.
    timers.set(id, { id, due, period: interval ? ms : null, callback, args, handle });
    return handle;
  }

  function clearTimer(handle: unknown, original: TimeoutHost['clearTimeout'], receiver: object): void {
    if ((typeof handle === 'object' && handle !== null) || typeof handle === 'function') {
      const clear = virtualClears.get(handle as object);
      if (clear) { clear(); return; }
    }
    // Production may still clear a real handle created before clock installation.
    Reflect.apply(original, receiver, [handle]);
  }

  const setFixtureTimeout = (callback: TimerCallback, ms = 0, ...args: unknown[]) =>
    register(callback, ms, args, false);
  const setFixtureInterval = (callback: TimerCallback, ms = 0, ...args: unknown[]) =>
    register(callback, ms, args, true);
  const clearFixtureTimeout = (handle: unknown) => clearTimer(handle, originalClearTimeout, scope);
  const clearFixtureInterval = (handle: unknown) => clearTimer(handle, originalClearInterval, scope);

  async function drainPromises(): Promise<void> {
    // The real event-loop turn lets the preceding callback's entire runnable
    // Promise chain drain. No fixed count of microtask awaits assumes settlement.
    await new Promise<void>(resolve => Reflect.apply(originalSetImmediate, scope, [resolve]));
    requireActive();
    if (callbackErrors.length) throw callbackErrors.shift();
  }

  function nextDue(target: number): Timer | undefined {
    let next: Timer | undefined;
    for (const timer of timers.values()) {
      if (timer.due <= target && (!next || timer.due < next.due ||
          (timer.due === next.due && timer.id < next.id))) next = timer;
    }
    return next;
  }

  async function advanceBy(ms: number): Promise<void> {
    requireActive();
    finiteNonnegative(ms);
    if (advancing) throw new Error('Fixture clock advanceBy must not overlap or reenter');
    const target = addTime(wallNow, ms);
    advancing = true;
    let executed = 0;
    try {
      await drainPromises();
      for (;;) {
        const timer = nextDue(target);
        if (!timer) {
          if (wallNow < target) {
            wallNow = target;
            // Include zero-delay work registered by Promise continuations at
            // the target, without ever advancing beyond it and rolling back.
            await drainPromises();
            continue;
          }
          return;
        }
        if (executed >= MAX_CALLBACKS_PER_ADVANCE) {
          throw new Error('Fixture clock exceeded 10000 callbacks in one advanceBy');
        }
        wallNow = Math.max(wallNow, timer.due);
        if (timer.period === null) timers.delete(timer.id);
        else timer.due = addTime(timer.due, timer.period);
        executed++;
        totalCallbacks++;
        try {
          const result = Reflect.apply(timer.callback, timer.handle, timer.args);
          // Observe async failures without awaiting a callback that may itself
          // need a later virtual timer. Its runnable continuations drain below.
          void Promise.resolve(result).catch(error => { callbackErrors.push(error); });
        } catch (error) {
          callbackErrors.push(error);
        }
        await drainPromises();
      }
    } finally {
      advancing = false;
    }
  }

  function pending(interval: boolean): number {
    let count = 0;
    for (const timer of timers.values()) if ((timer.period !== null) === interval) count++;
    return count;
  }

  clockInstalled = true;
  try {
    replace(date, 'now', () => wallNow);
    replace(scope, 'setTimeout', setFixtureTimeout);
    replace(scope, 'clearTimeout', clearFixtureTimeout);
    replace(scope, 'setInterval', setFixtureInterval);
    replace(scope, 'clearInterval', clearFixtureInterval);
    if ((shellWindow as object) !== (scope as object)) {
      replace(shellWindow, 'setTimeout', setFixtureTimeout);
      replace(shellWindow, 'clearTimeout', (handle: unknown) =>
        clearTimer(handle, originalWindowClearTimeout!, shellWindow));
    }
  } catch (error) {
    restore();
    throw error;
  }

  return {
    now: () => wallNow,
    advanceBy,
    pendingIntervals: () => pending(true),
    pendingTimeouts: () => pending(false),
    get callbacks() { return totalCallbacks; },
    restore,
  };
}
