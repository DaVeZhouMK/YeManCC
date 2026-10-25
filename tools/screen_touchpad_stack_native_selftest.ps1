[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$BaselineDirectory,
    [switch]$ExpectBaselineFailure
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath (Join-Path $OutputDirectory 'receipt.json')){throw 'Use a fresh output directory; existing evidence is retained.'}
$native=Join-Path $OutputDirectory 'FrozenNative'
New-Item -ItemType Directory -Force -Path $native|Out-Null
$receipt=[ordered]@{ok=$false;startedAtUtc=[DateTime]::UtcNow.ToString('o');scope='Native z-order on private never-switched desktop, foreground adapter substituted; not Steam or visible scanout proof';baselineExpected=[bool]$ExpectBaselineFailure;sources=@();systemInputInjected=$false;systemCursorManipulated=$false;visibleDesktopSwitched=$false;productDeployed=$false;fieldFlickerVerified=$false}
function SaveReceipt {$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
SaveReceipt
try {
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $project 'native') -File -Filter '*.h'){
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $native $file.Name)
    }
    Copy-Item -LiteralPath (Join-Path $project 'deps/json/json.hpp') -Destination (Join-Path $native 'json.hpp')
    if($BaselineDirectory){
        foreach($name in @('screen_touchpad_cursor.h','screen_touchpad_visibility.h','screen_touchpads.h')){
            Copy-Item -LiteralPath (Join-Path $BaselineDirectory $name) -Destination (Join-Path $native $name) -Force
        }
    }
    $source=Join-Path $PSScriptRoot 'screen_touchpad_stack_native_selftest.cpp'
    Copy-Item -LiteralPath $source -Destination (Join-Path $native 'screen_touchpad_stack_native_selftest.cpp')
    foreach($file in Get-ChildItem -LiteralPath $native -File){$receipt.sources+=@{name=$file.Name;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}}
    $cmd=Join-Path $OutputDirectory 'build.cmd';$exe=Join-Path $OutputDirectory 'stack.exe'
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$native" "$native\screen_touchpad_stack_native_selftest.cpp" /Fo"$OutputDirectory\stack.obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'compile.log')
    if($LASTEXITCODE -ne 0){throw 'Native stack fixture compilation failed.'}
    $report=Join-Path $OutputDirectory 'native-stack.json'
    $run=Start-Process -FilePath $exe -ArgumentList ('"'+$report+'"') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'run.stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'run.stderr.txt')
    $receipt.exitCode=$run.ExitCode
    if(!(Test-Path -LiteralPath $report)){throw 'Native fixture did not produce a report.'}
    $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
    if(!$r.hiddenDesktop -or !$r.inputDesktopUnchanged -or $r.systemInputInjected -or $r.systemCursorManipulated -or $r.visibleDesktopSwitched){throw 'Native fixture isolation/cleanup evidence invalid.'}
    $failures=@($r.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name})
    if($ExpectBaselineFailure){
        $expected=@('contact-restored-above-outline-and-external','cursor-restored-above-contact-and-external','moving-contact-retains-order-with-stationary-cursor','retained-cursor-restored-above-external')
        if($run.ExitCode -ne 3 -or $r.ok -or @(Compare-Object ($expected|Sort-Object) ($failures|Sort-Object)).Count -ne 0){throw 'Baseline did not reproduce exactly the known ordering failures.'}
    }elseif($run.ExitCode -ne 0 -or !$r.ok){throw 'Current native stack checks failed.'}
    foreach($file in $receipt.sources){if((Get-FileHash -LiteralPath (Join-Path $native $file.name) -Algorithm SHA256).Hash -ne $file.sha256){throw 'Frozen source changed during execution.'}}
    $receipt.ok=$true;$receipt.checks=@($r.cases).Count;$receipt.failures=$failures;$receipt.report=$report;$receipt.sourcesUnchanged=$true
    $receipt.executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');SaveReceipt}
Write-Output "NATIVE_STACK_VERIFIED checks=$($receipt.checks) expectedBaseline=$([bool]$ExpectBaselineFailure)"
