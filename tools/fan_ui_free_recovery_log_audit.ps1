# fan_ui_free_recovery_log_audit.ps1 (v1) -- FAN-926 P2/R3B log adjudicator.
#
# Purpose: turn FAN-926-REAL-DEVICE-LOG-CHECKLIST into a RUNNABLE gate. You copy the log
# bundle off the machine after the real-device test and this script produces the per-row
# verdict, so the adjudication is mechanical and reproducible instead of prose.
#
# Read-only. It never starts/stops a Host, never touches %LOCALAPPDATA% live logs unless you
# point -LogRoot at them (copy them to a scratch dir and point there).
#
# Discipline encoded here (operator rule: events = low frequency, heartbeat = high frequency):
#   * every responsibility row must appear at most ONCE per episode/instance
#   * a sandbox trigger row (mock-ui-free-escalation-*) means the run is NOT a real-device run
#   * the high-frequency runtime log must NOT exist while the detailed-gate flag is absent
#
# Verdict: FAN_R3B_AUDIT=SUCCESS / HONEST-FAILURE / INCONCLUSIVE / INVALID
# Exit: 0 = SUCCESS, 3 = HONEST-FAILURE, 5 = INCONCLUSIVE, 6 = INVALID
# ASCII only (PS 5.1 reads BOM-less .ps1 as ANSI).
param(
  [Parameter(Mandatory = $true)][string]$LogRoot,
  [string]$OutFile = ''
)
$ErrorActionPreference = 'Continue'

$LogRoot = [IO.Path]::GetFullPath($LogRoot).TrimEnd('\')
$faultLog = Join-Path $LogRoot 'fan-host-fault.log'
$lifecycle = Join-Path $LogRoot 'fan-lifecycle.log'
$cycleJson = Join-Path $LogRoot 'YeManFanHost.recovery-cycle.json'
$intentJson = Join-Path $LogRoot 'YeManFanHost.control-intent.json'
$runtimeLog = Join-Path $LogRoot 'yeman-fan-host-runtime.log'
$gateFlag = Join-Path $LogRoot 'fan-logging-enabled.flag'

$problems = New-Object System.Collections.Generic.List[string]
$notes = New-Object System.Collections.Generic.List[string]
$observed = [ordered]@{}

function Read-Rows([string]$Path) {
  $rows = New-Object System.Collections.Generic.List[object]
  if (-not (Test-Path -LiteralPath $Path)) { return $rows }
  foreach ($line in [IO.File]::ReadLines($Path)) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    try { $rows.Add(($line | ConvertFrom-Json)) } catch { }
  }
  return $rows
}

function Rows-Of($Rows, [string]$Name) {
  return @($Rows | Where-Object { $_.eventName -eq $Name })
}

if (-not (Test-Path -LiteralPath $faultLog)) {
  $problems.Add("fan-host-fault.log not found under $LogRoot - point -LogRoot at the copied bundle") | Out-Null
}

$faultRows = Read-Rows $faultLog
$lifeRows = Read-Rows $lifecycle

# ---- discipline 1: no sandbox trigger on a real-device run ---------------------------------
$armed = @(Rows-Of $faultRows 'recovery.mock-ui-free-escalation-armed')
$fired = @(Rows-Of $faultRows 'recovery.mock-ui-free-escalation-fired')
$observed.sandboxTriggerRows = $armed.Count + $fired.Count
if ($observed.sandboxTriggerRows -gt 0) {
  $problems.Add('INVALID: mock-ui-free-escalation rows present - this was a SANDBOX run, not a real-device run (drop --mock-ui-free-escalation and repeat)') | Out-Null
}

# ---- discipline 2: high-frequency runtime log must not exist with the detailed gate off -----
$observed.detailedGateFlagPresent = (Test-Path -LiteralPath $gateFlag)
$observed.runtimeLogPresent = (Test-Path -LiteralPath $runtimeLog)
if ($observed.runtimeLogPresent -and -not $observed.detailedGateFlagPresent) {
  $problems.Add('the high-frequency runtime log exists although the detailed-gate flag is absent - frequency discipline violated') | Out-Null
}

# ---- R3B: exactly one same-process attempt, success gated on a REAL HC receipt --------------
$begin = @(Rows-Of $faultRows 'recovery.ui-free-same-process-begin')
$receipt = @(Rows-Of $faultRows 'recovery.ui-free-same-process-receipt')
$confirmed = @(Rows-Of $faultRows 'recovery.ui-free-same-process-confirmed')
$faulted = @(Rows-Of $faultRows 'recovery.ui-free-same-process-fault')
$requested = @(Rows-Of $faultRows 'recovery.controlled-replacement-requested')
$spawned = @(Rows-Of $faultRows 'recovery.host-replacement-spawned')
$preExit = @(Rows-Of $faultRows 'recovery.host-replacement-predecessor-exit')
$tookOver = @(Rows-Of $faultRows 'recovery.host-replacement-took-over')
$capRefused = @(Rows-Of $faultRows 'recovery.host-replacement-unavailable')
$handoffUnsettled = @(Rows-Of $faultRows 'recovery.cycle-handoff-unsettled')
$intentRestored = @(Rows-Of $faultRows 'recovery.control-intent-restored')

$observed.counts = [ordered]@{
  uiFreeBegin                  = $begin.Count
  uiFreeReceipt                = $receipt.Count
  uiFreeConfirmed              = $confirmed.Count
  uiFreeFault                  = $faulted.Count
  replacementRequested         = $requested.Count
  replacementSpawned           = $spawned.Count
  replacementPredecessorExit   = $preExit.Count
  replacementTookOver          = $tookOver.Count
  replacementCapRefused        = $capRefused.Count
  cycleHandoffUnsettled        = $handoffUnsettled.Count
  controlIntentRestored        = $intentRestored.Count
}

# at most one replacement per cycle
if ($spawned.Count -gt 1) { $problems.Add("at most ONE controlled replacement per cycle, got $($spawned.Count)") | Out-Null }

# success shape
$success = $false
$receiptAccepted = $false
$receiptDetail = 'absent'
if ($receipt.Count -gt 0) {
  $r = $receipt[$receipt.Count - 1]
  $receiptAccepted = ($r.details.accepted -eq $true)
  $receiptDetail = [string]$r.details.detail
  $observed.lastReceiptAccepted = $receiptAccepted
  $observed.lastReceiptDetail = $receiptDetail
  if ($receiptAccepted -and $receiptDetail -notmatch '^reopen:receipt') {
    $problems.Add("an accepted receipt must carry a real HC receipt detail (got '$receiptDetail')") | Out-Null
  }
  if ($receiptDetail -match 'receipt-api-absent-no-hc-backend') {
    $problems.Add('INVALID: the receipt was never consulted - the HC backend was not running; this cannot judge R3B') | Out-Null
  }
}
if ($confirmed.Count -gt 0 -and -not $receiptAccepted) {
  $problems.Add('a same-process recovery confirmation exists WITHOUT an accepted HC receipt - false success') | Out-Null
}
$success = ($confirmed.Count -eq 1 -and $receiptAccepted)

# honesty: requested alone is never success
if ($requested.Count -gt 0 -and $confirmed.Count -eq 0) {
  $notes.Add('a replacement was requested and no same-process confirmation exists - that is a REQUEST, never a recovery by itself') | Out-Null
}

# ---- P2: takeover chain ----------------------------------------------------------------------
$tookOverProofOk = $false
if ($tookOver.Count -ge 1) {
  $t = $tookOver[$tookOver.Count - 1]
  $tookOverProofOk = ([string]$t.details.proof -eq 'lifetime-mutex-recreated-only-after-predecessor-handles-closed')
  $observed.takeoverProof = [string]$t.details.proof
  if (-not $tookOverProofOk) { $problems.Add("takeover receipt must carry the mutex exit proof, got '$($t.details.proof)'") | Out-Null }
  if ($preExit.Count -lt 1) { $problems.Add('a takeover happened without an orderly predecessor-exit row') | Out-Null }
}

# inherited cycle/budget must show on a post-takeover row's context
$inherited = $null
foreach ($row in @($faultRows)) {
  $c = $row.context.recoveryCycle
  if ($null -ne $c -and $c.inherited -eq $true) { $inherited = $c }
}
if ($null -ne $inherited) {
  $observed.inheritedCycle = [ordered]@{ cycleId = $inherited.cycleId; replacements = $inherited.replacements; remainingMs = $inherited.remainingMs }
  if ([int]$inherited.replacements -lt 1) { $problems.Add('an inherited cycle must already account for the one replacement') | Out-Null }
  if ([double]$inherited.remainingMs -ge 60000) { $problems.Add('an inherited cycle must NOT have a fresh 60 s budget (remainingMs >= 60000)') | Out-Null }
} elseif ($tookOver.Count -ge 1) {
  $problems.Add('a takeover happened but no row reports an inherited cycle') | Out-Null
}

foreach ($row in @($intentRestored)) {
  if ($row.details.isClientEnable -ne $false) { $problems.Add('the intent restore must not masquerade as a client enable') | Out-Null }
  if ($row.details.authority -ne 'host-takeover-recovery') { $problems.Add("intent restore authority must be host-takeover-recovery, got '$($row.details.authority)'") | Out-Null }
  if ([int]$row.details.curveNodes -le 0) { $notes.Add('the restored intent carried 0 curve nodes - no original curve was in effect') | Out-Null }
}

# ---- CP-07: acceptance binding + native side --------------------------------------------------
$bound = @(Rows-Of $faultRows 'parent-exit-handoff.bound')
$observed.parentExitBoundRows = $bound.Count
if ($bound.Count -gt 0) {
  $b = $bound[$bound.Count - 1]
  $observed.boundCycleId = [string]$b.details.CycleId
  if ([string]::IsNullOrWhiteSpace([string]$b.details.CycleId) -or [string]::IsNullOrWhiteSpace([string]$b.details.HostInstanceId)) {
    $problems.Add('parent-exit-handoff.bound must name BOTH the host instance and the cycle') | Out-Null
  }
}
$nativeParentExit = @()
if (Test-Path -LiteralPath $lifecycle) {
  foreach ($line in [IO.File]::ReadLines($lifecycle)) {
    if ($line -match '"event":"fan-exit-parent-exit"') { try { $nativeParentExit += ($line | ConvertFrom-Json) } catch { } }
  }
}
if ($nativeParentExit.Count -gt 0) {
  $n = $nativeParentExit[$nativeParentExit.Count - 1]
  $observed.nativeParentExit = [ordered]@{
    httpOk = $n.httpOk; accepted = $n.accepted; unbound = $n.unbound
    cycleId = $n.acceptedRecoveryCycleId; hostInstanceId = $n.acceptingHostInstanceId
  }
  if ($n.accepted -ne $true -or $n.unbound -ne $false) {
    $notes.Add("native did not accept the handoff (accepted=$($n.accepted) unbound=$($n.unbound)) - a request-only or unbound shape is not a recovery") | Out-Null
  } elseif ($null -ne $observed.boundCycleId -and [string]$n.acceptedRecoveryCycleId -ne [string]$observed.boundCycleId) {
    $problems.Add('native accepted cycle id does not match the Host ledger bound cycle id') | Out-Null
  }
} else {
  $notes.Add('no fan-exit-parent-exit row in fan-lifecycle.log - the native exit handoff leg was not exercised in this capture') | Out-Null
}

# ---- release vs cleanup must stay split -------------------------------------------------------
$claimSeen = $null
foreach ($row in @($faultRows)) {
  $c = $row.context.recoveryClaimEvidence
  if ($null -ne $c) { $claimSeen = $c }
}
if ($null -ne $claimSeen) {
  $observed.releaseVsCleanup = [ordered]@{
    oemReleaseActionKind = $claimSeen.oemReleaseActionKind
    oemRestoreConfirmed = $claimSeen.oemRestoreConfirmed
    hcCloseCleanupPending = $claimSeen.hcCloseCleanupPending
  }
} else {
  $notes.Add('no recoveryClaimEvidence in context - release/cleanup split could not be checked') | Out-Null
}

# ---- verdict ----------------------------------------------------------------------------------
$invalid = @($problems | Where-Object { $_ -like 'INVALID:*' }).Count -gt 0
$verdict = 'INCONCLUSIVE'
if ($invalid) { $verdict = 'INVALID' }
elseif ($problems.Count -gt 0) { $verdict = 'INVALID' }
elseif ($success) { $verdict = 'SUCCESS' }
elseif ($faulted.Count -gt 0 -or $requested.Count -gt 0 -or $tookOver.Count -gt 0) { $verdict = 'HONEST-FAILURE' }

Write-Output '--- observed ---'
Write-Output ("R3B  sameProcessBegin={0} receipt={1} confirmed={2} fault={3}" -f $begin.Count, $receipt.Count, $confirmed.Count, $faulted.Count)
Write-Output ("P2   requested={0} spawned={1} predExit={2} tookOver={3} capRefused={4} handoffUnsettled={5} intentRestored={6}" -f `
  $requested.Count, $spawned.Count, $preExit.Count, $tookOver.Count, $capRefused.Count, $handoffUnsettled.Count, $intentRestored.Count)
if ($null -ne $observed.inheritedCycle) {
  Write-Output ("     inheritedCycle cycle={0} replacements={1} remainingMs={2}" -f `
    $observed.inheritedCycle.cycleId, $observed.inheritedCycle.replacements, $observed.inheritedCycle.remainingMs)
}
Write-Output ("CP-07 native accepted={0} unbound={1}" -f $observed.nativeParentExit.accepted, $observed.nativeParentExit.unbound)
Write-Output ("discipline sandboxRows={0} runtimeLog={1} detailedFlag={2}" -f `
  $observed.sandboxTriggerRows, $observed.runtimeLogPresent, $observed.detailedGateFlagPresent)
foreach ($n in $notes) { Write-Output ('NOTE: ' + $n) }
foreach ($p in $problems) { Write-Output ('PROBLEM: ' + $p) }

$evidence = @{
  time = (Get-Date).ToString('o')
  logRoot = $LogRoot
  verdict = ('FAN_R3B_AUDIT=' + $verdict)
  observed = $observed
  problems = @($problems)
  notes = @($notes)
}
if (-not [string]::IsNullOrWhiteSpace($OutFile)) {
  ($evidence | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $OutFile -Encoding UTF8
  Write-Output ("evidence: {0}" -f $OutFile)
}
Write-Output ('FAN_R3B_AUDIT=' + $verdict + ' problems=' + $problems.Count + ' notes=' + $notes.Count)
exit $(if ($verdict -eq 'SUCCESS') { 0 } elseif ($verdict -eq 'HONEST-FAILURE') { 3 } elseif ($verdict -eq 'INVALID') { 6 } else { 5 })
