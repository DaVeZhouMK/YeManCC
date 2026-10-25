[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory.'}
$frozen=Join-Path $OutputDirectory 'Frozen'
[IO.Directory]::CreateDirectory($frozen)|Out-Null
$receipt=[ordered]@{ok=$false;scope='Selected actual cursor headers with OS API substitutes; bounded context and unchanged display assertions';startedAtUtc=[DateTime]::UtcNow.ToString('o');sources=@();actualInputCalls=0;realWindowsCreated=0;fieldFlickerVerified=$false;productDeployed=$false}
try {
 $files=@('native/screen_touchpad_cursor.h','native/screen_touchpad_cursor_visibility.h','native/screen_touchpad_display_policy.h','deps/json/json.hpp','tools/screen_touchpad_cursor_r1_simulation.cpp','tools/screen_touchpad_cursor_context_selftest.cpp','tools/screen_touchpad_cursor_context_selftest.ps1')
 foreach($rel in $files){$src=Join-Path $project $rel;$dst=Join-Path $frozen ([IO.Path]::GetFileName($rel));$hash=(Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash;Copy-Item -LiteralPath $src -Destination $dst;if((Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -ne $hash){throw 'Source changed while freezing'};$receipt.sources+=@{path=$src;frozen=$dst;sha256=$hash}}
 $exe=Join-Path $OutputDirectory 'cursor-context.exe';$cmd=Join-Path $OutputDirectory 'build.cmd'
 @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /DNDEBUG /DUNICODE /D_UNICODE /DYMCC_CURSOR_MOTION_GATE_FIXTURE_ENABLED=1 /I"$frozen" "$frozen\screen_touchpad_cursor_context_selftest.cpp" /Fo"$OutputDirectory\context.obj" /Fe"$exe" /link /NODEFAULTLIB:user32.lib /NODEFAULTLIB:gdi32.lib
if errorlevel 1 exit /b 1
dumpbin /imports "$exe" > "$OutputDirectory\imports.txt"
if errorlevel 1 exit /b 1
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
 & $cmd *> (Join-Path $OutputDirectory 'build.log')
 if($LASTEXITCODE -ne 0){throw 'Compilation failed; see build.log'}
 $imports=Get-Content -LiteralPath (Join-Path $OutputDirectory 'imports.txt') -Raw
 if($imports -match '(?im)^\s*(USER32|GDI32|DWMAPI|WIN32U|DINPUT8|XINPUT[^\s]*)\.dll\s*$'){throw 'Live graphics/input DLL imported'}
 $receipt.noLiveGraphicsOrInputDllImports=$true
 $report=Join-Path $OutputDirectory 'simulation.json'
 $proc=Start-Process -FilePath $exe -ArgumentList ('"'+$report+'"') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.txt')
 $receipt.exitCode=$proc.ExitCode;$r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
 $receipt.checks=$r.checks;$receipt.failures=@($r.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name})
 if($proc.ExitCode -ne 0 -or !$r.ok){throw 'Context evidence assertions failed'}
 foreach($file in $receipt.sources){if((Get-FileHash -LiteralPath $file.path -Algorithm SHA256).Hash -ne $file.sha256 -or (Get-FileHash -LiteralPath $file.frozen -Algorithm SHA256).Hash -ne $file.sha256){throw 'Input source changed during run'}}
 $receipt.sourcesUnchanged=$true;$receipt.executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;$receipt.ok=$true
}catch{$receipt.error=$_.Exception.Message;throw}finally{$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
$receipt|ConvertTo-Json -Depth 5
