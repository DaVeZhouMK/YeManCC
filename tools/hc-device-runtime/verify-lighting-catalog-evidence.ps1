# Candidate-specific source and actual-artifact admission. No installation operations.
function Assert-YmccLightingCatalogEvidence {
 param([Parameter(Mandatory=$true)]$Plan)
 $ErrorActionPreference='Stop'
 Set-StrictMode -Version Latest
 if($Plan.kind -ceq 'lighting-build-v4'){. (Join-Path $PSScriptRoot 'lighting-build-delivery.ps1');return Assert-YmccLightingBuildDelivery -Plan $Plan}
 if($Plan.kind -ceq 'lighting-catalog-v3'){. (Join-Path $PSScriptRoot 'verify-lighting-portable-evidence.ps1');return Assert-YmccLightingPortableEvidence -Plan $Plan}
 if($Plan.kind -ceq 'lighting-catalog-v2' -and $Plan.PSObject.Properties['reviewedContract']){throw 'LIGHTING_V3_REQUIRED'}
 function Need([bool]$Value,[string]$Code){if(-not $Value){throw ('LIGHTING_'+$Code)}}
 function HashFile([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()}
 function ReadProof($Reference,[string]$Pin){
  Need ($Reference.sha256 -ceq $Pin -and (HashFile $Reference.path) -ceq $Pin) 'PROOF_REFERENCE'
  return Get-Content -LiteralPath $Reference.path -Raw -Encoding UTF8|ConvertFrom-Json
 }
 $helper='7bfce707580fd70edb808ec4b864dd4315ddbfe3e2cf5bf36fd956249622a2fe'
 $core='88e5a77f781b2bf7e12df4d45b636b16f97031d02e8617572c7bba7176b8fb7e'
 Need ($Plan.kind -ceq 'lighting-catalog-v2') 'ADMISSION_KIND'
 Need ($Plan.newHelperSha256 -ceq $helper -and $Plan.preservedCoreSha256 -ceq $core) 'REVIEWED_ARTIFACT'
 $cap=$Plan.lightingEvidence
 $snapshot=ReadProof $cap.snapshot '0af131a0757d71841c5c7d7b437d37780a6aa7daf3620d50600104768eebc25f'
 Need ($snapshot.sourceChangedDuringCapture -eq $false -and $snapshot.productCompileCount -eq 12) 'SNAPSHOT'
 $sourceRoot=[IO.Path]::GetFullPath($Plan.componentSourceRoot).TrimEnd('\')
 foreach($record in @($snapshot.sourceInputs)){
  $rel=[string]$record.path
  Need (-not [IO.Path]::IsPathRooted($rel) -and -not $rel.Contains(':') -and $rel -notmatch '(^|[\\/])\.\.?([\\/]|$)') 'SOURCE_PATH'
  $file=[IO.Path]::GetFullPath((Join-Path $sourceRoot $rel))
  Need ($file.StartsWith($sourceRoot+'\',[StringComparison]::OrdinalIgnoreCase)) 'SOURCE_PATH'
  Need (Test-Path -LiteralPath $file -PathType Leaf) 'SOURCE_MISSING'
  Need ((HashFile $file) -ceq $record.sha256) 'SOURCE_BYTES'
 }
 $compiled=@($snapshot.sourceInputs|Where-Object {'Compile' -cin @($_.roles) -or 'project' -cin @($_.roles) -or 'Content' -cin @($_.roles)})
 Need ($compiled.Count -eq 14 -and @($Plan.compiledInputs).Count -eq 14 -and @($Plan.compiledInputs|Group-Object path|Where-Object Count -NE 1).Count -eq 0) 'COMPILED_SET'
 foreach($record in $compiled){Need (@($Plan.compiledInputs|Where-Object {$_.path -ceq $record.path -and $_.sha256 -ceq $record.sha256}).Count -eq 1) 'COMPILED_BINDING'}
 $pdb=ReadProof $cap.pdb '0db2c68067ae4847225df390082e6b67ef05181cc190491d4730b6256dbd6142'
 Need ($pdb.status -ceq 'PASS' -and $pdb.pairVerified -eq $true -and $pdb.helper.sha256 -ceq $helper -and (HashFile $pdb.helper.path) -ceq $helper -and (HashFile $pdb.pdb.path) -ceq $pdb.pdb.sha256 -and $pdb.compileSourceInputs -eq 12 -and $pdb.generatedDocuments -eq 3 -and @($pdb.documents).Count -eq 15 -and @($pdb.compilationRecords).Count -eq 2) 'PDB_PAIR'
 foreach($doc in $pdb.documents){Need ((HashFile $doc.path) -ceq $doc.checksum) 'PDB_DOCUMENT'}
 foreach($record in @($compiled|Where-Object {'Compile' -cin @($_.roles)})){Need (@($pdb.documents|Where-Object {$_.relative -ceq $record.path -and $_.checksum -ceq $record.sha256 -and $_.capturedCompileInput -eq $true}).Count -eq 1) 'PDB_COMPILE_BINDING'}
 $execution=ReadProof $cap.catalog 'f7f69d58e30c1902764447d558f8771b2fb6a71c0ea1f7a1ebae324652b0a7ee'
 Need ($execution.status -ceq 'PASS' -and $execution.actualHelperSha256 -ceq $helper -and $execution.existingAssertions -eq 549 -and $execution.models -eq 47 -and $execution.assertionsCallingActualTarget -eq 548 -and $execution.targetCalls -eq 552 -and $execution.realDeviceIo -eq $false -and $execution.mainEntryInvoked -eq $false) 'ACTUAL_CATALOG'
 foreach($record in $execution.sourceInputs){Need ((HashFile $record.path) -ceq $record.sha256) 'CATALOG_SOURCE'}
 Need ((HashFile $execution.harness.path) -ceq $execution.harness.sha256) 'CATALOG_HARNESS'
 $steps=@($execution.steps);Need ($steps.Count -eq 4 -and @($steps|Where-Object {$_.naturalExit -ne $true}).Count -eq 0) 'CATALOG_EXECUTION'
 foreach($step in $steps){if($null -ne $step.result){Need ((HashFile $step.result.path) -ceq $step.result.sha256) 'CATALOG_RAW_REPORT'}}
 $full=@($steps|Where-Object id -CEQ 'candidate-full');Need ($full.Count -eq 1 -and $full[0].exitCode -eq 0) 'CATALOG_PROCESS'
 $actual=Get-Content -LiteralPath $full[0].result.path -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($actual.status -ceq 'PASS' -and $actual.checks -eq 549 -and @($actual.cases).Count -eq 549 -and @($actual.failures).Count -eq 0 -and $actual.models -eq 47 -and $actual.assemblySha256 -ceq $helper -and (HashFile $actual.assemblyPath) -ceq $helper -and $actual.hardwareIo -eq $false -and $actual.HCAssemblyLoaded -eq $false -and $actual.productionEntryPointInvoked -eq $false -and @($actual.nativeAttempts).Count -eq 0 -and @($actual.dependencyAttempts).Count -eq 0) 'CATALOG_BEHAVIOR'
 Need (@($actual.cases|Where-Object {$_.ok -ne $true}).Count -eq 0 -and @($actual.cases|Where-Object actualTargetCalls -GT 0).Count -eq 548 -and @($actual.targetCalls).Count -eq 552) 'CATALOG_ASSERTIONS'
 $old=@($steps|Where-Object id -CEQ 'old-loki-control');$fixed=@($steps|Where-Object id -CEQ 'candidate-loki-control');$badSha=@($steps|Where-Object id -CEQ 'wrong-sha-refused')
 Need ($old.Count -eq 1 -and $old[0].exitCode -ne 0 -and $fixed.Count -eq 1 -and $fixed[0].exitCode -eq 0 -and $badSha.Count -eq 1 -and $badSha[0].exitCode -ne 0 -and $null -eq $badSha[0].result) 'CATALOG_CONTROLS'
 $oldReport=Get-Content -LiteralPath $old[0].result.path -Raw -Encoding UTF8|ConvertFrom-Json
 $fixedReport=Get-Content -LiteralPath $fixed[0].result.path -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($oldReport.status -ceq 'FAIL' -and $oldReport.checks -eq 1 -and $oldReport.cases[0].id -ceq 'loki-off-reaches-black-color-writer' -and $oldReport.cases[0].ok -eq $false -and $fixedReport.status -ceq 'PASS' -and $fixedReport.assemblySha256 -ceq $helper) 'LOKI_CONTROL'
 $outer=ReadProof $cap.global 'fc4277ddd080ad4fac0b3a53e0e85ecebd537ac7e58e23aae75220b3c160e6b4'
 Need ($outer.verdict.status -ceq 'PASS_SELECTED_SCOPE' -and $outer.verdict.exitCode -eq 0 -and @($outer.results).Count -eq 1 -and $outer.results[0].id -ceq 'lighting.signal-consumer' -and $outer.results[0].status -ceq 'PASS' -and $outer.results[0].exitCode -eq 0 -and $outer.results[0].sourceChanged -eq $false -and $outer.results[0].candidateArtifactChanged -eq $false -and $outer.results[0].requiredEvidenceMissing -eq $false -and $outer.results[0].requiredEvidenceFailed -eq $false -and $outer.results[0].timedOut -eq $false -and $outer.candidateInputs.expectedHelperSha256 -ceq $helper) 'GLOBAL_EXECUTION'
 $proof=ReadProof $cap.signal '902b361c299cf60bc09a7f2704168c087c5efde51f6897af4e7f420018e243bb'
 $candidate=$proof.compatibility.payloadCandidate;$behavior=$candidate.behavior
 $catalog=$candidate.catalogBehavior
 Need ($catalog.status -ceq 'PASS' -and $catalog.artifactMode -eq $true -and $catalog.checks -eq 549 -and $catalog.models -eq 47 -and $catalog.assemblySha256 -ceq $helper -and (HashFile $catalog.assemblyPath) -ceq $helper -and $catalog.hardwareIo -eq $false -and $catalog.HCAssemblyLoaded -eq $false -and $catalog.productionEntryPointInvoked -eq $false -and @($catalog.cases|Where-Object {$_.ok -ne $true}).Count -eq 0 -and @($catalog.cases|Where-Object actualTargetCalls -GT 0).Count -eq 548 -and @($catalog.targetCalls).Count -eq 552 -and @($catalog.nativeAttempts).Count -eq 0 -and @($catalog.dependencyAttempts).Count -eq 0) 'MAINTAINED_ACTUAL_CATALOG' 
 Need ($proof.status -ceq 'PASS' -and $proof.hardwareIo -eq $false -and @($proof.compatibility.differences).Count -eq 0 -and @($proof.compatibility.changedDuringRun).Count -eq 0 -and $candidate.actual -ceq $helper -and $candidate.expectedReviewedSha256 -ceq $helper -and (HashFile $candidate.file) -ceq $helper -and $behavior.passed -eq $true -and $behavior.actualSha256 -ceq $helper -and $behavior.checksPassed -eq 177 -and $behavior.mainEntryInvoked -eq $false -and @($behavior.nativeAttempts).Count -eq 0 -and @($behavior.dependencyAttempts).Count -eq 0) 'EXACT_SIGNAL_ARTIFACT'
 foreach($contract in @('boundHidLightingContract','onexRequestCacheContract','onexLightingContract','msiLightingContract')){Need ($proof.$contract.status -ceq 'PASS' -and $proof.$contract.passed -eq $true) 'TRANSPORT_CONTRACT'}
 foreach($name in @('actual Run is wired to the lighting-only boundary','actual Run prepares the OneX lighting transport session','actual Run releases the OneX transport in finally','actual action consumer prepares requested serial cache before dispatch','actual preset consumer prepares explicit brightness cache before dispatch')){Need (@($behavior.checks|Where-Object {$_.name -ceq $name -and $_.passed -eq $true}).Count -eq 1) 'OPEN_LIFECYCLE'}
 return [pscustomobject]@{status='PASS';contract='lighting-catalog-v2';helperSha256=$helper;coreSha256=$core;sourceInputs=27;compiledInputs=14;compileSources=12;pdbDocuments=15;actualCatalogChecks=549;actualTargetAssertions=548;actualTargetCalls=552;models=47;actualArtifactChecks=177;oldLokiFailureRetained=$true;hardwareAcceptance=$false;installationGranted=$false}
}