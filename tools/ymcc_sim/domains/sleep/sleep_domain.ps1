<#
.SYNOPSIS
  睡眠域（ymcc_sim -Domain sleep）：在 Fan Host 边界模拟"睡眠开关"与全部睡眠/唤醒形态，
  并可驱动已安装 App 的真 Host 做实测（开/关 App 授权窗口）。

.DESCRIPTION
  形态库来源：本机真实睡眠事实日志（PowerControl\Sleep\sleep-facts.log[.previous]，52 h / 5269 条事实）
  + 源码常量；逐项出处见同目录 sleep_forms.json（形态表）与它的 dataSources/代码常量段。

  注入通道：Fan Host 的鉴权 HTTP API（POST /api/suspend|resume，头 X-YeMan-Fan-Session），
  suspend 使用 source=selftest.native（Host 指定的外部模拟 authority）。

  安全边界（硬约束）：
    * 本域从不传 --real-backend ⇒ Host 的 realBackend 恒为 null ⇒ 不存在 EC/HID/HC 写入路径；
    * forms/quick/switch 模式自启"沙箱宿主"（安全模式 + lane 里的 mock-handshake 旗标），仅监听回环端口，
      系统不会真的睡眠（不做 SetSuspendState），不影响任何用户进程；
    * -Attach / live 模式会推进 Host 内存中的电源代次，本轮结束后 App 自身的电源边沿会被判 stale
      ⇒ Attach 必须显式 -AcknowledgeAttachRestart，且本轮结束后重启 App 才能恢复。

.PARAMETER Mode
  selfcheck = 离线自检（不起宿主、不启动 App；校验形态库/路径/身份/无 --real-backend 断言）
  forms     = 全形态回放（默认；自启临时宿主 8765，跑完自动关闭）
  quick     = 单次开关动作（沙箱宿主常驻 8766：sleep / wake / status / stop）
  switch    = 交互式开关（s/w/t/q，持续翻转，宿主常驻）
  live      = 真实 App 实测：启动 App → 真 Host 边沿 → 反推日志 → WM_APP_EXIT 关 App →（默认）重启复验 → 关 App

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\ymcc_sim\domains\sleep\sleep_domain.ps1 -Mode selfcheck
.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode forms                       # 全形态回放（20 形态）
.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode quick -Action sleep
.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode live -StartApp -Elevate     # 实测（需管理员会话）
.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode live -Attach -AcknowledgeAttachRestart
#>
[CmdletBinding()]
param(
  [ValidateSet('forms', 'quick', 'switch', 'live', 'selfcheck', 'retrybudget', 'sleeptypes', 'typeseries', 'rapidwake')][string]$Mode = 'forms',
  [ValidateSet('sleep', 'wake', 'status', 'stop')][string]$Action = 'status',
  [int]$Port = 0,
  [switch]$StartHost,
  [switch]$Attach,
  [switch]$AcknowledgeAttachRestart,
  [string]$HostExe = '',
  [string]$SessionTokenPath = '',
  [string]$SandboxDir = '',
  [string]$Forms = 'all',
  [string]$Keys,
  [double]$TimeScale = 1.0,
  [switch]$LongSleepReal,
  [string]$GenerationBase,
  [string]$EvidenceDir = '',
  [switch]$SkipShutdown,
  [int]$RequestTimeoutMs = 20000,
  [string]$RapidDelays = '',
  [int]$RapidCycles = 5,
  # ---- live mode (real app + real host; operator-authorized window) ----
  [string]$InstallExe = '',
  [switch]$StartApp,
  [switch]$CloseApp,
  [switch]$KeepAppOpen,
  [switch]$SkipRestartProbe,
  [switch]$SimulateRun,
  [int]$WaitAppReadySec = 150,
  [int]$WaitAppExitSec = 60,
  [switch]$Reverse
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path (Split-Path -Parent (Split-Path -Parent $scriptDir)) 'lib\sim_core.ps1')

$D = Get-SimDefaults
if (-not $HostExe) { $HostExe = $D.hostExe }
if (-not $SessionTokenPath) { $SessionTokenPath = $D.sessionTokenPath }
if (-not $InstallExe) { $InstallExe = $D.appExe }
if (-not $EvidenceDir) { $EvidenceDir = $D.evidenceDir }
if (-not (Test-Path -LiteralPath $EvidenceDir)) { New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null }

$script:Run = New-SimRun -Domain 'sleep' -Mode $Mode -ModeKind $(if ($Mode -eq 'live') { 'live-app-injection' } elseif ($Mode -eq 'selfcheck' -or $Mode -eq 'sleeptypes') { 'none' } else { 'offline-injection' }) -ToolPath $PSCommandPath -Seed 204
# S0/S3/S4 分型判型器（204-F）：纯函数 + fixtures；供 -Mode sleeptypes 使用
$script:TypingLib = Join-Path $scriptDir 'lib_sleep_typing.ps1'
if (Test-Path -LiteralPath $script:TypingLib) { . $script:TypingLib }
# 分型系列（204-G）：S0ix/S3/S4 注入式形态化测试（不触发真实睡眠）
$script:TypeSeriesLib = Join-Path $scriptDir 'lib_sleep_typeseries.ps1'
if (Test-Path -LiteralPath $script:TypeSeriesLib) { . $script:TypeSeriesLib }
# 入睡失败/极短睡眠（rapid-wake）系列（204-H）
$script:RapidWakeLib = Join-Path $scriptDir 'lib_sleep_rapidwake.ps1'
if (Test-Path -LiteralPath $script:RapidWakeLib) { . $script:RapidWakeLib }
$script:Checks = $script:Run.checks
$script:Steps = $script:Run.steps
$script:FormVerdicts = $script:Run.forms
$script:CurrentForm = ''
$script:Base = "http://127.0.0.1:$Port"
$script:Token = $null
$script:HostProc = $null
$script:StartedByUs = $false
$script:TempTokenFile = $null
$script:HostIdentity = $null
$script:MockFlag = $null
$script:HostMode = $null
$script:LongSleepStatus = 'not-selected'
$script:ScriptStartedApp = $false
$script:ChangeCallCount = 0          # SIM-2
$script:GateAllowed = $false         # SIM-2
$script:OwnedAppIdentities = @()     # SIM-3/4
$script:LogSnapshot = $null          # SIM-4
$script:SandboxFacts = $null         # SIM-2
$script:ShutdownOwnHost = $false     # D-204-6: transient modes close the host they started (single-instance host)

function Get-ThisToolIdentity {
  # the domain's own identity + the core it depends on (both resolvable on disk)
  return [ordered]@{
    domainScript = (Get-SimFileIdentity $PSCommandPath 'sleep_domain')
    coreLibrary = (Get-SimFileIdentity (Join-Path (Split-Path -Parent (Split-Path -Parent $scriptDir)) 'lib\sim_core.ps1') 'sim_core')
    formsLibrary = (Get-SimFileIdentity (Join-Path $scriptDir 'sleep_forms.json') 'sleep_forms')
    version = 'sleep_domain v1 (2026-09-22, migrated from sleep-form-simulator.ps1)'
  }
}

function Add-Check([string]$form, [string]$name, [bool]$ok, [string]$detail, [bool]$Required = $true) {
  # silent: form bodies call this as a statement; a returned value would leak "True"
  # lines into the run output. -Required defaults to $true (fail-closed, 204 SIM-1).
  $null = Add-SimCheck -Run $script:Run -Form $form -Name $name -Ok $ok -Detail $detail -Required $Required
}

function New-Generation {
  if ($script:GenBase -lt 1) { $script:GenBase = 1 }
  $script:GenBase = $script:GenBase + 1
  return $script:GenBase
}

function Save-LastGeneration([int64]$gen) {
  try {
    # [IO.File]::WriteAllText, NOT Set-Content -Encoding UTF8: in PS 5.1 the latter
    # emits a UTF-8 BOM, which breaks strict JSON readers (python json.load raised
    # "Unexpected UTF-8 BOM" on this file during the batch-30 evidence index).
    $payload = [ordered]@{ lastGeneration = $gen; updatedAt = (Get-Date).ToString('o'); by = 'ymcc_sim/sleep' } | ConvertTo-Json
    [IO.File]::WriteAllText((Join-Path $EvidenceDir 'sleep-sim-lastgen.json'), $payload, [Text.UTF8Encoding]::new($false))
  } catch { }
}

function Get-LastGeneration {
  $p = Join-Path $EvidenceDir 'sleep-sim-lastgen.json'
  if (Test-Path -LiteralPath $p) {
    try { return [int64](Get-Content -LiteralPath $p -Raw -Encoding UTF8 | ConvertFrom-Json).lastGeneration } catch { return 0 }
  }
  return 0
}

function Send-Api {
  param(
    [string]$Method,
    [string]$Path,
    [string]$Body = '{}',
    [string]$Label = '',
    [string]$Form = '',
    [switch]$NoToken,
    [switch]$NoStateAfter,
    [switch]$ChangeCall      # SIM-2: power transitions / control changes are gated + counted
  )
  if ($ChangeCall -and -not $script:GateAllowed) {
    $refused = [ordered]@{
      form = $Form; label = $Label; method = $Method; path = $Path; body = $Body
      status = 0; ms = 0; raw = ''; json = $null; deduplicated = $null; resumeNoop = $null
      reason = 'gate-not-established'; errorCode = 'SIM_GATE_NOT_ESTABLISHED'; stateAfter = $null
      changeCall = $true; refusedByGate = $true
    }
    Add-SimStep -Run $script:Run -Record $refused
    Add-SimEvent -Run $script:Run -Kind 'exception' -Detail ("change call refused because the hardware gate was not established: {0}" -f $Path)
    Write-Host ("    -> REFUSED {0} {1} (gate not established)" -f $Method, $Path) -ForegroundColor Red
    return [pscustomobject]$refused
  }
  if ($ChangeCall) { $script:ChangeCallCount = $script:ChangeCallCount + 1 }
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  $r = Send-SimFanApi -Method $Method -Path $Path -Base $script:Base -Token $(if ($NoToken) { $null } else { $script:Token }) -Body $Body -TimeoutMs $RequestTimeoutMs -IsChangeCall:$ChangeCall
  $sw.Stop()
  $stateAfter = $null
  if (-not $NoStateAfter -and -not $NoToken -and $Path -ne '/health' -and $Path -ne '/api/state') {
    try {
      $st = Send-Api -Method GET -Path '/api/state' -NoStateAfter -Form $Form -Label 'state'
      $stateAfter = $st.json.state
    } catch { }
  }
  $record = [ordered]@{
    form = $Form; label = $Label; method = $Method; path = $Path; body = $Body
    status = [int]$r.status; ms = [int]$r.ms; raw = $r.raw
    json = $r.json
    deduplicated = $r.deduplicated
    resumeNoop = $r.resumeNoop
    reason = $r.reason
    errorCode = $r.errorCode
    stateAfter = $stateAfter
    changeCall = [bool]$ChangeCall
    refusedByGate = $false
  }
  Add-SimStep -Run $script:Run -Record $record
  Write-Host ("    -> {0} {1} : {2} ({3} ms) {4}" -f $Method, $Path, [int]$r.status, [int]$r.ms,
    $(if ($r.errorCode) { "err=$($r.errorCode)" } elseif ($r.deduplicated) { 'deduplicated' } elseif ($r.resumeNoop) { "noop($($r.reason))" } else { '' }))
  return [pscustomobject]$record
}

function Suspend-Body([int64]$gen, [string]$source = 'selftest.native') {
  return ('{{"generation":{0},"source":"{1}"}}' -f $gen, $source)
}

function Resume-Body([int64]$gen, [string]$source = 'selftest.native') {
  return ('{{"generation":{0},"source":"{1}"}}' -f $gen, $source)
}

function State-Brief($state) {
  if (-not $state) { return 'n/a' }
  return ("state={0} powerState={1} lease={2} open={3}/{4}" -f $state.state, $state.powerState,
    $(if ($state.lease) { 'yes' } else { 'no' }), $state.openCalled, $state.openEventsCalled)
}

function Get-State {
  $st = Send-Api -Method GET -Path '/api/state' -NoStateAfter -Label 'state'
  return $st.json.state
}

# ---------------------------------------------------------------- host setup ----
function Get-HostIdentity([string]$exePath) {
  $exe = Get-SimFileIdentity $exePath 'hostExe'
  $dll = Get-SimFileIdentity (Join-Path (Split-Path -Parent $exePath) 'YeManFanHost.dll') 'hostDll'
  $flag = Get-SimFileIdentity (Join-Path (Split-Path -Parent $exePath) 'mock-handshake.flag') 'mockFlag'
  return [ordered]@{
    exePath = $exe.path; exeBytes = $exe.bytes; exeSha256 = $exe.sha256
    dllPath = $dll.path; dllBytes = $dll.bytes; dllSha256 = $dll.sha256
    mockFlagPath = $flag.path; mockFlagPresent = $flag.exists; mockFlagSha256 = $flag.sha256
    launchArgs = @('--port', "$Port", '--protocol-version', '2', '--session-token-file', '<temp>')
    realBackendPassed = $false
  }
}

function Test-PortListening([int]$p) { return (Test-SimPort $p) }

function Start-SandboxHost {
  if (-not (Test-Path -LiteralPath $HostExe)) { throw "找不到已安装的 Fan Host：$HostExe" }
  if (Test-PortListening $Port) { throw "端口 $Port 已被占用（可能有正在运行的 Host/App）。请先关闭，或改用 -Attach（需 -AcknowledgeAttachRestart）。" }
  $script:HostIdentity = Get-HostIdentity $HostExe
  # 2026-09-23 架构修正：mock 只经显式测试开关；本域启动宿主时固定传 --mock-handshake
  # （测试模式 AI-only）；遗留的 mock-handshake.flag 不再改变宿主行为。
  $script:MockFlag = $true
  $script:MockFlagSource = 'cli-arg(--mock-handshake)'
  if ($script:HostIdentity.mockFlagPresent) {
    Write-Host 'NOTE: lane 内仍留有 mock-handshake.flag —— 自 2026-09-23 起该文件不再开启 mock（只有 --mock-handshake 开关才有效）。' -ForegroundColor DarkGray
  }
  $script:TempTokenFile = Join-Path ([System.IO.Path]::GetTempPath()) ("YeManFanHost-sleepsim-{0}.session" -f $PID)
  $script:Token = New-SimFanSessionToken $script:TempTokenFile
  $launchArgs = @('--port', "$Port", '--protocol-version', '2', '--session-token-file', $script:TempTokenFile, '--mock-handshake')
  $proc = Start-Process -FilePath $HostExe -ArgumentList $launchArgs -PassThru -WindowStyle Hidden
  $script:HostProc = $proc
  $script:StartedByUs = $true
  $script:ShutdownOwnHost = $true   # 单实例实测（D-204-6）：瞬时模式自启的宿主必须在收尾关闭，否则下一次沙箱启动会被"未就绪"打断
  # SIM-2: record the facts the gate needs (started by this run; launch args proven free of --real-backend)
  $script:SandboxFacts = @{
    launchedByRun = $true
    realBackendPassed = (@($launchArgs | Where-Object { "$_" -match 'real-backend' }).Count -gt 0)
    launchArgs = $launchArgs
    hostPid = $proc.Id
    hostIdentity = (Get-SimProcessIdentity -ProcessId $proc.Id -Role 'FanHost')
  }
  $ready = $false
  for ($i = 0; $i -lt 60; $i++) {
    try {
      $health = Send-Api -Method GET -Path '/health' -NoStateAfter -Label 'health'
      if ($health.status -eq 200) { $ready = $true; break }
    } catch { }
    Start-Sleep -Milliseconds 200
  }
  if (-not $ready) { throw '自启的 Fan Host 未在 12 s 内就绪' }
  Write-Host ("Host 已启动 (pid={0})，安全模式：未传 --real-backend ⇒ realBackend=null ⇒ 零硬件写入路径；mock 开关={1}" -f $proc.Id, $script:MockFlag)
}

function Ensure-PersistentSandboxHost {
  # 开关模式（quick/switch）用：稳定令牌 + 默认沙箱端口，宿主跨命令常驻，便于反复翻转开关。
  if (-not $SandboxDir) { $SandboxDir = Join-Path $EvidenceDir 'sandbox' }
  if (-not (Test-Path -LiteralPath $SandboxDir)) { New-Item -ItemType Directory -Force -Path $SandboxDir | Out-Null }
  $tokenFile = Join-Path $SandboxDir 'YeManFanHost.session'
  if (Test-Path -LiteralPath $tokenFile) {
    $script:Token = (Get-Content -LiteralPath $tokenFile -Raw -Encoding UTF8).Trim()
  } else {
    $script:Token = New-SimFanSessionToken $tokenFile
  }
  $script:SandboxTokenFile = $tokenFile
  $script:HostIdentity = Get-HostIdentity $HostExe
    $script:MockFlag = $true
    $script:MockFlagSource = 'cli-arg(--mock-handshake)'
  if (Test-PortListening $Port) {
    $health = $null
    try { $health = Send-Api -Method GET -Path '/health' -NoStateAfter -Label 'health' } catch { }
    if (-not $health -or $health.status -ne 200) {
      throw ("端口 {0} 上有非沙箱宿主（令牌不匹配 status={1}）。请换 -Port，或先关闭那个宿主。" -f $Port, $(if ($health) { $health.status } else { 'n/a' }))
    }
    Write-Host ("复用已运行的沙箱宿主：{0}（模拟开关跨命令保持状态）" -f $script:Base)
    return
  }
  $persistArgs = @('--port', "$Port", '--protocol-version', '2', '--session-token-file', $tokenFile, '--mock-handshake')
  $proc = Start-Process -FilePath $HostExe -ArgumentList $persistArgs -PassThru -WindowStyle Hidden
  $script:HostProc = $proc
  $script:StartedByUs = $true
  $script:SandboxFacts = @{
    launchedByRun = $true
    realBackendPassed = (@($persistArgs | Where-Object { "$_" -match 'real-backend' }).Count -gt 0)
    launchArgs = $persistArgs
    hostPid = $proc.Id
    hostIdentity = (Get-SimProcessIdentity -ProcessId $proc.Id -Role 'FanHost')
  }
  $ready = $false
  for ($i = 0; $i -lt 60; $i++) {
    try {
      $health = Send-Api -Method GET -Path '/health' -NoStateAfter -Label 'health'
      if ($health.status -eq 200) { $ready = $true; break }
    } catch { }
    Start-Sleep -Milliseconds 200
  }
  if (-not $ready) { throw '沙箱 Fan Host 未在 12 s 内就绪' }
  Write-Host ("沙箱宿主已启动 (pid={0}，端口 {1})：安全模式（零硬件写入路径），mock 开关={2}；用 -Mode quick -Action stop 关闭。" -f $proc.Id, $Port, $script:MockFlag)
}

function Stop-SandboxHost {
  if (-not $SandboxDir) { $SandboxDir = Join-Path $EvidenceDir 'sandbox' }
  $tokenFile = Join-Path $SandboxDir 'YeManFanHost.session'
  if (-not (Test-Path -LiteralPath $tokenFile)) { Write-Host '没有沙箱令牌文件：从未启动过沙箱宿主。' -ForegroundColor Yellow; return }
  $script:Token = (Get-Content -LiteralPath $tokenFile -Raw -Encoding UTF8).Trim()
  if (-not (Test-PortListening $Port)) { Write-Host ("端口 {0} 上没有在跑的宿主（沙箱已停）。" -f $Port); return }
  $r = Send-Api -Method POST -Path '/api/shutdown' -Body '{}' -NoStateAfter -Label 'shutdown'
  if ($r.status -eq 200) { Write-Host ("沙箱宿主已请求退出：{0}" -f $script:Base) -ForegroundColor Green }
  else { Write-Host ("关闭请求被拒绝：status={0} code={1}（该端口上的宿主可能不是本沙箱）" -f $r.status, $r.errorCode) -ForegroundColor Yellow }
}

function Connect-ExistingHost {
  if (-not $AcknowledgeAttachRestart) {
    throw 'Attach 模式会推进 Host 内存电源代次，App 之后的真实电源边沿将被判 stale（必须重启 App 恢复）。如确要执行请加 -AcknowledgeAttachRestart。'
  }
  if (-not (Test-PortListening $Port)) { throw "端口 $Port 没有监听中的 Host；如要自启请去掉 -Attach。" }
  if (-not (Test-Path -LiteralPath $SessionTokenPath)) { throw "找不到会话令牌文件：$SessionTokenPath" }
  $script:Token = Get-SimFanSessionToken $SessionTokenPath
  if (-not $script:Token) { throw '会话令牌文件为空' }
  Write-Host ("Attach 模式：对接正在运行的 Host {0}（令牌文件 {1}）。注意：本轮结束后必须重启 App！" -f $script:Base, $SessionTokenPath) -ForegroundColor Yellow
}

# ---------------------------------------------------------------- forms ----
function Form-P1-Tokenless {
  $r = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body 1) -NoToken -Form 'P1' -Label 'tokenless-suspend'
  Add-Check 'P1' '401 without token' ($r.status -eq 401 -and $r.errorCode -eq 'API_SESSION_REQUIRED') "status=$($r.status) code=$($r.errorCode)"
}

function Form-P2-MissingGeneration {
  $r = Send-Api -Method POST -Path '/api/suspend' -Body '{"source":"selftest.native"}' -Form 'P2' -Label 'missing-generation'
  Add-Check 'P2' '400 missing generation' ($r.status -eq 400 -and $r.errorCode -eq 'POWER_GENERATION_REQUIRED') "status=$($r.status) code=$($r.errorCode)"
}

function Form-P3-WrongAuthority {
  $r = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body 999998 'ui.renderer') -Form 'P3' -Label 'wrong-authority'
  Add-Check 'P3' '403 non-native authority' ($r.status -eq 403 -and $r.errorCode -eq 'F4_NATIVE_AUTHORITY_REQUIRED') "status=$($r.status) code=$($r.errorCode)"
}

function Form-P7-MissingSource {
  $r = Send-Api -Method POST -Path '/api/suspend' -Body '{"generation":999997}' -Form 'P7' -Label 'missing-source'
  Add-Check 'P7' '400 missing source' ($r.status -eq 400 -and $r.errorCode -eq 'POWER_GENERATION_REQUIRED') "status=$($r.status) code=$($r.errorCode)"
}

function Form-P4-StaleSameGenSuspend {
  $g = New-Generation
  $s1 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'P4' -Label 'suspend'
  $r1 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'P4' -Label 'resume'
  $before = Get-State
  $s2 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'P4' -Label 'same-gen-suspend-again'
  Add-Check 'P4' 'same-gen suspend after resume rejected' ($s2.status -eq 409 -and $s2.errorCode -eq 'POWER_EVENT_STALE') "status=$($s2.status) code=$($s2.errorCode)"
  $after = Get-State
  Add-Check 'P4' 'state untouched by rejection' ($after.state -eq $before.state -and $after.powerState -eq $before.powerState) "before=$(State-Brief $before) | after=$(State-Brief $after)"
}

function Form-P5-StaleOlderSuspend {
  $g = New-Generation
  $s1 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'P5' -Label 'suspend'
  $r1 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'P5' -Label 'resume'
  $s2 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body ($g - 1)) -Form 'P5' -Label 'older-gen-suspend'
  Add-Check 'P5' 'older-generation suspend rejected' ($s2.status -eq 409 -and $s2.errorCode -eq 'POWER_EVENT_STALE') "status=$($s2.status) code=$($s2.errorCode)"
}

function Form-P6-StaleOlderResume {
  $g = New-Generation
  $s1 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'P6' -Label 'suspend'
  $r1 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'P6' -Label 'resume'
  $r2 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body ($g - 1)) -Form 'P6' -Label 'older-gen-resume'
  Add-Check 'P6' 'older-generation resume rejected' ($r2.status -eq 409 -and $r2.errorCode -eq 'POWER_EVENT_STALE') "status=$($r2.status) code=$($r2.errorCode)"
}

function Form-B1-NormalShortSleep {
  $g = New-Generation
  $s = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'B1' -Label 'suspend'
  Add-Check 'B1' 'suspend accepted' ($s.status -eq 200 -and -not $s.deduplicated) "status=$($s.status) $(State-Brief $s.stateAfter)"
  Add-Check 'B1' 'suspended boundary' ($s.stateAfter.state -eq 'Suspended' -and $s.stateAfter.powerState -eq 'Suspended') (State-Brief $s.stateAfter)
  Start-Sleep -Milliseconds ([int](1500 * $TimeScale))
  $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'B1' -Label 'resume'
  Add-Check 'B1' 'resume accepted as a real transition' ($r.status -eq 200 -and -not $r.resumeNoop) "status=$($r.status) noop=$($r.resumeNoop)"
  Add-Check 'B1' 'awake boundary' ($r.stateAfter.powerState -eq 'On' -and $r.stateAfter.state -eq 'AwaitingControl') (State-Brief $r.stateAfter)
}

function Form-B2-LongSleep {
  $g = New-Generation
  $s = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'B2' -Label 'suspend'
  Add-Check 'B2' 'suspend accepted' ($s.status -eq 200) "status=$($s.status)"
  if ($LongSleepReal) {
    $script:LongSleepStatus = 'real-wait-1800s'
    Write-Host '  (真实长睡：等待 30 分钟，期间不做任何 Host 调用……)' -ForegroundColor Yellow
    Start-Sleep -Seconds 1800
  } else {
    $script:LongSleepStatus = 'compressed'
    Start-Sleep -Milliseconds ([int](2000 * $TimeScale))
  }
  $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'B2' -Label 'resume'
  Add-Check 'B2' 'resume accepted' ($r.status -eq 200) "status=$($r.status)"
  Add-Check 'B2' 'awake boundary after long gap' ($r.stateAfter.powerState -eq 'On') (State-Brief $r.stateAfter)
  if (-not $LongSleepReal) {
    Add-Check 'B2' 'host 30-min grace not triggerable in compressed mode (expected)' $true "SKIP: 门槛在 Host/native 内部（>=30min），压缩模式不触发；出处见 sleep_forms.json codeConstants（Program.cs:8786）"
  }
}

function Form-B3-DuplicateIdempotent {
  $g = New-Generation
  $s1 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'B3' -Label 'suspend'
  $s2 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'B3' -Label 'duplicate-suspend'
  Add-Check 'B3' 'first suspend accepted' ($s1.status -eq 200 -and -not $s1.deduplicated) "status=$($s1.status)"
  Add-Check 'B3' 'duplicate suspend deduplicated' ($s2.status -eq 200 -and $s2.deduplicated -eq $true) "status=$($s2.status) dedup=$($s2.deduplicated)"
  Add-Check 'B3' 'still parked at Suspended' ($s2.stateAfter.state -eq 'Suspended') (State-Brief $s2.stateAfter)
  $r1 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'B3' -Label 'resume'
  $r2 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'B3' -Label 'duplicate-resume'
  Add-Check 'B3' 'first resume accepted' ($r1.status -eq 200 -and -not $r1.resumeNoop) "status=$($r1.status)"
  # 同代次重复唤醒：ingress 去重，API 层按"同代次收据绑定"返回既有终态（200 + state），
  # 不得启动第二次 Open/Close（Program.cs:11162-11176 default 分支）。
  Add-Check 'B3' 'duplicate resume binds the same-generation receipt, no second transition' ($r2.status -eq 200 -and $r2.stateAfter.powerState -eq 'On') "status=$($r2.status) $(State-Brief $r2.stateAfter)"
}

function Form-B4-ResumeNoop {
  # 空转唤醒（无匹配入睡 / 无本代次收据）：代次比 announced 更新但没有对应入睡边沿时，
  # Host 无法证明任何恢复 ⇒ 显式过期（fail-closed），且不得伪造 AwaitingControl（Program.cs:11162-11176）。
  # native 侧对该 409 只记 ok=false、不武装续租、且不重试（native/main.cpp:32835-32858）。
  $before = Get-State
  $g = New-Generation
  $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'B4' -Label 'resume-without-suspend'
  Add-Check 'B4' 'unprovable wake fails closed with 409 HC_RESUME_RESULT_EXPIRED' ($r.status -eq 409 -and $r.errorCode -eq 'HC_RESUME_RESULT_EXPIRED') "status=$($r.status) code=$($r.errorCode)"
  $after = Get-State
  Add-Check 'B4' 'no fabricated awake state' ($after.state -eq $before.state -and $after.powerState -eq $before.powerState) "before=$(State-Brief $before) | after=$(State-Brief $after)"
}

function Form-B5-DuplicateWakeReceipt {
  # 唤醒收据绑定：一次真实入睡→唤醒之后，再收到同代次的重复唤醒 ⇒ 200 + 既有终态（不第二次重建）
  $g = New-Generation
  $s = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'B5' -Label 'suspend'
  $r1 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'B5' -Label 'resume'
  $r2 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'B5' -Label 'repeat-wake'
  Add-Check 'B5' 'first wake is a real transition' ($r1.status -eq 200 -and -not $r1.resumeNoop) "status=$($r1.status)"
  Add-Check 'B5' 'repeat wake returns the bound receipt (200, awake)' ($r2.status -eq 200 -and $r2.stateAfter.powerState -eq 'On') "status=$($r2.status) $(State-Brief $r2.stateAfter)"
  Add-Check 'B5' 'repeat wake did not reopen a session' ($r2.stateAfter.openCalled -eq $false -and $r2.stateAfter.openEventsCalled -eq $false) "open=$($r2.stateAfter.openCalled) openEvents=$($r2.stateAfter.openEventsCalled)"
}

function Form-B6-PhantomWake {
  # 幻影唤醒：更高代次、但没有对应入睡（例如 Host 在睡眠期间被重启后的孤儿唤醒）⇒ 无 attempt 可证明 ⇒ 409，状态不动
  $before = Get-State
  $g = New-Generation
  $g = New-Generation
  $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'B6' -Label 'phantom-wake-higher-generation'
  Add-Check 'B6' 'phantom wake rejected as unprovable' ($r.status -eq 409 -and $r.errorCode -eq 'HC_RESUME_RESULT_EXPIRED') "status=$($r.status) code=$($r.errorCode)"
  $after = Get-State
  Add-Check 'B6' 'state untouched by the phantom wake' ($after.state -eq $before.state -and $after.powerState -eq $before.powerState) "before=$(State-Brief $before) | after=$(State-Brief $after)"
}

function Form-C1-SpuriousWakeResleepLadder {
  # 真实阶梯（native/main.cpp:822 = 500/1000/2000ms）：入睡 → 被外部设备误唤醒 → 立刻重睡（最多 3 次）
  $delays = @(500, 1000, 2000)
  $g = New-Generation
  $s = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'C1' -Label 'suspend-0'
  Add-Check 'C1' 'initial sleep accepted' ($s.status -eq 200 -and $s.stateAfter.state -eq 'Suspended') "status=$($s.status) $(State-Brief $s.stateAfter)"
  $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'C1' -Label 'spurious-wake'
  Add-Check 'C1' 'spurious wake accepted' ($r.status -eq 200 -and $r.stateAfter.powerState -eq 'On') "status=$($r.status) $(State-Brief $r.stateAfter)"
  for ($i = 0; $i -lt $delays.Count; $i++) {
    $delay = [int]($delays[$i] * $TimeScale)
    Start-Sleep -Milliseconds $delay
    $g = New-Generation
    $s = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'C1' -Label ("resleep-{0}(+{1}ms)" -f ($i + 1), $delays[$i])
    Add-Check 'C1' ("re-sleep {0} accepted after {1} ms" -f ($i + 1), $delays[$i]) ($s.status -eq 200 -and $s.stateAfter.state -eq 'Suspended') "status=$($s.status) $(State-Brief $s.stateAfter)"
    $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'C1' -Label ("resleep-resume-{0}" -f ($i + 1))
    Add-Check 'C1' ("re-sleep {0} wake accepted" -f ($i + 1)) ($r.status -eq 200 -and $r.stateAfter.powerState -eq 'On') "status=$($r.status)"
  }
}

function Form-C2-InstantDoubleSleep {
  # 瞬间双次睡眠：一次"闪醒"（<1s）后立刻第二次入睡；两次入睡间隔远小于用户操作粒度
  $g1 = New-Generation
  $s1 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g1) -Form 'C2' -Label 'sleep-1'
  Add-Check 'C2' 'first sleep accepted' ($s1.status -eq 200 -and $s1.stateAfter.state -eq 'Suspended') "status=$($s1.status)"
  Start-Sleep -Milliseconds ([int](300 * $TimeScale))
  $r1 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g1) -Form 'C2' -Label 'flash-wake'
  Add-Check 'C2' 'flash wake accepted (<=300ms)' ($r1.status -eq 200 -and $r1.stateAfter.powerState -eq 'On') "status=$($r1.status)"
  Start-Sleep -Milliseconds ([int](300 * $TimeScale))
  $g2 = New-Generation
  $s2 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g2) -Form 'C2' -Label 'sleep-2'
  Add-Check 'C2' 'second sleep accepted right after the wake' ($s2.status -eq 200 -and $s2.stateAfter.state -eq 'Suspended') "status=$($s2.status)"
  $span = [int]$r1.ms + [int]$s2.ms + [int](600 * $TimeScale)
  Add-Check 'C2' 'double-sleep cycle stays inside one user-perceptible flash' ($span -le 3000) "approx cycle span=${span}ms (wake+re-sleep)"
  $r2 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g2) -Form 'C2' -Label 'final-wake'
  Add-Check 'C2' 'final wake accepted' ($r2.status -eq 200 -and $r2.stateAfter.powerState -eq 'On') "status=$($r2.status)"
}

function Form-C3-DoubleSuspendDelivery {
  # 双次入睡投递（同一物理入睡被两个观察者各投递一次，第二次带新代次）
  $g1 = New-Generation
  $s1 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g1) -Form 'C3' -Label 'sleep-observer-a'
  $g2 = New-Generation
  $s2 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g2) -Form 'C3' -Label 'sleep-observer-b(new-gen)'
  Add-Check 'C3' 'first delivery accepted' ($s1.status -eq 200 -and -not $s1.deduplicated) "status=$($s1.status)"
  Add-Check 'C3' 'second delivery absorbed without a second close' ($s2.status -eq 200 -and $s2.deduplicated -eq $true) "status=$($s2.status) dedup=$($s2.deduplicated)"
  Add-Check 'C3' 'single suspended boundary' ($s2.stateAfter.state -eq 'Suspended') (State-Brief $s2.stateAfter)
  $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g2) -Form 'C3' -Label 'wake'
  Add-Check 'C3' 'wake accepted' ($r.status -eq 200 -and $r.stateAfter.powerState -eq 'On') "status=$($r.status)"
}

function Form-C4-DenseStorm {
  # 真实密度：19 次/秒峰值、88.4% 间隔 <=200ms（sleep_forms.json density）
  $cycles = 10
  $gapMs = [int](150 * $TimeScale)
  $accepted = 0
  $rejected = 0
  $lastState = $null
  for ($i = 0; $i -lt $cycles; $i++) {
    $g = New-Generation
    $s = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'C4' -Label ("cycle-{0}-suspend" -f ($i + 1))
    if ($s.status -eq 200) { $accepted++ } else { $rejected++ }
    Start-Sleep -Milliseconds $gapMs
    $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'C4' -Label ("cycle-{0}-resume" -f ($i + 1))
    if ($r.status -eq 200) { $accepted++ } else { $rejected++ }
    $lastState = $r.stateAfter
    if ($i -lt $cycles - 1) { Start-Sleep -Milliseconds $gapMs }
  }
  Add-Check 'C4' ("{0} rapid edges all accepted (no 503 queue rejection)" -f ($cycles * 2)) ($rejected -eq 0 -and $accepted -eq $cycles * 2) "accepted=$accepted rejected=$rejected gapMs=$gapMs"
  Add-Check 'C4' 'consistent awake state after the storm' ($lastState -and $lastState.powerState -eq 'On') (State-Brief $lastState)
}

function Open-MockSession {
  # handshake 已在预检完成；这里完成 open → open-events → 取租约，返回 leaseId
  $op = Send-Api -Method POST -Path '/api/open' -Body '{}' -Form $script:CurrentForm -Label 'open'
  $oe = Send-Api -Method POST -Path '/api/open-events' -Body '{}' -Form $script:CurrentForm -Label 'open-events'
  $ac = Send-Api -Method POST -Path '/api/acquire-control' -Body '{}' -Form $script:CurrentForm -Label 'acquire-control'
  return [pscustomobject]@{ open = $op; openEvents = $oe; acquire = $ac; leaseId = $ac.json.lease.leaseId }
}

function Form-D1-SessionSleepNoCurve {
  # 会话已开但从未写曲线（HardwareWritesObserved=false）⇒ 入睡的 Close 边界可证明，
  # Host 必须干净入睡（Program.cs:9606-9611: null 后端返回 !HardwareWritesObserved）。
  if ($script:HostMode -ne 'mock-handshake') {
    Add-Check 'D1' 'mock-handshake mode present (session form)' $true "SKIP：宿主未进入 mock-handshake（hostMode=$($script:HostMode)）——本形态不适用（2026-09-23 起由 --mock-handshake 开关开启）"
    return
  }
  $session = Open-MockSession
  Add-Check 'D1' 'session opened (open/open-events/lease)' ($session.open.status -eq 200 -and $session.openEvents.status -eq 200 -and $session.leaseId) "status=$($session.open.status)/$($session.openEvents.status) lease=$([bool]$session.leaseId)"
  Add-Check 'D1' 'no write observed before sleep' ($session.acquire.stateAfter.hardwareWritesObserved -eq $false) "hardwareWritesObserved=$($session.acquire.stateAfter.hardwareWritesObserved)"
  $g = New-Generation
  $s = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'D1' -Label 'suspend-with-session'
  Add-Check 'D1' 'sleep with an open session (no curve) accepted cleanly' ($s.status -eq 200 -and $s.stateAfter.state -eq 'Suspended') "status=$($s.status) $(State-Brief $s.stateAfter)"
  Add-Check 'D1' 'lease released by the sleep boundary' (-not $s.stateAfter.lease) "lease=$(if ($s.stateAfter.lease) { 'still-held' } else { 'cleared' })"
  Start-Sleep -Milliseconds ([int](500 * $TimeScale))
  $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'D1' -Label 'resume-with-session'
  Add-Check 'D1' 'session-aware resume accepted' ($r.status -eq 200 -and -not $r.resumeNoop) "status=$($r.status) noop=$($r.resumeNoop)"
  Add-Check 'D1' 'awake after session sleep (no curve to rebuild ⇒ await-user)' ($r.stateAfter.state -eq 'AwaitingControl' -and $r.stateAfter.powerState -eq 'On') (State-Brief $r.stateAfter)
  Add-Check 'D1' 'no fabricated session reopen on the empty resume' ($r.stateAfter.openCalled -eq $false -and $r.stateAfter.openEventsCalled -eq $false) "open=$($r.stateAfter.openCalled) openEvents=$($r.stateAfter.openEventsCalled)"
}

function Form-D2-CurveSessionFaultClosed {
  # 已写曲线的会话（HardwareWritesObserved=true）在 safe/mock 模式下没有可证明的 OEM 恢复路径
  # ⇒ 入睡必须 fail-closed 成 FaultLocked，绝不允许伪装成功；随后的唤醒必须显式 409，不得伪造恢复。
  if ($script:HostMode -ne 'mock-handshake') {
    Add-Check 'D2' 'mock-handshake mode present (curve session form)' $true "SKIP：宿主未进入 mock-handshake（hostMode=$($script:HostMode)）——本形态不适用（2026-09-23 起由 --mock-handshake 开关开启）"
    return
  }
  $session = Open-MockSession
  $curve = '{"nodes":[{"tempC":0,"dutyPercent":0},{"tempC":55,"dutyPercent":30},{"tempC":75,"dutyPercent":60},{"tempC":90,"dutyPercent":100}],"leaseId":"' + $session.leaseId + '"}'
  $en = Send-Api -Method POST -Path '/api/enable' -Body $curve -Form 'D2' -Label 'enable(mock curve)'
  Add-Check 'D2' 'masked curve enabled (write observed flag set)' ($en.status -eq 200 -and $en.stateAfter.hardwareWritesObserved -eq $true) "status=$($en.status) hwWrites=$($en.stateAfter.hardwareWritesObserved)"
  $g = New-Generation
  $s = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'D2' -Label 'suspend-with-curve-session'
  Add-Check 'D2' 'sleep with an unprovable restore fails closed' ($s.status -eq 200 -and $s.stateAfter.state -eq 'FaultLocked' -and $s.stateAfter.powerState -eq 'Unknown') "status=$($s.status) $(State-Brief $s.stateAfter)"
  Add-Check 'D2' 'no fabricated restore evidence' ($s.stateAfter.oemRestoreConfirmed -ne $true -and $s.stateAfter.oemRestoreEvidence -ne 'hc-default-table-readback-confirmed') "oemRestoreConfirmed=$($s.stateAfter.oemRestoreConfirmed) evidence=$($s.stateAfter.oemRestoreEvidence)"
  $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'D2' -Label 'resume-after-fault'
  Add-Check 'D2' 'wake after fault fails explicitly (no fabricated recovery)' ($r.status -eq 409 -and $r.errorCode -eq 'HC_RESUME_REBUILD_FAILED') "status=$($r.status) code=$($r.errorCode)"
  Add-Check 'D2' 'fault is not silently cleared by the rejected wake' ($r.stateAfter.state -eq 'FaultLocked') (State-Brief $r.stateAfter)
  $rel = Send-Api -Method POST -Path '/api/release-control' -Body ('{"leaseId":"' + $session.leaseId + '"}') -Form 'D2' -Label 'release-control-after-fault'
  Add-Check 'D2' 'release-control accepted (explicit operator path)' ($rel.status -eq 200) "status=$($rel.status)"
  # MOCK 设计事实（Program.cs:9777-9790）：mock 的 restore 会伪装 OemRestoreConfirmed=true
  # （否则写后无 HC restore 回调会 FaultLocked）。判据：真机读回证据（hc-default-table-readback-confirmed）
  # 绝不允许出现——mock 只能声称"mock 恢复"，不得伪装成物理 OEM 读回。
  Add-Check 'D2' 'mock restore claim never masquerades as a physical HC readback' ($rel.stateAfter.oemRestoreEvidence -ne 'hc-default-table-readback-confirmed') "state=$($rel.stateAfter.state) oemRestoreConfirmed=$($rel.stateAfter.oemRestoreConfirmed)(mock fiction) evidence=$($rel.stateAfter.oemRestoreEvidence)"
}

function Form-D3-CloseCompetition {
  $g = New-Generation
  $s = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'D3' -Label 'suspend-before-close'
  Add-Check 'D3' 'sleep accepted before close' ($s.status -eq 200) "status=$($s.status)"
  $cl = Send-Api -Method POST -Path '/api/close' -Body '{}' -Form 'D3' -Label 'close'
  Add-Check 'D3' 'close accepted' ($cl.status -eq 200) "status=$($cl.status) $(State-Brief $cl.stateAfter)"
  $lr = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'D3' -Label 'late-resume-after-close'
  Add-Check 'D3' 'late resume cannot reopen a closing host' ($lr.status -eq 409 -and $lr.errorCode -eq 'POWER_CLOSE_PENDING') "status=$($lr.status) code=$($lr.errorCode)"
  $st = Send-Api -Method GET -Path '/api/state' -NoStateAfter -Form 'D3' -Label 'state-after-competition'
  Add-Check 'D3' 'terminal state is Stopped' ($st.json.state.state -eq 'Stopped') (State-Brief $st.json.state)
}

# ------------------------------------------------------------------ run ----
$FormNames = [ordered]@{
  'P1' = '无令牌请求（fail-closed 门）'
  'P2' = '缺 generation（fail-closed 门）'
  'P3' = '非 native authority 入睡（fail-closed 门）'
  'P7' = '缺 source（fail-closed 门）'
  'P4' = '同代次过时入睡（跨代拒绝）'
  'P5' = '更低代次入睡（跨代拒绝）'
  'P6' = '更低代次唤醒（跨代拒绝）'
  'B1' = '正常短睡（1.5 s 压缩自真实 1–5 min）'
  'B2' = '长睡（>=30 min 门槛）'
  'B3' = '重复边沿幂等（双次投递）'
  'B4' = '空转唤醒（无匹配入睡，fail-closed）'
  'B5' = '重复唤醒收据绑定（同代次）'
  'B6' = '幻影唤醒（更高代次、无入睡）'
  'C1' = '误唤醒→瞬间重睡阶梯（500/1000/2000 ms）'
  'C2' = '瞬间双次睡眠（闪醒后立刻第二次入睡）'
  'C3' = '双观察者双次入睡投递'
  'C4' = '密集风暴（10 循环 @150 ms，真实峰值 19/s）'
  'D1' = '带会话睡眠（无曲线，干净入睡/唤醒）'
  'D2' = '曲线会话睡眠（safe/mock 无法证明 OEM 恢复 ⇒ fail-closed）'
  'D3' = '入睡中关闭竞争（晚到唤醒被 fail-closed）'
}

$FormScripts = [ordered]@{
  'P1' = { Form-P1-Tokenless }
  'P2' = { Form-P2-MissingGeneration }
  'P3' = { Form-P3-WrongAuthority }
  'P7' = { Form-P7-MissingSource }
  'B4' = { Form-B4-ResumeNoop }
  'P4' = { Form-P4-StaleSameGenSuspend }
  'P5' = { Form-P5-StaleOlderSuspend }
  'P6' = { Form-P6-StaleOlderResume }
  'B1' = { Form-B1-NormalShortSleep }
  'B2' = { Form-B2-LongSleep }
  'B3' = { Form-B3-DuplicateIdempotent }
  'B5' = { Form-B5-DuplicateWakeReceipt }
  'B6' = { Form-B6-PhantomWake }
  'C1' = { Form-C1-SpuriousWakeResleepLadder }
  'C2' = { Form-C2-InstantDoubleSleep }
  'C3' = { Form-C3-DoubleSuspendDelivery }
  'C4' = { Form-C4-DenseStorm }
  'D1' = { Form-D1-SessionSleepNoCurve }
  'D2' = { Form-D2-CurveSessionFaultClosed }
  'D3' = { Form-D3-CloseCompetition }
}

function Invoke-Form([string]$id, [scriptblock]$body) {
  $script:CurrentForm = $id
  $before = $script:Checks.Count
  Write-Host ("[{0}] {1}" -f $id, $FormNames[$id]) -ForegroundColor Cyan
  try { & $body } catch {
    Add-Check $id 'form completed without exception' $false $_.Exception.Message
  }
  $slice = @($script:Checks | Select-Object -Skip $before)
  $fails = @($slice | Where-Object { -not $_.passed })
  $skips = @($slice | Where-Object { $_.detail -like 'SKIP*' })
  $verdict = if ($fails.Count -gt 0) { 'FAIL' } elseif ($skips.Count -eq $slice.Count) { 'SKIP' } else { 'PASS' }
  $script:FormVerdicts.Add([ordered]@{ form = $id; name = $FormNames[$id]; verdict = $verdict; checks = $slice.Count; failed = $fails.Count })
  Write-Host ("  => {0}" -f $verdict) -ForegroundColor $(if ($verdict -eq 'FAIL') { 'Red' } elseif ($verdict -eq 'SKIP') { 'DarkGray' } else { 'Green' })
}

function Invoke-SleepSelfCheck {
  # 离线自检：不启动 App、不起宿主；只证明这套工具自身可用（形态库/身份/断言/前置路径）
  Write-Host '=== 睡眠域离线自检（不启动 App、不起宿主）===' -ForegroundColor White
  $toolId = Get-ThisToolIdentity
  Add-Check 'SELF' 'core library present' $toolId.coreLibrary.exists ("{0} bytes={1}" -f $toolId.coreLibrary.sha256.Substring(0, 16), $toolId.coreLibrary.bytes) | Out-Null
  Add-Check 'SELF' 'sleep forms library present' $toolId.formsLibrary.exists ("{0} bytes={1}" -f $toolId.formsLibrary.sha256.Substring(0, 16), $toolId.formsLibrary.bytes) | Out-Null

  $formsPath = Join-Path $scriptDir 'sleep_forms.json'
  $forms = $null
  if (Test-Path -LiteralPath $formsPath) {
    try { $forms = Get-Content -LiteralPath $formsPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $forms = $null }
  }
  $formIds = @()
  if ($forms -and $forms.forms) { $formIds = @($forms.forms | ForEach-Object { $_.id }) }
  Add-Check 'SELF' 'forms library parses with 20 forms' ($formIds.Count -eq 20) ("forms=$($formIds.Count)") | Out-Null
  $missingInLib = @($FormScripts.Keys | Where-Object { $formIds -notcontains $_ })
  $missingInCode = @($formIds | Where-Object { -not $FormScripts.Contains($_) })
  Add-Check 'SELF' 'form ids match the implemented form scripts' ($missingInLib.Count -eq 0 -and $missingInCode.Count -eq 0) ("lib-only=$($missingInLib -join ',') code-only=$($missingInCode -join ',')") | Out-Null
  # the safety claim is about ARGUMENTS, not documentation: assert that the quoted
  # argument form never appears in this domain nor in the core library (the launch
  # lines are the only place it could be passed). The forbidden token is assembled
  # from two pieces on purpose: a literal here would make this very line self-match
  # (the "auditor matches itself" trap seen in earlier audit rounds).
  $domainText = Get-Content -LiteralPath $PSCommandPath -Raw -Encoding UTF8
  $corePath = Join-Path (Split-Path -Parent (Split-Path -Parent $scriptDir)) 'lib\sim_core.ps1'
  $coreText = Get-Content -LiteralPath $corePath -Raw -Encoding UTF8
  $forbidden = "'" + '--real-' + 'backend' + "'"
  $bad = @()
  if ($domainText -match [regex]::Escape($forbidden)) { $bad += 'domain' }
  if ($coreText -match [regex]::Escape($forbidden)) { $bad += 'core' }
  Add-Check 'SELF' 'host is never launched with a real backend (quoted-arg assertion)' ($bad.Count -eq 0) $(if ($bad.Count -eq 0) { 'no quoted real-backend argument in domain/core => zero hardware write path' } else { 'found in: ' + ($bad -join ',') }) | Out-Null
  Add-Check 'SELF' 'host exe present' (Test-Path -LiteralPath $HostExe) $HostExe | Out-Null
  # 2026-09-23 架构修正后，mock 只由显式 `--mock-handshake` 开关开启（AI-only；遗留旗标文件不再生效），
  # 因此这里断言"域确实在两条启动路径上都传了该开关"（否则 D1/D2 会静默 SKIP），
  # 而不再断言旗标文件在场——正式导出物按设计不含该文件。
  $mockSwitchToken = "'" + '--mock-' + 'handshake' + "'"
  $mockSwitchPassed = [regex]::Matches($domainText, [regex]::Escape($mockSwitchToken)).Count -ge 2
  Add-Check 'SELF' 'mock switch passed by the domain (AI-only test mode)' $mockSwitchPassed 'D1/D2 need --mock-handshake on both launch paths; absent => those forms SKIP' | Out-Null
  Add-Check 'SELF' 'installed app exe present' (Test-Path -LiteralPath $InstallExe) $InstallExe | Out-Null
  $procs = Get-SimProductProcesses
  Add-Check 'SELF' 'product not running (clean precondition for live)' ($procs.Count -eq 0) (@($procs | ForEach-Object { "$($_.name):$($_.pid)" }) -join ',') -Required $false
  Add-Check 'SELF' 'elevation state (live mode needs an admin session)' $true $(if (Get-SimElevated) { 'administrator' } else { 'standard user - live needs -Elevate' }) -Required $false
  return $true
}

function Invoke-SleepLive {
  <#
    live：真实 App + 真 Host（operator 授权窗口）。204 §2 口径：
      * 不调用 /api/handshake（它有副作用：改 HardwareCapable/AuthorizationGranted/FanRoute/DeviceIdentity）
      * 硬件门由无副作用读（GET /api/state）建立；未知/开启 ⇒ 不发任何变更调用（0 次）
      * App / FanHost / RecoveryService 分别结算，绑定 PID+创建时间+镜像身份
      * 关闭只对本轮自有进程；反推只认快照偏移之后追加的字节
      * 重启复验：旧 Host 未结算 ⇒ 不对新实例下结论；新 App 复用旧 Host ⇒ 标 reuse，不算新实例测试
    -SimulateRun 只演练编排（不启动 exe、不发请求），verdict=REHEARSAL。
  #>
  Write-Host '=== 睡眠域 live（真实 App + 真 Host；204 SIM-2/3/4 口径）===' -ForegroundColor White
  $plan = [ordered]@{
    startApp = [bool]$StartApp; attach = [bool]$Attach; simulate = [bool]$SimulateRun
    closeApp = (-not $KeepAppOpen); restartProbe = (-not $SkipRestartProbe)
    waitAppReadySec = $WaitAppReadySec; waitAppExitSec = $WaitAppExitSec
    appExe = $InstallExe; hostExe = $HostExe; sessionTokenPath = $SessionTokenPath
    handshakeUsed = $false
    edges = @('suspend', 'resume')
  }
  $script:Run.extra['livePlan'] = $plan

  if (-not $StartApp -and -not $Attach) {
    Add-Check 'LIVE' 'live mode declared its app handling (-StartApp or -Attach)' $false '需要显式 -StartApp（由本域开/关 App）或 -Attach（只对接已在运行的 Host）'
    return
  }
  $procs = Get-SimProductProcesses
  if ($StartApp -and $procs.Count -gt 0) {
    Add-Check 'LIVE' 'clean precondition before starting the app' $false ("已在运行：" + (@($procs | ForEach-Object { "$($_.name):$($_.pid)" }) -join ','))
    Add-SimEvent -Run $script:Run -Kind 'flag' -Detail 'product already running' -Data @{ key = 'PRODUCT_ALREADY_RUNNING' }
    return
  }
  if ($SimulateRun) {
    $simStart = Start-SimProductApp -ExePath $InstallExe -Simulate
    $simStop = Stop-SimAppOwn -Simulate
    $script:Run.extra['simulatedStart'] = $simStart
    $script:Run.extra['simulatedStop'] = $simStop
    Add-Check 'LIVE' 'rehearsal: orchestration resolved, nothing started' $true ("app={0} host={1} edges={2}" -f $InstallExe, $HostExe, ($plan.edges -join '/')) -Required $false
    Add-SimStatus -Run $script:Run -Scope 'live-test' -Status 'NOT_RUN' -Detail 'rehearsal only (-SimulateRun): no exe started, no request sent' -Required $false
    Add-SimEvent -Run $script:Run -Kind 'flag' -Detail 'rehearsal' -Data @{ key = 'REHEARSAL' }
    return
  }

  # ---- start / attach ----
  if ($StartApp) {
    if (-not (Get-SimElevated)) {
      Add-Check 'LIVE' 'elevated session (app manifest requiresAdministrator)' $false '请用 -Elevate（或管理员会话）重跑'
      Add-SimEvent -Run $script:Run -Kind 'flag' -Detail 'needs elevation' -Data @{ key = 'NEEDS_ELEVATION' }
      return
    }
    $start = Start-SimProductApp -ExePath $InstallExe
    $script:ScriptStartedApp = $true
    $script:OwnedAppIdentities += @($start.identity)
    $script:Run.extra['start'] = $start
    Add-Check 'LIVE' 'app started (identity: pid+creation+image)' ($start.launched -and $start.pid -and $start.identity) ("pid={0} mode={1} image={2}" -f $start.pid, $start.launchMode, $start.identity.imageSha256.Substring(0, 16))
    $ready = Wait-SimHostReady -Port $Port -TokenPath $SessionTokenPath -TimeoutSec $WaitAppReadySec
    $script:Run.extra['hostReady'] = $ready
    Add-Check 'LIVE' 'fan host became ready' ([bool]$ready.ready) ("port={0} status={1}" -f $Port, $ready.status)
    if (-not $ready.ready) { Add-SimEvent -Run $script:Run -Kind 'flag' -Detail 'host not ready' -Data @{ key = 'HOST_START_FAILED' }; return }
    $script:Token = Get-SimFanSessionToken $SessionTokenPath
  } elseif ($Attach) { Connect-ExistingHost }

  # ---- SIM-3 identity / session / instance snapshots ----
  $script:Run.identity['nativeSessionHash'] = Get-SimSessionHash
  $script:Run.identity['rolesBefore'] = Get-SimRoleSnapshotAll
  $script:Run.identity['hostInstancesBefore'] = @(Get-SimHostInstance -ExePath $HostExe)

  # ---- SIM-4 log window snapshot (taken BEFORE any change call) ----
  $d = Get-SimDefaults
  $script:LogSnapshot = Get-SimLogSnapshot -Paths @($d.fanLifecycleLog, $d.fanApiLog, $d.hostRuntimeLog, $d.nativeLifecycleLog, $d.virtualGamepadLog)
  $script:Run.identity['logSnapshot'] = $script:LogSnapshot

  # ---- SIM-2 gate (side-effect-free read only) ----
  $gate = Test-SimActionGate -Base $script:Base -Token $script:Token -Kind 'external'
  $script:GateAllowed = [bool]$gate.allowed
  $script:Run.extra['actionGate'] = $gate
  if ($gate.state) { $script:HostMode = $gate.state.hostMode }
  Add-Check 'LIVE' 'hardware gate permits simulated edges (side-effect-free read)' ([bool]$gate.allowed) ("reason={0} hwWrites={1} hostMode={2}" -f $gate.reason, $gate.state.hardwareWritesEnabled, $gate.state.hostMode)
  if (-not $gate.allowed) {
    Add-SimStatus -Run $script:Run -Scope 'edges' -Status 'BLOCKED' -Detail ("gate not established: " + $gate.reason) -Required $true
    Add-Check 'LIVE' 'no simulated transition is sent while the gate is unknown/open' ($script:ChangeCallCount -eq 0) ("changeCalls={0}" -f $script:ChangeCallCount)
    return
  }

  # ---- edges (counted change calls) ----
  $g = [int64]$GenerationBase
  if ($g -lt 1) { $g = 1 }
  $g = $g + 1
  $sEdge = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'LIVE' -Label 'forward-suspend' -ChangeCall
  Add-Check 'LIVE' 'forward sleep edge accepted' ($sEdge.status -eq 200 -and $sEdge.stateAfter.state -eq 'Suspended') ("gen=$g status=$($sEdge.status) $(State-Brief $sEdge.stateAfter)")
  $rEdge = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'LIVE' -Label 'forward-resume' -ChangeCall
  Add-Check 'LIVE' 'forward wake edge accepted' ($rEdge.status -eq 200 -and $rEdge.stateAfter.powerState -eq 'On') ("gen=$g status=$($rEdge.status) $(State-Brief $rEdge.stateAfter)")
  $script:Run.extra['stateAfterEdges'] = $rEdge.stateAfter
  Add-Check 'LIVE' 'change calls were actually sent and counted (>=2)' ($script:ChangeCallCount -ge 2) ("changeCalls={0}" -f $script:ChangeCallCount)

  # ---- SIM-4 reverse evidence: only bytes appended after the snapshot ----
  Start-Sleep -Seconds 2
  $rev = Get-SimReverseEvidence -Label 'live-run-1' -Snapshot $script:LogSnapshot -ExpectedMarkers @("$g", "gen=$g")
  $script:Run.extra['reverse'] = $rev
  Add-Check 'LIVE' 'reverse evidence bound to this run window (no historical-only pass)' ([bool]$rev.anyMatchThisRun) ("fresh-hit logs=" + ((@($rev.logs | Where-Object { $_.matchesThisRun } | ForEach-Object { $_.name })) -join ','))
  $markerSum = (@($rev.logs | ForEach-Object { @($_.markerMatched).Count } | Measure-Object -Sum).Sum)
  Add-Check 'LIVE' 'generation marker seen inside this run window' ([int]$markerSum -gt 0) ("expected marker=$g hits=$markerSum") -Required $false

  # ---- SIM-3/4: own-only close, then per-role settlement ----
  $close1 = Stop-SimAppOwn -ExpectedIdentities $script:OwnedAppIdentities -TimeoutSec $WaitAppExitSec
  $script:Run.extra['close1'] = $close1
  Add-Check 'LIVE' 'owned app instance closed (identity-matched, foreign refused)' ([bool]$close1.exited) ("posted=$($close1.posted) refusedForeign=" + (@($close1.refused).Count))
  $settle = [ordered]@{}
  foreach ($role in @('FanHost', 'RecoveryService')) {
    $settle[$role] = Wait-SimRoleSettled -Role $role -TimeoutSec 120 -ExpectedIdentities @($script:Run.identity['hostInstancesBefore'])
  }
  $script:Run.extra['settlementAfterClose1'] = $settle
  Add-Check 'LIVE' 'previous host instance settled (restart verdict is only valid afterwards)' ([bool]$settle['FanHost'].settled) ("remaining=" + (@($settle['FanHost'].remaining).Count))
  Add-Check 'LIVE' 'recovery service settled separately' ([bool]$settle['RecoveryService'].settled) ("remaining=" + (@($settle['RecoveryService'].remaining).Count)) -Required $false

  # ---- restart probe ----
  if (-not $SkipRestartProbe -and -not $KeepAppOpen) {
    if (-not $settle['FanHost'].settled) {
      Add-SimStatus -Run $script:Run -Scope 'restart-probe' -Status 'BLOCKED' -Detail 'previous host instance did not settle: no new-instance verdict is possible' -Required $true
    } else {
      $start2 = Start-SimProductApp -ExePath $InstallExe
      $script:OwnedAppIdentities += @($start2.identity)
      $script:Run.extra['start2'] = $start2
      Add-Check 'LIVE' 'app restarted for the recovery probe' ([bool]$start2.launched) ("pid={0}" -f $start2.pid)
      $ready2 = Wait-SimHostReady -Port $Port -TokenPath $SessionTokenPath -TimeoutSec $WaitAppReadySec
      $script:Run.extra['hostReady2'] = $ready2
      if ($ready2.ready) {
        $script:Token = Get-SimFanSessionToken $SessionTokenPath
        $inst2 = @(Get-SimHostInstance -ExePath $HostExe)
        $script:Run.identity['hostInstancesAfterRestart'] = $inst2
        $rv = Get-SimRestartVerdict -Before $script:Run.identity['hostInstancesBefore'] -After $inst2
        $reused = [bool]$rv.reuse
        $script:Run.identity['restartVerdict'] = $rv
        $script:Run.identity['hostInstanceReused'] = $reused
        Add-Check 'LIVE' 'restart produced a fresh host instance (not a reuse)' (-not $reused) ("verdict=$($rv.verdict) instancesAfter=" + (@($inst2).Count))
        if ($reused) {
          Add-SimStatus -Run $script:Run -Scope 'new-instance-test' -Status 'BLOCKED' -Detail 'the restarted app reuses the old host instance; reuse cannot prove a fresh instance' -Required $true
        }
        $snap2 = Get-SimLogSnapshot -Paths @($d.fanLifecycleLog, $d.fanApiLog, $d.hostRuntimeLog, $d.nativeLifecycleLog)
        $gate2 = Test-SimActionGate -Base $script:Base -Token $script:Token -Kind 'external'
        $script:Run.extra['actionGate2'] = $gate2
        Add-Check 'LIVE' 'gate permits edges on the restarted instance' ([bool]$gate2.allowed) ("reason=" + $gate2.reason) -Required $false
        if ($gate2.allowed) {
          $g2 = $g + 1
          $s2 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g2) -Form 'LIVE' -Label 'restart-suspend' -ChangeCall
          $r2 = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g2) -Form 'LIVE' -Label 'restart-resume' -ChangeCall
          Add-Check 'LIVE' 'edges accepted on the restarted instance' ($s2.status -eq 200 -and $r2.status -eq 200) ("sleep=$($s2.status) wake=$($r2.status) gen=$g2")
          Start-Sleep -Seconds 2
          $rev2 = Get-SimReverseEvidence -Label 'live-run-2-after-restart' -Snapshot $snap2 -ExpectedMarkers @("$g2")
          $script:Run.extra['reverse2'] = $rev2
          Add-Check 'LIVE' 'second-window reverse evidence bound to this run' ([bool]$rev2.anyMatchThisRun) 'fresh hits only'
        }
      } else {
        Add-Check 'LIVE' 'fresh host instance became ready' $false ("port={0} status={1}" -f $Port, $ready2.status)
        Add-SimEvent -Run $script:Run -Kind 'flag' -Detail 'restart host not ready' -Data @{ key = 'HOST_START_FAILED' }
      }
    }
  } else {
    Add-SimStatus -Run $script:Run -Scope 'restart-probe' -Status 'NOT_RUN' -Detail 'skipped (-SkipRestartProbe or -KeepAppOpen)' -Required $false
  }

  if (-not $KeepAppOpen) {
    $close2 = Stop-SimAppOwn -ExpectedIdentities $script:OwnedAppIdentities -TimeoutSec $WaitAppExitSec
    $script:Run.extra['close2'] = $close2
    Add-Check 'LIVE' 'all owned app instances are gone' ([bool]$close2.exited) ("posted=$($close2.posted) refusedForeign=" + (@($close2.refused).Count))
  }
}

function Invoke-SleepRetryBudget {
  <#
    retrybudget：故障预算/恢复重试的**可观测**验证（只走公开 API，不改产品，沙箱宿主）。
    源码锚点：FanLab/real-host/Program.cs:6867-6869（MaxAttempt=30 / Interval=2000ms / Window=60000ms）、
    L10045-10051（预算耗尽 → 终态）、L10118-10123（终态记录 attempts/window/state）。
    可观测契约（本模式断言）：
      1) 曲线会话入睡 ⇒ FaultLocked（不可证明的 OEM 恢复必须 fail-closed）
      2) 故障期唤醒 ⇒ 409（不伪造重建）
      3) 观察窗内状态收敛为**稳定结果**（不得无限重试）——判据：最后 ≥10 s 状态不再变化
      4) 期间任何 On 样本只能来自 mock 恢复，且绝不伪装成物理读回证据
      5) 操作者 release-control 是显式恢复路径（状态离开 FaultLocked）
  #>
  Write-Host '=== 故障预算/恢复重试（可观测验证）===' -ForegroundColor White
  $session = Open-MockSession
  Add-Check 'RETRY' 'session opened for the fault-budget probe' ($session.open.status -eq 200 -and $session.openEvents.status -eq 200 -and $session.leaseId) "lease=$([bool]$session.leaseId)"
  $curve = '{"nodes":[{"tempC":0,"dutyPercent":0},{"tempC":55,"dutyPercent":30},{"tempC":75,"dutyPercent":60},{"tempC":90,"dutyPercent":100}],"leaseId":"' + $session.leaseId + '"}'
  $en = Send-Api -Method POST -Path '/api/enable' -Body $curve -Form 'RETRY' -Label 'enable(mock curve)'
  Add-Check 'RETRY' 'masked curve enabled (write observed)' ($en.status -eq 200 -and $en.stateAfter.hardwareWritesObserved -eq $true) "hwWrites=$($en.stateAfter.hardwareWritesObserved)"
  $g = New-Generation
  $s = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'RETRY' -Label 'suspend-with-curve' -ChangeCall
  Add-Check 'RETRY' 'sleep with an unprovable restore fails closed (FaultLocked)' ($s.status -eq 200 -and $s.stateAfter.state -eq 'FaultLocked') "status=$($s.status) $(State-Brief $s.stateAfter)"
  $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'RETRY' -Label 'resume-during-fault' -ChangeCall
  Add-Check 'RETRY' 'resume during the fault is refused (no fabricated rebuild)' ($r.status -eq 409) "status=$($r.status) code=$($r.errorCode)"

  $samples = @()
  $deadline = (Get-Date).AddSeconds(75)
  $lastKey = ''
  $stableSince = Get-Date
  while ((Get-Date) -lt $deadline) {
    $st = Get-State
    $key = "$($st.state)/$($st.powerState)"
    if ($key -ne $lastKey) { $stableSince = Get-Date; $lastKey = $key }
    $samples += [ordered]@{ at = (Get-SimNowStamp); state = $st.state; powerState = $st.powerState; oemConfirmed = $st.oemRestoreConfirmed; oemEvidence = $st.oemRestoreEvidence }
    Start-Sleep -Seconds 5
  }
  $script:Run.extra['retryBudgetSamples'] = $samples
  $stableSec = ((Get-Date) - $stableSince).TotalSeconds
  Add-Check 'RETRY' 'state converges to a stable outcome inside the window (no unbounded retry)' ($stableSec -ge 10) ("stableFor={0:N0}s samples={1} final={2}" -f $stableSec, @($samples).Count, $lastKey)
  $onSamples = @($samples | Where-Object { $_.powerState -eq 'On' })
  Add-Check 'RETRY' 'any awake sample comes only from a mock recovery, never a physical readback claim' (@($onSamples | Where-Object { $_.oemEvidence -eq 'hc-default-table-readback-confirmed' }).Count -eq 0) ("onSamples={0} (mock-only evidence required)" -f @($onSamples).Count)
  $final = Get-State
  Add-Check 'RETRY' 'final evidence is not a physical HC readback masquerade' ($final.oemRestoreEvidence -ne 'hc-default-table-readback-confirmed') "state=$($final.state) oemConfirmed=$($final.oemRestoreConfirmed) evidence=$($final.oemRestoreEvidence)"
  $rel = Send-Api -Method POST -Path '/api/release-control' -Body ('{"leaseId":"' + $session.leaseId + '"}') -Form 'RETRY' -Label 'release-control(operator path)' -ChangeCall
  Add-Check 'RETRY' 'operator release resolves the fault (state leaves FaultLocked)' ($rel.status -eq 200 -and $rel.stateAfter.state -ne 'FaultLocked') "status=$($rel.status) $(State-Brief $rel.stateAfter)"

  # 204 §6 B/P2 第二半（实测契约，探针证据见 204-N 报告 §2）：
  #   ① release 是可达的操作者路径，且安全清理 /api/disable（无需 lease）在故障后仍可达、清曲线；
  #   ② 同一 host 世代内，一旦观测到硬件写入而 OEM 恢复未证（hardwareWritesObserved=true 且
  #      oemRestoreEvidence=not-observed），下一次入睡**必须继续 fail-closed**——绝不允许静默成功；
  #      若某天它真的睡了，则必须同时给出已证恢复（hc-default-table-readback-confirmed，真机值），否则判 FAIL。
  #   ③ "下一次正常操作确实成功"在新 host 世代上验证：tests\api_contract.ps1 AC12（含宿主重启）。
  $dis = Send-Api -Method POST -Path '/api/disable' -Body '{}' -Form 'RETRY' -Label 'disable(safety cleanup, no lease)' -ChangeCall
  Add-Check 'RETRY' 'safety cleanup stays reachable after the fault (disable clears the armed curve)' ($dis.status -eq 200) "status=$($dis.status) state=$($dis.stateAfter.state)"
  $g3 = New-Generation
  $s3 = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g3) -Form 'RETRY' -Label 'same-host retry after release' -ChangeCall
  $s3State = "$($s3.stateAfter.state)"
  $s3Ev = "$($s3.stateAfter.oemRestoreEvidence)"
  $silentSleep = ($s3.status -eq 200) -and ($s3State -eq 'Suspended') -and ($s3Ev -ne 'hc-default-table-readback-confirmed')
  Add-Check 'RETRY' 'no silent sleep while the hardware write stays unproven (same host generation must keep failing closed)' (-not $silentSleep) `
    ("status={0} state={1} oemEvidence={2} hwWrites={3}" -f $s3.status, $s3State, $s3Ev, $s3.stateAfter.hardwareWritesObserved)
}

# ------------------------------------------------------------------- main ----
$evidenceWritten = $false
$exitCode = 0
try {
  if (-not $Attach -and -not $StartHost) { $StartHost = $true }
  if ($Port -le 0) { $Port = if ($Attach -or $Mode -eq 'forms' -or $Mode -eq 'live') { $D.sandboxDefaultPort } else { $D.sandboxAltPort } }
  $script:Base = "http://127.0.0.1:$Port"
  if (-not $GenerationBase) { $GenerationBase = if ($Attach -or $Mode -eq 'live') { '1' } else { '900000' } }
  $script:GenBase = [int64]$GenerationBase
  if ($Mode -eq 'quick' -or $Mode -eq 'switch') {
    $persisted = Get-LastGeneration
    if ($persisted -gt $script:GenBase) { $script:GenBase = $persisted }
  }

  Write-Host '=== 睡眠域（ymcc_sim -Domain sleep；204 SIM-1..4 口径）===' -ForegroundColor White
  Write-Host ("mode={0} modeKind={1} port={2} timeScale={3} 证据目录={4}" -f $Mode, $script:Run.modeKind, $Port, $TimeScale, $EvidenceDir)
  $script:Run.extra['provenance'] = 'sleep_forms.json (real sleep-facts logs + code constants)'

  if ($Mode -eq 'selfcheck') {
    Invoke-SleepSelfCheck | Out-Null
  }
  elseif ($Mode -eq 'sleeptypes') {
    Invoke-SleepTypes
  }
  elseif ($Mode -eq 'live') {
    Invoke-SleepLive
  }
  else {
    if ($Attach) { Connect-ExistingHost }
    elseif ($Mode -eq 'forms' -or $Mode -eq 'retrybudget' -or $Mode -eq 'typeseries' -or $Mode -eq 'rapidwake') { Start-SandboxHost }
    elseif ($Mode -eq 'quick' -and $Action -eq 'stop') { Stop-SandboxHost }
    else { Ensure-PersistentSandboxHost }

    if (-not ($Mode -eq 'quick' -and $Action -eq 'stop')) {
      # SIM-2: gate first (side-effect-free), then handshake/actions
      $gateKind = $(if ($Attach) { 'external' } else { 'sandbox' })
      $gate = Test-SimActionGate -Base $script:Base -Token $script:Token -Kind $gateKind -SandboxFacts $script:SandboxFacts
      $script:GateAllowed = [bool]$gate.allowed
      $script:Run.extra['actionGate'] = $gate
      Add-Check 'GATE' 'hardware gate permits simulated edges (side-effect-free read)' ([bool]$gate.allowed) ("kind=$gateKind reason={0} hwWrites={1} hostMode={2}" -f $gate.reason, $gate.state.hardwareWritesEnabled, $gate.state.hostMode)
      if ($gate.allowed) {
        # the handshake IS a side effect (mock mode rewrites capability/route/identity) - it is only used on a
        # host instance this run started itself, and never for an attached/external host
        $hs = Send-Api -Method POST -Path '/api/handshake' -Body '{}' -NoStateAfter -Form 'RUN' -Label 'handshake(sandbox-only)'
        $script:HostMode = $hs.json.hostMode
        Add-Check 'RUN' 'sandbox handshake completed (self-started instance)' ($hs.status -eq 200 -and $hs.json.ok -eq $true) ("status=$($hs.status) hostMode=$($script:HostMode) hardwareWritesEnabled=$($hs.json.hardwareWritesEnabled)") -Required $false
      } else {
        Add-SimStatus -Run $script:Run -Scope 'actions' -Status 'BLOCKED' -Detail ("gate not established: " + $gate.reason) -Required $true
      }
    }

    if (-not $script:GateAllowed) {
      Write-Host '（门未建立：本模式下不发任何变更调用）' -ForegroundColor Yellow
    }
    elseif ($Mode -eq 'retrybudget') {
      Invoke-SleepRetryBudget
    }
    elseif ($Mode -eq 'typeseries') {
      Invoke-SleepTypeSeries
    }
    elseif ($Mode -eq 'rapidwake') {
      Invoke-SleepRapidWake
    }
    elseif ($Mode -eq 'quick') {
      switch ($Action) {
        'status' {
          $st = Send-Api -Method GET -Path '/api/state' -NoStateAfter -Form 'QUICK' -Label 'status'
          Write-Host ("状态：{0}" -f (State-Brief $st.json.state)) -ForegroundColor Green
        }
        'sleep' {
          $g = New-Generation
          $r = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'QUICK' -Label 'sleep' -ChangeCall
          Save-LastGeneration $g
          Add-Check 'QUICK' 'simulated sleep accepted' ($r.status -eq 200) ("status=$($r.status) $(State-Brief $r.stateAfter) changeCalls=$($script:ChangeCallCount)")
        }
        'wake' {
          $g = Get-LastGeneration
          if ($g -le 0) { $g = New-Generation }
          $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'QUICK' -Label 'wake' -ChangeCall
          Add-Check 'QUICK' 'simulated wake accepted' ($r.status -eq 200) ("status=$($r.status) $(State-Brief $r.stateAfter) changeCalls=$($script:ChangeCallCount)")
        }
      }
    }
    elseif ($Mode -eq 'switch') {
      Write-Host '交互开关：[s] 睡眠（模拟） [w] 唤醒（模拟） [t] 状态 [q] 退出' -ForegroundColor White
      $queue = @()
      if ($Keys) { $queue = @($Keys -split '[,;\s]+' | Where-Object { $_ }) }
      while ($true) {
        $key = ''
        if ($queue.Count -gt 0) {
          $key = [string]$queue[0]
          $queue = @($queue | Select-Object -Skip 1)
          Write-Host ("  > {0}" -f $key)
        } else {
          try { $key = (Read-Host 'choice').Trim().ToLowerInvariant() }
          catch { Write-Host '（非交互会话：无 -Keys 输入，退出开关）' -ForegroundColor DarkGray; break }
        }
        $key = $key.Trim().ToLowerInvariant()
        if ($key -eq 'q') { break }
        elseif ($key -eq 't') {
          $st = Send-Api -Method GET -Path '/api/state' -NoStateAfter -Form 'SWITCH' -Label 'status'
          Write-Host ("  {0}" -f (State-Brief $st.json.state))
        }
        elseif ($key -eq 's') {
          $g = New-Generation
          $r = Send-Api -Method POST -Path '/api/suspend' -Body (Suspend-Body $g) -Form 'SWITCH' -Label 'sleep' -ChangeCall
          Save-LastGeneration $g
          Write-Host ("  sleep -> status={0} {1}" -f $r.status, (State-Brief $r.stateAfter)) -ForegroundColor Green
        }
        elseif ($key -eq 'w') {
          $g = Get-LastGeneration
          if ($g -le 0) { $g = New-Generation }
          $r = Send-Api -Method POST -Path '/api/resume' -Body (Resume-Body $g) -Form 'SWITCH' -Label 'wake' -ChangeCall
          Write-Host ("  wake -> status={0} {1}" -f $r.status, (State-Brief $r.stateAfter)) -ForegroundColor Green
        }
      }
    }
    else {
      $selected = if ($Forms -eq 'all') { @($FormScripts.Keys) } else { @($Forms -split '[,; ]+' | Where-Object { $_ }) }
      foreach ($id in $selected) {
        if (-not $FormScripts.Contains($id)) { Write-Host ("未知形态：{0}（可用：{1}）" -f $id, ($FormScripts.Keys -join ',')) -ForegroundColor Red; Add-SimCheck -Run $script:Run -Form 'RUN' -Name ("unknown form {0}" -f $id) -Ok $false -Detail 'form id not implemented' -Required $true; continue }
        Invoke-Form $id $FormScripts[$id]
      }
      $failed = @($script:Checks | Where-Object { -not $_.passed -and $_.required })
      $passedForms = @($script:FormVerdicts | Where-Object { $_.verdict -eq 'PASS' })
      $failedForms = @($script:FormVerdicts | Where-Object { $_.verdict -eq 'FAIL' })
      $skippedForms = @($script:FormVerdicts | Where-Object { $_.verdict -eq 'SKIP' })
      Write-Host ''
      Write-Host ("形态结果：PASS {0} / SKIP {1} / FAIL {2}；必需检查失败 {3}" -f $passedForms.Count, $skippedForms.Count, $failedForms.Count, $failed.Count) -ForegroundColor $(if ($failed.Count -gt 0) { 'Red' } else { 'Green' })
    }
  }
}
catch {
  Write-Host ("执行中止：{0}" -f $_.Exception.Message) -ForegroundColor Red
  Add-SimCheck -Run $script:Run -Form 'RUN' -Name 'run completed without exception' -Ok $false -Detail $_.Exception.Message -Required $true
  Add-SimEvent -Run $script:Run -Kind 'exception' -Detail $_.Exception.Message
}
finally {
  if ($Mode -eq 'live' -and $script:ScriptStartedApp -and -not $KeepAppOpen -and -not $SimulateRun) {
    $still = @(Get-SimProcessesByRole 'App')
    $mine = @()
    foreach ($p in $still) {
      if (Test-SimIdentityOwned -Candidate $p -Expected $script:OwnedAppIdentities) { $mine += $p }
    }
    if ($mine.Count -gt 0) {
      Write-Host '（异常收尾）本轮自有 App 实例仍在运行 → 走 WM_APP_EXIT 关闭（外来同名进程不动）。' -ForegroundColor Yellow
      $script:Run.extra['closeOnAbort'] = Stop-SimAppOwn -ExpectedIdentities $script:OwnedAppIdentities -TimeoutSec $WaitAppExitSec
    }
  }
  $shutdown = $null
  if ($script:StartedByUs -and -not $SkipShutdown -and ($Mode -eq 'forms' -or $script:ShutdownOwnHost)) {
    try { $shutdown = Send-SimFanApi -Method POST -Path '/api/shutdown' -Base $script:Base -Token $script:Token -Body '{}' -TimeoutMs $RequestTimeoutMs } catch { $shutdown = $null }
    for ($i = 0; $i -lt 30; $i++) { if ($script:HostProc.HasExited) { break }; Start-Sleep -Milliseconds 200 }
  }
  $hostExit = $null
  if ($script:StartedByUs) { $hostExit = [ordered]@{ pid = $script:HostProc.Id; exited = [bool]$script:HostProc.HasExited; identity = $script:SandboxFacts.hostIdentity } }
  $script:Run.extra['toolIdentity'] = Get-ThisToolIdentity
  $script:Run.extra['generationBase'] = $script:GenBase
  $script:Run.extra['hostIdentity'] = $script:HostIdentity
  $script:Run.extra['mockHandshakeFlag'] = $script:MockFlag
  $script:Run.extra['hostMode'] = $script:HostMode
  $script:Run.extra['startedByUs'] = $script:StartedByUs
  $script:Run.extra['sandboxFacts'] = $script:SandboxFacts
  $script:Run.extra['hostExit'] = $hostExit
  $script:Run.extra['shutdownResponse'] = $(if ($shutdown) { [ordered]@{ status = $shutdown.status; raw = $shutdown.raw } } else { $null })
  $script:Run.extra['longSleepStatus'] = $script:LongSleepStatus
  $script:Run.extra['attachWarning'] = $(if ($Attach -or $Mode -eq 'live') { 'Host 电源代次已被推进；必须重启 App 才能让 App 自身的电源边沿重新生效。' } else { $null })
  $script:Run.extra['changeCallCount'] = $script:ChangeCallCount
  $script:Run.realSleepPerformed = $false
  $script:Run.hardwareActionPerformed = $false
  $script:Run.changeCallCount = $script:ChangeCallCount
  $code = Complete-SimRun -Run $script:Run -EvidenceDir $EvidenceDir -Prefix 'sleep-sim'
  $evidenceWritten = $true
  if ($script:TempTokenFile -and (Test-Path -LiteralPath $script:TempTokenFile)) { Remove-Item -LiteralPath $script:TempTokenFile -Force -ErrorAction SilentlyContinue }
  exit $code
}
exit 3
