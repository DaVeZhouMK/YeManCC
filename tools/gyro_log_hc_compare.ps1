<#
.SYNOPSIS
  Gyro import/transmission log comparator against HC logic.

  Reads an input-capture.jsonl produced by the HC-parity-instrumented build
  (fields imu.event.gyro.sequence / timestamp100ns / hcTimeOfDayMs /
  receiptTick, and sample dtMs) and reports, per binding generation:

    - duration + written-sample count
    - gyro/accel event counts (sequence max) and average cadence (Hz)
    - steady-vs-bursty shape (inter-event deltas of the event timestamp)
    - dtMs distribution (HC TimerManager tick step verification)
    - FROZEN detection (generation with sequence stuck at 1)
    - host-stop / virtual-target-disabled tally from native-lifecycle.log

  HC logic anchors (source): HC consumes the latest deposited reading on its
  own TimerManager tick (SensorsManager.UpdateReport), decoupled from the
  WinRT event cadence; reading.timestamp = TimeOfDay.TotalMilliseconds.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$CaptureFile,
  [string]$LifecycleFile = ''
)

$ErrorActionPreference = 'Stop'

function FmtNum($v, $digits = 1) { if ($null -eq $v) { 'n/a' } else { [math]::Round($v, $digits) } }

$rows = foreach ($line in Get-Content -LiteralPath $CaptureFile) {
  try {
    $o = $line | ConvertFrom-Json
    if ($o.kind -eq 'sample' -and $o.imu -and $o.imu.event) {
      $gy = $o.imu.event.gyro; $ac = $o.imu.event.accel
      if ($gy -and $ac) {
        [pscustomobject]@{
          t = [int64]$o.t
          dtMs = if ($null -ne $o.dtMs) { [double]$o.dtMs } else { $null }
          gen = [int64]$gy.bindingGeneration
          gSeq = [int64]$gy.sequence
          gTick = if ($null -ne $gy.receiptTick) { [int64]$gy.receiptTick } else { 0 }
          gTod = if ($null -ne $gy.hcTimeOfDayMs) { [double]$gy.hcTimeOfDayMs } else { $null }
          aSeq = [int64]$ac.sequence
          aTick = if ($null -ne $ac.receiptTick) { [int64]$ac.receiptTick } else { 0 }
          aTod = if ($null -ne $ac.hcTimeOfDayMs) { [double]$ac.hcTimeOfDayMs } else { $null }
          vte = if ($o.testLane -and $null -ne $o.testLane.virtualTargetEnabled) { [bool]$o.testLane.virtualTargetEnabled } else { $null }
          me = if ($o.testLane -and $null -ne $o.testLane.motionEnabled) { [bool]$o.testLane.motionEnabled } else { $null }
          kind = 'sample'
        }
      }
    } elseif ($o.kind -ne $null) {
      [pscustomobject]@{ t = [int64]$o.t; dtMs = $null; gen = -1; gSeq = -1; gTick = -1; gTod = $null; aSeq = -1; aTick = -1; aTod = $null; kind = $o.kind }
    }
  } catch {}
}
if (-not $rows) { Write-Host 'NO SAMPLES FOUND' -ForegroundColor Red; exit 2 }

Write-Host '== per-generation gyro/accel event summary =='
$gens = $rows | Where-Object { $_.kind -eq 'sample' } | Group-Object gen | Sort-Object { [int64]$_.Name }
foreach ($g in $gens) {
  $s = $g.Group
  $dur = ($s[-1].t - $s[0].t)
  $durSec = if ($dur -gt 0) { $dur / 1000.0 } else { 0 }
  $gMax = ($s | Measure-Object gSeq -Maximum).Maximum
  $aMax = ($s | Measure-Object aSeq -Maximum).Maximum
  $gHz = if ($durSec -gt 0) { $gMax / $durSec } else { 0 }
  $aHz = if ($durSec -gt 0) { $aMax / $durSec } else { 0 }
  $frozen = ($durSec -gt 5 -and $gMax -le 1 -and $aMax -le 1)
  $flag = if ($frozen) { '  <-- FROZEN (sequence stuck)' } else { '' }
  "gen=$($g.Name) samples=$($s.Count) durationMs=$dur  gyroEvents=$gMax ($(FmtNum $gHz)Hz) accelEvents=$aMax ($(FmtNum $aHz)Hz)$flag"
}

Write-Host ''
Write-Host '== event-spacing shape (per generation, from event timestamp deltas) =='
foreach ($g in $gens) {
  $s = $g.Group
  foreach ($chan in @(@('gyro', 'gSeq', 'gTick', 'gTod'), @('accel', 'aSeq', 'aTick', 'aTod'))) {
    $name = $chan[0]; $seqName = $chan[1]; $todName = $chan[2]
    $prev = $null; $deltas = @()
    foreach ($r in $s) {
      if ($r.$seqName -gt 0) {
        $t = if ($null -ne $r.$todName) { $r.$todName } else { $r.$tick }
        if ($null -ne $prev -and $t -gt $prev) { $deltas += ($t - $prev) }
        $prev = $t
      }
    }
    if ($deltas.Count -gt 0) {
      $sorted = $deltas | Sort-Object
      $med = $sorted[[int]($sorted.Count / 2)]
      $pct90 = $sorted[[int]($sorted.Count * 0.9)]
      $maxGap = $sorted[-1]
      "$($g.Name)|$name spacing deltas=$($deltas.Count) median=$(FmtNum $med)ms p90=$(FmtNum $pct90)ms maxGap=$(FmtNum $maxGap)ms"
    } else { "$($g.Name)|$name spacing: no inter-event deltas" }
  }
}

Write-Host ''
Write-Host '== dtMs (HC tick step) =='
$dt = $rows | Where-Object { $null -ne $_.dtMs -and $_.dtMs -ge 0 } | ForEach-Object { $_.dtMs }
if ($dt.Count -gt 0) {
  $sorted = $dt | Sort-Object
  "dtMs count=$($dt.Count) min=$(FmtNum $sorted[0]) median=$(FmtNum $sorted[[int]($sorted.Count / 2)]) p90=$(FmtNum $sorted[[int]($sorted.Count * 0.9)]) max=$(FmtNum $sorted[-1])"
} else { 'dtMs: not present in this capture (pre-instrumentation build?)' }

Write-Host ''
Write-Host '== virtual-target / motion gate transitions (HC: target ties to app lifetime, not motion state) =='
$samples = $rows | Where-Object { $_.kind -eq 'sample' -and $null -ne $_.vte }
if ($samples.Count -gt 0) {
  $prevVte = $null; $prevMe = $null; $flipped = $false
  foreach ($r in $samples) {
    if ($null -ne $r.vte -and $null -ne $prevVte -and $r.vte -ne $prevVte) { "  t=$($r.t) virtualTargetEnabled -> $($r.vte)"; $flipped = $true }
    if ($null -ne $r.vte) { $prevVte = $r.vte }
    if ($null -ne $r.me -and $null -ne $prevMe -and $r.me -ne $prevMe) { "  t=$($r.t) motionEnabled -> $($r.me)"; $flipped = $true }
    if ($null -ne $r.me) { $prevMe = $r.me }
  }
  if (-not $flipped) { '  no gate transitions across samples' }
  "  firstVte=$($samples[0].vte) lastVte=$($samples[-1].vte) firstMotion=$($samples[0].me) lastMotion=$($samples[-1].me)"
} else { '  testLane fields absent in this capture' }

if ($LifecycleFile -and (Test-Path -LiteralPath $LifecycleFile)) {
  Write-Host ''
  Write-Host '== virtual-host stops from native-lifecycle.log =='
  $stops = Select-String -Path $LifecycleFile -Pattern '"event":"input-host-stop-request"' | ForEach-Object { try { ($_.Line | ConvertFrom-Json).reason } catch {} }
  if ($stops) { $stops | Group-Object | Sort-Object Count -Descending | ForEach-Object { "  $($_.Name): $($_.Count)" } }
  else { '  none' }
}

Write-Host ''
Write-Host 'HC logic readback: sensor event cadence is decoupled from output; per-tick (dtMs) pull of latest reading is what must look healthy. A generation marked FROZEN, or dtMs radically different from ~8-16ms, needs attention.'