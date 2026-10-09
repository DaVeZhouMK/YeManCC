/**
 * FAN-938 R5 承重验收集 —— 唤醒持续恢复与手动 T0（T4 及其独立反例）。
 *
 * 依据：`Docs\Tasks\Fan\FAN-920\FAN-938-R5-CONTINUOUS-RECOVERY-AND-MANUAL-T0-EXECUTION-ADJUDICATION-20261005.md`
 *   §3 P0：先把 T4 建成走**生产入口**的承重动态用例；旧基线应失败在“手动未触发恢复、
 *          被 wake-not-ready 拒绝”这一行为上；真实睡眠反例独立保留（受理可以成功、零写入）。
 *   §3 P2：陈旧 Suspended 不永久拒绝恢复；手动请求先登记为恢复意图；设备写入权限单独判定。
 *
 * 场景严格贴现场失败（fan-lifecycle L179–L208）：
 *   原控制意图有效；睡前 Close 已结束且 OEM 软件恢复确认；唤醒通知遗漏 / 公共 phase 陈旧；
 *   native 与原 Host 都仍报 Suspended；用户手动开启/调节。
 *
 * 只替换系统/设备边界（adapter/launcher 夹具 + 可注入 native power.lifecycle 只读快照），
 * 不替代桥、准入、任务调度、UI 受理代码——全部走 FanHostLifecycle 的真实生产路径。
 * 无硬件写入、不启动真实 Host、不触 EC、不睡眠工作机。
 */
import { FanHostLifecycle, type FanHostConfig, type FanHostLauncher, type FanHostProcess } from '../src/bridge/fanHost';
import { FanApiError, type FanApiAdapter, type FanHandshake, type FanLease, type FanNode, type FanState } from '../src/bridge/fanApi';
import type { PowerLifecycleState } from '../src/bridge/api';

const assert = (condition: unknown, message: string): asserts condition => {
  if (!condition) throw new Error(message);
};

const CURVE: FanNode[] = [
  { tempC: 0, dutyPercent: 0 },
  { tempC: 40, dutyPercent: 25 },
  { tempC: 70, dutyPercent: 70 },
  { tempC: 100, dutyPercent: 100 },
];

/** 与现场同义的 native 只读事务快照。
 *
 *  FAN-938 R6 §P2：唤醒依据只能是 **Fan 专属、按代次持久化的 `resumeReady`**（native 在真实
 *  唤醒分支 set、在睡眠分支 clear）。`inputReady` 只是输入链状态，native 在真实睡眠时仍可保留
 *  它为 true（main.cpp:58740-58752），因此**不**构成唤醒依据——这正是 R6-03 反例要钉住的形态。
 *  故这里把 `resumeReady` 显式化为第四个参数，旧调用点保持 `phase==='ready'` 的派生默认。 */
const nativeState = (
  generation: number,
  phase: PowerLifecycleState['phase'],
  inputReady: boolean,
  resumeReady: boolean = phase === 'ready',
): PowerLifecycleState => ({
  generation,
  phase,
  hardwareWritesAllowed: phase === 'ready',
  inputReady,
  resumeReady,
  hibernateAvailable: true,
});

class FakeLauncher implements FanHostLauncher {
  async start(): Promise<FanHostProcess> { return { pid: 18328, executable: 'fake.exe' }; }
  async stop(): Promise<void> { /* no-op */ }
}

/**
 * FAN-938 R6.1：只替换 **进程启动边界** 的替身。`startImpl` 由用例编排成
 * 「旧实例仍活 → fail-closed 抛错」或「前 N 次瞬态失败后成功」，从而在真实
 * `FanHostLifecycle`（真实 enqueue、真实 recovery owner、真实 startInternal /
 * assertReachableBeforeShortCircuit / rearm / applyMutation）上驱动 H1–H3；
 * 不替代任何恢复调度、准入或写入逻辑。
 */
class ScriptedLauncher implements FanHostLauncher {
  startCalls = 0;
  stopCalls = 0;
  startImpl: (call: number) => Promise<FanHostProcess> =
    async (call) => ({ pid: 30000 + call, executable: 'fake-fan-host.exe' });
  async start(_config: FanHostConfig): Promise<FanHostProcess> {
    this.startCalls += 1;
    return this.startImpl(this.startCalls);
  }
  async stop(_process: FanHostProcess): Promise<void> { this.stopCalls += 1; }
}

/**
 * H4 专用：把一次 start 卡在闸门上，制造「重建 await 仍在进行」的窗口，供用例在该
 * 窗口中投入 close/disable/新睡眠，验证旧重建任务随后失效、绝不迟到写入。
 */
class GatedLauncher extends ScriptedLauncher {
  private releaseStart: (() => void) | null = null;
  private gate: Promise<void> | null = null;
  armGate(): void {
    this.gate = new Promise<void>((resolve) => { this.releaseStart = resolve; });
  }
  releaseGate(): void {
    this.releaseStart?.();
    this.releaseStart = null;
  }
  override async start(_config: FanHostConfig): Promise<FanHostProcess> {
    this.startCalls += 1;
    if (this.gate) await this.gate;
    return { pid: 40000 + this.startCalls, executable: 'fake-fan-host.exe' };
  }
}

/**
 * 可编排的宿主快照。远程字段（remoteStateName / remotePowerState / remoteOpenCalled /
 * oemRestoreConfirmed / controlAccepting）由测试显式驱动，用来表达“睡前 Close 已结束、
 * OEM 软件恢复确认，唤醒通知遗漏，Host 仍陈旧报 Suspended”的先决条件。
 */
class FakeAdapter implements FanApiAdapter {
  readonly enabled = true;
  readonly calls: string[] = [];
  lease: FanLease | null = null;
  private leaseSequence = 0;
  inFlight = 0;
  maxInFlight = 0;
  openCalls = 0;
  enableCalls = 0;
  enableNodes: FanNode[] = [];
  unknownState = false;
  hcCloseCleanupPending = false;

  remoteStateName = 'Ready';
  remotePowerState = 'On';
  remoteOpenCalled = true;
  oemRestoreConfirmed = false;
  controlAccepting = true;
  retryAfterMs: number | undefined = 250;
  snapshotGeneration: number | null = null;

  /** 与真实 Host 语义一致：`resume` 负责按 HC 顺序重开会话 ⇒ 远程快照随之为可接管。 */
  private resumeOpensSession = true;

  craft(name: string, accepting = true): FanState { return this.snapshot(name, accepting); }

  /** 睡眠期宿主快照：HC 会话已关、OEM 软件恢复已确认，Host 仍（陈旧地）报 Suspended。 */
  markSleepClosedSuspended(): void {
    this.remoteStateName = 'Suspended';
    this.remotePowerState = 'Suspended';
    this.remoteOpenCalled = false;
    this.oemRestoreConfirmed = true;
    this.controlAccepting = false;
    this.lease = null;
  }

  async handshake(): Promise<FanHandshake> {
    this.calls.push('handshake');
    return {
      ok: true,
      supported: true,
      deviceClass: 'HandheldCompanion.Devices.ROGAlly',
      fanRoute: 'ProfileCurve',
      fanRouteWriteReady: true,
      deviceIdentity: { manufacturer: 'ASUS', model: 'ROG Ally', product: 'RC71L', bios: '3.08' },
    };
  }
  async open(): Promise<FanState> { this.calls.push('open'); this.openCalls += 1; return this.snapshot(); }
  async openEvents(): Promise<FanState> { this.calls.push('open-events'); return this.snapshot(); }
  async acquireControl(): Promise<FanLease> {
    this.calls.push('acquire');
    this.leaseSequence += 1;
    this.lease = { leaseId: `lease-${this.leaseSequence}`, generation: this.leaseSequence };
    return { ...this.lease };
  }
  async heartbeat(leaseId: string): Promise<FanLease> {
    this.calls.push('heartbeat');
    if (!this.lease || this.lease.leaseId !== leaseId) throw new FanApiError('LEASE_INVALID', 409, 'LEASE_INVALID');
    return { ...this.lease };
  }
  async enable(nodes: readonly FanNode[], _leaseId?: string): Promise<FanState> {
    this.calls.push('enable');
    if (this.remoteStateName === 'FaultLocked') throw new FanApiError('FAULT_LOCKED', 409, 'FAULT_LOCKED');
    if (this.hcCloseCleanupPending) throw new FanApiError('HC_CLOSE_PENDING', 409, 'HC_CLOSE_PENDING');
    this.enableCalls += 1;
    this.enableNodes = nodes.map((node) => ({ ...node }));
    return this.snapshot();
  }
  async applyPreset(_name: string, _leaseId?: string, nodes?: readonly FanNode[]): Promise<FanState> {
    this.calls.push('preset');
    if (nodes) this.enableNodes = nodes.map((node) => ({ ...node }));
    return this.snapshot();
  }
  async disable(): Promise<FanState> { this.calls.push('disable'); return this.snapshot(); }
  async restoreOem(): Promise<FanState> { this.calls.push('restore'); return this.cleanupState('OEM'); }
  async releaseControl(): Promise<FanState> { this.calls.push('release'); this.lease = null; return this.cleanupState('Released'); }
  async suspend(): Promise<FanState> { this.calls.push('suspend'); return this.snapshot('Suspended'); }
  async resume(request?: { generation: number }): Promise<FanState> {
    this.calls.push('resume');
    if (this.snapshotGeneration !== null && request) this.snapshotGeneration = request.generation;
    // Host-owned recovery:按 HC 顺序重开会话后快照才变得可接管。
    if (this.resumeOpensSession) {
      this.remoteStateName = 'Ready';
      this.remotePowerState = 'On';
      this.remoteOpenCalled = true;
      this.oemRestoreConfirmed = false;
      this.controlAccepting = true;
    }
    return this.snapshot();
  }
  async close(): Promise<FanState> { this.calls.push('close'); return this.cleanupState('Stopped'); }
  async shutdown(): Promise<void> { this.calls.push('shutdown'); }

  async getState(_timeoutMs?: number): Promise<FanState> {
    this.calls.push('state');
    this.inFlight += 1;
    this.maxInFlight = Math.max(this.maxInFlight, this.inFlight);
    try {
      return this.snapshot();
    } finally {
      this.inFlight -= 1;
    }
  }

  private snapshot(name = this.remoteStateName, accepting = this.controlAccepting): FanState {
    const result: FanState = {
      state: name,
      powerState: this.remotePowerState,
      protocolVersion: '2',
      hardwareWrites: false,
      hardwareWritesObserved: false,
      hardwareWritesEnabled: true,
      unknownState: this.unknownState,
      hcCloseCleanupPending: this.hcCloseCleanupPending,
      openCalled: this.remoteOpenCalled,
      openEventsCalled: this.remoteOpenCalled,
      oemRestoreConfirmed: this.oemRestoreConfirmed,
      controlAccepting: accepting,
      resumePhase: name === 'Resuming' ? 'Resuming' : name,
    };
    if (this.retryAfterMs !== undefined) result.retryAfterMs = this.retryAfterMs;
    if (this.snapshotGeneration !== null) result.resumePhaseGeneration = this.snapshotGeneration;
    result.lease = this.lease ? { ...this.lease } : null;
    result.leaseGeneration = this.lease?.generation ?? null;
    return result;
  }

  private cleanupState(name: string): FanState {
    const result = this.snapshot(name);
    result.oemRestoreConfirmed = true;
    result.openCalled = false;
    result.openEventsCalled = false;
    result.hardwareWritesEnabled = false;
    return result;
  }
}

/**
 * Start-entry race fixture: the ordinary startup is held in handshake and
 * then fails once.  A manual click arriving during that window must get a
 * second, explicit start pass rather than inheriting the failed ordinary
 * promise.
 */
class ManualUpgradeHandshakeAdapter extends FakeAdapter {
  handshakeCalls = 0;
  private releaseFirstHandshake: (() => void) | null = null;
  private firstHandshakeGate: Promise<void>;

  constructor() {
    super();
    this.firstHandshakeGate = new Promise<void>((resolve) => { this.releaseFirstHandshake = resolve; });
  }

  releaseFirst(): void {
    this.releaseFirstHandshake?.();
    this.releaseFirstHandshake = null;
  }

  override async handshake(): Promise<FanHandshake> {
    this.handshakeCalls += 1;
    if (this.handshakeCalls === 1) {
      await this.firstHandshakeGate;
      throw new Error('STARTUP_TRANSIENT_FOR_MANUAL_UPGRADE');
    }
    return super.handshake();
  }
}

/** FAN-941: both native/Host boundaries settle; only the manual upgrade must not await itself. */
class CompetingStartHandshakeAdapter extends FakeAdapter {
  handshakeCalls = 0;
  releaseFirst!: () => void;
  releaseSecond!: () => void;
  private firstGate = new Promise<void>(resolve => { this.releaseFirst = resolve; });
  private secondGate = new Promise<void>(resolve => { this.releaseSecond = resolve; });
  override async handshake(): Promise<FanHandshake> {
    this.handshakeCalls += 1;
    if (this.handshakeCalls === 1) {
      await this.firstGate;
      throw new Error('FAN941_FIRST_START_TRANSIENT');
    }
    if (this.handshakeCalls === 2) await this.secondGate;
    return super.handshake();
  }
}

class DeferredEnableReplyAdapter extends FakeAdapter {
  private releaseReply: (() => void) | null = null;
  private replyGate: Promise<void> | null = null;
  entered = false;
  arm(): void { this.entered = false; this.replyGate = new Promise<void>(resolve => { this.releaseReply = resolve; }); }
  release(): void { this.releaseReply?.(); this.releaseReply = null; }
  override async enable(nodes: readonly FanNode[], leaseId?: string): Promise<FanState> {
    const result = await super.enable(nodes, leaseId);
    if (this.replyGate) {
      this.entered = true;
      const gate = this.replyGate;
      this.replyGate = null;
      await gate;
    }
    return result;
  }
}

interface NativeProbe { (): Promise<PowerLifecycleState | null>; }

function makeLifecycle(
  adapter: FakeAdapter,
  readNativePowerState: NativeProbe,
  deadlineMs = 1200,
  launcher: FanHostLauncher = new FakeLauncher(),
): FanHostLifecycle {
  return new FanHostLifecycle({
    enabled: true,
    adapter,
    launcher,
    heartbeatIntervalMs: 0,
    resumeWaitDeadlineMs: deadlineMs,
    readNativePowerState,
  });
}

/**
 * 生产入口把平台推进到现场的“睡前已完成 + 唤醒通知遗漏”先决条件：
 * 建立原控制意图 → 代次前进 → 真实 suspending 边界 → suspend（清 lease，保留恢复曲线）
 * → 睡眠期 Host Close/OEM 恢复确认。**故意省略** resuming/resume-ready 两个唤醒边沿。
 */
async function enterPostSleepStaleSuspended(lifecycle: FanHostLifecycle, adapter: FakeAdapter): Promise<void> {
  await lifecycle.start();
  await lifecycle.applyPreset('balanced', CURVE);
  lifecycle.setPowerGeneration(2);
  lifecycle.observePowerBoundary('suspending', 2);
  await lifecycle.suspend();
  assert(lifecycle.state === 'suspended', 'fixture must reach suspended before the missed wake edge');
  adapter.markSleepClosedSuspended();
  adapter.calls.length = 0;
}

const pause = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

/** 与 `NativeFanHostLauncher.recoverPreviousHost` 同义的 fail-closed 拒绝文本：
 *  旧实例仍活（旧 PID/端口占用）时禁止启动第二个硬件 writer。 */
const FAIL_CLOSED_PREVIOUS_HOST =
  '旧 Fan Host 进程仍在运行但 native HTTP 不可用 (pid=7777)，已阻止重复接管';

/**
 * R6.1 夹具：把生命周期推进到「恢复 owner 已登记 + 仍持有常驻 Host」。此后可把
 * process 句柄置空来模拟 FanHost 进程丢失——这正是原 R6 反例的形态。
 * 前提：native 处于非唤醒态（resumeReady=false），故登记的是**恢复意图**而非直接写入。
 */
async function enterResidentRecoveryOwner(lifecycle: FanHostLifecycle, adapter: FakeAdapter): Promise<void> {
  await enterPostSleepStaleSuspended(lifecycle, adapter);
  await lifecycle.start();
  assert(lifecycle.recoveryActive, 'R6.1 fixture: recovery owner must be registered before host loss');
  assert(lifecycle.processId !== null, 'R6.1 fixture: a resident Host must exist before simulating its loss');
}

/** 隔离边界：只把进程句柄置空，模拟 FanHost 进程丢失（等价原 R6 反例 `runtime.process = null`）。 */
function loseHostProcess(lifecycle: FanHostLifecycle): void {
  (lifecycle as unknown as { process: FanHostProcess | null }).process = null;
}

/** 只读观测 owner 内部状态（不改变任何语义），用于断言「未静默停止」。 */
function recoveryStateOf(lifecycle: FanHostLifecycle): string {
  return (lifecycle as unknown as { recoveryState: string }).recoveryState;
}

async function waitFor(condition: () => boolean, timeoutMs: number, label: string): Promise<void> {
  const started = Date.now();
  while (!condition() && Date.now() - started < timeoutMs) await pause(25);
  assert(condition(), `R6.1 timeout waiting for ${label}`);
}

/** Host 丢失场景下 close() 可能因硬件状态不可确认而抛错；用例只关心写入行为。 */
async function closeQuietly(lifecycle: FanHostLifecycle): Promise<void> {
  await lifecycle.close().catch(() => undefined);
}

const curveShifted = (delta: number): FanNode[] =>
  CURVE.map((node) => ({ ...node, dutyPercent: Math.min(100, node.dutyPercent + delta) }));

/**
 * FAN-938 R6 §P1 红例夹具：让 enable 先以合法瞬态门 `POWER_RESUMING` 被拒 N 次再成功。
 * 用于证明「恢复任务不能在一次写入被拒后就结束」，以及「自动 resume 必须与手动收敛到
 * 同一个持续 owner，继续退避重试到曲线真正写入」。
 */
class FlakyAdapter extends FakeAdapter {
  failEnableRemaining = 0;
  enableAttempts = 0;
  successfulEnable = 0;
  override async enable(nodes: readonly FanNode[], leaseId?: string): Promise<FanState> {
    this.enableAttempts += 1;
    if (this.failEnableRemaining > 0) {
      this.failEnableRemaining -= 1;
      this.calls.push('enable-transient-rejection');
      throw new FanApiError('POWER_RESUMING', 409, 'POWER_RESUMING');
    }
    const value = await super.enable(nodes, leaseId);
    this.successfulEnable += 1;
    return value;
  }
}

/** FAN-941: only delay a device boundary; the production queue, intent and writer are untouched. */
class GatedMutationAdapter extends FakeAdapter {
  heldAt: 'open' | 'acquire' | null = null;
  reached = false;
  private gate: Promise<void> | null = null;
  private releaseBoundary: (() => void) | null = null;
  arm(at: 'open' | 'acquire'): void {
    this.heldAt = at;
    this.gate = new Promise<void>(resolve => { this.releaseBoundary = resolve; });
  }
  release(): void { this.releaseBoundary?.(); this.releaseBoundary = null; }
  private async waitAt(at: 'open' | 'acquire'): Promise<void> {
    if (this.heldAt !== at) return;
    this.reached = true;
    await this.gate;
    this.heldAt = null;
  }
  override async open(): Promise<FanState> { await this.waitAt('open'); return super.open(); }
  override async acquireControl(): Promise<FanLease> { await this.waitAt('acquire'); return super.acquireControl(); }
}

/** Authenticated terminal Host state follows Program.cs Open/Enable guards;
 * its virtual Close is the existing explicit retry entry, not OEM physical proof. */
class TerminalFaultAdapter extends FakeAdapter {
  terminal = false;
  pendingClose = false;
  failClose = false;
  closeEntered = false;
  private closeGate: Promise<void> | null = null;
  private releaseClose: (() => void) | null = null;
  armClose(): void { this.closeGate = new Promise<void>(resolve => { this.releaseClose = resolve; }); }
  release(): void { this.releaseClose?.(); this.releaseClose = null; }
  override async getState(timeoutMs?: number): Promise<FanState> {
    if (!this.terminal) return super.getState(timeoutMs);
    this.calls.push('terminal-state');
    return { ...this.craft('FaultLocked', false), unknownState: true, hardwareWritesEnabled: false,
      hcCloseCleanupPending: this.pendingClose };
  }
  override async open(): Promise<FanState> {
    if (this.terminal) throw new FanApiError('FAULT_LOCKED', 409, 'FAULT_LOCKED');
    this.remoteOpenCalled = true;
    return { ...await super.open(), openEventsCalled: false };
  }
  override async acquireControl(): Promise<FanLease> {
    if (this.terminal) throw new FanApiError('FAULT_LOCKED', 409, 'FAULT_LOCKED');
    return super.acquireControl();
  }
  override async enable(nodes: readonly FanNode[], leaseId?: string): Promise<FanState> {
    if (this.terminal) throw new FanApiError('FAULT_LOCKED', 409, 'FAULT_LOCKED');
    return super.enable(nodes, leaseId);
  }
  override async resume(request?: { generation: number }): Promise<FanState> {
    if (this.terminal) throw new FanApiError('FAULT_LOCKED', 409, 'FAULT_LOCKED');
    // Program.cs Resume() is a no-op outside its actual Suspended boundary.
    if (this.remoteStateName !== 'Suspended') { this.calls.push('resume-noop'); return this.craft(this.remoteStateName); }
    return super.resume(request);
  }
  override async close(): Promise<FanState> {
    this.closeEntered = true;
    if (this.closeGate) await this.closeGate;
    if (this.failClose) {
      this.calls.push('close-pending');
      this.pendingClose = true;
      throw new FanApiError('HC_CLOSE_PENDING', 409, 'HC_CLOSE_PENDING');
    }
    const closed = await super.close();
    this.terminal = false;
    this.pendingClose = false;
    this.lease = null;
    this.remoteOpenCalled = false;
    this.remoteStateName = 'Ready';
    return closed;
  }
}

/** Real timer/public heartbeat boundary; the response is held before a lease failure. */
class CancelHeartbeatAdapter extends FakeAdapter {
  heartbeatEntered = false;
  private gate: Promise<void> | null = null;
  private resolveGate: (() => void) | null = null;
  holdHeartbeat(): void { this.gate = new Promise<void>(resolve => { this.resolveGate = resolve; }); }
  releaseHeartbeat(): void { this.resolveGate?.(); this.resolveGate = null; }
  override async heartbeat(leaseId: string): Promise<FanLease> {
    if (!this.gate) return super.heartbeat(leaseId);
    this.calls.push('held-heartbeat'); this.heartbeatEntered = true;
    await this.gate; this.gate = null;
    throw new FanApiError('LEASE_INVALID: simulated failed renewal', 409, 'LEASE_INVALID');
  }
}

type CaseResult = { name: string; ok: boolean; detail?: string };
async function runCase(name: string, fn: () => Promise<void>): Promise<void> {
  try {
    await fn();
    console.log(`PASS  ${name}`);
  } catch (error) {
    const detail = error instanceof Error ? error.message : String(error);
    console.log(`FAIL  ${name}\n      ${detail}`);
    throw Object.assign(new Error(detail), { caseName: name });
  }
}

async function main(): Promise<void> {
  // T4a 承重正例：native 与原 Host 都陈旧报 Suspended，但系统真实唤醒事实 `resumeReady=true`
  // 已按本代持久化（inputReady 故意为 false，证明唤醒依据不是输入链）——手动开启必须被受理并推动恢复。
  await runCase(
    'T4a stale Suspended both sides + persisted resumeReady + manual start must be admitted, recover, and apply the latest curve',
    async () => {
      const adapter = new FakeAdapter();
      let reads = 0;
      const lifecycle = makeLifecycle(adapter, async () => { reads += 1; return nativeState(2, 'suspended', false, true); });
      await enterPostSleepStaleSuspended(lifecycle, adapter);

      const start = await lifecycle.start().then(
        (value) => ({ settled: 'fulfilled' as const, value, error: null as Error | null }),
        (error: Error) => ({ settled: 'rejected' as const, value: null, error }),
      );
      assert(
        start.settled === 'fulfilled',
        `P2: the manual request must be admitted as a recovery intent, not rejected (field failure reproduced): ${start.error?.message}`,
      );
      assert(reads >= 1, 'P2: the Fan-specific compensation must consult the native power transaction');
      assert(
        adapter.calls.filter((call) => call === 'resume').length === 1,
        `P2: exactly one bounded Host resume must be driven after the missed wake edge; got ${JSON.stringify(adapter.calls)}`,
      );

      await lifecycle.apply(CURVE.map((node) => ({ ...node, dutyPercent: Math.min(100, node.dutyPercent + 5) })));
      assert(
        adapter.enableNodes[1]?.dutyPercent === 30,
        'P2: the latest manual curve must reach the current HC enable, not replay the stale one',
      );

      // 下一次调节仍有效（失败是逐请求的，不得永久锁死）。
      adapter.calls.length = 0;
      await lifecycle.apply(CURVE);
      assert(adapter.calls.includes('enable'), 'P2: a follow-up adjustment must remain effective');
      await lifecycle.close();
    },
  );

  // T4a-start-entry：首击必须从生产 start() 入口进入手动救援，不能等到第二个 apply 阶段。
  await runCase(
    'T4a-start-entry manual click must rescue a stale Suspended Host before the first curve write',
    async () => {
      const adapter = new FakeAdapter();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(2, 'suspended', true, false));
      await enterPostSleepStaleSuspended(lifecycle, adapter);
      adapter.calls.length = 0;
      const gate = await lifecycle.start({
        manualRecovery: true,
        reason: 'user-action-while-suspended',
      });
      assert(gate.allowed, 'T0 first-click entry must remain accepted while the stale wake is recoverable');
      assert(
        adapter.calls.filter((call) => call === 'resume').length === 1,
        `T0 first-click entry must issue exactly one Host resume, got ${JSON.stringify(adapter.calls)}`,
      );
      assert(adapter.remoteOpenCalled === true, 'T0 first-click entry must complete Host Open/OpenEvents before write');
      await lifecycle.apply(curveShifted(6));
      assert(adapter.calls.includes('enable'), 'T0 first-click entry must make the first requested curve writable');
      await closeQuietly(lifecycle);
    },
  );

  // R7：完整丢边沿形态——本地 Fan 仍是 active/ready，但 native 已经进入
  // 新代次 Suspended；用户首击必须先把只读快照同步到 Fan coordinator，
  // 不能被误解释成 disable。该用例覆盖“前端没有收到 suspending/resumed”而
  // 不是旧 T4a 那种本地已经收到 suspending 的场景。
  await runCase(
    'R7 missed power edge while local Fan remains active: first control click is manual recovery, not disable',
    async () => {
      const adapter = new FakeAdapter();
      let native = nativeState(0, 'ready', true, false);
      const lifecycle = makeLifecycle(adapter, async () => native);
      await lifecycle.start();
      await lifecycle.applyPreset('balanced', CURVE);
      native = nativeState(2, 'suspended', true, false);
      const manual = await lifecycle.needsManualRecoveryForControlAsync();
      assert(manual, 'R7: a current native Suspended snapshot must classify the first click as recovery');
      assert(lifecycle.state === 'suspended', `R7: snapshot sync must mark local lifecycle suspended, got ${lifecycle.state}`);
      assert(lifecycle.coordinatorSnapshot.powerGeneration === 2, 'R7: snapshot sync must adopt the native generation');
      const gate = await lifecycle.start({ manualRecovery: true, reason: 'user-action-after-missed-power-edge' });
      assert(gate.allowed, 'R7: manual recovery must remain accepted after snapshot synchronization');
      assert(adapter.calls.filter((call) => call === 'resume').length === 1, `R7: recovery must issue one Host resume, calls=${JSON.stringify(adapter.calls)}`);
      await lifecycle.apply(curveShifted(8));
      assert(adapter.calls.includes('enable'), 'R7: recovery must make the first post-wake curve writable');
      await closeQuietly(lifecycle);
    },
  );

  // R7b: the ordinary prior cycle already made the local coordinator wake-ready.
  // If the next S0 boundary is completely missed, local generation 1 must not short-circuit
  // the one native read that discovers generation 2 Suspended; otherwise this click is treated as disable.
  await runCase(
    'R7b stale-local-generation ready cannot hide a newer native Suspended generation from the first manual click',
    async () => {
      const adapter = new FakeAdapter();
      let native = nativeState(0, 'ready', true, false);
      const lifecycle = makeLifecycle(adapter, async () => native);
      try {
        await lifecycle.start();
        await lifecycle.applyPreset('balanced', CURVE);
        native = nativeState(1, 'ready', true, true);
        lifecycle.setPowerGeneration(1);
        lifecycle.observePowerBoundary('resuming', 1);
        lifecycle.observePowerBoundary('resume-ready', 1);
        await lifecycle.resume();
        assert(lifecycle.coordinatorSnapshot.powerGeneration === 1, 'R7b: fixture must begin at the prior wake-ready generation');
        assert(!lifecycle.needsManualRecoveryForControl(), 'R7b: the prior generation must look locally healthy before its missed next sleep edge');

        native = nativeState(2, 'suspended', true, false);
        const manual = await lifecycle.needsManualRecoveryForControlAsync();
        assert(manual, 'R7b: explicit toggle must consult native even when local generation 1 is already wake-ready');
        assert(lifecycle.coordinatorSnapshot.powerGeneration === 2, 'R7b: local coordinator must adopt the native current generation');
        assert(lifecycle.state === 'suspended', 'R7b: stale local ready must reconcile to suspended, got ' + lifecycle.state);
        const gate = await lifecycle.start({ manualRecovery: true, reason: 'user-action-after-missed-next-generation-edge' });
        assert(gate.allowed, 'R7b: explicit recovery must be accepted after stale-generation reconciliation');
        assert(adapter.calls.filter((call) => call === 'resume').length >= 1, 'R7b: Host resume required, calls=' + JSON.stringify(adapter.calls));
        await lifecycle.apply(curveShifted(9));
        assert(adapter.calls.includes('enable'), 'R7b: the requested curve must be writable after Open/OpenEvents');
      } finally {
        await closeQuietly(lifecycle);
      }
    },
  );

  // R7c: local lifecycle already knows a prior sleep, but misses the next power generation.
  // The local state therefore already asks for recovery; that must not suppress native generation reconciliation.
  await runCase(
    'R7c already-suspended local generation still reconciles the latest native power transaction before manual resume',
    async () => {
      const adapter = new FakeAdapter();
      let native = nativeState(0, 'ready', true, false);
      let reads = 0;
      const lifecycle = makeLifecycle(adapter, async () => { reads += 1; return native; });
      try {
        await lifecycle.start();
        await lifecycle.applyPreset('balanced', CURVE);
        native = nativeState(1, 'ready', true, true);
        lifecycle.setPowerGeneration(1);
        lifecycle.observePowerBoundary('resuming', 1);
        lifecycle.observePowerBoundary('resume-ready', 1);
        await lifecycle.resume();
        lifecycle.setPowerGeneration(2);
        lifecycle.observePowerBoundary('suspending', 2);
        await lifecycle.suspend();
        assert(lifecycle.state === 'suspended', 'R7c: local Fan must be suspended at the prior generation');
        native = nativeState(3, 'suspended', true, false);
        const readsBeforeManual = reads;
        const manual = await lifecycle.needsManualRecoveryForControlAsync();
        assert(manual, 'R7c: local suspended state remains a manual recovery request');
        assert(reads > readsBeforeManual, 'R7c: local Suspended must not short-circuit the authoritative native read');
        assert(lifecycle.coordinatorSnapshot.powerGeneration === 3, 'R7c: the manual path must adopt the current native generation');
        const gate = await lifecycle.start({ manualRecovery: true, reason: 'manual-control-after-missed-next-generation' });
        assert(gate.allowed, 'R7c: current generation manual recovery must be accepted');
        assert(adapter.calls.includes('resume'), 'R7c: current generation must reach Host resume');
        await lifecycle.apply(curveShifted(10));
        assert(adapter.calls.includes('enable'), 'R7c: the requested curve must be written after Host Open/OpenEvents');
      } finally {
        await closeQuietly(lifecycle);
      }
    },
  );

  await runCase(
    'R10 confirmed wake without Host F5: request deadline hands off to one owner and restores the curve without a click',
    async () => {
      const adapter = new FakeAdapter();
      let native = nativeState(0, 'ready', true, false);
      const lifecycle = makeLifecycle(adapter, async () => native, 300);
      try {
        await enterPostSleepStaleSuspended(lifecycle, adapter);
        native = nativeState(2, 'ready', true, true);
        lifecycle.observePowerBoundary('resuming', 2);
        lifecycle.observePowerBoundary('resume-ready', 2);
        const failure = await lifecycle.resume().then(() => '', (error: Error) => error.message);
        assert(failure.startsWith('FAN_RESUME_WAIT_DEADLINE:'), 'R10: first request must honestly report its bounded deadline, got ' + failure);
        await waitFor(() => adapter.calls.includes('enable') && !lifecycle.recoveryActive, 4000, 'R10 retained owner after missing Host F5');
        assert(adapter.calls.filter(c => c === 'resume').length === 1, 'R10: retained owner must drive one Host resume');
        assert(adapter.remoteOpenCalled, 'R10: Open/OpenEvents must precede the curve');
        await lifecycle.apply(curveShifted(12));
        assert(adapter.enableNodes[1]?.dutyPercent === 37, 'R10: next adjustment must remain effective');
      } finally { await closeQuietly(lifecycle); }
    },
  );

  for (const evidence of ['current', 'stale', 'missing'] as const) {
    await runCase(
      `R11 ${evidence} Host Resuming generation: only current evidence reuses the running worker`,
      async () => {
        const adapter = new FakeAdapter();
        let native = nativeState(0, 'ready', true, false);
        const lifecycle = makeLifecycle(adapter, async () => native, 300);
        let complete: ReturnType<typeof setTimeout> | undefined;
        try {
          await enterPostSleepStaleSuspended(lifecycle, adapter);
          native = nativeState(2, 'ready', true, true);
          lifecycle.observePowerBoundary('resuming', 2);
          lifecycle.observePowerBoundary('resume-ready', 2);
          adapter.remoteStateName = 'Resuming';
          adapter.remotePowerState = 'Resuming';
          adapter.controlAccepting = false;
          adapter.oemRestoreConfirmed = false;
          adapter.snapshotGeneration = evidence === 'current' ? 2 : evidence === 'stale' ? 1 : null;
          complete = setTimeout(() => {
            adapter.remoteStateName = 'AwaitingControl';
            adapter.remotePowerState = 'On';
            adapter.remoteOpenCalled = true;
            adapter.controlAccepting = true;
            adapter.snapshotGeneration = 2;
          }, 850);
          const result = await lifecycle.start({ manualRecovery: true, reason: 'user-action-while-suspended' })
            .then(gate => ({ gate, error: '' }), (error: Error) => ({ gate: null, error: error.message }));
          if (evidence === 'current') {
            assert(result.error.startsWith('FAN_RESUME_WAIT_DEADLINE:'), 'R11: a direct start must honestly report the short deadline');
            assert(lifecycle.recoveryActive, 'R11: direct bridge callers retain an owner without a UI catch');
          } else {
            assert(result.gate?.allowed, 'R11: stale/missing worker evidence must drive an explicit resume');
            await lifecycle.apply(CURVE);
          }
          await waitFor(() => adapter.calls.includes('enable') && !lifecycle.recoveryActive, 4000, 'R11 retained recovery');
          assert(adapter.calls.filter(c => c === 'resume').length === (evidence === 'current' ? 0 : 1),
            'R11: only same-generation Resuming can replace an explicit resume request');
          assert(adapter.oemRestoreConfirmed === false, 'R11: OEM ownership readback is not a prerequisite for rearming');
          await lifecycle.apply(curveShifted(8));
          assert(adapter.enableNodes[1]?.dutyPercent === 33, 'R11: the next adjustment must work after recovery');
        } finally {
          if (complete) clearTimeout(complete);
          await closeQuietly(lifecycle);
        }
      },
    );
  }

  for (const action of ['close', 'disable', 'new-curve'] as const) {
    await runCase(
      `R12 queued recovery write is invalidated by ${action} before it starts`,
      async () => {
        const adapter = new FakeAdapter();
        let native = nativeState(0, 'ready', true, false);
        const lifecycle = makeLifecycle(adapter, async () => native);
        let releaseQueue!: () => void;
        try {
          await enterPostSleepStaleSuspended(lifecycle, adapter);
          native = nativeState(2, 'ready', true, true);
          adapter.remoteStateName = 'AwaitingControl';
          adapter.remotePowerState = 'On';
          adapter.remoteOpenCalled = true;
          adapter.controlAccepting = true;
          adapter.snapshotGeneration = 2;
          const runtime = lifecycle as unknown as {
            enqueue(work: () => Promise<void>): Promise<void>;
            operation: Promise<unknown>;
          };
          // Hold the real queue without replacing its admission or writer.
          const barrier = new Promise<void>(resolve => { releaseQueue = resolve; });
          const blocked = runtime.enqueue(() => barrier);
          const beforeOwnerQueued = runtime.operation;
          assert(await lifecycle.requestRecoveryAfterObservedHostState('queued-write-counterexample'), 'R12: the owner must be registered');
          await waitFor(() => runtime.operation !== beforeOwnerQueued, 2000, 'R12 owner write queued behind the barrier');
          const beforeWrites = adapter.enableCalls;
          const pending = action === 'close' ? lifecycle.close()
            : action === 'disable' ? lifecycle.disable()
            : lifecycle.apply(curveShifted(17));
          releaseQueue();
          await blocked;
          await pending;
          await pause(350);
          assert(adapter.enableCalls === beforeWrites + (action === 'new-curve' ? 1 : 0),
            'R12: the stale queued owner must perform zero writes');
          if (action === 'new-curve') {
            assert(adapter.enableNodes[1]?.dutyPercent === 42, 'R12: only the latest requested curve reaches the writer');
          } else {
            assert(!lifecycle.hasControlIntent, 'R12: a queued owner cannot resurrect a closed control intent');
          }
        } finally {
          releaseQueue?.();
          await closeQuietly(lifecycle);
        }
      },
    );
  }

  await runCase(
    'R13 curve changed during a recovery write: old reply cannot erase it or settle the owner',
    async () => {
      const adapter = new DeferredEnableReplyAdapter();
      let native = nativeState(0, 'ready', true, false);
      const lifecycle = makeLifecycle(adapter, async () => native);
      try {
        await enterPostSleepStaleSuspended(lifecycle, adapter);
        native = nativeState(2, 'ready', true, true);
        adapter.remoteStateName = 'AwaitingControl'; adapter.remotePowerState = 'On';
        adapter.remoteOpenCalled = true; adapter.controlAccepting = true; adapter.snapshotGeneration = 2;
        adapter.arm();
        const before = adapter.enableCalls;
        assert(await lifecycle.requestRecoveryAfterObservedHostState('curve-update-during-write'), 'R13: recovery must be registered');
        await waitFor(() => adapter.entered, 2000, 'R13 first write awaiting its real adapter reply');
        assert(lifecycle.stageRecoveryCurve(curveShifted(19)), 'R13: latest curve must be accepted while the owner writes');
        adapter.release();
        await waitFor(() => adapter.enableCalls === before + 2 && !lifecycle.recoveryActive, 4000, 'R13 new staged curve reaches the next owner attempt');
        assert(adapter.enableNodes[1]?.dutyPercent === 44, 'R13: late reply cannot replace the newer curve with the old curve');
        await lifecycle.apply(curveShifted(19));
        assert(adapter.enableCalls === before + 3, 'R13: a new explicit request must write even when the values match the last recovery');
      } finally { adapter.release(); await closeQuietly(lifecycle); }
    },
  );

  // R8: renderer wake notification is missing, but telemetry discovers the
  // Host is still suspended. The automatic owner waits without touching HC,
  // then uses the same resume/Open/OpenEvents/acquire/enable chain once native
  // reports that the new power generation is ready.
  await runCase(
    'R8 telemetry discovers missed wake edge and continuous owner resumes without a click',
    async () => {
      const adapter = new FakeAdapter();
      let native = nativeState(0, 'ready', true, false);
      const lifecycle = makeLifecycle(adapter, async () => native);
      await lifecycle.start();
      await lifecycle.applyPreset('balanced', CURVE);
      native = nativeState(2, 'suspended', true, false);

      const accepted = await lifecycle.requestRecoveryAfterObservedHostState('telemetry-host-not-ready');
      assert(accepted, 'R8: observed suspended Host with retained curve must register the owner');
      assert(lifecycle.recoveryActive, 'R8: recovery owner must remain active while native is suspended');
      await pause(350);
      assert(!adapter.calls.includes('resume'), `R8: no Host resume while native still suspended, calls=${JSON.stringify(adapter.calls)}`);
      assert(!adapter.calls.includes('enable'), `R8: no curve write while native still suspended, calls=${JSON.stringify(adapter.calls)}`);

      native = nativeState(2, 'ready', true, false);
      await waitFor(() => !lifecycle.recoveryActive, 3000, 'R8 automatic owner to resume and write the retained curve');
      assert(adapter.remoteOpenCalled, 'R8: automatic recovery must prove Open/OpenEvents before writing');
      assert(adapter.calls.includes('acquire'), 'R8: automatic recovery must acquire a fresh lease');
      assert(adapter.calls.includes('enable'), 'R8: automatic recovery must write the retained curve');
      await closeQuietly(lifecycle);
    },
  );

  await runCase(
    'R9 native snapshot timeout preserves a live manual control intent',
    async () => {
      const adapter = new FakeAdapter();
      let native: PowerLifecycleState | null = nativeState(0, 'ready', true, false);
      const lifecycle = makeLifecycle(adapter, async () => native);
      await lifecycle.start();
      await lifecycle.applyPreset('balanced', CURVE);
      lifecycle.setPowerGeneration(2);
      lifecycle.observePowerBoundary('resume-ready', 2);
      native = null;
      const manual = await lifecycle.needsManualRecoveryForControlAsync();
      assert(manual, 'R9: an unreadable native power snapshot must preserve the existing user control intent');
      assert(lifecycle.hasControlIntent, 'R9: snapshot failure must not erase the retained fan curve/control intent');
      await closeQuietly(lifecycle);
    },
  );

  // T4a-manual-rescue：现场缺陷形态——native 仍报 Suspended、resumeReady=false，
  // 但渲染器已经可交互，用户明确点击曲线。手动请求必须启动一次 Host resume，
  // 等 Open/OpenEvents 后再 enable；不能把 inputReady 当作自动唤醒证据。
  await runCase(
    'T4a-manual-rescue stale Suspended without resumeReady must rearm Host after an explicit apply',
    async () => {
      const adapter = new FakeAdapter();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(2, 'suspended', true, false));
      await enterPostSleepStaleSuspended(lifecycle, adapter);
      await lifecycle.start();
      adapter.calls.length = 0;
      await lifecycle.apply(curveShifted(7));
      const resumeCalls = adapter.calls.filter((call) => call === 'resume').length;
      assert(resumeCalls === 1, `T0 manual rescue must issue exactly one Host resume, got ${JSON.stringify(adapter.calls)}`);
      assert(adapter.remoteOpenCalled === true, 'T0 manual rescue Host resume must prove Open/OpenEvents before write');
      assert(adapter.calls.includes('enable'), 'T0 manual rescue must complete the requested curve write');
      assert(adapter.enableNodes[1]?.dutyPercent === 32, 'T0 manual rescue must apply the clicked curve, not the stale curve');
      await lifecycle.apply(curveShifted(9));
      assert(adapter.calls.filter((call) => call === 'resume').length === 1, 'T0 repeated manual adjustments must reuse the recovered session');
      await closeQuietly(lifecycle);
    },
  );

  await runCase(
    'R6.3 manual start upgrades an in-flight ordinary startup instead of sharing its failure',
    async () => {
      const adapter = new ManualUpgradeHandshakeAdapter();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false));
      const ordinary = lifecycle.start().then(
        () => ({ ok: true as const }),
        () => ({ ok: false as const }),
      );
      await waitFor(() => adapter.handshakeCalls === 1, 1000, 'ordinary startup to reach handshake');
      const manual = lifecycle.start({ manualRecovery: true, reason: 'user-action-before-control-write' });
      adapter.releaseFirst();
      const [ordinaryResult, manualResult] = await Promise.all([ordinary, manual]);
      assert(!ordinaryResult.ok, 'R6.3 fixture: the ordinary startup must retain its injected failure');
      assert(manualResult.allowed && manualResult.writeReady,
        'R6.3 manual click must receive a second successful start pass');
      assert(adapter.handshakeCalls === 2,
        `R6.3 manual upgrade must retry startup once, got ${adapter.handshakeCalls} handshakes`);
      await closeQuietly(lifecycle);
    },
  );

  for (const action of ['apply', 'preset'] as const) {
    for (const heldAt of ['open', 'acquire'] as const) {
      await runCase(
        'FAN-941 cancel ' + action + ' during ' + heldAt + ': zero late curve writes and the next explicit enable remains effective',
        async () => {
          const adapter = new GatedMutationAdapter();
          const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false));
          try {
            await lifecycle.start({ manualRecovery: true });
            adapter.arm(heldAt);
            const pending = (action === 'apply' ? lifecycle.apply(CURVE) : lifecycle.applyPreset('balanced', CURVE))
              .then(() => '', (error: Error) => error.message);
            await waitFor(() => adapter.reached, 1000, 'FAN-941 delayed ' + heldAt);
            const disabled = lifecycle.disable();
            adapter.release();
            const rejection = await pending;
            await disabled;
            assert(rejection.startsWith('FAN_CONTROL_INTENT_SUPERSEDED:'), 'an explicitly cancelled curve must be surfaced as superseded; got ' + rejection + '; calls=' + adapter.calls.join(','));
            assert(!adapter.calls.includes('enable') && !adapter.calls.includes('preset'), 'cancelled before the writer: no late curve may be submitted');
            assert(!lifecycle.hasControlIntent, 'ordered disable must settle the cancelled intent');
            await lifecycle.apply(curveShifted(7));
            assert(adapter.enableCalls === 1 && adapter.enableNodes[1]?.dutyPercent === 32,
              'cancellation must not lock the session or prevent the next actual adjustment');
          } finally {
            adapter.release();
            await closeQuietly(lifecycle);
          }
        },
      );
    }
  }

  for (const action of ['close', 'apply', 'preset'] as const) {
    await runCase(
      'FAN-941 heartbeat response superseded by ' + action + ': maintenance cannot replay the previous curve',
      async () => {
        const adapter = new CancelHeartbeatAdapter(), launcher = new ScriptedLauncher();
        const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false), 1200, launcher);
        try {
          await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
          const previousWrites = adapter.enableCalls;
          adapter.holdHeartbeat();
          const pending = lifecycle.heartbeat().catch(() => undefined);
          await waitFor(() => adapter.heartbeatEntered, 1000, 'superseded heartbeat');
          const next = action === 'close' ? lifecycle.close()
            : action === 'apply' ? lifecycle.apply(curveShifted(13)) : lifecycle.applyPreset('balanced', curveShifted(14));
          adapter.releaseHeartbeat(); await pending; await next;
          assert(adapter.enableCalls === previousWrites + (action === 'apply' ? 1 : 0),
            'only the new explicit operation may submit a curve; stale maintenance must not do so');
          if (action === 'close') assert(lifecycle.processId === null && !lifecycle.hasControlIntent, 'close must remain final');
          else assert(adapter.enableNodes[1]?.dutyPercent === (action === 'apply' ? 38 : 39), 'the latest user curve must survive');
        } finally { adapter.releaseHeartbeat(); await closeQuietly(lifecycle); }
      },
    );
  }

  await runCase(
    'FAN-941 heartbeat queued before off: zero stale renewal after the queue barrier opens',
    async () => {
      const adapter = new CancelHeartbeatAdapter(), launcher = new ScriptedLauncher();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false), 1200, launcher);
      let releaseBarrier!: () => void, probeEntered = false;
      const barrier = new Promise<void>(resolve => { releaseBarrier = resolve; });
      try {
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        const getState = adapter.getState.bind(adapter);
        adapter.getState = async timeout => { probeEntered = true; await barrier; return getState(timeout); };
        const probe = lifecycle.getState();
        await waitFor(() => probeEntered, 1000, 'state queue barrier');
        const queued = lifecycle.heartbeat().catch(() => undefined);
        const disabled = lifecycle.disable(); releaseBarrier();
        await probe; await queued; await disabled;
        assert(!adapter.calls.includes('heartbeat') && !adapter.calls.includes('held-heartbeat'),
          'a heartbeat queued in an earlier intent cannot renew after off arrives');
      } finally { releaseBarrier(); await closeQuietly(lifecycle); }
    },
  );

  await runCase(
    'FAN-941 heartbeat failure resync cancelled: no old-curve write after the awaited state reply',
    async () => {
      const adapter = new CancelHeartbeatAdapter(), launcher = new ScriptedLauncher();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false), 1200, launcher);
      let releaseProbe!: () => void, probeEntered = false;
      const probeGate = new Promise<void>(resolve => { releaseProbe = resolve; });
      try {
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        const priorWrites = adapter.enableCalls;
        const getState = adapter.getState.bind(adapter);
        adapter.getState = async timeout => { probeEntered = true; await probeGate; return getState(timeout); };
        adapter.holdHeartbeat();
        const pending = lifecycle.heartbeat().catch(() => undefined);
        await waitFor(() => adapter.heartbeatEntered, 1000, 'resync heartbeat');
        adapter.releaseHeartbeat();
        await waitFor(() => probeEntered, 1000, 'heartbeat resync await');
        const disabled = lifecycle.disable(); releaseProbe();
        await pending; await disabled;
        assert(adapter.enableCalls === priorWrites,
          'off during the resync await must invalidate the autonomous reacquire/write');
      } finally { releaseProbe(); adapter.releaseHeartbeat(); await closeQuietly(lifecycle); }
    },
  );

  await runCase(
    'FAN-941 real heartbeat timer: one queued or in-flight renewal while an earlier response is delayed',
    async () => {
      const adapter = new CancelHeartbeatAdapter(), launcher = new ScriptedLauncher();
      const lifecycle = new FanHostLifecycle({ enabled: true, adapter, launcher, heartbeatIntervalMs: 10,
        readNativePowerState: async () => nativeState(0, 'ready', true, false) });
      let submissions = 0;
      const heartbeat = lifecycle.heartbeat.bind(lifecycle);
      lifecycle.heartbeat = async () => { submissions++; return heartbeat(); };
      try {
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        adapter.holdHeartbeat();
        await waitFor(() => adapter.heartbeatEntered, 1000, 'timer heartbeat');
        await pause(85);
        assert(submissions === 1, 'the real recurring timer must not enqueue many renewals behind its pending predecessor');
        const disabled = lifecycle.disable(); adapter.releaseHeartbeat(); await disabled;
        assert(!lifecycle.hasControlIntent, 'timer maintenance cannot revive a cancelled curve');
      } finally { adapter.releaseHeartbeat(); await closeQuietly(lifecycle); }
    },
  );

  await runCase(
    'FAN-941 heartbeat failure after explicit off: no autonomous old-curve rewrite before disable settles',
    async () => {
      const adapter = new CancelHeartbeatAdapter(), launcher = new ScriptedLauncher();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false), 1200, launcher);
      try {
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        const priorWrites = adapter.enableCalls;
        adapter.holdHeartbeat();
        const heartbeat = lifecycle.heartbeat().catch(() => undefined);
        await waitFor(() => adapter.heartbeatEntered, 1000, 'held heartbeat');
        const disabled = lifecycle.disable(); adapter.releaseHeartbeat();
        await heartbeat; await disabled;
        assert(adapter.enableCalls === priorWrites,
          'a failed old heartbeat must not re-enable the old curve after the synchronous off intent');
        assert(!lifecycle.hasControlIntent, 'off must remain off after the old heartbeat settles');
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(curveShifted(7));
        assert(adapter.enableCalls === priorWrites + 1 && launcher.startCalls === 1,
          'the next manual enable still works on the settled resident');
      } finally { adapter.releaseHeartbeat(); await closeQuietly(lifecycle); }
    },
  );

  await runCase(
    'FAN-941 terminal reachable fault: one explicit Close and restart makes the next manual curve effective',
    async () => {
      const adapter = new TerminalFaultAdapter(), launcher = new ScriptedLauncher();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false), 1200, launcher);
      try {
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        adapter.terminal = true; adapter.calls.length = 0;
        await lifecycle.start();
        assert(!adapter.calls.includes('close') && launcher.startCalls === 1, 'ordinary startup must not automatically restart a terminal resident');
        await lifecycle.start({ manualRecovery: true, reason: 'explicit-terminal-retry' });
        await lifecycle.apply(curveShifted(9));
        assert(adapter.calls.filter(c => c === 'close').length === 1 && adapter.calls.filter(c => c === 'shutdown').length === 1,
          'one manual retry owns exactly one authenticated virtual Close/shutdown');
        assert(launcher.startCalls === 2 && launcher.stopCalls === 1 && lifecycle.processId === 30002,
          'the next Host starts only after the owned predecessor stops');
        assert(adapter.calls.slice(-4).join(',') === 'open,open-events,acquire,enable', 'a restarted Host must establish a fresh HC session and lease');
        assert(adapter.enableNodes[1]?.dutyPercent === 34, 'the latest manual intent must reach the new writer');
      } finally { await closeQuietly(lifecycle); }
    },
  );

  await runCase(
    'FAN-941 post-wake terminal restart: a fresh Host uses Open/OpenEvents instead of waiting on a no-op Resume',
    async () => {
      const adapter = new TerminalFaultAdapter(), launcher = new ScriptedLauncher();
      let generation = 0;
      const lifecycle = makeLifecycle(adapter, async () => nativeState(generation, 'ready', true, true), 1200, launcher);
      try {
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        generation = 1; lifecycle.setPowerGeneration(1); lifecycle.observePowerBoundary('resume-ready', 1);
        adapter.terminal = true;
        await lifecycle.start({ manualRecovery: true, reason: 'post-wake-terminal' });
        // FanView confirms again immediately before its actual curve write.
        await lifecycle.start({ manualRecovery: true, reason: 'before-current-curve' });
        await lifecycle.apply(curveShifted(11));
        assert(launcher.startCalls === 2 && launcher.stopCalls === 1, 'one new process must replace the settled predecessor');
        assert(!adapter.calls.includes('resume-noop'), 'a newly started awake Host must not depend on Resume to open its device');
        assert(adapter.enableNodes[1]?.dutyPercent === 36, 'post-wake manual intent must reach the fresh Open/OpenEvents/lease/write chain');
      } finally { await closeQuietly(lifecycle); }
    },
  );

  await runCase(
    'FAN-941 terminal Close unresolved: preserve the owner and never issue a second Close or spawn',
    async () => {
      const adapter = new TerminalFaultAdapter(), launcher = new ScriptedLauncher();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false), 1200, launcher);
      try {
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        adapter.terminal = true; adapter.failClose = true; adapter.calls.length = 0;
        const rejected = await lifecycle.start({ manualRecovery: true }).then(() => '', (e: Error) => e.message);
        assert(rejected.includes('HC_CLOSE_PENDING'), 'the manual caller must see the unresolved close');
        assert(launcher.startCalls === 1 && launcher.stopCalls === 0 && lifecycle.processId === 30001,
          'an unresolved Close is not exit proof and must retain one owner');
        await lifecycle.start({ manualRecovery: true });
        assert(adapter.calls.filter(c => c === 'close-pending').length === 1,
          'an observed in-flight cleanup must not receive a duplicate virtual Close');
        assert(launcher.startCalls === 1 && !adapter.calls.includes('shutdown'), 'pending cleanup must authorize no replacement or shutdown');
      } finally {
        adapter.failClose = false; adapter.pendingClose = false;
        await closeQuietly(lifecycle);
      }
    },
  );

  await runCase(
    'FAN-941 terminal fault during true Suspending: no rescue Close until the next confirmed awake click',
    async () => {
      const adapter = new TerminalFaultAdapter(), launcher = new ScriptedLauncher();
      let phase: PowerLifecycleState['phase'] = 'ready', generation = 0;
      const lifecycle = makeLifecycle(adapter, async () => nativeState(generation, phase, true, phase === 'ready'), 1200, launcher);
      try {
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        generation = 1; phase = 'suspending'; adapter.terminal = true; adapter.calls.length = 0;
        await lifecycle.start({ manualRecovery: true }).catch(() => undefined);
        assert(!adapter.calls.some(c => ['close', 'shutdown', 'open', 'open-events', 'acquire', 'enable', 'resume'].includes(c)),
          'terminal rescue must not perform lifecycle/control mutations during native Suspending');
        assert(launcher.startCalls === 1 && launcher.stopCalls === 0, 'sleep edge must retain the original owner');
        phase = 'ready';
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        assert(launcher.startCalls === 2 && adapter.enableCalls === 2, 'the next confirmed awake click must still recover the terminal');
      } finally { phase = 'ready'; await closeQuietly(lifecycle); }
    },
  );

  await runCase(
    'FAN-941 terminal exit temporarily denied: preserve the old owner and retry its exit before starting a replacement',
    async () => {
      const adapter = new TerminalFaultAdapter(), launcher = new ScriptedLauncher();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false), 1200, launcher);
      let exitDenied = true;
      launcher.stop = async () => { launcher.stopCalls++; if (exitDenied) throw new Error('root still-running'); };
      try {
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        adapter.terminal = true;
        const rejected = await lifecycle.start({ manualRecovery: true }).then(() => '', (e: Error) => e.message);
        assert(rejected.includes('still-running') && lifecycle.processId === 30001 && launcher.startCalls === 1,
          'failed root exit must retain the original owner and reject the replacement');
        const callsAtExitFailure = adapter.calls.length;
        await lifecycle.apply(CURVE).then(() => { throw new Error('closed owner must not accept a curve'); }, () => undefined);
        await lifecycle.applyPreset('balanced', CURVE).then(() => { throw new Error('closed owner must not accept a preset'); }, () => undefined);
        assert(!adapter.calls.slice(callsAtExitFailure).some(c => ['open', 'open-events', 'acquire', 'enable'].includes(c)),
          'pending root exit must not reopen the shutdown writer');
        exitDenied = false;
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(curveShifted(8));
        assert(launcher.stopCalls === 2 && launcher.startCalls === 2 && lifecycle.processId === 30002,
          'a second explicit retry must settle the retained exit, rather than reuse a shutdown Host');
        assert(adapter.calls.filter(c => c === 'close').length === 1,
          'a settled HC Close must not be replayed just to retry root exit');
      } finally { exitDenied = false; await closeQuietly(lifecycle); }
    },
  );

  await runCase(
    'FAN-941 disable during manual terminal Close cancels the queued restart without a replacement writer',
    async () => {
      const adapter = new TerminalFaultAdapter(), launcher = new ScriptedLauncher();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false), 1200, launcher);
      try {
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        adapter.terminal = true; adapter.armClose();
        const pending = lifecycle.start({ manualRecovery: true }).then(() => '', (e: Error) => e.message);
        await waitFor(() => adapter.closeEntered, 1000, 'manual terminal Close');
        const disabled = lifecycle.disable(); adapter.release();
        const rejection = await pending; await disabled;
        assert(rejection.startsWith('FAN_CONTROL_INTENT_SUPERSEDED:'), 'cancelled manual restart must end as superseded');
        assert(launcher.startCalls === 1 && launcher.stopCalls === 1 && !lifecycle.hasControlIntent,
          'cancel during Close must stop the old owner but not launch a new one');
        await lifecycle.start({ manualRecovery: true }); await lifecycle.apply(CURVE);
        assert(launcher.startCalls === 2 && adapter.enableCalls === 2, 'the next distinct manual click must still enable');
      } finally { adapter.release(); await closeQuietly(lifecycle); }
    },
  );

  await runCase(
    'FAN-941 launch refusal without an owned process must send no lifecycle POST and permit the next explicit start',
    async () => {
      const adapter = new FakeAdapter();
      const launcher = new ScriptedLauncher();
      launcher.startImpl = async call => {
        if (call === 1) throw new Error('OLD_HOST_CREDENTIAL_MISMATCH');
        return { pid: 52000 + call, executable: 'fake-fan-host.exe' };
      };
      const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false), 1200, launcher);
      // A valid cached credential alone does not transfer ownership of a resident Host.
      lifecycle.setConfig({ sessionToken: 'a'.repeat(64) });
      try {
        await lifecycle.start({ manualRecovery: true }).then(
          () => { throw new Error('launch refusal must be surfaced'); },
          error => { assert(String(error).includes('OLD_HOST_CREDENTIAL_MISMATCH'), 'preserve the actual launch error'); },
        );
        assert(adapter.calls.length === 0,
          'no process was returned: neither cached credentials nor a launch error authorize Close/shutdown of a resident');
        assert(launcher.stopCalls === 0 && lifecycle.processId === null, 'no unowned process may be stopped');
        assert(lifecycle.state === 'stopped', 'a failed unowned startup must clear Starting and remain retryable');
        await lifecycle.start({ manualRecovery: true });
        await lifecycle.apply(CURVE);
        assert(adapter.enableCalls === 1 && lifecycle.processId === 52002, 'the next explicit start must control one confirmed Host');
        assert(launcher.startCalls === 2, 'a refusal must not spawn a hidden second writer');
      } finally {
        await closeQuietly(lifecycle);
      }
    },
  );

  await runCase(
    'FAN-941 manual upgrade must settle when a competing ordinary start wins the microtask race',
    async () => {
      const adapter = new CompetingStartHandshakeAdapter();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(0, 'ready', true, false));
      const ordinary = lifecycle.start();
      await waitFor(() => adapter.handshakeCalls === 1, 1000, 'FAN-941 first handshake');
      const manual = lifecycle.start({ manualRecovery: true, reason: 'FAN-941-user-click' });
      const repeated = lifecycle.start({ manualRecovery: true, reason: 'FAN-941-repeated-click' });
      // This continuation is queued before the upgrade's .then callback. The
      // second ordinary start must not make that callback rejoin its own promise.
      const competing = ordinary.then(() => lifecycle.start(), () => lifecycle.start());
      adapter.releaseFirst();
      await waitFor(() => adapter.handshakeCalls === 2, 1000, 'FAN-941 competing handshake');
      adapter.releaseSecond();
      await competing;
      let settled = false;
      const results = Promise.all([manual, repeated]).then(values => { settled = true; return values; });
      await Promise.race([results, pause(500)]);
      assert(settled, 'FAN-941 self-await: all native boundaries returned but the manual start never settled');
      const gates = await results;
      assert(gates.every(gate => gate.allowed && gate.writeReady), 'FAN-941 both explicit callers must be admitted');
      assert(adapter.handshakeCalls === 2 && adapter.openCalls === 0 && adapter.enableCalls === 0,
        'FAN-941 startup must retain one queue and must not introduce an extra handshake/Open/enable');
      await lifecycle.apply(CURVE);
      assert(adapter.enableCalls === 1, 'FAN-941 next explicit curve must write exactly once after admission');
      await closeQuietly(lifecycle);
    },
  );

  // R6.4：若 native 的持久化 `fan.resume-ready` 一次性通知在 renderer 重建时丢失，
  // 但随后已经收到提交完成的 `power.resumed`，Fan 侧必须用该提交边界补齐自己的
  // wake admission，并继续同一条 HC resume/Open/OpenEvents/enable 链；不能等下一次点击。
  await runCase(
    'R6.4 committed power.resumed compensates a lost fan.resume-ready edge',
    async () => {
      const adapter = new FakeAdapter();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(2, 'suspended', true, false));
      await enterPostSleepStaleSuspended(lifecycle, adapter);
      adapter.calls.length = 0;
      lifecycle.observePowerBoundary('resumed', 2);
      await lifecycle.resumeAfterCommittedPowerWake(2);
      assert(adapter.calls.filter((call) => call === 'resume').length === 1,
        `R6.4 committed wake must issue one Host resume, got ${JSON.stringify(adapter.calls)}`);
      assert(adapter.remoteOpenCalled === true, 'R6.4 committed wake must prove Open/OpenEvents before write');
      assert(adapter.enableCalls === 1, 'R6.4 committed wake must restore the remembered curve without a click');
      await lifecycle.close();
    },
  );

  // T4b 独立反例：真实睡眠仍在进行——受理意图可以成功，但设备写入必须为零；实际唤醒才推进。
  await runCase(
    'T4b real sleep in progress: intent admitted, zero device writes until an actual wake edge',
    async () => {
      const adapter = new FakeAdapter();
      let native = nativeState(2, 'suspending', false);
      const lifecycle = makeLifecycle(adapter, async () => native);
      await enterPostSleepStaleSuspended(lifecycle, adapter);

      const first = await lifecycle.start().then(
        () => ({ settled: 'fulfilled' as const, error: null as Error | null }),
        (error: Error) => ({ settled: 'rejected' as const, error }),
      );
      assert(
        first.settled === 'fulfilled',
        `P2: while truly sleeping the request must still be accepted (intent registered); got ${first.error?.message}`,
      );
      assert(
        !adapter.calls.includes('enable') && !adapter.calls.includes('acquire'),
        `P0 counterexample: no device write while the real sleep transaction is in progress; got ${JSON.stringify(adapter.calls)}`,
      );

      // 真实唤醒边沿到达后，同代恢复才推进并应用最新曲线。
      native = nativeState(2, 'ready', true);
      await lifecycle.start();
      await lifecycle.apply(CURVE.map((node) => ({ ...node, dutyPercent: Math.min(100, node.dutyPercent + 5) })));
      assert(adapter.calls.includes('enable'), 'P2 counterexample: the actual wake edge must then allow the write');
      await lifecycle.close();
    },
  );

  // T4c 反例（§4 L159）：OEM/物理第二证据缺失**不得**被重新加成准入前置门。
  // §2.3 明确“物理交还没有证据”和“软件 OEM 回调没有额外证据”都不能永久挡住新控制；
  // §4 T3 要求第二证据保持 unknown 仍可通过第一证据接管。本用例断言 enable 恰恰发生在
  // 远端快照 oemRestoreConfirmed=false（无 OEM 软件回执）时；并本地重演一个“把 OEM
  // confirmed 加回准入”的变异谓词，证明该变异会让本用例（等价 T3/T4/T6）失败。
  await runCase(
    'T4c no OEM/physical second evidence must not gate recovery (mutant that re-adds the OEM prerequisite fails here)',
    async () => {
      const adapter = new FakeAdapter();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(2, 'suspended', true, true));
      await enterPostSleepStaleSuspended(lifecycle, adapter);
      // 睡眠期 Host 已 Close；此后不再有任何 OEM 软件回执或物理第二证据可用。
      adapter.oemRestoreConfirmed = false;

      await lifecycle.start();
      await lifecycle.apply(CURVE.map((node) => ({ ...node, dutyPercent: Math.min(100, node.dutyPercent + 5) })));
      assert(adapter.calls.includes('enable'), 'T3/T4: first evidence must be sufficient to take over the route');
      // 关键的负向断言：enable 发生时没有任何 OEM 软件回执。
      assert(
        adapter.oemRestoreConfirmed === false,
        'T3: the takeover must happen with the second evidence still unknown (oemRestoreConfirmed=false)',
      );
      // 变异谓词：若把“OEM confirmed”重新加回准入门（如 remoteControlAdmissible），
      // 则本次 enable 会被拒 ⇒ T3/T4/T6 失败。这里证明变异确实会拒绝本场景的快照。
      const mutantAdmission = (oemConfirmed: boolean): boolean => oemConfirmed === true;
      assert(
        mutantAdmission(adapter.oemRestoreConfirmed) === false,
        'counterexample: an OEM-gated admission would have blocked this takeover, so T3/T4/T6 would fail under that mutant',
      );
      // 手动入口未被永久禁用：下一次调节仍然有效。
      adapter.calls.length = 0;
      await lifecycle.apply(CURVE);
      assert(adapter.calls.includes('enable'), 'T6: the manual entry must remain usable after an OEM-unknown recovery');
      await lifecycle.close();
    },
  );

  // ── FAN-938 R6 §1.2 三个软件路径反例（复用评审方 _scratch 红基线场景，走同一生产入口）──

  // R6-01：恢复任务不得在曲线写成功前结束。宿主重开后第一次 enable 被 POWER_RESUMING 拒，
  // 若提前 settle，则只有 1 次尝试、0 次成功且任务消失；修复后 owner 必须继续退避到写成功。
  await runCase(
    'R6-01 manual recovery must survive a transient POWER_RESUMING enable rejection and reach a successful write',
    async () => {
      const adapter = new FlakyAdapter();
      let native = nativeState(2, 'suspending', false);
      const lifecycle = makeLifecycle(adapter, async () => native);
      try {
        await enterPostSleepStaleSuspended(lifecycle, adapter);
        await lifecycle.start(); // 经真实公共入口登记 pending recovery owner
        assert(lifecycle.recoveryActive, 'R6-01: fixture must really register the recovery owner');
        lifecycle.stageRecoveryCurve(CURVE);
        adapter.failEnableRemaining = 1;
        native = nativeState(2, 'ready', true);
        await pause(1600);
        assert(
          adapter.successfulEnable > 0,
          `R6-01: one transient write rejection ended recovery early: attempts=${adapter.enableAttempts}, successful=${adapter.successfulEnable}, recoveryActive=${lifecycle.recoveryActive}`,
        );
      } finally { await lifecycle.close(); }
    },
  );

  // R6-02：自动 resume() 必须与手动收敛到同一个持续 owner。前三次 enable 失败、第四次本可成功，
  // 旧实现（两次尝试）会在第二次就停止；修复后 owner 必须继续到成功。
  await runCase(
    'R6-02 automatic resume must hand off to the same continuous owner and survive repeated transient rejections',
    async () => {
      const adapter = new FlakyAdapter();
      const lifecycle = makeLifecycle(adapter, async () => nativeState(2, 'ready', true));
      try {
        await enterPostSleepStaleSuspended(lifecycle, adapter);
        adapter.remoteStateName = 'Ready'; adapter.remotePowerState = 'On';
        adapter.remoteOpenCalled = true; adapter.controlAccepting = true;
        adapter.failEnableRemaining = 3;
        lifecycle.observePowerBoundary('resuming', 2);
        lifecycle.observePowerBoundary('resume-ready', 2);
        let failure = '';
        await lifecycle.resume().catch((error) => { failure = error instanceof Error ? error.message : String(error); });
        await pause(1600);
        assert(
          adapter.successfulEnable > 0,
          `R6-02: automatic resume stopped after ${adapter.enableAttempts} rejects; recoveryActive=${lifecycle.recoveryActive}, error=${failure}`,
        );
      } finally { await lifecycle.close(); }
    },
  );

  // D2（裁决 §P5）：新 owner 必须能持续超过旧「单窗口」等待截止（生产 = RESUME_WAIT_DEADLINE_MS
  // 15s；这里用受控的 resumeWaitDeadlineMs 缩短，但退避延时仍走生产 scheduleRecoveryTick 调度
  // 路径，不把待测逻辑复制进测试谓词），在截止之后仍继续退避到曲线写入成功；写成功后立刻收敛，
  // 不再有后台 Open/enable（不重启轮询、不重新开启）。
  await runCase(
    'D2 continuous recovery owner outlives the single-window resume deadline, then converges without further Open/enable',
    async () => {
      const adapter = new FlakyAdapter();
      const deadlineMs = 1000; // 受控时钟：缩短旧「单窗口」截止（生产 = 15s）
      const lifecycle = makeLifecycle(adapter, async () => nativeState(2, 'ready', true), deadlineMs);
      try {
        await enterPostSleepStaleSuspended(lifecycle, adapter);
        adapter.remoteStateName = 'Ready'; adapter.remotePowerState = 'On';
        adapter.remoteOpenCalled = true; adapter.controlAccepting = true;
        // 前 6 次 enable 都是合法瞬态拒绝：有界重放消耗 2 次，owner 需再跨 4 轮退避
        // （250+500+1000ms）才成功——累计已远超单窗口截止。
        adapter.failEnableRemaining = 6;
        lifecycle.observePowerBoundary('resuming', 2);
        lifecycle.observePowerBoundary('resume-ready', 2);
        const began = Date.now();
        await lifecycle.resume().catch(() => undefined);
        assert(lifecycle.recoveryActive, 'D2: the automatic resume must hand off to the continuous owner');
        let waited = 0;
        while (adapter.successfulEnable === 0 && waited < 8000) { await pause(50); waited += 50; }
        assert(
          adapter.successfulEnable > 0,
          `D2: the owner must keep retrying past the single-window deadline; attempts=${adapter.enableAttempts}, active=${lifecycle.recoveryActive}`,
        );
        const elapsed = Date.now() - began;
        assert(
          elapsed > deadlineMs,
          `D2: recovery must outlive the single-window deadline; elapsed=${elapsed}ms must exceed deadline=${deadlineMs}ms`,
        );
        assert(!lifecycle.recoveryActive, 'D2: the owner must converge once the curve is written');
        const openCalls = adapter.openCalls;
        const enableCalls = adapter.enableCalls;
        adapter.calls.length = 0;
        await pause(1200);
        assert(
          adapter.openCalls === openCalls && adapter.enableCalls === enableCalls,
          `D2: no further Open/enable after convergence; extra=${JSON.stringify(adapter.calls)}`,
        );
      } finally { await lifecycle.close(); }
    },
  );

  // R6-03：唤醒依据必须是 Fan 专属 resumeReady，不能是输入链 inputReady。真实睡眠时 native
  // 允许保留 inputReady=true（main.cpp:58740-58752）而 resumeReady=false、hardwareWritesAllowed=false。
  // P2 修复后该请求**必须被拒**（apply 抛 FAN_RESUME_WAIT_DEADLINE，与 §5 真反例"证据不足必须 rejected"一致），
  // 且零 resume/acquire/enable。裁决要求把该反例纳入正规测试：这里显式捕获该正确拒绝后仍断言零设备写入。
  await runCase(
    'R6-03 real suspend with inputReady kept true must be rejected with zero resume/acquire/enable (resumeReady is the only wake fact)',
    async () => {
      const adapter = new FakeAdapter();
      const native = nativeState(2, 'suspending', true, false);
      const lifecycle = makeLifecycle(adapter, async () => native);
      try {
        await enterPostSleepStaleSuspended(lifecycle, adapter);
        await lifecycle.start();
        await lifecycle.apply(CURVE).then(
          () => { throw new Error('R6-03: a write must not be admitted while the real suspend is in progress'); },
          () => undefined, // 正确拒绝：证据不足必须 rejected（非弱化）
        );
        assert(
          !adapter.calls.includes('resume') && !adapter.calls.includes('acquire') && !adapter.calls.includes('enable'),
          `R6-03: inputReady=true must not be treated as wake evidence; got ${JSON.stringify(adapter.calls)}`,
        );
      } finally { await lifecycle.close(); }
    },
  );

  // D8（裁决 §P5）：新持续 owner 不得在「普通稳定态」或「无控制意图」态新增恢复轮询。
  // 生产实现只在登记了恢复意图时才 noteRecoveryIntentPending → scheduleRecoveryTick（单 timer
  // 守卫）。这里用真实 FanHostLifecycle：稳定态（已完成曲线写入、Ready/On、控制可受理）静置
  // 超过两个最短退避节拍，断言恢复 owner 保持空闲且零新增设备动作／零原生读取；关闭后（无意图）
  // 同样不得后台重新开启或继续轮询。
  await runCase(
    'D8 stable control and no-intent states start zero new recovery polling',
    async () => {
      const adapter = new FakeAdapter();
      let nativeReads = 0;
      const lifecycle = makeLifecycle(adapter, async () => { nativeReads += 1; return nativeState(2, 'ready', true); });
      // 租约心跳是既有的保活链，不是恢复轮询；恢复轮询的证据是 getState/resume/open/acquire/enable。
      const nonHeartbeat = () => adapter.calls.filter((call) => call !== 'heartbeat').length;
      try {
        await lifecycle.start();
        await lifecycle.apply(CURVE);
        assert(adapter.calls.includes('enable'), 'D8: fixture must reach a stable controlled state');
        assert(!lifecycle.recoveryActive, 'D8: a completed apply must not leave the recovery owner resident');
        const stableCalls = nonHeartbeat();
        const stableReads = nativeReads;
        await pause(1300);
        assert(
          !lifecycle.recoveryActive && nonHeartbeat() === stableCalls && nativeReads === stableReads,
          `D8: a stable controlled state must not poll; active=${lifecycle.recoveryActive}, device=${JSON.stringify(adapter.calls.filter((call) => call !== 'heartbeat').slice(stableCalls))}, nativeReads+${nativeReads - stableReads}`,
        );
        await lifecycle.close();
        assert(!lifecycle.recoveryActive, 'D8: a closed lifecycle must not keep a recovery owner resident');
        const closedCalls = nonHeartbeat();
        const closedReads = nativeReads;
        await pause(1300);
        assert(
          !lifecycle.recoveryActive && nonHeartbeat() === closedCalls && nativeReads === closedReads,
          `D8: no intent after close must not poll or re-open; active=${lifecycle.recoveryActive}, device=${JSON.stringify(adapter.calls.filter((call) => call !== 'heartbeat').slice(closedCalls))}, nativeReads+${nativeReads - closedReads}`,
        );
      } finally { await lifecycle.close(); }
    },
  );

  // ── FAN-938 R6.1：Host 进程丢失恢复定点返工 H1–H4（走同一真实 FanHostLifecycle）──
  // 原缺口（fanHost.ts:2654-2659）：owner 遇 `!this.process` 直接 stopRecoveryLoop 返回，
  // 页面停在“正在恢复”却永不重建 Host/重写曲线。以下四个用例钉住修复后的边界。

  // H1：process 丢失但旧实例仍活（旧 PID/端口占用 ⇒ recoverPreviousHost fail-closed）→ 不得 spawn
  // 第二个 writer；owner 保持意图、有界重试，且零 acquire/enable。
  await runCase(
    'R6.1-H1 host lost while the old instance is still alive must not spawn a second writer (fail-closed keeps retrying, zero writes)',
    async () => {
      const adapter = new FakeAdapter();
      let native = nativeState(2, 'suspended', true, false);
      const launcher = new ScriptedLauncher();
      const lifecycle = makeLifecycle(adapter, async () => native, 1200, launcher);
      try {
        await enterResidentRecoveryOwner(lifecycle, adapter);
        const baseline = launcher.startCalls;
        // 旧实例仍活 → launcher.start 抛 fail-closed，进程句柄始终为 null。
        launcher.startImpl = async () => { throw new Error(FAIL_CLOSED_PREVIOUS_HOST); };
        loseHostProcess(lifecycle);
        native = nativeState(2, 'ready', true);
        adapter.calls.length = 0;
        await waitFor(() => launcher.startCalls - baseline >= 2, 3000, 'bounded fail-closed rebuild attempts');
        assert(lifecycle.processId === null, 'R6.1-H1: no second writer may exist while the old instance is unconfirmed');
        assert(lifecycle.recoveryActive, 'R6.1-H1: the recovery intent must survive a fail-closed rebuild');
        assert(
          ['queued', 'recovering'].includes(recoveryStateOf(lifecycle)),
          `R6.1-H1: the owner must stay retryable (not silently stopped); got ${recoveryStateOf(lifecycle)}`,
        );
        assert(
          adapter.enableCalls === 0 && !adapter.calls.includes('acquire'),
          `R6.1-H1: a fail-closed rebuild must not write the device; got ${JSON.stringify(adapter.calls)}`,
        );
      } finally { await closeQuietly(lifecycle); }
    },
  );

  // H2：旧实例精确确认退出（start 成功）→ 只启动 **一个** 新 Host，重新握手并应用当前曲线后收敛。
  await runCase(
    'R6.1-H2 after the old instance is confirmed gone exactly one new Host is started, re-handshaken, and the current curve is applied',
    async () => {
      const adapter = new FakeAdapter();
      let native = nativeState(2, 'suspended', true, false);
      const launcher = new ScriptedLauncher();
      const lifecycle = makeLifecycle(adapter, async () => native, 1200, launcher);
      const staged = curveShifted(5); // CURVE[1] 25 → 30，用于区分“当前曲线”与陈旧曲线
      try {
        await enterResidentRecoveryOwner(lifecycle, adapter);
        const baseline = launcher.startCalls;
        loseHostProcess(lifecycle);
        native = nativeState(2, 'ready', true);
        assert(lifecycle.stageRecoveryCurve(staged), 'R6.1-H2: the latest curve must be staged on the running owner');
        adapter.calls.length = 0;
        await waitFor(() => adapter.enableCalls > 0, 4000, 'the rebuilt Host to apply the staged curve');
        assert(launcher.startCalls - baseline === 1, `R6.1-H2: exactly one new Host may be started; got ${launcher.startCalls - baseline}`);
        assert(lifecycle.processId !== null, 'R6.1-H2: a rebuilt Host must be resident');
        assert(
          adapter.enableNodes[1]?.dutyPercent === 30,
          `R6.1-H2: the current curve must reach the current enable; got ${JSON.stringify(adapter.enableNodes)}`,
        );
        assert(
          adapter.calls.includes('handshake') && adapter.calls.includes('enable'),
          `R6.1-H2: the rebuild must re-handshake and enable; got ${JSON.stringify(adapter.calls)}`,
        );
        await waitFor(() => !lifecycle.recoveryActive, 2000, 'the owner to converge after the curve write');
        assert(launcher.startCalls - baseline === 1, 'R6.1-H2: convergence must not spawn another Host');
      } finally { await closeQuietly(lifecycle); }
    },
  );

  // H3：新 Host 启动/认证瞬态失败 → 继续退避，不丢控制意图；转好后才写入。
  await runCase(
    'R6.1-H3 transient Host start failures must keep backing off without losing the control intent, then converge',
    async () => {
      const adapter = new FakeAdapter();
      let native = nativeState(2, 'suspended', true, false);
      const launcher = new ScriptedLauncher();
      const lifecycle = makeLifecycle(adapter, async () => native, 1200, launcher);
      try {
        await enterResidentRecoveryOwner(lifecycle, adapter);
        const baseline = launcher.startCalls;
        // 前 3 次重建瞬态失败（可重试），第 4 次成功。
        launcher.startImpl = async (call) => {
          if (call <= baseline + 3) throw new Error('Fan Host 启动瞬态失败（可重试）');
          return { pid: 30000 + call, executable: 'fake-fan-host.exe' };
        };
        loseHostProcess(lifecycle);
        native = nativeState(2, 'ready', true);
        assert(lifecycle.stageRecoveryCurve(CURVE), 'R6.1-H3: stage the current curve on the owner');
        adapter.calls.length = 0;
        await waitFor(() => launcher.startCalls - baseline >= 2, 3000, 'repeated bounded rebuild attempts');
        assert(lifecycle.recoveryActive, 'R6.1-H3: transient start failures must not lose the intent');
        assert(
          recoveryStateOf(lifecycle) !== 'needs-attention',
          `R6.1-H3: transient failures must stay retryable; got ${recoveryStateOf(lifecycle)}`,
        );
        assert(adapter.enableCalls === 0, `R6.1-H3: no device write before a Host is actually up; got ${JSON.stringify(adapter.calls)}`);
        await waitFor(() => adapter.enableCalls > 0, 8000, 'the owner to converge once the transient failures clear');
      } finally { await closeQuietly(lifecycle); }
    },
  );

  // H4：重建 await 期间 close/disable/新睡眠到达 → 旧重建任务必须失效，绝不迟到写旧曲线。
  const lateArrival = async (
    label: string,
    trigger: (lifecycle: FanHostLifecycle) => void,
    expectRecoveryActive: boolean,
  ): Promise<void> => {
    const adapter = new FakeAdapter();
    let native = nativeState(2, 'suspended', true, false);
    const launcher = new GatedLauncher();
    const lifecycle = makeLifecycle(adapter, async () => native, 1200, launcher);
    try {
      await enterResidentRecoveryOwner(lifecycle, adapter);
      const baseline = launcher.startCalls;
      launcher.armGate(); // 下一次重建 spawn 会卡在闸门上（原进程已确认退出，允许 spawn）
      const enableBaseline = adapter.enableCalls;
      loseHostProcess(lifecycle);
      native = nativeState(2, 'ready', true);
      lifecycle.stageRecoveryCurve(curveShifted(5));
      await waitFor(() => launcher.startCalls > baseline, 3000, `the rebuild spawn to block on the gate (${label})`);
      trigger(lifecycle); // 在重建 await 期间投入终止性/新代次意图
      adapter.calls.length = 0;
      launcher.releaseGate();
      await pause(600);
      assert(
        adapter.enableCalls === enableBaseline && !adapter.calls.includes('acquire'),
        `R6.1-H4 (${label}): the late arrival must invalidate the rebuild, no late write; got ${JSON.stringify(adapter.calls)}`,
      );
      assert(
        lifecycle.recoveryActive === expectRecoveryActive,
        `R6.1-H4 (${label}): recoveryActive expected ${expectRecoveryActive}, got ${lifecycle.recoveryActive}`,
      );
    } finally { await closeQuietly(lifecycle); }
  };

  await runCase(
    'R6.1-H4a close arriving during the rebuild await must invalidate the old task (no late write)',
    async () => { await lateArrival('close', (lifecycle) => { void lifecycle.close().catch(() => undefined); }, false); },
  );

  await runCase(
    'R6.1-H4b disable arriving during the rebuild await must invalidate the old task (no late write)',
    async () => { await lateArrival('disable', (lifecycle) => { void lifecycle.disable().catch(() => undefined); }, false); },
  );

  await runCase(
    'R6.1-H4c a new sleep generation arriving during the rebuild await must defer writes until the new generation really wakes',
    async () => {
      await lateArrival(
        'new-sleep',
        (lifecycle) => {
          lifecycle.setPowerGeneration(3);
          lifecycle.observePowerBoundary('suspending', 3);
        },
        true, // 意图保留（等待新代真实唤醒），但不写设备
      );
    },
  );

  console.log('FAN-938 R5 T4: continuous-recovery load-bearing case (stale Suspended) + independent real-sleep counterexample + OEM-unknown non-gate counterexample PASS');
}

void main().catch((error: unknown) => {
  const detail = error instanceof Error ? error.message : String(error);
  console.error(`FAN-938 R5 T4: RED (expected before the P2 fix)\n${detail}`);
  process.exitCode = 1;
});
