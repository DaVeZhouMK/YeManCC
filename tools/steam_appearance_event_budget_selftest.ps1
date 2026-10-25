param([Parameter(Mandatory=$true)][string]$Out,[string]$BaselineNative='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$baselineFlags=''
if($BaselineNative){
 $BaselineNative=(Resolve-Path -LiteralPath $BaselineNative).Path
 $baselineFlags='/DYMCC_BASELINE_METRICS /I"'+$BaselineNative+'"'
}
$cmd=Join-Path $Out 'compile.cmd'
$recipe=@"
@echo off
"C:\Windows\System32\chcp.com" 65001 >nul
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
REM BuildTools may omit Windows SDK registry discovery; use the installed SDK explicitly.
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\%WindowsSDKVersion%winrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 $baselineFlags /I"$repo\native" /I"$repo\deps\json" "$repo\tools\steam_appearance_event_budget_selftest.cpp" /Fo"$Out\test.obj" /Fe"$Out\test.exe" /link advapi32.lib user32.lib shell32.lib winhttp.lib ws2_32.lib iphlpapi.lib
if errorlevel 1 exit /b 1
"$Out\test.exe" "$Out"
"@
[IO.File]::WriteAllText($cmd,$recipe,(New-Object Text.UTF8Encoding($false)))
& $cmd 2>&1 | Tee-Object -FilePath (Join-Path $Out 'result.log')
if($LASTEXITCODE -ne 0){throw "Appearance event budget test failed: $LASTEXITCODE"}
