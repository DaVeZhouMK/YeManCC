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
const BLOCKLIST_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\speedhack-blocklist.txt';

export interface SpeedResult {
  ok: boolean;
  msgs: string[];
  skipped?: boolean;
  safeFallback?: boolean;
  reason?: 'blocked_target' | 'bridge_conflict' | 'invalid_factor' | 'operation_failed';
}

export interface SpeedTargetIdentity {
  pid: number;
  name?: string;
  title?: string;
  path?: string;
}

// Minecraft（包括 Dungeons）及 Java/LWJGL 目标不适合使用 OpenSpeedy DLL 注入。
// OpenSpeedy 3.3.8 的 speedpatch 在 MinHook 安装失败时仍会直接弹出
// “MH装载失败”消息框；外层无法在 DLL 已注入后可靠拦截，因此必须在注入前跳过。
export function isMinecraftTarget(
  target: Pick<SpeedTargetIdentity, 'name' | 'title'> | null | undefined,
): boolean {
  if (!target) return false;
  const text = [target.name, target.title]
    .filter((value): value is string => typeof value === 'string')
    .join(' ')
    .toLowerCase();
  const processName = (target.name || '').replace(/\.exe$/i, '').trim();
  return /minecraft|minecraftdungeons|lwjgl|\.minecraft[\\/]/i.test(text) ||
    /^(?:java|javaw|minecraft\.windows|dungeons|dungeons-win64-shipping)$/i.test(processName);
}

async function blockedTargetReason(
  target: Pick<SpeedTargetIdentity, 'name' | 'title'> | null | undefined,
): Promise<string | null> {
  if (isMinecraftTarget(target)) return 'minecraft-or-java';
  if (!target) return null;

  try {
    if (!await fs.exists(BLOCKLIST_PATH)) return null;
    const identity = [target.name, target.title]
      .filter((value): value is string => typeof value === 'string')
      .join(' ')
      .toLowerCase();
    const processName = (target.name || '').toLowerCase();
    const entries = (await fs.readTextFile(BLOCKLIST_PATH))
      .split(/\r?\n/)
      .map((line) => line.replace(/#.*$/, '').trim().toLowerCase())
      .filter(Boolean);
    return entries.find((entry) => identity.includes(entry) || processName === entry) || null;
  } catch {
    return null;
  }
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

type SpeedOperation =
  | { kind: 'apply'; factor: number }
  | { kind: 'clear' };

// 一次 PS 调用完成：arch 判定 + bridge 身份检查 + 管道事务 + 失败回滚。
async function speedOp(
  pid: number,
  operation: SpeedOperation,
): Promise<SpeedResult> {
  const operationText = operation.kind === 'apply' ? `apply:${operation.factor}` : 'clear';
  await log(`===== speedOp pid=${pid} operation=${operationText} =====`);

  const transaction = operation.kind === 'apply'
    ? `
$resp = Send-BridgeCommand 'SETSPEED 1'
if (-not (Test-BridgeOk $resp)) { Invoke-SafeRollback; [Console]::Out.WriteLine('RESULT:failed'); exit 2 }

$resp = Send-BridgeCommand 'INJECT ${pid}'
if (-not (Test-BridgeOk $resp)) { Invoke-SafeRollback; [Console]::Out.WriteLine('RESULT:failed'); exit 2 }

$resp = Send-BridgeCommand 'ENABLE ${pid}'
if (-not (Test-BridgeOk $resp)) { Invoke-SafeRollback; [Console]::Out.WriteLine('RESULT:failed'); exit 2 }

$resp = Send-BridgeCommand 'SETSPEED ${operation.factor}'
if (-not (Test-BridgeOk $resp)) { Invoke-SafeRollback; [Console]::Out.WriteLine('RESULT:failed'); exit 2 }

$resp = Send-BridgeCommand 'GETSPEED'
$actual = 0.0
$parts = @($resp -split '\\s+')
$parsed = $parts.Count -ge 2 -and [double]::TryParse(
  $parts[1],
  [Globalization.NumberStyles]::Float,
  [Globalization.CultureInfo]::InvariantCulture,
  [ref]$actual
)
if (-not $parsed -or [Math]::Abs($actual - ${operation.factor}) -gt 0.001) {
  Invoke-SafeRollback
  [Console]::Out.WriteLine('RESULT:failed')
  exit 2
}
[Console]::Out.WriteLine('RESULT:ok')
`
    : `
$null = Send-BridgeCommand 'SETSPEED 1'
$null = Send-BridgeCommand 'DISABLE ${pid}'
$null = Send-BridgeCommand 'EJECT ${pid}'
$resp = Send-BridgeCommand 'GETSPEED'
$actual = 0.0
$parts = @($resp -split '\\s+')
$parsed = $parts.Count -ge 2 -and [double]::TryParse(
  $parts[1],
  [Globalization.NumberStyles]::Float,
  [Globalization.CultureInfo]::InvariantCulture,
  [ref]$actual
)
[Console]::Out.WriteLine('SAFE_FALLBACK:1')
if ($parsed -and [Math]::Abs($actual - 1.0) -le 0.001) {
  [Console]::Out.WriteLine('RESULT:ok')
} else {
  [Console]::Out.WriteLine('RESULT:failed')
  exit 2
}
`;

  // 单脚本：arch → bridge → pipe
  const ps = `
# ── PID 存活检查（只认 PID，不读取路径） ──
$p = Get-Process -Id ${pid} -ErrorAction Stop
# ── arch 判定 ──
$sig = @'
[DllImport("kernel32.dll")]
public static extern bool IsWow64Process(IntPtr hProcess, out bool wow64Process);
'@
Add-Type -MemberDefinition $sig -Name W64 -Namespace K32 2>$null
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
$expectedBridge = [IO.Path]::GetFullPath($brPath)
$allBridge = @(Get-Process -Name $brName -ErrorAction SilentlyContinue)
$foreignBridge = @($allBridge | Where-Object {
  try { [IO.Path]::GetFullPath($_.Path) -ne $expectedBridge } catch { $true }
})
if ($foreignBridge.Count -gt 0) {
  [Console]::Out.WriteLine('SAFE_SKIP:bridge_conflict')
  [Console]::Out.WriteLine('SAFE_FALLBACK:1')
  exit 12
}
$br = @($allBridge | Where-Object {
  try { [IO.Path]::GetFullPath($_.Path) -eq $expectedBridge } catch { $false }
}) | Select-Object -First 1
if (-not $br) {
  Write-Output ('BRIDGE:launch_' + $brName)
  if (Test-Path $brPath) {
    Start-Process -FilePath $brPath -WindowStyle Hidden -WorkingDirectory $brDir
    Start-Sleep -Milliseconds 1200
    $br = @(Get-Process -Name $brName -ErrorAction SilentlyContinue | Where-Object {
      try { [IO.Path]::GetFullPath($_.Path) -eq $expectedBridge } catch { $false }
    }) | Select-Object -First 1
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
try { $pipe.ReadTimeout = 4000; $pipe.WriteTimeout = 4000 } catch {}

function Send-BridgeCommand([string]$command) {
  [Console]::Out.WriteLine('CMD:' + $command)
  try {
    $sw.WriteLine($command)
    $line = $sr.ReadLine()
  } catch {
    $line = 'ERROR pipe_io_failed'
  }
  if ([string]::IsNullOrWhiteSpace($line)) { $line = 'ERROR no_response' }
  [Console]::Out.WriteLine('RESP:' + $line)
  return [string]$line
}

function Test-BridgeOk([string]$response) {
  return $response -match '^OK(?:\\s|$)'
}

function Invoke-SafeRollback {
  $null = Send-BridgeCommand 'SETSPEED 1'
  $null = Send-BridgeCommand 'DISABLE ${pid}'
  $null = Send-BridgeCommand 'EJECT ${pid}'
  $null = Send-BridgeCommand 'GETSPEED'
  [Console]::Out.WriteLine('SAFE_FALLBACK:1')
}

# ── 管道事务 ──
${transaction}

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

  const bridgeConflict = lines.includes('SAFE_SKIP:bridge_conflict');
  const safeFallback = lines.includes('SAFE_FALLBACK:1');
  const ok = lines.includes('RESULT:ok');
  await log(`ok=${ok} skipped=${bridgeConflict} fallback=${safeFallback} arch=${arch} resps=[${resps.join(', ')}]`);
  if (bridgeConflict) {
    return {
      ok: true,
      skipped: true,
      safeFallback: true,
      reason: 'bridge_conflict',
      msgs: ['Another OpenSpeedy bridge owns the named pipe; kept at 1x'],
    };
  }
  return {
    ok,
    safeFallback,
    reason: ok ? undefined : 'operation_failed',
    msgs: resps,
  };
}

// ───────────────────────── 公开 API ─────────────────────────

export async function applyGameSpeed(
  pid: number,
  factor: number,
  target?: SpeedTargetIdentity,
): Promise<SpeedResult> {
  if (!Number.isFinite(factor) || factor <= 0 || factor > 16) {
    await log(`SKIP invalid speed factor pid=${pid} factor=${factor}`);
    return {
      ok: true,
      skipped: true,
      safeFallback: true,
      reason: 'invalid_factor',
      msgs: ['Invalid speed factor; kept at 1x'],
    };
  }
  const blocked = await blockedTargetReason(target);
  if (blocked) {
    await log(`SKIP blocked speed target pid=${pid} factor=${factor} rule=${blocked}`);
    return {
      ok: true,
      skipped: true,
      safeFallback: true,
      reason: 'blocked_target',
      msgs: [`Blocked by compatibility rule: ${blocked}; kept at 1x`],
    };
  }
  return speedOp(pid, { kind: 'apply', factor });
}

export async function clearGameSpeed(pid: number): Promise<SpeedResult> {
  return speedOp(pid, { kind: 'clear' });
}
