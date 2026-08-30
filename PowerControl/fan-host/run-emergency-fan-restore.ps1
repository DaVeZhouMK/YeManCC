[CmdletBinding()]
param(
  [int]$Port = 8765
)

$ErrorActionPreference = 'Stop'
$base = "http://127.0.0.1:$Port"
$sessionPath = Join-Path $PSScriptRoot 'YeManFanHost.session'
if (-not (Test-Path -LiteralPath $sessionPath -PathType Leaf)) {
  Write-Error "未找到 Fan Host 会话令牌：$sessionPath。没有发送任何关闭或硬件请求。"
  exit 2
}
$sessionToken = (Get-Content -LiteralPath $sessionPath -Raw).Trim()
if ($sessionToken -notmatch '^[0-9a-fA-F]{64}$') {
  Write-Error 'Fan Host 会话令牌格式无效。没有发送任何关闭或硬件请求。'
  exit 2
}

function Invoke-FanApi([string]$Method, [string]$Path) {
  $uri = "$base$Path"
  try {
    $request = @{
      UseBasicParsing = $true
      Method = $Method
      Uri = $uri
      TimeoutSec = 10
      Headers = @{ 'X-YeMan-Fan-Session' = $sessionToken }
    }
    if ($Method -ne 'GET') { $request.ContentType = 'application/json'; $request.Body = '{}' }
    $response = Invoke-WebRequest @request
    return [pscustomobject]@{
      StatusCode = [int]$response.StatusCode
      Body = if ([string]::IsNullOrWhiteSpace($response.Content)) { $null } else { $response.Content | ConvertFrom-Json }
    }
  } catch {
    $http = $_.Exception.Response
    if ($null -eq $http) { throw }
    $reader = New-Object System.IO.StreamReader($http.GetResponseStream())
    try { $raw = $reader.ReadToEnd() } finally { $reader.Dispose() }
    return [pscustomobject]@{
      StatusCode = [int]$http.StatusCode
      Body = if ([string]::IsNullOrWhiteSpace($raw)) { $null } else { $raw | ConvertFrom-Json }
    }
  }
}

function Test-RestoreConfirmed($state) {
  return $null -ne $state -and $state.oemRestoreConfirmed -eq $true -and $state.unknownState -ne $true
}

Write-Host "检查本机 YeMan Fan Host：$base"
$health = Invoke-FanApi 'GET' '/health'
if ($health.StatusCode -ne 200 -or $null -eq $health.Body -or $health.Body.host -ne 'YeManFanHost') {
  Write-Error "未找到可识别的 YeManFanHost。没有执行强制结束或杀进程；请保留现场并检查 Host 日志。"
  exit 2
}

$before = $health.Body.state
Write-Host ("当前状态：state={0}, hardwareWritesObserved={1}, hcRestoreCallbackReturned={2}, oemRestoreConfirmed={3}, oemPhysicalOwnershipConfirmed={4}, unknownState={5}" -f `
  $before.state, $before.hardwareWritesObserved, $before.hcRestoreCallbackReturned, $before.oemRestoreConfirmed, $before.oemPhysicalOwnershipConfirmed, $before.unknownState)

Write-Host '发送 OEM 恢复请求（未确认恢复前不会发送 shutdown）...'
$close = Invoke-FanApi 'POST' '/api/close'
$after = if ($null -ne $close.Body) { $close.Body.state } else { $null }
if ($close.StatusCode -ne 200 -or -not (Test-RestoreConfirmed $after)) {
  Write-Host 'close 未确认恢复，尝试显式 restore 后再次 close...'
  $restore = Invoke-FanApi 'POST' '/api/restore'
  $after = if ($null -ne $restore.Body) { $restore.Body.state } else { $null }
  if (Test-RestoreConfirmed $after) {
    $close = Invoke-FanApi 'POST' '/api/close'
    $after = if ($null -ne $close.Body) { $close.Body.state } else { $null }
  }
}
if ($close.StatusCode -ne 200 -or -not (Test-RestoreConfirmed $after)) {
  Write-Host "恢复未确认：HTTP $($close.StatusCode)"
  if ($null -ne $close.Body) { $close.Body | ConvertTo-Json -Depth 10 }
  Write-Error '保底已生效：Host 保持运行等待重试，禁止强制结束进程。请把上述结果发回。'
  exit 3
}

Write-Host 'OEM 恢复已确认，发送安全关闭请求...'
$shutdown = Invoke-FanApi 'POST' '/api/shutdown'
if ($shutdown.StatusCode -ne 200 -or $null -eq $shutdown.Body -or $shutdown.Body.ok -ne $true) {
  Write-Error "OEM 已确认，但 Host shutdown 未确认（HTTP $($shutdown.StatusCode)）；不要杀进程，稍后重试。"
  if ($null -ne $shutdown.Body) { $shutdown.Body | ConvertTo-Json -Depth 10 }
  exit 4
}

Write-Host 'PASS：OEM 控制已确认恢复，Host 已请求安全退出。'
exit 0
