<#
.SYNOPSIS
  Read-only source audit for the HC sleep/wake four-domain parity
  (gyro data linkage / sleep / gyro wake / controller wake).

  Mirrors the project discipline used by fan_hc_lifecycle_order_audit.ps1:
  frozen HC source + YMCC native source are the only inputs; nothing is
  executed, no hardware/WMI/HID is touched, nothing is written.

  Covers the C47 closure (24-UNIFIED-LIFECYCLE-AND-COORDINATION-CONTRACT):
   - D:G1 long-sleep (>=30min) resume grace sleeps 3s before recovery work
   - D:G3 Kernel-Power 507 Reason=7 (HC WakeReason.Joystick) enters the
     accidental-wake re-sleep evaluation, but ONLY outside the entry-failure
     window
  Forward (positive) and reverse (negative/guard) assertions both run.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcRoot,
  [Parameter(Mandatory = $true)][string]$NativeSource
)

$ErrorActionPreference = 'Stop'
$hcWindow = Join-Path $HcRoot 'Views\Windows\MainWindow.xaml.cs'
$hcSettings = Join-Path $HcRoot 'Properties\Settings.Designer.cs'
foreach ($path in @($hcWindow, $hcSettings, $NativeSource)) {
  if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required source is missing: $path" }
}

function Require-Order([string]$name, [string]$text, [string[]]$tokens) {
  $positions = @()
  foreach ($token in $tokens) {
    $position = $text.IndexOf($token, [StringComparison]::Ordinal)
    if ($position -lt 0) { throw "$name missing token: $token" }
    $positions += $position
  }
  for ($i = 1; $i -lt $positions.Count; $i++) {
    if ($positions[$i] -le $positions[$i - 1]) {
      throw "$name order mismatch: '$($tokens[$($i - 1)])' must precede '$($tokens[$i])'"
    }
  }
}

function Require-Absent([string]$name, [string]$text, [string]$token, [string]$what) {
  if ($text.IndexOf($token, [StringComparison]::Ordinal) -ge 0) {
    throw "$name must NOT contain $what - '$token'"
  }
}

function Slice([string]$text, [string]$startToken, [string]$endToken) {
  $start = $text.IndexOf($startToken, [StringComparison]::Ordinal)
  if ($start -lt 0) { throw "Missing source boundary: $startToken" }
  $end = $text.IndexOf($endToken, $start + $startToken.Length, [StringComparison]::Ordinal)
  if ($end -lt 0) { $end = $text.Length }
  return $text.Substring($start, $end - $start)
}

$hcWindowText = Get-Content -LiteralPath $hcWindow -Raw -Encoding UTF8
$hcSettingsText = Get-Content -LiteralPath $hcSettings -Raw -Encoding UTF8
$native = Get-Content -LiteralPath $NativeSource -Raw -Encoding UTF8

# ── HC baseline (frozen source) ────────────────────────────────────────────
# G1: SystemReady resumes managers only after a >=30min long-sleep 3s delay.
$ready = Slice $hcWindowText 'case SystemManager.SystemStatus.SystemReady:' 'case SystemManager.SystemStatus.SystemPending:'
Require-Order 'HC SystemReady (G1 baseline)' $ready @(
  'resumeTime = DateTime.Now;',
  'sleepDuration.TotalMinutes >= 30',
  'Task.Delay(3000)',
  'TimerManager.Start();',
  'SensorsManager.Resume(true);'
)

# G3: joystick wake is default re-sleep in HC (attribute precedes the property).
$joystickProp = $hcSettingsText.IndexOf('public bool GoBackToSleepOnJoystick', [StringComparison]::Ordinal)
if ($joystickProp -lt 0) { throw 'HC missing GoBackToSleepOnJoystick setting (Settings.Designer.cs)' }
$joystickAttrs = $hcSettingsText.Substring(
  [Math]::Max(0, $joystickProp - 200), [Math]::Min(200, $joystickProp))
if ($joystickAttrs -notmatch 'DefaultSettingValueAttribute\("True"\)') {
  throw 'HC GoBackToSleepOnJoystick must default True (Settings.Designer.cs)'
}

# ── YMCC forward (positive) assertions ─────────────────────────────────────
# G1: grace helper exists, is const-factored, sleeps 3000ms once per generation.
if ($native -notmatch 'static void sgApplyLongSleepResumeGrace\(unsigned long long generation\)') {
  throw 'YMCC missing sgApplyLongSleepResumeGrace (G1)'
}
if ($native -notmatch 'SG_LONG_SLEEP_MIN_EPOCH_DELTA = 30\.0 \* 60\.0') {
  throw 'YMCC missing SG_LONG_SLEEP_MIN_EPOCH_DELTA = 30min (G1)'
}
if ($native -notmatch 'SG_LONG_SLEEP_RESUME_GRACE_MS = 3000') {
  throw 'YMCC missing SG_LONG_SLEEP_RESUME_GRACE_MS = 3000 (G1)'
}
if ($native -notmatch 'g_sgLongSleepGraceGeneration == generation') {
  throw 'YMCC long-sleep grace must be per-generation once (G1)'
}
# The grace must be applied BEFORE recovery/rearm side effects in the work loop.
# Branch order in sgWorkLoop: WakeAutomatic → WakeHibernate → WakeSuspend.
$wakeSuspend = Slice $native 'else if (item.kind == SgWork::WakeSuspend)' "        } catch ("
# Grace must precede sgRealWake inside the user-wake arm.
if ($wakeSuspend.IndexOf('sgApplyLongSleepResumeGrace(item.generation)') -lt 0) {
  throw 'YMCC WakeSuspend arm missing grace call (G1)'
}
$wakeHibernate = Slice $native 'else if (item.kind == SgWork::WakeHibernate)' 'else if (item.kind == SgWork::WakeSuspend)'
if ($wakeHibernate.IndexOf('sgApplyLongSleepResumeGrace(item.generation)') -lt 0) {
  throw 'YMCC WakeHibernate arm missing grace call (G1)'
}

# G3: 507 Reason=7 (joystick) plumbed into the accidental-wake evaluation.
if ($native -notmatch 'static void sgNoteExternalDeviceKernel507Reason7\(\)') {
  throw 'YMCC missing sgNoteExternalDeviceKernel507Reason7 (G3)'
}
if ($native -notmatch 'else if \(g_sgLastS0WakeReason == 7\)\s*\r?\n\s*' +
    'sgNoteExternalDeviceKernel507Reason7\(\);') {
  throw 'YMCC WM_SG_S0_WAKE must route Reason=7 to joystick handler (G3)'
}

# ── YMCC reverse (negative/guard) assertions ───────────────────────────────
# G1: automatic (non-user) wakes must NOT consume the grace; grace belongs to
# explicit user wake / hibernate resume arms only.
$auto = Slice $native 'else if (item.kind == SgWork::WakeAutomatic)' 'else if (item.kind == SgWork::WakeHibernate)'
Require-Absent 'YMCC WakeAutomatic (G1 reverse)' $auto 'sgApplyLongSleepResumeGrace' 'long-sleep grace'

# G3: entry-failure window semantics are preserved -- Reason=7 inside the
# window must stay an entry-failure retry, not an accidental-wake evaluation.
$s0Wake = Slice $native 'case WM_SG_S0_WAKE:' 'case WM_SG_S4_WAKE:'
$ifEntry = $s0Wake.IndexOf('if (entryFailure) {')
$ifJoystick = $s0Wake.IndexOf('sgNoteExternalDeviceKernel507Reason7();')
if ($ifEntry -lt 0) { throw 'YMCC missing entryFailure branch (G3 reverse)' }
if ($ifJoystick -lt 0) { throw 'YMCC missing joystick branch (G3 reverse)' }
if ($ifJoystick -le $ifEntry) {
  throw 'YMCC joystick branch must be AFTER entryFailure branch (G3 reverse)'
}

Write-Host 'PASS sleep/wake four-domain HC parity source audit'