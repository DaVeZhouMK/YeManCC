# Only stages created by this file-only compatibility lane carry this marker.
# Call cleanup only while holding the module maintenance lease and after the
# currently requested payload has been verified in its installed home.
function Read-YmccBootstrapStageRecord([string]$Stage,[string]$Target){
  $safe=Resolve-YmccDeckyPath $Stage;$targetHome=Resolve-YmccDeckyPath $Target
  if([IO.Path]::GetFileName($safe) -cnotmatch '^module-bootstrap-[a-f0-9]{32}$' -or $targetHome -ine (Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName($safe)) 'PowerControl')){throw 'MODULE_BOOTSTRAP_STAGE_BINDING_INVALID'}
  $record=Read-YmccModuleJournalJson (Resolve-YmccDeckyPath $safe '.ymcc-bootstrap-stage.json')
  if(!(Test-YmccModuleInteger $record.schemaVersion) -or $record.schemaVersion -ne 2 -or $record.owner -isnot [string] -or $record.owner -cne 'YMCC.InputPayloadBootstrap' -or
     $record.target -isnot [string] -or $record.target -ine $targetHome -or $record.packageVersion -isnot [string] -or [string]::IsNullOrWhiteSpace($record.packageVersion) -or
     $record.payloadSha256 -isnot [string] -or $record.payloadSha256 -cnotmatch '^[a-f0-9]{64}$' -or $record.phase -isnot [string] -or $record.phase -notin @('preparing','applying','committed') -or
     $record.managedPaths -isnot [array] -or $record.managedPaths.Count -lt 1 -or $record.managedPaths.Count -gt 10000){throw 'MODULE_BOOTSTRAP_STAGE_RECORD_INVALID'}
  $installedManifest=Read-YmccModuleJournalJson (Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName($targetHome)) 'YeManCC/module-package.json');Assert-YmccModuleManifest $installedManifest
  if($record.packageVersion -ceq $installedManifest.packageVersion -and $installedManifest.PSObject.Properties['compatibility'] -and $record.payloadSha256 -cne $installedManifest.compatibility.payloadSha256){throw 'MODULE_BOOTSTRAP_STAGE_PAYLOAD_IDENTITY_CHANGED'}
  if($record.PSObject.Properties['stageId'] -and ($record.stageId -isnot [string] -or $record.stageId -cne [IO.Path]::GetFileName($safe))){throw 'MODULE_BOOTSTRAP_STAGE_ID_INVALID'}
  $known=@{};foreach($name in $record.managedPaths){if($name -isnot [string] -or !(Test-YmccModuleFile $name) -or $name.Contains('//') -or $known.ContainsKey($name)){throw 'MODULE_BOOTSTRAP_PATH_INVENTORY_INVALID'};$known[$name]=$true}
  if($record.PSObject.Properties['postCommitCleanup']){
    $post=$record.postCommitCleanup
    if(!(Test-YmccModuleObject $post) -or $record.phase -cne 'committed' -or !(Test-YmccModuleInteger $post.schemaVersion) -or $post.schemaVersion -ne 1 -or $post.moduleManifestSha256 -isnot [string] -or $post.moduleManifestSha256 -cnotmatch '^[a-f0-9]{64}$' -or ($post.PSObject.Properties['settled'] -and $post.settled -isnot [bool])){throw 'MODULE_POSTCOMMIT_MARKER_INVALID'}
  }
  if($record.PSObject.Properties['cleanupProof']){
    $proof=$record.cleanupProof
    if(!(Test-YmccModuleObject $proof) -or !(Test-YmccModuleInteger $proof.schemaVersion) -or $proof.schemaVersion -ne 1 -or $proof.moduleManifestSha256 -isnot [string] -or $proof.moduleManifestSha256 -cnotmatch '^[a-f0-9]{64}$' -or $proof.managedReceiptSha256 -isnot [string] -or $proof.managedReceiptSha256 -cnotmatch '^[a-f0-9]{64}$'){throw 'MODULE_CLEANUP_PROOF_INVALID'}
  }
  if($record.PSObject.Properties['rollbackLedger']){
    $ledger=$record.rollbackLedger
    if(!(Test-YmccModuleObject $ledger) -or !(Test-YmccModuleInteger $ledger.schemaVersion) -or $ledger.schemaVersion -ne 1 -or $ledger.entries -isnot [array] -or $ledger.entries.Count -gt 10001 -or $record.phase -ceq 'preparing'){throw 'MODULE_ROLLBACK_LEDGER_INVALID'}
    $seen=@{};foreach($entry in $ledger.entries){
      if(!(Test-YmccModuleObject $entry) -or $entry.path -isnot [string] -or ($entry.path -cne '.ymcc-input-managed.json' -and !$known.ContainsKey($entry.path)) -or $seen.ContainsKey($entry.path) -or
         !$entry.PSObject.Properties['before'] -or !$entry.PSObject.Properties['after'] -or $entry.remove -isnot [bool]){throw 'MODULE_ROLLBACK_ENTRY_INVALID'}
      foreach($name in @('before','after')){if($null -ne $entry.$name -and ($entry.$name -isnot [string] -or $entry.$name -cnotmatch '^[a-f0-9]{64}$')){throw 'MODULE_ROLLBACK_HASH_INVALID'}}
      if(($entry.remove -and ($null -ne $entry.after -or !$entry.before)) -or (!$entry.remove -and !$entry.after)){throw 'MODULE_ROLLBACK_ACTION_INVALID'}
      $seen[$entry.path]=$true
    }
  }
  return $record
}
function Test-YmccBootstrapRollbackComplete([string]$Stage,[string]$Target,$Record){
  if($Record.PSObject.Properties['cleanupProof']){
    $null=Get-YmccBootstrapInstalledProof $Target;$root=[IO.Path]::GetDirectoryName((Resolve-YmccDeckyPath $Target))
    return ((Get-YmccDeckyHash (Resolve-YmccDeckyPath $root 'YeManCC/module-package.json')) -ceq $Record.cleanupProof.moduleManifestSha256 -and (Get-YmccDeckyHash (Resolve-YmccDeckyPath $Target '.ymcc-input-managed.json')) -ceq $Record.cleanupProof.managedReceiptSha256)
  }

  if(!$Record.PSObject.Properties['rollbackLedger']){return $true} # Historical v2 without a cleanup checkpoint.
  foreach($entry in $Record.rollbackLedger.entries){if($entry.before){
    $backup=Resolve-YmccDeckyPath $Stage ('rollback/'+$entry.path)
    if(Test-YmccModuleJournalExists $backup){if((Get-YmccDeckyHash $backup) -cne $entry.before){return $false}}
    else{
      $destination=Resolve-YmccDeckyPath $Target $entry.path
      if(!(Test-YmccModuleJournalExists $destination) -or (Get-YmccDeckyHash $destination) -cne $entry.before){return $false}
    }
  }}
  return $true
}
function Get-YmccBootstrapInstalledProof([string]$Target){
  $targetHome=Resolve-YmccDeckyPath $Target;$root=[IO.Path]::GetDirectoryName($targetHome)
  if($targetHome -ine (Resolve-YmccDeckyPath $root 'PowerControl')){throw 'MODULE_BOOTSTRAP_TARGET_INVALID'}
  $manifest=Read-YmccModuleJournalJson (Resolve-YmccDeckyPath $root 'YeManCC/module-package.json');Assert-YmccModuleManifest $manifest
  if(!(Test-YmccBootstrapPayloadCurrent $targetHome $manifest)){throw 'MODULE_BOOTSTRAP_INSTALLED_BYTES_UNCONFIRMED'}
  $receipt=Read-YmccModuleJournalJson (Resolve-YmccDeckyPath $targetHome '.ymcc-input-managed.json');Assert-YmccModuleManifest $receipt
  if($receipt.packageVersion -cne $manifest.packageVersion -or !(Test-YmccModuleInventoryEqual $manifest $receipt)){throw 'MODULE_BOOTSTRAP_INSTALLED_RECEIPT_UNCONFIRMED'}
  foreach($name in @('sourceKind','payloadHashScope','payloadSha256','scriptSha256')){
    if($manifest.PSObject.Properties['compatibility'] -and (!$receipt.PSObject.Properties['compatibility'] -or $manifest.compatibility.$name -ine $receipt.compatibility.$name)){throw 'MODULE_BOOTSTRAP_INSTALLED_IDENTITY_UNCONFIRMED'}
  }
  return $manifest
}
function Assert-YmccBootstrapStagesReadable([string]$Root,[string]$Target){
  # Called under the existing maintenance lease, after the incoming manifest
  # was verified. Previous transaction/rollback data is explicitly disposable.
  foreach($item in @(Get-ChildItem -LiteralPath $Root -Filter 'module-bootstrap-*' -Force -ErrorAction Stop)){
    if($item.Name -cnotmatch '^module-bootstrap-[a-f0-9]{32}$'){continue}
    Move-YmccLegacyUpdateMetadata $Root $item.Name
  }
  return 0
}
function Test-YmccBootstrapReceiptNeedsTransaction([string]$Target,$Manifest){
  $receipt=Resolve-YmccUpdateFileTarget $Target '.ymcc-input-managed.json'
  if(!(Test-YmccModuleJournalExists $receipt)){return $false}
  try{$previous=Read-YmccModuleJournalJson $receipt;Assert-YmccModuleManifest $previous}
  catch{Move-YmccLegacyUpdateMetadata $Target '.ymcc-input-managed.json';return $false}
  return !(Test-YmccModuleInventoryEqual $Manifest $previous)
}
function Write-YmccBootstrapStage([string]$Stage, [string]$Target, [string]$PackageVersion, [string]$PayloadSha256, [ValidateSet('preparing','applying','committed')][string]$Phase, [string[]]$ManagedPaths, [string]$PostCommitModuleSha256='', $TransactionPlan=$null, [switch]$SettlePostCommitCleanup) {
  $safe=Resolve-YmccDeckyPath $Stage;$targetHome=Resolve-YmccDeckyPath $Target
  if([IO.Path]::GetFileName($safe) -cnotmatch '^module-bootstrap-[a-f0-9]{32}$' -or $targetHome -ine (Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName($safe)) 'PowerControl') -or [string]::IsNullOrWhiteSpace($PackageVersion) -or $PayloadSha256 -cnotmatch '^[a-f0-9]{64}$'){throw 'MODULE_BOOTSTRAP_STAGE_BINDING_INVALID'}
  $installedManifest=Read-YmccModuleJournalJson (Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName($targetHome)) 'YeManCC/module-package.json');Assert-YmccModuleManifest $installedManifest
  if($PackageVersion -ceq $installedManifest.packageVersion -and $installedManifest.PSObject.Properties['compatibility'] -and $PayloadSha256 -cne $installedManifest.compatibility.payloadSha256){throw 'MODULE_BOOTSTRAP_STAGE_PAYLOAD_IDENTITY_CHANGED'}
  $marker=Resolve-YmccDeckyPath $safe '.ymcc-bootstrap-stage.json';$previous=$null
  if(Test-YmccModuleJournalExists $marker){$previous=Read-YmccBootstrapStageRecord $safe $targetHome}
  $paths=@{};foreach($name in @($ManagedPaths)+@($previous.managedPaths)) {if($null -eq $name){continue};if(!(Test-YmccModuleFile $name) -or $name.Contains('//')){throw 'MODULE_BOOTSTRAP_PATH_INVENTORY_INVALID'};$paths[$name]=$true}
  $ledger=$null
  if($TransactionPlan){
    if(!(Test-YmccModuleObject $TransactionPlan) -or $Phase -cne 'applying' -or $TransactionPlan.target -isnot [string] -or (Resolve-YmccDeckyPath $TransactionPlan.target) -ine $targetHome -or $TransactionPlan.entries -isnot [array] -or $TransactionPlan.entries.Count -gt 10001){throw 'MODULE_ROLLBACK_PLAN_BINDING_INVALID'}
    $entries=@();$seen=@{};foreach($entry in $TransactionPlan.entries){
      if(!(Test-YmccModuleObject $entry) -or $entry.path -isnot [string] -or ($entry.path -cne '.ymcc-input-managed.json' -and (!(Test-YmccModuleFile $entry.path) -or $entry.path.Contains('//'))) -or $seen.ContainsKey($entry.path) -or
         !$entry.PSObject.Properties['before'] -or !$entry.PSObject.Properties['after'] -or $entry.remove -isnot [bool]){throw 'MODULE_ROLLBACK_ENTRY_INVALID'}
      foreach($name in @('before','after')){if($null -ne $entry.$name -and ($entry.$name -isnot [string] -or $entry.$name -cnotmatch '^[a-f0-9]{64}$')){throw 'MODULE_ROLLBACK_HASH_INVALID'}}
      if(($entry.remove -and ($null -ne $entry.after -or !$entry.before)) -or (!$entry.remove -and !$entry.after)){throw 'MODULE_ROLLBACK_ACTION_INVALID'}
      $seen[$entry.path]=$true
      if($entry.path -cne '.ymcc-input-managed.json'){$paths[$entry.path]=$true}
      $entries+=@([pscustomobject]@{path=$entry.path;before=$entry.before;after=$entry.after;remove=$entry.remove})
    }
    $ledger=[pscustomobject]@{schemaVersion=1;entries=$entries}
  }
  if($paths.Count -lt 1 -or $paths.Count -gt 10000){throw 'MODULE_BOOTSTRAP_PATH_INVENTORY_INVALID'}
  if($previous){
    $order=@{preparing=0;applying=1;committed=2}
    if($order[$Phase] -lt $order[[string]$previous.phase]){throw 'MODULE_BOOTSTRAP_PHASE_REWIND_REFUSED'}
    if(($previous.packageVersion -cne $PackageVersion -or $previous.payloadSha256 -cne $PayloadSha256) -and !$SettlePostCommitCleanup){throw 'MODULE_BOOTSTRAP_STAGE_IDENTITY_CHANGED'}
    if($previous.PSObject.Properties['rollbackLedger']){
      if($ledger -and ($ledger|ConvertTo-Json -Depth 64 -Compress) -cne ($previous.rollbackLedger|ConvertTo-Json -Depth 64 -Compress)){throw 'MODULE_ROLLBACK_LEDGER_RESET_REFUSED'}
      $ledger=$previous.rollbackLedger
    }
    if(!(Test-YmccBootstrapRollbackComplete $safe $targetHome $previous)){throw 'MODULE_ROLLBACK_BACKUP_UNCONFIRMED'}
  }
  if($Phase -ceq 'committed' -and !$PostCommitModuleSha256){
    $proof=Get-YmccBootstrapInstalledProof $targetHome
    if($proof.packageVersion -cne $PackageVersion -or $proof.compatibility.payloadSha256 -cne $PayloadSha256){throw 'MODULE_BOOTSTRAP_COMMIT_IDENTITY_CHANGED'}
  }
  $record=[ordered]@{schemaVersion=2;owner='YMCC.InputPayloadBootstrap';stageId=[IO.Path]::GetFileName($safe);target=$targetHome;packageVersion=$PackageVersion;payloadSha256=$PayloadSha256;phase=$Phase;managedPaths=@($paths.Keys|Sort-Object)}
  if($ledger){$record['rollbackLedger']=$ledger}
  if($previous){foreach($property in $previous.PSObject.Properties){if($property.Name -notin @('schemaVersion','owner','stageId','target','packageVersion','payloadSha256','phase','managedPaths','rollbackLedger')){$record[$property.Name]=$property.Value}}}
  if($PostCommitModuleSha256){
    $manifestPath=Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName($targetHome)) 'YeManCC/module-package.json'
    $manifest=Read-YmccModuleJournalJson $manifestPath;Assert-YmccModuleManifest $manifest
    if($Phase -cne 'committed' -or $PostCommitModuleSha256 -cnotmatch '^[a-f0-9]{64}$' -or (Get-YmccDeckyHash $manifestPath) -cne $PostCommitModuleSha256 -or $manifest.packageVersion -cne $PackageVersion -or $manifest.compatibility.payloadSha256 -cne $PayloadSha256){throw 'MODULE_POSTCOMMIT_MARKER_INVALID'}
    $post=[ordered]@{schemaVersion=1;moduleManifestSha256=$PostCommitModuleSha256;settled=$false}
    if($previous -and $previous.PSObject.Properties['postCommitCleanup']){foreach($property in $previous.postCommitCleanup.PSObject.Properties){if($property.Name -notin @('schemaVersion','moduleManifestSha256','settled')){$post[$property.Name]=$property.Value}}}
    $record['postCommitCleanup']=$post
  }
  if($SettlePostCommitCleanup){
    if(!$previous -or !$previous.PSObject.Properties['postCommitCleanup'] -or $Phase -cne 'committed'){throw 'MODULE_POSTCOMMIT_SETTLEMENT_UNBOUND'}
    $previous.postCommitCleanup|Add-Member -NotePropertyName settled -NotePropertyValue $true -Force;$record['postCommitCleanup']=$previous.postCommitCleanup
  }
  $bytes=[Text.Encoding]::UTF8.GetBytes(($record|ConvertTo-Json -Depth 64 -Compress));if($bytes.Length -gt 4194304){throw 'MODULE_BOOTSTRAP_MARKER_TOO_LARGE'}
  $sha=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  Set-YmccDeckyAtomic $safe '.ymcc-bootstrap-stage.json' $null $bytes $hash
}
function Test-YmccBootstrapPayloadCurrent([string]$Target, $Manifest) {
  Assert-YmccModuleManifest $Manifest
  foreach($entry in $Manifest.files) {
    # A bad leaf is repairable by the validated new package. Its ancestors must
    # still be ordinary directories before the slow repair path is selected.
    $parent=[IO.Path]::GetDirectoryName($entry.path.Replace('/','\')).Replace('\','/')
    $safeParent=Resolve-YmccDeckyPath $Target $parent
    $leaf=Get-Item -LiteralPath (Join-Path $safeParent ([IO.Path]::GetFileName($entry.path))) -Force -ErrorAction SilentlyContinue
    if($leaf -and ($leaf.PSIsContainer -or ($leaf.Attributes -band [IO.FileAttributes]::ReparsePoint))){return $false}
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
    $record=Read-YmccBootstrapStageRecord $safe $Target
    if(!(Test-YmccBootstrapRollbackComplete $safe $Target $record)){return $false}
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
          if($relative -ceq 'rollback/.ymcc-input-managed.json'){$savedReceipt=Read-YmccModuleJournalJson $item.FullName;Assert-YmccModuleManifest $savedReceipt}
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
function Test-YmccBootstrapPostCommitSettled([string]$Root,[string]$Target,$Record){
  if(!$Record.PSObject.Properties['postCommitCleanup']){return $true}
  if(!$Record.postCommitCleanup.PSObject.Properties['settled'] -or !$Record.postCommitCleanup.settled){return $false}
  try{
    $manifest=Get-YmccBootstrapInstalledProof $Target;$id=Assert-YmccPostCommitHcLayout $Root $manifest
    $runtimeRoot=Resolve-YmccDeckyPath $Target 'handheldcompanion-runtime'
    foreach($entry in @(Get-ChildItem -LiteralPath $runtimeRoot -Directory -Force -ErrorAction Stop)){if($entry.Name -cne $id -and $entry.Name -cmatch '^(YMCC-|HC-)[A-Za-z0-9_.-]+$'){return $false}}
    return (Test-YmccPostCommitHostReleased $Root 0)
  }catch{return $false}
}
function Remove-YmccBootstrapStage([string]$Stage, [string]$Target) {
  $tree=Test-YmccBootstrapStageOwned $Stage $Target -Snapshot
  if(!$tree){return $false}
  $root=[IO.Path]::GetDirectoryName($tree.root)
  if(!(Test-YmccBootstrapPostCommitSettled $root $Target $tree.record)){return $false}
  $hasRollback=@($tree.files|Where-Object{$_.Substring($tree.root.Length+1).Replace('\','/').StartsWith('rollback/')}).Count -gt 0
  if($tree.record.phase -cne 'preparing' -or $tree.record.PSObject.Properties['rollbackLedger'] -or $hasRollback){$null=Get-YmccBootstrapInstalledProof $Target}
  # Never infer settlement from phase. Keep the exact marker until all data is
  # gone; if final empty-directory removal fails, restore those same bytes.
  $marker=Resolve-YmccDeckyPath $tree.root '.ymcc-bootstrap-stage.json'
  if($tree.record.phase -cne 'preparing' -or $tree.record.PSObject.Properties['rollbackLedger'] -or $hasRollback){
    $checkpoint=[pscustomobject]@{schemaVersion=1;moduleManifestSha256=(Get-YmccDeckyHash (Resolve-YmccDeckyPath $root 'YeManCC/module-package.json'));managedReceiptSha256=(Get-YmccDeckyHash (Resolve-YmccDeckyPath $Target '.ymcc-input-managed.json'))}
    $tree.record|Add-Member -NotePropertyName cleanupProof -NotePropertyValue $checkpoint -Force
    $bytes=[Text.Encoding]::UTF8.GetBytes(($tree.record|ConvertTo-Json -Depth 64 -Compress));if($bytes.Length -gt 4194304){throw 'MODULE_BOOTSTRAP_MARKER_TOO_LARGE'}
    $sha=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
    Set-YmccDeckyAtomic $tree.root '.ymcc-bootstrap-stage.json' $null $bytes $hash
  }
  $ownershipBytes=[IO.File]::ReadAllBytes($marker)
  foreach($file in $tree.files){Remove-YmccBootstrapStageFile $tree.root $file}
  foreach($folder in @($tree.directories|Sort-Object {$_.Length} -Descending)){
    $relative=$folder.Substring($tree.root.Length+1).Replace('\','/');$safe=Resolve-YmccDeckyPath $tree.root $relative
    if([IO.Directory]::Exists($safe)){[IO.Directory]::Delete($safe,$false)}
  }
  try{
    Remove-YmccBootstrapStageFile $tree.root $marker
    [IO.Directory]::Delete($tree.root,$false)
  }catch{
    if([IO.Directory]::Exists($tree.root) -and !(Test-YmccModuleJournalExists $marker)){
      $sha=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($sha.ComputeHash($ownershipBytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
      Set-YmccDeckyAtomic $tree.root '.ymcc-bootstrap-stage.json' $null $ownershipBytes $hash
    }
    throw
  }
  return $true
}
function Remove-YmccBootstrapStages([string]$InstallRoot, [string]$Target, [ValidateRange(1,16)][int]$Maximum=8) {
  $root=Resolve-YmccDeckyPath $InstallRoot;$targetHome=Resolve-YmccDeckyPath $Target
  if($targetHome -ine (Resolve-YmccDeckyPath $root 'PowerControl')){throw 'MODULE_BOOTSTRAP_CLEANUP_TARGET_INVALID'}
  $candidates=@(Get-ChildItem -LiteralPath $root -Directory -Filter 'module-bootstrap-*' -Force -ErrorAction Stop|Select-Object -First 64)
  try{$null=Get-YmccBootstrapInstalledProof $targetHome}catch{return [pscustomobject]@{removed=0;preserved=$candidates.Count;confirmed=$false}}
  $removed=0;$preserved=0
  foreach($entry in $candidates){
    if($removed -ge $Maximum){break}
    try{$candidate=Resolve-YmccDeckyPath $root $entry.Name}catch{++$preserved;continue}
    if(!(Test-YmccBootstrapStageOwned $candidate $targetHome)){++$preserved;continue}
    if($candidate -ieq $root -or !$candidate.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'MODULE_BOOTSTRAP_CLEANUP_PATH_INVALID'}
    try{if(Remove-YmccBootstrapStage $candidate $targetHome){++$removed}else{++$preserved}}catch{++$preserved}
  }
  return [pscustomobject]@{removed=$removed;preserved=$preserved;confirmed=$true}
}
function Sync-YmccBootstrapReceipt([string]$Target, $Manifest) {
  Assert-YmccModuleManifest $Manifest
  $receipt=Resolve-YmccUpdateFileTarget $Target '.ymcc-input-managed.json';$previous=$null
  if(Test-YmccModuleJournalExists $receipt){
    try{$previous=Read-YmccModuleJournalJson $receipt;Assert-YmccModuleManifest $previous}
    catch{Move-YmccLegacyUpdateMetadata $Target '.ymcc-input-managed.json';$previous=$null}
    if($previous -and !(Test-YmccModuleInventoryEqual $Manifest $previous)){Move-YmccLegacyUpdateMetadata $Target '.ymcc-input-managed.json';$previous=$null}
  }
  if(!(Test-YmccBootstrapPayloadCurrent $Target $Manifest)){throw 'MODULE_RECEIPT_PAYLOAD_CHANGED'}
  $merged=Merge-YmccModuleManifestAnnotations $Manifest $previous
  $bytes=[Text.Encoding]::UTF8.GetBytes(($merged|ConvertTo-Json -Depth 64 -Compress));if($bytes.Length -gt 4194304){throw 'MODULE_RECEIPT_TOO_LARGE'}
  $sha=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  if($previous -and (Get-YmccDeckyHash $receipt) -ceq $hash){return}
  Set-YmccDeckyAtomic $Target '.ymcc-input-managed.json' $null $bytes $hash
}

# This is the existing module-bootstrap stage lane. No new watcher or updater.
function Read-YmccBootstrapSmallJson([string]$Path){
  $file=Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName($Path)) ([IO.Path]::GetFileName($Path))
  if(![IO.File]::Exists($file) -or ([IO.FileInfo]$file).Length -gt 4194304){throw 'MODULE_POSTCOMMIT_MANIFEST_MISSING_OR_TOO_LARGE'}
  return (Read-YmccModuleJournalJson $file)
}
function Register-YmccPostCommitHcCleanup([string]$Root,[string]$Target,$Manifest,[string]$ManifestPath,[string]$CommitState,[int]$OwnerPid){
  if($CommitState){
    $commit=Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($CommitState))) ([IO.Path]::GetFileName($CommitState))
    $state=Read-YmccBootstrapSmallJson $commit
    if(!(Test-YmccModuleInteger $state.pid) -or $state.phase -isnot [string] -or $state.version -isnot [string]){throw 'MODULE_POSTCOMMIT_STATE_NOT_CONFIRMED'}
    foreach($schema in @('schemaVersion','schema')){if($state.PSObject.Properties[$schema] -and (!(Test-YmccModuleInteger $state.$schema) -or $state.$schema -ne 1)){throw 'MODULE_POSTCOMMIT_STATE_NOT_CONFIRMED'}}
    foreach($binding in @('installRoot','root','target')){if($state.PSObject.Properties[$binding] -and ($state.$binding -isnot [string] -or (Resolve-YmccDeckyPath $state.$binding) -ine (Resolve-YmccDeckyPath $Root))){throw 'MODULE_POSTCOMMIT_STATE_NOT_CONFIRMED'}}
    if($state.PSObject.Properties['payloadSha256'] -and ($state.payloadSha256 -isnot [string] -or $state.payloadSha256 -cne $Manifest.compatibility.payloadSha256)){throw 'MODULE_POSTCOMMIT_STATE_NOT_CONFIRMED'}
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
  if(!(Test-YmccModuleInteger $layout.schemaVersion) -or $layout.packageId -isnot [string] -or $layout.packageVersion -isnot [string] -or !(Test-YmccModuleObject $layout.rules) -or !(Test-YmccModuleObject $binding) -or !(Test-YmccModuleInteger $binding.schemaVersion) -or !(Test-YmccModuleInteger $binding.fileCount) -or $binding.fileCount -lt 1 -or $binding.fileCount -gt 50000 -or $binding.directory -isnot [string] -or $binding.runtimeDirectory -isnot [string] -or $binding.manifestSha256 -isnot [string] -or $binding.runtimeManifestSha256 -isnot [string]){throw 'MODULE_POSTCOMMIT_LAYOUT_TYPE_INVALID'}
  if($layout.schemaVersion -ne 1 -or $layout.packageId -cne 'yemancc-update' -or [string]$layout.packageVersion -cne [string]$ModuleManifest.packageVersion -or [string]$binding.directory -cne 'fan-host-v2' -or $binding.schemaVersion -ne 2 -or [string]$binding.manifestSha256 -notmatch '^[a-fA-F0-9]{64}$' -or [string]$binding.runtimeManifestSha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'MODULE_POSTCOMMIT_LAYOUT_INVALID'}
  $hostRoot=Resolve-YmccDeckyPath $pc 'fan-host-v2';$hostPath=Resolve-YmccDeckyPath $hostRoot 'YeManFanHost.payload.json'
  if((Get-YmccDeckyHash $hostPath) -ine [string]$binding.manifestSha256){throw 'MODULE_POSTCOMMIT_HOST_MANIFEST_CHANGED'}
  $hostManifest=Read-YmccBootstrapSmallJson $hostPath;$id=[string]$hostManifest.runtimeId
  if(!(Test-YmccModuleInteger $hostManifest.schemaVersion) -or $hostManifest.runtimeId -isnot [string] -or $hostManifest.runtimeManifest -isnot [string] -or $hostManifest.files -isnot [array]){throw 'MODULE_POSTCOMMIT_HOST_TYPE_INVALID'}
  if($hostManifest.schemaVersion -ne 2 -or $id -cnotmatch '^(YMCC-|HC-)[A-Za-z0-9_.-]+$'){throw 'MODULE_POSTCOMMIT_RUNTIME_BINDING_INVALID'}
  $runtimeDirectory=([string]$binding.runtimeDirectory).Replace([char]92,[char]47)
  $runtimeReference=([string]$hostManifest.runtimeManifest).Replace([char]92,[char]47)
  if($runtimeDirectory -cne ('handheldcompanion-runtime/'+$id) -or $runtimeReference -cne ('../handheldcompanion-runtime/'+$id+'/HandheldCompanion.runtime.json')){throw 'MODULE_POSTCOMMIT_RUNTIME_BINDING_INVALID'}
  $runtime=Resolve-YmccDeckyPath $pc ('handheldcompanion-runtime/'+$id);$runtimePath=Resolve-YmccDeckyPath $runtime 'HandheldCompanion.runtime.json'
  if((Get-YmccDeckyHash $runtimePath) -ine [string]$binding.runtimeManifestSha256){throw 'MODULE_POSTCOMMIT_RUNTIME_MANIFEST_CHANGED'}
  $runtimeManifest=Read-YmccBootstrapSmallJson $runtimePath
  if(!(Test-YmccModuleInteger $runtimeManifest.schemaVersion) -or $runtimeManifest.runtimeId -isnot [string] -or $runtimeManifest.files -isnot [array]){throw 'MODULE_POSTCOMMIT_RUNTIME_TYPE_INVALID'}
  if($runtimeManifest.schemaVersion -ne 1 -or [string]$runtimeManifest.runtimeId -cne $id -or @($hostManifest.files).Count -ne [int]$binding.fileCount){throw 'MODULE_POSTCOMMIT_RUNTIME_IDENTITY_INVALID'}
  foreach($group in @(@{root=$hostRoot;manifest=$hostManifest;metadata='YeManFanHost.payload.json'},@{root=$runtime;manifest=$runtimeManifest;metadata='HandheldCompanion.runtime.json'})){
    Assert-YmccHcTree $group.root
    $files=@($group.manifest.files);if($files.Count -lt 1 -or $files.Count -gt 50000){throw 'MODULE_POSTCOMMIT_FILE_COUNT_INVALID'}
    $known=@{}
    foreach($file in $files){
      if(!(Test-YmccModuleObject $file) -or $file.path -isnot [string] -or $file.sha256 -isnot [string] -or ($file.PSObject.Properties['bytes'] -and (!(Test-YmccModuleInteger $file.bytes) -or $file.bytes -lt 0 -or $file.bytes -gt 2147483648))){throw 'MODULE_POSTCOMMIT_FILE_TYPE_INVALID'}
      $name=[string]$file.path;$key=$name.Replace([char]47,[char]92)
      if(!$name -or $known.ContainsKey($key) -or $name -ieq $group.metadata -or [string]$file.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'MODULE_POSTCOMMIT_FILE_INVALID'}
      $path=Resolve-YmccDeckyPath $group.root $name
      if(![IO.File]::Exists($path) -or ($file.PSObject.Properties['bytes'] -and ([IO.FileInfo]$path).Length -ne $file.bytes) -or (Get-YmccDeckyHash $path) -ine [string]$file.sha256){throw ('MODULE_POSTCOMMIT_FILE_CHANGED: '+$name)}
      $known[$key]=$true
    }
    $actual=@(Get-ChildItem -LiteralPath $group.root -Recurse -File -Force|ForEach-Object{$_.FullName.Substring($group.root.Length+1)}|Where-Object{$_ -ine $group.metadata})
    if(Compare-Object @($known.Keys) $actual){throw 'MODULE_POSTCOMMIT_INVENTORY_CHANGED'}
  }
  if($runtimeManifest.PSObject.Properties['sharedDependencies']){
    $shared=$runtimeManifest.sharedDependencies
    if(!(Test-YmccModuleObject $shared) -or $shared.directory -isnot [string] -or $shared.files -isnot [array]){throw 'MODULE_POSTCOMMIT_SHARED_TYPE_INVALID'}
    if([string]$shared.directory -cne 'PowerControl/ymcc-shared-runtime' -or @($shared.files).Count -lt 1 -or @($shared.files).Count -gt 128){throw 'MODULE_POSTCOMMIT_SHARED_IDENTITY_INVALID'}
    $sharedRoot=Resolve-YmccDeckyPath $pc 'ymcc-shared-runtime';Assert-YmccHcTree $sharedRoot;$known=@{}
    foreach($file in @($shared.files)){
      if(!(Test-YmccModuleObject $file) -or $file.path -isnot [string] -or $file.sha256 -isnot [string] -or !(Test-YmccModuleInteger $file.bytes) -or $file.bytes -lt 0 -or $file.bytes -gt 2147483648){throw 'MODULE_POSTCOMMIT_SHARED_ENTRY_TYPE_INVALID'}
      $name=[string]$file.path
      if($name -notmatch '^[A-Za-z0-9_.-]+[.]dll$' -or $known.ContainsKey($name) -or [string]$file.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'MODULE_POSTCOMMIT_SHARED_ENTRY_INVALID'}
      $path=Resolve-YmccDeckyPath $sharedRoot $name
      if(![IO.File]::Exists($path) -or ([IO.FileInfo]$path).Length -ne [long]$file.bytes -or (Get-YmccDeckyHash $path) -ine [string]$file.sha256){throw 'MODULE_POSTCOMMIT_SHARED_FILE_CHANGED'}
      $known[$name]=$true
    }
  }
  return $id
}
# Retire only the exact apphost shipped by public037, after the update has
# committed. Unknown bytes and occupied/read-only files are preserved. Parent
# handles fence directory replacement; hashing and deletion use the SAME handle.
function Remove-YmccPublic037BatteryAppHost([string]$Root){
  $file=Resolve-YmccDeckyPath $Root 'PowerControl/battery-host/YeManBatteryHost.exe'
  if(!(Test-YmccModuleJournalExists $file)){return}
  $home=Resolve-YmccDeckyPath $Root 'PowerControl/battery-host'
  $payload=Read-YmccBootstrapSmallJson (Resolve-YmccDeckyPath $home 'battery-host.payload.json')
  if($payload.component -cne 'YeManBatteryHost' -or $payload.execution -cne 'in-process' -or @($payload.files|Where-Object{$_.path -ieq 'YeManBatteryHost.exe'}).Count){throw 'BATTERY_APPHOST_RETIREMENT_COMPONENT_UNCONFIRMED'}
  $dll=@($payload.files|Where-Object{$_.path -ceq 'YeManBatteryHost.dll'})
  if($dll.Count -ne 1 -or [string]$dll[0].sha256 -notmatch '^[a-fA-F0-9]{64}$' -or (Get-YmccDeckyHash (Resolve-YmccDeckyPath $home 'YeManBatteryHost.dll')) -ine [string]$dll[0].sha256){throw 'BATTERY_APPHOST_RETIREMENT_COMPONENT_CHANGED'}
  if(-not ('Ymcc.Public037BatteryAppHost' -as [type])){
    Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.ComponentModel;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;
namespace Ymcc {
 public static class Public037BatteryAppHost {
  [StructLayout(LayoutKind.Sequential)] struct Info {
   public uint Attributes; public System.Runtime.InteropServices.ComTypes.FILETIME Creation, Access, Write;
   public uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
  }
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string path,uint access,uint share,IntPtr security,uint mode,uint flags,IntPtr template);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle handle,out Info info);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetFileInformationByHandle(SafeFileHandle handle,int kind,ref byte disposition,uint size);
  static string Native(string path) {return path.StartsWith(@"\\")?@"\\?\UNC\"+path.Substring(2):@"\\?\"+path;}
  static void Check(SafeFileHandle handle,bool directory) {
   Info info; if(!GetFileInformationByHandle(handle,out info))throw new Win32Exception(Marshal.GetLastWin32Error());
   if((info.Attributes&0x400)!=0 || ((info.Attributes&0x10)!=0)!=directory)throw new IOException("BATTERY_APPHOST_RETIREMENT_PATH_UNSAFE");
  }
  public static string Retire(string path) {
   path=Path.GetFullPath(path);var parents=new Stack<string>();var holds=new List<SafeFileHandle>();
   for(string at=Path.GetDirectoryName(path);at!=null;at=Path.GetDirectoryName(at))parents.Push(at);
   try {
    while(parents.Count!=0){
     var handle=CreateFileW(Native(parents.Pop()),0x80,3,IntPtr.Zero,3,0x02200000,IntPtr.Zero);
     if(handle.IsInvalid){int error=Marshal.GetLastWin32Error();handle.Dispose();throw new Win32Exception(error);}
     holds.Add(handle);Check(handle,true);
    }
    using(var handle=CreateFileW(Native(path),0x80010000,1,IntPtr.Zero,3,0x00200000,IntPtr.Zero)){
     if(handle.IsInvalid){int error=Marshal.GetLastWin32Error();if(error==2)return "absent";throw new Win32Exception(error);}
     Check(handle,false);
     using(var stream=new FileStream(handle,FileAccess.Read)){
      if(stream.Length!=162304)return "preserved-unrecognized";
      string hash;using(var sha=SHA256.Create()){hash=BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant();}
      if(hash!="6c3fd282b1dbe117c05a9164098085ada4c51b918d60860f595fdb3aa4d5ab67")return "preserved-unrecognized";
      byte disposition=1;
      if(!SetFileInformationByHandle(handle,4,ref disposition,1))throw new Win32Exception(Marshal.GetLastWin32Error());
     }
    }
    return "retired-public037";
   } finally {for(int i=holds.Count-1;i>=0;--i)holds[i].Dispose();}
  }
 }
}
"@
  }
  $result=[Ymcc.Public037BatteryAppHost]::Retire($file)
  if($result -ceq 'preserved-unrecognized'){Write-Warning 'BATTERY_APPHOST_PRESERVED_UNKNOWN_IDENTITY'}
  else{Write-Output ('BATTERY_APPHOST_RETIREMENT='+$result)}
}

function Complete-YmccPostCommitHcCleanup([string]$Root,[string]$Target,$Manifest,[string]$Stage,[int]$OwnerPid){
  if(!$Stage){return}
  try{
    if(!(Test-YmccBootstrapPayloadCurrent $Target $Manifest)){throw 'MODULE_POSTCOMMIT_INPUT_NOT_READY'}
    $id=Assert-YmccPostCommitHcLayout $Root $Manifest
    if(!(Test-YmccPostCommitHostReleased $Root $OwnerPid)){throw 'MODULE_POSTCOMMIT_OLD_HOST_STILL_RUNNING'}
    # Reuse the canonical deletion function and its direct-child path checks.
    $runtimeRoot=Resolve-YmccDeckyPath $Target 'handheldcompanion-runtime'
    foreach($directory in @(Get-ChildItem -LiteralPath $runtimeRoot -Directory -Force -ErrorAction Stop)){
      if($directory.Name -ceq $id -or $directory.Name -cnotmatch '^(YMCC-|HC-)[A-Za-z0-9_.-]+$'){continue}
      Assert-YmccHcTree $directory.FullName
      $old=Read-YmccModuleJournalJson (Resolve-YmccDeckyPath $directory.FullName 'HandheldCompanion.runtime.json')
      if(!(Test-YmccModuleInteger $old.schemaVersion) -or $old.schemaVersion -ne 1 -or $old.runtimeId -isnot [string] -or $old.runtimeId -cne $directory.Name -or $old.files -isnot [array]){throw 'MODULE_POSTCOMMIT_OLD_OWNERSHIP_UNKNOWN'}
    }
    Remove-YmccSupersededHcRuntimes $Target $id -Maximum 8
    Remove-YmccPublic037BatteryAppHost $Root
    foreach($entry in @(Get-ChildItem -LiteralPath $Root -Directory -Filter 'module-bootstrap-*' -Force|Select-Object -First 64)){
      $owned=Test-YmccBootstrapStageOwned $entry.FullName $Target -Snapshot
      if($owned -and $owned.record.PSObject.Properties['postCommitCleanup']){
        Write-YmccBootstrapStage $entry.FullName $Target ([string]$Manifest.packageVersion) ([string]$Manifest.compatibility.payloadSha256) 'committed' @($Manifest.files.path) -SettlePostCommitCleanup
      }
    }
  }catch{Write-Warning ('MODULE_HC_CLEANUP_DEFERRED: '+$_.Exception.Message)}
}
