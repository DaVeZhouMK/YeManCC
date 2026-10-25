[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
  [string]$HcDllPath = '',
  [string]$ArtifactsPath = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
if ([string]::IsNullOrWhiteSpace($HcDllPath)) {
  $HcDllPath = Join-Path $projectRoot 'deps\hc-runtime-upstream\HC-CANDIDATE-0.32.4.0-06c0b954-20260902\HandheldCompanion.dll'
}
$evidenceRoot = [IO.Path]::GetFullPath($EvidenceDirectory)
if ([string]::IsNullOrWhiteSpace($ArtifactsPath)) { $ArtifactsPath = Join-Path $evidenceRoot 'build' }
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$result = Join-Path $evidenceRoot 'actual-roundtrip-results.json'
$captureGlobalEvidence = $env:YMCC_SIM_GLOBAL_RUN -eq '1'
$evidenceHelper = Join-Path $projectRoot 'tools/ymcc_sim/global/capability-catalog/output-evidence.mjs'
if ($captureGlobalEvidence) {
 if ([IO.Path]::GetFullPath($ArtifactsPath) -cne [IO.Path]::GetFullPath((Join-Path $evidenceRoot 'build'))) { throw 'Global roundtrip requires owned build output' }
 & node $evidenceHelper capture --case fan.unified-capability-roundtrip --evidence-dir $evidenceRoot
 if ($LASTEXITCODE -ne 0) { throw 'Roundtrip input capture failed' }
}
& dotnet run --project (Join-Path $PSScriptRoot 'FanCapabilityRoundtrip.csproj') -c Release --artifacts-path $ArtifactsPath -- $result $HcDllPath
if ($LASTEXITCODE -ne 0) { throw "Fan capability roundtrip failed: $LASTEXITCODE" }
$record = Get-Content -LiteralPath $result -Raw -Encoding UTF8 | ConvertFrom-Json
if ($record.status -ne 'passed' -or $record.binaryStatus -ne 'passed-actual-locked-dll') {
  throw 'Actual HC DLL roundtrip evidence is incomplete'
}
if ($captureGlobalEvidence) {
 & node $evidenceHelper seal --case fan.unified-capability-roundtrip --evidence-dir $evidenceRoot
 if ($LASTEXITCODE -ne 0) { throw 'Roundtrip output/source evidence closure failed' }
}
Write-Output "FAN_CAPABILITY_ROUNDTRIP_OK: $($record.checks) checks; actual locked HC DLL; no hardware transport"
