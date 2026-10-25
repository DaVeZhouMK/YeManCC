[CmdletBinding()]
param([string]$ValidationRoot,[switch]$DiagnoseOnly)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\native-topology'))}
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Force -Path $ValidationRoot|Out-Null
$files=@('native\screen_touchpads.h','native\screen_touchpad_cursor.h','native\screen_button_overlay.h','native\screen_standalone_special.h','native\screen_control_glyphs.h','deps\json\json.hpp',
    'tools\screen_touchpads_native_topology_test_host.h','tools\screen_touchpads_native_topology_probe.cpp','tools\screen_touchpads_native_topology_stress.cpp','tools\verify-screen-touchpads-native-topology.ps1')
function SourceHashes {@($files|ForEach-Object{$p=Join-Path $project $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}})}
$receipt=[ordered]@{diagnosticValid=$false;candidateFound=$false;candidateStressPassed=$false;coreAdapterStressPassed=$false;featureAcceptancePassed=$false;
    scope='experimental-native-topology-not-production-acceptance';diagnoseOnly=[bool]$DiagnoseOnly;productDeployed=$false;productionCodeChanged=$false;
    startedUtc=[DateTime]::UtcNow.ToString('o');sources=(SourceHashes);helpers=@()}
$receiptPath=Join-Path $ValidationRoot 'native-topology-verification.json'
function Save-Receipt {$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $receiptPath -Encoding utf8}
Save-Receipt
try {
    $vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    foreach($name in @('probe','stress')) {
        $source=Join-Path $PSScriptRoot "screen_touchpads_native_topology_$name.cpp"
        $exe=Join-Path $ValidationRoot "native-topology-$name.exe";$obj=Join-Path $ValidationRoot "native-topology-$name.obj"
        $cmd=Join-Path $ValidationRoot "build-native-topology-$name.cmd"
        @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
        & $cmd
        if($LASTEXITCODE -ne 0){throw "Native topology $name compilation failed"}
        $receipt.helpers+=@([ordered]@{path=$exe;sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash})
    }
    function Run-Helper([string]$Name,[string]$Report,[string[]]$Arguments=@()) {
        $exe=Join-Path $ValidationRoot "native-topology-$Name.exe";$path=Join-Path $ValidationRoot $Report
        if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}
        & $exe $path @Arguments | Out-Host
        $exitCode=$LASTEXITCODE
        if(!(Test-Path -LiteralPath $path)){throw "No fresh topology report: $Report"}
        $r=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
        if(!$r.ownedTargetTerminal){throw "Owned helper has not exited: $Report; inspect the same process, do not restart it"}
        if(!$r.diagnosticValid){throw "Input positive controls invalid: $Report (exit $exitCode); not evidence against a routing hypothesis"}
        if($exitCode -notin @(0,2)){throw "Unexpected topology helper exit: $exitCode"}
        return $r
    }
    $matrix=Run-Helper 'probe' 'native-topology-probe.json'
    $raw=Run-Helper 'stress' 'native-topology-stress.json' @('--vanilla-native')
    $core=Run-Helper 'stress' 'native-topology-core-adapter.json' @('--adapt-core','--vanilla-native')
    $receipt.diagnosticValid=$true;$receipt.topologySamples=@($matrix.cases).Count;$receipt.topologyPassingSamples=$matrix.passingSamples
    $receipt.rawStressSamples=@($raw.cases).Count;$receipt.coreAdapterSamples=@($core.cases).Count
    $receipt.candidateStressPassed=[bool]$raw.candidateStressPassed;$receipt.coreAdapterStressPassed=[bool]$core.candidateStressPassed
    $receipt.topologyCandidateFound=[bool]$matrix.candidateFound; $receipt.stationaryMultiTouchPositiveControlValid=[bool]$core.stationaryMultiTouchPositiveControlValid
    $receipt.candidateFound=[bool]$matrix.candidateFound -and $receipt.candidateStressPassed -and $receipt.coreAdapterStressPassed
    # No production input-window adapter has been installed or accepted yet.
    # A passing prototype must never open the main production acceptance gate.
    $receipt.featureAcceptancePassed=$false
    $after=SourceHashes
    for($i=0;$i -lt $after.Count;$i++){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during native topology verification: $($after[$i].path)"}}
    if(!$DiagnoseOnly){throw 'NATIVE_TOPOLOGY_NOT_IMPLEMENTED: experimental evidence cannot satisfy production acceptance'}
    Write-Output "NATIVE_TOPOLOGY_DIAGNOSTIC_VALID candidateFound=$($receipt.candidateFound) featureAcceptancePassed=False topology=$($receipt.topologySamples) raw=$($receipt.rawStressSamples) core=$($receipt.coreAdapterSamples)"
} catch {$receipt.error=$_.Exception.Message;throw}
finally {$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt}