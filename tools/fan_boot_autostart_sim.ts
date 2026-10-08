/**
 * FAN-932 模拟环节：用生产的 `ensureFanAutoStart`（从 src/App.vue 抽取真实源码）
 * 驱动真实的 `FanHostLifecycle` + 替身适配器/启动器，验证「开机/休眠唤醒启动风扇」
 * 开关为**开**时，开机与唤醒两条边沿是否真的会启动 Host 并下发保存曲线。
 *
 * 为什么抽源码而不是重写一份：重写只能证明"我写了个一样的函数"，抽源码能证明
 * 生产编排本身在两条边沿上真的走到了 start + apply，且默认关/已有意图/机型不适配
 * 时零动作、不伪造成功。
 *
 * 注入方式：源码里的依赖（fanHostLifecycle / readSettingsSection / getFanFeatureSettings /
 * getFanPresetCurve / fanDiagnosticLog）通过**闭包读 env** 注入——不能一次性解构复制，
 * 否则测试中途改偏好/换生命周期不会生效（本文件第一版就踩了这个坑）。
 *
 * 安全边界：全程不启动任何真实进程（launcher 是替身）、不发任何 HTTP、不碰 EC/HID/HC。
 */
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { FanHostLifecycle } from '@/bridge/fanHost';
import type { FanApiAdapter, FanHandshake, FanLease, FanNode, FanPreset, FanResumeRequest, FanState } from '@/bridge/fanApi';

const root = process.cwd();
const SAVED_CURVE: FanNode[] = [
  { tempC: 0, dutyPercent: 0 },
  { tempC: 40, dutyPercent: 22 },
  { tempC: 70, dutyPercent: 55 },
  { tempC: 100, dutyPercent: 95 },
];

// ── 从 App.vue 抽取真实编排入口 ────────────────────────────────────────────────
const appSource = readFileSync(join(root, 'src/App.vue'), 'utf8');
const fnStart = appSource.indexOf('function ensureFanAutoStart(');
const fnEnd = appSource.indexOf('const router = useRouter();', fnStart);
assert(fnStart >= 0 && fnEnd > fnStart,
  'App.vue must expose the single shared `ensureFanAutoStart` entry between its declaration and the router setup');
let fnSource = appSource.slice(fnStart, fnEnd).trimEnd();
const signature = fnSource.split('\n');
assert(signature[0].startsWith('function ensureFanAutoStart('),
  'the auto-start entry signature must stay on one line for the simulation to strip its TypeScript types');
signature[0] = 'function ensureFanAutoStart(reason, generation = 0) {';
fnSource = signature.join('\n').replace(/readSettingsSection<[^<>]*>/g, 'readSettingsSection');
assert(fnSource.includes('fanHostLifecycle.start()') && fnSource.includes('fanHostLifecycle.apply(') &&
  fnSource.includes('gate.writeReady') && fnSource.includes('fanHostLifecycle.hasControlIntent'),
'source extraction lost the production start/apply/gate/intent logic');

function state(name = 'Ready'): FanState {
  return {
    state: name,
    hardwareWrites: false,
    hardwareWritesObserved: false,
    oemRestoreConfirmed: ['OEM', 'Released', 'Closed', 'Stopped', 'AwaitingControl', 'Suspended'].includes(name),
  };
}

class SimAdapter implements FanApiAdapter {
  readonly enabled = true;
  readonly calls: string[] = [];
  readonly enabledNodeSets: Array<readonly FanNode[]> = [];
  readonly sessionTransitions: string[] = [];
  private hostOpened = false;
  private hostEventsOpened = false;
  handshakeResult: FanHandshake = {
    ok: true,
    supported: true,
    deviceClass: 'HandheldCompanion.Devices.GPDWin5',
    fanRouteWriteReady: true,
    deviceIdentity: { manufacturer: 'GPD', model: 'G1618-05', product: 'G1618-05', bios: '2.20' },
  };
  get handshakes(): number { return this.calls.filter((c) => c === 'handshake').length; }
  get enables(): number { return this.calls.filter((c) => c === 'enable').length; }
  async handshake() { this.calls.push('handshake'); return this.handshakeResult; }
  protected hostSnapshot(): FanState {
    return { ...state('AwaitingControl'), powerState: 'On', protocolVersion: '2',
      openCalled: this.hostOpened, openEventsCalled: this.hostEventsOpened,
      controlAccepting: this.hostOpened && this.hostEventsOpened };
  }
  async getState() { this.calls.push('state'); return this.hostSnapshot(); }
  async enable(nodes: readonly FanNode[]) {
    this.calls.push('enable');
    this.enabledNodeSets.push(nodes.map((n) => ({ ...n })));
    return state('Applied');
  }
  async applyPreset(_name: FanPreset, _leaseId?: string, nodes?: readonly FanNode[]) {
    this.calls.push('preset');
    if (nodes) this.enabledNodeSets.push(nodes.map((n) => ({ ...n })));
    return state('Applied');
  }
  async disable() { this.calls.push('disable'); return state('OEM'); }
  async open() { this.calls.push('open'); this.hostOpened = true; this.sessionTransitions.push('Open'); return state('Open'); }
  async openEvents() { this.calls.push('open-events'); this.hostEventsOpened = true; this.sessionTransitions.push('OpenEvents'); return state('Events'); }
  async acquireControl(): Promise<FanLease> { this.calls.push('acquire'); return { leaseId: 'sim-lease', generation: 1 }; }
  async heartbeat(leaseId: string): Promise<FanLease> { this.calls.push('heartbeat'); return { leaseId, generation: 1 }; }
  async releaseControl() { this.calls.push('release'); return state('Released'); }
  async restoreOem() { this.calls.push('restore'); return state('OEM'); }
  async suspend() { this.calls.push('suspend'); return state('Suspended'); }
  async resume() {
    this.calls.push('resume');
    // /api/resume belongs to the Host's existing owner, not extra frontend Open calls.
    this.hostOpened = true; this.sessionTransitions.push('Open');
    this.hostEventsOpened = true; this.sessionTransitions.push('OpenEvents');
    return this.hostSnapshot();
  }
  async close() {
    this.calls.push('close'); this.hostOpened = false; this.hostEventsOpened = false;
    return { ...state('Stopped'), protocolVersion: '2', openCalled: false, openEventsCalled: false,
      hardwareWritesEnabled: false, hcVirtualCloseReturned: true, hcDeviceManagerStopCompleted: true };
  }
  async shutdown() { this.calls.push('shutdown'); }
}

class SimLauncher {
  readonly calls: string[] = [];
  async start() { this.calls.push('start'); return { pid: 4242, executable: 'sim-host.exe' }; }
  async stop() { this.calls.push('stop'); }
}

/**
 * FAN-933 用的「唤醒后常驻 Host」替身：睡眠时 HC 已按顺序 Close，所以
 * `getState()` 报 Suspended + openCalled=false；只有收到一次 `/api/resume` 之后，
 * 再经过若干次只读探测才收敛为 AwaitingControl + openCalled/openEventsCalled=true
 * （快速接受 ≠ 完成，正是本次事故的关键）。
 */
class WakeHostAdapter extends SimAdapter {
  resumeCalls = 0;
  /** resume 之后需要几次 getState 才收敛（模拟 Host 侧 Open/OpenEvents 耗时）。 */
  convergeAfterPolls = 2;
  /** true = 直接把 Host 当成已经重开好的会话（矩阵第 8 项：rearm 必须 no-op）。 */
  alreadyOpen = false;
  private pollsAfterResume = 0;
  override async resume(_request: FanResumeRequest): Promise<FanState> {
    this.calls.push('resume');
    this.resumeCalls += 1;
    this.pollsAfterResume = 0;
    return this.suspendedSnapshot();
  }
  override async getState(): Promise<FanState> {
    this.calls.push('state');
    if (this.alreadyOpen) return this.admissibleSnapshot();
    if (this.resumeCalls > 0) {
      this.pollsAfterResume += 1;
      if (this.pollsAfterResume >= this.convergeAfterPolls) return this.admissibleSnapshot();
    }
    return this.suspendedSnapshot();
  }
  private suspendedSnapshot(): FanState {
    const snapshot = state('Suspended');
    snapshot.powerState = 'Suspended';
    snapshot.openCalled = false;
    snapshot.openEventsCalled = false;
    snapshot.hardwareWritesEnabled = false;
    return snapshot;
  }
  private admissibleSnapshot(): FanState {
    const snapshot = state('AwaitingControl');
    snapshot.powerState = 'On';
    snapshot.protocolVersion = '2';
    snapshot.openCalled = true;
    snapshot.openEventsCalled = true;
    snapshot.controlAccepting = true;
    return snapshot;
  }
}

interface SimEnv {
  lifecycle: FanHostLifecycle;
  preference: boolean;
  featureEnabled: boolean;
  curve: FanNode[];
  logs: string[];
  aiMockActive: boolean;
}

/** 用真实生产源码 + 可中途变更的 env 组装一个编排实例。 */
function buildOrchestrator(lifecycle: FanHostLifecycle, logs: string[]) {
  const env: SimEnv = {
    lifecycle,
    preference: false,
    featureEnabled: true,
    curve: SAVED_CURVE.map((n) => ({ ...n })),
    logs,
    aiMockActive: false,
  };
  const factory = new Function('env', [
    'let latestResumeGeneration = 0;',
    'let aiFanMockActive = env.aiMockActive;',
    'let fanAutoStartChain = Promise.resolve();',
    'let fanBootAutoStartDone = false;',
    'const fanAutoStartGenerations = new Set();',
    // 依赖一律闭包读 env，保证测试中途改开关/换生命周期即时生效。
    'const fanHostLifecycle = {',
    '  get hasControlIntent() { return env.lifecycle.hasControlIntent; },',
    '  start: () => env.lifecycle.start(),',
    '  apply: (nodes) => env.lifecycle.apply(nodes),',
    '};',
    'const readSettingsSection = async () => ({ fanControl: env.preference });',
    'const getFanFeatureSettings = () => ({ featureEnabled: env.featureEnabled, preset: "balanced" });',
    'const getFanPresetCurve = () => env.curve.map((n) => ({ ...n }));',
    'const fanDiagnosticLog = (event, detail) => { env.logs.push(event + (detail ? " " + JSON.stringify(detail) : "")); };',
    fnSource,
    'return { ensureFanAutoStart, setLatestResumeGeneration: (g) => { latestResumeGeneration = g; } };',
  ].join('\n'));
  const api = factory(env) as {
    ensureFanAutoStart: (reason: 'boot' | 'resume', generation?: number) => Promise<void>;
    setLatestResumeGeneration: (g: number) => void;
  };
  return { env, api };
}

const testLifecycles = new Set<FanHostLifecycle>();

function newLifecycle(adapter: SimAdapter, launcher: SimLauncher, resumeWaitDeadlineMs?: number): FanHostLifecycle {
  let nativeGeneration = 0;
  let nativePhase: 'ready' | 'suspending' | 'resuming' = 'ready';
  let nativeResumeReady = true;
  const lifecycle = new FanHostLifecycle({
    enabled: true,
    adapter,
    launcher,
    heartbeatIntervalMs: 0,
    ...(resumeWaitDeadlineMs ? { resumeWaitDeadlineMs } : {}),
    requestManualWake: async () => null,
    readNativeActivity: async () => null, // 无 native 活动记录 ⇒ resume 诚实 no-work
    readNativePowerState: async () => ({ phase: nativePhase, generation: nativeGeneration,
      hardwareWritesAllowed: nativePhase === 'ready', inputReady: nativePhase === 'ready',
      resumeReady: nativeResumeReady, hibernateAvailable: false }),
  });
  const setGeneration = lifecycle.setPowerGeneration.bind(lifecycle);
  lifecycle.setPowerGeneration = (generation) => { nativeGeneration = generation; setGeneration(generation); };
  const observe = lifecycle.observePowerBoundary.bind(lifecycle);
  lifecycle.observePowerBoundary = (event, generation) => {
    if (event === 'suspending') { nativePhase = 'suspending'; nativeResumeReady = false; }
    if (event === 'resuming') { nativePhase = 'resuming'; nativeResumeReady = false; }
    if (event === 'resume-ready' || event === 'resumed') { nativePhase = 'ready'; nativeResumeReady = true; }
    observe(event, generation);
  };
  testLifecycles.add(lifecycle);
  return lifecycle;
}

/**
 * 复刻 FAN-933 现场：常驻 Host 先在 gen 0 建过会话（用户此前开过/上一次自动启动过），
 * `keepIntent=false` 时再 disable() 交回 OEM（Host 常驻但**无活动意图**），然后经历
 * 一次真实睡眠→唤醒（代次前进到 generation）。
 */
async function armResidentHostAfterWake(
  adapter: WakeHostAdapter,
  launcher: SimLauncher,
  generation: number,
  keepIntent: boolean,
  resumeWaitDeadlineMs?: number,
): Promise<FanHostLifecycle> {
  const lifecycle = newLifecycle(adapter, launcher, resumeWaitDeadlineMs);
  await lifecycle.start();
  await lifecycle.apply(SAVED_CURVE);
  if (!keepIntent) await lifecycle.disable();
  lifecycle.setPowerGeneration(generation);
  lifecycle.observePowerBoundary('suspending', generation);
  lifecycle.observePowerBoundary('resuming', generation);
  lifecycle.observePowerBoundary('resume-ready', generation);
  return lifecycle;
}

async function test(name: string, run: () => Promise<void>): Promise<void> {
  try {
    await run();
    console.log(`PASS ${name}`);
  } finally {
    // All launchers/adapters are fake. Exercise actual Close cancellation so
    // bounded-recovery timers cannot survive the fixture that created them.
    for (const lifecycle of testLifecycles) await lifecycle.close();
    testLifecycles.clear();
  }
}

async function main(): Promise<void> {
  // ── S1 默认关：开机边界零动作 ───────────────────────────────────────────────
  await test('S1 偏好关闭：开机边界零动作（不起 Host、不握手、不写）', async () => {
    const logs: string[] = [];
    const adapter = new SimAdapter();
    const launcher = new SimLauncher();
    const lifecycle = newLifecycle(adapter, launcher);
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = false;
    await api.ensureFanAutoStart('boot');
    assert.equal(launcher.calls.length, 0, `偏好关不得启动 Host（launcher=${launcher.calls.join(',')}）`);
    assert.equal(adapter.calls.length, 0, `偏好关不得发任何请求（adapter=${adapter.calls.join(',')}）`);
    assert.equal(lifecycle.hasControlIntent, false, '偏好关不得建立控制意图');
    assert.equal(lifecycle.processId, null, '偏好关不得留下 Host 进程');
    assert.ok(!logs.some((l) => l.includes('auto-start-applied')), '偏好关不得记录"已启动"');
  });

  // ── S2 开机 + 偏好开：真实起效 ──────────────────────────────────────────────
  await test('S2 偏好开启：开机边界真实启动并对适配机型下发保存曲线', async () => {
    const logs: string[] = [];
    const adapter = new SimAdapter();
    const launcher = new SimLauncher();
    const lifecycle = newLifecycle(adapter, launcher);
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = true;
    await api.ensureFanAutoStart('boot');
    assert.deepEqual(launcher.calls, ['start'], `开机应恰好启动一次 Host（launcher=${launcher.calls.join(',')}）`);
    assert.deepEqual(adapter.calls, ['handshake', 'open', 'open-events', 'acquire', 'enable'],
      `开机应完成握手→Open→OpenEvents→租约→写入（adapter=${adapter.calls.join(',')}）`);
    assert.equal(lifecycle.phase, 'active', '写入成功后生命周期应处于 active');
    assert.equal(lifecycle.hasControlIntent, true, '写入后必须已有活动控制意图');
    assert.deepEqual(adapter.enabledNodeSets[0], SAVED_CURVE, '下发的必须是保存的 preset 曲线');
    assert.ok(logs.some((l) => l.includes('auto-start-applied') && l.includes('"reason":"boot"')),
      '必须记录 auto-start-applied(boot)');
  });

  // ── S3 开机边界重复触发：只写一次 ──────────────────────────────────────────
  await test('S3 同一 run 重复触发开机边界：只启动/写入一次', async () => {
    const logs: string[] = [];
    const adapter = new SimAdapter();
    const launcher = new SimLauncher();
    const lifecycle = newLifecycle(adapter, launcher);
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = true;
    await Promise.all([api.ensureFanAutoStart('boot'), api.ensureFanAutoStart('boot')]);
    await api.ensureFanAutoStart('boot');
    assert.deepEqual(launcher.calls, ['start'], `重复开机触发不得二次启动（launcher=${launcher.calls.join(',')}）`);
    assert.equal(adapter.enables, 1, `重复开机触发不得二次写入（enable=${adapter.enables}）`);
  });

  // ── S4 唤醒 + 偏好开 + 无旧意图：先 resume 后自动启动，真实起效 ─────────────
  await test('S4 偏好开启 + 无旧意图：唤醒先 resume(no-work)，再真实启动并下发曲线', async () => {
    const logs: string[] = [];
    const adapter = new SimAdapter();
    const launcher = new SimLauncher();
    const lifecycle = newLifecycle(adapter, launcher);
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = true;
    // 复刻 App 的唤醒边沿顺序：suspending → resuming → resume-ready(g1)
    lifecycle.setPowerGeneration(1);
    lifecycle.observePowerBoundary('suspending', 1);
    lifecycle.observePowerBoundary('resuming', 1);
    lifecycle.observePowerBoundary('resume-ready', 1);
    await lifecycle.resume(); // 无 native 意图 ⇒ 诚实 no-work
    assert.equal(lifecycle.hasControlIntent, false, 'resume 无意图时不得凭空建立意图');
    // 可观测的 no-work 证据：resume 自身不得产生任何写入（lifecycle 的内部日志走 bridge 自己的 sink，
    // 不经过本文件的注入 logger，因此这里不用日志文本判定）。
    assert.equal(adapter.enables, 0, 'resume 无意图时不得写入');
    const beforeResumeCalls = adapter.calls.length;
    api.setLatestResumeGeneration(1);
    await api.ensureFanAutoStart('resume', 1);
    const afterCalls = adapter.calls.slice(beforeResumeCalls);
    assert.deepEqual(launcher.calls, ['start'], `唤醒自动启动应恰好一次（launcher=${launcher.calls.join(',')}）`);
    assert.deepEqual(afterCalls, ['handshake', 'state', 'resume', 'state', 'acquire', 'enable'],
      `唤醒自动启动必须由唯一 Host resume owner 证明重开后再租约→写曲线（calls=${afterCalls.join(',')}；logs=${logs.join(';')}）`);
    assert.deepEqual(adapter.sessionTransitions, ['Open', 'OpenEvents'],
      'resume owner 必须完成 Open→OpenEvents，不能以 handshake 或 Ready 空壳冒充重开');
    assert.equal(lifecycle.phase, 'active', '唤醒自动启动后应处于 active');
    assert.deepEqual(adapter.enabledNodeSets[0], SAVED_CURVE, '唤醒下发的必须是保存曲线');
    assert.ok(logs.some((l) => l.includes('auto-start-applied') && l.includes('"reason":"resume"')),
      '必须记录 auto-start-applied(resume)');
  });

  // ── S5 唤醒 + 已有旧意图：只恢复一次，不重复写 ─────────────────────────────
  await test('S5 偏好开启 + 已有活动意图：唤醒只恢复一次，自动启动不重复写', async () => {
    const logs: string[] = [];
    const adapter = new SimAdapter();
    const launcher = new SimLauncher();
    const lifecycle = newLifecycle(adapter, launcher);
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = true;
    // 先建立一次活动会话（等价于用户此前手动开启/开机已自动启动）
    await lifecycle.start();
    await lifecycle.apply(SAVED_CURVE);
    assert.equal(lifecycle.hasControlIntent, true, '前置：应已有活动意图');
    // 睡眠 → 唤醒
    lifecycle.setPowerGeneration(2);
    lifecycle.observePowerBoundary('suspending', 2);
    lifecycle.observePowerBoundary('resuming', 2);
    lifecycle.observePowerBoundary('resume-ready', 2);
    const beforeResume = adapter.enables;
    await lifecycle.resume(); // 有意图 ⇒ 重放一次
    const afterResume = adapter.enables;
    assert.equal(afterResume, beforeResume + 1, 'resume 应恰好重放一次曲线');
    api.setLatestResumeGeneration(2);
    await api.ensureFanAutoStart('resume', 2);
    assert.equal(adapter.enables, afterResume, '已有意图时自动启动必须 no-op（不得再写）');
    assert.equal(launcher.calls.filter((c) => c === 'start').length, 1, '已有意图时不得二次启动 Host');
    assert.ok(!logs.some((l) => l.includes('auto-start-applied')), '已有意图时不得记录"自动启动成功"');
  });

  // ── S6 同一唤醒代重复触发：只一次 ──────────────────────────────────────────
  await test('S6 同一唤醒代重复触发：只执行一次事务', async () => {
    const logs: string[] = [];
    const adapter = new SimAdapter();
    const launcher = new SimLauncher();
    const lifecycle = newLifecycle(adapter, launcher);
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = true;
    lifecycle.setPowerGeneration(3);
    lifecycle.observePowerBoundary('suspending', 3);
    lifecycle.observePowerBoundary('resuming', 3);
    lifecycle.observePowerBoundary('resume-ready', 3);
    await lifecycle.resume();
    api.setLatestResumeGeneration(3);
    await Promise.all([
      api.ensureFanAutoStart('resume', 3),
      api.ensureFanAutoStart('resume', 3),
      api.ensureFanAutoStart('resume', 3),
    ]);
    assert.deepEqual(launcher.calls, ['start'], `同代重复不得二次启动（launcher=${launcher.calls.join(',')}）`);
    assert.equal(adapter.enables, 1, `同代重复不得二次写入（enable=${adapter.enables}）`);
  });

  // ── S7 机型不适配：不伪造成功 ──────────────────────────────────────────────
  await test('S7 机型不适配：记录失败、零写入、不报成功', async () => {
    const logs: string[] = [];
    const adapter = new SimAdapter();
    adapter.handshakeResult = { ok: true, supported: false, deviceClass: '', fanRouteWriteReady: false };
    const launcher = new SimLauncher();
    const lifecycle = newLifecycle(adapter, launcher);
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = true;
    await api.ensureFanAutoStart('boot');
    assert.equal(adapter.enables, 0, `不适配机型不得写入（enable=${adapter.enables}）`);
    assert.equal(lifecycle.hasControlIntent, false, '不适配机型不得建立控制意图');
    assert.ok(logs.some((l) => l.includes('auto-start-failed') || l.includes('auto-start-blocked')),
      `不适配机型必须留下失败/阻断证据（logs=${logs.join(' | ')}）`);
    assert.ok(!logs.some((l) => l.includes('auto-start-applied')), '不适配机型绝不得记录"自动启动成功"');
  });

  // ── S8 偏好关 + 唤醒：零动作 ───────────────────────────────────────────────
  await test('S8 偏好关闭：唤醒边界同样零动作', async () => {
    const logs: string[] = [];
    const adapter = new SimAdapter();
    const launcher = new SimLauncher();
    const lifecycle = newLifecycle(adapter, launcher);
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = false;
    lifecycle.setPowerGeneration(4);
    lifecycle.observePowerBoundary('suspending', 4);
    lifecycle.observePowerBoundary('resuming', 4);
    lifecycle.observePowerBoundary('resume-ready', 4);
    await lifecycle.resume();
    api.setLatestResumeGeneration(4);
    await api.ensureFanAutoStart('resume', 4);
    assert.equal(launcher.calls.length, 0, `偏好关时唤醒不得启动 Host（launcher=${launcher.calls.join(',')}）`);
    assert.equal(adapter.enables, 0, `偏好关时唤醒不得写入（enable=${adapter.enables}）`);
  });

  // ── FAN-933：唤醒后「新控制接入」必须先 rearm 再 apply ─────────────────────
  await test('S9 FAN-933 矩阵2：唤醒后无旧意图+偏好开 ⇒ 1×/api/resume → 观察到 Open/OpenEvents → 1×acquire + 1×apply', async () => {
    const logs: string[] = [];
    const adapter = new WakeHostAdapter();
    const launcher = new SimLauncher();
    const lifecycle = await armResidentHostAfterWake(adapter, launcher, 7, false);
    const wakeMark = adapter.calls.length;
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = true;
    // App 的唤醒顺序：先既有 resume()（无意图 ⇒ 零动作），再自动启动。
    await lifecycle.resume();
    assert.equal(adapter.resumeCalls, 0, 'FAN-927：无活动意图的 resume() 必须零动作（不得调 /api/resume）');
    api.setLatestResumeGeneration(7);
    await api.ensureFanAutoStart('resume', 7);
    const wakeCalls = adapter.calls.slice(wakeMark);
    assert.equal(adapter.resumeCalls, 1, `rearm 必须恰好一次 /api/resume（actual=${adapter.resumeCalls}）`);
    const resumeAt = wakeCalls.indexOf('resume');
    const acquireAt = wakeCalls.indexOf('acquire');
    assert.ok(resumeAt >= 0 && acquireAt > resumeAt,
      `必须先完成 /api/resume 才允许 acquire-control（calls=${wakeCalls.join(',')}）`);
    assert.ok(!wakeCalls.includes('open') && !wakeCalls.includes('open-events'),
      '前端不得直接调 open()/openEvents()——HC 会话重开必须由 Host 的 /api/resume 完成');
    assert.equal(wakeCalls.filter((c) => c === 'acquire').length, 1, '恰好一次 acquire-control');
    assert.equal(wakeCalls.filter((c) => c === 'enable').length, 1, '恰好一次曲线写入');
    assert.deepEqual(adapter.enabledNodeSets.at(-1), SAVED_CURVE, '必须使用保存曲线');
    assert.equal(lifecycle.hasControlIntent, true, '完成后应有活动控制意图');
    assert.ok(logs.some((l) => l.includes('auto-start-applied')), '必须记录自动启动成功');
  });

  await test('S10 FAN-933 矩阵3：手动点击与偏好无关，走同一条 rearm 路径', async () => {
    const adapter = new WakeHostAdapter();
    const launcher = new SimLauncher();
    const lifecycle = await armResidentHostAfterWake(adapter, launcher, 4, false);
    const wakeMark = adapter.calls.length;
    await lifecycle.start();           // 等价于 FanView 的 confirmFanHostBeforeWrite()
    await lifecycle.apply(SAVED_CURVE);
    const wakeCalls = adapter.calls.slice(wakeMark);
    assert.equal(adapter.resumeCalls, 1, `手动接入同样只允许一次 /api/resume（actual=${adapter.resumeCalls}）`);
    assert.equal(wakeCalls.filter((c) => c === 'enable').length, 1, '手动接入应完成一次曲线写入');
  });

  await test('S11 FAN-933 矩阵4：已有活动意图的唤醒仍走既有恢复链，不 rearm、不覆盖曲线', async () => {
    const adapter = new WakeHostAdapter();
    adapter.alreadyOpen = true;        // Host 自己的恢复 owner 已重开 HC
    const launcher = new SimLauncher();
    const lifecycle = await armResidentHostAfterWake(adapter, launcher, 2, true);
    const wakeMark = adapter.calls.length;
    await lifecycle.resume();
    const replayEnables = adapter.calls.slice(wakeMark).filter((c) => c === 'enable').length;
    assert.equal(replayEnables, 1, '既有恢复链应恰好重放一次曲线');
    assert.equal(adapter.resumeCalls, 0, '有活动意图时不得走新控制 rearm（不调 /api/resume）');
    const logs: string[] = [];
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = true;
    api.setLatestResumeGeneration(2);
    await api.ensureFanAutoStart('resume', 2);
    assert.equal(adapter.resumeCalls, 0, '已有意图时自动启动必须 no-op');
    assert.equal(adapter.calls.slice(wakeMark).filter((c) => c === 'enable').length, replayEnables,
      '不得重复写曲线');
  });

  await test('S12 FAN-933 矩阵5：同代并发进入只允许一个在途 rearm', async () => {
    const adapter = new WakeHostAdapter();
    const launcher = new SimLauncher();
    const lifecycle = await armResidentHostAfterWake(adapter, launcher, 6, false);
    const wakeMark = adapter.calls.length;
    await Promise.all([lifecycle.start(), lifecycle.start(), lifecycle.start()]);
    assert.equal(adapter.resumeCalls, 1, `同代并发只允许一次 /api/resume（actual=${adapter.resumeCalls}）`);
    assert.equal(adapter.calls.slice(wakeMark).filter((c) => c === 'resume').length, 1, '不得重复 /api/resume');
    await lifecycle.start();           // 恢复后再进入：已准入同代，直接沿用控制门
    assert.equal(adapter.resumeCalls, 1, '恢复完成后同代再进入不得再次 rearm');
  });

  await test('S13 FAN-933 矩阵7：恢复不收敛 ⇒ 有界失败、零 acquire、零写入、不报成功', async () => {
    const adapter = new WakeHostAdapter();
    adapter.convergeAfterPolls = Number.MAX_SAFE_INTEGER;   // Host 永远不重开
    const launcher = new SimLauncher();
    const lifecycle = await armResidentHostAfterWake(adapter, launcher, 8, false, 400); // 有界 400ms
    const wakeMark = adapter.calls.length;
    let failure = '';
    try {
      await lifecycle.start();
    } catch (error) {
      failure = error instanceof Error ? error.message : String(error);
    }
    assert.ok(/FAN_RESUME_WAIT_DEADLINE|FAN_RESUME_ADMISSION_/.test(failure),
      `必须给出明确可重试错误（got=${failure}）`);
    const wakeCalls = adapter.calls.slice(wakeMark);
    assert.equal(wakeCalls.filter((c) => c === 'acquire').length, 0, '未恢复不得 acquire-control');
    assert.equal(wakeCalls.filter((c) => c === 'enable').length, 0, '未恢复不得写曲线');
    assert.equal(lifecycle.hasControlIntent, false, '未恢复不得建立控制意图');
    // 「明确重试可重新进入」：让 Host 可收敛后再点一次即应成功（此时 rearm 探测到已可接管，
    // 本就该 no-op，不再发第二条 /api/resume）。
    adapter.convergeAfterPolls = 1;
    const retryGate = await lifecycle.start();
    assert.equal(retryGate.allowed, true, '明确重试应能重新进入并放行');
    await lifecycle.apply(SAVED_CURVE);
    assert.equal(adapter.calls.slice(wakeMark).filter((c) => c === 'enable').length, 1,
      '重试成功后应完成一次曲线写入');
    assert.ok(adapter.resumeCalls <= 2, `重试不得造成请求风暴（resumeCalls=${adapter.resumeCalls}）`);
  });

  await test('S14 FAN-933 矩阵1：偏好关 + 唤醒 ⇒ 零 /api/resume、零写入', async () => {
    const adapter = new WakeHostAdapter();
    const launcher = new SimLauncher();
    const lifecycle = await armResidentHostAfterWake(adapter, launcher, 9, false);
    const wakeMark = adapter.calls.length;
    const logs: string[] = [];
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = false;
    await lifecycle.resume();
    api.setLatestResumeGeneration(9);
    await api.ensureFanAutoStart('resume', 9);
    const wakeCalls = adapter.calls.slice(wakeMark);
    assert.equal(adapter.resumeCalls, 0, '偏好关：不得发 /api/resume');
    assert.equal(wakeCalls.filter((c) => c === 'enable').length, 0, '偏好关：不得写曲线');
    assert.equal(lifecycle.hasControlIntent, false, '偏好关：不得建立控制意图');
  });

  await test('S15 未确认 native resume-ready：自动启动不得伪造恢复写权', async () => {
    const logs: string[] = [];
    const adapter = new SimAdapter();
    const launcher = new SimLauncher();
    const lifecycle = newLifecycle(adapter, launcher, 200);
    const { env, api } = buildOrchestrator(lifecycle, logs);
    env.preference = true;
    lifecycle.setPowerGeneration(1);
    lifecycle.observePowerBoundary('suspending', 1);
    lifecycle.observePowerBoundary('resuming', 1);
    api.setLatestResumeGeneration(1);
    await api.ensureFanAutoStart('resume', 1);
    assert.equal(adapter.calls.filter(c => c === 'resume').length, 0, '自动启动不能制造 native 唤醒事实');
    assert.equal(adapter.calls.filter(c => c === 'acquire').length, 0, '未确认唤醒不得取租约');
    assert.equal(adapter.enables, 0, '未确认唤醒不得写曲线');
    assert.ok(!logs.some(l => l.includes('auto-start-applied')), '未确认唤醒不得报成功');
  });

  console.log('fan boot/wake auto-start simulation: 15/15 passed (FAN-932 default-off zero action / boot & wake really start+apply / existing intent not re-written / unsupported not faked; FAN-933 wake rearm: 1x /api/resume before acquire, single-flight, bounded failure, no frontend open/openEvents)');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
  process.exit(1);
});
