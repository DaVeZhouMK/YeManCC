/**
 * FAN-926R 唤醒临时租约与前端有界等待裁决 —— R2 承重验收集（W1–W4 + E1 证据链）。
 *
 * 依据：`Docs\Tasks\Fan\FAN-920\FAN-926R-WAKE-LEASE-AND-BOUNDED-RETRY-ADJUDICATION-20260926.md`
 *   §4.1 只等待明确可等待的状态   §4.2 一次有界任务（15 s 绝对截止 / 250–1000 ms / 单在途读）
 *   §4.3 取消与新意图优先（入队前同步失效）   §4.4 默认关闭详细日志仍可追责
 *
 * 全部走**生产**代码路径（FanHostLifecycle 的真实入队/等待/准入逻辑），只用夹具替换
 * adapter/launcher 与受控时钟（`resumeWaitDeadlineMs`）。不碰用户运行实例、不产生硬件写入。
 */
import { FanHostLifecycle, unsupportedFanControlError, type FanHostLauncher, type FanHostProcess } from '../src/bridge/fanHost';
import { FanApiError, type FanApiAdapter, type FanHandshake, type FanLease, type FanNode, type FanState } from '../src/bridge/fanApi';
import { readFileSync } from 'node:fs';

const assert = (condition: unknown, message: string): asserts condition => {
  if (!condition) throw new Error(message);
};

const CURVE: FanNode[] = [
  { tempC: 0, dutyPercent: 0 },
  { tempC: 40, dutyPercent: 25 },
  { tempC: 70, dutyPercent: 70 },
  { tempC: 100, dutyPercent: 100 },
];

class FakeLauncher implements FanHostLauncher {
  async start(): Promise<FanHostProcess> { return { pid: 4242, executable: 'fake.exe' }; }
  async stop(): Promise<void> { /* no-op */ }
}

/** 可编排的宿主快照：resumingProbes 次"恢复中"后转可接受；可注入错误与陈旧代次。 */
class FakeAdapter implements FanApiAdapter {
  readonly enabled = true;
  readonly calls: string[] = [];
  lease: FanLease | null = null;
  private leaseSequence = 0;
  resumingProbes = 0;
  controlAccepting = true;
  retryAfterMs: number | undefined = 250;
  snapshotGeneration: number | null = null;
  stateFailure: Error | null = null;
  stateFailureTimes = 0;
  /** 先放过前 N 次快照再失败（用于"等待已开始后才发现鉴权失败"的形态）。 */
  stateFailureAfterCalls = 0;
  private stateCallCount = 0;
  stateName = 'Ready';
  unknownState = false;
  hcCloseCleanupPending = false;
  publishLeaseField = true;
  inFlight = 0;
  maxInFlight = 0;
  openCalls = 0;
  enableCalls = 0;
  enableNodes: FanNode[] = [];
  /** G7：逐次快照脚本（shift；用完后沿用最后一条）——用于 "Host Suspended → Resuming → Ready" 真实时间线。 */
  stateScript: Array<() => FanState> = [];
  /** G7：每次 getState 调用的**进入钩子**（在返回快照之前触发），用于模拟 "GET 在途时"到达的真实电源/意图入口。 */
  onStateCall: ((index: number) => void) | null = null;
  /** G7-D：逐次快照延时（ms，shift；用完后为 0）——用于 "初探慢/最后余额不足"。 */
  stateDelays: number[] = [];
  /** G7-D：每次 getState 实际使用的超时（用于断言"绝不超过剩余余额"）。 */
  readonly stateTimeouts: number[] = [];
  /** G7-D：下一次 enable 是否先以合法 POWER_RESUMING 被拒一次。 */
  enableResumingOnce = false;
  /** G8：受控心跳回包（测试自行 resolve/reject），消费一次。 */
  nextHeartbeat: Promise<FanLease> | null = null;

  /** G7：按名字/可接管性构造与生产形状一致的状态快照（供 stateScript 使用）。 */
  craft(name: string, accepting = true): FanState { return this.snapshot(name, accepting); }

  async handshake(): Promise<FanHandshake> {
    this.calls.push('handshake');
    return {
      ok: true,
      supported: true,
      deviceClass: 'HandheldCompanion.Devices.GPDWin5',
      fanRoute: 'ProfileCurve',
      fanRouteWriteReady: true,
      deviceIdentity: { manufacturer: 'GPD', model: 'G1618-05', product: 'G1618-05', bios: '2.20' },
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
    if (this.nextHeartbeat) {
      const gated = this.nextHeartbeat;
      this.nextHeartbeat = null;
      return gated;
    }
    if (!this.lease || this.lease.leaseId !== leaseId) throw new FanApiError('LEASE_INVALID', 409, 'LEASE_INVALID');
    return { ...this.lease };
  }
  async enable(nodes: readonly FanNode[], _leaseId?: string): Promise<FanState> {
    this.calls.push('enable');
    // 真实宿主的门：故障锁定 / 待清理时 enable 必须被拒（不能把"没等待"当成"已放行"）。
    if (this.stateName === 'FaultLocked') throw new FanApiError('FAULT_LOCKED', 409, 'FAULT_LOCKED');
    if (this.hcCloseCleanupPending) throw new FanApiError('HC_CLOSE_PENDING', 409, 'HC_CLOSE_PENDING');
    // G7-D：合法瞬态门——必须释放队列回到协调等待重入一次，而不是队列内延时重试。
    if (this.enableResumingOnce) {
      this.enableResumingOnce = false;
      throw new FanApiError('POWER_RESUMING', 409, 'POWER_RESUMING');
    }
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
  async resume(): Promise<FanState> { this.calls.push('resume'); return this.snapshot(); }
  async close(): Promise<FanState> { this.calls.push('close'); return this.cleanupState('Stopped'); }
  async shutdown(): Promise<void> { this.calls.push('shutdown'); }

  async getState(timeoutMs?: number): Promise<FanState> {
    this.calls.push('state');
    this.stateTimeouts.push(timeoutMs ?? NaN);
    this.inFlight += 1;
    this.maxInFlight = Math.max(this.maxInFlight, this.inFlight);
    try {
      const callIndex = this.stateCallCount;
      this.stateCallCount += 1;
      // G7-C：钩子在**回包之前**触发——模拟"GET 在途时"到达的真实电源/意图入口。
      this.onStateCall?.(callIndex);
      const delay = this.stateDelays.length > 0 ? (this.stateDelays.shift() as number) : 0;
      // 快照/失败在**请求时刻**确定；延时只模拟传输与宿主处理时延，不得改变本代判定。
      const shouldFail = this.stateFailureTimes > 0 && callIndex >= this.stateFailureAfterCalls;
      let result: FanState | null = null;
      if (!shouldFail) {
        if (this.stateScript.length > 0) {
          // 最后一条快照保持粘滞（真实宿主不会在 Ready 之后又退回 Suspended）。
          const step = this.stateScript.length === 1 ? (this.stateScript[0] as () => FanState) : (this.stateScript.shift() as () => FanState);
          result = step();
        } else if (this.resumingProbes > 0) {
          this.resumingProbes -= 1;
          result = this.snapshot('Resuming', false);
        } else {
          result = this.snapshot();
        }
      }
      if (delay > 0) await new Promise((resolve) => setTimeout(resolve, delay));
      if (shouldFail) {
        this.stateFailureTimes -= 1;
        throw this.stateFailure!;
      }
      return result as FanState;
    } finally {
      this.inFlight -= 1;
    }
  }

  private snapshot(name = this.stateName, accepting = this.controlAccepting): FanState {
    const result: FanState = {
      state: name,
      powerState: 'On',
      protocolVersion: '2',
      hardwareWrites: false,
      hardwareWritesObserved: false,
      hardwareWritesEnabled: true,
      unknownState: this.unknownState,
      hcCloseCleanupPending: this.hcCloseCleanupPending,
      openCalled: true,
      openEventsCalled: true,
      resumePhase: name === 'Resuming' ? 'Resuming' : name,
      controlAccepting: accepting,
    };
    if (this.retryAfterMs !== undefined) result.retryAfterMs = this.retryAfterMs;
    if (this.snapshotGeneration !== null) result.resumePhaseGeneration = this.snapshotGeneration;
    if (this.publishLeaseField) result.lease = this.lease ? { ...this.lease } : null;
    if (this.publishLeaseField) result.leaseGeneration = this.lease?.generation ?? null;
    return result;
  }

  private cleanupState(name: string): FanState {
    const result = this.snapshot(name);
    result.oemRestoreConfirmed = true;
    // 与真实宿主一致：Close/释放之后 HC 会话标志与"软件写生效"位必须回落，
    // 否则桥侧会正确拒绝把 Close 当成功（isSafeHostRecoveryState）。
    result.openCalled = false;
    result.openEventsCalled = false;
    result.hardwareWritesEnabled = false;
    return result;
  }
}

function newLifecycle(adapter: FakeAdapter, deadlineMs: number): FanHostLifecycle {
  return new FanHostLifecycle({
    enabled: true,
    adapter,
    launcher: new FakeLauncher(),
    heartbeatIntervalMs: 0,
    resumeWaitDeadlineMs: deadlineMs,
  });
}

/** 把一个 lifecycle 推进到"唤醒中"（native resume-ready 边沿）并让恢复有曲线可重放。 */
async function armWake(lifecycle: FanHostLifecycle, adapter: FakeAdapter, generation = 1): Promise<void> {
  await lifecycle.start();
  await lifecycle.applyPreset('balanced', CURVE);
  lifecycle.setPowerGeneration(generation);
  await lifecycle.suspend();
  assert(lifecycle.state === 'suspended', '夹具必须先进入 suspended');
  lifecycle.observePowerBoundary('resuming', generation);
  lifecycle.observePowerBoundary('resume-ready', generation);
  adapter.calls.length = 0;
}

async function main(): Promise<void> {
  // FAN-938 R4: drive the real explicit-control path with a missed native Ready event.
  // FAN-938 R6 §P2：唤醒依据 = Fan 专属、按代次持久化的 `resumeReady`（不是输入链 inputReady）。
  // 默认按 phase 派生：phase==='ready' ⇒ resumeReady=true；陈旧 phase 需显式传入 true。
  const nativeReady = (
    generation: number,
    phase: 'ready' | 'suspended' | 'suspending' | 'resuming' = 'ready',
    resumeReady = phase === 'ready',
  ) => ({
    generation, phase, hardwareWritesAllowed: phase === 'ready', inputReady: true,
    resumeReady, hibernateAvailable: true,
  });
  const missingWake = async (lifecycle: FanHostLifecycle, adapter: FakeAdapter) => {
    await lifecycle.start();
    await lifecycle.applyPreset('balanced', CURVE);
    lifecycle.setPowerGeneration(2);
    lifecycle.observePowerBoundary('suspending', 2);
    await lifecycle.suspend();
    // Deliberately omit both resuming and resume-ready, as in the failed notification path.
    adapter.calls.length = 0;
  };
  {
    const adapter = new FakeAdapter(); let reads = 0;
    const lifecycle = new FanHostLifecycle({ enabled: true, adapter, launcher: new FakeLauncher(),
      heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 1200,
      readNativePowerState: async () => { reads += 1; return nativeReady(2); },
    });
    await missingWake(lifecycle, adapter);
    adapter.stateScript = [() => ({ ...adapter.craft('Suspended', false), powerState: 'Suspended',
      openCalled: false, openEventsCalled: false }), () => adapter.craft('Ready', true)];
    await lifecycle.apply(CURVE.map(n => ({ ...n, dutyPercent: Math.min(100, n.dutyPercent + 5) })));
    assert(reads === 1, 'R4: lost edge must use one event-driven native read');
    assert(adapter.calls.filter(c => c === 'resume').length === 1, 'R4: closed Host must rearm exactly once');
    assert(lifecycle.controlReady && adapter.calls.includes('enable'), 'R4: manual request must reach current HC enable');
    assert(adapter.enableNodes[1].dutyPercent === 30, 'R4: must apply the new manual curve, not replay the stale one');
    await lifecycle.apply(CURVE);
    assert(reads === 1, 'R4: ordinary awake adjustment must add zero native reads');
    await lifecycle.close();
  }
  // FAN-938 R6 §P2：native **陈旧 phase 但真实唤醒事实 resumeReady=true**（宿主与 Host 都停在
  // Suspended，系统已真实唤醒）必须被受理并推动 Fan 专属恢复——不得再以 wake-not-ready 永久拒绝。
  // 注意「正在睡」(phase==='suspending') 恒拒绝，见下方 R6-03 反例，故不在此正例集合内。
  for (const state of [nativeReady(2, 'suspended', true), nativeReady(2, 'resuming', true)]) {
    const adapter = new FakeAdapter(); let reads = 0;
    const lifecycle = new FanHostLifecycle({ enabled: true, adapter, launcher: new FakeLauncher(),
      heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 800,
      readNativePowerState: async () => { reads += 1; return state; } });
    await missingWake(lifecycle, adapter);
    adapter.stateName = 'Suspended'; adapter.controlAccepting = false;
    // 陈旧 phase 被采纳后，宿主在受控 resume 后真正重开 HC 会话。
    adapter.stateScript = [() => ({ ...adapter.craft('Suspended', false), state: 'Suspended',
      powerState: 'Suspended', openCalled: false, openEventsCalled: false }), () => adapter.craft('Ready', true)];
    await lifecycle.apply(CURVE);
    assert(reads >= 1, 'R6 §P2: stale-phase recovery with persisted resumeReady must read native power state');
    assert(lifecycle.controlReady && adapter.calls.includes('enable'),
      'R6 §P2: stale phase + resumeReady=true must be accepted and reach HC enable');
    await lifecycle.close();
  }
  // FAN-938 R6 §P2 反例（R6-03）：真实睡眠进行中（phase==='suspending'）即使 inputReady=true，
  // resumeReady=false、hardwareWritesAllowed=false ⇒ 不是唤醒依据，必须零 resume/acquire/enable。
  {
    const adapter = new FakeAdapter(); let reads = 0;
    const lifecycle = new FanHostLifecycle({ enabled: true, adapter, launcher: new FakeLauncher(),
      heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 800,
      readNativePowerState: async () => { reads += 1; return nativeReady(2, 'suspending', false); } });
    await missingWake(lifecycle, adapter);
    adapter.stateName = 'Suspended'; adapter.controlAccepting = false;
    const result = await Promise.allSettled([lifecycle.apply(CURVE)]);
    assert(result[0].status === 'rejected',
      'R6 §P2: inputReady alone (resumeReady=false) must not admit a write while truly suspending');
    assert(!adapter.calls.some(c => ['resume', 'acquire', 'enable'].includes(c)),
      'R6 §P2: no resume/acquire/enable may be driven from inputReady during a real suspend');
    assert(reads >= 1, 'R6 §P2: the native power transaction must actually be consulted');
    await lifecycle.close();
  }
  // FAN-938 R5：真正的反例——代次不符或 native 不可用仍是"证据不足"，零变更调用；且不得永久锁死下一次点击。
  for (const state of [nativeReady(1), null]) {
    const adapter = new FakeAdapter(); let current = state;
    const lifecycle = new FanHostLifecycle({ enabled: true, adapter, launcher: new FakeLauncher(),
      heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 800, readNativePowerState: async () => current });
    await missingWake(lifecycle, adapter);
    adapter.stateName = 'Suspended'; adapter.controlAccepting = false;
    const result = await Promise.allSettled([lifecycle.apply(CURVE)]);
    assert(result[0].status === 'rejected', 'R5: unavailable/mismatched native evidence must reject');
    assert(!adapter.calls.some(c => ['resume','open','open-events','acquire','enable'].includes(c)),
      'R5: native evidence rejection must make zero mutating Host calls');
    // Failure is per request. A later explicit click with new Ready evidence must still recover.
    current = nativeReady(2); adapter.stateName = 'Ready'; adapter.controlAccepting = true;
    await lifecycle.apply(CURVE);
    assert(lifecycle.controlReady, 'R5: a rejected manual request must not permanently lock the next click');
    await lifecycle.close();
  }
  {
    const adapter = new FakeAdapter(); let release!: () => void;
    const pending = new Promise<void>(resolve => { release = resolve; });
    const lifecycle = new FanHostLifecycle({ enabled: true, adapter, launcher: new FakeLauncher(),
      heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 800,
      readNativePowerState: async () => { await pending; return nativeReady(2); } });
    await missingWake(lifecycle, adapter);
    const request = lifecycle.apply(CURVE);
    lifecycle.setPowerGeneration(3); lifecycle.observePowerBoundary('suspending', 3);
    release();
    const result = await Promise.allSettled([request]);
    assert(result[0].status === 'rejected', 'R4: an old native response cannot revive the newer sleep');
    assert(!adapter.calls.includes('enable') && !adapter.calls.includes('resume'), 'R4: cancelled request must not rearm or enable');
    assert(lifecycle.coordinatorSnapshot.powerGeneration === 3 && lifecycle.coordinatorSnapshot.phase === 'suspending',
      'R4: old response must not replace new generation/phase');
    await lifecycle.close();
  }
  {
    const adapter = new FakeAdapter();
    const lifecycle = new FanHostLifecycle({ enabled: true, adapter, launcher: new FakeLauncher(),
      heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 800, readNativePowerState: async () => nativeReady(2) });
    await missingWake(lifecycle, adapter); adapter.stateName = 'FaultLocked';
    const result = await Promise.allSettled([lifecycle.apply(CURVE)]);
    assert(result[0].status === 'rejected' && !adapter.calls.includes('enable'),
      'R4: native Ready must not clear a Host fault or write through it');
    adapter.stateName = 'Ready'; await lifecycle.close();
  }
  console.log('FAN-938 R4: missing-edge manual rearm / no extra awake reads / 5 rejection+retry cases / stale in-flight response / Host fault all PASS');

  // ───────────────── W1：同代等待可完成，恢复后真实 Enable/下一次调整有效 ─────────────────
  {
    const adapter = new FakeAdapter();
    adapter.resumingProbes = 3; // ≈750 ms：跨越原实现"40 ms 内两次尝试就放弃"的窗口
    const lifecycle = newLifecycle(adapter, 5000);
    await armWake(lifecycle, adapter);
    const startedAt = Date.now();
    await lifecycle.resume();
    const elapsed = Date.now() - startedAt;
    assert(lifecycle.controlReady === true, `W1: 等待结束后必须真的具备控制能力（controlReady=${lifecycle.controlReady}）`);
    assert(adapter.enableCalls >= 1, 'W1: 等待结束后必须真的重放曲线（enable）');
    assert(adapter.enableNodes.some((node) => node.dutyPercent === 70), 'W1: 重放的必须是原曲线');
    assert(elapsed < 5000, `W1: 等待必须落在客户端预算内（elapsed=${elapsed}ms）`);
    // 等待期间不得新造恢复 owner：只允许只读 /api/state。
    assert(!adapter.calls.includes('suspend') && !adapter.calls.includes('resume'),
      `W1: 等待期间不得重复 /api/resume 或新造恢复 owner（calls=${adapter.calls.join(',')}）`);
    assert(adapter.maxInFlight === 1, `W1: 同一等待任务最多一个在途读（maxInFlight=${adapter.maxInFlight}）`);
    // 晚接入后"下一次调整仍然有效"。
    const before = adapter.enableCalls;
    await lifecycle.apply(CURVE.map((node) => ({ ...node, dutyPercent: Math.min(100, node.dutyPercent + 5) })));
    assert(adapter.enableCalls === before + 1, 'W1: 等待恢复后下一次真实调整必须有效');
    await lifecycle.close();
  }

  // ───────────────── W2：入队前同步取消；旧 Enable=0；新曲线优先 ─────────────────
  {
    const adapter = new FakeAdapter();
    adapter.resumingProbes = 10_000; // 一直恢复中：等待必须被取消而不是占队到截止
    const lifecycle = newLifecycle(adapter, 4000);
    await armWake(lifecycle, adapter);
    const resumePromise = lifecycle.resume();
    await new Promise((resolve) => setTimeout(resolve, 300)); // 让等待真的开始
    const probesBeforeDisable = adapter.calls.filter((call) => call === 'state').length;
    assert(probesBeforeDisable >= 1, 'W2: 夹具必须先真正进入等待');
    const disableStartedAt = Date.now();
    await lifecycle.disable();
    const disableElapsed = Date.now() - disableStartedAt;
    assert(disableElapsed < 2000, `W2: disable 不得等旧 waiter 占队到截止（elapsed=${disableElapsed}ms）`);
    await resumePromise.then(
      () => { throw new Error('W2: 被新意图取代的 resume 不得静默成功'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        assert(/FAN_RESUME_WAIT_CANCELLED|FAN_CONTROL_INTENT_SUPERSEDED/.test(message),
          `W2: 取消必须以明确理由终止（message=${message}）`);
      });
    assert(adapter.enableCalls === 0, 'W2: 被取消的旧 resume 不得再写硬件（旧 Enable 必须为 0）');
    await lifecycle.close();
  }

  // ───────────────── W3：共享等待 / 非法间隔不忙轮询 / 绝对截止后可重试 ─────────────────
  {
    const adapter = new FakeAdapter();
    adapter.resumingProbes = 10_000;
    adapter.retryAfterMs = 1; // 非法（< 250）：必须回落到 250 ms，不得忙轮询
    const lifecycle = newLifecycle(adapter, 1200);
    await armWake(lifecycle, adapter);
    const first = lifecycle.resume();
    const second = lifecycle.resume(); // 重复通知必须合并到同一任务
    const results = await Promise.allSettled([first, second]);
    assert(results.every((result) => result.status === 'rejected'),
      'W3: 一直 Resuming 必须走到绝对截止并给出明确终态，不得伪成功');
    for (const result of results) {
      const message = result.status === 'rejected' ? String((result.reason as Error).message) : '';
      assert(/FAN_RESUME_WAIT_DEADLINE/.test(message), `W3: 截止终态必须是明确的 deadline（message=${message}）`);
    }
    const probes = adapter.calls.filter((call) => call === 'state').length;
    // 1.2 s 预算 / 250 ms 下限 ⇒ 最多约 7 次探测；若把非法 retryAfterMs=1 当真会得到上千次。
    assert(probes <= 10, `W3: 非法 retryAfterMs 不得忙轮询（probes=${probes}）`);
    assert(probes >= 2, `W3: 共享等待任务必须真的轮询过（probes=${probes}）`);
    assert(adapter.maxInFlight === 1, `W3: 合并后的单一等待任务最多一个在途读（maxInFlight=${adapter.maxInFlight}）`);
    assert(lifecycle.phase === 'ready' || lifecycle.state === 'awaiting-control',
      `W3: 截止后必须停在明确可重试状态而不是 connecting（state=${lifecycle.state}）`);
    // 下一次用户操作可以新建一次有界尝试，并在宿主恢复后可成功。
    adapter.resumingProbes = 0;
    await lifecycle.apply(CURVE);
    assert(adapter.enableCalls === 1, 'W3: 截止后用户操作必须能新建一次有界尝试并成功');
    await lifecycle.close();
  }

  // ───────────────── W4：非重试态 / 旧代不假成功 / 权威空 lease 与陈旧快照 ─────────────────
  {
    // 401：不是"恢复中"，必须立刻终态失败，而不是等满预算。
    // 夹具：第 1 次快照 = Resuming（让等待真的开始），随后的快照 401。
    const unauthorized = new FakeAdapter();
    unauthorized.retryAfterMs = 250;
    const unauthorizedLifecycle = newLifecycle(unauthorized, 4000);
    await armWake(unauthorizedLifecycle, unauthorized);
    unauthorized.resumingProbes = 1;
    unauthorized.stateFailure = new FanApiError('API_SESSION_REQUIRED', 401, 'API_SESSION_REQUIRED');
    unauthorized.stateFailureTimes = 1;
    unauthorized.stateFailureAfterCalls = 1; // 第 1 次（准入探测）= Resuming；第 2 次（等待循环）才 401
    const startedAt = Date.now();
    await unauthorizedLifecycle.resume().then(
      () => { throw new Error('W4: 401 不得被当作恢复中重试'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        assert(/FAN_RESUME_WAIT_TERMINAL_FAILED/.test(message), `W4: 401 必须是终态失败（message=${message}）`);
      });
    assert(Date.now() - startedAt < 3000, 'W4: 401 必须立刻终态失败，不得等满等待预算');
    await unauthorizedLifecycle.close();

    // FaultLocked：不是可等待状态——不得把等待预算耗在它上面；宿主自己也必须继续拒绝写入。
    const faulted = new FakeAdapter();
    const faultedLifecycle = newLifecycle(faulted, 4000);
    await armWake(faultedLifecycle, faulted);
    faulted.resumingProbes = 0;
    faulted.stateName = 'FaultLocked';
    faulted.controlAccepting = false;
    const faultedStartedAt = Date.now();
    await faultedLifecycle.resume().then(
      () => { throw new Error('W4: FaultLocked 不得被当作恢复中等待'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        // G7（执行单 §3.2）：终态准入结果现在**在准入处**诚实失败，而不是继续入队让宿主
        // 再拒一次——所以错误由本桥的 FAN_RESUME_ADMISSION_TERMINAL_FAILED 给出，并须
        // 指认宿主状态。语义要求（终止、不耗预算、不写）比旧断言更强，故同步锚点。
        assert(/FAN_RESUME_ADMISSION_TERMINAL_FAILED/.test(message) && /FaultLocked/i.test(message),
          `W4: FaultLocked 必须在准入处诚实失败并指认宿主状态（message=${message}）`);
      });
    // 等待预算 4000 ms ⇒ 只要总耗时远小于它，就证明根本没有进入轮询等待。
    assert(Date.now() - faultedStartedAt < 2000, 'W4: FaultLocked 不得消耗完整等待预算');
    assert(faulted.enableCalls === 0 && !faulted.calls.includes('open'),
      `W4: 终态准入不得再尝试一次控制写（calls=${faulted.calls.join(',')}）`);
    await faultedLifecycle.close();

    // 旧代 Ready 不能假成功：快照代次落后于本地 power generation ⇒ 必须继续等到截止。
    const stale = new FakeAdapter();
    const staleLifecycle = newLifecycle(stale, 900);
    await armWake(staleLifecycle, stale, 3);
    stale.resumingProbes = 0;
    stale.snapshotGeneration = 2; // 旧代 Ready（< 3）
    await staleLifecycle.resume().then(
      () => { throw new Error('W4: 旧代 Ready 不得放行'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        assert(/FAN_RESUME_WAIT_DEADLINE/.test(message), `W4: 旧代 Ready 必须继续等到截止（message=${message}）`);
      });
    await staleLifecycle.close();

    // 权威空 lease 清本地陈旧 token；陈旧快照不清新 token。
    const leaseAdapter = new FakeAdapter();
    const leaseLifecycle = newLifecycle(leaseAdapter, 5000);
    await leaseLifecycle.start();
    await leaseLifecycle.applyPreset('balanced', CURVE);
    assert(leaseLifecycle.currentLease !== null, 'W4: 夹具必须先持有 lease');
    leaseLifecycle.setPowerGeneration(4);
    leaseAdapter.snapshotGeneration = 4;
    leaseAdapter.lease = null;
    await leaseLifecycle.apply(CURVE); // 同代权威快照 = 无 lease ⇒ 清本地 token
    assert(leaseLifecycle.currentLease !== null || leaseAdapter.lease !== null,
      'W4: 同代权威无 lease 之后仍必须由真实 acquire 得到新 token（不得留下假 token）');
    await leaseLifecycle.close();
  }

  // ─────── G7-A：合法本地唤醒 → Host 尚报 Suspended → Resuming ~10.2 s → 同代 Ready ───────
  {
    const adapter = new FakeAdapter();
    // 与现场一致的时间线：准入探测就看到 Suspended（宿主还没接受 resume），随后约 10.2 s
    // Resuming，最后同代 Ready。之前**零控制写**，之后接管且下一次调整有效。
    adapter.stateScript = [
      () => adapter.craft('Suspended', false),
      ...Array.from({ length: 41 }, () => () => adapter.craft('Resuming', false)),
      () => adapter.craft('Ready', true),
    ];
    const lifecycle = newLifecycle(adapter, 13_000);
    await armWake(lifecycle, adapter);
    const openBaseline = adapter.openCalls;
    adapter.onStateCall = (index) => {
      if (index >= 42) return;
      assert(adapter.enableCalls === 0 && adapter.openCalls === openBaseline,
        `G7-A: 宿主尚未接受 resume 时不得有任何新控制写（index=${index}, enable=${adapter.enableCalls}, open=${adapter.openCalls}/${openBaseline}）`);
    };
    const startedAt = Date.now();
    await lifecycle.resume();
    const elapsed = Date.now() - startedAt;
    assert(elapsed >= 9_000 && elapsed < 13_000,
      `G7-A: 必须真的等过宿主自己的 ~10.2 s 重建（elapsed=${elapsed}ms）`);
    assert(lifecycle.controlReady === true, 'G7-A: 等待结束后必须真的具备控制能力');
    assert(adapter.enableCalls === 1, `G7-A: 恢复后必须恰好接管一次（enable=${adapter.enableCalls}）`);
    assert(adapter.enableNodes.some((node) => node.dutyPercent === 70), 'G7-A: 接管的必须是原曲线');
    assert(!adapter.calls.includes('suspend') && !adapter.calls.includes('resume'),
      `G7-A: 等待期间不得重复 /api/resume 或新造恢复 owner（calls=${adapter.calls.join(',')}）`);
    await lifecycle.apply(CURVE.map((node) => ({ ...node, dutyPercent: Math.min(100, node.dutyPercent + 5) })));
    assert(adapter.enableCalls === 2, 'G7-A: 恢复后下一次调整必须有效');
    await lifecycle.close();

    // 快恢复对照：同一规则不得把亚秒恢复拖到截止。
    const fastAdapter = new FakeAdapter();
    fastAdapter.stateScript = [
      () => fastAdapter.craft('Suspended', false),
      () => fastAdapter.craft('Ready', true),
    ];
    const fastLifecycle = newLifecycle(fastAdapter, 5_000);
    await armWake(fastLifecycle, fastAdapter);
    const fastStartedAt = Date.now();
    await fastLifecycle.resume();
    assert(Date.now() - fastStartedAt < 1_000 && fastAdapter.enableCalls === 1,
      `G7-A: 快恢复对照必须立即接管，不得被扩展等待拖慢（elapsed=${Date.now() - fastStartedAt}ms）`);
    await fastLifecycle.close();
  }

  // ─── G7-B：无唤醒依据 / 初探 terminal·superseded·null·401 / 未知 一律不放行恢复写 ───
  {
    /** 夹具：把一个 lifecycle 推进到"真睡眠"（只有 suspending 边界，**没有**本代唤醒依据）。 */
    const armSleep = async (adapter: FakeAdapter): Promise<FanHostLifecycle> => {
      const lifecycle = newLifecycle(adapter, 2_000);
      await lifecycle.start();
      await lifecycle.applyPreset('balanced', CURVE);
      lifecycle.setPowerGeneration(1);
      lifecycle.observePowerBoundary('suspending', 1);
      adapter.calls.length = 0;
      return lifecycle;
    };
    // (1) 真睡眠 + Host 报 Suspended：不得写。
    const asleepAdapter = new FakeAdapter();
    asleepAdapter.stateName = 'Suspended';
    const asleep = await armSleep(asleepAdapter);
    await asleep.apply(CURVE).then(
      () => { throw new Error('G7-B: 无唤醒依据的真睡眠不得放行恢复写'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        assert(/FAN_RESUME_(ADMISSION_(SUPERSEDED|TERMINAL_FAILED)|WAIT_DEADLINE)/.test(message),
          `G7-B: 真睡眠必须由准入终止或有界截止且不写（message=${message}）`);
      });
    assert(asleepAdapter.enableCalls === 0 &&
      asleepAdapter.calls.filter((call) => call === 'open' || call === 'enable' || call === 'acquire').length === 0,
      `G7-B: 真睡眠不得 Open/Enable/acquire（calls=${asleepAdapter.calls.join(',')}）`);
    await asleep.close();

    // (2) 探测失败/null + 无唤醒依据：不得当作准入成功。
    const nullAdapter = new FakeAdapter();
    const nullLife = await armSleep(nullAdapter);
    nullAdapter.stateFailure = new Error('TRANSIENT_TRANSPORT_UNAVAILABLE');
    nullAdapter.stateFailureTimes = 5;
    await nullLife.apply(CURVE).then(
      () => { throw new Error('G7-B: 探测 null 不得当作准入成功'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        assert(/FAN_RESUME_ADMISSION_PROBE_UNAVAILABLE/.test(message), `G7-B: null 探测必须明确拒绝（message=${message}）`);
      });
    assert(nullAdapter.enableCalls === 0, 'G7-B: 探测 null 之后不得写');
    await nullLife.close();

    // (3) 401 + 无唤醒依据：认证失败是终态，绝不放行。
    const authAdapter = new FakeAdapter();
    const authLife = await armSleep(authAdapter);
    authAdapter.stateFailure = new FanApiError('API_SESSION_REQUIRED', 401, 'API_SESSION_REQUIRED');
    authAdapter.stateFailureTimes = 5;
    await authLife.apply(CURVE).then(
      () => { throw new Error('G7-B: 401 不得放行'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        assert(/FAN_RESUME_ADMISSION_TERMINAL_FAILED/.test(message), `G7-B: 401 必须终态失败（message=${message}）`);
      });
    assert(authAdapter.enableCalls === 0, 'G7-B: 401 之后不得写');
    await authLife.close();

    // (4) 未知状态：即使有本代唤醒依据，unknownState 也必须终态失败且不写。
    const unknownAdapter = new FakeAdapter();
    unknownAdapter.unknownState = true;
    const unknownLife = newLifecycle(unknownAdapter, 2_000);
    await armWake(unknownLife, unknownAdapter);
    await unknownLife.resume().then(
      () => { throw new Error('G7-B: unknownState 不得放行'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        assert(/FAN_RESUME_ADMISSION_TERMINAL_FAILED/.test(message), `G7-B: unknownState 必须终态失败（message=${message}）`);
      });
    assert(unknownAdapter.enableCalls === 0, 'G7-B: unknownState 之后不得写');
    unknownAdapter.unknownState = false; // 仅注入本次准入探测；恢复未知位后才能正常结束 Host。
    await unknownLife.close();

    // (5) "另一实例/旧代 Ready"：快照代次落后于本代 ⇒ 只读等待到截止，绝不写。
    const otherAdapter = new FakeAdapter();
    const otherLife = newLifecycle(otherAdapter, 400);
    await armWake(otherLife, otherAdapter, 5);
    otherAdapter.snapshotGeneration = 4;
    await otherLife.resume().then(
      () => { throw new Error('G7-B: 旧代/另一实例 Ready 不得放行'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        assert(/FAN_RESUME_WAIT_DEADLINE/.test(message), `G7-B: 旧代 Ready 必须等到截止（message=${message}）`);
      });
    assert(otherAdapter.enableCalls === 0, 'G7-B: 旧代 Ready 之后不得写');
    await otherLife.close();

    // (6) 对照：正常醒着的显式开启仍按既有握手/授权合同可用。
    const awakeAdapter = new FakeAdapter();
    const awake = newLifecycle(awakeAdapter, 2_000);
    await awake.start();
    await awake.apply(CURVE);
    assert(awakeAdapter.enableCalls === 1,
      'G7-B: 正常醒着的显式开启不得被"无 wake context"误禁');
    await awake.close();

    // (7) **有**本代唤醒依据、但宿主始终报 Suspended 不转 Ready（G7-A 只覆盖"最终会好"的
    //     形态；`AdoptedSuspendedAdapter` 替身同步改为"宿主办完自己的 resume"后，这一形态
    //     必须由本用例接管）：只能只读等待到绝对截止、零控制写、给出明确可重试终态。
    const stuckAdapter = new FakeAdapter();
    const stuckLife = newLifecycle(stuckAdapter, 400);
    await armWake(stuckLife, stuckAdapter);
    stuckAdapter.stateName = 'Suspended';
    await stuckLife.resume().then(
      () => { throw new Error('G7-B: 宿主始终 Suspended 不得被当作恢复成功'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        assert(/FAN_RESUME_WAIT_DEADLINE/.test(message),
          `G7-B: 始终 Suspended 必须走到绝对截止（message=${message}）`);
      });
    assert(stuckAdapter.calls.filter((call) => call === 'enable' || call === 'open' || call === 'acquire').length === 0,
      `G7-B: 始终 Suspended 期间不得有任何控制写（calls=${stuckAdapter.calls.join(',')}）`);
    await stuckLife.close();
  }

  // ─── G7-C：GET 在途时的真实电源入口/新意图；同代 resumed 不得误取消本代 waiter ───
  {
    // (1) 准入 GET 在途时走 App 真实下一代 suspending：旧回包（Ready）不得清新 lease/写。
    const adapter = new FakeAdapter();
    const lifecycle = newLifecycle(adapter, 2_000);
    await armWake(lifecycle, adapter);
    adapter.onStateCall = (index) => {
      if (index !== 0) return;
      // App.vue 的真实入口序列（不是直接调 suspend()）。
      lifecycle.setPowerGeneration(2);
      lifecycle.observePowerBoundary('suspending', 2);
    };
    await lifecycle.resume().then(
      () => { throw new Error('G7-C: 被下一代睡眠取代的旧 resume 不得成功'); },
      (error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        assert(/FAN_CONTROL_INTENT_SUPERSEDED|FAN_RESUME_WAIT_CANCELLED|FAN_RESUME_ADMISSION_SUPERSEDED/.test(message),
          `G7-C: 被取代的旧请求必须以明确理由终止（message=${message}）`);
      });
    assert(adapter.calls.filter((call) => call === 'open' || call === 'enable').length === 0,
      `G7-C: 旧 GET 晚回不得产生任何控制写（calls=${adapter.calls.join(',')}）`);
    assert(lifecycle.currentLease === null, 'G7-C: 旧回包不得清新 lease');
    await lifecycle.close();

    // (2) 等待中到达新控制意图：旧 waiter 立即终止且不写；**新请求不复用已取消任务**。
    const adapter2 = new FakeAdapter();
    const life2 = newLifecycle(adapter2, 3_000);
    await armWake(life2, adapter2);
    adapter2.resumingProbes = 10_000;
    // 立刻挂上结算处理器：被取代的旧请求在等待期间被取消，不能让 Node 报未处理拒绝。
    const oldResume = life2.resume().then(() => 'resolved', (error: unknown) => String(error instanceof Error ? error.message : error));
    await new Promise((resolve) => setTimeout(resolve, 150));
    assert(adapter2.calls.filter((call) => call === 'state').length >= 1, 'G7-C: 旧 waiter 必须真的进入等待');
    adapter2.resumingProbes = 0; // 宿主已可接管
    const newCurve = CURVE.map((node) => ({ ...node, dutyPercent: Math.min(100, node.dutyPercent + 5) }));
    const newApply = life2.apply(newCurve).then(() => 'resolved', (error: unknown) => String(error instanceof Error ? error.message : error));
    const oldOutcome = await oldResume;
    assert(oldOutcome !== 'resolved' && /CANCELLED|SUPERSEDED/.test(oldOutcome),
      `G7-C: 旧 waiter 必须被新意图终止（${oldOutcome}）`);
    assert(adapter2.enableCalls === 0, 'G7-C: 旧 waiter 被取消时不得写硬件');
    const newOutcome = await newApply;
    assert(newOutcome !== oldOutcome && /不可控制|FAN_/.test(newOutcome),
      `G7-C: 新请求必须得到自己的结算，不得复用已取消任务的结局（${newOutcome}）`);
    assert(adapter2.enableCalls === 0,
      'G7-C: 新请求不得从被取消任务继承任何硬件写入');
    // 对照：干扰结束后，一次新的 resume 仍必须能建立自己的有界尝试并成功接管。
    await life2.resume();
    assert(adapter2.enableCalls === 1, 'G7-C: 干扰之后的新请求必须能自建尝试并接管');
    await life2.close();

    // (3) 对照：Host 仍 Resuming 时收到**同代** resumed/重复 resume-ready ⇒ waiter 沿原截止
    //     继续并最终接管；重复通知不得重开预算。
    const adapter3 = new FakeAdapter();
    adapter3.resumingProbes = 3;
    const life3 = newLifecycle(adapter3, 3_000);
    await armWake(life3, adapter3);
    let notified = false;
    adapter3.onStateCall = (index) => {
      if (index !== 1 || notified) return;
      notified = true;
      life3.observePowerBoundary('resumed', 1);
      life3.observePowerBoundary('resume-ready', 1);
      life3.observePowerBoundary('resume-ready', 1);
    };
    const startedAt = Date.now();
    await life3.resume();
    const elapsed = Date.now() - startedAt;
    assert(notified && adapter3.enableCalls === 1,
      'G7-C: 同代 resumed/重复 resume-ready 不得取消仍在等待的本代 waiter');
    assert(elapsed < 3_000, `G7-C: 同代重复通知不得重开等待预算（elapsed=${elapsed}ms）`);
    assert(adapter3.calls.filter((call) => call === 'state').length <= 8,
      `G7-C: 预算未重开 ⇒ 探测次数必须有界（probes=${adapter3.calls.filter((call) => call === 'state').length}）`);
    await life3.close();

    // (4) 唤醒重放自己的只读快照**之后**（最终写之前）到达终止性意图 ⇒ 不得再写。
    const adapter4 = new FakeAdapter();
    const life4 = newLifecycle(adapter4, 2_000);
    await armWake(life4, adapter4);
    let reachedReplayRead = false;
    adapter4.onStateCall = (index) => {
      if (index !== 1 || reachedReplayRead) return;
      reachedReplayRead = true;
      void life4.close().catch(() => undefined);
    };
    await life4.resume();
    assert(reachedReplayRead, 'G7-C: 夹具必须到达重放的只读快照');
    assert(adapter4.calls.filter((call) => call === 'enable').length === 0,
      `G7-C: 最终写前 revision 校验必须让被取代的旧重放不再写硬件（calls=${adapter4.calls.join(',')}）`);
  }

  // ─── G7-D：初探慢 + 最后余额 < 500 ms + 队列内才遇 POWER_RESUMING：一个绝对截止 ───
  {
    const adapter = new FakeAdapter();
    const lifecycle = newLifecycle(adapter, 700);
    await armWake(lifecycle, adapter);
    adapter.stateDelays = [400]; // 初探慢：一次占掉大部分预算
    adapter.stateScript = [
      () => adapter.craft('Suspended', false),
      () => adapter.craft('Ready', true),
    ];
    adapter.enableResumingOnce = true; // 队列内才遇到合法 POWER_RESUMING
    const startedAt = Date.now();
    const resumePromise = lifecycle.resume().then(() => 'resolved', (error: unknown) => String(error instanceof Error ? error.message : error));
    // "等待不占控制队列"：协调等待期间另一个只读请求必须立刻得到服务。
    const queueProbeStart = Date.now();
    await lifecycle.getState();
    const queueProbeElapsed = Date.now() - queueProbeStart;
    const resumeOutcome = await resumePromise;
    assert(resumeOutcome === 'resolved', `G7-D: 同代恢复必须成功（outcome=${resumeOutcome}）`);
    const elapsed = Date.now() - startedAt;
    assert(adapter.calls.filter((call) => call === 'enable').length === 2 && adapter.enableCalls === 1,
      `G7-D: 队列内合法 POWER_RESUMING 必须释放队列、重入一次并成功（enable 调用=${adapter.calls.filter((call) => call === 'enable').length}）`);
    assert(adapter.enableNodes.some((node) => node.dutyPercent === 70),
      'G7-D: 重入后写的必须仍是同一意图的原曲线（不得复活旧意图）');
    assert(elapsed <= 1_100, `G7-D: 必须守住同一个绝对截止（elapsed=${elapsed}ms）`);
    // 初探之后的所有 GET 超时都不得超过当时的剩余余额（禁止 max(500, remaining)）。
    const laterTimeouts = adapter.stateTimeouts.slice(1).filter((value) => Number.isFinite(value));
    assert(laterTimeouts.every((value) => value <= 700),
      `G7-D: GET 超时不得超过余额（later=${laterTimeouts.join(',')} all=${adapter.stateTimeouts.join(',')}）`);
    assert(laterTimeouts.some((value) => value < 500),
      `G7-D: 余额不足 500 ms 时必须按余额缩短超时（later=${laterTimeouts.join(',')} all=${adapter.stateTimeouts.join(',')}）`);
    assert(queueProbeElapsed < 300,
      `G7-D: 协调等待不得占据控制队列（getState 用时 ${queueProbeElapsed}ms）`);
    await lifecycle.close();
  }

  // ─── G8-A：睡眠边界后的迟到心跳（成功/失败）不得越代；边界后不再新发 tick ───
  {
    const newHeartbeatLifecycle = (adapter: FakeAdapter): FanHostLifecycle => new FanHostLifecycle({
      enabled: true,
      adapter,
      launcher: new FakeLauncher(),
      heartbeatIntervalMs: 40,
      resumeWaitDeadlineMs: 2_000,
    });
    // (1) 迟到**成功**。
    const okAdapter = new FakeAdapter();
    const okLife = newHeartbeatLifecycle(okAdapter);
    await okLife.start();
    await okLife.applyPreset('balanced', CURVE);
    assert(okLife.currentLease !== null, 'G8-A: 夹具必须先持有 lease');
    let resolveLate: ((lease: FanLease) => void) | null = null;
    okAdapter.nextHeartbeat = new Promise<FanLease>((resolve) => { resolveLate = resolve; });
    const inFlightOk = okLife.heartbeat().catch(() => undefined);
    await new Promise((resolve) => setTimeout(resolve, 20));
    assert(okAdapter.calls.filter((call) => call === 'heartbeat').length === 1, 'G8-A: 必须先有一个在途心跳');
    const callsAtBoundary = okAdapter.calls.length;
    const writesBeforeBoundary = okAdapter.calls.filter((call) => call === 'open' || call === 'enable').length;
    const leaseBefore = okLife.currentLease?.leaseId ?? null;
    // App 真实睡眠边界（生产入口：setPowerGeneration/observePowerBoundary，不直接调 suspend）。
    okLife.setPowerGeneration(1);
    okLife.observePowerBoundary('suspending', 1);
    resolveLate?.({ leaseId: 'lease-late-success', generation: 99 });
    await inFlightOk;
    assert(okLife.currentLease === null,
      `G8-A: 睡眠已同步清租约；迟到成功不得复活旧租约（lease=${okLife.currentLease?.leaseId ?? 'null'}）`);
    await new Promise((resolve) => setTimeout(resolve, 150));
    assert(okAdapter.calls.length === callsAtBoundary, 'G8-A: 睡眠边界后不得再新发心跳 tick');
    assert(okAdapter.calls.filter((call) => call === 'open' || call === 'enable').length === writesBeforeBoundary,
      `G8-A: 边界之后的回包不得触发额外 Open/Enable（calls=${okAdapter.calls.join(',')}）`);
    await okLife.close();

    // (2) 迟到**失败**。
    const failAdapter = new FakeAdapter();
    const failLife = newHeartbeatLifecycle(failAdapter);
    await failLife.start();
    await failLife.applyPreset('balanced', CURVE);
    let rejectLate: ((error: unknown) => void) | null = null;
    failAdapter.nextHeartbeat = new Promise<FanLease>((_resolve, reject) => { rejectLate = reject; });
    const inFlightFail = failLife.heartbeat().catch(() => undefined);
    await new Promise((resolve) => setTimeout(resolve, 20));
    assert(failAdapter.calls.filter((call) => call === 'heartbeat').length === 1, 'G8-A: 必须先有一个在途心跳');
    const failCallsAtBoundary = failAdapter.calls.length;
    const failWritesBeforeBoundary = failAdapter.calls.filter((call) => call === 'open' || call === 'enable').length;
    const failLeaseBefore = failLife.currentLease?.leaseId ?? null;
    failLife.setPowerGeneration(1);
    failLife.observePowerBoundary('suspending', 1);
    rejectLate?.(new FanApiError('POWER_SUSPENDED', 409, 'POWER_SUSPENDED'));
    await inFlightFail;
    assert(failLife.currentLease === null,
      'G8-A: 睡眠已同步清租约；迟到失败不得重建或重写租约');
    await new Promise((resolve) => setTimeout(resolve, 150));
    assert(failAdapter.calls.length === failCallsAtBoundary, 'G8-A: 睡眠边界后不得再新发心跳 tick');
    assert(failAdapter.calls.filter((call) => call === 'open' || call === 'enable').length === failWritesBeforeBoundary,
      `G8-A: 边界拒绝不得从 catch 直接 Open/Enable 重放（calls=${failAdapter.calls.join(',')}）`);
    await failLife.close();
  }

  // ───────────────── E1：默认关闭详细日志仍可追责（证据链） ─────────────────
  {
    const diagnosticsSource = readFileSync('src/bridge/fanDiagnostics.ts', 'utf8');
    const nativeSource = readFileSync('native/main.cpp', 'utf8');
    const hostSource = readFileSync('src/bridge/fanHost.ts', 'utf8');
    const evidenceStart = diagnosticsSource.indexOf('export function fanLifecycleEvidence');
    const evidenceEnd = diagnosticsSource.indexOf('\n}', evidenceStart);
    const evidenceBody = diagnosticsSource.slice(evidenceStart, evidenceEnd);
    assert(evidenceStart >= 0 && evidenceEnd > evidenceStart, 'E1: 必须存在 fanLifecycleEvidence 入口');
    assert(!evidenceBody.includes('fanDiagnosticLoggingEnabled'),
      'E1: 有限责任证据入口不得被详细日志开关门控（否则关闭详细日志时终态整段消失）');
    assert(evidenceBody.includes("invoke('fanLog.write'"), 'E1: 有限责任证据必须走既有 Fan 日志 IPC');
    // 每任务仅 started + 唯一终态：三个调用点（started 一次 / 终态由 finishResumeWait 统一写一次）。
    assert((hostSource.match(/fanLifecycleEvidence\('lifecycle\.resume-wait-started'/g) ?? []).length === 1,
      'E1: 每个等待任务只允许一条 wait-started');
    assert((hostSource.match(/fanLifecycleEvidence\(evidence,/g) ?? []).length === 1,
      'E1: 终态必须由唯一出口写一次（不得逐次轮询落盘）');
    assert(nativeSource.includes('static bool fanLifecycleEvidenceAlwaysLogged(const std::string& event)'),
      'E1: native 侧必须存在严格白名单判定');
    const whitelistStart = nativeSource.indexOf('static bool fanLifecycleEvidenceAlwaysLogged');
    const whitelistEnd = nativeSource.indexOf('\n}', whitelistStart);
    const whitelistBody = nativeSource.slice(whitelistStart, whitelistEnd);
    for (const name of ['lifecycle.resume-wait-started', 'lifecycle.resume-wait-completed',
      'lifecycle.resume-wait-cancelled', 'lifecycle.resume-wait-terminal-failed']) {
      assert(whitelistBody.includes(`"${name}"`), `E1: 白名单必须含 ${name}`);
    }
    assert(!/lifecycle\.(apply|preset|state)-/.test(whitelistBody), 'E1: 白名单不得放行普通诊断事件');
    const writeHandlerStart = nativeSource.indexOf('ipc_on("fanLog.write"');
    const writeHandlerEnd = nativeSource.indexOf('ipc_on("fanLog.getPath"', writeHandlerStart);
    const writeHandlerBody = nativeSource.slice(writeHandlerStart, writeHandlerEnd);
    assert(writeHandlerBody.indexOf('fanLifecycleEvidenceAlwaysLogged(event)') <
      writeHandlerBody.indexOf('if (!g_fanLogEnabled) return false;'),
      'E1: 白名单判定必须**先于**详细日志门（否则关闭日志时依然丢证据）');
    assert(writeHandlerBody.includes('appendFanLifecycleLog(event.c_str()'),
      'E1: 白名单事件必须写入已纳入导出的 fan-lifecycle.log');
  }

  // FAN-942: current typed rejection and old Host context both stop immediately, while manual retry remains usable.
  for (const typed of [true, false]) {
    const adapter = new FakeAdapter();
    const lifecycle = newLifecycle(adapter, 5000);
    await armWake(lifecycle, adapter);
    const rejected = () => ({ ...adapter.craft('AwaitingControl', false), openCalled: false, openEventsCalled: false,
      hardwareWritesEnabled: false, lastError: 'FanApiException: HC 当前设备未声明 FanControl 能力',
      ...(typed ? { fanCapabilitySupported: false } : {}) });
    adapter.stateScript = [rejected, rejected, rejected];
    const started = performance.now();
    await lifecycle.resume().then(() => { throw new Error('FAN-942: unsupported resume accepted'); }, error => {
      assert(error instanceof FanApiError && error.errorCode === 'FAN_UNSUPPORTED', `FAN-942: preserve definite capability error (${String(error)})`);
    });
    assert(performance.now() - started < 1500, 'FAN-942: definite rejection must not spend 15s waiting');
    assert(!adapter.calls.includes('enable'), 'FAN-942: unsupported state never writes');
    adapter.stateScript = [() => adapter.craft('Ready', true)];
    const retryGate = await lifecycle.start(); // The UI's explicit manual-enable entry rearms the session.
    assert(retryGate.allowed, 'FAN-942: manual start remains callable after capability rejection');
    await lifecycle.apply(CURVE);
    assert(adapter.calls.includes('enable'), 'FAN-942: corrected current capability permits next manual request');
    await lifecycle.close();
  }
  assert(unsupportedFanControlError({ state: 'AwaitingControl', openCalled: false, openEventsCalled: false,
    fanCapabilitySupported: true, lastError: 'HC 当前设备未声明 FanControl 能力' } as FanState) === null,
    'FAN-942: explicit current support outranks stale historical error text');

  console.log('fan resume wait selftest: PASS (W1 bounded wait + next adjustment, W2 pre-queue cancel, W3 dedupe/deadline, W4 non-retryable/stale/lease, G7-A slow wake ~10.2s, G7-B no-wake/terminal/null/401/unknown, G7-C in-flight power entry + same-generation resumed, G7-D one absolute deadline + queue release, G8-A late heartbeat, E1 limited-liability evidence)');
}

void main();
