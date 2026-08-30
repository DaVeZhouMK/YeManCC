[CmdletBinding()]
param(
  [string]$PayloadRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl\fan-host')
)

$ErrorActionPreference = 'Stop'
$expected = [ordered]@{
  'YeManFanHost.exe' = 'A3CE266B880F67045D3F895E2FC68B1FE81F641D5F1A08A01F0DE19D2458F733'
  'YeManFanHost.dll' = '5D097826A864CD18BEC9AA8E2662DA82F80381340BAA447CF6E7FEED47F4CC74'
  'HandheldCompanion.dll' = '70E27FD4D73A5CA3E3E750DE2736B5E1C3B126D716DD9F4F5794C84DA88C6415'
  'LibreHardwareMonitorLib.dll' = 'F7ED30F07EA636C0DDCC5764C15C0A25AE8A0ACA02DED77E4AF24439955487CF'
}
$expectedManifestSha256 = '334FB177323FBE8A04306684676211DC822B4F71661823774EB8CCBDD6110D08'
$payloadFull = [IO.Path]::GetFullPath($PayloadRoot).TrimEnd('\')
if (-not (Test-Path -LiteralPath $payloadFull -PathType Container)) { throw "Fan Host V2 payload missing: $payloadFull" }

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToUpperInvariant() }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}

$manifestPath = Join-Path $payloadFull 'YeManFanHost.payload.json'
if ((Get-Sha256 $manifestPath) -ne $expectedManifestSha256) { throw 'Fan Host V2 payload manifest SHA-256 mismatch' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ([int]$manifest.schemaVersion -ne 1 -or @($manifest.files).Count -ne 102) { throw 'Fan Host V2 payload manifest schema/file count mismatch' }
$listed = @{}
foreach ($entry in $manifest.files) {
  $relative = [string]$entry.path
  if ([string]::IsNullOrWhiteSpace($relative) -or [IO.Path]::IsPathRooted($relative) -or $relative.IndexOfAny([char[]]'\/') -ge 0) { throw "unsafe manifest path: $relative" }
  if ($listed.ContainsKey($relative.ToLowerInvariant())) { throw "duplicate manifest path: $relative" }
  $path = Join-Path $payloadFull $relative
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "manifest file missing: $relative" }
  if ((Get-Sha256 $path) -ne ([string]$entry.sha256).ToUpperInvariant()) { throw "manifest file hash mismatch: $relative" }
  $listed[$relative.ToLowerInvariant()] = $true
}
foreach ($pair in $expected.GetEnumerator()) {
  $path = Join-Path $payloadFull $pair.Key
  if ((Get-Sha256 $path) -ne $pair.Value) { throw "R5-v9 pinned hash mismatch: $($pair.Key)" }
}
$physicalNames = @(Get-ChildItem -LiteralPath $payloadFull -File -Force |
  Where-Object { $_.Name -ne 'YeManFanHost.payload.json' } |
  Select-Object -ExpandProperty Name | Sort-Object)
$manifestNames = @($manifest.files | ForEach-Object { [string]$_.path } | Sort-Object)
if (Compare-Object $physicalNames $manifestNames) {
  throw 'Fan Host V2 payload has files outside or missing from manifest'
}
$auth = Get-Content -LiteralPath (Join-Path $payloadFull 'YeManFanHost.authorization.md') -Raw -Encoding UTF8
foreach ($marker in @(
  'implementationState: r5-v9-mainline-minimal-fan-host-frozen',
  'approvedHostExeSha256: A3CE266B880F67045D3F895E2FC68B1FE81F641D5F1A08A01F0DE19D2458F733',
  'approvedHostDllSha256: 5D097826A864CD18BEC9AA8E2662DA82F80381340BAA447CF6E7FEED47F4CC74',
  'approvedHcSha256: 70E27FD4D73A5CA3E3E750DE2736B5E1C3B126D716DD9F4F5794C84DA88C6415'
)) { if (-not $auth.Contains($marker)) { throw "R5-v9 authorization marker missing: $marker" } }
Write-Output 'FAN_HOST_V2_OK'
Write-Output "Payload: $payloadFull"
Write-Output "Manifest SHA256: $expectedManifestSha256"
Write-Output "Manifest files: $(@($manifest.files).Count)"
foreach ($pair in $expected.GetEnumerator()) { Write-Output "$($pair.Key): $($pair.Value)" }

