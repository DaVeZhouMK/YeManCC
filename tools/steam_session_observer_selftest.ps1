param([string]$OutputDirectory='G:\YeManCC-Work\_scratch\steam-session-20261006\observer-selftest')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"$root\native" "$root\tools\steam_session_observer_selftest.cpp" /Fo"$OutputDirectory\observer-selftest.obj" /Fe"$OutputDirectory\observer-selftest.exe" /link advapi32.lib user32.lib
if errorlevel 1 exit /b 1
"$OutputDirectory\observer-selftest.exe" "$OutputDirectory"
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat
if($LASTEXITCODE -ne 0){throw "Steam session observer selftest failed: $LASTEXITCODE"}