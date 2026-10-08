# Virtual handpad three-state read-only acceptance probe (2026-09-09).
# Expectation after the lifecycle fixes:
#   closed      : physical XInput present (>=1 slot) and no virtual DS4 HID
#   virtual-on  : XInput slots empty and exactly one HIDMaestro DS4 (054C:09CC)
#   closed again: physical XInput present again (restore+cycle, no reboot queue)
# Read-only: never mutates drivers, HidHide or devices.
param()
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ToggleXi {
  [StructLayout(LayoutKind.Sequential)] public struct G { public int wButtons; public byte a, b; public short lx, ly, rx, ry; }
  [StructLayout(LayoutKind.Sequential)] public struct S { public uint p; public G g; }
  [DllImport("xinput1_4.dll")] public static extern int XInputGetState(int i, out S s);
}
'@

$connected = @()
for ($i = 0; $i -lt 4; $i++) {
  $s = New-Object ToggleXi+S
  $r = [ToggleXi]::XInputGetState($i, [ref]$s)
  if ($r -eq 0) { $connected += $i }
}

$exe = Get-ChildItem (Join-Path $PSScriptRoot 'hid_readonly_probe\bin\Release') -Recurse -Filter 'hid_readonly_probe.exe' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
$ds4 = $false
$xboxHid = $false
if ($exe) {
  $j = & $exe 2>&1 | ConvertFrom-Json
  foreach ($d in $j.devices) {
    if ($d.vid -eq 1356 -and $d.pid -eq 2508) { $ds4 = $true }
    if ($d.vid -eq 1118 -and $d.pid -eq 654) { $xboxHid = $true }
  }
}

$journal = Get-Content "C:\SOFT\YeMan\PowerControl\input-hidhide-visibility-journal.json" -ErrorAction SilentlyContinue | ConvertFrom-Json

$state = if ($connected.Count -gt 0 -and -not $ds4) { 'physical-pass-through' }
         elseif ($connected.Count -eq 0 -and $ds4) { 'virtual-only' }
         elseif ($connected.Count -eq 0 -and -not $ds4) { 'unexpected-none' }
         else { 'unexpected-both' }

[PSCustomObject]@{
  utc = (Get-Date).ToUniversalTime().ToString('u')
  expectedState = $state
  xinputSlotsConnected = ($connected -join ',')
  virtualDs4HidPresent = $ds4
  physicalXboxHidPresent = $xboxHid
  hidhideJournalActive = [bool]$journal.active
} | ConvertTo-Json