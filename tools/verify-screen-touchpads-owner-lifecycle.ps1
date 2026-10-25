[CmdletBinding()]
param([string]$ValidationRoot,[switch]$DiagnoseOnly)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\native-owner'))}
New-Item -ItemType Directory -Path $ValidationRoot -Force|Out-Null
$files=@('native\main.cpp','native\build_native.bat','native\screen_touchpad_owner.h','native\screen_touchpads.h','native\screen_touchpad_input_surface.h','native\screen_button_overlay.h','native\screen_touchpad_cursor.h','native\screen_standalone_special.h','native\screen_control_glyphs.h','deps\json\json.hpp','tools\screen_touchpads_native_topology_test_host.h','tools\screen_touchpads_owner_lifecycle_selftest.cpp','tools\verify-screen-touchpads-owner-lifecycle.ps1')
function Hashes{@($files|ForEach-Object{$p=Join-Path $project $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p).Hash}})}
$receipt=[ordered]@{ok=$false;featureAcceptancePassed=$false;productDeployed=$false;manualPhysicalInputRunExecuted=$false;candidateDefaultEnabled=$false;normalAppOwnerWired=$false;sources=(Hashes);startedUtc=[DateTime]::UtcNow.ToString('o')}
$receiptPath=Join-Path $ValidationRoot 'owner-lifecycle-verification.json'
function Save-Receipt{$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $receiptPath -Encoding UTF8}
Save-Receipt
try{
    $main=[IO.File]::ReadAllText((Join-Path $project 'native\main.cpp'))
    if(!$main.Contains('initializeForOwner(hi,g_hwnd,bank.slots[profile],profile,GetCommandLineW())') -or !$main.Contains('case ymcc::screenpads::kOwnerRebindMessage:') -or !$main.Contains('queueOwnerResumeRebind()') -or !$main.Contains('ownerOnSuspend()')){throw 'Actual main owner startup/power/rebind wiring is missing'}
    $receipt.normalAppOwnerWired=$true
    foreach($kind in @('candidate','legacy')){
        $define=if($kind -eq 'candidate'){'/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1'}else{''};$source=Join-Path $PSScriptRoot 'screen_touchpads_owner_lifecycle_selftest.cpp';$exe=Join-Path $ValidationRoot "owner-$kind.exe";$obj=Join-Path $ValidationRoot "owner-$kind.obj";$cmd=Join-Path $ValidationRoot "build-owner-$kind.cmd"
        @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE $define /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib shell32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
        & $cmd;if($LASTEXITCODE -ne 0){throw "Owner $kind build failed"}
        $report=Join-Path $ValidationRoot "owner-$kind-selftest.json";if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
        & $exe $report|Out-Host;$exit=$LASTEXITCODE;$r=Get-Content -LiteralPath $report -Raw -Encoding UTF8|ConvertFrom-Json
        if($exit -ne 0 -or !$r.ok -or !$r.ownedTargetTerminal){throw "Owner $kind input/lifecycle failed"}
        if($kind -eq 'candidate'){$receipt.candidateDefaultEnabled=[bool]$r.defaultStartup.active}
        $receipt["${kind}Checks"]=@($r.cases).Count;$receipt["${kind}Passed"]=$true;$receipt["${kind}ExeSha256"]=(Get-FileHash -LiteralPath $exe).Hash
    }
    $after=Hashes;for($i=0;$i -lt $after.Count;++$i){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during owner lifecycle test: $($after[$i].path)"}}
    $receipt.ok=$true
    if(!$DiagnoseOnly){throw 'OWNER_NOT_FULL_FEATURE_ACCEPTANCE: actual product startup/hardware static input is not fully accepted'}
    Write-Output "OWNER_LIFECYCLE_VERIFIED candidate=$($receipt.candidateChecks) legacy=$($receipt.legacyChecks) featureAcceptancePassed=False"
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt}