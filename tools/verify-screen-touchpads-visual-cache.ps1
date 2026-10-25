[CmdletBinding()]
param([string]$ValidationRoot,[switch]$NativeCandidate)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\visual-cache'))}
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Force -Path $ValidationRoot|Out-Null
$files=@('native\main.cpp','native\screen_touchpad_input_surface.h','native\screen_touchpads.h','native\screen_button_overlay.h','native\screen_touchpad_cursor.h','native\screen_standalone_special.h','native\screen_control_glyphs.h','tools\screen_touchpads_visual_cache_selftest.cpp','tools\verify-screen-touchpads-visual-cache.ps1')
function Hashes {@($files|ForEach-Object{$p=Join-Path $project $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}})}
$receipt=[ordered]@{ok=$false;nativeCandidateRequested=[bool]$NativeCandidate;scope='retained-native-layer-cache-not-whole-feature-completion';productDeployed=$false;systemInputInjected=$false;virtualTargetStarted=$false;realTouchFlickerAcceptanceRun=$false;startedUtc=[DateTime]::UtcNow.ToString('o');sources=(Hashes)}
$receiptPath=Join-Path $ValidationRoot 'visual-cache-verification.json'
function Save-Receipt {$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $receiptPath -Encoding utf8}
Save-Receipt
try {
 $vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
 $source=Join-Path $PSScriptRoot 'screen_touchpads_visual_cache_selftest.cpp'
 $exe=Join-Path $ValidationRoot 'visual-cache-selftest.exe';$obj=Join-Path $ValidationRoot 'visual-cache-selftest.obj'
 $cmd=Join-Path $ValidationRoot 'build-visual-cache-selftest.cmd'
 $candidateDefine=if($NativeCandidate){'/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1'}else{''}
 @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE $candidateDefine /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
 & $cmd
 if($LASTEXITCODE -ne 0){throw 'Native visual-cache fixture compilation failed'}
 $report=Join-Path $ValidationRoot 'visual-cache-selftest.json'
 if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
 & $exe $report|Out-Null
 $exitCode=$LASTEXITCODE
 if(!(Test-Path -LiteralPath $report)){throw 'No fresh native visual-cache report'}
 $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
 $receipt.nativeCandidateInputExercised=[bool]$r.nativeCandidateInputExercised
 if($NativeCandidate -and !$r.nativeCandidateInputExercised){throw 'Candidate visuals test did not exercise native input'}
 $receipt.cases=@($r.cases).Count;$receipt.failedCases=@($r.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name})
 $receipt.redundantOutlineUploads=$r.redundantOutlineUploads;$receipt.ownCompositedPixelRetentionChecked=$r.ownCompositedPixelRetentionChecked
 if($exitCode -ne 0 -or !$r.ok){throw 'Native visual-cache regression failed'}
 $after=Hashes
 for($i=0;$i -lt $after.Count;$i++){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during test: $($after[$i].path)"}}
 $receipt.ok=$true;$receipt.helperSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
 Write-Output "NATIVE_LAYER_CACHE_VERIFIED cases=$($receipt.cases) redundantUploads=$($receipt.redundantOutlineUploads); real touch flicker and mouse passthrough remain separate gates."
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt}
