[CmdletBinding()]
param([string]$ProjectRoot = '')
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
$lockPath = Join-Path $root 'deps\hidmaestro\dependency-lock.json'
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ([int]$lock.schemaVersion -ne 1 -or @($lock.files).Count -ne 3) { throw 'InputHost dependency lock is invalid' }
$expected = @(
  'deps/hidmaestro/HIDMaestro.Core.dll',
  'PowerControl/handheldcompanion-runtime/HC-CANDIDATE-0.32.4.0-06c0b954-20260902/Nefarius.Utilities.DeviceManagement.dll',
  'PowerControl/handheldcompanion-runtime/HC-CANDIDATE-0.32.4.0-06c0b954-20260902/Nefarius.Drivers.HidHide.dll'
)
$seen = @{}
foreach ($entry in @($lock.files)) {
  $key = [string]$entry.path
  if ($key -notin $expected -or $seen.ContainsKey($key)) { throw "Unexpected/duplicate InputHost dependency: $key" }
  $seen[$key] = $true
  $file = Join-Path $root $key
  if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -ne [int64]$entry.bytes -or
      (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine [string]$entry.sha256) { throw "InputHost locked dependency missing or changed: $key" }
}
Write-Output 'INPUTHOST_DEPENDENCIES_OK (3 repository-local approved DLLs)'
