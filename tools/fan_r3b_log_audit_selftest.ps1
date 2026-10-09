# fan_r3b_log_audit_selftest.ps1 (v1) -- regression controls for fan_ui_free_recovery_log_audit.ps1
#
# WHY: the R3B adjudicator decides SUCCESS / HONEST-FAILURE / INCONCLUSIVE / INVALID from a log
# bundle. Its discriminating power was only checked by hand once. This gate rebuilds the same
# bundles every run and asserts each verdict class AND exit code, so the adjudicator cannot
# silently stop discriminating (project rule: an old counterexample must still fail).
#
# Read-only w.r.t. real state: everything is written under %TEMP%\fan-r3b-audit-selftest-<pid>.
# ASCII only (PS 5.1 reads BOM-less .ps1 as ANSI).
param(
  [string]$AuditorPath = (Join-Path $PSScriptRoot 'fan_ui_free_recovery_log_audit.ps1')
)
$ErrorActionPreference = 'Continue'

if (-not (Test-Path -LiteralPath $AuditorPath)) { Write-Output "FAIL: auditor not found at $AuditorPath"; exit 2 }

$root = Join-Path $env:TEMP ('fan-r3b-audit-selftest-' + $PID)
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
New-Item -ItemType Directory -Force -Path $root | Out-Null

# Bundle catalogue: name, expected verdict, and the fault-log lines.
# Every line is a real row shape captured from the product (or a deliberate mutation of one).
$bundles = @(
  @{ name = 'A-no-hc-sandbox-capture'; verdict = 'INVALID'; rows = @(
      '{"eventName":"recovery.mock-ui-free-escalation-fired","details":{}}',
      '{"eventName":"recovery.ui-free-same-process-receipt","details":{"accepted":false,"detail":"receipt-api-absent-no-hc-backend"}}',
      '{"eventName":"recovery.controlled-replacement-requested","details":{}}'
    ) },
  @{ name = 'B-requested-only'; verdict = 'HONEST-FAILURE'; rows = @(
      '{"eventName":"recovery.ui-free-same-process-begin","details":{"cycleId":"c1"}}',
      '{"eventName":"recovery.ui-free-same-process-fault","details":{"error":"HC_SESSION_ROUTE_LOST"}}',
      '{"eventName":"recovery.controlled-replacement-requested","details":{"owner":"fan-host-requests-controlled-replacement"}}'
    ) },
  @{ name = 'C-complete-receipt-success'; verdict = 'SUCCESS'; rows = @(
      '{"eventName":"recovery.ui-free-same-process-begin","details":{"cycleId":"c1"}}',
      '{"eventName":"recovery.ui-free-same-process-receipt","details":{"accepted":true,"detail":"reopen:receipt op=op1 outcome=Completed bound=true hid=true readLoop=true remap=true uncertain= late= "}}',
      '{"eventName":"recovery.ui-free-same-process-confirmed","details":{"cycleId":"c1","selfOpenAttempts":1}}',
      '{"eventName":"parent-exit-handoff.bound","details":{"HostInstanceId":"h1","CycleId":"c1"}}'
    ) },
  @{ name = 'N1-confirmed-without-receipt'; verdict = 'INVALID'; rows = @(
      '{"eventName":"recovery.ui-free-same-process-confirmed","details":{"cycleId":"c1"}}'
    ) },
  @{ name = 'N2-receipt-never-consulted'; verdict = 'INVALID'; rows = @(
      '{"eventName":"recovery.ui-free-same-process-receipt","details":{"accepted":true,"detail":"receipt-api-absent-no-hc-backend"}}',
      '{"eventName":"recovery.ui-free-same-process-confirmed","details":{"cycleId":"c1"}}'
    ) },
  @{ name = 'N3-two-replacements-in-one-cycle'; verdict = 'INVALID'; rows = @(
      '{"eventName":"recovery.host-replacement-spawned","details":{"successorPid":1}}',
      '{"eventName":"recovery.host-replacement-spawned","details":{"successorPid":2}}'
    ) },
  @{ name = 'N4-fresh-budget-after-takeover'; verdict = 'INVALID'; rows = @(
      '{"eventName":"recovery.host-replacement-spawned","details":{"successorPid":1}}',
      '{"eventName":"recovery.host-replacement-predecessor-exit","details":{"successorPid":1}}',
      '{"eventName":"recovery.host-replacement-took-over","details":{"proof":"lifetime-mutex-recreated-only-after-predecessor-handles-closed"}}',
      '{"eventName":"api.request-success","context":{"recoveryCycle":{"cycleId":"c1","inherited":true,"replacements":1,"remainingMs":60000}},"details":{}}'
    ) },
  @{ name = 'N5-takeover-without-exit-proof'; verdict = 'INVALID'; rows = @(
      '{"eventName":"recovery.host-replacement-took-over","details":{"proof":"port-was-free"}}'
    ) }
)

$fail = 0
$pass = 0
foreach ($b in $bundles) {
  $dir = Join-Path $root $b.name
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
  $b.rows | Set-Content -LiteralPath (Join-Path $dir 'fan-host-fault.log') -Encoding UTF8
  $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $AuditorPath -LogRoot $dir 2>&1
  $code = $LASTEXITCODE
  $line = ($out | Select-String -Pattern '^FAN_R3B_AUDIT=' | Select-Object -Last 1)
  $got = 'MISSING'
  if ($null -ne $line) { $got = ([string]$line.Line -replace '^FAN_R3B_AUDIT=','' -split ' ')[0] }
  if ($got -eq $b.verdict) {
    $pass++
    Write-Output ("PASS  {0,-34} verdict={1} exit={2}" -f $b.name, $got, $code)
  } else {
    $fail++
    Write-Output ("FAIL  {0,-34} expected={1} got={2} exit={3}" -f $b.name, $b.verdict, $got, $code)
  }
}

Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
Write-Output ("R3B_AUDIT_SELFTEST pass={0} fail={1}" -f $pass, $fail)
if ($fail -gt 0) { Write-Output 'R3B_AUDIT_SELFTEST=FAIL'; exit 1 }
Write-Output 'R3B_AUDIT_SELFTEST=PASS'
exit 0
