[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('Build','Package','Verify')][string]$Stage,
  [Parameter(Mandatory=$true)][string]$WorkspaceRoot,
  [string]$CandidatePackage='',
  [string]$LifecycleEvidenceRoot=''
)
$ErrorActionPreference='Stop'
# Budget dispatch happens before the original gate allocates outputs or starts tests.
. (Join-Path $PSScriptRoot 'ymcc_sim/optimization/release-full-budget.ps1')
$fixtureProjectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
if(-not $env:YMCC_RELEASE_FIXTURE_ATTEMPT){
  Invoke-YmccFullReleaseFixtureStage -ProjectRoot $fixtureProjectRoot -Stage $Stage -WorkspaceRoot $WorkspaceRoot -CandidatePackage $CandidatePackage -LifecycleEvidenceRoot $LifecycleEvidenceRoot
  exit $LASTEXITCODE
}
Assert-YmccFullReleaseFixtureWorker -ProjectRoot $fixtureProjectRoot -Stage $Stage -WorkspaceRoot $WorkspaceRoot -CandidatePackage $CandidatePackage -LifecycleEvidenceRoot $LifecycleEvidenceRoot
$fixtureStageTimer=[Diagnostics.Stopwatch]::StartNew()
$windowsPowerShell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
# Standalone CI/desktop invocations can inherit PowerShell 7 module paths.
# Resolve this Windows PowerShell host's built-in hashing/JSON commands first.
$ownModules=Join-Path $PSHOME 'Modules'
if((($env:PSModulePath -split ';')[0]).TrimEnd('\') -ine $ownModules.TrimEnd('\')){$env:PSModulePath=$ownModules+';'+$env:PSModulePath}
$ProjectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$WorkspaceRoot=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')
$gateRoot=Join-Path $WorkspaceRoot 'Build\Validation\DeckyUpdate'
New-Item -ItemType Directory -Path $gateRoot -Force | Out-Null
. (Join-Path $PSScriptRoot 'release-export-identity.ps1')
$sourceDigest=Get-ExportIndexHash @(Get-ExportSourceIndex $ProjectRoot)
$steps=New-Object 'System.Collections.Generic.List[object]'
$script:recoveryRequirement=$null
$script:packageMaintenanceRequirement=$null
$script:resourceRequirements=$null
$script:javascriptRequirements=$null
$script:javascriptReferenceRequest=$null
$receiptPath=Join-Path $gateRoot ($Stage.ToUpperInvariant()+'.json')
function Invoke-Required([string]$Name,[string]$Program,[string[]]$Arguments) {
  $log=Join-Path $gateRoot ($Name+'.log')
  $stepStarted=[DateTime]::UtcNow.ToString('o')
  $stepTimer=[Diagnostics.Stopwatch]::StartNew()
  $priorPreference=$ErrorActionPreference
  $stepExit=-1;$launchError=$null
  try {
    $null=Get-Command $Program -ErrorAction Stop
    # Windows PowerShell 5 stderr progress remains evidence; native exit decides.
    $ErrorActionPreference='Continue'
    & $Program @Arguments *> $log
    $stepExit=$LASTEXITCODE
  } catch { $launchError=$_.Exception.Message }
  finally { $ErrorActionPreference=$priorPreference;$stepTimer.Stop() }
  $logSha=if(Test-Path -LiteralPath $log -PathType Leaf){(Get-FileHash -LiteralPath $log).Hash.ToLowerInvariant()}else{$null}
  $steps.Add([ordered]@{name=$Name;execution='ACTUAL';passed=($stepExit -eq 0 -and -not $launchError);exitCode=$stepExit;startedAtUtc=$stepStarted;wallMs=$stepTimer.Elapsed.TotalMilliseconds;log=$log;sha256=$logSha;launchError=$launchError})|Out-Null
  if($stepExit -ne 0 -or $launchError){throw ('DECKY_UPDATE_REQUIRED_CHECK_FAILED '+$Name+' exit='+$stepExit+' log='+$log+' launchError='+$launchError)}
}
function Test-DeckyLifecycleTrue($Value) { return ($Value -is [bool] -and $Value) }
function Get-DeckyLifecycleFullPath([string]$Path) {
  if(-not $Path -or -not [IO.Path]::IsPathRooted($Path) -or $Path -match '[\x00-\x1f]' -or $Path.StartsWith('\\?\')){throw 'DECKY_LIFECYCLE_ABSOLUTE_PATH_REQUIRED'}
  $full=[IO.Path]::GetFullPath($Path).TrimEnd('\')
  $cursor=$full
  while($cursor){if(Test-Path -LiteralPath $cursor){$item=Get-Item -LiteralPath $cursor -Force;if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'DECKY_LIFECYCLE_REPARSE_POINT'}};$parent=[IO.Path]::GetDirectoryName($cursor);if($parent -eq $cursor){break};$cursor=$parent}
  return $full
}
function Test-DeckyLifecycleInside([string]$Child,[string]$Parent) { return $Child.StartsWith($Parent.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) }
function Assert-DeckyLifecyclePathBudget([string]$Root) {
  $full=Get-DeckyLifecycleFullPath $Root
  $sample='2000-01-01T00-00-00-000Z-00000000\cases\plugins.decky-persistent-lifecycle\decky-persistent-lifecycle'
  $producer=Join-Path $full $sample
  $longest=Join-Path $producer ('native\native-fixtures\j\h\runtime-temp\ymcc-decky-'+('0'*32))
  # Conservative budget for the currently reviewed Win32 directory chain, not a timeout change.
  if($longest.Length -gt 240){throw ('DECKY_LIFECYCLE_EVIDENCE_PATH_TOO_LONG chars='+$longest.Length+' max=240 root='+$full)}
  return [ordered]@{evidenceRoot=$full;longestKnownDirectory=$longest;characters=$longest.Length;maxCharacters=240}
}
function Get-DeckyLifecycleOwnerKey {
  $w=Get-DeckyLifecycleFullPath $WorkspaceRoot;$p=Get-DeckyLifecycleFullPath $ProjectRoot
  $sha=[Security.Cryptography.SHA256]::Create()
  try{return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($w.ToLowerInvariant()+"`n"+$p.ToLowerInvariant())))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
}
function Assert-DeckyLifecycleRootScope([string]$Root) {
  $full=Get-DeckyLifecycleFullPath $Root;$project=Get-DeckyLifecycleFullPath $ProjectRoot;$workspace=Get-DeckyLifecycleFullPath $WorkspaceRoot
  if($full -eq [IO.Path]::GetPathRoot($full).TrimEnd('\') -or $full -eq $workspace -or $full -eq $project -or (Test-DeckyLifecycleInside $full $project) -or (Test-DeckyLifecycleInside $project $full)){throw 'DECKY_LIFECYCLE_EVIDENCE_SCOPE_INVALID'}
  return $full
}
function Read-DeckyLifecycleJson([string]$File,[int64]$MaxBytes=33554432) {
  $full=Get-DeckyLifecycleFullPath $File
  if(-not [IO.File]::Exists($full)){throw 'DECKY_LIFECYCLE_RAW_PROOF_MISSING'}
  if(([IO.FileInfo]$full).Length -gt $MaxBytes){throw 'DECKY_LIFECYCLE_RAW_PROOF_TOO_LARGE'}
  return (Get-Content -LiteralPath $full -Raw -Encoding UTF8|ConvertFrom-Json)
}
function Assert-DeckyLifecycleOwner([string]$Root,[string]$ExpectedOwnerId='') {
  $full=Assert-DeckyLifecycleRootScope $Root;$marker=Join-Path $full '.ymcc-lifecycle-owner.json'
  if(-not [IO.File]::Exists($marker)){throw 'DECKY_LIFECYCLE_OWNER_MISSING'}
  $owner=Read-DeckyLifecycleJson $marker 16384
  if($owner.schemaVersion -ne 1 -or $owner.ownerKey -cne (Get-DeckyLifecycleOwnerKey) -or [string]$owner.ownerId -cnotmatch '^[a-f0-9]{32}$' -or ($ExpectedOwnerId -and $owner.ownerId -cne $ExpectedOwnerId)){throw 'DECKY_LIFECYCLE_OWNER_MISMATCH'}
  return [ordered]@{root=$full;ownerId=[string]$owner.ownerId;marker=$marker;markerSha256=(Get-FileHash -LiteralPath $marker).Hash.ToLowerInvariant()}
}
function Initialize-DeckyLifecycleEvidence([string]$Requested='') {
  $root=$(if($Requested){$Requested}else{Join-Path $WorkspaceRoot '.ymcc-lc'})
  $root=Assert-DeckyLifecycleRootScope $root;$null=Assert-DeckyLifecyclePathBudget $root
  $marker=Join-Path $root '.ymcc-lifecycle-owner.json'
  if([IO.Directory]::Exists($root)){
    if(-not [IO.File]::Exists($marker) -and @([IO.Directory]::EnumerateFileSystemEntries($root)).Count){throw 'DECKY_LIFECYCLE_EVIDENCE_NOT_OWNED'}
  }else{[IO.Directory]::CreateDirectory($root)|Out-Null}
  if(-not [IO.File]::Exists($marker)){
    $owner=[ordered]@{schemaVersion=1;ownerKey=(Get-DeckyLifecycleOwnerKey);ownerId=[Guid]::NewGuid().ToString('N');createdAt=[DateTime]::UtcNow.ToString('o')}
    $bytes=[Text.Encoding]::UTF8.GetBytes(($owner|ConvertTo-Json -Compress))
    $stream=[IO.File]::Open($marker,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
  }
  return (Assert-DeckyLifecycleOwner $root)
}
# The original recovery consumer owns its native/CSS workers under the
# existing testing core. Keep publication logs in gateRoot, and give that
# consumer a separately owned, short evidence root inside the same core.
function Initialize-DeckyRecoveryEvidence {
  . (Join-Path $PSScriptRoot 'ymcc_sim/global/release-host-environment.ps1')
  $coreEvidence=Get-DeckyLifecycleFullPath (Join-Path (Get-YmccSimulationCoreRoot) 'Evidence')
  if(-not [IO.Directory]::Exists($coreEvidence)){throw 'DECKY_RECOVERY_TEST_CORE_UNAVAILABLE'}
  $root=Join-Path (Join-Path $coreEvidence 'DR') ((Get-DeckyLifecycleOwnerKey).Substring(0,12))
  return (Initialize-DeckyLifecycleEvidence $root)
}
function Get-DeckyLifecycleContractFingerprint {
  # Load only the plain contract and public gate; never import CLI/registry or run a consumer here.
  $code='const {pathToFileURL}=await import(''node:url'');const p=await import(''node:path'');const r=process.argv[1];const c=await import(pathToFileURL(p.join(r,''tools/ymcc_sim/global/decky-persistent-lifecycle/contract.mjs'')));const g=await import(pathToFileURL(p.join(r,''tools/ymcc_sim/global/capability-lifecycle/index.mjs'')));console.log(g.lifecycleContractFingerprint(c.lifecycleContract));'
  $value=@(& node --input-type=module -e $code $ProjectRoot)
  if($LASTEXITCODE -ne 0 -or $value.Count -ne 1 -or [string]$value[0] -cnotmatch '^[a-f0-9]{64}$'){throw 'DECKY_LIFECYCLE_CURRENT_CONTRACT_UNAVAILABLE'}
  return [string]$value[0]
}
function Get-DeckyLifecycleFileBinding([string]$File,[string]$RunRoot) {
  $file=Get-DeckyLifecycleFullPath $File;$run=Get-DeckyLifecycleFullPath $RunRoot
  if(-not (Test-DeckyLifecycleInside $file $run) -or -not [IO.File]::Exists($file)){throw 'DECKY_LIFECYCLE_RAW_PROOF_OUTSIDE_RUN_OR_MISSING'}
  return [ordered]@{path=$file;sha256=(Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant()}
}
function Assert-DeckyLifecycleSource([string]$Relative,[string]$Sha256) {
  if(-not $Relative -or [IO.Path]::IsPathRooted($Relative) -or $Relative -match '[:\x00-\x1f]' -or @($Relative -split '[/\\]'|Where-Object{$_ -eq '..' -or $_ -eq '.' -or $_ -eq ''}).Count -or $Sha256 -cnotmatch '^[a-f0-9]{64}$'){throw 'DECKY_LIFECYCLE_RAW_SOURCE_INVALID'}
  $source=Get-DeckyLifecycleFullPath (Join-Path $ProjectRoot $Relative);$project=Get-DeckyLifecycleFullPath $ProjectRoot
  if(-not (Test-DeckyLifecycleInside $source $project) -or -not [IO.File]::Exists($source) -or (Get-FileHash -LiteralPath $source).Hash -ine $Sha256){throw ('DECKY_LIFECYCLE_RAW_SOURCE_CHANGED '+$Relative)}
}
function Assert-DeckyLifecycleAudit($Audit,[string]$Fingerprint) {
  $features=@($Audit.features|Where-Object{$_.id -ceq 'plugins.decky' -and $_.adapterId -ceq 'plugins.decky-persistent-lifecycle'})
  if($Audit.schemaVersion -ne 1 -or $Audit.status -cne 'PASS' -or -not (Test-DeckyLifecycleTrue $Audit.softwareAcceptance) -or $features.Count -ne 1 -or $features[0].status -cne 'PASS' -or $features[0].contractFingerprint -cne $Fingerprint){throw 'DECKY_LIFECYCLE_RAW_AUDIT_NOT_PASS'}
  $required=@($features[0].requiredComponents)
  $components=@($Audit.components|Where-Object{$_.adapterId -ceq 'plugins.decky-owned-runtime-lifecycle' -and $_.featureId -ceq 'plugins.decky' -and $_.parentAdapterId -ceq 'plugins.decky-persistent-lifecycle'})
  if($required.Count -ne 1 -or $required[0] -cne 'plugins.decky-owned-runtime-lifecycle' -or $components.Count -ne 1 -or $components[0].status -cne 'COMPONENT_PASS' -or $components[0].componentExecutionStatus -cne 'PASS' -or $components[0].parentAcceptance -cne 'PASS' -or -not (Test-DeckyLifecycleTrue $components[0].requiredByParent) -or -not (Test-DeckyLifecycleTrue $components[0].fullFeatureAcceptance)){throw 'DECKY_LIFECYCLE_RAW_COMPONENT_AUDIT_NOT_PASS'}
}
function Assert-DeckyLifecycleProof($Proof) {
  if($null -eq $Proof -or $Proof.schemaVersion -ne 1 -or $Proof.adapterId -cne 'plugins.decky-persistent-lifecycle' -or $Proof.sourceDigest -cne $sourceDigest -or [string]$Proof.ownerId -cnotmatch '^[a-f0-9]{32}$' -or [string]$Proof.ownerMarkerSha256 -cnotmatch '^[a-f0-9]{64}$' -or [string]$Proof.runId -cnotmatch '^\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}-\d{3}Z-[a-f0-9]{8}$'){throw 'DECKY_LIFECYCLE_RAW_PROOF_INVALID_OR_OLD'}
  $owner=Assert-DeckyLifecycleOwner ([string]$Proof.evidenceRoot) ([string]$Proof.ownerId)
  if($Proof.ownerMarkerSha256 -cne $owner.markerSha256){throw 'DECKY_LIFECYCLE_OWNER_CHANGED'}
  $run=Join-Path $owner.root ([string]$Proof.runId)
  $producer=Join-Path $run 'cases\plugins.decky-persistent-lifecycle\decky-persistent-lifecycle\result.json'
  $component=Join-Path $run 'cases\plugins.decky-owned-runtime-lifecycle\decky-owned-runtime-lifecycle\result.json'
  foreach($pair in @(@('globalResult',(Join-Path $run 'result.json')),@('lifecycleAudit',(Join-Path $run 'lifecycle-acceptance.json')),@('producerResult',$producer),@('componentResult',$component))){
    $binding=$Proof.($pair[0]);$expected=Get-DeckyLifecycleFullPath $pair[1]
    if($null -eq $binding -or [string]$binding.sha256 -cnotmatch '^[a-f0-9]{64}$' -or -not [string]::Equals([string]$binding.path,$expected,[StringComparison]::OrdinalIgnoreCase)){throw 'DECKY_LIFECYCLE_RAW_PROOF_PATH_INVALID'}
    $actual=Get-DeckyLifecycleFileBinding $expected $run
    if($actual.sha256 -cne $binding.sha256){throw 'DECKY_LIFECYCLE_RAW_PROOF_CHANGED'}
  }
  $fingerprint=Get-DeckyLifecycleContractFingerprint
  if($Proof.contractFingerprint -cne $fingerprint){throw 'DECKY_LIFECYCLE_CONTRACT_CHANGED'}
  $global=Read-DeckyLifecycleJson $Proof.globalResult.path
  $audit=Read-DeckyLifecycleJson $Proof.lifecycleAudit.path
  $produced=Read-DeckyLifecycleJson $Proof.producerResult.path
  $componentProduced=Read-DeckyLifecycleJson $Proof.componentResult.path
  $rows=@($global.results|Where-Object{$_.id -ceq 'plugins.decky-persistent-lifecycle'})
  $componentRows=@($global.results|Where-Object{$_.id -ceq 'plugins.decky-owned-runtime-lifecycle'})
  if($global.schemaVersion -ne 1 -or $global.tool -cne 'ymcc_sim-global' -or $global.mode -cne 'run' -or $global.profile -cne 'desktop' -or $global.runId -cne $Proof.runId -or -not [string]::Equals([string]$global.repoRoot,(Get-DeckyLifecycleFullPath $ProjectRoot),[StringComparison]::OrdinalIgnoreCase) -or $global.plannedTests -ne 2 -or @($global.results).Count -ne 2 -or $global.verdict.status -cne 'PASS_SELECTED_SCOPE' -or $global.verdict.exitCode -ne 0 -or $rows.Count -ne 1 -or $componentRows.Count -ne 1){throw 'DECKY_LIFECYCLE_RAW_GLOBAL_NOT_PASS'}
  $started=[DateTime]::Parse([string]$global.startedAt).ToUniversalTime();$finished=[DateTime]::Parse([string]$global.finishedAt).ToUniversalTime()
  if($started -lt [DateTime]::Parse([string]$Proof.invocationStartedAt).ToUniversalTime() -or $finished -gt [DateTime]::Parse([string]$Proof.invocationFinishedAt).ToUniversalTime() -or $finished -lt $started){throw 'DECKY_LIFECYCLE_RAW_INVOCATION_MISMATCH'}
  foreach($executionRow in @($rows[0],$componentRows[0])){
    if($executionRow.status -cne 'PASS' -or $executionRow.exitCode -ne 0 -or $executionRow.signal -or $executionRow.timedOut -or $executionRow.launchError -or $executionRow.sourceChanged -or $executionRow.candidateArtifactChanged -or $executionRow.missingInputs -or $executionRow.buildError -or $executionRow.requiredEvidenceMissing -or $executionRow.requiredEvidenceFailed -or $executionRow.outputTruncated){throw 'DECKY_LIFECYCLE_RAW_EXECUTION_NOT_PASS'}
  }
  $row=$rows[0];$componentRow=$componentRows[0]
  Assert-DeckyLifecycleAudit $global.lifecycleAcceptance $fingerprint
  Assert-DeckyLifecycleAudit $audit $fingerprint
  if(-not (Test-DeckyLifecycleTrue $produced.passed) -or $produced.status -cne 'PASS' -or $produced.lifecycleEvidence.schemaVersion -ne 1 -or $produced.lifecycleEvidence.contractFingerprint -cne $fingerprint -or -not @($produced.cases).Count -or @($produced.cases|Where-Object{-not (Test-DeckyLifecycleTrue $_.passed)}).Count){throw 'DECKY_LIFECYCLE_RAW_PRODUCER_NOT_PASS'}
  if($componentProduced.schemaVersion -ne 1 -or $componentProduced.id -cne 'plugins.decky-owned-runtime-lifecycle' -or -not (Test-DeckyLifecycleTrue $componentProduced.passed) -or $componentProduced.status -cne 'PASS' -or -not (Test-DeckyLifecycleTrue $componentProduced.summary.completeCurrentProducer) -or $componentProduced.summary.failed -ne 0 -or @($componentProduced.cases).Count -ne $componentProduced.summary.cases -or -not @($componentProduced.cases).Count -or @($componentProduced.cases|Where-Object{-not (Test-DeckyLifecycleTrue $_.passed)}).Count){throw 'DECKY_LIFECYCLE_RAW_COMPONENT_NOT_PASS'}
  if(-not (Test-DeckyLifecycleTrue $componentProduced.originalExecution.reusedSameRun) -or -not (Test-DeckyLifecycleTrue $componentProduced.sharedExecution.originalExecutedInThisGlobalRun) -or $componentProduced.sharedExecution.businessProcessesStartedByThisComponent -ne 0){throw 'DECKY_LIFECYCLE_RAW_COMPONENT_NOT_SHARED_IN_RUN'}
  foreach($evidencePair in @(@($row,$Proof.producerResult),@($componentRow,$Proof.componentResult))){
    $raw=@($evidencePair[0].structuredEvidence|Where-Object{[string]::Equals([string]$_.path,[string]$evidencePair[1].path,[StringComparison]::OrdinalIgnoreCase)})
    if($raw.Count -ne 1 -or $raw[0].failed -isnot [bool] -or $raw[0].failed -or $raw[0].sha256 -cne $evidencePair[1].sha256){throw 'DECKY_LIFECYCLE_RAW_PRODUCER_NOT_BOUND'}
  }
  $sourceRows=@($row.sourceInputs)+@($row.dependencyInputs)+@($componentRow.sourceInputs)+@($componentRow.dependencyInputs)|Where-Object{$null -ne $_}
  if(-not @($sourceRows).Count -or @($sourceRows).Count -gt 8192){throw 'DECKY_LIFECYCLE_RAW_SOURCE_MISSING'}
  foreach($inputRow in $sourceRows){Assert-DeckyLifecycleSource ([string]$inputRow.path) ([string]$inputRow.sha256)}
  foreach($result in @($produced,$componentProduced)){
    $sourceProperties=@($result.sourceHashes.PSObject.Properties)
    if(-not $sourceProperties.Count -or $sourceProperties.Count -gt 8192){throw 'DECKY_LIFECYCLE_RAW_SOURCE_MISSING'}
    foreach($property in $sourceProperties){Assert-DeckyLifecycleSource $property.Name ([string]$property.Value)}
  }
}
function New-DeckyLifecycleProof($Owner,[string]$RunId,[string]$StartedAt,[string]$FinishedAt) {
  $run=Join-Path $Owner.root $RunId
  $proof=[ordered]@{schemaVersion=1;adapterId='plugins.decky-persistent-lifecycle';sourceDigest=$sourceDigest;evidenceRoot=$Owner.root;ownerId=$Owner.ownerId;ownerMarkerSha256=$Owner.markerSha256;runId=$RunId;invocationStartedAt=$StartedAt;invocationFinishedAt=$FinishedAt;contractFingerprint=(Get-DeckyLifecycleContractFingerprint);globalResult=(Get-DeckyLifecycleFileBinding (Join-Path $run 'result.json') $run);lifecycleAudit=(Get-DeckyLifecycleFileBinding (Join-Path $run 'lifecycle-acceptance.json') $run);producerResult=(Get-DeckyLifecycleFileBinding (Join-Path $run 'cases\plugins.decky-persistent-lifecycle\decky-persistent-lifecycle\result.json') $run);componentResult=(Get-DeckyLifecycleFileBinding (Join-Path $run 'cases\plugins.decky-owned-runtime-lifecycle\decky-owned-runtime-lifecycle\result.json') $run)}
  Assert-DeckyLifecycleProof $proof
  return $proof
}
function Invoke-DeckyLifecycleRequired {
  $owner=if($LifecycleEvidenceRoot){Initialize-DeckyLifecycleEvidence $LifecycleEvidenceRoot}else{Initialize-DeckyRecoveryEvidence}
  $before=@([IO.Directory]::GetDirectories($owner.root));$started=[DateTime]::UtcNow.ToString('o')
  Invoke-Required 'decky-persistent-lifecycle' 'node' @((Join-Path $PSScriptRoot 'ymcc_sim/global/cli.mjs'),'--mode','run','--profile','desktop','--test','plugins.decky-persistent-lifecycle,plugins.decky-owned-runtime-lifecycle','--out-dir',$owner.root)
  $finished=[DateTime]::UtcNow.ToString('o');$fresh=@([IO.Directory]::GetDirectories($owner.root)|Where-Object{$_ -notin $before})
  if($fresh.Count -ne 1){throw 'DECKY_LIFECYCLE_FRESH_RUN_NOT_UNIQUE'}
  $proof=New-DeckyLifecycleProof $owner ([IO.Path]::GetFileName($fresh[0])) $started $finished
  $steps[$steps.Count-1]['lifecycleProof']=$proof
}


# These are the five original consumers already executed completely in the parent.
function Get-DeckyJavascriptReferenceSteps {
  return @('decky_loader_official_selftest','decky_sidebar_safety_selftest','decky_sidebar_client_selftest','decky_sidebar_host_selftest','decky_sidebar_stability_matrix_selftest')
}
function New-DeckyJavascriptReferenceRequest {
  $node=Get-Command node -CommandType Application -ErrorAction Stop|Select-Object -First 1
  $program=Get-Item -LiteralPath $node.Source
  $environment=[ordered]@{}
  foreach($name in @('SystemRoot','ComSpec','PATH','PSModulePath','NODE_OPTIONS','NODE_PATH','ESBUILD_BINARY_PATH','ESBUILD_WORKER_THREADS','YMCC_MIRROR_CLIENT_SOURCE_OVERRIDE','YMCC_FAN_MIRROR_HOST_SOURCE_OVERRIDE','YMCC_DECKY_ARTIFACT_ROOT','YEMAN_WORKSPACE_ROOT','YMCC_SIM_RAW_PROGRAM_PWSH')){
    $environment[$name]=[Environment]::GetEnvironmentVariable($name)
  }
  $request=[ordered]@{schemaVersion=1;kind='ymcc-release-javascript-reference-request';program=@{path=[IO.Path]::GetFullPath($node.Source);sha256=(Get-FileHash -LiteralPath $node.Source -Algorithm SHA256).Hash.ToLowerInvariant();bytes=$program.Length};cwd=$ProjectRoot;environment=$environment;steps=@(Get-DeckyJavascriptReferenceSteps)}
  $file=Join-Path $gateRoot ('javascript-reference-request-'+[Guid]::NewGuid().ToString('N')+'.json')
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($request|ConvertTo-Json -Depth 8))
  $stream=[IO.FileStream]::new($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
  return [ordered]@{path=$file;sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant();bytes=$bytes.Length}
}
function Assert-DeckyJavascriptSharedScope($Row,[string]$Name) {
  if($null -eq $Row -or $Row.status -cne 'SHARED_REFERENCE' -or $Row.requiredReleaseStep -cne $Name -or $Row.originalProducerExecutionsWithinParent -ne 1 -or $Row.originalProducerExecutionsByThisReference -ne 0){throw 'DECKY_JAVASCRIPT_SHARED_SCOPE_INVALID'}
  foreach($field in @('cached','executedThisRun','actualPassedThisRun')){if($Row.$field -isnot [bool] -or $Row.$field -ne $false){throw 'DECKY_JAVASCRIPT_SHARED_EXECUTION_FLAG_INVALID'}}
}
function Assert-DeckyJavascriptSharedBuildReceipt($Receipt) {
  if(-not $Receipt.javascriptReferenceRequest -or (Get-FileHash -LiteralPath $Receipt.javascriptReferenceRequest.path -Algorithm SHA256).Hash -ine $Receipt.javascriptReferenceRequest.sha256){throw 'DECKY_JAVASCRIPT_SHARED_REQUEST_CHANGED'}
  $names=@(Get-DeckyJavascriptReferenceSteps)
  if(-not $Receipt.javascriptRequirements -or @($Receipt.javascriptRequirements.PSObject.Properties).Count -ne $names.Count){throw 'DECKY_JAVASCRIPT_SHARED_RECEIPT_REQUIRED'}
  $common=$null;$reference=$null
  foreach($name in $names){
    $rows=@($Receipt.steps|Where-Object{$_.name -ceq $name})
    $requirement=$Receipt.javascriptRequirements.$name
    if($rows.Count -ne 1 -or $rows[0].execution -cne 'SHARED_REFERENCE' -or -not(Test-DeckyLifecycleTrue $rows[0].passed) -or $rows[0].originalProducerExecutions -ne 0 -or $requirement.status -cne 'SHARED_REFERENCE'){throw 'DECKY_JAVASCRIPT_SHARED_RECEIPT_REQUIRED'}
    foreach($field in @('cached','executedThisRun','actualPassedThisRun')){if($rows[0].$field -isnot [bool] -or $rows[0].$field -ne $false){throw 'DECKY_JAVASCRIPT_SHARED_EXECUTION_FLAG_INVALID'}}
    if($requirement.name -cne $name -or $requirement.originalProducerExecutionsWithinParent -ne 1 -or $requirement.originalProducerExecutionsByThisReference -ne 0){throw 'DECKY_JAVASCRIPT_SHARED_RECEIPT_BINDING'}
    foreach($field in @('cached','executedThisRun','actualPassedThisRun')){if($requirement.$field -isnot [bool] -or $requirement.$field -ne $false){throw 'DECKY_JAVASCRIPT_SHARED_EXECUTION_FLAG_INVALID'}}
    $proof=$rows[0].sharedReference
    if(-not $proof -or -not $requirement.proof -or $proof.path -ine $requirement.proof.path -or $proof.sha256 -cne $requirement.proof.sha256){throw 'DECKY_JAVASCRIPT_SHARED_RECEIPT_BINDING'}
    if(-not $common){
      if((Get-FileHash -LiteralPath $proof.path -Algorithm SHA256).Hash -ine $proof.sha256){throw 'DECKY_JAVASCRIPT_SHARED_REFERENCE_CHANGED'}
      $common=$proof;$reference=Get-Content -LiteralPath $proof.path -Raw -Encoding UTF8|ConvertFrom-Json
      if($reference.status -cne 'SHARED_REFERENCE' -or @($reference.javascript).Count -ne $names.Count){throw 'DECKY_JAVASCRIPT_SHARED_REFERENCE_REQUIRED'}
    }elseif($proof.path -ine $common.path -or $proof.sha256 -cne $common.sha256){throw 'DECKY_JAVASCRIPT_SHARED_REFERENCE_MIXED'}
    $shared=@($reference.javascript|Where-Object{$_.requiredReleaseStep -ceq $name})
    if($shared.Count -ne 1){throw 'DECKY_JAVASCRIPT_SHARED_REFERENCE_REQUIRED'}
    Assert-DeckyJavascriptSharedScope $shared[0] $name
    foreach($field in @('originalProcess','originalResult')){
      $actual=$shared[0].$field;$bound=$proof.$field
      if(-not $actual -or -not $bound -or $actual.path -ine $bound.path -or $actual.sha256 -cne $bound.sha256 -or (Get-FileHash -LiteralPath $actual.path -Algorithm SHA256).Hash -ine $actual.sha256){throw 'DECKY_JAVASCRIPT_SHARED_RAW_CHANGED'}
    }
  }
}

function Assert-DeckyNativeSharedScope($Shared,[ValidateSet('health','runtime')][string]$Family) {
  $prefix='DECKY_NATIVE_'+$Family.ToUpperInvariant()
  if(-not $Shared -or $Shared.status -cne 'SHARED_REFERENCE' -or $Shared.requiredReleaseBuildStep -cne ('official-native-'+$Family+'-build') -or $Shared.requiredReleaseRunStep -cne ('official-native-'+$Family) -or $Shared.originalProducerExecutionsWithinParent -ne 1 -or $Shared.originalProducerExecutionsByThisReference -ne 0 -or $Shared.compilerInvocationsWithinParent -ne 1 -or $Shared.compilerInvocationsByThisReference -ne 0){throw ($prefix+'_SHARED_SCOPE_INVALID')}
  foreach($field in @('cached','executedThisRun','actualPassedThisRun')){if($Shared.$field -isnot [bool] -or $Shared.$field -ne $false){throw ($prefix+'_SHARED_EXECUTION_FLAG_INVALID')}}
  foreach($field in @('originalCompileProcess','originalRunProcess','originalProgram','compilerGraph')){
    $pin=$Shared.$field
    if(-not $pin -or -not $pin.path -or $pin.sha256 -cnotmatch '^[a-f0-9]{64}$' -or -not(Test-Path -LiteralPath $pin.path -PathType Leaf) -or (Get-FileHash -LiteralPath $pin.path -Algorithm SHA256).Hash -ine $pin.sha256){throw ($prefix+'_SHARED_RAW_CHANGED')}
  }
}
function Assert-DeckyNativeSharedBuildReceipt($Receipt,[ValidateSet('health','runtime')][string]$Family) {
  $prefix='DECKY_NATIVE_'+$Family.ToUpperInvariant();$fieldName='native'+$Family.Substring(0,1).ToUpperInvariant()+$Family.Substring(1)
  $required=$Receipt.($fieldName+'Requirement')
  if(-not $required -or $required.name -cne $Family -or $required.status -cne 'SHARED_REFERENCE' -or $required.originalProducerExecutionsWithinParent -ne 1 -or $required.originalProducerExecutionsByThisReference -ne 0 -or $required.compilerInvocationsWithinParent -ne 1 -or $required.compilerInvocationsByThisReference -ne 0){throw ($prefix+'_SHARED_RECEIPT_REQUIRED')}
  foreach($field in @('cached','executedThisRun','actualPassedThisRun')){if($required.$field -isnot [bool] -or $required.$field -ne $false){throw ($prefix+'_SHARED_EXECUTION_FLAG_INVALID')}}
  $common=$required.proof
  if(-not $common -or -not $common.path -or (Get-FileHash -LiteralPath $common.path -Algorithm SHA256).Hash -ine $common.sha256){throw ($prefix+'_SHARED_REFERENCE_CHANGED')}
  $reference=Get-Content -LiteralPath $common.path -Raw -Encoding UTF8|ConvertFrom-Json
  if($reference.status -cne 'SHARED_REFERENCE'){throw ($prefix+'_SHARED_REFERENCE_REQUIRED')}
  Assert-DeckyNativeSharedScope $reference.$fieldName $Family
  foreach($name in @(('official-native-'+$Family+'-build'),('official-native-'+$Family))){
    $rows=@($Receipt.steps|Where-Object{$_.name -ceq $name})
    if($rows.Count -ne 1 -or $rows[0].execution -cne 'SHARED_REFERENCE' -or -not(Test-DeckyLifecycleTrue $rows[0].passed) -or $rows[0].exitCode -ne 0 -or $rows[0].originalProducerExecutions -ne 0 -or $rows[0].compilerInvocations -ne 0){throw ($prefix+'_SHARED_RECEIPT_REQUIRED')}
    foreach($field in @('cached','executedThisRun','actualPassedThisRun')){if($rows[0].$field -isnot [bool] -or $rows[0].$field -ne $false){throw ($prefix+'_SHARED_EXECUTION_FLAG_INVALID')}}
    $proof=$rows[0].sharedReference
    if(-not $proof -or $proof.path -ine $common.path -or $proof.sha256 -cne $common.sha256){throw ($prefix+'_SHARED_RECEIPT_BINDING')}
    foreach($field in @('originalCompileProcess','originalRunProcess','originalProgram','compilerGraph')){
      $actual=$reference.$fieldName.$field
      foreach($bound in @($proof.$field,$common.$field)){
        if(-not $bound -or $bound.path -ine $actual.path -or $bound.sha256 -cne $actual.sha256){throw ($prefix+'_SHARED_RECEIPT_BINDING')}
      }
    }
  }
}

function Assert-DeckyNativeHealthSharedScope($Shared) { Assert-DeckyNativeSharedScope $Shared 'health' }
function Assert-DeckyNativeHealthSharedBuildReceipt($Receipt) { Assert-DeckyNativeSharedBuildReceipt $Receipt 'health' }
function Assert-DeckyNativeRuntimeSharedBuildReceipt($Receipt) { Assert-DeckyNativeSharedBuildReceipt $Receipt 'runtime' }

function Complete-DeckyRecoverySharedRequirement($LifecycleStep) {
  $proof=$LifecycleStep.lifecycleProof
  if(-not $proof -or -not $script:recoveryRequirement -or $script:recoveryRequirement.status -cne 'NOT_RUN_DEFERRED_TO_PARENT' -or -not $script:packageMaintenanceRequirement -or $script:packageMaintenanceRequirement.status -cne 'NOT_RUN_DEFERRED_TO_PARENT'){throw 'DECKY_RECOVERY_PARENT_REFERENCE_REQUIRED'}
  if(-not $script:javascriptRequirements -or @($script:javascriptRequirements.Keys).Count -ne @(Get-DeckyJavascriptReferenceSteps).Count -or -not $script:javascriptReferenceRequest -or (Get-FileHash -LiteralPath $script:javascriptReferenceRequest.path -Algorithm SHA256).Hash -ine $script:javascriptReferenceRequest.sha256){throw 'DECKY_JAVASCRIPT_PARENT_REFERENCE_REQUIRED'}
  foreach($name in @(Get-DeckyJavascriptReferenceSteps)){if($script:javascriptRequirements[$name].status -cne 'NOT_RUN_DEFERRED_TO_PARENT'){throw 'DECKY_JAVASCRIPT_PARENT_REFERENCE_REQUIRED'}}
  $sharedFile=Join-Path (Split-Path -Parent $proof.globalResult.path) 'release-baseline-shared-reference.json'
  Invoke-Required 'decky_update_recovery_selftest' 'node' @((Join-Path $PSScriptRoot 'ymcc_sim/optimization/release-baseline-reference.mjs'),'--global-result',$proof.globalResult.path,'--global-sha256',$proof.globalResult.sha256,'--parent-result',$proof.producerResult.path,'--parent-sha256',$proof.producerResult.sha256,'--out',$sharedFile,'--javascript-request',$script:javascriptReferenceRequest.path,'--native-health-reference','required','--native-runtime-reference','required')
  $reference=Get-Content -LiteralPath $sharedFile -Raw -Encoding UTF8|ConvertFrom-Json
  if($reference.status -cne 'SHARED_REFERENCE' -or $reference.originalProducerExecutionsWithinParent -ne 1 -or $reference.originalProducerExecutionsByThisReference -ne 0 -or $reference.cached -ne $false){throw 'DECKY_RECOVERY_SHARED_SCOPE_INVALID'}
  if($reference.packageMaintenance.status -cne 'SHARED_REFERENCE' -or $reference.packageMaintenance.requiredReleaseStep -cne 'decky-package-maintenance' -or $reference.packageMaintenance.originalProducerExecutionsWithinParent -ne 1 -or $reference.packageMaintenance.originalProducerExecutionsByThisReference -ne 0 -or $reference.packageMaintenance.cached -ne $false){throw 'DECKY_PACKAGE_MAINTENANCE_SHARED_SCOPE_INVALID'}
  if(-not $script:resourceRequirements){throw 'DECKY_RESOURCE_SHARED_SCOPE_INVALID'}
  foreach($family in @('cache','worker')){
    $requiredName=if($family -ceq 'cache'){'decky-local-resource-cache'}else{'decky-owned-resource-worker'}
    $required=$script:resourceRequirements[$family];$shared=$reference.resources.$family
    if(-not $required -or $required.status -cne 'NOT_RUN_DEFERRED_TO_PARENT' -or -not $shared -or $shared.status -cne 'SHARED_REFERENCE' -or $shared.requiredReleaseStep -cne $requiredName -or $shared.originalProducerExecutionsWithinParent -ne 1 -or $shared.originalProducerExecutionsByThisReference -ne 0 -or $shared.cached -ne $false){throw 'DECKY_RESOURCE_SHARED_SCOPE_INVALID'}
  }
  if(@($reference.javascript).Count -ne @(Get-DeckyJavascriptReferenceSteps).Count){throw 'DECKY_JAVASCRIPT_SHARED_REFERENCE_REQUIRED'}
  foreach($name in @(Get-DeckyJavascriptReferenceSteps)){
    $shared=@($reference.javascript|Where-Object{$_.requiredReleaseStep -ceq $name})
    if($shared.Count -ne 1){throw 'DECKY_JAVASCRIPT_SHARED_REFERENCE_REQUIRED'}
    Assert-DeckyJavascriptSharedScope $shared[0] $name
  }
  if(-not $script:nativeHealthRequirement -or $script:nativeHealthRequirement.status -cne 'NOT_RUN_DEFERRED_TO_PARENT'){throw 'DECKY_NATIVE_HEALTH_PARENT_REFERENCE_REQUIRED'}
  Assert-DeckyNativeHealthSharedScope $reference.nativeHealth
  if(-not $script:nativeRuntimeRequirement -or $script:nativeRuntimeRequirement.status -cne 'NOT_RUN_DEFERRED_TO_PARENT'){throw 'DECKY_NATIVE_RUNTIME_PARENT_REFERENCE_REQUIRED'}
  Assert-DeckyNativeSharedScope $reference.nativeRuntime 'runtime'
  $step=$steps[$steps.Count-1]
  $step['execution']='SHARED_REFERENCE';$step['originalProducerExecutions']=0;$step['verificationExecuted']=$true
  $step['sharedReference']=[ordered]@{path=$sharedFile;sha256=(Get-FileHash -LiteralPath $sharedFile).Hash.ToLowerInvariant();originalProcess=$reference.originalProcess;originalResult=$reference.originalResult;originalElapsedMs=$reference.originalElapsedMs}
  $script:recoveryRequirement=[ordered]@{name='decky_update_recovery_selftest';status='SHARED_REFERENCE';originalExecutedInThisParent=$true;originalProducerExecutionsWithinParent=1;originalProducerExecutionsByThisReference=0;proof=$step.sharedReference}
  $packageProof=[ordered]@{path=$sharedFile;sha256=$step.sharedReference.sha256;originalProcess=$reference.packageMaintenance.originalProcess;originalResult=$reference.packageMaintenance.originalResult;originalElapsedMs=$reference.packageMaintenance.originalElapsedMs}
  $steps.Add([ordered]@{name='decky-package-maintenance';execution='SHARED_REFERENCE';passed=$true;exitCode=0;wallMs=0;originalProducerExecutions=0;verificationExecuted=$false;verificationSharedWith='decky_update_recovery_selftest';sharedReference=$packageProof})|Out-Null
  $script:packageMaintenanceRequirement=[ordered]@{name='decky-package-maintenance';status='SHARED_REFERENCE';originalExecutedInThisParent=$true;originalProducerExecutionsWithinParent=1;originalProducerExecutionsByThisReference=0;proof=$packageProof}
  foreach($family in @('cache','worker')){
    $shared=$reference.resources.$family;$requiredName=$shared.requiredReleaseStep
    $resourceProof=[ordered]@{path=$sharedFile;sha256=$step.sharedReference.sha256;originalProcess=$shared.originalProcess;originalResult=$shared.originalResult;originalElapsedMs=$shared.originalElapsedMs}
    $steps.Add([ordered]@{name=$requiredName;execution='SHARED_REFERENCE';passed=$true;exitCode=0;wallMs=0;originalProducerExecutions=0;verificationExecuted=$false;verificationSharedWith='decky_update_recovery_selftest';sharedReference=$resourceProof})|Out-Null
    $script:resourceRequirements[$family]=[ordered]@{name=$requiredName;status='SHARED_REFERENCE';originalExecutedInThisParent=$true;originalProducerExecutionsWithinParent=1;originalProducerExecutionsByThisReference=0;proof=$resourceProof}
  }
  foreach($name in @(Get-DeckyJavascriptReferenceSteps)){
    $shared=@($reference.javascript|Where-Object{$_.requiredReleaseStep -ceq $name})[0]
    $jsProof=[ordered]@{path=$sharedFile;sha256=$step.sharedReference.sha256;originalProcess=$shared.originalProcess;originalResult=$shared.originalResult;originalElapsedMs=$shared.originalElapsedMs}
    $steps.Add([ordered]@{name=$name;execution='SHARED_REFERENCE';passed=$true;exitCode=0;wallMs=0;originalProducerExecutions=0;verificationExecuted=$false;verificationSharedWith='decky_update_recovery_selftest';cached=$false;executedThisRun=$false;actualPassedThisRun=$false;sharedReference=$jsProof})|Out-Null
    $script:javascriptRequirements[$name]=[ordered]@{name=$name;status='SHARED_REFERENCE';originalExecutedInThisParent=$true;originalProducerExecutionsWithinParent=1;originalProducerExecutionsByThisReference=0;cached=$false;executedThisRun=$false;actualPassedThisRun=$false;proof=$jsProof}
  }

  foreach($family in @('health','runtime')){
  $fieldName='native'+$family.Substring(0,1).ToUpperInvariant()+$family.Substring(1);$shared=$reference.$fieldName
  $nativeProof=[ordered]@{path=$sharedFile;sha256=$step.sharedReference.sha256;originalCompileProcess=$shared.originalCompileProcess;originalRunProcess=$shared.originalRunProcess;originalProgram=$shared.originalProgram;compilerGraph=$shared.compilerGraph}
  foreach($name in @(('official-native-'+$family+'-build'),('official-native-'+$family))){
    $steps.Add([ordered]@{name=$name;execution='SHARED_REFERENCE';passed=$true;exitCode=0;wallMs=0;originalProducerExecutions=0;compilerInvocations=0;verificationExecuted=$false;verificationSharedWith='decky_update_recovery_selftest';cached=$false;executedThisRun=$false;actualPassedThisRun=$false;sharedReference=$nativeProof})|Out-Null
  }
  $required=[ordered]@{name=$family;status='SHARED_REFERENCE';originalProducerExecutionsWithinParent=1;originalProducerExecutionsByThisReference=0;compilerInvocationsWithinParent=1;compilerInvocationsByThisReference=0;cached=$false;executedThisRun=$false;actualPassedThisRun=$false;proof=$nativeProof}
  Set-Variable -Scope Script -Name ($fieldName+'Requirement') -Value $required
  }
}

function Assert-BuildReceipt {
  $file=Join-Path $gateRoot 'BUILD.json'
  if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw 'DECKY_UPDATE_BUILD_RECEIPT_MISSING'}
  $receipt=Get-Content -LiteralPath $file -Raw -Encoding UTF8|ConvertFrom-Json
  if($receipt.passed -ne $true -or $receipt.sourceDigest -ne $sourceDigest){throw 'DECKY_UPDATE_BUILD_RECEIPT_STALE'}
  if($receipt.fixtureBudgetSession -ine $env:YMCC_RELEASE_FIXTURE_SESSION){throw 'FULL_RELEASE_BUILD_SESSION_CHANGED'}
  $lifecycleSteps=@($receipt.steps|Where-Object{$_.name -eq 'decky-persistent-lifecycle'})
  if($lifecycleSteps.Count -ne 1 -or -not (Test-DeckyLifecycleTrue $lifecycleSteps[0].passed)){throw 'DECKY_UPDATE_LIFECYCLE_RECEIPT_MISSING_OR_DUPLICATED'}
  Assert-DeckyLifecycleProof $lifecycleSteps[0].lifecycleProof
  $packageSteps=@($receipt.steps|Where-Object{$_.name -ceq 'decky-package-maintenance'})
  if($receipt.packageMaintenanceRequirement.status -cne 'SHARED_REFERENCE' -or $packageSteps.Count -ne 1 -or $packageSteps[0].execution -cne 'SHARED_REFERENCE' -or -not (Test-DeckyLifecycleTrue $packageSteps[0].passed) -or $packageSteps[0].originalProducerExecutions -ne 0){throw 'DECKY_PACKAGE_MAINTENANCE_SHARED_RECEIPT_REQUIRED'}
  foreach($family in @('cache','worker')){
    $name=if($family -ceq 'cache'){'decky-local-resource-cache'}else{'decky-owned-resource-worker'}
    $resourceSteps=@($receipt.steps|Where-Object{$_.name -ceq $name})
    if($receipt.resourceRequirements.$family.status -cne 'SHARED_REFERENCE' -or $resourceSteps.Count -ne 1 -or $resourceSteps[0].execution -cne 'SHARED_REFERENCE' -or -not (Test-DeckyLifecycleTrue $resourceSteps[0].passed) -or $resourceSteps[0].originalProducerExecutions -ne 0){throw 'DECKY_RESOURCE_SHARED_RECEIPT_REQUIRED'}
  }
  Assert-DeckyJavascriptSharedBuildReceipt $receipt
  Assert-DeckyNativeHealthSharedBuildReceipt $receipt
  Assert-DeckyNativeRuntimeSharedBuildReceipt $receipt
  $exe=Join-Path $WorkspaceRoot 'Build\App\Native\YeManCC.exe'
  if((Get-FileHash -LiteralPath $exe).Hash -ine $receipt.nativeSha256){throw 'DECKY_UPDATE_NATIVE_BUILD_CHANGED'}
}
try {
  # Offline identity at every stage; network latest-stable verification is mandatory before GitHub publish.
  Invoke-Required 'official-loader-identity' 'node' @((Join-Path $PSScriptRoot 'decky-loader-contract.mjs'),'verify')
  # Product updates are YMCC-only. Tests must reject an independent Loader update route.
  Invoke-Required 'health-script-identity' 'node' @((Join-Path $PSScriptRoot 'decky_sidebar_health_embed.mjs'),'--check')
  Invoke-Required 'package-maintenance-identity' 'node' @((Join-Path $PSScriptRoot 'decky_package_maintenance_embed.mjs'),'--check')
  if($Stage -eq 'Build'){
    # The complete original resource producers execute within the single parent baseline.
    $script:resourceRequirements=@{cache=[ordered]@{name='decky-local-resource-cache';status='NOT_RUN_DEFERRED_TO_PARENT';originalProducerExecutionsByThisReference=0};worker=[ordered]@{name='decky-owned-resource-worker';status='NOT_RUN_DEFERRED_TO_PARENT';originalProducerExecutionsByThisReference=0}}
    Invoke-Required 'decky-steam-session-lease' 'node' @((Join-Path $PSScriptRoot 'decky_binding_steam_lease_selftest.mjs'),(Join-Path $gateRoot ('steam-lease-'+[Guid]::NewGuid().ToString('N'))))
    # The parent baseline executes this exact full original, including all three mutations.
    $script:packageMaintenanceRequirement=[ordered]@{name='decky-package-maintenance';status='NOT_RUN_DEFERRED_TO_PARENT';originalProducerExecutionsByThisReference=0}
    $null=Assert-ExportBuildIdentity -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot
    $priorArtifactRoot=$env:YMCC_DECKY_ARTIFACT_ROOT
    try {
      $env:YMCC_DECKY_ARTIFACT_ROOT=Join-Path $gateRoot 'scenarios'
      $script:nativeHealthRequirement=[ordered]@{name='health';status='NOT_RUN_DEFERRED_TO_PARENT';originalProducerExecutionsByThisReference=0;compilerInvocationsByThisReference=0}
      $script:nativeRuntimeRequirement=[ordered]@{name='runtime';status='NOT_RUN_DEFERRED_TO_PARENT';originalProducerExecutionsByThisReference=0;compilerInvocationsByThisReference=0}
      $script:javascriptRequirements=@{}
      $script:javascriptReferenceRequest=New-DeckyJavascriptReferenceRequest
      $remainingJavascriptTests=[Collections.Generic.List[string]]::new()
    foreach($test in @('decky_loader_official_selftest','decky_ymcc_update_selftest','decky_update_recovery_selftest','decky_sidebar_safety_selftest','decky_sidebar_safety_owner_selftest','decky_sidebar_health_dom_selftest','decky_sidebar_bootstrap_expression_selftest','decky_updater_service_selftest','decky_updater_ui_selftest','decky_sidebar_client_selftest','decky_sidebar_host_selftest','decky_sidebar_stability_matrix_selftest')){
      $scenarioOutput=$gateRoot
      if($test -eq 'decky_update_recovery_selftest'){
        # The current parent runs the exact complete original once under its original deadline.
        $script:recoveryRequirement=[ordered]@{name=$test;status='NOT_RUN_DEFERRED_TO_PARENT';originalProducerExecutionsByThisReference=0}
        continue
      }
      if(@(Get-DeckyJavascriptReferenceSteps) -ccontains $test){
        $script:javascriptRequirements[$test]=[ordered]@{name=$test;status='NOT_RUN_DEFERRED_TO_PARENT';originalProducerExecutionsByThisReference=0}
        continue
      }
      $remainingJavascriptTests.Add($test)
    }
      if(-not $script:recoveryRequirement){throw 'DECKY_RECOVERY_REQUIRED_STEP_NOT_DECLARED'}
      Invoke-DeckyLifecycleRequired
      Complete-DeckyRecoverySharedRequirement $steps[$steps.Count-1]
      # Required parent/reference failures stop the remaining original JS consumers.
      foreach($test in $remainingJavascriptTests){
        $scenarioOutput=$gateRoot
        Invoke-Required $test 'node' @((Join-Path $PSScriptRoot ($test+'.mjs')),$scenarioOutput)
      }
    } finally {$env:YMCC_DECKY_ARTIFACT_ROOT=$priorArtifactRoot}
    Invoke-Required 'official-loader-upgrade-transaction' $windowsPowerShell @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'decky_loader_upgrade_selftest.ps1'),'-EvidenceDir',(Join-Path $gateRoot 'official-upgrade-transaction'))
    foreach($test in @('updater_manager_resume_selftest','decky_updater_owner_loopback_selftest')){
      Invoke-Required ($test+'-bundle') 'node' @((Join-Path $ProjectRoot 'node_modules\esbuild\bin\esbuild'),(Join-Path $PSScriptRoot ($test+'.ts')),'--bundle','--platform=node','--format=esm',('--alias:@='+($ProjectRoot+'/src')),('--outfile='+(Join-Path $gateRoot ($test+'.mjs'))))
    }
    Invoke-Required 'updater-manager-resume' 'node' @((Join-Path $gateRoot 'updater_manager_resume_selftest.mjs'))
    Invoke-Required 'updater-game-lifecycle' 'node' @((Join-Path $PSScriptRoot 'updater_service_game_selftest.mjs'),(Join-Path $gateRoot 'updater-service-game.mjs'))
    $broker=Join-Path $gateRoot 'decky-sidebar-broker-test.exe'
    Invoke-Required 'native-authenticated-broker-build' 'cl.exe' @('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNDEBUG','/DNOMINMAX','/DUNICODE','/D_UNICODE',(Join-Path $PSScriptRoot 'decky_sidebar_broker_selftest.cpp'),('/I'+(Join-Path $ProjectRoot 'deps\json')),('/Fo'+(Join-Path $gateRoot 'broker.obj')),('/Fe'+$broker),'/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64')
    $crashTest=Join-Path $gateRoot 'decky-crash-breaker-test.exe'
    Invoke-Required 'native-crash-breaker-build' 'cl.exe' @('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE',(Join-Path $PSScriptRoot 'decky_sidebar_crash_breaker_selftest.cpp'),('/I'+(Join-Path $ProjectRoot 'deps\json')),('/Fo'+(Join-Path $gateRoot 'crash-breaker.obj')),('/Fe'+$crashTest),'/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64','advapi32.lib','user32.lib','userenv.lib','winhttp.lib','ws2_32.lib','bcrypt.lib')
    Invoke-Required 'native-crash-breaker' $crashTest @((Join-Path $gateRoot 'crash-fixture'))
    foreach($consumerNative in @('runtime','setup','health','health_close','bootstrap_watch','bootstrap')){
      if($consumerNative -cin @('health','runtime')){$fieldName='native'+$consumerNative.Substring(0,1).ToUpperInvariant()+$consumerNative.Substring(1)+'Requirement';$required=Get-Variable -Scope Script -Name $fieldName -ValueOnly -ErrorAction SilentlyContinue;if(-not $required -or $required.status -cne 'SHARED_REFERENCE'){throw ('DECKY_NATIVE_'+$consumerNative.ToUpperInvariant()+'_PARENT_REFERENCE_REQUIRED')};continue}
      $consumerTest=Join-Path $gateRoot ('decky-official-'+$consumerNative+'-test.exe')
      Invoke-Required ('official-native-'+$consumerNative+'-build') 'cl.exe' @('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE',(Join-Path $PSScriptRoot ('decky_sidebar_'+$consumerNative+'_selftest.cpp')),('/I'+(Join-Path $ProjectRoot 'deps\json')),('/Fo'+(Join-Path $gateRoot ($consumerNative+'.obj'))),('/Fe'+$consumerTest),'/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64','advapi32.lib','user32.lib','userenv.lib','winhttp.lib','ws2_32.lib','bcrypt.lib')
      $consumerArguments=@();if($consumerNative -eq 'setup'){$consumerArguments=@((Join-Path $gateRoot 'setup-fixture'))};if($consumerNative -eq 'health_close'){$consumerArguments=@((Join-Path $gateRoot 'health-close-fixture'))}
      Invoke-Required ('official-native-'+$consumerNative) $consumerTest $consumerArguments
    }
    $priorBroker=$env:YMCC_DECKY_TEST_BROKER
    try{$env:YMCC_DECKY_TEST_BROKER=$broker;foreach($mode in @('--success','--download-fails','--bad-manifest','--install-fails')){Invoke-Required ('owner-loopback'+$mode) 'node' @((Join-Path $gateRoot 'decky_updater_owner_loopback_selftest.mjs'),$gateRoot,$mode)}}finally{$env:YMCC_DECKY_TEST_BROKER=$priorBroker}
    Invoke-Required 'native-download-resume' $windowsPowerShell @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'updater_resume_native_selftest.ps1'),'-OutputRoot',(Join-Path $gateRoot 'native-resume'))
    Invoke-Required 'installed-export-identity' $windowsPowerShell @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'release_export_identity_selftest.ps1'),'-ValidationRoot',(Join-Path $gateRoot 'export-identity'))
    Invoke-Required 'ordinary-file-rollback' $windowsPowerShell @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'updater_rollback_selftest.ps1'))
  }elseif($Stage -eq 'Package'){
    Assert-BuildReceipt
    if(-not $CandidatePackage){throw 'DECKY_UPDATE_CANDIDATE_REQUIRED'}
    $CandidatePackage=[IO.Path]::GetFullPath($CandidatePackage)
    if(-not $CandidatePackage.StartsWith($WorkspaceRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'DECKY_UPDATE_CANDIDATE_OUTSIDE_WORKSPACE'}
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($CandidatePackage)
    try{
      foreach($pair in @(
        @('PowerControl/decky/PluginLoader_noconsole.exe',(Join-Path $ProjectRoot 'PowerControl\decky\PluginLoader_noconsole.exe')),
        @('PowerControl/decky/plugins/ymcc-sidebar/dist/index.js',(Join-Path $WorkspaceRoot 'Build\App\Decky\plugins\ymcc-sidebar\dist\index.js')),
        @('YeManCC/YeManCC.exe',(Join-Path $WorkspaceRoot 'Build\App\Native\YeManCC.exe')),
        @('YeManCC/index.html',(Join-Path $WorkspaceRoot 'Build\App\Web\index.html'))
      )){
        $entries=@($zip.Entries|Where-Object{$_.FullName.Replace('\','/').TrimStart('/') -ieq $pair[0]})
        if($entries.Count -ne 1){throw ('DECKY_UPDATE_ARCHIVE_FILE_MISSING_OR_DUPLICATED '+$pair[0])}
        $stream=$entries[0].Open();$sha=[Security.Cryptography.SHA256]::Create();try{$actual=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
        if($actual -ine (Get-FileHash -LiteralPath $pair[1]).Hash){throw ('DECKY_UPDATE_ARCHIVE_PAYLOAD_CHANGED '+$pair[0])}
      }
    }finally{$zip.Dispose()}
    $packageSha=(Get-FileHash -LiteralPath $CandidatePackage).Hash.ToLowerInvariant()
    $version=Get-Content -LiteralPath (Join-Path $ProjectRoot 'version.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $candidateManifest=Join-Path $gateRoot 'candidate-version.json'
    @{version=[string]$version.version;sha256=$packageSha}|ConvertTo-Json|Set-Content -LiteralPath $candidateManifest -Encoding UTF8
    Invoke-Required 'complete-package-install-and-rollback' $windowsPowerShell @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'updater_install_chain_selftest.ps1'),'-PackagePath',$CandidatePackage,'-ManifestPath',$candidateManifest)
  }else{
    Assert-BuildReceipt
    $packageReceiptPath=Join-Path $gateRoot 'PACKAGE.json'
    if(-not(Test-Path -LiteralPath $packageReceiptPath -PathType Leaf)){throw 'DECKY_UPDATE_PACKAGE_RECEIPT_MISSING'}
    $r=Get-Content -LiteralPath $packageReceiptPath -Raw -Encoding UTF8|ConvertFrom-Json
    if($r.passed -ne $true -or $r.sourceDigest -ne $sourceDigest){throw 'DECKY_UPDATE_PACKAGE_RECEIPT_STALE'}
    $releasePackage=Join-Path $WorkspaceRoot 'Release\Packages\YeManCC.zip'
    if((Get-FileHash -LiteralPath $releasePackage).Hash -ine $r.packageSha256){throw 'DECKY_UPDATE_RELEASE_PACKAGE_CHANGED'}
  }
  if((Get-ExportIndexHash @(Get-ExportSourceIndex $ProjectRoot)) -ne $sourceDigest){throw 'DECKY_UPDATE_SOURCE_CHANGED_DURING_CHECK'}
  $result=[ordered]@{schemaVersion=1;passed=$true;stage=$Stage;fixtureWallMs=$fixtureStageTimer.Elapsed.TotalMilliseconds;fixtureBudgetSession=$env:YMCC_RELEASE_FIXTURE_SESSION;fixtureBudgetAttempt=$env:YMCC_RELEASE_FIXTURE_ATTEMPT;recoveryRequirement=$script:recoveryRequirement;packageMaintenanceRequirement=$script:packageMaintenanceRequirement;resourceRequirements=$script:resourceRequirements;javascriptRequirements=$script:javascriptRequirements;javascriptReferenceRequest=$script:javascriptReferenceRequest;nativeHealthRequirement=$script:nativeHealthRequirement;nativeRuntimeRequirement=$script:nativeRuntimeRequirement;stepAccounting=@{actualOriginalCommands=@($steps|Where-Object {$_.execution -eq 'ACTUAL'}).Count;sharedOriginalRequirements=@($steps|Where-Object {$_.execution -eq 'SHARED_REFERENCE'}).Count;referenceVerificationCommands=@($steps|Where-Object {$_.verificationExecuted -eq $true}).Count};checkedAt=[DateTime]::UtcNow.ToString('o');sourceDigest=$sourceDigest;nativeSha256=(Get-FileHash -LiteralPath (Join-Path $WorkspaceRoot 'Build\App\Native\YeManCC.exe')).Hash.ToLowerInvariant();packageSha256=$(if($Stage -eq 'Package'){$packageSha}else{$null});steps=@($steps.ToArray());liveSteamActions=0;userSoftwareInstallations=0;physicalControllerTested=$false}
  $result|ConvertTo-Json -Depth 16|Set-Content -LiteralPath $receiptPath -Encoding UTF8
  Write-Output ('DECKY_UPDATE_RELEASE_GATE_OK stage='+$Stage+' checks='+$steps.Count)
}catch{
  @{passed=$false;stage=$Stage;sourceDigest=$sourceDigest;error=$_.Exception.Message;fixtureWallMs=$fixtureStageTimer.Elapsed.TotalMilliseconds;fixtureBudgetSession=$env:YMCC_RELEASE_FIXTURE_SESSION;fixtureBudgetAttempt=$env:YMCC_RELEASE_FIXTURE_ATTEMPT;recoveryRequirement=$script:recoveryRequirement;packageMaintenanceRequirement=$script:packageMaintenanceRequirement;resourceRequirements=$script:resourceRequirements;javascriptRequirements=$script:javascriptRequirements;javascriptReferenceRequest=$script:javascriptReferenceRequest;nativeHealthRequirement=$script:nativeHealthRequirement;nativeRuntimeRequirement=$script:nativeRuntimeRequirement;steps=@($steps.ToArray())}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $receiptPath -Encoding UTF8
  throw
}

