<#
.SYNOPSIS
  Compatibility entry point for the rebuilt release chain.

.DESCRIPTION
  This script deliberately does not deploy to C:\SOFT\YeMan. It only builds
  and packages the workspace Release directories. Formal deployment requires a
  separate, explicitly authorized task.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [switch]$DeployInstalled
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot

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
  Pop-Location
}

if ($DeployInstalled) {
  Write-Output 'Release assembled and deployed to C:\SOFT\YeMan\YeManCC.'
} else {
  Write-Output 'Release assembled. No files were deployed to C:\SOFT\YeMan.'
}
