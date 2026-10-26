[CmdletBinding()]
param([string]$OutputRoot,[switch]$CompileProduction)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=Join-Path $root '..\..\Build\Validation\msi-foreground-isolation-20261011\native-entry'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
$snapshot=Join-Path $OutputRoot 'source-snapshot'
$nativeSnapshot=Join-Path $snapshot 'native'
New-Item -ItemType Directory -Path $nativeSnapshot,"$snapshot\tools","$snapshot\deps\json" -Force|Out-Null
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$sdkRoot='C:\Program Files (x86)\Windows Kits\10'
$sdkVersion=Get-ChildItem -LiteralPath "$sdkRoot\Include" -Directory|Where-Object{
    (Test-Path -LiteralPath "$($_.FullName)\um\Windows.h") -and
    (Test-Path -LiteralPath "$sdkRoot\Lib\$($_.Name)\um\x64\kernel32.lib")
}|Sort-Object { [version]$_.Name } -Descending|Select-Object -First 1
if(!$sdkVersion){throw 'Complete Windows SDK not found'}
$env:WindowsSdkDir="$sdkRoot\";$env:WindowsSDKVersion="$($sdkVersion.Name)\"
$env:INCLUDE=(@('ucrt','shared','um','winrt','cppwinrt')|ForEach-Object{"$sdkRoot\Include\$($sdkVersion.Name)\$_"}) -join ';'
$env:INCLUDE+=";$env:VCToolsInstallDir\include"
$env:LIB="$sdkRoot\Lib\$($sdkVersion.Name)\ucrt\x64;$sdkRoot\Lib\$($sdkVersion.Name)\um\x64;$env:VCToolsInstallDir\lib\x64"
$env:PATH="$env:SystemRoot\System32;$sdkRoot\bin\$($sdkVersion.Name)\x64;$env:PATH"
$winrt=Join-Path $env:WindowsSdkDir "Include\$($env:WindowsSDKVersion.TrimEnd('\'))\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$($env:WindowsSDKVersion.TrimEnd('\'))\um\x64\WindowsApp.lib"
Copy-Item -LiteralPath "$root\native\main.cpp" -Destination "$nativeSnapshot\main.cpp"
Copy-Item -LiteralPath "$root\native\dsu_server.cpp" -Destination "$nativeSnapshot\dsu_server.cpp"
Copy-Item -LiteralPath "$root\tools\msi_foreground_isolation_native_regression.cpp" -Destination "$snapshot\tools\msi_foreground_isolation_native_regression.cpp"
foreach($header in Get-ChildItem -LiteralPath "$root\native" -File -Recurse){
    $destination=Join-Path $nativeSnapshot ([IO.Path]::GetRelativePath("$root\native",$header.FullName))
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force|Out-Null
    Copy-Item -LiteralPath $header.FullName -Destination $destination
}
Copy-Item -LiteralPath "$root\tools\msi_native_foreground_regression.h" -Destination "$snapshot\tools\msi_native_foreground_regression.h"
Copy-Item -LiteralPath "$root\deps\json\json.hpp" -Destination "$snapshot\deps\json\json.hpp"
Copy-Item -LiteralPath "$root\capabilities" -Destination "$snapshot\capabilities" -Recurse
# Preserve the production TU dependency closure introduced by AMD settings.
Copy-Item -LiteralPath "$root\deps\adlx-sdk" -Destination "$snapshot\deps\adlx-sdk" -Recurse
Get-ChildItem -LiteralPath $snapshot -File -Recurse|ForEach-Object{Get-FileHash -LiteralPath $_.FullName}|Select-Object Path,Hash|ConvertTo-Json|Set-Content -LiteralPath "$OutputRoot\compiled-snapshot-hashes.json" -Encoding utf8
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE','/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1')
$include=@("/I$root\deps\webview2\build\native\include","/I$snapshot\deps\json","/I$winrt","/I$nativeSnapshot","/I$snapshot\tools")
& cl.exe @common /c "$snapshot\tools\msi_foreground_isolation_native_regression.cpp" @include "/Fo$OutputRoot\regression.obj"
if($LASTEXITCODE -ne 0){throw 'MSI foreground full production TU regression compile failed'}
& cl.exe @common /c "$nativeSnapshot\dsu_server.cpp" "/Fo$OutputRoot\dsu.obj"
if($LASTEXITCODE -ne 0){throw 'DSU compile failed'}
& cl.exe @common /c "$nativeSnapshot\steam_original_recovery_engine.cpp" @include "/Fo$OutputRoot\steam-recovery.obj"
if($LASTEXITCODE -ne 0){throw 'Steam recovery link dependency compile failed'}
$libs=@("$root\deps\webview2\build\native\x64\WebView2LoaderStatic.lib",$winapp,'user32.lib','gdi32.lib','shell32.lib','shlwapi.lib','ole32.lib','oleaut32.lib','dwmapi.lib','winhttp.lib','advapi32.lib','shcore.lib','version.lib','psapi.lib','PowrProf.lib','ws2_32.lib')
$exe=Join-Path $OutputRoot 'msi-foreground-native-regression.exe'
& cl.exe /nologo "$OutputRoot\regression.obj" "$OutputRoot\dsu.obj" "$OutputRoot\steam-recovery.obj" "/Fe$exe" /link /SUBSYSTEM:CONSOLE /MACHINE:x64 @libs
if($LASTEXITCODE -ne 0){throw 'MSI foreground regression link failed'}
& $exe "$OutputRoot\fixture-data" | Tee-Object -FilePath "$OutputRoot\native-regression-results.txt"
if($LASTEXITCODE -ne 0){throw 'MSI foreground production helper regression failed'}
$productionExe=$null
if($CompileProduction){
    & cl.exe @common /c "$nativeSnapshot\main.cpp" @include "/Fo$OutputRoot\main-production.obj"
    if($LASTEXITCODE -ne 0){throw 'Non-test production compile failed'}
    Push-Location "$root\native"
    try{& rc.exe /nologo "/fo$OutputRoot\app-production.res" app.rc;if($LASTEXITCODE -ne 0){throw 'Production resources failed'}}finally{Pop-Location}
    $productionExe=Join-Path $OutputRoot 'YeManCC-MSI-foreground-candidate.exe'
    & cl.exe /nologo "$OutputRoot\main-production.obj" "$OutputRoot\dsu.obj" "$OutputRoot\steam-recovery.obj" "$OutputRoot\app-production.res" "/Fe$productionExe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 @libs
    if($LASTEXITCODE -ne 0){throw 'Non-test production link failed'}
    & mt.exe -manifest "$root\native\app.manifest" ('-outputresource:'+$productionExe+';#1')
    if($LASTEXITCODE -ne 0){throw 'Production admin manifest failed'}
}
foreach($file in @('msi_foreground_isolation.h','msi_foreground_isolation_runtime.h','msi_native_foreground_isolation.h','msi_native_foreground_isolation_runtime.h','native_foreground_source_policy.h')){
    if((Get-FileHash -LiteralPath "$root\native\$file").Hash -ne (Get-FileHash -LiteralPath "$nativeSnapshot\$file").Hash){throw "Source changed during verification: $file"}
}
# A passing fixed snapshot is not automatically the latest mainline. Account
# for parallel edits instead of silently calling an older candidate current.
$changedSourceFiles=@()
foreach($copied in Get-ChildItem -LiteralPath $snapshot -File -Recurse){
    $sourcePath=Join-Path $root ([IO.Path]::GetRelativePath($snapshot,$copied.FullName))
    if(!(Test-Path -LiteralPath $sourcePath) -or (Get-FileHash -LiteralPath $sourcePath).Hash -ne (Get-FileHash -LiteralPath $copied.FullName).Hash){$changedSourceFiles+=$sourcePath}
}
[ordered]@{passed=$true;actualProductionTu=$true;testIoOnly=$true;realHardwareAccess=$false;productionCompiled=[bool]$CompileProduction;productionArtifact=$productionExe;productLaunched=$false;installedFilesModified=$false;sourceSnapshot=$snapshot;compiledSourceMatchesCurrent=($changedSourceFiles.Count -eq 0);changedSourceFiles=$changedSourceFiles} | ConvertTo-Json|Set-Content -LiteralPath "$OutputRoot\verification-summary.json" -Encoding utf8
Write-Output "MSI_FOREGROUND_NATIVE_REGRESSION_OK $OutputRoot"
