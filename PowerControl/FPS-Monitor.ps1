# FPS-Monitor.ps1 - YMCC 自动CPU浮动优化 后台监控守护
# 帧率来源：HWiNFO 共享内存 "Framerate Presented (avg)" / "(1%)"（不再用 RTSS，RTSS 易崩）
# GPU 来源：HWiNFO 共享内存 多 GPU 的 "D3D Usage" / "Core Load" / "Utilization" 最大值 %（比 Win32_Perf 准）
# 有真实游戏时：每 1 秒记录 {ts,fps,fps1,game,pid} 到 fps-status.json
# 未检测到游戏时：不记录状态数据（只写心跳 fps-monitor.hb，不写状态文件）
# 游戏识别由 native 游戏识别阀门完成；本脚本只读取唯一 PID 快照，且 HWiNFO 帧率 > 0 才输出
# 停止：创建 C:\SOFT\YeMan\PowerControl\fps-monitor.stop 文件即退出
$ErrorActionPreference = "SilentlyContinue"

$DIR      = "C:\SOFT\YeMan\PowerControl"
$STATUS   = Join-Path $DIR "fps-status.json"
$STOPFLAG = Join-Path $DIR "fps-monitor.stop"
$PIDFILE  = Join-Path $DIR "fps-monitor.pid"
$HB       = Join-Path $DIR "fps-monitor.hb"
$HWINFO_OK = Join-Path $DIR "hwinfo-ok"
$GAME_TARGET = Join-Path $DIR "game-target.json"

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

# 游戏识别阀门是唯一的候选选择器。本脚本只消费 native 发布的快照，
# 并用 PID + 进程创建时间复核，快照不存在或过期时安全地视为无游戏。
function Test-GameValveInteger($Value) {
  return ($Value -is [int] -or $Value -is [long])
}

function Assert-GameValvePlainPath([string]$Path) {
  $current = [IO.Path]::GetFullPath($Path)
  while ($current) {
    try {
      if (([IO.File]::GetAttributes($current) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse path refused' }
    } catch [IO.FileNotFoundException] { } catch [IO.DirectoryNotFoundException] { }
    $parent = [IO.Path]::GetDirectoryName($current)
    if ($parent -eq $current) { break }
    $current = $parent
  }
}

function Read-GameValveBytes([string]$Path, [int]$Limit = 65536) {
  Assert-GameValvePlainPath $Path
  $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::Read -bor [IO.FileShare]::Delete))
  try {
    $length = $stream.Length
    if ($length -le 0 -or $length -gt $Limit) { throw 'Empty or oversized valve document' }
    $bytes = New-Object byte[] ([int]$length)
    $offset = 0
    while ($offset -lt $bytes.Length) {
      $count = $stream.Read($bytes, $offset, $bytes.Length - $offset)
      if ($count -le 0) { throw 'Incomplete valve document' }
      $offset += $count
    }
    if ($stream.Length -ne $length) { throw 'Valve document changed during read' }
    return ,$bytes
  } finally { $stream.Dispose() }
}

function ConvertFrom-GameValveJson([byte[]]$Bytes) {
  $text = ([Text.UTF8Encoding]::new($false, $true)).GetString($Bytes).TrimStart([char]0xFEFF)
  if (-not $text.TrimStart().StartsWith('{')) { throw 'Valve root must be an object' }
  # Bound structure before full deserialization. Windows PowerShell also accepts
  # non-JSON NaN/Infinity primitives and can silently keep the last decoded key.
  # The built-in parser still validates grammar after this bounded lexical pass.
  $stack = [Collections.Generic.List[object]]::new()
  $tokens = [regex]::Matches($text, '"(?:\\.|[^"\\])*"|[{}\[\],:]|[^ \t\r\n{}\[\],:"]+')
  if ($tokens.Count -gt 20000) { throw 'Valve JSON complexity refused' }
  $cursor = 0
  foreach ($token in $tokens) {
    if ($text.Substring($cursor, $token.Index - $cursor) -cnotmatch '\A[ \t\r\n]*\z') { throw 'Invalid valve JSON token' }
    $cursor = $token.Index + $token.Length
    $part = $token.Value
    if ($part -eq '{' -or $part -eq '[') {
      if ($stack.Count -ge 32) { throw 'Valve JSON depth refused' }
      $stack.Add([pscustomobject]@{ Object = ($part -eq '{'); Key = ($part -eq '{'); Keys = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase) })
    } elseif ($part -eq '}' -or $part -eq ']') {
      if (-not $stack.Count) { throw 'Unbalanced valve JSON' }
      $stack.RemoveAt($stack.Count - 1)
    } elseif ($part -ne ',' -and $part -ne ':' -and -not $part.StartsWith('"')) {
      if ($part -cnotmatch '\A(?:true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)\z') { throw 'Invalid valve JSON primitive' }
      if ($part -cmatch '\A[-0-9]') {
        $number = 0.0
        if (-not [double]::TryParse($part, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
            [double]::IsNaN($number) -or [double]::IsInfinity($number)) { throw 'Non-finite valve JSON number' }
      }
    } elseif ($stack.Count) {
      $frame = $stack[$stack.Count - 1]
      if ($part -eq ',') { $frame.Key = $frame.Object }
      elseif ($part -eq ':') { $frame.Key = $false }
      elseif ($part.StartsWith('"') -and $frame.Object -and $frame.Key) {
        $key = ConvertFrom-Json -InputObject $part -ErrorAction Stop
        if (-not $frame.Keys.Add($key)) { throw 'Duplicate decoded valve key' }
        $frame.Key = $false
      }
    }
  }
  if ($stack.Count -or $text.Substring($cursor) -cnotmatch '\A[ \t\r\n]*\z') { throw 'Incomplete valve JSON' }
  return ConvertFrom-Json -InputObject $text -ErrorAction Stop
}

function Get-GameValveImageKey($Path) {
  if ($Path -isnot [string] -or $Path -notmatch '^[A-Za-z]:[\\/]' -or $Path.Contains([char]0)) { throw 'Unknown valve image' }
  return [IO.Path]::GetFullPath($Path).TrimEnd('\').ToLowerInvariant()
}

function Read-GameValveTarget {
  try {
    $bytes = Read-GameValveBytes $GAME_TARGET
    $target = ConvertFrom-GameValveJson $bytes
    if ($target.valid -isnot [bool] -or -not $target.valid -or
        -not (Test-GameValveInteger $target.pid) -or $target.pid -le 0 -or $target.pid -gt [int]::MaxValue -or
        -not (Test-GameValveInteger $target.generation) -or $target.generation -le 0 -or
        -not (Test-GameValveInteger $target.lastSeen) -or $target.lastSeen -le 0) { return $null }
    if ($target.PSObject.Properties['schemaVersion'] -and
        (-not (Test-GameValveInteger $target.schemaVersion) -or $target.schemaVersion -ne 1)) { return $null }
    if ($target.identityPolicy -isnot [string] -or $target.identityPolicy -cne 'ymcc-original-game-identity-v2') { return $null }
    $created = 0L
    if ($target.processCreated -isnot [string] -or $target.processCreated -notmatch '^[1-9][0-9]{0,18}$' -or
        -not [long]::TryParse($target.processCreated, [ref]$created) -or $created -le 0) { return $null }
    $image = Get-GameValveImageKey $target.path
    $age = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - $target.lastSeen
    if ($age -lt 0 -or $age -gt 10000) { return $null }
    $process = Get-Process -Id ([int]$target.pid) -ErrorAction Stop
    if (-not $process -or [long]$process.StartTime.ToFileTimeUtc() -ne $created -or
        (Get-GameValveImageKey $process.Path) -cne $image) { return $null }
    $again = Read-GameValveBytes $GAME_TARGET
    if ([Convert]::ToBase64String($bytes) -cne [Convert]::ToBase64String($again)) { return $null }
    $age = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - $target.lastSeen
    if ($age -lt 0 -or $age -gt 10000) { return $null }
    return $target
  } catch { return $null }
}


# ── HWiNFO 共享内存读取（帧率 avg/1%Low + GPU 3D 负载 + CPU Package Power）──
# 内存映射 "Global\HWiNFO_SENS_SM2"，签名 0x53695748("HWiS")；布局遵循 REALiX 官方 SM2 规范（#pragma pack(1)）
# Header: dwOffsetOfReadingSection@32 / dwSizeOfReadingElement@36 / dwNumReadingElements@40
# Reading 元素(460B): szLabelOrig@+12(128 ANSI) / szUnit@+268(16 ANSI) / double Value@+284 / double ValueAvg@+308
# 标签是 ANSI(单字节)，数值在 double 浮点字段（非文本）。
# GPU 3D 负载（多 GPU 取最大值）：标签含 "GPU" + 单位 "%" + (D3D Usage|Core Load|Utilization)，
#   排除 Video/Compute/显存控制器/总线/风扇/显存占用 等无关传感器；比 Win32_PerfFormattedData 准很多
function Read-HwinfoAll {
    $res = @{ avg = 0.0; p1 = 0.0; gpu = 0.0; packagePower = 0.0; ok = $false; err = ''; presentedSensor = $false }
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
        # pack(1) header: dwOffsetOfReadingSection@32 / dwSizeOfReadingElement@36 / dwNumReadingElements@40
        $offReading = $acc.ReadUInt32(32)
        $szReading  = $acc.ReadUInt32(36)
        $numReading = $acc.ReadUInt32(40)
        if ($szReading -lt 316 -or $numReading -eq 0 -or $numReading -gt 100000) { $res.err = 'unexpected reading section'; return $res }
        $endReading = [uint64]$offReading + ([uint64]$szReading * [uint64]$numReading)
        if ($offReading -ge [uint64]$acc.Capacity -or $endReading -gt [uint64]$acc.Capacity) { $res.err = 'reading section outside mapping'; return $res }
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
        # 共享内存健康不依赖特定 FPS 传感器；没有 Presented 传感器时仅停止游戏识别。
        $res.ok = $true
    } finally {
        if ($acc) { $acc.Dispose() }
        $mmf.Dispose()
    }
    return $res
}

# ── 主循环 ──
$LOG  = Join-Path $DIR "fps-monitor.log"
# 工具：原子写（temp 写入 + File.Replace 替换），读者永远看到旧值或新值，不会读到
# 截断半截内容。原 File.WriteAllText 先截断再写，前端每秒读 hb/status 时若撞上截断窗口会
# 读到空/半截→JSON.parse 失败→readStatus 返回 null→前端误判守护死亡→反复重拉（抖动根因）。
function Read-FpsWriteSnapshot([string]$Path, [int]$Limit = 262144) {
    Assert-GameValvePlainPath $Path
    try { $attributes = [IO.File]::GetAttributes($Path) }
    catch [IO.FileNotFoundException] { return $null }
    catch [IO.DirectoryNotFoundException] { return $null }
    if ($attributes -band [IO.FileAttributes]::Directory) { throw 'Status path is a directory' }
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::Read -bor [IO.FileShare]::Delete))
    try {
        $length = $stream.Length
        if ($length -lt 0 -or $length -gt $Limit) { throw 'Status document exceeds read budget' }
        $bytes = New-Object byte[] ([int]$length)
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $count = $stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($count -le 0) { throw 'Incomplete status snapshot' }
            $offset += $count
        }
        if ($stream.Length -ne $length) { throw 'Status changed during read' }
    } finally { $stream.Dispose() }
    return [pscustomobject]@{ Bytes = $bytes; Token = [Convert]::ToBase64String($bytes) }
}

function Test-FpsWriteSnapshot($Expected, $Actual) {
    if ($null -eq $Expected -or $null -eq $Actual) { return ($null -eq $Expected -and $null -eq $Actual) }
    return $Expected.Token -ceq $Actual.Token
}

function Write-FpsStage([string]$Path, [byte[]]$Bytes, $Created = $null) {
    Assert-GameValvePlainPath $Path
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    if ($null -ne $Created) { $Created.Value = $true }
    try { $stream.Write($Bytes, 0, $Bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    $saved = Read-FpsWriteSnapshot $Path
    if ($null -eq $saved -or $saved.Token -cne [Convert]::ToBase64String($Bytes)) { throw 'Status stage readback failed' }
}

function Write-Atomic($path, $content) {
    # Keep the existing call sites silent; the checked receipt is available to
    # the caller without treating a failed write as a saved status.
    $script:LastGameValveWriteReceipt = [pscustomobject]@{ Persisted = $false; Error = ''; PreservedPath = ''; Path = [string]$path }
    $stage = $null; $stageOwned = $false; $preserved = ''
    try {
        $destination = [IO.Path]::GetFullPath([string]$path)
        $root = [IO.Path]::GetFullPath($DIR).TrimEnd('\')
        if ([IO.Path]::GetDirectoryName($destination).TrimEnd('\') -ine $root) { throw 'Status destination is outside the PowerControl root' }
        Assert-GameValvePlainPath $destination
        $isStatus = $STATUS -and $destination -ieq [IO.Path]::GetFullPath($STATUS)
        $utf8 = [Text.UTF8Encoding]::new($false, $true)
        $bytes = $utf8.GetBytes([string]$content)
        if ($bytes.Length -gt 262144) { throw 'Status write budget exceeded' }
        $before = Read-FpsWriteSnapshot $destination
        $corrupt = $false
        if ($isStatus) {
            # New telemetry is an original observed target, never an authority
            # inferred from an unreadable or incompatible old document.
            $incoming = ConvertFrom-GameValveJson $bytes
            if ($incoming.PSObject.Properties['schemaVersion'] -and
                (-not (Test-GameValveInteger $incoming.schemaVersion) -or $incoming.schemaVersion -ne 1)) { throw 'Unsupported incoming status schema' }
            if ($null -ne $before) {
                $previous = $null
                try { $previous = ConvertFrom-GameValveJson $before.Bytes }
                catch { $corrupt = $true }
                if (-not $corrupt -and $previous.PSObject.Properties['schemaVersion'] -and
                    (-not (Test-GameValveInteger $previous.schemaVersion) -or $previous.schemaVersion -ne 1)) { throw 'Future or unknown status schema preserved' }
            }
        }
        $stage = $destination + '.tmp-' + [guid]::NewGuid().ToString('N')
        Write-FpsStage $stage $bytes ([ref]$stageOwned)
        if (-not (Test-FpsWriteSnapshot $before (Read-FpsWriteSnapshot $destination))) { throw 'Another writer changed status before commit' }
        if ($corrupt) {
            # Move the original file, preserving its bytes even for invalid UTF8.
            $preserved = $destination + '.corrupt-' + [guid]::NewGuid().ToString('N')
            [IO.File]::Move($destination, $preserved)
            [IO.File]::Move($stage, $destination)
        } elseif ($null -ne $before) {
            [IO.File]::Replace($stage, $destination, [NullString]::Value)
        } else {
            [IO.File]::Move($stage, $destination)
        }
        $stage = $null
        $saved = Read-FpsWriteSnapshot $destination
        if ($null -eq $saved -or $saved.Token -cne [Convert]::ToBase64String($bytes)) { throw 'Status readback differs; save unconfirmed' }
        $script:LastGameValveWriteReceipt = [pscustomobject]@{ Persisted = $true; Error = ''; PreservedPath = $preserved; Path = $destination }
    } catch {
        $script:LastGameValveWriteReceipt = [pscustomobject]@{ Persisted = $false; Error = $_.Exception.Message; PreservedPath = $preserved; Path = [string]$path }
    } finally {
        if ($stage -and $stageOwned) { try { [IO.File]::Delete($stage) } catch { } }
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
            # FPS is an output condition only. The native game recognition
            # valve has already selected the sole PID; this compatibility
            # script must not enumerate or choose another process.
            $target = Read-GameValveTarget
            if ($target) {
                $targetName = [string]$target.name
                $json = [ordered]@{
                    ts = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()
                    fps = $fps
                    fps1 = $fps1
                    gpu = $gpu
                    packagePower = $packagePower
                    game = $targetName
                    pid = [int]$target.pid
                } | ConvertTo-Json -Compress
                Write-Atomic $STATUS $json
                if ($cycle % 10 -eq 0) {
                    Write-Atomic $LOG ("[{0}] cycle=$cycle game={1} pid={2} fps=$fps fps1=$fps1 gpu=$gpu packagePower=$packagePower`n" -f ([DateTimeOffset]::Now.ToString('HH:mm:ss')), $targetName, $target.pid)
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
