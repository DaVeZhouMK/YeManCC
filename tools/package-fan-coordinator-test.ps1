<#
.SYNOPSIS
  Creates a standalone full test ZIP with the already-verified FanHost payload.

.DESCRIPTION
  This diagnostic ZIP is separate from the canonical YeManCC.zip release.
  It exports the same verified source payload as PowerControl\fan-host-v2;
  legacy installed fan-host is preserve-only and never a runtime fallback.
  It does not rebuild FanHost, alter its manifest, or change updater policy.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
  return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Assert-ChildPath([string]$Child, [string]$Parent, [string]$Label) {
  $childFull = Get-FullPath $Child
  $parentFull = Get-FullPath $Parent
  if (-not $childFull.StartsWith($parentFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "$Label is outside the workspace: $childFull"
  }
}

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}

function Get-ZipEntrySha256([System.IO.Compression.ZipArchiveEntry]$Entry) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = $Entry.Open()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}

# HC-SLIM-01 size-order export policy (2026-10-07). This diagnostic ZIP uses the
# same standard runtime authority as package-release.ps1. It may include a
# diagnostic FanHost, but it must never reintroduce the two shared DLL copies.
$hcRuntimeName = 'HC-CANDIDATE-0.32.4.0-06c0b954-20260902'
$hcSlimSharedFallbackHashes = [ordered]@{
  'Nefarius.Utilities.DeviceManagement.dll' = 'B5EAF086634438F2774F6B65DD14254AAA078BF1EBFEB004F997314B61272B7C'
  'Newtonsoft.Json.dll' = 'A28C251DFE36D881E9E2462E171441B8B0EC156FE3F452602C9149B1B9EFE05B'
}
$hcSlimSizeOrderRetainedHashes = [ordered]@{
  'Microsoft.Windows.SDK.NET.dll' = '0EC371D93798852E36461C8ADDDBEADCE0F963A04752F0B64E54FE19C1C834A7'
  'HandheldCompanion.dll' = '0C5132A9D13AEBFC5ADD2AA7C9AC54E8DAAA8EBC816A0C68D099BB9685DC2E49'
  'iNKORE.UI.WPF.Modern.dll' = '7650B744E0ADD04549593F680EBF1C8FE40125F36EB8C86445113B974EE25D6C'
  'libVIIPER.dll' = 'E2802542C7FB632914384AC3DBD4A09FA378A9BE91D04EBE822648B15E4960F7'
}
$hcSlimHidMaestroSha256 = 'BD42A99BCB260435CE25796C54A4B792F8A2CED6AB78659C0CF926011663938E'

function Assert-HcSlimSourcePolicy([string]$PowerControlRoot) {
  $virtualGamepad = Join-Path $PowerControlRoot 'feature-assets\virtual-gamepad'
  $runtime = Join-Path $PowerControlRoot "handheldcompanion-runtime\$hcRuntimeName"
  foreach ($name in $hcSlimSharedFallbackHashes.Keys) {
    $local = Join-Path $virtualGamepad $name
    if (Test-Path -LiteralPath $local) {
      throw "HC-SLIM-01 source policy violation: local virtual-gamepad duplicate must not be exported: $local"
    }
    $authority = Join-Path $runtime $name
    if (-not (Test-Path -LiteralPath $authority -PathType Leaf) -or
        (Get-Sha256 $authority) -ne $hcSlimSharedFallbackHashes[$name]) {
      throw "HC-SLIM-01 shared authority missing or changed: $name"
    }
  }
  foreach ($name in $hcSlimSizeOrderRetainedHashes.Keys) {
    $authority = Join-Path $runtime $name
    if (-not (Test-Path -LiteralPath $authority -PathType Leaf) -or
        (Get-Sha256 $authority) -ne $hcSlimSizeOrderRetainedHashes[$name]) {
      throw "HC-SLIM-01 retained authority missing or changed: $name"
    }
  }
  $hid = Join-Path $virtualGamepad 'HIDMaestro.Core.dll'
  # The source template normally contains flags only. InputHost/HIDMaestro
  # are assembled from Build\App\Native\InputHost, not from this template.
  if ((Test-Path -LiteralPath $hid) -and
      (-not (Test-Path -LiteralPath $hid -PathType Leaf) -or
       (Get-Sha256 $hid) -ne $hcSlimHidMaestroSha256)) {
    throw 'HC-SLIM-01 source HIDMaestro.Core.dll identity mismatch'
  }
}

function Assert-HcSlimStagedPolicy([string]$PowerControlRoot) {
  Assert-HcSlimSourcePolicy $PowerControlRoot
  $hid = Join-Path $PowerControlRoot 'feature-assets\virtual-gamepad\HIDMaestro.Core.dll'
  if (-not (Test-Path -LiteralPath $hid -PathType Leaf) -or
      (Get-Sha256 $hid) -ne $hcSlimHidMaestroSha256) {
    throw 'HC-SLIM-01 HIDMaestro.Core.dll is required at its retained identity'
  }
}

function Get-EvidenceCatalog([string]$Root, [string]$Workspace, [string[]]$Names = $null) {
  $names = if ($Names) { $Names } else { @(
    'README.md',
    'HC-00-INTRO-AND-EVIDENCE.md',
    'HC-01-ARCHITECTURE-MAP.md',
    'HC-02-LIFECYCLE-MAP.md',
    'HC-03-FUNCTION-MAP.md',
    'HC-04-MODEL-MAP.md',
    'HC-05-FAN-EC-OEM.md',
    'HC-06-GAMEPAD-HID.md',
    'HC-07-GYRO-SENSORS.md',
    'HC-08-OPEN-CLOSURE-LEDGER.md',
    'Top\HC-TOP-GRAPH.md',
     'Lifecycle\HC-LIFECYCLE-TOTAL.md',
     'Fan\HC-FAN-LIFECYCLE.md',
     'Fan\20260902-FAN-RESUME-BOUNDARY-AND-LOG-AUDIT.md',
     'HC-MODEL-LOGIC-CATALOG.md',
     'Features\HC-FEATURE-LIFECYCLE-INVENTORY.md',
     'Gamepad\HC-GAMEPAD-LIFECYCLE.md',
     'Gyro\HC-GYRO-LIFECYCLE.md'
  ) }
  foreach ($name in $names) {
    $path = Join-Path $Root $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "HC evidence catalog file is missing: $path"
    }
    [ordered]@{
      path = $path.Substring($Workspace.Length).TrimStart('\').Replace('/', '\')
      sha256 = Get-Sha256 $path
    }
  }
}

function Copy-DirectoryContents([string]$Source, [string]$Destination) {
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
    Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $Destination $item.Name) -Recurse -Force
  }
}

function Copy-TestPowerControl([string]$Source, [string]$Destination) {
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  foreach ($item in Get-ChildItem -LiteralPath $Source -Recurse -Force | Sort-Object FullName) {
    $relative = $item.FullName.Substring((Get-FullPath $Source).Length).TrimStart('\')
    $normalized = $relative.Replace('/', '\')
    if ($normalized -match '^(fan-host(?:-v2|-quarantine)?|\.git|build|dist|node_modules)(\\|$)' -or
        $normalized -match '(?i)\.(log|pid|tmp|pdb|obj|ilk)$') {
      continue
    }
    $target = Join-Path $Destination $relative
    if ($item.PSIsContainer) {
      New-Item -ItemType Directory -Force -Path $target | Out-Null
    } else {
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
      Copy-Item -LiteralPath $item.FullName -Destination $target -Force
    }
  }
}

$projectRoot = Get-FullPath (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  # Build/TestPackages is beside the checkout, matching build-workspace.ps1.
  $WorkspaceRoot = Get-FullPath (Join-Path $projectRoot '..\..')
} else {
  $WorkspaceRoot = Get-FullPath $WorkspaceRoot
}

$webBuild = Join-Path $WorkspaceRoot 'Build\App\Web'
$nativeBuild = Join-Path $WorkspaceRoot 'Build\App\Native'
$sourcePowerControl = Join-Path $projectRoot 'PowerControl'
Assert-HcSlimSourcePolicy $sourcePowerControl
$sourceFanHost = Join-Path $sourcePowerControl 'fan-host'
$sourceHcRuntime = Join-Path $sourcePowerControl 'handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902'
$sourceManifest = Join-Path $sourceFanHost 'YeManFanHost.payload.json'
$evidenceWorkspaceRoot = Split-Path -Parent $WorkspaceRoot
$evidenceRoot = Join-Path $evidenceWorkspaceRoot 'Docs\Research\HC'
$referenceEvidenceRoot = Join-Path $evidenceWorkspaceRoot 'Reference\HandheldCompanion'
foreach ($required in @(
  (Join-Path $webBuild 'index.html'),
  (Join-Path $webBuild 'assets'),
  (Join-Path $nativeBuild 'YeManCC.exe'),
  (Join-Path $nativeBuild 'YeManRecoveryService.exe'),
  $sourceManifest,
  (Join-Path $sourceFanHost 'YeManFanHost.exe'),
  (Join-Path $sourceHcRuntime 'HandheldCompanion.runtime.json')
)) {
  if (-not (Test-Path -LiteralPath $required)) { throw "Required test-package input is missing: $required" }
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
  $OutputRoot = Join-Path $WorkspaceRoot ("Build\TestPackages\FanCoordinator-{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
} else {
  $OutputRoot = Get-FullPath $OutputRoot
}
Assert-ChildPath $OutputRoot $WorkspaceRoot 'Test package output'
if (Test-Path -LiteralPath $OutputRoot) { throw "Test package output already exists: $OutputRoot" }

$stage = Join-Path $OutputRoot 'Stage'
$stageProgram = Join-Path $stage 'YeManCC'
$stagePowerControl = Join-Path $stage 'PowerControl'
$stageFanHost = Join-Path $stagePowerControl 'fan-host-v2'
New-Item -ItemType Directory -Force -Path $stageProgram | Out-Null

Copy-DirectoryContents $webBuild $stageProgram
Copy-Item -LiteralPath (Join-Path $nativeBuild 'YeManCC.exe') -Destination (Join-Path $stageProgram 'YeManCC.exe') -Force
Copy-Item -LiteralPath (Join-Path $nativeBuild 'YeManRecoveryService.exe') -Destination (Join-Path $stageProgram 'YeManRecoveryService.exe') -Force
foreach ($name in @('version.json', 'YeMan-Support.html')) {
  $source = Join-Path $projectRoot $name
  if (Test-Path -LiteralPath $source -PathType Leaf) {
    Copy-Item -LiteralPath $source -Destination (Join-Path $stageProgram $name) -Force
  }
}
Copy-TestPowerControl $sourcePowerControl $stagePowerControl
# Explicit source-to-runtime relocation; generic copy must not leak the legacy root.
Copy-DirectoryContents $sourceFanHost $stageFanHost
# Source virtual-gamepad contains only flags. Assemble the same 9-file
# InputHost bundle as the standard complete ZIP. The two fallback utilities
# are not copied from the build lane; their sole delivery location is HC.
$ymccHostFiles = @('YeManInputHost.exe', 'YeManInputHost.dll', 'YeManInputHost.deps.json', 'YeManInputHost.runtimeconfig.json', 'HIDMaestro.Core.dll')
$sharedHostDlls = @('Nefarius.Drivers.HidHide.dll', 'Nefarius.Vicius.Abstractions.dll', 'Microsoft.Extensions.DependencyInjection.Abstractions.dll', 'Microsoft.Extensions.Logging.Abstractions.dll')
$nativeInputHost = Join-Path $nativeBuild 'InputHost'
$stageVirtualGamepad = Join-Path $stagePowerControl 'feature-assets\virtual-gamepad'
New-Item -ItemType Directory -Force -Path $stageVirtualGamepad | Out-Null
foreach ($name in $ymccHostFiles) {
  $input = Join-Path $nativeInputHost $name
  if (-not (Test-Path -LiteralPath $input -PathType Leaf)) { throw "Test ZIP InputHost build artifact missing: $name" }
  Copy-Item -LiteralPath $input -Destination (Join-Path $stageVirtualGamepad $name) -Force
  if ((Get-Sha256 $input) -ne (Get-Sha256 (Join-Path $stageVirtualGamepad $name))) { throw "Test ZIP InputHost copy drift: $name" }
}
foreach ($name in $sharedHostDlls) {
  $input = Join-Path $sourceHcRuntime $name
  if (-not (Test-Path -LiteralPath $input -PathType Leaf)) { throw "Test ZIP shared HC artifact missing: $name" }
  Copy-Item -LiteralPath $input -Destination (Join-Path $stageVirtualGamepad $name) -Force
  if ((Get-Sha256 $input) -ne (Get-Sha256 (Join-Path $stageVirtualGamepad $name))) { throw "Test ZIP HC copy drift: $name" }
}
Assert-HcSlimStagedPolicy $stagePowerControl
# Check deps.json declarations at their actual approved locations.
$hostDeps = Get-Content -LiteralPath (Join-Path $stageVirtualGamepad 'YeManInputHost.deps.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($target in @($hostDeps.targets.PSObject.Properties)) {
  foreach ($pkg in @($target.Value.PSObject.Properties)) {
    if ($null -eq $pkg.Value.runtime) { continue }
    foreach ($runtimeFile in @($pkg.Value.runtime.PSObject.Properties.Name)) {
      $path = if ($hcSlimSharedFallbackHashes.Keys -contains $runtimeFile) {
        Join-Path (Join-Path $stagePowerControl "handheldcompanion-runtime\$hcRuntimeName") $runtimeFile
      } else { Join-Path $stageVirtualGamepad $runtimeFile }
      if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Test ZIP InputHost dependency missing at approved location: $runtimeFile" }
    }
  }
}


# This is a test-only launcher. The production binary intentionally defaults
# to C:\SOFT\YeMan\PowerControl; a standalone ZIP must instead prove the
# payload it carries, without mutating the machine's installed directory.
$testLauncher = Join-Path $stageProgram 'Start-YeManCC-FanCoordinator-Test.cmd'
@(
  '@echo off',
  'setlocal',
  'for %%I in ("%~dp0..\PowerControl") do set "YEMAN_POWER_CONTROL_DIR=%%~fI"',
  '"%~dp0YeManCC.exe"',
  'exit /b %ERRORLEVEL%'
) | Set-Content -LiteralPath $testLauncher -Encoding Ascii

$payload = (Get-Content -LiteralPath $sourceManifest -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
if ([int]$payload.schemaVersion -ne 2 -or @($payload.files).Count -ne 9) { throw 'FanHost payload manifest is invalid' }
$payloadPaths = @{}
foreach ($entry in @($payload.files)) {
  $relative = ([string]$entry.path).Replace('/', '\')
  if ([string]::IsNullOrWhiteSpace($relative) -or [IO.Path]::IsPathRooted($relative) -or $relative -match '(^|\\)\.\.(\\|$)') {
    throw "Unsafe FanHost manifest path: $relative"
  }
  $target = Join-Path $stageFanHost $relative
  if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "Staged FanHost file is missing: $relative" }
  $actual = Get-Sha256 $target
  if ($actual -ne ([string]$entry.sha256).ToUpperInvariant()) { throw "Staged FanHost hash mismatch: $relative" }
  $payloadPaths[$relative.ToLowerInvariant()] = $true
}
$actualFanFiles = @(Get-ChildItem -LiteralPath $stageFanHost -Recurse -File -Force | ForEach-Object {
  $_.FullName.Substring((Get-FullPath $stageFanHost).Length).TrimStart('\').ToLowerInvariant()
})
$allowedFanFiles = @($payloadPaths.Keys + 'yemanfanhost.payload.json' | Sort-Object -Unique)
$unexpectedFanFiles = @($actualFanFiles | Where-Object { $_ -notin $allowedFanFiles })
if ($unexpectedFanFiles.Count -gt 0) { throw "Staged FanHost has files outside its manifest: $($unexpectedFanFiles -join ', ')" }
$runtimeManifest = (Get-Content (Join-Path $stagePowerControl 'handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902\HandheldCompanion.runtime.json') -Raw | ConvertFrom-Json)
foreach ($entry in @($runtimeManifest.files)) { $rp=[string]$entry.path; $rpath=Join-Path $stagePowerControl "handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902\$rp"; if(!(Test-Path $rpath) -or (Get-Sha256 $rpath) -ne ([string]$entry.sha256).ToUpperInvariant()){throw "Staged HC runtime mismatch: $rp"} }
if (-not (Test-Path -LiteralPath $testLauncher -PathType Leaf)) { throw 'Test package launcher is missing' }
$testLauncherText = Get-Content -LiteralPath $testLauncher -Raw -Encoding Ascii
if ($testLauncherText -notmatch 'YEMAN_POWER_CONTROL_DIR' -or $testLauncherText -notmatch 'PowerControl') {
  throw 'Test package launcher does not bind its packaged PowerControl directory'
}

$packageEvidence = [ordered]@{
  kind = 'fan-coordinator-standalone-test'
  createdAtUtc = [DateTime]::UtcNow.ToString('o')
  sourceProject = $projectRoot
  fanHostManifestSha256 = Get-Sha256 $sourceManifest
  fanHostFiles = @($payload.files).Count
  fanHostExecutableSha256 = Get-Sha256 (Join-Path $sourceFanHost 'YeManFanHost.exe')
  yemanExeSha256 = Get-Sha256 (Join-Path $stageProgram 'YeManCC.exe')
  nativeExecutableRelativePath = 'YeManCC\YeManCC.exe'
  fanHostExecutableRelativePath = 'PowerControl\fan-host-v2\YeManFanHost.exe'
  runtimeEvidenceContract = [ordered]@{
    id = 'HC-08-runtime-trace-v1'
    nativeLifecycleLog = '%LOCALAPPDATA%\YeManCC\native-lifecycle.log'
    sleepFactsLog = 'PowerControl\Sleep\sleep-facts.log (resolved from YEMAN_POWER_CONTROL_DIR)'
    fanLifecycleLog = '%LOCALAPPDATA%\YeManCC\fan-lifecycle.log'
    recoveryServiceLog = '%LOCALAPPDATA%\YeManCC\recovery-service.log'
    fanHostRuntimeLog = '%LOCALAPPDATA%\YeManCC\fan-host\logs\yeman-fan-host-runtime.log'
    requiredNativeEvents = @('boot-single-instance-acquired', 'power-resume-notification', 'fan-host-resume-queued', 'fan-host-boundary-attempt', 'fan-host-boundary-http', 'fan-host-resume-result')
   requiredHostEvents = @('host.starting', 'startup.handshake', 'power.resume-auto-rebuild.success', 'hc.fan-dispatch.begin', 'hc.fan-dispatch.callback-returned')
  }
  hcEvidenceCatalog = @(Get-EvidenceCatalog $evidenceRoot $evidenceWorkspaceRoot)
  hcReferenceEvidenceCatalog = @(Get-EvidenceCatalog $referenceEvidenceRoot $evidenceWorkspaceRoot @('LIFECYCLE-ATLAS.md', 'LOGGING-EVIDENCE.md'))
  testLauncher = 'YeManCC\\Start-YeManCC-FanCoordinator-Test.cmd'
  hcSourceBaseline = $runtimeManifest.runtimeId
  hcVersion = $runtimeManifest.hcVersion
  hcSourceBaselineSource = 'PowerControl/handheldcompanion-runtime/HandheldCompanion.runtime.json'
  fanHostSourceBaseline = (Get-Content -LiteralPath (Join-Path $projectRoot 'FanLab\FANHOST-SOURCE-BASELINE.json') -Raw -Encoding UTF8 | ConvertFrom-Json).baselineId
  updaterPolicy = 'unchanged; this ZIP is standalone-test-only'
}
$packageEvidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $stageProgram 'fan-coordinator-test-manifest.json') -Encoding UTF8

$zipPath = Join-Path $OutputRoot 'YeManCC-FanCoordinator-Test.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
  # Archive-level standard export gate: repeat the same no-duplicate and
  # retained-authority checks after compression, not only on the staging tree.
  foreach ($name in $hcSlimSharedFallbackHashes.Keys) {
    $forbidden = "PowerControl/feature-assets/virtual-gamepad/$name"
    $hits = @($zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $forbidden })
    if ($hits.Count -ne 0) { throw "HC-SLIM-01 test ZIP contains forbidden local duplicate: $forbidden" }
    $authority = "PowerControl/handheldcompanion-runtime/$hcRuntimeName/$name"
    $authorityHits = @($zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $authority })
    if ($authorityHits.Count -ne 1 -or (Get-ZipEntrySha256 $authorityHits[0]) -ne $hcSlimSharedFallbackHashes[$name]) { throw "HC-SLIM-01 test ZIP shared authority mismatch: $authority" }
  }
  foreach ($name in $hcSlimSizeOrderRetainedHashes.Keys) {
    $authority = "PowerControl/handheldcompanion-runtime/$hcRuntimeName/$name"
    $hits = @($zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $authority })
    if ($hits.Count -ne 1 -or (Get-ZipEntrySha256 $hits[0]) -ne $hcSlimSizeOrderRetainedHashes[$name]) { throw "HC-SLIM-01 test ZIP retained authority mismatch: $authority" }
  }
  foreach ($name in @('HIDMaestro.Core.dll')) {
    $retainedPath = "PowerControl/feature-assets/virtual-gamepad/$name"
    $hits = @($zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $retainedPath })
    if ($hits.Count -ne 1 -or (Get-ZipEntrySha256 $hits[0]) -ne $hcSlimHidMaestroSha256) {
      throw "HC-SLIM-01 test ZIP HIDMaestro retained identity mismatch: $retainedPath"
    }
  }
  $roots = @($zip.Entries | ForEach-Object {
    $normalized = $_.FullName.Replace('\', '/').TrimStart('/')
    if ($normalized) { ($normalized -split '/')[0] }
  } | Sort-Object -Unique)
  if ((Compare-Object $roots @('PowerControl', 'YeManCC'))) { throw "Test ZIP root layout is invalid: $($roots -join ', ')" }
  foreach ($entry in @($payload.files)) {
    $relative = ([string]$entry.path).Replace('\', '/')
    $zipEntry = @($zip.Entries | Where-Object { $_.FullName.Replace('\', '/').TrimStart('/') -eq "PowerControl/fan-host-v2/$relative" })
    if ($zipEntry.Count -ne 1) { throw "Test ZIP FanHost entry missing or duplicated: $relative" }
    if ((Get-ZipEntrySha256 $zipEntry[0]) -ne ([string]$entry.sha256).ToUpperInvariant()) { throw "Test ZIP FanHost hash mismatch: $relative" }
  }
} finally {
  $zip.Dispose()
}

$zipHash = Get-Sha256 $zipPath
Write-Output 'FAN_COORDINATOR_TEST_PACKAGE_OK'
Write-Output "Output: $OutputRoot"
Write-Output "ZIP: $zipPath"
Write-Output "ZIP SHA256: $zipHash"
Write-Output "FanHost manifest SHA256: $($packageEvidence.fanHostManifestSha256)"
