# Compatibility reader for the native game recognition valve.
# It intentionally emits at most one PID and never enumerates candidates.
$ErrorActionPreference = 'SilentlyContinue'
$gate = 'C:\SOFT\YeMan\PowerControl\game-target.json'
try {
  if (-not (Test-Path -LiteralPath $gate)) { exit 0 }
  $target = Get-Content -LiteralPath $gate -Raw | ConvertFrom-Json
  $targetPid = [int]$target.pid
  $created = [Int64]$target.processCreated
  $lastSeen = [Int64]$target.lastSeen
  $generation = [Int64]$target.generation
  $age = [DateTimeOffset]::Now.ToUnixTimeMilliseconds() - $lastSeen
  if (-not $target.valid -or $targetPid -le 0 -or $created -le 0 -or
      $generation -le 0 -or $age -lt 0 -or $age -gt 10000) { exit 0 }
  $process = Get-Process -Id $targetPid
  if ([Int64]$process.StartTime.ToFileTimeUtc() -ne $created) { exit 0 }
  $path = [string]$target.path
  if (-not $path) { try { $path = $process.Path } catch {} }
  if ($path -and $path -like '*.exe') {
    Write-Output (('{0}|{1}|{2}|{3}|{4}' -f $targetPid, $process.ProcessName,
      $process.MainWindowTitle, $path, $process.WorkingSet64))
  }
} catch { }
