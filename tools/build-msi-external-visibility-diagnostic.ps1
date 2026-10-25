[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputRoot, [switch]$MsiExplicitRearmFix)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$out=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $out -Force | Out-Null
# An engineering candidate only. Compile/link and run synthetic helper tests;
# never launch the product, observe this workstation, install or deploy.
$inputs=@((Join-Path $repo 'native\main.cpp'),(Join-Path $repo 'native\dsu_server.cpp'),(Join-Path $repo 'native\app.rc'),(Join-Path $repo 'native\app.manifest'),(Join-Path $repo 'tools\controller_visibility_probe.cpp'),(Join-Path $repo 'tools\verify-controller-visibility-probe.ps1'))
$inputs+=@(Get-ChildItem -LiteralPath (Join-Path $repo 'native') -Filter '*.h' -File | ForEach-Object FullName)
$before=@($inputs|ForEach-Object {Get-FileHash -LiteralPath $_ | Select-Object Path,Hash})
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /c "`"$vc`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$sdk=$env:WindowsSDKVersion.TrimEnd('\')
$winrt=Join-Path $env:WindowsSdkDir "Include\$sdk\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$sdk\um\x64\WindowsApp.lib"
$opts=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE','/DYMCC_MSI_HOST_STAGE_DIAGNOSTIC','/DYMCC_MSI_VISIBILITY_DIAGNOSTIC','/DYMCC_MSI_USB_NAME_ORIGIN_DIAGNOSTIC','/DYMCC_MSI_VISIBILITY_RAW_DIAGNOSTIC','/DYMCC_MSI_VISIBILITY_WAKE_DIAGNOSTIC')
if($MsiExplicitRearmFix){$opts+=('/DYMCC_MSI_EXPLICIT_REARM_FIX')}
$includes=@("/I$repo\deps\webview2\build\native\include","/I$repo\deps\json","/I$winrt")
& cl.exe @opts /c "$repo\native\main.cpp" @includes "/Fo$out\main.obj"
if($LASTEXITCODE -ne 0){throw 'Non-fault engineering main compile failed'}
& cl.exe @opts /c "$repo\native\dsu_server.cpp" "/Fo$out\dsu.obj"
if($LASTEXITCODE -ne 0){throw 'DSU compile failed'}
Push-Location (Join-Path $repo 'native')
try{& rc.exe /nologo "/fo$out\app.res" app.rc;if($LASTEXITCODE -ne 0){throw 'Resource compile failed'}}finally{Pop-Location}
$native=Join-Path $out 'YeManCC-MSI-external-visibility-candidate.exe'
& cl.exe /nologo "$out\main.obj" "$out\dsu.obj" "$out\app.res" "/Fe$native" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 "$repo\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" $winapp user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib
if($LASTEXITCODE -ne 0){throw 'Non-fault engineering native link failed'}
& mt.exe -manifest "$repo\native\app.manifest" ("-outputresource:"+$native+';#1')
if($LASTEXITCODE -ne 0){throw 'Manifest embedding failed'}
$symbols=& dumpbin.exe /nologo /symbols "$out\main.obj"
if($LASTEXITCODE -ne 0){throw 'Production object symbol inspection failed'}
if($symbols -match 'g_msiVisibilityObserverActiveForTest|g_msiVisibilityObserverLaunchForTest|g_msiVisibilityObserverMetadataForTest|g_sensorThreadCommandForTest|g_msiControllerModeProbeIoForTest|g_msiSdOwnedPnpSnapshotForTest'){throw 'Fault-only transport found in non-fault candidate'}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$repo\tools\verify-controller-visibility-probe.ps1" -OutputRoot (Join-Path $out 'observer')
if($LASTEXITCODE -ne 0){throw 'Independent observer compile/synthetic test failed'}
$after=@($inputs|ForEach-Object {Get-FileHash -LiteralPath $_|Select-Object Path,Hash})
for($i=0;$i -lt $before.Count;$i++){if($before[$i].Hash -ne $after[$i].Hash){throw ('Source changed during candidate build: '+$before[$i].Path)}}
$probe=Join-Path $out 'observer\YeManControllerVisibilityProbe.exe'
[ordered]@{compiled=$true;linked=$true;sourceStable=$true;faultHooksAbsent=$true;engineeringFlags=@($opts | Where-Object {$_ -like '/DYMCC_*'} | ForEach-Object {$_.Substring(2)});msiExplicitRearmFixBuild=$true;msiExplicitRearmFixRequested=[bool]$MsiExplicitRearmFix;expectedProbeFormat=6;probeCommandSuffix='--raw-msi-receipts --msi-name-context <parent-PID-and-birth-bound-mapping>';msiNonSdReleaseEvidenceVersion=1;msiNonSdReleaseEvidenceScope='MSI opt-in existing release polls; no exact-owned absence proof or control change';msiUsbNameOriginVersion=1;msiUsbNameOriginScope='current known-MSI DInput source; hub-driver readback only';source=$after;native=(Get-FileHash -LiteralPath $native|Select-Object Path,Hash);probe=(Get-FileHash -LiteralPath $probe|Select-Object Path,Hash);productionLaunched=$false;liveObserverLaunched=$false;hardwareVerified=$false;deployed=$false;testPackageCreated=$false;scope='Matched non-fault native/probe engineering candidate; compile plus synthetic probe tests, NOT full-suite or G3E/Steam acceptance'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out 'candidate-binding.json') -Encoding utf8
Write-Output "MSI_EXTERNAL_VISIBILITY_DIAGNOSTIC_BUILD_OK $native"
