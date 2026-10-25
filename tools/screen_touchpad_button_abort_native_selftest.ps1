[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory.'}
$frozen=Join-Path $OutputDirectory 'FrozenNative'
New-Item -ItemType Directory -Path $frozen -Force | Out-Null
$receipt=[ordered]@{ok=$false;scope='Production button configuration/recovery with real WinRT source dispatcher interruptions on a private desktop';sources=@();systemInputInjected=$false;deviceWrites=0;productStarted=$false;startedAt=(Get-Date).ToString('o')}
try{
 # Freeze only the quoted-header closure used by this consumer, so parallel
 # monitor/Steam changes are not mistaken for changes to the tested inputs.
 $pending=[Collections.Generic.Queue[string]]::new();$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 $pending.Enqueue((Join-Path $project 'native/screen_touchpads.h'));$inputs=@()
 while($pending.Count){$file=$pending.Dequeue();if(!$seen.Add($file)){continue};$inputs+=Get-Item -LiteralPath $file
  foreach($match in [regex]::Matches([IO.File]::ReadAllText($file),'(?m)^\s*#include "([^"]+)"')){$child=Join-Path (Split-Path $file) $match.Groups[1].Value;if(Test-Path -LiteralPath $child -PathType Leaf){$pending.Enqueue([IO.Path]::GetFullPath($child))}}
 }
 $inputs+=Get-Item -LiteralPath (Join-Path $project 'deps/json/json.hpp'),(Join-Path $PSScriptRoot 'screen_touchpad_button_abort_native_selftest.cpp'),$PSCommandPath
 foreach($file in $inputs){$hash=(Get-FileHash -LiteralPath $file.FullName).Hash;$target=Join-Path $frozen $file.Name;Copy-Item -LiteralPath $file.FullName -Destination $target;if((Get-FileHash -LiteralPath $target).Hash -ne $hash){throw 'Source changed while freezing'};$receipt.sources+=@{path=$file.FullName;frozen=$target;sha256=$hash}}
 $exe=Join-Path $OutputDirectory 'native-button-abort.exe';$build=Join-Path $OutputDirectory 'build.cmd'
 @"
@echo off
set "PATH=C:\Windows\System32;C:\Windows;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /I"$frozen" /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt" "$frozen\screen_touchpad_button_abort_native_selftest.cpp" /Fo"$OutputDirectory\native-button-abort.obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib shell32.lib
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $build -Encoding ascii
 & $build *> (Join-Path $OutputDirectory 'compile.log')
 $receipt.compileExitCode=$LASTEXITCODE;if($LASTEXITCODE -ne 0){throw 'Native fixture compilation failed; see compile.log'}
 $report=Join-Path $OutputDirectory 'replay.json'
 $run=Start-Process -FilePath $exe -ArgumentList ('"'+$report+'"') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.txt')
 $receipt.runExitCode=$run.ExitCode;$r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
 $receipt.checks=$r.checks;$receipt.failures=@($r.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name})
 if($run.ExitCode -ne 0 -or !$r.ok -or !$r.runtimeClosed -or !$r.queueClosed -or !$r.inputDesktopUnchanged -or !$r.privateDesktopClosed){throw 'Native replay or cleanup assertions failed'}
 $receipt.sourcesUnchanged=$true
 foreach($file in $receipt.sources){if((Get-FileHash -LiteralPath $file.path).Hash -ne $file.sha256 -or (Get-FileHash -LiteralPath $file.frozen).Hash -ne $file.sha256){$receipt.sourcesUnchanged=$false}}
 $receipt.exeSha256=(Get-FileHash -LiteralPath $exe).Hash;$receipt.ok=$receipt.sourcesUnchanged
 $receipt.limitations=@('The callback interruption is deliberately arranged on the real dispatcher; it is not a trace of the unknown original callback.','No real handheld, Steam consumption or physical input delivery proof.')
}catch{$receipt.error=$_.Exception.Message;Write-Output $receipt.error}finally{$receipt.finishedAt=(Get-Date).ToString('o');$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
if(!$receipt.ok){exit 1}
Write-Output ('PASS checks='+$receipt.checks+' receipt='+(Join-Path $OutputDirectory 'receipt.json'))
