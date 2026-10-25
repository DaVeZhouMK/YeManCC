[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true)][string]$ReviewedContractPath)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..')).TrimEnd('\')
$out=[IO.Path]::GetFullPath($EvidenceDir).TrimEnd('\')
if($out -ieq $repo -or $out.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Use evidence outside the repository'}
if(Test-Path -LiteralPath (Join-Path $out 'result.json')){throw 'Fresh continuity evidence required'}
New-Item -ItemType Directory -Path $out -Force|Out-Null
$result=[ordered]@{schemaVersion=1;kind='lighting-component-continuity';status='BLOCKED';startedUtc=[DateTime]::UtcNow.ToString('o');cases=@();sourceHashes=@();sourceChanges=@();processes=@();claims=@{actualCWritten=$false;hardwareIo=$false;helperMainInvoked=$false;fullPackageExecuted=$false;physicalAcceptance=$false;fullProductAcceptance=$false}}
$processes=[Collections.Generic.List[object]]::new();$outputMappings=[Collections.Generic.List[object]]::new();$scratch=$null;$success=$false;$inputs=@()
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save([string]$p,$value){[IO.File]::WriteAllText($p,($value|ConvertTo-Json -Depth 60),[Text.UTF8Encoding]::new($false))}
function Need([bool]$ok,[string]$code){if(-not $ok){throw $code}}
function RunChild([string]$name,[string]$exe,[string[]]$arguments,[int]$expected,[bool]$nonzero=$false){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$exe;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $engineModules=$null
 if([IO.Path]::GetFileName($exe) -ieq 'powershell.exe'){
  $engineModules=Join-Path (Split-Path -Parent $exe) 'Modules';Need (Test-Path -LiteralPath $engineModules -PathType Container) 'WINDOWS_PS_MODULES_MISSING'
  $info.Environment['PSModulePath']=$engineModules+';'+$info.Environment['PSModulePath']
 }
 foreach($arg in $arguments){$info.ArgumentList.Add($arg)}
 $child=[Diagnostics.Process]::new();$child.StartInfo=$info;$started=[DateTime]::UtcNow
 try{
  Need ($child.Start()) 'CHILD_START_FAILED';$stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync();$timedOut=$false
  while(-not $child.WaitForExit(1000)){if(([DateTime]::UtcNow-$started).TotalSeconds -gt 100){$timedOut=$true;$child.Kill($true);$child.WaitForExit();break}}
  $code=$child.ExitCode;[IO.File]::WriteAllText((Join-Path $out ($name+'.stdout.log')),$stdout.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false));[IO.File]::WriteAllText((Join-Path $out ($name+'.stderr.log')),$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
  $record=[pscustomobject]@{id=$name;pid=$child.Id;exitCode=$code;expectedExitCode=$expected;expectedNonzero=$nonzero;ownModulesPrepended=$engineModules;naturalExit=(-not $timedOut);timedOut=$timedOut;executable=$exe;executableSha256=(Hash $exe);arguments=$arguments;startedUtc=$started.ToString('o');finishedUtc=[DateTime]::UtcNow.ToString('o')};$processes.Add($record)
  Need (-not $timedOut -and $(if($nonzero){$code -ne 0}else{$code -eq $expected})) ('CHILD_EXIT_'+$name)
  return $record
 }finally{$child.Dispose()}
}
function ArchiveFile([string]$path,[string]$label){
 Need (Test-Path -LiteralPath $path -PathType Leaf) 'RAW_OUTPUT_MISSING'
 $dest=Join-Path $out ('e/'+$label);New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force|Out-Null;Copy-Item -LiteralPath $path -Destination $dest
 Need ((Hash $path) -ceq (Hash $dest)) 'RAW_COPY_DIFFERS'
 $outputMappings.Add([pscustomobject]@{originalPath=$path;archivedPath=$dest;sha256=(Hash $dest)})
 return $dest
}
function ModuleFile($manifest,[string]$name,[string]$base){$module=@($manifest.modules|Where-Object id -CEQ $name);Need ($module.Count -eq 1) 'REPLAY_MODULE_MISSING';return Join-Path $base $module[0].artifacts.PSObject.Properties['.dll'].Value.path}
try{
 Need ($PSVersionTable.PSEdition -ceq 'Core') 'PWSH_REQUIRED'
 Need ($env:YMCC_SIM_GLOBAL_RUN -ceq '1') 'GLOBAL_OWNED_JOB_REQUIRED'
 . (Join-Path $PSScriptRoot 'New-ReviewedPlan.ps1')
 . (Join-Path $PSScriptRoot 'Test-PortableContract.ps1')
 . (Join-Path $repo 'tools/hc-device-runtime/verify-lighting-catalog-evidence.ps1')
 $planPath=Join-Path $out 'plan.json';$plan=New-YmccLightingReviewedPlan -ReviewedContractPath $ReviewedContractPath -OutputPlanPath $planPath -EvidenceRoot (Join-Path $out 'plan-evidence') -RepoRoot $repo
 $admission=Assert-YmccLightingCatalogEvidence -Plan $plan
 Need ($admission.status -ceq 'PASS' -and $admission.contract -ceq 'lighting-catalog-v3') 'PORTABLE_ADMISSION_FAILED'
 $review=Get-Content -LiteralPath $ReviewedContractPath -Raw -Encoding UTF8|ConvertFrom-Json
 $bundle=$plan.lightingEvidence.bundleRoot;$source=$plan.sourceBundleRoot
 Need ([IO.Path]::GetFullPath($plan.installRoot) -ieq [IO.Path]::GetFullPath((Join-Path $bundle 'fixture/before'))) 'FROZEN_REFERENCE_REQUIRED'
 $inputFiles=@($review.requiredTools|ForEach-Object {Join-Path $repo $_})+@(Join-Path $PSScriptRoot 'Test-PortableContract.ps1')+@(Get-ChildItem -LiteralPath $bundle -Recurse -File|ForEach-Object FullName)+@(Get-ChildItem -LiteralPath $source -Recurse -File|ForEach-Object FullName)
 $inputs=@($inputFiles|Sort-Object -Unique|ForEach-Object {$full=[IO.Path]::GetFullPath($_);Need ($full.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)) 'INPUT_OUTSIDE_REPO';[pscustomobject]@{path=$full.Substring($repo.Length+1).Replace('\','/');sha256=(Hash $full)}})
 $scratch=Join-Path ([IO.Path]::GetTempPath()) ('ymcc-lc-'+[guid]::NewGuid().ToString('N').Substring(0,12));New-Item -ItemType Directory -Path $scratch|Out-Null
 # RUN_CONSUMERS

 $core=(Get-Process -Id $PID).Path;$desktop=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe';$dotnet=(Get-Command dotnet.exe -ErrorAction Stop).Source
 $sourceManifest=Get-Content -LiteralPath (Join-Path $source 'LIGHTSETTER-SOURCE.json') -Raw -Encoding UTF8|ConvertFrom-Json
 Need ((Hash $dotnet) -ceq $sourceManifest.toolchain.dotnetSha256) 'DOTNET_IMAGE_DIFFERS'
 RunChild 'source-pe' $core @('-NoProfile','-File',(Join-Path $source 'Verify-LightSetterSourceBundle.ps1'),'-BundleRoot',$source,'-EvidencePath',(Join-Path $out 'source-pe.json')) 0|Out-Null
 $portable=Test-YmccLightingPortableContract -PlanPath $planPath -RepoRoot $repo -ScratchRoot (Join-Path $scratch 'port') -EvidencePath (Join-Path $out 'portable-controls.json')
 Need ($portable.status -ceq 'PASS' -and $portable.checks -eq 19 -and $portable.blockedReadCount -eq 2) 'PORTABLE_CONTROLS_FAILED'
 $manifest=Get-Content -LiteralPath (Join-Path $bundle 'ADMISSION-BUNDLE.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $catalog=Join-Path $source 'source/capabilities/hc-device-catalog.json';$helper=Join-Path $source 'artifacts/YeManLightSetter.dll';$old=Join-Path $bundle 'fixture/before/PowerControl/light-setter/YeManLightSetter.dll'
 $replays=@(
  @{name='catalog-original';module='catalog-original';args=@($catalog,'@result',$helper,$review.helperSha256);exit=0;count=549},
  @{name='boundary-177';module='boundary-177';args=@($helper,$review.helperSha256,'@result');exit=0;count=177},
  @{name='catalog-maintained';module='catalog-maintained';args=@($catalog,'@result',$helper,$review.helperSha256);exit=0;count=549},
  @{name='old-loki';module='catalog-original';args=@($catalog,'@result',$old,$review.oldHelperSha256,'--loki-only');exit=1;count=1},
  @{name='new-loki';module='catalog-original';args=@($catalog,'@result',$helper,$review.helperSha256,'--loki-only');exit=0;count=1},
  @{name='wrong-sha';module='catalog-original';args=@($catalog,'@result',$helper,('0'*64));exit=1;count=0}
 )
 foreach($replay in $replays){
  $receipt=Join-Path $out ($replay.name+'.json');$replayArguments=@($replay.args|ForEach-Object {if($_ -ceq '@result'){$receipt}else{$_}})
  $moduleFile=ModuleFile $manifest $replay.module $bundle
  RunChild $replay.name $dotnet (@($moduleFile)+$replayArguments) $replay.exit ($replay.name -ceq 'wrong-sha')|Out-Null
  if($replay.name -ceq 'wrong-sha'){Need (-not (Test-Path -LiteralPath $receipt)) 'WRONG_SHA_CREATED_SUCCESS';continue}
  $r=Get-Content -LiteralPath $receipt -Raw -Encoding UTF8|ConvertFrom-Json
  if($replay.name -ceq 'boundary-177'){Need ($r.passed -eq $true -and $r.checksPassed -eq 177 -and $r.actualSha256 -ceq $review.helperSha256 -and $r.mainEntryInvoked -eq $false -and @($r.nativeAttempts).Count -eq 0 -and @($r.dependencyAttempts).Count -eq 0) 'BOUNDARY_REPLAY_FAILED'}
  elseif($replay.name -ceq 'old-loki'){Need ($r.status -ceq 'FAIL' -and $r.checks -eq 1 -and $r.assemblySha256 -ceq $review.oldHelperSha256) 'OLD_LOKI_NOT_REPRODUCED'}
  else{Need ($r.status -ceq 'PASS' -and $r.checks -eq $replay.count -and $r.assemblySha256 -ceq $review.helperSha256 -and $r.productionEntryPointInvoked -eq $false -and $r.HCAssemblyLoaded -eq $false -and @($r.nativeAttempts).Count -eq 0) 'CATALOG_REPLAY_FAILED'}
 }
 $business=[Collections.Generic.List[object]]::new();$childPids=[Collections.Generic.List[int]]::new()
 foreach($engine in @(@{name='ps7';path=$core},@{name='ps51';path=$desktop})){
  $rawRoot=Join-Path $scratch $engine.name
  $test=RunChild ('transaction-'+$engine.name) $engine.path @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',(Join-Path $repo 'tools/hc-device-runtime/Test-LightingConnection.ps1'),'-PlanPath',$planPath,'-EnginePath',$engine.path,'-EvidenceRoot',$rawRoot) 0
  $rawFile=Join-Path $rawRoot 'verification.json';$raw=Get-Content -LiteralPath $rawFile -Raw -Encoding UTF8|ConvertFrom-Json
  Need ($raw.status -ceq 'PASS' -and $raw.checks -eq 59 -and @($raw.cases).Count -eq 59 -and @($raw.cases|Where-Object passed -NE $true).Count -eq 0 -and @($raw.executions).Count -eq 6) 'ORIGINAL_59_REJECTED'
  foreach($sourceRecord in $raw.sourceFiles){Need ((Hash $sourceRecord.path) -ceq $sourceRecord.sha256) 'ORIGINAL_SOURCE_CHANGED'}
  $archived=ArchiveFile $rawFile ($engine.name+'-original.json')
  foreach($entry in $raw.executions){
   $caseRoot=Join-Path $rawRoot $entry.case;$proofFile=[IO.Path]::GetFullPath($entry.proof)
   Need ($proofFile.StartsWith($rawRoot+'\',[StringComparison]::OrdinalIgnoreCase) -and (Hash $proofFile) -ceq $entry.proofSha256) 'BUSINESS_PROOF_OUTSIDE_OWN_RUN'
   $proof=Get-Content -LiteralPath $proofFile -Raw -Encoding UTF8|ConvertFrom-Json
   Need ($proof.processId -gt 0 -and $proof.processId -ne $test.pid -and $proof.scriptPath -ieq (Join-Path $repo 'tools/hc-device-runtime/connect-installed-lighting.ps1') -and $proof.hardwareIo -eq $false -and $proof.appRestarted -eq $false) 'CONNECTOR_PROCESS_RECEIPT'
   $childPids.Add([int]$proof.processId)
   if($entry.case -ceq 'success'){Need ($entry.exitCode -eq 0 -and $proof.passed -eq $true -and @($proof.changedFiles).Count -eq 2 -and $proof.truePreimages -eq 2) 'POSITIVE_TRANSACTION_FAILED'}
   elseif($entry.case -ceq 'locked-export'){Need ($entry.exitCode -eq 2 -and $proof.passed -eq $false -and $proof.rollbackPerformed -eq $true -and -not $proof.rollbackConflict -and -not $proof.rollbackError) 'REAL_LOCK_ROLLBACK_FAILED'}
   else{Need ($entry.exitCode -eq 2 -and $proof.passed -eq $false -and $proof.rollbackPerformed -eq $false) 'NEGATIVE_TRANSACTION_FAILED'}
   ArchiveFile $proofFile ($engine.name+'-'+$entry.case+'-proof.json')|Out-Null
   ArchiveFile (Join-Path $caseRoot 'plan.json') ($engine.name+'-'+$entry.case+'-plan.json')|Out-Null
   ArchiveFile (Join-Path $caseRoot 'run.log') ($engine.name+'-'+$entry.case+'-run.log')|Out-Null
   foreach($change in $plan.changes){
    $target=Join-Path $caseRoot ('installation/'+$change.relative);$expected=if($entry.case -ceq 'success'){$change.afterSha256}else{$change.beforeSha256};Need ((Hash $target) -ceq $expected) 'ACTUAL_TRANSACTION_TARGET_DIFFERS'
    $kind=if($change.relative.EndsWith('.dll')){'helper.dll'}else{'export.json'};ArchiveFile $target ($engine.name+'-'+$entry.case+'-'+$kind)|Out-Null
    $before=Join-Path $caseRoot ('connection/preimage/'+$change.relative);if(Test-Path -LiteralPath $before){Need ((Hash $before) -ceq $change.beforeSha256) 'ACTUAL_PREIMAGE_DIFFERS';ArchiveFile $before ($engine.name+'-'+$entry.case+'-pre-'+$kind)|Out-Null}
   }
  }
  $business.Add([pscustomobject]@{engine=$engine.name;checks=59;actualCases=6;sourceReport=$archived;sha256=(Hash $archived);processId=$test.pid})
 }
 # FINALIZE

 Need ($childPids.Count -eq 12) 'CONNECTOR_PROCESS_RECEIPTS_REQUIRED'
 ArchiveFile (Join-Path $scratch 'port/relocated-plan.json') 'portable-plan.json'|Out-Null
 Save (Join-Path $out 'outputs-map.json') @($outputMappings)
 $changed=@($inputs|Where-Object {(Hash (Join-Path $repo $_.path)) -cne $_.sha256})
 Need ($changed.Count -eq 0) 'INPUT_CHANGED_DURING_RUN'
 $result.sourceChanges=$changed;$result.processes=@($processes);$result.transactionReports=@($business)
 $result.cases=@([pscustomobject]@{id='portable-contract-controls';passed=$true;checks=19},[pscustomobject]@{id='actual-archived-consumer-replays';passed=$true;processes=6},[pscustomobject]@{id='original-ps7-transaction';passed=$true;checks=59},[pscustomobject]@{id='original-ps51-transaction';passed=$true;checks=59})
 $result.summary=@{originalBusinessChecksEachEngine=59;originalBusinessEngines=2;actualTransactions=12;portableControls=19;archivedConsumerProcesses=6;actualArtifactChecks=177;actualCatalogChecksEach=549;exactConnectorProcessIds=@($childPids);sourceInputs=$inputs.Count;sourceBundleManifestSha256=$review.sourceManifestSha256;admissionManifestSha256=$review.admissionManifestSha256;profile=$review.profile;tuple=$review.tuple;recordedOriginalPathsOpened=$false;immutableReferenceRoot=$plan.installRoot}
 $result.status='PASS';$success=$true
}catch{
 $result.status=if($_.Exception.Message -match 'GLOBAL_OWNED_JOB_REQUIRED|PWSH_REQUIRED|LIGHTING_REVIEWED_CONTRACT_MISSING'){'BLOCKED'}else{'FAIL'}
 $result.failure=@{message=$_.Exception.Message;scriptLine=$_.InvocationInfo.ScriptLineNumber;position=$_.InvocationInfo.PositionMessage}
}finally{
 $result.processes=@($processes)
 if($success -and $scratch){
  try{
   $resolved=(Resolve-Path -LiteralPath $scratch).ProviderPath;$tempBase=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\');$leaf=Split-Path -Leaf $resolved
   Need ($resolved -ieq [IO.Path]::GetFullPath($scratch) -and (Split-Path -Parent $resolved) -ieq $tempBase -and $leaf -match '^ymcc-lc-[0-9a-f]{12}$') 'SCRATCH_CLEANUP_SCOPE'
   Need (@(Get-ChildItem -LiteralPath $resolved -Recurse -Force|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count -eq 0) 'SCRATCH_CLEANUP_REPARSE'
   Remove-Item -LiteralPath $resolved -Recurse -Force
   $result.scratch=@{path=$scratch;removed=(-not (Test-Path -LiteralPath $scratch));generatedOutputsCopiedIntoCase=$true}
  }catch{$result.status='FAIL';$result.failure=@{message=$_.Exception.Message;phase='owned-scratch-cleanup'}}
 }elseif($scratch){$result.scratch=@{path=$scratch;removed=$false;retainedForFailure=$true}}
 $generated=@(Get-ChildItem -LiteralPath $out -Recurse -File|Where-Object Name -CNE 'result.json'|ForEach-Object {[pscustomobject]@{path=$_.FullName;sha256=(Hash $_.FullName)}})
 $result.sourceHashes=@($inputs)+$generated;$result.finishedUtc=[DateTime]::UtcNow.ToString('o')
 Save (Join-Path $out 'result.json') $result
}
Write-Output ('LIGHTING_COMPONENT_CONTINUITY '+$result.status)
if($result.status -ne 'PASS'){exit 2}