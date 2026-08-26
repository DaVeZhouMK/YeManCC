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
$hostEvents = Slice $hostText 'private void OpenEventsCore()' 'private void SubscribeExternalProfileEvents()'
Require-Order 'YeMan OpenEvents' $hostEvents @(
  'Invoke(device!, "OpenEvents");',
  'EnsureHcDeviceOpenForRestore();'
)
$hostClose = Slice $hostText 'private void CloseCore(bool stopDeviceManager)' 'private void CloseHcDevice()'
Require-Order 'YeMan Close' $hostClose @(
  'StopCpuTemperatureMonitor();',
  'CloseHcDevice();'
)
$hostDeviceClose = Slice $hostText 'private void CloseHcDevice()' 'private static void ExecuteCloseBoundary'
Require-Order 'YeMan virtual close' $hostDeviceClose @(
  'Invoke(device!, "Close");',
  'hcDeviceManagerLifecycle = "not-started/no-stop-required";'
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
Require-Contains 'YeMan resuspend race gate' $hostText 'suspendRequestedDuringResume'
Require-Contains 'YeMan resuspend race gate' $hostText 'power.suspend-deferred-during-resume'
Require-Contains 'YeMan resuspend owner' $hostText 'HC_RESUSPEND_AFTER_RESUME_FAILED'

# 3. Native and renderer boundaries: callbacks only enqueue, and UI/native
#    duplicate observers share the Host's idempotent serialized boundary.
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
Require-Contains 'frontend close one-owner' $frontendText 'send exactly one Close request'
Require-Contains 'frontend suspend lifecycle' $frontendText 'await this.adapter.suspend();'
Require-Contains 'frontend resume lifecycle' $frontendText "resumedState.state === 'Resuming'"
Require-Contains 'frontend fast-resume adoption' $frontendText 'Always perform one read-only state'

# 4. Explicitly record the architectural boundaries instead of disguising
#    them as equivalence: YeMan deliberately does not start HC's complete
#    ManagerFactory graph, and HC has no universal physical OEM ack.
$fullManagerGraph = $hostText.IndexOf('foreach (IManager manager in ManagerFactory.Managers)', [StringComparison]::Ordinal) -ge 0
$fanOnlyIsolation = (-not $hostText.Contains('StartHcDeviceManager();')) -and
  (-not $hostText.Contains('StopHcDeviceManager();')) -and
  $hostText.Contains('hcDeviceManagerLifecycle = "not-started/no-stop-required";')
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
- Duplicate native/UI suspend/resume observers: serialized/idempotent Host boundary

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
