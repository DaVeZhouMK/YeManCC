[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory,[Parameter(Mandatory=$true)][string]$BaselineDirectory)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$BaselineDirectory=[IO.Path]::GetFullPath($BaselineDirectory)
if(Test-Path -LiteralPath (Join-Path $OutputDirectory 'receipt.json')){throw 'Use a fresh output directory.'}
$receipt=[ordered]@{ok=$false;scope='Actual touch handlers, frozen includes, simulated UI, A/B/B/A process order; not end-to-end CPU';realInputInjected=0;realWindowsCreated=0;hardwareWrites=0;sources=@();executions=@()}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
try {
 $frozen=Join-Path $OutputDirectory 'frozen'
 New-Item -ItemType Directory -Path $frozen -Force|Out-Null
 foreach($file in Get-ChildItem -LiteralPath (Join-Path $project 'native') -File -Filter '*.h'){
  $copy=Join-Path $frozen $file.Name;Copy-Item -LiteralPath $file.FullName -Destination $copy
 }
 foreach($name in @('screen_touchpads_cpu_path_benchmark.cpp','screen_touchpads_button_cpu_simulation.cpp')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $frozen $name)}
 Copy-Item -LiteralPath (Join-Path $project 'deps/json/json.hpp') -Destination (Join-Path $frozen 'json.hpp')
 foreach($name in @('screen_touchpads.h','screen_button_overlay.h','touch_haptic_dispatch.h','screen_touchpads_cpu_path_benchmark.cpp','screen_touchpads_button_cpu_simulation.cpp')){
  $receipt.sources+=@{variant='current';file=$name;sha256=(Get-FileHash -LiteralPath (Join-Path $frozen $name)).Hash}
 }
 foreach($kind in @('before','after')){
  $inc=Join-Path $OutputDirectory $kind;New-Item -ItemType Directory -Path $inc -Force|Out-Null
  # Include siblings resolve from their copied source directory; copy the full
  # frozen set to each variant before overriding only declared baseline files.
  foreach($file in Get-ChildItem -LiteralPath $frozen -File){Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $inc $file.Name)}
  if($kind -eq 'before'){
   foreach($name in @('screen_touchpads.h','screen_button_overlay.h','touch_haptic_dispatch.h')){
    $old=Join-Path $BaselineDirectory $name;if(!(Test-Path -LiteralPath $old)){throw "Missing baseline $name"}
    Copy-Item -LiteralPath $old -Destination (Join-Path $inc $name) -Force
    $receipt.sources+=@{variant='before';file=$name;sha256=(Get-FileHash -LiteralPath $old).Hash}
   }
  }
  $cmd=Join-Path $inc 'build.cmd'
  @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$inc" "$inc\screen_touchpads_cpu_path_benchmark.cpp" /Fo"$inc\bench.obj" /Fe"$inc\bench.exe" /link user32.lib gdi32.lib ole32.lib
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
  & $cmd 2>&1 | Tee-Object -FilePath (Join-Path $inc 'build.log');if($LASTEXITCODE -ne 0){throw "Compile failed: $kind"}
 }
 $pass=0
 foreach($kind in @('before','after','after','before')){
  ++$pass;$report=Join-Path $OutputDirectory "$pass-$kind.json"
  & (Join-Path $OutputDirectory "$kind/bench.exe") $report
  if($LASTEXITCODE -ne 0){throw "Workload failed: $pass-$kind"}
  $data=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
  if(!$data.ok -or $data.realInputInjected -ne 0 -or $data.realWindowsCreated -ne 0){throw 'Invalid report'}
  $receipt.executions+=@{pass=$pass;kind=$kind;report=$report}
 }
 $receipt.ok=$true
} catch {$receipt.error=$_.Exception.Message;throw}
finally {$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
