[CmdletBinding()]
param(
  [string]$RuntimeRoot = 'C:\SOFT\YeMan\PowerControl\handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902',
  [string]$Output = '../../Build/Validation/HC-Parity/A1-hc-dependency-location-audit.json'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-', '') }
  finally { $sha.Dispose() }
}
$names = @('WpfScreenHelper.dll','GongSolutions.WPF.DragDrop.dll','GameLib.Core.dll','SDL3-CS.dll','iNKORE.UI.WPF.Modern.dll')
$searchRoots = @(
  (Join-Path $repo 'PowerControl\fan-host-quarantine'),
  (Join-Path $repo 'Backups\Fan-V6-20260902-full-source'),
  (Join-Path $repo 'PowerControl\handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902'),
  $RuntimeRoot
) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
$locations = foreach ($name in $names) {
  $runtimePath = Join-Path $RuntimeRoot $name
  $hits = foreach ($root in $searchRoots) {
    Get-ChildItem -LiteralPath $root -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
      ForEach-Object {
        [ordered]@{
          path = $_.FullName
          sha256 = Get-Sha256 $_.FullName
          scope = if ($_.FullName.StartsWith($RuntimeRoot, [StringComparison]::OrdinalIgnoreCase)) { 'locked-runtime' } else { 'quarantine-or-backup' }
        }
      }
  }
  [ordered]@{
    name = $name
    lockedRuntimePresent = (Test-Path -LiteralPath $runtimePath -PathType Leaf)
    candidateLocations = @($hits)
  }
}
$allLockedPresent = @($locations | Where-Object { -not $_.lockedRuntimePresent }).Count -eq 0
$result = [ordered]@{
  evidenceId = 'A1-HC-DEPENDENCY-LOCATION-20260903'
  status = if ($allLockedPresent) { 'LOCKED_RUNTIME_PRESENT' } else { 'LOCATED_NOT_PROMOTABLE' }
  systemMutation = $false
  runtimeRoot = $RuntimeRoot
  dependencies = @($locations)
  conclusion = if ($allLockedPresent) { 'All five supplemental dependencies are present in the locked runtime; their exact hashes are checked by A1-HC-CANDIDATE-RUNTIME-INTEGRITY. The isolated load probe remains required before runtime closure.' } else { 'Matching names exist only in quarantine/backup scopes; they are not part of the locked runtime manifest and were neither copied nor loaded. A new asset lock and isolated approval are required before any promotion.' }
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$outPath = Join-Path $repo $Output
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outPath -Encoding UTF8
Write-Output "A1 HC DEPENDENCY LOCATION: $($result.status)"
Write-Output "Evidence: $outPath"
