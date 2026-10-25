[CmdletBinding()]
param([string]$OutputRoot='G:\YeManCC-Work\Build\Validation\MSI-Controller-R6-20261013\terminal-return-native', [string]$NativeSource, [string]$RunnerSource)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if([string]::IsNullOrEmpty($NativeSource)){$NativeSource=Join-Path $repo 'native\main.cpp'}
if([string]::IsNullOrEmpty($RunnerSource)){$RunnerSource=Join-Path $repo 'tools\msi_terminal_return_native_selftest.cpp'}
$output=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$snapshot=Join-Path $output 'source'; New-Item -ItemType Directory -Path $snapshot -Force | Out-Null
Copy-Item -LiteralPath $NativeSource -Destination (Join-Path $snapshot 'main.cpp')
foreach($h in Get-ChildItem -LiteralPath (Join-Path $repo 'native') -Filter '*.h' -File){Copy-Item -LiteralPath $h.FullName -Destination (Join-Path $snapshot $h.Name)}
$runner=[IO.File]::ReadAllText($RunnerSource).Replace('#include "../native/main.cpp"','#include "main.cpp"')
[IO.File]::WriteAllText((Join-Path $snapshot 'runner.cpp'),$runner,[Text.UTF8Encoding]::new($false))
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /c "`"$vc`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$sdk=$env:WindowsSDKVersion.TrimEnd('\')
$winrt=Join-Path $env:WindowsSdkDir "Include\$sdk\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$sdk\um\x64\WindowsApp.lib"
$opts=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE')
& cl.exe @opts /DYMCC_FAULT_INJECT /c (Join-Path $snapshot 'runner.cpp') "/I$repo\deps\webview2\build\native\include" "/I$repo\deps\json" "/I$winrt" "/I$repo\native" "/I$repo\tools" "/Fo$output\runner.obj"
if($LASTEXITCODE -ne 0){throw 'runner compile failed'}
& cl.exe @opts /c "$repo\native\dsu_server.cpp" "/Fo$output\dsu.obj"
if($LASTEXITCODE -ne 0){throw 'DSU compile failed'}
& cl.exe /nologo "$output\runner.obj" "$output\dsu.obj" "/Fe$output\selftest.exe" /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "$repo\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" $winapp user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib
if($LASTEXITCODE -ne 0){throw 'runner link failed'}
$ev=Join-Path $output 'evidence';New-Item -ItemType Directory -Path $ev -Force | Out-Null
$result=& "$output\selftest.exe" $ev
$code=$LASTEXITCODE
$result | Set-Content -LiteralPath "$output\result.txt" -Encoding UTF8
$result | Write-Output
$hashes=@('main.cpp','msi_terminal_return_plan.h')|ForEach-Object {Get-FileHash -LiteralPath (Join-Path $snapshot $_)}
$live=@((Get-FileHash -LiteralPath $NativeSource),(Get-FileHash -LiteralPath (Join-Path $repo 'native\msi_terminal_return_plan.h')))
$matched=$true;for($i=0;$i -lt $hashes.Count;$i++){if($hashes[$i].Hash -ne $live[$i].Hash){$matched=$false}}
[ordered]@{exitCode=$code;actualProductionHandler=$true;hardwareBoundary='YMCC_FAULT_INJECT existing mode-write seam';realHardwareCalls=0;selectedSourceMatchesSnapshot=$matched;selectedNativeSource=$NativeSource;runnerSource=$RunnerSource;source=$hashes}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$output\binding.json" -Encoding UTF8
if($code -ne 0 -or -not $matched){throw 'actual terminal return handler verification failed'}
