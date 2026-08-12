// OpenSpeedy game-speed bridge. Each operation owns one PowerShell client
// transaction, matching the previously verified runtime lifecycle.
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

const speedOperationTail: { current: Promise<unknown> } = { current: Promise.resolve() };

function enqueueSpeedOperation<T>(operation: () => Promise<T>): Promise<T> {
  const run = speedOperationTail.current.then(operation, operation);
  speedOperationTail.current = run.then(() => undefined, () => undefined);
  return run;
}

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

async function log(message: string): Promise<void> {
  try {
    const line = `${new Date().toISOString().replace('T', ' ').slice(0, 23)}  ${message}\n`;
    const existing = await fs.exists(LOG_PATH) ? await fs.readTextFile(LOG_PATH) : '';
    const content = existing.length > 50_000 ? existing.slice(-30_000) + line : existing + line;
    await fs.writeTextFile(LOG_PATH, content);
  } catch {
    // Logging must never block or fail a game operation.
  }
}

function escapePowerShell(value: string): string {
  return value.replace(/'/g, "''");
}

type SpeedOperation = { kind: 'apply'; factor: number } | { kind: 'clear' };

async function speedOp(pid: number, operation: SpeedOperation): Promise<SpeedResult> {
  const action = operation.kind === 'apply' ? `apply:${operation.factor}` : 'clear';
  await log(`===== speedOp pid=${pid} operation=${action} =====`);
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
$parsed = $parts.Count -ge 2 -and [double]::TryParse($parts[1], [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$actual)
if (-not $parsed -or [Math]::Abs($actual - ${operation.factor}) -gt 0.001) { Invoke-SafeRollback; [Console]::Out.WriteLine('RESULT:failed'); exit 2 }
[Console]::Out.WriteLine('RESULT:ok')
`
    : `
# X1 只复位倍率。不要卸载 DLL：部分游戏在运行中 EJECT 时间钩子会崩溃。
$resp = Send-BridgeCommand 'SETSPEED 1'
$actual = 0.0
$parts = @($resp -split '\\s+')
$parsed = $parts.Count -ge 2 -and [double]::TryParse($parts[1], [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$actual)
[Console]::Out.WriteLine('SAFE_FALLBACK:1')
if ($parsed -and [Math]::Abs($actual - 1.0) -le 0.001) { [Console]::Out.WriteLine('RESULT:ok') } else { [Console]::Out.WriteLine('RESULT:failed'); exit 2 }
`;

  const ps = `
$p = Get-Process -Id ${pid} -ErrorAction Stop
$sig = @'
[DllImport("kernel32.dll")]
public static extern bool IsWow64Process(IntPtr hProcess, out bool wow64Process);
'@
Add-Type -MemberDefinition $sig -Name W64 -Namespace K32 2>$null
$wow = $false
[K32.W64]::IsWow64Process($p.Handle, [ref]$wow) | Out-Null
$arch = if ($wow) { 'x86' } else { 'x64' }
Write-Output "ARCH:$arch"
$brName = if ($arch -eq 'x86') { 'bridge32' } else { 'bridge64' }
$brExe = if ($arch -eq 'x86') { 'bridge32.exe' } else { 'bridge64.exe' }
$brDir = '${escapePowerShell(OPENSPEEDY_DIR)}'
$brPath = Join-Path $brDir $brExe
$expectedBridge = [IO.Path]::GetFullPath($brPath)
$allBridge = @(Get-Process -Name $brName -ErrorAction SilentlyContinue)
$foreignBridge = @($allBridge | Where-Object { try { [IO.Path]::GetFullPath($_.Path) -ne $expectedBridge } catch { $true } })
if ($foreignBridge.Count -gt 0) { [Console]::Out.WriteLine('SAFE_SKIP:bridge_conflict'); [Console]::Out.WriteLine('SAFE_FALLBACK:1'); exit 12 }
$br = @($allBridge | Where-Object { try { [IO.Path]::GetFullPath($_.Path) -eq $expectedBridge } catch { $false } }) | Select-Object -First 1
if (-not $br -and (Test-Path $brPath)) { Start-Process -FilePath $brPath -WindowStyle Hidden -WorkingDirectory $brDir; Start-Sleep -Milliseconds 1200 }
$pipeName = if ($brName -eq 'bridge64') { 'OpenSpeedyBridge64' } else { 'OpenSpeedyBridge32' }
$pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', $pipeName, [System.IO.Pipes.PipeDirection]::InOut)
try { $pipe.Connect(5000) } catch { Write-Output 'ERROR:pipe_connect_failed'; exit 1 }
$sw = New-Object System.IO.StreamWriter($pipe); $sw.AutoFlush = $true
$sr = New-Object System.IO.StreamReader($pipe); try { $pipe.ReadTimeout = 4000; $pipe.WriteTimeout = 4000 } catch {}
function Send-BridgeCommand([string]$command) { try { $sw.WriteLine($command); $line = $sr.ReadLine() } catch { $line = 'ERROR pipe_io_failed' }; if ([string]::IsNullOrWhiteSpace($line)) { $line = 'ERROR no_response' }; [Console]::Out.WriteLine('RESP:' + $line); return [string]$line }
function Test-BridgeOk([string]$response) { return $response -match '^OK(?:\\s|$)' }
function Invoke-SafeRollback { $null = Send-BridgeCommand 'SETSPEED 1'; $null = Send-BridgeCommand 'DISABLE ${pid}'; $null = Send-BridgeCommand 'EJECT ${pid}'; $null = Send-BridgeCommand 'GETSPEED'; [Console]::Out.WriteLine('SAFE_FALLBACK:1') }
${transaction}
$pipe.Close()
`.trim();

  const r = await shell.run('powershell', ['-NoProfile', '-Command', ps], 30000);
  const raw = (r.stdout || '') + '\n' + (r.stderr || '');
  await log(`raw(${raw.length}B): ${raw.slice(0, 800)}`);
  const lines = (r.stdout || '').split('\n').map((s) => s.trim()).filter(Boolean);
  const resps = lines.filter((line) => line.startsWith('RESP:')).map((line) => line.slice(5));
  const bridgeConflict = lines.includes('SAFE_SKIP:bridge_conflict');
  const safeFallback = lines.includes('SAFE_FALLBACK:1');
  const ok = lines.includes('RESULT:ok');
  await log(`ok=${ok} skipped=${bridgeConflict} fallback=${safeFallback} resps=[${resps.join(', ')}]`);
  if (bridgeConflict) return { ok: true, skipped: true, safeFallback: true, reason: 'bridge_conflict', msgs: ['OpenSpeedy bridge conflict; kept at 1x'] };
  return { ok, safeFallback, reason: ok ? undefined : 'operation_failed', msgs: resps };
}

export async function applyGameSpeed(pid: number, factor: number, target?: SpeedTargetIdentity): Promise<SpeedResult> {
  if (!Number.isFinite(factor) || factor <= 0 || factor > 16) return { ok: true, skipped: true, safeFallback: true, reason: 'invalid_factor', msgs: ['Invalid speed factor; kept at 1x'] };
  const blocked = await blockedTargetReason(target);
  if (blocked) return { ok: true, skipped: true, safeFallback: true, reason: 'blocked_target', msgs: [`Blocked by compatibility rule: ${blocked}; kept at 1x`] };
  return enqueueSpeedOperation(() => speedOp(pid, { kind: 'apply', factor }));
}

export async function clearGameSpeed(pid: number): Promise<SpeedResult> {
  return enqueueSpeedOperation(() => speedOp(pid, { kind: 'clear' }));
}
