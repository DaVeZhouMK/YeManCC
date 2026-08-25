$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$native = Get-Content -Raw (Join-Path $repoRoot 'native\main.cpp')
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $repoRoot)
$hostSource = Get-Content -Raw (Join-Path $workspaceRoot 'FanLab\real-host\Program.cs')

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
$hostQueueStart = $hostSource.IndexOf('private void QueuePowerTransition', $hostCallbackStart)
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
  hostSystemPowerObserver = $hostSource.Contains('SystemEvents.PowerModeChanged += OnPowerModeChanged')
  hostPowerCallbackQueues = $hostSource.Contains('QueuePowerTransition("suspend", () => { _ = engine.SuspendForSystemPower(); })') -and
    $hostSource.Contains('QueuePowerTransition("resume", () => { _ = engine.ResumeForSystemPower(); })')
  hostPowerCallbackHasNoWait = $hostCallbackBody.Contains('QueuePowerTransition') -and
    -not ($hostCallbackBody.Contains('.Wait(') -or $hostCallbackBody.Contains('.Result') -or
      $hostCallbackBody.Contains('GetAwaiter().GetResult') -or $hostCallbackBody.Contains('Thread.Sleep') -or
      $hostCallbackBody.Contains('lock (') -or $hostCallbackBody.Contains('diagnostics.Write'))
  hostSuspendGateIsLockFree = $hostSource.Contains('Volatile.Write(ref systemSuspendPending, 1)') -and
    $hostSource.Contains('realBackend?.BlockWritesForSuspend()')
  hostPowerQueueRunsAsync = $hostSource.Contains('Channel.CreateUnbounded<PowerTransition>') -and
    $hostSource.Contains('powerTransitions.Writer.TryWrite') -and
    $hostSource.Contains('ProcessPowerTransitionsAsync')
  hostRestoreBeforeClose = $hostSource.Contains('if (!RestoreHardware(close: true))')
  hostDuplicateSuspendIsReadOnly = $hostSource.Contains('power.suspend-ignored-duplicate') -and
    $hostSource.Contains('state.State == "Suspended"') -and
    $hostSource.Contains('state.HcCloseCleanupPending')
  hostFaultSuspendReentersRecovery = $hostSource.Contains('power.suspend-recovery-from-fault') -and
    $hostSource.Contains('if (!RestoreHardware(close: true, stopDeviceManager: false))')
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value })
if ($failed.Count -gt 0) {
  throw "fan sleep priority self-test failed: $($failed.Name -join ', ')"
}

Write-Output ('fan sleep priority self-test: PASS (' + ($checks.Keys -join ', ') + ')')
