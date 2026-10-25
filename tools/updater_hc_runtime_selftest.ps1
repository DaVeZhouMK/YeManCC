[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[string]$CanonicalLayoutRoot="",[switch]$InstallerOnly)
$ErrorActionPreference='Stop';$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$ev=[IO.Path]::GetFullPath($EvidenceDir);[IO.Directory]::CreateDirectory($ev)|Out-Null
$helper=Join-Path $ev 'production-helper.ps1';$config=@{install=(Join-Path $ev 'unused-install');staging=(Join-Path $ev 'unused-stage');script=$helper;progress=(Join-Path $ev 'unused-progress.json');version='0.0.36';parentPid=$PID};$configPath=Join-Path $ev 'config.json';$config|ConvertTo-Json|Set-Content -LiteralPath $configPath
& node (Join-Path $repo 'tools/updater_helper_extract.mjs') (Join-Path $repo 'native/main.cpp') $configPath|Out-Null
if($LASTEXITCODE){throw 'HELPER_EXTRACTION_FAILED'}
$tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($helper,[ref]$tokens,[ref]$parseErrors);if($parseErrors.Count){throw ($parseErrors.Message -join ';')}
$needed=@('Normalize-ManifestRelativePath','Copy-TreeChecked','Assert-TreeMatch','Assert-FileMatch','Assert-YmccHcTree','Sync-YmccHcRuntime','Get-YmccSharedRuntimePlan','Sync-YmccSharedRuntime','Invoke-YmccFanPayloadInstaller','Sync-YmccImmutableFanPayload','Remove-YmccSupersededHcRuntimes')
foreach($name in $needed){$functions=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true));if($functions.Count -ne 1){throw ('PRODUCTION_FUNCTION_AMBIGUOUS '+$name)};. ([scriptblock]::Create($functions[0].Extent.Text))}
$copyRetries=0;$copyWaitSeconds=0;$checks=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[scriptblock]$Body){try{&$Body;$checks.Add(@{name=$Name;status='PASS'})}catch{$checks.Add(@{name=$Name;status='FAIL';error=$_.Exception.Message;stack=$_.ScriptStackTrace})}}
function Need([bool]$Ok,[string]$Why){if(!$Ok){throw $Why}}
$source=Join-Path $repo 'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1';$manifest=Join-Path $source 'HandheldCompanion.runtime.json';$sourceHash=(Get-FileHash -LiteralPath $manifest).Hash;$pc=Join-Path $ev 'PowerControl';[IO.Directory]::CreateDirectory($pc)|Out-Null
$target=Join-Path $pc ('handheldcompanion-runtime/'+(Split-Path -Leaf $source))
# Execute the real owned-worker function with inert scripts in this evidence
# root. Privileged payload validation and deployment state are not performed.
$installerHome=Join-Path $ev 'installer 路径 $literal (owned)'
[IO.Directory]::CreateDirectory($installerHome)|Out-Null
$installerScript=Join-Path $installerHome 'install-fan-host-payload.ps1'
Check 'installer literal Unicode and spaced arguments return no pipeline values' {
 [IO.File]::WriteAllText($installerScript,'param([string]$PayloadDirectory) [IO.File]::WriteAllText((Join-Path $PSScriptRoot "argument.txt"),$PayloadDirectory); exit 0',[Text.UTF8Encoding]::new($true))
 $values=@(Invoke-YmccFanPayloadInstaller $installerScript $installerHome)
 Need ($values.Count -eq 0) 'installer leaked pipeline values'
 Need ([IO.File]::ReadAllText((Join-Path $installerHome 'argument.txt')) -ceq $installerHome) 'installer target argument changed'
}
Check 'installer nonzero exit fails within budget' {
 [IO.File]::WriteAllText($installerScript,'param([string]$PayloadDirectory) exit 23',[Text.UTF8Encoding]::new($true))
 $caught=$false;try{Invoke-YmccFanPayloadInstaller $installerScript $installerHome}catch{$caught=$_.Exception.Message -match 'installation failed: 23'}
 Need $caught 'installer nonzero exit accepted'
}
Check 'hung installer is bounded and only its owned child is terminated' {
 [IO.File]::WriteAllText($installerScript,'param([string]$PayloadDirectory) [IO.File]::WriteAllText((Join-Path $PSScriptRoot "child-pid.txt"),[string]$PID); Start-Sleep -Seconds 60',[Text.UTF8Encoding]::new($true))
 $clock=[Diagnostics.Stopwatch]::StartNew();$caught=$false
 try{Invoke-YmccFanPayloadInstaller $installerScript $installerHome 2000}catch{$caught=$_.Exception.Message -match 'FAN_HOST_INSTALLER_TIMEOUT.*ownedWorkerStopped=True'}
 Need $caught 'hung installer did not report a terminated owned worker'
 Need ($clock.ElapsedMilliseconds -lt 7000) 'installer wait exceeded finite budget'
 $ownedPid=[int][IO.File]::ReadAllText((Join-Path $installerHome 'child-pid.txt'))
 Need ($null -eq (Get-Process -Id $ownedPid -ErrorAction SilentlyContinue)) 'owned installer survived timeout'
 [IO.File]::WriteAllText($installerScript,'param([string]$PayloadDirectory) exit 0',[Text.UTF8Encoding]::new($true))
 Invoke-YmccFanPayloadInstaller $installerScript $installerHome
 Need (!$script:YmccFanPayloadInstallerWorker) 'installer retry retained a live worker handle'
 @{elapsedMs=$clock.ElapsedMilliseconds;ownedPid=$ownedPid;retry='PASS'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $ev 'installer-timeout.json')
}
Check 'installer rejects a script outside its exact payload directory' {
 $caught=$false;try{Invoke-YmccFanPayloadInstaller $installerScript $pc}catch{$caught=$_.Exception.Message -eq 'FAN_HOST_INSTALLER_PATH_INVALID'}
 Need $caught 'installer accepted an escaped script'
}
if(!$InstallerOnly){
foreach($cycle in 0..2){
 Check ('incoming HC exact sync cycle '+$cycle) {$syncResult=@(Sync-YmccHcRuntime $source $pc);Need ($syncResult.Count -eq 1 -and $syncResult[0] -ceq $target) 'HC sync must return exactly one target path without robocopy output';Need ((Split-Path -Leaf $syncResult[0]) -ceq (Split-Path -Leaf $source)) 'HC returned target cannot feed the real updater cleanup call';Assert-TreeMatch $source $target 'actual 85-file HC source'}
 Check ('stale and same-size corrupt libraries removed cycle '+$cycle) {
   $stale=Join-Path $target 'previously-trimmed-old.dll';[IO.File]::WriteAllText($stale,'stale')
   $new=Join-Path $source 'HandheldCompanion.dll';$old=Join-Path $target 'HandheldCompanion.dll';$bytes=[IO.File]::ReadAllBytes($old);$bytes[100]=$bytes[100] -bxor 1;[IO.File]::WriteAllBytes($old,$bytes);[IO.File]::SetLastWriteTimeUtc($old,[IO.File]::GetLastWriteTimeUtc($new))
   $null=Sync-YmccHcRuntime $source $pc;Need (!(Test-Path -LiteralPath $stale)) 'stale library retained';Need ((Get-FileHash -LiteralPath $old).Hash -eq (Get-FileHash -LiteralPath $new).Hash) 'same-size/time corruption retained'
 }
}
Check 'superseded owned runtime cleaned, unrelated directory retained' {
 $older=Join-Path $pc 'handheldcompanion-runtime/HC-OLD-OWNED';$foreign=Join-Path $pc 'handheldcompanion-runtime/user-notes';[IO.Directory]::CreateDirectory($older)|Out-Null;[IO.Directory]::CreateDirectory($foreign)|Out-Null
 @{schemaVersion=1;runtimeId='HC-OLD-OWNED';files=@()}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $older 'HandheldCompanion.runtime.json');[IO.File]::WriteAllText((Join-Path $older 'Old.dll'),'old');[IO.File]::WriteAllText((Join-Path $foreign 'keep.txt'),'keep')
 Remove-YmccSupersededHcRuntimes $pc (Split-Path -Leaf $source);Need (!(Test-Path -LiteralPath $older)) 'old runtime retained';Need (Test-Path -LiteralPath (Join-Path $foreign 'keep.txt')) 'unrelated directory deleted'
}
Check 'locked library fails within budget and recovers after release' {
 $locked=Join-Path $target 'HandheldCompanion.dll';$stream=[IO.File]::Open($locked,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None);$clock=[Diagnostics.Stopwatch]::StartNew();$failed=$false
 try{try{$null=Sync-YmccHcRuntime $source $pc}catch{$failed=$true}}finally{$stream.Dispose()}
 Need $failed 'locked library unexpectedly accepted';Need ($clock.ElapsedMilliseconds -lt 15000) 'copy lock wait not bounded';$null=Sync-YmccHcRuntime $source $pc;Assert-TreeMatch $source $target 'lock release repair'
}
Check 'runtime target junction rejected and unrelated bytes preserved' {
 $sandbox=Join-Path $ev 'LinkPowerControl';$linkRoot=Join-Path $sandbox 'handheldcompanion-runtime';$external=Join-Path $ev 'unrelated';[IO.Directory]::CreateDirectory($sandbox)|Out-Null;[IO.Directory]::CreateDirectory($external)|Out-Null;[IO.File]::WriteAllText((Join-Path $external 'keep.txt'),'untouched')
 New-Item -ItemType Junction -Path $linkRoot -Target $external|Out-Null;$rejected=$false;try{try{$null=Sync-YmccHcRuntime $source $sandbox}catch{$rejected=$_.Exception.Message -match 'REPARSE_POINT'}}finally{[IO.Directory]::Delete($linkRoot)}
 Need $rejected 'junction accepted';Need ([IO.File]::ReadAllText((Join-Path $external 'keep.txt')) -ceq 'untouched') 'foreign bytes changed'
}
Check 'HC is excluded from ordinary restore and uses incoming sync' {
 $text=Get-Content -LiteralPath (Join-Path $repo 'native/main.cpp') -Raw
 Need ($text.Contains("Register-TreeForRollback `$powerControlSource `$pcDir 'PowerControl' @('pawnio','fan-host-v2','decky','handheldcompanion-runtime','ymcc-shared-runtime')")) 'HC may be restored from old ordinary files'
 Need ($text.Contains('Sync-YmccImmutableFanPayload $fanHostRuntimeSource $fanHostSource $pcDir')) 'incoming HC sync disconnected'
}
Check 'actual source manifest stayed unchanged' {Need ((Get-FileHash -LiteralPath $manifest).Hash -eq $sourceHash) 'source changed during test'}

$sharedSource=Join-Path $repo 'PowerControl/ymcc-shared-runtime';$sharedManifest=Join-Path $sharedSource 'manifest.json';$sharedSourceHash=(Get-FileHash -LiteralPath $sharedManifest).Hash
$runtimeId=Split-Path -Leaf $source;$sharedTarget=Join-Path $pc 'ymcc-shared-runtime'
Check 'shared source plan and exact return contract' {
 $plan=@(Get-YmccSharedRuntimePlan $sharedSource $runtimeId);Need ($plan.Count -eq 1 -and $plan[0].files.Count -eq @(Get-ChildItem -LiteralPath $sharedSource -Recurse -File).Count) 'shared source plan is not one exact inventory'
 $result=@(Sync-YmccSharedRuntime $sharedSource $pc $runtimeId);Need ($result.Count -eq 1 -and $result[0] -ceq $sharedTarget) 'shared sync leaked native output or returned another target'
 Assert-TreeMatch $sharedSource $sharedTarget 'shared initial exact copy'
}
Check 'shared stale libraries and same-size corruption are repaired' {
 $stale=Join-Path $sharedTarget 'old-unmanaged.dll';[IO.File]::WriteAllText($stale,'stale')
 $file=Join-Path $sharedTarget 'Newtonsoft.Json.dll';$bytes=[IO.File]::ReadAllBytes($file);$bytes[100]=$bytes[100] -bxor 1;[IO.File]::WriteAllBytes($file,$bytes);[IO.File]::SetLastWriteTimeUtc($file,[IO.File]::GetLastWriteTimeUtc((Join-Path $sharedSource 'Newtonsoft.Json.dll')))
 $null=Sync-YmccSharedRuntime $sharedSource $pc $runtimeId;Need (!(Test-Path -LiteralPath $stale)) 'shared obsolete library survived';Assert-TreeMatch $sharedSource $sharedTarget 'shared stale/corruption repair'
}
Check 'unlisted shared source is rejected before target mutation' {
 $bad=Join-Path $ev 'bad-shared-source';Copy-Item -LiteralPath $sharedSource -Destination $bad -Recurse;[IO.File]::WriteAllText((Join-Path $bad 'unexpected.dll'),'unexpected')
 $rejected=$false;try{$null=Sync-YmccSharedRuntime $bad $pc $runtimeId}catch{$rejected=$_.Exception.Message -match 'SOURCE_INVENTORY_MISMATCH'};Need $rejected 'unlisted source accepted';Assert-TreeMatch $sharedSource $sharedTarget 'bad source did not mutate target'
}
Check 'corrupt shared source is rejected before target mutation' {
 $bad=Join-Path $ev 'corrupt-shared-source';Copy-Item -LiteralPath $sharedSource -Destination $bad -Recurse;$file=Join-Path $bad 'Newtonsoft.Json.dll';$bytes=[IO.File]::ReadAllBytes($file);$bytes[100]=$bytes[100] -bxor 1;[IO.File]::WriteAllBytes($file,$bytes)
 $rejected=$false;try{$null=Sync-YmccSharedRuntime $bad $pc $runtimeId}catch{$rejected=$_.Exception.Message -match 'SOURCE_HASH'};Need $rejected 'corrupt shared source accepted';Assert-TreeMatch $sharedSource $sharedTarget 'corrupt source did not mutate target'
}
Check 'shared target junction is rejected without touching outside bytes' {
 $sandbox=Join-Path $ev 'SharedLinkPowerControl';$external=Join-Path $ev 'shared-outside';[IO.Directory]::CreateDirectory($sandbox)|Out-Null;[IO.Directory]::CreateDirectory($external)|Out-Null;[IO.File]::WriteAllText((Join-Path $external 'keep.txt'),'untouched');$link=Join-Path $sandbox 'ymcc-shared-runtime'
 New-Item -ItemType Junction -Path $link -Target $external|Out-Null;$rejected=$false
 try{try{$null=Sync-YmccSharedRuntime $sharedSource $sandbox $runtimeId}catch{$rejected=$_.Exception.Message -match 'REPARSE_POINT'}}finally{[IO.Directory]::Delete($link,$false)}
 Need $rejected 'shared target junction accepted';Need ([IO.File]::ReadAllText((Join-Path $external 'keep.txt')) -ceq 'untouched') 'shared sync changed outside bytes'
}
Check 'locked shared library fails bounded then repairs after release' {
 $file=Join-Path $sharedTarget 'Newtonsoft.Json.dll';$stream=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None);$timer=[Diagnostics.Stopwatch]::StartNew();$failed=$false
 try{try{$null=Sync-YmccSharedRuntime $sharedSource $pc $runtimeId}catch{$failed=$true}}finally{$stream.Dispose()}
 Need $failed 'locked shared library accepted';Need ($timer.ElapsedMilliseconds -lt 15000) 'shared copy lock wait unbounded';$null=Sync-YmccSharedRuntime $sharedSource $pc $runtimeId;Assert-TreeMatch $sharedSource $sharedTarget 'shared lock release recovery'
}
if($CanonicalLayoutRoot){Check 'canonical HC-local LHM replaces only its former shared copy' {
 $canonicalPc=[IO.Path]::GetFullPath($CanonicalLayoutRoot);$canonicalRuntime=Join-Path $canonicalPc ('handheldcompanion-runtime/'+$runtimeId);$canonicalShared=Join-Path $canonicalPc 'ymcc-shared-runtime'
 $null=Sync-YmccHcRuntime $canonicalRuntime $pc;$null=Sync-YmccSharedRuntime $canonicalShared $pc $runtimeId
 Need (!(Test-Path -LiteralPath (Join-Path $sharedTarget 'LibreHardwareMonitorLib.dll'))) 'old shared LHM survived relocation';Need (Test-Path -LiteralPath (Join-Path $target 'LibreHardwareMonitorLib.dll')) 'canonical HC LHM missing'
 Assert-TreeMatch $canonicalShared $sharedTarget 'canonical shared exact closure';Assert-TreeMatch $canonicalRuntime $target 'canonical HC exact closure'
}}
Check 'mismatched Host runtime binding is rejected before immutable mutation' {
 $bad=Join-Path $ev 'bad-host-binding';Copy-Item -LiteralPath (Join-Path $repo 'PowerControl/fan-host') -Destination $bad -Recurse
 $file=Join-Path $bad 'YeManFanHost.payload.json';$document=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json;$document.runtimeManifest='../handheldcompanion-runtime/HC-WRONG/HandheldCompanion.runtime.json';$document|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $file -Encoding UTF8
 $untouched=Join-Path $ev 'BindingPowerControl';[IO.Directory]::CreateDirectory($untouched)|Out-Null;[IO.File]::WriteAllText((Join-Path $untouched 'keep.txt'),'untouched')
 $rejected=$false;try{$null=Sync-YmccImmutableFanPayload $source $bad $untouched}catch{$rejected=$_.Exception.Message -match 'RUNTIME_BINDING_INVALID'}
 Need $rejected 'mismatched Host/runtime binding accepted';Need (@(Get-ChildItem -LiteralPath $untouched -Force).Count -eq 1 -and [IO.File]::ReadAllText((Join-Path $untouched 'keep.txt')) -ceq 'untouched') 'rejected Host/runtime binding mutated an immutable target'
}
Check 'actual shared source manifest stayed unchanged' {Need ((Get-FileHash -LiteralPath $sharedManifest).Hash -ceq $sharedSourceHash) 'shared source changed'}
}
$failed=@($checks|Where-Object status -eq 'FAIL');@{status=$(if($failed.Count){'FAIL'}else{'PASS'});checks=@($checks);runtimeManifestSha256=$sourceHash;manifestFileCount=@((Get-Content -LiteralPath $manifest -Raw|ConvertFrom-Json).files).Count;consumer='actual emitted functions and real robocopy';realInstallationModified=$false}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $ev 'result.json')
foreach($check in $checks){Write-Output ($check.status+' '+$check.name)}
if($failed.Count){exit 1}
