# Source/build/staging/archive binding. All assertions are read-only unless their
# explicit name is Write-*. No installed-product paths or caches are used.
. (Join-Path $PSScriptRoot 'sleep-steam-export-guard.ps1')
. (Join-Path $PSScriptRoot 'gpu-dedicated-memory-export-guard.ps1')
function Get-ExportHash([System.IO.Stream]$Stream) {
  $sha=[Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($Stream))).Replace('-','').ToLowerInvariant() }
  finally { $sha.Dispose() }
}
function Get-ExportFileHash([string]$Path) {
  $stream=[IO.File]::OpenRead($Path)
  try { return Get-ExportHash $stream } finally { $stream.Dispose() }
}
function Get-ExportTextHash([string]$Text) {
  $stream=[IO.MemoryStream]::new([Text.Encoding]::UTF8.GetBytes($Text))
  try { return Get-ExportHash $stream } finally { $stream.Dispose() }
}
function ConvertTo-ExportRelativePath([string]$Path) {
  $p=$Path.Replace('\','/')
  if(-not $p -or $p.StartsWith('/') -or $p -match '[:\x00-\x1f]' -or $p.Contains('//')) { throw "EXPORT_UNSAFE_PATH: $Path" }
  foreach($part in $p.Split('/')) {
    if(-not $part -or $part -in @('.','..') -or $part -match '[. ]$' -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw "EXPORT_UNSAFE_PATH: $Path" }
  }
  return $p
}
function Get-ExportTree([string]$Root) {
  $full=[IO.Path]::GetFullPath($Root).TrimEnd('\')
  if(-not [IO.Directory]::Exists($full)) { throw "EXPORT_DIRECTORY_MISSING: $full" }
  $items=@(Get-ChildItem -LiteralPath $full -Recurse -Force)
  $seen=@{};$result=@()
  foreach($item in $items) {
    if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "EXPORT_REPARSE_POINT: $($item.FullName)" }
    if($item.PSIsContainer) { continue }
    $relative=ConvertTo-ExportRelativePath ($item.FullName.Substring($full.Length+1))
    if($seen.ContainsKey($relative)) { throw "EXPORT_DUPLICATE_PATH: $relative" }
    $seen[$relative]=$true
    $result+=[ordered]@{path=$relative;bytes=[int64]$item.Length;sha256=(Get-ExportFileHash $item.FullName)}
  }
  return @($result | Sort-Object { $_.path })
}
function Get-ExportIndexHash([object[]]$Index) {
  # PowerShell 5.1 and 7 use different culture collations. The identity must not
  # depend on host version, locale, filesystem enumeration or dictionary order.
  [string[]]$rows=@($Index|ForEach-Object { $_.path+'|'+$_.bytes+'|'+$_.sha256 })
  [Array]::Sort($rows,[StringComparer]::Ordinal)
  return Get-ExportTextHash ($rows -join "\n")
}
function Get-ExportSourceIndex([string]$ProjectRoot) {
  $result=@()
  # Build-time generated version files are captured after write-version.mjs.
  foreach($scope in @('native','src','public','capabilities','decky-plugin','InputHost','GamepadPrerequisites','FanLab/LightSetter','scripts')) {
    $directory=Join-Path $ProjectRoot $scope
    if(-not [IO.Directory]::Exists($directory)) { throw "EXPORT_SOURCE_SCOPE_MISSING: $scope" }
    foreach($file in @(Get-ChildItem -LiteralPath $directory -Recurse -Force -File)) {
      $relative=$file.FullName.Substring(([IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')).Length+1).Replace('\','/')
      if($relative -match '(^|/)(bin|obj|node_modules|\.git)(/|$)' -or $relative -match '\.(obj|res|exe|dll|lib|exp|pdb|ilk|log|zip)$') { continue }
      if($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "EXPORT_REPARSE_POINT: $relative" }
      $result+=[ordered]@{path=(ConvertTo-ExportRelativePath $relative);bytes=[int64]$file.Length;sha256=(Get-ExportFileHash $file.FullName)}
    }
  }
  foreach($relative in @('tools/steam_cssloader_manual_observation_selftest.cpp','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-contract.json','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-request-regression.cpp','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-request-regression.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-request-regression.test.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-native.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-owned-async-parent.mjs','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/manifest.json','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/NAudio.Core.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/NAudio.Wasapi.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.DirectInput.dll','deps/ymcc-device-core-runtime/regression-fixtures/pre-abi-repair/SharpDX.Direct3D9.dll','deps/ymcc-device-core-runtime/export-baseline/approved-20261023-compact34.json','tools/ymcc_sim/domains/fan/fan_domain.ps1','tools/SharpDxCallbackSelfTest/Run-SelfTest.ps1','tools/HcManagerInitializationSelfTest/Run-SelfTest.ps1','deps/ymcc-device-core-runtime/export-baseline.lock.json','deps/ymcc-device-core-runtime/export-baseline/profile.json','deps/ymcc-device-core-runtime/export-baseline/runtime.json','deps/ymcc-device-core-runtime/export-baseline/inventory.json','tools/hc-export-baseline-guard.ps1','tools/Test-HcExportBaselineGuard.ps1','tools/hc-export-source-contract.ps1','tools/hc-device-runtime/runtime-selftest.ps1','tools/hc-device-runtime/repair-package.py','tools/hc-device-runtime/connect-installed-runtime.ps1','tools/hc-application-dependency-trim/Program.cs','tools/hc-application-dependency-trim/ComGateSelfTest.cs','tools/hc-application-dependency-trim/ApplicationTrim.csproj','tools/hc-application-dependency-trim/test-com-gate.ps1','tools/hc-application-dependency-trim/create-overlay.ps1','PowerControl/settings-baseline.json','PowerControl/settings-defaults-v1.json','tools/settings-baseline-contract.ps1','tools/settings-baseline-contract.mjs','tools/settings_baseline_selftest.mjs','tools/settings_baseline_package_selftest.ps1','tools/updater_config_migration_selftest.ts','tools/settings_startup_recovery_browser_selftest.mjs','tools/settings_baseline_native_prepare.mjs','tools/settings_baseline_native_selftest.mjs','tools/global_cache_config_native_selftest.mjs','docs/settings-upgrade-baseline.md','tools/fixtures/settings_baseline.mjs','tools/fixtures/settings_baseline_native.cpp','tools/ymcc_sim/global/extra-adapter-assembly.mjs','tools/ymcc_sim/global/extra-adapter-index.json','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/catalog.mjs','tools/ymcc_sim/global/candidate-options.mjs','tools/ymcc_sim/global/bundled-node-tool-authority.mjs','tools/ymcc_sim/global/snapshot-node-tools.mjs','tools/ymcc_sim/global/execution-completion/index.mjs','tools/ymcc_sim/global/source-snapshot.mjs','tools/ymcc_sim/global/source-snapshot.test.mjs','tools/fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs','tools/ymcc_sim/global/receipt-audit.mjs','tools/ymcc_sim/global/adapters.mjs','tools/ymcc_sim/global/sleep.mjs','tools/ymcc_sim/global/log-corpus.mjs','tools/ymcc_sim/global/signal-replay.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/complete-host-selftest/adapter.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/README.md','tools/decky_sidebar_host_selftest.mjs','tools/decky_sidebar_hang_recovery_selftest.mjs','tools/decky_sidebar_recovery_selftest.cpp','tools/ymcc_sim/global/capability-lifecycle/index.mjs','tools/ymcc_sim/global/cli.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/adapter.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/contract.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/run.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/consumer-selftest.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/native-fixture.cpp','tools/fixtures/decky_mirror_host_fixture.mjs','index.html','vite.config.ts','tsconfig.json','tsconfig.node.json','app.config.json','version.json','package.json','pnpm-lock.yaml','tools/controller_visibility_probe.cpp','deps/json/json.hpp','tools/build-workspace.ps1','tools/build-lightsetter-payload.ps1','tools/lighting-build-contract.ps1','tools/lighting-candidate-source.ps1','tools/build-battery-host.ps1','tools/battery-charge-generate-routes.mjs','tools/battery-charge-contract.mjs','tools/verify-battery-runtime.ps1','tools/write-battery-runtime-receipt.ps1','docs/battery-charge-capabilities.json','tools/package-release.ps1','tools/release-export-identity.ps1','tools/gpu-dedicated-memory-export-guard.ps1','tools/sleep-steam-export-guard.ps1','tools/verify-steam-sleep-export.ps1','tools/decky-release-identity.ps1','tools/build-decky-sidebar-plugin.mjs','tools/decky_sidebar_decky_api_build.mjs','hud-output.html','radial-output.html','tools/monitor_hud_vue_build.mjs','tools/monitor-hud-export.ps1','tools/deploy-installed.ps1','tools/decky_update_release_gate.ps1','.github/workflows/release.yml','.github/workflows/release-recovery-034.yml','tools/decky_sidebar_build_verify.ps1','tools/decky_resource_cache_selftest.mjs','tools/decky_resource_cache_selftest.cpp','tools/decky_resource_worker_selftest.mjs','tools/decky_resource_worker_selftest.cpp','tools/decky_package_maintenance_selftest.ps1','tools/decky_package_maintenance_embed.mjs','tools/decky_retained_session_repair_selftest.mjs','tools/decky_sidebar_setup_selftest.cpp','tools/decky-loader-contract.mjs','tools/decky-loader-upgrade.ps1','tools/decky_loader_official_selftest.mjs','tools/decky_loader_upgrade_selftest.ps1','tools/decky_sidebar_runtime_selftest.cpp','tools/decky_sidebar_setup_selftest.cpp','tools/release_export_identity_selftest.ps1','tools/decky_ymcc_update_selftest.mjs','tools/decky_update_recovery_selftest.mjs','tools/ymcc_sim/global/extra-adapters.mjs','tools/ymcc_sim/global/decky-update-recovery/adapter.mjs','tools/ymcc_sim/global/decky-update-recovery/run.mjs','tools/ymcc_sim/global/decky-update-recovery/native-fixture.cpp','tools/ymcc_sim/global/decky-update-recovery/read-selftest.mjs','tools/ymcc_sim/global/decky-update-recovery/client-selftest.mjs','tools/ymcc_sim/global/decky-update-recovery/field-record.json','tools/ymcc_sim/global/decky-update-recovery/field-record-20261021.json','tools/ymcc_sim/global/decky-update-recovery/upstream-reinject.json','tools/decky_no_repeat_context_selftest.mjs','tools/decky_slider_stability_selftest.mjs','tools/decky_performance_intent_selftest.mjs','tools/decky_sidebar_slider_drag_selftest.mjs','tools/decky_sidebar_client_selftest.mjs','tools/fixtures/decky_game_memory_store.mjs','tools/fixtures/decky_task_paths.mjs','tools/steam_manual_startup_source_selftest.mjs','tools/ymcc_sim/global/decky-update-recovery/field-record-wake-20261022.json','tools/decky_wake_chain_selftest.mjs', 'tools/fixtures/decky_mirror_host_fixture.mjs', 'tools/decky_sidebar_frontend_boot_selftest.mjs', 'tools/ymcc_sim/global/decky-update-recovery/field-record-empty-frontend-20261022.json', 'tools/decky_manual_repair_selftest.mjs','tools/decky_trainer_foreground_selftest.mjs','tools/game_trainer_foreground_selftest.ps1','tools/ymcc_sim/global/decky-update-recovery/field-record-trainer-20261022.json','tools/ymcc_sim/global/decky-update-recovery/field-record-backend-20261022.json','tools/ymcc_sim/global/decky-update-recovery/field-record-20261022.json','tools/ymcc_sim/global/decky-update-recovery/README.md','tools/decky_sidebar_health_embed.mjs','tools/decky_sidebar_health_dom_selftest.mjs','tools/decky_sidebar_health_selftest.cpp','tools/decky_sidebar_health_close_selftest.cpp','tools/decky_sidebar_safety_selftest.mjs','tools/decky_sidebar_safety_owner_selftest.mjs','tools/decky_sidebar_bootstrap_watch_selftest.cpp','tools/decky_sidebar_bootstrap_selftest.cpp','tools/decky_sidebar_bootstrap_expression_selftest.mjs','tools/decky_sidebar_frontend_update_embed.mjs','tools/decky_updater_service_selftest.mjs','tools/decky_updater_ui_selftest.mjs','tools/decky_updater_owner_loopback_selftest.ts','tools/decky_legacy_store_selftest.mjs','tools/updater_install_chain_selftest.ps1','tools/updater_resume_native_selftest.ps1','tools/updater_rollback_selftest.ps1','tools/updater_manager_resume_selftest.ts','tools/updater_service_game_selftest.mjs','tools/updater_service_game_selftest.ts','tools/decky_sidebar_broker_selftest.cpp','tools/decky_sidebar_crash_breaker_selftest.cpp','tools/decky_sidebar_stability_matrix_selftest.mjs','tools/decky_store_compat_selftest.mjs','tools/hc-device-runtime/verify-runtime.ps1','tools/hc-device-runtime/build-runtime.py','deps/ymcc-device-core-runtime/profile.json','deps/ymcc-device-core-runtime/upstream.runtime.json','deps/ymcc-shared-runtime/manifest.json','tools/ymcc-shared-runtime/shared-runtime.ps1')) {
    $file=Join-Path $ProjectRoot $relative
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_SOURCE_MISSING: $relative" }
    $result+=[ordered]@{path=$relative;bytes=[int64]([IO.FileInfo]$file).Length;sha256=(Get-ExportFileHash $file)}
  }
  return @($result | Sort-Object { $_.path })
}
function Get-BatteryHostBuildPayload([string]$WorkspaceRoot,[string]$ProjectRoot) {
  $build=Join-Path $WorkspaceRoot 'Build/App'
  $result=@()
  $batteryRoot=Join-Path $ProjectRoot 'PowerControl/battery-host'
  $allowed=(Get-Content -LiteralPath (Join-Path $ProjectRoot 'native/BatteryHost/payload-files.json') -Raw | ConvertFrom-Json)
  foreach($name in $allowed) {
    if(-not [IO.File]::Exists((Join-Path $batteryRoot $name))) { throw "EXPORT_BUILD_MISSING: battery-host/$name" }
  }
  foreach($file in @(Get-ExportTree $batteryRoot)) {
    if($file.path -notin $allowed) { throw "EXPORT_UNEXPECTED_BATTERY_PAYLOAD: $($file.path)" }
    if($file.path -ne 'battery-host.payload.json') {
      $built=Join-Path $build ('BatteryHost/'+$file.path)
      if(-not [IO.File]::Exists($built) -or (Get-ExportFileHash $built) -ne $file.sha256) { throw "EXPORT_BUILD_CHANGED: battery-host/$($file.path)" }
    }
    $result+=[ordered]@{path='PowerControl/battery-host/'+$file.path;bytes=$file.bytes;sha256=$file.sha256;lane='battery'}
  }
  return @($result|Sort-Object { $_.path })
}
function Get-ExportBuildPayload([string]$WorkspaceRoot,[string]$ProjectRoot) {
  $build=Join-Path $WorkspaceRoot 'Build/App'
  $result=@()
  foreach($file in @(Get-ExportTree (Join-Path $build 'Web'))) {
    $result+=[ordered]@{path='YeManCC/'+$file.path;bytes=$file.bytes;sha256=$file.sha256;lane='web'}
  }
  foreach($name in @('YeManCC.exe','YeManRecoveryService.exe','YeManControllerVisibilityProbe.exe','YMCCRtssProfileHelper.exe','YMCCOverlayBridge.dll')) {
    $file=Join-Path $build ('Native/'+$name)
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_BUILD_MISSING: $name" }
    $result+=[ordered]@{path='YeManCC/'+$name;bytes=[int64]([IO.FileInfo]$file).Length;sha256=(Get-ExportFileHash $file);lane='native'}
  }
  # Bind the five build-owned InputHost files; shared dependencies remain pinned
  # to the HC runtime by package-release.ps1. These enter the complete stage later.
  foreach($name in @('YeManInputHost.exe','YeManInputHost.dll','YeManInputHost.deps.json','YeManInputHost.runtimeconfig.json','HIDMaestro.Core.dll')) {
    $file=Join-Path $build ('Native/InputHost/'+$name)
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_BUILD_MISSING: InputHost/$name" }
    $result+=[ordered]@{path='PowerControl/feature-assets/virtual-gamepad/'+$name;bytes=[int64]([IO.FileInfo]$file).Length;sha256=(Get-ExportFileHash $file);lane='inputhost'}
  }
  foreach($name in @('YeManLightSetter.exe','YeManLightSetter.dll','YeManLightSetter.deps.json','YeManLightSetter.runtimeconfig.json','YeManLightSetter.json','YeManLightDeviceCore.dll')) {
    $file=Join-Path $ProjectRoot ('PowerControl/light-setter/'+$name)
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_BUILD_MISSING: light-setter/$name" }
    $result+=[ordered]@{path='PowerControl/light-setter/'+$name;bytes=[int64]([IO.FileInfo]$file).Length;sha256=(Get-ExportFileHash $file);lane='light-setter'}
  }
  $result+=@(Get-BatteryHostBuildPayload $WorkspaceRoot $ProjectRoot)
  foreach($name in @('dist/index.js','plugin.json','package.json','LICENSE.decky-api','LICENSE.vue')) {
    $file=Join-Path $build ('Decky/plugins/ymcc-sidebar/'+$name)
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_BUILD_MISSING: Decky/$name" }
    $result+=[ordered]@{path='PowerControl/decky/plugins/ymcc-sidebar/'+$name;bytes=[int64]([IO.FileInfo]$file).Length;sha256=(Get-ExportFileHash $file);lane='decky'}
  }
  foreach($name in @('PluginLoader_noconsole.exe','LICENSE.decky-loader')) {
    $file=Join-Path $ProjectRoot ('PowerControl/decky/'+$name)
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_BUILD_MISSING: $name" }
    $result+=[ordered]@{path='PowerControl/decky/'+$name;bytes=[int64]([IO.FileInfo]$file).Length;sha256=(Get-ExportFileHash $file);lane='decky'}
  }
  foreach($name in @('YeManCC/index.html','YeManCC/hud-output.html','YeManCC/app.config.json')) {
    if(-not @($result|Where-Object {$_.path -eq $name}).Count) { throw "EXPORT_BUILD_MISSING: $name" }
  }
  return @($result|Sort-Object { $_.path })
}
function Write-ExportBuildIdentity([string]$ProjectRoot,[string]$WorkspaceRoot,[object[]]$SourceBefore) {
  $after=@(Get-ExportSourceIndex $ProjectRoot)
  $sourceHash=Get-ExportIndexHash $after
  $beforeHash=Get-ExportIndexHash $SourceBefore
  if($beforeHash -ne $sourceHash) {
    $beforeByPath=@{};foreach($row in $SourceBefore){$beforeByPath[[string]$row.path]=$row}
    $afterByPath=@{};foreach($row in $after){$afterByPath[[string]$row.path]=$row}
    $added=@($afterByPath.Keys|Where-Object{-not $beforeByPath.ContainsKey($_)}|Sort-Object)
    $removed=@($beforeByPath.Keys|Where-Object{-not $afterByPath.ContainsKey($_)}|Sort-Object)
    $changed=@(foreach($name in @($beforeByPath.Keys|Sort-Object)) {
      if($afterByPath.ContainsKey($name) -and ($beforeByPath[$name].sha256 -cne $afterByPath[$name].sha256 -or $beforeByPath[$name].bytes -ne $afterByPath[$name].bytes)) {
        [ordered]@{path=$name;before=$beforeByPath[$name];after=$afterByPath[$name]}
      }
    })
    $directory=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity'
    New-Item -ItemType Directory -Force -Path $directory|Out-Null
    $failurePath=Join-Path $directory ('REJECTED-'+[Guid]::NewGuid().ToString('N')+'.json')
    $failure=[ordered]@{schema=1;status='rejected';accepted=$false;reason='EXPORT_SOURCE_CHANGED_DURING_BUILD';createdAtUtc=[DateTime]::UtcNow.ToString('o');sourceBeforeSha256=$beforeHash;sourceAfterSha256=$sourceHash;added=$added;removed=$removed;changed=$changed;sourceBefore=@($SourceBefore);sourceAfter=$after}
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($failure|ConvertTo-Json -Depth 9))
    $stream=[IO.File]::Open($failurePath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    $names=@($added)+@($removed)+@($changed|ForEach-Object{$_.path})
    throw ('EXPORT_SOURCE_CHANGED_DURING_BUILD: '+(($names|Select-Object -First 5)-join ', ')+'; evidence='+$failurePath)
  }
  $payload=@(Get-ExportBuildPayload $WorkspaceRoot $ProjectRoot)
  $steamSleep = Assert-SteamSleepNativeExport $ProjectRoot (Join-Path $WorkspaceRoot 'Build/App/Native/YeManCC.exe')
  $gpuMemory = Assert-GpuDedicatedMemoryNativeExport $ProjectRoot (Join-Path $WorkspaceRoot 'Build/App/Native/YeManCC.exe')
  $version=([IO.File]::ReadAllText((Join-Path $ProjectRoot 'version.json'))|ConvertFrom-Json).version
  $identity=[ordered]@{schema=1;version=$version;createdAtUtc=[DateTime]::UtcNow.ToString('o');sourceTreeSha256=$sourceHash;sources=$after;payload=$payload;steamSleepRecovery=$steamSleep;gpuDedicatedMemory=$gpuMemory;buildId=(Get-ExportTextHash ($sourceHash+'|'+(Get-ExportIndexHash $payload)))}
  $path=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json'
  New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($path))|Out-Null
  [IO.File]::WriteAllText($path,($identity|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
  return $identity
}
function Assert-ExportBuildIdentity([string]$ProjectRoot,[string]$WorkspaceRoot) {
  $path=Join-Path $WorkspaceRoot 'Build/Validation/ExportIdentity/BUILD-IDENTITY.json'
  if(-not [IO.File]::Exists($path)) { throw 'EXPORT_BUILD_IDENTITY_MISSING: run build-workspace.ps1; old captures cannot be relabelled as fresh' }
  $identity=[IO.File]::ReadAllText($path)|ConvertFrom-Json
  if($identity.schema -ne 1 -or -not $identity.sources -or -not $identity.payload -or $identity.buildId -notmatch '^[a-f0-9]{64}$') { throw 'EXPORT_BUILD_IDENTITY_INVALID' }
  $sourceHash=Get-ExportIndexHash @(Get-ExportSourceIndex $ProjectRoot)
  if($identity.sourceTreeSha256 -ne $sourceHash -or (Get-ExportIndexHash @($identity.sources)) -ne $sourceHash) { throw 'EXPORT_SOURCE_CHANGED: source does not match this build; rebuild first' }
  $payload=@(Get-ExportBuildPayload $WorkspaceRoot $ProjectRoot)
  if((Get-ExportIndexHash @($identity.payload)) -ne (Get-ExportIndexHash $payload)) { throw 'EXPORT_BUILD_CHANGED: Native/Web/Decky payload does not match build capture' }
  if($identity.buildId -ne (Get-ExportTextHash ($sourceHash+'|'+(Get-ExportIndexHash $payload)))) { throw 'EXPORT_BUILD_IDENTITY_INVALID' }
  $steamSleep = Assert-SteamSleepNativeExport $ProjectRoot (Join-Path $WorkspaceRoot 'Build/App/Native/YeManCC.exe')
  Assert-SteamSleepExportReceipt $identity.steamSleepRecovery $steamSleep
  $gpuMemory = Assert-GpuDedicatedMemoryNativeExport $ProjectRoot (Join-Path $WorkspaceRoot 'Build/App/Native/YeManCC.exe')
  Assert-GpuDedicatedMemoryExportReceipt $identity.gpuDedicatedMemory $gpuMemory
  return $identity
}
function Assert-ExportStagedBuild([string]$Root,$BuildIdentity,[switch]$BeforeCompletePackage) {
  if ($BuildIdentity.steamSleepRecovery.status -eq 'passed') {
    Assert-SteamSleepArtifactBytes ([IO.File]::ReadAllBytes((Join-Path $Root 'YeManCC/YeManCC.exe'))) $BuildIdentity.steamSleepRecovery $Root
  }
  foreach($record in @($BuildIdentity.payload)) {
    if($BeforeCompletePackage -and $record.lane -eq 'inputhost') { continue }
    $relative=ConvertTo-ExportRelativePath $record.path
    $file=Join-Path $Root $relative.Replace('/','\')
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_STAGE_MISSING: $relative" }
    if(([IO.FileInfo]$file).Length -ne $record.bytes -or (Get-ExportFileHash $file) -ne $record.sha256) { throw "EXPORT_STAGE_MISMATCH: $relative" }
  }
  $expected=@($BuildIdentity.payload|Where-Object {$_.lane -eq 'web' -and $_.path.StartsWith('YeManCC/assets/') }|ForEach-Object {$_.path.Substring(8)})
  $actual=@(Get-ExportTree (Join-Path $Root 'YeManCC/assets')|ForEach-Object {$_.path})
  $expectedAssets=@($expected|ForEach-Object {$_.Substring(7)})
  if(@(Compare-Object $expectedAssets $actual).Count) { throw 'EXPORT_STAGE_WEB_FILESET_MISMATCH: stale or extra assets present' }
}
function Write-ExportPackageIdentity([string]$Root,$BuildIdentity) {
  Assert-ExportStagedBuild $Root $BuildIdentity
  $identityPath=Join-Path $Root 'YeManCC/export-identity.json'
  if([IO.File]::Exists($identityPath)) { throw 'EXPORT_STALE_PACKAGE_IDENTITY: staging must be clean' }
  $payload=@(Get-ExportTree $Root)
  $record=[ordered]@{schema=1;packageId='YeManCC';version=$BuildIdentity.version;buildId=$BuildIdentity.buildId;sourceTreeSha256=$BuildIdentity.sourceTreeSha256;steamSleepRecovery=$BuildIdentity.steamSleepRecovery;gpuDedicatedMemory=$BuildIdentity.gpuDedicatedMemory;exportedAtUtc=[DateTime]::UtcNow.ToString('o');payloadFileCount=$payload.Count;payload=$payload}
  [IO.File]::WriteAllText($identityPath,($record|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
  return $record
}
# A local development export must carry the same build receipt as its installed
# native/Web/Decky files; otherwise an old identity survives a successful deploy.
function Write-ExportInstalledIdentityFromArchive([string]$Root,[string]$Archive,$BuildIdentity,[string]$BackupRoot) {
  Assert-ExportStagedBuild $Root $BuildIdentity
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
  try {
    $entries=@($zip.Entries|Where-Object {$_.FullName.Replace('\','/').TrimStart('/') -eq 'YeManCC/export-identity.json'})
    if($entries.Count -ne 1 -or $entries[0].Length -gt 4194304){throw 'EXPORT_INSTALLED_IDENTITY_ARCHIVE_INVALID'}
    $stream=$entries[0].Open();$buffer=[IO.MemoryStream]::new()
    try {$stream.CopyTo($buffer);$bytes=$buffer.ToArray()}finally{$stream.Dispose();$buffer.Dispose()}
  }finally{$zip.Dispose()}
  try {$record=[Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF)|ConvertFrom-Json}catch{throw 'EXPORT_INSTALLED_IDENTITY_JSON_INVALID'}
  if($record.schema -ne 1 -or $record.packageId -ne 'YeManCC' -or $record.version -ne $BuildIdentity.version -or $record.buildId -ne $BuildIdentity.buildId -or $record.sourceTreeSha256 -ne $BuildIdentity.sourceTreeSha256){throw 'EXPORT_INSTALLED_IDENTITY_BUILD_MISMATCH'}
  $target=Join-Path $Root 'YeManCC/export-identity.json'
  if(Test-Path -LiteralPath $target){
    if(-not(Test-Path -LiteralPath $target -PathType Leaf) -or ((Get-Item -LiteralPath $target -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'EXPORT_INSTALLED_IDENTITY_UNSAFE_TARGET'}
    $backup=Join-Path $BackupRoot 'YeManCC/export-identity.json'
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($backup))|Out-Null
    Copy-Item -LiteralPath $target -Destination $backup -Force
  }
  [IO.File]::WriteAllBytes($target,$bytes)
  $expectedStream=[IO.MemoryStream]::new($bytes)
  try {$expectedHash=Get-ExportHash $expectedStream}finally{$expectedStream.Dispose()}
  if((Get-ExportFileHash $target) -ne $expectedHash){throw 'EXPORT_INSTALLED_IDENTITY_WRITE_MISMATCH'}
  return $record
}
# The runnable Release must be the exact same product tree as the verified ZIP.
# Release metadata/Packages are siblings, so enumerate only the two product roots.
function Assert-ExportPublishedTree([string]$Root,[string]$CompleteRoot,$BuildIdentity) {
  Assert-ExportStagedBuild $Root $BuildIdentity
  foreach($name in @('YeManCC','PowerControl')) {
    $expected=@(Get-ExportTree (Join-Path $CompleteRoot $name))
    $actual=@(Get-ExportTree (Join-Path $Root $name))
    if((Get-ExportIndexHash $expected) -ne (Get-ExportIndexHash $actual)) {
      throw "EXPORT_RELEASE_TREE_MISMATCH: $name does not match the complete ZIP tree"
    }
  }
}
function Assert-ExportArchiveIdentity([string]$Zip,[string]$Root,$BuildIdentity) {
  Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
  Assert-ExportStagedBuild $Root $BuildIdentity
  $expected=@{};foreach($file in @(Get-ExportTree $Root)){$expected[$file.path]=$file}
  $archive=[IO.Compression.ZipFile]::OpenRead($Zip)
  try {
    $actual=@{};$directories=@{};$count=0
    foreach($entry in $archive.Entries) {
      $raw=$entry.FullName.Replace('\','/')
      $isDirectory=$raw.EndsWith('/')
      $relative=ConvertTo-ExportRelativePath $raw.TrimEnd('/')
      if($relative.Split('/')[0] -notin @('YeManCC','PowerControl')) { throw "EXPORT_ZIP_UNEXPECTED_ROOT: $relative" }
      if($isDirectory) {
        if($actual.ContainsKey($relative)-or $directories.ContainsKey($relative)){throw "EXPORT_ZIP_DUPLICATE: $relative"}
        $directories[$relative]=$true;continue
      }
      if($actual.ContainsKey($relative)-or $directories.ContainsKey($relative)) { throw "EXPORT_ZIP_DUPLICATE: $relative" }
      $actual[$relative]=$true
      if(-not $expected.ContainsKey($relative)) { throw "EXPORT_ZIP_UNEXPECTED_FILE: $relative" }
      $stream=$entry.Open();try{$sha=Get-ExportHash $stream}finally{$stream.Dispose()}
      $record=$expected[$relative]
      if($entry.Length -ne $record.bytes -or $sha -ne $record.sha256) { throw "EXPORT_ZIP_MISMATCH: $relative" }
      $count++
    }
    foreach($relative in $expected.Keys){if(-not $actual.ContainsKey($relative)){throw "EXPORT_ZIP_MISSING: $relative"}}
    $identity=[IO.File]::ReadAllText((Join-Path $Root 'YeManCC/export-identity.json'))|ConvertFrom-Json
    $withoutIdentity=@($expected.Values|Where-Object {$_.path-ne'YeManCC/export-identity.json'})
    if($identity.schema-ne 1 -or $identity.packageId-ne'YeManCC' -or $identity.buildId-ne$BuildIdentity.buildId -or $identity.version-ne$BuildIdentity.version -or $identity.sourceTreeSha256-ne$BuildIdentity.sourceTreeSha256 -or $identity.payloadFileCount-ne$withoutIdentity.Count -or (Get-ExportIndexHash @($identity.payload))-ne(Get-ExportIndexHash $withoutIdentity)) { throw 'EXPORT_PACKAGE_IDENTITY_INVALID' }
    Assert-SteamSleepExportReceipt $identity.steamSleepRecovery $BuildIdentity.steamSleepRecovery
    return [ordered]@{status='passed';steamSleepRecovery=$BuildIdentity.steamSleepRecovery;buildId=$BuildIdentity.buildId;version=$BuildIdentity.version;sourceTreeSha256=$BuildIdentity.sourceTreeSha256;files=$count;zipSha256=(Get-ExportFileHash $Zip);zip=[IO.Path]::GetFullPath($Zip);nativeSha256=(@($BuildIdentity.payload|Where-Object {$_.path-eq'YeManCC/YeManCC.exe'})[0].sha256);pluginSha256=(@($BuildIdentity.payload|Where-Object {$_.path-eq'PowerControl/decky/plugins/ymcc-sidebar/dist/index.js'})[0].sha256)}
  } finally { $archive.Dispose() }
}

function Assert-ExportWorkspaceChild([string]$Path,[string]$Parent) {
  $full=[IO.Path]::GetFullPath($Path).TrimEnd('\');$base=[IO.Path]::GetFullPath($Parent).TrimEnd('\')
  if(-not $full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)) { throw "EXPORT_PATH_OUTSIDE_WORKSPACE: $full" }
  $cursor=$full
  while($cursor -and $cursor.Length -ge $base.Length) {
    if(Test-Path -LiteralPath $cursor) {
      $item=Get-Item -LiteralPath $cursor -Force
      if($item.Attributes-band[IO.FileAttributes]::ReparsePoint){throw "EXPORT_REPARSE_POINT: $cursor"}
    }
    $cursor=[IO.Path]::GetDirectoryName($cursor)
  }
}
function Enter-ExportWorkspaceLock([string]$WorkspaceRoot) {
  $key=Get-ExportTextHash ([IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\').ToLowerInvariant())
  $mutex=[Threading.Mutex]::new($false,('Local\YeManCC.Export.'+$key))
  $held=$false
  try{try{$held=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$held=$true}
    if(-not$held){throw 'EXPORT_WORKSPACE_BUSY: another build/export is using this workspace'}
    return $mutex
  }catch{$mutex.Dispose();throw}
}
function Backup-ExportReleaseItem([string]$Path,[string]$BackupRoot,[string]$WorkspaceRoot,$Ledger) {
  Assert-ExportWorkspaceChild $Path (Join-Path $WorkspaceRoot 'Release')
  Assert-ExportWorkspaceChild $BackupRoot (Join-Path $WorkspaceRoot 'Backup/Release')
  $backup=Join-Path $BackupRoot ([IO.Path]::GetFileName($Path))
  if(Test-Path -LiteralPath $backup){throw "EXPORT_BACKUP_COLLISION: $backup"}
  $record=[ordered]@{path=$Path;backup=$backup;hadOriginal=(Test-Path -LiteralPath $Path)}
  $Ledger.Add($record)
  if($record.hadOriginal){New-Item -ItemType Directory -Force -Path $BackupRoot|Out-Null;Move-Item -LiteralPath $Path -Destination $backup -ErrorAction Stop}
}
function Undo-ExportReleaseItems([string]$WorkspaceRoot,$Ledger) {
  $failures=@()
  foreach($record in @($Ledger.ToArray())) {
    try {
      Assert-ExportWorkspaceChild $record.path (Join-Path $WorkspaceRoot 'Release')
      Assert-ExportWorkspaceChild $record.backup (Join-Path $WorkspaceRoot 'Backup/Release')
      # If backup never moved, the original was never replaced: DO NOT delete it.
      if($record.hadOriginal -and -not(Test-Path -LiteralPath $record.backup)) { continue }
      if(Test-Path -LiteralPath $record.path){Remove-Item -LiteralPath $record.path -Recurse -Force -ErrorAction Stop}
      if($record.hadOriginal){New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($record.path))|Out-Null;Move-Item -LiteralPath $record.backup -Destination $record.path -ErrorAction Stop}
    } catch { $failures+=$_.Exception.Message }
  }
  if($failures.Count){throw ('EXPORT_ROLLBACK_FAILED: backups preserved; '+($failures-join'; '))}
}
