# FPS-Monitor.ps1 - YMCC 自动CPU浮动优化 后台监控守护
# 帧率来源：HWiNFO 共享内存 "Framerate Presented (avg)" / "(1%)"（不再用 RTSS，RTSS 易崩）
# GPU 来源：HWiNFO 共享内存 多 GPU 的 "D3D Usage" / "Core Load" / "Utilization" 最大值 %（比 Win32_Perf 准）
# 有真实游戏时：每 1 秒记录 {ts,fps,fps1,game,pid} 到 fps-status.json
# 未检测到游戏时：每 10 秒扫一次，且不在记录任何数据（只写心跳 fps-monitor.hb，不写状态文件）
# 真实游戏识别 = 工作集 > 500MB + 黑名单(内置+exclude.txt)，且 HWiNFO 帧率 > 0
# 停止：创建 C:\SOFT\YeMan\PowerControl\fps-monitor.stop 文件即退出
$ErrorActionPreference = "SilentlyContinue"

$DIR      = "C:\SOFT\YeMan\PowerControl"
$STATUS   = Join-Path $DIR "fps-status.json"
$STOPFLAG = Join-Path $DIR "fps-monitor.stop"
$PIDFILE  = Join-Path $DIR "fps-monitor.pid"
$HB       = Join-Path $DIR "fps-monitor.hb"
$HWINFO_OK = Join-Path $DIR "hwinfo-ok"
$EXCLUDE  = Join-Path $DIR "Sleep\exclude.txt"

# ── 单实例：杀掉所有其它运行中的本脚本实例 ──
# 旧逻辑只按 pidfile 杀「一个」旧 PID；反复重拉时旧实例未被引用→僵尸累积。
# 改为按命令行匹配杀掉全部同类 powershell 实例，保证任意时刻最多一个存活。
try {
    $me = $PID
    Get-CimInstance Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe'" -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_.ProcessId -ne $me) {
            $cl = $_.CommandLine
            if ($cl -and $cl -like '*-File*' -and $cl -like '*FPS-Monitor.ps1*') {
                Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
            }
        }
    }
} catch { }
Set-Content -Path $PIDFILE -Value $PID
if (Test-Path $STOPFLAG) { Remove-Item $STOPFLAG -Force }
# 初始心跳：证明守护已存活（前端据此区分"空闲"与"守护已死"）
Set-Content -Path $HB -Value ('{"ts":' + [DateTimeOffset]::Now.ToUnixTimeMilliseconds() + '}') -Encoding ASCII

# ── 黑名单（与 quickapp.ts SYSTEM_BLACKLIST 对齐）──
$BLACKLIST = @(
  'system','idle','csrss','winlogon','lsass','services','smss',
  # 浏览器：看视频/网页也会让 HWiNFO "Framerate Presented" > 0，被误判为游戏 → 加入黑名单
  'msedge','chrome',
  'dwm','explorer','shellhost','searchui','searchhost','runtimebroker',
  'sihost','taskhostw','fontdrvhost','conhost','rundll32',
  'msedgewebview2','applicationframehost','startmenuexperiencehost',
  'peopleexperiencehost','systemsettings','lockapp','audiodg',
  'svchost','nvcontainer','nvdisplaycontainer','nvdisplay',
  'rtkauduservice64','yemancc','yemantdpctl','workbuddy',
  'uuremote','uuremotefe','uur','neteaseuu','sunloginclient',
  'teamviewer','anydesk','todesk','rtss','hwinfo64','gameviewer'
)
function Load-Exclude {
    $set = @{}
    foreach ($b in $BLACKLIST) { $set[$b] = $true }
    if (Test-Path $EXCLUDE) {
        foreach ($line in (Get-Content $EXCLUDE)) {
            $t = $line.Trim()
            if ($t -and -not $t.StartsWith('#')) { $set[($t -replace '\.exe$','').ToLower()] = $true }
        }
    }
    return $set
}

# ── HWiNFO 共享内存读取（帧率 avg/1%Low + GPU 3D 负载 + CPU Package Power）──
# 内存映射 "Global\HWiNFO_SENS_SM2"，签名 0x53695748("HWiS")；布局遵循 REALiX 官方 SM2 规范（#pragma pack(1)）
# Header: dwOffsetOfReadingSection@32 / dwSizeOfReadingElement@36 / dwNumReadingElements@40
# Reading 元素(460B): szLabelOrig@+12(128 ANSI) / szUnit@+268(16 ANSI) / double Value@+284 / double ValueAvg@+308
# 标签是 ANSI(单字节)，数值在 double 浮点字段（非文本）；avg/1%Low 均用 ValueAvg(运行均值)以稳定，瞬时 Value 噪声大
# GPU 3D 负载（多 GPU 取最大值）：标签含 "GPU" + 单位 "%" + (D3D Usage|Core Load|Utilization)，
#   排除 Video/Compute/显存控制器/总线/风扇/显存占用 等无关传感器；比 Win32_PerfFormattedData 准很多
function Read-HwinfoAll {
    $res = @{ avg = 0.0; p1 = 0.0; gpu = 0.0; packagePower = 0.0; ok = $false; err = ''; presentedSensor = $false }
    $mmf = $null
    foreach ($nm in @('Global\HWiNFO_SENS_SM2','HWiNFO_SENS_SM2','Global\HWiNFO_SENS_SM','HWiNFO_SENS_SM')) {
        try { $mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting($nm); if ($mmf) { break } } catch {}
    }
    if (-not $mmf) { $res.err = 'no HWiNFO shared memory'; return $res }
    try {
        $acc = $mmf.CreateViewAccessor(0, 0)
        $sig = $acc.ReadUInt32(0)
        if ($sig -ne 0x53695748) { $res.err = 'bad HWiNFO signature'; return $res }
        # pack(1) header: dwOffsetOfReadingSection@32 / dwSizeOfReadingElement@36 / dwNumReadingElements@40
        $offReading = $acc.ReadUInt32(32)
        $szReading  = $acc.ReadUInt32(36)
        $numReading = $acc.ReadUInt32(40)
        if ($szReading -lt 300) { $res.err = 'unexpected reading element size'; return $res }
        # reading element(460B, pack=1): szLabelOrig@+12(128 ANSI) / szUnit@+268(16 ANSI) / double Value@+284 / ValueAvg@+308
        $enc = [System.Text.Encoding]::ASCII
        $buf = New-Object byte[] 128
        $bufU = New-Object byte[] 16
        for ($i = 0; $i -lt $numReading; $i++) {
            $base = [long]($offReading + $i * $szReading)
            $acc.ReadArray([long]($base + 12), $buf, 0, 128) | Out-Null   # szLabelOrig (ANSI)
            $lab = $enc.GetString($buf).TrimEnd([char]0)
            if ($lab -match 'Framerate') {
                $v  = $acc.ReadDouble([long]($base + 284))
                $va = $acc.ReadDouble([long]($base + 308))
                $cur  = if (-not [double]::IsNaN($v) -and -not [double]::IsInfinity($v)) { $v } else { 0 }
                $cur1 = if (-not [double]::IsNaN($v) -and -not [double]::IsInfinity($v)) { $v } else { 0 }
                if ($lab -match 'Presented' -and $lab -match '\(avg\)') {
                    $res.presentedSensor = $true
                    if ($cur -le 0 -and -not [double]::IsNaN($va) -and -not [double]::IsInfinity($va)) { $cur = $va }
                    $res.avg = $cur
                } elseif (($lab -match 'Presented' -and $lab -match '\(1%\)') -or ($lab -match '1% Low')) {
                    $res.presentedSensor = $true
                    if ($cur1 -le 0 -and -not [double]::IsNaN($va) -and -not [double]::IsInfinity($va)) { $cur1 = $va }
                    if ($res.p1 -eq 0) { $res.p1 = $cur1 }
                }
            } elseif ($lab -match 'GPU' -and $lab -notmatch 'Video|Compute|Memory Controller|Bus Load|Busy|Memory Usage|Fan') {
                # GPU 3D 负载候选：读单位(%)，匹配 D3D Usage / Core Load / Utilization
                $acc.ReadArray([long]($base + 268), $bufU, 0, 16) | Out-Null   # szUnit (ANSI)
                $unit = $enc.GetString($bufU).TrimEnd([char]0)
                if ($unit -eq '%' -and $lab -match 'D3D Usage|Core Load|Utilization') {
                    $gv = $acc.ReadDouble([long]($base + 284))   # 当前负载(%)，取多 GPU 最大值
                    if ($gv -gt $res.gpu) { $res.gpu = $gv }
                }
            } elseif ($lab -match '(?i)CPU.*(Package|Pkg).*Power|Package Power.*CPU' -and $lab -notmatch '(?i)Core|CCD|SoC|GPU') {
                # CPU Package Power：只取整颗 CPU 封装功耗，避免误读单核心/CCD/SoC 功耗
                $acc.ReadArray([long]($base + 268), $bufU, 0, 16) | Out-Null
                $unit = $enc.GetString($bufU).TrimEnd([char]0)
                if ($unit -match '(?i)^W$|Watts?') {
                    $pv = $acc.ReadDouble([long]($base + 284))
                    if ($pv -gt 0) { $res.packagePower = $pv }
                }
            }
        }
        if ($res.presentedSensor) { $res.ok = $true }
        else { $res.err = 'Framerate Presented sensors not found' }
    } finally {
        if ($acc) { $acc.Dispose() }
        $mmf.Dispose()
    }
    return $res
}

# ── 主循环 ──
$excl = Load-Exclude
$LOG  = Join-Path $DIR "fps-monitor.log"
# 游戏进程枚举缓存：Get-Process 全枚举（提权下遍历全系统进程+查工作集）很重，约 200ms+ 系统级瞬时负载。
# 若每 2 秒跑一次，YeManCC 的 UI 线程消息泵会被判定持续繁忙 → 鼠标旁出现 IDC_APPSTARTING 转圈。
# 且此分支只在 HWiNFO 提供帧率时才进入（$hw.ok -and $fps -gt 0）——正好解释「只有 浮动+HWiNFO64 开才转圈」。
# 用户要求：游戏进程还在 → 不再搜索、直接继续；游戏没了 → 每 GAME_ENUM_MS 才搜一次。
$gameCacheId = 0; $gameCacheName = $null; $gameCacheTs = 0
$GAME_ENUM_MS = 30000
# 工具：原子写（temp 写入 + File.Replace 替换），读者永远看到旧值或新值，不会读到
# 截断半截内容。原 File.WriteAllText 先截断再写，前端每秒读 hb/status 时若撞上截断窗口会
# 读到空/半截→JSON.parse 失败→readStatus 返回 null→前端误判守护死亡→反复重拉（抖动根因）。
function Write-Atomic($path, $content) {
    try {
        $tmp = $path + '.tmp'
        [System.IO.File]::WriteAllText($tmp, $content)
        if (Test-Path $path) {
            [System.IO.File]::Replace($tmp, $path, $null)  # 原子替换（NTFS MoveFileEx）
        } else {
            [System.IO.File]::Move($tmp, $path)
        }
    } catch {
        # 极端情况回退：直接写（仍可能被截断，但至少尽力）
        try { [System.IO.File]::WriteAllText($path, $content) } catch { }
    }
}
function Touch-Delete($path) {
    try { if (Test-Path $path) { Remove-Item $path -Force } } catch { }
}

$cycle = 0
while ($true) {
    $cycle++
    if (Test-Path $STOPFLAG) { break }
    try {
        # 心跳（每轮都写，证明守护存活；前端据此区分"空闲"与"守护已死"）
        Write-Atomic $HB ('{"ts":' + [DateTimeOffset]::Now.ToUnixTimeMilliseconds() + '}')

        # HWiNFO 共享内存读取（帧率 + GPU）
        $hw = Read-HwinfoAll
        $fps  = [math]::Round($hw.avg, 1)
        $fps1 = [math]::Round($hw.p1, 1)
        $gpu  = [math]::Round($hw.gpu, 0)
        $packagePower = [math]::Round($hw.packagePower, 1)

        # HWiNFO 健康标记：共享内存/header/目标传感器可读即健康，FPS 为 0 不代表 HWiNFO 故障。
        if ($hw.ok) {
            Write-Atomic $HWINFO_OK ([DateTimeOffset]::Now.ToUnixTimeMilliseconds().ToString())
        } else {
            Touch-Delete $HWINFO_OK
        }

        if ($hw.ok -and $fps -gt 0) {
            # 有渲染 → 选游戏进程：工作集 > 500MB + 黑名单过滤，取最大工作集者
            # ⚠ 全进程枚举很重（提权遍历全系统进程+查工作集）。用户要求：游戏进程还在 → 不再搜索直接继续；
            #   游戏没了 → 每 GAME_ENUM_MS(30s) 才搜一次。缓存窗口内的存活校验用轻量 Get-Process -Id（毫秒级）。
            $nowMs = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()
            if ($gameCacheId -le 0) {
                # 无缓存游戏 → 冷却期外才全枚举搜索新游戏
                if ($nowMs - $gameCacheTs -gt $GAME_ENUM_MS) {
                    $gameCacheTs = $nowMs
                    $best = $null; $bestWs = 0
                    foreach ($pr in (Get-Process | Where-Object { $_.WorkingSet64 -gt 524288000 })) {
                        $nm = $pr.ProcessName.ToLower()
                        if (-not $excl.ContainsKey($nm)) {
                            if ($pr.WorkingSet64 -gt $bestWs) { $bestWs = $pr.WorkingSet64; $best = $pr }
                        }
                    }
                    if ($best) {
                        $gameCacheName = $best.ProcessName; $gameCacheId = [int]$best.Id
                    }
                }
            } elseif ($nowMs - $gameCacheTs -gt $GAME_ENUM_MS) {
                # 有缓存游戏且到 30s：仅轻量单进程校验存活；仍在 → 直接继续（不枚举）；死了 → 清缓存走搜索
                $gameCacheTs = $nowMs
                $chk = Get-Process -Id $gameCacheId -ErrorAction SilentlyContinue
                if (-not $chk) { $gameCacheId = 0; $gameCacheName = $null }
            }
            if ($gameCacheId -gt 0) {
                # 有真实游戏：每 2 秒记录完整数据（用户明确要求 2 秒刷新）
                $json = '{"ts":' + [DateTimeOffset]::Now.ToUnixTimeMilliseconds() +
                        ',"fps":' + $fps + ',"fps1":' + $fps1 + ',"gpu":' + $gpu +
                        ',"packagePower":' + $packagePower +
                        ',"game":' + $('"' + $gameCacheName + '"') + ',"pid":' + $gameCacheId + '}'
                Write-Atomic $STATUS $json
                # 调试日志：每 10 轮记录一次（避免日志爆炸）
                if ($cycle % 10 -eq 0) {
                    Write-Atomic $LOG ("[{0}] cycle=$cycle game={1} fps=$fps fps1=$fps1 gpu=$gpu packagePower=$packagePower`n" -f ([DateTimeOffset]::Now.ToString('HH:mm:ss')), $gameCacheName)
                }
                Start-Sleep -Milliseconds 2000
                continue
            }
        }

        # 未检测到游戏（HWiNFO 无帧率 / 无候选）：2 秒扫一次（用户要求；旧版 10s 太慢）
        Touch-Delete $STATUS
        Start-Sleep -Milliseconds 2000
    } catch {
        # 任何异常都不杀死循环，仅记录到日志
        Write-Atomic $LOG ("[{0}] ERROR cycle=$cycle $_`n" -f ([DateTimeOffset]::Now.ToString('HH:mm:ss')))
        Start-Sleep -Milliseconds 2000
    }
}

Remove-Item $STOPFLAG -Force -ErrorAction SilentlyContinue
Touch-Delete $STATUS
Touch-Delete $HB
Touch-Delete $HWINFO_OK
Remove-Item $PIDFILE -Force -ErrorAction SilentlyContinue
