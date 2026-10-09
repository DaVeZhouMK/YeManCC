[CmdletBinding()]
param(
  [ValidateSet('visible','suppressed')][string]$Result,
  [ValidateSet('game','steam','hidapi')][string]$Consumer,
  [string]$ConsumerName
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$evidencePath = Join-Path $repo '..\..\Build\Validation\HC-Parity\A1-B08-physical-hidhide-probe.json'
if (-not (Test-Path -LiteralPath $evidencePath -PathType Leaf)) { throw "Physical HidHide evidence is missing: $evidencePath" }
$evidence = Get-Content -LiteralPath $evidencePath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($evidence.status -notin @('OBSERVATION_REQUIRED','OBSERVED_NOT_SUPPRESSED','PASS')) { throw "Cannot attach consumer observation to probe status: $($evidence.status)" }
$observation = [ordered]@{
  consumer = $Consumer
  consumerName = $ConsumerName
  result = $Result
  reporter = 'interactive-user'
  source = 'user observation during the immediately preceding bounded HidHide cloak interval'
  systemMutation = $false
  recordedUtc = [DateTime]::UtcNow.ToString('o')
}
$evidence | Add-Member -NotePropertyName consumerObservation -NotePropertyValue $observation -Force
$evidence | Add-Member -NotePropertyName consumerSuppressionObserved -NotePropertyValue ($Result -eq 'suppressed') -Force
if ($Result -eq 'visible') { $evidence | Add-Member -NotePropertyName status -NotePropertyValue 'OBSERVED_NOT_SUPPRESSED' -Force }
$evidence | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $evidencePath -Encoding UTF8
Write-Output "A1 HIDHIDE CONSUMER OBSERVATION: $Result ($Consumer/$ConsumerName)"
Write-Output "Evidence: $evidencePath"
