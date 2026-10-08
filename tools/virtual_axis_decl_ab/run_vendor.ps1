# run_vendor.ps1 —— S0' 候选验证：顶层集合改全 vendor（"平台不当手柄"）后，开始菜单是否还被动（一次性；需管理员）
# 只改 1 处标签：05010905a101 -> 0600ff0901a101（数据字节不动；线缆仍为 Sony 语义）
# 三轮：V1 dualsense 行为 / V2 dualsense 平台口径 / V3 ds4 行为

$Dir = 'C:\Users\DaVe\Desktop\手柄\0916-axisdecl-AB'
$Tool = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\virtual_axis_decl_ab\bin\Release\net10.0-windows\virtual_axis_decl_ab.exe'
$Probe = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\hid_readonly_probe\bin\Release\net10.0-windows\hid_readonly_probe.exe'
New-Item -ItemType Directory -Force -Path $Dir | Out-Null

function Run-Behavior([string]$name, [string]$persona) {
    $log = Join-Path $Dir "$name.log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Write-Host "=== $name (behavior) ==="
    $p = Start-Process -FilePath $Tool -ArgumentList '--mode', 'vendor', '--persona', $persona, '--hold-sec', '20', '--start-probe-sec', '6' -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    $p.WaitForExit(120000) | Out-Null
    $txt = Get-Content $log -Raw
    $verdict = ([regex]::Match($txt, 'verdict=(.*?)\r?\n')).Groups[1].Value.Trim()
    $events = ([regex]::Matches($txt, 'key (DOWN|UP  ) (\S+) @\+(\S+)s') | ForEach-Object { $_.Groups[1].Value.Trim() + ':' + $_.Groups[2].Value + '@' + $_.Groups[3].Value }) -join ' '
    Write-Host "   verdict=$verdict | events: $events"
    Get-Content $log | Tee-Object -FilePath (Join-Path $Dir "$name-harness.log") | Out-Null
}

function Run-Capture([string]$name, [string]$persona) {
    $log = Join-Path $Dir "$name.log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Write-Host "=== $name (capture) ==="
    $p = Start-Process -FilePath $Tool -ArgumentList '--mode', 'vendor', '--persona', $persona, '--hold-sec', '20' -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 1500
    & $Probe --usage-watch 6 --vid 054C --pid 0CE6 2>&1 | Tee-Object -FilePath (Join-Path $Dir "$name-usage.log") | Select-Object -Last 6
    $p.WaitForExit(120000) | Out-Null
    Get-Content $log | Tee-Object -FilePath (Join-Path $Dir "$name-harness.log") | Out-Null
}

Run-Behavior 'V1-vendor-dualsense-behavior' 'dualsense'
Run-Capture  'V2-vendor-dualsense-capture'  'dualsense'
Run-Behavior 'V3-vendor-ds4-behavior'       'dualshock4'

Write-Host "=== residual device check ==="
Get-PnpDevice -PresentOnly -Class HIDClass -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match 'DualSense|Wireless Controller' } |
    Select-Object FriendlyName, InstanceId, Status |
    Tee-Object -FilePath (Join-Path $Dir 'residual-devices-vendor.txt')
Write-Host "=== done ==="