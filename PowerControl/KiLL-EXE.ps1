# Compatibility helper for the gamepad close shortcut.
# The native game recognition valve is the only process selector.  Return the
# exact admitted PID; never choose a new process by working-set size/name.
$ErrorActionPreference = 'SilentlyContinue'
$gate = 'C:\SOFT\YeMan\PowerControl\game-target.json'
try {
  if (-not (Test-Path -LiteralPath $gate)) { exit 0 }
  $target = Get-Content -LiteralPath $gate -Raw | ConvertFrom-Json
  $targetPid = [int]$target.pid
  $created = [Int64]$target.processCreated
  $generation = [Int64]$target.generation
  $age = [DateTimeOffset]::Now.ToUnixTimeMilliseconds() - [Int64]$target.lastSeen
  if (-not $target.valid -or $targetPid -le 0 -or $created -le 0 -or
      $generation -le 0 -or $age -lt 0 -or $age -gt 10000) { exit 0 }
  $process = Get-Process -Id $targetPid
  if (-not $process -or [Int64]$process.StartTime.ToFileTimeUtc() -ne $created) { exit 0 }
  Write-Output $targetPid
} catch { }
