[CmdletBinding()]
param(
    [string]$ValidationRoot,
    [switch]$RealTouch
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\Build\Validation\ScreenTouchpads-Cursor'))}
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Force -Path $ValidationRoot | Out-Null
# Includes production headers; does not start a virtual target or change the
# installed product. Default fixtures inject no system input. RealTouch uses
# relocated helper HWNDs, two tagged 1px mouse moves and a read-only test hook
# to diagnose interference; it restores the pointer and never blocks input.
$files=@('native\screen_touchpads.h','native\screen_touchpad_cursor.h','native\screen_button_overlay.h','native\screen_control_glyphs.h','tools\screen_touchpads_cursor_selftest.cpp','tools\verify-screen-touchpads-cursor.ps1')
function SourceHashes {
    @($files | ForEach-Object {$path=Join-Path $project $_;[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}})
}
$receiptPath=Join-Path $ValidationRoot 'cursor-verification.json'
$receipt=[ordered]@{ok=$false;status='running';startedAtUtc=[DateTime]::UtcNow.ToString('o');realTouchRequested=[bool]$RealTouch;sources=(SourceHashes)}
function SaveReceipt { $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding utf8 }
SaveReceipt # Invalidate old PASS before compiling or running anything.
try {
    $vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    if(!(Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 BuildTools are required for this native regression helper.'}
    $exe=Join-Path $ValidationRoot 'cursor_selftest.exe'
    $obj=Join-Path $ValidationRoot 'cursor_selftest.obj'
    $source=Join-Path $PSScriptRoot 'screen_touchpads_cursor_selftest.cpp'
    $cmd=Join-Path $ValidationRoot 'build-cursor-selftest.cmd'
    @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd
    if($LASTEXITCODE -ne 0){throw 'Cursor regression helper compilation failed.'}
    $receipt.helperSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    $model=Join-Path $ValidationRoot 'cursor-model-selftest.json'
    if(Test-Path -LiteralPath $model){Remove-Item -LiteralPath $model}
    & $exe '--model-only' $model
    $modelExit=$LASTEXITCODE
    if(!(Test-Path -LiteralPath $model)){throw 'Cursor fixture produced no fresh report.'}
    $m=Get-Content -LiteralPath $model -Raw | ConvertFrom-Json
    $receipt.fixtureCases=$m.cases.Count;$receipt.fixtureOk=[bool]$m.ok
    if($modelExit -ne 0 -or !$m.ok){throw 'Cursor fixture/rendering regression failed.'}
    if($RealTouch){
        $real=Join-Path $ValidationRoot 'cursor-real-selftest.json'
        if(Test-Path -LiteralPath $real){Remove-Item -LiteralPath $real}
        & $exe $real
        $realExit=$LASTEXITCODE
        if(!(Test-Path -LiteralPath $real)){throw 'Real touch tests produced no fresh report.'}
        $r=Get-Content -LiteralPath $real -Raw | ConvertFrom-Json
        $receipt.realTouchExitCode=$realExit;$receipt.realTouchOk=[bool]$r.ok
        $receipt.realTouchCases=@($r.cases).Count;$receipt.realTouchSamples=@($r.samples).Count
        $receipt.realTouchStatus=$r.status
        $receipt.failedRealTouchCases=@($r.cases | Where-Object {!$_.ok} | ForEach-Object {$_.name})
        $receipt.externalMouseMoves=$r.externalMouseMoves;$receipt.externalInjectedMouseMoves=$r.externalInjectedMouseMoves
        if($realExit -eq 2){throw "Real touch tests blocked by Windows input-desktop preflight; see $real. Not a PASS."}
        if($realExit -ne 0 -or !$r.ok){throw "Real touch regression failed; see $real. External mouse moves: $($r.externalMouseMoves), injected: $($r.externalInjectedMouseMoves)."}
    }
    $after=SourceHashes
    for($i=0;$i -lt $after.Count;$i++) {
        if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed while testing: $($after[$i].path)"}
    }
    $receipt.ok=$true;$receipt.status='passed';$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');SaveReceipt
    Write-Output "CURSOR_VERIFIED $ValidationRoot"
} catch {
    $receipt.ok=$false;$receipt.status='failed';$receipt.error=$_.Exception.Message;$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');SaveReceipt
    throw
}
