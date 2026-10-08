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
  [switch]$DeployInstalled
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$previousWorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT

Push-Location $ProjectRoot
try {
  if (-not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $env:YEMAN_WORKSPACE_ROOT = [IO.Path]::GetFullPath($WorkspaceRoot)
  }
  & pnpm run release
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
  Pop-Location
}

if ($DeployInstalled) {
  Write-Output 'Release assembled and deployed to C:\SOFT\YeMan\YeManCC.'
} else {
  Write-Output 'Release assembled. No files were deployed to C:\SOFT\YeMan.'
}
