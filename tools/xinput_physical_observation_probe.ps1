[CmdletBinding()]
param([int]$DurationSeconds = 20)
$ErrorActionPreference = 'Stop'
if ($DurationSeconds -lt 5 -or $DurationSeconds -gt 60) { throw 'DurationSeconds must be between 5 and 60.' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$source = @'
using System;
using System.Runtime.InteropServices;
public static class YeManXInputProbe {
  [StructLayout(LayoutKind.Sequential)] public struct Gamepad { public ushort wButtons; public byte bLeftTrigger,bRightTrigger; public short sThumbLX,sThumbLY,sThumbRX,sThumbRY; }
  [StructLayout(LayoutKind.Sequential)] public struct State { public uint dwPacketNumber; public Gamepad Gamepad; }
  [DllImport("xinput1_4.dll", EntryPoint="XInputGetState")] public static extern uint GetState(uint user, out State state);
}
'@
if (-not ('YeManXInputProbe' -as [type])) { Add-Type -TypeDefinition $source }
$samples=@(); $deadline=[DateTime]::UtcNow.AddSeconds($DurationSeconds); $last=@{}; $connectedSlots=@()
Write-Output "XInput read-only observation running for $DurationSeconds seconds. Press A, move left stick, then release."
while([DateTime]::UtcNow -lt $deadline){
  foreach($slot in 0..3){
    $state=New-Object YeManXInputProbe+State; $rc=[YeManXInputProbe]::GetState($slot,[ref]$state)
    if($rc -eq 0){
    if($slot -notin $connectedSlots){$connectedSlots += $slot}
    $value=[ordered]@{packet=[uint32]$state.dwPacketNumber;buttons=[uint16]$state.Gamepad.wButtons;leftTrigger=[byte]$state.Gamepad.bLeftTrigger;rightTrigger=[byte]$state.Gamepad.bRightTrigger;leftX=[int16]$state.Gamepad.sThumbLX;leftY=[int16]$state.Gamepad.sThumbLY;rightX=[int16]$state.Gamepad.sThumbRX;rightY=[int16]$state.Gamepad.sThumbRY}
    $json=$value|ConvertTo-Json -Compress
    if($json -ne $last[$slot]){$samples += [ordered]@{timestampUtc=[DateTime]::UtcNow.ToString('o');slot=$slot;state=$value};$last[$slot]=$json}
    }
  }
  Start-Sleep -Milliseconds 50
}
$hasButton=@($samples|Where-Object {$_.state.buttons -ne 0}).Count -gt 0
$hasAxis=@($samples|Where-Object {[Math]::Abs([int]$_.state.leftX) -gt 2500 -or [Math]::Abs([int]$_.state.leftY) -gt 2500 -or [Math]::Abs([int]$_.state.rightX) -gt 2500 -or [Math]::Abs([int]$_.state.rightY) -gt 2500}).Count -gt 0
$out=Join-Path $repo 'Mainline\Build\Validation\HC-Parity\R1-xinput-physical-observation-20260903.json'
$result=[ordered]@{evidenceId='R1-XINPUT-PHYSICAL-OBSERVATION-20260903';status=if($connectedSlots.Count -gt 0 -and ($hasButton -or $hasAxis)){'PASS'}elseif($connectedSlots.Count -gt 0){'OBSERVATION_REQUIRED'}else{'NO_XINPUT_CONTROLLER'};systemMutation=$false;durationSeconds=$DurationSeconds;connected=($connectedSlots.Count -gt 0);connectedSlots=$connectedSlots;buttonObserved=$hasButton;axisObserved=$hasAxis;samples=$samples;conclusion='Read-only XInput observation across slots 0-3. No HidHide, virtual device, HC or host operation was invoked.';generatedUtc=[DateTime]::UtcNow.ToString('o')}
$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $out -Encoding UTF8
Write-Output "R1 XINPUT PHYSICAL OBSERVATION: $($result.status)"
Write-Output "Evidence: $out"
