function Write-YmccLightingCandidateSourceProof {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$WorkspaceRoot,[Parameter(Mandatory=$true)][string]$HelperPath,[Parameter(Mandatory=$true)][string]$PdbPath,[Parameter(Mandatory=$true)]$SourceInputs,[Parameter(Mandatory=$true)][string]$OutputPath)
 $ErrorActionPreference='Stop';$project=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\');$workspace=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')
 $stream=$null;$debug=$null;$pe=$null;$provider=$null
 try{
  $stream=[IO.File]::OpenRead($HelperPath);$debug=[IO.File]::OpenRead($PdbPath)
  $pe=[Reflection.PortableExecutable.PEReader]::new($stream);$entries=@($pe.ReadDebugDirectory()|Where-Object Type -EQ CodeView);if($entries.Count -ne 1){throw 'LIGHTING_CANDIDATE_CODEVIEW'}
  $cv=$pe.ReadCodeViewDebugDirectoryData($entries[0]);$provider=[Reflection.Metadata.MetadataReaderProvider]::FromPortablePdbStream($debug);$reader=$provider.GetMetadataReader();$id=[byte[]]$reader.DebugMetadataHeader.Id
  if([Guid]::new([byte[]]$id[0..15]) -ne $cv.Guid -or [BitConverter]::ToUInt32($id,16) -ne $entries[0].Stamp){throw 'LIGHTING_CANDIDATE_PDB_PAIR'}
  $compile=@($SourceInputs|Where-Object {'Compile' -cin @($_.roles)})
  $docs=@(foreach($handle in $reader.Documents){
   $doc=$reader.GetDocument($handle);$full=[IO.Path]::GetFullPath($reader.GetString($doc.Name));$sum=([BitConverter]::ToString($reader.GetBlobBytes($doc.Hash))).Replace('-','').ToLowerInvariant()
   if($reader.GetGuid($doc.HashAlgorithm).ToString() -cne '8829d00f-11b8-4213-878b-770e8597ac16' -or (Get-YmccLightingFileHash $full) -cne $sum){throw 'LIGHTING_CANDIDATE_SOURCE_BYTES'}
   $generated=$false;$relative=$null
   if($full.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)){$relative=$full.Substring($project.Length+1).Replace('\','/');if(@($compile|Where-Object {$_.path -ceq $relative -and $_.sha256 -ceq $sum}).Count -ne 1){throw 'LIGHTING_CANDIDATE_UNDECLARED_SOURCE'}}
   elseif($full.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase) -and $full -match '[\\/]obj[\\/]' -and [IO.Path]::GetFileName($full) -match '(AssemblyInfo\.cs|GlobalUsings\.g\.cs|AssemblyAttributes\.cs)$'){$relative=$full.Substring($workspace.Length+1).Replace('\','/');$generated=$true}
   else{throw 'LIGHTING_CANDIDATE_SOURCE_OUTSIDE_BUILD'}
   [pscustomobject]@{path=$relative;sha256=$sum;generated=$generated}
  })
  if(@($docs|Where-Object generated -EQ $false).Count -ne $compile.Count -or (Compare-Object @($compile.path|Sort-Object) @($docs|Where-Object generated -EQ $false|ForEach-Object path|Sort-Object))){throw 'LIGHTING_CANDIDATE_COMPILE_SET'}
  $records=@(foreach($handle in $reader.CustomDebugInformation){$record=$reader.GetCustomDebugInformation($handle);$kind=$reader.GetGuid($record.Kind).ToString();if($kind -in @('b5feec05-8cd0-4a83-96da-466284bb4bd8','7e4d4708-096e-4c5c-aeda-cb10ba6a740d')){[pscustomobject]@{kind=$kind;bytesBase64=[Convert]::ToBase64String($reader.GetBlobBytes($record.Value))}}})
  if($records.Count -ne 2){throw 'LIGHTING_CANDIDATE_COMPILATION_RECORDS'}
  $pdbFull=[IO.Path]::GetFullPath($PdbPath);if(-not $pdbFull.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LIGHTING_CANDIDATE_PDB_OUTSIDE_BUILD'}
  $result=[ordered]@{schemaVersion=1;status='PASS';helperSha256=(Get-YmccLightingFileHash $HelperPath);pairVerified=$true;pdb=@{path=$pdbFull.Substring($workspace.Length+1).Replace('\','/');sha256=(Get-YmccLightingFileHash $PdbPath);guid=$cv.Guid.ToString();stamp=$entries[0].Stamp};compileSourceInputs=$compile.Count;generatedDocuments=@($docs|Where-Object generated -EQ $true).Count;documents=$docs;compilationRecords=$records;runtimeAssemblyLoaded=$false}
  [IO.File]::WriteAllText($OutputPath,($result|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false));return [pscustomobject]$result
 }finally{if($null -ne $provider){$provider.Dispose()};if($null -ne $pe){$pe.Dispose()};if($null -ne $stream){$stream.Dispose()};if($null -ne $debug){$debug.Dispose()}}
}
# Read PE/PDB metadata only. No product assembly is loaded and no process is started.
function Get-YmccLightingCoreStaticMetadata {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$Path)
 $stream=$null;$pe=$null;$provider=$null
 try{
  $stream=[IO.File]::OpenRead($Path);$pe=[Reflection.PortableExecutable.PEReader]::new($stream)
  $metadata=[Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($pe)
  if(-not $metadata.IsAssembly -or $metadata.GetString($metadata.GetAssemblyDefinition().Name) -cne 'YeManLightDeviceCore'){throw 'LIGHTING_PREPARED_CORE_ASSEMBLY'}
  $embedded=@($pe.ReadDebugDirectory()|Where-Object Type -EQ EmbeddedPortablePdb);$codeview=@($pe.ReadDebugDirectory()|Where-Object Type -EQ CodeView)
  if($embedded.Count -ne 1 -or $codeview.Count -ne 1){throw 'LIGHTING_PREPARED_CORE_EMBEDDED_PDB'}
  $provider=$pe.ReadEmbeddedPortablePdbDebugDirectoryData($embedded[0]);$reader=$provider.GetMetadataReader();$cv=$pe.ReadCodeViewDebugDirectoryData($codeview[0]);$pdbId=[byte[]]$reader.DebugMetadataHeader.Id
  if([Guid]::new([byte[]]$pdbId[0..15]) -ne $cv.Guid -or [BitConverter]::ToUInt32($pdbId,16) -ne $codeview[0].Stamp){throw 'LIGHTING_PREPARED_CORE_PDB_PAIR'}
  $documents=@(foreach($h in $reader.Documents){$d=$reader.GetDocument($h);if($reader.GetGuid($d.HashAlgorithm).ToString() -cne '8829d00f-11b8-4213-878b-770e8597ac16'){throw 'LIGHTING_PREPARED_CORE_PDB_HASH_ALGORITHM'};[pscustomobject]@{pdbName=$reader.GetString($d.Name);sha256=([BitConverter]::ToString($reader.GetBlobBytes($d.Hash))).Replace('-','').ToLowerInvariant()}})
  $records=@{};foreach($h in $reader.CustomDebugInformation){$d=$reader.GetCustomDebugInformation($h);$kind=$reader.GetGuid($d.Kind).ToString();if($kind -in @('b5feec05-8cd0-4a83-96da-466284bb4bd8','7e4d4708-096e-4c5c-aeda-cb10ba6a740d')){if($records.ContainsKey($kind)){throw 'LIGHTING_PREPARED_CORE_DUPLICATE_COMPILATION_RECORD'};$records[$kind]=$reader.GetBlobBytes($d.Value)}}
  if($records.Count -ne 2){throw 'LIGHTING_PREPARED_CORE_COMPILATION_RECORDS'}
  $parts=[Text.Encoding]::UTF8.GetString($records['b5feec05-8cd0-4a83-96da-466284bb4bd8']).Split([char]0);$options=[ordered]@{}
  if($parts.Count%2 -ne 1 -or $parts[-1] -cne ''){throw 'LIGHTING_PREPARED_CORE_COMPILATION_OPTIONS'}
  for($i=0;$i -lt $parts.Count-1;$i+=2){if($options.Contains($parts[$i])){throw 'LIGHTING_PREPARED_CORE_DUPLICATE_OPTION'};$options[$parts[$i]]=$parts[$i+1]}
  if($options['optimization'] -cne 'release' -or $options['output-kind'] -cne 'DynamicallyLinkedLibrary' -or $options['platform'] -cne 'AnyCpu' -or $options['language'] -cne 'C#' -or $options['define'] -match '(?i)TEST|MOCK|FIXTURE|DEBUG'){throw 'LIGHTING_PREPARED_CORE_NONPRODUCTION_OPTIONS'}
  $blob=[byte[]]$records['7e4d4708-096e-4c5c-aeda-cb10ba6a740d'];$position=0;$references=[Collections.Generic.List[object]]::new()
  while($position -lt $blob.Length){
   $strings=@();for($i=0;$i -lt 2;$i++){$start=$position;while($position -lt $blob.Length -and $blob[$position] -ne 0){$position++};if($position -ge $blob.Length){throw 'LIGHTING_PREPARED_CORE_REFERENCE_RECORD'};$strings+=([Text.UTF8Encoding]::new($false,$true).GetString($blob,$start,$position-$start));$position++}
   if($position+25 -gt $blob.Length){throw 'LIGHTING_PREPARED_CORE_REFERENCE_RECORD'}
   $flags=$blob[$position];$position++;$stamp=[BitConverter]::ToUInt32($blob,$position);$position+=4;$size=[BitConverter]::ToUInt32($blob,$position);$position+=4;$mvid=[Guid]::new([byte[]]$blob[$position..($position+15)]);$position+=16
   if([IO.Path]::GetFileName($strings[0]) -cne $strings[0] -or $strings[0] -notmatch '\.dll$' -or $strings[1] -cne '' -or $flags -ne 1){throw 'LIGHTING_PREPARED_CORE_REFERENCE_KIND'}
   $references.Add([pscustomobject]@{name=$strings[0];aliases=$strings[1];flags=$flags;timestamp=$stamp;imageSize=$size;mvid=$mvid.ToString()})
  }
  if(-not $references.Count -or @($references|Group-Object name|Where-Object Count -NE 1).Count){throw 'LIGHTING_PREPARED_CORE_REFERENCE_SET'}
  [pscustomobject]@{documents=$documents;options=$options;references=@($references.ToArray());pdbGuid=$cv.Guid.ToString();pdbStamp=$codeview[0].Stamp;moduleMvid=$metadata.GetGuid($metadata.GetModuleDefinition().Mvid).ToString();runtimeAssemblyLoaded=$false}
 }finally{if($null -ne $provider){$provider.Dispose()};if($null -ne $pe){$pe.Dispose()};if($null -ne $stream){$stream.Dispose()}}
}
function Get-YmccLightingCoreReferenceMetadata {
 param([Parameter(Mandatory=$true)][string]$Path)
 $s=[IO.File]::OpenRead($Path);$pe=$null
 try{$pe=[Reflection.PortableExecutable.PEReader]::new($s);$m=[Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($pe);[pscustomobject]@{name=[IO.Path]::GetFileName($Path);timestamp=[BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$pe.PEHeaders.CoffHeader.TimeDateStamp),0);imageSize=[uint32]$pe.PEHeaders.PEHeader.SizeOfImage;mvid=$m.GetGuid($m.GetModuleDefinition().Mvid).ToString()}}finally{if($null -ne $pe){$pe.Dispose()};$s.Dispose()}
}
function Assert-YmccLightingPreparedCore {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$EvidencePath,[Parameter(Mandatory=$true)]$CoreSourceInputs,[Parameter(Mandatory=$true)][string]$ExpectedSha256,[Parameter(Mandatory=$true)][string]$DotnetPath,[Parameter(Mandatory=$true)][string]$EvaluatedSdkVersion)
 $ErrorActionPreference='Stop';$project=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
 function Need($ok,[string]$code){if(-not $ok){throw ('LIGHTING_PREPARED_CORE_'+$code)}}
 function FileIdentity($reference){
  Need ($reference.path -is [string] -and [IO.Path]::IsPathRooted($reference.path) -and $reference.sha256 -match '^[a-f0-9]{64}$') 'FILE_REFERENCE'
  $file=[IO.Path]::GetFullPath($reference.path);Need (Test-Path -LiteralPath $file -PathType Leaf) 'FILE_MISSING'
  for($cursor=$file;$cursor;$cursor=Split-Path -Parent $cursor){Need (-not ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) 'REPARSE_POINT'}
  Need ((Get-Item -LiteralPath $file).Length -eq [long]$reference.bytes -and (Get-YmccLightingFileHash $file) -ceq $reference.sha256) 'FILE_IDENTITY';return $file
 }
 function Same($a,$b){return (($a|ConvertTo-Json -Depth 50 -Compress) -ceq ($b|ConvertTo-Json -Depth 50 -Compress))}
 $evidenceFile=[IO.Path]::GetFullPath($EvidencePath);$evidenceHash=Get-YmccLightingFileHash $evidenceFile
 $e=Get-Content -LiteralPath $evidenceFile -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($e.schemaVersion -eq 1 -and $e.kind -ceq 'ymcc-lighting-prepared-core-evidence-v1' -and $e.productionMainInvoked -eq $false) 'EVIDENCE_SCHEMA'
 $sourceFile=FileIdentity $e.sourceIdentity;$source=Get-Content -LiteralPath $sourceFile -Raw -Encoding UTF8|ConvertFrom-Json
 $publishFile=FileIdentity $e.publishReceipt;$publish=Get-Content -LiteralPath $publishFile -Raw -Encoding UTF8|ConvertFrom-Json
 $auditFile=FileIdentity $e.embeddedPdbAudit;$audit=Get-Content -LiteralPath $auditFile -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($source.buildReceipt.sha256 -ceq $e.publishReceipt.sha256 -and $source.buildReceipt.bytes -eq $e.publishReceipt.bytes -and [IO.Path]::GetFullPath($source.buildReceipt.path) -ieq $publishFile) 'ORIGINAL_PUBLISH_BINDING'
 Need ($publish.kind -ceq 'source-bound-production-independent-lighting-core-pin' -and $publish.exitCode -eq 0 -and $publish.productMainInvoked -eq $false -and $publish.hardwareIo -eq $false -and $publish.privateFixtureDllAdopted -eq $false -and $source.sourceEntryNotInvoked -eq $true -and $source.sourceTransportHasNoTestReplacement -eq $true) 'ORIGINAL_PRODUCTION_EXIT'
 $coreFile=FileIdentity $e.core;Need ($e.core.sha256 -ceq $ExpectedSha256 -and $source.productOutput.sha256 -ceq $ExpectedSha256 -and $source.productOutput.bytes -eq $e.core.bytes -and [IO.Path]::GetFullPath($source.productOutput.path) -ieq $coreFile) 'CORE_PIN'
 $policy=Get-Content -LiteralPath (Join-Path $project 'capabilities/hc-lighting-policy.json') -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($policy.independentCore.sha256 -ceq $ExpectedSha256) 'POLICY_PIN'
 $projectRelative='FanLab/LightSetter/DeviceCore/YeManLightDeviceCore.csproj';$oldProject=[IO.Path]::GetFullPath($source.productProject.path);Need ($oldProject.Replace('\','/').EndsWith('/'+$projectRelative,[StringComparison]::Ordinal)) 'PRODUCTION_PROJECT_PATH'
 $originalRoot=$oldProject.Substring(0,$oldProject.Length-$projectRelative.Length).TrimEnd('\','/');$originalCoreRoot=Split-Path -Parent $oldProject
 $declared=@($source.productProject)+@($source.productCompileSources);$mapped=@()
 foreach($row in $declared){$full=[IO.Path]::GetFullPath($row.path);Need ($full.StartsWith($originalRoot+'\',[StringComparison]::OrdinalIgnoreCase)) 'ORIGINAL_SOURCE_SCOPE';$rel=$full.Substring($originalRoot.Length+1).Replace('\','/');$current=Resolve-YmccLightingChild $project $rel;Need ((Get-YmccLightingFileHash $current) -ceq $row.sha256 -and (Get-Item -LiteralPath $current).Length -eq [long]$row.bytes) 'CURRENT_PRODUCTION_SOURCE';$mapped+=@{path=$rel;sha256=$row.sha256}}
 $observed=@($CoreSourceInputs);Need ($observed.Count -eq $mapped.Count -and @($observed|Group-Object path|Where-Object Count -NE 1).Count -eq 0) 'EVALUATED_SOURCE_SET'
 foreach($row in $mapped){Need (@($observed|Where-Object {$_.path -ceq $row.path -and $_.sha256 -ceq $row.sha256}).Count -eq 1) 'EVALUATED_SOURCE_IDENTITY'}
 foreach($row in $e.projectRootInputs){$p=Resolve-YmccLightingChild $project $row.relative;Need ((Test-Path -LiteralPath $p -PathType Leaf) -eq $row.exists) 'ROOT_INPUT_SET';if($row.exists){Need ((Get-YmccLightingFileHash $p) -ceq $row.sha256) 'ROOT_INPUT_BYTES'}}
 $argsExpected=@('publish',$source.productProject.path,'-c','Release','--nologo','--disable-build-servers','--artifacts-path',$e.originalArtifactsRoot,'--output',(Split-Path -Parent $source.productOutput.path),'-p:Platform=AnyCPU','-p:UseSharedCompilation=false','-p:NuGetAudit=false','-nodeReuse:false')
 Need (Same @($publish.arguments) $argsExpected) 'ORIGINAL_ARGUMENTS';Need ([IO.Path]::GetFullPath($publish.file) -ieq [IO.Path]::GetFullPath($source.host.path)) 'ORIGINAL_HOST'
 $null=FileIdentity $source.host;Need ((Get-YmccLightingFileHash $DotnetPath) -ceq $source.host.sha256 -and $EvaluatedSdkVersion -ceq $e.sdkVersion) 'SELECTED_COMPILER_HOST_AND_SDK';foreach($row in $e.buildEvidenceFiles){$null=FileIdentity $row}
 $assetsFile=FileIdentity $e.projectAssets;$assets=Get-Content -LiteralPath $assetsFile -Raw -Encoding UTF8|ConvertFrom-Json
 Need (@($assets.libraries.PSObject.Properties).Count -eq 0 -and @($assets.targets.PSObject.Properties).Count -eq 1 -and $assets.targets.PSObject.Properties.Name -ceq 'net10.0' -and @($assets.targets.'net10.0'.PSObject.Properties).Count -eq 0) 'PACKAGE_DEPENDENCY_CLOSURE'
 Need ($assets.project.restore.sdkAnalysisLevel -ceq $e.sdkVersion -and [IO.Path]::GetFullPath($assets.project.restore.projectPath) -ieq $oldProject) 'ORIGINAL_SDK_PROJECT'
 $frameworks=@($assets.project.frameworks.'net10.0'.frameworkReferences.PSObject.Properties);Need ($frameworks.Count -eq 1 -and $frameworks[0].Name -ceq 'Microsoft.NETCore.App') 'FRAMEWORK_DEPENDENCY_CLOSURE'
 $global=Get-Content -LiteralPath (Join-Path $project 'global.json') -Raw -Encoding UTF8|ConvertFrom-Json;Need ($global.sdk.version -ceq $e.sdkVersion -and $global.sdk.rollForward -ceq 'disable') 'CURRENT_SDK_PIN'
 $hostRoot=Split-Path -Parent ([IO.Path]::GetFullPath($source.host.path));$sdkRoot=Join-Path $hostRoot ('sdk/'+$e.sdkVersion);$runtimeVersion=([string]$e.compilationOptions.'runtime-version').Split('-')[0]
 $expectedSets=[ordered]@{compiler=(Join-Path $sdkRoot 'Roslyn/bincore');sdk=(Join-Path $sdkRoot 'Sdks/Microsoft.NET.Sdk');runtime=(Join-Path $hostRoot ('shared/Microsoft.NETCore.App/'+$runtimeVersion));referencePack=(Join-Path $hostRoot ('packs/Microsoft.NETCore.App.Ref/'+$runtimeVersion))}
 Need (@($e.toolchainFileSets).Count -eq $expectedSets.Count -and @($e.toolchainFileSets|Group-Object role|Where-Object Count -NE 1).Count -eq 0) 'TOOLCHAIN_REQUIRED_SETS'
 foreach($role in $expectedSets.Keys){$sets=@($e.toolchainFileSets|Where-Object role -CEQ $role);Need ($sets.Count -eq 1 -and [IO.Path]::GetFullPath($sets[0].root) -ieq [IO.Path]::GetFullPath($expectedSets[$role])) 'TOOLCHAIN_REQUIRED_ROOT'}
 Need ([IO.Path]::GetFullPath($e.referenceAssemblyRoot) -ieq [IO.Path]::GetFullPath((Join-Path $expectedSets.referencePack 'ref/net10.0')) -and [IO.Path]::GetFullPath($e.compilerAssembly.path) -ieq [IO.Path]::GetFullPath((Join-Path $expectedSets.compiler 'Microsoft.CodeAnalysis.CSharp.dll')) -and [IO.Path]::GetFullPath($e.runtimeCoreLibrary.path) -ieq [IO.Path]::GetFullPath((Join-Path $expectedSets.runtime 'System.Private.CoreLib.dll')) -and [IO.Path]::GetFullPath($e.sdkVersionFile.path) -ieq [IO.Path]::GetFullPath((Join-Path $sdkRoot '.version'))) 'TOOLCHAIN_CANONICAL_FILES'
 $sdkNames=@(Get-ChildItem -LiteralPath $sdkRoot -File|Where-Object {$_.Name -eq '.version' -or $_.Extension -in @('.dll','.json','.props','.targets','.config')}|ForEach-Object Name|Sort-Object)
 Need (@(Compare-Object $sdkNames @($e.sdkFiles|ForEach-Object {[IO.Path]::GetFileName($_.path)}|Sort-Object)).Count -eq 0) 'SDK_ROOT_DEPENDENCY_SET'
 foreach($row in $e.sdkFiles){Need ((Split-Path -Parent ([IO.Path]::GetFullPath($row.path))) -ieq [IO.Path]::GetFullPath($sdkRoot)) 'SDK_ROOT_DEPENDENCY_SCOPE'}
 Need (@($e.buildEvidenceFiles).Count -ge 7 -and [IO.Path]::GetFullPath($e.projectAssets.path) -ieq [IO.Path]::GetFullPath((Join-Path $e.originalArtifactsRoot 'obj/YeManLightDeviceCore/project.assets.json'))) 'ORIGINAL_BUILD_EVIDENCE_SET'
 foreach($set in $e.toolchainFileSets){
  Need ([IO.Path]::IsPathRooted($set.root) -and $set.files.Count -gt 0) 'TOOLCHAIN_SET'
  $physical=@(Get-ChildItem -LiteralPath $set.root -File -Recurse|ForEach-Object {$_.FullName.Substring(([string]$set.root).TrimEnd('\').Length+1).Replace('\','/')}|Sort-Object)
  Need (@(Compare-Object $physical @($set.files.relative|Sort-Object)).Count -eq 0) 'TOOLCHAIN_FILE_SET'
  foreach($row in $set.files){$p=Resolve-YmccLightingChild $set.root $row.relative;$null=FileIdentity ([pscustomobject]@{path=$p;sha256=$row.sha256;bytes=$row.bytes})}
 }
 foreach($row in $e.sdkFiles){$null=FileIdentity $row}
 $versionFile=FileIdentity $e.sdkVersionFile;$sdkText=[IO.File]::ReadAllLines($versionFile);Need ($sdkText[1] -ceq $e.sdkVersion) 'SDK_VERSION_FILE'
 $meta=Get-YmccLightingCoreStaticMetadata $coreFile
 Need (Same $meta.options $e.compilationOptions) 'COMPILATION_OPTIONS_CHANGED'
 Need ([Diagnostics.FileVersionInfo]::GetVersionInfo((FileIdentity $e.compilerAssembly)).ProductVersion -ceq $meta.options.'compiler-version' -and $meta.options.'compiler-version'.EndsWith('+'+$sdkText[0],[StringComparison]::Ordinal)) 'COMPILER_IDENTITY'
 Need ([Diagnostics.FileVersionInfo]::GetVersionInfo((FileIdentity $e.runtimeCoreLibrary)).ProductVersion -ceq $meta.options.'runtime-version') 'COMPILER_RUNTIME_IDENTITY'
 Need ($meta.options.'source-file-count' -ceq [string]$meta.documents.Count -and $meta.documents.Count -eq (@($source.productCompileSources).Count+3)) 'PDB_DOCUMENT_COUNT'
 Need ($audit.status -ceq 'ACTUAL_PRODUCTION_PE_EMBEDDED_PDB_EXACT_SOURCE_SET_VERIFIED' -and $audit.sha256 -ceq $ExpectedSha256 -and $audit.assemblyOrEntryLoaded -eq $false -and $audit.hardwareIo -eq $false -and $audit.productMainInvoked -eq $false) 'EXISTING_PDB_AUDIT'
 Need (@($audit.allPdbDocuments).Count -eq $meta.documents.Count -and @($e.pdbDocuments).Count -eq $meta.documents.Count) 'PDB_AUDIT_DOCUMENT_SET'
 foreach($doc in $meta.documents){
  $rows=@($e.pdbDocuments|Where-Object pdbName -CEQ $doc.pdbName);$oldRows=@($audit.allPdbDocuments|Where-Object pdbName -CEQ $doc.pdbName);Need ($rows.Count -eq 1 -and $oldRows.Count -eq 1 -and $rows[0].file.sha256 -ceq $doc.sha256 -and $oldRows[0].sha256 -ceq $doc.sha256) 'PDB_DOCUMENT_BINDING'
  if($rows[0].kind -ceq 'production'){$rel=[string]$rows[0].relative;Need (@($mapped|Where-Object {$_.path -ceq $rel -and $_.sha256 -ceq $doc.sha256}).Count -eq 1) 'PDB_CURRENT_COMPILE';$null=FileIdentity ([pscustomobject]@{path=(Resolve-YmccLightingChild $project $rel);sha256=$rows[0].file.sha256;bytes=$rows[0].file.bytes})}
  else{Need ($rows[0].kind -ceq 'generated' -and [IO.Path]::GetFileName($rows[0].file.path) -in @('YeManLightDeviceCore.GlobalUsings.g.cs','YeManLightDeviceCore.AssemblyInfo.cs','.NETCoreApp,Version=v10.0.AssemblyAttributes.cs')) 'PDB_GENERATED_SET';$null=FileIdentity $rows[0].file}
 }
 Need ($meta.references.Count -eq @($e.compilationReferences).Count) 'PDB_REFERENCE_COUNT'
 foreach($ref in $meta.references){$rows=@($e.compilationReferences|Where-Object name -CEQ $ref.name);Need ($rows.Count -eq 1) 'PDB_REFERENCE_SET';$p=FileIdentity $rows[0].file;$actual=Get-YmccLightingCoreReferenceMetadata $p;Need ($actual.name -ceq $ref.name -and $actual.mvid -ceq $ref.mvid -and $actual.timestamp -eq $ref.timestamp -and $actual.imageSize -eq $ref.imageSize) 'PDB_REFERENCE_PE_IDENTITY'}
 $refNames=@(Get-ChildItem -LiteralPath $e.referenceAssemblyRoot -File -Filter '*.dll'|ForEach-Object Name|Sort-Object);Need (@(Compare-Object $refNames @($meta.references.name|Sort-Object)).Count -eq 0) 'COMPLETE_REFERENCE_PACK'
 Need ((Get-YmccLightingFileHash $coreFile) -ceq $ExpectedSha256 -and (Get-YmccLightingFileHash $evidenceFile) -ceq $evidenceHash) 'FINAL_READBACK'
 [pscustomobject]@{kind='ymcc-lighting-prepared-core-reuse';status='SOURCE_AND_TOOL_IDENTITY_VERIFIED';corePath=$coreFile;coreSha256=$ExpectedSha256;coreBytes=$e.core.bytes;evidencePath=$evidenceFile;evidenceSha256=$evidenceHash;sourceIdentityPath=$sourceFile;publishReceiptPath=$publishFile;embeddedPdbAuditPath=$auditFile;originalPublishExitCode=0;originalPublishArguments=@($publish.arguments);compilerVersion=$meta.options.'compiler-version';runtimeVersion=$meta.options.'runtime-version';sdkVersion=$e.sdkVersion;productionSourceDocuments=@($source.productCompileSources).Count;generatedSourceDocuments=3;compilationReferences=$meta.references.Count;toolchainFiles=(@($e.toolchainFileSets|ForEach-Object {$_.files}).Count+@($e.sdkFiles).Count);publishedThisInvocation=$false;productMainInvoked=$false;runtimeAssemblyLoaded=$false;evidenceCapturedRetrospectively=$e.evidenceCapturedRetrospectively}
}
