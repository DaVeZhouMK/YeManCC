import { http } from './api';
import { fanDiagnosticLogLazy, fanDiagnosticLoggingEnabled, getFanDiagnosticPowerGeneration } from './fanDiagnostics';

/** Main-program boundary for the imported Fan API. Disabled by default. */
export type FanPreset = 'soft' | 'balanced' | 'aggressive';
export interface FanNode { tempC: number; dutyPercent: number; }
/** Current Host curve contract, only for deciding whether a receipt is replayable. */
export function isFanReplayCurve(value: unknown): value is FanNode[] {
  if (!Array.isArray(value) || value.length !== 4) return false;
  let previousTemperature = 0, previousDuty = 0;
  for (let i = 0; i < value.length; i += 1) {
    const node = value[i];
    if (!node || typeof node !== 'object' || !Number.isFinite(node.tempC) || !Number.isFinite(node.dutyPercent)
        || node.tempC < 0 || node.tempC > 100 || node.dutyPercent < 0 || node.dutyPercent > 100
        || (i === 0 && node.tempC !== 0) || node.tempC < previousTemperature || node.dutyPercent < previousDuty) return false;
    previousTemperature = node.tempC; previousDuty = node.dutyPercent;
  }
  return previousDuty >= 50;
}
/** Version evidence is source-qualified; it is never an inferred EC scalar. */
export interface FanVersionEvidence {
  status: 'not-attempted' | 'read' | 'unsupported' | 'failed' | string;
  kind: 'ec-revision' | 'controller-firmware' | string;
  source?: string | null;
  method?: string | null;
  revision?: string | null;
  major?: number | null;
  minor?: number | null;
  verified: boolean;
  readOnly: boolean;
  reason?: string;
}
export interface FanHandshake {
  protocolVersion?: number;
  hostMode?: string;
  mockZeroHardwareEvidence?: boolean;
  hardwareCapable?: boolean;
  hardwareWritesEnabled?: boolean;
  hardwareWritesObserved?: boolean;
  ok: boolean;
  supported: boolean;
  deviceClass?: string;
  /** HC Batch-03 route family; present even when writes are not authorized. */
  fanRoute?: string;
  fanRouteCount?: number;
  fanRouteWriteReady?: boolean;
  deviceIdentity?: Record<string, unknown>;
  ecEvidence?: FanVersionEvidence | null;
  controllerFirmwareEvidence?: FanVersionEvidence | null;
  reason?: string;
}
export interface FanLease {
  leaseId: string;
  generation?: number;
  expiresAtMonoMs?: number;
  owner?: string;
}
/** The power transaction identity required by the Host's HC SystemPending
 * boundary. A suspend request without both fields is never a valid lifecycle
 * request, even when the loopback transport itself is healthy. */
export interface FanSuspendRequest {
  generation: number;
  source: string;
  leaseId?: string;
  reason?: string;
}
export interface FanResumeRequest {
  generation: number;
  source: string;
  leaseId?: string;
  reason?: string;
}
export interface FanState {
  fanCapabilitySupported?: boolean | null;
  fanControlCapabilityDeclared?: boolean | null;
  fanCapabilityEvidence?: string;
  state: string;
  powerState?: string;
  /** Protocol-2 Hosts attest the full HC lifecycle in each state snapshot. */
  protocolVersion?: string | number;
  hardwareWrites: boolean;
  hardwareWritesEnabled?: boolean;
  hardwareWritesObserved?: boolean;
  unknownState?: boolean;
  /** HC Open() and OpenEvents() completion evidence from the resident Host. */
  openCalled?: boolean;
  openEventsCalled?: boolean;
  oemRestoreConfirmed?: boolean;
  /** HC virtual Close() returned; this is separate from physical OEM proof. */
  hcVirtualCloseReturned?: boolean;
  /** DeviceManager.Stop() returned; false means ACPI/HID cleanup is pending. */
  hcDeviceManagerStopCompleted?: boolean;
  /** Only true where HC supplies a route-specific physical readback (currently ASUS). */
  oemPhysicalOwnershipConfirmed?: boolean;
  /** HC default-table restore evidence exists, but Close resource cleanup may
   * still be pending. Pending cleanup is never safe to treat as stopped. */
  hcCloseCleanupPending?: boolean;
  /** Human-readable evidence type; never implies a universal OEM ownership
   * readback because HC does not expose one across device families. */
  oemRestoreEvidence?: string;
  oemOwnershipStatus?: string;
  profilePowerLineOnline?: boolean | null;
  profileSelectionSource?: string;
  profileTemplateFingerprint?: string;
  lastUpdateSource?: string;
  externalProfileEventsSubscribed?: boolean;
  lastExternalProfileSource?: string;
  ecEvidence?: FanVersionEvidence | null;
  controllerFirmwareEvidence?: FanVersionEvidence | null;
  leaseGeneration?: number | null;
  lease?: FanLease | null;
  [key: string]: unknown;
}
export interface FanApiAdapter {
  readonly enabled: boolean;
  handshake(timeoutMs?: number): Promise<FanHandshake>;
  getState(timeoutMs?: number): Promise<FanState>;
  enable(nodes: readonly FanNode[], leaseId?: string): Promise<FanState>;
  applyPreset(name: FanPreset, leaseId?: string, nodes?: readonly FanNode[]): Promise<FanState>;
  disable(leaseId?: string, options?: { controlIntentOnly?: boolean }): Promise<FanState>;
  /** Lifecycle calls are intentionally separate from UI profile calls. */
  open(): Promise<FanState>;
  openEvents(): Promise<FanState>;
  acquireControl(): Promise<FanLease>;
  heartbeat(leaseId: string): Promise<FanLease>;
  releaseControl(leaseId: string): Promise<FanState>;
  restoreOem(leaseId?: string): Promise<FanState>;
  suspend(request: FanSuspendRequest): Promise<FanState>;
  resume(request: FanResumeRequest): Promise<FanState>;
  close(): Promise<FanState>;
  /** Request the resident Host to exit only after close/restore succeeds. */
  shutdown(): Promise<void>;
  setSessionToken?(token: string): void;
}

/** Structured transport/HTTP failure consumed by lifecycle and UI gates.
 * `message` remains compatible with existing localized error handling. */
export class FanApiError extends Error {
  readonly status: number;
  readonly errorCode?: string;
  readonly generation: number;
  /** Local request provenance; never populated from a Host error payload. */
  readonly requestPath?: string;

  constructor(message: string, status: number, errorCode?: string, requestPath?: string) {
    super(message);
    this.name = 'FanApiError';
    this.status = status;
    this.errorCode = errorCode;
    this.requestPath = requestPath;
    this.generation = getFanDiagnosticPowerGeneration();
  }
}

function responseHeader(headers: string, name: string): string | undefined {
  const prefix = `${name.toLowerCase()}:`;
  const line = headers.split(/\r?\n/).find((entry) => entry.toLowerCase().startsWith(prefix));
  return line?.slice(prefix.length).trim() || undefined;
}

// Immutable field names only; live Host values are still read on every response.
const FAN_DIAGNOSTIC_STATE_KEYS = [
    'state', 'powerState', 'hostMode', 'protocolVersion', 'ok', 'supported', 'mockZeroHardwareEvidence', 'hardwareCapable',
    'hardwareWriteAuthorizationGranted', 'hardwareWritesEnabled',
    'hardwareWritesObserved', 'oemRestoreConfirmed', 'hcCloseCleanupPending',
    'hcVirtualCloseReturned', 'hcDeviceManagerStopCompleted',
    'hcDeviceManagerStopInvoked', 'hcManagerCleanupEvidence', 'hcDeviceManagerLifecycle',
    'oemPhysicalOwnershipConfirmed', 'oemRestoreEvidence', 'oemOwnershipStatus', 'unknownState',
    'factoryType', 'deviceClass', 'fanRoute', 'fanRouteCount',
    'fanRouteWriteReady', 'openCalled', 'openEventsCalled',
    'fanCapabilitySupported', 'fanControlCapabilityDeclared', 'fanCapabilityEvidence',
    'resumePhase', 'resumePhaseGeneration', 'controlAccepting', 'retryAfterMs',
    'powerOperationGeneration', 'powerOperationAttempt', 'powerOperationStatus',
    'hcManagerFactoryStarted', 'hcManagerFactoryIsolated', 'hcOpenEventsInvoked',
    'hcManagerStatuses',
    'hcEventMode', 'leaseGeneration', 'lastTemperatureC', 'currentDutyPercent',
    'recoveryReadbackRequired', 'profilePowerLineOnline', 'profileSelectionSource',
    'profileTemplateFingerprint', 'lastUpdateSource', 'externalProfileEventsSubscribed',
    'lastExternalProfileSource', 'reason',
    'resumeAccepted', 'resumeNoop', 'controlIntentCancelled',
  ] as const;

function summarizeFanState(value: unknown): Record<string, unknown> | undefined {
  if (!value || typeof value !== 'object') return undefined;
  const source = value as Record<string, unknown>;
  const summary: Record<string, unknown> = {};
  for (const key of FAN_DIAGNOSTIC_STATE_KEYS) {
    if (key in source) summary[key] = source[key];
  }
  if (source.deviceIdentity && typeof source.deviceIdentity === 'object') summary.deviceIdentity = source.deviceIdentity;
  if (source.ecEvidence && typeof source.ecEvidence === 'object') summary.ecEvidence = source.ecEvidence;
  if (source.controllerFirmwareEvidence && typeof source.controllerFirmwareEvidence === 'object') {
    summary.controllerFirmwareEvidence = source.controllerFirmwareEvidence;
  }
  if (Array.isArray(source.activeCurve)) summary.activeCurve = source.activeCurve;
  return Object.keys(summary).length > 0 ? summary : undefined;
}

// Fixed receipt facts only. Do not mix root acceptance with a nested state or infer missing values.
function summarizeFanReceipt(value: FanResponseObject): Record<string, unknown> {
  const summary: Record<string, unknown> = {};
  for (const key of ['ok', 'supported', 'resumeAccepted', 'resumeNoop', 'controlIntentCancelled'] as const) {
    if (typeof value[key] === 'boolean') summary[key] = value[key];
  }
  return summary;
}

function summarizeRequestBody(body?: unknown): Record<string, unknown> | undefined {
  if (body === undefined || body === null) return undefined;
  try {
    // HTTP request JSON is already available as the original object at the
    // caller. Reusing it avoids a second JSON.parse on every logged heartbeat,
    // state mutation, and control request. Keep string input for old callers
    // and malformed-body diagnostics.
    const source = typeof body === 'string'
      ? JSON.parse(body) as Record<string, unknown>
      : body as Record<string, unknown>;
    if (!source || typeof source !== 'object' || Array.isArray(source)) return { bodyParse: 'failed' };
    const summary: Record<string, unknown> = {};
    if (typeof source.name === 'string') summary.preset = source.name;
    if (Array.isArray(source.nodes)) {
      summary.nodes = source.nodes.map((node) => {
        const item = node as Record<string, unknown>;
        return { tempC: item.tempC, dutyPercent: item.dutyPercent };
      });
    }
    if ('leaseId' in source) summary.leasePresent = typeof source.leaseId === 'string' && source.leaseId.length > 0;
    return summary;
  } catch {
    return { bodyParse: 'failed' };
  }
}

const DISABLED_HANDSHAKE: FanHandshake = { ok: false, supported: false, reason: 'Fan 功能尚未启用' };
const DISABLED_STATE: FanState = { state: 'Disabled', powerState: 'Unknown', hardwareWrites: false, hardwareWritesObserved: false };
const clone = <T>(value: T): T => structuredClone(value);

/** Safe adapter used by the imported-but-disabled product build. */
export class DisabledFanApiAdapter implements FanApiAdapter {
  readonly enabled = false;
  async handshake(): Promise<FanHandshake> { return clone(DISABLED_HANDSHAKE); }
  async getState(): Promise<FanState> { return clone(DISABLED_STATE); }
  async enable(_nodes: readonly FanNode[], _leaseId?: string): Promise<FanState> { throw new Error('Fan 功能尚未启用'); }
  async applyPreset(_name: FanPreset, _leaseId?: string, _nodes?: readonly FanNode[]): Promise<FanState> { throw new Error('Fan 功能尚未启用'); }
  async disable(_leaseId?: string): Promise<FanState> { return clone(DISABLED_STATE); }
  async open(): Promise<FanState> { return clone(DISABLED_STATE); }
  async openEvents(): Promise<FanState> { return clone(DISABLED_STATE); }
  async acquireControl(): Promise<FanLease> { throw new Error('Fan 功能尚未启用'); }
  async heartbeat(_leaseId: string): Promise<FanLease> { throw new Error('Fan 功能尚未启用'); }
  async releaseControl(_leaseId: string): Promise<FanState> { return clone(DISABLED_STATE); }
  async restoreOem(_leaseId?: string): Promise<FanState> { return clone(DISABLED_STATE); }
  async suspend(_request: FanSuspendRequest): Promise<FanState> { return clone(DISABLED_STATE); }
  async resume(_request: FanResumeRequest): Promise<FanState> { return clone(DISABLED_STATE); }
  async close(): Promise<FanState> { return clone(DISABLED_STATE); }
  async shutdown(): Promise<void> { return; }
}

type FanResponseObject = Record<string, unknown>;
const STATE_RESPONSE_PATHS = new Set([
  '/api/state', '/api/enable', '/api/preset', '/api/disable', '/api/cancel-control-intent',
  '/api/open', '/api/open-events', '/api/release-control', '/api/restore', '/api/suspend', '/api/resume', '/api/close',
]);
const LEGACY_ROOT_STATE_PATHS = new Set([
  '/api/open', '/api/open-events', '/api/release-control', '/api/restore', '/api/suspend', '/api/resume', '/api/close',
]);
const STATE_BOOLEAN_FIELDS = [
  'hardwareWrites', 'hardwareWritesEnabled', 'hardwareWritesObserved', 'unknownState', 'openCalled', 'openEventsCalled',
  'oemRestoreConfirmed', 'hcVirtualCloseReturned', 'hcDeviceManagerStopCompleted',
  'oemPhysicalOwnershipConfirmed', 'hcCloseCleanupPending', 'controlAccepting',
] as const;

function isFanResponseObject(value: unknown): value is FanResponseObject {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

// Non-global matcher: reuse only fixed grammar, never a previous validity result.
const FAN_LEASE_ID_PATTERN = /^[A-Za-z0-9_-]{8,80}$/;
// Native send/header errors can lose a committed POST just like a deadline.
const FAN_HTTP_TRANSPORT_FAILURE_PATTERN = /IPC command timed out: http.request|HTTP request (?:deadline exceeded|cancelled|timed out)|WinHTTP 12002|HTTP request failed \(WinHTTP [0-9]+, phase=(?:send|headers|body)\)/;

/** Shared native transport grammar; neither a write receipt nor retry permission. */
export function isFanHttpTransportFailure(error: unknown): boolean {
  return FAN_HTTP_TRANSPORT_FAILURE_PATTERN.test(error instanceof Error ? error.message : String(error));
}

function validFanResponseLease(value: unknown): value is FanLease {
  if (!isFanResponseObject(value) || typeof value.leaseId !== 'string' || !FAN_LEASE_ID_PATTERN.test(value.leaseId)) return false;
  if (value.generation !== undefined && (!Number.isSafeInteger(value.generation) || (value.generation as number) <= 0)) return false;
  if (value.expiresAtMonoMs !== undefined && (!Number.isFinite(value.expiresAtMonoMs) || (value.expiresAtMonoMs as number) <= 0)) return false;
  return value.owner === undefined || typeof value.owner === 'string';
}

function validFanResponseState(value: unknown): value is FanState {
  if (!isFanResponseObject(value) || typeof value.state !== 'string' || !value.state.trim()) return false;
  // Optional fields remain optional; when supplied, they must retain their wire types.
  for (let i = 0; i < STATE_BOOLEAN_FIELDS.length; i += 1) {
    const field = value[STATE_BOOLEAN_FIELDS[i]];
    if (field !== undefined && typeof field !== 'boolean') return false;
  }
  if (value.lease !== undefined && value.lease !== null && !validFanResponseLease(value.lease)) return false;
  return value.leaseGeneration === undefined || value.leaseGeneration === null ||
    (Number.isSafeInteger(value.leaseGeneration) && (value.leaseGeneration as number) > 0);
}

function fanResponseState(path: string, response: FanResponseObject): unknown {
  // A legacy lifecycle reply may put State's string at the root rather than
  // put a State object under `state`. Keep that documented fallback usable.
  if (LEGACY_ROOT_STATE_PATHS.has(path) && typeof response.state === 'string') return response;
  return response.state ?? (LEGACY_ROOT_STATE_PATHS.has(path) ? response : undefined);
}

function validFanResponseReceipt(path: string, response: FanResponseObject, requestBody: unknown): boolean {
  if (response.ok !== undefined && typeof response.ok !== 'boolean') return false;
  if (path === '/api/handshake') {
    if (typeof response.ok !== 'boolean' || typeof response.supported !== 'boolean') return false;
    // Capability observations retain their wire types. A truthy string or a
    // contradictory receipt must never reach the route's write admission.
    for (const key of ['mockZeroHardwareEvidence', 'hardwareCapable', 'hardwareWritesEnabled', 'hardwareWritesObserved', 'fanRouteWriteReady']) {
      if (response[key] !== undefined && typeof response[key] !== 'boolean') return false;
    }
    if (response.protocolVersion !== undefined && (!Number.isSafeInteger(response.protocolVersion) || (response.protocolVersion as number) <= 0)) return false;
    if (response.hostMode === 'real-hc-unavailable' && response.supported) return false;
    // A fault-locked re-probe keeps prior write evidence until restoration.
    // Capability loss cannot erase that debt or make the receipt malformed.
    if (response.hardwareCapable === false && (response.supported || response.fanRouteWriteReady === true)) return false;
    if (response.supported === false && response.fanRouteWriteReady === true) return false;
    if (response.mockZeroHardwareEvidence === true && (response.hardwareWritesEnabled === true || response.hardwareWritesObserved === true)) return false;
    // A ready route with writes currently disabled is valid before acquisition,
    // including the zero-hardware mock. Missing optional observations stay absent.
    return true;
  }
  if (STATE_RESPONSE_PATHS.has(path)) {
    if (path === '/api/cancel-control-intent' && response.controlIntentCancelled !== true) return false;
    return validFanResponseState(fanResponseState(path, response));
  }
  if (path === '/api/acquire-control' || path === '/api/heartbeat') {
    const lease = response.lease ?? (isFanResponseObject(response.result) ? response.result.lease : undefined);
    return validFanResponseLease(lease) && (path !== '/api/heartbeat' ||
      (isFanResponseObject(requestBody) && lease.leaseId === requestBody.leaseId));
  }
  return true;
}

/** Adapter for a separately authorized local Fan API host. Never default-constructed. */
export class HttpFanApiAdapter implements FanApiAdapter {
  readonly enabled = true;
  private sessionToken?: string;
  private requestHeaders: Readonly<Record<string, string>>;
  constructor(private readonly baseUrl: string, sessionToken?: string) {
    this.sessionToken = sessionToken;
    this.requestHeaders = this.createRequestHeaders();
  }
  setSessionToken(token: string): void {
    if (token === this.sessionToken) return;
    this.sessionToken = token;
    this.requestHeaders = this.createRequestHeaders();
  }
  private createRequestHeaders(): Readonly<Record<string, string>> {
    // Replace on rotation so in-flight requests retain their original credential.
    // Only fixed request data is reused; the Host authenticates every request.
    return Object.freeze({
      'Content-Type': 'application/json',
      ...(this.sessionToken ? { 'X-YeMan-Fan-Session': this.sessionToken } : {}),
    });
  }
  private timeoutFor(path: string, overrideMs?: number): number {
    if (Number.isFinite(overrideMs) && overrideMs !== undefined && overrideMs > 0) {
      // Shared http.request and native timeoutBudget already use 250ms.
      // Do not inflate a caller's remaining 250-499ms probe budget to 500ms.
      return Math.max(250, Math.floor(overrideMs));
    }
    if (path === '/api/shutdown') return 1500;
    // The HC STA boundary allows 15s. The first authenticated handshake owns
    // capability initialization; it and Open/events must not be cut off at 5s.
    if (path === '/api/handshake' || path === '/api/open' || path === '/api/open-events') return 16000;
    // HC Window_Closed waits without a finite deadline for ManagerFactory
    // initialization, then owns one virtual Close. Keep the transport open
    // long enough to observe that boundary, but a timeout never authorizes a
    // second Close or proves a physical OEM handoff.
    if (path === '/api/close') return 45000;
    if (path === '/api/restore' || path === '/api/suspend') return 10000;
    // HC SystemReady keeps a 10-second device-readiness budget plus the
    // Open/OpenEvents/lease/curve rebuild; the Host's automatic resume worker
    // waits up to 15 seconds (WaitForAutomaticResumeAsync). A shorter bridge
    // timeout would truncate a slow post-sleep rebuild into a false F5 failure
    // and stall the wake transaction in native `resuming`.
    if (path === '/api/resume') return 16000;
    return 5000;
  }
  private invalidResponse(path: string, method: 'GET' | 'POST', status: number): FanApiError {
    // Mutations may have committed even when their reply is unusable. Use the
    // existing unknown-result policy; never classify this as permission to replay.
    const unknown = method === 'POST' && path !== '/api/handshake';
    const code = unknown ? 'FAN_HTTP_RESULT_UNKNOWN' : 'FAN_API_RESPONSE_INVALID';
    return new FanApiError(`${code}: 风扇返回收据无效，请核对实际状态后重试`, status, code, path);
  }
  private async request(path: string, method: 'GET' | 'POST', body?: unknown, timeoutMs?: number): Promise<any> {
    const startedAt = Date.now();
    const bodyText = body === undefined ? undefined : JSON.stringify(body);
    let responseReceived = false;
    let responseStatus: number | undefined;
    let diagnosticRequestId: string | undefined;
    try {
      const response = await http.request(`${this.baseUrl}${path}`, {
        method,
        headers: this.requestHeaders,
        body: bodyText,
        timeoutMs: this.timeoutFor(path, timeoutMs),
      });
      responseReceived = true;
      responseStatus = response.status;
      // Read this optional log correlation only when enabled; a malformed header must not affect control.
      if (fanDiagnosticLoggingEnabled.value) {
        try { diagnosticRequestId = responseHeader(response.headers, 'X-YeMan-Fan-Request-Id'); } catch { /* diagnostic-only */ }
      }
      if (response.bodyComplete !== undefined && response.bodyComplete !== true) {
        // The Host may have committed a POST. Do not parse/promote its prefix
        // or turn missing body proof into an automatic retry. HTTP auth status
        // remains authoritative even when its error body did not finish.
        if (response.status < 200 || response.status >= 300)
          throw new FanApiError(`Fan API 请求失败 (${response.status})`, response.status, undefined, path);
        throw this.invalidResponse(path, method, response.status);
      }
      let decoded: unknown;
      try { decoded = JSON.parse(response.body || '{}'); }
      catch {
        if (response.status < 200 || response.status >= 300) {
          throw new FanApiError(`Fan API 请求失败 (${response.status})`, response.status, undefined, path);
        }
        throw this.invalidResponse(path, method, response.status);
      }
      if (!isFanResponseObject(decoded)) {
        if (response.status < 200 || response.status >= 300) {
          throw new FanApiError(`Fan API 请求失败 (${response.status})`, response.status, undefined, path);
        }
        throw this.invalidResponse(path, method, response.status);
      }
      const parsed = decoded;
      const hostError = isFanResponseObject(parsed.error) ? parsed.error : undefined;
      const code = typeof hostError?.code === 'string' ? hostError.code : undefined;
      const receiptValid = validFanResponseReceipt(path, parsed, body);
      if (fanDiagnosticLoggingEnabled.value) {
        fanDiagnosticLogLazy('api.response', () => ({
          observationSchemaVersion: 2,
          method,
          path,
          status: response.status,
          ok: response.status >= 200 && response.status < 300 && parsed.ok !== false && receiptValid,
          durationMs: Date.now() - startedAt,
          requestId: diagnosticRequestId,
          receiptValid,
          receipt: summarizeFanReceipt(parsed),
          bodyCompleteObserved: typeof response.bodyComplete === 'boolean',
          ...(typeof response.bodyComplete === 'boolean' ? { bodyComplete: response.bodyComplete } : {}),
          generation: getFanDiagnosticPowerGeneration(),
          errorCode: code,
          request: summarizeRequestBody(body),
          response: summarizeFanState(parsed.state ?? parsed),
        }));
      }
      if (response.status < 200 || response.status >= 300 || parsed.ok === false) {
        // Preserve a Host error code for lifecycle safety policy. The UI still
        // translates it for users, while the lifecycle can distinguish a known
        // external-controller conflict from a retryable transport failure.
        const message = (typeof hostError?.message === 'string' ? hostError.message : undefined) || `Fan API 请求失败 (${response.status})`;
        throw new FanApiError(code ? `${code}: ${message}` : message, response.status, code, path);
      }
      if (!receiptValid) throw this.invalidResponse(path, method, response.status);
      return parsed;
    } catch (error) {
      if (fanDiagnosticLoggingEnabled.value) {
        fanDiagnosticLogLazy(responseReceived ? 'api.request-failure' : 'api.transport-failure', () => ({
          observationSchemaVersion: 2,
          method,
          path,
          durationMs: Date.now() - startedAt,
          generation: getFanDiagnosticPowerGeneration(),
          responseReceived,
          ...(responseStatus !== undefined ? { status: responseStatus } : {}),
          ...(diagnosticRequestId ? { requestId: diagnosticRequestId } : {}),
          ...(error instanceof FanApiError && error.errorCode ? { errorCode: error.errorCode } : {}),
          request: summarizeRequestBody(body),
          error: error instanceof Error ? error.message : String(error),
        }));
      }
      // Transport failure is not a refusal: the Host may have committed the POST.
      // Reconcile native send/header errors without replaying an uncertain mutation.
      if (!responseReceived && method === 'POST' && error instanceof Error && !(error instanceof FanApiError)
          && isFanHttpTransportFailure(error)) {
        throw new FanApiError('FAN_HTTP_RESULT_UNKNOWN: 请求回执未确认，正在核对风扇实际状态', 0, 'FAN_HTTP_RESULT_UNKNOWN', path);
      }
      throw error;
    }
  }
  private async stateRequest(path: string, body?: unknown): Promise<FanState> {
    const parsed = await this.request(path, 'POST', body);
    return fanResponseState(path, parsed) as FanState;
  }
  handshake(timeoutMs?: number): Promise<FanHandshake> { return this.request('/api/handshake', 'POST', undefined, timeoutMs); }
  async getState(timeoutMs?: number): Promise<FanState> { return (await this.request('/api/state', 'GET', undefined, timeoutMs)).state as FanState; }
  async enable(nodes: readonly FanNode[], leaseId?: string): Promise<FanState> {
    return (await this.request('/api/enable', 'POST', { nodes, ...(leaseId ? { leaseId } : {}) })).state as FanState;
  }
  async applyPreset(name: FanPreset, leaseId?: string, nodes?: readonly FanNode[]): Promise<FanState> {
    return (await this.request('/api/preset', 'POST', { name, ...(nodes ? { nodes } : {}), ...(leaseId ? { leaseId } : {}) })).state as FanState;
  }
  async disable(leaseId?: string, options?: { controlIntentOnly?: boolean }): Promise<FanState> {
    if (options?.controlIntentOnly) {
      // A separate route makes old Hosts fail closed with 404 rather than
      // misinterpret an unknown flag as an OEM write while asleep.
      const response = await this.request('/api/cancel-control-intent', 'POST', {});
      if (response.controlIntentCancelled !== true) throw new Error('Fan Host 未确认控制意图取消');
      return response.state as FanState;
    }
    return (await this.request('/api/disable', 'POST', leaseId ? { leaseId } : {})).state as FanState;
  }
  open(): Promise<FanState> { return this.stateRequest('/api/open'); }
  openEvents(): Promise<FanState> { return this.stateRequest('/api/open-events'); }
  async acquireControl(): Promise<FanLease> {
    const parsed = await this.request('/api/acquire-control', 'POST', {});
    return (parsed.lease ?? parsed.result?.lease) as FanLease;
  }
  async heartbeat(leaseId: string): Promise<FanLease> {
    const parsed = await this.request('/api/heartbeat', 'POST', { leaseId });
    return (parsed.lease ?? parsed.result?.lease) as FanLease;
  }
  releaseControl(leaseId: string): Promise<FanState> {
    return this.stateRequest('/api/release-control', { leaseId });
  }
  restoreOem(leaseId?: string): Promise<FanState> {
    return this.stateRequest('/api/restore', leaseId ? { leaseId } : {});
  }
  suspend(request: FanSuspendRequest): Promise<FanState> {
    if (!Number.isSafeInteger(request.generation) || request.generation <= 0 || typeof request.source !== 'string' || !request.source.trim()) {
      return Promise.reject(new Error('POWER_GENERATION_REQUIRED: Fan suspend 需要有效 generation/source'));
    }
    return this.stateRequest('/api/suspend', {
      generation: request.generation,
      source: request.source,
      ...(request.leaseId ? { leaseId: request.leaseId } : {}),
      ...(request.reason ? { reason: request.reason } : {}),
    });
  }
  resume(request: FanResumeRequest): Promise<FanState> {
    if (!Number.isSafeInteger(request.generation) || request.generation <= 0 || typeof request.source !== 'string' || !request.source.trim()) {
      return Promise.reject(new Error('POWER_GENERATION_REQUIRED: Fan resume 需要有效 generation/source'));
    }
    return this.stateRequest('/api/resume', {
      generation: request.generation,
      source: request.source,
      ...(request.leaseId ? { leaseId: request.leaseId } : {}),
      ...(request.reason ? { reason: request.reason } : {}),
    });
  }
  close(): Promise<FanState> { return this.stateRequest('/api/close'); }
  async shutdown(): Promise<void> { await this.request('/api/shutdown', 'POST', {}); }
}

export function createFanApiAdapter(options: { enabled?: boolean; baseUrl?: string; sessionToken?: string } = {}): FanApiAdapter {
  if (!options.enabled) return new DisabledFanApiAdapter();
  return new HttpFanApiAdapter(options.baseUrl || 'http://127.0.0.1:8765', options.sessionToken);
}
