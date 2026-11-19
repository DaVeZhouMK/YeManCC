param([Parameter(Mandatory=$true)][string]$Out,[string]$BaselineNative='',[switch]$ExceptionsOnly,[switch]$ReceiptBorrowOnly,[switch]$ColdImageOnly)
$ErrorActionPreference='Stop'
if($ReceiptBorrowOnly -and ($ExceptionsOnly -or $BaselineNative)){throw 'Receipt borrow selection cannot combine baseline or exception selections'}
if($ColdImageOnly -and ($ReceiptBorrowOnly -or $ExceptionsOnly)){throw 'Cold image selection cannot combine receipt or exception selections'}
$repo=Split-Path -Parent $PSScriptRoot
$Out=[IO.Path]::GetFullPath($Out);New-Item -ItemType Directory -Path $Out -Force|Out-Null
$flags=''
if($BaselineNative){$BaselineNative=(Resolve-Path -LiteralPath $BaselineNative).Path;$flags='/DYMCC_BASELINE_METRICS /I"'+$BaselineNative+'"'}
$cmd=Join-Path $Out 'compile.cmd'
$selection=if($ExceptionsOnly){' --exceptions-only'}elseif($ReceiptBorrowOnly){' --receipt-borrow-only'}elseif($ColdImageOnly){' --cold-image-only'}else{''}
$costStage=if($ExceptionsOnly -or $ReceiptBorrowOnly -or $ColdImageOnly){'exit /b 0'}else{''}
$recipe=@"
@echo off
chcp 65001 >nul
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 $flags /I"$repo\native" /I"$repo\deps\json" "$repo\tools\steam_window_event_budget_selftest.cpp" /Fo"$Out\test.obj" /Fe"$Out\test.exe" /link advapi32.lib user32.lib
if errorlevel 1 exit /b 1
"$Out\test.exe" "$Out"$selection
if errorlevel 1 exit /b 1
$costStage
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"$repo\native" /I"$repo\deps\json" "$repo\tools\steam_window_event_cost_selftest.cpp" /Fo"$Out\cost.obj" /Fe"$Out\cost.exe"
if errorlevel 1 exit /b 1
"$Out\cost.exe" "$Out"
"@
[IO.File]::WriteAllText($cmd,$recipe,(New-Object Text.UTF8Encoding($false)))
& $cmd 2>&1 | Tee-Object -FilePath (Join-Path $Out 'result.log')
if($LASTEXITCODE -ne 0){throw "Steam window event budget test failed: $LASTEXITCODE"}
