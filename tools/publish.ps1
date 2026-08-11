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
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT
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
} finally {
  Pop-Location
}

Write-Output 'Release assembled. No files were deployed to C:\SOFT\YeMan.'
