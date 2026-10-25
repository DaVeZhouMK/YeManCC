[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,
  [string]$OldNativeExe='', [string]$NewNativeExe='', [string]$RealProjectRoot='')
$ErrorActionPreference='Stop'
if (-not $RealProjectRoot) { $RealProjectRoot=Split-Path -Parent $PSScriptRoot }
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
New-Item -ItemType Directory -Force -Path $ValidationRoot | Out-Null
. (Join-Path $PSScriptRoot 'release-export-identity.ps1')
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$results=[Collections.Generic.List[object]]::new()
$marker='steam-wake-round26-bound-final-wake-readiness'
$policy='ymcc-steam-exact-instance-join-v1'
function Text([string]$Path,[string]$Value) {
  New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($Path)) | Out-Null
  [IO.File]::WriteAllText($Path,$Value,[Text.UTF8Encoding]::new($false))
}
function Fixture {
  $root=Join-Path $ValidationRoot ([Guid]::NewGuid().ToString('N'))
  $project=Join-Path $root 'source'; $work=Join-Path $root 'workspace'; $stage=Join-Path $root 'stage'
  foreach($scope in @('native','src','public','decky-plugin','InputHost','FanLab/LightSetter','scripts')) { Text (Join-Path $project ($scope+'/input.txt')) 'inert' }
  $requiredBlock=[regex]::Match([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'release-export-identity.ps1')),'foreach\(\$relative in @\((.*?)\)\)\s*\{',[Text.RegularExpressions.RegexOptions]::Singleline)
  if(-not $requiredBlock.Success){throw 'Required export source contract missing'}
  foreach($entry in [regex]::Matches($requiredBlock.Groups[1].Value,"'([^']+)'")){Text (Join-Path $project $entry.Groups[1].Value) 'inert'}
  # Inert fixture only: satisfy the shared battery payload identity contract.
  # No BatteryHost executable is launched and no production battery source is changed.
  $batteryFixturePayload=@('YeManBatteryHost.exe','YeManBatteryHost.dll','YeManBatteryHost.deps.json','YeManBatteryHost.runtimeconfig.json','LibreHardwareMonitorLib.dll','RAMSPDToolkit-NDD.dll','System.Management.dll','THIRD-PARTY-NOTICES.md','LICENSE.LibreHardwareMonitor.txt','LICENSE.System.Management.txt','battery-host.payload.json')
  Text (Join-Path $project 'native/BatteryHost/payload-files.json') ($batteryFixturePayload|ConvertTo-Json)
  foreach($name in $batteryFixturePayload) {
    Text (Join-Path $project ('PowerControl/battery-host/'+$name)) 'inert-battery'
    if($name -ne 'battery-host.payload.json') { Text (Join-Path $work ('Build/App/BatteryHost/'+$name)) 'inert-battery' }
  }
  Text (Join-Path $project 'version.json') '{"version":"0.0.35"}'
  foreach($name in @('sleep_recovery_auto.h','sleep_steam_recovery.h','sleep_steam_capture.h','sleep_steam_tracking.h','sleep_steam_input_context.h','sleep_steam_game_probe.h','sleep_steam_settle.h')) { Text (Join-Path $project ('native/'+$name)) 'inert' }
  Text (Join-Path $project 'native/sleep_recovery_auto.h') ('"'+$marker+'" "wakeReadiness"')
  Text (Join-Path $project 'native/sleep_steam_recovery.h') ('"'+$marker+'" "'+$policy+'" "steamSessionRetry"')
  foreach($name in @('index.html','hud-output.html','app.config.json','assets/current.js')) { Text (Join-Path $work ('Build/App/Web/'+$name)) 'web' }
  foreach($name in @('YeManCC.exe','YeManRecoveryService.exe','YeManControllerVisibilityProbe.exe','YeManTaskViewHelper.exe','YMCCRtssProfileHelper.exe','YMCCOverlayBridge.dll')) { Text (Join-Path $work ('Build/App/Native/'+$name)) 'native' }
  Text (Join-Path $work 'Build/App/Native/YeManCC.exe') ($marker+' '+$policy+' wakeReadiness steamSessionRetry')
  foreach($name in @('dist/index.js','plugin.json','package.json','LICENSE.decky-api','LICENSE.vue')) { Text (Join-Path $work ('Build/App/Decky/plugins/ymcc-sidebar/'+$name)) 'decky' }
  foreach($name in @('PluginLoader_noconsole.exe','LICENSE.decky-loader')) { Text (Join-Path $project ('PowerControl/decky/'+$name)) 'loader' }
 foreach($name in @('YeManInputHost.exe','YeManInputHost.dll','YeManInputHost.deps.json','YeManInputHost.runtimeconfig.json','HIDMaestro.Core.dll')) { Text (Join-Path $work ('Build/App/Native/InputHost/'+$name)) ('inert-inputhost-'+$name) }
 foreach($name in @('YeManLightSetter.exe','YeManLightSetter.dll','YeManLightSetter.deps.json','YeManLightSetter.runtimeconfig.json','YeManLightSetter.json','YeManLightDeviceCore.dll')) { Text (Join-Path $project ('PowerControl/light-setter/'+$name)) ('inert-light-setter-'+$name) }
  return @{project=$project;work=$work;stage=$stage;exe=(Join-Path $work 'Build/App/Native/YeManCC.exe');zip=(Join-Path $root 'fixture.zip')}
}
function Capture($f) { Write-ExportBuildIdentity $f.project $f.work @(Get-ExportSourceIndex $f.project) }
function Stage($f,$build) {
  foreach($item in $build.payload) {
    $src=if($item.lane-eq'web') { Join-Path $f.work ('Build/App/Web/'+$item.path.Substring(8)) }
         elseif($item.lane-eq'native') { Join-Path $f.work ('Build/App/Native/'+$item.path.Substring(8)) }
         elseif($item.lane-eq'inputhost') { Join-Path $f.work ('Build/App/Native/InputHost/'+[IO.Path]::GetFileName($item.path)) }
         elseif($item.path.StartsWith('PowerControl/decky/plugins/')) { Join-Path $f.work ('Build/App/Decky/'+$item.path.Substring(19)) }
         else { Join-Path $f.project $item.path }
    $dest=Join-Path $f.stage $item.path
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($dest)) | Out-Null
    Copy-Item -LiteralPath $src -Destination $dest
  }
  $null=Write-ExportPackageIdentity $f.stage $build
  [IO.Compression.ZipFile]::CreateFromDirectory($f.stage,$f.zip)
}
function Reject([scriptblock]$Action,[string]$Reason) {
  $message=''; try { & $Action | Out-Null } catch { $message=$_.Exception.Message }
  if (-not $message.StartsWith($Reason,[StringComparison]::Ordinal)) { throw "Expected $Reason, got $message" }
}
function Case([string]$Name,[scriptblock]$Action) {
  $f=Fixture
  try { & $Action $f; $results.Add(@{name=$Name;passed=$true}) }
  catch { $results.Add(@{name=$Name;passed=$false;error=$_.Exception.Message}) }
}
Case 'current Steam build identity propagates through staging and ZIP' {param($f)
  $b=Capture $f; $v=Assert-ExportBuildIdentity $f.project $f.work
  if($v.steamSleepRecovery.implementation-ne$marker -or $v.steamSleepRecovery.fieldAcceptance) { throw 'Invalid Steam delivery receipt' }
  Stage $f $b; $r=Assert-ExportArchiveIdentity $f.zip $f.stage $b
  if($r.steamSleepRecovery.nativeSha256-ne$b.steamSleepRecovery.nativeSha256) { throw 'Archive receipt lost native identity' }
  $null=Assert-SteamSleepArchiveExport $f.project $f.zip $f.exe
}
Case 'old R25 with a fresh timestamp cannot be captured as R26' {param($f)
  Text $f.exe ('steam-wake-round25-original-steam-binding '+$policy)
  (Get-Item -LiteralPath $f.exe).LastWriteTimeUtc=[DateTime]::UtcNow.AddDays(1)
  Reject { Capture $f } 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'R26 without the account-read amendment cannot be captured' {param($f)
  Text $f.exe ($marker+' '+$policy)
  Reject { Capture $f } 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'missing joint binding marker is rejected' {param($f)
  Text $f.exe ($marker+' steamSessionRetry')
  Reject { Capture $f } 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'incomplete Steam source cannot silently pass as legacy' {param($f)
  Remove-Item -LiteralPath (Join-Path $f.project 'native/sleep_steam_capture.h')
  Reject { Capture $f } 'EXPORT_STEAM_SLEEP_SOURCE_MISSING'
}
Case 'inconsistent implementation markers on the two owners reject export' {param($f)
  Text (Join-Path $f.project 'native/sleep_steam_recovery.h') ('"steam-wake-round25-original-steam-binding" "'+$policy+'"')
  Reject { Capture $f } 'EXPORT_STEAM_SLEEP_SOURCE_VERSION_MISMATCH'
}
Case 'missing binding policy in the actual source rejects export' {param($f)
  Text (Join-Path $f.project 'native/sleep_steam_recovery.h') ('"'+$marker+'"')
  Reject { Capture $f } 'EXPORT_STEAM_SLEEP_BINDING_POLICY_MISSING'
}
Case 'removed Steam receipt cannot pass the normal packaging gate' {param($f)
  $b=Capture $f; $b.Remove('steamSleepRecovery')
  Text (Join-Path $f.work 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json') ($b|ConvertTo-Json -Depth 7)
  Reject { Assert-ExportBuildIdentity $f.project $f.work } 'EXPORT_STEAM_SLEEP_IDENTITY_MISSING'
}
Case 'wrong implementation receipt cannot pass the normal packaging gate' {param($f)
  $b=Capture $f; $b.steamSleepRecovery.implementation='old'
  Text (Join-Path $f.work 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json') ($b|ConvertTo-Json -Depth 7)
  Reject { Assert-ExportBuildIdentity $f.project $f.work } 'EXPORT_STEAM_SLEEP_IDENTITY_MISMATCH'
}
Case 'wrong Steam source hash in the receipt is rejected' {param($f)
  $b=Capture $f; $b.steamSleepRecovery.sourceSha256['sleep_steam_recovery.h']='wrong'
  Text (Join-Path $f.work 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json') ($b|ConvertTo-Json -Depth 7)
  Reject { Assert-ExportBuildIdentity $f.project $f.work } 'EXPORT_STEAM_SLEEP_SOURCE_IDENTITY_MISMATCH'
}
Case 'old Release replaced into staging is rejected' {param($f)
  $b=Capture $f; Stage $f $b
  Text (Join-Path $f.stage 'YeManCC/YeManCC.exe') 'steam-wake-round25-original-steam-binding'
  Reject { Assert-ExportStagedBuild $f.stage $b } 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'same marker but different native bytes in final ZIP is rejected' {param($f)
  $b=Capture $f; Stage $f $b
  $zip=[IO.Compression.ZipFile]::Open($f.zip,[IO.Compression.ZipArchiveMode]::Update)
  try {
    @($zip.Entries | Where-Object { $_.FullName.Replace('\','/') -eq 'YeManCC/YeManCC.exe' })[0].Delete()
    $e=$zip.CreateEntry('YeManCC/YeManCC.exe'); $w=[IO.StreamWriter]::new($e.Open())
    try {$w.Write($marker+' '+$policy+' wakeReadiness steamSessionRetry changed')} finally {$w.Dispose()}
  } finally {$zip.Dispose()}
  Reject { Assert-SteamSleepArchiveExport $f.project $f.zip $f.exe } 'EXPORT_STEAM_SLEEP_ZIP_NATIVE_HASH_MISMATCH'
}
Case 'duplicate case-aliased native entries in ZIP are rejected' {param($f)
  $b=Capture $f; Stage $f $b
  $zip=[IO.Compression.ZipFile]::Open($f.zip,[IO.Compression.ZipArchiveMode]::Update)
  try { $entry=$zip.CreateEntry('yemancc/yemancc.exe') } finally {$zip.Dispose()}
  Reject { Assert-SteamSleepArchiveExport $f.project $f.zip $f.exe } 'EXPORT_STEAM_SLEEP_ZIP_NATIVE_AMBIGUOUS_OR_MISSING'
}
Case 'missing native in ZIP is rejected' {param($f)
  $b=Capture $f; Stage $f $b
  $zip=[IO.Compression.ZipFile]::Open($f.zip,[IO.Compression.ZipArchiveMode]::Update)
  try {@($zip.Entries | Where-Object { $_.FullName.Replace('\','/') -eq 'YeManCC/YeManCC.exe' })[0].Delete()} finally {$zip.Dispose()}
  Reject { Assert-SteamSleepArchiveExport $f.project $f.zip $f.exe } 'EXPORT_STEAM_SLEEP_ZIP_NATIVE_AMBIGUOUS_OR_MISSING'
}
Case 'old R25 in ZIP is rejected even beside a valid R26 build' {param($f)
  $b=Capture $f; Stage $f $b
  $zip=[IO.Compression.ZipFile]::Open($f.zip,[IO.Compression.ZipArchiveMode]::Update)
  try {
    @($zip.Entries | Where-Object { $_.FullName.Replace('\','/') -eq 'YeManCC/YeManCC.exe' })[0].Delete()
    $e=$zip.CreateEntry('YeManCC/YeManCC.exe');$w=[IO.StreamWriter]::new($e.Open())
    try {$w.Write('steam-wake-round25-original-steam-binding')} finally {$w.Dispose()}
  } finally {$zip.Dispose()}
  Reject { Assert-SteamSleepArchiveExport $f.project $f.zip $f.exe } 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'standalone external exporter checks current source build and ZIP identity' {param($f)
  $b=Capture $f; Stage $f $b
  $r=(& (Join-Path $PSScriptRoot 'verify-steam-sleep-export.ps1') -ProjectRoot $f.project -WorkspaceRoot $f.work -NativeExe $f.exe -Zip $f.zip | Out-String) | ConvertFrom-Json
  if($r.buildId-ne$b.buildId -or $r.verification-ne'source-build-and-artifact-identity') {throw 'Standalone lost source/build binding'}
}
Case 'same-version native from another build is rejected by standalone exporter' {param($f)
  $b=Capture $f
  $other=Join-Path $f.work 'other.exe';Text $other ($marker+' '+$policy+' wakeReadiness steamSessionRetry changed')
  Reject { & (Join-Path $PSScriptRoot 'verify-steam-sleep-export.ps1') -ProjectRoot $f.project -WorkspaceRoot $f.work -NativeExe $other } 'EXPORT_STEAM_SLEEP_CANDIDATE_NOT_CURRENT_BUILD'
}
Case 'external exporter cannot accept source edits made after build' {param($f)
  $b=Capture $f
  Text (Join-Path $f.project 'native/sleep_steam_capture.h') 'new-source'
  Reject { & (Join-Path $PSScriptRoot 'verify-steam-sleep-export.ps1') -ProjectRoot $f.project -WorkspaceRoot $f.work -NativeExe $f.exe } 'EXPORT_SOURCE_CHANGED'
}
Case 'missing wake readiness diagnostic is rejected' {param($f)
  Text $f.exe ($marker+' '+$policy+' steamSessionRetry')
  Reject { Capture $f } 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
# Inert R41 fixtures ensure an R40-only receipt cannot claim the new callback layer.
function R41Fixture($f) {
  $r41='steam-wake-round41-native-focus-feedback'
  Text (Join-Path $f.project 'native/sleep_recovery_auto.h') ('"'+$r41+'" "wakeReadiness"')
  Text (Join-Path $f.project 'native/sleep_steam_recovery.h') ('"'+$r41+'" "'+$policy+'" "steamSessionRetry"')
  foreach($name in @('steam_game_link_runtime.h','steam_game_link_state.h','steam_live_cdp.h','steam_native_focus_feedback.h')) { Text (Join-Path $f.project ('native/'+$name)) 'inert-R41' }
  Text $f.exe ($r41+' '+$policy+' wakeReadiness steamSessionRetry steam-running-link-v1 steam-native-focus-feedback-v1')
}
Case 'R41 requires both original running layer and separate native focus feedback identities' {param($f)
  R41Fixture $f
  $r=Assert-SteamSleepNativeExport $f.project $f.exe
  if($r.fieldAcceptance -or 'steam-running-link-v1' -notin $r.requiredMarkers -or 'steam-native-focus-feedback-v1' -notin $r.requiredMarkers) {throw 'Missing R41 identity or false field acceptance'}
}
Case 'R41 rejects artifact with no native feedback identity' {param($f)
  R41Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-native-focus-feedback-v1','inert-old-callback-layer'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'R41 rejects artifact with no original running link identity' {param($f)
  R41Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-running-link-v1','inert-missing-link-layer'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'R41 missing native focus helper source is rejected' {param($f)
  R41Fixture $f
  # Delete one explicitly named inert file inside the isolated fixture only.
  Remove-Item -LiteralPath (Join-Path $f.project 'native/steam_native_focus_feedback.h')
  Reject {Get-SteamSleepExportContract $f.project} 'EXPORT_STEAM_SLEEP_SOURCE_MISSING'
}
# R42 fixtures ensure the transport contact export marker is distinct from R41.
function R42Fixture($f) {
  $r42='steam-wake-round42-transport-contact-export'
  Text (Join-Path $f.project 'native/sleep_recovery_auto.h') ('"'+$r42+'" "wakeReadiness"')
  Text (Join-Path $f.project 'native/sleep_steam_recovery.h') ('"'+$r42+'" "'+$policy+'" "steamSessionRetry"')
  foreach($name in @('steam_game_link_runtime.h','steam_game_link_state.h','steam_live_cdp.h','steam_native_focus_feedback.h')) { Text (Join-Path $f.project ('native/'+$name)) 'inert-R42' }
  Text $f.exe ($r42+' '+$policy+' wakeReadiness steamSessionRetry steam-running-link-v1 steam-native-focus-feedback-v1')
}
Case 'R42 requires transport contact export plus existing running/focus identities' {param($f)
  R42Fixture $f
  $r=Assert-SteamSleepNativeExport $f.project $f.exe
  if($r.fieldAcceptance -or 'steam-running-link-v1' -notin $r.requiredMarkers -or 'steam-native-focus-feedback-v1' -notin $r.requiredMarkers -or $r.implementation -ne 'steam-wake-round42-transport-contact-export') {throw 'Missing R42 transport contact identity or false field acceptance'}
}
Case 'R42 rejects artifact without transport contact marker' {param($f)
  R42Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-wake-round42-transport-contact-export','steam-wake-round41-native-focus-feedback'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
# R43 fixtures ensure the readout-preservation fix has a distinct export identity.
function R43Fixture($f) {
  $r43='steam-wake-round43-transport-contact-readout'
  Text (Join-Path $f.project 'native/sleep_recovery_auto.h') ('"'+$r43+'" "wakeReadiness"')
  Text (Join-Path $f.project 'native/sleep_steam_recovery.h') ('"'+$r43+'" "'+$policy+'" "steamSessionRetry"')
  foreach($name in @('steam_game_link_runtime.h','steam_game_link_state.h','steam_live_cdp.h','steam_native_focus_feedback.h')) { Text (Join-Path $f.project ('native/'+$name)) 'inert-R43' }
  Text $f.exe ($r43+' '+$policy+' wakeReadiness steamSessionRetry steam-running-link-v1 steam-native-focus-feedback-v1')
}
Case 'R43 requires readout-preservation export identity' {param($f)
  R43Fixture $f
  $r=Assert-SteamSleepNativeExport $f.project $f.exe
  if($r.fieldAcceptance -or $r.implementation -ne 'steam-wake-round43-transport-contact-readout' -or 'steam-running-link-v1' -notin $r.requiredMarkers -or 'steam-native-focus-feedback-v1' -notin $r.requiredMarkers) {throw 'Missing R43 readout identity or false field acceptance'}
}
Case 'R43 rejects artifact with old transport identity' {param($f)
  R43Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-wake-round43-transport-contact-readout','steam-wake-round42-transport-contact-export'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}

# R44 fixtures ensure the GameSessions AppLifetime read-only identity is exported.
function R44Fixture($f) {
  $r44='steam-wake-round44-transport-contact-readout'
  Text (Join-Path $f.project 'native/sleep_recovery_auto.h') ('"'+$r44+'" "wakeReadiness"')
  Text (Join-Path $f.project 'native/sleep_steam_recovery.h') ('"'+$r44+'" "'+$policy+'" "steamSessionRetry"')
  foreach($name in @('steam_game_link_runtime.h','steam_game_link_state.h','steam_live_cdp.h','steam_native_focus_feedback.h','steam_app_lifetime_feedback.h')) { Text (Join-Path $f.project ('native/'+$name)) 'inert-R44' }
  Text $f.exe ($r44+' '+$policy+' wakeReadiness steamSessionRetry steam-running-link-v1 steam-native-focus-feedback-v1 steam-app-lifetime-feedback-v1')
}
Case 'R44 requires AppLifetime read-only identity' {param($f)
  R44Fixture $f
  $r=Assert-SteamSleepNativeExport $f.project $f.exe
  if($r.fieldAcceptance -or $r.implementation -ne 'steam-wake-round44-transport-contact-readout' -or 'steam-running-link-v1' -notin $r.requiredMarkers -or 'steam-native-focus-feedback-v1' -notin $r.requiredMarkers -or 'steam-app-lifetime-feedback-v1' -notin $r.requiredMarkers) {throw 'Missing R44 AppLifetime identity or false field acceptance'}
}
Case 'R44 rejects artifact without AppLifetime identity' {param($f)
  R44Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-app-lifetime-feedback-v1','inert-missing-app-lifetime'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}

# R45 fixtures require the identity-contact field in addition to all R44 read-only identities.
function R45Fixture($f) {
  $r45='steam-wake-round45-identity-contact-layer'
  Text (Join-Path $f.project 'native/sleep_recovery_auto.h') ('"'+$r45+'" "wakeReadiness"')
  Text (Join-Path $f.project 'native/sleep_steam_recovery.h') ('"'+$r45+'" "'+$policy+'" "steamSessionRetry" "contactConnected"')
  foreach($name in @('steam_game_link_runtime.h','steam_game_link_state.h','steam_live_cdp.h','steam_native_focus_feedback.h','steam_app_lifetime_feedback.h')) { Text (Join-Path $f.project ('native/'+$name)) 'inert-R45 contactConnected' }
  Text $f.exe ($r45+' '+$policy+' wakeReadiness steamSessionRetry steam-running-link-v1 steam-native-focus-feedback-v1 steam-app-lifetime-feedback-v1 contactConnected')
}
Case 'R45 requires identity contact layer' {param($f)
  R45Fixture $f
  $r=Assert-SteamSleepNativeExport $f.project $f.exe
  if($r.fieldAcceptance -or $r.implementation -ne 'steam-wake-round45-identity-contact-layer' -or 'contactConnected' -notin $r.requiredMarkers -or 'steam-app-lifetime-feedback-v1' -notin $r.requiredMarkers) {throw 'Missing R45 identity contact or false field acceptance'}
}
Case 'R45 rejects artifact without identity contact marker' {param($f)
  R45Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('contactConnected','inert-missing-contact'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}

# R47 fixtures require the contact proof to be distinct from the read-only
# contact status and the bounded Valve rescope failure marker.
function R47Fixture($f) {
  $r47='steam-wake-round47-contact-rescope'
  Text (Join-Path $f.project 'native/sleep_recovery_auto.h') ('"'+$r47+'" "wakeReadiness"')
  Text (Join-Path $f.project 'native/sleep_steam_recovery.h') ('"'+$r47+'" "'+$policy+'" "steamSessionRetry" "contactConnected" "contactRepairable"')
  $live='contactRecovery contactGlobalScopeSafe steam-contact-rescope-not-confirmed steam-contact-global-running-unsafe'
  Text (Join-Path $f.project 'native/steam_live.js') $live
  Text (Join-Path $f.project 'native/steam_live_script.h') ('inline constexpr const char kScript[] = R"YMSTEAM('+$live+')YMSTEAM";'+"`n"+'inline constexpr const char kUiModeScript[] = R"YMSTEAM(mode)YMSTEAM";')
  foreach($name in @('steam_game_link_runtime.h','steam_game_link_state.h','steam_live_cdp.h','steam_native_focus_feedback.h','steam_app_lifetime_feedback.h')) { Text (Join-Path $f.project ('native/'+$name)) 'inert-R47 contactConnected' }
  Text $f.exe ($r47+' '+$policy+' wakeReadiness steamSessionRetry steam-running-link-v1 steam-native-focus-feedback-v1 steam-app-lifetime-feedback-v1 contactConnected contactRepairable steam-contact-rescope-not-confirmed steam-contact-global-running-unsafe')
}
Case 'R47 requires bounded contact rescope identity' {param($f)
  R47Fixture $f
  $r=Assert-SteamSleepNativeExport $f.project $f.exe
  if($r.fieldAcceptance -or $r.implementation -ne 'steam-wake-round47-contact-rescope' -or 'contactConnected' -notin $r.requiredMarkers -or 'contactRepairable' -notin $r.requiredMarkers -or 'steam-contact-rescope-not-confirmed' -notin $r.requiredMarkers -or 'steam-contact-global-running-unsafe' -notin $r.requiredMarkers) {throw 'Missing R47 contact rescope identity or false field acceptance'}
}
Case 'R47 rejects artifact without contact rescope marker' {param($f)
  R47Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-contact-rescope-not-confirmed','inert-missing-contact-rescope'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'R47 requires contact safety in the actual JS even when native markers exist' {param($f)
  R47Fixture $f
  Text (Join-Path $f.project 'native/sleep_steam_recovery.h') (([IO.File]::ReadAllText((Join-Path $f.project 'native/sleep_steam_recovery.h')))+' steam-contact-global-running-unsafe')
  Text (Join-Path $f.project 'native/steam_live.js') 'contactRecovery contactGlobalScopeSafe steam-contact-rescope-not-confirmed'
  Reject {Get-SteamSleepExportContract $f.project} 'EXPORT_STEAM_SLEEP_CONTACT_RESCOPE_MISSING'
}
Case 'R47 rejects a stale embedded script with all expected marker strings' {param($f)
  R47Fixture $f
  Text (Join-Path $f.project 'native/steam_live.js') (([IO.File]::ReadAllText((Join-Path $f.project 'native/steam_live.js')))+' changed-source')
  Reject {Get-SteamSleepExportContract $f.project} 'EXPORT_STEAM_SLEEP_SCRIPT_EMBED_MISMATCH'
}
Case 'R47 receipt hashes include both JS source and generated script' {param($f)
  R47Fixture $f
  $contract=Get-SteamSleepExportContract $f.project
  foreach($name in @('steam_live.js','steam_live_script.h')) {
    if($contract.sourceSha256[$name]-ne(Get-FileHash -LiteralPath (Join-Path $f.project ('native/'+$name)) -Algorithm SHA256).Hash.ToLowerInvariant()) {throw 'Missing JS source identity'}
  }
}
# R48 keeps R47 recovery policy and adds local observer diagnostic identity only.
function R48Fixture($f) {
  R47Fixture $f
  $r48='steam-wake-round48-lifetime-observer-diagnostics'
  foreach($name in @('sleep_recovery_auto.h','sleep_steam_recovery.h')) {
    $path=Join-Path $f.project ('native/'+$name)
    Text $path (([IO.File]::ReadAllText($path)).Replace('steam-wake-round47-contact-rescope',$r48))
  }
  $live=([IO.File]::ReadAllText((Join-Path $f.project 'native/steam_live.js')))+' observerInstanceId observerCreatedAtUnixMs diagnosticCountersAvailable rawCallbacksEntered accountUnavailableCallbacks accountMismatchCallbacks nativeSubscriptionHealth'
  Text (Join-Path $f.project 'native/steam_live.js') $live
  Text (Join-Path $f.project 'native/steam_live_script.h') ('inline constexpr const char kScript[] = R"YMSTEAM('+$live+')YMSTEAM";' + "`n" + 'inline constexpr const char kUiModeScript[] = R"YMSTEAM(mode)YMSTEAM";')
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-wake-round47-contact-rescope',$r48)+' observerInstanceId observerCreatedAtUnixMs diagnosticCountersAvailable rawCallbacksEntered accountUnavailableCallbacks accountMismatchCallbacks nativeSubscriptionHealth')
}
Case 'R48 requires local observer diagnostics without field acceptance' {param($f)
  R48Fixture $f
  $r=Assert-SteamSleepNativeExport $f.project $f.exe
  if($r.fieldAcceptance -or $r.implementation-ne'steam-wake-round48-lifetime-observer-diagnostics' -or 'rawCallbacksEntered'-notin$r.requiredMarkers -or 'nativeSubscriptionHealth'-notin$r.requiredMarkers -or 'contactRepairable'-notin$r.requiredMarkers) {throw 'R48 diagnostics or inherited contact contract missing'}
}
Case 'R48 rejects a previous R47 artifact' {param($f)
  R48Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-wake-round48-lifetime-observer-diagnostics','steam-wake-round47-contact-rescope'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'R48 rejects a missing callback-drop diagnostic in actual source' {param($f)
  R48Fixture $f
  $live=([IO.File]::ReadAllText((Join-Path $f.project 'native/steam_live.js'))).Replace('rawCallbacksEntered','missing-callback-counter')
  Text (Join-Path $f.project 'native/steam_live.js') $live
  Text (Join-Path $f.project 'native/steam_live_script.h') ('inline constexpr const char kScript[] = R"YMSTEAM('+$live+')YMSTEAM";' + "`n" + 'inline constexpr const char kUiModeScript[] = R"YMSTEAM(mode)YMSTEAM";')
  Reject {Get-SteamSleepExportContract $f.project} 'EXPORT_STEAM_SLEEP_LIFETIME_DIAGNOSTICS_MISSING'
}
Case 'R48 rejects an artifact with no observer identity' {param($f)
  R48Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('observerInstanceId','missing-observer-identity'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
# R49 preserves all prior identities and observes a still-unseen original window without another action.
function R49Fixture($f) {
  R48Fixture $f
  foreach($name in @('sleep_recovery_auto.h','sleep_steam_recovery.h')) {
    $path=Join-Path $f.project ('native/'+$name)
    Text $path (([IO.File]::ReadAllText($path)).Replace('steam-wake-round48-lifetime-observer-diagnostics','steam-wake-round49-dispatched-window-observation')+' sameOriginalWindowReadbackTransaction pendingOriginalWindowReadback nativeWindowReadbackWait waitingInitialOriginalWindow nativeReturn')
  }
  $live=([IO.File]::ReadAllText((Join-Path $f.project 'native/steam_live.js')))+' nativeReturn'
  Text (Join-Path $f.project 'native/steam_live.js') $live
  Text (Join-Path $f.project 'native/steam_live_script.h') ('inline constexpr const char kScript[] = R"YMSTEAM('+$live+')YMSTEAM";' + "`n" + 'inline constexpr const char kUiModeScript[] = R"YMSTEAM(mode)YMSTEAM";')
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-wake-round48-lifetime-observer-diagnostics','steam-wake-round49-dispatched-window-observation')+' nativeWindowReadbackWait waitingInitialOriginalWindow nativeReturn')
}
Case 'R49 requires window readback and native return evidence, preserving prior observer/contact contracts' {param($f)
  R49Fixture $f
  $r=Assert-SteamSleepNativeExport $f.project $f.exe
  if($r.fieldAcceptance -or $r.implementation-ne'steam-wake-round49-dispatched-window-observation' -or 'waitingInitialOriginalWindow'-notin$r.requiredMarkers -or 'nativeReturn'-notin$r.requiredMarkers -or 'nativeSubscriptionHealth'-notin$r.requiredMarkers -or 'contactRepairable'-notin$r.requiredMarkers) {throw 'R49 or inherited contract missing'}
}
Case 'R49 rejects a previous R48 image' {param($f)
  R49Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-wake-round49-dispatched-window-observation','steam-wake-round48-lifetime-observer-diagnostics'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'R49 marker alone cannot replace missing readback evidence' {param($f)
  R49Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('nativeWindowReadbackWait','old-no-readback'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
# R50 diagnostic must be identifiable, private, and retain all R49 recovery guards.
function R50Fixture($f) {
  R49Fixture $f
  foreach($name in @('sleep_recovery_auto.h','sleep_steam_recovery.h')) {
    $path=Join-Path $f.project ('native/'+$name)
    Text $path (([IO.File]::ReadAllText($path)).Replace('steam-wake-round49-dispatched-window-observation','steam-wake-round50-native-record-diagnostic')+' prefetchNativeRecordIdentity nativeRecordDiagnostic native-record-pre-capture-ui-bound')
  }
  Text (Join-Path $f.project 'native/steam_native_record_diagnostic.h') 'steam-native-cuser-record-diagnostic diagnostic-only-no-admission recordState'
  Text (Join-Path $f.project 'native/steam_native_record_identity.h') 'reply.erase(it)'
  Text (Join-Path $f.project 'native/steam_native_record_win32.h') 'read-only-fixed-profile'
  Text (Join-Path $f.project 'native/steam_native_return_evidence.h') 'bounded-only-fixed-native-return'
  Text (Join-Path $f.project 'native/main.cpp') 'steamreturnevidence::bounded(result) steam-native-return-evidence-v1'
  $live=([IO.File]::ReadAllText((Join-Path $f.project 'native/steam_live.js')))+' settledNativeReturn'
  Text (Join-Path $f.project 'native/steam_live.js') $live
  Text (Join-Path $f.project 'native/steam_live_script.h') ('inline constexpr const char kScript[] = R"YMSTEAM('+$live+')YMSTEAM";' + "`n" + 'inline constexpr const char kUiModeScript[] = R"YMSTEAM(mode)YMSTEAM";')
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-wake-round49-dispatched-window-observation','steam-wake-round50-native-record-diagnostic')+' nativeRecordDiagnostic steam-native-cuser-record-diagnostic recordState native-record-pre-capture-ui-bound settledNativeReturn steam-native-return-evidence-v1')
}
Case 'R50 requires private record diagnostic and inherits R49 observation and original identity contracts' {param($f)
  R50Fixture $f
  $r=Assert-SteamSleepNativeExport $f.project $f.exe
  if($r.fieldAcceptance -or $r.implementation-ne'steam-wake-round50-native-record-diagnostic' -or 'nativeRecordDiagnostic'-notin$r.requiredMarkers -or 'waitingInitialOriginalWindow'-notin$r.requiredMarkers -or $r.sourceSha256.Count-lt13) {throw 'R50 inherited contract or source identity missing'}
}
Case 'R50 rejects a previous R49 image' {param($f)
  R50Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-wake-round50-native-record-diagnostic','steam-wake-round49-dispatched-window-observation'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'R50 rejects a missing native diagnostic header' {param($f)
  R50Fixture $f
  Remove-Item -LiteralPath (Join-Path $f.project 'native/steam_native_record_win32.h')
  Reject {Get-SteamSleepExportContract $f.project} 'EXPORT_STEAM_SLEEP_NATIVE_RECORD_SOURCE_MISSING'
}
Case 'R50 rejects source without private reply stripping' {param($f)
  R50Fixture $f
  Text (Join-Path $f.project 'native/steam_native_record_identity.h') 'missing-private-strip'
  Reject {Get-SteamSleepExportContract $f.project} 'EXPORT_STEAM_SLEEP_NATIVE_RECORD_CONTRACT_MISSING'
}
Case 'R50 marker alone cannot replace missing actual native record evidence' {param($f)
  R50Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('nativeRecordDiagnostic','missing-record-evidence'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
Case 'R50 rejects host source missing bounded postflight evidence' {param($f)
  R50Fixture $f
  Text (Join-Path $f.project 'native/main.cpp') 'missing-host-return-preservation'
  Reject {Get-SteamSleepExportContract $f.project} 'EXPORT_STEAM_SLEEP_NATIVE_RETURN_POSTFLIGHT_MISSING'
}
Case 'R50 rejects an earlier same-round artifact without host postflight retention' {param($f)
  R50Fixture $f
  Text $f.exe (([IO.File]::ReadAllText($f.exe)).Replace('steam-native-return-evidence-v1','missing-host-return-preservation'))
  Reject {Assert-SteamSleepNativeExport $f.project $f.exe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH'
}
if ($OldNativeExe) { Case 'actual previous implementation binary is rejected' {param($f) Reject {Assert-SteamSleepNativeExport $RealProjectRoot $OldNativeExe} 'EXPORT_STEAM_SLEEP_VERSION_MISMATCH' } }
if ($NewNativeExe) { Case 'actual current mainline binary passes delivery check' {param($f)
  $r=Assert-SteamSleepNativeExport $RealProjectRoot $NewNativeExe
  if($r.status-ne'passed' -or $r.implementation-ne(Get-SteamSleepExportContract $RealProjectRoot).implementation -or $r.fieldAcceptance) {throw 'Wrong actual delivery result'}
} }
$failed=@($results|Where-Object {-not $_.passed})
$report=[ordered]@{status=$(if($failed.Count){'failed'}else{'passed'});cases=$results.Count;failed=$failed.Count;results=@($results.ToArray());productionApplicationChanged=$false;realReleaseExecuted=$false;fieldAcceptance=$false}
Text (Join-Path $ValidationRoot 'RESULT.json') ($report|ConvertTo-Json -Depth 5)
Write-Output ('STEAM_EXPORT_GUARD '+$report.status+' cases='+$report.cases+' failed='+$report.failed)
$failed|ConvertTo-Json -Depth 4
if($failed.Count){exit 1}
