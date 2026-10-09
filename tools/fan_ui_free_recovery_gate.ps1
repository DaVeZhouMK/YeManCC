param(
    [string]$HostRoot
)
# FAN-926 P2 (adjudication item 3): UI-free single-owner recovery chain - process-level gate.
#   A) a successor instance may NOT acquire HC write authority while the predecessor is alive;
#      it must WAIT (same process), and
#   B) once the predecessor really exits, that SAME successor acquires and writes the takeover
#      receipt whose proof is the OS lifetime mutex (not a port/PID/timeout guess),
#   C) negative: a path with no formal HC receipt is never reported as a completed recovery,
#   D) end to end (sandbox trigger): the predecessor tries one same-process recovery, spawns
#      exactly ONE successor, exits orderly by itself; the successor inherits the SAME cycle
#      and remaining budget (not a fresh 60 s) and a second replacement is refused.
# ASCII only on purpose: PowerShell reads .ps1 as ANSI here (project rule).

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($HostRoot)) {
    $HostRoot = Join-Path $repoRoot 'PowerControl\fan-host'
}
$hostExe = Join-Path $HostRoot 'YeManFanHost.exe'
if (-not (Test-Path -LiteralPath $hostExe -PathType Leaf)) { throw "current release Host executable missing: $hostExe" }

$portProbe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$portProbe.Start()
$port = ([System.Net.IPEndPoint]$portProbe.LocalEndpoint).Port
$portProbe.Stop()
$base = "http://127.0.0.1:$port"

$sessionPath = Join-Path ([System.IO.Path]::GetTempPath()) ("YeManFanHost-uifree-{0}.session" -f [guid]::NewGuid().ToString('N'))
$session = -join (1..32 | ForEach-Object { '{0:x2}' -f (Get-Random -Minimum 0 -Maximum 256) })
$logsRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("YeManFanHost-uifree-logs-{0}" -f [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Force -Path $logsRoot
$previousLogRoot = $env:YEMANCC_SELFTEST_LOG_ROOT
$env:YEMANCC_SELFTEST_LOG_ROOT = $logsRoot
$faultLog = Join-Path $logsRoot 'fan-host-fault.log'

$predecessor = $null
$successorEarly = $null
$successorLate = $null
$predecessorD = $null
$predecessorE = $null

# NOTE: the parameter must NOT be called $Base - PowerShell is case-insensitive and it would
# shadow the script-scope $base, turning the URI into "/health" and failing every probe.
function Wait-Health([int]$Attempts = 60, [string]$BaseUrl = '') {
    if ([string]::IsNullOrWhiteSpace($BaseUrl)) { $BaseUrl = $base }
    for ($i = 0; $i -lt $Attempts; $i += 1) {
        try {
            $health = Invoke-RestMethod -Uri "$BaseUrl/health" -Headers @{ 'X-YeMan-Fan-Session' = $session } -TimeoutSec 1
            if ($health.host -eq 'YeManFanHost') { return $true }
        } catch { }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

# Stop only Hosts running THIS gate's staged payload (never a Host installed elsewhere).
function Stop-StagedHosts {
    $staged = [System.IO.Path]::GetFullPath($hostExe)
    foreach ($process in @(Get-Process -Name YeManFanHost -ErrorAction SilentlyContinue)) {
        try {
            if ([System.IO.Path]::GetFullPath($process.Path) -ieq $staged) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            }
        } catch { }
    }
}

try {
    $session | Set-Content -LiteralPath $sessionPath -Encoding ASCII -NoNewline
    $commonArgs = @('--port', [string]$port, '--protocol-version', '2', '--session-token-file', $sessionPath, '--mock-handshake')

    $predErr = Join-Path $logsRoot 'predecessor.stderr.txt'
    $predOut = Join-Path $logsRoot 'predecessor.stdout.txt'
    $predecessor = Start-Process -FilePath $hostExe -ArgumentList $commonArgs -PassThru -WindowStyle Hidden -RedirectStandardError $predErr -RedirectStandardOutput $predOut
    if (-not (Wait-Health)) {
        $predecessor.Refresh()
        $why = "predecessor Host did not expose authenticated health (exited=$($predecessor.HasExited)"
        if ($predecessor.HasExited) { $why += " exitCode=$($predecessor.ExitCode)" }
        $why += " port=$port tokenLen=$((Get-Content -LiteralPath $sessionPath -Raw).Length)"
        if (Test-Path -LiteralPath $predErr) { $why += " stderr=" + ((Get-Content -LiteralPath $predErr -Raw) -replace '\s+', ' ') }
        if (Test-Path -LiteralPath $predOut) { $why += " stdout=" + ((Get-Content -LiteralPath $predOut -Raw) -replace '\s+', ' ') }
        $why += " logs=" + ((Get-ChildItem -LiteralPath $logsRoot -File | ForEach-Object Name) -join ',')
        throw $why
    }

    # ---- A) ONE successor must WAIT while the predecessor is alive ----
    # The discriminating shape: a single successor process must still be waiting (not exited)
    # while the predecessor holds the lifetime lease, and must then take over by itself once
    # the predecessor really exits. A fresh second process would prove nothing.
    $successorEarly = Start-Process -FilePath $hostExe -ArgumentList ($commonArgs + @('--successor-takeover', '--predecessor-instance', 'uifree-gate-predecessor')) -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 4
    $successorEarly.Refresh()
    if ($successorEarly.HasExited) {
        throw "FAN-926: a successor must WAIT for the predecessor, not exit while it is alive (exit=$($successorEarly.ExitCode))"
    }
    if (Test-Path -LiteralPath $faultLog) {
        $earlyTakeover = Select-String -LiteralPath $faultLog -Pattern '"eventName":"recovery.host-replacement-took-over"' -SimpleMatch | Select-Object -Last 1
        if ($null -ne $earlyTakeover) {
            throw 'FAN-926: a waiting successor must not write a takeover receipt before the predecessor exited'
        }
    }
    Write-Output 'fan P2 successor-waits-for-live-predecessor: PASS (still waiting after 4 s, wrote no takeover receipt)'

    # ---- B) the SAME successor takes over once the predecessor really exits ----
    Stop-Process -Id $predecessor.Id -Force
    $predecessor.WaitForExit(8000) | Out-Null
    $predecessor = $null
    if (-not (Wait-Health 80)) { throw 'FAN-926: the waiting successor did not take over after the predecessor exited' }
    if ($successorEarly.HasExited) {
        throw "FAN-926: the waiting successor must survive the takeover, but it exited with $($successorEarly.ExitCode)"
    }
    $successorLate = $successorEarly
    $takeoverRow = $null
    # Window sizing (see the note in phase D): keep every observation window strictly larger than the
    # sandbox trigger latency so this gate cannot become order/timing dependent. Waiting longer never
    # weakens the assertion - it breaks out on the first matching row.
    for ($i = 0; $i -lt 300; $i += 1) {
        if (Test-Path -LiteralPath $faultLog) {
            $takeoverRow = Select-String -LiteralPath $faultLog -Pattern '"eventName":"recovery.host-replacement-took-over"' -SimpleMatch | Select-Object -Last 1
            if ($null -ne $takeoverRow) { break }
        }
        Start-Sleep -Milliseconds 100
    }
    if ($null -eq $takeoverRow) { throw 'FAN-926: the successful successor must write a takeover receipt' }
    $row = $takeoverRow.Line | ConvertFrom-Json
    if ($row.details.proof -ne 'lifetime-mutex-recreated-only-after-predecessor-handles-closed') {
        throw "FAN-926: the takeover receipt must carry the mutex-based exit proof, got '$($row.details.proof)'"
    }
    if ($row.details.predecessorInstanceId -ne 'uifree-gate-predecessor') {
        throw 'FAN-926: the takeover receipt must name the predecessor it replaced'
    }
    Write-Output 'fan P2 successor-takeover-receipt: PASS (same process waited then acquired; proof=lifetime-mutex)'

    # ---- C) negative: the takeover path never claims a completed recovery ----
    # A fresh successor legitimately starts Stopped; what must NOT exist is a recovery
    # confirmation (there was no formal HC receipt here) or a faked restore claim.
    $confirmedRow = Select-String -LiteralPath $faultLog -Pattern '"eventName":"recovery.ui-free-same-process-confirmed"' -SimpleMatch | Select-Object -Last 1
    if ($null -ne $confirmedRow) {
        throw 'FAN-926: no same-process recovery confirmation may exist without a formal HC receipt'
    }
    $state = (Invoke-RestMethod -Uri "$base/api/state" -Headers @{ 'X-YeMan-Fan-Session' = $session } -TimeoutSec 2).state
    if ($state.oemRestoreConfirmed -ne $false -or $state.hardwareWritesEnabled -ne $false -or $state.hardwareWritesObserved -ne $false) {
        throw "FAN-926: a takeover that proved nothing about the fan must not report a confirmed restore " +
            "(oemRestoreConfirmed=$($state.oemRestoreConfirmed) live=$($state.hardwareWritesEnabled) writesObserved=$($state.hardwareWritesObserved))"
    }
    Write-Output 'fan P2 takeover-is-not-a-recovery-claim: PASS (no confirmation row, no faked restore)'

    # ---- D) automatic controlled replacement, end to end (sandbox trigger) ----
    # The predecessor injects a permanent UI-unavailable route loss. It must: try exactly one
    # same-process recovery, then spawn exactly ONE successor and exit orderly by itself; the
    # successor must take over and INHERIT the same cycle (budget); and it must NOT spawn a
    # second replacement for the same cycle.
    if ($null -ne $successorLate) { Stop-Process -Id $successorLate.Id -Force -ErrorAction SilentlyContinue }
    $successorLate = $null
    Start-Sleep -Milliseconds 600
    $logsRootD = Join-Path ([System.IO.Path]::GetTempPath()) ("YeManFanHost-uifree-logsD-{0}" -f [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Force -Path $logsRootD
    $env:YEMANCC_SELFTEST_LOG_ROOT = $logsRootD
    $faultLogD = Join-Path $logsRootD 'fan-host-fault.log'
    $probeD = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $probeD.Start()
    $portD = ([System.Net.IPEndPoint]$probeD.LocalEndpoint).Port
    $probeD.Stop()
    $baseD = "http://127.0.0.1:$portD"
    $predArgs = @('--port', [string]$portD, '--protocol-version', '2', '--session-token-file', $sessionPath,
        '--mock-handshake', '--mock-ui-free-escalation', '--parent-pid', [string]$PID)
    $predecessorD = Start-Process -FilePath $hostExe -ArgumentList $predArgs -PassThru -WindowStyle Hidden
    try {
        if (-not (Wait-Health 60 $baseD)) { throw 'FAN-926: the escalation predecessor did not become ready' }
        $autoDeadline = [DateTime]::UtcNow.AddSeconds(25)
        while (-not $predecessorD.HasExited -and [DateTime]::UtcNow -lt $autoDeadline) {
            Start-Sleep -Milliseconds 200
            $predecessorD.Refresh()
        }
        if (-not $predecessorD.HasExited) {
            throw 'FAN-926: the predecessor must exit orderly by itself after spawning the successor'
        }
        if (-not (Wait-Health 80 $baseD)) { throw 'FAN-926: the successor did not take over the port after the predecessor exited' }

        $predRows = @(Select-String -LiteralPath $faultLogD -Pattern '"eventName":"recovery.host-replacement-predecessor-exit"' -SimpleMatch)
        if ($predRows.Count -ne 1) {
            throw "FAN-926: exactly one orderly predecessor exit row is required, got $($predRows.Count)"
        }
        $spawnedRows = @(Select-String -LiteralPath $faultLogD -Pattern '"eventName":"recovery.host-replacement-spawned"' -SimpleMatch)
        if ($spawnedRows.Count -ne 1) {
            throw "FAN-926: exactly ONE successor may be spawned for the cycle, got $($spawnedRows.Count)"
        }
        $takeoverRows = @(Select-String -LiteralPath $faultLogD -Pattern '"eventName":"recovery.host-replacement-took-over"' -SimpleMatch)
        if ($takeoverRows.Count -ne 1) {
            throw "FAN-926: exactly one takeover receipt is required, got $($takeoverRows.Count)"
        }
        # The successor must have refused to spawn again, on the CYCLE ledger (not its own counter),
        # and must report the inherited cycle.
        #
        # WINDOW SIZING (measured, not guessed): the sandbox trigger arms at successor start and fires
        # ~8 s later, so the successor's cap refusal lands ~10 s AFTER the takeover row. An earlier 10 s
        # window sat exactly on that boundary and made this gate order/timing dependent (it passed alone
        # and failed right after the other gates). Poll for 30 s so the observation window is strictly
        # larger than the escalation latency; this widens only the WAIT, never the assertion.
        $capRow = $null
        for ($i = 0; $i -lt 120; $i += 1) {
            $capRow = Select-String -LiteralPath $faultLogD -Pattern '"reason":"replacement-cap-reached-in-cycle"' -SimpleMatch | Select-Object -Last 1
            if ($null -ne $capRow) { break }
            Start-Sleep -Milliseconds 250
        }
        if ($null -eq $capRow) {
            $spawnedNow = @(Select-String -LiteralPath $faultLogD -Pattern '"eventName":"recovery.host-replacement-spawned"' -SimpleMatch).Count
            $availNow = @(Select-String -LiteralPath $faultLogD -Pattern '"eventName":"recovery.host-replacement-unavailable"' -SimpleMatch).Count
            throw ("FAN-926: the successor must refuse a second replacement on the cycle ledger " +
                "(waited 30 s; spawned rows now=$spawnedNow, unavailable rows now=$availNow - " +
                "if spawned>1 the cap did NOT hold on the inherited cycle)")
        }
        $cap = $capRow.Line | ConvertFrom-Json
        if ($cap.context.recoveryCycle.inherited -ne $true) {
            throw "FAN-926: the successor must report an INHERITED cycle, got inherited=$($cap.context.recoveryCycle.inherited)"
        }
        if ($cap.context.recoveryCycle.replacements -lt 1) {
            throw "FAN-926: the inherited cycle must already account for the one replacement, got $($cap.context.recoveryCycle.replacements)"
        }
        # The budget must be INHERITED, not refreshed: a takeover that hands the successor a fresh
        # 60 s would silently reopen the recovery window. The log adjudicator already fails such a
        # bundle (control N4); assert it here too so the process-level gate covers it directly.
        if ([double]$cap.context.recoveryCycle.remainingMs -ge 60000) {
            throw "FAN-926: an inherited cycle must NOT have a fresh 60 s budget, got remainingMs=$($cap.context.recoveryCycle.remainingMs)"
        }
        Write-Output ("fan P2 automatic-replacement-with-budget-inheritance: PASS (cycle=" + $cap.context.recoveryCycle.cycleId +
            " inherited=true replacements=" + $cap.context.recoveryCycle.replacements +
            " remainingMs=" + $cap.context.recoveryCycle.remainingMs + " secondSpawnRefused=true)")
    }
    finally {
        if ($null -ne $predecessorD) {
            try { if (-not $predecessorD.HasExited) { Stop-Process -Id $predecessorD.Id -Force -ErrorAction SilentlyContinue } } catch { }
        }
    }
    # ---- E) the successor must restore the ORIGINAL control intent ----
    # Step D's successor is still holding the lifetime lease; release it first, otherwise this
    # step's predecessor would be rejected by the process-lifetime mutex (exit 2, no health).
    Stop-StagedHosts
    Start-Sleep -Milliseconds 700
    # A real curve is written through the real API on the predecessor; the automatic replacement
    # must carry that intent across, and the successor must apply it under its own recovery
    # authority (never masquerading as a client api.enable), then accept the next client write.
    $logsRootE = Join-Path ([System.IO.Path]::GetTempPath()) ("YeManFanHost-uifree-logsE-{0}" -f [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Force -Path $logsRootE
    $env:YEMANCC_SELFTEST_LOG_ROOT = $logsRootE
    $faultLogE = Join-Path $logsRootE 'fan-host-fault.log'
    $probeE = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $probeE.Start()
    $portE = ([System.Net.IPEndPoint]$probeE.LocalEndpoint).Port
    $probeE.Stop()
    $baseE = "http://127.0.0.1:$portE"
    $intentCurve = '{"nodes":[{"tempC":0,"dutyPercent":70},{"tempC":50,"dutyPercent":80},{"tempC":70,"dutyPercent":90},{"tempC":90,"dutyPercent":100}]}'
    $predArgsE = @('--port', [string]$portE, '--protocol-version', '2', '--session-token-file', $sessionPath,
        '--mock-handshake', '--mock-ui-free-escalation', '--parent-pid', [string]$PID)
    $predecessorE = Start-Process -FilePath $hostExe -ArgumentList $predArgsE -PassThru -WindowStyle Hidden
    try {
        if (-not (Wait-Health 60 $baseE)) { throw 'FAN-926: the intent predecessor did not become ready' }
        $headers = @{ 'X-YeMan-Fan-Session' = $session }
        $ctl = Invoke-RestMethod -Uri "$baseE/api/acquire-control" -Method Post -Headers $headers -Body '{}' -ContentType 'application/json' -TimeoutSec 3
        $leaseId = $null
        if ($ctl.lease -is [string]) { $leaseId = $ctl.lease }
        elseif ($null -ne $ctl.lease) {
            foreach ($name in @('leaseId', 'id', 'value')) {
                if ($null -ne $ctl.lease.$name) { $leaseId = [string]$ctl.lease.$name; break }
            }
        }
        if ([string]::IsNullOrWhiteSpace($leaseId)) { throw "FAN-926: could not read a lease id from /api/acquire-control: $($ctl | ConvertTo-Json -Depth 4 -Compress)" }
        # {"nodes":[...]} -> {"leaseId":"X","nodes":[...]}   (no trimming: that would break the JSON)
        $enableBody = '{"leaseId":"' + $leaseId + '","nodes":' + $intentCurve.Substring($intentCurve.IndexOf('['))
        $en = Invoke-RestMethod -Uri "$baseE/api/enable" -Method Post -Headers $headers -Body $enableBody -ContentType 'application/json' -TimeoutSec 3
        if ($en.ok -ne $true) { throw "FAN-926: the intent curve was rejected by the real API: $($en | ConvertTo-Json -Depth 4 -Compress)" }
        $predState = (Invoke-RestMethod -Uri "$baseE/api/state" -Headers $headers -TimeoutSec 3).state
        if (@($predState.activeCurve).Count -ne 4) {
            throw "FAN-926: the predecessor must hold a 4-node active curve, got $(@($predState.activeCurve).Count)"
        }

        $autoDeadlineE = [DateTime]::UtcNow.AddSeconds(30)
        while (-not $predecessorE.HasExited -and [DateTime]::UtcNow -lt $autoDeadlineE) {
            Start-Sleep -Milliseconds 200
            $predecessorE.Refresh()
        }
        if (-not $predecessorE.HasExited) { throw 'FAN-926: the intent predecessor must exit orderly by itself' }
        if (-not (Wait-Health 80 $baseE)) { throw 'FAN-926: the intent successor did not take over' }

        $restoredRow = $null
        for ($i = 0; $i -lt 120; $i += 1) {
            $restoredRow = Select-String -LiteralPath $faultLogE -Pattern '"eventName":"recovery.control-intent-restored"' -SimpleMatch | Select-Object -Last 1
            if ($null -ne $restoredRow) { break }
            Start-Sleep -Milliseconds 250
        }
        if ($null -eq $restoredRow) {
            $whyRow = Select-String -LiteralPath $faultLogE -Pattern '"eventName":"recovery.control-intent-unusable"' -SimpleMatch | Select-Object -Last 1
            throw "FAN-926: the successor must restore the original control intent" + $(if ($null -ne $whyRow) { " (reported unusable: $($whyRow.Line))" } else { " (no intent row at all)" })
        }
        $restored = $restoredRow.Line | ConvertFrom-Json
        if ($restored.details.curveNodes -ne 4) {
            throw "FAN-926: the restored intent must carry the original 4 nodes, got $($restored.details.curveNodes)"
        }
        if ($restored.details.authority -ne 'host-takeover-recovery' -or $restored.details.isClientEnable -ne $false) {
            throw "FAN-926: the takeover restore must not masquerade as a client enable (authority=$($restored.details.authority) isClientEnable=$($restored.details.isClientEnable))"
        }
        if ($restored.details.nextAdjustmentStillRequired -ne $true) {
            throw 'FAN-926: restoring the intent must not be reported as the end of recovery'
        }
        Write-Output ("fan P2 successor-restores-original-intent: PASS (curveNodes=" + $restored.details.curveNodes +
            " cycle=" + $restored.details.cycleId + " authority=host-takeover-recovery clientEnable=false)")

        # The next client adjustment must still take effect on the successor (still adjustable).
        $ctl2 = Invoke-RestMethod -Uri "$baseE/api/acquire-control" -Method Post -Headers $headers -Body '{}' -ContentType 'application/json' -TimeoutSec 3
        $lease2 = $null
        if ($ctl2.lease -is [string]) { $lease2 = $ctl2.lease }
        elseif ($null -ne $ctl2.lease) {
            foreach ($name in @('leaseId', 'id', 'value')) {
                if ($null -ne $ctl2.lease.$name) { $lease2 = [string]$ctl2.lease.$name; break }
            }
        }
        $nextBody = '{"leaseId":"' + $lease2 + '","nodes":[{"tempC":0,"dutyPercent":75},{"tempC":50,"dutyPercent":85},{"tempC":70,"dutyPercent":95},{"tempC":90,"dutyPercent":100}]}'
        $next = Invoke-RestMethod -Uri "$baseE/api/enable" -Method Post -Headers $headers -Body $nextBody -ContentType 'application/json' -TimeoutSec 3
        if ($next.ok -ne $true) { throw 'FAN-926: the next user adjustment must take effect after the takeover restore' }
        Write-Output 'fan P2 next-adjustment-after-takeover: PASS (client write accepted on the successor)'
    }
    finally {
        if ($null -ne $predecessorE) {
            try { if (-not $predecessorE.HasExited) { Stop-Process -Id $predecessorE.Id -Force -ErrorAction SilentlyContinue } } catch { }
        }
    }
}
finally {
    foreach ($process in @($successorEarly, $successorLate, $predecessor, $predecessorD, $predecessorE)) {
        if ($null -ne $process) {
            try { if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } } catch { }
        }
    }
    # Orphaned successors are found by IMAGE PATH, never by name: this gate must never touch a
    # Host the operator installed elsewhere (different path) and never kill unrelated processes.
    Stop-StagedHosts
    if (Test-Path -LiteralPath $sessionPath -PathType Leaf) { Remove-Item -LiteralPath $sessionPath -Force }
    $env:YEMANCC_SELFTEST_LOG_ROOT = $previousLogRoot
}
Write-Output 'fan Host UI-free recovery gate: PASS (process level; A/B/C/D/E)'
