<#
  Static and decision-model acceptance for the external YMCC hang watchdog.
  This does not need a live sleep cycle or a WebView runtime.

  GP-XBOX-2 (2026-09-28): the death chain is no longer "UI probe failed -> kill".
  The service must read the native cross-process lifecycle snapshot first and only
  restart on a real deadlock (stale heartbeat or confirmed idle UI stall), only after the old FanHost /
  8765 ownership has been released. This script checks the same-source model and
  the required/forbidden tokens. connectedMask / virtualEndpointMask must never
  become a recovery criterion (adjudication section 7 gate 7).
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$service = Get-Content -LiteralPath (Join-Path $root 'native\recovery_service.cpp') -Raw
$native = Get-Content -LiteralPath (Join-Path $root 'native\main.cpp') -Raw

function Require-Text([string]$Text, [string]$Need, [string]$Message) {
  if (-not $Text.Contains($Need)) { throw "FAIL: $Message`nMissing: $Need" }
}
function Forbid-Text([string]$Text, [string]$Need, [string]$Message) {
  if ($Text.Contains($Need)) { throw "FAIL: $Message`nForbidden: $Need" }
}

Require-Text $service 'constexpr DWORD kStartupGraceMs = 30000;' '30-second startup grace is required'
Require-Text $service 'constexpr DWORD kRoundIntervalMs = 3000;' '3-second probe cadence is required'
Require-Text $service 'SendMessageTimeoutW(hwnd, WM_NULL' 'HWND channel must use WM_NULL'
Require-Text $service 'WM_RECOVERY_WEBVIEW_PING' 'WebView channel must use its dedicated ping'
Require-Text $service 'for (int sample = 0; sample < 3; ++sample)' 'confirmation requires three samples'
Require-Text $service 'if (hwndOk || webviewOk) return true;' 'any healthy channel must pass confirmation'
Require-Text $service 'if (!firstProbe(options, pongEvent, serial++))' 'confirmation only follows a failed first probe'
Require-Text $service '"unresponsive-confirmed"' 'confirmed hangs must be logged independently'
Require-Text $service '"restart-result"' 'restart outcome must be logged independently'
Require-Text $service 'target-termination-timeout' 'a stuck termination must not launch a duplicate YMCC'
Require-Text $service '--exit-event' 'exit-intent handshake argument must be parsed'
Require-Text $service 'restart-skipped-intentional-exit' 'intentional exit must be logged and skip restart'
Require-Text $native 'g_recoveryExitEvent' 'main process must own an exit-intent event'
Require-Text $native 'g_recoveryExitEvent) SetEvent(g_recoveryExitEvent);' 'beginAsyncExit must signal exit intent'
Require-Text $native '--exit-event' 'recovery service must receive the exit-intent event name'
# R-A (section 32 A6): the recovery service must register power notifications and
# must not sentence death inside the sleep/wake grace window.
Require-Text $service 'PowerRegisterSuspendResumeNotification' 'recovery must register power notification'
Require-Text $service 'DEVICE_NOTIFY_CALLBACK' 'power notification must use callback mode (no message pump)'
Require-Text $service 'confirmation-deferred-sleep' 'sleeping/wake-grace must defer the death sentence'
Require-Text $service 'kWakeGraceMs' 'a bounded wake grace constant must exist'

Require-Text $native 'case WM_RECOVERY_WEBVIEW_PING:' 'native window must answer recovery pings'
Require-Text $native 'sgRecordFact("sleep-orphan-recovery-start")' 'sleep orphan recovery must stay in the sleep-facts stream'
Require-Text $native 'appendFanLifecycleLog("fan-exit-handoff-result"' 'fan exit recovery must stay in the fan lifecycle stream'
if ($native.Contains('appendNativeLifecycleLog("sleep-orphan-recovery-start")')) {
  throw 'FAIL: sleep recovery must not duplicate into the generic native log'
}

# GP-XBOX-2 (2026-09-28 adjudication sections 4/5/6): lifecycle-snapshot death gate.
# Service side: snapshot read (name / seqlock / generation check / fail-closed) plus
# the ordered decision and the ten recovery-* events.
Require-Text $service 'kHeartbeatStaleMs' 'stale-heartbeat threshold must exist'
Require-Text $service 'kDeferWindowMs' 'bounded defer window must exist'
Require-Text $service 'kOwnerReleaseWaitMs' 'bounded owner-release wait must exist'
Require-Text $service 'kTerminationWaitMs' 'termination confirmation wait must exist'
Require-Text $service 'Local\\YeManCC.RecoveryState.' 'the service must read the native recovery-state mapping'
Require-Text $service 'process-start-key-mismatch' 'pid reuse (processStartKey mismatch) must be rejected'
Require-Text $service 'torn-read' 'a torn (odd-sequence) snapshot read must be rejected'
Require-Text $service 'mapping-absent' 'a missing snapshot channel must be reported explicitly'
Require-Text $service '"recovery-observation"' 'probe failure must first write recovery-observation'
Require-Text $service '"recovery-deferred-lifecycle"' 'a busy lifecycle with fresh heartbeat must defer'
Require-Text $service '"recovery-cancelled-lifecycle-settled"' 'a settled lifecycle must cancel the episode'
Require-Text $service '"recovery-blocked-state-unavailable"' 'an unreadable snapshot must fail closed'
Require-Text $service 'fail-closed-no-terminate' 'fail-closed policy must be explicit in the log'
Require-Text $service '"recovery-restart-allowed"' 'a real deadlock must be logged before terminating'
Require-Text $service '"recovery-restart-blocked-process-alive"' 'an unconfirmed target exit must block the new process'
Require-Text $service '"recovery-fan-owner-state"' 'fan ownership observations must be logged'
Require-Text $service '"recovery-restart-blocked-owner"' 'a held FanHost/8765 must block the new process'
Require-Text $service '"recovery-restart-owner-timeout"' 'an owner-release timeout must stop the hand'
Require-Text $service '"recovery-restart-started"' 'a restart start must be logged'
Require-Text $service 'probeFanHostOwner' 'the owner-release gate must probe the real FanHost/8765 ownership'
Require-Text $service 'waitForOwnerRelease' 'the restart must wait for the owner release'
Require-Text $service '--snapshot-read-once' 'the cross-process reader selftest entry must exist'
Require-Text $service '--selftest' 'the offline model selftest entry must exist'
Require-Text $service 'gate-8-replay-1744-no-kill-no-broken-pipe' 'the 17:44 replay gate must exist'
Require-Text $service 'gate-9-state-unavailable-fail-closed' 'the fail-closed gate must exist'
# Gate 7: the dual-slot masks are evidence input only and may never gate recovery.
Forbid-Text $service 'connectedMask' 'connectedMask must never be a recovery criterion'
Forbid-Text $service 'virtualEndpointMask' 'virtualEndpointMask must never be a recovery criterion'

# Native side: publish-only channel (pid + processStartKey, independent heartbeat
# thread, busy-section RAII guards, exit intent).
Require-Text $native 'RecoveryStateSnapshot' 'native must define the cross-process snapshot'
Require-Text $native 'processStartKey' 'native snapshot must carry the process generation key'
Require-Text $native 'kRecoveryStateHeartbeatMs' 'native must publish at a bounded heartbeat cadence'
Require-Text $native 'recoveryStatePublisherStart' 'native must start the publisher thread'
Require-Text $native 'recoveryStatePublisherStop' 'native must stop the publisher thread'
Require-Text $native 'recoverySnapSetExitIntent' 'exit intent must be published'
Require-Text $native 'RecoveryStateBusyGuard recoveryBusyGuard("input-host-stop")' 'inputHostStop must mark the lifecycle busy'
Require-Text $native 'RecoveryStateBusyGuard recoveryLifecycleBusy("lifecycle-work")' 'lifecycle work must mark the lifecycle busy'
Require-Text $native 'RecoveryStateBusyGuard recoveryBusyGuard("input-host-start")' 'inputHostStart must mark the lifecycle busy'
Require-Text $native 'recoverySnapNoteCommand(command)' 'the host command funnel must publish the last command'
Require-Text $native '--recovery-state-selftest' 'the native channel selftest entry must exist'
Forbid-Text $native 'connectedMask == virtualEndpointMask' 'the suspended single-slot gate must not come back'

# UI-stall evidence is independent from the publisher heartbeat (2026-10-01).
$uiProgress = Get-Content -LiteralPath (Join-Path $root 'native/recovery_ui_progress.h') -Raw -Encoding UTF8
Require-Text $native 'g_recoveryUiDispatchPulse.fetch_add' 'only the UI posted-ping handler may advance dispatch progress'
Require-Text $native 'view->reserved0 = ymcc::encodeRecoveryUiPulse' 'publish explicit UI capability and pulse in the existing ABI'
Require-Text $service 'copy.reserved0 & ymcc::kRecoveryUiPulsePresent' 'zero from an old publisher is unknown, not stalled'
Require-Text $service '"recovery-ui-stall-confirmed"' 'a stalled UI must be diagnosed before restarting'
Require-Text $service 'episode.uiProgress.reset()' 'successful probes, busy work, and sleep grace must reset the deadline'
Require-Text $service 'QueryUnbiasedInterruptTime' 'the stall deadline must not count sleep time'
Require-Text $uiProgress 'kRecoveryUiStallMs = 30000' 'transient wake latency must not restart the app'
Require-Text $uiProgress 'kRecoveryUiStallSamples = 3' 'one long probe gap is not a confirmed UI stall'
Require-Text $service 'gate-10-idle-ui-stall-fresh-publisher-restarts-once' 'regress the October 1 fresh-publisher hang'
Require-Text $service 'gate-14-busy-ui-stall-no-kill' 'preserve the protected lifecycle no-kill rule'
Require-Text $service 'gate-16-ui-stall-owner-blocked-no-second-instance' 'UI-stall recovery must keep owner release fencing'

# Same-source decision model (mirrors recovery_service.cpp recoveryDecisionRound).
# A restart is only allowed when the UI failed, the snapshot is readable, the
# publisher is stale or a supported idle UI pump has stopped for a confirmed window.
# Busy+fresh, unknown UI progress and intentional exits remain protected.
function Should-Restart([bool]$UiFailed, [bool]$SnapshotOk, [bool]$HeartbeatFresh, [bool]$ExitIntent, [bool]$Sleeping = $false, [bool]$ConfirmedUiStall = $false, [bool]$LifecycleBusy = $false) {
  if ($Sleeping) { return $false }        # R-A: never kill while sleeping / in wake grace
  if (-not $UiFailed) { return $false }
  if ($ExitIntent) { return $false }      # normal close: never restart
  if (-not $SnapshotOk) { return $false } # GP-XBOX-2 5.6: unreadable state fails closed
  if ($HeartbeatFresh) { return $ConfirmedUiStall -and (-not $LifecycleBusy) } # UI pump is distinct from the publisher
  return $true                            # 5.5: real deadlock only
}
function Should-Defer([bool]$UiFailed, [bool]$SnapshotOk, [bool]$LifecycleBusy, [bool]$HeartbeatFresh) {
  return $UiFailed -and $SnapshotOk -and $HeartbeatFresh -and $LifecycleBusy
}
function Should-Cancel([bool]$UiFailed, [bool]$SnapshotOk, [bool]$LifecycleBusy, [bool]$HeartbeatFresh) {
  return $UiFailed -and $SnapshotOk -and $HeartbeatFresh -and (-not $LifecycleBusy)
}

if (Should-Restart $false $true $true $false) { throw 'FAIL: healthy UI was restarted' }
if (Should-Restart $true $true $true $false) { throw 'FAIL: fresh heartbeat (live native) was restarted' }
if (Should-Restart $true $true $true $true) { throw 'FAIL: exit intent with failing UI was restarted' }
if (Should-Restart $true $false $false $false) { throw 'FAIL: fail-closed snapshot was treated as deadlock' }
if (-not (Should-Restart $true $true $false $false)) { throw 'FAIL: stale heartbeat + idle snapshot + no exit did not restart once' }
if (Should-Restart $true $true $false $false $true) { throw 'FAIL: sleeping system was restarted' }
if (-not (Should-Defer $true $true $true $true)) { throw 'FAIL: busy lifecycle with fresh heartbeat must defer' }
if (Should-Defer $true $true $false $true) { throw 'FAIL: idle lifecycle must not defer' }
if (-not (Should-Cancel $true $true $false $true)) { throw 'FAIL: settled lifecycle must cancel the episode' }
# 17:44 replay shape: UI failing + PREPARE_TARGET busy + fresh heartbeat -> no kill.
if (Should-Restart $true $true $true $false) { throw 'FAIL: 17:44 replay killed the lifecycle window' }

if (-not (Should-Restart $true $true $true $false $false $true $false)) { throw 'FAIL: confirmed idle UI stall was masked by a fresh publisher' }
if (Should-Restart $true $true $true $false $false $true $true) { throw 'FAIL: UI stall restarted protected lifecycle work' }
if (Should-Restart $true $true $true $false $true $true $false) { throw 'FAIL: UI stall restarted the sleeping system' }
if (Should-Restart $true $true $true $true $false $true $false) { throw 'FAIL: UI stall ignored intentional exit' }

Write-Output 'Recovery service self-test: PASS (routing + lifecycle-snapshot death gate + owner-release gate + posted-UI-progress stall gates + 1744 replay + fail-closed + sleep-defer)'