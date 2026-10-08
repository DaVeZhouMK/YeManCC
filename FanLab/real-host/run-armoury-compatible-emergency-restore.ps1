[CmdletBinding()]
param(
  [int]$Port = 8773,
  [string]$SessionTokenPath
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SessionTokenPath)) {
  $SessionTokenPath = Join-Path -Path $PSScriptRoot -ChildPath 'output\real-fan-test\session-token.txt'
}
$base = "http://127.0.0.1:$Port"
if (-not (Test-Path -LiteralPath $SessionTokenPath -PathType Leaf)) {
  throw "未找到测试会话令牌：$SessionTokenPath；没有发送关闭或恢复请求。"
}
$token = (Get-Content -LiteralPath $SessionTokenPath -Raw).Trim()
if ($token -notmatch '^[0-9a-fA-F]{64}$') { throw '测试会话令牌格式无效。' }
$headers = @{ 'X-YeMan-Fan-Session' = $token }
$output = Join-Path $PSScriptRoot 'output\armoury-compatible-emergency-restore'
$summary = [ordered]@{
  ok = $false
  mode = 'emergency-restore'
  startedAtUtc = [DateTime]::UtcNow
  httpRequests = @()
  diagnosticSnapshot = $null
  steps = @()
}
New-Item -ItemType Directory -Force -Path $output | Out-Null

function Protect-SensitiveText([AllowEmptyString()][string]$Text) {
  if ($null -eq $Text) { return $null }
  $safe = $Text
  $safe = [regex]::Replace($safe, '(?i)("(?:X-YeMan-Fan-Session|authorization|confirm|sessionToken|session-token|leaseId|token|password|secret)"\s*:\s*)("[^"]*"|[^,}\s]+)', '$1"<redacted>"')
  $safe = [regex]::Replace($safe, '(?i)\b(X-YeMan-Fan-Session|authorization|confirm|sessionToken|session-token|leaseId|token|password|secret)\s*[:=]\s*[^\s,;}"'']+', '$1=<redacted>')
  $safe = [regex]::Replace($safe, '(?i)\b[0-9a-f]{64}\b', '<redacted-token>')
  return $safe
}

function Get-SafeValue($Value) {
  if ($null -eq $Value) { return $null }
  try {
    $json = $Value | ConvertTo-Json -Depth 20 -Compress
    $safeJson = Protect-SensitiveText $json
    return $safeJson | ConvertFrom-Json
  } catch {
    return Protect-SensitiveText ([string]$Value)
  }
}

function ConvertFrom-JsonSafe([string]$Raw) {
  if ([string]::IsNullOrWhiteSpace($Raw)) { return $null }
  try { return $Raw | ConvertFrom-Json } catch { return $null }
}

function Read-HttpBody($Response) {
  if ($null -eq $Response) { return '' }
  try {
    if ($null -ne $Response.Content) {
      $contentText = if ($Response.Content -is [System.Net.Http.HttpContent]) { $Response.Content.ReadAsStringAsync().GetAwaiter().GetResult() } else { [string]$Response.Content }
      if (-not [string]::IsNullOrWhiteSpace($contentText) -and $contentText -ne 'System.Byte[]') { return $contentText }
    }
  } catch {}
  try {
    if ($null -ne $Response.RawContentStream) {
      if ($Response.RawContentStream.CanSeek) { $Response.RawContentStream.Position = 0 }
      $reader = [IO.StreamReader]::new($Response.RawContentStream)
      try { $rawText = $reader.ReadToEnd(); if (-not [string]::IsNullOrWhiteSpace($rawText)) { return $rawText } } finally { $reader.Dispose() }
    }
  } catch {}
  try {
    $reader = [IO.StreamReader]::new($Response.GetResponseStream())
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
  } catch { return '' }
}

function Get-HttpHeader($Response, [string]$Name) {
  if ($null -eq $Response) { return $null }
  try {
    $value = $Response.Headers[$Name]
    if ($null -ne $value) { return [string]$value }
  } catch {}
  return $null
}

function Add-HttpRecord([string]$Method, [string]$Path, [string]$ClientRequestId, $RequestBody, [int]$StatusCode, $Response, [string]$FailureMessage = $null, [AllowEmptyString()][string]$RawBody = $null) {
  $raw = if ($PSBoundParameters.ContainsKey('RawBody')) { $RawBody } else { Read-HttpBody $Response }
  $parsed = ConvertFrom-JsonSafe $raw
  $hostRequestId = Get-HttpHeader $Response 'X-YeMan-Fan-Request-Id'
  $requestId = if ([string]::IsNullOrWhiteSpace($hostRequestId)) { $ClientRequestId } else { $hostRequestId }
  $apiError = if ($null -ne $parsed -and $null -ne $parsed.error) { $parsed.error } else { $null }
  $record = [ordered]@{
    capturedAtUtc = [DateTime]::UtcNow
    requestId = $requestId
    clientRequestId = $ClientRequestId
    hostRequestId = $hostRequestId
    method = $Method
    path = $Path
    statusCode = $StatusCode
    requestRawBody = if ($null -eq $RequestBody) { $null } else { Protect-SensitiveText (($RequestBody | ConvertTo-Json -Depth 20 -Compress)) }
    responseRawBody = Protect-SensitiveText $raw
    responseParsedBody = Get-SafeValue $parsed
    error = [ordered]@{
      code = if ($null -ne $apiError) { [string]$apiError.code } else { $null }
      message = if ($null -ne $apiError) { Protect-SensitiveText ([string]$apiError.message) } else { Protect-SensitiveText $FailureMessage }
      state = if ($null -ne $parsed) { Get-SafeValue $parsed.state } else { $null }
    }
  }
  $summary.httpRequests += $record
  return $record
}

function Add-Step([string]$Name, $Value) {
  $summary.steps += [ordered]@{ name = $Name; value = Get-SafeValue $Value; capturedAtUtc = [DateTime]::UtcNow }
}

function Invoke-Fan([string]$method, [string]$path) {
  $clientRequestId = [guid]::NewGuid().ToString('N')
  $body = if ($method -eq 'GET') { $null } else { @{} }
  $params = @{ Method = $method; Uri = "${base}${path}"; Headers = $headers; ErrorAction = 'Stop'; UseBasicParsing = $true; TimeoutSec = 15 }
  if ($null -ne $body) { $params.ContentType = 'application/json'; $params.Body = '{}' }
  try {
    $response = Invoke-WebRequest @params
    $raw = Read-HttpBody $response
    $record = Add-HttpRecord $method $path $clientRequestId $body ([int]$response.StatusCode) $response -RawBody $raw
    if ([int]$response.StatusCode -ge 400) {
      $failure = [InvalidOperationException]::new("Fan API $method $path failed: HTTP $($record.statusCode) requestId=$($record.requestId)")
      $failure.Data['YeManHttpRecord'] = $true
      throw $failure
    }
    return (ConvertFrom-JsonSafe $raw)
  } catch {
    if ($_.Exception.Data.Contains('YeManHttpRecord')) { throw }
    $response = $null
    try { $response = $_.Exception.Response } catch {}
    $statusCode = 0
    try { if ($null -ne $response) { $statusCode = [int]$response.StatusCode } } catch {}
    $record = Add-HttpRecord $method $path $clientRequestId $body $statusCode $response $_.Exception.Message
    $failure = [InvalidOperationException]::new("Fan API $method $path failed: HTTP $($record.statusCode) requestId=$($record.requestId) code=$($record.error.code) message=$($record.error.message)")
    $failure.Data['YeManHttpRecord'] = $true
    throw $failure
  }
}

function Capture-DiagnosticSnapshot {
  $stateRoot = Join-Path $env:LOCALAPPDATA 'YeManCC\fan-host'
  $snapshot = [ordered]@{
    capturedAtUtc = [DateTime]::UtcNow
    runtimeLogs = @()
    quarantine = @()
  }
  $logCandidates = @(
    (Join-Path $stateRoot 'logs\yeman-fan-host-runtime.log'),
    (Join-Path $PSScriptRoot 'yeman-fan-host-runtime.log')
  ) | Select-Object -Unique
  $logIndex = 0
  foreach ($logPath in $logCandidates) {
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) { continue }
    $safeName = if ($logIndex -eq 0) { 'host-runtime.log' } else { "host-runtime-$logIndex.log" }
    $copyPath = Join-Path $output $safeName
    try {
      $safeLog = Protect-SensitiveText (Get-Content -LiteralPath $logPath -Raw -ErrorAction Stop)
      Set-Content -LiteralPath $copyPath -Value $safeLog -Encoding UTF8
      $snapshot.runtimeLogs += [ordered]@{
        name = $safeName
        sourceName = [IO.Path]::GetFileName($logPath)
        captured = $true
        size = (Get-Item -LiteralPath $copyPath).Length
        sha256 = (Get-FileHash -LiteralPath $copyPath -Algorithm SHA256).Hash.ToLowerInvariant()
      }
    } catch {
      $snapshot.runtimeLogs += [ordered]@{ name = $safeName; sourceName = [IO.Path]::GetFileName($logPath); captured = $false; error = Protect-SensitiveText $_.Exception.Message }
    }
    $logIndex++
  }
  $quarantineRoots = @(
    (Join-Path $PSScriptRoot 'fan-host-quarantine'),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'fan-host-quarantine'),
    (Join-Path $stateRoot 'quarantine')
  ) | Select-Object -Unique
  foreach ($root in $quarantineRoots) {
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
    try {
      $rootFull = [IO.Path]::GetFullPath($root)
      $items = @(Get-ChildItem -LiteralPath $rootFull -File -Recurse -ErrorAction Stop | ForEach-Object {
        $relative = $_.FullName.Substring($rootFull.Length).TrimStart([char[]]"\\/")
        [ordered]@{ name = Protect-SensitiveText $relative; size = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
      })
      $snapshot.quarantine += [ordered]@{ rootName = [IO.Path]::GetFileName($rootFull); exists = $true; files = $items }
    } catch {
      $snapshot.quarantine += [ordered]@{ rootName = [IO.Path]::GetFileName($root); exists = $true; files = @(); error = Protect-SensitiveText $_.Exception.Message }
    }
  }
  $summary.diagnosticSnapshot = $snapshot
  $snapshot | ConvertTo-Json -Depth 20 | Protect-SensitiveText | Set-Content -LiteralPath (Join-Path $output 'diagnostic-snapshot.json') -Encoding UTF8
}

$exitCode = 4
try {
  $state = (Invoke-Fan 'GET' '/health').state
  Add-Step 'health' $state
  Write-Host ("当前状态：state={0}, writes={1}, callback={2}, confirmed={3}, physical={4}, unknown={5}" -f `
    $state.state, $state.hardwareWritesObserved, $state.hcRestoreCallbackReturned, $state.oemRestoreConfirmed, $state.oemPhysicalOwnershipConfirmed, $state.unknownState)

  if ($state.hardwareWritesObserved -ne $true -and $state.unknownState -ne $true) {
    Write-Host '当前会话未发生硬件写入；执行只读会话安全关闭。'
    $close = Invoke-Fan 'POST' '/api/close'; Add-Step 'close-read-only' $close
    $shutdown = Invoke-Fan 'POST' '/api/shutdown'; Add-Step 'shutdown-read-only' $shutdown
    $summary.ok = $true
    $exitCode = 0
  } else {
    $close = $null
    try { $close = Invoke-Fan 'POST' '/api/close'; Add-Step 'close' $close } catch { Write-Warning (Protect-SensitiveText $_.Exception.Message) }
    $state = if ($null -ne $close) { $close.state } else { $null }
    if ($null -eq $state -or $state.unknownState -eq $true -or $state.oemRestoreConfirmed -ne $true) {
      Write-Host '恢复未确认，尝试显式 restore；不强制结束 Host。'
      try { $restore = Invoke-Fan 'POST' '/api/restore'; Add-Step 'restore' $restore; $state = $restore.state } catch { Write-Warning (Protect-SensitiveText $_.Exception.Message) }
      if ($null -ne $state -and $state.unknownState -ne $true -and $state.oemRestoreConfirmed -eq $true) {
        try { $close = Invoke-Fan 'POST' '/api/close'; Add-Step 'close-after-restore' $close; $state = $close.state } catch { Write-Warning (Protect-SensitiveText $_.Exception.Message) }
      }
    }

    if ($null -eq $state -or $state.unknownState -eq $true -or $state.oemRestoreConfirmed -ne $true) {
      Write-Error 'OEM 恢复未确认：保留 Host 等待重试，禁止 Stop-Process。'
      $summary.error = 'OEM 恢复未确认；保留 Host 等待重试。'
      $exitCode = 3
    } else {
      Write-Host 'OEM 恢复已确认；现在才允许请求 Host shutdown。'
      try { $shutdown = Invoke-Fan 'POST' '/api/shutdown'; Add-Step 'shutdown' $shutdown; $summary.ok = $true; $exitCode = 0 } catch { Write-Warning (Protect-SensitiveText $_.Exception.Message); $summary.error = 'OEM 已恢复但 shutdown 请求失败。'; $exitCode = 4 }
    }
  }
} catch {
  $summary.error = Protect-SensitiveText $_.Exception.Message
  if ($exitCode -eq 0) { $exitCode = 4 }
}
try { Capture-DiagnosticSnapshot } catch { $summary.diagnosticSnapshot = [ordered]@{ captured = $false; error = Protect-SensitiveText $_.Exception.Message } }
$summary.exitCode = $exitCode
$summary.finishedAtUtc = [DateTime]::UtcNow
$summaryJson = Protect-SensitiveText ($summary | ConvertTo-Json -Depth 20)
$summaryJson | Set-Content -LiteralPath (Join-Path $output 'session-summary.json') -Encoding UTF8
$summaryJson
exit $exitCode
