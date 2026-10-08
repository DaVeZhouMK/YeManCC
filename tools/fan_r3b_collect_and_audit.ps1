# fan_r3b_collect_and_audit.ps1 (v1) -- one-command capture + adjudication for the real-device R3B run.
#
# WHY: the R3B evidence you need lives in FOUR places under %LOCALAPPDATA%\YeManCC, and hand-assembling
# them is the most likely way a device session gets wasted. This collects them into one directory,
# runs the adjudicator, and (optionally) zips the result so it can be handed over as-is.
#
# READ-ONLY against the live product: it COPIES out, never edits/deletes anything in %LOCALAPPDATA%.
#
# Usage (right after your device session):
#   powershell -NoProfile -ExecutionPolicy Bypass -File fan_r3b_collect_and_audit.ps1
#   # -> %USERPROFILE%\Desktop\fan-r3b-capture-<timestamp>\  + audit.json + .zip
#
# ASCII only (PS 5.1 reads BOM-less .ps1 as ANSI).
param(
  [string]$OutDir = '',
  [string]$SourceRoot = (Join-Path $env:LOCALAPPDATA 'YeManCC'),
  [string]$AuditorPath = (Join-Path $PSScriptRoot 'fan_ui_free_recovery_log_audit.ps1'),
  [switch]$NoZip
)
$ErrorActionPreference = 'Continue'

if ([string]::IsNullOrWhiteSpace($OutDir)) {
  $stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
  $desktop = [Environment]::GetFolderPath('Desktop')
  if ([string]::IsNullOrWhiteSpace($desktop)) { $desktop = $env:USERPROFILE }
  $OutDir = Join-Path $desktop ('fan-r3b-capture-' + $stamp)
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Write-Output ("source : " + $SourceRoot)
Write-Output ("outdir : " + $OutDir)

# The four evidence locations FAN-926 asks for (see the log checklist).
$wanted = @(
  'fan-host\logs\fan-host-fault.log',
  'fan-host\logs\yeman-fan-host-runtime.log',
  'fan-host\YeManFanHost.recovery-cycle.json',
  'fan-host\YeManFanHost.control-intent.json',
  'fan-host\fan-logging-enabled.flag',
  'fan-lifecycle.log',
  'recovery-service.log'
)
$copied = 0
$missing = New-Object System.Collections.Generic.List[string]
foreach ($rel in $wanted) {
  $src = Join-Path $SourceRoot $rel
  if (-not (Test-Path -LiteralPath $src)) { $missing.Add($rel) | Out-Null; continue }
  $dst = Join-Path $OutDir ([IO.Path]::GetFileName($rel))
  Copy-Item -LiteralPath $src -Destination $dst -Force
  $copied++
  Write-Output ("  copied {0,10} B  {1}" -f (Get-Item -LiteralPath $dst).Length, $rel)
}
Write-Output ("copied=$copied missing=" + $missing.Count)
foreach ($m in $missing) { Write-Output ("  MISSING " + $m) }

# Host the fault log where the adjudicator expects it (it looks for fan-host-fault.log at the root).
$fl = Join-Path $OutDir 'fan-host-fault.log'
if (-not (Test-Path -LiteralPath $fl)) {
  Write-Output 'FAIL: fan-host-fault.log was not captured - without it R3B cannot be judged'
  exit 2
}

# Snapshot which build was running, so the adjudication can be tied to an identity.
$dll = Join-Path $SourceRoot 'fan-host\YeManFanHost.dll'
if (Test-Path -LiteralPath $dll) {
  $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash
  ("runningFanHostDllSha256=" + $h) | Set-Content -LiteralPath (Join-Path $OutDir 'RUNNING-IDENTITY.txt') -Encoding ASCII
  Write-Output ("runningFanHostDllSha256=" + $h)
}

if (-not (Test-Path -LiteralPath $AuditorPath)) {
  Write-Output ("FAIL: adjudicator not found at " + $AuditorPath)
  exit 2
}
Write-Output '--- adjudication ---'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $AuditorPath -LogRoot $OutDir -OutFile (Join-Path $OutDir 'audit.json')
$code = $LASTEXITCODE

if (-not $NoZip) {
  $zip = $OutDir + '.zip'
  if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
  Compress-Archive -Path (Join-Path $OutDir '*') -DestinationPath $zip -Force
  Write-Output ("package: " + $zip)
}
Write-Output ("FAN_R3B_CAPTURE_DONE auditorExit=" + $code)
exit $code
