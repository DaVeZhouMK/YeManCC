[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$TouchpadHeader,
    [string[]]$ExpectedFailures=@()
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh output directory.'}
$native=Join-Path $OutputDirectory 'FrozenNative'
New-Item -ItemType Directory -Path $native -Force | Out-Null
$receipt=[ordered]@{ok=$false;scope='Real normal/isolated WinRT input sources on an owned private desktop; no application, devices, system input or CPU sampling';expectedFailures=$ExpectedFailures;sources=@()}
try {
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $project 'native') -File -Filter '*.h'){
        $sha=(Get-FileHash -LiteralPath $file.FullName).Hash;$copy=Join-Path $native $file.Name
        Copy-Item -LiteralPath $file.FullName -Destination $copy
        if((Get-FileHash -LiteralPath $copy).Hash -ne $sha){throw "Source changed during snapshot: $($file.Name)"}
    }
    Copy-Item -LiteralPath (Join-Path $project 'deps\json\json.hpp') -Destination (Join-Path $native 'json.hpp')
    if($TouchpadHeader){Copy-Item -LiteralPath $TouchpadHeader -Destination (Join-Path $native 'screen_touchpads.h') -Force}
    $cpp=Join-Path $native 'screen_touchpad_manual_error_reselect_selftest.cpp'
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'screen_touchpad_manual_error_reselect_selftest.cpp') -Destination $cpp
    foreach($file in Get-ChildItem -LiteralPath $native -File){$receipt.sources+=@{name=$file.Name;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash}}
    $build=Join-Path $OutputDirectory 'build.cmd';$exe=Join-Path $OutputDirectory 'manual-reselect.exe'
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\%WindowsSDKVersion%winrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /sourceDependencies "$OutputDirectory\compile-inputs.json" /I"$native" /I"%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt" "$cpp" /Fo"$OutputDirectory\manual-reselect.obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib shell32.lib
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $build -Encoding ascii
    & $build 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'compile.log')
    if($LASTEXITCODE -ne 0){throw 'Native manual-reselect consumer compilation failed'}
    $report=Join-Path $OutputDirectory 'result.json'
    $child=Start-Process -FilePath $exe -ArgumentList @(('"'+$report+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.txt')
    if(!$child.WaitForExit(45000)){$child.Kill();$child.WaitForExit();throw 'Owned native consumer exceeded its deadline'}
    $result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    $receipt.exitCode=$child.ExitCode;$receipt.checks=@($result.cases).Count
    $receipt.actualFailures=@($result.cases | Where-Object {!$_.ok} | ForEach-Object {$_.name})
    if(!$result.privateDesktopClosed -or !$result.parentInputDesktopUnchanged -or !$result.queuesClosed -or $result.jobActiveBeforeClose -ne 0 -or $result.forcedTermination -or $result.systemInputInjected -or $result.deviceWrites -ne 0 -or $result.productStarted -or $result.visibleDesktopSwitched){throw 'Native consumer isolation/cleanup evidence invalid'}
    $actual=@($receipt.actualFailures | Sort-Object);$expected=@($ExpectedFailures | Sort-Object)
    if(($actual -join "`n") -ne ($expected -join "`n")){throw 'Native failures do not match the expected control'}
    $expectedExit=if($expected.Count){1}else{0}
    if($child.ExitCode -ne $expectedExit -or [bool]$result.ok -ne ($expected.Count -eq 0)){throw 'Native consumer exit does not match its result'}
    foreach($source in $receipt.sources){if((Get-FileHash -LiteralPath (Join-Path $native $source.name)).Hash -ne $source.sha256){throw 'Frozen native source changed during verification'}}
    $receipt.ok=$true;$receipt.negativeControl=[bool]$expected.Count;$receipt.executableSha256=(Get-FileHash -LiteralPath $exe).Hash
    Write-Output "MANUAL_ERROR_RESELECT_VERIFIED checks=$($receipt.checks) control=$($receipt.negativeControl)"
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
