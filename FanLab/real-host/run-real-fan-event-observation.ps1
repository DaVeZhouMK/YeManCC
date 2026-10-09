[CmdletBinding()]
param(
  [ValidateSet('handshake', 'write-observe')]
  [string]$Mode = 'handshake',
  [ValidateRange(30, 600)]
  [int]$ObserveSeconds = 180,
  [ValidateRange(1024, 65535)]
  [int]$Port = 8773,
  [string]$Authorization,
  [string]$Confirm
)

$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$nestedPackage = Join-Path $testRoot 'fan-host'
$package = if (Test-Path -LiteralPath (Join-Path $nestedPackage 'YeManFanHost.exe')) { $nestedPackage } else { $testRoot }
$exe = Join-Path $package 'YeManFanHost.exe'
$hc = Join-Path $package 'HandheldCompanion.dll'
$base = "http://127.0.0.1:$Port"
$output = Join-Path $testRoot 'output\real-fan-event-observation'
$process = $null
$safeToTerminate = $false
$launchMutex = $null
$launchMutexOwned = $false
$dotnetRuntimeUrl = 'https://dotnet.microsoft.com/download/dotnet/10.0/runtime'
$summary = [ordered]@{
  ok = $false
  mode = $Mode
  observeSeconds = $ObserveSeconds
  startedAtUtc = [DateTime]::UtcNow
  hardwareWritesEnabled = $false
  hardwareWritesObserved = $false
  httpRequests = @()
  diagnosticSnapshot = $null
  steps = @()
}

New-Item -ItemType Directory -Force -Path $output | Out-Null
$sessionTokenPath = Join-Path $output 'session-token.txt'
$observationPath = Join-Path $output 'event-observation.ndjson'

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

function Acquire-LaunchGuard {
  $created = $false
  $name = "Local\YeManFanHost-Phase2-$Port"
  $script:launchMutex = New-Object System.Threading.Mutex($false, $name, [ref]$created)
  try { $script:launchMutexOwned = $script:launchMutex.WaitOne(0) } catch {
    try { $script:launchMutex.Dispose() } catch {}
    $script:launchMutex = $null
    throw "无法建立 Fan Host 单实例保护：$($_.Exception.Message)"
  }
  if (-not $script:launchMutexOwned) {
    try { $script:launchMutex.Dispose() } catch {}
    $script:launchMutex = $null
    throw "同一 Fan Host 测试正在运行或正在启动（端口 $Port）；未覆盖现有会话令牌。"
  }
}

function Assert-NoExistingHost {
  $existingProcess = @(Get-Process -Name YeManFanHost -ErrorAction SilentlyContinue)
  $existingListener = @()
  try { $existingListener = @(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) } catch {}
  if ($existingProcess.Count -gt 0 -or $existingListener.Count -gt 0) {
    $pids = ($existingProcess | Select-Object -ExpandProperty Id) -join ', '
    throw "检测到已有 Fan Host 会话（pid=$pids, port=$Port）；未覆盖现有会话令牌，请先完成 OEM 恢复/关闭。"
  }
}

function Ensure-WindowsDesktopRuntime {
  $runtimeLines = @()
  try { $runtimeLines = @(dotnet --list-runtimes 2>$null) } catch { $runtimeLines = @() }
  $hasDesktop10 = @($runtimeLines | Where-Object { $_ -match '^Microsoft\.WindowsDesktop\.App\s+10\.' }).Count -gt 0
  if (-not $hasDesktop10) {
    Write-Host '未检测到 Microsoft Windows Desktop Runtime 10 x64。'
    Write-Host "请安装：$dotnetRuntimeUrl"
    try { Start-Process $dotnetRuntimeUrl | Out-Null } catch {}
    throw "缺少 .NET Desktop Runtime 10；已打开安装地址：$dotnetRuntimeUrl"
  }
}

function Ensure-Admin {
  $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请以管理员身份运行 PowerShell；Fan Host payload ACL 预检需要管理员权限'
  }
}

function Ensure-NoConflict {
  $blocked = @('handheldcompanion', 'fancontrol', 'nbfc', 'rw', 'rweverything', 'ecview', 'ghelper')
  $found = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $blocked -contains $_.ProcessName.ToLowerInvariant()
  } | Select-Object -ExpandProperty ProcessName -Unique)
  if ($found.Count -gt 0) { throw "请先关闭冲突的第三方风扇工具：$($found -join ', ')" }
}

function Add-Step([string]$name, $value) {
  $summary.steps += [ordered]@{ name = $name; value = Get-SafeValue $value }
}

function Request([string]$method, [string]$path, $body = $null) {
  $clientRequestId = [guid]::NewGuid().ToString('N')
  $params = @{ Uri = "$base$path"; Method = $method; Headers = $headers; ErrorAction = 'Stop'; UseBasicParsing = $true; TimeoutSec = 15 }
  if ($null -ne $body) {
    $params.Body = ($body | ConvertTo-Json -Depth 12 -Compress)
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

function Write-Observation($state, [string]$eventName = 'poll') {
  $record = [ordered]@{
    capturedAtUtc = [DateTime]::UtcNow
    event = $eventName
    state = Get-SafeValue $state.state
  }
  (Protect-SensitiveText ($record | ConvertTo-Json -Depth 20 -Compress)) | Add-Content -LiteralPath $observationPath -Encoding UTF8
  $s = $state.state
  Write-Host ("{0} event={1} state={2} power={3} generation={4} writes={5} restore={6}" -f `
    $record.capturedAtUtc.ToString('o'), $eventName, $s.state, $s.powerState, $s.powerGeneration, `
    $s.hardwareWritesObserved, $s.oemRestoreConfirmed)
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
  Acquire-LaunchGuard
  Ensure-WindowsDesktopRuntime
  if (-not (Test-Path -LiteralPath $exe)) { throw "找不到 Fan Host：$exe" }
  if (-not (Test-Path -LiteralPath $hc)) { throw "找不到 HC 程序集：$hc" }

  # Check for an existing Host before creating or replacing the sidecar token.
  # A rejected duplicate launch must never invalidate a live session's token.
  Assert-NoExistingHost
  $sessionToken = -join (1..32 | ForEach-Object { '{0:x2}' -f (Get-Random -Minimum 0 -Maximum 256) })
  $headers = @{ 'X-YeMan-Fan-Session' = $sessionToken }
  $sessionToken | Set-Content -LiteralPath $sessionTokenPath -Encoding ASCII -NoNewline

  $installer = Join-Path $package 'install-fan-host-payload.ps1'
  if (Test-Path -LiteralPath $installer) {
    Ensure-Admin
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $installer -PayloadDirectory $package -SkipStateDirectory
    if ($LASTEXITCODE -ne 0) { throw "Fan Host payload ACL 安装失败：exit=$LASTEXITCODE" }
  }

  if ($Mode -eq 'write-observe') {
    Ensure-NoConflict
    if ([string]::IsNullOrWhiteSpace($Authorization)) { throw 'write-observe 需要 -Authorization 授权 MD' }
    if ($Confirm -ne 'I-CONFIRM-YEMAN-REAL-FAN-TEST') { throw 'write-observe 需要 -Confirm I-CONFIRM-YEMAN-REAL-FAN-TEST' }
  }

  $args = @('--real-backend', '--hc-assembly', $hc, '--port', [string]$Port, '--protocol-version', '2', '--session-token-file', $sessionTokenPath)
  if ($Mode -eq 'write-observe') {
    if (-not [IO.Path]::IsPathRooted($Authorization)) {
      $candidate = Join-Path $testRoot $Authorization
      if (Test-Path -LiteralPath $candidate) { $Authorization = [IO.Path]::GetFullPath($candidate) }
    }
    $args += @('--allow-hardware-writes', '--authorization', $Authorization, '--confirm', $sessionToken)
  }

  $process = Start-Process -FilePath $exe -ArgumentList $args -WindowStyle Hidden -PassThru
  $ready = $false
  for ($i = 0; $i -lt 60; $i++) {
    try { $null = Request 'GET' '/health'; $ready = $true; break } catch { Start-Sleep -Milliseconds 250 }
  }
  if (-not $ready) { throw 'Fan Host 未在限定时间内就绪' }

  $handshake = Request 'POST' '/api/handshake' @{}
  Add-Step 'handshake' $handshake
  if ($handshake.ok -ne $true) { throw "HC 握手失败：$($handshake.reason)" }
  if ($Mode -eq 'handshake') {
    $summary.ok = $true
    $summary.hardwareWritesEnabled = $handshake.hardwareWritesEnabled
    $summary.hardwareWritesObserved = $handshake.hardwareWritesObserved
    return
  }
  if ($handshake.supported -ne $true) { throw "设备 Gate 未通过：$($handshake.reason)" }

  $open = Request 'POST' '/api/open' @{}
  Add-Step 'open' $open
  $events = Request 'POST' '/api/open-events' @{}
  Add-Step 'open-events' $events
  $lease = Request 'POST' '/api/acquire-control' @{}
  Add-Step 'acquire-control' $lease
  if ([string]::IsNullOrWhiteSpace($lease.lease.leaseId)) { throw 'Fan Host 未返回 lease' }

  # All four nodes at 100% makes the short observation audible. This is a
  # test-only curve and is still subject to HC route validation and lease gate.
  $curve = @{
    leaseId = $lease.lease.leaseId
    nodes = @(
      @{ tempC = 0; dutyPercent = 100 },
      @{ tempC = 40; dutyPercent = 100 },
      @{ tempC = 60; dutyPercent = 100 },
      @{ tempC = 100; dutyPercent = 100 }
    )
  }
  $applied = Request 'POST' '/api/enable' $curve
  Add-Step 'full-duty-test-curve' $applied
  $summary.hardwareWritesEnabled = $true
  Write-Observation (Request 'GET' '/api/state') 'write-enabled'

  # Count active observation samples instead of comparing wall-clock time.
  # Windows suspends this PowerShell process during S3/S4/S0ix; a wall-clock
  # deadline would otherwise consume the entire post-wake observation window
  # while the process was asleep.
  $sampleCount = [int][Math]::Ceiling($ObserveSeconds / 2.0)
  Write-Host "观察已开始：请在此期间依次执行 AC/DC 切换和安全的睡眠/唤醒；有效观察窗口 ${ObserveSeconds}s。"
  for ($sample = 0; $sample -lt $sampleCount; $sample++) {
    try { Write-Observation (Request 'GET' '/api/state') 'poll' }
    catch { Write-Host "状态读取暂时失败：$($_.Exception.Message)" }
    if ($sample + 1 -lt $sampleCount) { Start-Sleep -Seconds 2 }
  }

  $restore = Request 'POST' '/api/restore' @{ leaseId = $lease.lease.leaseId }
  Add-Step 'oem-restore' $restore
  $release = Request 'POST' '/api/release-control' @{ leaseId = $lease.lease.leaseId }
  Add-Step 'release-control' $release
  $close = Request 'POST' '/api/close' @{}
  Add-Step 'close' $close
  Write-Observation $close 'close-result'
  if ($close.state.unknownState -eq $true -or $close.state.oemRestoreConfirmed -ne $true) {
    throw 'OEM 恢复/关闭未确认；保留 Host 等待重试'
  }
  $summary.hardwareWritesObserved = $close.state.hardwareWritesObserved
  $summary.ok = $true
}
catch {
  $summary.error = $_.Exception.Message
}
finally {
  if ($null -ne $process -and -not $process.HasExited) {
    $state = $null
    try { $state = (Request 'GET' '/api/state').state } catch {}
    try { $state = (Request 'POST' '/api/restore' @{}).state } catch {}
    try { $state = (Request 'POST' '/api/close' @{}).state } catch {}
    if ($null -ne $state) {
      $safeToTerminate = ($state.unknownState -ne $true -and $state.oemRestoreConfirmed -eq $true -and
        $state.hardwareWritesEnabled -ne $true -and $state.openCalled -ne $true)
      # A read-only handshake never opens HC or writes hardware. It does not
      # require physical OEM confirmation, so it may close its own safe idle
      # Host instead of leaving a resident process for the next test.
      $readOnlyHandshake = ($Mode -eq 'handshake' -and
        $state.unknownState -ne $true -and $state.hardwareWritesObserved -ne $true -and
        $state.hardwareWritesEnabled -ne $true -and $state.openCalled -ne $true)
      if ($readOnlyHandshake) { $safeToTerminate = $true }
    }
  if ($safeToTerminate) {
      try { Request 'POST' '/api/shutdown' @{} | Out-Null } catch {}
      try { if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } } catch {}
    } else {
      Write-Warning 'OEM 恢复未确认；保留 Fan Host，禁止强制结束进程。'
    }
  }
  try { Capture-DiagnosticSnapshot } catch { $summary.diagnosticSnapshot = [ordered]@{ captured = $false; error = Protect-SensitiveText $_.Exception.Message } }
  $summary.finishedAtUtc = [DateTime]::UtcNow
  $summaryJson = Protect-SensitiveText ($summary | ConvertTo-Json -Depth 20)
  $summaryJson | Set-Content -LiteralPath (Join-Path $output 'session-summary.json') -Encoding UTF8
  $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
  $evidenceZip = Join-Path $testRoot "event-observation-evidence-$stamp.zip"
  try {
    # The session token is an authenticated recovery capability. Keep it on
    # disk for emergency restore, but never place it in an evidence archive.
    $archiveFiles = @(Get-ChildItem -LiteralPath $output -File | Where-Object { $_.Name -ne 'session-token.txt' } | Select-Object -ExpandProperty FullName)
    if ($archiveFiles.Count -gt 0) { Compress-Archive -Path $archiveFiles -DestinationPath $evidenceZip -Force }
    $summary.evidenceZip = $evidenceZip
    $summary.evidenceZipSha256 = (Get-FileHash -LiteralPath $evidenceZip -Algorithm SHA256).Hash
  } catch { Write-Warning "证据压缩失败：$($_.Exception.Message)" }
  $summaryJson = Protect-SensitiveText ($summary | ConvertTo-Json -Depth 20)
  $summaryJson | Set-Content -LiteralPath (Join-Path $output 'session-summary.json') -Encoding UTF8
  $summaryJson
  if ($launchMutexOwned -and $null -ne $launchMutex) {
    try { $launchMutex.ReleaseMutex() } catch {}
    try { $launchMutex.Dispose() } catch {}
  }
}

if (-not $summary.ok) { exit 1 }
