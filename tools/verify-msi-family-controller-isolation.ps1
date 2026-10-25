# Compile and execute full native production helper tests only. No installation,
# real controller/driver/Steam access, or product startup is permitted here.
[CmdletBinding()]
param([string]$OutputRoot, [Parameter(Mandatory)][string]$WakeFixture, [switch]$CompileProduction)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot=Join-Path $root '..\..\Build\Validation\msi-g3e-isolation-wake-20261011\native-entry' }
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$evidence=Join-Path $OutputRoot 'evidence'
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $lines) {if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$winrt=Join-Path $env:WindowsSdkDir "Include\$($env:WindowsSDKVersion.TrimEnd('\'))\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$($env:WindowsSDKVersion.TrimEnd('\'))\um\x64\WindowsApp.lib"
$inputs=@('native\main.cpp','native\msi_virtual_wake.h','native\msi_mode_recovery.h','tools\msi_family_controller_isolation_regression.cpp','tools\msi_virtual_wake_regression.h','native\dsu_server.cpp')
$before=@($inputs | ForEach-Object { Get-FileHash -LiteralPath (Join-Path $root $_) | Select-Object Path,Hash })
# Compile a captured TU/header snapshot, not a concurrently edited main.cpp.
$snapshot=Join-Path $OutputRoot 'source-snapshot'
New-Item -ItemType Directory -Path $snapshot -Force | Out-Null
Copy-Item -LiteralPath "$root\native\main.cpp" -Destination (Join-Path $snapshot 'main.cpp')
foreach($header in (Get-ChildItem -LiteralPath "$root\native" -Filter '*.h' -File)) { Copy-Item -LiteralPath $header.FullName -Destination (Join-Path $snapshot $header.Name) }
Copy-Item -LiteralPath "$root\tools\msi_virtual_wake_regression.h" -Destination (Join-Path $snapshot 'msi_virtual_wake_regression.h')
$runner=[IO.File]::ReadAllText("$root\tools\msi_family_controller_isolation_regression.cpp").Replace('#include "../native/main.cpp"','#include "main.cpp"')
[IO.File]::WriteAllText((Join-Path $snapshot 'regression.cpp'),$runner,[Text.UTF8Encoding]::new($false))
$fingerprint=& node.exe "$root\tools\msi_family_controller_scope_fingerprint.mjs" (Join-Path $snapshot 'main.cpp')
if($LASTEXITCODE -ne 0){throw 'Snapshot repair fingerprint failed'}
$fingerprint | Set-Content -LiteralPath (Join-Path $evidence 'source-scope-before.json') -Encoding UTF8
$snapshotFiles=Get-ChildItem -LiteralPath $snapshot -File | ForEach-Object {Get-FileHash -LiteralPath $_.FullName | Select-Object Path,Hash}
$snapshotFiles | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'compiled-snapshot-hashes.json') -Encoding UTF8
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE')
& cl.exe @common /c "$snapshot\regression.cpp" "/I$root\deps\webview2\build\native\include" "/I$root\deps\json" "/I$winrt" "/I$snapshot" "/I$root\native" "/I$root\tools" "/Fo$OutputRoot\regression.obj"
if($LASTEXITCODE -ne 0){throw 'Native MSI helper regression compile failed'}
& cl.exe @common /c "$root\native\dsu_server.cpp" "/Fo$OutputRoot\dsu.obj"
if($LASTEXITCODE -ne 0){throw 'DSU compile failed'}
$exe=Join-Path $OutputRoot 'msi-family-controller-regression.exe'
& cl.exe /nologo "$OutputRoot\regression.obj" "$OutputRoot\dsu.obj" "/Fe$exe" /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "$root\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" $winapp user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib
if($LASTEXITCODE -ne 0){throw 'Native MSI helper regression link failed'}
& $exe $evidence $WakeFixture
if($LASTEXITCODE -ne 0){throw 'Native MSI helper regression failed; inspect evidence'}
$productionExe=$null
if($CompileProduction) {
    & cl.exe @common /c "$snapshot\main.cpp" "/I$root\deps\webview2\build\native\include" "/I$root\deps\json" "/I$winrt" "/I$snapshot" "/I$root\native" "/Fo$OutputRoot\main-production.obj"
    if($LASTEXITCODE -ne 0){throw 'Captured non-fault production compile failed'}
    Push-Location "$root\native"
    try { & rc.exe /nologo "/fo$OutputRoot\app-production.res" app.rc; if($LASTEXITCODE -ne 0){throw 'Production resources failed'} } finally { Pop-Location }
    $productionExe=Join-Path $OutputRoot 'YeManCC-MSI-isolation-wake-candidate.exe'
    & cl.exe /nologo "$OutputRoot\main-production.obj" "$OutputRoot\dsu.obj" "$OutputRoot\app-production.res" "/Fe$productionExe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 "$root\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" $winapp user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib
    if($LASTEXITCODE -ne 0){throw 'Captured production link failed'}
    & mt.exe -manifest "$root\native\app.manifest" ("-outputresource:"+$productionExe+';#1')
    if($LASTEXITCODE -ne 0){throw 'Production admin manifest failed'}
}
$after=@($inputs | ForEach-Object { Get-FileHash -LiteralPath (Join-Path $root $_) | Select-Object Path,Hash })
$currentFingerprint=& node.exe "$root\tools\msi_family_controller_scope_fingerprint.mjs" "$root\native\main.cpp"
if($LASTEXITCODE -ne 0 -or ($currentFingerprint -join "`n") -ne ($fingerprint -join "`n")){throw 'Relevant MSI scope changed during regression; rerun'}
$currentFingerprint | Set-Content -LiteralPath (Join-Path $evidence 'source-scope-after.json') -Encoding UTF8
for($i=1;$i -lt $before.Count;$i++){if($before[$i].Hash -ne $after[$i].Hash){throw "Scoped header/runner changed during regression: $($before[$i].Path)"}}
$wholeMainUnchanged=$before[0].Hash -eq $after[0].Hash
[ordered]@{passed=$true;sourceHashes=$after;actualProductionTu=$true;compiledFromSourceSnapshot=$true;currentRepairScopeMatchesSnapshot=$true;wholeMainUnchanged=$wholeMainUnchanged;productionCompiled=[bool]$CompileProduction;productionArtifact=$productionExe;nativeBinary=$exe;wakeFixture=$WakeFixture;realHardwareAccess=$false;installedFilesModified=$false} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidence 'verification-summary.json') -Encoding UTF8
Write-Output "MSI_FAMILY_CONTROLLER_REGRESSION_OK $evidence"
