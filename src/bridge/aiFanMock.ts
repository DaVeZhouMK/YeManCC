import type { FanHandshake, FanState } from './fanApi';

/**
 * R12 T-02/T-03/T-04：本桥**自有** AI-fan 失败码的显式有限集合。
 *
 * 可信性来自"是否**属于这个确切集合**"，不是来自匹配 `AI_FAN_*` 形状——形状门会把任意
 * 合成大写串（甚至嵌入敏感标识的串）当成自有码原样透出。未知值一律折叠到调用方给定的
 * 固定码，保证没有未受信任文本或原始异常进入响应文件 / 诊断 / 免门失败收据。
 */
export const AI_FAN_KNOWN_ERROR_CODES: readonly string[] = [
  // 会话/请求解析（aiFanMock）
  'AI_FAN_SESSION_UNREADABLE', 'AI_FAN_SESSION_SCHEMA', 'AI_FAN_SESSION_INVALID',
  'AI_FAN_SESSION_REQUIRED', 'AI_FAN_TOKEN_NAMESPACE', 'AI_FAN_REQUEST_SCHEMA',
  'AI_FAN_REQUEST_REJECTED',
  // 服务运行时（aiFanService）
  'AI_FAN_NATIVE_SESSION_CHANGED', 'AI_FAN_RUNTIME_HANDSHAKE_DENIED',
  'AI_FAN_ZERO_WRITE_READBACK_REQUIRED', 'AI_FAN_CLOSE_RECEIPT_REQUIRED',
  'AI_FAN_CONTROL_NOT_ACTIVE', 'AI_FAN_CONTROL_STILL_ACTIVE', 'AI_FAN_OWNED_HOST_NOT_RUNNING',
  // Host 启动/停止（fanHost）
  'AI_FAN_HOST_NOT_FOUND', 'AI_FAN_RESIDENT_NOT_OWNED', 'AI_FAN_RESIDENT_NOT_ZERO_WRITE_MOCK',
  'AI_FAN_PORT_UNKNOWN', 'AI_FAN_PORT_OCCUPIED', 'AI_FAN_SESSION_DIRECTORY_FAILED',
  'AI_FAN_SESSION_TOKEN_NOT_PERSISTED', 'AI_FAN_MOCK_LAUNCH_FAILED', 'AI_FAN_MOCK_NOT_READY',
  'AI_FAN_NATIVE_CAPABILITY_LOST', 'AI_FAN_STOP_IDENTITY_CHANGED', 'AI_FAN_MOCK_CLOSE_NOT_CONFIRMED',
];
const AI_FAN_KNOWN_ERROR_CODE_SET: ReadonlySet<string> = new Set(AI_FAN_KNOWN_ERROR_CODES);

/** 把任意异常投影成有限自有码：属于集合则透出，其余（可能含未受信任文本）折叠到 `fallback`。 */
export function projectAiFanErrorCode(error: unknown, fallback: string): string {
  const message = error instanceof Error ? error.message : String(error);
  return AI_FAN_KNOWN_ERROR_CODE_SET.has(message) ? message : fallback;
}

export interface AiFanMockSession {
  schemaVersion: 1;
  enabled: true;
  mode: 'mock-handshake';
  sessionId: string;
  parentPid: number;
  stateDirectory: string;
  requestPath: string;
  responsePath: string;
  hostPid: number;
  hostCreationTime100ns: string;
}
const guid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
export function parseAiFanMockSession(raw: unknown): AiFanMockSession | null {
  if (!raw || typeof raw !== 'object') throw new Error('AI_FAN_SESSION_UNREADABLE');
  const x = raw as Record<string, unknown>;
  if (x.schemaVersion !== 1 || typeof x.enabled !== 'boolean') throw new Error('AI_FAN_SESSION_SCHEMA');
  if (x.enabled === false) return null;
  const id = x.sessionId;
  const dir = x.stateDirectory;
  if (x.mode !== 'mock-handshake' || typeof id !== 'string' || !guid.test(id)
    || !Number.isSafeInteger(x.parentPid) || (x.parentPid as number) <= 0
    || typeof dir !== 'string' || !/^[a-zA-Z]:\\/.test(dir) || dir.split(/[\\/]/).includes('..')
    || !dir.endsWith('\\ai-fan-sessions\\' + id + '\\fan-host')
    || x.requestPath !== dir + '\\ai-request.json' || x.responsePath !== dir + '\\ai-response.json'
    || !Number.isSafeInteger(x.hostPid) || (x.hostPid as number) < 0
    || typeof x.hostCreationTime100ns !== 'string' || !/^\d+$/.test(x.hostCreationTime100ns)
    || ((x.hostPid as number) > 0 && x.hostCreationTime100ns === '0')) {
    throw new Error('AI_FAN_SESSION_INVALID');
  }
  return x as unknown as AiFanMockSession;
}
export function aiFanMockArguments(session: AiFanMockSession, tokenPath: string): string[] {
  if (!parseAiFanMockSession(session)) throw new Error('AI_FAN_SESSION_REQUIRED');
  if (tokenPath !== session.stateDirectory + '\\YeManFanHost.session') throw new Error('AI_FAN_TOKEN_NAMESPACE');
  return ['--mock-handshake', '--mock-zero-hardware-evidence', '--port', '8765', '--protocol-version', '2',
    '--parent-pid', String(session.parentPid), '--session-token-file', tokenPath];
}
export function aiFanMockHandshakeAllowed(handshake: FanHandshake): boolean {
  return handshake.ok === true && handshake.supported === true && handshake.protocolVersion === 2
    && handshake.hostMode === 'mock-handshake' && handshake.mockZeroHardwareEvidence === true && handshake.fanRouteWriteReady === true
    && handshake.hardwareWritesEnabled === false && handshake.hardwareWritesObserved === false;
}
export function aiFanMockStateAllowed(state: FanState): boolean {
  return state.hostMode === 'mock-handshake' && Number(state.protocolVersion) === 2
    && state.hardwareWritesEnabled === false && state.hardwareWritesObserved === false
    && state.hardwareWrites !== true && state.mockZeroHardwareEvidence === true && state.unknownState !== true;
}
export type AiFanAction = 'probe' | 'on' | 'off' | 'close' | 'status';
export interface AiFanRequest {
  schemaVersion: 1; sessionId: string; parentPid: number; requestId: string;
  sequence: number; action: AiFanAction; preset?: 'soft' | 'balanced' | 'aggressive';
}
export function parseAiFanRequest(raw: unknown, session: AiFanMockSession): AiFanRequest {
  if (!raw || typeof raw !== 'object') throw new Error('AI_FAN_REQUEST_SCHEMA');
  const x = raw as Record<string, unknown>;
  const allowed = new Set(['schemaVersion', 'sessionId', 'parentPid', 'requestId', 'sequence', 'action', 'preset']);
  if (Object.keys(x).some((key) => !allowed.has(key)) || x.schemaVersion !== 1
    || x.sessionId !== session.sessionId || x.parentPid !== session.parentPid
    || typeof x.requestId !== 'string' || !guid.test(x.requestId)
    || !Number.isSafeInteger(x.sequence) || (x.sequence as number) <= 0
    || !['probe','on','off','close','status'].includes(String(x.action))
    || (x.preset !== undefined && (x.action !== 'on' || !['soft','balanced','aggressive'].includes(String(x.preset))))) {
    throw new Error('AI_FAN_REQUEST_REJECTED');
  }
  return x as unknown as AiFanRequest;
}
/** Whitelisted output only: lease ids, session tokens and authorization never leave the process. */
export function aiFanRuntimeEvidence(state: FanState): Record<string, unknown> {
  const safe: Record<string, unknown> = {};
  for (const key of ['state','powerState','hostMode','protocolVersion','hardwareWritesEnabled',
    'hardwareWritesObserved','mockZeroHardwareEvidence','mockControlEnabled','mockCloseCompleted','mockWritesObserved','mockControlSequence','openCalled','openEventsCalled','leaseGeneration',
    'lastUpdateSource']) {
    if (typeof state[key] === 'string' || typeof state[key] === 'number' || typeof state[key] === 'boolean' || state[key] === null) safe[key] = state[key];
  }
  safe.evidenceScope = 'simulated control and null-backend physical write flags; not physical OEM or HC callback readback';
  safe.leaseHeld = Boolean(state.lease && typeof state.lease.leaseId === 'string' && state.lease.leaseId);
  return safe;
}
