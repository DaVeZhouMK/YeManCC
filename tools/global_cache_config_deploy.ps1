# Same-version, white-list-only deployment. Never starts/stops product processes,
# resets settings, cleans asset trees, or replaces fan/input payloads.
[CmdletBinding()]
param(
  [ValidateSet('Validate','Apply','Rollback')][string]$Mode='Validate',
  [Parameter(Mandatory=$true)][string]$Manifest,
  [string]$InstallRoot='C:\SOFT\YeMan\YeManCC',
  [string]$BackupRoot,
  [int]$FixtureFaultAfter=0
)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$manifestPath=[IO.Path]::GetFullPath($Manifest)
$plan=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
$install=[IO.Path]::GetFullPath($InstallRoot).TrimEnd([char[]]@('\','/'))
$payload=[IO.Path]::GetFullPath([string]$plan.overlayDirectory).TrimEnd([char[]]@('\','/'))
$utf8=[Text.UTF8Encoding]::new($false)
function CheckedPath([string]$root,[string]$relative) {
  if([IO.Path]::IsPathRooted($relative) -or $relative.Split([char[]]@('/','\')) -contains '..') { throw "Unsafe relative path: $relative" }
  $path=[IO.Path]::GetFullPath((Join-Path $root $relative))
  if(!$path.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Path escapes intended root'}
  return $path
}
function CheckParents([string]$path,[string]$root) {
  $cursor=$path
  while($cursor -and $cursor.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)) {
    if(Test-Path -LiteralPath $cursor){$entry=Get-Item -LiteralPath $cursor -Force;if(($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse point is not a deployment target: $cursor"}}
    if($cursor -eq $root){break};$cursor=[IO.Path]::GetDirectoryName($cursor)
  }
}
function FileHash([string]$path) {
  if(!(Test-Path -LiteralPath $path -PathType Leaf)){return $null}
  $input=[IO.File]::OpenRead($path);$algorithm=[Security.Cryptography.SHA256]::Create()
  try{$digest=$algorithm.ComputeHash($input)}finally{$input.Dispose();$algorithm.Dispose()}
  return ([BitConverter]::ToString($digest)).Replace('-','').ToLowerInvariant()
}
function NoProductProcesses {
  $running=@(Get-Process -Name @('YeManCC','CustomSteamLibrary','SteamArtworkLab') -ErrorAction SilentlyContinue)
  if($running.Count){throw ('Products must be closed by the user first: '+(($running|ForEach-Object{$_.ProcessName+' PID '+$_.Id}) -join ', '))}
}
function WriteJournal([object]$state) {
  $json=$state|ConvertTo-Json -Depth 18
  $target=Join-Path $script:rollback 'deployment-journal.json'
  $temp=Join-Path $script:rollback ('journal-'+[guid]::NewGuid().ToString('N')+'.tmp')
  $bytes=$utf8.GetBytes($json);$stream=[IO.File]::Open($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
  if(Test-Path -LiteralPath $target){[IO.File]::Replace($temp,$target,[NullString]::Value)}else{[IO.File]::Move($temp,$target)}
}
function AtomicCopy([string]$source,[string]$target,[string]$expected) {
  CheckParents $target $install
  $parent=[IO.Path]::GetDirectoryName($target);[IO.Directory]::CreateDirectory($parent)|Out-Null
  $temp=Join-Path $parent ('.global-config-repair-'+[guid]::NewGuid().ToString('N')+'.tmp')
  try {
    $input=[IO.File]::OpenRead($source);$output=[IO.File]::Open($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$input.CopyTo($output);$output.Flush($true)}finally{$output.Dispose();$input.Dispose()}
    if((FileHash $temp) -ne $expected){throw 'Staged copy hash mismatch'}
    if(Test-Path -LiteralPath $target -PathType Leaf){[IO.File]::Replace($temp,$target,[NullString]::Value)}else{[IO.File]::Move($temp,$target)}
  } finally { if(Test-Path -LiteralPath $temp -PathType Leaf){Remove-Item -LiteralPath $temp -Force} }
}
function RestoreFiles([object]$state) {
  NoProductProcesses
  # Reverse ordering restores the old index before restoring backing assets.
  if(!$state.files -or $state.files.Count -eq 0){throw 'Empty rollback journal'}
  foreach($file in @($state.files)[($state.files.Count-1)..0]) {
    if([string]$file.path -notmatch $allow){throw 'Rollback journal contains an unmanaged path'}
    $known=@($plan.files|Where-Object{$_.path -eq $file.path})
    if($known.Count -ne 1 -or $known[0].sha256 -ne $file.sha256 -or $known[0].expectedInstalledSha256 -ne $file.expectedInstalledSha256 -or $known[0].installedPresent -ne $file.installedPresent){throw 'Rollback journal differs from the original manifest'}
    $target=CheckedPath $install ([string]$file.path);CheckParents $target $install
    $current=FileHash $target
    if($file.installedPresent) {
      if($current -eq $file.expectedInstalledSha256){continue}
      if($current -ne $file.sha256 -and $null -ne $current){throw "Rollback refuses a newer unknown modification: $($file.path)"}
      $old=CheckedPath (Join-Path $script:rollback 'files') ([string]$file.path)
      if((FileHash $old) -ne $file.expectedInstalledSha256){throw "Rollback copy is corrupt: $($file.path)"}
      AtomicCopy $old $target $file.expectedInstalledSha256
    } elseif($current) {
      if($current -ne $file.sha256){throw "Rollback refuses to delete unknown file: $($file.path)"}
      # This is one verified added file inside the checked installation root,
      # never a directory or recursive operation.
      Remove-Item -LiteralPath $target -Force
    }
  }
}
$allow='^(YeManCC\.exe|index\.html|hls\.min\.js|gamepad-base\.png|(assets|icons)/[^:]+|CustomSteamLibrary/(CustomSteamLibrary\.exe|SteamArtworkLab\.exe|package-manifest\.json|run-workspace\.bat|assets/custom-steam-library\.ico|workspace-ui/(index\.html|app\.js|styles\.css)|CUSTOM-STEAM-LIBRARY-(INTEGRATION|UPGRADE)-CONTRACT\.md|SEPARATION-TASK-CUSTOM-STEAM-LIBRARY\.md))$'
if(!$plan.files -or $plan.files.Count -eq 0){throw 'Empty deployment manifest'}
if(@($plan.files.path|Select-Object -Unique).Count -ne $plan.files.Count){throw 'Duplicate managed file paths'}
$isFixture=$install.StartsWith('G:\YeManCC-Work\_scratch\global-cache-config-audit-20261006\',[StringComparison]::OrdinalIgnoreCase)
if($Mode -eq 'Apply' -and $plan.status -ne 'READY_FOR_DEPLOY' -and !($isFixture -and $plan.status -eq 'FIXTURE_READY')){throw 'Candidate is not installation-ready'}
foreach($required in @('YeManCC.exe','index.html','CustomSteamLibrary/CustomSteamLibrary.exe','CustomSteamLibrary/SteamArtworkLab.exe','CustomSteamLibrary/package-manifest.json')){if($plan.files.path -notcontains $required){throw "Incomplete matching main/frontend/child patch: $required"}}

foreach($file in $plan.files) {
  if([string]$file.path -notmatch $allow){throw "Not a managed patch file: $($file.path)"}
  $target=CheckedPath $install ([string]$file.path);$source=CheckedPath $payload ([string]$file.path)
  CheckParents $target $install;CheckParents $source $payload
}
if($FixtureFaultAfter -gt 0 -and !$install.StartsWith('G:\YeManCC-Work\_scratch\global-cache-config-audit-20261006\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fault injection is permitted only in scratch fixtures'}
NoProductProcesses
if($Mode -eq 'Rollback') {
  if(!$BackupRoot){throw 'Rollback requires an exact backup directory'}
  $script:rollback=[IO.Path]::GetFullPath($BackupRoot).TrimEnd([char[]]@('\','/'))
  if(!$script:rollback.StartsWith($install+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Rollback directory is outside the intended installation'}
  CheckParents $script:rollback $install
  $state=Get-Content -LiteralPath (Join-Path $script:rollback 'deployment-journal.json') -Raw -Encoding UTF8|ConvertFrom-Json
  if([string]$state.installRoot -ne $install){throw 'Rollback belongs to another installation'}
  RestoreFiles $state;$state.status='rolled-back';WriteJournal $state
  [pscustomobject]@{status='rolled-back';backup=$script:rollback;launchedProducts=$false}|ConvertTo-Json
  exit 0
}
$versionPath=Join-Path $install 'version.json'
if(!(Test-Path -LiteralPath $versionPath -PathType Leaf)){throw 'Same-version patch requires an existing version identity'}
$existingVersion=Get-Content -LiteralPath $versionPath -Raw -Encoding UTF8|ConvertFrom-Json
if([string]$existingVersion.version -ne [string]$plan.version){throw 'Installed version differs from this same-version patch'}
foreach($file in $plan.files) {
  $target=CheckedPath $install ([string]$file.path);$source=CheckedPath $payload ([string]$file.path)
  if((FileHash $source) -ne $file.sha256){throw "Payload hash mismatch: $($file.path)"}
  if([bool]$file.installedPresent -ne (Test-Path -LiteralPath $target -PathType Leaf)){throw "Installation presence changed: $($file.path)"}
  if($file.installedPresent -and (FileHash $target) -ne $file.expectedInstalledSha256){throw "Installation identity changed: $($file.path)"}
}
if($Mode -eq 'Validate'){[pscustomobject]@{status='validated';fileCount=$plan.files.Count;installedWrites=$false}|ConvertTo-Json;exit 0}
# All identities must be current immediately before preparing a durable backup.
NoProductProcesses
$script:rollback=Join-Path $install ('.global-config-repair-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
CheckParents $script:rollback $install
[IO.Directory]::CreateDirectory($script:rollback)|Out-Null
foreach($file in $plan.files|Where-Object{$_.installedPresent}) {
  $target=CheckedPath $install ([string]$file.path);$old=CheckedPath (Join-Path $script:rollback 'files') ([string]$file.path)
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($old))|Out-Null
  # Backups use the same verified, flushed atomic copy as activation.
  AtomicCopy $target $old $file.expectedInstalledSha256
  if((FileHash $old) -ne $file.expectedInstalledSha256){throw "Backup identity changed: $($file.path)"}
}
# Keep the visible entry point last; no source/settings/version/reset actions.
$ordered=@($plan.files|Sort-Object @{Expression={if($_.path -eq 'index.html'){3}elseif($_.path -like '*.exe'){2}else{1}}},path)
$state=[pscustomobject]@{schemaVersion=1;status='prepared';installRoot=$install;payloadRoot=$payload;manifest=$manifestPath;createdAt=(Get-Date).ToString('o');files=$ordered;activatedCount=0;failure=$null}
WriteJournal $state
try {
  $written=0
  foreach($file in $ordered) {
    NoProductProcesses
    $target=CheckedPath $install ([string]$file.path);$current=FileHash $target
    if($file.installedPresent){if($current -ne $file.expectedInstalledSha256){throw "Concurrent installation change: $($file.path)"}}elseif($current){throw "New file appeared during installation: $($file.path)"}
    AtomicCopy (CheckedPath $payload ([string]$file.path)) $target $file.sha256
    if((FileHash $target) -ne $file.sha256){throw "Committed hash mismatch: $($file.path)"}
    ++$written
    $state.activatedCount=$written
    if($FixtureFaultAfter -gt 0 -and $written -eq $FixtureFaultAfter){throw 'Fixture-injected deployment failure'}
  }
  $state.status='committed';WriteJournal $state
} catch {
  $state.failure=$_.Exception.Message;$state.status='rollback-needed';WriteJournal $state
  try{RestoreFiles $state;$state.status='rolled-back';WriteJournal $state}catch{$state.status='rollback-needed';WriteJournal $state;throw}
  throw ('Patch failed and original files were restored: '+$state.failure)
}
[pscustomobject]@{status='committed';fileCount=$ordered.Count;backup=$script:rollback;productsStartedOrStopped=$false;settingsReset=$false}|ConvertTo-Json
