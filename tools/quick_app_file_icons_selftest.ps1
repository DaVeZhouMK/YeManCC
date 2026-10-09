param([string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root '..\..\Build\Validation\quick-app-file-icons-20261007'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$bat=Join-Path $OutputDirectory 'compile.cmd'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DUNICODE /D_UNICODE /I"$root\native" /I"$root\deps\json" "$root\tools\quick_app_file_icons_selftest.cpp" /Fo"$OutputDirectory\quick-app-file-icons.obj" /Fe"$OutputDirectory\quick-app-file-icons.exe"
if errorlevel 1 exit /b 1
"$OutputDirectory\quick-app-file-icons.exe" "$OutputDirectory"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat
if($LASTEXITCODE -ne 0){throw "Quick app native file icons selftest failed: $LASTEXITCODE"}
