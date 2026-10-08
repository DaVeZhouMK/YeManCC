$ErrorActionPreference = 'Stop'

# --self-test prints raw runtime log lines first; the machine-readable result is
# the last JSON line. Parse from the tail instead of converting the whole blob.
function Read-SelfTestJson([string]$raw) {
  $lines = @($raw -split "`r?`n")
  for ($i = $lines.Count - 1; $i -ge 0; $i--) {
    $trimmed = $lines[$i].Trim()
    if ($trimmed.Length -eq 0) { continue }
    try { return ($trimmed | ConvertFrom-Json) } catch { }
  }
  throw 'self-test output did not contain a JSON line'
}

# R1/R4 lifecycle-only safe Mock. The executable is invoked without
# --real-backend, so HC, EC, ACPI, HID and hardware writes cannot be reached.
$exe = Join-Path $PSScriptRoot 'bin\Release\net10.0-windows10.0.19041.0\win-x64\YeManFanHost.exe'
$required = @(
  'r1-hc-system-pending-order',
  'r1-hc-system-ready-order',
  'r4-hc-window-closed-order',
  'r1-suspend-resume-mock',
  'r1-resume-during-suspend-mock',
  'r4-close-during-resume-mock',
  'r1-isready-10s-timeout-open',
  'r1-open-failure-no-openevents',
  'r1-openevents-failure-no-ready',
  'r1-external-applied-bounded-retry',
  'r1-external-profile-session-generation',
  'r1-external-discarded-boundary',
  'r4-manager-initializing-boundary',
  'r4-exit-boundary-no-busy-loop',
  'r4-close-failure-reopen-entry',
  'r4-duplicate-power-events',
  'r1-fan-only-open-openevents-boundary',
  'r1-fan-only-system-pending-close-retains-manager',
  'r1-fan-only-system-ready-reopen-boundary',
  'r4-fan-only-window-closed-single-owner'
)

try {
  if (-not (Test-Path -LiteralPath $exe)) { throw "safe Host binary missing: $exe" }
  $raw = (& $exe --self-test --port 8797 | Out-String).Trim()
  $result = Read-SelfTestJson $raw
  $missing = @($required | Where-Object { $_ -notin @($result.checks) })
  if ($result.ok -ne $true -or $missing.Count -ne 0) {
    throw "R1/R4 lifecycle self-test failed; missing=$($missing -join ',')"
  }
  [ordered]@{
    ok = $true
    mode = 'R1-R4-lifecycle-safe-mock-only'
    requiredChecks = $required
    missing = @()
    hardwareWritesEnabled = $false
    hardwareWritesObserved = $false
    rawSelfTest = $result
  } | ConvertTo-Json -Depth 12
}
catch {
  [ordered]@{
    ok = $false
    mode = 'R1-R4-lifecycle-safe-mock-only'
    hardwareWritesEnabled = $false
    hardwareWritesObserved = $false
    error = $_.Exception.Message
  } | ConvertTo-Json -Depth 12
  exit 1
}
