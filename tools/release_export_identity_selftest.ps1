[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[switch]$PreMainOnly,[string]$PreMainBindingReference = '',[string]$CaseFilter = '')
$ErrorActionPreference='Stop'
if($PreMainOnly -and $CaseFilter){throw 'CaseFilter is for original exporter cases; use PreMainOnly independently.'}
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Force -Path $ValidationRoot|Out-Null
. (Join-Path $PSScriptRoot 'release-export-identity.ps1')
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$results=[Collections.Generic.List[object]]::new()
$script:exportModeChecks=[Collections.Generic.List[object]]::new()
function Text([string]$Path,[string]$Value){New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($Path))|Out-Null;[IO.File]::WriteAllText($Path,$Value,[Text.UTF8Encoding]::new($false))}
function Fixture {
 $root=Join-Path $ValidationRoot ('f-'+[Guid]::NewGuid().ToString('N'));$project=Join-Path $root $(if($PreMainOnly){'Mainline/YeManCC-source/YeManCC'}else{'source'});$work=Join-Path $root 'workspace';$stage=Join-Path $work 'Build/Package/CompleteRoot'
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
function Invoke-ExportModeConsumerChecks {
 $script:exportModeChecks=[Collections.Generic.List[object]]::new()
 function Read-ModeAst([string]$Name){
  $tokens=$null;$errors=$null
  $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $Name),[ref]$tokens,[ref]$errors)
  if(@($errors).Count){throw ('Production mode source parse failed: '+$Name)}
  return $ast
 }
 function Find-ModeStatement($Ast,[string]$Literal,[string]$ConditionVariable){
  $matches=@($Ast.FindAll({param($statement)
   $statement -is [Management.Automation.Language.IfStatementAst] -and
   @($statement.Clauses[0].Item1.FindAll({param($node)$node -is [Management.Automation.Language.VariableExpressionAst] -and $node.VariablePath.UserPath -ceq $ConditionVariable},$true)).Count -gt 0 -and
   @($statement.FindAll({param($node)$node -is [Management.Automation.Language.StringConstantExpressionAst] -and $node.Value -ceq $Literal},$true)).Count -gt 0
  },$true))
  if($matches.Count -ne 1){throw ('Expected one production mode statement: '+$Literal)}
  return $matches[0].Extent.Text
 }
 $packageAst=Read-ModeAst 'package-release.ps1'
 $deployAst=Read-ModeAst 'deploy-installed.ps1'
 $assignment=@($packageAst.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -ceq '$TestExport'})
 if($assignment.Count -ne 1){throw 'Expected one production TestExport assignment'}
 $guardText=(Find-ModeStatement $packageAst 'Choose ordinary export or explicit FullAcceptance.' 'TestExport')+"`n"+(Find-ModeStatement $packageAst 'FastRelease is separate from TestExport and FullAcceptance.' 'FastRelease')+"`n"+$assignment[0].Extent.Text
 $guard=[scriptblock]::Create($guardText)
 $marker=[scriptblock]::Create((Find-ModeStatement $packageAst 'mainline-local-test-export' 'TestExport'))
 $consumerText=Find-ModeStatement $deployAst 'TEST_EXPORT_DEPLOY_DECLARATION_INVALID' 'testExportMarker'
 $f=Fixture
 $WorkspaceRoot=$f.work;$completeStaging=$f.stage;$testExportRoot=$f.stage
 $testExportMarker=Join-Path $f.stage 'YeManCC/test-export.json';$exportBuildIdentity=$f.build
 $consumerPath=Join-Path $f.root 'mode-consumer/consumer.ps1'
 Text $consumerPath $consumerText
 # Only formal verification is a recording port; ordinary admission consumes
 # the original archive identity and declaration checks without replacements.
 $portScript='param([string]$Stage,[string]$WorkspaceRoot)' + "`n" + 'if($Stage -cne ''Verify''){throw ''Unexpected formal verification stage''}' + "`n" + '[IO.File]::WriteAllText((Join-Path $WorkspaceRoot ''formal-verify-port.txt''),''Verify'')'
 Text (Join-Path $f.root 'mode-consumer/decky_update_release_gate.ps1') $portScript
 $formalPort=Join-Path $f.work 'formal-verify-port.txt'
 $zip=Join-Path $f.work 'Release/Packages/YeManCC.zip'
 New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($zip))|Out-Null
 function Bind-ModeArchive {
  # Reset only this fixture's previously generated identity for the next variant.
  $ownedIdentity=Join-Path $completeStaging 'YeManCC/export-identity.json'
  if([IO.File]::Exists($ownedIdentity)){[IO.File]::Delete($ownedIdentity)}
  $null=Write-ExportPackageIdentity $completeStaging $exportBuildIdentity
  if([IO.File]::Exists($zip)){[IO.File]::Delete($zip)}
  [IO.Compression.ZipFile]::CreateFromDirectory($completeStaging,$zip)
 }
 function Run-Mode([string]$Name,[bool]$Fast,[bool]$Full,[bool]$ExplicitTest,[string]$GuardError=''){
  $FastRelease=$Fast;$FullAcceptance=$Full;$TestExport=$ExplicitTest
  $errorText='';try{. $guard}catch{$errorText=$_.Exception.Message}
  if($GuardError){
   if($errorText -cne $GuardError){throw ('Original mode guard mismatch: '+$Name+' / '+$errorText)}
   $script:exportModeChecks.Add(@{mode=$Name;originalGuardRejected=$true;archiveExecuted=$false});return
  }
  if($errorText){throw $errorText}
  if([IO.File]::Exists($testExportMarker)){[IO.File]::Delete($testExportMarker)}
  if([IO.File]::Exists($formalPort)){[IO.File]::Delete($formalPort)}
  . $marker
  Bind-ModeArchive
  & $consumerPath|Out-Null
  $declared=[IO.File]::Exists($testExportMarker);$formal=[IO.File]::Exists($formalPort)
  $ordinary=$env:YEMAN_TEST_EXPORT -ceq '1'
  $script:exportModeChecks.Add(@{mode=$Name;declarationPresent=$declared;ordinaryAdmission=$ordinary;formalVerifyPortInvoked=$formal;completeArchiveIdentityConsumed=$declared;passed=($ordinary -eq (-not $Full) -and $declared -eq (-not $Full) -and $formal -eq $Full)})
  if($Full){if($ordinary -or $declared -or -not $formal){throw 'FullAcceptance incorrectly admitted as ordinary export'}}
  elseif(-not $ordinary -or -not $declared -or $formal){throw ('Ordinary export mode lacks archive-bound declaration: '+$Name)}
 }
 $savedTestExport=$env:YEMAN_TEST_EXPORT
 try{
  Run-Mode 'ordinary-default' $false $false $false
  Run-Mode 'ordinary-explicit-TestExport' $false $false $true
  Run-Mode 'fast-ordinary' $true $false $false
  Run-Mode 'explicit-FullAcceptance' $false $true $false
  Run-Mode 'reject-TestExport-plus-FullAcceptance' $false $true $true 'Choose ordinary export or explicit FullAcceptance.'
  Run-Mode 'reject-FastRelease-plus-TestExport' $true $false $true 'FastRelease is separate from TestExport and FullAcceptance.'
  Run-Mode 'reject-FastRelease-plus-FullAcceptance' $true $true $false 'FastRelease is separate from TestExport and FullAcceptance.'
  $TestExport=$true;$FastRelease=$false;. $marker
  $declaration=[IO.File]::ReadAllText($testExportMarker)|ConvertFrom-Json
  $declaration.formalReleaseAccepted=$true
  Text $testExportMarker ($declaration|ConvertTo-Json -Depth 5)
  Bind-ModeArchive
  Reject {& $consumerPath|Out-Null} 'TEST_EXPORT_DEPLOY_DECLARATION_INVALID'
  $script:exportModeChecks.Add(@{mode='reject-archive-bound-invalid-declaration';passed=$true;realArchiveIdentityUsed=$true})
  . $marker;Bind-ModeArchive
  $declaration=[IO.File]::ReadAllText($testExportMarker)|ConvertFrom-Json
  $declaration.fixtureValidation='CACHED_REFERENCE'
  Text $testExportMarker ($declaration|ConvertTo-Json -Depth 5)
  Reject {& $consumerPath|Out-Null} 'EXPORT_ZIP_MISMATCH: YeManCC/test-export.json'
  $script:exportModeChecks.Add(@{mode='reject-unbound-declaration-change';passed=$true;realArchiveIdentityUsed=$true})
 }finally{$env:YEMAN_TEST_EXPORT=$savedTestExport}
}
function Case([string]$CaseName,[scriptblock]$Action){if($CaseFilter -and $CaseName -cne $CaseFilter){return};$f=Fixture;try{& $Action $f;$results.Add(@{name=$CaseName;passed=$true})}catch{$results.Add(@{name=$CaseName;passed=$false;error=$_.Exception.Message})}}

if($PreMainOnly){
 . (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1')
 $script:preMainControls=[Collections.Generic.List[object]]::new()
 function git {if($args -contains 'rev-parse'){$args[1]}else{'main'};$global:LASTEXITCODE=0}
 function Original-Block([string]$File,[string]$Begin,[string]$End){
   $text=[IO.File]::ReadAllText((Join-Path $PSScriptRoot $File));$i=$text.IndexOf($Begin,[StringComparison]::Ordinal);$j=$text.IndexOf($End,[StringComparison]::Ordinal)
   if($i -lt 0 -or $j -le $i -or $text.IndexOf($Begin,$i+1,[StringComparison]::Ordinal) -ge 0){throw 'Exact original integration block required'}
   $span=$text.Substring($i+$Begin.Length,$j-$i-$Begin.Length)
   $spanRoot=Join-Path $ValidationRoot 'original-stage-spans/tools';New-Item -ItemType Directory -Force -Path $spanRoot|Out-Null
   Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1') -Destination (Join-Path $spanRoot 'custom_steam_library_build_guard.ps1') -Force
   $spanPath=Join-Path $spanRoot (([IO.Path]::GetFileNameWithoutExtension($File))+'-'+$i+'.ps1')
   Text $spanPath $span
   return $spanPath
 }
 $script:workerStart=Original-Block 'build-workspace.ps1' '# CSL_PREMAIN_BEGIN: real producer before main, with one later package consumer.' '# CSL_PREMAIN_END'
 $script:workerPending=Original-Block 'build-workspace.ps1' '# CSL_PREMAIN_PENDING_BEGIN' '# CSL_PREMAIN_PENDING_END'
 $script:workerBind=Original-Block 'build-workspace.ps1' '# CSL_PREMAIN_BIND_BEGIN' '# CSL_PREMAIN_BIND_END'
 $script:workerSelect=Original-Block 'package-release.ps1' '# CSL_PREMAIN_SELECT_BEGIN' '# CSL_PREMAIN_SELECT_END'
 $script:workerConsume=Original-Block 'package-release.ps1' '# CSL_PREMAIN_CONSUME_BEGIN' '# CSL_PREMAIN_CONSUME_END'
 $script:workerValidate=Original-Block 'package-release.ps1' '# CSL_PREMAIN_VALIDATE_BEGIN' '# CSL_PREMAIN_VALIDATE_END'
 $script:workerReferenceReceipt=Original-Block 'package-release.ps1' '# CSL_PREMAIN_REFERENCE_RECEIPT_BEGIN' '# CSL_PREMAIN_REFERENCE_RECEIPT_END'
 $mainText=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'build-workspace.ps1'))
 $calls=@($mainText -split "\r?\n"|Where-Object{$_.Trim() -ceq '& cmd.exe /d /c (Join-Path $ProjectRoot ''native\build_native.bat'')'})
 if($calls.Count -ne 1){throw 'One original native invocation required'};$script:nativeInvocation=Join-Path $ValidationRoot 'original-stage-spans/tools/native-invocation.ps1';Text $script:nativeInvocation $calls[0]
 function Prepare-PremainFixture($f){
   foreach($name in @('custom_steam_library_build_guard.ps1','build-custom-steam-library-worker.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $f.project ('tools/'+$name)) -Force}
   foreach($name in @('steam_artwork_lab.cpp','steam_artwork_lab.rc','fixture-local.h')){Text (Join-Path $f.project ('native/custom-steam-library/'+$name)) ('inert-'+$name)}
   foreach($name in @('native/game_detection_blacklist_entries.h','native/process_efficiency.h','deps/json/json.hpp','CustomSteamLibrary/assets/custom-steam-library.ico')){Text (Join-Path $f.project $name) ('inert-'+$name)}
   Text (Join-Path $f.project 'CustomSteamLibrary/SteamArtworkLab.exe') 'inert-old-worker'
   Text (Join-Path $f.project 'CustomSteamLibrary/CustomSteamLibrary.exe') 'inert-old-host'
   Text (Join-Path $f.project 'CustomSteamLibrary/package-manifest.json') '{"packageVersion":"0.0.33","generatedAt":"inert-initial","backgroundMediaProtocol":1,"backgroundCatalogProtocol":1,"backgroundIdentityProtocol":1,"backgroundVideoQualityPolicy":3,"backgroundManualProtocol":1,"backgroundVideoSelectionProtocol":1,"sourceBuild":{"host":"inert-host","worker":"old"},"fileIndex":[{"path":"SteamArtworkLab.exe","bytes":0,"sha256":"old"},{"path":"CustomSteamLibrary.exe","bytes":0,"sha256":"old"}]}'
   for($n=0;$n -lt 64;$n++){
     try{$null=@(Get-CustomSteamLibrarySourceIndex $f.project host);break}catch{
       $message=$_.Exception.Message;$prefix='Missing host build input: '
       if(-not $message.StartsWith($prefix)){throw}
       $rel=$message.Substring($prefix.Length);$target=[IO.Path]::GetFullPath((Join-Path $f.project $rel))
       if(-not $target.StartsWith($f.project+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Inert host input escaped original fixture'}
       Text $target ('inert-original-host-input-'+$rel)
     }
   }
   $tools=Join-Path $f.root 'selected-tools';New-Item -ItemType Directory -Path $tools|Out-Null
   foreach($name in @('cl.exe','link.exe','rc.exe')){Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/where.exe') -Destination (Join-Path $tools $name)}
   $f.tools=$tools;$f.events=Join-Path $f.root 'events.txt'
   Text (Join-Path $f.project 'native/build_native.bat') "@echo off`r`necho main>>`"%YMCC_PREMAIN_EVENTS%`"`r`nexit /b 0`r`n"
   $batch=@(
     '@echo off','setlocal','set "OUTDIR=%~1"','echo worker>>"%YMCC_PREMAIN_EVENTS%"',
     'if "%YMCC_PREMAIN_FORCE_FAIL%"=="1" (',' echo steam_artwork_lab.cpp^(1^): error C3861: controlled_missing_symbol',' exit /b 7',')',
     '"%YMCC_CSL_TOOL_HOST%" -NoLogo -NoProfile -NonInteractive -File "%YMCC_CSL_TOOL_SCRIPT%" -CaptureCompilerTools "%OUTDIR%\compiler-tools-before.json"',
     'if errorlevel 1 exit /b 1','echo INERT-COMPILER-CONTROL-ONLY>"%OUTDIR%\SteamArtworkLab.exe"',
     '"%YMCC_CSL_TOOL_HOST%" -NoLogo -NoProfile -NonInteractive -File "%YMCC_CSL_TOOL_SCRIPT%" -CaptureCompilerTools "%OUTDIR%\compiler-tools-after.json"','exit /b %errorlevel%'
   ) -join "`r`n"
   Text (Join-Path $f.project 'native/custom-steam-library/build_worker.bat') ($batch+"`r`n")
 }
 function Inert-HostPublish($f){
   [IO.File]::AppendAllText($f.events,"host`r`n")
   $root=Join-Path $f.project 'CustomSteamLibrary';$cslHostPayload=Join-Path $root 'CustomSteamLibrary.exe'
   Text $cslHostPayload ('INERT-HOST-PUBLISH-'+[Guid]::NewGuid().ToString('N'))
   $m=Get-Content -LiteralPath (Join-Path $root 'package-manifest.json') -Raw|ConvertFrom-Json
   $hp=Get-CslStagePin $cslHostPayload;$m.sourceBuild.host=$hp.sha256
   $m.sourceBuild|Add-Member -NotePropertyName hostSources -NotePropertyValue @(Get-CustomSteamLibrarySourceIndex $f.project host) -Force
   $entry=@($m.fileIndex|Where-Object{$_.path -ceq 'CustomSteamLibrary.exe'});$entry[0].bytes=$hp.bytes;$entry[0].sha256=$hp.sha256
   $m.generatedAt=[DateTimeOffset]::Now.ToString('o')
   $m|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $root 'package-manifest.json') -Encoding UTF8
 }
 function Calls($f,[string]$Name){if(-not(Test-Path -LiteralPath $f.events)){return 0};return @((Get-Content -LiteralPath $f.events)|Where-Object{$_ -ceq $Name}).Count}
 Case 'pre-main worker failure prevents original native invocation' {param($f)
   Prepare-PremainFixture $f;$ProjectRoot=$f.project;$WorkspaceRoot=$f.work;$exportWorkspaceLock=Enter-ExportWorkspaceLock $WorkspaceRoot
   $env:YMCC_PREMAIN_EVENTS=$f.events;$env:YMCC_PREMAIN_FORCE_FAIL='1';$oldPath=$env:PATH;$env:PATH=$f.tools+';'+$oldPath
   try {
     $failed=$false
     try{. $script:workerStart;. $script:nativeInvocation}catch{$failed=$true;$errorText=$_.Exception.Message}
     if(-not $failed -or (Calls $f worker) -ne 1 -or (Calls $f main) -ne 0 -or -not $errorText.StartsWith('Custom Steam Library compiler failed')){throw ('Worker error failed to stop original main: '+$errorText)}
     $script:preMainControls.Add(@{name='worker-error-main-zero';worker=1;main=0;originalError=$errorText;fixture=$f.root})
   }finally{$env:PATH=$oldPath;$env:YMCC_PREMAIN_FORCE_FAIL=$null;$exportWorkspaceLock.ReleaseMutex();$exportWorkspaceLock.Dispose()}
 }
 Case 'same-chain worker once and original identity drift rejection' {param($f)
   Prepare-PremainFixture $f;$ProjectRoot=$f.project;$WorkspaceRoot=$f.work;$exportWorkspaceLock=Enter-ExportWorkspaceLock $WorkspaceRoot
   $env:YMCC_PREMAIN_EVENTS=$f.events;$env:YMCC_PREMAIN_FORCE_FAIL=$null;$oldPath=$env:PATH;$env:PATH=$f.tools+';'+$oldPath
   try{
     if($null -ne (Get-CslWorkerForPackage $f.build $ProjectRoot $WorkspaceRoot)){throw 'Old Build unexpectedly shared a worker'}
     . $script:workerStart;$exportSourcesBefore=@(Get-ExportSourceIndex $ProjectRoot)
     . $script:nativeInvocation;if($LASTEXITCODE -ne 0){throw 'Inert original native command failed'}
     . $script:workerPending
     $exportBuildIdentity=Write-ExportBuildIdentity $ProjectRoot $WorkspaceRoot $exportSourcesBefore
     Reject {Get-CslWorkerForPackage $exportBuildIdentity $ProjectRoot $WorkspaceRoot} 'CSL_STAGE_BUILD_BIND_PENDING'
     $buildFile=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json';$newBytes=[IO.File]::ReadAllBytes($buildFile)
     $foreign=Get-Content -LiteralPath $buildFile -Raw|ConvertFrom-Json;$foreign|Add-Member -NotePropertyName foreignWriter -NotePropertyValue 'controlled-race'
     $foreign|ConvertTo-Json -Depth 9|Set-Content -LiteralPath $buildFile -Encoding UTF8;$foreignBytes=[IO.File]::ReadAllBytes($buildFile)
     Reject {. $script:workerBind} 'CSL_STAGE_BUILD_RECORD_CHANGED'
     if([Convert]::ToBase64String([IO.File]::ReadAllBytes($buildFile)) -cne [Convert]::ToBase64String($foreignBytes)){throw 'Concurrent record was overwritten'}
     [IO.File]::WriteAllBytes($buildFile,$newBytes)
     . $script:workerBind
     $preMainBuildIdentity=Assert-ExportBuildIdentity -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot
     . $script:workerSelect;if(-not $preMainSelection){throw 'New Build did not bind producer'}
     $variants=@('pending-build-consumption','full-record-concurrent-replacement');$file=Join-Path $f.project 'native/custom-steam-library/steam_artwork_lab.cpp';$saved=[IO.File]::ReadAllBytes($file);Alter $file
     Reject {Assert-ExportBuildIdentity $ProjectRoot $WorkspaceRoot} 'EXPORT_SOURCE_CHANGED'
     [IO.File]::WriteAllBytes($file,$saved);$variants+='whole-build-source-drift'
     foreach($item in @(@{name='worker-payload';path=$preMainWorker.publishedWorker.path},@{name='selected-tool';path=(Join-Path $f.tools 'cl.exe')})){
       $saved=[IO.File]::ReadAllBytes($item.path);Alter $item.path
       Reject {Get-CslWorkerForPackage $preMainBuildIdentity $ProjectRoot $WorkspaceRoot} 'CSL_STAGE_INPUT_CHANGED'
       [IO.File]::WriteAllBytes($item.path,$saved);$variants+=$item.name
     }
     $manifestPath=$preMainWorker.manifest.path;$savedManifest=[IO.File]::ReadAllBytes($manifestPath)
     $tampered=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
     $tampered|Add-Member -NotePropertyName nonHostTamper -NotePropertyValue 'must-reject'
     $tampered|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $manifestPath -Encoding UTF8
     Reject {Get-CslWorkerForPackage $preMainBuildIdentity $ProjectRoot $WorkspaceRoot} 'CSL_STAGE_NONHOST_MANIFEST_CHANGED'
     [IO.File]::WriteAllBytes($manifestPath,$savedManifest);$variants+='nonhost-manifest'
     $receiptPath=$preMainBuildIdentity.preMainWorker.path;$saved=[IO.File]::ReadAllBytes($receiptPath);Remove-Item -LiteralPath $receiptPath
     Reject {Get-CslWorkerForPackage $preMainBuildIdentity $ProjectRoot $WorkspaceRoot} 'CSL_STAGE_INPUT_MISSING'
     [IO.File]::WriteAllBytes($receiptPath,$saved);$variants+='missing-declared-proof'
     $references=[Collections.Generic.List[object]]::new();$recoveries=[Collections.Generic.List[object]]::new()
     $producerDirectory=[IO.Path]::GetDirectoryName($receiptPath)
     $producerFiles=@(Get-ChildItem -LiteralPath $producerDirectory -File|ForEach-Object{Get-CslStagePin $_.FullName})
     Inert-HostPublish $f
     try{throw 'CONTROLLED_AFTER_HOST_BEFORE_USE'}catch{if($_.Exception.Message -cne 'CONTROLLED_AFTER_HOST_BEFORE_USE'){throw}}
     $preMainBuildIdentity=Assert-ExportBuildIdentity $ProjectRoot $WorkspaceRoot
     . $script:workerSelect
     if(-not $preMainSelection -or $preMainSelection.packageManifest -or (Calls $f worker) -ne 1 -or (Calls $f main) -ne 1){throw 'Post-host pre-Use reentry required durable state or rebuilt worker/main'}
     $recoveries.Add(@{name='host-published-before-use-reentry';workerExecutions=0;mainExecutions=0;durableCheckpointRequired=$false})
     # Every actual Package attempt still performs the original host Publish.
     Inert-HostPublish $f;. $script:workerConsume;$references.Add($preMainWorkerReference)
     try{throw 'CONTROLLED_LATE_PACKAGE_FAILURE'}catch{if($_.Exception.Message -cne 'CONTROLLED_LATE_PACKAGE_FAILURE'){throw}}
     $preMainBuildIdentity=Assert-ExportBuildIdentity $ProjectRoot $WorkspaceRoot
     . $script:workerSelect
     if(-not $preMainSelection -or $preMainSelection.packageManifest){throw 'Late failure cannot reenter from immutable producer'}
     $recoveries.Add(@{name='late-package-failure-reentry';workerExecutions=0;mainExecutions=0})
     Inert-HostPublish $f;. $script:workerConsume;$references.Add($preMainWorkerReference)
     foreach($item in @(@{name='package-manifest-memory-pin';path=$preMainSelection.packageManifest.path},@{name='package-host-memory-pin';path=$preMainSelection.packageHost.path})){
       $saved=[IO.File]::ReadAllBytes($item.path)
       if($item.name -ceq 'package-manifest-memory-pin'){
         $m=Get-Content -LiteralPath $item.path -Raw|ConvertFrom-Json;$m.generatedAt='changed-during-this-package'
         $m|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $item.path -Encoding UTF8
       }else{Alter $item.path}
       Reject {. $script:workerValidate} 'CSL_STAGE_INPUT_CHANGED'
       [IO.File]::WriteAllBytes($item.path,$saved);$variants+=$item.name
     }
     . $script:workerValidate
     $preMainBuildIdentity=Assert-ExportBuildIdentity $ProjectRoot $WorkspaceRoot
     . $script:workerSelect;Inert-HostPublish $f;. $script:workerConsume;. $script:workerValidate
     $references.Add($preMainWorkerReference)
     $recoveries.Add(@{name='successful-same-build-idempotent-reference';workerExecutions=0;mainExecutions=0})
     foreach($reference in $references){if($reference.status -cne 'SHARED_REFERENCE' -or $reference.executedThisPackage -ne $false -or $reference.workerExecutionsThisPackage -ne 0 -or $reference.producerReceipt.sha256 -cne $preMainBuildIdentity.preMainWorker.sha256){throw 'Reference was counted as new worker execution or lost producer identity'}}
     # Exercise the exact existing final ZIP receipt attachment without running Package.
     $exportArchiveReceipt=[ordered]@{status='passed';buildId=$preMainBuildIdentity.buildId}
     . $script:workerReferenceReceipt
     if($exportArchiveReceipt.preMainWorkerReference.status -cne 'SHARED_REFERENCE' -or $exportArchiveReceipt.preMainWorkerReference.workerExecutionsThisPackage -ne 0){throw 'Existing archive receipt lost truthful reference metadata'}
     if((Calls $f worker) -ne 1 -or (Calls $f main) -ne 1 -or (Calls $f host) -ne 4){throw 'Idempotent chains changed execution counts or skipped host Publish'}
     foreach($pin in $producerFiles){Assert-CslStagePin $pin}
     if(@(Get-ChildItem -LiteralPath $producerDirectory -File).Count -ne $producerFiles.Count){throw 'Package wrote durable producer lifecycle state'}
     $script:preMainControls.Add(@{name='normal-one-each-and-rejections';worker=1;main=1;inertHostPublishes=4;originalBuildId=$preMainBuildIdentity.buildId;producerProof=$preMainBuildIdentity.preMainWorker;rejected=$variants;acceptedRecoveries=@($recoveries.ToArray());packageWorkerReferences=@($references.ToArray());durablePackageStateCreated=$false;fixture=$f.root;compilerPathsAreInertAliases=$true;realMsvcOrRcExecuted=$false;archiveReceiptAttachmentOnly=$true})
     if($PreMainBindingReference){
       $ref=Get-Content -LiteralPath $PreMainBindingReference -Raw -Encoding UTF8|ConvertFrom-Json
       foreach($pin in @($ref.pins)){Assert-CslStagePin $pin}
       foreach($fragment in @($ref.fragments)){
         $bytes=[IO.File]::ReadAllBytes($fragment.path);$slice=[byte[]]::new([int]$fragment.bytes)
         [Array]::Copy($bytes,[int]$fragment.offset,$slice,0,$slice.Length)
         $stream=[IO.MemoryStream]::new($slice,$false)
         try{if((Get-ExportHash $stream) -cne $fragment.sha256){throw 'Unchanged binding reference fragment differs'}}finally{$stream.Dispose()}
       }
       $original=Get-Content -LiteralPath $ref.originalReport.path -Raw -Encoding UTF8|ConvertFrom-Json
       if($original.status -cne 'passed' -or $original.cases -ne 2 -or $original.failed -ne 0){throw 'Original binding reference not passed'}
       $script:preMainControls.Add(@{name='unchanged-binding-and-original-mutex';status='SHARED_REFERENCE';executedThisRun=$false;reference=(Get-CslStagePin $PreMainBindingReference);scope=$ref.scope;excluded=$ref.excluded})
     }else{
     $firstBuildId=$preMainBuildIdentity.buildId;$firstProof=$preMainBuildIdentity.preMainWorker.path;$firstProofBytes=[IO.File]::ReadAllBytes($firstProof)
     # Actual inert chain A reaches the original Write but is deliberately not Completed.
     . $script:workerStart;$exportSourcesBefore=@(Get-ExportSourceIndex $ProjectRoot)
     . $script:nativeInvocation;. $script:workerPending
     $exportBuildIdentity=Write-ExportBuildIdentity $ProjectRoot $WorkspaceRoot $exportSourcesBefore
     $unboundA=$exportBuildIdentity;$buildFile=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json';$unboundABytes=[IO.File]::ReadAllBytes($buildFile)
     [IO.File]::WriteAllBytes((Join-Path $f.root 'UNBOUND-A-ORIGINAL.json'),$unboundABytes)
     if($unboundA.Contains('preMainWorker')){throw 'Unbound A unexpectedly contains a proof'}
     $priorPending=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/WorkerPremain/PENDING.json';$priorPendingNonce=$preMainWorker.nonce
     $priorPendingBytes=[IO.File]::ReadAllBytes($priorPending);$expectedPending=Get-CslStagePin $priorPending;$probeNonce=[Guid]::NewGuid().ToString('N')
     $exportWorkspaceLock.ReleaseMutex()
     try{
       Reject {Reset-CslPriorPendingForNewBuild $ProjectRoot $WorkspaceRoot $expectedPending $probeNonce} 'CSL_STAGE_ORIGINAL_LOCK_NOT_OWNED'
       if(-not ('YmccCslForeignMutexHolder' -as [type])){
         Add-Type -TypeDefinition (@(
           'using System;using System.Threading;',
           'public sealed class YmccCslForeignMutexHolder:IDisposable {',
           ' readonly ManualResetEvent ready=new ManualResetEvent(false),stop=new ManualResetEvent(false);readonly Thread thread;Exception error;',
           ' public YmccCslForeignMutexHolder(string name){thread=new Thread(()=>{try{using(var m=Mutex.OpenExisting(name)){if(!m.WaitOne(1500))throw new Exception("Foreign fixture could not own mutex");try{ready.Set();stop.WaitOne(3000);}finally{m.ReleaseMutex();}}}catch(Exception e){error=e;ready.Set();}});thread.IsBackground=true;thread.Start();if(!ready.WaitOne(2000)||error!=null)throw new Exception("Foreign mutex fixture failed",error);}',
           ' public void Dispose(){stop.Set();if(!thread.Join(2000))throw new Exception("Foreign fixture thread did not exit");ready.Dispose();stop.Dispose();}',
           '}'
         )-join "`n")
       }
       $mutexName='Local\YeManCC.Export.'+(Get-ExportTextHash ([IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\').ToLowerInvariant()))
       $foreignHolder=[YmccCslForeignMutexHolder]::new($mutexName)
       try{Reject {Reset-CslPriorPendingForNewBuild $ProjectRoot $WorkspaceRoot $expectedPending $probeNonce} 'CSL_STAGE_ORIGINAL_LOCK_NOT_OWNED'}finally{$foreignHolder.Dispose()}
     }finally{if(-not $exportWorkspaceLock.WaitOne(0)){throw 'Original test workspace lock reacquisition failed'}}
     if([Convert]::ToBase64String([IO.File]::ReadAllBytes($priorPending)) -cne [Convert]::ToBase64String($priorPendingBytes)){throw 'Unowned recovery modified pending'}
     $foreignPending=[Text.Encoding]::UTF8.GetString($priorPendingBytes)|ConvertFrom-Json;$foreignPending.project='G:\foreign-project'
     $foreignPending|ConvertTo-Json|Set-Content -LiteralPath $priorPending -Encoding UTF8;$foreignPendingBytes=[IO.File]::ReadAllBytes($priorPending);$foreignPin=Get-CslStagePin $priorPending
     Reject {Reset-CslPriorPendingForNewBuild $ProjectRoot $WorkspaceRoot $foreignPin $probeNonce} 'CSL_STAGE_PRIOR_PENDING_SCOPE_MISMATCH'
     if([Convert]::ToBase64String([IO.File]::ReadAllBytes($priorPending)) -cne [Convert]::ToBase64String($foreignPendingBytes)){throw 'Foreign pending changed'}
     [IO.File]::WriteAllBytes($priorPending,$priorPendingBytes)
     [IO.File]::AppendAllText($priorPending,' ');$changedPending=[IO.File]::ReadAllBytes($priorPending)
     Reject {Reset-CslPriorPendingForNewBuild $ProjectRoot $WorkspaceRoot $expectedPending $probeNonce} 'CSL_STAGE_INPUT_CHANGED'
     if([Convert]::ToBase64String([IO.File]::ReadAllBytes($priorPending)) -cne [Convert]::ToBase64String($changedPending)){throw 'Changed pending was not retained'}
     [IO.File]::WriteAllBytes($priorPending,$priorPendingBytes)
     # Actual replacement B archives A's marker, then its worker fails before main.
     $beforeFailWorker=Calls $f worker;$beforeFailMain=Calls $f main;$env:YMCC_PREMAIN_FORCE_FAIL='1'
     try{Reject {. $script:workerStart} 'Custom Steam Library compiler failed'}finally{$env:YMCC_PREMAIN_FORCE_FAIL=$null}
     if((Calls $f worker) -ne ($beforeFailWorker+1) -or (Calls $f main) -ne $beforeFailMain){throw 'Replacement B did not fail before main'}
     if([Convert]::ToBase64String([IO.File]::ReadAllBytes($buildFile)) -cne [Convert]::ToBase64String($unboundABytes)){throw 'Failed B changed unbound A Build'}
     $packageAfterB=Assert-ExportBuildIdentity $ProjectRoot $WorkspaceRoot
     Reject {Get-CslWorkerForPackage $packageAfterB $ProjectRoot $WorkspaceRoot} 'CSL_STAGE_BUILD_BIND_PENDING'
     $pendingB=[IO.File]::ReadAllBytes($priorPending);[IO.File]::WriteAllBytes((Join-Path $f.root 'PENDING-B-FAILED-PRODUCER.json'),$pendingB)
     $script:preMainControls.Add(@{name='actual-unbound-A-then-failed-B';worker=1;main=0;unboundBuildByteExact=$true;pendingStillRejectsLegacyFallback=$true;proofFieldsFabricated=$false})
     # C is a fresh normal producer in the same workspace, no cleanup or evidence deletion.
     . $script:workerStart;$exportSourcesBefore=@(Get-ExportSourceIndex $ProjectRoot)
     . $script:nativeInvocation;. $script:workerPending
     $exportBuildIdentity=Write-ExportBuildIdentity $ProjectRoot $WorkspaceRoot $exportSourcesBefore
     . $script:workerBind;$preMainBuildIdentity=Assert-ExportBuildIdentity $ProjectRoot $WorkspaceRoot
     if($preMainBuildIdentity.buildId -cne $firstBuildId -or $preMainBuildIdentity.preMainWorker.path -ieq $firstProof -or [Convert]::ToBase64String([IO.File]::ReadAllBytes($firstProof)) -cne [Convert]::ToBase64String($firstProofBytes)){throw 'Same-identity new producer lost distinct nonce or changed prior receipt'}
     . $script:workerSelect;Inert-HostPublish $f;. $script:workerConsume;. $script:workerValidate
     $null=Get-CslWorkerForPackage $preMainBuildIdentity $ProjectRoot $WorkspaceRoot
     if((Calls $f worker) -ne 4 -or (Calls $f main) -ne 3){throw 'Normal chains plus incomplete A / failed B have wrong execution counts'}
     $archives=@(Get-ChildItem -LiteralPath ([IO.Path]::GetDirectoryName($priorPending)) -Filter ($priorPendingNonce+'.aborted-*.json'))
     if($archives.Count -ne 1 -or [Convert]::ToBase64String([IO.File]::ReadAllBytes($archives[0].FullName)) -cne [Convert]::ToBase64String($priorPendingBytes)){throw 'Prior pending was not preserved by new Build under original workspace mutex'}
     $script:preMainControls.Add(@{name='same-build-id-new-nonce-second-chain';worker=1;main=1;totalInertWorkerCalls=4;totalInertMainCalls=3;buildId=$preMainBuildIdentity.buildId;firstProofUnchanged=$true;priorPendingArchivedByteExact=$true;originalWorkspaceMutexReused=$true;pendingRecoveryRejections=@('no-current-ownership','owned-by-other-thread','foreign-project','changed-bytes');secondProof=$preMainBuildIdentity.preMainWorker})
     }
   }finally{$env:PATH=$oldPath;$env:YMCC_PREMAIN_EVENTS=$null;$exportWorkspaceLock.ReleaseMutex();$exportWorkspaceLock.Dispose()}
 }
 $failed=@($results|Where-Object{-not $_.passed})
 $report=[ordered]@{status=$(if($failed.Count){'failed'}else{'passed'});cases=$results.Count;failed=$failed.Count;results=@($results.ToArray());controls=@($script:preMainControls.ToArray());scope='Two selected original exporter cases with idempotent Package parameters; exact candidate Start/Bind/Select/Use/final assertion/receipt attachment and original native invocation, inert BAT and host controls; no real main/host/Package/MSVC/RC';oldDefaultCasesRun=0}
 $report|ConvertTo-Json -Depth 14|Set-Content -LiteralPath (Join-Path $ValidationRoot 'PREMAIN-EXPORTER-SELFTEST.json') -Encoding UTF8
 $report|ConvertTo-Json -Depth 14
 if($failed.Count){throw 'Selected pre-main exporter regressions failed'}
 return
}

Case 'fixture orchestration source identity detects same-length changes without running fixtures' {param($f)
 $names=@('tools/ymcc_sim/global/decky-persistent-lifecycle/current-parent-global.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/baseline-environment.mjs','tools/ymcc_sim/global/decky-update-recovery/resource-execution.mjs','tools/ymcc_sim/optimization/release-baseline-reference.mjs','tools/ymcc_sim/optimization/release-resource-reference.mjs','tools/ymcc_sim/optimization/release-full-budget.ps1','tools/ymcc_sim/optimization/release-full-budget.mjs','tools/ymcc_sim/optimization/execution-budget.mjs')
 foreach($name in $names){$rows=@($f.before|Where-Object {$_.path-ceq$name});if($rows.Count-ne1){throw ('ORCHESTRATION_SOURCE_NOT_CAPTURED: '+$name)}}
 $old=Get-ExportIndexHash @($f.before)
 foreach($name in $names){$file=Join-Path $f.project $name;$before=[IO.FileInfo]::new($file).Length;Alter $file;if(([IO.FileInfo]::new($file)).Length-ne$before){throw 'CONTROL_LENGTH_CHANGED'}}
 $current=@(Get-ExportSourceIndex $f.project);$changed=@($current|Where-Object {$row=$_;$prior=@($f.before|Where-Object {$_.path-ceq$row.path});$prior.Count-eq1-and$prior[0].sha256-cne$row.sha256})
 if($changed.Count-ne9-or(Get-ExportIndexHash $current)-ceq$old){throw 'NINE_ORCHESTRATION_SOURCE_CHANGES_NOT_DETECTED'}
 foreach($name in $names){if(@($changed|Where-Object {$_.path-ceq$name}).Count-ne1){throw ('MISSING_CHANGED_ORCHESTRATION_SOURCE: '+$name)}}
 Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_SOURCE_CHANGED:'
}
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
foreach($name in @('tools/steam_cssloader_manual_observation_selftest.cpp','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-contract.json','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-request-regression.cpp','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-request-regression.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-request-regression.test.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-native.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-parent.mjs','deps/ymcc-device-core-runtime/export-baseline/approved-20261023-compact34.json','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/manifest.json','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/NAudio.Core.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/NAudio.Wasapi.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.DirectInput.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.Direct3D9.dll','tools/ymcc_sim/domains/fan/fan_domain.ps1','tools/SharpDxCallbackSelfTest/Run-SelfTest.ps1','tools/HcManagerInitializationSelfTest/Run-SelfTest.ps1','native/input.txt','src/input.txt','public/input.txt','capabilities/HcControllerFeedbackCapabilityCatalog.h','capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs','InputHost/input.txt','FanLab/LightSetter/input.txt','decky-plugin/input.txt','hud-output.html','tools/monitor_hud_vue_build.mjs','tools/monitor-hud-export.ps1','tools/deploy-installed.ps1','tools/hc-device-runtime/build-runtime.py','tools/hc-device-runtime/verify-runtime.ps1','tools/hc-device-runtime/runtime-selftest.ps1','tools/hc-device-runtime/repair-package.py','tools/hc-application-dependency-trim/Program.cs','tools/hc-application-dependency-trim/test-com-gate.ps1','PowerControl/settings-baseline.json','PowerControl/settings-defaults-v1.json','tools/hc-export-baseline-guard.ps1','tools/hc-export-source-contract.ps1','deps/ymcc-device-core-runtime/export-baseline.lock.json','deps/ymcc-device-core-runtime/export-baseline/inventory.json')){Case ('same-length source change rejected: '+$name) {param($f)
 if($name -ceq 'src/input.txt'){$originalSource=[IO.File]::ReadAllBytes((Join-Path $f.project $name))}
 Alter (Join-Path $f.project $name);Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_SOURCE_CHANGED:'
 if($name -ceq 'src/input.txt'){
  [IO.File]::WriteAllBytes((Join-Path $f.project $name),$originalSource)
  $policyRelative='PowerControl/GameTitlePolicy.json';$policy=Join-Path $f.project $policyRelative
  $policyBytes=[IO.File]::ReadAllBytes($policy);$policyHash=Get-ExportFileHash $policy
  $normal=@(Get-ExportSourceIndex $f.project);$rows=@($normal|Where-Object {$_.path -ceq $policyRelative})
  if($rows.Count -ne 1 -or $rows[0].bytes -ne $policyBytes.Length -or $rows[0].sha256 -cne $policyHash){throw 'Web policy must be captured exactly once with its actual bytes and hash'}
  $null=Assert-ExportBuildIdentity $f.project $f.work
  Alter $policy
  $mutated=@(Get-ExportSourceIndex $f.project);$changed=@($mutated|Where-Object {$_.path -ceq $policyRelative})
  if($changed.Count -ne 1 -or $changed[0].bytes -ne $policyBytes.Length -or $changed[0].sha256 -ceq $policyHash -or (Get-ExportIndexHash $normal) -ceq (Get-ExportIndexHash $mutated)){throw 'Same-length Web policy mutation must change the source digest'}
  Reject {Assert-ExportBuildIdentity $f.project $f.work} 'EXPORT_SOURCE_CHANGED:'
  [IO.File]::WriteAllBytes($policy,$policyBytes)
  $null=Assert-ExportBuildIdentity $f.project $f.work
  [IO.File]::Delete($policy)
  Reject {Get-ExportSourceIndex $f.project} ('EXPORT_SOURCE_MISSING: '+$policyRelative)
  $linkTarget=Join-Path $f.root 'policy-link-target';$null=New-Item -ItemType Directory -Path $linkTarget
  $null=New-Item -ItemType Junction -Path $policy -Target $linkTarget
  try{Reject {Get-ExportSourceIndex $f.project} ('EXPORT_REPARSE_POINT: '+$policyRelative)}finally{[IO.Directory]::Delete($policy)}
  [IO.File]::WriteAllBytes($policy,$policyBytes)
  $null=Assert-ExportBuildIdentity $f.project $f.work
  Write-Output 'GAME_TITLE_POLICY_SOURCE_CLOSURE_OK unique=1 sameLengthMutation=REJECTED missing=REJECTED reparse=REJECTED originalSourceAssertion=RETAINED'
 }
}}
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
if(-not $CaseFilter -or $CaseFilter -ceq 'production exporter integrates isolated Decky/source capture/deferred publish/rollback'){
try{if($packager.IndexOf('$exportArchiveReceipt = Assert-ExportArchiveIdentity')-gt$packager.IndexOf('foreach ($path in $releaseItems) { Backup-ExportReleaseItem')){throw 'Publication too early'};if(-not$packager.Contains('Undo-ExportReleaseItems')-or-not$packager.Contains('Assert-ExportBuildIdentity')-or-not$buildScript.Contains('Write-ExportBuildIdentity')-or-not$buildScript.Contains('--out $DeckyBuild')){throw 'Missing production integration'};Invoke-ExportModeConsumerChecks;$results.Add(@{name='production exporter integrates isolated Decky/source capture/deferred publish/rollback';passed=$true})}catch{$results.Add(@{name='production integration';passed=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace})}
}
$failed=@($results|Where-Object {-not$_.passed})
$selectionRejected=([bool]$CaseFilter -and $results.Count -eq 0)
$report=[ordered]@{status=$(if($selectionRejected){'selection-rejected'}elseif($failed.Count){'failed'}else{'passed'});cases=$results.Count;failed=$failed.Count;results=@($results.ToArray());caseFilter=$CaseFilter;selectionRejected=$selectionRejected;modeConsumerChecks=@($script:exportModeChecks.ToArray());productionApplicationChanged=$false;realReleaseExecuted=$false;fixtureRoot=$ValidationRoot}
$report|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $ValidationRoot 'FULL-EXPORTER-SELFTEST.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 7
if($selectionRejected){throw ('Unknown exporter selftest case: '+$CaseFilter)}
if($failed.Count){throw 'Exporter regressions failed'}