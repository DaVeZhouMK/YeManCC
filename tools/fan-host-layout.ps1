# Single lane contract for tooling. Source/state names are NOT runtime fallback names.
# Pure, read-only helpers; no product processes, ACL changes, deployment or network.
function Get-FanHostLaneContract {
  return [pscustomobject]@{ sourceDirectory='fan-host'; runtimeDirectory='fan-host-v2'; legacyDirectory='fan-host'; stateDirectory='fan-host'; canonicalZip='YeManCC.zip' }
}
function Get-FanHostLaneHash([string]$Path) {
  $sha=[Security.Cryptography.SHA256]::Create()
  try { $stream=[IO.File]::OpenRead($Path);try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','') } finally {$stream.Dispose()} } finally {$sha.Dispose()}
}
function Assert-FanHostExistingExport([string]$SourcePayload,[string]$TargetPayload) {
  if (!(Test-Path -LiteralPath $TargetPayload)) { return 'absent' }
  $target=Get-Item -LiteralPath $TargetPayload -Force
  if (!$target.PSIsContainer -or ($target.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'FAN_V2_EXISTING_TARGET_UNSAFE: existing target must be a real directory' }
  if (@(Get-ChildItem -LiteralPath $TargetPayload -Recurse -Force | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count) { throw 'FAN_V2_EXISTING_TARGET_UNSAFE: nested reparse point' }
  $sourceFiles=@(Get-ChildItem -LiteralPath $SourcePayload -Recurse -File -Force)
  $sourceNames=@($sourceFiles|ForEach-Object {$_.FullName.Substring($SourcePayload.Length).TrimStart('\')}|Sort-Object -Unique)
  $targetNames=@(Get-ChildItem -LiteralPath $TargetPayload -Recurse -File -Force|ForEach-Object {$_.FullName.Substring($TargetPayload.Length).TrimStart('\')}|Sort-Object -Unique)
  if (!$sourceNames.Count -or !$targetNames.Count -or (Compare-Object $sourceNames $targetNames)) { throw 'FAN_V2_EXISTING_TARGET_DRIFT: preserving a different file set is forbidden; use the transactional updater' }
  foreach ($file in $sourceFiles) { $relative=$file.FullName.Substring($SourcePayload.Length).TrimStart('\');if ((Get-FanHostLaneHash $file.FullName) -ne (Get-FanHostLaneHash (Join-Path $TargetPayload $relative))) { throw ('FAN_V2_EXISTING_TARGET_DRIFT: file differs from approved source: '+$relative+'; use the transactional updater') } }
  return 'identical'
}
function Assert-FanHostToolingPaths([string]$ProjectRoot) {
  $contracts=@(
    @{file='src/bridge/fanHost.ts';required="joinWindowsPath(powerControlDir, 'fan-host-v2')";forbidden="joinWindowsPath(powerControlDir, 'fan-host')"},
    @{file='native/main.cpp';required='POWER_CONTROL_DIR + L"\\fan-host-v2\\YeManFanHost.exe"';forbidden='POWER_CONTROL_DIR + L"\\fan-host\\'},
    @{file='tools/package-release.ps1';required="(Join-Path `$StagingPowerControl 'fan-host-v2')";forbidden="(Join-Path `$StagingPowerControl 'fan-host')"},
    @{file='tools/deploy-installed.ps1';required="`$installedFanHost = Join-Path `$installedPowerControl 'fan-host-v2'";forbidden="`$installedFanHost = Join-Path `$installedPowerControl 'fan-host'";extra="Assert-FanHostExistingExport (Join-Path `$sourcePowerControl 'fan-host') (Join-Path (Split-Path -Parent `$InstallRoot) 'PowerControl\fan-host-v2')"},
    @{file='tools/package-fan-coordinator-test.ps1';required="`$stageFanHost = Join-Path `$stagePowerControl 'fan-host-v2'";forbidden="`$stageFanHost = Join-Path `$stagePowerControl 'fan-host'"},
    @{file='tools/verify-yemancc-export.ps1';required="`$zipEntries['PowerControl/fan-host-v2/YeManFanHost.dll']";forbidden="`$zipEntries['PowerControl/fan-host/YeManFanHost.dll']"},
    @{file='tools/audit-source-export-coverage.ps1';required='^PowerControl/fan-host/(.+)$';forbidden='';extra='PowerControl/fan-host-v2/$($Matches[1])'},
    @{file='tools/Send-YMCC-AI-Fan-Mock.ps1';required='PowerControl\fan-host-v2\YeManFanHost.exe';forbidden='PowerControl\fan-host\YeManFanHost.exe'},
    @{file='tools/cpu-rog/Send-YMCC-AI-Fan-Mock.ps1';required='PowerControl\fan-host-v2\YeManFanHost.exe';forbidden='PowerControl\fan-host\YeManFanHost.exe'},
    @{file='tools/fan_bridge_real_host_smoke_selftest.ts';required="'fan-host-v2'";forbidden='PowerControl/fan-host/YeManFanHost'},
    @{file='tools/ai_fan_runtime_selftest.ts';required="'PowerControl','fan-host-v2'";forbidden="'PowerControl','fan-host'"},
    @{file='tools/fan_host_supersede_exit_selftest.ts';required='PowerControl\\fan-host-v2\\YeManFanHost.exe';forbidden='PowerControl\\fan-host\\YeManFanHost.exe'},
    @{file='tools/fan_p4_refreeze_package.ps1';required='FAN_LEGACY_EXPORT_RETIRED';forbidden='';extra="throw 'FAN_LEGACY_EXPORT_RETIRED"},
    @{file='.github/workflows/release.yml';required='tools/fan_host_path_gate.ps1';forbidden='';extra="--pattern 'YeManCC.zip'"}
  )
  foreach($c in $contracts) {
    $file=Join-Path $ProjectRoot $c.file
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw ('FAN_PATH_CONTRACT_FILE_MISSING: '+$c.file) }
    $text=[IO.File]::ReadAllText($file)
    if (!$text.Contains($c.required) -or ($c.extra -and !$text.Contains($c.extra))) { throw ('FAN_PATH_CONTRACT_DRIFT: '+$c.file) }
    if ($c.forbidden -and $text.Contains($c.forbidden)) { throw ('FAN_LEGACY_RUNTIME_PATH: '+$c.file) }
  }
  $workflow=[IO.File]::ReadAllText((Join-Path $ProjectRoot '.github/workflows/release.yml'))
  $pre=$workflow.IndexOf('name: Verify local Fan Host V2 lane before publishing')
  $publish=$workflow.IndexOf('name: Publish GitHub Release')
  $download=$workflow.IndexOf("gh release download")
  $post=$workflow.IndexOf('name: Verify published Fan Host V2 lane and source-bound bytes')
  $advertise=$workflow.IndexOf('name: Commit version manifest back to main via API')
  if ($pre -lt 0 -or $publish -le $pre -or $download -le $publish -or $post -le $download -or $advertise -le $post) { throw 'FAN_PUBLISH_GATE_ORDER_DRIFT' }
  $lanes=Get-FanHostLaneContract
  if (Test-Path -LiteralPath (Join-Path $ProjectRoot ('PowerControl\'+$lanes.runtimeDirectory))) { throw 'FAN_SOURCE_OUTPUT_MIXED: source tree must not contain a competing fan-host-v2 export; only the verified fan-host source is a package input' }
  return $contracts.Count
}
function Assert-FanHostArchiveLane([string]$ProjectRoot,[string]$ReleaseZip) {
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $lanes=Get-FanHostLaneContract
  if ([IO.Path]::GetFileName($ReleaseZip) -cne $lanes.canonicalZip) { throw 'FAN_PACKAGE_NAME_DRIFT: canonical asset must be YeManCC.zip' }
  $archive=[IO.Compression.ZipFile]::OpenRead($ReleaseZip)
  try {
    $entries=@{};$names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in $archive.Entries) {
      $n=$entry.FullName.Replace('\','/')
      if ($n.StartsWith('/') -or $n.Contains(':') -or @($n -split '/'|Where-Object{$_ -eq '..'}).Count -or !$names.Add($n)) { throw ('FAN_ZIP_PATH_UNSAFE_OR_DUPLICATE: '+$n) }
      if ($n -match '^PowerControl/fan-host(?:/|$)' -or $n -match '(^|/)fan-host-quarantine(/|$)') { throw 'FAN_LEGACY_PAYLOAD_IN_ZIP: legacy directory must remain preserve-only' }
      if (!$n.EndsWith('/')) { $entries[$n]=$entry }
    }
    function Read-FanZipJson([string]$Name) {
      if (!$entries.ContainsKey($Name)) { throw ('FAN_ZIP_ENTRY_MISSING: '+$Name) }
      $reader=[IO.StreamReader]::new($entries[$Name].Open(),[Text.Encoding]::UTF8,$true)
      try { return ($reader.ReadToEnd().TrimStart([char]0xFEFF)|ConvertFrom-Json) } finally {$reader.Dispose()}
    }
    function Get-FanZipHash([string]$Name) {
      if (!$entries.ContainsKey($Name)) { throw ('FAN_ZIP_ENTRY_MISSING: '+$Name) }
      $stream=$entries[$Name].Open();$sha=[Security.Cryptography.SHA256]::Create()
      try {return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')} finally {$stream.Dispose();$sha.Dispose()}
    }
    $layout=Read-FanZipJson 'YeManCC/update-manifest.json'
    if ([int]$layout.schemaVersion -ne 1 -or [string]$layout.packageId -cne 'yemancc-update' -or [string]$layout.rules.fanHost -cne 'preserve-existing' -or [string]$layout.rules.fanHostV2 -cne 'replace') { throw 'FAN_LAYOUT_POLICY_DRIFT: preserve legacy, replace V2' }
    $actualRoots=@($entries.Keys|ForEach-Object {($_ -split '/')[0]}|Sort-Object -Unique)
    $declaredRoots=@($layout.roots|ForEach-Object source|Sort-Object -Unique)
    if ((Compare-Object @('PowerControl','YeManCC') $actualRoots) -or (Compare-Object $declaredRoots $actualRoots)) { throw 'FAN_LAYOUT_ROOT_DRIFT: expected only canonical PowerControl and YeManCC roots' }
    $binding=$layout.rules.fanHostPayload
    if (!$binding -or [string]$binding.directory -cne $lanes.runtimeDirectory -or [int]$binding.schemaVersion -ne 2) { throw 'FAN_PAYLOAD_BINDING_PATH_DRIFT' }
    $prefix='PowerControl/'+$lanes.runtimeDirectory+'/'
    $manifestEntry=$prefix+'YeManFanHost.payload.json'
    $sourcePayload=Join-Path $ProjectRoot ('PowerControl\'+$lanes.sourceDirectory)
    $sourceManifest=Join-Path $sourcePayload 'YeManFanHost.payload.json'
    $manifest=Read-FanZipJson $manifestEntry
    $sourceHash=Get-FanHostLaneHash $sourceManifest
    if ([int]$manifest.schemaVersion -ne 2 -or [int]$binding.fileCount -ne @($manifest.files).Count -or [string]$binding.manifestSha256 -ine $sourceHash -or (Get-FanZipHash $manifestEntry) -ine $sourceHash) { throw 'FAN_PAYLOAD_MANIFEST_IDENTITY_DRIFT' }
    $expected=@('YeManFanHost.payload.json');$listed=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($f in @($manifest.files)) {
      $name=[string]$f.path
      if ($name -notmatch '^[^\\/:]+$' -or $name -in @('.','..') -or !$listed.Add($name)) { throw 'FAN_PAYLOAD_MANAGED_PATH_UNSAFE_OR_DUPLICATE' }
      $expected+=$name;$sha=Get-FanZipHash ($prefix+$name)
      if ($sha -ine [string]$f.sha256 -or $sha -ine (Get-FanHostLaneHash (Join-Path $sourcePayload $name))) { throw ('FAN_PAYLOAD_FILE_IDENTITY_DRIFT: '+$name) }
    }
    $actual=@($entries.Keys|Where-Object {$_.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)}|ForEach-Object {$_.Substring($prefix.Length)}|Sort-Object -Unique)
    if (Compare-Object @($expected|Sort-Object -Unique) $actual) { throw 'FAN_PAYLOAD_FILE_SET_DRIFT' }
    $runtimeRelative=([string]$binding.runtimeDirectory).Replace('\','/')
    if ($runtimeRelative -notmatch '^handheldcompanion-runtime/[^/:]+$' -or $runtimeRelative -match '(^|/)\.\.(/|$)') { throw 'FAN_RUNTIME_BINDING_PATH_UNSAFE' }
    $runtimePrefix='PowerControl/'+$runtimeRelative+'/'
    $runtimeName=$runtimePrefix+'HandheldCompanion.runtime.json';$runtime=Read-FanZipJson $runtimeName
    $sourceRuntime=Join-Path (Join-Path $ProjectRoot 'PowerControl') $runtimeRelative
    if ([int]$runtime.schemaVersion -ne 1 -or @($runtime.files).Count -eq 0 -or (Get-FanZipHash $runtimeName) -ine (Get-FanHostLaneHash (Join-Path $sourceRuntime 'HandheldCompanion.runtime.json'))) { throw 'FAN_RUNTIME_MANIFEST_SOURCE_DRIFT' }
    if ((Get-FanZipHash $runtimeName) -ine [string]$binding.runtimeManifestSha256 -or ([string]$manifest.runtimeManifest).Replace('\','/') -cne ('../'+$runtimeRelative+'/HandheldCompanion.runtime.json') -or [string]$manifest.runtimeId -cne [string]$runtime.runtimeId -or [string]$runtime.runtimeId -cne ($runtimeRelative -split '/')[-1]) { throw 'FAN_RUNTIME_REFERENCE_IDENTITY_DRIFT' }
    $runtimeExpected=@('HandheldCompanion.runtime.json');$runtimePaths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($f in @($runtime.files)) {
      $n=([string]$f.path).Replace('\','/')
      if ($n.StartsWith('/') -or $n.Contains(':') -or @($n -split '/'|Where-Object{$_ -eq '..' -or $_ -eq ''}).Count -or !$runtimePaths.Add($n)) { throw 'FAN_RUNTIME_MANAGED_PATH_UNSAFE_OR_DUPLICATE' }
      $runtimeExpected+=$n
      if ((Get-FanZipHash ($runtimePrefix+$n)) -ine [string]$f.sha256 -or (Get-FanZipHash ($runtimePrefix+$n)) -ine (Get-FanHostLaneHash (Join-Path $sourceRuntime $n))) { throw ('FAN_RUNTIME_FILE_HASH_DRIFT: '+$n) }
    }
    $runtimeActual=@($entries.Keys|Where-Object {$_.StartsWith($runtimePrefix,[StringComparison]::OrdinalIgnoreCase)}|ForEach-Object {$_.Substring($runtimePrefix.Length)}|Sort-Object -Unique)
    if (Compare-Object @($runtimeExpected|Sort-Object -Unique) $runtimeActual) { throw 'FAN_RUNTIME_FILE_SET_DRIFT' }
    return [pscustomobject]@{zip=[IO.Path]::GetFullPath($ReleaseZip);zipSha256=(Get-FanHostLaneHash $ReleaseZip);packageVersion=[string]$layout.packageVersion;runtimeDirectory=$lanes.runtimeDirectory;hostFiles=@($manifest.files).Count;runtimeFiles=@($runtime.files).Count;payloadManifestSha256=$sourceHash}
  } finally {$archive.Dispose()}
}