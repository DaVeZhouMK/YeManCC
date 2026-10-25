[CmdletBinding()]
param(
 [string]$RepositoryRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [ValidateSet('leaf','devices')][string]$Mode = 'leaf',
 [ValidateSet('focused','extended')][string]$Profile = 'focused',
 [string]$InputRuntimeRoot = ''
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath($RepositoryRoot)
$source=if([string]::IsNullOrWhiteSpace($InputRuntimeRoot)){Join-Path $repo 'PowerControl\handheldcompanion-runtime\YMCC-DEVICECORE-20261018-v1'}else{[IO.Path]::GetFullPath($InputRuntimeRoot)}
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Output must be new; stale linked files cannot satisfy ABI gates'}
New-Item -ItemType Directory -Path $out | Out-Null
$inputRoot=Join-Path $out 'input'
New-Item -ItemType Directory -Path $inputRoot -Force | Out-Null
$manifestPath=Join-Path $source 'HandheldCompanion.runtime.json'
if((Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne '65a3ed817b38ba36ef974d9c005bd4ca7057b222efa920d25a4fa16e74b2eb18'){throw 'This experiment is locked to the original 109-file snapshot; use -InputRuntimeRoot with that preserved snapshot'}
$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
foreach($file in $manifest.files){
 $p=Join-Path $source $file.path
 $actual=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()
 if($actual -ne $file.sha256){throw "Input manifest mismatch: $($file.path)"}
 $target=Join-Path $inputRoot $file.path
 New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
 Copy-Item -LiteralPath $p -Destination $target -Force
}
Copy-Item -LiteralPath (Join-Path $source 'HandheldCompanion.runtime.json') -Destination $inputRoot -Force
$hcHash=(Get-FileHash -LiteralPath (Join-Path $inputRoot 'HandheldCompanion.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
if($hcHash -ne '08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583'){throw 'Unreviewed HC input'}
$artifacts=Join-Path $out 'tool-build'
& dotnet build (Join-Path $PSScriptRoot 'ApplicationTrim.csproj') -c Release --artifacts-path $artifacts -v quiet
if($LASTEXITCODE -ne 0){throw 'Tool build failed'}
$tool=Join-Path $artifacts 'bin\ApplicationTrim\release\ApplicationTrim.dll'
& dotnet $tool roots $inputRoot $repo (Join-Path $out 'device-roots.xml')
if($LASTEXITCODE -ne 0){throw 'Root discovery failed'}
# Discover ABI owners from the complete immutable pre-link input, never from linker output.
$comAudit=Join-Path $out 'com-audit.json';$comRoots=Join-Path $out 'com-roots.xml'
& dotnet $tool auditcom $inputRoot $comAudit $comRoots
if($LASTEXITCODE -ne 0){throw 'COM/interop inventory failed'}
$com=Get-Content -LiteralPath $comAudit -Raw -Encoding UTF8 | ConvertFrom-Json
$protected=@($com.protectedAssemblies | ForEach-Object {$_.name})
$linker=Join-Path $env:USERPROFILE '.nuget\packages\microsoft.net.illink.tasks\10.0.11\tools\net\illink.dll'
$dotnet=Split-Path -Parent (Get-Command dotnet).Source
$core=Join-Path $dotnet 'shared\Microsoft.NETCore.App\10.0.11'
$desktop=Join-Path $dotnet 'shared\Microsoft.WindowsDesktop.App\10.0.11'
$link=Join-Path $out "linked-$Mode"
$targetPattern='^(Sentry|NAudio(\.|$)|NVorbis$|Microsoft\.Win32\.TaskScheduler$|Microsoft\.WindowsAPICodePack|SharpDX|GongSolutions\.|IGDB$|craftersmine\.|RestEase|Polly|GameLib\.|YamlDotNet$|Microsoft\.Extensions\.Http|Interop\.IWshRuntimeLibrary$|BlackSharp\.)'
if($Profile -eq 'extended'){$targetPattern='^(Sentry|NAudio(\.|$)|NVorbis$|Microsoft\.Win32\.TaskScheduler$|Microsoft\.WindowsAPICodePack|SharpDX|GongSolutions\.|IGDB$|craftersmine\.|RestEase|Polly|GameLib\.|YamlDotNet$|Microsoft\.Extensions\.|Interop\.IWshRuntimeLibrary$|BlackSharp\.|Serilog|Gma\.|GregsStack\.|WindowsInput$|PrecisionTimer\.|protobuf-net|ricaun\.|WpfScreenHelper$|Reloaded\.Memory$)'}
$sharedExcluded=@('Microsoft.Extensions.DependencyInjection.Abstractions','Microsoft.Extensions.Logging.Abstractions')
$targets=@(Get-ChildItem -LiteralPath $inputRoot -File -Filter '*.dll' | Where-Object {$_.BaseName -match $targetPattern -and $_.BaseName -notin $sharedExcluded} | ForEach-Object {$_.BaseName})
$argsList=@('-x',$comRoots,'-d',$inputRoot,'-d',$core,'-d',$desktop,'-out',$link,'--action','copyused','--trim-mode','copyused','--skip-unresolved','true','--deterministic','--keep-metadata','all','--strip-security','false','--keep-dep-attributes','true','--dump-dependencies','--dependencies-file',(Join-Path $out "linker-dependencies-$Mode.xml.gz"))
foreach($opt in @('unreachablebodies','overrideremoval','unusedtypechecks','unusedinterfaces','beforefieldinit','ipconstprop','substitutefeatureguards')){$argsList+=@('--disable-opt',$opt)}
if($Mode -eq 'leaf'){$argsList+=@('-a',(Join-Path $inputRoot 'HandheldCompanion.dll'),'all','-a',(Join-Path $inputRoot 'Shared.dll'),'all','--action','copy','HandheldCompanion','--action','copy','Shared')}
else{$argsList+=@('-x',(Join-Path $out 'device-roots.xml'),'--action','link','HandheldCompanion','--action','link','Shared')}
if($Mode -eq 'leaf') {
 # Unchanged libraries remain shipped and may hold additional API consumers. Root all of them,
 # including shared authority copies, rather than trusting only the linker's rewritten scratch files.
 foreach($file in Get-ChildItem -LiteralPath $inputRoot -File -Filter '*.dll') {
  if($targets -contains $file.BaseName -or $file.BaseName -in @('HandheldCompanion','Shared')){continue}
  try{$identity=[Reflection.AssemblyName]::GetAssemblyName($file.FullName)}catch [BadImageFormatException]{continue}
  $argsList+=@('-a',$file.FullName,'all','--action','copy',$identity.Name)
 }
}
if($Profile -eq 'extended'){
 # Reflection-heavy JSON/COM/config libraries keep their complete type/member surface in this experiment.
 # This is an explicit failed/small-gain attempt rather than silently dropping reflected models.
 $leafRoots=New-Object System.Xml.XmlDocument
 $linkerElement=$leafRoots.CreateElement('linker');$leafRoots.AppendChild($linkerElement)|Out-Null
 foreach($name in $targets){
  if($name -match '^(Sentry$|Serilog|protobuf-net|IGDB$|craftersmine\.|RestEase$|Microsoft\.Win32\.TaskScheduler$|ricaun\.|Microsoft\.Extensions\.(Configuration|Options|DependencyModel|Logging$))'){
   $argsList+=@('-a',(Join-Path $inputRoot ($name+'.dll')),'all')
   $assemblyElement=$leafRoots.CreateElement('assembly');$assemblyElement.SetAttribute('fullname',$name);$linkerElement.AppendChild($assemblyElement)|Out-Null
   $typeElement=$leafRoots.CreateElement('type');$typeElement.SetAttribute('fullname','*');$typeElement.SetAttribute('preserve','all');$assemblyElement.AppendChild($typeElement)|Out-Null
  }
 }
 $descriptor=Join-Path $out 'leaf-reflection-roots.xml';$leafRoots.Save($descriptor);$argsList+=@('-x',$descriptor)
}
# Last action wins: ABI assemblies are always whole-copy roots, in either experiment mode.
foreach($name in $targets){if($name -notin $protected){$argsList+=@('--action','link',$name)}}
foreach($name in $protected){
 $original=@($com.protectedAssemblies | Where-Object name -eq $name)[0]
 $argsList+=@('-a',[string]$original.path,'all','--action','copy',$name)
}
foreach($base in @($core,$desktop)){foreach($file in Get-ChildItem -LiteralPath $base -File -Filter '*.dll'){$argsList+=@('--action','skip',$file.BaseName)}}
foreach($name in @('WindowsBase.dll','PresentationCore.dll','PresentationFramework.dll','System.Windows.Forms.dll')){$argsList+=@('-reference',(Join-Path $desktop $name))}
[IO.File]::WriteAllText((Join-Path $out "linker-arguments-$Mode.json"),($argsList|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
& dotnet $linker @argsList *> (Join-Path $out "linker-$Mode.log")
$exit=$LASTEXITCODE
# ILLink emits plain XML even when the requested file ends in .gz. Preserve the complete trace,
# but compress it after the linker has terminated so experiments do not retain hundreds of MB each.
$dependencyPath=Join-Path $out "linker-dependencies-$Mode.xml.gz"
if(Test-Path -LiteralPath $dependencyPath){
 $reader=[IO.File]::OpenRead($dependencyPath);try{$a=$reader.ReadByte();$b=$reader.ReadByte()}finally{$reader.Dispose()}
 if($a -ne 31 -or $b -ne 139){
  $packed=$dependencyPath+'.packed';$inputStream=[IO.File]::OpenRead($dependencyPath);$outputStream=[IO.File]::Create($packed)
  $gzip=[IO.Compression.GZipStream]::new($outputStream,[IO.Compression.CompressionLevel]::Fastest)
  try{$inputStream.CopyTo($gzip)}finally{$gzip.Dispose();$outputStream.Dispose();$inputStream.Dispose()}
  Move-Item -LiteralPath $packed -Destination $dependencyPath -Force
 }
}
$record=[ordered]@{schemaVersion=1;mode=$Mode;profile=$Profile;linkerExitCode=$exit;hcInputSha256=$hcHash;inputManifestSha256=(Get-FileHash -LiteralPath (Join-Path $inputRoot 'HandheldCompanion.runtime.json')).Hash.ToLowerInvariant();targetAssemblies=$targets;hardwareAccess=$false;productionModified=$false;unresolvedAllowedForExperiment=$true;promotionApproved=$false;comProtectionRequired=$true;comVerificationPassed=$false;comAuditSha256=(Get-FileHash -LiteralPath $comAudit -Algorithm SHA256).Hash.ToLowerInvariant();comProtectedAssemblies=$protected;comExternalDependencies=@($com.unavailableDependencies)}
if(Test-Path -LiteralPath $link){$record.files=@(Get-ChildItem -LiteralPath $link -File | ForEach-Object {$before=Join-Path $inputRoot $_.Name;[ordered]@{name=$_.Name;beforeBytes=if(Test-Path -LiteralPath $before){(Get-Item -LiteralPath $before).Length}else{$null};afterBytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})}
[IO.File]::WriteAllText((Join-Path $out "candidate-$Mode.json"),($record|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
if($exit -ne 0){throw "Candidate linker failed: $exit (recorded)"}
$comVerify=Join-Path $out "com-verify-$Mode.json"
& dotnet $tool verifycom $inputRoot $link $comVerify
$comExit=$LASTEXITCODE
$record.comVerificationPassed=($comExit -eq 0)
$record.comVerificationSha256=(Get-FileHash -LiteralPath $comVerify -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $out "candidate-$Mode.json"),($record|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
if($comExit -ne 0){throw 'COM/interop whole-assembly preservation failed; candidate rejected'}
& dotnet $tool verify (Join-Path $inputRoot 'HandheldCompanion.dll') (Join-Path $link 'HandheldCompanion.dll') (Join-Path $out "device-IL-$Mode.json")
if($LASTEXITCODE -ne 0){throw 'Device IL preservation failed'}
Write-Output "CANDIDATE_ONLY: $Mode $link"

