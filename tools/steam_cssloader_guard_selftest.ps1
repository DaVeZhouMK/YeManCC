param([string]$OutputDirectory='G:\YeManCC-Work\_scratch\steam-css-guard-20261011\selftest')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"$root\native" "$root\tools\steam_cssloader_guard_selftest.cpp" /Fo"$OutputDirectory\guard-selftest.obj" /Fe"$OutputDirectory\guard-selftest.exe" /link advapi32.lib user32.lib iphlpapi.lib
if errorlevel 1 exit /b 1
"$OutputDirectory\guard-selftest.exe" "$OutputDirectory"
"@|Set-Content -LiteralPath $bat -Encoding ascii
& $bat 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'result.log')
if($LASTEXITCODE -ne 0){throw "Steam CSS guard selftest failed: $LASTEXITCODE"}
