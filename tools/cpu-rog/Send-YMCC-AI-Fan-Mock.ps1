# Strict mock file protocol only. No Fan HTTP, EC, hardware writes, settings or hand-filled ACK.
function Get-RogArgv([string]$Line) { Initialize-RogNative; return [RogPortable.Native]::Argv($Line) }
function Get-RogFileTime([int]$ProcessId) { Initialize-RogNative; return [RogPortable.Native]::Creation([uint32]$ProcessId) }
function Get-RogLocalAppData { return [Environment]::GetFolderPath('LocalApplicationData') }
function Get-RogFanNamespace($Options,$Row) {
    if (-not $Options.SessionId) { throw 'FAN_SESSION_REQUIRED' }
    $argv=@(Get-RogArgv ([string]$Row.CommandLine))
    if ($argv.Count -lt 3 -or [IO.Path]::GetFullPath($argv[0]) -ine $Options.ExePath) { throw 'CLI_TARGET_IDENTITY_UNVERIFIED' }
    $fanValues=@(); $isolatedValues=@()
    for ($i=1;$i -lt $argv.Count;$i++) {
        $arg=$argv[$i]
        if ($arg -clike '--ai-fan-mock-session*' -and $arg -cne '--ai-fan-mock-session') { throw 'FAN_CLI_SESSION_AMBIGUOUS' }
        if ($arg -clike '--ai-cpu-isolated-session*' -and $arg -cne '--ai-cpu-isolated-session') { throw 'PAIRED_CLI_SESSION_AMBIGUOUS' }
        if ($arg -ceq '--ai-fan-mock-session' -or $arg -ceq '--ai-cpu-isolated-session') {
            if ($i+1 -ge $argv.Count) { throw 'FAN_CLI_SESSION_MISSING_VALUE' }
            $value=$argv[++$i]
            if ($arg -ceq '--ai-fan-mock-session') { $fanValues+=@($value) } else { $isolatedValues+=@($value) }
        }
    }
    if ($fanValues.Count -ne 1 -or $fanValues[0] -cne $Options.SessionId) { throw 'CAPABILITY_UNVERIFIED' }
    if ($isolatedValues.Count -gt 1 -or ($isolatedValues.Count -eq 1 -and $isolatedValues[0] -cne $Options.SessionId)) { throw 'PAIRED_CLI_SESSION_MISMATCH' }
    if ($isolatedValues.Count -eq 1) {
        $base=[IO.Path]::GetDirectoryName($Options.ExePath)
        $relative='ai-cpu-sessions\'+$Options.SessionId+'\shared\ai-fan-sessions\'+$Options.SessionId+'\fan-host'
    } else {
        $base=Get-RogLocalAppData
        if (-not $base -or -not [IO.Path]::IsPathRooted($base)) { throw 'LOCAL_APPDATA_NAMESPACE_UNVERIFIED' }
        $relative='YeManCC\ai-fan-sessions\'+$Options.SessionId+'\fan-host'
    }
    $path=[IO.Path]::GetFullPath((Join-Path $base $relative)); Assert-RogNoReparse $path
    return @{path=$path;pairedIsolated=($isolatedValues.Count -eq 1);namespaceDerivedFromExactCli=$true}
}
function Assert-RogZeroWrite($Evidence) {
    if (-not (Test-RogFalse $Evidence.hardwareWritesEnabled) -or -not (Test-RogFalse $Evidence.hardwareWritesObserved) -or
        -not (Test-RogTrue $Evidence.mockZeroHardwareEvidence) -or $Evidence.hostMode -cne 'mock-handshake' -or
        [string]$Evidence.protocolVersion -cne '2' -or $Evidence.mockControlEnabled -isnot [bool]) { throw 'FAN_ZERO_PHYSICAL_WRITE_READBACK_UNVERIFIED' }
}
function Test-RogSequence($Value) { return (($Value -is [int] -or $Value -is [long]) -and $Value -ge 0 -and $Value -le 9007199254740991L) }
function Assert-RogFanHost($Options,$Evidence) {
    if (-not (Test-RogSequence $Evidence.hostPid) -or $Evidence.hostPid -lt 1 -or $Evidence.hostPid -gt 2147483647 -or
        $Evidence.hostCreationTime100ns -isnot [string] -or $Evidence.hostCreationTime100ns -notmatch '^[1-9][0-9]{0,18}$' -or
        -not (Test-RogTrue $Evidence.runtimeVerified)) { throw 'FAN_HOST_RUNTIME_UNVERIFIED' }
    $hostRow=Get-RogProcessRow ([int]$Evidence.hostPid)
    $hostPath=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($Options.ExePath))) 'PowerControl\fan-host-v2\YeManFanHost.exe'
    Assert-RogNoReparse $hostPath
    if ($null -eq $hostRow -or -not $hostRow.ExecutablePath -or [IO.Path]::GetFullPath($hostRow.ExecutablePath) -ine $hostPath -or
        $hostRow.ParentProcessId -ne $Options.TargetPid) { throw 'FAN_HOST_PATH_OR_PARENT_CHANGED' }
    if ((Get-RogOwnerSid $hostRow) -ne (Get-RogPrincipalSid)) { throw 'FAN_HOST_NOT_CURRENT_USER' }
    if ((Get-RogFileTime ([int]$Evidence.hostPid)) -cne $Evidence.hostCreationTime100ns) { throw 'FAN_HOST_FILETIME_CHANGED' }
    return $true
}
function Confirm-RogFanReply($Options,$Reply,[string]$RequestId,[long]$Sequence,$Started,$BeforeClose=$null) {
    $action=$Options.Action.Substring(4)
    if ($Reply.sessionId -cne $Options.SessionId -or $Reply.parentPid -ne $Options.TargetPid -or $Reply.requestId -cne $RequestId -or
        -not (Test-RogSequence $Reply.sequence) -or $Reply.sequence -ne $Sequence -or $Reply.action -cne $action -or
        -not (Test-RogTrue $Reply.ok)) { throw 'FAN_RESPONSE_EXACT_REQUEST_UNVERIFIED' }
    $at=Convert-RogUtc $Reply.timestampUtc
    if ($at -lt $Started -or $at -gt (Get-RogClock).AddSeconds(1)) { throw 'FAN_RESPONSE_NOT_FRESH' }
    $e=$Reply.evidence
    if ($null -eq $e -or $e.source -cne 'mock' -or $e.mode -cne 'mock-handshake' -or -not (Test-RogSequence $e.hostPid)) { throw 'FAN_SOURCE_NOT_STRICT_MOCK' }
    $safe=[ordered]@{source='mock';mode='mock-handshake';hostPid=$e.hostPid;runtimeVerified=$false;zeroPhysicalWritesVerified=$false}
    if ($e.hostPid -gt 0) {
        Assert-RogZeroWrite $e; [void](Assert-RogFanHost $Options $e)
        $safe.runtimeVerified=$true; $safe.zeroPhysicalWritesVerified=$true
        $safe.hostCreationTime100ns=$e.hostCreationTime100ns; $safe.mockControlEnabled=$e.mockControlEnabled
        if (Test-RogSequence $e.mockControlSequence) { $safe.mockControlSequence=$e.mockControlSequence }
    } elseif (-not (Test-RogFalse $e.runtimeVerified)) { throw 'FAN_STOPPED_HOST_RUNTIME_NOT_FALSE' }
    if ($action -in @('probe','on') -and $e.hostPid -eq 0) { throw 'CAPABILITY_UNVERIFIED' }
    if ($action -eq 'on' -and (-not (Test-RogTrue $e.mockControlEnabled) -or -not (Test-RogTrue $e.leaseHeld) -or
        -not (Test-RogSequence $e.mockControlSequence) -or $e.mockControlSequence -lt 1)) { throw 'FAN_CONTROL_ON_READBACK_UNVERIFIED' }
    if ($action -eq 'off' -and $e.hostPid -gt 0 -and -not (Test-RogFalse $e.mockControlEnabled)) { throw 'FAN_CONTROL_OFF_READBACK_UNVERIFIED' }
    if ($action -eq 'close') {
        if ($e.hostPid -ne 0 -or $null -eq $BeforeClose -or $null -eq $e.closeReceipt) { throw 'FAN_CLOSE_RECEIPT_REQUIRED' }
        Assert-RogZeroWrite $e.closeReceipt
        if (-not (Test-RogTrue $e.closeReceipt.mockCloseCompleted) -or -not (Test-RogFalse $e.closeReceipt.mockControlEnabled) -or
            -not (Test-RogTrue $e.childExited) -or $e.closedHostPid -ne $BeforeClose.hostPid -or
            $e.closedHostCreationTime100ns -cne $BeforeClose.hostCreationTime100ns) { throw 'FAN_CLOSE_IDENTITY_UNVERIFIED' }
        if ($null -ne (Get-RogProcessRow ([int]$e.closedHostPid))) { throw 'FAN_CLOSED_HOST_STILL_PRESENT_OR_PID_REUSED' }
        $safe.zeroPhysicalWritesVerified=$true; $safe.closedHostPid=$e.closedHostPid; $safe.closedHostCreationTime100ns=$e.closedHostCreationTime100ns; $safe.childExited=$true
    }
    # Deliberately no token/lease, arbitrary strings, raw request/response or settings.
    return $safe
}
function Invoke-RogFan($Options,$Context) {
    if ($Options.Action -notin @('fan-probe','fan-on','fan-off','fan-close','fan-status')) { throw 'FAN_ACTION_NOT_ALLOWED' }
    Assert-RogAuthorized $Options; [void](Assert-RogTarget $Options -RequireTrusted)
    if (-not (Get-RogPrivilege).verifiedHigh) { throw 'PRIVILEGE_REQUIRED_NOT_AUTHORIZED' }
    $root=Assert-RogProcess $Options
    $namespace=Get-RogFanNamespace $Options $root
    $dir=$namespace.path
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) { throw 'CAPABILITY_UNVERIFIED' }
    $requestPath=Join-Path $dir 'ai-request.json'; $responsePath=Join-Path $dir 'ai-response.json'
    $mutex=$null; $locked=$false; $temporary=$null
    try {
        $mutex=New-Object Threading.Mutex($false,('Local\YMCC-AI-Fan-Client-'+$Options.TargetPid+'-'+$Options.SessionId))
        try { $locked=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked=$true }
        if (-not $locked) { throw 'FAN_CLIENT_SESSION_BUSY' }
        $sequence=1L; $beforeClose=$null
        foreach ($path in @($requestPath,$responsePath)) {
            Assert-RogNoReparse $path
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                $previous=Read-RogJson $path 16384
                if ($previous.sessionId -cne $Options.SessionId -or $previous.parentPid -ne $Options.TargetPid -or -not (Test-RogSequence $previous.sequence)) { throw 'FAN_PREVIOUS_SLOT_INVALID' }
                $sequence=[Math]::Max($sequence,[long]$previous.sequence+1)
                if ($Options.Action -eq 'fan-close' -and $path -eq $responsePath) {
                    $prior=$previous.evidence; Assert-RogZeroWrite $prior; [void](Assert-RogFanHost $Options $prior)
                    $beforeClose=@{hostPid=$prior.hostPid;hostCreationTime100ns=$prior.hostCreationTime100ns}
                }
            }
        }
        if ($sequence -gt 9007199254740991L) { throw 'FAN_SEQUENCE_EXHAUSTED' }
        if ($Options.Action -eq 'fan-close' -and $null -eq $beforeClose) { throw 'FAN_CLOSE_PRIOR_HOST_IDENTITY_REQUIRED' }
        $requestId=[guid]::NewGuid().ToString(); $started=Get-RogClock
        $request=@{schemaVersion=1;sessionId=$Options.SessionId;parentPid=$Options.TargetPid;requestId=$requestId;sequence=$sequence;action=$Options.Action.Substring(4)}
        if ($Options.Action -eq 'fan-on') { $request.preset=$Options.Preset }
        $temporary=Join-Path $dir ('ai-request.'+$requestId+'.tmp')
        Write-RogNewJson $temporary $request
        [void](Assert-RogProcess $Options); [void](Assert-RogTarget $Options -RequireTrusted); Assert-RogNoReparse $dir; Assert-RogNoReparse $requestPath
        if (Test-Path -LiteralPath $requestPath) { [IO.File]::Replace($temporary,$requestPath,$null) } else { [IO.File]::Move($temporary,$requestPath) }
        $temporary=$null; $deadline=$started.AddSeconds($Options.TimeoutSeconds)
        do {
            [void](Assert-RogProcess $Options); Assert-RogNoReparse $dir
            if (Test-Path -LiteralPath $responsePath -PathType Leaf) {
                $reply=Read-RogJson $responsePath 16384
                if ($reply.requestId -ceq $requestId -and $reply.sequence -eq $sequence) {
                    $safe=Confirm-RogFanReply $Options $reply $requestId $sequence $started $beforeClose
                    [void](Assert-RogProcess $Options); [void](Assert-RogTarget $Options -RequireTrusted)
                    return @{sessionId=$Options.SessionId;parentPid=$Options.TargetPid;parentCreationTimeUtc=$Options.TargetCreationTimeUtc;
                        requestId=$requestId;sequence=$sequence;action=$request.action;stateReadAtUtc=Format-RogUtc $reply.timestampUtc;
                        pairedIsolated=$namespace.pairedIsolated;runtimeEvidence=$safe;rogRegressionPassed=$false;abPassed=$false;scope='strict-mock-runtime-only'}
                }
            }
            if ((Get-RogClock) -ge $deadline) { break }; Wait-RogTick
        } while ($true)
        throw 'CAPABILITY_UNVERIFIED'
    } finally {
        if ($temporary -and (Test-Path -LiteralPath $temporary)) { Assert-RogNoReparse $temporary; Remove-Item -LiteralPath $temporary -Force }
        if ($locked) { $mutex.ReleaseMutex() }; if ($mutex) { $mutex.Dispose() }
    }
}
if ($MyInvocation.InvocationName -ne '.') {
    $raw=@($args)
    . (Join-Path $PSScriptRoot 'Invoke-ROG-CPU.ps1')
    $outcome=@(Invoke-RogEntry @{} $PSScriptRoot $raw 'fan')
    $code=[int]$outcome[-1]; if ($outcome.Count -gt 1) { $outcome[0..($outcome.Count-2)] | Write-Output }; exit $code
}