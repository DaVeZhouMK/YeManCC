$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $PSScriptRoot '..\src\views\FanView.vue'
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8

function Assert-Contains([string]$Text, [string]$Needle, [string]$Label) {
  if (-not $Text.Contains($Needle)) { throw "FanView state-sync check failed: $Label" }
}
function Assert-Absent([string]$Text, [string]$Needle, [string]$Label) {
  if ($Text.Contains($Needle)) { throw "FanView state-sync check failed: $Label" }
}

Assert-Contains $source 'async function adoptResidentControlState()' 'resident adoption function exists'
Assert-Contains $source 'await fanHostLifecycle.getState()' 'adoption is read-only state polling'
Assert-Contains $source "remote.hardwareWritesEnabled === true" 'adoption requires live write telemetry'
Assert-Contains $source "remote.oemRestoreConfirmed !== true" 'adoption rejects restored OEM state'
Assert-Contains $source "remote.hcCloseCleanupPending !== true" 'adoption rejects pending HC cleanup'
Assert-Contains $source "fanDiagnosticLog('ui.resident-control-adopted'" 'adoption is logged'
# FAN-926R §2.1: entering the page starts nothing. The decision comes from the
# shared lifecycle policy; a resident session is adopted read-only, otherwise the
# page stays idle until the user turns the switch on.
Assert-Contains $source 'async function adoptOnDemandEntryState()' 'on-demand entry resolver exists'
Assert-Contains $source 'void adoptOnDemandEntryState();' 'mount defers to the on-demand entry resolver'
Assert-Contains $source 'resolveFanEntryAction(fanHostLifecycle.phase)' 'entry decision uses the shared lifecycle policy'

$mountStart = $source.IndexOf('onMounted(() => {')
$mountEnd = $source.IndexOf('async function adoptOnDemandEntryState()')
if ($mountStart -lt 0 -or $mountEnd -le $mountStart) { throw 'FanView state-sync mount boundary missing' }
$mountBody = $source.Substring($mountStart, $mountEnd - $mountStart)
Assert-Absent $mountBody 'fanHostLifecycle.start' 'mounting must not start the Fan Host'
Assert-Absent $mountBody 'ensureSupported' 'mounting must not handshake the Fan Host'

$entryStart = $source.IndexOf('async function adoptOnDemandEntryState()')
$entryEnd = $source.IndexOf('onUnmounted(() => {', $entryStart)
if ($entryStart -lt 0 -or $entryEnd -le $entryStart) { throw 'FanView on-demand entry boundary missing' }
$entryBody = $source.Substring($entryStart, $entryEnd - $entryStart)
# The persisted boot preference must not be an input, and page entry must not be
# a second start path (FAN-926R §2.1: no first-entry auto-enable).
Assert-Absent $entryBody 'startupDesired' 'page entry must not read the boot fan-control preference'
Assert-Absent $entryBody 'requestCurveApply' 'page entry must not start control on its own'
Assert-Absent $entryBody 'fanHostLifecycle.start' 'page entry must not start the Fan Host'
Assert-Absent $entryBody 'fanHostLifecycle.apply' 'page entry must not apply a curve'

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

# FAN-932 §3.2: the fan page mirrors the boot/wake preference, but it is a settings
# mirror only - it must never become a start path nor touch the lifecycle.
Assert-Contains $source 'async function onFanBootAutoStartToggle(' 'the fan page preference mirror exists'
Assert-Contains $source "saveSettingsSection('startupDesired', { fanControl: v })" 'the mirror persists the shared preference'
$mirrorStart = $source.IndexOf('async function onFanBootAutoStartToggle(')
$mirrorEnd = $source.IndexOf('const enableStageText = computed(', $mirrorStart)
if ($mirrorStart -lt 0 -or $mirrorEnd -le $mirrorStart) { throw 'FanView state-sync check failed: preference mirror boundary missing' }
$mirrorBody = $source.Substring($mirrorStart, $mirrorEnd - $mirrorStart)
Assert-Absent $mirrorBody 'fanHostLifecycle.start' 'the mirror must not start the Fan Host'
Assert-Absent $mirrorBody 'fanHostLifecycle.apply' 'the mirror must not apply a curve'
Assert-Absent $mirrorBody 'fanHostLifecycle.disable' 'the mirror must not close the Fan Host'

Write-Output 'fan view state-sync self-test: PASS (read-only Ready adoption; no duplicate Open/Enable/Close; page entry starts nothing and is not a second start path; the boot/wake preference mirror only reads/writes the setting)'
