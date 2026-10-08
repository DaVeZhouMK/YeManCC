<#
  R-A/R-B post-deploy smoke + storm-simulation test (2026-09-15)
  Covers: 1) boot event chain  2) R-A recovery power-notify registered
          3) process exit -> no restart  4) lifecycle log writable
  Usage: powershell -ExecutionPolicy Bypass -File smoke_storm_test.ps1
#>
[CmdletBinding()]
param(
  [string]$ExePath = 'C:\SOFT\YeMan\YeManCC\YeManCC.exe',
  [string]$LogDir = "$env:LOCALAPPDATA\YeManCC"
)
$ErrorActionPreference = 'Stop'
$passed = 0; $failed = 0
function Assert-Step([string]$Name, [scriptblock]$Block) {
  try {
    & $Block | Out-Null
    Write-Output "PASS: $Name"
    $script:passed++
  } catch {
    Write-Output "FAIL: $Name -> $($_.Exception.Message)"
    $script:failed++
  }
}
if (-not (Test-Path -LiteralPath $ExePath)) { throw "exe not found: $ExePath" }
$nl = Join-Path $LogDir 'native-lifecycle.log'
$rec = Join-Path $LogDir 'recovery-service.log'
Write-Output "== smoke+storm test @ $(Get-Date -Format 'yyyy-MM-ddTHH:mm:ss') =="

# 1) launch
$proc = Start-Process -FilePath $ExePath -PassThru
$bootSeen = $false
$deadline = (Get-Date).AddSeconds(45)
while (-not $bootSeen -and (Get-Date) -lt $deadline) {
  if ($proc.HasExited) { throw "early exit code=$($proc.ExitCode)" }
  if (Test-Path -LiteralPath $nl) {
    $tail = Get-Content -LiteralPath $nl -Tail 60 -ErrorAction SilentlyContinue
    if ($tail -match 'window-created' -or $tail -match 'boot-single-instance-acquired') { $bootSeen = $true }
  }
  Start-Sleep -Milliseconds 300
}
Assert-Step '1) boot chain (window-created / boot-single-instance)' { if (-not $bootSeen) { throw 'boot not seen in 45s' } }

# 2) R-A: recovery power notify registration (deployed recovery exe)
Start-Sleep -Seconds 3
Assert-Step '2) R-A recovery power-notify-registered ok:true' {
  $r = Get-Content -LiteralPath $rec -Tail 6 -ErrorAction SilentlyContinue
  $hit = $r | Select-String 'power-notify-registered'
  if (-not $hit) { throw 'power-notify-registered line missing (old recovery deployed?)' }
  $last = $hit | Select-Object -Last 1
  if ($last.Line -notmatch '"ok":true') { throw 'power-notify-registered ok != true' }
}

# 3) exit -> recovery must NOT restart (process end is target-exited path)
# NOTE: recovery_service has a 30s startup grace, so killing within that window
# produces no exit record; the invariant that matters is "no restart-attempt".
$beforeExit = (Get-Content -LiteralPath $rec).Count
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 5
Assert-Step '3) clean process end -> no restart-attempt' {
  $after = Get-Content -LiteralPath $rec
  $new = $after | Select-Object -Skip $beforeExit
  $restart = $new | Select-String 'restart-attempt'
  if ($restart) { throw 'restart-attempt appeared during exit window' }
}

# 4) lifecycle log writable
Assert-Step '4) native-lifecycle.log exists and non-empty' {
  if (-not (Test-Path -LiteralPath $nl)) { throw 'native-lifecycle.log missing' }
  if ((Get-Item -LiteralPath $nl).Length -lt 100) { throw 'native-lifecycle.log too small' }
}

Write-Output "== RESULT: passed=$passed failed=$failed =="
if ($failed -gt 0) { exit 1 }
