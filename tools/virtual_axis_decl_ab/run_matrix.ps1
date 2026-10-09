# run_matrix.ps1 —— 导航层读法"逐轴矩阵"实验（一次性；需管理员）
# 目标：钉死"平台的 UI 导航层到底按哪个字节/哪个 usage 读"，并找出能否在"数据仍为 Sony 语义"
#       的前提下让导航层保持安静。
# 全部使用同一工具：virtual_axis_decl_ab（退出即 Dispose；可退回）。

$Dir = 'C:\Users\DaVe\Desktop\手柄\0916-axisdecl-AB'
$Tool = 'g:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\virtual_axis_decl_ab\bin\Release\net10.0-windows\virtual_axis_decl_ab.exe'
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
$matrix = @(
    @{ n = 'R01-original-dynamic-neutral';   a = @('--mode', 'original', '--persona', 'dualsense') },
    @{ n = 'R02-original-dynamic-neutral-2'; a = @('--mode', 'original', '--persona', 'dualsense') },
    @{ n = 'R03-swapped-dynamic-neutral';    a = @('--mode', 'swapped', '--persona', 'dualsense') },
    @{ n = 'R04-swapped-dynamic-neutral-2';  a = @('--mode', 'swapped', '--persona', 'dualsense') },
    @{ n = 'R05-swapped-static-neutral';     a = @('--mode', 'swapped', '--persona', 'dualsense', '--static') },
    @{ n = 'R06-original-static-neutral';    a = @('--mode', 'original', '--persona', 'dualsense', '--static') },
    @{ n = 'R07-swapped-static-stick0';      a = @('--mode', 'swapped', '--persona', 'dualsense', '--static', '--set', 'Rx=00,Ry=00') },
    @{ n = 'R08-swapped-static-trigHalf';    a = @('--mode', 'swapped', '--persona', 'dualsense', '--static', '--set', 'Z=80,Rz=80') },
    @{ n = 'R09-swapped-static-trigMax';     a = @('--mode', 'swapped', '--persona', 'dualsense', '--static', '--set', 'Z=FF,Rz=FF') },
    @{ n = 'R10-swapped-static-leftStick0';  a = @('--mode', 'swapped', '--persona', 'dualsense', '--static', '--set', 'X=00,Y=00') },
    @{ n = 'R11-ds4-original-dynamic-neutral'; a = @('--mode', 'original', '--persona', 'dualshock4') },
    @{ n = 'R12-ds4-swapped-dynamic-neutral';  a = @('--mode', 'swapped', '--persona', 'dualshock4') }
)

$summary = @()
foreach ($r in $matrix) {
    $log = Join-Path $Dir "$($r.n).log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    Write-Host "=== $($r.n) ==="
    $args = @($r.a) + @('--hold-sec', '20', '--start-probe-sec', '5')
    $p = Start-Process -FilePath $Tool -ArgumentList $args -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    $p.WaitForExit(120000) | Out-Null
    $txt = Get-Content $log -Raw
    $verdict = ([regex]::Match($txt, 'verdict=(.*?)\r?\n')).Groups[1].Value.Trim()
    $pre = ([regex]::Match($txt, 'keys down BEFORE create: \[(.*?)\]')).Groups[1].Value
    $post = ([regex]::Match($txt, 'keys down AFTER dispose\+0\.9s: \[(.*?)\]')).Groups[1].Value
    $events = ([regex]::Matches($txt, 'key (DOWN|UP  ) (\S+) @\+(\S+)s') | ForEach-Object { $_.Groups[1].Value.Trim() + ':' + $_.Groups[2].Value + '@' + $_.Groups[3].Value }) -join ' '
    $summary += [pscustomobject]@{ Round = $r.n; Verdict = $verdict; Before = $pre; After = $post; Events = $events }
    Write-Host "   verdict=$verdict | before=[$pre] after=[$post]"
    Write-Host "   events: $events"
}
$summary | Export-Csv -Path (Join-Path $Dir 'matrix-summary.csv') -NoTypeInformation -Encoding UTF8
$summary | Format-Table -AutoSize | Out-String -Width 400 | Set-Content (Join-Path $Dir 'matrix-summary.txt')
Write-Host "=== residual device check ==="
Get-PnpDevice -PresentOnly -Class HIDClass -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match 'DualSense|Wireless Controller' } |
    Select-Object FriendlyName, InstanceId, Status |
    Tee-Object -FilePath (Join-Path $Dir 'residual-devices-matrix.txt')
Write-Host "=== done ==="