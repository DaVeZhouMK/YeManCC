# YeManCC WebView2 Stability Test
# Cycles: launch -> wait 120s -> close -> record. 15 cycles = 30min.
param(
    [int]$Cycles = 15,
    [int]$SecondsPerCycle = 120,
    [string]$ExePath = "C:\SOFT\YeMan\YeManCC3\dist\YeManCC.exe",
    [string]$ReportPath = "C:\SOFT\YeMan\YeManCC3\stability-report.txt"
)

$ErrorActionPreference = "Continue"
$report = @()
$crashCount = 0
$normalCount = 0
$unexpectedExit = 0

function Log($msg) {
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $line = "[$ts] $msg"
    Write-Host $line -ForegroundColor Cyan
    $script:report += $line
}

Log "==========================================="
Log "YeManCC WebView2 Stability Test START"
Log "Params: Cycles=$Cycles, SecPerCycle=$SecondsPerCycle, Total=$(( $Cycles * $SecondsPerCycle ))s"
Log "Exe: $ExePath"
Log "Report: $ReportPath"
Log "==========================================="

if (-not (Test-Path $ExePath)) {
    Log "[FATAL] exe not found: $ExePath"
    exit 1
}

# Pre-clean: kill any residual process
Get-Process -Name YeManCC -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

for ($i = 1; $i -le $Cycles; $i++) {
    $cycleStart = Get-Date
    Log "--- Cycle $i / $Cycles ---"

    # Check for pre-existing crash dumps
    $dataDir = "$env:LOCALAPPDATA\QQ_YeManCC"
    $crashDir = "$dataDir\EBWebView\Crapadpad\reports"
    # Also check alternate crash dir naming
    $crashDir2 = "$dataDir\EBWebView\Crashpad\reports"
    $preDumps = @()
    if (Test-Path $crashDir) { $preDumps += @(Get-ChildItem -Path $crashDir -Filter "*.dmp" -ErrorAction SilentlyContinue) }
    if (Test-Path $crashDir2) { $preDumps += @(Get-ChildItem -Path $crashDir2 -Filter "*.dmp" -ErrorAction SilentlyContinue) }
    if ($preDumps.Count -gt 0) {
        Log "[WARN] Pre-existing dumps found: $($preDumps.Count) files"
        foreach ($d in $preDumps) { Log "  DMP: $($d.FullName) size=$($d.Length)" }
    }

    # Launch process
    try {
        $proc = Start-Process -FilePath $ExePath -PassThru -ErrorAction Stop
        Log "PID=$($proc.Id) launched at $(Get-Date -Format 'HH:mm:ss')"
    } catch {
        Log "[FAIL] Launch failed: $_"
        $script:unexpectedExit++
        continue
    }

    # Monitor loop: check every 5s for up to SecondsPerCycle
    $elapsed = 0
    $exitCode = $null

    while ($elapsed -lt $SecondsPerCycle) {
        Start-Sleep -Seconds 5
        $elapsed += 5

        if ($proc.HasExited) {
            $exitCode = $proc.ExitCode
            $actualRuntime = (Get-Date) - $cycleStart
            Log "[CRASH?] Process exited after $([math]::Round($actualRuntime.TotalSeconds,0))s! ExitCode=$exitCode"

            # Check for new crash dumps
            $postDumps = @()
            if (Test-Path $crashDir) { $postDumps += @(Get-ChildItem -Path $crashDir -Filter "*.dmp" -ErrorAction SilentlyContinue) }
            if (Test-Path $crashDir2) { $postDumps += @(Get-ChildItem -Path $crashDir2 -Filter "*.dmp" -ErrorAction SilentlyContinue) }

            # Find dumps created after cycle start
            $newDumps = $postDumps | Where-Object { $_.CreationTime -gt $cycleStart }
            if ($newDumps.Count -gt 0) {
                Log "[CRASH] $($newDumps.Count) new .dmp files confirmed WebView2 crash!"
                foreach ($d in $newDumps) { Log "  DMP: $($d.FullName) size=$($d.Length) Created=$($d.CreationTime)" }
                $script:crashCount++
            } else {
                Log "[EXIT] No new .dmp but process exited (code=$exitCode)"
                $script:unexpectedExit++
            }
            break
        }

        # Status every 30s
        if ($elapsed % 30 -eq 0) {
            try {
                $p = Get-Process -Id $proc.Id -ErrorAction Stop
                $memMB = [math]::Round($p.WorkingSet64 / 1MB, 0)
                Log "  ... alive ${elapsed}s | RAM=${memMB}MB"
            } catch {
                Log "  ... ${elapsed}s process gone during check"
            }
        }
    }

    # Normal end of cycle: if still running, close it
    if (-not $proc.HasExited) {
        $runtime = (Get-Date) - $cycleStart
        try {
            $proc.CloseMainWindow() | Out-Null
            if (-not $proc.WaitForExit(5000)) {
                Log "[WARN] Graceful close timeout, force killing PID=$($proc.Id)"
                $proc.Kill()
                $proc.WaitForExit(3000) | Out-Null
            }
            Log "OK closed normally (ran $([math]::Round($runtime.TotalSeconds,0))s)"
            $script:normalCount++
        } catch {
            Log "[WARN] Close error: $_"
        }
    }

    # Inter-cycle pause
    Start-Sleep -Seconds 3
}

# Final report
Log ""
Log "==========================================="
Log "Stability Test COMPLETE!"
Log "Total cycles : $Cycles"
Log "Normal       : $normalCount"
Log "WebView2 Crash: $crashCount"
Log "Unexpected   : $unexpectedExit"
$pct = if ($Cycles -gt 0) { [math]::Round($crashCount / $Cycles * 100, 1) } else { 0 }
Log "Crash rate   : ${pct}%"
Log "==========================================="

# Save report
$reportDir = Split-Path $ReportPath -Parent
if ($reportDir -and -not (Test-Path $reportDir)) { New-Item -ItemType Directory -Path $reportDir -Force | Out-Null }
$report | Out-File -FilePath $ReportPath -Encoding UTF8
Log "Report saved: $ReportPath"

Write-Host ""
Write-Host "=== SUMMARY ===" -ForegroundColor Yellow
Write-Host "  Normal:   $normalCount / $Cycles" -ForegroundColor Green
Write-Host "  Crashes:  $crashCount / $Cycles" -ForegroundColor Red
Write-Host "  Unexpected: $unexpectedExit / $Cycles" -ForegroundColor Yellow
