[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [string]$RepositoryRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$BaselineRuntime='D:\Codex\2026-10-18\kai-s\work\hc-followup-trim\application\leaf-v3\input',
 [string]$ApplicationInspector='D:\Codex\2026-10-18\kai-s\work\hc-followup-trim\application\tool-build\bin\ApplicationTrim\release\ApplicationTrim.dll'
)
$ErrorActionPreference='Stop'
$out=[IO.Path]::GetFullPath($EvidenceDirectory);if(Test-Path -LiteralPath $out){throw 'Evidence directory must be new'};New-Item -ItemType Directory -Path $out|Out-Null
$repo=[IO.Path]::GetFullPath($RepositoryRoot);$original=[IO.Path]::GetFullPath($BaselineRuntime);$current=Join-Path $repo 'PowerControl\handheldcompanion-runtime\YMCC-DEVICECORE-20261018-v1'
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function WriteJson([string]$p,$v){[IO.File]::WriteAllText($p,(ConvertTo-Json -InputObject $v -Depth 16),[Text.UTF8Encoding]::new($false))}
function Freeze([string]$source,[string]$destination){
 $m=Get-Content -LiteralPath (Join-Path $source 'HandheldCompanion.runtime.json') -Raw -Encoding UTF8|ConvertFrom-Json
 New-Item -ItemType Directory -Path $destination|Out-Null
 foreach($f in $m.files){$p=Join-Path $source $f.path;if((Hash $p)-ne $f.sha256){throw "Source drift: $($f.path)"};$target=Join-Path $destination $f.path;New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force|Out-Null;Copy-Item -LiteralPath $p -Destination $target}
 Copy-Item -LiteralPath (Join-Path $source 'HandheldCompanion.runtime.json') -Destination $destination
}
if((Hash (Join-Path $original 'HandheldCompanion.runtime.json'))-ne '65a3ed817b38ba36ef974d9c005bd4ca7057b222efa920d25a4fa16e74b2eb18'){throw 'Original 109-file baseline required'}
$inputRoot=Join-Path $out 'original-input';Freeze $original $inputRoot
$currentRoot=Join-Path $out 'current-input';Freeze $current $currentRoot
$shared=Join-Path $repo 'deps\ymcc-shared-runtime';$sharedManifest=Get-Content -LiteralPath (Join-Path $shared 'manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
$sharedAdded=@();foreach($f in $sharedManifest.files){$p=Join-Path $shared $f.path;if((Hash $p)-ne $f.sha256){throw 'Shared input drift'};$dest=Join-Path $currentRoot ([IO.Path]::GetFileName($f.path));if(Test-Path -LiteralPath $dest){if((Hash $dest)-ne $f.sha256){throw 'Local/shared collision'}}else{Copy-Item -LiteralPath $p -Destination $dest};$sharedAdded+=[ordered]@{path=[IO.Path]::GetFileName($f.path);sha256=$f.sha256}}
WriteJson (Join-Path $out 'INPUTS.json') ([ordered]@{schemaVersion=1;originalManifestSha256=(Hash (Join-Path $inputRoot 'HandheldCompanion.runtime.json'));currentManifestSha256=(Hash (Join-Path $currentRoot 'HandheldCompanion.runtime.json'));sharedAuthorityManifestSha256=(Hash (Join-Path $shared 'manifest.json'));sharedOriginalsAddedForClosure=$sharedAdded;sourceCurrent=$current;sourceOriginal=$original;noProductionWrites=$true;applicationInspectorSha256=(Hash $ApplicationInspector)})
$feed=Join-Path $out 'empty-offline-feed';New-Item -ItemType Directory -Path $feed|Out-Null
$build=Join-Path $out 'tool-build';& dotnet build (Join-Path $PSScriptRoot 'TransportSurface.csproj') -c Release --artifacts-path $build --source $feed -v quiet
if($LASTEXITCODE -ne 0){throw 'Transport tool build failed'}
$tool=Join-Path $build 'bin\TransportSurface\release\TransportSurface.dll'
& dotnet $tool inventory $inputRoot (Join-Path $out 'original-inventory.json');if($LASTEXITCODE-ne 0){throw 'Inventory failed'}
& dotnet $tool inventory $currentRoot (Join-Path $out 'current-inventory.json');if($LASTEXITCODE-ne 0){throw 'Current inventory failed'}
$inventory=Get-Content -LiteralPath (Join-Path $out 'original-inventory.json') -Raw -Encoding UTF8|ConvertFrom-Json
$targets=@('controller-hidapi.net.dll','hidapi.net.dll','Nefarius.Utilities.Bluetooth.dll','Nefarius.ViGEm.Client.dll','PInvoke.Windows.Core.dll','Resources/RTSS/RTSSSharedMemoryNET.dll')
$linker=Join-Path $env:USERPROFILE '.nuget\packages\microsoft.net.illink.tasks\10.0.11\tools\net\illink.dll';$dotnet=Split-Path -Parent (Get-Command dotnet).Source;$core=Join-Path $dotnet 'shared\Microsoft.NETCore.App\10.0.11';$desktop=Join-Path $dotnet 'shared\Microsoft.WindowsDesktop.App\10.0.11'
$results=@()
foreach($rel in $targets){
 $assembly=@($inventory.assemblies|Where-Object {$_.path.Replace('\','/') -eq $rel});if($assembly.Count-ne 1){throw "Missing target $rel"};$name=$assembly[0].name
 $experiment=Join-Path $out $name;New-Item -ItemType Directory -Path $experiment|Out-Null;$source=Join-Path $inputRoot $rel;$linked=Join-Path $experiment 'linked'
 $rootXml=Join-Path $experiment 'native-and-lifecycle-roots.xml';& dotnet $tool roots $source $rootXml;if($LASTEXITCODE-ne 0){throw "Root generation failed: $name"}
 $arguments=@('-d',$inputRoot,'-d',(Join-Path $inputRoot 'Resources\RTSS'),'-d',$core,'-d',$desktop,'-out',$linked,'-x',$rootXml,'--action','copyused','--trim-mode','copyused','--action','link',$name,'--skip-unresolved','true','--deterministic','--keep-metadata','all','--strip-security','false','--keep-dep-attributes','true')
 foreach($opt in @('unreachablebodies','overrideremoval','unusedtypechecks','unusedinterfaces','beforefieldinit','ipconstprop','substitutefeatureguards')){$arguments+=@('--disable-opt',$opt)}
 foreach($a in $inventory.assemblies){if($a.name -ne $name){$arguments+=@('-a',(Join-Path $inputRoot $a.path),'all','--action','copy',$a.name)}}
 foreach($base in @($core,$desktop)){foreach($file in Get-ChildItem -LiteralPath $base -File -Filter '*.dll'){$arguments+=@('--action','skip',$file.BaseName)}}
 foreach($file in @('WindowsBase.dll','PresentationCore.dll','PresentationFramework.dll','System.Windows.Forms.dll')){$arguments+=@('-reference',(Join-Path $desktop $file))}
 WriteJson (Join-Path $experiment 'linker-arguments.json') $arguments
 & dotnet $linker @arguments *> (Join-Path $experiment 'linker.log');$linkExit=$LASTEXITCODE
 $candidate=Join-Path $linked ([IO.Path]::GetFileName($rel));$row=[ordered]@{path=$rel;assembly=$name;originalSha256=(Hash $source);originalBytes=(Get-Item -LiteralPath $source).Length;ilOnly=$assembly[0].ilOnly;allOtherManagedAssembliesRooted=$inventory.assemblies.Count-1;allOriginalAbiAndPInvokeTypesRooted=$true;allLifecycleMethodsRooted=$true;linkerExitCode=$linkExit;candidateExists=(Test-Path -LiteralPath $candidate);nativeExecuted=$false;productionModified=$false;promotionApproved=$false}
 if($linkExit -eq 0 -and (Test-Path -LiteralPath $candidate)){
  $row.candidateBytes=(Get-Item -LiteralPath $candidate).Length;$row.candidateSha256=Hash $candidate;$row.savingBytes=$row.originalBytes-$row.candidateBytes
  & dotnet $tool nativeverify $source $candidate (Join-Path $experiment 'native-surface.json') *> (Join-Path $experiment 'native-surface.log');$row.nativeSurfaceExitCode=$LASTEXITCODE
  $oneTarget=Join-Path $experiment 'target.json';WriteJson $oneTarget @($name)
  # Read-only reuse of the existing inspector. Only a single reviewed target file is overlaid.
  if($rel -notlike 'Resources/*'){
   $overlay=Join-Path $experiment 'current-overlay';New-Item -ItemType Directory -Path $overlay|Out-Null
   foreach($f in Get-ChildItem -LiteralPath $currentRoot -Recurse -File){$r=[IO.Path]::GetRelativePath($currentRoot,$f.FullName);$p=Join-Path $overlay $r;New-Item -ItemType Directory -Path (Split-Path -Parent $p) -Force|Out-Null;Copy-Item -LiteralPath $f.FullName -Destination $p}
   $currentTarget=Join-Path $overlay $rel;if((Hash $currentTarget)-ne $row.originalSha256){throw "Current target differs from original: $rel"};Copy-Item -LiteralPath $candidate -Destination $currentTarget -Force
   & dotnet $ApplicationInspector contracts $overlay $oneTarget (Join-Path $experiment 'contracts-current.json') *> (Join-Path $experiment 'contracts-current.log');$row.currentContractExitCode=$LASTEXITCODE
   & dotnet $ApplicationInspector leafverify $inputRoot $overlay $oneTarget (Join-Path $experiment 'leaf-IL.json') *> (Join-Path $experiment 'leaf-IL.log');$row.leafIlExitCode=$LASTEXITCODE
   $row.overlay=$overlay
  }
 }
 WriteJson (Join-Path $experiment 'RESULT.json') $row;$results+=$row
 Write-Output "TRANSPORT_ATTEMPT: $name linker=$linkExit candidate=$($row.candidateExists)"
}
WriteJson (Join-Path $out 'ATTEMPTS.json') ([ordered]@{schemaVersion=1;status='six actual linker attempts complete';results=$results;noHardwareExecuted=$true;noProductionModified=$true})
