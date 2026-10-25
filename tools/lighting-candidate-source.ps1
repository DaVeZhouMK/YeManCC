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