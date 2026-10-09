# Offline-only fixtures: fake processes/control/UAC/native calls and an inert collector.
param([string]$ValidationDirectory='')
$ErrorActionPreference='Stop'
$validationClock=[Diagnostics.Stopwatch]::StartNew()
$sourceRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ValidationDirectory) { $ValidationDirectory=Join-Path $sourceRoot 'validation' }
. (Join-Path $sourceRoot 'Invoke-ROG-CPU.ps1')
. (Join-Path $sourceRoot 'Control-YMCC-Lifecycle.ps1')
. (Join-Path $sourceRoot 'Send-YMCC-AI-Fan-Mock.ps1')
. (Join-Path $sourceRoot 'Invoke-YMCC-AuthorizedWorker.ps1')
$script:cases=New-Object 'System.Collections.Generic.List[object]'
function Assert-Test($Condition,[string]$Code='ASSERTION_FAILED') { if (-not $Condition) { throw $Code } }
function Test-Case([string]$Name,[scriptblock]$Body) {
    try { & $Body; $script:cases.Add(@{name=$Name;passed=$true}); Write-Output ('PASS '+$Name) }
    catch { $script:cases.Add(@{name=$Name;passed=$false;errorCode=(Get-RogCode $_)}); Write-Output ('FAIL '+$Name+' '+(Get-RogCode $_)) }
}
function Expect-Code([scriptblock]$Body,[string]$Code) {
    $actual='NO_EXCEPTION'; try { & $Body | Out-Null } catch { $actual=Get-RogCode $_ }
    Assert-Test ($actual -ceq $Code) ('EXPECTED_'+$Code)
}
# Temporary copied fixtures are retained for audit, never packaged or treated as ROG Results.
$fixtureBase=Join-Path $ValidationDirectory ('YMCC offline runner v10.1 空格 '+[guid]::NewGuid().ToString('N'))
$fixtureRoot=Join-Path $fixtureBase '复制后的工具 中文 path'; [void][IO.Directory]::CreateDirectory($fixtureRoot)
$kitFiles=@('Invoke-ROG-CPU.ps1','Invoke-YMCC-AuthorizedWorker.ps1','Control-YMCC-Lifecycle.ps1','Send-YMCC-AI-Fan-Mock.ps1','config.example.json')
$docFiles=@('docs\ROG-CAPTURE-AI-TASK-v10-20261004.md','docs\KNOWN-CAPTURE-ERRORS-AND-FIXES.md','docs\OPERATOR-COMMANDS.md')
[void][IO.Directory]::CreateDirectory((Join-Path $fixtureRoot 'docs'))
foreach ($doc in $docFiles) { [IO.File]::WriteAllText((Join-Path $fixtureRoot $doc),'INERT_FIXTURE_DOC_NOT_OPERATOR_INSTRUCTIONS',[Text.UTF8Encoding]::new($false)) }
foreach ($file in $kitFiles) { [IO.File]::Copy((Join-Path $sourceRoot $file),(Join-Path $fixtureRoot $file),$false) }
# Only the temporary copied runner gets subprocess seam injection. The real source is never rewritten.
# Keep its actual wrapper parser/dispatcher; no own-token/native/process/CIM/UAC calls may escape.
$copiedRunner=Join-Path $fixtureRoot 'Invoke-ROG-CPU.ps1'
$wrapperSeams=@(
 'function Initialize-RogNative { throw "REAL_NATIVE_CALL_FORBIDDEN_IN_WRAPPER_FIXTURE" }',
 'function Get-RogPrivilege { return @{verifiedHigh=$false;currentUserSid="S-1-5-21-100-200-300-1001";fixture=$true} }',
 'function Get-RogPrincipalSid { return "S-1-5-21-100-200-300-1001" }',
 'function Get-RogProcessRow { throw "REAL_PROCESS_READ_FORBIDDEN_IN_WRAPPER_FIXTURE" }',
 'function Get-RogNamedRows { throw "REAL_CIM_READ_FORBIDDEN_IN_WRAPPER_FIXTURE" }',
 'function Get-RogOwnerSid { throw "REAL_OWNER_READ_FORBIDDEN_IN_WRAPPER_FIXTURE" }',
 'function Get-CimInstance { throw "REAL_CIM_CALL_FORBIDDEN_IN_WRAPPER_FIXTURE" }',
 'function Invoke-CimMethod { throw "REAL_CIM_CALL_FORBIDDEN_IN_WRAPPER_FIXTURE" }',
 'function Get-Process { throw "REAL_PROCESS_CALL_FORBIDDEN_IN_WRAPPER_FIXTURE" }',
 'function Start-Process { throw "REAL_UAC_OR_PROCESS_START_FORBIDDEN_IN_WRAPPER_FIXTURE" }',
 'function Start-Sleep { throw "REAL_SLEEP_FORBIDDEN_IN_WRAPPER_FIXTURE" }') -join [Environment]::NewLine
$copiedText=[IO.File]::ReadAllText($copiedRunner)
$wrapperMarker="if (`$MyInvocation.InvocationName -ne '.') {"
Assert-Test ($copiedText.Contains($wrapperMarker)) 'WRAPPER_FIXTURE_ENTRY_MARKER_REQUIRED'
[IO.File]::WriteAllText($copiedRunner,$copiedText.Replace($wrapperMarker,($wrapperSeams+[Environment]::NewLine+$wrapperMarker)),[Text.UTF8Encoding]::new($true))
$stub=@(
 'param($ExePath,$OutDir,$WarmupSeconds,$Seconds,$SampleMilliseconds,$InventoryMilliseconds,',
 ' $FanState,$GamepadState,$GyroState,$Label,[switch]$RequireControlEvidence,$LifecycleJsonl,$VirtualHandshakeJsonl,$ExpectedSessionId,[string[]]$LogFile=@())',
 '# Synthetic data only, no counters/sleep/ports/processes.',
 '[void][IO.Directory]::CreateDirectory($OutDir)',
 '$s=@{measurementValid=$true;scenarioVerified=$false;realCapturePerformed=$false;fixture=$true;label=$Label;requireControlEvidence=[bool]$RequireControlEvidence;logFileCount=@($LogFile).Count}',
 '[IO.File]::WriteAllText((Join-Path $OutDir "summary.json"),($s|ConvertTo-Json),[Text.UTF8Encoding]::new($false))',
 'exit 0') -join [Environment]::NewLine
[IO.File]::WriteAllText((Join-Path $fixtureRoot 'Collect-YMCC-CPU.ps1'),$stub,[Text.UTF8Encoding]::new($true))
$fakeExe=Join-Path $fixtureRoot '产品 中文\YeManCC.exe'; [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($fakeExe))
[IO.File]::WriteAllText($fakeExe,'INERT_OFFLINE_FIXTURE_NOT_AN_EXECUTABLE',[Text.UTF8Encoding]::new($false))
$fakeHash=(Get-FileHash -LiteralPath $fakeExe -Algorithm SHA256).Hash.ToLowerInvariant()
$script:sid='S-1-5-21-100-200-300-1001'; $session='11111111-2222-3333-4444-555555555555'
$birth='2026-10-04T01:02:03.1234560Z'; $script:clock=Convert-RogUtc $birth
$script:privilegeCalls=0; $script:rowQueries=0; $script:rows=@{}; $script:high=$true; $script:startCalls=0; $script:closeCalls=0; $script:tickCalls=0
$script:elevateCalls=0; $script:elevationDenied=$false; $script:normalCloseAccepted=$true
$script:fakeTargetPrivilege=$true; $script:fakeArgv=@(); $script:fakeFileTime='134000000000000000'
$script:localAppData=Join-Path $fixtureRoot '假用户 local app data'
$script:sourceOwnerFunction=${function:Get-RogOwnerSid}
$script:sourceNoReparseFunction=${function:Assert-RogNoReparse}
$script:inHighWorker=$false; $script:fakeWorkerHigh=$true; $script:denyLowTargetReads=$false
$script:parentMetadataReads=0; $script:workerMetadataReads=0; $script:parentHashReads=0; $script:workerHashReads=0
$script:parentProcessReads=0; $script:workerProcessReads=0; $script:parentOwnerReads=0; $script:workerOwnerReads=0
$script:fakeFiniteCalls=0
function Assert-RogNoReparse([string]$Path) {
    if ($Path -ieq $fakeExe) {
        if ($script:inHighWorker) { $script:workerMetadataReads++ } else { $script:parentMetadataReads++ }
        if ($script:denyLowTargetReads -and -not $script:high -and -not $script:inHighWorker) { throw 'PERMISSION_DENIED' }
    }
    & $script:sourceNoReparseFunction $Path
}
function Get-FileHash {
    param([string]$LiteralPath,[string]$Algorithm='SHA256',$ErrorAction)
    if ($LiteralPath -ieq $fakeExe) {
        if ($script:inHighWorker) { $script:workerHashReads++ } else { $script:parentHashReads++ }
        if ($script:denyLowTargetReads -and -not $script:high -and -not $script:inHighWorker) { throw 'PERMISSION_DENIED' }
    }
    Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm -ErrorAction Stop
}
# Every runtime seam is fake; even unexpected fall-through cannot touch real products.
function Initialize-RogNative { throw 'REAL_NATIVE_CALL_FORBIDDEN_IN_FIXTURE' }
function Get-RogPrincipalSid { return $script:sid }
function Get-RogPrivilege { $script:privilegeCalls++; return @{currentUserSid=$script:sid;verifiedHigh=$script:high;workerPid=7777} }
function Get-RogTargetPrivilege($ProcessId) { return @{ok=$true;isElevated=$script:fakeTargetPrivilege;integrityLevel=$(if($script:fakeTargetPrivilege){'high'}else{'not-high'});canMutateYmcc=$script:fakeTargetPrivilege} }
function Get-RogProcessRow([int]$ProcessId) {
    $script:rowQueries++
    if ($script:inHighWorker) { $script:workerProcessReads++ } else { $script:parentProcessReads++ }
    if ($script:denyLowTargetReads -and -not $script:high -and -not $script:inHighWorker) { throw 'PROCESS_QUERY_DENIED' }
    if ($script:rows.ContainsKey($ProcessId)) { return $script:rows[$ProcessId] }; return $null
}
function Get-RogOwnerSid($Row) {
    if ($script:inHighWorker) { $script:workerOwnerReads++ } else { $script:parentOwnerReads++ }
    if ($script:denyLowTargetReads -and -not $script:high -and -not $script:inHighWorker) { throw 'PROCESS_OWNER_QUERY_DENIED' }
    if ($Row.owner) { return $Row.owner }; return $script:sid
}
function Get-CimInstance {
    param($ClassName,$Filter,$OperationTimeoutSec,$ErrorAction)
    if ($Filter -like 'ParentProcessId=*') { return @() }
    if ($script:inHighWorker) { $script:workerProcessReads++ } else { $script:parentProcessReads++ }
    if ($script:denyLowTargetReads -and -not $script:high -and -not $script:inHighWorker) { throw 'PROCESS_QUERY_DENIED' }
    return @($script:rows.Values)
}
function Invoke-CimMethod { throw 'REAL_CIM_METHOD_FORBIDDEN_IN_FIXTURE' }
function Get-Process { throw 'REAL_PROCESS_HANDLE_FORBIDDEN_IN_FIXTURE' }
function Start-Sleep { throw 'REAL_SLEEP_FORBIDDEN_IN_FIXTURE' }
function Wait-RogTick { $script:tickCalls++ }
function Get-RogClock { $script:clock=$script:clock.AddSeconds(1); return $script:clock }
function Get-RogArgv($Line) { return $script:fakeArgv }
function Get-RogFileTime($ProcessId) { return $script:fakeFileTime }
function Get-RogLocalAppData { return $script:localAppData }
function Get-RogWindowObserved($Process) { return $true } # A window still is NOT frontend-ready.
function New-FakeHandle([int]$Id) {
    $p=[pscustomobject]@{Id=$Id;Handle=1;ExitCode=0}; $p | Add-Member ScriptMethod Dispose {}; return $p
}
function Get-RogProcessHandle($ProcessId) { return New-FakeHandle $ProcessId }
function Invoke-RogNormalClose($Handle) { $script:closeCalls++; if ($script:normalCloseAccepted) { $script:rows.Remove([int]$Handle.Id) }; return $script:normalCloseAccepted }
function Start-RogExactTarget($Options) {
    $script:startCalls++; $script:rows[501]=[pscustomobject]@{ProcessId=501;ExecutablePath=$Options.ExePath;CreationDate=(Convert-RogUtc $birth);ParentProcessId=100;CommandLine='PRIVATE_START_COMMAND_LINE';owner=$script:sid}
    return New-FakeHandle 501
}
$script:elevationMode='synthetic'; $script:workerErrorCode='PROCESS_BIRTH_UNAVAILABLE'; $script:workerTimedOut=$false
$script:workerProofMismatch=$false; $script:workerMissingProof=$false
function Start-Process {
    param($FilePath,$Verb,$WindowStyle,$WorkingDirectory,$ArgumentList,[switch]$PassThru,$ErrorAction)
    $script:elevateCalls++
    Assert-Test ($Verb -ceq 'RunAs' -and $WindowStyle -ceq 'Hidden') 'ELEVATION_MUST_BE_HIDDEN_RUNAS'
    Assert-Test ($ArgumentList -notmatch 'ExecutionPolicy|Bypass') 'NO_POLICY_BYPASS'
    if ($script:elevationDenied) { throw 'PRIVATE_DENIAL_TEXT_DO_NOT_EXPORT' }
    $encoded=$ArgumentList.Substring($ArgumentList.LastIndexOf(' ')+1)
    $command=[Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
    $paths=@([regex]::Matches($command,"FromBase64String\('([A-Za-z0-9+/=]+)'\)"))
    Assert-Test ($paths.Count -eq 2) 'FAKE_REQUEST_PATH_PARSE_FAILED'
    $requestPath=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($paths[1].Groups[1].Value))
    $requestDir=[IO.Path]::GetDirectoryName($requestPath); $r=Read-RogJson $requestPath
    if ($script:elevationMode -eq 'execute-worker') {
        # Actual worker functions/dispatcher, fake high token and fake process inventory only.
        function Get-RogPrivilege { $script:privilegeCalls++; return @{verifiedHigh=$script:fakeWorkerHigh;currentUserSid=$script:sid;workerPid=$PID} }
        $previousWorkerScope=$script:inHighWorker; $script:inHighWorker=$true
        try { $workerOutput=@(Invoke-RogWorker $requestPath $fixtureRoot) } finally { $script:inHighWorker=$previousWorkerScope }
        $p=New-FakeHandle $PID; $p.ExitCode=[int]$workerOutput[-1]
    } else {
        $ok=($script:elevationMode -ne 'synthetic-failure')
        $proof=@{toolVersion='rog-portable-runner-2';runId=$r.runId;ok=$ok;exitCode=$(if($ok){0}else{1});
            verifiedHigh=$true;workerPid=9001;nonce=$r.nonce;requesterSid=$r.requesterSid;action=$r.options.Action;
            identityAcquired=$ok;errorCode=$(if($ok){$null}else{$script:workerErrorCode});
            diagnostics=@{phase='identity-query';identity=@{attempted=$true;availableFields=@('pid','exePath','PRIVATE_OPAQUE_FIELD')}};
            details=@{opaque='PRIVATE_WORKER_TEXT';CommandLine='PRIVATE_COMMAND_LINE';token='PRIVATE_TOKEN'}}
        if ($script:workerProofMismatch) { $proof.nonce='0'*32 }
        if (-not $script:workerMissingProof) { Write-RogNewJson (Join-Path $requestDir 'worker-result.json') $proof }
        $p=New-FakeHandle 9001; $p.ExitCode=$proof.exitCode
    }
    $p | Add-Member ScriptMethod WaitForExit { param($Milliseconds) $script:observedWorkerTimeout=$Milliseconds; return (-not $script:workerTimedOut) }
    return $p
}
function New-FakeOptions([hashtable]$More=@{}) {
    $o=@{Action='status';ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth;SessionId=$session}
    foreach ($key in $More.Keys) { $o[$key]=$More[$key] }; return Get-RogOptions $o $fixtureRoot
}
function Reset-FakeTarget {
    $script:rows=@{401=[pscustomobject]@{ProcessId=401;ExecutablePath=$fakeExe;CreationDate=(Convert-RogUtc $birth);ParentProcessId=100;CommandLine='PRIVATE_COMMAND_LINE';owner=$script:sid}}
    $script:high=$true; $script:fakeTargetPrivilege=$true; $script:normalCloseAccepted=$true; $script:clock=Convert-RogUtc $birth
}
function Invoke-FakeEntry([hashtable]$Options) {
    $output=@(Invoke-RogEntry $Options $fixtureRoot)
    $line=$output | Where-Object {$_ -is [string] -and $_.StartsWith('ROG_RESULT ')} | Select-Object -Last 1
    Assert-Test ([bool]$line) 'ENTRY_MISSING_STRUCTURED_RESULT'; $path=$line.Substring(11)
    return @{code=[int]$output[-1];result=(Read-RogJson $path);path=$path}
}
function Invoke-FakeWrapper([string]$File,[string[]]$Arguments=@()) {
    $engine=Join-Path $PSHOME $(if($PSVersionTable.PSEdition -eq 'Core'){'pwsh.exe'}else{'powershell.exe'})
    # New shells run copied wrappers only; cases validate or fail BEFORE any runtime calls.
    $output=@(& $engine -NoProfile -NonInteractive -File (Join-Path $fixtureRoot $File) @Arguments 2>&1); $code=$LASTEXITCODE
    $line=$output | ForEach-Object {[string]$_} | Where-Object {$_ -like 'ROG_RESULT *' -or $_ -like 'ROG_WORKER_RESULT *'} | Select-Object -Last 1
    Assert-Test ([bool]$line) 'WRAPPER_MISSING_STRUCTURED_RESULT'; $path=$line.Substring($line.IndexOf(' ')+1)
    return @{code=$code;result=(Read-RogJson $path);path=$path}
}
Reset-FakeTarget
Test-Case 'source parser validation only' {
 foreach ($name in @('Invoke-ROG-CPU.ps1','Invoke-YMCC-AuthorizedWorker.ps1','Control-YMCC-Lifecycle.ps1','Send-YMCC-AI-Fan-Mock.ps1','tests\Test-ROG-CPU-Runner.ps1')) {
  $tokens=$null;$errors=$null;[void][Management.Automation.Language.Parser]::ParseFile((Join-Path $sourceRoot $name),[ref]$tokens,[ref]$errors); Assert-Test (@($errors).Count -eq 0) 'SOURCE_PARSER_FAILED'
 }
}
Test-Case 'static finite operations no forbidden commands or local paths' {
 foreach ($name in $kitFiles | Where-Object {$_ -like '*.ps1'}) {
  $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $sourceRoot $name),[ref]$tokens,[ref]$errors)
  $commands=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.CommandAst]},$true) | ForEach-Object {$_.GetCommandName()})
  Assert-Test (@($commands | Where-Object {$_ -in @('Stop-Process','taskkill','New-Service','Register-ScheduledTask','Set-Acl','Invoke-WebRequest','Invoke-RestMethod','Set-ExecutionPolicy')}).Count -eq 0) 'FORBIDDEN_COMMAND'
  $text=[IO.File]::ReadAllText((Join-Path $sourceRoot $name)); Assert-Test ($text -notmatch '(?i)[A-Z]:\\(?:Users|YeManCC-Work)|ForceAfterTimeout|StateDir\s*=') 'LOCAL_PATH_OR_UNSAFE_OPTION'
 }
}
Test-Case 'validate default never invokes runtime controls' { $r=Invoke-FakeEntry @{}; Assert-Test ($r.code -eq 0 -and $r.result.details.productTouched -eq $false); Assert-Test ($r.result.details.manifest.status -eq 'NOT_PRESENT_SOURCE_CHECK_ONLY'); Assert-Test ($script:elevateCalls -eq 0 -and $script:startCalls -eq 0 -and $script:closeCalls -eq 0 -and $script:privilegeCalls -eq 0 -and $script:rowQueries -eq 0) }
Test-Case 'copied wrappers enforce fake own token and prohibit native process CIM UAC' {
    $text=[IO.File]::ReadAllText($copiedRunner)
    Assert-Test ($text.Contains('REAL_NATIVE_CALL_FORBIDDEN_IN_WRAPPER_FIXTURE') -and $text.Contains('REAL_UAC_OR_PROCESS_START_FORBIDDEN_IN_WRAPPER_FIXTURE'))
    $r=Invoke-FakeWrapper 'Invoke-ROG-CPU.ps1' @('-Action','preflight','-ExePath',(Join-Path $fixtureRoot 'YMCC-missing-proof.exe'))
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_EXE_NOT_FOUND')
    Assert-Test ($r.result.diagnostics.permission.querySucceeded -and -not $r.result.diagnostics.permission.verifiedHigh -and -not $r.result.diagnostics.elevation.attempted)
}
Test-Case 'copied default wrapper spaces Chinese portable path'  { $r=Invoke-FakeWrapper 'Invoke-ROG-CPU.ps1'; Assert-Test ($r.code -eq 0 -and $r.path.StartsWith($fixtureRoot) -and -not $r.result.rogRegressionPassed) }
Test-Case 'wrapper invalid action structured failure' { $r=Invoke-FakeWrapper 'Invoke-ROG-CPU.ps1' @('-Action','nonsense'); Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'INVALID_ACTION') }
Test-Case 'wrapper missing parameter value structured failure' { $r=Invoke-FakeWrapper 'Invoke-ROG-CPU.ps1' @('-ExePath'); Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PARAMETER_VALUE_REQUIRED') }
Test-Case 'wrapper unknown argument opaque value not leaked' { $r=Invoke-FakeWrapper 'Invoke-ROG-CPU.ps1' @('-NoSuchParameter','PRIVATE_UNKNOWN'); Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'UNKNOWN_OR_POSITIONAL_PARAMETER'); Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_UNKNOWN') }
Test-Case 'wrapper preflight missing path' { $r=Invoke-FakeWrapper 'Invoke-ROG-CPU.ps1' @('-Action','preflight'); Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_PATH_REQUIRED') }
Test-Case 'wrapper missing target file' { $r=Invoke-FakeWrapper 'Invoke-ROG-CPU.ps1' @('-Action','preflight','-ExePath',(Join-Path $fixtureRoot 'YMCC-missing.exe')); Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_EXE_NOT_FOUND') }
Test-Case 'wrapper bad hash cannot reach process control' { $r=Invoke-FakeWrapper 'Invoke-ROG-CPU.ps1' @('-Action','start','-ExePath',$fakeExe,'-ExpectedSha256',('0'*64),'-AuthorizedAction','start'); Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_SHA256_MISMATCH') }
Test-Case 'wrapper no hash cannot authorize observed hash' { $r=Invoke-FakeWrapper 'Invoke-ROG-CPU.ps1' @('-Action','start','-ExePath',$fakeExe,'-AuthorizedAction','start'); Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TRUSTED_PRODUCT_HASH_REQUIRED') }
Test-Case 'wrapper known hash is not user action authorization' { $r=Invoke-FakeWrapper 'Invoke-ROG-CPU.ps1' @('-Action','start','-ExePath',$fakeExe,'-ExpectedSha256',$fakeHash); Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'USER_ACTION_AUTHORIZATION_REQUIRED') }
Test-Case 'lifecycle wrapper failure safe' { $r=Invoke-FakeWrapper 'Control-YMCC-Lifecycle.ps1' @('-Action','stop','-AuthorizedAction','stop'); Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_PATH_REQUIRED') }
Test-Case 'fan wrapper failure safe' { $r=Invoke-FakeWrapper 'Send-YMCC-AI-Fan-Mock.ps1' @('-Action','on','-AuthorizedAction','on'); Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_PATH_REQUIRED') }
Test-Case 'worker wrapper missing request safe' { $r=Invoke-FakeWrapper 'Invoke-YMCC-AuthorizedWorker.ps1'; Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'WORKER_REQUEST_REQUIRED') }
Test-Case 'output unique session repeat refuses overwrite' { $a=Invoke-FakeEntry @{}; $b=Invoke-FakeEntry @{}; Assert-Test ($a.path -ne $b.path); Expect-Code {Write-RogNewJson $a.path @{private='bad'}} 'SAFE_OPERATION_FAILED' }
Test-Case 'bad numeric argument safe result' { $r=Invoke-FakeEntry @{Seconds='not-a-number'}; Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'INVALID_SECONDS') }
Test-Case 'config cannot authorize control' { $p=Join-Path $fixtureRoot 'bad-config.json'; Write-RogNewJson $p @{schemaVersion=1;AuthorizedAction='start'}; $r=Invoke-FakeEntry @{ConfigPath=$p}; Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'CONFIG_FIELD_NOT_ALLOWED') }
Test-Case 'private start arguments rejected' { Expect-Code {New-FakeOptions @{StartArgument=@('--token','PRIVATE_SECRET')}} 'START_ARGUMENT_NOT_ALLOWED' }
Test-Case 'exact paired public start arguments accepted' { $o=New-FakeOptions @{StartArgument=@('--ai-fan-mock-session',$session,'--ai-cpu-isolated-session',$session)}; Assert-Test ($o.StartArgument.Count -eq 4) }
Test-Case 'paired public start argument mismatch rejected' { Expect-Code {New-FakeOptions @{StartArgument=@('--ai-fan-mock-session',$session,'--ai-cpu-isolated-session','bad')}} 'START_ARGUMENT_NOT_ALLOWED' }
Test-Case 'UTC JSON stable across zh-CN and en-US' {
 $p=Join-Path $fixtureRoot 'utc-fixture.json'; Write-RogNewJson $p @{birth=$birth}; $old=[Threading.Thread]::CurrentThread.CurrentCulture
 try {foreach($culture in @('zh-CN','en-US')) {[Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]::GetCultureInfo($culture); Assert-Test ((Convert-RogUtc (Read-RogJson $p).birth).Ticks -eq (Convert-RogUtc $birth).Ticks)}} finally {[Threading.Thread]::CurrentThread.CurrentCulture=$old}
 Assert-Test ((Format-RogUtc (Convert-RogUtc $birth)) -ceq $birth)
}
Test-Case 'preflight unknown hash is read-only discovery' { $r=Invoke-FakeEntry @{Action='preflight';ExePath=$fakeExe}; Assert-Test ($r.code -eq 0 -and -not $r.result.details.target.trustedHashSupplied -and -not $r.result.details.target.discoveredHashIsAuthorization); Assert-Test ($r.result.details.fan.state -eq 'CAPABILITY_UNVERIFIED' -and -not $r.result.details.runAsAttempted) }
Test-Case 'status exact fake root no commandLine serialization' { $r=Invoke-FakeEntry @{Action='status';ExePath=$fakeExe;TargetPid=401;TargetCreationTimeUtc=$birth}; Assert-Test ($r.code -eq 0 -and $r.result.details.rootCount -eq 1); Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_COMMAND_LINE|CommandLine"') }
Test-Case 'PID reused birth rejected' { $o=New-FakeOptions; $script:rows[401].CreationDate=(Convert-RogUtc $birth).AddSeconds(1); Expect-Code {Assert-RogProcess $o} 'TARGET_PROCESS_BIRTH_CHANGED'; Reset-FakeTarget }
Test-Case 'process changed path rejected' { $o=New-FakeOptions; $script:rows[401].ExecutablePath=Join-Path $fixtureRoot 'YMCC-other.exe'; Expect-Code {Assert-RogProcess $o} 'TARGET_PROCESS_PATH_CHANGED'; Reset-FakeTarget }
Test-Case 'different current user rejected' { $o=New-FakeOptions; $script:rows[401].owner='OTHER_USER'; Expect-Code {Assert-RogProcess $o} 'TARGET_NOT_CURRENT_USER'; Reset-FakeTarget }
Test-Case 'observe without control evidence remains non-S0 non-S4 non-AB' {
 $r=Invoke-FakeEntry @{Action='observe';ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth;Label='S4'}
 Assert-Test ($r.code -eq 0 -and -not $r.result.details.scenarioVerified -and -not $r.result.abPassed -and $r.result.details.scenarioClass -eq 'unknown-observation-not-S0-not-S4-not-AB')
 $s=Read-RogJson (Join-Path ([IO.Path]::GetDirectoryName($r.path)) 'capture\summary.json'); Assert-Test ($s.label -eq 'S-observed' -and -not $s.requireControlEvidence -and -not $s.realCapturePerformed)
}
Test-Case 'capture missing runtime evidence does not call collector' { $r=Invoke-FakeEntry @{Action='capture';ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth}; Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'REAL_CONTROL_AND_RUNTIME_EVIDENCE_REQUIRED'); Assert-Test (-not (Test-Path -LiteralPath (Join-Path ([IO.Path]::GetDirectoryName($r.path)) 'capture'))) }
Test-Case 'capture opaque evidence not formal proof nor copied into result' {
 $l=Join-Path $fixtureRoot 'opaque-life.jsonl';$v=Join-Path $fixtureRoot 'opaque-runtime.jsonl';Write-RogNewJson $l @{opaque='PRIVATE_EVIDENCE'};Write-RogNewJson $v @{opaque='PRIVATE_EVIDENCE'}
 $r=Invoke-FakeEntry @{Action='capture';ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth;ExpectedSessionId=$session;LifecycleJsonl=$l;VirtualHandshakeJsonl=$v}
 Assert-Test ($r.code -ne 0 -and -not $r.result.productAcceptancePassed -and -not $r.result.rogRegressionPassed);Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_EVIDENCE')
}
Test-Case 'permission failure is not capability absence' { $script:high=$false; $r=Invoke-FakeEntry @{Action='stop';ExePath=$fakeExe;ExpectedSha256=$fakeHash;AuthorizedAction='stop';TargetPid=401;TargetCreationTimeUtc=$birth}; Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PRIVILEGE_REQUIRED_NOT_AUTHORIZED');$script:high=$true }
Test-Case 'normal fake root close only never force' {Reset-FakeTarget;$o=New-FakeOptions @{Action='stop';AuthorizedAction='stop'};$r=Invoke-RogLifecycle $o (New-RogContext $fixtureRoot 'stop');Assert-Test ($r.ownedTreeExited -and -not $r.forceUsed);Reset-FakeTarget}
Test-Case 'normal close unavailable no forced fallback' {Reset-FakeTarget;$script:normalCloseAccepted=$false;$o=New-FakeOptions @{Action='stop';AuthorizedAction='stop'};Expect-Code {Invoke-RogLifecycle $o (New-RogContext $fixtureRoot 'stop')} 'NORMAL_CLOSE_UNAVAILABLE_NO_FORCE';Reset-FakeTarget}
Test-Case 'fake restart process evidence is not frontend ready' {Reset-FakeTarget;$o=New-FakeOptions @{Action='restart';AuthorizedAction='restart'};$c=New-RogContext $fixtureRoot 'restart';$r=Invoke-RogLifecycle $o $c;Assert-Test ($r.stop.ownedTreeExited -and $r.start.targetPid -eq 501 -and $r.windowObserved -and -not $r.frontendReadyVerified -and -not $r.runtimeReadyVerified);Assert-Test ([IO.File]::ReadAllText((Join-Path $c.Directory 'lifecycle-control.jsonl')) -notmatch 'PRIVATE_');Reset-FakeTarget}
Test-Case 'failed restart stop cannot cause start' {Reset-FakeTarget;$before=$script:startCalls;$script:normalCloseAccepted=$false;$o=New-FakeOptions @{Action='restart';AuthorizedAction='restart'};Expect-Code {Invoke-RogLifecycle $o (New-RogContext $fixtureRoot 'restart')} 'NORMAL_CLOSE_UNAVAILABLE_NO_FORCE';Assert-Test ($script:startCalls -eq $before);Reset-FakeTarget}
Test-Case 'fake start rejects already running root' {$o=New-FakeOptions @{Action='start';AuthorizedAction='start';TargetPid=0;TargetCreationTimeUtc=''};Expect-Code {Invoke-RogLifecycle $o (New-RogContext $fixtureRoot 'start')} 'TARGET_ALREADY_RUNNING_OR_AMBIGUOUS'}
Test-Case 'fake start actual target high proof required' {$script:rows=@{};$script:fakeTargetPrivilege=$false;$o=New-FakeOptions @{Action='start';AuthorizedAction='start';TargetPid=0;TargetCreationTimeUtc=''};Expect-Code {Invoke-RogLifecycle $o (New-RogContext $fixtureRoot 'start')} 'TARGET_HIGH_TOKEN_UNVERIFIED';Reset-FakeTarget}
Test-Case 'fake hidden RunAs waits finite worker only' {$o=New-FakeOptions @{Action='start';AuthorizedAction='start';AllowElevation=$true;TargetPid=0;TargetCreationTimeUtc=''};$script:elevationContext=New-RogContext $fixtureRoot 'start';$r=Invoke-RogElevated $o $script:elevationContext;Assert-Test ($r.finiteWorkerExited -and $r.verifiedHigh);Assert-Test (($r|ConvertTo-Json) -notmatch 'PRIVATE_WORKER_TEXT')}
Test-Case 'fake RunAs refusal no bypass' {$o=New-FakeOptions @{Action='start';AuthorizedAction='start';AllowElevation=$true};$script:elevationContext=New-RogContext $fixtureRoot 'start';$script:elevationDenied=$true;Expect-Code {Invoke-RogElevated $o $script:elevationContext} 'ELEVATION_DENIED_OR_TOOL_POLICY_REFUSED';$script:elevationDenied=$false}
Test-Case 'worker lower fake token safely records failure' {
 $c=New-RogContext $fixtureRoot 'stop';$o=New-FakeOptions @{Action='stop';AuthorizedAction='stop'};$p=Join-Path $c.Directory 'worker-request.json';Write-RogNewJson $p @{schemaVersion=1;nonce=[guid]::NewGuid().ToString('N');requesterSid=$script:sid;options=$o;createdUtc=[DateTime]::UtcNow.ToString('o');runId=$c.RunId}
 $script:high=$false;$out=@(Invoke-RogWorker $p $fixtureRoot);$script:high=$true;$r=Read-RogJson (Join-Path $c.Directory 'worker-result.json');Assert-Test ($out[-1] -ne 0 -and $r.errorCode -eq 'WORKER_HIGH_TOKEN_UNVERIFIED')
}
Test-Case 'worker cannot take arbitrary request namespace' {$out=@(Invoke-RogWorker (Join-Path $fixtureRoot 'outside.json') $fixtureRoot);Assert-Test ($out[-1] -ne 0)}
Test-Case 'fan exact LocalAppData namespace' {$o=New-FakeOptions;$script:fakeArgv=@($fakeExe,'--ai-fan-mock-session',$session);$r=Get-RogFanNamespace $o $script:rows[401];Assert-Test (-not $r.pairedIsolated -and $r.path.StartsWith($script:localAppData) -and $r.namespaceDerivedFromExactCli)}
Test-Case 'fan exact paired isolated derived namespace' {$o=New-FakeOptions;$script:fakeArgv=@($fakeExe,'--ai-fan-mock-session',$session,'--ai-cpu-isolated-session',$session);$r=Get-RogFanNamespace $o $script:rows[401];Assert-Test ($r.pairedIsolated -and $r.path -eq (Join-Path ([IO.Path]::GetDirectoryName($fakeExe)) ('ai-cpu-sessions\'+$session+'\shared\ai-fan-sessions\'+$session+'\fan-host')))}
Test-Case 'fan duplicated CLI session refused' {$o=New-FakeOptions;$script:fakeArgv=@($fakeExe,'--ai-fan-mock-session',$session,'--ai-fan-mock-session',$session);Expect-Code {Get-RogFanNamespace $o $script:rows[401]} 'CAPABILITY_UNVERIFIED'}
Test-Case 'fan paired SID mismatch refused' {$o=New-FakeOptions;$script:fakeArgv=@($fakeExe,'--ai-fan-mock-session',$session,'--ai-cpu-isolated-session','bad');Expect-Code {Get-RogFanNamespace $o $script:rows[401]} 'PAIRED_CLI_SESSION_MISMATCH'}
Test-Case 'fan CLI equals spoof refused' {$o=New-FakeOptions;$script:fakeArgv=@($fakeExe,('--ai-fan-mock-session='+$session));Expect-Code {Get-RogFanNamespace $o $script:rows[401]} 'CLI_TARGET_IDENTITY_UNVERIFIED'}
function New-FakeFanReply {
 return [pscustomobject]@{sessionId=$session;parentPid=401;requestId='request-1';sequence=3;action='on';ok=$true;timestampUtc=$birth;
 evidence=[pscustomobject]@{source='mock';mode='mock-handshake';hostPid=601;hostCreationTime100ns=$script:fakeFileTime;runtimeVerified=$true;hardwareWritesEnabled=$false;hardwareWritesObserved=$false;mockZeroHardwareEvidence=$true;hostMode='mock-handshake';protocolVersion='2';mockControlEnabled=$true;mockControlSequence=1;leaseHeld=$true;token='PRIVATE_TOKEN';lease='PRIVATE_LEASE';arbitrary='PRIVATE_EVIDENCE'}}
}
$hostPath=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($fakeExe))) 'PowerControl\fan-host-v2\YeManFanHost.exe'
$script:rows[601]=[pscustomobject]@{ProcessId=601;ExecutablePath=$hostPath;ParentProcessId=401;CreationDate=(Convert-RogUtc $birth);owner=$script:sid}
Test-Case 'fan reply strict mock zero writes FILETIME safe projection' {$o=New-FakeOptions @{Action='fan-on'};$r=Confirm-RogFanReply $o (New-FakeFanReply) 'request-1' 3 (Convert-RogUtc $birth);Assert-Test ($r.runtimeVerified -and $r.zeroPhysicalWritesVerified);Assert-Test (($r|ConvertTo-Json) -notmatch '(?i)PRIVATE_|token|lease')}
Test-Case 'fan physical write true refused' {$o=New-FakeOptions @{Action='fan-on'};$r=New-FakeFanReply;$r.evidence.hardwareWritesObserved=$true;Expect-Code {Confirm-RogFanReply $o $r 'request-1' 3 (Convert-RogUtc $birth)} 'FAN_ZERO_PHYSICAL_WRITE_READBACK_UNVERIFIED'}
Test-Case 'fan false string spoof refused' {$o=New-FakeOptions @{Action='fan-on'};$r=New-FakeFanReply;$r.evidence.hardwareWritesEnabled='false';Expect-Code {Confirm-RogFanReply $o $r 'request-1' 3 (Convert-RogUtc $birth)} 'FAN_ZERO_PHYSICAL_WRITE_READBACK_UNVERIFIED'}
Test-Case 'fan reused host FILETIME refused' {$o=New-FakeOptions @{Action='fan-on'};$r=New-FakeFanReply;$r.evidence.hostCreationTime100ns='134000000000000001';Expect-Code {Confirm-RogFanReply $o $r 'request-1' 3 (Convert-RogUtc $birth)} 'FAN_HOST_FILETIME_CHANGED'}
Test-Case 'fan wrong host parent refused' {$o=New-FakeOptions @{Action='fan-on'};$script:rows[601].ParentProcessId=999;Expect-Code {Confirm-RogFanReply $o (New-FakeFanReply) 'request-1' 3 (Convert-RogUtc $birth)} 'FAN_HOST_PATH_OR_PARENT_CHANGED';$script:rows[601].ParentProcessId=401}
Test-Case 'fan request mismatch refused' {$o=New-FakeOptions @{Action='fan-on'};$r=New-FakeFanReply;$r.requestId='bad';Expect-Code {Confirm-RogFanReply $o $r 'request-1' 3 (Convert-RogUtc $birth)} 'FAN_RESPONSE_EXACT_REQUEST_UNVERIFIED'}
function Write-FixtureManifest($Override=$null) {
 $files=@();foreach($f in ($kitFiles+@('Collect-YMCC-CPU.ps1')+$docFiles)) {$p=Join-Path $fixtureRoot $f;$files+=@{path=$f;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash;bytes=(Get-Item -LiteralPath $p).Length}}
 if($Override){& $Override $files};$m=@{version='10.20261004';sourceRole='portable-ROG-collector-toolkit';localToolValidation=@{status='PASSED';scope='local-software-tool-validation-only';rogRegressionPassed=$false};rogRegressionStatus='NOT_RUN';files=$files}
 [IO.File]::WriteAllText((Join-Path $fixtureRoot 'kit-manifest.json'),($m|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
}
Test-Case 'manifest copied hashes sizes checked not ROG proof' {Write-FixtureManifest;$r=Invoke-FakeEntry @{};Assert-Test ($r.code -eq 0 -and $r.result.details.manifest.verified -and -not $r.result.rogRegressionPassed)}
Test-Case 'manifest mismatch safe failure' {Write-FixtureManifest {param($f) $f[0].sha256='0'*64};$r=Invoke-FakeEntry @{};Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'MANIFEST_FILE_MISMATCH')}
Test-Case 'manifest traversal refused' {Write-FixtureManifest {param($f) $f[0].path='..\private.json'};$r=Invoke-FakeEntry @{};Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'MANIFEST_PATH_REJECTED')}
Test-Case 'manifest false ROG passed refused' {Write-FixtureManifest;$p=Join-Path $fixtureRoot 'kit-manifest.json';$m=Read-RogJson $p;$m.rogRegressionStatus='PASSED';[IO.File]::WriteAllText($p,($m|ConvertTo-Json -Depth 10));$r=Invoke-FakeEntry @{};Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'MANIFEST_PROVENANCE_INVALID')}
Test-Case 'fixtures did not sleep or call real sampler or process control' {Assert-Test ($script:tickCalls -eq 0 -and $script:elevateCalls -eq 2 -and $script:startCalls -eq 2)}
Test-Case 'CLI LogFile multiple explicit values parsed like StartArgument' {
    $o=Convert-RogInvocation @('-Action','observe','-LogFile','relative log.txt','中文.log','-Seconds','1')
    Assert-Test ($o.LogFile.Count -eq 2 -and $o.Seconds -eq '1')
}
Test-Case 'LogFile forwarded only to inert collector metadata interface' {
    Reset-FakeTarget
    $r=Invoke-FakeEntry @{Action='observe';ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth;LogFile=@('explicit log.txt','中文.log')}
    Assert-Test ($r.code -eq 0)
    $summary=Read-RogJson (Join-Path ([IO.Path]::GetDirectoryName($r.path)) 'capture\summary.json')
    Assert-Test ($summary.logFileCount -eq 2 -and -not $summary.realCapturePerformed)
}
Test-Case 'LogFile wildcard failure never exports original path' {
    $r=Invoke-FakeEntry @{LogFile=@('PRIVATE_LOG_PATH\*.log')}
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'LOG_FILE_ARGUMENT_INVALID')
    Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_LOG_PATH')
}
Test-Case 'default sample interval matches 250ms task book' { $o=Get-RogOptions @{} $fixtureRoot; Assert-Test ($o.SampleMilliseconds -eq 250) }
Test-Case 'reparse directory gate uses fake filesystem item no junction created' {
    function Test-Path { param($LiteralPath,$PathType) return $true }
    function Get-Item { param($LiteralPath,[switch]$Force,$ErrorAction) return [pscustomobject]@{Attributes=[IO.FileAttributes]::ReparsePoint} }
    Expect-Code { Assert-RogNoReparse $fixtureRoot } 'REPARSE_PATH_REJECTED'
}
Test-Case 'normal exit timeout with fake owned child no force or restart' {
    Reset-FakeTarget
    $child=[pscustomobject]@{ProcessId=402;ExecutablePath=(Join-Path ([IO.Path]::GetDirectoryName($fakeExe)) 'owned.exe');CreationDate=(Convert-RogUtc $birth);ParentProcessId=401;owner=$script:sid}
    $script:rows[402]=$child
    function Get-CimInstance { param($ClassName,$Filter,$ErrorAction) if ($Filter -eq 'ParentProcessId=401') { return $child }; return @() }
    $o=New-FakeOptions @{Action='restart';AuthorizedAction='restart';GraceSeconds=1}
    $before=$script:startCalls
    Expect-Code { Invoke-RogLifecycle $o (New-RogContext $fixtureRoot 'restart') } 'NORMAL_EXIT_TIMEOUT_OWNED_TREE_NOT_CLOSED_NO_FORCE'
    Assert-Test ($script:startCalls -eq $before); Reset-FakeTarget
}
Test-Case 'capture root gate binds externally trusted hash and exact process' {
    $o=New-FakeOptions @{Action='capture';ExpectedSessionId=$session}
    $p=Join-Path $fixtureRoot 'synthetic-start.jsonl'
    $row=@{sessionId=$session;action='ymcc.start';applied=$true;targetPid=401;targetExe=$fakeExe;targetCreationTimeUtc=$birth;targetSha256=$fakeHash;targetPrivilegeVerified=$true;timestampUtc=[DateTime]::UtcNow.ToString('o')}
    Write-RogNewJson $p $row; [IO.File]::WriteAllText($p,((Read-RogJson $p)|ConvertTo-Json -Compress)); $o.LifecycleJsonl=$p
    Assert-RogCaptureRootEvidence $o
    $row.targetPid=999; [IO.File]::WriteAllText($p,($row|ConvertTo-Json -Compress))
    Expect-Code { Assert-RogCaptureRootEvidence $o } 'CONTROL_EVIDENCE_EXACT_ROOT_UNVERIFIED'
}
Test-Case 'capture root gate refuses stale prior success' {
    $o=New-FakeOptions @{Action='capture';ExpectedSessionId=$session}
    $p=Join-Path $fixtureRoot 'synthetic-stale-start.jsonl'
    Write-RogNewJson $p @{sessionId=$session;action='ymcc.start';applied=$true;targetPid=401;targetExe=$fakeExe;targetCreationTimeUtc=$birth;targetSha256=$fakeHash;targetPrivilegeVerified=$true;timestampUtc=[DateTime]::UtcNow.AddHours(-1).ToString('o')}
    [IO.File]::WriteAllText($p,((Read-RogJson $p)|ConvertTo-Json -Compress)); $o.LifecycleJsonl=$p
    Expect-Code { Assert-RogCaptureRootEvidence $o } 'CONTROL_EVIDENCE_NOT_FRESH'
}
Test-Case 'worker mock success is single-use exact user proof not product regression' {
    $c=New-RogContext $fixtureRoot 'stop'; $o=New-FakeOptions @{Action='stop';AuthorizedAction='stop'}
    $p=Join-Path $c.Directory 'worker-request.json'; Write-RogNewJson $p @{schemaVersion=1;nonce=[guid]::NewGuid().ToString('N');requesterSid=$script:sid;options=$o;createdUtc=[DateTime]::UtcNow.ToString('o');runId=$c.RunId}
    function Invoke-RogFiniteOperation { param($Options,$Context) return @{synthetic=$true;runtimeReadyVerified=$false} }
    $out=@(Invoke-RogWorker $p $fixtureRoot); $r=Read-RogJson (Join-Path $c.Directory 'worker-result.json')
    Assert-Test ($out[-1] -eq 0 -and $r.ok -and $r.verifiedHigh -and -not $r.rogRegressionPassed)
    $before=(Get-FileHash -LiteralPath (Join-Path $c.Directory 'worker-result.json')).Hash
    $repeat=@(Invoke-RogWorker $p $fixtureRoot)
    Assert-Test ($repeat[-1] -ne 0 -and (Get-FileHash -LiteralPath (Join-Path $c.Directory 'worker-result.json')).Hash -eq $before)
}
Test-Case 'manifest must bind all adjacent docs including OPERATOR-COMMANDS' {
    Write-FixtureManifest
    $path=Join-Path $fixtureRoot 'docs\unlisted-fixture.md'
    [IO.File]::WriteAllText($path,'INERT_EXTRA_DOC')
    $r=Invoke-FakeEntry @{}
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'MANIFEST_DOC_NOT_BOUND')
}
Test-Case 'LogFile bounded to same 32-file core limit' {
    $logs=@(1..32 | ForEach-Object { 'explicit-'+$_+'.log' })
    $o=Get-RogOptions @{LogFile=$logs} $fixtureRoot; Assert-Test ($o.LogFile.Count -eq 32)
    Expect-Code { Get-RogOptions @{LogFile=($logs+@('file-33.log'))} $fixtureRoot } 'LOG_FILE_COUNT_LIMIT'
}
# V10 bounded read-only discovery/elevation regressions. All CIM/process/token/UAC seams stay fake.
function Reset-ReadOnlyFixture {
    Reset-FakeTarget; $script:high=$false; $script:elevationMode='execute-worker'; $script:elevationDenied=$false
    $script:workerTimedOut=$false; $script:workerProofMismatch=$false; $script:workerMissingProof=$false
    $script:inHighWorker=$false; $script:fakeWorkerHigh=$true; $script:denyLowTargetReads=$false
    $script:parentMetadataReads=0; $script:workerMetadataReads=0; $script:parentHashReads=0; $script:workerHashReads=0
    $script:parentProcessReads=0; $script:workerProcessReads=0; $script:parentOwnerReads=0; $script:workerOwnerReads=0
    $script:fakeFiniteCalls=0
}
function Get-ReadOnlyArgs([string]$Action='status') { return @{Action=$Action;ExePath=$fakeExe;TargetPid=401} }
Test-Case 'v10 version and manifest fixture use v10 task name' {
    $c=New-RogContext $fixtureRoot 'validate'; $r=New-RogResult $c
    Assert-Test ($r.toolVersion -ceq 'rog-portable-runner-2')
    Assert-Test ($docFiles -contains 'docs\ROG-CAPTURE-AI-TASK-v10-20261004.md')
}
Test-Case 'v10 nonadmin status default never elevates and discovers PID birth' {
    Reset-ReadOnlyFixture; $before=$script:elevateCalls
    $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -eq 0 -and $script:elevateCalls -eq $before)
    Assert-Test (-not $r.result.diagnostics.permission.verifiedHigh -and -not $r.result.diagnostics.elevation.attempted)
    Assert-Test ($r.result.diagnostics.elevation.decision -eq 'NOT_REQUESTED' -and $r.result.diagnostics.identity.acquired)
    Assert-Test ($r.result.details.root.creationTimeUtc -ceq $birth -and -not $r.result.details.target.trustedHashSupplied -and -not $r.result.details.target.discoveredHashIsAuthorization)
}
Test-Case 'v10 nonadmin preflight default never elevates or probes features' {
    Reset-ReadOnlyFixture; $before=$script:elevateCalls
    function Invoke-RogCollector { throw 'SAMPLING_FORBIDDEN' }
    function Invoke-RogLifecycle { throw 'LIFECYCLE_FORBIDDEN' }
    function Invoke-RogFan { throw 'FAN_FORBIDDEN' }
    function Get-RogFanNamespace { throw 'FAN_NAMESPACE_FORBIDDEN' }
    $r=Invoke-FakeEntry (Get-ReadOnlyArgs 'preflight')
    Assert-Test ($r.code -eq 0 -and $script:elevateCalls -eq $before -and $r.result.details.status.identityAcquired)
    Assert-Test (-not $r.result.details.fan.reachability.checked -and -not $r.result.details.fan.functionalOnAttempted)
}
Test-Case 'v10 readonly elevation flag without exact authorization is rejected before attempt' {
    Reset-ReadOnlyFixture; $before=$script:elevateCalls
    foreach ($action in @('preflight','status')) {
        $o=Get-ReadOnlyArgs $action; $o.AllowElevation=$true
        $r=Invoke-FakeEntry $o
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'USER_ACTION_AUTHORIZATION_REQUIRED')
        Assert-Test ($r.result.diagnostics.elevation.requested -and -not $r.result.diagnostics.elevation.authorized -and -not $r.result.diagnostics.elevation.attempted)
        Assert-Test ($r.result.diagnostics.elevation.decision -eq 'AUTHORIZATION_MISSING')
    }
    Assert-Test ($script:elevateCalls -eq $before)
}
Test-Case 'v10 readonly mismatched authorization including case is rejected' {
    Reset-ReadOnlyFixture; $before=$script:elevateCalls
    foreach ($auth in @('preflight','STATUS')) {
        $o=Get-ReadOnlyArgs; $o.AllowElevation=$true; $o.AuthorizedAction=$auth; $r=Invoke-FakeEntry $o
        Assert-Test ($r.code -ne 0 -and -not $r.result.diagnostics.elevation.attempted)
    }
    Assert-Test ($script:elevateCalls -eq $before)
}
Test-Case 'v10 authorization alone is not an elevation request' {
    Reset-ReadOnlyFixture; $before=$script:elevateCalls; $o=Get-ReadOnlyArgs; $o.AuthorizedAction='status'
    $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -eq 0 -and $r.result.diagnostics.elevation.authorized -and -not $r.result.diagnostics.elevation.requested -and $script:elevateCalls -eq $before)
}
Test-Case 'v10 already high readonly with both flags does not spawn another worker' {
    Reset-ReadOnlyFixture; $script:high=$true; $before=$script:elevateCalls
    $o=Get-ReadOnlyArgs; $o.AllowElevation=$true; $o.AuthorizedAction='status'; $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -eq 0 -and $r.result.diagnostics.elevation.decision -eq 'ALREADY_HIGH_NO_WORKER' -and $script:elevateCalls -eq $before)
}
foreach ($requestedAction in @('preflight','status')) {
    Test-Case ('v10 authorized fake RunAs '+$requestedAction+' discovers with no trusted hash PID or birth') {
        Reset-ReadOnlyFixture; $before=$script:elevateCalls
        $o=@{Action=$requestedAction;ExePath=$fakeExe;AllowElevation=$true;AuthorizedAction=$requestedAction}
        $r=Invoke-FakeEntry $o
        Assert-Test ($r.code -eq 0 -and $script:elevateCalls -eq $before+1)
        Assert-Test ($r.result.diagnostics.elevation.bindingVerified -and $r.result.diagnostics.elevation.workerExited -and $r.result.diagnostics.elevation.workerHighVerified)
        Assert-Test ($r.result.diagnostics.identity.acquired -and $r.result.workerResultRelativePath -eq 'worker-result.json')
        $worker=Read-RogJson (Join-Path ([IO.Path]::GetDirectoryName($r.path)) 'worker-result.json')
        $status=if($requestedAction -eq 'preflight'){$worker.details.status}else{$worker.details}
        Assert-Test ($worker.ok -and $worker.identityAcquired -and $status.root.pid -eq 401 -and $status.root.creationTimeUtc -ceq $birth)
        Assert-Test (-not $status.target.trustedHashSupplied -and -not $status.target.discoveredHashIsAuthorization)
        Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_|CommandLine"')
    }
}
Test-Case 'v10 authorized preflight worker never samples controls or calls fan helper' {
    Reset-ReadOnlyFixture
    function Invoke-RogCollector { throw 'SAMPLING_FORBIDDEN' }; function Invoke-RogLifecycle { throw 'LIFECYCLE_FORBIDDEN' }
    function Invoke-RogFan { throw 'FAN_FORBIDDEN' }; function Get-RogFanNamespace { throw 'FAN_NAMESPACE_FORBIDDEN' }
    $o=Get-ReadOnlyArgs 'preflight'; $o.AllowElevation=$true; $o.AuthorizedAction='preflight'; $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -eq 0 -and $r.result.diagnostics.identity.acquired)
}
Test-Case 'v10 explicit PID no birth discovers actual UTC without filling input' {
    Reset-ReadOnlyFixture; $o=Get-RogOptions (Get-ReadOnlyArgs) $fixtureRoot; $s=Get-RogStatus $o
    Assert-Test ($s.identityAcquired -and $s.root.creationTimeUtc -ceq $birth -and $o.TargetCreationTimeUtc -eq '')
}
Test-Case 'v10 provided birth is validated exactly no estimated local time' {
    Reset-ReadOnlyFixture; $o=Get-ReadOnlyArgs; $o.TargetCreationTimeUtc=(Convert-RogUtc $birth).AddTicks(1)
    $r=Invoke-FakeEntry $o; Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_PROCESS_BIRTH_CHANGED' -and -not $r.result.diagnostics.identity.acquired)
    $o.TargetCreationTimeUtc='23:08'; $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_CREATION_TIME_FORMAT_INVALID')
}
Test-Case 'v10 input birth null and empty only permit read-only discovery' {
    Reset-ReadOnlyFixture
    foreach ($value in @($null,'','  ')) {
        $o=Get-ReadOnlyArgs; $o.TargetCreationTimeUtc=$value; $r=Invoke-FakeEntry $o
        Assert-Test ($r.code -eq 0 -and $r.result.diagnostics.identity.acquired -and $r.result.diagnostics.identity.requestedBirthState -eq 'NOT_SUPPLIED')
    }
}
Test-Case 'v10 invalid birth types including false zero never silently treated as empty' {
    Reset-ReadOnlyFixture; $before=$script:rowQueries
    foreach ($value in @(0,$false,12345,[pscustomobject]@{opaque='PRIVATE_BIRTH'})) {
        $o=Get-ReadOnlyArgs; $o.TargetCreationTimeUtc=$value; $r=Invoke-FakeEntry $o
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_CREATION_TIME_TYPE_INVALID' -and $r.result.diagnostics.identity.requestedBirthState -eq 'TYPE_INVALID')
        Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_BIRTH')
    }
    Assert-Test ($script:rowQueries -eq $before)
}
Test-Case 'v10 CIM birth unavailable retains path PID owner facts not permission-only claim' {
    Reset-ReadOnlyFixture; $script:rows[401].CreationDate=$null; $r=Invoke-FakeEntry (Get-ReadOnlyArgs 'preflight')
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PROCESS_BIRTH_UNAVAILABLE')
    Assert-Test ($r.result.facts.status.candidates[0].pid -eq 401 -and $r.result.facts.status.candidates[0].currentUserVerified)
    Assert-Test ($r.result.diagnostics.identity.availableFields -contains 'exePath' -and $r.result.diagnostics.identity.availableFields -notcontains 'creationTimeUtc')
    Assert-Test ($r.result.facts.status.root -eq $null -and -not $r.result.diagnostics.identity.acquired -and $r.result.diagnostics.phase -eq 'identity-query')
}
Test-Case 'v10 CIM birth empty type invalid and kind invalid have fixed separate codes' {
    foreach ($rowCase in @(@{value='';code='PROCESS_BIRTH_UNAVAILABLE'},@{value=123;code='PROCESS_BIRTH_TYPE_INVALID'},@{value=[DateTime]::SpecifyKind((Convert-RogUtc $birth),[DateTimeKind]::Unspecified);code='PROCESS_BIRTH_KIND_INVALID'})) {
        Reset-ReadOnlyFixture; $script:rows[401].CreationDate=$rowCase.value; $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq $rowCase.code -and -not $r.result.diagnostics.identity.acquired)
    }
}
Test-Case 'v10 DMTF and local-culture birth strings are not guessed or converted' {
    foreach ($value in @('20261004230800.123456+480','10/4/2026 23:08','PRIVATE_DATE')) {
        Reset-ReadOnlyFixture; $script:rows[401].CreationDate=$value; $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PROCESS_BIRTH_FORMAT_INVALID')
        Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_DATE|20261004230800')
    }
}
Test-Case 'v10 UTC DateTime DateTimeOffset JSON and cultures preserve precise birth' {
    $old=[Threading.Thread]::CurrentThread.CurrentCulture
    try {
        foreach($culture in @('zh-CN','en-US')) {
            [Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]::GetCultureInfo($culture)
            foreach ($value in @((Convert-RogUtc $birth),[DateTimeOffset]::new((Convert-RogUtc $birth)),$birth)) {
                Reset-ReadOnlyFixture; $script:rows[401].CreationDate=$value; $o=Get-ReadOnlyArgs; $o.TargetCreationTimeUtc=$value
                $r=Invoke-FakeEntry $o; Assert-Test ($r.code -eq 0 -and $r.result.details.root.creationTimeUtc -ceq $birth)
            }
        }
    } finally { [Threading.Thread]::CurrentThread.CurrentCulture=$old }
}
Test-Case 'v10 missing path is unavailable not assumed access denied and retains actual birth' {
    Reset-ReadOnlyFixture; $script:rows[401].ExecutablePath=$null; $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PROCESS_PATH_UNAVAILABLE')
    Assert-Test ($r.result.facts.status.candidates[0].creationTimeUtc -ceq $birth -and $r.result.facts.status.unknownCandidateCount -eq 1 -and -not $r.result.diagnostics.identity.acquired)
}
Test-Case 'v10 invalid path type never string-cast into a target' {
    Reset-ReadOnlyFixture; $script:rows[401].ExecutablePath=123; $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PROCESS_PATH_TYPE_INVALID')
}
Test-Case 'v10 actual query denial is distinct from generic query failure' {
    Reset-ReadOnlyFixture
    function Get-RogProcessRow { param($ProcessId) throw [UnauthorizedAccessException]::new('PRIVATE_ACCESS_DENIAL') }
    $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PROCESS_QUERY_DENIED' -and $r.result.diagnostics.identity.state -eq 'QUERY_FAILED')
    Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_ACCESS_DENIAL')
}
Test-Case 'v10 generic CIM error is not falsely named a permission failure' {
    Reset-ReadOnlyFixture; function Get-RogProcessRow { param($ProcessId) throw 'PRIVATE_QUERY_FAILURE' }
    $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PROCESS_QUERY_FAILED')
    Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_QUERY_FAILURE')
}
Test-Case 'v10 unavailable owner preserves other fields but no identity' {
    Reset-ReadOnlyFixture; function Get-RogOwnerSid { param($Row) return $null }
    $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PROCESS_OWNER_UNAVAILABLE' -and $r.result.facts.status.candidates[0].creationTimeUtc -ceq $birth)
}
Test-Case 'v10 invalid owner type separate from missing or denied' {
    Reset-ReadOnlyFixture; function Get-RogOwnerSid { param($Row) return 123 }
    $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PROCESS_OWNER_TYPE_INVALID')
}
Test-Case 'v10 actual owner helper ReturnValue denial and malformed type are classified' {
    function Invoke-CimMethod { param($InputObject,$MethodName,$OperationTimeoutSec,$ErrorAction) return [pscustomobject]@{ReturnValue=2;Sid=$null} }
    Expect-Code { & $script:sourceOwnerFunction ([pscustomobject]@{}) } 'PROCESS_OWNER_QUERY_DENIED'
    function Invoke-CimMethod { param($InputObject,$MethodName,$OperationTimeoutSec,$ErrorAction) return [pscustomobject]@{ReturnValue='0';Sid=$script:sid} }
    Expect-Code { & $script:sourceOwnerFunction ([pscustomobject]@{}) } 'PROCESS_OWNER_TYPE_INVALID'
}
Test-Case 'v10 explicit candidate PID foreign path is refused without echoing foreign path' {
    Reset-ReadOnlyFixture; $script:rows[401].ExecutablePath=Join-Path $fixtureRoot 'PRIVATE_FOREIGN\YeManCC.exe'
    $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_PROCESS_PATH_CHANGED' -and -not $r.result.diagnostics.identity.acquired)
    Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_FOREIGN')
}
Test-Case 'v10 explicit candidate PID foreign owner is refused' {
    Reset-ReadOnlyFixture; $script:rows[401].owner='S-1-5-21-900-800-700-1002'; $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_NOT_CURRENT_USER' -and -not $r.result.diagnostics.identity.acquired)
}
Test-Case 'v10 broad discovery excludes known foreign candidates no false root' {
    Reset-ReadOnlyFixture; $script:rows[401].owner='S-1-5-21-900-800-700-1002'
    $r=Invoke-FakeEntry @{Action='status';ExePath=$fakeExe}
    Assert-Test ($r.code -eq 0 -and $r.result.details.rootCount -eq 0 -and $r.result.details.excludedCandidateCount -eq 1 -and -not $r.result.details.identityAcquired)
}
Test-Case 'v10 ambiguous current-user roots preserve candidates but never choose one' {
    Reset-ReadOnlyFixture; $script:rows[402]=[pscustomobject]@{ProcessId=402;ExecutablePath=$fakeExe;CreationDate=(Convert-RogUtc $birth).AddSeconds(1);owner=$script:sid;CommandLine='PRIVATE_SECOND_CMD'}
    $r=Invoke-FakeEntry @{Action='status';ExePath=$fakeExe}
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_ROOT_AMBIGUOUS' -and $r.result.facts.status.rootCount -eq 2 -and $r.result.facts.status.root -eq $null)
    Assert-Test (-not $r.result.diagnostics.identity.acquired -and [IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_SECOND_CMD')
}
Test-Case 'v10 one complete root plus unknown candidate still no unique identity' {
    Reset-ReadOnlyFixture; $script:rows[402]=[pscustomobject]@{ProcessId=402;ExecutablePath=$null;CreationDate=$null;owner=$script:sid}
    $r=Invoke-FakeEntry @{Action='status';ExePath=$fakeExe}
    Assert-Test ($r.code -ne 0 -and $r.result.facts.status.rootCount -eq 1 -and $r.result.facts.status.unknownCandidateCount -eq 1 -and -not $r.result.diagnostics.identity.acquired)
}
Test-Case 'v10 unknown fields with RootCount zero cannot authorize later start' {
    Reset-ReadOnlyFixture; $script:rows[401].ExecutablePath=$null
    $o=New-FakeOptions @{Action='start';AuthorizedAction='start';TargetPid=0;TargetCreationTimeUtc=''}; $before=$script:startCalls
    Expect-Code { Start-RogVerifiedTarget $o } 'PROCESS_PATH_UNAVAILABLE'
    Assert-Test ($script:startCalls -eq $before)
}
Test-Case 'v10 token query unavailable does not suppress read-only identity or claim denied' {
    Reset-ReadOnlyFixture; function Get-RogPrivilege { throw 'PRIVATE_TOKEN_QUERY_FAILURE' }
    $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -eq 0 -and $r.result.diagnostics.identity.acquired -and -not $r.result.diagnostics.permission.querySucceeded -and $r.result.diagnostics.permission.errorCode -eq 'TOKEN_QUERY_FAILED')
    Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_TOKEN_QUERY_FAILURE')
}
Test-Case 'v10 rejected RunAs retains target facts and exact attempted diagnostics' {
    Reset-ReadOnlyFixture; $script:elevationDenied=$true; $o=Get-ReadOnlyArgs; $o.AllowElevation=$true; $o.AuthorizedAction='status'
    $r=Invoke-FakeEntry $o; $script:elevationDenied=$false
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'ELEVATION_DENIED_OR_TOOL_POLICY_REFUSED')
    Assert-Test ($null -eq $r.result.facts.target.observedSha256 -and -not $r.result.facts.target.metadataVerified -and $r.result.facts.target.exePath -ceq $fakeExe)
    Assert-Test ($script:parentMetadataReads -eq 0 -and $script:parentHashReads -eq 0 -and $script:parentProcessReads -eq 0)
    Assert-Test ($r.result.diagnostics.elevation.attempted -and $r.result.diagnostics.elevation.workerExited -eq $null)
    Assert-Test ($r.result.diagnostics.phase -eq 'elevation-launch' -and [IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_DENIAL')
}
Test-Case 'v10 real worker dispatcher fake birth failure retains partial worker facts and parent binding' {
    Reset-ReadOnlyFixture; $script:rows[401].CreationDate=$null; $o=Get-ReadOnlyArgs 'preflight'; $o.AllowElevation=$true; $o.AuthorizedAction='preflight'
    $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PROCESS_BIRTH_UNAVAILABLE')
    Assert-Test ($r.result.diagnostics.elevation.bindingVerified -and $r.result.diagnostics.elevation.workerExited -and $r.result.diagnostics.elevation.workerExitCode -eq 1 -and $r.result.diagnostics.elevation.workerActionPhase -eq 'identity-query')
    $worker=Read-RogJson (Join-Path ([IO.Path]::GetDirectoryName($r.path)) 'worker-result.json')
    Assert-Test ($worker.facts.status.candidates[0].pid -eq 401 -and $worker.facts.status.candidates[0].currentUserVerified -and -not $worker.identityAcquired)
    Assert-Test ($r.result.diagnostics.identity.availableFields -contains 'pid' -and $r.result.workerResultRelativePath -eq 'worker-result.json')
}
Test-Case 'v10 bound worker opaque error or details never projected' {
    Reset-ReadOnlyFixture; $script:elevationMode='synthetic-failure'; $script:workerErrorCode='PRIVATE_UPPERCASE_SECRET'
    $o=Get-ReadOnlyArgs; $o.AllowElevation=$true; $o.AuthorizedAction='status'; $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'WORKER_ACTION_FAILED' -and $r.result.diagnostics.elevation.bindingVerified)
    Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_|CommandLine"|token"')
}
Test-Case 'v10 mismatched worker nonce rejected before error or facts projection' {
    Reset-ReadOnlyFixture; $script:elevationMode='synthetic-failure'; $script:workerErrorCode='PROCESS_QUERY_DENIED'; $script:workerProofMismatch=$true
    $o=Get-ReadOnlyArgs; $o.AllowElevation=$true; $o.AuthorizedAction='status'; $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'WORKER_PROOF_UNVERIFIED' -and -not $r.result.diagnostics.elevation.bindingVerified)
}
Test-Case 'v10 worker timeout is bounded no force and preserves attempt fields' {
    Reset-ReadOnlyFixture; $script:elevationMode='synthetic'; $script:workerTimedOut=$true
    $o=Get-ReadOnlyArgs; $o.AllowElevation=$true; $o.AuthorizedAction='status'; $before=$script:elevateCalls; $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'FINITE_WORKER_TIMEOUT_NO_FORCE' -and $script:elevateCalls -eq $before+1)
    Assert-Test ($r.result.diagnostics.elevation.workerExited -eq $false -and $r.result.diagnostics.elevation.timeoutMilliseconds -eq 180000)
}
Test-Case 'v10 missing worker proof distinct from attempted refusal or action failure' {
    Reset-ReadOnlyFixture; $script:elevationMode='synthetic'; $script:workerMissingProof=$true
    $o=Get-ReadOnlyArgs; $o.AllowElevation=$true; $o.AuthorizedAction='status'; $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'WORKER_PROOF_MISSING' -and $r.result.diagnostics.elevation.workerExited)
}
Test-Case 'v10 only read-only actions may use untrusted discovered hash' {
    Reset-ReadOnlyFixture; $before=$script:elevateCalls
    foreach ($action in @('start','stop','restart','observe','capture','fan-probe')) {
        $r=Invoke-FakeEntry @{Action=$action;ExePath=$fakeExe;TargetPid=401;AuthorizedAction=$action;AllowElevation=$true}
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TRUSTED_PRODUCT_HASH_REQUIRED')
    }
    Assert-Test ($script:elevateCalls -eq $before)
}
Test-Case 'v10 non-readonly absent birth stays exact identity gate before any RunAs' {
    Reset-ReadOnlyFixture; $before=$script:elevateCalls
    foreach ($action in @('stop','restart','observe','capture','fan-probe')) {
        $r=Invoke-FakeEntry @{Action=$action;ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;AuthorizedAction=$action;AllowElevation=$true}
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'EXACT_PROCESS_IDENTITY_REQUIRED')
    }
    Assert-Test ($script:elevateCalls -eq $before)
}
Test-Case 'v10 observe capture elevation require exact authorization even if already high' {
    Reset-ReadOnlyFixture; $script:high=$true; $before=$script:elevateCalls
    foreach ($action in @('observe','capture')) {
        $r=Invoke-FakeEntry @{Action=$action;ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth;AllowElevation=$true}
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'USER_ACTION_AUTHORIZATION_REQUIRED')
    }
    Assert-Test ($script:elevateCalls -eq $before)
}
Test-Case 'v10 attempted diagnostics are this tool invocation not a machine-wide denial claim' {
    Reset-ReadOnlyFixture; $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -eq 0 -and -not $r.result.productAcceptancePassed -and -not $r.result.rogRegressionPassed)
    Assert-Test (-not $r.result.sideEffectsRequested -and -not $r.result.diagnostics.elevation.attempted)
}
# Final descriptor boundary: three negative cases only; ordinary local/UNC forms remain lexical descriptors.
foreach ($descriptorKind in @('wildcard','device','ads')) {
    Test-Case ('v10 descriptor '+$descriptorKind+' refused before token target reads or RunAs') {
        Reset-ReadOnlyFixture; $script:denyLowTargetReads=$true; $before=$script:elevateCalls; $beforeToken=$script:privilegeCalls
        $driveRoot=[IO.Path]::GetPathRoot($fakeExe)
        $badPath=switch ($descriptorKind) {
            'wildcard' { $driveRoot+'ordinary*?directory\YeManCC.exe' }
            'device' { '\\.\pipe\YeManCC.exe' }
            'ads' { $driveRoot+'ordinary:alternate\YeManCC.exe' }
        }
        Expect-Code { Assert-RogTargetDescriptor @{ExePath=$badPath;ExpectedSha256=''} } 'TARGET_PATH_DESCRIPTOR_REJECTED'
        $r=Invoke-FakeEntry @{Action='status';ExePath=$badPath;AllowElevation=$true;AuthorizedAction='status'}
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_PATH_DESCRIPTOR_REJECTED')
        Assert-Test ($script:elevateCalls -eq $before -and $script:privilegeCalls -eq $beforeToken -and $script:parentMetadataReads -eq 0 -and $script:parentHashReads -eq 0 -and $script:parentProcessReads -eq 0)
        # Extended Win32 namespace is blocked by the same gate, without another action case.
        if ($descriptorKind -eq 'device') { Expect-Code { Assert-RogTargetDescriptor @{ExePath='\\?\C:\ordinary\YeManCC.exe';ExpectedSha256=''} } 'TARGET_PATH_DESCRIPTOR_REJECTED' }
        $ordinary=Assert-RogTargetDescriptor @{ExePath=$fakeExe;ExpectedSha256=''}
        $unc=Assert-RogTargetDescriptor @{ExePath='\\offline-fixture-host\share\ordinary 中文\YeManCC.exe';ExpectedSha256=''}
        Assert-Test (-not $ordinary.metadataVerified -and -not $unc.metadataVerified -and $null -eq $unc.observedSha256)
    }
}
# Simulate Framework path rejection on every engine. The normalizer must remain uncalled:
# checking only the resulting error code would miss a reordered guard with the same safe mapping.
Test-Case 'v10.1 raw descriptor refused before throwing PS5 normalizer token target or RunAs' {
    Reset-ReadOnlyFixture; $script:denyLowTargetReads=$true
    $beforeToken=$script:privilegeCalls; $beforeElevation=$script:elevateCalls; $script:normalizationCalls=0
    function Resolve-RogTargetPath {
        param($Path,$Root)
        $script:normalizationCalls++
        throw [ArgumentException]::new('PRIVATE_PS5_NORMALIZER_EXCEPTION')
    }
    $driveRoot=[IO.Path]::GetPathRoot($fakeExe)
    $badLiterals=@(($driveRoot+'ordinary*?directory\YeManCC.exe'),'relative*?directory\YeManCC.exe',
        ($driveRoot+'ordinary:alternate\YeManCC.exe'),'relative:alternate\YeManCC.exe',
        '\\offline-fixture-host\share\ordinary:alternate\YeManCC.exe','\\.\pipe\YeManCC.exe',
        '\\?\C:\ordinary\YeManCC.exe','//./pipe/YeManCC.exe')
    foreach ($badLiteral in $badLiterals) {
        Expect-Code { Assert-RogPathDescriptorLiteral $badLiteral } 'TARGET_PATH_DESCRIPTOR_REJECTED'
        Expect-Code { Assert-RogTargetDescriptor @{ExePath=$badLiteral;ExpectedSha256=''} -RequireTrusted } 'TARGET_PATH_DESCRIPTOR_REJECTED'
        Expect-Code { Get-RogOptions @{Action='status';ExePath=$badLiteral} $fixtureRoot } 'TARGET_PATH_DESCRIPTOR_REJECTED'
        $r=Invoke-FakeEntry @{Action='restart';ExePath=$badLiteral;AllowElevation=$true;AuthorizedAction='restart'}
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -ceq 'TARGET_PATH_DESCRIPTOR_REJECTED')
        Assert-Test (-not $r.result.diagnostics.permission.queryAttempted -and -not $r.result.diagnostics.elevation.attempted)
        Assert-Test ($script:normalizationCalls -eq 0) 'NORMALIZER_CALLED_BEFORE_LITERAL_REJECTION'
    }
    Assert-Test ($script:privilegeCalls -eq $beforeToken -and $script:elevateCalls -eq $beforeElevation)
    Assert-Test ($script:parentMetadataReads -eq 0 -and $script:parentHashReads -eq 0 -and $script:parentProcessReads -eq 0 -and $script:parentOwnerReads -eq 0 -and $script:fakeFiniteCalls -eq 0)
}
Test-Case 'v10.1 legal relative absolute UNC Chinese spaces need no early trusted hash' {
    $relative=Get-RogOptions @{Action='status';ExePath='产品 中文\YeManCC.exe'} $fixtureRoot
    $absolute=Get-RogOptions @{Action='status';ExePath=$fakeExe} $fixtureRoot
    $uncLiteral='\\offline-fixture-host\share\ordinary 中文\YeManCC.exe'
    $unc=Get-RogOptions @{Action='status';ExePath=$uncLiteral} $fixtureRoot
    Assert-Test ($relative.ExePath -ceq $fakeExe -and $absolute.ExePath -ceq $fakeExe -and $unc.ExePath -ceq $uncLiteral)
    foreach ($o in @($relative,$absolute,$unc)) {
        $descriptor=Assert-RogTargetDescriptor $o
        Assert-Test (-not $descriptor.metadataVerified -and -not $descriptor.trustedHashSupplied -and $null -eq $descriptor.observedSha256)
    }
}
Test-Case 'v10.1 genuine invalid NUL syntax maps fixed code before token target or RunAs' {
    Reset-ReadOnlyFixture; $script:denyLowTargetReads=$true
    $beforeToken=$script:privilegeCalls; $beforeElevation=$script:elevateCalls
    $badLiteral=[IO.Path]::GetPathRoot($fakeExe)+'PRIVATE_INVALID_SYNTAX'+[char]0+'\YeManCC.exe'
    Assert-RogPathDescriptorLiteral $badLiteral # Not wildcard/device/ADS: actual normalization must reject it.
    $pathApiThrew=$false
    try { [IO.Path]::GetFullPath($badLiteral) | Out-Null } catch { $pathApiThrew=$true }
    Assert-Test $pathApiThrew 'INVALID_SYNTAX_FIXTURE_MUST_THROW'
    Expect-Code { Get-RogOptions @{Action='status';ExePath=$badLiteral} $fixtureRoot } 'TARGET_PATH_DESCRIPTOR_REJECTED'
    $r=Invoke-FakeEntry @{Action='status';ExePath=$badLiteral;AllowElevation=$true;AuthorizedAction='status'}
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -ceq 'TARGET_PATH_DESCRIPTOR_REJECTED')
    Assert-Test ($script:privilegeCalls -eq $beforeToken -and $script:elevateCalls -eq $beforeElevation)
    Assert-Test (-not $r.result.diagnostics.permission.queryAttempted -and -not $r.result.diagnostics.elevation.attempted)
    Assert-Test ($script:parentMetadataReads -eq 0 -and $script:parentHashReads -eq 0 -and $script:parentProcessReads -eq 0 -and $script:parentOwnerReads -eq 0)
    Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_INVALID_SYNTAX|Exception|ArgumentException')
}
Test-Case 'v10.1 AST locks raw guard before the only normalization boundary and descriptor Path APIs' {
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $sourceRoot 'Invoke-ROG-CPU.ps1'),[ref]$tokens,[ref]$errors)
    $optionsFunction=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Get-RogOptions'},$true)
    $exeBlocks=@($optionsFunction.Body.FindAll({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Clauses[0].Item1.Extent.Text -ceq '$o.ExePath'},$true))
    Assert-Test ($exeBlocks.Count -eq 1) 'EXE_NORMALIZATION_BLOCK_REQUIRED'
    $commands=@($exeBlocks[0].FindAll({param($n) $n -is [Management.Automation.Language.CommandAst]},$true))
    Assert-Test ($commands.Count -eq 2 -and $commands[0].GetCommandName() -ceq 'Assert-RogPathDescriptorLiteral' -and $commands[1].GetCommandName() -ceq 'Resolve-RogTargetPath') 'RAW_GATE_MUST_PRECEDE_NORMALIZATION_SEAM'
    $inlineCalls=@($exeBlocks[0].FindAll({param($n) $n -is [Management.Automation.Language.InvokeMemberExpressionAst]},$true))
    Assert-Test ($inlineCalls.Count -eq 0) 'NORMALIZATION_MUST_NOT_BYPASS_MOCKABLE_SEAM'
    $descriptorFunction=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Assert-RogTargetDescriptor'},$true)
    $gate=$descriptorFunction.Body.Find({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'Assert-RogPathDescriptorLiteral'},$true)
    $pathCalls=@($descriptorFunction.Body.FindAll({param($n) $n -is [Management.Automation.Language.InvokeMemberExpressionAst] -and $n.Expression.Extent.Text -ceq '[IO.Path]'},$true))
    Assert-Test ($null -ne $gate -and $pathCalls.Count -eq 2)
    foreach ($call in $pathCalls) { Assert-Test ($gate.Extent.StartOffset -lt $call.Extent.StartOffset) 'DESCRIPTOR_LITERAL_GATE_MUST_PRECEDE_PATH_API' }
}
# Bootstrap review gates: low parent target readers genuinely deny, high worker readers are fake/inert only.
Test-Case 'v10 CIM uint32 PID broad discovery accepted' {
    Reset-ReadOnlyFixture; $script:rows[401].ProcessId=[uint32]401
    $r=Invoke-FakeEntry @{Action='status';ExePath=$fakeExe}
    Assert-Test ($r.code -eq 0 -and $r.result.details.root.pid -eq 401 -and $r.result.details.identityAcquired)
}
Test-Case 'v10 CIM uint32 PID exact TargetPid match accepted' {
    Reset-ReadOnlyFixture; $script:rows[401].ProcessId=[uint32]401
    $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -eq 0 -and $r.result.details.root.pid -eq 401 -and $r.result.details.root.creationTimeUtc -ceq $birth)
}
Test-Case 'v10 CIM PID only bounded signed int uint32 long not float string overflow' {
    foreach ($valid in @([int]1,[uint32]401,[long]2147483647)) { Assert-Test (Test-RogPidValue $valid) }
    foreach ($invalid in @([uint32]2147483648,[uint32]::MaxValue,[long]2147483648,0,-1,401.0,'401',$null)) {
        Assert-Test (-not (Test-RogPidValue $invalid))
        Reset-ReadOnlyFixture; $script:rows[401].ProcessId=$invalid
        $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
        Assert-Test ($r.code -ne 0 -and -not $r.result.diagnostics.identity.acquired)
        Assert-Test ($r.result.errorCode -in @('PROCESS_ID_TYPE_INVALID','PROCESS_ID_UNAVAILABLE'))
    }
}
Test-Case 'v10 CIM PID mismatch refused sequence protocol not broadened for uint32' {
    Reset-ReadOnlyFixture; $script:rows[401].ProcessId=[uint32]402
    $r=Invoke-FakeEntry (Get-ReadOnlyArgs)
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'TARGET_IDENTITY_UNAVAILABLE')
    Assert-Test (-not (Test-RogSequenceValue ([uint32]401)))
}
Test-Case 'v10 CIM owner uint32 zero success int long statuses remain compatible' {
    foreach ($value in @([uint32]0,[int]0,[long]0)) {
        $script:fakeOwnerReturn=$value
        function Invoke-CimMethod { param($InputObject,$MethodName,$OperationTimeoutSec,$ErrorAction) return [pscustomobject]@{ReturnValue=$script:fakeOwnerReturn;Sid=$script:sid} }
        Assert-Test ((& $script:sourceOwnerFunction ([pscustomobject]@{})) -ceq $script:sid)
    }
}
Test-Case 'v10 CIM owner uint32 denied failed malformed classifications' {
    foreach ($value in @([uint32]2,[uint32]3,[uint32]1,[uint32]::MaxValue,[long]4294967296,0.0,'0')) {
        $script:fakeOwnerReturn=$value
        function Invoke-CimMethod { param($InputObject,$MethodName,$OperationTimeoutSec,$ErrorAction) return [pscustomobject]@{ReturnValue=$script:fakeOwnerReturn;Sid=$script:sid} }
        $code=if($value -is [uint32]) {if($value -in @(2,3)){'PROCESS_OWNER_QUERY_DENIED'}else{'PROCESS_OWNER_QUERY_FAILED'}}else{'PROCESS_OWNER_TYPE_INVALID'}
        Expect-Code { & $script:sourceOwnerFunction ([pscustomobject]@{}) } $code
    }
}
foreach ($reviewAction in @('preflight','status')) {
    Test-Case ('v10 low protected metadata and process '+$reviewAction+' authorized worker discovers no hash') {
        Reset-ReadOnlyFixture; $script:denyLowTargetReads=$true; $before=$script:elevateCalls
        function Invoke-RogCollector { throw 'SAMPLING_FORBIDDEN' }; function Invoke-RogLifecycle { throw 'LIFECYCLE_FORBIDDEN' }; function Invoke-RogFan { throw 'FAN_FORBIDDEN' }
        # Demonstrate the same reader actually refuses low access; reset the audited counters afterward.
        Expect-Code { Assert-RogTarget (Get-RogOptions (Get-ReadOnlyArgs $reviewAction) $fixtureRoot) } 'PERMISSION_DENIED'
        Expect-Code { Get-RogProcessRow 401 } 'PROCESS_QUERY_DENIED'
        Expect-Code { Get-RogOwnerSid $script:rows[401] } 'PROCESS_OWNER_QUERY_DENIED'
        $script:parentMetadataReads=0; $script:parentProcessReads=0; $script:parentOwnerReads=0
        $o=Get-ReadOnlyArgs $reviewAction; $o.AllowElevation=$true; $o.AuthorizedAction=$reviewAction
        $r=Invoke-FakeEntry $o
        Assert-Test ($r.code -eq 0 -and $script:elevateCalls -eq $before+1 -and $r.result.diagnostics.identity.acquired)
        Assert-Test ($script:parentMetadataReads -eq 0 -and $script:parentHashReads -eq 0 -and $script:parentProcessReads -eq 0 -and $script:parentOwnerReads -eq 0)
        Assert-Test ($script:workerMetadataReads -gt 0 -and $script:workerHashReads -gt 0 -and $script:workerProcessReads -gt 0 -and $script:workerOwnerReads -gt 0)
        $w=Read-RogJson (Join-Path ([IO.Path]::GetDirectoryName($r.path)) 'worker-result.json')
        $status=if($reviewAction -eq 'preflight'){$w.details.status}else{$w.details}
        Assert-Test ($w.verifiedHigh -and $status.root.pid -eq 401 -and $status.root.creationTimeUtc -ceq $birth)
        Assert-Test ($null -eq $r.result.facts.target.observedSha256 -and -not $r.result.facts.target.metadataVerified -and -not $status.target.discoveredHashIsAuthorization)
    }
    Test-Case ('v10 low protected '+$reviewAction+' no flag fails locally never elevates') {
        Reset-ReadOnlyFixture; $script:denyLowTargetReads=$true; $before=$script:elevateCalls
        $r=Invoke-FakeEntry (Get-ReadOnlyArgs $reviewAction)
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PERMISSION_DENIED' -and $script:elevateCalls -eq $before -and -not $r.result.diagnostics.elevation.attempted)
    }
}
Test-Case 'v10 low protected read-only missing authorization neither reads target nor launches' {
    Reset-ReadOnlyFixture; $script:denyLowTargetReads=$true; $before=$script:elevateCalls
    $o=Get-ReadOnlyArgs; $o.AllowElevation=$true; $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'USER_ACTION_AUTHORIZATION_REQUIRED' -and $script:elevateCalls -eq $before)
    Assert-Test ($script:parentMetadataReads -eq 0 -and $script:parentHashReads -eq 0 -and $script:parentProcessReads -eq 0 -and $script:parentOwnerReads -eq 0)
}
foreach ($reviewAction in @('observe','capture','restart','stop','fan-probe','start')) {
    Test-Case ('v10 low protected '+$reviewAction+' complete selectors reach high worker before real gates') {
        Reset-ReadOnlyFixture; $script:denyLowTargetReads=$true; $before=$script:elevateCalls
        function Invoke-RogFiniteOperation($Options,$Context) {
            Assert-Test ($script:inHighWorker -and $Context.Result.verifiedHigh)
            Assert-Test ($Context.Result.facts.target.metadataVerified -and $Context.Result.facts.target.hashMatchVerified)
            if ($Options.Action -ne 'start') { Assert-Test ($script:workerProcessReads -gt 0 -and $script:workerOwnerReads -gt 0) }
            $script:fakeFiniteCalls++; return @{synthetic=$true;productActionPerformed=$false;samplingPerformed=$false}
        }
        $o=@{Action=$reviewAction;ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth;AuthorizedAction=$reviewAction;AllowElevation=$true}
        if ($reviewAction -eq 'start') { $o.TargetPid=0; $o.TargetCreationTimeUtc='' }
        $r=Invoke-FakeEntry $o
        Assert-Test ($r.code -eq 0 -and $script:elevateCalls -eq $before+1 -and $script:fakeFiniteCalls -eq 1 -and $r.result.diagnostics.elevation.bindingVerified)
        Assert-Test ($script:parentMetadataReads -eq 0 -and $script:parentHashReads -eq 0 -and $script:parentProcessReads -eq 0 -and $script:parentOwnerReads -eq 0)
        Assert-Test ($script:workerMetadataReads -gt 0 -and $script:workerHashReads -gt 0)
    }
}
foreach ($reviewAction in @('observe','restart')) {
    Test-Case ('v10 low protected '+$reviewAction+' no elevation flag refuses before finite operation') {
        Reset-ReadOnlyFixture; $script:denyLowTargetReads=$true; $before=$script:elevateCalls
        function Invoke-RogFiniteOperation { $script:fakeFiniteCalls++; throw 'FINITE_OPERATION_FORBIDDEN' }
        $r=Invoke-FakeEntry @{Action=$reviewAction;ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth;AuthorizedAction=$reviewAction}
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'PERMISSION_DENIED' -and $script:elevateCalls -eq $before -and $script:fakeFiniteCalls -eq 0)
    }
}
Test-Case 'v10 low process-only denial does not block authorized observe restart parent' {
    foreach ($action in @('observe','restart')) {
        Reset-ReadOnlyFixture
        function Get-RogProcessRow([int]$ProcessId) {
            if (-not $script:inHighWorker) { $script:parentProcessReads++; throw 'PROCESS_QUERY_DENIED' }
            $script:workerProcessReads++; return $script:rows[$ProcessId]
        }
        function Get-RogOwnerSid($Row) {
            if (-not $script:inHighWorker) { $script:parentOwnerReads++; throw 'PROCESS_OWNER_QUERY_DENIED' }
            $script:workerOwnerReads++; return $script:sid
        }
        function Invoke-RogFiniteOperation { $script:fakeFiniteCalls++; return @{synthetic=$true} }
        $r=Invoke-FakeEntry @{Action=$action;ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth;AuthorizedAction=$action;AllowElevation=$true}
        Assert-Test ($r.code -eq 0 -and $script:parentProcessReads -eq 0 -and $script:parentOwnerReads -eq 0 -and $script:workerProcessReads -gt 0 -and $script:workerOwnerReads -gt 0)
    }
}
Test-Case 'v10 already high exact target is actually validated locally no worker' {
    Reset-ReadOnlyFixture; $script:high=$true; $before=$script:elevateCalls
    function Invoke-RogFiniteOperation { $script:fakeFiniteCalls++; return @{synthetic=$true} }
    $r=Invoke-FakeEntry @{Action='observe';ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth;AuthorizedAction='observe';AllowElevation=$true}
    Assert-Test ($r.code -eq 0 -and $script:elevateCalls -eq $before -and $script:parentMetadataReads -gt 0 -and $script:parentHashReads -gt 0 -and $script:parentProcessReads -gt 0 -and $script:parentOwnerReads -gt 0)
}
Test-Case 'v10 worker unverified high token fails before target metadata hash CIM owner' {
    Reset-ReadOnlyFixture; $script:fakeWorkerHigh=$false; $script:denyLowTargetReads=$true
    function Invoke-RogFiniteOperation { $script:fakeFiniteCalls++; throw 'FINITE_OPERATION_FORBIDDEN' }
    $o=Get-ReadOnlyArgs; $o.AllowElevation=$true; $o.AuthorizedAction='status'; $r=Invoke-FakeEntry $o
    Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq 'WORKER_HIGH_TOKEN_UNVERIFIED' -and $r.result.diagnostics.elevation.bindingVerified)
    Assert-Test ($r.result.diagnostics.elevation.workerActionPhase -eq 'permission-verification' -and $r.result.diagnostics.elevation.workerHighVerified -eq $false)
    Assert-Test ($script:workerMetadataReads -eq 0 -and $script:workerHashReads -eq 0 -and $script:workerProcessReads -eq 0 -and $script:workerOwnerReads -eq 0 -and $script:fakeFiniteCalls -eq 0)
}
foreach ($fault in @('hash','path','birth','owner')) {
    Test-Case ('v10 high worker actual '+$fault+' gate fails before synthetic product operation') {
        Reset-ReadOnlyFixture; $script:denyLowTargetReads=$true
        function Invoke-RogFiniteOperation { $script:fakeFiniteCalls++; throw 'FINITE_OPERATION_FORBIDDEN' }
        $o=@{Action='restart';ExePath=$fakeExe;ExpectedSha256=$fakeHash;TargetPid=401;TargetCreationTimeUtc=$birth;AuthorizedAction='restart';AllowElevation=$true}
        switch ($fault) {
            'hash' { $o.ExpectedSha256='0'*64; $expected='TARGET_SHA256_MISMATCH' }
            'path' { $script:rows[401].ExecutablePath=Join-Path $fixtureRoot 'PRIVATE_FOREIGN\YeManCC.exe'; $expected='TARGET_PROCESS_PATH_CHANGED' }
            'birth' { $script:rows[401].CreationDate=(Convert-RogUtc $birth).AddTicks(1); $expected='TARGET_PROCESS_BIRTH_CHANGED' }
            'owner' { $script:rows[401].owner='S-1-5-21-900-800-700-1002'; $expected='TARGET_NOT_CURRENT_USER' }
        }
        $r=Invoke-FakeEntry $o
        Assert-Test ($r.code -ne 0 -and $r.result.errorCode -eq $expected -and $script:fakeFiniteCalls -eq 0 -and $r.result.diagnostics.elevation.bindingVerified)
        Assert-Test ($script:parentMetadataReads -eq 0 -and $script:parentHashReads -eq 0 -and $script:parentProcessReads -eq 0 -and $script:parentOwnerReads -eq 0)
        Assert-Test ([IO.File]::ReadAllText($r.path) -notmatch 'PRIVATE_|CommandLine"')
    }
}
Reset-ReadOnlyFixture
Reset-FakeTarget; $script:elevationMode='synthetic'; $script:workerTimedOut=$false; $script:workerMissingProof=$false; $script:workerProofMismatch=$false
$failed=@($script:cases | Where-Object {-not $_.passed})
$report=[ordered]@{schemaVersion=1;toolVersion='rog-portable-runner-2';attemptFieldsScope='this-tool-invocation-only-not-machine-wide';productVersionConfirmed=$false;toolRelease='v10.1';rogActions='NOT_EXECUTED_BY_OFFLINE_FIXTURE';regressionFieldsScope='this-offline-fixture-only-not-other-chats-or-global-product-state';timestampUtc=[DateTime]::UtcNow.ToString('o');powershellVersion=$PSVersionTable.PSVersion.ToString();scope='local-software-tool-validation-only';localToolValidation=($failed.Count -eq 0);rogRegressionPassed=$false;rogToolRegression='NOT_RUN';rogProductRegression='NOT_RUN';rogCpuAB='NOT_RUN';realProductProcessesTouched=$false;realElevationPerformed=$false;realCpuSamplingPerformed=$false;realPortCallsPerformed=$false;casesTotal=$script:cases.Count;casesPassed=$script:cases.Count-$failed.Count;casesFailed=$failed.Count;cases=$script:cases.ToArray();fixtureMode='temporary inert wrappers + synthetic collector + fake process/control/native seams';fixtureRetainedForAudit=$true}
$sourceFiles=@()
foreach ($name in ($kitFiles+@('tests\Test-ROG-CPU-Runner.ps1'))) {
    $path=Join-Path $sourceRoot $name
    $sourceFiles+=@{path=$name;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $path).Length}
}
$report.validatedSourceFiles=$sourceFiles
$report.collectorExecution='INERT_FIXTURE_ONLY_REAL_COLLECTOR_VALIDATION_DELEGATED'
$report.wrapperSubprocessPrivilege='FAKE_TOKEN_NO_NATIVE_NO_SYSTEM_TARGET_READS'
$report.bootstrapCoverage='LOW_PARENT_DENIED_METADATA_HASH_PROCESS_OWNER_SEAMS;HIGH_WORKER_REAL_VALIDATION_OF_INERT_FILE_AND_FAKE_CIM;FINITE_PRODUCT_ACTIONS_SYNTHETIC'
$report.realNativeCallsPerformed=$false
$validationClock.Stop(); $report.runtimeMilliseconds=$validationClock.Elapsed.TotalMilliseconds
$report.fixtureRoot=$fixtureRoot
[void][IO.Directory]::CreateDirectory($ValidationDirectory)
$name='runner-validation-v10.1-'+$PSVersionTable.PSVersion.ToString()+'-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
Write-RogNewJson (Join-Path $ValidationDirectory ($name+'.json')) $report
$md=@('# Portable runner offline validation',('PowerShell: '+$report.powershellVersion),('Passed/total: '+$report.casesPassed+'/'+$report.casesTotal),'Scope: local-software-tool-validation-only','rogRegressionPassed=false; ROG tool/product/CPU A-B: NOT_RUN for this offline fixture only; no global or other-chat product-state claim','No real product start/close/elevation/sampling/port/native calls. Copied wrappers use an inert collector.','Temporary fixtures retained outside checkout for audit; original collector/tests and product files unchanged.') -join [Environment]::NewLine
[IO.File]::WriteAllText((Join-Path $ValidationDirectory ($name+'.md')),$md,[Text.UTF8Encoding]::new($false))
Write-Output ('VALIDATION_REPORT '+(Join-Path $ValidationDirectory ($name+'.json')))
if ($failed.Count) {exit 1};exit 0
