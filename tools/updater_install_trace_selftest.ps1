param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
$root=[IO.Path]::GetFullPath($EvidenceDir)
if (Test-Path -LiteralPath $root) { throw 'Use a new scratch directory' }
$null=[IO.Directory]::CreateDirectory($root)
$helper=Join-Path $root 'emitted-helper.ps1'
$config=Join-Path $root 'config.json'
@{install=(Join-Path $root 'unused');staging=(Join-Path $root 'stage');script=$helper;
    progress=(Join-Path $root 'update-progress.json');parentPid=1;version='0.0.41'} |
    ConvertTo-Json | Set-Content -LiteralPath $config -Encoding UTF8
& node (Join-Path $PSScriptRoot 'updater_helper_extract.mjs') (Join-Path $PSScriptRoot '../native/main.cpp') $config
if ($LASTEXITCODE) { throw 'Helper extraction failed' }
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($helper,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors.Message -join '; ') }
foreach ($name in @('Test-YmccRecoveryInteger','Resolve-YmccDeckyPath','Write-YmccRecoveryFile','Set-YmccInstallCheckpoint','Write-YmccInstallTrace','Write-YmccInstallState','Add-YmccInstallIssue')) {
    $defs=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true))
    if ($defs.Count -ne 1) { throw "Missing or ambiguous function: $name" }
    . ([scriptblock]::Create($defs[0].Extent.Text))
}
# Use actual startup and catch branches. All product/ACL/authority ports are
# inert; the trace's filesystem serialization, replace and cleanup are real.
$source=[IO.File]::ReadAllText($helper).Replace("`r`n","`n")
$start=$source.IndexOf("  Set-YmccInstallCheckpoint 'launch-new'")
$catch=$source.IndexOf("} catch {`n  `$installError = `$_.Exception.Message",$start)
$end=$source.IndexOf("} finally {`n  try {`n  try { `$safeStage=Resolve-YmccDeckyPath",$catch)
if ($start -lt 0 -or $catch -le $start -or $end -le $catch) { throw 'Startup/catch boundaries changed' }
$branch=[scriptblock]::Create("try {`n"+$source.Substring($start,$end-$start)+"}`n")

function Assert-YmccRecoveryAcl($Path) { }
function Set-Acl($LiteralPath,$AclObject,$ErrorAction) { }
function Get-YmccRecoverySnapshotOnce($Path) {
    @{Document=@{operationId=$operationId;version=$version;sha256=$sha256;authorizedAt=1000}}
}
function Set-YmccInstallJournal($Phase) { $script:journalPhase=$Phase }
function Set-UpdateProgress($Phase,$Message,$ErrorMessage) { }
function Write-Utf8Atomic($Path,$Text) { }
function Get-Process($Id,$ErrorAction) { if ($Id -eq 200) { return @{Id=$Id} } }
function Get-YmccUpdaterProcessIdentity($Process) { @{Creation='1'} }
function Stop-YmccUpdaterParent { if ($scenario -eq 'blocked-child') { throw 'fake child lock' } }
function Restore-YmccDeckyPackage { }
function Restore-OrdinaryFiles { $script:restored=$true; if ($scenario -eq 'rollback-fails') { throw 'fake write error' } }
function Start-Process($FilePath,$WorkingDirectory,$ArgumentList,[switch]$PassThru) {
    if (!$ArgumentList) {
        if ($scenario -eq 'relaunch-fails') { throw 'fake recovery launch failure' }
        return @{Id=300}
    }
    if ($scenario -eq 'launch-fails') { throw [ComponentModel.Win32Exception]::new(5,'private path and token') }
    return @{Id=200;HasExited=($scenario -in @('early-exit','rollback-fails','relaunch-fails'));ExitCode=73}
}
function Need([bool]$Ok,[string]$What) { if (!$Ok) { throw $What } }
function Run-Case([string]$scenario,[string]$expectedError,[string]$expectedRollback,[string]$expectedRelaunch) {
    $dir=Join-Path $root $scenario
    $null=[IO.Directory]::CreateDirectory($dir)
    $operationId='private-operation';$version='0.0.41';$sha256='a'*64
    $recoveryJournal=Join-Path $dir 'test-journal.json';$trace=$recoveryJournal+'.install-trace.json'
    $exeDir=$dir;$exePath=Join-Path $dir 'dummy.exe';[IO.File]::WriteAllText($exePath,'not an executable')
    $backup=Join-Path $dir 'backup';$handshakePath=Join-Path $dir 'handshake.json';$state=Join-Path $dir 'state.json'
    $handshakeToken='private-token';$deckyResumeArgs=@();$parentPid=100;$parentExitMode='forced-owned-parent'
    $deckyPreparationLease=$null;$deckyTransaction=$null;$moduleTransaction=$null;$tdpOriginalMoved=$false;$tdpCommitted=$false
    $newProcess=$null;$rollbackChildExitConfirmed=$true;$ordinaryRollbackPrepared=$true
    $immutablePayloadStarted=$false;$immutablePayloadComplete=$false;$immutableRepairPending=$false
    $updateCommitted=$false;$rollbackSucceeded=$false;$recoveryError=$null
    $script:ymccInstallStep=$null;$script:ymccTraceAuthorizedAt=$null;$script:ymccStartupFailure=$null;$script:ymccStartupExitCode=$null
    $script:ymccInstallIssues=[ordered]@{'apply-decky'=1}
    $script:journalPhase=$null;$script:restored=$false
    $success=$scenario -in @('commit','blocked-write')
    $handshakeTimeoutSeconds=if ($success -or $scenario -in @('early-exit','rollback-fails','relaunch-fails')) { 2 } else { 0 }
    if ($success) {
        @{pid=200;phase='started';token=$handshakeToken} | ConvertTo-Json | Set-Content -LiteralPath $handshakePath
    }
    $lock=$null
    if ($scenario -eq 'blocked-write') {
        [IO.File]::WriteAllText($trace,'locked')
        $lock=[IO.File]::Open($trace,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    }
    try { Need (@(. $branch).Count -eq 0) "$scenario leaked output" }
    finally { if ($lock) { $lock.Dispose() } }
    Need ($updateCommitted -eq $success) "$scenario changed commit behavior"
    Need ($script:restored -eq (!$success -and $scenario -ne 'blocked-child')) "$scenario changed rollback behavior"
    Need (@(Get-ChildItem -LiteralPath $dir -Filter '*.tmp-*').Count -eq 0) "$scenario leaked temp files"
    if ($lock) { Need ([IO.File]::ReadAllText($trace) -ceq 'locked') 'locked preimage changed';return }
    $raw=[IO.File]::ReadAllText($trace);$r=$raw | ConvertFrom-Json
    Need ($r.schemaVersion -eq 1 -and $r.authorizedAt -eq 1000 -and $r.parentExit -eq 'forced-owned-parent') 'trace identity/parent exit missing'
    Need ($r.issues.'apply-decky' -eq 1) 'continuation issue summary missing'
    Need (!$raw.Contains('private-token') -and !$raw.Contains('private path and token')) 'trace leaked raw exception or handshake token'
    if ($success) {
        Need ($r.outcome -eq 'committed' -and $r.step -eq 'commit' -and $script:journalPhase -eq 'committed') 'commit trace missing'
    } else {
        Need ($r.outcome -eq 'failed' -and $r.errorKind -eq $expectedError) "$scenario misclassified failure"
        Need ($r.rollback -eq $expectedRollback -and $r.relaunch -eq $expectedRelaunch) "$scenario wrong rollback or relaunch"
        if ($expectedError -eq 'startup-exited') { Need ($r.exitCode -eq 73) "$scenario exit code missing" }
        if ($expectedError -eq 'startup-launch') { Need ($r.win32Code -eq 5) "$scenario Win32 code missing" }
    }
}
Run-Case 'commit' '' '' ''
Run-Case 'blocked-write' '' '' ''
Run-Case 'launch-fails' 'startup-launch' 'succeeded' 'started'
Run-Case 'early-exit' 'startup-exited' 'succeeded' 'started'
Run-Case 'timeout' 'handshake-timeout' 'succeeded' 'started'
Run-Case 'blocked-child' 'handshake-timeout' 'blocked-child' 'not-attempted'
Run-Case 'rollback-fails' 'startup-exited' 'failed' 'not-attempted'
Run-Case 'relaunch-fails' 'startup-exited' 'succeeded' 'failed'
Write-Output 'PASS: 8 startup, rollback and trace-write cases'
