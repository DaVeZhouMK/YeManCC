[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Path $ValidationRoot -Force|Out-Null
$native=Join-Path $ValidationRoot 'FrozenNative'
New-Item -ItemType Directory -Path $native -Force|Out-Null
$inputs=@(Get-ChildItem -LiteralPath (Join-Path $project 'native') -File -Filter '*.h')+@(
    Get-Item -LiteralPath (Join-Path $project 'deps\json\json.hpp'),
    (Join-Path $PSScriptRoot 'screen_touchpads_mapping_cpu_benchmark.cpp'))
$receipt=[ordered]@{ok=$false;status='running';startedAtUtc=[DateTime]::UtcNow.ToString('o');
    hardwareWrites=0;systemInputInjected=$false;productDeployed=$false;sources=@()}
try {
    foreach($input in $inputs) {
        $receipt.sources+=@([ordered]@{path=$input.FullName;sha256=(Get-FileHash -LiteralPath $input.FullName).Hash})
        $destination=Join-Path $native $input.Name
        Copy-Item -LiteralPath $input.FullName -Destination $destination -Force
        if((Get-FileHash -LiteralPath $destination).Hash -ne $receipt.sources[-1].sha256){throw "Input changed during snapshot: $($input.FullName)"}
    }
    $exe=Join-Path $ValidationRoot 'mapping-cpu-benchmark.exe'
    $obj=Join-Path $ValidationRoot 'mapping-cpu-benchmark.obj'
    $cmd=Join-Path $ValidationRoot 'build-mapping-cpu-benchmark.cmd'
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$native" "$native\screen_touchpads_mapping_cpu_benchmark.cpp" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd
    if($LASTEXITCODE -ne 0){throw 'Mapping CPU benchmark build failed'}
    $report=Join-Path $ValidationRoot 'mapping-cpu-benchmark.json'
    & $exe $report
    if($LASTEXITCODE -ne 0){throw 'Mapping CPU benchmark failed'}
    $result=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
    if(!$result.ok -or $result.hardwareWrites -ne 0 -or $result.systemInputInjected){throw 'Invalid safety/checksum result'}
    foreach($input in $receipt.sources) {
        if((Get-FileHash -LiteralPath $input.path).Hash -ne $input.sha256){throw "Source changed while measuring: $($input.path)"}
    }
    $receipt.ok=$true;$receipt.status='passed';$receipt.report=$report
    $receipt.executableSha256=(Get-FileHash -LiteralPath $exe).Hash
    Write-Output "MAPPING_CPU_RESEARCH_VERIFIED workloads=$(@($result.rows).Count); not end-to-end YMCC CPU"
} catch {$receipt.status='failed';$receipt.error=$_.Exception.Message;throw}
finally {$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $ValidationRoot 'mapping-cpu-verification.json') -Encoding utf8}
