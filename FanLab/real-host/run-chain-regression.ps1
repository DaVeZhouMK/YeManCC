$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
$exe = Join-Path $PSScriptRoot 'bin\Release\net10.0-windows10.0.19041.0\win-x64\YeManFanHost.exe'
$selfTest = Join-Path $PSScriptRoot 'bin\Release\net10.0-windows10.0.19041.0\win-x64\YeManFanHost.dll'
$port = 8770
$base = "http://127.0.0.1:$port"
$result = [ordered]@{
  ok = $false
  executable = $exe
  checks = @()
  hardwareCapable = $null
  hardwareWritesEnabled = $null
  hardwareWritesObserved = $null
  residualHostProcess = $null
}
$proc = $null

function Add-Check([string]$name, [bool]$passed, [int]$status, [string]$detail) {
  $script:result.checks += [ordered]@{ name = $name; passed = $passed; status = $status; detail = $detail }
  if (-not $passed) { throw "check failed: $name - $detail" }
}

function Invoke-Json([string]$method, [string]$path, [string]$body = '{}', [bool]$authenticated = $true) {
  $client = [System.Net.Http.HttpClient]::new()
  try {
    $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::$method, "$base$path")
    if ($authenticated) { $request.Headers.Add('X-YeMan-Fan-Session', $session) }
    if ($method -ne 'GET') { $request.Content = [System.Net.Http.StringContent]::new($body, [Text.Encoding]::UTF8, 'application/json') }
    $response = $client.SendAsync($request).Result
    $raw = $response.Content.ReadAsStringAsync().Result
    return [pscustomobject]@{ status = [int]$response.StatusCode; json = ($raw | ConvertFrom-Json) }
  }
  finally { $client.Dispose() }
}

$session = -join (1..32 | ForEach-Object { '{0:x2}' -f (Get-Random -Minimum 0 -Maximum 256) })
$headers = @{ 'X-YeMan-Fan-Session' = $session }
$sessionPath = Join-Path ([System.IO.Path]::GetTempPath()) ("YeManFanHost-chain-{0}.session" -f $PID)
$session | Set-Content -LiteralPath $sessionPath -Encoding ASCII -NoNewline

try {
  if (-not (Test-Path -LiteralPath $exe)) { throw "current Host binary not built: $exe" }
  $proc = Start-Process -FilePath $exe -ArgumentList @('--port', [string]$port, '--protocol-version', '2', '--session-token-file', $sessionPath) -PassThru -WindowStyle Hidden
  $ready = $false
  for ($i = 0; $i -lt 30; $i++) {
    try { $null = Invoke-RestMethod "$base/health" -Headers $headers -TimeoutSec 1; $ready = $true; break } catch { Start-Sleep -Milliseconds 100 }
  }
  if (-not $ready) { throw 'host did not become ready' }

  $health = Invoke-Json 'GET' '/health'
  Add-Check 'health' ($health.status -eq 200 -and $health.json.ok -eq $true) $health.status 'loopback health responds'

  $unauthenticatedClose = Invoke-Json 'POST' '/api/close' '{}' $false
  Add-Check 'session-required-before-close' ($unauthenticatedClose.status -eq 401 -and $unauthenticatedClose.json.error.code -eq 'API_SESSION_REQUIRED') $unauthenticatedClose.status 'Host rejects an unauthenticated close request'

  $handshake = Invoke-Json 'POST' '/api/handshake'
  $hs = $handshake.json
  $safe = ($handshake.status -eq 200 -and $hs.ok -eq $true -and $hs.supported -eq $false -and $hs.hardwareCapable -eq $false -and $hs.hardwareWritesEnabled -eq $false -and $hs.hardwareWritesObserved -eq $false)
  Add-Check 'handshake-safe-gate' $safe $handshake.status 'supported=false and all hardware flags false'
  $result.hardwareCapable = $hs.hardwareCapable
  $result.hardwareWritesEnabled = $hs.hardwareWritesEnabled
  $result.hardwareWritesObserved = $hs.hardwareWritesObserved

  $state = Invoke-Json 'GET' '/api/state'
  Add-Check 'state-read' ($state.status -eq 200 -and $state.json.ok -eq $true) $state.status 'state endpoint responds'
  Add-Check 'open' ((Invoke-Json 'POST' '/api/open').status -eq 200) 200 'open is side-effect-free in safe build'
  Add-Check 'open-events' ((Invoke-Json 'POST' '/api/open-events').status -eq 200) 200 'open-events is side-effect-free in safe build'

  $acquire = Invoke-Json 'POST' '/api/acquire-control'
  Add-Check 'unsupported-acquire-block' ($acquire.status -eq 409 -and $acquire.json.error.code -eq 'FAN_UNSUPPORTED') $acquire.status 'unsupported hardware cannot acquire lease'
  $heartbeat = Invoke-Json 'POST' '/api/heartbeat' '{}'
  Add-Check 'missing-lease-block' ($heartbeat.status -eq 400 -and $heartbeat.json.error.code -eq 'INVALID_REQUEST') $heartbeat.status 'missing lease is rejected'
  $enable = Invoke-Json 'POST' '/api/enable' '{}'
  Add-Check 'missing-enable-lease-block' ($enable.status -eq 409 -and $enable.json.error.code -eq 'LEASE_REQUIRED') $enable.status 'curve writes require a lease before hardware gate'

  Add-Check 'restore' ((Invoke-Json 'POST' '/api/restore' '{}').status -eq 200) 200 'restore endpoint returns safe state'

  # Tagged power contract (2026-10-01): /api/suspend and /api/resume require a
  # body carrying a positive generation and a non-empty source.
  $suspend = Invoke-Json 'POST' '/api/suspend' '{"generation":1,"source":"selftest.native"}'
  Add-Check 'suspend' ($suspend.status -eq 200) $suspend.status 'suspend endpoint returns safe state'
  $resume = Invoke-Json 'POST' '/api/resume' '{"generation":2,"source":"selftest.native"}'
  Add-Check 'resume' ($resume.status -eq 200) $resume.status 'resume endpoint returns safe state'
  $suspendAfterResume = Invoke-Json 'POST' '/api/suspend' '{"generation":3,"source":"selftest.native"}'
  Add-Check 'suspend-after-resume' ($suspendAfterResume.status -eq 200 -and $suspendAfterResume.json.state.state -eq 'Suspended') $suspendAfterResume.status 'second suspend returns a confirmed suspended state'
  $close = Invoke-Json 'POST' '/api/close' '{}'
  Add-Check 'close-from-suspended' ($close.status -eq 200 -and $close.json.state.state -eq 'Stopped') $close.status 'close from suspended leaves stopped state'

  $result.ok = $true
}
catch {
  $result.error = $_.Exception.Message
}
finally {
  if ($null -ne $proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
  Start-Sleep -Milliseconds 300
  $result.residualHostProcess = @(Get-Process -Name YeManFanHost -ErrorAction SilentlyContinue).Count -gt 0
  Remove-Item -LiteralPath $sessionPath -Force -ErrorAction SilentlyContinue
  if ($result.residualHostProcess) { $result.ok = $false }
  $result | ConvertTo-Json -Depth 8
}

if (-not $result.ok) { exit 1 }
