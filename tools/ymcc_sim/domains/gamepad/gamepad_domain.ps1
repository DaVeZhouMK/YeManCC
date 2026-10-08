<#
.SYNOPSIS
  手柄域（ymcc_sim -Domain gamepad）：把既有虚拟手柄自测设施接到全局模拟器下。

.DESCRIPTION
  本域不重写任何虚拟手柄设施，只做"包装 + 统一证据 + 统一前置/提权口径"：
    * tools\virtual_gamepad_selfloop.ps1      —— 代码化自环（persona 自选、线材/泄漏/Start 探测）
    * tools\virtual_toggle_state.ps1          —— 三态只读验收探针（closed / virtual-on / closed）
    * tools\xinput_slot_observation.ps1       —— XInput 槽位只读观测
    * tools\virtual_output_gyro_link_audit.ps1（注册，不包装：需自有 -HcRoot 等参数）
  这些工具归输入线所有，本域只读不改；包装层的所有参数都原样透传给被包装工具。

  未实现：无（原 -Scenario sleep / full-virtual 两处已于 204-N 实现，见下；不再有任何假实现占位）。

  -Scenario sleep（204-N，live-app-injection）：
    合成电源广播 —— 只向 App 主窗口投递系统睡眠入口消息，不触发任何真实睡眠/休眠：
      WM_POWERBROADCAST(0x0218) + PBT_APMSUSPEND(0x0004)         入睡
      WM_POWERBROADCAST(0x0218) + PBT_APMRESUMEAUTOMATIC(0x0012)  唤醒（自动）
      WM_POWERBROADCAST(0x0218) + PBT_APMRESUMESUSPEND(0x0007)    唤醒（用户；覆盖 userWake 分支）
    依据：App 自身的 suspend/resume 订阅回调（native/main.cpp:41252-41267）把真实系统事件
    原样 PostMessage 到同一窗口 —— 因此本注入复现的是**同一条代码路径**；序列 4→18→7 与工程既有
    tools\wake_race_sim_test.ps1 同形（该原生竞态工具的原语与本场景一致）。
    ⚠ 副作用（必须知情）：入睡会触发 App 自身睡眠守护（冻结最大工作集进程，main.cpp:911-912），
      唤醒时恢复。因此本场景**默认只做演练（exit 12，什么都不投递）**，必须显式 -Confirm 且在
      授权窗口、操作者在场时执行；本域不自动启动 App（requireAdministrator ⇐ 用 -Elevate）。
    断言边界：本域只断言"投递成功/应用未崩溃/单实例/日志窗口推进/唤醒收敛"这类**作业级**事实；
      手柄链语义（input-host-lifecycle-work(power-suspend-no-write/power-rearm)、keepalive 等）
      归 W6 —— 本域只回交原始证据（日志窗口 + 偏移 + 哈希），不代签。
  -Scenario full-virtual：委派给整机虚拟测试域（-Domain full -Mode run），原样镜像其退出码。

.PARAMETER Mode
  registry = 只列设施与身份（默认；不运行任何东西）
  selfloop = 透传运行虚拟手柄自环（需要管理员会话：App requireAdministrator）
  probe    = 运行只读探针（toggle-state / xinput-slot）

.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain gamepad -Mode registry
.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain gamepad -Mode selfloop -Persona xbox360 -DurationSec 25 -Elevate
.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain gamepad -Mode selfloop -DryRun
.EXAMPLE
  tools\ymcc_sim\ymcc_sim.ps1 -Domain gamepad -Mode probe -Probe toggle-state
#>
[CmdletBinding()]
param(
  [ValidateSet('registry', 'selfloop', 'probe')][string]$Mode = 'registry',
  [string]$Scenario = '',
  [ValidateSet('dualshock4', 'xbox360', 'dualsense', 'disabled')][string]$Persona = 'dualshock4',
  [int]$DurationSec = 25,
  [string]$OutDir = '',
  [int]$WaitStartSec = 150,
  [int]$WireWatchSec = 0,
  [int]$LeakWatchSec = 0,
  [int]$StartProbeSec = 0,
  [int]$UsageWatchSec = 0,
  [string]$WireProfilePath = '',
  [string]$WireDiffPath = '',
  [string]$UsageVidHex = '',
  [string]$UsagePidHex = '',
  [ValidateSet('toggle-state', 'xinput-slot')][string]$Probe = 'toggle-state',
  [switch]$DryRun,
  # ---- -Scenario sleep（204-N）：合成电源广播；默认演练，-Confirm 才真投递 ----
  [switch]$Confirm,
  [int]$SuspendHoldSec = 8,
  [int]$ResolveTimeoutSec = 90,
  [string]$EvidenceDir = '',
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$Passthru
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path (Split-Path -Parent (Split-Path -Parent $scriptDir)) 'lib\sim_core.ps1')

$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $env:LOCALAPPDATA 'YeManCC\sim-evidence\gamepad-selfloop' }
if (-not $EvidenceDir) { $EvidenceDir = $D.evidenceDir }
if (-not (Test-Path -LiteralPath $EvidenceDir)) { New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null }

$script:Run = New-SimRun -Domain 'gamepad' -Mode $Mode -ToolPath $PSCommandPath
$script:Run.extra['scenario'] = $Scenario

$facilities = [ordered]@{
  selfloop      = (Join-Path $D.toolsDir 'virtual_gamepad_selfloop.ps1')
  toggleState   = (Join-Path $D.toolsDir 'virtual_toggle_state.ps1')
  xinputSlots   = (Join-Path $D.toolsDir 'xinput_slot_observation.ps1')
  gyroLinkAudit = (Join-Path $D.toolsDir 'virtual_output_gyro_link_audit.ps1')
  hidProbe      = (Join-Path $D.toolsDir 'hid_readonly_probe\bin\Release\net10.0-windows\hid_readonly_probe.exe')
}

function Get-FacilityMap {
  $list = @()
  foreach ($k in @('selfloop', 'toggleState', 'xinputSlots', 'gyroLinkAudit', 'hidProbe')) {
    $id = Get-SimFileIdentity $facilities[$k] $k
    $id['wrapped'] = ($k -in @('selfloop', 'toggleState', 'xinputSlots'))
    $list += $id
  }
  return @($list)
}

function Show-Registry {
  Write-Host '=== 手柄域设施清单（只读；不运行任何东西）===' -ForegroundColor White
  $map = Get-FacilityMap
  foreach ($f in $map) {
    $tag = if ($f.wrapped) { 'wrapped' } else { 'registered' }
    $ok = Add-SimCheck -Run $script:Run -Form 'REG' -Name ("facility {0}" -f $f.label) -Ok $f.exists -Detail ("{0} {1} bytes={2} [{3}]" -f $tag, $f.sha256.Substring(0, 16), $f.bytes, $f.path)
  }
  $script:Run.extra['facilities'] = $map
  Add-SimCheck -Run $script:Run -Form 'REG' -Name 'pad-sleep scenario implemented (204-N)' -Ok $true -Detail '合成电源广播（WM_POWERBROADCAST/PBT_APMSUSPEND↔PBT_APMRESUMEAUTOMATIC）；默认演练 exit 12，-Confirm 才投递；无 App ⇒ exit 10；手柄链语义断言归 W6' | Out-Null
  Add-SimCheck -Run $script:Run -Form 'REG' -Name 'toggles: none applied to any product/OS setting' -Ok $true -Detail '本域只包装既有只读/代码化工具，不改设置、不装驱动、不动 HidHide' | Out-Null
  $procs = Get-SimProductProcesses
  # 信息级：registry 是只读清单，产品在跑只影响 selfloop/live 的前置，不影响清单本身是否成立
  Add-SimCheck -Run $script:Run -Form 'REG' -Name 'product process state (informational; selfloop wants a clean start)' -Ok $true -Required $false -Detail $(if ($procs.Count -eq 0) { 'none running' } else { 'running: ' + (@($procs | ForEach-Object { "$($_.name):$($_.pid)" }) -join ',') }) | Out-Null
  return 0
}

function Get-SelfloopArgList {
  $a = @('-Persona', $Persona, '-DurationSec', "$DurationSec", '-OutDir', $OutDir, '-WaitStartSec', "$WaitStartSec")
  if ($WireWatchSec -gt 0) { $a += @('-WireWatchSec', "$WireWatchSec") }
  if ($LeakWatchSec -gt 0) { $a += @('-LeakWatchSec', "$LeakWatchSec") }
  if ($StartProbeSec -gt 0) { $a += @('-StartProbeSec', "$StartProbeSec") }
  if ($UsageWatchSec -gt 0) { $a += @('-UsageWatchSec', "$UsageWatchSec") }
  if ($WireProfilePath) { $a += @('-WireProfilePath', $WireProfilePath) }
  if ($WireDiffPath) { $a += @('-WireDiffPath', $WireDiffPath) }
  if ($UsageVidHex) { $a += @('-UsageVidHex', $UsageVidHex) }
  if ($UsagePidHex) { $a += @('-UsagePidHex', $UsagePidHex) }
  foreach ($p in @($Passthru)) { if ($null -ne $p -and "$p" -ne '') { $a += "$p" } }
  return $a
}

function Invoke-Selfloop {
  $tool = $facilities.selfloop
  if (-not (Test-Path -LiteralPath $tool)) {
    Add-SimCheck -Run $script:Run -Form 'SELFLOOP' -Name 'wrapped tool present' -Ok $false -Detail $tool | Out-Null
    return 3
  }
  $toolId = Get-SimFileIdentity $tool 'selfloop'
  $script:Run.extra['wrappedTool'] = $toolId
  $wrappedArgs = Get-SelfloopArgList
  $line = 'powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File ' + $tool + ' ' + ($wrappedArgs -join ' ')
  Write-Host ("自环命令行：{0}" -f $line) -ForegroundColor Gray

  $procs = Get-SimProductProcesses
  Add-SimCheck -Run $script:Run -Form 'SELFLOOP' -Name 'wrapped tool present' -Ok $true -Detail ("{0} bytes={1}" -f $toolId.sha256.Substring(0, 16), $toolId.bytes) | Out-Null
  Add-SimCheck -Run $script:Run -Form 'SELFLOOP' -Name 'app exe present' -Ok (Test-Path -LiteralPath $D.appExe) -Detail $D.appExe | Out-Null
  Add-SimCheck -Run $script:Run -Form 'SELFLOOP' -Name 'no resident instance (selfloop would observe a stale one)' -Ok ($procs.Count -eq 0) -Detail (@($procs | ForEach-Object { "$($_.name):$($_.pid)" }) -join ',') | Out-Null

  if ($DryRun) {
    Add-SimCheck -Run $script:Run -Form 'SELFLOOP' -Name 'DryRun: nothing launched' -Ok $true -Detail 'preconditions checked, wrapped tool NOT started' | Out-Null
    $script:Run.extra['commandLine'] = $line
    $script:Run.extra['verdict'] = 'DRYRUN'
    return 0
  }

  if (-not (Get-SimElevated)) {
    Add-SimCheck -Run $script:Run -Form 'SELFLOOP' -Name 'elevated session (app manifest requiresAdministrator)' -Ok $false -Detail '请用 -Elevate（或管理员会话）重跑' | Out-Null
    return 6
  }
  if ($procs.Count -gt 0) { return 4 }

  # run the wrapped tool verbatim; its exit code is the domain's exit code
  & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $tool @wrappedArgs
  $code = $LASTEXITCODE
  $script:Run.extra['commandLine'] = $line
  $script:Run.extra['wrappedExitCode'] = $code
  $script:Run.extra['procAfter'] = @(Get-SimProductProcesses)
  Start-Sleep -Seconds 1
  $script:Run.extra['reverse'] = Get-SimReverseEvidence -Label 'gamepad-selfloop'
  Add-SimCheck -Run $script:Run -Form 'SELFLOOP' -Name 'wrapped selfloop returned a code' -Ok ($null -ne $code) -Detail ("exit={0}" -f $code) | Out-Null
  $script:Run.extra['verdict'] = $(if ($code -eq 0) { 'SELFLOOP_OK' } else { 'SELFLOOP_FAILED' })
  return $code
}

function Invoke-Probe {
  $tool = if ($Probe -eq 'toggle-state') { $facilities.toggleState } else { $facilities.xinputSlots }
  if (-not (Test-Path -LiteralPath $tool)) {
    Add-SimCheck -Run $script:Run -Form 'PROBE' -Name 'probe tool present' -Ok $false -Detail $tool | Out-Null
    return 3
  }
  $script:Run.extra['probeTool'] = Get-SimFileIdentity $tool $Probe
  if ($DryRun) {
    Add-SimCheck -Run $script:Run -Form 'PROBE' -Name 'DryRun: nothing launched' -Ok $true -Detail ('would run: ' + $tool) | Out-Null
    $script:Run.extra['verdict'] = 'DRYRUN'
    return 0
  }
  & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $tool
  $code = $LASTEXITCODE
  $script:Run.extra['probeExitCode'] = $code
  Add-SimCheck -Run $script:Run -Form 'PROBE' -Name ("read-only probe {0} ran" -f $Probe) -Ok ($null -ne $code) -Detail ("exit={0} tool={1}" -f $code, $tool) | Out-Null
  $script:Run.extra['verdict'] = 'PROBE_DONE'
  return $code
}

# ────────────────────────── -Scenario sleep（204-N，合成电源广播） ──────────────────────────
function Ensure-SimWinType {
  if ('YMSimWin' -as [type]) { return }
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class YMSimWin {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  public static IntPtr FindTopWindow(int pid) {
    IntPtr titled = IntPtr.Zero, any = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint wp; GetWindowThreadProcessId(h, out wp);
      if (wp == (uint)pid && IsWindowVisible(h)) {
        if (any == IntPtr.Zero) any = h;
        if (GetWindowTextLength(h) > 0) { titled = h; return false; }
      }
      return true;
    }, IntPtr.Zero);
    return titled != IntPtr.Zero ? titled : any;
  }
}
'@
}

function Invoke-PadSleepScenario {
  # 合成电源广播：只投递窗口消息，不触发真实睡眠。断言只到作业级；手柄链语义归 W6。
  param()
  if (-not ('YMSimWin' -as [type])) { Ensure-SimWinType }
  $WM_POWERBROADCAST = 0x0218; $PBT_APMSUSPEND = 0x0004; $PBT_APMRESUMEAUTOMATIC = 0x0012; $PBT_APMRESUMESUSPEND = 0x0007

  # 1) 前置：App 恰一个实例（本场景不自动启动 App）
  $apps = @(Get-SimProcessesByRole 'App')
  $appOk = (@($apps).Count -eq 1)
  Add-SimCheck -Run $script:Run -Form 'PAD-SLEEP' -Name 'exactly one App instance (this scenario never starts the app)' -Ok $appOk -Required $true `
    -Detail $(if ($appOk) { ("pid={0} creationTime={1}" -f $apps[0].pid, $apps[0].creationTime) } else { ("appInstances={0}；请在有窗口时先启动 App（requireAdministrator，可 -Elevate），或直接把它当 BLOCKED 处理" -f @($apps).Count) }) | Out-Null
  if (-not $appOk) {
    Add-SimStatus -Run $script:Run -Scope 'pad-sleep' -Status 'BLOCKED' -Detail 'app-not-running: 合成电源广播需要一个在跑的 App 接收窗口消息' -Required $true
    $script:Run.extra['verdict'] = 'BLOCKED_APP_NOT_RUNNING'
    $script:Run.extra['howTo'] = 'powershell -File tools\ymcc_sim\ymcc_sim.ps1 -Domain gamepad -Scenario sleep -Confirm（在授权窗口、操作者在场、App 已启动时）'
    return 10
  }
  $appPid = [int]$apps[0].pid

  # 2) 主窗口句柄
  $hwnd = [YMSimWin]::FindTopWindow($appPid)
  $winOk = ($hwnd -ne [IntPtr]::Zero)
  Add-SimCheck -Run $script:Run -Form 'PAD-SLEEP' -Name 'App top-level window found (post target)' -Ok $winOk -Required $true -Detail ("pid={0} hwnd={1}" -f $appPid, $hwnd) | Out-Null
  if (-not $winOk) {
    Add-SimStatus -Run $script:Run -Scope 'pad-sleep' -Status 'BLOCKED' -Detail 'window-not-found: 没有可见顶层窗口可投递' -Required $true
    $script:Run.extra['verdict'] = 'BLOCKED_WINDOW_NOT_FOUND'
    return 10
  }

  # 3) 投递前的偏移快照 + 逐角色身份快照（SIM-4）
  $logPaths = @($D.nativeLifecycleLog, $D.virtualGamepadLog, $D.fanLifecycleLog, $D.fanApiLog, $D.hostRuntimeLog)
  $snapshot = Get-SimLogSnapshot -Paths $logPaths
  $rolesBefore = Get-SimRoleSnapshotAll
  $script:Run.extra['logSnapshot'] = $snapshot

  # 4) 默认演练：什么都不投递
  if (-not $Confirm) {
    Add-SimCheck -Run $script:Run -Form 'PAD-SLEEP' -Name 'rehearsal: nothing injected (-Confirm required)' -Ok $true -Required $true `
      -Detail ("would post WM_POWERBROADCAST/PBT_APMSUSPEND to hwnd={0}, hold {1}s, then PBT_APMRESUMEAUTOMATIC + PBT_APMRESUMESUSPEND (4→18→7，真实序列)；副作用：入睡会冻结最大工作集进程（App 自身睡眠守护）" -f $hwnd, $SuspendHoldSec) | Out-Null
    Add-SimStatus -Run $script:Run -Scope 'pad-sleep' -Status 'NOT_RUN' -Detail 'rehearsal-only' -Required $false
    Add-SimEvent -Run $script:Run -Kind 'flag' -Detail 'rehearsal: nothing was injected' -Data @{ key = 'REHEARSAL' }
    $script:Run.extra['verdict'] = 'REHEARSAL'
    $script:Run.extra['injection'] = [ordered]@{ hwnd = "$hwnd"; suspend = 'not-sent'; resume = 'not-sent' }
    return 12
  }

  # 5) 真投递：入睡 → 保持 → 唤醒（唤醒与真实序列一致：先 18 自动唤醒、再 7 用户唤醒；
  #    与工程既有 tools\wake_race_sim_test.ps1 的 4→18→7 序列同形，覆盖 userWake 分支）
  $sentSuspend = [YMSimWin]::PostMessage($hwnd, $WM_POWERBROADCAST, [IntPtr]$PBT_APMSUSPEND, [IntPtr]::Zero)
  Add-SimCheck -Run $script:Run -Form 'PAD-SLEEP' -Name 'PBT_APMSUSPEND posted to the app window' -Ok $sentSuspend -Required $true -Detail ("hwnd={0} posted={1}" -f $hwnd, $sentSuspend) | Out-Null
  Start-Sleep -Seconds ([Math]::Max(1, $SuspendHoldSec))
  $sentResume = [YMSimWin]::PostMessage($hwnd, $WM_POWERBROADCAST, [IntPtr]$PBT_APMRESUMEAUTOMATIC, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
  $sentResumeUser = [YMSimWin]::PostMessage($hwnd, $WM_POWERBROADCAST, [IntPtr]$PBT_APMRESUMESUSPEND, [IntPtr]::Zero)
  Add-SimCheck -Run $script:Run -Form 'PAD-SLEEP' -Name 'wake posted (18 automatic, then 7 user - the real sequence)' -Ok ($sentResume -and $sentResumeUser) -Required $true -Detail ("hwnd={0} auto={1} user={2}" -f $hwnd, $sentResume, $sentResumeUser) | Out-Null

  # 6) 唤醒收敛：轮询本轮新增日志（偏移绑定；历史命中永不算）
  $deadline = (Get-Date).AddSeconds([Math]::Max(10, $ResolveTimeoutSec))
  $resolved = $false; $probe = $null
  while ((Get-Date) -lt $deadline) {
    $probe = Get-SimReverseEvidence -Label 'pad-sleep' -Snapshot $snapshot -ExpectedMarkers @('resum', 'suspend')
    if ($probe.anyMatchThisRun) { $resolved = $true; break }
    Start-Sleep -Seconds 2
  }
  if (-not $probe) { $probe = Get-SimReverseEvidence -Label 'pad-sleep' -Snapshot $snapshot -ExpectedMarkers @('resum', 'suspend') }
  $script:Run.extra['reverse'] = $probe
  Add-SimCheck -Run $script:Run -Form 'PAD-SLEEP' -Name 'power rows appeared in this run window (offset-bound, not historical)' -Ok $resolved -Required $true `
    -Detail ("anyMatchThisRun={0} logs={1}" -f $probe.anyMatchThisRun, (@($probe.logs | ForEach-Object { "$($_.name):$(@($_.hits).Count)" }) -join ',')) | Out-Null

  # 7) 作业级存活断言：pid 不变、单实例、窗口仍在
  $appsAfter = @(Get-SimProcessesByRole 'App')
  $samePid = (@($appsAfter).Count -eq 1) -and ([int]$appsAfter[0].pid -eq $appPid)
  $winAlive = [YMSimWin]::IsWindow($hwnd)
  Add-SimCheck -Run $script:Run -Form 'PAD-SLEEP' -Name 'app survived the synthetic cycle (same pid, single instance, window alive)' -Ok ($samePid -and $winAlive) -Required $true `
    -Detail ("instances={0} samePid={1} windowAlive={2}" -f @($appsAfter).Count, $samePid, $winAlive) | Out-Null

  # 8) 交回原始证据（手柄链语义断言归 W6）
  $script:Run.extra['handoffToW6'] = [ordered]@{
    note = 'pad-side semantics (input-host-lifecycle-work / keepalive / release receipts) are W6 assertions; Fan hands over raw log windows only'
    logs = @($probe.logs | ForEach-Object { [ordered]@{ name = $_.name; path = $_.path; fromOffset = $_.fromOffset; toOffset = $_.toOffset; hitCount = @($_.hits).Count; sample = @($_.hits | Select-Object -First 3) } })
    rolesBefore = $rolesBefore
    rolesAfter = (Get-SimRoleSnapshotAll)
  }
  if ($resolved -and $samePid -and $winAlive) {
    Add-SimStatus -Run $script:Run -Scope 'pad-sleep' -Status 'PASS' -Detail 'injection delivered and resolved; pad-side semantics handed to W6'
    $script:Run.extra['verdict'] = 'PAD_SLEEP_DELIVERED'
    return 0
  }
  Add-SimStatus -Run $script:Run -Scope 'pad-sleep' -Status 'FAIL' -Detail 'injection did not resolve within the window, or the app did not survive'
  $script:Run.extra['verdict'] = 'PAD_SLEEP_FAILED'
  return 2
}

function Invoke-FullVirtualScenario {
  # full-virtual：委派整机虚拟测试域（-Mode run），原样镜像退出码；不重复其断言
  $fullScript = Join-Path (Split-Path -Parent $scriptDir) 'full\full_domain.ps1'
  if (-not (Test-Path -LiteralPath $fullScript)) {
    Add-SimCheck -Run $script:Run -Form 'SCENARIO' -Name 'full domain script present' -Ok $false -Detail $fullScript | Out-Null
    return 3
  }
  $childArgs = @('-Mode', 'run', '-EvidenceDir', $EvidenceDir)
  if ($DryRun) { $childArgs += '-DryRun' }
  if ($Confirm) { $childArgs += '-WithApp' }
  Write-Host ("委派：powershell -File {0} {1}" -f $fullScript, ($childArgs -join ' ')) -ForegroundColor Gray
  & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $fullScript @childArgs
  $code = $LASTEXITCODE
  $script:Run.extra['delegated'] = [ordered]@{ script = $fullScript; args = $childArgs; exitCode = $code }
  Add-SimCheck -Run $script:Run -Form 'SCENARIO' -Name 'full-virtual delegated to -Domain full -Mode run' -Ok ($code -eq 0) -Detail ("childExit={0}" -f $code) | Out-Null
  $script:Run.extra['verdict'] = $(if ($code -eq 0) { 'FULL_VIRTUAL_OK' } else { 'FULL_VIRTUAL_CHILD_FAILED' })
  return [int]$code
}

# ------------------------------------------------------------------- main ----
$exitCode = 0
try {
  Write-Host '=== 手柄域（ymcc_sim -Domain gamepad）===' -ForegroundColor White
  Write-Host ("mode={0} scenario={1} dryRun={2}" -f $Mode, $(if ($Scenario) { $Scenario } else { '(none)' }), [bool]$DryRun)

  if ($Scenario -eq 'sleep') {
    $exitCode = Invoke-PadSleepScenario
  } elseif ($Scenario -eq 'full-virtual') {
    $exitCode = Invoke-FullVirtualScenario
  } elseif ($Scenario) {
    Add-SimCheck -Run $script:Run -Form 'SCENARIO' -Name ("unknown scenario '{0}'" -f $Scenario) -Ok $false -Detail '可用：sleep（合成电源广播，需 -Confirm）/ full-virtual（委派 -Domain full -Mode run）'
    $script:Run.extra['verdict'] = 'BAD_SCENARIO'
    $exitCode = 3
  } elseif ($Mode -eq 'registry') {
    $exitCode = Show-Registry
    $script:Run.extra['verdict'] = 'REGISTRY'
  } elseif ($Mode -eq 'selfloop') {
    $exitCode = Invoke-Selfloop
  } else {
    $exitCode = Invoke-Probe
  }
}
catch {
  Write-Host ("执行中止：{0}" -f $_.Exception.Message) -ForegroundColor Red
  Add-SimCheck -Run $script:Run -Form 'RUN' -Name 'run completed' -Ok $false -Detail $_.Exception.Message | Out-Null
  $script:Run.extra['verdict'] = 'ABORTED'
  if ($exitCode -eq 0) { $exitCode = 3 }
}
finally {
  $sum = Get-SimRunSummary $script:Run
  $script:Run.extra['summary'] = $sum
  $script:Run.extra['toolIdentity'] = [ordered]@{
    domainScript = (Get-SimFileIdentity $PSCommandPath 'gamepad_domain')
    coreLibrary = (Get-SimFileIdentity (Join-Path (Split-Path -Parent (Split-Path -Parent $scriptDir)) 'lib\sim_core.ps1') 'sim_core')
    version = 'gamepad_domain v2 (2026-09-23, 204-N: -Scenario sleep = synthetic power broadcast; -Scenario full-virtual delegates to the full domain)'
  }
  $agg = Save-SimEvidence -Run $script:Run -EvidenceDir $EvidenceDir -Prefix 'gamepad-sim'
  if ($agg -ne 0) { $exitCode = $agg }   # SIM-1: the aggregate verdict never lets a failure exit 0
}
exit $exitCode