# extremes.ps1 - 极端情况模拟（204-I）：数据端口**不提供数据/不提供反馈**的各种形态。
# 覆盖（operator 要求）：
#   E1 silent      —— 服务端永不响应（"无数据、无反馈、连接挂着"）：工具必须有预算内超时，不得卡死
#   E2 drop        —— 连接被丢弃（RST）：必须记为请求失败，不得崩溃
#   E3 garbage     —— 截断/垃圾 JSON：不得把垃圾当成功；门判 state-read-failed
#   E4 noop-success——"声称成功但状态完全不变"（有反馈、实际无数据）：检查项必须能检出状态未变
#   E5 ec/hc 无数据 —— mock/HC 侧映射（enable 200 但 hardwareWritesObserved=false）：不得认作"写入被观测"
#   E6 fire-and-forget —— 只发送不等返回：请求失败被如实记录，整体 verdict 非 0
# 手柄侧（只登记规格，不改 W6 资产）：见输出末尾的 w6 断言清单。
#
# 用法：powershell -File tools\ymcc_sim\tests\extremes.ps1 [-OutDir <dir>]
[CmdletBinding()]
param([string]$OutDir = '')
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
. (Join-Path $simRoot 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $D.evidenceDir 'extremes' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:Cases = New-Object System.Collections.ArrayList
function Add-Case([string]$id, [string]$name, [bool]$ok, [string]$expected, [string]$actual) {
  $script:Cases.Add([ordered]@{ id = $id; name = $name; pass = [bool]$ok; expected = $expected; actual = $actual }) | Out-Null
  Write-Host ("  [{0}] {1} :: {2} (expected {3} / actual {4})" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $id, $name, $expected, $actual) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}
function Start-Stub([int]$Port, [string]$State) {
  $log = Join-Path $OutDir "stub-$State-$stamp.jsonl"
  if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
  $p = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $testsDir 'stub_host.ps1'), '-Port', "$Port", '-State', $State, '-LogPath', $log) -PassThru -WindowStyle Hidden
  for ($i = 0; $i -lt 40; $i++) { if (Test-SimPort $Port) { break }; Start-Sleep -Milliseconds 250 }
  return [ordered]@{ proc = $p; log = $log; port = $Port }
}
function Stop-Stub($stub) { try { if (-not $stub.proc.HasExited) { $stub.proc.Kill() } } catch { }; Start-Sleep -Milliseconds 250 }

Write-Host '=== 极端情况模拟（204-I）：无数据 / 无反馈 / 卡死 / 垃圾 / 声称成功但无效果 ===' -ForegroundColor White
$report = [ordered]@{ at = (Get-Date).ToString('o'); tool = (Get-SimFileIdentity (Join-Path $simRoot 'lib\sim_core.ps1') 'sim_core'); cases = @(); w6Requirements = @() }
$port = 8891

# E1 silent（永不响应）——必须有预算内超时，且门判失败
$stub = Start-Stub -Port $port -State 'silent'
$t0 = Get-Date
$sw = [Diagnostics.Stopwatch]::StartNew()
$st = Get-SimStateSafe -Base "http://127.0.0.1:$port" -Token 'x' -TimeoutMs 2500
$sw.Stop()
$gate = Test-SimActionGate -Base "http://127.0.0.1:$port" -Token 'x' -Kind 'external'
$elapsed = $sw.Elapsed.TotalSeconds
Stop-Stub $stub
Add-Case 'E1' 'silent（服务端永不响应）：请求在预算内失败、不卡死、门不放行' (($st.status -eq -1 -or $st.status -eq 0) -and $elapsed -lt 12 -and (-not $gate.allowed)) 'status=-1/超时<12s/allowed=false' ("status={0} elapsed={1:N1}s allowed={2} reason={3}" -f $st.status, $elapsed, $gate.allowed, $gate.reason)

# E2 drop（RST）
$stub = Start-Stub -Port $port -State 'drop'
$st2 = Get-SimStateSafe -Base "http://127.0.0.1:$port" -Token 'x' -TimeoutMs 3000
$gate2 = Test-SimActionGate -Base "http://127.0.0.1:$port" -Token 'x' -Kind 'external'
Stop-Stub $stub
Add-Case 'E2' 'drop（连接被丢弃）：记为请求失败、不崩溃、门不放行' (($st2.status -eq -1) -and (-not $gate2.allowed)) 'status=-1 / allowed=false' ("status={0} err={1} allowed={2}" -f $st2.status, $st2.errorCode, $gate2.allowed)

# E3 garbage（截断 JSON）
$stub = Start-Stub -Port $port -State 'garbage'
$st3 = Get-SimStateSafe -Base "http://127.0.0.1:$port" -Token 'x' -TimeoutMs 3000
$gate3 = Test-SimActionGate -Base "http://127.0.0.1:$port" -Token 'x' -Kind 'external'
Stop-Stub $stub
Add-Case 'E3' 'garbage（截断/垃圾 JSON）：不把垃圾当成功、门判 state-read-failed' (($st3.status -eq 200 -and $null -eq $st3.json) -and (-not $gate3.allowed) -and ("$($gate3.reason)" -like 'state-read-failed*')) '200 且 json=null / allowed=false / reason=state-read-failed' ("status={0} jsonNull={1} allowed={2} reason={3}" -f $st3.status, ($null -eq $st3.json), $gate3.allowed, $gate3.reason)

# E4 noop-success（声称成功但状态完全不变）
$stub = Start-Stub -Port $port -State 'noop-success'
$sus = Send-SimFanApi -Method POST -Path '/api/suspend' -Base "http://127.0.0.1:$port" -Token 'x' -Body '{"generation":1,"source":"selftest.native"}' -TimeoutMs 3000 -IsChangeCall
$stAfter = Get-SimStateSafe -Base "http://127.0.0.1:$port" -Token 'x' -TimeoutMs 3000
Stop-Stub $stub
$claimedOk = ($sus.status -eq 200)
$stateUnchanged = ("$($stAfter.json.state.state)" -eq 'Stopped')
Add-Case 'E4' 'noop-success（有反馈但实际无数据）：工具侧检查项能检出"状态未变"（不得认作成功）' ($claimedOk -and $stateUnchanged) '请求 200 但 state 仍 Stopped（检出"未变"）' ("httpStatus={0} stateAfter={1} unchanged={2}" -f $sus.status, $stAfter.json.state.state, $stateUnchanged)

# E5 EC/HC 无数据映射（mock 边界）：enable 声称 ok 但 hardwareWritesObserved 不成立 ⇒ 不得认作"写入被观测"
$stub = Start-Stub -Port $port -State 'noop-success'
$en = Send-SimFanApi -Method POST -Path '/api/enable' -Base "http://127.0.0.1:$port" -Token 'x' -Body '{"nodes":[],"leaseId":"x"}' -TimeoutMs 3000 -IsChangeCall
$stEn = Get-SimStateSafe -Base "http://127.0.0.1:$port" -Token 'x' -TimeoutMs 3000
Stop-Stub $stub
$hwFlag = "$($stEn.json.state.hardwareWritesObserved)"
Add-Case 'E5' 'EC/HC 无数据（mock 边界）：enable 声称 ok 但 hardwareWritesObserved 不成立 ⇒ 不得认作"写入被观测"' (($en.status -eq 200) -and ($hwFlag -ne 'True')) 'enable=200 且 hwWritesObserved≠True' ("enable={0} hwWritesObserved={1}" -f $en.status, $hwFlag)

# E6 fire-and-forget：只发送不等返回（silent 下的变更调用）
$stub = Start-Stub -Port $port -State 'silent'
$fire = Send-SimFanApi -Method POST -Path '/api/suspend' -Base "http://127.0.0.1:$port" -Token 'x' -Body '{"generation":2}' -TimeoutMs 1500 -IsChangeCall
$posts = 0
if (Test-Path -LiteralPath $stub.log) { $posts = @(Get-Content -LiteralPath $stub.log -Encoding UTF8 | Where-Object { $_ -match '"ev":"request"' -and $_ -match '"method":"POST"' }).Count }
Stop-Stub $stub
Add-Case 'E6' 'fire-and-forget（只发送、无返回）：请求失败被如实记录，服务端收到过该请求' (($fire.status -eq -1) -and ($posts -ge 1)) 'status=-1 且服务端日志有该 POST' ("status={0} serverSawPosts={1}" -f $fire.status, $posts)

# E7 风扇"无反馈"（EC/HC 永不回来）：历史缺陷对照 —— 大修前 host 会**一直等 HC 的 EC 成功反馈**
# 做法：沙箱宿主 + 曲线会话 → 入睡（需要重建而不可证明）→ **不给任何后续动作** → 观测 70 s
# 断言：a) 服务仍可用（14/14 采样可读）；b) 状态有界且末段稳定（不无限重试/不震荡）；c) 不伪造成功
$e7Port = 8892
$e7Dir = Join-Path $OutDir 'e7-sandbox'
New-Item -ItemType Directory -Force -Path $e7Dir | Out-Null
$e7Tok = Join-Path $e7Dir 'YeManFanHost.session'
New-SimFanSessionToken $e7Tok | Out-Null
$e7Proc = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$e7Port", '--protocol-version', '2', '--session-token-file', $e7Tok, '--mock-handshake') -PassThru -WindowStyle Hidden
$e7Ready = Wait-SimHostReady -Port $e7Port -TokenPath $e7Tok -TimeoutSec 60
$e7Base = "http://127.0.0.1:$e7Port"
$e7t = Get-SimFanSessionToken $e7Tok
$e7Samples = @(); $e7ReadOk = 0; $e7SuspendState = 'n/a'
if ($e7Ready.ready -and $e7t) {
  $null = Send-SimFanApi -Method POST -Path '/api/handshake' -Base $e7Base -Token $e7t -Body '{}' -TimeoutMs 8000
  $null = Send-SimFanApi -Method POST -Path '/api/open' -Base $e7Base -Token $e7t -Body '{}' -TimeoutMs 8000
  $null = Send-SimFanApi -Method POST -Path '/api/open-events' -Base $e7Base -Token $e7t -Body '{}' -TimeoutMs 8000
  $lease = Send-SimFanApi -Method POST -Path '/api/acquire-control-lease' -Base $e7Base -Token $e7t -Body '{}' -TimeoutMs 8000
  $curveE7 = '{"nodes":[{"tempC":0,"dutyPercent":0},{"tempC":55,"dutyPercent":30},{"tempC":75,"dutyPercent":60},{"tempC":90,"dutyPercent":100}],"leaseId":"' + $lease.json.leaseId + '"}'
  $null = Send-SimFanApi -Method POST -Path '/api/enable' -Base $e7Base -Token $e7t -Body $curveE7 -TimeoutMs 8000
  $spE7 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $e7Base -Token $e7t -Body '{"generation":1,"source":"selftest.native"}' -TimeoutMs 8000 -IsChangeCall
  if ($spE7 -and $spE7.json) { $e7SuspendState = "$($spE7.json.state.state)" }
  for ($i = 1; $i -le 14; $i++) {
    Start-Sleep -Seconds 5
    $st = Send-SimFanApi -Method GET -Path '/api/state' -Base $e7Base -Token $e7t -TimeoutMs 4000
    if ($st.status -eq 200) { $e7ReadOk++ }
    $e7Samples += [ordered]@{ i = $i; status = $st.status; state = "$($st.json.state.state)"; powerState = "$($st.json.state.powerState)"; evidence = "$($st.json.state.oemRestoreEvidence)" }
  }
}
$e7States = @($e7Samples | ForEach-Object { $_.state } | Select-Object -Unique)
$e7Tail = @($e7Samples | Select-Object -Last 3 | ForEach-Object { $_.state } | Select-Object -Unique)
$e7Ok = ($e7Ready.ready) -and ($e7ReadOk -eq 14) -and ($e7States.Count -le 3) -and ($e7Tail.Count -eq 1) -and (@($e7Samples | Where-Object { $_.evidence -eq 'hc-default-table-readback-confirmed' }).Count -eq 0)
Add-Case 'E7' '无反馈（EC/HC 永不回来）：服务仍可用、状态有界且末段稳定、不伪造成功（对照历史"一直等 HC 的 EC 反馈"）' $e7Ok 'ready=1 / 14-14 采样可读 / distinctStates<=3 / 末段稳定 / 无物理读回伪装' ("ready={0} reads={1}/14 distinct={2} tail={3} suspendState={4} evidence={5}" -f $e7Ready.ready, $e7ReadOk, $e7States.Count, ($e7Tail -join ','), $e7SuspendState, ((@($e7Samples | ForEach-Object { $_.evidence } | Select-Object -Unique)) -join ','))
$report.e7Samples = $e7Samples
if ($e7Ready.ready -and $e7t) { $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $e7Base -Token $e7t -Body '{}' -TimeoutMs 5000 }
Start-Sleep -Seconds 2

# 手柄侧（规格登记；不改 W6 资产）
$report.w6Requirements = @(
  'input-host 无返回：探针/自环必须超时退出且非 0（不得把"无返回"当通过）',
  'input-host 返回成功但无变化（noop-success）：必须校验可观测副作用（如 slot/设备节点/帧计数），不得只信响应码',
  'input-host 只发送无返回（fire-and-forget）：必须记录尝试与失败，且不重复无限重试（有预算）',
  'input-host 垃圾/截断响应：解析失败必须判 UNKNOWN/FAIL，不得崩溃',
  'input-host 中途消失（进程在会话中被杀）：必须检出并给出明确失败原因，不静默成功',
  'W6 认领方式：可向 Fan 提交上述 ymcc_sim 场景/断言需求；Fan 回交原始工具证据，不代签 W6 验收（203 §联合通知）'
)

$failed = @($script:Cases | Where-Object { -not $_.pass })
$verdict = if ($failed.Count -eq 0) { 'PASS' } else { 'FAIL' }
$report.cases = @($script:Cases); $report.failed = $failed.Count; $report.verdict = $verdict
$report.note = '桩件级极端场景（工具侧断言）；EC/HC 真机"无数据返回"仍需设备（DEVICE_PENDING）'
$jf = Join-Path $OutDir ("extremes-$stamp.json")
[IO.File]::WriteAllText($jf, ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Host ''
Write-Host ("cases={0} failed={1}" -f @($script:Cases).Count, $failed.Count) -ForegroundColor $(if ($failed.Count) { 'Red' } else { 'Green' })
Write-Host ("EXTREMES={0}  report={1}" -f $verdict, $jf) -ForegroundColor $(if ($verdict -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host '手柄侧断言清单（供 W6 认领）:' -ForegroundColor White
$report.w6Requirements | ForEach-Object { Write-Host ("  - " + $_) -ForegroundColor Gray }
if ($verdict -eq 'PASS') { exit 0 } else { exit 2 }