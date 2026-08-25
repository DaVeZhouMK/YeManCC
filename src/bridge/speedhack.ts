// OpenSpeedy game-speed bridge. Each operation owns one PowerShell client
// transaction, while the target DLL stays injected for the lifetime of the
// target process. Changing a factor must not be treated as a new injection.
import { fs, shell } from './api';
import { detectGame, type DetectedGame } from './gamedetect';

export const OPENSPEEDY_DIR = 'C:\\SOFT\\YeMan\\PowerControl\\OpenSpeedy';
export const SPEED_PRESETS = [0.5, 0.8, 1, 1.2, 1.5, 2, 4, 8];
const LOG_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\speedhack.log';
const BLOCKLIST_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\speedhack-blocklist.txt';
const LOG_KEEP_BYTES = 350_000;
const LOG_TRIM_AT_BYTES = 500_000;
const speedDiagnosticSession = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
const POST_RESET_PROBE_DELAYS_MS = [250, 1000, 3000] as const;

export interface SpeedResult {
  ok: boolean;
  msgs: string[];
  skipped?: boolean;
  safeFallback?: boolean;
  reason?:
    | 'blocked_target'
    | 'bridge_conflict'
    | 'invalid_factor'
    | 'operation_failed'
    | 'valve_no_target'
    | 'valve_target_mismatch'
    | 'stale_target';
}

export interface SpeedTargetIdentity {
  pid: number;
  processCreated?: string;
  name?: string;
  title?: string;
  path?: string;
}

const speedOperationTail: { current: Promise<unknown> } = { current: Promise.resolve() };
const speedLogTail: { current: Promise<void> } = { current: Promise.resolve() };

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
  const write = async () => {
    try {
      const line = `${new Date().toISOString().replace('T', ' ').slice(0, 23)}  ${message}\n`;
      const existing = await fs.exists(LOG_PATH) ? await fs.readTextFile(LOG_PATH) : '';
      const content = existing.length > LOG_TRIM_AT_BYTES
        ? existing.slice(-LOG_KEEP_BYTES) + line
        : existing + line;
      await fs.writeTextFile(LOG_PATH, content);
    } catch {
      // Logging must never block or fail a game operation.
    }
  };
  const current = speedLogTail.current.then(write, write);
  speedLogTail.current = current.then(() => undefined, () => undefined);
  return current;
}

function escapePowerShell(value: string): string {
  return value.replace(/'/g, "''");
}

// OpenSpeedy bridge builds differ in GETSPEED output: some return only OK,
// while others append the numeric factor. SETSPEED remains authoritative;
// validate the optional numeric readback when it is present.
const bridgeSpeedReadback = `
function Test-OptionalSpeedReadback([string]$response, [double]$expected) {
  $parts = @($response -split '\\s+')
  if ($parts.Count -lt 2) { return $true }
  $actual = 0.0
  $parsed = [double]::TryParse($parts[1], [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$actual)
  return $parsed -and [Math]::Abs($actual - $expected) -le 0.001
}
`;

// The bundled bridge currently answers ISENABLED optimistically on older
// builds. Read the DLL's per-PID file mapping directly instead. The mapping is
// created by speedpatch.dll during DLL_PROCESS_ATTACH and is the same flag
// consumed by the hook when deciding whether to scale time.
const targetStateProbe = `
$mapProbe = @'
using System;
using System.Runtime.InteropServices;
public static class YeManOpenSpeedyMapProbe {
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern IntPtr OpenFileMapping(uint desiredAccess, bool inheritHandle, string name);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern IntPtr MapViewOfFile(IntPtr mapping, uint desiredAccess, uint offsetHigh, uint offsetLow, UIntPtr bytes);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool UnmapViewOfFile(IntPtr address);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool CloseHandle(IntPtr handle);
}
'@
Add-Type -TypeDefinition $mapProbe -ErrorAction SilentlyContinue
$gameValveTargetPath = 'C:\SOFT\YeMan\PowerControl\game-target.json'
function Read-GameValveIdentity {
  try {
    if (-not (Test-Path -LiteralPath $gameValveTargetPath)) { return $null }
    $snapshot = Get-Content -LiteralPath $gameValveTargetPath -Raw -ErrorAction Stop | ConvertFrom-Json
    $snapshotPid = [int]$snapshot.pid
    $snapshotCreated = [UInt64]$snapshot.processCreated
    $snapshotGeneration = [UInt64]$snapshot.generation
    $age = [DateTimeOffset]::Now.ToUnixTimeMilliseconds() - [Int64]$snapshot.lastSeen
    if (-not $snapshot.valid -or $snapshotPid -le 0 -or $snapshotCreated -le 0 -or
        $snapshotGeneration -le 0 -or $age -lt 0 -or $age -gt 10000) { return $null }
    return @{ pid = $snapshotPid; processCreated = $snapshotCreated }
  } catch {
    return $null
  }
}
function Test-TrackedProcessIdentity([int]$targetPid, [UInt64]$expectedCreated) {
  if ($expectedCreated -le 0) { return $false }
  try {
    $targetProcess = Get-Process -Id $targetPid -ErrorAction Stop
    $actualCreated = [UInt64]$targetProcess.StartTime.ToFileTimeUtc()
    return $actualCreated -eq $expectedCreated
  } catch {
    return $false
  }
}
function Test-TargetIdentity([int]$targetPid, [UInt64]$expectedCreated) {
  if (-not (Test-TrackedProcessIdentity $targetPid $expectedCreated)) { return $false }
  $valve = Read-GameValveIdentity
  return $valve -and $valve.pid -eq $targetPid -and $valve.processCreated -eq $expectedCreated
}
function Test-TargetEnabled([int]$targetPid) {
  $map = [YeManOpenSpeedyMapProbe]::OpenFileMapping(0x0004, $false, "OpenSpeedy.$targetPid")
  if ($map -eq [IntPtr]::Zero) { return $false }
  $view = [IntPtr]::Zero
  try {
    # dwNumberOfBytesToMap=0 maps the complete one-byte mapping and avoids
    # PowerShell converting an Int32 into the P/Invoke UIntPtr parameter.
    $view = [YeManOpenSpeedyMapProbe]::MapViewOfFile($map, 0x0004, 0, 0, [UIntPtr]::Zero)
    if ($view -eq [IntPtr]::Zero) { return $false }
    return [Runtime.InteropServices.Marshal]::ReadByte($view) -ne 0
  } finally {
    if ($view -ne [IntPtr]::Zero) { [void][YeManOpenSpeedyMapProbe]::UnmapViewOfFile($view) }
    [void][YeManOpenSpeedyMapProbe]::CloseHandle($map)
  }
}

function Get-TargetMappingState([int]$targetPid) {
  $map = [YeManOpenSpeedyMapProbe]::OpenFileMapping(0x0004, $false, "OpenSpeedy.$targetPid")
  if ($map -eq [IntPtr]::Zero) { return 'missing' }
  $view = [IntPtr]::Zero
  try {
    $view = [YeManOpenSpeedyMapProbe]::MapViewOfFile($map, 0x0004, 0, 0, [UIntPtr]::Zero)
    if ($view -eq [IntPtr]::Zero) { return 'invalid' }
    return $(if ([Runtime.InteropServices.Marshal]::ReadByte($view) -ne 0) { 'enabled' } else { 'disabled' })
  } finally {
    if ($view -ne [IntPtr]::Zero) { [void][YeManOpenSpeedyMapProbe]::UnmapViewOfFile($view) }
    [void][YeManOpenSpeedyMapProbe]::CloseHandle($map)
  }
}

function Write-TargetSnapshot([string]$stage, [int]$targetPid) {
  try {
    $targetProcess = Get-Process -Id $targetPid -ErrorAction Stop
    $startUtcTicks = 0
    $uptimeMs = -1
    $cpuMs = -1
    $threads = -1
    $handles = -1
    $workingSet = -1
    $privateBytes = -1
    $windowHandle = 0
    $sessionId = -1
    try {
      $startUtc = $targetProcess.StartTime.ToUniversalTime()
      $startUtcTicks = $startUtc.Ticks
      $uptimeMs = [Math]::Max(0, [Math]::Round(([DateTime]::UtcNow - $startUtc).TotalMilliseconds))
    } catch {}
    try { $cpuMs = [Math]::Round($targetProcess.TotalProcessorTime.TotalMilliseconds) } catch {}
    try { $threads = $targetProcess.Threads.Count } catch {}
    try { $handles = $targetProcess.HandleCount } catch {}
    try { $workingSet = $targetProcess.WorkingSet64 } catch {}
    try { $privateBytes = $targetProcess.PrivateMemorySize64 } catch {}
    try { $windowHandle = $targetProcess.MainWindowHandle.ToInt64() } catch {}
    try { $sessionId = $targetProcess.SessionId } catch {}
    [Console]::Out.WriteLine(
      'TARGET_SNAPSHOT:' + $stage +
      '|pid=' + $targetPid +
      '|startUtcTicks=' + $startUtcTicks +
      '|uptimeMs=' + $uptimeMs +
      '|cpuMs=' + $cpuMs +
      '|threads=' + $threads +
      '|handles=' + $handles +
      '|workingSet=' + $workingSet +
      '|privateBytes=' + $privateBytes +
      '|windowHandle=' + $windowHandle +
      '|sessionId=' + $sessionId
    )
  } catch {
    [Console]::Out.WriteLine('TARGET_SNAPSHOT:' + $stage + '|pid=' + $targetPid + '|state=missing|errorType=' + $_.Exception.GetType().Name)
  }
}
`;

type SpeedOperation = { kind: 'apply'; factor: number } | { kind: 'clear' };

export type SpeedOperationSource = 'user-factor' | 'user-reset' | 'target-change' | 'unknown';

type SpeedRequestContext = {
  id: number;
  source: SpeedOperationSource;
  requestedAt: number;
  intentEpoch: number;
};

type SpeedSession = {
  target: SpeedTargetIdentity;
  enabled: boolean;
  factor: number;
  appliedAt: number;
  firstAppliedAt: number;
  operationCount: number;
  factorHistory: number[];
};

let speedSession: SpeedSession | null = null;
let speedOperationSequence = 0;
let speedIntentEpoch = 0;
export interface GameSpeedState {
  pid: number;
  processCreated: string;
  factor: number;
  enabled: boolean;
}

export function getGameSpeedState(): GameSpeedState | null {
  if (!speedSession) return null;
  return {
    pid: speedSession.target.pid,
    processCreated: speedSession.target.processCreated || '',
    factor: speedSession.factor,
    enabled: speedSession.enabled,
  };
}

function diagnosticNumber(lines: string[], prefix: string): number {
  const line = lines.find((entry) => entry.startsWith(prefix));
  if (!line) return 0;
  const value = Number(line.slice(prefix.length).split('|', 1)[0]);
  return Number.isFinite(value) && value > 0 ? Math.round(value) : 0;
}

function targetStartUtcTicks(lines: string[]): string {
  const line = lines.find((entry) => entry.startsWith('TARGET_SNAPSHOT:transaction_begin|'));
  const match = line?.match(/(?:^|\|)startUtcTicks=(\d+)/);
  return match?.[1] || '0';
}

async function runPostResetDiagnostics(
  pid: number,
  operationId: number,
  resetCompletedAt: number,
  expectedStartUtcTicks: string,
  transactionBridgePid: number,
): Promise<void> {
  const delays = POST_RESET_PROBE_DELAYS_MS.join(',');
  await log(
    `POST_X1_SCHEDULE diagVersion=4 appSession=${speedDiagnosticSession} op=${operationId} ` +
    `pid=${pid} delaysMs=${delays} resetCompletedUnixMs=${resetCompletedAt} ` +
    `expectedStartUtcTicks=${expectedStartUtcTicks} transactionBridgePid=${transactionBridgePid}`,
  );

  // Keep the X1 result path unchanged: launch the read-only probe on the next
  // browser task and never await it from the speed operation. The probe uses
  // one PowerShell process for all three checkpoints, avoiding three launches.
  if (typeof window === 'undefined' || typeof window.setTimeout !== 'function') {
    await log(`POST_X1_SKIP op=${operationId} reason=scheduler_unavailable`);
    return;
  }

  window.setTimeout(() => {
    void (async () => {
      const script = `
$targetPid = ${pid}
$operationId = ${operationId}
$resetCompletedUnixMs = [Int64]${resetCompletedAt}
$expectedStartUtcTicks = [Int64]${expectedStartUtcTicks}
$transactionBridgePid = ${transactionBridgePid}
$mapProbe = @'
using System;
using System.Runtime.InteropServices;
public static class YeManOpenSpeedyPostResetMapProbe {
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern IntPtr OpenFileMapping(uint desiredAccess, bool inheritHandle, string name);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern IntPtr MapViewOfFile(IntPtr mapping, uint desiredAccess, uint offsetHigh, uint offsetLow, UIntPtr bytes);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool UnmapViewOfFile(IntPtr address);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool CloseHandle(IntPtr handle);
}
'@
Add-Type -TypeDefinition $mapProbe -ErrorAction SilentlyContinue

function Get-UnixMs { return [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
function Get-MappingState([int]$probePid) {
  $map = [YeManOpenSpeedyPostResetMapProbe]::OpenFileMapping(0x0004, $false, "OpenSpeedy.$probePid")
  if ($map -eq [IntPtr]::Zero) { return 'missing' }
  $view = [IntPtr]::Zero
  try {
    $view = [YeManOpenSpeedyPostResetMapProbe]::MapViewOfFile($map, 0x0004, 0, 0, [UIntPtr]::Zero)
    if ($view -eq [IntPtr]::Zero) { return 'invalid' }
    return $(if ([Runtime.InteropServices.Marshal]::ReadByte($view) -ne 0) { 'enabled' } else { 'disabled' })
  } finally {
    if ($view -ne [IntPtr]::Zero) { [void][YeManOpenSpeedyPostResetMapProbe]::UnmapViewOfFile($view) }
    [void][YeManOpenSpeedyPostResetMapProbe]::CloseHandle($map)
  }
}
function Get-BridgeState {
  $bridgeProcesses = @(Get-Process -Name bridge32,bridge64 -ErrorAction SilentlyContinue | Sort-Object Id)
  $bridgePids = if ($bridgeProcesses.Count -gt 0) { ($bridgeProcesses.Id -join ',') } else { 'none' }
  $originalAlive = if ($transactionBridgePid -gt 0 -and @($bridgeProcesses.Id) -contains $transactionBridgePid) { 1 } else { 0 }
  return @($bridgePids, $originalAlive)
}
function Write-PostResetSnapshot([int]$targetDelayMs) {
  $deadline = $resetCompletedUnixMs + $targetDelayMs
  $remaining = $deadline - (Get-UnixMs)
  if ($remaining -gt 0) { Start-Sleep -Milliseconds ([int]$remaining) }
  $actualDelayMs = (Get-UnixMs) - $resetCompletedUnixMs
  $mapping = Get-MappingState $targetPid
  $bridgeState = Get-BridgeState
  try {
    $targetProcess = Get-Process -Id $targetPid -ErrorAction Stop
    $startUtcTicks = 0
    $uptimeMs = -1
    $cpuMs = -1
    $threads = -1
    $handles = -1
    $workingSet = -1
    $privateBytes = -1
    $windowHandle = 0
    $sessionId = -1
    try {
      $startUtc = $targetProcess.StartTime.ToUniversalTime()
      $startUtcTicks = $startUtc.Ticks
      $uptimeMs = [Math]::Max(0, [Math]::Round(([DateTime]::UtcNow - $startUtc).TotalMilliseconds))
    } catch {}
    try { $cpuMs = [Math]::Round($targetProcess.TotalProcessorTime.TotalMilliseconds) } catch {}
    try { $threads = $targetProcess.Threads.Count } catch {}
    try { $handles = $targetProcess.HandleCount } catch {}
    try { $workingSet = $targetProcess.WorkingSet64 } catch {}
    try { $privateBytes = $targetProcess.PrivateMemorySize64 } catch {}
    try { $windowHandle = $targetProcess.MainWindowHandle.ToInt64() } catch {}
    try { $sessionId = $targetProcess.SessionId } catch {}
    $sameInstance = if ($expectedStartUtcTicks -le 0 -or $startUtcTicks -eq $expectedStartUtcTicks) { 1 } else { 0 }
    [Console]::Out.WriteLine(
      'POST_X1_SNAPSHOT:op=' + $operationId +
      '|targetDelayMs=' + $targetDelayMs +
      '|actualDelayMs=' + $actualDelayMs +
      '|pid=' + $targetPid +
      '|state=running' +
      '|sameInstance=' + $sameInstance +
      '|startUtcTicks=' + $startUtcTicks +
      '|uptimeMs=' + $uptimeMs +
      '|cpuMs=' + $cpuMs +
      '|threads=' + $threads +
      '|handles=' + $handles +
      '|workingSet=' + $workingSet +
      '|privateBytes=' + $privateBytes +
      '|windowHandle=' + $windowHandle +
      '|sessionId=' + $sessionId +
      '|mapping=' + $mapping +
      '|bridgePids=' + $bridgeState[0] +
      '|transactionBridgeAlive=' + $bridgeState[1]
    )
  } catch {
    [Console]::Out.WriteLine(
      'POST_X1_SNAPSHOT:op=' + $operationId +
      '|targetDelayMs=' + $targetDelayMs +
      '|actualDelayMs=' + $actualDelayMs +
      '|pid=' + $targetPid +
      '|state=missing' +
      '|mapping=' + $mapping +
      '|bridgePids=' + $bridgeState[0] +
      '|transactionBridgeAlive=' + $bridgeState[1] +
      '|errorType=' + $_.Exception.GetType().Name
    )
  }
}

Write-PostResetSnapshot 250
Write-PostResetSnapshot 1000
Write-PostResetSnapshot 3000

$eventStart = [DateTimeOffset]::FromUnixTimeMilliseconds($resetCompletedUnixMs - 1000).LocalDateTime
$events = @(Get-WinEvent -FilterHashtable @{ LogName='Application'; StartTime=$eventStart; Id=@(1000,1001,1002) } -ErrorAction SilentlyContinue | Select-Object -First 20)
$eventSummary = if ($events.Count -gt 0) {
  (($events | ForEach-Object { $_.TimeCreated.ToUniversalTime().Ticks.ToString() + ':' + $_.Id.ToString() + ':' + $_.RecordId.ToString() }) -join ',')
} else { 'none' }
[Console]::Out.WriteLine('POST_X1_EVENTS:op=' + $operationId + '|count=' + $events.Count + '|items=' + $eventSummary)
`.trim();

      try {
        const result = await shell.run('powershell', ['-NoProfile', '-Command', script], 15000);
        const raw = `${result.stdout || ''}\n${result.stderr || ''}`;
        const rawForLog = raw.length > 12_000 ? `${raw.slice(0, 12_000)}\n...[truncated]` : raw;
        await log(
          `POST_X1_RAW diagVersion=4 appSession=${speedDiagnosticSession} op=${operationId} ` +
          `exitCode=${result.exitCode} bytes=${raw.length}\n${rawForLog}`,
        );
      } catch (error) {
        await log(
          `POST_X1_ERROR diagVersion=4 appSession=${speedDiagnosticSession} op=${operationId} ` +
          `message=${String((error as Error)?.message || error).replace(/[\r\n]+/g, ' ').slice(0, 500)}`,
        );
      }
    })();
  }, 0);
}

function sameSpeedTarget(
  left: SpeedTargetIdentity | null,
  right: SpeedTargetIdentity | null,
): boolean {
  if (!left || !right || left.pid !== right.pid) return false;
  // PID is the control selector. Creation time is the mandatory guard against
  // Windows reusing that selector for a different process instance.
  return Boolean(left.processCreated && right.processCreated &&
    left.processCreated === right.processCreated);
}

function valveTargetFromGame(game: DetectedGame): SpeedTargetIdentity | null {
  if (!game.processCreated || !Number.isSafeInteger(game.pid) || game.pid <= 0) return null;
  return {
    pid: game.pid,
    processCreated: game.processCreated,
    name: game.name,
    title: game.title,
    path: game.path,
  };
}

function normalizeProcessCreated(value: string | undefined): string {
  return value && /^\d+$/.test(value) ? value : '0';
}

async function acquireSpeedValveTarget(expectedPid: number): Promise<
  { target: SpeedTargetIdentity } | { target: null; reason: 'valve_no_target' | 'valve_target_mismatch' | 'stale_target' }
> {
  const game = await detectGame(true).catch(() => null);
  if (!game) return { target: null, reason: 'valve_no_target' };
  const target = valveTargetFromGame(game);
  if (!target) return { target: null, reason: 'stale_target' };
  if (expectedPid > 0 && target.pid !== expectedPid) {
    return { target: null, reason: 'valve_target_mismatch' };
  }
  return { target };
}

async function speedOp(
  pid: number,
  operation: SpeedOperation,
  target?: SpeedTargetIdentity,
  request?: SpeedRequestContext,
): Promise<SpeedResult> {
  const operationId = request?.id ?? ++speedOperationSequence;
  const source = request?.source ?? 'unknown';
  const action = operation.kind === 'apply' ? `apply:${operation.factor}` : 'clear';
  const startedAt = Date.now();
  const queueWaitMs = request ? Math.max(0, startedAt - request.requestedAt) : 0;
  const previousPid = speedSession?.target.pid || 0;
  const previousFactor = speedSession?.factor ?? 1;
  const sincePreviousMs = speedSession ? Math.max(0, startedAt - speedSession.appliedAt) : -1;
  const sinceFirstApplyMs = speedSession ? Math.max(0, startedAt - speedSession.firstAppliedAt) : -1;
  const previousOperationCount = speedSession?.operationCount ?? 0;
  const previousHistory = speedSession?.factorHistory.join('>') || 'none';
  const currentTarget = target || { pid };
  const expectedCreated = normalizeProcessCreated(currentTarget.processCreated);
  const oldTarget = speedSession?.target || null;
  const oldExpectedCreated = normalizeProcessCreated(oldTarget?.processCreated);
  if (expectedCreated === '0') {
    return { ok: true, skipped: true, safeFallback: true, reason: 'stale_target', msgs: ['Game valve identity is missing; kept at 1x'] };
  }
  // The request may have waited behind another speed transaction. Re-read the
  // native valve immediately before injection/enable/set-speed so a target
  // switch during the queue can never apply the old operation.
  const latest = await detectGame(true).catch(() => null);
  const latestTarget = latest ? valveTargetFromGame(latest) : null;
  const latestMatchesTarget = !!latestTarget && sameSpeedTarget(latestTarget, currentTarget);
  if (!latestMatchesTarget && source !== 'target-change') {
    if (operation.kind === 'apply') speedSession = null;
    return {
      ok: true,
      skipped: true,
      safeFallback: true,
      reason: 'stale_target',
      msgs: ['The game valve target changed while queued; kept at 1x'],
    };
  }
  if (source === 'target-change' && speedSession &&
      !sameSpeedTarget(speedSession.target, currentTarget)) {
    return { ok: true, skipped: true, safeFallback: true, reason: 'stale_target', msgs: ['A newer speed target superseded the old cleanup'] };
  }
  if (!latestMatchesTarget && request?.intentEpoch !== speedIntentEpoch) {
    return { ok: true, skipped: true, safeFallback: true, reason: 'stale_target', msgs: ['A newer speed target superseded the old cleanup'] };
  }
  const sameTarget = operation.kind === 'apply' && sameSpeedTarget(speedSession?.target || null, currentTarget);
  const switchingTarget = operation.kind === 'apply' && !!speedSession && !sameTarget;
  const oldPid = switchingTarget ? speedSession?.target.pid : null;
  const needsEnable = operation.kind === 'apply' && (!sameTarget || !speedSession?.enabled);
  await log(
    `BEGIN diagVersion=4 appSession=${speedDiagnosticSession} op=${operationId} source=${source} ` +
    `pid=${pid} action=${action} queueWaitMs=${queueWaitMs} previousPid=${previousPid} ` +
    `previousFactor=${previousFactor} sincePreviousMs=${sincePreviousMs} sinceFirstApplyMs=${sinceFirstApplyMs} ` +
    `previousOperationCount=${previousOperationCount} history=${previousHistory} sameTarget=${sameTarget ? 1 : 0} ` +
    `switchingTarget=${switchingTarget ? 1 : 0} needsEnable=${needsEnable ? 1 : 0}`,
  );

  const transaction = operation.kind === 'apply'
    ? `
${oldPid ? `# Stop the previous target before changing the shared factor. The
# previous PID is usable only when its saved process instance still exists.
# It is intentionally no longer the current valve target; this is the
# arbiter-owned cleanup needed to remove the old game's speed hook.
if (Test-TrackedProcessIdentity ${oldPid} [UInt64]${oldExpectedCreated}) {
$resp = Send-BridgeCommand 'DISABLE ${oldPid}'
if (-not (Test-BridgeOk $resp)) { Invoke-SafeRollback; [Console]::Out.WriteLine('RESULT:failed'); exit 2 }
$resp = Send-BridgeCommand 'SETSPEED 1'
if (-not (Test-BridgeOk $resp)) { Invoke-SafeRollback; [Console]::Out.WriteLine('RESULT:failed'); exit 2 }
} else {
  [Console]::Out.WriteLine('OLD_TARGET_SKIP:identity_mismatch')
}
` : ''}
${`# Target selection comes from the game recognition valve. The PID and
# process creation time are checked again immediately before every control
# operation; names and titles remain display metadata only.
if (-not (Test-TargetIdentity ${pid} [UInt64]${expectedCreated})) {
  [Console]::Out.WriteLine('SAFE_SKIP:target_identity_mismatch')
  [Console]::Out.WriteLine('SAFE_FALLBACK:1')
  [Console]::Out.WriteLine('RESULT:failed')
  exit 3
}
$targetState = Get-TargetMappingState ${pid}
[Console]::Out.WriteLine('TARGET_MAPPING_BEFORE:' + $targetState)
if ($targetState -eq 'missing' -or $targetState -eq 'invalid') {
  $resp = Send-BridgeCommand 'INJECT ${pid}'
  if (-not (Test-BridgeOk $resp)) { Invoke-SafeRollback; [Console]::Out.WriteLine('RESULT:failed'); exit 2 }
  Start-Sleep -Milliseconds 150
  Write-TargetSnapshot 'after_inject' ${pid}
}
$targetState = Get-TargetMappingState ${pid}
if ($targetState -eq 'missing' -or $targetState -eq 'invalid') {
  [Console]::Out.WriteLine('TARGET_MAPPING:missing')
  Invoke-SafeRollback
  [Console]::Out.WriteLine('RESULT:failed')
  exit 2
}
if (${needsEnable ? '$true' : '$false'} -or $targetState -ne 'enabled') {
  if (-not (Test-TargetIdentity ${pid} [UInt64]${expectedCreated})) {
    [Console]::Out.WriteLine('SAFE_SKIP:target_identity_mismatch')
    Invoke-SafeRollback
    [Console]::Out.WriteLine('RESULT:failed')
    exit 3
  }
  $resp = Send-BridgeCommand 'ENABLE ${pid}'
  if (-not (Test-BridgeOk $resp)) { Invoke-SafeRollback; [Console]::Out.WriteLine('RESULT:failed'); exit 2 }
  Write-TargetSnapshot 'after_enable' ${pid}
}
[Console]::Out.WriteLine('TARGET_MAPPING:' + (Get-TargetMappingState ${pid}))
`}$targetEnabled = Test-TargetEnabled ${pid}
if (-not $targetEnabled) {
  [Console]::Out.WriteLine('TARGET_ENABLED:0')
  Invoke-SafeRollback
  [Console]::Out.WriteLine('RESULT:failed')
  exit 2
}
[Console]::Out.WriteLine('TARGET_ENABLED:1')
if (-not (Test-TargetIdentity ${pid} [UInt64]${expectedCreated})) {
  [Console]::Out.WriteLine('SAFE_SKIP:target_identity_mismatch')
  Invoke-SafeRollback
  [Console]::Out.WriteLine('RESULT:failed')
  exit 3
}
Write-TargetSnapshot 'before_setspeed' ${pid}
$resp = Send-BridgeCommand 'SETSPEED ${operation.factor}'
if (-not (Test-BridgeOk $resp)) { Invoke-SafeRollback; [Console]::Out.WriteLine('RESULT:failed'); exit 2 }
Write-TargetSnapshot 'after_setspeed' ${pid}
$resp = Send-BridgeCommand 'GETSPEED'
if (-not (Test-BridgeOk $resp) -or -not (Test-OptionalSpeedReadback $resp ${operation.factor}) -or -not (Test-TargetEnabled ${pid})) {
  [Console]::Out.WriteLine('TARGET_ENABLED:0')
  Invoke-SafeRollback
  [Console]::Out.WriteLine('RESULT:failed')
  exit 2
}
[Console]::Out.WriteLine('TARGET_FACTOR_REQUESTED:${operation.factor}')
Write-TargetSnapshot 'after_getspeed' ${pid}
[Console]::Out.WriteLine('TARGET_MAPPING_AFTER_SPEED:' + (Get-TargetMappingState ${pid}))
[Console]::Out.WriteLine('RESULT:ok')
`
    : `
# X1 only resets the shared factor. Do not unload the DLL while the game runs.
if (-not (Test-TargetIdentity ${pid} [UInt64]${expectedCreated})) {
  [Console]::Out.WriteLine('SAFE_SKIP:target_identity_mismatch')
  [Console]::Out.WriteLine('SAFE_FALLBACK:1')
  [Console]::Out.WriteLine('RESULT:failed')
  exit 3
}
Write-TargetSnapshot 'before_setspeed_1' ${pid}
$resp = Send-BridgeCommand 'SETSPEED 1'
if (-not (Test-BridgeOk $resp)) { [Console]::Out.WriteLine('SAFE_FALLBACK:1'); [Console]::Out.WriteLine('RESULT:failed'); exit 2 }
Write-TargetSnapshot 'after_setspeed_1' ${pid}
$resp = Send-BridgeCommand 'GETSPEED'
[Console]::Out.WriteLine('TARGET_FACTOR_REQUESTED:1')
Write-TargetSnapshot 'after_getspeed_1' ${pid}
[Console]::Out.WriteLine('SAFE_FALLBACK:1')
if ((Test-BridgeOk $resp) -and (Test-OptionalSpeedReadback $resp 1.0)) { [Console]::Out.WriteLine('RESULT:ok') } else { [Console]::Out.WriteLine('RESULT:failed'); exit 2 }
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
$patchPath = Join-Path $brDir $(if ($arch -eq 'x86') { 'speedpatch32.dll' } else { 'speedpatch64.dll' })
$null = [Console]::Out.WriteLine('BRIDGE_EXPECTED:' + $brPath)
foreach ($component in @(@('bridge', $brPath), @('speedpatch', $patchPath))) {
  try {
    $componentFile = Get-Item -LiteralPath $component[1] -ErrorAction Stop
    $componentHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $componentFile.FullName).Hash
    [Console]::Out.WriteLine(
      'COMPONENT:' + $component[0] +
      '|size=' + $componentFile.Length +
      '|lastWriteUtcTicks=' + $componentFile.LastWriteTimeUtc.Ticks +
      '|sha256=' + $componentHash
    )
  } catch {
    [Console]::Out.WriteLine('COMPONENT:' + $component[0] + '|state=missing|errorType=' + $_.Exception.GetType().Name)
  }
}
$expectedBridge = [IO.Path]::GetFullPath($brPath)
$allBridge = @(Get-Process -Name $brName -ErrorAction SilentlyContinue)
$foreignBridge = @($allBridge | Where-Object { try { [IO.Path]::GetFullPath($_.Path) -ne $expectedBridge } catch { $true } })
$null = [Console]::Out.WriteLine('BRIDGE_RUNNING:' + $allBridge.Count)
$null = [Console]::Out.WriteLine('BRIDGE_FOREIGN:' + $foreignBridge.Count)
if ($foreignBridge.Count -gt 0) { [Console]::Out.WriteLine('SAFE_SKIP:bridge_conflict'); [Console]::Out.WriteLine('SAFE_FALLBACK:1'); exit 12 }
$br = @($allBridge | Where-Object { try { [IO.Path]::GetFullPath($_.Path) -eq $expectedBridge } catch { $false } }) | Select-Object -First 1
$launchedBridgePid = 0
if (-not $br -and (Test-Path $brPath)) {
  $launchedBridge = Start-Process -FilePath $brPath -WindowStyle Hidden -WorkingDirectory $brDir -PassThru
  $launchedBridgePid = $launchedBridge.Id
  [Console]::Out.WriteLine('BRIDGE_LAUNCH:1|pid=' + $launchedBridgePid)
  Start-Sleep -Milliseconds 1200
} else {
  $existingBridgePid = if ($br) { $br.Id } else { 0 }
  [Console]::Out.WriteLine('BRIDGE_LAUNCH:0|pid=' + $existingBridgePid)
}
$activeBridge = @(
  Get-Process -Name $brName -ErrorAction SilentlyContinue |
    Where-Object { try { [IO.Path]::GetFullPath($_.Path) -eq $expectedBridge } catch { $false } } |
    Sort-Object Id
)
$selectedBridgePid = if ($activeBridge.Count -gt 0) { $activeBridge[0].Id } elseif ($launchedBridgePid -gt 0) { $launchedBridgePid } else { 0 }
[Console]::Out.WriteLine('BRIDGE_SELECTED_PID:' + $selectedBridgePid)
[Console]::Out.WriteLine('BRIDGE_ACTIVE_PIDS:' + $(if ($activeBridge.Count -gt 0) { $activeBridge.Id -join ',' } else { 'none' }))
$pipeName = if ($brName -eq 'bridge64') { 'OpenSpeedyBridge64' } else { 'OpenSpeedyBridge32' }
[Console]::Out.WriteLine('BRIDGE_PIPE:' + $pipeName)
$pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', $pipeName, [System.IO.Pipes.PipeDirection]::InOut)
$pipeConnectStarted = [Diagnostics.Stopwatch]::GetTimestamp()
try {
  $pipe.Connect(5000)
  $pipeConnectElapsed = ([Diagnostics.Stopwatch]::GetTimestamp() - $pipeConnectStarted) * 1000.0 / [Diagnostics.Stopwatch]::Frequency
  [Console]::Out.WriteLine('BRIDGE_PIPE_CONNECT:ok|elapsedMs=' + [Math]::Round($pipeConnectElapsed, 1))
} catch {
  $pipeConnectElapsed = ([Diagnostics.Stopwatch]::GetTimestamp() - $pipeConnectStarted) * 1000.0 / [Diagnostics.Stopwatch]::Frequency
  [Console]::Out.WriteLine('BRIDGE_PIPE_CONNECT:failed|elapsedMs=' + [Math]::Round($pipeConnectElapsed, 1) + '|errorType=' + $_.Exception.GetType().Name)
  Write-Output 'ERROR:pipe_connect_failed'
  exit 1
}
$sw = New-Object System.IO.StreamWriter($pipe); $sw.AutoFlush = $true
$sr = New-Object System.IO.StreamReader($pipe); try { $pipe.ReadTimeout = 4000; $pipe.WriteTimeout = 4000 } catch {}
function Send-BridgeCommand([string]$command) {
  $started = [Diagnostics.Stopwatch]::GetTimestamp()
  try { $sw.WriteLine($command); $line = $sr.ReadLine() } catch { $line = 'ERROR pipe_io_failed' }
  if ([string]::IsNullOrWhiteSpace($line)) { $line = 'ERROR no_response' }
  $elapsed = ([Diagnostics.Stopwatch]::GetTimestamp() - $started) * 1000.0 / [Diagnostics.Stopwatch]::Frequency
  [Console]::Out.WriteLine('BRIDGE_CMD:' + $command + '|elapsedMs=' + [Math]::Round($elapsed, 1) + '|response=' + $line)
  [Console]::Out.WriteLine('RESP:' + $line)
  return [string]$line
}
function Test-BridgeOk([string]$response) { return $response -match '^OK(?:\\s|$)' }
function Invoke-SafeRollback { $null = Send-BridgeCommand 'SETSPEED 1'; $null = Send-BridgeCommand 'GETSPEED'; [Console]::Out.WriteLine('SAFE_FALLBACK:1') }
${bridgeSpeedReadback}
${targetStateProbe}
Write-TargetSnapshot 'transaction_begin' ${pid}
${transaction}
$pipe.Close()
`.trim();

  const r = await shell.run('powershell', ['-NoProfile', '-Command', ps], 30000);
  const raw = (r.stdout || '') + '\n' + (r.stderr || '');
  const rawForLog = raw.length > 8000 ? `${raw.slice(0, 8000)}\n...[truncated]` : raw;
  await log(`RAW op=${operationId} bytes=${raw.length}\n${rawForLog}`);
  const lines = (r.stdout || '').split('\n').map((s) => s.trim()).filter(Boolean);
  const resps = lines.filter((line) => line.startsWith('RESP:')).map((line) => line.slice(5));
  const diagnosticEvents = lines.filter((line) =>
    line.startsWith('BRIDGE_CMD:') ||
    line.startsWith('BRIDGE_PIPE_CONNECT:') ||
    line.startsWith('TARGET_MAPPING') ||
    line.startsWith('TARGET_ENABLED:') ||
    line.startsWith('TARGET_FACTOR_REQUESTED:'),
  );
  const targetSnapshotCount = lines.filter((line) => line.startsWith('TARGET_SNAPSHOT:')).length;
  const transactionTargetStartUtcTicks = targetStartUtcTicks(lines);
  const transactionBridgePid = diagnosticNumber(lines, 'BRIDGE_SELECTED_PID:');
  const bridgeConflict = lines.includes('SAFE_SKIP:bridge_conflict');
  const targetIdentityMismatch = lines.includes('SAFE_SKIP:target_identity_mismatch');
  const safeFallback = lines.includes('SAFE_FALLBACK:1');
  const ok = lines.includes('RESULT:ok');
  const arch = lines.find((line) => line.startsWith('ARCH:'))?.slice(5) || 'unknown';
  await log(
    `END op=${operationId} durationMs=${Date.now() - startedAt} exitCode=${r.exitCode} arch=${arch} ` +
    `ok=${ok ? 1 : 0} skipped=${bridgeConflict ? 1 : 0} fallback=${safeFallback ? 1 : 0} ` +
    `snapshotCount=${targetSnapshotCount} events=[${diagnosticEvents.join(' || ')}] resps=[${resps.join(' || ')}] ` +
    `note=protocol-success-does-not-prove-game-health`,
  );
  if (ok && source !== 'target-change') {
    const after = await detectGame(true).catch(() => null);
    const afterTarget = after ? valveTargetFromGame(after) : null;
    if (!afterTarget || !sameSpeedTarget(afterTarget, currentTarget)) {
      if (operation.kind === 'apply') speedSession = null;
      return {
        ok: true,
        skipped: true,
        safeFallback: true,
        reason: 'stale_target',
        msgs: ['The game valve target changed during the operation; discarded the result'],
      };
    }
  }
  if (bridgeConflict) return { ok: true, skipped: true, safeFallback: true, reason: 'bridge_conflict', msgs: ['OpenSpeedy bridge conflict; kept at 1x'] };
  if (targetIdentityMismatch) return { ok: true, skipped: true, safeFallback: true, reason: 'stale_target', msgs: ['Target process instance changed; kept at 1x'] };
  if (ok && operation.kind === 'apply') {
    const completedAt = Date.now();
    const continuingSession = sameSpeedTarget(speedSession?.target || null, currentTarget);
    const previousSession = continuingSession ? speedSession : null;
    speedSession = {
      target: currentTarget,
      enabled: true,
      factor: operation.factor,
      appliedAt: completedAt,
      firstAppliedAt: previousSession?.firstAppliedAt ?? completedAt,
      operationCount: (previousSession?.operationCount ?? 0) + 1,
      factorHistory: [...(previousSession?.factorHistory ?? []), operation.factor].slice(-12),
    };
  }
  if (ok && operation.kind === 'clear' && speedSession?.target.pid === pid) {
    const resetCompletedAt = Date.now();
    speedSession.enabled = true;
    speedSession.factor = 1;
    speedSession.appliedAt = resetCompletedAt;
    speedSession.operationCount += 1;
    speedSession.factorHistory = [...speedSession.factorHistory, 1].slice(-12);
    void runPostResetDiagnostics(
      pid,
      operationId,
      resetCompletedAt,
      transactionTargetStartUtcTicks,
      transactionBridgePid,
    );
  }
  if (!ok && operation.kind === 'apply') speedSession = null;
  return { ok, safeFallback, reason: ok ? undefined : 'operation_failed', msgs: resps };
}

export async function applyGameSpeed(
  pid: number,
  factor: number,
  target?: SpeedTargetIdentity,
  source: SpeedOperationSource = 'user-factor',
): Promise<SpeedResult> {
  if (!Number.isFinite(factor) || factor <= 0 || factor > 16) return { ok: true, skipped: true, safeFallback: true, reason: 'invalid_factor', msgs: ['Invalid speed factor; kept at 1x'] };
  if (!Number.isSafeInteger(pid) || pid <= 0) return { ok: true, skipped: true, safeFallback: true, reason: 'valve_target_mismatch', msgs: ['Invalid game PID; kept at 1x'] };
  speedIntentEpoch += 1;
  const admitted = await acquireSpeedValveTarget(pid);
  if ('reason' in admitted) {
    return { ok: true, skipped: true, safeFallback: true, reason: admitted.reason, msgs: ['The requested PID was not admitted by the game valve; kept at 1x'] };
  }
  if (target?.processCreated && target.processCreated !== admitted.target.processCreated) {
    return { ok: true, skipped: true, safeFallback: true, reason: 'stale_target', msgs: ['The requested process instance is stale; kept at 1x'] };
  }
  // The caller-provided PID is only an expected value. The actual control
  // target and all identity metadata come from the valve response above.
  const valveTarget = admitted.target;
  const blocked = await blockedTargetReason(valveTarget);
  if (blocked) return { ok: true, skipped: true, safeFallback: true, reason: 'blocked_target', msgs: [`Blocked by compatibility rule: ${blocked}; kept at 1x`] };
  const request: SpeedRequestContext = {
    id: ++speedOperationSequence,
    source,
    requestedAt: Date.now(),
    intentEpoch: speedIntentEpoch,
  };
  await log(
    `QUEUE diagVersion=4 appSession=${speedDiagnosticSession} op=${request.id} source=${source} ` +
    `pid=${valveTarget.pid} processCreated=${valveTarget.processCreated} action=apply:${factor}`,
  );
  return enqueueSpeedOperation(() => speedOp(valveTarget.pid, { kind: 'apply', factor }, valveTarget, request));
}

export async function clearGameSpeed(
  pid: number,
  source: SpeedOperationSource = 'unknown',
): Promise<SpeedResult> {
  if (!Number.isSafeInteger(pid) || pid <= 0) return { ok: true, skipped: true, safeFallback: true, reason: 'valve_target_mismatch', msgs: ['Invalid game PID; kept at 1x'] };
  const sessionTarget = speedSession?.target || null;
  if (!sessionTarget || sessionTarget.pid !== pid || !sessionTarget.processCreated) {
    return { ok: true, skipped: true, safeFallback: true, reason: 'valve_target_mismatch', msgs: ['No matching valve-admitted speed session; kept at 1x'] };
  }
  const cleanupOldTarget = source === 'target-change';
  const current = cleanupOldTarget ? null : await detectGame(true).catch(() => null);
  const currentTarget = current ? valveTargetFromGame(current) : null;
  const sameAsValve = !!currentTarget && sameSpeedTarget(sessionTarget, currentTarget);
  if (!cleanupOldTarget && !sameAsValve) {
    return { ok: true, skipped: true, safeFallback: true, reason: 'valve_target_mismatch', msgs: ['The current valve target is different; kept at 1x'] };
  }
  // Target-change cleanup may use the saved old instance only. It never
  // re-elects a process by PID; the PowerShell transaction checks its saved
  // creation time before sending any OpenSpeedy command.
  const clearTarget = cleanupOldTarget ? sessionTarget : currentTarget!;
  const request: SpeedRequestContext = {
    id: ++speedOperationSequence,
    source,
    requestedAt: Date.now(),
    intentEpoch: speedIntentEpoch,
  };
  await log(
    `QUEUE diagVersion=4 appSession=${speedDiagnosticSession} op=${request.id} source=${source} ` +
    `pid=${clearTarget.pid} processCreated=${clearTarget.processCreated} action=clear`,
  );
  return enqueueSpeedOperation(() => speedOp(clearTarget.pid, { kind: 'clear' }, clearTarget, request));
}
