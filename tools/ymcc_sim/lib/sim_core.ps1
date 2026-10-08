# =============================================================================
# ymcc_sim core library v2  (tools\ymcc_sim\lib\sim_core.ps1)   2026-09-22 (batch 204-A)
#
# v2 adds the four trust fixes demanded by 204 §2 (SIM-1..SIM-4):
#   SIM-1  ONE result aggregator: required-check failures / exceptions / timeouts / missing summary /
#          child process without an exit code can never exit 0; the JSON verdict, the counts and the
#          process exit code are produced by that same function (Complete-SimRun).
#   SIM-2  the hardware gate is an ACTION PRECONDITION, read through SIDE-EFFECT FREE reads
#          (GET /api/state, GET /health). /api/handshake is NOT side-effect free (it sets
#          HardwareCapable/AuthorizationGranted/FactoryType/FanRoute/DeviceIdentity in mock mode) and is
#          only used inside a host instance this run started itself. Unknown/real gate => no change calls.
#   SIM-3  App / FanHost / RecoveryService are settled separately, bound to pid + creation time + image
#          identity (hash), plus hostInstanceId / nativeSessionHash. A restart that reuses the old host is
#          recorded as reuse and may not serve as a new-instance test.
#   SIM-4  cleanup only touches processes this run started (or explicitly adopted), matched by identity;
#          reverse logs are bound to this run's offsets/time/session/generation and historical hits never pass.
#
# Hard rules kept from v1: never pass --real-backend; never stop foreign processes; record files are
# written with [IO.File]::WriteAllText (UTF-8, no BOM); PowerShell 5.1 only (no ternary, wrap in @()).
# =============================================================================

$script:SimCoreVersion = 'ymcc_sim core v2 (2026-09-22, batch 204-A)'

# Module-path self-heal (measured 2026-09-22): when a child PowerShell 5.1 process inherits a PSModulePath whose
# first entries belong to PowerShell 7, autoloading of Microsoft.PowerShell.Utility can fail and cmdlets such as
# Get-FileHash / Get-Content disappear inside this very script ("CommandNotFoundException"). Prepend $PSHOME\Modules
# so the 5.1 modules win. Idempotent.
$simHomeModules = Join-Path $PSHOME 'Modules'
$simFirstEntry = ($env:PSModulePath -split ';')[0]
if (-not $simFirstEntry -or ($simFirstEntry.TrimEnd('\') -ine $simHomeModules.TrimEnd('\'))) {
  # the Windows PowerShell module dir must be FIRST, not merely present: a leading PowerShell-7 entry shadows
  # Microsoft.PowerShell.Utility and Get-FileHash/Get-Content disappear (measured)
  $env:PSModulePath = $simHomeModules + ';' + $env:PSModulePath
}

# exit code table (single source: Complete-SimRun)
$script:SimExitCodes = [ordered]@{
  ALL_PASS                  = 0
  CHECK_FAIL                = 2
  ARG_OR_PREFLIGHT          = 3
  PRODUCT_ALREADY_RUNNING   = 4
  NOT_IMPLEMENTED           = 5
  NEEDS_ELEVATION           = 6
  HOST_START_FAILED         = 7
  TIMEOUT                   = 8
  EVIDENCE_INCOMPLETE_STALE = 9
  BLOCKED_REQUIRED          = 10
  REHEARSAL                 = 12   # a run that declared itself a rehearsal: nothing started, no request sent
}
# first match wins (REHEARSAL is deliberately last: any real failure still wins)
$script:SimExitPrecedence = @('ARG_OR_PREFLIGHT', 'PRODUCT_ALREADY_RUNNING', 'NEEDS_ELEVATION',
  'HOST_START_FAILED', 'TIMEOUT', 'BLOCKED_REQUIRED', 'EVIDENCE_INCOMPLETE_STALE', 'CHECK_FAIL', 'REHEARSAL')

function Get-SimDefaults {
  $selfDir = Split-Path -Parent $PSCommandPath
  if (-not $selfDir) { $selfDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
  $simRoot = Split-Path -Parent $selfDir
  $toolsDir = Split-Path -Parent $simRoot
  $localApp = Join-Path $env:LOCALAPPDATA 'YeManCC'
  # 2026-09-23（安装位刷新后的测试前提修正）：正式导出物按设计**不含** lane 的 mock-handshake.flag，
  # 安装位被正式包刷新后 FanHost 进入"真装/无 mock"模式 ⇒ 依赖 mock 的断言会被 fail-closed 拒绝（这是对的）。
  # 用 YEMAN_SIM_PAYLOAD_ROOT 指向一份 **lane 载荷副本**（与安装位逐字节同二进制 + 放入 mock 旗标），
  # 即可对"同一载荷"跑 mock 类生命周期电池，且不写安装位。默认（未设变量）= 安装位原路径，行为不变。
  $payloadOverride = "$env:YEMAN_SIM_PAYLOAD_ROOT"
  $hostExePath = 'C:\SOFT\YeMan\PowerControl\fan-host\YeManFanHost.exe'
  $hostDllPath = 'C:\SOFT\YeMan\PowerControl\fan-host\YeManFanHost.dll'
  $mockFlagPath = 'C:\SOFT\YeMan\PowerControl\fan-host\mock-handshake.flag'
  if ($payloadOverride) {
    $hostExePath = Join-Path $payloadOverride 'YeManFanHost.exe'
    $hostDllPath = Join-Path $payloadOverride 'YeManFanHost.dll'
    $mockFlagPath = Join-Path $payloadOverride 'mock-handshake.flag'
  }
  return [ordered]@{
    installRoot        = 'C:\SOFT\YeMan'
    appExe             = 'C:\SOFT\YeMan\YeManCC\YeManCC.exe'
    hostExe            = $hostExePath
    hostDll            = $hostDllPath
    mockFlag           = $mockFlagPath
    payloadOverride    = $payloadOverride
    settingsPath       = 'C:\SOFT\YeMan\PowerControl\yeman-settings.json'
    sleepFactsLog      = 'C:\SOFT\YeMan\PowerControl\Sleep\sleep-facts.log'
    localAppData       = $localApp
    sessionTokenPath   = (Join-Path $localApp 'fan-host\YeManFanHost.session')
    hostRuntimeLog     = (Join-Path $localApp 'fan-host\logs\yeman-fan-host-runtime.log')
    fanLifecycleLog    = (Join-Path $localApp 'fan-lifecycle.log')
    fanApiLog          = (Join-Path $localApp 'fan-api.log')
    nativeLifecycleLog = (Join-Path $localApp 'native-lifecycle.log')
    virtualGamepadLog  = (Join-Path $localApp 'virtual-gamepad.log')
    evidenceDir        = (Join-Path $localApp 'sim-evidence')
    simRoot            = $simRoot
    toolsDir           = $toolsDir
    selfloopTool       = (Join-Path $toolsDir 'virtual_gamepad_selfloop.ps1')
    msiRulesPath       = (Join-Path $simRoot 'domains\msi\msi_rules.json')
    msiFixturesDir     = (Join-Path $simRoot 'domains\msi\fixtures')
    productProcessNames = @('YeManCC', 'YeManFanHost', 'YeManInputHost', 'YeManRecoveryService', 'YeManLightSetter')
    roleMap            = [ordered]@{ App = 'YeManCC'; FanHost = 'YeManFanHost'; RecoveryService = 'YeManRecoveryService'; InputHost = 'YeManInputHost'; LightSetter = 'YeManLightSetter' }
    sandboxDefaultPort = 8765
    sandboxAltPort     = 8766
    appExitMessage     = 0x0406
    appExitMessageAlt  = 0x0806
  }
}

# ------------------------------------------------------------------ identity --
function Get-SimFileIdentity {
  param([string]$Path, [string]$Label = '')
  $exists = $false; $bytes = 0; $sha = 'missing'; $mtime = $null
  if ($Path -and (Test-Path -LiteralPath $Path -PathType Leaf)) {
    $exists = $true
    $item = Get-Item -LiteralPath $Path
    $bytes = $item.Length
    $mtime = $item.LastWriteTime.ToString('o')
    $sha = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
  }
  return [ordered]@{ label = $Label; path = $Path; exists = $exists; bytes = $bytes; sha256 = $sha; mtime = $mtime }
}

function Get-SimTokenHash {
  # secrets never enter the evidence: only a hash of the session token is recorded
  param([string]$Token)
  if (-not $Token) { return $null }
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Token))).Replace('-', '')) }
  finally { $sha.Dispose() }
}

function Get-SimElevated {
  return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-SimNowStamp { return (Get-Date).ToString('o') }

# ------------------------------------------------------------------- process --
function Get-SimProductProcesses {
  # Returns the array directly (may be empty). MEASURED TRAP (batch 33): returning ",$list" and wrapping the call
  # in @() yields ONE element (the empty array) even when nothing matches - every role then looked "still alive"
  # and settlement/foreign checks could never pass. Callers must wrap with @(f) or read .Count; do NOT rely on a
  # leading comma here.
  $d = Get-SimDefaults
  $list = @(Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $d.productProcessNames -contains $_.ProcessName } |
    ForEach-Object { [ordered]@{ pid = $_.Id; name = $_.ProcessName } })
  return $list
}

function Get-SimProcessIdentity {
  # FULL identity of one process: pid + creation time + image path + image hash. Used for SIM-3 (settlement)
  # and SIM-4 (own-process-only cleanup).
  param([int]$ProcessId, [string]$Role = '')
  $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
  if (-not $p) { return [ordered]@{ role = $Role; pid = $ProcessId; alive = $false } }
  $img = $null; $sha = 'unavailable'
  try { $img = $p.Path } catch { $img = $null }
  try { if ($img -and (Test-Path -LiteralPath $img)) { $sha = (Get-FileHash -LiteralPath $img -Algorithm SHA256).Hash } } catch { $sha = 'unavailable' }
  return [ordered]@{
    role = $Role; pid = $p.Id; name = $p.ProcessName; alive = $true
    creationTime = $(try { $p.StartTime.ToString('o') } catch { $null })
    imagePath = $img; imageSha256 = $sha
  }
}

function Get-SimProcessesByRole {
  param([string]$Role)
  $d = Get-SimDefaults
  $name = $d.roleMap[$Role]
  $out = @()
  foreach ($p in @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -eq $name })) {
    $out += (Get-SimProcessIdentity -ProcessId $p.Id -Role $Role)
  }
  return $out   # empty => caller's @() gives 0 (previous ",$out" form produced a phantom element)
}

function Get-SimRoleSnapshotAll {
  # SIM-3: per-role snapshot (App / FanHost / RecoveryService / InputHost / LightSetter) with full identity
  $roles = @('App', 'FanHost', 'RecoveryService', 'InputHost', 'LightSetter')
  $map = [ordered]@{}
  foreach ($r in $roles) {
    $map[$r] = @(Get-SimProcessesByRole $r)
  }
  return $map
}

function Wait-SimRoleSettled {
  # SIM-3: settle ONE role. Returns whether the *specific* identities disappeared and what is left.
  param([string]$Role, [int]$TimeoutSec = 60, [array]$ExpectedIdentities)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $left = @()
  while ($true) {
    $left = @(Get-SimProcessesByRole $Role)
    if ($left.Count -eq 0) { break }
    if ((Get-Date) -ge $deadline) { break }
    Start-Sleep -Milliseconds 500
  }
  $foreign = @()
  if ($ExpectedIdentities -and @($ExpectedIdentities).Count -gt 0) {
    # identity-based settlement: only identities we started count; a same-name different-image/time process
    # is foreign and is reported, never claimed as ours
    $mine = @($ExpectedIdentities | ForEach-Object { $_.pid })
    $foreign = @($left | Where-Object { $mine -notcontains $_.pid })
  }
  return [ordered]@{ role = $Role; settled = ($left.Count -eq 0); remaining = $left; foreignRemaining = $foreign; waitedAt = (Get-SimNowStamp) }
}

# ---------------------------------------------------------------------- http --
if (-not ([System.Management.Automation.PSTypeName]'SimFanHttp').Type) {
  Add-Type -AssemblyName System.Net.Http -ErrorAction SilentlyContinue
}

function Send-SimFanApi {
  param(
    [Parameter(Mandatory = $true)][string]$Method,
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$Base = 'http://127.0.0.1:8765',
    [string]$Token,
    [string]$Body = '{}',
    [int]$TimeoutMs = 20000,
    [switch]$IsChangeCall    # SIM-2: change calls are counted and gated
  )
  $client = [System.Net.Http.HttpClient]::new()
  $client.Timeout = [TimeSpan]::FromMilliseconds($TimeoutMs)
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  try {
    $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::$Method, "$Base$Path")
    if ($Token) { $request.Headers.Add('X-YeMan-Fan-Session', $Token) }
    if ($Method -ne 'GET') { $request.Content = [System.Net.Http.StringContent]::new($Body, [Text.Encoding]::UTF8, 'application/json') }
    $response = $client.SendAsync($request).Result
    $raw = $response.Content.ReadAsStringAsync().Result
    $json = $null
    try { if ($raw) { $json = $raw | ConvertFrom-Json } } catch { $json = $null }
    $sw.Stop()
    $errorCode = $null
    if ($json -and $json.error) { $errorCode = $json.error.code }
    return [pscustomobject]@{
      method = $Method; path = $Path; base = $Base; body = $Body
      status = [int]$response.StatusCode; ms = [int]$sw.ElapsedMilliseconds
      raw = $raw; json = $json; errorCode = $errorCode; changeCall = [bool]$IsChangeCall
      deduplicated = $(if ($json) { $json.deduplicated } else { $null })
      resumeNoop = $(if ($json) { $json.resumeNoop } else { $null })
      reason = $(if ($json) { $json.reason } else { $null })
    }
  } catch {
    $sw.Stop()
    return [pscustomobject]@{
      method = $Method; path = $Path; base = $Base; body = $Body
      status = -1; ms = [int]$sw.ElapsedMilliseconds; raw = ''; json = $null
      errorCode = 'SIM_REQUEST_FAILED'; errorMessage = $_.Exception.Message
      changeCall = [bool]$IsChangeCall; deduplicated = $null; resumeNoop = $null; reason = 'request-failed'
    }
  } finally { $client.Dispose() }
}

function Get-SimStateSafe {
  # SIM-2: the ONLY precondition read source. GET /api/state does not mutate the engine.
  param([string]$Base, [string]$Token, [int]$TimeoutMs = 8000)
  $r = Send-SimFanApi -Method GET -Path '/api/state' -Base $Base -Token $Token -TimeoutMs $TimeoutMs
  return $r
}

function Test-SimActionGate {
  <#
    SIM-2. Decides whether simulated power transitions may be sent, using side-effect-free reads only.
      kind = 'sandbox'  -> a host instance THIS run started (launchArgs recorded, no --real-backend)
      kind = 'external' -> a host we attach to (must prove a masked gate through /api/state)
    Returns allowed=false with a reason whenever the gate is unknown or open. Never calls /api/handshake.
  #>
  param(
    [string]$Base, [string]$Token,
    [ValidateSet('sandbox', 'external')][string]$Kind = 'external',
    [hashtable]$SandboxFacts    # for kind=sandbox: @{ realBackendPassed=$false; launchedByRun=$true; launchArgs=@(...) }
  )
  $facts = [ordered]@{ kind = $Kind; checkedAt = (Get-SimNowStamp); reads = @(); allowed = $false; reason = $null; state = $null }
  $st = Get-SimStateSafe -Base $Base -Token $Token
  $facts.reads += [ordered]@{ path = '/api/state'; status = $st.status; raw = $st.raw }
  if ($st.status -ne 200 -or -not $st.json -or -not $st.json.state) {
    $facts.reason = 'state-read-failed: cannot establish the hardware gate without a side-effect-free read'
    return $facts
  }
  $s = $st.json.state
  $facts.state = [ordered]@{
    state = $s.state; powerState = $s.powerState; hostMode = $s.hostMode
    hardwareWritesEnabled = $s.hardwareWritesEnabled; hardwareWritesObserved = $s.hardwareWritesObserved
    hardwareCapable = $s.hardwareCapable; temperatureSource = $s.temperatureSource
  }
  if ($null -eq $s.hardwareWritesEnabled) {
    $facts.reason = 'gate-unknown: hardwareWritesEnabled absent from /api/state'
    return $facts
  }
  if ($s.hardwareWritesEnabled -ne $false) {
    $facts.reason = ("gate-open: hardwareWritesEnabled={0} -> simulated transitions refused" -f $s.hardwareWritesEnabled)
    return $facts
  }
  if ($Kind -eq 'external') {
    if ("$($s.hostMode)" -notmatch 'mock|safe') {
      $facts.reason = ("masked-identity-unknown: hostMode='{0}' is neither mock nor safe" -f $s.hostMode)
      return $facts
    }
  } else {
    if (-not $SandboxFacts -or -not $SandboxFacts.launchedByRun) {
      $facts.reason = 'sandbox-ownership-missing: host was not started by this run'
      return $facts
    }
    if ($SandboxFacts.realBackendPassed -ne $false) {
      $facts.reason = 'sandbox-real-backend: launch args are not proven free of --real-backend'
      return $facts
    }
  }
  $facts.allowed = $true
  $facts.reason = 'masked gate confirmed (hardwareWritesEnabled=false) via side-effect-free read'
  return $facts
}

# ------------------------------------------------------------------ app/roles --
if (-not ([System.Management.Automation.PSTypeName]'SimAppWin32').Type) {
  Add-Type @'
using System;
using System.Runtime.InteropServices;
public class SimAppWin32 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  public static int Post(int targetPid, uint msg) {
    int n = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == (uint)targetPid) { PostMessage(h, msg, IntPtr.Zero, IntPtr.Zero); n++; }
      return true;
    }, IntPtr.Zero);
    return n;
  }
}
'@
}

function Start-SimProductApp {
  param([string]$ExePath, [switch]$Simulate, [int]$WaitAppearSec = 30)
  $d = Get-SimDefaults
  if (-not $ExePath) { $ExePath = $d.appExe }
  $elevated = Get-SimElevated
  $result = [ordered]@{
    exe = $ExePath; exists = (Test-Path -LiteralPath $ExePath); elevatedSession = $elevated
    simulated = [bool]$Simulate; launched = $false; launchMode = 'none'; pid = $null; note = $null
    identity = $null
  }
  if (-not $result.exists) { $result.note = 'exe-missing'; return [pscustomobject]$result }
  if ($Simulate) { $result.note = 'simulated (no exe started)'; return [pscustomobject]$result }
  if ($elevated) {
    $p = Start-Process -FilePath $ExePath -PassThru
    $result.launched = $true; $result.launchMode = 'direct'; $result.pid = $p.Id
  } else {
    Start-Process -FilePath $ExePath -Verb RunAs | Out-Null
    $result.launched = $true; $result.launchMode = 'runas-prompt'; $result.note = 'UAC prompt shown; PID resolved by name'
    for ($i = 0; $i -lt ($WaitAppearSec * 2); $i++) {
      $proc = @(Get-Process -Name 'YeManCC' -ErrorAction SilentlyContinue)
      if ($proc.Count -gt 0) { $result.pid = $proc[0].Id; break }
      Start-Sleep -Milliseconds 500
    }
  }
  if ($result.pid) { $result.identity = Get-SimProcessIdentity -ProcessId $result.pid -Role 'App' }
  return [pscustomobject]$result
}

function Wait-SimHostReady {
  param([int]$Port = 8765, [string]$TokenPath, [int]$TimeoutSec = 150)
  $d = Get-SimDefaults
  if (-not $TokenPath) { $TokenPath = $d.sessionTokenPath }
  $base = "http://127.0.0.1:$Port"
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $last = $null
  while ((Get-Date) -lt $deadline) {
    $token = Get-SimFanSessionToken $TokenPath
    if ($token -and (Test-SimPort $Port)) {
      try {
        $h = Send-SimFanApi -Method GET -Path '/health' -Base $base -Token $token -TimeoutMs 5000
        $last = $h
        if ($h.status -eq 200) {
          return [ordered]@{ ready = $true; port = $Port; tokenPath = $TokenPath; status = $h.status; raw = $h.raw; waitedAt = (Get-SimNowStamp) }
        }
      } catch { }
    }
    Start-Sleep -Milliseconds 500
  }
  return [ordered]@{ ready = $false; port = $Port; tokenPath = $TokenPath; lastStatus = $(if ($last) { $last.status } else { $null }); waitedAt = (Get-SimNowStamp) }
}

function Test-SimIdentityOwned {
  # SIM-4 pure predicate: an identity is OURS only when pid AND creation time match. Used by the close path
  # (Stop-SimAppOwn) and by the abort safety net; exercised directly by the tool-gate negative for
  # "PID reuse / foreign same-name process".
  param($Candidate, $Expected)
  if (-not $Candidate) { return $false }
  foreach ($e in @($Expected)) {
    if (-not $e) { continue }
    if ([int]$e.pid -eq [int]$Candidate.pid -and "$($e.creationTime)" -eq "$($Candidate.creationTime)") { return $true }
  }
  return $false
}

function Get-SimRestartVerdict {
  # SIM-3 pure predicate: did the restarted app reuse the previous host instance? Reuse may NOT be reported as
  # a fresh-instance test. Used by live and exercised by the tool-gate negative for "old host answer".
  param($Before, $After)
  $reuse = $false
  foreach ($a in @($After)) {
    if (-not $a -or -not $a.instanceId) { continue }
    foreach ($b in @($Before)) {
      if (-not $b -or -not $b.instanceId) { continue }
      if ("$($a.instanceId)" -eq "$($b.instanceId)") { $reuse = $true }
    }
  }
  return [ordered]@{ reuse = $reuse; verdict = $(if ($reuse) { 'REUSE' } else { 'FRESH' }); beforeCount = @($Before).Count; afterCount = @($After).Count }
}

function Stop-SimAppOwn {
  <#
    SIM-4: graceful close of the app instances THIS run started (identity matched by pid+creationTime).
    Foreign same-name processes are never touched and are reported instead.
  #>
  param([array]$ExpectedIdentities, [int]$TimeoutSec = 60, [switch]$Simulate)
  $d = Get-SimDefaults
  $res = [ordered]@{ simulated = [bool]$Simulate; targets = @(); refused = @(); posted = 0; postedAlt = 0; exited = $false; timeline = @() }
  $expected = @()
  foreach ($e in @($ExpectedIdentities)) { if ($e -and $e.pid) { $expected += $e } }
  $live = @(Get-SimProcessesByRole 'App')
  foreach ($p in $live) {
    if (Test-SimIdentityOwned -Candidate $p -Expected $expected) { $res.targets += $p } else { $res.refused += $p }
  }
  if ($res.targets.Count -eq 0) { $res.exited = $true; $res.timeline += 'no owned app instance to close'; return [pscustomobject]$res }
  if ($Simulate) { $res.timeline += 'simulated: would post WM_APP_EXIT to owned app window(s)'; return [pscustomobject]$res }
  foreach ($t in $res.targets) {
    $n = [SimAppWin32]::Post([int]$t.pid, [uint32]$d.appExitMessage)
    $res.posted += $n
    $res.timeline += ("posted 0x{0:X} to {1} window(s) of owned pid {2}" -f $d.appExitMessage, $n, $t.pid)
  }
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $still = @($res.targets | Where-Object { Get-Process -Id $_.pid -ErrorAction SilentlyContinue })
    if ($still.Count -eq 0) { $res.exited = $true; break }
    Start-Sleep -Milliseconds 500
  }
  if (-not $res.exited) {
    foreach ($t in $res.targets) { $res.postedAlt += [SimAppWin32]::Post([int]$t.pid, [uint32]$d.appExitMessageAlt) }
    $res.timeline += ("fallback posted 0x{0:X}" -f $d.appExitMessageAlt)
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
      $still = @($res.targets | Where-Object { Get-Process -Id $_.pid -ErrorAction SilentlyContinue })
      if ($still.Count -eq 0) { $res.exited = $true; break }
      Start-Sleep -Milliseconds 500
    }
  }
  return [pscustomobject]$res
}

# ------------------------------------------------------ host instance identity --
function Get-SimHostInstance {
  # SIM-3: hostInstanceId = pid + creation time + image hash of the fan-host process (the *instance*, not the name)
  param([string]$ExePath)
  $d = Get-SimDefaults
  if (-not $ExePath) { $ExePath = $d.hostExe }
  $list = @()
  foreach ($p in @(Get-SimProcessesByRole 'FanHost')) {
    $list += [ordered]@{ pid = $p.pid; creationTime = $p.creationTime; imagePath = $p.imagePath; imageSha256 = $p.imageSha256
                         instanceId = ("{0}:{1}" -f $p.pid, $p.creationTime) }
  }
  return $list   # empty => caller's @() gives 0
}

function Get-SimSessionHash {
  $d = Get-SimDefaults
  $t = Get-SimFanSessionToken $d.sessionTokenPath
  return (Get-SimTokenHash $t)
}

# ---------------------------------------------------------- reverse log binding --
function Get-SimLogSnapshot {
  # SIM-4: (path, length, mtime) snapshot taken BEFORE an action; only bytes appended after this offset count.
  param([string[]]$Paths)
  $out = [ordered]@{ takenAt = (Get-SimNowStamp); files = [ordered]@{} }
  foreach ($p in @($Paths)) {
    if (-not $p) { continue }
    if (Test-Path -LiteralPath $p -PathType Leaf) {
      $f = Get-Item -LiteralPath $p
      $out.files[$p] = [ordered]@{ exists = $true; length = $f.Length; mtime = $f.LastWriteTime.ToString('o') }
    } else { $out.files[$p] = [ordered]@{ exists = $false; length = 0; mtime = $null } }
  }
  return $out
}

function Read-SimTailLines {
  param([string]$Path, [int]$Count = 150)
  if (-not (Test-Path -LiteralPath $Path)) { return @() }
  $fs = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
  try {
    $block = 65536; $buf = New-Object byte[] $block; $sb = New-Object Text.StringBuilder
    $pos = $fs.Length; $newlines = 0
    while ($pos -gt 0 -and $newlines -le ($Count + 1)) {
      $read = [Math]::Min($block, $pos); $pos -= $read; $fs.Position = $pos
      $got = $fs.Read($buf, 0, $read)
      $chunk = [Text.Encoding]::UTF8.GetString($buf, 0, $got)
      $newlines += ([regex]::Matches($chunk, "`n")).Count
      [void]$sb.Insert(0, $chunk)
    }
    $lines = @($sb.ToString() -split "`n")
    $start = [Math]::Max(0, $lines.Count - $Count)
    return @($lines[$start..($lines.Count - 1)])
  } finally { $fs.Dispose() }
}

function Read-SimBytesFrom {
  # SIM-4: read only the bytes appended after an offset (so history can never masquerade as this run's evidence)
  param([string]$Path, [long]$FromOffset, [int]$MaxBytes = 4194304)
  if (-not (Test-Path -LiteralPath $Path)) { return [ordered]@{ available = $false; text = ''; fromOffset = $FromOffset; toOffset = 0 } }
  $info = Get-Item -LiteralPath $Path
  if ($info.Length -le $FromOffset) { return [ordered]@{ available = $false; text = ''; fromOffset = $FromOffset; toOffset = $info.Length } }
  $take = [Math]::Min($info.Length - $FromOffset, $MaxBytes)
  $fs = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
  try {
    $fs.Position = $FromOffset
    $buf = New-Object byte[] $take
    $got = $fs.Read($buf, 0, $take)
    return [ordered]@{ available = $true; text = [Text.Encoding]::UTF8.GetString($buf, 0, $got); fromOffset = $FromOffset; toOffset = ($FromOffset + $got) }
  } finally { $fs.Dispose() }
}

function Get-SimLogMatchesSince {
  <#
    SIM-4: matches are accepted ONLY from the window appended after the run's snapshot offset; each hit is
    located at its absolute offset and (optionally) must carry one of the expected edge markers.
  #>
  param(
    [string]$Path, [long]$FromOffset, [string]$Pattern, [string[]]$ExpectedMarkers,
    [int]$Limit = 12, [switch]$RequireMarker
  )
  $read = Read-SimBytesFrom -Path $Path -FromOffset $FromOffset
  $entry = [ordered]@{
    path = $Path; fromOffset = $FromOffset; toOffset = $read.toOffset; available = $read.available
    hits = @(); markerMatched = @(); historicalHitsPresent = $false
  }
  if (-not $read.available) {
    # nothing was appended: prove whether the file already contained the pattern (historical only)
    $tail = @(Read-SimTailLines -Path $Path -Count 200)
    $entry.historicalHitsPresent = @($tail | Where-Object { $_ -match $Pattern }).Count -gt 0
    return $entry
  }
  $chunk = $read.text
  $lines = @($chunk -split "`n")
  $off = $FromOffset
  foreach ($ln in $lines) {
    if ($ln -match $Pattern) {
      $trunc = if ($ln.Length -gt 400) { $ln.Substring(0, 400) + '...[truncated ' + $ln.Length + 'B]' } else { $ln }
      $entry.hits += [ordered]@{ absoluteOffset = $off; line = $trunc }
      $entry.hits = @($entry.hits)
    }
    $off += ([Text.Encoding]::UTF8.GetByteCount($ln) + 1)
  }
  $entry.hits = @($entry.hits | Select-Object -Last $Limit)
  if ($ExpectedMarkers -and @($ExpectedMarkers).Count -gt 0) {
    foreach ($h in $entry.hits) {
      foreach ($m in $ExpectedMarkers) { if ($h.line -match [regex]::Escape("$m")) { $entry.markerMatched += $h; break } }
    }
    $entry.markerMatched = @($entry.markerMatched)
  }
  $entry.matchesThisRun = $(if ($RequireMarker) { @($entry.markerMatched).Count -gt 0 } else { @($entry.hits).Count -gt 0 })
  return $entry
}

function Get-SimReverseEvidence {
  # v2: offset/session/marker bound (SIM-4). Needs the snapshot taken before the edges.
  param([string]$Label = 'reverse', [hashtable]$Snapshot, [string[]]$ExpectedMarkers, [int]$Limit = 10)
  $d = Get-SimDefaults
  $specs = @(
    [ordered]@{ name = 'fan-lifecycle'; path = $d.fanLifecycleLog; pattern = 'lease|suspend|resume|power|host|stale|error' }
    [ordered]@{ name = 'fan-api'; path = $d.fanApiLog; pattern = 'lease|suspend|resume|power|host|stale|error' }
    [ordered]@{ name = 'host-runtime'; path = $d.hostRuntimeLog; pattern = 'api\.suspend|api\.resume|POWER_|stale|deduplicated|resumeNoop' }
    [ordered]@{ name = 'native-lifecycle'; path = $d.nativeLifecycleLog; pattern = 'fan|power\.|susp|resum' }
    [ordered]@{ name = 'virtual-gamepad'; path = $d.virtualGamepadLog; pattern = 'sleep|suspend|resume|power' }
  )
  $out = [ordered]@{ label = $Label; scannedAt = (Get-SimNowStamp); snapshotTakenAt = $Snapshot.takenAt; expectedMarkers = @($ExpectedMarkers); logs = @(); anyMatchThisRun = $false }
  foreach ($s in $specs) {
    $from = 0
    if ($Snapshot -and $Snapshot.files -and $Snapshot.files.Contains($s.path)) { $from = [long]$Snapshot.files[$s.path].length }
    $e = Get-SimLogMatchesSince -Path $s.path -FromOffset $from -Pattern $s.pattern -ExpectedMarkers $ExpectedMarkers -Limit $Limit -RequireMarker:($ExpectedMarkers -and @($ExpectedMarkers).Count -gt 0)
    $e['name'] = $s.name
    $out.logs += $e
    if ($e.matchesThisRun) { $out.anyMatchThisRun = $true }
  }
  return $out
}

# ------------------------------------------------------------------ run/result --
function New-SimRun {
  param(
    [Parameter(Mandatory = $true)][string]$Domain,
    [string]$Mode = '',
    [ValidateSet('offline-injection', 'live-app-injection', 'real-os-sleep', 'device-observation', 'none')][string]$ModeKind = 'none',
    [string]$ToolPath = '',
    [int]$Seed = 204,
    [hashtable]$Notes
  )
  $run = [ordered]@{
    tool = 'ymcc_sim'
    coreVersion = $script:SimCoreVersion
    runId = [guid]::NewGuid().ToString('N')
    domain = $Domain
    mode = $Mode
    modeKind = $ModeKind
    seed = $Seed
    runStamp = (Get-Date -Format 'yyyyMMdd-HHmmss')
    startedAt = (Get-SimNowStamp)
    finishedAt = $null
    elevatedSession = (Get-SimElevated)
    entryScript = Get-SimFileIdentity $ToolPath
    notes = $Notes
    realSleepPerformed = $false
    hardwareActionPerformed = $false
    changeCallCount = 0
    forms = [System.Collections.Generic.List[object]]::new()
    checks = [System.Collections.Generic.List[object]]::new()
    steps = [System.Collections.Generic.List[object]]::new()
    events = [System.Collections.Generic.List[object]]::new()
    statuses = [System.Collections.Generic.List[object]]::new()
    requiredButMissing = [System.Collections.Generic.List[object]]::new()
    identity = [ordered]@{}
    reverse = $null
    extra = [ordered]@{}
  }
  return $run
}

function Add-SimCheck {
  param($Run, [string]$Form, [string]$Name, [bool]$Ok, [string]$Detail, [bool]$Required = $true, [switch]$Quiet)
  $Run.checks.Add([ordered]@{ form = $Form; name = $Name; passed = [bool]$Ok; detail = $Detail; required = [bool]$Required })
  if (-not $Quiet) {
    $tag = if ($Ok) { 'PASS' } else { 'FAIL' }
    $color = if ($Ok) { 'Gray' } elseif ($Required) { 'Red' } else { 'Yellow' }
    Write-Host ("  [{0}]{1} {2} :: {3}" -f $tag, $(if ($Required) { '' } else { '(info)' }), $Name, $Detail) -ForegroundColor $color
  }
  return $Ok
}

function Add-SimStep { param($Run, $Record) $Run.steps.Add($Record) }
function Add-SimEvent {
  param($Run, [string]$Kind, [string]$Detail, [hashtable]$Data)
  $Run.events.Add([ordered]@{ kind = $Kind; at = (Get-SimNowStamp); detail = $Detail; data = $Data })
}
function Add-SimStatus {
  param($Run, [string]$Scope, [ValidateSet('PASS', 'FAIL', 'BLOCKED', 'NOT_APPLICABLE', 'NOT_IMPLEMENTED', 'NOT_RUN')][string]$Status,
        [string]$Detail, [bool]$Required = $false)
  $Run.statuses.Add([ordered]@{ scope = $Scope; status = $Status; detail = $Detail; required = [bool]$Required })
}
function Add-SimRequiredButMissing { param($Run, [string]$Name, [string]$Detail) $Run.requiredButMissing.Add([ordered]@{ name = $Name; detail = $Detail }) }

function Get-SimRunCounts {
  param($Run)
  $c = [ordered]@{
    checksTotal = @($Run.checks).Count
    checksPassed = @($Run.checks | Where-Object { $_.passed }).Count
    requiredTotal = @($Run.checks | Where-Object { $_.required }).Count
    requiredFailed = @($Run.checks | Where-Object { $_.required -and -not $_.passed }).Count
    infoFailed = @($Run.checks | Where-Object { -not $_.required -and -not $_.passed }).Count
    eventsTotal = @($Run.events).Count
    timeouts = @($Run.events | Where-Object { $_.kind -eq 'timeout' }).Count
    exceptions = @($Run.events | Where-Object { $_.kind -eq 'exception' }).Count
    childNoExit = @($Run.events | Where-Object { $_.kind -eq 'child-no-exit' }).Count
    requiredBlocked = @($Run.statuses | Where-Object { $_.required -and $_.status -eq 'BLOCKED' }).Count
    requiredNotRun = @($Run.statuses | Where-Object { $_.required -and ($_.status -eq 'NOT_RUN' -or $_.status -eq 'NOT_IMPLEMENTED') }).Count
    missingRequired = @($Run.requiredButMissing).Count
  }
  return $c
}

function Get-SimVerdict {
  <#
    SIM-1: the ONE place that turns a run into a verdict + exit code. Never let a failed/missing/unknown item
    become success: required check failure, required missing, required blocked/not-run, exception, timeout,
    child without exit code, or a missing summary all force a non-zero code.
  #>
  param($Run)
  $c = Get-SimRunCounts $Run
  $prev = [ordered]@{
    ALL_PASS = 0; CHECK_FAIL = 0; ARG_OR_PREFLIGHT = 0; PRODUCT_ALREADY_RUNNING = 0
    NOT_IMPLEMENTED = 0; NEEDS_ELEVATION = 0; HOST_START_FAILED = 0; TIMEOUT = 0
    EVIDENCE_INCOMPLETE_STALE = 0; BLOCKED_REQUIRED = 0; REHEARSAL = 0
  }
  foreach ($f in @($Run.events | Where-Object { $_.kind -eq 'flag' })) {
    $flag = "$($f.data.key)"
    if ($prev.Contains($flag)) { $prev[$flag] = 1 }
  }
  if ($c.exceptions -gt 0) { $prev['CHECK_FAIL'] = 1 }
  if ($c.timeouts -gt 0 -or $c.childNoExit -gt 0) { $prev['TIMEOUT'] = 1 }
  if ($c.requiredFailed -gt 0) { $prev['CHECK_FAIL'] = 1 }
  if ($c.missingRequired -gt 0) { $prev['EVIDENCE_INCOMPLETE_STALE'] = 1 }
  if (@($Run.requiredButMissing | Where-Object { $_.name -match 'summary|verdict|identity|exit' }).Count -gt 0) { $prev['EVIDENCE_INCOMPLETE_STALE'] = 1 }
  if ($c.requiredBlocked -gt 0) { $prev['BLOCKED_REQUIRED'] = 1 }
  if ($c.requiredNotRun -gt 0) { $prev['BLOCKED_REQUIRED'] = 1 }
  $verdict = 'ALL_PASS'
  foreach ($k in $script:SimExitPrecedence) {
    if ($prev[$k] -eq 1) { $verdict = $k; break }
  }
  return [ordered]@{ verdict = $verdict; exitCode = [int]$script:SimExitCodes[$verdict]; flags = $prev; counts = $c }
}

# ---------------------------------------------------------------- misc (v1 carry-overs)
function Test-SimPort {
  param([int]$Port)
  try {
    $client = [System.Net.Sockets.TcpClient]::new()
    $iar = $client.BeginConnect('127.0.0.1', $Port, $null, $null)
    $ok = $iar.AsyncWaitHandle.WaitOne(400)
    $connected = ($ok -and $client.Connected)
    $client.Close()
    return $connected
  } catch { return $false }
}

function Get-SimFanSessionToken {
  param([string]$Path)
  if (-not $Path) { $d = Get-SimDefaults; $Path = $d.sessionTokenPath }
  if (-not (Test-Path -LiteralPath $Path)) { return $null }
  try { return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8).Trim() } catch { return $null }
}

function New-SimFanSessionToken {
  param([string]$Path)
  $token = ([guid]::NewGuid().ToString('N') + [guid]::NewGuid().ToString('N'))
  [IO.File]::WriteAllText($Path, $token, [Text.UTF8Encoding]::new($false))
  return $token
}

function Get-SimRunSummary {
  # compact summary for the non-sleep domains; verdict/exitCode come from the same aggregator
  param($Run)
  $v = Get-SimVerdict $Run
  return [ordered]@{
    checks = @($Run.checks).Count
    failed = @($Run.checks | Where-Object { -not $_.passed }).Count
    requiredFailed = $v.counts.requiredFailed
    forms = @($Run.forms).Count
    failedForms = @($Run.forms | Where-Object { $_.verdict -eq 'FAIL' }).Count
    skippedForms = @($Run.forms | Where-Object { $_.verdict -eq 'SKIP' }).Count
    verdict = $v.verdict
    exitCode = $v.exitCode
  }
}

function Save-SimEvidence {
  # v2: this is now the SAME entry point as Complete-SimRun (a single verdict source, SIM-1). It still honours
  # the old parameters so the gamepad/full domains keep working, and returns the exit code.
  param($Run, [string]$EvidenceDir, [string]$Prefix)
  return (Complete-SimRun -Run $Run -EvidenceDir $EvidenceDir -Prefix $Prefix)
}

function Complete-SimRun {
  <#
    SIM-1: writes the evidence JSON and returns the process exit code FROM THE SAME VERDICT OBJECT.
    Never returns 0 unless the verdict is ALL_PASS.
  #>
  param($Run, [string]$EvidenceDir, [string]$Prefix)
  $d = Get-SimDefaults
  if (-not $EvidenceDir) { $EvidenceDir = $d.evidenceDir }
  if (-not (Test-Path -LiteralPath $EvidenceDir)) { New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null }
  if (-not $Prefix) { $Prefix = ("sim-{0}" -f $Run.domain) }
  $Run.finishedAt = (Get-SimNowStamp)
  $v = Get-SimVerdict $Run
  $Run.verdict = $v.verdict
  $Run.exitCode = $v.exitCode
  $Run.verdictFlags = $v.flags
  $Run.counts = $v.counts
  $path = Join-Path $EvidenceDir ("{0}-{1}-{2}.json" -f $Prefix, $Run.runStamp, $Run.runId.Substring(0, 8))
  $json = $Run | ConvertTo-Json -Depth 10
  [IO.File]::WriteAllText($path, $json, [Text.UTF8Encoding]::new($false))
  # read-back guard: the JSON on disk must carry the same verdict/exit code we are about to return
  $check = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
  if ("$($check.verdict)" -ne "$($Run.verdict)" -or [int]$check.exitCode -ne [int]$Run.exitCode) {
    Write-Host 'EVIDENCE_WRITE_MISMATCH: on-disk verdict/exitCode differs from the computed one' -ForegroundColor Red
    return 9
  }
  Write-Host ("证据：{0}  verdict={1} exitCode={2}" -f $path, $Run.verdict, $Run.exitCode) -ForegroundColor White
  Write-Host ("计数：checks {0}/{1} 通过，必需 {2} 失败，timeout {3}，exception {4}，missing {5}" -f `
      $v.counts.checksPassed, $v.counts.checksTotal, $v.counts.requiredFailed, $v.counts.timeouts, $v.counts.exceptions, $v.counts.missingRequired)
  return [int]$Run.exitCode
}