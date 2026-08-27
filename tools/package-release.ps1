<#
.SYNOPSIS
  Assemble Build outputs and verified runtime assets into Release.

.DESCRIPTION
  Writes only to the workspace Build, Release and Backup areas. It never
  deploys to C:\SOFT\YeMan.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT
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
    if ((Get-Item -LiteralPath $file).Length -ne [int64]$entry.bytes) { throw "Custom Steam Library file size mismatch: $relative" }
    if ((Get-Sha256 $file) -ne ([string]$entry.sha256).ToUpperInvariant()) { throw "Custom Steam Library file hash mismatch: $relative" }
  }
  return $manifest
}

function Get-RelativePath([string]$Root, [string]$Path) {
  $rootFull = Get-FullPath $Root
  $pathFull = Get-FullPath $Path
  return $pathFull.Substring($rootFull.Length).TrimStart('\')
}

function Test-IsExcludedPowerControlPath([string]$Relative, [bool]$IsDirectory) {
  $r = $Relative.Replace('/', '\')
  # Fan Host is deliberately frozen for this release. It is not part of the
  # update payload, and an existing installed copy must remain untouched.
  if ($r -match '^fan-host(?:\\|$)') {
    return $true
  }
  if ($r -match '(^|\\)(\.git|build|dist|__pycache__|KX\.bak_removed|product-old-files-[^\\]+)(\\|$)') { return $true }
  if ($r -match '^(TPD|intel|ryzenadj|tools|pawnio|OpenSpeedy|RTSS-Overlays)(\\|$)') { return $true }
  if ($IsDirectory) { return $false }
  if ($r -match '\.bak(?:_|$)' -or $r -match '\.(obj|pdb|ilk|log|pid|hb|tmp)$') { return $true }
  if ($r -match '\.(md|py|spec)$') { return $true }
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
  if ($r -match '\.json$') {
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
$StagingCustomSteamLibrary = Join-Path $StagingRoot 'CustomSteamLibrary'
$UpdateRoot = Join-Path $PackageBuildRoot 'UpdateRoot'
$ReleaseYeManCC = Join-Path $ReleaseRoot 'YeManCC'
$ReleasePowerControl = Join-Path $ReleaseRoot 'PowerControl'
$ReleaseCustomSteamLibrary = Join-Path $ReleaseRoot 'CustomSteamLibrary'
$ReleasePackages = Join-Path $ReleaseRoot 'Packages'

$CustomSteamLibrarySource = if ([string]::IsNullOrWhiteSpace($env:YEMAN_CUSTOM_STEAM_LIBRARY_SOURCE)) {
  Join-Path $ProjectRoot 'CustomSteamLibrary'
} else {
  Get-FullPath $env:YEMAN_CUSTOM_STEAM_LIBRARY_SOURCE
}

# The ZIP envelope is manifest-driven. The legacy bridge keeps the old
# YeManCC + PowerControl envelope so a pre-manifest updater can install the
# new updater. The bootstrap variant also embeds the ready CustomSteamLibrary
# under YeManCC while omitting the manifest from the ZIP, so the broken
# pre-manifest PowerShell helper takes its safe no-manifest path.
$releaseEnvelope = if ([string]::IsNullOrWhiteSpace($env:YEMAN_RELEASE_ENVELOPE)) { 'full' } else { [string]$env:YEMAN_RELEASE_ENVELOPE }
if ($releaseEnvelope -notin @('full', 'legacy-bridge', 'legacy-bootstrap')) {
  throw "Unsupported release envelope: $releaseEnvelope"
}
$isLegacyBootstrap = $releaseEnvelope -eq 'legacy-bootstrap'
$isLegacyBridge = $releaseEnvelope -in @('legacy-bridge', 'legacy-bootstrap')
$updateLayoutRoots = @(
  [ordered]@{ source = 'YeManCC'; target = 'YeManCC'; mode = 'program' },
  [ordered]@{ source = 'PowerControl'; target = 'PowerControl'; mode = 'power-control' }
)
if (-not $isLegacyBridge) {
  $updateLayoutRoots += [ordered]@{ source = 'CustomSteamLibrary'; target = 'YeManCC\CustomSteamLibrary'; mode = 'green-child'; packageManifest = 'package-manifest.json'; stopProcesses = @('CustomSteamLibrary.exe', 'SteamArtworkLab.exe') }
}
$requiredUpdateRoots = @($updateLayoutRoots | ForEach-Object { [string]$_.source } | Sort-Object -Unique)
$fanHostUpdatePolicy = 'preserve-existing'

foreach ($path in @($BuildRoot, $ReleaseRoot, $PackageBuildRoot, $StagingRoot, $UpdateRoot, $ReleaseYeManCC, $ReleasePowerControl, $ReleaseCustomSteamLibrary, $ReleasePackages)) {
  Assert-ChildPath $path $WorkspaceRoot 'Task5 output'
}
if ((Get-FullPath $ReleaseYeManCC) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\YeManCC'))) { throw 'Unexpected YeManCC release target' }
if ((Get-FullPath $ReleasePowerControl) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\PowerControl'))) { throw 'Unexpected PowerControl release target' }
if ((Get-FullPath $ReleaseCustomSteamLibrary) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\CustomSteamLibrary'))) { throw 'Unexpected CustomSteamLibrary release target' }

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
New-Item -ItemType Directory -Force -Path $StagingYeManCC, $StagingPowerControl, $StagingCustomSteamLibrary, $UpdateRoot | Out-Null

Copy-DirectoryContents $BuildWeb $StagingYeManCC
Copy-Item -LiteralPath (Join-Path $BuildNative 'YeManCC.exe') -Destination (Join-Path $StagingYeManCC 'YeManCC.exe') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'version.json') -Destination (Join-Path $StagingYeManCC 'version.json') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'YeMan-Support.html') -Destination (Join-Path $StagingYeManCC 'YeMan-Support.html') -Force

$sourcePowerControl = Join-Path $ProjectRoot 'PowerControl'
Copy-PowerControlTemplates $sourcePowerControl $StagingPowerControl
Assert-CustomSteamLibraryPackage $CustomSteamLibrarySource | Out-Null
Copy-DirectoryContents $CustomSteamLibrarySource $StagingCustomSteamLibrary
if ($isLegacyBootstrap) {
  Copy-DirectoryContents $CustomSteamLibrarySource (Join-Path $StagingYeManCC 'CustomSteamLibrary')
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
$fanHostStagingPath = Join-Path $StagingPowerControl 'fan-host'
if (Test-Path -LiteralPath $fanHostStagingPath) { throw 'Fan Host must be excluded from this release staging area' }

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
    $relative -match '^PowerControl\\fan-host(?:\\|$)' -or
    $relative -match '^PowerControl\\Sleep\\(Enable\.txt|Escalation\.txt|sleepguard\.json|target\.txt|睡眠击杀名单\.txt)$'
  ) { $forbidden += $relative }
}
if ($forbidden.Count -gt 0) { throw "Forbidden files entered Release staging: $($forbidden -join ', ')" }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupRoot = Join-Path $BackupReleaseRoot "PreTask5-$stamp"
New-Item -ItemType Directory -Force -Path $ReleaseRoot | Out-Null
$releaseItems = @($updateLayoutRoots | ForEach-Object { Join-Path $ReleaseRoot ([string]$_.source) }) + @(
  (Join-Path $ReleaseRoot 'CustomSteamLibrary'),
  $ReleasePackages,
  (Join-Path $ReleaseRoot 'version.json'),
  (Join-Path $ReleaseRoot 'release-manifest.json'),
  (Join-Path $ReleaseRoot 'release-manifest.sha256'),
  (Join-Path $ReleaseRoot 'TESTING.md')
)
foreach ($path in $releaseItems) { Move-ExistingReleaseItem $path $backupRoot }

Move-Item -LiteralPath $StagingYeManCC -Destination $ReleaseYeManCC
Move-Item -LiteralPath $StagingPowerControl -Destination $ReleasePowerControl
Move-Item -LiteralPath $StagingCustomSteamLibrary -Destination $ReleaseCustomSteamLibrary
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
if (-not $isLegacyBridge) {
  if (-not (Test-Path -LiteralPath (Join-Path $UpdateRoot 'CustomSteamLibrary\package-manifest.json') -PathType Leaf)) {
    throw 'Update ZIP CustomSteamLibrary directory is missing package-manifest.json'
  }
  if (-not (Test-Path -LiteralPath (Join-Path $UpdateRoot 'CustomSteamLibrary\CustomSteamLibrary.exe') -PathType Leaf)) {
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
  $flatEntries = @($zipArchive.Entries | Where-Object {
    $normalized = $_.FullName.Replace('\', '/').TrimStart('/')
    $normalized -match '^(YeManCC\.exe|YeMan-Support\.html|assets/|CustomSteamLibrary\.exe|SteamArtworkLab\.exe|workspace-ui/)'
  })
  if ($flatEntries.Count -gt 0) {
    throw "YeManCC.zip contains flattened YeManCC entries: $($flatEntries.FullName -join ', ')"
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
  'Important: the current application contract still reads PowerControl from',
  'C:\SOFT\YeMan\PowerControl, so a direct Release EXE test is not a fully',
  'isolated hardware/configuration test. Task5 does not deploy either directory.', '',
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
