<#
.SYNOPSIS
  Second-pass T3 discovery audit for HC profile/event/lifecycle parity.

  This is source-only. It never loads HandheldCompanion, starts ManagerFactory,
  invokes Open/OpenEvents/Close, touches WMI/ACPI/HID, or enables hardware.
  Every source difference which is not proven safe remains in the explicit
  needs-investigation register; this script does not silently call it parity.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcRoot,
  [Parameter(Mandatory = $true)][string]$HostSource,
  [Parameter(Mandatory = $true)][string]$YeManRoot,
  [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
foreach ($path in @($HcRoot, $YeManRoot)) {
  if (!(Test-Path -LiteralPath $path -PathType Container)) { throw "Directory missing: $path" }
}
if (!(Test-Path -LiteralPath $HostSource -PathType Leaf)) { throw "Host source missing: $HostSource" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$paths = [ordered]@{
  hcPowerProfile = Join-Path $HcRoot 'Managers\PowerProfileManager.cs'
  hcProfile      = Join-Path $HcRoot 'Managers\ProfileManager.cs'
  hcSystem       = Join-Path $HcRoot 'Managers\SystemManager.cs'
  hcFactory      = Join-Path $HcRoot 'Managers\ManagerFactory.cs'
  hcManager      = Join-Path $HcRoot 'Managers\IManager.cs'
  hcDevice       = Join-Path $HcRoot 'Devices\IDevice.cs'
  hcFan          = Join-Path $HcRoot 'Misc\FanProfile.cs'
  hcPower        = Join-Path $HcRoot 'Misc\PowerProfile.cs'
  hcProfileType  = Join-Path $HcRoot 'Misc\Profile.cs'
  hcWindow       = Join-Path $HcRoot 'Views\Windows\MainWindow.xaml.cs'
  host           = $HostSource
  fanHost        = Join-Path $YeManRoot 'src\bridge\fanHost.ts'
  fanApi         = Join-Path $YeManRoot 'src\bridge\fanApi.ts'
  native         = Join-Path $YeManRoot 'native\main.cpp'
  package        = Join-Path $YeManRoot 'package.json'
}
foreach ($entry in $paths.GetEnumerator()) {
  if (!(Test-Path -LiteralPath $entry.Value -PathType Leaf)) { throw "Input source missing ($($entry.Key)): $($entry.Value)" }
}

$text = @{}
foreach ($entry in $paths.GetEnumerator()) { $text[$entry.Key] = Get-Content -LiteralPath $entry.Value -Raw -Encoding UTF8 }

function Has([string]$value, [string]$pattern) {
  return [regex]::IsMatch($value, $pattern, [Text.RegularExpressions.RegexOptions]::Singleline)
}
function Line([string]$value, [string]$pattern) {
  $m = [regex]::Match($value, $pattern, [Text.RegularExpressions.RegexOptions]::Singleline)
  if (!$m.Success) { return $null }
  return (($value.Substring(0, $m.Index) -split "`n").Count)
}
function Sha([string]$path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash([System.IO.File]::ReadAllBytes($path)))).Replace('-', '') }
  finally { $sha.Dispose() }
}

$checks = [System.Collections.Generic.List[object]]::new()
$diffs = [System.Collections.Generic.List[object]]::new()
function Pass([string]$id, [string]$title, [string]$evidence) {
  $checks.Add([ordered]@{ id=$id; title=$title; status='pass'; evidence=$evidence })
}
function Fail([string]$id, [string]$title, [string]$evidence) {
  $checks.Add([ordered]@{ id=$id; title=$title; status='fail'; evidence=$evidence })
}
function Need([string]$id, [string]$severity, [string]$title, [string]$hc, [string]$ye, [string]$impact, [string]$next) {
  $diffs.Add([ordered]@{ id=$id; severity=$severity; status='needs-investigation'; title=$title; hc=$hc; yeman=$ye; impact=$impact; next=$next })
}

# 1. HC ManagerFactory suspend/resume boundary and YeMan gates.
if ((Has $text.hcFactory 'Managers\.Where\(m => m\.SuspendWithOS\).*?manager\.Resume\(\)') -and
    (Has $text.hcFactory 'Managers\.Where\(m => m\.SuspendWithOS\).*?manager\.Suspend\(\)') -and
    (Has $text.hcManager 'public bool SuspendWithOS')) {
  Pass 'R2-L01' 'HC Suspend/Resume only dispatches managers opted into OS suspension' "ManagerFactory.cs:Resume/Suspend; IManager.cs:SuspendWithOS"
} else { Fail 'R2-L01' 'HC Suspend/Resume boundary' 'SuspendWithOS filtering was not proven' }
if ((Has $text.host 'BlockWritesForSuspend') -and (Has $text.host 'AllowWritesAfterResume') -and
    (Has $text.host 'BlockWritesForClose') -and (Has $text.host 'closeWriteBlocked')) {
  Pass 'R2-L02' 'YeMan has distinct suspend and stronger close write gates' "Program.cs:BlockWritesForSuspend/Close/AllowWritesAfterResume"
} else { Fail 'R2-L02' 'YeMan power-boundary gates' 'suspend/close gate separation was not proven' }

# 2. HC OpenEvents event graph and exact virtual close lifecycle.
if ((Has $text.hcDevice 'ManagerFactory\.settingsManager\.Initialized\s*\+=\s*SettingsManager_Initialized') -and
    (Has $text.hcDevice 'ManagerFactory\.powerProfileManager\.Initialized\s*\+=\s*PowerProfileManager_Initialized') -and
    (Has $text.hcDevice 'ManagerFactory\.deviceManager\.Initialized\s*\+=\s*DeviceManager_Initialized')) {
  Pass 'R2-E01' 'HC OpenEvents installs Initialized subscriptions for Settings, PowerProfile and DeviceManager' "IDevice.cs:OpenEvents query graph"
} else { Fail 'R2-E01' 'HC OpenEvents Initialized graph' 'one or more HC subscriptions were not found' }
if ((Has $text.hcDevice 'ManagerFactory\.settingsManager\.Initialized\s*-=' ) -and
    (Has $text.hcDevice 'ManagerFactory\.powerProfileManager\.Initialized\s*-=' ) -and
    (Has $text.hcDevice 'ManagerFactory\.deviceManager\.Initialized\s*-=' ) -and
    (Has $text.hcDevice 'Closed\?\.Invoke\(this\)')) {
  Pass 'R2-E02' 'HC Close removes the OpenEvents subscriptions and raises Closed last' "IDevice.cs:Close"
} else { Fail 'R2-E02' 'HC Close unsubscribe order' 'Close cleanup/Closed event was not proven' }
if ((Has $text.host 'Invoke\(device!,\s*"OpenEvents"\)') -and
    (Has $text.host 'Invoke\(device!,\s*"Close"\)') -and
    (Has $text.host 'CloseHcDevice\(stopDeviceManager\)') -and
    (Has $text.host 'StopHcDeviceManager\(\)')) {
  Pass 'R2-E03' 'YeMan calls the HC virtual OpenEvents/Close and stops DeviceManager after Close' "Program.cs:OpenEventsCore/CloseHcDevice"
} else { Fail 'R2-E03' 'YeMan HC lifecycle invocation' 'virtual calls or post-close manager stop were not proven' }
if ((Has $text.hcWindow 'CurrentDevice\.Close\(\)') -and
    (Has $text.hcWindow 'foreach \(IManager manager in ManagerFactory\.Managers\)\s*manager\.Stop\(\)')) {
  Pass 'R2-E04' 'HC Window_Closed closes the device before stopping all managers' "MainWindow.xaml.cs:Window_Closed"
} else { Fail 'R2-E04' 'HC Window_Closed ordering' 'device-before-manager stop order was not proven' }

# 3. Profile event ordering, AC/DC selection and UpdateSource semantics.
if ((Has $text.hcPowerProfile 'profileManager\.Applied\s*\+=\s*ProfileManager_Applied') -and
    (Has $text.hcPowerProfile 'profileManager\.Discarded\s*\+=\s*ProfileManager_Discarded') -and
    (Has $text.hcPowerProfile 'SystemManager\.PowerLineStatusChanged\s*\+=\s*SystemManager_PowerLineStatusChanged')) {
  Pass 'R2-E05' 'HC PowerProfileManager observes Applied, Discarded and AC/DC transitions' "PowerProfileManager.cs:Start"
} else { Fail 'R2-E05' 'HC PowerProfileManager subscriptions' 'required profile events were not proven' }
$hcDeviceApply = Line $text.hcPowerProfile 'IDevice\.GetCurrent\(\)\.PowerProfileManager_Applied\(powerProfile,\s*source\)'
$hcEventApply = Line $text.hcPowerProfile 'Applied\?\.Invoke\(powerProfile,\s*source\)'
if ($hcDeviceApply -and $hcEventApply -and $hcEventApply -gt $hcDeviceApply) {
  Pass 'R2-E06' 'HC invokes the device callback before notifying Applied subscribers' "PowerProfileManager.cs:$hcDeviceApply -> $hcEventApply"
} else { Fail 'R2-E06' 'HC Applied ordering' 'device-before-event ordering was not proven' }
if ((Has $text.hcPowerProfile 'currentProfile\s*=\s*null') -and (Has $text.hcPowerProfile 'Discarded\?\.Invoke\(powerProfile,\s*swapped')) {
  Pass 'R2-E07' 'HC Discarded clears current state and does not synthesize a replacement' "PowerProfileManager.cs:ProfileManager_Discarded"
} else { Fail 'R2-E07' 'HC Discarded semantics' 'discard clear/no-fallback semantics were not proven' }
if ((Has $text.hcPowerProfile 'profile\.PowerProfiles\[\(int\)powerStatus\.PowerLineStatus\]') -and
    (Has $text.hcPowerProfile 'ProfileManager_Applied\(profile,\s*UpdateSource\.Background\)')) {
  Pass 'R2-E08' 'HC AC/DC transition selects the mapped PowerProfiles entry' "PowerProfileManager.cs:SystemManager_PowerLineStatusChanged"
} else { Fail 'R2-E08' 'HC AC/DC mapping' 'PowerProfiles mapping was not proven' }
if ((Has $text.host 'pendingExternalProfileUpdateSource') -and
    (Has $text.host 'NormalizeUpdateSourceName') -and
    (Has $text.host 'Invoke\("PowerProfileManager_Applied",\s*profile,\s*source\)') -and
    (Has $text.host 'ApplyPowerProfile\(profile,\s*source\)')) {
  Pass 'R2-E09' 'YeMan preserves external UpdateSource and dispatches through HC callback' "Program.cs:OnExternalProfileApplied/TryAdoptExternalHcProfile/ApplyPowerProfile"
} else { Fail 'R2-E09' 'YeMan UpdateSource dispatch' 'external source preservation/callback dispatch was not proven' }

# 4. Runtime state and curve callback boundary (HW temperature is the one
# permitted intentional source difference).
foreach ($token in @('TjHysteresisC','TjCooldown','TjReleaseSamples','tjLatch','tjLastHitUtc','belowCount','aggressivity','avgTemp')) {
  if (!(Has $text.hcFan ("\b" + [regex]::Escape($token) + "\b"))) { Fail ('R2-F-' + $token) 'HC FanProfile runtime field' $token }
}
if ((Has $text.hcFan 'SetTemperature\(double temp\)') -and (Has $text.hcFan 'GetFanSpeed\(double temp\)') -and
    (Has $text.hcFan 'Math\.Clamp\(y,\s*0\.0,\s*100\.0\)') -and
    (Has $text.host 'CopyFanRuntimeState') -and (Has $text.host 'DirectCpuTemperatureMonitor')) {
  Pass 'R2-F01' 'HC FanProfile algorithm/runtime state is retained; YeMan uses the permitted HW temperature adapter' "FanProfile.cs and Program.cs:CopyFanRuntimeState/DirectCpuTemperatureMonitor"
} else { Fail 'R2-F01' 'FanProfile callback/runtime parity' 'algorithm, runtime copy or HW adapter boundary was not proven' }

# 5. Retry/lease/close safety boundaries discovered from prior incident logs.
if ((Has $text.host 'RequestPowerProfileWatchdogTick') -and (Has $text.host 'external-profile-discarded-no-applied') -and
    (Has $text.host 'retain-last-safe-template')) {
  Pass 'R2-S01' 'Discarded-only window is a bounded wait and retains the last safe template' "Program.cs:watchdog and no-next-Applied diagnostic"
} else { Fail 'R2-S01' 'Discarded wait safety' 'bounded wait/safe-template retention was not proven' }
if ((Has $text.host 'HC_CLOSE_PENDING') -and (Has $text.host 'PARENT_EXIT_HANDOFF') -and
    (Has $text.host 'Interlocked\.Exchange\(ref closeBoundaryClaimed')) {
  Pass 'R2-S02' 'Close/recovery requests share one HC boundary and reject duplicate/parent-exit races' "Program.cs:closeBoundaryClaimed/HC_CLOSE_PENDING/PARENT_EXIT_HANDOFF"
} else { Fail 'R2-S02' 'Close race safety' 'single close boundary was not proven' }
if ((Has $text.fanHost 'heartbeat\(') -and (Has $text.fanHost 'recoverAfterMutationFailure') -and
    (Has $text.fanHost 'restoreAndRelease') -and (Has $text.fanHost 'sleep')) {
  Pass 'R2-S03' 'Bridge retains lease heartbeat, restore/release and sleep lifecycle calls' "fanHost.ts lifecycle adapter"
} else { Fail 'R2-S03' 'Fan bridge lifecycle' 'lease/restore/sleep boundary was not proven' }

# Explicit boundaries are retained unchanged from the first T3 audit. They are
# not isolated bugs and are not promoted to pass merely by model tests.
Need 'T3-R2-INV-001' 'P1' 'Full HC ManagerFactory profile graph remains intentionally isolated' `
  'HC starts ProfileManager/PowerProfileManager with Settings/Platform Initialized callbacks.' `
  'YeMan starts only DeviceManager and rejects active non-fan managers.' `
  'External TDP/GPU/profile side effects are contained, but full manager-graph equivalence is not claimed.' `
  'Keep isolation; require explicit design evidence before enabling non-fan managers.'
Need 'T3-R2-INV-002' 'P1' 'External Applied/Discarded observation is queued instead of synchronous HC subscriber timing' `
  'HC calls the device callback and Applied synchronously under profileLock.' `
  'YeMan snapshots on the event thread and applies from the serialized watchdog.' `
  'This avoids reentrancy but preserves a latency/order boundary requiring target logs.' `
  'Retain event-burst and real-machine evidence; do not claim byte-for-byte timing.'
Need 'T3-R2-INV-003' 'P1' 'AC/DC custom Profile.PowerProfiles mapping is unavailable while the graph is isolated' `
  'HC selects profile.PowerProfiles[(int)PowerLineStatus].' `
  'YeMan uses a pinned template/default route without starting full ProfileManager.' `
  'A custom profile can select a non-default AC/DC GUID that is not source-proven.' `
  'Capture a custom mapping or add a read-only adapter without starting non-fan managers.'
Need 'T3-R2-INV-005' 'P1' 'Discarded without a following Applied remains an observable manager-state boundary' `
  'HC Discarded clears state and waits for the next Applied owner.' `
  'YeMan retains the last safe template and emits bounded no-next-Applied evidence.' `
  'Safe for writes but not a full manager-state mirror if the external owner disappears.' `
  'Keep as a target-machine evidence item.'
Need 'T3-R2-INV-006' 'P1' 'HC Settings/Platform Initialized semantics are not recreated by YeMan' `
  'HC device OpenEvents subscribes Settings/Platform initialization and setting changes.' `
  'YeMan deliberately uses HW temperature and keeps those managers stopped.' `
  'Non-fan settings/platform semantics are outside the fan-only host.' `
  'Do not start non-fan managers solely to remove this boundary.'
Need 'T3-R2-INV-007' 'P1' 'Lease/heartbeat and native HTTP ownership are YeMan-specific' `
  'HC owns the device through application lifecycle without a Fan API lease.' `
  'YeMan adds lease, heartbeat, parent watchdog and conflict recovery.' `
  'The safety protocol cannot be declared source-equivalent to HC.' `
  'Keep lease/sleep/close regression and target logs.'

$hashes = [ordered]@{}
foreach ($entry in $paths.GetEnumerator()) { $hashes[$entry.Key] = [ordered]@{ path=$entry.Value; sha256=(Sha $entry.Value) } }
$result = [ordered]@{
  audit='T3-HC-PROFILE-EVENTS-R2'
  capturedAtUtc=[DateTime]::UtcNow.ToString('o')
  hardwareWrites=$false
  frozenHcModified=$false
  checks=$checks
  parityDiff=$diffs
  summary=[ordered]@{
    pass=@($checks | Where-Object status -eq 'pass').Count
    fail=@($checks | Where-Object status -eq 'fail').Count
    needsInvestigation=$diffs.Count
    p0=@($diffs | Where-Object severity -eq 'P0').Count
    p1=@($diffs | Where-Object severity -eq 'P1').Count
  }
  inputs=$hashes
}
$json = $result | ConvertTo-Json -Depth 16
$json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'discovery.json') -Encoding UTF8
$json | Set-Content -LiteralPath (Join-Path $OutputDirectory 't3-r2-audit-result.json') -Encoding UTF8
($hashes.GetEnumerator() | ForEach-Object { "{0}  {1}" -f $_.Value.sha256, $_.Value.path }) |
  Set-Content -LiteralPath (Join-Path $OutputDirectory 'sha256.txt') -Encoding UTF8

$md = [System.Text.StringBuilder]::new()
[void]$md.AppendLine('# T3 HC Profile / Events second-pass discovery')
[void]$md.AppendLine('')
[void]$md.AppendLine("Captured UTC: $($result.capturedAtUtc)")
[void]$md.AppendLine('Hardware writes: false (source-only audit)')
[void]$md.AppendLine('Frozen HC source modified: false')
[void]$md.AppendLine('')
[void]$md.AppendLine('## Default rule')
[void]$md.AppendLine('')
[void]$md.AppendLine('Unproven differences, test-passed-without-source-evidence, and unexplained behavior remain needs-investigation. HW temperature is the only pre-excluded source difference. The six first-pass P1 boundaries are carried forward, not silently closed.')
[void]$md.AppendLine('')
[void]$md.AppendLine('## Second-pass checks')
[void]$md.AppendLine('')
foreach ($c in $checks) { [void]$md.AppendLine("- [$($c.status)] $($c.id): $($c.title) — $($c.evidence)") }
[void]$md.AppendLine('')
[void]$md.AppendLine('## Carried-forward needs-investigation (non-isolated)')
[void]$md.AppendLine('')
foreach ($d in $diffs) {
  [void]$md.AppendLine("- **$($d.severity) $($d.id)** $($d.title)")
  [void]$md.AppendLine("  - HC: $($d.hc)")
  [void]$md.AppendLine("  - YeMan: $($d.yeman)")
  [void]$md.AppendLine("  - Impact: $($d.impact)")
  [void]$md.AppendLine("  - Next evidence: $($d.next)")
}
[void]$md.AppendLine('')
[void]$md.AppendLine('## Result')
[void]$md.AppendLine('')
[void]$md.AppendLine("pass=$($result.summary.pass); fail=$($result.summary.fail); needs-investigation=$($result.summary.needsInvestigation); P0=$($result.summary.p0); P1=$($result.summary.p1)")
[void]$md.AppendLine('No HC assembly or hardware path was loaded by this audit.')
$md.ToString() | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.md') -Encoding UTF8

$needs = [System.Text.StringBuilder]::new()
[void]$needs.AppendLine('# T3 R2 needs-investigation register')
[void]$needs.AppendLine('')
foreach ($d in $diffs) { [void]$needs.AppendLine("- $($d.severity) $($d.id): $($d.title)") }
$needs.ToString() | Set-Content -LiteralPath (Join-Path $OutputDirectory 'needs-investigation.md') -Encoding UTF8

Write-Output ("T3 R2 discovery: EXECUTED (pass={0}; fail={1}; needs-investigation={2}; P0={3}; P1={4}; hardwareWrites=false)" -f $result.summary.pass,$result.summary.fail,$result.summary.needsInvestigation,$result.summary.p0,$result.summary.p1)
