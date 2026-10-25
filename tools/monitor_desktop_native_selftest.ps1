param([switch]$ClickOwnTarget)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$out='G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\desktop-output-20261011\native-test'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$exe=Join-Path $out 'desktop-native-selftest.exe'
& cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DUNICODE /D_UNICODE (Join-Path $root 'tools\monitor_desktop_native_selftest.cpp') ("/I"+(Join-Path $root 'deps\webview2\build\native\include')) ("/I"+(Join-Path $root 'deps\json')) ("/Fo"+(Join-Path $out 'harness.obj')) ("/Fe"+$exe) /link /SUBSYSTEM:CONSOLE (Join-Path $root 'deps\webview2\build\native\x64\WebView2LoaderStatic.lib') user32.lib gdi32.lib ole32.lib shlwapi.lib version.lib advapi32.lib
if($LASTEXITCODE -ne 0){throw 'native harness compile failed'}
$args=@('G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\desktop-output-20261011\Build\App\Web',$out,(Join-Path $out 'frame.json'))
if($ClickOwnTarget){$args+='--click-own-target'}
& $exe @args
if($LASTEXITCODE -ne 0){throw 'native HUD verification failed; inspect report'}
