# TopMonitor.ps1 - YMCC 顶部监控条 后台常驻守护
# 数据源：HWiNFO 共享内存（照搬 FPS-Monitor.ps1 的 Read-HwinfoAll 并扩展）+ Windows 电池 API
#   每 2 秒一轮，无论有无游戏都写 C:\SOFT\YeMan\PowerControl\topmon.json：
#     tdpW       = CPU Package Power (W)
#     freqMhz    = CPU 当前主频（多核 Core Clocks 取最大值, MHz）
#     tempC      = CPU (Tctl/Tdie) 温度（无则回退 CPU Package 温度, °C）
#     ac         = ACLineStatus (1=AC 0=DC)
#     hasBattery = 是否存在 Windows 电池（false=台式机）
#     chargeW    = Battery Charge Rate（正=充电 负=放电, mW 转 W；无传感器=0）
#     remainMin  = HWiNFO Estimated Remaining Time（分钟；无数据=-1，Windows API 兜底）
# 电池状态由前端合并显示为：台式机 / 充电 xxW / 放电 xxW
#     hwDown     = HWiNFO 共享内存不可用标记（前端据此显示 -- 并走复位）
# HWiNFO 强制复位：共享内存不可用 → 立即运行统一事务，6s 快速轮询确认。
# 停止：创建 C:\SOFT\YeMan\PowerControl\topmon.stop 文件即退出
$ErrorActionPreference = "SilentlyContinue"

$DIR      = "C:\SOFT\YeMan\PowerControl"
$STATUS   = Join-Path $DIR "topmon.json"
$STOPFLAG = Join-Path $DIR "topmon.stop"
$PIDFILE  = Join-Path $DIR "topmon.pid"
$HWINFO_BAT = Join-Path $DIR "YeManHWiNFO.bat"
$HWINFO_EXE = 'C:\Program Files\HWiNFO64\HWiNFO64.exe'
$HWINFO_CFG = 'C:\Program Files\HWiNFO64\YeMan'

# ── 跨进程单实例互斥：避免顶部条与页面/旧实例同时抢写 topmon.json ──
$topMonMutex = $null
$topMonMutexCreated = $false
try {
    $topMonMutex = New-Object System.Threading.Mutex($false, 'Global\YeManCC_TopMonitor', [ref]$topMonMutexCreated)
    if (-not $topMonMutexCreated) { exit }
} catch { $topMonMutex = $null }

# ── 单实例：杀掉所有其它运行中的本脚本实例（照搬 FPS-Monitor.ps1）──
try {
    $me = $PID
    Get-CimInstance Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe'" -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_.ProcessId -ne $me) {
            $cl = $_.CommandLine
            if ($cl -and $cl -like '*-File*' -and $cl -like '*TopMonitor.ps1*') {
                Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
            }
        }
    }
} catch { }
Set-Content -Path $PIDFILE -Value $PID
if (Test-Path $STOPFLAG) { Remove-Item $STOPFLAG -Force }

# ── HWiNFO 共享内存读取（照搬 FPS-Monitor.ps1 布局，扩展 频率/温度/ChargeRate）──
# 内存映射 "Global\HWiNFO_SENS_SM2"，签名 0x53695748("HWiS")；布局遵循 REALiX 官方 SM2 规范 (#pragma pack(1))
# Header: dwOffsetOfReadingSection@32 / dwSizeOfReadingElement@36 / dwNumReadingElements@40
# Reading 元素(460B): szLabelOrig@+12(128 ANSI) / szUnit@+268(16 ANSI) / double Value@+284 / double ValueAvg@+308
# 扩展传感器：
#   频率    : 标签含 Clocks 且属 CPU/核（排除 Uncore/Bus/Mesh/Ref/Ratio/Boost/DRAM），单位 MHz → 多核取最大值
#   温度    : 标签含 (Tctl|Tdie) 优先（AMD），回退 CPU Package 温度（Intel），单位 °C
#   Charge  : 标签含 Charge Rate|Battery Power（排除 Level/Capacity/Remaining），单位 mW→/1000 转 W / W 原值 / mA 不可换算置 0
function Read-HwinfoAll {
    $res = @{ tdpW = 0.0; freqMhz = 0; tempC = 0.0; chargeW = 0.0; remainMin = -1; cpuUsage = 0.0; gpuPowerW = 0.0; gpuClockMhz = 0.0; ok = $false; err = '' }
    if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -eq 0) {
        $res.err = 'HWiNFO64.exe is not running'; return $res
    }
    $mmf = $null
    foreach ($nm in @('Global\HWiNFO_SENS_SM2','HWiNFO_SENS_SM2','Global\HWiNFO_SENS_SM','HWiNFO_SENS_SM')) {
        try { $mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting($nm); if ($mmf) { break } } catch {}
    }
    if (-not $mmf) { $res.err = 'no HWiNFO shared memory'; return $res }
    try {
        $acc = $mmf.CreateViewAccessor(0, 0)
        $sig = $acc.ReadUInt32(0)
        if ($sig -ne 0x53695748) { $res.err = 'bad HWiNFO signature'; return $res }
        $offReading = $acc.ReadUInt32(32)
        $szReading  = $acc.ReadUInt32(36)
        $numReading = $acc.ReadUInt32(40)
        if ($szReading -lt 316 -or $numReading -eq 0 -or $numReading -gt 100000) { $res.err = 'unexpected reading section'; return $res }
        $endReading = [uint64]$offReading + ([uint64]$szReading * [uint64]$numReading)
        if ($offReading -ge [uint64]$acc.Capacity -or $endReading -gt [uint64]$acc.Capacity) { $res.err = 'reading section outside mapping'; return $res }
        $enc = [System.Text.Encoding]::ASCII
        $buf = New-Object byte[] 128
        $bufU = New-Object byte[] 16
        for ($i = 0; $i -lt $numReading; $i++) {
            $base = [long]($offReading + $i * $szReading)
            $acc.ReadArray([long]($base + 12), $buf, 0, 128) | Out-Null   # szLabelOrig (ANSI)
            $lab = $enc.GetString($buf).TrimEnd([char]0)
            # ── CPU Package Power (W) ──
            if ($lab -match '(?i)CPU.*(Package|Pkg).*Power|Package Power.*CPU' -and $lab -notmatch '(?i)Core|CCD|SoC|GPU') {
                $acc.ReadArray([long]($base + 268), $bufU, 0, 16) | Out-Null
                $unit = $enc.GetString($bufU).TrimEnd([char]0)
                if ($unit -match '(?i)^W$|Watts?') {
                    $pv = $acc.ReadDouble([long]($base + 284))
                    if ($pv -gt 0) { $res.tdpW = $pv }
                }
                continue
            }
            # ── CPU 频率：多核 Core N Clock 取最大值（代表当前最高活动核频率）──
            # 实测标签（AMD 9950X）："Core 0 Clock (perf #5)" 单位 MHz；"Clocks" 复数标签（部分 Intel）
            # 排除 Effective（真实有效频率远低于核心时钟）/Bus/Memory/GPU 等无关时钟
            if ($lab -match '(?i)(Core\s*\d*\s*Clocks?|CPU.*Clocks?)' -and $lab -notmatch '(?i)Effective|Bus|Memory|GPU|Video|Crossbar|VCN|SoC|Ref|Ratio|Boost|Uncore|Mesh|DRAM|Display|Encoder') {
                $acc.ReadArray([long]($base + 268), $bufU, 0, 16) | Out-Null
                $unit = $enc.GetString($bufU).TrimEnd([char]0)
                if ($unit -match '(?i)MHz') {
                    $fv = $acc.ReadDouble([long]($base + 284))
                    if ($fv -gt $res.freqMhz) { $res.freqMhz = [int]$fv }
                }
                continue
            }
            # ── CPU 温度：Tctl/Tdie 优先（AMD），CPU Package 温度兜底（Intel）──
            # ⚠ 单位 °C 的 ° 是 ANSI 0xB0 字节，ASCII 解码后成 U+FFFD（显示 ??）无法匹配，
            #    故温度不再校验单位——温度标签与功率(W)/电压(V)/转速(RPM) 传感器天然不重名。
            if ($lab -match '(?i)CPU.*(Tctl|Tdie)' -or $lab -match '(?i)^CPU Package$') {
                if ($lab -notmatch '(?i)Power|Voltage|RPM|Current') {
                    $tv = $acc.ReadDouble([long]($base + 284))
                    if ($tv -gt 0) { $res.tempC = $tv }
                }
                continue
            }
            # ── HWiNFO Estimated Remaining Time：统一换算为分钟 ──
            if ($lab -match '(?i)Estimated.*Remaining.*Time|Remaining.*Time' -and $lab -notmatch '(?i)Capacity|Charge Rate') {
                $acc.ReadArray([long]($base + 268), $bufU, 0, 16) | Out-Null
                $unit = $enc.GetString($bufU).TrimEnd([char]0)
                $rv = $acc.ReadDouble([long]($base + 284))
                if ($rv -gt 0) {
                    if ($unit -match '(?i)^h$|hours?') { $res.remainMin = [int][math]::Round($rv * 60) }
                    elseif ($unit -match '(?i)^min$|minutes?') { $res.remainMin = [int][math]::Round($rv) }
                    elseif ($unit -match '(?i)^s$|seconds?') { $res.remainMin = [int][math]::Round($rv / 60) }
                }
                continue
            }
            # ── 电池 Charge Rate：正=充电 负=放电；mW→W 换算 ──
            if ($lab -match '(?i)Charge Rate|Battery Power|Battery.*Charge' -and $lab -notmatch '(?i)Level|Capacity|Remaining|Time') {
                $acc.ReadArray([long]($base + 268), $bufU, 0, 16) | Out-Null
                $unit = $enc.GetString($bufU).TrimEnd([char]0)
                if ($unit -match '(?i)^mW$') {
                    $res.chargeW = ($acc.ReadDouble([long]($base + 284))) / 1000.0
                } elseif ($unit -match '(?i)^W$|Watts?') {
                    $res.chargeW = $acc.ReadDouble([long]($base + 284))
                }
                # mA 无电压无法换算 → 保持 0（前端显示 --）
                continue
            }
            # ── 显卡实时瓦数：只取 GPU Power，不取 Power Limit(max/rated) ──
            # HWiNFO 常见标签："GPU Power"、"GPU Power (平均)"；明确排除限制/额定/能力值。
            if ($lab -match '(?i)GPU\s+Power(?:\s|$)' -and $lab -notmatch '(?i)Power\s*Limit|Limit\s*Power|Rated|Maximum|Max|Capability|TGP|TBP') {
                $acc.ReadArray([long]($base + 268), $bufU, 0, 16) | Out-Null
                $unit = $enc.GetString($bufU).TrimEnd([char]0)
                if ($unit -match '(?i)^W$|Watts?') {
                    $pv = $acc.ReadDouble([long]($base + 284))
                    # 实时 GPU Power 允许正负/零，只记录有限值；多 GPU 取实时功耗最高者。
                    if (-not [double]::IsNaN($pv) -and -not [double]::IsInfinity($pv) -and $pv -ge 0 -and $pv -lt 1000 -and $pv -gt $res.gpuPowerW) {
                        $res.gpuPowerW = $pv
                    }
                }
                continue
            }
            # ── 显卡主频：GPU Clock（多 GPU 取最大值），排除 SoC/显存/视频/显示引擎 ──
            if ($lab -match '(?i)GPU.*Clock|Clock.*GPU' -and $lab -notmatch '(?i)SoC|Memory|Video|Display|Encoder|Decoder|Bus|Core\s*\d') {
                $acc.ReadArray([long]($base + 268), $bufU, 0, 16) | Out-Null
                $unit = $enc.GetString($bufU).TrimEnd([char]0)
                if ($unit -match '(?i)MHz') {
                    $cv = $acc.ReadDouble([long]($base + 284))
                    if ($cv -gt $res.gpuClockMhz) { $res.gpuClockMhz = [int]$cv }
                }
                continue
            }
        }
        # 健康条件是进程存在且共享内存结构可读，不要求特定传感器必须有非零值。
        $res.ok = $true
    } finally {
        if ($acc) { $acc.Dispose() }
        $mmf.Dispose()
    }
    return $res
}

# ── GetSystemPowerStatus P/Invoke（ACLineStatus：1=AC 0=DC，零子进程开销）──
Add-Type -Namespace YMCC.TopMon -Name Pwr -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true)]
public static extern bool GetSystemPowerStatus(out SYSTEM_POWER_STATUS sps);
[StructLayout(LayoutKind.Sequential)]
public struct SYSTEM_POWER_STATUS {
    public byte ACLineStatus; public byte BatteryFlag; public byte BatteryLifePercent;
    public byte SystemStatusFlag; public uint BatteryLifeTime; public uint BatteryFullLifeTime;
}
'@ -ErrorAction SilentlyContinue
function Get-PowerStatus {
    $result = @{ ac = $true; remainMin = -1 }
    try {
        $sps = New-Object YMCC.TopMon.Pwr+SYSTEM_POWER_STATUS
        if ([YMCC.TopMon.Pwr]::GetSystemPowerStatus([ref]$sps)) {
            $result.ac = ($sps.ACLineStatus -eq 1)
            if ($sps.BatteryLifeTime -ne [uint32]::MaxValue -and $sps.BatteryLifeTime -gt 0) {
                $result.remainMin = [int][math]::Round($sps.BatteryLifeTime / 60.0)
            }
        }
    } catch { }
    return $result
}

# ── Windows 原生 CPU 总占用：性能计数器（任务管理器同源，不读取 HWiNFO）──
function Get-CpuUsagePct {
    try {
        $sample = Get-Counter '\Processor(_Total)\% Processor Time' -ErrorAction Stop
        $v = [double]$sample.CounterSamples[0].CookedValue
        if ([double]::IsNaN($v) -or [double]::IsInfinity($v)) { return $null }
        return [math]::Max(0, [math]::Min(100, [math]::Round($v, 0)))
    } catch {
        # Windows 性能计数器暂时不可用时，不阻断其它监控字段写盘
        return $null
    }
}

# 工具：原子写（temp + File.Replace），读者永远看到完整内容（照搬 FPS-Monitor.ps1）
function Write-Atomic($path, $content) {
    try {
        $tmp = $path + '.tmp'
        [System.IO.File]::WriteAllText($tmp, $content)
        if (Test-Path $path) { [System.IO.File]::Replace($tmp, $path, $null) }
        else { [System.IO.File]::Move($tmp, $path) }
    } catch {
        try { [System.IO.File]::WriteAllText($path, $content) } catch { }
    }
}
function Touch-Delete($path) {
    try { if (Test-Path $path) { Remove-Item $path -Force } } catch { }
}

# ── HWiNFO 强制恢复状态：与 autofloat 共用 YeManHWiNFO.bat 全局互斥 ──
$hwDown = $false
$recoveryAttempted = $false
$recoveryTs = 0
$RECOVERY_COOLDOWN_MS = 30000
$RECOVERY_POLL_MS = 250
$RECOVERY_POLL_COUNT = 120
$hwPrereqMissing = $false

# 主循环：每 2 秒一轮，无论有无游戏
$cycle = 0
while ($true) {
    $cycle++
    if (Test-Path $STOPFLAG) { break }
    try {
        $hw = Read-HwinfoAll

        # HWiNFO 强制复位：共享内存不可用时立即修复；只有必要文件缺失才停止。
        if (-not $hw.ok -and -not $hwPrereqMissing) {
            if (-not (Test-Path -LiteralPath $HWINFO_BAT -PathType Leaf)) {
                $hwPrereqMissing = $true
                $hwDown = $true
            } elseif (-not $recoveryAttempted -or ([DateTimeOffset]::Now.ToUnixTimeMilliseconds() - $recoveryTs) -ge $RECOVERY_COOLDOWN_MS) {
                $recoveryAttempted = $true
                $recoveryTs = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()
                # restart 统一覆盖无进程和共享内存失效，避免两套逻辑互相改变状态。
                try { & cmd.exe /c "$HWINFO_BAT restart" 2>$null } catch { }
                # HWiNFO 首次初始化共享内存可能较慢，保持同一轮恢复等待完成。
                for ($i = 0; $i -lt $RECOVERY_POLL_COUNT; $i++) {
                    $hw = Read-HwinfoAll
                    if ($hw.ok) { $hwDown = $false; break }
                    Start-Sleep -Milliseconds $RECOVERY_POLL_MS
                }
                if (-not $hw.ok) { $hwDown = $true }
            } else {
                $hwDown = $true
            }
        } else {
            $hwDown = $false
            $recoveryAttempted = $false
            $hwPrereqMissing = $false
        }

        # ── 电池数据：AC/DC + 是否存在电池；台式机与电池设备由 hasBattery 区分 ──
        $power = Get-PowerStatus
        $ac = [bool]$power.ac
        $remainMin = if ($hw.remainMin -ge 0) { [int]$hw.remainMin } else { [int]$power.remainMin }
        $hasBattery = $false
        try {
            $hasBattery = @(Get-CimInstance Win32_Battery -ErrorAction SilentlyContinue).Count -gt 0
        } catch { $hasBattery = $false }

        # ── 系统 CPU 总占用：Windows 原生（覆盖 HWiNFO 占位）──
        $cpuPct = Get-CpuUsagePct
        if ($cpuPct -ne $null) { $hw.cpuUsage = [math]::Round($cpuPct, 0) }

        # ── 组装 topmon.json（每轮都写，ts 新鲜度即守护存活判据）──
        $tdp  = [math]::Round($hw.tdpW, 1)
        $temp = [math]::Round($hw.tempC, 0)
        $chg  = [math]::Round($hw.chargeW, 1)
        $json = '{"ts":' + [DateTimeOffset]::Now.ToUnixTimeMilliseconds() +
                ',"tdpW":' + $tdp + ',"freqMhz":' + $hw.freqMhz +
                ',"tempC":' + $temp + ',"ac":' + $(if ($ac) { 1 } else { 0 }) +
                ',"hasBattery":' + $(if ($hasBattery) { 'true' } else { 'false' }) +
                ',"chargeW":' + $chg + ',"remainMin":' + $remainMin +
                ',"cpuUsage":' + [math]::Round($hw.cpuUsage, 0) +
                ',"gpuPowerW":' + [math]::Round($hw.gpuPowerW, 1) +
                ',"gpuClockMhz":' + $hw.gpuClockMhz +
                ',"hwDown":' + $(if ($hwDown) { 'true' } else { 'false' }) + '}'
        Write-Atomic $STATUS $json
    } catch {
        # 任何异常不杀死循环（照搬 FPS-Monitor.ps1）
        Start-Sleep -Milliseconds 2000
    }
    Start-Sleep -Milliseconds 2000
}

# ── 退出清理 ──
Remove-Item $STOPFLAG -Force -ErrorAction SilentlyContinue
Touch-Delete $STATUS
Remove-Item $PIDFILE -Force -ErrorAction SilentlyContinue
