# Normal lighting builds use current sources and separate exact candidate evidence.
function Get-YmccLightingRuntimeNames { @('YeManLightSetter.exe','YeManLightSetter.dll','YeManLightSetter.deps.json','YeManLightSetter.runtimeconfig.json','YeManLightSetter.json','YeManLightDeviceCore.dll') }
function Get-YmccLightingFileHash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Resolve-YmccLightingChild([string]$Root,[string]$Relative){
 $base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
 if(-not $Relative -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)'){throw 'LIGHTING_BUILD_UNSAFE_PATH'}
 $path=[IO.Path]::GetFullPath((Join-Path $base $Relative));if(-not $path.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LIGHTING_BUILD_UNSAFE_PATH'}
 $cursor=$path;while($cursor.Length -ge $base.Length){if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'LIGHTING_BUILD_REPARSE'}};if($cursor -ieq $base){break};$cursor=Split-Path -Parent $cursor}
 return $path
}
function Get-YmccLightingBuildSourceIdentity {
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)]$Evaluation,[string]$ProjectRelative='FanLab/LightSetter/YeManLightSetter.csproj',[string]$RolePrefix='')
 $root=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\');$map=@{}
 function AddInput([string]$Path,[string]$Role){
  $full=[IO.Path]::GetFullPath($Path)
  if(-not $full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LIGHTING_BUILD_SOURCE_OUTSIDE_PROJECT'}
  $rel=$full.Substring($root.Length+1).Replace('\','/');$file=Resolve-YmccLightingChild $root $rel
  if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw 'LIGHTING_BUILD_SOURCE_MISSING'}
  if(-not $map.ContainsKey($rel)){$map[$rel]=[pscustomobject]@{path=$rel;sha256=(Get-YmccLightingFileHash $file);bytes=(Get-Item -LiteralPath $file).Length;roles=@()}}
  $map[$rel].roles+=@($Role)
 }
 if(@($Evaluation.Items.ProjectReference).Count){throw 'LIGHTING_BUILD_UNREVIEWED_PROJECT_REFERENCE'}
 AddInput (Join-Path $root $ProjectRelative) ($RolePrefix+'project')
 foreach($kind in @('Compile','Content')){foreach($item in @($Evaluation.Items.$kind)){AddInput $item.FullPath ($RolePrefix+$kind)}}
 foreach($kind in @('DirectoryBuildPropsPath','DirectoryBuildTargetsPath')){if($Evaluation.Properties.$kind){AddInput $Evaluation.Properties.$kind ($RolePrefix+$kind)}}
 if(@($map.Values|Where-Object {($RolePrefix+'Compile') -cin @($_.roles)}).Count -lt 1){throw 'LIGHTING_BUILD_COMPILE_SET_EMPTY'}
 return @($map.Values|Sort-Object path)
}
function Assert-YmccLightingBuildSources {
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)]$Inputs)
 if(@($Inputs).Count -lt 3 -or @($Inputs|Group-Object path|Where-Object Count -NE 1).Count){throw 'LIGHTING_BUILD_SOURCE_SET_INVALID'}
 foreach($row in $Inputs){$file=Resolve-YmccLightingChild $ProjectRoot $row.path;if(-not(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-YmccLightingFileHash $file) -cne $row.sha256){throw ('LIGHTING_BUILD_SOURCE_CHANGED: '+$row.path)}}
}
function Assert-YmccLightingBuildCandidate {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$WorkspaceRoot,[Parameter(Mandatory=$true)][string]$CandidateReceiptPath,[Parameter(Mandatory=$true)][string]$PayloadRoot)
 $ErrorActionPreference='Stop';Set-StrictMode -Version Latest
 function Need([bool]$ok,[string]$code){if(-not $ok){throw ('LIGHTING_BUILD_'+$code)}}
 $workspace=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\');$receipt=[IO.Path]::GetFullPath($CandidateReceiptPath)
 Need ($receipt.StartsWith((Join-Path $workspace 'Build')+'\',[StringComparison]::OrdinalIgnoreCase)) 'RECEIPT_OUTSIDE_BUILD'
 Need (Test-Path -LiteralPath $receipt -PathType Leaf) 'RECEIPT_MISSING'
 $record=Get-Content -LiteralPath $receipt -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($record.schemaVersion -eq 1 -and $record.kind -ceq 'ymcc-lighting-build-candidate-v1' -and $record.status -ceq 'PASS' -and $record.hardwareIo -eq $false -and $record.helperMainInvoked -eq $false) 'RECEIPT_CONTRACT'
 Assert-YmccLightingBuildSources -ProjectRoot $ProjectRoot -Inputs $record.sourceInputs
 Need (@($record.toolInputs).Count -eq 3) 'TOOL_SET'
 foreach($tool in $record.toolInputs){Need ((Get-YmccLightingFileHash (Resolve-YmccLightingChild $ProjectRoot $tool.path)) -ceq $tool.sha256) 'TOOL_SOURCE_CHANGED'}
 function ReadProof($Reference){$file=Resolve-YmccLightingChild $workspace $Reference.path;Need ((Get-YmccLightingFileHash $file) -ceq $Reference.sha256) 'PROOF_BYTES';return Get-Content -LiteralPath $file -Raw -Encoding UTF8|ConvertFrom-Json}
 $evaluation=ReadProof $record.evaluation;$observed=@(Get-YmccLightingBuildSourceIdentity -ProjectRoot $ProjectRoot -Evaluation $evaluation)
 $coreEvaluation=ReadProof $record.coreEvaluation;$observed+=@(Get-YmccLightingBuildSourceIdentity -ProjectRoot $ProjectRoot -Evaluation $coreEvaluation -ProjectRelative 'FanLab/LightSetter/DeviceCore/YeManLightDeviceCore.csproj' -RolePrefix 'Core')
 $observed=@($observed|Sort-Object path)
 Need (($observed|ConvertTo-Json -Depth 8 -Compress) -ceq ($record.sourceInputs|ConvertTo-Json -Depth 8 -Compress)) 'EVALUATED_SOURCE_BINDING'
 $source=ReadProof $record.sourceProof
 Need ($source.status -ceq 'PASS' -and $source.pairVerified -eq $true -and $source.helperSha256 -ceq $record.helperSha256 -and $source.compileSourceInputs -eq @($record.sourceInputs|Where-Object {'Compile' -cin @($_.roles)}).Count) 'PDB_SOURCE_PROOF'
 foreach($document in @($source.documents)){
  $base=if($document.generated){$workspace}else{$ProjectRoot};$file=Resolve-YmccLightingChild $base $document.path
  Need ((Get-YmccLightingFileHash $file) -ceq $document.sha256) 'PDB_SOURCE_BYTES'
 }
 $pdb=Resolve-YmccLightingChild $workspace $source.pdb.path;Need ((Get-YmccLightingFileHash $pdb) -ceq $source.pdb.sha256) 'PDB_BYTES'
 $global=ReadProof $record.globalResult;$rows=@($global.results)
 Need ($global.verdict.status -ceq 'PASS_SELECTED_SCOPE' -and $global.verdict.exitCode -eq 0 -and $rows.Count -eq 1 -and $rows[0].id -ceq 'lighting.signal-consumer' -and $rows[0].status -ceq 'PASS' -and $rows[0].exitCode -eq 0 -and $rows[0].sourceChanged -eq $false -and $rows[0].candidateArtifactChanged -eq $false -and $rows[0].requiredEvidenceMissing -eq $false -and $rows[0].requiredEvidenceFailed -eq $false -and $rows[0].timedOut -eq $false -and $global.candidateInputs.expectedHelperSha256 -ceq $record.helperSha256) 'ACTUAL_GLOBAL_EXECUTION'
 foreach($row in $rows[0].sourceInputs){Need ($row.status -ceq 'OK' -and (Get-YmccLightingFileHash (Resolve-YmccLightingChild $ProjectRoot $row.path)) -ceq $row.sha256) 'CONSUMER_SOURCE_CHANGED'}
 $toolManifestRelative='tools/ymcc_sim/global/lighting-signals/tool-inputs.json'
 $toolSourceRows=@($rows[0].sourceInputs|Where-Object path -CEQ $toolManifestRelative)
 Need ($toolSourceRows.Count -eq 1 -and $toolSourceRows[0].status -ceq 'OK') 'CONSUMER_TOOL_SOURCE_REQUIRED'
 $toolManifest=Get-Content -LiteralPath (Resolve-YmccLightingChild $ProjectRoot $toolManifestRelative) -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($toolManifest.schemaVersion -eq 1 -and @($toolManifest.files).Count -gt 0 -and @($toolManifest.files).Count -le 3072) 'CONSUMER_TOOL_SET'
 $externalToolPins=@{}
 foreach($tool in @($toolManifest.files)){
  if($tool.PSObject.Properties['path']){
   $toolPath=[IO.Path]::GetFullPath($tool.path)
   Need ([IO.Path]::IsPathRooted($tool.path) -and $tool.sha256 -cmatch '^[a-f0-9]{64}$' -and -not $externalToolPins.ContainsKey($toolPath)) 'CONSUMER_TOOL_PATH'
   $externalToolPins[$toolPath]=$tool.sha256
  }
 }
 foreach($artifact in @($rows[0].candidateArtifacts)){
  $full=[IO.Path]::GetFullPath($artifact.path);$base=if($full.StartsWith(([IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\'))+'\',[StringComparison]::OrdinalIgnoreCase)){[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')}else{$workspace}
  if($full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){$file=Resolve-YmccLightingChild $base $full.Substring($base.Length+1)}
  else{
   Need ($externalToolPins.ContainsKey($full) -and $externalToolPins[$full] -ceq $artifact.sha256) 'CONSUMER_ARTIFACT_OUTSIDE_SOURCE_BUILD'
   $file=Resolve-YmccLightingChild (Split-Path -Parent $full) (Split-Path -Leaf $full)
  }
  Need ($artifact.status -ceq 'OK' -and (Get-YmccLightingFileHash $file) -ceq $artifact.sha256) 'CONSUMER_ARTIFACT_CHANGED'
 }
 $signalFile=Resolve-YmccLightingChild $workspace $record.signalResult.path
 $links=@($rows[0].structuredEvidence|Where-Object {([IO.Path]::GetFullPath($_.path)) -ieq $signalFile -and $_.sha256 -ceq $record.signalResult.sha256 -and $_.failed -eq $false})
 Need ($links.Count -eq 1) 'GLOBAL_INNER_RECEIPT_BINDING'
 $signal=ReadProof $record.signalResult;$actual=$signal.compatibility.payloadCandidate
 $innerSources=@($signal.sourceHashes.PSObject.Properties)
 Need ($innerSources.Count -gt 0) 'INNER_SOURCE_SET'
 foreach($innerSource in $innerSources){Need ((Get-YmccLightingFileHash (Resolve-YmccLightingChild $ProjectRoot $innerSource.Name)) -ceq $innerSource.Value) 'INNER_SOURCE_CHANGED'}
 Need ($signal.status -ceq 'PASS' -and $signal.hardwareIo -eq $false -and $actual.actual -ceq $record.helperSha256 -and $actual.expectedReviewedSha256 -ceq $record.helperSha256 -and $actual.behavior.passed -eq $true -and $actual.behavior.actualSha256 -ceq $record.helperSha256 -and $actual.behavior.checksPassed -ge 177 -and $actual.behavior.mainEntryInvoked -eq $false -and @($actual.behavior.nativeAttempts).Count -eq 0 -and @($actual.behavior.dependencyAttempts).Count -eq 0) 'EXACT_ARTIFACT_CONSUMER'
 Need ($actual.catalogBehavior.status -ceq 'PASS' -and $actual.catalogBehavior.artifactMode -eq $true -and $actual.catalogBehavior.assemblySha256 -ceq $record.helperSha256 -and $actual.catalogBehavior.productionEntryPointInvoked -eq $false -and $actual.catalogBehavior.HCAssemblyLoaded -eq $false -and $actual.catalogBehavior.checks -ge 549 -and @($actual.catalogBehavior.failures).Count -eq 0) 'EXACT_CATALOG_CONSUMER'
 foreach($contract in @('boundHidLightingContract','onexRequestCacheContract','onexLightingContract','msiLightingContract')){Need ($signal.$contract.passed -eq $true) 'TRANSPORT_CONSUMER'}
 $names=@(Get-YmccLightingRuntimeNames);Need (@($record.runtimeFiles).Count -eq 6 -and @($record.runtimeFiles|Group-Object name|Where-Object Count -NE 1).Count -eq 0) 'RUNTIME_SET'
 $payload=[IO.Path]::GetFullPath($PayloadRoot).TrimEnd('\');$physical=@(Get-ChildItem -LiteralPath $payload -Recurse -File|ForEach-Object {$_.FullName.Substring($payload.Length+1).Replace('\','/')})
 Need (@($physical|Where-Object {$_ -cnotin ($names+@('YeManLightSetter.pdb'))}).Count -eq 0) 'UNEXPECTED_RUNTIME_FILE'
 foreach($name in $names){$files=@($record.runtimeFiles|Where-Object name -CEQ $name);Need ($files.Count -eq 1) 'RUNTIME_BINDING';$file=Resolve-YmccLightingChild $payload $name;Need ((Get-YmccLightingFileHash $file) -ceq $files[0].sha256) 'RUNTIME_BYTES';$built=Resolve-YmccLightingChild $workspace $files[0].path;Need ((Get-YmccLightingFileHash $built) -ceq $files[0].sha256) 'BUILT_RUNTIME_BYTES'}
 Need (@($record.runtimeFiles|Where-Object {$_.name -ceq 'YeManLightSetter.dll' -and $_.sha256 -ceq $record.helperSha256}).Count -eq 1 -and @($record.runtimeFiles|Where-Object {$_.name -ceq 'YeManLightDeviceCore.dll' -and $_.sha256 -ceq $record.coreSha256}).Count -eq 1) 'HELPER_CORE_BINDING'
 if('YeManLightSetter.pdb' -cin $physical){Need ((Get-YmccLightingFileHash (Join-Path $payload 'YeManLightSetter.pdb')) -ceq $source.pdb.sha256) 'PAYLOAD_PDB_BYTES'}
 return [pscustomobject]@{status='PASS';kind=$record.kind;receiptSha256=(Get-YmccLightingFileHash $receipt);helperSha256=$record.helperSha256;coreSha256=$record.coreSha256;runtimeFiles=6;sourceInputs=@($record.sourceInputs).Count;actualArtifactChecks=$actual.behavior.checksPassed;actualCatalogChecks=$actual.catalogBehavior.checks;hardwareAcceptance=$false}
}
function Assert-YmccLightingCompiledTestPayload {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$WorkspaceRoot,[Parameter(Mandatory=$true)][string]$CandidateReceiptPath,[Parameter(Mandatory=$true)][string]$PayloadRoot)
 $workspace=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')
 $receipt=[IO.Path]::GetFullPath($CandidateReceiptPath)
 if(-not $receipt.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LIGHTING_TEST_RECEIPT_SCOPE'}
 $record=Get-Content -LiteralPath $receipt -Raw -Encoding UTF8|ConvertFrom-Json
 if($record.kind -cne 'ymcc-lighting-compiled-test-v1' -or $record.status -cne 'COMPILED_TEST_ONLY' -or $record.fixturesExecuted -ne $false){throw 'LIGHTING_TEST_COMPILE_RECEIPT_REQUIRED'}
 Assert-YmccLightingBuildSources -ProjectRoot $ProjectRoot -Inputs @($record.sourceInputs)
 foreach($tool in @($record.toolInputs)){if((Get-YmccLightingFileHash (Join-Path $ProjectRoot $tool.path)) -cne $tool.sha256){throw 'LIGHTING_TEST_TOOL_CHANGED'}}
 $names=@(Get-YmccLightingRuntimeNames)
 if(@($record.runtimeFiles).Count -ne $names.Count -or @(Compare-Object @($names|Sort-Object) @($record.runtimeFiles.name|Sort-Object)).Count){throw 'LIGHTING_TEST_PAYLOAD_SET'}
 foreach($item in @($record.runtimeFiles)){$file=Resolve-YmccLightingChild $PayloadRoot $item.name;if((Get-Item -LiteralPath $file).Length -ne [long]$item.bytes -or (Get-YmccLightingFileHash $file) -cne $item.sha256){throw ('LIGHTING_TEST_PAYLOAD_CHANGED: '+$item.name)}}
 $proof=Resolve-YmccLightingChild $workspace $record.sourceProof.path
 if((Get-YmccLightingFileHash $proof) -cne $record.sourceProof.sha256){throw 'LIGHTING_TEST_SOURCE_PROOF_CHANGED'}
 return [pscustomobject]@{status='COMPILED_TEST_ONLY';fixturesExecuted=$false;runtimeFiles=$names.Count;sourceInputs=@($record.sourceInputs).Count}
}
function Assert-YmccLightingMainlinePayload {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$WorkspaceRoot,[Parameter(Mandatory=$true)][string]$PayloadRoot)
 $receipt=Resolve-YmccLightingChild $WorkspaceRoot 'Build/App/LightSetter/CANDIDATE-ACCEPTANCE.json'
 if($env:YEMAN_TEST_EXPORT -eq '1'){
  Assert-YmccLightingCompiledTestPayload -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot -CandidateReceiptPath $receipt -PayloadRoot $PayloadRoot
 }else{
  Assert-YmccLightingBuildCandidate -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot -CandidateReceiptPath $receipt -PayloadRoot $PayloadRoot
 }
}