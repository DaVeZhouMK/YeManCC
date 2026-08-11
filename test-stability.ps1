# YeManCC WebView2 Stability / Resource Regression Test
#
# 目标：区分崩溃、正常退出、托盘隐藏常驻和测试清理；同时记录 YeManCC
# 及其 WebView2 子进程的工作集、私有内存、句柄、线程和 CPU 累积值。

param(
    [int]$Cycles = 1,
    [int]$SecondsPerCycle = 120,
    [int]$SampleSeconds = 5,
    [string]$ExePath = "C:\SOFT\YeMan\YeManCC\YeManCC.exe",
    [string]$ReportPath = "C:\SOFT\YeMan\YeManCC4\YeManCC3\stability-report.jsonl",
    [switch]$KeepResident,
    [switch]$NoPreCleanup
)

$ErrorActionPreference = "Continue"
$report = [System.Collections.Generic.List[string]]::new()
$normalCount = 0
$residentCount = 0
$crashCount = 0
$unexpectedExit = 0

function Log([string]$Message, [string]$Level = "INFO") {
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $line = "[$ts] [$Level] $Message"
    Write-Host $line
    $script:report.Add($line)
}

function Get-ProcessTree([int]$RootPid) {
    $all = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
    $children = @{}
    foreach ($p in $all) {
        $parent = [int]$p.ParentProcessId
        if (-not $children.ContainsKey($parent)) { $children[$parent] = [System.Collections.Generic.List[object]]::new() }
        $children[$parent].Add($p)
    }
    $queue = [System.Collections.Queue]::new()
    $seen = [System.Collections.Generic.HashSet[int]]::new()
    $queue.Enqueue($RootPid)
    $result = [System.Collections.Generic.List[object]]::new()
    while ($queue.Count -gt 0) {
        $nodePid = [int]$queue.Dequeue()
        if (-not $seen.Add($nodePid)) { continue }
        if ($nodePid -ne $RootPid) {
            $match = $all | Where-Object { [int]$_.ProcessId -eq $nodePid } | Select-Object -First 1
            if ($null -ne $match) { $result.Add($match) }
        }
        if ($children.ContainsKey($nodePid)) {
            foreach ($child in $children[$nodePid]) { $queue.Enqueue([int]$child.ProcessId) }
        }
    }
    return @($result)
}

function Get-TreeSnapshot([int]$RootPid) {
    $ids = [System.Collections.Generic.List[int]]::new()
    $ids.Add($RootPid)
    foreach ($p in @(Get-ProcessTree $RootPid)) { $ids.Add([int]$p.ProcessId) }

    $working = 0L
    $private = 0L
    $handles = 0
    $threads = 0
    $cpuSeconds = 0.0
    $rows = [System.Collections.Generic.List[object]]::new()
    foreach ($id in $ids) {
        try {
            $p = Get-Process -Id $id -ErrorAction Stop
            $cpu = 0.0
            try { $cpu = $p.TotalProcessorTime.TotalSeconds } catch {}
            $row = [pscustomobject]@{
                pid = $id
                name = $p.ProcessName
                workingMB = [math]::Round($p.WorkingSet64 / 1MB, 1)
                privateMB = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
                handles = $p.HandleCount
                threads = $p.Threads.Count
                cpuSeconds = [math]::Round($cpu, 3)
            }
            $rows.Add($row)
            $working += [int64]$p.WorkingSet64
            $private += [int64]$p.PrivateMemorySize64
            $handles += [int]$p.HandleCount
            $threads += [int]$p.Threads.Count
            $cpuSeconds += $cpu
        } catch {}
    }
    return [pscustomobject]@{
        ts = (Get-Date).ToString("o")
        pid = $RootPid
        processCount = $rows.Count
        workingMB = [math]::Round($working / 1MB, 1)
        privateMB = [math]::Round($private / 1MB, 1)
        handles = $handles
        threads = $threads
        cpuSeconds = [math]::Round($cpuSeconds, 3)
        processes = @($rows)
    }
}

function Get-NewCrashDumps([datetime]$Since) {
    $root = Join-Path $env:LOCALAPPDATA "YeManCC\EBWebView"
    $dirs = @(
        (Join-Path $root "Crashpad\reports"),
        (Join-Path $root "crashpad\reports")
    )
    $dumps = @()
    foreach ($dir in $dirs) {
        if (Test-Path -LiteralPath $dir) {
            $dumps += @(Get-ChildItem -LiteralPath $dir -Filter "*.dmp" -File -ErrorAction SilentlyContinue |
                Where-Object { $_.CreationTime -gt $Since })
        }
    }
    return @($dumps | Sort-Object CreationTime -Unique)
}

function Stop-TestTree([int]$RootPid) {
    $children = @(Get-ProcessTree $RootPid)
    foreach ($p in $children | Sort-Object { [int]$_.ParentProcessId } -Descending) {
        Stop-Process -Id ([int]$p.ProcessId) -Force -ErrorAction SilentlyContinue
    }
    Stop-Process -Id $RootPid -Force -ErrorAction SilentlyContinue
}

function Write-Record($Object) {
    $script:report.Add(($Object | ConvertTo-Json -Depth 5 -Compress))
}

Log "YeManCC stability/resource test START"
Log "Cycles=$Cycles SecondsPerCycle=$SecondsPerCycle SampleSeconds=$SampleSeconds"
Log "Exe=$ExePath"
Log "Profile=$env:LOCALAPPDATA\YeManCC\EBWebView"

if (-not (Test-Path -LiteralPath $ExePath)) {
    Log "EXE not found: $ExePath" "FATAL"
    exit 1
}

if (-not $NoPreCleanup) {
    foreach ($p in @(Get-Process -Name YeManCC -ErrorAction SilentlyContinue)) {
        Log "Pre-clean exact YeManCC PID=$($p.Id)"
        Stop-TestTree ([int]$p.Id)
    }
    Start-Sleep -Seconds 2
}

for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
    $cycleStart = Get-Date
    $cycleResident = $false
    $cycleNormal = $false
    Log "--- Cycle $cycle / $Cycles ---"
    try {
        $proc = Start-Process -FilePath $ExePath -PassThru -ErrorAction Stop
    } catch {
        Log "Launch failed: $_" "FAIL"
        $unexpectedExit++
        continue
    }
    Log "PID=$($proc.Id) launched"

    $first = $null
    $previous = $null
    $peakWorking = 0.0
    $peakPrivate = 0.0
    $peakHandles = 0
    $peakThreads = 0
    $elapsed = 0

    while ($elapsed -lt $SecondsPerCycle) {
        Start-Sleep -Seconds $SampleSeconds
        $elapsed += $SampleSeconds
        $proc.Refresh()
        if ($proc.HasExited) {
            $dumps = @(Get-NewCrashDumps $cycleStart)
            if ($dumps.Count -gt 0) {
                Log "Process exited early with new Crashpad dump(s): $($dumps.Count)" "CRASH"
                $crashCount++
            } else {
                Log "Process exited early without Crashpad dump. ExitCode=$($proc.ExitCode)" "EXIT"
                $unexpectedExit++
            }
            break
        }
        $snap = Get-TreeSnapshot ([int]$proc.Id)
        if ($null -eq $first) { $first = $snap }
        if ($null -ne $previous) {
            $dt = [math]::Max(0.1, $SampleSeconds)
            $snap | Add-Member -NotePropertyName treeCpuPctOneCore -NotePropertyValue ([math]::Round((($snap.cpuSeconds - $previous.cpuSeconds) / $dt) * 100, 2))
        } else {
            $snap | Add-Member -NotePropertyName treeCpuPctOneCore -NotePropertyValue $null
        }
        $previous = $snap
        $peakWorking = [math]::Max($peakWorking, [double]$snap.workingMB)
        $peakPrivate = [math]::Max($peakPrivate, [double]$snap.privateMB)
        $peakHandles = [math]::Max($peakHandles, [int]$snap.handles)
        $peakThreads = [math]::Max($peakThreads, [int]$snap.threads)
        Write-Record ([pscustomobject]@{ type="sample"; cycle=$cycle; elapsed=$elapsed; snapshot=$snap })
        if (($elapsed % 30) -eq 0) {
            Log "alive ${elapsed}s | tree WS=$($snap.workingMB)MB Private=$($snap.privateMB)MB Handles=$($snap.handles) Threads=$($snap.threads) CPU1core=$($snap.treeCpuPctOneCore)%"
        }
    }

    if (-not $proc.HasExited) {
        try { $proc.CloseMainWindow() | Out-Null } catch {}
        if ($proc.WaitForExit(5000)) {
            Log "Graceful process exit confirmed"
            $normalCount++
            $cycleNormal = $true
        } else {
            $proc.Refresh()
            if ($proc.HasExited) {
                Log "Process exited during close wait"
                $normalCount++
                $cycleNormal = $true
            } else {
                Log "CloseMainWindow left YeManCC resident (likely tray hide); not counted as normal exit" "RESIDENT"
                $residentCount++
                $cycleResident = $true
                if (-not $KeepResident) {
                    Stop-TestTree ([int]$proc.Id)
                    Start-Sleep -Seconds 2
                }
            }
        }
    }

    Write-Record ([pscustomobject]@{
        type="cycle-summary"; cycle=$cycle; normal=$cycleNormal; resident=$cycleResident
        peakWorkingMB=$peakWorking; peakPrivateMB=$peakPrivate; peakHandles=$peakHandles; peakThreads=$peakThreads
    })
    if ($cycle -lt $Cycles) { Start-Sleep -Seconds 3 }
}

Log "==========================================="
Log "COMPLETE cycles=$Cycles normal=$normalCount resident=$residentCount crashes=$crashCount unexpected=$unexpectedExit"
Log "==========================================="

$reportDir = Split-Path -Parent $ReportPath
if ($reportDir -and -not (Test-Path -LiteralPath $reportDir)) {
    New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
}
$report | Set-Content -LiteralPath $ReportPath -Encoding UTF8
Log "Report saved: $ReportPath"

if ($crashCount -gt 0 -or $unexpectedExit -gt 0) { exit 2 }
exit 0
