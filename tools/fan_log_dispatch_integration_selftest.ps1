param(
    [string]$ExePath = '',
    [string]$EvidenceDir = '',
    [switch]$SkipBuild
)

# FAN-926R 执行单 §6.1：E1b —— native **分派/处理器到磁盘**集成门（离线，无硬件、不睡眠）。
#
# 只驱动**测试专用构建**（native/build_fanlog_test.bat，编译期带 -DYMCC_FAN_LOG_TEST；
# 生产 exe 不含该入口与变异开关）。本门断言：
#   控制（--fan-log-mutation 0，生产行为）：全部 checks 必须为 true；
#   变异 1（免门分支移回详细日志开关之后）/ 2（放宽白名单）/ 3（绕过字段投影）：
#   必须 exit=3 且对应 check 变红；
#   另有"假的成功"守卫：不可写根上 handler 返回 true 而实际未落盘，两者必须分开记账。
#
# 隔离：测试根经 --test-data-root 在任何 yemancc_data_dir() 访问前绑定，夹具/日志/结果
# 只落证据目录，绝不写真实用户 LOCALAPPDATA。报告口径：native 分派/处理器到磁盘集成，
# **不**声称完整 WebView 端到端。

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Join-Path $repoRoot '..\..\Build\Validation\FanLogDispatch\YeManCC-fanlog-selftest.exe'
}
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $repoRoot ('..\..\Build\Validation\FanLogDispatch\evidence-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
$EvidenceDir = (Resolve-Path -LiteralPath $EvidenceDir).Path

if (-not $SkipBuild) {
    $env:YEMAN_WORKSPACE_ROOT = (Resolve-Path -LiteralPath (Join-Path $repoRoot '..\..')).Path
    & (Join-Path $repoRoot 'native\build_fanlog_test.bat') | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'FANLOG test build failed' }
}
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$exeHash = (Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash

$failures = New-Object System.Collections.Generic.List[string]

function Invoke-Entry {
    param([string]$Name, [int]$Mutation)
    $root = Join-Path $EvidenceDir ("run-$Name")
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    $args = @('--test-data-root', $root, '--fan-log-dispatch-selftest', $root)
    if ($Mutation -ne 0) { $args = @('--test-data-root', $root, '--fan-log-mutation', "$Mutation", '--fan-log-dispatch-selftest', $root) }
    $proc = Start-Process -FilePath $ExePath -ArgumentList $args -PassThru -Wait
    $resultPath = Join-Path $root 'fan-log-dispatch.result.json'
    $checks = $null
    if (Test-Path -LiteralPath $resultPath) {
        $checks = (Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json).checks
        Copy-Item -LiteralPath $resultPath -Destination (Join-Path $EvidenceDir "result-$Name.json") -Force
    }
    $false_ = @()
    if ($null -ne $checks) {
        # `disk-write-proof-on-unwritable-root` 是**故意为 false 的记账位**：它证明"handler 返回
        # true"并不等于落盘成功。它是证据，不是断言，故不计入红色项。
        $false_ = @($checks.PSObject.Properties |
            Where-Object { $_.Value -eq $false -and $_.Name -ne 'disk-write-proof-on-unwritable-root' } |
            ForEach-Object { $_.Name })
    }
    return [pscustomobject]@{ name = $Name; exit = $proc.ExitCode; checks = $checks; failed = $false_ }
}

# 控制：生产行为必须全绿。
$control = Invoke-Entry -Name 'control' -Mutation 0
if ($control.exit -ne 0) { $failures.Add("control exit=$($control.exit) expected=0") }
if ($null -eq $control.checks) { $failures.Add('control result json missing') }
elseif (@($control.failed).Count -ne 0) { $failures.Add("control failed checks: $($control.failed -join ',')") }
# 控制里必须**明确**记录"handler true ≠ 落盘"这一对。
if ($null -ne $control.checks -and $control.checks.'handler-returned-true-on-unwritable-root' -ne $true) {
    $failures.Add('control must record handler-returned-true-on-unwritable-root=true (fixture did not exercise the path)')
}

# 变异反控：必须变红，且由对应断言检出。
$mutations = @(
    @{ id = 1; name = 'exempt-branch-moved-behind-switch'; expect = @('log-off-whitelist-events-on-disk') },
    @{ id = 2; name = 'whitelist-widened'; expect = @('log-off-non-whitelist-blocked') },
    @{ id = 3; name = 'projection-bypassed'; expect = @('projection-drops-secrets-and-unknown-keys') }
)
$mutationReport = @()
foreach ($m in $mutations) {
    $run = Invoke-Entry -Name ("mutation-$($m.name)") -Mutation $m.id
    if ($run.exit -eq 0) { $failures.Add("mutation $($m.name): not caught (exit=0)") }
    foreach ($expected in $m.expect) {
        if (-not (@($run.failed) -contains $expected)) {
            $failures.Add("mutation $($m.name): expected check '$expected' did not go red (red=$(@($run.failed) -join ','))")
        }
    }
    $mutationReport += [pscustomobject]@{ mutation = $m.name; exit = $run.exit; red = @($run.failed) }
}

$summary = [ordered]@{
    exe          = $ExePath
    exeSha256    = $exeHash
    mode         = 'native-dispatch-handler-to-disk-integration'
    scope        = 'native fanLog.write dispatch + handler chain to real writer; NOT a full WebView end-to-end run'
    control      = [ordered]@{ exit = $control.exit; checks = $control.checks }
    mutations    = $mutationReport
    failed       = $failures.Count
    failures     = @($failures)
    evidenceDir  = $EvidenceDir
}
$summaryPath = Join-Path $EvidenceDir 'fan-log-dispatch-integration-summary.json'
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))

if ($failures.Count -gt 0) {
    Write-Host ("FAN_LOG_DISPATCH_INTEGRATION=FAIL failed={0}" -f $failures.Count)
    $failures | ForEach-Object { Write-Host ("  - " + $_) }
    exit 2
}
Write-Host 'FAN_LOG_DISPATCH_INTEGRATION=ALL_PASS (control green; 3/3 mutations caught)'
Write-Host ("evidence=" + $summaryPath)
exit 0
