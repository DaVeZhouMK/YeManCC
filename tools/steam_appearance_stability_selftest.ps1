param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$Out=[IO.Path]::GetFullPath($Out);New-Item -ItemType Directory -Path $Out -Force|Out-Null
$cmd=Join-Path $Out 'compile.cmd'
$recipe=@"
@echo off
chcp 65001 >nul
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"$repo\native" /I"$repo\deps\json" "$repo\tools\steam_ui_mode_adversarial_selftest.cpp" /Fo"$Out\adversarial.obj" /Fe"$Out\adversarial.exe" /link advapi32.lib user32.lib
if errorlevel 1 exit /b 1
"$Out\adversarial.exe" "$Out"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"$repo\native" /I"$repo\deps\json" "$repo\tools\steam_appearance_shared_chain_selftest.cpp" /Fo"$Out\chain.obj" /Fe"$Out\chain.exe" /link advapi32.lib user32.lib shell32.lib ws2_32.lib iphlpapi.lib
if errorlevel 1 exit /b 1
"$Out\chain.exe" "$Out"
"@
[IO.File]::WriteAllText($cmd,$recipe,(New-Object Text.UTF8Encoding($false)))
& $cmd 2>&1 | Tee-Object -FilePath (Join-Path $Out 'result.log')
if($LASTEXITCODE -ne 0){throw "Steam appearance/common-interface stability tests failed: $LASTEXITCODE"}
