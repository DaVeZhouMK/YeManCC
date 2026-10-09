import { app, fs } from './api';
import { parseAiFanMockSession, parseAiFanRequest, aiFanMockStateAllowed, aiFanRuntimeEvidence, projectAiFanErrorCode, type AiFanMockSession, type AiFanRequest } from './aiFanMock';
import type { FanHostLifecycle } from './fanHost';
import type { FanNode } from './fanApi';

export interface AiFanServiceDeps {
  session(): Promise<unknown>;
  read(path: string): Promise<string>;
  write(path: string, content: string): Promise<void>;
  claim(request: AiFanRequest): Promise<boolean>;
  setTimer(callback: () => void, milliseconds: number): unknown;
  clearTimer(handle: unknown): void;
  now(): string;
}
const defaultDeps: AiFanServiceDeps = {
  session: () => app.aiFanMockSession(),
  read: (path) => fs.readTextFile(path, 8192),
  write: (path, content) => fs.writeTextFileAtomic(path, content),
  claim: (request) => app.aiFanMockAcceptRequest(request),
  setTimer: (callback, ms) => window.setTimeout(callback, ms),
  clearTimer: (handle) => window.clearTimeout(handle as number),
  now: () => new Date().toISOString(),
};
export interface AiFanServiceOptions {
  lifecycle: Pick<FanHostLifecycle, 'start' | 'apply' | 'disable' | 'close' | 'getState' | 'phase' | 'getAiMockCloseReceipt'>;
  preset(name: 'soft' | 'balanced' | 'aggressive'): readonly FanNode[];
  deps?: AiFanServiceDeps;
  onError?(error: unknown): void;
}
/** AI-only bounded file channel. Normal launch returns without even reading a
 * request file or creating a timer. Every action uses the production lifecycle;
 * a native monotonic claim prevents repeats across renderer re-creation.
 */
export async function startAiFanService(options: AiFanServiceOptions): Promise<() => void> {
  const deps = options.deps ?? defaultDeps;
  let raw: unknown;
  try { raw = await deps.session(); }
  catch (error) {
    if (/^unknown: app\.aiFanMockSession$/.test(String(error instanceof Error ? error.message : error))) return () => {};
    throw error;
  }
  const session = parseAiFanMockSession(raw);
  if (!session) return () => {};
  let stopped = false;
  let timer: unknown = null;
  let lastText = '';
  async function run(request: AiFanRequest): Promise<Record<string, unknown>> {
    const before = parseAiFanMockSession(await deps.session());
    if (!before || before.sessionId !== session!.sessionId || before.parentPid !== session!.parentPid) throw new Error('AI_FAN_NATIVE_SESSION_CHANGED');
    const life = options.lifecycle;
    let closeReceipt: Record<string, unknown> | null = null;
    if (request.action === 'probe' || request.action === 'on') {
      const gate = await life.start();
      if (!gate.allowed || !gate.writeReady) throw new Error('AI_FAN_RUNTIME_HANDSHAKE_DENIED');
      // Readback, not a configuration ACK, gates the first requested control.
      const state = await life.getState();
      if (!aiFanMockStateAllowed(state)) throw new Error('AI_FAN_ZERO_WRITE_READBACK_REQUIRED');
      if (request.action === 'on') await life.apply(options.preset(request.preset ?? 'balanced'));
    } else if (request.action === 'off') {
      if (before.hostPid > 0) {
        const state = await life.getState();
        if (!aiFanMockStateAllowed(state)) throw new Error('AI_FAN_ZERO_WRITE_READBACK_REQUIRED');
        await life.disable();
      }
    } else if (request.action === 'close') {
      if (before.hostPid > 0) {
        const state = await life.getState();
        if (!aiFanMockStateAllowed(state)) throw new Error('AI_FAN_ZERO_WRITE_READBACK_REQUIRED');
        await life.close();
        closeReceipt = life.getAiMockCloseReceipt();
        if (!closeReceipt || closeReceipt.mockCloseCompleted !== true) throw new Error('AI_FAN_CLOSE_RECEIPT_REQUIRED');
      }
    }
    const after = parseAiFanMockSession(await deps.session());
    if (!after || after.sessionId !== session!.sessionId || after.parentPid !== session!.parentPid) throw new Error('AI_FAN_NATIVE_SESSION_CHANGED');
    const result: Record<string, unknown> = {
      phase: life.phase, source: 'mock', mode: 'mock-handshake', hostPid: after.hostPid,
      hostCreationTime100ns: after.hostCreationTime100ns, runtimeVerified: false,
    };
    if (closeReceipt) {
      result.closeReceipt = closeReceipt;
      result.closedHostPid = before.hostPid;
      result.closedHostCreationTime100ns = before.hostCreationTime100ns;
      result.childExited = after.hostPid === 0;
    }
    if (after.hostPid > 0) {
      const state = await life.getState();
      if (!aiFanMockStateAllowed(state)) throw new Error('AI_FAN_ZERO_WRITE_READBACK_REQUIRED');
      Object.assign(result, aiFanRuntimeEvidence(state), { runtimeVerified: true });
      const active = state.mockControlEnabled === true && Boolean(state.lease?.leaseId);
      if (request.action === 'on' && !active) throw new Error('AI_FAN_CONTROL_NOT_ACTIVE');
      if ((request.action === 'off' || request.action === 'close') && active) throw new Error('AI_FAN_CONTROL_STILL_ACTIVE');
    } else if (request.action === 'on' || request.action === 'probe') {
      throw new Error('AI_FAN_OWNED_HOST_NOT_RUNNING');
    }
    return result;
  }
  async function poll() {
    timer = null;
    let nextDelayMs = 1000; // Idle AI channel must not add a 4 Hz filesystem/IPC loop.
    if (stopped) return;
    try {
      let text: string;
      try { text = await deps.read(session!.requestPath); }
      catch { return; } // Missing request is idle, not a successful feature action.
      if (stopped || text === lastText) return;
      lastText = text;
      nextDelayMs = 250;
      let request: AiFanRequest | null = null;
      try {
        request = parseAiFanRequest(JSON.parse(text.replace(/^\uFEFF/, '')), session!);
        if (!(await deps.claim(request))) return; // No replay, no duplicate action/response.
        if (stopped) return;
        const evidence = await run(request);
        if (!stopped) await deps.write(session!.responsePath, JSON.stringify({
          schemaVersion: 1, sessionId: session!.sessionId, parentPid: session!.parentPid,
          requestId: request.requestId, sequence: request.sequence, action: request.action,
          timestampUtc: deps.now(), ok: true, evidence,
        }, null, 2));
      } catch (error) {
        options.onError?.(error);
        // R11 T-03 / R12：保留真实、已分类的失败，但只透出**显式枚举**的自有固定码——
        // 不是匹配 `AI_FAN_*` 形状。每个非自有异常（可能嵌入未受信任载荷文本）都折叠
        // 到固定码，因此不会有未受信任载荷或原始异常泄漏进响应文件。
        const errorCode = projectAiFanErrorCode(error, 'AI_FAN_REQUEST_OR_RUNTIME_FAILED');
        if (!stopped) await deps.write(session!.responsePath, JSON.stringify({
          schemaVersion: 1, sessionId: session!.sessionId, parentPid: session!.parentPid,
          requestId: request?.requestId ?? null, sequence: request?.sequence ?? null,
          action: request?.action ?? null, timestampUtc: deps.now(), ok: false,
          errorCode, runtimeVerified: false,
        }, null, 2));
      }
    } catch (error) { options.onError?.(error); }
    finally { if (!stopped) timer = deps.setTimer(() => { void poll(); }, nextDelayMs); }
  }
  void poll();
  return () => { stopped = true; if (timer !== null) deps.clearTimer(timer); timer = null; };
}