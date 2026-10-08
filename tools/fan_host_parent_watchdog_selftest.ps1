param(
    [string]$HostRoot
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($HostRoot)) {
    # The executable under the currently verified payload is the deployable
    # test subject.  Do not fall back to a pre-migration C: build output.
    $HostRoot = Join-Path $repoRoot 'PowerControl\fan-host'
}
$hostRoot = $HostRoot
$hostExe = Join-Path $hostRoot 'YeManFanHost.exe'
$portProbe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$portProbe.Start()
$port = ([System.Net.IPEndPoint]$portProbe.LocalEndpoint).Port
$portProbe.Stop()
$base = "http://127.0.0.1:$port"
$sessionPath = Join-Path ([System.IO.Path]::GetTempPath()) ("YeManFanHost-parent-watchdog-{0}.session" -f [guid]::NewGuid().ToString('N'))
$session = -join (1..32 | ForEach-Object { '{0:x2}' -f (Get-Random -Minimum 0 -Maximum 256) })
# FAN-926 (P3 / adjudication 5.2): the spawned Host must never append to the real
# %LOCALAPPDATA% user ledger. Give this run its own isolated log root and restore
# the caller's environment in the finally block.
$isolatedLogRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("YeManFanHost-parent-watchdog-logs-{0}" -f [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Force -Path $isolatedLogRoot
$previousLogRoot = $env:YEMANCC_SELFTEST_LOG_ROOT
$env:YEMANCC_SELFTEST_LOG_ROOT = $isolatedLogRoot
# §3-B第五条：本门要在**真实运行时**核对"停监听/延后释放/失败终态/进程退出"四语义的分列证据。
# 其中 host.stopped 属受门控的详细记录，故在本 run 的**隔离日志根**内显式开启详细日志
# （即生产 operator 开关；flag 落在隔离目录，绝不触碰用户真实 %LOCALAPPDATA% 状态）。
Set-Content -LiteralPath (Join-Path $isolatedLogRoot 'fan-logging-enabled.flag') -Value 'enabled' -Encoding ASCII -NoNewline
$parent = $null
$hostProcess = $null
$duplicateProcess = $null

try {
    if (-not (Test-Path -LiteralPath $hostExe -PathType Leaf)) { throw "current release Host executable missing: $hostExe" }
    $session | Set-Content -LiteralPath $sessionPath -Encoding ASCII -NoNewline
    $parent = Start-Process -FilePath (Get-Command powershell.exe).Source -ArgumentList @(
        '-NoLogo', '-NoProfile', '-Command', 'Start-Sleep -Seconds 10'
    ) -PassThru -WindowStyle Hidden
    $hostProcess = Start-Process -FilePath $hostExe -ArgumentList @(
        '--port', [string]$port,
        '--protocol-version', '2',
        '--parent-pid', [string]$parent.Id,
        '--session-token-file', $sessionPath,
        # 2026-09-23（s30 架构）：mock 只经显式开关；lane 旗标文件已不再生效。
        # 缺此参数时宿主按真装口径启动并拒绝 mock 断言（本夹具会看到"无健康"）。
        '--mock-handshake'
    ) -PassThru -WindowStyle Hidden

    $health = $null
    # Match the product launcher readiness budget (5s) with a small margin;
    # cold .NET startup can be slower under Defender/first-run compilation.
    for ($attempt = 0; $attempt -lt 60; $attempt += 1) {
        try {
            $health = Invoke-RestMethod -Uri "$base/health" -Headers @{ 'X-YeMan-Fan-Session' = $session } -TimeoutSec 1
            break
        } catch { Start-Sleep -Milliseconds 100 }
    }
    if ($null -eq $health -or $health.host -ne 'YeManFanHost') { throw 'safe Host did not expose authenticated health' }
    if ($health.state.hardwareCapable -ne $false -or $health.state.hardwareWritesEnabled -ne $false -or $health.state.hardwareWritesObserved -ne $false) {
        throw 'parent watchdog test Host was not in safe no-hardware mode'
    }

    # F06 (FAN-925B): the authentication matrix must be observable on the real HTTP path.
    # A wrong or missing credential is only "this request was not accepted" - it must never
    # grant a bypass, and it must be distinguishable from the accepted request below.
    foreach ($case in @(
        @{ Name = 'wrong-token'; Headers = @{ 'X-YeMan-Fan-Session' = ('f' * 64) }; Expect = 401 },
        @{ Name = 'missing-header'; Headers = @{}; Expect = 401 },
        @{ Name = 'correct-token'; Headers = @{ 'X-YeMan-Fan-Session' = $session }; Expect = 200 }
    )) {
        $status = 0
        try {
            $response = Invoke-WebRequest -Uri "$base/api/state" -Headers $case.Headers -TimeoutSec 2 -UseBasicParsing
            $status = [int]$response.StatusCode
        } catch {
            $problem = $_.Exception.Response
            if ($null -ne $problem) { $status = [int]$problem.StatusCode } else { throw "F06: $($case.Name) transport failure" }
        }
        if ($status -ne $case.Expect) {
            throw "F06: $($case.Name) must return $($case.Expect) on the real Host HTTP path, got $status"
        }
    }
    Write-Output 'fan Host auth matrix: PASS (wrong-token=401, missing-header=401, correct-token=200, no bypass)'

    # The listener port alone is not an ownership lock: the resident Host can
    # recreate it while retaining an HC recovery session. A second safe Host
    # on a different port must still be rejected by the lifetime mutex.
    $duplicateProcess = Start-Process -FilePath $hostExe -ArgumentList @(
        '--port', [string]($port + 1),
        '--protocol-version', '2',
        '--session-token', $session
    ) -PassThru -WindowStyle Hidden
    if (-not $duplicateProcess.WaitForExit(3000) -or $duplicateProcess.ExitCode -ne 2) {
        throw 'second Host was not rejected by the process-lifetime mutex'
    }

    # The parent-exit handoff must answer without waiting for OEM recovery.
    # In the safe Host there is no hardware call, so it then settles to
    # Stopped; a real Host instead remains resident and retries on failure.
    $handoff = Invoke-RestMethod -Uri "$base/api/parent-exit" -Method Post -Headers @{ 'X-YeMan-Fan-Session' = $session } -Body '{}' -ContentType 'application/json' -TimeoutSec 1
    if ($handoff.recoveryAccepted -ne $true -or $handoff.hostWillRemainResident -ne $true) {
        throw 'parent-exit handoff was not accepted by the Host'
    }
    # F06 (FAN-925B) + FAN-926 P1 (CP-07): the acceptance must be bound to the Host instance
    # that actually handled the request AND to a REALISED recovery cycle - a prior successful
    # GET /api/state cannot stand in for handoff acceptance, and neither can an unbound "ok"
    # or a freshly minted field.
    if ([string]::IsNullOrWhiteSpace($handoff.acceptingHostInstanceId) -or
        [string]::IsNullOrWhiteSpace($handoff.acceptedRecoveryCycleId) -or
        [string]::IsNullOrWhiteSpace($handoff.acceptanceScope) -or
        $handoff.acceptanceScope -ne 'accepted-by-this-host-instance-for-this-recovery-cycle') {
        throw 'F06: handoff acceptance must be bound to the accepting Host instance and recovery cycle'
    }
    if ($handoff.acceptedRecoveryCycleId -notmatch '^[0-9a-f]{32}$') {
        throw "FAN-926: acceptedRecoveryCycleId must be a real cycle identity, got '$($handoff.acceptedRecoveryCycleId)'"
    }
    $boundInstance = $handoff.acceptingHostInstanceId
    $boundCycle = $handoff.acceptedRecoveryCycleId
    # Critical evidence: the ACK and the Host's own responsibility ledger must name the SAME
    # instance + cycle (this is what native's strict binding criterion consumes).
    $faultLog = Join-Path $isolatedLogRoot 'fan-host-fault.log'
    # §3-B第五条：父退出必须绑定"pid **及启动时间**"。只核对 pid 无法排除 pid 复用——旧父退出后
    # 同号新进程会被误当成同一父。生产侧在启动时把**真实父启动时间**写进 host.instance-started，
    # 这里以外部看门狗身份独立复核该值，必须与我们亲自拉起的那个父进程一致（<1s 容差）。
    $startedRow = $null
    for ($attempt = 0; $attempt -lt 20; $attempt += 1) {
        if (Test-Path -LiteralPath $faultLog -PathType Leaf) {
            $startedRow = Select-String -LiteralPath $faultLog -Pattern '"eventName":"host.instance-started"' -SimpleMatch | Select-Object -Last 1
            if ($null -ne $startedRow) { break }
        }
        Start-Sleep -Milliseconds 50
    }
    if ($null -eq $startedRow) { throw 'parent watchdog: the Host ledger must record host.instance-started' }
    $startedRecord = $startedRow.Line | ConvertFrom-Json
    if ([int]$startedRecord.details.parentPid -ne [int]$parent.Id) {
        throw "parent watchdog: Host bound parentPid=$($startedRecord.details.parentPid) but the real parent is $($parent.Id)"
    }
    # IMPORTANT: read the RAW JSON text, not the ConvertFrom-Json value. ConvertFrom-Json turns an
    # ISO8601 string into a System.DateTime, and a later [string] round-trip drops the trailing 'Z',
    # so RoundtripKind parsing would yield Kind=Unspecified and shift the instant by the local offset.
    $startMatch = [regex]::Match($startedRow.Line, '"parentStartTimeUtc"\s*:\s*"([^"]*)"')
    if (-not $startMatch.Success) {
        throw 'parent watchdog: the Host must bind the parent START TIME, not just the pid'
    }
    $boundParentStart = $startMatch.Groups[1].Value
    if ([string]::IsNullOrWhiteSpace($boundParentStart) -or $boundParentStart -eq 'unknown') {
        throw 'parent watchdog: the Host must bind the parent START TIME, not just the pid'
    }
    $expectedParentStart = $parent.StartTime.ToUniversalTime()
    $observedParentStart = [datetime]::Parse(
        $boundParentStart,
        [System.Globalization.CultureInfo]::InvariantCulture,
        [System.Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    if ([math]::Abs(($observedParentStart - $expectedParentStart).TotalSeconds) -gt 1.0) {
        throw "parent watchdog: bound parent start $boundParentStart does not match the real parent start $($expectedParentStart.ToString('O'))"
    }
    Write-Output "fan Host parent identity binding: PASS (parentPid=$($parent.Id) parentStartTimeUtc=$boundParentStart matched the real parent start within 1s)"
    $boundRow = $null
    for ($attempt = 0; $attempt -lt 20; $attempt += 1) {
        if (Test-Path -LiteralPath $faultLog -PathType Leaf) {
            $boundRow = Select-String -LiteralPath $faultLog -Pattern '"eventName":"parent-exit-handoff.bound"' -SimpleMatch | Select-Object -Last 1
            if ($null -ne $boundRow) { break }
        }
        Start-Sleep -Milliseconds 50
    }
    if ($null -eq $boundRow) {
        throw 'FAN-926: the Host ledger must record the realised parent-exit acceptance'
    }
    if ($boundRow.Line -notmatch [regex]::Escape($boundCycle) -or $boundRow.Line -notmatch [regex]::Escape($boundInstance)) {
        throw 'FAN-926: the ACK must name the very responsibility the Host ledger bound'
    }
    # A repeat request must reuse the accepted responsibility: one owner, one worker, one cycle.
    $repeat = Invoke-RestMethod -Uri "$base/api/parent-exit" -Method Post -Headers @{ 'X-YeMan-Fan-Session' = $session } -Body '{}' -ContentType 'application/json' -TimeoutSec 1
    if ($repeat.acceptedRecoveryCycleId -ne $boundCycle) {
        throw "FAN-926: a repeat handoff must reuse the accepted cycle, got '$($repeat.acceptedRecoveryCycleId)'"
    }
    Write-Output "fan Host handoff binding: PASS (acceptingHostInstanceId=$boundInstance acceptedRecoveryCycleId=$boundCycle ledgerBound=true idempotentRepeat=true)"
    $settled = $false
    for ($attempt = 0; $attempt -lt 20; $attempt += 1) {
        try {
            $state = (Invoke-RestMethod -Uri "$base/api/state" -Headers @{ 'X-YeMan-Fan-Session' = $session } -TimeoutSec 1).state
            if ($state.state -eq 'Stopped' -and $state.hardwareWritesEnabled -eq $false -and $state.hardwareWritesObserved -eq $false) {
                $settled = $true
                break
            }
        } catch { }
        Start-Sleep -Milliseconds 50
    }
    if (-not $settled) { throw 'safe Host parent-exit handoff did not settle without hardware writes' }
    # FAN-926 (P1 / adjudication 5.1): the worker and settlement rows must correlate to the
    # SAME responsibility the ACK named - otherwise a disabled detailed log leaves nothing but
    # isolated action lines that cannot be reconciled with the acceptance receipt. The exported
    # diagnostic context (hostInstanceId + context.recoveryCycle.cycleId) is the single
    # mechanism that carries this, so assert exactly that.
    foreach ($rowName in @('parent-exit-handoff.recovery-begin', 'parent-exit-handoff.recovery-confirmed')) {
        $workerRow = Select-String -LiteralPath $faultLog -Pattern ('"eventName":"' + $rowName + '"') -SimpleMatch | Select-Object -Last 1
        if ($null -eq $workerRow) { throw "FAN-926: the Host ledger must record $rowName" }
        $row = $workerRow.Line | ConvertFrom-Json
        if ($row.context.recoveryCycle.cycleId -ne $boundCycle -or $row.hostInstanceId -ne $boundInstance) {
            throw "FAN-926: $rowName must carry the very responsibility the ACK named (host=$($row.hostInstanceId) cycle=$($row.context.recoveryCycle.cycleId))"
        }
    }
    Write-Output 'fan Host post-acceptance correlation: PASS (bound/accepted/recovery-begin/recovery-confirmed share one host+cycle)'

    # §3-B第五条：settle 只证明"本宿主实例停止接受控制/采样"，**不是**"进程已退出"。
    # settle 之后、父退出之前，宿主进程必须仍在驻留——这正是"停监听 / 进程退出"两语义的分界：
    # 引擎/驱动句柄的最终释放被延后到进程收尾，不能把 settle 当成进程消失。
    $hostProcess.Refresh()
    if ($hostProcess.HasExited) {
        throw 'parent watchdog: the safe Host must remain resident after settling - stopping a listener is not a process exit'
    }
    # 正常父退出必须走"成功结算"而非"失败终态"：cycle 必须 completed=true 且 terminal=false。
    $confirmedRow = Select-String -LiteralPath $faultLog -Pattern '"eventName":"parent-exit-handoff.recovery-confirmed"' -SimpleMatch | Select-Object -Last 1
    if ($null -eq $confirmedRow) { throw 'parent watchdog: the Host ledger must record parent-exit-handoff.recovery-confirmed' }
    $confirmedRecord = $confirmedRow.Line | ConvertFrom-Json
    if ($confirmedRecord.context.recoveryCycle.terminal -ne $false -or
        $confirmedRecord.context.recoveryCycle.completed -ne $true -or
        $confirmedRecord.context.state -ne 'Stopped') {
        throw "parent watchdog: a normal parent exit must settle as a completed NON-terminal cycle (terminal=$($confirmedRecord.context.recoveryCycle.terminal) completed=$($confirmedRecord.context.recoveryCycle.completed) state=$($confirmedRecord.context.state))"
    }
    Write-Output "fan Host recovery terminal split: PASS (cycleId=$boundCycle completed=true terminal=false state=Stopped; settle is not a process exit)"

    $parent.WaitForExit()
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    $runtimeLog = Join-Path $isolatedLogRoot 'yeman-fan-host-runtime.log'
    $stoppedRow = $null
    $stoppedRecord = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        $hostProcess.Refresh()
        if ($null -eq $stoppedRow -and (Test-Path -LiteralPath $runtimeLog -PathType Leaf)) {
            $stoppedRow = Select-String -LiteralPath $runtimeLog -Pattern '"eventName":"host.stopped"' -SimpleMatch | Select-Object -Last 1
            if ($null -ne $stoppedRow) { $stoppedRecord = $stoppedRow.Line | ConvertFrom-Json }
        }
        if ($hostProcess.HasExited -and $null -ne $stoppedRecord) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $hostProcess.HasExited) { throw 'Host remained resident after its parent exited' }
    # §3-B第五条：本门即生产注释所称的"外部 watchdog"（持有父 pid＋启动时间），它独立证明进程**真正消失**；
    # 而 host.stopped 自身必须如实声明 processExitProven=false（不冒充退出证明），四语义分列、互不冒充。
    if ($null -eq $stoppedRecord) { throw 'parent watchdog: the Host must record host.stopped when it stops its listener' }
    if ($stoppedRecord.details.path -ne 'BoundedFinalizeSafe' -or
        $stoppedRecord.details.listenerStopped -ne $true -or
        $stoppedRecord.details.engineDisposeDeferred -ne $true -or
        $stoppedRecord.details.failureTerminal -ne $false -or
        $stoppedRecord.details.processExitProven -ne $false) {
        throw "parent watchdog: host.stopped must split the four semantics (path=$($stoppedRecord.details.path) listenerStopped=$($stoppedRecord.details.listenerStopped) engineDisposeDeferred=$($stoppedRecord.details.engineDisposeDeferred) failureTerminal=$($stoppedRecord.details.failureTerminal) processExitProven=$($stoppedRecord.details.processExitProven))"
    }
    if ($stoppedRecord.hostInstanceId -ne $boundInstance) {
        throw 'parent watchdog: host.stopped must belong to the same Host instance that accepted the handoff'
    }
    Write-Output 'fan Host listener-vs-process split: PASS (host.stopped path=BoundedFinalizeSafe listenerStopped=true engineDisposeDeferred=true failureTerminal=false processExitProven=false; real exit proven separately by this external watchdog via pid+start-time)'
    Write-Output 'fan Host parent watchdog selftest: PASS (parent-exit handoff -> safe Host close -> no residual; hardwareWrites=false)'
}
finally {
    if ($null -ne $duplicateProcess -and -not $duplicateProcess.HasExited) { Stop-Process -Id $duplicateProcess.Id -Force -ErrorAction SilentlyContinue }
    if ($null -ne $hostProcess -and -not $hostProcess.HasExited) { Stop-Process -Id $hostProcess.Id -Force -ErrorAction SilentlyContinue }
    if ($null -ne $parent -and -not $parent.HasExited) { Stop-Process -Id $parent.Id -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $sessionPath -PathType Leaf) { Remove-Item -LiteralPath $sessionPath -Force }
    $env:YEMANCC_SELFTEST_LOG_ROOT = $previousLogRoot
}
