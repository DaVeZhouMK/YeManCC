[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot)
$ErrorActionPreference='Stop'
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Force -Path $ValidationRoot|Out-Null
. (Join-Path $PSScriptRoot 'release-export-identity.ps1')
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$results=[Collections.Generic.List[object]]::new()
function Text([string]$Path,[string]$Value){New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($Path))|Out-Null;[IO.File]::WriteAllText($Path,$Value,[Text.UTF8Encoding]::new($false))}
function Fixture {
 $root=Join-Path $ValidationRoot ('f-'+[Guid]::NewGuid().ToString('N'));$project=Join-Path $root 'source';$work=Join-Path $root 'workspace';$stage=Join-Path $work 'Build/Package/CompleteRoot'
 foreach($scope in @('native','src','decky-plugin','InputHost','scripts')){Text (Join-Path $project ($scope+'/input.txt')) ('source-'+$scope)}
 foreach($name in @('index.html','vite.config.ts','tsconfig.json','tsconfig.node.json','app.config.json','version.json','package.json','pnpm-lock.yaml','tools/build-workspace.ps1','tools/package-release.ps1','tools/release-export-identity.ps1','tools/decky-release-identity.ps1','tools/build-decky-sidebar-plugin.mjs','tools/decky_sidebar_decky_api_build.mjs')){Text (Join-Path $project $name) ('inert-'+$name)}
 Text (Join-Path $project 'version.json') '{"version":"0.0.33"}'
 foreach($name in @('index.html','app.config.json','assets/current.js','assets/current.css')){Text (Join-Path $work ('Build/App/Web/'+$name)) ('web-'+$name)}
 foreach($name in @('YeManCC.exe','YeManRecoveryService.exe','YMCCRtssProfileHelper.exe','YMCCOverlayBridge.dll')){Text (Join-Path $work ('Build/App/Native/'+$name)) ('inert-native-'+$name)}
 foreach($name in @('dist/index.js','plugin.json','package.json','LICENSE.decky-api')){Text (Join-Path $work ('Build/App/Decky/plugins/ymcc-sidebar/'+$name)) ('decky-'+$name)}
 foreach($name in @('PluginLoader_noconsole.exe','LICENSE.decky-loader')){Text (Join-Path $project ('PowerControl/decky/'+$name)) ('loader-'+$name)}
 $before=@(Get-ExportSourceIndex $project)
 $build=Write-ExportBuildIdentity $project $work $before
 foreach($r in $build.payload){$rel=$r.path;$file=if($r.lane-eq'web'){Join-Path $work ('Build/App/Web/'+$rel.Substring(8))}elseif($r.lane-eq'native'){Join-Path $work ('Build/App/Native/'+$rel.Substring(8))}elseif($rel.StartsWith('PowerControl/decky/plugins/')){Join-Path $work ('Build/App/Decky/'+$rel.Substring(19))}else{Join-Path $project $rel};$target=Join-Path $stage $rel;New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($target))|Out-Null;Copy-Item -LiteralPath $file -Destination $target}
 Text (Join-Path $stage 'PowerControl/runtime/owner.bin') 'inert-owner-resource'
 $null=Write-ExportPackageIdentity $stage $build
 $zip=Join-Path $root 'candidate.zip';[IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip)
 return @{root=$root;project=$project;work=$work;stage=$stage;build=$build;zip=$zip;before=$before}
}
function Alter([string]$Path){$data=[IO.File]::ReadAllBytes($Path);$data[0]=$data[0]-bxor 1;[IO.File]::WriteAllBytes($Path,$data)}
function Zip-Edit($F,[string]$Name,[string]$Mode,[string]$Text='changed') {
 $z=[IO.Compression.ZipFile]::Open($F.zip,[IO.Compression.ZipArchiveMode]::Update)
 try{$entry=$z.Entries|Where-Object {$_.FullName.Replace('\','/')-eq$Name}|Select-Object -First 1
  if($Mode-in@('delete','replace')){if(-not$entry){throw 'Fixture entry absent'};$entry.Delete()}
  if($Mode-ne'delete'){$entry=$z.CreateEntry($Name);$w=[IO.StreamWriter]::new($entry.Open());try{$w.Write($Text)}finally{$w.Dispose()}}
 }finally{$z.Dispose()}
}
function Reject([scriptblock]$Action,[string]$Reason){$message='';try{& $Action|Out-Null}catch{$message=$_.Exception.Message};if(-not$message.StartsWith($Reason,[StringComparison]::Ordinal)){throw "Expected $Reason, got $message"}}
function Case([string]$CaseName,[scriptblock]$Action){$f=Fixture;try{& $Action $f;$results.Add(@{name=$CaseName;passed=$true})}catch{$results.Add(@{name=$CaseName;passed=$false;error=$_.Exception.Message})}}
Case 'source/build/staging/whole ZIP including identity match' {param($f)$b=Assert-ExportBuildIdentity $f.project $f.work;$r=Assert-ExportArchiveIdentity $f.zip $f.stage $b;if($r.status-ne'passed'-or$r.files-ne16){throw 'Unexpected valid receipt'}}
Case 'timestamp-only change does not invalidate a byte-identical source' {param($f)(Get-Item -LiteralPath (Join-Path $f.project 'src/input.txt')).LastWriteTime=[DateTime]::Now.AddDays(1);$null=Assert-ExportBuildIdentity $f.project $f.work}
foreach($name in @('native/input.txt','src/input.txt','decky-plugin/input.txt')){Case ('same-length source change rejected: '+$name) {param($f)Alter (Join-Path $f.project $name);Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_SOURCE_CHANGED:'}}
foreach($name in @('Native/YeManCC.exe','Web/assets/current.js','Decky/plugins/ymcc-sidebar/dist/index.js')){Case ('same-length built payload change rejected: '+$name) {param($f)Alter (Join-Path $f.work ('Build/App/'+$name));Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_BUILD_CHANGED:'}}
Case 'legacy build missing capture is not silently recaptured by exporter' {param($f)Move-Item -LiteralPath (Join-Path $f.work 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json') -Destination (Join-Path $f.root 'old-capture.json');Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_BUILD_IDENTITY_MISSING:'}
Case 'source modified during compilation rejects build completion' {param($f)Alter (Join-Path $f.project 'native/input.txt');Reject {Write-ExportBuildIdentity $f.project $f.work $f.before} 'EXPORT_SOURCE_CHANGED_DURING_BUILD:'}
foreach($name in @('YeManCC/YeManCC.exe','YeManCC/assets/current.js','PowerControl/decky/plugins/ymcc-sidebar/dist/index.js')){Case ('staging copy drift rejected: '+$name) {param($f)Alter (Join-Path $f.stage $name);Reject {Assert-ExportStagedBuild $f.stage $f.build} 'EXPORT_STAGE_MISMATCH:'}}
Case 'stale extra Web chunk rejected before ZIP generation' {param($f)Text (Join-Path $f.stage 'YeManCC/assets/old.js') 'old';Reject {Assert-ExportStagedBuild $f.stage $f.build} 'EXPORT_STAGE_WEB_FILESET_MISMATCH:'}
foreach($name in @('YeManCC/YeManCC.exe','YeManCC/assets/current.js','PowerControl/decky/plugins/ymcc-sidebar/dist/index.js','PowerControl/runtime/owner.bin')){Case ('final ZIP byte drift rejected: '+$name) {param($f)Zip-Edit $f $name replace;Reject {Assert-ExportArchiveIdentity $f.zip $f.stage $f.build} 'EXPORT_ZIP_MISMATCH:'}}
Case 'missing dependency rejected in final ZIP' {param($f)Zip-Edit $f 'PowerControl/runtime/owner.bin' delete;Reject {Assert-ExportArchiveIdentity $f.zip $f.stage $f.build} 'EXPORT_ZIP_MISSING:'}
Case 'missing export identity rejected in final ZIP' {param($f)Zip-Edit $f 'YeManCC/export-identity.json' delete;Reject {Assert-ExportArchiveIdentity $f.zip $f.stage $f.build} 'EXPORT_ZIP_MISSING:'}
Case 'duplicate case/slash plugin entry rejected' {param($f)Zip-Edit $f 'POWERCONTROL\DECKY\PLUGINS\YMCC-SIDEBAR\DIST\INDEX.JS' add;Reject {Assert-ExportArchiveIdentity $f.zip $f.stage $f.build} 'EXPORT_ZIP_DUPLICATE:'}
Case 'unexpected extra resource rejected' {param($f)Zip-Edit $f 'PowerControl/old.bin' add;Reject {Assert-ExportArchiveIdentity $f.zip $f.stage $f.build} 'EXPORT_ZIP_UNEXPECTED_FILE:'}
Case 'unexpected top-level directory rejected' {param($f)Zip-Edit $f 'OtherRoot/input.bin' add;Reject {Assert-ExportArchiveIdentity $f.zip $f.stage $f.build} 'EXPORT_ZIP_UNEXPECTED_ROOT:'}
foreach($name in @('../outside.txt','YeManCC/assets/../old.js','YeManCC/NUL.txt','YeManCC/old.js.','YeManCC//old.js','C:/outside.txt')){Case ('unsafe final ZIP path rejected: '+$name) {param($f)Zip-Edit $f $name add;Reject {Assert-ExportArchiveIdentity $f.zip $f.stage $f.build} 'EXPORT_UNSAFE_PATH:'}}
Case 'staging changed after manifest generation cannot be relabelled as original payload' {param($f)Alter (Join-Path $f.stage 'PowerControl/runtime/owner.bin');$zip=Join-Path $f.root 'after-stage-change.zip';[IO.Compression.ZipFile]::CreateFromDirectory($f.stage,$zip);Reject {Assert-ExportArchiveIdentity $zip $f.stage $f.build} 'EXPORT_PACKAGE_IDENTITY_INVALID'}
Case 'original Release remains untouched when there was no publication' {param($f)$old=Join-Path $f.work 'Release/YeManCC/original.txt';Text $old 'previous-good-release';$ledger=[Collections.Generic.List[object]]::new();Undo-ExportReleaseItems $f.work $ledger;if([IO.File]::ReadAllText($old)-ne'previous-good-release'){throw 'Original deleted'}}
Case 'late failure restores previous directory and removes newly created package lane' {param($f)$old=Join-Path $f.work 'Release/YeManCC';Text (Join-Path $old 'original.txt') 'previous-good-release';$pack=Join-Path $f.work 'Release/Packages';$backup=Join-Path $f.work 'Backup/Release/test';$ledger=[Collections.Generic.List[object]]::new();Backup-ExportReleaseItem $old $backup $f.work $ledger;Backup-ExportReleaseItem $pack $backup $f.work $ledger;Text (Join-Path $old 'new.txt') 'new';Text (Join-Path $pack 'YeManCC.zip') 'new';Undo-ExportReleaseItems $f.work $ledger;if(-not[IO.File]::Exists((Join-Path $old 'original.txt'))-or(Test-Path -LiteralPath $pack)){throw 'Rollback incomplete'}}
Case 'partially failed backup never deletes untouched original' {param($f)$old=Join-Path $f.work 'Release/PowerControl';Text (Join-Path $old 'original.txt') 'old';$ledger=[Collections.Generic.List[object]]::new();$ledger.Add(@{path=$old;backup=(Join-Path $f.work 'Backup/Release/test/PowerControl');hadOriginal=$true});Undo-ExportReleaseItems $f.work $ledger;if(-not[IO.File]::Exists((Join-Path $old 'original.txt'))){throw 'Untouched original lost'}}
Case 'recursive rollback is confined to workspace Release and Backup' {param($f)$ledger=[Collections.Generic.List[object]]::new();$ledger.Add(@{path=$f.project;backup=(Join-Path $f.work 'Backup/Release/test/source');hadOriginal=$false});Reject {Undo-ExportReleaseItems $f.work $ledger} 'EXPORT_ROLLBACK_FAILED:';if(-not[IO.Directory]::Exists($f.project)){throw 'Source deleted'}}
Case 'second process cannot build/export into occupied workspace' {param($f)$mutex=Enter-ExportWorkspaceLock $f.work;try{$lib=(Join-Path $PSScriptRoot 'release-export-identity.ps1').Replace("'","''");$work=$f.work.Replace("'","''");$command='. ''{0}'';try{{$m=Enter-ExportWorkspaceLock ''{1}'';$m.ReleaseMutex();$m.Dispose();exit 1}}catch{{if($_.Exception.Message.StartsWith(''EXPORT_WORKSPACE_BUSY:'')){{exit 0}}else{{exit 2}}}}' -f $lib,$work;& powershell.exe -NoProfile -NonInteractive -Command $command;if($LASTEXITCODE-ne0){throw 'Workspace lock failed'}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
Case 'fingerprint order is ordinal and independent of input order or PowerShell version' {param($f)
 $rows=@(@{path='a/input';bytes=1;sha256='1'},@{path='A/input';bytes=2;sha256='2'},@{path='a-/input';bytes=3;sha256='3'},@{path='a_/input';bytes=4;sha256='4'})
 $ordered=Get-ExportIndexHash $rows;[Array]::Reverse($rows);if((Get-ExportIndexHash $rows)-ne$ordered){throw 'Input order changed identity'}
 $data=Join-Path $f.root 'order.json';$rows|ConvertTo-Json -Depth 3|Set-Content -LiteralPath $data -Encoding UTF8
 $lib=(Join-Path $PSScriptRoot 'release-export-identity.ps1').Replace("'","''");$data=$data.Replace("'","''")
 $command='. ''{0}'';$x=[IO.File]::ReadAllText(''{1}'')|ConvertFrom-Json;Get-ExportIndexHash @($x)' -f $lib,$data
 $legacy=(& powershell.exe -NoProfile -NonInteractive -Command $command|Out-String).Trim()
 if($LASTEXITCODE-ne0-or$legacy-ne$ordered){throw 'PowerShell 5.1 fingerprint drift'}
 $pwsh=Get-Command pwsh.exe -ErrorAction SilentlyContinue
 if($pwsh){$modern=(& $pwsh.Source -NoProfile -NonInteractive -Command $command|Out-String).Trim();if($LASTEXITCODE-ne0-or$modern-ne$ordered){throw 'PowerShell 7 fingerprint drift'}}
}
$packager=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'package-release.ps1'))
$buildScript=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'build-workspace.ps1'))
try{if($packager.IndexOf('$exportArchiveReceipt = Assert-ExportArchiveIdentity')-gt$packager.IndexOf('foreach ($path in $releaseItems) { Backup-ExportReleaseItem')){throw 'Publication too early'};if(-not$packager.Contains('Undo-ExportReleaseItems')-or-not$packager.Contains('Assert-ExportBuildIdentity')-or-not$buildScript.Contains('Write-ExportBuildIdentity')-or-not$buildScript.Contains('--out $DeckyBuild')){throw 'Missing production integration'};$results.Add(@{name='production exporter integrates isolated Decky/source capture/deferred publish/rollback';passed=$true})}catch{$results.Add(@{name='production integration';passed=$false;error=$_.Exception.Message})}
$failed=@($results|Where-Object {-not$_.passed})
$report=[ordered]@{status=$(if($failed.Count){'failed'}else{'passed'});cases=$results.Count;failed=$failed.Count;results=@($results.ToArray());productionApplicationChanged=$false;realReleaseExecuted=$false;fixtureRoot=$ValidationRoot}
$report|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $ValidationRoot 'FULL-EXPORTER-SELFTEST.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 7
if($failed.Count){throw 'Exporter regressions failed'}