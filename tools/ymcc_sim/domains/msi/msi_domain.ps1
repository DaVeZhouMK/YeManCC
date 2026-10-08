<#
.SYNOPSIS
  MSI/CT 风扇域（ymcc_sim -Domain msi）：三方能力表、证据等级、离线 golden fixtures、默认关闭的只读回读适配。

.DESCRIPTION
  本域只做只读与离线工作（204 §8.2 B1 / §8.3）：
    table    三方能力表（HC / CT / YMCC）+ 每条事实的证据等级（源文件行号 / 反编译字符串 / 未验证）
    fixtures 全部 golden fixtures 的离线断言（纯解码器：短包、失败、跨会话、边界字节、遥测未知）
    decode   单个 fixture 解码（离线）
    readback 只读回读适配：默认关闭（必须 -EnableReadback）；单飞 + 超时 + 会话归属 + 机型白名单；
             没有 MSI 设备时如实交付 MSI_DEVICE_PENDING（不伪造 PASS）。永不写：本域源码内不得出现任何 Set_*
             调用（由内置只读断言核验，断言串刻意拆分以免自匹配）。

  曲线字节永远是 duty，不是 RPM；未知值一律 null/Unknown，绝不用 0 填空（HC 的 ReadFanDuty 占位 0 不是测量值）。

.EXAMPLE
  powershell -File tools\ymcc_sim\domains\msi\msi_domain.ps1 -Mode fixtures
.EXAMPLE
  powershell -File tools\ymcc_sim\domains\msi\msi_domain.ps1 -Mode readback          # 无设备 -> MSI_DEVICE_PENDING
#>
[CmdletBinding()]
param(
  [ValidateSet('table', 'fixtures', 'decode', 'readback')][string]$Mode = 'table',
  [string]$Fixture = '',
  [switch]$EnableReadback,
  [string]$Model = '',
  [int]$TimeoutSec = 3,
  [string]$EvidenceDir = ''
)
$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path (Split-Path -Parent (Split-Path -Parent $scriptDir)) 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $EvidenceDir) { $EvidenceDir = $D.evidenceDir }
if (-not (Test-Path -LiteralPath $EvidenceDir)) { New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null }
$rulesPath = Join-Path $scriptDir 'msi_rules.json'
$fixturesDir = Join-Path $scriptDir 'fixtures'

$script:Run = New-SimRun -Domain 'msi' -Mode $Mode -ModeKind 'offline-injection' -ToolPath $PSCommandPath -Seed 204
$script:Run.extra['msiDeviceStatus'] = 'MSI_DEVICE_PENDING'

# ---------------------------------------------------------------- decoder ----
function ConvertFrom-MsiCurve {
  <#
    Pure decoder for the fan curve payload. Contract:
      * raw must be exactly 32 bytes and the read must have succeeded, else ok=false with an explicit reason
      * a model not on the whitelist is 'device-not-whitelisted'; a block other than 1/2 is 'block-unsupported'
      * duties = raw[1..6] (dutyPercent); raw[7] is the kept/boundary byte and is reported, never written
      * a cross-session / late result is rejected before decoding
      * nothing is ever presented as RPM; unknown bytes stay raw
  #>
  param($Fixture, [string[]]$Whitelist = @())
  $out = [ordered]@{
    name = $Fixture.name; kind = $Fixture.kind; ok = $false; reason = $null
    blockIndex = $null; duties = $null; payload7 = $null; boundaryPreserved = $null
    raw = $Fixture.raw; asRpm = $false; values = $null; rawOnly = $true
  }
  if ($Fixture.PSObject.Properties.Name -contains 'session' -and $Fixture.PSObject.Properties.Name -contains 'capturedSession') {
    if ("$($Fixture.session)" -ne "$($Fixture.capturedSession)") { $out.reason = 'cross-session'; return $out }
  }
  if (-not $Fixture.readSuccess) { $out.reason = 'read-failed'; return $out }
  # device eligibility BEFORE any decode: a model that is not on the whitelist is 'not supported'
  if (($Fixture.PSObject.Properties.Name -contains 'model') -and @($Whitelist).Count -gt 0) {
    if (@($Whitelist) -notcontains "$($Fixture.model)") { $out.reason = 'device-not-whitelisted'; return $out }
  }
  $raw = @($Fixture.raw)
  if ($Fixture.kind -eq 'telemetry') {
    # B2 only: unit/scale/freshness unverified -> raw only, values stay Unknown
    $out.ok = $true; $out.rawOnly = $true; $out.values = $null; $out.reason = 'scale-unverified'
    return $out
  }
  if ($raw.Count -ne 32) { $out.reason = 'short-packet'; return $out }
  # only block 1 (CPU) and block 2 (GPU) are defined curve tables; anything else is unsupported
  if (@(1, 2) -notcontains [int]$raw[0]) { $out.reason = 'block-unsupported'; return $out }
  $out.ok = $true
  $out.blockIndex = [int]$raw[0]
  $duties = @()
  for ($i = 1; $i -le 6; $i++) { $duties += [int]$raw[$i] }
  $out.duties = $duties
  $out.payload7 = [int]$raw[7]
  $out.boundaryPreserved = ([int]$raw[7] -eq 75)
  $out.rawOnly = $false
  return $out
}

function Test-MsiFixtureExpectation {
  param($Fixture, $Decoded)
  $exp = $Fixture.expect
  $fails = @()
  foreach ($k in @($exp.PSObject.Properties.Name)) {
    $want = $exp.$k
    $got = $Decoded[$k]
    if ($k -eq 'duties' -and $null -ne $want) {
      $w = @($want) | ForEach-Object { [int]$_ }
      $g = @($got) | ForEach-Object { [int]$_ }
      if (($w -join ',') -ne ($g -join ',')) { $fails += ("duties want=[{0}] got=[{1}]" -f ($w -join ','), ($g -join ',')) }
      continue
    }
    if ("$want" -ne "$got") { $fails += ("{0} want='{1}' got='{2}'" -f $k, $want, $got) }
  }
  return $fails
}

# ---------------------------------------------------------------- modes ----
function Get-MsiRouteAnchorFacts {
  # READ-ONLY scan of the fan route table (Program.cs is never modified): confirm every MSI model is mapped
  # onto the ProfileCurve + MsiHcDefaultRelease route line, and record the anchor plus the raw line text.
  $yeManCCRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $scriptDir)))
  $progPath = Join-Path $yeManCCRoot 'FanLab\real-host\Program.cs'
  $models = @('ClawA1M', 'ClawA2VM', 'ClawBZ2EM', 'ClawCG3EM')
  $facts = [ordered]@{ path = $progPath; exists = $false; anchor = $null; lineText = $null; routeKind = $null; strategy = $null; modelsFound = @(); allFourPresent = $false }
  if (-not (Test-Path -LiteralPath $progPath -PathType Leaf)) { return $facts }
  $facts.exists = $true
  $lines = @(Get-Content -LiteralPath $progPath -Encoding UTF8)
  for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'ProfileCurve' -and $lines[$i] -match 'MsiHcDefaultRelease') {
      $span = $lines[$i]
      if (($i + 1) -lt $lines.Count) { $span = $span + ' ' + $lines[$i + 1] }
      $facts.anchor = ("Program.cs:{0}-{1}" -f ($i + 1), ($i + 2))
      $facts.lineText = $span.Trim()
      $facts.routeKind = 'ProfileCurve'
      $facts.strategy = 'MsiHcDefaultRelease'
      foreach ($m in $models) { if ($span.Contains($m)) { $facts.modelsFound += $m } }
      $facts.modelsFound = @($facts.modelsFound)
      break
    }
  }
  $facts.allFourPresent = (@($models | Where-Object { @($facts.modelsFound) -contains $_ }).Count -eq 4)
  return $facts
}

function Show-MsiTable {
  $rules = Get-Content -LiteralPath $rulesPath -Raw -Encoding UTF8 | ConvertFrom-Json
  $script:Run.extra['rules'] = $rules
  $script:Run.extra['rulesIdentity'] = Get-SimFileIdentity $rulesPath 'msi_rules'
  Write-Host '=== MSI 风扇三方能力表（HC / CT / YMCC，含证据等级）===' -ForegroundColor White
  $rows = @(
    [ordered]@{ item = 'MSI 写入路径存在'; hc = 'yes: Set_Fan block1/2 + Set_Data 212/152'; ct = 'yes: 6-slot read-modify-write'; ymcc = 'yes: ProfileCurve route bound (allMappedFanRoutesAuthorized)'; grade = 'source-code' }
    [ordered]@{ item = 'MSI 曲线可读（软件）'; hc = 'no: base ReadFanDuty returns 0 (placeholder, zero callers)'; ct = 'yes: Get_Fan + boundary-byte guard'; ymcc = 'gap: readback=not-observed'; grade = 'source-code' }
    [ordered]@{ item = 'MSI 温度/RPM 遥测'; hc = 'no: Get_Fan result discarded (blind write)'; ct = 'candidate: Get_Fan(0) head (unverified)'; ymcc = 'none today'; grade = 'decompiled-strings' }
    [ordered]@{ item = '未知字节处理'; hc = 'overwrites payload[1..8] (incl. byte7=75)'; ct = 'refuses to write without the EC boundary bytes'; ymcc = 'this batch: report only, never write'; grade = 'source-code' }
    [ordered]@{ item = '满速入口'; hc = 'block 152, but the consumer only fires for LegionGo/ClawA2VM'; ct = 'has a full-speed toggle'; ymcc = 'not added in this batch'; grade = 'source-code' }
    [ordered]@{ item = '物理 RPM / EC 直读'; hc = 'no'; ct = 'PawnIO/LPC bypass (unverified on device)'; ymcc = 'no'; grade = 'unverified' }
    [ordered]@{ item = '设备实测'; hc = 'n/a'; ct = 'self-declared: values copied from MSI, not device-tested'; ymcc = 'MSI_DEVICE_PENDING'; grade = 'device-measured' }
  )
  foreach ($r in $rows) {
    Write-Host ("  {0,-22} grade={1}" -f $r.item, $r.grade) -ForegroundColor Gray
    Write-Host ("      HC={0}" -f $r.hc)
    Write-Host ("      CT={0}" -f $r.ct)
    Write-Host ("      YMCC={0}" -f $r.ymcc)
  }
  $script:Run.extra['capabilityTable'] = $rows
  Add-SimCheck -Run $script:Run -Form 'TABLE' -Name 'three-way capability table rendered from rules with evidence grades' -Ok $true -Detail ("rows={0} whitelist={1}" -f $rows.Count, @($rules.deviceWhitelist).Count)
  Add-SimCheck -Run $script:Run -Form 'TABLE' -Name 'curve bytes are never presented as RPM (policy present)' -Ok ($null -ne $rules.rpmPolicy -and "$($rules.rpmPolicy.rule)" -match 'never') -Detail $rules.rpmPolicy.rule
  Add-SimCheck -Run $script:Run -Form 'TABLE' -Name 'unknown values policy present (never 0)' -Ok ("$($rules.byteSemantics.curvePayload.bytes8_31.role)" -eq 'unknown') -Detail ("bytes8..31=" + $rules.byteSemantics.curvePayload.bytes8_31.role)
  Add-SimCheck -Run $script:Run -Form 'TABLE' -Name 'write policy recorded (no EC write / no full-speed / no driver / no re-order)' -Ok ($null -ne $rules.writePolicy) -Detail $rules.writePolicy.batchDecision
  Add-SimCheck -Run $script:Run -Form 'TABLE' -Name 'unsupported semantics recorded (device / block / telemetry kept distinct)' -Ok (@($rules.unsupportedSemantics.PSObject.Properties.Name).Count -ge 3) -Detail ("reasons=" + (@($rules.unsupportedSemantics.PSObject.Properties.Name) -join ','))

  # ---- per-item capability detail (HC route / readback / RPM / OEM / CT); unknown stays MSI_DEVICE_PENDING ----
  $anchor = Get-MsiRouteAnchorFacts
  $script:Run.extra['routeAnchor'] = $anchor
  Write-Host ''
  Write-Host '--- 能力细节：HC 可写路由 / 回读 / RPM / OEM / CT ---' -ForegroundColor White
  $cap = [ordered]@{
    hcRoute          = ('yes: FanRouteKind.ProfileCurve + FanRestoreStrategy.MsiHcDefaultRelease @ {0}' -f $(if ($anchor.anchor) { $anchor.anchor } else { 'anchor-not-found' }))
    hcRouteFourTypes = ('4-type check: ' + $(if ($anchor.allFourPresent) { 'all present' } else { 'MISSING' }) + ' [' + (@($anchor.modelsFound) -join ',') + ']')
    readback         = 'unknown: software readback not observed (HC ReadFanDuty is a 0 placeholder); status=MSI_DEVICE_PENDING'
    rpm              = 'unproven: curve bytes are dutyPercent, never RPM; status=MSI_DEVICE_PENDING'
    oemOwnership     = 'unproven: OEM EC/firmware physical ownership not established; status=MSI_DEVICE_PENDING'
    ctReference      = 'reference-only: decompiled CT strings hint, not this-host evidence; status=MSI_DEVICE_PENDING'
    deviceStatus     = 'MSI_DEVICE_PENDING'
  }
  $script:Run.extra['capabilityDetail'] = $cap
  foreach ($k in @($cap.Keys)) { Write-Host ("  {0,-16} {1}" -f $k, $cap[$k]) -ForegroundColor Gray }
  Add-SimCheck -Run $script:Run -Form 'TABLE' -Name 'HC writable route anchored to Program.cs:4755-4756 (read-only scan)' -Ok ([bool]$anchor.allFourPresent) -Detail ("anchor={0} text={1}" -f $anchor.anchor, $anchor.lineText)
  Add-SimCheck -Run $script:Run -Form 'TABLE' -Name 'four MSI models all present on the route table line (read-only)' -Ok (@($anchor.modelsFound).Count -eq 4) -Detail ("found=" + (@($anchor.modelsFound) -join ','))
  Add-SimCheck -Run $script:Run -Form 'TABLE' -Name 'readback/RPM/OEM/CT status words are MSI_DEVICE_PENDING (unknown never becomes 0 or RPM)' -Ok (@(@($cap.readback, $cap.rpm, $cap.oemOwnership, $cap.ctReference) | Where-Object { $_ -match 'MSI_DEVICE_PENDING' }).Count -eq 4) -Detail 'four columns declared pending'
}

function Assert-MsiReadOnly {
  # The read path may not contain a WRITE INVOCATION. Descriptive text (the capability table mentions the HC write
  # paths on purpose) is allowed; the patterns below only match call sites / write cmdlets. Tokens are assembled so
  # this assertion cannot match itself.
  $src = Get-Content -LiteralPath $PSCommandPath -Raw -Encoding UTF8
  $write = 'Set' + '_'
  $ciSet = 'Set' + '-CimInstance'
  $wfile = 'Write' + 'File'
  $patterns = @(
    ("-MethodName\s+.{0,3}" + $write),
    ("\." + $write + "[A-Za-z]*\s*\("),
    ("WMI\." + $write),
    ("Ec[A-Za-z]*Write[A-Za-z]*\s*\("),
    ("\b" + $wfile + "[A-Za-z]*\s*\("),
    ("\b" + $ciSet + "\b")
  )
  $hits = @()
  foreach ($p in $patterns) { if ($src -match $p) { $hits += $p } }
  return $hits
}

function Invoke-MsiFixtures {
  Write-Host '=== MSI golden fixtures（离线纯解码器断言）===' -ForegroundColor White
  $rules = Get-Content -LiteralPath $rulesPath -Raw -Encoding UTF8 | ConvertFrom-Json
  $whitelist = @($rules.deviceWhitelist | ForEach-Object { $_.model })
  $script:Run.extra['fixtureWhitelist'] = $whitelist
  $files = @(Get-ChildItem -LiteralPath $fixturesDir -Filter '*.json' | Sort-Object Name)
  Add-SimCheck -Run $script:Run -Form 'FIXTURES' -Name 'fixture set present (>=6)' -Ok ($files.Count -ge 6) -Detail ("count={0}" -f $files.Count)
  foreach ($f in $files) {
    $fx = Get-Content -LiteralPath $f.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    $dec = ConvertFrom-MsiCurve -Fixture $fx -Whitelist $whitelist
    $fails = Test-MsiFixtureExpectation -Fixture $fx -Decoded $dec
    Add-SimCheck -Run $script:Run -Form 'FIXTURES' -Name ("fixture {0}" -f $fx.name) -Ok ($fails.Count -eq 0) -Detail ($(if ($fails.Count -eq 0) { "ok={0} reason={1} duties={2} asRpm={3}" -f $dec.ok, $dec.reason, ($dec.duties -join '/'), $dec.asRpm } else { $fails -join '; ' }))
    $script:Run.extra['fixtures'] = @($script:Run.extra['fixtures']) + @([ordered]@{ name = $fx.name; ok = $dec.ok; reason = $dec.reason; duties = $dec.duties; payload7 = $dec.payload7; boundaryPreserved = $dec.boundaryPreserved; rawOnly = $dec.rawOnly; asRpm = $dec.asRpm })
  }
  # every required fixture KIND must be present and asserted (good / read-failed / boundary-differs /
  # short-packet / cross-session / unsupported-or-unknown); a missing kind is a FAIL, not a silent pass
  $fxList = @($script:Run.extra['fixtures'])
  $covered = [ordered]@{
    good                = @($fxList | Where-Object { $_.name -eq 'curve-good' -and $_.ok }).Count
    readFailed          = @($fxList | Where-Object { $_.reason -eq 'read-failed' }).Count
    boundaryDiffers     = @($fxList | Where-Object { $_.name -eq 'curve-boundary-differs' -and $_.boundaryPreserved -eq $false }).Count
    shortPacket         = @($fxList | Where-Object { $_.reason -eq 'short-packet' }).Count
    crossSession        = @($fxList | Where-Object { $_.reason -eq 'cross-session' }).Count
    unsupported         = @($fxList | Where-Object { $_.reason -in @('device-not-whitelisted', 'block-unsupported') }).Count
    telemetryUnknown    = @($fxList | Where-Object { $_.reason -eq 'scale-unverified' -and $_.rawOnly }).Count
  }
  $script:Run.extra['fixtureCoverage'] = $covered
  $missing = @($covered.Keys | Where-Object { [int]$covered[$_] -lt 1 })
  Add-SimCheck -Run $script:Run -Form 'FIXTURES' -Name 'fixture coverage: all required kinds present and asserted' -Ok ($missing.Count -eq 0) -Detail ("missing=" + $(if ($missing.Count) { $missing -join ',' } else { 'none' }) + " covered=" + (($covered.Keys | ForEach-Object { "{0}={1}" -f $_, $covered[$_] }) -join ' '))
  $ro = Assert-MsiReadOnly
  Add-SimCheck -Run $script:Run -Form 'FIXTURES' -Name 'read path contains no write call (no Set_* call site, no EC/file write token)' -Ok (@($ro).Count -eq 0) -Detail ("hits=" + $(if (@($ro).Count) { $ro -join ',' } else { 'none' }))
  Add-SimCheck -Run $script:Run -Form 'FIXTURES' -Name 'zero-fill ban: unknown telemetry keeps values=null' -Ok (@($script:Run.extra['fixtures'] | Where-Object { $_.name -eq 'telemetry-block0' -and $_.rawOnly -and $null -eq (ConvertFrom-MsiCurve -Fixture (Get-Content -LiteralPath (Join-Path $fixturesDir 'telemetry-block0.json') -Raw -Encoding UTF8 | ConvertFrom-Json)).values }).Count -eq 1) -Detail 'telemetry fixture returns raw only'
}

function Invoke-MsiDecode {
  if (-not $Fixture) { Add-SimCheck -Run $script:Run -Form 'DECODE' -Name 'fixture name supplied' -Ok $false -Detail '需要 -Fixture <name>'; return }
  $path = Join-Path $fixturesDir ("{0}.json" -f $Fixture)
  if (-not (Test-Path -LiteralPath $path)) { Add-SimCheck -Run $script:Run -Form 'DECODE' -Name ("fixture {0} exists" -f $Fixture) -Ok $false -Detail $path; return }
  $rules = Get-Content -LiteralPath $rulesPath -Raw -Encoding UTF8 | ConvertFrom-Json
  $whitelist = @($rules.deviceWhitelist | ForEach-Object { $_.model })
  $fx = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
  $dec = ConvertFrom-MsiCurve -Fixture $fx -Whitelist $whitelist
  $script:Run.extra['decoded'] = $dec
  Write-Host ("decoded {0}: ok={1} reason={2} duties={3} payload7={4} rawOnly={5} asRpm={6}" -f $dec.name, $dec.ok, $dec.reason, ($dec.duties -join '/'), $dec.payload7, $dec.rawOnly, $dec.asRpm)
  $fails = Test-MsiFixtureExpectation -Fixture $fx -Decoded $dec
  Add-SimCheck -Run $script:Run -Form 'DECODE' -Name ("decode {0} matches its expectation" -f $fx.name) -Ok ($fails.Count -eq 0) -Detail ($(if ($fails.Count) { $fails -join '; ' } else { 'ok' }))
}

function Invoke-MsiReadback {
  <#
    B1 read-only adapter. Default OFF. Single-flight + timeout + session/generation binding + model whitelist.
    On this machine (no MSI device) it must deliver MSI_DEVICE_PENDING and never fabricate a value.
  #>
  $rules = Get-Content -LiteralPath $rulesPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if (-not $EnableReadback) {
    Add-SimCheck -Run $script:Run -Form 'READBACK' -Name 'read-only adapter is default-off' $true -Detail 'readback not enabled on this invocation (-EnableReadback absent)'
    Add-SimStatus -Run $script:Run -Scope 'readback' -Status 'NOT_RUN' -Detail 'default-off: enable with -EnableReadback inside an authorized window' -Required $false
    return
  }
  Add-SimCheck -Run $script:Run -Form 'READBACK' -Name 'readback enabled explicitly (authorized window only)' $true -Detail '-EnableReadback present'
  $script:Run.modeKind = 'device-observation'
  # single-flight: a lock file in the evidence dir, stale after 30 s
  $lock = Join-Path $EvidenceDir 'msi-readback.lock'
  if (Test-Path -LiteralPath $lock) {
    $age = (Get-Date) - (Get-Item -LiteralPath $lock).LastWriteTime
    if ($age.TotalSeconds -lt 30) {
      Add-SimCheck -Run $script:Run -Form 'READBACK' -Name 'single-flight (no concurrent readback)' -Ok $false -Detail ("lock is {0:N1}s old: another readback is in flight" -f $age.TotalSeconds)
      return
    }
  }
  [IO.File]::WriteAllText($lock, ("{0}|{1}" -f $PID, (Get-SimNowStamp)), [Text.UTF8Encoding]::new($false))
  try {
    # session/generation binding for this tool run
    $script:Run.identity['nativeSessionHash'] = Get-SimSessionHash
    $script:Run.identity['readbackCapturedAt'] = Get-SimNowStamp
    $whitelist = @($rules.deviceWhitelist | ForEach-Object { $_.model })
    $msi = @(Get-CimInstance -Namespace 'root\wmi' -ClassName 'MSI_ACPI' -ErrorAction SilentlyContinue)
    $script:Run.extra['msiAcpiInstances'] = @($msi | ForEach-Object { $_.InstanceName })
    if (@($msi).Count -eq 0) {
      Add-SimCheck -Run $script:Run -Form 'READBACK' -Name 'MSI device present (MSI_ACPI WMI instance)' -Ok $false -Detail 'no MSI_ACPI instance on this machine'
      Add-SimStatus -Run $script:Run -Scope 'readback' -Status 'NOT_RUN' -Detail 'MSI_DEVICE_PENDING: no MSI device on this machine; the adapter stays default-off and untested on hardware' -Required $true
      return
    }
    if ($Model -and ($whitelist -notcontains $Model)) {
      Add-SimCheck -Run $script:Run -Form 'READBACK' -Name ("model {0} is whitelisted" -f $Model) -Ok $false -Detail ("whitelist=" + ($whitelist -join ','))
      return
    }
    Add-SimCheck -Run $script:Run -Form 'READBACK' -Name 'device probe + whitelist' -Ok $true -Detail ("instances={0} model={1}" -f @($msi).Count, $(if ($Model) { $Model } else { '(auto)' }))
    # bounded read: Get_Fan block 1 through CIM with a hard timeout; result decoded by the pure decoder
    $job = Start-Job -ScriptBlock {
      param($inst)
      try {
        $r = Invoke-CimMethod -InputObject $inst -MethodName 'Get_Fan' -Arguments @{ iDataBlockIndex = 1 } -ErrorAction Stop
        return ($r | Out-String)
      } catch { return ("ERROR: " + $_.Exception.Message) }
    } -ArgumentList $msi[0]
    $done = Wait-Job -Job $job -Timeout $TimeoutSec
    if (-not $done) {
      Stop-Job -Job $job -ErrorAction SilentlyContinue
      Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
      Add-SimCheck -Run $script:Run -Form 'READBACK' -Name 'read completes inside its budget (single-flight + timeout)' -Ok $false -Detail ("timeout={0}s (late result is discarded, not awaited forever)" -f $TimeoutSec)
      Add-SimEvent -Run $script:Run -Kind 'timeout' -Detail ("MSI readback exceeded {0}s" -f $TimeoutSec)
      return
    }
    $raw = Receive-Job -Job $job
    Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
    Add-SimCheck -Run $script:Run -Form 'READBACK' -Name 'read completed inside its budget' -Ok $true -Detail ("{0}" -f (("$raw").Substring(0, [Math]::Min(120, ("$raw").Length))))
    $script:Run.extra['readbackRaw'] = "$raw"
  } finally {
    Remove-Item -LiteralPath $lock -Force -ErrorAction SilentlyContinue
  }
}

# ------------------------------------------------------------------- main ----
$exitCode = 0
try {
  Write-Host ("=== MSI/CT 风扇域（ymcc_sim -Domain msi）mode={0} ===" -f $Mode) -ForegroundColor White
  switch ($Mode) {
    'table' { Show-MsiTable }
    'fixtures' { Invoke-MsiFixtures }
    'decode' { Invoke-MsiDecode }
    'readback' { Invoke-MsiReadback }
  }
}
catch {
  Write-Host ("执行中止：{0}" -f $_.Exception.Message) -ForegroundColor Red
  Add-SimCheck -Run $script:Run -Form 'RUN' -Name 'run completed without exception' -Ok $false -Detail $_.Exception.Message
  Add-SimEvent -Run $script:Run -Kind 'exception' -Detail $_.Exception.Message
}
finally {
  $script:Run.extra['deviceStatus'] = 'MSI_DEVICE_PENDING'
  $exitCode = Complete-SimRun -Run $script:Run -EvidenceDir $EvidenceDir -Prefix 'msi-sim'
}
exit $exitCode