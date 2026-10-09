# run_vendorpad.ps1 —— S0'' 验证：vendor 主集合 + 末尾"空 GD/GamePad 集合"（一次性；需管理员）
# 目标：① 幻影是否仍被消除（行为门） ② Steam 是否仍能认出 PS5/PS4（configset / controller_ui 差异）

$Dir = 'C:\Users\DaVe\Desktop\手柄\0916-axisdecl-AB'
$Tool = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\virtual_axis_decl_ab\bin\Release\net10.0-windows\virtual_axis_decl_ab.exe'
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
$cfgRoot = 'D:\Game\Steam\steamapps\common\Steam Controller Configs'
$uiLog = 'D:\Game\Steam\logs\controller_ui.txt'

function Snap([string]$tag) {
    $out = Join-Path $Dir "steam-snapshot-$tag.txt"
    "== configsets ==" | Set-Content $out
    Get-ChildItem $cfgRoot -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 10 |
        ForEach-Object { "  $($_.Name)  $($_.LastWriteTime)" } | Add-Content $out
    "== controller_ui tail ==" | Add-Content $out
    if (Test-Path $uiLog) { Get-Content $uiLog -Tail 12 | Add-Content $out }
    Get-Content $out | Write-Host
}

Write-Host "=== 基线 ==="; Snap 'before'

function Run-Behavior([string]$name) {
    $log = Join-Path $Dir "$name.log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Write-Host "=== $name ==="
    $p = Start-Process -FilePath $Tool -ArgumentList '--mode', 'vendorpad', '--persona', 'dualsense', '--hold-sec', '20', '--start-probe-sec', '6' -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    $p.WaitForExit(120000) | Out-Null
    $txt = Get-Content $log -Raw
    Write-Host ("   verdict=" + ([regex]::Match($txt, 'verdict=(.*?)\r?\n')).Groups[1].Value.Trim())
    Get-Content $log | Tee-Object -FilePath (Join-Path $Dir "$name-harness.log") | Out-Null
}

function Run-Hold([string]$name, [int]$sec) {
    $log = Join-Path $Dir "$name.log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Write-Host "=== $name (hold ${sec}s, 给 Steam 枚举窗口) ==="
    $p = Start-Process -FilePath $Tool -ArgumentList '--mode', 'vendorpad', '--persona', 'dualsense', '--hold-sec', $sec -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    $p.WaitForExit(180000) | Out-Null
    Get-Content $log | Tee-Object -FilePath (Join-Path $Dir "$name-harness.log") | Out-Null
}

Run-Behavior 'W1-vendorpad-behavior-a'
Run-Hold 'W2-vendorpad-hold40' 40
Run-Behavior 'W3-vendorpad-behavior-b'

Write-Host "=== 运行后 ==="; Snap 'after'
Write-Host "=== residual device check ==="
Get-PnpDevice -PresentOnly -Class HIDClass -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match 'DualSense|Wireless Controller' } |
    Select-Object FriendlyName, InstanceId, Status |
    Tee-Object -FilePath (Join-Path $Dir 'residual-devices-vendorpad.txt')
Write-Host "=== done ==="