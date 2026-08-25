$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$native = Get-Content -LiteralPath (Join-Path $repoRoot 'native\main.cpp') -Raw
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $repoRoot)
$hostSource = Get-Content -LiteralPath (Join-Path $workspaceRoot 'FanLab\real-host\Program.cs') -Raw
$appSource = Get-Content -LiteralPath (Join-Path $repoRoot 'src\App.vue') -Raw

function Require-Contains {
    param([string]$Source, [string]$Needle, [string]$Name)
    if (-not $Source.Contains($Needle)) { throw "fan exit cleanup source check failed: $Name" }
}

$appUnmountBlocks = [regex]::Matches($appSource, 'onUnmounted\(\(\) => \{(?s:.*?)\}\);')
foreach ($block in $appUnmountBlocks) {
    if ($block.Value -match 'fanHostLifecycle\.close\s*\(') {
        throw 'fan exit cleanup source check failed: root unmount must not issue a second /api/close; native app-exit/NavRail owns the single HC close boundary'
    }
}

$postStart = $native.IndexOf('static bool fanHostEmergencyPost')
$postEnd = $native.IndexOf('static bool fanHostEmergencySuspend', $postStart)
$postBody = if ($postStart -ge 0 -and $postEnd -gt $postStart) { $native.Substring($postStart, $postEnd - $postStart) } else { '' }
$exitStart = $native.IndexOf('static DWORD WINAPI exitCleanupThreadProc')
$exitEnd = $native.IndexOf('static void beginAsyncExit', $exitStart)
$exitBody = if ($exitStart -ge 0 -and $exitEnd -gt $exitStart) { $native.Substring($exitStart, $exitEnd - $exitStart) } else { '' }
$readyStart = $native.IndexOf('case WM_APP_EXIT_READY:')
$readyEnd = $native.IndexOf('case WM_POWER_RESUME_READY:', $readyStart)
$readyBody = if ($readyStart -ge 0 -and $readyEnd -gt $readyStart) { $native.Substring($readyStart, $readyEnd - $readyStart) } else { '' }
Require-Contains $native 'static bool fanHostExactProcessRunning()' 'exact Host identity check'
Require-Contains $native 'static bool fanHostOwnsLoopbackPort' 'Host loopback ownership check'
Require-Contains $native 'GetExtendedTcpTable' 'OS TCP listener ownership proof'
Require-Contains $native 'fanHostOwnsLoopbackPort(entry.th32ProcessID)' 'exact Host check binds process identity to port owner'
Require-Contains $native 'static std::string fanHostReadSessionToken()' 'session sidecar reader'
Require-Contains $postBody 'X-YeMan-Fan-Session:' 'authenticated native Host request'
Require-Contains $postBody 'if (!fanHostExactProcessRunning()) return false;' 'identity gate before token use'
Require-Contains $postBody 'state.contains("hardwareWritesEnabled")' 'canonical live-write telemetry field'
Require-Contains $postBody 'state.value("hardwareWrites", false)' 'legacy live-write telemetry alias'
Require-Contains $postBody 'const bool closeCleanupPending = state.value("hcCloseCleanupPending", false);' 'pending HC cleanup is visible to native safety gate'
Require-Contains $postBody 'const bool openCalled = state.value("openCalled", false);' 'HC Open session state is visible to native safety gate'
Require-Contains $postBody 'const bool openEventsCalled = state.value("openEventsCalled", false);' 'HC OpenEvents session state is visible to native safety gate'
Require-Contains $postBody 'const bool unknownState = state.value("unknownState", false);' 'unknown Host state is visible to native safety gate'
Require-Contains $postBody 'const bool suspendBoundary = _wcsicmp(endpoint, L"/api/suspend") == 0;' 'native safety gate distinguishes HC SystemPending from Window_Closed'
Require-Contains $postBody 'const bool hcTerminalBoundary = suspendBoundary' 'native safety gate accepts only the matching HC terminal lifecycle boundary'
Require-Contains $postBody 'const bool releaseEvidence = !historicalWrites || restoreConfirmed || hcTerminalBoundary;' 'historical writes require either HC Hardware release or a complete HC terminal boundary'
Require-Contains $postBody 'const bool expectedTerminalState = suspendBoundary' 'native safety gate requires the endpoint-specific terminal state'
Require-Contains $postBody 'const bool telemetryComplete =' 'native safety gate requires complete telemetry'
Require-Contains $postBody 'const bool hasAnyHcCloseEvidence =' 'native safety gate detects partial protocol-2 HC close evidence'
Require-Contains $postBody 'const bool hasCompleteHcCloseEvidence =' 'native safety gate requires both HC close evidence fields'
Require-Contains $postBody 'const bool hcCloseEvidenceSafe =' 'native safety gate rejects explicit incomplete HC close evidence'
Require-Contains $postBody 'safe = telemetryComplete && expectedTerminalState &&' 'native safety gate rejects incomplete/unknown/pending/live-write or wrong terminal state'
Require-Contains $postBody '!openCalled &&' 'native safety gate rejects an unreleased HC session'
Require-Contains $postBody '!openEventsCalled && releaseEvidence' 'native safety gate requires release evidence after HC session close'
Require-Contains $native 'static bool fanHostCleanupForAppExit()' 'native exit cleanup routine'
if ($native -notmatch 'fanHostEmergencyPost\s*\(\s*L"/api/parent-exit",\s*"app-exit",\s*1,\s*false,\s*&handoffStatus\s*\)') {
    throw 'fan exit cleanup source check failed: parent-exit recovery handoff before UI exit'
}
Require-Contains $native 'if (handoffStatus != 404 && handoffStatus != 405)' 'transport-ambiguous parent-exit never races a second close'
Require-Contains $native 'fanHostEmergencyPost(L"/api/close", "app-exit", 1)' 'legacy bounded close only when parent-exit is absent'
Require-Contains $native 'fanHostEmergencyPost(L"/api/shutdown", "app-exit", 1, false)' 'shutdown only after close'
Require-Contains $exitBody 'fan-exit-recovery-handed-off' 'unconfirmed recovery is handed to resident Host'
Require-Contains $native 'static std::atomic<bool> g_exitReadyPosted{false};' 'single final-exit message guard'
Require-Contains $native 'static void postExitReadyOnce(HWND hwnd, WPARAM code = 0)' 'idempotent final-exit notifier'
Require-Contains $native '#define EXIT_CLEANUP_WATCHDOG_MS 8000' 'global exit cleanup deadline'
Require-Contains $native 'SetTimer(hwnd, EXIT_CLEANUP_WATCHDOG_TIMER_ID,' 'exit cleanup watchdog is armed'
Require-Contains $native 'appendNativeLifecycleLog("exit-cleanup-deadline"' 'exit deadline is diagnosable'
Require-Contains $native 'static bool joinThreadBoundedForExit' 'bounded native worker join helper'
Require-Contains $exitBody 'sgStopWorkThread(1000);' 'sleep worker exit wait is bounded'
Require-Contains $exitBody 'poolStop(1000);' 'IPC worker exit wait is bounded'
Require-Contains $exitBody 'stopTopMonitorForExit(1000);' 'monitor exit wait is bounded'
Require-Contains $native 'static bool sgCleanupBeforeExit(bool nonBlocking)' 'sleep exit cleanup has an explicit non-blocking mode'
Require-Contains $native 'if (!opLock.try_lock()) {' 'sleep cleanup does not wait behind an active sleep worker'
Require-Contains $native '"sleep-exit-cleanup-deferred"' 'deferred sleep recovery is diagnosable'
Require-Contains $exitBody 'sgCleanupBeforeExit(true)' 'exit uses non-blocking sleep cleanup'
Require-Contains $readyBody 'gamepadSerialStop(500);' 'UI-thread serial worker wait is bounded'
Require-Contains $readyBody 'WaitForSingleObject(g_exitCleanupThread, 0);' 'UI never waits for cleanup worker'
if ($exitBody.Contains('.join()')) {
    throw 'fan exit cleanup source check failed: exit worker still performs an unbounded thread join'
}
if ($native.Contains('WM_APP_EXIT_ABORTED') -or $native.Contains('fan-exit-cleanup-blocked')) {
    throw 'fan exit cleanup source check failed: main application still has an OEM-restore exit abort path'
}

function Test-NativeSafeState {
    param(
        [bool]$CanonicalFieldPresent,
        [bool]$CanonicalLiveWrites,
        [bool]$LegacyLiveWrites,
        [bool]$HistoricalWrites,
        [bool]$RestoreConfirmed,
        [bool]$UnknownState,
        [bool]$CloseCleanupPending,
        [string]$State = 'Suspended',
        [bool]$OpenCalled = $false,
        [bool]$OpenEventsCalled = $false,
        [bool]$TelemetryComplete = $true,
        [Nullable[bool]]$HcVirtualCloseReturned = $null,
        [Nullable[bool]]$HcDeviceManagerStopCompleted = $null,
        [bool]$SuspendBoundary = $false
    )
    # Mirrors the native JSON contract, including the legacy alias fallback.
    $live = if ($CanonicalFieldPresent) { $CanonicalLiveWrites } else { $LegacyLiveWrites }
    $hasHcVirtualCloseEvidence = $null -ne $HcVirtualCloseReturned
    $hasHcDeviceManagerStopEvidence = $null -ne $HcDeviceManagerStopCompleted
    $hasAnyHcCloseEvidence = $hasHcVirtualCloseEvidence -or $hasHcDeviceManagerStopEvidence
    $hasCompleteHcCloseEvidence = $hasHcVirtualCloseEvidence -and $hasHcDeviceManagerStopEvidence
    $hcTerminalBoundary = if ($SuspendBoundary) {
        $State -eq 'Suspended' -and $HcVirtualCloseReturned
    } else {
        $State -eq 'Stopped' -and $HcVirtualCloseReturned -and $HcDeviceManagerStopCompleted
    }
    $releaseEvidence = (-not $HistoricalWrites) -or $RestoreConfirmed -or $hcTerminalBoundary
    $hcCloseEvidenceSafe = -not $hasAnyHcCloseEvidence -or ($HcVirtualCloseReturned -and ($SuspendBoundary -or $HcDeviceManagerStopCompleted))
    $expectedTerminalState = if ($SuspendBoundary) { $State -eq 'Suspended' } else { $State -eq 'Stopped' }
    $requiredHcEvidencePresent = if ($SuspendBoundary) { $hasHcVirtualCloseEvidence } else { $hasCompleteHcCloseEvidence }
    return $TelemetryComplete -and $expectedTerminalState -and (-not $hasAnyHcCloseEvidence -or $requiredHcEvidencePresent) -and $hcCloseEvidenceSafe -and (-not $UnknownState) -and (-not $CloseCleanupPending) -and (-not $live) -and
        (-not $OpenCalled) -and (-not $OpenEventsCalled) -and $releaseEvidence
}
$safeStateScenarios = @(
    @{ name = 'canonical-live-write-rejected'; value = -not (Test-NativeSafeState $true $true $false $true $true $false $false) },
    @{ name = 'legacy-live-write-alias-rejected'; value = -not (Test-NativeSafeState $false $false $true $true $true $false $false) },
    @{ name = 'suspended-label-cannot-hide-unconfirmed-history'; value = -not (Test-NativeSafeState $true $false $false $true $false $false $false 'Suspended') },
    @{ name = 'open-session-cannot-hide-unreleased-device'; value = -not (Test-NativeSafeState $true $false $false $false $false $false $false 'Suspended' $true $true) },
    @{ name = 'confirmed-hardware-history-accepted'; value = Test-NativeSafeState $true $false $false $true $true $false $false 'Stopped' },
    @{ name = 'complete-hc-window-close-without-profile-callback-accepted'; value = Test-NativeSafeState $true $false $false $true $false $false $false 'Stopped' $false $false $true -HcVirtualCloseReturned $true -HcDeviceManagerStopCompleted $true },
    @{ name = 'complete-hc-system-pending-without-profile-callback-accepted'; value = Test-NativeSafeState $true $false $false $true $false $false $false 'Suspended' $false $false $true -HcVirtualCloseReturned $true -HcDeviceManagerStopCompleted $false -SuspendBoundary $true },
    @{ name = 'explicit-hc-close-incomplete-rejected'; value = -not (Test-NativeSafeState $true $false $false $true $true $false $false 'Stopped' $false $false $true -HcVirtualCloseReturned $true -HcDeviceManagerStopCompleted $false) },
    @{ name = 'explicit-hc-close-complete-accepted'; value = Test-NativeSafeState $true $false $false $true $true $false $false 'Stopped' $false $false $true -HcVirtualCloseReturned $true -HcDeviceManagerStopCompleted $true },
    @{ name = 'partial-hc-close-evidence-rejected'; value = -not (Test-NativeSafeState $true $false $false $true $true $false $false 'Stopped' $false $false $true -HcVirtualCloseReturned $true) },
    @{ name = 'incomplete-200-state-rejected'; value = -not (Test-NativeSafeState $false $false $false $false $false $false $false 'Stopped' $false $false $false) }
)
$failedSafeState = @($safeStateScenarios | Where-Object { -not $_.value })
if ($failedSafeState.Count -gt 0) { throw "native safety telemetry simulation failed: $($failedSafeState.name -join ', ')" }
Write-Output ('native safety telemetry simulation: PASS (' + ($safeStateScenarios.name -join ', ') + ')')
Require-Contains $hostSource 'if (!IsAuthorizedRequest(context.Request))' 'Host rejects unauthenticated requests'
Require-Contains $hostSource 'private void BlockWritesForClose()' 'Host has a lock-free close write gate'
Require-Contains $hostSource 'Volatile.Write(ref closeWriteBlocked, 1);' 'close gate blocks new writes before engine lock'
Require-Contains $hostSource 'throw new FanApiException(409, "HOST_CLOSING"' 'close gate rejects later control admission'
Require-Contains $hostSource 'public object BeginParentExitHandoff()' 'Host has an authenticated parent-exit recovery handoff'
Require-Contains $hostSource 'Interlocked.Exchange(ref parentExitHandoffQueued, 1)' 'parent-exit handoff is one-shot'
Require-Contains $hostSource 'parent-exit-handoff.existing-close-observed' 'parent-exit handoff observes an already-owned HC Close instead of spawning a second recovery worker'
Require-Contains $hostSource 'state.CloseCalled || state.HcCloseCleanupPending || realBackend?.OperationTimedOut == true)' 'parent-exit handoff rejects duplicate ownership during an in-flight HC Close'
Require-Contains $hostSource 'private int closeBoundaryClaimed;' 'close ownership is claimed before api-close can acquire the engine lock'
Require-Contains $hostSource 'Interlocked.Exchange(ref closeBoundaryClaimed, 1);' 'api-close claims the HC lifecycle boundary before parent-exit can race it'
Require-Contains $hostSource 'Volatile.Read(ref closeBoundaryClaimed) != 0 ||' 'parent-exit observes the pre-lock close owner and cannot enqueue a second HC Close'
Require-Contains $hostSource 'if (Volatile.Read(ref parentExitHandoffQueued) != 0)' 'parent-exit worker remains the sole HC Close owner'
Require-Contains $hostSource 'api.close.parent-exit-observed' 'late close requests observe parent-exit recovery instead of re-entering HC Close'
Require-Contains $hostSource 'state.CloseCalled = true;' 'parent-exit recovery records the active HC Close boundary before restore'
Require-Contains $hostSource '("POST", "/api/parent-exit") => BeginParentExitFromApi()' 'Host exposes parent-exit handoff endpoint'

function Simulate-Exit([bool]$exactHost, [bool]$validToken, [bool]$handoffAccepted, [bool]$closeConfirmed, [bool]$shutdownResponds, [bool]$workerCompleted, [bool]$watchdogFired) {
    if (-not $workerCompleted -and -not $watchdogFired) {
        return [pscustomobject]@{ allowExit = $false; recoveryOwner = 'pending'; shutdownAttempted = $false }
    }
    if (-not $exactHost) { return [pscustomobject]@{ allowExit = $true; recoveryOwner = 'none'; shutdownAttempted = $false } }
    # A missing token or a wedged loopback route cannot become a UI deadlock.
    # The Host was launched with a parent watchdog, so parent termination is
    # still an independent recovery trigger.
    if (-not $validToken) { return [pscustomobject]@{ allowExit = $true; recoveryOwner = 'parent-watchdog'; shutdownAttempted = $false } }
    if (-not $closeConfirmed) {
        return [pscustomobject]@{ allowExit = $true; recoveryOwner = $(if ($handoffAccepted) { 'resident-host' } else { 'parent-watchdog' }); shutdownAttempted = $false }
    }
    # A confirmed close restores OEM first. A later shutdown timeout leaves the
    # Host's parent watchdog as a process-lifetime fallback, not a reason to
    # force-kill the Host or retain the UI process.
    return [pscustomobject]@{ allowExit = $true; recoveryOwner = 'confirmed'; shutdownAttempted = $true; shutdownResponds = $shutdownResponds }
}

$scenarios = @(
    @{ name = 'no-host-allows-normal-exit'; value = (Simulate-Exit $false $false $false $false $false $true $false).allowExit -eq $true },
    @{ name = 'authenticated-close-allows-exit'; value = (Simulate-Exit $true $true $true $true $true $true $false).allowExit -eq $true },
    @{ name = 'unconfirmed-restore-handoff-allows-exit'; value = ((Simulate-Exit $true $true $true $false $false $true $false).allowExit -eq $true -and (Simulate-Exit $true $true $true $false $false $true $false).recoveryOwner -eq 'resident-host') },
    @{ name = 'handoff-timeout-falls-back-to-parent-watchdog'; value = ((Simulate-Exit $true $true $false $false $false $true $false).allowExit -eq $true -and (Simulate-Exit $true $true $false $false $false $true $false).recoveryOwner -eq 'parent-watchdog') },
    @{ name = 'missing-session-does-not-block-exit'; value = ((Simulate-Exit $true $false $false $false $false $true $false).allowExit -eq $true -and (Simulate-Exit $true $false $false $false $false $true $false).recoveryOwner -eq 'parent-watchdog') },
    @{ name = 'shutdown-timeout-after-safe-close-does-not-revoke-safety'; value = (Simulate-Exit $true $true $true $true $false $true $false).allowExit -eq $true },
    @{ name = 'blocked-nonfan-worker-is-released-by-exit-watchdog'; value = (Simulate-Exit $true $true $true $false $false $false $true).allowExit -eq $true },
    @{ name = 'busy-sleep-worker-defers-marker-recovery-without-blocking-exit'; value = (Simulate-Exit $true $true $true $false $false $false $true).recoveryOwner -eq 'resident-host' },
    @{ name = 'incomplete-worker-before-deadline-does-not-prematurely-destroy'; value = (Simulate-Exit $true $true $true $false $false $false $false).allowExit -eq $false }
)
$failed = @($scenarios | Where-Object { -not $_.value })
if ($failed.Count -gt 0) { throw "fan exit cleanup simulation failed: $($failed.name -join ', ')" }
Write-Output ('fan exit cleanup selftest: PASS (' + ($scenarios.name -join ', ') + '; hardwareWrites=false)')
