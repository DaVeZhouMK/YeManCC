/**
 * RobustSim-v0.0.23-r1
 *
 * Pure state models used to validate recovery decisions before native WebView2
 * callbacks are changed. No browser, filesystem or IPC side effects belong in
 * this file.
 */

export type WebViewFailureKind =
  | 'browser'
  | 'browser-exit'
  | 'renderer'
  | 'gpu'
  | 'utility'
  | 'unknown';

export interface WebViewFailureEvent {
  generation: number;
  environmentId: string;
  browserProcessId: number;
  kind: WebViewFailureKind;
  timestamp?: number;
}

export type WebViewRecoveryDecision =
  | 'start-recovery'
  | 'ignore-stale-generation'
  | 'ignore-duplicate'
  | 'record-overlap'
  | 'record-non-browser';

export interface WebViewRecoverySnapshot {
  generation: number;
  phase: 'healthy' | 'recovering' | 'ready';
  sourceKey: string | null;
  recoveryCount: number;
}

/**
 * A browser crash normally produces two signals. Generation is bumped when
 * recovery starts, so late callbacks from the old environment cannot touch the
 * new controller.
 */
export class WebViewRecoveryCoordinator {
  private generation = 1;
  private phase: WebViewRecoverySnapshot['phase'] = 'healthy';
  private sourceKey: string | null = null;
  private recoveryCount = 0;

  observe(event: WebViewFailureEvent): WebViewRecoveryDecision {
    if (event.generation !== this.generation) return 'ignore-stale-generation';
    if (event.kind !== 'browser' && event.kind !== 'browser-exit') return 'record-non-browser';

    const key = `${event.environmentId}:${event.browserProcessId}`;
    if (this.phase === 'recovering') {
      if (this.sourceKey === key) return 'ignore-duplicate';
      return 'record-overlap';
    }

    this.sourceKey = key;
    this.phase = 'recovering';
    this.recoveryCount += 1;
    this.generation += 1;
    return 'start-recovery';
  }

  markReady(generation = this.generation): boolean {
    if (generation !== this.generation) return false;
    this.phase = 'ready';
    return true;
  }

  resetHealthy(generation = this.generation): boolean {
    if (generation !== this.generation) return false;
    this.phase = 'healthy';
    this.sourceKey = null;
    return true;
  }

  snapshot(): WebViewRecoverySnapshot {
    return {
      generation: this.generation,
      phase: this.phase,
      sourceKey: this.sourceKey,
      recoveryCount: this.recoveryCount,
    };
  }
}

export interface WebViewHostCapabilities {
  hasVirtualHostMapping: boolean;
  hasSecureResourceFallback: boolean;
}

export type WebViewHostMode = 'virtual-host' | 'secure-resource-fallback' | 'repair-required';

export function selectWebViewHostMode(capabilities: WebViewHostCapabilities): WebViewHostMode {
  if (capabilities.hasVirtualHostMapping) return 'virtual-host';
  if (capabilities.hasSecureResourceFallback) return 'secure-resource-fallback';
  return 'repair-required';
}

export type TdpCapabilityState = 'unknown' | 'supported' | 'unsupported' | 'transient-failed';
export type TdpFailureClass = 'unsupported' | 'transient' | 'permission' | 'unknown';

export interface TdpWriteResult {
  applied: boolean;
  skipped: boolean;
  state: TdpCapabilityState;
  error?: string;
}

export function classifyTdpFailure(error: unknown): TdpFailureClass {
  const text = String(error instanceof Error ? error.message : error).toLowerCase();
  if (/unsupported|not support|not available|no transport|unknown cpu|tdp.*disabled/.test(text)) {
    return 'unsupported';
  }
  if (/access denied|permission|elevation|administrator|pawnio/.test(text)) return 'permission';
  if (/timeout|busy|temporar|resume|power transition|daemon/.test(text)) return 'transient';
  return 'unknown';
}

/**
 * All hardware writes pass through one serial queue. Unsupported capability is
 * latched for this process; transient failures are rate-limited and still
 * reject so callers can show an actionable error.
 */
export class TdpCapabilityCircuitBreaker {
  private state: TdpCapabilityState = 'unknown';
  private nextRetryAt = 0;
  private lastUnsupportedError = 'TDP capability unsupported';
  private tail: Promise<void> = Promise.resolve();

  constructor(
    private readonly now: () => number = () => Date.now(),
    private readonly transientBackoffMs = 5000,
  ) {}

  getState(): TdpCapabilityState {
    return this.state;
  }

  reset(): void {
    this.state = 'unknown';
    this.nextRetryAt = 0;
    this.lastUnsupportedError = 'TDP capability unsupported';
  }

  run(task: () => Promise<void>): Promise<TdpWriteResult> {
    const operation = this.tail.then(async (): Promise<TdpWriteResult> => {
      if (this.state === 'unsupported') {
        return { applied: false, skipped: true, state: this.state, error: this.lastUnsupportedError };
      }
      if (this.state === 'transient-failed' && this.now() < this.nextRetryAt) {
        return { applied: false, skipped: true, state: this.state, error: 'TDP hardware write is in backoff' };
      }

      try {
        await task();
        this.state = 'supported';
        this.nextRetryAt = 0;
        return { applied: true, skipped: false, state: this.state };
      } catch (error) {
        const failure = classifyTdpFailure(error);
        if (failure === 'unsupported') {
          this.state = 'unsupported';
          this.nextRetryAt = 0;
          this.lastUnsupportedError = String(error instanceof Error ? error.message : error);
          return {
            applied: false,
            skipped: true,
            state: this.state,
            error: this.lastUnsupportedError,
          };
        }
        this.state = 'transient-failed';
        this.nextRetryAt = this.now() + this.transientBackoffMs;
        throw error;
      }
    });
    this.tail = operation.then(() => undefined, () => undefined);
    return operation;
  }
}

export type RouteReadyStatus = 'route-ready' | 'route-degraded';

export interface RouteReadyToken {
  epoch: number;
  route: string;
}

export interface RouteReadyEvent extends RouteReadyToken {
  status: RouteReadyStatus;
}

export class RouteReadinessCoordinator {
  private epoch = 0;
  private active: RouteReadyToken | null = null;

  begin(route: string): RouteReadyToken {
    this.active = { epoch: ++this.epoch, route };
    return this.active;
  }

  complete(token: RouteReadyToken, status: RouteReadyStatus): RouteReadyEvent | null {
    if (!this.active || token.epoch !== this.active.epoch || token.route !== this.active.route) return null;
    return { ...token, status };
  }
}

export type SoftwareRenderMode = 'default' | 'legacy' | 'software';

export interface SoftwareRenderState {
  mode: SoftwareRenderMode;
  runtimeVersion: string;
  reason: string;
  failureCount: number;
  lastFailureAt: number;
  healthyBoots: number;
}

export function chooseSoftwareRenderMode(
  state: SoftwareRenderState | null,
  runtimeVersion: string,
  now = Date.now(),
  ttlMs = 7 * 24 * 60 * 60 * 1000,
): SoftwareRenderMode {
  if (!state || state.runtimeVersion !== runtimeVersion) return 'default';
  if (state.mode !== 'software') return state.mode;
  if (now - state.lastFailureAt > ttlMs) return 'default';
  return 'software';
}

export function recordSoftwareFailure(
  state: SoftwareRenderState | null,
  runtimeVersion: string,
  reason: string,
  now = Date.now(),
): SoftwareRenderState {
  return {
    mode: 'software',
    runtimeVersion,
    reason: reason.slice(0, 160),
    failureCount: (state?.runtimeVersion === runtimeVersion ? state.failureCount : 0) + 1,
    lastFailureAt: now,
    healthyBoots: 0,
  };
}

export function recordHealthySoftwareBoot(
  state: SoftwareRenderState | null,
  healthyBootThreshold = 3,
): SoftwareRenderState | null {
  if (!state) return null;
  const next = { ...state, healthyBoots: state.healthyBoots + 1 };
  return next.healthyBoots >= healthyBootThreshold ? null : next;
}

export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

export interface FrontendErrorRecord {
  ts: number;
  where: string;
  message: string;
  stack?: string;
  route?: string;
  generation?: string;
  build?: string;
}

export function sanitizeFrontendError(input: FrontendErrorRecord): FrontendErrorRecord {
  const stripQuery = (value: string) => value.split('?')[0].split('#')[0].slice(0, 240);
  return {
    ts: input.ts,
    where: input.where.slice(0, 100),
    message: input.message.replace(/https?:\/\/[^\s]+/gi, '[url]').slice(0, 1000),
    stack: input.stack?.replace(/https?:\/\/[^\s]+/gi, '[url]').slice(0, 4000),
    route: input.route ? stripQuery(input.route) : undefined,
    generation: input.generation?.slice(0, 80),
    build: input.build?.slice(0, 80),
  };
}

export class PersistentFrontendErrorLog {
  private writing = false;

  constructor(
    private readonly storage: StorageLike | null,
    private readonly key = 'yemancc.frontend-errors.v1',
    private readonly maxEntries = 80,
  ) {}

  append(record: FrontendErrorRecord): void {
    if (!this.storage || this.writing) return;
    this.writing = true;
    try {
      const previous = this.read();
      previous.push(sanitizeFrontendError(record));
      this.storage.setItem(this.key, JSON.stringify(previous.slice(-this.maxEntries)));
    } catch {
      // Diagnostics must never become a new application failure.
    } finally {
      this.writing = false;
    }
  }

  read(): FrontendErrorRecord[] {
    if (!this.storage) return [];
    try {
      const value = JSON.parse(this.storage.getItem(this.key) || '[]');
      return Array.isArray(value) ? value.slice(-this.maxEntries) : [];
    } catch {
      return [];
    }
  }
}
