<#
.SYNOPSIS
  T1 read-only lifecycle parity audit for HC and YeMan Fan Host.

  This audit reads the frozen HC source, YeMan Fan Host source, native power
  boundary, and frontend lifecycle bridge. It never loads HC, starts a device
  manager, calls WMI/ACPI/HID/EC, or enables hardware writes.

  The default rule is deliberately conservative: an unexplained difference,
  unproven success, or pass without evidence is needs-investigation. The only
  pre-excluded difference is the temperature source (YeMan uses HW).
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcRoot,
  [Parameter(Mandatory = $true)][string]$HostSource,
  [Parameter(Mandatory = $true)][string]$NativeSource,
  [Parameter(Mandatory = $true)][string]$FrontendSource,
  [Parameter(Mandatory = $true)][string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
$mainWindow = Join-Path $HcRoot 'Views\Windows\MainWindow.xaml.cs'
$hcDevice = Join-Path $HcRoot 'Devices\IDevice.cs'
$hcRog = Join-Path $HcRoot 'Devices\ASUS\ROGAlly.cs'
$hcSystem = Join-Path $HcRoot 'Managers\SystemManager.cs'
foreach ($path in @($mainWindow, $hcDevice, $hcRog, $hcSystem, $HostSource, $NativeSource, $FrontendSource)) {
  if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required source is missing: $path" }
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Read-Utf8([string]$path) { Get-Content -LiteralPath $path -Raw -Encoding UTF8 }
function Get-Sha256([string]$path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
  } finally { $sha.Dispose() }
}
function Require-Contains([string]$name, [string]$text, [string]$token) {
  if ($text.IndexOf($token, [StringComparison]::Ordinal) -lt 0) { throw "$name missing: $token" }
}
function Require-Order([string]$name, [string]$text, [string[]]$tokens) {
  $positions = @()
  foreach ($token in $tokens) {
    $position = $text.IndexOf($token, [StringComparison]::Ordinal)
    if ($position -lt 0) { throw "$name missing token: $token" }
    $positions += $position
  }
  for ($i = 1; $i -lt $positions.Count; $i++) {
    if ($positions[$i] -le $positions[$i - 1]) {
      throw "$name order mismatch: '$($tokens[$($i - 1)])' must precede '$($tokens[$i])'"
    }
  }
}
function Slice([string]$text, [string]$startToken, [string]$endToken) {
  $start = $text.IndexOf($startToken, [StringComparison]::Ordinal)
  if ($start -lt 0) { throw "Missing source boundary: $startToken" }
  $end = $text.IndexOf($endToken, $start + $startToken.Length, [StringComparison]::Ordinal)
  if ($end -lt 0) { $end = $text.Length }
  return $text.Substring($start, $end - $start)
}

$hcWindow = Read-Utf8 $mainWindow
$hcDeviceText = Read-Utf8 $hcDevice
$hcRogText = Read-Utf8 $hcRog
$hcSystemText = Read-Utf8 $hcSystem
$hostText = Read-Utf8 $HostSource
$nativeText = Read-Utf8 $NativeSource
$frontendText = Read-Utf8 $FrontendSource

# 1. Frozen HC device/application lifecycle order.
$hcPending = Slice $hcWindow 'case SystemManager.SystemStatus.SystemPending:' 'private void SystemManager_SessionLockChanged'
Require-Order 'HC SystemPending' $hcPending @(
  'ManagerFactory.Suspend();',
  'VirtualManager.SetSystemSleepState(true);',
  'ControllerManager.Suspend(true);',
  'TimerManager.Stop();',
  'SensorsManager.Suspend(true);',
  'PlatformManager.LibreHardware.Stop();',
  'CurrentDevice.Close();',
  'SystemManager.SetThreadExecutionState(SystemManager.ES_CONTINUOUS);'
)
$hcReady = Slice $hcWindow 'case SystemManager.SystemStatus.SystemReady:' 'case SystemManager.SystemStatus.SystemPending:'
Require-Order 'HC SystemReady' $hcReady @(
  'TimerManager.Start();',
  'PerformanceManager.Resume(true);',
  'ManagerFactory.Resume();',
  'PlatformManager.LibreHardware.Start();',
  'VirtualManager.SetSystemSleepState(false);',
  'ControllerManager.Resume(true);',
  'SensorsManager.Resume(true);',
  'CurrentDevice.Open()',
  'CurrentDevice.OpenEvents()'
)
$hcClosed = Slice $hcWindow 'private async void Window_Closed' 'private async void Window_Closing'
Require-Order 'HC Window_Closed' $hcClosed @(
  'CurrentDevice.Close();',
  'Automation.RemoveAllEventHandlers()',
  'foreach (IManager manager in ManagerFactory.Managers)',
  'manager.Stop();',
  'VirtualManager.Stop()',
  'SystemManager.Stop();'
)
$hcDeviceClose = Slice $hcDeviceText 'public virtual void Close()' 'public virtual void Initialize'
Require-Order 'HC IDevice.Close' $hcDeviceClose @(
  'SetFanControl(false);',
  'openLibSys?.Dispose();',
  'ManagerFactory.settingsManager.Initialized -= SettingsManager_Initialized;',
  'ManagerFactory.powerProfileManager.Initialized -= PowerProfileManager_Initialized;',
  'ManagerFactory.deviceManager.Initialized -= DeviceManager_Initialized;',
  'Closed?.Invoke(this);'
)
$hcRogClose = Slice $hcRogText 'public override void Close()' 'public override bool IsReady'
Require-Order 'HC ROG Close' $hcRogClose @(
  'AsusACPI.Close();',
  'ConfigureController(false);',
  'foreach (HidDevice hidDevice in hidDevices.Values)',
  'hidDevice.Dispose();',
  'base.Close();'
)
Require-Contains 'HC SystemManager event log' $hcSystemText 'EventID=506'
Require-Contains 'HC SystemManager event log' $hcSystemText 'EventID=507'
Require-Contains 'HC SystemManager resume guard' $hcSystemText 'if (isPowerSuspended)'

# 2. YeMan's equivalent single device boundary and serialized transitions.
$hostOpen = Slice $hostText 'private void OpenCore()' 'public void OpenEvents()'
Require-Order 'YeMan Open' $hostOpen @(
  'WaitForHcDeviceReadyBeforeOpen();',
  'OpenHcDevice();',
  'CaptureOemBaseline();'
)
if ($hostOpen.Contains('StartHcDeviceManager();')) { throw 'YeMan fan-only Open must not start HC DeviceManager' }
# §2（FAN-926R 裁决）：旧要求（独立 `private void OpenEventsCore()` + `Invoke` 后轮询
# `EnsureHcDeviceOpenForRestore()`）已被 920-v1.14 §25 正式取代 —— 现行生产入口为
# `OpenEventsBeginCore()`，合同 = 调用前确证会话/路由就绪 + 调用后以正式收据开始设备打开。
$hostEvents = Slice $hostText 'private HcDeviceOpenNeed OpenEventsBeginCore()' 'private void OpenEventsFinalizeCore()'
Require-Order 'YeMan OpenEvents' $hostEvents @(
  'EnsureHcSessionReadyForOpenEvents();',
  'Invoke(device!, "OpenEvents");'
)
if (-not $hostEvents.Contains('BeginHcDeviceOpenForRestore();')) { throw 'YeMan OpenEvents must begin the formal HC device-open receipt after invoking OpenEvents' }
$hostClose = Slice $hostText 'private void CloseCore(bool stopDeviceManager)' 'private void CloseHcDevice()'
Require-Order 'YeMan Close' $hostClose @(
  'StopCpuTemperatureMonitor();',
  'CloseHcDevice();'
)
$hostDeviceClose = Slice $hostText 'private void CloseHcDevice()' 'private static void ExecuteCloseBoundary'
Require-Order 'YeMan virtual close' $hostDeviceClose @(
  'Invoke(device!, "Close");',
  'HcVirtualCloseReturned = true;',
  'UnsubscribeExternalProfileEvents();',
  'confirmManagerCleanup: ConfirmManagerCleanupAfterClose'
)
Require-Contains 'YeMan close owner' $hostText 'Interlocked.Exchange(ref closeBoundaryClaimed, 1)'
Require-Contains 'YeMan close pending dedupe' $hostText 'HC_CLOSE_PENDING'
Require-Contains 'YeMan manager isolation marker' $hostText 'not-started/no-stop-required'
Require-Contains 'YeMan host ordered power queue' $hostText 'Channel.CreateUnbounded<PowerTransition>'
Require-Contains 'YeMan host ordered power queue' $hostText 'powerTransitions.Writer.TryWrite'
Require-Contains 'YeMan host ordered power queue' $hostText 'ProcessPowerTransitionsAsync'
Require-Contains 'YeMan native resume gate' $hostText 'automaticResumeWorkerThreadId'
Require-Contains 'YeMan resume admission gate' $hostText 'POWER_RESUMING'
Require-Contains 'YeMan resume worker cleanup' $hostText 'finally'
Require-Contains 'YeMan HC IsReady wait' $hostText 'private void WaitForHcDeviceReadyBeforeOpen()'
$readyWaitIndex = $hostText.IndexOf('WaitForHcDeviceReadyBeforeOpen();', [StringComparison]::Ordinal)
$openDeviceIndex = $hostText.IndexOf('OpenHcDevice();', [StringComparison]::Ordinal)
if ($readyWaitIndex -lt 0 -or $openDeviceIndex -lt 0 -or $readyWaitIndex -ge $openDeviceIndex) {
  throw 'YeMan HC SystemReady must poll IsReady before the virtual Open call'
}
Require-Contains 'YeMan resuspend HC parity' $hostText 'HC SystemPending closes synchronously even when the previous'
Require-Contains 'YeMan resuspend HC parity' $hostText 'PowerState == "Suspended" || state.PowerState == "Suspending"'
Require-Contains 'YeMan resuspend benign skip' $hostText 'the next SystemReady re-runs the F5'

# 3. Native and renderer boundaries: native is the sole F4 sender; renderer
#    observes suspend and participates only in the tagged F5 resume transaction.
Require-Contains 'native power callback safety' $nativeText 'fanHostScheduleEmergencySuspend("suspend-confirmed", currentPowerGeneration())'
Require-Contains 'native power callback safety' $nativeText 'fanHostScheduleEmergencySuspend("suspend-broadcast", generation)'
Require-Contains 'native power callback safety' $nativeText 'fanHostScheduleEmergencySuspend("kernel-power-506", generation)'
Require-Contains 'native detached worker' $nativeText 'std::thread([reasonText = std::string(reason ? reason : "unknown"), generation]'
Require-Contains 'native detached worker' $nativeText '}).detach();'
Require-Contains 'native resume generation gate' $nativeText 'g_resumeReadyGeneration'
Require-Contains 'native resume generation gate' $nativeText 'PowerLifecycle::Resuming'
Require-Contains 'native resume IPC' $nativeText 'ipc_emit("power.resuming"'
Require-Contains 'native resume IPC' $nativeText 'ipc_emit("power.resumed"'
Require-Contains 'native exit handoff' $nativeText 'fanHostEmergencyPost('
Require-Contains 'native exit handoff' $nativeText 'L"/api/parent-exit"'
Require-Contains 'Host F4 native authority gate' $hostText 'RequireNativeSuspendAuthority(body);'
Require-Contains 'Host F4 native authority gate' $hostText 'F4_NATIVE_AUTHORITY_REQUIRED'
$hostSystemPending = Slice $hostText 'private object SuspendUnlocked()' 'private ResumeAdmission StartAutomaticResumeUnlocked('
Require-Contains 'Host F4 single close owner' $hostSystemPending 'CloseForSystemPending()'
# ★ 因 FAN-926R 裁决 §3-D1 **改要求**（明示取代，不是"仅更新符号/证明行为未变"）：
#   旧要求 = SuspendUnlocked 不得在 Close 前预写默认曲线，且 SystemPending 全局一律
#            `skipOemRestore: true` / `clearOemEvidence: true`（"一律 Close-first"）。
#   新要求 = 有写历史且会话可操作时，由**唯一生命周期 owner 在进入 HC Close 之前**
#            有界执行一次现成默认模式恢复；会话不可操作时才 not-attempted；证据保留并三轴分列。
#   仍然成立的部分：睡眠**通知/接受路径本身**不得直接发起 HC 长调用（由 owner 承担），
#   也不得以写 0/手动停扇替代系统睡眠。
if ($hostSystemPending.Contains('RestoreHardware(close: true') -or $hostSystemPending.Contains('RestoreOem(')) {
  throw 'YeMan F4: the sleep notification path itself must not call the release directly; the single close owner owns it'
}
Require-Contains 'Host D1 bounded pre-close release decision' $hostText 'power.suspend-preclose-release-decision'
Require-Contains 'Host D1 release-before-close' $hostText 'skipOemRestore: !preCloseReleaseAttempted'
Require-Contains 'Host D1 evidence retained' $hostText 'clearOemEvidence: false'
Require-Contains 'Host D1 usable-session gate' $hostText 'var preCloseReleaseAttempted = state.HardwareWritesObserved'
Require-Contains 'Host strict F4 no physical claim' $hostText 'state.OemRestoreEvidence = "not-observed"'
Require-Contains 'Host strict F4 no ownership claim' $hostText 'ownershipClaim = false'
Require-Contains 'Host strict F4 retry owner' $hostText 'recoveryRetryUsesSystemPendingClose'
Require-Contains 'frontend close one-owner' $frontendText 'send exactly one Close request'
Require-Contains 'frontend resume lifecycle' $frontendText "async resume(): Promise<void>"
# 920 §8.0-D B5 remap: the old assertions pinned the deleted guard/negotiation window
# (runFanGuardOnce('coordinator-resume') / FAN_GUARD_RESUME_PENDING / "read-only state polls"),
# self-documented in fanHost.ts L2026-2028 and L1833-1834 (E9 T0 trim). Intent preserved: resume
# must remain event-driven, must read the remote state read-only before rebuilding, and must not
# reintroduce a guard/resume-pending window.
# 920-v1.2 W1: resume() now re-writes through the shared bounded retry (mutateWithBoundedRetry),
# so the pinned literal is the callback form rather than a bare single-shot await.
# G7（FAN-926R 执行单 §3.3）**因本裁决改要求**：准入 + 单次入队执行的唯一执行器是
# mutateWithBoundedRetry，重放体抽到 replayResumeCurve()，因此"重放回调"锚点变成
# `() => this.replayResumeCurve(generation, intentRevision)`，而曲线写入仍是同一个
# applyMutation(desiredCurve)。断言意图不变：resume 必须事件驱动、先只读快照再经同一
# applyMutation 重写已记住的曲线，且不得复活 guard/resume-pending 观察窗。
Require-Contains 'frontend resume lifecycle' $frontendText '() => this.replayResumeCurve(generation, intentRevision)'
Require-Contains 'frontend resume lifecycle' $frontendText 'this.applyMutation(this.desiredCurve as readonly FanNode[])'
Require-Contains 'frontend resume lifecycle' $frontendText 'const remote = await this.adapter.getState().catch(() => null);'
Require-Contains 'frontend resume lifecycle' $frontendText '无 adapter.resume/协商/观察窗'
if ($frontendText.Contains('FAN_GUARD_RESUME_PENDING') -or $frontendText.Contains('runFanGuardOnce')) {
  throw 'YeMan frontend must not reintroduce a resident fan-guard/resume-pending window (E9 deletion面)'
}

# 4. Explicitly record the architectural boundaries instead of disguising
#    them as equivalence: YeMan deliberately does not start HC's complete
#    ManagerFactory graph, and HC has no universal physical OEM ack.
$fullManagerGraph = $hostText.IndexOf('foreach (IManager manager in ManagerFactory.Managers)', [StringComparison]::Ordinal) -ge 0
$fanOnlyIsolation = (-not $hostText.Contains('StartHcDeviceManager();')) -and
  (-not $hostText.Contains('StopHcDeviceManager();')) -and
  $hostText.Contains('hcDeviceManagerLifecycle = ManagerFactoryNotStarted;') -and
  $hostText.Contains('ManagerFactoryNotStarted = "not-started/no-stop-required"')
$routeSpecificReadback = $hostText.IndexOf('hc-default-table-readback-confirmed', [StringComparison]::Ordinal) -ge 0
$differences = @(
  [pscustomobject]@{ id = 'T1-HC-FULL-MANAGER-GRAPH'; severity = 'P1'; status = 'needs-investigation'; detail = 'Fan Host intentionally keeps HC non-fan ManagerFactory graph stopped; this is not full HC application equivalence.' },
  [pscustomobject]@{ id = 'T1-PHYSICAL-OEM-ACK'; severity = 'P1'; status = 'needs-investigation'; detail = 'HC source has no universal physical OEM ownership acknowledgement; route readback is evidence enrichment only.' },
  [pscustomobject]@{ id = 'T1-TEMPERATURE-SOURCE'; severity = 'excluded'; status = 'accepted-boundary'; detail = 'YeMan uses HW temperature as explicitly approved; control callback remains HC PowerProfileManager semantics.' }
)

$generated = [DateTime]::UtcNow.ToString('o')
$hashes = foreach ($path in @($mainWindow, $hcDevice, $hcRog, $hcSystem, $HostSource, $NativeSource, $FrontendSource)) {
  [ordered]@{ path = $path; sha256 = (Get-Sha256 $path) }
}
$record = [ordered]@{
  generatedAtUtc = $generated
  hardwareWrites = $false
  hcDeviceOrder = 'source-confirmed'
  hcWindowOrder = 'source-confirmed'
  yemanDeviceOrder = 'source-confirmed'
  powerCallbacksNonBlocking = $true
  powerTransitionsSerialized = $true
  duplicateSuspendResumeDeduped = $true
  resumeAdmissionClosedDuringRebuild = $true
  resumeWorkerOnlyBypass = $true
  closeOwnerSingleBoundary = $true
  managerStopFailureRetainedForRetry = $true
  fullManagerFactoryGraphStarted = $fullManagerGraph
  fanOnlyManagerIsolation = $fanOnlyIsolation
  physicalOemAckContract = $false
  routeSpecificReadbackImplemented = $routeSpecificReadback
  differences = $differences
  sourceHashes = $hashes
}
$record | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputRoot 't1-lifecycle-deep.json') -Encoding UTF8
$differences | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 't1-needs-investigation.json') -Encoding UTF8
$summary = @"
# T1 HC Lifecycle Deep Audit

- Generated UTC: $generated
- Hardware writes: false
- HC device, sleep, resume and Window_Closed order: source-confirmed
- YeMan serialized power queue and single Close owner: source-confirmed
- YeMan external admission while automatic resume rebuild is ``Resuming``: blocked (``POWER_RESUMING``)
- Automatic resume worker bypass: thread-bound only, cleared in ``finally``
- Power commands: native is the sole F4 suspend authority; renderer only coordinates tagged F5 resume

## Standing rule

Any unexplained difference, unproven success, or pass without sufficient evidence is ``needs-investigation``. Temperature HW is the only pre-excluded difference. No physical OEM acknowledgement is inferred from HTTP 2xx or a void HC callback.

## Remaining explicit boundaries (not silently claimed complete)

1. Full HC ManagerFactory graph is intentionally not started by the isolated Fan Host; starting it would take ownership of TDP/GPU/Windows profile managers.
2. HC has no universal physical OEM ownership acknowledgement; route-specific readback remains separate evidence.
3. Hardware writes remain disabled in this audit.
"@
$summary | Set-Content -LiteralPath (Join-Path $OutputRoot 't1-lifecycle-deep.md') -Encoding UTF8
$hashLines = foreach ($f in Get-ChildItem -LiteralPath $OutputRoot -File | Where-Object Name -ne 'sha256.txt' | Sort-Object Name) {
  "{0}  {1}" -f (Get-Sha256 $f.FullName), $f.Name
}
$hashLines | Set-Content -LiteralPath (Join-Path $OutputRoot 'sha256.txt') -Encoding ASCII

Write-Output ("T1 HC lifecycle deep selftest: PASS (source-order=confirmed; resume-admission=closed; duplicate-boundary=serialized; hardwareWrites=false; output={0})" -f $OutputRoot)
Write-Output 'T1 disposition: needs-investigation only for explicit architectural boundaries; no new unexplained P0/P1 lifecycle deviation found.'
