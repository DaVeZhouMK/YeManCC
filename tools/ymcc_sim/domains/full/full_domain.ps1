<#
.SYNOPSIS
  整机虚拟测试域（ymcc_sim -Domain full）：把手柄域与睡眠域组合成一次运行。

.DESCRIPTION
  204-N：-Mode run 已实现（原为骨架 NOT_IMPLEMENTED）。组合原则：
    * 必需段（offline-injection，本机可跑）：睡眠域 sleeptypes（离线判型）+ 睡眠域 forms（20 形态，沙箱宿主）
      + 工具总门 run_all -Quick（工具门/不变量/极端/回归/gyro/信号）。任一段非 0 ⇒ 总判 FAIL。
    * 窗口段（live-app-injection，需 App + 授权窗口）：睡眠域 live（真实 App 边沿 + 自有关闭 + 重启探测）
      + 手柄域 -Scenario sleep（合成电源广播）。不带 -WithApp 时记为 NOT_RUN（不判失败、不假装跑过）。
    * 合并报告：逐段记录退出码、证据文件路径与哈希、口径；总判来自共享判定源（Complete-SimRun）。
  不做假实现：没有窗口就不写"已跑"，没有设备就不写"已验证"。

.PARAMETER Mode
  plan（默认）= 只解析组合清单与可用性，不运行任何东西
  run          = 组合执行（必需段跑；窗口段需 -WithApp 且管理员会话）

.PARAMETER WithApp
  run 模式下追加窗口段（需要已在运行的 App 或用 -StartApp；App 为 requireAdministrator，需管理员会话）。

.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain full -Mode plan
.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain full -Mode run
.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain full -Mode run -WithApp -Elevate   # 授权窗口 + 操作者在场
#>
[CmdletBinding()]
param(
  [ValidateSet('plan', 'run')][string]$Mode = 'plan',
  [switch]$WithApp,
  [switch]$DryRun,
  [switch]$SkipForms,
  [switch]$SkipToolGate,
  [string]$EvidenceDir = ''
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
. (Join-Path $simRoot 'lib\sim_core.ps1')
$frontDoor = Join-Path $simRoot 'ymcc_sim.ps1'

$D = Get-SimDefaults
if (-not $EvidenceDir) { $EvidenceDir = $D.evidenceDir }
if (-not (Test-Path -LiteralPath $EvidenceDir)) { New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null }
$segDir = Join-Path $EvidenceDir 'full-segments'
New-Item -ItemType Directory -Force -Path $segDir | Out-Null

$script:Run = New-SimRun -Domain 'full' -Mode $Mode -ModeKind 'offline-injection' -ToolPath $PSCommandPath

# 组合清单：kind = required（本机可跑）| window（需 App + 授权窗口）
$segments = @(
  [ordered]@{ name = 'sleep-typing'; kind = 'required'; args = @('-Domain', 'sleep', '-Mode', 'sleeptypes'); skip = $false }
  [ordered]@{ name = 'sleep-forms';  kind = 'required'; args = @('-Domain', 'sleep', '-Mode', 'forms');      skip = [bool]$SkipForms }
  [ordered]@{ name = 'sim-suites';   kind = 'required'; args = @('-File', (Join-Path $simRoot 'tests\run_all.ps1'), '-Quick'); skip = [bool]$SkipToolGate }
  [ordered]@{ name = 'sleep-live';   kind = 'window';   args = @('-Domain', 'sleep', '-Mode', 'live');      skip = $false }
  [ordered]@{ name = 'pad-sleep';    kind = 'window';   args = @('-Domain', 'gamepad', '-Scenario', 'sleep', '-Confirm'); skip = $false }
)

$exitCode = 5
try {
  Write-Host '=== 整机虚拟测试域（ymcc_sim -Domain full）===' -ForegroundColor White
  Write-Host ("mode={0} withApp={1}" -f $Mode, [bool]$WithApp) -ForegroundColor Gray

  $readyList = @()
  foreach ($s in $segments) {
    $ready = $true
    $note = ''
    switch ($s.name) {
      'sleep-typing' { $note = '离线判型（不触发睡眠）' }
      'sleep-forms'  { $note = '20 形态注入（沙箱宿主，自启自清）' }
      'sim-suites'   { $note = '工具总门 -Quick（tool_gate/invariants/extremes/regressions/gyro_core/signals/api_contract = 7 套件；不带 -Quick 时含 cycle = 8）' }
      'sleep-live'   { $ready = [bool]$WithApp; $note = '真实 App 边沿 + 自有关闭 + 重启探测（需授权窗口）' }
      'pad-sleep'    { $ready = [bool]$WithApp; $note = '合成电源广播（需 App 在跑 + 操作者在场；入睡会冻结最大工作集进程）' }
    }
    $readyList += [ordered]@{ name = $s.name; kind = $s.kind; ready = $ready; args = $s.args; note = $note; skip = [bool]$s.skip }
  }
  $script:Run.extra['segments'] = $readyList

  if ($Mode -eq 'plan') {
    foreach ($s in $readyList) { Add-SimCheck -Run $script:Run -Form 'PLAN' -Name ("segment {0}" -f $s.name) -Ok $true -Detail ("kind={0} runnable={1} :: {2}" -f $s.kind, $s.ready, $s.note) | Out-Null }
    Add-SimCheck -Run $script:Run -Form 'PLAN' -Name 'plan-only (no action taken)' -Ok $true -Detail '组合清单已解析；-Mode run 执行必需段，-WithApp 追加窗口段' | Out-Null
    $script:Run.extra['verdict'] = 'PLAN'
    $exitCode = 0
  } else {
    if ($WithApp -and -not (Get-SimElevated)) {
      Add-SimCheck -Run $script:Run -Form 'RUN' -Name 'elevated session for the window segments' -Ok $false -Required $false -Detail '窗口段需要管理员会话（App requireAdministrator）——请 -Elevate 重跑' | Out-Null
      Add-SimStatus -Run $script:Run -Scope 'window-segments' -Status 'BLOCKED' -Detail 'needs-elevation' -Required $false
      Add-SimEvent -Run $script:Run -Kind 'flag' -Detail 'needs elevation for the requested window segments' -Data @{ key = 'NEEDS_ELEVATION' }
      $script:Run.extra['verdict'] = 'NEEDS_ELEVATION'
      $exitCode = 6
    } else {
      foreach ($s in $readyList) {
        if ($s.skip) {
          Add-SimStatus -Run $script:Run -Scope $s.name -Status 'NOT_RUN' -Detail 'segment skipped by switch (-SkipForms/-SkipToolGate)' -Required $false
          Add-SimCheck -Run $script:Run -Form 'SEGMENT' -Name ("{0} (skipped)" -f $s.name) -Ok $true -Required $false -Detail 'skipped by switch' | Out-Null
          continue
        }
        if (-not $s.ready) {
          Add-SimStatus -Run $script:Run -Scope $s.name -Status 'NOT_RUN' -Detail ("window segment not requested (-WithApp) :: {0}" -f $s.note) -Required $false
          Add-SimCheck -Run $script:Run -Form 'SEGMENT' -Name ("{0} (not run)" -f $s.name) -Ok $true -Required $false -Detail ("NOT_RUN :: {0}" -f $s.note) | Out-Null
          continue
        }
        if ($DryRun) {
          Add-SimCheck -Run $script:Run -Form 'SEGMENT' -Name ("{0} (dry-run)" -f $s.name) -Ok $true -Required $false -Detail ('would run: ' + ($s.args -join ' ')) | Out-Null
          continue
        }
        $childArgs = @()
        if ($s.args[0] -eq '-File') { $childArgs = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass') + $s.args }
        else { $childArgs = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $frontDoor) + $s.args }
        Write-Host ("--- [{0}] {1}" -f $s.name, $s.note) -ForegroundColor White
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $out = & powershell @childArgs 2>&1 | Out-String
        $code = $LASTEXITCODE
        $sw.Stop()
        $tail = (@($out -split "`r?`n") | Where-Object { "$_".Trim() -ne '' } | Select-Object -Last 2) -join ' | '
        $s['exitCode'] = [int]$code
        $s['seconds'] = [Math]::Round($sw.Elapsed.TotalSeconds, 1)
        $script:Run.extra[("segment-" + $s.name)] = [ordered]@{ exitCode = [int]$code; seconds = $s.seconds; tail = $tail }
        # 窗口段被请求（-WithApp）时同样计入必需：请求了就不得静默放过
        $required = [bool](($s.kind -eq 'required') -or $WithApp)
        Add-SimCheck -Run $script:Run -Form 'SEGMENT' -Name ("{0} exit=0" -f $s.name) -Ok ($code -eq 0) -Required $required `
          -Detail ("exit={0} {1}s :: {2}" -f $code, $s.seconds, $tail) | Out-Null
      }
      $exitCode = 0
    }
  }
}
catch {
  Write-Host ("执行中止：{0}" -f $_.Exception.Message) -ForegroundColor Red
  Add-SimCheck -Run $script:Run -Form 'RUN' -Name 'run completed' -Ok $false -Detail $_.Exception.Message | Out-Null
  $script:Run.extra['verdict'] = 'ABORTED'
  $exitCode = 3
}
finally {
  $sum = Get-SimRunSummary $script:Run
  $script:Run.extra['summary'] = $sum
  $script:Run.extra['toolIdentity'] = [ordered]@{
    domainScript = (Get-SimFileIdentity $PSCommandPath 'full_domain')
    coreLibrary = (Get-SimFileIdentity (Join-Path $simRoot 'lib\sim_core.ps1') 'sim_core')
    version = 'full_domain v2 (2026-09-23, 204-N: -Mode run composed; window segments require -WithApp)'
  }
  $agg = Save-SimEvidence -Run $script:Run -EvidenceDir $EvidenceDir -Prefix 'full-sim'
  if ($agg -ne 0) { $exitCode = $agg }   # SIM-1: single verdict source
  elseif ($Mode -eq 'run' -and $exitCode -eq 0 -and -not $WithApp) {
    $script:Run.extra['verdictNote'] = 'required segments all green; window segments were NOT_RUN (pass -WithApp in an authorized window)'
    Write-Host '必需段全绿；窗口段未跑（授权窗口内用 -WithApp 追加）' -ForegroundColor Yellow
  }
}
exit $exitCode