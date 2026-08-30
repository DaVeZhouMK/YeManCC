$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $PSScriptRoot '..\src\views\FanView.vue'
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8

function Assert-Contains([string]$Text, [string]$Needle, [string]$Label) {
  if (-not $Text.Contains($Needle)) { throw "FanView state-sync check failed: $Label" }
}

Assert-Contains $source 'async function adoptResidentControlState()' 'resident adoption function exists'
Assert-Contains $source 'await fanHostLifecycle.getState()' 'adoption is read-only state polling'
Assert-Contains $source "remote.hardwareWritesEnabled === true" 'adoption requires live write telemetry'
Assert-Contains $source "remote.oemRestoreConfirmed !== true" 'adoption rejects restored OEM state'
Assert-Contains $source "remote.hcCloseCleanupPending !== true" 'adoption rejects pending HC cleanup'
Assert-Contains $source "fanDiagnosticLog('ui.resident-control-adopted'" 'adoption is logged'
Assert-Contains $source 'void ensureSupported().then((ok) =>' 'mount waits for support gate'
Assert-Contains $source 'if (ok) void adoptResidentControlState();' 'mount adopts resident Host state'

$start = $source.IndexOf('async function adoptResidentControlState()')
$end = $source.IndexOf('async function applyCurveOnce()', $start)
if ($start -lt 0 -or $end -le $start) { throw 'FanView state-sync function boundary missing' }
$adoptionBody = $source.Substring($start, $end - $start)
if ($adoptionBody.Contains('fanHostLifecycle.apply(') -or
    $adoptionBody.Contains('fanHostLifecycle.open(') -or
    $adoptionBody.Contains('fanHostLifecycle.enable(') -or
    $adoptionBody.Contains('fanHostLifecycle.disable(')) {
  throw 'resident adoption must not issue a hardware lifecycle mutation'
}

Write-Output 'fan view state-sync self-test: PASS (read-only Ready adoption; no duplicate Open/Enable/Close)'
