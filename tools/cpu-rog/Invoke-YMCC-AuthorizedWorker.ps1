# Finite same-user high-integrity worker, not a command broker. Runs one action and exits.
function Invoke-RogWorker([object]$Path,[string]$Root,[object[]]$Extra=@()) {
    $ErrorActionPreference='Stop'; $context=$null; $result=$null; $inRequest=$false
    try {
        if ($Extra.Count) { $parsed=Convert-RogInvocation $Extra 'worker'; if ($parsed.ContainsKey('RequestPath')) { $Path=$parsed.RequestPath } }
        if ($Path -isnot [string] -or -not $Path) { throw 'WORKER_REQUEST_REQUIRED' }
        if (-not [IO.Path]::IsPathRooted($Path)) { throw 'WORKER_REQUEST_ABSOLUTE_PATH_REQUIRED' }
        $Path=[IO.Path]::GetFullPath($Path)
        $resultsRoot=[IO.Path]::GetFullPath((Join-Path $Root 'Results')).TrimEnd('\')+'\'
        if (-not $Path.StartsWith($resultsRoot,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($Path) -cne 'worker-request.json') { throw 'WORKER_REQUEST_NAMESPACE_REJECTED' }
        Assert-RogNoReparse $Path; $request=Read-RogJson $Path
        foreach ($p in $request.PSObject.Properties) { if ($p.Name -notin @('schemaVersion','nonce','requesterSid','options','createdUtc','runId')) { throw 'WORKER_REQUEST_FIELD_REJECTED' } }
        if ($request.schemaVersion -ne 1 -or $request.nonce -isnot [string] -or $request.nonce -cnotmatch '^[0-9a-f]{32}$') { throw 'WORKER_REQUEST_INVALID' }
        $created=Convert-RogUtc $request.createdUtc
        if ($created -lt [DateTime]::UtcNow.AddSeconds(-60) -or $created -gt [DateTime]::UtcNow.AddSeconds(2)) { throw 'WORKER_REQUEST_EXPIRED' }
        if ($request.requesterSid -isnot [string] -or $request.requesterSid -cne (Get-RogPrincipalSid)) { throw 'ELEVATED_WORKER_USER_CHANGED' }
        $dir=[IO.Path]::GetDirectoryName($Path)
        $relativeRun=$dir.Substring($resultsRoot.Length).Replace('\','/')
        if ($request.runId -isnot [string] -or $request.runId -cne $relativeRun -or $relativeRun -cnotmatch '^session-[A-Za-z0-9-]+/repeat-[0-9a-f]{32}$') { throw 'WORKER_RUN_ID_MISMATCH' }
        if ((Test-Path -LiteralPath (Join-Path $dir 'worker-result.json')) -or (Test-Path -LiteralPath (Join-Path $dir 'worker-claimed.json'))) { throw 'WORKER_REQUEST_ALREADY_USED' }
        $context=@{Root=$Root;Directory=$dir;RunId=$request.runId;Action='worker';StartedUtc=[DateTime]::UtcNow.ToString('o')}
        $inRequest=$true; $result=New-RogResult $context; $context.Result=$result
        $result.nonce=$request.nonce; $result.requesterSid=$request.requesterSid; $result.workerPid=$PID; $result.verifiedHigh=$false; $result.identityAcquired=$false
        $d=$result.diagnostics; $d.phase='worker-claim'
        Write-RogNewJson (Join-Path $dir 'worker-claimed.json') @{nonce=$request.nonce;workerPid=$PID}
        $options=@{}; foreach ($p in $request.options.PSObject.Properties) { $options[$p.Name]=$p.Value }
        $d.phase='parameter-validation'; $o=Get-RogOptions $options $Root
        if ($o.Action -notin @('preflight','status','start','stop','restart','observe','capture','fan-probe','fan-on','fan-off','fan-close','fan-status')) { throw 'WORKER_ACTION_NOT_ALLOWED' }
        $result.action=$o.Action; $result.sessionId=$o.SessionId
        $d.readOnly=($o.Action -in @('preflight','status')); $d.identity.discoveryOnly=(-not [bool]$o.ExpectedSha256)
        $d.elevation.requested=$o.AllowElevation; $d.elevation.authorized=($o.AuthorizedAction -ceq $o.Action)
        $d.phase='action-authorization'; Assert-RogAuthorized $o
        $d.phase='target-descriptor-validation'
        $result.facts.target=Assert-RogTargetDescriptor $o -RequireTrusted:(-not $d.readOnly)
        $needsSelector=($o.Action -in @('observe','capture','stop','restart') -or $o.Action -like 'fan-*')
        if ($needsSelector) { $d.phase='selector-validation'; Assert-RogProcessSelector $o }
        # Prove this worker's token before any actual file/hash/CIM/owner read of the target.
        $d.phase='permission-verification'; $privilege=Get-RogDiagnosticPrivilege $context
        $result.verifiedHigh=(Test-RogTrue $privilege.verifiedHigh)
        if (-not $result.verifiedHigh) { throw 'WORKER_HIGH_TOKEN_UNVERIFIED' }
        $d.phase='target-validation'; $result.facts.target=Assert-RogTarget $o -RequireTrusted:(-not $d.readOnly)
        if ($needsSelector) { $d.phase='identity-gate'; [void](Assert-RogProcess $o) }
        # Never recursively elevate. No source/settings/CLI mutation or function probes in discovery.
        $o.AllowElevation=$false; $d.phase='finite-operation'
        $result.details=Invoke-RogFiniteOperation $o $context
        $result.identityAcquired=$d.identity.acquired; $result.ok=$true; $result.exitCode=0; $d.phase='completed'
    } catch {
        if (-not $context) { $context=New-RogContext $Root 'worker'; $result=New-RogResult $context }
        $result.errorCode=Get-RogSafeWorkerError (Get-RogCode $_); $result.exitCode=1
    }
    $name=if ($inRequest) {'worker-result.json'} else {'result.json'}
    try { Write-RogResult $context $result $name; Write-Output ('ROG_WORKER_RESULT '+(Join-Path $context.Directory $name)) }
    catch { Write-Output '{"toolVersion":"rog-portable-runner-2","ok":false,"exitCode":2,"errorCode":"WORKER_RESULT_WRITE_FAILED","rogRegressionPassed":false}'; return 2 }
    return [int]$result.exitCode
}
if ($MyInvocation.InvocationName -ne '.') {
    $raw=@($args)
    . (Join-Path $PSScriptRoot 'Invoke-ROG-CPU.ps1')
    $outcome=@(Invoke-RogWorker '' $PSScriptRoot $raw)
    $code=[int]$outcome[-1]; if ($outcome.Count -gt 1) { $outcome[0..($outcome.Count-2)] | Write-Output }; exit $code
}