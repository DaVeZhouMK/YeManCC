[CmdletBinding()]
param(
  [string]$RuntimeRoot = "C:\SOFT\YeMan\PowerControl\handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902",
  [string]$Output = "../../Build/Validation/HC-Parity/A1-hc-candidate-runtime-integrity.json"
)
$ErrorActionPreference = 'Stop'
function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path))).Replace('-', '').ToLowerInvariant()) }
  finally { $sha.Dispose() }
}
$manifestPath = Join-Path $RuntimeRoot 'HandheldCompanion.runtime.json'
if (!(Test-Path -LiteralPath $manifestPath)) { throw "runtime manifest missing: $manifestPath" }
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$results = foreach ($entry in $manifest.files) {
  $path = Join-Path $RuntimeRoot ([string]$entry.path)
  if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
    [pscustomobject]@{ path = $entry.path; expected = $entry.sha256; actual = $null; status = 'missing' }
    continue
  }
  $actual = Get-Sha256 $path
  [pscustomobject]@{ path = $entry.path; expected = ([string]$entry.sha256).ToLowerInvariant(); actual = $actual; status = if ($actual -eq ([string]$entry.sha256).ToLowerInvariant()) { 'match' } else { 'mismatch' } }
}
$summary = [ordered]@{
  evidenceId = 'A1-HC-CANDIDATE-RUNTIME-INTEGRITY-20260903'
  runtimeId = $manifest.runtimeId
  hcVersion = $manifest.hcVersion
  hcCommit = $manifest.hcCommit
  manifestSha256 = Get-Sha256 $manifestPath
  fileCount = @($results).Count
  matchCount = @($results | Where-Object status -eq 'match').Count
  missingCount = @($results | Where-Object status -eq 'missing').Count
  mismatchCount = @($results | Where-Object status -eq 'mismatch').Count
  status = if (@($results | Where-Object status -ne 'match').Count -eq 0) { 'PASS' } else { 'FAIL' }
  files = @($results)
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outPath = Join-Path $repo $Output
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $outPath -Encoding UTF8
Write-Output "HC CANDIDATE RUNTIME INTEGRITY: $($summary.status) matches=$($summary.matchCount)/$($summary.fileCount)"
Write-Output "Evidence: $outPath"
