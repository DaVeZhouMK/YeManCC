import { reportFrontendTrace } from '@/robust/frontendDiagnostics';

const WINDOW_BUDGET_MS = 2000;
const MAX_WINDOWS = 6;
let previewInstanceSequence = 0;
const MAX_TEXT_LENGTH = 80;
const HISTOGRAM_UPPER_BOUNDS_MS = [0.25, 0.5, 1, 2, 4, 8, 16, 33, 67, 125, 250, 500, 1000, 2000] as const;

type Context = {
  inputSource: string | null;
  xinputPrimarySlot: number | null;
  virtualOutput: {
    state: string | null;
    requestedPersona: string | null;
    boundPersona: string | null;
    hostEpoch: number | null;
    startProtection: { requestId: number | null };
  };
};

type Histogram = {
  count: number;
  minMs: number | null;
  maxMs: number | null;
  sumMs: number;
  saturated: boolean;
  buckets: number[];
};

type EndReason = 'window-budget' | 'timeout-early' | 'hidden' | 'visibility-lost'
  | 'visible-edge' | 'persona-changed' | 'disposed';

type TraceWindow = {
  sequence: number;
  displayPersona: string | null;
  wallclockStartedAtMs: number | null;
  documentAtStart: { focused: boolean | null; visibility: string | null };
  performanceStartedAtMs: number;
  deadlinePerformanceMs: number;
  latestReceiptAtMs: number;
  patchStartedAtMs: number | null;
  rafPatchAtMs: number | null;
  gamepadStateDeliveryCount: number;
  presentedCount: number;
  updateCount: number;
  beforeUpdateCount: number;
  overwrittenPatchStartCount: number;
  updateWithoutPatchStartCount: number;
  firstContext: Context;
  lastContext: Context;
  contextChangedCount: number;
  latestReceiptAgeAtPatch: Histogram;
  patchSpan: Histogram;
  patchToRafCallback: Histogram;
  timeout: { callbackObserved: boolean; cancelRequested: boolean; cancelFailed: boolean };
  raf: {
    requestedCount: number;
    callbackCount: number;
    outsideWindowCallbackCount: number;
    coalescedUpdateCount: number;
    unavailableCount: number;
    cancelRequestedCount: number;
    cancelFailedCount: number;
  };
};

function record(value: unknown): Record<string, unknown> | null {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
    ? value as Record<string, unknown> : null;
}

function text(value: unknown): string | null {
  return typeof value === 'string' ? value.slice(0, MAX_TEXT_LENGTH) : null;
}

function integer(value: unknown): number | null {
  return typeof value === 'number' && Number.isSafeInteger(value) ? value : null;
}

function context(payload: unknown): Context {
  const raw = record(payload);
  const output = record(raw?.virtualOutput);
  const protection = record(output?.startProtection);
  // Read only this fixed whitelist. Never copy, enumerate or stringify a payload.
  return {
    inputSource: text(raw?.inputSource),
    xinputPrimarySlot: integer(raw?.xinputPrimarySlot),
    virtualOutput: {
      state: text(output?.state),
      requestedPersona: text(output?.requestedPersona),
      boundPersona: text(output?.boundPersona),
      hostEpoch: integer(output?.hostEpoch),
      startProtection: { requestId: integer(protection?.requestId) },
    },
  };
}

// Evidence only: intersection is not proof of native-window occlusion or focus.
function documentEvidence(): { focused: boolean | null; visibility: string | null } {
  try {
    if (typeof document === 'undefined') return { focused: null, visibility: null };
    return { focused: typeof document.hasFocus === 'function' ? document.hasFocus() : null,
      visibility: text(document.visibilityState) };
  } catch { return { focused: null, visibility: null }; }
}

function sameContext(a: Context, b: Context): boolean {
  return a.inputSource === b.inputSource && a.xinputPrimarySlot === b.xinputPrimarySlot
    && a.virtualOutput.state === b.virtualOutput.state
    && a.virtualOutput.requestedPersona === b.virtualOutput.requestedPersona
    && a.virtualOutput.boundPersona === b.virtualOutput.boundPersona
    && a.virtualOutput.hostEpoch === b.virtualOutput.hostEpoch
    && a.virtualOutput.startProtection.requestId === b.virtualOutput.startProtection.requestId;
}

function hasActivity(payload: unknown): boolean {
  const raw = record(payload);
  if (raw?.connected !== true) return false;
  const buttons = raw.buttons;
  if (Array.isArray(buttons)) {
    for (let i = 0, end = Math.min(buttons.length, 32); i < end; ++i) {
      if (buttons[i] === true) return true;
    }
  }
  const axes = raw.axes;
  if (Array.isArray(axes)) {
    for (let i = 0, end = Math.min(axes.length, 4); i < end; ++i) {
      const value: unknown = axes[i];
      if (typeof value === 'number' && Number.isFinite(value) && Math.abs(value) > 0.12) return true;
    }
  }
  return false;
}

function increment(value: number): number {
  return Math.min(Number.MAX_SAFE_INTEGER, value + 1);
}

function histogram(): Histogram {
  return { count: 0, minMs: null, maxMs: null, sumMs: 0, saturated: false,
    buckets: new Array<number>(HISTOGRAM_UPPER_BOUNDS_MS.length + 1).fill(0) };
}

function addSample(target: Histogram, ms: number): void {
  if (!Number.isFinite(ms) || ms < 0) throw new Error('Invalid renderer trace duration');
  if (target.count === Number.MAX_SAFE_INTEGER || target.sumMs + ms > Number.MAX_SAFE_INTEGER) {
    target.saturated = true;
    return;
  }
  ++target.count;
  target.sumMs += ms;
  target.minMs = target.minMs === null ? ms : Math.min(target.minMs, ms);
  target.maxMs = target.maxMs === null ? ms : Math.max(target.maxMs, ms);
  let bucket = 0;
  while (bucket < HISTOGRAM_UPPER_BOUNDS_MS.length && ms > HISTOGRAM_UPPER_BOUNDS_MS[bucket]) ++bucket;
  ++target.buckets[bucket];
}

/** Read-only renderer evidence. Construction deliberately evaluates neither options closure. */
export function createControllerPreviewTrace(options: {
  displayPersona: () => string;
  visible: () => boolean;
}): {
  setVisible(v: boolean): void;
  personaChanged(): void;
  receipt(payload: unknown): void;
  presented(): void;
  willPatch(): void;
  didPatch(): void;
  dispose(): void;
} {
  // Instance ordinal is local to this renderer module; null preserves exhaustion.
  const previewInstanceId = previewInstanceSequence < Number.MAX_SAFE_INTEGER ? ++previewInstanceSequence : null;
  let disposed = false;
  let disabled = false;
  let announcedVisible = false;
  let armed = false;
  let personaKnown = false;
  let lastPersona: string | null = null;
  let windowsStarted = 0;
  let generation = 0;
  let active: TraceWindow | null = null;
  let ownedTimeout: number | null = null;
  let ownedRaf: number | null = null;
  let lastPerformanceMs: number | null = null;

  function now(): number {
    const stamp = performance.now();
    if (!Number.isFinite(stamp) || stamp < 0 || (lastPerformanceMs !== null && stamp < lastPerformanceMs)) {
      throw new Error('Invalid renderer trace clock');
    }
    lastPerformanceMs = stamp;
    return stamp;
  }

  function releaseOwned(ended: TraceWindow | null): void {
    const timeout = ownedTimeout;
    const raf = ownedRaf;
    ownedTimeout = null;
    ownedRaf = null;
    // Attempt both releases even if an unavailable/throwing host API rejects one.
    // Callers invalidate the generation and detach active before reaching here.
    if (timeout !== null) {
      if (ended) ended.timeout.cancelRequested = true;
      try { window.clearTimeout(timeout); }
      catch {
        if (ended) ended.timeout.cancelFailed = true;
        disabled = true;
      }
    }
    if (raf !== null) {
      if (ended) ended.raf.cancelRequestedCount = increment(ended.raf.cancelRequestedCount);
      try { window.cancelAnimationFrame(raf); }
      catch {
        if (ended) ended.raf.cancelFailedCount = increment(ended.raf.cancelFailedCount);
        disabled = true;
      }
    }
  }

  function failClosed(): void {
    disabled = true;
    armed = false;
    ++generation;
    active = null;
    releaseOwned(null);
  }

  function safely(action: () => void): void {
    try { action(); }
    catch { failClosed(); }
  }

  function finish(endedBy: EndReason, endedAtMs?: number): void {
    const ended = active;
    armed = false;
    active = null;
    ++generation;
    releaseOwned(ended);
    if (!ended) return;
    const performanceEndedAtMs = endedAtMs ?? now();
    const elapsedMs = performanceEndedAtMs - ended.performanceStartedAtMs;
    const observationDurationMs = Math.min(WINDOW_BUDGET_MS, elapsedMs);
    const detail: Record<string, unknown> = {
      schemaVersion: 1,
      stage: 'window',
      policy: 'VC-PREVIEW-TRACE-20261024-R1',
      previewInstanceId,
      windowSequence: ended.sequence,
      visibilityEvidence: { kind: 'ui-lifecycle-and-intersection; not an occlusion proof',
        atStart: ended.documentAtStart, atEnd: documentEvidence() },
      displayPersona: ended.displayPersona,
      wallclockStartedAtMs: ended.wallclockStartedAtMs,
      wallclockEndedAtMs: integer(Date.now()),
      performanceStartedAtMs: ended.performanceStartedAtMs,
      performanceEndedAtMs,
      deadlinePerformanceMs: ended.deadlinePerformanceMs,
      observationEndedAtPerformanceMs: Math.min(performanceEndedAtMs, ended.deadlinePerformanceMs),
      observationDurationMs,
      elapsedMs,
      finalizationOverrunMs: Math.max(0, elapsedMs - WINDOW_BUDGET_MS),
      endedBy,
      partial: endedBy !== 'window-budget',
      budget: { windowMs: WINDOW_BUDGET_MS, maxWindows: MAX_WINDOWS,
        windowsConsumed: windowsStarted, windowsRemaining: MAX_WINDOWS - windowsStarted },
      clock: 'renderer.performance.now; wallclock fields are labels only',
      semantics: {
        gamepadStateDeliveryCount: 'gamepad.state IPC deliveries, not physical frames',
        presentedCount: 'presenter publications',
        updateCount: 'component onUpdated calls within the window',
        latestReceiptAgeAtPatch: 'willPatch time minus latest receipt time',
        patchSpan: 'paired component willPatch to didPatch JS hook span',
        patchToRafCallback: 'didPatch to the next requested RAF callback; coalesced updates do not replace its origin',
        scope: 'renderer observations only; no native-to-game latency or GPU paint measurement',
      },
      gamepadStateDeliveryCount: ended.gamepadStateDeliveryCount,
      presentedCount: ended.presentedCount,
      updateCount: ended.updateCount,
      beforeUpdateCount: ended.beforeUpdateCount,
      overwrittenPatchStartCount: ended.overwrittenPatchStartCount,
      updateWithoutPatchStartCount: ended.updateWithoutPatchStartCount,
      patchStartWithoutCompletionAtEnd: ended.patchStartedAtMs !== null,
      context: { first: ended.firstContext, last: ended.lastContext, changed: ended.contextChangedCount,
        mixed: ended.contextChangedCount > 0, comparison: 'bounded normalized tuples; no attribution across changes' },
      // Buckets are inclusive upper bounds; null denotes the overflow bucket.
      histogramUpperBoundsMs: [...HISTOGRAM_UPPER_BOUNDS_MS, null],
      latestReceiptAgeAtPatch: ended.latestReceiptAgeAtPatch,
      patchSpan: ended.patchSpan,
      patchToRafCallback: ended.patchToRafCallback,
      timeout: ended.timeout,
      raf: { ...ended.raf,
        pendingWhenEnding: ended.rafPatchAtMs !== null,
        missingCallbackCount: ended.raf.requestedCount - ended.raf.callbackCount - ended.raf.outsideWindowCallbackCount,
        missingIncludesCancelled: true },
    };
    // Detachment precedes reporting: one attempt per consumed window, no retries.
    reportFrontendTrace('controller-preview', detail);
  }

  function isCurrent(win: TraceWindow, token: number): boolean {
    return !disposed && !disabled && active === win && generation === token;
  }

  function stillVisible(win: TraceWindow, token: number): boolean {
    if (!isCurrent(win, token)) return false;
    if (!announcedVisible || options.visible() !== true) {
      if (isCurrent(win, token)) finish('visibility-lost');
      return false;
    }
    return isCurrent(win, token);
  }

  function sampleWindow(): { win: TraceWindow; stamp: number } | null {
    const win = active;
    if (!win || !stillVisible(win, generation)) return null;
    const stamp = now();
    // Timer delivery can be delayed. Every synchronous sampler enforces the same deadline.
    if (stamp >= win.deadlinePerformanceMs) {
      finish('window-budget', stamp);
      return null;
    }
    return { win, stamp };
  }

  function begin(payload: unknown): void {
    const token = generation;
    if (options.visible() !== true) {
      if (generation === token) finish('visibility-lost');
      return;
    }
    if (!armed || !announcedVisible || disposed || disabled || generation !== token || !hasActivity(payload)) return;
    const stamp = now();
    const displayPersona = text(options.displayPersona());
    const first = context(payload);
    if (!armed || !announcedVisible || disposed || disabled || generation !== token || windowsStarted >= MAX_WINDOWS) return;
    armed = false;
    ++windowsStarted;
    const win: TraceWindow = {
      sequence: windowsStarted, displayPersona, wallclockStartedAtMs: integer(Date.now()),
      documentAtStart: documentEvidence(),
      performanceStartedAtMs: stamp, deadlinePerformanceMs: stamp + WINDOW_BUDGET_MS,
      latestReceiptAtMs: stamp, patchStartedAtMs: null, rafPatchAtMs: null,
      gamepadStateDeliveryCount: 1, presentedCount: 0, updateCount: 0, beforeUpdateCount: 0,
      overwrittenPatchStartCount: 0, updateWithoutPatchStartCount: 0,
      firstContext: first, lastContext: first, contextChangedCount: 0,
      latestReceiptAgeAtPatch: histogram(), patchSpan: histogram(), patchToRafCallback: histogram(),
      timeout: { callbackObserved: false, cancelRequested: false, cancelFailed: false },
      raf: { requestedCount: 0, callbackCount: 0, outsideWindowCallbackCount: 0,
        coalescedUpdateCount: 0, unavailableCount: 0, cancelRequestedCount: 0, cancelFailedCount: 0 },
    };
    personaKnown = true;
    lastPersona = displayPersona;
    active = win;
    const windowToken = ++generation;
    const scheduledAtMs = now();
    if (scheduledAtMs >= win.deadlinePerformanceMs) {
      finish('window-budget', scheduledAtMs);
      return;
    }
    // Exactly one owned timeout per window; never retry or reschedule an early timer.
    ownedTimeout = window.setTimeout(() => {
      safely(() => {
        if (!stillVisible(win, windowToken)) return;
        ownedTimeout = null;
        win.timeout.callbackObserved = true;
        const stamp = now();
        finish(stamp >= win.deadlinePerformanceMs ? 'window-budget' : 'timeout-early', stamp);
      });
    }, Math.ceil(win.deadlinePerformanceMs - scheduledAtMs));
  }

  return {
    setVisible(v: boolean): void {
      if (disposed || disabled) return;
      safely(() => {
        if (!v) {
          announcedVisible = false;
          finish('hidden');
          return;
        }
        if (announcedVisible) return;
        announcedVisible = true;
        finish('visible-edge');
        if (disabled || windowsStarted >= MAX_WINDOWS) return;
        const token = generation;
        const nextPersona = text(options.displayPersona());
        const visible = options.visible() === true;
        if (disposed || disabled || !announcedVisible || token !== generation) return;
        personaKnown = true;
        lastPersona = nextPersona;
        armed = visible;
      });
    },
    personaChanged(): void {
      if (disposed || disabled) return;
      safely(() => {
        // Always invalidate resources, even for a duplicate notification. Only a real change arms.
        const previous = lastPersona;
        const known = personaKnown;
        finish('persona-changed');
        if (disabled || windowsStarted >= MAX_WINDOWS) return;
        const token = generation;
        const next = text(options.displayPersona());
        const visible = announcedVisible && options.visible() === true;
        if (disposed || disabled || token !== generation) return;
        personaKnown = true;
        lastPersona = next;
        armed = visible && (!known || next !== previous);
      });
    },
    receipt(payload: unknown): void {
      // The sixth active window may finish its allocated budget. After it closes this
      // fast path reads neither a clock, an options closure nor any payload property.
      if (disposed || disabled || (windowsStarted >= MAX_WINDOWS && active === null)) return;
      if (!active && (!armed || !announcedVisible)) return;
      safely(() => {
        if (!active) { begin(payload); return; }
        const sample = sampleWindow();
        if (!sample) return;
        const { win, stamp } = sample;
        win.gamepadStateDeliveryCount = increment(win.gamepadStateDeliveryCount);
        win.latestReceiptAtMs = stamp;
        const next = context(payload);
        if (!sameContext(win.lastContext, next)) win.contextChangedCount = increment(win.contextChangedCount);
        win.lastContext = next;
      });
    },
    presented(): void {
      if (disposed || disabled || !active) return;
      safely(() => {
        const sample = sampleWindow();
        if (sample) sample.win.presentedCount = increment(sample.win.presentedCount);
      });
    },
    willPatch(): void {
      if (disposed || disabled || !active) return;
      safely(() => {
        const sample = sampleWindow();
        if (!sample) return;
        const { win, stamp } = sample;
        win.beforeUpdateCount = increment(win.beforeUpdateCount);
        if (win.patchStartedAtMs !== null) win.overwrittenPatchStartCount = increment(win.overwrittenPatchStartCount);
        win.patchStartedAtMs = stamp;
        addSample(win.latestReceiptAgeAtPatch, stamp - win.latestReceiptAtMs);
      });
    },
    didPatch(): void {
      if (disposed || disabled || !active) return;
      safely(() => {
        const sample = sampleWindow();
        if (!sample) return;
        const { win, stamp } = sample;
        win.updateCount = increment(win.updateCount);
        if (win.patchStartedAtMs === null) win.updateWithoutPatchStartCount = increment(win.updateWithoutPatchStartCount);
        else addSample(win.patchSpan, stamp - win.patchStartedAtMs);
        win.patchStartedAtMs = null;
        if (ownedRaf !== null) {
          win.raf.coalescedUpdateCount = increment(win.raf.coalescedUpdateCount);
          return;
        }
        if (typeof window.requestAnimationFrame !== 'function') {
          win.raf.unavailableCount = increment(win.raf.unavailableCount);
          return;
        }
        const token = generation;
        win.rafPatchAtMs = stamp;
        ownedRaf = window.requestAnimationFrame(() => {
          safely(() => {
            if (!stillVisible(win, token)) return;
            ownedRaf = null;
            const callbackAtMs = now(); // Ignore the RAF timestamp: all metrics use performance.now().
            if (callbackAtMs >= win.deadlinePerformanceMs) {
              win.raf.outsideWindowCallbackCount = increment(win.raf.outsideWindowCallbackCount);
              win.rafPatchAtMs = null;
              finish('window-budget', callbackAtMs);
              return;
            }
            win.raf.callbackCount = increment(win.raf.callbackCount);
            if (win.rafPatchAtMs !== null) addSample(win.patchToRafCallback, callbackAtMs - win.rafPatchAtMs);
            win.rafPatchAtMs = null;
            // No self-scheduling: only a later didPatch can request another RAF.
          });
        });
        win.raf.requestedCount = increment(win.raf.requestedCount);
      });
    },
    dispose(): void {
      if (disposed) return;
      disposed = true;
      announcedVisible = false;
      safely(() => { finish('disposed'); });
    },
  };
}
