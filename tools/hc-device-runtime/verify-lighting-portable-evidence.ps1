# Reviewed portable lighting admission. Recorded original paths are labels only.
function Assert-YmccLightingPortableEvidence {
 param([Parameter(Mandatory=$true)]$Plan)
 $ErrorActionPreference='Stop';Set-StrictMode -Version Latest
 function Need([bool]$Value,[string]$Code){if(-not $Value){throw ('LIGHTING_'+$Code)}}
 function HashFile([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
 function Root([string]$Path){[IO.Path]::GetFullPath($Path).TrimEnd('\')}
 function File([string]$Base,[string]$Relative){
  Need (-not [string]::IsNullOrWhiteSpace($Relative) -and -not [IO.Path]::IsPathRooted($Relative) -and -not $Relative.Contains(':') -and $Relative -notmatch '(^|[\\/])\.\.?([\\/]|$)') 'BUNDLE_PATH'
  $p=[IO.Path]::GetFullPath((Join-Path $Base $Relative));Need ($p.StartsWith($Base+'\',[StringComparison]::OrdinalIgnoreCase)) 'BUNDLE_PATH'
  Need (Test-Path -LiteralPath $p -PathType Leaf) 'SOURCE_MISSING'
  $cursor=$p
  while($cursor.Length -ge $Base.Length){Need (-not ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) 'BUNDLE_REPARSE';if($cursor -ieq $Base){break};$cursor=Split-Path -Parent $cursor}
  return $p
 }
 function ReadJson([string]$Path){Get-Content -LiteralPath $Path -Raw -Encoding UTF8|ConvertFrom-Json}
 function CheckBundle([string]$Base,[string]$Name,[string]$Pin,[string[]]$Excluded){
  $manifestFile=File $Base $Name;Need ((HashFile $manifestFile) -ceq $Pin) 'BUNDLE_MANIFEST'
  $manifest=ReadJson $manifestFile;$declared=@($manifest.members|ForEach-Object path|Sort-Object)
  Need (@($manifest.members|Group-Object path|Where-Object Count -NE 1).Count -eq 0) 'BUNDLE_DUPLICATE'
  $physical=@(Get-ChildItem -LiteralPath $Base -Recurse -File|ForEach-Object {$_.FullName.Substring($Base.Length+1).Replace('\','/')}|Where-Object {$_ -cnotin $Excluded}|Sort-Object)
  Need ($declared.Count -eq $physical.Count -and -not (Compare-Object $declared $physical)) 'BUNDLE_MEMBER_SET'
  foreach($row in $manifest.members){$p=File $Base $row.path;Need ((HashFile $p) -ceq $row.sha256 -and (Get-Item -LiteralPath $p).Length -eq $row.bytes) 'BUNDLE_MEMBER_BYTES'}
  return $manifest
 }
 Need ($Plan.kind -ceq 'lighting-catalog-v3') 'ADMISSION_KIND'
 $contractPin='92f820533b2744fe695029b301111bdb528c9d5449af6ffdeb164496a1829d93'
 Need ($Plan.reviewedContract.sha256 -ceq $contractPin -and (HashFile $Plan.reviewedContract.path) -ceq $contractPin) 'REVIEWED_CONTRACT'
 $review=ReadJson $Plan.reviewedContract.path
 Need ($review.kind -ceq 'ymcc-lighting-reviewed-contract-v1' -and $review.profile -ceq 'ymcc-device-core-v1') 'REVIEWED_PROFILE'
 $helper=$review.helperSha256;$core=$review.tuple.independentCore
 Need ($helper -ceq '7bfce707580fd70edb808ec4b864dd4315ddbfe3e2cf5bf36fd956249622a2fe' -and $Plan.newHelperSha256 -ceq $helper -and $Plan.preservedCoreSha256 -ceq $core -and $Plan.preservedNativeSha256 -ceq $review.tuple.native) 'REVIEWED_ARTIFACT'
 $sourceRoot=Root $Plan.sourceBundleRoot;$cap=$Plan.lightingEvidence;$admissionRoot=Root $cap.bundleRoot
 Need ((Root $Plan.componentSourceRoot) -ieq (Join-Path $sourceRoot 'source')) 'SOURCE_MISSING'
 $source=CheckBundle $sourceRoot 'LIGHTSETTER-SOURCE.json' $review.sourceManifestSha256 @('LIGHTSETTER-SOURCE.json')
 $admission=CheckBundle $admissionRoot 'ADMISSION-BUNDLE.json' $review.admissionManifestSha256 @('ADMISSION-BUNDLE.json','REVIEWED-CONTRACT.json')
 Need ((Root $Plan.testPreimageRoot) -ieq (Join-Path $admissionRoot 'fixture/before')) 'PREIMAGE_ROOT'
 Need ($cap.manifest.sha256 -ceq $review.admissionManifestSha256 -and [IO.Path]::GetFullPath($cap.manifest.path) -ieq (File $admissionRoot 'ADMISSION-BUNDLE.json') -and (HashFile (File $admissionRoot 'REVIEWED-CONTRACT.json')) -ceq $contractPin) 'MANIFEST_REFERENCE'
 Need ($source.kind -ceq 'ymcc-lightsetter-source-v1' -and $source.helper.sha256 -ceq $helper -and $source.manualCompileSources -eq 12 -and $source.controlledSourceInputs -eq 27 -and @($source.members).Count -eq 45 -and $admission.kind -ceq 'ymcc-lighting-admission-v1' -and $admission.helperSha256 -ceq $helper -and $admission.sourceBundleManifestSha256 -ceq $review.sourceManifestSha256) 'BUNDLE_CONTRACT'
 function ReadProof([string]$Name,[string]$Pin){
  $declaration=$admission.proofs.$Name;$reference=$cap.$Name
  $base=if($declaration.bundle -ceq 'source'){$sourceRoot}else{$admissionRoot};$expected=File $base $declaration.path
  Need ($declaration.sha256 -ceq $Pin -and $reference.sha256 -ceq $Pin -and [IO.Path]::GetFullPath($reference.path) -ieq $expected -and (HashFile $expected) -ceq $Pin) 'PROOF_REFERENCE'
  return ReadJson $expected
 }
 function RecordedFile([string]$Original,[string]$Sum){
  $key=$Original.Replace('/','\');$rows=@($admission.recordedFiles|Where-Object {$_.originalPath.Replace('/','\') -ieq $key -and $_.sha256 -ceq $Sum})
  Need ($rows.Count -eq 1) 'RECORDED_FILE_BINDING';$record=$rows[0]
  Need ($record.bundle -cin @('source','evidence')) 'RECORDED_FILE_ROOT'
  $base=if($record.bundle -ceq 'source'){$sourceRoot}else{$admissionRoot};$p=File $base $record.path
  Need ((HashFile $p) -ceq $Sum) 'RECORDED_FILE_BYTES';return $p
 }
 $profile=ReadProof 'profile' $review.profileSha256
 Need ($profile.schemaVersion -eq 1 -and $profile.runtimeId -ceq 'YMCC-DEVICECORE-20261018-v1' -and $admission.profileRuntimeId -ceq $profile.runtimeId) 'PROFILE_BINDING'
 $sourceAudit=ReadProof 'sourceBundleVerification' $admission.proofs.sourceBundleVerification.sha256
 Need ($sourceAudit.status -ceq 'PASS' -and $sourceAudit.bundleManifestSha256 -ceq $review.sourceManifestSha256 -and $sourceAudit.helperSha256 -ceq $helper -and $sourceAudit.pdbSha256 -ceq $source.pdb.sha256 -and $sourceAudit.pairVerified -eq $true -and $sourceAudit.sourceDocuments -eq 15 -and $sourceAudit.manualCompileSources -eq 12 -and $sourceAudit.compilerRecords -eq 2 -and $sourceAudit.originalAbsoluteSourceFilesRequired -eq $false) 'PORTABLE_SOURCE_PROOF'
 $originalPlan=ReadProof 'originalPlan' '2464b03ade43bbec03fefb8cf6f9b39c378c471dda2a74b69c79a925f5f3af85'
 Need (@($Plan.preserved).Count -eq 103 -and @($Plan.preserved|Group-Object relative|Where-Object Count -NE 1).Count -eq 0) 'PRESERVED_SET'
 foreach($row in $originalPlan.preserved){Need (@($Plan.preserved|Where-Object {$_.relative -ceq $row.relative -and $_.sha256 -ceq $row.sha256}).Count -eq 1 -and (HashFile (File $admissionRoot ('fixture/before/'+$row.relative))) -ceq $row.sha256) 'PRESERVED_BINDING'}
 Need (@($Plan.changes).Count -eq 2 -and @($Plan.changes|Group-Object relative|Where-Object Count -NE 1).Count -eq 0) 'CHANGED_SET'
 foreach($row in $originalPlan.changes){Need (@($Plan.changes|Where-Object {$_.relative -ceq $row.relative -and $_.beforeSha256 -ceq $row.beforeSha256 -and $_.afterSha256 -ceq $row.afterSha256 -and $_.sourceRelative -ceq $row.sourceRelative}).Count -eq 1) 'CHANGED_BINDING'}
 $tuplePaths=@{native='YeManCC/YeManCC.exe';fanHost='PowerControl/fan-host-v2/YeManFanHost.dll';fanBackend='PowerControl/fan-host-v2/YeManFanBackend.dll';fanManifest='PowerControl/fan-host-v2/YeManFanHost.payload.json';runtimeManifest='PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/HandheldCompanion.runtime.json';hc='PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/HandheldCompanion.dll';independentCore='PowerControl/light-setter/YeManLightDeviceCore.dll'}
 foreach($name in $tuplePaths.Keys){Need ((HashFile (File $admissionRoot ('fixture/before/'+$tuplePaths[$name]))) -ceq $review.tuple.$name) 'TUPLE_BINDING'}
 Need (@($admission.modules).Count -eq 3) 'REPLAY_MODULE_SET'
 $expectedDocs=@{'catalog-original'=8;'boundary-177'=19;'catalog-maintained'=16}
 foreach($module in $admission.modules){
  Need ($expectedDocs.ContainsKey($module.id)) 'REPLAY_MODULE_KIND'
  $metadataFile=File $admissionRoot $module.metadata.path;Need ((HashFile $metadataFile) -ceq $module.metadata.sha256) 'REPLAY_METADATA_BYTES';$metadata=ReadJson $metadataFile
  Need ($metadata.status -ceq 'PASS' -and $metadata.pairVerified -eq $true -and $metadata.runtimeAssemblyLoaded -eq $false -and @($metadata.documents).Count -eq $expectedDocs[$module.id] -and @($metadata.compilationRecords).Count -eq 2 -and @($module.documents).Count -eq $expectedDocs[$module.id]) 'REPLAY_PDB_PROOF'
  foreach($suffix in @('.dll','.pdb','.deps.json','.runtimeconfig.json')){$asset=$module.artifacts.PSObject.Properties[$suffix].Value;Need ((HashFile (File $admissionRoot $asset.path)) -ceq $asset.sha256) 'REPLAY_MODULE_BYTES'}
  Need ($metadata.assemblySha256 -ceq $module.artifacts.PSObject.Properties['.dll'].Value.sha256 -and $metadata.pdbSha256 -ceq $module.artifacts.PSObject.Properties['.pdb'].Value.sha256) 'REPLAY_PDB_PAIR'
  foreach($doc in $metadata.documents){$rows=@($module.documents|Where-Object {$_.originalPath -ceq $doc.originalPath -and $_.sha256 -ceq $doc.sha256});Need ($rows.Count -eq 1 -and (HashFile (File $admissionRoot $rows[0].path)) -ceq $doc.sha256) 'REPLAY_SOURCE_BYTES'}
 }
 $snapshot=ReadProof 'snapshot' '0af131a0757d71841c5c7d7b437d37780a6aa7daf3620d50600104768eebc25f'
 Need ($snapshot.sourceChangedDuringCapture -eq $false -and $snapshot.productCompileCount -eq 12) 'SNAPSHOT'
 $compiledRoot=[IO.Path]::GetFullPath($Plan.componentSourceRoot).TrimEnd('\')
 foreach($record in @($snapshot.sourceInputs)){
  $rel=[string]$record.path
  Need (-not [IO.Path]::IsPathRooted($rel) -and -not $rel.Contains(':') -and $rel -notmatch '(^|[\\/])\.\.?([\\/]|$)') 'SOURCE_PATH'
  $file=[IO.Path]::GetFullPath((Join-Path $compiledRoot $rel))
  Need ($file.StartsWith($compiledRoot+'\',[StringComparison]::OrdinalIgnoreCase)) 'SOURCE_PATH'
  Need (Test-Path -LiteralPath $file -PathType Leaf) 'SOURCE_MISSING'
  Need ((HashFile $file) -ceq $record.sha256) 'SOURCE_BYTES'
 }
 $compiled=@($snapshot.sourceInputs|Where-Object {'Compile' -cin @($_.roles) -or 'project' -cin @($_.roles) -or 'Content' -cin @($_.roles)})
 Need ($compiled.Count -eq 14 -and @($Plan.compiledInputs).Count -eq 14 -and @($Plan.compiledInputs|Group-Object path|Where-Object Count -NE 1).Count -eq 0) 'COMPILED_SET'
 foreach($record in $compiled){Need (@($Plan.compiledInputs|Where-Object {$_.path -ceq $record.path -and $_.sha256 -ceq $record.sha256}).Count -eq 1) 'COMPILED_BINDING'}
 $pdb=ReadProof 'pdb' '0db2c68067ae4847225df390082e6b67ef05181cc190491d4730b6256dbd6142'
 Need ($pdb.status -ceq 'PASS' -and $pdb.pairVerified -eq $true -and $pdb.helper.sha256 -ceq $helper -and (HashFile (RecordedFile $pdb.helper.path $pdb.helper.sha256)) -ceq $helper -and (HashFile (RecordedFile $pdb.pdb.path $pdb.pdb.sha256)) -ceq $pdb.pdb.sha256 -and $pdb.compileSourceInputs -eq 12 -and $pdb.generatedDocuments -eq 3 -and @($pdb.documents).Count -eq 15 -and @($pdb.compilationRecords).Count -eq 2) 'PDB_PAIR'
 foreach($doc in $pdb.documents){Need ((HashFile (RecordedFile $doc.path $doc.checksum)) -ceq $doc.checksum) 'PDB_DOCUMENT'}
 foreach($record in @($compiled|Where-Object {'Compile' -cin @($_.roles)})){Need (@($pdb.documents|Where-Object {$_.relative -ceq $record.path -and $_.checksum -ceq $record.sha256 -and $_.capturedCompileInput -eq $true}).Count -eq 1) 'PDB_COMPILE_BINDING'}
 $execution=ReadProof 'catalog' 'f7f69d58e30c1902764447d558f8771b2fb6a71c0ea1f7a1ebae324652b0a7ee'
 Need ($execution.status -ceq 'PASS' -and $execution.actualHelperSha256 -ceq $helper -and $execution.existingAssertions -eq 549 -and $execution.models -eq 47 -and $execution.assertionsCallingActualTarget -eq 548 -and $execution.targetCalls -eq 552 -and $execution.realDeviceIo -eq $false -and $execution.mainEntryInvoked -eq $false) 'ACTUAL_CATALOG'
 foreach($record in $execution.sourceInputs){Need ((HashFile (RecordedFile $record.path $record.sha256)) -ceq $record.sha256) 'CATALOG_SOURCE'}
 Need ((HashFile (RecordedFile $execution.harness.path $execution.harness.sha256)) -ceq $execution.harness.sha256) 'CATALOG_HARNESS'
 $steps=@($execution.steps);Need ($steps.Count -eq 4 -and @($steps|Where-Object {$_.naturalExit -ne $true}).Count -eq 0) 'CATALOG_EXECUTION'
 foreach($step in $steps){if($null -ne $step.result){Need ((HashFile (RecordedFile $step.result.path $step.result.sha256)) -ceq $step.result.sha256) 'CATALOG_RAW_REPORT'}}
 $full=@($steps|Where-Object id -CEQ 'candidate-full');Need ($full.Count -eq 1 -and $full[0].exitCode -eq 0) 'CATALOG_PROCESS'
 $actual=Get-Content -LiteralPath (RecordedFile $full[0].result.path $full[0].result.sha256) -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($actual.status -ceq 'PASS' -and $actual.checks -eq 549 -and @($actual.cases).Count -eq 549 -and @($actual.failures).Count -eq 0 -and $actual.models -eq 47 -and $actual.assemblySha256 -ceq $helper -and (HashFile (RecordedFile $actual.assemblyPath $actual.assemblySha256)) -ceq $helper -and $actual.hardwareIo -eq $false -and $actual.HCAssemblyLoaded -eq $false -and $actual.productionEntryPointInvoked -eq $false -and @($actual.nativeAttempts).Count -eq 0 -and @($actual.dependencyAttempts).Count -eq 0) 'CATALOG_BEHAVIOR'
 Need (@($actual.cases|Where-Object {$_.ok -ne $true}).Count -eq 0 -and @($actual.cases|Where-Object actualTargetCalls -GT 0).Count -eq 548 -and @($actual.targetCalls).Count -eq 552) 'CATALOG_ASSERTIONS'
 $old=@($steps|Where-Object id -CEQ 'old-loki-control');$fixed=@($steps|Where-Object id -CEQ 'candidate-loki-control');$badSha=@($steps|Where-Object id -CEQ 'wrong-sha-refused')
 Need ($old.Count -eq 1 -and $old[0].exitCode -ne 0 -and $fixed.Count -eq 1 -and $fixed[0].exitCode -eq 0 -and $badSha.Count -eq 1 -and $badSha[0].exitCode -ne 0 -and $null -eq $badSha[0].result) 'CATALOG_CONTROLS'
 $oldReport=Get-Content -LiteralPath (RecordedFile $old[0].result.path $old[0].result.sha256) -Raw -Encoding UTF8|ConvertFrom-Json
 $fixedReport=Get-Content -LiteralPath (RecordedFile $fixed[0].result.path $fixed[0].result.sha256) -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($oldReport.status -ceq 'FAIL' -and $oldReport.checks -eq 1 -and $oldReport.cases[0].id -ceq 'loki-off-reaches-black-color-writer' -and $oldReport.cases[0].ok -eq $false -and $fixedReport.status -ceq 'PASS' -and $fixedReport.assemblySha256 -ceq $helper) 'LOKI_CONTROL'
 $outer=ReadProof 'global' 'fc4277ddd080ad4fac0b3a53e0e85ecebd537ac7e58e23aae75220b3c160e6b4'
 Need ($outer.verdict.status -ceq 'PASS_SELECTED_SCOPE' -and $outer.verdict.exitCode -eq 0 -and @($outer.results).Count -eq 1 -and $outer.results[0].id -ceq 'lighting.signal-consumer' -and $outer.results[0].status -ceq 'PASS' -and $outer.results[0].exitCode -eq 0 -and $outer.results[0].sourceChanged -eq $false -and $outer.results[0].candidateArtifactChanged -eq $false -and $outer.results[0].requiredEvidenceMissing -eq $false -and $outer.results[0].requiredEvidenceFailed -eq $false -and $outer.results[0].timedOut -eq $false -and $outer.candidateInputs.expectedHelperSha256 -ceq $helper) 'GLOBAL_EXECUTION'
 $proof=ReadProof 'signal' '902b361c299cf60bc09a7f2704168c087c5efde51f6897af4e7f420018e243bb'
 $candidate=$proof.compatibility.payloadCandidate;$behavior=$candidate.behavior
 $catalog=$candidate.catalogBehavior
 Need ($catalog.status -ceq 'PASS' -and $catalog.artifactMode -eq $true -and $catalog.checks -eq 549 -and $catalog.models -eq 47 -and $catalog.assemblySha256 -ceq $helper -and (HashFile (RecordedFile $catalog.assemblyPath $catalog.assemblySha256)) -ceq $helper -and $catalog.hardwareIo -eq $false -and $catalog.HCAssemblyLoaded -eq $false -and $catalog.productionEntryPointInvoked -eq $false -and @($catalog.cases|Where-Object {$_.ok -ne $true}).Count -eq 0 -and @($catalog.cases|Where-Object actualTargetCalls -GT 0).Count -eq 548 -and @($catalog.targetCalls).Count -eq 552 -and @($catalog.nativeAttempts).Count -eq 0 -and @($catalog.dependencyAttempts).Count -eq 0) 'MAINTAINED_ACTUAL_CATALOG' 
 Need ($proof.status -ceq 'PASS' -and $proof.hardwareIo -eq $false -and @($proof.compatibility.differences).Count -eq 0 -and @($proof.compatibility.changedDuringRun).Count -eq 0 -and $candidate.actual -ceq $helper -and $candidate.expectedReviewedSha256 -ceq $helper -and (HashFile (RecordedFile $candidate.file $candidate.actual)) -ceq $helper -and $behavior.passed -eq $true -and $behavior.actualSha256 -ceq $helper -and $behavior.checksPassed -eq 177 -and $behavior.mainEntryInvoked -eq $false -and @($behavior.nativeAttempts).Count -eq 0 -and @($behavior.dependencyAttempts).Count -eq 0) 'EXACT_SIGNAL_ARTIFACT'
 foreach($contract in @('boundHidLightingContract','onexRequestCacheContract','onexLightingContract','msiLightingContract')){Need ($proof.$contract.status -ceq 'PASS' -and $proof.$contract.passed -eq $true) 'TRANSPORT_CONTRACT'}
 foreach($name in @('actual Run is wired to the lighting-only boundary','actual Run prepares the OneX lighting transport session','actual Run releases the OneX transport in finally','actual action consumer prepares requested serial cache before dispatch','actual preset consumer prepares explicit brightness cache before dispatch')){Need (@($behavior.checks|Where-Object {$_.name -ceq $name -and $_.passed -eq $true}).Count -eq 1) 'OPEN_LIFECYCLE'}

 Need ($oldReport.assemblySha256 -ceq $review.oldHelperSha256 -and (HashFile (RecordedFile $oldReport.assemblyPath $oldReport.assemblySha256)) -ceq $review.oldHelperSha256) 'OLD_BINARY_CONTROL'
 return [pscustomobject]@{status='PASS';contract='lighting-catalog-v3';helperSha256=$helper;coreSha256=$core;sourceInputs=27;compiledInputs=14;compileSources=12;pdbDocuments=15;actualCatalogChecks=549;actualTargetAssertions=548;actualTargetCalls=552;models=47;actualArtifactChecks=177;oldLokiFailureRetained=$true;hardwareAcceptance=$false;installationGranted=$false;sourceBundleMembers=45;admissionBundleMembers=@($admission.members).Count;replayModules=3;originalAbsoluteSourceFilesRequired=$false;recordedPathsOpened=$false}

}