# api_contract.ps1 - 风扇侧 API 契约负例（204-N）：把公开 API 的"必须拒绝"面变成可 FAIL 的断言。
#   契约真值取自产品源码（YeManCC\FanLab\real-host\Program.cs，只读）：
#     401 API_SESSION_REQUIRED          IsAuthorizedRequest 常量时间比对        (:11329-11337)
#     404 NOT_FOUND                     路由兜底                              (:11286)
#     400 INVALID_JSON                  JsonException 分支                     (:11310-11314)
#     413 BODY_TOO_LARGE                ReadJsonAsync >1 MiB                   (:11347)
#     400 POWER_GENERATION_REQUIRED     RequireTaggedPowerCommand（gen>0+source）(:11193-11204)
#     403 F4_NATIVE_AUTHORITY_REQUIRED  RequireNativeSuspendAuthority 仅 /api/suspend (:11210-11217)
#   约束：只打本脚本自启的沙箱宿主（无 --real-backend），自启自清；不触发睡眠；产品零改动。
[CmdletBinding()]
param([string]$OutDir = '')
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
. (Join-Path $simRoot 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $D.evidenceDir 'api-contract' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:Cases = New-Object System.Collections.ArrayList
# 注意：函数名不得用短别名（D-204-17：R 命中 Invoke-History 曾静默空转）
function Check-C([string]$id, [string]$name, [bool]$ok, [string]$actual) {
  $script:Cases.Add([ordered]@{ id = $id; name = $name; pass = [bool]$ok; actual = $actual }) | Out-Null
  Write-Host ("  [{0}] {1} :: {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $id, $actual) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}

# AC13（2026-09-23 架构纪律，operator 裁决）：遗留的 mock-handshake.flag **不得**开启 mock——
# 只有显式 `--mock-handshake`（AI 代码/模拟器专用）才行，用户路径（App 只传 real-backend 等）永不触达。
# 做法：如宿主 exe 同目录存在该遗留旗标，则**不带开关**启动一次，断言 hostMode ≠ mock-handshake。
$ac13ok = $true
$ac13detail = 'no stray flag next to the host exe; nothing to prove (informational)'
$strayFlag = Join-Path (Split-Path -Parent $D.hostExe) 'mock-handshake.flag'
if (Test-Path -LiteralPath $strayFlag) {
  $p13 = 8896
  $sand13 = Join-Path $OutDir 'sandbox-ac13'; New-Item -ItemType Directory -Force -Path $sand13 | Out-Null
  $tf13 = Join-Path $sand13 'ac13.session'; New-SimFanSessionToken $tf13 | Out-Null
  $pr13 = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$p13", '--protocol-version', '2', '--session-token-file', $tf13) -PassThru -WindowStyle Hidden
  $r13 = Wait-SimHostReady -Port $p13 -TokenPath $tf13 -TimeoutSec 45
  if ($r13.ready) {
    $t13 = Get-SimFanSessionToken $tf13
    $s13 = Send-SimFanApi -Method GET -Path '/api/state' -Base "http://127.0.0.1:$p13" -Token $t13 -TimeoutMs 8000
    $ac13ok = ("$($s13.json.state.hostMode)" -ne 'mock-handshake')
    $ac13detail = ("strayFlag=present arg=absent hostMode={0} hwWrites={1}" -f $s13.json.state.hostMode, $s13.json.state.hardwareWritesEnabled)
    $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base "http://127.0.0.1:$p13" -Token $t13 -Body '{}' -TimeoutMs 5000
    $t0 = Get-Date; while ((Test-SimPort $p13) -and ((Get-Date) - $t0).TotalSeconds -lt 20) { Start-Sleep -Milliseconds 500 }
  } else {
    $ac13ok = $false
    $ac13detail = ("strayFlag=present but probe host not ready (exited={0})" -f $pr13.HasExited)
  }
}
Check-C 'AC13' '遗留 mock-handshake.flag 不得开启 mock（仅 --mock-handshake 开关有效；用户路径永不触达）' $ac13ok $ac13detail

$port = 8898
$sandDir = Join-Path $OutDir 'sandbox'; New-Item -ItemType Directory -Force -Path $sandDir | Out-Null
$tokFile = Join-Path $sandDir 'YeManFanHost.session'
# D-204-21：空闲后首跑偶发"宿主未就绪"（不可复现，已两次出现）⇒ 对本轮自启的宿主做一次有界重试（自愈），
# 并把次数写进证据；重试仍失败则按原样报未就绪，绝不假装已就绪。
$proc = $null; $ready = $null; $hostAttempts = 0
while ($hostAttempts -lt 2 -and -not ($ready -and $ready.ready)) {
  $hostAttempts++
  if ($proc -and -not $proc.HasExited) { try { $proc.Kill() } catch { }; Start-Sleep -Seconds 2 }
  if (Test-Path -LiteralPath $tokFile) { Remove-Item -LiteralPath $tokFile -Force -ErrorAction SilentlyContinue }
  New-SimFanSessionToken $tokFile | Out-Null
  $proc = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$port", '--protocol-version', '2', '--session-token-file', $tokFile, '--mock-handshake') -PassThru -WindowStyle Hidden
  $ready = Wait-SimHostReady -Port $port -TokenPath $tokFile -TimeoutSec 60
  if (-not $ready.ready) {
    $proc.Refresh()
    Write-Host ("宿主第 {0} 次未就绪（exited={1} exitCode={2} lastHealth={3}）" -f $hostAttempts, $proc.HasExited, $(if ($proc.HasExited) { $proc.ExitCode } else { 'running' }), $ready.lastStatus) -ForegroundColor Yellow
  }
}
$tok = Get-SimFanSessionToken $tokFile
$base = "http://127.0.0.1:$port"
$proc.Refresh()
$hostOk = [bool]$ready.ready -and [bool]$tok
Write-Host ("契约宿主就绪={0} port={1} attempts={2} exited={3} exitCode={4}" -f $hostOk, $port, $hostAttempts, $proc.HasExited, $(if ($proc.HasExited) { $proc.ExitCode } else { 'running' })) -ForegroundColor Gray

# AC1 认证：缺令牌 / 错令牌一律 401，且不得泄露任何状态
$noTok = Send-SimFanApi -Method GET -Path '/api/state' -Base $base -Token '' -TimeoutMs 5000
$badTok = Send-SimFanApi -Method GET -Path '/api/state' -Base $base -Token 'deadbeefdeadbeefdeadbeefdeadbeef' -TimeoutMs 5000
Check-C 'AC1' '认证：缺令牌与错令牌一律 401 API_SESSION_REQUIRED（无误状态泄露）' `
  (($noTok.status -eq 401) -and ($noTok.errorCode -eq 'API_SESSION_REQUIRED') -and ($badTok.status -eq 401) -and ($badTok.errorCode -eq 'API_SESSION_REQUIRED') -and ($null -eq $noTok.json.state)) `
  ("missing={0}/{1} wrong={2}/{3} stateLeak={4}" -f $noTok.status, $noTok.errorCode, $badTok.status, $badTok.errorCode, ($null -ne $noTok.json.state))

# AC2 未知路由 / 方法不匹配 ⇒ 404（已认证）
$u1 = Send-SimFanApi -Method POST -Path '/api/does-not-exist' -Base $base -Token $tok -Body '{}' -TimeoutMs 5000
$u2 = Send-SimFanApi -Method GET -Path '/api/suspend' -Base $base -Token $tok -TimeoutMs 5000
Check-C 'AC2' '未知路由与方法不匹配一律 404 NOT_FOUND' (($u1.status -eq 404) -and ($u1.errorCode -eq 'NOT_FOUND') -and ($u2.status -eq 404)) ("unknown={0}/{1} getSuspend={2}" -f $u1.status, $u1.errorCode, $u2.status)

# AC3 非法 JSON ⇒ 400 INVALID_JSON（截断体，与桩件 garbage 同族但这里打真宿主）
$m1 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":1,' -TimeoutMs 5000
Check-C 'AC3' '截断/非法 JSON ⇒ 400 INVALID_JSON（不得 500 或静默接受）' (($m1.status -eq 400) -and ($m1.errorCode -eq 'INVALID_JSON')) ("status={0} code={1}" -f $m1.status, $m1.errorCode)

# AC4 超大请求体 ⇒ 413 BODY_TOO_LARGE（1.1 MiB）
$big = '{"x":"' + ('a' * (1100 * 1024)) + '"}'
$m2 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body $big -TimeoutMs 15000
Check-C 'AC4' '超大请求体 ⇒ 413 BODY_TOO_LARGE（有界）' (($m2.status -eq 413) -and ($m2.errorCode -eq 'BODY_TOO_LARGE')) ("status={0} code={1}" -f $m2.status, $m2.errorCode)

# ── AC5/AC6 期间的零副作用基线（SIM-2 口径：读只走 GET /api/state） ──
$snapBefore = Get-SimStateSafe -Base $base -Token $tok
# AC5 代次/来源非法族 ⇒ 400 POWER_GENERATION_REQUIRED
$a1 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{}' -TimeoutMs 5000 -IsChangeCall
$a2 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":0,"source":"selftest.native"}' -TimeoutMs 5000 -IsChangeCall
$a3 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":-1,"source":"selftest.native"}' -TimeoutMs 5000 -IsChangeCall
$a4 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":"7","source":"selftest.native"}' -TimeoutMs 5000 -IsChangeCall
$a5 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":5}' -TimeoutMs 5000 -IsChangeCall
$ac5ok = @($a1, $a2, $a3, $a4, $a5 | Where-Object { $_.status -eq 400 -and $_.errorCode -eq 'POWER_GENERATION_REQUIRED' }).Count -eq 5
Check-C 'AC5' '代次/来源非法族（缺体/0/负数/字符串/缺 source）一律 400 POWER_GENERATION_REQUIRED' $ac5ok ("statuses=" + ((@($a1, $a2, $a3, $a4, $a5 | ForEach-Object { "$($_.status):$($_.errorCode)" })) -join ','))

# AC6 越权来源 ⇒ 403，且被拒调用必须零状态变更（gen 与 state 都不许动）
$a6 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":7,"source":"ui.renderer"}' -TimeoutMs 6000 -IsChangeCall
$snapAfter = Get-SimStateSafe -Base $base -Token $tok
$same = ($snapBefore.status -eq 200) -and ($snapAfter.status -eq 200) -and
        ("$($snapBefore.json.state.state)" -eq "$($snapAfter.json.state.state)") -and
        ("$($snapBefore.json.state.powerState)" -eq "$($snapAfter.json.state.powerState)") -and
        ("$($snapBefore.json.state.powerOperationGeneration)" -eq "$($snapAfter.json.state.powerOperationGeneration)") -and
        ("$($snapBefore.json.state.powerOperationAttempt)" -eq "$($snapAfter.json.state.powerOperationAttempt)")
Check-C 'AC6' '越权来源（renderer）⇒ 403 F4_NATIVE_AUTHORITY_REQUIRED，且被拒调用零状态变更（state/powerState/gen/attempt 全不动）' `
  (($a6.status -eq 403) -and ($a6.errorCode -eq 'F4_NATIVE_AUTHORITY_REQUIRED') -and $same) `
  ("status={0}/{1} stateUnchanged={2} ({3}->{4}) powerState {5}->{6} gen {7}->{8} attempt {9}->{10}" -f $a6.status, $a6.errorCode, $same, $snapBefore.json.state.state, $snapAfter.json.state.state, $snapBefore.json.state.powerState, $snapAfter.json.state.powerState, $snapBefore.json.state.powerOperationGeneration, $snapAfter.json.state.powerOperationGeneration, $snapBefore.json.state.powerOperationAttempt, $snapAfter.json.state.powerOperationAttempt)

# AC7 resume 同样必须携带有效 generation/source（tagged command）
$r1 = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body '{}' -TimeoutMs 5000 -IsChangeCall
Check-C 'AC7' 'resume 缺 generation/source ⇒ 400 POWER_GENERATION_REQUIRED（同样受 tagged 约束）' (($r1.status -eq 400) -and ($r1.errorCode -eq 'POWER_GENERATION_REQUIRED')) ("status={0} code={1}" -f $r1.status, $r1.errorCode)

# AC8 并发风暴：12 个并行边沿（6 组 suspend+resume）⇒ 无 5xx、无挂起、宿主存活、无未知态
$tasks = New-Object System.Collections.Generic.List[System.Threading.Tasks.Task]
$client = [System.Net.Http.HttpClient]::new()
$client.Timeout = [TimeSpan]::FromSeconds(25)
try {
  foreach ($k in 0..5) {
    $g = 200 + $k
    foreach ($p in @('suspend', 'resume')) {
      $req = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::Post, "$base/api/$p")
      $req.Headers.Add('X-YeMan-Fan-Session', $tok)
      $req.Content = [System.Net.Http.StringContent]::new(('{"generation":' + $g + ',"source":"selftest.native"}'), [Text.Encoding]::UTF8, 'application/json')
      $tasks.Add($client.SendAsync($req))
    }
  }
  $joined = [System.Threading.Tasks.Task]::WhenAll($tasks.ToArray())
  $allDone = $false
  try { $allDone = $joined.Wait(30000) } catch { $allDone = $false }   # 有任务 fault（如连接被拒）时 Wait 会抛 AggregateException
  $codes = @()
  foreach ($t in $tasks) { try { $codes += [int]$t.Result.StatusCode } catch { $codes += -1 } }
} finally { $client.Dispose() }
Start-Sleep -Seconds 2
$aliveAfter = -not $proc.HasExited
$snapRace = Get-SimStateSafe -Base $base -Token $tok -TimeoutMs 8000
$legal = @($codes | Where-Object { $_ -notin @(200, 409) })
$raceOk = $allDone -and ($aliveAfter) -and (@($legal).Count -eq 0) -and ($snapRace.status -eq 200) -and ("$($snapRace.json.state.state)" -ne '') -and ($snapRace.json.state.unknownState -eq $false)
Check-C 'AC8' '并发风暴（12 并行边沿）⇒ 仅 200/409、不挂起、宿主存活、状态非未知' $raceOk `
  ("allDone={0} alive={1} codes={2} state={3} unknown={4}" -f $allDone, $aliveAfter, (($codes | Sort-Object) -join ','), $snapRace.json.state.state, $snapRace.json.state.unknownState)

# AC9 正控（非空转）：合法的 suspend/resume 仍必须成功，证明上面 4xx 不是"全拒"造成的假绿
$p1 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":500,"source":"selftest.native"}' -TimeoutMs 8000 -IsChangeCall
Start-Sleep -Milliseconds 250
$p2 = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body '{"generation":500,"source":"selftest.native"}' -TimeoutMs 8000 -IsChangeCall
$posOk = ($p1.status -eq 200) -and ("$($p1.json.state.state)" -eq 'Suspended') -and ($p2.status -eq 200) -and ("$($p2.json.state.state)" -ne 'Suspended')
Check-C 'AC9' '正控：合法 native 边沿仍成功（非空转证明）' $posOk ("suspend={0}/{1} resume={2}/{3}" -f $p1.status, $p1.json.state.state, $p2.status, $p2.json.state.state)

# AC11 乱序代次（204 §6 B/P1「A→B→C→迟到 A」）：C 成功后，迟到的旧代次必须被拒且状态不得回退
$gA = 700; $gB = 701; $gC = 702
$null = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body ('{"generation":' + $gA + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
$null = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body ('{"generation":' + $gA + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
$null = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body ('{"generation":' + $gB + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
$null = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body ('{"generation":' + $gB + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
$null = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body ('{"generation":' + $gC + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
$resC = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body ('{"generation":' + $gC + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
Start-Sleep -Milliseconds 300
$snapC = Get-SimStateSafe -Base $base -Token $tok
$lateA = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body ('{"generation":' + $gA + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
Start-Sleep -Milliseconds 300
$snapLate = Get-SimStateSafe -Base $base -Token $tok
$lateOk = ($resC.status -eq 200) -and ($lateA.status -ne 200) -and
          ("$($snapC.json.state.state)" -eq "$($snapLate.json.state.state)") -and
          ("$($snapC.json.state.powerOperationGeneration)" -eq "$($snapLate.json.state.powerOperationGeneration)")
Check-C 'AC11' '乱序代次：A→B→C 成功后迟到的 A 被拒，且状态与代次不回退（旧任务不得覆盖新结果）' $lateOk `
  ("resumeC={0} lateA={1}/{2} state {3}->{4} gen {5}->{6}" -f $resC.status, $lateA.status, $lateA.errorCode, $snapC.json.state.state, $snapLate.json.state.state, $snapC.json.state.powerOperationGeneration, $snapLate.json.state.powerOperationGeneration)

# AC12（204 §6 B/P2 第二半）：故障→release→安全清理 之后，"下一次正常操作确实成功"在新 host 世代上成立
#   契约链（源码锚点 Program.cs:8061-8131）：open→open-events→acquire-control→enable(curve)→suspend 即 FaultLocked
#   →resume 409→release-control 200（清 lease+曲线，但本世代 hardwareWritesObserved 仍为 true）
#   →/api/disable 200（安全清理可达，无需 lease）→ 旧宿主 shutdown → 新世代宿主 → 普通 suspend/resume 必须成功
$gF = 800; $gG = 900
$acOk = $false; $acDetail = 'not-run'
if ($hostOk) {
  $null = Send-SimFanApi -Method POST -Path '/api/open' -Base $base -Token $tok -Body '{}' -TimeoutMs 8000 -IsChangeCall
  $null = Send-SimFanApi -Method POST -Path '/api/open-events' -Base $base -Token $tok -Body '{}' -TimeoutMs 8000 -IsChangeCall
  $acq = Send-SimFanApi -Method POST -Path '/api/acquire-control' -Base $base -Token $tok -Body '{}' -TimeoutMs 8000 -IsChangeCall
  $lease = "$($acq.json.lease.leaseId)"
  $curve = '{"nodes":[{"tempC":0,"dutyPercent":0},{"tempC":55,"dutyPercent":30},{"tempC":75,"dutyPercent":60},{"tempC":90,"dutyPercent":100}],"leaseId":"' + $lease + '"}'
  $en = Send-SimFanApi -Method POST -Path '/api/enable' -Base $base -Token $tok -Body $curve -TimeoutMs 8000 -IsChangeCall
  $sf = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body ('{"generation":' + $gF + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
  $rf = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body ('{"generation":' + $gF + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
  $relc = Send-SimFanApi -Method POST -Path '/api/release-control' -Base $base -Token $tok -Body ('{"leaseId":"' + $lease + '"}') -TimeoutMs 8000 -IsChangeCall
  $dis = Send-SimFanApi -Method POST -Path '/api/disable' -Base $base -Token $tok -Body '{}' -TimeoutMs 8000 -IsChangeCall
  $detail1 = ("enable={0} fault={1}/{2} resume={3}/{4} release={5} disable={6}" -f $en.status, $sf.status, $sf.json.state.state, $rf.status, $rf.errorCode, $relc.status, $dis.status)
  # 关闭旧世代，等端口释放后启新世代（新 hostInstanceId）
  $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $base -Token $tok -Body '{}' -TimeoutMs 5000
  $t0 = Get-Date
  while ((Test-SimPort $port) -and ((Get-Date) - $t0).TotalSeconds -lt 25) { Start-Sleep -Milliseconds 500 }
  New-SimFanSessionToken $tokFile | Out-Null
  $proc2 = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$port", '--protocol-version', '2', '--session-token-file', $tokFile, '--mock-handshake') -PassThru -WindowStyle Hidden
  $ready2 = Wait-SimHostReady -Port $port -TokenPath $tokFile -TimeoutSec 60
  $tok2 = Get-SimFanSessionToken $tokFile
  if ($ready2.ready -and $tok2) {
    $null = Send-SimFanApi -Method POST -Path '/api/handshake' -Base $base -Token $tok2 -Body '{}' -TimeoutMs 8000
    $sG = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok2 -Body ('{"generation":' + $gG + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
    Start-Sleep -Milliseconds 300
    $rG = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok2 -Body ('{"generation":' + $gG + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
    $acOk = ($en.status -eq 200) -and ($sf.status -eq 200) -and ("$($sf.json.state.state)" -eq 'FaultLocked') -and ($rf.status -eq 409) -and
            ($relc.status -eq 200) -and ($relc.json.state.state -ne 'FaultLocked') -and ($dis.status -eq 200) -and
            $ready2.ready -and ($sG.status -eq 200) -and ("$($sG.json.state.state)" -eq 'Suspended') -and ($rG.status -eq 200) -and ("$($rG.json.state.state)" -ne 'Suspended')
    $acDetail = ($detail1 + " | freshHost={0} suspend={1}/{2} resume={3}/{4}" -f $ready2.ready, $sG.status, $sG.json.state.state, $rG.status, $rG.json.state.state)
    $tok = $tok2; $proc = $proc2
  } else {
    $acDetail = ($detail1 + " | freshHost=not-ready（旧世代已 shut down，故后续 AC10 判据按未就绪处理）")
  }
}
Check-C 'AC12' '故障→release→安全清理后，"下一次正常操作"在新 host 世代上确实成功（P2 第二半）' $acOk $acDetail

# 收尾：关闭本脚本自启的宿主
if ($hostOk) { $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $base -Token $tok -Body '{}' -TimeoutMs 5000 }
Start-Sleep -Seconds 3
$left = @(Get-SimHostInstance)
$closed = (@($left).Count -eq 0) -and (-not (Test-SimPort $port))
Check-C 'AC10' '退出零残留：契约宿主自启自清（端口释放、无遗留实例）' $closed ("leftHosts=" + @($left).Count + " portOpen=" + (Test-SimPort $port))

# AC14（2026-09-23 T0，operator 裁决：绝不允许"父进程已退出而宿主不退"的死循环）：
# 用 --parent-pid 指向一个仿真父进程起动宿主，杀掉父进程后断言**宿主有界退出**且端口释放。
# 该宿主处于非 Stopped 态（mock 下 finalize 判据本就不满足）⇒ 走的正是"预算耗尽即退出"这条路，
# 与旧版掌机上"故障态父进程已退出、宿主永驻"的场景同构。
# 注意：必须在主契约宿主关停**之后**跑——宿主是单实例进程（命名互斥体），并发启动会被直接拒绝。
$peOk = $false; $peDetail = 'not-run'
$pPE = 8899
  $sandPE = Join-Path $OutDir 'sandbox-ac14'; New-Item -ItemType Directory -Force -Path $sandPE | Out-Null
  $tfPE = Join-Path $sandPE 'ac14.session'; New-SimFanSessionToken $tfPE | Out-Null
  $dummy = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile', '-Command', 'Start-Sleep -Seconds 300') -PassThru -WindowStyle Hidden
  $prPE = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$pPE", '--protocol-version', '2', '--session-token-file', $tfPE, '--mock-handshake', '--parent-pid', "$($dummy.Id)") -PassThru -WindowStyle Hidden
  $rPE = Wait-SimHostReady -Port $pPE -TokenPath $tfPE -TimeoutSec 60
  # 先把探针宿主推进到"不可安全收尾"的故障态（与掌机上 FaultLocked 时父进程退出同构），
  # 否则走的是优雅收尾路径、证明不了"预算耗尽仍必须退出"。
  $tokPEv = Get-SimFanSessionToken $tfPE
  $basePE = "http://127.0.0.1:$pPE"
  $pePreState = 'not-reached'
  if ($rPE.ready -and $tokPEv) {
    $null = Send-SimFanApi -Method POST -Path '/api/open' -Base $basePE -Token $tokPEv -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $null = Send-SimFanApi -Method POST -Path '/api/open-events' -Base $basePE -Token $tokPEv -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $acqPE = Send-SimFanApi -Method POST -Path '/api/acquire-control' -Base $basePE -Token $tokPEv -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $leasePE = "$($acqPE.json.lease.leaseId)"
    $curvePE = '{"nodes":[{"tempC":0,"dutyPercent":0},{"tempC":55,"dutyPercent":30},{"tempC":75,"dutyPercent":60},{"tempC":90,"dutyPercent":100}],"leaseId":"' + $leasePE + '"}'
    $null = Send-SimFanApi -Method POST -Path '/api/enable' -Base $basePE -Token $tokPEv -Body $curvePE -TimeoutMs 8000 -IsChangeCall
    $sfPE = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $basePE -Token $tokPEv -Body ('{"generation":' + $gF + ',"source":"selftest.native"}') -TimeoutMs 8000 -IsChangeCall
    $pePreState = "$($sfPE.json.state.state)"
  }
  $tKill = Get-Date
  Stop-Process -Id $dummy.Id -Force -ErrorAction SilentlyContinue
  $exited = $false
  while (((Get-Date) - $tKill).TotalSeconds -lt 40) {
    Start-Sleep -Milliseconds 500
    if ($prPE.HasExited) { $exited = $true; break }
  }
  $peElapsed = ((Get-Date) - $tKill).TotalSeconds
  # 断言：故障态下父进程退出 ⇒ 宿主**必须退出**（mock 下交还可能优雅完成，故不要求跑满预算；
  # 预算是源码级保证：预算耗尽即退出，见 invariants 的静态锚点检查），且端口释放。
  $peOk = [bool]$rPE.ready -and ($pePreState -eq 'FaultLocked') -and $exited -and ($peElapsed -lt 40) -and (-not (Test-SimPort $pPE))
  $peDetail = ("ready={0} preKillState={1} parentKilled=pid{2} hostExited={3} elapsedSec={4:F1} portOpen={5}" -f $rPE.ready, $pePreState, $dummy.Id, $exited, $peElapsed, (Test-SimPort $pPE))
  if (-not $prPE.HasExited) { Stop-Process -Id $prPE.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 800
Check-C 'AC14' 'T0：父进程退出后宿主有界退出（预算耗尽即退，无死循环）且端口释放' $peOk $peDetail

# AC15（2026-09-23 真机 16:13 取证新增）：唤醒过渡期的**应答可判定性**。
# 真机事实：唤醒后宿主自建重建耗时 10.2 s，期间 /api/open 两次 409、native 三次恢复全为传输失败
# （status=0）、末次 500 ⇒ 调用方无法区分"宿主忙"与"宿主不在"。本条把契约写成机器判据：
# 过渡窗内每个探针的响应必须是**可判定**的 HTTP 状态（≥100），传输失败必须为 0 次；
# 且至少观测到一条带原因码的 409 或一条 2xx；负例对照=对关闭端口探针必须被判为不可判定。
$wkOk = $false; $wkDetail = 'not-run'
Start-Sleep -Seconds 1
$pWK = 8897
$sandWK = Join-Path $OutDir 'sandbox-ac15'; New-Item -ItemType Directory -Force -Path $sandWK | Out-Null
$tfWK = Join-Path $sandWK 'ac15.session'; New-SimFanSessionToken $tfWK | Out-Null
$prWK = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$pWK", '--protocol-version', '2', '--session-token-file', $tfWK, '--mock-handshake') -PassThru -WindowStyle Hidden
$rWK = Wait-SimHostReady -Port $pWK -TokenPath $tfWK -TimeoutSec 60
if ($rWK.ready) {
  $tokWK = Get-SimFanSessionToken $tfWK
  $baseWK = "http://127.0.0.1:$pWK"
  $null = Send-SimFanApi -Method POST -Path '/api/open' -Base $baseWK -Token $tokWK -Body '{}' -TimeoutMs 8000 -IsChangeCall
  $null = Send-SimFanApi -Method POST -Path '/api/open-events' -Base $baseWK -Token $tokWK -Body '{}' -TimeoutMs 8000 -IsChangeCall
  $null = Send-SimFanApi -Method POST -Path '/api/acquire-control' -Base $baseWK -Token $tokWK -Body '{}' -TimeoutMs 8000 -IsChangeCall
  $null = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $baseWK -Token $tokWK -Body '{"generation":1,"source":"selftest.native"}' -TimeoutMs 8000
  $probes = New-Object System.Collections.ArrayList
  $t0 = Get-Date
  for ($i = 0; $i -lt 12; $i++) {
    foreach ($pp in @(@('GET', '/api/state'), @('POST', '/api/resume'), @('POST', '/api/open'))) {
      $res = Send-SimFanApi -Method $pp[0] -Path $pp[1] -Base $baseWK -Token $tokWK -Body '{"generation":1,"source":"selftest.native"}' -TimeoutMs 650
      $null = $probes.Add([ordered]@{ tMs = [int](((Get-Date) - $t0).TotalMilliseconds); path = $pp[1]; status = [int]$res.status })
    }
    Start-Sleep -Milliseconds 200
  }
  # 负例对照：关闭端口上的探针**必须**被判为不可判定（证明上面的判据不是空转）。
  $ctl = Send-SimFanApi -Method GET -Path '/api/state' -Base 'http://127.0.0.1:8896' -Token $tokWK -Body '' -TimeoutMs 400
  $ctlOk = ([int]$ctl.status -lt 100)
  $undecidable = @($probes | Where-Object { $_.status -lt 100 }).Count
  $decidableReason = @($probes | Where-Object { ($_.status -eq 409) -or ($_.status -ge 200 -and $_.status -lt 300) }).Count
  $settleSec = ((Get-Date) - $t0).TotalSeconds
  $wkOk = ($undecidable -eq 0) -and ($decidableReason -ge 1) -and $ctlOk
  $wkDetail = ("probes={0} undecidable={1} decidable(409/2xx)={2} transitionSec={3:F1} first6Ms={4} negControl(closed-port undecidable)={5}" -f
    $probes.Count, $undecidable, $decidableReason, $settleSec, (($probes | Select-Object -First 6 | ForEach-Object { $_.tMs }) -join '/'), $ctlOk)
}
if ($null -ne $prWK -and -not $prWK.HasExited) {
  $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $baseWK -Token $tokWK -Body '{}' -TimeoutMs 5000
  Stop-Process -Id $prWK.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 800
Check-C 'AC15' '唤醒过渡窗内每个应答可判定（零传输失败），至少一条带原因码 409/2xx；负例对照非空转' $wkOk $wkDetail

# AC15b（2026-09-23 FAN-204 s31 补充裁决 C）：**10.2 s 慢重建**过渡窗 + 接受契约。
# 真机事实：唤醒后 HC 重建 ≈10.2 s（AC15a 的 3 s mock 覆盖不到）。做法：同一 lane 载荷以
# `--mock-handshake --mock-resume-rebuild-delay-ms 10200` 起动（注入只在 mock 开关同时存在时
# 生效；生产路径不可达），空会话入睡后唤醒——注入把这次空恢复当成"有曲线要重建"，
# 于是 10.2 s 重建窗口在后台真实走一遍，而**接受响应必须快速返回**。断言：
#   ① 接受快速（< 2 s）且形状正确：resumeAccepted=true / resumePhase=Resuming / generation /
#      retryAfterMs / attempt / 快照 state=Resuming、controlAccepting=false；
#   ② 过渡窗内每次探针都可判定（status ≥ 100，零传输失败），且 /api/state 的阶段可读；
#   ③ 窗内重复 /api/resume 必须**不启动第二个 owner**（200 + resumeAccepted=false + 阶段仍 Resuming）；
#   ④ 有界观察：窗口 ≥9.5 s（证明不是 3 s mock），且 ≤15 s 内阶段变为 Ready（phase=Ready、
#      controlAccepting=true）。
$srOk = $false; $srDetail = 'not-run'
$pSR = 8893
$sandSR = Join-Path $OutDir 'sandbox-ac15b'; New-Item -ItemType Directory -Force -Path $sandSR | Out-Null
$tfSR = Join-Path $sandSR 'ac15b.session'; New-SimFanSessionToken $tfSR | Out-Null
$prSR = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$pSR", '--protocol-version', '2', '--session-token-file', $tfSR, '--mock-handshake', '--mock-resume-rebuild-delay-ms', '10200') -PassThru -WindowStyle Hidden
$rSR = Wait-SimHostReady -Port $pSR -TokenPath $tfSR -TimeoutSec 60
if ($rSR.ready) {
  $tokSR = Get-SimFanSessionToken $tfSR
  $baseSR = "http://127.0.0.1:$pSR"
  $null = Send-SimFanApi -Method POST -Path '/api/open' -Base $baseSR -Token $tokSR -Body '{}' -TimeoutMs 8000 -IsChangeCall
  $null = Send-SimFanApi -Method POST -Path '/api/open-events' -Base $baseSR -Token $tokSR -Body '{}' -TimeoutMs 8000 -IsChangeCall
  $null = Send-SimFanApi -Method POST -Path '/api/acquire-control' -Base $baseSR -Token $tokSR -Body '{}' -TimeoutMs 8000 -IsChangeCall
  $null = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $baseSR -Token $tokSR -Body '{"generation":9,"source":"selftest.native"}' -TimeoutMs 8000 -IsChangeCall
  $t0SR = Get-Date
  $acc = Send-SimFanApi -Method POST -Path '/api/resume' -Base $baseSR -Token $tokSR -Body '{"generation":9,"source":"selftest.native"}' -TimeoutMs 650 -IsChangeCall
  $acceptMs = [int]((Get-Date) - $t0SR).TotalMilliseconds
  # ③ 窗内重复接受：不得启动第二个 owner（同代次去重 ⇒ 现场回报，阶段仍 Resuming）
  $dupSR = Send-SimFanApi -Method POST -Path '/api/resume' -Base $baseSR -Token $tokSR -Body '{"generation":9,"source":"selftest.native"}' -TimeoutMs 650 -IsChangeCall
  $probesSR = New-Object System.Collections.ArrayList
  $windowSec = -1.0
  $readySeen = $false
  $resumingProbes = 0
  for ($i = 0; $i -lt 200; $i++) {
    $el = ((Get-Date) - $t0SR).TotalSeconds
    if ($el -gt 16) { break }
    foreach ($pp in @(@('GET', '/api/state'), @('POST', '/api/open'))) {
      $res = Send-SimFanApi -Method $pp[0] -Path $pp[1] -Base $baseSR -Token $tokSR -Body '{"generation":9,"source":"selftest.native"}' -TimeoutMs 650
      $null = $probesSR.Add([ordered]@{ tMs = [int](((Get-Date) - $t0SR).TotalMilliseconds); path = $pp[1]; status = [int]$res.status; state = "$($res.json.state.state)" })
    }
    $stSR = Send-SimFanApi -Method GET -Path '/api/state' -Base $baseSR -Token $tokSR -TimeoutMs 650
    if ("$($stSR.json.state.state)" -eq 'Resuming') { $resumingProbes++ }
    if ("$($stSR.json.state.state)" -eq 'Ready') { $readySeen = $true; $windowSec = ((Get-Date) - $t0SR).TotalSeconds; break }
    Start-Sleep -Milliseconds 250
  }
  $undecidableSR = @($probesSR | Where-Object { $_.status -lt 100 }).Count
  $acceptOk = ($acc.status -eq 200) -and ($acc.json.resumeAccepted -eq $true) -and ("$($acc.json.resumePhase)" -eq 'Resuming') -and
              ($acc.json.retryAfterMs -gt 0) -and ($acc.json.attempt -ge 1) -and ("$($acc.json.state.state)" -eq 'Resuming') -and
              ($acc.json.state.controlAccepting -eq $false) -and ($acceptMs -lt 2000)
  $dupOk = ($dupSR.status -eq 200) -and ($dupSR.json.resumeAccepted -eq $false) -and ("$($dupSR.json.resumePhase)" -eq 'Resuming')
  $srOk = $acceptOk -and $dupOk -and ($undecidableSR -eq 0) -and ($resumingProbes -ge 3) -and $readySeen -and
          ($windowSec -ge 9.5) -and ($windowSec -le 15.5)
  $srDetail = ("acceptMs={0} accepted={1} phase={2} attempt={3} retryAfterMs={4} | dupAccepted={5} dupPhase={6} | probes={7} undecidable={8} resumingProbes={9} | windowSec={10:F1} ready={11}" -f
    $acceptMs, $acc.json.resumeAccepted, $acc.json.resumePhase, $acc.json.attempt, $acc.json.retryAfterMs,
    $dupSR.json.resumeAccepted, $dupSR.json.resumePhase, $probesSR.Count, $undecidableSR, $resumingProbes, $windowSec, $readySeen)
} else {
  $srDetail = ("slow-rebuild sandbox host not ready (exited={0})" -f $prSR.HasExited)
}
if ($null -ne $prSR -and -not $prSR.HasExited) {
  $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $baseSR -Token $tokSR -Body '{}' -TimeoutMs 5000
  Stop-Process -Id $prSR.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 800
Check-C 'AC15b' '10.2 s 慢重建窗：接受快速且可判定、窗内零传输失败、无第二 owner、≤15 s 到 Ready' $srOk $srDetail

# AC15c（2026-09-23 真机 20:26 取证 · FAN-204 Q1-b 结构性裁决 DC61E311…）：**引擎锁被 HC 打开
# 占用**时的可判定性。真机事实：唤醒重建那一次 HC `Open()` 在引擎唯一 `gate` 之内持续 **10.22 s**
# （紧随的 `OpenEvents` 仅 63 ms）；期间 `/api/state` 被排队 10.30 s / 6.96 s（客户端已离开 → 500）、
# 另一个 `/api/open` 排队 10.22 s 才拿到 409、接受响应靠"抢锁"才快。AC15b 的注入发生在取锁
# **之前**，故覆盖不到本形态；本用例用 `--mock-hc-open-hold-ms 10200` 在 `Open()` 的**锁内**
# 如实复现同一持有期（只在 `automaticResumeInProgress` 时生效 ⇒ 沙箱里普通 `/api/open` 不受影响，
# 与真机同形：被挡住的只有**唤醒重建那一次**），并配 `--mock-resume-rebuild-delay-ms 1` 让 mock
# 走与真机相同的**后台重建**路径（mock 下"空恢复"本来走同步完成分支，不调 Open）。三种模式：
#   · strict（**默认**，裁决 ⑦ 的验收夹具）：并发探针 = `/api/state` + `/health` + `/api/resume`
#     （外加真机 ④ 的 `/api/open`）——每个探针必须在预算内（<2 s）得到**可判定**的
#     200/202/409，**不允许 500 / 传输超时**；接受可判定且 <2 s；持锁期内不得出现 Ready
#     （不得伪造）；≥1 次 Resuming 见证过渡真值；≤15.5 s 进入 Ready。
#   · neg（负例：`--mock-no-published-snapshot` 禁用已发布快照 ⇒ 读路径退回阻塞读）：严格判据
#     **必须失败**（出现传输失败/500），且宿主仍收敛到 Ready ⇒ 证明本夹具由"已发布快照"这个
#     守卫承载、非空转（守卫去 ⇒ 缺陷复现）。
#   · observe（历史取证：修复前对旧二进制复核夹具灵敏度用；修复后默认不再是它）：断言
#     "复现成立 + 仍收敛 + 不撒谎"。
function Invoke-Ac15cFixture {
  # 裁决 ⑦ 的竞争夹具：真 gate 竞争（Open 持锁 ≥10 s）下并发读路径必须可判定。
  # 返回 [pscustomobject]@{ ok; name; detail }（Check-C 由调用方执行）。
  param([string]$Mode)
  $cOk = $false; $cName = "AC15c-$Mode"; $cDetail = 'not-run'
  $pC = 8894
  $sandC = Join-Path $OutDir 'sandbox-ac15c'; New-Item -ItemType Directory -Force -Path $sandC | Out-Null
  $tfC = Join-Path $sandC 'ac15c.session'; New-SimFanSessionToken $tfC | Out-Null
  $holdMsC = 10200
  $probeTimeoutC = 1800
  $launchC = @('--port', "$pC", '--protocol-version', '2', '--session-token-file', $tfC, '--mock-handshake', '--mock-hc-open-hold-ms', "$holdMsC", '--mock-resume-rebuild-delay-ms', '1')
  if ($Mode -eq 'neg') { $launchC += '--mock-no-published-snapshot' }
  $prC = Start-Process -FilePath $D.hostExe -ArgumentList $launchC -PassThru -WindowStyle Hidden
  $rC = Wait-SimHostReady -Port $pC -TokenPath $tfC -TimeoutSec 60
  if ($rC.ready) {
    $tokC = Get-SimFanSessionToken $tfC
    $baseC = "http://127.0.0.1:$pC"
    $null = Send-SimFanApi -Method POST -Path '/api/open' -Base $baseC -Token $tokC -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $null = Send-SimFanApi -Method POST -Path '/api/open-events' -Base $baseC -Token $tokC -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $null = Send-SimFanApi -Method POST -Path '/api/acquire-control' -Base $baseC -Token $tokC -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $null = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $baseC -Token $tokC -Body '{"generation":11,"source":"selftest.native"}' -TimeoutMs 8000 -IsChangeCall
    $t0C = Get-Date
    $accC = Send-SimFanApi -Method POST -Path '/api/resume' -Base $baseC -Token $tokC -Body '{"generation":11,"source":"selftest.native"}' -TimeoutMs 650 -IsChangeCall
    $accMsC = [int]((Get-Date) - $t0C).TotalMilliseconds
    $probesC = New-Object System.Collections.ArrayList
    $readySeenC = $false; $windowSecC = -1.0; $firstFastMsC = -1; $resumingProbesC = 0; $readyDuringHoldC = 0; $open409C = 0
    $probeSetC = @(@('GET', '/api/state'), @('GET', '/health'), @('POST', '/api/resume'), @('POST', '/api/open'))
    for ($i = 0; $i -lt 200; $i++) {
      $el = ((Get-Date) - $t0C).TotalSeconds
      if ($el -gt 16) { break }
      $stateNow = ''
      foreach ($pp in $probeSetC) {
        $ts0 = Get-Date
        $res = Send-SimFanApi -Method $pp[0] -Path $pp[1] -Base $baseC -Token $tokC -Body '{"generation":11,"source":"selftest.native"}' -TimeoutMs $probeTimeoutC -IsChangeCall:($pp[0] -eq 'POST')
        $durMs = [int]((Get-Date) - $ts0).TotalMilliseconds
        $st = [int]$res.status
        $stName = "$($res.json.state.state)"
        $null = $probesC.Add([ordered]@{ tMs = [int](((Get-Date) - $t0C).TotalMilliseconds); path = $pp[1]; status = $st; code = "$($res.errorCode)"; state = $stName; durMs = $durMs })
        if ($st -ge 100 -and $durMs -lt 2000 -and $firstFastMsC -lt 0) { $firstFastMsC = [int](((Get-Date) - $t0C).TotalMilliseconds) }
        if ($pp[1] -eq '/api/state') { $stateNow = $stName }
        if ($pp[1] -eq '/api/open' -and $st -eq 409 -and "$($res.errorCode)" -eq 'POWER_RESUMING') { $open409C++ }
        # "持锁期不得报 Ready" 必须按**应答落地时刻**判（负例里探针会被阻塞，若用迭代起点会把
        # "10 s 后才返回的 Ready" 误计成"持锁期内的 Ready"）。
        if ($stName -eq 'Ready' -and (((Get-Date) - $t0C).TotalSeconds -lt 9.0)) { $readyDuringHoldC++ }
      }
      if ($stateNow -eq 'Resuming') { $resumingProbesC++ }
      if ($stateNow -eq 'Ready') { $readySeenC = $true; $windowSecC = ((Get-Date) - $t0C).TotalSeconds; break }
      Start-Sleep -Milliseconds 200
    }
    $undC = @($probesC | Where-Object { $_.status -lt 100 }).Count
    $c500C = @($probesC | Where-Object { $_.status -eq 500 }).Count
    $badC = @($probesC | Where-Object { $_.status -ge 100 -and (@(200, 202, 409) -notcontains $_.status) }).Count
    $maxProbeMsC = (@($probesC | ForEach-Object { $_.durMs }) + @(0) | Measure-Object -Maximum).Maximum
    $accDecidableC = (($accC.status -eq 200) -and ($accC.json.resumeAccepted -eq $true))
    $accFastC = ($accDecidableC -and ($accMsC -lt 2000))
    $allDecidableC = ($undC -eq 0) -and ($c500C -eq 0) -and ($badC -eq 0) -and ($maxProbeMsC -lt 2000)
    $noLieC = ($readyDuringHoldC -eq 0) -and ($resumingProbesC -ge 1)
    $convergesC = $readySeenC -and ($windowSecC -ge 9.0) -and ($windowSecC -le 15.5)
    $reproducesC = ($undC -ge 1) -or ($c500C -ge 1) -or (-not $accDecidableC) -or ($maxProbeMsC -ge 2000)
    if ($Mode -eq 'neg') {
      # 负例：守卫（已发布快照）被禁用 ⇒ 严格判据**必须失败**；宿主仍必须收敛（失败只可归因于读路径）。
      $cOk = $convergesC -and (-not $allDecidableC) -and $reproducesC
      $cName = 'AC15c-neg（负例：--mock-no-published-snapshot 禁用已发布快照 ⇒ 严格判据必须失败且宿主仍收敛，证明夹具非空转）'
    } elseif ($Mode -eq 'observe') {
      $cOk = $reproducesC -and $convergesC -and $noLieC
      $cName = 'AC15c-observe（历史取证：修复前复现成立 + 仍收敛 + 不撒谎；默认已改为 strict）'
    } else {
      $cOk = $accFastC -and $allDecidableC -and $convergesC -and $noLieC
      $cName = 'AC15c-strict（裁决 ⑦：持锁 10.2 s 期间 /api/state+/health+/api/resume+/api/open 全部预算内可判定、零 500/零传输失败、≤15 s Ready、不伪造）'
    }
    $cDetail = ("mode={0} acceptMs={1} acceptDecidable={2} | probes={3} undecidable={4} c500={5} badStatus={6} maxProbeMs={7} | firstFastMs={8} resumingProbes={9} readyDuringHold={10} open409={11} | windowSec={12:F1} ready={13} | allDecidable={14} repro={15}" -f
      $Mode, $accMsC, $accDecidableC, $probesC.Count, $undC, $c500C, $badC, $maxProbeMsC, $firstFastMsC, $resumingProbesC, $readyDuringHoldC, $open409C, $windowSecC, $readySeenC, $allDecidableC, $reproducesC)
    # 证据：每个模式各自一份 JSON（便于人工复核竞争窗口的逐探针明细）。
    $jObj = [ordered]@{ mode = $Mode; ok = $cOk; name = $cName; detail = $cDetail; acceptMs = $accMsC; acceptDecidable = $accDecidableC; probes = @($probesC); undecidable = $undC; c500 = $c500C; badStatus = $badC; maxProbeMs = $maxProbeMsC; windowSec = $windowSecC; ready = $readySeenC; reproduces = $reproducesC; noLie = $noLieC; allDecidable = $allDecidableC }
    [IO.File]::WriteAllText((Join-Path $OutDir ("ac15c-{0}.json" -f $Mode)), ($jObj | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
    if ($env:YMCC_AC15C_JSON) { [IO.File]::WriteAllText($env:YMCC_AC15C_JSON, ($jObj | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false))) }
  } else {
    $cDetail = ("gate-hold sandbox host not ready (exited={0})" -f $prC.HasExited)
  }
  if ($null -ne $prC -and -not $prC.HasExited) {
    $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $baseC -Token $tokC -Body '{}' -TimeoutMs 5000
    Stop-Process -Id $prC.Id -Force -ErrorAction SilentlyContinue
  }
  Start-Sleep -Milliseconds 800
  return [pscustomobject]@{ ok = $cOk; name = $cName; detail = $cDetail }
}

# 用例 1 = 修复验收（strict；`YMCC_AC15C_MODE=observe` 只用于对**旧二进制**复核夹具灵敏度——只能取值为
# observe/strict，取其它值一律按 strict）；
# 用例 2 = 负例（禁用已发布快照 ⇒ 严格判据必须失败），与用例 1 同批运行，保证"判定由已发布快照这个
# 守卫承载"这件事每次总门都有证据，而不是只写在报告里。两例固定 ⇒ 用例数恒为 19，不随环境变量漂移。
$ac15cMode = if ($env:YMCC_AC15C_MODE -and "$env:YMCC_AC15C_MODE".ToLowerInvariant() -eq 'observe') { 'observe' } else { 'strict' }
$fx1 = Invoke-Ac15cFixture -Mode $ac15cMode
Check-C 'AC15c' $fx1.name $fx1.ok $fx1.detail
$fx2 = Invoke-Ac15cFixture -Mode 'neg'
Check-C 'AC15c-neg' $fx2.name $fx2.ok $fx2.detail

# AC15d（2026-09-24 FAN-204 **Q1-c / HB-RESUME-BLOCKED** 裁决 A）：**heartbeat 取锁前快速判定**。
# 真机原始行（01:09:51）：`POST /api/heartbeat` 在 HC `Open()` 持 gate 的 ≈10.2 s 内排队
# `10009.35 ms` 才返回 409 ⇒ 表现为"唤醒后风扇暂时不可控"。本夹具与 AC15c 同形（锁内持有时长注入
# `--mock-hc-open-hold-ms`），但探针换成**控制写路径** `/api/heartbeat`，断言：
#   ① 重建窗内每次 heartbeat 都在预算内（<2 s）拿到**可判定 409**，零传输失败、零非 409；
#   ② 结构化字段齐全：code=POWER_RESUMING、details.controlAccepting=false、details.retryAfterMs>0、
#      details.generation>=0、details.resumePhase=Resuming、details.gateWaited=false、
#      details.hcCalled=false、details.waitedForGateMs<50（真的没等门）；
#   ③ **不延长旧 lease**：details.leaseRenewed=false 且 details.leaseExtensionMs=0；
#   ④ ≤15.5 s 收敛到 Ready、持锁期不报 Ready、窗内至少 3 次 Resuming 心跳（窗口真的被覆盖）。
# 两个负例（必须让上面某条断言失败，证明夹具非空转）：
#   legacy = `--mock-heartbeat-legacy-gate`（退回"直接进 gate"）⇒ ①②③ 失败（排队 ≈10 s、无结构化字段）；
#   renew  = `--mock-heartbeat-renew-lease`（快速拒绝路径**如实上报**"错误延长了旧 lease"这一契约变异）
#            ⇒ ③ 失败。口径：该开关只变异**上报值**，不改引擎状态——真做状态级误延长会与恢复 worker 的
#            AcquireControl 冲突并卡死宿主（另一种更危险的缺陷形态），不属本夹具断言范围。
function Invoke-Ac15dFixture {
  param([string]$Mode)
  $dOk = $false; $dName = "AC15d-$Mode"; $dDetail = 'not-run'
  # 三种模式**各用独立端口**：同一个端口连续三次起停会撞上上一轮的 LISTEN/TIME_WAIT，
  # 使后一轮的宿主绑定失败并立刻退出（2026-09-24 实测 AC15d-legacy 因此报 "host not ready"）。
  $pD = switch ($Mode) { 'legacy' { 8891 } 'renew' { 8892 } default { 8897 } }
  $tWait = Get-Date
  while ((Test-SimPort $pD) -and ((Get-Date) - $tWait).TotalSeconds -lt 20) { Start-Sleep -Milliseconds 400 }
  $sandD = Join-Path $OutDir 'sandbox-ac15d'; New-Item -ItemType Directory -Force -Path $sandD | Out-Null
  $tfD = Join-Path $sandD ("ac15d-{0}.session" -f $Mode); New-SimFanSessionToken $tfD | Out-Null
  $holdMsD = 10200
  $hbTimeoutD = 1800
  $launchD = @('--port', "$pD", '--protocol-version', '2', '--session-token-file', $tfD, '--mock-handshake', '--mock-hc-open-hold-ms', "$holdMsD", '--mock-resume-rebuild-delay-ms', '1')
  if ($Mode -eq 'legacy') { $launchD += '--mock-heartbeat-legacy-gate' }
  if ($Mode -eq 'renew') { $launchD += '--mock-heartbeat-renew-lease' }
  $prD = Start-Process -FilePath $D.hostExe -ArgumentList $launchD -PassThru -WindowStyle Hidden
  $rD = Wait-SimHostReady -Port $pD -TokenPath $tfD -TimeoutSec 60
  if ($rD.ready) {
    $tokD = Get-SimFanSessionToken $tfD
    $baseD = "http://127.0.0.1:$pD"
    $null = Send-SimFanApi -Method POST -Path '/api/open' -Base $baseD -Token $tokD -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $null = Send-SimFanApi -Method POST -Path '/api/open-events' -Base $baseD -Token $tokD -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $acqD = Send-SimFanApi -Method POST -Path '/api/acquire-control' -Base $baseD -Token $tokD -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $leaseD = "$($acqD.json.lease.leaseId)"
    $null = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $baseD -Token $tokD -Body '{"generation":12,"source":"selftest.native"}' -TimeoutMs 8000 -IsChangeCall
    $t0D = Get-Date
    $accD = Send-SimFanApi -Method POST -Path '/api/resume' -Base $baseD -Token $tokD -Body '{"generation":12,"source":"selftest.native"}' -TimeoutMs 650 -IsChangeCall
    $accMsD = [int]((Get-Date) - $t0D).TotalMilliseconds
    $hbsD = New-Object System.Collections.ArrayList
    $readySeenD = $false; $windowSecD = -1.0; $readyDuringHoldD = 0; $resumingHbD = 0; $maxHbMsD = 0
    for ($i = 0; $i -lt 90; $i++) {
      if (((Get-Date) - $t0D).TotalSeconds -gt 16) { break }
      # 收敛用**非阻塞读路径**判（Q1-b 已保证不被门挡住）——负例里 heartbeat 自己会被阻塞，
      # 若拿它当收敛判据，负例就永远看不到 Ready，会把"宿主收敛"误判成失败。
      $st = Send-SimFanApi -Method GET -Path '/api/state' -Base $baseD -Token $tokD -TimeoutMs 1800
      $stNameD = "$($st.json.state.state)"
      $tsD = Get-Date
      $hb = Send-SimFanApi -Method POST -Path '/api/heartbeat' -Base $baseD -Token $tokD -Body ('{"leaseId":"' + $leaseD + '"}') -TimeoutMs $hbTimeoutD -IsChangeCall
      $durD = [int]((Get-Date) - $tsD).TotalMilliseconds
      if ($durD -gt $maxHbMsD) { $maxHbMsD = $durD }
      $det = $hb.json.error.details
      $null = $hbsD.Add([ordered]@{
          tMs = [int](((Get-Date) - $t0D).TotalMilliseconds); state = $stNameD
          status = [int]$hb.status; code = "$($hb.errorCode)"; durMs = $durD
          hbState = "$($hb.json.state.state)"
          hasDetails = ($null -ne $det)
          gateWaited = $(if ($det) { $det.gateWaited } else { $null })
          waitedForGateMs = $(if ($det) { $det.waitedForGateMs } else { $null })
          hcCalled = $(if ($det) { $det.hcCalled } else { $null })
          controlAccepting = $(if ($det) { $det.controlAccepting } else { $null })
          retryAfterMs = $(if ($det) { $det.retryAfterMs } else { $null })
          resumePhase = $(if ($det) { "$($det.resumePhase)" } else { $null })
          generation = $(if ($det) { $det.generation } else { $null })
          leaseRenewed = $(if ($det) { $det.leaseRenewed } else { $null })
          leaseExtensionMs = $(if ($det) { $det.leaseExtensionMs } else { $null })
        })
      if ($stNameD -eq 'Resuming') { $resumingHbD++ }
      if ($stNameD -eq 'Ready' -and (((Get-Date) - $t0D).TotalSeconds -lt 9.0)) { $readyDuringHoldD++ }
      if ($stNameD -eq 'Ready') { $readySeenD = $true; $windowSecD = ((Get-Date) - $t0D).TotalSeconds; break }
      Start-Sleep -Milliseconds 150
    }
    $inWinD = @($hbsD | Where-Object { $_.state -eq 'Resuming' })
    # 到达 Ready 之后，用**同一个陈旧令牌**再打一次 heartbeat：必须仍被拒（LEASE_INVALID）——
    # 这是"快速拒绝路径没有替陈旧令牌铸出/延长 lease、也没有创建第二 owner"的可观测证据。
    $staleAfterReady = $null
    if ($readySeenD) {
      $staleAfterReady = Send-SimFanApi -Method POST -Path '/api/heartbeat' -Base $baseD -Token $tokD -Body ('{"leaseId":"' + $leaseD + '"}') -TimeoutMs 3000 -IsChangeCall
    }
    $staleAfterReadyOk = ($null -ne $staleAfterReady) -and ($staleAfterReady.status -eq 409) -and ("$($staleAfterReady.errorCode)" -eq 'LEASE_INVALID')
    $undD = @($hbsD | Where-Object { $_.status -lt 100 }).Count
    $badStD = @($hbsD | Where-Object { $_.status -ge 100 -and $_.status -ne 409 }).Count
    $slowD = @($inWinD | Where-Object { $_.durMs -ge 2000 }).Count
    $structD = @($inWinD | Where-Object {
        $_.hasDetails -and "$($_.code)" -eq 'POWER_RESUMING' -and $_.gateWaited -eq $false -and $_.hcCalled -eq $false -and
        $_.controlAccepting -eq $false -and "$($_.resumePhase)" -eq 'Resuming' -and [int]$_.retryAfterMs -gt 0 -and
        [int]$_.generation -ge 0 -and [int]$_.waitedForGateMs -lt 50 -and "$($_.hbState)" -eq 'Resuming'
      }).Count
    $noRenewD = @($inWinD | Where-Object { $_.leaseRenewed -eq $false -and [int]$_.leaseExtensionMs -eq 0 }).Count
    $accFastD = ($accD.status -eq 200) -and ($accD.json.resumeAccepted -eq $true) -and ($accMsD -lt 2000)
    $accDecidableD = ($accD.status -eq 200) -and ($accD.json.resumeAccepted -eq $true)
    $convergesD = $readySeenD -and ($windowSecD -ge 9.0) -and ($windowSecD -le 15.5)
    $noLieD = ($readyDuringHoldD -eq 0) -and ($resumingHbD -ge 3)
    $fastOkD = ($undD -eq 0) -and ($badStD -eq 0) -and ($slowD -eq 0) -and ($inWinD.Count -ge 3) -and ($structD -eq $inWinD.Count)
    $notRenewedOkD = ($inWinD.Count -ge 3) -and ($noRenewD -eq $inWinD.Count) -and $staleAfterReadyOk
    if ($Mode -eq 'legacy') {
      $dOk = $convergesD -and (-not $fastOkD) -and (-not $notRenewedOkD)
      $dName = 'AC15d-legacy（负例：heartbeat 退回直接进 gate ⇒ 快速判定/结构化字段断言必须失败，宿主仍收敛）'
    } elseif ($Mode -eq 'renew') {
      $dOk = $convergesD -and $fastOkD -and (-not $notRenewedOkD)
      $dName = 'AC15d-renew（负例：快速拒绝路径如实上报"错误延长旧 lease"⇒ "不续租"断言必须失败，其余仍成立）'
    } else {
      $dOk = $accFastD -and $fastOkD -and $notRenewedOkD -and $convergesD -and $noLieD
      $dName = 'AC15d-strict（Q1-c：重建窗内 heartbeat 全部预算内可判定 409 + 结构化字段齐全 + 不续租/不铸新租 + ≤15.5 s Ready + 不撒谎）'
    }
    $dDetail = ("mode={0} acceptMs={1} acceptDecidable={2} | heartbeats={3} inWindow={4} undecidable={5} badStatus={6} slow={7} maxHbMs={8} | structOk={9} notRenewed={10} staleAfterReady={11} resumingHb={12} readyDuringHold={13} | windowSec={14:F1} ready={15} | fastOk={16} converges={17}" -f
      $Mode, $accMsD, $accDecidableD, $hbsD.Count, $inWinD.Count, $undD, $badStD, $slowD, $maxHbMsD, $structD, $noRenewD, $staleAfterReadyOk, $resumingHbD, $readyDuringHoldD, $windowSecD, $readySeenD, $fastOkD, $convergesD)
    $jObjD = [ordered]@{ mode = $Mode; ok = $dOk; name = $dName; detail = $dDetail; acceptMs = $accMsD; heartbeats = @($hbsD); inWindow = $inWinD.Count; undecidable = $undD; badStatus = $badStD; slow = $slowD; maxHbMs = $maxHbMsD; structOk = $structD; notRenewed = $noRenewD; staleAfterReadyOk = $staleAfterReadyOk; staleAfterReady = $staleAfterReady; windowSec = $windowSecD; ready = $readySeenD; fastOk = $fastOkD; converges = $convergesD }
    [IO.File]::WriteAllText((Join-Path $OutDir ("ac15d-{0}.json" -f $Mode)), ($jObjD | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
    if ($env:YMCC_AC15D_JSON) { [IO.File]::WriteAllText($env:YMCC_AC15D_JSON, ($jObjD | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false))) }
  } else {
    $dDetail = ("heartbeat-race sandbox host not ready (exited={0})" -f $prD.HasExited)
  }
  if ($null -ne $prD -and -not $prD.HasExited) {
    $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $baseD -Token $tokD -Body '{}' -TimeoutMs 5000
    Stop-Process -Id $prD.Id -Force -ErrorAction SilentlyContinue
  }
  Start-Sleep -Milliseconds 800
  return [pscustomobject]@{ ok = $dOk; name = $dName; detail = $dDetail }
}

# 三条固定用例：正例（strict）+ 两个负例（legacy / renew）——每次总门都同时证明
# "判定由取锁前快速路径承载"且"不续租"这两个断言都不是空转。
$fx3 = Invoke-Ac15dFixture -Mode 'strict'
Check-C 'AC15d' $fx3.name $fx3.ok $fx3.detail
$fx4 = Invoke-Ac15dFixture -Mode 'legacy'
Check-C 'AC15d-legacy' $fx4.name $fx4.ok $fx4.detail
$fx5 = Invoke-Ac15dFixture -Mode 'renew'
Check-C 'AC15d-renew' $fx5.name $fx5.ok $fx5.detail

# AC15e（2026-09-24 FAN-204 §31 **R4 阶段 2**）：**用户控制端点统一快速拒绝**（open / open-events /
# acquire-control / enable）。与 AC15d 同形（`--mock-hc-open-hold-ms` 复现 HC 打开就绪持锁窗 ≈10.2 s），
# 把探针从 heartbeat 换成四个用户控制端点，断言：
#   ① 重建窗内每次调用都在预算内（< 2 s）拿到**可判定 409 POWER_RESUMING**；零传输失败、零非 409；
#   ② 结构化字段齐全：details.operation 与端点对应、gateWaited=false、hcCalled=false、
#      controlAccepting=false、retryAfterMs>0、generation>=0、resumePhase=Resuming、
#      leaseRenewed=false、leaseExtensionMs=0；
#   ③ 四个端点都被覆盖（endpoints=4）且宿主仍收敛（≤15.5 s Ready、持锁期不报 Ready）。
# 负例（必须让 ①② 失败，证明统一判定非空转）：`--mock-user-control-legacy-gate`（退回"直接进 gate"，
# 排队 ≈10 s、无结构化字段）。**安全清理端点（disable/restore/release-control）不在本夹具范围**：
# 源码合同要求它们始终可达，不得为"统一"而阻断 OEM 交还（审计表见 HC-FAN-LIFECYCLE §17.5）。
function Invoke-Ac15eFixture {
  param([string]$Mode)
  $eOk = $false; $eName = "AC15e-$Mode"; $eDetail = 'not-run'
  $pE = if ($Mode -eq 'legacy') { 8902 } else { 8901 }
  $tWaitE = Get-Date
  while ((Test-SimPort $pE) -and ((Get-Date) - $tWaitE).TotalSeconds -lt 20) { Start-Sleep -Milliseconds 400 }
  $sandE = Join-Path $OutDir 'sandbox-ac15e'; New-Item -ItemType Directory -Force -Path $sandE | Out-Null
  $tfE = Join-Path $sandE ("ac15e-{0}.session" -f $Mode); New-SimFanSessionToken $tfE | Out-Null
  $holdMsE = 10200
  $callTimeoutE = 1800
  $launchE = @('--port', "$pE", '--protocol-version', '2', '--session-token-file', $tfE, '--mock-handshake', '--mock-hc-open-hold-ms', "$holdMsE", '--mock-resume-rebuild-delay-ms', '1')
  if ($Mode -eq 'legacy') { $launchE += '--mock-user-control-legacy-gate' }
  $prE = Start-Process -FilePath $D.hostExe -ArgumentList $launchE -PassThru -WindowStyle Hidden
  $rE = Wait-SimHostReady -Port $pE -TokenPath $tfE -TimeoutSec 60
  if ($rE.ready) {
    $tokE = Get-SimFanSessionToken $tfE
    $baseE = "http://127.0.0.1:$pE"
    $null = Send-SimFanApi -Method POST -Path '/api/open' -Base $baseE -Token $tokE -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $null = Send-SimFanApi -Method POST -Path '/api/open-events' -Base $baseE -Token $tokE -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $acqE = Send-SimFanApi -Method POST -Path '/api/acquire-control' -Base $baseE -Token $tokE -Body '{}' -TimeoutMs 8000 -IsChangeCall
    $leaseE = "$($acqE.json.lease.leaseId)"
    $curveE = '{"nodes":[{"tempC":0,"dutyPercent":0},{"tempC":55,"dutyPercent":30},{"tempC":75,"dutyPercent":60},{"tempC":90,"dutyPercent":100}],"leaseId":"' + $leaseE + '"}'
    $null = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $baseE -Token $tokE -Body '{"generation":13,"source":"selftest.native"}' -TimeoutMs 8000 -IsChangeCall
    $t0E = Get-Date
    $accE = Send-SimFanApi -Method POST -Path '/api/resume' -Base $baseE -Token $tokE -Body '{"generation":13,"source":"selftest.native"}' -TimeoutMs 650 -IsChangeCall
    $accMsE = [int]((Get-Date) - $t0E).TotalMilliseconds
    $probesE = New-Object System.Collections.ArrayList
    $readySeenE = $false; $windowSecE = -1.0; $readyDuringHoldE = 0
    for ($i = 0; $i -lt 60; $i++) {
      if (((Get-Date) - $t0E).TotalSeconds -gt 16) { break }
      # 收敛判据用非阻塞读路径（与 AC15d 同理：负例里控制端点自己会被阻塞，不能拿它当收敛判据）。
      $stE = Send-SimFanApi -Method GET -Path '/api/state' -Base $baseE -Token $tokE -TimeoutMs 1800
      $stNameE = "$($stE.json.state.state)"
      foreach ($ep in @(
          @{ path = '/api/open'; body = '{}' },
          @{ path = '/api/open-events'; body = '{}' },
          @{ path = '/api/acquire-control'; body = '{}' },
          @{ path = '/api/enable'; body = $curveE })) {
        $tsE = Get-Date
        $resE = Send-SimFanApi -Method POST -Path $ep.path -Base $baseE -Token $tokE -Body $ep.body -TimeoutMs $callTimeoutE -IsChangeCall
        $durE = [int]((Get-Date) - $tsE).TotalMilliseconds
        $detE = $resE.json.error.details
        $null = $probesE.Add([ordered]@{
            tMs = [int](((Get-Date) - $t0E).TotalMilliseconds); state = $stNameE; path = $ep.path
            status = [int]$resE.status; code = "$($resE.errorCode)"; durMs = $durE
            hasDetails = ($null -ne $detE)
            operation = $(if ($detE) { "$($detE.operation)" } else { $null })
            gateWaited = $(if ($detE) { $detE.gateWaited } else { $null })
            hcCalled = $(if ($detE) { $detE.hcCalled } else { $null })
            controlAccepting = $(if ($detE) { $detE.controlAccepting } else { $null })
            retryAfterMs = $(if ($detE) { $detE.retryAfterMs } else { $null })
            resumePhase = $(if ($detE) { "$($detE.resumePhase)" } else { $null })
            generation = $(if ($detE) { $detE.generation } else { $null })
            leaseRenewed = $(if ($detE) { $detE.leaseRenewed } else { $null })
            leaseExtensionMs = $(if ($detE) { $detE.leaseExtensionMs } else { $null })
          })
      }
      if ($stNameE -eq 'Ready' -and (((Get-Date) - $t0E).TotalSeconds -lt 9.0)) { $readyDuringHoldE++ }
      if ($stNameE -eq 'Ready') { $readySeenE = $true; $windowSecE = ((Get-Date) - $t0E).TotalSeconds; break }
      Start-Sleep -Milliseconds 300
    }
    $inWinE = @($probesE | Where-Object { $_.state -eq 'Resuming' })
    $expectedOpE = @{ '/api/open' = 'open'; '/api/open-events' = 'open-events'; '/api/acquire-control' = 'acquire-control'; '/api/enable' = 'enable' }
    $undE = @($inWinE | Where-Object { $_.status -lt 100 }).Count
    $badStE = @($inWinE | Where-Object { $_.status -ge 100 -and $_.status -ne 409 }).Count
    $slowE = @($inWinE | Where-Object { $_.durMs -ge 2000 }).Count
    $structE = @($inWinE | Where-Object {
        $_.hasDetails -and "$($_.code)" -eq 'POWER_RESUMING' -and $_.gateWaited -eq $false -and $_.hcCalled -eq $false -and
        $_.controlAccepting -eq $false -and "$($_.resumePhase)" -eq 'Resuming' -and [int]$_.retryAfterMs -gt 0 -and
        [int]$_.generation -ge 0 -and $_.leaseRenewed -eq $false -and [int]$_.leaseExtensionMs -eq 0 -and
        "$($_.operation)" -eq "$($expectedOpE[$_.path])"
      }).Count
    # 注意：$inWinE 元素是 [ordered] 哈希表 ⇒ 只能用 $_['path'] 取值；
    # `Select-Object -ExpandProperty path` 对哈希表**取不到键**（本夹具第一跑因此得到 endpoints=0）。
    $epsE = @($inWinE | ForEach-Object { $_['path'] } | Select-Object -Unique).Count
    $convergesE = $readySeenE -and ($windowSecE -ge 9.0) -and ($windowSecE -le 15.5)
    $noLieE = ($readyDuringHoldE -eq 0)
    $fastOkE = ($undE -eq 0) -and ($badStE -eq 0) -and ($slowE -eq 0) -and ($inWinE.Count -ge 8) -and ($structE -eq $inWinE.Count) -and ($epsE -eq 4)
    if ($Mode -eq 'legacy') {
      $eOk = $convergesE -and (-not $fastOkE)
      $eName = 'AC15e-legacy（负例：四个控制端点退回直接进 gate ⇒ 快速判定/结构化断言必须失败，宿主仍收敛）'
    } else {
      $eOk = $fastOkE -and $convergesE -and $noLieE
      $eName = 'AC15e-strict（阶段 2：重建窗内 open/open-events/acquire-control/enable 全部预算内可判定 409 + 结构化字段齐全 + ≤15.5 s 收敛）'
    }
    $eDetail = ("mode={0} acceptMs={1} | probes={2} inWindow={3} endpoints={4} undecidable={5} badStatus={6} slow={7} | structOk={8} | windowSec={9:F1} ready={10} readyDuringHold={11} | fastOk={12} converges={13}" -f
      $Mode, $accMsE, $probesE.Count, $inWinE.Count, $epsE, $undE, $badStE, $slowE, $structE, $windowSecE, $readySeenE, $readyDuringHoldE, $fastOkE, $convergesE)
    $jObjE = [ordered]@{ mode = $Mode; ok = $eOk; name = $eName; detail = $eDetail; acceptMs = $accMsE; probes = @($probesE); inWindow = $inWinE.Count; endpoints = $epsE; undecidable = $undE; badStatus = $badStE; slow = $slowE; structOk = $structE; windowSec = $windowSecE; ready = $readySeenE; readyDuringHold = $readyDuringHoldE; fastOk = $fastOkE; converges = $convergesE }
    [IO.File]::WriteAllText((Join-Path $OutDir ("ac15e-{0}.json" -f $Mode)), ($jObjE | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
    if ($env:YMCC_AC15E_JSON) { [IO.File]::WriteAllText($env:YMCC_AC15E_JSON, ($jObjE | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false))) }
  } else {
    $eDetail = ("user-control fast-reject sandbox host not ready (exited={0})" -f $prE.HasExited)
  }
  if ($null -ne $prE -and -not $prE.HasExited) {
    $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $baseE -Token $tokE -Body '{}' -TimeoutMs 5000
    Stop-Process -Id $prE.Id -Force -ErrorAction SilentlyContinue
  }
  Start-Sleep -Milliseconds 800
  return [pscustomobject]@{ ok = $eOk; name = $eName; detail = $eDetail }
}

$fx6 = Invoke-Ac15eFixture -Mode 'strict'
Check-C 'AC15e' $fx6.name $fx6.ok $fx6.detail
$fx7 = Invoke-Ac15eFixture -Mode 'legacy'
Check-C 'AC15e-legacy' $fx7.name $fx7.ok $fx7.detail

# AC16（2026-09-23，operator 口径"不操作时应≈0%"）：宿主**空闲 CPU 地板**。
# 口径换算：任务管理器 %（全机，16 逻辑处理器）= %total × 16 → 单核 ms/s。真机实测：
# 空闲期 cpuMsPerSec 1–5 ms/s（≈0.006–0.03% 全机）、活动期 13–56 ms/s（≈0.08–0.35% 全机）。
# 本条断言：① 纯空闲 60 s 的地板必须低于阈值（idleMsPerSec < 20，即 < 2% 单核）；
# ② 阳性对照：改变类调用期间 CPU 必须**明显高于**空闲地板（证明该指标非空转）。
$idleOk = $false; $idleDetail = 'not-run'
# 注意：**不得**用 $pID —— PowerShell 变量名大小写不敏感，$pID 与只读的 $PID（当前进程号）同名，
# 赋值会被拒（非终止错误）后沿用进程号当端口，属于"靠巧合工作"，此处改用无歧义名。
$ac16Port = 8895
$sandID = Join-Path $OutDir 'sandbox-ac16'; New-Item -ItemType Directory -Force -Path $sandID | Out-Null
$tfID = Join-Path $sandID 'ac16.session'; New-SimFanSessionToken $tfID | Out-Null
$prID = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$ac16Port", '--protocol-version', '2', '--session-token-file', $tfID, '--mock-handshake') -PassThru -WindowStyle Hidden
$rID = Wait-SimHostReady -Port $ac16Port -TokenPath $tfID -TimeoutSec 60
if ($rID.ready) {
  $tokID = Get-SimFanSessionToken $tfID
  $baseID = "http://127.0.0.1:$ac16Port"
  # ① 空闲地板：不做任何调用，静置 60 s
  $c0 = $prID.TotalProcessorTime
  $swIdle = [Diagnostics.Stopwatch]::StartNew()
  Start-Sleep -Seconds 60
  $swIdle.Stop()
  $prID.Refresh()
  $idleMs = (($prID.TotalProcessorTime - $c0).TotalMilliseconds) / $swIdle.Elapsed.TotalSeconds
  # ② 阳性对照：enable→restore（改变类调用）期间的地板必须显著更高。
  #  2026-09-23 r3 修正（夹具自身缺陷，非产品）：此前 body 用**2 节点**曲线 ⇒ 每次都 400
  #  INVALID_CURVE（产品要求 4 节点），"改变类工作"根本没发生，活动窗 CPU ≈ 0 且窗口仅 ~0.1 s
  #  ⇒ 断言随采样噪声随机 FAIL（真因不是产品）。现在：合法 4 节点曲线 + 断言这些调用确实 200 +
  #  把测量窗拉到 ≥1.0 s，使 activeMsPerSec 成为可复现的速率而不是 0.1 s 的噪声。
  $ac16Curve = '{"nodes":[{"tempC":0,"dutyPercent":0},{"tempC":55,"dutyPercent":45},{"tempC":75,"dutyPercent":70},{"tempC":90,"dutyPercent":100}],"leaseId":"LEASE"}'
  $c1 = $prID.TotalProcessorTime
  $swAct = [Diagnostics.Stopwatch]::StartNew()
  $ac16Rounds = 0; $ac16Bad = 0
  # 轮数上限只是"防呆"，**不得**成为活动窗的截断原因：2026-09-24 实测本机 400 轮在 0.93 s 内
  # 跑完 ⇒ 上限先于 1.0 s 生效，把 activeSec 压到 <1 s 而误判 FAIL（夹具缺陷，非产品）。
  # 提到 2000 后，窗口一律由 1.0 s 判据结束，轮数上限实际上永不触发。
  while ($swAct.Elapsed.TotalSeconds -lt 1.0 -and $ac16Rounds -lt 2000) {
    $ac16Rounds++
    $null = Send-SimFanApi -Method POST -Path '/api/open' -Base $baseID -Token $tokID -Body '{}' -TimeoutMs 5000 -IsChangeCall
    $null = Send-SimFanApi -Method POST -Path '/api/open-events' -Base $baseID -Token $tokID -Body '{}' -TimeoutMs 5000 -IsChangeCall
    $acq = Send-SimFanApi -Method POST -Path '/api/acquire-control' -Base $baseID -Token $tokID -Body '{}' -TimeoutMs 5000 -IsChangeCall
    $ls = "$($acq.json.lease.leaseId)"
    $en = Send-SimFanApi -Method POST -Path '/api/enable' -Base $baseID -Token $tokID -Body ($ac16Curve.Replace('LEASE', $ls)) -TimeoutMs 5000 -IsChangeCall
    $rs = Send-SimFanApi -Method POST -Path '/api/restore' -Base $baseID -Token $tokID -Body ('{"leaseId":"' + $ls + '"}') -TimeoutMs 5000 -IsChangeCall
    $rl = Send-SimFanApi -Method POST -Path '/api/release-control' -Base $baseID -Token $tokID -Body ('{"leaseId":"' + $ls + '"}') -TimeoutMs 5000 -IsChangeCall
    if ($acq.status -ne 200 -or $en.status -ne 200 -or $rs.status -ne 200 -or $rl.status -ne 200) { $ac16Bad++ }
  }
  $swAct.Stop()
  $prID.Refresh()
  $actMs = (($prID.TotalProcessorTime - $c1).TotalMilliseconds) / $swAct.Elapsed.TotalSeconds
  $idleOk = ($idleMs -lt 20) -and ($actMs -gt $idleMs) -and ($ac16Bad -eq 0) -and ($swAct.Elapsed.TotalSeconds -ge 1.0)
  $idleDetail = ("idleMsPerSec={0:F1} activeMsPerSec={1:F1} idleSec={2:F0} activeSec={3:F2} rounds={4} badCalls={5} (idle<20 且 active>idle 且 改变类调用全 200 且 activeSec>=1)" -f
    $idleMs, $actMs, $swIdle.Elapsed.TotalSeconds, $swAct.Elapsed.TotalSeconds, $ac16Rounds, $ac16Bad)
}
if ($null -ne $prID -and -not $prID.HasExited) {
  $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $baseID -Token $tokID -Body '{}' -TimeoutMs 5000
  Stop-Process -Id $prID.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 800
Check-C 'AC16' '宿主空闲 CPU 地板 < 2% 单核，且改变类调用期间显著更高（指标非空转）' $idleOk $idleDetail

$failed = @($script:Cases | Where-Object { -not $_.pass })
$expectedCases = 24   # 2026-09-24 R4 阶段 2：AC15e + AC15e-legacy（用户控制端点统一快速拒绝的正例/负例）加入 ⇒ 22+2
$fakeGreen = (@($script:Cases).Count -ne $expectedCases)
$verdict = if ($failed.Count -eq 0 -and -not $fakeGreen) { 'PASS' } else { 'FAIL' }
$jf = Join-Path $OutDir ("api-contract-$stamp.json")
[IO.File]::WriteAllText($jf, ([ordered]@{
  at = (Get-Date).ToString('o'); cases = @($script:Cases); failed = $failed.Count; expectedCases = $expectedCases
  hostStartAttempts = $hostAttempts; fakeGreen = [bool]$fakeGreen; verdict = $verdict
  contractSource = 'FanLab/real-host/Program.cs:11329-11337(auth) /11286(404) /11310-11314(INVALID_JSON) /11347(413) /11193-11204(generation) /11210-11217(authority)'
  note = '契约负例；沙箱宿主自启自清；realSleepPerformed=false；产品零改动'
} | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
if ($fakeGreen) { Write-Host ("api-contract: FAKE-GREEN cases={0} expected={1}（套件自身空转，判 FAIL）" -f @($script:Cases).Count, $expectedCases) -ForegroundColor Red }
Write-Host ("api-contract: cases={0}/{1} failed={2} -> {3}" -f @($script:Cases).Count, $expectedCases, $failed.Count, $verdict) -ForegroundColor $(if ($verdict -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host ("report=" + $jf)
if ($verdict -eq 'PASS') { exit 0 } else { exit 2 }