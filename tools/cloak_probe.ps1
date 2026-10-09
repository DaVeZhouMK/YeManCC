$ErrorActionPreference = 'Stop'
$cli = 'C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe'
$hidChild = 'HID\VID_045E&PID_028E&IG_01\a&1e87af41&0&0000'
$usbBase  = 'USB\VID_045E&PID_028E\Flydigi_XInput_14'
$probe    = 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\hub_cycle_probe.exe'

Write-Output '=== cloak probe: cloaking Flydigi ...'
& $cli --cloak-on
& $cli --dev-hide $hidChild
Start-Sleep -Milliseconds 800

try {
  Write-Output '--- probe HID child (cloaked) ---'
  & $probe --instance $hidChild
  Write-Output '--- probe USB base (cloaked) ---'
  & $probe --instance $usbBase
} finally {
  Write-Output '=== restoring ...'
  & $cli --dev-unhide $hidChild
  & $cli --cloak-off
  Start-Sleep -Milliseconds 500
}
Write-Output '=== post-restore verify (HID child) ==='
& $probe --instance $hidChild
Write-Output 'done'