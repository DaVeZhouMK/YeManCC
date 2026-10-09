# run_perm1.ps1 —— S0''' 验证：声明序 X,Y,Z,Rx,Rz,Ry（Z/Rx 得 0x80、Rz/Ry 得 0），数据零改动（一次性；需管理员）
# 依据 12 轮实测的"单轴模型"：横向键←Z(0x80中性)、纵向键←Rx(0x80中性)、RT←Rz(0中性)、LT←Ry(0中性)
# 预期：幻影消失（行为门）+ 仍是普通 GD 手柄（Steam 侧应能认出）

$Dir = 'C:\Users\DaVe\Desktop\手柄\0916-axisdecl-AB'
$Tool = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\virtual_axis_decl_ab\bin\Release\net10.0-windows\virtual_axis_decl_ab.exe'
$Probe = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\hid_readonly_probe\bin\Release\net10.0-windows\hid_readonly_probe.exe'
New-Item -ItemType Directory -Force -Path $Dir | Out-Null

function Run-Behavior([string]$name, [string]$mode, [string]$persona) {
    $log = Join-Path $Dir "$name.log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Write-Host "=== $name (mode=$mode persona=$persona) ==="
    $p = Start-Process -FilePath $Tool -ArgumentList '--mode', $mode, '--persona', $persona, '--hold-sec', '20', '--start-probe-sec', '6' -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    $p.WaitForExit(120000) | Out-Null
    $txt = Get-Content $log -Raw
    Write-Host ("   verdict=" + ([regex]::Match($txt, 'verdict=(.*?)\r?\n')).Groups[1].Value.Trim())
    Get-Content $log | Tee-Object -FilePath (Join-Path $Dir "$name-harness.log") | Out-Null
}

function Run-Capture([string]$name, [string]$mode, [string]$persona) {
    $log = Join-Path $Dir "$name.log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Write-Host "=== $name (capture) ==="
    $p = Start-Process -FilePath $Tool -ArgumentList '--mode', $mode, '--persona', $persona, '--hold-sec', '22' -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 1500
    & $Probe --usage-watch 6 --vid 054C --pid 0CE6 2>&1 | Select-Object -Last 5 | Tee-Object -FilePath (Join-Path $Dir "$name-usage.log")
    $p.WaitForExit(120000) | Out-Null
    Get-Content $log | Tee-Object -FilePath (Join-Path $Dir "$name-harness.log") | Out-Null
}

$cfgRoot = 'D:\Game\Steam\steamapps\common\Steam Controller Configs'
Write-Host "=== Steam configset 基线 ==="
Get-ChildItem $cfgRoot -Recurse -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 6 | ForEach-Object { "  $($_.Name)  $($_.LastWriteTime)" } | Tee-Object -FilePath (Join-Path $Dir 'perm1-steam-before.txt')

Run-Behavior 'P1-perm1-dualsense-behavior' 'perm1' 'dualsense'
Run-Capture  'P2-perm1-dualsense-capture'  'perm1' 'dualsense'
Run-Behavior 'P3-perm1-ds4-behavior'       'perm1' 'dualshock4'

Write-Host "=== Steam configset 运行后 ==="
Get-ChildItem $cfgRoot -Recurse -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 6 | ForEach-Object { "  $($_.Name)  $($_.LastWriteTime)" } | Tee-Object -FilePath (Join-Path $Dir 'perm1-steam-after.txt')
Write-Host "=== controller_ui tail ==="
Get-Content 'D:\Game\Steam\logs\controller_ui.txt' -Tail 10 | Tee-Object -FilePath (Join-Path $Dir 'perm1-controller_ui-tail.txt')
Write-Host "=== residual device check ==="
Get-PnpDevice -PresentOnly -Class HIDClass -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match 'DualSense|Wireless Controller' } | Select-Object FriendlyName, InstanceId | Tee-Object -FilePath (Join-Path $Dir 'residual-devices-perm1.txt')
Write-Host "=== done ==="