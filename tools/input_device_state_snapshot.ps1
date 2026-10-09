$ErrorActionPreference = 'Stop'
$out = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\..\..\Build\Validation\HC-Parity\A1-device-state-snapshot-20260903.json'
$controllers = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
  $_.InstanceId -match '^(HID|USB|BTH)\\' -and
  (([string]$_.FriendlyName) -match '(?i)gamepad|controller|joystick|xbox|dualshock|playstation|joy-con|pro controller') -and
  (([string]$_.FriendlyName) -notmatch '(?i)LED|AURA|lighting')
} | Select-Object Status,Class,FriendlyName,InstanceId)
$xinput = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -match '(?i)VID_045E|XINPUT|XBOX' } | Select-Object Status,Class,FriendlyName,InstanceId)
$processes = @(Get-Process YeManCC,HIDMaestroTest,YeManInputHost,HandheldCompanion -ErrorAction SilentlyContinue | Select-Object ProcessName,Id,Path)
$result = [ordered]@{ evidenceId='A1-DEVICE-STATE-SNAPSHOT-20260903'; status=if($controllers.Count -eq 0){'NO_PHYSICAL_CONTROLLER'}else{'CONTROLLER_PRESENT'}; systemMutation=$false; controllerCandidates=$controllers; xinputCandidates=$xinput; processes=$processes; capturedUtc=[DateTime]::UtcNow.ToString('o') }
New-Item -ItemType Directory -Force (Split-Path -Parent $out) | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $out -Encoding UTF8
Write-Output "A1 DEVICE STATE SNAPSHOT: $($result.status)"; Write-Output "Evidence: $out"
