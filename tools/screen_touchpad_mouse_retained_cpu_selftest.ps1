[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$CursorSourceDirectory,
    [string[]]$ExpectedFailures=@()
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(!$CursorSourceDirectory){$CursorSourceDirectory=Join-Path $project 'native'}
$CursorSourceDirectory=[IO.Path]::GetFullPath($CursorSourceDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh output directory.'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$names=@('screen_touchpad_cursor.h','screen_touchpad_cursor_visibility.h','screen_touchpad_display_policy.h')
$files=@($names | ForEach-Object {if(Test-Path -LiteralPath (Join-Path $CursorSourceDirectory $_)){Join-Path $CursorSourceDirectory $_}else{Join-Path $project "native\$_"}})
$files+=@(Join-Path $PSScriptRoot 'screen_touchpad_cursor_r1_simulation.cpp')
$files+=@(Join-Path $PSScriptRoot 'screen_touchpad_mouse_retained_cpu_selftest.cpp')
$files+=@($PSCommandPath)
function Hashes { @($files | ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash}}) }
$receipt=[ordered]@{ok=$false;scope='Actual Mouse cursor functions with deterministic API substitutes; no application/device/CPU sampling';sources=(Hashes);expectedFailures=$ExpectedFailures}
try {
    $source=Join-Path $PSScriptRoot 'screen_touchpad_mouse_retained_cpu_selftest.cpp'
    $exe=Join-Path $OutputDirectory 'mouse-retained.exe'
    $build=Join-Path $OutputDirectory 'build.cmd'
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\%WindowsSDKVersion%winrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /sourceDependencies "$OutputDirectory\compile-inputs.json" /I"$CursorSourceDirectory" /I"$project\native" /I"$project\deps\json" "$source" /Fo"$OutputDirectory\mouse-retained.obj" /Fe"$exe" /link /NODEFAULTLIB:user32.lib /NODEFAULTLIB:gdi32.lib
if errorlevel 1 exit /b 1
dumpbin /imports "$exe" > "$OutputDirectory\imports.txt"
"@ | Set-Content -LiteralPath $build -Encoding ascii
    & $build 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'compile.log')
    if($LASTEXITCODE -ne 0){throw 'Cursor consumer compilation failed'}
    $imports=Get-Content -LiteralPath (Join-Path $OutputDirectory 'imports.txt') -Raw
    if($imports -match '(?im)^\s*(USER32|GDI32|DWMAPI|DINPUT8|XINPUT[^\s]*)\.dll\s*$'){throw 'Unexpected live graphics/input DLL import'}
    $report=Join-Path $OutputDirectory 'result.json'
    $child=Start-Process -FilePath $exe -ArgumentList @(('"'+$report+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.txt')
    if(!$child.WaitForExit(15000)){$child.Kill();$child.WaitForExit();throw 'Owned cursor consumer timed out'}
    $exitCode=$child.ExitCode
    $result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    $receipt.exitCode=$exitCode;$receipt.checks=@($result.cases).Count
    $receipt.actualFailures=@($result.cases | Where-Object {!$_.ok} | ForEach-Object {$_.name})
    $actual=@($receipt.actualFailures | Sort-Object);$expected=@($ExpectedFailures | Sort-Object)
    if(($actual -join "`n") -ne ($expected -join "`n")){throw 'Observed failures do not match the expected baseline'}
    $expectedExit=if($expected.Count){1}else{0}
    if($exitCode -ne $expectedExit -or $result.realWindowsCreated -ne 0 -or $result.realInputInjected -ne 0 -or $result.hardwareCalls -ne 0){throw 'Invalid cursor consumer result'}
    $after=Hashes
    for($i=0;$i -lt $after.Count;++$i){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed: $($after[$i].path)"}}
    $receipt.ok=$true;$receipt.baselineDefectsReproduced=[bool]$expected.Count;$receipt.metrics=$result.metrics;$receipt.noLiveGraphicsInputDllImports=$true
    Write-Output "MOUSE_RETAINED_VERIFIED checks=$($receipt.checks) baseline=$($receipt.baselineDefectsReproduced)"
} catch {$receipt.error=$_.Exception.Message;throw}
finally {$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
