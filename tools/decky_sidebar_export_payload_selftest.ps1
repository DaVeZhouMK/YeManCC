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
 'decky/plugins/ymcc-sidebar/package.json','decky/plugins/ymcc-sidebar/LICENSE.decky-api','decky/plugins/ymcc-sidebar/LICENSE.vue'
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
if($receipt.files.Count -ne 7 -or $receipt.status -ne 'passed' -or -not $receipt.stagingChecked){throw 'Correct seven-asset payload rejected'}
$cases.Add('current source/stage/final ZIP match all seven assets')
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
Expect-Reject 'missing Vue license rejected in final ZIP' 'DECKY_EXPORT_ENTRY_COUNT:' (Write-FixtureZip 'missing-vue-license' @($entries|Where-Object{$_.name-ne$entries[6].name}))
$wrongLicense=@($entries|ForEach-Object{if($_.name-eq$entries[6].name){@{name=$_.name;bytes=[Text.Encoding]::UTF8.GetBytes('wrong-Vue-license')}}else{$_}})
Expect-Reject 'Vue license byte drift rejected in final ZIP' 'DECKY_EXPORT_ZIP_MISMATCH:' (Write-FixtureZip 'wrong-vue-license' $wrongLicense)
$built=Join-Path $fixture 'built-plugin';New-Item -ItemType Directory -Path $built -Force|Out-Null
Copy-Item -Path (Join-Path $source 'decky\plugins\ymcc-sidebar\*') -Destination $built -Recurse
# Keep the compiled plugin/stage/ZIP exact while SOURCE dist is deliberately stale.
[IO.File]::WriteAllText((Join-Path $source 'decky\plugins\ymcc-sidebar\dist\index.js'),'STALE-CACHED-SOURCE-DIST')
$builtReceipt=Assert-DeckyExportPayload -ReleaseZip $good -SourcePowerControl $source -BuiltPluginRoot $built -StagedPowerControl $stage
if(-not$builtReceipt.builtPluginChecked-or$builtReceipt.sourceMatched){throw 'Compiled plugin authority not reflected in receipt'}
$cases.Add('formal ZIP checks compiled plugin, not cached source dist')
$cached=@($entries|ForEach-Object{if($_.name-eq$plugin){@{name=$_.name;bytes=[Text.Encoding]::UTF8.GetBytes('STALE-CACHED-SOURCE-DIST')}}else{$_}})
$caught='';try{Assert-DeckyExportPayload -ReleaseZip (Write-FixtureZip 'cached-old-source' $cached) -SourcePowerControl $source -BuiltPluginRoot $built -StagedPowerControl $stage|Out-Null}catch{$caught=$_.Exception.Message}
if(-not$caught.StartsWith('DECKY_EXPORT_ZIP_MISMATCH:')){throw 'Stale source cached ZIP accepted as compiled payload'}
$cases.Add('stale cached-source ZIP cannot pass against compiled plugin')
# Descriptors come from a fresh in-memory build in the production gate. Test
# the archive comparison separately with inert bytes and a stale source cache.
$compiledFiles = @($entries | Where-Object { $_.name.StartsWith('PowerControl/decky/plugins/ymcc-sidebar/') } | ForEach-Object {
  $file = Join-Path $built $_.name.Substring('PowerControl/decky/plugins/ymcc-sidebar/'.Length)
  @{name=$_.name.Substring('PowerControl/decky/plugins/ymcc-sidebar/'.Length);bytes=$_.bytes.Length;sha256=(Get-DeckyExportFileSha256 $file)}
})
$current = Assert-DeckyExportPayload -ReleaseZip $good -SourcePowerControl $source -CurrentSourcePluginFiles $compiledFiles
if(-not$current.currentSourceChecked-or$current.builtPluginChecked){throw 'Current-source descriptor authority not reflected in receipt'}
$cases.Add('fresh compilation descriptors accept archive while cached source dist is stale')
function Expect-CompiledReject([string]$Name,[string]$Reason,[string]$Zip,[object[]]$Files=$compiledFiles){
 $errorText=''
 try{Assert-DeckyExportPayload -ReleaseZip $Zip -SourcePowerControl $source -CurrentSourcePluginFiles $Files|Out-Null}catch{$errorText=$_.Exception.Message}
 if(-not$errorText.StartsWith($Reason,[StringComparison]::Ordinal)){throw "Expected $Reason for $Name; got $errorText"}
 $cases.Add($Name)
}
Expect-CompiledReject 'cached old plugin rejected against current compilation' 'DECKY_EXPORT_ZIP_MISMATCH:' (Write-FixtureZip 'compiled-cached-old' $cached)
Expect-CompiledReject 'same-length archive drift rejected against current compilation' 'DECKY_EXPORT_ZIP_MISMATCH:' (Write-FixtureZip 'compiled-drift' $drift)
Expect-CompiledReject 'missing plugin rejected against current compilation' 'DECKY_EXPORT_ENTRY_COUNT:' (Write-FixtureZip 'compiled-missing' @($entries|Where-Object{$_.name-ne$plugin}))
Expect-CompiledReject 'case and slash duplicate rejected against current compilation' 'DECKY_EXPORT_ENTRY_COUNT:' (Write-FixtureZip 'compiled-duplicate' $duplicate)
Expect-CompiledReject 'missing Vue license rejected against current compilation' 'DECKY_EXPORT_ENTRY_COUNT:' (Write-FixtureZip 'compiled-license-missing' @($entries|Where-Object{$_.name-ne$entries[6].name}))
Expect-CompiledReject 'incomplete compilation descriptor set rejected' 'DECKY_EXPORT_CURRENT_SOURCE_RECORD_INVALID' $good @($compiledFiles|Where-Object{$_.name-ne'plugin.json'})
Expect-CompiledReject 'duplicate compilation descriptor rejected' 'DECKY_EXPORT_CURRENT_SOURCE_RECORD_INVALID' $good (@($compiledFiles)+@($compiledFiles[0]))
# Every envelope carries a managed identity, including the legacy bootstrap.
$managedFiles=@(foreach($name in $assets){$file=Join-Path $stage $name.Replace('/','\');[pscustomobject]@{path=$name.Substring('decky/'.Length);bytes=([IO.FileInfo]$file).Length;sha256=(Get-DeckyExportFileSha256 $file).ToLowerInvariant()}})
$managed=[pscustomobject]@{schemaVersion=1;owner='YMCC';origin='official-release';loaderVersion='v3.3.0';loaderSha256=$managedFiles[0].sha256;files=$managedFiles}
$metadata=@{name='YeManCC/decky-package.json';bytes=[Text.Encoding]::UTF8.GetBytes(($managed|ConvertTo-Json -Depth 8 -Compress))}
Assert-DeckyExportPayload -ReleaseZip (Write-FixtureZip 'managed-good' (@($entries)+@($metadata))) -SourcePowerControl $source -BuiltPluginRoot $built -StagedPowerControl $stage -RequireManagedManifest|Out-Null
$cases.Add('normal and bootstrap envelopes bind metadata to all seven verified final ZIP assets')
$manifestCases=@(
  @{name='missing';rows=@($entries);reason='DECKY_EXPORT_MANAGED_MANIFEST_MISSING_OR_DUPLICATED'},
  @{name='duplicate';rows=(@($entries)+@($metadata)+@(@{name='YeManCC\DECKY-PACKAGE.JSON';bytes=$metadata.bytes}));reason='DECKY_EXPORT_MANAGED_MANIFEST_MISSING_OR_DUPLICATED'}
)
$wrong=$managed|ConvertTo-Json -Depth 8|ConvertFrom-Json;$wrong.files[2].sha256='0'*64
$manifestCases+=@{name='mismatched';rows=(@($entries)+@(@{name='YeManCC/decky-package.json';bytes=[Text.Encoding]::UTF8.GetBytes(($wrong|ConvertTo-Json -Depth 8 -Compress))}));reason='DECKY_EXPORT_MANAGED_MANIFEST_ASSET_MISMATCH'}
foreach($c in $manifestCases){
  $failure=''
  try{Assert-DeckyExportPayload -ReleaseZip (Write-FixtureZip ('managed-'+$c.name) $c.rows) -SourcePowerControl $source -BuiltPluginRoot $built -StagedPowerControl $stage -RequireManagedManifest|Out-Null}catch{$failure=$_.Exception.Message}
  if(!$failure.StartsWith($c.reason)){throw ('Managed metadata case '+$c.name+' got '+$failure)}
  $cases.Add('managed metadata '+$c.name+' rejected before publishing')
}
$packager=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'package-release.ps1'))
$gate=$packager.IndexOf('$deckyExportReceipt = Assert-DeckyExportPayload')
$publish=$packager.IndexOf("Write-Output 'PACKAGE_OK'")
if($gate-lt 0 -or $publish-lt$gate -or -not$packager.Contains('build-decky-sidebar-plugin.mjs')){throw 'Release gate missing or after PACKAGE_OK'}
$cases.Add('final ZIP guard runs before PACKAGE_OK and original source freshness guard is retained')
$currentGate=$packager.IndexOf('$deckyCurrentSourceReceipt = Assert-DeckyCurrentSourceExport')
if($currentGate-lt$gate-or$currentGate-gt$publish){throw 'Current-source final ZIP guard missing or after PACKAGE_OK'}
$cases.Add('fresh source compilation is compared with published ZIP before PACKAGE_OK')

$report=[ordered]@{status='passed';cases=$cases.Count;results=@($cases.ToArray());realReleaseExecuted=$false;installedProductModified=$false;fixtureRoot=$fixture}
$report|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $ValidationRoot 'EXPORT-PAYLOAD-SELFTEST.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 5
