<#!
.SYNOPSIS
  Source-only HC profile/update-source boundary audit.

  This script reads frozen HC source and the Fan Host source only. It never
  loads HC, starts ManagerFactory, invokes WMI/ACPI/HID, or writes hardware.
#>
#!>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcRoot,
  [Parameter(Mandatory = $true)][string]$HostSource
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $HcRoot -PathType Container)) { throw "HC root missing: $HcRoot" }
if (!(Test-Path -LiteralPath $HostSource -PathType Leaf)) { throw "Host source missing: $HostSource" }

$deviceRoot = Join-Path $HcRoot 'Devices'
$powerProfile = Join-Path $HcRoot 'Managers\PowerProfileManager.cs'
$systemManager = Join-Path $HcRoot 'Managers\SystemManager.cs'
$fanProfile = Join-Path $HcRoot 'Misc\FanProfile.cs'
foreach ($path in @($deviceRoot, $powerProfile, $systemManager, $fanProfile)) {
  if (!(Test-Path -LiteralPath $path)) { throw "HC source missing: $path" }
}

$hostText = Get-Content -LiteralPath $HostSource -Raw -Encoding UTF8
$profile = Get-Content -LiteralPath $powerProfile -Raw -Encoding UTF8
$system = Get-Content -LiteralPath $systemManager -Raw -Encoding UTF8
$fan = Get-Content -LiteralPath $fanProfile -Raw -Encoding UTF8

if ($profile -notmatch 'SystemManager\.PowerLineStatusChanged\s*\+=\s*SystemManager_PowerLineStatusChanged') {
  throw 'HC PowerProfileManager does not subscribe to PowerLineStatusChanged'
}
if ($profile -notmatch 'ManagerFactory\.profileManager\.Applied\s*\+=\s*ProfileManager_Applied' -or
    $profile -notmatch 'ManagerFactory\.profileManager\.Discarded\s*\+=\s*ProfileManager_Discarded') {
  throw 'HC PowerProfileManager profile Applied/Discarded subscriptions missing'
}
if ($profile -notmatch 'ProfileManager_Applied\(profile,\s*UpdateSource\.Background\)') {
  throw 'HC power-line transition does not use UpdateSource.Background'
}
if ($profile -notmatch 'IDevice\.GetCurrent\(\)\.PowerProfileManager_Applied\(powerProfile,\s*source\)') {
  throw 'HC PowerProfileManager does not forward the original UpdateSource to the device callback'
}
if ($system -notmatch 'PowerLineStatusChanged') {
  throw 'HC SystemManager power-line event declaration/raise missing'
}

# Device override audit: all existing overrides declare source but the frozen
# bodies must not branch on it. This is the evidence that Background is safe
# for the four mapped override bodies, not a claim about unspecified future HC.
$overrideFiles = @()
foreach ($file in Get-ChildItem -LiteralPath $deviceRoot -Recurse -Filter '*.cs') {
  $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
  if ($text -match 'override\s+void\s+PowerProfileManager_Applied\([^)]*\bsource\b[^)]*\)') { $overrideFiles += $file.FullName }
}
if ($overrideFiles.Count -ne 4) { throw "Expected 4 HC PowerProfileManager_Applied overrides, found $($overrideFiles.Count)" }
$sourceBodyUse = @()
foreach ($path in $overrideFiles) {
  $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
  $match = [regex]::Match($text, '(?s)override\s+void\s+PowerProfileManager_Applied\([^)]*\bsource\b[^)]*\)\s*\{(?<body>.*?)\n\s*\}\s*(?:public|private|protected|internal|override|$)')
  if ($match.Success -and $match.Groups['body'].Value -match '\bsource\b') { $sourceBodyUse += $path }
}
if ($sourceBodyUse.Count -gt 0) { throw "HC override body branches on UpdateSource: $($sourceBodyUse -join ', ')" }

if ($hostText -notmatch 'Invoke\("PowerProfileManager_Applied",\s*profile,\s*source\)' -or
    $hostText -notmatch 'Enum\.Parse\(updateType,\s*"Background"(?:,\s*ignoreCase:\s*false)?\)') {
  throw 'Fan Host does not use HC PowerProfileManager_Applied with Background'
}
if ($hostText -notmatch 'PowerLineWatchdog|PowerProfileWatchdogTick|PowerLineStatus') {
  throw 'Fan Host has no AC/DC profile boundary observer'
}
if ($hostText -notmatch 'SubscribeExternalProfileEvents|external-profile-events-subscribed') {
  throw 'Fan Host has no external profile event subscription boundary'
}
foreach ($field in @('avgTemp', 'aggressivity', 'tjLatch', 'tjLastHitUtc', 'belowCount')) {
  if ($fan -notmatch ('\b' + $field + '\b') -or $hostText -notmatch ('CopyFanRuntimeState')) {
    throw "FanProfile runtime field is not represented by the HC-preserving clone boundary: $field"
  }
}

Write-Output ("fan HC profile boundary audit: EXECUTED (hcOverrides={0}; sourceBranches=0; powerLineEvent=true; appliedDiscardedSubscriptions=true; hostBackground=true; fanRuntimeStatePreserved=true; fullManagerFactoryEquivalence=needs-investigation; hardwareWrites=false)" -f $overrideFiles.Count)
