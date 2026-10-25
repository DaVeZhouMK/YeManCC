[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$CursorSourceDirectory,[string[]]$ExpectedFailures=@())
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$CursorSourceDirectory){$CursorSourceDirectory=Join-Path $project 'native'}
$CursorSourceDirectory=[IO.Path]::GetFullPath($CursorSourceDirectory)
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath (Join-Path $OutputDirectory 'receipt.json')){throw 'Use a fresh output directory.'}
$native=Join-Path $OutputDirectory 'FrozenNative'
New-Item -ItemType Directory -Force -Path $native|Out-Null
$receipt=[ordered]@{ok=$false;status='started';startedAtUtc=[DateTime]::UtcNow.ToString('o');scope='Real UI/BUS threads, PostMessage, GDI and HWND on private desktop; observation/clock/Host/Steam adapters';expectedFailures=$ExpectedFailures;sources=@();sourceInputs=@();systemInputInjected=$false;systemCursorManipulated=$false;visibleDesktopSwitched=$false;fieldFlickerVerified=$false;productDeployed=$false}
function SaveReceipt {$receipt|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
SaveReceipt
try{
 $queueInputs=@()
 foreach($name in @('screen_touchpad_cursor.h','screen_touchpad_cursor_visibility.h','screen_touchpad_display_policy.h')){
  $queueInputs+=@([ordered]@{source=(Join-Path $CursorSourceDirectory $name);name=$name})
 }
 $queueInputs+=@([ordered]@{source=(Join-Path $project 'deps/json/json.hpp');name='json.hpp'})
 $queueInputs+=@([ordered]@{source=(Join-Path $PSScriptRoot 'screen_touchpad_native_queue_selftest.cpp');name='screen_touchpad_native_queue_selftest.cpp'})
 $queueInputs+=@([ordered]@{source=$PSCommandPath;name='screen_touchpad_native_queue_selftest.ps1'})
 foreach($inputFile in $queueInputs){
  $sourceHash=(Get-FileHash -LiteralPath $inputFile.source -Algorithm SHA256).Hash
  Copy-Item -LiteralPath $inputFile.source -Destination (Join-Path $native $inputFile.name)
  $copyHash=(Get-FileHash -LiteralPath (Join-Path $native $inputFile.name) -Algorithm SHA256).Hash
  if($copyHash -ne $sourceHash){throw ('Source changed while freezing: '+$inputFile.source)}
  $receipt.sources+=@{name=$inputFile.name;sha256=$copyHash}
  $receipt.sourceInputs+=@{path=$inputFile.source;sha256=$sourceHash}
 }
 $cursorText=Get-Content -LiteralPath (Join-Path $native 'screen_touchpad_cursor.h') -Raw
 $motionGate= $cursorText -match 'inline void cursorResetDeckMotion'
 $legacy= !($cursorText -match 'inline void cursorRequestBeforeReport\(')
 $dispatchTickArgument= $cursorText -match 'cursorContactChanged\([^\)]*ULONGLONG\s+dispatchTick'
 $reportPulse= $cursorText -match 'std::atomic<ULONGLONG>\s+cursorDeckReportPulseTick'
 $defines=@()
 if($motionGate){$defines+='/DYMCC_CURSOR_MOTION_GATE_FIXTURE_ENABLED=1'}
 if($legacy){$defines+='/DYMCC_CURSOR_REPORT_ORDER_LEGACY_FIXTURE=1'}
 if($dispatchTickArgument){$defines+='/DYMCC_CURSOR_DECK_MOVE_TICK_FIXTURE_ENABLED=1'}
 if($reportPulse){$defines+='/DYMCC_CURSOR_DECK_MOVE_REPORT_PULSE_FIXTURE_ENABLED=1'}
 $receipt.legacyOrderFixture=$legacy;$receipt.dispatchTickArgumentAvailable=$dispatchTickArgument;$receipt.reportPulseAvailable=$reportPulse
 $receipt.compileDefines=$defines;$receipt.compiledFromFrozenSources=$true
 $queueOriginal='G:/YeManCC-Work/Build/Analysis/Touchpad-Move-Order-R13/Before/screen_touchpad_native_queue_selftest.cpp'
 if(!(Test-Path -LiteralPath $queueOriginal)){throw 'The frozen twenty-check queue fixture is required for R13 preservation checking.'}
 $originalText=Get-Content -LiteralPath $queueOriginal -Raw
 $originalNames=@([regex]::Matches($originalText,'check\("([^\"]+)"') | ForEach-Object {$_.Groups[1].Value})
 if($originalNames.Count -ne 20){throw 'Unexpected frozen original assertion count.'}
 $receipt.preservedOriginalNames=$originalNames
 $receipt.originalFixtureSha256=(Get-FileHash -LiteralPath $queueOriginal -Algorithm SHA256).Hash
 $cmd=Join-Path $OutputDirectory 'build.cmd';$exe=Join-Path $OutputDirectory 'native-cursor-queue.exe'
 $defineText=$defines -join ' '
 @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /DNDEBUG /DUNICODE /D_UNICODE $defineText /I"$native" "$native\screen_touchpad_native_queue_selftest.cpp" /Fo"$OutputDirectory\native-queue.obj" /Fe"$exe" /link user32.lib gdi32.lib
if errorlevel 1 exit /b 1
dumpbin /imports "$exe" > "$OutputDirectory\imports.txt"
if errorlevel 1 exit /b 1
"@|Set-Content -LiteralPath $cmd -Encoding ascii
 & $cmd 2>&1|Tee-Object -FilePath (Join-Path $OutputDirectory 'compile.log')
 if($LASTEXITCODE -ne 0){throw 'Native queue fixture compile failed.'}
 $report=Join-Path $OutputDirectory 'native-queue.json'
 $run=Start-Process -FilePath $exe -ArgumentList ('"'+$report+'"') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.txt')
 $receipt.exitCode=$run.ExitCode;$receipt.report=$report
 if(!(Test-Path -LiteralPath $report)){throw 'Native queue executable did not write a report.'}
 $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
 $actual=@($r.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name}|Sort-Object)
 $receipt.checks=@($r.cases).Count;$receipt.actualFailures=$actual;$receipt.simulationOk=[bool]$r.ok
 $receipt.isolation=$r|Select-Object hiddenDesktop,privateDesktopClosed,uiWorkerTerminated,inputDesktopUnchanged,mainThreadDesktopUnchanged,systemInputInjected,systemCursorManipulated,visibleDesktopSwitched
 if(!$r.hiddenDesktop -or !$r.privateDesktopClosed -or !$r.uiWorkerTerminated -or !$r.inputDesktopUnchanged -or !$r.mainThreadDesktopUnchanged -or $r.systemInputInjected -or $r.systemCursorManipulated -or $r.visibleDesktopSwitched){throw 'Native isolation/cleanup evidence failed.'}
 foreach($originalName in $originalNames){
  $match=@($r.cases|Where-Object {$_.name -eq $originalName})
  if($match.Count -ne 1 -or !$match[0].ok){throw ('Original native assertion missing/failed: '+$originalName)}
 }
 $receipt.original20PreservedAndPassed=$true
 $expected=@($ExpectedFailures|Sort-Object)
 if(($actual -join "`n") -ne ($expected -join "`n")){throw ('Unexpected native failures: '+($actual -join ','))}
 $expectedExit=if($expected.Count){3}else{0}
 if($run.ExitCode -ne $expectedExit){throw 'Unexpected native fixture exit code.'}
 foreach($file in $receipt.sources){if((Get-FileHash -LiteralPath (Join-Path $native $file.name) -Algorithm SHA256).Hash -ne $file.sha256){throw 'Frozen source changed.'}}
 foreach($file in $receipt.sourceInputs){if((Get-FileHash -LiteralPath $file.path -Algorithm SHA256).Hash -ne $file.sha256){throw ('Original source changed during run: '+$file.path)}}
 $receipt.ok=$true;$receipt.status=if($expected.Count){'expected-baseline-order-failures-reproduced'}else{'native-MOVE-order-regression-passed'}
 $receipt.sourcesUnchanged=$true;$receipt.executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
}catch{$receipt.status='failed';$receipt.error=$_.Exception.Message;throw}finally{$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');SaveReceipt}
Write-Output "NATIVE_CURSOR_QUEUE_VERIFIED checks=$($receipt.checks) expectedFailures=$(@($ExpectedFailures).Count)"