[CmdletBinding()]
param([string]$ValidationRoot)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $ValidationRoot){$ValidationRoot=Join-Path $project '..\..\Build\Validation\DeckyExport'}
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
$fixture=Join-Path $ValidationRoot ('fixtures-'+[Guid]::NewGuid().ToString('N'))
$source=Join-Path $fixture 'source'
$stage=Join-Path $fixture 'stage'
New-Item -ItemType Directory -Path $source,$stage -Force | Out-Null
. (Join-Path $PSScriptRoot 'decky-release-identity.ps1')
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$assets=@(
 'decky/PluginLoader_noconsole.exe','decky/LICENSE.decky-loader',
 'decky/plugins/ymcc-sidebar/dist/index.js','decky/plugins/ymcc-sidebar/plugin.json',
 'decky/plugins/ymcc-sidebar/package.json','decky/plugins/ymcc-sidebar/LICENSE.decky-api'
)
$entries=@()
foreach($relative in $assets){
 $bytes=[Text.Encoding]::UTF8.GetBytes('inert-source-'+$relative)
 foreach($root in @($source,$stage)){$target=Join-Path $root $relative.Replace('/','\');New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force|Out-Null;[IO.File]::WriteAllBytes($target,$bytes)}
 $entries+=@{name='PowerControl/'+$relative;bytes=$bytes}
}
function Write-FixtureZip([string]$Name,[object[]]$Rows){
 $path=Join-Path $fixture ($Name+'.zip')
 $zip=[IO.Compression.ZipFile]::Open($path,[IO.Compression.ZipArchiveMode]::Create)
 try{foreach($row in $Rows){$entry=$zip.CreateEntry([string]$row.name);$stream=$entry.Open();try{$stream.Write($row.bytes,0,$row.bytes.Length)}finally{$stream.Dispose()}}}finally{$zip.Dispose()}
 return $path
}
$cases=New-Object 'System.Collections.Generic.List[string]'
function Expect-Reject([string]$Name,[string]$Reason,[string]$Zip,[string]$Source=$source,[string]$Stage=$stage){
 $caught=''
 try{Assert-DeckyExportPayload -ReleaseZip $Zip -SourcePowerControl $Source -StagedPowerControl $Stage|Out-Null}catch{$caught=$_.Exception.Message}
 if(-not $caught.StartsWith($Reason,[StringComparison]::Ordinal)){throw "Expected $Reason for $Name; got $caught"}
 $cases.Add($Name)
}
$good=Write-FixtureZip 'current' $entries
$receipt=Assert-DeckyExportPayload -ReleaseZip $good -SourcePowerControl $source -StagedPowerControl $stage
if($receipt.files.Count -ne 6 -or $receipt.status -ne 'passed' -or -not $receipt.stagingChecked){throw 'Correct six-asset payload rejected'}
$cases.Add('current source/stage/final ZIP match all six assets')
$backslash=@($entries|ForEach-Object{@{name=$_.name.Replace('/','\');bytes=$_.bytes}})
Assert-DeckyExportPayload -ReleaseZip (Write-FixtureZip 'backslash' $backslash) -SourcePowerControl $source -StagedPowerControl $stage|Out-Null
$cases.Add('Windows ZIP backslash entry names accepted')
$plugin=$entries[2].name
$drift=@($entries|ForEach-Object{if($_.name -eq $plugin){$bytes=[byte[]]$_.bytes.Clone();$bytes[0]=[byte]88;@{name=$_.name;bytes=$bytes}}else{$_}})
Expect-Reject 'same-length stale icon/frontend rejected by SHA256' 'DECKY_EXPORT_ZIP_MISMATCH:' (Write-FixtureZip 'old-plugin' $drift)
Expect-Reject 'missing frontend rejected' 'DECKY_EXPORT_ENTRY_COUNT:' (Write-FixtureZip 'missing-plugin' @($entries|Where-Object{$_.name-ne$plugin}))
Expect-Reject 'missing loader rejected' 'DECKY_EXPORT_ENTRY_COUNT:' (Write-FixtureZip 'missing-loader' @($entries|Where-Object{$_.name-ne$entries[0].name}))
$duplicate=@($entries)+@(@{name=$plugin.ToUpperInvariant().Replace('/','\');bytes=$entries[2].bytes})
Expect-Reject 'case/slash duplicate cannot overwrite verified plugin' 'DECKY_EXPORT_ENTRY_COUNT:' (Write-FixtureZip 'duplicate-plugin' $duplicate)
$wrongManifest=@($entries|ForEach-Object{if($_.name-eq$entries[3].name){@{name=$_.name;bytes=[Text.Encoding]::UTF8.GetBytes('old-plugin-manifest')}}else{$_}})
Expect-Reject 'plugin manifest drift rejected' 'DECKY_EXPORT_ZIP_MISMATCH:' (Write-FixtureZip 'wrong-manifest' $wrongManifest)
$stagePlugin=Join-Path $stage 'decky\plugins\ymcc-sidebar\dist\index.js'
[IO.File]::WriteAllText($stagePlugin,'old-staging-frontend')
Expect-Reject 'stale staging rejected even with correct final archive' 'DECKY_EXPORT_STAGED_MISMATCH:' $good
[IO.File]::WriteAllBytes($stagePlugin,$entries[2].bytes)
$empty=Join-Path $fixture 'empty';New-Item -ItemType Directory -Path $empty -Force|Out-Null
Expect-Reject 'missing staging rejected' 'DECKY_EXPORT_STAGED_MISSING:' $good $source $empty
Expect-Reject 'missing source rejected' 'DECKY_EXPORT_SOURCE_MISSING:' $good $empty $stage
Expect-Reject 'legacy archive without Decky rejected' 'DECKY_EXPORT_ENTRY_COUNT:' (Write-FixtureZip 'legacy' @(@{name='YeManCC/version.json';bytes=[Text.Encoding]::UTF8.GetBytes('{}')}))
Assert-DeckyExportPayload -ReleaseZip $good -SourcePowerControl $source|Out-Null
$cases.Add('archive-only audit supported without modifying product')
$packager=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'package-release.ps1'))
$gate=$packager.IndexOf('$deckyExportReceipt = Assert-DeckyExportPayload')
$publish=$packager.IndexOf("Write-Output 'PACKAGE_OK'")
if($gate-lt 0 -or $publish-lt$gate -or -not$packager.Contains('build-decky-sidebar-plugin.mjs')){throw 'Release gate missing or after PACKAGE_OK'}
$cases.Add('final ZIP guard runs before PACKAGE_OK and original source freshness guard is retained')
$report=[ordered]@{status='passed';cases=$cases.Count;results=@($cases.ToArray());realReleaseExecuted=$false;installedProductModified=$false;fixtureRoot=$fixture}
$report|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $ValidationRoot 'EXPORT-PAYLOAD-SELFTEST.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 5
