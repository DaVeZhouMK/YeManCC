[CmdletBinding()]
param([string]$ValidationRoot,[switch]$DiagnoseOnly)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\native-button-input'))}
New-Item -ItemType Directory -Path $ValidationRoot -Force|Out-Null
$files=@('native\screen_touchpads.h','native\screen_touchpad_input_surface.h','native\screen_button_overlay.h','native\screen_standalone_special.h','native\screen_touchpad_cursor.h','native\screen_control_glyphs.h','deps\json\json.hpp',
    'tools\screen_touchpads_native_topology_test_host.h','tools\screen_touchpads_native_button_acceptance.cpp','tools\screen_touchpads_native_real_output_acceptance.cpp','tools\screen_touchpads_native_cold_acceptance.cpp','tools\verify-screen-touchpads-native-button-input.ps1')
function Hashes {@($files|ForEach-Object{$p=Join-Path $project $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p).Hash}})}
$receipt=[ordered]@{diagnosticValid=$false;featureAcceptancePassed=$false;productDeployed=$false;productionCodeChanged=$true;candidateDefaultEnabled=$false;startedUtc=[DateTime]::UtcNow.ToString('o');sources=(Hashes)}
$receiptPath=Join-Path $ValidationRoot 'button-input-verification.json'
function Save-Receipt {$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $receiptPath -Encoding UTF8}
Save-Receipt
try {
    foreach($name in @('button-input','real-output','cold-input')) {
        $sourceName=if($name -eq 'button-input'){'screen_touchpads_native_button_acceptance.cpp'}elseif($name -eq 'real-output'){'screen_touchpads_native_real_output_acceptance.cpp'}else{'screen_touchpads_native_cold_acceptance.cpp'}
        $source=Join-Path $PSScriptRoot $sourceName;$exe=Join-Path $ValidationRoot "$name.exe";$obj=Join-Path $ValidationRoot "$name.obj";$cmd=Join-Path $ValidationRoot "build-$name.cmd"
        @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib advapi32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
        & $cmd;if($LASTEXITCODE -ne 0){throw "Input/output helper compilation failed: $name"}
        $report=Join-Path $ValidationRoot "$name-acceptance.json";if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
        & $exe $report|Out-Host;$exit=$LASTEXITCODE;if(!(Test-Path -LiteralPath $report)){throw "No fresh input/output report: $name"}
        $r=Get-Content -LiteralPath $report -Raw -Encoding UTF8|ConvertFrom-Json
        if(!$r.ownedTargetTerminal){throw "Owned input/output helper target not terminal: $name"}
        if($exit -ne 0 -or !$r.diagnosticValid){throw "Input/output diagnostic failure: $name"}
        if($name -eq 'button-input'){$buttons=$r;$receipt.buttonExeSha256=(Get-FileHash -LiteralPath $exe).Hash}elseif($name -eq 'real-output'){$output=$r;$receipt.realOutputExeSha256=(Get-FileHash -LiteralPath $exe).Hash}else{$cold=$r;$receipt.coldExeSha256=(Get-FileHash -LiteralPath $exe).Hash}
    }
    $receipt.coldSamples=@($cold.cases).Count;$receipt.coldFirstContactPassed=[bool]$cold.coldFirstContactPassed
    if(!$receipt.coldFirstContactPassed){throw 'Cold first contact / same-config reinitialization regression failed'}
    $receipt.buttonSamples=@($buttons.cases).Count;$receipt.buttonPassed=[bool]$buttons.buttonComponentPassed;$receipt.noDuplicateSamples=@($buttons.cases|Where-Object {$_.noDuplicateDelivery}).Count
    $receipt.realOutputCases=@($output.cases).Count;$receipt.realOutputPassed=[bool]$output.realBindingOutputPassed;$receipt.touchMouseTapMappingPassed=[bool]$output.touchMouseTapMappingPassed
    if(!$receipt.buttonPassed -or !$receipt.realOutputPassed -or !$receipt.touchMouseTapMappingPassed -or $receipt.noDuplicateSamples -ne $receipt.buttonSamples){throw 'Button route / real mapping output regression failed'}
    $receipt.diagnosticValid=$true
    $after=Hashes;for($i=0;$i -lt $after.Count;++$i){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during button/output verification: $($after[$i].path)"}}
    if(!$DiagnoseOnly){throw 'BUTTON_INPUT_NOT_FULL_ACCEPTANCE: hardware static and complete production activation remain unproven'}
    Write-Output "BUTTON_INPUT_DIAGNOSTIC_VALID buttonSamples=$($receipt.buttonSamples) realOutputCases=$($receipt.realOutputCases) featureAcceptancePassed=False"
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt}