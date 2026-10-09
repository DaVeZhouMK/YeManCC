# lib_sleep_typing.ps1 - S0/S3/S4 分型判型器（纯函数 + 离线 golden fixtures + -Mode sleeptypes）
#
# 2026-09-22 (batch 204-F)。判型只依据可观测事实；证据不足一律 UNKNOWN（不猜、不填最可能值）。
# 判据与来源见同目录 sleep_types.json（含 URL 与证据等级）。
#
# 用法（由 sleep_domain.ps1 点源）：
#   Invoke-SleepTypes           离线：本机能力 + fixtures 断言
#   判型器纯函数 Get-SleepTypeVerdict -Facts <hashtable>

function Get-SleepPlatformCapability {
  <#
    只解析 powercfg /a 的 "available" 段（英文 / 中文双语），避免把 "not available" 段里的名称当成支持项。
    返回 @{ s0ix; s3; s4; availableText }
  #>
  param([string]$Text)
  $out = [ordered]@{ s0ix = $false; s3 = $false; s4 = $false; availableText = '' }
  if (-not $Text) { return $out }
  $lines = @($Text -split "`r?`n")
  $inAvail = $false; $avail = New-Object System.Collections.Generic.List[string]
  foreach ($ln in $lines) {
    $l = $ln.Trim()
    if ($l -match '(states are available on this system|此系统上有以下睡眠状态)') { $inAvail = $true; continue }
    if ($l -match '(states are not available on this system|此系统上没有以下睡眠状态)') { $inAvail = $false; continue }
    if ($inAvail) { $avail.Add($ln) }
  }
  # 无标题行时退化为全文（但排除 not available 段）
  if ($avail.Count -eq 0) {
    $cut = $Text.Length
    foreach ($m in '(states are not available on this system|此系统上没有以下睡眠状态)') { $i = $Text.IndexOf($m); if ($i -ge 0 -and $i -lt $cut) { $cut = $i } }
    $avail.Add($Text.Substring(0, $cut))
  }
  $out.availableText = ($avail -join "`n")
  $out.s0ix = $out.availableText -match '(S0 Low Power Idle|S0 低电量待机)'
  $out.s3 = $out.availableText -match '(Standby \(S3\)|待机 \(S3\))'
  $out.s4 = $out.availableText -match '(Hibernate|休眠)'
  return $out
}
function Test-SleepFactKey {
  # hashtable 与 PSCustomObject 都支持（fixtures 是 JSON ⇒ PSCustomObject）
  param($Facts, [string]$Key)
  if (-not $Facts) { return $false }
  if ($Facts -is [hashtable]) { return ($Facts.ContainsKey($Key) -and $null -ne $Facts[$Key]) }
  $prop = $Facts.PSObject.Properties[$Key]
  return ($null -ne $prop -and $null -ne $prop.Value)
}
function Get-SleepTypeVerdict {
  <#
    Facts 允许的键（都可缺省；缺省即"无此证据"）：
      pcfgA            : powercfg /a 的文本
      events42         : 数组，每项 @{ targetState=3|4; effectiveState=3|4; sleepReason='...' }
      events107        : 数组，每项 @{ targetState=3|4; wakeFromState=3|4 }
      sessions         : 数组，每项 @{ id=506|507|566; ... }（modern standby 会话族）
      hiberfilExists   : $true/$false（窗口后）
      hiberfilUpdated  : $true/$false（窗口内 mtime 变化）
      gapSeconds       : 挂起间隙
      requested        : 's3' | 's4' | 'auto'
      productFacts     : 数组（sleep-facts 行文本，仅作旁证，不参与数值等价）
    返回 @{ type; confidence; evidence=@(); rationale=@(); downgrade=$false }
  #>
  param([object]$Facts)
  $out = [ordered]@{ type = 'UNKNOWN'; confidence = 'none'; evidence = @(); rationale = @(); downgrade = $false }
  $has = { param($k) Test-SleepFactKey -Facts $Facts -Key $k }

  # 1) 平台互斥：powercfg /a 明确列出 S0ix ⇒ 该机为 Modern Standby 型（S3 不可用）
  if (& $has 'pcfgA') {
    $a = "$($Facts.pcfgA)"
    $cap = Get-SleepPlatformCapability -Text $a
    $s0 = $cap.s0ix
    $s3 = $cap.s3
    $out.rationale += ("powercfg(available 段): s0ix={0} s3={1} s4={2}" -f $s0, $s3, $cap.s4)
    if ($s0) {
      if (& $has 'sessions' -and @($Facts.sessions).Count -gt 0) {
        $disconnected = @($Facts.sessions | Where-Object { "$($_.text)" -match 'Network Disconnected' }).Count -gt 0
        $out.type = $(if ($disconnected) { 'S0ix-network-disconnected' } else { 'S0ix' })
        $out.confidence = 'high'
        $out.evidence += 'pcfgA:S0LowPowerIdle'
        $out.evidence += ('sessions=' + (@($Facts.sessions | ForEach-Object { $_.id }) -join ','))
      } else {
        $out.type = 'S0ix'; $out.confidence = 'medium'
        $out.evidence += 'pcfgA:S0LowPowerIdle(no session events seen)'
      }
      return $out
    }
  }

  # 2) Kernel-Power 42 的目标态是 S3/S4 的主判据
  $t42 = $null
  if (& $has 'events42' -and @($Facts.events42).Count -gt 0) {
    foreach ($e in @($Facts.events42)) {
      foreach ($k in 'targetState', 'effectiveState') {
        $pv = $e.PSObject.Properties[$k]
        if ($null -ne $pv -and $null -ne $pv.Value) { $t42 = [int]$pv.Value; break }
      }
      if ($null -ne $t42) { break }
    }
    if ($null -ne $t42) {
      $out.evidence += ("42.targetState/effectiveState=$t42")
      $out.rationale += 'Kernel-Power 42 目标态'
    }
  }

  # 3) hiberfil 证据（S4 的必要条件之一）
  $hib = $null
  if (& $has 'hiberfilExists') { $hib = [bool]$Facts.hiberfilExists; $out.evidence += ("hiberfil.exists=$hib") }
  if (& $has 'hiberfilUpdated') { $out.evidence += ("hiberfil.updatedInWindow=" + [bool]$Facts.hiberfilUpdated) }

  $req = $(if (& $has 'requested') { "$($Facts.requested)".ToLowerInvariant() } else { 'auto' })
  $out.evidence += ("requested=$req")
  if (& $has 'gapSeconds') { $out.evidence += ("gapSeconds=$($Facts.gapSeconds)") }

  if ($null -ne $t42) {
    if ($t42 -eq 4) {
      $out.type = 'S4'; $out.confidence = $(if ($hib -eq $false) { 'medium' } else { 'high' })
      if ($hib -eq $false) { $out.rationale += '目标态=4 但 hiberfil 不存在（可能已被清理）' }
      return $out
    }
    if ($t42 -eq 3) {
      if ($req -eq 's4') {
        # operator 口径（2026-09-22）：微软侧已知 S4 可能被**错误表达**为 S3 —— 以"是否真的写盘"分流：
        #   hiberfil 在本窗口被写入且间隙足够长 ⇒ 实际按 S4 执行，只是被表达为 S3（误表达）
        #   未写盘 ⇒ 请求未被执行为 S4（降级）
        $hibUpd = (& $has 'hiberfilUpdated') -and [bool]$Facts.hiberfilUpdated
        $gapBig = (& $has 'gapSeconds') -and [double]$Facts.gapSeconds -ge 20
        if ($hibUpd -and $gapBig) {
          $out.type = 'S4_MISLABELED_AS_S3'; $out.confidence = 'high'; $out.downgrade = $false
          $out.evidence += 'hiberfil-updated-in-window'
          $out.evidence += ("gap=$($Facts.gapSeconds)s")
          $out.rationale += '事件/状态表达为 S3，但 hiberfil 在本窗口被写入且间隙足够长 ⇒ 实际按 S4 执行（微软侧已知的误表达）'
          return $out
        }
        $out.type = 'S4_DOWNGRADED_TO_S3'; $out.confidence = 'high'; $out.downgrade = $true
        $out.rationale += '请求 s4 但 42 目标态=3（且未观察到 hiberfil 更新）⇒ 判定降级'
        return $out
      }
      $out.type = 'S3'; $out.confidence = 'high'
      return $out
    }
    $out.rationale += ("42 目标态=$t42 不属于已登记形态 ⇒ 保守判 UNKNOWN")
    return $out
  }

  # 3.5) 无 42 证据时的保守判型（不猜）：平台能力 + 请求类型 + 秒级间隙 + 旁证 lifecycle，三者齐备才判 S3(medium)
  if ($null -eq $t42 -and $req -eq 's3') {
    $pcfgText = "$($Facts.pcfgA)"
    $capForRule = Get-SleepPlatformCapability -Text $pcfgText
    $s3Capable = (& $has 'pcfgA') -and $capForRule.s3 -and (-not $capForRule.s0ix)
    $gapPlausible = (& $has 'gapSeconds') -and [double]$Facts.gapSeconds -ge 2 -and [double]$Facts.gapSeconds -le 600
    $lifecycleSeen = $false
    if (& $has 'productFacts') { $lifecycleSeen = @($Facts.productFacts | Where-Object { "$_" -match 'suspended|resuming' }).Count -gt 0 }
    if ($s3Capable -and $gapPlausible -and $lifecycleSeen) {
      $out.type = 'S3'; $out.confidence = 'medium'
      $out.evidence += 'pcfg:S3-capable+no-S0ix'
      $out.evidence += 'gap-seconds-plausible'
      $out.evidence += 'product-lifecycle:suspended/resuming'
      $out.rationale += '无 42（事件日志不可读）故 confidence=medium；读到 42 目标态是升级为 high 的唯一路径'
      return $out
    }
  }
  # 3.7) 请求 s4 且无 42 证据：以写盘 + 间隙为硬证据分流（禁止再触发的环境下，未确认就如实记未确认）
  if ($req -eq 's4' -and $null -eq $t42) {
    $hibUpd37 = (& $has 'hiberfilUpdated') -and [bool]$Facts.hiberfilUpdated
    $gapOk37 = (& $has 'gapSeconds') -and [double]$Facts.gapSeconds -ge 8
    if ($hibUpd37 -and $gapOk37) {
      $out.type = 'S4'; $out.confidence = 'medium'
      $out.evidence += 'hiberfil-updated-in-window'
      $out.evidence += ("gap=$($Facts.gapSeconds)s")
      $out.rationale += '无 42 证据时以写盘+间隙判 S4(medium)；若事件/状态表达为 S3，属 operator 口径下的误表达'
      return $out
    }
    if ((-not $hibUpd37) -and (-not $gapOk37)) {
      $out.type = 'S4_NOT_CONFIRMED'; $out.confidence = 'low'; $out.downgrade = $false
      $out.rationale += '请求了 s4，但既无 hiberfil 写盘、也无系统挂起间隙 ⇒ 未确认（不判降级、不判 S4）'
      return $out
    }
  }
  # 4) 无 42：若出现 modern standby 会话族则判 S0ix（低置信）
  if (& $has 'sessions' -and @($Facts.sessions).Count -gt 0) {
    $out.type = 'S0ix'; $out.confidence = 'low'
    $out.evidence += ('sessions=' + (@($Facts.sessions | ForEach-Object { $_.id }) -join ','))
    $out.rationale += '仅见会话族事件、无 42/107 ⇒ 低置信 S0ix'
    return $out
  }

  $out.rationale += '证据不足：不猜'
  return $out
}

function Test-SleepTypeFixture {
  param($Fixture)
  $dec = Get-SleepTypeVerdict -Facts $Fixture.facts
  $exp = $Fixture.expect
  $fails = @()
  foreach ($k in @($exp.PSObject.Properties.Name)) {
    $want = $exp.$k; $got = $dec[$k]
    if ("$want" -ne "$got") { $fails += ("{0} want='{1}' got='{2}'" -f $k, $want, $got) }
  }
  return [ordered]@{ name = $Fixture.name; verdict = $dec; fails = $fails }
}

function Invoke-SleepTypes {
  Write-Host '=== S0/S3/S4 分型（本机能力 + 判据 + 离线 golden fixtures）===' -ForegroundColor White
  $typesPath = Join-Path $scriptDir 'sleep_types.json'
  $fixturesDir = Join-Path $scriptDir 'fixtures'
  $types = $null
  if (Test-Path -LiteralPath $typesPath) { $types = Get-Content -LiteralPath $typesPath -Raw -Encoding UTF8 | ConvertFrom-Json }
  Add-SimCheck -Run $script:Run -Form 'TYPES' -Name '类型定义文件存在且可解析' ($null -ne $types -and @($types.types).Count -ge 3) ("types=" + $(if ($types) { @($types.types | ForEach-Object { $_.type }) -join ',' } else { 'missing' }))
  $script:Run.extra['sleepTypes'] = $types

  # 本机能力（field-observed）
  $pcfg = ((& powercfg /a 2>&1) -join "`n")
  $script:Run.extra['powercfgA'] = $pcfg
  $cap = Get-SleepPlatformCapability -Text $pcfg
  $script:Run.extra['platformCapability'] = $cap
  $capS0 = $cap.s0ix
  $capS3 = $cap.s3
  $capS4 = $cap.s4
  Add-SimCheck -Run $script:Run -Form 'TYPES' -Name '本机能力与登记一致（S3 可用 / S0ix 不可用 / 休眠可用）' ($capS3 -and (-not $capS0) -and $capS4) ("s0ix={0} s3={1} hibernate={2}" -f $capS0, $capS3, $capS4)
  Add-SimStatus -Run $script:Run -Scope 'S0ix 真机分型' -Status 'NOT_APPLICABLE' -Detail '9950 固件不支持 S0 Low Power Idle ⇒ 该形态只能离线分型（DEVICE_PENDING）' -Required $false

  # 真机证据判型（复用已产出的 S3 周期证据）
  $s3Evidence = 'g:\YeManCC-Work\_scratch\Repair202GateA-20260922-075435\window-s3-20260922\s3-result-20260922-230911.json'
  if (Test-Path -LiteralPath $s3Evidence) {
    $ev = Get-Content -LiteralPath $s3Evidence -Raw -Encoding UTF8 | ConvertFrom-Json
    $cycles = @($ev.cycles)
    $facts42 = @()
    foreach ($c in $cycles) {
      foreach ($ln in @($c.sleepFactsDelta)) {
        if ("$ln" -match '"event":"kernel-power"') {
          $t = $null
          if ("$ln" -match '"targetState":(\d+)') { $t = [int]$Matches[1] }
          $facts42 += [ordered]@{ targetState = $t; raw = ("$ln").Substring(0, [Math]::Min(200, ("$ln").Length)) }
        }
      }
    }
    $gapMax = 0
    foreach ($c in $cycles) { if ($c.gapSeconds -and [double]$c.gapSeconds -gt $gapMax) { $gapMax = [double]$c.gapSeconds } }
    $allDelta = @()
    foreach ($c in $cycles) { $allDelta += @($c.sleepFactsDelta) }
    $script:Run.extra['kernelPowerRows'] = @($facts42 | ForEach-Object { $_.raw })
    $f = @{ pcfgA = $pcfg; requested = 's3'; gapSeconds = $gapMax; productFacts = $allDelta }
    $v = Get-SleepTypeVerdict -Facts $f
    $script:Run.extra['s3TypeVerdict'] = $v
    Add-SimCheck -Run $script:Run -Form 'TYPES' -Name '真 S3 证据判型 = S3' ($v.type -eq 'S3') ("type={0} conf={1} evidence={2}" -f $v.type, $v.confidence, ($v.evidence -join ' '))
    Add-SimCheck -Run $script:Run -Form 'TYPES' -Name '产品 sleep-facts 的 kernel-power 字段已采集（原值保留，不做跨来源等价假设）' (@($facts42).Count -gt 0) ("rows=" + @($facts42).Count + " raw targetState=" + ((@($facts42 | ForEach-Object { $_.targetState }) -join ',')))
  } else {
    Add-SimCheck -Run $script:Run -Form 'TYPES' -Name '真 S3 证据文件存在（用于判型）' $false $s3Evidence
  }

  # 离线 golden fixtures
  $files = @(Get-ChildItem -LiteralPath $fixturesDir -Filter 'sleeptype-*.json' -ErrorAction SilentlyContinue | Sort-Object Name)
  Add-SimCheck -Run $script:Run -Form 'TYPES' -Name '分型 fixtures 集存在（>=5）' ($files.Count -ge 5) ("count=" + $files.Count)
  foreach ($fx in $files) {
    $obj = Get-Content -LiteralPath $fx.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    $r = Test-SleepTypeFixture -Fixture $obj
    Add-SimCheck -Run $script:Run -Form 'TYPES' -Name ("fixture " + $obj.name) ($r.fails.Count -eq 0) ($(if ($r.fails.Count) { $r.fails -join '; ' } else { "type={0} conf={1}" -f $r.verdict.type, $r.verdict.confidence }))
    $script:Run.extra['typeFixtures'] = @($script:Run.extra['typeFixtures']) + @([ordered]@{ name = $obj.name; type = $r.verdict.type; confidence = $r.verdict.confidence; downgrade = $r.verdict.downgrade })
  }
}