﻿﻿﻿<#
  HC-SLIM-01 / R4 §7：ROG 候选 Backend 风扇测试入口与设备卡准备工具。

  职责（只做 §7 允许的事）：
    * 默认 `-Mode Plan`：只读、零 HTTP、零启动、零落盘（除非显式 -WriteEvidence）。
      解析候选 Host 的映像/运行时/sidecar/授权路径，统计当前 YeManFanHost owner
      （只读进程枚举）、检查详细日志门状态，输出脱敏候选启动参数与 D1/D2/D3 设备卡。
    * 独立显式 `-Mode Device -Action Status|Start|Stop`：仅在用户明确执行时用于真实设备。
      **Device Start / 有序 Stop 的具体执行流程收敛到工具内单一可注入入口 `Invoke-DeviceFlow`；
      生产 Device 分支与 `-SelfTest` 调用同一流程**（§8.2①）。
      复用现有 Host CLI 与认证 resident-adopt 合同；显式加入 `--fan-provider backend`；
      对旧实例分开判定"可请求有序退场"(Live∪Stopped，认证 200) 与"可被页面 adopt"；
      401/foreign/多 owner/身份未知立即止步，绝不轮换活跃 token、绝不杀端口 owner、绝不并起第二 Host。
      真实启动用正确 Windows argv 转义（`Join-WindowsCommandLine`，含空格路径整体保留）。
      回显 token/confirm 一律脱敏。
    * `-SelfTest`：在 fake process/HTTP/时钟边界上跑 T1-T6 + §8 T7-T13 + §9 T14-T18（含真实
      argv 记录器、HTTP.sys raw-owner 正例与最小反推 mutation）离线检查。
    * `-Mutation none|skip-init|skip-pid-binding|skip-identity-admission`：**仅供 `-SelfTest`**
      用于最小反推；生产 Plan/Device 收到非 none 即 BLOCKED。

  边界与禁则（§7/§8/§9）：
    * 不新增产品 UI、不设常驻测试开关、不改默认 HC、不增加生产重复哈希门；
      测试工具不替代正常用户控制路径；不从本工具写安装位；不改产品源码（Host/Backend/native/bridge/
      手柄/灯光）。
    * 实例身份（§9 S8-01）：TCP port-owner 只作占用/诊断（本 Host 用 HttpListener，raw owner 常为
      系统 PID 4，既不能批准也不能否决身份）。身份以**认证只读 GET /api/perf-counters** 的
      processId/非空 hostInstanceId + 精确映像唯一运行进程核验；旧 owner 与新候选共用该入口。
      任何 Open/OpenEvents/Close 之前先核验应答者；错误 body 含 Ready、错误状态码、空实例 ID
      一律不算成功；raw owner 不参与采纳判定。
    * 工具本身不宣称"provider=backend 恢复完成"——那需要设备上首次写入后由日志门证据支撑。
    * 退出码：0 OK / 2 SELFTEST_FAIL / 3 ARG_OR_PREFLIGHT / 10 BLOCKED。
#>
[CmdletBinding()]
param(
    [ValidateSet('Plan', 'Device')]
    [string]$Mode = 'Plan',

    [ValidateSet('Status', 'Start', 'Stop')]
    [string]$Action = 'Status',

    [switch]$SelfTest,

    [string]$PowerControlDir,
    [string]$HostRoot,
    [int]$Port = 8765,
    [string]$SessionTokenPath,
    [string]$FanStateDir,
    [string]$ParentProcess = 'YeManCC',
    [int]$ParentPid = 0,
    [string]$AuthorizationPath,
    [string]$EvidenceDir,
    [switch]$ReadOnlyHost,
    [switch]$WriteEvidence,
    [ValidateSet('none', 'skip-init', 'skip-pid-binding', 'skip-identity-admission')]
    [string]$Mutation = 'none',
    [int]$ReadyTimeoutSec = 40,
    [int]$TailLines = 4000
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# 常量：与产品源码对齐（fanHost.ts / Program.cs）。改动这里必须同步核对方。
# ---------------------------------------------------------------------------
$script:FanHostDefaultPort = 8765
$script:FanHostProtocolVersion = 2
# fanHost.ts isLiveResidentFanState 的镜像；T5 双向断言此处与源清单一致。
$script:LiveResidentFanStates = @(
    'suspended', 'resuming', 'resumed', 'ready',
    'open', 'awaitingcontrol', 'starting', 'handshaking'
)
# §8.2③：旧实例"可请求有序退场"的准入集合 = 存活态 ∪ {stopped}。
# 与"可被页面 adopt"（LiveResidentFanStates）刻意分开：正常 Stopped 旧 Host 可沿认证合同有序退出，
# 但它本身不是 adopt 目标。Stopped 由 Host 的 /api/close 成功后置位，是合法合同态。
$script:OrderlyExitOldOwnerStates = @($script:LiveResidentFanStates + 'stopped')
$script:V3AdapterFile = 'YeManFanHcAdapter.dll'
$script:PayloadManifestFile = 'YeManFanHost.payload.json'
$script:RuntimeManifestFileName = 'HandheldCompanion.runtime.json'
$script:SessionFileName = 'YeManFanHost.session'
$script:AuthorizationFileName = 'YeManFanHost.authorization.md'
$script:HostExeName = 'YeManFanHost.exe'

# ---------------------------------------------------------------------------
# 通用 helper（复用 FanLab\real-host\run-armoury-compatible-emergency-restore.ps1 的模式）
# ---------------------------------------------------------------------------
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
        return (Protect-SensitiveText $json | ConvertFrom-Json)
    } catch {
        return (Protect-SensitiveText ([string]$Value))
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

function Test-SessionToken([string]$Value) {
    return (-not [string]::IsNullOrWhiteSpace($Value)) -and ($Value.Trim() -match '^[0-9a-fA-F]{64}$')
}

function Get-StateNameFromBody($Body) {
    $parsed = if ($Body -is [string]) { ConvertFrom-JsonSafe $Body } else { $Body }
    if ($null -eq $parsed) { return '' }
    if ($parsed.state -is [string]) { return [string]$parsed.state }
    if ($null -ne $parsed.state -and $parsed.state.state -is [string]) { return [string]$parsed.state.state }
    return ''
}

function Get-StateObjectFromBody($Body) {
    # /api/state 形状为嵌套 {"ok":true,"state":{...}}；取内层对象以读 openCalled/openEventsCalled。
    $parsed = if ($Body -is [string]) { ConvertFrom-JsonSafe $Body } else { $Body }
    if ($null -eq $parsed) { return $null }
    if ($null -ne $parsed.state -and $null -ne $parsed.state.state) { return $parsed.state }
    return $null
}

function Test-LiveResidentState([string]$StateName) {
    if ([string]::IsNullOrWhiteSpace($StateName)) { return $false }
    return ($script:LiveResidentFanStates -contains $StateName.ToLowerInvariant())
}

# ---------------------------------------------------------------------------
# Windows argv 转义（§8.2④）：`Start-Process -ArgumentList <数组>` 不保留含空格参数的边界，
# 会把带空格路径拆成多参数。必须按 CommandLineToArgvW/CRT 规则自行拼成单一命令行字符串整体传入。
# 规则：无空白/引号→原样；否则包双引号，内部 `"` 前按 (2n+1) 个反斜杠转义，结尾反斜杠按 2n 翻倍。
# ---------------------------------------------------------------------------
function ConvertTo-WindowsArgumentQuoted([AllowEmptyString()][string]$Arg) {
    if ($null -eq $Arg) { $Arg = '' }
    if ($Arg.Length -gt 0 -and $Arg -notmatch '[\s"]') { return $Arg }
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.Append('"')
    $backslashes = 0
    foreach ($ch in $Arg.ToCharArray()) {
        if ($ch -eq '\') { $backslashes++; continue }
        if ($ch -eq '"') {
            [void]$sb.Append('\' * (2 * $backslashes + 1))
            [void]$sb.Append('"')
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) { [void]$sb.Append('\' * $backslashes); $backslashes = 0 }
        [void]$sb.Append($ch)
    }
    if ($backslashes -gt 0) { [void]$sb.Append('\' * (2 * $backslashes)) }
    [void]$sb.Append('"')
    return $sb.ToString()
}

function Join-WindowsCommandLine([string[]]$ArgList) {
    $parts = @()
    foreach ($a in @($ArgList)) { $parts += (ConvertTo-WindowsArgumentQuoted $a) }
    return ($parts -join ' ')
}

# ---------------------------------------------------------------------------
# 注入式边界：Plan/Device 用真实边界；SelfTest 用 fake 边界。
# 每个边界函数显式调用，便于离线断言"零启动/零写入/零凭据轮换"。
# ---------------------------------------------------------------------------
function New-RealBoundary {
    return @{
        ListOwners = {
            param($exePath)
            $found = @()
            $procs = Get-CimInstance -ClassName Win32_Process -Filter "Name='$script:HostExeName'" -ErrorAction SilentlyContinue
            foreach ($p in $procs) {
                if ($null -ne $p.ExecutablePath -and $p.ExecutablePath -ieq $exePath) {
                    $found += [pscustomobject]@{ pid = [int]$p.ProcessId; path = [string]$p.ExecutablePath }
                }
            }
            return @($found)
        }
        Http = {
            param($port, $method, $path, $token)
            $uri = "http://127.0.0.1:$port$path"
            $params = @{ Method = $method; Uri = $uri; Headers = @{ 'X-YeMan-Fan-Session' = $token }; ErrorAction = 'Stop'; UseBasicParsing = $true; TimeoutSec = 10 }
            if ($method -ne 'GET') { $params.ContentType = 'application/json'; $params.Body = '{}' }
            try {
                $r = Invoke-WebRequest @params
                return @{ status = [int]$r.StatusCode; body = (Read-HttpBody $r); transportError = $null }
            } catch {
                $resp = $null
                try { $resp = $_.Exception.Response } catch {}
                if ($null -eq $resp) { return @{ status = 0; body = ''; transportError = $_.Exception.Message } }
                $status = 0
                try { $status = [int]$resp.StatusCode } catch {}
                return @{ status = $status; body = (Read-HttpBody $resp); transportError = $null }
            }
        }
        StartHost = {
            param($exePath, $commandLine)
            # §8.2④：传入已转义的单一命令行字符串；Start-Process 原样传递，参数边界由 Join-WindowsCommandLine 保证。
            $p = Start-Process -FilePath $exePath -ArgumentList $commandLine -PassThru -WindowStyle Hidden
            return @{ pid = [int]$p.Id }
        }
        GetPortOwners = {
            param($portNumber)
            $owners = @()
            try {
                $conns = Get-NetTCPConnection -LocalPort $portNumber -State Listen -ErrorAction SilentlyContinue
                foreach ($c in $conns) { if ($null -ne $c.OwningProcess) { $owners += [int]$c.OwningProcess } }
            } catch {}
            return @($owners | Select-Object -Unique)
        }
        ListParentProcesses = {
            param($processName)
            $baseName = [IO.Path]::GetFileNameWithoutExtension($processName)
            $found = @()
            foreach ($p in @(Get-Process -Name $baseName -ErrorAction SilentlyContinue)) {
                $found += [pscustomobject]@{ pid = [int]$p.Id; name = [string]$p.ProcessName }
            }
            return @($found)
        }
        ProcessRunning = { param($procId) return ($null -ne (Get-Process -Id $procId -ErrorAction SilentlyContinue)) }
        ReadSidecar = {
            param($path)
            if (Test-Path -LiteralPath $path -PathType Leaf) { return (Get-Content -LiteralPath $path -Raw) }
            return $null
        }
        ReadLog = {
            param($path, $tail)
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return @() }
            return @(Get-Content -LiteralPath $path -Tail $tail -ErrorAction SilentlyContinue)
        }
        Sleep = { param($ms) Start-Sleep -Milliseconds $ms }
        UtcNow = { return [DateTime]::UtcNow }
        HashFile = { param($path) return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
}

# ---------------------------------------------------------------------------
# 路径解析：与 fanHost.ts resolveFanHostConfig + resolveRuntimeFromPayloadManifest
# 保持一致（HC 运行时目录以 payload manifest 为准，V3 adapter 覆盖 assembly）。
# ---------------------------------------------------------------------------
function Resolve-ExpectedPaths {
    param([hashtable]$B, [string]$PowerControl, [string]$HostDirOverride, [string]$SidecarOverride, [string]$AuthOverride)

    $hostDirectory = if (-not [string]::IsNullOrWhiteSpace($HostDirOverride)) { $HostDirOverride } else { Join-Path $PowerControl 'fan-host' }
    $hostParent = $hostDirectory -replace '[\\/]fan-host$', ''
    $hcRuntime = Join-Path $PowerControl 'handheldcompanion-runtime'
    $hcAssembly = Join-Path $hcRuntime 'HandheldCompanion.dll'
    $manifestNote = 'fallback:no-payload-manifest'
    $manifestPath = Join-Path $hostDirectory $script:PayloadManifestFile

    if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
        try {
            $raw = Get-Content -LiteralPath $manifestPath -Raw
            $manifest = ConvertFrom-JsonSafe $raw
            $runtimeManifest = if ($null -ne $manifest -and $manifest.runtimeManifest -is [string]) { $manifest.runtimeManifest.Trim() } else { '' }
            if ($runtimeManifest -match '^\.\.[\\/][^\\/]+(?:[\\/][^\\/]+)*[\\/]HandheldCompanion\.runtime\.json$' -and
                -not ($runtimeManifest.Substring(3) -match '\.\.')) {
                $rel = $runtimeManifest.Substring(3)
                $runtimeManifestPath = Join-Path $hostParent $rel
                if ($runtimeManifestPath -match '(?i)\\HandheldCompanion\.runtime\.json$') {
                    $hcRuntime = Split-Path -Parent $runtimeManifestPath
                    $hcAssembly = Join-Path $hcRuntime 'HandheldCompanion.dll'
                    $manifestNote = 'resolved:payload-manifest'
                }
            }
        } catch {
            $manifestNote = 'error:payload-manifest-read'
        }
    }

    $v3Adapter = Join-Path $hostDirectory $script:V3AdapterFile
    $adapterNote = 'absent'
    if (Test-Path -LiteralPath $v3Adapter -PathType Leaf) {
        $hcAssembly = $v3Adapter
        $adapterNote = 'present:V3-adapter-overrides-assembly'
    }

    $sidecar = if (-not [string]::IsNullOrWhiteSpace($SidecarOverride)) { $SidecarOverride } else { Join-Path $hostDirectory $script:SessionFileName }
    $auth = if (-not [string]::IsNullOrWhiteSpace($AuthOverride)) { $AuthOverride } else { Join-Path $hostDirectory $script:AuthorizationFileName }

    return [ordered]@{
        powerControlDir = $PowerControl
        hostDirectory = $hostDirectory
        hostParent = $hostParent
        hostExecutable = Join-Path $hostDirectory $script:HostExeName
        hcRuntimeDirectory = $hcRuntime
        hcAssemblyPath = $hcAssembly
        runtimeResolution = $manifestNote
        v3Adapter = $adapterNote
        sessionTokenPath = $sidecar
        authorizationPath = $auth
        payloadManifestPath = $manifestPath
    }
}

# ---------------------------------------------------------------------------
# 候选启动参数：镜像 fanHost.ts 的正常启动参数，并显式追加 --fan-provider backend。
# 注意：Device Start 不传 --port，宿主使用默认 8765（页面按 8765 采纳）。
# ---------------------------------------------------------------------------
function New-CandidateLaunchArgs {
    param(
        [hashtable]$Paths,
        [int]$BindParentPid,
        [string]$BindParentProcess,
        [string]$ConfirmationToken,
        [switch]$ReadOnly
    )
    $argList = @(
        '--real-backend',
        '--hc-assembly', $Paths.hcAssemblyPath,
        '--hc-runtime-root', $Paths.hcRuntimeDirectory
    )
    if ($BindParentPid -gt 0) { $argList += @('--parent-pid', [string]$BindParentPid) }
    else { $argList += @('--parent-process', $BindParentProcess) }
    $argList += @('--protocol-version', [string]$script:FanHostProtocolVersion, '--session-token-file', $Paths.sessionTokenPath)
    if (-not $ReadOnly) {
        $argList += @(
            '--allow-hardware-writes',
            '--authorization', $Paths.authorizationPath,
            '--confirm', $ConfirmationToken
        )
    }
    $argList += @('--fan-provider', 'backend')
    return , $argList
}

function Get-RedactedArgs {
    param([string[]]$ArgList)
    $out = @()
    for ($i = 0; $i -lt $ArgList.Count; $i++) {
        $arg = $ArgList[$i]
        $out += $arg
        if ($arg -eq '--confirm' -or $arg -eq '--session-token') {
            $out += '<redacted-token>'
            $i++
        }
    }
    return (($out | ForEach-Object { Protect-SensitiveText $_ }) -join ' ')
}

# ---------------------------------------------------------------------------
# 认证实例身份（§9 S8-01）：认证只读 GET /api/perf-counters 是唯一的应用身份入口。
#   * 不使用 TCP port-owner 判定身份：本 Host 用 System.Net.HttpListener，raw TCP owner 常为
#     系统 PID 4（HTTP.sys），既不能批准也不能否决身份；port-owner 只作占用/诊断。
#   * 成功要求：200、有效 JSON、ok=true、processId 存在且（若给期望）等于期望 PID、
#     hostInstanceId 非空且（若给期望）一致（源锚 Program.cs:17503–17511，初始 Stopped 即可读）。
#   * 旧 owner 与新候选共用本入口；任何 Open/OpenEvents/Close 之前先核验。
# 返回：{ ok; status; processId; hostInstanceId; reason }
# ---------------------------------------------------------------------------
function Read-AuthenticatedInstance {
    param(
        [hashtable]$B, [int]$PortNumber, [string]$Token,
        [int]$ExpectedPid = 0,
        [AllowNull()][string]$ExpectedInstanceId = $null,
        [switch]$SkipInstanceAdmission
    )
    $result = [ordered]@{ ok = $false; status = 0; processId = 0; hostInstanceId = $null; reason = $null }
    $resp = & $B.Http $PortNumber 'GET' '/api/perf-counters' $Token
    $result.status = [int]$resp.status
    if ([int]$resp.status -ne 200) {
        $result.reason = if ([int]$resp.status -eq 401) { 'instance-401-auth-failed' } else { "instance-status-$($resp.status)" }
        return $result
    }
    $parsed = ConvertFrom-JsonSafe $resp.body
    if ($null -eq $parsed) { $result.reason = 'instance-invalid-json'; return $result }
    if (-not [bool]$parsed.ok) { $result.reason = 'instance-ok-not-true'; return $result }
    if ($null -eq $parsed.processId) { $result.reason = 'instance-missing-processid'; return $result }
    $result.processId = [int]$parsed.processId
    if (-not [string]::IsNullOrWhiteSpace([string]$parsed.hostInstanceId)) { $result.hostInstanceId = [string]$parsed.hostInstanceId }
    if ($ExpectedPid -gt 0 -and $result.processId -ne $ExpectedPid) {
        $result.reason = "instance-pid-mismatch:$($result.processId)!=$ExpectedPid"
        return $result
    }
    if (-not $SkipInstanceAdmission) {
        if ([string]::IsNullOrWhiteSpace($result.hostInstanceId)) { $result.reason = 'instance-missing-instanceid'; return $result }
        if (-not [string]::IsNullOrWhiteSpace($ExpectedInstanceId) -and $result.hostInstanceId -ne $ExpectedInstanceId) {
            $result.reason = 'instance-id-changed'
            return $result
        }
    }
    $result.ok = $true
    return $result
}

# ---------------------------------------------------------------------------
# 设备决策（§8.2③ + §9 S8-01）：把"旧实例可请求有序退场"与"可被页面 adopt"分开判定。
#   * 无匹配 owner 且端口无 foreign 监听 → START_CANDIDATE
#   * 无匹配 owner 但端口被他人占用（foreign listener）→ STOP（身份未知，不并起第二 Host）
#   * >1 匹配 owner → STOP（多 owner，不猜测/不强杀）
#   * 恰好 1 匹配 owner：需认证 state 200 且 Instance（认证 perf 身份）ok，且状态 ∈ Live∪{Stopped}
#     → END_OLD_CONTROL_THEN_START；否则（401/身份缺失/失配/未知态/FaultLocked）→ STOP
# 返回对象：decision / ownerCount / portOwnerCount / stateName / reason
# ---------------------------------------------------------------------------
function Resolve-DeviceDecision {
    param($Owners, $Probe, $PortOwners, $Instance)
    $ownerCount = @($Owners).Count
    $portOwnerCount = @($PortOwners).Count
    $stateName = ''
    if ($null -ne $Probe) { $stateName = Get-StateNameFromBody $Probe.body }
    $lowerState = if ([string]::IsNullOrWhiteSpace($stateName)) { '' } else { $stateName.ToLowerInvariant() }

    if ($ownerCount -eq 0) {
        if ($portOwnerCount -gt 0) {
            return [ordered]@{ decision = 'STOP_OWNER_UNKNOWN'; ownerCount = 0; portOwnerCount = $portOwnerCount; stateName = $stateName; reason = 'foreign-port-listener' }
        }
        return [ordered]@{ decision = 'START_CANDIDATE'; ownerCount = 0; portOwnerCount = 0; stateName = $stateName; reason = 'no-owner' }
    }
    if ($ownerCount -gt 1) {
        return [ordered]@{ decision = 'STOP_OWNER_UNKNOWN'; ownerCount = $ownerCount; portOwnerCount = $portOwnerCount; stateName = $stateName; reason = 'multi-owner' }
    }
    if ($null -eq $Probe -or [int]$Probe.status -ne 200) {
        $probeStatus = if ($null -ne $Probe) { [int]$Probe.status } else { 0 }
        return [ordered]@{ decision = 'STOP_OWNER_UNKNOWN'; ownerCount = 1; portOwnerCount = $portOwnerCount; stateName = $stateName; reason = "probe-not-authenticated:$probeStatus" }
    }
    if ($null -eq $Instance -or -not [bool]$Instance.ok) {
        $instReason = if ($null -ne $Instance) { [string]$Instance.reason } else { 'instance-missing' }
        return [ordered]@{ decision = 'STOP_OWNER_UNKNOWN'; ownerCount = 1; portOwnerCount = $portOwnerCount; stateName = $stateName; reason = "instance-not-authenticated:$instReason" }
    }
    if (-not ($script:OrderlyExitOldOwnerStates -contains $lowerState)) {
        return [ordered]@{ decision = 'STOP_OWNER_UNKNOWN'; ownerCount = 1; portOwnerCount = $portOwnerCount; stateName = $stateName; reason = "state-not-orderly-exit:$stateName" }
    }
    return [ordered]@{ decision = 'END_OLD_CONTROL_THEN_START'; ownerCount = 1; portOwnerCount = $portOwnerCount; stateName = $stateName; reason = 'authenticated-orderly-exit' }
}

# ---------------------------------------------------------------------------
# 日志门 + provider 证据（只有详细日志门开启，provider 事件才落 runtime log）。
# ---------------------------------------------------------------------------
function Get-LoggingGateState {
    param([string]$StateRoot)
    $flagPath = Join-Path $StateRoot 'fan-logging-enabled.flag'
    $exists = Test-Path -LiteralPath $flagPath -PathType Leaf
    $enabled = $false
    if ($exists) {
        $value = Get-Content -LiteralPath $flagPath -Raw -ErrorAction SilentlyContinue
        if ($null -ne $value) {
            $value = $value.Trim()
            if ($value -ieq 'enabled' -or $value -eq '1') { $enabled = $true }
        }
    }
    return [ordered]@{ flagPath = $flagPath; exists = $exists; enabled = $enabled }
}

function Get-ProviderEvidence {
    param($Lines, [int]$ProcessId, [string]$HostInstanceId)
    $frozen = $null
    $dispatch = $null
    $dispatchCount = 0
    foreach ($line in @($Lines)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $rec = ConvertFrom-JsonSafe $line
        if ($null -eq $rec) { continue }
        if ($null -ne $rec.processId -and [int]$rec.processId -ne $ProcessId) { continue }
        if (-not [string]::IsNullOrWhiteSpace($HostInstanceId) -and $rec.hostInstanceId -ne $HostInstanceId) { continue }
        if ($rec.eventName -eq 'fan.provider.frozen') { $frozen = $rec }
        elseif ($rec.eventName -like 'hc.fan-dispatch.*') { $dispatchCount += 1; $dispatch = $rec }
    }
    return [ordered]@{
        providerFrozen = [ordered]@{
            found = ($null -ne $frozen)
            provider = if ($null -ne $frozen) { [string]$frozen.provider } else { $null }
            component = if ($null -ne $frozen -and $null -ne $frozen.details) { [string]$frozen.details.component } else { $null }
        }
        fanDispatch = [ordered]@{
            found = ($null -ne $dispatch)
            count = $dispatchCount
            source = if ($null -ne $dispatch) { [string]$dispatch.source } else { $null }
            provider = if ($null -ne $dispatch) { [string]$dispatch.provider } else { $null }
            operation = if ($null -ne $dispatch) { [string]$dispatch.operation } else { $null }
        }
        backendProviderObserved = ($null -ne $frozen -and [string]$frozen.provider -ieq 'backend')
        backendDispatchObserved = ($null -ne $dispatch -and [string]$dispatch.source -ieq 'backend.asus-acpi-curve')
    }
}

# ---------------------------------------------------------------------------
# 设备卡输出
# ---------------------------------------------------------------------------
function Show-RogChecklist {
    Write-Host ''
    Write-Host '===== ROG 候选 Backend 设备卡（由用户执行；本工具默认不执行）====='
    Write-Host 'D1 清醒时连续调整：'
    Write-Host '  步骤：用 -Mode Device -Action Start 启动候选；页面开启风扇；调整一次曲线；再调整一次。'
    Write-Host '  判据：实际 PID/代次/包身份；fan.provider.frozen 的 provider=backend 且 hc.fan-dispatch.*'
    Write-Host '        的 source=backend.asus-acpi-curve；两次新请求各有当次收据；用户观察变化。'
    Write-Host '  不算通过：默认 HC、旧收据、单纯 Open 回执。'
    Write-Host 'D2 关闭/重开：'
    Write-Host '  步骤：页面关闭；按既有路径退出宿主；再用 -Mode Device -Action Start 重开并调一次。'
    Write-Host '  判据：本代自有句柄 release、停止/退出及新代 reopen/新写入链；无双 owner；无新旧收据混用。'
    Write-Host 'D3 两轮睡眠/唤醒：'
    Write-Host '  步骤：受控时睡眠->唤醒两轮，每轮继续调节；若自动恢复失败则点击原单开关手动继续。'
    Write-Host '  判据：同周期关闭/重开、当前 provider、意图保留与下一次写入；手动入口可用，失败不能永久禁用。'
    Write-Host '兜底：若 provider 实证仍是 HC，只登记 HC 回归，不晋级候选；任一项失败保留完整日志与失败阶段，'
    Write-Host '      停止扩大候选部署并交回。设备结果不外推 MSI/GPD/AYA/OneX。'
    Write-Host '撤回：以 -Mode Device -Action Stop 经既有认证 close/shutdown 结束候选；确认 owner 归零；'
    Write-Host '      之后正常路径继续默认 HC（不需要改产品源码或重出包）。'
    Write-Host '=============================================================='
}

# ---------------------------------------------------------------------------
# Device 动作实现（§8.2①③⑤）：所有设备侧改动集中在 Invoke-DeviceFlow；
# 生产 Device 分支与 -SelfTest 调用同一流程，禁止另写"应该调用哪些 API"的镜像。
# ---------------------------------------------------------------------------
function Invoke-EndOldControl {
    # §8.2⑤ + §9 S8-02：确切实例的退场责任。
    #   * 接收期望 PID 与实例 ID；Close 前与 Close→Shutdown 之间各复核同一应答者（认证 perf 身份）。
    #   * Close 按真实嵌套状态合同：Program.cs:17204–17222 返回 {ok:true,state}，无顶层 hostInstanceId。
    #   * shutdown 按 {ok:true,shutdownRequested:true} 认定。
    #   * 401、身份缺失/失配、perf 实例/PID 改变立即止步（不再试 close、不无认证 shutdown/强杀）。
    param(
        [hashtable]$B, [int]$PortNumber, [string]$Token, $Summary,
        [int]$ExpectedPid = 0, [AllowNull()][string]$ExpectedInstanceId = $null
    )
    $result = [ordered]@{
        ok = $false; preInstanceId = $null; postInstanceId = $null
        closeStatus = $null; closeState = ''; closeOk = $false
        shutdownStatus = $null; shutdownOk = $false; shutdownRequested = $false
        haltedAt = $null; reason = $null
    }

    # (1) Close 前先核验应答者是期望实例（不猜端口监听者，不用 raw TCP owner）。
    $pre = Read-AuthenticatedInstance -B $B -PortNumber $PortNumber -Token $Token -ExpectedPid $ExpectedPid -ExpectedInstanceId $ExpectedInstanceId
    $Summary.steps += [ordered]@{ name = 'end-old-pre-close'; status = $pre.status; reason = $pre.reason }
    $result.preInstanceId = $pre.hostInstanceId
    if (-not $pre.ok) {
        $result.haltedAt = 'pre-close'
        $result.reason = "pre-close-instance-not-authenticated:$($pre.reason)"
        return $result
    }

    # (2) POST /api/close（真实合同 {ok:true,state}，无顶层 hostInstanceId）。
    $close = & $B.Http $PortNumber 'POST' '/api/close' $Token
    $result.closeStatus = [int]$close.status
    $result.closeState = Get-StateNameFromBody $close.body
    $closeParsed = ConvertFrom-JsonSafe $close.body
    $result.closeOk = ($null -ne $closeParsed -and [bool]$closeParsed.ok)
    $Summary.steps += [ordered]@{ name = 'end-old-close'; status = $close.status; state = $result.closeState; ok = $result.closeOk }

    if ([int]$close.status -eq 401) { $result.haltedAt = 'close'; $result.reason = 'close-401-auth-failed'; return $result }
    if ([int]$close.status -ne 200) { $result.haltedAt = 'close'; $result.reason = "close-status-$($close.status)"; return $result }
    if (-not $result.closeOk) { $result.haltedAt = 'close'; $result.reason = 'close-ok-not-true'; return $result }

    # (3) Close→Shutdown 之间复核同一应答者：身份改变立即止步，绝不向新一代应答者发 shutdown。
    $post = Read-AuthenticatedInstance -B $B -PortNumber $PortNumber -Token $Token -ExpectedPid $ExpectedPid -ExpectedInstanceId $ExpectedInstanceId
    $Summary.steps += [ordered]@{ name = 'end-old-post-close'; status = $post.status; reason = $post.reason }
    $result.postInstanceId = $post.hostInstanceId
    if (-not $post.ok) {
        $result.haltedAt = 'post-close'
        $result.reason = "post-close-instance-change:$($post.reason)"
        return $result
    }

    # (4) POST /api/shutdown（合同 {ok:true,shutdownRequested:true}）。
    $shutdown = & $B.Http $PortNumber 'POST' '/api/shutdown' $Token
    $result.shutdownStatus = [int]$shutdown.status
    $shutdownParsed = ConvertFrom-JsonSafe $shutdown.body
    $result.shutdownRequested = ($null -ne $shutdownParsed -and [bool]$shutdownParsed.shutdownRequested)
    $Summary.steps += [ordered]@{ name = 'end-old-shutdown'; status = $shutdown.status; shutdownRequested = $result.shutdownRequested }
    if ([int]$shutdown.status -eq 401) { $result.haltedAt = 'shutdown'; $result.reason = 'shutdown-401-auth-failed'; return $result }
    if ([int]$shutdown.status -ne 200) { $result.haltedAt = 'shutdown'; $result.reason = "shutdown-status-$($shutdown.status)"; return $result }
    if (-not $result.shutdownRequested) { $result.haltedAt = 'shutdown'; $result.reason = 'shutdown-not-requested'; return $result }
    $result.shutdownOk = $true
    $result.ok = $true
    return $result
}

function Wait-OwnerZero {
    param([hashtable]$B, [string]$ExePath, [int]$TimeoutSec, $Summary)
    $deadline = (& $B.UtcNow).AddSeconds($TimeoutSec)
    while ((& $B.UtcNow) -lt $deadline) {
        $owners = @(& $B.ListOwners $ExePath)
        if ($owners.Count -eq 0) { return $true }
        & $B.Sleep 250
    }
    return $false
}

function Invoke-CandidateTeardown {
    # §8.2⑤ + §9 S8-02：仅当本轮候选身份已由认证 perf 证明（同为 startedPid 的确切实例）时才允许
    # 认证有序退场；EndOldControl 在 Close 前与 Close→Shutdown 之间复核同一应答者，401/身份改变即止步，
    # 绝不无认证关闭/kill，也不冒充已释放。未证身份/身份已变时调用方不得进入本函数。
    param(
        [hashtable]$B, [int]$PortNumber, [string]$Token, [int]$StartedPid, [string]$ExePath,
        [int]$TimeoutSec, $Summary, [AllowNull()][string]$ExpectedInstanceId = $null
    )
    $teardown = [ordered]@{ attempted = $true; closeStatus = $null; shutdownStatus = $null; haltedAt = $null; ownerZero = $false; reason = $null }
    $end = Invoke-EndOldControl -B $B -PortNumber $PortNumber -Token $Token -Summary $Summary -ExpectedPid $StartedPid -ExpectedInstanceId $ExpectedInstanceId
    $teardown.closeStatus = $end.closeStatus
    $teardown.shutdownStatus = $end.shutdownStatus
    $teardown.haltedAt = $end.haltedAt
    if ($end.haltedAt) { $teardown.reason = "teardown-halted-at-$($end.haltedAt):$($end.reason)"; return $teardown }
    $teardown.ownerZero = Wait-OwnerZero -B $B -ExePath $ExePath -TimeoutSec $TimeoutSec -Summary $Summary
    if (-not $teardown.ownerZero) { $teardown.reason = 'teardown-owner-not-zero' }
    return $teardown
}

function Invoke-CandidateBringUp {
    # §9 S8-01/S8-02：先用认证实例身份证明应答者属于本次新 Host，再依既有 Host 合同
    # POST /api/open → /api/open-events 到达真可 adopt（state 200/ok + Ready + openCalled&&openEventsCalled + 同一认证实例）。
    #   * 身份 = 恰好一个同映像 owner 且 PID=startedPid（精确映像核验）+ 认证 perf 的 processId 一致、
    #     hostInstanceId 非空。raw TCP port-owner 只登记诊断，不作身份（HttpListener 下常为系统 PID 4）。
    #   * 身份未证 → 不 teardown（不向未知应答者发 close/shutdown，保留 startedPid 与残留责任）。
    #   * 401 / perf PID/实例改变 → 立即止步：零后续变更、零清理。
    #   * 仅当身份成立、且初始化普通失败或 adopt 超时时，才按合同对本工具实例做认证有序退场。
    param(
        [hashtable]$B, $Paths, [int]$PortNumber, [string]$Token,
        [int]$BindParentPid, [string]$BindParentProcess, [int]$TimeoutSec, $Summary,
        [string]$Mutation = 'none', [switch]$ReadOnlyHost
    )
    $result = [ordered]@{
        ok = $false; startedPid = $null; state = ''; hostInstanceId = $null
        bindingVerified = $false; identityVerified = $false; initSkipped = $false; initPerformed = $false
        adoptable = $false; perfPid = $null; pidBound = $false; reason = $null; mutation = $Mutation
        portOwnerDiagnostic = @(); commandLineRedacted = ''; teardown = $null
    }
    $skipIdentity = ($Mutation -eq 'skip-identity-admission')
    $enforcePid = ($Mutation -ne 'skip-pid-binding')
    $teardownArgs = @{ B = $B; PortNumber = $PortNumber; Token = $Token; ExePath = $Paths.hostExecutable; TimeoutSec = 15; Summary = $Summary }

    $argList = New-CandidateLaunchArgs -Paths $Paths -BindParentPid $BindParentPid -BindParentProcess $BindParentProcess -ConfirmationToken $Token -ReadOnly:$ReadOnlyHost
    $result.commandLineRedacted = Get-RedactedArgs -ArgList $argList
    $commandLine = Join-WindowsCommandLine $argList
    $started = & $B.StartHost $Paths.hostExecutable $commandLine
    $startedPid = [int]$started.pid
    $result.startedPid = $startedPid
    $expectedPid = if ($enforcePid) { $startedPid } else { 0 }
    $Summary.steps += [ordered]@{ name = 'start-candidate'; pid = $startedPid; argsRedacted = $result.commandLineRedacted }

    $deadline = (& $B.UtcNow).AddSeconds($TimeoutSec)

    # (1) 身份准入：精确映像唯一 owner（PID=startedPid）+ 认证 perf 证明应答者是同一实例。
    $capturedInstanceId = $null
    while ((& $B.UtcNow) -lt $deadline) {
        $owners = @(& $B.ListOwners $Paths.hostExecutable)
        $ownerPids = @($owners | ForEach-Object { [int]$_.pid })
        $result.portOwnerDiagnostic = @(& $B.GetPortOwners $PortNumber)
        $uniqueSameImage = (($ownerPids.Count -eq 1) -and ($ownerPids -contains $startedPid))
        if ($uniqueSameImage) {
            $inst = Read-AuthenticatedInstance -B $B -PortNumber $PortNumber -Token $Token -ExpectedPid $expectedPid -SkipInstanceAdmission:$skipIdentity
            $result.perfPid = $inst.processId
            if ($inst.ok) {
                $result.identityVerified = $true
                $result.hostInstanceId = $inst.hostInstanceId
                $capturedInstanceId = $inst.hostInstanceId
                break
            }
            if ($inst.reason -like 'instance-401*' -or $inst.reason -like 'instance-pid-mismatch*') {
                $result.reason = "candidate-instance-rejected:$($inst.reason)"
                return $result
            }
        }
        & $B.Sleep 250
    }
    $result.bindingVerified = $result.identityVerified
    $result.pidBound = $result.identityVerified
    if (-not $result.identityVerified) {
        $result.reason = 'candidate-identity-timeout'
        return $result
    }

    # 复读 sidecar：Host `--session-token-file` 要求文件存在并读取（非重生成）。若凭据变化/失效，
    # 说明身份状态改变：立即止步，不做无认证关闭/kill。
    $sidecarRaw = & $B.ReadSidecar $Paths.sessionTokenPath
    $effectiveToken = if ($null -ne $sidecarRaw) { $sidecarRaw.Trim() } else { '' }
    if (-not (Test-SessionToken $effectiveToken) -or $effectiveToken -ne $Token) {
        $result.reason = 'candidate-sidecar-changed'
        return $result
    }

    # (2) 初始化：冷启动 Stopped →（POST open）Open →（POST open-events）Ready。
    #    401 一律止步且零清理；非 200 的普通失败在身份成立时按合同有序退场。
    if ($Mutation -eq 'skip-init') {
        $result.initSkipped = $true
    } else {
        $open = & $B.Http $PortNumber 'POST' '/api/open' $Token
        $Summary.steps += [ordered]@{ name = 'candidate-open'; status = $open.status; state = (Get-StateNameFromBody $open.body) }
        if ([int]$open.status -eq 401) { $result.reason = 'candidate-open-401'; return $result }
        if ([int]$open.status -ne 200) {
            $result.reason = "candidate-open-status-$($open.status)"
            $result.teardown = Invoke-CandidateTeardown @teardownArgs -StartedPid $startedPid -ExpectedInstanceId $capturedInstanceId
            return $result
        }
        $openEvents = & $B.Http $PortNumber 'POST' '/api/open-events' $Token
        $Summary.steps += [ordered]@{ name = 'candidate-open-events'; status = $openEvents.status; state = (Get-StateNameFromBody $openEvents.body) }
        if ([int]$openEvents.status -eq 401) { $result.reason = 'candidate-open-events-401'; return $result }
        if ([int]$openEvents.status -ne 200) {
            $result.reason = "candidate-open-events-status-$($openEvents.status)"
            $result.teardown = Invoke-CandidateTeardown @teardownArgs -StartedPid $startedPid -ExpectedInstanceId $capturedInstanceId
            return $result
        }
        $result.initPerformed = $true
    }

    # (3) 有界观测到达真可 adopt：state 必须 200 且 ok=true 且为 Ready，Open/OpenEvents 标志成立，
    #     且认证 perf 实例自始至终一致。错误状态码即使 body 含 Ready 也不能算成功。
    $adoptable = $false
    while ((& $B.UtcNow) -lt $deadline) {
        $stateResp = & $B.Http $PortNumber 'GET' '/api/state' $Token
        $stateParsed = ConvertFrom-JsonSafe $stateResp.body
        $stateObj = Get-StateObjectFromBody $stateResp.body
        $stateName = Get-StateNameFromBody $stateResp.body
        $result.state = $stateName
        $inst = Read-AuthenticatedInstance -B $B -PortNumber $PortNumber -Token $Token -ExpectedPid $expectedPid -ExpectedInstanceId $capturedInstanceId -SkipInstanceAdmission:$skipIdentity
        $result.perfPid = $inst.processId
        if (-not $inst.ok) {
            if ($inst.reason -like 'instance-401*' -or $inst.reason -like 'instance-pid-mismatch*' -or $inst.reason -eq 'instance-id-changed') {
                $result.reason = "candidate-instance-changed:$($inst.reason)"
                return $result
            }
        }
        $stateOk200 = ([int]$stateResp.status -eq 200)
        $stateBodyOk = ($null -ne $stateParsed -and [bool]$stateParsed.ok)
        $openCalled = ($null -ne $stateObj -and [bool]$stateObj.openCalled)
        $openEventsCalled = ($null -ne $stateObj -and [bool]$stateObj.openEventsCalled)
        if ($stateOk200 -and $stateBodyOk -and ($stateName -ieq 'Ready') -and $openCalled -and $openEventsCalled -and $inst.ok) { $adoptable = $true; break }
        & $B.Sleep 250
    }
    $result.adoptable = $adoptable
    $result.ok = $adoptable
    if (-not $adoptable) {
        if ([string]::IsNullOrWhiteSpace($result.reason)) { $result.reason = 'candidate-adopt-timeout' }
        $result.teardown = Invoke-CandidateTeardown @teardownArgs -StartedPid $startedPid -ExpectedInstanceId $capturedInstanceId
    }
    return $result
}

function Invoke-DeviceFlow {
    # §8.2① + §9：Device Start/有序 Stop/Status 的唯一入口；生产 Device 分支与 -SelfTest 调用同一流程。
    # 旧应答者身份一律走认证 perf 实例核验（§9 S8-01）；有序退场由 Invoke-EndOldControl 复核（§9 S8-02）；
    # -ReadOnlyHost 必须真实传递到子进程参数（§9 S8-03，禁止静默启动可写 Host）。
    param(
        [hashtable]$B, $Paths, [string]$Action, [int]$PortNumber, [string]$Token,
        [int]$BindParentPid, [string]$BindParentProcess, [int]$ReadyTimeoutSec, [int]$TailLines,
        [string]$FanStateDir, $Summary, [string]$Mutation = 'none', [switch]$ForSelfTest, [switch]$ReadOnlyHost
    )
    $flow = [ordered]@{
        action = $Action; exitCode = 0; blockedReason = $null; decision = $null
        endOld = $null; bringUp = $null; candidateIdentity = $null; providerEvidence = $null
    }

    if ($Mutation -ne 'none' -and -not $ForSelfTest) {
        $flow.blockedReason = "Mutation '$Mutation' 仅允许在 -SelfTest 中使用（生产 Plan/Device 不接受强注入）。"
        $flow.exitCode = 10
        Add-Check 'device-mutation-forbidden' $false $flow.blockedReason
        return $flow
    }

    if ($Action -eq 'Status') {
        $health = & $B.Http $PortNumber 'GET' '/health' $Token
        $stateResp = & $B.Http $PortNumber 'GET' '/api/state' $Token
        Add-Check 'status-health' ([int]$health.status -eq 200) @{ status = $health.status }
        Add-Check 'status-state' ([int]$stateResp.status -eq 200) @{ status = $stateResp.status; state = (Get-StateNameFromBody $stateResp.body) }
        $perf = & $B.Http $PortNumber 'GET' '/api/perf-counters' $Token
        $perfParsed = ConvertFrom-JsonSafe $perf.body
        $runtimeLog = Join-Path $FanStateDir 'logs\yeman-fan-host-runtime.log'
        if ($null -ne $perfParsed) {
            $lines = & $B.ReadLog $runtimeLog $TailLines
            $flow.providerEvidence = Get-ProviderEvidence -Lines $lines -ProcessId ([int]$perfParsed.processId) -HostInstanceId ([string]$perfParsed.hostInstanceId)
            $Summary.providerEvidence = $flow.providerEvidence
        }
        return $flow
    }

    # Start 需要唯一父进程绑定；Stop 不需要。
    $parentPidResolved = $BindParentPid
    if ($Action -eq 'Start' -and $parentPidResolved -le 0) {
        $candidates = @(& $B.ListParentProcesses $BindParentProcess)
        if ($candidates.Count -ne 1) {
            $flow.blockedReason = "需要恰好一个运行中的 $BindParentProcess 作为父进程，实际找到 $($candidates.Count) 个。"
            Add-Check 'device-parent-unique' $false $flow.blockedReason
            $flow.exitCode = 10
            return $flow
        }
        $parentPidResolved = [int]$candidates[0].pid
        Add-Check 'device-parent-unique' $true "pid=$parentPidResolved"
    }
    # §9 S8-03：文档声称的"验证存活"必须真实执行，否则不得声称父进程约束成立。
    if ($Action -eq 'Start' -and $parentPidResolved -gt 0) {
        if (-not (& $B.ProcessRunning $parentPidResolved)) {
            $flow.blockedReason = "指定的父进程 pid=$parentPidResolved 当前不存活：拒绝启动可写 Host。"
            Add-Check 'device-parent-alive' $false "pid=$parentPidResolved not running"
            $flow.exitCode = 10
            return $flow
        }
        Add-Check 'device-parent-alive' $true "pid=$parentPidResolved"
    }

    $ownersNow = @(& $B.ListOwners $Paths.hostExecutable)
    $portOwnersNow = @(& $B.GetPortOwners $PortNumber)
    $probe = if ($ownersNow.Count -eq 1) { & $B.Http $PortNumber 'GET' '/api/state' $Token } else { $null }
    # 旧应答者身份由认证 perf 实例核验（§9 S8-01）；raw TCP port-owner 只作诊断，不作身份。
    $instance = if ($ownersNow.Count -eq 1) { Read-AuthenticatedInstance -B $B -PortNumber $PortNumber -Token $Token -ExpectedPid ([int]$ownersNow[0].pid) } else { $null }
    $flow.instance = $instance
    $decision = Resolve-DeviceDecision -Owners $ownersNow -Probe $probe -PortOwners $portOwnersNow -Instance $instance
    $flow.decision = $decision
    Add-Check 'device-decision' ($decision.decision -ne 'STOP_OWNER_UNKNOWN') $decision

    $oldExpectedPid = if ($ownersNow.Count -eq 1) { [int]$ownersNow[0].pid } else { 0 }
    $oldExpectedInstanceId = if ($null -ne $instance -and $instance.ok) { [string]$instance.hostInstanceId } else { $null }

    if ($decision.decision -eq 'STOP_OWNER_UNKNOWN') {
        $flow.blockedReason = "旧 owner/端口/认证不明（$($decision.reason)）：停止本次测试（不轮换 token、不杀端口 owner、不并起第二 Host）。"
        $flow.exitCode = 10
        return $flow
    }

    if ($Action -eq 'Stop') {
        if ($decision.decision -eq 'START_CANDIDATE') {
            $Summary.steps += [ordered]@{ name = 'stop-noop'; reason = 'no-owner' }
            Add-Check 'device-stop-noop' $true 'no owner'
            return $flow
        }
        $end = Invoke-EndOldControl -B $B -PortNumber $PortNumber -Token $Token -Summary $Summary -ExpectedPid $oldExpectedPid -ExpectedInstanceId $oldExpectedInstanceId
        $flow.endOld = $end
        if ($end.haltedAt) {
            $flow.blockedReason = "有序退场在 $($end.haltedAt) 阶段止步（$($end.reason)）：不继续变更。"
            Add-Check 'device-stop-halted' $false $end.reason
            $flow.exitCode = 10
            return $flow
        }
        if (-not (Wait-OwnerZero -B $B -ExePath $Paths.hostExecutable -TimeoutSec 15 -Summary $Summary)) {
            $flow.blockedReason = '认证关闭后 owner 未归零：停止，绝不 Force。'
            Add-Check 'device-owner-zero' $false $flow.blockedReason
            $flow.exitCode = 10
            return $flow
        }
        Add-Check 'device-stop-owner-zero' $true 'owner zero after authenticated stop'
        return $flow
    }

    # Action=Start：旧实例（Live∪Stopped，认证成立）先真实有序退出并确认归零，再启动唯一候选。
    if ($decision.decision -eq 'END_OLD_CONTROL_THEN_START') {
        $end = Invoke-EndOldControl -B $B -PortNumber $PortNumber -Token $Token -Summary $Summary -ExpectedPid $oldExpectedPid -ExpectedInstanceId $oldExpectedInstanceId
        $flow.endOld = $end
        if ($end.haltedAt) {
            $flow.blockedReason = "旧实例有序退场在 $($end.haltedAt) 阶段止步（$($end.reason)）：不启动候选。"
            Add-Check 'device-end-old-halted' $false $end.reason
            $flow.exitCode = 10
            return $flow
        }
        if (-not (Wait-OwnerZero -B $B -ExePath $Paths.hostExecutable -TimeoutSec 15 -Summary $Summary)) {
            $flow.blockedReason = '既有认证关闭后 owner 未归零：停止，绝不 Force、不并起第二 Host。'
            Add-Check 'device-owner-zero' $false $flow.blockedReason
            $flow.exitCode = 10
            return $flow
        }
        Add-Check 'device-old-owner-zero' $true 'old owner zero after authenticated exit'
    }

    $bringUp = Invoke-CandidateBringUp -B $B -PortNumber $PortNumber -Token $Token -Paths $Paths -BindParentPid $parentPidResolved -BindParentProcess $BindParentProcess -TimeoutSec $ReadyTimeoutSec -Summary $Summary -Mutation $Mutation -ReadOnlyHost:$ReadOnlyHost
    $flow.bringUp = $bringUp
    Add-Check 'device-candidate-identity' ([bool]$bringUp.identityVerified) @{ startedPid = $bringUp.startedPid; identityVerified = $bringUp.identityVerified; perfPid = $bringUp.perfPid; portOwnerDiagnostic = $bringUp.portOwnerDiagnostic }
    Add-Check 'device-candidate-adoptable' ([bool]$bringUp.adoptable) @{ state = $bringUp.state; startedPid = $bringUp.startedPid; perfPid = $bringUp.perfPid; identityVerified = $bringUp.identityVerified }
    if (-not $bringUp.adoptable) {
        $flow.blockedReason = "候选未到达真可 adopt（$($bringUp.reason)）。"
        $flow.exitCode = 10
        return $flow
    }
    $flow.candidateIdentity = [ordered]@{ pid = $bringUp.startedPid; state = $bringUp.state; hostInstanceId = $bringUp.hostInstanceId; perfPid = $bringUp.perfPid }
    $Summary.candidateIdentity = $flow.candidateIdentity
    return $flow
}

# ---------------------------------------------------------------------------
# 主体
# ---------------------------------------------------------------------------
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($PowerControlDir)) { $PowerControlDir = Join-Path $repoRoot 'PowerControl' }
if ([string]::IsNullOrWhiteSpace($FanStateDir)) { $FanStateDir = Join-Path $env:LOCALAPPDATA 'YeManCC\fan-host' }
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $stamp = (Get-Date).ToString('yyyyMMdd')
    $EvidenceDir = Join-Path $repoRoot ("Build\Validation\fan-backend-rog-test-prep-{0}" -f $stamp)
}

$exitCode = 0
$summary = [ordered]@{
    tool = 'prepare-fan-backend-rog-test'
    task = 'HC-SLIM-01-R4-S8'
    mode = $Mode
    action = $Action
    mutation = $Mutation
    startedAtUtc = [DateTime]::UtcNow
    hostDefaultPort = $script:FanHostDefaultPort
    liveResidentStates = $script:LiveResidentFanStates
    checks = @()
    steps = @()
    httpRequests = @()
    candidateArgsRedacted = ''
    providerEvidence = $null
    blockedReason = $null
}

function Add-Check([string]$Name, [bool]$Ok, $Detail) {
    $summary.checks += [ordered]@{ name = $Name; ok = $Ok; detail = Get-SafeValue $Detail }
}

# ===========================================================================
# SelfTest：fake process/HTTP/时钟边界离线检查 T1-T18（§8.3 + §9 S8-01..03）
#   * 所有 fake 边界内联在本函数作用域定义：scriptblock 被 Invoke-DeviceFlow /
#     Invoke-CandidateBringUp 等嵌套函数经 `& $B.Http` 调用时，按 PowerShell 动态作用域
#     沿调用栈解析 $fxSim/$fxClock（Invoke-SelfTest 仍在栈上），共享可变状态用引用类型承载。
#   * 生产 Device 分支与本函数调用完全相同的 Invoke-DeviceFlow（§8.2①），不写
#     "应该调用哪些 API"的镜像；反例与最小反推（skip-init / skip-pid-binding）都走同一流程。
# ===========================================================================
function Invoke-SelfTest {
    $script:SelfTestFailures = @()
    $script:SelfTestResults = @()
    function Assert-That([string]$Name, [bool]$Condition, [string]$Detail) {
        if ($Condition) { $script:SelfTestResults += "PASS  $Name" }
        else { $script:SelfTestFailures += "$Name :: $Detail"; $script:SelfTestResults += "FAIL  $Name :: $Detail" }
    }
    $fakePowerControl = Join-Path ([System.IO.Path]::GetTempPath()) 'fan-rog-selftest-nonexistent'

    # --- 共享 fake 边界（内联，动态作用域见文件头说明）---
    $fxClock = @{ now = [DateTime]::UtcNow }
    $fxSim = $null
    $fxNewSim = {
        param($Overrides)
        $sim = [ordered]@{
            healthStatus = 200; stateStatus = 200; perfStatus = 200
            openStatus = 200; openEventsStatus = 200; closeStatus = 200; shutdownStatus = 200
            sState = 'Stopped'; openCalled = $false; openEventsCalled = $false; neverReady = $false
            perfPidOverride = $null; perfHostInstanceId = 'h-new'; perfInstanceAfterOpen = $null; perfOmitProcessId = $false
            liveOwners = @(); portOwners = @(); ownersAtStart = @()
            sidecarToken = ''; sidecarAfterStart = $null; bindOnStart = $true; portOwnerRaw = $null; parentAlive = $true
            starts = 0; startedPid = 0; nextPid = 9001; lastCommandLine = ''; lastExePath = ''
            requests = (New-Object System.Collections.ArrayList)
        }
        if ($null -ne $Overrides) { foreach ($k in $Overrides.Keys) { $sim[$k] = $Overrides[$k] } }
        return $sim
    }
    $fxB = @{
        Http = {
            param($port, $method, $path, $token)
            $null = $fxSim.requests.Add("$method $path")
            switch ($path) {
                '/health' { return @{ status = [int]$fxSim.healthStatus; body = '{"ok":true}' } }
                '/api/state' {
                    $oc = if ($fxSim.openCalled) { 'true' } else { 'false' }
                    $oe = if ($fxSim.openEventsCalled) { 'true' } else { 'false' }
                    $body = '{"ok":true,"state":{"state":"' + [string]$fxSim.sState + '","openCalled":' + $oc + ',"openEventsCalled":' + $oe + '}}'
                    return @{ status = [int]$fxSim.stateStatus; body = $body }
                }
                '/api/perf-counters' {
                    if ($fxSim.perfOmitProcessId) {
                        $body = '{"ok":true,"hostInstanceId":"' + [string]$fxSim.perfHostInstanceId + '"}'
                    } else {
                        if ($null -ne $fxSim.perfPidOverride) { $pp = [int]$fxSim.perfPidOverride }
                        elseif (@($fxSim.liveOwners).Count -eq 1) { $pp = [int](@($fxSim.liveOwners)[0]) }
                        else { $pp = [int]$fxSim.startedPid }
                        $body = '{"ok":true,"hostInstanceId":"' + [string]$fxSim.perfHostInstanceId + '","processId":' + $pp + '}'
                    }
                    return @{ status = [int]$fxSim.perfStatus; body = $body }
                }
                '/api/open' {
                    if ([int]$fxSim.openStatus -eq 200) {
                        $fxSim.sState = 'Open'; $fxSim.openCalled = $true
                        if ($null -ne $fxSim.perfInstanceAfterOpen) { $fxSim.perfHostInstanceId = $fxSim.perfInstanceAfterOpen }
                    }
                    return @{ status = [int]$fxSim.openStatus; body = ('{"ok":true,"state":"' + [string]$fxSim.sState + '"}') }
                }
                '/api/open-events' {
                    if ([int]$fxSim.openEventsStatus -eq 200 -and -not $fxSim.neverReady) { $fxSim.sState = 'Ready'; $fxSim.openEventsCalled = $true }
                    return @{ status = [int]$fxSim.openEventsStatus; body = ('{"ok":true,"state":"' + [string]$fxSim.sState + '"}') }
                }
                '/api/close' {
                    if ([int]$fxSim.closeStatus -eq 200) { $fxSim.sState = 'Stopped'; $fxSim.openCalled = $false; $fxSim.openEventsCalled = $false }
                    return @{ status = [int]$fxSim.closeStatus; body = ('{"ok":true,"state":"' + [string]$fxSim.sState + '"}') }
                }
                '/api/shutdown' {
                    if ([int]$fxSim.shutdownStatus -eq 200) { $fxSim.sState = 'Stopped'; $fxSim.liveOwners = @(); $fxSim.portOwners = @() }
                    return @{ status = [int]$fxSim.shutdownStatus; body = '{"ok":true,"shutdownRequested":true}' }
                }
                default { return @{ status = 404; body = '{"ok":false}' } }
            }
        }
        StartHost = {
            param($exePath, $commandLine)
            $fxSim.starts = $fxSim.starts + 1
            $fxSim.lastExePath = $exePath
            $fxSim.lastCommandLine = $commandLine
            $fxSim.ownersAtStart = @($fxSim.liveOwners)
            $newPid = [int]$fxSim.nextPid
            $fxSim.startedPid = $newPid
            if ($fxSim.bindOnStart) {
                $fxSim.liveOwners = @($newPid)
                if ($null -ne $fxSim.portOwnerRaw) { $fxSim.portOwners = @([int]$fxSim.portOwnerRaw) } else { $fxSim.portOwners = @($newPid) }
            }
            $fxSim.sState = 'Stopped'; $fxSim.openCalled = $false; $fxSim.openEventsCalled = $false
            if ($null -ne $fxSim.sidecarAfterStart) { $fxSim.sidecarToken = $fxSim.sidecarAfterStart }
            return @{ pid = $newPid }
        }
        GetPortOwners = { param($portNumber) return @($fxSim.portOwners) }
        ListOwners = { param($exePath) return @(@($fxSim.liveOwners) | ForEach-Object { [pscustomobject]@{ pid = [int]$_; path = $exePath } }) }
        ListParentProcesses = { param($processName) return @([pscustomobject]@{ pid = 4242; name = $processName }) }
        ProcessRunning = { param($procId) if ([int]$procId -eq 4242) { return [bool]$fxSim.parentAlive }; return (@($fxSim.liveOwners) -contains [int]$procId) }
        ReadSidecar = { param($path) return $fxSim.sidecarToken }
        ReadLog = { param($path, $tail) return @() }
        Sleep = { param($ms) $fxClock.now = $fxClock.now.AddMilliseconds([double]$ms) }
        UtcNow = { return $fxClock.now }
        HashFile = { param($path) return $fxSim.sidecarToken }
    }

    # --- T1：Plan 零启动零写入（含 sidecar 字节不变）---
    $writeCalls = New-Object System.Collections.ArrayList
    $startCalls = New-Object System.Collections.ArrayList
    $httpCalls = New-Object System.Collections.ArrayList
    $fakeSidecar = ('a' * 64)
    $t1Boundary = @{
        ListOwners = { param($exePath) $null = $writeCalls; return @() }
        Http = { param($p, $m, $u, $t) $null = $httpCalls.Add("$m $u"); return @{ status = 0; body = '' } }
        StartHost = { param($exePath, $commandLine) $null = $startCalls.Add($commandLine); return @{ pid = 1234 } }
        ProcessRunning = { param($procId) return $false }
        ReadSidecar = { param($path) return $fakeSidecar }
        ReadLog = { param($path, $tail) return @() }
        Sleep = { param($ms) }
        UtcNow = { return [DateTime]::UtcNow }
        HashFile = { param($path) return $fakeSidecar }
    }
    $paths = Resolve-ExpectedPaths -B $t1Boundary -PowerControl $fakePowerControl -HostDirOverride '' -SidecarOverride '' -AuthOverride ''
    $argsT1 = New-CandidateLaunchArgs -Paths $paths -BindParentPid 4242 -BindParentProcess 'YeManCC' -ConfirmationToken $fakeSidecar
    $redactedT1 = Get-RedactedArgs -ArgList $argsT1
    Assert-That 'T1.plan-no-start' ($startCalls.Count -eq 0) "startCalls=$($startCalls.Count)"
    Assert-That 'T1.plan-no-http' ($httpCalls.Count -eq 0) "httpCalls=$($httpCalls.Count)"
    Assert-That 'T1.plan-no-write' ($writeCalls.Count -eq 0) "writeCalls=$($writeCalls.Count)"
    Assert-That 'T1.sidecar-bytes-unchanged' ($fakeSidecar.Length -eq 64 -and $fakeSidecar -match '^a{64}$') 'sidecar mutated'

    # --- T2：启动参数含 backend 且保留认证/父进程约束 + 脱敏 ---
    $joinedT2 = ($argsT1 -join ' ')
    Assert-That 'T2.has-fan-provider-backend' ($joinedT2 -match '--fan-provider backend') 'missing --fan-provider backend'
    Assert-That 'T2.has-real-backend' ($joinedT2 -match '--real-backend') 'missing --real-backend'
    Assert-That 'T2.has-parent-binding' ($joinedT2 -match '--parent-pid 4242' -or $joinedT2 -match '--parent-process') 'missing parent binding'
    Assert-That 'T2.has-session-token-file' ($joinedT2 -match '--session-token-file') 'missing --session-token-file'
    Assert-That 'T2.has-protocol-version-2' ($joinedT2 -match '--protocol-version 2') 'missing --protocol-version 2'
    Assert-That 'T2.has-write-confirmation' ($joinedT2 -match '--allow-hardware-writes' -and $joinedT2 -match '--confirm') 'missing write confirmation params'
    Assert-That 'T2.redaction-hides-token' ($redactedT1 -match '<redacted-token>' -and $redactedT1 -notmatch [regex]::Escape($fakeSidecar)) 'confirm token leaked'

    # --- T3：旧 owner 探测 401 → STOP（走同一 Invoke-DeviceFlow），零第二进程、零后续变更请求 ---
    $fxSim = & $fxNewSim @{ liveOwners = @(777); portOwners = @(777); sidecarToken = $fakeSidecar; stateStatus = 401 }
    $flow3 = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req3 = @($fxSim.requests)
    Assert-That 'T3.401-stops' ($flow3.exitCode -eq 10 -and $flow3.decision.decision -eq 'STOP_OWNER_UNKNOWN' -and $flow3.decision.reason -eq 'probe-not-authenticated:401') "exit=$($flow3.exitCode) decision=$($flow3.decision.decision) reason=$($flow3.decision.reason)"
    Assert-That 'T3.no-second-process' ($fxSim.starts -eq 0) "starts=$($fxSim.starts)"
    Assert-That 'T3.no-mutation-request' (@($req3 | Where-Object { $_ -match '^POST /api/(close|shutdown|open|open-events)$' }).Count -eq 0) "reqs=$($req3 -join '|')"

    # --- T4：live resident 旧实例 → 认证 close/shutdown 有序退场；sidecar 不变 ---
    $fxSim = & $fxNewSim @{ liveOwners = @(888); portOwners = @(888); sState = 'ready'; sidecarToken = $fakeSidecar }
    $owners4 = @(& $fxB.ListOwners 'X:\no\path\YeManFanHost.exe')
    $portOwners4 = @(& $fxB.GetPortOwners $script:FanHostDefaultPort)
    $probe4 = & $fxB.Http $script:FanHostDefaultPort 'GET' '/api/state' $fakeSidecar
    $decision4 = Resolve-DeviceDecision -Owners $owners4 -Probe $probe4 -PortOwners $portOwners4 -Instance @{ ok = $true; processId = 888; hostInstanceId = 'h-new' }
    Assert-That 'T4.live-resident-exit' ($decision4.decision -eq 'END_OLD_CONTROL_THEN_START') "decision=$($decision4.decision)"
    $t4Summary = [ordered]@{ steps = @() }
    $end4 = Invoke-EndOldControl -B $fxB -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -Summary $t4Summary -ExpectedPid 888 -ExpectedInstanceId 'h-new'
    $req4 = @($fxSim.requests)
    $idxClose4 = [array]::IndexOf($req4, 'POST /api/close')
    $idxShut4 = [array]::IndexOf($req4, 'POST /api/shutdown')
    Assert-That 'T4.authenticated-close-shutdown' ($end4.ok -and $idxClose4 -ge 0 -and $idxShut4 -gt $idxClose4) "ok=$($end4.ok) reqs=$($req4 -join '|')"
    Assert-That 'T4.sidecar-unchanged' ($fxSim.sidecarToken -eq $fakeSidecar) 'sidecar mutated'

    # --- T5：adopt 合同镜像（双向断言；标注为镜像 fanHost.ts，非真实 bridge 调用）---
    $mirror = @('suspended', 'resuming', 'resumed', 'ready', 'open', 'awaitingcontrol', 'starting', 'handshaking')
    $sameSet = (($script:LiveResidentFanStates | Sort-Object) -join ',') -eq (($mirror | Sort-Object) -join ',')
    Assert-That 'T5.live-state-mirror' $sameSet "tool=$($script:LiveResidentFanStates -join ',')"
    Assert-That 'T5.mirror-scope-documented' $true 'mirror of fanHost.ts isLiveResidentFanState; not a live bridge call'
    Assert-That 'T5.unknown-state-not-live' (-not (Test-LiveResidentState 'stopped')) 'stopped must not be adoptable'

    # --- T6：provider 证据解析（合成日志）---
    $synthetic = @(
        '{"eventName":"host.instance-started","processId":4321,"hostInstanceId":"h-abc","provider":"HC","source":"hc.device-callback"}',
        '{"eventName":"fan.provider.frozen","processId":4321,"hostInstanceId":"h-abc","provider":"backend","operation":"freeze","details":{"component":"YeManFanBackend.Asus.AsusFanCurveWriter@1"}}',
        '{"eventName":"hc.fan-dispatch.begin","processId":4321,"hostInstanceId":"h-abc","provider":"backend","source":"backend.asus-acpi-curve","operation":"ApplySoftwareCurve"}',
        '{"eventName":"hc.fan-dispatch.begin","processId":1111,"hostInstanceId":"h-other","provider":"backend","source":"backend.asus-acpi-curve","operation":"ApplySoftwareCurve"}'
    )
    $ev = Get-ProviderEvidence -Lines $synthetic -ProcessId 4321 -HostInstanceId 'h-abc'
    Assert-That 'T6.provider-frozen-backend' ($ev.providerFrozen.found -and $ev.providerFrozen.provider -eq 'backend') "frozen=$($ev.providerFrozen.provider)"
    Assert-That 'T6.dispatch-source-backend' ($ev.fanDispatch.found -and $ev.fanDispatch.source -eq 'backend.asus-acpi-curve') "source=$($ev.fanDispatch.source)"
    Assert-That 'T6.process-correlation' ($ev.fanDispatch.count -eq 1) "count=$($ev.fanDispatch.count)"
    $evHc = Get-ProviderEvidence -Lines @('{"eventName":"fan.provider.frozen","processId":4321,"hostInstanceId":"h-abc","provider":"HC"}') -ProcessId 4321 -HostInstanceId 'h-abc'
    Assert-That 'T6.hc-provider-not-accepted' (-not $evHc.backendProviderObserved) 'HC provider wrongly accepted'

    # --- T7：冷启动（无 owner）→ 唯一候选；仅 Open→OpenEvents 后到达真可 adopt ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9001 }
    $flow7 = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req7 = @($fxSim.requests)
    $idxOpen7 = [array]::IndexOf($req7, 'POST /api/open')
    $idxEvents7 = [array]::IndexOf($req7, 'POST /api/open-events')
    Assert-That 'T7.cold-start-single-candidate' ($flow7.exitCode -eq 0 -and $fxSim.starts -eq 1) "exit=$($flow7.exitCode) starts=$($fxSim.starts)"
    Assert-That 'T7.open-before-open-events' ($idxOpen7 -ge 0 -and $idxEvents7 -gt $idxOpen7) "reqs=$($req7 -join '|')"
    Assert-That 'T7.adoptable-after-init' ($flow7.bringUp.adoptable -and $flow7.bringUp.bindingVerified -and $flow7.bringUp.pidBound) "adoptable=$($flow7.bringUp.adoptable) binding=$($flow7.bringUp.bindingVerified) pidBound=$($flow7.bringUp.pidBound)"
    Assert-That 'T7.identity-bound' ($flow7.candidateIdentity.pid -eq 9001 -and $flow7.bringUp.perfPid -eq 9001) "pid=$($flow7.candidateIdentity.pid) perfPid=$($flow7.bringUp.perfPid)"
    Assert-That 'T7.no-acquire-enable' (@($req7 | Where-Object { $_ -match 'acquire|enable' }).Count -eq 0) "reqs=$($req7 -join '|')"
    Assert-That 'T7.sidecar-unchanged' ($fxSim.sidecarToken -eq $fakeSidecar) 'sidecar mutated'

    # --- T7b：最小反推 A —— 删掉 Open/OpenEvents 初始化，新流程门必须失败 ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9101 }
    $flow7b = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -Mutation 'skip-init' -ForSelfTest
    $req7b = @($fxSim.requests)
    Assert-That 'T7b.skip-init-not-adoptable' ($flow7b.exitCode -eq 10 -and -not $flow7b.bringUp.adoptable -and $flow7b.bringUp.reason -eq 'candidate-adopt-timeout') "exit=$($flow7b.exitCode) adoptable=$($flow7b.bringUp.adoptable) reason=$($flow7b.bringUp.reason)"
    Assert-That 'T7b.skip-init-no-init-request' (@($req7b | Where-Object { $_ -eq 'POST /api/open' -or $_ -eq 'POST /api/open-events' }).Count -eq 0) "reqs=$($req7b -join '|')"
    Assert-That 'T7b.skip-init-teardown' ($flow7b.bringUp.teardown.closeStatus -eq 200 -and $flow7b.bringUp.teardown.shutdownStatus -eq 200 -and $flow7b.bringUp.teardown.ownerZero) "teardown=$($flow7b.bringUp.teardown | ConvertTo-Json -Compress)"

    # --- T7c：实例错配 —— perf.processId ≠ 新 PID，默认必须拒绝（绑定检查承重）---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9201; perfPidOverride = 7777 }
    $flow7c = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    Assert-That 'T7c.pid-mismatch-rejected' ($flow7c.exitCode -eq 10 -and -not $flow7c.bringUp.adoptable -and -not $flow7c.bringUp.pidBound -and $flow7c.bringUp.perfPid -ne $flow7c.bringUp.startedPid) "exit=$($flow7c.exitCode) adoptable=$($flow7c.bringUp.adoptable) pidBound=$($flow7c.bringUp.pidBound) perfPid=$($flow7c.bringUp.perfPid) startedPid=$($flow7c.bringUp.startedPid)"

    # --- T7d：最小反推 B —— 去掉 PID 绑定检查后，同一错配被错误接受（证明该检查承重）---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9202; perfPidOverride = 7777 }
    $flow7d = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -Mutation 'skip-pid-binding' -ForSelfTest
    Assert-That 'T7d.mutation-accepts-mismatch' ($flow7d.exitCode -eq 0 -and $flow7d.bringUp.adoptable -and $flow7d.bringUp.pidBound -and $flow7d.bringUp.perfPid -ne $flow7d.bringUp.startedPid) "exit=$($flow7d.exitCode) adoptable=$($flow7d.bringUp.adoptable) perfPid=$($flow7d.bringUp.perfPid) startedPid=$($flow7d.bringUp.startedPid)"

    # --- T7e：身份变化 —— 启动后 sidecar 被替换，立即止步、无后续变更、不做 teardown ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9301; sidecarAfterStart = ('b' * 64) }
    $flow7e = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req7e = @($fxSim.requests)
    Assert-That 'T7e.sidecar-changed-halts' ($flow7e.exitCode -eq 10 -and -not $flow7e.bringUp.adoptable -and $flow7e.bringUp.reason -eq 'candidate-sidecar-changed') "exit=$($flow7e.exitCode) reason=$($flow7e.bringUp.reason)"
    Assert-That 'T7e.no-mutation-after-identity-change' (@($req7e | Where-Object { $_ -match '^POST /api/(open|close|shutdown|open-events)$' }).Count -eq 0) "reqs=$($req7e -join '|')"
    Assert-That 'T7e.no-teardown' ($null -eq $flow7e.bringUp.teardown) "teardown=$($flow7e.bringUp.teardown)"

    # --- T8：决策矩阵（可有序退出 = Live∪Stopped，且与可 adopt 分离）---
    $instOld = @{ ok = $true; processId = 1; hostInstanceId = 'h-old' }
    $dOrd = Resolve-DeviceDecision -Owners @([pscustomobject]@{ pid = 1 }) -Probe @{ status = 200; body = '{"state":"Stopped"}' } -PortOwners @(1) -Instance $instOld
    $dLive = Resolve-DeviceDecision -Owners @([pscustomobject]@{ pid = 1 }) -Probe @{ status = 200; body = '{"state":"ready"}' } -PortOwners @(1) -Instance $instOld
    $dUnauth = Resolve-DeviceDecision -Owners @([pscustomobject]@{ pid = 1 }) -Probe @{ status = 401; body = '{"ok":false}' } -PortOwners @(1) -Instance $instOld
    $dMulti = Resolve-DeviceDecision -Owners @(@{ pid = 1 }, @{ pid = 2 }) -Probe $null -PortOwners @(1) -Instance $null
    $dPort = Resolve-DeviceDecision -Owners @() -Probe $null -PortOwners @(999) -Instance $null
    $dWeird = Resolve-DeviceDecision -Owners @([pscustomobject]@{ pid = 1 }) -Probe @{ status = 200; body = '{"state":"FaultLocked"}' } -PortOwners @(1) -Instance $instOld
    $dStart = Resolve-DeviceDecision -Owners @() -Probe $null -PortOwners @() -Instance $null
    $dNoInstance = Resolve-DeviceDecision -Owners @([pscustomobject]@{ pid = 1 }) -Probe @{ status = 200; body = '{"state":"ready"}' } -PortOwners @(1) -Instance @{ ok = $false; processId = 0; hostInstanceId = $null; reason = 'instance-missing-instanceid' }
    Assert-That 'T8.stopped-orderly-exit' ($dOrd.decision -eq 'END_OLD_CONTROL_THEN_START') "decision=$($dOrd.decision)"
    Assert-That 'T8.live-orderly-exit' ($dLive.decision -eq 'END_OLD_CONTROL_THEN_START') "decision=$($dLive.decision)"
    Assert-That 'T8.401-stops' ($dUnauth.decision -eq 'STOP_OWNER_UNKNOWN' -and $dUnauth.reason -eq 'probe-not-authenticated:401') "decision=$($dUnauth.decision) reason=$($dUnauth.reason)"
    Assert-That 'T8.multi-owner-stops' ($dMulti.decision -eq 'STOP_OWNER_UNKNOWN' -and $dMulti.reason -eq 'multi-owner') "decision=$($dMulti.decision) reason=$($dMulti.reason)"
    Assert-That 'T8.foreign-port-stops' ($dPort.decision -eq 'STOP_OWNER_UNKNOWN' -and $dPort.reason -eq 'foreign-port-listener') "decision=$($dPort.decision) reason=$($dPort.reason)"
    Assert-That 'T8.odd-state-stops' ($dWeird.decision -eq 'STOP_OWNER_UNKNOWN' -and $dWeird.reason -eq 'state-not-orderly-exit:FaultLocked') "decision=$($dWeird.decision) reason=$($dWeird.reason)"
    Assert-That 'T8.no-owner-starts' ($dStart.decision -eq 'START_CANDIDATE') "decision=$($dStart.decision)"
    Assert-That 'T8.unauthenticated-instance-stops' ($dNoInstance.decision -eq 'STOP_OWNER_UNKNOWN' -and $dNoInstance.reason -eq 'instance-not-authenticated:instance-missing-instanceid') "decision=$($dNoInstance.decision) reason=$($dNoInstance.reason)"

    # --- T9：端到端 —— 认证 Stopped 旧实例真实退场归零后，唯一候选冷启动上位 ---
    $fxSim = & $fxNewSim @{ liveOwners = @(7001); portOwners = @(7001); sState = 'Stopped'; sidecarToken = $fakeSidecar; nextPid = 9401 }
    $flow9 = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req9 = @($fxSim.requests)
    $iClose9 = [array]::IndexOf($req9, 'POST /api/close')
    $iShut9 = [array]::IndexOf($req9, 'POST /api/shutdown')
    $iOpen9 = [array]::IndexOf($req9, 'POST /api/open')
    $iEv9 = [array]::IndexOf($req9, 'POST /api/open-events')
    Assert-That 'T9.old-stopped-exit-then-single-candidate' ($flow9.exitCode -eq 0 -and $fxSim.starts -eq 1) "exit=$($flow9.exitCode) starts=$($fxSim.starts)"
    Assert-That 'T9.orderly-order' ($iClose9 -ge 0 -and $iShut9 -gt $iClose9 -and $iOpen9 -gt $iShut9 -and $iEv9 -gt $iOpen9) "reqs=$($req9 -join '|')"
    Assert-That 'T9.owners-zero-before-start' (@($fxSim.ownersAtStart).Count -eq 0) "ownersAtStart=$(@($fxSim.ownersAtStart) -join ',')"
    Assert-That 'T9.candidate-bound' ($flow9.bringUp.adoptable -and $flow9.candidateIdentity.pid -eq 9401) "adoptable=$($flow9.bringUp.adoptable) pid=$($flow9.candidateIdentity.pid)"

    # --- T10a：旧实例 close 返回 401 → 止步，不发 shutdown、不起候选 ---
    $fxSim = & $fxNewSim @{ liveOwners = @(7003); portOwners = @(7003); sState = 'ready'; sidecarToken = $fakeSidecar; closeStatus = 401; nextPid = 9501 }
    $flow10a = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req10a = @($fxSim.requests)
    Assert-That 'T10a.close-401-halts' ($flow10a.exitCode -eq 10 -and $flow10a.endOld.haltedAt -eq 'close' -and $flow10a.endOld.reason -eq 'close-401-auth-failed') "exit=$($flow10a.exitCode) haltedAt=$($flow10a.endOld.haltedAt) reason=$($flow10a.endOld.reason)"
    Assert-That 'T10a.no-shutdown-no-start' (@($req10a | Where-Object { $_ -eq 'POST /api/shutdown' }).Count -eq 0 -and $fxSim.starts -eq 0) "reqs=$($req10a -join '|') starts=$($fxSim.starts)"

    # --- T10b：Stop 动作下 close 401 同样止步，不发 shutdown ---
    $fxSim = & $fxNewSim @{ liveOwners = @(7004); portOwners = @(7004); sState = 'ready'; sidecarToken = $fakeSidecar; closeStatus = 401 }
    $flow10b = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Stop' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 0 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req10b = @($fxSim.requests)
    Assert-That 'T10b.stop-close-401-halts' ($flow10b.exitCode -eq 10 -and $flow10b.endOld.haltedAt -eq 'close') "exit=$($flow10b.exitCode) haltedAt=$($flow10b.endOld.haltedAt)"
    Assert-That 'T10b.stop-no-shutdown' (@($req10b | Where-Object { $_ -eq 'POST /api/shutdown' }).Count -eq 0) "reqs=$($req10b -join '|')"

    # --- T11：真实 Windows argv 边界（含空格路径）→ 子进程读回逐 token 全等 ---
    $t11Dir = Join-Path ([System.IO.Path]::GetTempPath()) ('fan-argv-' + [Guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $t11Dir -Force
    $recScript = Join-Path $t11Dir 'recorder.ps1'
    $recOut = Join-Path $t11Dir 'argv.json'
    $recBody = @'
param()
$argv = @($args)
[IO.File]::WriteAllText($env:FAN_ARGV_OUT, ($argv | ConvertTo-Json -Compress))
'@
    [IO.File]::WriteAllText($recScript, $recBody)
    $hostArgs11 = @(
        '--real-backend',
        '--hc-assembly', 'C:\SOFT\YeMan\Power Control\handheldcompanion-runtime\HandheldCompanion.dll',
        '--hc-runtime-root', 'C:\SOFT\YeMan\Power Control\handheldcompanion-runtime',
        '--session-token-file', 'C:\SOFT\YeMan\Power Control\fan-host\YeManFanHost.session',
        '--authorization', 'C:\SOFT\YeMan\Power Control\fan-host\YeManFanHost.authorization.md',
        '--fan-provider', 'backend'
    )
    $selfExe = [System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
    $recCmd = Join-WindowsCommandLine (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $recScript) + $hostArgs11)
    try {
        $env:FAN_ARGV_OUT = $recOut
        $null = Start-Process -FilePath $selfExe -ArgumentList $recCmd -PassThru -WindowStyle Hidden -Wait
        $observed = @()
        if (Test-Path -LiteralPath $recOut) {
            $rawJson = Get-Content -LiteralPath $recOut -Raw
            if (-not [string]::IsNullOrWhiteSpace($rawJson)) {
                # PS5.1 把 JSON 数组作为一个不可枚举对象返回，pwsh 7 会枚举；显式 foreach 归一化，保证双宿主同一结果。
                $parsedArgv = $rawJson | ConvertFrom-Json
                foreach ($token in $parsedArgv) { $observed += $token }
            }
        }
        $sep = [string][char]1
        $tokensEqual = ($observed.Count -eq $hostArgs11.Count) -and (($observed -join $sep) -eq ($hostArgs11 -join $sep))
        Assert-That 'T11.argv-roundtrip-equal' $tokensEqual "count=$($observed.Count) observed=$($observed -join '|')"
    } finally {
        Remove-Item Env:FAN_ARGV_OUT -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $t11Dir -Recurse -Force -ErrorAction SilentlyContinue
    }

    # --- T12a：候选 open 初始化失败(500) → 对本工具已启动实例做明确有序退场 ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9601; openStatus = 500 }
    $flow12a = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    Assert-That 'T12a.open-500-fails' ($flow12a.exitCode -eq 10 -and -not $flow12a.bringUp.ok -and $flow12a.bringUp.reason -eq 'candidate-open-status-500') "exit=$($flow12a.exitCode) reason=$($flow12a.bringUp.reason)"
    Assert-That 'T12a.teardown-orderly' ($flow12a.bringUp.teardown.closeStatus -eq 200 -and $flow12a.bringUp.teardown.shutdownStatus -eq 200 -and $flow12a.bringUp.teardown.ownerZero -and $fxSim.starts -eq 1) "teardown=$($flow12a.bringUp.teardown | ConvertTo-Json -Compress) starts=$($fxSim.starts)"

    # --- T12b：初始化失败后又遇关闭 401 → teardown 止步、绝无无认证 shutdown ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9602; openStatus = 500; closeStatus = 401 }
    $flow12b = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req12b = @($fxSim.requests)
    Assert-That 'T12b.teardown-halts-at-close' ($flow12b.exitCode -eq 10 -and $flow12b.bringUp.teardown.haltedAt -eq 'close' -and $flow12b.bringUp.teardown.reason -eq 'teardown-halted-at-close:close-401-auth-failed') "exit=$($flow12b.exitCode) teardown=$($flow12b.bringUp.teardown | ConvertTo-Json -Compress)"
    Assert-That 'T12b.no-unauthenticated-shutdown' (@($req12b | Where-Object { $_ -eq 'POST /api/shutdown' }).Count -eq 0) "reqs=$($req12b -join '|')"

    # --- T13：生产禁则 —— 非 -SelfTest 传 -Mutation 立即 BLOCKED，零启动零请求 ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9701 }
    $flow13 = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -Mutation 'skip-init'
    Assert-That 'T13.mutation-forbidden-outside-selftest' ($flow13.exitCode -eq 10 -and $flow13.blockedReason -match '仅允许在 -SelfTest') "exit=$($flow13.exitCode) blocked=$($flow13.blockedReason)"
    Assert-That 'T13.no-side-effects' ($fxSim.starts -eq 0 -and @($fxSim.requests).Count -eq 0) "starts=$($fxSim.starts) reqs=$(@($fxSim.requests) -join '|')"

    # --- T14（§9 S8-01）：HTTP.sys 正例 —— raw TCP port-owner=4 只作诊断，不作身份；认证实例身份成立即 adopt ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9801; portOwnerRaw = 4 }
    $flow14 = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $portDiag14 = @($flow14.bringUp.portOwnerDiagnostic)
    Assert-That 'T14.raw-owner-not-identity' ($flow14.exitCode -eq 0 -and $fxSim.starts -eq 1 -and $flow14.bringUp.adoptable -and $flow14.bringUp.identityVerified) "exit=$($flow14.exitCode) starts=$($fxSim.starts) adoptable=$($flow14.bringUp.adoptable) identity=$($flow14.bringUp.identityVerified)"
    Assert-That 'T14.port-owner-diagnostic-only' ($portDiag14.Count -eq 1 -and $portDiag14[0] -eq 4 -and $flow14.bringUp.startedPid -eq 9801) "diag=$($portDiag14 -join ',') startedPid=$($flow14.bringUp.startedPid)"

    # --- T15a（§9 S8-01）：空 hostInstanceId → 身份未证，不 adopt、零 teardown、零变更请求 ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9802; perfHostInstanceId = '' }
    $flow15a = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req15a = @($fxSim.requests)
    Assert-That 'T15a.empty-instance-id-not-adopted' ($flow15a.exitCode -eq 10 -and -not $flow15a.bringUp.adoptable -and $flow15a.bringUp.reason -eq 'candidate-identity-timeout') "exit=$($flow15a.exitCode) adoptable=$($flow15a.bringUp.adoptable) reason=$($flow15a.bringUp.reason)"
    Assert-That 'T15a.no-teardown-no-mutation' ($null -eq $flow15a.bringUp.teardown -and @($req15a | Where-Object { $_ -match '^POST /api/(open|close|shutdown|open-events)$' }).Count -eq 0) "teardown=$($flow15a.bringUp.teardown) reqs=$($req15a -join '|')"

    # --- T15c（§9 S8-01）：旧应答者 PID 与认证 perf 不符（foreign）→ STOP，零 close/shutdown ---
    $fxSim = & $fxNewSim @{ liveOwners = @(8001); portOwners = @(8001); sState = 'ready'; sidecarToken = $fakeSidecar; perfPidOverride = 5555 }
    $flow15c = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req15c = @($fxSim.requests)
    Assert-That 'T15c.foreign-responder-stops' ($flow15c.exitCode -eq 10 -and $flow15c.decision.decision -eq 'STOP_OWNER_UNKNOWN' -and $flow15c.decision.reason -like 'instance-not-authenticated:instance-pid-mismatch*') "exit=$($flow15c.exitCode) decision=$($flow15c.decision.decision) reason=$($flow15c.decision.reason)"
    Assert-That 'T15c.no-close-no-shutdown-no-start' (@($req15c | Where-Object { $_ -match '^POST /api/(close|shutdown|open|open-events)$' }).Count -eq 0 -and $fxSim.starts -eq 0) "reqs=$($req15c -join '|') starts=$($fxSim.starts)"

    # --- T15d（§9 S8-02）：Open 后实例 ID 轮换 → adopt 阶段立即止步、零 teardown ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9803; perfInstanceAfterOpen = 'h-rotated' }
    $flow15d = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req15d = @($fxSim.requests)
    Assert-That 'T15d.instance-changed-halts' ($flow15d.exitCode -eq 10 -and -not $flow15d.bringUp.adoptable -and $flow15d.bringUp.reason -eq 'candidate-instance-changed:instance-id-changed') "exit=$($flow15d.exitCode) reason=$($flow15d.bringUp.reason)"
    Assert-That 'T15d.no-teardown-after-change' ($null -eq $flow15d.bringUp.teardown -and @($req15d | Where-Object { $_ -match '^POST /api/(close|shutdown)$' }).Count -eq 0) "teardown=$($flow15d.bringUp.teardown) reqs=$($req15d -join '|')"

    # --- T15e（§9 S8-01）：perf 缺 processId → 身份未证，不 adopt、零 teardown ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9804; perfOmitProcessId = $true }
    $flow15e = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req15e = @($fxSim.requests)
    Assert-That 'T15e.missing-processid-not-adopted' ($flow15e.exitCode -eq 10 -and $flow15e.bringUp.reason -eq 'candidate-identity-timeout') "exit=$($flow15e.exitCode) reason=$($flow15e.bringUp.reason)"
    Assert-That 'T15e.no-teardown-no-mutation' ($null -eq $flow15e.bringUp.teardown -and @($req15e | Where-Object { $_ -match '^POST /api/(open|close|shutdown|open-events)$' }).Count -eq 0) "teardown=$($flow15e.bringUp.teardown) reqs=$($req15e -join '|')"

    # --- T16a（§9 S8-01）：state 错误状态码但 body 含 Ready → 不得算成功（状态码是一等准入）---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9805; stateStatus = 500 }
    $flow16a = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    Assert-That 'T16a.error-status-ready-body-rejected' ($flow16a.exitCode -eq 10 -and -not $flow16a.bringUp.adoptable -and $flow16a.bringUp.reason -eq 'candidate-adopt-timeout') "exit=$($flow16a.exitCode) adoptable=$($flow16a.bringUp.adoptable) reason=$($flow16a.bringUp.reason)"
    Assert-That 'T16a.identity-teardown-still-orderly' ($flow16a.bringUp.teardown.closeStatus -eq 200 -and $flow16a.bringUp.teardown.shutdownStatus -eq 200) "teardown=$($flow16a.bringUp.teardown | ConvertTo-Json -Compress)"

    # --- T16b（§9 S8-01）：perf 错误状态码 → 身份未证，不 adopt、零 teardown ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9806; perfStatus = 500 }
    $flow16b = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req16b = @($fxSim.requests)
    Assert-That 'T16b.perf-error-status-rejected' ($flow16b.exitCode -eq 10 -and $flow16b.bringUp.reason -eq 'candidate-identity-timeout') "exit=$($flow16b.exitCode) reason=$($flow16b.bringUp.reason)"
    Assert-That 'T16b.no-teardown-no-mutation' ($null -eq $flow16b.bringUp.teardown -and @($req16b | Where-Object { $_ -match '^POST /api/(open|close|shutdown|open-events)$' }).Count -eq 0) "teardown=$($flow16b.bringUp.teardown) reqs=$($req16b -join '|')"

    # --- T16c（§9 S8-02）：Open 401 → 立即止步，零后续变更、零清理 ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9807; openStatus = 401 }
    $flow16c = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req16c = @($fxSim.requests)
    Assert-That 'T16c.open-401-halts' ($flow16c.exitCode -eq 10 -and $flow16c.bringUp.reason -eq 'candidate-open-401' -and $null -eq $flow16c.bringUp.teardown) "exit=$($flow16c.exitCode) reason=$($flow16c.bringUp.reason) teardown=$($flow16c.bringUp.teardown)"
    Assert-That 'T16c.no-close-shutdown' (@($req16c | Where-Object { $_ -match '^POST /api/(close|shutdown)$' }).Count -eq 0) "reqs=$($req16c -join '|')"

    # --- T16d（§9 S8-02）：OpenEvents 401 → 立即止步，零后续变更、零清理 ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9808; openEventsStatus = 401 }
    $flow16d = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req16d = @($fxSim.requests)
    Assert-That 'T16d.open-events-401-halts' ($flow16d.exitCode -eq 10 -and $flow16d.bringUp.reason -eq 'candidate-open-events-401' -and $null -eq $flow16d.bringUp.teardown) "exit=$($flow16d.exitCode) reason=$($flow16d.bringUp.reason) teardown=$($flow16d.bringUp.teardown)"
    Assert-That 'T16d.no-close-shutdown' (@($req16d | Where-Object { $_ -match '^POST /api/(close|shutdown)$' }).Count -eq 0) "reqs=$($req16d -join '|')"

    # --- T17（§9 S8-02）：候选始终未绑定端口（未证身份）→ 保留 startedPid/失败阶段，零清理 ---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9809; bindOnStart = $false }
    $flow17 = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -ForSelfTest
    $req17 = @($fxSim.requests)
    Assert-That 'T17.binding-unproven-retains-pid' ($flow17.exitCode -eq 10 -and -not $flow17.bringUp.identityVerified -and $flow17.bringUp.reason -eq 'candidate-identity-timeout' -and $flow17.bringUp.startedPid -eq 9809) "exit=$($flow17.exitCode) reason=$($flow17.bringUp.reason) startedPid=$($flow17.bringUp.startedPid)"
    Assert-That 'T17.no-cleanup-to-unknown-responder' ($null -eq $flow17.bringUp.teardown -and @($req17 | Where-Object { $_ -match '^POST /api/(open|close|shutdown|open-events)$' }).Count -eq 0) "teardown=$($flow17.bringUp.teardown) reqs=$($req17 -join '|')"

    # --- T18（§9 一处反推）：mutation 去掉实例 ID 准入 → 空实例 ID 被错误 adopt（证明该准入承重）---
    $fxSim = & $fxNewSim @{ sidecarToken = $fakeSidecar; nextPid = 9810; perfHostInstanceId = '' }
    $flow18 = Invoke-DeviceFlow -B $fxB -Paths $paths -Action 'Start' -PortNumber $script:FanHostDefaultPort -Token $fakeSidecar -BindParentPid 4242 -BindParentProcess 'YeManCC' -ReadyTimeoutSec 4 -TailLines 10 -FanStateDir 'X:\no\state' -Summary $summary -Mutation 'skip-identity-admission' -ForSelfTest
    Assert-That 'T18.mutation-admits-empty-instance' ($flow18.exitCode -eq 0 -and $flow18.bringUp.adoptable -and $flow18.bringUp.identityVerified) "exit=$($flow18.exitCode) adoptable=$($flow18.bringUp.adoptable) reason=$($flow18.bringUp.reason)"

    Write-Host '----- SelfTest T1-T18 -----'
    $script:SelfTestResults | ForEach-Object { Write-Host $_ }
    if ($script:SelfTestFailures.Count -gt 0) {
        Write-Host "SelfTest FAILED: $($script:SelfTestFailures.Count) case(s)"
        return 2
    }
    Write-Host 'SelfTest PASSED: T1-T18 (fake process/HTTP/clock boundary + real argv recorder + §9 S8-01..03 instance identity/teardown/mutation)'
    return 0
}

try {
    if ($SelfTest) {
        $summary.mode = 'SelfTest'
        $exitCode = Invoke-SelfTest
        $summary.exitCode = $exitCode
        Write-Host ($summary | ConvertTo-Json -Depth 10 -Compress)
        exit $exitCode
    }

    # -------------------------------------------------------------------
    # 解析路径（Plan/Device 共用；只读）
    # -------------------------------------------------------------------
    $B = New-RealBoundary
    $paths = Resolve-ExpectedPaths -B $B -PowerControl $PowerControlDir -HostDirOverride $HostRoot -SidecarOverride $SessionTokenPath -AuthOverride $AuthorizationPath
    $summary.paths = $paths

    $exeExists = Test-Path -LiteralPath $paths.hostExecutable -PathType Leaf
    $assemblyExists = Test-Path -LiteralPath $paths.hcAssemblyPath -PathType Leaf
    $sidecarExists = Test-Path -LiteralPath $paths.sessionTokenPath -PathType Leaf
    Add-Check 'host-executable-exists' $exeExists $paths.hostExecutable
    Add-Check 'hc-assembly-exists' $assemblyExists $paths.hcAssemblyPath
    Add-Check 'sidecar-exists' $sidecarExists $paths.sessionTokenPath

    # 只读读取 sidecar（仅用于判定格式成立与派生 confirm；绝不回显）
    $sidecarRaw = & $B.ReadSidecar $paths.sessionTokenPath
    $token = if ($null -ne $sidecarRaw) { $sidecarRaw.Trim() } else { '' }
    $tokenValid = Test-SessionToken $token
    Add-Check 'sidecar-token-format-valid' $tokenValid 'present-and-64-hex'

    $owners = @(& $B.ListOwners $paths.hostExecutable)
    Add-Check 'host-owner-count' ($owners.Count -le 1) "count=$($owners.Count)"

    $gate = Get-LoggingGateState -StateRoot $FanStateDir
    Add-Check 'detailed-logging-gate-enabled' ([bool]$gate.enabled) $gate

    # 候选参数（脱敏后仅登记，不落明文）
    $bindPid = if ($ParentPid -gt 0) { $ParentPid } else { 0 }
    $candidateArgs = New-CandidateLaunchArgs -Paths $paths -BindParentPid $bindPid -BindParentProcess $ParentProcess -ConfirmationToken $token -ReadOnly:$ReadOnlyHost
    $summary.candidateArgsRedacted = Get-RedactedArgs -ArgList $candidateArgs

    # -------------------------------------------------------------------
    # Plan：只读输出
    # -------------------------------------------------------------------
    if ($Mode -eq 'Plan') {
        Write-Host '===== ROG 候选 Backend 测试准备（Plan / 只读）====='
        Write-Host ("PowerControlDir : {0}" -f $paths.powerControlDir)
        Write-Host ("HostDirectory   : {0}" -f $paths.hostDirectory)
        Write-Host ("HostExecutable  : {0}  (exists={1})" -f $paths.hostExecutable, $exeExists)
        Write-Host ("HC assembly     : {0}  (exists={1}, {2}, {3})" -f $paths.hcAssemblyPath, $assemblyExists, $paths.runtimeResolution, $paths.v3Adapter)
        Write-Host ("Session sidecar : {0}  (exists={1}, valid={2})" -f $paths.sessionTokenPath, $sidecarExists, $tokenValid)
        Write-Host ("Authorization   : {0}" -f $paths.authorizationPath)
        Write-Host ("Current owner   : {0} YeManFanHost process(es) matching this image" -f $owners.Count)
        if ($owners.Count -gt 0) { Write-Host ("   owner pid(s) : {0}" -f (($owners | ForEach-Object { $_.pid }) -join ', ')) }
        Write-Host ("Logging gate    : enabled={0} flag={1}" -f $gate.enabled, $gate.flagPath)
        if (-not $gate.enabled) {
            Write-Host '  [提示] 详细日志门关闭：provider 证据（fan.provider.frozen / hc.fan-dispatch.*）不会落到 runtime log。'
            Write-Host '         请在应用内既有"详细日志"开关开启后，再由用户执行设备测试（本工具不自行写 flag）。'
        }
        Write-Host ("Candidate args  : {0}" -f $summary.candidateArgsRedacted)
        Write-Host ("Parent binding  : {0}" -f $(if ($bindPid -gt 0) { "pid=$bindPid" } else { "process=$ParentProcess (Device Start 会解析唯一运行中的 $ParentProcess)" }))
        Write-Host 'Plan 未启动任何 Host、未发起 HTTP、未写入任何位置。'
        Show-RogChecklist
        Add-Check 'plan-zero-start' $true 'no host process started'
        Add-Check 'plan-zero-http' $true 'no HTTP issued'
        Add-Check 'plan-zero-write' $true 'no file written (unless -WriteEvidence)'
    }
    else {
        # ---------------------------------------------------------------
        # Device：显式设备模式
        # ---------------------------------------------------------------
        if ($Port -ne $script:FanHostDefaultPort) {
            $summary.blockedReason = "Device 模式必须使用默认端口 $($script:FanHostDefaultPort)（页面按该端口采纳宿主），收到 -Port=$Port。"
            Add-Check 'device-port-default' $false $summary.blockedReason
            $exitCode = 10
        }
        elseif (-not $exeExists -or -not $assemblyExists) {
            $summary.blockedReason = '候选 Host 映像或 HC assembly 不存在。'
            Add-Check 'device-payload-present' $false $summary.blockedReason
            $exitCode = 10
        }
        elseif (-not $tokenValid) {
            $summary.blockedReason = '会话 sidecar 缺失或格式无效：无法在不轮换凭据的前提下绑定 owner。'
            Add-Check 'device-sidecar-valid' $false $summary.blockedReason
            $exitCode = 10
        }
        else {
            # §8.2①：Device Start / 有序 Stop / Status 全部走同一入口 Invoke-DeviceFlow
            # （与 -SelfTest 共用完全相同的流程；生产分支不再另写"该调哪些 API"的镜像）。
            $flow = Invoke-DeviceFlow -B $B -Paths $paths -Action $Action -PortNumber $Port -Token $token `
                -BindParentPid $bindPid -BindParentProcess $ParentProcess -ReadyTimeoutSec $ReadyTimeoutSec `
                -TailLines $TailLines -FanStateDir $FanStateDir -Summary $summary -Mutation $Mutation -ReadOnlyHost:$ReadOnlyHost
            $summary.deviceFlow = [ordered]@{
                action = $flow.action; decision = $(if ($null -ne $flow.decision) { $flow.decision.decision } else { $null })
                exitCode = $flow.exitCode; blockedReason = $flow.blockedReason
                candidateIdentity = $flow.candidateIdentity
            }

            if ($flow.exitCode -ne 0) {
                $summary.blockedReason = $flow.blockedReason
                $exitCode = $flow.exitCode
            }
            elseif ($Action -eq 'Start') {
                $id = $flow.candidateIdentity
                Write-Host ("候选 Host 已启动并可被采纳：pid={0} state={1} hostInstanceId={2} perf.processId={3}" -f $id.pid, $id.state, $id.hostInstanceId, $id.perfPid)
                Write-Host '注意：provider=backend 需在页面首次写入后才可观测；本工具不宣称恢复完成。'
            }
            elseif ($Action -eq 'Stop') {
                if ($null -ne $flow.decision -and $flow.decision.decision -eq 'START_CANDIDATE') {
                    Write-Host '当前无匹配 owner；无需关闭。'
                } else {
                    Write-Host '候选已按既有认证路径关闭；owner 归零。正常路径继续默认 HC。'
                }
            }
            else {
                if ($null -ne $flow.providerEvidence) {
                    Write-Host ("provider 证据：frozen.provider={0} dispatch.source={1} (logGateEnabled={2})" -f `
                        $flow.providerEvidence.providerFrozen.provider, $flow.providerEvidence.fanDispatch.source, $gate.enabled)
                } else {
                    Write-Host '无法读取 /api/perf-counters：provider 证据不可关联（可能无存活 Host 或未认证）。'
                }
            }
        }
        Show-RogChecklist
    }
}
catch {
    $summary.error = Protect-SensitiveText $_.Exception.Message
    if ($exitCode -eq 0) { $exitCode = 3 }
}

$summary.exitCode = $exitCode
$summary.finishedAtUtc = [DateTime]::UtcNow

# 落盘策略：Device 恒写；Plan 仅 -WriteEvidence 时写。
$shouldWrite = ($Mode -eq 'Device') -or ($WriteEvidence -and $Mode -eq 'Plan')
if ($shouldWrite -and -not $SelfTest) {
    try {
        New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
        $json = Protect-SensitiveText (($summary | ConvertTo-Json -Depth 20))
        Set-Content -LiteralPath (Join-Path $EvidenceDir 'rog-backend-test-prep.json') -Value $json -Encoding UTF8
        Write-Host ("证据已写入：{0}" -f (Join-Path $EvidenceDir 'rog-backend-test-prep.json'))
    } catch {
        Write-Warning (Protect-SensitiveText $_.Exception.Message)
        if ($exitCode -eq 0) { $exitCode = 3 }
    }
}

Write-Host (Protect-SensitiveText (($summary | ConvertTo-Json -Depth 20 -Compress)))
exit $exitCode
