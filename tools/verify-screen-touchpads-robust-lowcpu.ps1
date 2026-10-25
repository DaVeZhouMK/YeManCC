[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[Parameter(Mandatory=$true)][string]$AppExe,[string]$SourceRoot,[string]$BaselineNative)
$ErrorActionPreference='Stop'
$live=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source=if($SourceRoot){[IO.Path]::GetFullPath($SourceRoot)}else{$live}
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Force -Path $ValidationRoot | Out-Null
$receipt=[ordered]@{ok=$false;startedUtc=[DateTime]::UtcNow.ToString('o');sourceRoot=$source;hardwareWrites=0;productDeployed=$false;visibleDesktopSwitched=$false;hardwareTouchVerified=$false;endToEndCpuMeasured=$false;runs=@();sources=@()}
$required=@('native\main.cpp','native\screen_touchpads.h','native\touch_haptic_dispatch.h','native\touch_feedback_lease.h','native\msi_dinput_acquire_recovery.h','native\screen_touchpad_cursor.h')
foreach($file in $required){$path=Join-Path $source $file;$receipt.sources+=@([ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash})}
function Build-Unit([string]$name,[string]$file,[string]$include,[string]$define='') {
    $exe=Join-Path $ValidationRoot "$name.exe";$cmd=Join-Path $ValidationRoot "build-$name.cmd"
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE $define /I"$include" /I"$source\native" /I"$source\deps\json" /I"%CPPWINRT_INCLUDE%" "$file" /Fo"$ValidationRoot\$name.obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib shell32.lib
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd | Out-Host;if($LASTEXITCODE -ne 0){throw "Compile failed: $name"};return $exe
}
try {
    & (Join-Path $PSScriptRoot 'verify-screen-touchpads-haptics.ps1') -ValidationRoot (Join-Path $ValidationRoot 'Haptics') -AppExe $AppExe -SourceRoot $source
    $h=Get-Content -LiteralPath (Join-Path $ValidationRoot 'Haptics\haptics-verification.json') -Raw|ConvertFrom-Json
    $receipt.runs+=@([ordered]@{name='core-legacy-candidate-and-actual-app';ok=$h.ok;checks=($h.runs|Measure-Object checks -Sum).Sum;report=(Join-Path $ValidationRoot 'Haptics\haptics-verification.json')})
    foreach($unit in @(@('feedback-lease','screen_touchpads_feedback_lease_selftest.cpp'),@('event-time','screen_touchpads_event_time_selftest.cpp'))){
        $exe=Build-Unit $unit[0] (Join-Path $PSScriptRoot $unit[1]) (Join-Path $source 'native')
        $report=Join-Path $ValidationRoot ($unit[0]+'.json');& $exe $report | Out-Host;if($LASTEXITCODE -ne 0){throw "Unit failed: $($unit[0])"}
        $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
        if(!$r.ok -or $r.hardwareWrites -ne 0 -or $r.systemInputInjected){throw "Unsafe/failed unit: $($unit[0])"}
        $receipt.runs+=@([ordered]@{name=$unit[0];ok=$r.ok;checks=@($r.cases).Count;report=$report})
    }
    $hostExe=Build-Unit 'hidden-fixture-host' (Join-Path $PSScriptRoot 'screen_touchpads_hidden_fixture_host.cpp') (Join-Path $source 'native')
    $cursorExe=Build-Unit 'cursor-model' (Join-Path $PSScriptRoot 'screen_touchpads_cursor_selftest.cpp') (Join-Path $source 'native')
    $cursorReport=Join-Path $ValidationRoot 'cursor-model.json'
    & $hostExe $cursorExe '--model-only' $cursorReport | Out-Host;if($LASTEXITCODE -ne 0){throw 'Hidden cursor model failed'}
    $c=Get-Content -LiteralPath $cursorReport -Raw|ConvertFrom-Json
    if(!$c.ok -or $c.windowsTouchInjected -or $c.systemMouseOrKeyboardInjected -or $c.virtualTargetStarted){throw 'Invalid cursor model scope'}
    $receipt.runs+=@([ordered]@{name='hidden-cursor-model';ok=$c.ok;checks=@($c.cases).Count;report=$cursorReport})
    & (Join-Path $PSScriptRoot 'verify-screen-touchpads-mouse-pass-game-switch.ps1') -ValidationRoot (Join-Path $ValidationRoot 'GameSwitch')
    $g=Get-Content -LiteralPath (Join-Path $ValidationRoot 'GameSwitch\game-switch-after.json') -Raw|ConvertFrom-Json
    if(!$g.ok -or !$g.hiddenDesktop -or $g.visibleDesktopSwitched -or $g.systemInputInjected){throw 'Invalid hidden game-switch scope'}
    $receipt.runs+=@([ordered]@{name='hidden-game-switch';ok=$g.ok;checks=@($g.cases).Count;rounds=$g.rounds;hitRows=@($g.hitRows).Count;report=(Join-Path $ValidationRoot 'GameSwitch\game-switch-after.json')})
    # Measure only after builds/functional checks: no compiler running alongside this benchmark.
    $receipt.benchmarks=@();$kinds=if($BaselineNative){@('before','after')}else{@('after')}
    foreach($kind in $kinds){
        $include=if($kind -eq 'before'){$BaselineNative}else{Join-Path $source 'native'}
        $exe=Build-Unit "mapping-$kind" (Join-Path $PSScriptRoot 'screen_touchpads_mapping_cpu_benchmark.cpp') $include
        $report=Join-Path $ValidationRoot "mapping-$kind.json";& $exe $report *> (Join-Path $ValidationRoot "mapping-$kind.log")
        if($LASTEXITCODE -ne 0){throw "Benchmark failed: $kind"};$b=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
        if(!$b.ok -or $b.hardwareWrites -ne 0 -or $b.systemInputInjected -or $b.windowsCreated -ne 0){throw "Benchmark scope invalid: $kind"}
        $receipt.benchmarks+=@([ordered]@{kind=$kind;scope=$b.scope;report=$report;rows=$b.rows})
    }
    if($BaselineNative){
        $receipt.comparison=@();$before=$receipt.benchmarks[0].rows;$after=$receipt.benchmarks[1].rows
        if($before.Count -ne $after.Count){throw 'Benchmark workload mismatch'}
        for($i=0;$i -lt $before.Count;++$i){
            if($before[$i].workload -ne $after[$i].workload -or $before[$i].checksum -ne $after[$i].checksum -or $before[$i].pulses -ne $after[$i].pulses){throw "Mapping/feel changed: $($before[$i].workload)"}
            $receipt.comparison+=@([ordered]@{workload=$before[$i].workload;beforeMedianWallUs=$before[$i].medianWallUs;afterMedianWallUs=$after[$i].medianWallUs;ratio=$after[$i].medianWallUs/$before[$i].medianWallUs;beforeMedianCpuMs=$before[$i].medianCpuMs;afterMedianCpuMs=$after[$i].medianCpuMs;checksumEqual=$true;pulsesEqual=$true})
        }
    }
    foreach($file in $receipt.sources){if((Get-FileHash -LiteralPath $file.path).Hash -ne $file.sha256){throw "Source changed during acceptance: $($file.path)"}}
    $receipt.appSha256=(Get-FileHash -LiteralPath $AppExe).Hash;$receipt.totalChecks=($receipt.runs|ForEach-Object {$_['checks']}|Measure-Object -Sum).Sum;$receipt.ok=$true
    Write-Output "TOUCHPAD_ROBUST_LOWCPU_OK checks=$($receipt.totalChecks); no real device/actual game/end-to-end CPU claim"
} catch {$receipt.error=$_.Exception.Message;throw}
finally {$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');$receipt|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $ValidationRoot 'robust-lowcpu-verification.json') -Encoding utf8}
