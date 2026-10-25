[CmdletBinding()]
param(
    [string]$OutputRoot,
    [string]$LegacyLogSource,
    [switch]$MsiReportChainDiagnostic
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$workspace=[IO.Path]::GetFullPath((Join-Path $root '..\..\..'))
$validation=Join-Path $workspace 'Build\Validation\MSI-External-Visibility-R5-20261013'
$triage=Join-Path $validation 'full-suite-triage'
$dedicatedSd=Join-Path $validation 'sd-prepare-receipt-selftest'
if([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot=Join-Path $triage ('sd-prepare-receipt\run-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
$allowed=$false
foreach($base in @($triage,$dedicatedSd)) {
    $absolute=[IO.Path]::GetFullPath($base).TrimEnd('\')
    if($OutputRoot.Equals($absolute,[StringComparison]::OrdinalIgnoreCase) -or
       $OutputRoot.StartsWith($absolute+'\',[StringComparison]::OrdinalIgnoreCase)) {$allowed=$true}
}
if(-not $allowed) {throw 'All generated artifacts must remain inside full-suite-triage or the dedicated sd-prepare-receipt-selftest directory.'}
if([string]::IsNullOrWhiteSpace($LegacyLogSource)) {
    $LegacyLogSource=Join-Path $workspace 'Build\Validation\MSI-Shield-R2-20261013\mode-ticket-regression\source\main.cpp'
}
$LegacyLogSource=[IO.Path]::GetFullPath($LegacyLogSource)
$currentMain=Join-Path $root 'native\main.cpp'
$testSource=Join-Path $root 'tools\msi_sd_prepare_receipt_native_selftest.cpp'
$fenceHeader=Join-Path $root 'native\msi_steamdeck_release_fence_1726.h'
foreach($path in @($currentMain,$testSource,$fenceHeader,$LegacyLogSource)) {
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)) {throw "Missing read-only input: $path"}
}
New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
$evidence=Join-Path $OutputRoot 'evidence'
New-Item -ItemType Directory -Path $evidence -Force|Out-Null
$encoding=New-Object Text.UTF8Encoding($false)
function Write-Utf8([string]$Path,[string]$Value) {[IO.File]::WriteAllText($Path,$Value,$encoding)}
function Extract-RequiredPrepareLog([string]$Path) {
    $text=[IO.File]::ReadAllText($Path)
    $pattern='(?s)\bif\s*\(\s*g_msiSteamDeckReleaseFence\.required\(\)\s*\)\s*appendNativeLifecycleLog\(\s*"msi-sd-release-fence"\s*,\s*\{.*?\}\s*\);'
    $found=@([regex]::Matches($text,$pattern)|Where-Object {$_.Value -match '"phase"\s*,\s*"prepare"'})
    if($found.Count -ne 1) {throw "Expected exactly one original required PREPARE log statement in $Path; found $($found.Count). No guessed/copy-maintained fallback is permitted."}
    return [pscustomobject]@{path=$Path;startLine=$text.Substring(0,$found[0].Index).Split("`n").Length;statement=$found[0].Value}
}
function Extract-ReleaseLog([string]$Path) {
    $source=[IO.File]::ReadAllText($Path)
    $pattern='(?s)\bappendNativeLifecycleLog\(\s*"msi-sd-release-fence"\s*,\s*\{.*?\}\s*\);'
    $found=@([regex]::Matches($source,$pattern)|Where-Object {$_.Value -match '"phase"\s*,\s*"release"'})
    if($found.Count -ne 1) {throw "Expected exactly one original RELEASE log statement in $Path; found $($found.Count). No guessed expression or whole-handler substitute is allowed."}
    return [pscustomobject]@{path=$Path;startLine=$source.Substring(0,$found[0].Index).Split("`n").Length;statement=$found[0].Value;scope='Exact RELEASE log arguments only; no stop handler/cleanup/debt/unhide execution'}
}
$before=@($currentMain,$testSource,$fenceHeader,$LegacyLogSource,$PSCommandPath)|ForEach-Object {Get-FileHash -LiteralPath $_ -Algorithm SHA256}
$before|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $OutputRoot 'source-hashes-before.json') -Encoding UTF8
$currentLog=Extract-RequiredPrepareLog $currentMain
$legacyLog=Extract-RequiredPrepareLog $LegacyLogSource
$currentReleaseLog=Extract-ReleaseLog $currentMain
$legacyReleaseLog=Extract-ReleaseLog $LegacyLogSource
Write-Utf8 (Join-Path $OutputRoot 'current-release-log.txt') $currentReleaseLog.statement
Write-Utf8 (Join-Path $OutputRoot 'legacy-release-log.txt') $legacyReleaseLog.statement
Write-Utf8 (Join-Path $OutputRoot 'current-required-prepare-log.txt') $currentLog.statement
Write-Utf8 (Join-Path $OutputRoot 'legacy-required-prepare-log.txt') $legacyLog.statement
$header="#pragma once`r`n// Generated from read-only source; log sink only is captured by the test TU.`r`n"
$header+="// Current source: $($currentLog.path):$($currentLog.startLine)`r`n"
$header+="static void sdPrepareRequiredLogCurrent(bool prepared, bool prepareAttempted, bool prepareSent, const json& prepareReceipt) {`r`n$($currentLog.statement)`r`n}`r`n"
$header+="// Legacy source: $($legacyLog.path):$($legacyLog.startLine)`r`n"
$header+="static void sdPrepareRequiredLogLegacy(bool prepared, bool prepareAttempted, bool prepareSent, const json& prepareReceipt) {`r`n$($legacyLog.statement)`r`n}`r`n"
$header+="// Current RELEASE source: $($currentReleaseLog.path):$($currentReleaseLog.startLine)`r`n"
$header+="static void sdReleaseLogCurrent(bool localAck, bool localReleaseAck, const json& release) {`r`n$($currentReleaseLog.statement)`r`n}`r`n"
$header+="// Legacy RELEASE source: $($legacyReleaseLog.path):$($legacyReleaseLog.startLine)`r`n"
$header+="static void sdReleaseLogLegacy(bool localAck, bool localReleaseAck, const json& release) {`r`n$($legacyReleaseLog.statement)`r`n}`r`n"
Write-Utf8 (Join-Path $OutputRoot 'msi_sd_prepare_receipt_log.generated.h') $header
[pscustomobject]@{scope='One actual old-HELLO native negative; other cases exact required log expressions + actual Session/modeled request only';current=$currentLog;legacy=$legacyLog}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputRoot 'extraction.json') -Encoding UTF8

$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0) {throw 'vcvars64 environment setup failed'}
foreach($line in $lines) {if($line -match '^([^=]+)=(.*)$') {[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$winrt=Join-Path $env:WindowsSdkDir "Include\$($env:WindowsSDKVersion.TrimEnd('\'))\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$($env:WindowsSDKVersion.TrimEnd('\'))\um\x64\WindowsApp.lib"
$includes=@("/I$root\deps\webview2\build\native\include","/I$root\deps\json","/I$winrt","/I$OutputRoot")
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE')
$fault=@('/DYMCC_FAULT_INJECT')
if($MsiReportChainDiagnostic) {$fault+=('/DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC')}
& cl.exe @common @fault /c $testSource @includes ("/Fo$OutputRoot\sd-receipt.obj") 2>&1|Tee-Object -FilePath (Join-Path $OutputRoot 'native-test-build.log')
if($LASTEXITCODE -ne 0) {throw 'Offline SD receipt native selftest compile failed'}
& cl.exe @common /c "$root\native\dsu_server.cpp" ("/Fo$OutputRoot\dsu.obj") 2>&1|Tee-Object -FilePath (Join-Path $OutputRoot 'dsu-build.log')
if($LASTEXITCODE -ne 0) {throw 'Link dependency compile failed; no dependency executable was run'}
$exe=Join-Path $OutputRoot 'msi-sd-prepare-receipt-native-selftest.exe'
& cl.exe /nologo "$OutputRoot\sd-receipt.obj" "$OutputRoot\dsu.obj" ("/Fe$exe") /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "$root\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" $winapp user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib 2>&1|Tee-Object -FilePath (Join-Path $OutputRoot 'native-test-link.log')
if($LASTEXITCODE -ne 0) {throw 'Offline SD receipt native selftest link failed'}
$after=@($before|ForEach-Object {Get-FileHash -LiteralPath $_.Path -Algorithm SHA256})
$after|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $OutputRoot 'source-hashes-after-build.json') -Encoding UTF8
$stable=$true
foreach($prior in $before) {if(($after|Where-Object {$_.Path -eq $prior.Path}).Hash -ne $prior.Hash) {$stable=$false}}
if(-not $stable) {throw 'Source changed during compilation; no mixed candidate run or green assertion allowed. Retry after the main-agent patch is stable.'}

# Only this new, restricted offline selftest runs. Never execute the historical
# full runner, product exe, probe, or any external metadata/hardware commands.
$psi=New-Object Diagnostics.ProcessStartInfo
$psi.FileName=$exe;$psi.Arguments='"'+$evidence+'"';$psi.WorkingDirectory=$OutputRoot
$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
$process=[Diagnostics.Process]::Start($psi)
$stdoutTask=$process.StandardOutput.ReadToEndAsync();$stderrTask=$process.StandardError.ReadToEndAsync()
if(-not $process.WaitForExit(60000)) {$process.Kill();throw 'Own offline selftest exceeded 60 seconds; no other process was terminated'}
$stdout=$stdoutTask.GetAwaiter().GetResult();$stderr=$stderrTask.GetAwaiter().GetResult()
$nativeExit=$process.ExitCode
Write-Utf8 (Join-Path $OutputRoot 'stdout.txt') $stdout
Write-Utf8 (Join-Path $OutputRoot 'stderr.txt') $stderr
$process.Dispose()
$unsigned=[BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$nativeExit),0)
[pscustomobject]@{binary=$exe;sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;nativeExitCode=$nativeExit;nativeExitHex=('0x{0:X8}' -f $unsigned)}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputRoot 'process-result.json') -Encoding UTF8
$resultPath=Join-Path $evidence 'msi-sd-prepare-receipt-native-selftest.json'
if(-not (Test-Path -LiteralPath $resultPath)) {throw "Own offline runner did not produce results; native exit=$nativeExit"}
$result=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json
$summary=[pscustomobject]@{nativeExitCode=$nativeExit;passed=($nativeExit -eq 0 -and $result.passed);caseCount=$result.caseCount;prepareCaseCount=$result.prepareCaseCount;releaseCaseCount=$result.releaseCaseCount;preservedPrepareCheckCount=$result.preservedPrepareCheckCount;releaseCheckCount=$result.releaseCheckCount;combinedPrepareAndRelease=$result.combinedPrepareAndRelease;checkCount=$result.checkCount;failed=$result.failed;scope=$result.scope;wholeHandlerExecuted=$result.wholeHandlerExecuted;wholeHandlerCoverage=$result.wholeHandlerCoverage;otherReceiptShapesWholeHandlerExecuted=$result.otherReceiptShapesWholeHandlerExecuted;wholeReleaseHandlerExecuted=$result.wholeReleaseHandlerExecuted;actualUnhideExecutedForReleaseCases=$result.actualUnhideExecutedForReleaseCases;nativeOldHelloNegative=$result.nativeOldHelloNegative;actualLogSinkExecuted=$result.actualLogSinkExecuted;hardwareAPICalls=$result.hardwareAPICalls;hardwareAPICountBasis=$result.hardwareAPICountBasis;metadataSeamCalls=$result.metadataSeamCalls;realPrepareRequests=$result.realPrepareRequests;realReleaseRequests=$result.realReleaseRequests;simulatedReleaseRequests=$result.simulatedReleaseRequests;simulatedPrepareRequests=$result.simulatedPrepareRequests;identityActualSentinel=$result.identityActualSentinel;sourceStable=$stable;sourceHashes=$before;currentLogSource=$currentLog.path;currentLogStartLine=$currentLog.startLine;legacyLogSource=$legacyLog.path;legacyLogStartLine=$legacyLog.startLine;currentReleaseLogStartLine=$currentReleaseLog.startLine;legacyReleaseLogStartLine=$legacyReleaseLog.startLine;prepareLegacyExpectedThrowCount=@($result.cases|Where-Object {$_.legacyThrow}).Count;releaseLegacyExpectedThrowCount=@($result.releaseCases|Where-Object {$_.legacyThrow}).Count;prepareCurrentThrowCount=@($result.cases|Where-Object {$_.currentThrow}).Count;releaseCurrentThrowCount=@($result.releaseCases|Where-Object {$_.currentThrow}).Count;faultInjectBuild=$true;msiReportChainDiagnosticBuild=[bool]$MsiReportChainDiagnostic;productOrProbeExecuted=$false;historicalFullRunnerExecuted=$false}
$summary|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputRoot 'verification-summary.json') -Encoding UTF8
$summary|ConvertTo-Json -Depth 6
if($nativeExit -ne 0 -or -not $result.passed) {
    $stdout -split '\r?\n'|Where-Object {$_ -match '^FAIL '}
    throw "SD receipt safety regression failed; native exit=$nativeExit; inspect $OutputRoot"
}
Write-Output "MSI_SD_PREPARE_RECEIPT_NATIVE_OK $OutputRoot"