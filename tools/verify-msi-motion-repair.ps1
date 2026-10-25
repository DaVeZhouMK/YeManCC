[CmdletBinding()]
param([string]$OutputRoot, [switch]$CompileProduction, [string]$ReplayLogRoot, [string]$ControllerReplayFixture, [string]$VirtualWakeReplayFixture, [switch]$CauseDiagnostic, [switch]$PersonaModeHandoff, [switch]$VisibilityDiagnostic, [switch]$RawVisibilityDiagnostic, [switch]$MsiLivenessUnknownFix, [switch]$MsiExplicitRearmFix, [switch]$MsiLateHidePendingFix, [switch]$MsiBlockedStartPreparationFix, [switch]$MsiQuarantineDiagnostic, [switch]$VisibilityWakeDiagnostic, [switch]$MsiReportChainDiagnostic, [switch]$MsiUsbNameOriginDiagnostic, [string]$PersonaReplayFixture)
$ErrorActionPreference='Stop'
if($MsiUsbNameOriginDiagnostic -and -not $VisibilityDiagnostic){throw 'MsiUsbNameOriginDiagnostic requires VisibilityDiagnostic'}
if($RawVisibilityDiagnostic -and -not $VisibilityDiagnostic){throw 'RawVisibilityDiagnostic requires VisibilityDiagnostic (explicit engineering build only)'}
$root=Split-Path -Parent $PSScriptRoot
if([string]::IsNullOrWhiteSpace($OutputRoot)) {$OutputRoot=Join-Path (Split-Path (Split-Path $root -Parent) -Parent) 'Build\Validation\MSI-Motion-20261011'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
$evidence=Join-Path $OutputRoot 'evidence'
New-Item -ItemType Directory -Path $evidence -Force|Out-Null
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$winrt=Join-Path $env:WindowsSdkDir "Include\$($env:WindowsSDKVersion.TrimEnd('\'))\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$($env:WindowsSDKVersion.TrimEnd('\'))\um\x64\WindowsApp.lib"
$includes=@("/I$root\deps\webview2\build\native\include","/I$root\deps\json","/I$winrt")
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE')
if($CauseDiagnostic){$common+=('/DYMCC_MSI_CAUSE_DIAGNOSTIC')}
if($PersonaModeHandoff){$common+=('/DYMCC_MSI_PERSONA_MODE_HANDOFF')}
if($VisibilityDiagnostic){$common+=('/DYMCC_MSI_VISIBILITY_DIAGNOSTIC')}
if($RawVisibilityDiagnostic){$common+=('/DYMCC_MSI_VISIBILITY_RAW_DIAGNOSTIC')}
if($MsiLivenessUnknownFix){$common+=('/DYMCC_MSI_LIVENESS_UNKNOWN_FIX')}
if($MsiExplicitRearmFix){$common+=('/DYMCC_MSI_EXPLICIT_REARM_FIX')}
if($MsiLateHidePendingFix){$common+=('/DYMCC_MSI_LATE_HIDE_PENDING_FIX')}
if($MsiBlockedStartPreparationFix){$common+=('/DYMCC_MSI_BLOCKED_START_PREPARATION_FIX')}
if($MsiQuarantineDiagnostic){$common+=('/DYMCC_MSI_QUARANTINE_DIAGNOSTIC')}
if($VisibilityWakeDiagnostic){$common+=('/DYMCC_MSI_VISIBILITY_WAKE_DIAGNOSTIC')}
if($MsiReportChainDiagnostic){$common+=('/DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC')}
if($MsiUsbNameOriginDiagnostic){$common+=('/DYMCC_MSI_USB_NAME_ORIGIN_DIAGNOSTIC')}
$files=@('native\msi_external_raw_receipts.h','native\msi_external_raw_receipts_windows.h','native\msi_target_intent_evidence.h','tools\msi_target_intent_evidence_selftest.cpp','native\msi_physical_name_evidence.h','native\msi_slot_api_evidence.h','tools\msi_slot_api_evidence_selftest.cpp','tools\msi_quarantine_cache_regression.h','native\msi_visibility_wake_diagnostics.h','native\msi_visibility_wake_runtime.h','tools\msi_visibility_wake_regression.h','tools\controller_visibility_probe.cpp','tools\verify-msi-motion-repair.ps1','tools\verify-msi-quarantine-diagnostic-io.ps1','tools\msi_quarantine_diagnostic_io_selftest.cpp','native\msi_blocked_start_preparation.h','native\msi_quarantine_diagnostics.h','tools\msi_blocked_start_preparation_regression.h','native\msi_late_hide_pending.h','tools\msi_late_hide_pending_regression.h','native\msi_explicit_rearm_ticket.h','tools\msi_explicit_rearm_regression.h','native\msi_host_liveness_admission.h','tools\msi_host_liveness_admission_regression.h','native\msi_controller_visibility_diagnostics.h','tools\build_msi_persona_log_fixture.mjs','native\msi_persona_mode_handoff.h','native\msi_persona_mode_handoff_runtime.h','tools\msi_persona_mode_handoff_regression.h','native\msi_controller_cause_diagnostics.h','tools\msi_controller_cause_diagnostics_regression.h','native\msi_controller_transition_trace.h','native\msi_controller_transition_windows.h','tools\msi_controller_transition_logging_regression.h','tools\msi_mode_identity_regression.h','native\msi_virtual_wake.h','tools\msi_virtual_wake_regression.h','tools\build_msi_virtual_wake_fixture.mjs','native\msi_key_event_class.h','native\msi_key_event_windows.h','native\msi_rear_audit.h','tools\msi_key_diagnostics_selftest.cpp','native\msi_mode_recovery.h','native\main.cpp','native\input_idle_diagnostics.h','native\input_idle_windows.h','native\input_idle_windows_json.h','native\dsu_server.cpp','native\app.rc','native\resource.h','native\version.h','native\app.manifest','native\msi_legacy_sensor.h','native\msi_motion_capabilities.h','tools\msi_fake_sd_fence_protocol_regression.h','tools\msi_g3e_native_regression.cpp','tools\msi_legacy_sensor_selftest.cpp','native\sensor_thread_mailbox.h','tools\sensor_thread_mailbox_selftest.cpp','native\sensor_choice.h','native\sensor_sta_wait.h','tools\msi_sensor_adapter_selftest.cpp','tools\sensor_sta_selection_selftest.cpp','tools\msi_motion_log_replay.h','tools\build_msi_motion_log_replay_fixture.mjs','tools\build_msi_controller_log_fixture.mjs')
$files+=@('native\msi_official_policy_metadata.h','tools\msi_official_policy_metadata_selftest.cpp',
    'native\msi_native_foreground_isolation_runtime.h','native\msi_focus_visibility_evidence_20261012_fa1322.h',
    'tools\msi_focus_visibility_evidence_20261012_fa1322.cpp',
    'native\msi_native_foreground_isolation.h','native\msi_virtual_report_diagnostic.h',
    'native\msi_virtual_report_diagnostic_runtime.h','tools\msi_native_report_chain_pair_selftest.cpp',
    'tools\msi_virtual_report_diagnostic_selftest.cpp','tools\msi_report_chain_log_export_selftest.mjs')
$files+=@('native\msi_usb_name_origin_context.h')
$before=@($files|ForEach-Object {Get-FileHash -LiteralPath (Join-Path $root $_) -Algorithm SHA256|Select-Object Path,Hash})
# Prove the copied actual helper is sealed with all-fake platform APIs before any native-entry test.
& "$root\tools\verify-msi-quarantine-diagnostic-io.ps1" -OutputRoot "$OutputRoot\quarantine-io-boundary"
$ioBoundary=Get-Content -LiteralPath "$OutputRoot\quarantine-io-boundary\verification-summary.json" -Raw|ConvertFrom-Json
if(-not $ioBoundary.exactActualHelper -or -not $ioBoundary.exactActualSlotApiHelper -or -not $ioBoundary.allPlatformApisStubbed){throw 'Quarantine I/O preflight not trustworthy'}
& cl.exe @common /DNOMINMAX "$root\tools\msi_target_intent_evidence_selftest.cpp" @includes ("/Fo$OutputRoot\msi-intent-evidence.obj") ("/Fe$OutputRoot\msi-intent-evidence-test.exe")
if($LASTEXITCODE -ne 0){throw 'MSI pure target intent evidence compile failed'}
$intentEvidence=@(& "$OutputRoot\msi-intent-evidence-test.exe")
if($LASTEXITCODE -ne 0){throw 'MSI pure target intent evidence tests failed'}
$intentEvidence|Set-Content -LiteralPath "$evidence\msi-target-intent-evidence-pure.txt" -Encoding UTF8

& cl.exe @common /W4 /WX /DNOMINMAX "$root\tools\msi_official_policy_metadata_selftest.cpp" @includes ("/Fo$OutputRoot\msi-policy-metadata.obj") ("/Fe$OutputRoot\msi-policy-metadata-test.exe")
if($LASTEXITCODE -ne 0){throw 'MSI read-only policy metadata compile failed'}
$policyMetadata=@(& "$OutputRoot\msi-policy-metadata-test.exe")
if($LASTEXITCODE -ne 0){throw 'MSI read-only policy metadata fake test failed'}
$policyMetadata|Set-Content -LiteralPath "$evidence\msi-policy-metadata-fake.txt" -Encoding UTF8
& cl.exe @common /DNOMINMAX "$root\tools\msi_legacy_sensor_selftest.cpp" ("/Fo$OutputRoot\core.obj") ("/Fe$OutputRoot\msi-core-test.exe") /link /SUBSYSTEM:CONSOLE ole32.lib
if($LASTEXITCODE -ne 0){throw 'MSI Sensor API core compile failed'}
$core=& "$OutputRoot\msi-core-test.exe"
$core|Set-Content -LiteralPath "$evidence\msi-legacy-core-selftest.txt" -Encoding UTF8
if($LASTEXITCODE -ne 0){throw 'MSI Sensor API core test failed'}
& cl.exe @common /DYMCC_FAULT_INJECT "$root\tools\msi_sensor_adapter_selftest.cpp" ("/Fo$OutputRoot\adapter.obj") ("/Fe$OutputRoot\msi-adapter-test.exe") /link /SUBSYSTEM:CONSOLE ole32.lib oleaut32.lib
if($LASTEXITCODE -ne 0){throw 'SDK adapter lifecycle compile failed'}
$adapter=& "$OutputRoot\msi-adapter-test.exe"
$adapter|Set-Content -LiteralPath "$evidence\msi-sensor-adapter-selftest.txt" -Encoding UTF8
if($LASTEXITCODE -ne 0){throw 'SDK adapter lifecycle test failed'}
& cl.exe @common "$root\tools\sensor_sta_selection_selftest.cpp" ("/I$winrt") ("/Fo$OutputRoot\selection.obj") ("/Fe$OutputRoot\sensor-selection-test.exe") /link /SUBSYSTEM:CONSOLE $winapp user32.lib ole32.lib
if($LASTEXITCODE -ne 0){throw 'STA sensor selection compile failed'}
$selection=& "$OutputRoot\sensor-selection-test.exe"
$selection|Set-Content -LiteralPath "$evidence\sensor-sta-selection-selftest.txt" -Encoding UTF8
if($LASTEXITCODE -ne 0){throw 'STA sensor selection test failed'}
& cl.exe @common "$root\tools\sensor_thread_mailbox_selftest.cpp" ("/Fo$OutputRoot\mailbox.obj") ("/Fe$OutputRoot\sensor-mailbox-test.exe") /link /SUBSYSTEM:CONSOLE
if($LASTEXITCODE -ne 0){throw 'Sensor request mailbox compile failed'}
$mailbox=& "$OutputRoot\sensor-mailbox-test.exe"
$mailbox|Set-Content -LiteralPath "$evidence\sensor-thread-mailbox-selftest.txt" -Encoding UTF8
if($LASTEXITCODE -ne 0){throw 'Sensor request mailbox test failed'}
& cl.exe @common "$root\tools\msi_key_diagnostics_selftest.cpp" ("/Fo$OutputRoot\keys.obj") ("/Fe$OutputRoot\msi-keys-core.exe") /link /SUBSYSTEM:CONSOLE ole32.lib oleaut32.lib
if($LASTEXITCODE -ne 0){throw 'MSI key class SDK/core compile failed'}
$keys=& "$OutputRoot\msi-keys-core.exe"
$keys|Set-Content -LiteralPath "$evidence\msi-key-diagnostics-selftest.txt" -Encoding UTF8
if($LASTEXITCODE -ne 0){throw 'MSI key diagnostics core failed'}
& cl.exe @common /DYMCC_FAULT_INJECT /c "$root\tools\msi_g3e_native_regression.cpp" @includes ("/Fo$OutputRoot\regression.obj")
if($LASTEXITCODE -ne 0){throw 'Production-entry regression compile failed'}
& cl.exe @common /c "$root\native\dsu_server.cpp" ("/Fo$OutputRoot\dsu.obj")
if($LASTEXITCODE -ne 0){throw 'DSU compile failed'}
& cl.exe /nologo "$OutputRoot\regression.obj" "$OutputRoot\dsu.obj" ("/Fe$OutputRoot\msi-regression.exe") /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "$root\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" $winapp user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib
if($LASTEXITCODE -ne 0){throw 'Native regression link failed'}
if($PersonaReplayFixture){Copy-Item -LiteralPath $PersonaReplayFixture -Destination "$evidence\msi-persona-log-replay-input.json"}
if($ReplayLogRoot) {
    $fixture=Join-Path $evidence 'real-log-replay-input.json'
    & node.exe "$root\tools\build_msi_motion_log_replay_fixture.mjs" $ReplayLogRoot $fixture
    if($LASTEXITCODE -ne 0){throw 'Real exported-log replay fixture build failed'}
    if ($ControllerReplayFixture) {
        Copy-Item -LiteralPath $ControllerReplayFixture -Destination "$evidence\controller-log-replay-input.json"
        if($VirtualWakeReplayFixture) {
            Copy-Item -LiteralPath $VirtualWakeReplayFixture -Destination "$evidence\virtual-wake-replay-input.json"
            & "$OutputRoot\msi-regression.exe" $evidence $fixture "$evidence\controller-log-replay-input.json" "$evidence\virtual-wake-replay-input.json"
        } else { & "$OutputRoot\msi-regression.exe" $evidence $fixture "$evidence\controller-log-replay-input.json" }
    } else { & "$OutputRoot\msi-regression.exe" $evidence $fixture }
} else { & "$OutputRoot\msi-regression.exe" $evidence }
if($LASTEXITCODE -ne 0){throw 'MSI actual production-entry regressions failed; inspect evidence'}
if($CompileProduction) {
    # Compile the actual shipping TU WITHOUT resident fault-injection hooks.
    # No production exe is installed/launched, no app settings/drivers are changed.
    & cl.exe @common /c "$root\native\main.cpp" @includes ("/Fo$OutputRoot\main-production.obj")
    if($LASTEXITCODE -ne 0){throw 'Non-fault production compile failed'}
    # Match the normal native link/resource/manifest recipe, in isolated output.
    # This candidate is NOT launched, installed or placed in a release payload.
    Push-Location "$root\native"
    try {
        & rc.exe /nologo ("/fo$OutputRoot\app-production.res") app.rc
        if($LASTEXITCODE -ne 0){throw 'Production resource compile failed'}
    } finally { Pop-Location }
    $productionExe=Join-Path $OutputRoot 'YeManCC-MSI-repair-candidate.exe'
    & cl.exe /nologo "$OutputRoot\main-production.obj" "$OutputRoot\dsu.obj" "$OutputRoot\app-production.res" ("/Fe$productionExe") /link /SUBSYSTEM:WINDOWS /MACHINE:x64 "$root\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" $winapp user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib
    if($LASTEXITCODE -ne 0){throw 'Non-fault production link failed'}
    & mt.exe -manifest "$root\native\app.manifest" ("-outputresource:"+$productionExe+';#1')
    if($LASTEXITCODE -ne 0){throw 'Production admin manifest embedding failed'}
    $symbols=& dumpbin.exe /nologo /symbols "$OutputRoot\main-production.obj"
    if($LASTEXITCODE -ne 0){throw 'Production symbol verification failed'}
    if($symbols -match 'g_sensorThreadCommandForTest|g_msiControllerOfficialStackForTest|g_msiControllerModeProbeIoForTest|g_completionForTest|g_msiQuarantineSlotEvidenceForTest|g_msiStartPreparationEntriesForTest|g_msiVisibilityObserverActiveForTest|g_msiVisibilityObserverLaunchForTest|g_msiVisibilityObserverMetadataForTest'){throw 'Fault-only sensor transport leaked into production object'}
    'fault-only sensor command transport absent'|Set-Content -LiteralPath "$evidence\production-fault-hook-check.txt" -Encoding UTF8
}
$after=@($files|ForEach-Object {Get-FileHash -LiteralPath (Join-Path $root $_) -Algorithm SHA256|Select-Object Path,Hash})
for($i=0;$i -lt $before.Count;$i++){if($before[$i].Hash -ne $after[$i].Hash){throw "Source changed while compiling: $($before[$i].Path). Rerun against a stable source snapshot."}}
$personaReport=Get-Content -LiteralPath "$evidence\msi-persona-mode-handoff-regression.txt" -Raw
$observedPersonaReplayPairs=([regex]::Matches($personaReport,'(?m)^realPid=')).Count
if($PersonaReplayFixture -and $observedPersonaReplayPairs -ne 4){throw 'Requested four-pair persona replay did not execute completely'}
[pscustomobject]@{msiTargetIntentEvidenceResult=$intentEvidence[-1];msiPolicyMetadataFakeResult=$policyMetadata[-1];visibilityWakeDiagnosticBuild=[bool]$VisibilityWakeDiagnostic;msiUsbNameOriginDiagnosticBuild=[bool]$MsiUsbNameOriginDiagnostic;msiReportChainDiagnosticBuild=[bool]$MsiReportChainDiagnostic;quarantineIoBoundaryVerified=$true;personaReplayProvided=[bool]$PersonaReplayFixture;personaReplayPairCount=$observedPersonaReplayPairs;personaReplayFixtureSha256=$(if($PersonaReplayFixture){(Get-FileHash -LiteralPath "$evidence\msi-persona-log-replay-input.json" -Algorithm SHA256).Hash}else{$null});sourceHashes=$after;coreResult=$core[-1];adapterResult=$adapter[-1];selectionResult=$selection[-1];mailboxResult=$mailbox[-1];causeDiagnosticBuild=[bool]$CauseDiagnostic;personaModeHandoffBuild=[bool]$PersonaModeHandoff;visibilityDiagnosticBuild=[bool]$VisibilityDiagnostic;rawVisibilityDiagnosticBuild=[bool]$RawVisibilityDiagnostic;msiLivenessUnknownFixBuild=[bool]$MsiLivenessUnknownFix;msiExplicitRearmFixBuild=$true;msiExplicitRearmFixRequested=[bool]$MsiExplicitRearmFix;msiLateHidePendingFixBuild=[bool]$MsiLateHidePendingFix;msiBlockedStartPreparationFixBuild=[bool]$MsiBlockedStartPreparationFix;msiQuarantineDiagnosticBuild=[bool]$MsiQuarantineDiagnostic;productionCompiled=[bool]$CompileProduction;productionLinked=[bool]$CompileProduction;productionArtifact=$productionExe;productionArtifactSha256=$(if($CompileProduction){(Get-FileHash -LiteralPath $productionExe -Algorithm SHA256).Hash}else{$null});hardwareVerified=$false;realLogsReplayed=[bool]$ReplayLogRoot;scope='Native source/core/entry regressions plus isolated non-fault native compile/link/manifest; no executable installation or HID/Sensor API hardware access'}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$evidence\verification-summary.json" -Encoding UTF8
Write-Output "MSI_MOTION_REGRESSION_OK $evidence"
