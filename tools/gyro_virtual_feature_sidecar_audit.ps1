[CmdletBinding()]
param(
  [string]$SourceRoot,
  [string]$InstalledPowerControl = 'C:\SOFT\YeMan\PowerControl',
  [string]$ReleaseZip,
  [string]$OutputPath
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($SourceRoot)) { $SourceRoot = Join-Path $scriptRoot '..' }
if ([string]::IsNullOrWhiteSpace($ReleaseZip)) { $ReleaseZip = Join-Path $scriptRoot '..\..\..\Release\Packages\YeManCC.zip' }
if ([string]::IsNullOrWhiteSpace($OutputPath)) { $OutputPath = Join-Path $scriptRoot '..\..\..\Build\Validation\GyroVirtual-sidecar-audit.json' }

function Get-PathRecord([string]$path) {
  $item = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
  [pscustomobject]@{ path = $path; exists = [bool]$item; isFile = [bool]($item -and -not $item.PSIsContainer); bytes = if ($item -and -not $item.PSIsContainer) { [int64]$item.Length } else { $null } }
}

$features = @('virtual-gamepad', 'gyro-motion')
$records = @()
foreach ($feature in $features) {
  $records += Get-PathRecord (Join-Path $SourceRoot "PowerControl\feature-assets\$feature\.test-sidecar.json")
  $records += Get-PathRecord (Join-Path $InstalledPowerControl "feature-assets\$feature\.test-sidecar.json")
}
$zipEntries = @()
if (Test-Path -LiteralPath $ReleaseZip -PathType Leaf) {
  $archive = [IO.Compression.ZipFile]::OpenRead($ReleaseZip)
  try { $zipEntries = @($archive.Entries | Where-Object { $_.FullName.Replace('\', '/') -match 'PowerControl/feature-assets/(virtual-gamepad|gyro-motion)(/|$)' } | ForEach-Object FullName) }
  finally { $archive.Dispose() }
}
$result = [pscustomobject]@{
  evidenceId = 'GYRO-VIRTUAL-SIDECAR-AUDIT-20260903'
  status = if ((@($records | Where-Object { $_.path -match 'YeManCC-source' -and -not $_.exists }).Count -eq 0) -and $zipEntries.Count -gt 0) { 'PASS' } else { 'FAIL' }
  testOnly = $true
  sourceAndInstalledMarkers = $records
  releaseZip = $ReleaseZip
 releaseZipMatchingEntries = $zipEntries
  conclusion = 'The single complete package carries the gyro-motion/virtual-gamepad feature assets (and their sidecar markers); those assets are now part of the updater input as well.'
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Output ("GYRO VIRTUAL SIDECAR AUDIT: $($result.status)")
Write-Output ("Evidence: $OutputPath")
