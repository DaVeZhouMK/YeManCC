[CmdletBinding()]
param([string]$ValidationRoot,[string]$SourceRoot)
$ErrorActionPreference='Stop'
$liveProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project=if($SourceRoot){[IO.Path]::GetFullPath($SourceRoot)}else{$liveProject}
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenTouchpads-GameVisibility'))}
New-Item -ItemType Directory -Path $ValidationRoot -Force|Out-Null
$files=@('native\screen_touchpads.h','native\screen_touchpad_owner.h','native\screen_touchpad_visibility.h','native\screen_touchpad_input_surface.h','native\screen_button_overlay.h','native\touch_haptic_dispatch.h','tools\screen_touchpads_native_topology_test_host.h','tools\screen_touchpads_game_visibility_selftest.cpp','tools\verify-screen-touchpads-game-visibility.ps1')
function Hashes{@($files|ForEach-Object{$root=if($_.StartsWith('native\')){$project}else{$liveProject};$p=Join-Path $root $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p).Hash}})}
$receipt=[ordered]@{ok=$false;sourceRoot=$project;actualGame=$false;hardwareTouchVerified=$false;productDeployed=$false;sources=(Hashes);startedUtc=[DateTime]::UtcNow.ToString('o')}
try{
    $exe=Join-Path $ValidationRoot 'game-visibility.exe';$cmd=Join-Path $ValidationRoot 'build-game-visibility.cmd'
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1 /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$liveProject\tools\screen_touchpads_game_visibility_selftest.cpp" /Fo"$ValidationRoot\game-visibility.obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib shell32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd;if($LASTEXITCODE -ne 0){throw 'Visibility fixture compilation failed'}
    $report=Join-Path $ValidationRoot 'game-visibility.json';if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
    & $exe $report|Out-Host;$exit=$LASTEXITCODE
    $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
    if($exit -ne 0 -or !$r.ok -or !$r.diagnosticValid -or !$r.ownedTargetTerminal){throw 'Game visibility reproduction/regression failed'}
    $after=Hashes;for($i=0;$i -lt $after.Count;++$i){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during visibility test: $($after[$i].path)"}}
    $receipt.ok=$true;$receipt.checks=@($r.cases).Count;$receipt.report=$report;$receipt.testInputInjected=$true;$receipt.helperSha256=(Get-FileHash -LiteralPath $exe).Hash
    Write-Output "GAME_VISIBILITY_VERIFIED checks=$($receipt.checks); real game/exclusive fullscreen not claimed"
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $ValidationRoot 'game-visibility-verification.json') -Encoding UTF8}
