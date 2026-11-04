<#
.SYNOPSIS
  Explicit developer deployment of one verified canonical mainline package.
.DESCRIPTION
  A local installation transaction, including same-version rebuilds. The public
  updater's version/recovery rules and ordinary deploy's existing-Fan guard are
  unchanged. The incoming immutable tuple follows the production updater. This
  tool never controls the main application lifecycle, installs a driver, or changes a
  device setting. Original deploy retains its bounded helper/selftest executions.
  The caller must normally exit YMCC before using -Apply.
  Without -Apply, validate and report the plan without installed-tree writes.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$ExpectedArchiveSha256,
  [Parameter(Mandatory=$true)][string]$PreviousArchive,
  [Parameter(Mandatory=$true)][string]$PreviousArchiveSha256,
  [Parameter(Mandatory=$true)][string]$EvidenceDir,
  [string]$WorkspaceRoot=$env:YEMAN_WORKSPACE_ROOT,
  [string]$InstallRoot='C:\SOFT\YeMan\YeManCC',
  [string]$CompletedLocalRepairReceipt='',
  [string]$CompletedLocalRepairSha256='',
  [switch]$Apply
)
$ErrorActionPreference='Stop'
$localProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
if(!$WorkspaceRoot){$WorkspaceRoot=Join-Path $localProject '..\..'}
$WorkspaceRoot=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\')
$InstallRoot=[IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
# This development entry owns only the user's explicitly authorized location.
if($InstallRoot -ine 'C:\SOFT\YeMan\YeManCC'){throw 'LOCAL_DEPLOY_UNAUTHORIZED_INSTALL_ROOT'}
$localInstallParent=Split-Path -Parent $InstallRoot
$localEvidence=[IO.Path]::GetFullPath($EvidenceDir).TrimEnd('\')
if($localEvidence -ieq $localInstallParent -or $localEvidence.StartsWith($localInstallParent+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LOCAL_DEPLOY_EVIDENCE_INSIDE_INSTALL'}
$localEvidenceRoot=Join-Path (Split-Path -Parent $WorkspaceRoot) 'Testing\YMCC\Evidence'
if(!$localEvidence.StartsWith($localEvidenceRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LOCAL_DEPLOY_EVIDENCE_OUTSIDE_CORE'}
. (Join-Path $PSScriptRoot 'release-export-identity.ps1')
. (Join-Path $localProject 'native\decky_package_maintenance.ps1')
. (Join-Path $PSScriptRoot 'fan-host-layout.ps1')
. (Join-Path $PSScriptRoot 'local-fan-transition.ps1')
. (Join-Path $PSScriptRoot 'settings-baseline-contract.ps1')
function New-LocalDeploymentReceipt($Value){
  $path=Join-Path $localEvidence 'LOCAL-DEPLOYMENT.json'
  $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 32)+"`n");$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}
function Write-LocalDeploymentReceipt($Value){
  $path=Join-Path $localEvidence 'LOCAL-DEPLOYMENT.json'
  $temporary=$path+'.tmp-'+[guid]::NewGuid().ToString('N')
  try{
    $stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 32)+"`n");$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
    [IO.File]::Replace($temporary,$path,($path+'.previous-'+[guid]::NewGuid().ToString('N')+'.json'))
  }finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}
function Close-LocalDeploymentScope($Receipt,[bool]$ReceiptOwned,$ModuleLease,$WorkspaceMutex){
  try{if($ReceiptOwned){Write-LocalDeploymentReceipt $Receipt}}
  finally{try{if($ModuleLease){Exit-YmccModuleMaintenance $ModuleLease}}finally{if($WorkspaceMutex){try{$WorkspaceMutex.ReleaseMutex()}finally{$WorkspaceMutex.Dispose()}}}}
}
function Resolve-LocalDeploymentChild([string]$Root,[string]$Relative){
  $safe=ConvertTo-ExportRelativePath $Relative
  $base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
  $path=[IO.Path]::GetFullPath((Join-Path $base $safe.Replace('/','\')))
  if(!$path.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LOCAL_DEPLOY_PATH_ESCAPE'}
  $cursor=$path
  while($cursor){
    if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw ('LOCAL_DEPLOY_REPARSE_POINT: '+$cursor)}}
    $cursor=[IO.Path]::GetDirectoryName($cursor)
  }
  return $path
}
function Get-LocalDeploymentTree([string]$Root){
  $result=[Collections.Generic.List[object]]::new()
  foreach($name in @('YeManCC','PowerControl')){
    $localTreeRoot=Resolve-LocalDeploymentChild $Root $name
    if(!(Test-Path -LiteralPath $localTreeRoot -PathType Container)){throw ('LOCAL_DEPLOY_INSTALLED_ROOT_MISSING: '+$name)}
    $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($localTreeRoot)
    while($pending.Count){
      $dir=$pending.Pop()
      foreach($item in @(Get-ChildItem -LiteralPath $dir -Force)){
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw ('LOCAL_DEPLOY_REPARSE_POINT: '+$item.FullName)}
        $relative=$item.FullName.Substring($Root.Length+1).Replace('\','/')
        if($item.PSIsContainer){if($item.Name -notlike '.deployment-backup-*'){$pending.Push($item.FullName)};continue}
        if($result.Count -ge 50000){throw 'LOCAL_DEPLOY_INVENTORY_LIMIT'}
        $result.Add([pscustomobject]@{path=$relative;bytes=[int64]$item.Length;sha256=(Get-ExportFileHash $item.FullName)})
      }
    }
  }
  return @($result.ToArray()|Sort-Object path)
}
function Test-LocalDeploymentImmutable([string]$Relative){
  return $Relative -match '^PowerControl/(fan-host-v2|handheldcompanion-runtime|ymcc-shared-runtime)(/|$)'
}
function Assert-LocalDeploymentStopped {
  # Name lookups are bounded to the installed product's known resident roles.
  # Never stop a process here, and reject unknown paths rather than guessing.
  foreach($name in @('YeManCC','CustomSteamLibrary','SteamArtworkLab','YeManRecoveryService','YeManControllerVisibilityProbe','YeManFanHost','YeManInputHost','YeManLightSetter','YeManBatteryHost','YeManTdpCtl')){
    foreach($process in @(Get-Process -Name $name -ErrorAction SilentlyContinue)){
      try{$image=$process.MainModule.FileName;if(!$image){throw 'unknown process path'}
        if([IO.Path]::GetFullPath($image).StartsWith($localInstallParent+'\',[StringComparison]::OrdinalIgnoreCase)){throw ('LOCAL_DEPLOY_PRODUCT_STILL_RUNNING: '+$name+'/'+$process.Id)}
      }finally{$process.Dispose()}
    }
  }
}
function Save-LocalDeploymentPreimage($Index,[string]$Backup){
  foreach($entry in $Index){
    if(Test-LocalDeploymentImmutable $entry.path){continue}
    $source=Resolve-LocalDeploymentChild $localInstallParent $entry.path
    $destination=Resolve-LocalDeploymentChild $Backup $entry.path
    if((Get-ExportFileHash $source) -cne $entry.sha256){throw ('LOCAL_DEPLOY_PREIMAGE_CHANGED: '+$entry.path)}
    [IO.Directory]::CreateDirectory((Split-Path -Parent $destination))|Out-Null
    [IO.File]::Copy($source,$destination,$false)
    if((Get-ExportFileHash $destination) -cne $entry.sha256){throw ('LOCAL_DEPLOY_PREIMAGE_UNCONFIRMED: '+$entry.path)}
  }
}
function Restore-LocalDeploymentOrdinary($Before,$Incoming,[string]$Backup){
  $old=@{};foreach($entry in $Before){$old[$entry.path]=$entry}
  $new=@{};foreach($entry in $Incoming){$new[$entry.path]=$entry}
  $failures=[Collections.Generic.List[string]]::new()
  foreach($entry in $Before){
    if(Test-LocalDeploymentImmutable $entry.path){continue}
    try{
      $target=Resolve-LocalDeploymentChild $localInstallParent $entry.path
      $backupFile=Resolve-LocalDeploymentChild $Backup $entry.path
      if((Get-ExportFileHash $backupFile) -cne $entry.sha256){throw 'backup changed'}
      if(Test-Path -LiteralPath $target -PathType Leaf){
        $actual=Get-ExportFileHash $target
        if($actual -ceq $entry.sha256){continue}
        if(!$new.ContainsKey($entry.path) -or $actual -cne $new[$entry.path].sha256){throw 'later or unknown content preserved'}
      }elseif(Test-Path -LiteralPath $target){throw 'target is no longer a file'}
      [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
      [IO.File]::Copy($backupFile,$target,$true)
      if((Get-ExportFileHash $target) -cne $entry.sha256){throw 'restore hash mismatch'}
    }catch{$failures.Add($entry.path+': '+$_.Exception.Message)}
  }
  foreach($entry in $Incoming){
    if($old.ContainsKey($entry.path) -or (Test-LocalDeploymentImmutable $entry.path)){continue}
    try{
      $target=Resolve-LocalDeploymentChild $localInstallParent $entry.path
      if(Test-Path -LiteralPath $target -PathType Leaf){if((Get-ExportFileHash $target) -cne $entry.sha256){throw 'later added content preserved'};[IO.File]::Delete($target)}
      elseif(Test-Path -LiteralPath $target){throw 'added target became a directory'}
    }catch{$failures.Add($entry.path+': '+$_.Exception.Message)}
  }
  return [pscustomobject]@{ordinaryPreimageRestored=($failures.Count -eq 0);failures=@($failures.ToArray());immutableRollbackSkippedByPolicy=$true}
}
function Get-LocalDeploymentDerivedDeckyPlan([string]$SourceDecky,[string]$LoaderLock,[string]$InstalledDecky){
  # Reuse the exact production manifest and serialization used by the original
  # Update-InstalledDeckyReceipt. This receipt is derived owned metadata, not a
  # package file or a user preference. Loader/settings ownership is unchanged.
  $manifest=New-YmccDeckyManifest $SourceDecky $LoaderLock
  $bytes=[Text.Encoding]::UTF8.GetBytes(($manifest|ConvertTo-Json -Depth 8 -Compress))
  $stream=[IO.MemoryStream]::new($bytes)
  try{$hash=Get-ExportHash $stream}finally{$stream.Dispose()}
  $before=[Collections.Generic.List[object]]::new();$retired=[Collections.Generic.List[string]]::new()
  foreach($name in @('.ymcc-managed.receipt','.ymcc-managed.json')){
    $path=Resolve-YmccDeckyPath $InstalledDecky $name
    if([IO.Directory]::Exists($path)){throw 'LOCAL_DEPLOY_DERIVED_RECEIPT_IS_DIRECTORY'}
    $exists=[IO.File]::Exists($path);$previousHash=$null
    if($exists){
      if((Get-Item -LiteralPath $path).Length -gt 131072){throw 'LOCAL_DEPLOY_DERIVED_RECEIPT_TOO_LARGE'}
      $previousHash=Get-YmccDeckyHash $path
      $previous=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
      Assert-YmccDeckyManifest $previous -Previous
      if([version]$previous.loaderVersion.Substring(1) -gt [version]$manifest.loaderVersion.Substring(1)){throw 'DECKY_PACKAGE_DOWNGRADE_REFUSED_UPDATE_YMCC'}
      if((Get-YmccDeckyHash $path) -cne $previousHash){throw 'LOCAL_DEPLOY_DERIVED_PREIMAGE_CHANGED'}
      if($name -ceq '.ymcc-managed.json'){$retired.Add('PowerControl/decky/'+$name)}
    }
    $before.Add([pscustomobject]@{path=('PowerControl/decky/'+$name);existed=$exists;sha256=$previousHash})
  }
  return [pscustomobject]@{manifest=$manifest;entries=@([pscustomobject]@{path='PowerControl/decky/.ymcc-managed.receipt';bytes=[int64]$bytes.Length;sha256=$hash});before=@($before.ToArray());retiredPaths=@($retired.ToArray())}
}
function Assert-LocalDeploymentDerivedPreimage($Plan){
  foreach($entry in $Plan.before){
    $path=Resolve-LocalDeploymentChild $localInstallParent $entry.path
    if((Test-Path -LiteralPath $path) -and !(Test-Path -LiteralPath $path -PathType Leaf)){throw ('LOCAL_DEPLOY_DERIVED_PREIMAGE_CHANGED: '+$entry.path)}
    $exists=Test-Path -LiteralPath $path -PathType Leaf
    if($exists -ne $entry.existed -or ($exists -and (Get-ExportFileHash $path) -cne $entry.sha256)){throw ('LOCAL_DEPLOY_DERIVED_PREIMAGE_CHANGED: '+$entry.path)}
  }
}
function Assert-LocalDeploymentRetiredPaths($Plan){
  foreach($relative in $Plan.retiredPaths){if(Test-Path -LiteralPath (Resolve-LocalDeploymentChild $localInstallParent $relative)){throw ('LOCAL_DEPLOY_DERIVED_LEGACY_NOT_RETIRED: '+$relative)}}
}
function Get-LocalDeploymentMutablePaths($Entries,$Contract){
  $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach($path in @('PowerControl/Sleep/player-blacklist.txt','PowerControl/MG-AUTO/memreduct.ini','PowerControl/feature-assets/virtual-gamepad/enabled.flag','PowerControl/feature-assets/virtual-gamepad/input-capture.flag','PowerControl/feature-assets/virtual-gamepad/test-auto-install.flag','PowerControl/feature-assets/virtual-gamepad/.test-sidecar.json','PowerControl/feature-assets/gyro-motion/enabled.flag','PowerControl/feature-assets/gyro-motion/input-capture.flag','PowerControl/feature-assets/gyro-motion/.test-sidecar.json')){$null=$names.Add($path)}
  foreach($entry in $Entries){if($entry.path.StartsWith('PowerControl/',[StringComparison]::OrdinalIgnoreCase) -and (Test-YmccMutableSettingsPath $entry.path.Substring(13) $Contract)){$null=$names.Add($entry.path)}}
  return @($names|Sort-Object)
}
function Get-LocalDeploymentMutableState([string[]]$Paths){
  return @(foreach($path in $Paths){$file=Resolve-LocalDeploymentChild $localInstallParent $path;$exists=Test-Path -LiteralPath $file;if($exists -and !(Test-Path -LiteralPath $file -PathType Leaf)){throw ('LOCAL_DEPLOY_MUTABLE_NOT_FILE: '+$path)};[pscustomobject]@{path=$path;existed=[bool]$exists;sha256=$(if($exists){Get-ExportFileHash $file}else{$null})}})
}
function Restore-LocalDeploymentMutable($State,$Incoming,[string]$Backup,$CanonicalWrites=@()){
  $new=@{};foreach($entry in $Incoming){$new[$entry.path]=@([string]$entry.sha256)}
  foreach($entry in $CanonicalWrites){$new[$entry.path]=@($new[$entry.path])+@([string]$entry.sha256)}
  foreach($entry in $State){
    $target=Resolve-LocalDeploymentChild $localInstallParent $entry.path
    $exists=Test-Path -LiteralPath $target
    if($exists -and !(Test-Path -LiteralPath $target -PathType Leaf)){throw ('LOCAL_DEPLOY_MUTABLE_TARGET_NOT_FILE: '+$entry.path)}
    $hash=if($exists){Get-ExportFileHash $target}else{$null}
    if($exists -eq $entry.existed -and $hash -ceq $entry.sha256){continue}
    if($exists -and (!$new.ContainsKey($entry.path) -or $new[$entry.path] -cnotcontains $hash)){throw ('LOCAL_DEPLOY_LATER_MUTABLE_CHANGE_PRESERVED: '+$entry.path)}
    if($entry.existed){
      $saved=Resolve-LocalDeploymentChild $Backup $entry.path
      if((Get-ExportFileHash $saved) -cne $entry.sha256){throw ('LOCAL_DEPLOY_MUTABLE_BACKUP_CHANGED: '+$entry.path)}
      [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null;[IO.File]::Copy($saved,$target,$true)
    }elseif($exists){[IO.File]::Delete($target)}
    if((Test-Path -LiteralPath $target -PathType Leaf) -ne $entry.existed -or ($entry.existed -and (Get-ExportFileHash $target) -cne $entry.sha256)){throw ('LOCAL_DEPLOY_MUTABLE_RESTORE_FAILED: '+$entry.path)}
  }
}
function Assert-LocalDeploymentPackageReadback($Entries,[string[]]$MutablePaths){
  $mutable=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);foreach($path in $MutablePaths){$null=$mutable.Add($path)}
  foreach($entry in $Entries){
    if($mutable.Contains($entry.path)){continue}
    $target=Resolve-LocalDeploymentChild $localInstallParent $entry.path
    if(!(Test-Path -LiteralPath $target -PathType Leaf) -or (Get-Item -LiteralPath $target).Length -ne $entry.bytes -or (Get-ExportFileHash $target) -cne $entry.sha256){throw ('LOCAL_DEPLOY_COMPLETE_PACKAGE_READBACK_FAILED: '+$entry.path)}
  }
}
$localMutex=$null;$localModuleLease=$null;$localStage=$null;$localBackup=$null;$localReceiptOwned=$false
$localBefore=@();$localNew=$null;$localImmutableStarted=$false;$localCommitted=$false
$localReceipt=[ordered]@{schemaVersion=1;owner='YMCC.DeveloperLocalDeployment';admission='EXPLICIT_LOCAL_VERIFIED_MAINLINE_PACKAGE';startedAtUtc=[DateTime]::UtcNow.ToString('o');status='VALIDATING';apply=[bool]$Apply;installRoot=$InstallRoot;workspaceRoot=$WorkspaceRoot;sourceRoot=$localProject;fullFixtureValidation='NOT_RUN';applicationsControlled=$false;deviceSettingsWritten=$false;publicUpdateVersionPolicyChanged=$false}
try{
  $null=Resolve-LocalDeploymentChild (Split-Path -Parent $localEvidence) (Split-Path -Leaf $localEvidence)
  [IO.Directory]::CreateDirectory($localEvidence)|Out-Null
  New-LocalDeploymentReceipt $localReceipt
  $localReceiptOwned=$true
  $localMutex=Enter-ExportWorkspaceLock $WorkspaceRoot
  $build=Assert-ExportBuildIdentity -ProjectRoot $localProject -WorkspaceRoot $WorkspaceRoot
  $archive=Join-Path $WorkspaceRoot 'Release\Packages\YeManCC.zip'
  if($ExpectedArchiveSha256 -cnotmatch '^[a-fA-F0-9]{64}$' -or (Get-ExportFileHash $archive) -ine $ExpectedArchiveSha256){throw 'LOCAL_DEPLOY_CANONICAL_ARCHIVE_PIN_CHANGED'}
  $completeRoot=Join-Path $WorkspaceRoot 'Build\Package\CompleteRoot'
  $proof=Assert-ExportArchiveIdentity -Zip $archive -Root $completeRoot -BuildIdentity $build
  $null=Assert-ExportPublishedTree -Root (Join-Path $WorkspaceRoot 'Release') -CompleteRoot $completeRoot -BuildIdentity $build
  $null=Assert-FanHostArchiveLane $localProject $archive
  $localNew=Read-YmccPinnedLocalArchive -Archive $archive -Sha256 $ExpectedArchiveSha256
  $prior=Read-YmccPinnedLocalArchive -Archive $PreviousArchive -Sha256 $PreviousArchiveSha256
  $priorInstalled=Assert-YmccPriorLocalFanInstall -PriorPackage $prior -InstallParent $localInstallParent -CompletedLocalRepairReceipt $CompletedLocalRepairReceipt -CompletedLocalRepairSha256 $CompletedLocalRepairSha256 -IncomingPackage $localNew
  $localReceipt.buildId=$build.buildId;$localReceipt.sourceTreeSha256=$build.sourceTreeSha256
  $localReceipt.canonicalArchive=$proof;$localReceipt.priorInstalled=$priorInstalled
  if($CompletedLocalRepairReceipt){$localReceipt.completedLocalRepair=@{path=[IO.Path]::GetFullPath($CompletedLocalRepairReceipt);sha256=$CompletedLocalRepairSha256}}
  $localReceipt.tools=@(foreach($relative in @('tools/deploy-local-mainline.ps1','tools/local-fan-transition.ps1','tools/deploy-installed.ps1','native/updater_hc_runtime.ps1','native/decky_package_maintenance.ps1')){$file=Join-Path $localProject $relative;@{path=$relative;bytes=(Get-Item -LiteralPath $file).Length;sha256=(Get-ExportFileHash $file)}})
  $derivedPlan=Get-LocalDeploymentDerivedDeckyPlan -SourceDecky (Join-Path $WorkspaceRoot 'Release\PowerControl\decky') -LoaderLock (Join-Path $localProject 'decky-plugin\vendor\decky-loader.lock.json') -InstalledDecky (Join-Path $localInstallParent 'PowerControl\decky')
  $localAllIncoming=@($localNew.entries)
  foreach($entry in $derivedPlan.entries){
    $packaged=@($localNew.entries|Where-Object path -IEQ $entry.path)
    if($packaged.Count){if($packaged.Count -ne 1 -or $packaged[0].sha256 -cne $entry.sha256 -or $packaged[0].bytes -ne $entry.bytes){throw 'LOCAL_DEPLOY_DERIVED_PACKAGE_CONFLICT'}}else{$localAllIncoming+=,$entry}
  }
  foreach($relative in $derivedPlan.retiredPaths){if(@($localNew.entries|Where-Object path -IEQ $relative).Count){throw 'LOCAL_DEPLOY_DERIVED_LEGACY_PACKAGE_CONFLICT'}}
  $localReceipt.derivedManagedPlan=$derivedPlan
  $localReceipt.status='PLAN_VERIFIED';Write-LocalDeploymentReceipt $localReceipt
  if(!$Apply){$localReceipt.status='PLAN_VERIFIED_INSTALL_NOT_RUN';return}
  $localModuleLease=Enter-YmccModuleMaintenance
  Assert-LocalDeploymentStopped
  $operation=[guid]::NewGuid().ToString('N')
  $localStage=Resolve-LocalDeploymentChild (Join-Path $WorkspaceRoot 'Build\LocalDeploymentStage') $operation
  $localBackup=Resolve-LocalDeploymentChild (Join-Path $WorkspaceRoot 'Backup\LocalDeployment') $operation
  [IO.Directory]::CreateDirectory($localStage)|Out-Null
  # Extraction is preceded by complete closed archive and unsafe-entry checks.
  [IO.Compression.ZipFile]::ExtractToDirectory($archive,$localStage)
  $null=Assert-ExportArchiveIdentity -Zip $archive -Root $localStage -BuildIdentity $build
  $incomingTuple=Assert-YmccIncomingLocalFanStage -StageRoot $localStage
  $localBefore=@(Get-LocalDeploymentTree $localInstallParent)
  $mutablePaths=@(Get-LocalDeploymentMutablePaths @($localNew.entries+$localBefore) (Read-YmccSettingsBaselineContract $localProject))
  $mutableState=@(Get-LocalDeploymentMutableState $mutablePaths)
  # Original deploy copies this source default; the complete ZIP instead ships a
  # public-updater compatibility template. Both are known writer outputs. User
  # contents must be restored from the preimage, never accepted as a template.
  $canonicalMutableWrites=@([pscustomobject]@{path='PowerControl/Sleep/player-blacklist.txt';sha256=(Get-ExportFileHash (Join-Path $localProject 'PowerControl/Sleep/player-blacklist.txt'))})
  $localReceipt.canonicalMutableWrites=$canonicalMutableWrites
  [IO.Directory]::CreateDirectory($localBackup)|Out-Null
  Save-LocalDeploymentPreimage $localBefore $localBackup
  [IO.File]::WriteAllText((Join-Path $localBackup 'PREIMAGE.json'),($localBefore|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
  # Revalidate exact source/payload/package and old installation after staging.
  $null=Assert-ExportBuildIdentity -ProjectRoot $localProject -WorkspaceRoot $WorkspaceRoot
  if((Get-ExportFileHash $archive) -ine $ExpectedArchiveSha256){throw 'LOCAL_DEPLOY_ARCHIVE_CHANGED_AFTER_PLAN'}
  $null=Assert-YmccPriorLocalFanInstall -PriorPackage $prior -InstallParent $localInstallParent -CompletedLocalRepairReceipt $CompletedLocalRepairReceipt -CompletedLocalRepairSha256 $CompletedLocalRepairSha256 -IncomingPackage $localNew
  Assert-LocalDeploymentStopped
  $localReceipt.stageRoot=$localStage;$localReceipt.backupRoot=$localBackup;$localReceipt.incomingTuple=$incomingTuple;$localReceipt.mutableState=$mutableState
  $localReceipt.canonicalChildBehavior='Original prerequisite preparation and native/InputHost/TDP helper smoke remain; no main app lifecycle or driver/device-setting operation requested'
  foreach($tool in $localReceipt.tools){if((Get-ExportFileHash (Join-Path $localProject $tool.path)) -cne $tool.sha256){throw ('LOCAL_DEPLOY_TOOL_CHANGED: '+$tool.path)}}
  Assert-LocalDeploymentDerivedPreimage $derivedPlan
  $localReceipt.status='APPLYING_VERIFIED_IMMUTABLE_TUPLE';Write-LocalDeploymentReceipt $localReceipt
  $localImmutableStarted=$true
  $transition=Invoke-YmccLocalFanTransition -StageRoot $localStage -PowerControl (Join-Path $localInstallParent 'PowerControl')
  $localReceipt.immutableTransition=$transition
  # Ordinary export must now pass its ORIGINAL guard against the actual fully
  # verified incoming tuple; there is no skip/force/absent-target substitution.
  $null=Assert-FanHostExistingExport (Join-Path $localProject 'PowerControl\fan-host') (Join-Path $localInstallParent 'PowerControl\fan-host-v2')
  $localReceipt.status='RUNNING_ORIGINAL_CANONICAL_DEPLOY';Write-LocalDeploymentReceipt $localReceipt
  $shell=Join-Path $PSHOME 'pwsh.exe';if(!(Test-Path -LiteralPath $shell)){$shell=Join-Path $PSHOME 'powershell.exe'}
  & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'deploy-installed.ps1') -WorkspaceRoot $WorkspaceRoot -InstallRoot $InstallRoot 1> (Join-Path $localEvidence 'CANONICAL-DEPLOY.stdout.log') 2> (Join-Path $localEvidence 'CANONICAL-DEPLOY.stderr.log')
  $localReceipt.canonicalDeployExitCode=$LASTEXITCODE
  if($LASTEXITCODE -ne 0){throw 'LOCAL_DEPLOY_ORIGINAL_CANONICAL_FAILED'}
  Restore-LocalDeploymentMutable $mutableState $localNew.entries $localBackup $canonicalMutableWrites
  Assert-LocalDeploymentPackageReadback $localAllIncoming $mutablePaths
  Assert-LocalDeploymentRetiredPaths $derivedPlan
  $localReceipt.completeImmutablePackageReadback=$true;$localReceipt.mutablePresenceAndBytesPreserved=$true
  $null=Assert-ExportBuildIdentity -ProjectRoot $localProject -WorkspaceRoot $WorkspaceRoot
  $null=Assert-ExportStagedBuild -Root $localInstallParent -BuildIdentity $build
  $installed=Get-Content -LiteralPath (Join-Path $InstallRoot 'export-identity.json') -Raw -Encoding UTF8|ConvertFrom-Json
  if($installed.buildId -cne $build.buildId -or $installed.sourceTreeSha256 -cne $build.sourceTreeSha256){throw 'LOCAL_DEPLOY_INSTALLED_IDENTITY_MISMATCH'}
  $null=Assert-FanHostExistingExport (Join-Path $localStage 'PowerControl\fan-host-v2') (Join-Path $localInstallParent 'PowerControl\fan-host-v2')
  # The ordinary exporter owns package files; mutable pre-existing files outside
  # that closed inventory retain their actual original contents.
  $newPaths=@{};foreach($row in $localAllIncoming){$newPaths[$row.path]=$true}
  $retiredPaths=@{};foreach($relative in $derivedPlan.retiredPaths){$retiredPaths[$relative]=$true}
  foreach($row in $localBefore){
    if($newPaths.ContainsKey($row.path) -or $retiredPaths.ContainsKey($row.path) -or (Test-LocalDeploymentImmutable $row.path) -or $row.path -match '^YeManCC/(assets|icons|InputHost)/'){continue}
    $file=Resolve-LocalDeploymentChild $localInstallParent $row.path
    if(!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-ExportFileHash $file) -cne $row.sha256){throw ('LOCAL_DEPLOY_UNKNOWN_OR_USER_FILE_CHANGED: '+$row.path)}
  }
  $localCommitted=$true;$localReceipt.status='COMMITTED_VERIFIED_MAINLINE_LOCAL_PACKAGE'
  $localReceipt.installedBuildId=$installed.buildId;$localReceipt.candidateCpuAcceptance=$false
}catch{
  $localReceipt.error=$_.Exception.Message;$localReceipt.status='REJECTED_BEFORE_INSTALL'
  if($localImmutableStarted -and !$localCommitted){
    $localReceipt.status='REPAIR_REQUIRED'
    try{$localReceipt.immutableRepair=Invoke-YmccLocalFanTransition -StageRoot $localStage -PowerControl (Join-Path $localInstallParent 'PowerControl')}catch{$localReceipt.immutableRepairError=$_.Exception.Message}
    if($localBefore.Count -and $localBackup){
      try{if($mutableState){Restore-LocalDeploymentMutable $mutableState $localNew.entries $localBackup $canonicalMutableWrites}}catch{$localReceipt.mutableRollbackError=$_.Exception.Message}
      $localReceipt.rollback=Restore-LocalDeploymentOrdinary $localBefore $localAllIncoming $localBackup
    }
    # Same production updater contract: never replace incoming immutable trees
    # with a former runtime/profile. Preserve stage, backup and explicit journal.
  }
  throw
}finally{
  $localReceipt.endedAtUtc=[DateTime]::UtcNow.ToString('o')
  Close-LocalDeploymentScope $localReceipt $localReceiptOwned $localModuleLease $localMutex
}