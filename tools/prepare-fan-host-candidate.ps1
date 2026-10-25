#requires -Version 7.0
<# Prepare a source-bound Fan component candidate. Does not promote, install or start Host/HC. #>
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$ArtifactRoot,
 [Parameter(Mandatory=$true)][string]$BuildEvidenceRoot,
 [Parameter(Mandatory=$true)][string]$MetadataToolDll,
 [Parameter(Mandatory=$true)][string]$OutputRoot,
 [ValidatePattern('^[a-z][a-z0-9-]{0,63}$')][string]$ComponentId='candidate-coldwake-20261023',
 [string]$PayloadInstallerSource=''
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$out=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if($out -ieq $repo -or $out.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Use fresh evidence outside the source worktree'}
if(Test-Path -LiteralPath $out){throw 'Fresh candidate output required'}
for($cursor=$out;$cursor;$cursor=Split-Path -Parent $cursor){if((Test-Path -LiteralPath $cursor)-and((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Candidate output ancestor reparse refused'}}
function Safe-Target([string]$Path){$full=[IO.Path]::GetFullPath($Path);if(-not$full.StartsWith($out+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Candidate output escape refused'};return $full}
function Hash([string]$File){(Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant()}
function Json([string]$File,$Value){$File=Safe-Target $File;[IO.Directory]::CreateDirectory((Split-Path -Parent $File))|Out-Null;[IO.File]::WriteAllText($File,($Value|ConvertTo-Json -Depth 100)+"`n",[Text.UTF8Encoding]::new($false))}
function Regular([string]$File){
 if(-not(Test-Path -LiteralPath $File -PathType Leaf)){throw "Candidate input missing: $File"}
 for($cursor=[IO.Path]::GetFullPath($File);$cursor;$cursor=Split-Path -Parent $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Candidate reparse input refused'}}
}
function Copy-Checked([string]$From,[string]$To){$To=Safe-Target $To;Regular $From;$hash=Hash $From;[IO.Directory]::CreateDirectory((Split-Path -Parent $To))|Out-Null;Copy-Item -LiteralPath $From -Destination $To;if((Hash $From)-cne$hash-or(Hash $To)-cne$hash){throw "Candidate copy drift: $From"};return $hash}
function Set-One([string]$Text,[string]$Pattern,[string]$Value){$m=[regex]::Matches($Text,$Pattern);if($m.Count-ne1-or$m[0].Groups.Count-ne2){throw "Candidate pin shape mismatch: $Pattern"};$g=$m[0].Groups[1];return $Text.Substring(0,$g.Index)+$Value+$Text.Substring($g.Index+$g.Length)}
$inputs=[ordered]@{}
function Input([string]$File){Regular $File;$p=[IO.Path]::GetFullPath($File);$h=Hash $p;if($inputs.Contains($p)-and$inputs[$p]-cne$h){throw "Candidate input drift: $p"};$inputs[$p]=$h}
Input $MetadataToolDll
[IO.Directory]::CreateDirectory($out)|Out-Null
$project=Join-Path $out 'project'
$bundleRelative='deps/fan-host-source/'+$ComponentId
$bundle=Join-Path $project $bundleRelative
[IO.Directory]::CreateDirectory($bundle)|Out-Null
$inspectionFile=Join-Path $out 'inspection.json'
& dotnet $MetadataToolDll inspect $repo ([IO.Path]::GetFullPath($ArtifactRoot)) ([IO.Path]::GetFullPath($BuildEvidenceRoot)) $inspectionFile
if($LASTEXITCODE-ne0){throw 'Candidate PE/PDB/current source binding failed'}
$inspection=Get-Content -LiteralPath $inspectionFile -Raw|ConvertFrom-Json
function Get-CompiledSet {
$observedCompile=[ordered]@{}
foreach($relative in @('FanLab/real-host/YeManFanHost.csproj','FanLab/fan-backend/YeManFanBackend.csproj')){
 $observedCompile[$relative]=$true
 $folder=Split-Path -Parent (Join-Path $repo $relative)
 foreach($file in Get-ChildItem -LiteralPath $folder -Recurse -File -Filter '*.cs'|Where-Object{$_.FullName-notmatch'[\\/](bin|obj)[\\/]'} ){$observedCompile[[IO.Path]::GetRelativePath($repo,$file.FullName).Replace('\','/')]=$true}
 [xml]$xml=Get-Content -LiteralPath (Join-Path $repo $relative) -Raw
 foreach($include in $xml.SelectNodes('//*[local-name()="Compile" and @Include]')){
  $value=[string]$include.Include
  if($value-match'[*?$]' -or [IO.Path]::IsPathRooted($value)){throw 'Candidate linked compile requires explicit relative input'}
  $full=[IO.Path]::GetFullPath((Join-Path $folder $value));if(-not$full.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Candidate linked compile escape'}
  $observedCompile[[IO.Path]::GetRelativePath($repo,$full).Replace('\','/')]=$true
 }
}
return ,$observedCompile
}
$compiled=Get-CompiledSet
# Compiler documents prove the handwritten closure, not just a caller-supplied file count.
$handwritten=@($inspection.documents|Where-Object{[string]$_.selectedPath -and ([IO.Path]::GetFullPath($_.selectedPath)).StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)})
$docPaths=@($handwritten|ForEach-Object{[IO.Path]::GetRelativePath($repo,$_.selectedPath).Replace('\','/')}|Sort-Object -Unique)
$compileCs=@($compiled.Keys|Where-Object{$_-like'*.cs'}|Sort-Object -Unique)
if(Compare-Object $docPaths $compileCs){throw 'Candidate compiler document/source closure differs'}
$sourceRows=@();$pins=[ordered]@{}
foreach($relative in @($compiled.Keys|Sort-Object)){
 $from=Join-Path $repo $relative;Input $from;$h=Copy-Checked $from (Join-Path $bundle ('source/'+$relative));$pins[$relative]=$h
 $sourceRows+= [ordered]@{path=$relative;archivePath='source/'+$relative;sha256=$h;roles=@($(if($relative-like'*.csproj'){'project'}else{'Compile'}))}
}
$contentRelative='FanLab/real-host/YeManFanHost.json'
Input (Join-Path $repo $contentRelative)
$contentHash=Copy-Checked (Join-Path $repo $contentRelative) (Join-Path $bundle ('source/'+$contentRelative))
$sourceRows+=[ordered]@{path=$contentRelative;archivePath='source/'+$contentRelative;sha256=$contentHash;roles=@('Content')}
$modules=@()
foreach($module in $inspection.modules){
 $name=[string]$module.name
 foreach($ext in @('dll','pdb')){Input (Join-Path $ArtifactRoot ($name+'.'+$ext));Copy-Checked (Join-Path $ArtifactRoot ($name+'.'+$ext)) (Join-Path $bundle ('artifacts/'+$name+'.'+$ext))|Out-Null}
 $docs=@()
 foreach($doc in @($inspection.documents|Where-Object{$_.module-ceq$name})){
  $isSource=([IO.Path]::GetFullPath($doc.selectedPath)).StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)
  $relative=if($isSource){[IO.Path]::GetRelativePath($repo,$doc.selectedPath).Replace('\','/')}else{$null}
  $archivePath=if($isSource){'source/'+$relative}else{'generated/'+$name+'/'+[IO.Path]::GetFileName($doc.selectedPath)}
  Input $doc.selectedPath
  if(-not$isSource){Copy-Checked $doc.selectedPath (Join-Path $bundle $archivePath)|Out-Null}
  if((Hash (Join-Path $bundle $archivePath))-cne$doc.sha256){throw 'Candidate document changed after inspection'}
  $docs+=[ordered]@{originalPath=[string]$doc.originalPath;archivePath=$archivePath;sourceRelative=$relative;generated=(-not$isSource);sha256=[string]$doc.sha256;pdbHashAlgorithm=[string]$doc.pdbHashAlgorithm;pdbChecksum=[string]$doc.pdbChecksum}
 }
 $modules+=[ordered]@{name=$name;dllPath='artifacts/'+$name+'.dll';dllSha256=$module.dllSha256;pdbPath='artifacts/'+$name+'.pdb';pdbSha256=$module.pdbSha256;codeViewGuid=$module.codeViewGuid;stamp=$module.stamp;mvid=$module.mvid;compilationOptions=$module.compilationOptions;documents=$docs;customDebugRecords=$module.customDebugRecords}
}
Copy-Checked $inspectionFile (Join-Path $bundle 'evidence/pe-pdb-current-source.json')|Out-Null
$buildTargets=Join-Path (Split-Path -Parent $BuildEvidenceRoot) 'isolated-build.targets'
Regular $buildTargets;Input $buildTargets
Copy-Checked $buildTargets (Join-Path $bundle 'evidence/isolated-build.targets')|Out-Null
Input $MetadataToolDll;Input $PSCommandPath
Json (Join-Path $bundle 'evidence/build-provenance.json') ([ordered]@{scope='existing actual isolated mainline Release build; no rebuild or runtime execution';artifactRoot=[IO.Path]::GetFullPath($ArtifactRoot);buildEvidenceRoot=[IO.Path]::GetFullPath($BuildEvidenceRoot);metadataToolSha256=Hash $MetadataToolDll;preparationToolSha256=Hash $PSCommandPath;isolatedBuildTargetsSha256=Hash $buildTargets;projectSourceRebuildReproducibilityProven=$false})
# An installer contract change is an explicit source input, separate from the
# previously valid immutable baseline. Archive the exact new bytes before the
# existing payload manifest and consumer pins are generated below.
if($PayloadInstallerSource){
 if([IO.Path]::GetFileName($PayloadInstallerSource)-cne'install-fan-host-payload.ps1'){throw 'Candidate installer source filename refused'}
 Input $PayloadInstallerSource
 Copy-Checked $PayloadInstallerSource (Join-Path $bundle 'payload-sources/install-fan-host-payload.ps1')|Out-Null
}
$members=@(Get-ChildItem -LiteralPath $bundle -Recurse -File|Sort-Object FullName|ForEach-Object{[ordered]@{path=[IO.Path]::GetRelativePath($bundle,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=Hash $_.FullName}})
$manifest=[ordered]@{schemaVersion=1;kind='ymcc-host-component-source-bundle';componentId=$ComponentId;selectionStatus='prepared-not-promoted';runtimeDeployment=$false;sourceInputs=$sourceRows;modules=$modules;files=$members;historicalSourceDocumentPathsRequired=$false;rebuildReproducibilityProven=$false;purpose='Current fan initialization repair candidate; not installed or published'}
if($PayloadInstallerSource){$manifest['payloadSourceInputs']=@([ordered]@{path='payload-sources/install-fan-host-payload.ps1';sha256=Hash $PayloadInstallerSource})}
$bundleManifest=Join-Path $bundle 'manifest.json';Json $bundleManifest $manifest;$bundleHash=Hash $bundleManifest
& dotnet $MetadataToolDll verify $bundle $bundleHash (Join-Path $out 'portable-archive-verification.json')
if($LASTEXITCODE-ne0){throw 'Candidate portable source archive failed independent verification'}

$oldPayload=Join-Path $repo 'PowerControl/fan-host';$oldManifestPath=Join-Path $oldPayload 'YeManFanHost.payload.json';Input $oldManifestPath
$old=Get-Content -LiteralPath $oldManifestPath -Raw|ConvertFrom-Json
if($old.schemaVersion-ne2-or@($old.files).Count-ne9){throw 'Candidate baseline must be exact nine-file schema2 payload'}
$payload=Join-Path $project 'PowerControl/fan-host';[IO.Directory]::CreateDirectory($payload)|Out-Null
$seen=@{}
foreach($row in $old.files){
 $name=[string]$row.path;if($name-notmatch'^[^/\\:]+$'-or$seen.ContainsKey($name)){throw 'Candidate unsafe or duplicate payload member'};$seen[$name]=$true
 $from=Join-Path $oldPayload $name;Input $from;if((Hash $from)-cne([string]$row.sha256).ToLowerInvariant()){throw 'Candidate baseline member differs from manifest'}
 Copy-Checked $from (Join-Path $payload $name)|Out-Null
}
foreach($name in @('YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll','YeManFanHost.deps.json','YeManFanHost.runtimeconfig.json','YeManFanHost.json')){Input (Join-Path $ArtifactRoot $name);Copy-Item -LiteralPath (Join-Path $ArtifactRoot $name) -Destination (Join-Path $payload $name) -Force}
if((Hash (Join-Path $payload 'YeManFanHost.json'))-cne$contentHash){throw 'Candidate source and artifact config differ'}
if($PayloadInstallerSource){Copy-Checked $PayloadInstallerSource (Join-Path $payload 'install-fan-host-payload.ps1')|Out-Null}
$hostHash=Hash (Join-Path $payload 'YeManFanHost.dll');$exeHash=Hash (Join-Path $payload 'YeManFanHost.exe');$backendHash=Hash (Join-Path $payload 'YeManFanBackend.dll')
$authPath=Join-Path $payload 'YeManFanHost.authorization.md';$auth=[IO.File]::ReadAllText($authPath)
$auth=Set-One $auth 'approvedHostDllSha256:\s*([0-9A-Fa-f]{64})' $hostHash.ToUpperInvariant()
$auth=Set-One $auth 'approvedHostExeSha256:\s*([0-9A-Fa-f]{64})' $exeHash.ToUpperInvariant()
$auth=Set-One $auth '(?m)^sourceSnapshotManifestSha256:\s*([0-9A-Fa-f]{64})' $bundleHash
$auth=Set-One $auth '(?m)^sourceBatch:[ \t]*([^\r\n]+)' $ComponentId
$auth+="`r`nfanColdWakePreparedCandidate: $ComponentId; fan-only initialization and authenticated on-demand handshake; staged-not-promoted`r`n"
[IO.File]::WriteAllText($authPath,$auth,[Text.UTF8Encoding]::new($true))
$newFiles=@(Get-ChildItem -LiteralPath $payload -File|Sort-Object Name|ForEach-Object{[ordered]@{path=$_.Name;sha256=Hash $_.FullName}})
Json (Join-Path $payload 'YeManFanHost.payload.json') ([ordered]@{schemaVersion=2;runtimeManifest=$old.runtimeManifest;runtimeId=$old.runtimeId;files=$newFiles})
$payloadHash=Hash (Join-Path $payload 'YeManFanHost.payload.json')
# The original runtime and shared runtime are immutable candidate inputs. Copy to the same relative layout.
$runtimeRelative=[string]$old.runtimeManifest
if($runtimeRelative -notmatch '(?i)^\.\.[\\/][^\\/]+(?:[\\/][^\\/]+)*[\\/]HandheldCompanion\.runtime\.json$' -or $runtimeRelative.Substring(3)-match '(^|[\\/])\.\.([\\/]|$)'){throw 'Candidate runtime manifest path refused'}
$runtimeManifest=[IO.Path]::GetFullPath((Join-Path $oldPayload $runtimeRelative))
if(-not$runtimeManifest.StartsWith((Join-Path $repo 'PowerControl')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Candidate runtime source escape refused'}
$runtime=Split-Path -Parent $runtimeManifest;$stagedRuntime=[IO.Path]::GetFullPath((Join-Path $payload ([string]$old.runtimeManifest)))
$stagedRuntime=Split-Path -Parent $stagedRuntime
$stagedRuntime=Safe-Target $stagedRuntime
if(-not$stagedRuntime.StartsWith((Join-Path $project 'PowerControl')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Candidate runtime destination escape refused'}
foreach($tree in @(@($runtime,$stagedRuntime),@((Join-Path $repo 'PowerControl/ymcc-shared-runtime'),(Join-Path $project 'PowerControl/ymcc-shared-runtime')))){
 foreach($file in Get-ChildItem -LiteralPath $tree[0] -Recurse -File){$relative=[IO.Path]::GetRelativePath($tree[0],$file.FullName);Input $file.FullName;Copy-Checked $file.FullName (Join-Path $tree[1] $relative)|Out-Null}
}
$verifySource=Join-Path $repo 'tools/verify-r5v9-fan-host-payload.ps1';Input $verifySource
$verify=[IO.File]::ReadAllText($verifySource)
$verify=Set-One $verify "'YeManFanHost\.dll'\s*=\s*'([0-9A-Fa-f]{64})'" $hostHash.ToUpperInvariant()
$verify=Set-One $verify "'YeManFanHost\.exe'\s*=\s*'([0-9A-Fa-f]{64})'" $exeHash.ToUpperInvariant()
# Current verifier derives authorization markers from its already updated expected map.
foreach($marker in @("('approvedHostDllSha256: ' + `$expected['YeManFanHost.dll'])","('approvedHostExeSha256: ' + `$expected['YeManFanHost.exe'])")){
 if([regex]::Matches($verify,[regex]::Escape($marker)).Count-ne1){throw 'Candidate authorization marker must derive from the exact expected map'}
}
$verify=Set-One $verify '\$expectedManifestSha256\s*=\s*''([0-9A-Fa-f]{64})''' $payloadHash
$verify=Set-One $verify '\$componentSourceBundleRelative\s*=\s*''([^'']+)''' $bundleRelative
$verify=Set-One $verify '\$componentSourceBundleManifestSha256\s*=\s*''([0-9A-Fa-f]{64})''' $bundleHash
$verify=Set-One $verify '\$componentBackendSha256\s*=\s*''([0-9A-Fa-f]{64})''' $backendHash
$pinText=[string]::Join("`r`n",@($pins.GetEnumerator()|ForEach-Object{"  '$($_.Key)' = '$($_.Value)'"}))
$verify=Set-One $verify '(?s)\$compiledSourcePins\s*=\s*\[ordered\]@\{(.*?)\r?\n\}' ("`r`n"+$pinText)
$stagedVerify=Join-Path $project 'tools/verify-r5v9-fan-host-payload.ps1';[IO.Directory]::CreateDirectory((Split-Path -Parent $stagedVerify))|Out-Null
[IO.File]::WriteAllText($stagedVerify,$verify,[Text.UTF8Encoding]::new($false))
$helper=Join-Path $repo 'tools/hc-device-runtime/verify-host-source-bundle.ps1';Input $helper
Copy-Checked $helper (Join-Path $project 'tools/hc-device-runtime/verify-host-source-bundle.ps1')|Out-Null
& $stagedVerify -PayloadRoot $payload -SourceRoot (Join-Path $bundle 'source') *> (Join-Path $out 'staged-verifier.log')
if($LASTEXITCODE-ne0){throw 'Staged original payload verifier refused candidate'}
# Keep the actual publication/copy ASTs for candidate qualification. Only candidate identity data changes.
foreach($relative in @('tools/build-workspace.ps1','tools/package-release.ps1','src/bridge/fanHost.ts','src/bridge/fanHost/startupConfiguration.ts','src/bridge/fanHost/deployment.ts','src/bridge/fanHost/capabilityAdmission.ts')){
 $from=Join-Path $repo $relative;Input $from;Copy-Checked $from (Join-Path $project $relative)|Out-Null
}
$nativeFrom=Join-Path $repo 'native/main.cpp';Input $nativeFrom
$native=Set-One ([IO.File]::ReadAllText($nativeFrom)) '\$expectedFanHostV2ManifestSha256\s*=\s*''([0-9A-Fa-f]{64})''' $payloadHash
$nativeOut=Join-Path $project 'native/main.cpp';[IO.Directory]::CreateDirectory((Split-Path -Parent $nativeOut))|Out-Null
[IO.File]::WriteAllText($nativeOut,$native,[Text.UTF8Encoding]::new($false))
$producerFrom=Join-Path $repo 'tools/fan_promotion_gate_selftest.ps1';Input $producerFrom
$producer=[IO.File]::ReadAllText($producerFrom)
$producer=Set-One $producer '\$componentBundleRelative\s*=\s*''([^'']+)''' $bundleRelative
$producer=Set-One $producer '\$componentBundleManifestSha256\s*=\s*''([0-9A-Fa-f]{64})''' $bundleHash
[IO.File]::WriteAllText((Join-Path $project 'tools/fan_promotion_gate_selftest.ps1'),$producer,[Text.UTF8Encoding]::new($false))
$finalCompile=Get-CompiledSet
if(Compare-Object @($compiled.Keys|Sort-Object) @($finalCompile.Keys|Sort-Object)){throw 'Candidate compile input set changed during preparation'}
foreach($entry in $inputs.GetEnumerator()){if((Hash $entry.Key)-cne$entry.Value){throw "Candidate input changed during preparation: $($entry.Key)"}}
Json (Join-Path $out 'PREPARED.json') ([ordered]@{schemaVersion=1;status='STAGED_SOURCE_BOUND_CANDIDATE_NOT_PROMOTED';componentId=$ComponentId;projectRoot=$project;payloadRoot=$payload;bundleRoot=$bundle;hostDllSha256=$hostHash;backendDllSha256=$backendHash;apphostSha256=$exeHash;payloadManifestSha256=$payloadHash;sourceBundleManifestSha256=$bundleHash;compiledSourcePins=$pins;sourceInputCount=$sourceRows.Count;inputHashes=$inputs;productionUpdated=$false;installed=$false;hardwareCalls=0;next='current backend/catalog qualification and real PackageSource positive/negative gates, then compare-and-swap promotion of payload and all consumers'})
Write-Output "FAN_CANDIDATE_PREPARED $out"