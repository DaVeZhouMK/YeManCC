# run_vendor2.ps1 —— S0' 收口验证（一次性；需管理员）
# 目标：① DS4 persona 的 vendor 变体归因（复现×2 + 关闭时前台类/鼠标位移遥测）
#       ② DualSense vendor 在线 25s，给 Steam 留枚举窗口 → 之后核对 Steam 侧是否仍认 PS5（"游戏不缺功能"门）

$Dir = 'C:\Users\DaVe\Desktop\手柄\0916-axisdecl-AB'
$Tool = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\virtual_axis_decl_ab\bin\Release\net10.0-windows\virtual_axis_decl_ab.exe'
New-Item -ItemType Directory -Force -Path $Dir | Out-Null

function Run-Behavior([string]$name, [string]$persona) {
    $log = Join-Path $Dir "$name.log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Write-Host "=== $name ==="
    $p = Start-Process -FilePath $Tool -ArgumentList '--mode', 'vendor', '--persona', $persona, '--hold-sec', '20', '--start-probe-sec', '6' -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    $p.WaitForExit(120000) | Out-Null
    $txt = Get-Content $log -Raw
    $verdict = ([regex]::Match($txt, 'verdict=(.*?)\r?\n')).Groups[1].Value.Trim()
    Write-Host "   verdict=$verdict"
    Get-Content $log | Tee-Object -FilePath (Join-Path $Dir "$name-harness.log") | Out-Null
}

Write-Host "=== Steam 侧基线（运行前） ==="
$steamRoot = 'D:\Game\Steam'
$cfgDir = Join-Path $steamRoot 'steamapps\common\Steam Controller Configs'
Get-ChildItem $cfgDir -Directory -ErrorAction SilentlyContinue | ForEach-Object {
    $sub = Join-Path $_.FullName 'config'
    if (Test-Path $sub) {
        Get-ChildItem $sub -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 4 |
            ForEach-Object { "  $($_.Directory.Parent.Name)\$($_.Name)  $($_.LastWriteTime)" }
    }
} | Tee-Object -FilePath (Join-Path $Dir 'steam-configset-before.txt')

Run-Behavior 'V4-vendor-ds4-behavior-a' 'dualshock4'
Run-Behavior 'V5-vendor-ds4-behavior-b' 'dualshock4'
Run-Behavior 'V6-vendor-dualsense-hold25' 'dualsense'

Write-Host "=== Steam 侧（运行后） ==="
Get-ChildItem $cfgDir -Directory -ErrorAction SilentlyContinue | ForEach-Object {
    $sub = Join-Path $_.FullName 'config'
    if (Test-Path $sub) {
        Get-ChildItem $sub -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 4 |
            ForEach-Object { "  $($_.Directory.Parent.Name)\$($_.Name)  $($_.LastWriteTime)" }
    }
} | Tee-Object -FilePath (Join-Path $Dir 'steam-configset-after.txt')

$steamLogs = Join-Path $steamRoot 'logs'
foreach ($lf in @('controller_ui.txt', 'controller_support.txt', 'console-linux.txt')) {
    $f = Join-Path $steamLogs $lf
    if (Test-Path $f) {
        Write-Host "--- tail $lf ---"
        Get-Content $f -Tail 25 | Tee-Object -FilePath (Join-Path $Dir "steam-$lf-tail.txt")
    }
}
Write-Host "=== residual device check ==="
Get-PnpDevice -PresentOnly -Class HIDClass -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match 'DualSense|Wireless Controller' } |
    Select-Object FriendlyName, InstanceId, Status |
    Tee-Object -FilePath (Join-Path $Dir 'residual-devices-vendor2.txt')
Write-Host "=== done ==="