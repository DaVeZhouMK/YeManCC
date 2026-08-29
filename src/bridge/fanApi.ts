import { http } from './api';
import { fanDiagnosticLog, getFanDiagnosticPowerGeneration } from './fanDiagnostics';

/** Main-program boundary for the imported Fan API. Disabled by default. */
export type FanPreset = 'soft' | 'balanced' | 'aggressive';
export interface FanNode { tempC: number; dutyPercent: number; }
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
export interface FanState {
  state: string;
  powerState?: string;
  hardwareWrites: boolean;
  hardwareWritesEnabled?: boolean;
  hardwareWritesObserved?: boolean;
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
  disable(leaseId?: string): Promise<FanState>;
  /** Lifecycle calls are intentionally separate from UI profile calls. */
  open(): Promise<FanState>;
  openEvents(): Promise<FanState>;
  acquireControl(): Promise<FanLease>;
  heartbeat(leaseId: string): Promise<FanLease>;
  releaseControl(leaseId: string): Promise<FanState>;
  restoreOem(leaseId?: string): Promise<FanState>;
  suspend(leaseId?: string): Promise<FanState>;
  resume(): Promise<FanState>;
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

  constructor(message: string, status: number, errorCode?: string) {
    super(message);
    this.name = 'FanApiError';
    this.status = status;
    this.errorCode = errorCode;
    this.generation = getFanDiagnosticPowerGeneration();
  }
}

function responseHeader(headers: string, name: string): string | undefined {
  const prefix = `${name.toLowerCase()}:`;
  const line = headers.split(/\r?\n/).find((entry) => entry.toLowerCase().startsWith(prefix));
  return line?.slice(prefix.length).trim() || undefined;
}

function summarizeFanState(value: unknown): Record<string, unknown> | undefined {
  if (!value || typeof value !== 'object') return undefined;
  const source = value as Record<string, unknown>;
  const summary: Record<string, unknown> = {};
  for (const key of [
    'state', 'powerState', 'hostMode', 'protocolVersion', 'hardwareCapable',
    'hardwareWriteAuthorizationGranted', 'hardwareWritesEnabled',
    'hardwareWritesObserved', 'oemRestoreConfirmed', 'hcCloseCleanupPending',
    'hcVirtualCloseReturned', 'hcDeviceManagerStopCompleted',
    'oemPhysicalOwnershipConfirmed', 'oemRestoreEvidence', 'oemOwnershipStatus', 'unknownState',
    'factoryType', 'deviceClass', 'fanRoute', 'fanRouteCount',
    'fanRouteWriteReady', 'openCalled', 'openEventsCalled',
    'hcManagerFactoryStarted', 'hcManagerFactoryIsolated', 'hcOpenEventsInvoked',
    'hcManagerStatuses',
    'hcEventMode', 'leaseGeneration', 'lastTemperatureC', 'currentDutyPercent',
    'recoveryReadbackRequired', 'profilePowerLineOnline', 'profileSelectionSource',
    'profileTemplateFingerprint', 'lastUpdateSource', 'externalProfileEventsSubscribed',
    'lastExternalProfileSource', 'reason',
  ]) {
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

function summarizeRequestBody(body?: string): Record<string, unknown> | undefined {
  if (!body) return undefined;
  try {
    const source = JSON.parse(body) as Record<string, unknown>;
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
  async suspend(_leaseId?: string): Promise<FanState> { return clone(DISABLED_STATE); }
  async resume(): Promise<FanState> { return clone(DISABLED_STATE); }
  async close(): Promise<FanState> { return clone(DISABLED_STATE); }
  async shutdown(): Promise<void> { return; }
}

/** Adapter for a separately authorized local Fan API host. Never default-constructed. */
export class HttpFanApiAdapter implements FanApiAdapter {
  readonly enabled = true;
  private sessionToken?: string;
  constructor(private readonly baseUrl: string, sessionToken?: string) { this.sessionToken = sessionToken; }
  setSessionToken(token: string): void { this.sessionToken = token; }
  private timeoutFor(path: string, overrideMs?: number): number {
    if (Number.isFinite(overrideMs) && overrideMs !== undefined && overrideMs > 0) {
      return Math.max(500, Math.floor(overrideMs));
    }
    if (path === '/api/shutdown') return 1500;
    // HC Window_Closed waits without a finite deadline for ManagerFactory
    // initialization, then owns one virtual Close. Keep the transport open
    // long enough to observe that boundary, but a timeout never authorizes a
    // second Close or proves a physical OEM handoff.
    if (path === '/api/close') return 45000;
    if (path === '/api/restore' || path === '/api/suspend') return 10000;
    return 5000;
  }
  private async request(path: string, method: 'GET' | 'POST', body?: unknown, timeoutMs?: number): Promise<any> {
    const startedAt = Date.now();
    const bodyText = body === undefined ? undefined : JSON.stringify(body);
    let responseReceived = false;
    try {
      const response = await http.request(`${this.baseUrl}${path}`, {
        method,
        headers: {
          'Content-Type': 'application/json',
          ...(this.sessionToken ? { 'X-YeMan-Fan-Session': this.sessionToken } : {}),
        },
        body: bodyText,
        timeoutMs: this.timeoutFor(path, timeoutMs),
      });
      responseReceived = true;
      const parsed = JSON.parse(response.body || '{}');
      const code = typeof parsed?.error?.code === 'string' ? parsed.error.code : undefined;
      fanDiagnosticLog('api.response', {
        method,
        path,
        status: response.status,
        ok: response.status >= 200 && response.status < 300 && parsed.ok !== false,
        durationMs: Date.now() - startedAt,
        requestId: responseHeader(response.headers, 'X-YeMan-Fan-Request-Id'),
        generation: getFanDiagnosticPowerGeneration(),
        errorCode: code,
        request: summarizeRequestBody(bodyText),
        response: summarizeFanState(parsed.state ?? parsed),
      });
      if (response.status < 200 || response.status >= 300 || parsed.ok === false) {
        // Preserve a Host error code for lifecycle safety policy. The UI still
        // translates it for users, while the lifecycle can distinguish a known
        // external-controller conflict from a retryable transport failure.
        const message = parsed?.error?.message || `Fan API 请求失败 (${response.status})`;
        throw new FanApiError(code ? `${code}: ${message}` : message, response.status, code);
      }
      return parsed;
    } catch (error) {
      fanDiagnosticLog(responseReceived ? 'api.request-failure' : 'api.transport-failure', {
        method,
        path,
        durationMs: Date.now() - startedAt,
        generation: getFanDiagnosticPowerGeneration(),
        request: summarizeRequestBody(bodyText),
        error: error instanceof Error ? error.message : String(error),
      });
      throw error;
    }
  }
  private async stateRequest(path: string, body?: unknown): Promise<FanState> {
    const parsed = await this.request(path, 'POST', body);
    return (parsed.state ?? parsed) as FanState;
  }
  handshake(timeoutMs?: number): Promise<FanHandshake> { return this.request('/api/handshake', 'POST', undefined, timeoutMs); }
  async getState(timeoutMs?: number): Promise<FanState> { return (await this.request('/api/state', 'GET', undefined, timeoutMs)).state as FanState; }
  async enable(nodes: readonly FanNode[], leaseId?: string): Promise<FanState> {
    return (await this.request('/api/enable', 'POST', { nodes, ...(leaseId ? { leaseId } : {}) })).state as FanState;
  }
  async applyPreset(name: FanPreset, leaseId?: string, nodes?: readonly FanNode[]): Promise<FanState> {
    return (await this.request('/api/preset', 'POST', { name, ...(nodes ? { nodes } : {}), ...(leaseId ? { leaseId } : {}) })).state as FanState;
  }
  async disable(leaseId?: string): Promise<FanState> {
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
  suspend(leaseId?: string): Promise<FanState> {
    return this.stateRequest('/api/suspend', leaseId ? { leaseId } : {});
  }
  resume(): Promise<FanState> { return this.stateRequest('/api/resume'); }
  close(): Promise<FanState> { return this.stateRequest('/api/close'); }
  async shutdown(): Promise<void> { await this.request('/api/shutdown', 'POST', {}); }
}

export function createFanApiAdapter(options: { enabled?: boolean; baseUrl?: string; sessionToken?: string } = {}): FanApiAdapter {
  if (!options.enabled) return new DisabledFanApiAdapter();
  return new HttpFanApiAdapter(options.baseUrl || 'http://127.0.0.1:8765', options.sessionToken);
}
