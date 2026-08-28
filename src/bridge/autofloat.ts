// autofloat.ts — 自动浮动优化桥层（CPU主频调度 + TDP节能联动）
//
// 架构：native monitor daemon（与顶部监控共用一个线程）
//   · 有真实游戏时：每 1 秒把 { ts, fps, fps1, game, pid } 写入 fps-status.json；
//     帧率来自 HWiNFO 共享内存的 "Framerate Presented (avg)" / "(1%)"（不再依赖 RTSS，RTSS 易崩）。
//   · 未检测到游戏时：每 10 秒扫一次，且不在记录任何数据（只写心跳 fps-monitor.hb，不写状态文件）。
// 前端每 1 秒轮询：先看 fps-status.json（有游戏时的真数据），再看 fps-monitor.hb
// （区分"守护存活但空闲"与"守护已死"），并在 CpuView 里跑控制循环（先积极性后主频），
// 通过逐条 applyFloatPowerParams 写入 —— 原有全部联动（最小CPU三联动、
// EPP、节流状态等）自动一起生效。
//
// 调度主要看帧率（目标 vs 实际 avg），并叠加 HWiNFO 信号：
//   · 1% Low：低于「目标帧率 × 50%」→ 帧时间抖动 → 立即升压平滑
//   · GPU 3D 负载：限制 TDP 向下调节速度，GPU 越忙越慢降，避免瓶颈场景过快降功耗
//   · CPU Package Power：仅用于显示和实际功耗监测；TDP 目标按电源方案的 TDP 最大值计算
// 以上数据全部来自 HWiNFO 共享内存，不再用 RTSS / Win32_Perf（易崩/不准）。
// 真实游戏识别由 native 游戏识别阀门完成；HWiNFO 帧率只决定浮动输出是否可用。
//
// 守护生命周期：
//   start/stop = native IPC 切换 FPS 输出，保留 fps-status.json/fps-monitor.hb 契约

import { fs } from './api';
import { invoke } from './ipc';
import { readSettingsSection, saveSettingsSection } from './settingsRepository';
import {
  applyFloatPowerParams,
  readPowerParams,
  type PowerParams,
  detectPowerMode,
  readTdp,
  setTdp,
  detectVendor,
  readRtssLimit,
  setRtssLimit,
  clampTdp,
  type Vendor,
  ensureTdpDaemon,
  getActiveScheme,
  setActiveScheme,
  PW,
} from './yeman';

let PC_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
let STATUS = PC_DIR + '\\fps-status.json';
let HB = PC_DIR + '\\fps-monitor.hb';
// HWiNFO 健康标记（守护每轮写入，前端据此判断 HWiNFO 共享内存是否可用）
let HWINFO_OK = PC_DIR + '\\hwinfo-ok';
// 持久化配置：记住「帧数目标 + 调度档位 + TDP向下浮动策略」，跨重启保留上次选择
let CONFIG = PC_DIR + '\\autofloat.json';
// 浮动运行标志：native 手柄后台（Start+方向）据此判断浮动是否接管，运行时只转发按键事件
let FLOAT_ACTIVE = PC_DIR + '\\float-active';

export function setAutofloatPowerControlDir(dir: string): void {
  PC_DIR = dir.replace(/\//g, '\\').replace(/\\+$/, '');
  STATUS = PC_DIR + '\\fps-status.json';
  HB = PC_DIR + '\\fps-monitor.hb';
  HWINFO_OK = PC_DIR + '\\hwinfo-ok';
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
export const TDP_FLOAT_STRATEGIES: Record<TdpFloatStrategy, { name: string; minDrop: number; ratio: number }> = {
  none: { name: '无下降', minDrop: 0, ratio: 0 },
  small: { name: '小幅度', minDrop: 5, ratio: 0.2 },
  medium: { name: '中幅度', minDrop: 7, ratio: 0.3 },
  large: { name: '大幅度', minDrop: 10, ratio: 0.4 },
  aggressive: { name: '激进幅度', minDrop: 15, ratio: 0.5 },
};

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
  try {
    const tdp = await readSettingsSection<any>('tdp');
    const j = (tdp.float || {}) as { target?: number; profile?: string; tdpStrategy?: string };
    return {
      target: safeTarget(Number(j.target)),
      profile: safeProfile(String(j.profile)),
      tdpStrategy: safeTdpStrategy(String(j.tdpStrategy)),
    };
  } catch {
    return { target: 0, profile: 'bal', tdpStrategy: 'small' };
  }
}

// 写入当前 帧数目标 + 调度档位 + TDP 策略：同一路径串行原子保存，后提交值不会被旧请求反向覆盖。
let configWriteQueue: Promise<void> = Promise.resolve();
function writeConfig(): Promise<void> {
  const next = configWriteQueue.then(() => saveSettingsSection('tdp', {
    float: { target: curTarget, profile: curProfile, tdpStrategy: curTdpStrategy },
  }));
  configWriteQueue = next.catch(() => {});
  return next;
}

// ── 档位：CPU 性能压制档（左→右压制递增，与 TDP 向下策略同方向：小/中/大/激进）──
// 频率范围整体反转：小幅度=压制最轻（主频上限最高），激进幅度=压制最狠（主频上限最低）
export type FloatProfile = 'none' | 'eco' | 'bal' | 'perf' | 'aggressive';
type FloatProfileSpec = { name: string; min: number; max: number; minAggr?: number; noAdjust?: boolean };
export const FLOAT_PROFILES: Record<FloatProfile, FloatProfileSpec> = {
  none: { name: '无压制', min: 0, max: 0, minAggr: 100, noAdjust: true },
  eco: { name: '小幅度', min: 3500, max: 4500, minAggr: 80 },   // 精睿使用 3.5-4.5GHz，压制最小
  bal: { name: '中幅度', min: 2500, max: 3500 },
  perf: { name: '大幅度', min: 2000, max: 3500 },
  aggressive: { name: '激进幅度', min: 1500, max: 3000 },        // 压制最狠（性能最低）
};
function floatProfileSpec(profile: FloatProfile): FloatProfileSpec {
  return FLOAT_PROFILES[profile];
}

type FloatCpuOverrides = {
  minState: number;
  turbo: boolean;
};

function floatCpuOverrides(profile: FloatProfile): FloatCpuOverrides | null {
  switch (profile) {
    case 'eco': return { minState: 30, turbo: true };
    case 'bal': return { minState: 20, turbo: true };
    case 'perf': return { minState: 10, turbo: true };
    case 'aggressive': return { minState: 0, turbo: false };
    default: return null;
  }
}

// ── 帧数目标：连续值，30–300、5 帧步进（0 = 不锁帧/关闭浮动）──
export const FPS_TARGET_MIN = 30;
// 性能调度帧数目标/RTSS 锁帧/浮动目标统一上限 300FPS（2026-08-04 起由 200 放开；0 表示不锁帧）。
export const FPS_TARGET_MAX = 300;
export const FPS_TARGET_STEP = 5;
export type FpsTarget = number;
export function clampFpsTarget(t: number): number {
  if (Number(t) === 0) return 0; // 0 = 不锁帧
  const v = Math.round(Number(t) / FPS_TARGET_STEP) * FPS_TARGET_STEP;
  return Math.max(FPS_TARGET_MIN, Math.min(FPS_TARGET_MAX, v));
}

const AUTO_TDP_MIN = 5;
// 固定基础步进（原值）：降档每次 -1W、回调每次 +5W
const TDP_DOWN_BASE = 1;
const TDP_RECOVER_BASE = 5;
// TDP 回调冷却：与「CPU 连续达标降档判定 DOWN_STABLE_N」解耦，避免调整降档稳定性时
// 意外拖慢 TDP 回调速度（2026-08-05 拆分语义劫持）。
const TDP_RECOVER_COOLDOWN_MS = 1000;
// 动态加速量：以【当前 TDP 实际值 tdpLimit】为基准 * 0.03 取整（舍小数），结果 < 1 时抛弃
// （加速量记为 0，只走基础步进），叠加在基础步进之上；提升与下降共用同一加速量。
// ⚠️ 必须用当前实际值 tdpLimit 而非 tdpMax：200W 机器压到 30W 时若按 tdpMax 算，加速量仍 = 6，
// 回调一步就 +11W、降档一步 -7W，低瓦数跳变过大出 bug。按实际值则低瓦数加速量自动归 0。
// 例（按当前实际值）：200W→6(降7/回11)、100W→3(降4/回8)、50W→1.5→1(降2/回6)、30W→0.9→0(抛弃: 降1/回5)。
function tdpStepW(): number {
  const v = Math.floor(tdpLimit * 0.03);
  return v < 1 ? 0 : v;
}

export function getTdpTarget(tdpMax: number, strategy: TdpFloatStrategy): number {
  if (!Number.isFinite(tdpMax) || tdpMax <= 0) return 0;
  if (strategy === 'none') return Math.floor(tdpMax);
  const p = TDP_FLOAT_STRATEGIES[strategy];
  const drop = Math.max(p.minDrop, Math.ceil(tdpMax * p.ratio));
  return Math.max(AUTO_TDP_MIN, Math.floor(tdpMax - drop));
}

function gpuTdpDownIntervalMs(gpu: number): number {
  if (gpu > 90) return 3000;
  if (gpu > 70) return 2000;
  return 1000;
}

async function applyTdpLimit(next: number): Promise<number> {
  const value = Math.max(AUTO_TDP_MIN, Math.round(next));
  if (value <= 0 || value === tdpLimit) return tdpLimit;
  const vendor = tdpVendor !== 'unknown' ? tdpVendor : await detectVendor().catch(() => 'unknown' as Vendor);
  if (vendor === 'unknown') return tdpLimit;
  // 与顶部 TDP 滑块共用 setTdp 的实际下发路径；save:false 保证只改硬件临时值，不改 TDP最大值配置。
  // 常驻 daemon 方案已生效：setTdp → ensureTdpDaemon（心跳新鲜直接复用）→ 写命令文件（毫秒级），
  // 不再每几秒 CreateProcess(YeManTdpCtl.exe)，根治 IDC_APPSTARTING 转圈。
  const applied = await setTdp(tdpMode, value, { apply: true, save: false, vendor });
  if (!applied) return tdpLimit;
  tdpVendor = vendor;
  tdpLimit = value;
  notify();
  return value;
}

function shouldRecoverTdp(st: FloatStatus): boolean {
  if (curTarget <= 0) return false;
  const fpsLow = st.fps > 0 && st.fps < curTarget * 0.8;
  const lowOnePercent = st.fps1 > 0 && st.fps1 < curTarget * 0.5;
  return fpsLow || lowOnePercent;
}

async function recoverTdp(): Promise<boolean> {
  if (tdpLimit <= 0 || tdpOriginal <= tdpLimit || Date.now() - lastTdpRecoverTs < TDP_RECOVER_COOLDOWN_MS) return false;
  const next = Math.min(tdpOriginal, tdpLimit + TDP_RECOVER_BASE + tdpStepW());
  const applied = await applyTdpLimit(next);
  lastTdpRecoverTs = Date.now();
  // 用实际下发值比较：applyTdpLimit 内部会 Math.round + 钳 AUTO_TDP_MIN，
  // 原 tdpLimit===next 在浮点/钳制时误报失败（2026-08-05 修复）。
  return applied === Math.max(AUTO_TDP_MIN, Math.round(next));
}

async function lowerTdp(st: FloatStatus): Promise<boolean> {
  if (tdpMax <= 0 || tdpLimit <= AUTO_TDP_MIN) return false;
  if (Date.now() - lastTdpDownTs < gpuTdpDownIntervalMs(st.gpu)) return false;
  const target = getTdpTarget(tdpMax, curTdpStrategy);
  if (target <= 0 || target >= tdpLimit) return false;
  // 按 GPU 占用间隔、以（基础 1W + 动态加速量：当前实际 TDP值*0.03 取整，<1 抛弃）逐步逼近策略目标（目标 = TDP最大值按策略计算的下限）
  const next = Math.max(target, tdpLimit - (TDP_DOWN_BASE + tdpStepW()));
  const applied = await applyTdpLimit(next);
  lastTdpDownTs = Date.now();
  return applied === Math.max(AUTO_TDP_MIN, Math.round(next));
}

export interface FloatStatus {
  ts: number;
  fps: number; // HWiNFO Framerate Presented (avg) —— 主要调度系数
  fps1: number; // HWiNFO Framerate Presented (1%) —— 1% Low
  gpu: number; // HWiNFO GPU 3D 负载最大值%（多 GPU 取最大，比 Win32_Perf 准）
  packagePower: number; // HWiNFO CPU Package Power，单位 W；0=未读取到
  game: string | null;
  pid: number;
  idle?: boolean; // 仅守护心跳，无真实游戏帧率数据
}

// 启动守护（shell.hidden 隐藏窗口直接拉起 ps1，无 VBS 中转，避免 LOLBin 拦截）
let monitorStartInFlight: Promise<void> | null = null;
export async function startMonitor(): Promise<void> {
  if (monitorStartInFlight) return monitorStartInFlight;
  // 清掉旧停止标志，避免刚启动就被自己停掉
  monitorStartInFlight = (async () => {
    await invoke('monitor.start', { fps: true });
    monitorLaunched = true; // 标记已拉起；tick 失联重拉前先看此标志，避免反复重启抖动
  })();
  try {
    await monitorStartInFlight;
  } finally {
    monitorStartInFlight = null;
  }
}

// 停止守护（写停止标志，守护 1 秒内自清理退出）
export async function stopMonitor(): Promise<void> {
  await invoke('monitor.stop', { fps: true });
  monitorLaunched = false; // 显式停止 → 允许后续按需重拉
}

// 读取最新状态；逻辑：
//   1) fps-status.json 存在且 <5s 且含真实游戏 → 返回真数据（游戏态）
//   2) 否则看心跳 fps-monitor.hb：若 <15s 新鲜 → 守护存活但空闲（未检测到游戏）
//      → 返回合成空闲状态（game:null），前端据此待机、不再重拉守护
//   3) 心跳也缺失/过期 → 守护已死 → null（前端会重拉）
export async function readStatus(): Promise<FloatStatus | null> {
  try {
    const txt = await fs.readTextFile(STATUS);
    const raw = JSON.parse(txt) as Partial<FloatStatus>;
    const st: FloatStatus = {
      ts: Number(raw.ts) || 0,
      fps: Number(raw.fps) || 0,
      fps1: Number(raw.fps1) || 0,
      gpu: Number(raw.gpu) || 0,
      packagePower: Number(raw.packagePower) || 0,
      game: typeof raw.game === 'string' ? raw.game : null,
      pid: Number(raw.pid) || 0,
    };
    if (typeof st.ts === 'number' && Date.now() - st.ts <= 5000 && st.game) return st;
  } catch {
    /* 无状态文件 / JSON 损坏 */
  }
  try {
    const hbTxt = await fs.readTextFile(HB);
    const hb = JSON.parse(hbTxt) as { ts?: number };
    if (typeof hb.ts === 'number' && Date.now() - hb.ts < 15000) {
      return { ts: hb.ts, fps: 0, fps1: 0, gpu: 0, packagePower: 0, game: null, pid: 0, idle: true };
    }
  } catch {
    /* 无心跳 */
  }
  return null;
}

// ── HWiNFO 状态观察 ──
// 恢复动作只由 native 数据读取链路在故障边沿触发一次；前端只观察健康标记，
// 禁止额外枚举进程、启动脚本或高频轮询恢复，避免重新干扰 UI 渲染。
async function hwinfoStatus(): Promise<boolean> {
  try {
    const txt = await fs.readTextFile(HWINFO_OK);
    const ts = Number(txt.trim());
    return Number.isFinite(ts) && Date.now() - ts < 6000;
  } catch {
    return false;
  }
}

const HWINFO_RECOVERY_GRACE_MS = 30000;

async function ensureHwiNfo(): Promise<boolean> {
  if (await hwinfoStatus()) {
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

/* ── 控制状态机（纯函数，便于测试）─────────────────────────────
 * 「栈式」性能轴，先积极性后主频（降序），升序反向弹栈：
 *   降（帧率达标，连续 N 秒，速度已加快）：
 *     ① 积极性 100 → 0（步长 -20）    ← 积极性下限已放开到 0
 *     ② 积极性到底后：主频 max → min（步长 -300）
 *   升（帧率不达标，立即，幅度加大）：
 *     ① 主频 → max（步长 +500，大幅度回血）
 *     ② 积极性：按掉帧幅度直接拉升（≤5 帧 +30 / ≥10 帧 拉满 100）
 * 说明：调度只看帧率（目标 vs 实际 avg），不再参考 CPU/GPU 占用。
 */
export interface CtlState {
  freq: number; // 当前主频上限 MHz
  aggr: number; // 当前调度积极性 0-100
}

export const AGGR_FLOOR = 0;   // 积极性下限放开到 0（最小CPU三联动可彻底归零）
const AGGR_STEP_DOWN = 20;    // 降积极性步长加大（原 5），降低更跟手
const AGGR_STEP_UP = 10;      // 预留（升由掉帧幅度驱动，见 tick）
const FREQ_STEP_DOWN = 300;   // 降频步长加大（原 100）
const FREQ_STEP_UP = 500;     // 升频步长 +0.5GHz（原 300），大幅度回血

// 按当前档位把积极性钳到合法区间。
// 带 minAggr 的档位（eco=80）表示积极性「绝对下限」：即使 GPU 占用触发压低也不得低于该值
// （性能优先档位，牺牲一点节能保住帧率）；无 minAggr 的档位（bal/perf/aggressive）按 GPU
// 占用受上限钳制（GPU 越忙 → 上限越低，省电优先）。（2026-08-05 修正注释与实现对齐）
function clampAggr(aggr: number, profile: FloatProfile): number {
  const p = floatProfileSpec(profile);
  if (p.noAdjust) return 100;
  const floor = p.minAggr ?? AGGR_FLOOR;
  let v = Math.max(floor, Math.min(100, aggr));
  // 带 minAggr（绝对下限）的档位不受 GPU 占用压低；其余档位仍受 GPU 上限钳制
  if (p.minAggr === undefined) v = Math.min(v, gpuCap);
  return v;
}

// 1% Low 卡顿判定：1%Low 低于「目标帧率 × LOW_WARN_RATIO」→ 帧时间抖动明显，触发 CPU 升压（平滑帧时间）
// 例：目标=60 但 1%Low=25(<30) → 判定卡顿升压；1%Low>=30 视为流畅不额外升压。
// 不再用 avg×比例（旧逻辑会让稳定 60/1%Low≈31 的场面误判卡顿、积极性顶满）。
export const LOW_WARN_RATIO = 0.5;

// GPU 3D 负载(%) → CPU 调度积极性上限（HWiNFO 多 GPU 取最大值，比 Win32_Perf 准）：
// GPU 越忙(越可能是瓶颈) → 积极性上限越低（免得 CPU 白费电也救不了帧率）；频率上限不受 GPU 约束
export function gpuAggrCap(gpu: number): number {
  if (gpu >= 99) return 50;
  if (gpu >= 95) return 60;
  if (gpu >= 80) return 70;
  return 100;
}

export type CtlAction = 'down-aggr' | 'down-freq' | 'down-tdp' | 'up-freq' | 'up-aggr' | 'up-tdp' | 'hold';

export function stepDown(s: CtlState, profile: FloatProfile): { next: CtlState; action: CtlAction } {
  const p = floatProfileSpec(profile);
  if (p.noAdjust) return { next: s, action: 'hold' };
  const floor = p.minAggr ?? AGGR_FLOOR;
  if (s.aggr > floor) {
    return { next: { freq: s.freq, aggr: clampAggr(s.aggr - AGGR_STEP_DOWN, profile) }, action: 'down-aggr' };
  }
  if (s.freq > p.min) {
    return { next: { freq: Math.max(p.min, s.freq - FREQ_STEP_DOWN), aggr: clampAggr(s.aggr, profile) }, action: 'down-freq' };
  }
  return { next: s, action: 'hold' };
}

export function stepUp(s: CtlState, profile: FloatProfile): { next: CtlState; action: CtlAction } {
  const p = floatProfileSpec(profile);
  if (p.noAdjust) return { next: s, action: 'hold' };
  if (s.freq < p.max) {
    return { next: { freq: Math.min(p.max, s.freq + FREQ_STEP_UP), aggr: s.aggr }, action: 'up-freq' };
  }
  if (s.aggr < 100) {
    return { next: { freq: s.freq, aggr: Math.min(100, s.aggr + AGGR_STEP_UP) }, action: 'up-aggr' };
  }
  return { next: s, action: 'hold' };
}

// 把任意状态钳到档位范围内（切换档位时用）
export function clampToProfile(s: CtlState, profile: FloatProfile): CtlState {
  const p = floatProfileSpec(profile);
  if (p.noAdjust) return s;
  return { freq: Math.max(p.min, Math.min(p.max, s.freq)), aggr: clampAggr(s.aggr, profile) };
}

// 判定阈值（HWiNFO 帧率有 ±1~2 抖动，留滞回带防振荡）
export const DOWN_STABLE_N = 3; // 连续 3 秒达标才降一档（缓慢调低）

// 掉帧容差：只有实际帧率明显低于目标（掉了 dropMargin 帧以上）才判为「掉帧」并升压。
// 否则（含 58 vs 60 这种接近达标、或远超目标的流畅场景）一律判「达标」走降档，
// 避免把 HWiNFO 抖动/临界帧率误判成掉帧、把积极性顶在 100 下不来
// （用户实测 FPS58/low51 卡在 100：58<60 永远进不了下降分支）。
export function dropMargin(target: number): number {
  return target > 0 ? Math.max(3, Math.round(target * 0.08)) : 0; // 目标 60 → 容差 5 帧
}

export function judge(fps: number, target: number, stutter: boolean): 'up' | 'down' | 'hold' {
  if (stutter) return 'up';                             // 1% Low 卡顿 → 立即升压平滑
  if (fps < target - dropMargin(target)) return 'up';   // 明显掉帧（跌破容差）才升
  return 'down';                                        // 达标/接近达标 → 降（积极性趋向 0）
}

/* ── 单例控制器（模块级，路由切页不中断）───────────────────────
 * CpuView 只负责 UI 展示与开关；真正的 1 秒控制循环挂在模块单例上，
 * 切到其它页面仍持续调节。写入始终走逐条 applyFloatPowerParams —— 原有联动
 * （最小CPU三联动跟积极性等）全部一起生效。
 */
export interface FloatInfo {
  enabled: boolean;
  target: FpsTarget;
  profile: FloatProfile;
  tdpStrategy: TdpFloatStrategy;
  tdpMax: number;
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
let downCount = 0;
let lastTdpDownTs = 0;
let lastTdpRecoverTs = 0;
let tdpMax = 0; // 当前电源方案配置的 TDP 最大值，单位 W；策略目标固定以此为基准
let tdpLimit = 0; // 当前实际下发到 YeManTdpCtl 的临时目标上限，单位 W
let tdpOriginal = 0; // 当前电源模式启用前读到的 TDP 配置，关闭时恢复
let tdpMode: 'ac' | 'dc' = 'ac';
let tdpVendor: Vendor = 'unknown';
let lastAction: FloatInfo['lastAction'] = 'idle';
let lastTdpAction: FloatInfo['lastAction'] = 'idle';
let lastStatus: FloatStatus | null = null;
let lastFloatActiveTs = 0; // 浮动标志刷新时间戳（每 5 秒刷新，供 native 按 30s 新鲜度判定）
let restartCooldown = 0; // 守护失联重拉冷却
let monitorLaunched = false; // 守护已被前端拉起（未显式停止）→ 失联时先不重拉，避免瞬读抖动把健康守护误杀重启
let deadStreak = 0; // 连续失联轮数，达阈值才强制重拉（区分"瞬读抖动"与"守护真死"）
let restartBurst = 0; // 连续重拉计数：每次重拉后递增、读到有效状态归零；超过阈值进入退避冷却
const RESTART_BURST_MAX = 6; // 连续 6 次重拉仍无状态 → 冷却拉长到 30s，避免守护持续故障时 5s 一次的进程风暴
let starting = false;    // 启用流程进行中标志（防并发双启动守护 + 双控制循环）
let stopping = false;    // disableFloat 在 enableFloat 启动期间被调用 → 等启动完成后自动关闭
let stoppingUnlockRtss = false;
let tickRunning = false; // 控制循环重入锁：tick 内含多个 await（读状态/电源模式/下发硬件），
                         // 防止 setInterval 重叠触发多个 tick 交错争抢 UI 线程消息泵与进程池
                         // （鼠标旁转圈 IDC_APPSTARTING 的根因之一）。
let ctlApplyQueue: Promise<void> = Promise.resolve(); // tick 与外部热切档共用 CPU 参数写入队列
let gpuCap = 100;       // 当前 GPU 上限钳制值（每轮按 st.gpu 刷新；守护失联→100）
let hwinfoDown = false;                  // HWiNFO 共享内存不可用
let hwinfoErr = false;                   // 完整修复尝试（10 秒轮询）结束仍不可用
let hwinfoRecoveryAttempted = false;     // 已尝试运行 YeManHWiNFO.bat 修复（避免重复弹窗）
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

// 根据 cpu_auto_enable.json 的 mode + 当前电源状态，自动启用/禁用浮动优化。
// 调用点：CpuView 下拉变更、App.vue 启动、AC/DC 插拔事件。
const AUTO_ENABLE_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\cpu_auto_enable.json';
export async function applyCpuAutoEnable(modeOverride?: string): Promise<void> {
  let mode = modeOverride ?? 'never';
  if (modeOverride === undefined) {
    try {
      const cpu = await readSettingsSection<any>('cpu');
      mode = cpu.autoEnable?.mode ?? 'never';
    } catch { /* 文件不存在或损坏 → 按 never 处理 */ }
  }

  // 始终从 config 恢复「帧数目标 + 调度档位」供 UI 回显（仅读盘、不写盘、不拉守护）。
  // 这是「记忆」的关键：即便当前电源态不需要启用浮动，下拉里也应正确高亮上次选中的 30/45/60/90 与挡位。
  const c = await readConfig();
  curTarget = c.target;
  curProfile = c.profile;
  curTdpStrategy = c.tdpStrategy;

  // 0 是明确的“不锁帧”，不是一个可供浮动控制器比较的无限大目标。
  // 因此无论自动启用下拉选择什么，都不能启动 CPU/TDP 浮动。
  if (c.target <= 0) {
    if (timer !== null || starting) await disableFloat({ unlockRtss: true });
    else await syncRtssLimit(0).catch(() => {});
    notify();
    return;
  }

  if (mode === 'never') {
    if (timer !== null) await disableFloat();
    return;
  }

  const pm = await detectPowerMode();
  const shouldEnable = mode === 'always' || (mode === 'ac' && pm === 'ac') || (mode === 'dc' && pm === 'dc');
  if (shouldEnable) {
    if (timer === null) {
      const target = c.target > 0 ? c.target : 60;
      await enableFloat(target, c.profile, c.tdpStrategy);
    }
  } else {
    if (timer !== null) await disableFloat();
  }
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
  const active = await getActiveScheme().catch(() => '');
  if (active.toLowerCase() !== PW.YEMAN.toLowerCase()) return;
  await setActiveScheme(PW.YEMAN).catch(() => {});
}

// powercfg /setXvalueindex 只修改野蛮方案的保存值；Windows 对活动策略
// 仍可能继续使用旧的已加载值。手动 CPU 页面每次提交后都会 /setactive，
// 浮动动态提交也必须复用同一条刷新链路，否则会出现“注册表变化、实际频率不变”。
// 这里保留当前方案保护：如果用户/其它程序已经切走，不强制切回野蛮电源。
async function reactivateFloatPolicyAfterWrite(): Promise<void> {
  const active = await getActiveScheme().catch(() => '');
  if (active.toLowerCase() !== PW.YEMAN.toLowerCase()) return;
  await setActiveScheme(PW.YEMAN).catch(() => {});
}

async function applyCtl(): Promise<void> {
  // CPU 浮动统一使用当前 ctl 同步写入 AC/DC；不再存在锁定值覆盖浮动值的旁路。
  const t = snapshot ?? { acTurbo: true, dcTurbo: true, acFreq: 0, dcFreq: 0, acAggr: 100, dcAggr: 100 };
  const acFreq = ctl.freq;
  const dcFreq = ctl.freq;
  const acAggr = ctl.aggr;
  const dcAggr = ctl.aggr;
  // applyFloatPowerParams also disables boost for low targets; keep the snapshot
  // only for normal/high-frequency profiles where boost is meaningful.
  const overrides = floatCpuOverrides(curProfile);
  const acTurbo = overrides?.turbo ?? (acFreq > 0 && acFreq <= 2000 ? false : t.acTurbo);
  const dcTurbo = overrides?.turbo ?? (dcFreq > 0 && dcFreq <= 2000 ? false : t.dcTurbo);
  const sides: Array<'ac' | 'dc'> = ['ac', 'dc'];
  const params: PowerParams = {
    acFreq,
    dcFreq,
    acTurbo,
    dcTurbo,
    acAggr,
    dcAggr,
    acMinState: overrides?.minState,
    dcMinState: overrides?.minState,
    sides,
    restoreMaxState: false,
  };
  const runId = floatRunId;
  // Keep only the newest target. A slow powercfg call must not create a stale
  // backlog, but a request is never silently discarded before it is run.
  ctlApplyPending = { params, runId };
  if (ctlApplyRunning) {
    await ctlApplyQueue;
    return;
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
        const applied = await applyFloatPowerParams(next.params)
          .then(() => true)
          .catch(() => false);
        if (applied) await reactivateFloatPolicyAfterWrite();
      }
    } finally {
      ctlApplyRunning = false;
    }
  })();
  ctlApplyQueue = run.catch(() => {});
  await run;
}

async function tick(): Promise<void> {
  // 重入锁：丢弃与上一轮尚未结束的 tick 重叠的调用，避免多 tick 交错把 UI 线程
  // 消息泵与 3 线程进程池同时打满（鼠标旁转圈的根因）。
  if (tickRunning) return;
  tickRunning = true;
  try {
  // 每 5 秒刷新浮动运行标志时间戳（native 按 30 秒新鲜度判定，防残留误判）
  if (Date.now() - lastFloatActiveTs > 5000) {
    lastFloatActiveTs = Date.now();
    await fs.writeTextFile(FLOAT_ACTIVE, '1').catch(() => {});
  }
  // 状态读取（浮动按 AC/DC 锁定状态分别写入 CPU；TDP 按当前电源模式读取并独立浮动，
  // 不 per-tick 探测 AC/DC，避免每轮 IPC/powershell 阻塞；改为 15s 周期探测，插拔电源后跟随）。
  const st = await readStatus();
  lastStatus = st;
  if (curTarget <= 0) {
    // 不锁帧没有可比较的 FPS 目标：控制循环保持待机，绝不降 TDP/CPU。
    downCount = 0;
    lastTdpDownTs = 0;
    lastAction = 'idle';
    lastTdpAction = 'idle';
    notify();
    return;
  }
  gpuCap = st ? gpuAggrCap(st.gpu ?? 0) : 100; // 每轮刷新 GPU 上限（守护失联→不限）
  // 周期性刷新电源模式：1s 一次。检测到切换后更新 tdpMode，后续 TDP 写入跟随新寄存器；
  // 单真相源下 tdpMax/tdpOriginal 与电源侧无关，无需重读（2026-08-05 修复插拔电源 TDP 失效）。
  if (Date.now() - tdpModeCheckTs > TDP_MODE_CHECK_MS) {
    tdpModeCheckTs = Date.now();
    const pm = await detectPowerMode().catch(() => null);
    if (pm === 'ac' || pm === 'dc') {
      if (pm !== tdpMode) {
        tdpMode = pm;
        // AC/DC changes can be made by Windows or OEM software while the
        // float loop is alive. Re-apply the complete three-class linkage once
        // on the new side instead of trusting the old write cache.
        markCtlDirty();
      } else {
        tdpMode = pm;
      }
    }
  }
  if (!st) {
    // 守护失联（未启动/被杀/瞬读抖动）：每 5 秒尝试重拉一次
    lastAction = 'wait';
    lastTdpAction = 'wait';
    deadStreak++;
    if (--restartCooldown <= 0) {
      // 重拉退避：连续失败（restartBurst 达上限）后冷却从 5s 拉长到 30s，防进程风暴
      restartCooldown = restartBurst >= RESTART_BURST_MAX ? 30 : 5;
      // 我们已拉起且未显式停止（monitorLaunched=true）→ 先不重拉，可能是前端读 hb/status 的
      // 瞬读抖动；连续多轮（deadStreak>=3，约 3 秒）仍失联才重新启用 native worker，
      // 避免把健康守护误杀重启造成抖动。monitorLaunched=false（显式停止后）= 直接重拉。
      if (!monitorLaunched) {
        await startMonitor().catch(() => {});
      } else if (deadStreak >= 3) {
        monitorLaunched = false;
        await startMonitor().catch(() => {});
      }
      // 每次重拉后重置连失计数，给守护全新的宽限期；读到有效状态（下方）再清零 restartBurst
      deadStreak = 0;
      restartBurst++;
    }
    notify();
    return;
  }
  restartCooldown = 0;
  monitorLaunched = true; // 读到有效状态（含空闲心跳）→ 守护确实活着
  deadStreak = 0;
  restartBurst = 0; // 守护恢复 → 连续重拉计数归零
  // 顶部 TDP 最大值滑块在浮动运行期间变更时，由 TdpView.applyTdp → notifyTdpMaxChanged
  // 直接更新内存基准（tdpMax/tdpOriginal），不再每轮读取 tdp.txt，避免每秒 fs 读与守护写争用（鼠标转圈）。

  // 每 2 秒检测 HWiNFO 健康状态（守护存活时轮询；恢复成功自动清除 err-bar）
  if (Date.now() - hwinfoPollTs > 2000) {
    hwinfoPollTs = Date.now();
    void ensureHwiNfo().then(() => notify());
  }

  if (st.fps <= 0 || !st.game) {
    // 无真实游戏：回档位上限 + 积极性满（受档位 floor/GPU 上限钳制），待机不再调节
    downCount = 0;
    lastTdpDownTs = 0;
    if (tdpOriginal > 0 && tdpLimit !== tdpOriginal) await applyTdpLimit(tdpOriginal).catch(() => {});
    const idleSpec = floatProfileSpec(curProfile);
    const idle: CtlState = idleSpec.noAdjust
      ? ctl
      : { freq: idleSpec.max, aggr: clampAggr(100, curProfile) };
    if (idle.freq !== ctl.freq || idle.aggr !== ctl.aggr) {
      ctl = idle;
      markCtlDirty();
    }
    lastAction = 'idle';
    lastTdpAction = 'idle';
    notify();
    return;
  }

  // 1% Low 卡顿信号：1%Low 明显低于「目标帧率 × LOW_WARN_RATIO」→ 帧时间抖动 → 升压平滑
  // 单纯看 Low（不掺 avg）：目标=60 时阈值 30；Low≥30 视为流畅（即便 avg 抖动也不误判）
  const stutter = st.fps1 > 0 && curTarget > 0 && st.fps1 < curTarget * LOW_WARN_RATIO;
  let tdpActed = false;
  const tdpNeedsRecovery = shouldRecoverTdp(st);
  if (tdpNeedsRecovery && await recoverTdp().catch(() => false)) {
    lastAction = 'up-tdp';
    lastTdpAction = 'up-tdp';
    tdpActed = true;
  }
  const tdpReadyToLower = !tdpNeedsRecovery && st.fps > 0 && st.fps1 > 0 &&
    st.fps >= curTarget * 0.8 && st.fps1 >= curTarget * 0.5;
  if (tdpReadyToLower) {
    const lowered = await lowerTdp(st).catch(() => false);
    if (lowered) {
      lastAction = 'down-tdp';
      lastTdpAction = 'down-tdp';
      tdpActed = true;
    }
  }
  if (!tdpActed) lastTdpAction = 'hold';
  // 无压制：保留 RTSS 与 TDP 状态监控，但不参与 CPU 降频/积极性调节。
  if (curProfile === 'none') {
    downCount = 0;
    lastAction = 'hold';
    notify();
    return;
  }
  const verdict = judge(st.fps, curTarget, stutter);
  if (verdict === 'up') {
    downCount = 0;
    const p = floatProfileSpec(curProfile);
    let next: CtlState;
    let action: CtlAction;
    if (stutter && st.fps >= curTarget) {
      // 卡顿型升压：avg 已达标但 1%Low 掉得多（CPU 帧时间抖动）→ 主拉积极性(平滑帧时间)+升频
      next = {
        freq: Math.min(p.max, ctl.freq + FREQ_STEP_UP),
        aggr: Math.min(100, ctl.aggr + 30),
      };
      action = next.aggr > ctl.aggr ? 'up-aggr' : 'up-freq';
    } else {
      // 掉帧型升压（原逻辑）：按掉帧幅度驱动
      const drop = Math.max(0, curTarget - st.fps); // 掉帧幅度 = 目标 - 实际
      if (drop >= 10) {
        // 掉帧 ≥10：积极性拉满 + 主频上限拉满
        next = { freq: p.max, aggr: 100 };
        action = 'up-aggr';
      } else if (drop <= 5) {
        // 掉帧 ≤5：马上 +30 积极性，主频上限大步顶起
        next = {
          freq: Math.min(p.max, ctl.freq + FREQ_STEP_UP),
          aggr: Math.min(100, ctl.aggr + 30),
        };
        action = next.freq > ctl.freq ? 'up-freq' : 'up-aggr';
      } else {
        // 5 < drop < 10：按掉帧幅度线性 +积极性（约每帧 +5）
        next = {
          freq: Math.min(p.max, ctl.freq + FREQ_STEP_UP),
          aggr: Math.min(100, ctl.aggr + Math.round(drop * 5)),
        };
        action = next.freq > ctl.freq ? 'up-freq' : 'up-aggr';
      }
    }
    next.aggr = clampAggr(next.aggr, curProfile); // 按档位 floor/GPU 上限钳制
    lastAction = action;
    if (next.freq !== ctl.freq || next.aggr !== ctl.aggr) {
      ctl = next;
      markCtlDirty();
    }
  } else if (verdict === 'down') {
    downCount++;
    if (downCount >= DOWN_STABLE_N) {
      downCount = 0;
      const r = stepDown(ctl, curProfile);
      if (r.action !== 'hold') lastAction = r.action;
      if (r.action !== 'hold') {
        ctl = { freq: r.next.freq, aggr: clampAggr(r.next.aggr, curProfile) };
        markCtlDirty();
      }
    } else {
      lastAction = 'hold';
    }
  } else {
    downCount = 0;
    lastAction = 'hold';
  }
  // 本帧若没有发生 TDP 调节动作，标注为「稳定」
  if (!tdpActed) lastTdpAction = 'hold';
  notify();
  } finally {
    tickRunning = false;
  }
}

// 帧数目标是固定值：开启（含程序启动自动启用）时立即把 RTSS 锁帧应用到目标值。
// 无条件应用——即使当前限制为 0（用户原本未锁帧）也启用；关闭时恢复启用前原值。
let rtssOriginal = 0; // 启用浮动前 RTSS 锁帧值（可为 0），关闭时恢复
let rtssOriginalReady = false;
let rtssSyncQueue: Promise<void> = Promise.resolve();
async function syncRtssLimit(target: number): Promise<void> {
  const next = rtssSyncQueue.then(async () => {
    try {
      await setRtssLimit(target);
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

    tdpOriginal = await readTdp(tdpMode).catch(() => null) ?? 0;
    if (runId !== floatRunId) return;
    tdpMax = tdpOriginal;
    tdpLimit = tdpOriginal;
    tdpVendor = await detectVendor().catch(() => 'unknown' as Vendor);
    if (runId !== floatRunId) return;

    // 先保存原值；只有保存成功后才允许浮动改写 RTSS，关闭时才有安全恢复依据。
    const original = await readRtssLimit().catch(() => null);
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
    void ensureHwiNfo().then(() => notify()).catch(() => {});
    scheduleRtssSync(curTarget);
  } catch {
    // 外围初始化是 best-effort；CPU 参数已经在进入此函数前完成写入。
  }
}

// 开启（或调整目标）：target>0 必须
export async function enableFloat(target: FpsTarget, profile: FloatProfile, tdpStrategy: TdpFloatStrategy = curTdpStrategy): Promise<void> {
  curTarget = target;
  curProfile = profile;
  curTdpStrategy = tdpStrategy;
  // 配置保存只负责记忆 UI 选择，不能成为 CPU powercfg 写入的前置条件。
  await writeConfig().catch(() => {});
  if (target <= 0) {
    if (timer !== null || starting) {
      await disableFloat({ unlockRtss: true });
    } else {
      await syncRtssLimit(0).catch(() => {});
      notify();
    }
    return;
  }
  // 重入保护：下方 2s+ 阻塞期间 timer 尚未置位，并发调用会双拉守护 + 双控制循环（CPU 参数互踩）。
  // 用 starting 在进函数即占位，确保同一时刻只有一个启用流程在跑；已在运行(starting/timer)则只更新目标/档位。
  if (timer !== null || starting) return;
  starting = true;
  const runId = ++floatRunId;
  try {
    rtssOriginalReady = false;
    rtssOriginal = 0;
    // 快照只用于关闭时恢复，不是 CPU 浮动写入的前置条件。
    // 高频 CPU 参数不在启动关键路径读取；写入先直接提交，快照后台完成即可。
    snapshot = null;
    snapshotLoad = readPowerParams().catch(() => null);
    void snapshotLoad.then((value) => {
      if (runId === floatRunId && (starting || timer !== null)) snapshot = value;
    }).catch(() => {});
    // CPU 浮动同时直写 AC/DC，所以启动时不需要先读取当前供电模式。
    // 实际 AC/DC 只供 TDP 后台初始化使用，失败也不影响 CPU 写入。
    tdpMode = 'ac';
    const initialSpec = floatProfileSpec(profile);
    ctl = initialSpec.noAdjust
      ? snapshot
        ? {
            freq: tdpMode === 'ac' ? snapshot.acFreq : snapshot.dcFreq,
            aggr: tdpMode === 'ac' ? snapshot.acAggr : snapshot.dcAggr,
          }
        : { freq: 0, aggr: 100 }
      : { freq: initialSpec.max, aggr: 100 }; // 从档位上限起步，安全第一
    downCount = 0;
    lastTdpDownTs = 0;
    lastTdpRecoverTs = 0;
    // TDP/RTSS/HWiNFO/监控都移到 CPU 首次写入之后，不再成为 CPU 浮动的前置条件。
    tdpOriginal = 0;
    tdpMax = 0;
    tdpLimit = 0;
    tdpVendor = 'unknown';
    lastAction = 'wait';
    hwinfoRecoveryAttempted = false;
    hwinfoDown = false;
    hwinfoErr = false;
    // 先写入浮动接管标志，再直接提交 CPU 三类 AC/DC 参数。
    await fs.writeTextFile(FLOAT_ACTIVE, '1').catch(() => {});
    if (!floatProfileSpec(curProfile).noAdjust) {
      ctl = clampToProfile(ctl, curProfile);
      await applyCtl();
      // 手动 CPU 调节链路会在写入后 setactive；浮动也必须完成同样的
      // 首次策略装载，否则注册表值会变化但实际 CPU 仍沿用旧缓存。
      await activateFloatPolicyOnce();
    }
    timer = setInterval(() => void tick(), 1000);
    notify();
    // CPU 已经完成直写并 /setactive；外围初始化完全后台化，失败不回滚 CPU。
    const init = initializeFloatPeripherals(runId, target);
    peripheralInit = init;
    void init.finally(() => {
      if (peripheralInit === init) peripheralInit = null;
    });
  } finally {
    starting = false;
    // disableFloat 在启动期间被调用 → 启动完成后自动执行关闭，避免启停交叉
    if (stopping) {
      stopping = false;
      const unlockRtss = stoppingUnlockRtss;
      stoppingUnlockRtss = false;
      void Promise.resolve().then(() => disableFloat({ unlockRtss }));
    }
  }
}

// 关闭：停止 CPU 浮动控制；外围浮动资源保持不动，CPU 参数交给 CPU 挡位基线。
// ⚠️ 严禁在此处把 curTarget/curProfile 归零或 writeConfig({target:0})——
// 帧数目标 + 调度档位 是用户「偏好选择」，必须跨开关/跨电源态持久记忆在 autofloat.json。
// 归零会导致拔插电源 / 切到「从不」时把记住的 45/aggressive 洗成 0，下次启用回退 60（用户实测"没记忆"）。
export async function disableFloat(options: { unlockRtss?: boolean } = {}): Promise<void> {
  // enableFloat 正在初始化（starting=true、timer 尚未创建）：标记 stopping，
  // 等 enableFloat 完成后自动执行关闭，避免在此刻强行停止导致守护/daemon/RTSS 状态交叉。
  if (starting) {
    stopping = true;
    stoppingUnlockRtss = stoppingUnlockRtss || options.unlockRtss === true;
    return;
  }
  ++floatRunId;
  ctlApplyPending = null;
  if (ctlFlushTimer !== null) {
    window.clearTimeout(ctlFlushTimer);
    ctlFlushTimer = null;
  }
  ctlDirty = false;
  // 等正在执行的旧 CPU 写入结束，避免取消后旧浮动命令晚到，覆盖随后
  // 由 CPU 挡位提交的新值。
  await ctlApplyQueue.catch(() => {});
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
  downCount = 0;
  lastTdpDownTs = 0;
  lastTdpRecoverTs = 0;
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
    } else {
      ctl = clampToProfile(ctl, profile);
      if (timer !== null) await applyCtl();
    }
  }
  scheduleRtssSync(curTarget);
  notify();
}

// 切换档位：立即钳到新范围
export async function setFloatProfile(profile: FloatProfile): Promise<void> {
  curProfile = profile;
  void writeConfig();
  if (timer !== null || starting) {
    if (profile === 'none') {
      // 无压制：只取消浮动控制；CPU 挡位写入由调度入口负责。
    } else {
      ctl = clampToProfile(ctl, profile);
      if (timer !== null) await applyCtl().catch(() => {});
    }
  }
  notify();
}

// 已运行时增量切换目标：UI/内存立即更新，RTSS 采用尾随防抖，避免连续切档反复重载。
export async function setFloatTarget(target: FpsTarget): Promise<void> {
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

export function setTdpFloatStrategy(strategy: TdpFloatStrategy): void {
  curTdpStrategy = safeTdpStrategy(strategy);
  void writeConfig();
  notify();
}

// 手柄后台（Start+上/下）浮动运行时：改写程序记录的 TDP 最大值，
// 自动浮动按新基准实时重算目标并 notify 刷新状态行；不直接下发硬件（由控制循环按新目标继续调节）。
export async function adjustFloatTdpMax(delta: number): Promise<number> {
  if (timer === null) return 0;
  const next = clampTdp(Math.round(tdpMax) + delta);
  if (next === tdpMax) return tdpMax;
  tdpMax = next;
  tdpOriginal = next;
  // 已下发临时值若超过新最大值则钳制到新上限；等待真实下发后再更新 tdpLimit。
  if (tdpLimit > tdpMax) {
    await applyTdpLimit(tdpMax).catch(() => {});
  }
  // 只改写最大值配置（save:true 记录到程序配置），不直接下发硬件
  await setTdp(tdpMode, next, { apply: false, save: true }).catch(() => {});
  notify();
  return next;
}

// 顶部 TDP 最大值滑块在浮动运行期间变更时调用：直接更新内存基准，
// 不再依赖 tick() 每轮读取 tdp.txt（避免每秒一次 fs 读与守护写争用 → 鼠标转圈）。
// 写盘由调用方 setTdp 完成；此处仅同步 autofloat 内部基准。
export async function notifyTdpMaxChanged(watts: number): Promise<void> {
  if (timer === null && !starting) return; // 浮动未运行/未启动则无需维护内存基准
  const w = clampTdp(watts);
  tdpMax = w;
  tdpOriginal = w;
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
  void writeConfig();
  notify();
  scheduleRtssSync(next); // 尾随 2 秒防抖，避免手柄连发时每步重载 RTSS 卡顿
  return next;
}

// 恢复上次记忆的 帧数目标 + 调度档位（仅载入选择，不自动拉守护）；
// 返回记忆值，由调用方（CpuView 挂载时）决定是否按该选择自动接管。
export async function loadFloatConfig(): Promise<{ target: FpsTarget; profile: FloatProfile; tdpStrategy: TdpFloatStrategy }> {
  const c = await readConfig();
  curTarget = c.target;
  curProfile = c.profile;
  curTdpStrategy = c.tdpStrategy;
  return c;
}
