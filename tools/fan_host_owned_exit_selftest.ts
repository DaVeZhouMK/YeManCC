/** FAN-941: production NativeFanHostLauncher exit proof; native IPC alone is a fixture.
 * No process is launched or terminated. Tests root-exit failure, PID reuse,
 * unreadable identity, and capture at authenticated resident adoption.
 */
import assert from 'node:assert/strict';
import { webcrypto } from 'node:crypto';
if (!(globalThis as any).crypto) (globalThis as any).crypto = webcrypto;
if (typeof (globalThis as any).CustomEvent === 'undefined') {
  (globalThis as any).CustomEvent = class extends Event {
    detail: unknown;
    constructor(type: string, p?: { detail?: unknown }) { super(type); this.detail = p?.detail; }
  };
}
const EXE = 'C:\\fixture\\YeManFanHost.exe', PID = 9411, CREATED = '133001111111111111';
const TOKEN = '0123456789abcdef'.repeat(4);
let current: any = { valid: false, pid: PID };
let exact: any = { ok: false, matched: true, exited: false, reason: 'still-running' };
let tree: any = { ok: false, attempted: 1, terminated: 0 };
let calls: Array<{ cmd: string; args: any }> = [];
let listener: ((event: { data: any }) => void) | undefined;
let handoff: HandoffFixture | null = null;
type HandoffFixture = {
  kind: string; stateReads: number; perfReads: number; identityReads: number;
  oldExited: boolean; launched: boolean; current: boolean; trace: string[];
};
const NEW_PID = PID + 1, NEW_CREATED = '133002222222222222';
const realTimer = globalThis.setTimeout, realPerformance = globalThis.performance;
let virtualNow: number | null = null;
// Deterministic passage of the unchanged 4s/16s product observation windows.
Object.defineProperty(globalThis, 'performance', { configurable: true,
  value: { now: () => virtualNow ?? realPerformance.now() } });
(globalThis as any).setTimeout = (fn: (...args: any[]) => void, ms?: number, ...args: any[]) => {
  if (virtualNow !== null && (ms === 100 || ms === 250)) {
    virtualNow += ms; queueMicrotask(() => fn(...args)); return 0 as any;
  }
  return realTimer(fn, ms, ...args);
};
const retired = () => ({ state: 'FaultLocked', unknownState: true, oemRestoreConfirmed: false,
  hcVirtualCloseReturned: true, hcDeviceManagerStopCompleted: true,
  hcCloseCleanupPending: false, openCalled: false, openEventsCalled: false,
  hardwareWritesEnabled: false, hardwareWrites: false, hardwareWritesObserved: true,
  controlAccepting: false });
function handoffState(f: HandoffFixture) {
  if (f.launched || f.kind === 'live' || f.kind === 'adopt-pid-reuse') return { ...retired(), state: 'Ready', unknownState: false };
  if (f.kind === 'pending' || f.kind === 'off-while-pending' || f.kind === 'generation-while-pending') return { ...retired(), hcCloseCleanupPending: true, hcVirtualCloseReturned: false };
  if (f.kind === 'pending-settles' && f.stateReads < 3)
    return { ...retired(), hcCloseCleanupPending: true, hcVirtualCloseReturned: false };
  if (f.kind === 'live-writes') return { ...retired(), hcVirtualCloseReturned: false,
    openCalled: true, hardwareWritesEnabled: true };
  return retired();
}
async function handoffInvoke(cmd: string, a: any, f: HandoffFixture): Promise<any> {
  if (cmd === 'process.identity') {
    f.identityReads++;
    if (a.pid === NEW_PID && f.launched) return { valid: true, pid: NEW_PID, processCreated: NEW_CREATED, path: EXE };
    if (a.pid === 77) return { valid: true, pid: 77, processCreated: CREATED, path: 'C:\\foreign.exe' };
    if (a.pid !== PID || f.oldExited || f.kind === 'unreadable') return { valid: false, pid: a.pid };
    return { valid: true, pid: PID, processCreated: ((f.kind === 'pid-reuse' && f.identityReads > 1) || (f.kind === 'adopt-pid-reuse' && f.identityReads > 2)) ? NEW_CREATED : CREATED, path: EXE };
  }
  if (cmd === 'process.terminateExact') {
    if (a.pid === NEW_PID && f.launched) {
      assert.deepEqual(a, { pid: NEW_PID, processCreated: NEW_CREATED, path: EXE });
      assert.equal(f.current, false); f.trace.push('cancelled-child-exit');
      return { ok: true, matched: true, exited: true, reason: 'exited' };
    }
    f.trace.push('exit-proof');
    assert.deepEqual(a, { pid: PID, processCreated: CREATED, path: EXE });
    assert.equal(f.kind === 'pending' || f.kind === 'live-writes', false, 'must not kill unresolved hardware owner');
    assert.equal(f.current, true, 'cancelled start must not terminate an owner');
    if (f.kind === 'exit-unproven') return { ok: false, matched: true, exited: false, reason: 'open-denied' };
    f.oldExited = true;
    if (f.kind === 'off-after-exit' || f.kind === 'new-generation') f.current = false;
    return { ok: true, matched: true, exited: true, reason: 'exited' };
  }
  if (cmd === 'proc.findExact') return { found: !f.oldExited, pid: f.oldExited ? 0 : PID };
  if (cmd === 'shell.run') {
    assert.equal(String(a.program).toLowerCase(), 'netstat.exe', 'fixture never runs deployment installer');
    return { exitCode: 0, stdout: f.oldExited ? '' : ' TCP 127.0.0.1:8765 0.0.0.0:0 LISTENING ' + (f.kind === 'foreign' ? 77 : 4), stderr: '' };
  }
  if (cmd === 'http.request') {
    const endpoint = new URL(a.url).pathname; f.trace.push(endpoint);
    if (f.oldExited && !f.launched) throw new Error('ECONNREFUSED');
    if (endpoint === '/api/perf-counters') {
      f.perfReads++;
      if (f.kind === '401' || f.kind === '403') return { status: Number(f.kind), body: '{}', headers: '' };
      if (f.kind === 'perf-timeout') throw new Error('IPC command timed out: http.request');
      return { status: 200, body: JSON.stringify({ ok: true,
        processId: f.kind === 'wrong-pid' ? PID + 20 : PID,
        hostInstanceId: f.kind === 'missing-instance' ? undefined :
          f.kind === 'changed-instance' && f.perfReads > 1 ? 'other-instance' : 'owned-instance' }), headers: '' };
    }
    if (endpoint === '/api/state') {
      f.stateReads++;
      if (['off-before-exit', 'off-while-pending', 'generation-while-pending'].includes(f.kind) && f.stateReads >= 2) f.current = false;
      return { status: 200, body: JSON.stringify({ ok: true, state: handoffState(f) }), headers: '' };
    }
    if (endpoint === '/health' && f.launched && ['off-during-ready', 'generation-during-ready'].includes(f.kind)) f.current = false;
    if (endpoint === '/health') return { status: 200,
      body: JSON.stringify({ ok: true, host: 'YeManFanHost', protocolVersion: 2, state: handoffState(f) }), headers: '' };
    if (endpoint === '/api/close') return { status: 200,
      body: JSON.stringify({ ok: true, state: handoffState(f) }), headers: '' };
    throw new Error('unexpected handoff endpoint ' + endpoint);
  }
  if (cmd === 'shell.hidden') {
    assert.equal(f.oldExited, true, 'exact old-instance exit must precede replacement launch');
    assert.equal(f.current, true, 'OFF/new-generation must cancel replacement launch');
    assert.equal(f.launched, false, 'one request launches at most one replacement');
    f.launched = true; f.trace.push('launch'); return { ok: true, pid: NEW_PID };
  }
  if (cmd === 'app.pid') return 20000;
  if (cmd === 'fs.exists') return !/YeManFanBackendV3Adapter\.dll$/i.test(String(a.path));
  if (cmd === 'fs.sha256File') return 'a'.repeat(64);
  if (cmd === 'fs.readTextFile' || cmd === 'fs.readTextRange') {
    let content: string;
    if (/YeManFanHost\.payload\.json$/i.test(a.path)) content = JSON.stringify({ schemaVersion: 2,
      runtimeId: 'owned-runtime', runtimeManifest: '..\\handheldcompanion-runtime\\owned-runtime\\HandheldCompanion.runtime.json',
      files: [{ path: 'YeManFanHost.dll', sha256: 'a'.repeat(64) }] });
    else if (/HandheldCompanion\.runtime\.json$/i.test(a.path)) content = JSON.stringify({ schemaVersion: 1,
      runtimeId: 'owned-runtime', files: [{ path: 'HandheldCompanion.dll', sha256: 'a'.repeat(64) }] });
    else if (/YeManFanHost\.deployment\.json$/i.test(a.path)) content = JSON.stringify({ schemaVersion: 1,
      validationRulesVersion: 1, result: 'ok', runtimeId: 'owned-runtime',
      payloadRoot: 'c:\\fixture\\fan-host-v2', runtimeRoot: 'c:\\fixture\\handheldcompanion-runtime\\owned-runtime',
      payloadManifestSha256: 'a'.repeat(64), runtimeManifestSha256: 'a'.repeat(64) });
    else content = TOKEN;
    return cmd === 'fs.readTextFile' ? content : { content, offset: 0, size: Buffer.byteLength(content) };
  }
  return undefined;
}
async function invoke(cmd: string, args: any): Promise<any> {
  calls.push({ cmd, args });
  if (handoff) { const result = await handoffInvoke(cmd, args, handoff); if (result !== undefined) return result; }
  if (cmd === 'process.identity') return current;
  if (cmd === 'process.terminateExact') return exact;
  if (cmd === 'process.terminateTree') return tree;
  if (cmd === 'app.aiFanMockSession') return { schemaVersion: 1, enabled: false };
  if (cmd === 'app.dataDir') return 'C:\\fixture-data';
  if (cmd === 'app.fanStateDir') return 'C:\\fixture-state';
  if (cmd === 'fs.exists') return /(?:yemanfanhost\.exe|yemanfanhost\.session)$/i.test(String(args.path));
  if (cmd === 'fs.mkdir') return true;
  if (cmd === 'fs.readTextFile') return TOKEN;
  if (cmd === 'fs.readTextRange') return { content: TOKEN, offset: 0, size: Buffer.byteLength(TOKEN) };
  if (cmd === 'shell.run') return { exitCode: 0, stdout: ' TCP 127.0.0.1:8765 0.0.0.0:0 LISTENING ' + PID, stderr: '' };
  if (cmd === 'proc.findExact') return { found: true, pid: PID };
  if (cmd === 'http.request') return { status: 200, body: JSON.stringify({ ok: true, state: { state: 'Ready' } }), headers: '' };
  if (cmd === 'app.fanActivity' || cmd === 'app.fanLifecycleEvidence') return null;
  throw new Error('unexpected native command: ' + cmd);
}
const win = new EventTarget() as any;
win.setTimeout = setTimeout; win.clearTimeout = clearTimeout;
win.chrome = { webview: {
  addEventListener(_type: string, fn: typeof listener) { listener = fn; },
  postMessage(msg: any) { void invoke(msg.cmd, msg.args ?? {}).then(
    result => listener?.({ data: { id: msg.id, result } }),
    error => listener?.({ data: { id: msg.id, error: String(error) } }),
  ); },
} };
(globalThis as any).window = win;
// The real shared UI lifecycle expects document when a native window fixture exists.
(globalThis as any).document = Object.assign(new EventTarget(), { visibilityState: 'visible', hidden: false, createElement: () => ({ content: {}, style: {} }) });
const { NativeFanHostLauncher, resolveFanHostConfig } = await import('../src/bridge/fanHost');
const owned = { pid: PID, executable: EXE, processCreated: CREATED };
const reset = () => { calls = []; current = { valid: false, pid: PID }; exact = { ok: false, matched: true, exited: false, reason: 'still-running' }; };
let passed = 0;
async function run(name: string, fn: () => Promise<void>) { await fn(); passed++; console.log('PASS ' + name); }
await run('root termination denied must reject even if the request itself resolves', async () => {
  reset(); tree = { ok: false, attempted: 1, terminated: 0 };
  await assert.rejects(new NativeFanHostLauncher().stop(owned));
  assert.equal(calls.filter(c => c.cmd === 'process.terminateTree').length, 0);
  assert.deepEqual(calls.find(c => c.cmd === 'process.terminateExact')?.args, { pid: PID, processCreated: CREATED, path: EXE });
});
await run('only a child exited cannot release the root owner', async () => {
  reset(); tree = { ok: true, attempted: 2, terminated: 1 };
  await assert.rejects(new NativeFanHostLauncher().stop(owned));
});
await run('missing captured creation time cannot bind a replacement PID during stop', async () => {
  reset(); current = { valid: true, pid: PID, processCreated: CREATED, path: EXE };
  await assert.rejects(new NativeFanHostLauncher().stop({ pid: PID, executable: EXE }));
  assert.equal(calls.filter(c => c.cmd.startsWith('process.terminate')).length, 0);
});
await run('proven PID reuse means the old instance exited without killing the new process', async () => {
  reset(); current = { valid: true, pid: PID, processCreated: '133009999999999999', path: EXE };
  await new NativeFanHostLauncher().stop(owned);
  assert.equal(calls.filter(c => c.cmd.startsWith('process.terminate')).length, 0);
});
await run('identity-mismatch with unreadable fields is not an exit receipt', async () => {
  reset(); exact = { ok: false, matched: false, exited: false, reason: 'identity-mismatch' };
  await assert.rejects(new NativeFanHostLauncher().stop(owned));
});
await run('kernel not-found confirms the original instance is gone', async () => {
  reset(); exact = { ok: false, matched: false, exited: false, reason: 'not-found' };
  await new NativeFanHostLauncher().stop(owned);
});
await run('exact native root exit receipt is accepted', async () => {
  reset(); exact = { ok: true, matched: true, exited: true, reason: 'exited' };
  await new NativeFanHostLauncher().stop(owned);
});
await run('authenticated adoption captures the identity before returning its owner', async () => {
  reset(); current = { valid: true, pid: PID, processCreated: CREATED, path: EXE };
  const config = resolveFanHostConfig('C:\\fixture');
  config.hostExecutable = EXE; config.sessionToken = TOKEN;
  const result = await new NativeFanHostLauncher().start(config);
  assert.equal(result.adopted, true); assert.equal(result.pid, PID);
  assert.equal((result as any).processCreated, CREATED);
  assert.equal(calls.filter(c => c.cmd.startsWith('process.terminate')).length, 0);
});
await run('unreadable initial identity rejects adoption instead of publishing an unfinishable owner, and the next attempt remains valid', async () => {
  reset();
  const launcher = new NativeFanHostLauncher(), config = resolveFanHostConfig('C:\\fixture');
  config.hostExecutable = EXE; config.sessionToken = TOKEN;
  await assert.rejects(launcher.start(config), /实例身份暂不可确认/);
  assert.equal(calls.filter(c => c.cmd.startsWith('process.terminate')).length, 0);
  current = { valid: true, pid: PID, processCreated: CREATED, path: EXE };
  const retry = await launcher.start(config);
  assert.equal(retry.adopted, true); assert.equal((retry as any).processCreated, CREATED);
});
console.log('FAN-941 original owned Host exit proof: ' + passed + '/9 PASS');
for (const kind of ['retired', 'pending-settles', 'live', '401', '403', 'wrong-pid', 'missing-instance',
    'changed-instance', 'pid-reuse', 'adopt-pid-reuse', 'unreadable', 'foreign', 'perf-timeout', 'pending', 'live-writes',
    'exit-unproven', 'off-before-exit', 'off-after-exit', 'new-generation', 'off-while-pending', 'generation-while-pending', 'off-during-ready', 'generation-during-ready']) {
  await run('HTTP.sys handoff: ' + kind, async () => {
    reset(); virtualNow = 0;
    const f: HandoffFixture = { kind, stateReads: 0, perfReads: 0, identityReads: 0,
      oldExited: false, launched: false, current: true, trace: [] };
    handoff = f;
    const config = resolveFanHostConfig('C:\\fixture'); config.hostExecutable = EXE;
    try {
      const operation = new NativeFanHostLauncher().start(config, () => {
        if (!f.current) throw new Error('FAN_CONTROL_INTENT_SUPERSEDED:fixture-' + kind);
      });
      if (kind === 'retired' || kind === 'pending-settles') {
        const result = await operation;
        assert.equal(result.pid, NEW_PID); assert.equal(result.processCreated, NEW_CREATED);
        assert.equal(calls.filter(c => c.cmd === 'shell.hidden').length, 1);
        assert.equal(f.trace.filter(e => e === '/api/close').length, 0, 'retired/in-flight Close is observed, not replayed');
        assert.equal(f.trace.filter(e => e === '/api/shutdown').length, 0, 'unknown OEM release never becomes a success claim');
        assert.ok(f.trace.indexOf('exit-proof') < f.trace.indexOf('launch'));
        assert.equal(retired().oemRestoreConfirmed, false);
      } else if (kind === 'live') {
        const result = await operation;
        assert.equal(result.pid, PID); assert.equal(result.adopted, true);
        assert.equal(f.trace.includes('/api/close'), false); assert.equal(f.trace.includes('exit-proof'), false);
      } else if (kind === 'off-during-ready' || kind === 'generation-during-ready') {
        await assert.rejects(operation, /FAN_CONTROL_INTENT_SUPERSEDED/);
        assert.equal(f.launched, true); assert.equal(f.trace.filter(e => e === 'cancelled-child-exit').length, 1);
        assert.equal(calls.filter(c => c.cmd === 'shell.hidden').length, 1);
      } else {
        await assert.rejects(operation);
        assert.equal(f.launched, false, 'refusal/cancellation must not spawn a second writer');
        if (kind !== 'exit-unproven' && kind !== 'off-after-exit' && kind !== 'new-generation')
          assert.equal(f.trace.includes('exit-proof'), false, 'identity/cleanup/refusal is not termination permission');
      }
    } finally { handoff = null; virtualNow = null; }
  });
}
console.log('FAN-941 owned Host exit proof: ' + passed + '/32 PASS; actual launcher, inert native only; physical UNKNOWN');
