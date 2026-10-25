[CmdletBinding()]
param([string]$EvidenceRoot='D:/Codex/2026-10-18/kai-s/work/hc-third-trim/winrt',[ValidateSet('guarded','guarded-v2','full-dynamic','sdk-data-only')][string]$Mode='guarded',[string]$AttemptName='')
$ErrorActionPreference='Stop'
if([string]::IsNullOrWhiteSpace($AttemptName)){$AttemptName=$Mode}
$runtime=Join-Path $EvidenceRoot 'baseline/runtime';$shared=Join-Path $EvidenceRoot 'baseline/shared/bin'
$tool=Join-Path $EvidenceRoot 'build/bin/WinrtSecondPass/release/WinrtSecondPass.dll'
$descriptor=Join-Path $EvidenceRoot "roots-$Mode.xml"
& dotnet $tool roots $runtime $shared $Mode $descriptor
if($LASTEXITCODE){throw 'Root generation failed'}
$illink=Join-Path $env:USERPROFILE '.nuget/packages/microsoft.net.illink.tasks/10.0.11/tools/net/illink.dll'
$core='C:/Program Files/dotnet/shared/Microsoft.NETCore.App/10.0.11';$desktop='C:/Program Files/dotnet/shared/Microsoft.WindowsDesktop.App/10.0.11'
$linked=Join-Path $EvidenceRoot "attempt-$AttemptName/linked"
New-Item -ItemType Directory -Path (Split-Path -Parent $linked) -Force | Out-Null
$linkArgs=@('-d',$runtime,'-d',$shared,'-d',$core,'-d',$desktop,'-out',$linked,'-x',$descriptor,'--action','copy','--trim-mode','copy','--skip-unresolved','true','--deterministic','--keep-metadata','all','--strip-security','false','--keep-dep-attributes','true','--ignore-substitutions','--strip-descriptors','false','--strip-substitutions','false','--strip-link-attributes','false')
foreach($opt in @('unreachablebodies','overrideremoval','unusedtypechecks','unusedinterfaces','beforefieldinit','ipconstprop','substitutefeatureguards')){$linkArgs+=@('--disable-opt',$opt)}
foreach($dir in @($runtime,$shared)) {foreach($file in Get-ChildItem -LiteralPath $dir -File -Recurse -Filter '*.dll') {
 try{[Reflection.AssemblyName]::GetAssemblyName($file.FullName)|Out-Null}catch{continue}
 if($file.BaseName -notin @('Microsoft.Windows.SDK.NET','WinRT.Runtime')){$linkArgs+=@('-a',$file.FullName,'all','--action','copy',$file.BaseName)}
}}
foreach($name in @('Microsoft.Windows.SDK.NET','WinRT.Runtime')){$linkArgs+=@('--action',$(if($Mode -eq 'sdk-data-only' -and $name -eq 'WinRT.Runtime'){'copy'}else{'link'}),$name)}
foreach($dir in @($core,$desktop)){foreach($file in Get-ChildItem -LiteralPath $dir -File -Recurse -Filter '*.dll'){$linkArgs+=@('--action','skip',$file.BaseName)}}
foreach($name in @('WindowsBase.dll','PresentationCore.dll','PresentationFramework.dll','System.Windows.Forms.dll')){$linkArgs+=@('-reference',(Join-Path $desktop $name))}
$linkArgs|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceRoot "attempt-$AttemptName/arguments.json") -Encoding utf8
$responseFile=Join-Path $EvidenceRoot "attempt-$AttemptName/linker.rsp"
@($linkArgs|ForEach-Object {'"'+($_ -replace '"','\"')+'"'})|Set-Content -LiteralPath $responseFile -Encoding utf8
& dotnet $illink ('@'+$responseFile) *> (Join-Path $EvidenceRoot "attempt-$AttemptName/linker.log")
$exit=$LASTEXITCODE
@{mode=$Mode;exitCode=$exit;skipUnresolved=$true;immutableConsumersRootedInFull=$true;productionModified=$false;promotionApproved=$false;descriptor=$descriptor;linkerSha256=(Get-FileHash -LiteralPath $illink -Algorithm SHA256).Hash.ToLowerInvariant()}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceRoot "attempt-$AttemptName/RESULT.json") -Encoding utf8
if($exit){throw "Linker failed ($exit); see retained log"}
$overlay=Join-Path $EvidenceRoot "attempt-$AttemptName/overlay"
New-Item -ItemType Directory -Path $overlay -Force | Out-Null

# Wildcards are deliberately used only for a bounded source copy, never for deletion.
Copy-Item -Path "$runtime/*" -Destination $overlay -Recurse -Force
foreach($name in @('Microsoft.Windows.SDK.NET.dll','WinRT.Runtime.dll')){if($Mode -eq 'sdk-data-only' -and $name -eq 'WinRT.Runtime.dll'){continue};Copy-Item -LiteralPath (Join-Path $linked $name) -Destination (Join-Path $overlay $name) -Force}
Write-Output "Candidate built at $overlay"





