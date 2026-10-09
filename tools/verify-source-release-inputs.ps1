[CmdletBinding()]
param([string]$ProjectRoot = '')
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
$lock = Get-Content -LiteralPath (Join-Path $root 'tools\release-assets.lock.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($entry in @($lock.files)) {
  $key = [string]$entry.fallbackPath
  if (-not $key.StartsWith('PowerControl/') -or $key.Contains('..')) { throw "Unsafe locked source path: $key" }
  $file = Join-Path $root $key
  if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-FileHash -LiteralPath $file).Hash -ine [string]$entry.sha256) { throw "Repository-local locked source missing/changed: $key" }
}
$cli = Join-Path $root 'PowerControl\redist\HidHideCLI.exe'
if (-not (Test-Path -LiteralPath $cli -PathType Leaf) -or (Get-FileHash -LiteralPath $cli).Hash -ine '9DD283FEDFBD301E1A574A3D4B8663F6274CCB5C896F468ED55A6239F9ADE270') { throw 'Repository-local approved HidHide CLI missing/changed' }
Write-Output 'REPOSITORY_SOURCE_ASSETS_OK (no installed-product fallback required)'
