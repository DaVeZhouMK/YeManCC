[CmdletBinding()]
param([string]$ValidationRoot,[switch]$Manual)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\native-stationary'))}
New-Item -ItemType Directory -Path $ValidationRoot -Force|Out-Null
$files=@('native\screen_touchpad_input_surface.h','native\screen_touchpads.h','native\screen_touchpad_cursor.h','native\screen_button_overlay.h','native\screen_standalone_special.h','native\screen_control_glyphs.h','deps\json\json.hpp',
    'tools\screen_touchpads_native_topology_test_host.h','tools\screen_touchpads_native_physical_acceptance.cpp','tools\verify-screen-touchpads-native-physical.ps1')
function Hashes {@($files|ForEach-Object{$path=Join-Path $project $_;[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash}})}
$receipt=[ordered]@{compiled=$false;manualRequested=[bool]$Manual;manualRunExecuted=$false;hardwareAcceptancePassed=$false;featureAcceptancePassed=$false;productDeployed=$false;systemInputInjected=$false;startedUtc=[DateTime]::UtcNow.ToString('o');sources=(Hashes)}
$receiptPath=Join-Path $ValidationRoot 'physical-touch-verification.json'
function Save-Receipt {$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $receiptPath -Encoding UTF8}
Save-Receipt
try {
    $source=Join-Path $PSScriptRoot 'screen_touchpads_native_physical_acceptance.cpp';$exe=Join-Path $ValidationRoot 'physical-touch.exe';$obj=Join-Path $ValidationRoot 'physical-touch.obj';$cmd=Join-Path $ValidationRoot 'build-physical-touch.cmd'
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd;if($LASTEXITCODE -ne 0){throw 'Physical input helper compile failed'}
    $receipt.compiled=$true;$receipt.exeSha256=(Get-FileHash -LiteralPath $exe).Hash
    $after=Hashes;for($i=0;$i -lt $after.Count;++$i){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during physical helper build: $($after[$i].path)"}}
    if(!$Manual){Write-Output 'PHYSICAL_HELPER_BUILT_NOT_EXECUTED: use -Manual only when ready at the touchscreen with a real mouse';return}
    Write-Host 'First click the owned white test window if it does not activate (up to 10 seconds). Then a 20-second interactive test: two fingers held still on the blue pad for 1 second, then concurrently mouse click, drag and wheel INSIDE the pad. First legacy Win32, then candidate. No injected input or binding output.'
    $report=Join-Path $ValidationRoot 'physical-touch-acceptance.json';if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
    & $exe $report --manual;$exit=$LASTEXITCODE;$receipt.manualRunExecuted=$true
    if(!(Test-Path -LiteralPath $report)){throw 'No fresh physical input report'}
    $result=Get-Content -LiteralPath $report -Raw -Encoding UTF8|ConvertFrom-Json
    if(!$result.ownedTargetTerminal){throw 'Owned physical input target still live; inspect same process, do not restart'}
    $receipt.hardwareAcceptancePassed=[bool]$result.hardwareAcceptancePassed;$receipt.testRunValid=[bool]$result.testRunValid
    if($exit -ne 0 -or !$result.hardwareAcceptancePassed){throw 'PHYSICAL_INPUT_NOT_ACCEPTED: missing/failed same-scope hardware evidence; feature acceptance remains false'}
    Write-Output 'PHYSICAL_COMPARISON_PASSED: featureAcceptancePassed remains false pending the full production scope'
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt}