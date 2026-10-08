#requires -Version 5.1
<#
  FAN-936-R1 section 5.2 - MSI read-only CLI offline Gate.

  The adjudication requires the read-only tool to be proven through the ACTUAL
  CLI entry point (YeManFanHost.exe --msi-readonly-sample), covering the
  Load/Initialize/Dispose path with zero Open and zero hardware write. Calling
  HC methods directly via UninitializedObject (the old group B) cannot stand in
  for the CLI path.

  This gate is strictly OFFLINE: it only ever runs against a runtime root that
  carries the fake System.Management boundary, so no real device is sampled and
  nothing is written to hardware. It must never be pointed at a real 9950 device.

  Group A (positive): the offline runtime is present.
    - process exits 0 within a hard timeout (bounded exit, no unbounded wait)
    - loadOutcome is loaded/already-loaded and hcCoreInitialized is true,
      proving the Load + Initialize path actually ran through the CLI
    - sampleOutcome is "sampled"
    - hcOpen=false, openAttempted=false, readOnlyNoHardwareWrite=true and the
      msiState-level readOnlyNoHardwareWrite/deviceOpen/clawOpen flags confirm
      zero Open / zero hardware write
    - firmware is genuinely unknown before Open(), so firmwareKnown=false,
      firmware="unknown", firmwareHex="" - never a fabricated measured 0x0000

  Group B (negative): the runtime root is missing.
    - process exits non-zero (a failed sample must not exit 0 pretending success)
    - loadOutcome="failed" and sampleOutcome != "sampled"
    - a JSON snapshot is still emitted and openAttempted stays false

  Both groups are launched with YEMANCC_SELFTEST_LOG_ROOT pointed at an isolated
  root so the production ledger is never polluted.

  Usage (copy/paste):
    powershell -NoProfile -ExecutionPolicy Bypass -File .\run-msi-readonly-cli-gate.ps1 `
      -HostDir  '<deploy-root-with-YeManFanHost.payload.json>' `
      -HcRuntimeRoot '<offline-runtime-with-fake-System.Management.dll>' `
      -HcDeviceType 'HandheldCompanion.Devices.ClawA1M'

  Always exit 1 on any assertion failure.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HostDir,
  [Parameter(Mandatory = $true)][string]$HcRuntimeRoot,
  [string]$HcDeviceType = 'HandheldCompanion.Devices.ClawA1M',
  [string]$IsolationRoot = (Join-Path $env:TEMP ('fan936-cli-gate-' + [Guid]::NewGuid().ToString('N'))),
  [int]$TimeoutSeconds = 90,
  [string]$ExpectedFakeSystemManagementSha256
)

$ErrorActionPreference = 'Stop'

# The machine-readable result is the final JSON line; runtime/diagnostic log
# lines may precede it, so parse from the tail instead of the whole blob.
function Read-TailJson([string]$raw) {
  $lines = @($raw -split "`r?`n")
  for ($i = $lines.Count - 1; $i -ge 0; $i--) {
    $trimmed = $lines[$i].Trim()
    if ($trimmed.Length -eq 0) { continue }
    try { return ($trimmed | ConvertFrom-Json) } catch { }
  }
  throw 'host output did not contain a JSON line'
}

# JsonSerializerDefaults.Web serializes camelCase; look properties up
# case-insensitively so the assertions do not depend on exact casing.
function Get-Prop($obj, [string]$name) {
  if ($null -eq $obj) { return $null }
  $prop = $obj.PSObject.Properties | Where-Object { $_.Name -ieq $name } | Select-Object -First 1
  if ($null -eq $prop) { return $null }
  return $prop.Value
}

function Invoke-HostReadOnlySample([string]$RuntimeRoot, [string]$Label) {
  $exe = Join-Path $HostDir 'YeManFanHost.exe'
  if (-not (Test-Path -LiteralPath $exe)) { throw "Host executable missing: $exe" }

  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $exe
  $psi.WorkingDirectory = $HostDir
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.CreateNoWindow = $true
  $psi.Arguments = '--msi-readonly-sample --hc-runtime-root "{0}" --hc-device-type "{1}"' -f $RuntimeRoot, $HcDeviceType
  $psi.EnvironmentVariables['YEMANCC_SELFTEST_LOG_ROOT'] = $IsolationRoot

  $proc = New-Object System.Diagnostics.Process
  $proc.StartInfo = $psi
  $null = $proc.Start()
  $outTask = $proc.StandardOutput.ReadToEndAsync()
  $errTask = $proc.StandardError.ReadToEndAsync()

  # Process-level hard timeout: a hung Host must never make the gate wait
  # forever. The Host's own STA bound is 15s, so a healthy run finishes well
  # inside the default window.
  $exited = $proc.WaitForExit($TimeoutSeconds * 1000)
  $timedOut = -not $exited
  if ($timedOut) {
    try { $proc.Kill() } catch { }
    $null = $proc.WaitForExit(5000)
  }

  $stdout = ''
  $stderr = ''
  try { $stdout = $outTask.Result } catch { }
  try { $stderr = $errTask.Result } catch { }
  $exitCode = $null
  if (-not $timedOut) { $exitCode = $proc.ExitCode }
  $proc.Dispose()

  $json = $null
  $parseError = $null
  if (-not [string]::IsNullOrWhiteSpace($stdout)) {
    try { $json = Read-TailJson $stdout } catch { $parseError = $_.Exception.Message }
  }

  return [ordered]@{
    label = $Label
    exitCode = $exitCode
    timedOut = $timedOut
    json = $json
    parseError = $parseError
    stdout = $stdout.Trim()
    stderr = $stderr.Trim()
  }
}

try {
  if (-not (Test-Path -LiteralPath $HostDir)) { throw "HostDir is missing: $HostDir" }
  $payloadManifest = Join-Path $HostDir 'YeManFanHost.payload.json'
  if (-not (Test-Path -LiteralPath $payloadManifest)) { throw "HostDir lacks YeManFanHost.payload.json: $HostDir" }
  if (-not (Test-Path -LiteralPath $HcRuntimeRoot)) { throw "HcRuntimeRoot is missing: $HcRuntimeRoot" }
  New-Item -ItemType Directory -Force -Path $IsolationRoot | Out-Null

  $absentRuntime = Join-Path $IsolationRoot 'absent-runtime'
  if (Test-Path -LiteralPath $absentRuntime) { Remove-Item -Recurse -Force -LiteralPath $absentRuntime }

  $exe = Join-Path $HostDir 'YeManFanHost.exe'
  $commandA = '{0} --msi-readonly-sample --hc-runtime-root "{1}" --hc-device-type "{2}"' -f $exe, $HcRuntimeRoot, $HcDeviceType
  $commandB = '{0} --msi-readonly-sample --hc-runtime-root "{1}" --hc-device-type "{2}"' -f $exe, $absentRuntime, $HcDeviceType

  $failures = New-Object System.Collections.Generic.List[string]

  # Offline proof: the runtime root must carry the fake System.Management
  # boundary, so no real device can be reached.
  if ($PSBoundParameters.ContainsKey('ExpectedFakeSystemManagementSha256')) {
    $smPath = Join-Path $HcRuntimeRoot 'System.Management.dll'
    if (-not (Test-Path -LiteralPath $smPath)) {
      $failures.Add('offline: System.Management.dll is missing from the runtime root')
    }
    else {
      $smSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $smPath).Hash.ToLowerInvariant()
      if ($smSha -ne $ExpectedFakeSystemManagementSha256.ToLowerInvariant()) {
        $failures.Add("offline: runtime System.Management.dll sha '$smSha' does not match the expected fake boundary '$ExpectedFakeSystemManagementSha256'")
      }
    }
  }

  $groupA = Invoke-HostReadOnlySample $HcRuntimeRoot 'A-positive'
  $groupB = Invoke-HostReadOnlySample $absentRuntime 'B-negative'

  # ---- Group A: positive offline sample through the CLI entry point ----
  if ($groupA.timedOut) { $failures.Add("A: process exceeded the $TimeoutSeconds s hard timeout (unbounded wait / hang)") }
  if ($groupA.exitCode -ne 0) { $failures.Add("A: expected exit 0, got $($groupA.exitCode)") }
  if ($null -eq $groupA.json) {
    $failures.Add("A: no parseable JSON snapshot ($($groupA.parseError))")
  }
  else {
    $j = $groupA.json
    if ((Get-Prop $j 'hcOpen') -ne $false) { $failures.Add('A: hcOpen must be false (the read-only CLI path must not Open)') }
    if ((Get-Prop $j 'openAttempted') -ne $false) { $failures.Add('A: openAttempted must be false') }
    if ((Get-Prop $j 'readOnlyNoHardwareWrite') -ne $true) { $failures.Add('A: readOnlyNoHardwareWrite must be true') }
    if ((Get-Prop $j 'hcCoreInitialized') -ne $true) { $failures.Add('A: hcCoreInitialized must be true (the Load/Initialize path must run)') }
    $loadOutcomeA = [string](Get-Prop $j 'loadOutcome')
    if ($loadOutcomeA -ne 'loaded' -and $loadOutcomeA -ne 'already-loaded') {
      $failures.Add("A: loadOutcome must be loaded/already-loaded, got '$loadOutcomeA'")
    }
    $sampleOutcomeA = [string](Get-Prop $j 'sampleOutcome')
    if ($sampleOutcomeA -ne 'sampled') { $failures.Add("A: sampleOutcome must be 'sampled', got '$sampleOutcomeA'") }

    $msi = Get-Prop $j 'msiState'
    if ($null -eq $msi) {
      $failures.Add('A: msiState is missing from the snapshot')
    }
    else {
      if ((Get-Prop $msi 'readOnlyNoHardwareWrite') -ne $true) { $failures.Add('A: msiState.readOnlyNoHardwareWrite must be true') }
      if ((Get-Prop $msi 'deviceOpen') -ne $false) { $failures.Add('A: msiState.deviceOpen must be false') }
      if ((Get-Prop $msi 'clawOpen') -ne $false) { $failures.Add('A: msiState.clawOpen must be false') }

      $firmwareKnown = Get-Prop $msi 'firmwareKnown'
      $firmware = [string](Get-Prop $msi 'firmware')
      $firmwareHex = [string](Get-Prop $msi 'firmwareHex')
      if ($firmwareKnown -ne $false) { $failures.Add('A: firmwareKnown must be false in a pre-Open sample') }
      if ($firmware -eq '0x0000' -or $firmware -eq '0x0' -or $firmware -eq '0') {
        $failures.Add("A: unknown firmware must not be reported as a measured 0 (firmware='$firmware')")
      }
      if ($firmware -ne 'unknown') { $failures.Add("A: unknown firmware must be reported as 'unknown', got '$firmware'") }
      if ($firmwareHex -ne '') { $failures.Add("A: firmwareHex must be empty when firmware is unknown, got '$firmwareHex'") }
    }
  }

  # ---- Group B: missing runtime must fail loudly, never exit 0 ----
  if ($groupB.timedOut) { $failures.Add("B: process exceeded the $TimeoutSeconds s hard timeout (unbounded wait / hang)") }
  if ($groupB.exitCode -eq 0) { $failures.Add('B: a failed sample must not exit 0 pretending success') }
  if (-not $groupB.timedOut -and $null -eq $groupB.exitCode) { $failures.Add('B: exit code was unavailable') }
  if ($null -eq $groupB.json) {
    $failures.Add("B: expected a JSON snapshot even on failure ($($groupB.parseError))")
  }
  else {
    $loadOutcomeB = [string](Get-Prop $groupB.json 'loadOutcome')
    $sampleOutcomeB = [string](Get-Prop $groupB.json 'sampleOutcome')
    if ($loadOutcomeB -ne 'failed') { $failures.Add("B: loadOutcome must be 'failed', got '$loadOutcomeB'") }
    if ($sampleOutcomeB -eq 'sampled') { $failures.Add("B: sampleOutcome must not be 'sampled' when the runtime is absent") }
    if ((Get-Prop $groupB.json 'openAttempted') -ne $false) { $failures.Add('B: openAttempted must remain false even on failure') }
  }

  $ok = ($failures.Count -eq 0)
  [ordered]@{
    ok = $ok
    mode = 'MSI-readonly-CLI-gate'
    hostDir = $HostDir
    hcRuntimeRoot = $HcRuntimeRoot
    hcDeviceType = $HcDeviceType
    isolationRoot = $IsolationRoot
    timeoutSeconds = $TimeoutSeconds
    realDeviceSampling = $false
    hardwareWritesEnabled = $false
    commandA = $commandA
    commandB = $commandB
    groupA = $groupA
    groupB = $groupB
    failures = @($failures)
  } | ConvertTo-Json -Depth 12

  if (-not $ok) { exit 1 }
}
catch {
  [ordered]@{
    ok = $false
    mode = 'MSI-readonly-CLI-gate'
    realDeviceSampling = $false
    hardwareWritesEnabled = $false
    error = $_.Exception.Message
  } | ConvertTo-Json -Depth 12
  exit 1
}
