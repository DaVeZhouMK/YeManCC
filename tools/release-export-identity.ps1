# Source/build/staging/archive binding. All assertions are read-only unless their
# explicit name is Write-*. No installed-product paths or caches are used.
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
  foreach($scope in @('native','src','decky-plugin','InputHost','scripts')) {
    $directory=Join-Path $ProjectRoot $scope
    if(-not [IO.Directory]::Exists($directory)) { throw "EXPORT_SOURCE_SCOPE_MISSING: $scope" }
    foreach($file in @(Get-ChildItem -LiteralPath $directory -Recurse -Force -File)) {
      $relative=$file.FullName.Substring(([IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')).Length+1).Replace('\','/')
      if($relative -match '(^|/)(bin|obj|node_modules|\.git)(/|$)' -or $relative -match '\.(obj|res|exe|dll|lib|exp|pdb|ilk|log|zip)$') { continue }
      if($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "EXPORT_REPARSE_POINT: $relative" }
      $result+=[ordered]@{path=(ConvertTo-ExportRelativePath $relative);bytes=[int64]$file.Length;sha256=(Get-ExportFileHash $file.FullName)}
    }
  }
  foreach($relative in @('index.html','vite.config.ts','tsconfig.json','tsconfig.node.json','app.config.json','version.json','package.json','pnpm-lock.yaml','tools/build-workspace.ps1','tools/package-release.ps1','tools/release-export-identity.ps1','tools/decky-release-identity.ps1','tools/build-decky-sidebar-plugin.mjs','tools/decky_sidebar_decky_api_build.mjs')) {
    $file=Join-Path $ProjectRoot $relative
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_SOURCE_MISSING: $relative" }
    $result+=[ordered]@{path=$relative;bytes=[int64]([IO.FileInfo]$file).Length;sha256=(Get-ExportFileHash $file)}
  }
  return @($result | Sort-Object { $_.path })
}
function Get-ExportBuildPayload([string]$WorkspaceRoot,[string]$ProjectRoot) {
  $build=Join-Path $WorkspaceRoot 'Build/App'
  $result=@()
  foreach($file in @(Get-ExportTree (Join-Path $build 'Web'))) {
    $result+=[ordered]@{path='YeManCC/'+$file.path;bytes=$file.bytes;sha256=$file.sha256;lane='web'}
  }
  foreach($name in @('YeManCC.exe','YeManRecoveryService.exe','YMCCRtssProfileHelper.exe','YMCCOverlayBridge.dll')) {
    $file=Join-Path $build ('Native/'+$name)
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_BUILD_MISSING: $name" }
    $result+=[ordered]@{path='YeManCC/'+$name;bytes=[int64]([IO.FileInfo]$file).Length;sha256=(Get-ExportFileHash $file);lane='native'}
  }
  foreach($name in @('dist/index.js','plugin.json','package.json','LICENSE.decky-api')) {
    $file=Join-Path $build ('Decky/plugins/ymcc-sidebar/'+$name)
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_BUILD_MISSING: Decky/$name" }
    $result+=[ordered]@{path='PowerControl/decky/plugins/ymcc-sidebar/'+$name;bytes=[int64]([IO.FileInfo]$file).Length;sha256=(Get-ExportFileHash $file);lane='decky'}
  }
  foreach($name in @('PluginLoader_noconsole.exe','LICENSE.decky-loader')) {
    $file=Join-Path $ProjectRoot ('PowerControl/decky/'+$name)
    if(-not [IO.File]::Exists($file)) { throw "EXPORT_BUILD_MISSING: $name" }
    $result+=[ordered]@{path='PowerControl/decky/'+$name;bytes=[int64]([IO.FileInfo]$file).Length;sha256=(Get-ExportFileHash $file);lane='decky'}
  }
  foreach($name in @('YeManCC/index.html','YeManCC/app.config.json')) {
    if(-not @($result|Where-Object {$_.path -eq $name}).Count) { throw "EXPORT_BUILD_MISSING: $name" }
  }
  return @($result|Sort-Object { $_.path })
}
function Write-ExportBuildIdentity([string]$ProjectRoot,[string]$WorkspaceRoot,[object[]]$SourceBefore) {
  $after=@(Get-ExportSourceIndex $ProjectRoot)
  $sourceHash=Get-ExportIndexHash $after
  if((Get-ExportIndexHash $SourceBefore) -ne $sourceHash) { throw 'EXPORT_SOURCE_CHANGED_DURING_BUILD: rebuild without concurrent edits' }
  $payload=@(Get-ExportBuildPayload $WorkspaceRoot $ProjectRoot)
  $version=([IO.File]::ReadAllText((Join-Path $ProjectRoot 'version.json'))|ConvertFrom-Json).version
  $identity=[ordered]@{schema=1;version=$version;createdAtUtc=[DateTime]::UtcNow.ToString('o');sourceTreeSha256=$sourceHash;sources=$after;payload=$payload;buildId=(Get-ExportTextHash ($sourceHash+'|'+(Get-ExportIndexHash $payload)))}
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
  return $identity
}
function Assert-ExportStagedBuild([string]$Root,$BuildIdentity) {
  foreach($record in @($BuildIdentity.payload)) {
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
  $record=[ordered]@{schema=1;packageId='YeManCC';version=$BuildIdentity.version;buildId=$BuildIdentity.buildId;sourceTreeSha256=$BuildIdentity.sourceTreeSha256;exportedAtUtc=[DateTime]::UtcNow.ToString('o');payloadFileCount=$payload.Count;payload=$payload}
  [IO.File]::WriteAllText($identityPath,($record|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
  return $record
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
    return [ordered]@{status='passed';buildId=$BuildIdentity.buildId;version=$BuildIdentity.version;sourceTreeSha256=$BuildIdentity.sourceTreeSha256;files=$count;zipSha256=(Get-ExportFileHash $Zip);zip=[IO.Path]::GetFullPath($Zip);nativeSha256=(@($BuildIdentity.payload|Where-Object {$_.path-eq'YeManCC/YeManCC.exe'})[0].sha256);pluginSha256=(@($BuildIdentity.payload|Where-Object {$_.path-eq'PowerControl/decky/plugins/ymcc-sidebar/dist/index.js'})[0].sha256)}
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
