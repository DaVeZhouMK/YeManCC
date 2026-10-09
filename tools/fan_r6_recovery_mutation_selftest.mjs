/**
 * FAN-938 R6 §P5/D1,D2,D5 承重规则的**实际隔离变异**反控。
 *
 * 依据：`Docs\Tasks\Fan\FAN-920\FAN-938-R6-REVIEW-AND-T0-REWORK-EXECUTION-ADJUDICATION-20261005.md`
 *   §P5 L125：最少做两个实际隔离变异——
 *     (a) 恢复被提前 settle／固定外层截止恢复旧行为，应由 **D1/D2** 捕获；
 *     (b) 把 inputReady 单独加回唤醒放行，应由 **D5** 捕获。
 *   只对变化的承重规则做一次，不启动全库变异。
 *
 * 做法：对 `src/bridge/fanHost.ts` 施加**最小文本变异**生成临时模块
 * `src/bridge/fanHost.__r6mutant.ts`，再用同一组 R6 定向行为断言
 * （`src/bridge/__r6_mutant_scenario.ts`）跑一遍：
 *   - 原始（未变异）⇒ 四项全绿（正控）；
 *   - 变异 A（先 settle 再写）⇒ D1/R6-01 必须变红；
 *   - 变异 B（把 inputReady 加回 wake fact 并去掉真实 suspend 门）⇒ D5/R6-03 必须变红；
 *   - 变异 C（恢复旧「固定外层截止」，超过 2 轮即放弃）⇒ D2 必须变红。
 *
 * 只读产品源码；只向 Build/Validation/Mutations 与 src/bridge 下的 __r6mutant 临时文件写入，
 * 结束后清理。不启动真实 Host、不触 EC、不写硬件、不睡眠工作机。
 */
import { execSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const ROOT = process.cwd();
const HOST_SOURCE = join(ROOT, 'src/bridge/fanHost.ts');
const MUTANT = join(ROOT, 'src/bridge/fanHost.__r6mutant.ts');
const SCENARIO = join(ROOT, 'src/bridge/__r6_mutant_scenario.ts');
const OUT_DIR = join(ROOT, '..', '..', 'Build', 'Validation', 'Mutations');
const OUT_FILE = join(OUT_DIR, '__r6_mutant_scenario.mjs');
const ORIGINAL = readFileSync(HOST_SOURCE, 'utf8').replace(/\r\n/g, '\n');

/**
 * R6 定向行为断言集合。走生产 `FanHostLifecycle` 的公共入口（start/apply/resume/
 * observePowerBoundary/stageRecoveryCurve），只替换系统/设备边界。
 * 每项对应一条「必须仍然成立」的 R6 承重规则。
 */
const SCENARIO_SOURCE = `import { FanHostLifecycle, type FanHostLauncher, type FanHostProcess } from './fanHost.__r6mutant';
import { FanApiError, type FanApiAdapter, type FanHandshake, type FanLease, type FanNode, type FanState } from './fanApi';
import type { PowerLifecycleState } from './api';

const CURVE: FanNode[] = [
  { tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 },
  { tempC: 70, dutyPercent: 70 }, { tempC: 100, dutyPercent: 100 },
];
const assert = (condition: unknown, message: string): void => { if (!condition) throw new Error(message); };
const pause = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

const nativeState = (generation: number, phase: PowerLifecycleState['phase'], inputReady: boolean, resumeReady: boolean = phase === 'ready'): PowerLifecycleState => ({
  generation,
  phase,
  hardwareWritesAllowed: phase === 'ready',
  inputReady,
  resumeReady,
  hibernateAvailable: true,
});

class L implements FanHostLauncher {
  async start(): Promise<FanHostProcess> { return { pid: 18328, executable: 'fake.exe' }; }
  async stop(): Promise<void> { /* noop */ }
}

class FakeAdapter implements FanApiAdapter {
  readonly enabled = true;
  calls: string[] = [];
  lease: FanLease | null = null;
  private leaseSequence = 0;
  openCalls = 0;
  remoteStateName = 'Ready';
  remotePowerState = 'On';
  remoteOpenCalled = true;
  oemRestoreConfirmed = false;
  controlAccepting = true;
  retryAfterMs: number | undefined = 250;

  markSleepClosedSuspended(): void {
    this.remoteStateName = 'Suspended';
    this.remotePowerState = 'Suspended';
    this.remoteOpenCalled = false;
    this.oemRestoreConfirmed = true;
    this.controlAccepting = false;
    this.lease = null;
  }

  private snapshot(name = this.remoteStateName, accepting = this.controlAccepting): FanState {
    const result: FanState = {
      state: name, powerState: this.remotePowerState, protocolVersion: '2',
      hardwareWrites: false, hardwareWritesObserved: false, hardwareWritesEnabled: true,
      unknownState: false, hcCloseCleanupPending: false,
      openCalled: this.remoteOpenCalled, openEventsCalled: this.remoteOpenCalled,
      oemRestoreConfirmed: this.oemRestoreConfirmed, controlAccepting: accepting,
      resumePhase: name === 'Resuming' ? 'Resuming' : name,
    };
    if (this.retryAfterMs !== undefined) result.retryAfterMs = this.retryAfterMs;
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

  async handshake(): Promise<FanHandshake> {
    this.calls.push('handshake');
    return { ok: true, supported: true, deviceClass: 'HandheldCompanion.Devices.ROGAlly', fanRoute: 'ProfileCurve', fanRouteWriteReady: true, deviceIdentity: { manufacturer: 'ASUS', model: 'ROG Ally', product: 'RC71L', bios: '3.08' } };
  }
  async open(): Promise<FanState> { this.calls.push('open'); this.openCalls += 1; return this.snapshot(); }
  async openEvents(): Promise<FanState> { this.calls.push('open-events'); return this.snapshot(); }
  async acquireControl(): Promise<FanLease> {
    this.calls.push('acquire');
    this.leaseSequence += 1;
    this.lease = { leaseId: 'lease-' + this.leaseSequence, generation: this.leaseSequence };
    return { ...this.lease };
  }
  async heartbeat(leaseId: string): Promise<FanLease> {
    this.calls.push('heartbeat');
    if (!this.lease || this.lease.leaseId !== leaseId) throw new FanApiError('LEASE_INVALID', 409, 'LEASE_INVALID');
    return { ...this.lease };
  }
  async enable(nodes: readonly FanNode[], _leaseId?: string): Promise<FanState> {
    this.calls.push('enable');
    this.lastNodes = nodes.map((node) => ({ ...node }));
    return this.snapshot();
  }
  lastNodes: FanNode[] = [];
  async applyPreset(_name: string, _leaseId?: string, nodes?: readonly FanNode[]): Promise<FanState> {
    this.calls.push('preset');
    if (nodes) this.lastNodes = nodes.map((node) => ({ ...node }));
    return this.snapshot();
  }
  async disable(): Promise<FanState> { this.calls.push('disable'); return this.snapshot(); }
  async restoreOem(): Promise<FanState> { this.calls.push('restore'); return this.cleanupState('OEM'); }
  async releaseControl(): Promise<FanState> { this.calls.push('release'); this.lease = null; return this.cleanupState('Released'); }
  async suspend(): Promise<FanState> { this.calls.push('suspend'); return this.snapshot('Suspended'); }
  async resume(): Promise<FanState> {
    this.calls.push('resume');
    this.remoteStateName = 'Ready';
    this.remotePowerState = 'On';
    this.remoteOpenCalled = true;
    this.oemRestoreConfirmed = false;
    this.controlAccepting = true;
    return this.snapshot();
  }
  async close(): Promise<FanState> { this.calls.push('close'); return this.cleanupState('Stopped'); }
  async shutdown(): Promise<void> { this.calls.push('shutdown'); }
  async getState(_timeoutMs?: number): Promise<FanState> { this.calls.push('state'); return this.snapshot(); }
}

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

function makeLifecycle(adapter: FakeAdapter, readNativePowerState: () => Promise<PowerLifecycleState | null>, deadlineMs = 1200): FanHostLifecycle {
  return new FanHostLifecycle({ enabled: true, adapter, launcher: new L(), heartbeatIntervalMs: 0, resumeWaitDeadlineMs: deadlineMs, readNativePowerState });
}

async function enterPostSleepStaleSuspended(life: FanHostLifecycle, adapter: FakeAdapter): Promise<void> {
  await life.start();
  await life.applyPreset('balanced', CURVE);
  life.setPowerGeneration(2);
  life.observePowerBoundary('suspending', 2);
  await life.suspend();
  assert(life.state === 'suspended', 'fixture must reach suspended before the missed wake edge');
  adapter.markSleepClosedSuspended();
  adapter.calls.length = 0;
}

async function main(): Promise<void> {
  // D1/R6-01：一次瞬态 POWER_RESUMING 拒绝后，恢复 owner 必须继续退避到写入成功。
  // 若「提前 settle」（变异 A），首轮写入前即清 owner ⇒ 0 成功、任务消失。
  {
    const adapter = new FlakyAdapter();
    let native = nativeState(2, 'suspending', false);
    const life = makeLifecycle(adapter, async () => native);
    try {
      await enterPostSleepStaleSuspended(life, adapter);
      await life.start();
      assert(life.recoveryActive, 'R6-01: fixture must register the recovery owner');
      life.stageRecoveryCurve(CURVE);
      adapter.failEnableRemaining = 1;
      native = nativeState(2, 'ready', true);
      await pause(1700);
      assert(adapter.successfulEnable > 0,
        'D1/R6-01: a transient POWER_RESUMING rejection must not end recovery (attempts=' + adapter.enableAttempts + ', successful=' + adapter.successfulEnable + ', recoveryActive=' + life.recoveryActive + ')');
    } finally { await life.close(); }
  }

  // D2：新 owner 必须持续超过旧「单窗口」截止，写成功后不再 Open/enable。
  // 「固定外层截止」变异会在此变红（owner 在窗口内提前放弃 ⇒ 0 成功）。
  {
    const adapter = new FlakyAdapter();
    const deadlineMs = 1000;
    const life = makeLifecycle(adapter, async () => nativeState(2, 'ready', true), deadlineMs);
    try {
      await enterPostSleepStaleSuspended(life, adapter);
      adapter.remoteStateName = 'Ready'; adapter.remotePowerState = 'On';
      adapter.remoteOpenCalled = true; adapter.controlAccepting = true;
      adapter.failEnableRemaining = 6;
      life.observePowerBoundary('resuming', 2);
      life.observePowerBoundary('resume-ready', 2);
      const began = Date.now();
      await life.resume().catch(() => undefined);
      assert(life.recoveryActive, 'D2: automatic resume must hand off to the continuous owner');
      let waited = 0;
      while (adapter.successfulEnable === 0 && waited < 8000) { await pause(50); waited += 50; }
      assert(adapter.successfulEnable > 0,
        'D2: the owner must keep retrying past the single-window deadline (attempts=' + adapter.enableAttempts + ', active=' + life.recoveryActive + ')');
      const elapsed = Date.now() - began;
      assert(elapsed > deadlineMs,
        'D2: recovery must outlive the single-window deadline (elapsed=' + elapsed + 'ms, deadline=' + deadlineMs + 'ms)');
      assert(!life.recoveryActive, 'D2: the owner must converge once the curve is written');
      const openCalls = adapter.openCalls;
      const enableCount = adapter.calls.filter((call) => call === 'enable').length;
      await pause(1200);
      assert(adapter.openCalls === openCalls && adapter.calls.filter((call) => call === 'enable').length === enableCount,
        'D2: no further Open/enable after convergence; extra=' + JSON.stringify(adapter.calls));
    } finally { await life.close(); }
  }

  // D1/R6-02：自动 resume() 必须接续到同一持续 owner；前三次失败、第四次成功。
  {
    const adapter = new FlakyAdapter();
    const life = makeLifecycle(adapter, async () => nativeState(2, 'ready', true));
    try {
      await enterPostSleepStaleSuspended(life, adapter);
      adapter.remoteStateName = 'Ready'; adapter.remotePowerState = 'On';
      adapter.remoteOpenCalled = true; adapter.controlAccepting = true;
      adapter.failEnableRemaining = 3;
      life.observePowerBoundary('resuming', 2);
      life.observePowerBoundary('resume-ready', 2);
      await life.resume().catch(() => undefined);
      await pause(1700);
      assert(adapter.successfulEnable > 0,
        'D1/R6-02: automatic resume must hand off to the continuous owner (attempts=' + adapter.enableAttempts + ', successful=' + adapter.successfulEnable + ')');
    } finally { await life.close(); }
  }

  // D5/R6-03：真实睡眠 inputReady=true 但 resumeReady=false —— 必须拒绝且零 resume/acquire/enable。
  // 若把 inputReady 加回 wake fact（变异 B），此处会放行 ⇒ 变红。
  {
    const adapter = new FakeAdapter();
    const life = makeLifecycle(adapter, async () => nativeState(2, 'suspending', true, false));
    try {
      await enterPostSleepStaleSuspended(life, adapter);
      await life.start();
      await life.apply(CURVE).then(
        () => { throw new Error('D5/R6-03: a write must not be admitted while the real suspend is in progress'); },
        () => undefined,
      );
      assert(!adapter.calls.includes('resume') && !adapter.calls.includes('acquire') && !adapter.calls.includes('enable'),
        'D5/R6-03: inputReady=true must not be treated as wake evidence; got ' + JSON.stringify(adapter.calls));
    } finally { await life.close(); }
  }

  console.log('r6 mutant scenario: ALL CHECKS PASS');
}

void main().catch((error: unknown) => {
  const detail = error instanceof Error ? error.message : String(error);
  console.error(detail);
  process.exitCode = 1;
});
`;

/** 每个变异：最小文本替换 + 期望被哪一项 R6 断言抓住。 */
const MUTATIONS = [
  {
    id: 'recovery-settled-before-curve-write',
    caughtBy: 'D1/R6-01（提前 settle ⇒ 1 次尝试 0 成功）',
    find: [
      '      const intentRevision = this.controlIntentRevision;',
      '      await this.enqueue(async () => {',
      '        // An explicit writer, close or newer sleep can finish while this owner',
      "        // waits for the queue. Recheck at execution, not only before enqueue.",
      '        if (token !== this.recoveryToken || !this.recoveryIntent',
      '            || !this.isResumeReplayCurrent(generation, intentRevision)) return false;',
      '        const latestCurve = this.desiredCurve;',
      '        if (!latestCurve) return false;',
      '        const response = await this.applyMutation(cloneFanNodes(latestCurve));',
      '        if (token === this.recoveryToken && this.recoveryIntent',
      '            && this.isResumeReplayCurrent(generation, intentRevision)) {',
      '          this.completedRecoveryWrite = {',
      '            generation, intentRevision, hostSession: this.coordinatorSession,',
      '            curve: this.activeCurve, response,',
      '          };',
      "          this.settleRecoveryTaskIfAny('curve-written');",
      '        }',
      '        return true;',
      '      });',
    ].join('\n'),
    replace: [
      "      this.settleRecoveryTaskIfAny('curve-written');",
      '      await this.enqueue(() => this.applyMutation(cloneFanNodes(desired)));',
      '      if (token !== this.recoveryToken || !this.recoveryIntent) return;',
    ].join('\n'),
  },
  {
    id: 'inputready-readded-as-wake-fact',
    caughtBy: 'D5/R6-03（真实睡眠 inputReady 放行 ⇒ 非零设备动作）',
    find: [
      '    const automaticWake = sameGeneration && !suspendInProgress',
      '      && (phaseReady || fanWakeFact);',
    ].join('\n'),
    replace: [
      '    const automaticWake = sameGeneration',
      '      && (phaseReady || fanWakeFact || native?.inputReady === true);',
    ].join('\n'),
  },
  {
    id: 'fixed-outer-deadline-restores-giveup',
    caughtBy: 'D2（固定外层截止 ⇒ owner 在单窗口内放弃，0 成功）',
    find: [
      '      // 瞬态失败（含 POWER_RESUMING）保留意图继续退避。',
      "      this.recoveryState = 'queued';",
      '      this.scheduleRecoveryTick(this.nextRecoveryBackoff());',
    ].join('\n'),
    replace: [
      '      // 变异：恢复旧「固定外层截止」——超过 2 轮即放弃。',
      '      if (this.recoveryAttempts >= 2) { this.recoveryState = "needs-attention"; this.stopRecoveryLoop("fixed-outer-deadline"); return; }',
      "      this.recoveryState = 'queued';",
      '      this.scheduleRecoveryTick(this.nextRecoveryBackoff());',
    ].join('\n'),
  },
];

function bundleAndRun() {
  mkdirSync(OUT_DIR, { recursive: true });
  execSync(
    `npx esbuild "${SCENARIO}" --bundle --platform=node --format=esm --outfile="${OUT_FILE}" --alias:@=./src`,
    { cwd: ROOT, stdio: 'pipe' },
  );
  try {
    execSync(`"${process.execPath}" "${OUT_FILE}"`, { cwd: ROOT, stdio: 'pipe' });
    return { code: 0, detail: '' };
  } catch (error) {
    const stderr = error.stderr ? error.stderr.toString() : '';
    const stdout = error.stdout ? error.stdout.toString() : '';
    const detail = (stderr + stdout).split('\n').filter((line) => line.trim().length > 0).slice(-4).join(' | ');
    return { code: typeof error.status === 'number' && error.status !== 0 ? error.status : 1, detail };
  }
}

let failed = 0;
try {
  writeFileSync(MUTANT, ORIGINAL);
  writeFileSync(SCENARIO, SCENARIO_SOURCE);
  const control = bundleAndRun();
  if (control.code !== 0) {
    console.log(`fan r6 recovery mutation control: FAIL (原始源码未通过定向断言：${control.detail})`);
    failed += 1;
  } else {
    console.log('fan r6 recovery mutation control: PASS (原始源码全绿: D1/R6-01 + D2 + D1/R6-02 + D5/R6-03)');
  }
  for (const mutation of MUTATIONS) {
    if (!ORIGINAL.includes(mutation.find)) {
      console.log(`fan r6 recovery mutation[${mutation.id}]: FAIL (锚点缺失，无法施加变异)`);
      failed += 1;
      continue;
    }
    writeFileSync(MUTANT, ORIGINAL.replace(mutation.find, mutation.replace));
    writeFileSync(SCENARIO, SCENARIO_SOURCE);
    const mutated = bundleAndRun();
    if (mutated.code === 0) {
      console.log(`fan r6 recovery mutation[${mutation.id}]: FAIL (变异未被抓到；期望由「${mutation.caughtBy}」检出)`);
      failed += 1;
    } else {
      console.log(`fan r6 recovery mutation[${mutation.id}]: PASS (已变红，由「${mutation.caughtBy}」检出：${mutated.detail})`);
    }
  }
} finally {
  for (const path of [MUTANT, SCENARIO]) {
    try { rmSync(path, { force: true }); } catch { /* best effort */ }
  }
}

if (failed > 0) {
  console.log(`fan r6 recovery mutation selftest: FAIL (${failed} 项)`);
  process.exit(1);
}
console.log('fan r6 recovery mutation selftest: PASS (control green; 3/3 R6 load-bearing mutations caught)');
