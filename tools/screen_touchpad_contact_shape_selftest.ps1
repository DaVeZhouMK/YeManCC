[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [string]$CursorSourceDirectory,
 [switch]$ExpectBaseline,
 [string]$ExpectedCursorSHA256,
 [string]$ExpectedOwnerSHA256
)
$ErrorActionPreference='Stop'
$shapeProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$shapeAnalysis=[IO.Path]::GetFullPath('G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261016-094240')
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\','/')
if(!(Split-Path $OutputDirectory -Leaf).StartsWith('Shape-') -or !(Split-Path $OutputDirectory -Parent).Equals($shapeAnalysis,[StringComparison]::OrdinalIgnoreCase)){throw 'Output must be a direct Shape-* child of the authorized analysis directory.'}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new Shape-* directory; prior reports and raw execution evidence are preserved.'}
if(!$CursorSourceDirectory){$CursorSourceDirectory=Join-Path $shapeProject 'native'}
$CursorSourceDirectory=[IO.Path]::GetFullPath($CursorSourceDirectory)
$shapeSource=Join-Path $PSScriptRoot 'screen_touchpad_contact_shape_selftest.cpp'
$shapeHarness=Join-Path $CursorSourceDirectory 'screen_touchpad_cursor_r1_simulation.cpp'
if(!(Test-Path -LiteralPath $shapeHarness)){$shapeHarness=Join-Path $PSScriptRoot 'screen_touchpad_cursor_r1_simulation.cpp'}
$shapeTrace=Join-Path $shapeAnalysis 'export-trace.json'
$shapeCursor=Join-Path $CursorSourceDirectory 'screen_touchpad_cursor.h'
$shapeVisibility=Join-Path $CursorSourceDirectory 'screen_touchpad_cursor_visibility.h'
$shapeDisplay=Join-Path $CursorSourceDirectory 'screen_touchpad_display_policy.h'
$shapeOwner=Join-Path $CursorSourceDirectory 'screen_touchpads.h'
$shapeJson=Join-Path $shapeProject 'deps/json/json.hpp'
$shapeInputs=@($shapeSource,$PSCommandPath,$shapeHarness,$shapeCursor,$shapeVisibility,$shapeDisplay,$shapeTrace,$shapeJson,$shapeOwner)
function Get-ShapeHashes {@($shapeInputs|ForEach-Object{[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})}
$shapeIdentity=Get-ShapeHashes
$shapeFeature=(Get-Content -LiteralPath $shapeCursor -Raw).Contains('cursorDeckContactShapeBridgeShows')
if($ExpectBaseline -and $shapeFeature){throw 'Baseline requires frozen policy15 without contact-shape bridge.'}
if(!$ExpectBaseline -and !$shapeFeature){throw 'Candidate contact-shape implementation is not present yet.'}
$shapeCursorHash=($shapeIdentity|Where-Object{$_.path -eq $shapeCursor}).sha256
$shapeOwnerHash=($shapeIdentity|Where-Object{$_.path -eq $shapeOwner}).sha256
if($ExpectedCursorSHA256 -and $shapeCursorHash -ne $ExpectedCursorSHA256){throw 'Cursor identity does not match the requested candidate.'}
if($ExpectedOwnerSHA256 -and $shapeOwnerHash -ne $ExpectedOwnerSHA256){throw 'Outer owner identity does not match the requested candidate.'}
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$shapeFrozenDirectory=Join-Path $OutputDirectory 'Frozen'
$shapeIdentityDirectory=Join-Path $OutputDirectory 'Identity'
$shapeBuildDirectory=Join-Path $OutputDirectory 'Build'
$shapeReportPath=Join-Path $OutputDirectory 'CONTACT-SHAPE-REPORT.md'
$shapeReceiptPath=Join-Path $OutputDirectory 'receipt.json'
$shapeResultPath=Join-Path $OutputDirectory 'simulation.json'
$shapeImports=Join-Path $OutputDirectory 'imports.txt'
$shapeBuildLog=Join-Path $OutputDirectory 'build.log'
$shapeStdout=Join-Path $OutputDirectory 'run.stdout.txt'
$shapeStderr=Join-Path $OutputDirectory 'run.stderr.txt'
$shapeExe=Join-Path $OutputDirectory 'contact-shape.exe'
$shapeReceipt=[ordered]@{
 schemaVersion=2;ok=$false;status='started';startedAtUtc=[DateTime]::UtcNow.ToString('o');baseline=[bool]$ExpectBaseline;
 contactShapeFeatureCompiled=$shapeFeature;sourceHashes=$shapeIdentity;cursorSHA256=$shapeCursorHash;outerOwnerSHA256=$shapeOwnerHash;
 outerOwnerHeaderCompiled=$false;outerOwnerIdentityOnly=$true;originalHarnessDownInvoked=$false;originalHarnessMainInvoked=$false;
 realWindowsCreated=0;realInputInjected=0;fieldPixelsVerified=$false;compressedFramesReconstructed=$false;
 outputDirectory=$OutputDirectory;artifacts=@{receipt='receipt.json';result='simulation.json';imports='imports.txt';executable='contact-shape.exe';sourceHashes='source-hashes.json';frozenInputs='frozen-build-sources.json';artifactHashes='artifact-hashes.json';fieldReplay='field-replay-results.json';endpoints='replay-endpoints.json';buildLog='build.log';stdout='run.stdout.txt';stderr='run.stderr.txt'}
}
function Save-ShapeReceipt { [IO.File]::WriteAllText($shapeReceiptPath,($shapeReceipt|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false)) }
Save-ShapeReceipt
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'source-hashes.json'),(ConvertTo-Json -InputObject @($shapeIdentity) -Depth 8),[Text.UTF8Encoding]::new($false))
$shapeResult=$null;$shapeException=$null;$shapeFrozen=@()
try{
 foreach($shapeDirectory in @($shapeFrozenDirectory,$shapeIdentityDirectory,$shapeBuildDirectory)){[IO.Directory]::CreateDirectory($shapeDirectory)|Out-Null}
 foreach($shapeInput in $shapeInputs){
  $shapeRole=if($shapeInput -eq $shapeOwner){'outer-owner-identity-only'}elseif($shapeInput -eq $shapeTrace){'recorded-replay-input'}elseif($shapeInput -eq $PSCommandPath){'runner-provenance'}else{'compile-input'}
  $shapeDestinationDirectory=if($shapeInput -eq $shapeOwner){$shapeIdentityDirectory}else{$shapeFrozenDirectory}
  $shapeDest=Join-Path $shapeDestinationDirectory ([IO.Path]::GetFileName($shapeInput))
  Copy-Item -LiteralPath $shapeInput -Destination $shapeDest
  $shapeHash=(Get-FileHash -LiteralPath $shapeDest -Algorithm SHA256).Hash
  if($shapeHash -ne ($shapeIdentity|Where-Object{$_.path -eq $shapeInput}).sha256){throw "Source changed while freezing: $shapeInput"}
  $shapeFrozen+=@([ordered]@{source=$shapeInput;frozenPath=$shapeDest;role=$shapeRole;sha256=$shapeHash})
 }
 $shapeReceipt.frozenBuildInputs=$shapeFrozen
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'frozen-build-sources.json'),(ConvertTo-Json -InputObject @($shapeFrozen) -Depth 8),[Text.UTF8Encoding]::new($false))
 Save-ShapeReceipt
 $shapeVcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'
 if(!(Test-Path -LiteralPath $shapeVcvars)){throw 'MSVC x64 BuildTools unavailable.'}
 $shapeObj=Join-Path $shapeBuildDirectory 'contact-shape.obj';$shapeCmd=Join-Path $shapeBuildDirectory 'build.cmd'
 $shapeFlag=if($shapeFeature){1}else{0}
 @"
@echo off
call "$shapeVcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /DNDEBUG /DUNICODE /D_UNICODE /DYMCC_TEST_CONTACT_SHAPE=$shapeFlag /I"$shapeFrozenDirectory" "$shapeFrozenDirectory/screen_touchpad_contact_shape_selftest.cpp" /Fo"$shapeObj" /Fe"$shapeExe" /link /NODEFAULTLIB:user32.lib /NODEFAULTLIB:gdi32.lib
if errorlevel 1 exit /b 1
dumpbin /imports "$shapeExe" > "$shapeImports"
if errorlevel 1 exit /b 1
"@ | Set-Content -LiteralPath $shapeCmd -Encoding ascii
 & $shapeCmd 2>&1 | Out-File -LiteralPath $shapeBuildLog -Encoding utf8
 $shapeReceipt.buildExitCode=$LASTEXITCODE
 if($shapeReceipt.buildExitCode -ne 0){throw 'Actual fixture compilation/import inspection failed.'}
 $shapeImportText=Get-Content -LiteralPath $shapeImports -Raw
 if($shapeImportText -match '(?im)^\s*(USER32|GDI32|DWMAPI|WIN32U|DINPUT8?|XINPUT[^\s]*|HID|POINTERINPUT|INPUTHOST|GAMEINPUT)\.dll\s*$'){throw 'Forbidden live graphics/input DLL import.'}
 $shapeReceipt.noLiveGraphicsOrInputDllImports=$true
 $shapeReceipt.executableSHA256=(Get-FileHash -LiteralPath $shapeExe -Algorithm SHA256).Hash
 $shapeReceipt.importsSHA256=(Get-FileHash -LiteralPath $shapeImports -Algorithm SHA256).Hash
 $shapeArgs=@(('"'+(Join-Path $shapeFrozenDirectory 'export-trace.json')+'"'),('"'+$shapeResultPath+'"'))
 $shapeProcess=Start-Process -FilePath $shapeExe -ArgumentList $shapeArgs -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $shapeStdout -RedirectStandardError $shapeStderr
 $shapeReceipt.exitCode=$shapeProcess.ExitCode
 if(!(Test-Path -LiteralPath $shapeResultPath)){throw 'Fixture did not produce simulation output.'}
 $shapeReceipt.simulationSHA256=(Get-FileHash -LiteralPath $shapeResultPath -Algorithm SHA256).Hash
 $shapeResult=Get-Content -LiteralPath $shapeResultPath -Raw|ConvertFrom-Json
 $shapeFailed=@($shapeResult.cases|Where-Object{!$_.ok})
 $shapeReceipt.caseCount=@($shapeResult.cases).Count;$shapeReceipt.passCount=@($shapeResult.cases|Where-Object{$_.ok}).Count
 $shapeReceipt.actualFailures=@($shapeFailed|ForEach-Object{$_.name});$shapeReceipt.simulationOk=[bool]$shapeResult.ok
 $shapeAfter=Get-ShapeHashes
 for($shapeIndex=0;$shapeIndex -lt $shapeIdentity.Count;$shapeIndex++){if($shapeIdentity[$shapeIndex].sha256 -ne $shapeAfter[$shapeIndex].sha256){throw ('Source/input changed during execution: '+$shapeIdentity[$shapeIndex].path)}}
 $shapeReceipt.sourcesUnchanged=$true
 if($shapeResult.error){throw ('Fixture exception: '+$shapeResult.error)}
 if($ExpectBaseline){
  if(@($shapeFailed|Where-Object{$_.category -ne 'policy16'}).Count){throw 'Baseline failed a preserved-behavior assertion; see actual output.'}
  foreach($shapeRequired in @('known-contact-stationary-shows-current-XY','field-known-contact-seq20-bridges-current-XY','field-known-contact-seq24-bridges-current-XY','field-known-contact-seq28-bridges-current-XY')){if($shapeReceipt.actualFailures -notcontains $shapeRequired){throw ('Baseline did not reproduce expected failure: '+$shapeRequired)}}
  if($shapeProcess.ExitCode -ne 1){throw 'Baseline must retain real failing assertions and exit code 1.'}
  $shapeReceipt.status='policy15-contact-shape-gap-reproduced'
 }else{
  if(!$shapeResult.ok -or $shapeProcess.ExitCode -ne 0){throw 'Candidate assertions failed; actual failure output is preserved.'}
  $shapeReceipt.status='contact-shape-candidate-tests-passed'
 }
 $shapeReceipt.ok=$true
}catch{$shapeException=$_.Exception.Message;$shapeReceipt.status='failed';$shapeReceipt.error=$shapeException}
finally{
 $shapeReceipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o')
 if($shapeResult -and $shapeResult.fieldReplay){
  [IO.File]::WriteAllText((Join-Path $OutputDirectory 'field-replay-results.json'),($shapeResult.fieldReplay|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $OutputDirectory 'replay-endpoints.json'),(ConvertTo-Json -InputObject @($shapeResult.fieldReplay.rows) -Depth 12),[Text.UTF8Encoding]::new($false))
 }
 Save-ShapeReceipt
 $shapeBody=New-Object Text.StringBuilder
 [void]$shapeBody.AppendLine('# Contact shape offline API-substitution test')
 [void]$shapeBody.AppendLine('')
 [void]$shapeBody.AppendLine(('Status: **'+$shapeReceipt.status+'**. Actual assertions: '+$shapeReceipt.passCount+'/'+$shapeReceipt.caseCount+'. Baseline failures stay failed in raw simulation.json (exe exit=1); expected-baseline acceptance is not a candidate pass.'))
 [void]$shapeBody.AppendLine('')
 [void]$shapeBody.AppendLine(('Compiled cursor SHA256: '+$shapeCursorHash+'. Outer owner identity SHA256: '+$shapeOwnerHash+'.'))
 [void]$shapeBody.AppendLine('')
 [void]$shapeBody.AppendLine('Own raw contact setup is used; original harness down/main are not called. Cursor/visibility/display headers are compiled from the retained Frozen directory. Identity/screen_touchpads.h is an identity-only snapshot: the surrounding owner is substituted, not compiled from that full header. The owner invariant assertion checks only substitute contact down/mode state, not the real Host/report pipeline. No real User32/GDI/input, HWND, hooks, device or system cursor.')
 [void]$shapeBody.AppendLine('')
 [void]$shapeBody.AppendLine('Field data is compressed first/last/min/max, not a frame sequence. Only recorded first/last points are replayed. Last-endpoint Contact provenance is unknown and is modeled as Periodic. Preparation rereads hold the substitute sample; original preparation-time state is unknown. Modeled visibility is not proof of pixels, continuity between samples, or a real-device fix.')
 [void]$shapeBody.AppendLine('')
 if($shapeResult -and $shapeResult.fieldReplay){
  [void]$shapeBody.AppendLine('| Contact seq | observed delay to first SHOWING (ms) | recorded helper | modeled helper at Contact | modeled current XY |')
  [void]$shapeBody.AppendLine('| --- | ---: | --- | --- | --- |')
  foreach($shapeWindow in $shapeResult.fieldReplay.contactWindows){[void]$shapeBody.AppendLine(('| '+$shapeWindow.contactSequence+' | '+$shapeWindow.observedDeltaMs+' | '+$shapeWindow.recordedHelperVisibleAtContact+' | '+$shapeWindow.modeledHelperVisibleAtContact+' | '+$shapeWindow.modeledAtCurrentXY+' |'))}
 }
 [void]$shapeBody.AppendLine('')
 [void]$shapeBody.AppendLine('Persistent raw artifacts (fresh execution, never reconstructed from older Markdown):')
 foreach($shapeFile in @('receipt.json','simulation.json','contact-shape.exe','imports.txt','source-hashes.json','frozen-build-sources.json','artifact-hashes.json','field-replay-results.json','replay-endpoints.json','build.log','run.stdout.txt','run.stderr.txt')){[void]$shapeBody.AppendLine(('- ['+$shapeFile+']('+(Join-Path $OutputDirectory $shapeFile)+')'))}
 [void]$shapeBody.AppendLine('')
 [void]$shapeBody.AppendLine('Frozen contains byte-preserved compile inputs, runner provenance and replay input; Identity contains the uncompiled outer header. Build contains the actual command and object. The artifact manifest hashes every retained file except itself. All prior Shape-* runs are unchanged. No temporary executable is discarded by this runner.')
 if($shapeException){[void]$shapeBody.AppendLine('');[void]$shapeBody.AppendLine(('Actual failure: '+$shapeException))}
 [IO.File]::WriteAllText($shapeReportPath,$shapeBody.ToString(),[Text.UTF8Encoding]::new($false))
 $shapeManifestPath=Join-Path $OutputDirectory 'artifact-hashes.json'
 $shapePrefix=$OutputDirectory+[IO.Path]::DirectorySeparatorChar
 $shapeManifestFiles=@(Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File | Where-Object{$_.FullName -ne $shapeManifestPath} | Sort-Object FullName | ForEach-Object{[ordered]@{relativePath=$_.FullName.Substring($shapePrefix.Length).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
 $shapeManifest=[ordered]@{schemaVersion=1;createdAtUtc=[DateTime]::UtcNow.ToString('o');manifestExcludesItself=$true;files=$shapeManifestFiles}
 [IO.File]::WriteAllText($shapeManifestPath,($shapeManifest|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
}
$shapeReceipt|ConvertTo-Json -Depth 5
if($shapeException){throw $shapeException}