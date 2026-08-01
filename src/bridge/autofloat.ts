// autofloat.ts — 自动CPU浮动优化 桥层
//
// 架构：后台 PowerShell 守护 (FPS-Monitor.ps1)
//   · 有真实游戏时：每 1 秒把 { ts, fps, fps1, game, pid } 写入 fps-status.json；
//     帧率来自 HWiNFO 共享内存的 "Framerate Presented (avg)" / "(1%)"（不再依赖 RTSS，RTSS 易崩）。
//   · 未检测到游戏时：每 10 秒扫一次，且不在记录任何数据（只写心跳 fps-monitor.hb，不写状态文件）。
// 前端每 1 秒轮询：先看 fps-status.json（有游戏时的真数据），再看 fps-monitor.hb
// （区分"守护存活但空闲"与"守护已死"），并在 CpuView 里跑控制循环（先积极性后主频），
// 通过既有 applyPowerParams 写入 —— 原有全部联动（最小CPU三联动跟积极性、
// EPP、节流状态等）自动一起生效。
//
// 调度主要看帧率（目标 vs 实际 avg），并叠加两个 HWiNFO 信号：
//   · 1% Low（Framerate Presented (1%)）：1%Low 低于「目标帧率 × 50%」→ 帧时间抖动(卡顿) → 立即升压平滑（不看 avg，避免稳定场误判）
//   · GPU 3D 负载（多 GPU 取最大值）：GPU 越忙越可能是瓶颈 → 钳低 CPU 调度积极性上限（免得白费电也救不了帧率）
// 以上三路数据全部来自 HWiNFO 共享内存（守护内读取），不再用 RTSS / Win32_Perf（易崩/不准）。
// 真实游戏识别（守护内完成）＝ 工作集 > 500MB ＋ 黑名单(内置+exclude.txt) ＋ HWiNFO 帧率>0。
//
// 守护生命周期：
//   start = shell.hidden 拉起 powershell -WindowStyle Hidden（无窗口、无 VBS 中转）
//   stop  = 写 fps-monitor.stop 标志文件，守护自清理退出

import { shell, fs } from './api';
import { applyPowerParams, readPowerParams, setActiveScheme, PW, type PowerParams, detectPowerMode } from './yeman';
import { applyCpuLock } from './cpulock';

const PC_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
const PS1  = PC_DIR + '\\FPS-Monitor.ps1';
const STATUS = PC_DIR + '\\fps-status.json';
const HB   = PC_DIR + '\\fps-monitor.hb';
const STOPFLAG = PC_DIR + '\\fps-monitor.stop';
// HWiNFO 健康标记（守护每轮写入，前端据此判断 HWiNFO 共享内存是否可用）
const HWINFO_OK = PC_DIR + '\\hwinfo-ok';
const YEMAN_RTSS_VBS = PC_DIR + '\\YeManRTSS.vbs';
// 持久化配置：记住「帧数目标 + 调度档位」，跨重启保留上次选择
const CONFIG = PC_DIR + '\\autofloat.json';

function safeProfile(p: string): FloatProfile {
  return p === 'eco' || p === 'bal' || p === 'perf' || p === 'aggressive' ? p : 'bal';
}
function safeTarget(t: number): FpsTarget {
  return (FPS_TARGETS as readonly number[]).includes(t) ? (t as FpsTarget) : 0;
}

// 读取持久化配置（失败回退默认 关闭/平衡）
async function readConfig(): Promise<{ target: FpsTarget; profile: FloatProfile }> {
  try {
    const txt = await fs.readTextFile(CONFIG);
    const j = JSON.parse(txt) as { target?: number; profile?: string };
    return { target: safeTarget(Number(j.target)), profile: safeProfile(String(j.profile)) };
  } catch {
    return { target: 0, profile: 'bal' };
  }
}

// 写入当前 帧数目标 + 调度档位（不阻塞主流程）
async function writeConfig(): Promise<void> {
  try {
    await fs.writeTextFile(CONFIG, JSON.stringify({ target: curTarget, profile: curProfile }));
  } catch {
    /* 忽略写入失败 */
  }
}

// ── 档位：CPU 最大主频的调度上限范围（MHz）──
export type FloatProfile = 'eco' | 'bal' | 'perf' | 'aggressive';
export const FLOAT_PROFILES: Record<FloatProfile, { name: string; min: number; max: number; minAggr?: number }> = {
  eco: { name: '最小CPU', min: 1500, max: 3000 },
  bal: { name: '平衡CPU', min: 2000, max: 3500 },
  perf: { name: '高性能CPU', min: 2500, max: 4500 },
  aggressive: { name: '激进CPU', min: 4500, max: 7000, minAggr: 80 },
};

// ── 帧数目标选项（0 = 关闭，仅内部使用；UI 下拉只展示 >0 的值）──
export const FPS_TARGETS = [0, 30, 45, 60, 90] as const;
export type FpsTarget = (typeof FPS_TARGETS)[number];

export interface FloatStatus {
  ts: number;
  fps: number; // HWiNFO Framerate Presented (avg) —— 主要调度系数
  fps1: number; // HWiNFO Framerate Presented (1%) —— 1% Low
  gpu: number; // HWiNFO GPU 3D 负载最大值%（多 GPU 取最大，比 Win32_Perf 准）
  game: string | null;
  pid: number;
}

// 启动守护（shell.hidden 隐藏窗口直接拉起 ps1，无 VBS 中转，避免 LOLBin 拦截）
export async function startMonitor(): Promise<void> {
  // 清掉旧停止标志，避免刚启动就被自己停掉
  await fs.remove(STOPFLAG).catch(() => {});
  await shell.hidden('powershell.exe', [
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-WindowStyle', 'Hidden', '-File', PS1,
  ]);
}

// 停止守护（写停止标志，守护 1 秒内自清理退出）
export async function stopMonitor(): Promise<void> {
  await fs.writeTextFile(STOPFLAG, '1').catch(() => {});
}

// 读取最新状态；逻辑：
//   1) fps-status.json 存在且 <5s 且含真实游戏 → 返回真数据（游戏态）
//   2) 否则看心跳 fps-monitor.hb：若 <15s 新鲜 → 守护存活但空闲（未检测到游戏）
//      → 返回合成空闲状态（game:null），前端据此待机、不再重拉守护
//   3) 心跳也缺失/过期 → 守护已死 → null（前端会重拉）
export async function readStatus(): Promise<FloatStatus | null> {
  try {
    const txt = await fs.readTextFile(STATUS);
    const st = JSON.parse(txt) as FloatStatus;
    if (typeof st.ts === 'number' && Date.now() - st.ts <= 5000 && st.game) return st;
  } catch {
    /* 无状态文件 / JSON 损坏 */
  }
  try {
    const hbTxt = await fs.readTextFile(HB);
    const hb = JSON.parse(hbTxt) as { ts?: number };
    if (typeof hb.ts === 'number' && Date.now() - hb.ts < 15000) {
      return { ts: hb.ts, fps: 0, fps1: 0, gpu: 0, game: null, pid: 0 };
    }
  } catch {
    /* 无心跳 */
  }
  return null;
}

// ── HWiNFO 健壮性：检测共享内存是否可用 ──
// 守护每轮写入 hwinfo-ok（文件时间戳），前端据此判断 HWiNFO 是否已启动且共享内存可读
async function hwinfoStatus(): Promise<boolean> {
  try {
    const txt = await fs.readTextFile(HWINFO_OK);
    const ts = Number(txt.trim());
    return !isNaN(ts) && Date.now() - ts < 30000; // 30 秒内有效（守护心跳 10s 一次）
  } catch {
    return false;
  }
}

// 尝试修复 HWiNFO：运行 YeManRTSS.vbs（启动 HWiNFO + 修复共享内存），然后轮询 10 秒
// 返回 true=已恢复，false=仍不可用
async function ensureHwiNfo(): Promise<boolean> {
  if (await hwinfoStatus()) {
    hwinfoDown = false;
    hwinfoRecoveryAttempted = false;
    return true;
  }
  // 避免重复尝试（60 秒冷却，用独立变量不受 tick() 干扰）
  if (hwinfoRecoveryAttempted && Date.now() - hwinfoRecoveryTs < 60000) {
    hwinfoDown = true;
    return false;
  }
  hwinfoRecoveryAttempted = true;
  hwinfoRecoveryTs = Date.now();
  try {
    await shell.run('cscript.exe', ['//nologo', YEMAN_RTSS_VBS]);
  } catch { /* 忽略 cscript 启动错误 */ }
  // 轮询 10 秒（每 500ms 检测一次）
  for (let i = 0; i < 20; i++) {
    await new Promise((r) => setTimeout(r, 500));
    if (await hwinfoStatus()) {
      hwinfoDown = false;
      hwinfoRecoveryAttempted = false; // 成功后重置，允许后续再次自动修复
      return true;
    }
  }
  hwinfoDown = true;
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
// 对 aggressive 档位，minAggr=80 是绝对下限：即使 GPU 占用触发压低也无效。
function clampAggr(aggr: number, profile: FloatProfile): number {
  const p = FLOAT_PROFILES[profile];
  const floor = p.minAggr ?? AGGR_FLOOR;
  let v = Math.max(floor, Math.min(100, aggr));
  // 非 aggressive 档位仍受 GPU 占用上限钳制；aggressive 忽略 GPU 压低。
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

export type CtlAction = 'down-aggr' | 'down-freq' | 'up-freq' | 'up-aggr' | 'hold';

export function stepDown(s: CtlState, profile: FloatProfile): { next: CtlState; action: CtlAction } {
  const p = FLOAT_PROFILES[profile];
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
  const p = FLOAT_PROFILES[profile];
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
  const p = FLOAT_PROFILES[profile];
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
 * 切到其它页面仍持续调节。写入始终走 applyPowerParams —— 原有联动
 * （最小CPU三联动跟积极性等）全部一起生效。
 */
export interface FloatInfo {
  enabled: boolean;
  target: FpsTarget;
  profile: FloatProfile;
  status: FloatStatus | null; // 守护最新状态（null=无数据/守护未就绪）
  state: CtlState; // 当前控制量（主频上限 + 积极性）
  lastAction: CtlAction | 'idle' | 'wait';
  hwinfoDown: boolean; // HWiNFO 共享内存不可用（未开启/传感器缺失/内存共享未启用）
}

let timer: ReturnType<typeof setInterval> | null = null;
let ctl: CtlState = { freq: 0, aggr: 100 };
let snapshot: PowerParams | null = null; // 开启前的面板参数，关闭时恢复
let curTarget: FpsTarget = 0;
let curProfile: FloatProfile = 'bal';
let downCount = 0;
let lastAction: FloatInfo['lastAction'] = 'idle';
let lastStatus: FloatStatus | null = null;
let restartCooldown = 0; // 守护失联重拉冷却
let starting = false;    // 启用流程进行中标志（防并发双启动守护 + 双控制循环）
let gpuCap = 100;       // 当前 GPU 上限钳制值（每轮按 st.gpu 刷新；守护失联→100）
let hwinfoDown = false;                  // HWiNFO 共享内存不可用
let hwinfoRecoveryAttempted = false;     // 已尝试运行 YeManRTSS.vbs 修复（避免重复弹窗）
let hwinfoRecoveryTs = 0;                // 上次尝试修复的时间戳（60s 冷却）
let hwinfoPollTs = 0;                    // 上次 HWiNFO 健康轮询时间戳（2s 间隔）
const listeners = new Set<(info: FloatInfo) => void>();

function notify() {
  const info = getFloatInfo();
  listeners.forEach((cb) => cb(info));
}

export function getFloatInfo(): FloatInfo {
  return { enabled: timer !== null, target: curTarget, profile: curProfile, status: lastStatus, state: { ...ctl }, lastAction, hwinfoDown };
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
export async function applyCpuAutoEnable(): Promise<void> {
  let mode = 'never';
  try {
    const raw = await fs.readTextFile(AUTO_ENABLE_FILE);
    mode = (JSON.parse(raw) as { mode?: string }).mode ?? 'never';
  } catch { /* 文件不存在或损坏 → 按 never 处理 */ }

  // 始终从 config 恢复「帧数目标 + 调度档位」供 UI 回显（仅读盘、不写盘、不拉守护）。
  // 这是「记忆」的关键：即便当前电源态不需要启用浮动，下拉里也应正确高亮上次选中的 30/45/60/90 与挡位。
  const c = await readConfig();
  curTarget = c.target;
  curProfile = c.profile;

  if (mode === 'never') {
    if (timer !== null) await disableFloat();
    return;
  }

  const pm = await detectPowerMode();
  const shouldEnable = mode === 'always' || (mode === 'ac' && pm === 'ac') || (mode === 'dc' && pm === 'dc');
  if (shouldEnable) {
    if (timer === null) {
      const target = c.target > 0 ? c.target : 60;
      await enableFloat(target, c.profile);
    }
  } else {
    if (timer !== null) await disableFloat();
  }
}

export function onFloatUpdate(cb: (info: FloatInfo) => void): () => void {
  listeners.add(cb);
  return () => listeners.delete(cb);
}

// 把当前控制量写入电源方案（AC/DC 同值；睿频保持快照原状），并延迟激活生效
let activateTimer: ReturnType<typeof setTimeout> | null = null;
async function applyCtl(): Promise<void> {
  // 优先级：自动CPU浮动优化开启 = 最高优先。锁定不影响浮动，浮动照常联动改写 AC/DC。
  // （锁定值只在浮动关闭时生效：见 disableFloat / applyCpuLock）
  const t = snapshot ?? { acTurbo: true, dcTurbo: true, acFreq: 0, dcFreq: 0, acAggr: 100, dcAggr: 100 };
  await applyPowerParams({
    acFreq: ctl.freq,
    dcFreq: ctl.freq,
    acTurbo: t.acTurbo,
    dcTurbo: t.dcTurbo,
    acAggr: ctl.aggr,
    dcAggr: ctl.aggr,
  });
  if (activateTimer !== null) clearTimeout(activateTimer);
  activateTimer = setTimeout(() => {
    activateTimer = null;
    setActiveScheme(PW.YEMAN).catch(() => {});
  }, 1500);
}

async function tick(): Promise<void> {
  const st = await readStatus();
  lastStatus = st;
  gpuCap = st ? gpuAggrCap(st.gpu ?? 0) : 100; // 每轮刷新 GPU 上限（守护失联→不限）
  if (!st) {
    // 守护失联（未启动/被杀）：每 5 秒尝试重拉一次
    lastAction = 'wait';
    if (--restartCooldown <= 0) {
      restartCooldown = 5;
      await startMonitor().catch(() => {});
    }
    notify();
    return;
  }
  restartCooldown = 0;

  // 每 2 秒检测 HWiNFO 健康状态（守护存活时轮询；恢复成功自动清除 err-bar）
  if (Date.now() - hwinfoPollTs > 2000) {
    hwinfoPollTs = Date.now();
    void ensureHwiNfo().then(() => notify());
  }

  if (st.fps <= 0 || !st.game) {
    // 无真实游戏：回档位上限 + 积极性满（受档位 floor/GPU 上限钳制），待机不再调节
    downCount = 0;
    const idle: CtlState = { freq: FLOAT_PROFILES[curProfile].max, aggr: clampAggr(100, curProfile) };
    if (idle.freq !== ctl.freq || idle.aggr !== ctl.aggr) {
      ctl = idle;
      await applyCtl().catch(() => {});
    }
    lastAction = 'idle';
    notify();
    return;
  }

  // 1% Low 卡顿信号：1%Low 明显低于「目标帧率 × LOW_WARN_RATIO」→ 帧时间抖动 → 升压平滑
  // 单纯看 Low（不掺 avg）：目标=60 时阈值 30；Low≥30 视为流畅（即便 avg 抖动也不误判）
  const stutter = st.fps1 > 0 && curTarget > 0 && st.fps1 < curTarget * LOW_WARN_RATIO;
  const verdict = judge(st.fps, curTarget, stutter);
  if (verdict === 'up') {
    downCount = 0;
    const p = FLOAT_PROFILES[curProfile];
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
      await applyCtl().catch(() => {});
    }
  } else if (verdict === 'down') {
    downCount++;
    if (downCount >= DOWN_STABLE_N) {
      downCount = 0;
      const r = stepDown(ctl, curProfile);
      lastAction = r.action;
      if (r.action !== 'hold') {
        ctl = { freq: r.next.freq, aggr: clampAggr(r.next.aggr, curProfile) };
        await applyCtl().catch(() => {});
      }
    } else {
      lastAction = 'hold';
    }
  } else {
    downCount = 0;
    lastAction = 'hold';
  }
  notify();
}

// 开启（或调整目标）：target>0 必须
export async function enableFloat(target: FpsTarget, profile: FloatProfile): Promise<void> {
  curTarget = target;
  curProfile = profile;
  void writeConfig();
  // 重入保护：下方 2s+ 阻塞期间 timer 尚未置位，并发调用会双拉守护 + 双控制循环（CPU 参数互踩）。
  // 用 starting 在进函数即占位，确保同一时刻只有一个启用流程在跑；已在运行(starting/timer)则只更新目标/档位。
  if (timer !== null || starting) return;
  starting = true;
  try {
    snapshot = await readPowerParams().catch(() => null);
    ctl = { freq: FLOAT_PROFILES[profile].max, aggr: 100 }; // 从档位上限起步，安全第一
    downCount = 0;
    lastAction = 'wait';
    hwinfoRecoveryAttempted = false;
    hwinfoDown = false;
    await startMonitor().catch(() => {});
    // 等待守护初始化（2 秒），然后检测 HWiNFO 是否可用；若不可用则尝试自动修复
    await new Promise((r) => setTimeout(r, 2000));
    await ensureHwiNfo().catch(() => {});
    await applyCtl().catch(() => {});
    timer = setInterval(() => void tick(), 1000);
    notify();
  } finally {
    starting = false;
  }
}

// 关闭：停守护 + 恢复开启前面板参数。
// ⚠️ 严禁在此处把 curTarget/curProfile 归零或 writeConfig({target:0})——
// 帧数目标 + 调度档位 是用户「偏好选择」，必须跨开关/跨电源态持久记忆在 autofloat.json。
// 归零会导致拔插电源 / 切到「从不」时把记住的 45/aggressive 洗成 0，下次启用回退 60（用户实测"没记忆"）。
export async function disableFloat(): Promise<void> {
  if (timer !== null) { clearInterval(timer); timer = null; }
  // 注意：curTarget / curProfile 保持不变，不再写 config（选择未变，也不能洗掉记忆）
  downCount = 0;
  lastAction = 'idle';
  lastStatus = null;
  hwinfoDown = false;
  await stopMonitor().catch(() => {});
  // 锁定优先：关闭自动优化时重新套用锁定值。applyCpuLock 内部会重新读盘并校验 locked，
  // 不依赖可能过期的模块缓存，避免「isCpuLocked() 读到 false → 漏刷锁定值」的竞态。
  const appliedLock = await applyCpuLock().catch(() => false);
  if (!appliedLock && snapshot) {
    await applyPowerParams(snapshot).catch(() => {});
    await setActiveScheme(PW.YEMAN).catch(() => {});
  }
  snapshot = null;
  notify();
}

// 切换档位：立即钳到新范围
export async function setFloatProfile(profile: FloatProfile): Promise<void> {
  curProfile = profile;
  void writeConfig();
  if (timer !== null) {
    ctl = clampToProfile(ctl, profile);
    await applyCtl().catch(() => {});
  }
  notify();
}

export function setFloatTarget(target: FpsTarget): void {
  curTarget = target;
  void writeConfig();
  notify();
}

// 恢复上次记忆的 帧数目标 + 调度档位（仅载入选择，不自动拉守护）；
// 返回记忆值，由调用方（CpuView 挂载时）决定是否按该选择自动接管。
export async function loadFloatConfig(): Promise<{ target: FpsTarget; profile: FloatProfile }> {
  const c = await readConfig();
  curTarget = c.target;
  curProfile = c.profile;
  return c;
}
