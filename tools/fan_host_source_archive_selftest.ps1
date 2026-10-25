#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PreparedRoot,[Parameter(Mandatory=$true)][string]$MetadataToolDll,[Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $EvidenceRoot){throw 'Fresh evidence root required'}
$prepared=Get-Content -LiteralPath (Join-Path $PreparedRoot 'PREPARED.json') -Raw|ConvertFrom-Json
$bundle=[string]$prepared.bundleRoot;$expected=[string]$prepared.sourceBundleManifestSha256
$before=(Get-FileHash -LiteralPath (Join-Path $bundle 'manifest.json')).Hash
New-Item -ItemType Directory -Path $EvidenceRoot|Out-Null
$rows=[Collections.Generic.List[object]]::new()
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function WriteManifest([string]$root,$m){[IO.File]::WriteAllText((Join-Path $root 'manifest.json'),($m|ConvertTo-Json -Depth 100)+"`n",[Text.UTF8Encoding]::new($false))}
function Reseal([string]$root,$m){
 $m.files=@(Get-ChildItem -LiteralPath $root -Recurse -File|Where-Object{$_.FullName-ne(Join-Path $root 'manifest.json')}|Sort-Object FullName|ForEach-Object{[ordered]@{path=[IO.Path]::GetRelativePath($root,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=Hash $_.FullName}})
 WriteManifest $root $m
 return Hash (Join-Path $root 'manifest.json')
}
function Case([string]$name,[scriptblock]$mutate,[string]$errorCode,[bool]$reseal){
 $dir=Join-Path $EvidenceRoot $name;$clone=Join-Path $dir 'bundle';[IO.Directory]::CreateDirectory($dir)|Out-Null
 Copy-Item -LiteralPath $bundle -Destination $clone -Recurse
 $m=Get-Content -LiteralPath (Join-Path $clone 'manifest.json') -Raw|ConvertFrom-Json
 & $mutate $clone $m
 $pin=if($reseal){Reseal $clone $m}else{$expected}
 $log=Join-Path $dir 'output.log'
 & dotnet $MetadataToolDll verify $clone $pin (Join-Path $dir 'result.json') *> $log
 $exit=$LASTEXITCODE;$raw=[IO.File]::ReadAllText($log)
 $passed=if($errorCode){$exit-eq1-and$raw.Contains($errorCode)}else{$exit-eq0-and$raw.Contains('SOURCE_ARCHIVE_METADATA_OK')}
 $rows.Add([ordered]@{name=$name;passed=$passed;exitCode=$exit;expectedError=$errorCode;resealedForInternalProof=$reseal;log=$log})
 Write-Output "$(if($passed){'PASS'}else{'FAIL'}) $name"
}
Case 'positive-portable-original' {param($p,$m)} '' $false
Case 'negative-wrong-pdb-despite-resealed-file-identities' {
 param($p,$m)
 Copy-Item -LiteralPath (Join-Path $p 'artifacts/YeManFanBackend.pdb') -Destination (Join-Path $p 'artifacts/YeManFanHost.pdb') -Force
 ($m.modules|Where-Object name -CEQ 'YeManFanHost').pdbSha256=Hash (Join-Path $p 'artifacts/YeManFanHost.pdb')
} 'SOURCE_ARCHIVE_DLL_PDB_MISMATCH' $true
Case 'negative-source-bytes-despite-resealed-source-identities' {
 param($p,$m)
 $relative='FanLab/real-host/Program.cs';$file=Join-Path $p ('source/'+$relative)
 [IO.File]::AppendAllText($file,"`n// Controlled source checksum fault`n")
 ($m.sourceInputs|Where-Object path -CEQ $relative).sha256=Hash $file
 foreach($module in $m.modules){foreach($doc in $module.documents){if($doc.archivePath-ceq('source/'+$relative)){$doc.sha256=Hash $file}}}
} 'SOURCE_ARCHIVE_DOCUMENT_CHECKSUM_MISMATCH' $true
Case 'negative-resealed-manifest-cannot-change-external-pin' {
 param($p,$m);$m.purpose='Controlled reseal fault';WriteManifest $p $m
} 'SOURCE_ARCHIVE_EXTERNAL_MANIFEST_PIN_MISMATCH' $false
Case 'negative-lighting-document-omitted-despite-reseal' {
 param($p,$m)
 $relative='capabilities/HcLightingCapabilityCatalog.cs';$archive='source/'+$relative
 Remove-Item -LiteralPath (Join-Path $p $archive)
 $m.sourceInputs=@($m.sourceInputs|Where-Object path -CNE $relative)
 foreach($module in $m.modules){$module.documents=@($module.documents|Where-Object archivePath -CNE $archive)}
} 'SOURCE_ARCHIVE_DOCUMENT_SET' $true
Case 'negative-generated-document-omitted-despite-reseal' {
 param($p,$m)
 $module=$m.modules|Where-Object name -CEQ 'YeManFanHost';$doc=@($module.documents|Where-Object generated)[0]
 Remove-Item -LiteralPath (Join-Path $p $doc.archivePath)
 $module.documents=@($module.documents|Where-Object archivePath -CNE $doc.archivePath)
} 'SOURCE_ARCHIVE_DOCUMENT_SET' $true
Case 'negative-duplicate-module-despite-reseal' {
 param($p,$m);$hostModule=$m.modules|Where-Object name -CEQ 'YeManFanHost';$m.modules=@($hostModule,$hostModule)
} 'SOURCE_ARCHIVE_MODULE_NAME' $true
Case 'negative-source-row-disconnected-from-valid-pdb-copy' {
 param($p,$m)
 $relative='FanLab/real-host/Program.cs';$current=Join-Path $p ('source/'+$relative);$copy='evidence/old-source-copy.cs'
 Copy-Item -LiteralPath $current -Destination (Join-Path $p $copy)
 [IO.File]::AppendAllText($current,"`n// Controlled disconnected source row`n")
 ($m.sourceInputs|Where-Object path -CEQ $relative).sha256=Hash $current
 foreach($module in $m.modules){foreach($doc in $module.documents){if($doc.archivePath-ceq('source/'+$relative)){$doc.archivePath=$copy}}}
} 'SOURCE_ARCHIVE_DOCUMENT_SOURCE_ASSOCIATION' $true
if((Get-FileHash -LiteralPath (Join-Path $bundle 'manifest.json')).Hash-cne$before){throw 'Original candidate changed'}
$result=[ordered]@{schemaVersion=1;ok=@($rows|Where-Object{-not$_.passed}).Count-eq0;checks=$rows.Count;scope='actual PE/portable-PDB metadata and source archive verification; fault copies only; no Host/HC execution or hardware';originalCandidateManifestSha256=$expected;metadataToolSha256=Hash $MetadataToolDll;results=$rows}
[IO.File]::WriteAllText((Join-Path $EvidenceRoot 'result.json'),($result|ConvertTo-Json -Depth 20)+"`n",[Text.UTF8Encoding]::new($false))
if(-not$result.ok){exit 1}