import { getPolicyGame } from './gamePolicyTarget';
import { SAVING_RULES, automaticCpuControls, stepContinuousFloat, type ContinuousSideControls, type ContinuousFloatState, type ControlSide } from './performanceControls';
import { displayedFramerateForControl } from './monitorControlFramerate';
import { monitorProofFresh, monitorCpuFresh } from './monitorSnapshotSignals';
// autofloat.ts — 自动浮动优化桥层（CPU主频调度 + TDP节能联动）
//
// 架构（196 后）：数据经统一入口 src/bridge/monitorData.ts（新 native 走内存快照 monitor.snapshot；
// 旧 native 自动回退原四文件通路）。数据语义与历史一致：
//   · 有真实游戏时：native 每秒产出 { ts, fps, fps1, gpu, packagePower, game, pid }（fps-status 语义）；
//     后台仅用 HWiNFO Displayed；Presented/RTSS 的展示回退不进入控制。
//   · 未检测到游戏时：按既有降频 cadence 扫描，只出心跳（fps-monitor.hb 语义），不产出状态数据。
// 前端每秒取数（readStatus，经统一入口）：先看真实游戏数据（<5s 且含 game），再看心跳
// （<15s 区分"守护存活但空闲"与"守护已死"），由常驻单例执行连续控制循环，
// 通过逐条 applyFloatPowerParams 写入 —— 原有全部联动（最小CPU三联动、
// EPP、节流状态等）自动一起生效。
//
// 调度主要看帧率（目标 vs 实际 avg），并叠加 HWiNFO 信号：
//   · 1% Low：低于「目标帧率 × 50%」→ 帧时间抖动 → 立即升压平滑
//   · CPU 使用率：CPU 压力大时暂停 CPU 降频，达到恢复条件时回升
//   · CPU Package Power：仅用于显示和实际功耗监测；TDP 目标按电源方案的 TDP 最大值计算
// 全部信号消费 YMCC 统一快照：后台 Displayed 与 HUD Presented 独立，CPU 压力来自统一 Windows 采样。
// 真实游戏识别由 native 游戏识别阀门完成；HWiNFO 帧率只决定浮动输出是否可用。
//
// 守护生命周期（196）：start/stop 在统一入口同时登记 CPU 压力与 FPS 需求（{top:true,fps:true}），
// 与展示端需求互不干扰；fps-status.json/fps-monitor.hb 契约仅在兼容（文件）通路存在。

import { fs } from './api';
import { invoke } from './ipc';
import { acquireMonitorDemand, monitorDemandConfirmed, monitorSnapshotEpoch, readMonitorSnapshot, setMonitorDataPowerControlDir, type MonitorSnapshotView } from './monitorData';
import { getSettingsGeneration, assertSettingsGeneration, readSettingsSection, saveSettingsSection } from './settingsRepository';
import {
  applyFloatPowerParams,
  readPowerParams,
  type PowerParams,
  detectPowerMode, detectPowerModeReliable,
  readTdp,
  setTdp,
  detectVendor,
  readRtssLimit,
  setRtssLimit,
  clampTdp,
  getTdpHardwareMinimum,
  getTdpHardwareMaximum,
  getTdpExecutionCapability,
  resolveTdpTarget,
  type Vendor,
  ensureTdpDaemon,
  getActiveScheme,
  setActiveScheme,
  PW,
} from './yeman';

let PC_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
// 196：fps-status.json / fps-monitor.hb / hwinfo-ok 的读取已并入统一数据入口
// （monitorData.ts，路径由 setMonitorDataPowerControlDir 同步），本文件不再直接读。
// 持久化配置：记住「帧数目标 + 调度档位 + TDP向下浮动策略」，跨重启保留上次选择
let CONFIG = PC_DIR + '\\autofloat.json';
// 浮动运行标志：native 手柄后台（Start+方向）据此判断浮动是否接管，运行时只转发按键事件
let FLOAT_ACTIVE = PC_DIR + '\\float-active';

export function setAutofloatPowerControlDir(dir: string): void {
  PC_DIR = dir.replace(/\//g, '\\').replace(/\\+$/, '');
  // 统一入口（含旧 native 的四文件回退通路）必须使用同一目录。
  setMonitorDataPowerControlDir(PC_DIR);
  CONFIG = PC_DIR + '\\autofloat.json';
  FLOAT_ACTIVE = PC_DIR + '\\float-active';
}

function safeProfile(p: string): FloatProfile {
  return p === 'none' || p === 'eco' || p === 'bal' || p === 'perf' || p === 'aggressive' ? p : 'bal';
}
function safeTarget(t: number): FpsTarget {
  if (!Number.isFinite(Number(t)) || Number(t) === 0) return 0;
  const v = Math.round(Number(t) / 5) * 5;
  return v >= FPS_TARGET_MIN && v <= FPS_TARGET_MAX ? v : 0;
}

export type TdpFloatStrategy = 'none' | 'small' | 'medium' | 'large' | 'aggressive';
// Compatibility presentation uses the same ratios as the sole controller.
export const TDP_FLOAT_STRATEGIES = Object.fromEntries(
  Object.entries(SAVING_RULES).map(([key, rule]) => [key, { name: rule.label, ratio: rule.ratio }]),
) as Record<TdpFloatStrategy, { name: string; ratio: number }>;

// 编辑性能组合的显示契约：瓦数是主体，执行策略只能作为小字。
// 顺序和文案属于用户确认过的稳定接口，禁止在视图里各自重写。
export const TDP_FLOAT_STRATEGY_ORDER: TdpFloatStrategy[] = [
  'none', 'small', 'medium', 'large', 'aggressive',
];
export const TDP_FLOAT_EXECUTION_LABELS: Record<TdpFloatStrategy, string> = {
  none: '无下降执行',
  small: '小幅浮动执行',
  medium: '中幅浮动执行',
  large: '大幅浮动执行',
  aggressive: '激进浮动执行',
};

function safeTdpStrategy(v: string): TdpFloatStrategy {
  return v === 'none' || v === 'small' || v === 'medium' || v === 'large' || v === 'aggressive' ? v : 'small';
}

// 读取持久化配置（失败回退默认 关闭/平衡/小幅度）
async function readConfig(): Promise<{ target: FpsTarget; profile: FloatProfile; tdpStrategy: TdpFloatStrategy }> {
    const tdp = await readSettingsSection<any>('tdp');
    const j = (tdp.float || {}) as { target?: number; profile?: string; tdpStrategy?: string };
    return {
      target: safeTarget(Number(j.target)),
      profile: safeProfile(String(j.profile)),
      tdpStrategy: safeTdpStrategy(String(j.tdpStrategy)),
    };
}

// 写入当前 帧数目标 + 调度档位 + TDP 策略：同一路径串行原子保存，后提交值不会被旧请求反向覆盖。
let configWriteQueue: Promise<void> = Promise.resolve();
function writeConfig(): Promise<void> {
  const generation = getSettingsGeneration();
  const snapshot = { float: { target: curTarget, profile: curProfile, tdpStrategy: curTdpStrategy } };
  const next = configWriteQueue.then(async () => {
    try { await saveSettingsSection('tdp', snapshot, generation); }
    catch (error) {
      // Restore selection from durable settings only if no newer submission
      // owns it. No rollback is allowed to clobber a later user's selection.
      if (getSettingsGeneration() === generation && curTarget === snapshot.float.target && curProfile === snapshot.float.profile && curTdpStrategy === snapshot.float.tdpStrategy) {
        try {
          const durable = await readConfig();
          curTarget = durable.target;
          curProfile = durable.profile;
          curTdpStrategy = durable.tdpStrategy;
          notify();
        } catch { /* A read failure must not manufacture persisted defaults. */ }
      }
      throw error;
    }
  });
  configWriteQueue = next.catch(() => {});
  return next;
}

// ── 档位：CPU 性能压制档（左→右压制递增，与 TDP 向下策略同方向：小/中/大/激进）──
// 频率范围整体反转：小幅度=压制最轻（主频上限最高），激进幅度=压制最狠（主频上限最低）
export type FloatProfile = 'none' | 'eco' | 'bal' | 'perf' | 'aggressive';
// Legacy profile names are accepted only while reading old settings.
// Runtime decisions use ContinuousSideControls + stepContinuousFloat.

// ── 帧数目标：连续值，30–300、5 帧步进（0 = 不锁帧/关闭浮动）──
export const FPS_TARGET_MIN = 15;
// 性能调度帧数目标/RTSS 锁帧/浮动目标统一上限 300FPS（2026-08-04 起由 200 放开；0 表示不锁帧）。
export const FPS_TARGET_MAX = 300;
export const FPS_TARGET_STEP = 5;
export type FpsTarget = number;
export function clampFpsTarget(t: number): number {
  if (Number(t) === 0) return 0; // 0 = 不锁帧
  const v = Math.round(Number(t) / FPS_TARGET_STEP) * FPS_TARGET_STEP;
  return Math.max(FPS_TARGET_MIN, Math.min(FPS_TARGET_MAX, v));
}

function floatTdpMinimum(): number { return getTdpHardwareMinimum(); }
function boundFloatTdp(watts: number): number {
  return Math.max(floatTdpMinimum(), Math.min(getTdpHardwareMaximum(), Math.round(watts)));
}
export function getTdpTarget(tdpMax: number, strategy: TdpFloatStrategy): number {
  if (!Number.isFinite(tdpMax) || tdpMax <= 0) return 0;
  // Bound the baseline BEFORE computing ratios: 55/120W intent must not
  // create a 41W floor or recovery target on a 35W admitted device.
  // Display/CPU consumers stay pure even when a descriptor is not ready.
  const maximum = Math.max(getTdpHardwareMinimum(), Math.min(getTdpHardwareMaximum(), Math.round(tdpMax)));
  if (strategy === 'none') return maximum;
  const p = TDP_FLOAT_STRATEGIES[strategy];
  return Math.max(getTdpHardwareMinimum(), Math.floor(maximum * (1 - p.ratio)));
}

async function applyTdpLimit(next: number): Promise<number> {
  const runId = floatRunId, targetRevision = tdpTargetRevision;
  if (continuousOwner && !await continuousOwner.current()) return tdpLimit;
  const capability = await getTdpExecutionCapability(); // CPU bounds are brand-independent.
  if (runId !== floatRunId || targetRevision !== tdpTargetRevision) return tdpLimit;
  const value = Math.max(floatTdpMinimum(), resolveTdpTarget(Math.round(next), capability).effectiveW);
  if (value <= 0 || value === tdpLimit) return tdpLimit;
  const vendor = capability.cpuFamily === 'intel-arc-g3-extreme' ? 'intel' :
    tdpVendor !== 'unknown' ? tdpVendor : await detectVendor().catch(() => 'unknown' as Vendor);
  if (vendor === 'unknown') return tdpLimit;
  // 与顶部 TDP 滑块共用 setTdp 的实际下发路径；save:false 保证只改硬件临时值，不改 TDP最大值配置。
  // 常驻 daemon 方案已生效：setTdp → ensureTdpDaemon（心跳新鲜直接复用）→ 写命令文件（毫秒级），
  // 不再每几秒 CreateProcess(YeManTdpCtl.exe)，根治 IDC_APPSTARTING 转圈。
  if (continuousOwner && !await continuousOwner.current()) return tdpLimit;
  let accepted: number | null = null;
  const applied = await setTdp(tdpMode, value, { apply: true, save: false, vendor, source: 'autofloat', onAccepted: w => { accepted = w; } });
  if (runId !== floatRunId || targetRevision !== tdpTargetRevision) return tdpLimit;
  if (!applied || accepted === null || !Number.isFinite(accepted) || accepted < getTdpHardwareMinimum() || accepted > value) { tdpLimit = 0; notify(); return 0; } // unknown is never an old value masquerading as applied
  tdpVendor = vendor;
  tdpLimit = accepted;
  if (accepted < value) {
    // This operation's OEM-accepted budget, NOT a CPU/model max and NOT a
    // permanent ceiling. A new explicit target retries the requested intent.
    tdpMax = Math.min(tdpMax, accepted);
    tdpOriginal = Math.min(tdpOriginal, accepted);
    if (continuousOwner) continuousOwner.bank.tdp = Math.min(continuousOwner.bank.tdp, accepted);
  }
  notify();
  return accepted;
}

export interface FloatStatus {
  ts: number;
  fps: number; // HWiNFO Framerate Displayed (avg) —— 后台调度系数
  fps1: number; // HWiNFO Framerate Displayed (1% low)；缺失为 -1
  gpu: number | null; // HWiNFO GPU 3D 负载最大值%（多 GPU 取最大，比 Win32_Perf 准）
  packagePower: number; // HWiNFO CPU Package Power，单位 W；0=未读取到
  game: string | null;
  pid: number;
  observedAt?: number;
  fps1ObservedAt?: number;
  processCreated?: string;
  idle?: boolean; // 仅守护心跳，无真实游戏帧率数据
}

// 登记统一采集需求；唯一 native 采集线程负责取数及上游恢复。
let monitorStartInFlight: Promise<void> | null = null;
// 控制同时需要 CPU 压力与 FPS；统一入口合并需求，与顶部/内页的展示生命周期互不干扰。
let fpsDemandRelease: (() => void) | null = null;
export async function startMonitor(): Promise<void> {
  if (monitorStartInFlight) return monitorStartInFlight;
  monitorStartInFlight = (async () => {
    // Continuous control consumes Windows CPU pressure as well as FPS. Own both
    // channels instead of relying on the visibility of the topbar/HUD.
    if (!fpsDemandRelease) fpsDemandRelease = acquireMonitorDemand('autofloat', { top: true, fps: true, fileSystem: false });
    // 196/197：只有 native 回执确认才算已启动；仅登记需求不得置为已拉起
    //（未确认时 tick 的失联分支按既有冷却/突发上限继续重试，不造成重启风暴）。
    monitorLaunched = monitorDemandConfirmed().fps;
  })();
  try {
    await monitorStartInFlight;
  } finally {
    monitorStartInFlight = null;
  }
}

// 释放本控制器的 CPU/FPS 需求；其他消费者仍可保留对应通道。
export async function stopMonitor(): Promise<void> {
  if (fpsDemandRelease) {
    fpsDemandRelease();
    fpsDemandRelease = null;
  }
  monitorLaunched = false; // 显式停止 → 允许后续按需重拉
}

// Read the independent Displayed receipt from the single YMCC top snapshot.
// Missing/expired Displayed means control waits; Presented/RTSS HUD values do
// not substitute. The original heartbeat still distinguishes idle from dead.
export async function readStatus(): Promise<FloatStatus | null> {
  return statusFromSnapshot(await readMonitorSnapshot());
}

function statusFromSnapshot(view: MonitorSnapshotView | null): FloatStatus | null {
  if (!view) return null;
  const now = Date.now(), displayed = displayedFramerateForControl(view, now), top = view.top;
  // Presentation fallback never substitutes a control sample, even if it has
  // the same generic fps label. The original Displayed producer proof owns age.
  if (displayed && top) {
    return { ts: displayed.observedAt, observedAt: displayed.observedAt,
      fps1ObservedAt: displayed.fps1 === null ? undefined : displayed.observedAt,
      processCreated: displayed.processCreated, fps: displayed.fps, fps1: displayed.fps1 ?? -1,
      gpu: top.gpuSelectionPresent === true && typeof top.gpuUsagePct === 'number' && Number.isFinite(top.gpuUsagePct) && top.gpuUsagePct >= 0 && top.gpuUsagePct <= 100 ? top.gpuUsagePct : null,
      packagePower: top.cpuPowerValid !== false && typeof top.tdpW === 'number' && Number.isFinite(top.tdpW) && top.tdpW >= 0 ? top.tdpW : -1,
      game: displayed.game, pid: displayed.pid };
  }
  if (monitorProofFresh(view.heartbeatAt, now, 15000)) {
    return {
      ts: view.heartbeatAt,
      fps: 0,
      fps1: 0,
      gpu: 0,
      packagePower: 0,
      game: null,
      pid: 0,
      idle: true,
    };
  }
  return null;
}

// ── HWiNFO 状态观察 ──
// 恢复动作只由 native 数据读取链路在故障边沿触发一次；前端只观察健康标记，
// 禁止额外枚举进程、启动脚本或高频轮询恢复，避免重新干扰 UI 渲染。
function snapshotHwinfoHealthy(view: MonitorSnapshotView | null): boolean {
  return view !== null && !view.hwDown && monitorProofFresh(view.hwinfoOkAt, Date.now(), 6000);
}
async function hwinfoStatus(): Promise<boolean> {
  return snapshotHwinfoHealthy(await readMonitorSnapshot());
}

const HWINFO_RECOVERY_GRACE_MS = 30000;

interface TickMonitorObservation {
  view: MonitorSnapshotView | null;
  run: number; directory: string; epoch: number;
  startedAt: number; wallStartedAt: number;
}
async function ensureHwiNfo(observed?: TickMonitorObservation): Promise<boolean> {
  // Keep health notifications asynchronous to the control tick, as before.
  // Validate context after yielding and consume the observation synchronously.
  if (observed) await Promise.resolve();
  const age = observed ? performance.now() - observed.startedAt : -1;
  const reusable = observed && age >= 0 && age < 1000 && Date.now() >= observed.wallStartedAt &&
    observed.run === floatRunId && observed.directory === PC_DIR && observed.epoch === monitorSnapshotEpoch();
  const healthy = reusable ? snapshotHwinfoHealthy(observed.view) : await hwinfoStatus();
  if (healthy) {
    hwinfoDown = false;
    hwinfoErr = false;
    hwinfoNoExe = false;
    hwinfoRecoveryAttempted = false;
    return true;
  }

  hwinfoDown = true;
  hwinfoNoExe = false;
  if (!hwinfoRecoveryAttempted) {
    hwinfoRecoveryAttempted = true;
    hwinfoRecoveryTs = Date.now();
    hwinfoErr = false;
  } else if (Date.now() - hwinfoRecoveryTs >= HWINFO_RECOVERY_GRACE_MS) {
    hwinfoErr = true;
  }
  notify();
  return false;
}

/* ── ContinuousPerformance runtime status types ───────────────────── */
export interface CtlState {
  freq: number;
  aggr: number;
}
export type CtlAction = 'down-freq' | 'down-tdp' | 'up-freq' | 'up-tdp' | 'hold';

/* ── 单例控制器（模块级，路由切页不中断）───────────────────────
 * 连续性能面板负责 UI；1 秒控制循环挂在模块单例上，
 * 切到其它页面仍持续调节。写入始终走逐条 applyFloatPowerParams —— 原有联动
 * （最小CPU三联动跟积极性等）全部一起生效。
 */
export interface FloatInfo {
  enabled: boolean;
  target: FpsTarget;
  profile: FloatProfile;
  tdpStrategy: TdpFloatStrategy;
  tdpMax: number;
  tdpRequestedMax?: number; // portable saved intent; tdpMax is the admitted baseline
  tdpTarget: number;
  tdpApplied: number;
  status: FloatStatus | null; // 守护最新状态（null=无数据/守护未就绪）
  state: CtlState; // 当前控制量（主频上限 + 积极性）
  lastAction: CtlAction | 'idle' | 'wait';
  tdpState: CtlAction | 'idle' | 'wait'; // TDP 这一排专用的状态标注（降TDP/回调TDP/稳定）
  hwinfoDown: boolean; // HWiNFO 共享内存不可用（未开启/传感器缺失/内存共享未启用）
  hwinfoErr: boolean;  // 一次完整自动修复尝试（10 秒轮询）结束仍不可用 → UI 才弹提示
  hwinfoNoExe: boolean; // 补救时 HWiNFO 进程不在（未装/未启动）→ 两套逻辑之「无 HW」
}

let timer: ReturnType<typeof setInterval> | null = null;
let ctl: CtlState = { freq: 0, aggr: 100 };
let snapshot: PowerParams | null = null; // 开启前的面板参数，供锁定/显示使用
let curTarget: FpsTarget = 0;
let curProfile: FloatProfile = 'bal';
let curTdpStrategy: TdpFloatStrategy = 'small';
let tdpMax = 0; // admitted effective baseline; never an out-of-range saved intent
let tdpRequestedMax = 0;
let tdpTargetRevision = 0;
let tdpLimit = 0; // 当前实际下发到 YeManTdpCtl 的临时目标上限，单位 W
let tdpOriginal = 0; // admitted recovery/idle baseline; disableFloat never restores TDP
let tdpMode: 'ac' | 'dc' = 'ac';
let tdpVendor: Vendor = 'unknown';
let lastAction: FloatInfo['lastAction'] = 'idle';
let lastTdpAction: FloatInfo['lastAction'] = 'idle';
let lastStatus: FloatStatus | null = null;
let lastFloatActiveTs = 0; // 浮动标志刷新时间戳（每 5 秒刷新，供 native 按 30s 新鲜度判定）
let monitorLaunched = false; // 守护已被前端拉起（未显式停止）→ 失联时先不重拉，避免瞬读抖动把健康守护误杀重启
const RESTART_BURST_MAX = 6; // 连续 6 次重拉仍无状态 → 冷却拉长到 30s，避免守护持续故障时 5s 一次的进程风暴
let starting = false;    // 启用流程进行中标志（防并发双启动守护 + 双控制循环）
let stopping = false;    // disableFloat 在 enableFloat 启动期间被调用 → 等启动完成后自动关闭
let stoppingUnlockRtss = false;
let tickRunning = false; // 控制循环重入锁：tick 内含多个 await（读状态/电源模式/下发硬件），
                         // 防止 setInterval 重叠触发多个 tick 交错争抢 UI 线程消息泵与进程池
                         // （鼠标旁转圈 IDC_APPSTARTING 的根因之一）。
let ctlApplyQueue: Promise<void> = Promise.resolve(); // tick 与外部热切档共用 CPU 参数写入队列
let hwinfoDown = false;                  // HWiNFO 共享内存不可用
let hwinfoErr = false;                   // 完整修复尝试（10 秒轮询）结束仍不可用
let hwinfoRecoveryAttempted = false;     // 已观察本次 YMCC 原生恢复（避免重复提示）
let hwinfoRecoveryTs = 0;                // 上次观察到恢复等待状态的时间戳（30s 前端宽限）
let hwinfoNoExe = false;                 // 补救时 HWiNFO 进程不在（未装/未启动）→ 两套逻辑之「无 HW」
let hwinfoPollTs = 0;                    // 上次 HWiNFO 健康轮询时间戳（2s 间隔）
let tdpModeCheckTs = 0;                  // 上次 AC/DC 电源模式探测时间戳（1s 间隔，插拔电源后快速跟随）
// Reconcile AC/DC promptly; the native monitor already samples once per
// second while float control is active, so this does not add another clock.
const TDP_MODE_CHECK_MS = 1000;
const listeners = new Set<(info: FloatInfo) => void>();

function notify() {
  const info = getFloatInfo();
  listeners.forEach((cb) => cb(info));
}

export function getFloatInfo(): FloatInfo {
  return {
    enabled: timer !== null || starting,
    target: curTarget,
    profile: curProfile,
    tdpStrategy: curTdpStrategy,
    tdpMax,
    tdpRequestedMax,
    tdpTarget: getTdpTarget(tdpMax, curTdpStrategy), // 策略目标（一次性下限，不随已下发值漂移）
    tdpApplied: tdpLimit, // 当前实际下发的临时值（逐步逼近中）
    status: lastStatus,
    state: { ...ctl },
    lastAction,
    tdpState: lastTdpAction,
    hwinfoDown,
    hwinfoErr,
    hwinfoNoExe,
  };
}

// 开启浮动优化前的「用户面板参数」快照（锁定功能要锁的就是这套，而不是浮动中的动态值）
export function getFloatSnapshot(): PowerParams | null {
  return snapshot ? { ...snapshot } : null;
}

// 解锁后恢复联动：立即把浮动优化当前控制量写回电源方案（未开启则空操作）
export async function reapplyFloat(): Promise<void> {
  if (timer !== null) await applyCtl().catch(() => {});
}

export function onFloatUpdate(cb: (info: FloatInfo) => void): () => void {
  listeners.add(cb);
  return () => listeners.delete(cb);
}

// 把当前控制量写入 AC/DC 电源方案，并延迟激活生效
// CPU 调度写入节流：控制循环每 1 秒决策，但硬件写入（applyCtl → 2~cmd + powercfg）
// 合并到每 CTL_FLUSH_MS 落地一次。避免每秒密集起子进程持续占满消息泵/线程池，
// 否则 OS 在 C++ UI 线程判定"持续忙" → 鼠标旁 IDC_APPSTARTING 转圈
// （手动调 CPU/TDP 只写一次不转，正是此差异）。仅 HWiNFO 提供真帧率时才进控制循环，
// 故「只有浮动 + HWiNFO 开才转圈」的根因在此。
let ctlDirty = false;
let ctlFlushTimer: ReturnType<typeof setTimeout> | null = null;
// 浮动值变化后最多等待 200ms 合并提交；只有 ctlDirty 时才会写入，
// 因此不是固定 200ms 轮询，也不会在数值未变化时反复激活电源策略。
const CTL_FLUSH_MS = 200;
let ctlApplyPending: { params: PowerParams; runId: number } | null = null;
let ctlApplyRunning = false;
let floatRunId = 0;
let snapshotLoad: Promise<PowerParams | null> | null = null;
let peripheralInit: Promise<void> | null = null;

function markCtlDirty(): void {
  ctlDirty = true;
  if (ctlFlushTimer !== null) window.clearTimeout(ctlFlushTimer);
  ctlFlushTimer = window.setTimeout(() => {
    ctlFlushTimer = null;
    if (!ctlDirty || (!starting && timer === null)) return;
    ctlDirty = false;
    void applyCtl().catch(() => {});
  }, CTL_FLUSH_MS);
}

// powercfg /setXvalueindex 写入的是方案数据；CPU 浮动首次接管后必须重新
// 激活一次当前的野蛮方案，Windows 才会把三类 CPU 参数装载到活动策略。
// 只在启用时做一次，不放进 1 秒控制循环，避免高频广播造成卡顿或重入。
async function activateFloatPolicyOnce(): Promise<void> {
  const owner = continuousOwner, run = floatRunId;
  const active = await getActiveScheme().catch(() => '');
  if (!owner || continuousOwner !== owner || run !== floatRunId || !await owner.current() || active.toLowerCase() !== PW.YEMAN.toLowerCase()) return;
  await setActiveScheme(PW.YEMAN).catch(() => {});
}

// powercfg /setXvalueindex 只修改野蛮方案的保存值；Windows 对活动策略
// 仍可能继续使用旧的已加载值。手动 CPU 页面每次提交后都会 /setactive，
// 浮动动态提交也必须复用同一条刷新链路，否则会出现“注册表变化、实际频率不变”。
// 这里保留当前方案保护：如果用户/其它程序已经切走，不强制切回野蛮电源。
async function reactivateFloatPolicyAfterWrite(): Promise<void> {
  const owner = continuousOwner, run = floatRunId;
  const active = await getActiveScheme().catch(() => '');
  if (!owner || continuousOwner !== owner || run !== floatRunId || !await owner.current() || active.toLowerCase() !== PW.YEMAN.toLowerCase()) return;
  await setActiveScheme(PW.YEMAN).catch(() => {});
}

interface ContinuousFloatOwner {
  bank: ContinuousSideControls; side: ControlSide; retryAfter?: number; current: () => Promise<boolean>;
  applyCore: (watts: number) => Promise<void>; state: ContinuousFloatState; targetPid: number; initialTdp?: { requested: number; accepted: number };
}
let continuousOwner: ContinuousFloatOwner | null = null;

export function holdContinuousFloatAfterCoreChange(): void {
  if (continuousOwner) continuousOwner.state = { ...continuousOwner.state, trial: null, stable: 0, recoverUntil: Date.now() + 10000 };
}
export function updateContinuousFloatCoreRules(bank: ContinuousSideControls): void {
  if (!continuousOwner) return;
  Object.assign(continuousOwner.bank.auto, { coreSplit: bank.auto.coreSplit, coreLow: bank.auto.coreLow, coreHigh: bank.auto.coreHigh });
}
async function tickContinuous(owner: ContinuousFloatOwner, sample: Parameters<typeof stepContinuousFloat>[2]): Promise<void> {
  const run = floatRunId;
  if (continuousOwner !== owner || !await owner.current() || sample.now < (owner.retryAfter ?? 0)) return;
  if (tdpLimit <= 0) {
    const accepted = await applyTdpLimit(owner.bank.tdp).catch(() => { tdpLimit = 0; return 0; });
    if (run !== floatRunId || continuousOwner !== owner || !await owner.current()) return;
    if (accepted <= 0) { owner.retryAfter = sample.now + 3000; return; }
    owner.state = { ...owner.state, watts: accepted, trial: null, stable: 0, recoverUntil: sample.now + 5000 };
    return;
  }
  const next = stepContinuousFloat(owner.bank, owner.state, sample), prior = owner.state;
  if (run !== floatRunId || continuousOwner !== owner || !await owner.current()) return;
  if (next.watts !== prior.watts) {
    const accepted = await applyTdpLimit(next.watts).catch(() => { tdpLimit = 0; return 0; });
    if (accepted <= 0) { owner.retryAfter = sample.now + 3000; return; }
    next.watts = accepted;
  }
  if (run !== floatRunId || continuousOwner !== owner || !await owner.current()) return;
  if (next.frequencyMHz !== prior.frequencyMHz || next.aggressiveness !== prior.aggressiveness) {
    ctl = { freq: next.frequencyMHz, aggr: next.aggressiveness };
    if (!await applyCtl()) { ctl = { freq: prior.frequencyMHz, aggr: prior.aggressiveness }; owner.retryAfter = sample.now + 3000; return; }
  }
  if (run !== floatRunId || continuousOwner !== owner || !await owner.current()) return;
  owner.state = next;
  if (tdpLimit > 0 && !owner.state.trial && sample.fps !== null && owner.state.observedAt !== prior.observedAt) await owner.applyCore(next.watts);
  if (run !== floatRunId || continuousOwner !== owner) return;
  lastTdpAction = next.watts < prior.watts ? 'down-tdp' : next.watts > prior.watts ? 'up-tdp' : 'hold';
  const cpuBefore = prior.frequencyMHz || Infinity, cpuAfter = next.frequencyMHz || Infinity;
  lastAction = cpuAfter < cpuBefore || next.aggressiveness < prior.aggressiveness ? 'down-freq' :
    cpuAfter > cpuBefore || next.aggressiveness > prior.aggressiveness ? 'up-freq' : 'hold';
  notify();
}

export async function enableContinuousFloat(bank: ContinuousSideControls, side: ControlSide, target: number,
  current: () => Promise<boolean>, applyCore: (watts: number) => Promise<void>,
  initialTdp?: { requested: number; accepted: number }): Promise<void> {
  await disableFloat();
  if (!await current()) return;
  const cpu = automaticCpuControls(bank);
  const owner: ContinuousFloatOwner = { bank: structuredClone(bank), side, current, applyCore, initialTdp, targetPid: getPolicyGame()?.pid || 0,
    state: { watts: bank.tdp, frequencyMHz: cpu.maxMHz, aggressiveness: cpu.aggressiveness, stable: 0, lastDown: Date.now(), recoverUntil: 0 } };
  await startFloatRuntime(target, owner, bank.auto.saving, false);
}

let ctlLastApplied = false;
async function applyCtl(): Promise<boolean> {
  // Every active float owner is continuous. There is no legacy profile branch
  // that can write a second CPU/TDP policy.
  const owner = continuousOwner;
  if (!owner || !await owner.current()) return false;
  const cpu = automaticCpuControls(owner.bank);
  const side = owner.side;
  const params: PowerParams = {
    acFreq: ctl.freq,
    dcFreq: ctl.freq,
    acTurbo: cpu.turbo,
    dcTurbo: cpu.turbo,
    acAggr: ctl.aggr,
    dcAggr: ctl.aggr,
    acMinState: cpu.minState,
    dcMinState: cpu.minState,
    sides: [side],
    restoreMaxState: false,
  };

  const runId = floatRunId;
  // Keep only the newest target. A slow powercfg call must not create a stale
  // backlog, but a request is never silently discarded before it is run.
  ctlApplyPending = { params, runId };
  if (ctlApplyRunning) {
    await ctlApplyQueue;
    return ctlLastApplied;
  }

  ctlApplyRunning = true;
  const run = (async () => {
    try {
      while (ctlApplyPending) {
        const next = ctlApplyPending;
        ctlApplyPending = null;
        if (next.runId !== floatRunId || (!starting && timer === null)) continue;
        // Every CPU value is submitted directly. The manual CPU path reloads
        // the active policy after its write; floating must do the same after
        // each coalesced dynamic write, otherwise only the saved values move.
        const currentSide = await detectPowerModeReliable();
        if (!currentSide || !next.params.sides?.includes(currentSide) || (continuousOwner && !await continuousOwner.current())) continue;
        ctlLastApplied = false;
        const applied = await applyFloatPowerParams(next.params).catch(() => false);
        ctlLastApplied = applied;
        if (applied && next.runId === floatRunId && continuousOwner === owner && await owner.current()) await reactivateFloatPolicyAfterWrite();
      }
    } finally {
      ctlApplyRunning = false;
    }
  })();
  ctlApplyQueue = run.catch(() => {});
  await run;
  return ctlLastApplied;
}

let tickWork: Promise<void> | null = null;
function tick(): Promise<void> {
  if (tickWork) return tickWork;
  const work = tickOnce(); tickWork = work;
  void work.finally(() => { if (tickWork === work) tickWork = null; }).catch(() => {});
  return work;
}
async function tickOnce(): Promise<void> {
  if (tickRunning) return;
  tickRunning = true;
  try {
    if (Date.now() - lastFloatActiveTs > 5000) {
      lastFloatActiveTs = Date.now();
      await fs.writeTextFile(FLOAT_ACTIVE, '1').catch(() => {});
    }
    const readRun = floatRunId, readDir = PC_DIR, readEpoch = monitorSnapshotEpoch();
    const readAt = performance.now(), readWallAt = Date.now();
    const view = await readMonitorSnapshot();
    if (readRun !== floatRunId || readDir !== PC_DIR) return;
    const st = statusFromSnapshot(view);
    lastStatus = st;
    if (Date.now() - hwinfoPollTs > 2000) {
      hwinfoPollTs = Date.now();
      void ensureHwiNfo({ view, run: readRun, directory: readDir, epoch: readEpoch,
        startedAt: readAt, wallStartedAt: readWallAt }).then(() => notify()).catch(() => {});
    }
    if (!continuousOwner) {
      lastAction = 'idle'; lastTdpAction = 'idle'; notify(); return;
    }
    if (!st) {
      await tickContinuous(continuousOwner, {now: Date.now(), fps: null, low: null, target: curTarget, minimum: getTdpHardwareMinimum(), cpuBusy: null}).catch(() => {});
      lastAction = 'wait'; lastTdpAction = 'wait'; notify(); return;
    }
    const top = view?.top, now = Date.now(), game = getPolicyGame();
    const correctCreation = !!st.processCreated && st.processCreated === game?.processCreated;
    // A foreign/unconfirmed game is not a measured zero-FPS game.
    if (!game || !st.game || st.pid !== continuousOwner.targetPid || !correctCreation) return;
    const cpuBusy = monitorCpuFresh(top, now, false, true) ? top!.cpuUsage : null;
    const hardwareFresh = !!top && !view?.hwDown && !top.hwDown && monitorProofFresh(top.ts, now, 6000);
    // Only the confirmed selected adapter; legacy multi-GPU maxima cannot establish ownership.
    const gpu = hardwareFresh && top!.gpuSelectionPresent === true && top!.gpuName &&
      typeof top!.gpuUsagePct === 'number' && Number.isFinite(top!.gpuUsagePct) && top!.gpuUsagePct >= 0 && top!.gpuUsagePct <= 100 ? top!.gpuUsagePct : null;
    await tickContinuous(continuousOwner, { now, fps: st.fps, low: st.fps1 > 0 ? st.fps1 : null, target: curTarget,
      minimum: getTdpHardwareMinimum(), cpuBusy, gpuBusy: gpu, observedAt: st.observedAt ?? st.ts,
      lowObservedAt: st.fps1ObservedAt ?? st.ts, gpuObservedAt: gpu === null ? undefined : top!.ts,
      cpuClockMHz: hardwareFresh && top!.cpuClockValid !== false && top!.freqMhz > 0 ? top!.freqMhz : null }).catch(() => {});
  } finally {
    tickRunning = false;
  }
}

// 帧数目标是固定值：开启（含程序启动自动启用）时立即把 RTSS 锁帧应用到目标值。
// 无条件应用——即使当前限制为 0（用户原本未锁帧）也启用；关闭时恢复启用前原值。
let floatRtssLinked = true; // legacy direct-float callers; performance/standalone FPS explicitly detach it
export function setFloatRtssLinked(linked:boolean):void {
  floatRtssLinked=linked;
  if(!linked){cancelPendingRtssSync();rtssOriginalReady=false;}
}
let rtssOriginal = 0; // 启用浮动前 RTSS 锁帧值（可为 0），关闭时恢复
let rtssOriginalReady = false;
let rtssSyncQueue: Promise<void> = Promise.resolve();
async function syncRtssLimit(target: number): Promise<void> {
  if(!floatRtssLinked)return;
  const next = rtssSyncQueue.then(async () => {
    try {
      if(floatRtssLinked)await setRtssLimit(target,()=>floatRtssLinked);
    } catch {
      /* 忽略：RTSS 不可用不影响其它流程 */
    }
  });
  rtssSyncQueue = next.catch(() => {});
  await next;
}

// 目标切换采用短尾随防抖：UI 立即更新，连续切档只重载最后一个 RTSS 目标。
const RTSS_SYNC_SETTLE_MS = 120;
let rtssSyncTimer: number | null = null;
export function cancelPendingRtssSync(): void {
  if (rtssSyncTimer !== null) {
    window.clearTimeout(rtssSyncTimer);
    rtssSyncTimer = null;
  }
}

function scheduleRtssSync(target: number): void {
  if(!floatRtssLinked)return;
  if (rtssSyncTimer !== null) window.clearTimeout(rtssSyncTimer);
  rtssSyncTimer = window.setTimeout(() => {
    rtssSyncTimer = null;
    void syncRtssLimit(target).catch(() => {});
  }, RTSS_SYNC_SETTLE_MS);
}

// 外围能力初始化不参与 CPU 浮动的首个写入。
// TDP、RTSS、native 监控或 HWiNFO 任一异常，都只能影响各自功能，不能阻止
// CPU 三类参数先通过 powercfg 写入并激活。
async function initializeFloatPeripherals(runId: number, target: FpsTarget): Promise<void> {
  try {
    const mode = await detectPowerMode().catch(() => tdpMode);
    if (runId !== floatRunId) return;
    if (mode === 'ac' || mode === 'dc') tdpMode = mode;

    const targetRevision = tdpTargetRevision;
    const owner = continuousOwner;
    const receipt = owner?.initialTdp;
    const requested = receipt?.requested ?? await readTdp(tdpMode).catch(() => null) ?? 0;
    const capability = await getTdpExecutionCapability().catch(() => null);
    if (runId !== floatRunId) return;
    if (capability?.ready && targetRevision === tdpTargetRevision) {
      const target = requested > 0 ? resolveTdpTarget(requested, capability) : null;
      tdpRequestedMax = target?.intentW ?? 0;
      tdpOriginal = receipt?.accepted ?? target?.effectiveW ?? 0;
      tdpMax = tdpOriginal;
      if (continuousOwner) continuousOwner.bank.tdp = tdpOriginal;
      // Native OEM applied values come from successful writes, not saved config.
      // A direct enableFloat caller also gets a first admitted write below.
      tdpLimit = receipt?.accepted ?? (capability.selected ? 0 : tdpOriginal);
      tdpVendor = capability.cpuFamily === 'intel-arc-g3-extreme' ? 'intel' : await detectVendor().catch(() => 'unknown' as Vendor);
    }
    if (runId !== floatRunId) return;

    // 先保存原值；只有保存成功后才允许浮动改写 RTSS，关闭时才有安全恢复依据。
    const original = floatRtssLinked ? await readRtssLimit().catch(() => null) : null;
    if (runId !== floatRunId) return;
    if (original !== null) {
      rtssOriginal = original;
      rtssOriginalReady = true;
      await syncRtssLimit(target).catch(() => {});
    }

    await startMonitor().catch(() => {});
    if (runId !== floatRunId) {
      // 取消浮动不清理外围资源；监控保持运行，由后续逻辑接管。
      return;
    }
    // Await only inside the background initializer. enableFloat itself has
    // already returned after CPU powercfg writes are complete.
    await ensureTdpDaemon().catch(() => {});
    if (runId !== floatRunId) return;
    if (capability?.selected && tdpOriginal > 0 && tdpLimit === 0) await applyTdpLimit(tdpOriginal).catch(() => {});
    if (runId !== floatRunId) return;
    void ensureHwiNfo().then(() => notify()).catch(() => {});
    scheduleRtssSync(curTarget);
  } catch {
    // 外围初始化是 best-effort；CPU 参数已经在进入此函数前完成写入。
  }
}

// 唯一浮动运行入口。旧页面的 enableFloat 只保留为参数适配器，
// 实际监控、TDP、CPU、恢复和 AC/DC 门控全部走这个连续运行时。
async function startFloatRuntime(target: FpsTarget, owner: ContinuousFloatOwner, tdpStrategy: TdpFloatStrategy, persistLegacy: boolean): Promise<void> {
  continuousOwner = owner;
  curTarget = target;
  curProfile = 'none';
  curTdpStrategy = tdpStrategy;
  if (persistLegacy) await writeConfig();
  if (target <= 0) {
    if (timer !== null || starting) await disableFloat({ unlockRtss: true });
    else { await syncRtssLimit(0).catch(() => {}); notify(); }
    return;
  }
  if (timer !== null || starting) return;
  starting = true;
  const runId = ++floatRunId;
  try {
    rtssOriginalReady = false;
    rtssOriginal = 0;
    snapshot = null;
    snapshotLoad = readPowerParams().catch(() => null);
    void snapshotLoad.then((value) => {
      if (runId === floatRunId && (starting || timer !== null)) snapshot = value;
    }).catch(() => {});
    tdpMode = owner.side;
    const cpu = automaticCpuControls(owner.bank);
    ctl = { freq: cpu.maxMHz, aggr: cpu.aggressiveness };
    owner.state = { ...owner.state, frequencyMHz: cpu.maxMHz, aggressiveness: cpu.aggressiveness };
    ++tdpTargetRevision;
    tdpRequestedMax = owner.initialTdp?.requested ?? 0;
    tdpOriginal = owner.initialTdp?.accepted ?? 0;
    tdpMax = tdpOriginal;
    tdpLimit = tdpOriginal;
    tdpVendor = 'unknown';
    lastAction = 'wait';
    hwinfoRecoveryAttempted = false;
    hwinfoDown = false;
    hwinfoErr = false;
    await fs.writeTextFile(FLOAT_ACTIVE, '1').catch(() => {});
    if (runId !== floatRunId || continuousOwner !== owner || !await owner.current()) return;
    await applyCtl();
    if (runId !== floatRunId || continuousOwner !== owner || !await owner.current()) return;
    await activateFloatPolicyOnce();
    if (runId !== floatRunId || continuousOwner !== owner || !await owner.current()) return;
    timer = setInterval(() => void tick(), 1000);
    notify();
    const init = initializeFloatPeripherals(runId, target);
    peripheralInit = init;
    void init.finally(() => { if (peripheralInit === init) peripheralInit = null; });
  } finally {
    starting = false;
    if (stopping) {
      stopping = false;
      const unlockRtss = stoppingUnlockRtss;
      stoppingUnlockRtss = false;
      void Promise.resolve().then(() => disableFloat({ unlockRtss }));
    }
  }
}

function legacyContinuousBank(profile: FloatProfile, strategy: TdpFloatStrategy, tdp: number, side: ControlSide): ContinuousSideControls {
  const map = { none: 0, eco: 20, bal: 45, perf: 65, aggressive: 85 } as const;
  const bias = map[profile];
  return {
    mode: 'auto', tdp, tdpCeiling: Math.max(1, tdp),
    auto: { bias, saving: strategy === 'none' ? 'none' : strategy, coreSplit: 0, coreLow: 'bigFirst', coreHigh: 'bigFirst' },
    manual: { frequencyMHz: 4000, unlimited: false, aggressiveness: 10 + Math.round((100 - bias) * .9), turbo: true, coreMode: 'bigFirst' },
  };
}

export async function enableFloat(target: FpsTarget, profile: FloatProfile, tdpStrategy: TdpFloatStrategy = curTdpStrategy, owner: ContinuousFloatOwner | null = null): Promise<void> {
  const side = owner?.side ?? await detectPowerModeReliable() ?? 'ac';
  const tdp = owner?.bank.tdp || await readTdp(side).catch(() => 0) || 0;
  const bank = owner?.bank ?? legacyContinuousBank(profile, tdpStrategy, tdp, side);
  const legacyOwner: ContinuousFloatOwner = owner ?? {
    bank, side, targetPid: 0, current: async () => true, applyCore: async () => {},
    state: { watts: tdp, frequencyMHz: 0, aggressiveness: 100, stable: 0, lastDown: Date.now(), recoverUntil: 0 },
  };
  await startFloatRuntime(target, legacyOwner, tdpStrategy, true);
}

// 关闭：停止 CPU 浮动控制；外围浮动资源保持不动，CPU 参数交给 CPU 挡位基线。
// ⚠️ 严禁在此处把 curTarget/curProfile 归零或 writeConfig({target:0})——
// 帧数目标 + 调度档位 是用户「偏好选择」，必须跨开关/跨电源态持久记忆在 autofloat.json。
// 归零会导致拔插电源 / 切到「从不」时把记住的 45/aggressive 洗成 0，下次启用回退 60（用户实测"没记忆"）。
export async function disableFloat(options: { unlockRtss?: boolean } = {}): Promise<void> {
  continuousOwner = null;
  ++floatRunId; // Invalidate initialization and in-flight observations before any await.
  // enableFloat 正在初始化（starting=true、timer 尚未创建）：标记 stopping，
  // 等 enableFloat 完成后自动执行关闭，避免在此刻强行停止导致守护/daemon/RTSS 状态交叉。
  if (starting) {
    stopping = true;
    stoppingUnlockRtss = stoppingUnlockRtss || options.unlockRtss === true;
    await ctlApplyQueue.catch(() => {});
    if (tickWork) await tickWork.catch(() => {});
    return;
  }
  ctlApplyPending = null;
  if (ctlFlushTimer !== null) {
    window.clearTimeout(ctlFlushTimer);
    ctlFlushTimer = null;
  }
  ctlDirty = false;
  // 等正在执行的旧 CPU 写入结束，避免取消后旧浮动命令晚到，覆盖随后
  // 由 CPU 挡位提交的新值。
  await ctlApplyQueue.catch(() => {});
  // A TDP/core request issued by the old tick must settle before switching Windows plans.
  if (tickWork) await tickWork.catch(() => {});
  // 结束监控；若外围初始化正准备启动监控，先等待它完成再停止，避免竞态重启。
  const pendingMonitorStart = monitorStartInFlight;
  if (pendingMonitorStart) await pendingMonitorStart.catch(() => {});
  await stopMonitor().catch(() => {});
  snapshotLoad = null;
  if (timer !== null) { clearInterval(timer); timer = null; }
  // 结束所有浮动控制：取消尚未执行的 RTSS 尾随更新并移除活动标志。
  // 不恢复 RTSS/TDP/CPU 快照，不重制任何当前参数；TDP daemon 保持常驻。
  if (rtssSyncTimer !== null) { window.clearTimeout(rtssSyncTimer); rtssSyncTimer = null; }
  if (options.unlockRtss) await syncRtssLimit(0).catch(() => {});
  await fs.remove(FLOAT_ACTIVE).catch(() => {});
  // 注意：curTarget / curProfile 保持不变，不再写 config（选择未变，也不能洗掉记忆）
  lastAction = 'idle';
  lastStatus = null;
  hwinfoDown = false;
  hwinfoErr = false;
  hwinfoNoExe = false;
  // 取消浮动不恢复快照，也不额外改写 CPU；TDP daemon 继续常驻但不再接收浮动控制。
  // CPU 挡位/性能组合是唯一基线；取消后由调用方已经提交的 CPU 挡位接管。
  snapshot = null;
  notify();
}

// 性能调度热切档：三项设置作为一个内存事务更新，只落盘一次；
// 返回前保证 autofloat.json 已提交，避免页面报告成功时监控仍读到旧档位。
export async function applyFloatSettings(
  target: FpsTarget,
  profile: FloatProfile,
  strategy: TdpFloatStrategy,
): Promise<void> {
  curTarget = clampFpsTarget(target);
  curProfile = profile;
  curTdpStrategy = safeTdpStrategy(strategy);
  await writeConfig();
  // CPU 档位/浮动策略仅在循环运行时即时生效（需 timer），RTSS 锁帧写盘后立即排程重载；
  // 将 scheduleRtssSync 放到 if 之外，确保页面保存/重启恢复后 FPS 也能真正写进 RTSS。
  if (timer !== null || starting) {
    if (profile === 'none') {
      // 无浮动只停止 CPU 浮动控制，不恢复快照、不额外写 CPU。
      // CPU 挡位由性能调度入口负责提交。
    } else if (timer !== null) {
      await applyCtl();
    }
  }
  scheduleRtssSync(curTarget);
  notify();
}

// 切换档位：立即钳到新范围
export async function setFloatProfile(profile: FloatProfile): Promise<void> {
  curProfile = profile;
  await writeConfig();
  if (timer !== null || starting) {
    if (profile === 'none') {
      // 无压制：只取消浮动控制；CPU 挡位写入由调度入口负责。
    } else {
      if (timer !== null) await applyCtl().catch(() => {});
    }
  }
  notify();
}

// 已运行时增量切换目标：UI/内存立即更新，RTSS 采用尾随防抖，避免连续切档反复重载。
export function isContinuousFloat(): boolean { return continuousOwner !== null; }
export async function setFloatTarget(target: FpsTarget): Promise<void> {
  if (continuousOwner) {
    curTarget = clampFpsTarget(target);
    continuousOwner.state.stable = 0;
    continuousOwner.state.recoverUntil = Date.now() + 4000;
    if (curTarget <= 0) { continuousOwner.state.trial = null; }
    notify(); return;
  }
  curTarget = clampFpsTarget(target);
  await writeConfig();
  if (curTarget <= 0) {
    if (timer !== null || starting) await disableFloat({ unlockRtss: true });
    else await syncRtssLimit(0).catch(() => {});
  } else if (timer !== null) {
    scheduleRtssSync(curTarget);
  }
  notify();
}

export async function setTdpFloatStrategy(strategy: TdpFloatStrategy): Promise<void> {
  curTdpStrategy = safeTdpStrategy(strategy);
  await writeConfig();
  notify();
}

// 手柄后台（Start+上/下）浮动运行时：改写程序记录的 TDP 最大值，
// 自动浮动按新基准实时重算目标并 notify 刷新状态行；不直接下发硬件（由控制循环按新目标继续调节）。
export async function adjustFloatTdpMax(delta: number): Promise<number> {
  if (timer === null) return 0;
  const capability = await getTdpExecutionCapability();
  const next = resolveTdpTarget(clampTdp(Math.round(tdpMax) + delta), capability).effectiveW;
  if (next === tdpMax) return tdpMax;
  // Do not poison float state when the configuration save fails.
  await setTdp(tdpMode, next, { apply: false, save: true, source: 'hotkey' });
  ++tdpTargetRevision;
  tdpRequestedMax = next;
  tdpMax = next;
  tdpOriginal = next;
  // 已下发临时值若超过新最大值则钳制到新上限；等待真实下发后再更新 tdpLimit。
  if (tdpLimit !== tdpMax) {
    await applyTdpLimit(tdpMax).catch(() => {});
  }
  notify();
  return tdpMax;
}

// 顶部 TDP 最大值滑块在浮动运行期间变更时调用：直接更新内存基准，
// 不再依赖 tick() 每轮读取 tdp.txt（避免每秒一次 fs 读与守护写争用 → 鼠标转圈）。
// 写盘由调用方 setTdp 完成；此处仅同步 autofloat 内部基准。
export async function notifyTdpMaxChanged(watts: number): Promise<void> {
  if (timer === null && !starting) return; // 浮动未运行/未启动则无需维护内存基准
  const runId = floatRunId;
  const capability = await getTdpExecutionCapability();
  if (runId !== floatRunId || (timer === null && !starting)) return;
  const target = resolveTdpTarget(watts, capability);
  ++tdpTargetRevision;
  tdpRequestedMax = target.intentW;
  const w = target.effectiveW;
  tdpMax = w;
  tdpOriginal = w;
  if (continuousOwner) continuousOwner.bank.tdp = w;
  if (tdpLimit !== tdpMax && tdpMax > 0) {
    // 上下调都立即把当前临时值收敛到新基准，避免 tdpLimit 与 tdpMax 长时间分叉。
    await applyTdpLimit(tdpMax);
  }
  notify();
}

// 手柄后台（Start+左/右）在浮动运行时：以 ±5 帧步进调节帧数目标（30–90 连续，与 UI 滑块一致），
// 并立即把 RTSS 锁帧应用到新目标；不再是循环固定档位。
export async function adjustFloatTarget(dir: 1 | -1): Promise<number> {
  if (timer === null) return 0;
  const base = curTarget >= FPS_TARGET_MIN && curTarget <= FPS_TARGET_MAX ? curTarget : 60;
  let next = Math.round((base + dir * FPS_TARGET_STEP) / FPS_TARGET_STEP) * FPS_TARGET_STEP;
  next = Math.max(FPS_TARGET_MIN, Math.min(FPS_TARGET_MAX, next));
  if (next === curTarget) return curTarget;
  curTarget = next;
  await writeConfig();
  notify();
  scheduleRtssSync(next); // 尾随 2 秒防抖，避免手柄连发时每步重载 RTSS 卡顿
  return next;
}

// 恢复上次记忆的 帧数目标 + 调度档位（仅载入选择，不自动拉守护）；
// 只供旧配置迁移读取；正式接管由 performanceSchedule 的连续 bank 决定。
export async function loadFloatConfig(): Promise<{ target: FpsTarget; profile: FloatProfile; tdpStrategy: TdpFloatStrategy }> {
  const c = await readConfig();
  curTarget = c.target;
  curProfile = c.profile;
  curTdpStrategy = c.tdpStrategy;
  return c;
}
