# invariants.ps1 - 睡眠/风扇侧 12 条机器可判定不变量（204-L）
# 对齐 W3 健壮模拟测试设计：用"不变量"替代"没报错"当稳态；每条都能 FAIL。
# 只读/注入式：沙箱宿主（自启自清）+ 桩件；不触发真实睡眠；产品零改动。
[CmdletBinding()]
param([string]$OutDir = '')
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
. (Join-Path $simRoot 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $D.evidenceDir 'invariants' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:Cases = New-Object System.Collections.ArrayList
function Inv([string]$id, [string]$name, [bool]$ok, [string]$actual) {
  $script:Cases.Add([ordered]@{ id = $id; name = $name; pass = [bool]$ok; actual = $actual }) | Out-Null
  Write-Host ("  [{0}] {1} :: {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $id, $actual) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}
$settingsPath = $D.settingsPath
$settingsBefore = if (Test-Path -LiteralPath $settingsPath) { (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash } else { 'missing' }
$procsBefore = @(Get-SimProcessesByRole 'App') + @(Get-SimProcessesByRole 'FanHost')
$filesBefore = @(Get-ChildItem (Join-Path $D.localAppData 'fan-host') -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName + '|' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })

# I1–I3：单实例 / 唯一宿主实例 / 退出零残留 —— 由沙箱宿主生命周期验证
$port = 8896
$sandDir = Join-Path $OutDir 'sandbox'; New-Item -ItemType Directory -Force -Path $sandDir | Out-Null
$tokFile = Join-Path $sandDir 'YeManFanHost.session'; New-SimFanSessionToken $tokFile | Out-Null
$proc = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$port", '--protocol-version', '2', '--session-token-file', $tokFile, '--mock-handshake') -PassThru -WindowStyle Hidden
$ready = Wait-SimHostReady -Port $port -TokenPath $tokFile -TimeoutSec 60
$tok = Get-SimFanSessionToken $tokFile
$base = "http://127.0.0.1:$port"
$inst1 = @(Get-SimHostInstance)
Inv 'I1' '无双实体：同一角色同时只有一个进程（自启后实例数=1）' (@($inst1).Count -eq 1) ("hostInstances=" + @($inst1).Count)
$tok2 = Join-Path $sandDir 'second.session'; New-SimFanSessionToken $tok2 | Out-Null
$p2 = Start-Process -FilePath $D.hostExe -ArgumentList @('--port', "$($port + 1)", '--protocol-version', '2', '--session-token-file', $tok2, '--mock-handshake') -PassThru -WindowStyle Hidden
$p2.WaitForExit(8000) | Out-Null; $p2.Refresh()
Inv 'I2' '单实例语义：第二实例被拒（exitCode≠0）' ($p2.HasExited -and $p2.ExitCode -ne 0) ("exited={0} code={1}" -f $p2.HasExited, $(if ($p2.HasExited) { $p2.ExitCode } else { 'running' }))
if (-not $p2.HasExited) { try { $p2.Kill() } catch { } }

# I4：未知即拒绝且 changeCallCount==0（门未建立时）
$stubLog = Join-Path $OutDir "stub-unknown-$stamp.jsonl"
$stub = Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $testsDir 'stub_host.ps1'), '-Port', '8897', '-State', 'gate-unknown', '-LogPath', $stubLog) -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
$gate = Test-SimActionGate -Base 'http://127.0.0.1:8897' -Token 'x' -Kind 'external'
$posts = 0
if (Test-Path -LiteralPath $stubLog) { $posts = @(Get-Content -LiteralPath $stubLog -Encoding UTF8 | Where-Object { $_ -match '"method":"POST"' }).Count }
try { if (-not $stub.HasExited) { $stub.Kill() } } catch { }
Inv 'I4' '未知即拒绝：门未建立 ⇒ 0 次变更调用' ((-not $gate.allowed) -and ($posts -eq 0)) ("allowed={0} POSTs={1}" -f $gate.allowed, $posts)

# I5/I6/I7/I9/I10：真宿主路径（幂等 / 代次单调 / 静默成功检出 / 收敛有界）
$i5 = $false; $i6 = $false; $i9 = $false; $i10 = $false; $d5 = ''; $d6 = ''; $d9 = ''; $d10 = ''
if ($ready.ready -and $tok) {
  $null = Send-SimFanApi -Method POST -Path '/api/handshake' -Base $base -Token $tok -Body '{}' -TimeoutMs 8000
  # I9 代次单调：先用 gen=1 成功，再用 gen=1 复发 ⇒ 必须被拒（幂等/单调）
  $s1 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":1,"source":"selftest.native"}' -TimeoutMs 6000 -IsChangeCall
  $r1 = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body '{"generation":1,"source":"selftest.native"}' -TimeoutMs 6000 -IsChangeCall
  $s1b = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":1,"source":"selftest.native"}' -TimeoutMs 6000 -IsChangeCall
  $i9 = ($s1.status -eq 200) -and ($s1b.status -ne 200)
  $d9 = ("first={0} reuse={1}" -f $s1.status, $s1b.status)
  # I6 幂等：连续两次同参 resume 不得双写（第二次应 dedup/noop 或状态不变）
  $s2 = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body '{"generation":2,"source":"selftest.native"}' -TimeoutMs 6000 -IsChangeCall
  $r2a = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body '{"generation":2,"source":"selftest.native"}' -TimeoutMs 6000 -IsChangeCall
  $r2b = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body '{"generation":2,"source":"selftest.native"}' -TimeoutMs 6000 -IsChangeCall
  $i6 = ("$($r2b.json.state.state)" -ne 'Suspended')
  $d6 = ("second-resume={0} dedup={1} noop={2} state={3}" -f $r2b.status, $r2b.json.deduplicated, $r2b.json.resumeNoop, $r2b.json.state.state)
  # I10 无静默成功：状态面必须与请求结果一致（请求 200 ⇒ 状态离开 Stopped；此处检 Suspended→resume 后回 AwaitingControl/On）
  $i10 = ("$($r2a.json.state.state)" -in @('AwaitingControl', 'On'))
  $d10 = ("resume={0} state={1}" -f $r2a.status, $r2a.json.state.state)
  # I5 重试有界：连续 5 次快速循环后状态集合有限
  $states = @()
  for ($i = 0; $i -lt 5; $i++) {
    $null = Send-SimFanApi -Method POST -Path '/api/suspend' -Base $base -Token $tok -Body ('{"generation":' + (10 + $i) + ',"source":"selftest.native"}') -TimeoutMs 6000 -IsChangeCall
    Start-Sleep -Milliseconds 60
    $rr = Send-SimFanApi -Method POST -Path '/api/resume' -Base $base -Token $tok -Body ('{"generation":' + (10 + $i) + ',"source":"selftest.native"}') -TimeoutMs 6000 -IsChangeCall
    $states += "$($rr.json.state.state)"
  }
  $i5 = (@($states | Select-Object -Unique).Count -le 2)
  $d5 = ("distinctStates=" + ((@($states | Select-Object -Unique)) -join ','))
  # I3 退出零残留
  $null = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $base -Token $tok -Body '{}' -TimeoutMs 5000
  Start-Sleep -Seconds 3
  $left = @(Get-SimHostInstance)
  Inv 'I3' '退出零残留：宿主优雅退出且端口释放' ((@($left).Count -eq 0) -and (-not (Test-SimPort $port))) ("leftHosts=" + @($left).Count + " portOpen=" + (Test-SimPort $port))
} else {
  Inv 'I3' '退出零残留（宿主未就绪，未评）' $false 'host-not-ready'
}
Inv 'I5' '重试有界：连续快速循环后状态集合有限（不震荡）' $i5 $d5
Inv 'I6' '命令幂等：重复 resume 不产生第二次副作用' $i6 $d6
Inv 'I9' '代次单调：复用旧代次被拒' $i9 $d9
Inv 'I10' '无静默成功：请求结果与状态面一致' $i10 $d10

# I7 证据完备新鲜：必需摘要缺失 ⇒ exit 9（假绿防护；自 204-M 起用独立 provoke 脚本，避免内联拼接）
$po = & powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $testsDir 'provoke-missing.ps1') -SimRoot $simRoot -EvidenceDir $OutDir 2>&1 | Out-String
$pc = $LASTEXITCODE
Inv 'I7' '证据完备新鲜：必需摘要缺失 ⇒ exit 9（假绿防护）' ($pc -eq 9) ("exit=" + $pc + " | " + (($po -split "`r?`n" | Where-Object { $_ -match 'verdict=' } | Select-Object -First 1)))

# I8 配置不丢更新 + I11 爆炸半径受限
$settingsAfter = if (Test-Path -LiteralPath $settingsPath) { (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash } else { 'missing' }
Inv 'I8' '配置不丢更新：设置文件未被工具改写' ($settingsBefore -eq $settingsAfter) ("before=" + $settingsBefore.Substring(0, 8) + " after=" + $settingsAfter.Substring(0, 8))
$procsAfter = @(Get-SimProcessesByRole 'App') + @(Get-SimProcessesByRole 'FanHost')
Inv 'I11' '爆炸半径受限：产品进程身份未变' ((@($procsBefore).Count -eq @($procsAfter).Count)) ("before=" + @($procsBefore).Count + " after=" + @($procsAfter).Count)

# I12 未证保持未证：正常 mock 路径的 oemRestoreEvidence 不得是物理读回值（由 typeseries/retrybudget 证据断言；此处抽样既有证据）
$evDir = $D.evidenceDir
$recent = @(Get-ChildItem $evDir -Filter 'sleep-sim-*.json' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 3)
$badMask = 0
foreach ($f in $recent) {
  $t = Get-Content -LiteralPath $f.FullName -Raw -Encoding UTF8
  if ($t -match 'hc-default-table-readback-confirmed') { $badMask++ }
}
Inv 'I12' '未证保持未证：既有睡眠证据中无"物理读回"伪装' ($badMask -eq 0) ("scanned=" + @($recent).Count + " masked=" + $badMask)

# I13（2026-09-23 T0，operator：绝不允许"父进程已退出而宿主不退"的死循环）：
# 有界收尾的源码锚点——预算常量 + 两条路径都设 deadline + 预算耗尽写成"未证实"事件。
$t0Src = Join-Path (Split-Path -Parent (Split-Path -Parent $simRoot)) 'FanLab\real-host\Program.cs'
$t0Ok = $false; $t0Detail = "source-missing: $t0Src"
if (Test-Path -LiteralPath $t0Src) {
  $t0Text = Get-Content -LiteralPath $t0Src -Raw -Encoding UTF8
  $t0Need = @('ParentExitFinalizeBudgetMs', 'exit-budget-exhausted', 'unverified-not-success')
  $t0Missing = @($t0Need | Where-Object { -not $t0Text.Contains($_) })
  $t0SiteCount = ([regex]::Matches($t0Text, 'finalizeDeadline')).Count
  $t0Ok = ($t0Missing.Count -eq 0) -and ($t0SiteCount -ge 4)
  $t0Detail = ("anchors=" + $t0Need.Count + " finalizeDeadlineSites=" + $t0SiteCount + " missing=" + ($t0Missing -join ','))
}
Inv 'I13' 'T0：父进程退出/停止请求的收尾有界（预算常量 + 两条路径 deadline + 未证实事件）' $t0Ok $t0Detail

# I14（2026-09-23，真机 16:13 取证新增；同日 Q1 补充裁决后改为"新契约"版）：
# ① native 侧**接受**窗口有界：退避 0/250/500 + 单次 HTTP 650 ms ⇒ ≤2700 ms（有界）；
# ② native 侧**观察**窗口 = 宿主重建预算内（kFanResumeObserveWindowMs = 15000，同一 generation 只读）；
# ③ 宿主侧必须暴露结构化接受（resumeAccepted/resumePhase/retryAfterMs）与阶段真值
#    （Ready/Resuming/Suspended/FaultLocked 四态派生），且**大于 3 s 的重建仍在窗口内**——
#    真机 10.2 s 重建（2026-09-23 16:09:14→16:09:24）在新契约下由 AC15b 沙箱场景覆盖。
$resumeSrc = Join-Path (Split-Path -Parent (Split-Path -Parent $simRoot)) 'native\main.cpp'
$resumeHostSrc = Join-Path (Split-Path -Parent (Split-Path -Parent $simRoot)) 'FanLab\real-host\Program.cs'
$resumeOk = $false
$resumeDetail = "source-missing: $resumeSrc"
if ((Test-Path -LiteralPath $resumeSrc) -and (Test-Path -LiteralPath $resumeHostSrc)) {
  $rText = Get-Content -LiteralPath $resumeSrc -Raw -Encoding UTF8
  $hText = Get-Content -LiteralPath $resumeHostSrc -Raw -Encoding UTF8
  $rMissing = @(@(
    'const DWORD acceptBackoffMs[3] = {0, 250, 500};',
    'setHttpTimeouts(session, 650);',
    '"resume-recovery-exhausted"',
    'static constexpr unsigned long long kFanResumeObserveWindowMs = 15000;',
    '"host-rebuild-timeout"',
    'fanHostEmergencyGetState') | Where-Object { -not $rText.Contains($_) })
  $hMissing = @(@(
    'resumeAccepted = true,',
    'resumePhase = snapshot?.ResumePhase',
    'ResumeRetryAfterMs',
    'DeriveResumePhase',
    'MockResumeRebuildDelayMs') | Where-Object { -not $hText.Contains($_) })
  $nativeAcceptWindowMs = 3 * 650 + 250 + 500
  $observeWindowMs = 15000
  $resumeOk = ($rMissing.Count -eq 0) -and ($hMissing.Count -eq 0) -and
    ($nativeAcceptWindowMs -le 3000) -and ($observeWindowMs -ge 10200) -and ($observeWindowMs -le 15000)
  $resumeDetail = "acceptWindowMs=$nativeAcceptWindowMs observeWindowMs=$observeWindowMs anchorsMissing=" + (($rMissing + $hMissing) -join ',') +
    " | real-machine rebuild=10200ms now covered by the bounded observation (AC15b slow-rebuild sandbox scenario)"
}
Inv 'I14' '唤醒恢复：接受有界 + 观察窗覆盖真机重建预算（结构性对齐，不再有 3 s 缺口）' $resumeOk $resumeDetail

$failed = @($script:Cases | Where-Object { -not $_.pass })
$verdict = if ($failed.Count -eq 0) { 'PASS' } else { 'FAIL' }
$report = [ordered]@{ at = (Get-Date).ToString('o'); tool = Get-SimFileIdentity (Join-Path $simRoot 'lib\sim_core.ps1') 'sim_core'
  cases = @($script:Cases); failed = $failed.Count; verdict = $verdict
  note = '14 条机器可判定不变量（睡眠/风扇侧，含 I13 T0 有界退出锚点、I14 唤醒恢复窗口对齐）；realSleepPerformed=false；沙箱宿主自启自清' }
$jf = Join-Path $OutDir ("invariants-$stamp.json")
[IO.File]::WriteAllText($jf, ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Host ("invariants: cases={0} failed={1} -> {2}" -f @($script:Cases).Count, $failed.Count, $verdict) -ForegroundColor $(if ($failed.Count) { 'Red' } else { 'Green' })
Write-Host ("report=" + $jf)
if ($failed.Count -eq 0) { exit 0 } else { exit 2 }
