# fanhost_thread_diag.ps1  (read-only, no admin required)
#
# Two modes, one entry point (FAN-931 P0: "reuse the existing capability, do not
# invent a resident monitor"):
#
#  -Mode threads  (default, legacy): samples every OS thread's CPU time + start
#                 time every IntervalMs and writes a CSV to the Desktop.
#  -Mode cpu      (FAN-931 P0): a **bounded, auto-terminating** CPU attribution
#                 capture for one scenario, over an explicitly attributed target
#                 set (never "every WebView child belongs to Fan"):
#                   * targets: Host (required) + native (the parent the Host
#                     itself declares via GET /health) + WebView2 renderers that
#                     are **confirmed descendants of that native pid**; anything
#                     whose ancestry cannot be confirmed is reported as
#                     not-attributed instead of being silently added
#                   * binds pid + process start time + executable hash per target
#                     (hash only at the first and last sample - never a
#                     per-sample full scan); Host instance id from the Host
#                   * 1 s granularity, cumulative user/kernel CPU deltas against a
#                     monotonic clock, default 120 s and it ends by itself
#                   * reports ms CPU/s, machine-normalized % (processor count is
#                     normalized, per the FAN-931 recomputation), logical
#                     processor count, mean and windowed tail - per target and
#                     summed, with the unattributed remainder called out
#                   * NOT subject to the Host's 1% thread-attribution threshold
#                   * optional on-CPU stack capture (dotnet-trace) for 30-60 s;
#                     without the tooling the report says "not-attributed"
#                   * same-monotonic-timeline cadence correlation for 1/2/5/30 s
#                   * request-entry aggregate via GET /api/perf-counters
#                     (start/end delta) + the **sampler's own CPU** and an
#                     optional no-sampling control window, so the measurement
#                     overhead is published instead of assumed
#                     Never records the session token or lease secrets.
#
# Examples:
#   # legacy thread CSV
#   powershell -NoProfile -ExecutionPolicy Bypass -File fanhost_thread_diag.ps1
#   # bounded attribution capture of an already-running Host
#   powershell -NoProfile -ExecutionPolicy Bypass -File fanhost_thread_diag.ps1 `
#       -Mode cpu -HostPid 1234 -Seconds 120 -PerfBaseUrl http://127.0.0.1:8765 `
#       -SessionTokenFile "$env:TEMP\fan.session" -OutDir "$env:TEMP\fan931"
#   # bounded capture of a Host this tool launches itself (isolated instance)
#   powershell -NoProfile -ExecutionPolicy Bypass -File fanhost_thread_diag.ps1 `
#       -Mode cpu -HostExePath .\YeManFanHost.exe -HostArgs @('--port','8799','--mock-handshake') ...
param(
  [ValidateSet('threads', 'cpu')]
  [string]$Mode = 'threads',
  [int]$Seconds = 120,
  [int]$IntervalMs = 2000,
  [string]$OutFile = '',
  # ---- -Mode cpu ----
  [int]$HostPid = 0,
  [string]$HostExePath = '',
  [string[]]$HostArgs = @(),
  [string]$OutDir = '',
  [string]$PerfBaseUrl = '',
  [string]$SessionTokenFile = '',
  # 0 = do not attempt an on-CPU stack capture.
  [int]$StackSeconds = 0,
  # Thread CPU deltas taken at the first/last sample plus at most this many
  # intermediate samples (bounded work, independent of the 1% gate).
  [int]$MaxThreadSnapshots = 4,
  # Explicit native (fan-owning app) pid. Default: the parentPid the Host itself
  # declares through GET /health (authoritative - not a name search).
  [int]$NativePid = 0,
  # After the capture, sleep this many seconds doing nothing but the 1 s clock and
  # report the sampler's own CPU for that window as the no-sampling control.
  [int]$SamplerOverheadControlSeconds = 0
)
$ErrorActionPreference = 'Stop'

if ($Mode -eq 'threads') {
  # ---------------------------------------------------------------------------
  # Legacy thread sampler (unchanged behaviour).
  # ---------------------------------------------------------------------------
  if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
    $OutFile = Join-Path ([Environment]::GetFolderPath('Desktop')) ("fanhost-thread-diag-$stamp.csv")
  }
  $header = "# machine=$env:COMPUTERNAME started=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') seconds=$Seconds intervalMs=$IntervalMs"
  $header | Out-File -Encoding ASCII $OutFile
  "tick,time,pid,procCpuMs,thrCount,threads(tid:cpuMs:startedAt:state)" | Out-File -Encoding ASCII -Append $OutFile
  Write-Host "saving to: $OutFile"
  Write-Host "READY - now start YMCC, turn the fan on, then click the virtual gamepad switch."

  $deadline = (Get-Date).AddSeconds($Seconds)
  $tick = 0
  $sawProcess = $false
  while ((Get-Date) -lt $deadline) {
    $tick++
    $procs = Get-Process -Name YeManFanHost -ErrorAction SilentlyContinue
    if ($procs) { $sawProcess = $true }
    else { Write-Host ("{0} tick {1}: YeManFanHost not running" -f (Get-Date).ToString('HH:mm:ss'), $tick) }
    foreach ($p in $procs) {
      $parts = @()
      foreach ($t in $p.Threads) {
        $cpu = 'na'
        try { $cpu = [int]$t.TotalProcessorTime.TotalMilliseconds } catch { }
        $st = 'na'
        try { $st = $t.StartTime.ToString('HH:mm:ss.fff') } catch { }
        $parts += ("{0}:{1}:{2}:{3}" -f $t.Id, $cpu, $st, $t.ThreadState)
      }
      $pc = 'na'
      try { $pc = [int]($p.CPU * 1000) } catch { }
      $line = "{0},{1},{2},{3},{4},{5}" -f $tick, (Get-Date).ToString('HH:mm:ss.fff'), $p.Id, $pc, $p.Threads.Count, ($parts -join ';')
      Add-Content -Encoding ASCII -Path $OutFile -Value $line
      Write-Host ("{0} pid={1} cpu={2}ms threads={3}" -f (Get-Date).ToString('HH:mm:ss'), $p.Id, $pc, $p.Threads.Count)
    }
    Start-Sleep -Milliseconds $IntervalMs
  }
  if (-not $sawProcess) { "NOTE: YeManFanHost never appeared during this run." | Add-Content -Encoding ASCII -Path $OutFile }
  "done" | Add-Content -Encoding ASCII -Path $OutFile
  Write-Host ""
  Write-Host "DONE. Send this file back: $OutFile"
  exit 0
}

# -----------------------------------------------------------------------------
# -Mode cpu : bounded attribution capture (FAN-931 P0)
# -----------------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($OutDir)) {
  $OutDir = Join-Path ([Environment]::GetFolderPath('Desktop')) ("fanhost-cpu-capture-" + (Get-Date).ToString('yyyyMMdd-HHmmss'))
}
$null = New-Item -ItemType Directory -Force -Path $OutDir
$samplesCsv = Join-Path $OutDir 'cpu-samples.csv'
$summaryJson = Join-Path $OutDir 'cpu-summary.json'
$threadsCsv = Join-Path $OutDir 'thread-deltas.csv'

Add-Type -Namespace Yc931 -Name K32 -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern bool GetProcessTimes(IntPtr hProcess, out long creation, out long exit, out long kernel, out long user);
'@ -ErrorAction Stop

function Get-CumulativeCpu {
  param([System.Diagnostics.Process]$Process)
  $result = [ordered]@{ ok = $false; userMs = $null; kernelMs = $null; totalMs = $null }
  try {
    $creation = 0L; $exit = 0L; $kernel = 0L; $user = 0L
    if ([Yc931.K32]::GetProcessTimes($Process.Handle, [ref]$creation, [ref]$exit, [ref]$kernel, [ref]$user)) {
      $result.ok = $true
      $result.userMs = [double]$user / 10000.0
      $result.kernelMs = [double]$kernel / 10000.0
      $result.totalMs = $result.userMs + $result.kernelMs
    }
  } catch { }
  if (-not $result.ok) {
    # Fall back to the managed counter (total only) rather than reporting zero.
    try {
      $result.ok = $true
      $result.totalMs = $Process.TotalProcessorTime.TotalMilliseconds
      $result.userMs = $null
      $result.kernelMs = $null
    } catch { }
  }
  return $result
}

function Get-SelfCpuMs {
  # The sampler's own CPU, read with the same API as the targets.
  try {
    $me = [System.Diagnostics.Process]::GetCurrentProcess()
    $c = Get-CumulativeCpu -Process $me
    if ($c.ok -and $null -ne $c.totalMs) { return [double]$c.totalMs }
  } catch { }
  return $null
}

function Get-ThreadSnapshot {
  param([System.Diagnostics.Process]$Process)
  $rows = @{}
  try {
    foreach ($t in $Process.Threads) {
      $ms = $null
      try { $ms = [double]$t.TotalProcessorTime.TotalMilliseconds } catch { }
      $started = 'na'
      try { $started = $t.StartTime.ToString('HH:mm:ss.fff') } catch { }
      $rows[[int]$t.Id] = [pscustomobject]@{ tid = [int]$t.Id; cpuMs = $ms; startedAt = $started; state = [string]$t.ThreadState }
    }
  } catch { }
  return $rows
}

function Get-PerfCounters {
  param([string]$BaseUrl, [string]$Token)
  if ([string]::IsNullOrWhiteSpace($BaseUrl)) { return $null }
  try {
    $headers = @{}
    if (-not [string]::IsNullOrWhiteSpace($Token)) { $headers['X-YeMan-Fan-Session'] = $Token }
    return Invoke-RestMethod -Uri ($BaseUrl.TrimEnd('/') + '/api/perf-counters') -Headers $headers -TimeoutSec 3
  } catch { return $null }
}

function Get-Health {
  param([string]$BaseUrl, [string]$Token)
  if ([string]::IsNullOrWhiteSpace($BaseUrl)) { return $null }
  try {
    $headers = @{}
    if (-not [string]::IsNullOrWhiteSpace($Token)) { $headers['X-YeMan-Fan-Session'] = $Token }
    return Invoke-RestMethod -Uri ($BaseUrl.TrimEnd('/') + '/health') -Headers $headers -TimeoutSec 3
  } catch { return $null }
}

function Get-ProcessTable {
  # ONE bounded Win32_Process snapshot, taken only at bind time (never per sample).
  $table = @{}
  try {
    foreach ($row in (Get-CimInstance Win32_Process -ErrorAction Stop)) {
      $table[[int]$row.ProcessId] = [pscustomobject]@{
        pid = [int]$row.ProcessId; ppid = [int]$row.ParentProcessId; name = [string]$row.Name; cmd = [string]$row.CommandLine
      }
    }
  } catch { }
  return $table
}

function Get-Descendants {
  param([hashtable]$Table, [int]$RootPid)
  $children = @{}
  foreach ($k in $Table.Keys) {
    $p = $Table[$k].ppid
    if (-not $children.ContainsKey($p)) { $children[$p] = New-Object System.Collections.Generic.List[int] }
    $children[$p].Add($k)
  }
  $out = New-Object System.Collections.Generic.List[int]
  $queue = New-Object System.Collections.Generic.Queue[int]
  $queue.Enqueue($RootPid)
  $guard = 0
  while ($queue.Count -gt 0 -and $guard -lt 4096) {
    $guard++
    $cur = $queue.Dequeue()
    if ($children.ContainsKey($cur)) {
      foreach ($c in $children[$cur]) { $out.Add($c); $queue.Enqueue($c) }
    }
  }
  return $out
}

function New-Target {
  param([int]$ProcessId, [string]$Role, [string]$Why)
  $proc = $null
  try { $proc = Get-Process -Id $ProcessId -ErrorAction Stop } catch { }
  if (-not $proc) { return [pscustomobject]@{ role = $role; pid = $ProcessId; attribution = 'not-attributed'; why = $Why; process = $null } }
  $start = $null
  try { $start = $proc.StartTime.ToUniversalTime().ToString('o') } catch { }
  $image = $null
  try { $image = $proc.MainModule.FileName } catch { }
  return [pscustomobject]@{ role = $role; pid = $ProcessId; attribution = $Why; why = 'ok'; process = $proc; startTimeUtc = $start; imagePath = $image }
}

$launchedProcess = $null
$token = ''
if (-not [string]::IsNullOrWhiteSpace($SessionTokenFile) -and (Test-Path -LiteralPath $SessionTokenFile)) {
  $token = (Get-Content -LiteralPath $SessionTokenFile -Raw).Trim()
}

if ($HostPid -le 0 -and -not [string]::IsNullOrWhiteSpace($HostExePath)) {
  if (-not (Test-Path -LiteralPath $HostExePath -PathType Leaf)) { throw "Host executable not found: $HostExePath" }
  # An isolated, self-owned instance: this tool owns the child and ends it at the
  # end of the capture window. -HostArgs must carry the safe/mock switches.
  $launchedProcess = Start-Process -FilePath $HostExePath -ArgumentList $HostArgs -PassThru -WindowStyle Hidden
  $HostPid = $launchedProcess.Id
  Write-Host "launched isolated Host pid=$HostPid"
}

$hostProcess = $null
if ($HostPid -gt 0) {
  # Bind once (pid + creation time + image). No per-sample name scan of the machine.
  for ($i = 0; $i -lt 60; $i++) {
    $hostProcess = Get-Process -Id $HostPid -ErrorAction SilentlyContinue
    if ($hostProcess) { break }
    Start-Sleep -Milliseconds 250
  }
  if (-not $hostProcess) { throw "Host pid $HostPid not found" }
} else {
  # One-shot discovery is allowed, but never once per sample.
  $candidates = @(Get-Process -Name YeManFanHost -ErrorAction SilentlyContinue)
  if ($candidates.Count -ne 1) { throw "expected exactly one YeManFanHost to attach to, found $($candidates.Count); pass -HostPid" }
  $hostProcess = $candidates[0]
  $HostPid = $hostProcess.Id
}

$processorCount = [Environment]::ProcessorCount
$perfFirst = Get-PerfCounters -BaseUrl $PerfBaseUrl -Token $token
$health = Get-Health -BaseUrl $PerfBaseUrl -Token $token

# ---- attribution: Host (required) / native (Host-declared parent) / confirmed renderers ----
$targets = New-Object System.Collections.Generic.List[object]
$hostTarget = New-Target -ProcessId $HostPid -Role 'host' -Why 'bound (explicit pid or single named process)'
$targets.Add($hostTarget)

$attributionNotes = New-Object System.Collections.Generic.List[string]
$nativePidResolved = 0
if ($NativePid -gt 0) {
  $nativePidResolved = $NativePid
  $attributionNotes.Add('native pid supplied explicitly (-NativePid)')
} elseif ($health -and $health.parentPid) {
  $nativePidResolved = [int]$health.parentPid
  $attributionNotes.Add('native pid taken from the Host''s own GET /health parentPid (authoritative, not a name search)')
} else {
  $attributionNotes.Add('native pid unavailable (no /health parentPid and no -NativePid) => native not-attributed')
}
$nativeTarget = $null
if ($nativePidResolved -gt 0) {
  $nativeTarget = New-Target -ProcessId $nativePidResolved -Role 'native' -Why 'declared parent of the Host'
  $targets.Add($nativeTarget)
}

# WebView2 processes are only attributed when their ancestry is CONFIRMED to be the
# native app. Otherwise nothing is attributed - all WebView children of the machine
# are never folded into "Fan".
$rendererTargets = New-Object System.Collections.Generic.List[object]
if ($null -ne $nativeTarget -and $nativeTarget.process) {
  $table = Get-ProcessTable
  if ($table.Count -gt 0 -and $table.ContainsKey($nativePidResolved)) {
    $descendants = Get-Descendants -Table $table -RootPid $nativePidResolved
    $webview = @($descendants | Where-Object { $table[$_].name -ieq 'msedgewebview2.exe' })
    foreach ($pid2 in $webview) {
      $cmd = $table[$pid2].cmd
      $isRenderer = ($cmd -match '--type=renderer')
      $role = if ($isRenderer) { 'renderer' } else { 'webview-other' }
      $t = New-Target -ProcessId $pid2 -Role $role -Why 'confirmed descendant of the native pid (Win32_Process ancestry, one-shot)'
      $rendererTargets.Add($t)
      $targets.Add($t)
    }
    $attributionNotes.Add("webview2 descendants of native pid $nativePidResolved : $($webview.Count) (renderer=$(@($webview | Where-Object { $table[$_].cmd -match '--type=renderer' }).Count))")
  } else {
    $attributionNotes.Add('Win32_Process snapshot unavailable or native pid absent => webview processes NOT attributed')
  }
} else {
  $attributionNotes.Add('native not bound => webview processes NOT attributed')
}

$boundTargets = @($targets | Where-Object { $null -ne $_.process })
Write-Host ("targets: " + (($boundTargets | ForEach-Object { "$($_.role)=$($_.pid)" }) -join ', '))
foreach ($note in $attributionNotes) { Write-Host "  attribution: $note" }

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$stackJob = $null
$stackTool = ''
$stackNote = 'not-attributed'
if ($StackSeconds -gt 0) {
  $trace = Get-Command dotnet-trace -ErrorAction SilentlyContinue
  $stack = Get-Command dotnet-stack -ErrorAction SilentlyContinue
  if ($trace) {
    $stackTool = $trace.Source
    $stackJob = Start-Process -FilePath $stackTool -ArgumentList @('collect', '--process-id', [string]$HostPid, '--duration', ("00:00:{0:d2}" -f $StackSeconds), '--profile', 'cpu-sampling', '--output', (Join-Path $OutDir 'on-cpu.nettrace')) -PassThru -WindowStyle Hidden
    $stackNote = "dotnet-trace cpu-sampling attempted ($StackSeconds s)"
  } elseif ($stack) {
    $stackTool = $stack.Source
    $stackNote = 'dotnet-stack present but only dotnet-trace gives an on-CPU sample profile'
  } else {
    $stackNote = 'not-attributed (no dotnet-trace/dotnet-stack on PATH)'
  }
}

# One held writer per CSV: Out-File -Append reopens the file for every row, and the
# sampler's own CPU is itself a reported measurement (FAN-931 P0), so it must not
# dominate the thing it observes.
$samplesWriter = New-Object System.IO.StreamWriter($samplesCsv, $false, [System.Text.Encoding]::ASCII)
$threadsWriter = New-Object System.IO.StreamWriter($threadsCsv, $false, [System.Text.Encoding]::ASCII)
$samplesWriter.WriteLine('tick,monotonicMs,wallUtc,role,pid,userMs,kernelMs,totalMs,threadCount,present')
$threadsWriter.WriteLine('tick,monotonicMs,tid,deltaMs,totalMs,startedAt,state,isNew')

$lastThreads = Get-ThreadSnapshot -Process $hostProcess
$lastMonotonic = $stopwatch.Elapsed.TotalMilliseconds
$selfCpuStart = Get-SelfCpuMs
$deadlineMs = $Seconds * 1000
$samples = New-Object System.Collections.Generic.List[object]
$tick = 0

Write-Host "capturing $($boundTargets.Count) target(s) for $Seconds s (1 s granularity, auto-end). Do NOT change the scenario mid-window."
while ($stopwatch.Elapsed.TotalMilliseconds -lt $deadlineMs) {
  $tick++
  $now = $stopwatch.Elapsed.TotalMilliseconds
  $wall = (Get-Date).ToUniversalTime().ToString('o')
  # Thread-count and per-thread CPU both need a Toolhelp snapshot, so both are taken
  # only on the bounded cadence (first/last + a few intermediates) - never per tick,
  # which used to make the sampler itself the most expensive process on the machine.
  $intervalMs = $now - $lastMonotonic
  $takeThreadSnapshot = ($intervalMs -ge (($Seconds * 1000.0) / 4.0))
  foreach ($t in $boundTargets) {
    $present = $true
    $proc = $t.process
    try { if ($proc.HasExited) { $present = $false } } catch { $present = $false }
    $cpu = if ($present) { Get-CumulativeCpu -Process $proc } else { [ordered]@{ ok = $false; userMs = $null; kernelMs = $null; totalMs = $null } }
    $threadCount = 'na'
    if ($present -and $takeThreadSnapshot -and $t.pid -eq $HostPid) {
      try { $threadCount = $proc.Threads.Count } catch { }
    }
    $samples.Add([pscustomobject]@{
      tick = $tick; monotonicMs = [Math]::Round($now, 1); wallUtc = $wall; role = $t.role; pid = $t.pid
      userMs = $cpu.userMs; kernelMs = $cpu.kernelMs; totalMs = $cpu.totalMs; threadCount = $threadCount; present = $present
    })
    $samplesWriter.WriteLine(("{0},{1},{2},{3},{4},{5},{6},{7},{8},{9}" -f $tick, ([Math]::Round($now, 1)), $wall, $t.role, $t.pid,
      $cpu.userMs, $cpu.kernelMs, $cpu.totalMs, $threadCount, $present))
  }

  # Host thread deltas: first/last plus a bounded number of intermediates.
  if ($takeThreadSnapshot) {
    $snap = Get-ThreadSnapshot -Process $hostProcess
    foreach ($tid in $snap.Keys) {
      $prev = $lastThreads[$tid]
      $delta = $null
      if ($prev -and $null -ne $prev.cpuMs -and $null -ne $snap[$tid].cpuMs) { $delta = [Math]::Round($snap[$tid].cpuMs - $prev.cpuMs, 1) }
      $threadsWriter.WriteLine(("{0},{1},{2},{3},{4},{5},{6},{7}" -f $tick, ([Math]::Round($now, 1)), $tid, $delta, $snap[$tid].cpuMs, $snap[$tid].startedAt, $snap[$tid].state, ($null -eq $prev)))
    }
    $lastThreads = $snap
  }
  $lastMonotonic = $now
  Start-Sleep -Milliseconds 1000
}
$selfCpuEnd = Get-SelfCpuMs
$captureWallMs = $stopwatch.Elapsed.TotalMilliseconds

# Final Host thread snapshot (first/last pair is the bounded minimum).
$finalThreads = Get-ThreadSnapshot -Process $hostProcess
foreach ($tid in $finalThreads.Keys) {
  $prev = $lastThreads[$tid]
  $delta = $null
  if ($prev -and $null -ne $prev.cpuMs -and $null -ne $finalThreads[$tid].cpuMs) { $delta = [Math]::Round($finalThreads[$tid].cpuMs - $prev.cpuMs, 1) }
  $threadsWriter.WriteLine(("{0},{1},{2},{3},{4},{5},{6},{7}" -f $tick, ([Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 1)), $tid, $delta, $finalThreads[$tid].cpuMs, $finalThreads[$tid].startedAt, $finalThreads[$tid].state, ($null -eq $prev)))
}
$samplesWriter.Flush(); $samplesWriter.Dispose()
$threadsWriter.Flush(); $threadsWriter.Dispose()

# ---- no-sampling control window: the sampler's own floor cost ----
$controlCpuMs = $null
if ($SamplerOverheadControlSeconds -gt 0) {
  $controlStart = Get-SelfCpuMs
  $controlBegin = $stopwatch.Elapsed.TotalMilliseconds
  while (($stopwatch.Elapsed.TotalMilliseconds - $controlBegin) -lt ($SamplerOverheadControlSeconds * 1000)) {
    Start-Sleep -Milliseconds 1000
  }
  $controlEnd = Get-SelfCpuMs
  if ($null -ne $controlStart -and $null -ne $controlEnd) {
    $controlCpuMs = [Math]::Round($controlEnd - $controlStart, 1)
  }
}

$perfLast = Get-PerfCounters -BaseUrl $PerfBaseUrl -Token $token

if ($stackJob -and -not $stackJob.HasExited) {
  try { if (-not $stackJob.WaitForExit(($StackSeconds + 15) * 1000)) { Stop-Process -Id $stackJob.Id -Force -ErrorAction SilentlyContinue } } catch { }
}
if ($launchedProcess -and -not $launchedProcess.HasExited) {
  # The tool owns this instance; end it so the capture does not leave a process.
  try { Stop-Process -Id $launchedProcess.Id -Force -ErrorAction SilentlyContinue } catch { }
}

# ---- per-target windows: mean + windowed tail over ~30 s windows ----
function Get-TargetSummary {
  param([int]$ProcessId, [object[]]$Rows, [int]$Processors)
  $rows = @($Rows | Where-Object { $_.pid -eq $ProcessId -and $null -ne $_.totalMs })
  if ($rows.Count -eq 0) { return $null }
  $windows = New-Object System.Collections.Generic.List[object]
  $totalCpuMs = 0.0; $totalMs = 0.0
  $windowStart = $rows[0]; $windowCpuStart = $rows[0].totalMs
  for ($i = 1; $i -lt $rows.Count; $i++) {
    $s = $rows[$i]
    $elapsed = $s.monotonicMs - $windowStart.monotonicMs
    if ($elapsed -ge 30000 -or $i -eq ($rows.Count - 1)) {
      $cpuDelta = $s.totalMs - $windowCpuStart
      $windows.Add([pscustomobject]@{
        startMonotonicMs = $windowStart.monotonicMs; endMonotonicMs = $s.monotonicMs
        intervalMs = [Math]::Round($elapsed, 1); cpuMs = [Math]::Round($cpuDelta, 1)
        cpuMsPerSec = if ($elapsed -gt 0) { [Math]::Round($cpuDelta / ($elapsed / 1000.0), 3) } else { $null }
        normalizedPercent = if ($elapsed -gt 0) { [Math]::Round($cpuDelta / $elapsed / $Processors * 100.0, 5) } else { $null }
      })
      $totalCpuMs += $cpuDelta; $totalMs += $elapsed
      $windowStart = $s; $windowCpuStart = $s.totalMs
    }
  }
  $maxWindow = $null
  foreach ($w in $windows) { if ($null -eq $maxWindow -or $w.normalizedPercent -gt $maxWindow.normalizedPercent) { $maxWindow = $w } }
  return [ordered]@{
    samples = $rows.Count; coveredMs = [Math]::Round($totalMs, 1); totalCpuMs = [Math]::Round($totalCpuMs, 1)
    meanMsPerSec = if ($totalMs -gt 0) { [Math]::Round($totalCpuMs / ($totalMs / 1000.0), 3) } else { $null }
    meanNormalizedPercent = if ($totalMs -gt 0) { [Math]::Round($totalCpuMs / $totalMs / $Processors * 100.0, 5) } else { $null }
    windows = $windows
    maxWindow = $maxWindow
  }
}

$allRows = $samples.ToArray()
$targetSummaries = New-Object System.Collections.Generic.List[object]
foreach ($t in $boundTargets) {
  $s = Get-TargetSummary -ProcessId $t.pid -Rows $allRows -Processors $processorCount
  $hashFirst = 'unavailable'; $hashLast = 'unavailable'
  if ($t.imagePath) {
    if (Test-Path -LiteralPath $t.imagePath -PathType Leaf) {
      try { $hashFirst = (Get-FileHash -LiteralPath $t.imagePath -Algorithm SHA256).Hash } catch { }
    }
  }
  $hashLast = $hashFirst
  $targetSummaries.Add([ordered]@{
    role = $t.role; pid = $t.pid; attribution = $t.attribution
    startTimeUtc = $t.startTimeUtc; imagePath = $t.imagePath
    imageSha256First = $hashFirst; imageSha256Last = $hashLast
    hashStableDuringCapture = ($hashFirst -eq $hashLast)
    cpu = $s
  })
}

$attributedTotalMsPerSec = 0.0
foreach ($ts in $targetSummaries) { if ($ts.cpu -and $ts.cpu.meanMsPerSec) { $attributedTotalMsPerSec += $ts.cpu.meanMsPerSec } }

# ---- same-monotonic-timeline cadence correlation for the Host (candidate, not a claim) ----
function Get-MeanValue([object[]]$Values) { if ($Values.Count -eq 0) { return $null } ; return [Math]::Round((($Values | Measure-Object -Average).Average), 3) }
$hostRows = @($allRows | Where-Object { $_.pid -eq $HostPid -and $null -ne $_.totalMs })
$cadence = [ordered]@{}
foreach ($d in @(1, 2, 5, 30)) {
  $periodMs = $d * 1000
  $onBoundary = @(); $offBoundary = @()
  for ($i = 1; $i -lt $hostRows.Count; $i++) {
    $a = $hostRows[$i - 1]; $b = $hostRows[$i]
    $dt = $b.monotonicMs - $a.monotonicMs
    if ($dt -le 0) { continue }
    $rate = ($b.totalMs - $a.totalMs) / ($dt / 1000.0)
    $offset = $b.monotonicMs % $periodMs
    if ($offset -lt 500 -or $offset -gt ($periodMs - 500)) { $onBoundary += $rate } else { $offBoundary += $rate }
  }
  $cadence["${d}s"] = [pscustomobject]@{
    onBoundarySamples = $onBoundary.Count; onBoundaryMeanMsPerSec = Get-MeanValue $onBoundary
    offBoundarySamples = $offBoundary.Count; offBoundaryMeanMsPerSec = Get-MeanValue $offBoundary
    note = 'candidate correlation only: a shared timestamp is not a cause'
  }
}

# ---- perf-counter delta (no secrets recorded) ----
$counterDelta = $null
if ($perfFirst -and $perfLast -and $perfFirst.hostInstanceId -eq $perfLast.hostInstanceId) {
  $counterDelta = [ordered]@{ hostInstanceId = $perfFirst.hostInstanceId
    diagnosticContextBuilds = $perfLast.counters.diagnosticContextBuilds - $perfFirst.counters.diagnosticContextBuilds
    staVerifyRequests = $perfLast.counters.staVerifyRequests - $perfFirst.counters.staVerifyRequests
    parentIdentityReads = $perfLast.counters.parentIdentityReads - $perfFirst.counters.parentIdentityReads
    loopStopAttempts = $perfLast.counters.loopStopAttempts - $perfFirst.counters.loopStopAttempts
    loopStopRequests = $perfLast.counters.loopStopRequests - $perfFirst.counters.loopStopRequests
    loopStopAlreadyPending = $perfLast.counters.loopStopAlreadyPending - $perfFirst.counters.loopStopAlreadyPending
    runtimeSamplesRecorded = $perfLast.counters.runtimeSamplesRecorded - $perfFirst.counters.runtimeSamplesRecorded
    runtimeSamplesSkipped = $perfLast.counters.runtimeSamplesSkipped - $perfFirst.counters.runtimeSamplesSkipped
    runtimeStatusReads = $perfLast.counters.runtimeStatusReads - $perfFirst.counters.runtimeStatusReads
    requestTotals = @{} }
  foreach ($entry in $perfLast.counters.paths) {
    $firstTotal = 0
    foreach ($f in $perfFirst.counters.paths) { if ($f.path -eq $entry.path) { $firstTotal = $f.total; break } }
    if (($entry.total - $firstTotal) -ne 0) { $counterDelta.requestTotals[$entry.path] = $entry.total - $firstTotal }
  }
}

# sampler self cost
$samplerCpuMs = $null
if ($null -ne $selfCpuStart -and $null -ne $selfCpuEnd) { $samplerCpuMs = [Math]::Round($selfCpuEnd - $selfCpuStart, 1) }

$summary = [ordered]@{
  schemaVersion = 2
  tool = 'fanhost_thread_diag.ps1 -Mode cpu (FAN-931 P0)'
  machine = $env:COMPUTERNAME
  scenarioSeconds = $Seconds
  sampleIntervalMs = 1000
  processorCount = $processorCount
  normalization = 'DeltaProcessCpuMs / intervalMs / processorCount * 100 (interval mean, NOT an instantaneous peak)'
  attribution = [ordered]@{
    policy = 'a WebView2 process is counted only when its ancestry is CONFIRMED to be the Host-declared native pid; otherwise nothing is attributed (never all WebView children of the machine)'
    notes = $attributionNotes
    hostInstanceId = if ($perfFirst) { $perfFirst.hostInstanceId } else { 'unavailable' }
    hostDeclaredParentPid = if ($health) { $health.parentPid } else { $null }
    targets = $targetSummaries
    attributedTotalMsPerSec = [Math]::Round($attributedTotalMsPerSec, 3)
    attributedTotalNormalizedPercent = [Math]::Round($attributedTotalMsPerSec / 1000.0 / $processorCount * 100.0, 5)
    unattributedNote = 'processes outside this target set were NOT measured; the attributed total is a floor, not the machine total'
  }
  cpu = $targetSummaries | Where-Object { $_.role -eq 'host' } | Select-Object -First 1 | ForEach-Object { $_.cpu }
  threadAttribution = [ordered]@{
    thresholdGate = 'bypassed (external reader; not the Host 1% gate)'
    snapshotBudget = 'first/last plus bounded intermediates for the Host only'
    csv = $threadsCsv
  }
  samplerOverhead = [ordered]@{
    samplerSelfCpuMs = $samplerCpuMs
    samplerSelfPercentOfOneCore = if ($null -ne $samplerCpuMs -and $captureWallMs -gt 0) { [Math]::Round($samplerCpuMs / $captureWallMs * 100.0, 3) } else { $null }
    samplerSelfNormalizedPercent = if ($null -ne $samplerCpuMs -and $captureWallMs -gt 0) { [Math]::Round($samplerCpuMs / $captureWallMs / $processorCount * 100.0, 5) } else { $null }
    captureWallMs = [Math]::Round($captureWallMs, 1)
    noSamplingControlSeconds = $SamplerOverheadControlSeconds
    noSamplingControlCpuMs = $controlCpuMs
    note = 'the sampler runs in its own process; compare samplerSelfCpuMs with the target CPU to judge whether the observer perturbs the observation'
  }
  onCpuStacks = [ordered]@{ requestedSeconds = $StackSeconds; tool = $stackTool; result = $stackNote }
  cadenceCorrelation = $cadence
  perfCounterDelta = $counterDelta
  warnings = @(
    'detailed logging on/off changes the absolute numbers; capture both and label them',
    'a capture window that spans an enable/disable/restore/power transition is not a steady-state baseline',
    'a 250 ms finer window was NOT used in this run, so its sampler-overhead protocol is not triggered',
    'without a same-scenario ROG A/B this cannot sign a real-machine CPU reduction'
  )
}
$summary | ConvertTo-Json -Depth 8 | Out-File -Encoding UTF8 $summaryJson

Write-Host ""
foreach ($ts in $targetSummaries) {
  if ($ts.cpu) {
    Write-Host ("{0,-14} pid={1,-7} mean={2,8} ms CPU/s = {3,9}%  maxWindow={4}%" -f $ts.role, $ts.pid, $ts.cpu.meanMsPerSec, $ts.cpu.meanNormalizedPercent, $ts.cpu.maxWindow.normalizedPercent)
  } else {
    Write-Host ("{0,-14} pid={1,-7} {2}" -f $ts.role, $ts.pid, $ts.attribution)
  }
}
Write-Host ("attributed total = {0} ms CPU/s = {1}% of {2} logical processors" -f $summary.attribution.attributedTotalMsPerSec, $summary.attribution.attributedTotalNormalizedPercent, $processorCount)
Write-Host ("sampler self CPU = {0} ms over {1} ms ({2}% of one core); no-sampling control = {3} ms" -f $samplerCpuMs, $summary.samplerOverhead.captureWallMs, $summary.samplerOverhead.samplerSelfPercentOfOneCore, $controlCpuMs)
Write-Host ("samples : {0}" -f $samplesCsv)
Write-Host ("threads : {0}" -f $threadsCsv)
Write-Host ("summary : {0}" -f $summaryJson)
Write-Host "DONE (capture ended by itself; no resident monitor left running)."
