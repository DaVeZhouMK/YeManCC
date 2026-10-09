$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$native = Get-Content -Raw (Join-Path $repoRoot 'native\main.cpp')
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $repoRoot)
$hostCandidates = @(
  (Join-Path $workspaceRoot 'FanLab\real-host\Program.cs'),
  'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\FanLab\real-host\Program.cs',
  'G:\YeManCC-Work\Isolated\Tasks\HC-Candidate-20260902\assets\candidates\FanHost-source-FANHOST-REAL-HOST-20260831\Program.cs'
)
$hostSourcePath = $hostCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $hostSourcePath) {
  throw "FanHost source baseline not found; checked: $($hostCandidates -join '; ')"
}
$hostSource = Get-Content -Raw -LiteralPath $hostSourcePath
$appSource = Get-Content -Raw (Join-Path $repoRoot 'src\App.vue')
$f4Start = $hostSource.IndexOf('private object SuspendUnlocked()')
$f4End = if ($f4Start -ge 0) { $hostSource.IndexOf('private ResumeAdmission StartAutomaticResumeUnlocked(', $f4Start) } else { -1 }
$f4Body = if ($f4Start -ge 0 -and $f4End -gt $f4Start) { $hostSource.Substring($f4Start, $f4End - $f4Start) } else { '' }

$queryStart = $native.IndexOf('else if (w == PBT_APMQUERYSUSPEND)')
$suspendStart = $native.IndexOf('else if (w == PBT_APMSUSPEND)', $queryStart)
$queryBody = if ($queryStart -ge 0 -and $suspendStart -gt $queryStart) {
  $native.Substring($queryStart, $suspendStart - $queryStart)
} else { '' }
$suspendEnd = $native.IndexOf('else if (w == PBT_APMQUERYSUSPENDFAILED', $suspendStart)
$suspendBody = if ($suspendStart -ge 0 -and $suspendEnd -gt $suspendStart) {
  $native.Substring($suspendStart, $suspendEnd - $suspendStart)
} else { '' }
$hostCallbackStart = $hostSource.IndexOf('private void OnPowerModeChanged')
$hostQueueStart = if ($hostCallbackStart -ge 0) { $hostSource.IndexOf('private object SuspendOnPowerWorker', $hostCallbackStart) } else { -1 }
$hostCallbackBody = if ($hostCallbackStart -ge 0 -and $hostQueueStart -gt $hostCallbackStart) {
  $hostSource.Substring($hostCallbackStart, $hostQueueStart - $hostCallbackStart)
} else { '' }

$checks = [ordered]@{
  nativeBoundedHostOperation = $native.Contains('static bool fanHostEmergencyPost')
  nativeAsyncScheduler = $native.Contains('static void fanHostScheduleEmergencySuspend') -and
    $native.Contains('std::thread([reasonText = std::string(reason ? reason : "unknown"), generation]') -and
    $native.Contains('}).detach();')
  nativeQueryBoundaryNeverCallsHost = -not ($queryBody.Contains('fanHostEmergencyPost') -or
    $queryBody.Contains('fanHostEmergencySuspend') -or
    $queryBody.Contains('fanHostScheduleEmergencySuspend'))
  nativeSuspendCallbackHasNoBlockingWait = -not ($suspendBody.Contains('fanHostEmergencyPost') -or
    $suspendBody.Contains('WinHttp') -or $suspendBody.Contains('Sleep(') -or
    $suspendBody.Contains('WaitFor'))
  nativeSuspendBoundaryQueuesOnly = $native.Contains('fanHostScheduleEmergencySuspend("suspend-broadcast", generation)')
  nativeConfirmedQueryBoundaryQueuesOnly = $native.Contains('fanHostScheduleEmergencySuspend("suspend-confirmed", currentPowerGeneration())')
  nativeKernelPowerBoundaryQueuesOnly = $native.Contains('fanHostScheduleEmergencySuspend("kernel-power-506", generation)')
  safeCloseFallback = $native.Contains('fanHostEmergencyPost(L"/api/close", reason, 1)') -and
    $native.Contains('if (suspendStatus != 404 && suspendStatus != 405)') -and
    $native.Contains('no concurrent close fallback')
  # Older Host revisions subscribed directly to SystemEvents. The current
  # source receives the native tagged boundary through PowerIngress and the
  # serialized Channel worker; accept either explicitly, never a path guessed
  # from a different migration copy.
  hostSystemPowerObserver = $hostSource.Contains('SystemEvents.PowerModeChanged += OnPowerModeChanged') -or
    ($hostSource.Contains('PowerIngressTarget') -and $hostSource.Contains('ProcessPowerTransitionsAsync'))
  hostPowerCallbackQueues = $hostSource.Contains('QueuePowerTransition(') -and
    $hostSource.Contains('PowerIngressTarget.Suspended') -and
    $hostSource.Contains('SuspendOnPowerWorker') -and
    $hostSource.Contains('engine.ResumeForSystemPower()')
  hostPowerCallbackHasNoWait = (($hostCallbackBody.Contains('QueuePowerTransition') -and
    -not ($hostCallbackBody.Contains('.Wait(') -or $hostCallbackBody.Contains('.Result') -or
      $hostCallbackBody.Contains('GetAwaiter().GetResult') -or $hostCallbackBody.Contains('Thread.Sleep') -or
      $hostCallbackBody.Contains('lock (') -or $hostCallbackBody.Contains('diagnostics.Write'))) -or
    ($hostCallbackStart -lt 0 -and $hostSource.Contains('QueuePowerTransition(') -and
      $hostSource.Contains('Channel.CreateUnbounded<PowerTransition>') -and
      $hostSource.Contains('ProcessPowerTransitionsAsync')))
  hostSuspendGateIsLockFree = $hostSource.Contains('Volatile.Write(ref systemSuspendPending, 1)') -and
    $hostSource.Contains('realBackend?.BlockWritesForSuspend()')
  hostPowerQueueRunsAsync = $hostSource.Contains('Channel.CreateUnbounded<PowerTransition>') -and
    $hostSource.Contains('powerTransitions.Writer.TryWrite') -and
    $hostSource.Contains('ProcessPowerTransitionsAsync')
  f4NativeIsSingleProductionSender = $native.Contains('generation, "native.power"') -and
    -not $appSource.Contains('fanHostLifecycle.suspend()') -and
    $hostSource.Contains('RequireNativeSuspendAuthority(body)') -and
    $hostSource.Contains('F4_NATIVE_AUTHORITY_REQUIRED')
  # ★ 因 FAN-926R 裁决 §3-D1 **改要求**（明示取代，不是"仅更新符号/证明行为未变"）：
  #   旧要求（原检查名 `hostSystemPendingIsStrictHcCloseFirst`）= SystemPending 一律
  #   `skipOemRestore: true` + `clearOemEvidence: true`，即"一律 Close-first、禁止默认表预写"。
  #   新要求 = 有写历史且会话可操作时，由**唯一生命周期 owner** 在进入 HC Close **之前**
  #   有界执行一次现成默认模式恢复，再 Close；会话不可操作时才 not-attempted；证据保留、三轴分列。
  #   仍然成立：关闭边界必须进入（`realBackend.Close(stopDeviceManager);`），且不得新开一条
  #   绕过单一 owner 的释放路径（`ExecuteRestoreBeforeCloseBoundary` 仍须不存在）。
  hostSystemPendingReleasesBeforeCloseWhenSessionUsable = $f4Body.Contains('if (!CloseForSystemPending())') -and
    $hostSource.Contains('skipOemRestore: !preCloseReleaseAttempted') -and
    $hostSource.Contains('clearOemEvidence: false') -and
    $hostSource.Contains('power.suspend-preclose-release-decision') -and
    $hostSource.Contains('not-attempted-session-not-usable') -and
    $hostSource.Contains('realBackend.Close(stopDeviceManager);') -and
    -not $hostSource.Contains('ExecuteRestoreBeforeCloseBoundary')
  hostSystemPendingDoesNotPrewriteOem = $f4Body.Length -gt 0 -and
    -not $f4Body.Contains('RestoreOem(') -and
    -not $f4Body.Contains('RestoreHardware(close: true') -and
    $hostSource.Contains('private bool CloseForSystemPending()')
  hostSystemPendingPhysicalOwnershipIsNotObserved = $hostSource.Contains('private void MarkSystemPendingOemEvidenceNotObserved()') -and
    $hostSource.Contains('state.OemRestoreEvidence = "not-observed"') -and
    $hostSource.Contains('state.OemOwnershipStatus = "hc-close-returned-physical-unknown"') -and
    $hostSource.Contains('recoveryRetryUsesSystemPendingClose')
  hostDuplicateSuspendIsReadOnly = $hostSource.Contains('power.suspend-ignored-duplicate') -and
    $hostSource.Contains('state.State == "Suspended"') -and
    $hostSource.Contains('state.HcCloseCleanupPending')
  hostFaultSuspendReentersCloseFirst = $hostSource.Contains('power.suspend-recovery-from-fault') -and
    $f4Body.Contains('if (!CloseForSystemPending())')
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value })
if ($failed.Count -gt 0) {
  throw "fan sleep priority self-test failed: $($failed.Name -join ', ')"
}

Write-Output ('fan sleep priority self-test: PASS (' + ($checks.Keys -join ', ') + ')')
