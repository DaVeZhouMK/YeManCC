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
  $end = $text.IndexOf($endToken, $start + $startToken.Length, [StringComparison]::Ordinal)
  if ($end -lt 0) { $end = $text.Length }
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
  'StartHcDeviceManager();',
  'WaitForHcDeviceReadyBeforeOpen();',
  'OpenHcDevice();',
  'CaptureOemBaseline();'
)
$hostEvents = Slice $hostText 'private void OpenEventsCore()' 'private void SubscribeExternalProfileEvents()'
Require-Order 'YeMan OpenEvents' $hostEvents @(
  'AssertHcNonFanManagersIsolated();',
  'Invoke(device!, "OpenEvents");',
  'WaitForHcDeviceOpenForRestore();'
)
$hostClose = Slice $hostText 'private void CloseCore(bool stopDeviceManager)' 'private void WaitForHcManagersBeforeWindowClose()'
Require-Order 'YeMan Close' $hostClose @(
  'WaitForHcManagersBeforeWindowClose();',
  'StopCpuTemperatureMonitor();',
  'UnsubscribeExternalProfileEvents();',
  'CloseHcDevice(stopDeviceManager);'
)
$hostCloseBoundary = Slice $hostText 'private void CloseHcDevice(bool stopDeviceManager = true)' 'private void SetHcPhase'
Require-Order 'YeMan CloseHcDevice' $hostCloseBoundary @(
  'Invoke(device!, "Close");',
  'StopHcDeviceManager();'
)

$fullGraph = $hostText.IndexOf('foreach (IManager manager in ManagerFactory.Managers)', [StringComparison]::Ordinal) -ge 0
$fanOnly = ($hostText.IndexOf('StartHcDeviceManager();', [StringComparison]::Ordinal) -ge 0) -and
  ($hostText.IndexOf('AssertHcNonFanManagersIsolated', [StringComparison]::Ordinal) -ge 0)
$directCallback = $hostText.IndexOf('Invoke("PowerProfileManager_Applied", profile', [StringComparison]::Ordinal) -ge 0
$powerManagerStarted = $hostText.IndexOf('Invoke(hcPowerProfileManager, "Start")', [StringComparison]::Ordinal) -ge 0

Write-Output 'fan HC lifecycle order audit: EXECUTED (hardwareWrites=false)'
Write-Output ("fan HC lifecycle order audit: needs-investigation (fullManagerGraph={0}; fanOnlyManagerIsolation={1}; directProfileCallback={2}; powerProfileManagerStarted={3}; physicalOemAck=false)" -f $fullGraph, $fanOnly, $directCallback, $powerManagerStarted)
Write-Output 'HC device-level order is source-confirmed; complete HC application graph and physical OEM acknowledgement remain unproven.'
