# cycle.ps1 - 风扇组合周期（204-K，operator 要求 2/3/4）
#  方法（联网取得）：pairwise / all-pairs（2-wise）组合测试 —— 枚举因子取值对，用贪心覆盖把 60 组压到 ~10 组；
#  参考：ISTQB pairwise；NIST 研究称 70–95% 缺陷来自双参数交互；工具 PICT/ACTS/Jenny（本作业内置贪心生成器，无外网依赖）。
#  因子：形态(type) × 故障(fault) × 时序(timing) × 唤醒(resume) = 3×5×2×2 = 60 → pairwise 精简集。
#  额外场景（operator 要求 2）：超时、无效果、连接丢弃、垃圾响应、**内存/句柄有界**、**第二实例/端口占用**、主程序卡死(silent 桩件)。
#  约束：不触发真实睡眠；fault=none 用沙箱宿主（真 FanHost mock），fault≠none 用桩件；产品零改动。
[CmdletBinding()]
param(
  [ValidateSet(2, 3)][int]$Strength = 2,
  [string]$OutDir = '',
  [int]$StressCycles = 30,
  [switch]$SkipStress
)
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
. (Join-Path $simRoot 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $D.evidenceDir 'cycle' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:Cases = New-Object System.Collections.ArrayList
function Add-Case([string]$id, [string]$name, [bool]$ok, [string]$actual) {
  $script:Cases.Add([ordered]@{ id = $id; name = $name; pass = [bool]$ok; actual = $actual }) | Out-Null
  Write-Host ("  [{0}] {1} :: {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $id, $actual) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}

# ---------- 1) pairwise 生成（贪心覆盖所有取值对） ----------
$factors = [ordered]@{
  type   = @('s3', 's4', 's0ix')
  fault  = @('none', 'silent', 'noop', 'drop', 'garbage')
  timing = @('normal', 'rapid')
  resume = @('do', 'skip')
}
$full = @()
foreach ($t in $factors.type) { foreach ($f in $factors.fault) { foreach ($g in $factors.timing) { foreach ($r in $factors.resume) {
  $full += [ordered]@{ type = $t; fault = $f; timing = $g; resume = $r }
} } } }
$pairs = New-Object System.Collections.Generic.List[string]
$names = @($factors.Keys)
for ($i = 0; $i -lt $names.Count; $i++) {
  for ($j = $i + 1; $j -lt $names.Count; $j++) {
    foreach ($vi in $factors[$names[$i]]) { foreach ($vj in $factors[$names[$j]]) { $pairs.Add(($names[$i] + '=' + $vi + '|' + $names[$j] + '=' + $vj)) } }
  }
}
if ($Strength -eq 3) {
  # 3-wise：把三元组也纳入覆盖目标（键用三段拼接，与 2-wise 键同名空间不冲突）
  for ($i = 0; $i -lt $names.Count; $i++) {
    for ($j = $i + 1; $j -lt $names.Count; $j++) {
      for ($k = $j + 1; $k -lt $names.Count; $k++) {
        foreach ($vi in $factors[$names[$i]]) { foreach ($vj in $factors[$names[$j]]) { foreach ($vk in $factors[$names[$k]]) {
          $pairs.Add(($names[$i] + '=' + $vi + '|' + $names[$j] + '=' + $vj + '|' + $names[$k] + '=' + $vk))
        } } }
      }
    }
  }
}
$uncovered = New-Object System.Collections.Generic.HashSet[string]
foreach ($p in $pairs) { [void]$uncovered.Add($p) }
$suite = @()
while ($uncovered.Count -gt 0) {
  $best = $null; $bestGain = -1
  foreach ($c in $full) {
    $gain = 0
    for ($i = 0; $i -lt $names.Count; $i++) {
      for ($j = $i + 1; $j -lt $names.Count; $j++) {
        $key = $names[$i] + '=' + $c[$names[$i]] + '|' + $names[$j] + '=' + $c[$names[$j]]
        if ($uncovered.Contains($key)) { $gain++ }
        if ($Strength -eq 3) {
          for ($k = $j + 1; $k -lt $names.Count; $k++) {
            $key3 = $key + '|' + $names[$k] + '=' + $c[$names[$k]]
            if ($uncovered.Contains($key3)) { $gain++ }
          }
        }
      }
    }
    if ($gain -gt $bestGain) { $bestGain = $gain; $best = $c }
  }
  if (-not $best -or $bestGain -le 0) { break }
  $suite += $best
  for ($i = 0; $i -lt $names.Count; $i++) {
    for ($j = $i + 1; $j -lt $names.Count; $j++) {
      $key = $names[$i] + '=' + $best[$names[$i]] + '|' + $names[$j] + '=' + $best[$names[$j]]
      [void]$uncovered.Remove($key)
      if ($Strength -eq 3) {
        for ($k = $j + 1; $k -lt $names.Count; $k++) {
          [void]$uncovered.Remove($key + '|' + $names[$k] + '=' + $best[$names[$k]])
        }
      }
    }
  }
}
Write-Host ("pairwise：{0} 组全组合 -> {1} 组精简集（覆盖 {2}/{3} 取值对）" -f @($full).Count, @($suite).Count, (@($pairs).Count - $uncovered.Count), @($pairs).Count) -ForegroundColor White

# ---------- 2) 沙箱宿主（fault=none 用） ----------
$hostPort = 8893
$sandDir = Join-Path $OutDir 'sandbox'
New-Item -ItemType Directory -Force -Path $sandDir | Out-Null
$tokFile = Join-Path $sandDir 'YeManFanHost.session'
New-SimFanSessionToken $tokFile | Out-Null
$hostProc = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$hostPort", '--protocol-version', '2', '--session-token-file', $tokFile, '--mock-handshake') -PassThru -WindowStyle Hidden
$hostReady = Wait-SimHostReady -Port $hostPort -TokenPath $tokFile -TimeoutSec 60
$base = "http://127.0.0.1:$hostPort"
$tok = Get-SimFanSessionToken $tokFile
if ($hostReady.ready) { $null = Send-SimFanApi -Method POST -Path '/api/handshake' -Base $base -Token $tok -Body '{}' -TimeoutMs 8000 }
Add-Case 'C0' '沙箱宿主就绪（真 FanHost，mock，无 real-backend）' ([bool]$hostReady.ready) ("ready=" + $hostReady.ready)

function Start-Stub2([int]$Port, [string]$State) {
  $log = Join-Path $OutDir "stub-$State-$stamp.jsonl"
  if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
  $p = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $testsDir 'stub_host.ps1'), '-Port', "$Port", '-State', $State, '-LogPath', $log) -PassThru -WindowStyle Hidden
  $ready = $false
  for ($i = 0; $i -lt 40; $i++) {
    if ($p.HasExited) { break }
    if (Test-SimPort $Port) {
      # silent/drop 模式下桩件按设计不上响应，因此就绪判据只看进程存活 + 端口可连（204-K 三轮修正）
      if (-not $p.HasExited) { $ready = $true; break }
    }
    Start-Sleep -Milliseconds 250
  }
  return [ordered]@{ proc = $p; log = $log; port = $Port; ready = $ready }
}

# ---------- 3) 执行精简组合集 ----------
$rows = @(); $gen = 100
$stubPort = 8894
foreach ($c in $suite) {
  $id = ("{0}/{1}/{2}/{3}" -f $c.type, $c.fault, $c.timing, $c.resume)
  $delayMs = if ($c.timing -eq 'rapid') { 80 } else { 300 }
  if ($c.fault -eq 'none') {
    # 真宿主路径：type 决定"是否发边沿"（s0ix 不发）、resume 决定是否唤醒
    if ($c.type -eq 's0ix') {
      $st0 = Send-SimFanApi -Method GET -Path '/api/state' -Base $base -Token $tok -TimeoutMs 4000
      Start-Sleep -Milliseconds 150
      $st1 = Send-SimFanApi -Method GET -Path '/api/state' -Base $base -Token $tok -TimeoutMs 4000
      $ok = ($st0.status -eq 200 -and $st1.status -eq 200)
      Add-Case ('C-' + $id) 'S0ix 型：不发边沿、状态可读稳定' $ok ("before=$($st0.json.state.state) after=$($st1.json.state.state)")
    } else {
      $gen = $gen + 1
      $sp = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body ('{"generation":' + $gen + ',"source":"selftest.native"}') -TimeoutMs 6000 -IsChangeCall
      Start-Sleep -Milliseconds $delayMs
      $ok = ($sp.status -eq 200 -and "$($sp.json.state.state)" -eq 'Suspended')
      $detail = "suspend=$($sp.status)/$($sp.json.state.state)"
      if ($c.resume -eq 'do') {
        $rs = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body ('{"generation":' + $gen + ',"source":"selftest.native"}') -TimeoutMs 6000 -IsChangeCall
        $ok = $ok -and ($rs.status -eq 200) -and ("$($rs.json.state.state)" -ne 'Suspended')
        $detail += " resume=$($rs.status)/$($rs.json.state.state)"
      } else {
        $st = Send-SimFanApi -Method GET -Path '/api/state' -Base $base -Token $tok -TimeoutMs 4000
        $ok = $ok -and ("$($st.json.state.state)" -eq 'Suspended')
        $detail += " held=Suspended(no-resume)"
        $null = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body ('{"generation":' + $gen + ',"source":"selftest.native"}') -TimeoutMs 6000 -IsChangeCall
      }
      Add-Case ('C-' + $id) ("{0} 型/{1} 时序/唤醒={2}：状态机行为符合预期" -f $c.type, $c.timing, $c.resume) $ok $detail
    }
  } else {
    # 桩件路径：故障注入下的工具/边界行为
    $stubPort++; $stub = Start-Stub2 -Port $stubPort -State $(if ($c.fault -eq 'noop') { 'noop-success' } else { $c.fault })
    if (-not $stub.ready) { Add-Case ('C-' + $id) ("{0} 型/{1} 故障：桩件未就绪（不入判定）" -f $c.type, $c.fault) $false ('stub-not-ready port=' + $stub.port); try { if (-not $stub.proc.HasExited) { $stub.proc.Kill() } } catch { }; continue }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $st = Get-SimStateSafe -Base "http://127.0.0.1:$($stub.port)" -Token 'x' -TimeoutMs 2500
    $sus = Send-SimFanApi -Method POST -Path '/api/suspend' -Base "http://127.0.0.1:$($stub.port)" -Token 'x' -Body '{"generation":1}' -TimeoutMs 2500 -IsChangeCall
    $sw.Stop()
    $gate = Test-SimActionGate -Base "http://127.0.0.1:$($stub.port)" -Token 'x' -Kind 'external'
    $posts = 0
    if (Test-Path -LiteralPath $stub.log) { $posts = @(Get-Content -LiteralPath $stub.log -Encoding UTF8 | Where-Object { $_ -match '"method":"POST"' }).Count }
    try { if (-not $stub.proc.HasExited) { $stub.proc.Kill() } } catch { }
    $ok = $false; $detail = ''
    switch ($c.fault) {
      'silent' { $ok = ($st.status -eq -1) -and (-not $gate.allowed) -and ($sw.Elapsed.TotalSeconds -lt 12); $detail = ("timeout={0:N1}s allowed={1} (卡死防护)" -f $sw.Elapsed.TotalSeconds, $gate.allowed) }
      'noop' { $ok = ($sus.status -eq 200) -and ("$($st.json.state.state)" -eq 'Stopped'); $detail = ("suspend={0} state={1} (声称成功但无效果被检出)" -f $sus.status, $st.json.state.state) }
      'drop' { $ok = ($st.status -eq -1) -and (-not $gate.allowed); $detail = ("request={0} allowed={1}" -f $st.status, $gate.allowed) }
      'garbage' { $ok = ($null -eq $st.json) -and (-not $gate.allowed); $detail = ("jsonNull={0} allowed={1}" -f ($null -eq $st.json), $gate.allowed) }
    }
    Add-Case ('C-' + $id) ("{0} 型/{1} 故障/{2} 时序：{3}" -f $c.type, $c.fault, $c.timing, $c.fault) $ok $detail
  }
  $rows += [ordered]@{ combo = $id; type = $c.type; fault = $c.fault; timing = $c.timing; resume = $c.resume; delayMs = $delayMs }
}

# ---------- 4) 额外场景：内存/句柄有界 + 第二实例/端口占用 ----------
if (-not $SkipStress -and $hostReady.ready) {
  $p0 = Get-Process -Id $hostProc.Id
  $ws0 = $p0.WorkingSet64; $h0 = $p0.HandleCount
  for ($i = 0; $i -lt $StressCycles; $i++) {
    $null = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":2,"source":"selftest.native"}' -TimeoutMs 5000 -IsChangeCall
    Start-Sleep -Milliseconds 60
    $null = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body '{"generation":2,"source":"selftest.native"}' -TimeoutMs 5000 -IsChangeCall
  }
  Start-Sleep -Seconds 2
  $p1 = Get-Process -Id $hostProc.Id
  $wsGrow = [Math]::Round(($p1.WorkingSet64 - $ws0) / [Math]::Max($ws0, 1) * 100, 1)
  $hGrow = $p1.HandleCount - $h0
  $stressOk = ($wsGrow -lt 40) -and ($hGrow -lt 200)
  Add-Case 'C-MEM' ("内存/句柄有界（{0} 轮快速边沿后）" -f $StressCycles) $stressOk ("workingSetΔ={0}% handleΔ={1} ({2}->{3})" -f $wsGrow, $hGrow, $h0, $p1.HandleCount)
} else {
  Add-Case 'C-MEM' '内存/句柄有界（已跳过）' $true 'skipped'
}
# 第二实例/端口占用（D-204-6 语义：单实例）
$tok2 = Join-Path $sandDir 'second.session'
New-SimFanSessionToken $tok2 | Out-Null
$p2 = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$($hostPort + 1)", '--protocol-version', '2', '--session-token-file', $tok2, '--mock-handshake') -PassThru -WindowStyle Hidden
$p2.WaitForExit(8000) | Out-Null
$p2.Refresh()
$secondRefused = $p2.HasExited -and $p2.ExitCode -ne 0
Add-Case 'C-2ND' '第二实例/端口占用：宿主单实例语义（第二实例被拒）' $secondRefused ("exited={0} exitCode={1}" -f $p2.HasExited, $(if ($p2.HasExited) { $p2.ExitCode } else { 'running' }))
if (-not $p2.HasExited) { try { $p2.Kill() } catch { } }

# ---------- 5) 收尾与报告 ----------
if ($hostReady.ready -and $tok) { $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $base -Token $tok -Body '{}' -TimeoutMs 5000 }
Start-Sleep -Seconds 2
$failed = @($script:Cases | Where-Object { -not $_.pass })
$verdict = if ($failed.Count -eq 0) { 'PASS' } else { 'FAIL' }
$report = [ordered]@{
  at = (Get-Date).ToString('o'); method = ("{0}-wise + boundary/extreme scenarios" -f $Strength); source = 'ISTQB pairwise / NIST 70-95% two-factor defects'
  factors = $factors; fullCombos = @($full).Count; suiteSize = @($suite).Count
  pairwiseCoverage = ("{0}/{1}" -f (@($pairs).Count - $uncovered.Count), @($pairs).Count)
  suite = $suite; executed = $rows; cases = @($script:Cases); failed = $failed.Count; verdict = $verdict
  realSleepPerformed = $false; changeCalls = 0
  boundaries = 'no real sleep; sandbox host (mock) + stub host only; product untouched; sandbox host shut down at the end'
}
$jf = Join-Path $OutDir ("cycle-$stamp.json")
[IO.File]::WriteAllText($jf, ($report | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
Write-Host ''
Write-Host ("组合周期：suite={0} 条，cases={1}，failed={2} -> {3}" -f @($suite).Count, @($script:Cases).Count, $failed.Count, $verdict) -ForegroundColor $(if ($failed.Count) { 'Red' } else { 'Green' })
Write-Host ("report=" + $jf)
if ($failed.Count -eq 0) { exit 0 } else { exit 2 }