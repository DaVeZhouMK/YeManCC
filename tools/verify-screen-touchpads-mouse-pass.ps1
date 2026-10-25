[CmdletBinding()]
param([string]$ValidationRoot,[switch]$DiagnoseOnly)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\acceptance'))}
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Force -Path $ValidationRoot | Out-Null
$sourceFiles=@('native\screen_touchpads.h','native\screen_touchpad_cursor.h','native\screen_button_overlay.h','native\screen_standalone_special.h','native\monitor_desktop_hud.h','tools\screen_touchpads_mouse_pass_acceptance.cpp','tools\verify-screen-touchpads-mouse-pass.ps1')
function SourceHashes {@($sourceFiles|ForEach-Object{$p=Join-Path $project $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}})}
$receipt=[ordered]@{diagnosticValid=$false;featureAcceptancePassed=$false;diagnoseOnly=[bool]$DiagnoseOnly;productDeployed=$false;productionCodeChanged=$false;startedUtc=[DateTime]::UtcNow.ToString('o');sources=(SourceHashes)}
$receiptPath=Join-Path $ValidationRoot 'mouse-pass-verification.json'
function Save-Receipt {$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $receiptPath -Encoding utf8}
Save-Receipt
try {
 $vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
 $source=Join-Path $PSScriptRoot 'screen_touchpads_mouse_pass_acceptance.cpp'
 $exe=Join-Path $ValidationRoot 'mouse-pass-acceptance.exe';$obj=Join-Path $ValidationRoot 'mouse-pass-acceptance.obj'
 $cmd=Join-Path $ValidationRoot 'build-mouse-pass-acceptance.cmd'
 @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$source" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
 & $cmd
 if($LASTEXITCODE -ne 0){throw 'Mouse pass acceptance compile failed'}
 $report=Join-Path $ValidationRoot 'mouse-pass-acceptance.json'
 if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
 if($DiagnoseOnly){& $exe $report|Out-Null}else{& $exe $report --require-pass|Out-Null}
 $exitCode=$LASTEXITCODE
 if(!(Test-Path -LiteralPath $report)){throw 'No fresh mouse pass report'}
 $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
 $receipt.diagnosticValid=[bool]$r.diagnosticValid;$receipt.featureAcceptancePassed=[bool]$r.featureAcceptancePassed
 $receipt.candidateFound=[bool]$r.candidateFound;$receipt.cases=@($r.cases).Count;$receipt.ownedTargetTerminal=$r.ownedTargetTerminal;$receipt.exitCode=$exitCode
 $after=SourceHashes
 for($i=0;$i -lt $after.Count;$i++){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during test: $($after[$i].path)"}}
 $receipt.helperSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
 if(!$r.diagnosticValid){throw 'Cross-process input controls failed; results cannot support conclusions'}
 if(!$DiagnoseOnly -and !$r.featureAcceptancePassed){throw 'MOUSE_PASS_NOT_IMPLEMENTED: candidate/production acceptance is still not satisfied'}
 if($exitCode -ne 0){throw "Mouse pass helper failed: $exitCode"}
 Write-Output "MOUSE_PASS_DIAGNOSTIC_VALID featureAcceptancePassed=$($r.featureAcceptancePassed) cases=$($receipt.cases)"
} catch {$receipt.error=$_.Exception.Message;throw}
finally {$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-Receipt}
