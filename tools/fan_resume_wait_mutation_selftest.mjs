/**
 * FAN-926R 唤醒边沿 —— 最小回归变异反控（执行单 §7）。
 *
 * 做法：对 `src/bridge/fanHost.ts` 施加**最小文本变异**，生成临时变异模块
 * `src/bridge/fanHost.__mutant.ts`，再用同一组定向行为断言（`src/bridge/__mutant_scenario.ts`）
 * 跑一遍：
 *   - 原始（未变异）⇒ 全部断言通过（正控）；
 *   - 每个变异 ⇒ 对应断言必须失败（行为门变红）。
 *
 * 五类变异中前四类在这里；第五类（日志免门分支移回开关之后）属 native E1b，见
 * `native/test/fan_log_dispatch_integration.cpp` 的变异反控。
 *
 * 只读产品源码、只写临时文件到 Build/Validation/Mutations 与 src/bridge 下的 __mutant 文件，
 * 结束后清理；不启动 Host、不产生硬件写入。
 */
import { execSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';

const ROOT = process.cwd();
const HOST_SOURCE = join(ROOT, 'src/bridge/fanHost.ts');
const MUTANT = join(ROOT, 'src/bridge/fanHost.__mutant.ts');
const SCENARIO = join(ROOT, 'src/bridge/__mutant_scenario.ts');
const OUT_DIR = join(ROOT, '..', '..', 'Build', 'Validation', 'Mutations');
const OUT_FILE = join(OUT_DIR, '__mutant_scenario.mjs');
// 统一到 LF：仓库里该文件的换行风格不影响锚点匹配（编译产物不受影响）。
const ORIGINAL = readFileSync(HOST_SOURCE, 'utf8').replace(/\r\n/g, '\n');

/**
 * 定向行为断言集合。逐项对应一条"必须仍然成立"的规则；被变异破坏时对应项抛错。
 * 每一项都用**生产方法/生产电源入口**，不使用替代的 suspend() 测试方法。
 */
const SCENARIO_SOURCE = `import { FanHostLifecycle, type FanHostLauncher, type FanHostProcess } from './fanHost.__mutant';
import type { FanApiAdapter, FanHandshake, FanLease, FanNode, FanState } from './fanApi';

const CURVE: FanNode[] = [
  { tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 },
  { tempC: 70, dutyPercent: 70 }, { tempC: 100, dutyPercent: 100 },
];
const assert = (condition: unknown, message: string): void => { if (!condition) throw new Error(message); };

class L implements FanHostLauncher {
  async start(): Promise<FanHostProcess> { return { pid: 7, executable: 'x' }; }
  async stop(): Promise<void> { /* noop */ }
}
class A implements FanApiAdapter {
  readonly enabled = true;
  calls: string[] = [];
  script: Array<() => FanState> = [];
  onStateCall: ((index: number) => void) | null = null;
  nextHeartbeat: Promise<FanLease> | null = null;
  lease: FanLease | null = null;
  private seq = 0;
  private n = 0;
  snap(name: string, accepting = true): FanState {
    return {
      state: name, powerState: 'On', protocolVersion: '2', hardwareWrites: false,
      hardwareWritesObserved: false, hardwareWritesEnabled: true, openCalled: true, openEventsCalled: true,
      resumePhase: name, controlAccepting: accepting, retryAfterMs: 250,
      lease: this.lease ? { ...this.lease } : null, leaseGeneration: this.lease?.generation ?? null,
    };
  }
  async handshake(): Promise<FanHandshake> { this.calls.push('handshake'); return { ok: true, supported: true, fanRoute: 'ProfileCurve', fanRouteWriteReady: true }; }
  async open(): Promise<FanState> { this.calls.push('open'); return this.snap('Ready'); }
  async openEvents(): Promise<FanState> { this.calls.push('open-events'); return this.snap('Ready'); }
  async acquireControl(): Promise<FanLease> { this.calls.push('acquire'); this.seq += 1; this.lease = { leaseId: 'lease-' + this.seq, generation: this.seq }; return { ...this.lease }; }
  async heartbeat(id: string): Promise<FanLease> {
    this.calls.push('heartbeat');
    if (this.nextHeartbeat) { const gated = this.nextHeartbeat; this.nextHeartbeat = null; return gated; }
    if (!this.lease || this.lease.leaseId !== id) throw new Error('LEASE_INVALID');
    return { ...this.lease };
  }
  async enable(): Promise<FanState> { this.calls.push('enable'); return this.snap('Ready'); }
  async applyPreset(): Promise<FanState> { this.calls.push('preset'); return this.snap('Ready'); }
  async disable(): Promise<FanState> { this.calls.push('disable'); return this.snap('Ready'); }
  async restoreOem(): Promise<FanState> { this.calls.push('restore'); const s = this.snap('OEM'); s.oemRestoreConfirmed = true; return s; }
  async releaseControl(): Promise<FanState> { this.calls.push('release'); this.lease = null; return this.snap('Released'); }
  async suspend(): Promise<FanState> { this.calls.push('suspend'); return this.snap('Suspended'); }
  async resume(): Promise<FanState> { this.calls.push('resume'); return this.snap('Ready'); }
  async close(): Promise<FanState> {
    this.calls.push('close');
    const s = this.snap('Stopped');
    s.oemRestoreConfirmed = true; s.openCalled = false; s.openEventsCalled = false; s.hardwareWritesEnabled = false;
    return s;
  }
  async shutdown(): Promise<void> { this.calls.push('shutdown'); }
  async getState(): Promise<FanState> {
    const index = this.n; this.n += 1;
    this.calls.push('state');
    this.onStateCall?.(index);
    if (this.script.length > 0) {
      const step = this.script.length === 1 ? this.script[0] : this.script.shift()!;
      return step();
    }
    return this.snap('Ready');
  }
}

const newLifecycle = (a: A, deadlineMs = 2000): FanHostLifecycle =>
  new FanHostLifecycle({ enabled: true, adapter: a, launcher: new L(), heartbeatIntervalMs: 0, resumeWaitDeadlineMs: deadlineMs });

async function armWake(life: FanHostLifecycle, a: A, generation = 1): Promise<void> {
  await life.start();
  await life.applyPreset('balanced', CURVE);
  life.setPowerGeneration(generation);
  await life.suspend();
  life.observePowerBoundary('resuming', generation);
  life.observePowerBoundary('resume-ready', generation);
  a.calls.length = 0;
}

async function main(): Promise<void> {
  // 1) 真睡眠 + 宿主报 Suspended：无唤醒依据 ⇒ 必须在准入处终止且不写。
  {
    const a = new A();
    const life = newLifecycle(a);
    await life.start();
    await life.applyPreset('balanced', CURVE);
    life.setPowerGeneration(1);
    life.observePowerBoundary('suspending', 1);
    a.calls.length = 0;
    a.script = [() => a.snap('Suspended', false)];
    const outcome = await life.apply(CURVE).then(() => 'resolved', (e: unknown) => String((e as Error).message));
    assert(/FAN_RESUME_ADMISSION_(SUPERSEDED|TERMINAL_FAILED)/.test(outcome),
      'real sleep without wake evidence must terminate at admission (outcome=' + outcome + ')');
    assert(a.calls.filter((c) => c === 'open' || c === 'enable').length === 0, 'real sleep must not write');
    await life.close();
  }

  // 2) 准入 GET 在途时收到**同代** suspending 边界（生产入口，只走 observePowerBoundary）：
  //    旧回包不得写入。
  {
    const a = new A();
    const life = newLifecycle(a);
    await armWake(life, a);
    a.onStateCall = (index) => { if (index === 0) life.observePowerBoundary('suspending', 1); };
    const outcome = await life.resume().then(() => 'resolved', (e: unknown) => String((e as Error).message));
    assert(/FAN_CONTROL_INTENT_SUPERSEDED|FAN_RESUME_WAIT_CANCELLED|FAN_RESUME_ADMISSION_SUPERSEDED/.test(outcome),
      'in-flight GET superseded by the real same-generation suspending entry must terminate (outcome=' + outcome + ')');
    assert(a.calls.filter((c) => c === 'open' || c === 'enable').length === 0, 'superseded request must not write');
    await life.close();
  }

  // 3) 睡眠边界后迟到的**成功**心跳：不得替换 lease，不得再发 tick。
  {
    const a = new A();
    const life = new FanHostLifecycle({ enabled: true, adapter: a, launcher: new L(), heartbeatIntervalMs: 40, resumeWaitDeadlineMs: 2000 });
    await life.start();
    await life.applyPreset('balanced', CURVE);
    assert(life.currentLease !== null, 'fixture must hold a lease');
    let resolveLate: ((lease: FanLease) => void) | null = null;
    a.nextHeartbeat = new Promise<FanLease>((resolve) => { resolveLate = resolve; });
    const inFlight = life.heartbeat().catch(() => undefined);
    await new Promise((r) => setTimeout(r, 20));
    const callsAtBoundary = a.calls.length;
    const leaseBefore = life.currentLease !== null ? life.currentLease.leaseId : null;
    life.observePowerBoundary('suspending', 0);
    resolveLate!({ leaseId: 'lease-late', generation: 99 });
    await inFlight;
    assert(life.currentLease === null || life.currentLease.leaseId === leaseBefore,
      'late heartbeat success must not replace the lease (lease=' + (life.currentLease ? life.currentLease.leaseId : 'null') + ')');
    assert(life.currentLease === null || life.currentLease.leaseId !== 'lease-late', 'late heartbeat success must not be adopted');
    await new Promise((r) => setTimeout(r, 150));
    assert(a.calls.length === callsAtBoundary, 'no new heartbeat tick after the sleep boundary');
    await life.close();
  }

  // 4) 唤醒重放自己的只读快照之后（最终写之前）到达终止性意图：不得再写。
  {
    const a = new A();
    const life = newLifecycle(a);
    await armWake(life, a);
    let fired = false;
    a.onStateCall = (index) => { if (index === 1 && !fired) { fired = true; void life.close().catch(() => undefined); } };
    await life.resume();
    assert(fired, 'fixture must reach the replay read');
    assert(a.calls.filter((c) => c === 'enable').length === 0,
      'a superseded wake replay must not write hardware (calls=' + a.calls.join(',') + ')');
  }

  console.log('mutant scenario: ALL CHECKS PASS');
}

void main();
`;

/** 每一类变异：最小文本替换 + 期望被哪一项断言抓住（用于报告）。 */
const MUTATIONS = [
  {
    id: 'terminal-superseded-treated-as-pass',
    caughtBy: '1) 真睡眠必须在准入处终止',
    find: "    throw new Error(`FAN_RESUME_ADMISSION_${reason.toUpperCase().replace(/-/g, '_')}:${action}:${detail}`);",
    replace: "    void reason; void action; void detail; return undefined as never;",
  },
  {
    id: 'real-power-entry-cancel-removed',
    caughtBy: '2) 同代 suspending 入口取消 / 3) 睡眠边界心跳作废',
    find: "    if (event !== 'suspending' && event !== 'shutdown') return;\n    if (this.supersededPowerGeneration === generation) return;",
    replace: "    if (event !== 'suspending' && event !== 'shutdown') return;\n    return;",
  },
  {
    id: 'late-heartbeat-epoch-check-removed',
    caughtBy: '3) 迟到心跳不得替换 lease',
    find: "    return attempt.epoch === this.heartbeatEpoch\n      && attempt.generation === Math.max(0, Math.floor(this.powerGeneration))\n      && attempt.hostPid === (this.process?.pid ?? 0)\n      && this.lease?.leaseId === attempt.leaseId;",
    replace: "    void attempt; return true;",
  },
  {
    id: 'final-write-revision-check-removed',
    caughtBy: '4) 最终写前 revision 校验',
    find: "    return intentRevision === this.controlIntentRevision\n      && generation === Math.max(0, Math.floor(this.powerGeneration));",
    replace: "    void generation; void intentRevision; return true;",
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
  // 正控：原始源码必须全绿。
  writeFileSync(MUTANT, ORIGINAL);
  writeFileSync(SCENARIO, SCENARIO_SOURCE);
  const control = bundleAndRun();
  if (control.code !== 0) {
    console.log(`fan resume wait mutation control: FAIL (原始源码未通过定向断言：${control.detail})`);
    failed += 1;
  } else {
    console.log('fan resume wait mutation control: PASS (原始源码全绿)');
  }
  for (const mutation of MUTATIONS) {
    if (!ORIGINAL.includes(mutation.find)) {
      console.log(`fan resume wait mutation[${mutation.id}]: FAIL (锚点缺失，无法施加变异)`);
      failed += 1;
      continue;
    }
    writeFileSync(MUTANT, ORIGINAL.replace(mutation.find, mutation.replace));
    writeFileSync(SCENARIO, SCENARIO_SOURCE);
    const mutated = bundleAndRun();
    if (mutated.code === 0) {
      console.log(`fan resume wait mutation[${mutation.id}]: FAIL (变异未被抓到；期望由「${mutation.caughtBy}」检出)`);
      failed += 1;
    } else {
      console.log(`fan resume wait mutation[${mutation.id}]: PASS (已变红，由「${mutation.caughtBy}」检出：${mutated.detail})`);
    }
  }
} finally {
  for (const path of [MUTANT, SCENARIO]) {
    try { rmSync(path, { force: true }); } catch { /* best effort */ }
  }
}

if (failed > 0) {
  console.log(`fan resume wait mutation selftest: FAIL (${failed} 项)`);
  process.exit(1);
}
console.log('fan resume wait mutation selftest: PASS (control green; 4/4 minimal mutations caught)');
