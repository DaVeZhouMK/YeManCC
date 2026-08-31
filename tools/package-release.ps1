<#
.SYNOPSIS
  Assemble Build outputs and verified runtime assets into Release.

.DESCRIPTION
  Writes only to the workspace Build, Release and Backup areas. It never
  deploys to C:\SOFT\YeMan.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [ValidateSet('full', 'legacy-bridge', 'legacy-bootstrap')]
  [string]$ReleaseEnvelope = '',
  [string]$CustomSteamLibrarySource = ''
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
    try {
      return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    } finally {
      $stream.Dispose()
    }
  } finally {
    $sha.Dispose()
  }
}

function Copy-DirectoryContents([string]$Source, [string]$Destination) {
  if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
    throw "Copy source is missing: $Source"
  }
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
    Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $Destination $item.Name) -Recurse -Force
  }
}

function Normalize-PackageRelativePath([string]$Path) {
  $normalized = ([string]$Path).Replace('/', '\')
  if ([string]::IsNullOrWhiteSpace($normalized) -or [IO.Path]::IsPathRooted($normalized) -or
      $normalized.Contains(':') -or @($normalized -split '\\' | Where-Object { $_ -eq '..' -or $_ -eq '' }).Count -gt 0) {
    throw "Package manifest path is unsafe: $Path"
  }
  return $normalized
}

function Assert-CustomSteamLibraryPackage([string]$Source) {
  if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
    throw "Custom Steam Library package source is missing: $Source"
  }
  $manifestPath = Join-Path $Source 'package-manifest.json'
  if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'Custom Steam Library package-manifest.json is missing'
  }
  $manifestText = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8
  $manifest = $manifestText.TrimStart([char]0xFEFF) | ConvertFrom-Json
  if ([string]$manifest.packageId -ne 'custom-steam-library') { throw 'Custom Steam Library packageId is invalid' }
  if ([string]$manifest.packageType -ne 'green-child') { throw 'Custom Steam Library packageType is invalid' }
  if ([string]$manifest.entryPoint -ne 'CustomSteamLibrary.exe') { throw 'Custom Steam Library entryPoint is invalid' }
  if ([string]$manifest.worker -ne 'SteamArtworkLab.exe') { throw 'Custom Steam Library worker is invalid' }
  if ([string]$manifest.updater.unknownPaths -ne 'preserve') { throw 'Custom Steam Library unknown-path policy must be preserve' }
  if ([int]$manifest.updater.healthHandshake.protocol -ne 1 -or [bool]$manifest.updater.healthHandshake.requiredBeforeCommit -ne $true) { throw 'Custom Steam Library health handshake policy is missing' }
  $manifestFilesRaw = @($manifest.files | ForEach-Object { Normalize-PackageRelativePath ([string]$_) })
  $managedFilesRaw = @($manifest.managedPaths | ForEach-Object { Normalize-PackageRelativePath ([string]$_) })
  $manifestFiles = @($manifestFilesRaw | Sort-Object -Unique)
  $managedFiles = @($managedFilesRaw | Sort-Object -Unique)
  if ($manifestFiles.Count -ne $manifestFilesRaw.Count -or $managedFiles.Count -ne $managedFilesRaw.Count) { throw 'Custom Steam Library manifest contains duplicate paths' }
  if (Compare-Object $manifestFiles $managedFiles) { throw 'Custom Steam Library files and managedPaths are inconsistent' }
  $expectedFiles = @($manifestFiles + 'package-manifest.json' | Sort-Object -Unique)
  $actualFiles = @(Get-ChildItem -LiteralPath $Source -Recurse -File -Force | ForEach-Object {
    Get-RelativePath $Source $_.FullName
  } | Sort-Object -Unique)
  if (Compare-Object $expectedFiles $actualFiles) {
    throw "Custom Steam Library package contains files outside its manifest: $($actualFiles -join ', ')"
  }
  $indexEntries = @($manifest.fileIndex)
  if ($indexEntries.Count -ne $manifestFiles.Count) { throw 'Custom Steam Library fileIndex is incomplete' }
  $indexedPathsRaw = @($indexEntries | ForEach-Object { Normalize-PackageRelativePath ([string]$_.path) })
  $indexedPaths = @($indexedPathsRaw | Sort-Object -Unique)
  if ($indexedPaths.Count -ne $indexedPathsRaw.Count -or (Compare-Object $manifestFiles $indexedPaths)) { throw 'Custom Steam Library fileIndex does not exactly cover files' }
  foreach ($entry in $indexEntries) {
    $relative = Normalize-PackageRelativePath ([string]$entry.path)
    $file = Join-Path $Source $relative
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Custom Steam Library manifest file is missing: $relative" }
    $entry.bytes = [int64](Get-Item -LiteralPath $file -Force).Length
    $entry.sha256 = (Get-Sha256 $file).ToLowerInvariant()
  }
  $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
  foreach ($entry in $indexEntries) {
    $relative = Normalize-PackageRelativePath ([string]$entry.path)
    $file = Join-Path $Source $relative
    if ((Get-Item -LiteralPath $file).Length -ne [int64]$entry.bytes) { throw "Custom Steam Library file size mismatch: $relative" }
    if ((Get-Sha256 $file) -ne ([string]$entry.sha256).ToUpperInvariant()) { throw "Custom Steam Library file hash mismatch: $relative" }
  }
  return $manifest
}

function Get-ZipEntrySha256([System.IO.Compression.ZipArchiveEntry]$Entry) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = $Entry.Open()
    try {
      return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    } finally {
      $stream.Dispose()
    }
  } finally {
    $sha.Dispose()
  }
}

function Sync-CustomSteamLibraryMainline([string]$Source, [string]$Version) {
  # CustomSteamLibrary is a separately compiled child, but its release input
  # must follow the same mainline package run.  Previously the packager copied
  # whatever stale child directory happened to be on disk, which allowed an
  # older child manifest and executable to enter a newer YeManCC update.
  $labRoot = Join-Path $WorkspaceRoot 'SteamArtworkLab'
  $publishScript = Join-Path $labRoot 'publish-custom-steam-library.ps1'
  $buildRoot = Join-Path $labRoot 'build'
  $required = @(
    $publishScript,
    (Join-Path $buildRoot 'SteamLibraryWorkspace.exe'),
    (Join-Path $buildRoot 'SteamArtworkLab.exe'),
    (Join-Path $labRoot 'workspace-ui\index.html'),
    (Join-Path $labRoot 'workspace-ui\app.js'),
    (Join-Path $labRoot 'workspace-ui\styles.css')
  )
  if (-not (Test-Path -LiteralPath $labRoot -PathType Container)) {
    Write-Output 'CustomSteamLibrary mainline build directory is absent; using the committed mainline child package.'
    $manifest = Assert-CustomSteamLibraryPackage $Source
    if ([string]$manifest.packageVersion -ne $Version) {
      throw "CustomSteamLibrary version did not follow mainline: child=$($manifest.packageVersion), main=$Version"
    }
    return
  }
  foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "CustomSteamLibrary mainline build is incomplete: $path"
    }
  }
  & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $publishScript `
    -TargetRoot $Source -UpdateExisting -PackageVersion $Version
  if ($LASTEXITCODE -ne 0) {
    throw "CustomSteamLibrary mainline synchronization failed: exit=$LASTEXITCODE"
  }
  Refresh-CustomSteamLibraryFileIndex $Source
  $manifestPath = Join-Path $Source 'package-manifest.json'
  $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([string]$manifest.packageVersion -ne $Version) {
    throw "CustomSteamLibrary version did not follow mainline: child=$($manifest.packageVersion), main=$Version"
  }
  return $manifest
}

function Refresh-CustomSteamLibraryFileIndex([string]$Source) {
  $manifestPath = Join-Path $Source 'package-manifest.json'
  $manifest = (Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
  foreach ($entry in @($manifest.fileIndex)) {
    $relative = Normalize-PackageRelativePath ([string]$entry.path)
    $file = Join-Path $Source $relative
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Custom Steam Library indexed file is missing: $relative" }
    $entry.bytes = [int64](Get-Item -LiteralPath $file -Force).Length
    $entry.sha256 = (Get-Sha256 $file).ToLowerInvariant()
  }
  $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}

function Get-RelativePath([string]$Root, [string]$Path) {
  $rootFull = Get-FullPath $Root
  $pathFull = Get-FullPath $Path
  return $pathFull.Substring($rootFull.Length).TrimStart('\')
}

function Test-IsExcludedPowerControlPath([string]$Relative, [bool]$IsDirectory) {
  $r = $Relative.Replace('/', '\')
  if ($r -match '^fan-host(?:-quarantine|\\|$)') { return $true }
  if ($r -match '(^|\\)(\.git|build|dist|__pycache__|KX\.bak_removed|product-old-files-[^\\]+)(\\|$)') { return $true }
  if ($r -match '^(TPD|intel|ryzenadj|tools|pawnio|OpenSpeedy|RTSS-Overlays)(\\|$)') { return $true }
  if ($IsDirectory) { return $false }
  if ($r -match '\.bak(?:_|$)' -or $r -match '\.(obj|pdb|ilk|log|pid|hb|tmp)$') { return $true }
  if ($r -match '\.(md|py|spec)$' -and $r -ine 'fan-host\YeManFanHost.authorization.md') { return $true }
  if ($r -in @('.gitignore', 'AUTOFLOAT_SPEC.md', 'Test-AutoRotation-Repair.bat', 'Test-AutoRotation-Repair.vbs', 'physpanel.exe')) { return $true }
  if ($r -match '^MG-AUTO\\(memreduct\.exe|memreduct\.exe\.sig|memreduct\.ini|memreduct\.lng|memreduct\.sig|portable\.dat)$') { return $true }
  if ($r -in @(
    'yeman-settings.json', 'yeman-settings.json.bak',
    'ui-settings.json', 'summon.json', 'music_player.json',
    'performance-schedule.json', 'game-custom.json', 'control-config.json',
    'autofloat.json', 'tdp-auto-apply.json', 'cpu_profiles.json',
    'cpu_autostart.json', 'cpu_auto_enable.json', 'cpu_lock.json',
    'launch_apps.json', 'boot_config.json', 'yeman-power-scheme.json',
    'tray_resident.json', 'autoclose.json', 'Power.txt',
    'ui-background\background.json', 'ui-background\dynamic-online.json',
    'ui-background\dynamic-cache.json', 'ui-background\background.mp4',
    'Sleep\Enable.txt', 'Sleep\Escalation.txt', 'Sleep\sleepguard.json',
    'Sleep\target.txt', 'Sleep\睡眠击杀名单.txt',
    'Sleep\quickapp_suspended.json', 'Sleep\sleep-trigger-last.txt',
    'Sleep\resleep-last.txt'
  )) { return $true }
  if ($r -match '^Sleep\\controlled-sleep') { return $true }
  if ($r -match '^(float-active|fps-monitor\.(hb|pid|log)|hwinfo-ok|hwinfo-recovery\.ts|speedhack\.log|startup_trace\.txt|topmon\.json)$') { return $true }
  if ($r -match '^(FPS-|tdp-).+\.txt$' -or $r -match '^yeman-gcm-search-result.*\.json$') { return $true }
  if ($r -match '\.json$' -and $r -notmatch '^fan-host\\') {
    throw "Unclassified PowerControl JSON must be added to the release policy: $r"
  }
  return $false
}

function Copy-PowerControlTemplates([string]$Source, [string]$Destination) {
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  foreach ($entry in Get-ChildItem -LiteralPath $Source -Recurse -Force) {
    $relative = Get-RelativePath $Source $entry.FullName
    if (Test-IsExcludedPowerControlPath $relative $entry.PSIsContainer) { continue }
    $target = Join-Path $Destination $relative
    if ($entry.PSIsContainer) {
      New-Item -ItemType Directory -Force -Path $target | Out-Null
    } else {
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
      Copy-Item -LiteralPath $entry.FullName -Destination $target -Force
    }
  }
}

function Move-ExistingReleaseItem([string]$Path, [string]$BackupRoot) {
  if (-not (Test-Path -LiteralPath $Path)) { return }
  $item = Get-Item -LiteralPath $Path -Force
  if ($item.PSIsContainer -and @(Get-ChildItem -LiteralPath $Path -Force).Count -eq 0) {
    Remove-Item -LiteralPath $Path -Force
    return
  }
  New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null
  Move-Item -LiteralPath $Path -Destination (Join-Path $BackupRoot $item.Name)
}

$ProjectRoot = Get-FullPath (Split-Path -Parent $PSScriptRoot)
$versionInfo = Get-Content -LiteralPath (Join-Path $ProjectRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$packageInfo = Get-Content -LiteralPath (Join-Path $ProjectRoot 'package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$version = [string]$versionInfo.version
$packageVersion = [string]$packageInfo.version
if ($version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw "version.json has an invalid strict version: $version" }
$versionParts = @([int64]$Matches[1], [int64]$Matches[2], [int64]$Matches[3])
if (@($versionParts | Where-Object { $_ -gt [int]::MaxValue }).Count -gt 0) { throw "version.json version part is out of range: $version" }
if ($packageVersion -ne $version) { throw "Version mismatch: version.json=$version, package.json=$packageVersion" }
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  $WorkspaceRoot = Get-FullPath (Join-Path $ProjectRoot '..\..')
} else {
  $WorkspaceRoot = Get-FullPath $WorkspaceRoot
}

$BuildRoot = Join-Path $WorkspaceRoot 'Build'
$ReleaseRoot = Join-Path $WorkspaceRoot 'Release'
$BackupReleaseRoot = Join-Path $WorkspaceRoot 'Backup\Release'
$AssetsRoot = Join-Path $WorkspaceRoot 'Assets'
$BuildWeb = Join-Path $BuildRoot 'App\Web'
$BuildNative = Join-Path $BuildRoot 'App\Native'
$PackageBuildRoot = Join-Path $BuildRoot 'Package'
$StagingRoot = Join-Path $PackageBuildRoot 'Staging'
$StagingYeManCC = Join-Path $StagingRoot 'YeManCC'
$StagingPowerControl = Join-Path $StagingRoot 'PowerControl'
$StagingCustomSteamLibrary = Join-Path $StagingYeManCC 'CustomSteamLibrary'
$UpdateRoot = Join-Path $PackageBuildRoot 'UpdateRoot'
$ReleaseYeManCC = Join-Path $ReleaseRoot 'YeManCC'
$ReleasePowerControl = Join-Path $ReleaseRoot 'PowerControl'
$ReleaseCustomSteamLibrary = Join-Path $ReleaseYeManCC 'CustomSteamLibrary'
$LegacyReleaseCustomSteamLibrary = Join-Path $ReleaseRoot 'CustomSteamLibrary'
$ReleasePackages = Join-Path $ReleaseRoot 'Packages'

$customSteamLibrarySourceFromEnv = [string]$env:YEMAN_CUSTOM_STEAM_LIBRARY_SOURCE
if (-not [string]::IsNullOrWhiteSpace($customSteamLibrarySourceFromEnv) -and [string]::IsNullOrWhiteSpace($CustomSteamLibrarySource)) {
  throw 'CustomSteamLibrary source override requires the explicit -CustomSteamLibrarySource parameter; refusing a stale environment override.'
}
$canonicalCustomSteamLibrarySource = Get-FullPath (Join-Path $ProjectRoot 'CustomSteamLibrary')
$CustomSteamLibrarySource = if ([string]::IsNullOrWhiteSpace($CustomSteamLibrarySource)) {
  $canonicalCustomSteamLibrarySource
} else {
  Get-FullPath $CustomSteamLibrarySource
}
if ($CustomSteamLibrarySource -ne $canonicalCustomSteamLibrarySource) {
  throw "CustomSteamLibrary source must be the formal mainline directory: $canonicalCustomSteamLibrarySource"
}

Sync-CustomSteamLibraryMainline $CustomSteamLibrarySource $version | Out-Null
Refresh-CustomSteamLibraryFileIndex $CustomSteamLibrarySource

# The ZIP envelope is manifest-driven. The legacy bridge keeps the old
# YeManCC + PowerControl envelope so a pre-manifest updater can install the
# new updater. The bootstrap variant also embeds the ready CustomSteamLibrary
# under YeManCC while omitting the manifest from the ZIP, so the broken
# pre-manifest PowerShell helper takes its safe no-manifest path.
$releaseEnvelopeFromEnv = [string]$env:YEMAN_RELEASE_ENVELOPE
if ([string]::IsNullOrWhiteSpace($ReleaseEnvelope)) {
  if (-not [string]::IsNullOrWhiteSpace($releaseEnvelopeFromEnv) -and $releaseEnvelopeFromEnv -ne 'full') {
    throw 'Legacy release envelope requires the explicit -ReleaseEnvelope parameter; refusing to overwrite the normal Release package.'
  }
  $releaseEnvelope = 'full'
} else {
  $releaseEnvelope = $ReleaseEnvelope
}
if ($releaseEnvelope -notin @('full', 'legacy-bridge', 'legacy-bootstrap')) {
  throw "Unsupported release envelope: $releaseEnvelope"
}
$isLegacyBootstrap = $releaseEnvelope -eq 'legacy-bootstrap'
$isLegacyBridge = $releaseEnvelope -in @('legacy-bridge', 'legacy-bootstrap')
$updateLayoutRoots = @(
  [ordered]@{ source = 'YeManCC'; target = 'YeManCC'; mode = 'program' },
  [ordered]@{ source = 'PowerControl'; target = 'PowerControl'; mode = 'power-control' }
)
$embedCustomSteamLibrary = -not $isLegacyBridge -or $isLegacyBootstrap
$requiredUpdateRoots = @($updateLayoutRoots | ForEach-Object { [string]$_.source } | Sort-Object -Unique)
$fanHostUpdatePolicy = 'preserve-existing'

foreach ($path in @($BuildRoot, $ReleaseRoot, $PackageBuildRoot, $StagingRoot, $UpdateRoot, $ReleaseYeManCC, $ReleasePowerControl, $ReleaseCustomSteamLibrary, $ReleasePackages)) {
  Assert-ChildPath $path $WorkspaceRoot 'Task5 output'
}
if ((Get-FullPath $ReleaseYeManCC) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\YeManCC'))) { throw 'Unexpected YeManCC release target' }
if ((Get-FullPath $ReleasePowerControl) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\PowerControl'))) { throw 'Unexpected PowerControl release target' }
if ((Get-FullPath $ReleaseCustomSteamLibrary) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\YeManCC\CustomSteamLibrary'))) { throw 'Unexpected CustomSteamLibrary release target' }

$requiredBuild = @(
  (Join-Path $BuildWeb 'index.html'),
  (Join-Path $BuildWeb 'assets'),
  (Join-Path $BuildWeb 'app.config.json'),
  (Join-Path $BuildNative 'YeManCC.exe')
)
foreach ($path in $requiredBuild) {
  if (-not (Test-Path -LiteralPath $path)) { throw "Build output is incomplete: $path" }
}

if (Test-Path -LiteralPath $StagingRoot) { Remove-Item -LiteralPath $StagingRoot -Recurse -Force }
if (Test-Path -LiteralPath $UpdateRoot) { Remove-Item -LiteralPath $UpdateRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $StagingYeManCC, $StagingPowerControl, $UpdateRoot | Out-Null

Copy-DirectoryContents $BuildWeb $StagingYeManCC
Copy-Item -LiteralPath (Join-Path $BuildNative 'YeManCC.exe') -Destination (Join-Path $StagingYeManCC 'YeManCC.exe') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'version.json') -Destination (Join-Path $StagingYeManCC 'version.json') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'YeMan-Support.html') -Destination (Join-Path $StagingYeManCC 'YeMan-Support.html') -Force

$sourcePowerControl = Join-Path $ProjectRoot 'PowerControl'
Copy-PowerControlTemplates $sourcePowerControl $StagingPowerControl
Assert-CustomSteamLibraryPackage $CustomSteamLibrarySource | Out-Null
if ($embedCustomSteamLibrary) {
  Copy-DirectoryContents $CustomSteamLibrarySource $StagingCustomSteamLibrary
}

$updateLayoutManifest = [ordered]@{
  schemaVersion = 1
  packageId = 'yemancc-update'
  packageVersion = $version
  requiredRoots = $requiredUpdateRoots
  roots = $updateLayoutRoots
  rules = [ordered]@{
    unknownRoots = 'reject-unless-declared'
    unknownFiles = 'preserve-when-targeted'
    rollback = 'per-root'
    fanHost = $fanHostUpdatePolicy
  }
}
$updateLayoutManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $StagingYeManCC 'update-manifest.json') -Encoding UTF8

$lockPath = Join-Path $ProjectRoot 'tools\release-assets.lock.json'
$assetLock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
$useWorkspaceAssets = Test-Path -LiteralPath $AssetsRoot -PathType Container
$assetSources = @{}
foreach ($entry in $assetLock.files) {
  $source = if ($useWorkspaceAssets) {
    Join-Path $AssetsRoot ([string]$entry.assetPath).Replace('/', '\')
  } else {
    Join-Path $ProjectRoot ([string]$entry.fallbackPath).Replace('/', '\')
  }
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Release asset is missing: $source" }
  $hash = Get-Sha256 $source
  if ($hash -ne [string]$entry.sha256) { throw "Release asset hash mismatch: $source" }
  $destination = Join-Path $StagingPowerControl ([string]$entry.releasePath).Replace('/', '\')
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
  Copy-Item -LiteralPath $source -Destination $destination -Force
  $assetSources[[string]$entry.component] = if ($useWorkspaceAssets) { 'workspace-assets' } else { 'verified-source-fallback' }
}

$requiredProgram = @('YeManCC.exe', 'index.html', 'app.config.json', 'version.json', 'YeMan-Support.html', 'update-manifest.json')
foreach ($name in $requiredProgram) {
  if (-not (Test-Path -LiteralPath (Join-Path $StagingYeManCC $name) -PathType Leaf)) { throw "Release program file is missing: $name" }
}
if (-not (Test-Path -LiteralPath (Join-Path $StagingYeManCC 'assets') -PathType Container)) { throw 'Release web assets directory is missing' }

$requiredPowerControl = @(
  'TDP',
  'OpenSpeedy\bridge32.exe', 'OpenSpeedy\bridge64.exe',
  'MG-AUTO\memreduct.exe', 'physpanel.exe',
  'pawnio\YeManTdpCtl.exe', 'pawnio\PawnIO_setup.exe',
  'pawnio\AMDFamily17.bin', 'pawnio\IntelMSR.bin',
  'pawnio\RyzenSMU.bin',
  'pawnio\_internal\python313.dll',
  'RTSS-Overlays\YeManOBS-W-1.ovl',
  'RTSS-Overlays\YeManOBS-L-1.ovl',
  'RTSS-Overlays\YeManOBS-JJ-1.ovl',
  'RTSS-Overlays\Empty.ovl'
)
foreach ($relative in $requiredPowerControl) {
  if (-not (Test-Path -LiteralPath (Join-Path $StagingPowerControl $relative))) { throw "Release PowerControl item is missing: $relative" }
}
$pawnioExpected = @($assetLock.files | Where-Object component -eq 'PawnIO' | ForEach-Object { ([string]$_.releasePath).Substring('pawnio/'.Length).Replace('/', '\') } | Sort-Object)
$pawnioActual = @(Get-ChildItem -LiteralPath (Join-Path $StagingPowerControl 'pawnio') -Recurse -File | ForEach-Object { Get-RelativePath (Join-Path $StagingPowerControl 'pawnio') $_.FullName } | Sort-Object)
if (Compare-Object $pawnioExpected $pawnioActual) { throw 'PawnIO runtime does not exactly match the locked atomic file set' }

$forbidden = @()
foreach ($file in Get-ChildItem -LiteralPath $StagingRoot -Recurse -Force -File) {
  $relative = Get-RelativePath $StagingRoot $file.FullName
  $isFormalFanHostAuthorization = $relative -ieq 'PowerControl\fan-host\YeManFanHost.authorization.md'
  $isCustomSteamLibraryManagedDocument = $relative -match '^(?:CustomSteamLibrary|YeManCC\\CustomSteamLibrary)\\(CUSTOM-STEAM-LIBRARY-INTEGRATION-CONTRACT|CUSTOM-STEAM-LIBRARY-UPGRADE-CONTRACT|SEPARATION-TASK-CUSTOM-STEAM-LIBRARY)\.md$'
  if (
    $relative -match '(^|\\)(\.git|node_modules|build|dist|testrun|outputs|__pycache__|\.workbuddy)(\\|$)' -or
    $relative -match '\.(bak(?:_|$)|obj$|pdb$|ilk$|log$|pid$|hb$|py$|spec$|ts$|vue$|cpp$)' -or
    ($relative -match '\.md$' -and -not $isFormalFanHostAuthorization -and -not $isCustomSteamLibraryManagedDocument) -or
    $relative -match '(^|\\)(yeman-settings\.json(?:\.bak)?|startup_trace\.txt|hwinfo-ok|fps-monitor\.(hb|pid|log))$' -or
    $relative -match '^PowerControl\\(TPD|intel|ryzenadj|tools)(\\|$)' -or
    $relative -match '^PowerControl\\Sleep\\(Enable\.txt|Escalation\.txt|sleepguard\.json|target\.txt|睡眠击杀名单\.txt)$'
  ) { $forbidden += $relative }
}
if ($forbidden.Count -gt 0) { throw "Forbidden files entered Release staging: $($forbidden -join ', ')" }

$publishStarted = $false
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupRoot = Join-Path $BackupReleaseRoot "PreTask5-$stamp"
try {
New-Item -ItemType Directory -Force -Path $ReleaseRoot | Out-Null
$releaseItems = @($updateLayoutRoots | ForEach-Object { Join-Path $ReleaseRoot ([string]$_.source) }) + @(
  $LegacyReleaseCustomSteamLibrary,
  $ReleasePackages,
  (Join-Path $ReleaseRoot 'version.json'),
  (Join-Path $ReleaseRoot 'release-manifest.json'),
  (Join-Path $ReleaseRoot 'release-manifest.sha256'),
  (Join-Path $ReleaseRoot 'TESTING.md')
)
foreach ($path in $releaseItems) { Move-ExistingReleaseItem $path $backupRoot }

Move-Item -LiteralPath $StagingYeManCC -Destination $ReleaseYeManCC
Move-Item -LiteralPath $StagingPowerControl -Destination $ReleasePowerControl
if (Test-Path -LiteralPath $StagingRoot) { Remove-Item -LiteralPath $StagingRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $ReleasePackages | Out-Null

# Keep every declared product directory at the ZIP root. The updater and the
# archive validator both consume this same root list, so adding a future root
# no longer requires another hand-written Copy-Item branch here.
foreach ($root in $updateLayoutRoots) {
  $source = Join-Path $ReleaseRoot ([string]$root.source)
  $destination = Join-Path $UpdateRoot ([string]$root.source)
  Copy-DirectoryContents $source $destination
}
$UpdateYeManCC = Join-Path $UpdateRoot 'YeManCC'
if ($isLegacyBootstrap) {
  Remove-Item -LiteralPath (Join-Path $UpdateYeManCC 'update-manifest.json') -Force
}

$updateTopLevel = @(Get-ChildItem -LiteralPath $UpdateRoot -Force | Select-Object -ExpandProperty Name | Sort-Object)
$declaredUpdateRoots = @($updateLayoutRoots | ForEach-Object { [string]$_.source } | Sort-Object -Unique)
$missingUpdateRoots = @($requiredUpdateRoots | Where-Object { $_ -notin $updateTopLevel })
$unexpectedUpdateRoots = @($updateTopLevel | Where-Object { $_ -notin $declaredUpdateRoots })
if ($missingUpdateRoots.Count -gt 0 -or $unexpectedUpdateRoots.Count -gt 0) {
  throw "Update ZIP roots do not match update-manifest.json; missing: $($missingUpdateRoots -join ', '); unexpected: $($unexpectedUpdateRoots -join ', '); got: $($updateTopLevel -join ', ')"
}
if (-not (Test-Path -LiteralPath (Join-Path $UpdateYeManCC 'YeManCC.exe') -PathType Leaf)) {
  throw 'Update ZIP YeManCC directory is missing YeManCC.exe'
}
if (-not (Test-Path -LiteralPath (Join-Path $UpdateRoot 'PowerControl\pawnio\YeManTdpCtl.exe') -PathType Leaf)) {
  throw 'Update ZIP PowerControl directory is missing PawnIO runtime'
}
if ($embedCustomSteamLibrary) {
  if (-not (Test-Path -LiteralPath (Join-Path $UpdateRoot 'YeManCC\CustomSteamLibrary\package-manifest.json') -PathType Leaf)) {
    throw 'Update ZIP CustomSteamLibrary directory is missing package-manifest.json'
  }
  if (-not (Test-Path -LiteralPath (Join-Path $UpdateRoot 'YeManCC\CustomSteamLibrary\CustomSteamLibrary.exe') -PathType Leaf)) {
    throw 'Update ZIP CustomSteamLibrary directory is missing CustomSteamLibrary.exe'
  }
}
if (-not $isLegacyBootstrap -and -not (Test-Path -LiteralPath (Join-Path $UpdateYeManCC 'update-manifest.json') -PathType Leaf)) {
  throw 'Update ZIP YeManCC directory is missing update-manifest.json'
}

$compatPackage = Join-Path $ReleasePackages 'YeManCC.zip'
Compress-Archive -Path (Join-Path $UpdateRoot '*') -DestinationPath $compatPackage -CompressionLevel Optimal -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipArchive = [IO.Compression.ZipFile]::OpenRead($compatPackage)
try {
  $zipRoots = @($zipArchive.Entries | ForEach-Object {
    $normalized = $_.FullName.Replace('\', '/').TrimStart('/')
    if ($normalized) { ($normalized -split '/')[0] }
  } | Sort-Object -Unique)
  $missingZipRoots = @($requiredUpdateRoots | Where-Object { $_ -notin $zipRoots })
  $unexpectedZipRoots = @($zipRoots | Where-Object { $_ -notin $declaredUpdateRoots })
  if ($missingZipRoots.Count -gt 0 -or $unexpectedZipRoots.Count -gt 0) {
    throw "YeManCC.zip roots do not match update-manifest.json; missing: $($missingZipRoots -join ', '); unexpected: $($unexpectedZipRoots -join ', '); got: $($zipRoots -join ', ')"
  }
  $layoutManifestEntries = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq 'YeManCC/update-manifest.json' })
  if ($isLegacyBootstrap) {
    if ($layoutManifestEntries.Count -ne 0) { throw 'legacy bootstrap YeManCC.zip must not contain update-manifest.json' }
  } elseif ($layoutManifestEntries.Count -ne 1) {
    throw 'YeManCC.zip must contain exactly one YeManCC/update-manifest.json'
  }
  $fanHostEntries = @($zipArchive.Entries | Where-Object {
    $_.FullName.Replace('\', '/').TrimStart('/') -match '^PowerControl/fan-host(?:/|$)'
  })
  if ($fanHostEntries.Count -gt 0) { throw "YeManCC.zip must not contain PowerControl/fan-host entries: $($fanHostEntries.FullName -join ', ')" }
  $fanHostQuarantineEntries = @($zipArchive.Entries | Where-Object {
    $_.FullName.Replace('\', '/').TrimStart('/') -match '^PowerControl/fan-host-quarantine(?:/|$)'
  })
  if ($fanHostQuarantineEntries.Count -gt 0) { throw "YeManCC.zip must not contain PowerControl/fan-host-quarantine entries: $($fanHostQuarantineEntries.FullName -join ', ')" }
  $flatEntries = @($zipArchive.Entries | Where-Object {
    $normalized = $_.FullName.Replace('\', '/').TrimStart('/')
    $normalized -match '^(YeManCC\.exe|YeMan-Support\.html|assets/|CustomSteamLibrary\.exe|SteamArtworkLab\.exe|workspace-ui/)'
  })
  if ($flatEntries.Count -gt 0) {
    throw "YeManCC.zip contains flattened YeManCC entries: $($flatEntries.FullName -join ', ')"
  }
  if ($embedCustomSteamLibrary) {
    $zipChildRoot = 'YeManCC/CustomSteamLibrary'
    $zipChildManifest = @($zipArchive.Entries | Where-Object {
      $_.FullName.Replace('\', '/').TrimStart('/') -eq "$zipChildRoot/package-manifest.json"
    })
    if ($zipChildManifest.Count -ne 1) {
      throw 'YeManCC.zip must contain exactly one nested YeManCC/CustomSteamLibrary/package-manifest.json'
    }
    $zipChildReader = New-Object IO.StreamReader($zipChildManifest[0].Open())
    try { $zipChild = $zipChildReader.ReadToEnd() | ConvertFrom-Json }
    finally { $zipChildReader.Dispose() }
    if ([string]$zipChild.packageVersion -ne $version) {
      throw "Nested CustomSteamLibrary version does not follow mainline: child=$($zipChild.packageVersion), main=$version"
    }
    foreach ($record in @($zipChild.fileIndex)) {
      $relative = Normalize-PackageRelativePath ([string]$record.path).Replace('\', '/')
      $entryPath = "$zipChildRoot/$($relative.Replace('\', '/'))"
      $entry = @($zipArchive.Entries | Where-Object { $_.FullName.Replace('\', '/').TrimStart('/') -eq $entryPath })
      if ($entry.Count -ne 1) { throw "YeManCC.zip is missing nested CustomSteamLibrary file: $relative" }
      if ([int64]$entry[0].Length -ne [int64]$record.bytes) { throw "Nested CustomSteamLibrary byte mismatch: $relative" }
      if ((Get-ZipEntrySha256 $entry[0]) -ne ([string]$record.sha256).ToUpperInvariant()) {
        throw "Nested CustomSteamLibrary SHA-256 mismatch: $relative"
      }
    }
    $flatChildEntries = @($zipArchive.Entries | Where-Object {
      $_.FullName.Replace('\', '/').TrimStart('/') -match '^CustomSteamLibrary(?:/|$)'
    })
    if ($flatChildEntries.Count -gt 0) {
      throw 'YeManCC.zip must not contain a legacy top-level CustomSteamLibrary directory'
    }
  }
} finally {
  $zipArchive.Dispose()
}
$packageHash = Get-Sha256 $compatPackage

$releaseVersion = [ordered]@{
  version = $version
  notes = [string]$versionInfo.notes
  sha256 = $packageHash
  publishedAt = (Get-Date -Format 'yyyy-MM-dd')
  package = 'Packages/YeManCC.zip'
}
$releaseVersion | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ReleaseRoot 'version.json') -Encoding UTF8

$testingLines = @(
  '# YeManCC Release Testing', '',
  "Version: $version", '',
  'Run the new executable explicitly:',
  (Join-Path $ReleaseYeManCC 'YeManCC.exe'), '',
  'The installed shortcut and scheduled task still run:',
  'C:\SOFT\YeMan\YeManCC\YeManCC.exe', '',
  'PowerControl/fan-host is excluded from this release; an existing installed',
  'Fan Host is preserved and is not overwritten by the updater.', '',
  "Update package SHA-256: $packageHash"
)
$testingLines | Set-Content -LiteralPath (Join-Path $ReleaseRoot 'TESTING.md') -Encoding UTF8

Write-Output 'PACKAGE_OK'
Write-Output "Release YeManCC:      $ReleaseYeManCC"
Write-Output "Release PowerControl: $ReleasePowerControl"
Write-Output "Release CustomSteamLibrary: $ReleaseCustomSteamLibrary"
Write-Output "Update package:       $compatPackage"
Write-Output "Package SHA256:       $packageHash"
Write-Output "Release envelope:     $releaseEnvelope"
Write-Output "Assets:               $($assetSources.Values -join ', ')"
$publishStarted = $true
} catch {
  # Release publication is transactional. If a move/copy/validation fails
  # after old output was backed up, restore every published item so the next
  # run never starts from a half-built Release directory.
  if (-not $publishStarted) {
    foreach ($path in $releaseItems) {
      if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
      }
      $backupPath = Join-Path $backupRoot ([IO.Path]::GetFileName($path))
      if (Test-Path -LiteralPath $backupPath) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        Move-Item -LiteralPath $backupPath -Destination $path -Force -ErrorAction SilentlyContinue
      }
    }
  }
  throw
}
