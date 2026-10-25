param(
    [Parameter(Mandatory=$true)][string]$Out,
    [string]$BeforeHeader=''
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$source=Join-Path $repo 'tools\steam_cssloader_single_helper_selftest.cpp'
$build=@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
REM BuildTools may omit Windows SDK registry discovery; use the installed SDK explicitly.
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\%WindowsSDKVersion%winrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"$repo\native" /I"$repo\deps\json" "$source" /Fo"$Out\single.obj" /Fe"$Out\single.exe" /link advapi32.lib user32.lib iphlpapi.lib ws2_32.lib
if errorlevel 1 exit /b 1
"@
if($BeforeHeader){
    $BeforeHeader=[IO.Path]::GetFullPath($BeforeHeader).Replace('\','/')
    if(!(Test-Path -LiteralPath $BeforeHeader -PathType Leaf)){throw 'Before header missing'}
    # Isolate only this implementation; resolve unchanged dependency includes from native/.
    $stagedHeader=Join-Path $Out 'before-use.h'
    Copy-Item -LiteralPath $BeforeHeader -Destination $stagedHeader
    $BeforeHeader=$stagedHeader.Replace('\','/')
    $beforeSource=Join-Path $Out 'before.cpp'
    $text=[IO.File]::ReadAllText($source).Replace('#include "steam_cssloader_use.h"',('#include "'+$BeforeHeader+'"'))
    [IO.File]::WriteAllText($beforeSource,$text,(New-Object Text.UTF8Encoding($false)))
    $build+="`ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I`"$repo\native`" /I`"$repo\deps\json`" `"$beforeSource`" /Fo`"$Out\before.obj`" /Fe`"$Out\before.exe`" /link advapi32.lib user32.lib iphlpapi.lib ws2_32.lib`nif errorlevel 1 exit /b 1`n"
}
$cmd=Join-Path $Out 'compile.cmd'
[IO.File]::WriteAllText($cmd,$build,(New-Object Text.UTF8Encoding($false)))
& $cmd 2>&1 | Tee-Object -FilePath (Join-Path $Out 'build.log')
if($LASTEXITCODE -ne 0){throw 'Single-helper regression compilation failed'}
if($BeforeHeader){
    $previousPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try { & (Join-Path $Out 'before.exe') (Join-Path $Out 'before') $repo 2>&1 | Tee-Object -FilePath (Join-Path $Out 'before.log');$beforeExit=$LASTEXITCODE }
    finally {$ErrorActionPreference=$previousPreference}
    if($beforeExit -ne 1 -or [IO.File]::ReadAllText((Join-Path $Out 'before.log')) -notmatch 'one automatic helper initialized CSS and signaled ready|resident confirmation completes with the real result mapping'){throw 'Before version did not fail at the resident launch or confirmation boundary'}

}
& (Join-Path $Out 'single.exe') (Join-Path $Out 'after') $repo 2>&1 | Tee-Object -FilePath (Join-Path $Out 'result.log')
if($LASTEXITCODE -ne 0){throw 'Single-helper regression failed'}
