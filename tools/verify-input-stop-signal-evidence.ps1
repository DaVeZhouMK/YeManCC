#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$OutputRoot = 'G:\YeManCC-Work\Build\Validation\VirtualGamepad-CtrlReturn-20261014\verification',
    [switch]$BuildNative
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $out){throw 'Choose a fresh evidence directory; this verifier never deletes prior evidence.'}
$src=Join-Path $out 'source'
[IO.Directory]::CreateDirectory($src) | Out-Null
$files=@(Get-Item -LiteralPath "$repo\native\main.cpp","$repo\native\keyboard_modifier_snapshot.h","$repo\native\input_stop_signal_evidence.h","$repo\deps\json\json.hpp","$repo\tools\input_stop_signal_evidence_selftest.cpp")
if($BuildNative){
    $files+=@(Get-ChildItem -LiteralPath "$repo\native" -Recurse -File | Where-Object {$_.Extension -in @('.h','.cpp','.rc','.manifest','.ico','.inc')})
    $files+=@(Get-ChildItem -LiteralPath "$repo\deps\webview2\build\native\include" -File)
    $files+=@(Get-Item -LiteralPath "$repo\deps\webview2\build\native\x64\WebView2LoaderStatic.lib")
}
$binding=@()
foreach($file in @($files | Sort-Object FullName -Unique)){
    $relative=[IO.Path]::GetRelativePath($repo,$file.FullName);$copy=Join-Path $src $relative
    $sha=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    [IO.Directory]::CreateDirectory((Split-Path -Parent $copy)) | Out-Null
    [IO.File]::Copy($file.FullName,$copy,$false)
    if((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $sha){throw 'Copy changed during snapshot'}
    $binding+=@([ordered]@{relativePath=$relative;source=$file.FullName;snapshot=$copy;sha256=$sha})
}
$main=[IO.File]::ReadAllText("$src\native\main.cpp")
$begin=$main.IndexOf('// Ctrl-return evidence shares the Host mutex')
$end=$main.IndexOf('static void keyboardModifierFlushEvidence(', $begin)
if($begin -lt 0 -or $end -le $begin){throw 'Production adapter anchors missing'}
$adapter=$main.Substring($begin,$end-$begin)
[IO.File]::WriteAllText("$src\tools\actual_input_stop_signal_adapter.inc",$adapter,[Text.UTF8Encoding]::new($false))
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$vars=& $env:ComSpec /d /c "`"$vc`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $vars){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$opts=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE')
Write-Output 'Compiling isolated fake-input fixture'
& cl.exe @opts "$src\tools\input_stop_signal_evidence_selftest.cpp" "/I$src\deps\json" "/Fo$out\fixture.obj" "/Fe$out\fixture.exe" *> "$out\fixture-build.log"
if($LASTEXITCODE -ne 0){Get-Content "$out\fixture-build.log" -Tail 25;throw 'Fixture compile failed'}
& dumpbin.exe /imports "$out\fixture.exe" *> "$out\fixture-imports.txt"
if($LASTEXITCODE -ne 0){throw 'Fixture import audit failed'}
$imports=[IO.File]::ReadAllText("$out\fixture-imports.txt")
if($imports -match '(?im)USER32\.dll|\bSendInput\b|\bGetAsyncKeyState\b|\bGetKeyboardState\b|\bDeviceIoControl\b'){throw 'Fixture has live input imports'}
& "$out\fixture.exe" *> "$out\fixture-results.txt"
if($LASTEXITCODE -ne 0){Get-Content "$out\fixture-results.txt";throw 'Fixture failed'}
Get-Content "$out\fixture-results.txt"
$exe=$null
if($BuildNative){
    Write-Output 'Compiling frozen production Native (build only; no execution)'
    $sdk=$env:WindowsSDKVersion.TrimEnd('\')
    $winrt=Join-Path $env:WindowsSdkDir "Include\$sdk\cppwinrt"
    $winapp=Join-Path $env:WindowsSdkDir "Lib\$sdk\um\x64\WindowsApp.lib"
    $defines=@('/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1','/DYMCC_MSI_HOST_STAGE_DIAGNOSTIC','/DYMCC_MSI_VISIBILITY_DIAGNOSTIC','/DYMCC_MSI_USB_NAME_ORIGIN_DIAGNOSTIC','/DYMCC_MSI_VISIBILITY_RAW_DIAGNOSTIC','/DYMCC_MSI_VISIBILITY_WAKE_DIAGNOSTIC')
    & cl.exe @opts @defines /c "$src\native\main.cpp" "/I$src\deps\json" "/I$src\deps\webview2\build\native\include" "/I$winrt" "/Fo$out\main.obj" *> "$out\native-build.log"
    if($LASTEXITCODE -ne 0){Get-Content "$out\native-build.log" -Tail 30;throw 'Native compile failed'}
    & cl.exe @opts /c "$src\native\dsu_server.cpp" "/Fo$out\dsu.obj" *> "$out\dsu-build.log"
    if($LASTEXITCODE -ne 0){throw 'DSU compile failed'}
    Push-Location "$src\native"
    try {& rc.exe /nologo "/fo$out\app.res" app.rc *> "$out\resources.log";if($LASTEXITCODE -ne 0){throw 'Resource compile failed'}}finally{Pop-Location}
    $exe=Join-Path $out 'YeManCC-CtrlReturn-diagnostic.exe'
    & cl.exe /nologo "$out\main.obj" "$out\dsu.obj" "$out\app.res" "/Fe$exe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 "$src\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" $winapp user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib *> "$out\native-link.log"
    if($LASTEXITCODE -ne 0){Get-Content "$out\native-link.log" -Tail 20;throw 'Native link failed'}
    & mt.exe -manifest "$src\native\app.manifest" ("-outputresource:"+$exe+';#1') *> "$out\manifest.log"
    if($LASTEXITCODE -ne 0){throw 'Manifest embedding failed'}
}
$drift=@($binding | Where-Object {(Get-FileHash -LiteralPath $_.source -Algorithm SHA256).Hash -ne $_.sha256})
foreach($b in $binding){if((Get-FileHash -LiteralPath $b.snapshot -Algorithm SHA256).Hash -ne $b.sha256){throw 'Frozen source changed'}}
[ordered]@{passed=$true;time=[DateTimeOffset]::Now.ToString('o');actualAdaptersExtracted=$true;fixtureLiveInputImports=$false;productionLaunched=$false;deviceOperations=$false;deployed=$false;nativeBuilt=[bool]$BuildNative;native=if($exe){Get-FileHash -LiteralPath $exe -Algorithm SHA256 | Select-Object Path,Hash}else{$null};source=$binding;concurrentSourceDrift=$drift;scope='Only diagnostics validated; no Ctrl root cause or real-device acceptance claimed'} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$out\result.json" -Encoding utf8
Write-Output "STOP_SIGNAL_EVIDENCE_VERIFIED $out"
