[CmdletBinding()]
param([string]$OutputDirectory = '')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
if (!$OutputDirectory) { $OutputDirectory=Join-Path $project '..\..\Build\Validation\http-deadline-native' }
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /std:c++20 /EHsc /utf-8 /MT /I"$project\native" "$project\tools\http_deadline_transport_fixture.cpp" /Fo"$OutputDirectory\fixture.obj" /Fe"$OutputDirectory\http-transport-fixture.exe"
"@ | Set-Content -LiteralPath $bat -Encoding ASCII
& $bat
if($LASTEXITCODE-ne0){throw 'HTTP deadline fixture compile failed'}
& node (Join-Path $PSScriptRoot 'http_deadline_transport_selftest.mjs') $OutputDirectory
if($LASTEXITCODE-ne0){throw 'HTTP deadline native tests failed'}