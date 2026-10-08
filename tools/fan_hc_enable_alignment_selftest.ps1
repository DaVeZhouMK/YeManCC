<#
.SYNOPSIS
  Read-only conformance check between YeManFanHost and the frozen HC enable path.

.DESCRIPTION
  This test reads source files only. It does not load HC, open a device, access
  WMI/EC/ACPI/HID, or start YeManFanHost. It guards the intended boundary:
  every non-temperature fan operation must enter the pinned HC virtual device
  lifecycle and its original PowerProfileManager_Applied callback.
#>
[CmdletBinding()]
param(
  [string]$HcRoot,
  [string]$HostSource
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($HcRoot)) {
  $HcRoot = Join-Path $PSScriptRoot '..\deps\handheldcompanion-runtime\source'
}
if ([string]::IsNullOrWhiteSpace($HostSource)) {
  $HostSource = Join-Path $PSScriptRoot '..\FanLab\real-host\Program.cs'
}

function Assert-Check([bool]$Condition, [string]$Name) {
  if (-not $Condition) { throw "HC enable alignment failed: $Name" }
}

function Read-Required([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required source is missing: $Path" }
  return Get-Content -LiteralPath $Path -Raw -Encoding UTF8
}

$app = Read-Required (Join-Path $HcRoot 'App.xaml.cs')
$window = Read-Required (Join-Path $HcRoot 'Views\Windows\MainWindow.xaml.cs')
$device = Read-Required (Join-Path $HcRoot 'Devices\IDevice.cs')
$powerProfileManager = Read-Required (Join-Path $HcRoot 'Managers\PowerProfileManager.cs')
$fanProfile = Read-Required (Join-Path $HcRoot 'Misc\FanProfile.cs')
$managerFactory = Read-Required (Join-Path $HcRoot 'Managers\ManagerFactory.cs')
$deviceManager = Read-Required (Join-Path $HcRoot 'Managers\DeviceManager.cs')
$msi = Read-Required (Join-Path $HcRoot 'Devices\MSI\ClawA1M.cs')
$lenovo = Read-Required (Join-Path $HcRoot 'Devices\Lenovo\LegionGo.cs')
$lenovoGo2 = Read-Required (Join-Path $HcRoot 'Devices\Lenovo\LegionGoTablet2.cs')
$rog = Read-Required (Join-Path $HcRoot 'Devices\ASUS\ROGAlly.cs')
$ayanFlip1 = Read-Required (Join-Path $HcRoot 'Devices\AYANEO\AYANEOFlip1SDS.cs')
$ayanFlipDs = Read-Required (Join-Path $HcRoot 'Devices\AYANEO\AYANEOFlipDS.cs')
$hostSourceText = Read-Required $HostSource
$fanApiSource = Read-Required (Join-Path $PSScriptRoot '..\src\bridge\fanApi.ts')
$fanHostSource = Read-Required (Join-Path $PSScriptRoot '..\src\bridge\fanHost.ts')
$allDeviceSource = [string]::Join("`n", @(Get-ChildItem -LiteralPath (Join-Path $HcRoot 'Devices') -Recurse -File -Filter '*.cs' | ForEach-Object {
  Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8
}))

# Frozen HC startup: Initialize, then ManagerFactory (DeviceManager is first),
# and when the system is ready, IsReady -> Open -> OpenEvents.
$initializeIndex = $app.IndexOf('IDevice.GetCurrent().Initialize(firstStart, newUpdate);')
$managerStartIndex = $app.IndexOf('foreach (IManager manager in ManagerFactory.Managers)')
$deviceManagerIndex = $managerFactory.IndexOf('deviceManager,')
$readyIndex = $window.IndexOf('while (!timeout.IsCompleted && !CurrentDevice.IsReady())')
$openIndex = $window.IndexOf('if (CurrentDevice.Open())')
$openEventsIndex = $window.IndexOf('CurrentDevice.OpenEvents();', $openIndex)
Assert-Check ($initializeIndex -ge 0 -and $managerStartIndex -gt $initializeIndex -and $deviceManagerIndex -ge 0) 'upstream initialization and DeviceManager ordering'
Assert-Check ($readyIndex -ge 0 -and $openIndex -gt $readyIndex -and $openEventsIndex -gt $openIndex) 'upstream IsReady -> Open -> OpenEvents ordering'

# DeviceManager.Start owns its own PrepareStart boundary in the frozen HC
# source.  YeMan must call that public Start method, not try to duplicate or
# reorder PrepareStart from the Host reflection layer.
Assert-Check ($deviceManager.Contains('public override void Start()') -and
  $deviceManager.Contains('base.PrepareStart();') -and
  $deviceManager.IndexOf('base.PrepareStart();') -lt $deviceManager.IndexOf('base.Start();')) 'upstream DeviceManager.Start owns PrepareStart -> listener setup -> base.Start'

# Freeze the two process/power close orders as source evidence.  The complete
# HC app suspends only its SuspendWithOS managers before CurrentDevice.Close;
# Window_Closed waits for Initializing managers, then calls CurrentDevice.Close.
$pendingIndex = $window.IndexOf('case SystemManager.SystemStatus.SystemPending:')
$pendingEnd = $window.IndexOf('case SystemManager.SystemStatus.SystemReady:', $pendingIndex)
if ($pendingEnd -lt 0) { $pendingEnd = $window.Length }
$pending = if ($pendingIndex -ge 0) { $window.Substring($pendingIndex, $pendingEnd - $pendingIndex) } else { '' }
Assert-Check ($pending.IndexOf('ManagerFactory.Suspend();') -ge 0 -and
  $pending.IndexOf('VirtualManager.SetSystemSleepState(true);') -gt $pending.IndexOf('ManagerFactory.Suspend();') -and
  $pending.IndexOf('ControllerManager.Suspend(true);') -gt $pending.IndexOf('VirtualManager.SetSystemSleepState(true);') -and
  $pending.IndexOf('TimerManager.Stop();') -gt $pending.IndexOf('ControllerManager.Suspend(true);') -and
  $pending.IndexOf('SensorsManager.Suspend(true);') -gt $pending.IndexOf('TimerManager.Stop();') -and
  $pending.IndexOf('PlatformManager.LibreHardware.Stop();') -gt $pending.IndexOf('SensorsManager.Suspend(true);') -and
  $pending.IndexOf('CurrentDevice.Close();') -gt $pending.IndexOf('PlatformManager.LibreHardware.Stop();')) 'upstream SystemPending manager-stop -> CurrentDevice.Close order'
$windowCloseIndex = $window.IndexOf('private async void Window_Closed(')
$windowClose = if ($windowCloseIndex -ge 0) { $window.Substring($windowCloseIndex) } else { '' }
Assert-Check ($windowClose.IndexOf('ManagerFactory.Managers.Any(manager => manager.Status.HasFlag(ManagerStatus.Initializing))') -ge 0 -and
  $windowClose.IndexOf('CurrentDevice.Close();') -gt $windowClose.IndexOf('while (ManagerFactory.Managers.Any(manager => manager.Status.HasFlag(ManagerStatus.Initializing)))')) 'upstream Window_Closed waits for manager initialization before CurrentDevice.Close'

# The read-only handshake follows HC's device discovery/bootstrap sequence but
# deliberately passes FirstStart=false. The only two upstream Initialize
# overrides are UI preference setup guarded by FirstStart; neither may turn a
# handshake into a fan/EC/ACPI write.
$flipDsInitializeStart = $ayanFlipDs.IndexOf('public override void Initialize(bool FirstStart, bool NewUpdate)')
$flipDsOpenEventsStart = $ayanFlipDs.IndexOf('public override void OpenEvents()', $flipDsInitializeStart)
$flipDsInitialize = $ayanFlipDs.Substring($flipDsInitializeStart, $flipDsOpenEventsStart - $flipDsInitializeStart)
Assert-Check (([regex]::Matches($allDeviceSource, 'override\s+void\s+Initialize\s*\(')).Count -eq 2) 'frozen HC Initialize override count'
Assert-Check ($ayanFlip1.Contains('if (FirstStart)') -and $flipDsInitialize.Contains('if (FirstStart)') -and
  -not $ayanFlip1.Contains('SetFan') -and -not $ayanFlip1.Contains('EcWrite') -and
  -not $flipDsInitialize.Contains('SetFan') -and -not $flipDsInitialize.Contains('EcWrite')) 'frozen HC Initialize overrides are FirstStart-only and fan-write-free'
Assert-Check ($hostSourceText.Contains('Invoke(device, "Initialize", false, false);')) 'Host handshake uses non-FirstStart HC initialization'

# Frozen HC fan transition: PowerProfileManager calls the virtual device
# callback, then FanProfile owns smoothing/range and SetFanDuty dispatch.
Assert-Check ($device.Contains('SetFanControl(false, profile.OEMPowerMode);') -and $device.Contains('SetFanControl(true, profile.OEMPowerMode);')) 'upstream base hardware/software fan mode transition'
Assert-Check ($powerProfileManager.Contains('IDevice.GetCurrent().PowerProfileManager_Applied(powerProfile, source);')) 'upstream profile callback dispatch'
Assert-Check ($powerProfileManager.Contains('currentProfile.FanProfile.SetTemperature((float)value);') -and $powerProfileManager.Contains('IDevice.GetCurrent().SetFanDuty(fanSpeed);')) 'upstream FanProfile temperature dispatch'
Assert-Check ($fanProfile.Contains('public void SetTemperature(double temp)') -and $fanProfile.Contains('public double GetFanSpeed()')) 'upstream FanProfile contract'

# The only HC profile overrides that add fan policy must remain the pinned
# MSI, Lenovo and ASUS bodies. Their Guid/OEMPowerMode requirements are
# carried by the Host profile clone rather than a synthetic profile.
Assert-Check (([regex]::Matches($allDeviceSource, 'override\s+void\s+PowerProfileManager_Applied\s*\(')).Count -eq 4) 'frozen HC profile override count'
Assert-Check ($msi.Contains('public override void PowerProfileManager_Applied(PowerProfile profile, UpdateSource source)') -and $msi.Contains('if (profile.Guid == BetterBatteryGuid)')) 'upstream MSI profile-Guid policy'
Assert-Check ($lenovo.Contains('currentFanMode != profile.OEMPowerMode') -and $lenovo.Contains('SetSmartFanMode(profile.OEMPowerMode)')) 'upstream Lenovo OEM mode policy'
Assert-Check ($lenovoGo2.Contains('SetFanControl(true);') -and $lenovoGo2.Contains('SetFanDuty(fanPercent);') -and $lenovoGo2.Contains('SetFanControl(false);')) 'upstream Legion Go 2 profile policy'
Assert-Check ($rog.Contains('AsusACPI.SetFanCurve(AsusFan.CPU, defaultCPUFan);') -and $rog.Contains('AsusACPI.SetFanCurve(AsusFan.GPU, defaultGPUFan);')) 'upstream ROG OEM release policy'
Assert-Check (([regex]::Matches($rog, 'AsusACPI\.SetFanCurve\(AsusFan\.(CPU|GPU|Mid), asus\);')).Count -eq 3 -and
  -not $rog.Contains('GetFanCurve(')) 'upstream ROG software apply has exactly three ACPI writes and no readback gate'
# The callback signature carries UpdateSource, but the complete frozen
# device-fan implementation does not branch on it. Background is therefore
# the same fan-operation context that HC itself uses for startup/power-line
# replay, and the Host must keep that fixed rather than inventing a new mode.
$profileOverrideSources = @($msi, $lenovo, $lenovoGo2, $rog)
$profileOverrideSourceCounts = @($profileOverrideSources | ForEach-Object { ([regex]::Matches($_, '\bsource\b')).Count })
Assert-Check ($profileOverrideSourceCounts.Count -eq 4 -and
  (@($profileOverrideSourceCounts | Where-Object { $_ -ne 1 }).Count -eq 0)) 'frozen HC device fan callbacks do not branch on UpdateSource'
$rogCloseStart = $rog.IndexOf('public override void Close()')
$rogCloseEnd = $rog.IndexOf('public override bool IsReady()', $rogCloseStart)
$rogClose = $rog.Substring($rogCloseStart, $rogCloseEnd - $rogCloseStart)
Assert-Check ($rogCloseStart -ge 0 -and $rogCloseEnd -gt $rogCloseStart -and
  $rogClose.IndexOf('AsusACPI.Close();') -ge 0 -and
  $rogClose.IndexOf('base.Close();') -gt $rogClose.IndexOf('AsusACPI.Close();')) 'upstream ROG Close releases ACPI before base fan cleanup; Hardware callback must precede Close'

# YeManHost mirrors HC activation order, but begins it only after its separate
# authorization gate. A failed Open must not invent a Close-side write.
# §2（FAN-926R 裁决）：按**现行生产入口**定位 Host 分段。边界必须**唯一命中**且区间非空；
# 缺失 / 重复 / end<=start 一律报**清晰的测试错误**，绝不把空字符串或"全文尾部"当成合法
# 函数体（旧写法用 IndexOf 的隐式 -1 与 EOF 兜底，正是本次"提取错误"的根因）。
function Get-Boundary([string]$Text, [string]$Token) {
  $hits = ([regex]::Matches($Text, [regex]::Escape($Token))).Count
  if ($hits -lt 1) { throw "fan_hc_enable_alignment: source boundary missing: '$Token'" }
  if ($hits -ne 1) { throw "fan_hc_enable_alignment: source boundary ambiguous ($hits hits): '$Token'" }
  return $Text.IndexOf($Token, [StringComparison]::Ordinal)
}
$hostOpenStart = Get-Boundary $hostSourceText 'private void OpenCore()'
$hostOpenEnd = Get-Boundary $hostSourceText 'public void OpenEvents()'
$hostOpen = $hostSourceText.Substring($hostOpenStart, $hostOpenEnd - $hostOpenStart)
# 旧要求（独立的 `private void OpenEventsCore()`）已被**现行合同取代**：HC OpenEvents 的生产
# 入口现为 `private HcDeviceOpenNeed OpenEventsBeginCore()`，由 `RealHcBackend.OpenEvents()`
# 经 `OnStaBounded` 调用（见 `open-events-begin`）。断言内容不变，只重定位入口。
$hostEventsStart = Get-Boundary $hostSourceText 'private HcDeviceOpenNeed OpenEventsBeginCore()'
$hostEventsEnd = Get-Boundary $hostSourceText 'private void OpenEventsFinalizeCore()'
$hostEvents = $hostSourceText.Substring($hostEventsStart, $hostEventsEnd - $hostEventsStart)
Assert-Check ($hostOpenStart -ge 0 -and $hostOpenEnd -gt $hostOpenStart) 'Host Open boundary present'
Assert-Check (-not $hostOpen.Contains('StartHcDeviceManager();') -and
  $hostOpen.IndexOf('WaitForHcDeviceReadyBeforeOpen();') -ge 0 -and
  $hostOpen.IndexOf('OpenHcDevice();') -gt $hostOpen.IndexOf('WaitForHcDeviceReadyBeforeOpen();') -and
  $hostOpen.IndexOf('CaptureOemBaseline();') -gt $hostOpen.IndexOf('OpenHcDevice();')) 'Host fan-only IsReady -> Open -> baseline order (DeviceManager intentionally isolated)'
# 旧要求（`Invoke` 之后再轮询 `EnsureHcDeviceOpenForRestore()`）已被 920-v1.14 §25 **正式取代**：
# 现合同 = 调用**前**确证 HC 会话/路由就绪 + 调用**后**以正式收据开始设备打开。
# `fan_host_lifecycle_selftest.ts` / `fan_payload_selftest.ps1` 已记录该取代，本条门漏同步。
Assert-Check ($hostEvents.Contains('EnsureHcSessionReadyForOpenEvents();') -and
  $hostEvents.IndexOf('Invoke(device!, "OpenEvents")') -gt $hostEvents.IndexOf('EnsureHcSessionReadyForOpenEvents();') -and
  $hostEvents.Contains('BeginHcDeviceOpenForRestore();') -and
  -not $hostEvents.Contains('StartHcDeviceManager();')) 'Host OpenEvents asserts the HC route handle BEFORE invoking OpenEvents and begins the formal receipt AFTER'
Assert-Check (([regex]::Matches($hostSourceText, 'Invoke\(device!, "IsReady"\)')).Count -eq 1 -and -not $hostSourceText.Contains('FAN_NOT_READY')) 'Host has exactly one HC IsReady probe after DeviceManager startup, with no invented readiness rejection'
Assert-Check (($hostSourceText.Contains('if (IsOpen)') -or
  $hostSourceText.Contains('IsOpen || (openAttempted && hcOpenInvocationStarted)')) -and
  -not $hostSourceText.Contains('StopHcDeviceManager();') -and
  $hostSourceText.Contains('not-started/no-stop-required')) 'failed Host Open does not manufacture a DeviceManager stop in the fan-only process'
Assert-Check ($hostOpen.Contains('openAttempted = false;') -and
  $hostOpen.Contains('oemBaselineCaptured = false;') -and
  $hostSourceText.Contains("A failed HC Open() is not an active fan session") -and
  $hostSourceText.Contains('MainWindow simply stops before OpenEvents') -and
  $hostSourceText.Contains('if (realBackend.IsOpen || realBackend.OpenAttempted)') -and
  $hostSourceText.Contains('state.OpenCalled = false;') -and
  $hostSourceText.Contains('state.State = "AwaitingControl";')) 'failed HC Open clears the Host session boundary and cannot enter fabricated restore'
Assert-Check (-not $hostSourceText.Contains('foreach (IManager manager in ManagerFactory.Managers)') -and
  -not $hostSourceText.Contains('powerProfileManager.Start') -and
  -not $hostSourceText.Contains('profileManager.Start')) 'Host isolates HC full power/TDP manager graph'
Assert-Check ($hostEvents.Contains('Invoke(device!, "OpenEvents")') -and
  $hostEvents.Contains('EnsureHcSessionReadyForOpenEvents();') -and
  -not $hostEvents.Contains('AssertHcNonFanManagersIsolated();')) 'Host keeps OpenEvents on the leased HC device boundary without starting non-fan managers'
Assert-Check ($hostSourceText.Contains('private void EnsureHcSessionReadyForOpenEvents()') -and
  -not $hostSourceText.Contains('HC_DEVICE_NOT_OPEN_FOR_RESTORE') -and
  $hostSourceText.Contains('hc-device-open-unconfirmed')) 'ROG restore cannot be acknowledged before its HC IsOpen/HID boundary is real'

# Non-temperature writes are exactly one HC device profile callback. The one
# SetFanDuty dispatch is the explicitly allowed temperature source boundary.
$forbiddenWriters = @(
  'WriteEcByte', 'ECRamDirectWriteByte', 'WriteMsiWmiFanTable',
  'WriteMsiWmiData', 'ApplyMsiFanCurve', 'ApplyMsiHcDefaultRelease',
  'ApplyLenovoFanCurve', 'ApplyLenovoHcDefaultTable',
  'ApplyLegionGo2FanCurve', 'ApplyLegionGo2OemRelease',
  'ApplySmartFanModeBaseline', 'InvokeSetFanControl'
)
Assert-Check ((@($forbiddenWriters | Where-Object { $hostSourceText.Contains($_) }).Count -eq 0)) 'no Host-side vendor fan writer'
Assert-Check (([regex]::Matches($hostSourceText, 'Invoke\("PowerProfileManager_Applied"')).Count -eq 1) 'single HC profile callback write entry'
Assert-Check ($hostSourceText -match 'Enum\.Parse\(updateType,\s*"Background"(?:,\s*ignoreCase:\s*false)?\)') 'Host uses HC Background callback context for the source-independent device fan callback'
Assert-Check (([regex]::Matches($hostSourceText, 'Invoke\("SetFanDuty"')).Count -eq 1 -and $hostSourceText.Contains('Invoke(activeFanProfile, "SetTemperature", temp)')) 'single allowed HC temperature dispatch'
Assert-Check ($hostSourceText.Contains('CaptureHcProfileTemplate();') -and
  ($hostSourceText.Contains('CloneHcPowerProfilePreservingFanState(hcProfileTemplate)') -or
   $hostSourceText.Contains('CloneHcPowerProfile(hcProfileTemplate)') -or
   $hostSourceText.Contains('hcProfileTemplate = CloneHcPowerProfilePreservingFanState(selected)')) -and
  $hostSourceText.Contains('baselineMsiShiftValue') -and $hostSourceText.Contains('baselineSmartFanMode')) 'Host preserves HC profile identity for MSI and Lenovo'
Assert-Check ($hostSourceText.Contains('Invoke(fan, "SetFanSpeed", duties);') -and -not $hostSourceText.Contains('Activator.CreateInstance(fanType, duties, 1)')) 'Host mutates the cloned HC FanProfile through its own curve validation instead of constructing a parallel profile'
Assert-Check ($hostSourceText.Contains('ApplyPowerProfile(BuildPowerProfile(Array.Empty<double>(), software: false));') -and
  $hostSourceText.Contains('Invoke(device!, "Close")') -and
  $hostSourceText.Contains('if (close)')) 'Host keeps HC Hardware profile handoff separate from HC virtual Close'
Assert-Check ($hostSourceText.Contains('public bool HcVirtualCloseReturned { get; set; }') -and
  $hostSourceText.Contains('public bool HcDeviceManagerStopCompleted { get; set; }') -and
  $hostSourceText.Contains('public bool OemPhysicalOwnershipConfirmed { get; set; }') -and
  $hostSourceText.Contains('HcVirtualCloseReturned = true;') -and
  $hostSourceText.Contains('HcDeviceManagerStopCompleted = true;') -and
  $hostSourceText.Contains('hc-callback-only-physical-unknown') -and
  $hostSourceText.Contains('hc-default-table-readback-confirmed')) 'Host separates HC Close/manager completion from physical OEM evidence'
Assert-Check (($managerFactory.Contains('processManager = new() { SuspendWithOS = true };') -or
  ($hostSourceText.Contains('private const string ManagerFactoryNotStarted = "not-started/no-stop-required";') -and
   $hostSourceText.Contains('ResetManagerLifecycle();'))) -and
  -not $managerFactory.Contains('deviceManager = new() { SuspendWithOS = true }') -and
  $hostSourceText.Contains('private bool CloseForSystemPending()') -and
  # ★ 因 FAN-926R 裁决 §3-D1 **改要求**（明示取代，不是"仅更新符号/证明行为未变"）：
  # 旧要求 = SystemPending 一律 `skipOemRestore: true` + `clearOemEvidence: true`（禁止默认表预写）。
  # 新要求 = 有写历史且会话可操作时，**先**在同一生命周期 owner 内有界执行一次现成默认模式恢复，
  # 再进入 HC Close；只有会话不可操作时才 not-attempted；证据不再清除（D3 三轴分列）。
  # 关闭边界仍必须进入、仍不得夺取 DeviceManager 所有权。
  $hostSourceText.Contains('power.suspend-preclose-release-decision') -and
  $hostSourceText.Contains('skipOemRestore: !preCloseReleaseAttempted') -and
  $hostSourceText.Contains('clearOemEvidence: false') -and
  $hostSourceText.Contains('not-attempted-session-not-usable') -and
  -not $hostSourceText.Contains('RestoreHardware(close: true, stopDeviceManager: false)') -and
  ($hostSourceText.Contains('hc-close.device-manager-not-started') -or $hostSourceText.Contains('ManagerFactoryNotStarted')) -and
  $hostSourceText.Contains('realBackend.Close(stopDeviceManager)') -and
  $hostSourceText.Contains('if (closed && !stopDeviceManager)')) 'F4 SystemPending enters the HC virtual Close boundary without DeviceManager ownership; D1: a usable session performs ONE bounded default-mode restore BEFORE Close'

# A device Open establishes only the HC transport/session. It must not be
# reported as an active fan write. Conversely, every accepted software curve
# must invalidate an old OEM acknowledgement before it enters the HC callback.
$engineOpenStart = $hostSourceText.IndexOf('public object Open()')
$engineOpenEnd = $hostSourceText.IndexOf("`n    public object OpenEvents()", $engineOpenStart)
$engineOpen = $hostSourceText.Substring($engineOpenStart, $engineOpenEnd - $engineOpenStart)
$engineEnableStart = $hostSourceText.IndexOf('public object Enable(JsonElement body)')
$engineEnableEnd = $hostSourceText.IndexOf("`n    public object Preset(JsonElement body)", $engineEnableStart)
$engineEnable = $hostSourceText.Substring($engineEnableStart, $engineEnableEnd - $engineEnableStart)
Assert-Check (-not $engineOpen.Contains('state.HardwareWritesEnabled = true;') -and
  $engineEnable.IndexOf('state.HardwareWritesObserved = true;') -ge 0 -and
  $engineEnable.IndexOf('state.HardwareWritesEnabled = true;') -gt $engineEnable.IndexOf('state.HardwareWritesObserved = true;') -and
  $engineEnable.IndexOf('state.OemRestoreConfirmed = false;') -gt $engineEnable.IndexOf('state.HardwareWritesObserved = true;')) 'only an HC software curve enters active write state and invalidates prior OEM proof'
Assert-Check ($engineOpen.Contains('state.OemRestoreConfirmed = false;') -and
  $engineOpen.Contains('state.OemRestoreEvidence = "none";') -and
  $engineOpen.Contains('state.HardwareWritesEnabled || state.Lease is not null')) 'a fresh HC Open cannot reuse prior OEM proof or overlap an active session'

# HC's Hardware profile callback is the profile-handoff boundary. GetFanCurve
# is optional diagnostic telemetry only: HC itself does not define a
# cross-firmware readback ACK, and ROG/Xbox firmware may expose a table that
# differs from HC's compiled defaults. Sleep/process close must go directly
# through the device's virtual Close() and must not be blocked by that
# optional observation.
$hostRestoreStart = $hostSourceText.IndexOf('private void RestoreOemCore()')
$hostRestoreEnd = $hostSourceText.IndexOf("`n    private bool IsRouteStillReady()", $hostRestoreStart)
$hostRestore = $hostSourceText.Substring($hostRestoreStart, $hostRestoreEnd - $hostRestoreStart)
# R1（FAN-926R 唤醒租约裁决 §3.2.9）**因本裁决改要求**：`RestoreHardware` 增加了
# `string cause = "unspecified"` 形参（用于分清"租约到期 / 幂等交还 / 关闭边界"等释放来源，
# 让"是否由占位租约触发"可判读）。断言内容不变，只把边界锚点同步到现行签名；
# 仍要求边界**唯一命中且区间非空**，缺失/重复一律报清晰测试错误。
$engineRestoreStart = $hostSourceText.IndexOf('private bool RestoreHardware(bool close, bool stopDeviceManager = true, bool skipOemRestore = false, string cause = "unspecified")')
if ($engineRestoreStart -lt 0 -or
    $hostSourceText.IndexOf('private bool RestoreHardware(', $engineRestoreStart + 1) -ge 0) {
  throw 'fan_hc_enable_alignment: RestoreHardware(bool,bool,bool,string) 生产入口必须恰好存在一处'
}
$engineRestoreEnd = $hostSourceText.IndexOf("`n    private void ClearLease()", $engineRestoreStart)
if ($engineRestoreEnd -le $engineRestoreStart) {
  throw 'fan_hc_enable_alignment: RestoreHardware 边界定位失败（end<=start 或锚点缺失）'
}
$engineRestore = $hostSourceText.Substring($engineRestoreStart, $engineRestoreEnd - $engineRestoreStart)
Assert-Check ($hostRestoreStart -ge 0 -and $hostRestoreEnd -gt $hostRestoreStart -and
  $hostRestore.IndexOf('ApplyPowerProfile(BuildPowerProfile(Array.Empty<double>(), software: false));') -ge 0 -and
  $hostRestore.IndexOf('MarkHcOemReleaseCallbackCompleted();') -gt $hostRestore.IndexOf('ApplyPowerProfile(BuildPowerProfile(Array.Empty<double>(), software: false));') -and
  $hostRestore.IndexOf('ConfirmOemRestore()') -gt $hostRestore.IndexOf('ApplyPowerProfile(BuildPowerProfile(Array.Empty<double>(), software: false));') -and
  $hostRestore.Contains('ConfirmAsusOemReadback(out var restoreDetail)') -and
  $hostSourceText.Contains('TryReadAsusDefaults') -and
  -not $hostSourceText.Contains('TryWriteAsusDefaultsDirect') -and
  -not $hostRestore.Contains('ECRamDirectWriteByte')) 'Host OEM restore uses HC Hardware callback; ASUS readback is diagnostic-only'
Assert-Check ($hostSourceText.Contains('InvokeStaticMember(acpi, "GetFanCurve"') -and
  $hostSourceText.Contains('restore.asus-default-readback-unconfirmed') -and
  -not $hostSourceText.Contains('restore.asus-default-direct-fallback') -and
  -not $hostSourceText.Contains('CaptureAsusBaseline') -and
  -not $hostSourceText.Contains('ConfirmAppliedAsusCurve') -and
   -not $hostSourceText.Contains('AreAsusCurves')) 'Host records one HC GetFanCurve diagnostic without adding a vendor fallback gate'
$closeSessionIndex = $engineRestore.IndexOf('CloseHcSessionForLifecycle(stopDeviceManager, skipOemRestore)')
if ($closeSessionIndex -lt 0) { $closeSessionIndex = $engineRestore.IndexOf('CloseHcSessionForLifecycle(stopDeviceManager)') }
Assert-Check ($engineRestoreStart -ge 0 -and $engineRestoreEnd -gt $engineRestoreStart -and
  $closeSessionIndex -ge 0 -and
  $engineRestore.IndexOf('if (close)') -ge 0 -and
  $closeSessionIndex -gt $engineRestore.IndexOf('if (close)') -and
  $engineRestore.IndexOf('realBackend.RestoreOem();') -ge 0 -and
  $engineRestore.IndexOf('realBackend.RestoreOem();') -gt $closeSessionIndex) 'profile handoff uses HC Hardware; sleep/process close uses direct HC virtual Close'
Assert-Check (([regex]::Matches($engineRestore, 'state\.HcCloseCleanupPending = true;')).Count -ge 1 -and
  (([regex]::Matches($engineRestore, 'return false;')).Count -ge 2)) 'any HC Close exception remains pending and cannot be reported as a successful stopped boundary'
Assert-Check ($hostSourceText.Contains('TimedOutOperationReturned?.Invoke(workItem.Failure is null)') -and
  $hostSourceText.Contains('private void OnTimedOutHcOperationReturned(bool operationSucceeded)') -and
  $hostSourceText.Contains('if (!operationSucceeded && state.OemRestoreConfirmed && !state.HardwareWritesEnabled)') -and
  $hostSourceText.Contains('var completedHcClose =') -and
  $hostSourceText.Contains('(completedHcClose || backend.OemRestoreVerified)') -and
  $hostSourceText.Contains('hc.timeout-returned-after-completed-boundary') -and
  $hostSourceText.Contains('RecoveryRetryWindowMs = 60_000') -and
  $hostSourceText.Contains('RecoveryRetryMaxAttempt = 30') -and
  $hostSourceText.Contains('recovery.retry-scheduled')) 'a timed-out HC call is acknowledged only after its HC callback or complete terminal Close boundary returns normally; Close exceptions remain retryable without a busy retry loop'
Assert-Check ($engineRestore.Contains('var hardwareAlreadyReleased = HasConfirmedOemRelease(state.OemRestoreConfirmed, state.HardwareWritesEnabled);') -and
  $engineRestore.Contains('diagnostics.Write("restore.already-confirmed"') -and
  ($engineRestore.Contains('CloseHcSessionForLifecycle(stopDeviceManager)') -or
   $engineRestore.Contains('CloseHcSessionForLifecycle(stopDeviceManager, skipOemRestore)'))) 'restore -> release keeps HC Hardware handoff separate from the virtual HC Close boundary'
Assert-Check ($hostSourceText.Contains('StopCpuTemperatureMonitor();') -and
  $hostSourceText.Contains('CloseHcDevice();') -and
  $hostSourceText.Contains('Invoke(device!, "Close")') -and
  ($hostSourceText.Contains('hcDeviceManagerLifecycle = "not-started/no-stop-required";') -or
   $hostSourceText.Contains('private const string ManagerFactoryNotStarted = "not-started/no-stop-required";')) -and
  -not $hostSourceText.Contains('WaitForProcessCloseOrder();') -and
  -not $hostSourceText.Contains('manager-initialization-timeout')) 'process close stops monitoring before the single HC virtual Close and preserves failed cleanup state'
Assert-Check ($fanApiSource.Contains("if (path === '/api/close') return 45000;") -and
  $fanHostSource.Contains('timeoutMs: 45000') -and
  $fanHostSource.Contains('Match HC Window_Closed: process exit owns one virtual Close')) 'all frontend close transports retain one HC Close owner'
Assert-Check ($fanHostSource.Contains('hcVirtualCloseReturned') -and
  $fanHostSource.Contains('hcDeviceManagerStopCompleted') -and
  $fanHostSource.Contains('DeviceManager')) 'frontend close requires explicit HC virtual Close and DeviceManager evidence when provided'
Assert-Check ($fanHostSource.Contains('function assertHcSessionClosed') -and
  $fanHostSource.Contains('lifecycle.suspend-begin') -and
  # Keep this source assertion ASCII-stable across Windows PowerShell code
  # pages; the implementation itself still emits the localized diagnostic.
  $fanHostSource.Contains('hcVirtualCloseReturned') -and
  $fanHostSource.Contains('async suspend')) 'frontend suspend requires HC virtual Close evidence while retaining DeviceManager'
Assert-Check ($hostSourceText.Contains('power.suspend-ignored-duplicate') -and
  $hostSourceText.Contains('state.State == "Suspending"') -and
  $hostSourceText.Contains('state.State == "Suspended"') -and
  $hostSourceText.Contains('state.HcCloseCleanupPending')) 'duplicate native/Host suspend notifications are read-only and cannot invoke HC Close twice'

# Parent watchdog identity is part of the lifecycle boundary: PID reuse or an
# ambiguous same-name process must trigger recovery, never keep an old HC
# session alive under a new process.
# FAN-931 P1.3: the watcher now binds one trusted parent instance and waits for
# its exit instead of reopening the handle every 2 s, so the identity comparison
# lives in TryBindParentProcess. The intent is unchanged and asserted by its
# three distinct rejection reasons (never by "the old function still exists").
Assert-Check ($hostSourceText.Contains('parentStartTimeUtc') -and
  $hostSourceText.Contains('parentExecutablePath') -and
  $hostSourceText.Contains('CaptureParentIdentity') -and
  $hostSourceText.Contains('CaptureNamedParentIdentity') -and
  $hostSourceText.Contains('GetUniqueNamedProcess') -and
  $hostSourceText.Contains('private (Process? Process, string Reason) TryBindParentProcess()') -and
  $hostSourceText.Contains('parent-start-time-mismatch') -and
  $hostSourceText.Contains('parent-image-mismatch') -and
  $hostSourceText.Contains('parent-not-uniquely-resolvable') -and
  $hostSourceText.Contains('Ambiguity is a') -and
  $hostSourceText.Contains('return null;')) 'parent watchdog rejects PID reuse and ambiguous name-only identity'
Assert-Check ($engineRestore.Contains('var backendSessionActive = realBackend?.IsOpen == true || realBackend?.OpenAttempted == true;') -and
  $engineRestore.Contains('(!state.OpenCalled && !backendSessionActive)')) 'OEM restore cannot be skipped when the reflected state flag disagrees with the live HC backend session'
Assert-Check ($hostSourceText.Contains('HOST_EVENTS_NOT_OPEN') -and
  $hostSourceText.Contains('if (realBackend is not null && (!state.OpenCalled || !state.OpenEventsCalled))')) 'lease admission cannot precede HC OpenEvents'

# Repeated capability probes are read-only. A listener restart or a UI page
# revisit must not clear the live/history bits of an already-open HC session;
# otherwise native /api/close could falsely accept an active ACPI/HID owner as
# idle. A failed probe while a session is live must remain fault-locked and
# enter the same serialized recovery path as other HC failures.
$handshakeStart = $hostSourceText.IndexOf('public object Handshake()')
$handshakeEnd = $hostSourceText.IndexOf("`n    public object Open()", $handshakeStart)
$handshake = $hostSourceText.Substring($handshakeStart, $handshakeEnd - $handshakeStart)
Assert-Check ($handshakeStart -ge 0 -and $handshakeEnd -gt $handshakeStart -and
  $handshake.Contains('var liveSessionBeforeHandshake = HasLiveHardwareSession();') -and
  $handshake.Contains('if (!liveSessionBeforeHandshake)') -and
  $handshake.Contains('hardwareWritesEnabled = state.HardwareWritesEnabled') -and
  $handshake.Contains('hardwareWritesObserved = state.HardwareWritesObserved') -and
  $handshake.Contains('state.UnknownState = true;') -and
  $handshake.Contains('state.State = "FaultLocked";') -and
  $handshake.Contains('ScheduleRecoveryRetry();')) 'repeated handshake preserves active HC evidence and fault-locks failed live probes'

# HC does not expose a common vendor readback contract, so every mapped
# factory route remains write-ready after the normal authorization/handshake.
Assert-Check ($hostSourceText.Contains('kind, description, true, true, strategy') -and
  $hostSourceText.Contains('GPDWinMini') -and
  -not $hostSourceText.Contains('no OEM release contract')) 'all mapped HC routes remain write-ready without a Host readback gate'

# HC offers no cross-vendor hardware ownership acknowledgement.  MSI and
# Lenovo use their own best-effort reads inside their callback bodies and do
# not make those reads an admission, active-curve or release condition.  The
# Host may preserve that optional profile context, but must never turn it into
# a fabricated route-conflict lockout.
$verifySessionStart = $hostSourceText.IndexOf('private void VerifyActiveCurveSessionCore()')
$verifySessionEnd = $hostSourceText.IndexOf("`n    public void RestoreOem()", $verifySessionStart)
$verifySession = $hostSourceText.Substring($verifySessionStart, $verifySessionEnd - $verifySessionStart)
$obsoleteReadbackGates = @(
  'CaptureGenericEcBaseline', 'ConfirmAppliedGeneric', 'ConfirmAppliedMsiCurve',
  'ConfirmAppliedLenovoCurve', 'AreMsiCurves', 'AreLenovoCurves',
  'FAN_ROUTE_CONFLICT'
)
Assert-Check ($verifySessionStart -ge 0 -and $verifySessionEnd -gt $verifySessionStart -and
  ($verifySession.Contains('if (!IsOpen || !oemBaselineCaptured || !IsRouteStillReady())') -or
   $verifySession.Contains('EnsureHcSessionReadyForControl();')) -and
  -not $verifySession.Contains('GetFan') -and -not $verifySession.Contains('GetSmartFanMode') -and
  -not $verifySession.Contains('GetShiftValue') -and
  ((@($obsoleteReadbackGates | Where-Object { $hostSourceText.Contains($_) }).Count) -eq 0)) 'active lease verifies only the HC session, never an invented vendor ownership/readback contract'

# YeMan intentionally supplies CPU temperature outside HC's full manager
# graph. A stale input stops scheduling and hands control back through the HC
# Hardware callback; only a real fan-dispatch exception or failed handoff may
# enter FaultLocked. This preserves HC fan-route semantics while keeping the
# isolated temperature source fail-safe.
$staleHandlerStart = $hostSourceText.IndexOf('private void OnBackendTemperatureMonitorStale()')
$staleHandlerEnd = $hostSourceText.IndexOf("`n    internal void MarkOperationTimeoutForSelfTest()", $staleHandlerStart)
$staleHandler = $hostSourceText.Substring($staleHandlerStart, $staleHandlerEnd - $staleHandlerStart)
Assert-Check ($hostSourceText.Contains('public Action<Exception>? FanDispatchFailure { get; set; }') -and
  $hostSourceText.Contains('public Action? TemperatureMonitorStale { get; set; }') -and
  $hostSourceText.Contains('TemperatureMonitorStale?.Invoke()') -and
  $hostSourceText.Contains('FanDispatchFailure?.Invoke(ex)') -and
  $hostSourceText.Contains('realBackend.TemperatureMonitorStale = OnBackendTemperatureMonitorStale;') -and
  $staleHandlerStart -ge 0 -and $staleHandlerEnd -gt $staleHandlerStart -and
  $staleHandler.IndexOf('RestoreHardware(close: false)') -ge 0 -and
  $staleHandler.IndexOf('state.State = "Ready";') -gt $staleHandler.IndexOf('RestoreHardware(close: false)') -and
  $staleHandler.IndexOf('state.UnknownState = false;') -gt $staleHandler.IndexOf('RestoreHardware(close: false)') -and
  $staleHandler.IndexOf('FaultLocked') -gt $staleHandler.IndexOf('return;')) 'a stale isolated temperature sample restores OEM and remains resumable; only failed recovery fault-locks'

Write-Output 'fan HC enable source-path self-test: PASS (source-only; full ManagerFactory equivalence remains needs-investigation; hardwareWrites=false)'
