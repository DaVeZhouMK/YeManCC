[CmdletBinding()]
param(
  [ValidateSet('handshake', 'write')]
  [string]$Mode = 'handshake',
  [string]$Authorization,
  [string]$Confirm
)

$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$nestedPackage = Join-Path $testRoot 'fan-host'
$package = if (Test-Path -LiteralPath (Join-Path $nestedPackage 'YeManFanHost.exe')) { $nestedPackage } else { $testRoot }
$exe = Join-Path $package 'YeManFanHost.exe'
$hc = Join-Path $package 'HandheldCompanion.dll'
$port = 8773
$base = "http://127.0.0.1:$port"
$sessionToken = $null
$headers = $null
$output = Join-Path $testRoot 'output\real-fan-test'
$process = $null
$safeToTerminate = $false
$launchMutex = $null
$launchMutexOwned = $false
$dotnetRuntimeUrl = 'https://dotnet.microsoft.com/download/dotnet/10.0/runtime'
$summary = [ordered]@{
  ok = $false
  mode = $Mode
  startedAtUtc = [DateTime]::UtcNow
  hardwareWritesEnabled = $false
  hardwareWritesObserved = $false
  httpRequests = @()
  diagnosticSnapshot = $null
  steps = @()
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
$sessionTokenPath = Join-Path $output 'session-token.txt'

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

function Ensure-WindowsDesktopRuntime {
  $runtimeLines = @()
  try {
    $runtimeLines = @(dotnet --list-runtimes 2>$null)
  } catch {
    $runtimeLines = @()
  }

  $hasDesktop10 = @($runtimeLines | Where-Object {
    $_ -match '^Microsoft\.WindowsDesktop\.App\s+10\.'
  }).Count -gt 0

  if (-not $hasDesktop10) {
    Write-Host '未检测到 Microsoft Windows Desktop Runtime 10 x64。'
    Write-Host "请安装：$dotnetRuntimeUrl"
    try { Start-Process $dotnetRuntimeUrl | Out-Null } catch {}
    throw "缺少 .NET Desktop Runtime 10；已打开安装地址：$dotnetRuntimeUrl"
  }
}

function Add-Step([string]$name, $value) {
  $summary.steps += [ordered]@{ name = $name; value = Get-SafeValue $value }
}

function Request([string]$method, [string]$path, $body = $null) {
  $clientRequestId = [guid]::NewGuid().ToString('N')
  $params = @{ Uri = "$base$path"; Method = $method; Headers = $headers; ErrorAction = 'Stop'; UseBasicParsing = $true; TimeoutSec = 15 }
  if ($null -ne $body) {
    $params.Body = ($body | ConvertTo-Json -Depth 10 -Compress)
    $params.ContentType = 'application/json'
  }
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

function Ensure-Admin {
  $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请以管理员身份运行 PowerShell；Fan Host payload ACL 预检需要管理员权限'
  }
}

function Ensure-NoConflict {
  # Armoury Crate is the ASUS OEM stack, not a third-party fan writer. HC
  # models its services separately; allow it to remain present and record it.
  $blocked = @('handheldcompanion', 'fancontrol', 'nbfc', 'rw', 'rweverything', 'ecview', 'ghelper')
  $found = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $name = $_.ProcessName.ToLowerInvariant()
    # Match complete process names. A substring check falsely classified
    # XboxGameBarWidgets as RWEverything because "barw" contains "rw".
    $blocked -contains $name
  } | Select-Object -ExpandProperty ProcessName -Unique)
  if ($found.Count -gt 0) { throw "close conflicting fan tools first: $($found -join ', ')" }
}

function Acquire-LaunchGuard {
  $created = $false
  $name = "Local\YeManFanHost-Phase2-$port"
  $script:launchMutex = New-Object System.Threading.Mutex($false, $name, [ref]$created)
  try { $script:launchMutexOwned = $script:launchMutex.WaitOne(0) } catch {
    try { $script:launchMutex.Dispose() } catch {}
    $script:launchMutex = $null
    throw "无法建立 Fan Host 单实例保护：$($_.Exception.Message)"
  }
  if (-not $script:launchMutexOwned) {
    try { $script:launchMutex.Dispose() } catch {}
    $script:launchMutex = $null
    throw "同一 Fan Host 测试正在运行或正在启动（端口 $port）；未覆盖现有会话令牌。"
  }
}

function Assert-NoExistingHost {
  $existingProcess = @(Get-Process -Name YeManFanHost -ErrorAction SilentlyContinue)
  $existingListener = @()
  try { $existingListener = @(Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue) } catch {}
  if ($existingProcess.Count -gt 0 -or $existingListener.Count -gt 0) {
    $pids = ($existingProcess | Select-Object -ExpandProperty Id) -join ', '
    throw "检测到已有 Fan Host 会话（pid=$pids, port=$port）；未覆盖现有会话令牌，请先完成 OEM 恢复/关闭。"
  }
}

function Get-OemStackSnapshot {
  $serviceNames = @('ArmouryCrateSEService', 'AsusAppService', 'ArmouryCrateControlInterface')
  $services = foreach ($serviceName in $serviceNames) {
    try {
      $service = Get-Service -Name $serviceName -ErrorAction Stop
      [ordered]@{ name = $serviceName; exists = $true; status = [string]$service.Status; startType = [string]$service.StartType }
    } catch {
      [ordered]@{ name = $serviceName; exists = $false; status = 'not-found'; startType = $null }
    }
  }
  $processes = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -match '^(ArmouryCrate|ArmouryCrateSE|ArmouryCrateControlInterface|AsusAppService)$'
  } | Select-Object ProcessName, Id)
  [ordered]@{ services = @($services); processes = $processes }
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
    (Join-Path $package 'yeman-fan-host-runtime.log'),
    (Join-Path $testRoot 'yeman-fan-host-runtime.log')
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
    (Join-Path $package 'fan-host-quarantine'),
    (Join-Path $testRoot 'fan-host-quarantine'),
    (Join-Path (Split-Path -Parent $package) 'fan-host-quarantine'),
    (Join-Path $stateRoot 'quarantine')
  ) | Select-Object -Unique
  foreach ($root in $quarantineRoots) {
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
    $items = @()
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

try {
  # Acquire the guard and inspect existing Host ownership before generating or
  # replacing the sidecar token. A rejected duplicate launch must never
  # invalidate a live session's authenticated recovery route.
  Acquire-LaunchGuard
  Assert-NoExistingHost
  $sessionToken = -join (1..32 | ForEach-Object { '{0:x2}' -f (Get-Random -Minimum 0 -Maximum 256) })
  $headers = @{ 'X-YeMan-Fan-Session' = $sessionToken }
  $sessionToken | Set-Content -LiteralPath $sessionTokenPath -Encoding ASCII -NoNewline

  Ensure-WindowsDesktopRuntime
  if (-not (Test-Path -LiteralPath $exe)) { throw "missing Host: $exe" }
  if (-not (Test-Path -LiteralPath $hc)) { throw "missing HC assembly: $hc" }

  # The production Host rejects an extracted, user-writable payload. The
  # structured test package keeps diagnostics outside the immutable host
  # directory, so it is safe to apply the same ACL gate before HC load.
  $installer = Join-Path $package 'install-fan-host-payload.ps1'
  if (Test-Path -LiteralPath $installer) {
    Ensure-Admin
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $installer -PayloadDirectory $package -SkipStateDirectory
    if ($LASTEXITCODE -ne 0) { throw "Fan Host payload ACL installation failed: exit=$LASTEXITCODE" }
  }

  Add-Step 'oem-stack-before' (Get-OemStackSnapshot)

  $args = @('--real-backend', '--hc-assembly', $hc, '--port', [string]$port, '--protocol-version', '2', '--session-token-file', $sessionTokenPath)
  if ($Mode -eq 'write') {
    Ensure-NoConflict
    if ([string]::IsNullOrWhiteSpace($Authorization)) { throw 'write mode requires -Authorization <approved authorization markdown>' }
    if ($Confirm -ne 'I-CONFIRM-YEMAN-REAL-FAN-TEST') { throw 'write mode requires -Confirm I-CONFIRM-YEMAN-REAL-FAN-TEST' }
    if (-not [IO.Path]::IsPathRooted($Authorization)) {
      $candidate = Join-Path $testRoot $Authorization
      if (Test-Path -LiteralPath $candidate) { $Authorization = [IO.Path]::GetFullPath($candidate) }
    }
    # The operator phrase is checked above.  HC write authorization itself
    # requires the per-process random session token, not the human phrase.
    # Passing the phrase here makes the Host reject every write with HTTP 403.
    $args += @('--allow-hardware-writes', '--authorization', $Authorization, '--confirm', $sessionToken)
  }

  $process = Start-Process -FilePath $exe -ArgumentList $args -WindowStyle Hidden -PassThru
  $ready = $false
  for ($i = 0; $i -lt 60; $i++) {
    try { $null = Request 'GET' '/health'; $ready = $true; break } catch { Start-Sleep -Milliseconds 250 }
  }
  if (-not $ready) { throw 'Host did not become ready' }

  $handshake = Request 'POST' '/api/handshake' @{}
  Add-Step 'handshake' $handshake
  if ($handshake.ok -ne $true) { throw "HC handshake failed: $($handshake.reason)" }
  if ($handshake.supported -ne $true -and $Mode -eq 'handshake') {
    $summary.ok = $true
    $summary.hardwareWritesEnabled = $handshake.hardwareWritesEnabled
    $summary.hardwareWritesObserved = $handshake.hardwareWritesObserved
    return
  }
  if ($handshake.supported -ne $true) { throw "device Gate did not pass: $($handshake.reason)" }
  if ($Mode -eq 'handshake') {
    $summary.ok = $true
    $summary.hardwareWritesEnabled = $handshake.hardwareWritesEnabled
    $summary.hardwareWritesObserved = $handshake.hardwareWritesObserved
    return
  }

  $open = Request 'POST' '/api/open' @{}
  Add-Step 'open' $open
  $events = Request 'POST' '/api/open-events' @{}
  Add-Step 'open-events' $events
  $lease = Request 'POST' '/api/acquire-control' @{}
  Add-Step 'acquire-control' $lease
  if ([string]::IsNullOrWhiteSpace($lease.lease.leaseId)) { throw 'Host did not return a lease' }

  $curve = @{
    leaseId = $lease.lease.leaseId
    nodes = @(
      @{ tempC = 0; dutyPercent = 0 },
      @{ tempC = 40; dutyPercent = 20 },
      @{ tempC = 60; dutyPercent = 45 },
      @{ tempC = 100; dutyPercent = 90 }
    )
  }
  $applied = Request 'POST' '/api/enable' $curve
  Add-Step 'balanced-curve' $applied
  Start-Sleep -Seconds 5
  $state = Request 'GET' '/api/state'
  Add-Step 'readback-state' $state
  $summary.hardwareWritesEnabled = $state.state.hardwareWritesEnabled
  $summary.hardwareWritesObserved = $state.state.hardwareWritesObserved

  $restore = Request 'POST' '/api/restore' @{ leaseId = $lease.lease.leaseId }
  Add-Step 'oem-restore' $restore
  $release = Request 'POST' '/api/release-control' @{ leaseId = $lease.lease.leaseId }
  Add-Step 'release-control' $release
  $close = Request 'POST' '/api/close' @{}
  Add-Step 'close' $close
  Add-Step 'oem-stack-after-close' (Get-OemStackSnapshot)
  if ($close.state.unknownState -eq $true -or $close.state.oemRestoreConfirmed -ne $true) {
    throw 'OEM restore/close was not confirmed'
  }
  $summary.ok = $true
}
catch {
  $summary.error = $_.Exception.Message
}
finally {
  if ($null -ne $process -and -not $process.HasExited) {
    # A failed test must never force-kill the only process that can still
    # restore OEM fan control. Query state first, then use the Host's own
    # restore/close path. Only a confirmed restore (or a session that never
    # opened and never wrote) may be terminated.
    $state = $null
    try {
      $stateResponse = Request 'GET' '/api/state'
      $state = $stateResponse.state
      if ($state.hardwareWritesObserved -eq $true) { $summary.hardwareWritesObserved = $true }
    } catch {}
    try {
      $restoreResponse = Request 'POST' '/api/restore' @{}
      $state = $restoreResponse.state
    } catch {}
    try {
      $closeResponse = Request 'POST' '/api/close' @{}
      $state = $closeResponse.state
    } catch {}
    if ($Mode -eq 'handshake' -and $summary.hardwareWritesObserved -ne $true) {
      # Handshake never opens HC or writes hardware.  Do not let any stale
      # physical-restore flag strand this read-only process; close and end it
      # unconditionally after the authenticated API boundary is attempted.
      try { Request 'POST' '/api/shutdown' @{} | Out-Null } catch {}
      try { if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } } catch {}
      $safeToTerminate = $true
    } elseif ($null -ne $state) {
      $safeToTerminate = ($state.unknownState -ne $true -and $state.oemRestoreConfirmed -eq $true)
      # Handshake mode is read-only by construction: it never calls Open,
      # OpenEvents, acquire-control or enable.  A read-only session has no
      # OEM ownership to restore, so it must not strand a resident Host merely
      # because the physical-restore flag is false.
      if ($Mode -eq 'handshake' -and
          $summary.hardwareWritesObserved -ne $true -and
          $state.unknownState -ne $true) {
        $safeToTerminate = $true
      }
    } elseif ($summary.hardwareWritesObserved -ne $true) {
      $safeToTerminate = $true
    }
  if ($safeToTerminate) {
      try { Request 'POST' '/api/shutdown' @{} | Out-Null } catch {}
      try { if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } } catch {}
    } else {
      Write-Warning 'OEM restore 未确认；保留 Fan Host 运行以等待恢复重试，未强制结束进程。'
    }
  }
  try { Capture-DiagnosticSnapshot } catch { $summary.diagnosticSnapshot = [ordered]@{ captured = $false; error = Protect-SensitiveText $_.Exception.Message } }
  $summary.finishedAtUtc = [DateTime]::UtcNow
  $summaryJson = Protect-SensitiveText ($summary | ConvertTo-Json -Depth 20)
  $summaryJson | Set-Content -LiteralPath (Join-Path $output 'session-summary.json') -Encoding UTF8
  $summaryJson
  if ($launchMutexOwned -and $null -ne $launchMutex) {
    try { $launchMutex.ReleaseMutex() } catch {}
    try { $launchMutex.Dispose() } catch {}
  }
}

if (-not $summary.ok) { exit 1 }
