[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RuntimeRoot,
    [Parameter(Mandatory=$true)][string]$SigningKeyPath,
    [Parameter(Mandatory=$true)][string]$OfflinePackageRoot,
    [Parameter(Mandatory=$true)][string]$CallbackGoldenPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$taskSourceRoot=$PSScriptRoot
$taskRepository=Split-Path (Split-Path $taskSourceRoot -Parent) -Parent
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if((Test-Path -LiteralPath $taskOutput) -and @(Get-ChildItem -LiteralPath $taskOutput -Force).Count -gt 0){throw 'OutputDirectory must be absent or empty'}
New-Item -ItemType Directory -Force -Path $taskOutput,(Join-Path $taskOutput 'logs') | Out-Null
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save([string]$Path,$Value){$Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $Path -Encoding utf8}
function LocalPath([string]$Root,[string]$Relative){
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar
    $resolved=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if(-not $resolved.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Runtime path escapes root'}
    $resolved
}
function CopyRuntime([string]$From,[string]$To){
    if(Test-Path -LiteralPath $To){throw 'Refuse existing runtime destination'}
    New-Item -ItemType Directory -Path $To | Out-Null
    $manifestPath=Join-Path $From 'HandheldCompanion.runtime.json'
    $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    foreach($file in $manifest.files){
        $source=LocalPath $From $file.path
        if((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse input file refused'}
        if((Hash $source) -ne $file.sha256){throw ('Runtime input hash mismatch: '+$file.path)}
        $dest=LocalPath $To $file.path
        New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
        Copy-Item -LiteralPath $source -Destination $dest
        if((Hash $dest) -ne $file.sha256){throw 'Frozen copy hash mismatch'}
    }
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $To 'HandheldCompanion.runtime.json')
}
function UpdateManifest([string]$Root){
    $path=Join-Path $Root 'HandheldCompanion.runtime.json'
    $manifest=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    foreach($file in $manifest.files){$local=LocalPath $Root $file.path;$file.sha256=Hash $local;if($file.PSObject.Properties.Name -contains 'bytes'){$file.bytes=(Get-Item -LiteralPath $local).Length}}
    $manifest | Add-Member -NotePropertyName uiTrimCandidateOnly -NotePropertyValue $true -Force
    Save $path $manifest
}
function RunDotnet([string[]]$Arguments,[string]$LogName,[int]$ExpectedExit=0){
    $output=& dotnet @Arguments 2>&1
    $code=$LASTEXITCODE
    $output | Out-File -LiteralPath (Join-Path $taskOutput ('logs/'+$LogName+'.log')) -Encoding utf8
    if($code -ne $ExpectedExit){$output | Write-Output;throw ('dotnet gate failed: '+$LogName+' exit '+$code)}
}
function RequirePassed([string]$Path){$result=Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json;if($result.Status -ne 'passed'){throw ('Receipt failed: '+$Path)}}
$taskFrozen=Join-Path $taskOutput 'input'
CopyRuntime $RuntimeRoot $taskFrozen
$taskSourceFiles=@(Get-ChildItem -LiteralPath $taskSourceRoot -Recurse -File)+@('Common.cs','ResourceBlob.cs','PeCompactor.cs','DevelopmentSigning.cs' | ForEach-Object {Get-Item -LiteralPath (Join-Path $taskRepository ('tools/hc-devicecore-trim/'+$_))})
$taskSourcePins=@($taskSourceFiles | ForEach-Object {[ordered]@{path=$_.FullName;sha256=Hash $_.FullName}})
Save (Join-Path $taskOutput 'source-pins.json') $taskSourcePins
$taskToolBin=Join-Path $taskOutput 'tool-build/bin'
RunDotnet @('build',(Join-Path $taskSourceRoot 'HcUiDependencyTrim.csproj'),('-p:BaseIntermediateOutputPath='+(Join-Path $taskOutput 'tool-build/obj/')),('-p:OutputPath='+$taskToolBin+'/'),'--source',$OfflinePackageRoot,'--nologo','-v:q') 'build-tool'
$taskTool=Join-Path $taskToolBin 'HcUiDependencyTrim.dll'
$taskCandidate=Join-Path $taskOutput 'replacements'
RunDotnet @($taskTool,'build',$taskFrozen,$SigningKeyPath,$taskCandidate,'tables') 'build-candidate'
$taskRuntime=Join-Path $taskOutput 'runtime'
CopyRuntime $taskFrozen $taskRuntime
foreach($name in @('iNKORE.UI.WPF.Modern.dll','iNKORE.UI.WPF.Modern.Controls.dll')){Copy-Item -LiteralPath (Join-Path $taskCandidate $name) -Destination (Join-Path $taskRuntime $name) -Force}
UpdateManifest $taskRuntime
foreach($name in @('UiHeadlessProbe','SettingsGlyphProbe','UiTableProbe')){
    RunDotnet @('build',(Join-Path $taskSourceRoot ('probes/'+$name+'/'+$name+'.csproj')),('-p:BaseIntermediateOutputPath='+(Join-Path $taskOutput ('probe-build/'+$name+'/obj/'))),('-p:OutputPath='+(Join-Path $taskOutput ('probe-build/'+$name+'/bin/'))),'--source',$OfflinePackageRoot,'--nologo','-v:q') ('build-'+$name)
}
$taskHeadless=Join-Path $taskOutput 'probe-build/UiHeadlessProbe/bin/UiHeadlessProbe.dll'
$taskGlyph=Join-Path $taskOutput 'probe-build/SettingsGlyphProbe/bin/SettingsGlyphProbe.dll'
$taskTable=Join-Path $taskOutput 'probe-build/UiTableProbe/bin/UiTableProbe.dll'
$taskReceipts=[Collections.Generic.List[string]]::new()
function Checked([string[]]$Arguments,[string]$Name){RunDotnet $Arguments $Name;RequirePassed (Join-Path $taskOutput ($Name+'.json'));$taskReceipts.Add($Name+'.json')}
Checked @($taskTool,'audit',$taskFrozen,$taskRuntime,(Join-Path $taskOutput 'audit.json')) 'audit'
Checked @($taskHeadless,$taskFrozen,'538ea1f68f629a04',(Join-Path $taskOutput 'baseline-headless.json')) 'baseline-headless'
Checked @($taskHeadless,$taskRuntime,'538ea1f68f629a04',(Join-Path $taskOutput 'candidate-headless.json')) 'candidate-headless'
Checked @($taskGlyph,$taskFrozen,(Join-Path $taskOutput 'baseline-glyph.json'),'62127') 'baseline-glyph'
Checked @($taskGlyph,$taskRuntime,(Join-Path $taskOutput 'candidate-glyph.json'),'62127') 'candidate-glyph'
Checked @($taskTable,'capture',$taskFrozen,(Join-Path $taskCandidate 'static-table-slice.json'),(Join-Path $taskOutput 'baseline-tables.json')) 'baseline-tables'
Checked @($taskTable,'compare',$taskRuntime,(Join-Path $taskOutput 'baseline-tables.json'),(Join-Path $taskOutput 'candidate-tables.json')) 'candidate-tables'
RunDotnet @('build',(Join-Path $taskRepository 'tools/hc-devicecore-trim/probes/CallbackPathProbe/CallbackPathProbe.csproj'),('-p:BaseIntermediateOutputPath='+(Join-Path $taskOutput 'probe-build/CallbackPathProbe/obj/')),('-p:OutputPath='+(Join-Path $taskOutput 'probe-build/CallbackPathProbe/bin/')),'--source',$OfflinePackageRoot,'--nologo','-v:q') 'build-callback'
Checked @((Join-Path $taskOutput 'probe-build/CallbackPathProbe/bin/CallbackPathProbe.dll'),(Join-Path $taskRuntime 'HandheldCompanion.dll'),(Hash (Join-Path $taskRuntime 'HandheldCompanion.dll')),$CallbackGoldenPath,(Join-Path $taskOutput 'callbacks.json')) 'callbacks'
$taskPriorTrap=$env:YMCC_UI_NEGATIVE_TRAP_ROOT
try{
    $env:YMCC_UI_NEGATIVE_TRAP_ROOT=$taskFrozen
    foreach($name in @('iNKORE.UI.WPF.Modern','iNKORE.UI.WPF.Modern.Controls')){
        $case=$name+'.deny'
        Checked @($taskHeadless,$taskRuntime,'538ea1f68f629a04',(Join-Path $taskOutput ($case+'.json')),'deny',$name) $case
        $missing=Join-Path $taskOutput ('missing-'+$name)
        CopyRuntime $taskRuntime $missing
        Remove-Item -LiteralPath (LocalPath $missing ($name+'.dll'))
        $case=$name+'.missing'
        Checked @($taskHeadless,$missing,'538ea1f68f629a04',(Join-Path $taskOutput ($case+'.json')),'missing',$name) $case
    }
}finally{$env:YMCC_UI_NEGATIVE_TRAP_ROOT=$taskPriorTrap}
RunDotnet @($taskHeadless,$taskRuntime,'0000000000000000',(Join-Path $taskOutput 'identity-negative.json')) 'identity-negative' 1
$taskIdentity=Get-Content -LiteralPath (Join-Path $taskOutput 'identity-negative.json') -Raw | ConvertFrom-Json
if($taskIdentity.Status -ne 'failed' -or $taskIdentity.Exception -notmatch 'Selected target identity mismatch'){throw 'Wrong identity was not rejected by actual resolver'}
foreach($pin in $taskSourcePins){if((Hash $pin.path) -ne $pin.sha256){throw 'Source changed during verification'}}
$taskBuild=Get-Content -LiteralPath (Join-Path $taskCandidate 'build-result.json') -Raw | ConvertFrom-Json
foreach($file in $taskBuild.Results){if((Hash $file.Path) -ne $file.Sha256){throw 'Replacement drift'}}
$taskSummary=[ordered]@{status='passed';scope='Desktop software headless UI dependency reduction; only two DLL replacements';replacements=$taskBuild.Results;receipts=@($taskReceipts);identityNegativePassed=$true;sourcePins=(Join-Path $taskOutput 'source-pins.json');hcHash=Hash (Join-Path $taskRuntime 'HandheldCompanion.dll');candidateBytes=($taskBuild.Results | Measure-Object Bytes -Sum).Sum;hardwareExecuted=$false;productionModified=$false;fullHcUiSupported=$false}
Save (Join-Path $taskOutput 'verification.json') $taskSummary
Write-Output ('Verified UI replacements: '+$taskSummary.candidateBytes+' bytes; '+$taskReceipts.Count+' positive/negative receipts plus wrong-identity rejection.')
