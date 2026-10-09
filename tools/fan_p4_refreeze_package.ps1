#requires -Version 5.1
<#
  FAN-204 s31 addendum (P-4): re-freeze package builder + audits.

  Inputs (read-only):
    - FanLab/real-host source (already rebuilt by tools/build-fan-host-payload.ps1)
    - PowerControl/fan-host (the in-tree payload: the re-frozen generation)
    - PowerControl/handheldcompanion-runtime/<id> (shared HC runtime closure)
    - native/main.cpp + tools/verify-r5v9-fan-host-payload.ps1 + the payload authorization file
      (the three pin sites that must agree with the payload identity chain)
    - Mainline/Backup/fan-host-payload-20260923-0CDD92CB (the previous generation for rollback)

  Outputs (into -OutDir):
    - FanHostPayload-complete-<gen>.zip          whole payload generation (8 files + inventory)
    - FanHostPayload-update-<prev>_to_<gen>.zip  only the changed files (dll + manifest + authorization)
    - FanHostPayload-rollback-<prev>.zip         the previous generation (rollback material)
    - INSTALL-CHECKLIST.md                       pre-install verification + rollback commands
    - fan-p4-refreeze-<stamp>.json               full evidence (hashes, audits, negative control)

  This script never installs, never replaces the installed payload, never writes to the payload
  source tree and never builds a release/update package of the application (that stays with the
  release owner / operator). Keep this file pure ASCII (PS 5.1 misdecodes BOM-less non-ASCII).
#>
[CmdletBinding()]
param(
  [string]$ProjectRoot = '',
  [string]$BackupRoot = '',
  [string]$OutDir = '',
  [string]$HcRuntimeRoot = '',
  # The generation the update/rollback packages are built FROM (8 hex chars, upper case).
  # r2 shipped with 0CDD92CB (default, keeps r2 reproducible); r3 overrides it with 1D61C674.
  [string]$PrevGen = '0CDD92CB'
)

$ErrorActionPreference = 'Stop'
# Historical P4 generated old-path/partial envelopes; it cannot be used for V2.
throw 'FAN_LEGACY_EXPORT_RETIRED: use package-release.ps1 and fan_host_path_gate.ps1; legacy fan-host must never be an installTarget again'
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
$payloadRoot = Join-Path $ProjectRoot 'PowerControl\fan-host'
$mainCpp = Join-Path $ProjectRoot 'native\main.cpp'
$verifier = Join-Path $ProjectRoot 'tools\verify-r5v9-fan-host-payload.ps1'
if ([string]::IsNullOrWhiteSpace($HcRuntimeRoot)) {
  $HcRuntimeRoot = Join-Path $ProjectRoot 'PowerControl\handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902'
}
if ([string]::IsNullOrWhiteSpace($BackupRoot)) {
  $BackupRoot = Join-Path (Split-Path -Parent (Split-Path -Parent $ProjectRoot)) 'Backup\fan-host-payload-20260923-0CDD92CB'
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
  $OutDir = Join-Path (Split-Path -Parent (Split-Path -Parent $ProjectRoot)) '..\_scratch\FAN-204-s31R2-20260923\packages'
}
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $hash = $sha.ComputeHash([IO.File]::ReadAllBytes($Path))
    return ([BitConverter]::ToString($hash)).Replace('-', '').ToLowerInvariant()
  } finally { $sha.Dispose() }
}

$problems = New-Object System.Collections.Generic.List[string]
function Fail([string]$Message) { $script:problems.Add($Message) | Out-Null }

# ---------------------------------------------------------------- 1) payload set audit
$manifestPath = Join-Path $payloadRoot 'YeManFanHost.payload.json'
$manifestHash = Get-Sha256 $manifestPath
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$physical = @(Get-ChildItem -LiteralPath $payloadRoot -File -Force |
  Where-Object { $_.Name -ne 'YeManFanHost.payload.json' } | ForEach-Object { $_.Name } | Sort-Object)
$listed = @($manifest.files | ForEach-Object { [string]$_.path } | Sort-Object)
$declaredDup = @($manifest.files | Group-Object { ([string]$_.path).ToLowerInvariant() } | Where-Object { $_.Count -gt 1 })
if ($declaredDup.Count -gt 0) { Fail ("payload manifest declares duplicate paths: " + (($declaredDup | ForEach-Object { $_.Name }) -join ',')) }
$diff = Compare-Object $physical $listed
if ($diff) { Fail ("payload files outside/missing from the manifest: " + (($diff | ForEach-Object { $_.InputObject }) -join ',')) }
foreach ($entry in $manifest.files) {
  $p = Join-Path $payloadRoot ([string]$entry.path)
  if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { Fail ("payload manifest lists a missing file: " + $entry.path); continue }
  if ((Get-Sha256 $p) -ne ([string]$entry.sha256).ToLowerInvariant()) { Fail ("payload file hash mismatch: " + $entry.path) }
}
if ([int]$manifest.schemaVersion -ne 2) { Fail ("payload manifest schemaVersion is not 2: " + $manifest.schemaVersion) }

# ---------------------------------------------------------------- 2) runtime closure audit
$runtimeManifestPath = Join-Path $HcRuntimeRoot 'HandheldCompanion.runtime.json'
if (-not (Test-Path -LiteralPath $runtimeManifestPath -PathType Leaf)) { Fail "HC runtime manifest missing: $runtimeManifestPath" }
$runtimeManifestHash = if (Test-Path -LiteralPath $runtimeManifestPath) { Get-Sha256 $runtimeManifestPath } else { 'missing' }
$runtimeFiles = 0
if (Test-Path -LiteralPath $runtimeManifestPath) {
  $runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  $runtimeFiles = @($runtimeManifest.files).Count
  $runtimeDeclaredDup = @($runtimeManifest.files | Group-Object { ([string]$_.path).ToLowerInvariant() } | Where-Object { $_.Count -gt 1 })
  if ($runtimeDeclaredDup.Count -gt 0) { Fail ("HC runtime manifest declares duplicate paths: " + (($runtimeDeclaredDup | ForEach-Object { $_.Name }) -join ',')) }
  foreach ($entry in $runtimeManifest.files) {
    $p = Join-Path $HcRuntimeRoot ([string]$entry.path)
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { Fail ("HC runtime manifest lists a missing file: " + $entry.path); continue }
    if ((Get-Sha256 $p) -ne ([string]$entry.sha256).ToLowerInvariant()) { Fail ("HC runtime file hash mismatch: " + $entry.path) }
  }
  $runtimeManifestRel = [string]$manifest.runtimeManifest
  if ([string]$manifest.runtimeId -ne [string]$runtimeManifest.runtimeId) { Fail 'payload manifest runtimeId does not match the HC runtime manifest' }
  if ($runtimeManifestRel -notmatch [regex]::Escape((Split-Path -Leaf $HcRuntimeRoot))) { Fail 'payload manifest does not point at the audited HC runtime directory' }
  foreach ($required in @('HandheldCompanion.dll', 'HandheldCompanion.deps.json', 'LibreHardwareMonitorLib.dll')) {
    if (@($runtimeManifest.files | Where-Object { [string]$_.path -ieq $required }).Count -ne 1) { Fail ("HC runtime required file missing/duplicated: " + $required) }
  }
}

# ---------------------------------------------------------------- 3) identity chain
$hostDll = Join-Path $payloadRoot 'YeManFanHost.dll'
$hostExe = Join-Path $payloadRoot 'YeManFanHost.exe'
$hostDllHash = Get-Sha256 $hostDll
$hostExeHash = Get-Sha256 $hostExe
$hcDll = Join-Path $HcRuntimeRoot 'HandheldCompanion.dll'
$hcDllHash = Get-Sha256 $hcDll
$authText = Get-Content -LiteralPath (Join-Path $payloadRoot 'YeManFanHost.authorization.md') -Raw -Encoding UTF8
function AuthValue([string]$Key) {
  $m = [regex]::Match($authText, "(?m)^" + [regex]::Escape($Key) + ":\s*([^\r\n]+)")
  if ($m.Success) { return $m.Groups[1].Value.Trim() }
  return ''
}
$authDll = AuthValue 'approvedHostDllSha256'
$authExe = AuthValue 'approvedHostExeSha256'
$authHc = AuthValue 'approvedHcSha256'
if ($authDll.ToLowerInvariant() -ne $hostDllHash) { Fail "authorization approvedHostDllSha256 != payload DLL ($authDll vs $hostDllHash)" }
if ($authExe.ToLowerInvariant() -ne $hostExeHash) { Fail "authorization approvedHostExeSha256 != payload EXE ($authExe vs $hostExeHash)" }
if ($authHc.ToLowerInvariant() -ne $hcDllHash) { Fail "authorization approvedHcSha256 != HC runtime DLL ($authHc vs $hcDllHash)" }

$verifierText = Get-Content -LiteralPath $verifier -Raw -Encoding UTF8
$vManifest = [regex]::Match($verifierText, "\`$expectedManifestSha256\s*=\s*'([0-9a-fA-F]{64})'").Groups[1].Value.ToLowerInvariant()
$vDll = [regex]::Match($verifierText, "'YeManFanHost\.dll'\s*=\s*'([0-9A-Fa-f]{64})'").Groups[1].Value.ToLowerInvariant()
$vExe = [regex]::Match($verifierText, "'YeManFanHost\.exe'\s*=\s*'([0-9A-Fa-f]{64})'").Groups[1].Value.ToLowerInvariant()
if ($vManifest -ne $manifestHash) { Fail "verify-r5v9 manifest pin != payload manifest ($vManifest vs $manifestHash)" }
if ($vDll -ne $hostDllHash) { Fail "verify-r5v9 DLL pin != payload DLL ($vDll vs $hostDllHash)" }
if ($vExe -ne $hostExeHash) { Fail "verify-r5v9 EXE pin != payload EXE ($vExe vs $hostExeHash)" }

$mainText = Get-Content -LiteralPath $mainCpp -Raw -Encoding UTF8
$mManifest = [regex]::Match($mainText, "\`$expectedFanHostV2ManifestSha256\s*=\s*'([0-9a-fA-F]{64})'").Groups[1].Value.ToLowerInvariant()
$mCount = [regex]::Match($mainText, '\$expectedFanHostV2ManifestFileCount\s*=\s*(\d+)').Groups[1].Value
if ($mManifest -ne $manifestHash) { Fail "native/main.cpp manifest pin != payload manifest ($mManifest vs $manifestHash)" }
if ([int]$mCount -ne @($manifest.files).Count) { Fail "native/main.cpp file-count pin != payload manifest file count ($mCount vs $(@($manifest.files).Count))" }

# ---------------------------------------------------------------- 4) negative control
# The audit must FAIL on a tampered copy; a silent pass would make every other audit vacuous.
$negRoot = Join-Path $OutDir ('negative-control-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force -Path $negRoot | Out-Null
Copy-Item -Path (Join-Path $payloadRoot '*') -Destination $negRoot -Force
$negDll = Join-Path $negRoot 'YeManFanHost.dll'
$bytes = [IO.File]::ReadAllBytes($negDll)
$bytes[[int]($bytes.Length / 2)] = [byte]($bytes[[int]($bytes.Length / 2)] -bxor 0x5A)
[IO.File]::WriteAllBytes($negDll, $bytes)
$negManifest = Get-Content -LiteralPath (Join-Path $negRoot 'YeManFanHost.payload.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$negMismatch = $false
foreach ($entry in $negManifest.files) {
  if ((Get-Sha256 (Join-Path $negRoot ([string]$entry.path))) -ne ([string]$entry.sha256).ToLowerInvariant()) { $negMismatch = $true }
}
if (-not $negMismatch) { Fail 'negative control: a tampered payload DLL was NOT detected (audit is vacuous)' }
Remove-Item -LiteralPath $negRoot -Recurse -Force

# ---------------------------------------------------------------- 5) packages
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$gen = $hostDllHash.Substring(0, 8).ToUpperInvariant()
$prevGen = $PrevGen.ToUpperInvariant()
$completeZip = Join-Path $OutDir ("FanHostPayload-complete-$gen.zip")
$updateZip = Join-Path $OutDir ("FanHostPayload-update-$prevGen`_to_$gen.zip")
$rollbackZip = Join-Path $OutDir ("FanHostPayload-rollback-$prevGen.zip")

# complete: the payload tree exactly as it must land under PowerControl/fan-host
$stageRoot = Join-Path $OutDir ('stage-complete-' + $stamp)
New-Item -ItemType Directory -Force -Path (Join-Path $stageRoot 'fan-host') | Out-Null
Copy-Item -Path (Join-Path $payloadRoot '*') -Destination (Join-Path $stageRoot 'fan-host') -Force
$completeInventory = [ordered]@{
  generation = $gen
  builtAtUtc = (Get-Date).ToUniversalTime().ToString('o')
  manifestSha256 = $manifestHash
  installTarget = 'PowerControl/fan-host'
  files = @(Get-ChildItem -LiteralPath (Join-Path $stageRoot 'fan-host') -File | Sort-Object Name | ForEach-Object {
      [ordered]@{ path = 'fan-host/' + $_.Name; sha256 = Get-Sha256 $_.FullName; bytes = $_.Length }
    })
}
[IO.File]::WriteAllText((Join-Path $stageRoot 'PACKAGE-INVENTORY.json'), ($completeInventory | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
Remove-Item -LiteralPath $completeZip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stageRoot '*') -DestinationPath $completeZip -CompressionLevel Optimal -Force
Remove-Item -LiteralPath $stageRoot -Recurse -Force

# update: only the files that actually changed between generations
$updateFiles = @('YeManFanHost.dll', 'YeManFanHost.payload.json', 'YeManFanHost.authorization.md')
$updateStage = Join-Path $OutDir ('stage-update-' + $stamp)
New-Item -ItemType Directory -Force -Path (Join-Path $updateStage 'fan-host') | Out-Null
foreach ($f in $updateFiles) { Copy-Item (Join-Path $payloadRoot $f) (Join-Path $updateStage 'fan-host') -Force }
$updateInventory = [ordered]@{
  from = $prevGen
  to = $gen
  installTarget = 'PowerControl/fan-host'
  note = 'changed files only; the whole-package overlay (complete zip) is the approved device-window path'
  files = @($updateFiles | Sort-Object | ForEach-Object {
      [ordered]@{ path = 'fan-host/' + $_; sha256 = Get-Sha256 (Join-Path $payloadRoot $_) }
    })
}
[IO.File]::WriteAllText((Join-Path $updateStage 'PACKAGE-INVENTORY.json'), ($updateInventory | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
Remove-Item -LiteralPath $updateZip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $updateStage '*') -DestinationPath $updateZip -CompressionLevel Optimal -Force
Remove-Item -LiteralPath $updateStage -Recurse -Force

# rollback: the previous generation straight from the archived backup
if (Test-Path -LiteralPath (Join-Path $BackupRoot 'YeManFanHost.dll')) {
  $rbStage = Join-Path $OutDir ('stage-rollback-' + $stamp)
  New-Item -ItemType Directory -Force -Path (Join-Path $rbStage 'fan-host') | Out-Null
  Copy-Item -Path (Join-Path $BackupRoot '*') -Destination (Join-Path $rbStage 'fan-host') -Force -Recurse
  $rbManifest = Get-Content -LiteralPath (Join-Path $rbStage 'fan-host\YeManFanHost.payload.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  $rbBad = @()
  foreach ($entry in $rbManifest.files) {
    $p = Join-Path $rbStage ('fan-host\' + ([string]$entry.path -replace '/', '\'))
    if (-not (Test-Path -LiteralPath $p)) { $rbBad += ('missing:' + $entry.path); continue }
    if ((Get-Sha256 $p) -ne ([string]$entry.sha256).ToLowerInvariant()) { $rbBad += ('hash:' + $entry.path) }
  }
  if ($rbBad.Count -gt 0) { Fail ("rollback archive is not a consistent generation: " + ($rbBad -join ',')) }
  $rbInventory = [ordered]@{
    generation = $prevGen
    manifestSha256 = (Get-Sha256 (Join-Path $BackupRoot 'YeManFanHost.payload.json'))
    dllSha256 = (Get-Sha256 (Join-Path $BackupRoot 'YeManFanHost.dll'))
    installTarget = 'PowerControl/fan-host'
    files = @(Get-ChildItem -LiteralPath $BackupRoot -File | Sort-Object Name | ForEach-Object {
        [ordered]@{ path = 'fan-host/' + $_.Name; sha256 = Get-Sha256 $_.FullName }
      })
  }
  [IO.File]::WriteAllText((Join-Path $rbStage 'PACKAGE-INVENTORY.json'), ($rbInventory | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
  Remove-Item -LiteralPath $rollbackZip -Force -ErrorAction SilentlyContinue
  $rbSource = Join-Path $rbStage '*'
  if (-not (Test-Path -LiteralPath $rbStage -PathType Container)) { Fail "rollback staging directory missing: $rbStage" }
  if ([string]::IsNullOrWhiteSpace((Split-Path -Leaf $rollbackZip))) { Fail "rollback archive path is invalid: $rollbackZip" }
  Compress-Archive -Path $rbSource -DestinationPath $rollbackZip -CompressionLevel Optimal -Force
  Remove-Item -LiteralPath $rbStage -Recurse -Force
} else {
  Fail "rollback backup missing: $BackupRoot"
}

# ---------------------------------------------------------------- 6) checklist + evidence
$checklistPath = Join-Path $OutDir 'INSTALL-CHECKLIST.md'
$checklist = @"
# Fan payload install checklist (FAN-204 s31 addendum, generation $gen)

Device window is NOT approved by this script: it prepares material only.
Whole-package overlay is the approved path; never copy a single file or only the fan-host folder.

## 1. Pre-install identity checks (all must print the same values)
- payload manifest SHA256 = $manifestHash
- payload DLL SHA256      = $hostDllHash
- payload EXE SHA256      = $hostExeHash
- HC runtime DLL SHA256   = $hcDllHash
- native/main.cpp pin, tools/verify-r5v9 pin and the payload authorization file must all agree
  with the four values above (this script fails the build if they do not).

## 2. Install (device window, operator supervised)
1. Take a rollback copy of the installed payload first (PowerControl\fan-host -> backup).
2. Overlay the whole exported package (application + PowerControl payload), not a single file.
3. Run tools/verify-r5v9-fan-host-payload.ps1 against the installed payload root: expect FAN_HOST_V2_OK.
4. Start the App once; confirm the Host starts and /api/state answers with resumePhase = Suspended/Ready.

## 3. Device acceptance (same window)
- B3: close the App with a curve active -> Host exits within budget, port 8765 released.
- P-2/Q1: S3 wake -> the Host answers /api/resume with resumeAccepted=true + resumePhase=Resuming,
  native logs fan-host-resume-observe during the rebuild, then terminal=recovered (no exhausted).
- B2: suspend chain stays clean (close-hc-success, no HC_CLOSE_PENDING streak).
- Log: runtime log stays within the 2 MiB rotation with 3 archives.

## 4. Rollback (if any check fails)
1. Stop the App.
2. Restore PowerControl\fan-host from $rollbackZip (whole generation, 8 files).
3. Re-run tools/verify-r5v9-fan-host-payload.ps1 against the restored payload: expect FAN_HOST_V2_OK
   with manifest $((Get-Sha256 (Join-Path $BackupRoot 'YeManFanHost.payload.json'))).
"@
[IO.File]::WriteAllText($checklistPath, $checklist, (New-Object Text.UTF8Encoding($false)))

$evidence = [ordered]@{
  at = (Get-Date).ToString('o')
  generation = $gen
  previousGeneration = $prevGen
  payloadRoot = $payloadRoot
  manifestSha256 = $manifestHash
  manifestFileCount = @($manifest.files).Count
  hostDllSha256 = $hostDllHash
  hostExeSha256 = $hostExeHash
  hcDllSha256 = $hcDllHash
  hcRuntimeManifestSha256 = $runtimeManifestHash
  hcRuntimeFileCount = $runtimeFiles
  pins = [ordered]@{
    mainCppManifest = $mManifest
    mainCppFileCount = $mCount
    verifyR5v9Manifest = $vManifest
    verifyR5v9Dll = $vDll
    verifyR5v9Exe = $vExe
    authorizationDll = $authDll
    authorizationExe = $authExe
    authorizationHc = $authHc
  }
  packages = [ordered]@{
    complete = [ordered]@{ path = $completeZip; sha256 = (Get-Sha256 $completeZip); bytes = (Get-Item $completeZip).Length }
    update = [ordered]@{ path = $updateZip; sha256 = (Get-Sha256 $updateZip); bytes = (Get-Item $updateZip).Length }
    rollback = [ordered]@{ path = $rollbackZip; sha256 = (Get-Sha256 $rollbackZip); bytes = (Get-Item $rollbackZip).Length; exists = (Test-Path -LiteralPath $rollbackZip) }
  }
  checklist = $checklistPath
  negativeControl = 'tampered payload DLL detected (audit is not vacuous)'
  problems = @($problems)
  verdict = if ($problems.Count -eq 0) { 'READY_FOR_INSTALL_REVIEW' } else { 'CHECK_FAIL' }
}
$evidencePath = Join-Path $OutDir ("fan-p4-refreeze-$stamp.json")
[IO.File]::WriteAllText($evidencePath, ($evidence | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))

if ($problems.Count -gt 0) {
  Write-Host ("FAN_P4_REFREEZE=CHECK_FAIL problems=" + $problems.Count) -ForegroundColor Red
  $problems | ForEach-Object { Write-Host ('  - ' + $_) }
  exit 2
}
Write-Host ("FAN_P4_REFREEZE=READY_FOR_INSTALL_REVIEW generation=$gen files=$(@($manifest.files).Count)")
Write-Host ("evidence=" + $evidencePath)
Write-Host ("complete=" + $completeZip)
Write-Host ("update=" + $updateZip)
Write-Host ("rollback=" + $rollbackZip)
exit 0