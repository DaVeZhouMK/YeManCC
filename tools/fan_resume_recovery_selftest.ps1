param(
    [string]$ExePath = "",
    [string]$EvidenceDir = "",
    [string]$DataRoot = ""
)

# 2026-09-23（FAN-204 s31 补充裁决 Q1 / 前身 P-2）：唤醒恢复**接受 + 有界观察**的动态夹具。
# 只驱动**注入构建**（native/build_faultinject.bat 产出；生产 exe 编译期剔除全部夹具），
# 对 12 次运行断言：
#   · 接受有界（≤3 次传输重试；接受本身不等待重建）；
#   · 有界观察（同一 generation 只读 /api/state，≤15 s）；只有 Ready 且 !unknownState 且
#     !hcCloseCleanupPending 才算 recovered；
#   · 诚实的互斥终态：recovered / noop / superseded / exhausted{reason}；
#   · 真机 10.2 s 慢重建窗必须被覆盖（slow-ready ≥10 s 仍判 recovered）；
#   · 15 s 未 Ready ⇒ exhausted{host-rebuild-timeout}，不得伪成功、不得永久 latch。
# 生产判据不被夹具改写——夹具只改写 /api/resume（POST）与 /api/state（GET）的输入。
#
# FAN-926R 执行单 §6.2：本 runner **必须显式传 -ExePath/-EvidenceDir/-DataRoot**；夹具文件、
# 结果 JSON 与日志只落 -DataRoot（经 --test-data-root 在任何 yemancc_data_dir() 访问前绑定），
# 绝不写真实用户 LOCALAPPDATA/Temp。

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Join-Path $repoRoot '..\..\..\_scratch\FAN-204-s31R2-20260923\ws\Build\App\Native\YeManCC-faultinject.exe'
}
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $repoRoot ('..\..\..\_scratch\FAN-204-s31R2-20260923\evidence-run-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null

$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) { throw "fault-inject exe missing: $ExePath" }
$exeHash = (Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash

if ([string]::IsNullOrWhiteSpace($DataRoot)) {
    $DataRoot = Join-Path $env:LOCALAPPDATA 'YeManCC'
}
New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null
$DataRoot = (Resolve-Path -LiteralPath $DataRoot).Path

$dataDir = $DataRoot
$faultFile = Join-Path $dataDir 'test-resume-recovery-fault.txt'
$recordFile = Join-Path $dataDir 'fan-resume-recovery-selftest.json'

$expectedRuns = 12
$failures = New-Object System.Collections.Generic.List[string]
$runIndex = 0

function Get-Record([string]$resumeMode, [string]$stateMode) {
    # 注意：必须逐行写。`-Value @("resume=" + $m, "state=" + $s)` 会被 PowerShell 拼成
    # 一行（"resume=accepted state=slow-ready"）⇒ 宿主侧读到非法 mode ⇒ 夹具静默失效。
    $faultLines = @("resume=$resumeMode", "state=$stateMode")
    Set-Content -LiteralPath $faultFile -Value $faultLines -Encoding ASCII
    if (Test-Path -LiteralPath $recordFile) { Remove-Item -LiteralPath $recordFile -Force }
    # GUI 子系统 exe：& 调用不会等待、也不设置 $LASTEXITCODE（D-204-1 同族坑），
    # 必须用 Start-Process -Wait -PassThru 读 ExitCode（不加 -RedirectStandard*）。
    # FAN-926R §6.2：显式传测试数据根（在任何 yemancc_data_dir() 访问前绑定）。
    $proc = Start-Process -FilePath $ExePath -ArgumentList '--test-data-root', $DataRoot, '--fan-resume-selftest' -PassThru -Wait
    $code = $proc.ExitCode
    if ($code -ne 0) { throw "selftest exited $code for resume=$resumeMode state=$stateMode" }
    if (-not (Test-Path -LiteralPath $recordFile)) { throw "record missing for resume=$resumeMode state=$stateMode" }
    $raw = Get-Content -LiteralPath $recordFile -Raw
    Copy-Item -LiteralPath $recordFile -Destination (Join-Path $EvidenceDir ("record-$resumeMode-$stateMode-$runIndex.json")) -Force
    return ($raw | ConvertFrom-Json)
}

function Assert-Case {
    param(
        [string]$Name,
        [string]$ResumeMode,
        [string]$StateMode = 'real',
        [int]$Attempts = -1,
        [int]$MaxAttempts = 3,
        [int]$Probes = -1,
        [string]$Terminal = 'none',
        [int]$Exhausted = 0,
        [string]$ExhaustReason = '',
        [int]$Superseded = 0,
        [int]$MinElapsedMs = 0,
        [int]$MaxElapsedMs = 20000
    )
    $script:runIndex += 1
    $rec = Get-Record -resumeMode $ResumeMode -stateMode $StateMode
    $events = @($rec.events)
    $attemptEvents = @($events | Where-Object { $_.event -eq 'fan-host-resume-attempt' })
    $observeEvents = @($events | Where-Object { $_.event -eq 'fan-host-resume-observe' })
    $terminalEvent = @($events | Where-Object { $_.event -eq 'fan-host-resume-native-trigger' })
    $exhaustedEvents = @($events | Where-Object { $_.event -eq 'resume-recovery-exhausted' })
    $supersededEvents = @($events | Where-Object { $_.event -eq 'fan-host-resume-superseded' })
    $recovered = @($terminalEvent | Where-Object { $_.terminal -eq 'recovered' })

    if ($Attempts -ge 0 -and $attemptEvents.Count -ne $Attempts) { $script:failures.Add("$Name : attempts=$($attemptEvents.Count) expected=$Attempts") }
    if ($attemptEvents.Count -gt $MaxAttempts) { $script:failures.Add("$Name : attempts $($attemptEvents.Count) exceed the bounded budget $MaxAttempts") }
    if ($Probes -ge 0 -and $observeEvents.Count -gt $Probes) { $script:failures.Add("$Name : observe-logs=$($observeEvents.Count) exceed bounded $Probes") }
    if ($exhaustedEvents.Count -ne $Exhausted) { $script:failures.Add("$Name : exhausted=$($exhaustedEvents.Count) expected=$Exhausted") }
    if ($supersededEvents.Count -ne $Superseded) { $script:failures.Add("$Name : superseded=$($supersededEvents.Count) expected=$Superseded") }
    if ($Terminal -eq 'none') {
        if ($terminalEvent.Count -ne 0) { $script:failures.Add("$Name : unexpected terminal=$($terminalEvent.terminal -join ',')") }
    }
    elseif ($Terminal -eq 'recovered') {
        if ($recovered.Count -ne 1) { $script:failures.Add("$Name : recovered=$($recovered.Count) expected=1") }
    }
    else {
        if ($terminalEvent.Count -ne 1 -or $terminalEvent[0].terminal -ne $Terminal) {
            $script:failures.Add("$Name : terminal=$($terminalEvent.terminal -join ',') expected=$Terminal")
        }
    }
    if ($ExhaustReason -ne '' -and $exhaustedEvents.Count -ge 1) {
        if ("$($exhaustedEvents[0].reason)" -ne $ExhaustReason) {
            $script:failures.Add("$Name : exhausted.reason=$($exhaustedEvents[0].reason) expected=$ExhaustReason")
        }
    }
    if ($terminalEvent.Count -ge 1) {
        $elapsed = [int]$terminalEvent[0].elapsedMs
        if ($elapsed -gt $MaxElapsedMs) { $script:failures.Add("$Name : elapsedMs=$elapsed exceeds budget ${MaxElapsedMs}ms") }
        if ($elapsed -lt $MinElapsedMs) { $script:failures.Add("$Name : elapsedMs=$elapsed below expected window ${MinElapsedMs}ms (bounded observation not exercised)") }
    }
    # 反例守卫：失败序列绝不出现 recovered；成功序列绝不出现 exhausted。
    if ($Terminal -ne 'recovered' -and $recovered.Count -gt 0) { $script:failures.Add("$Name : failure was written as success") }
    if ($Terminal -eq 'recovered' -and $exhaustedEvents.Count -gt 0) { $script:failures.Add("$Name : success also wrote exhaustion") }
    return $rec
}

# ① 宿主未监听（传输失败 status=0，观察也不可达）→ 有界接受后如实耗尽（host-unreachable）
$null = Assert-Case -Name 'host-not-listening' -ResumeMode 'nohost' -StateMode 'unreachable' -Attempts 3 -Terminal 'exhausted' -Exhausted 1 -ExhaustReason 'host-unreachable'
# ② 650 ms 超时（同一 native 判据：无状态码）→ 同上（两者在 native 层同形，分别跑并分别断言）
$null = Assert-Case -Name 'accept-transport-timeout' -ResumeMode 'timeout' -StateMode 'unreachable' -Attempts 3 -Terminal 'exhausted' -Exhausted 1 -ExhaustReason 'host-unreachable'
# ③ 带原因码的 409 属"处理中"⇒ 接受一次后进入观察，重建完成后 recovered（不得把 409 判死）
$null = Assert-Case -Name 'accept-409-transitional-then-ready' -ResumeMode 'accept409' -StateMode 'resuming-then-ready' -Attempts 1 -Terminal 'recovered' -Exhausted 0
# ④ 真机 10.2 s 慢重建窗：接受快、观察窗内到 Ready ⇒ recovered（覆盖 ≥10 s，不是 3 s mock）
$null = Assert-Case -Name 'slow-rebuild-10200ms-observed' -ResumeMode 'accepted' -StateMode 'slow-ready' -Attempts 1 -Terminal 'recovered' -Exhausted 0 -MinElapsedMs 10000
# ⑤ 合法 no-op（宿主明确回答"无可恢复"）→ 单独记录，不重试、不写耗尽
$null = Assert-Case -Name 'legal-noop' -ResumeMode 'noop' -Attempts 1 -Terminal 'noop' -Exhausted 0
# ⑥ 空恢复（带曲线者已重建完成/无可重建）⇒ noop（await-user），不得记成 recovered
$null = Assert-Case -Name 'await-user-noop' -ResumeMode 'accepted-await-user' -Attempts 1 -Terminal 'noop' -Exhausted 0
# ⑦ 15 s 未 Ready ⇒ exhausted{host-rebuild-timeout}（不得伪成功、不得永久 latch）
$null = Assert-Case -Name 'rebuild-timeout-15s' -ResumeMode 'accepted' -StateMode 'resuming-forever' -Attempts 1 -Terminal 'exhausted' -Exhausted 1 -ExhaustReason 'host-rebuild-timeout' -MinElapsedMs 14900 -MaxElapsedMs 20000
# ⑧ 宿主报故障（unknown/FaultLocked）⇒ exhausted{host-report-fault-locked}，不伪成功
$null = Assert-Case -Name 'host-report-fault-locked' -ResumeMode 'accepted' -StateMode 'faultlocked' -Attempts 1 -Terminal 'exhausted' -Exhausted 1 -ExhaustReason 'host-report-fault-locked'
# ⑨ 显式 409 superseded（新睡眠取代）⇒ 立即终态，不重试、不观察
$null = Assert-Case -Name 'superseded-at-accept' -ResumeMode 'superseded409' -Attempts 1 -Terminal 'superseded' -Exhausted 0
# ⑩ 旧响应迟到 / 新 session 已接管 ⇒ 结果不得被消费
$null = Assert-Case -Name 'late-response-superseded' -ResumeMode 'supersede-before-consume' -Attempts 1 -Terminal 'none' -Exhausted 0 -Superseded 1
# ⑪ 耗尽后不永久 latch：同模式再跑一次仍是有界 3 次（可被下一次 resume-ready 重新触发）
$null = Assert-Case -Name 'retrigger-after-exhaustion' -ResumeMode 'nohost' -StateMode 'unreachable' -Attempts 3 -Terminal 'exhausted' -Exhausted 1 -ExhaustReason 'host-unreachable'
# ⑫ 旧宿主（同步等待实现）的终态形状仍必须判 recovered（兼容不回退）
$null = Assert-Case -Name 'legacy-host-terminal-ready' -ResumeMode 'ready-legacy' -Attempts 1 -Terminal 'recovered' -Exhausted 0

if ($runIndex -ne $expectedRuns) { $failures.Add("run count=$runIndex expected=$expectedRuns (case-count self check)") }

$summary = [ordered]@{
    exe          = $ExePath
    exeSha256    = $exeHash
    runs         = $runIndex
    expectedRuns = $expectedRuns
    failed       = $failures.Count
    failures     = @($failures)
    evidenceDir  = $EvidenceDir
}
$summaryPath = Join-Path $EvidenceDir 'fan-resume-recovery-summary.json'
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))

if ($failures.Count -gt 0) {
    Write-Host ("FAN_RESUME_RECOVERY_SELFTEST=CHECK_FAIL runs={0} failed={1}" -f $runIndex, $failures.Count)
    $failures | ForEach-Object { Write-Host ("  - " + $_) }
    exit 2
}
Write-Host ("FAN_RESUME_RECOVERY_SELFTEST=ALL_PASS runs={0}/{1}" -f $runIndex, $expectedRuns)
Write-Host ("evidence=" + $summaryPath)
exit 0