# run_ab.ps1 —— S0「描述符声明交换」本机 A/B 编排（一次性验证；可退回）
# 需要管理员权限（HIDMaestro 共享内存段要求提升；见 2026-09-16 实测 Win32Exception(5)）。
#
# 四轮：① original 抓取（usage-watch + wire）② original 行为（开始菜单）
#       ③ swapped  抓取 ④ swapped 行为
# 产出：<Dir> 下的日志/harness 输出/线缆档案 + 残留设备检查。

$Dir = 'C:\Users\DaVe\Desktop\手柄\0916-axisdecl-AB'
$Tool = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\virtual_axis_decl_ab\bin\Release\net10.0-windows\virtual_axis_decl_ab.exe'
$Probe = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\hid_readonly_probe\bin\Release\net10.0-windows\hid_readonly_probe.exe'
New-Item -ItemType Directory -Force -Path $Dir | Out-Null

function Run-Capture([string]$mode) {
    $log = Join-Path $Dir "$mode-capture.log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Write-Host "=== [$mode] capture round (hold 20s) ==="
    $p = Start-Process -FilePath $Tool -ArgumentList '--mode', $mode, '--persona', 'dualsense', '--hold-sec', '20' -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 1500
    & $Probe --usage-watch 8 --vid 054C --pid 0CE6 2>&1 | Tee-Object -FilePath (Join-Path $Dir "$mode-usage.log")
    & $Probe --watch 5 --vid 054C --pid 0CE6 --save-profile (Join-Path $Dir "wire-$mode.json") 2>&1 | Tee-Object -FilePath (Join-Path $Dir "$mode-wire.log") | Select-Object -Last 5
    $p.WaitForExit(90000) | Out-Null
    Write-Host "harness exit=$($p.ExitCode)"
    Get-Content $log | Tee-Object -FilePath (Join-Path $Dir "$mode-capture-harness.log")
}

function Run-Behavior([string]$mode) {
    $log = Join-Path $Dir "$mode-behavior.log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Write-Host "=== [$mode] start-menu behavior round ==="
    $p = Start-Process -FilePath $Tool -ArgumentList '--mode', $mode, '--persona', 'dualsense', '--hold-sec', '20', '--start-probe-sec', '6' -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    $p.WaitForExit(90000) | Out-Null
    Get-Content $log | Tee-Object -FilePath (Join-Path $Dir "$mode-behavior-harness.log")
}

Run-Capture 'original'
Run-Behavior 'original'
Run-Capture 'swapped'
Run-Behavior 'swapped'

Write-Host "=== residual device check ==="
Get-PnpDevice -PresentOnly -Class HIDClass -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match 'DualSense|Wireless Controller' } |
    Select-Object FriendlyName, InstanceId, Status |
    Tee-Object -FilePath (Join-Path $Dir 'residual-devices.txt')
Write-Host "=== done. artifacts in $Dir ==="