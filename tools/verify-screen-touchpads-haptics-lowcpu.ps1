[CmdletBinding()]
param([string]$ValidationRoot,[string]$AppExe,[string]$BaselineInclude,[string]$SourceRoot)
$ErrorActionPreference='Stop'
$liveProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project=if($SourceRoot){[IO.Path]::GetFullPath($SourceRoot)}else{$liveProject}
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenTouchpads-Haptics-LowCpu'))}
New-Item -ItemType Directory -Path $ValidationRoot -Force|Out-Null
$receipt=[ordered]@{ok=$false;scope='pure Core microbenchmark and simulated physical writer; not end-to-end YMCC CPU';hardwareWrites=0;productDeployed=$false;startedUtc=[DateTime]::UtcNow.ToString('o')}
try{
    & (Join-Path $PSScriptRoot 'verify-screen-touchpads-haptics.ps1') -ValidationRoot $ValidationRoot -AppExe $AppExe -SourceRoot $SourceRoot
    $haptics=Get-Content -LiteralPath (Join-Path $ValidationRoot 'haptics-verification.json') -Raw|ConvertFrom-Json
    $receipt.hapticChecks=($haptics.runs|Measure-Object checks -Sum).Sum;$receipt.sources=$haptics.sources;$receipt.appSha256=$haptics.appSha256
    if(!$BaselineInclude -and (Test-Path -LiteralPath (Join-Path $ValidationRoot 'before-source\screen_touchpads.h'))){$BaselineInclude=Join-Path $ValidationRoot 'before-source'}
    $kinds=if($BaselineInclude){@('before','after')}else{@('after')};$receipt.benchmarks=@()
    foreach($kind in $kinds){
        $inc=if($kind -eq 'before'){$BaselineInclude}else{Join-Path $project 'native'}
        $exe=Join-Path $ValidationRoot "benchmark-$kind.exe";$cmd=Join-Path $ValidationRoot "build-benchmark-$kind.cmd"
        @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$inc" /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$liveProject\tools\screen_touchpads_haptic_benchmark.cpp" /Fo"$ValidationRoot\benchmark-$kind.obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
        & $cmd;if($LASTEXITCODE -ne 0){throw "Benchmark build failed: $kind"}
        $report=Join-Path $ValidationRoot "benchmark-$kind.json";& $exe $report|Out-Host
        if($LASTEXITCODE -ne 0){throw "Benchmark failed: $kind"}
        $b=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
        if(!$b.ok -or $b.hardwareWrites -ne 0 -or $b.systemInputInjected){throw "Invalid benchmark safety: $kind"}
        $receipt.benchmarks+=@([ordered]@{kind=$kind;sourceSha256=(Get-FileHash -LiteralPath (Join-Path $inc 'screen_touchpads.h')).Hash;report=$report;rows=$b.rows})
    }
    if($BaselineInclude){
        $receipt.comparison=@();$before=$receipt.benchmarks[0].rows;$after=$receipt.benchmarks[1].rows
        for($i=0;$i -lt $before.Count;++$i){
            if($before[$i].checksum -ne $after[$i].checksum){throw "Mouse mapping checksum changed in benchmark: $($before[$i].workload)"}
            $receipt.comparison+=@([ordered]@{workload=$before[$i].workload;beforeMedianWallUs=$before[$i].medianWallUs;afterMedianWallUs=$after[$i].medianWallUs;ratio=$after[$i].medianWallUs/$before[$i].medianWallUs})
        }
    }
    $receipt.ok=$true;Write-Output "LOWCPU_HAPTICS_VERIFIED hapticChecks=$($receipt.hapticChecks); pure-event CPU only, no real motor claim"
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');$receipt|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $ValidationRoot 'lowcpu-verification.json') -Encoding UTF8}
