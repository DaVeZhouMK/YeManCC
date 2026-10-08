$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$validation = Join-Path $repo 'Mainline\Build\Validation\HC-Parity'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Read-Json([string]$path) { if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }; return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json }
function Status([string]$name, [string[]]$allowed) {
  $path = Join-Path $validation $name
  $json = Read-Json $path
  if ($null -eq $json) { return [ordered]@{ evidence=$name; present=$false; status='MISSING'; allowed=$allowed; valid=$false } }
  return [ordered]@{ evidence=$name; present=$true; status=[string]$json.status; allowed=$allowed; valid=($allowed -contains [string]$json.status); systemMutation=$json.systemMutation }
}
$manifest = Read-Json (Join-Path $project 'tools\input-capability-manifest-20260903.json')
$rows = @(
  (Status 'A1-hc-candidate-runtime-integrity.json' @('PASS')),
  (Status 'A1-hc-manager-graph-audit.json' @('PASS')),
  (Status 'A1-hc-parity-ledger-audit-20260903.json' @('PASS')),
  (Status 'A1-hc-input-order-audit-20260903.json' @('PASS')),
  (Status 'A1-HIDHIDE-READONLY-20260903.json' @('PASS','LOCATED')),
  (Status 'A1-third-party-input-isolation-readonly-20260903.json' @('LOCATED_UNVERIFIED','NOT_LOCATED')),
  (Status 'A1-device-state-snapshot-20260903.json' @('CONTROLLER_PRESENT')),
  (Status 'R1-xinput-physical-observation-20260903.json' @('PASS','OBSERVATION_REQUIRED')),
  (Status 'A1-B08-physical-hidhide-probe.json' @('OBSERVED_NOT_SUPPRESSED','OBSERVATION_REQUIRED','PASS')),
  (Status 'A1-gyro-virtual-release-boundary-20260903.json' @('PASS'))
)
$hardRuntimeProof = @($rows | Where-Object { $_.evidence -in @('A1-hc-manager-graph-audit.json','A1-device-state-snapshot-20260903.json') -and $_.status -ne 'PASS' }).Count -eq 0
$runtimeClosed = $false
$reasons = @()
if (-not $hardRuntimeProof) { $reasons += 'real-runtime-proof-incomplete' }
if ($manifest.closure -ne 'CLOSED' -or $manifest.runtimeVisibility -ne 'available') { $reasons += 'capability-manifest-not-closed' }
if ($manifest.capabilities.hcInputIsolation -ne $true) { $reasons += 'hc-input-isolation-unproven' }
if ($manifest.capabilities.physicalControllerAcquisition -ne $true) { $reasons += 'physical-controller-not-proven' }
if ($manifest.capabilities.gameConsumerObserved -ne $true) { $reasons += 'game-consumer-not-observed' }
$out = Join-Path $repo 'Mainline\Build\Validation\HC-Parity\GYRO-VIRTUAL-MAINLINE-STATUS-20260903.json'
$result = [ordered]@{
  evidenceId='GYRO-VIRTUAL-MAINLINE-STATUS-20260903'; status=if($runtimeClosed){'RUNTIME_CLOSED'}else{'RUNTIME_BLOCKED'}; systemMutation=$false
  designFramework='PARTIALLY_CLOSED'; f1Mock='PASS'; a1ReadOnly='PARTIAL'; runtimeClosed=$runtimeClosed; reasons=$reasons
  capabilityManifest=[ordered]@{closure=$manifest.closure; runtimeVisibility=$manifest.runtimeVisibility; realRuntimeAuthorized=$manifest.realRuntimeAuthorized; updaterExcluded=$manifest.updaterExcluded}
  evidence=$rows
  conclusion='Aggregated read-only status. Runtime closure is allowed only after physical acquisition, HC input-only isolation, and per-game-consumer isolation/recovery observations are all proven. HidHide is not considered an XInput blocker, and located third-party tools remain UNENCLOSED until isolation is proven; this report never promotes a mock or virtual-only result.'
  generatedUtc=[DateTime]::UtcNow.ToString('o')
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $out -Encoding UTF8
Write-Output "GYRO VIRTUAL MAINLINE STATUS: $($result.status)"
Write-Output "Evidence: $out"
