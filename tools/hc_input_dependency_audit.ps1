[CmdletBinding()]
param(
  [string]$RuntimeRoot = 'C:\SOFT\YeMan\PowerControl\handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902',
  [string]$SourceRoot = 'deps/handheldcompanion-runtime/source',
  [string]$Output = '../../Build/Validation/HC-Parity/A1-hc-input-dependency-audit.json'
)
$ErrorActionPreference = 'Stop'
function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-', '') }
  finally { $sha.Dispose() }
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source = (Resolve-Path (Join-Path $repo $SourceRoot)).Path
$manifestPath = Join-Path $RuntimeRoot 'HandheldCompanion.runtime.json'
$depsPath = Join-Path $RuntimeRoot 'HandheldCompanion.deps.json'
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$deps = if (Test-Path -LiteralPath $depsPath) { Get-Content -Raw -LiteralPath $depsPath | ConvertFrom-Json } else { $null }
$knownInputDependencies = @('WpfScreenHelper.dll', 'GongSolutions.WPF.DragDrop.dll', 'GameLib.Core.dll', 'SDL3-CS.dll', 'iNKORE.UI.WPF.Modern.dll', 'System.IO.Ports.dll', 'GamepadMotion.dll')
$dependencyState = foreach ($name in $knownInputDependencies) {
  $path = Join-Path $RuntimeRoot $name
  [pscustomobject]@{ name = $name; exists = (Test-Path -LiteralPath $path -PathType Leaf); manifestListed = @($manifest.files | Where-Object { ([string]$_.path) -ieq $name }).Count -gt 0 }
}
$factory = Get-Content -Raw -LiteralPath (Join-Path $source 'Managers/ManagerFactory.cs')
$constructed = @([regex]::Matches($factory, '(?m)^\s*(\w+)\s*=\s*new\s*\(') | ForEach-Object { $_.Groups[1].Value })
$missing = @($dependencyState | Where-Object { -not $_.exists })
$allListed = @($dependencyState | Where-Object { -not $_.manifestListed }).Count -eq 0
$result = [ordered]@{
  evidenceId = 'A1-HC-INPUT-DEPENDENCY-AUDIT-20260903'
  status = if ($missing.Count -gt 0) { 'BLOCKED' } elseif (-not $allListed) { 'PARTIAL' } else { 'PASS' }
  systemMutation = $false
  runtimeRoot = $RuntimeRoot
  runtimeManifestSha256 = Get-Sha256 $manifestPath
  depsFilePresent = ($null -ne $deps)
  knownInputDependencies = @($dependencyState)
  missingKnownInputDependencies = @($missing.name)
  managerFactoryConstructedManagers = $constructed
  conclusion = if ($missing.Count -gt 0) { 'Known input load dependencies are missing from the locked runtime; do not copy or promote an input-only Host.' } elseif (-not $allListed) { 'Known dependency names are present but not all are recorded in the locked runtime manifest; keep the runtime unpromoted until the manifest is repaired.' } else { 'All known input load dependencies are present and manifest-listed in the locked runtime; isolated type/load closure remains a separate evidence gate.' }
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$outPath = Join-Path $repo $Output
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outPath -Encoding UTF8
Write-Output "A1 HC INPUT DEPENDENCY AUDIT: $($result.status) missing=$($missing.Count)"
Write-Output "Evidence: $outPath"
