param(
 [string]$OutputDirectory='G:\YeManCC-Work\_scratch\steamdeck-live-20261005\transport-test',
 [string]$FailureDiagnosticsCase="",
 [string]$UiIdentityReceiptCase="",
 [string]$MainSharedIdentityCase="",
 [string]$ListenerInventoryCase="",
 [switch]$EndpointDiscoveryOnly,
 [string]$EndpointDiscoveryCase="",
 [switch]$PrepareOnly,
 [switch]$TransportControlsOnly,
 [string]$TransportControlsCase="",
 [switch]$FailureDiagnosticsOnly,
 [switch]$FailureDiagnosticsOldSourceNegativeOnly,
 [switch]$ListenerInventoryOnly,
 [switch]$DocumentReadOnly,
 [switch]$ExpectedSteamIdentityOnly,
 [switch]$MainSharedIdentityOnly,
 [switch]$UiIdentityReceiptOnly,
 [switch]$DeckyFinalAdmitOnly,
 [switch]$LiveReadOnly,
 [switch]$LiveRunningReadOnly,
 [switch]$LiveAppLifetimeReadOnly,
 [switch]$LiveApiSurfaceReadOnly,
 [switch]$LiveApiContractReadOnly,
 [switch]$LiveOverlayRoundTrip,
 [string]$SteamPath='',
 [uint32]$Account=0,
 [uint32]$AppId=0,
 [string]$ControllerPath='',
 [string]$ControllerType='controller_generic'
)
$ErrorActionPreference='Stop'
if($PSBoundParameters.ContainsKey('TransportControlsCase') -and (!$TransportControlsOnly -or $TransportControlsCase -notin @('queue-source-revoke','queue-owner-revoke','queue-owner-throws','queue-inherited-deadline'))){throw 'Invalid transport control case or missing explicit transport controls selection'}
if($TransportControlsOnly -and (@($EndpointDiscoveryOnly,$FailureDiagnosticsOnly,$FailureDiagnosticsOldSourceNegativeOnly,$ListenerInventoryOnly,$DocumentReadOnly,$ExpectedSteamIdentityOnly,$MainSharedIdentityOnly,$UiIdentityReceiptOnly,$DeckyFinalAdmitOnly,$LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count -or $FailureDiagnosticsCase -or $UiIdentityReceiptCase -or $MainSharedIdentityCase -or $ListenerInventoryCase -or $EndpointDiscoveryCase)){throw 'Transport controls require an exact selection without other groups or live access'}
if(@($LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count -gt 1){throw 'Choose one live operation'}
if(($LiveReadOnly -or $LiveRunningReadOnly -or $LiveApiSurfaceReadOnly -or $LiveApiContractReadOnly -or $LiveOverlayRoundTrip) -and (!$SteamPath -or !$Account)){throw 'Explicit SteamPath and Account are required for live access'}
if($LiveReadOnly -and !$ControllerPath){throw 'ControllerPath is required for live read'}
if($ListenerInventoryOnly -and @($LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count){throw 'Listener inventory selection cannot run live operations'}
if($DeckyFinalAdmitOnly -and @($ListenerInventoryOnly,$LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count){throw 'Decky final admission selection cannot run another group or live operation'}
if($ExpectedSteamIdentityOnly -and @($DeckyFinalAdmitOnly,$ListenerInventoryOnly,$LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count){throw 'Expected identity selection cannot run another group or live operation'}
if($MainSharedIdentityOnly -and @($ExpectedSteamIdentityOnly,$DeckyFinalAdmitOnly,$ListenerInventoryOnly,$LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count){throw 'Main identity selection cannot run another group or live operation'}
if($UiIdentityReceiptOnly -and @($MainSharedIdentityOnly,$ExpectedSteamIdentityOnly,$DeckyFinalAdmitOnly,$ListenerInventoryOnly,$LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count){throw 'UI identity selection cannot run another group or live operation'}
if(($FailureDiagnosticsOnly -or $FailureDiagnosticsOldSourceNegativeOnly) -and @($ListenerInventoryOnly,$ExpectedSteamIdentityOnly,$MainSharedIdentityOnly,$UiIdentityReceiptOnly,$DeckyFinalAdmitOnly,$LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count){throw 'Failure diagnostics cannot combine original groups or live access'}
if($FailureDiagnosticsOnly -and $FailureDiagnosticsOldSourceNegativeOnly){throw 'Choose exactly one failure diagnostics selection'}
if($DocumentReadOnly -and @($FailureDiagnosticsOnly,$FailureDiagnosticsOldSourceNegativeOnly,$UiIdentityReceiptOnly,$MainSharedIdentityOnly,$ExpectedSteamIdentityOnly,$DeckyFinalAdmitOnly,$ListenerInventoryOnly,$LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count){throw 'Document read selection cannot run another group or live operation'}
if($UiIdentityReceiptCase -and (!$UiIdentityReceiptOnly -or $UiIdentityReceiptCase -notin @('late-caught-failure','client-state','public-read-owner','public-show-owner'))){throw 'Invalid UI receipt case or missing explicit UI receipt selection'}
if($PSBoundParameters.ContainsKey('ListenerInventoryCase') -and (!$ListenerInventoryOnly -or $ListenerInventoryCase -notin @('endpoint-parent-prefilter','endpoint-parent-prefilter-cost'))){throw 'Invalid listener inventory case or missing explicit listener selection'}
if($PSBoundParameters.ContainsKey('MainSharedIdentityCase') -and (!$MainSharedIdentityOnly -or $MainSharedIdentityCase -notin @('native-owner','native-return','mutation-policy','menu-mutation-fields','process-pid-fields','socket-owner-construction','command-busy-return','defer-receipt','reply-envelope','message-assembly','menu-pure-owner','discovery-cancel','source-drain','source-drain-bound','source-drain-repair','source-drain-ui','source-drain-write','source-drain-missing'))){throw 'Invalid main identity case or missing explicit main identity selection'}
if($EndpointDiscoveryOnly -and @($FailureDiagnosticsOnly,$FailureDiagnosticsOldSourceNegativeOnly,$DocumentReadOnly,$UiIdentityReceiptOnly,$MainSharedIdentityOnly,$ExpectedSteamIdentityOnly,$DeckyFinalAdmitOnly,$ListenerInventoryOnly,$LiveReadOnly,$LiveRunningReadOnly,$LiveAppLifetimeReadOnly,$LiveApiSurfaceReadOnly,$LiveApiContractReadOnly,$LiveOverlayRoundTrip).Where({$_}).Count){throw 'Endpoint discovery selection cannot combine other groups or live access'}
if($PSBoundParameters.ContainsKey('EndpointDiscoveryCase') -and (!$EndpointDiscoveryOnly -or $EndpointDiscoveryCase -ne 'bootstrap-failure-diagnostics')){throw 'Invalid endpoint discovery case or missing explicit endpoint selection'}
$root=Split-Path $PSScriptRoot
if($DeckyFinalAdmitOnly){
 # Keep the compiled main dispatch in this actual ManualRepair consumer current.
 # Only its owner-observation ports are inert; Recovery and transport are real.
 $main=[IO.File]::ReadAllText((Join-Path $root 'native/main.cpp'))
 $fixture=[IO.File]::ReadAllText((Join-Path $root 'tools/steam_live_transport_fixture.cpp'))
 $functionAt=$main.IndexOf('static ymcc::deckyrepair::Dependencies deckySidebarRepairDependencies(){',[StringComparison]::Ordinal)
 if($functionAt -lt 0){throw 'Decky production dependencies anchor missing'}
 $dispatchAt=$main.IndexOf('        [](const auto& session,const auto& action,const auto& admit){',$functionAt,[StringComparison]::Ordinal)
 if($dispatchAt -lt 0){throw 'Decky production dispatch anchor missing'}
 $dispatchEnd=$main.IndexOf('        []{return GetTickCount64();}',$dispatchAt,[StringComparison]::Ordinal)
 if($dispatchEnd -lt 0){throw 'Decky production dispatch end missing'}
 $dispatch=$main.Substring($dispatchAt,$dispatchEnd-$dispatchAt).Trim().TrimEnd(',')
 if(!$fixture.Replace("`r`n","`n").Contains($dispatch.Replace("`r`n","`n"))){throw 'Decky final-admit consumer production dispatch is stale'}
}
if($PrepareOnly -and (Test-Path -LiteralPath $OutputDirectory)){throw 'Fresh preparation output directory required'}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
# Compile the exact current main consumer in the existing transport fixture.
if(!$DocumentReadOnly){
$mainSource=[IO.File]::ReadAllText((Join-Path $root 'native/main.cpp'))
$mainStart=$mainSource.IndexOf('static json steamLiveRequest(DWORD account, json request, ',[StringComparison]::Ordinal)
if($mainStart -lt 0){throw 'Main Steam request consumer anchor missing'}
$mainEnd=$mainSource.IndexOf('static bool steamLiveCanDefer(',$mainStart,[StringComparison]::Ordinal)
if($mainEnd -le $mainStart){throw 'Main Steam request consumer end missing'}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'main_steam_live_request.inc'),$mainSource.Substring($mainStart,$mainEnd-$mainStart),[Text.UTF8Encoding]::new($false))
}
 $bootstrap=[IO.File]::ReadAllText((Join-Path $root 'native/decky_sidebar_bootstrap.h'))
 # The existing memory adapter compiles the exact current retirement consumer.
 # Its HTTP/request leaves remain memory ports and do not prove real async cleanup.
 $retirementStart=$bootstrap.IndexOf('template<class Work>',[StringComparison]::Ordinal)
 $retirementEnd=$bootstrap.IndexOf('inline void bootstrapDiscoveryRejected(',$retirementStart,[StringComparison]::Ordinal)
 if($retirementStart -lt 0 -or $retirementEnd -le $retirementStart){throw 'Bootstrap retirement consumer missing'}
 $retirementBlock=$bootstrap.Substring($retirementStart,$retirementEnd-$retirementStart).TrimEnd()
 $discoveryFixture=[IO.File]::ReadAllText((Join-Path $root 'tools/steam_endpoint_discovery_cases.inc'))
 if(!$discoveryFixture.Replace("`r`n","`n").Contains($retirementBlock.Replace("`r`n","`n"))){throw 'Bootstrap retirement memory consumer stale; synchronize the exact product template before compilation'}
 $begin=$bootstrap.IndexOf('        const auto started=GetTickCount64();',$bootstrap.IndexOf('inline nlohmann::json injectBootstrap('),[StringComparison]::Ordinal)
 $finish=$bootstrap.IndexOf('        auto observed=std::make_shared<ObservedProcess>(endpoint.pid);',$begin,[StringComparison]::Ordinal)
 if($begin -lt 0 -or $finish -le $begin){throw 'Current shared bootstrap discovery consumer missing'}
 $actual=$bootstrap.Substring($begin,$finish-$begin)+"`nreturn {endpoint,target};`n"
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'bootstrap_discovery_current.inc'),$actual,[Text.UTF8Encoding]::new($false))
 # A before-source block is supplied only by an explicit evidence override.
 # The default product consumer compiles the same current block in that slot.
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'bootstrap_discovery_before.inc'),$actual,[Text.UTF8Encoding]::new($false))
 $failureBegin=$bootstrap.LastIndexOf('}catch(const std::exception& error){',[StringComparison]::Ordinal)
 if($failureBegin -lt 0){throw 'Bootstrap final failure projection missing'}
 $failureBegin++
 $failureReturn=$bootstrap.IndexOf('        return result;',$failureBegin,[StringComparison]::Ordinal)
 $failureEnd=if($failureReturn -ge 0){$bootstrap.IndexOf('}',$failureReturn,[StringComparison]::Ordinal)+1}else{-1}
 if($failureEnd -le $failureBegin){throw 'Bootstrap final failure projection end missing'}
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'bootstrap_failure_result.inc'),$bootstrap.Substring($failureBegin,$failureEnd-$failureBegin),[Text.UTF8Encoding]::new($false))
$uiFixtureDefine=if($DocumentReadOnly){'/DYMCC_STEAM_DOCUMENT_READ_ONLY'}elseif($FailureDiagnosticsOldSourceNegativeOnly){'/DYMCC_FAILURE_OLD_SOURCE'}else{''}
if($MainSharedIdentityOnly -and $MainSharedIdentityCase -eq 'socket-owner-construction'){$uiFixtureDefine+=' /DYMCC_SOCKET_OWNER_CONSTRUCTION_FIXTURE'}
if($UiIdentityReceiptOnly -or $MainSharedIdentityCase -eq 'source-drain-ui'){
 $uiSource=[IO.File]::ReadAllText((Join-Path $root 'native/steam_ui_mode.h')).Replace("`r`n","`n")
 $readStart=$uiSource.IndexOf('inline Json read(',[StringComparison]::Ordinal)
 if($readStart -lt 0){throw 'UI receipt read consumer missing'}
 $readEnd=$uiSource.IndexOf('inline const wchar_t* uriFor(',$readStart,[StringComparison]::Ordinal)
 if($readEnd -le $readStart){throw 'UI receipt read consumer end missing'}
 $readBody=$uiSource.Substring($readStart,$readEnd-$readStart).Replace('steamsession::Observer& observer','::DeckySelectedSessionPort& observer')
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'ui_receipt_read.inc'),$readBody,[Text.UTF8Encoding]::new($false))
 $modePrefix='    modeObserver.readOwned = '
 $ownedModeRead=$mainSource.Contains($modePrefix)
 if(!$ownedModeRead){$modePrefix='    modeObserver.read = '}
 $modeReadAt=$mainSource.IndexOf($modePrefix,[StringComparison]::Ordinal)
 $modeReadEnd=$mainSource.IndexOf('    modeObserver.changed = ',$modeReadAt,[StringComparison]::Ordinal)
 if($modeReadAt -lt 0 -or $modeReadEnd -le $modeReadAt){throw 'Actual shared mode-read callback missing'}
 $modeBody='static const auto productionUiModeRead = '+$mainSource.Substring($modeReadAt+$modePrefix.Length,$modeReadEnd-$modeReadAt-$modePrefix.Length)
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'main_ui_mode_read.inc'),$modeBody,[Text.UTF8Encoding]::new($false))
 if($ownedModeRead){
  $powerBegin=$mainSource.IndexOf('[[maybe_unused]] static bool captureSharedPowerLifecycle(', [StringComparison]::Ordinal)
  $powerEnd=$mainSource.IndexOf('[[maybe_unused]] static ymcc::sleeppower::RevocationMirrors', $powerBegin, [StringComparison]::Ordinal)
  if($powerBegin -lt 0 -or $powerEnd -le $powerBegin){throw 'Actual shared power helpers missing'}
  [IO.File]::WriteAllText((Join-Path $OutputDirectory 'mode_power_helpers.inc'),$mainSource.Substring($powerBegin,$powerEnd-$powerBegin),[Text.UTF8Encoding]::new($false))
  $uiFixtureDefine='/DYMCC_UI_RECEIPT_FIXTURE /DYMCC_UI_RECEIPT_OWNED_FIXTURE'
 }else{$uiFixtureDefine='/DYMCC_UI_RECEIPT_FIXTURE'}
}
if($UiIdentityReceiptCase -in @('public-read-owner','public-show-owner')){
 $stateBegin=$mainSource.IndexOf('    ipc_on("steam.uiState", ',[StringComparison]::Ordinal)
 $stateEnd=$mainSource.IndexOf('    ipc_on("steam.ui.showBigPicture",',$stateBegin,[StringComparison]::Ordinal)
 if($stateBegin -lt 0 -or $stateEnd -le $stateBegin){throw 'Actual public UI-state consumer missing'}
 $state=$mainSource.Substring($stateBegin,$stateEnd-$stateBegin).Trim()
 if(!$state.StartsWith('ipc_on("steam.uiState", ') -or !$state.EndsWith(');')){throw 'Public UI-state wrapper changed'}
 $state='static const auto productionUiStateRead = '+$state.Substring('ipc_on("steam.uiState", '.Length,$state.Length-'ipc_on("steam.uiState", '.Length-2)+';'
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'main_ui_state_read.inc'),$state,[Text.UTF8Encoding]::new($false))
 $showBegin=$uiSource.IndexOf('inline Json showBigPicture(',[StringComparison]::Ordinal)
 $showEnd=$uiSource.IndexOf('// Graceful fixed-command exit',$showBegin,[StringComparison]::Ordinal)
 $currentBegin=$uiSource.IndexOf('inline bool clientCommandCurrent(',[StringComparison]::Ordinal)
 $currentEnd=$uiSource.IndexOf('inline ClientDispatch dispatchClientCommand(',$currentBegin,[StringComparison]::Ordinal)
 if($showBegin -lt 0 -or $showEnd -le $showBegin -or $currentBegin -lt 0 -or $currentEnd -le $currentBegin){throw 'Actual display-read owner consumers missing'}
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'ui_show_read.inc'),$uiSource.Substring($currentBegin,$currentEnd-$currentBegin)+$uiSource.Substring($showBegin,$showEnd-$showBegin).Replace('steamsession::Observer& observer','::DeckySelectedSessionPort& observer'),[Text.UTF8Encoding]::new($false))
 $uiFixtureDefine+=' /DYMCC_UI_PUBLIC_READ_OWNER_FIXTURE'
}
if($PrepareOnly){
 $prepared=[ordered]@{status='PREPARED_ONLY';compiled=$false;businessExecuted=$false;transportControlsOnly=[bool]$TransportControlsOnly;transportControlsCase=$TransportControlsCase;mainSharedIdentity=[bool]$MainSharedIdentityOnly;mainSharedIdentityCase=$MainSharedIdentityCase;documentReadOnly=[bool]$DocumentReadOnly;mainSource=(Join-Path $root 'native/main.cpp')}
 $preparedPath=Join-Path $OutputDirectory 'PREPARED.json'
 $stream=[IO.File]::Open($preparedPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($prepared|ConvertTo-Json -Depth 8));$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
 $prepared|ConvertTo-Json -Depth 8
 return
}
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
REM BuildTools may omit Windows SDK registry discovery; use the installed SDK explicitly.
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\%WindowsSDKVersion%winrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /sourceDependencies "$OutputDirectory\fixture.dependencies.json" $uiFixtureDefine /I"$root\deps\json" /I"$root\native" /I"$OutputDirectory" "$root\tools\steam_live_transport_fixture.cpp" /Fo"$OutputDirectory\fixture.obj" /Fe"$OutputDirectory\fixture.exe" /link /VERBOSE:LIB advapi32.lib
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat
if($LASTEXITCODE -ne 0){throw 'Transport fixture compile failed'}
$exe=Join-Path $OutputDirectory 'fixture.exe'
if($TransportControlsOnly){ if($TransportControlsCase){ & $exe --transport-controls-only --case $TransportControlsCase }else{ & $exe --transport-controls-only } }
elseif($EndpointDiscoveryOnly){ if($EndpointDiscoveryCase){ & $exe --endpoint-discovery-only --case $EndpointDiscoveryCase }else{ & $exe --endpoint-discovery-only } }
elseif($FailureDiagnosticsOnly){ if($FailureDiagnosticsCase){ & $exe --failure-diagnostics-only --case $FailureDiagnosticsCase }else{ & $exe --failure-diagnostics-only } }
elseif($FailureDiagnosticsOldSourceNegativeOnly){ & $exe --failure-diagnostics-old-source-negative-only }
elseif($DocumentReadOnly){ & $exe --document-read-only (Join-Path $OutputDirectory 'document-result.json') }
elseif($UiIdentityReceiptOnly){ if($UiIdentityReceiptCase){ & $exe --ui-receipt-only --case $UiIdentityReceiptCase }else{ & $exe --ui-receipt-only } }
elseif($MainSharedIdentityOnly){ if($MainSharedIdentityCase){ & $exe --main-shared-identity-only --case $MainSharedIdentityCase }else{ & $exe --main-shared-identity-only } }
elseif($ExpectedSteamIdentityOnly){ & $exe --expected-steam-identity-only }
elseif($DeckyFinalAdmitOnly){ & $exe --decky-final-admit-only }
elseif($ListenerInventoryOnly){ if($ListenerInventoryCase){ & $exe --listener-inventory-only --case $ListenerInventoryCase }else{ & $exe --listener-inventory-only } }
elseif($LiveReadOnly){ & $exe --live-readonly $SteamPath $Account $ControllerPath $ControllerType }
elseif($LiveRunningReadOnly){ if($AppId){ & $exe --live-running-read $SteamPath $Account $AppId } else { & $exe --live-running-read $SteamPath $Account } }
elseif($LiveAppLifetimeReadOnly){ if($AppId){ & $exe --live-app-lifetime-read $SteamPath $Account $AppId } else { & $exe --live-app-lifetime-read $SteamPath $Account } }
elseif($LiveApiSurfaceReadOnly){ & $exe --live-api-surface-read $SteamPath $Account }
elseif($LiveApiContractReadOnly){ & $exe --live-api-contract-read $SteamPath $Account }
elseif($LiveOverlayRoundTrip){ & $exe --live-overlay-roundtrip $SteamPath $Account }
else{ & $exe }
if($LASTEXITCODE -ne 0){throw "Transport test failed: $LASTEXITCODE"}
