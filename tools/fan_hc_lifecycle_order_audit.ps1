<#
.SYNOPSIS
  Read-only source order audit for the HC device lifecycle and YeMan Fan Host.

  This script only reads frozen HC source and Fan Host source. It never loads
  HC, starts ManagerFactory, invokes WMI/ACPI/HID, or writes hardware.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcRoot,
  [Parameter(Mandatory = $true)][string]$HostSource
)

$ErrorActionPreference = 'Stop'
$mainWindow = Join-Path $HcRoot 'Views\Windows\MainWindow.xaml.cs'
$device = Join-Path $HcRoot 'Devices\IDevice.cs'
$rog = Join-Path $HcRoot 'Devices\ASUS\ROGAlly.cs'
foreach ($path in @($mainWindow, $device, $rog, $HostSource)) {
  if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required source is missing: $path" }
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
  $startHits = ([regex]::Matches($text, [regex]::Escape($startToken))).Count
  if ($startHits -ne 1) { throw "Ambiguous source boundary ($startHits hits): $startToken" }
  $end = $text.IndexOf($endToken, $start + $startToken.Length, [StringComparison]::Ordinal)
  # §2（FAN-926R 裁决）：endToken 缺失时**必须报错**，不能把"全文尾部"当合法函数体。
  if ($end -lt 0) { throw "Missing source boundary (end): $endToken after $startToken" }
  if ($end -le $start) { throw "Source boundary order invalid: $startToken -> $endToken" }
  return $text.Substring($start, $end - $start)
}

$hcWindow = Get-Content -LiteralPath $mainWindow -Raw -Encoding UTF8
$hcDevice = Get-Content -LiteralPath $device -Raw -Encoding UTF8
$hcRog = Get-Content -LiteralPath $rog -Raw -Encoding UTF8
$hostText = Get-Content -LiteralPath $HostSource -Raw -Encoding UTF8

$pending = Slice $hcWindow 'case SystemManager.SystemStatus.SystemPending:' 'private void SystemManager_SessionLockChanged'
Require-Order 'HC SystemPending' $pending @(
  'ManagerFactory.Suspend();',
  'VirtualManager.SetSystemSleepState(true);',
  'ControllerManager.Suspend(true);',
  'TimerManager.Stop();',
  'SensorsManager.Suspend(true);',
  'PlatformManager.LibreHardware.Stop();',
  'CurrentDevice.Close();',
  'SystemManager.SetThreadExecutionState(SystemManager.ES_CONTINUOUS);'
)

$closed = Slice $hcWindow 'private async void Window_Closed' 'private async void Window_Closing'
Require-Order 'HC Window_Closed' $closed @(
  'CurrentDevice.Close();',
  'SystemManager.Initialized -= SystemManager_Initialized;',
  'Automation.RemoveAllEventHandlers()',
  'foreach (IManager manager in ManagerFactory.Managers)',
  'manager.Stop();',
  'VirtualManager.Stop()',
  'SystemManager.Stop();'
)

$deviceClose = Slice $hcDevice 'public virtual void Close()' 'public virtual void Initialize'
Require-Order 'HC IDevice.Close' $deviceClose @(
  'SetFanControl(false);',
  'openLibSys?.Dispose();',
  'ManagerFactory.settingsManager.Initialized -= SettingsManager_Initialized;',
  'ManagerFactory.powerProfileManager.Initialized -= PowerProfileManager_Initialized;',
  'ManagerFactory.deviceManager.Initialized -= DeviceManager_Initialized;',
  'Closed?.Invoke(this);'
)

$rogClose = Slice $hcRog 'public override void Close()' 'public override bool IsReady'
Require-Order 'HC ROG Close' $rogClose @(
  'AsusACPI.Close();',
  'ConfigureController(false);',
  'foreach (HidDevice hidDevice in hidDevices.Values)',
  'hidDevice.Dispose();',
  'base.Close();'
)

$hostOpen = Slice $hostText 'private void OpenCore()' 'public void OpenEvents()'
Require-Order 'YeMan Open' $hostOpen @(
  'WaitForHcDeviceReadyBeforeOpen();',
  'OpenHcDevice();',
  'CaptureOemBaseline();'
)
if ($hostOpen.Contains('StartHcDeviceManager();')) { throw 'YeMan fan-only Open must not start HC DeviceManager' }
# §2（FAN-926R 裁决）：旧要求（独立 `private void OpenEventsCore()`）已被**现行合同取代** ——
# HC OpenEvents 的生产入口现为 `private HcDeviceOpenNeed OpenEventsBeginCore()`，由
# `RealHcBackend.OpenEvents()` 经 `OnStaBounded` 调用。断言内容不变，只重定位入口。
$hostEvents = Slice $hostText 'private HcDeviceOpenNeed OpenEventsBeginCore()' 'private void OpenEventsFinalizeCore()'
# 旧要求（`Invoke` 之后再轮询 `EnsureHcDeviceOpenForRestore()`）已被 920-v1.14 §25 **正式取代**：
# 现合同 = **调用前**确证 HC 会话/路由就绪（`EnsureHcSessionReadyForOpenEvents()`）+ **调用后**
# 以正式收据开始设备打开（`BeginHcDeviceOpenForRestore()`）。`tools\fan_host_lifecycle_selftest.ts`
# 与 `tools\fan_payload_selftest.ps1` 已记录该取代；本条门此前漏同步（FAN-926R 裁决 §2 修正）。
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
$hostCloseBoundary = Slice $hostText 'private void CloseHcDevice()' 'private static void ExecuteCloseBoundary'
Require-Order 'YeMan CloseHcDevice' $hostCloseBoundary @(
  'Invoke(device!, "Close");',
  'HcVirtualCloseReturned = true;',
  'UnsubscribeExternalProfileEvents();',
  'confirmManagerCleanup: ConfirmManagerCleanupAfterClose'
)

$fullGraph = $hostText.IndexOf('foreach (IManager manager in ManagerFactory.Managers)', [StringComparison]::Ordinal) -ge 0
$fanOnly = (-not $hostText.Contains('StartHcDeviceManager();')) -and
  (-not $hostText.Contains('StopHcDeviceManager();')) -and
  $hostText.Contains('hcDeviceManagerLifecycle = ManagerFactoryNotStarted;') -and
  $hostText.Contains('ManagerFactoryNotStarted = "not-started/no-stop-required"')
$directCallback = $hostText.IndexOf('Invoke("PowerProfileManager_Applied", profile', [StringComparison]::Ordinal) -ge 0
$powerManagerStarted = $hostText.IndexOf('Invoke(hcPowerProfileManager, "Start")', [StringComparison]::Ordinal) -ge 0

Write-Output 'fan HC lifecycle order audit: EXECUTED (hardwareWrites=false)'
Write-Output ("fan HC lifecycle order audit: needs-investigation (fullManagerGraph={0}; fanOnlyManagerIsolation={1}; directProfileCallback={2}; powerProfileManagerStarted={3}; physicalOemAck=false)" -f $fullGraph, $fanOnly, $directCallback, $powerManagerStarted)
Write-Output 'HC device-level order is source-confirmed; complete HC application graph and physical OEM acknowledgement remain unproven.'
