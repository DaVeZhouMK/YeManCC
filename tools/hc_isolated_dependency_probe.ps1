[CmdletBinding()]
param(
  [string]$RuntimeRoot = 'C:\SOFT\YeMan\PowerControl\handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902',
  [string]$DependencyRoot = 'PowerControl\handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902',
  [string]$Output = '../../Build/Validation/HC-Parity/A1-hc-isolated-dependency-probe.json'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dependencyFull = (Resolve-Path (Join-Path $repo $DependencyRoot)).Path
$probeRoot = Join-Path (Join-Path $repo '..\..\Temp') ('hc-input-probe-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $probeRoot | Out-Null
Get-ChildItem -LiteralPath $RuntimeRoot -File | Copy-Item -Destination $probeRoot -Force
foreach ($name in @('WpfScreenHelper.dll','GongSolutions.WPF.DragDrop.dll','GameLib.Core.dll','SDL3-CS.dll','iNKORE.UI.WPF.Modern.dll')) {
  $source = Join-Path $dependencyFull $name
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "dependency missing in quarantine: $name" }
  Copy-Item -LiteralPath $source -Destination (Join-Path $probeRoot $name) -Force
}
$net10Probe = Join-Path $PSScriptRoot 'hc_net10_probe.ps1'
if (-not (Test-Path -LiteralPath $net10Probe -PathType Leaf)) { throw "net10 probe is missing: $net10Probe" }
# The old Windows PowerShell loader is intentionally retired. HC's managed assembly
# targets .NET 10 and GamepadMotion.dll is native; the matched net10 probe handles both
# without invoking ManagerFactory or any motion export.
& powershell -NoProfile -ExecutionPolicy Bypass -File $net10Probe -ProbeRoot $probeRoot
if ($LASTEXITCODE -ne 0) { throw 'net10 isolated dependency probe failed' }
$net10Output = Join-Path $repo '../../Build/Validation/HC-Parity/A1-hc-net10-probe.json'
if (-not (Test-Path -LiteralPath $net10Output -PathType Leaf)) { throw "net10 evidence missing: $net10Output" }
$net10 = Get-Content -LiteralPath $net10Output -Raw | ConvertFrom-Json
$probe = $net10.observed
$result = [ordered]@{
  evidenceId = 'A1-HC-ISOLATED-DEPENDENCY-PROBE-20260903'
  status = [string]$net10.status
  probeImplementation = 'net10-native-aware'
  supersedes = 'legacy-powershell-loader'
  replacementEvidence = $net10Output
  systemMutation = $false
  productRuntimeMutated = $false
  probeRoot = $probeRoot
  sourceRuntime = $RuntimeRoot
  supplementalDependencies = $dependencyFull
  invokedManagerFactory = $false
  handHeldCompanion = $probe.handHeldCompanion
  gamepadMotion = $probe.gamepadMotion
  gamepadMotionNative = $probe.gamepadMotionNative
  conclusion = 'Temporary isolated probe only; no ManagerFactory/static manager method invoked. The five supplemental files are copied from the exact-hash HC runtime candidate; this probe does not alter the locked runtime or product payload.'
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$outPath = Join-Path $repo $Output
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outPath -Encoding UTF8
Write-Output "A1 HC ISOLATED DEPENDENCY PROBE: $($result.status)"
Write-Output "Evidence: $outPath"
