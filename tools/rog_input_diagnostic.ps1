[CmdletBinding()]
param(
  [int]$DurationSeconds = 8,
  [string]$Output = "$PWD\rog-input-diagnostic.json"
)
$ErrorActionPreference = 'Stop'

$xinputSource = @'
using System;
using System.Runtime.InteropServices;
public static class YeManRogXInput {
  [StructLayout(LayoutKind.Sequential)] public struct Gamepad {
    public ushort wButtons; public byte bLeftTrigger; public byte bRightTrigger;
    public short sThumbLX; public short sThumbLY; public short sThumbRX; public short sThumbRY;
  }
  [StructLayout(LayoutKind.Sequential)] public struct State { public uint dwPacketNumber; public Gamepad Gamepad; }
  [DllImport("xinput1_4.dll", EntryPoint="XInputGetState")] public static extern uint GetState(uint user, out State state);
}
'@
if (-not ('YeManRogXInput' -as [type])) { Add-Type -TypeDefinition $xinputSource }

$samples = [System.Collections.Generic.List[object]]::new()
$connectedSlots = [System.Collections.Generic.HashSet[int]]::new()
$deadline = (Get-Date).AddSeconds([Math]::Max(1, $DurationSeconds))
while ((Get-Date) -lt $deadline) {
  foreach ($slot in 0..3) {
    $state = New-Object YeManRogXInput+State
    $rc = [YeManRogXInput]::GetState($slot, [ref]$state)
    if ($rc -eq 0) {
      [void]$connectedSlots.Add($slot)
      $samples.Add([ordered]@{
        timestampUtc = [DateTime]::UtcNow.ToString('o'); slot = $slot
        packet = $state.dwPacketNumber; buttons = $state.Gamepad.wButtons
        leftTrigger = $state.Gamepad.bLeftTrigger; rightTrigger = $state.Gamepad.bRightTrigger
        leftX = $state.Gamepad.sThumbLX; leftY = $state.Gamepad.sThumbLY
        rightX = $state.Gamepad.sThumbRX; rightY = $state.Gamepad.sThumbRY
      })
    }
  }
  Start-Sleep -Milliseconds 100
}

$pnp = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
  $_.FriendlyName -match '(?i)ROG|Ally|Xbox|Gamepad|HID-compliant game controller' -or
  $_.InstanceId -match '(?i)ROG|ALLY|XINPUT|VID_045E|VID_0B05'
} | Select-Object Status, Class, FriendlyName, InstanceId)
$processes = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
  $_.ProcessName -match '(?i)Armoury|ArmouryCrate|ROG|GameVisual|GameCenter|Xbox|GameBar'
} | Select-Object ProcessName, Id, Path)
$services = @(Get-Service -ErrorAction SilentlyContinue | Where-Object {
  $_.Name -match '(?i)Armoury|ROG|ASUS|Xbox|GameInput' -or $_.DisplayName -match '(?i)Armoury|ROG|ASUS|Xbox|GameInput'
} | Select-Object Status, Name, DisplayName)

$result = [ordered]@{
  evidenceId = 'R1-ROG-INPUT-DIAGNOSTIC-20260904'
  status = if ($connectedSlots.Count -gt 0) { 'XINPUT_VISIBLE' } else { 'NO_XINPUT_SLOT'
  }
  systemMutation = $false
  durationSeconds = $DurationSeconds
  connectedXInputSlots = @($connectedSlots | Sort-Object)
  xinputSampleCount = $samples.Count
  xinputSamples = @($samples | Select-Object -First 80)
  pnpMatches = $pnp
  relatedProcesses = $processes
  relatedServices = $services
  interpretation = 'Read-only snapshot. XInput visibility proves only that YeManCC and games can potentially read the same device; it does not prove isolation.'
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$parent = Split-Path -Parent $Output
if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding UTF8
Write-Output "ROG input diagnostic: $($result.status)"
Write-Output "XInput slots: $(@($connectedSlots | Sort-Object) -join ',')"
Write-Output "Evidence: $Output"
