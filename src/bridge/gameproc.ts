// gameproc.ts — 快捷应用「游戏控制」桥层（暂停 / 继续 / 关闭 / 模拟鼠标）
//
// 关键约束（用户 2026-07-28 明确）：暂停 / 继续 / 关闭 只能且必须作用于
// 「本页识别到的游戏 exe」，一律用 detectForegroundGame 的结果（game.pid / game.name），
// 绝不自己再去扫描「内存 >500MB 最大进程」。
//
// 实现：复用用户 Suspend-LargestGame.ps1 同款 NtApi（Add-Type + NtSuspend/NtResumeProcess），
// 但针对传入的具体 root pid 计算其整棵子进程树（WMI 父子 BFS），只冻/唤醒这棵树的进程。
// 继续时按暂停时记录的 pid 列表精确恢复（不恢复无关进程），状态落 Sleep/quickapp_suspended.json。
// 关闭 = taskkill /F /T 连整棵树关。
// 模拟鼠标复用 JoyXoff.bat（右摇杆 → 鼠标）。
// 纯前端调用，不重编译 native 壳。

import { shell, proc, fs } from './api';

export interface GameCtlResult {
  ok: boolean;
  okCount: number;
  failCount: number;
  msgs: string[];
}

const CTRL_PS = 'C:\\SOFT\\YeMan\\PowerControl\\_quickapp_ctrl.ps1';
const SUSPEND_STATE = 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\quickapp_suspended.json';
export const QUICKAPP_SUSPENDED_EVENT = 'quickapp:suspended-state';

function emitSuspendedState(suspended: boolean): void {
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent(QUICKAPP_SUSPENDED_EVENT, { detail: { suspended } }));
  }
}

// NtApi 互操作块（与用户 Suspend-LargestGame.ps1 完全一致；PS 5.1 兼容，无 ?? / 内联 if）
const NTAPI_BLOCK = `Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public class NtApi {
  [DllImport("ntdll.dll")] public static extern int NtSuspendProcess(IntPtr h);
  [DllImport("ntdll.dll")] public static extern int NtResumeProcess(IntPtr h);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
'@
`;

// 暂停：传入 root pid，计算其整棵子进程树并 NtSuspend；输出 SUSPENDED <ok> <fail> 与 PIDS <csv>
const SUSPEND_BODY = `param([int]$RootPid)
$ErrorActionPreference = 'Stop'
$procs = Get-CimInstance Win32_Process
$map = @{}
foreach ($p in $procs) {
  $pp = [int]$p.ParentProcessId
  if (-not $map.ContainsKey($pp)) { $map[$pp] = New-Object System.Collections.Generic.List[int] }
  $map[$pp].Add([int]$p.ProcessId)
}
$set = New-Object System.Collections.Generic.HashSet[int]
$q = New-Object System.Collections.Queue
$q.Enqueue($RootPid)
while ($q.Count -gt 0) {
  $cur = $q.Dequeue()
  if ($set.Contains($cur)) { continue }
  $set.Add($cur) | Out-Null
  if ($map.ContainsKey($cur)) {
    foreach ($c in $map[$cur]) { if (-not $set.Contains($c)) { $q.Enqueue($c) } }
  }
}
$ok = 0
$fail = 0
foreach ($p in $set) {
  $h = [NtApi]::OpenProcess(0x800, $false, [int]$p)
  if ($h -eq [IntPtr]::Zero) { $fail++; continue }
  $r = [NtApi]::NtSuspendProcess($h)
  [NtApi]::CloseHandle($h)
  if ($r -eq 0) { $ok++ } else { $fail++ }
}
$pidsOut = ($set | ForEach-Object { $_ }) -join ','
# 输出 root 进程身份，供恢复时校验 PID 未被复用
try {
  $root = Get-Process -Id $RootPid -ErrorAction Stop
  Write-Output ("ROOT_NAME " + $root.ProcessName)
  Write-Output ("ROOT_PATH " + $root.Path)
} catch { Write-Output ("ROOT_NAME ") }
Write-Output ("SUSPENDED " + $ok + " " + $fail)
Write-Output ("PIDS " + $pidsOut)
`;

// 继续：显式传入 root pid + 逗号分隔的 pid 列表 + root 身份，逐一 NtResume；先校验 root PID 未被复用
const RESUME_BODY = `param([int]$RootPid,[string]$Pids,[string]$RootName)
$ErrorActionPreference = 'Stop'
# 校验 root PID 身份：若进程已退出或名称不匹配，拒绝恢复（防止 PID 复用误操作）。
try {
  $root = Get-Process -Id $RootPid -ErrorAction Stop
  if ($RootName -and $root.ProcessName -ne $RootName) {
    Write-Output ("ROOT_MISMATCH " + $root.ProcessName)
    exit 1
  }
} catch { Write-Output ("ROOT_GONE"); exit 1 }
$list = @()
$failedList = @()
foreach ($x in ($Pids -split ',')) {
  if ($x -ne '') { $list += [int]$x }
}
$ok = 0
$fail = 0
foreach ($p in $list) {
  $h = [NtApi]::OpenProcess(0x800, $false, [int]$p)
  if ($h -eq [IntPtr]::Zero) { $fail++; $failedList += [string]$p; continue }
  $r = [NtApi]::NtResumeProcess($h)
  [NtApi]::CloseHandle($h)
  if ($r -eq 0) { $ok++ } else { $fail++; $failedList += [string]$p }
}
Write-Output ("RESUMED " + $ok + " " + $fail)
if ($failedList.Count -gt 0) {
  Write-Output ("FAILED_PIDS " + ($failedList -join ','))
}
`;

let controlQueue = Promise.resolve();

function withControlLock<T>(op: () => Promise<T>): Promise<T> {
  const next = controlQueue.then(op, op);
  controlQueue = next.then(() => undefined, () => undefined);
  return next;
}

async function writeCtrlScript(body: string): Promise<void> {
  await fs.writeTextFileAtomic(CTRL_PS, NTAPI_BLOCK + body);
}

async function runPsFile(args: string[]): Promise<{ stdout: string; stderr: string }> {
  const r = await shell.run('powershell', [
    '-NoProfile',
    '-ExecutionPolicy',
    'Bypass',
    '-WindowStyle',
    'Hidden',
    '-File',
    CTRL_PS,
    ...args,
  ]);
  return { stdout: r.stdout || '', stderr: r.stderr || '' };
}

// ── 暂停游戏：仅作用于本页识别到的游戏（root pid 整棵进程树） ──
export async function suspendGame(
  rootPid: number,
  name: string
): Promise<GameCtlResult> {
  return withControlLock(async () => {
  await writeCtrlScript(SUSPEND_BODY);
  const r = await runPsFile(['-RootPid', String(rootPid)]);
  const out = r.stdout + r.stderr;
  const m = out.match(/SUSPENDED\s+(\d+)\s+(\d+)/);
  const okCount = m ? Number(m[1]) : 0;
  const failCount = m ? Number(m[2]) : 0;
  const pidsLine = out.match(/PIDS\s+(.+)/);
  const pids = pidsLine
    ? pidsLine[1]
        .split(',')
        .map((s) => parseInt(s, 10))
        .filter((n) => !isNaN(n))
    : [];

  const msgs: string[] = [];
  if (okCount === 0 && failCount === 0) {
    msgs.push(name + ' 进程不存在或已退出');
    return { ok: false, okCount, failCount, msgs };
  }
  if (okCount === 0 && failCount > 0) {
    msgs.push('无权限暂停（需以管理员运行 YeManCC）');
    return { ok: false, okCount, failCount, msgs };
  }
  if (failCount > 0) {
    msgs.push(failCount + ' 个进程无权限（可能已提权，需管理员运行）');
  }

  // 记录暂停状态（含 root 进程身份），供恢复时校验 PID 未被复用。
  const rootNameMatch = out.match(/ROOT_NAME\s+(.+)/);
  const rootName = rootNameMatch ? rootNameMatch[1].trim() : name;
  const rootPathMatch = out.match(/ROOT_PATH\s+(.+)/);
  const rootPath = rootPathMatch ? rootPathMatch[1].trim() : '';
  const state = { root: rootPid, name: rootName, path: rootPath, pids, ts: Date.now() };
  await fs.writeTextFileAtomic(SUSPEND_STATE, JSON.stringify(state));
  emitSuspendedState(true);
  return { ok: true, okCount, failCount, msgs };
  });
}

// ── 继续游戏：验证 root PID 身份 + 按记录 pid 精确恢复；部分失败不丢弃状态文件 ──
export async function resumeGame(): Promise<GameCtlResult> {
  return withControlLock(async () => {
  const exists = await fs.exists(SUSPEND_STATE);
  if (!exists) {
    emitSuspendedState(false);
    return { ok: false, okCount: 0, failCount: 0, msgs: ['没有已暂停的游戏'] };
  }
  let state: any;
  try {
    state = JSON.parse(await fs.readTextFile(SUSPEND_STATE));
  } catch {
    await fs.remove(SUSPEND_STATE).catch(() => {});
    emitSuspendedState(false);
    return { ok: false, okCount: 0, failCount: 0, msgs: ['暂停状态损坏，已清除'] };
  }
  const pids: number[] = Array.isArray(state.pids) ? state.pids : [state.root];
  const rootPid = Number(state.root);
  const rootName: string = state.name || '';
  if (!Number.isInteger(rootPid) || rootPid <= 0) {
    return { ok: false, okCount: 0, failCount: 0, msgs: ['暂停状态缺少有效 root PID，已保留状态供人工恢复'] };
  }

  await writeCtrlScript(RESUME_BODY);
  const r = await runPsFile([
    '-RootPid', String(rootPid),
    '-Pids', pids.join(','),
    '-RootName', rootName,
  ]);
  const out = r.stdout + r.stderr;

  // 根进程身份校验失败：保留 PID 状态，避免误判后丢失唯一恢复凭据。
  if (out.includes('ROOT_GONE') || out.includes('ROOT_MISMATCH')) {
    emitSuspendedState(true);
    const detail = out.includes('ROOT_MISMATCH') ? 'PID 已被其他进程复用' : 'root 进程已退出';
    return {
      ok: false,
      okCount: 0,
      failCount: pids.length,
      msgs: [`暂停状态校验失败（${detail}），已保留 PID 列表供重试或人工恢复`],
    };
  }

  const m = out.match(/RESUMED\s+(\d+)\s+(\d+)/);
  const okCount = m ? Number(m[1]) : 0;
  const failCount = m ? Number(m[2]) : 0;

  // 部分恢复：只移除已成功恢复的 PID，失败 PID 留在状态文件中供下次重试。
  if (failCount > 0) {
    const failedLine = out.match(/FAILED_PIDS\s+(.+)/);
    const failedSet = new Set(
      failedLine ? failedLine[1].split(',').map((s) => parseInt(s, 10)).filter((n) => !isNaN(n)) : [],
    );
    const remaining = pids.filter((p) => failedSet.has(p));
    if (remaining.length > 0) {
      state.pids = remaining;
      await fs.writeTextFileAtomic(SUSPEND_STATE, JSON.stringify(state));
      emitSuspendedState(true); // 仍有进程挂起
    } else {
      await fs.remove(SUSPEND_STATE).catch(() => {});
      emitSuspendedState(false);
    }
  } else {
    // 全部恢复成功 → 清除状态文件
    await fs.remove(SUSPEND_STATE).catch(() => {});
    emitSuspendedState(false);
  }

  const msgs: string[] = [];
  if (okCount === 0 && failCount > 0) {
    msgs.push('部分进程已退出或需管理员权限');
  }
  if (failCount > 0) {
    msgs.push(`${failCount} 个进程恢复失败，可重试`);
  }
  return { ok: okCount > 0 || failCount === 0, okCount, failCount, msgs };
  });
}

// ── 关闭游戏：仅关闭本页识别到的游戏（taskkill 连整棵树关） ──
export async function closeGame(
  rootPid: number,
  name: string
): Promise<GameCtlResult> {
  const r = await shell
    .run('taskkill', ['/F', '/T', '/PID', String(rootPid)])
    .catch(() => ({ exitCode: 1, stdout: '', stderr: '' }));
  const ok = r.exitCode === 0;
  const msgs: string[] = [];
  if (!ok) {
    msgs.push(name + ' 关闭失败（可能已退出，或被占用/需管理员）');
    return { ok: false, okCount: 0, failCount: 1, msgs };
  }

  // 仅 taskkill 成功后清理对应标记；失败时必须保留恢复凭据。
  const mark = 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\suspended\\' + rootPid + '.txt';
  if (await fs.exists(mark)) {
    await fs.remove(mark).catch(() => {});
  }
  // 全局暂停状态：仅当关闭的游戏 root PID 与记录一致时才清（避免关 A 游戏
  // 误删 B 游戏的暂停状态 —— 2026-08-05 修复）。
  if (await fs.exists(SUSPEND_STATE).catch(() => false)) {
    let isThisGame = false;
    try {
      const st = JSON.parse(await fs.readTextFile(SUSPEND_STATE)) as { root?: number };
      isThisGame = Number(st.root) === rootPid;
    } catch { isThisGame = false; } // 状态损坏：保守不清，避免误清他游戏状态
    if (isThisGame) {
      await fs.remove(SUSPEND_STATE).catch(() => {});
      emitSuspendedState(false);
    }
  }
  return { ok, okCount: ok ? 1 : 0, failCount: 0, msgs };
}

// ── 查询指定游戏是否由快捷控制暂停（跨刷新 / 跨页面同步） ──
export async function hasSuspendedState(rootPid?: number): Promise<{ suspended: boolean; name?: string }> {
  try {
    if (await fs.exists(SUSPEND_STATE)) {
      const raw = await fs.readTextFile(SUSPEND_STATE, 65536).catch(() => '');
      const state = raw
        ? JSON.parse(raw) as { root?: number; name?: string }
        : {};
      const matchesTarget =
        rootPid === undefined || Number(state.root) === rootPid;
      return {
        suspended: matchesTarget,
        name: matchesTarget ? state.name : undefined,
      };
    }
  } catch {
    return { suspended: false };
  }

  if (!rootPid) return { suspended: false };
  const mark =
    'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\suspended\\' + rootPid + '.txt';
  try {
    return { suspended: await fs.exists(mark) };
  } catch {
    return { suspended: false };
  }
}

// ── 模拟鼠标（JoyXoff 开关） ──
export async function isJoyxoffRunning(): Promise<boolean> {
  try {
    // 用前缀匹配（native proc.running 支持 *）：覆盖 JoyXoff / JoyXoff64 / JoyXoffService 等
    // 不同版本/安装形态的进程名，避免另一台机器的 JoyXoff 变体名导致检测失败（按钮暗掉）。
    const r = await proc.running(['JoyXoff*']);
    if (!r) return false;
    // 返回键即查询名（含 *），故按「含 joyxoff 子串且为 true」判定，大小写不敏感。
    return Object.keys(r).some((k) => String(k).toLowerCase().includes('joyxoff') && r[k]);
  } catch {
    return false;
  }
}

const JOYXOFF_BAT = 'C:\\SOFT\\YeMan\\PowerControl\\JoyXoff.bat';
const JOYXOFF_VBS = 'C:\\SOFT\\YeMan\\PowerControl\\模拟鼠标.vbs';

export async function toggleJoyxoff(): Promise<boolean> {
  // 优先启动 模拟鼠标.vbs（内部跑 JoyXoff.bat 切换 + 播放提示音）；
  // wscript 无窗口且不等待，避免 VBS 播提示音期间阻塞 UI（转圈）。
  // 脚本缺失时回退直接跑 JoyXoff.bat。
  const hasVbs = await fs.exists(JOYXOFF_VBS).catch(() => false);
  if (hasVbs) {
    await shell.hidden('wscript.exe', ['//nologo', JOYXOFF_VBS]).catch(() => {});
  } else {
    await shell.run('cmd', ['/c', JOYXOFF_BAT]).catch(() => {});
  }
  // 轮询真实进程状态（最多 ~2s，250ms 步进）：慢机器等待更久、快机器不空等 1.5s
  // （2026-08-05 修复硬编码延迟）。
  for (let i = 0; i < 8; i++) {
    await new Promise((r) => setTimeout(r, 250));
    if (await isJoyxoffRunning()) return true; // 已切换到开
    // 无法区分「切换中」与「已关」，若检测到关态持续到最后则按关返回
  }
  return false;
}
