[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath (Join-Path $OutputDirectory 'receipt.json')){throw 'Use a fresh output directory; existing evidence is retained.'}
$native=Join-Path $OutputDirectory 'FrozenNative'
New-Item -ItemType Directory -Force -Path $native|Out-Null
$receipt=[ordered]@{ok=$false;startedAtUtc=[DateTime]::UtcNow.ToString('o');scope='Explicit WM_SETCURSOR delivery and SetCursor shape probes on never-shown private UI desktop; actual production/WinRT child and parent; no pointer movement or input injection';sources=@();systemInputInjected=$false;systemCursorManipulated=$true;privateDesktopCursorShapeOnly=$true;visibleDesktopSwitched=$false;productDeployed=$false;fieldFlickerVerified=$false}
function SaveReceipt {$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
SaveReceipt
try {
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $project 'native') -File -Filter '*.h'){
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $native $file.Name)
    }
    Copy-Item -LiteralPath (Join-Path $project 'deps/json/json.hpp') -Destination (Join-Path $native 'json.hpp')
    $source=Join-Path $PSScriptRoot 'screen_touchpad_setcursor_route_selftest.cpp'
    Copy-Item -LiteralPath $source -Destination (Join-Path $native 'screen_touchpad_setcursor_route_selftest.cpp')
    foreach($file in Get-ChildItem -LiteralPath $native -File){$receipt.sources+=@{name=$file.Name;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}}
    $cmd=Join-Path $OutputDirectory 'build.cmd';$exe=Join-Path $OutputDirectory 'setcursor-route.exe'
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1 /I"$native" /I"%CPPWINRT_INCLUDE%" "$native\screen_touchpad_setcursor_route_selftest.cpp" /Fo"$OutputDirectory\setcursor-route.obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib shell32.lib
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'compile.log')
    if($LASTEXITCODE -ne 0){throw 'Native cursor route fixture compilation failed.'}
    $report=Join-Path $OutputDirectory 'native-setcursor-route.json'
    $run=Start-Process -FilePath $exe -ArgumentList ('"'+$report+'"') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'run.stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'run.stderr.txt')
    $receipt.exitCode=$run.ExitCode
    if(!(Test-Path -LiteralPath $report)){throw 'Native fixture did not produce a report.'}
    $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
    if(!$r.hiddenDesktop -or !$r.inputDesktopUnchanged -or !$r.runtimeShutdownCompleted -or !$r.queueShutdownObservedCompleted -or !$r.uiWorkerTerminated -or !$r.privateDesktopClosed -or !$r.ownedTargetsTerminal -or $r.systemInputInjected -or !$r.systemCursorManipulated -or !$r.privateThreadCursorShapeTests -or $r.cursorPositionChangedByFixture -or $r.visibleDesktopSwitched){throw 'Native fixture isolation/cleanup evidence invalid.'}
    $failures=@($r.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name})
    if($run.ExitCode -ne 0 -or !$r.ok){throw 'Current native cursor route checks failed.'}
    foreach($file in $receipt.sources){if((Get-FileHash -LiteralPath (Join-Path $native $file.name) -Algorithm SHA256).Hash -ne $file.sha256){throw 'Frozen source changed during execution.'}}
    $receipt.ok=$true;$receipt.checks=@($r.cases).Count;$receipt.failures=$failures;$receipt.report=$report;$receipt.sourcesUnchanged=$true
    $receipt.executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');SaveReceipt}
Write-Output "NATIVE_SETCURSOR_ROUTE_VERIFIED checks=$($receipt.checks)"
