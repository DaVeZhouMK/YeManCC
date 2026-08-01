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

# ── 单实例：杀掉旧实例 ──
if (Test-Path $PIDFILE) {
    $old = [int](Get-Content $PIDFILE -ErrorAction SilentlyContinue)
    if ($old -and $old -ne $PID) {
        $op = Get-Process -Id $old -ErrorAction SilentlyContinue
        if ($op -and $op.ProcessName -match "powershell") { Stop-Process -Id $old -Force }
    }
}
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

# ── HWiNFO 共享内存读取（帧率 avg/1%Low + GPU 3D 负载最大值）──
# 内存映射 "Global\HWiNFO_SENS_SM2"，签名 0x53695748("HWiS")；布局遵循 REALiX 官方 SM2 规范（#pragma pack(1)）
# Header: dwOffsetOfReadingSection@32 / dwSizeOfReadingElement@36 / dwNumReadingElements@40
# Reading 元素(460B): szLabelOrig@+12(128 ANSI) / szUnit@+268(16 ANSI) / double Value@+284 / double ValueAvg@+308
# 标签是 ANSI(单字节)，数值在 double 浮点字段（非文本）；avg/1%Low 均用 ValueAvg(运行均值)以稳定，瞬时 Value 噪声大
# GPU 3D 负载（多 GPU 取最大值）：标签含 "GPU" + 单位 "%" + (D3D Usage|Core Load|Utilization)，
#   排除 Video/Compute/显存控制器/总线/风扇/显存占用 等无关传感器；比 Win32_PerfFormattedData 准很多
function Read-HwinfoAll {
    $res = @{ avg = 0.0; p1 = 0.0; gpu = 0.0; ok = $false; err = '' }
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
                $v  = $acc.ReadDouble([long]($base + 284))   # double Value (current, 噪声大)
                $va = $acc.ReadDouble([long]($base + 308))   # double ValueAvg (running avg, 稳定)
                $cur  = $v; if ($cur  -eq 0) { $cur  = $va }   # avg: 用瞬时值（实时帧率），缺失回退运行均值
                $cur1 = $v; if ($cur1 -eq 0) { $cur1 = $va }   # 1%Low: 用瞬时值（实时），缺失回退运行均值
                if ($lab -match 'Presented' -and $lab -match '\(avg\)') {
                    $res.avg = $cur
                } elseif (($lab -match 'Presented' -and $lab -match '\(1%\)') -or ($lab -match '1% Low')) {
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
            }
        }
        if ($res.avg -gt 0 -or $res.p1 -gt 0) { $res.ok = $true }
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
# 工具：用 .NET IO 替代 PowerShell cmdlet（避免 Set-Content 在某些环境卡死文件锁）
function Write-Atomic($path, $content) {
    try { [System.IO.File]::WriteAllText($path, $content) } catch { }
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

        # HWiNFO 健康标记（仅成功读时写入时间戳；前端 30s 有效）
        if ($hw.ok) {
            Write-Atomic $HWINFO_OK ([DateTimeOffset]::Now.ToUnixTimeMilliseconds().ToString())
        } else {
            Touch-Delete $HWINFO_OK
        }

        if ($hw.ok -and $fps -gt 0) {
            # 有渲染 → 选游戏进程：工作集 > 500MB + 黑名单过滤，取最大工作集者
            $best = $null; $bestWs = 0
            foreach ($pr in (Get-Process | Where-Object { $_.WorkingSet64 -gt 524288000 })) {
                $nm = $pr.ProcessName.ToLower()
                if (-not $excl.ContainsKey($nm)) {
                    if ($pr.WorkingSet64 -gt $bestWs) { $bestWs = $pr.WorkingSet64; $best = $pr }
                }
            }
            if ($best) {
                # 有真实游戏：每 2 秒记录完整数据（用户明确要求 2 秒刷新）
                $json = '{"ts":' + [DateTimeOffset]::Now.ToUnixTimeMilliseconds() +
                        ',"fps":' + $fps + ',"fps1":' + $fps1 + ',"gpu":' + $gpu +
                        ',"game":' + $('"' + $best.ProcessName + '"') + ',"pid":' + [int]$best.Id + '}'
                Write-Atomic $STATUS $json
                # 调试日志：每 10 轮记录一次（避免日志爆炸）
                if ($cycle % 10 -eq 0) {
                    Write-Atomic $LOG ("[{0}] cycle=$cycle game={1} fps=$fps fps1=$fps1 gpu=$gpu`n" -f ([DateTimeOffset]::Now.ToString('HH:mm:ss')), $best.ProcessName)
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
