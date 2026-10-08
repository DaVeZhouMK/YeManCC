param([string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root '..\..\Build\Validation\controller-display-20261006\native-presence'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$bat=Join-Path $OutputDirectory 'compile.cmd'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"$root\native" "$root\tools\steam_presence_selftest.cpp" /Fo"$OutputDirectory\steam-presence.obj" /Fe"$OutputDirectory\steam-presence.exe" /link advapi32.lib
if errorlevel 1 exit /b 1
"$OutputDirectory\steam-presence.exe"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat
if($LASTEXITCODE -ne 0){throw "Steam presence selftest failed: $LASTEXITCODE"}
