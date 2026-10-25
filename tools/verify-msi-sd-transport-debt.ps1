[CmdletBinding()]
param([string]$OutputRoot='G:\YeManCC-Work\Build\Validation\MSI-SD-Lifecycle-R6-20261013\transport-debt-native')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$allowed='G:\YeManCC-Work\Build\Validation\MSI-SD-Lifecycle-R6-20261013\transport-debt-native'
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if(-not ($OutputRoot.Equals($allowed,[StringComparison]::OrdinalIgnoreCase) -or $OutputRoot.StartsWith($allowed+'\',[StringComparison]::OrdinalIgnoreCase))) {
    throw 'All sidecar build/results must stay inside the authorized R6 transport-debt-native root.'
}
$main=Join-Path $root 'native\main.cpp'
$fence=Join-Path $root 'native\msi_steamdeck_release_fence_1726.h'
$test=Join-Path $root 'tools\msi_sd_transport_debt_native_selftest.cpp'
if(-not ([IO.File]::ReadAllText($main).Contains('g_msiSdOwnedPnpSnapshotForTest') -and [IO.File]::ReadAllText($fence).Contains('currentPinned'))) {
    throw 'Frozen product patch not ready. Do not compile guessed substitutes or run an old full runner.'
}
New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
$evidence=Join-Path $OutputRoot ('evidence-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $evidence -Force|Out-Null
$encoding=New-Object Text.UTF8Encoding($false)
function Write-Utf8([string]$Path,[string]$Value){[IO.File]::WriteAllText($Path,$Value,$encoding)}
$before=@($main,$fence,$test,$PSCommandPath)|ForEach-Object {Get-FileHash -LiteralPath $_ -Algorithm SHA256}
$before|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $OutputRoot 'source-hashes-before.json') -Encoding UTF8
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 environment setup failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$winrt=Join-Path $env:WindowsSdkDir "Include\$($env:WindowsSDKVersion.TrimEnd('\'))\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$($env:WindowsSDKVersion.TrimEnd('\'))\um\x64\WindowsApp.lib"
$includes=@("/I$root\deps\webview2\build\native\include","/I$root\deps\json","/I$winrt")
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE')
& cl.exe @common /DYMCC_FAULT_INJECT /c $test @includes ("/Fo$OutputRoot\transport-debt.obj") 2>&1|Tee-Object -FilePath (Join-Path $OutputRoot 'native-test-build.log')
if($LASTEXITCODE -ne 0){throw 'Transport-debt offline sidecar compile failed'}
& cl.exe @common /c "$root\native\dsu_server.cpp" ("/Fo$OutputRoot\dsu.obj") 2>&1|Tee-Object -FilePath (Join-Path $OutputRoot 'dsu-build.log')
if($LASTEXITCODE -ne 0){throw 'Link dependency compile failed; no dependency executable was run'}
$exe=Join-Path $OutputRoot 'msi-sd-transport-debt-native-selftest.exe'
& cl.exe /nologo "$OutputRoot\transport-debt.obj" "$OutputRoot\dsu.obj" ("/Fe$exe") /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "$root\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" $winapp user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib 2>&1|Tee-Object -FilePath (Join-Path $OutputRoot 'native-test-link.log')
if($LASTEXITCODE -ne 0){throw 'Transport-debt offline sidecar link failed'}
$after=@($before|ForEach-Object {Get-FileHash -LiteralPath $_.Path -Algorithm SHA256})
$after|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $OutputRoot 'source-hashes-after-build.json') -Encoding UTF8
$stable=$true
foreach($prior in $before){if(($after|Where-Object {$_.Path -eq $prior.Path}).Hash -ne $prior.Hash){$stable=$false}}
if(-not $stable){throw 'Source changed during build. No mixed-candidate run/green result; retry on frozen source.'}
# Only this new fault sidecar is executed, never production/probe/full-suite.
$psi=New-Object Diagnostics.ProcessStartInfo
$psi.FileName=$exe;$psi.Arguments='"'+$evidence+'"';$psi.WorkingDirectory=$OutputRoot
$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
$process=[Diagnostics.Process]::Start($psi)
$stdoutTask=$process.StandardOutput.ReadToEndAsync();$stderrTask=$process.StandardError.ReadToEndAsync()
if(-not $process.WaitForExit(60000)){$process.Kill();throw 'Own offline sidecar timed out at 60 seconds; only that test process was terminated'}
$stdout=$stdoutTask.GetAwaiter().GetResult();$stderr=$stderrTask.GetAwaiter().GetResult();$exit=$process.ExitCode;$process.Dispose()
Write-Utf8 (Join-Path $OutputRoot 'stdout.txt') $stdout
Write-Utf8 (Join-Path $OutputRoot 'stderr.txt') $stderr
$unsigned=[BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$exit),0)
[pscustomobject]@{binary=$exe;sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;exitCode=$exit;exitHex=('0x{0:X8}' -f $unsigned);evidence=$evidence}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputRoot 'process-result.json') -Encoding UTF8
$resultPath=Join-Path $evidence 'msi-sd-transport-debt-native-selftest.json'
if(-not (Test-Path -LiteralPath $resultPath)){throw "Sidecar did not produce results; native exit=$exit"}
$result=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json
$summary=[pscustomobject]@{exitCode=$exit;passed=($exit -eq 0 -and $result.passed);caseCount=$result.caseCount;nativeNegativeCaseCount=$result.nativeNegativeCaseCount;checkCount=$result.checkCount;failed=$result.failed;scope=$result.scope;sourceStable=$stable;sourceHashes=$before;realHardwareAPICalls=$result.realHardwareAPICalls;tripwireAttempts=$result.tripwireAttempts;tripwireScope=$result.tripwireScope;fakePnpHookCalls=$result.fakePnpHookCalls;syntheticMetadataCalls=$result.syntheticMetadataCalls;realHostProcesses=$result.realHostProcesses;newTargetCreates=$result.newTargetCreates;healthySdWholeHandlerExecuted=$result.healthySdWholeHandlerExecuted;realSelectedRestoreExecuted=$result.realSelectedRestoreExecuted;globalUnhideAdded=$result.globalUnhideAdded;faultInjectBuild=$true;productionOrProbeExecuted=$false;historicalFullSuiteExecuted=$false;evidence=$evidence;resultPath=$resultPath}
$summary|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputRoot 'verification-summary.json') -Encoding UTF8
$summary|ConvertTo-Json -Depth 6
if($exit -ne 0 -or -not $result.passed){$stdout -split '\r?\n'|Where-Object {$_ -match '^FAIL '};throw "Transport-debt safety regression failed; native exit=$exit; inspect $OutputRoot"}
Write-Output "MSI_SD_TRANSPORT_DEBT_NATIVE_OK $OutputRoot"