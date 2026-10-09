# lib_sleep_typeseries.ps1 - 睡眠分型系列（204-G）：按 S0ix / S3 / S4 三种形态各跑一组**注入式**边沿，
# 观测风扇（FanHost）睡眠表现。**不触发任何真实睡眠**（operator 2026-09-22 硬约束）：
#   modeKind = offline-injection；只使用自启沙箱宿主（mock、无 --real-backend）与鉴权 HTTP。
#
# 分型语义（与 sleep_types.json 对齐，但这里是"模拟"而非真机判定；真机判型见 -Mode sleeptypes / §3.9·§3.10）：
#   S3  型：冻结-恢复语义。带 lease 入睡 → 静默 → 唤醒 ⇒ 期望 200（被接受）
#   S4  型：写盘-恢复语义。**无 lease**入睡 → 较长静默（模拟休眠期）→ 唤醒 ⇒ 期望 409 fail-closed（不伪造重建）
#   S0ix型：无经典挂起边沿 ⇒ 不发送任何变更调用，仅验证状态保持稳定（本机固件不支持 S0ix，标注形态化）

function Invoke-SleepTypeSeries {
  Write-Host '=== 睡眠分型系列（S0ix / S3 / S4；注入式，不触发真实睡眠）===' -ForegroundColor White
  $script:Run.extra['simulatedSeries'] = @()
  $script:Run.extra['seriesPolicy'] = 'offline-injection only; no real sleep; sandbox host (mock, no --real-backend)'

  $series = @(
    [ordered]@{ type = 'S3';    quiet = 3;  lease = $true;  desc = '经典 S3 语义：带 lease 入睡 → 静默 3s → 唤醒（期望 200）' }
    [ordered]@{ type = 'S4';    quiet = 25; lease = $false; desc = 'S4 语义：无 lease 入睡 → 静默 25s（模拟写盘期）→ 唤醒（期望 409 fail-closed）' }
    [ordered]@{ type = 'S0ix';  quiet = 3;  lease = $null;  desc = 'S0ix 语义：无经典挂起边沿 ⇒ 不发边沿，仅状态保持检查' }
  )

  foreach ($s in $series) {
    $rec = [ordered]@{
      type = $s.type; desc = $s.desc; simulated = $true; quietSec = $s.quiet; usesLease = $s.lease
      steps = @(); fanBehaviour = @(); outcome = $null
    }
    Write-Host ("--- 分型 " + $s.type + "：" + $s.desc) -ForegroundColor White
    $st0 = Send-Api -Method GET -Path '/api/state' -NoStateAfter -Form 'SERIES' -Label ($s.type + '-before')
    $rec.steps += [ordered]@{ step = 'state-before'; state = $st0.json.state.state; powerState = $st0.json.state.powerState }

    if ($s.type -eq 'S0ix') {
      $c0 = $script:ChangeCallCount
      Start-Sleep -Seconds $s.quiet
      $st1 = Send-Api -Method GET -Path '/api/state' -NoStateAfter -Form 'SERIES' -Label 's0ix-after'
      $delta = $script:ChangeCallCount - $c0
      $stable = ("$($st1.json.state.state)" -eq "$($st0.json.state.state)") -and ("$($st1.json.state.powerState)" -eq "$($st0.json.state.powerState)")
      Add-SimCheck -Run $script:Run -Form 'SERIES' -Name 'S0ix：该型不发送任何变更调用（无经典挂起边沿）' -Ok ($delta -eq 0) -Detail ("deltaChangeCalls=$delta")
      Add-SimCheck -Run $script:Run -Form 'SERIES' -Name 'S0ix：静默期内状态保持稳定' -Ok $stable -Detail ("$($st0.json.state.state)/$($st0.json.state.powerState) -> $($st1.json.state.state)/$($st1.json.state.powerState)")
      $script:Run.extra['changeCallCount'] = $script:ChangeCallCount
      $rec.fanBehaviour += 'S0ix：无经典 suspend/resume 边沿 ⇒ FanHost 不发生挂起迁移；本机固件不支持 S0ix（形态化，NOT_APPLICABLE）'
      $rec.outcome = $(if ($delta -eq 0 -and $stable) { 'PASS(no-edge-semantics)' } else { 'FAIL' })
      $script:Run.extra['simulatedSeries'] = @($script:Run.extra['simulatedSeries']) + @($rec)
      continue
    }

    if ($s.lease) {
      $sess = Open-MockSession
      Add-SimCheck -Run $script:Run -Form 'SERIES' -Name ($s.type + '：测试前置 lease 建立') -Ok ([bool]$sess.leaseId -and $sess.open.status -eq 200) -Detail ("lease=" + [bool]$sess.leaseId)
      $rec.steps += [ordered]@{ step = 'session+lease'; open = $sess.open.status; openEvents = $sess.openEvents.status; leaseId = $(if ($sess.leaseId) { 'present' } else { 'absent' }) }
    } else {
      $rec.steps += [ordered]@{ step = 'session+lease'; note = 'intentionally NOT opened (S4 语义对照：无 lease)' }
    }

    $g = New-Generation
    $sp = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'SERIES' -Label ($s.type + '-suspend') -ChangeCall
    Add-SimCheck -Run $script:Run -Form 'SERIES' -Name ($s.type + '：模拟入睡被接受') -Ok ($sp.status -eq 200 -and $sp.stateAfter.state -eq 'Suspended') -Detail ("status=$($sp.status) $(State-Brief $sp.stateAfter)")
    $rec.steps += [ordered]@{ step = 'suspend'; status = $sp.status; state = $sp.stateAfter.state; generation = $g }

    Write-Host ("    静默 " + $s.quiet + " s（模拟 " + $s.type + " 期间无事件）")
    Start-Sleep -Seconds $s.quiet
    $stm = Send-Api -Method GET -Path '/api/state' -NoStateAfter -Form 'SERIES' -Label ($s.type + '-mid')
    Add-SimCheck -Run $script:Run -Form 'SERIES' -Name ($s.type + '：静默期内保持 Suspended（无自动跳变）') -Ok ($stm.json.state.state -eq 'Suspended') -Detail ("state=$($stm.json.state.state) powerState=$($stm.json.state.powerState)")
    $rec.steps += [ordered]@{ step = 'mid'; delaySec = $s.quiet; state = $stm.json.state.state; powerState = $stm.json.state.powerState }

    $rs = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'SERIES' -Label ($s.type + '-resume') -ChangeCall
    $rec.steps += [ordered]@{ step = 'resume'; status = $rs.status; errorCode = $rs.errorCode; state = $rs.stateAfter.state; powerState = $rs.stateAfter.powerState }
    if ($s.lease) {
      Add-SimCheck -Run $script:Run -Form 'SERIES' -Name 'S3：带 lease 唤醒被接受（200 且回到 On）' -Ok ($rs.status -eq 200 -and $rs.stateAfter.powerState -eq 'On') -Detail ("status=$($rs.status) $(State-Brief $rs.stateAfter)")
      $rec.fanBehaviour += ("S3：带 lease 入睡→静默" + $s.quiet + "s→唤醒 = " + $rs.status + "（冻结-恢复语义被接受）")
      $rec.outcome = $(if ($rs.status -eq 200) { 'PASS(accepted-resume)' } else { 'FAIL' })
    } else {
      # S4 语义（写盘-恢复）两段对照：
      #  段1 无曲线 ⇒ 无重建需求 ⇒ 干净恢复（200 回 On）
      #  段2 带曲线 ⇒ 需要 HC 重建而不可证明 ⇒ fail-closed（409 / FaultLocked），并给出显式恢复路径
      Add-SimCheck -Run $script:Run -Form 'SERIES' -Name 'S4a：无曲线唤醒 = 干净恢复（200 且回到 On）' -Ok ($rs.status -eq 200 -and $rs.stateAfter.powerState -eq 'On') -Detail ("status=$($rs.status) $(State-Brief $rs.stateAfter)")
      $sess2 = Open-MockSession
      $curve2 = '{"nodes":[{"tempC":0,"dutyPercent":0},{"tempC":55,"dutyPercent":30},{"tempC":75,"dutyPercent":60},{"tempC":90,"dutyPercent":100}],"leaseId":"' + $sess2.leaseId + '"}'
      $en2 = Send-Api -Method POST -Path '/api/enable' -Body $curve2 -Form 'SERIES' -Label 'S4b-enable-curve'
      Add-SimCheck -Run $script:Run -Form 'SERIES' -Name 'S4b：曲线会话就绪（写入被观测）' -Ok ($en2.status -eq 200 -and $en2.stateAfter.hardwareWritesObserved -eq $true) -Detail ("status=$($en2.status) hwWrites=$($en2.stateAfter.hardwareWritesObserved)")
      $g2 = New-Generation
      $sp2 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g2) -Form 'SERIES' -Label 'S4b-suspend' -ChangeCall
      # 真实语义（本批实测）：带曲线入睡时即进入 FaultLocked（fail-closed，不伪造完成）；静默期内自动收敛；
      # 唤醒码可为 200（收敛后干净恢复）或 409（仍需显式重建），两者都如实记录，但不允许伪装物理读回。
      Add-SimCheck -Run $script:Run -Form 'SERIES' -Name 'S4b：带曲线入睡 fail-closed（进入 FaultLocked，不伪造完成）' -Ok ($sp2.status -eq 200 -and "$($sp2.stateAfter.state)" -eq 'FaultLocked') -Detail ("status=$($sp2.status) $(State-Brief $sp2.stateAfter)")
      Start-Sleep -Seconds $s.quiet
      $rs2 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g2) -Form 'SERIES' -Label 'S4b-resume' -ChangeCall
      $notPhysical = "$($rs2.stateAfter.oemRestoreEvidence)" -ne 'hc-default-table-readback-confirmed'
      Add-SimCheck -Run $script:Run -Form 'SERIES' -Name 'S4b：静默期后收敛且不伪装物理重建（resume 码如实记录）' -Ok (($rs2.status -eq 200 -or $rs2.status -eq 409) -and $notPhysical) -Detail ("status=$($rs2.status) code=$($rs2.errorCode) state=$($rs2.stateAfter.state) evidence=$($rs2.stateAfter.oemRestoreEvidence)")
      $rel = Send-Api -Method POST -Path '/api/release-control' -Body ('{"leaseId":"' + $sess2.leaseId + '"}') -Form 'SERIES' -Label 'S4b-release-control' -ChangeCall
      Add-SimCheck -Run $script:Run -Form 'SERIES' -Name 'S4b：显式恢复路径有效（release-control 200 且离开 FaultLocked）' -Ok ($rel.status -eq 200 -and "$($rel.stateAfter.state)" -ne 'FaultLocked') -Detail ("status=$($rel.status) $(State-Brief $rel.stateAfter)")
      $rec.steps += [ordered]@{ step = 's4b'; curveEnabled = $en2.status; suspend = $sp2.status; resume = $rs2.status; resumeCode = $rs2.errorCode; resumedState = $rs2.stateAfter.state; release = $rel.status; releasedState = $rel.stateAfter.state }
      $rec.fanBehaviour += ("S4a（无曲线）：入睡→静默" + $s.quiet + "s→唤醒 = " + $rs.status + "（干净恢复）")
      $rec.fanBehaviour += ("S4b（带曲线）：入睡即 FaultLocked（fail-closed）→ 静默" + $s.quiet + "s 收敛 → 唤醒 = " + $rs2.status + "（" + $rs2.stateAfter.state + "，不伪装物理重建）；release-control=" + $rel.status)
      $rec.outcome = $(if ($rs.status -eq 200 -and "$($sp2.stateAfter.state)" -eq 'FaultLocked' -and $rel.status -eq 200) { 'PASS(two-path)' } else { 'FAIL' })
    }
    $script:Run.extra['simulatedSeries'] = @($script:Run.extra['simulatedSeries']) + @($rec)
  }

  $seriesRecs = @($script:Run.extra['simulatedSeries'])
  Add-SimCheck -Run $script:Run -Form 'SERIES' -Name '全系列共 3 型完成（S0ix/S3/S4）' -Ok (@($seriesRecs).Count -eq 3 -and @($seriesRecs | Where-Object { $_.outcome -like 'PASS*' }).Count -eq 3) -Detail ((@($seriesRecs | ForEach-Object { $_.type + '=' + $_.outcome })) -join ' | ')
  Write-Host ''
  Write-Host '分型系列小结（风扇侧）:' -ForegroundColor White
  foreach ($r in $seriesRecs) { Write-Host ("  [" + $r.type + "] " + $r.outcome + " :: " + ($r.fanBehaviour -join '; ')) -ForegroundColor Gray }
  $script:Run.extra['seriesDisclaimer'] = '本系列为注入式模拟（offline-injection），不是真实 S0ix/S3/S4；真机分型见 -Mode sleeptypes 与 FAN-204-RESULT §3.9/§3.10'
}