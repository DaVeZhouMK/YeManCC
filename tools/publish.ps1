<#
.SYNOPSIS
  Compatibility entry point for the rebuilt release chain.

.DESCRIPTION
  This script deliberately does not deploy to C:\SOFT\YeMan. It only builds
  and packages the workspace Release directories. Formal deployment requires a
  separate, explicitly authorized task.
  YMCC standard export and this release entry converge on package-release.ps1
  (package.json: release -> build -> package). HC-SLIM-01 keeps the four large
  runtime DLLs and HIDMaestro; the two duplicate utility placements are omitted
  only while their pinned HC-runtime authorities remain available. No separate
  legacy ZIP or unverified slimming policy is assembled by this wrapper.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [switch]$DeployInstalled,
  [switch]$TestExport,
  [switch]$FullAcceptance,
  [switch]$FastRelease
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$previousWorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT
$previousTestExport = $env:YEMAN_TEST_EXPORT
$previousFullAcceptance = $env:YEMAN_FULL_ACCEPTANCE

Push-Location $ProjectRoot
try {
  if (-not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $env:YEMAN_WORKSPACE_ROOT = [IO.Path]::GetFullPath($WorkspaceRoot)
  }
  # Explicit test export is local-only; ordinary export defaults to no fixtures; FullAcceptance is explicit.
  if ($TestExport -and $FullAcceptance) { throw 'Choose ordinary export or explicit FullAcceptance.' }
  if ($FastRelease -and ($FullAcceptance -or $TestExport)) { throw 'FastRelease is separate from TestExport and FullAcceptance.' }
  $TestExport = -not [bool]$FullAcceptance
  $env:YEMAN_FULL_ACCEPTANCE = if ($FullAcceptance) { '1' } else { '0' }
  $env:YEMAN_TEST_EXPORT = if ($FullAcceptance) { '0' } else { '1' }
  if ($TestExport -and -not $FastRelease) { Write-Output 'YMCC_TEST_EXPORT: complete local test package; formal release gates are not accepted.' }
  if ($FastRelease) {
    Write-Output 'YMCC_FAST_RELEASE: fixtures NOT_RUN; compilation, locked payload and ZIP identity remain required.'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\build-workspace.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -FastRelease
    if ($LASTEXITCODE -ne 0) { throw "Fast release build failed: exit=$LASTEXITCODE" }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\package-release.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -FastRelease
  } elseif ($FullAcceptance) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\build-workspace.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -FullAcceptance
    if ($LASTEXITCODE -ne 0) { throw "Full acceptance build failed: exit=$LASTEXITCODE" }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\package-release.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -FullAcceptance
  } else {
    & pnpm run release
  }
  if ($LASTEXITCODE -ne 0) {
    throw "Release build failed: exit=$LASTEXITCODE"
  }
  if ($DeployInstalled) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\deploy-installed.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT
    if ($LASTEXITCODE -ne 0) {
      throw "Installed deployment failed: exit=$LASTEXITCODE"
    }
  }
} finally {
  $env:YEMAN_WORKSPACE_ROOT = $previousWorkspaceRoot
  $env:YEMAN_TEST_EXPORT = $previousTestExport
  $env:YEMAN_FULL_ACCEPTANCE = $previousFullAcceptance
  Pop-Location
}

if ($DeployInstalled) {
  Write-Output 'Release assembled and deployed to C:\SOFT\YeMan\YeManCC.'
} else {
  Write-Output 'Release assembled. No files were deployed to C:\SOFT\YeMan.'
}
