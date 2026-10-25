[CmdletBinding()]
param([string]$ValidationRoot,[switch]$DiagnoseOnly)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\native-stationary'))}
New-Item -ItemType Directory -Path $ValidationRoot -Force|Out-Null
$files=@('native\screen_touchpads.h','native\screen_touchpad_input_surface.h','native\screen_touchpad_cursor.h','native\screen_button_overlay.h','native\screen_standalone_special.h','native\screen_control_glyphs.h','deps\json\json.hpp',
    'tools\screen_touchpads_native_topology_test_host.h','tools\screen_touchpads_native_palm_probe.cpp','tools\screen_touchpads_native_palm_policy_probe.cpp','tools\verify-screen-touchpads-native-stationary-options.ps1')
function Hashes {@($files|ForEach-Object{$p=Join-Path $project $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p).Hash}})}
$receipt=[ordered]@{diagnosticValid=$false;featureAcceptancePassed=$false;diagnoseOnly=[bool]$DiagnoseOnly;productDeployed=$false;productionCodeChanged=$false;startedUtc=[DateTime]::UtcNow.ToString('o');sources=(Hashes)}
$receiptPath=Join-Path $ValidationRoot 'stationary-options-verification.json'
function Save-Receipt {$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $receiptPath -Encoding UTF8}
Save-Receipt
try {
    foreach($name in @('palm','palm-policy')) {
        $sourceName=if($name -eq 'palm'){'screen_touchpads_native_palm_probe.cpp'}else{'screen_touchpads_native_palm_policy_probe.cpp'}
        $source=Join-Path $PSScriptRoot $sourceName;$exe=Join-Path $ValidationRoot "$name.exe";$obj=Join-Path $ValidationRoot "$name.obj";$cmd=Join-Path $ValidationRoot "build-$name.cmd"
        @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
        & $cmd;if($LASTEXITCODE -ne 0){throw "Stationary option probe compile failed: $name"}
        $report=Join-Path $ValidationRoot "$name-probe.json";if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
        & $exe $report|Out-Host;$exit=$LASTEXITCODE;if(!(Test-Path -LiteralPath $report)){throw "No fresh stationary option report: $name"}
        $result=Get-Content -LiteralPath $report -Raw -Encoding UTF8|ConvertFrom-Json
        if($exit -ne 0 -or !$result.diagnosticValid -or !$result.ownedTargetTerminal){throw "Stationary option APIs/input/cleanup invalid: $name"}
        if($name -eq 'palm'){$palm=$result;$receipt.palmExeSha256=(Get-FileHash -LiteralPath $exe).Hash}else{$policy=$result;$receipt.policyExeSha256=(Get-FileHash -LiteralPath $exe).Hash}
    }
    $receipt.registrationCases=@($palm.cases).Count;$receipt.registrationStaticCoreSamples=@($palm.cases|Where-Object {$_.coreHeldSamples -gt 0}).Count
    $receipt.policyCases=@($policy.cases).Count;$receipt.policyStaticJointPassed=@($policy.cases|Where-Object {$_.stationaryJointAcceptance}).Count
    $receipt.defaultPalmRejectionEnabled=[bool]$policy.cases[0].defaultPalmRejectionEnabled;$receipt.diagnosticValid=$true
    $after=Hashes;for($i=0;$i -lt $after.Count;++$i){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during stationary probes: $($after[$i].path)"}}
    if(!$DiagnoseOnly){throw 'STATIONARY_NOT_ACCEPTED: API success is not dual-static feature acceptance'}
    Write-Output "STATIONARY_OPTIONS_DIAGNOSTIC_VALID registration=$($receipt.registrationCases) policies=$($receipt.policyCases) featureAcceptancePassed=False"
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt}