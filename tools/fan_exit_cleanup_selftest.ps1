$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$native = Get-Content -LiteralPath (Join-Path $repoRoot 'native\main.cpp') -Raw
$workspaceRoot = $repoRoot
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
# FAN-926 (P0/P3): W3's GP-926 post-image extracted the native boundary safety
# gate into one evidence builder plus one predicate. Re-anchor these source-shape
# guards to that post-image by MEANING; never rename a production symbol to fit
# an outdated text guard (GP-926 ship rule).
$boundaryStart = $native.IndexOf('static FanBoundaryEvidence fanBoundaryEvidenceFromState')
$boundaryEnd = $native.IndexOf('static json fanBoundaryEvidenceDetail', $boundaryStart)
if ($boundaryStart -lt 0 -or $boundaryEnd -le $boundaryStart) {
    throw 'fan exit cleanup source check failed: native boundary evidence body not found'
}
$boundaryBody = $native.Substring($boundaryStart, $boundaryEnd - $boundaryStart)
$exitStart = $native.IndexOf('static DWORD WINAPI exitCleanupThreadProc')
$exitEnd = $native.IndexOf('static void beginAsyncExit', $exitStart)
$exitBody = if ($exitStart -ge 0 -and $exitEnd -gt $exitStart) { $native.Substring($exitStart, $exitEnd - $exitStart) } else { '' }
$readyStart = $native.IndexOf('case WM_APP_EXIT_READY:')
$readyEnd = $native.IndexOf('case WM_POWER_RESUME_READY:', $readyStart)
$readyBody = if ($readyStart -ge 0 -and $readyEnd -gt $readyStart) { $native.Substring($readyStart, $readyEnd - $readyStart) } else { '' }
Require-Contains $native 'static bool fanHostExactProcessRunning()' 'exact Host identity check'
Require-Contains $native 'static std::wstring processImagePath(DWORD pid)' 'Host image identity probe'
Require-Contains $native 'static bool fanHostOwnsLoopbackPort' 'Host loopback ownership check'
Require-Contains $native 'static constexpr DWORD kHttpSysListenerOwnerPid = 4' 'HttpListener owner (HTTP.sys pid 4) is a first-class identity'
Require-Contains $native 'row.dwOwningPid == pid || row.dwOwningPid == kHttpSysListenerOwnerPid' 'port ownership accepts the Host pid or the HTTP.sys owner'
Require-Contains $native 'GetExtendedTcpTable' 'OS TCP listener ownership proof'
Require-Contains $native 'fanHostOwnsLoopbackPort(entry.th32ProcessID)' 'exact Host check consults loopback ownership'
Require-Contains $native 'fan-host-identity-unverified' 'identity failure is logged, never a silent false'
Require-Contains $native 'FAN_LEASE_RENDERER_GRACE_MS' 'renderer rebuild grace (90s) constant exists'
Require-Contains $native 'renderer-epoch-grace-expired' 'keepalive disarms only after the renderer epoch grace expires'
Require-Contains $native 'fanLeaseKeepaliveNoteRendererRebuild' 'renderer rebuild bumps the keepalive epoch'
Require-Contains $native 'fanHostEmergencyResume' 'native triggers the Host resume rebuild itself'
Require-Contains $native 'L"/api/resume"' 'native resume endpoint (fast accept + bounded observation)'
Require-Contains $native 'g_fanResumeRecoveryInFlight' 'resume recovery is serialized (single in-flight sequence per generation/session)'
# 2026-09-23（FAN-204 s31 补充裁决 Q1）：接受与后台重建分离——接受只做有界传输重试，
# 完成由同一 generation 的有界只读观察判定（≤15 s；只有 Ready 才算 recovered）。
Require-Contains $native 'const DWORD acceptBackoffMs[3] = {0, 250, 500};' 'bounded accept backoff 0/250/500 ms (accept never waits for the rebuild)'
Require-Contains $native 'for (int attempt = 1; attempt <= 3; ++attempt)' 'at most three bounded accept attempts'
Require-Contains $native 'static bool fanHostEmergencyGetState' 'observation reads state through its own GET helper'
Require-Contains $native 'static constexpr unsigned long long kFanResumeObserveWindowMs = 15000;' 'observation window is the ruled 15 s'
Require-Contains $native '"resume-recovery-exhausted"' 'exhaustion is written as its own terminal event'
Require-Contains $native 'fanResumeParseReply' 'resume response is parsed: a bare HTTP 200 is never a recovery'
Require-Contains $native 'fanResumeClassify' 'terminal classification is a single explicit function'
Require-Contains $native 'finish("noop", "host-explicit-noop", false)' 'legal resume no-op is recorded separately'
Require-Contains $native 'finish("recovered", "observed-ready", false)' 'only a verified Ready recovery is written as recovered'
Require-Contains $native '"terminal", terminal' 'the terminal event always carries the single classified verdict'
Require-Contains $native 'host-rebuild-timeout' 'the 15 s timeout writes the ruled honest terminal reason'
Require-Contains $native 'generation-taken-over' 'a late response from a superseded generation is never consumed'
# 2026-09-23（裁决 B）：恢复序列**不得**并发发送第二个 Close/Restore（函数体内只允许 /api/resume）。
$resumeStart = $native.IndexOf('static void fanHostEmergencyResume(unsigned long long generation) {')
$resumeEnd = $native.IndexOf('static void fanHostScheduleEmergencyResume', $resumeStart)
if ($resumeStart -lt 0 -or $resumeEnd -le $resumeStart) { throw 'fan exit cleanup source check failed: resume recovery body not found' }
$resumeBody = $native.Substring($resumeStart, $resumeEnd - $resumeStart)
Require-Contains $resumeBody 'L"/api/resume"' 'resume recovery sends the resume endpoint'
Require-Contains $resumeBody 'fanHostEmergencyGetState' 'observation phase reads state (no second lifecycle owner)'
foreach ($forbidden in @('/api/close', '/api/restore', '/api/shutdown')) {
    if ($resumeBody.Contains($forbidden)) {
        throw "fan exit cleanup source check failed: resume recovery must not send $forbidden (no concurrent close/restore)"
    }
}
if ($resumeBody.Contains('L"/api/open"') -or $resumeBody.Contains('L"/api/enable"')) {
    throw 'fan exit cleanup source check failed: resume recovery must not drive Open/Enable itself (the Host owns the background rebuild)'
}
Require-Contains $native 'static std::string fanHostReadSessionToken()' 'session sidecar reader'
Require-Contains $postBody 'X-YeMan-Fan-Session:' 'authenticated native Host request'
Require-Contains $postBody 'if (!fanHostExactProcessRunning()) {' 'identity gate before token use'
Require-Contains $postBody 'fanHostIdentityNoteUnverified(reason ? reason : "emergency-post");' 'identity gate failure leaves evidence'
Require-Contains $boundaryBody 'const bool hasCanonicalLiveWrites = state.contains("hardwareWritesEnabled");' 'canonical live-write telemetry field'
Require-Contains $boundaryBody 'const bool hasLegacyLiveWrites = state.contains("hardwareWrites");' 'legacy live-write alias is discriminated, not merged'
Require-Contains $boundaryBody 'evidence.telemetryComplete =' 'native safety gate requires complete telemetry'
Require-Contains $boundaryBody 'state.contains("state") &&' 'telemetry completeness requires the state label itself'
Require-Contains $boundaryBody '(hasCanonicalLiveWrites || hasLegacyLiveWrites) &&' 'telemetry completeness accepts canonical-or-legacy live-write evidence'
Require-Contains $boundaryBody 'evidence.writesHistory = state.value("hardwareWritesObserved", false);' 'write history is visible to the native safety gate'
Require-Contains $boundaryBody 'evidence.releaseReceipt = state.value("oemRestoreConfirmed", false);' 'the software release receipt is visible to the native safety gate'
Require-Contains $boundaryBody 'evidence.unknownState = state.value("unknownState", false);' 'unknown Host state is visible to the native safety gate'
Require-Contains $boundaryBody 'evidence.cleanupPending = state.value("hcCloseCleanupPending", false);' 'pending HC cleanup is visible to the native safety gate'
Require-Contains $boundaryBody 'evidence.sessionOpen = state.value("openCalled", false) ||' 'HC Open session state is visible to the native safety gate'
Require-Contains $boundaryBody 'state.value("openEventsCalled", false);' 'HC OpenEvents session state is visible to the native safety gate'
Require-Contains $boundaryBody 'evidence.liveWrites = hasCanonicalLiveWrites' 'the canonical live-write field wins when both are present'
Require-Contains $boundaryBody ': state.value("hardwareWrites", false);' 'the legacy live-write alias is only a fallback'
Require-Contains $postBody 'const bool suspendBoundary = _wcsicmp(endpoint, L"/api/suspend") == 0;' 'native safety gate distinguishes HC SystemPending from Window_Closed'
Require-Contains $boundaryBody 'evidence.expectedTerminalState = suspendBoundary' 'native safety gate requires the endpoint-specific terminal state'
Require-Contains $boundaryBody 'evidence.hcCleanupTerminal = suspendBoundary' 'native safety gate accepts only the matching HC terminal lifecycle boundary'
Require-Contains $boundaryBody 'const bool hasAnyHcCloseEvidence =' 'native safety gate detects partial protocol-2 HC close evidence'
Require-Contains $boundaryBody 'evidence.hcCloseEvidenceComplete = hasHcVirtualCloseEvidence && hasHcDeviceManagerStopEvidence;' 'native safety gate requires both HC close evidence fields'
Require-Contains $boundaryBody 'evidence.hcCloseEvidenceAcceptable = !hasAnyHcCloseEvidence ||' 'native safety gate rejects explicit incomplete HC close evidence'
Require-Contains $boundaryBody 'evidence.releaseEvidence = !evidence.writesHistory ||' 'historical writes require a proven release action'
Require-Contains $boundaryBody 'evidence.hostReleaseActionProven = fanReleaseActionKindProven(evidence.hostReleaseActionKind);' 'the Host release-action classification is the single release authority'
Require-Contains $native 'static bool fanBoundaryIsSafe(const FanBoundaryEvidence& evidence) {' 'native safety predicate has one decision entry'
Require-Contains $boundaryBody 'if (!evidence.telemetryComplete || !evidence.expectedTerminalState) return false;' 'native safety gate rejects incomplete telemetry or a wrong terminal state'
Require-Contains $boundaryBody 'if (!evidence.hcCloseEvidenceAcceptable) return false;' 'native safety gate rejects unacceptable HC close evidence'
Require-Contains $boundaryBody 'if (evidence.unknownState || evidence.cleanupPending) return false;' 'native safety gate rejects unknown or pending state'
Require-Contains $boundaryBody 'if (evidence.liveWrites || evidence.sessionOpen) return false;' 'native safety gate rejects live writes and an unreleased HC session'
Require-Contains $boundaryBody 'if (!evidence.releaseEvidence) return false;' 'cleanup or a Close return can never substitute for release evidence'
Require-Contains $native 'safe = fanBoundaryIsSafe(boundary);' 'the native safety assessment consumes the single decision entry'
Require-Contains $native 'static FanExitHandoffResult fanHostCleanupForAppExit()' 'native exit cleanup routine'
if ($native -notmatch 'fanHostEmergencyPost\s*\(\s*L"/api/parent-exit",\s*"app-exit",\s*1,\s*false,\s*&handoffStatus') {
    throw 'fan exit cleanup source check failed: parent-exit recovery handoff before UI exit'
}
Require-Contains $native 'if (handoffStatus != 404 && handoffStatus != 405)' 'transport-ambiguous parent-exit never races a second close'
if ($native -notmatch 'fanHostEmergencyPost\s*\(\s*L"/api/close",\s*"app-exit",\s*1,\s*true') {
    throw 'fan exit cleanup source check failed: legacy bounded close only when parent-exit is absent'
}
Require-Contains $native 'fanHostEmergencyPost(L"/api/shutdown", "app-exit", 1, false,' 'shutdown only after close'
Require-Contains $exitBody 'fanHostCleanupForAppExit()' 'exit cleanup delegates to the app-exit handoff owner'
Require-Contains $native 'fan-exit-recovery-handed-off' 'unconfirmed recovery is handed to resident Host'
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
# FAN-925B §5.1: the auth check must classify the rejection (and must not gain a bypass path).
Require-Contains $hostSource 'private bool IsAuthorizedRequest(HttpListenerRequest request, out string rejectionCategory)' 'Host rejects unauthenticated requests'
Require-Contains $hostSource 'rejectionCategory = "request-credential-missing";' 'auth rejection is classified, not silently false'
Require-Contains $hostSource 'scope = "this-request-not-accepted; no bypass, no rotation, no second owner",' 'a 401 never authorises a bypass/rotation/second owner'
Require-Contains $hostSource 'private void BlockWritesForClose()' 'Host has a lock-free close write gate'
Require-Contains $hostSource 'Volatile.Write(ref closeWriteBlocked, 1);' 'close gate blocks new writes before engine lock'
Require-Contains $hostSource 'throw new FanApiException(409, "HOST_CLOSING"' 'close gate rejects later control admission'
Require-Contains $hostSource 'public object BeginParentExitHandoff()' 'Host has an authenticated parent-exit recovery handoff'
# FAN-926 P1 (CP-07): the acknowledgement may only report a REALISED acceptance
# (this-host-instance + real recovery cycle), never a bare field fill.
Require-Contains $hostSource 'private ParentExitAcceptance? EnsureParentExitAcceptance()' 'parent-exit binds a real acceptance before the ACK'
Require-Contains $hostSource 'cycleOk = EnsureRecoveryCycleLocked("parent-exit-handoff");' 'the acceptance establishes or reuses the real recovery cycle'
Require-Contains $hostSource 'if (acceptanceFastPathActive)' 'cycle persistence defers while the acceptance fast path is active'
Require-Contains $hostSource 'acceptanceFastPathActive = true;' 'the acceptance fast path is marked so the ACK never touches disk'
Require-Contains $hostSource 'internal static bool UiFreeRecoveryComplete(' 'UI-free recovery completion has a single decision entry'
# D6（FAN-926R 重钉/恢复/导出裁决 §3）**因本裁决改要求**：后继等待不再使用固定 15 s，
# 而是与前置停止、接管恢复**共用同一周期剩余预算**（`successorWaitMs` 由继承的剩余预算算出）。
# 断言意图不变：受控更换的后继必须**在有界等待里拿到前驱退出证明**，不得无界等、不得并行开窗。
Require-Contains $hostSource 'HostInstanceLease.TryAcquireBounded(successorWaitMs)' 'a controlled replacement successor must wait for the predecessor exit proof (bounded, shared cycle budget)'
Require-Contains $hostSource 'lock (gate) { lock (acceptanceGate) { PersistRecoveryCycleSynchronously(); } }' 'deferred cycle persistence re-enters the engine gate before writing'
Require-Contains $hostSource 'acceptedRecoveryCycleId = acceptance.CycleId,' 'the ACK reports the accepted cycle instead of a freshly minted field'
Require-Contains $hostSource 'recoveryAccepted = false,' 'an unbound responsibility is never reported as accepted'
Require-Contains $hostSource 'lock (acceptanceGate) return EnsureRecoveryCycleCoreLocked(reason);' 'the acceptance lock is a short critical section, never the HC/ACPI gate'
Require-Contains $hostSource 'internal static bool SameEpisodeParentIdentity(' 'the same-episode parent rule is a pure, directly testable function'
Require-Contains $hostSource 'var staged = path + "." + Guid.NewGuid().ToString("N") + ".new";' 'cycle persistence uses a unique staged name (the acceptance path never takes the engine gate)'
# FAN-925B §3.3/§3.4: the two release-action decisions must be consumed through their single
# decision entry, and the old shortcuts must not reappear anywhere in the Host source.
Require-Contains $hostSource 'var releaseSafe = RouteLostCloseIsRelease(' 'route-lost close boundary is consumed via the single decision entry'
Require-Contains $hostSource 'var fallbackProven = FanHostEngine.AcpiFallbackProven(' 'ACPI fallback proof is consumed via the single decision entry'
foreach ($forbidden in @('skipOemRestore && hcCloseComplete', 'closeBoundaryOnly', '? "TransportedComplete"')) {
    if ($hostSource.Contains($forbidden)) {
        throw "FAN-925B regression guard: forbidden release/cleanup shortcut reappeared in Host source: '$forbidden'"
    }
}
Require-Contains $hostSource 'Interlocked.Exchange(ref parentExitHandoffQueued, 1)' 'parent-exit handoff is one-shot'
Require-Contains $hostSource 'parent-exit-handoff.existing-close-observed' 'parent-exit handoff observes an already-owned HC Close instead of spawning a second recovery worker'
Require-Contains $hostSource 'state.CloseCalled || state.HcCloseCleanupPending || realBackend?.OperationTimedOut == true;' 'parent-exit handoff rejects duplicate ownership during an in-flight HC Close'
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
