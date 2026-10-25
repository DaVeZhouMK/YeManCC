[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$BaselineDirectory,[switch]$ExpectBaselineFailure)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath (Join-Path $OutputDirectory 'receipt.json')){throw 'Use a fresh evidence output directory.'}
$native=Join-Path $OutputDirectory 'FrozenNative'
New-Item -ItemType Directory -Force -Path $native|Out-Null
$receipt=[ordered]@{ok=$false;scope='Native User32/GDI on private never-switched desktop; synthetic cursor state; not Steam/scanout proof';sources=@();baselineExpected=[bool]$ExpectBaselineFailure;systemInputInjected=$false;systemCursorManipulated=$false;productDeployed=$false}
try {
 foreach($file in Get-ChildItem -LiteralPath (Join-Path $project 'native') -File -Filter '*.h'){
  $copy=Join-Path $native $file.Name;Copy-Item -LiteralPath $file.FullName -Destination $copy
 }
 Copy-Item -LiteralPath (Join-Path $project 'deps/json/json.hpp') -Destination (Join-Path $native 'json.hpp')
 if($BaselineDirectory){foreach($name in @('screen_touchpad_cursor.h','screen_touchpads.h')){Copy-Item -LiteralPath (Join-Path $BaselineDirectory $name) -Destination (Join-Path $native $name) -Force}}
 foreach($name in @('screen_touchpads.h','screen_touchpad_cursor.h','screen_touchpad_cursor_visibility.h','screen_touchpad_display_policy.h')){
  $receipt.sources+=@{name=$name;sha256=(Get-FileHash -LiteralPath (Join-Path $native $name)).Hash}
 }
 $source=Join-Path $PSScriptRoot 'screen_touchpad_cursor_native_surface_selftest.cpp'
 Copy-Item -LiteralPath $source -Destination (Join-Path $native 'screen_touchpad_cursor_native_surface_selftest.cpp')
 $receipt.sources+=@{name='screen_touchpad_cursor_native_surface_selftest.cpp';sha256=(Get-FileHash -LiteralPath $source).Hash}
 $cmd=Join-Path $OutputDirectory 'build.cmd';$exe=Join-Path $OutputDirectory 'surface.exe'
 @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /I"$native" "$native\screen_touchpad_cursor_native_surface_selftest.cpp" /Fo"$OutputDirectory\surface.obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
 & $cmd 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'compile.log')
 if($LASTEXITCODE -ne 0){throw 'Native surface compilation failed'}
 $report=Join-Path $OutputDirectory 'native-surface.json'
 & $exe $report
 $code=$LASTEXITCODE
 $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
 if(!$r.hiddenDesktop -or !$r.inputDesktopUnchanged -or $r.systemInputInjected -or $r.systemCursorManipulated -or $r.visibleDesktopSwitched){throw 'Native surface isolation/cleanup evidence invalid'}
 $failures=@($r.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name})
 if($ExpectBaselineFailure){
  $expected=@('next-suppressed-frame-recovers-native-surface','128-native-shape-changes-retain-valid-inert-surface','native-render-path-reported-no-error','12-native-style-losses-recover-before-shutdown')
  if($code -ne 3 -or $r.ok -or @(Compare-Object ($expected|Sort-Object) ($failures|Sort-Object)).Count -ne 0){throw 'Baseline did not reproduce exactly the known recovery failures'}
  $diag=@($r.diagnostics|Where-Object {$_.label -eq 'after-next-upload'})
  if($diag.Count -ne 1 -or $diag[0].error -ne 87){throw 'Baseline error87 missing'}
 }elseif($code -ne 0 -or !$r.ok){throw 'Current native surface checks failed'}
 $receipt.ok=$true;$receipt.exitCode=$code;$receipt.checks=$r.cases.Count;$receipt.failures=$failures;$receipt.report=$report
 $receipt.executableSha256=(Get-FileHash -LiteralPath $exe).Hash
}catch{$receipt.error=$_.Exception.Message;throw}
finally{$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
