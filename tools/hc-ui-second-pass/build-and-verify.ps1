[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$RuntimeRoot,
 [Parameter(Mandatory=$true)][string]$SigningKeyPath,
 [Parameter(Mandatory=$true)][string]$OfflinePackageRoot,
 [Parameter(Mandatory=$true)][string]$CallbackGoldenPath,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [string]$BaselineRun
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$taskSource=$PSScriptRoot
$taskRepo=Split-Path (Split-Path $taskSource -Parent) -Parent
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if((Test-Path -LiteralPath $taskOutput) -and @(Get-ChildItem -LiteralPath $taskOutput -Force).Count -gt 0){throw 'Output must be absent or empty'}
New-Item -ItemType Directory -Force -Path $taskOutput,(Join-Path $taskOutput 'logs') | Out-Null
$env:NuGetAudit='false'
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save([string]$Path,$Value){$Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $Path -Encoding utf8}
function LocalPath([string]$Root,[string]$Relative){
 $prefix=[IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar
 $path=[IO.Path]::GetFullPath((Join-Path $Root $Relative));if(-not $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Escaping path'};$path
}
function CopyRuntime([string]$From,[string]$To){
 if(Test-Path -LiteralPath $To){throw 'Destination exists'}
 New-Item -ItemType Directory -Path $To | Out-Null
 $mp=Join-Path $From 'HandheldCompanion.runtime.json';$m=Get-Content -LiteralPath $mp -Raw | ConvertFrom-Json
 foreach($f in $m.files){$source=LocalPath $From $f.path;if((Hash $source) -ne $f.sha256){throw ('Hash mismatch: '+$f.path)};if((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse file'};$dest=LocalPath $To $f.path;New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null;Copy-Item -LiteralPath $source -Destination $dest}
 Copy-Item -LiteralPath $mp -Destination (Join-Path $To 'HandheldCompanion.runtime.json')
}
function UpdateManifest([string]$Root){
 $path=Join-Path $Root 'HandheldCompanion.runtime.json';$m=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
 foreach($f in $m.files){$local=LocalPath $Root $f.path;$f.sha256=Hash $local;if($f.PSObject.Properties.Name -contains 'bytes'){$f.bytes=(Get-Item -LiteralPath $local).Length}}
 $m | Add-Member -NotePropertyName uiSecondPassCandidateOnly -NotePropertyValue $true -Force;Save $path $m
}
function Run([string[]]$Arguments,[string]$Name,[int]$ExpectedExit=0){
 $text=& dotnet @Arguments 2>&1;$code=$LASTEXITCODE;$text | Out-File -LiteralPath (Join-Path $taskOutput ('logs/'+$Name+'.log')) -Encoding utf8
 if($code -ne $ExpectedExit){$text | Write-Output;throw ('Failed '+$Name+' exit '+$code)}
}
$taskReceipts=[Collections.Generic.List[string]]::new()
function Checked([string[]]$Arguments,[string]$Name){Run $Arguments $Name;$r=Get-Content -LiteralPath (Join-Path $taskOutput ($Name+'.json')) -Raw | ConvertFrom-Json;if($r.Status -ne 'passed'){throw ('Failed receipt '+$Name)};$taskReceipts.Add($Name+'.json')}
if(-not $BaselineRun){
 $BaselineRun=Join-Path $taskOutput 'baseline-repro'
 & (Join-Path $taskRepo 'tools/hc-ui-dependency-trim/build-and-verify.ps1') -RuntimeRoot $RuntimeRoot -SigningKeyPath $SigningKeyPath -OfflinePackageRoot $OfflinePackageRoot -CallbackGoldenPath $CallbackGoldenPath -OutputDirectory $BaselineRun | Out-File -LiteralPath (Join-Path $taskOutput 'logs/baseline-repro.log') -Encoding utf8
}
$taskBaseline=[IO.Path]::GetFullPath($BaselineRun)
$taskBaseSummary=Get-Content -LiteralPath (Join-Path $taskBaseline 'verification.json') -Raw | ConvertFrom-Json
if($taskBaseSummary.status -ne 'passed' -or $taskBaseSummary.candidateBytes -ne 825344){throw 'Baseline verification does not match approved pass'}
foreach($pin in (Get-Content -LiteralPath (Join-Path $taskBaseline 'source-pins.json') -Raw | ConvertFrom-Json)){if((Hash $pin.path) -ne $pin.sha256){throw 'Baseline source pin drift'}}
$taskBefore=Join-Path $taskBaseline 'runtime';$taskOriginal=Join-Path $taskBaseline 'input'
$taskPins=@{'iNKORE.UI.WPF.Modern.dll'='66ed45707f25d70d372d501efc74aa70124af60f21d1a0de201412c25d4d11f0';'iNKORE.UI.WPF.Modern.Controls.dll'='cc8eac8d44009e4fcb433c769564864d6108ec18a441981bfd5a5f4761862612'}
foreach($name in $taskPins.Keys){if((Hash (Join-Path $taskBefore $name)) -ne $taskPins[$name]){throw 'Approved UI baseline SHA mismatch'}}
$taskSourcePins=@(Get-ChildItem -LiteralPath $taskSource -Recurse -File | ForEach-Object {[ordered]@{path=$_.FullName;sha256=Hash $_.FullName}})
$taskLinked=@('tools/hc-devicecore-trim/Common.cs','tools/hc-devicecore-trim/DevelopmentSigning.cs','tools/hc-devicecore-trim/PeCompactor.cs','tools/hc-ui-dependency-trim/probes/Shared/StrictContext.cs')
$taskSourcePins+=@($taskLinked | ForEach-Object {$p=Join-Path $taskRepo $_;[ordered]@{path=$p;sha256=Hash $p}})
Save (Join-Path $taskOutput 'source-pins.json') $taskSourcePins
Run @('build',(Join-Path $taskSource 'HcUiSecondPass.csproj'),('-p:BaseIntermediateOutputPath='+(Join-Path $taskOutput 'tool-build/obj/')),('-p:OutputPath='+(Join-Path $taskOutput 'tool-build/bin/')),'-p:NuGetAudit=false','--source',$OfflinePackageRoot,'--nologo','-v:q') 'build-tool'
$taskTool=Join-Path $taskOutput 'tool-build/bin/HcUiSecondPass.dll'
$taskReplacement=Join-Path $taskOutput 'replacements'
Run @($taskTool,'build',$taskBefore,$SigningKeyPath,$taskReplacement) 'build-candidate'
$taskRuntime=Join-Path $taskOutput 'runtime';CopyRuntime $taskBefore $taskRuntime
foreach($name in $taskPins.Keys){Copy-Item -LiteralPath (Join-Path $taskReplacement $name) -Destination (Join-Path $taskRuntime $name) -Force}
UpdateManifest $taskRuntime
Checked @($taskTool,'audit',$taskBefore,$taskRuntime,(Join-Path $taskOutput 'audit.json')) 'audit'
$taskHeadless=Join-Path $taskBaseline 'probe-build/UiHeadlessProbe/bin/UiHeadlessProbe.dll'
$taskGlyph=Join-Path $taskBaseline 'probe-build/SettingsGlyphProbe/bin/SettingsGlyphProbe.dll'
$taskTables=Join-Path $taskBaseline 'probe-build/UiTableProbe/bin/UiTableProbe.dll'
$taskCallback=Join-Path $taskBaseline 'probe-build/CallbackPathProbe/bin/CallbackPathProbe.dll'
Checked @($taskHeadless,$taskRuntime,'538ea1f68f629a04',(Join-Path $taskOutput 'candidate-headless.json')) 'candidate-headless'
Checked @($taskGlyph,$taskRuntime,(Join-Path $taskOutput 'candidate-glyph.json'),'62127') 'candidate-glyph'
Checked @($taskTables,'compare',$taskRuntime,(Join-Path $taskBaseline 'baseline-tables.json'),(Join-Path $taskOutput 'candidate-tables.json')) 'candidate-tables'
foreach($entry in @(@{Root=$taskOriginal;Name='original-callbacks'},@{Root=$taskBefore;Name='baseline-callbacks'},@{Root=$taskRuntime;Name='candidate-callbacks'})){
 Checked @($taskCallback,(Join-Path $entry.Root 'HandheldCompanion.dll'),(Hash (Join-Path $entry.Root 'HandheldCompanion.dll')),$CallbackGoldenPath,(Join-Path $taskOutput ($entry.Name+'.json'))) $entry.Name
}
Run @('build',(Join-Path $taskSource 'probes/UiDpProbe/UiDpProbe.csproj'),('-p:BaseIntermediateOutputPath='+(Join-Path $taskOutput 'dp-build/obj/')),('-p:OutputPath='+(Join-Path $taskOutput 'dp-build/bin/')),'-p:NuGetAudit=false','--source',$OfflinePackageRoot,'--nologo','-v:q') 'build-dp'
$taskDp=Join-Path $taskOutput 'dp-build/bin/UiDpProbe.dll'
Checked @($taskDp,$taskOriginal,(Join-Path $taskOutput 'original-dp.json'),'properties-only') 'original-dp'
foreach($entry in @(@{Root=$taskBefore;Name='baseline-dp'},@{Root=$taskRuntime;Name='candidate-dp'})){Checked @($taskDp,$entry.Root,(Join-Path $taskOutput ($entry.Name+'.json'))) $entry.Name}
$taskDpOriginal=Get-Content -LiteralPath (Join-Path $taskOutput 'original-dp.json') -Raw | ConvertFrom-Json
$taskDpBefore=Get-Content -LiteralPath (Join-Path $taskOutput 'baseline-dp.json') -Raw | ConvertFrom-Json
$taskDpAfter=Get-Content -LiteralPath (Join-Path $taskOutput 'candidate-dp.json') -Raw | ConvertFrom-Json
if(($taskDpOriginal.Cases | ConvertTo-Json -Depth 20 -Compress) -ne ($taskDpAfter.Cases | ConvertTo-Json -Depth 20 -Compress)){throw 'Original property/callback baseline differs'}
if(($taskDpBefore.Cases | ConvertTo-Json -Depth 20 -Compress) -ne ($taskDpAfter.Cases | ConvertTo-Json -Depth 20 -Compress) -or ($taskDpBefore.InterfaceMaps | ConvertTo-Json -Depth 20 -Compress) -ne ($taskDpAfter.InterfaceMaps | ConvertTo-Json -Depth 20 -Compress)){throw 'DP callbacks or interface maps differ'}
Save (Join-Path $taskOutput 'dp-compare.json') @{Status='passed';CaseCount=@($taskDpAfter.Cases).Count;InterfaceMapCount=$taskDpAfter.InterfaceMapCount;ExactBaselineResults=$true};$taskReceipts.Add('dp-compare.json')
$taskTrap=$env:YMCC_UI_NEGATIVE_TRAP_ROOT
try{
 $env:YMCC_UI_NEGATIVE_TRAP_ROOT=$taskOriginal
 foreach($name in @('iNKORE.UI.WPF.Modern','iNKORE.UI.WPF.Modern.Controls')){
  $case=$name+'.deny';Checked @($taskHeadless,$taskRuntime,'538ea1f68f629a04',(Join-Path $taskOutput ($case+'.json')),'deny',$name) $case
  $missing=Join-Path $taskOutput ('missing-'+$name);CopyRuntime $taskRuntime $missing;Remove-Item -LiteralPath (LocalPath $missing ($name+'.dll'))
  $case=$name+'.missing';Checked @($taskHeadless,$missing,'538ea1f68f629a04',(Join-Path $taskOutput ($case+'.json')),'missing',$name) $case
 }
}finally{$env:YMCC_UI_NEGATIVE_TRAP_ROOT=$taskTrap}
Run @($taskHeadless,$taskRuntime,'0000000000000000',(Join-Path $taskOutput 'identity-negative.json')) 'identity-negative' 1
$taskIdentity=Get-Content -LiteralPath (Join-Path $taskOutput 'identity-negative.json') -Raw | ConvertFrom-Json
if($taskIdentity.Status -ne 'failed' -or $taskIdentity.Exception -notmatch 'Selected target identity mismatch'){throw 'Identity negative failed'}
foreach($name in @('iNKORE.UI.WPF.Modern','iNKORE.UI.WPF.Modern.Controls')){
 $bad=Join-Path $taskOutput ('bad-signature-'+$name+'.dll');Run @($taskTool,'corrupt-signature',(Join-Path $taskReplacement ($name+'.dll')),$bad) ('bad-signature-'+$name)
 $r=Get-Content -LiteralPath ($bad+'.json') -Raw | ConvertFrom-Json;if(-not $r.SignatureVerificationRejected){throw 'Corrupt signature accepted'};$taskReceipts.Add((Split-Path ($bad+'.json') -Leaf))
}
foreach($pin in $taskSourcePins){if((Hash $pin.path) -ne $pin.sha256){throw 'Source drift'}}
$taskBuild=Get-Content -LiteralPath (Join-Path $taskReplacement 'build-result.json') -Raw | ConvertFrom-Json
$taskAudit=Get-Content -LiteralPath (Join-Path $taskOutput 'audit.json') -Raw | ConvertFrom-Json
$taskBytes=($taskBuild.Results | Measure-Object Bytes -Sum).Sum
Save (Join-Path $taskOutput 'verification.json') ([ordered]@{status='passed';baselineRun=$taskBaseline;baselineBytes=825344;candidateBytes=$taskBytes;savedBytes=(825344-$taskBytes);replacements=$taskBuild.Results;receipts=@($taskReceipts);identityNegativeRejected=$true;retainedMethods=$taskAudit.RetainedMethodBodyChecks;removedMethods=@($taskAudit.RemovedMethods).Count;hcSha256=Hash (Join-Path $taskRuntime 'HandheldCompanion.dll');productionModified=$false;hardwareExecuted=$false;sourcePins=(Join-Path $taskOutput 'source-pins.json')})
Write-Output ('UI second pass verified: '+$taskBytes+' bytes; saved '+(825344-$taskBytes)+' bytes; '+@($taskAudit.RemovedMethods).Count+' methods removed.')

