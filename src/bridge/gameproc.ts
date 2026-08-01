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
Write-Output ("SUSPENDED " + $ok + " " + $fail)
Write-Output ("PIDS " + $pidsOut)
`;

// 继续：传入逗号分隔的 pid 列表，逐一 NtResume
const RESUME_BODY = `param([string]$Pids)
$ErrorActionPreference = 'Stop'
$list = @()
foreach ($x in ($Pids -split ',')) {
  if ($x -ne '') { $list += [int]$x }
}
$ok = 0
$fail = 0
foreach ($p in $list) {
  $h = [NtApi]::OpenProcess(0x800, $false, [int]$p)
  if ($h -eq [IntPtr]::Zero) { $fail++; continue }
  $r = [NtApi]::NtResumeProcess($h)
  [NtApi]::CloseHandle($h)
  if ($r -eq 0) { $ok++ } else { $fail++ }
}
Write-Output ("RESUMED " + $ok + " " + $fail)
`;

async function writeCtrlScript(body: string): Promise<void> {
  await fs.writeTextFile(CTRL_PS, NTAPI_BLOCK + body);
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

  // 记录暂停状态，供「继续」精确恢复
  const state = { root: rootPid, name, pids, ts: Date.now() };
  await fs.writeTextFile(SUSPEND_STATE, JSON.stringify(state));
  return { ok: true, okCount, failCount, msgs };
}

// ── 继续游戏：仅恢复本页游戏被暂停的进程（按记录 pid 精确恢复） ──
export async function resumeGame(): Promise<GameCtlResult> {
  const exists = await fs.exists(SUSPEND_STATE);
  if (!exists) {
    return { ok: false, okCount: 0, failCount: 0, msgs: ['没有已暂停的游戏'] };
  }
  let state: any;
  try {
    state = JSON.parse(await fs.readTextFile(SUSPEND_STATE));
  } catch {
    await fs.remove(SUSPEND_STATE).catch(() => {});
    return { ok: false, okCount: 0, failCount: 0, msgs: ['暂停状态损坏，已清除'] };
  }
  const pids: number[] = Array.isArray(state.pids) ? state.pids : [state.root];
  await writeCtrlScript(RESUME_BODY);
  const r = await runPsFile(['-Pids', pids.join(',')]);
  const out = r.stdout + r.stderr;
  const m = out.match(/RESUMED\s+(\d+)\s+(\d+)/);
  const okCount = m ? Number(m[1]) : 0;
  const failCount = m ? Number(m[2]) : 0;
  await fs.remove(SUSPEND_STATE).catch(() => {});

  const msgs: string[] = [];
  if (okCount === 0 && failCount > 0) {
    msgs.push('部分进程已退出或需管理员权限');
  }
  return { ok: okCount > 0 || failCount === 0, okCount, failCount, msgs };
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
  }
  // 若该游戏正是被暂停的那个，顺手清掉 native Sleep\\suspended\\<pid>.txt 标记
  const mark = 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\suspended\\' + rootPid + '.txt';
  if (await fs.exists(mark)) {
    await fs.remove(mark).catch(() => {});
  }
  return { ok, okCount: ok ? 1 : 0, failCount: 0, msgs };
}

// ── 查询当前是否有本页游戏被暂停（跨刷新 / 跨页面同步） ──
// 暂停现在复用 native 睡眠守护的 sgSuspendCurrent，标记写在 Sleep\\suspended\\<pid>.txt
export async function hasSuspendedState(): Promise<{ suspended: boolean; name?: string }> {
  const dir = 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\suspended';
  try {
    const list = await fs.readDir(dir);
    if (!list || list.length === 0) return { suspended: false };
    return { suspended: true };
  } catch {
    return { suspended: false };
  }
}

// ── 模拟鼠标（JoyXoff 开关） ──
export async function isJoyxoffRunning(): Promise<boolean> {
  try {
    const r = await proc.running(['Joyxoff']);
    if (!r) return false;
    return !!(r['Joyxoff'] || r['joyxoff'] || r['Joyxoff.exe']);
  } catch {
    return false;
  }
}

const JOYXOFF_BAT = 'C:\\SOFT\\YeMan\\PowerControl\\JoyXoff.bat';

export async function toggleJoyxoff(): Promise<boolean> {
  await shell.run('cmd', ['/c', JOYXOFF_BAT]).catch(() => {});
  return await isJoyxoffRunning();
}
