[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BundleRoot,[Parameter(Mandatory=$true)][string]$EvidencePath)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$root=[IO.Path]::GetFullPath($BundleRoot).TrimEnd('\')
if(Test-Path -LiteralPath $EvidencePath){throw 'Fresh source-bundle evidence required'}
$manifest=Get-Content -LiteralPath (Join-Path $root 'LIGHTSETTER-SOURCE.json') -Raw -Encoding UTF8|ConvertFrom-Json
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Member([string]$Relative){
 if([IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)'){throw 'Unsafe bundle member'}
 $path=[IO.Path]::GetFullPath((Join-Path $root $Relative));if(-not $path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Escaped bundle member'}
 $current=$path;while($current.Length -ge $root.Length){if((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse source bundle refused'};if($current -ieq $root){break};$current=Split-Path -Parent $current};return $path
}
$declared=@($manifest.members|ForEach-Object path|Sort-Object)
$physical=@(Get-ChildItem -LiteralPath $root -Recurse -File|Where-Object Name -CNE 'LIGHTSETTER-SOURCE.json'|ForEach-Object {$_.FullName.Substring($root.Length+1).Replace('\','/')}|Sort-Object)
if($declared.Count -ne $physical.Count -or (Compare-Object $declared $physical) -or @($manifest.members|Group-Object path|Where-Object Count -NE 1).Count){throw 'Bundle physical set differs'}
foreach($entry in $manifest.members){$file=Member $entry.path;if((Hash $file) -cne $entry.sha256 -or (Get-Item -LiteralPath $file).Length -ne $entry.bytes){throw ('Bundle member changed: '+$entry.path)}}
$dll=Member $manifest.helper.path;$pdb=Member $manifest.pdb.path
if((Hash $dll) -cne $manifest.helper.sha256 -or (Hash $pdb) -cne $manifest.pdb.sha256){throw 'Bundle artifact identity differs'}
$stream=[IO.File]::OpenRead($dll);$pdbStream=[IO.File]::OpenRead($pdb);$pe=$null;$provider=$null
try{
 $pe=[Reflection.PortableExecutable.PEReader]::new($stream);$entries=@($pe.ReadDebugDirectory()|Where-Object Type -EQ CodeView);if($entries.Count -ne 1){throw 'Ambiguous CodeView'}
 $cv=$pe.ReadCodeViewDebugDirectoryData($entries[0]);$provider=[Reflection.Metadata.MetadataReaderProvider]::FromPortablePdbStream($pdbStream);$reader=$provider.GetMetadataReader();$id=[byte[]]$reader.DebugMetadataHeader.Id
 if([Guid]::new([byte[]]$id[0..15]) -ne $cv.Guid -or [BitConverter]::ToUInt32($id,16) -ne $entries[0].Stamp){throw 'Helper/PDB pairing differs'}
 $documents=@(foreach($handle in $reader.Documents){
  $doc=$reader.GetDocument($handle);$name=$reader.GetString($doc.Name);$sum=([BitConverter]::ToString($reader.GetBlobBytes($doc.Hash))).Replace('-','').ToLowerInvariant();$rows=@($manifest.sourceDocuments|Where-Object originalPath -CEQ $name)
  if($rows.Count -ne 1 -or $reader.GetGuid($doc.HashAlgorithm).ToString() -cne '8829d00f-11b8-4213-878b-770e8597ac16' -or $rows[0].sha256 -cne $sum -or (Hash (Member $rows[0].archivePath)) -cne $sum){throw 'PDB document differs from archived bytes'}
  [pscustomobject]@{path=$name;archivePath=$rows[0].archivePath;sha256=$sum;generated=$rows[0].generated}
 })
 if($documents.Count -ne 15 -or @($documents|Where-Object generated -EQ $false).Count -ne 12 -or @($documents|Where-Object generated -EQ $true).Count -ne 3){throw 'PDB source set differs'}
 $records=@(foreach($handle in $reader.CustomDebugInformation){$record=$reader.GetCustomDebugInformation($handle);$kind=$reader.GetGuid($record.Kind).ToString();if($kind -in @('b5feec05-8cd0-4a83-96da-466284bb4bd8','7e4d4708-096e-4c5c-aeda-cb10ba6a740d')){[pscustomobject]@{kind=$kind;bytesBase64=[Convert]::ToBase64String($reader.GetBlobBytes($record.Value))}}})
 if($records.Count -ne 2){throw 'Compiler records missing'}
 foreach($record in $records){if(@($manifest.compilationRecords|Where-Object {$_.kind -ceq $record.kind -and $_.bytesBase64 -ceq $record.bytesBase64}).Count -ne 1){throw 'Compiler record changed'}}
 $result=[ordered]@{schemaVersion=1;status='PASS';bundleManifestSha256=(Hash (Join-Path $root 'LIGHTSETTER-SOURCE.json'));helperSha256=(Hash $dll);pdbSha256=(Hash $pdb);pairVerified=$true;members=$declared.Count;sourceDocuments=$documents.Count;manualCompileSources=12;generatedDocuments=3;compilerRecords=$records.Count;originalAbsoluteSourceFilesRequired=$false;runtimeAssemblyLoaded=$false;hardwareIo=$false}
 [IO.File]::WriteAllText($EvidencePath,($result|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false));$result|ConvertTo-Json
}finally{if($null -ne $provider){$provider.Dispose()};if($null -ne $pe){$pe.Dispose()};$stream.Dispose();$pdbStream.Dispose()}