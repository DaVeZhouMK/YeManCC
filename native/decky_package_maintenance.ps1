# YMCC owns the integration and this transaction, never the official Loader bytes.
# Embedded in the existing YMCC updater; no product helper EXE/network updater.
# Source: the already authenticated whole YMCC package. Never execute the Loader to
# inspect a version, kill by process name, purge the Decky home, or refresh Steam UI.
# Future upgrades: regenerate deckyManaged from the public lock + matching plugin;
# the incoming package (not the outgoing client's old lock) supplies its identity.
function Enter-YmccModuleMaintenance([ValidateRange(0,15000)][int]$WaitMilliseconds=0) {
  $mutex = New-Object Threading.Mutex($false, 'Global\YeManCC.ModuleMaintenance')
  $held = $false
  try {
    try { $held = $mutex.WaitOne($WaitMilliseconds) }
    catch [Threading.AbandonedMutexException] { $held = $true }
    if (!$held) { throw 'MODULE_MAINTENANCE_BUSY: YMCC 或模块安装正在进行，请等待完成后重试。' }
    return $mutex
  } catch { if ($held) { $mutex.ReleaseMutex() }; $mutex.Dispose(); throw }
}
function Exit-YmccModuleMaintenance($Lease) {
  if ($Lease) { try { $Lease.ReleaseMutex() } finally { $Lease.Dispose() } }
}
function Get-YmccDeckyFiles {
  return @('PluginLoader_noconsole.exe','LICENSE.decky-loader',
    'plugins/ymcc-sidebar/dist/index.js','plugins/ymcc-sidebar/plugin.json',
    'plugins/ymcc-sidebar/package.json','plugins/ymcc-sidebar/LICENSE.decky-api',
    'plugins/ymcc-sidebar/LICENSE.vue')
}
function Resolve-YmccDeckyPath([string]$Root, [string]$Relative='') {
  if (![IO.Path]::IsPathRooted($Root)) { throw 'DECKY_PATH_NOT_ABSOLUTE' }
  $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
  if ($base -eq [IO.Path]::GetPathRoot($base).TrimEnd('\')) { throw 'DECKY_PATH_IS_VOLUME_ROOT' }
  if ($Relative -and ($Relative.Contains('\') -or $Relative -match '(^/|:|(^|/)\.{1,2}(/|$)|//|[. ](/|$))')) { throw 'DECKY_PATH_INVALID' }
  $full = $base
  if ($Relative) { $full = [IO.Path]::GetFullPath((Join-Path $base $Relative.Replace('/','\'))) }
  if ($full -ine $base -and !$full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'DECKY_PATH_ESCAPES_HOME' }
  # Inspect every existing ancestor, including the named home and its parents.
  for ($cursor=$full; $cursor; $cursor=[IO.Path]::GetDirectoryName($cursor)) {
    $item=Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
    if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'DECKY_REPARSE_POINT_REFUSED' }
  }
  return $full
}
function Get-YmccDeckyHash([string]$Path) {
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
}
function Test-YmccDeckyOwnedFile([string]$Name) {
  # Historical receipt entries can retire only immutable code in our namespace.
  # Never accept settings/logs, another plugin or a recursive directory request.
  return $Name -cin (Get-YmccDeckyFiles) -or $Name -cmatch '^plugins/ymcc-sidebar/dist/[A-Za-z0-9_-][A-Za-z0-9_.-]*\.(js|css|map)$'
}

# Decode only bounded, complete JSON objects. WinPS ConvertFrom-Json otherwise
# accepts duplicate decoded keys and coerces identity/length property types.
function Read-YmccDeckyDocument([string]$Path) {
  $Path=Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName($Path)) ([IO.Path]::GetFileName($Path))
  $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  try {
    if ($stream.Length -le 0 -or $stream.Length -gt 131072) { throw 'DECKY_DOCUMENT_INVALID_SIZE' }
    $bytes=New-Object byte[] 131073;$used=0
    do { $read=$stream.Read($bytes,$used,$bytes.Length-$used);$used+=$read } while($read -gt 0 -and $used -lt $bytes.Length)
    if ($used -gt 131072) { throw 'DECKY_DOCUMENT_INVALID_SIZE' }
    $utf8=New-Object Text.UTF8Encoding($false,$true)
    $text=$utf8.GetString($bytes,0,$used).TrimStart([char]0xfeff)
  } finally { $stream.Dispose() }
  $pattern='"(?:[^"\\\x00-\x1f]|\\(?:["\\/bfnrt]|u[0-9a-fA-F]{4}))*"|[{}\[\],:]|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?|true|false|null'
  $stack=New-Object 'System.Collections.Generic.List[object]';$offset=0;$closed=$false
  foreach($token in [regex]::Matches($text,$pattern)) {
    if ($text.Substring($offset,$token.Index-$offset) -notmatch '^[ \t\r\n]*$' -or $closed) { throw 'DECKY_DOCUMENT_INVALID' }
    $offset=$token.Index+$token.Length;$value=$token.Value
    if ($stack.Count -eq 0 -and $value -cne '{') { throw 'DECKY_DOCUMENT_OBJECT_REQUIRED' }
    if ($value -ceq '{' -or $value -ceq '[') {
      if ($stack.Count -ge 32) { throw 'DECKY_DOCUMENT_DEPTH_LIMIT' }
      $stack.Add(@{object=($value -ceq '{');key=$true;names=(New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase))})
    } elseif($value -ceq '}' -or $value -ceq ']') {
      if (!$stack.Count -or ($stack[$stack.Count-1].object -ne ($value -ceq '}'))) { throw 'DECKY_DOCUMENT_INVALID' }
      $stack.RemoveAt($stack.Count-1);if(!$stack.Count){$closed=$true}
    } elseif($value -ceq ',') {
      if(!$stack.Count){throw 'DECKY_DOCUMENT_INVALID'};$stack[$stack.Count-1].key=$true
    } elseif($value.StartsWith('"') -and $stack.Count -and $stack[$stack.Count-1].object -and $stack[$stack.Count-1].key) {
      $name=[regex]::Replace($value.Substring(1,$value.Length-2),'\\(?:["\\/bfnrt]|u[0-9a-fA-F]{4})',[Text.RegularExpressions.MatchEvaluator]{param($escape)
        switch -CaseSensitive ($escape.Value) {
          '\"' {return '"'}; '\\' {return '\'}; '\/' {return '/'}
          '\b' {return [string][char]8}; '\f' {return [string][char]12}; '\n' {return "`n"}; '\r' {return "`r"}; '\t' {return "`t"}
          default {return [string][char][Convert]::ToInt32($escape.Value.Substring(2),16)}
        }
      })
      if(!$stack[$stack.Count-1].names.Add($name)){throw 'DECKY_DOCUMENT_DUPLICATE_KEY'}
      $stack[$stack.Count-1].key=$false
    }
  }
  if(!$closed -or $stack.Count -or $text.Substring($offset) -notmatch '^[ \t\r\n]*$'){throw 'DECKY_DOCUMENT_INVALID'}
  return ConvertFrom-Json -InputObject $text -ErrorAction Stop
}
function Test-YmccDeckyInteger($Value) {
  return $Value -is [int] -or $Value -is [long] -or $Value -is [uint32] -or $Value -is [uint64]
}

function Assert-YmccDeckyManifest($Manifest, [switch]$Previous) {
  if (!$Manifest -or !($Manifest -is [Management.Automation.PSCustomObject] -or $Manifest -is [Collections.IDictionary]) -or
      !(Test-YmccDeckyInteger $Manifest.schemaVersion) -or $Manifest.schemaVersion -ne 1 -or $Manifest.owner -isnot [string] -or $Manifest.owner -cne 'YMCC' -or
      $Manifest.origin -isnot [string] -or $Manifest.loaderVersion -isnot [string] -or $Manifest.loaderSha256 -isnot [string] -or $Manifest.files -isnot [array] -or
      $Manifest.origin -cne 'official-release' -or $Manifest.loaderVersion -cnotmatch '^v\d+\.\d+\.\d+$' -or
      $Manifest.loaderSha256 -cnotmatch '^[a-f0-9]{64}$' -or @($Manifest.files).Count -lt 1 -or @($Manifest.files).Count -gt 64) { throw 'DECKY_MANIFEST_INVALID' }
  $names=@{}
  foreach ($file in @($Manifest.files)) {
    $name=$file.path
    if ($name -isnot [string] -or $file.sha256 -isnot [string] -or !(Test-YmccDeckyInteger $file.bytes) -or !(Test-YmccDeckyOwnedFile $name) -or $name.Contains('..') -or $name.Contains('//') -or
        $names.ContainsKey($name) -or $file.sha256 -cnotmatch '^[a-f0-9]{64}$' -or [int64]$file.bytes -le 0 -or [int64]$file.bytes -gt 134217728) { throw 'DECKY_MANAGED_FILE_INVALID' }
    $names[$name]=$file
  }
  if (!$names.ContainsKey('PluginLoader_noconsole.exe') -or $names['PluginLoader_noconsole.exe'].sha256 -cne $Manifest.loaderSha256) { throw 'DECKY_LOADER_IDENTITY_INVALID' }
  if (!$Previous -and (@($names.Keys).Count -ne 7 -or @((Get-YmccDeckyFiles) | Where-Object { !$names.ContainsKey($_) }).Count)) { throw 'DECKY_PACKAGE_FILESET_INVALID' }
}
function New-YmccDeckyManifest([string]$Source, [string]$LoaderLock) {
  $lock=Read-YmccDeckyDocument $LoaderLock
  if (!(Test-YmccDeckyInteger $lock.schemaVersion) -or $lock.schemaVersion -ne 1 -or $lock.origin -isnot [string] -or $lock.repository -isnot [string] -or $lock.byteIdentical -isnot [bool] -or
      !(Test-YmccDeckyInteger $lock.loaderPatches) -or !(Test-YmccDeckyInteger $lock.asset.size) -or $lock.asset.size -le 0 -or $lock.asset.size -gt 134217728 -or
      $lock.asset.sha256 -isnot [string] -or $lock.asset.sha256 -cnotmatch '^[a-f0-9]{64}$' -or $lock.version -isnot [string] -or
      $lock.origin -cne 'official-release' -or $lock.byteIdentical -ne $true -or $lock.loaderPatches -ne 0 -or $lock.repository -cne 'SteamDeckHomebrew/decky-loader') { throw 'DECKY_OFFICIAL_LOCK_INVALID' }
  $files=@(foreach ($name in Get-YmccDeckyFiles) {
    $file=Resolve-YmccDeckyPath $Source $name
    if (![IO.File]::Exists($file)) { throw ('DECKY_PACKAGE_FILE_MISSING '+$name) }
    [ordered]@{path=$name;bytes=([IO.FileInfo]$file).Length;sha256=(Get-YmccDeckyHash $file)}
  })
  $result=[pscustomobject][ordered]@{schemaVersion=1;owner='YMCC';origin='official-release';loaderVersion=$lock.version;loaderSha256=$lock.asset.sha256;files=$files}
  Assert-YmccDeckyManifest $result
  if ($files[0].bytes -ne $lock.asset.size) { throw 'DECKY_OFFICIAL_LENGTH_MISMATCH' }
  return $result
}
function Get-YmccDeckyPlan([string]$Source, [string]$Target, $Manifest, [string]$InstalledManifestPath='') {
  Assert-YmccDeckyManifest $Manifest
  $sourceHome=Resolve-YmccDeckyPath $Source
  $targetHome=Resolve-YmccDeckyPath $Target
  if ($sourceHome -ieq $targetHome -or $sourceHome.StartsWith($targetHome+'\',[StringComparison]::OrdinalIgnoreCase) -or $targetHome.StartsWith($sourceHome+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'DECKY_SOURCE_TARGET_OVERLAP' }
  # Official Loader migrates every ROOT *.json into settings with os.rename.
  # Keep YMCC ownership metadata outside that namespace; never patch the Loader.
  $receipt=Resolve-YmccDeckyPath $targetHome '.ymcc-managed.receipt'
  $legacyReceipt=Resolve-YmccDeckyPath $targetHome '.ymcc-managed.json'
  $movedReceipt=Resolve-YmccDeckyPath $targetHome 'settings/.ymcc-managed.json'
  if ([IO.Directory]::Exists($receipt) -or [IO.Directory]::Exists($legacyReceipt)) { throw 'DECKY_RECEIPT_IS_DIRECTORY' }
  $previous=$null; $preserved=@(); $entries=@(); $incoming=@{}
  $installedIdentity=$receipt
  if (![IO.File]::Exists($receipt) -and [IO.File]::Exists($legacyReceipt)) { $installedIdentity=$legacyReceipt }
  elseif (![IO.File]::Exists($receipt) -and [IO.File]::Exists($movedReceipt)) { $installedIdentity=$movedReceipt }
  elseif (![IO.File]::Exists($receipt) -and $InstalledManifestPath -and [IO.File]::Exists($InstalledManifestPath)) {
    $installedIdentity=Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName($InstalledManifestPath)) ([IO.Path]::GetFileName($InstalledManifestPath))
  }
  if ([IO.File]::Exists($installedIdentity)) {
    if (([IO.FileInfo]$installedIdentity).Length -gt 131072) { throw 'DECKY_INSTALLED_RECEIPT_TOO_LARGE' }
    $previous=Read-YmccDeckyDocument $installedIdentity
    Assert-YmccDeckyManifest $previous -Previous
    if ([version]$previous.loaderVersion.Substring(1) -gt [version]$Manifest.loaderVersion.Substring(1)) { throw 'DECKY_PACKAGE_DOWNGRADE_REFUSED_UPDATE_YMCC' }
  }
  # A duplicate old root receipt must also be validated before retiring it,
  # including its downgrade fence. A foreign/corrupt file is never deleted.
  $legacyHash=$null
  if ([IO.File]::Exists($legacyReceipt)) {
    if (([IO.FileInfo]$legacyReceipt).Length -gt 131072) { throw 'DECKY_INSTALLED_RECEIPT_TOO_LARGE' }
    $legacy=Read-YmccDeckyDocument $legacyReceipt
    Assert-YmccDeckyManifest $legacy -Previous
    if ([version]$legacy.loaderVersion.Substring(1) -gt [version]$Manifest.loaderVersion.Substring(1)) { throw 'DECKY_PACKAGE_DOWNGRADE_REFUSED_UPDATE_YMCC' }
    $legacyHash=Get-YmccDeckyHash $legacyReceipt
  }
  foreach ($file in @($Manifest.files)) {
    $src=Resolve-YmccDeckyPath $sourceHome $file.path
    $dst=Resolve-YmccDeckyPath $targetHome $file.path
    if (![IO.File]::Exists($src) -or ([IO.FileInfo]$src).Length -ne $file.bytes -or (Get-YmccDeckyHash $src) -cne $file.sha256) { throw ('DECKY_PACKAGE_HASH_MISMATCH '+$file.path) }
    if ([IO.Directory]::Exists($dst)) { throw 'DECKY_TARGET_FILE_IS_DIRECTORY' }
    $oldHash=$null
    if ([IO.File]::Exists($dst)) { $oldHash=Get-YmccDeckyHash $dst }
    $incoming[$file.path]=$true
    if ($oldHash -cne $file.sha256) { $entries+= [pscustomobject]@{path=$file.path;source=$src;before=$oldHash;after=$file.sha256;remove=$false;bytes=$null} }
  }
  $plugin=Get-Content -LiteralPath (Resolve-YmccDeckyPath $sourceHome 'plugins/ymcc-sidebar/plugin.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($plugin.name -cne 'ymcc-sidebar') { throw 'DECKY_PLUGIN_IDENTITY_INVALID' }
  if ($previous) { foreach ($file in @($previous.files)) {
    if ($incoming.ContainsKey($file.path)) { continue }
    $dst=Resolve-YmccDeckyPath $targetHome $file.path
    if ([IO.File]::Exists($dst)) {
      if ((Get-YmccDeckyHash $dst) -ceq $file.sha256) { $entries+=[pscustomobject]@{path=$file.path;source=$null;before=$file.sha256;after=$null;remove=$true;bytes=$null} }
      else { $preserved+=$file.path }
    }
  } }
  $receiptBytes=[Text.Encoding]::UTF8.GetBytes(($Manifest | ConvertTo-Json -Depth 8 -Compress))
  $sha=[Security.Cryptography.SHA256]::Create()
  try { $receiptHash=([BitConverter]::ToString($sha.ComputeHash($receiptBytes))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
  $oldReceipt=$null
  if ([IO.File]::Exists($receipt)) { $oldReceipt=Get-YmccDeckyHash $receipt }
  if ($oldReceipt -cne $receiptHash) { $entries+=[pscustomobject]@{path='.ymcc-managed.receipt';source=$null;before=$oldReceipt;after=$receiptHash;remove=$false;bytes=$receiptBytes} }
  # Write the safe receipt FIRST; retire only the validated legacy root inside
  # the same backup/rollback transaction. Leave settings and user plugins alone.
  if ($legacyHash) { $entries+=[pscustomobject]@{path='.ymcc-managed.json';source=$null;before=$legacyHash;after=$null;remove=$true;bytes=$null} }
  # File lock failure is detected before the first mutation; foreign Loader stays untouched.
  foreach ($entry in $entries) { if ($entry.before) {
    $file=Resolve-YmccDeckyPath $targetHome $entry.path
    $stream=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $stream.Dispose()
  } }
  return [pscustomobject]@{source=$sourceHome;target=$targetHome;manifest=$Manifest;entries=@($entries);preservedObsolete=@($preserved);legacyAdoption=($null -eq $previous)}
}
function Set-YmccDeckyAtomic([string]$Root, [string]$Relative, [string]$Source, [byte[]]$Bytes, [string]$Expected) {
  $destination=Resolve-YmccDeckyPath $Root $Relative
  $directory=[IO.Path]::GetDirectoryName($destination)
  [IO.Directory]::CreateDirectory($directory) | Out-Null
  $temporary=Join-Path $directory ('.ymcc-'+[guid]::NewGuid().ToString('N')+'.tmp')
  try {
    if ($Source) { [IO.File]::Copy($Source,$temporary,$false) } else { [IO.File]::WriteAllBytes($temporary,$Bytes) }
    if ((Get-YmccDeckyHash $temporary) -cne $Expected) { throw 'DECKY_STAGED_HASH_MISMATCH' }
    $destination=Resolve-YmccDeckyPath $Root $Relative
    if ([IO.File]::Exists($destination)) { [IO.File]::Replace($temporary,$destination,[NullString]::Value) } else { [IO.File]::Move($temporary,$destination) }
    if ((Get-YmccDeckyHash $destination) -cne $Expected) { throw 'DECKY_COMMIT_HASH_MISMATCH' }
  } finally { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
}
function Undo-YmccDeckyTransaction($Transaction) {
  if (!$Transaction -or $Transaction.restored) { return }
  # Snapshot every path first. Refuse to overwrite content written by another owner.
  foreach ($entry in $Transaction.applied.ToArray()) {
    $destination=Resolve-YmccDeckyPath $Transaction.plan.target $entry.path
    $now=$null
    if ([IO.File]::Exists($destination)) { $now=Get-YmccDeckyHash $destination }
    if ($now -cne $entry.after -and $now -cne $entry.before) { throw 'DECKY_ROLLBACK_CONCURRENT_CHANGE' }
  }
  for ($i=$Transaction.applied.Count-1; $i -ge 0; --$i) {
    $entry=$Transaction.applied[$i]
    $destination=Resolve-YmccDeckyPath $Transaction.plan.target $entry.path
    $now=$null
    if ([IO.File]::Exists($destination)) { $now=Get-YmccDeckyHash $destination }
    if ($now -ceq $entry.before) { continue } # A failed atomic replacement may have left the original untouched.
    # Another updater can change a later file after the whole-list preflight.
    # Revalidate at its own restore step; never overwrite that late owner.
    if ($now -cne $entry.after) { throw 'DECKY_ROLLBACK_CONCURRENT_CHANGE' }
    if ($entry.before) {
      $backup=Resolve-YmccDeckyPath $Transaction.backup $entry.path
      Set-YmccDeckyAtomic $Transaction.plan.target $entry.path $backup $null $entry.before
    } else {
      $destination=Resolve-YmccDeckyPath $Transaction.plan.target $entry.path
      if ([IO.File]::Exists($destination)) { [IO.File]::Delete($destination) }
    }
  }
  $Transaction.restored=$true
}
function Invoke-YmccDeckyInstall($Plan, [string]$BackupRoot) {
  $backup=Resolve-YmccDeckyPath $BackupRoot
  if ([IO.Directory]::Exists($backup) -or [IO.File]::Exists($backup)) { throw 'DECKY_BACKUP_ALREADY_EXISTS' }
  if ($backup -ieq $Plan.source -or $backup.StartsWith($Plan.source+'\',[StringComparison]::OrdinalIgnoreCase) -or $Plan.source.StartsWith($backup+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'DECKY_BACKUP_SOURCE_OVERLAP' }
  if ($backup -ieq $Plan.target -or $backup.StartsWith($Plan.target+'\',[StringComparison]::OrdinalIgnoreCase) -or $Plan.target.StartsWith($backup+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'DECKY_BACKUP_TARGET_OVERLAP' }
  $transaction=[pscustomobject]@{plan=$Plan;backup=$backup;applied=(New-Object 'System.Collections.Generic.List[object]');restored=$false}
  [IO.Directory]::CreateDirectory($backup) | Out-Null
  foreach ($entry in $Plan.entries) {
    $destination=Resolve-YmccDeckyPath $Plan.target $entry.path
    $now=$null
    if ([IO.File]::Exists($destination)) { $now=Get-YmccDeckyHash $destination }
    if ($now -cne $entry.before) { throw 'DECKY_TARGET_CHANGED_BEFORE_INSTALL' }
    if ($entry.before) {
      $copy=Resolve-YmccDeckyPath $backup $entry.path
      [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($copy)) | Out-Null
      [IO.File]::Copy($destination,$copy,$false)
      if ((Get-YmccDeckyHash $copy) -cne $entry.before) { throw 'DECKY_BACKUP_HASH_MISMATCH' }
    }
  }
  try {
    foreach ($entry in $Plan.entries) {
      $destination=Resolve-YmccDeckyPath $Plan.target $entry.path
      $now=$null
      if ([IO.File]::Exists($destination)) { $now=Get-YmccDeckyHash $destination }
      if ($now -cne $entry.before) { throw 'DECKY_TARGET_CHANGED_DURING_INSTALL' }
      if ($entry.remove) { [IO.File]::Delete($destination);  $transaction.applied.Add($entry) }
      else {
        $transaction.applied.Add($entry)
        Set-YmccDeckyAtomic $Plan.target $entry.path $entry.source $entry.bytes $entry.after
      }
    }
    # Final identity, including unchanged files; receipt alone is never healthy proof.
    foreach ($file in $Plan.manifest.files) {
      if ((Get-YmccDeckyHash (Resolve-YmccDeckyPath $Plan.target $file.path)) -cne $file.sha256) { throw 'DECKY_FINAL_HASH_MISMATCH' }
    }
    return $transaction
  } catch {
    $failure=$_.Exception.Message
    try { Undo-YmccDeckyTransaction $transaction } catch { throw ($failure+'; Decky rollback: '+$_.Exception.Message) }
    throw $failure
  }
}

# Hold an exclusive backend port lease while immutable files change. This also
# prevents official file-watchers from starting the plugin halfway through an
# install (community #982). An occupied/foreign backend is refused, never killed.
function Open-YmccDeckyBackendLease([ValidateRange(0,65535)][int]$Port=1337) {
  $lease=New-Object Net.Sockets.TcpListener([Net.IPAddress]::Any,$Port)
  $lease.ExclusiveAddressUse=$true
  try { $lease.Start(); return $lease }
  catch { $lease.Stop(); throw ('DECKY_BACKEND_IN_USE_OR_UNAVAILABLE: '+$_.Exception.Message) }
}
function Install-YmccDeckyPackage($Plan, [string]$BackupRoot, [ValidateRange(0,65535)][int]$BackendPort=1337) {
  $moduleLease=Enter-YmccModuleMaintenance
  $lease=$null
  try {
    if (@($Plan.entries).Count) { $lease=Open-YmccDeckyBackendLease $BackendPort }
    return Invoke-YmccDeckyInstall $Plan $BackupRoot
  } finally { try { if ($lease) { $lease.Stop() } } finally { Exit-YmccModuleMaintenance $moduleLease } }
}
function Restore-YmccDeckyPackage($Transaction, [ValidateRange(0,65535)][int]$BackendPort=1337) {
  if (!$Transaction -or $Transaction.restored) { return }
  $moduleLease=Enter-YmccModuleMaintenance
  $lease=$null
  try {
    if ($Transaction.applied.Count) { $lease=Open-YmccDeckyBackendLease $BackendPort }
    Undo-YmccDeckyTransaction $Transaction
  } finally { try { if ($lease) { $lease.Stop() } } finally { Exit-YmccModuleMaintenance $moduleLease } }
}
