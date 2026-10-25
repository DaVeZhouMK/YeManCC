[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$AssemblyPath,[Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputPath){throw 'Fresh metadata output required'}
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
$pdb=[IO.Path]::ChangeExtension($AssemblyPath,'.pdb');$stream=[IO.File]::OpenRead($AssemblyPath);$debug=[IO.File]::OpenRead($pdb);$pe=$null;$provider=$null
try{
 $pe=[Reflection.PortableExecutable.PEReader]::new($stream);$entries=@($pe.ReadDebugDirectory()|Where-Object Type -EQ CodeView);if($entries.Count -ne 1){throw 'Ambiguous CodeView'}
 $cv=$pe.ReadCodeViewDebugDirectoryData($entries[0]);$provider=[Reflection.Metadata.MetadataReaderProvider]::FromPortablePdbStream($debug);$reader=$provider.GetMetadataReader();$id=[byte[]]$reader.DebugMetadataHeader.Id
 if([Guid]::new([byte[]]$id[0..15]) -ne $cv.Guid -or [BitConverter]::ToUInt32($id,16) -ne $entries[0].Stamp){throw 'Module PDB pair differs'}
 $docs=@(foreach($handle in $reader.Documents){$doc=$reader.GetDocument($handle);if($reader.GetGuid($doc.HashAlgorithm).ToString() -cne '8829d00f-11b8-4213-878b-770e8597ac16'){throw 'Non-SHA256 PDB document'};[pscustomobject]@{originalPath=$reader.GetString($doc.Name);sha256=([BitConverter]::ToString($reader.GetBlobBytes($doc.Hash))).Replace('-','').ToLowerInvariant()}})
 $records=@(foreach($handle in $reader.CustomDebugInformation){$record=$reader.GetCustomDebugInformation($handle);$kind=$reader.GetGuid($record.Kind).ToString();if($kind -in @('b5feec05-8cd0-4a83-96da-466284bb4bd8','7e4d4708-096e-4c5c-aeda-cb10ba6a740d')){[pscustomobject]@{kind=$kind;bytesBase64=[Convert]::ToBase64String($reader.GetBlobBytes($record.Value))}}})
 if($records.Count -ne 2){throw 'Compiler records missing'}
 $result=[ordered]@{schemaVersion=1;status='PASS';assemblyPath=$AssemblyPath;assemblySha256=(Hash $AssemblyPath);pdbPath=$pdb;pdbSha256=(Hash $pdb);codeViewGuid=$cv.Guid.ToString();stamp=$entries[0].Stamp;pairVerified=$true;documents=$docs;compilationRecords=$records;runtimeAssemblyLoaded=$false}
 [IO.File]::WriteAllText($OutputPath,($result|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false));[pscustomobject]@{status='PASS';module=[IO.Path]::GetFileName($AssemblyPath);documents=$docs.Count;records=$records.Count}|ConvertTo-Json
}finally{if($null -ne $provider){$provider.Dispose()};if($null -ne $pe){$pe.Dispose()};$stream.Dispose();$debug.Dispose()}