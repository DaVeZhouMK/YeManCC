[CmdletBinding(DefaultParameterSetName='Candidate')]
param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$WorkspaceRoot,[Parameter(Mandatory=$true,ParameterSetName='Candidate')][string]$PayloadRoot,[Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true,ParameterSetName='Shadow')][string]$ShadowCandidateReceiptPath,[Parameter(Mandatory=$true,ParameterSetName='Shadow')][string]$ShadowOldContractPath,[Parameter(Mandatory=$true,ParameterSetName='Shadow')][string]$ShadowMainSnapshotPath,[Parameter(Mandatory=$true,ParameterSetName='Acl')][switch]$PromotionAcl)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $EvidenceDir){throw 'Fresh lighting build-candidate evidence required'}
New-Item -ItemType Directory -Path $EvidenceDir|Out-Null
if($PSCmdlet.ParameterSetName -ceq 'Acl'){
 & {
  $issuer=Join-Path $PSScriptRoot 'build-lightsetter-payload.ps1';$parseErrors=$null;$parseTokens=$null
  $issuerAst=[Management.Automation.Language.Parser]::ParseFile($issuer,[ref]$parseTokens,[ref]$parseErrors)
  if($parseErrors.Count){throw 'PROMOTION_ACL_ISSUER_PARSE'}
  foreach($name in @('Replace','Restore-YmccLightingExactAcl')){
   $definition=@($issuerAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
   if($definition.Count -ne 1){throw 'PROMOTION_ACL_FUNCTION_EXTRACTION'}
   . ([scriptblock]::Create($definition[0].Extent.Text))
  }
  . (Join-Path $PSScriptRoot 'lighting-build-contract.ps1')
  $promotionNodes=@($issuerAst.FindAll({param($node)$node -is [Management.Automation.Language.ForEachStatementAst] -and $node.Extent.Text.Contains('$applied.Add($change)')},$true))
  $rollbackNodes=@($issuerAst.FindAll({param($node)$node -is [Management.Automation.Language.ForStatementAst] -and $node.Extent.Text.StartsWith('for($i=$applied.Count-1;')},$true))
  if($promotionNodes.Count -ne 1 -or $rollbackNodes.Count -ne 1){throw 'PROMOTION_ACL_TRANSACTION_EXTRACTION'}
  $promotion=[scriptblock]::Create($promotionNodes[0].Extent.Text);$rollback=[scriptblock]::Create($rollbackNodes[0].Extent.Text)
  $originalRestore=(Get-Command Restore-YmccLightingExactAcl -CommandType Function).ScriptBlock
  $checks=[Collections.Generic.List[object]]::new()
  function Check([bool]$value,[string]$name){$checks.Add([pscustomobject]@{name=$name;passed=$value});if(-not $value){throw ('PROMOTION_ACL_REGRESSION_FAILED '+$name)}}
  function Stage([string]$name,[bool]$existing=$true){
   $dir=Resolve-YmccLightingChild $EvidenceDir $name;New-Item -ItemType Directory -Path $dir|Out-Null
   $from=Join-Path $dir 'new.bin';$to=Join-Path $dir 'target.bin';$backup=Join-Path $dir 'backup.bin'
   [IO.File]::WriteAllText($from,'candidate bytes');$before=$null;$sddl=$null
   if($existing){[IO.File]::WriteAllText($to,'original bytes');$before=Get-YmccLightingFileHash $to;$sddl=(Get-Acl -LiteralPath $to).Sddl;Copy-Item -LiteralPath $to -Destination $backup}
   return [pscustomobject]@{name=$name;from=$from;to=$to;before=$before;after=(Get-YmccLightingFileHash $from);sddl=$sddl;backup=$backup}
  }
  $change=Stage 'original-failure';Replace $change.from $change.to
  $originalFailureReproduced=((Get-Acl -LiteralPath $change.to).Sddl -cne $change.sddl)
  Restore-YmccLightingExactAcl $change.to $change.sddl
  Check ((Get-Acl -LiteralPath $change.to).Sddl -ceq $change.sddl) 'exact original inherited DACL restored after actual Replace'
  Restore-YmccLightingExactAcl $change.to $change.sddl
  Check ((Get-Acl -LiteralPath $change.to).Sddl -ceq $change.sddl) 'already identical full descriptor is a no-op'
  $changes=@((Stage 'normal'),(Stage 'new-file' $false));$applied=[Collections.Generic.List[object]]::new();. $promotion
  Check ($applied.Count -eq 2) 'actual promotion loop accepts existing and new files'
  Check ((Get-YmccLightingFileHash $changes[0].to) -ceq $changes[0].after -and (Get-YmccLightingFileHash $changes[1].to) -ceq $changes[1].after) 'both payload hashes remain exact'
  Check ((Get-Acl -LiteralPath $changes[0].to).Sddl -ceq $changes[0].sddl) 'actual admission retains exact full SDDL guard'
  $rollbackErrors=@();. $rollback
  Check ($rollbackErrors.Count -eq 0 -and (Get-YmccLightingFileHash $changes[0].to) -ceq $changes[0].before -and (Get-Acl -LiteralPath $changes[0].to).Sddl -ceq $changes[0].sddl) 'actual rollback restores original bytes and exact permissions'
  Check (-not(Test-Path -LiteralPath $changes[1].to)) 'actual rollback removes only the newly created file'
  $change=Stage 'protected';$acl=Get-Acl -LiteralPath $change.to;$acl.SetAccessRuleProtection($true,$true);Set-Acl -LiteralPath $change.to -AclObject $acl;$change.sddl=(Get-Acl -LiteralPath $change.to).Sddl
  $changes=@($change);$applied=[Collections.Generic.List[object]]::new();. $promotion
  Check ((Get-Acl -LiteralPath $change.to).AreAccessRulesProtected -and (Get-Acl -LiteralPath $change.to).Sddl -ceq $change.sddl) 'explicit protected DACL is preserved'
  $change=Stage 'unexpected-metadata';$saved=Get-Acl -LiteralPath $change.to;$saved.SetAccessRuleProtection($true,$true);Set-Acl -LiteralPath $change.to -AclObject $saved;$unexpected=(Get-Acl -LiteralPath $change.to).Sddl;$errorText=''
  try{Restore-YmccLightingExactAcl $change.to $change.sddl}catch{$errorText=$_.Exception.Message}
  Check ($errorText -ceq 'LIGHTING_CANDIDATE_ACL_CHANGED' -and (Get-Acl -LiteralPath $change.to).Sddl -ceq $unexpected) 'other metadata changes are refused without overwriting them'
  $changes=@((Stage 'stale-before'));[IO.File]::WriteAllText($changes[0].to,'concurrent before promotion');$applied=[Collections.Generic.List[object]]::new();$errorText=''
  try{. $promotion}catch{$errorText=$_.Exception.Message}
  Check ($errorText -ceq 'LIGHTING_CANDIDATE_PREIMAGE_CHANGED' -and $applied.Count -eq 0 -and [IO.File]::ReadAllText($changes[0].to) -ceq 'concurrent before promotion') 'original stale preimage guard still stops before replacement'
  & {
   $script:lightingAclFailOnce=$true
   function Restore-YmccLightingExactAcl([string]$Path,[string]$Sddl){if($script:lightingAclFailOnce){$script:lightingAclFailOnce=$false;throw 'INJECTED_ACL_RESTORE_FAILURE'};& $originalRestore $Path $Sddl}
   $changes=@((Stage 'restore-failure'));$applied=[Collections.Generic.List[object]]::new();$errorText=''
   try{. $promotion}catch{$errorText=$_.Exception.Message};$rollbackErrors=@();. $rollback
   Check ($errorText -ceq 'INJECTED_ACL_RESTORE_FAILURE' -and $rollbackErrors.Count -eq 0 -and (Get-YmccLightingFileHash $changes[0].to) -ceq $changes[0].before -and (Get-Acl -LiteralPath $changes[0].to).Sddl -ceq $changes[0].sddl) 'restore failure rolls back original bytes and exact descriptor'
  }
  & {
   function Restore-YmccLightingExactAcl([string]$Path,[string]$Sddl){throw 'INJECTED_ACL_RESTORE_FAILURE'}
   $changes=@((Stage 'concurrent-rollback'));$applied=[Collections.Generic.List[object]]::new();try{. $promotion}catch{}
   [IO.File]::WriteAllText($changes[0].to,'newer owner bytes');$rollbackErrors=@();. $rollback
   Check ($rollbackErrors.Count -eq 1 -and [IO.File]::ReadAllText($changes[0].to) -ceq 'newer owner bytes') 'rollback refuses to overwrite a newer writer'
  }
  Check (@(Get-ChildItem -LiteralPath $EvidenceDir -File -Recurse -Filter '*.ymcc-*.tmp').Count -eq 0) 'all owned replacement temporary files cleaned'
  $report=[ordered]@{schemaVersion=1;status='PASS';checks=$checks.Count;cases=@($checks);scope='AST-extracted actual promotion and rollback loops, actual Replace and exact ACL restore; inert owned files only';originalFailureReproduced=$originalFailureReproduced;issuerSha256=(Get-YmccLightingFileHash $issuer);testSha256=(Get-YmccLightingFileHash $PSCommandPath);exactFullSddlAdmissionUnchanged=$true;hardwareIo=$false;installedApplicationMutated=$false;externalUserAclMutated=$false}
  [IO.File]::WriteAllText((Join-Path $EvidenceDir 'result.json'),($report|ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($false))
  Write-Output ('LIGHTING_PROMOTION_ACL_SELFTEST PASS '+$checks.Count)
 }
 return
}
if($PSCmdlet.ParameterSetName -ceq 'Shadow'){
 & {
  $currentFile=Join-Path $PSScriptRoot 'lighting-build-contract.ps1'
  $oldText=[IO.File]::ReadAllText($ShadowOldContractPath);$newText=[IO.File]::ReadAllText($currentFile)
  $oldLoop="foreach(`$source in `$innerSources){Need ((Get-YmccLightingFileHash (Resolve-YmccLightingChild `$ProjectRoot `$source.Name)) -ceq `$source.Value) 'INNER_SOURCE_CHANGED'}"
  $newLoop="foreach(`$innerSource in `$innerSources){Need ((Get-YmccLightingFileHash (Resolve-YmccLightingChild `$ProjectRoot `$innerSource.Name)) -ceq `$innerSource.Value) 'INNER_SOURCE_CHANGED'}"
  if(-not $oldText.Contains($oldLoop) -or $oldText.Replace($oldLoop,$newLoop) -cne $newText){throw 'SHADOW_REGRESSION_EXPECTS_ONLY_THE_REAL_LOOP_VARIABLE_FIX'}
  $candidate=Get-Content -LiteralPath $ShadowCandidateReceiptPath -Raw -Encoding UTF8|ConvertFrom-Json
  $failedBuildFile=Join-Path (Split-Path -Parent $ShadowCandidateReceiptPath) 'BUILD-RESULT.json'
  $failedBuild=Get-Content -LiteralPath $failedBuildFile -Raw -Encoding UTF8|ConvertFrom-Json
  if($failedBuild.status -cne 'FAIL' -or $failedBuild.appliedFiles -ne 0){throw 'SHADOW_REGRESSION_REQUIRES_THE_REAL_FAILED_UNPROMOTED_BUILD'}
  $signal=Get-Content -LiteralPath (Join-Path $WorkspaceRoot $candidate.signalResult.path) -Raw -Encoding UTF8|ConvertFrom-Json
  $oldPin=@($candidate.toolInputs|Where-Object path -CEQ 'tools/lighting-build-contract.ps1')[0].sha256
  if((Get-FileHash -LiteralPath $ShadowOldContractPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $oldPin -or (Get-FileHash -LiteralPath $ShadowMainSnapshotPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $signal.sourceHashes.'native/main.cpp'){throw 'SHADOW_REGRESSION_ORIGINAL_BYTES_NOT_VERIFIED'}
  $oldCopy=Join-Path $EvidenceDir 'original-issuer-contract.ps1';$mainCopy=Join-Path $EvidenceDir 'native-main-original.cpp';Copy-Item -LiteralPath $ShadowOldContractPath -Destination $oldCopy;Copy-Item -LiteralPath $ShadowMainSnapshotPath -Destination $mainCopy
  $script:shadowOverlay=@{}
  $script:shadowOverlay[[IO.Path]::GetFullPath((Join-Path $ProjectRoot 'tools/lighting-build-contract.ps1'))]=$oldCopy
  $script:shadowOverlay[[IO.Path]::GetFullPath((Join-Path $ProjectRoot 'native/main.cpp'))]=$mainCopy
  $cases=[Collections.Generic.List[object]]::new();$stagePayload=Join-Path (Split-Path -Parent $ShadowCandidateReceiptPath) 'payload'
  foreach($variant in @(@{name='old';text=$oldText},@{name='fixed';text=$newText})){
   . ([scriptblock]::Create($variant.text))
   $script:shadowOriginalResolver=(Get-Command Resolve-YmccLightingChild -CommandType Function).ScriptBlock
   function Resolve-YmccLightingChild([string]$Root,[string]$Relative){
    $actual=& $script:shadowOriginalResolver $Root $Relative
    if($script:shadowOverlay.ContainsKey($actual)){return $script:shadowOverlay[$actual]}
    return $actual
   }
   $answer=$null;$observedError=$null
   try{$answer=Assert-YmccLightingBuildCandidate -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot -CandidateReceiptPath $ShadowCandidateReceiptPath -PayloadRoot $stagePayload}catch{$observedError=$_.Exception.Message}
   if($variant.name -ceq 'old'){$ok=($null -ne $observedError -and $observedError -match 'pdb' -and $null -eq $answer)}else{$ok=($null -eq $observedError -and $answer.status -ceq 'PASS' -and $answer.runtimeFiles -eq 6)}
   $cases.Add([pscustomobject]@{name=$variant.name;passed=$ok;error=$observedError;answer=$answer})
   if(-not $ok){throw ('SHADOW_REGRESSION_VARIANT_FAILED '+$variant.name+' '+$observedError)}
  }
  . ([scriptblock]::Create($newText))
  $publicRefused=$false;$publicError=$null
  try{Assert-YmccLightingBuildCandidate -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot -CandidateReceiptPath $ShadowCandidateReceiptPath -PayloadRoot $stagePayload|Out-Null}catch{$publicError=$_.Exception.Message;$publicRefused=$publicError -match 'TOOL_SOURCE_CHANGED|INNER_SOURCE_CHANGED'}
  if(-not $publicRefused){throw 'SHADOW_REGRESSION_CANNOT_APPROVE_THE_FAILED_PREDECESSOR'}
  $cases.Add([pscustomobject]@{name='normal-admission-without-overlay-still-refuses-failed-predecessor';passed=$publicRefused;error=$publicError})
  $report=[ordered]@{schemaVersion=1;status='PASS';cases=@($cases);checks=3;scope='Exact old/new complete validator loop variable regression with two verified original filesystem inputs. Not a candidate approval.';productAssertionsChanged=$false;sourceDifference='Only foreach variable renamed to keep PDB proof object alive';oldContractSha256=$oldPin;newContractSha256=(Get-FileHash -LiteralPath $currentFile -Algorithm SHA256).Hash.ToLowerInvariant();originalMainSha256=$signal.sourceHashes.'native/main.cpp';overlayFiles=@(@{originalPath=(Join-Path $ProjectRoot 'tools/lighting-build-contract.ps1');actualPath=$oldCopy;sha256=$oldPin},@{originalPath=(Join-Path $ProjectRoot 'native/main.cpp');actualPath=$mainCopy;sha256=$signal.sourceHashes.'native/main.cpp'});failedCandidateApprovalGranted=$false;actualGCanonicalWritten=$false;actualCWritten=$false;hardwareIo=$false}
  [IO.File]::WriteAllText((Join-Path $EvidenceDir 'result.json'),($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
  Write-Output 'LIGHTING_VALIDATOR_SHADOW_REGRESSION PASS old-failed fixed-passed public-still-refused'
 }
 return
}
. (Join-Path $PSScriptRoot 'lighting-build-contract.ps1')
$marker=Join-Path $WorkspaceRoot 'Build/App/LightSetter/CANDIDATE-ACCEPTANCE.json'
$base=Get-Content -LiteralPath $marker -Raw -Encoding UTF8|ConvertFrom-Json
$checks=[Collections.Generic.List[object]]::new()
function Check([bool]$ok,[string]$name){$checks.Add([pscustomobject]@{name=$name;passed=$ok});if(-not $ok){throw ('CANDIDATE_REGRESSION_FAILED '+$name)}}
function Save([string]$path,$data){[IO.File]::WriteAllText($path,($data|ConvertTo-Json -Depth 60),[Text.UTF8Encoding]::new($false))}
function Hash([string]$path){Get-YmccLightingFileHash $path}
function Clone {return $base|ConvertTo-Json -Depth 60|ConvertFrom-Json}
$r=Assert-YmccLightingMainlinePayload -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot -PayloadRoot $PayloadRoot
Check ($r.status -eq 'PASS' -and $r.runtimeFiles -eq 6 -and $r.actualArtifactChecks -ge 177 -and $r.actualCatalogChecks -ge 549) 'current separately built candidate and six-file tuple admitted'
$packageFile=Join-Path $ProjectRoot 'tools/package-release.ps1';$errors=$null;$tokens=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($packageFile,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Current package source syntax failed'}
$extractions=@(foreach($name in @('Get-FullPath','Get-RelativePath','Test-IsExcludedPowerControlPath','Copy-PowerControlTemplates')){
 $nodes=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true));if($nodes.Count -ne 1){throw ('Package function missing '+$name)}
 . ([scriptblock]::Create($nodes[0].Extent.Text))
 [pscustomobject]@{name=$name;sourcePath='tools/package-release.ps1';sourceSha256=(Hash $packageFile);exactText=$nodes[0].Extent.Text}
})
$power=Join-Path $EvidenceDir 'copy-source';$copy=Join-Path $EvidenceDir 'package-stage'
New-Item -ItemType Directory -Path (Join-Path $power 'light-setter') -Force|Out-Null
foreach($file in Get-ChildItem -LiteralPath $PayloadRoot -File){Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $power ('light-setter/'+$file.Name))}
Copy-PowerControlTemplates $power $copy
$staged=Join-Path $copy 'light-setter'
$r=Assert-YmccLightingMainlinePayload -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot -PayloadRoot $staged
Check ($r.status -eq 'PASS') 'actual package template copier feeds current candidate gate'
Check (@(Get-ChildItem -LiteralPath $staged -File).Count -eq 6 -and -not(Test-Path -LiteralPath (Join-Path $staged 'YeManLightSetter.pdb'))) 'actual package copier ships exactly six runtime files and excludes PDB'
foreach($name in Get-YmccLightingRuntimeNames){Check ((Hash (Join-Path $staged $name)) -ceq @($base.runtimeFiles|Where-Object name -CEQ $name)[0].sha256) ('copied exact runtime '+$name)}
function Reject([string]$name,[scriptblock]$change,[string]$expected=''){
 $record=Clone;$path=Join-Path $EvidenceDir ($name+'/receipt.json');$payload=Join-Path $EvidenceDir ($name+'/payload');New-Item -ItemType Directory -Path $payload -Force|Out-Null
 foreach($file in Get-ChildItem -LiteralPath $PayloadRoot -File){Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $payload $file.Name)}
 & $change $record $payload
 Save $path $record
 $errorText=$null;try{Assert-YmccLightingBuildCandidate -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot -CandidateReceiptPath $path -PayloadRoot $payload|Out-Null}catch{$errorText=$_.Exception.Message}
 Check ($null -ne $errorText -and (-not $expected -or $errorText.Contains($expected))) ('reject '+$name)
}
Reject 'missing-core' {param($r,$p)Remove-Item -LiteralPath (Join-Path $p 'YeManLightDeviceCore.dll')} 'YeManLightDeviceCore.dll'
Reject 'wrong-core' {param($r,$p)[IO.File]::AppendAllText((Join-Path $p 'YeManLightDeviceCore.dll'),'x')} 'RUNTIME_BYTES'
Reject 'extra-runtime-dll' {param($r,$p)[IO.File]::WriteAllText((Join-Path $p 'Unreviewed.dll'),'extra')} 'UNEXPECTED_RUNTIME_FILE'
Reject 'missing-pdb-proof' {param($r,$p)$r.sourceProof.path='Build/lighting-missing-pdb-proof.json'} 'lighting-missing-pdb-proof.json'
Reject 'wrong-source' {param($r,$p)@($r.sourceInputs|Where-Object {'Compile' -cin @($_.roles)})[0].sha256='0'*64} 'SOURCE_CHANGED'
Reject 'wrong-global-receipt' {param($r,$p)$r.globalResult.sha256='0'*64} 'PROOF_BYTES'
Reject 'borrowed-fixed7bf-global' {param($r,$p)
 $source=Join-Path $ProjectRoot 'deps/lightsetter-admission/candidate7b-v1/proofs/candidate-global.json'
 $file=Join-Path $EvidenceDir 'old7bf-global.json';Copy-Item -LiteralPath $source -Destination $file
 $r.globalResult.path=$file.Substring(([IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')).Length+1).Replace('\','/');$r.globalResult.sha256=Hash $file
} 'ACTUAL_GLOBAL_EXECUTION'
Reject 'changed-inner-source-record' {param($r,$p)
 $inner=Get-Content -LiteralPath (Join-Path $WorkspaceRoot $r.signalResult.path) -Raw -Encoding UTF8|ConvertFrom-Json
 $inner.sourceHashes.'native/main.cpp'='0'*64;$innerFile=Join-Path $EvidenceDir 'changed-inner-source.json';Save $innerFile $inner
 $oldInnerPath=[IO.Path]::GetFullPath((Join-Path $WorkspaceRoot $r.signalResult.path));$r.signalResult.path=$innerFile.Substring(([IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')).Length+1).Replace('\','/');$r.signalResult.sha256=Hash $innerFile
 $outer=Get-Content -LiteralPath (Join-Path $WorkspaceRoot $r.globalResult.path) -Raw -Encoding UTF8|ConvertFrom-Json
 foreach($link in $outer.results[0].structuredEvidence){if([IO.Path]::GetFullPath($link.path) -ieq $oldInnerPath){$link.path=$innerFile;$link.sha256=$r.signalResult.sha256}}
 $outerFile=Join-Path $EvidenceDir 'changed-inner-source-global.json';Save $outerFile $outer
 $r.globalResult.path=$outerFile.Substring(([IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')).Length+1).Replace('\','/');$r.globalResult.sha256=Hash $outerFile
} 'INNER_SOURCE_CHANGED'
Reject 'retagged-new-helper' {param($r,$p)$r.helperSha256='0'*64} 'PDB_SOURCE_PROOF'
Reject 'missing-runtime-identity' {param($r,$p)$r.runtimeFiles=@($r.runtimeFiles|Where-Object name -CNE 'YeManLightDeviceCore.dll')} 'RUNTIME_SET'
Reject 'wrong-candidate-contract' {param($r,$p)$r.kind='lighting-catalog-v3'} 'RECEIPT_CONTRACT'
Reject 'source-proof-pdb-file-missing' {param($r,$p)
 $proof=Get-Content -LiteralPath (Join-Path $WorkspaceRoot $r.sourceProof.path) -Raw -Encoding UTF8|ConvertFrom-Json
 $proof.pdb.path='Build/lighting-missing.pdb';$file=Join-Path $EvidenceDir 'missing-pdb-source-proof.json';Save $file $proof
 $r.sourceProof.path=$file.Substring(([IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')).Length+1).Replace('\','/');$r.sourceProof.sha256=Hash $file
} 'lighting-missing.pdb'
$r=Assert-YmccLightingMainlinePayload -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot -PayloadRoot $PayloadRoot
Check ($r.status -eq 'PASS') 'negative cases leave approved candidate and payload unchanged'
$report=[ordered]@{schemaVersion=1;status='PASS';passed=$true;checks=$checks.Count;cases=@($checks);sourceHashes=@([pscustomobject]@{path='tools/lighting-build-candidate-selftest.ps1';sha256=(Hash $PSCommandPath)},[pscustomobject]@{path='tools/lighting-build-contract.ps1';sha256=(Hash (Join-Path $PSScriptRoot 'lighting-build-contract.ps1'))},[pscustomobject]@{path='tools/package-release.ps1';sha256=(Hash $packageFile)});candidateReceiptSha256=(Hash $marker);helperSha256=$base.helperSha256;coreSha256=$base.coreSha256;sourceInputs=@($base.sourceInputs).Count;actualCopiedFiles=@(Get-ChildItem -LiteralPath $staged -File|ForEach-Object {[pscustomobject]@{name=$_.Name;sha256=(Hash $_.FullName)}});productionExtractions=$extractions;actualGCanonicalWritten=$false;actualCWritten=$false;fullPackageExecuted=$false;hardwareIo=$false}
Save (Join-Path $EvidenceDir 'result.json') $report
Write-Output ('LIGHTING_BUILD_CANDIDATE_SELFTEST PASS '+$checks.Count)