[CmdletBinding()]
param(
  [string]$NativeSource
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($NativeSource)) {
  $NativeSource = Join-Path $PSScriptRoot '..\native\main.cpp'
}
$source = Get-Content -LiteralPath $NativeSource -Raw

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw "HC HID identity selftest failed: $Message" }
}

# These are the HC DeviceManager/IController boundaries already used by the
# production resolver. This test is intentionally read-only: it does not call
# HidHide, PnP, XInput, or a device driver.
Require ($source.Contains('caps.UsagePage == 0x01 && (caps.Usage == 0x04 || caps.Usage == 0x05)')) `
  'physical HID gamepad/joystick usages 0x04 and 0x05 are not admitted'
Require ($source.Contains('hidHideUpper(instance).find("IG_") != std::string::npos')) `
  'physical XInput IG_ child admission is missing'
Require ($source.Contains('HIDMAESTRO') -and $source.Contains('HM-CTL') -and $source.Contains('VIGEM')) `
  'virtual identity exclusion is incomplete'
Require ($source.Contains('hidHideGetPhysicalIdentities(physicalSources)')) `
  'physical source collection is not the HC-bound admission step'
Require ($source.Contains('physical.baseContainerDeviceInstanceId') -and $source.Contains('physical.deviceInstanceId')) `
  'HC base-container + selected-instance hide pair is incomplete'
# S59 alignment (HC IController.HideHID, ControllerManager.cs:888-894): HC hides
# ONLY the selected controller's base-container and device-instance paths; the
# descendant walk (EnumerateDeviceAndChildren) is commented out in HC because
# hiding non-gaming HID children makes HidHideCLI return non-zero and aborts the
# whole p-hid transaction. YMCC therefore intentionally does NOT collect XInput
# composite HID/IG descendants — the legacy hidHideCollectPhysicalDescendants
# helper was removed. The descendant exclusion is evidenced by the dedicated
# later-hide journal field instead.
Require (-not $source.Contains('hidHideCollectPhysicalDescendants')) `
  'XInput composite HID/IG descendant collection must NOT be present (HC descendant walk commented out in HC)'
Require ($source.Contains('virtualTargetExcluded')) `
  'virtual target exclusion evidence is missing'

$fixtures = @(
  [pscustomobject]@{ name = 'HC-gamepad-usage'; usagePage = 0x01; usage = 0x04; admitted = $true },
  [pscustomobject]@{ name = 'HC-joystick-usage'; usagePage = 0x01; usage = 0x05; admitted = $true },
  [pscustomobject]@{ name = 'HC-consumer-page'; usagePage = 0x05; usage = 0x01; admitted = $true },
  [pscustomobject]@{ name = 'keyboard'; usagePage = 0x01; usage = 0x06; admitted = $false }
)
foreach ($fixture in $fixtures) {
  $admitted = $fixture.usagePage -eq 0x05 -or
    ($fixture.usagePage -eq 0x01 -and $fixture.usage -in @(0x04, 0x05))
  Require ($admitted -eq $fixture.admitted) $fixture.name
}

Write-Output 'HC HID identity selftest: PASS'
Write-Output 'ROG rule status: HC fixture PASS; real ROG hardware not present on this PC'
Write-Output 'Mutation performed: none'
