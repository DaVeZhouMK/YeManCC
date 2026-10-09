<#
.SYNOPSIS
  T4 read-only audit for the complete YeMan Fan lifecycle around frozen HC.

  The audit reads source and existing self-test evidence only. It never loads
  HandheldCompanion, starts ManagerFactory, invokes WMI/ACPI/HID/EC, or enables
  a hardware write. Any unexplained or unproven difference is deliberately
  emitted as needs-investigation; temperature HW is the only excluded delta.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcRoot,
  [Parameter(Mandatory = $true)][string]$HostSource,
  [Parameter(Mandatory = $true)][string]$NativeSource,
  [Parameter(Mandatory = $true)][string]$FrontendSource,
  [Parameter(Mandatory = $true)][string]$ApiSource,
  [Parameter(Mandatory = $true)][string]$NavSource,
  [Parameter(Mandatory = $true)][string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
function Read-Utf8([string]$path) { Get-Content -LiteralPath $path -Raw -Encoding UTF8 }
function Sha256([string]$path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
  } finally { $sha.Dispose() }
}
function Has([string]$text, [string]$token) { return $text.IndexOf($token, [StringComparison]::Ordinal) -ge 0 }
function Slice([string]$text, [string]$start, [string]$end) {
  $s = $text.IndexOf($start, [StringComparison]::Ordinal)
  if ($s -lt 0) { return '' }
  $e = $text.IndexOf($end, $s + $start.Length, [StringComparison]::Ordinal)
  if ($e -lt 0) { $e = $text.Length }
  return $text.Substring($s, $e - $s)
}

$appSource = Join-Path (Split-Path -Parent (Split-Path -Parent $FrontendSource)) 'App.vue'
$required = @(
  $HostSource, $NativeSource, $FrontendSource, $appSource, $ApiSource, $NavSource,
  (Join-Path $HcRoot 'Views\Windows\MainWindow.xaml.cs'),
  (Join-Path $HcRoot 'Devices\IDevice.cs'),
  (Join-Path $HcRoot 'Devices\ASUS\ROGAlly.cs')
)
foreach ($path in $required) { if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required source is missing: $path" } }
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$hcWindowPath = Join-Path $HcRoot 'Views\Windows\MainWindow.xaml.cs'
$hcDevicePath = Join-Path $HcRoot 'Devices\IDevice.cs'
$hcRogPath = Join-Path $HcRoot 'Devices\ASUS\ROGAlly.cs'
$hcWindow = Read-Utf8 $hcWindowPath
$hcDevice = Read-Utf8 $hcDevicePath
$hcRog = Read-Utf8 $hcRogPath
$hostText = Read-Utf8 $HostSource
$native = Read-Utf8 $NativeSource
$front = Read-Utf8 $FrontendSource
$app = Read-Utf8 $appSource
$api = Read-Utf8 $ApiSource
$nav = Read-Utf8 $NavSource

$checks = [System.Collections.Generic.List[object]]::new()
$needs = [System.Collections.Generic.List[object]]::new()
function Pass([string]$id, [string]$title, [string]$evidence) { $checks.Add([ordered]@{ id=$id; status='pass'; title=$title; evidence=$evidence }) }
function Fail([string]$id, [string]$title, [string]$evidence) { $checks.Add([ordered]@{ id=$id; status='fail'; title=$title; evidence=$evidence }) }
function Need([string]$id, [string]$title, [string]$hc, [string]$yeman, [string]$impact, [string]$next) { $needs.Add([ordered]@{ id=$id; severity='P1'; status='needs-investigation'; title=$title; hc=$hc; yeman=$yeman; impact=$impact; next=$next }) }
function Require-All([string]$id, [string]$title, [string]$text, [string[]]$tokens) {
  $missing = @($tokens | Where-Object { !(Has $text $_) })
  if ($missing.Count -eq 0) { Pass $id $title ($tokens -join '; ') }
  else { Fail $id $title ("missing: " + ($missing -join '; ')) }
}

# HC close/restore source contract.
Require-All 'T4-HC-CLOSE-01' 'HC SystemPending performs manager suspend before device Close' $hcWindow @(
  'ManagerFactory.Suspend();', 'PlatformManager.LibreHardware.Stop();', 'CurrentDevice.Close();')
Require-All 'T4-HC-CLOSE-02' 'HC Window_Closed owns CurrentDevice.Close and manager Stop' $hcWindow @(
  'private async void Window_Closed', 'CurrentDevice.Close();', 'foreach (IManager manager in ManagerFactory.Managers)', 'manager.Stop();')
Require-All 'T4-HC-CLOSE-03' 'HC device Close releases fan, OpenLibSys and subscriptions' $hcDevice @(
  'SetFanControl(false);', 'openLibSys?.Dispose();', 'ManagerFactory.settingsManager.Initialized -= SettingsManager_Initialized;', 'Closed?.Invoke(this);')
Require-All 'T4-HC-CLOSE-04' 'HC ROG Close follows AsusACPI/controller/HID/base order' $hcRog @(
  'AsusACPI.Close();', 'ConfigureController(false);', 'hidDevice.Dispose();', 'base.Close();')

# Host/native HTTP and single-owner exit contract.
# §2（FAN-926R 裁决）：接受现行**带 `rejectionCategory` 的**调用签名；参数个数变化**不得**被
# 当成撤掉认证检查。仍须证明：认证在业务分派链**之前**、保留同会话头、定长比较、发出 auth.rejected。
$authCheck = $hostText.IndexOf('if (!IsAuthorizedRequest(context.Request, out var rejectionCategory))', [StringComparison]::Ordinal)
$dispatchChain = $hostText.IndexOf('if (method == "GET" && path == "/health")', [StringComparison]::Ordinal)
if ($authCheck -ge 0 -and $dispatchChain -gt $authCheck -and
    (Has $hostText 'private bool IsAuthorizedRequest(HttpListenerRequest request, out string rejectionCategory)') -and
    (Has $hostText 'X-YeMan-Fan-Session') -and
    (Has $hostText 'CryptographicOperations.FixedTimeEquals') -and
    (Has $hostText 'auth.rejected')) {
  Pass 'T4-HOST-AUTH-01' 'Host authenticates every loopback request before dispatch' 'IsAuthorizedRequest(context.Request, out rejectionCategory) precedes the /health dispatch chain; X-YeMan-Fan-Session; FixedTimeEquals; auth.rejected'
} else { Fail 'T4-HOST-AUTH-01' 'Host authentication (current signature) must precede the dispatch chain' 'auth-call-order/constant-time/header/rejection boundary' }
Require-All 'T4-HOST-BOUNDARY-01' 'Host claims one Close boundary and blocks writes before lock' $hostText @(
  'BlockWritesForClose();', 'Interlocked.Exchange(ref closeBoundaryClaimed, 1);', 'HC_CLOSE_PENDING', 'PARENT_EXIT_HANDOFF')
Require-All 'T4-HOST-PARENT-01' 'Parent-exit handoff is asynchronous and observer-owned' $hostText @(
  'public object BeginParentExitHandoff()', 'Task.Run(CompleteParentExitHandoff)', 'engine recovery timer is the sole', 'ParentRecoveryIsSafeToFinalize')
# FAN-931 P1.3：看门狗改为"一次绑定可信父实例 + 等待退出"，身份（启动时间/映像）比对
# 仍在绑定处执行 ⇒ 以 TryBindParentProcess 及其拒绝原因作为等价锚点，不降标。
Require-All 'T4-HOST-WATCHDOG-01' 'Watchdog verifies parent start/path identity' $hostText @(
  'parentStartTimeUtc', 'parentExecutablePath', 'CaptureParentIdentity', 'TryBindParentProcess', 'parent-start-time-mismatch', 'parent-image-mismatch')
Require-All 'T4-HOST-SLEEP-01' 'Host power callback only marks/queues and does not block' $hostText @(
  'MarkSystemSuspendPending()', 'Channel.CreateUnbounded<PowerTransition>', 'powerTransitions.Writer.TryWrite', 'ProcessPowerTransitionsAsync')
# R1（FAN-926R 唤醒租约裁决 §3.2.5）**因本裁决改要求**：`ExpireLease` 现在必须按
# "创建它的那个租约"自证身份（签名带 callbackLeaseId/generation），迟到回调不得处置新租约。
# 断言内容不变（租约到期/心跳都走 HC profile 释放、不走虚拟 Close），只重定位到现行入口。
Require-All 'T4-HOST-LEASE-01' 'Lease expiry and heartbeat use HC profile release without virtual Close' $hostText @(
  'VerifyActiveCurveSession()', 'RestoreHardware(close: false)', 'private void ExpireLease(string callbackLeaseId, long callbackLeaseGeneration)', 'EnsureLeaseFromBody')
Require-All 'T4-HOST-RESUME-01' 'Automatic resume rebuilds Open -> OpenEvents -> lease -> curve' $hostText @(
  'StartAutomaticResumeUnlocked(', 'Open();', 'OpenEvents();', 'AcquireControl();', 'Enable(document.RootElement)')
Require-All 'T4-NATIVE-SLEEP-01' 'Native sleep callback schedules only asynchronous Host work' $native @(
  'fanHostScheduleEmergencySuspend("suspend-confirmed", currentPowerGeneration())', 'std::thread([reasonText', '}).detach();')
Require-All 'T4-NATIVE-EXIT-01' 'Native exit uses parent-exit first and only legacy-fallbacks on 404/405' $native @(
  'L"/api/parent-exit"', 'handoffStatus != 404 && handoffStatus != 405', 'L"/api/close"', 'L"/api/shutdown"')
Require-All 'T4-NATIVE-EXIT-02' 'Native exit verifies state before shutdown/force stop' $native @(
  'fanHostEmergencyPost(L"/api/shutdown", "app-exit", 1, false,', 'fanHostCleanupForAppExit()', 'fanHostExactProcessRunning()')
Require-All 'T4-NATIVE-RESUME-01' 'Native triggers the Host resume rebuild itself (HC: SystemReady attempts Open outside SystemPending)' $native @(
  'fanHostEmergencyResume(', 'L"/api/resume"', 'fanLeaseKeepaliveArm(leaseId, "native.power-resume")')
Require-All 'T4-NATIVE-RESUME-02' 'Native completes the wake commit itself; renderer IPC stays an idempotent second caller (HC: SystemReady completes in-process)' $native @(
  'static bool commitPowerResume(', '"native.resume-ready"', 'commitPowerResume(current, "renderer.resumeComplete")',
  'power resumeComplete already_ready')
Require-All 'T4-NATIVE-EPOCH-01' 'Native keepalive survives renderer rebuilds via a bounded epoch grace and never fails silently' $native @(
  'g_fanKeepaliveRendererEpoch', 'FAN_LEASE_RENDERER_GRACE_MS', 'renderer-epoch-grace-expired',
  'fan-host-identity-unverified', 'kHttpSysListenerOwnerPid')
Require-All 'T4-API-01' 'Frontend HTTP adapter has long Close timeout and request telemetry' $api @(
  "if (path === '/api/close') return 45000;", 'requestId', 'api.transport-failure')

# Frontend recovery/exit contract. These are intentionally strict because an
# await on close failure bypasses native parent-exit safety.
$safeAbort = Slice $front 'private async safeAbortAfterStart()' 'private async stopProcessOnly()'
$safeAbortCloseCount = @([regex]::Matches($safeAbort, 'adapter\.close\(\)')).Count
# 920 §8.0-D B5 remap: waitForHostRecovery(true) was deleted in E9 and the deletion is self-documented
# inside this very slice ("不再 waitForHostRecovery"). The current read-only observation is
# assertHcSessionClosed on the returned evidence, still with exactly one Close and no retry loop.
if ((Has $safeAbort 'const closed = await this.adapter.close();') -and
    $safeAbortCloseCount -eq 1 -and
    (Has $safeAbort 'assertHcSessionClosed(') -and
    (Has $safeAbort '不再 waitForHostRecovery')) {
  Pass 'T4-FRONT-CLOSE-01' 'Startup rollback sends one Close then validates the returned evidence read-only' 'safeAbortAfterStart'
} else { Fail 'T4-FRONT-CLOSE-01' 'Startup rollback has a duplicate Close path' 'safeAbortAfterStart source boundary' }
# 920 §8.0-D B5 remap: recoverLockedHostBeforeStart was deleted (fanHost.ts L1923-1924 self-documents
# the four removed symbols). Intent preserved: a locked/unknown Host must not gain a second Close path.
# Current equivalent: disable() explicitly refuses to recover those states and performs no Close there.
$lockedRefusal = Slice $front "if (this.state === 'fault-locked' || this.state === 'conflict-locked' || this.state === 'unknown')" "this.revokeCoordinatorWrites('disable');"
$lockedRefusalCloseCount = @([regex]::Matches($lockedRefusal, 'closeHostAfterRestore\(\)|adapter\.close\(\)')).Count
if ((Has $front 'recoverLockedHostBeforeStart / recoverAfterMutationFailure / recoverAfterHidRemoval / isObservedRouteLoss 已删') -and
    (Has $lockedRefusal '不做关闭恢复') -and $lockedRefusalCloseCount -eq 0) {
  Pass 'T4-FRONT-CLOSE-02' 'Fault-locked states are refused with no Close of their own; one Close owner remains' 'locked-state refusal boundary'
} else { Fail 'T4-FRONT-CLOSE-02' 'Fault-locked recovery may duplicate Close' 'locked-state refusal boundary' }
Require-All 'T4-FRONT-LEASE-01' 'Frontend heartbeat/mutation failure recovery is serialized and observer-only after transport loss' $front @(
  'restoreAndRelease', 'startHeartbeat', 'stopHeartbeat', '不再 recoverAfterMutationFailure', '不再 waitForHostRecovery')
$systemPending = Slice $hostText 'private object SuspendUnlocked()' 'private ResumeAdmission StartAutomaticResumeUnlocked('
if ((Has $native 'generation, "native.power"') -and
    (Has $hostText 'RequireNativeSuspendAuthority(body);') -and
    (Has $hostText 'F4_NATIVE_AUTHORITY_REQUIRED') -and
    (Has $systemPending 'CloseForSystemPending()') -and
    !(Has $systemPending 'RestoreHardware(close: true') -and
    !(Has $systemPending 'RestoreOem(') -and
    # ★ 因 FAN-926R 裁决 §3-D1 **改要求**：旧要求 = SystemPending 一律 `skipOemRestore: true`
    # （"strict HC Close-first"）。新要求 = 有写历史且会话可操作时，由唯一 owner 在进入 HC Close
    # **之前**有界执行一次现成默认模式恢复；通知路径本身仍不得直接发起 HC 长调用、不得夺取
    # DeviceManager，也不得以写 0 替代系统睡眠。
    (Has $hostText 'skipOemRestore: !preCloseReleaseAttempted') -and
    (Has $hostText 'power.suspend-preclose-release-decision') -and
    (Has $hostText 'state.OemRestoreEvidence = "not-observed"') -and
    !(Has $app 'fanHostLifecycle.suspend()') -and
    # ★ 因 FAN-926R 执行单 §3.3 **改要求**：resume 的"准入 + 单次入队执行"统一由
    # mutateWithBoundedRetry 承担，重放体抽到 replayResumeCurve()；曲线写入仍是同一个
    # applyMutation(desiredCurve)。断言语义不变：渲染侧只做 F5 重建、走同一写入路径。
    (Has $front '() => this.replayResumeCurve(generation, intentRevision)') -and
    (Has $front 'this.applyMutation(this.desiredCurve as readonly FanNode[])') -and
    (Has $front 'this.resumeCurve = curveToResume') -and
    (Has $front 'await this.applyMutation(curve)')) {
  Pass 'T4-FRONT-SLEEP-01' 'F4 is native-only; the single owner performs ONE bounded default-mode release BEFORE the HC Close (D1); renderer coordinates F5 rebuild' 'native.power; CloseForSystemPending; power.suspend-preclose-release-decision; renderer observation only; F5 resume'
} else { Fail 'T4-FRONT-SLEEP-01' 'F4/F5 ownership or the D1 release-before-close order drifted' 'native/Host/App/fanHost source boundary' }
if ((Has $nav 'void fanHostLifecycle.close().catch(() => {});') -and
    (Has $nav 'await app.exit(0);') -and !(Has $nav 'await fanHostLifecycle.close();')) {
  Pass 'T4-FRONT-EXIT-01' 'Exit button hands off immediately to native parent-exit safety' 'NavRail.quit'
} else { Fail 'T4-FRONT-EXIT-01' 'Exit button can wait behind Fan Host Close' 'NavRail.quit source boundary' }

# Explicit architectural boundaries: not silently promoted to pass.
$fullGraph = Has $hostText 'foreach (IManager manager in ManagerFactory.Managers)'
$powerManagerStarted = Has $hostText 'Invoke(hcPowerProfileManager, "Start")'
if (!$powerManagerStarted) {
  Need 'T4-BOUNDARY-01' 'Full HC ManagerFactory/profile graph is intentionally isolated' `
    'HC starts ManagerFactory.Resume/PowerProfileManager and related Initialized subscriptions.' `
    'YeMan starts only DeviceManager and asserts non-fan managers remain isolated.' `
    'AC/DC/profile subscription timing is not byte-for-byte HC application equivalence.' `
    'Retain isolation; require an explicit architecture decision and real evidence before claiming full graph parity.'
}
Need 'T4-BOUNDARY-02' 'Physical OEM ownership is not a universal HC acknowledgement' `
  'HC virtual Close/Hardware callback has no universal physical EC/ACPI ownership ACK.' `
  'YeMan records callback/readback evidence but cannot prove all physical routes from source/mock tests.' `
  'HTTP success or void Close cannot be treated as physical proof.' `
  'Require per-device real readback evidence; keep hardware writes disabled in T4.'
Need 'T4-BOUNDARY-03' 'Lease/heartbeat is a YeMan safety layer without a direct HC analogue' `
  'HC owns device lifecycle through application/profile managers, without a Fan API lease token.' `
  'YeMan adds a 15-second lease, heartbeat and conflict recovery around the HC callback.' `
  'Safety behavior is testable but not source-identical to HC.' `
  'Retain lease tests and real conflict evidence; do not classify as HC defect.'

$sourcePaths = @($hcWindowPath, $hcDevicePath, $hcRogPath, $HostSource, $NativeSource, $FrontendSource, $ApiSource, $NavSource)
$hashes = foreach ($path in $sourcePaths) { [ordered]@{ path=$path; sha256=(Sha256 $path) } }
$record = [ordered]@{
  audit = 'T4-HC-PARITY'
  capturedAtUtc = [DateTime]::UtcNow.ToString('o')
  hardwareWrites = $false
  checks = $checks
  needsInvestigation = $needs
  summary = [ordered]@{ pass = @($checks | Where-Object status -eq 'pass').Count; fail = @($checks | Where-Object status -eq 'fail').Count; p1 = $needs.Count }
  sourceHashes = $hashes
}
$record | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputRoot 't4-parity.json') -Encoding UTF8
$needs | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputRoot 't4-needs-investigation.json') -Encoding UTF8
$checks | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputRoot 't4-checks.json') -Encoding UTF8
$hashLines = foreach ($f in Get-ChildItem -LiteralPath $OutputRoot -File | Sort-Object Name) { "{0}  {1}" -f (Sha256 $f.FullName), $f.Name }
$hashLines | Set-Content -LiteralPath (Join-Path $OutputRoot 'sha256.txt') -Encoding ASCII

$summary = @"
# T4 HC Parity Audit

- Generated UTC: $($record.capturedAtUtc)
- Hardware writes: false
- Checks passed: $($record.summary.pass)
- Checks failed: $($record.summary.fail)
- Explicit needs-investigation P1 boundaries: $($record.summary.p1)

Standing rule: any unexplained difference, unproven success, or pass without
matching source evidence remains needs-investigation. HW temperature is the
only pre-excluded difference. This audit does not claim physical OEM success.
"@
$summary | Set-Content -LiteralPath (Join-Path $OutputRoot 't4-summary.md') -Encoding UTF8
if ($record.summary.fail -gt 0) { throw "T4 source audit failed: $($record.summary.fail) check(s)" }
Write-Output ("T4 HC parity audit: PASS (checks={0}; needs-investigation-P1={1}; hardwareWrites=false; output={2})" -f $record.summary.pass, $record.summary.p1, $OutputRoot)
