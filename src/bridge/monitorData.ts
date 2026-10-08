// monitorData.ts — 196/197/198：前三端统一监控数据入口
//
// 数据所有权：native 单采集者（约 1Hz）→ 有界内存快照（monitor.snapshot）→ 本入口。
// 消费端：TopMonitorBar（展示）、PerformanceScheduleView（展示，经共享 ref）、autofloat
// （独立控制节拍）。本入口只负责：能力协商、在途请求合并、生命周期（会话/代际/序号）校验、
// 需求登记（把三端需求合并为 monitor.start/stop 的 {top,fps} 标志）。不新增轮询；不缓存长期
// TTL；500ms 图表刷新只读本入口已取到的数据，不触发新的硬件采集。
//
// 198 状态契约（按 198 §3A）：
//   · 每个标志三态：期望(desired) / 已确认(confirmed) / 已发出未确认(pending=结果未知)。
//     超时或拒绝都只进 pending，不得当作成功；**释放时若存在 pending 的 start，也必须发幂等
//     stop**（start 可能已真实执行），确保最终能真正停止；反之 stop 未确认时同样收敛。
//   · 回执校验：仅 `ok===true` 才算确认（ok:false 属拒绝，进 pending 并按预算重试）。
//   · 读回校准：快照携带 topActive/fpsActive（unavailable 也带）⇒ 观察到活跃即视为已确认、
//     观察到不活跃即清 confirmed/pending（不依赖回执即可收敛）。
//   · 会话身份：响应携带 native 会话号（与采集代际分开）。**只有真实会话变化**才重置
//     confirmed/pending/takeover 与代际/序号基准并重新协调仍有的需求；同会话低代际/乱序一律
//     拒收——不再把"代际变小"当作重启证明。
//   · 意图串行：pump 单跑者 + 每次 await 后重算需求 ⇒ 旧意图不会覆盖新意图；
//     日常 read 只按退避推进（不绕退避耗预算），acquire/release 作为新意图可立即推进（含必要停止）。
//   · takeover：需 ok && cleaned 才算完成、绑定会话与 relay epoch，旧会话/旧 epoch 的迟到回执
//     不得把新会话标成 done；失败按读取事件有界重试，且不偷读残留文件。
//   · 停止失效按**字段自身时间戳**比较（不用四文件最大时间掩盖旧 payload）。
//
// 兼容矩阵：新前端 + 旧 native（无 monitor.snapshot ⇒ unknown）→ 本会话回退四文件通路；
// 新 native + 旧前端 → native 默认继续写四文件（takeover 以 native 会话绑定，重载/恢复复位）；
// 无 session 字段的过渡版快照 native ⇒ 仅按代际/序号单调校验（保守：回退即拒收，标注为兼容防护）。
import { invoke } from './ipc';
import { fs } from './api';
import type { TopMonData } from './topmon';

export interface MonitorFpsPayload {
  ts: number;
  fps: number;
  fps1: number;
  gpu: number;
  packagePower: number;
  game: string | null;
  pid: number;
}

export interface MonitorSnapshotView {
  schema: number;
  /** native 会话号（null=对端未提供，走兼容单调校验） */
  session: number | null;
  /** relay 所有权代数（takeover/release 绑定用；null=对端未提供） */
  relayEpoch: number | null;
  generation: number;
  seq: number;
  collectedAt: number;
  /** 监控心跳（fps 闩锁期间刷新）；null = 无心跳（语义同 fps-monitor.hb 缺失） */
  heartbeatAt: number | null;
  /** HWiNFO 健康位时间；null = 不健康/未启动（语义同 hwinfo-ok 缺失） */
  hwinfoOkAt: number | null;
  top: TopMonData | null;
  fps: MonitorFpsPayload | null;
  topActive: boolean;
  fpsActive: boolean;
  hwDown: boolean;
  source: 'snapshot' | 'files';
}

export interface MonitorHubDeps {
  invoke: (cmd: string, args?: object, options?: { timeoutMs?: number }) => Promise<unknown>;
  readTextFile: (path: string, maxBytes?: number) => Promise<string>;
  now: () => number;
  /** 测试注入：默认全局 setTimeout/clearTimeout（仅用于有界收尾重试，不建高频轮询） */
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (handle: unknown) => void;
}

export type MonitorTransportMode = 'unknown' | 'snapshot' | 'files';
export type MonitorTakeoverState = 'idle' | 'pending' | 'done';
export type MonitorFlagState = { top: boolean; fps: boolean };

const DEFAULT_PC_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
let pcDir = DEFAULT_PC_DIR;

export function setMonitorDataPowerControlDir(dir: string): void {
  pcDir = dir.replace(/\//g, '\\').replace(/\\+$/, '');
}

function clampPercent(value: unknown): number {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? Math.min(100, Math.max(0, parsed)) : 0;
}

// 与 topmon.json 历史字段默认值一致（语义不变）。
function parseTopPayload(raw: string): TopMonData | null {
  try {
    const r = JSON.parse(raw) as Partial<TopMonData>;
    return {
      ts: Number(r.ts) || 0,
      tdpW: Number(r.tdpW) || 0,
      freqMhz: Number(r.freqMhz) || 0,
      tempC: Number(r.tempC) || 0,
      ac: Number(r.ac) === 0 ? 0 : 1,
      hasBattery: r.hasBattery === true,
      batteryPercent: Number.isFinite(Number(r.batteryPercent)) ? Number(r.batteryPercent) : -1,
      chargeW: Number(r.chargeW) || 0,
      remainMin: Number.isFinite(Number(r.remainMin)) ? Number(r.remainMin) : -1,
      cpuUsage: Number(r.cpuUsage) || 0,
      gpuPowerW: Number(r.gpuPowerW) || 0,
      gpuClockMhz: Number(r.gpuClockMhz) || 0,
      thermalThrottleFound: r.thermalThrottleFound === true,
      thermalThrottleMax: Number(r.thermalThrottleMax) || 0,
      virtualMemoryCommittedFound: r.virtualMemoryCommittedFound === true,
      virtualMemoryCommittedMb: Number(r.virtualMemoryCommittedMb) || 0,
      virtualMemoryLoadFound: r.virtualMemoryLoadFound === true,
      virtualMemoryLoadPct: clampPercent(r.virtualMemoryLoadPct),
      hwDown: !!r.hwDown,
    };
  } catch {
    return null;
  }
}

// 与 fps-status.json 历史字段默认值一致。
function parseFpsPayload(raw: string): MonitorFpsPayload | null {
  try {
    const r = JSON.parse(raw) as Partial<MonitorFpsPayload>;
    return {
      ts: Number(r.ts) || 0,
      fps: Number(r.fps) || 0,
      fps1: Number(r.fps1) || 0,
      gpu: Number(r.gpu) || 0,
      packagePower: Number(r.packagePower) || 0,
      game: typeof r.game === 'string' ? r.game : null,
      pid: Number(r.pid) || 0,
    };
  } catch {
    return null;
  }
}

function isUnknownCommand(err: unknown): boolean {
  const message = err instanceof Error ? err.message : String(err);
  return /^unknown:/i.test(message.trim());
}

export interface MonitorDataHub {
  read: () => Promise<MonitorSnapshotView | null>;
  acquire: (owner: string, need: { top?: boolean; fps?: boolean }) => () => void;
  transport: () => MonitorTransportMode;
  /** 已确认（ok 回执或读回观察到活跃）的需求标志（测试/诊断） */
  applied: () => MonitorFlagState;
  /** 已发出但结果未知（超时/拒绝）的标志（测试/诊断） */
  pending: () => MonitorFlagState;
  /** 期望需求（测试/诊断） */
  desired: () => MonitorFlagState;
  /** 当前 native 会话号（null=未知/对端未提供，测试/诊断） */
  session: () => number | null;
  /** takeover 状态：idle=未尝试 / pending=失败待重试 / done=已确认（测试/诊断） */
  takeover: () => MonitorTakeoverState;
  /** 已发生的 start/stop/takeover 调用次数（有界重试观察，测试/诊断） */
  attempts: () => { start: number; stop: number; takeover: number };
  pendingReads: () => number;
}

const SNAPSHOT_SCHEMA = 1;
const START_RETRY_BUDGET = 6;
const STOP_RETRY_BUDGET = 6;
const TAKEOVER_RETRY_BUDGET = 5;

export function createMonitorDataHub(deps: MonitorHubDeps): MonitorDataHub {
  const setTimer =
    deps.setTimer ?? ((fn: () => void, ms: number) => setTimeout(fn, ms) as unknown);
  const clearTimer =
    deps.clearTimer ?? ((handle: unknown) => clearTimeout(handle as ReturnType<typeof setTimeout>));

  let transport: MonitorTransportMode = 'unknown';
  let inflight: Promise<MonitorSnapshotView | null> | null = null;
  let pendingReadCount = 0;

  // 需求登记：同 owner 每次登记独立 token（旧 disposer 只释放自己的登记）。
  const demands = new Map<string, { token: number; top: boolean; fps: boolean }>();
  let nextToken = 1;

  // 每个标志三态：confirmed（明确成功/读回活跃）、pending（已发出未确认）。
  const confirmed: MonitorFlagState = { top: false, fps: false };
  const pending: MonitorFlagState = { top: false, fps: false };

  // 停止失效（按字段自身时间戳比较；旧在途回复不得复活已释放数据）。
  const lastStopAt = { top: 0, fps: 0 };

  // 199：命令/读取的版本关联。读回只能确认它**确实观察到**的命令状态：
  //   · intentEpoch 在每次"发出"启停命令前自增，并记录到对应标志（flagIntent）；
  //   · 读请求在**发起前**捕获 doneEpoch（必须先于 pump，因为 pump 会同步发出命令）；
  //   · 读回校准时，只有 flagIntent[flag] <= doneEpoch（该标志的读请求发出后没有再发命令）
  //     才允许校准。于是"较早发出、较晚返回"的旧回复抹不掉更新的 pending，
  //     也不会在停止收敛后又被旧 active 读回重新触发命令。
  // 说明（与 native 的配合）：native 的 topActive/fpsActive 是**响应时刻的活闩锁**，且 IPC 按
  // 队列顺序处理，因此"读请求晚于命令发出"的观察一定晚于该命令被执行 ⇒ 该观察可安全校准。
  let intentEpoch = 0;
  const flagIntent = { top: 0, fps: 0 };

  // 生命周期基准：会话号（真实会话变化才重置）+ 采集代际/序号（同会话单调）。
  let expectedSession: number | null = null;
  let expectedGeneration = 0;
  let expectedSeq = 0;
  let expectedRelayEpoch: number | null = null;

  // 有界重试：intent（acquire/release）立即推进；poll/timer 仅按退避推进（read 不绕退避）。
  let startBudget = START_RETRY_BUDGET;
  let stopBudget = STOP_RETRY_BUDGET;
  let startRetryNextAt = 0; // 启动侧退避门
  let stopRetryNextAt = 0;  // 停止侧退避门（与启动分开：启动失败不得阻塞停止收尾）
  let retryTimer: unknown = null;
  let pumpRunning = false;
  let pumpQueued = false;
  const attempts = { start: 0, stop: 0, takeover: 0 };

  // takeover：明确回执（ok && cleaned）才算完成；绑定会话与 relay epoch。
  let takeoverState: MonitorTakeoverState = 'idle';
  let takeoverBudget = TAKEOVER_RETRY_BUDGET;
  let takeoverInFlight = false;

  function desired(): MonitorFlagState {
    const want = { top: false, fps: false };
    for (const need of demands.values()) {
      if (need.top) want.top = true;
      if (need.fps) want.fps = true;
    }
    return want;
  }

  function backoffMs(remaining: number, budgetMax: number): number {
    const used = Math.max(0, budgetMax - remaining);
    return Math.min(8000, 500 * Math.pow(2, used));
  }

  function hasUnsettledWork(): boolean {
    const want = desired();
    return (
      (want.top && !confirmed.top) ||
      (want.fps && !confirmed.fps) ||
      (!want.top && (confirmed.top || pending.top)) ||
      (!want.fps && (confirmed.fps || pending.fps))
    );
  }

  function nextRetryAt(): number {
    const want = desired();
    const stopWork =
      (!want.top && (confirmed.top || pending.top)) || (!want.fps && (confirmed.fps || pending.fps));
    const startWork = (want.top && !confirmed.top) || (want.fps && !confirmed.fps);
    const gates: number[] = [];
    if (stopWork) gates.push(stopRetryNextAt);
    if (startWork) gates.push(startRetryNextAt);
    return gates.length ? Math.min(...gates) : 0;
  }

  function scheduleRetry(): void {
    if (!hasUnsettledWork()) return;
    if (startBudget <= 0 && stopBudget <= 0) return;
    if (retryTimer !== null) return;
    const wait = Math.max(0, nextRetryAt() - deps.now());
    retryTimer = setTimer(() => {
      retryTimer = null;
      void pump('timer');
    }, wait > 0 ? wait : 500);
  }

  function cancelRetry(): void {
    if (retryTimer !== null) {
      clearTimer(retryTimer);
      retryTimer = null;
    }
  }

  // 真实会话变化：重置确认/接管与代际/序号基准，并重新协调仍有的需求。
  function acceptNewSession(session: number): void {
    expectedSession = session;
    expectedGeneration = 0;
    expectedSeq = 0;
    confirmed.top = false;
    confirmed.fps = false;
    pending.top = false;
    pending.fps = false;
    takeoverState = 'idle';
    takeoverBudget = TAKEOVER_RETRY_BUDGET;
    startBudget = START_RETRY_BUDGET;
    stopBudget = STOP_RETRY_BUDGET;
    startRetryNextAt = 0;
    stopRetryNextAt = 0;
    void pump('intent');
  }

  // 需求与回执对齐：停止先于启动；单跑者 + 每次 await 后重算（旧意图不覆盖新意图）。
  // trigger: intent=新意图（立即推进）；poll/timer=按退避推进（日常 read 不绕退避耗预算）。
  async function pump(trigger: 'intent' | 'poll' | 'timer'): Promise<void> {
    if (pumpRunning) {
      pumpQueued = true;
      return;
    }
    // 新意图只允许"立即推进一次"：清掉两侧退避门一次；同一次 pass 内的后续重试仍必须服从退避。
    if (trigger === 'intent') {
      startRetryNextAt = 0;
      stopRetryNextAt = 0;
    }
    pumpRunning = true;
    try {
      for (let guard = 0; guard < 8; guard++) {
        pumpQueued = false;
        const want = desired();
        const needStartTop = want.top && !confirmed.top;
        const needStartFps = want.fps && !confirmed.fps;
        const needStopTop = !want.top && (confirmed.top || pending.top);
        const needStopFps = !want.fps && (confirmed.fps || pending.fps);
        if (!needStartTop && !needStartFps && !needStopTop && !needStopFps) {
          cancelRetry();
          break;
        }
        // 退避门按工作类型分别判断（停止失败不阻塞启动重试，反之亦然）。
        if (needStopTop || needStopFps) {
          if (stopBudget <= 0) break;
          if (deps.now() < stopRetryNextAt) {
            scheduleRetry();
            break;
          }
          // 期望已释放：先落失效门（即使回执失败，旧在途回复也不得复活数据）。
          if (needStopTop) lastStopAt.top = deps.now();
          if (needStopFps) lastStopAt.fps = deps.now();
          attempts.stop += 1;
          stopBudget -= 1;
          pending.top = pending.top || needStopTop;
          pending.fps = pending.fps || needStopFps;
          // 199：命令版本——发出一刻即记账，早于该命令发出的读回不得清掉它。
          const stopIssueEpoch = ++intentEpoch;
          if (needStopTop) flagIntent.top = stopIssueEpoch;
          if (needStopFps) flagIntent.fps = stopIssueEpoch;
          try {
            const res = (await deps.invoke('monitor.stop', {
              top: needStopTop,
              fps: needStopFps,
            })) as Record<string, unknown> | null;
            if (res && res.ok === true) {
              if (needStopTop) {
                confirmed.top = false;
                pending.top = false;
              }
              if (needStopFps) {
                confirmed.fps = false;
                pending.fps = false;
              }
              stopRetryNextAt = 0;
            } else {
              stopRetryNextAt = deps.now() + backoffMs(stopBudget, STOP_RETRY_BUDGET);
            }
          } catch {
            // 超时/异常 = 结果未知：可能已执行 ⇒ 保持 pending，按预算重试或由读回校准收敛。
            stopRetryNextAt = deps.now() + backoffMs(stopBudget, STOP_RETRY_BUDGET);
          }
          scheduleRetry();
          continue; // 重算最新需求，旧意图不覆盖新意图
        }
        if (needStartTop || needStartFps) {
          if (startBudget <= 0) break;
          if (deps.now() < startRetryNextAt) {
            scheduleRetry();
            break;
          }
          attempts.start += 1;
          startBudget -= 1;
          pending.top = pending.top || needStartTop;
          pending.fps = pending.fps || needStartFps;
          // 199：命令版本——发出一刻即记账（见 flagIntent 注释）。
          const startIssueEpoch = ++intentEpoch;
          if (needStartTop) flagIntent.top = startIssueEpoch;
          if (needStartFps) flagIntent.fps = startIssueEpoch;
          try {
            const res = (await deps.invoke('monitor.start', {
              top: needStartTop,
              fps: needStartFps,
            })) as Record<string, unknown> | null;
            if (res && res.ok === true) {
              if (needStartTop) {
                confirmed.top = true;
                pending.top = false;
              }
              if (needStartFps) {
                confirmed.fps = true;
                pending.fps = false;
              }
              startRetryNextAt = 0;
            } else {
              // 拒绝（ok:false）：不得当作成功；保持 pending（可能已执行）并退避。
              startRetryNextAt = deps.now() + backoffMs(startBudget, START_RETRY_BUDGET);
            }
          } catch {
            startRetryNextAt = deps.now() + backoffMs(startBudget, START_RETRY_BUDGET);
          }
          scheduleRetry();
          continue;
        }
      }
    } finally {
      pumpRunning = false;
      if (pumpQueued) void pump('poll');
    }
  }

  // 读回校准：不依赖回执即可收敛。仅当对端**显式携带**布尔闩锁时才校准
// （字段缺失=信息不足，不得按"不活跃"处理，否则会对过渡版 native 反复重发 start）。
// 199：只校准"读请求发出后没有再发命令"的标志（flagIntent <= doneEpoch）——旧回复不得
// 清掉较新命令的 pending/confirmed；被跳过的标志由有界重试或更晚的有效读回收敛。
  function reconcileFromView(active: { top: boolean | null; fps: boolean | null },
                             doneEpoch: number): void {
    const topCalibratable = flagIntent.top <= doneEpoch;
    const fpsCalibratable = flagIntent.fps <= doneEpoch;
    if (topCalibratable) {
      if (active.top === true) confirmed.top = true;
      if (active.top === false) {
        confirmed.top = false;
        pending.top = false;
      }
    }
    if (fpsCalibratable) {
      if (active.fps === true) confirmed.fps = true;
      if (active.fps === false) {
        confirmed.fps = false;
        pending.fps = false;
      }
    }
    // 校准产生新的未决工作（如需停止/需重发）时按退避推进一次（成功的路径 retryNextAt=0 ⇒ 立即）。
    if (hasUnsettledWork()) void pump('poll');
  }

  // takeover：需 ok && cleaned、绑定发起时的会话与 relay epoch；迟到回执不得标记新会话。
  function maybeTakeover(): void {
    if (transport === 'files' || takeoverState === 'done' || takeoverInFlight) return;
    if (takeoverBudget <= 0) return;
    takeoverInFlight = true;
    attempts.takeover += 1;
    takeoverBudget -= 1;
    takeoverState = 'pending'; // 已发起未确认：不得当作 idle（更不得当作 done）
    const sessionAtIssue = expectedSession;
    const epochAtIssue = expectedRelayEpoch;
    const args: Record<string, unknown> = {};
    if (epochAtIssue !== null) args.epoch = epochAtIssue;
    void (async () => {
      try {
        const res = (await deps.invoke('monitor.takeover', args)) as Record<string, unknown> | null;
        if (expectedSession !== sessionAtIssue || expectedRelayEpoch !== epochAtIssue) return; // 旧会话/旧 epoch
        takeoverState = res && res.ok === true && res.cleaned === true ? 'done' : 'pending';
      } catch {
        if (expectedSession === sessionAtIssue && expectedRelayEpoch === epochAtIssue) {
          takeoverState = 'pending';
        }
      } finally {
        takeoverInFlight = false;
      }
    })();
  }

  // 生命周期 + 数据校验：只有真实会话变化才重置；同会话低代际/乱序拒收。
  function acceptLifecycle(r: Record<string, unknown>): 'ok' | 'stale' | 'new-session' {
    const sessionRaw = Number(r.session);
    const hasSession = Number.isFinite(sessionRaw) && sessionRaw > 0;
    const generation = Number(r.generation);
    const seq = Number(r.seq);
    const relayRaw = Number(r.relayEpoch);
    if (!Number.isFinite(generation) || generation < 0) return 'stale';
    if (!Number.isFinite(seq) || seq < 0) return 'stale';
    if (hasSession) {
      if (expectedSession === null) {
        // 首次"学会"会话号不是会话变化：只建立基准，不重置确认/接管。
        expectedSession = sessionRaw;
        expectedGeneration = generation;
        expectedSeq = seq;
        if (Number.isFinite(relayRaw)) expectedRelayEpoch = relayRaw;
        return 'ok';
      }
      if (sessionRaw !== expectedSession) {
        // 真实会话变化：重置确认/接管与代际/序号基准，并重新协调仍有的需求。
        acceptNewSession(sessionRaw);
        expectedGeneration = generation;
        expectedSeq = seq;
        if (Number.isFinite(relayRaw)) expectedRelayEpoch = relayRaw;
        return 'new-session';
      }
    }
    // 同会话（或对端无 session 的兼容情形）：严格单调。
    if (generation < expectedGeneration) return 'stale';
    if (generation === expectedGeneration && seq < expectedSeq) return 'stale';
    expectedGeneration = generation;
    expectedSeq = seq;
    if (Number.isFinite(relayRaw)) expectedRelayEpoch = relayRaw;
    return 'ok';
  }

  function normalizeSnapshot(res: unknown): MonitorSnapshotView | null {
    if (!res || typeof res !== 'object') return null;
    const r = res as Record<string, unknown>;
    if (r.available !== true) return null;
    if (Number(r.schema) !== SNAPSHOT_SCHEMA) return null;
    const lifecycle = acceptLifecycle(r);
    if (lifecycle === 'stale') return null; // 同会话旧代际/乱序：拒收
    const collectedAt = Number(r.collectedAt);
    if (!Number.isFinite(collectedAt) || collectedAt <= 0) return null;
    const heartbeat = Number(r.heartbeatAt);
    const hwinfoOk = Number(r.hwinfoOkAt);
    const sessionRaw = Number(r.session);
    const relayRaw = Number(r.relayEpoch);
    const top = typeof r.top === 'string' ? parseTopPayload(r.top) : null;
    const fps = typeof r.fps === 'string' ? parseFpsPayload(r.fps) : null;
    return {
      schema: SNAPSHOT_SCHEMA,
      session: Number.isFinite(sessionRaw) && sessionRaw > 0 ? sessionRaw : null,
      relayEpoch: Number.isFinite(relayRaw) ? relayRaw : null,
      generation: Number(r.generation) || 0,
      seq: Number(r.seq) || 0,
      collectedAt,
      heartbeatAt: Number.isFinite(heartbeat) && heartbeat > 0 ? heartbeat : null,
      hwinfoOkAt: Number.isFinite(hwinfoOk) && hwinfoOk > 0 ? hwinfoOk : null,
      top,
      fps,
      topActive: r.topActive === true,
      fpsActive: r.fpsActive === true,
      hwDown: r.hwDown === true,
      source: 'snapshot',
    };
  }

  // 停止失效：按各字段自身时间戳比较（文件兼容分支同样适用）。不晚于停止意图的都不复活。
  function applyStaleFloor(view: MonitorSnapshotView): MonitorSnapshotView {
    if (view.top && view.top.ts > 0 && view.top.ts <= lastStopAt.top) view.top = null;
    if (view.fps && view.fps.ts > 0 && view.fps.ts <= lastStopAt.fps) view.fps = null;
    if (view.heartbeatAt !== null && view.heartbeatAt <= lastStopAt.fps) view.heartbeatAt = null;
    if (view.hwinfoOkAt !== null && view.hwinfoOkAt <= Math.max(lastStopAt.top, lastStopAt.fps)) {
      view.hwinfoOkAt = null;
    }
    return view;
  }

  async function readViaSnapshot(doneEpoch: number): Promise<MonitorSnapshotView | null> {
    const res = await deps.invoke('monitor.snapshot', {}, { timeoutMs: 2000 });
    // unavailable 也携带可验证生命周期与闩锁（校准用）——不当作数据，但可收敛状态。
    if (res && typeof res === 'object') {
      const r = res as Record<string, unknown>;
      if (Number(r.schema) === SNAPSHOT_SCHEMA) {
        const lifecycle = acceptLifecycle(r);
        if (lifecycle !== 'stale') {
          reconcileFromView({
            top: typeof r.topActive === 'boolean' ? r.topActive : null,
            fps: typeof r.fpsActive === 'boolean' ? r.fpsActive : null,
          }, doneEpoch);
          if (r.available !== true) return null;
        } else {
          return null;
        }
      }
    }
    const view = normalizeSnapshot(res);
    if (!view) return null;
    reconcileFromView({ top: view.topActive, fps: view.fpsActive }, doneEpoch);
    maybeTakeover();
    return applyStaleFloor(view);
  }

  // 兼容通路：旧 native 无快照命令时按原四文件读取，字段语义不变。
  async function readViaFiles(): Promise<MonitorSnapshotView | null> {
    const readSafe = async (path: string, maxBytes: number): Promise<string | null> => {
      try {
        return await deps.readTextFile(path, maxBytes);
      } catch {
        return null;
      }
    };
    const topRaw = await readSafe(pcDir + '\\topmon.json', 8192);
    const fpsRaw = await readSafe(pcDir + '\\fps-status.json', 8192);
    const hbRaw = await readSafe(pcDir + '\\fps-monitor.hb', 512);
    const hwinfoRaw = await readSafe(pcDir + '\\hwinfo-ok', 64);
    const top = topRaw ? parseTopPayload(topRaw) : null;
    const fps = fpsRaw ? parseFpsPayload(fpsRaw) : null;
    let heartbeatAt: number | null = null;
    if (hbRaw) {
      try {
        const ts = Number((JSON.parse(hbRaw) as { ts?: number }).ts);
        if (Number.isFinite(ts) && ts > 0) heartbeatAt = ts;
      } catch {
        heartbeatAt = null;
      }
    }
    let hwinfoOkAt: number | null = null;
    if (hwinfoRaw !== null) {
      const ts = Number(hwinfoRaw.trim());
      if (Number.isFinite(ts) && ts > 0) hwinfoOkAt = ts;
    }
    if (!top && !fps && heartbeatAt === null && hwinfoOkAt === null) return null;
    const collectedAt = Math.max(top?.ts ?? 0, fps?.ts ?? 0, heartbeatAt ?? 0, hwinfoOkAt ?? 0);
    return applyStaleFloor({
      schema: 0,
      session: null,
      relayEpoch: null,
      generation: 0,
      seq: 0,
      collectedAt,
      heartbeatAt,
      hwinfoOkAt,
      top,
      fps,
      topActive: top !== null,
      fpsActive: fps !== null,
      hwDown: top?.hwDown === true,
      source: 'files',
    });
  }

  async function read(): Promise<MonitorSnapshotView | null> {
    if (inflight) { const view = await inflight; return view ? structuredClone(view) : null; }
    // 199：先捕获"读取版本"再让 pump 可能同步发出命令——本读回只校准版本不晚于它的命令。
    const doneEpoch = intentEpoch;
    // 读取=仅按退避推进未决工作（不绕退避耗预算）；真正的新意图由 acquire/release 立即推进。
    void pump('poll');
    pendingReadCount += 1;
    inflight = (async () => {
      try {
        if (transport === 'files') return await readViaFiles();
        try {
          const view = await readViaSnapshot(doneEpoch);
          if (transport === 'unknown') transport = 'snapshot';
          return view;
        } catch (err) {
          if (transport === 'unknown' && isUnknownCommand(err)) {
            transport = 'files';
            return await readViaFiles();
          }
          // 已确认支持快照但超时/失效（或未知 schema）⇒ 返回不可用，不偷偷回读残留文件。
          return null;
        }
      } catch {
        return null;
      } finally {
        pendingReadCount -= 1;
        inflight = null;
      }
    })();
    const view = await inflight;
    return view ? structuredClone(view) : null;
  }

  // 需求登记：同 owner 每次登记独立 token；旧 disposer 只释放自己的登记。
  function acquire(owner: string, need: { top?: boolean; fps?: boolean }): () => void {
    const token = nextToken++;
    const prev = demands.get(owner);
    const top = need.top === true;
    const fps = need.fps === true;
    const isNewIntent = !prev || prev.top !== top || prev.fps !== fps;
    demands.set(owner, { token, top, fps });
    if (isNewIntent) {
      // 新的需求意图：重置有界预算（重复同意图不重置，避免重启风暴）。
      startBudget = START_RETRY_BUDGET;
      stopBudget = STOP_RETRY_BUDGET;
      startRetryNextAt = 0;
      stopRetryNextAt = 0;
      // 未完成的接管声明随新意图恢复重试（已 done 则不重置）。
      if (takeoverState !== 'done') takeoverBudget = TAKEOVER_RETRY_BUDGET;
    }
    void pump('intent');
    let released = false;
    return () => {
      if (released) return;
      released = true;
      const cur = demands.get(owner);
      if (!cur || cur.token !== token) return; // 旧 disposer：不误停新订阅
      demands.delete(owner);
      // 释放=新意图：若曾发出未确认的 start，也必须推进幂等 stop（可能已真实执行）。
      startBudget = START_RETRY_BUDGET;
      stopBudget = STOP_RETRY_BUDGET;
      startRetryNextAt = 0;
      stopRetryNextAt = 0;
      void pump('intent');
    };
  }

  return {
    read,
    acquire,
    transport: () => transport,
    applied: () => ({ ...confirmed }),
    pending: () => ({ ...pending }),
    desired,
    session: () => expectedSession,
    takeover: () => takeoverState,
    attempts: () => ({ ...attempts }),
    pendingReads: () => pendingReadCount,
  };
}

// ── 生产单例（touch/autofloat 共用同一在途请求）──
const productionHub = createMonitorDataHub({
  invoke: (cmd, args, options) => invoke(cmd, args, options),
  readTextFile: (path, maxBytes) => fs.readTextFile(path, maxBytes),
  now: () => Date.now(),
});

export const readMonitorSnapshot = (): Promise<MonitorSnapshotView | null> => productionHub.read();
export const acquireMonitorDemand = (owner: string, need: { top?: boolean; fps?: boolean }): (() => void) =>
  productionHub.acquire(owner, need);
export const monitorTransport = (): MonitorTransportMode => productionHub.transport();
/** 已确认启动结果（autofloat 等据此区分"已登记"与"守护已启动"）。 */
export const monitorDemandConfirmed = (): MonitorFlagState => productionHub.applied();
/** takeover 状态（诊断：idle/pending/done）。 */
export const monitorTakeoverState = (): MonitorTakeoverState => productionHub.takeover();