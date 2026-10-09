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
async function invoke(cmd: string, args: any): Promise<any> {
  calls.push({ cmd, args });
  if (cmd === 'process.identity') return current;
  if (cmd === 'process.terminateExact') return exact;
  if (cmd === 'process.terminateTree') return tree;
  if (cmd === 'app.aiFanMockSession') return { schemaVersion: 1, enabled: false };
  if (cmd === 'app.fanStateDir') return 'C:\\fixture-state';
  if (cmd === 'fs.exists') return String(args.path).toLowerCase().endsWith('yemanfanhost.exe');
  if (cmd === 'fs.mkdir') return true;
  if (cmd === 'fs.readTextFile') return TOKEN;
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
console.log('FAN-941 owned Host exit proof: ' + passed + '/9 PASS; native boundary mocked, no device proof');
