# lib_sleep_rapidwake.ps1 - 入睡失败/极短睡眠（rapid-wake）系列（204-H）
# 场景（operator 描述的真实情况）：S0ix 下按下睡眠后 1–2 s 即被唤醒；本系列**极端化**为 0.1 / 0.5 / 1 / 2 s，
# 并测"入睡失败马上唤醒"的边界：极短静默、去重/幂等、残余状态、带曲线 fail-closed、连续快速循环是否累积故障。
# 注入式（offline-injection）：只经鉴权 HTTP + 自启沙箱宿主；**不触发任何真实睡眠**（硬约束）。
[CmdletBinding()]
param()

function Invoke-SleepRapidWake {
  Write-Host '=== 入睡失败/极短睡眠（rapid-wake）系列：0.1 / 0.5 / 1 / 2 s ===' -ForegroundColor White
  $delays = @(0.1, 0.5, 1, 2)
  if ($RapidDelays) { $delays = @($RapidDelays -split '[,;\s]+' | Where-Object { $_ } | ForEach-Object { [double]$_ }) }
  $cycles = if ($RapidCycles -gt 0) { $RapidCycles } else { 5 }
  $script:Run.extra['rapidWake'] = [ordered]@{ delays = $delays; cycles = $cycles; rows = @(); curve01 = $null; stress = @() }

  # ---- A) 极短静默入睡→唤醒（无曲线；纯边界） ----
  $rows = @()
  foreach ($d in $delays) {
    $ms = [int]([double]$d * 1000)
    $g = New-Generation
    $sp = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'RAPID' -Label ("suspend-{0}s" -f $d) -ChangeCall
    Start-Sleep -Milliseconds $ms
    $rs = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'RAPID' -Label ("resume-{0}s" -f $d) -ChangeCall
    $st = "$($rs.stateAfter.state)"; $ps = "$($rs.stateAfter.powerState)"
    $resolvedOk = ($rs.status -eq 200 -and ($st -eq 'AwaitingControl' -or $st -eq 'On')) -or
                  ($rs.status -eq 409 -and $st -eq 'FaultLocked') -or
                  ($rs.deduplicated -eq $true) -or ($rs.resumeNoop -eq $true)
    Add-SimCheck -Run $script:Run -Form 'RAPID' -Name ("入睡 {0}s：suspend 被接受" -f $d) -Ok ($sp.status -eq 200) -Detail ("status=$($sp.status) state=$($sp.stateAfter.state)")
    Add-SimCheck -Run $script:Run -Form 'RAPID' -Name ("入睡 {0}s：极短静默后唤醒被如实处理（200 恢复 / 409 fail-closed / 去重·幂等）" -f $d) -Ok $resolvedOk -Detail ("resume=$($rs.status) state=$st/$ps dedup=$($rs.deduplicated) noop=$($rs.resumeNoop) reason=$($rs.reason) code=$($rs.errorCode)")
    Add-SimCheck -Run $script:Run -Form 'RAPID' -Name ("入睡 {0}s：轮末不残留 Suspended（状态可继续）" -f $d) -Ok ($st -ne 'Suspended' -or $rs.status -eq 409) -Detail ("final=$st/$ps")
    $rows += [ordered]@{ delaySec = [double]$d; suspend = $sp.status; suspendState = $sp.stateAfter.state; resume = $rs.status; finalState = $st; finalPowerState = $ps
                         deduplicated = $rs.deduplicated; resumeNoop = $rs.resumeNoop; reason = $rs.reason; code = $rs.errorCode }
  }
  $script:Run.extra['rapidWake']['rows'] = $rows

  # ---- B) 带曲线的 0.1 s（入睡失败即唤醒时，需要重建的路径如何表现） ----
  $sess = Open-MockSession
  $curve = '{"nodes":[{"tempC":0,"dutyPercent":0},{"tempC":55,"dutyPercent":30},{"tempC":75,"dutyPercent":60},{"tempC":90,"dutyPercent":100}],"leaseId":"' + $sess.leaseId + '"}'
  $en = Send-Api -Method POST -Path '/api/enable' -Body $curve -Form 'RAPID' -Label 'curve-0.1s-enable'
  Add-SimCheck -Run $script:Run -Form 'RAPID' -Name '0.1s 带曲线：曲线会话就绪（写入被观测）' -Ok ($en.status -eq 200 -and $en.stateAfter.hardwareWritesObserved -eq $true) -Detail ("status=$($en.status) hwWrites=$($en.stateAfter.hardwareWritesObserved)")
  $g2 = New-Generation
  $sp2 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g2) -Form 'RAPID' -Label 'suspend-0.1s-curve' -ChangeCall
  Start-Sleep -Milliseconds 100
  $rs2 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g2) -Form 'RAPID' -Label 'resume-0.1s-curve' -ChangeCall
  $st2 = "$($rs2.stateAfter.state)"; $notPhysical2 = "$($rs2.stateAfter.oemRestoreEvidence)" -ne 'hc-default-table-readback-confirmed'
  Add-SimCheck -Run $script:Run -Form 'RAPID' -Name '0.1s 带曲线：入睡 fail-closed（FaultLocked，不伪造完成）' -Ok ("$($sp2.stateAfter.state)" -eq 'FaultLocked' -or $rs2.status -eq 409) -Detail ("suspendState=$($sp2.stateAfter.state) resume=$($rs2.status) code=$($rs2.errorCode)")
  Add-SimCheck -Run $script:Run -Form 'RAPID' -Name '0.1s 带曲线：不伪装物理重建（证据非物理读回）' -Ok $notPhysical2 -Detail ("evidence=$($rs2.stateAfter.oemRestoreEvidence)")
  $rel = Send-Api -Method POST -Path '/api/release-control' -Body ('{"leaseId":"' + $sess.leaseId + '"}') -Form 'RAPID' -Label 'release-control-0.1s' -ChangeCall
  Add-SimCheck -Run $script:Run -Form 'RAPID' -Name '0.1s 带曲线：显式恢复路径有效（release-control 200）' -Ok ($rel.status -eq 200) -Detail ("status=$($rel.status) state=$($rel.stateAfter.state)")
  $script:Run.extra['rapidWake']['curve01'] = [ordered]@{ suspendState = $sp2.stateAfter.state; resume = $rs2.status; resumeCode = $rs2.errorCode; finalState = $st2; evidence = $rs2.stateAfter.oemRestoreEvidence; release = $rel.status }

  # ---- C) 连续快速循环（压力：入睡失败立即唤醒 ×N，检查是否累积故障） ----
  $stress = @()
  for ($i = 1; $i -le $cycles; $i++) {
    $g3 = New-Generation
    $s3 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g3) -Form 'RAPID' -Label ("stress-$i-suspend") -ChangeCall
    Start-Sleep -Milliseconds 100
    $r3 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g3) -Form 'RAPID' -Label ("stress-$i-resume") -ChangeCall
    $stress += [ordered]@{ i = $i; suspend = $s3.status; resume = $r3.status; state = "$($r3.stateAfter.state)"; code = $r3.errorCode; dedup = $r3.deduplicated; noop = $r3.resumeNoop }
  }
  $script:Run.extra['rapidWake']['stress'] = $stress
  $bad = @($stress | Where-Object { $_.suspend -ne 200 -or ($_.resume -ne 200 -and $_.resume -ne 409) })
  Add-SimCheck -Run $script:Run -Form 'RAPID' -Name ("连续 {0} 次 0.1s 快速入睡-唤醒：无累积故障（每次 suspend=200 且 resume ∈ 200/409）" -f $cycles) -Ok ($bad.Count -eq 0) -Detail ("iterations=$cycles bad=$($bad.Count) finals=" + ((@($stress | ForEach-Object { $_.state }) | Select-Object -Unique) -join ','))
  $stabled = (@($stress | ForEach-Object { $_.state }) | Select-Object -Unique)
  Add-SimCheck -Run $script:Run -Form 'RAPID' -Name '快速循环后状态一致（无漂移）' -Ok ($stabled.Count -eq 1) -Detail ("distinctFinalStates=" + ($stabled -join ','))

  Write-Host ''
  Write-Host 'rapid-wake 小结（风扇侧）:' -ForegroundColor White
  foreach ($r in $rows) { Write-Host ("  [{0}s] suspend={1} resume={2} final={3}/{4} dedup={5} noop={6} {7}" -f $r.delaySec, $r.suspend, $r.resume, $r.finalState, $r.finalPowerState, $r.deduplicated, $r.resumeNoop, $r.code) -ForegroundColor Gray }
  Write-Host ("  压力 {0}×0.1s：finals={1}" -f $cycles, ($stabled -join ',')) -ForegroundColor Gray
  $script:Run.extra['rapidWakeDisclaimer'] = '注入式模拟（S0ix rapid-wake 语义）；非真实睡眠；真机 S0ix 本机不支持（DEVICE_PENDING）'
}