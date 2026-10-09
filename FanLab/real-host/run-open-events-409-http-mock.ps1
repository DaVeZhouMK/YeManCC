[CmdletBinding()]
param(
  [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
$scriptRoot = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) { (Get-Location).Path } else { $PSScriptRoot }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $scriptRoot 'output\http-409-mock' }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$responseBody = (@{
  ok = $false
  error = @{ code = 'HC_MANAGER_FACTORY_LIFECYCLE_UNKNOWN'; message = 'HC ManagerFactory cleanup boundary unconfirmed' }
  state = @{
    state = 'FaultLocked'; openCalled = $true; openEventsCalled = $false
    hardwareWritesEnabled = $false; hardwareWritesObserved = $false
    hcManagerFactoryLifecycle = 'Unknown'; hcCloseCleanupPending = $true; unknownState = $true
  }
} | ConvertTo-Json -Depth 8 -Compress)

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
$task = $listener.AcceptTcpClientAsync()
$requestId = [guid]::NewGuid().ToString('N')
$client = [Net.Http.HttpClient]::new()
$record = $null
try {
  $request = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::Post, "http://127.0.0.1:$port/api/open-events")
  $request.Headers.Add('X-YeMan-Fan-Request-Id', $requestId)
  $request.Content = [Net.Http.StringContent]::new('{}', [Text.Encoding]::UTF8, 'application/json')
  $requestTask = $client.SendAsync($request)
  $accepted = $task.GetAwaiter().GetResult()
  $stream = $accepted.GetStream()
  $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 4096, $true)
  try { $null = $reader.ReadLine(); while (($line = $reader.ReadLine()) -ne '') { } } finally { $reader.Dispose() }
  $bodyBytes = [Text.Encoding]::UTF8.GetBytes($responseBody)
  $head = "HTTP/1.1 409 Conflict`r`nContent-Type: application/json`r`nX-YeMan-Fan-Request-Id: $requestId`r`nContent-Length: $($bodyBytes.Length)`r`nConnection: close`r`n`r`n"
  $headBytes = [Text.Encoding]::ASCII.GetBytes($head)
  $stream.Write($headBytes, 0, $headBytes.Length)
  $stream.Write($bodyBytes, 0, $bodyBytes.Length)
  $stream.Flush(); $accepted.Dispose()
  try { $null = $requestTask.GetAwaiter().GetResult() } catch { }
  # Repeat the same evidence fields required by the real wrappers. Tokens are
  # not present in this fixture; raw and parsed body are both retained.
  $parsed = $responseBody | ConvertFrom-Json
  $record = [ordered]@{
    capturedAtUtc = [DateTime]::UtcNow
    method = 'POST'; path = '/api/open-events'; statusCode = 409
    requestId = $requestId; hostRequestId = $requestId
    responseRawBody = $responseBody
    responseParsedBody = $parsed
    error = [ordered]@{ code = $parsed.error.code; message = $parsed.error.message; state = $parsed.state }
    hardwareWritesEnabled = $false; hardwareWritesObserved = $false
  }
  $summary = [ordered]@{ ok = $true; mode = 'http-open-events-409-mock'; hardwareWritesEnabled = $false; hardwareWritesObserved = $false; httpRequests = @($record) }
  $summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'session-summary.json') -Encoding UTF8
  $summary | ConvertTo-Json -Depth 12
}
finally {
  try { $client.Dispose() } catch { }
  try { $listener.Stop() } catch { }
}
