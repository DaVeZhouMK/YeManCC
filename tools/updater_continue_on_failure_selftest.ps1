param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($EvidenceDir)
if (Test-Path -LiteralPath $root) { throw 'Use an unused scratch directory' }
$null=[IO.Directory]::CreateDirectory($root)
$sourceDir=Join-Path $root 'package'; $target=Join-Path $root 'install'; $rollbackFiles=Join-Path $root 'rollback'
$helper=Join-Path $root 'emitted-helper.ps1'; $config=Join-Path $root 'config.json'
@{install=$target;staging=$sourceDir;script=$helper;progress=(Join-Path $root 'progress.json');parentPid=1;version='0.0.41'} |
    ConvertTo-Json | Set-Content -LiteralPath $config -Encoding UTF8
& node (Join-Path $PSScriptRoot 'updater_helper_extract.mjs') (Join-Path $PSScriptRoot '../native/main.cpp') $config
if ($LASTEXITCODE) { throw 'Helper extraction failed' }
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($helper,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors.Message -join '; ') }
foreach ($name in @('Resolve-YmccDeckyPath','Get-YmccDeckyHash','Assert-YmccUpdateTargetWritable','Set-YmccDeckyAtomic',
    'Add-YmccInstallIssue','Clear-YmccManagedReadOnly','Copy-YmccManagedFile','Copy-YmccManagedTree','Restore-OrdinaryFiles',
    'Write-YmccInstallState','Write-Utf8Atomic','Set-UpdateProgress','Assert-FileMatch','Assert-TreeMatch','Copy-TreeChecked',
    'Rename-DirectoryChecked','Undo-YmccTdpUpdate','Test-AdditionalLayoutRootPayloads','Copy-AdditionalLayoutRootsChecked')) {
    $defs=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true))
    if ($defs.Count -ne 1) { throw "Missing or ambiguous function: $name" }
    . ([scriptblock]::Create($defs[0].Extent.Text))
}
function Need([bool]$Pass,[string]$Message) { if (!$Pass) { throw $Message } }
function Put([string]$Base,[string]$Relative,[string]$Value) {
    $path=Join-Path $Base $Relative
    $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path,$Value)
    return $path
}
$configPath=Put $target '01-config.json' '{ broken'
[IO.File]::SetAttributes($configPath,[IO.FileAttributes]::ReadOnly)
$lockedPath=Put $target '02-locked.bin' 'old locked'
$dirPath=Put $target '03-wrong-type.json\old.txt' 'directory data'
$userPath=Put $target 'Sleep\player-blacklist.txt' 'keep byte for byte'
foreach ($file in @('01-config.json','02-locked.bin','03-wrong-type.json','04-following.bin','Sleep\player-blacklist.txt')) {
    $null=Put $sourceDir $file ('new '+$file)
}
$ordinaryFileLedger=[Collections.Generic.List[object]]::new()
$script:ymccInstallIssues=[ordered]@{}; $script:ymccCoreWriteFailures=0
$lock=[IO.File]::Open($lockedPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
try {
    Copy-YmccManagedTree $sourceDir $target 'PowerControl' @('Sleep\player-blacklist.txt')
    Need ($script:ymccInstallIssues['PowerControl'] -eq 1 -and $script:ymccInstallIssues['old-file-backup'] -eq 1) 'locked optional file not reported'
    Need ($script:ymccCoreWriteFailures -eq 0) 'optional lock marked core failure'
    Need ([IO.File]::ReadAllText($configPath) -ceq 'new 01-config.json') 'bad read-only config was not covered'
    Need ([IO.File]::ReadAllText((Join-Path $target '03-wrong-type.json')) -ceq 'new 03-wrong-type.json') 'old wrong-type directory blocked replacement'
    Need ([IO.File]::ReadAllText((Join-Path $target '04-following.bin')) -ceq 'new 04-following.bin') 'copy stopped after optional failure'
    Need ([IO.File]::ReadAllText($userPath) -ceq 'keep byte for byte') 'excluded user data was changed'
    $core=Put $sourceDir 'new-exe.bin' 'new core'; $after=Put $sourceDir 'other.bin' 'other core'
    Copy-YmccManagedFile $core $target '02-locked.bin' 'YeManCC' -Core
    Copy-YmccManagedFile $after $target 'later-core.bin' 'YeManCC' -Core
    Need ($script:ymccCoreWriteFailures -eq 1 -and [IO.File]::Exists((Join-Path $target 'later-core.bin'))) 'locked core file did not allow following file, or was not reported'
} finally { $lock.Dispose() }
$missing=Put $sourceDir 'YeManCC.exe' 'new executable bytes'
Copy-YmccManagedFile $missing $target 'YeManCC.exe' 'YeManCC' -Core
Need ([IO.File]::ReadAllText((Join-Path $target 'YeManCC.exe')) -ceq 'new executable bytes') 'missing old executable blocked new bytes'
Restore-OrdinaryFiles
Need ([IO.File]::ReadAllText($configPath) -ceq '{ broken') 'original config rollback failed'
Need ([bool]([IO.File]::GetAttributes($configPath) -band [IO.FileAttributes]::ReadOnly)) 'original read-only bit was lost'
Need ([IO.File]::ReadAllText($dirPath) -ceq 'directory data') 'isolated directory rollback failed'
Need ([IO.File]::ReadAllText($lockedPath) -ceq 'old locked') 'locked preimage changed'
Need (![IO.File]::Exists((Join-Path $target 'YeManCC.exe'))) 'new file survived rollback'
Need (!@(Get-ChildItem -LiteralPath $target -Filter '.ymcc-incoming*' -Force).Count) 'temporary file leaked'
$version='0.0.41'
$state=Join-Path $root 'update-state.json'; [IO.File]::WriteAllText($state,'old state')
$stateLock=[IO.File]::Open($state,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
try { Write-YmccInstallState 'launching' } finally { $stateLock.Dispose() }
Need ($script:ymccInstallIssues['write-state'] -eq 1 -and [IO.File]::ReadAllText($state) -ceq 'old state') 'locked diagnostic state was not isolated from install'
$coreSource=Join-Path $root 'core-package'; $coreTarget=Join-Path $root 'core-install'
$oldConfig=Put $coreTarget 'app.config.json' 'bad legacy config'
$null=Put $coreSource 'app.config.json' 'new config'; $null=Put $coreSource 'index.html' 'new UI'
$oldConfigLock=[IO.File]::Open($oldConfig,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
try { Copy-YmccManagedTree $coreSource $coreTarget 'Program' @() -Core } finally { $oldConfigLock.Dispose() }
Need ($script:ymccInstallIssues['Program'] -eq 1 -and $script:ymccCoreWriteFailures -eq 1 -and [IO.File]::Exists((Join-Path $coreTarget 'index.html'))) 'ordinary app config blocked core UI copy or increased the core failure count'
$emitted=[IO.File]::ReadAllText($helper)
$gate=$emitted.IndexOf("throw ('UPDATE_CORE_FILES_INCOMPLETE:")
Need ($gate -gt $emitted.IndexOf('Copy-YmccManagedTree $programSource') -and $gate -lt $emitted.IndexOf('Start-Process -FilePath $exePath -WorkingDirectory $exeDir -ArgumentList')) 'core failure must be tested after copy but before launch'
Write-Output 'PASS: overwrite, optional/core locks, later copies, rollback, protected user file, and diagnostic failure'

# Run the production top-level install/catch/finally. Hardware, foreign processes,
# package authorization, and new-process startup are explicit inert test ports.
$installer=@($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] -and $_.Body.Extent.Text.Contains('Enter-YmccModuleMaintenance 15000') })
Need ($installer.Count -eq 1) 'ambiguous installer statement'
$installBody=[scriptblock]::Create($installer[0].Extent.Text)
function Enter-YmccModuleMaintenance { 'fixture-lease' }
function Exit-YmccModuleMaintenance($Lease) { Need ($Lease -ceq 'fixture-lease') 'maintenance lease missing' }
function Set-YmccInstallCheckpoint([string]$Step) { $script:ymccInstallStep=$Step; $steps.Add($Step) }
function Set-YmccInstallJournal([string]$Phase) { $phases.Add($Phase) }
function Write-YmccInstallTrace { }
function Stop-YmccUpdateHookTools { throw 'simulated tool auto-restart' }
function Stop-YmccUpdaterParent { 'forced-owned-parent' }
function Stop-AdditionalLayoutRootProcesses { }
function Stop-ResidentProcesses { throw 'simulated resident refuses to exit' }
function Get-UpdateLayoutRoots { @() }
function Assert-YmccModuleManifest { }
function Assert-YmccDeckyManifest { }
function Get-YmccModulePlan { throw 'simulated HIDM module in use' }
function Get-YmccDeckyPlan { throw 'DECKY_BACKEND_IN_USE_OR_UNAVAILABLE' }
function Restore-YmccDeckyPackage { }
function Get-Process([int]$Id,$ErrorAction) { if ($Id -eq 5000) { @{Id=$Id} } }
function Start-Process($FilePath,$WorkingDirectory,$ArgumentList,[switch]$PassThru) {
    if (!$ArgumentList) { $hostObserved.Add('relaunch-old'); return @{Id=5100} }
    $hostObserved.Add('launch-new')
    Need ([IO.File]::ReadAllText($FilePath) -ceq 'new core EXE') 'stale core launched'
    Need ([IO.File]::ReadAllText((Join-Path $exeDir 'index.html')) -ceq 'new core UI') 'stale UI launched'
    [IO.File]::WriteAllText($handshakePath,(@{phase='started';pid=5000;token=$handshakeToken}|ConvertTo-Json -Compress))
    return @{Id=5000;HasExited=$false}
}
function Run-InstallCase([string]$Case,[switch]$LockCore,[switch]$AfterRollback) {
    $sandbox=Join-Path $root $Case
    $installRoot=Join-Path $sandbox 'installed';$staging=Join-Path $sandbox 'staging'
    $programSource=Join-Path $staging 'YeManCC';$powerControlSource=Join-Path $staging 'PowerControl'
    $exeDir=Join-Path $installRoot 'YeManCC';$pcDir=Join-Path $installRoot 'PowerControl'
    $packageExe=Put $programSource 'YeManCC.exe' 'new core EXE';$exePath=Join-Path $exeDir 'YeManCC.exe'
    $null=Put $programSource 'index.html' 'new core UI';$oldUi=Join-Path $exeDir 'index.html'
    $null=Put $programSource 'app.config.json' '{}';$oldConfig=Join-Path $exeDir 'app.config.json'
    $null=Put $programSource 'YeMan-Support.html' 'new support';$supportPath=Join-Path $exeDir 'YeMan-Support.html'
    if (!$AfterRollback) {
        $null=Put $exeDir 'YeManCC.exe' 'old core EXE';$null=Put $exeDir 'index.html' 'old core UI'
        $null=Put $exeDir 'app.config.json' 'invalid old JSON';$null=Put $exeDir 'YeMan-Support.html' 'old support'
    }
    $layoutManifestPath=Put $programSource 'update-manifest.json' '{"deckyManaged":{}}'
    $null=Put $programSource 'module-package.json' '{}'
    $systemBlacklistSource=Put $powerControlSource 'Sleep\system-blacklist.txt' 'new system rules'
    $null=Put $powerControlSource 'pawnio\YeManTdpCtl.exe' 'new TDP'
    $null=Put $powerControlSource 'pawnio\_internal\python3.dll' 'new TDP DLL'
    $tdpSource=Join-Path $powerControlSource 'pawnio';$tdpTarget=Join-Path $pcDir 'pawnio'
    if (!$AfterRollback) { $null=Put $tdpTarget 'YeManTdpCtl.exe' 'old TDP' }
    $tdpStage=Join-Path $pcDir 'pawnio.update-fixture';$tdpBackup=Join-Path $pcDir 'pawnio.rollback-fixture'
    $copyRetries=1;$copyWaitSeconds=0;$playerBlacklistExisted=$false
    $fanHostSource=Join-Path $powerControlSource 'fan-host-v2'
    $customSteamLibraryDir=Join-Path $exeDir 'CustomSteamLibrary';$customSteamLibrarySource=Join-Path $programSource 'CustomSteamLibrary'
    $legacyCustomSteamLibrarySource=Join-Path $staging 'CustomSteamLibrary'
    $state=Join-Path $sandbox 'update-state.json';$progressPath=Join-Path $sandbox 'update-progress.json'
    $rollbackRoot=Join-Path $sandbox 'rollback';$rollbackFiles=Join-Path $rollbackRoot 'files'
    $customHealthHandshakePath=Join-Path $staging 'custom-health-fixture.json'
    $handshakePath=Join-Path $sandbox 'handshake.json';$handshakeToken='fixture-token';$handshakeTimeoutSeconds=2
    $scriptPath=Join-Path $sandbox 'not-used-by-fixture.ps1';$deckyResumeArgs=@('--should-not-be-used')
    $operationId='fixture-operation';$sha256='a'*64;$parentPid=4000;$parentCreationFileTime='1'
    $script:ymccInstallIssues=[ordered]@{};$script:ymccCoreWriteFailures=0
    $ordinaryFileLedger=[Collections.Generic.List[object]]::new()
    $steps=[Collections.Generic.List[string]]::new();$phases=[Collections.Generic.List[string]]::new();$hostObserved=[Collections.Generic.List[string]]::new()
    $newProcess=$null;$rollbackChildExitConfirmed=$true;$ordinaryRollbackPrepared=$false
    $immutablePayloadStarted=$false;$immutablePayloadComplete=$false;$immutableRepairPending=$false
    $moduleTransaction=$null;$deckyTransaction=$null;$deckyPreparationLease=$null
    $tdpCommitted=$false;$tdpOriginalMoved=$false;$updateCommitted=$false;$rollbackSucceeded=$false;$recoveryError=$null
    $locked=if($LockCore){$exePath}else{$oldConfig}
    $lock=if(!$AfterRollback){[IO.File]::Open($locked,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)}else{$null}
    try { . $installBody } finally { if($lock){$lock.Dispose()} }
    $receipt=Get-Content -LiteralPath $state -Raw -Encoding UTF8 | ConvertFrom-Json
    Need ($steps.Contains('copy-program') -and $steps.Contains('prepare-launch')) 'an optional failure cut off copying'
    Need ($script:ymccInstallIssues['stop-hooks'] -eq 1 -and $script:ymccInstallIssues['stop-residents'] -eq 1 -and $script:ymccInstallIssues['apply-input'] -eq 1 -and $script:ymccInstallIssues['plan-decky'] -eq 1) 'optional failures not reported'
    if ($LockCore) {
        Need (!$hostObserved.Contains('launch-new') -and $hostObserved.Contains('relaunch-old')) 'core failure did not recover to old app'
        Need ($receipt.phase -eq 'rolled-back' -and $phases[-1] -eq 'repair-required' -and $receipt.error -match 'UPDATE_CORE_FILES_INCOMPLETE') 'failed core incorrectly confirmed'
        Need ($ordinaryFileLedger.Where({$_.relative -eq 'index.html' -and $_.written}).Count -eq 1) 'core lock skipped following UI update'
        Need ([IO.File]::ReadAllText($exePath) -ceq 'old core EXE' -and [IO.File]::ReadAllText($oldUi) -ceq 'old core UI') 'old core did not recover'
        Need ([IO.File]::ReadAllText((Join-Path $tdpTarget 'YeManTdpCtl.exe')) -ceq 'old TDP') 'TDP transaction was not restored'
    } elseif ($AfterRollback) {
        Need ($hostObserved.Contains('launch-new') -and $receipt.phase -eq 'committed' -and $phases[-1] -eq 'committed') 'releasing the lock on the same installation did not allow commit'
        Need ([IO.File]::ReadAllText($oldConfig) -ceq '{}' -and [IO.File]::ReadAllText((Join-Path $tdpTarget 'YeManTdpCtl.exe')) -ceq 'new TDP') 'retry did not install new config and TDP'
    } else {
        Need ($hostObserved.Contains('launch-new') -and $receipt.phase -eq 'committed' -and $phases[-1] -eq 'committed') 'optional lock prevented commit'
        Need ($receipt.issues.YeManCC -eq 1 -and [IO.File]::ReadAllText($oldConfig) -ceq 'invalid old JSON') 'locked config missing issue or changed'
    }
    Write-Output "PASS production install control flow: $Case retry=$AfterRollback"
}
. Run-InstallCase 'forward-optional-failures'
. Run-InstallCase 'reverse-core-lock' -LockCore
. Run-InstallCase 'reverse-core-lock' -AfterRollback
