[CmdletBinding(SupportsShouldProcess)]
param(
[string]$DeviceInstanceId,
  [string]$ApplicationPath = 'C:\SOFT\YeMan\YeManCC\YeManCC.exe',
  [ValidateRange(1,60)][int]$ObservationSeconds = 30,
  [string]$Output = '../../Build/Validation/HC-Parity/A1-B08-physical-hidhide-probe.json'
)
$ErrorActionPreference = 'Stop'
$cli = 'C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe'
if (!(Test-Path -LiteralPath $cli -PathType Leaf)) { throw "HidHideCLI missing: $cli" }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outPath = Join-Path $repo $Output
if ([string]::IsNullOrWhiteSpace($DeviceInstanceId)) {
  $candidates = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object {
      $_.InstanceId -match '^(HID|USB|BTH)\\' -and
      (([string]$_.FriendlyName) -match '(?i)gamepad|controller|joystick|xbox|dualshock|playstation|joy-con|pro controller') -and
      (([string]$_.FriendlyName) -notmatch '(?i)LED|AURA|lighting')
    } |
    Select-Object InstanceId, FriendlyName |
    ForEach-Object { [pscustomobject]@{ instanceId = $_.InstanceId; friendlyName = $_.FriendlyName } })
  $result = [ordered]@{
    evidenceId='A1-B08-PHYSICAL-HIDHIDE-PROBE-20260903'; status='BLOCKED_NO_CONCRETE_DEVICE'; systemMutation=$false
    targetDeviceInstanceId=$null; candidates=$candidates; observation='Insert a physical controller and rerun with its concrete InstanceId; no HidHide mutation was attempted.'
    generatedUtc=[DateTime]::UtcNow.ToString('o')
  }
  New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
  $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outPath -Encoding UTF8
  Write-Output "A1 PHYSICAL HIDHIDE PROBE: $($result.status)"; Write-Output "Evidence: $outPath"
  exit 0
}
if ($DeviceInstanceId -notmatch '^(HID|USB|BTH|ROOT)\\') { throw 'DeviceInstanceId must be a concrete HID/PnP instance path' }
function Invoke-Cli([string[]]$CliArgs) { @(& $cli @CliArgs | ForEach-Object { [string]$_ }) }
function Read-State {
  [ordered]@{ cloak=(Invoke-Cli @('--cloak-state') -join ' ').Trim(); inverse=(Invoke-Cli @('--inv-state') -join ' ').Trim(); apps=@(Invoke-Cli @('--app-list')); hidden=@(Invoke-Cli @('--dev-list')) }
}
function Read-ActiveObservation([string]$instanceId) {
  $pnp = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -eq $instanceId } | Select-Object Status,Class,FriendlyName,InstanceId)
  $related = @()
  if ($instanceId -match '(?i)VID_[0-9A-F]{4}&PID_[0-9A-F]{4}') {
    $vidPid = $Matches[0]
    $related = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
      Where-Object { ([string]$_.InstanceId) -match [regex]::Escape($vidPid) } |
      Select-Object Status,Class,FriendlyName,InstanceId)
  }
  $xinput = @()
  try {
    if (-not ('YeManHidHideXInputProbe' -as [type])) {
      Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class YeManHidHideXInputProbe {
  [StructLayout(LayoutKind.Sequential)] public struct Gamepad { public ushort wButtons; public byte bLeftTrigger,bRightTrigger; public short sThumbLX,sThumbLY,sThumbRX,sThumbRY; }
  [StructLayout(LayoutKind.Sequential)] public struct State { public uint dwPacketNumber; public Gamepad Gamepad; }
  [DllImport("xinput1_4.dll", EntryPoint="XInputGetState")] public static extern uint GetState(uint user, out State state);
}
'@
    }
    foreach ($slot in 0..3) {
      $state = New-Object YeManHidHideXInputProbe+State
      $rc = [YeManHidHideXInputProbe]::GetState($slot, [ref]$state)
      $xinput += [ordered]@{ slot=$slot; connected=($rc -eq 0); returnCode=$rc; buttons=if($rc -eq 0){[uint16]$state.Gamepad.wButtons}else{$null}; leftX=if($rc -eq 0){[int]$state.Gamepad.sThumbLX}else{$null}; leftY=if($rc -eq 0){[int]$state.Gamepad.sThumbLY}else{$null} }
    }
  } catch { $xinput = @([ordered]@{ error=$_.Exception.Message }) }
  [ordered]@{ targetPresent = ($pnp.Count -gt 0); pnp = $pnp; relatedDevices = $related; xinput = $xinput }
}
$before = Read-State
$addedApp = $false; $hidden = $false; $cloak = $false
$status = 'BLOCKED'; $errorText = $null; $after = $null; $activeObservation = $null; $xinputBefore = $null
try {
  if ($PSCmdlet.ShouldProcess($DeviceInstanceId, 'HidHide physical-device suppression probe')) {
    $xinputBefore = Read-ActiveObservation $DeviceInstanceId
    if ($before.apps -notcontains "--app-reg `"$ApplicationPath`"") { Invoke-Cli @('--app-reg', $ApplicationPath) | Out-Null; $addedApp = $true }
    Invoke-Cli @('--dev-hide', $DeviceInstanceId) | Out-Null; $hidden = $true
    Invoke-Cli @('--cloak-on') | Out-Null; $cloak = $true
    Write-Output "HidHide cloak active for $ObservationSeconds seconds. Exercise the target controller in the selected consumer now."
    Start-Sleep -Seconds $ObservationSeconds
    $activeObservation = Read-ActiveObservation $DeviceInstanceId
    $status = 'OBSERVATION_REQUIRED'
  } else { $status = 'DRY-RUN' }
} catch { $errorText = $_.Exception.Message; $status = 'FAILED' }
finally {
  if ($cloak) { Invoke-Cli @('--cloak-off') | Out-Null }
  if ($hidden) { Invoke-Cli @('--dev-unhide', $DeviceInstanceId) | Out-Null }
  if ($addedApp) { Invoke-Cli @('--app-unreg', $ApplicationPath) | Out-Null }
  $after = Read-State
}
$result = [ordered]@{
  evidenceId='A1-B08-PHYSICAL-HIDHIDE-PROBE-20260903'; status=$status; systemMutation=($status -eq 'OBSERVATION_REQUIRED');
  targetDeviceInstanceId=$DeviceInstanceId; applicationPath=$ApplicationPath; before=$before; after=$after;
  activeObservation=$activeObservation;
  xinputBefore=$xinputBefore;
  suppressionObserved=[ordered]@{
    primaryPnp=($null -ne $xinputBefore -and $xinputBefore.targetPresent -and -not $activeObservation.targetPresent)
    hidCompanion=($null -ne $xinputBefore -and @($xinputBefore.relatedDevices | Where-Object Class -eq 'HIDClass').Count -gt @($activeObservation.relatedDevices | Where-Object Class -eq 'HIDClass').Count)
    xinput=($null -ne $xinputBefore -and @($xinputBefore.xinput | Where-Object connected).Count -gt @($activeObservation.xinput | Where-Object connected).Count)
  };
  rollback=[ordered]@{ cloakOff=($after.cloak -eq '--cloak-off'); targetNotHidden=(@($after.hidden | Where-Object { $_ -match [regex]::Escape($DeviceInstanceId) }).Count -eq 0); temporaryApplicationRemoved=($addedApp -eq $false -or @($after.apps | Where-Object { $_ -match [regex]::Escape($ApplicationPath) }).Count -eq 0) };
  observation='Record Get-PnpDevice/HIDAPI/XInput visibility while cloak is active; do not infer suppression from CLI state alone'; error=$errorText; generatedUtc=[DateTime]::UtcNow.ToString('o')
}
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outPath -Encoding UTF8
Write-Output "A1 PHYSICAL HIDHIDE PROBE: $status"; Write-Output "Evidence: $outPath"
