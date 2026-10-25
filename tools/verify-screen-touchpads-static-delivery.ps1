[CmdletBinding()]
param([string]$ValidationRoot)
$ErrorActionPreference='Stop'
$env:PSModulePath=[IO.Path]::Combine($PSHOME,'Modules')+';'+$env:PSModulePath
Import-Module Microsoft.PowerShell.Utility -ErrorAction Stop
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\static-delivery'))}
New-Item -ItemType Directory -Force -Path $ValidationRoot|Out-Null
$source=Join-Path $PSScriptRoot 'screen_touchpads_static_delivery_probe.cpp'
$exe=Join-Path $ValidationRoot 'static-delivery.exe';$obj=Join-Path $ValidationRoot 'static-delivery.obj';$cmd=Join-Path $ValidationRoot 'build-static-delivery.cmd'
$files=@('native\screen_touchpads.h','native\screen_touchpad_input_surface.h','native\screen_button_overlay.h','tools\screen_touchpads_native_topology_test_host.h','tools\screen_touchpads_static_delivery_probe.cpp','tools\screen_touchpads_event_time_selftest.cpp','tools\verify-screen-touchpads-static-delivery.ps1')
function Hashes{@($files|ForEach-Object{$p=Join-Path $project $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p).Hash}})}
$receipt=[ordered]@{diagnosticValid=$false;featureAcceptancePassed=$false;hardwareTouchVerified=$false;productDeployed=$false;sources=(Hashes);runs=@();startedUtc=[DateTime]::UtcNow.ToString('o')}
try{
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd;if($LASTEXITCODE -ne 0){throw 'Static delivery probe compile failed'}
    foreach($mode in @('default','mouse-pointer','synthetic','synthetic-mouse-pointer')){
        $report=Join-Path $ValidationRoot "$mode.json"
        $arguments=@($report);if($mode -like '*mouse-pointer*'){$arguments+='--mouse-pointer'};if($mode -like 'synthetic*'){$arguments+='--synthetic'}
        & $exe @arguments|Out-Host
        $exit=$LASTEXITCODE;$r=Get-Content -LiteralPath $report -Raw -Encoding UTF8|ConvertFrom-Json
        $receipt.runs+=@([ordered]@{mode=$mode;report=$report;exit=$exit;valid=[bool]$r.diagnosticValid;cases=@($r.cases).Count})
        if($exit -ne 0 -or !$r.diagnosticValid -or !$r.ownedTargetTerminal){throw "Input delivery probe invalid: $mode"}
        $falseTaps=@($r.cases|Where-Object {$_.route -like 'production-*' -and $_.lateLongHoldMisclassifiedAsTap})
        if($falseTaps.Count){throw "Queued long hold misclassified as tap: $mode"}
    }
    $clockExe=Join-Path $ValidationRoot 'event-time.exe';$clockObj=Join-Path $ValidationRoot 'event-time.obj';$clockCmd=Join-Path $ValidationRoot 'build-event-time.cmd'
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" "$project\tools\screen_touchpads_event_time_selftest.cpp" /Fo"$clockObj" /Fe"$clockExe" /link user32.lib gdi32.lib
"@|Set-Content -LiteralPath $clockCmd -Encoding ascii
    & $clockCmd;if($LASTEXITCODE -ne 0){throw 'Clock regression compile failed'}
    & $clockExe (Join-Path $ValidationRoot 'event-time.json')|Out-Host;if($LASTEXITCODE -ne 0){throw 'Clock regression failed'}
    $receipt.clockCases=10
    $after=Hashes;for($i=0;$i -lt $after.Count;$i++){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during probe: $($after[$i].path)"}}
    $receipt.tapSemanticsPassed=$true;$receipt.diagnosticValid=$true;$receipt.exeSha256=(Get-FileHash -LiteralPath $exe).Hash
    Write-Output 'STATIC_DELIVERY_DIAGNOSED: product and hardware acceptance remain false'
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $ValidationRoot 'verification.json') -Encoding UTF8}
