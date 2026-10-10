[CmdletBinding()]
param([string]$ValidationRoot)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\Build\Validation\ScreenTouchpads-StandaloneSpecial'))}
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Force -Path $ValidationRoot | Out-Null
$receiptPath=Join-Path $ValidationRoot 'standalone-special-verification.json'
$files=@('native\main.cpp','native\steam_live_cdp.h','native\steam_live.js','native\steam_live_script.h','src\bridge\screenTouchpads.ts','src\components\ScreenTouchpadsSettings.vue','tools\screen_standalone_special_actions_selftest.mjs','native\screen_touchpads.h','native\screen_standalone_special.h','native\screen_special_actions.h','native\screen_button_overlay.h','native\screen_touchpad_cursor.h','native\screen_control_glyphs.h','tools\screen_touchpads_standalone_special_selftest.cpp','tools\verify-screen-touchpads-standalone-special.ps1')
function Get-Sources {
    @($files | ForEach-Object {$path=Join-Path $project $_;[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}})
}
$receipt=[ordered]@{ok=$false;status='running';systemInputInjected=$false;virtualTargetStarted=$false;startedAtUtc=[DateTime]::UtcNow.ToString('o');sources=(Get-Sources)}
function Save-Receipt { $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding utf8 }
Save-Receipt
try {
    $vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    if(!(Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 BuildTools are required.'}
    $exe=Join-Path $ValidationRoot 'standalone-special.exe';$obj=Join-Path $ValidationRoot 'standalone-special.obj'
    $source=Join-Path $PSScriptRoot 'screen_touchpads_standalone_special_selftest.cpp'
    $cmd=Join-Path $ValidationRoot 'build-standalone-special.cmd'
    @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd
    if($LASTEXITCODE -ne 0){throw 'Standalone special helper compilation failed.'}
    $report=Join-Path $ValidationRoot 'standalone-special-selftest.json'
    if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
    & $exe $report
    $code=$LASTEXITCODE
    if(!(Test-Path -LiteralPath $report)){throw 'No fresh standalone special report.'}
    $r=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    $receipt.cases=@($r.cases).Count
    $receipt.failedCases=@($r.cases | Where-Object {!$_.ok} | ForEach-Object {$_.name})
    if($code -ne 0 -or !$r.ok){throw 'Standalone special regression failed.'}
    Push-Location -LiteralPath $project
    try {
        & node (Join-Path $PSScriptRoot 'steam_live_script_embed.mjs') --check
        if($LASTEXITCODE -ne 0){throw 'Steam live script embed is stale.'}
        $steamReport=Join-Path $ValidationRoot 'steam-actions-selftest.json'
        & node (Join-Path $PSScriptRoot 'screen_standalone_special_actions_selftest.mjs') $steamReport | Out-Null
        if($LASTEXITCODE -ne 0){throw 'Steam standalone action regression failed.'}
        $steam=Get-Content -LiteralPath $steamReport -Raw | ConvertFrom-Json
        if(!$steam.ok){throw 'Steam standalone action report failed.'}
        $receipt.steamActionCases=$steam.passed
    } finally { Pop-Location }
    $after=Get-Sources
    for($i=0;$i -lt $after.Count;$i++){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed while testing: $($after[$i].path)"}}
    $receipt.helperSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    $receipt.ok=$true;$receipt.status='passed';$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt
    Write-Output "STANDALONE_SPECIAL_VERIFIED $ValidationRoot"
} catch {
    $receipt.ok=$false;$receipt.status='failed';$receipt.error=$_.Exception.Message;$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt
    throw
}
