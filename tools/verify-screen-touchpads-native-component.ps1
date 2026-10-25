[CmdletBinding()]
param([string]$ValidationRoot,[switch]$DiagnoseOnly)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\native-component'))}
New-Item -ItemType Directory -Path $ValidationRoot -Force|Out-Null
$files=@('native\screen_touchpads.h','native\screen_touchpad_input_surface.h','native\screen_touchpad_cursor.h','native\screen_button_overlay.h','native\screen_standalone_special.h','native\screen_control_glyphs.h','deps\json\json.hpp',
    'tools\screen_touchpads_native_component_acceptance.cpp','tools\screen_touchpads_native_stationary_probe.cpp','tools\screen_touchpads_native_mapping_selftest.cpp','tools\screen_touchpads_native_dual_acceptance.cpp','tools\screen_touchpads_native_ownership_selftest.cpp','tools\screen_touchpads_native_topology_test_host.h','tools\verify-screen-touchpads-native-component.ps1')
function Hashes {@($files|ForEach-Object{$p=Join-Path $project $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p).Hash}})}
$receipt=[ordered]@{featureAcceptancePassed=$false;diagnosticValid=$false;candidateCompletePassed=$false;productDeployed=$false;candidateDefaultEnabled=$false;productionLifecycleCodePresent=$true;diagnoseOnly=[bool]$DiagnoseOnly;sources=(Hashes);startedUtc=[DateTime]::UtcNow.ToString('o')}
$receiptPath=Join-Path $ValidationRoot 'component-verification.json'
function Save-Receipt {$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $receiptPath -Encoding UTF8}
Save-Receipt
try {
    foreach($test in @(@('component','screen_touchpads_native_component_acceptance.cpp'),@('stationary','screen_touchpads_native_stationary_probe.cpp'),@('mapping','screen_touchpads_native_mapping_selftest.cpp'),@('dual','screen_touchpads_native_dual_acceptance.cpp'),@('ownership','screen_touchpads_native_ownership_selftest.cpp'))) {
        $name=$test[0];$source=Join-Path $PSScriptRoot $test[1];$exe=Join-Path $ValidationRoot "$name.exe";$obj=Join-Path $ValidationRoot "$name.obj";$cmd=Join-Path $ValidationRoot "build-$name.cmd"
        @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
        & $cmd;if($LASTEXITCODE -ne 0){throw "Component test compilation failed: $name"}
        $report=Join-Path $ValidationRoot "$name-acceptance.json";if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
        & $exe $report|Out-Host;$exit=$LASTEXITCODE
        if(!(Test-Path -LiteralPath $report)){throw "No fresh component report: $name"}
        $result=Get-Content -LiteralPath $report -Raw -Encoding UTF8|ConvertFrom-Json
        if(!$result.ownedTargetTerminal){throw "Owned target not terminal: $name"}
        if(($name -eq 'ownership' -and !$result.ok) -or ($name -ne 'ownership' -and !$result.diagnosticValid)){throw "Invalid test positive control: $name"}
        if($exit -notin @(0,2)){throw "Unexpected test exit $exit"}
        if($name -eq 'component'){$component=$result;$receipt.componentExeSha256=(Get-FileHash -LiteralPath $exe).Hash}elseif($name -eq 'stationary'){$stationary=$result;$receipt.stationaryExeSha256=(Get-FileHash -LiteralPath $exe).Hash}elseif($name -eq 'mapping'){$mapping=$result;$receipt.mappingExeSha256=(Get-FileHash -LiteralPath $exe).Hash}elseif($name -eq 'dual'){$dual=$result;$receipt.dualExeSha256=(Get-FileHash -LiteralPath $exe).Hash}else{$ownership=$result;$receipt.ownershipExeSha256=(Get-FileHash -LiteralPath $exe).Hash}
    }
    $receipt.ownershipChecks=@($ownership.cases).Count;$receipt.ownershipPassed=[bool]$ownership.ok
    if(!$receipt.ownershipPassed){throw 'Native surface ownership regression failed'}
    $receipt.dualChecks=@($dual.cases).Count;$receipt.dualPassed=[bool]$dual.dualProductionComponentPassed
    if(!$receipt.dualPassed){throw 'Dual pad native component regression failed'}
    $receipt.mappingChecks=@($mapping.cases).Count;$receipt.mappingsPassed=[bool]$mapping.productionMappingsPassed
    if(!$receipt.mappingsPassed){throw 'Production mapping/admission component regression failed'}
    $receipt.diagnosticValid=$true;$receipt.motionChecks=@($component.cases).Count;$receipt.motionPassed=@($component.cases|Where-Object {$_.jointAcceptance}).Count
    $receipt.coordinatesPassed=@($component.cases|Where-Object {$_.coreCoordinatesMatch}).Count;$receipt.candidateCompletePassed=[bool]$component.candidateStressPassed
    $receipt.cacheChecks=@($component.componentCache).Count;$receipt.cachePassed=@($component.componentCache|Where-Object {$_.sameFrameRetained -and $_.forceReupload}).Count
    $receipt.syntheticOriginChecks=$component.syntheticOriginChecks;$receipt.syntheticOriginsNotMislabelledHardware=[bool]$component.syntheticOriginsNotMislabelledHardware
    if(!$receipt.syntheticOriginsNotMislabelledHardware){throw 'Synthetic input provenance is unknown/mislabelled; never accept it as hardware evidence'}
    $receipt.noDuplicateDeliveryChecks=@($component.cases|Where-Object {$_.noDuplicateDelivery}).Count
    $receipt.closedChecks=@($component.componentClosed).Count;$receipt.closedPassed=@($component.componentClosed|Where-Object {$_}).Count
    $receipt.stationaryProbeChecks=@($stationary.cases).Count;$receipt.stationaryProbePassed=@($stationary.cases|Where-Object {$_.stationaryAcceptance}).Count
    $receipt.stationaryLegacyPositiveControlValid=@($stationary.cases|Where-Object {$_.implementation -eq 'legacy-Win32' -and $_.stationaryAcceptance}).Count -gt 0
    if($receipt.motionPassed -ne $receipt.motionChecks -or $receipt.coordinatesPassed -ne $receipt.motionChecks -or $receipt.noDuplicateDeliveryChecks -ne $receipt.motionChecks -or $receipt.cachePassed -ne $receipt.cacheChecks -or $receipt.closedPassed -ne $receipt.closedChecks){throw 'Native component motion/coordinate/duplicate/cache/lifecycle regression failed'}
    $after=Hashes;for($i=0;$i -lt $after.Count;++$i){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during component verification: $($after[$i].path)"}}
    # This gate includes unresolved static multi-touch and real production scope.
    # Motion/cache success is never a substitute for the whole objective.
    $receipt.featureAcceptancePassed=$false
    if(!$DiagnoseOnly){throw 'NATIVE_COMPONENT_NOT_ACCEPTED: full production/dual-static acceptance remains unproven'}
    Write-Output "NATIVE_COMPONENT_DIAGNOSTIC_VALID motion=$($receipt.motionPassed)/$($receipt.motionChecks) candidateComplete=$($receipt.candidateCompletePassed) featureAcceptancePassed=False"
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt}