[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$allowed='G:\YeManCC-Work\Build\Validation\MSI-Host-Stage-R8-20261013'
$out=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if(-not $out.StartsWith($allowed+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output outside R8 evidence root'}
if(Test-Path -LiteralPath $out){throw 'Use a fresh candidate output directory'}
$sourceRoot=Join-Path $out 'source'
New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'native') -Force | Out-Null
$paths=@(Get-ChildItem -LiteralPath (Join-Path $repo 'native') -Recurse -File | Where-Object {$_.Extension -in @('.h','.cpp','.rc','.manifest','.ico')})
$before=@()
foreach($p in $paths){
    $hash=(Get-FileHash -LiteralPath $p.FullName).Hash
    $relative=$p.FullName.Substring($repo.Length+1)
    $copy=Join-Path $sourceRoot $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force | Out-Null
    Copy-Item -LiteralPath $p.FullName -Destination $copy
    if((Get-FileHash -LiteralPath $copy).Hash -cne $hash){throw ('Snapshot source drift: '+$p.FullName)}
    $before+=@([pscustomobject]@{Path=$p.FullName;Hash=$hash;Snapshot=$copy})
}
New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'deps\json') -Force | Out-Null
$jsonSource=Join-Path $repo 'deps\json\json.hpp'
$jsonCopy=Join-Path $sourceRoot 'deps\json\json.hpp'
$jsonHash=(Get-FileHash -LiteralPath $jsonSource).Hash
Copy-Item -LiteralPath $jsonSource -Destination $jsonCopy
if((Get-FileHash -LiteralPath $jsonCopy).Hash -cne $jsonHash){throw 'JSON dependency snapshot drift'}
$before+=@([pscustomobject]@{Path=$jsonSource;Hash=$jsonHash;Snapshot=$jsonCopy})
# Build only the controller Native lane. Do not run the application or probe.
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /c "`"$vc`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$sdk=$env:WindowsSDKVersion.TrimEnd('\')
$winrt=Join-Path $env:WindowsSdkDir "Include\$sdk\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$sdk\um\x64\WindowsApp.lib"
$loader=Join-Path $repo 'deps\webview2\build\native\x64\WebView2LoaderStatic.lib'
$dependencyInputs=@($loader,(Join-Path $repo 'deps\json\json.hpp'),(Join-Path $repo 'deps\webview2\build\native\include\WebView2.h'))
$dependencyBefore=@($dependencyInputs | ForEach-Object {Get-FileHash -LiteralPath $_ | Select-Object Path,Hash})
$opts=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE','/DYMCC_MSI_HOST_STAGE_DIAGNOSTIC')
$inc=@("/I$repo\deps\webview2\build\native\include","/I$repo\deps\json","/I$winrt")
& cl.exe @opts /c "$sourceRoot\native\main.cpp" @inc "/Fo$out\main.obj" 2>&1 | Tee-Object -FilePath (Join-Path $out 'main-build.log')
if($LASTEXITCODE -ne 0){throw 'Native compile failed'}
& cl.exe @opts /c "$sourceRoot\native\dsu_server.cpp" "/Fo$out\dsu.obj" 2>&1 | Tee-Object -FilePath (Join-Path $out 'dsu-build.log')
if($LASTEXITCODE -ne 0){throw 'DSU compile failed'}
Push-Location (Join-Path $sourceRoot 'native')
try {& rc.exe /nologo "/fo$out\app.res" app.rc;if($LASTEXITCODE -ne 0){throw 'resource build failed'}}finally{Pop-Location}
$exe=Join-Path $out 'YeManCC-MSI-host-stage-candidate.exe'
& cl.exe /nologo "$out\main.obj" "$out\dsu.obj" "$out\app.res" "/Fe$exe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 $loader $winapp user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib 2>&1 | Tee-Object -FilePath (Join-Path $out 'link.log')
if($LASTEXITCODE -ne 0){throw 'Native link failed'}
& mt.exe -manifest "$sourceRoot\native\app.manifest" ("-outputresource:"+$exe+';#1')
if($LASTEXITCODE -ne 0){throw 'manifest embed failed'}
$symbols=& dumpbin.exe /nologo /symbols "$out\main.obj"
if($LASTEXITCODE -ne 0){throw 'object inspection failed'}
if($symbols -match 'g_msiVisibilityObserverActiveForTest|g_msiVisibilityObserverLaunchForTest|g_msiVisibilityObserverMetadataForTest|g_sensorThreadCommandForTest|g_msiControllerModeProbeIoForTest|g_msiSdOwnedPnpSnapshotForTest'){throw 'fault-only hook in engineering Native'}
$snapshotDrift=@($before | Where-Object {(Get-FileHash -LiteralPath $_.Snapshot).Hash -cne $_.Hash})
$currentDrift=@($before | Where-Object {(Get-FileHash -LiteralPath $_.Path).Hash -cne $_.Hash})
$dependencyDrift=@($dependencyBefore | Where-Object {(Get-FileHash -LiteralPath $_.Path).Hash -cne $_.Hash})
[ordered]@{compiled=$true;linked=$true;sourceSnapshotStable=($snapshotDrift.Count -eq 0);source=$before;currentSourceDrift=$currentDrift;dependencyInputs=$dependencyBefore;dependencyDrift=$dependencyDrift;faultHooksAbsent=$true;engineeringFlags=@('YMCC_MSI_HOST_STAGE_DIAGNOSTIC');native=(Get-FileHash -LiteralPath $exe | Select-Object Path,Hash);productionLaunched=$false;probeLaunched=$false;deployed=$false;testPackageCreated=$false;scope='Native compile/link from a private source snapshot only. No product execution, device validation or distribution package.'} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $out 'candidate-binding.json') -Encoding utf8
if($snapshotDrift.Count -or $dependencyDrift.Count){throw 'Frozen build inputs changed'}
Write-Output "MSI_STAGE_NATIVE_SNAPSHOT_BUILD_OK $exe currentSourceDrift=$($currentDrift.Count)"


