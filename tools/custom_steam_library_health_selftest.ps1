[CmdletBinding()]
param(
  [string]$PackageRoot = ''
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($PackageRoot)) { $PackageRoot = Join-Path $PSScriptRoot '..\CustomSteamLibrary' }

function Assert-True([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw "custom steam library health self-test: $Message" }
}

function Stop-TestProcess([System.Diagnostics.Process]$Process) {
  if (-not $Process) { return }
  try { if (-not $Process.HasExited) { $Process.CloseMainWindow() | Out-Null } } catch { }
  $deadline = (Get-Date).AddSeconds(10)
  while ((Get-Date) -lt $deadline) {
    try { if ($Process.HasExited) { return } } catch { return }
    Start-Sleep -Milliseconds 100
  }
  try { if (-not $Process.HasExited) { Stop-Process -Id $Process.Id -Force -ErrorAction Stop; $Process.WaitForExit(10000) } } catch { }
}

function Stop-TestWebViewProcesses([string]$Root) {
  for ($attempt = 1; $attempt -le 20; $attempt++) {
    $active = @(Get-CimInstance Win32_Process -Filter "Name = 'msedgewebview2.exe'" -ErrorAction SilentlyContinue |
      Where-Object { $_.CommandLine -and $_.CommandLine.Contains($Root) })
    foreach ($item in $active) { Stop-Process -Id ([int]$item.ProcessId) -Force -ErrorAction SilentlyContinue }
    if ($active.Count -eq 0) { return }
    Start-Sleep -Milliseconds 250
  }
}

function Invoke-HealthProbe([string]$Root, [string]$DataRoot, [bool]$ExpectSuccess) {
  $manifest = Get-Content -LiteralPath (Join-Path $Root 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  $marker = Join-Path $Root ('.health-' + [guid]::NewGuid().ToString('N') + '.json')
  $token = [guid]::NewGuid().ToString('N')
  $process = $null
  try {
    $arguments = @(
      '--input-owner=host',
      '--update-health-only',
      '--update-health-handshake', ('"' + $marker + '"'),
      '--update-health-handshake-token', $token,
      '--update-health-package-version', [string]$manifest.packageVersion
    )
    $process = Start-Process -FilePath (Join-Path $Root 'CustomSteamLibrary.exe') -WorkingDirectory $Root -ArgumentList $arguments -PassThru -WindowStyle Hidden
    Assert-True ([bool]$process) "health process was not started"
    $deadline = (Get-Date).AddSeconds(90)
    $observed = $null
    while ((Get-Date) -lt $deadline) {
      if (Test-Path -LiteralPath $marker -PathType Leaf) {
        try { $observed = Get-Content -LiteralPath $marker -Raw -Encoding UTF8 | ConvertFrom-Json } catch { }
        if ($observed -and [string]$observed.phase -in @('ready', 'failed')) { break }
      }
      try { if ($process.HasExited) { break } } catch { break }
      Start-Sleep -Milliseconds 200
    }
    if ($ExpectSuccess) {
      Assert-True ($observed -and [string]$observed.phase -eq 'ready') "expected ready marker, got $($observed | ConvertTo-Json -Compress)"
      Assert-True ([string]$observed.token -eq $token) 'health token mismatch'
      Assert-True ([int]$observed.pid -eq $process.Id) 'health pid mismatch'
      Assert-True ([string]$observed.packageId -eq 'custom-steam-library') 'health package id mismatch'
      Assert-True ([string]$observed.packageVersion -eq [string]$manifest.packageVersion) 'health package version mismatch'
      Assert-True ([bool]$observed.healthOnly -and [string]$observed.inputOwner -eq 'host') 'health startup mode mismatch'
      Assert-True ([int]$observed.protocol -eq 1) 'health protocol mismatch'
      Assert-True ([bool]$observed.worker -and [bool]$observed.workspaceUi -and [bool]$observed.webview2) 'runtime readiness flags incomplete'
      Assert-True ([bool]$observed.dataRootWritable) 'data root probe was not reported writable'
      Assert-True ([IO.Path]::GetFullPath([string]$observed.dataRoot) -eq [IO.Path]::GetFullPath($DataRoot) ) 'health data root mismatch'
    } else {
      Assert-True ($observed -and [string]$observed.phase -eq 'failed') "expected failed marker, got $($observed | ConvertTo-Json -Compress)"
      Assert-True (-not [string]::IsNullOrWhiteSpace([string]$observed.error)) 'failed marker did not include an error'
    }
  } finally {
    Stop-TestProcess $process
    Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
  }
}

$sourceRoot = [IO.Path]::GetFullPath($PackageRoot)
Assert-True (Test-Path -LiteralPath (Join-Path $sourceRoot 'CustomSteamLibrary.exe') -PathType Leaf) 'package host is missing'
Assert-True (Test-Path -LiteralPath (Join-Path $sourceRoot 'SteamArtworkLab.exe') -PathType Leaf) 'package worker is missing'
Assert-True (Test-Path -LiteralPath (Join-Path $sourceRoot 'workspace-ui\index.html') -PathType Leaf) 'package UI is missing'

$tempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$testRoot = Join-Path $tempParent ('yeman-custom-health-' + [guid]::NewGuid().ToString('N'))
Assert-True $testRoot.StartsWith($tempParent + '\', [StringComparison]::OrdinalIgnoreCase) 'unsafe temporary test path'
$portableRoot = Join-Path $testRoot 'package'
$dataRoot = Join-Path $testRoot 'data'
$brokenRoot = Join-Path $testRoot 'broken'
$previousDataRoot = [Environment]::GetEnvironmentVariable('YEMAN_STEAM_BIG_PICTURE_DATA_ROOT', 'Process')

try {
  New-Item -ItemType Directory -Force -Path $portableRoot, $dataRoot, $brokenRoot | Out-Null
  Get-ChildItem -LiteralPath $sourceRoot -Force | Copy-Item -Destination $portableRoot -Recurse -Force
  Get-ChildItem -LiteralPath $sourceRoot -Force | Copy-Item -Destination $brokenRoot -Recurse -Force
  Remove-Item -LiteralPath (Join-Path $brokenRoot 'workspace-ui') -Recurse -Force
  [Environment]::SetEnvironmentVariable('YEMAN_STEAM_BIG_PICTURE_DATA_ROOT', $dataRoot, 'Process')
  Invoke-HealthProbe $portableRoot $dataRoot $true
  Invoke-HealthProbe $brokenRoot $dataRoot $false
  Write-Output 'custom steam library health self-test: PASS'
  Write-Output 'success: independent hidden startup, WebView2, worker, data-root and marker authentication'
  Write-Output 'failure: missing workspace-ui emitted a failed marker without hanging the updater'
} finally {
  [Environment]::SetEnvironmentVariable('YEMAN_STEAM_BIG_PICTURE_DATA_ROOT', $previousDataRoot, 'Process')
  Stop-TestWebViewProcesses $testRoot
  for ($attempt = 1; $attempt -le 20 -and (Test-Path -LiteralPath $testRoot); $attempt++) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $testRoot) { Start-Sleep -Milliseconds 250 }
  }
  if (Test-Path -LiteralPath $testRoot) { throw "health self-test temporary directory cleanup failed: $testRoot" }
}
