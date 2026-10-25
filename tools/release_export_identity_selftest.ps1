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
 foreach($scope in @('native','src','public','capabilities','decky-plugin','InputHost','GamepadPrerequisites','FanLab/LightSetter','scripts')){Text (Join-Path $project ($scope+'/input.txt')) ('source-'+$scope)}
 Text (Join-Path $project 'capabilities/HcControllerFeedbackCapabilityCatalog.h') 'inert-native-capability'
 Text (Join-Path $project 'capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs') 'inert-managed-capability'
 # The fixture materializes the maintained required-file contract, then the
 # behavior cases below independently test stale/missing/changed payloads.
 $requiredBlock=[regex]::Match([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'release-export-identity.ps1')),'foreach\(\$relative in @\((.*?)\)\)\s*\{',[Text.RegularExpressions.RegexOptions]::Singleline)
 if(-not $requiredBlock.Success){throw 'Required source contract missing'}
 foreach($entry in [regex]::Matches($requiredBlock.Groups[1].Value,"'([^']+)'")){ $name=$entry.Groups[1].Value;Text (Join-Path $project $name) ('inert-'+$name) }
 Text (Join-Path $project 'version.json') '{"version":"0.0.33"}'
 # Keep the existing Web contract: the required probe replaces the optional CSS fixture.
 foreach($name in @('index.html','hud-output.html','radial-output.html','app.config.json','assets/current.js')){Text (Join-Path $work ('Build/App/Web/'+$name)) ('web-'+$name)}
 foreach($name in @('YeManCC.exe','YeManRecoveryService.exe','YeManControllerVisibilityProbe.exe','YMCCRtssProfileHelper.exe','YMCCOverlayBridge.dll')){Text (Join-Path $work ('Build/App/Native/'+$name)) ('inert-native-'+$name)}
 foreach($name in @('dist/index.js','plugin.json','package.json','LICENSE.decky-api','LICENSE.vue')){Text (Join-Path $work ('Build/App/Decky/plugins/ymcc-sidebar/'+$name)) ('decky-'+$name)}
 foreach($name in @('PluginLoader_noconsole.exe','LICENSE.decky-loader')){Text (Join-Path $project ('PowerControl/decky/'+$name)) ('loader-'+$name)}
 # Include the newer battery export lane with inert bytes; no owner is run.
 Text (Join-Path $project 'native/BatteryHost/payload-files.json') '["fixture-owner.dll","battery-host.payload.json"]'
 Text (Join-Path $project 'PowerControl/battery-host/fixture-owner.dll') 'inert-battery-owner'
 Text (Join-Path $work 'Build/App/BatteryHost/fixture-owner.dll') 'inert-battery-owner'
 Text (Join-Path $project 'PowerControl/battery-host/battery-host.payload.json') '{"fixture":true}'
 foreach($name in @('YeManInputHost.exe','YeManInputHost.dll','YeManInputHost.deps.json','YeManInputHost.runtimeconfig.json','HIDMaestro.Core.dll')) { Text (Join-Path $work ('Build/App/Native/InputHost/'+$name)) ('inert-inputhost-'+$name) }
 foreach($name in @('YeManLightSetter.exe','YeManLightSetter.dll','YeManLightSetter.deps.json','YeManLightSetter.runtimeconfig.json','YeManLightSetter.json','YeManLightDeviceCore.dll')) { Text (Join-Path $project ('PowerControl/light-setter/'+$name)) ('inert-light-setter-'+$name) }
 $before=@(Get-ExportSourceIndex $project)
 $build=Write-ExportBuildIdentity $project $work $before
 foreach($r in $build.payload){$rel=$r.path;$file=if($r.lane-eq'web'){Join-Path $work ('Build/App/Web/'+$rel.Substring(8))}elseif($r.lane-eq'native'){Join-Path $work ('Build/App/Native/'+$rel.Substring(8))}elseif($r.lane-eq'inputhost'){Join-Path $work ('Build/App/Native/InputHost/'+[IO.Path]::GetFileName($rel))}elseif($rel.StartsWith('PowerControl/decky/plugins/')){Join-Path $work ('Build/App/Decky/'+$rel.Substring(19))}else{Join-Path $project $rel};$target=Join-Path $stage $rel;New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($target))|Out-Null;Copy-Item -LiteralPath $file -Destination $target}
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
Case 'source/build/staging/whole ZIP including identity match' {param($f)$b=Assert-ExportBuildIdentity $f.project $f.work;$r=Assert-ExportArchiveIdentity $f.zip $f.stage $b;if($r.status-ne'passed'-or$r.files-ne32){throw 'Unexpected valid receipt'}}
Case 'development install refreshes stale identity only after matching native web and Decky' {param($f)
 $old='old-development-identity';Text (Join-Path $f.stage 'YeManCC/export-identity.json') $old
 $backup=Join-Path $f.root 'installed-backup'
 $record=Write-ExportInstalledIdentityFromArchive $f.stage $f.zip $f.build $backup
 if($record.buildId -ne $f.build.buildId){throw 'Installed identity was not refreshed'}
 if([IO.File]::ReadAllText((Join-Path $backup 'YeManCC/export-identity.json')) -ne $old){throw 'Old installed identity was not backed up'}
 $actual=[IO.File]::ReadAllText((Join-Path $f.stage 'YeManCC/export-identity.json'))|ConvertFrom-Json
 if($actual.buildId -ne $f.build.buildId){throw 'Stale installed metadata survived'}
}
Case 'development identity rejects another build without overwriting current receipt' {param($f)
 $before=[IO.File]::ReadAllBytes((Join-Path $f.stage 'YeManCC/export-identity.json'))
 Zip-Edit $f 'YeManCC/export-identity.json' replace '{"schema":1,"packageId":"YeManCC","buildId":"foreign"}'
 Reject {Write-ExportInstalledIdentityFromArchive $f.stage $f.zip $f.build (Join-Path $f.root 'backup')} 'EXPORT_INSTALLED_IDENTITY_BUILD_MISMATCH'
 if((Get-ExportTextHash ([Convert]::ToBase64String($before))) -ne (Get-ExportTextHash ([Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $f.stage 'YeManCC/export-identity.json')))))){throw 'Rejected identity overwrote installed file'}
}
Case 'development identity cannot relabel mismatched Decky payload' {param($f)
 Alter (Join-Path $f.stage 'PowerControl/decky/plugins/ymcc-sidebar/dist/index.js')
 Reject {Write-ExportInstalledIdentityFromArchive $f.stage $f.zip $f.build (Join-Path $f.root 'backup')} 'EXPORT_STAGE_MISMATCH:'
}
Case 'embedded TaskView needs no separate helper build or archive member' {param($f)Text (Join-Path $f.work 'Build/App/Native/YeManTaskViewHelper.exe') 'stale-unused-helper';$b=Assert-ExportBuildIdentity $f.project $f.work;$r=Assert-ExportArchiveIdentity $f.zip $f.stage $b;if($r.status-ne'passed'-or(Test-Path -LiteralPath (Join-Path $f.stage 'YeManCC/YeManTaskViewHelper.exe'))){throw 'Separate helper is still required'}}
Case 'timestamp-only change does not invalidate a byte-identical source' {param($f)(Get-Item -LiteralPath (Join-Path $f.project 'src/input.txt')).LastWriteTime=[DateTime]::Now.AddDays(1);$null=Assert-ExportBuildIdentity $f.project $f.work}
foreach($name in @('tools/steam_cssloader_manual_observation_selftest.cpp','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-contract.json','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-request-regression.cpp','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-request-regression.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-request-regression.test.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-native.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-parent.mjs','deps/ymcc-device-core-runtime/export-baseline/approved-20261023-compact34.json','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/manifest.json','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/NAudio.Core.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/NAudio.Wasapi.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.DirectInput.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.Direct3D9.dll','tools/ymcc_sim/domains/fan/fan_domain.ps1','tools/SharpDxCallbackSelfTest/Run-SelfTest.ps1','tools/HcManagerInitializationSelfTest/Run-SelfTest.ps1','native/input.txt','src/input.txt','public/input.txt','capabilities/HcControllerFeedbackCapabilityCatalog.h','capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs','InputHost/input.txt','FanLab/LightSetter/input.txt','decky-plugin/input.txt','hud-output.html','tools/monitor_hud_vue_build.mjs','tools/monitor-hud-export.ps1','tools/deploy-installed.ps1','tools/hc-device-runtime/build-runtime.py','tools/hc-device-runtime/verify-runtime.ps1','tools/hc-device-runtime/runtime-selftest.ps1','tools/hc-device-runtime/repair-package.py','tools/hc-application-dependency-trim/Program.cs','tools/hc-application-dependency-trim/test-com-gate.ps1','PowerControl/settings-baseline.json','PowerControl/settings-defaults-v1.json','tools/hc-export-baseline-guard.ps1','tools/hc-export-source-contract.ps1','deps/ymcc-device-core-runtime/export-baseline.lock.json','deps/ymcc-device-core-runtime/export-baseline/inventory.json')){Case ('same-length source change rejected: '+$name) {param($f)Alter (Join-Path $f.project $name);Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_SOURCE_CHANGED:'}}
foreach($name in @('Native/YeManCC.exe','Native/InputHost/YeManInputHost.dll','Native/InputHost/HIDMaestro.Core.dll','Web/assets/current.js','Web/hud-output.html','Decky/plugins/ymcc-sidebar/dist/index.js','Decky/plugins/ymcc-sidebar/LICENSE.vue')){Case ('same-length built payload change rejected: '+$name) {param($f)Alter (Join-Path $f.work ('Build/App/'+$name));Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_BUILD_CHANGED:'}}
Case 'light setter payload changed after build rejects packaging' {param($f)Alter (Join-Path $f.project 'PowerControl/light-setter/YeManLightSetter.dll');Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_BUILD_CHANGED:'}
Case 'independent lighting core changed after build rejects packaging' {param($f)Alter (Join-Path $f.project 'PowerControl/light-setter/YeManLightDeviceCore.dll');Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_BUILD_CHANGED:'}
Case 'complete runnable Release matches the ZIP tree' {param($f)$dest=Join-Path $f.work 'Release';New-Item -ItemType Directory -Path $dest|Out-Null;Copy-Item -Path (Join-Path $f.stage '*') -Destination $dest -Recurse;Assert-ExportPublishedTree $dest $f.stage $f.build}
Case 'preliminary stage may defer InputHost but final Release cannot omit it' {param($f)$file=Join-Path $f.stage 'PowerControl/feature-assets/virtual-gamepad/YeManInputHost.dll';[IO.File]::Delete($file);Assert-ExportStagedBuild $f.stage $f.build -BeforeCompletePackage;Reject {Assert-ExportStagedBuild $f.stage $f.build} 'EXPORT_STAGE_MISSING:'}
Case 'runnable Release missing a complete-package dependency is rejected' {param($f)$dest=Join-Path $f.work 'Release';New-Item -ItemType Directory -Path $dest|Out-Null;Copy-Item -Path (Join-Path $f.stage '*') -Destination $dest -Recurse;[IO.File]::Delete((Join-Path $dest 'PowerControl/runtime/owner.bin'));Reject {Assert-ExportPublishedTree $dest $f.stage $f.build} 'EXPORT_RELEASE_TREE_MISMATCH:'}
Case 'runnable Release with an extra old file is rejected' {param($f)$dest=Join-Path $f.work 'Release';New-Item -ItemType Directory -Path $dest|Out-Null;Copy-Item -Path (Join-Path $f.stage '*') -Destination $dest -Recurse;Text (Join-Path $dest 'YeManCC/old.dll') 'old';Reject {Assert-ExportPublishedTree $dest $f.stage $f.build} 'EXPORT_RELEASE_TREE_MISMATCH:'}
Case 'legacy build missing capture is not silently recaptured by exporter' {param($f)Move-Item -LiteralPath (Join-Path $f.work 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json') -Destination (Join-Path $f.root 'old-capture.json');Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_BUILD_IDENTITY_MISSING:'}
Case 'source modified during compilation rejects build completion' {param($f)
 $acceptedFile=Join-Path $f.work 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json'
 $acceptedHash=Get-ExportFileHash $acceptedFile
 Alter (Join-Path $f.project 'native/input.txt')
 Reject {Write-ExportBuildIdentity $f.project $f.work $f.before} 'EXPORT_SOURCE_CHANGED_DURING_BUILD:'
 $failures=@(Get-ChildItem -LiteralPath (Split-Path -Parent $acceptedFile) -Filter 'REJECTED-*.json' -File)
 if($failures.Count -ne 1){throw 'Rejected build did not preserve one separate failure receipt'}
 $failure=[IO.File]::ReadAllText($failures[0].FullName)|ConvertFrom-Json
 if($failure.status -cne 'rejected' -or $failure.accepted -ne $false -or $failure.reason -cne 'EXPORT_SOURCE_CHANGED_DURING_BUILD' -or @($failure.changed).Count -ne 1 -or $failure.changed[0].path -cne 'native/input.txt' -or @($failure.added).Count -or @($failure.removed).Count){throw 'Rejected build receipt did not identify the actual source change'}
 if($failure.sourceBeforeSha256 -cne (Get-ExportIndexHash $f.before) -or $failure.sourceAfterSha256 -cne (Get-ExportIndexHash @(Get-ExportSourceIndex $f.project)) -or @($failure.sourceBefore).Count -ne $f.before.Count -or @($failure.sourceAfter).Count -ne $f.before.Count){throw 'Rejected build source inventories are incomplete'}
 if((Get-ExportFileHash $acceptedFile) -cne $acceptedHash){throw 'Rejected build overwrote the previous accepted identity'}
}
foreach($name in @('YeManCC/YeManCC.exe','YeManCC/assets/current.js','PowerControl/feature-assets/virtual-gamepad/YeManInputHost.dll','PowerControl/light-setter/YeManLightSetter.dll','PowerControl/light-setter/YeManLightDeviceCore.dll','PowerControl/decky/plugins/ymcc-sidebar/dist/index.js')){Case ('staging copy drift rejected: '+$name) {param($f)Alter (Join-Path $f.stage $name);Reject {Assert-ExportStagedBuild $f.stage $f.build} 'EXPORT_STAGE_MISMATCH:'}}
Case 'stale extra Web chunk rejected before ZIP generation' {param($f)Text (Join-Path $f.stage 'YeManCC/assets/old.js') 'old';Reject {Assert-ExportStagedBuild $f.stage $f.build} 'EXPORT_STAGE_WEB_FILESET_MISMATCH:'}
foreach($name in @('YeManCC/YeManCC.exe','YeManCC/assets/current.js','PowerControl/decky/plugins/ymcc-sidebar/dist/index.js','PowerControl/runtime/owner.bin')){Case ('final ZIP byte drift rejected: '+$name) {param($f)Zip-Edit $f $name replace;Reject {Assert-ExportArchiveIdentity $f.zip $f.stage $f.build} 'EXPORT_ZIP_MISMATCH:'}}
Case 'missing dependency rejected in final ZIP' {param($f)Zip-Edit $f 'PowerControl/runtime/owner.bin' delete;Reject {Assert-ExportArchiveIdentity $f.zip $f.stage $f.build} 'EXPORT_ZIP_MISSING:'}
Case 'radial root entry source participates in build identity' {param($f)Alter (Join-Path $f.project 'radial-output.html');Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_SOURCE_CHANGED'}
Case 'missing radial document rejected in complete stage' {param($f)Remove-Item -LiteralPath (Join-Path $f.stage 'YeManCC/radial-output.html');Reject {Assert-ExportStagedBuild $f.stage $f.build} 'EXPORT_STAGE_MISSING:'}
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
Case 'second process cannot build/export into occupied workspace' {param($f)$mutex=Enter-ExportWorkspaceLock $f.work;try{$lib=(Join-Path $PSScriptRoot 'release-export-identity.ps1').Replace("'","''");$work=$f.work.Replace("'","''");$command='. ''{0}'';try{{$m=Enter-ExportWorkspaceLock ''{1}'';$m.ReleaseMutex();$m.Dispose();exit 1}}catch{{if($_.Exception.Message.StartsWith(''EXPORT_WORKSPACE_BUSY:'')){{exit 0}}else{{exit 2}}}}' -f $lib,$work;& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command $command;if($LASTEXITCODE-ne0){throw 'Workspace lock failed'}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
Case 'fingerprint order is ordinal and independent of input order or PowerShell version' {param($f)
 $rows=@(@{path='a/input';bytes=1;sha256='1'},@{path='A/input';bytes=2;sha256='2'},@{path='a-/input';bytes=3;sha256='3'},@{path='a_/input';bytes=4;sha256='4'})
 $ordered=Get-ExportIndexHash $rows;[Array]::Reverse($rows);if((Get-ExportIndexHash $rows)-ne$ordered){throw 'Input order changed identity'}
 $data=Join-Path $f.root 'order.json';$rows|ConvertTo-Json -Depth 3|Set-Content -LiteralPath $data -Encoding UTF8
 $lib=(Join-Path $PSScriptRoot 'release-export-identity.ps1').Replace("'","''");$data=$data.Replace("'","''")
 $command='. ''{0}'';$x=[IO.File]::ReadAllText(''{1}'')|ConvertFrom-Json;Get-ExportIndexHash @($x)' -f $lib,$data
 $legacy=(& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command $command|Out-String).Trim()
 if($LASTEXITCODE-ne0-or$legacy-ne$ordered){throw 'PowerShell 5.1 fingerprint drift'}
 $pwsh=Get-Command pwsh.exe -ErrorAction SilentlyContinue
 if($pwsh){$modern=(& $pwsh.Source -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command $command|Out-String).Trim();if($LASTEXITCODE-ne0-or$modern-ne$ordered){throw 'PowerShell 7 fingerprint drift'}}
}
$packager=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'package-release.ps1'))
$buildScript=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'build-workspace.ps1'))
try{if($packager.IndexOf('$exportArchiveReceipt = Assert-ExportArchiveIdentity')-gt$packager.IndexOf('foreach ($path in $releaseItems) { Backup-ExportReleaseItem')){throw 'Publication too early'};if(-not$packager.Contains('Undo-ExportReleaseItems')-or-not$packager.Contains('Assert-ExportBuildIdentity')-or-not$buildScript.Contains('Write-ExportBuildIdentity')-or-not$buildScript.Contains('--out $DeckyBuild')){throw 'Missing production integration'};$results.Add(@{name='production exporter integrates isolated Decky/source capture/deferred publish/rollback';passed=$true})}catch{$results.Add(@{name='production integration';passed=$false;error=$_.Exception.Message})}
$failed=@($results|Where-Object {-not$_.passed})
$report=[ordered]@{status=$(if($failed.Count){'failed'}else{'passed'});cases=$results.Count;failed=$failed.Count;results=@($results.ToArray());productionApplicationChanged=$false;realReleaseExecuted=$false;fixtureRoot=$ValidationRoot}
$report|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $ValidationRoot 'FULL-EXPORTER-SELFTEST.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 7
if($failed.Count){throw 'Exporter regressions failed'}