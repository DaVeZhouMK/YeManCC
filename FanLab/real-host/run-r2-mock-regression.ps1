$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http

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

# R2-only safe Mock regression. The Host is started without --real-backend,
# therefore it never loads HC and cannot call Open, EC, ACPI, HID or a write.
$exe = Join-Path $PSScriptRoot 'bin\Release\net10.0-windows10.0.19041.0\win-x64\YeManFanHost.exe'
$port = 8772
$base = "http://127.0.0.1:$port"
$session = -join (1..32 | ForEach-Object { '{0:x2}' -f (Get-Random -Minimum 0 -Maximum 256) })
$sessionPath = Join-Path ([System.IO.Path]::GetTempPath()) ("YeManFanHost-r2-{0}.session" -f $PID)
$session | Set-Content -LiteralPath $sessionPath -Encoding ASCII -NoNewline
$proc = $null
$checks = [System.Collections.Generic.List[object]]::new()

function Invoke-Api([string]$method, [string]$path, [string]$body = '{}') {
  $client = [System.Net.Http.HttpClient]::new()
  try {
    $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::$method, "$base$path")
    $request.Headers.Add('X-YeMan-Fan-Session', $session)
    if ($method -ne 'GET') { $request.Content = [System.Net.Http.StringContent]::new($body, [Text.Encoding]::UTF8, 'application/json') }
    $response = $client.SendAsync($request).Result
    $raw = $response.Content.ReadAsStringAsync().Result
    return [pscustomobject]@{ status = [int]$response.StatusCode; json = ($raw | ConvertFrom-Json) }
  }
  finally { $client.Dispose() }
}

function Check([string]$name, [bool]$passed, [string]$detail) {
  $checks.Add([ordered]@{ name = $name; passed = $passed; detail = $detail })
  if (-not $passed) { throw "R2 check failed: $name - $detail" }
}

try {
  if (-not (Test-Path -LiteralPath $exe)) { throw "safe Host binary missing: $exe" }
  $selfTestRaw = (& $exe --self-test --port 8799 | Out-String).Trim()
  $selfTest = Read-SelfTestJson $selfTestRaw
  $requiredIntegrationChecks = @(
    'r2-callback-queue-api-suspend-windows-resume',
    'r2-callback-queue-windows-suspend-api-resume',
    'r2-callback-duplicate-suspend-resume',
    'r2-callback-concurrent-admission',
    'r2-callback-queue-rejection-reconciles',
    'r2-queued-suspend-close-failure-resume',
    'r2-single-close-owner'
  )
  $missingIntegration = @($requiredIntegrationChecks | Where-Object { $_ -notin @($selfTest.checks) })
  Check 'callback-queue-integration-selftest' ($selfTest.ok -eq $true -and $missingIntegration.Count -eq 0) "missing=$($missingIntegration -join ',')"
  $proc = Start-Process -FilePath $exe -ArgumentList @('--port', [string]$port, '--protocol-version', '2', '--session-token-file', $sessionPath) -PassThru -WindowStyle Hidden
  $ready = $false
  for ($i = 0; $i -lt 40; $i++) {
    try { $null = Invoke-RestMethod "$base/health" -Headers @{ 'X-YeMan-Fan-Session' = $session } -TimeoutSec 1; $ready = $true; break } catch { Start-Sleep -Milliseconds 100 }
  }
  if (-not $ready) { throw 'safe Host did not become ready' }

  # Tagged power contract (2026-10-01): /api/suspend and /api/resume require a
  # body carrying a positive generation and a non-empty source. A duplicate edge
  # reuses the accepted generation so it is suppressed as the same physical edge.

  # Reverse notification first: a resume edge with no preceding sleep edge must
  # never manufacture a resume/Open. The ingress suppresses the same-state
  # (already-Ready) edge; with no matching receipt the resume API fails closed.
  $reverseResume = Invoke-Api 'POST' '/api/resume' '{"generation":1,"source":"selftest.native"}'
  Check 'reverse-resume-before-suspend' ($reverseResume.status -eq 409 -and $reverseResume.json.error.code -eq 'HC_RESUME_RESULT_EXPIRED') "status=$($reverseResume.status) code=$($reverseResume.json.error.code)"

  # Forward suspend/resume and duplicate notifications. No duplicate call may
  # move the safe engine out of its already-confirmed boundary.
  $suspend = Invoke-Api 'POST' '/api/suspend' '{"generation":2,"source":"selftest.native"}'
  $duplicateSuspend = Invoke-Api 'POST' '/api/suspend' '{"generation":2,"source":"selftest.native"}'
  Check 'forward-suspend' ($suspend.status -eq 200 -and $suspend.json.state.state -eq 'Suspended') "state=$($suspend.json.state.state)"
  Check 'duplicate-suspend-idempotent' ($duplicateSuspend.status -eq 200 -and $duplicateSuspend.json.state.state -eq 'Suspended') "state=$($duplicateSuspend.json.state.state)"
  $resume = Invoke-Api 'POST' '/api/resume' '{"generation":3,"source":"selftest.native"}'
  $duplicateResume = Invoke-Api 'POST' '/api/resume' '{"generation":3,"source":"selftest.native"}'
  Check 'forward-resume' ($resume.status -eq 200) "status=$($resume.status)"
  Check 'duplicate-resume-idempotent' ($duplicateResume.status -eq 200) "status=$($duplicateResume.status)"

  # Re-enter the suspended edge for the close competition. The API close
  # wrapper closes the ingress before the single engine Close owner runs, so a
  # late resume cannot enqueue a new Open while close is in progress.
  $null = Invoke-Api 'POST' '/api/suspend' '{"generation":4,"source":"selftest.native"}'
  # The compiled self-test above runs the actual ingress concurrently. The
  # endpoint portion below exercises the same close/resume ordering through
  # the safe HTTP boundary without creating child PowerShell jobs that could
  # prompt for interactive input on older Windows PowerShell hosts.
  $close1 = Invoke-Api 'POST' '/api/close'
  $lateResume = Invoke-Api 'POST' '/api/resume' '{"generation":5,"source":"selftest.native"}'
  $close2 = Invoke-Api 'POST' '/api/close'
  $competition = @(
    [pscustomobject]@{ status = $close1.status; kind = 'close' },
    [pscustomobject]@{ status = $lateResume.status; kind = 'resume' },
    [pscustomobject]@{ status = $close2.status; kind = 'close' }
  )
  $competitionOk = ($competition.Count -eq 3 -and @($competition | Where-Object { $_.status -notin @(200, 409) }).Count -eq 0)
  Check 'suspend-resume-close-competition' $competitionOk (($competition | ConvertTo-Json -Compress -Depth 4))

  $state = Invoke-Api 'GET' '/api/state'
  Check 'competition-terminal-state' ($state.status -eq 200 -and $state.json.state.state -eq 'Stopped') "state=$($state.json.state.state)"
  $shutdown = Invoke-Api 'POST' '/api/shutdown'
  Check 'shutdown-accepted' ($shutdown.status -eq 200 -and $shutdown.json.shutdownRequested -eq $true) "status=$($shutdown.status)"

  [ordered]@{
    ok = $true
    mode = 'R2-safe-mock-only'
    hardwareWritesEnabled = $false
    hardwareWritesObserved = $false
    checks = $checks
  } | ConvertTo-Json -Depth 8
}
catch {
  [ordered]@{
    ok = $false
    mode = 'R2-safe-mock-only'
    hardwareWritesEnabled = $false
    hardwareWritesObserved = $false
    checks = $checks
    error = $_.Exception.Message
  } | ConvertTo-Json -Depth 8
  exit 1
}
finally {
  if ($null -ne $proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
  Start-Sleep -Milliseconds 300
  Remove-Item -LiteralPath $sessionPath -Force -ErrorAction SilentlyContinue
}
