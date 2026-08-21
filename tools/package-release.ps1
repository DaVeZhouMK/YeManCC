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

function Get-RelativePath([string]$Root, [string]$Path) {
  $rootFull = Get-FullPath $Root
  $pathFull = Get-FullPath $Path
  return $pathFull.Substring($rootFull.Length).TrimStart('\')
}

function Test-IsExcludedPowerControlPath([string]$Relative, [bool]$IsDirectory) {
  $r = $Relative.Replace('/', '\')
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
$UpdateRoot = Join-Path $PackageBuildRoot 'UpdateRoot'
$ReleaseYeManCC = Join-Path $ReleaseRoot 'YeManCC'
$ReleasePowerControl = Join-Path $ReleaseRoot 'PowerControl'
$ReleasePackages = Join-Path $ReleaseRoot 'Packages'

foreach ($path in @($BuildRoot, $ReleaseRoot, $PackageBuildRoot, $StagingRoot, $UpdateRoot, $ReleaseYeManCC, $ReleasePowerControl, $ReleasePackages)) {
  Assert-ChildPath $path $WorkspaceRoot 'Task5 output'
}
if ((Get-FullPath $ReleaseYeManCC) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\YeManCC'))) { throw 'Unexpected YeManCC release target' }
if ((Get-FullPath $ReleasePowerControl) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Release\PowerControl'))) { throw 'Unexpected PowerControl release target' }

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

$requiredProgram = @('YeManCC.exe', 'index.html', 'app.config.json', 'version.json', 'YeMan-Support.html')
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
  if (
    $relative -match '(^|\\)(\.git|node_modules|build|dist|testrun|outputs|__pycache__|\.workbuddy)(\\|$)' -or
    $relative -match '\.(bak(?:_|$)|obj$|pdb$|ilk$|log$|pid$|hb$|py$|spec$|ts$|vue$|cpp$)' -or
    $relative -match '(^|\\)(yeman-settings\.json(?:\.bak)?|startup_trace\.txt|hwinfo-ok|fps-monitor\.(hb|pid|log))$' -or
    $relative -match '^PowerControl\\(TPD|intel|ryzenadj|tools)(\\|$)' -or
    $relative -match '^PowerControl\\Sleep\\(Enable\.txt|Escalation\.txt|sleepguard\.json|target\.txt|睡眠击杀名单\.txt)$'
  ) { $forbidden += $relative }
}
if ($forbidden.Count -gt 0) { throw "Forbidden files entered Release staging: $($forbidden -join ', ')" }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupRoot = Join-Path $BackupReleaseRoot "PreTask5-$stamp"
New-Item -ItemType Directory -Force -Path $ReleaseRoot | Out-Null
foreach ($path in @(
  $ReleaseYeManCC, $ReleasePowerControl, $ReleasePackages,
  (Join-Path $ReleaseRoot 'version.json'),
  (Join-Path $ReleaseRoot 'release-manifest.json'),
  (Join-Path $ReleaseRoot 'release-manifest.sha256'),
  (Join-Path $ReleaseRoot 'TESTING.md')
)) { Move-ExistingReleaseItem $path $backupRoot }

Move-Item -LiteralPath $StagingYeManCC -Destination $ReleaseYeManCC
Move-Item -LiteralPath $StagingPowerControl -Destination $ReleasePowerControl
if (Test-Path -LiteralPath $StagingRoot) { Remove-Item -LiteralPath $StagingRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $ReleasePackages | Out-Null

# Keep both product directories at the ZIP root so extraction to C:\SOFT\YeMan
# produces sibling C:\SOFT\YeMan\YeManCC and C:\SOFT\YeMan\PowerControl.
$UpdateYeManCC = Join-Path $UpdateRoot 'YeManCC'
Copy-Item -LiteralPath $ReleaseYeManCC -Destination $UpdateYeManCC -Recurse -Force
Copy-Item -LiteralPath $ReleasePowerControl -Destination (Join-Path $UpdateRoot 'PowerControl') -Recurse -Force

$updateTopLevel = @(Get-ChildItem -LiteralPath $UpdateRoot -Force | Select-Object -ExpandProperty Name | Sort-Object)
if ($updateTopLevel.Count -ne 2 -or
    $updateTopLevel[0] -ne 'PowerControl' -or
    $updateTopLevel[1] -ne 'YeManCC') {
  throw "Update ZIP root must contain exactly YeManCC and PowerControl; got: $($updateTopLevel -join ', ')"
}
if (-not (Test-Path -LiteralPath (Join-Path $UpdateYeManCC 'YeManCC.exe') -PathType Leaf)) {
  throw 'Update ZIP YeManCC directory is missing YeManCC.exe'
}
if (-not (Test-Path -LiteralPath (Join-Path $UpdateRoot 'PowerControl\pawnio\YeManTdpCtl.exe') -PathType Leaf)) {
  throw 'Update ZIP PowerControl directory is missing PawnIO runtime'
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
  if ($zipRoots.Count -ne 2 -or $zipRoots[0] -ne 'PowerControl' -or $zipRoots[1] -ne 'YeManCC') {
    throw "YeManCC.zip root must contain exactly PowerControl and YeManCC; got: $($zipRoots -join ', ')"
  }
  $flatEntries = @($zipArchive.Entries | Where-Object {
    $normalized = $_.FullName.Replace('\', '/').TrimStart('/')
    $normalized -match '^(YeManCC\.exe|YeMan-Support\.html|assets/)'
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
Write-Output "Update package:       $compatPackage"
Write-Output "Package SHA256:       $packageHash"
Write-Output "Assets:               $($assetSources.Values -join ', ')"
