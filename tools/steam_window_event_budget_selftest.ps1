param([Parameter(Mandatory=$true)][string]$Out,[string]$BaselineNative='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$Out=[IO.Path]::GetFullPath($Out);New-Item -ItemType Directory -Path $Out -Force|Out-Null
$flags=''
if($BaselineNative){$BaselineNative=(Resolve-Path -LiteralPath $BaselineNative).Path;$flags='/DYMCC_BASELINE_METRICS /I"'+$BaselineNative+'"'}
$cmd=Join-Path $Out 'compile.cmd'
$recipe=@"
@echo off
chcp 65001 >nul
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 $flags /I"$repo\native" /I"$repo\deps\json" "$repo\tools\steam_window_event_budget_selftest.cpp" /Fo"$Out\test.obj" /Fe"$Out\test.exe" /link advapi32.lib user32.lib
if errorlevel 1 exit /b 1
"$Out\test.exe" "$Out"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"$repo\native" /I"$repo\deps\json" "$repo\tools\steam_window_event_cost_selftest.cpp" /Fo"$Out\cost.obj" /Fe"$Out\cost.exe"
if errorlevel 1 exit /b 1
"$Out\cost.exe" "$Out"
"@
[IO.File]::WriteAllText($cmd,$recipe,(New-Object Text.UTF8Encoding($false)))
& $cmd 2>&1 | Tee-Object -FilePath (Join-Path $Out 'result.log')
if($LASTEXITCODE -ne 0){throw "Steam window event budget test failed: $LASTEXITCODE"}
