[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$HelperCandidateRoot,[Parameter(Mandatory=$true)][string]$HelperWorkspaceRoot,[Parameter(Mandatory=$true)][string]$HelperSnapshotRoot,[Parameter(Mandatory=$true)][string]$NativeWorkspaceRoot,[Parameter(Mandatory=$true)][string]$InstallRoot,[Parameter(Mandatory=$true)][string]$OutputRoot,[Parameter(Mandatory=$true)][string]$InventoryTemplatePath)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputRoot){throw 'Fresh lighting delivery output required'}
$project=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\');$helper=[IO.Path]::GetFullPath($HelperCandidateRoot).TrimEnd('\');$work=[IO.Path]::GetFullPath($HelperWorkspaceRoot).TrimEnd('\');$nativeRoot=[IO.Path]::GetFullPath($NativeWorkspaceRoot).TrimEnd('\');$install=[IO.Path]::GetFullPath($InstallRoot).TrimEnd('\');$out=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if($out.StartsWith($install+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Delivery output must be separate from installation'}
$bundle=Join-Path $out 'bundle';$stage=Join-Path $out 'stage';New-Item -ItemType Directory -Path $bundle,$stage -Force|Out-Null
function Save([string]$p,$d){[IO.File]::WriteAllText($p,($d|ConvertTo-Json -Depth 90),[Text.UTF8Encoding]::new($false))}
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
$script:records=[Collections.Generic.List[object]]::new();$script:copied=@{}
function Capture([string]$original,[string]$expected,[string[]]$alternatives=@()){
 $key=$original.ToLowerInvariant()+'|'+$expected
 if($script:copied.ContainsKey($key)){return $script:copied[$key]}
 $from=@(@($alternatives)+@($original)|Where-Object {(Test-Path -LiteralPath $_ -PathType Leaf) -and (Hash $_) -ceq $expected})|Select-Object -First 1
 if(-not $from){throw ('Exact recorded bytes missing: '+$original)}
 $rel='objects/'+$expected+'/'+[IO.Path]::GetFileName($original);$to=Join-Path $bundle $rel;New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force|Out-Null
 if(-not(Test-Path -LiteralPath $to)){Copy-Item -LiteralPath $from -Destination $to};if((Hash $to) -cne $expected){throw 'Copied evidence mismatch'}
 $script:records.Add([pscustomobject]@{originalPath=$original;sha256=$expected;path=$rel});$script:copied[$key]=$rel;return $rel
}
function Ref([string]$path){$sum=Hash $path;[pscustomobject]@{path=(Capture $path $sum);sha256=$sum}}
$r=Get-Content -LiteralPath (Join-Path $helper 'CANDIDATE-ACCEPTANCE.json') -Raw -Encoding UTF8|ConvertFrom-Json
$globalPath=Join-Path $work $r.globalResult.path;$signalPath=Join-Path $work $r.signalResult.path;$g=Get-Content -LiteralPath $globalPath -Raw -Encoding UTF8|ConvertFrom-Json;$s=Get-Content -LiteralPath $signalPath -Raw -Encoding UTF8|ConvertFrom-Json
if((Hash $globalPath) -cne $r.globalResult.sha256 -or (Hash $signalPath) -cne $r.signalResult.sha256){throw 'Original candidate proof changed'}
foreach($row in $r.sourceInputs){Capture (Join-Path $project $row.path) $row.sha256 @((Join-Path $HelperSnapshotRoot $row.path),(Join-Path (Split-Path -Parent $HelperSnapshotRoot) ('consumer-source-snapshot/'+$row.path)))|Out-Null}
foreach($row in $r.toolInputs){Capture (Join-Path $project $row.path) $row.sha256 @((Join-Path $work $row.snapshotPath))|Out-Null}
foreach($row in $g.results[0].sourceInputs){Capture (Join-Path $project $row.path) $row.sha256 @((Join-Path (Split-Path -Parent $HelperSnapshotRoot) ('consumer-source-snapshot/'+$row.path)),(Join-Path $HelperSnapshotRoot $row.path))|Out-Null}
foreach($row in $s.sourceHashes.PSObject.Properties){Capture (Join-Path $project $row.Name) $row.Value @((Join-Path (Split-Path -Parent $HelperSnapshotRoot) ('consumer-source-snapshot/'+$row.Name)),(Join-Path $HelperSnapshotRoot $row.Name))|Out-Null}
foreach($row in $g.results[0].candidateArtifacts){Capture $row.path $row.sha256|Out-Null}
$pdbPath=Join-Path $work $r.sourceProof.path;$pdb=Get-Content -LiteralPath $pdbPath -Raw -Encoding UTF8|ConvertFrom-Json
Capture (Join-Path $work $pdb.pdb.path) $pdb.pdb.sha256|Out-Null
foreach($row in $pdb.documents){$base=if($row.generated){$work}else{$project};Capture (Join-Path $base $row.path) $row.sha256 @((Join-Path $HelperSnapshotRoot $row.path),(Join-Path (Split-Path -Parent $HelperSnapshotRoot) ('consumer-source-snapshot/'+$row.path)))|Out-Null}
$nPath=Join-Path $nativeRoot 'native-candidate.json';$n=Get-Content -LiteralPath $nPath -Raw -Encoding UTF8|ConvertFrom-Json;$ns=Get-Content -LiteralPath $n.sourceManifest.path -Raw -Encoding UTF8|ConvertFrom-Json
foreach($row in $ns.files){Capture (Join-Path $n.sourceRoot $row.path) $row.sha256|Out-Null}
Capture $n.binary.path $n.binary.sha256|Out-Null;Capture $n.pdb.path $n.pdb.sha256|Out-Null
$consumerPath=Join-Path $nativeRoot 'native-consumer/result.json';$consumer=Get-Content -LiteralPath $consumerPath -Raw -Encoding UTF8|ConvertFrom-Json
foreach($ref in @($consumer.inputs,$consumer.compiledConsumer,$consumer.fixtureSource,$consumer.actualHelperBehavior)){Capture $ref.path $ref.sha256|Out-Null}
$nativePdbPath=Join-Path $nativeRoot 'pdb-proof/result.json';$np=Get-Content -LiteralPath $nativePdbPath -Raw -Encoding UTF8|ConvertFrom-Json
if($np.status -cne 'PASS' -or $consumer.status -cne 'PASS'){throw 'Native candidate not admitted'}
if((Hash (Join-Path $install 'YeManCC/YeManCC.exe')) -cne $n.binary.sha256){throw 'Target native must already be the matching private or separately installed candidate'}
foreach($row in $r.runtimeFiles){$from=Join-Path $work $row.path;if((Hash $from) -cne $row.sha256){throw 'Runtime source drift'};New-Item -ItemType Directory -Path (Join-Path $bundle 'runtime') -Force|Out-Null;Copy-Item -LiteralPath $from -Destination (Join-Path $bundle ('runtime/'+$row.name))}
$inventory=Get-Content -LiteralPath $InventoryTemplatePath -Raw -Encoding UTF8|ConvertFrom-Json
$paths=@(@($inventory.preserved)+@($inventory.changes)|ForEach-Object relative|Sort-Object -Unique)
$rows=@(foreach($rel in $paths){$p=Join-Path $install $rel;if(-not(Test-Path -LiteralPath $p -PathType Leaf)){throw ('Missing target product file '+$rel)};[pscustomobject]@{relative=$rel;sha256=(Hash $p)}})
$tupleFiles=[ordered]@{native='YeManCC/YeManCC.exe';core='PowerControl/light-setter/YeManLightDeviceCore.dll';hc='PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/HandheldCompanion.dll';runtimeManifest='PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/HandheldCompanion.runtime.json';fanHost='PowerControl/fan-host-v2/YeManFanHost.dll';fanBackend='PowerControl/fan-host-v2/YeManFanBackend.dll';fanManifest='PowerControl/fan-host-v2/YeManFanHost.payload.json'}
$tuple=[ordered]@{helper=$r.helperSha256;core=$r.coreSha256;catalog=$consumer.catalogSha256};$tupleRows=[ordered]@{}
foreach($key in $tupleFiles.Keys){$rel=$tupleFiles[$key];$sum=Hash (Join-Path $install $rel);$tuple[$key]=$sum;$tupleRows[$key]=@{relative=$rel;sha256=$sum}}
if($tuple.core -cne $r.coreSha256){throw 'Installed independent core mismatch'}
$m=[ordered]@{schemaVersion=1;kind='ymcc-lighting-build-delivery-v1';createdUtc=[DateTime]::UtcNow.ToString('o');hardwareIo=$false;originalProjectRoot=$project;originalHelperWorkspace=$work;helperApproval=(Ref (Join-Path $helper 'CANDIDATE-ACCEPTANCE.json'));helperSourceProof=(Ref $pdbPath);globalResult=(Ref $globalPath);signalResult=(Ref $signalPath);nativeBuild=(Ref $nPath);nativeSources=(Ref $n.sourceManifest.path);nativePdb=(Ref $nativePdbPath);nativeConsumer=(Ref $consumerPath);tuple=$tuple;tupleFiles=$tupleRows;installInventory=$rows;recordedFiles=@($script:records);members=@()}
$m.members=@(Get-ChildItem -LiteralPath $bundle -Recurse -File|ForEach-Object {[pscustomobject]@{path=$_.FullName.Substring($bundle.Length+1).Replace('\','/');sha256=(Hash $_.FullName);bytes=$_.Length}}|Sort-Object path)
$manifest=Join-Path $bundle 'LIGHTING-BUILD-DELIVERY.json';Save $manifest $m
$helperRel='PowerControl/light-setter/YeManLightSetter.dll';$exportRel='YeManCC/export-identity.json'
foreach($rel in @($helperRel,$exportRel)){New-Item -ItemType Directory -Path (Split-Path -Parent (Join-Path $stage $rel)) -Force|Out-Null}
Copy-Item -LiteralPath (Join-Path $bundle 'runtime/YeManLightSetter.dll') -Destination (Join-Path $stage $helperRel)
if(-not(Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')){throw 'Lighting delivery generation requires PowerShell Core with literal JSON date support'}
$identity=Get-Content -LiteralPath (Join-Path $install $exportRel) -Raw -Encoding UTF8|ConvertFrom-Json -DateKind String
$entry=@($identity.payload|Where-Object path -CEQ $helperRel);if($entry.Count -ne 1){throw 'Missing exact export helper row'};$entry[0].sha256=$r.helperSha256;if($entry[0].PSObject.Properties['bytes']){$entry[0].bytes=(Get-Item -LiteralPath (Join-Path $stage $helperRel)).Length};if($entry[0].PSObject.Properties['size']){$entry[0].size=(Get-Item -LiteralPath (Join-Path $stage $helperRel)).Length}
Save (Join-Path $stage $exportRel) $identity
$plan=[ordered]@{kind='lighting-build-v4';installRoot=$install;stageRoot=$stage;evidenceDir=(Join-Path $out 'connection');newHelperSha256=$r.helperSha256;preservedNativeSha256=$tuple.native;preservedCoreSha256=$tuple.core;buildDelivery=@{path=$manifest;sha256=(Hash $manifest)};changes=@(foreach($rel in @($helperRel,$exportRel)){$p=Join-Path $install $rel;@{relative=$rel;sourceRelative=$rel;beforeSha256=(Hash $p);afterSha256=(Hash (Join-Path $stage $rel));beforeSddl=(Get-Acl -LiteralPath $p).Sddl}});preserved=@(foreach($row in $rows|Where-Object relative -CNotIn @($helperRel,$exportRel)){$p=Join-Path $install $row.relative;@{relative=$row.relative;sha256=$row.sha256;sddl=(Get-Acl -LiteralPath $p).Sddl}})}
Save (Join-Path $out 'plan.json') $plan
. (Join-Path $PSScriptRoot 'lighting-build-delivery.ps1');$result=Assert-YmccLightingBuildDelivery -Plan ([pscustomobject]$plan)
Save (Join-Path $out 'admission.json') $result;Write-Output ('LIGHTING_BUILD_DELIVERY_READY '+$result.helperSha256)
