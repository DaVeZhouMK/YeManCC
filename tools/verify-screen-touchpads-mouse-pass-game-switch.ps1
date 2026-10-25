[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$BaselineNative)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Path $ValidationRoot -Force|Out-Null
$after=Join-Path $ValidationRoot 'FrozenNative';New-Item -ItemType Directory -Path $after -Force|Out-Null
$receipt=[ordered]@{ok=$false;status='running';startedAtUtc=[DateTime]::UtcNow.ToString('o');
 hiddenDesktop=$true;systemInputInjected=$false;productDeployed=$false;hardwareTouchVerified=$false;sources=@();runs=@()}
try {
    $inputs=@(Get-ChildItem -LiteralPath (Join-Path $project 'native') -File -Filter '*.h')+@(
      Get-Item -LiteralPath (Join-Path $project 'deps\json\json.hpp'),(Join-Path $PSScriptRoot 'screen_touchpads_mouse_pass_game_switch_selftest.cpp'))
    foreach($input in $inputs){$sha=(Get-FileHash -LiteralPath $input.FullName).Hash;$receipt.sources+=@([ordered]@{path=$input.FullName;sha256=$sha});
      $copy=Join-Path $after $input.Name;Copy-Item -LiteralPath $input.FullName -Destination $copy -Force;
      if((Get-FileHash -LiteralPath $copy).Hash -ne $sha){throw "Source changed during snapshot: $($input.FullName)"}}
    $variants=@([ordered]@{name='after';include=$after})
    if($BaselineNative){
      $before=Join-Path $ValidationRoot 'BaselineNative';New-Item -ItemType Directory -Path $before -Force|Out-Null
      Get-ChildItem -LiteralPath $after -File|ForEach-Object{Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $before $_.Name) -Force}
      $baselineSources=@();Get-ChildItem -LiteralPath $BaselineNative -File -Filter '*.h'|ForEach-Object{
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $before $_.Name) -Force
        $baselineSources+=@([ordered]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash})}
      $receipt.baselineSources=$baselineSources;$variants=@([ordered]@{name='before';include=$before})+$variants
    }
    foreach($variant in $variants){$kind=$variant.name;$include=$variant.include;$exe=Join-Path $ValidationRoot "game-switch-$kind.exe";$cmd=Join-Path $ValidationRoot "build-game-switch-$kind.cmd"
      @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1 /I"$include" /I"%CPPWINRT_INCLUDE%" "$after\screen_touchpads_mouse_pass_game_switch_selftest.cpp" /Fo"$ValidationRoot\game-switch-$kind.obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib shell32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
      & $cmd;if($LASTEXITCODE -ne 0){throw "Fixture build failed: $kind"}
      $report=Join-Path $ValidationRoot "game-switch-$kind.json"
      if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
      & $exe $report;$code=$LASTEXITCODE
      if(!(Test-Path -LiteralPath $report)){throw "No fresh fixture report: $kind"}
      $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
      if(!$r.hiddenDesktop -or $r.visibleDesktopSwitched -or $r.systemInputInjected -or !$r.ownedTargetsTerminal){throw "Invalid isolation/cleanup result: $kind"}
      if($kind -eq 'after' -and ($code -ne 0 -or !$r.ok)){throw 'New production mouse-pass regression failed'}
      if($kind -eq 'before' -and ($code -eq 0 -or $r.ok -or !@($r.cases|Where-Object {$_.name -eq 'failed-native-refresh-hides-pad-input-and-outline' -and !$_.ok}).Count)){
        throw 'Previous native-refresh opaque fallback was not reproduced'}
      $receipt.runs+=@([ordered]@{kind=$kind;exitCode=$code;ok=$r.ok;checks=@($r.cases).Count;nativeHits=@($r.hitRows).Count;
        failedChecks=@($r.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name});report=$report;executableSha256=(Get-FileHash -LiteralPath $exe).Hash})
    }
    foreach($source in $receipt.sources){if((Get-FileHash -LiteralPath $source.path).Hash -ne $source.sha256){throw "Live source changed while verifying: $($source.path)"}}
    $receipt.ok=$true;$receipt.status='passed';Write-Output 'HIDDEN_GAME_SWITCH_VERIFIED; native hit targeting only, no actual game or physical touch claim'
}catch{$receipt.status='failed';$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $ValidationRoot 'mouse-pass-game-switch-verification.json') -Encoding utf8}
