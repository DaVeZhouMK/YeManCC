# stub_host.ps1 - tiny canned Fan-Host stub for the ymcc_sim TOOL_GATE (204-A SIM-2 negatives).
# Serves GET /health and GET /api/state only; every POST is counted into the request log (so the gate can prove
# "0 change calls"). Never talks to a real host, never writes anything but its own log.
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][int]$Port,
  [ValidateSet('gate-open', 'gate-unknown', 'gate-safe', 'noop-success', 'garbage', 'drop', 'silent')][string]$State = 'gate-open',
  [Parameter(Mandatory = $true)][string]$LogPath,
  [int]$MaxSeconds = 90
)
$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

function Write-Log($obj) {
  $line = ($obj | ConvertTo-Json -Compress -Depth 5)
  [IO.File]::AppendAllText($LogPath, $line + "`n", [Text.UTF8Encoding]::new($false))
}

$stateObj = switch ($State) {
  'gate-open' { [ordered]@{ ok = $true; state = [ordered]@{ state = 'AwaitingControl'; powerState = 'On'; hostMode = 'real-backend'; hardwareWritesEnabled = $true; hardwareWritesObserved = $false } } }
  'gate-unknown' { [ordered]@{ ok = $true; state = [ordered]@{ state = 'Stopped'; powerState = 'Unknown'; hostMode = 'unknown'; hardwareWritesObserved = $false } } }
  'gate-safe' { [ordered]@{ ok = $true; state = [ordered]@{ state = 'Stopped'; powerState = 'Unknown'; hostMode = 'mock-handshake'; hardwareWritesEnabled = $false; hardwareWritesObserved = $false } } }
  # 极端场景（204-I）：声称成功但状态完全不变（"有反馈但实际无数据"）
  'noop-success' { [ordered]@{ ok = $true; state = [ordered]@{ state = 'Stopped'; powerState = 'Unknown'; hostMode = 'mock-handshake'; hardwareWritesEnabled = $false; hardwareWritesObserved = $false } } }
  # 垃圾/截断响应
  'garbage' { [ordered]@{ ok = $true; state = [ordered]@{ state = 'Stopped'; powerState = 'Unknown'; hostMode = 'mock-handshake'; hardwareWritesEnabled = $false } } }
  # 连接被丢弃（RST）
  'drop' { [ordered]@{ ok = $true; state = [ordered]@{ state = 'Stopped'; powerState = 'Unknown'; hostMode = 'mock-handshake'; hardwareWritesEnabled = $false } } }
  # 永不响应（无数据、无反馈、连接挂着）
  'silent' { [ordered]@{ ok = $true; state = [ordered]@{ state = 'Stopped'; powerState = 'Unknown'; hostMode = 'mock-handshake'; hardwareWritesEnabled = $false } } }
}
$stateJson = $stateObj | ConvertTo-Json -Depth 6 -Compress

$listener = [System.Net.HttpListener]::new()
$listener.Prefixes.Add("http://127.0.0.1:$Port/")
$listener.Start()
Write-Log @{ ev = 'stub-listening'; port = $Port; state = $State; at = (Get-Date).ToString('o') }

$deadline = (Get-Date).AddSeconds($MaxSeconds)
try {
  while ((Get-Date) -lt $deadline) {
    # BLOCKING GetContext: this process exists only to serve the stub, and abandoning a pending async context
    # (the first draft) left the request unserved so the client saw a null response.
    $ctx = $listener.GetContext()
    $req = $ctx.Request
    $path = $req.Url.AbsolutePath
    $method = $req.HttpMethod
    $entry = @{ ev = 'request'; method = $method; path = $path; at = (Get-Date).ToString('o') }
    Write-Log $entry
    # 极端响应策略（204-I）：silent 永不响应；drop 直接 RST；garbage 返回截断 JSON；noop-success 一律 ok 但不改状态
    if ($State -eq 'silent') {
      Write-Log @{ ev = 'silent-no-response'; path = $path; at = (Get-Date).ToString('o') }
      Start-Sleep -Seconds 300
      continue
    }
    if ($State -eq 'drop') {
      try { $ctx.Response.Abort() } catch { }
      Write-Log @{ ev = 'dropped'; path = $path; at = (Get-Date).ToString('o') }
      continue
    }
    $body = '{"ok":true}'
    if ($State -eq 'garbage') { $body = '{"ok":true,"state":{"state":"Stopped"' }   # 截断 JSON
    # noop-success 只影响"变更类"请求（声称成功但不改状态）；GET /api/state 仍必须返回真实状态对象，
    # 否则测的就不是"成功但无数据"，而是"状态读取也坏了"（204-I 首跑即因此误判 E4）。
    elseif ($State -eq 'noop-success' -and $method -ne 'GET') { $body = '{"ok":true,"status":"accepted"}' }
    elseif ($method -eq 'GET' -and $path -eq '/health') { $body = '{"ok":true,"host":"stub","protocolVersion":2,"sessionRequired":true}' }
    elseif ($method -eq 'GET' -and $path -eq '/api/state') { $body = $stateJson }
    elseif ($method -ne 'GET') { $body = '{"ok":true,"stub":"accepted"}' }
    $bytes = [Text.Encoding]::UTF8.GetBytes($body)
    $ctx.Response.StatusCode = 200
    $ctx.Response.ContentType = 'application/json'
    $ctx.Response.ContentLength64 = $bytes.Length
    $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
    $ctx.Response.OutputStream.Close()
  }
} finally {
  try { $listener.Stop(); $listener.Close() } catch { }
  Write-Log @{ ev = 'stub-stopped'; at = (Get-Date).ToString('o') }
}