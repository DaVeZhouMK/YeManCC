param([string]$Out='G:\YeManCC-Work\Mainline\Temp\steam-appearance-validation\robust', [switch]$Observe)
$ErrorActionPreference='Stop'
$source=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl\steam-appearance.ps1'
# Offline fixtures must not query live Steam/CSS ownership.
function Get-Process {param($Name,$Id) @()}
function Get-NetTCPConnection {param($State,$LocalPort) @()}
. $source -Action Library
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$script:PresetStateBase=Join-Path $Out 'states';$script:FixtureMode='offline'
function Get-BackendProcesses {@()}
function Test-BackendOwner {$false}
function Invoke-LoaderRpc([string]$Method,$Arguments=@{}) {
 if($Method -eq 'get_themes'){return @($script:ThemeNames|ForEach-Object {[pscustomobject]@{name=$_;enabled=($script:FixtureMode -ne 'ignore')}})}
 if($Method -eq 'set_theme_state'){
   if($script:FixtureMode -eq 'cascade-fail'){
     $p=Join-Path $script:ThemeRoot 'Mini Carousel\config_USER.json';$c=[IO.File]::ReadAllText($p)|ConvertFrom-Json;$c.active=$false;Write-ConfigAtomic $p ($c|ConvertTo-Json -Depth 64 -Compress)
     throw 'fixture cascade failure'
   }
   return @{success=$true}
 }
 throw ('Unexpected mock RPC: '+$Method)
}
$checks=New-Object Collections.Generic.List[object]
function Check([bool]$Passed,[string]$Name){$checks.Add([pscustomobject]@{ok=$Passed;name=$Name});if(!$Passed -and !$Observe){throw $Name}}
function Rejected([scriptblock]$Work){try{& $Work|Out-Null;return $false}catch{return $true}}
function Profile([switch]$Bundle){$script:FixtureMode='offline';$script:ThemeRoot=Join-Path $Out ('profile-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $script:ThemeRoot -Force|Out-Null;if($Bundle){foreach($name in $script:ThemeNames){Copy-Item -LiteralPath (Join-Path $script:PackRoot ('themes\'+$name)) -Destination $script:ThemeRoot -Recurse}}}
function Foreign([string]$Folder,[string]$Name){$d=Join-Path $script:ThemeRoot $Folder;New-Item -ItemType Directory -Path $d -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $d 'theme.json'),(@{name=$Name;manifest_version=1;inject=@{}}|ConvertTo-Json -Compress));[IO.File]::WriteAllText((Join-Path $d 'config_USER.json'),'{"active":true,"Tone":"keep-me"}');return $d}
# A corrupt ownership file must not silently begin a new takeover.
Profile;Invoke-PresetPrepare|Out-Null;$owner=Join-Path (Get-PresetStateDir) 'owner.json';[IO.File]::WriteAllText($owner,'{broken');$round=Join-Path $script:ThemeRoot 'Round\config_USER.json';$before=Get-ConfigHash $round
$blocked=Rejected {Invoke-PresetPrepare};Check ($blocked -and (Get-ConfigHash $round) -eq $before -and [IO.File]::ReadAllText($owner) -eq '{broken') 'Corrupt ownership is rejected without replacing the original restore pointer'
# Themes introduced after takeover must retain a reversible original definition/config.
Profile;Invoke-PresetPrepare|Out-Null;$late=Foreign 'Late Theme' 'Late Theme';$duplicate=Foreign 'Late Duplicate' 'Art Hero';$original=@{}
foreach($p in @((Join-Path $late 'config_USER.json'),(Join-Path $duplicate 'config_USER.json'),(Join-Path $duplicate 'theme.json'))){$original[$p]=Get-ConfigHash $p}
Invoke-PresetPrepare|Out-Null;Invoke-PresetRestore|Out-Null
foreach($p in $original.Keys){Check ((Get-ConfigHash $p) -eq $original[$p]) ('Late conflict restores original bytes: '+[IO.Path]::GetFileName([IO.Path]::GetDirectoryName($p))+'/'+[IO.Path]::GetFileName($p))}
# Managed config writes must reject directory junctions even for direct Apply.
Profile;$external=Join-Path $Out ('outside-'+[guid]::NewGuid().ToString('N'));Copy-Item -LiteralPath (Join-Path $script:PackRoot 'themes\Round') -Destination $external -Recurse
$junction=Join-Path $script:ThemeRoot 'Round';New-Item -ItemType Junction -Path $junction -Target $external|Out-Null;$externalConfig=Join-Path $external 'config_USER.json';$before=Get-ConfigHash $externalConfig
$rows=@(Get-ThemeRows);$revision=Get-ThemeRevision $rows;$blocked=Rejected {Invoke-AppearanceApply ([pscustomobject]@{expectedRevision=$revision;states=[pscustomobject]@{Round=$false}})}
Check ($blocked -and (Get-ConfigHash $externalConfig) -eq $before) 'Direct Apply rejects junction targets without writing outside the selected theme root'
# Reply success is not sufficient if the backend did not adopt the requested states.
Profile -Bundle;$script:FixtureMode='ignore';$blocked=Rejected {Sync-AppearanceBackend @(Get-ThemeRows) $true};Check $blocked 'Backend success reply with wrong enabled states is rejected'
# Refresh may cause a partial dependency cascade; requested disk states must survive it.
Profile -Bundle;$a=Join-Path $script:ThemeRoot 'Art Hero\config_USER.json';$c=[IO.File]::ReadAllText($a)|ConvertFrom-Json;$c.active=$false;Write-ConfigAtomic $a ($c|ConvertTo-Json -Depth 64 -Compress);$script:FixtureMode='cascade-fail'
$blocked=Rejected {Sync-AppearanceBackend @(Get-ThemeRows) $true};$mini=[IO.File]::ReadAllText((Join-Path $script:ThemeRoot 'Mini Carousel\config_USER.json'))|ConvertFrom-Json
Check ($blocked -and $mini.active) 'Failed refresh/dependency cascade preserves independent switch choices on disk'
# Journal shape must not reinterpret string flags or duplicate target entries.
Profile -Bundle;Invoke-PresetPrepare|Out-Null;$state=Read-PresetState;$jpath=Join-Path $state.originalBackup 'journal.json';$raw=[IO.File]::ReadAllText($jpath);$j=$raw|ConvertFrom-Json;$j.entries[0].existed='false';[IO.File]::WriteAllText($jpath,($j|ConvertTo-Json -Depth 8))
Check (Rejected {Read-PresetBackup $state.originalBackup}) 'Backup journal rejects non-boolean existed flags'
$j=$raw|ConvertFrom-Json;$j.entries=@($j.entries)+@($j.entries[0]);[IO.File]::WriteAllText($jpath,($j|ConvertTo-Json -Depth 8))
Check (Rejected {Read-PresetBackup $state.originalBackup}) 'Backup journal rejects duplicate target paths'
# A separate process exits after partial theme/owner writes, leaving a durable marker.
Profile -Bundle;Invoke-PresetPrepare|Out-Null;$owner=Join-Path (Get-PresetStateDir) 'owner.json';$round=Join-Path $script:ThemeRoot 'Round\config_USER.json';$oldOwner=Get-ConfigHash $owner;$oldRound=Get-ConfigHash $round;$undo=New-PresetBackup @($round)
$command=". '$($source.Replace("'","''"))' -Action Library; `$script:ThemeRoot='$($script:ThemeRoot.Replace("'","''"))'; `$script:PresetStateBase='$($script:PresetStateBase.Replace("'","''"))'; `$b=Read-PresetBackup '$($undo.directory.Replace("'","''"))'; Begin-PresetTransaction `$b; Write-PresetBytes '$($round.Replace("'","''"))' ([Text.Encoding]::UTF8.GetBytes('{broken partial config')); Write-ConfigAtomic '$($owner.Replace("'","''"))' '{broken partial owner'; [Environment]::Exit(97)"
$encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command));$job=Start-Process -FilePath powershell.exe -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) -WindowStyle Hidden -PassThru
if(!$job.WaitForExit(15000)){Stop-Process -Id $job.Id;throw 'Interrupted-transaction worker timed out'}
Check ($job.ExitCode -eq 97 -and (Test-Path -LiteralPath (Join-Path (Get-PresetStateDir) 'transaction.json'))) 'A terminated isolated worker leaves a durable transaction marker before commit'
$status=Get-AppearanceStatus
Check ((Get-ConfigHash $owner) -eq $oldOwner -and (Get-ConfigHash $round) -eq $oldRound -and $status.packPrepared) 'Next Status recovers both original owner bytes and partial theme writes before reporting readiness'
Check (!(Test-Path -LiteralPath (Join-Path (Get-PresetStateDir) 'transaction.json'))) 'Successful recovery removes the pending marker only after all rollback writes complete'
# Interrupted first takeover removes only newly created files and its new owner marker.
Profile;$round=Join-Path $script:ThemeRoot 'Round\config_USER.json';$undo=New-PresetBackup @($round);Begin-PresetTransaction $undo;Write-PresetBytes $round ([Text.Encoding]::UTF8.GetBytes('{"active":true}'));Save-PresetState ([pscustomobject]@{version=1;themeRoot=[IO.Path]::GetFullPath($script:ThemeRoot);owned=$true;packDigest=('a'*64);originalBackup=$undo.directory;latestBackup=$undo.directory;pendingReload=$true});$script:PresetTransactionActive=$false
Recover-PresetTransaction
Check (!(Test-Path -LiteralPath $round) -and !(Test-Path -LiteralPath (Join-Path (Get-PresetStateDir) 'owner.json'))) 'Interrupted first takeover restores absence of newly created theme and owner files'
# Invalid recovery marker must not be ignored or reinterpreted as a fresh takeover.
Profile;New-Item -ItemType Directory -Path (Get-PresetStateDir) -Force|Out-Null;$marker=Join-Path (Get-PresetStateDir) 'transaction.json';[IO.File]::WriteAllText($marker,'{broken marker')
Check (Rejected {Invoke-PresetPrepare}) 'Corrupt recovery marker blocks further writes instead of silently starting another takeover'
# Identical configs in another root are not interchangeable revision baselines.
Profile -Bundle;$revisionA=Get-ThemeRevision @(Get-ThemeRows);Profile -Bundle;$revisionB=Get-ThemeRevision @(Get-ThemeRows)
Check ($revisionA -ne $revisionB) 'Revision binds to the selected theme directory, not just identical config bytes'
# Remember the actual per-user custom paths even after the loader is no longer running.
$customRoot=$script:ThemeRoot;$customExe=Join-Path $Out 'CustomLoader\CssLoader-Standalone-Headless.exe';New-Item -ItemType Directory -Path (Split-Path -Parent $customExe) -Force|Out-Null;[IO.File]::WriteAllText($customExe,'inert fixture, never executed');$script:BackendExe=$customExe;Save-PresetTargetHint
$script:ThemeRoot=Join-Path $Out 'WrongDefaultThemes';$script:BackendExe='C:\SOFT\Missing\CssLoader-Standalone-Headless.exe';Initialize-PresetTargetHint
Check ($script:ThemeRoot -eq $customRoot -and $script:BackendExe -eq $customExe) 'Offline initialization reuses saved custom theme and loader paths instead of DaVe defaults'
$script:BackendExe='C:\VerifiedCurrent\CssLoader-Standalone-Headless.exe';Initialize-PresetTargetHint -KeepBackendPath
Check ($script:BackendExe -eq 'C:\VerifiedCurrent\CssLoader-Standalone-Headless.exe') 'Cached target cannot override the currently verified port-owner executable'
$hint=Get-PresetTargetHintPath;[IO.File]::WriteAllText($hint,'{"version":1,"themeRoot":"relative","backendExe":"invalid"}')
Check (Rejected {Initialize-PresetTargetHint}) 'Corrupt target hint blocks automatic writes to a guessed default profile'
# Damage to the restore baseline is not allowed to hide behind apparently healthy CSS.
Profile;Invoke-PresetPrepare|Out-Null;$state=Read-PresetState;$b=Read-PresetBackup $state.originalBackup
# The absent-at-first-takeover journal has no blobs: make its first entry invalid instead.
$jpath=Join-Path $b.directory 'journal.json';$j=$b.journal;$j.entries[0].existed='false';[IO.File]::WriteAllText($jpath,($j|ConvertTo-Json -Depth 8));$status=Get-AppearanceStatus
Check (!$status.canRestore -and !$status.packPrepared -and $status.packError) 'Damaged restore baseline is visible as unprepared/error rather than a healthy managed preset'
$round=Join-Path $script:ThemeRoot 'Round\config_USER.json';$before=Get-ConfigHash $round
Check ((Rejected {Invoke-AppearanceApply ([pscustomobject]@{expectedRevision=$status.revision;states=[pscustomobject]@{Round=$false}})}) -and (Get-ConfigHash $round) -eq $before) 'Apply stops before writing if owned restore baseline cannot be verified'
function Get-NetTCPConnection {@()};$cleanup=Clear-OldLoaderStyles
Check (!$cleanup.mainConfirmed -and !$cleanup.attempted) 'Missing verified Steam CEF endpoint is never treated as confirmed old-style cleanup'
Remove-Item -LiteralPath 'Function:\Get-NetTCPConnection'
# Batch Apply must survive termination between two config writes, too.
Profile -Bundle;Invoke-PresetPrepare|Out-Null;$art=Join-Path $script:ThemeRoot 'Art Hero\config_USER.json';$oldArt=Get-ConfigHash $art;$revision=Get-ThemeRevision @(Get-ThemeRows)
$command=". '$($source.Replace("'","''"))' -Action Library; `$script:ThemeRoot='$($script:ThemeRoot.Replace("'","''"))'; `$script:PresetStateBase='$($script:PresetStateBase.Replace("'","''"))'; function Write-ThemeConfigAtomic([string]`$Path,[string]`$Text){Write-ConfigAtomic `$Path `$Text;[Environment]::Exit(98)};Invoke-AppearanceApply ([pscustomobject]@{expectedRevision='$revision';states=[pscustomobject]@{'Art Hero'=`$false;Round=`$false}})"
$encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command));$job=Start-Process -FilePath powershell.exe -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) -WindowStyle Hidden -PassThru
if(!$job.WaitForExit(15000)){Stop-Process -Id $job.Id;throw 'Apply-interruption worker timed out'}
Check ($job.ExitCode -eq 98 -and (Test-Path -LiteralPath (Join-Path (Get-PresetStateDir) 'transaction.json'))) 'Batch Apply persists a transaction marker before the first switch write'
$status=Get-AppearanceStatus;Check ((Get-ConfigHash $art) -eq $oldArt -and @($status.themes|Where-Object configured).Count -eq $script:ThemeNames.Count) 'Next read rolls back an interrupted batch Apply instead of leaving a mixed accidental master state'
# A same-named executable belonging to another user/session must never be controlled.
$self=[Diagnostics.Process]::GetCurrentProcess()
Check (!(Test-CurrentUserProcess ([pscustomobject]@{SessionId=($script:CurrentSessionId+1);Id=$PID}))) 'Process control rejects a different Windows session before querying or acting on it'
function Get-CimInstance { [pscustomobject]@{ProcessId=$PID} }
function Invoke-CimMethod { [pscustomobject]@{ReturnValue=$script:FixtureOwnerReturn;Sid=$script:FixtureOwnerSid} }
$script:FixtureOwnerReturn=0;$script:FixtureOwnerSid='S-1-5-21-other-user';$script:ProcessOwnerCache=@{}
Check (!(Test-CurrentUserProcess $self)) 'Same-session process owned by another user SID is not adopted or terminated'
$script:FixtureOwnerReturn=5;$script:FixtureOwnerSid='';$script:ProcessOwnerCache=@{}
Check (Rejected {Test-CurrentUserProcess $self}) 'Unavailable process-owner verification fails closed rather than assuming ownership'
Remove-Item -LiteralPath 'Function:\Get-CimInstance';Remove-Item -LiteralPath 'Function:\Invoke-CimMethod';$script:ProcessOwnerCache=@{}
# Invalid third-party manifests are a real loader conflict, not an invisible skipped row.
Profile;Invoke-PresetPrepare|Out-Null;$bad=Foreign 'Late Broken Manifest' 'Later Theme';$manifest=Join-Path $bad 'theme.json';[IO.File]::WriteAllText($manifest,'{broken original manifest');$beforeManifest=Get-ConfigHash $manifest;$config=Join-Path $bad 'config_USER.json';$beforeConfig=Get-ConfigHash $config
Invoke-PresetPrepare|Out-Null;$shim=[IO.File]::ReadAllText($manifest)|ConvertFrom-Json;$paused=[IO.File]::ReadAllText($config)|ConvertFrom-Json
Check ($shim.name -like '_ymcc-paused-*' -and !$paused.active -and $paused.Tone -eq 'keep-me') 'Malformed late third-party manifest is safely paused with its original config parameters preserved'
Invoke-PresetRestore|Out-Null
Check ((Get-ConfigHash $manifest) -eq $beforeManifest -and (Get-ConfigHash $config) -eq $beforeConfig) 'Restore returns malformed third-party manifest/config raw bytes rather than losing or pretending to repair them'
# Huge broken user configs must not allocate unbounded memory before backup.
Profile;Invoke-PresetPrepare|Out-Null;$huge=Foreign 'Too Large' 'Too Large';$config=Join-Path $huge 'config_USER.json';$stream=[IO.File]::Open($config,[IO.FileMode]::Open,[IO.FileAccess]::Write);try{$stream.SetLength(16777217)}finally{$stream.Dispose()};$before=Get-ConfigHash $config;$owner=Join-Path (Get-PresetStateDir) 'owner.json';$beforeOwner=Get-ConfigHash $owner
Check ((Rejected {Invoke-PresetPrepare}) -and (Get-ConfigHash $config) -eq $before -and (Get-ConfigHash $owner) -eq $beforeOwner) 'Oversized third-party config fails before unbounded allocation, theme overwrite, or owner changes'
# Asset/manifest repair must not reset valid user parameters merely because installed=false.
Profile;Invoke-PresetPrepare|Out-Null;$round=Join-Path $script:ThemeRoot 'Round\config_USER.json';$manifest=Join-Path $script:ThemeRoot 'Round\theme.json';$c=[IO.File]::ReadAllText($round)|ConvertFrom-Json;$c.active=$false;$c.Intensity='30px';Write-ConfigAtomic $round ($c|ConvertTo-Json -Depth 64 -Compress);$before=Get-ConfigHash $round;[IO.File]::WriteAllText($manifest,'{broken theme definition')
Invoke-PresetPrepare|Out-Null
Check ((Get-ConfigHash $round) -eq $before) 'Repair of malformed managed manifest preserves exact valid switch and parameter bytes'
Remove-Item -LiteralPath $manifest;Invoke-PresetPrepare|Out-Null
Check ((Get-ConfigHash $round) -eq $before) 'Repair of missing managed manifest also preserves existing custom config rather than resetting it'
$readConfig=${function:Read-PresetUserConfig};function Read-PresetUserConfig([string]$Path){if($Path -eq $round){throw 'fixture sharing violation'};& $readConfig $Path};$owner=Join-Path (Get-PresetStateDir) 'owner.json';$oldOwner=Get-ConfigHash $owner
Check ((Rejected {Invoke-PresetPrepare}) -and (Get-ConfigHash $round) -eq $before -and (Get-ConfigHash $owner) -eq $oldOwner) 'Unreadable owned config fails before repair writes rather than being treated as a corrupt/defaultable config'
Set-Item -LiteralPath 'Function:\Read-PresetUserConfig' -Value $readConfig
$text=[IO.File]::ReadAllText($source);Check ($text -match "if\(\`$ControlAction -in @\([^\r\n]*'Ensure'") 'Ensure lifecycle shares the same configuration-operation mutex as Apply/Reset/Restore'
$failed=@($checks|Where-Object {!$_.ok});[pscustomobject]@{ok=($failed.Count -eq 0);scope='Isolated profiles, mock backend, junction inside test workspace; no production Steam/theme changes';checks=$checks.ToArray();failedCount=$failed.Count}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'robust-result.json') -Encoding UTF8
$checks|ForEach-Object {Write-Output (($(if($_.ok){'PASS'}else{'FAIL'}))+': '+$_.name)}
if($failed.Count -and !$Observe){exit 1}
