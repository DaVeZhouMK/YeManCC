# run_all.ps1 - 模拟器总门（204-M v2）：一次运行跑完全部套件，只产出一个总判（SIM-1 单一判定源）。
#  用途：把"分散套件"收敛为可复跑的 no-known-gap 门；任何子套件非 0 退出 ⇒ 总判 FAIL。
#  约束：本脚本自身不启动任何东西（各子套件分别自启自清自己的沙箱宿主/桩件）；不触发真实睡眠。
#  用法：powershell -File tools\ymcc_sim\tests\run_all.ps1 [-Strength 2|3] [-WithSleep] [-Quick] [-OutDir x]
[CmdletBinding()]
param(
  [ValidateSet(2, 3)][int]$Strength = 3,
  [switch]$WithSleep,
  [switch]$Quick,
  [string]$OutDir = ''
)
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
. (Join-Path $simRoot 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $D.evidenceDir 'run-all' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$suites = New-Object System.Collections.ArrayList
$suites.Add([ordered]@{ id = 'tool_gate';   file = (Join-Path $testsDir 'tool_gate.ps1');   args = @(); note = '工具门 16 项（SIM-1..4 负例 / 正常对照 / 同种子复跑）' }) | Out-Null
$suites.Add([ordered]@{ id = 'invariants';  file = (Join-Path $testsDir 'invariants.ps1');  args = @(); note = '12 条机器可判定不变量' }) | Out-Null
$suites.Add([ordered]@{ id = 'extremes';    file = (Join-Path $testsDir 'extremes.ps1');    args = @(); note = '极端场景 E1–E7（silent/drop/garbage/noop-success/无反馈）' }) | Out-Null
$suites.Add([ordered]@{ id = 'regressions'; file = (Join-Path $testsDir 'regressions.ps1'); args = @(); note = '永久回归负例 R1–R10' }) | Out-Null
$suites.Add([ordered]@{ id = 'gyro_core';   file = (Join-Path $testsDir 'gyro_core.ps1');   args = @(); note = 'S1–S3/S12 共用 core 只读锚点' }) | Out-Null
$suites.Add([ordered]@{ id = 'signals';     file = (Join-Path $testsDir 'signals.ps1');     args = @(); note = 'HC 信号清单 + 逐条伪装筛选' }) | Out-Null
$suites.Add([ordered]@{ id = 'api_contract'; file = (Join-Path $testsDir 'api_contract.ps1'); args = @(); note = '风扇侧 API 契约负例 AC1–AC12（auth/404/非法JSON/413/代次/越权零副作用/并发/正控/乱序代次/故障后新世代）' }) | Out-Null
if (-not $Quick) {
  $suites.Add([ordered]@{ id = 'cycle'; file = (Join-Path $testsDir 'cycle.ps1'); args = @('-Strength', "$Strength"); note = ("风扇组合周期 {0}-wise" -f $Strength) }) | Out-Null
}
if ($WithSleep) {
  $front = Join-Path $simRoot 'ymcc_sim.ps1'
  foreach ($m in @('sleeptypes', 'typeseries', 'rapidwake')) {
    $suites.Add([ordered]@{ id = ('sleep-' + $m); file = $front; args = @('-Domain', 'sleep', '-Mode', $m); note = ('睡眠域 {0}（注入式；不触发真实睡眠）' -f $m) }) | Out-Null
  }
}
$expected = @($suites).Count

$run = New-SimRun -Domain 'tests' -Mode 'run-all' -ModeKind 'offline-injection' -ToolPath $PSCommandPath -Seed 204 -Notes @{ strength = $Strength; withSleep = [bool]$WithSleep; quick = [bool]$Quick }
Write-Host '=== ymcc_sim 总门 run-all（全部套件一次运行）===' -ForegroundColor White
Write-Host ("套件 {0} 个；strength={1} withSleep={2}" -f $expected, $Strength, [bool]$WithSleep) -ForegroundColor Gray

foreach ($s in $suites) {
  Write-Host ("--- [{0}] {1}" -f $s.id, $s.note) -ForegroundColor White
  $childArgs = @($s.args)
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $out = & powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File $s.file @childArgs 2>&1 | Out-String
  $code = $LASTEXITCODE
  $sw.Stop()
  $tail = (@($out -split "`r?`n") | Where-Object { "$_".Trim() -ne '' } | Select-Object -Last 3) -join ' | '
  $counts = ''
  if ($out -match 'cases=(\d+)/(\d+) failed=(\d+)') { $counts = ("cases={0}/{1} failed={2}" -f $Matches[1], $Matches[2], $Matches[3]) }
  elseif ($out -match 'cases=(\d+) failed=(\d+)') { $counts = ("cases={0} failed={1}" -f $Matches[1], $Matches[2]) }
  elseif ($out -match 'checks (\d+)/(\d+)') { $counts = ("checks={0}/{1}" -f $Matches[1], $Matches[2]) }
  $s['exitCode'] = [int]$code
  $s['seconds'] = [Math]::Round($sw.Elapsed.TotalSeconds, 1)
  $s['counts'] = $counts
  Add-SimCheck -Run $run -Form 'SUITE' -Name $s.id -Ok ($code -eq 0) -Detail ("exit={0} {1}s {2} :: {3}" -f $code, $s['seconds'], $counts, $tail) | Out-Null
}

# 假绿防护：套件数量必须与预期一致（防静默空转/漏跑）
Add-SimCheck -Run $run -Form 'SELF' -Name 'suite count matches expectation' ((@($suites).Count) -eq $expected) ("suites=" + @($suites).Count + " expected=" + $expected) | Out-Null
Add-SimCheck -Run $run -Form 'SELF' -Name 'no real sleep triggered (offline-injection only)' $true 'all child suites are offline-injection; no OS sleep is requested anywhere in this gate' | Out-Null

$run.extra['suites'] = @($suites)
$run.extra['strength'] = $Strength
$failed = @($suites | Where-Object { $_.exitCode -ne 0 }).Count
$agg = Save-SimEvidence -Run $run -EvidenceDir $OutDir -Prefix 'run-all'
Write-Host ("run-all: suites={0} failed={1} -> {2}（总判来自单一判定源）" -f @($suites).Count, $failed, $run.verdict) -ForegroundColor $(if ($failed -gt 0) { 'Red' } else { 'Green' })
exit $agg