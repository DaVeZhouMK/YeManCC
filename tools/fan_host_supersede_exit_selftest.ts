/**
 * FAN-938 R6 —— P3/D7「挂起旧实例退路」无硬件故障注入门。
 *
 * 依据：`Docs\Tasks\Fan\FAN-920\FAN-938-R6-REVIEW-AND-T0-REWORK-EXECUTION-ADJUDICATION-20261005.md`
 *   §P3 L102：补无硬件故障注入门：旧调用永不返回、监听先停但进程仍活、新旧同端口、
 *             PID/creation 不匹配；**必须证明不提前 spawn**。正常父退出 watchdog 的旧
 *             结果保留，不能拿来覆盖挂起更换用例。
 *   §D7（L122）：§P3 挂起旧实例与进程退出判据；保持正常 watchdog/认证结果的真实范围。
 *
 * 本门**直接驱动真实 `NativeFanHostLauncher.start()`**（不是 FakeLauncher、不替代桥/
 * 准入/恢复代码）：通过 WebView2 IPC 注入只替换系统边界（fs / proc / shell / http），
 * 让真实 fanHost.ts 里的“识别旧常驻 Host → 结算 → 确认退出 → 才允许 spawn”的整条生产
 * 路径被完整执行。四类注入都必须 **reject 且 `shell.hidden` 调用数 = 0**（未确认旧实例
 * 退出前绝不 spawn 新硬件 writer）。
 *
 * 无硬件写入、不启动真实 Host、不触 EC、不睡眠工作机。
 */
import assert from 'node:assert/strict';
import { webcrypto } from 'node:crypto';

const HOST_EXE = 'C:\\SOFT\\YeMan\\PowerControl\\fan-host-v2\\YeManFanHost.exe';
const POWER_CONTROL_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
const FAN_STATE_DIR = 'C:\\fake-fanstate';
const OLD_PID = 9101;
const FOREIGN_PID = 7202;
const TOKEN = '0123456789abcdef'.repeat(4); // 64 位小写 hex，合法会话令牌

// Node 的 webcrypto：`sessionTokenFingerprint` 的实参在 `fanDiagnosticLog` 关闸时仍被
// 立即求值，因此全局 crypto 必须存在（拿到的只是非秘密指纹）。
if (!(globalThis as any).crypto) (globalThis as any).crypto = webcrypto;
// ipc.ts 在分发事件时使用 CustomEvent；旧 Node 无全局实现时补一个最小垫片。
if (typeof (globalThis as any).CustomEvent === 'undefined') {
  (globalThis as any).CustomEvent = class CustomEvent<T = unknown> extends Event {
    detail: T;
    constructor(type: string, params?: { detail?: T }) {
      super(type);
      this.detail = params?.detail as T;
    }
  };
}

type Scenario = 'S1' | 'S2' | 'S3' | 'S4';
let scenario: Scenario = 'S1';
let stateCalls = 0;
const identityCalls = new Map<number, number>();
const shellHiddenCalls: Array<{ program: string; args: string[] }> = [];
const shellHiddenPids = new Set<number>();

const ok = (value: unknown) => ({ status: 200, body: JSON.stringify(value), headers: '' });
const jsonState = (state: string) => ok({ state: { state } });

/** 每场景重开时清空可观测注入状态。 */
function resetScenario(next: Scenario): void {
  scenario = next;
  stateCalls = 0;
  identityCalls.clear();
  shellHiddenCalls.length = 0;
  shellHiddenPids.clear();
}

/** 监听端口的 netstat 行：S3 由**外部进程**占用，其余由旧 YeManFanHost 占用。 */
function netstat(stdoutPid: number) {
  return {
    exitCode: 0,
    stdout: `  TCP    127.0.0.1:8765         0.0.0.0:0              LISTENING       ${stdoutPid}\n`,
    stderr: '',
  };
}

const validOld = () => ({ valid: true, pid: OLD_PID, processCreated: '133000000000000000', path: HOST_EXE });

/**
 * 按调用序号编排进程身份：
 *  - S1：前 2 次（findExactHostOwner / resolveExactHostInstance）有效；confirm 循环第 3 次
 *        句柄失效 → 直接落到 terminateExact 兜底。
 *  - S4：第 1 次（findExactHostOwner）有效；resolveExactHostInstance 第 2 次 creation 不匹配
 *        → 无法确证监听实例身份。
 *  - S3：外部进程有效但路径不是当前 YeManFanHost。
 */
async function identity(pid: number) {
  const n = (identityCalls.get(pid) ?? 0) + 1;
  identityCalls.set(pid, n);
  if (scenario === 'S3' && pid === FOREIGN_PID) {
    return { valid: true, pid, processCreated: '999000000000000000', path: 'C:\\Other\\ForeignListener.exe' };
  }
  if (pid === OLD_PID && (scenario === 'S1' || scenario === 'S4')) {
    if (scenario === 'S1') return n <= 2 ? validOld() : { valid: false, pid };
    return n === 1 ? validOld() : { valid: false, pid };
  }
  return { valid: false, pid };
}

async function httpRequest(url: string, options: { method?: string } = {}) {
  const path = new URL(url).pathname;
  const method = (options.method ?? 'GET').toUpperCase();
  if (path === '/health') {
    // S2：监听先停但进程仍活 —— native HTTP 已不可用。
    if (scenario === 'S2') throw new Error('connect ECONNREFUSED 127.0.0.1:8765');
    return ok({ host: 'YeManFanHost', protocolVersion: 2 });
  }
  if (path === '/api/state') {
    stateCalls += 1;
    if (scenario === 'S3') return stateCalls >= 2 ? jsonState('Ready') : { status: 500, body: '{}', headers: '' };
    if (scenario === 'S1') return stateCalls >= 3 ? jsonState('Stopped') : { status: 500, body: '{}', headers: '' };
    // S2 / S4：准入探测与结算观察都拿不到可用 state。
    return { status: 500, body: '{}', headers: '' };
  }
  if (path === '/api/close') {
    // S1：旧调用永不返回（超时）——恢复观察改由 waitForRemoteHcCloseCleanup 承担。
    if (scenario === 'S1') throw new Error('旧调用永不返回：close 请求超时');
    return jsonState('Stopped');
  }
  if (path === '/api/shutdown') return { status: 404, body: '{}', headers: '' };
  throw new Error(`unexpected http path: ${method} ${path}`);
}

async function invoke(cmd: string, a: Record<string, any>): Promise<any> {
  switch (cmd) {
    case 'app.aiFanMockSession':
      return { schemaVersion: 1, enabled: false }; // parseAiFanMockSession → null（走真实启动）
    case 'app.fanStateDir':
      return FAN_STATE_DIR;
    case 'app.dataDir':
      return 'C:\\fake-data';
    case 'app.pid':
      return 20000;
    case 'app.fanActivity':
      return null;
    case 'fs.exists':
      return String(a.path).toLowerCase().endsWith('yemanfanhost.exe');
    case 'fs.mkdir':
      return true;
    case 'fs.readTextFile':
      if (String(a.path).toLowerCase().endsWith('yemanfanhost.session')) return TOKEN;
      throw new Error('unexpected read path: ' + String(a.path));
    case 'fs.writeTextFileAtomic':
      return true;
    case 'fs.sha256File':
      return '0'.repeat(64);
    case 'proc.findExact':
      return { found: true, pid: OLD_PID };
    case 'process.identity':
      return identity(Number(a.pid));
    case 'process.terminateExact':
      // S1：绑定句柄确认旧实例仍在运行 → 无法确认退出。
      return { ok: false, matched: true, exited: false, reason: 'still-running' };
    case 'shell.run': {
      assert.equal(String(a.program).toLowerCase(), 'netstat.exe');
      return netstat(scenario === 'S3' ? FOREIGN_PID : OLD_PID);
    }
    case 'shell.hidden':
      shellHiddenCalls.push({ program: String(a.program), args: a.args ?? [] });
      if (typeof a.pid === 'number') shellHiddenPids.add(a.pid);
      return { ok: true, pid: 99999 };
    case 'http.request':
      return httpRequest(String(a.url), { method: a.method });
    case 'fanLog.write':
    case 'fanLog.record':
      return true;
    default:
      throw new Error('unknown: ' + cmd);
  }
}

let listener: ((e: { data: any }) => void) | null = null;
const win = new EventTarget() as EventTarget & Record<string, any>;
win.setTimeout = setTimeout;
win.clearTimeout = clearTimeout;
win.chrome = {
  webview: {
    addEventListener(_type: string, fn: (e: { data: any }) => void) { listener = fn; },
    postMessage(msg: any) {
      void invoke(msg.cmd, msg.args ?? {}).then(
        (result) => listener?.({ data: { id: msg.id, result } }),
        (error) => listener?.({ data: { id: msg.id, error: String(error instanceof Error ? error.message : error) } }),
      );
    },
  },
};
(globalThis as any).window = win;

// 必须动态 import：ipc.ts 在模块加载时捕获 window.chrome.webview。
const { NativeFanHostLauncher, resolveFanHostConfig } = await import('../src/bridge/fanHost');

const EXPECT: Record<Scenario, string> = {
  S1: '旧 Fan Host 实例无法确认退出',
  S2: '旧 Fan Host 进程仍在运行但 native HTTP 不可用',
  S3: '旧 Fan Host 健康响应来自非当前 YeManFanHost',
  S4: '旧 Fan Host 监听进程身份不可确证',
};

const LABEL: Record<Scenario, string> = {
  S1: 'S1 旧调用永不返回：无法确认旧实例退出，禁止 spawn 新 writer',
  S2: 'S2 监听先停但进程仍活：拒绝重复接管，禁止 spawn 新 writer',
  S3: 'S3 新旧同端口：健康响应来自非当前 YeManFanHost，禁止 spawn 新 writer',
  S4: 'S4 PID/creation 不匹配：监听实例身份不可确证，禁止 spawn 新 writer',
};

let passed = 0;
for (const s of ['S1', 'S2', 'S3', 'S4'] as Scenario[]) {
  resetScenario(s);
  const config = resolveFanHostConfig(POWER_CONTROL_DIR);
  const launcher = new NativeFanHostLauncher();
  let rejection: string | null = null;
  try {
    await launcher.start(config);
  } catch (error) {
    rejection = error instanceof Error ? error.message : String(error);
  }
  assert.ok(rejection, `${s}: 未确认旧实例退出前 start() 必须拒绝`);
  assert.ok(
    rejection.includes(EXPECT[s]),
    `${s}: 拒绝原因应为「${EXPECT[s]}」，实际：${rejection}`,
  );
  assert.equal(
    shellHiddenCalls.length,
    0,
    `${s}: 未确认旧实例退出前不得 spawn 新 Host（实际 shell.hidden 调用 ${shellHiddenCalls.length} 次）`,
  );
  console.log(`PASS  ${LABEL[s]}`);
  passed += 1;
}

assert.equal(passed, 4);
console.log('FAN-938 R6 P3/D7: superseded old-instance exit backstop — four no-hardware fault injections never spawn a second hardware writer PASS');
