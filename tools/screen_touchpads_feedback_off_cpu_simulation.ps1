[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$BaselineDirectory,[switch]$ExpectBaseline)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath (Join-Path $OutputDirectory 'receipt.json')){throw 'Use a fresh output directory.'}
$native=Join-Path $OutputDirectory 'FrozenNative'
New-Item -ItemType Directory -Force -Path $native | Out-Null
$receipt=[ordered]@{ok=$false;scope='Actual touch handler with virtual time and input/cursor stubs; no real windows or device work';baseline=[bool]$ExpectBaseline;sourceHashes=@();realInputInjected=0;realWindowsCreated=0;cpuPercentMeasured=$false}
try {
 foreach($file in Get-ChildItem -LiteralPath (Join-Path $project 'native') -File -Filter '*.h'){
  $destination=Join-Path $native $file.Name
  Copy-Item -LiteralPath $file.FullName -Destination $destination
 }
 Copy-Item -LiteralPath (Join-Path $project 'deps/json/json.hpp') -Destination (Join-Path $native 'json.hpp')
 if($BaselineDirectory){foreach($name in @('screen_touchpads.h','touch_haptic_dispatch.h')){Copy-Item -LiteralPath (Join-Path $BaselineDirectory $name) -Destination (Join-Path $native $name) -Force}}
 foreach($name in @('screen_touchpads.h','screen_button_overlay.h','screen_standalone_special.h','touch_haptic_dispatch.h')){
  $receipt.sourceHashes+=@{name=$name;sha256=(Get-FileHash -LiteralPath (Join-Path $native $name)).Hash}
 }
 $source=Join-Path $PSScriptRoot 'screen_touchpads_feedback_off_cpu_simulation.cpp'
 Copy-Item -LiteralPath $source -Destination (Join-Path $native 'screen_touchpads_feedback_off_cpu_simulation.cpp')
 $exe=Join-Path $OutputDirectory 'feedback-off-cpu-simulation.exe';$obj=Join-Path $OutputDirectory 'feedback-off-cpu-simulation.obj'
 $cmd=Join-Path $OutputDirectory 'build.cmd'
 @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$native" "$native\screen_touchpads_feedback_off_cpu_simulation.cpp" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
 & $cmd 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'compile.log')
 if($LASTEXITCODE -ne 0){throw 'Simulation compilation failed'}
 $report=Join-Path $OutputDirectory 'simulation.json'
 if($ExpectBaseline){& $exe $report 'baseline'}else{& $exe $report}
 if($LASTEXITCODE -ne 0){throw 'Simulation failed'}
 $result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
 if(!$result.ok -or $result.realInputInjected -ne 0 -or $result.realWindowsCreated -ne 0){throw 'Simulation evidence invalid'}
 $receipt.ok=$true;$receipt.checks=$result.cases.Count;$receipt.disabledMotionStateChanges=$result.disabledMotionStateChanges
} catch {$receipt.error=$_.Exception.Message;throw}
finally {$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
