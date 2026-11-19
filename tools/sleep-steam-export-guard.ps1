# Read-only Steam sleep source/artifact check used by build and export identity.
# This checks delivery only; it never claims controller or Continue acceptance.
function Get-SteamSleepExportContract([string]$ProjectRoot) {
  $owner = Join-Path $ProjectRoot 'native/sleep_recovery_auto.h'
  $recovery = Join-Path $ProjectRoot 'native/sleep_steam_recovery.h'
  if (-not [IO.File]::Exists($owner) -and -not [IO.File]::Exists($recovery)) { return $null }
  $files = @('sleep_recovery_auto.h','sleep_steam_recovery.h','sleep_steam_capture.h',
    'sleep_steam_tracking.h','sleep_steam_input_context.h','sleep_steam_game_probe.h','sleep_steam_settle.h')
  if (([IO.File]::Exists($owner) -and [IO.File]::ReadAllText($owner).Contains('#include "sleep_steam_command_attempt.h"')) -or
      ([IO.File]::Exists($recovery) -and [IO.File]::ReadAllText($recovery).Contains('#include "sleep_steam_command_attempt.h"'))) {
    $files += @('sleep_steam_command_attempt.h','steam_command_disposition.h')
  }
  $linkRuntime=Join-Path $ProjectRoot 'native/steam_game_link_runtime.h'
  if([IO.File]::Exists($linkRuntime)) { $files += @('steam_game_link_runtime.h','steam_game_link_state.h','steam_live_cdp.h') }
  $feedbackHeader=Join-Path $ProjectRoot 'native/steam_native_focus_feedback.h'
  if([IO.File]::Exists($feedbackHeader)) { $files += 'steam_native_focus_feedback.h' }
  $appLifetimeHeader=Join-Path $ProjectRoot 'native/steam_app_lifetime_feedback.h'
  if([IO.File]::Exists($appLifetimeHeader)) { $files += 'steam_app_lifetime_feedback.h' }
  foreach($name in @('steam_native_record_diagnostic.h','steam_native_record_identity.h','steam_native_record_win32.h','steam_native_return_evidence.h')) { if([IO.File]::Exists((Join-Path $ProjectRoot ('native/'+$name)))) { $files += $name } }
  if([IO.File]::Exists((Join-Path $ProjectRoot 'native/steam_native_return_evidence.h'))) { $files += 'main.cpp' }
  # The original-instance engine is a separate implementation from the older
  # round50 contact coordinator. Bind its host fixes and revision as well, so a
  # stale round50 EXE cannot pass delivery merely by sharing old marker strings.
  $originalEngine = Join-Path $ProjectRoot 'native/steam_original_recovery_engine.cpp'
  $originalRevision = $null
  $originalHostPolicy = $null
  if ([IO.File]::Exists($originalEngine)) {
    $files += @('main.cpp','steam_original_recovery_engine.cpp','steam_original_recovery_engine.h',
      'steam_original_recovery_runtime.h','steam_original_recovery_dual_route.h',
      'steam_original_recovery_policy.h','steam_original_recovery_wake_watch.h',
      'steam_original_recovery_config.h','steam_original_recovery_storage.h')
    $engineText = [IO.File]::ReadAllText($originalEngine)
    $revisions = @([regex]::Matches($engineText, 'constexpr const char\* kEngineRevision="(dual-route-[a-z0-9-]+)";') |
      ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    if ($revisions.Count -ne 1) { throw 'EXPORT_STEAM_ORIGINAL_REVISION_MISSING' }
    $originalRevision = $revisions[0]
    $originalHostPolicy = 'ymcc-original-game-identity-v2'
    $hostText = [IO.File]::ReadAllText((Join-Path $ProjectRoot 'native/main.cpp'))
    foreach ($anchor in @('g_gameValveTarget.identityGeneration != snapshot.identityGeneration',
      '!snapshot.identityGeneration', 'g_gameValveTarget.identityGeneration = g_gameValveTarget.generation')) {
      if (-not $hostText.Contains($anchor)) { throw 'EXPORT_STEAM_ORIGINAL_HOST_IDENTITY_MISSING' }
    }
    # Bind the policy to the actual returned target JSON, not its construction syntax.
    # The initializer and direct-object forms must keep the same serialized contract.
    $cppTokenPattern = '(?<raw>\b(?:u8|u|U|L)?R"(?<delimiter>[^\s()\\]{0,16})\([\s\S]*?\)\k<delimiter>")|(?<literal>"(?:\\[\s\S]|[^"\\])*"|''(?:\\[\s\S]|[^''\\])*'')|(?<comment>/\*[\s\S]*?\*/|//[^\r\n]*)'
    $maskedHost = [regex]::Replace($hostText, $cppTokenPattern, [Text.RegularExpressions.MatchEvaluator]{ param($m) [string]::new([char]32, $m.Length) })
    $serializers = @([regex]::Matches($maskedHost, '(?m)^static json nativeGameTargetJson\(const NativeGameValveSnapshot& target\)\s*\{'))
    if ($serializers.Count -ne 1) { throw 'EXPORT_STEAM_ORIGINAL_HOST_IDENTITY_MISSING' }
    $open = $serializers[0].Index + $serializers[0].Length - 1
    $cursor = $open + 1; $depth = 1
    while ($cursor -lt $maskedHost.Length -and $depth -gt 0) {
      if ($maskedHost[$cursor] -eq '{') { $depth++ } elseif ($maskedHost[$cursor] -eq '}') { $depth-- }
      $cursor++
    }
    if ($depth -ne 0) { throw 'EXPORT_STEAM_ORIGINAL_HOST_IDENTITY_MISSING' }
    $serializerBody = $hostText.Substring($open + 1, $cursor - $open - 2)
    $serializerCode = [regex]::Replace($serializerBody, $cppTokenPattern, [Text.RegularExpressions.MatchEvaluator]{ param($m) if ($m.Groups['literal'].Success) { $m.Value } else { [string]::new([char]32, $m.Length) } })
    $legacyReturn = [regex]::Match($serializerCode, '(?s)\breturn\s*\{(?<object>.*)\};\s*$')
    $legacyIdentity = $legacyReturn.Success -and [regex]::IsMatch($legacyReturn.Groups['object'].Value, '(?m)^\s*\{"identityPolicy",\s*"ymcc-original-game-identity-v2"\},?\s*$')
    $directIdentity = [regex]::IsMatch($serializerCode, '(?m)^\s*json result = json::object\(\);\s*$') -and
      [regex]::IsMatch($serializerCode, '(?m)^\s*result\["identityPolicy"\] = "ymcc-original-game-identity-v2";\s*$') -and
      [regex]::IsMatch($serializerCode, '(?s)\breturn result;\s*$')
    if ([regex]::Matches($serializerCode, '"identityPolicy"').Count -ne 1 -or (-not $legacyIdentity -and -not $directIdentity)) {
      throw 'EXPORT_STEAM_ORIGINAL_HOST_IDENTITY_MISSING'
    }
    $trackerText = [IO.File]::ReadAllText($linkRuntime)
    $trackerCode = [regex]::Replace($trackerText, $cppTokenPattern, [Text.RegularExpressions.MatchEvaluator]{ param($m) [string]::new([char]32, $m.Length) })
    # Check the effective lifecycle scope and its actual ongoing call. The
    # startup/current-list request is independent of this original-instance read.
    $lifetimeBodies=@{};$groupTracking=$false;$completionTracking=$false
    foreach($entry in @(
      @{name='tracking';pattern='(?m)^static\s+TrackerRead\s+tracking\s*\(\s*const\s+Client\s*&\s*client\s*,\s*const\s+Game\s*&\s*game\s*,\s*TrackingScope\s+scope\s*=\s*TrackingScope::ProcessLifetime\s*(?<groupParameters>,\s*ymcc::steamgamefacts::TrackerGroupCapturePolicy\s+groupPolicy\s*=\s*ymcc::steamgamefacts::TrackerGroupCapturePolicy::None\s*,\s*const\s+ymcc::steamgamefacts::TrackerOwnerCurrent\s*&\s*requestCurrent\s*=\s*\{\s*\}\s*(?<completionParameter>,\s*NativeSteamTrackerFreshCompletion\s*\*\s*completion\s*=\s*nullptr\s*)?)?\)\s*\{'},
      @{name='run';pattern='(?m)^static\s+void\s+run\s*\([^;{}]*\)\s*\{'})) {
      $definitions=@([regex]::Matches($trackerCode,$entry.pattern))
      if($definitions.Count -ne 1){throw 'EXPORT_STEAM_ORIGINAL_PROCESS_LIFETIME_MISSING'}
      if($entry.name -ceq 'tracking'){$groupTracking=$definitions[0].Groups['groupParameters'].Success;$completionTracking=$definitions[0].Groups['completionParameter'].Success}
      $start=$definitions[0].Index+$definitions[0].Length-1;$end=$start+1;$level=1
      while($end -lt $trackerCode.Length -and $level -gt 0){
        if($trackerCode[$end] -eq '{'){$level++}elseif($trackerCode[$end] -eq '}'){$level--};$end++
      }
      if($level -ne 0){throw 'EXPORT_STEAM_ORIGINAL_PROCESS_LIFETIME_MISSING'}
      $lifetimeBodies[$entry.name]=$trackerCode.Substring($start+1,$end-$start-2)
    }
    $legacyLifetime=(-not $completionTracking) -and [regex]::IsMatch($lifetimeBodies.tracking,'\btrackedProcessInventory\s*\(\s*lines\s*,\s*scope\s*\)') -and
      [regex]::IsMatch($lifetimeBodies.tracking,'(?s)\btrack\s*\([^;]*,\s*scope\s*\)')
    # The shared facade reads both inventories. The same translated scope must
    # select the inventory and binding; the ongoing caller still uses lifetime.
    # Only the exact typed optional out may extend the group facade.
    $freshReadPattern=if($completionTracking){
      '\bconst\s+auto\s+fresh\s*=\s*nativeSteamGameTrackerReadFresh\s*\(\s*client\s*,\s*game\s*,\s*logScope\s*,\s*&\s*freshDiagnostic\s*,\s*groupPolicy\s*,\s*requestCurrent\s*,\s*completion\s*\)\s*;'
    }elseif($groupTracking){
      '\bconst\s+auto\s+fresh\s*=\s*nativeSteamGameTrackerReadFresh\s*\(\s*client\s*,\s*game\s*,\s*logScope\s*,\s*&\s*freshDiagnostic\s*,\s*groupPolicy\s*,\s*requestCurrent\s*\)\s*;'
    }else{
      '\bconst\s+auto\s+fresh\s*=\s*nativeSteamGameTrackerReadFresh\s*\(\s*client\s*,\s*game\s*,\s*logScope\s*(?:,\s*&\s*freshDiagnostic\s*)?\)\s*;'
    }
    $sharedLifetime=$true
    foreach($pattern in @(
      '\bconst\s+auto\s+logScope\s*=\s*scope\s*==\s*TrackingScope::RunningListAndProcess\s*\?\s*ymcc::steamgamefacts::TrackerLogScope::RunningListAndProcess\s*:\s*ymcc::steamgamefacts::TrackerLogScope::ProcessLifetime\s*;',
      $freshReadPattern,
      '\bconst\s+auto\s*&\s*parsed\s*=\s*\*fresh\.tracker->parsedSource\s*;',
      '\bconst\s+auto\s*&\s*selected\s*=\s*logScope\s*==\s*ymcc::steamgamefacts::TrackerLogScope::RunningListAndProcess\s*\?\s*parsed\.runningList\s*:\s*parsed\.processLifetime\s*;',
      '\binventory\.complete\s*=\s*selected\.inventory\.complete\s*;',
      '\bfor\s*\(\s*const\s+auto\s*&\s*member\s*:\s*selected\.inventory\.members\s*\)\s*inventory\.rows\.push_back\s*\(\s*\{\s*member\.appid\s*,\s*member\.pid\s*,',
      '\bresult\.binding\.status\s*=\s*selected\.binding\.status\s*;',
      '\bresult\.binding\.appid\s*=\s*selected\.binding\.appid\s*;',
      '\bresult\.gameId\s*=\s*selected\.binding\.gameIdRaw\s*;',
      '\bresult\.primary\s*=\s*SleepSteamRecovery::nativePrimaryJoin\s*\(')) {
      if(-not [regex]::IsMatch($lifetimeBodies.tracking,$pattern)){$sharedLifetime=$false;break}
    }
    if(((-not $legacyLifetime) -and (-not $sharedLifetime)) -or
       -not [regex]::IsMatch($lifetimeBodies.run,'\btracking\s*\(\s*client\s*,\s*game\s*(?:,\s*TrackingScope::ProcessLifetime\s*(?:,\s*ymcc::steamgamefacts::TrackerGroupCapturePolicy::Capture\s*)?)?\)')) {
      throw 'EXPORT_STEAM_ORIGINAL_PROCESS_LIFETIME_MISSING'
    }
    $files = @($files | Select-Object -Unique)
  }
  $hashes = [ordered]@{}
  foreach ($name in $files) {
    $path = Join-Path $ProjectRoot ('native/' + $name)
    if (-not [IO.File]::Exists($path)) { throw "EXPORT_STEAM_SLEEP_SOURCE_MISSING: $name" }
    $hashes[$name] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  $implementations = @()
  foreach ($path in @($owner, $recovery)) {
    $text = [IO.File]::ReadAllText($path)
    $markers = @([regex]::Matches($text, '"(steam-wake-round[0-9]+-[a-z0-9-]+)"') |
      ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    if ($markers.Count -ne 1) { throw "EXPORT_STEAM_SLEEP_SOURCE_VERSION_INVALID: $path" }
    $implementations += $markers[0]
  }
  if ($implementations[0] -ne $implementations[1]) { throw 'EXPORT_STEAM_SLEEP_SOURCE_VERSION_MISMATCH' }
  $recoveryText = [IO.File]::ReadAllText($recovery)
  $policies = @([regex]::Matches($recoveryText, '"(ymcc-steam-exact-instance-join-v[0-9]+)"') |
    ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
  if ($policies.Count -ne 1) { throw 'EXPORT_STEAM_SLEEP_BINDING_POLICY_MISSING' }
  $required = @($implementations[0], $policies[0])
  if ($originalRevision) { $required += @($originalRevision, $originalHostPolicy) }
  if($implementations[0] -in @('steam-wake-round40-persistent-game-link','steam-wake-round41-native-focus-feedback','steam-wake-round42-transport-contact-export','steam-wake-round43-transport-contact-readout','steam-wake-round44-transport-contact-readout','steam-wake-round45-identity-contact-layer','steam-wake-round47-contact-rescope','steam-wake-round48-lifetime-observer-diagnostics','steam-wake-round49-dispatched-window-observation','steam-wake-round50-native-record-diagnostic')) {
    if(-not [IO.File]::Exists($linkRuntime)) { throw 'EXPORT_STEAM_SLEEP_SOURCE_MISSING: steam_game_link_runtime.h' }
    $required += 'steam-running-link-v1'
  }
  if($implementations[0] -in @('steam-wake-round41-native-focus-feedback','steam-wake-round42-transport-contact-export','steam-wake-round43-transport-contact-readout','steam-wake-round44-transport-contact-readout')) {
    if(-not [IO.File]::Exists($feedbackHeader)) { throw 'EXPORT_STEAM_SLEEP_SOURCE_MISSING: steam_native_focus_feedback.h' }
    $required += 'steam-native-focus-feedback-v1'
  }
  if($implementations[0] -eq 'steam-wake-round44-transport-contact-readout') {
    if(-not [IO.File]::Exists($appLifetimeHeader)) { throw 'EXPORT_STEAM_SLEEP_SOURCE_MISSING: steam_app_lifetime_feedback.h' }
    $required += 'steam-app-lifetime-feedback-v1'
  }
  if($implementations[0] -eq 'steam-wake-round45-identity-contact-layer') {
    if(-not [IO.File]::Exists($appLifetimeHeader)) { throw 'EXPORT_STEAM_SLEEP_SOURCE_MISSING: steam_app_lifetime_feedback.h' }
    $required += 'steam-app-lifetime-feedback-v1'
    if(-not [IO.File]::ReadAllText($linkRuntime).Contains('contactConnected')) { throw 'EXPORT_STEAM_SLEEP_IDENTITY_CONTACT_MISSING' }
    $required += 'contactConnected'
  }
  if($implementations[0] -in @('steam-wake-round47-contact-rescope','steam-wake-round48-lifetime-observer-diagnostics','steam-wake-round49-dispatched-window-observation','steam-wake-round50-native-record-diagnostic')) {
    if(-not [IO.File]::Exists($appLifetimeHeader)) { throw 'EXPORT_STEAM_SLEEP_SOURCE_MISSING: steam_app_lifetime_feedback.h' }
    $required += 'steam-app-lifetime-feedback-v1'
    if(-not [IO.File]::ReadAllText($linkRuntime).Contains('contactConnected')) { throw 'EXPORT_STEAM_SLEEP_IDENTITY_CONTACT_MISSING' }
    # Verify each contract where it is implemented. Native marker lists alone
    # cannot stand in for the actual JS that is embedded into the EXE.
    if(-not $recoveryText.Contains('contactRepairable')) { throw 'EXPORT_STEAM_SLEEP_CONTACT_RESCOPE_MISSING' }
    foreach($name in @('steam_live.js','steam_live_script.h')) {
      $path=Join-Path $ProjectRoot ('native/'+$name)
      if(-not [IO.File]::Exists($path)) { throw "EXPORT_STEAM_SLEEP_SOURCE_MISSING: $name" }
      $hashes[$name]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $liveText=[IO.File]::ReadAllText((Join-Path $ProjectRoot 'native/steam_live.js')).TrimEnd()
    foreach($marker in @('contactRecovery','contactGlobalScopeSafe','steam-contact-rescope-not-confirmed','steam-contact-global-running-unsafe')) {
      if(-not $liveText.Contains($marker)) { throw 'EXPORT_STEAM_SLEEP_CONTACT_RESCOPE_MISSING' }
    }
    $embedded=[IO.File]::ReadAllText((Join-Path $ProjectRoot 'native/steam_live_script.h'))
    $modeBoundary=$embedded.IndexOf('inline constexpr const char kUiModeScript[]',[StringComparison]::Ordinal)
    if($modeBoundary-lt0) { throw 'EXPORT_STEAM_SLEEP_SCRIPT_EMBED_MISMATCH' }
    $fullScript=$embedded.Substring(0,$modeBoundary)
    $chunks=@([regex]::Matches($fullScript,'(?s)R"YMSTEAM\((.*?)\)YMSTEAM"')|ForEach-Object {$_.Groups[1].Value})
    if($chunks.Count-eq0 -or ($chunks-join'')-cne$liveText) { throw 'EXPORT_STEAM_SLEEP_SCRIPT_EMBED_MISMATCH' }
    $required += @('contactConnected','contactRepairable','steam-contact-rescope-not-confirmed','steam-contact-global-running-unsafe')
  }
  if($implementations[0] -in @('steam-wake-round48-lifetime-observer-diagnostics','steam-wake-round49-dispatched-window-observation','steam-wake-round50-native-record-diagnostic')) {
    # Local observer diagnostics identify callback drops; they are not native health or recovery proof.
    foreach($marker in @('observerInstanceId','observerCreatedAtUnixMs','diagnosticCountersAvailable','rawCallbacksEntered','accountUnavailableCallbacks','accountMismatchCallbacks','nativeSubscriptionHealth')) {
      if(-not $liveText.Contains($marker)) { throw 'EXPORT_STEAM_SLEEP_LIFETIME_DIAGNOSTICS_MISSING' }
      $required += $marker
    }
  }
  if($implementations[0] -in @('steam-wake-round49-dispatched-window-observation','steam-wake-round50-native-record-diagnostic')) {
    $ownerText=[IO.File]::ReadAllText($owner)
    foreach($marker in @('sameOriginalWindowReadbackTransaction','pendingOriginalWindowReadback','nativeWindowReadbackWait','waitingInitialOriginalWindow')) {
      if(-not $ownerText.Contains($marker)) { throw 'EXPORT_STEAM_SLEEP_WINDOW_READBACK_MISSING' }
    }
    if(-not $liveText.Contains('nativeReturn') -or -not $recoveryText.Contains('nativeReturn')) { throw 'EXPORT_STEAM_SLEEP_NATIVE_RETURN_DIAGNOSTIC_MISSING' }
    $required += @('nativeWindowReadbackWait','waitingInitialOriginalWindow','nativeReturn')
  }
  if($implementations[0] -eq 'steam-wake-round50-native-record-diagnostic') {
    foreach($name in @('steam_native_record_diagnostic.h','steam_native_record_identity.h','steam_native_record_win32.h','steam_native_return_evidence.h')) {
      if(-not [IO.File]::Exists((Join-Path $ProjectRoot ('native/'+$name)))) { throw ('EXPORT_STEAM_SLEEP_NATIVE_RECORD_SOURCE_MISSING: '+$name) }
    }
    $mainText=[IO.File]::ReadAllText((Join-Path $ProjectRoot 'native/main.cpp'))
    if(-not $mainText.Contains('steamreturnevidence::bounded(result)') -or -not $mainText.Contains('steam-native-return-evidence-v1')) { throw 'EXPORT_STEAM_SLEEP_NATIVE_RETURN_POSTFLIGHT_MISSING' }
    $recordText=[IO.File]::ReadAllText((Join-Path $ProjectRoot 'native/steam_native_record_diagnostic.h'))
    $identityText=[IO.File]::ReadAllText((Join-Path $ProjectRoot 'native/steam_native_record_identity.h'))
    if(-not $recordText.Contains('diagnostic-only-no-admission') -or -not $identityText.Contains('reply.erase(it)') -or -not $recoveryText.Contains('prefetchNativeRecordIdentity') -or -not $liveText.Contains('settledNativeReturn')) { throw 'EXPORT_STEAM_SLEEP_NATIVE_RECORD_CONTRACT_MISSING' }
    $required += @('nativeRecordDiagnostic','steam-native-cuser-record-diagnostic','recordState','native-record-pre-capture-ui-bound','settledNativeReturn','steam-native-return-evidence-v1')
  }
  foreach ($marker in @('wakeReadiness', 'steamSessionRetry')) {
    if (($recoveryText + [IO.File]::ReadAllText($owner)).Contains('"' + $marker + '"')) { $required += $marker }
  }
  return [ordered]@{implementation=$implementations[0];bindingPolicy=$policies[0];requiredMarkers=$required;sourceSha256=$hashes}
}
function Assert-SteamSleepArtifactBytes([byte[]]$Bytes, $Contract, [string]$Artifact) {
  if ($null -eq $Contract) { return }
  $text = [Text.Encoding]::ASCII.GetString($Bytes)
  foreach ($marker in @($Contract.requiredMarkers)) {
    if (-not $marker -or -not $text.Contains([string]$marker)) {
      throw "EXPORT_STEAM_SLEEP_VERSION_MISMATCH: $Artifact lacks $marker; rebuild and package current mainline before exporting"
    }
  }
}
function Assert-SteamSleepNativeExport([string]$ProjectRoot, [string]$NativeExe) {
  $contract = Get-SteamSleepExportContract $ProjectRoot
  if ($null -eq $contract) { return [ordered]@{status='not-applicable';fieldAcceptance=$false} }
  if (-not [IO.File]::Exists($NativeExe)) { throw "EXPORT_STEAM_SLEEP_ARTIFACT_MISSING: $NativeExe" }
  $bytes = [IO.File]::ReadAllBytes($NativeExe)
  Assert-SteamSleepArtifactBytes $bytes $contract $NativeExe
  $sha = [Security.Cryptography.SHA256]::Create()
  try { $hash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
  finally { $sha.Dispose() }
  return [ordered]@{status='passed';implementation=$contract.implementation;bindingPolicy=$contract.bindingPolicy;
    requiredMarkers=$contract.requiredMarkers;sourceSha256=$contract.sourceSha256;nativeSha256=$hash;fieldAcceptance=$false}
}
function Assert-SteamSleepExportReceipt($Captured, $Expected) {
  if ($Expected.status -ne 'passed') { return }
  if ($null -eq $Captured -or $Captured.status -ne 'passed') { throw 'EXPORT_STEAM_SLEEP_IDENTITY_MISSING: rebuild with current build-workspace.ps1' }
  if ($Captured.implementation -ne $Expected.implementation -or $Captured.bindingPolicy -ne $Expected.bindingPolicy -or
      $Captured.nativeSha256 -ne $Expected.nativeSha256 -or
      ($Captured.requiredMarkers -join '|') -ne ($Expected.requiredMarkers -join '|')) {
    throw 'EXPORT_STEAM_SLEEP_IDENTITY_MISMATCH'
  }
  foreach ($name in $Expected.sourceSha256.Keys) {
    $actual = if ($Captured.sourceSha256 -is [Collections.IDictionary]) { $Captured.sourceSha256[$name] }
              else { $Captured.sourceSha256.PSObject.Properties[$name].Value }
    if ($actual -ne $Expected.sourceSha256[$name]) { throw "EXPORT_STEAM_SLEEP_SOURCE_IDENTITY_MISMATCH: $name" }
  }
}
function Assert-SteamSleepArchiveExport([string]$ProjectRoot, [string]$Zip, [string]$NativeExe) {
  $expected = Assert-SteamSleepNativeExport $ProjectRoot $NativeExe
  if ($expected.status -ne 'passed') { return $expected }
  Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
  $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
  try {
    $entries = @($archive.Entries | Where-Object { $_.FullName.Replace('\','/') -ieq 'YeManCC/YeManCC.exe' })
    if ($entries.Count -ne 1) { throw 'EXPORT_STEAM_SLEEP_ZIP_NATIVE_AMBIGUOUS_OR_MISSING' }
    $stream = $entries[0].Open(); $memory = [IO.MemoryStream]::new()
    try { $stream.CopyTo($memory); $bytes = $memory.ToArray() }
    finally { $stream.Dispose(); $memory.Dispose() }
    Assert-SteamSleepArtifactBytes $bytes $expected $Zip
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    if ($hash -ne $expected.nativeSha256) { throw 'EXPORT_STEAM_SLEEP_ZIP_NATIVE_HASH_MISMATCH' }
    return $expected
  } finally { $archive.Dispose() }
}
