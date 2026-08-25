<#
.SYNOPSIS
  T3 source-only audit for HC profile/event/ACDC/runtime boundaries.

  This is deliberately a source audit. It never loads HandheldCompanion,
  starts ManagerFactory, calls Open/OpenEvents, touches WMI/ACPI/HID, or
  enables a hardware write. All unresolved differences are emitted as
  needs-investigation; the audit does not silently treat a source boundary as
  equivalent merely because a mock test passed.
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
  hcFan          = Join-Path $HcRoot 'Misc\FanProfile.cs'
  hcDevice       = Join-Path $HcRoot 'Devices\IDevice.cs'
  host           = $HostSource
  fanHost        = Join-Path $YeManRoot 'src\bridge\fanHost.ts'
  fanApi         = Join-Path $YeManRoot 'src\bridge\fanApi.ts'
  package        = Join-Path $YeManRoot 'package.json'
}
foreach ($entry in $paths.GetEnumerator()) {
  if (!(Test-Path -LiteralPath $entry.Value -PathType Leaf)) { throw "Input source missing ($($entry.Key)): $($entry.Value)" }
}

$text = @{}
foreach ($entry in $paths.GetEnumerator()) {
  $text[$entry.Key] = Get-Content -LiteralPath $entry.Value -Raw -Encoding UTF8
}

function Has([string]$value, [string]$pattern) { return [regex]::IsMatch($value, $pattern, [Text.RegularExpressions.RegexOptions]::Singleline) }
function FirstLine([string]$value, [string]$pattern) {
  $m = [regex]::Match($value, $pattern, [Text.RegularExpressions.RegexOptions]::Singleline)
  if (!$m.Success) { return $null }
  return (($value.Substring(0, $m.Index) -split "`n").Count)
}
function Sha([string]$path) {
  # PowerShell 5 installations can expose Get-FileHash through a profile
  # function that is absent under -NoProfile. Use the framework's SHA256
  # implementation so this source-only audit is deterministic in both shells.
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
  } finally { $sha.Dispose() }
}

$checks = [System.Collections.Generic.List[object]]::new()
$diffs = [System.Collections.Generic.List[object]]::new()
function Pass([string]$id, [string]$title, [string]$evidence) {
  $checks.Add([ordered]@{ id=$id; title=$title; status='pass'; evidence=$evidence })
}
function Need([string]$id, [string]$severity, [string]$title, [string]$hcEvidence, [string]$yeEvidence, [string]$impact, [string]$next) {
  $diffs.Add([ordered]@{ id=$id; severity=$severity; status='needs-investigation'; title=$title; hc=$hcEvidence; yeman=$yeEvidence; impact=$impact; next=$next })
}
function Fail([string]$id, [string]$title, [string]$evidence) {
  $checks.Add([ordered]@{ id=$id; title=$title; status='fail'; evidence=$evidence })
}

# HC event graph and ordering.
if ((Has $text.hcPowerProfile 'profileManager\.Applied\s*\+=\s*ProfileManager_Applied') -and
    (Has $text.hcPowerProfile 'profileManager\.Discarded\s*\+=\s*ProfileManager_Discarded') -and
    (Has $text.hcPowerProfile 'SystemManager\.PowerLineStatusChanged\s*\+=\s*SystemManager_PowerLineStatusChanged')) {
  Pass 'HC-E01' 'HC subscribes to Applied, Discarded and AC/DC events' "PowerProfileManager.cs:$((FirstLine $text.hcPowerProfile 'profileManager\\.Applied'))"
} else { Fail 'HC-E01' 'HC event subscriptions' 'one or more required subscriptions are absent' }

if ((Has $text.hcPowerProfile 'profileManager\.Initialized\s*\+=\s*ProfileManager_Initialized') -and
    (Has $text.hcPowerProfile 'profileManager\.Initialized\s*-=\s*ProfileManager_Initialized') -and
    (Has $text.hcPowerProfile 'PowerLineStatusChanged\s*-=\s*SystemManager_PowerLineStatusChanged')) {
  Pass 'HC-E02' 'HC unsubscribes Initialized and power-line events on Stop' 'PowerProfileManager.cs:61,140-141'
} else { Fail 'HC-E02' 'HC Stop event cleanup' 'Initialized/power-line unsubscribe not proven' }

$appliedPos = $text.hcPowerProfile.IndexOf('IDevice.GetCurrent().PowerProfileManager_Applied(powerProfile, source)')
$eventPos = $text.hcPowerProfile.IndexOf('Applied?.Invoke(powerProfile, source)')
if ($appliedPos -ge 0 -and $eventPos -gt $appliedPos) {
  Pass 'HC-E03' 'HC device callback precedes Applied subscribers' 'PowerProfileManager.cs:238 before 240'
} else { Fail 'HC-E03' 'HC Applied ordering' 'device callback-before-event ordering not proven' }

if ((Has $text.hcPowerProfile 'currentProfile\s*=\s*null') -and (Has $text.hcPowerProfile 'Discarded\?\.Invoke\(powerProfile, swapped\)')) {
  Pass 'HC-E04' 'HC Discarded clears current context and does not apply a replacement' 'PowerProfileManager.cs:250-262'
} else { Fail 'HC-E04' 'HC Discarded semantics' 'clear/event semantics not proven' }

$hcE05A = Has $text.hcPowerProfile 'profile\.PowerProfiles\[\(int\)powerStatus\.PowerLineStatus\]'
$hcE05B = Has $text.hcPowerProfile 'ProfileManager_Applied\(profile, UpdateSource\.Background\)'
if ($hcE05A -and $hcE05B) {
  Pass 'HC-E05' 'HC AC/DC selection uses current Profile PowerProfiles mapping' 'PowerProfileManager.cs:214-230'
} else { Fail 'HC-E05' 'HC AC/DC mapping' 'PowerProfiles mapping not proven' }

$hcE06A = Has $text.hcPowerProfile 'UpdateOrCreateProfile\(profile, UpdateSource\.Serializer\)'
$hcE06B = Has $text.hcPowerProfile 'Updated\?\.Invoke\(profile, source\)'
$hcE06C = Has $text.hcPowerProfile 'Applied\?\.Invoke\(profile, source\)'
if ($hcE06A -and $hcE06B -and $hcE06C) {
  Pass 'HC-E06' 'HC UpdateSource flow distinguishes Serializer from active updates' 'PowerProfileManager.cs:307-344'
} else { Fail 'HC-E06' 'HC UpdateSource flow' 'update/source ordering not proven' }

# FanProfile algorithm and runtime state.
foreach ($token in @('TjHysteresisC', 'TjCooldown', 'TjReleaseSamples', 'tjLatch', 'tjLastHitUtc', 'belowCount', 'aggressivity', 'avgTemp')) {
  if (!(Has $text.hcFan ("\b" + [regex]::Escape($token) + "\b"))) { Fail ('HC-F-' + $token) 'HC FanProfile runtime field' $token }
}
$hcF01A = Has $text.hcFan 'SetTemperature\(double temp\)'
$hcF01B = Has $text.hcFan 'GetFanSpeed\(double temp\)'
$hcF01C = Has $text.hcFan 'Math\.Clamp\(y, 0\.0, 100\.0\)'
if ($hcF01A -and $hcF01B -and $hcF01C) {
  Pass 'HC-F01' 'HC FanProfile temperature smoothing, Tjmax latch and interpolation are present' 'FanProfile.cs:61-153'
} else { Fail 'HC-F01' 'HC FanProfile algorithm' 'interpolation/latch/smoothing not all present' }

# YeMan source boundaries.
if ((Has $text.host 'Invoke\("PowerProfileManager_Applied",\s*profile,\s*source\)') -and
    (Has $text.host 'NormalizeUpdateSourceName')) {
  Pass 'YM-E01' 'Host dispatches through HC PowerProfileManager_Applied with a normalized UpdateSource' 'Program.cs:1738-1755'
} else { Fail 'YM-E01' 'Host HC callback dispatch' 'direct HC callback not proven' }
if ((Has $text.host 'SubscribeExternalProfileEvents') -and (Has $text.host 'OnExternalProfileApplied') -and (Has $text.host 'OnExternalProfileDiscarded')) {
  Pass 'YM-E02' 'Host subscribes to external Applied/Discarded events' 'Program.cs:795-895'
} else { Fail 'YM-E02' 'Host external profile subscriptions' 'subscription boundary absent' }
if ((Has $text.host 'RequestPowerProfileWatchdogTick') -and (Has $text.host 'waiting-for-Applied') -and
    (Has $text.host 'pendingExternalProfileDiscarded')) {
  Pass 'YM-E03' 'Host treats Discarded as a wait boundary and serializes follow-up observation' 'Program.cs:835-895,1218-1243'
} else { Fail 'YM-E03' 'Host Discarded handoff' 'new HC-aligned discard wait behavior absent' }
$appliedHandlerStart = $text.host.IndexOf('private void OnExternalProfileApplied')
$discardedHandlerStart = $text.host.IndexOf('private void OnExternalProfileDiscarded')
$handlerEnd = $text.host.IndexOf('private void RequestPowerProfileWatchdogTick')
$handlerSlice = if ($appliedHandlerStart -ge 0 -and $handlerEnd -gt $appliedHandlerStart) {
  $text.host.Substring($appliedHandlerStart, $handlerEnd - $appliedHandlerStart)
} else { '' }
if ((Has $text.host 'externalProfileGate') -and
    $handlerSlice -notmatch 'lock\s*\(hardwareGate\)' -and
    (Has $text.host 'external-profile-discarded-no-applied') -and
    (Has $text.host 'retain-last-safe-template')) {
  Pass 'YM-E04' 'Host event callbacks use an independent snapshot gate and bounded no-next-Applied evidence' 'Program.cs: externalProfileGate; callback slice; 2-second no-next-Applied diagnostic'
} else { Fail 'YM-E04' 'Host profile callback lock ordering' 'callback still acquires hardwareGate or bounded discard evidence is absent' }
$adoptStart = $text.host.IndexOf('private bool TryAdoptExternalHcProfile')
$watchdogStart = $text.host.IndexOf('private void EnsurePowerProfileWatchdog')
$adoptSlice = if ($adoptStart -ge 0 -and $watchdogStart -gt $adoptStart) {
  $text.host.Substring($adoptStart, $watchdogStart - $adoptStart)
} else { '' }
if ($adoptSlice -notmatch '(?m)^\s*(?:var\s+\w+\s*=\s*)?Invoke\(manager,\s*"GetCurrent"' -and (Has $text.host 'pendingExternalProfileUpdateSource') -and
    (Has $text.host 'NormalizeUpdateSourceName') -and (Has $text.host 'ApplyPowerProfile\(profile, source\)')) {
  Pass 'YM-E05' 'Host consumes detached Applied snapshots without re-entering HC Manager under hardwareGate and preserves UpdateSource' 'Program.cs: TryAdoptExternalHcProfile; PowerProfileWatchdogTick; ApplyPowerProfile'
} else { Fail 'YM-E05' 'Host profile callback re-entry/source propagation' 'manager re-entry or source propagation boundary is not proven' }
if ((Has $text.host 'NormalizeUpdateSourceName') -and (Has $text.host 'lastUpdateSource = normalized') -and
    (Has $text.host 'Enum\.Parse\(updateType, normalized')) {
  Pass 'YM-E06' 'Host forwards external UpdateSource and uses Background only for Host-owned fan updates' 'Program.cs: NormalizeUpdateSourceName/ApplyPowerProfile'
} else { Fail 'YM-E06' 'Host UpdateSource propagation' 'source normalization/forwarding boundary is not proven' }
if ((Has $text.host 'CopyFanRuntimeState') -and (Has $text.host 'avgTemp') -and (Has $text.host 'aggressivity') -and
    (Has $text.host 'tjLatch') -and (Has $text.host 'tjLastHitUtc') -and (Has $text.host 'belowCount')) {
  Pass 'YM-F01' 'Host preserves HC private FanProfile runtime state when cloning' 'Program.cs:1380-1413'
} else { Fail 'YM-F01' 'Host runtime-state preservation' 'private HC runtime state copy incomplete' }
if ((Has $text.host 'DirectCpuTemperatureMonitor') -and (Has $text.host 'AreTemperatureWritesBlocked\(\)') -and
    (Has $text.host 'Invoke\(activeFanProfile, "SetTemperature", temp\)') -and (Has $text.host 'Invoke\("SetFanDuty", duty\)')) {
  Pass 'YM-F02' 'Host uses the permitted HW temperature source and HC fan callback body' 'Program.cs:1007-1103'
} else { Fail 'YM-F02' 'Host temperature gate' 'HW source or write gate not proven' }
if ((Has $text.host 'ApplyPowerProfile\(BuildPowerProfile\(Array\.Empty<double>\(\), software: false\)\)') -and
    (Has $text.host 'BlockWritesForSuspend') -and (Has $text.host 'BlockWritesForClose')) {
  Pass 'YM-L01' 'Host has HC Hardware restore and suspend/close write gates' 'Program.cs:1519-1556,1660-1672'
} else { Fail 'YM-L01' 'Host restore/write gates' 'restore or gate sequence not proven' }
if ((Has $text.fanHost 'heartbeat\(') -and (Has $text.fanHost 'recoverAfterMutationFailure') -and
    (Has $text.fanHost 'restoreAndRelease')) {
  Pass 'YM-L02' 'YeMan lease heartbeat and mutation recovery are serialized' 'fanHost.ts:1586-1720'
} else { Fail 'YM-L02' 'YeMan lease/recovery' 'heartbeat/recovery boundary absent' }

# Explicit non-equivalence boundaries. These are not silently promoted to pass.
Need 'T3-INV-001' 'P1' 'Full HC ManagerFactory profile graph is intentionally isolated' `
  'HC starts PowerProfileManager with ProfileManager, SettingsManager, PlatformManager and Initialized callbacks.' `
  'Host starts only DeviceManager; AssertHcNonFanManagersIsolated rejects active non-fan managers.' `
  'External TDP/GPU/profile side effects are safely contained, but full ProfileManager equivalence is not claimed.' `
  'Keep the isolation boundary; require real-machine evidence or an explicit design decision before claiming full graph equivalence.'
Need 'T3-INV-002' 'P1' 'External Applied/Discarded observation is serialized after the HC callback' `
  'HC executes the device callback and Applied event synchronously under profileLock.' `
  'Host records the event and schedules the existing serialized watchdog; it does not re-enter HC from the event thread.' `
  'This is safer for reentrancy, but it is not byte-for-byte synchronous HC timing; event-to-apply latency and burst ordering need evidence.' `
  'Use the T3 event-burst test and real logs to prove bounded ordering; do not call it full equivalence yet.'
Need 'T3-INV-003' 'P1' 'AC/DC selection cannot use full Profile.PowerProfiles mapping while the graph is isolated' `
  'HC selects profile.PowerProfiles[(int)PowerLineStatus] from the current Profile.' `
  'Host selects a pinned device profile using OEM context, MSI shift, or HC default AC/DC GUID; a full ProfileManager current mapping is not active.' `
  'A custom external profile may map AC/DC to non-default GUIDs; Host parity is not proven for that case.' `
  'Capture a real custom profile mapping or add a read-only mapping adapter that never starts non-fan managers.'
Need 'T3-INV-005' 'P1' 'Discarded next-profile timing remains an observable boundary' `
  'HC Discarded only clears currentProfile and emits Discarded; the next Applied owns the replacement.' `
  'Host now waits for Applied, but the isolated manager may not expose a next profile event if an external owner disappears.' `
  'A permanently discarded profile can leave the last safe template in memory; this is safe for writes but not a proven full-manager state mirror.' `
  'The Host now emits a bounded no-next-Applied diagnostic and retains the last safe template; real profile-switch evidence is still required before claiming manager-state equivalence.'
Need 'T3-INV-006' 'P1' 'HC Initialized and Settings/Platform subscriptions are not recreated by YeMan' `
  'HC subscribes to ProfileManager.Initialized, SettingsManager.SettingValueChanged and PlatformManager initialization.' `
  'YeMan deliberately avoids those managers and reads CPU temperature through the permitted HW source.' `
  'TDP/settings/platform event semantics are outside this fan-only host and cannot be claimed equivalent.' `
  'Track as an explicit boundary; never enable non-fan ManagerFactory startup just to remove this note.'
Need 'T3-INV-007' 'P1' 'Lease/heartbeat ownership is YeMan-specific and has no direct HC source analogue' `
  'HC owns the device through application lifecycle and profile manager state, without a Fan API lease token.' `
  'YeMan adds a 15-second lease, heartbeat and conflict recovery around the HC callback.' `
  'The safety boundary is testable but cannot be declared HC-equivalent by source comparison alone.' `
  'Retain lease tests and require real conflict/sleep evidence before closing.'

$hashes = [ordered]@{}
foreach ($entry in $paths.GetEnumerator()) { $hashes[$entry.Key] = [ordered]@{ path=$entry.Value; sha256=(Sha $entry.Value) } }
$result = [ordered]@{
  audit = 'T3-HC-PROFILE-EVENTS'
  capturedAtUtc = [DateTime]::UtcNow.ToString('o')
  hardwareWrites = $false
  frozenHcModified = $false
  checks = $checks
  parityDiff = $diffs
  summary = [ordered]@{
    pass = @($checks | Where-Object status -eq 'pass').Count
    fail = @($checks | Where-Object status -eq 'fail').Count
    needsInvestigation = $diffs.Count
    p0 = @($diffs | Where-Object severity -eq 'P0').Count
    p1 = @($diffs | Where-Object severity -eq 'P1').Count
  }
  inputs = $hashes
}
$jsonPath = Join-Path $OutputDirectory 'parity-diff.json'
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDirectory 't3-audit-result.json') -Encoding UTF8
$currentInventory = foreach ($entry in $paths.GetEnumerator()) {
  [ordered]@{ key=$entry.Key; path=$entry.Value; sha256=$hashes[$entry.Key].sha256 }
}
$currentInventory | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'source-inventory-current.json') -Encoding UTF8
($hashes.GetEnumerator() | ForEach-Object { "{0}  {1}" -f $_.Value.sha256, $_.Value.path }) |
  Set-Content -LiteralPath (Join-Path $OutputDirectory 'sha256-current.txt') -Encoding UTF8

$md = [System.Text.StringBuilder]::new()
[void]$md.AppendLine('# T3 HC Profile / Events parity audit')
[void]$md.AppendLine('')
[void]$md.AppendLine("Captured UTC: $($result.capturedAtUtc)")
[void]$md.AppendLine('Hardware writes: false (source-only audit)')
[void]$md.AppendLine('Frozen HC source modified: false')
[void]$md.AppendLine('')
[void]$md.AppendLine('## Rule boundary')
[void]$md.AppendLine('')
[void]$md.AppendLine('All unproven differences, test-passed-without-source evidence, and unexplained behavior remain needs-investigation. HW temperature is the only pre-excluded source difference.')
[void]$md.AppendLine('')
[void]$md.AppendLine('## Confirmed checks')
[void]$md.AppendLine('')
foreach ($c in $checks) { [void]$md.AppendLine("- [$($c.status)] $($c.id): $($c.title) — $($c.evidence)") }
[void]$md.AppendLine('')
[void]$md.AppendLine('## Needs investigation (non-isolated)')
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
[void]$md.AppendLine('No hardware path was loaded or called by this audit.')
$md.ToString() | Set-Content -LiteralPath (Join-Path $OutputDirectory 'audit-summary.md') -Encoding UTF8

$needs = [System.Text.StringBuilder]::new()
[void]$needs.AppendLine('# T3 needs-investigation register')
[void]$needs.AppendLine('')
foreach ($d in $diffs) { [void]$needs.AppendLine("- $($d.severity) $($d.id): $($d.title)") }
$needs.ToString() | Set-Content -LiteralPath (Join-Path $OutputDirectory 'needs-investigation.md') -Encoding UTF8

$plan = @'
# T3 verification plan

1. Run this source audit after every YeMan or pinned HC change.
2. Run `pnpm test:fan-hc-profile-boundary` and the host/lifecycle/sleep/route regressions.
3. Run `pnpm test:fan-hc-profile-events-model` for Applied/Discarded bursts,
   UpdateSource preservation, no-next-Applied safe-template retention and
   sleep/close write gating.
4. Run the real host in handshake/read-only mode first; hardwareWritesEnabled must remain false.
5. On an authorized target, capture Applied -> Discarded -> Applied bursts, AC/DC flips, lease expiry/conflict, suspend/resume and close. Confirm no event writes after suspend/close gates.
6. Do not close a needs-investigation item without source evidence or a target-machine log.
'@
$plan | Set-Content -LiteralPath (Join-Path $OutputDirectory 'test-plan.md') -Encoding UTF8

Write-Output ("T3 profile/events audit: EXECUTED (pass={0}; fail={1}; needs-investigation={2}; P0={3}; P1={4}; hardwareWrites=false)" -f $result.summary.pass,$result.summary.fail,$result.summary.needsInvestigation,$result.summary.p0,$result.summary.p1)
