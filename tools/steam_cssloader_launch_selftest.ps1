param([Parameter(Mandatory=$true)][string]$OutputDirectory,[switch]$NegativeControl)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$nativeInclude=Join-Path $root 'native'
if($NegativeControl){
 $nativeInclude=Join-Path $OutputDirectory 'mutated-native';[IO.Directory]::CreateDirectory($nativeInclude)|Out-Null
 $source=[IO.File]::ReadAllText((Join-Path $root 'native\steam_cssloader_launch.h'))
 $anchor='deckylaunch::Result result; result.mode = "ymcc-current-token";'
 if(-not $source.Contains($anchor)){throw 'Negative launch anchor changed'}
 $source=$source.Replace($anchor,'return deckylaunch::create(executable, command, environment, directory, startup, child);'+[Environment]::NewLine+$anchor)
 [IO.File]::WriteAllText((Join-Path $nativeInclude 'steam_cssloader_launch.h'),$source,[Text.UTF8Encoding]::new($false))
}
if(-not $NegativeControl){& node (Join-Path $PSScriptRoot 'steam_cssloader_launch_policy_selftest.mjs') $OutputDirectory;if($LASTEXITCODE -ne 0){throw 'Policy consumer extraction failed'}}
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\%WindowsSDKVersion%winrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"$nativeInclude" /I"$root\native" "$root\tools\steam_cssloader_launch_selftest.cpp" /Fo"$OutputDirectory\launch.obj" /Fe"$OutputDirectory\launch.exe" /link advapi32.lib user32.lib
if errorlevel 1 exit /b 1
"$OutputDirectory\launch.exe" "$OutputDirectory"
if errorlevel 1 exit /b 1
if not exist "$OutputDirectory\policy.cpp" exit /b 0
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "$OutputDirectory\policy.cpp" /Fo"$OutputDirectory\policy.obj" /Fe"$OutputDirectory\policy.exe"
if errorlevel 1 exit /b 1
"$OutputDirectory\policy.exe" "$OutputDirectory\policy-result.json"
"@ | Set-Content -LiteralPath $bat -Encoding ascii
$priorErrorPolicy=$ErrorActionPreference;$ErrorActionPreference='Continue'
try{$output=& $bat 2>&1;$code=$LASTEXITCODE}finally{$ErrorActionPreference=$priorErrorPolicy}
$output|Tee-Object -FilePath (Join-Path $OutputDirectory 'result.log')
if($NegativeControl){if($code -ne 1 -or ($output -join "`n") -notmatch 'CSS helper uses caller-token route'){throw 'CSS downgrade negative was not detected at the intended assertion'};[IO.File]::WriteAllText((Join-Path $OutputDirectory 'negative-result.json'),'{"ok":true,"downgradeDetected":true,"expected":"CSS helper uses caller-token route"}',[Text.UTF8Encoding]::new($false))}
elseif($code -ne 0){throw "CSS helper launch selftest failed: $code"}
