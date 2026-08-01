// speedhack.ts — 游戏变速桥层（基于 OpenSpeedy 命名管道，纯前端实现）
//
// 原理：PowerShell .NET NamedPipeClientStream 连接 OpenSpeedy bridge 的命名管道。
// 为减少 PS 冷启动耗时（~5s/次），所有步骤合并为一个 PS 脚本一同执行，
// 不拆成「arch→bridge→pipe」三次独立调用。
// active-game 模型：同一时刻只给一个游戏加速。

import { fs, shell } from './api';

export const OPENSPEEDY_DIR = 'C:\\SOFT\\YeMan\\PowerControl\\OpenSpeedy';
export const SPEED_PRESETS = [0.5, 1, 2, 4, 8];
const LOG_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\speedhack.log';

export interface SpeedResult {
  ok: boolean;
  msgs: string[];
}

// ───────────────────────── 日志 ─────────────────────────

async function log(msg: string): Promise<void> {
  const ts = new Date().toISOString().replace('T', ' ').slice(0, 23);
  const line = `${ts}  ${msg}\n`;
  try {
    let content = line;
    if (await fs.exists(LOG_PATH)) {
      const existing = await fs.readTextFile(LOG_PATH);
      content =
        existing.length > 50_000
          ? existing.slice(-30_000) + line
          : existing + line;
    }
    await fs.writeTextFile(LOG_PATH, content);
  } catch {
    /* 日志失败不阻塞 */
  }
}

// ───────────────────────── 单脚本全流程 ─────────────────────────

function escapePS(s: string): string {
  return s.replace(/'/g, "''");
}

// 一次 PS 调用完成：arch 判定 + bridge 启动 + 管道命令下发
async function speedOp(
  pid: number,
  pipeCommands: string[]
): Promise<SpeedResult> {
  await log(`===== speedOp pid=${pid} cmds=[${pipeCommands.join('; ')}] =====`);

  // 管道命令序列（PS 5.1 兼容：不用 ?? 不用 if 表达式）
  const cmdLines = pipeCommands
    .map(
      (c) =>
        `  Write-Output 'CMD:${escapePS(c)}'; ` +
        `$sw.WriteLine('${escapePS(c)}'); ` +
        `$ln = $sr.ReadLine(); if (-not $ln) { $ln = 'NO_RESP' }; ` +
        `Write-Output "RESP:$ln"`
    )
    .join('\n');

  // 单脚本：arch → bridge → pipe
  const ps = `
# ── arch 判定 ──
$sig = @'
[DllImport("kernel32.dll")]
public static extern bool IsWow64Process(IntPtr hProcess, out bool wow64Process);
'@
Add-Type -MemberDefinition $sig -Name W64 -Namespace K32 2>$null
$p = Get-Process -Id ${pid} -ErrorAction Stop
$wow = $false
[K32.W64]::IsWow64Process($p.Handle, [ref]$wow) | Out-Null
$arch = if ($wow) { 'x86' } else { 'x64' }
Write-Output "ARCH:$arch"

# ── bridge 选择（必须匹配游戏架构，否则 DLL 注入必然失败）──
# x86 游戏 → bridge32 + OpenSpeedyBridge32；x64 游戏 → bridge64 + OpenSpeedyBridge64
$brName = if ($arch -eq 'x86') { 'bridge32' } else { 'bridge64' }
$brExe  = if ($arch -eq 'x86') { 'bridge32.exe' } else { 'bridge64.exe' }
$brDir  = '${escapePS(OPENSPEEDY_DIR)}'
$brPath = Join-Path $brDir $brExe
$br = Get-Process -Name $brName -ErrorAction SilentlyContinue
if (-not $br) {
  Write-Output ('BRIDGE:launch_' + $brName)
  if (Test-Path $brPath) {
    Start-Process -FilePath $brPath -WindowStyle Hidden -WorkingDirectory $brDir
    Start-Sleep -Milliseconds 1200
    $br = Get-Process -Name $brName -ErrorAction SilentlyContinue
  } else {
    Write-Output ('BRIDGE:missing_' + $brExe)
  }
}
if ($br) { Write-Output ('BRIDGE:' + $brName + 'ok') } else { Write-Output 'BRIDGE:nobridge' }

# ── 管道连接 ──
$pipeName = if ($brName -eq 'bridge64') { 'OpenSpeedyBridge64' } else { 'OpenSpeedyBridge32' }
$pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', $pipeName, [System.IO.Pipes.PipeDirection]::InOut)
try {
  $pipe.Connect(5000)
} catch {
  Write-Output 'ERROR:pipe_connect_failed'
  exit 1
}
$sw = New-Object System.IO.StreamWriter($pipe)
$sw.AutoFlush = $true
$sr = New-Object System.IO.StreamReader($pipe)

# ── 管道命令 ──
${cmdLines}

$pipe.Close()
`.trim();

  const r = await shell.run('powershell', ['-NoProfile', '-Command', ps]);
  const raw = (r.stdout || '') + '\n' + (r.stderr || '');
  await log(`raw(${raw.length}B): ${raw.slice(0, 800)}`);

  // 解析输出：提取 ARCH / BRIDGE / CMD / RESP 标记行
  const lines = (r.stdout || '')
    .split('\n')
    .map((s) => s.trim())
    .filter(Boolean);
  let arch = 'x64';
  const resps: string[] = [];
  for (const ln of lines) {
    if (ln.startsWith('ARCH:')) arch = ln.slice(5);
    else if (ln.startsWith('RESP:')) resps.push(ln.slice(5));
  }

  const ok = resps.length >= pipeCommands.length && !resps.some((rsp) => rsp.includes('ERROR'));
  await log(`ok=${ok} arch=${arch} resps=[${resps.join(', ')}]`);
  return { ok, msgs: resps };
}

// ───────────────────────── 公开 API ─────────────────────────

export async function applyGameSpeed(
  pid: number,
  factor: number
): Promise<SpeedResult> {
  return speedOp(pid, [
    `INJECT ${pid}`,
    `ENABLE ${pid}`,
    `SETSPEED ${factor}`,
  ]);
}

export async function clearGameSpeed(pid: number): Promise<SpeedResult> {
  return speedOp(pid, [`DISABLE ${pid}`, `EJECT ${pid}`]);
}
