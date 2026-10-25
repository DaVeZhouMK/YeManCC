# Only stages created by this file-only compatibility lane carry this marker.
# Call cleanup only while holding the module maintenance lease and after the
# currently requested payload has been verified in its installed home.
function Write-YmccBootstrapStage([string]$Stage, [string]$Target, [string]$PackageVersion, [string]$PayloadSha256, [ValidateSet('preparing','applying','committed')][string]$Phase, [string[]]$ManagedPaths, [string]$PostCommitModuleSha256='') {
  $safe=Resolve-YmccDeckyPath $Stage
  if ([IO.Path]::GetFileName($safe) -cnotmatch '^module-bootstrap-[a-f0-9]{32}$') { throw 'MODULE_BOOTSTRAP_STAGE_NAME_INVALID' }
  $paths=@($ManagedPaths);if ($paths.Count -lt 1 -or $paths.Count -gt 10000) { throw 'MODULE_BOOTSTRAP_PATH_INVENTORY_INVALID' }
  foreach($relative in $paths) { if (!(Test-YmccModuleFile $relative)) { throw 'MODULE_BOOTSTRAP_PATH_INVENTORY_INVALID' } }
  $record=[ordered]@{schemaVersion=2;owner='YMCC.InputPayloadBootstrap';target=(Resolve-YmccDeckyPath $Target);packageVersion=$PackageVersion;payloadSha256=$PayloadSha256;phase=$Phase;managedPaths=$paths}
  if($PostCommitModuleSha256){
    if($Phase -cne 'committed' -or $PostCommitModuleSha256 -cnotmatch '^[a-f0-9]{64}$'){throw 'MODULE_POSTCOMMIT_MARKER_INVALID'}
    $record['postCommitCleanup']=[ordered]@{schemaVersion=1;moduleManifestSha256=$PostCommitModuleSha256}
  }
  $bytes=[Text.Encoding]::UTF8.GetBytes(($record|ConvertTo-Json -Compress))
  $sha=[Security.Cryptography.SHA256]::Create()
  try { $hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
  Set-YmccDeckyAtomic $safe '.ymcc-bootstrap-stage.json' $null $bytes $hash
}
function Test-YmccBootstrapPayloadCurrent([string]$Target, $Manifest) {
  Assert-YmccModuleManifest $Manifest
  foreach($entry in $Manifest.files) {
    $file=Resolve-YmccDeckyPath $Target $entry.path
    if (![IO.File]::Exists($file) -or ([IO.FileInfo]$file).Length -ne $entry.bytes -or (Get-YmccDeckyHash $file) -cne $entry.sha256) { return $false }
  }
  return $true
}
function Test-YmccBootstrapStageOwned([string]$Stage, [string]$Target, [switch]$Snapshot) {
  try {
    $safe=Resolve-YmccDeckyPath $Stage
    if ([IO.Path]::GetFileName($safe) -cnotmatch '^module-bootstrap-[a-f0-9]{32}$') { return $false }
    $marker=Resolve-YmccDeckyPath $safe '.ymcc-bootstrap-stage.json'
    if (![IO.File]::Exists($marker) -or ([IO.FileInfo]$marker).Length -gt 4194304) { return $false }
    $record=Get-Content -LiteralPath $marker -Raw -Encoding UTF8|ConvertFrom-Json
    if ($record.schemaVersion -ne 2 -or $record.owner -cne 'YMCC.InputPayloadBootstrap' -or $record.target -ine (Resolve-YmccDeckyPath $Target) -or $record.payloadSha256 -cnotmatch '^[a-f0-9]{64}$' -or $record.phase -notin @('preparing','applying','committed')) { return $false }
    if($record.PSObject.Properties['postCommitCleanup']){
      if($record.phase -cne 'committed' -or $record.postCommitCleanup.schemaVersion -ne 1 -or [string]$record.postCommitCleanup.moduleManifestSha256 -cnotmatch '^[a-f0-9]{64}$'){return $false}
    }
    if (@($record.managedPaths).Count -lt 1 -or @($record.managedPaths).Count -gt 10000) { return $false }
    $allowed=@{};$knownDirectories=@{'payload'=$true;'rollback'=$true}
    foreach($name in @($record.managedPaths)) {
      if (!(Test-YmccModuleFile $name) -or $allowed.ContainsKey($name)) { return $false }
      $allowed[$name]=$true;$parts=$name -split '/';$prefix=''
      for($index=0;$index -lt $parts.Count-1;$index++) {
        if($prefix){$prefix+='/'};$prefix+=$parts[$index]
        $knownDirectories[$prefix]=$true;$knownDirectories['payload/'+$prefix]=$true;$knownDirectories['rollback/'+$prefix]=$true
      }
    }
    $files=New-Object 'System.Collections.Generic.List[string]';$directories=New-Object 'System.Collections.Generic.List[string]'
    $pending=New-Object 'System.Collections.Generic.Stack[string]';$pending.Push($safe);$count=0
    while($pending.Count) {
      $folder=$pending.Pop()
      foreach($item in @(Get-ChildItem -LiteralPath $folder -Force -ErrorAction Stop)) {
        if (++$count -gt 50000 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { return $false }
        $relative=$item.FullName.Substring($safe.Length+1).Replace('\','/')
        if ($item.PSIsContainer) {
          if (!$knownDirectories.ContainsKey($relative)) { return $false }
          $directories.Add($item.FullName);$pending.Push($item.FullName)
        } else {
          $owned=$relative -ceq '.ymcc-bootstrap-stage.json' -or $relative -cmatch '^\.ymcc-[a-f0-9]{32}\.tmp$'
          $leaf=$relative -replace '^(payload|rollback)/',''
          if ($allowed.ContainsKey($leaf) -or $relative -ceq 'rollback/.ymcc-input-managed.json') { $owned=$true }
          if (!$owned) { return $false }
          if ($relative -cne '.ymcc-bootstrap-stage.json') { $files.Add($item.FullName) }
        }
      }
    }
    if ($Snapshot) { return [pscustomobject]@{root=$safe;files=$files.ToArray();directories=$directories.ToArray();record=$record} }
    return $true
  } catch { return $false }
}
function Remove-YmccBootstrapStageFile([string]$Stage, [string]$File) {
  $relative=$File.Substring($Stage.Length+1).Replace('\','/')
  $safe=Resolve-YmccDeckyPath $Stage $relative
  if ([IO.File]::Exists($safe)) {
    $attributes=[IO.File]::GetAttributes($safe)
    if ($attributes -band [IO.FileAttributes]::ReadOnly) { [IO.File]::SetAttributes($safe,($attributes -band (-bnot [IO.FileAttributes]::ReadOnly))) }
    [IO.File]::Delete($safe)
  }
}
function Remove-YmccBootstrapStage([string]$Stage, [string]$Target) {
  $tree=Test-YmccBootstrapStageOwned $Stage $Target -Snapshot
  if (!$tree -or $tree.record.PSObject.Properties['postCommitCleanup']) { return $false }
  # No recursive delete: revalidate each path, remove only empty directories,
  # and keep ownership evidence until every payload/backup file is gone.
  foreach($file in $tree.files) { Remove-YmccBootstrapStageFile $tree.root $file }
  foreach($folder in @($tree.directories|Sort-Object {$_.Length} -Descending)) {
    $relative=$folder.Substring($tree.root.Length+1).Replace('\','/')
    $safe=Resolve-YmccDeckyPath $tree.root $relative
    if([IO.Directory]::Exists($safe)){[IO.Directory]::Delete($safe,$false)}
  }
  Remove-YmccBootstrapStageFile $tree.root (Join-Path $tree.root '.ymcc-bootstrap-stage.json')
  [IO.Directory]::Delete($tree.root,$false)
  return $true
}
function Remove-YmccBootstrapStages([string]$InstallRoot, [string]$Target, [ValidateRange(1,16)][int]$Maximum=8) {
  $root=Resolve-YmccDeckyPath $InstallRoot
  $targetHome=Resolve-YmccDeckyPath $Target
  $expectedTarget=Resolve-YmccDeckyPath $root 'PowerControl'
  if ($targetHome -ine $expectedTarget) { throw 'MODULE_BOOTSTRAP_CLEANUP_TARGET_INVALID' }
  $removed=0;$preserved=0
  foreach($entry in @(Get-ChildItem -LiteralPath $root -Directory -Filter 'module-bootstrap-*' -Force -ErrorAction Stop|Select-Object -First 64)) {
    if ($removed -ge $Maximum) { break }
    try { $candidate=Resolve-YmccDeckyPath $root $entry.Name } catch { ++$preserved;continue }
    if (!(Test-YmccBootstrapStageOwned $candidate $targetHome)) { ++$preserved;continue }
    # Resolve every entry before deleting; unknown content and redirected paths
    # make the complete stage ineligible. File-only stages have no child process.
    if ($candidate -ieq $root -or !$candidate.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'MODULE_BOOTSTRAP_CLEANUP_PATH_INVALID' }
    try { if(Remove-YmccBootstrapStage $candidate $targetHome){++$removed}else{++$preserved} } catch { ++$preserved }
  }
  return [pscustomobject]@{removed=$removed;preserved=$preserved}
}
function Sync-YmccBootstrapReceipt([string]$Target, $Manifest) {
  Assert-YmccModuleManifest $Manifest
  $receipt=Resolve-YmccDeckyPath $Target '.ymcc-input-managed.json'
  $bytes=[Text.Encoding]::UTF8.GetBytes(($Manifest|ConvertTo-Json -Depth 8 -Compress))
  $sha=[Security.Cryptography.SHA256]::Create()
  try { $hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
  if ([IO.File]::Exists($receipt) -and (Get-YmccDeckyHash $receipt) -ceq $hash) { return }
  # All declared bytes, including unchanged files, must still agree before a
  # receipt can certify them. The module lease excludes other YMCC mutations.
  if (!(Test-YmccBootstrapPayloadCurrent $Target $Manifest)) { throw 'MODULE_RECEIPT_PAYLOAD_CHANGED' }
  Set-YmccDeckyAtomic $Target '.ymcc-input-managed.json' $null $bytes $hash
}

# This is the existing module-bootstrap stage lane. No new watcher or updater.
function Read-YmccBootstrapSmallJson([string]$Path){
  $file=Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName($Path)) ([IO.Path]::GetFileName($Path))
  if(![IO.File]::Exists($file) -or ([IO.FileInfo]$file).Length -gt 4194304){throw 'MODULE_POSTCOMMIT_MANIFEST_MISSING_OR_TOO_LARGE'}
  return (Get-Content -LiteralPath $file -Raw -Encoding UTF8|ConvertFrom-Json)
}
function Register-YmccPostCommitHcCleanup([string]$Root,[string]$Target,$Manifest,[string]$ManifestPath,[string]$CommitState,[int]$OwnerPid){
  if($CommitState){
    $commit=Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($CommitState))) ([IO.Path]::GetFileName($CommitState))
    $state=Read-YmccBootstrapSmallJson $commit
    if($OwnerPid -le 0 -or $state.phase -cne 'committed' -or [int]$state.pid -ne $OwnerPid -or [string]$state.version -cne [string]$Manifest.packageVersion){throw 'MODULE_POSTCOMMIT_STATE_NOT_CONFIRMED'}
  }
  foreach($entry in @(Get-ChildItem -LiteralPath $Root -Directory -Filter 'module-bootstrap-*' -Force|Select-Object -First 64)){
    $owned=Test-YmccBootstrapStageOwned $entry.FullName $Target -Snapshot
    if($owned -and $owned.record.PSObject.Properties['postCommitCleanup']){return $entry.FullName}
  }
  if(!$CommitState){return ''}
  $stage=Resolve-YmccDeckyPath $Root ('module-bootstrap-'+[guid]::NewGuid().ToString('N'))
  [IO.Directory]::CreateDirectory($stage)|Out-Null
  Write-YmccBootstrapStage $stage $Target ([string]$Manifest.packageVersion) ([string]$Manifest.compatibility.payloadSha256) 'committed' @($Manifest.files.path) (Get-YmccDeckyHash $ManifestPath)
  return $stage
}
function Test-YmccPostCommitHostReleased([string]$Root,[int]$OwnerPid){
  $pc=Resolve-YmccDeckyPath $Root 'PowerControl';$owner=$null
  foreach($process in @(Get-Process -Name YeManFanHost,YeManLightSetter -ErrorAction SilentlyContinue)){
    try{
      $identity=Get-YmccUpdaterProcessIdentity $process
      if(!$identity -or !$identity.Image.StartsWith($pc+'\',[StringComparison]::OrdinalIgnoreCase)){continue}
      if(!$owner -and $OwnerPid -gt 0){$owner=Get-YmccUpdaterProcessIdentity -ProcessId $OwnerPid}
      # Read handles only; do not enumerate modules or stop any process here.
      if(!$owner -or $owner.Image -ine (Join-Path $Root 'YeManCC/YeManCC.exe') -or [long]$identity.Creation -lt [long]$owner.Creation){return $false}
    }catch{return $false}finally{$process.Dispose()}
  }
  return $true
}

function Assert-YmccPostCommitHcLayout([string]$Root,$ModuleManifest){
  $pc=Resolve-YmccDeckyPath $Root 'PowerControl'
  $layout=Read-YmccBootstrapSmallJson (Resolve-YmccDeckyPath $Root 'YeManCC/update-manifest.json')
  $binding=$layout.rules.fanHostPayload
  if($layout.schemaVersion -ne 1 -or $layout.packageId -cne 'yemancc-update' -or [string]$layout.packageVersion -cne [string]$ModuleManifest.packageVersion -or [string]$binding.directory -cne 'fan-host-v2' -or $binding.schemaVersion -ne 2 -or [string]$binding.manifestSha256 -notmatch '^[a-fA-F0-9]{64}$' -or [string]$binding.runtimeManifestSha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'MODULE_POSTCOMMIT_LAYOUT_INVALID'}
  $hostRoot=Resolve-YmccDeckyPath $pc 'fan-host-v2';$hostPath=Resolve-YmccDeckyPath $hostRoot 'YeManFanHost.payload.json'
  if((Get-YmccDeckyHash $hostPath) -ine [string]$binding.manifestSha256){throw 'MODULE_POSTCOMMIT_HOST_MANIFEST_CHANGED'}
  $hostManifest=Read-YmccBootstrapSmallJson $hostPath;$id=[string]$hostManifest.runtimeId
  if($hostManifest.schemaVersion -ne 2 -or $id -cnotmatch '^(YMCC-|HC-)[A-Za-z0-9_.-]+$'){throw 'MODULE_POSTCOMMIT_RUNTIME_BINDING_INVALID'}
  $runtimeDirectory=([string]$binding.runtimeDirectory).Replace([char]92,[char]47)
  $runtimeReference=([string]$hostManifest.runtimeManifest).Replace([char]92,[char]47)
  if($runtimeDirectory -cne ('handheldcompanion-runtime/'+$id) -or $runtimeReference -cne ('../handheldcompanion-runtime/'+$id+'/HandheldCompanion.runtime.json')){throw 'MODULE_POSTCOMMIT_RUNTIME_BINDING_INVALID'}
  $runtime=Resolve-YmccDeckyPath $pc ('handheldcompanion-runtime/'+$id);$runtimePath=Resolve-YmccDeckyPath $runtime 'HandheldCompanion.runtime.json'
  if((Get-YmccDeckyHash $runtimePath) -ine [string]$binding.runtimeManifestSha256){throw 'MODULE_POSTCOMMIT_RUNTIME_MANIFEST_CHANGED'}
  $runtimeManifest=Read-YmccBootstrapSmallJson $runtimePath
  if($runtimeManifest.schemaVersion -ne 1 -or [string]$runtimeManifest.runtimeId -cne $id -or @($hostManifest.files).Count -ne [int]$binding.fileCount){throw 'MODULE_POSTCOMMIT_RUNTIME_IDENTITY_INVALID'}
  foreach($group in @(@{root=$hostRoot;manifest=$hostManifest;metadata='YeManFanHost.payload.json'},@{root=$runtime;manifest=$runtimeManifest;metadata='HandheldCompanion.runtime.json'})){
    Assert-YmccHcTree $group.root
    $files=@($group.manifest.files);if($files.Count -lt 1 -or $files.Count -gt 50000){throw 'MODULE_POSTCOMMIT_FILE_COUNT_INVALID'}
    $known=@{}
    foreach($file in $files){
      $name=[string]$file.path;$key=$name.Replace([char]47,[char]92)
      if(!$name -or $known.ContainsKey($key) -or $name -ieq $group.metadata -or [string]$file.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'MODULE_POSTCOMMIT_FILE_INVALID'}
      $path=Resolve-YmccDeckyPath $group.root $name
      if(![IO.File]::Exists($path) -or (Get-YmccDeckyHash $path) -ine [string]$file.sha256){throw ('MODULE_POSTCOMMIT_FILE_CHANGED: '+$name)}
      $known[$key]=$true
    }
    $actual=@(Get-ChildItem -LiteralPath $group.root -Recurse -File -Force|ForEach-Object{$_.FullName.Substring($group.root.Length+1)}|Where-Object{$_ -ine $group.metadata})
    if(Compare-Object @($known.Keys) $actual){throw 'MODULE_POSTCOMMIT_INVENTORY_CHANGED'}
  }
  if($runtimeManifest.PSObject.Properties['sharedDependencies']){
    $shared=$runtimeManifest.sharedDependencies
    if([string]$shared.directory -cne 'PowerControl/ymcc-shared-runtime' -or @($shared.files).Count -lt 1 -or @($shared.files).Count -gt 128){throw 'MODULE_POSTCOMMIT_SHARED_IDENTITY_INVALID'}
    $sharedRoot=Resolve-YmccDeckyPath $pc 'ymcc-shared-runtime';Assert-YmccHcTree $sharedRoot;$known=@{}
    foreach($file in @($shared.files)){
      $name=[string]$file.path
      if($name -notmatch '^[A-Za-z0-9_.-]+[.]dll$' -or $known.ContainsKey($name) -or [string]$file.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'MODULE_POSTCOMMIT_SHARED_ENTRY_INVALID'}
      $path=Resolve-YmccDeckyPath $sharedRoot $name
      if(![IO.File]::Exists($path) -or ([IO.FileInfo]$path).Length -ne [long]$file.bytes -or (Get-YmccDeckyHash $path) -ine [string]$file.sha256){throw 'MODULE_POSTCOMMIT_SHARED_FILE_CHANGED'}
      $known[$name]=$true
    }
  }
  return $id
}
function Complete-YmccPostCommitHcCleanup([string]$Root,[string]$Target,$Manifest,[string]$Stage,[int]$OwnerPid){
  if(!$Stage){return}
  try{
    if(!(Test-YmccBootstrapPayloadCurrent $Target $Manifest)){throw 'MODULE_POSTCOMMIT_INPUT_NOT_READY'}
    $id=Assert-YmccPostCommitHcLayout $Root $Manifest
    if(!(Test-YmccPostCommitHostReleased $Root $OwnerPid)){throw 'MODULE_POSTCOMMIT_OLD_HOST_STILL_RUNNING'}
    # Reuse the canonical deletion function and its direct-child path checks.
    Remove-YmccSupersededHcRuntimes $Target $id -Maximum 8
    foreach($entry in @(Get-ChildItem -LiteralPath $Root -Directory -Filter 'module-bootstrap-*' -Force|Select-Object -First 64)){
      $owned=Test-YmccBootstrapStageOwned $entry.FullName $Target -Snapshot
      if($owned -and $owned.record.PSObject.Properties['postCommitCleanup']){
        Write-YmccBootstrapStage $entry.FullName $Target ([string]$Manifest.packageVersion) ([string]$Manifest.compatibility.payloadSha256) 'committed' @($Manifest.files.path)
      }
    }
  }catch{Write-Warning ('MODULE_HC_CLEANUP_DEFERRED: '+$_.Exception.Message)}
}
