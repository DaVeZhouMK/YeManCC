<#
.SYNOPSIS
  Deploy one matching YeManCC native/WebView2 runtime to the installed folder.

.DESCRIPTION
  The native host and WebView resources form one protocol version.  This script
  backs up replaced resources, removes stale hashed web assets, copies the
  complete current build, then verifies the index entry point and executable.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [string]$InstallRoot = 'C:\SOFT\YeMan\YeManCC'
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
  return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Get-Sha256([string]$Path) {
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Copy-WithBackup([string]$Source, [string]$Destination, [string]$BackupRoot) {
  if (Test-Path -LiteralPath $Destination -PathType Leaf) {
    Copy-Item -LiteralPath $Destination -Destination (Join-Path $BackupRoot (Split-Path -Leaf $Destination)) -Force
  }
  Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

$ProjectRoot = Get-FullPath (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  $WorkspaceRoot = Get-FullPath (Join-Path $ProjectRoot '..\..')
} else {
  $WorkspaceRoot = Get-FullPath $WorkspaceRoot
}

$WebBuild = Join-Path $WorkspaceRoot 'Build\App\Web'
$NativeExe = Join-Path $WorkspaceRoot 'Build\App\Native\YeManCC.exe'
$InstallRoot = Get-FullPath $InstallRoot
$ExpectedInstall = 'C:\SOFT\YeMan\YeManCC'
if ($InstallRoot -ne $ExpectedInstall) { throw "Unexpected install target: $InstallRoot" }
foreach ($required in @($WebBuild, (Join-Path $WebBuild 'index.html'), (Join-Path $WebBuild 'assets'), $NativeExe)) {
  if (-not (Test-Path -LiteralPath $required)) { throw "Build output is incomplete: $required" }
}
if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) { throw "Install directory is missing: $InstallRoot" }
if (Get-Process -Name 'YeManCC' -ErrorAction SilentlyContinue) { throw 'YeManCC.exe is running. Exit it before deployment.' }

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$BackupRoot = Join-Path $InstallRoot ".deployment-backup-$timestamp"
New-Item -ItemType Directory -Path $BackupRoot | Out-Null

# The hashed asset folder must be an exact copy. Keeping old chunks allows an
# old index.html to silently bind to a new native IPC protocol.
foreach ($folder in @('assets', 'icons')) {
  $destination = Join-Path $InstallRoot $folder
  if (Test-Path -LiteralPath $destination) {
    Move-Item -LiteralPath $destination -Destination (Join-Path $BackupRoot $folder)
  }
}

foreach ($item in Get-ChildItem -LiteralPath $WebBuild -Force) {
  $destination = Join-Path $InstallRoot $item.Name
  if ($item.PSIsContainer) {
    Copy-Item -LiteralPath $item.FullName -Destination $destination -Recurse -Force
  } else {
    Copy-WithBackup $item.FullName $destination $BackupRoot
  }
}
Copy-WithBackup $NativeExe (Join-Path $InstallRoot 'YeManCC.exe') $BackupRoot
foreach ($name in @('version.json', 'YeMan-Support.html')) {
  $source = Join-Path $ProjectRoot $name
  if (Test-Path -LiteralPath $source -PathType Leaf) {
    Copy-WithBackup $source (Join-Path $InstallRoot $name) $BackupRoot
  }
}

$installedIndex = Get-Content -LiteralPath (Join-Path $InstallRoot 'index.html') -Raw -Encoding UTF8
if ($installedIndex -notmatch 'src="\./(assets/[^\"]+\.js)"') { throw 'Installed index.html has no hashed JavaScript entry point' }
$entry = $Matches[1]
$sourceEntry = Join-Path $WebBuild $entry
$installedEntry = Join-Path $InstallRoot $entry
if (-not (Test-Path -LiteralPath $installedEntry -PathType Leaf)) { throw "Installed entry point is missing: $entry" }
if ((Get-Sha256 $sourceEntry) -ne (Get-Sha256 $installedEntry)) { throw "Installed entry point differs from build: $entry" }
if ((Get-Sha256 $NativeExe) -ne (Get-Sha256 (Join-Path $InstallRoot 'YeManCC.exe'))) { throw 'Installed executable differs from build' }

$sourceAssets = @(Get-ChildItem -LiteralPath (Join-Path $WebBuild 'assets') -Recurse -File | ForEach-Object { $_.FullName.Substring((Join-Path $WebBuild 'assets').Length).TrimStart('\') } | Sort-Object)
$installedAssets = @(Get-ChildItem -LiteralPath (Join-Path $InstallRoot 'assets') -Recurse -File | ForEach-Object { $_.FullName.Substring((Join-Path $InstallRoot 'assets').Length).TrimStart('\') } | Sort-Object)
if (Compare-Object $sourceAssets $installedAssets) { throw 'Installed web asset set differs from the build' }

Write-Output 'DEPLOY_OK'
Write-Output "Installed: $InstallRoot"
Write-Output "Backup:    $BackupRoot"
Write-Output "Entry:     $entry"
