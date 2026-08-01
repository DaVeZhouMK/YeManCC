# 自动CPU浮动优化 — 功能规格 / 复刻蓝图（AUTOFLOAT）

> 用途：用户将恢复到「老版本」前端+native 后，仅把本功能按此文档重新加回去。
> **铁律**：只新增 `shell.hidden` 处理器，**绝不改动**老版本已有的 `shell.execute` / `shell.run` / `proc.running` 等，
> 否则会把 RTSS / 其它功能再次带崩（这是上一轮"去弹窗"误改 `shell.execute` 的教训）。

---

## 1. 功能概述

后台 PowerShell 守护 (`FPS-Monitor.ps1`) 每秒采样 **FPS（来自 HWiNFO 共享内存 "Framerate Presented (avg)" / "(1%)"）＋ 真实游戏身份（工作集>500MB 且非黑名单）**，
前端 `autofloat.ts` 跑一个 1 秒控制循环，**先积极性后主频**地浮动调节 CPU 最大主频上限与调度积极性，
通过既有 `applyPowerParams` 写入 —— 原来的全部联动（最小CPU三联动跟积极性、EPP、节流状态等）自动一起生效。

- **调度主要看帧率（avg），并叠加两路 HWiNFO 信号**：① `1% Low`（Framerate Presented (1%)）明显低于 avg → 判定帧时间抖动(卡顿) → 立即升压平滑；② `GPU 3D 负载`（多 GPU 取最大值，来自 HWiNFO，比 Win32_Perf 准）→ GPU 越忙越钳低 CPU 调度积极性上限。CPU 占用率机制已移除（数据不可靠）。
- 有真实游戏：每秒记录 `fps-status.json`（`{ts,fps,fps1,gpu,game,pid}`）。
- **未检测到游戏：每 10 秒扫一次，且不写任何状态数据**（只留心跳，删除状态文件）。
- 帧率来源 = **HWiNFO 共享内存**（不再用 RTSS，RTSS 读取容易让软件崩溃）。

---

## 2. 涉及文件

### 新增（老版本没有，整文件新增）
| 文件 | 说明 |
|---|---|
| `C:\SOFT\YeMan\PowerControl\FPS-Monitor.ps1` | 后台监控守护（PowerShell，读 HWiNFO 共享内存） |
| native `shell.hidden` IPC 处理器 | 隐藏窗口、异步、脱离存活地拉起子进程（见 §3） |

### 新增（前端模块）
| 文件 | 说明 |
|---|---|
| `src/bridge/autofloat.ts` | 浮动控制桥层（完整见 §5） |

### 修改（在老版本基础上增量加）
| 文件 | 改动 |
|---|---|
| `src/bridge/api.ts` | 给 `shell` 增加 `hidden` 方法（见 §4） |
| `src/views/CpuView.vue` | 引入 autofloat、加「🤖 自动CPU浮动优化」卡片、状态行显示 FPS/1%Low、CSS（见 §6） |

### 复用（老版本已存在，**不要动**）
- `applyPowerParams` / `readPowerParams` / `setActiveScheme` / `PW.YEMAN` —— 来自 `src/bridge/yeman.ts`
- `fs` / `shell` IPC 封装 —— `src/bridge/api.ts`

---

## 3. native 新增 `shell.hidden` 处理器（关键：不碰 shell.execute）

在 `native/main.cpp` 的其它 `ipc_on("shell.*")` 旁边新增。**行为 = 老版 shell.execute（异步、不阻塞、进程脱离存活），但 CREATE_NO_WINDOW 隐藏窗口。**

```cpp
// ── shell.hidden：与 shell.execute 行为一致（异步、不阻塞、进程脱离存活），
//    但用 CREATE_NO_WINDOW 隐藏窗口。专供「自动CPU浮动优化」守护等
//    需要后台常驻但不弹窗的场景。⚠ 不要改动 shell.execute / shell.run。──
ipc_on("shell.hidden", [](const json& a) -> json {
    auto program = a.value("program", std::string{});
    if (program.empty()) throw std::runtime_error("program is required");
    std::wstring cmdLine = quote_windows_arg(U2W(program));
    if (a.contains("args") && a["args"].is_array()) {
        for (auto& arg : a["args"]) {
            if (!arg.is_string()) throw std::runtime_error("shell.hidden args must be strings");
            cmdLine += L" ";
            cmdLine += quote_windows_arg(U2W(arg.get<std::string>()));
        }
    }
    std::vector<wchar_t> cmd(cmdLine.begin(), cmdLine.end());
    cmd.push_back(0);
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    // CREATE_NO_WINDOW 隐藏窗口；不创建可继承管道（守护 stdout 不需要，避免缓冲死锁）；
    // 不传 envBlock（YeManCC 以 requireAdministrator 运行，子进程不会再弹 UAC/MessageBox）。
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        DWORD le = GetLastError();
        throw std::runtime_error(("Failed to start hidden process (Win32 " + std::to_string(le) + ")").c_str());
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return json{{"ok", true}};
});
```

> 说明：`quote_windows_arg` / `U2W` / `W2U` 在 main.cpp 已存在（与 shell.run 同款）。
> 此处理器**绝不调用 `WaitForSingleObject`**（守护要常驻），也**不创建 stdout 管道**。

---

## 4. `src/bridge/api.ts` 增量改动

在现有 `export const shell = { ... }` 里加一行：

```ts
export const shell = {
  run: (program: string, args: string[] = [], timeoutMs = 30000) =>
    invoke<RunResult>('shell.run', { program, args, timeoutMs }),
  open: (url: string) => invoke<boolean>('shell.open', { url }),
  execute: (program: string, args: string[] = []) =>
    invoke<boolean>('shell.execute', { program, args }),
  hidden: (program: string, args: string[] = []) =>
    invoke<{ ok: boolean }>('shell.hidden', { program, args }),   // ← 新增
};
```

---

## 5. `src/bridge/autofloat.ts`（完整模块，新增文件）

> 与当前线上版一致。启动守护走 `shell.hidden`（对应 §3/§4）。
> **注意：调度主要看帧率(avg)，并叠加 1%Low 卡顿升压 + GPU(HWiNFO)积极性上限钳制；CPU 占用率机制已移除（数据不可靠）。**

```ts
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
// 调度主要看帧率（目标 vs 实际 avg），并叠加两路 HWiNFO 信号：
//   · 1% Low（Framerate Presented (1%)）：avg 达标但 1%Low 明显低于 avg → 帧时间抖动(卡顿) → 立即升压平滑
//   · GPU 3D 负载（多 GPU 取最大值）：GPU 越忙越可能是瓶颈 → 钳低 CPU 调度积极性上限（免得白费电也救不了帧率）
// 以上三路数据全部来自 HWiNFO 共享内存（守护内读取），不再用 RTSS / Win32_Perf（易崩/不准）。
// 真实游戏识别（守护内完成）＝ 工作集 > 500MB ＋ 黑名单(内置+exclude.txt) ＋ HWiNFO 帧率>0。
//
// 守护生命周期：
//   start = shell.hidden 拉起 powershell -WindowStyle Hidden（无窗口、无 VBS 中转）
//   stop  = 写 fps-monitor.stop 标志文件，守护自清理退出

import { shell, fs } from './api';
import { applyPowerParams, readPowerParams, setActiveScheme, PW, type PowerParams } from './yeman';

const PC_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
const PS1  = PC_DIR + '\\FPS-Monitor.ps1';
const STATUS = PC_DIR + '\\fps-status.json';
const HB   = PC_DIR + '\\fps-monitor.hb';
const STOPFLAG = PC_DIR + '\\fps-monitor.stop';
// 持久化配置：记住「帧数目标 + 调度档位」，跨重启保留上次选择
const CONFIG = PC_DIR + '\\autofloat.json';

function safeProfile(p: string): FloatProfile {
  return p === 'eco' || p === 'bal' || p === 'perf' ? p : 'bal';
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
export type FloatProfile = 'eco' | 'bal' | 'perf';
export const FLOAT_PROFILES: Record<FloatProfile, { name: string; min: number; max: number }> = {
  eco: { name: '节能', min: 1500, max: 3000 },
  bal: { name: '平衡', min: 2000, max: 3500 },
  perf: { name: '高性能', min: 2500, max: 4500 },
};

// ── 帧数目标选项（0 = 关闭）──
export const FPS_TARGETS = [0, 30, 45, 60] as const;
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

/* ── 控制状态机（纯函数，便于测试）─────────────────────────────
 * 「栈式」性能轴，先积极性后主频（降序），升序反向弹栈：
 *   降（帧率达标，连续 N 秒，速度已加快）：
 *     ① 积极性 100 → 0（步长 -20）    ← 积极性下限已放开到 0
 *     ② 积极性到底后：主频 max → min（步长 -300）
 *   升（帧率不达标，立即，幅度加大）：
 *     ① 主频 → max（步长 +500，大幅度回血）
 *     ② 积极性：按掉帧幅度直接拉升（≤5 帧 +30 / ≥10 帧 拉满 100）
 * 说明：调度主要看帧率（目标 vs 实际 avg），并叠加两路 HWiNFO 信号：
 *   · 1% Low（Framerate Presented (1%)）：avg 达标但 1%Low 明显低于 avg → 帧时间抖动(卡顿) → 立即升压平滑
 *   · GPU 3D 负载（多 GPU 取最大值）：GPU 越忙越可能是瓶颈 → 钳低 CPU 调度积极性上限（免得白费电也救不了帧率）
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

// 1% Low 卡顿判定：1%Low < avg × 该比例 → 帧时间抖动明显，触发 CPU 升压（平滑帧时间）
export const P1_LOW_RATIO = 0.6;

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
  if (s.aggr > AGGR_FLOOR) {
    return { next: { freq: s.freq, aggr: Math.max(AGGR_FLOOR, s.aggr - AGGR_STEP_DOWN) }, action: 'down-aggr' };
  }
  if (s.freq > p.min) {
    return { next: { freq: Math.max(p.min, s.freq - FREQ_STEP_DOWN), aggr: s.aggr }, action: 'down-freq' };
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
  return { freq: Math.max(p.min, Math.min(p.max, s.freq)), aggr: Math.max(AGGR_FLOOR, Math.min(100, s.aggr)) };
}

// 判定阈值（HWiNFO 帧率有 ±1 抖动，留滞回带防振荡）
export const DOWN_STABLE_N = 3; // 连续 3 秒达标才降一档（缓慢调低）

export function judge(fps: number, target: number, stutter: boolean): 'up' | 'down' | 'hold' {
  if (stutter) return 'up';           // 1% Low 明显低于 avg → 帧时间抖动，立即升压平滑
  if (fps < target) return 'up';      // 掉帧(含抖动) → 立即升
  return 'down';                      // 帧率达标 → 降（需连续 DOWN_STABLE_N 秒）
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
let gpuCap = 100;       // 当前 GPU 上限钳制值（每轮按 st.gpu 刷新；守护失联→100）
const listeners = new Set<(info: FloatInfo) => void>();

function notify() {
  const info = getFloatInfo();
  listeners.forEach((cb) => cb(info));
}

export function getFloatInfo(): FloatInfo {
  return { enabled: timer !== null, target: curTarget, profile: curProfile, status: lastStatus, state: { ...ctl }, lastAction };
}

export function onFloatUpdate(cb: (info: FloatInfo) => void): () => void {
  listeners.add(cb);
  return () => listeners.delete(cb);
}

// 把当前控制量写入电源方案（AC/DC 同值；睿频保持快照原状），并延迟激活生效
let activateTimer: ReturnType<typeof setTimeout> | null = null;
async function applyCtl(): Promise<void> {
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

  if (st.fps <= 0 || !st.game) {
    // 无真实游戏：回档位上限 + 积极性满（受 GPU 上限钳制），待机不再调节
    downCount = 0;
    const idle: CtlState = { freq: FLOAT_PROFILES[curProfile].max, aggr: Math.min(100, gpuCap) };
    if (idle.freq !== ctl.freq || idle.aggr !== ctl.aggr) {
      ctl = idle;
      await applyCtl().catch(() => {});
    }
    lastAction = 'idle';
    notify();
    return;
  }

  // 1% Low 卡顿信号：avg 达标但 1%Low 明显低于 avg → 帧时间抖动 → 升压平滑
  const stutter = st.fps1 > 0 && st.fps > 0 && st.fps1 < st.fps * P1_LOW_RATIO;
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
    next.aggr = Math.min(next.aggr, gpuCap); // GPU 越忙积极性上限越低（单一钳制点）
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
        ctl = { freq: r.next.freq, aggr: Math.min(r.next.aggr, gpuCap) };
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
  if (timer !== null) return; // 已在运行，仅更新目标/档位
  snapshot = await readPowerParams().catch(() => null);
  ctl = { freq: FLOAT_PROFILES[profile].max, aggr: 100 }; // 从档位上限起步，安全第一
  downCount = 0;
  lastAction = 'wait';
  await startMonitor().catch(() => {});
  await applyCtl().catch(() => {});
  timer = setInterval(() => void tick(), 1000);
  notify();
}

// 关闭：停守护 + 恢复开启前面板参数
export async function disableFloat(): Promise<void> {
  if (timer !== null) { clearInterval(timer); timer = null; }
  curTarget = 0;
  void writeConfig();
  downCount = 0;
  lastAction = 'idle';
  lastStatus = null;
  await stopMonitor().catch(() => {});
  if (snapshot) {
    await applyPowerParams(snapshot).catch(() => {});
    await setActiveScheme(PW.YEMAN).catch(() => {});
    snapshot = null;
  }
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
```

---

## 6. `FPS-Monitor.ps1`（完整守护脚本，新增文件）

放到 `C:\SOFT\YeMan\PowerControl\FPS-Monitor.ps1`（autofloat.ts 的 `PS1` 常量硬编码此路径）。

> **帧率来源 = HWiNFO 共享内存**（不再用 RTSS）。HWiNFO 需开启「Shared Memory」：
> HWiNFO → 设置 → 常规 → 勾选「启用共享内存支持(Enable shared memory)」，并保持 HWiNFO 运行（传感器模式）。
> 映射名 `Global\HWiNFO_SENS_SM2`，签名 `0x53695748`("HWiS")；读取其 `Framerate Presented (avg)` / `(1%)`。

```powershell
# FPS-Monitor.ps1 - YMCC 自动CPU浮动优化 后台监控守护
# 帧率来源：HWiNFO 共享内存 "Framerate Presented (avg)" / "(1%)"（不再用 RTSS，RTSS 易崩）
# 有真实游戏时：每 1 秒记录 {ts,fps,fps1,game,pid} 到 fps-status.json
# 未检测到游戏时：每 10 秒扫一次，且不在记录任何数据（只写心跳 fps-monitor.hb，不写状态文件）
# 真实游戏识别 = 工作集 > 500MB + 黑名单(内置+exclude.txt)，且 HWiNFO 帧率 > 0
# 停止：创建 C:\SOFT\YeMan\PowerControl\fps-monitor.stop 文件即退出
$ErrorActionPreference = "SilentlyContinue"

$DIR      = "C:\SOFT\YeMan\PowerControl"
$STATUS   = Join-Path $DIR "fps-status.json"
$STOPFLAG = Join-Path $DIR "fps-monitor.stop"
$PIDFILE  = Join-Path $DIR "fps-monitor.pid"
$HB       = Join-Path $DIR "fps-monitor.hb"
$EXCLUDE  = Join-Path $DIR "Sleep\exclude.txt"

# ── 单实例：杀掉旧实例 ──
if (Test-Path $PIDFILE) {
    $old = [int](Get-Content $PIDFILE -ErrorAction SilentlyContinue)
    if ($old -and $old -ne $PID) {
        $op = Get-Process -Id $old -ErrorAction SilentlyContinue
        if ($op -and $op.ProcessName -match "powershell") { Stop-Process -Id $old -Force }
    }
}
Set-Content -Path $PIDFILE -Value $PID
if (Test-Path $STOPFLAG) { Remove-Item $STOPFLAG -Force }
# 初始心跳：证明守护已存活（前端据此区分"空闲"与"守护已死"）
Set-Content -Path $HB -Value ('{"ts":' + [DateTimeOffset]::Now.ToUnixTimeMilliseconds() + '}') -Encoding ASCII

# ── 黑名单（与 quickapp.ts SYSTEM_BLACKLIST 对齐）──
$BLACKLIST = @(
  'system','idle','csrss','winlogon','lsass','services','smss',
  # 浏览器：看视频/网页也会让 HWiNFO "Framerate Presented" > 0，被误判为游戏 → 加入黑名单
  'msedge','chrome',
  'dwm','explorer','shellhost','searchui','searchhost','runtimebroker',
  'sihost','taskhostw','fontdrvhost','conhost','rundll32',
  'msedgewebview2','applicationframehost','startmenuexperiencehost',
  'peopleexperiencehost','systemsettings','lockapp','audiodg',
  'svchost','nvcontainer','nvdisplaycontainer','nvdisplay',
  'rtkauduservice64','yemancc','yemantdpctl','workbuddy',
  'uuremote','uuremotefe','uur','neteaseuu','sunloginclient',
  'teamviewer','anydesk','todesk','rtss','hwinfo64','gameviewer'
)
function Load-Exclude {
    $set = @{}
    foreach ($b in $BLACKLIST) { $set[$b] = $true }
    if (Test-Path $EXCLUDE) {
        foreach ($line in (Get-Content $EXCLUDE)) {
            $t = $line.Trim()
            if ($t -and -not $t.StartsWith('#')) { $set[($t -replace '\.exe$','').ToLower()] = $true }
        }
    }
    return $set
}

# ── HWiNFO 共享内存读取（帧率 avg/1%Low + GPU 3D 负载最大值）──
# 内存映射 "Global\HWiNFO_SENS_SM2"，签名 0x53695748("HWiS")；布局遵循 REALiX 官方 SM2 规范（#pragma pack(1)）
# Header: dwOffsetOfReadingSection@32 / dwSizeOfReadingElement@36 / dwNumReadingElements@40
# Reading 元素(460B): szLabelOrig@+12(128 ANSI) / szUnit@+268(16 ANSI) / double Value@+284 / double ValueAvg@+308
# 标签是 ANSI(单字节)，数值在 double 浮点字段（非文本）；avg/1%Low 均用 ValueAvg(运行均值)以稳定，瞬时 Value 噪声大
# GPU 3D 负载（多 GPU 取最大值）：标签含 "GPU" + 单位 "%" + (D3D Usage|Core Load|Utilization)，
#   排除 Video/Compute/显存控制器/总线/风扇/显存占用 等无关传感器；比 Win32_PerfFormattedData 准很多
function Read-HwinfoAll {
    $res = @{ avg = 0.0; p1 = 0.0; gpu = 0.0; ok = $false; err = '' }
    $mmf = $null
    foreach ($nm in @('Global\HWiNFO_SENS_SM2','HWiNFO_SENS_SM2','Global\HWiNFO_SENS_SM','HWiNFO_SENS_SM')) {
        try { $mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting($nm); if ($mmf) { break } } catch {}
    }
    if (-not $mmf) { $res.err = 'no HWiNFO shared memory'; return $res }
    try {
        $acc = $mmf.CreateViewAccessor(0, 0)
        $sig = $acc.ReadUInt32(0)
        if ($sig -ne 0x53695748) { $res.err = 'bad HWiNFO signature'; return $res }
        # pack(1) header: dwOffsetOfReadingSection@32 / dwSizeOfReadingElement@36 / dwNumReadingElements@40
        $offReading = $acc.ReadUInt32(32)
        $szReading  = $acc.ReadUInt32(36)
        $numReading = $acc.ReadUInt32(40)
        if ($szReading -lt 300) { $res.err = 'unexpected reading element size'; return $res }
        # reading element(460B, pack=1): szLabelOrig@+12(128 ANSI) / szUnit@+268(16 ANSI) / double Value@+284 / ValueAvg@+308
        $enc = [System.Text.Encoding]::ASCII
        $buf = New-Object byte[] 128
        $bufU = New-Object byte[] 16
        for ($i = 0; $i -lt $numReading; $i++) {
            $base = [long]($offReading + $i * $szReading)
            $acc.ReadArray([long]($base + 12), $buf, 0, 128) | Out-Null   # szLabelOrig (ANSI)
            $lab = $enc.GetString($buf).TrimEnd([char]0)
            if ($lab -match 'Framerate') {
                $v  = $acc.ReadDouble([long]($base + 284))   # double Value (current, 噪声大)
                $va = $acc.ReadDouble([long]($base + 308))   # double ValueAvg (running avg, 稳定)
                $cur  = $va; if ($cur  -eq 0) { $cur  = $v }   # avg: 用稳定均值(与 HWiNFO 显示一致)，缺失回退当前值
                $cur1 = $va; if ($cur1 -eq 0) { $cur1 = $v }   # 1%Low: 用稳定均值，缺失回退当前值
                if ($lab -match 'Presented' -and $lab -match '\(avg\)') {
                    $res.avg = $cur
                } elseif (($lab -match 'Presented' -and $lab -match '\(1%\)') -or ($lab -match '1% Low')) {
                    if ($res.p1 -eq 0) { $res.p1 = $cur1 }
                }
            } elseif ($lab -match 'GPU' -and $lab -notmatch 'Video|Compute|Memory Controller|Bus Load|Busy|Memory Usage|Fan') {
                # GPU 3D 负载候选：读单位(%)，匹配 D3D Usage / Core Load / Utilization
                $acc.ReadArray([long]($base + 268), $bufU, 0, 16) | Out-Null   # szUnit (ANSI)
                $unit = $enc.GetString($bufU).TrimEnd([char]0)
                if ($unit -eq '%' -and $lab -match 'D3D Usage|Core Load|Utilization') {
                    $gv = $acc.ReadDouble([long]($base + 284))   # 当前负载(%)，取多 GPU 最大值
                    if ($gv -gt $res.gpu) { $res.gpu = $gv }
                }
            }
        }
        if ($res.avg -gt 0 -or $res.p1 -gt 0) { $res.ok = $true }
        else { $res.err = 'Framerate Presented sensors not found' }
    } finally {
        if ($acc) { $acc.Dispose() }
        $mmf.Dispose()
    }
    return $res
}

# ── 主循环 ──
$excl = Load-Exclude

while ($true) {
    if (Test-Path $STOPFLAG) { break }

    # 心跳：证明守护存活。每轮都写（游戏时 1s、空闲时 10s），前端据此区分"空闲"与"守护已死"
    Set-Content -Path $HB -Value ('{"ts":' + [DateTimeOffset]::Now.ToUnixTimeMilliseconds() + '}') -Encoding ASCII

    # HWiNFO 帧率（avg + 1%Low）+ GPU 3D 负载最大值（多 GPU）
    $hw = Read-HwinfoAll
    $fps  = [math]::Round($hw.avg, 1)
    $fps1 = [math]::Round($hw.p1, 1)
    $gpu  = [math]::Round($hw.gpu, 0)

    if ($hw.ok -and $fps -gt 0) {
        # 有渲染 → 选游戏进程：工作集 > 500MB + 黑名单过滤，取最大工作集者
        $best = $null; $bestWs = 0
        foreach ($pr in (Get-Process | Where-Object { $_.WorkingSet64 -gt 524288000 })) {
            $nm = $pr.ProcessName.ToLower()
            if (-not $excl.ContainsKey($nm)) {
                if ($pr.WorkingSet64 -gt $bestWs) { $bestWs = $pr.WorkingSet64; $best = $pr }
            }
        }
        if ($best) {
            # 有真实游戏：每 1 秒记录完整数据
            $json = '{"ts":' + [DateTimeOffset]::Now.ToUnixTimeMilliseconds() +
                    ',"fps":' + $fps + ',"fps1":' + $fps1 + ',"gpu":' + $gpu +
                    ',"game":' + $('"' + $best.ProcessName + '"') + ',"pid":' + [int]$best.Id + '}'
            Set-Content -Path $STATUS -Value $json -Encoding ASCII
            Start-Sleep -Milliseconds 1000
            continue
        }
    }

    # 未检测到游戏（HWiNFO 无帧率 / 无候选）：10 秒扫一次，且不在记录任何数据
    if (Test-Path $STATUS) { Remove-Item $STATUS -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 10000
}

Remove-Item $STOPFLAG -Force -ErrorAction SilentlyContinue
Remove-Item $STATUS  -Force -ErrorAction SilentlyContinue
Remove-Item $HB      -Force -ErrorAction SilentlyContinue
Remove-Item $PIDFILE -Force -ErrorAction SilentlyContinue
```

> 注：守护识别游戏**不再依赖 RTSS**。帧率只读 HWiNFO（需 HWiNFO 开启共享内存并保持运行）。若 HWiNFO 没开共享内存，FPS 读不到 → 前端停在待机（不影响其它功能）。这是预期行为。

---

## 7. `CpuView.vue` 增量改动

### 7.1 `import` 段（在现有 import 末尾加）
```ts
import {
  FLOAT_PROFILES,
  type FloatProfile,
  type FloatInfo,
  type FpsTarget,
  enableFloat,
  disableFloat,
  setFloatProfile,
  setFloatTarget,
  getFloatInfo,
  onFloatUpdate,
} from '@/bridge/autofloat';
```

### 7.2 `<script setup>` 中（放在合适位置，例如 CPU 滑块逻辑附近）
```ts
// ── 自动CPU浮动优化（控制循环在 autofloat.ts 模块单例上，切页不中断）──
const floatInfo = ref<FloatInfo>(getFloatInfo());
const floatOn = computed(() => floatInfo.value.enabled);
const floatBusy = ref(false);
const offFloatUpdate = onFloatUpdate((info) => {
  floatInfo.value = info;
  // 控制器每次调节后，同步面板滑块显示（仅显示；写入由控制器完成，原有联动一起走）
  if (info.enabled) {
    acFreq.value = info.state.freq;
    dcFreq.value = info.state.freq;
    acAggr.value = info.state.aggr;
    dcAggr.value = info.state.aggr;
  }
});
onUnmounted(offFloatUpdate);

const FLOAT_TARGET_OPTS = [
  { value: 0, label: '关闭' },
  { value: 30, label: '30' },
  { value: 45, label: '45' },
  { value: 60, label: '60' },
];
const FLOAT_PROFILE_OPTS = (Object.keys(FLOAT_PROFILES) as FloatProfile[]).map((k) => ({
  value: k,
  label: `${FLOAT_PROFILES[k].name} ${(FLOAT_PROFILES[k].min / 1000).toFixed(1)}-${(FLOAT_PROFILES[k].max / 1000).toFixed(1)}`,
}));

async function onFloatTarget(v: number) {
  if (!isYemanScheme.value || !paramsOk.value || floatBusy.value) return;
  floatBusy.value = true;
  try {
    const t = v as FpsTarget;
    if (t === 0) {
      await disableFloat();
      await refresh(); // 恢复快照后回读面板真实值
    } else if (floatOn.value) {
      setFloatTarget(t);
    } else {
      await enableFloat(t, floatInfo.value.profile);
    }
    floatInfo.value = getFloatInfo();
  } finally {
    floatBusy.value = false;
  }
}
async function onFloatProfile(v: string) {
  if (!isYemanScheme.value || floatBusy.value) return;
  floatBusy.value = true;
  try {
    await setFloatProfile(v as FloatProfile);
    floatInfo.value = getFloatInfo();
  } finally {
    floatBusy.value = false;
  }
}

const FLOAT_ACTION_TEXT: Record<string, string> = {
  'down-aggr': '↓ 降积极性',
  'down-freq': '↓ 降主频',
  'up-freq': '↑ 升主频',
  'up-aggr': '↑ 升积极性',
  hold: '稳定',
  idle: '待机',
  wait: '监控启动中…',
};
const floatStatusText = computed(() => {
  const i = floatInfo.value;
  if (!i.enabled) return '';
  const s = i.status;
  if (!s) return '⏳ 监控启动中…（后台读取 HWiNFO 帧率）';
  const lim = `上限 ${(i.state.freq / 1000).toFixed(1)}GHz · 积极性 ${i.state.aggr}`;
  if (!s.game || s.fps <= 0) return `💤 待机：未检测到游戏（HWiNFO 帧率=0） · ${lim}`;
  return `🎮 ${s.game} · ${s.fps}fps / 目标${i.target} · 1%Low ${s.fps1}fps · ${lim} · ${FLOAT_ACTION_TEXT[i.lastAction] ?? ''}`;
});
```
> 上述引用了 `acFreq / dcFreq / acAggr / dcAggr / isYemanScheme / paramsOk / refresh`，这些在老版本 CpuView 已存在（CPU 主频/积极性滑块相关），**无需新增**。`onUnmounted` / `computed` / `ref` 同理。

### 7.3 `<template>` 中（放在「当前电源方案」卡片之后）
```html
<section class="card" :class="{ disabled: !isYemanScheme }">
  <h3 class="card-title">🤖 自动CPU浮动优化 <span class="float-sub">（会有少量占用）</span></h3>
  <div class="float-row">
    <span class="row-label">帧数目标</span>
    <SegButton
      :model-value="floatInfo.target"
      :options="FLOAT_TARGET_OPTS"
      color="accent"
      full
      :disabled="!isYemanScheme || !paramsOk || floatBusy"
      @update:model-value="(v: string | number) => onFloatTarget(Number(v))"
    />
  </div>
  <div v-if="floatInfo.target > 0" class="float-row">
    <span class="row-label">调度档位（CPU最大主频浮动范围 GHz）</span>
    <SegButton
      :model-value="floatInfo.profile"
      :options="FLOAT_PROFILE_OPTS"
      color="accent"
      full
      :disabled="!isYemanScheme || floatBusy"
      @update:model-value="(v: string | number) => onFloatProfile(String(v))"
    />
  </div>
  <div v-if="floatOn" class="float-status">{{ floatStatusText }}</div>
  <div v-if="floatOn" class="float-hint">自动模式接管中：下方主频/积极性滑块跟随显示，手动拖动已锁定；关闭后恢复开启前设置。</div>
</section>
```
> 之后原来的 AC/DC CPU最大主频 Slider 需加 `:disabled="... || floatOn"`（见 §7.4），使自动模式接管时手动滑块锁定。

### 7.4 AC/DC 主频 & 积极性 Slider 的 disabled（合并到老版本现有 disabled 表达式）
- `acFreq` 的 `<Slider>` 加 `|| floatOn`（原 disabled：`!isYemanScheme || !paramsOk || floatOn`）
- `acAggr` 的 `<Slider>` 加 `|| floatOn`
- DC 侧同理（`dcFreq` / `dcAggr` 各加 `|| floatOn`）

### 7.5 CSS（`<style>` 末尾追加）
```css
/* 自动CPU浮动优化 */
.float-sub { font-size: 10px; font-weight: 400; color: #8a97a8; }
.float-row { display: flex; flex-direction: column; gap: 6px; margin-top: 10px; }
.float-status { font-size: 11px; color: #7dd3fc; margin-top: 10px; line-height: 1.5; word-break: break-all; }
.float-hint { font-size: 10px; color: #8a97a8; margin-top: 6px; line-height: 1.4; }
```

---

## 8. 常量 / 调参速查表

| 常量 | 值 | 含义 |
|---|---|---|
| `AGGR_FLOOR` | 0 | 积极性下限（已放开到 0） |
| `AGGR_STEP_DOWN` | 20 | 降积极性步长 |
| `FREQ_STEP_DOWN` | 300 | 降频步长 MHz |
| `FREQ_STEP_UP` | 500 | 升频步长 MHz（+0.5GHz） |
| `DOWN_STABLE_N` | 3 | 连续 3 秒达标才降一档 |
| `P1_LOW_RATIO` | 0.6 | 1%Low < avg×0.6 → 判定帧时间抖动(卡顿)→CPU 升压平滑 |
| `gpuAggrCap` | 99→50 / 95→60 / 80→70 / else 100 | GPU 3D 负载% → CPU 积极性上限（多 GPU 取最大） |
| 档位 eco / bal / perf | 1.5–3.0 / 2.0–3.5 / 2.5–4.5 GHz | 主频浮动范围 |
| 升：掉帧≥10 | 积极性&主频拉满 |  |
| 升：掉帧≤5 | 积极性+30、主频+0.5GHz |  |
| 升：5<drop<10 | 积极性+~(drop×5)、主频+0.5GHz |  |
| 升：卡顿型(1%Low<avg×0.6 但 avg 达标) | 积极性+30、主频+0.5GHz（主拉积极性平滑帧时间） |  |
| 调度依据 | 帧率(avg) 为主 + 1%Low 卡顿升压 + GPU 上限钳制 | 全部来自 HWiNFO 共享内存 |
| 状态字段 | `fps`(avg) / `fps1`(1%Low) / `gpu`(GPU 3D 负载%多GPU最大) | 来自 HWiNFO |
| 空闲扫描 | 10 秒 / 不写数据 | 无游戏时 |
| 心跳有效期 | 15 秒 | 区分"空闲"与"守护死" |
| 状态文件有效期 | 5 秒 | 区分"游戏态"与"过期" |

---

## 9. 复刻步骤（顺序）

1. 恢复老版本（前端 + native + `YeManCC.exe` 部署到 `YeManCC\` 与 `YeManCC4\YeManCC\`）。
2. native `main.cpp`：新增 §3 的 `shell.hidden` 处理器。**不改** shell.execute / shell.run。
3. `npm run build` 前端（产出 index.html + assets）。
4. 新增 `src/bridge/autofloat.ts`（§5）、`src/bridge/api.ts` 加 `shell.hidden`（§4）、`src/views/CpuView.vue` 增量（§7）。
5. 把 `FPS-Monitor.ps1`（§6）放到 `C:\SOFT\YeMan\PowerControl\`（**确保 HWiNFO 已开启共享内存**）。
6. 部署前端到两 web 根；清 WebView2 缓存 `EBWebView\`；重启 YeManCC。
7. 验证：进 CPU 页 → 帧数目标选 60 → 开游戏 → 状态行应显示 🎮 游戏·fps；切到其它页仍持续调节（模块单例）；关游戏 → 10 秒后转 💤 待机、不再写 fps-status.json。

---

## 10. 回滚
- 浮动优化纯增量：删 `autofloat.ts`、回退 `api.ts`/`CpuView.vue` 三处增量、删 `shell.hidden` 处理器、删 `FPS-Monitor.ps1` 即可，不影响其它任何功能。
- `shell.hidden` 与老版 `shell.execute`/`shell.run` 完全独立，删除它不会动到 RTSS 等。
