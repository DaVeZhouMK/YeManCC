# 确保以管理员身份运行
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

# 游戏识别阀门是唯一的进程选择器；这里只执行既定的 CPU 掩码。
$gate = 'C:\SOFT\YeMan\PowerControl\game-target.json'
$target = $null
try {
    $raw = Get-Content -LiteralPath $gate -Raw -ErrorAction Stop | ConvertFrom-Json
    $targetPid = [int]$raw.pid
    $created = [Int64]$raw.processCreated
    $generation = [Int64]$raw.generation
    $age = [DateTimeOffset]::Now.ToUnixTimeMilliseconds() - [Int64]$raw.lastSeen
    $process = Get-Process -Id $targetPid -ErrorAction Stop
    if ($raw.valid -and $targetPid -gt 0 -and $created -gt 0 -and $generation -gt 0 -and
        $age -ge 0 -and $age -le 10000 -and
        [Int64]$process.StartTime.ToFileTimeUtc() -eq $created) {
        $target = $process
    }
} catch { }

if ($target) {
    Write-Host "Adjusting CPU affinity for valve PID: $($target.Id)"

    # 设置 CPU 亲和性为12核
    # 计算对应的二进制掩码
    $AffinityMask = 0xFFF # 0xFFF = 111111111111 (二进制)
    $target.ProcessorAffinity = $AffinityMask
} else {
    Write-Host "No current game admitted by the native game valve."
}



