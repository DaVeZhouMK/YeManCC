<#
.SYNOPSIS
  Build YeManCC into the workspace Build area without touching the installed product.

.DESCRIPTION
  Local workspace layout:
    YeManCC-source\YeManCC3 -> Build\App\Web
                             -> Build\App\Native

  CI can set YEMAN_WORKSPACE_ROOT to keep Build and Release inside the checkout.
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

$ProjectRoot = Get-FullPath (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  $WorkspaceRoot = Get-FullPath (Join-Path $ProjectRoot '..\..')
} else {
  $WorkspaceRoot = Get-FullPath $WorkspaceRoot
}

$BuildRoot = Join-Path $WorkspaceRoot 'Build'
$AppBuildRoot = Join-Path $BuildRoot 'App'
$WebBuild = Join-Path $AppBuildRoot 'Web'
$NativeBuild = Join-Path $AppBuildRoot 'Native'

Assert-ChildPath $AppBuildRoot $WorkspaceRoot 'Build output'
if ((Get-FullPath $AppBuildRoot) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Build\App'))) {
  throw "Unexpected Build output: $AppBuildRoot"
}

if (Test-Path -LiteralPath $AppBuildRoot) {
  Remove-Item -LiteralPath $AppBuildRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $WebBuild, $NativeBuild | Out-Null

$env:YEMAN_WORKSPACE_ROOT = $WorkspaceRoot
$env:YEMAN_BUILD_WEB_DIR = $WebBuild

Push-Location $ProjectRoot
try {
  & node (Join-Path $ProjectRoot 'scripts\write-version.mjs')
  if ($LASTEXITCODE -ne 0) { throw "Version generation failed: exit=$LASTEXITCODE" }

  & pnpm exec vue-tsc --noEmit
  if ($LASTEXITCODE -ne 0) { throw "Type check failed: exit=$LASTEXITCODE" }

  & pnpm exec vite build
  if ($LASTEXITCODE -ne 0) { throw "Frontend build failed: exit=$LASTEXITCODE" }

  Copy-Item -LiteralPath (Join-Path $ProjectRoot 'app.config.json') -Destination (Join-Path $WebBuild 'app.config.json') -Force

  & cmd.exe /d /c (Join-Path $ProjectRoot 'native\build_native.bat')
  if ($LASTEXITCODE -ne 0) { throw "Native build failed: exit=$LASTEXITCODE" }
} finally {
  Pop-Location
}

$required = @(
  (Join-Path $WebBuild 'index.html'),
  (Join-Path $WebBuild 'assets'),
  (Join-Path $WebBuild 'app.config.json'),
  (Join-Path $NativeBuild 'YeManCC.exe')
)
foreach ($path in $required) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Build output is incomplete: $path"
  }
}

$exe = Get-Item -LiteralPath (Join-Path $NativeBuild 'YeManCC.exe')
$hash = Get-Sha256 $exe.FullName
Write-Output "BUILD_OK"
Write-Output "Workspace: $WorkspaceRoot"
Write-Output "Web:       $WebBuild"
Write-Output "Native:    $($exe.FullName)"
Write-Output "EXE SHA256: $hash"
