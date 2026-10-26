[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot='', [switch]$ReplayOnly)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
if(!$ComponentRoot){$ComponentRoot=Join-Path $project 'PowerControl'}
$Out=[IO.Path]::GetFullPath($Out)
if(Test-Path -LiteralPath $Out){throw 'Fresh isolated fixture root required'}
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$oldLocal=$env:LOCALAPPDATA;$oldUser=$env:USERPROFILE
$env:LOCALAPPDATA=Join-Path $Out 'AppData';$env:USERPROFILE=Join-Path $Out 'User'
function Get-Process {param($Name,$Id) @()}
function Get-NetTCPConnection {param($State,$LocalPort) @()}
$backend=Join-Path $Out 'CssLoader-Standalone-Headless.exe';[IO.File]::WriteAllText($backend,'test placeholder; never executed')
try{
 . (Join-Path $ComponentRoot 'steam-appearance.ps1') -Action Library -BackendPath $backend
 # Library intentionally returns before defining the command dispatcher. Load
 # that exact function from the production AST, without replacing any logic.
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $ComponentRoot 'steam-appearance.ps1'),[ref]$tokens,[ref]$errors)
 $control=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-AppearanceControl'},$true)
 if(!$control){throw 'Production Ensure dispatcher missing'}
 Invoke-Expression $control.Extent.Text
 $script:AppearanceFailureCode=0;$script:Live=$false;$script:Editing=$false;$script:Reloads=0;$script:Loads=@();$script:Rows=@();$script:Mode='ok';$script:Stops=0
 $script:AppearanceFixtureMutexPrefix='Local\YMCC.CssFixture.'+[guid]::NewGuid().ToString('N')+'.'
function New-AppearanceMutex([string]$Purpose) {
 if($Purpose -notin @('Launch','ThemeControl')){throw 'APPEARANCE_FIXTURE_MUTEX_PURPOSE_DENIED'}
 New-Object Threading.Mutex($false,($script:AppearanceFixtureMutexPrefix+$Purpose))
}
function Get-BackendProcesses {if($script:Live){[pscustomobject]@{Id=42;Path=$script:BackendExe}}}
 function Test-BackendOwner {$script:Live}
 function Assert-SingleAppearanceBackend($Processes){if(@($Processes).Count -ne 1){throw "fixture instance identity"}}
 function Test-CssDesktopEditing {$script:Editing}
 function Read-AppearanceIdle {return $null}
 function Stop-VerifiedIdleProcesses {$script:Stops++;throw 'No process termination allowed in isolated upgrade test'}
 function Clear-OldLoaderStyles {$script:Stops++;throw 'No CDP style cleanup allowed in isolated upgrade test'}
 function Invoke-LoaderRpc([string]$Method,$Arguments=@{}){
  switch($Method){
   fetch_theme_path {return $script:ThemeRoot}
   get_themes {return $script:Rows}
   reset {
    $script:Reloads++;$script:Loads+=@((Get-ConfigHash (Join-Path $script:ThemeRoot 'YMCC Game Details\shared.css')))
    if($script:Mode -eq 'reload-failed'){return [pscustomobject]@{fails=@('fixture reload not confirmed')}}
    $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
    return [pscustomobject]@{fails=@()}
   }
   set_theme_state {($script:Rows|Where-Object name -eq $Arguments.name).enabled=$Arguments.state;return [pscustomobject]@{success=$true}}
   default {throw ('Unexpected fixture backend operation: '+$Method)}
  }
 }
 $currentPack=Join-Path $project 'PowerControl\steam-appearance'
 $legacyPack=Join-Path $Out 'legacy-pack';Copy-Item -LiteralPath $currentPack -Destination $legacyPack -Recurse
 $legacyCss="/* isolated previous package */`n.BasicUI .appdetails_Header_2gZXh{--ymcc-details-hero-height:min(70vh, calc(100vh - 12rem));}`n"
 [IO.File]::WriteAllText((Join-Path $legacyPack 'themes\YMCC Game Details\shared.css'),$legacyCss,(New-Object Text.UTF8Encoding($false)))
 $themePath=Join-Path $legacyPack 'themes\YMCC Game Details\theme.json';$theme=Get-Content -LiteralPath $themePath -Raw -Encoding UTF8|ConvertFrom-Json;$theme.version='v1.0.0';[IO.File]::WriteAllText($themePath,($theme|ConvertTo-Json -Depth 12),(New-Object Text.UTF8Encoding($false)))
 $legacyFocusPath=Join-Path $legacyPack 'themes\Darken Unfocused Games\global.css'
 [IO.File]::WriteAllText($legacyFocusPath,'.BasicUI .appportrait_LibraryItemBox_WYgDg {filter:brightness(var(--ren-game-tile-brightness)) saturate(var(--ren-game-tile-saturate));}')
 $manifestPath=Join-Path $legacyPack 'manifest.json';$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
 foreach($file in $manifest.files){$path=Join-Path $legacyPack ('themes\'+$file.path);$file.sha256=(Get-ConfigHash $path).ToLowerInvariant();$file.bytes=(Get-Item -LiteralPath $path).Length}
 ($manifest.themes|Where-Object name -eq 'YMCC Game Details').version='v1.0.0'
 [IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 16),(New-Object Text.UTF8Encoding($false)))
 $expected=(Get-ConfigHash (Join-Path $currentPack 'themes\YMCC Game Details\shared.css'))
 $legacy=(Get-ConfigHash (Join-Path $legacyPack 'themes\YMCC Game Details\shared.css'))
 $cases=New-Object Collections.Generic.List[string]
 function Assert($Value,[string]$Label){if(!$Value){throw $Label};$cases.Add($Label)}
 function Rejected([scriptblock]$Work){try{& $Work|Out-Null;return $false}catch{return $true}}
 function Hashes([string]$Directory){$result=@{};foreach($f in Get-ChildItem -LiteralPath $Directory -File -Recurse){$result[$f.FullName.Substring($Directory.Length)]=(Get-ConfigHash $f.FullName)};return $result}
 function Same($A,$B){if($A.Count -ne $B.Count){return $false};foreach($key in $A.Keys){if(!$B.ContainsKey($key) -or $A[$key] -ne $B[$key]){return $false}};return $true}
 function New-Scenario([string]$Name,[switch]$NoOwner,[switch]$DisableDetails){
  $script:ThemeRoot=Join-Path $Out ($Name+'\themes');$script:PresetStateBase=Join-Path $Out ($Name+'\state');$script:PackRoot=$legacyPack
  New-Item -ItemType Directory -Path $script:ThemeRoot -Force|Out-Null
  $script:Live=$false;$script:Editing=$false;$script:Reloads=0;$script:Loads=@();$script:Rows=@();$script:Mode='ok'
  if($NoOwner){Copy-Item -Path (Join-Path $legacyPack 'themes\*') -Destination $script:ThemeRoot -Recurse}
  else{Invoke-PresetPrepare|Out-Null;Complete-PresetReload}
  $round=Join-Path $script:ThemeRoot 'Round\config_USER.json';$value=Get-Content -LiteralPath $round -Raw -Encoding UTF8|ConvertFrom-Json;$value.active=$false;$value.Intensity='30px';$value|Add-Member -NotePropertyName fixture -NotePropertyValue @{nested='keep'} -Force;Write-ConfigAtomic $round ($value|ConvertTo-Json -Depth 12)
  Write-ConfigAtomic (Join-Path $script:ThemeRoot 'Darken Unfocused Games\config_USER.json') '{"active":false,"Brighntess":"0.3","Saturation":"0.8","Apply":"Recent Only"}'
  if($DisableDetails){Write-ConfigAtomic (Join-Path $script:ThemeRoot 'YMCC Game Details\config_USER.json') '{"active":false}'}
  $foreign=Join-Path $script:ThemeRoot 'User Custom Theme';New-Item -ItemType Directory -Path $foreign -Force|Out-Null
  [IO.File]::WriteAllText((Join-Path $foreign 'theme.json'),'{"name":"User Custom Theme","author":"user","manifest_version":3,"inject":{}}')
  [IO.File]::WriteAllText((Join-Path $foreign 'config_USER.json'),'{"active":false,"nested":{"value":"keep foreign"}}')
  $script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}});$script:Live=$true;$script:PackRoot=$currentPack
  # Resolve the fixture backend target hint before read-only/idempotence snapshots.
  Get-AppearanceStatus|Out-Null
 }
 New-Scenario 'replay'
 $before=Get-AppearanceStatus;$configs=@{};foreach($f in Get-ChildItem -LiteralPath $script:ThemeRoot -Filter config_USER.json -Recurse){$configs[$f.FullName]=(Get-ConfigHash $f.FullName)}
 Assert (!$before.packPrepared -and $before.packRepairRequired -and (Get-ConfigHash (Join-Path $script:ThemeRoot 'YMCC Game Details\shared.css')) -eq $legacy) 'Reproduce current package with a still-old owned CSSLoader user theme'
 $result=Invoke-AppearanceControl 'Ensure'
 Assert ((Get-ConfigHash (Join-Path $script:ThemeRoot 'YMCC Game Details\shared.css')) -eq $expected) 'Ensure synchronizes current bundled CSS before existing backend reload'
 Assert ($result.ok -and $script:Reloads -eq 1 -and $script:Loads[0] -eq $expected) 'Backend rereads the new resource exactly once, without another process'
 Assert ((Get-ConfigHash (Join-Path $script:ThemeRoot 'Darken Unfocused Games\global.css')) -eq (Get-ConfigHash (Join-Path $currentPack 'themes\Darken Unfocused Games\global.css'))) 'Focus-highlight upgrade replaces the old whole-tile filter with current cover-only CSS'
 $focusConfig=Get-Content -LiteralPath (Join-Path $script:ThemeRoot 'Darken Unfocused Games\config_USER.json') -Raw|ConvertFrom-Json
 Assert (!$focusConfig.active -and $focusConfig.Brighntess -eq '0.3' -and $focusConfig.Saturation -eq '0.8' -and $focusConfig.Apply -eq 'Recent Only') 'Focus-highlight upgrade preserves user OFF, brightness, saturation and scope'
 foreach($entry in $configs.GetEnumerator()){Assert ((Get-ConfigHash $entry.Key) -eq $entry.Value) ('User configuration preserved byte for byte: '+[IO.Path]::GetFileName([IO.Path]::GetDirectoryName($entry.Key)))}
 $state=Read-PresetState;Assert ($state.packDigest -eq (Read-PresetManifest).digest -and !$state.pendingReload) 'Owned resource receipt advances only with confirmed backend reread'
 if(!$ReplayOnly){
  New-Scenario 'running-mode';$configsBefore=@{};foreach($f in Get-ChildItem -LiteralPath $script:ThemeRoot -Filter config_USER.json -Recurse){$configsBefore[$f.FullName]=(Get-ConfigHash $f.FullName)}
  $result=Invoke-AppearanceControl 'UpdateOwnedResources';Assert ($result.ok -and $script:Reloads -eq 1 -and $script:Loads[0] -eq $expected) 'Existing-backend Automatic mode updates owned resources without calling the launcher'
  foreach($entry in $configsBefore.GetEnumerator()){Assert ((Get-ConfigHash $entry.Key) -eq $entry.Value) ('Existing-backend update preserves config: '+[IO.Path]::GetFileName([IO.Path]::GetDirectoryName($entry.Key)))}
  $count=$script:Reloads;Invoke-AppearanceControl 'UpdateOwnedResources'|Out-Null;Assert ($script:Reloads -eq $count) 'Repeated already-running mode does not repeat resource reload'
  New-Scenario 'mode-backend-gone';$script:Live=$false;$result=Invoke-AppearanceControl 'UpdateOwnedResources';Assert ($result.ok -and $script:Reloads -eq 0 -and !$script:Live -and (Read-PresetState).pendingReload) 'Already-running mode whose backend vanished only saves resources; never launches a replacement'
  New-Scenario 'replay-again';Invoke-AppearanceControl 'Ensure'|Out-Null
  $tree=Hashes (Join-Path $Out 'replay-again');Invoke-AppearanceControl 'Ensure'|Out-Null;Assert ((Same $tree (Hashes (Join-Path $Out 'replay-again'))) -and $script:Reloads -eq 1) 'Repeated Ensure is idempotent with no backup, write, or extra reload'
  Invoke-PresetRestore|Out-Null;Assert (!(Test-Path -LiteralPath (Join-Path $script:ThemeRoot 'YMCC Game Details\shared.css')) -and (Test-Path -LiteralPath (Join-Path $script:ThemeRoot 'User Custom Theme\config_USER.json'))) 'Original rollback remains usable after an owned package resource upgrade'
  New-Scenario 'read-only';$tree=Hashes (Join-Path $Out 'read-only');$status=Invoke-AppearanceControl 'Status';Assert ($status.snapshot.packUpdateAvailable -and (Same $tree (Hashes (Join-Path $Out 'read-only'))) -and $script:Reloads -eq 0) 'Status reports a pending resource update without applying or reloading it'
  New-Scenario 'editor';$script:Editing=$true;$tree=Hashes (Join-Path $Out 'editor');Invoke-AppearanceControl 'Ensure'|Out-Null;Assert ((Same $tree (Hashes (Join-Path $Out 'editor'))) -and $script:Reloads -eq 0) 'Desktop editor ownership postpones resource upgrade without rewriting files'
  New-Scenario 'foreign';$foreignFile=Join-Path $script:ThemeRoot 'User Custom Theme\config_USER.json';[IO.File]::WriteAllText($foreignFile,'{"active":true,"nested":{"value":"keep active"}}');$tree=Hashes (Join-Path $Out 'foreign');Invoke-AppearanceControl 'Ensure'|Out-Null;Assert ((Same $tree (Hashes (Join-Path $Out 'foreign'))) -and $script:Reloads -eq 0) 'New active foreign theme requires explicit reapply; automatic upgrade does not disable it'
  New-Scenario 'unowned' -NoOwner;$tree=Hashes (Join-Path $Out 'unowned');Invoke-AppearanceControl 'Ensure'|Out-Null;Assert ((Same $tree (Hashes (Join-Path $Out 'unowned'))) -and !(Read-PresetState)) 'An unowned profile never becomes owned merely from Ensure'
  Assert (Rejected {Invoke-PresetPrepare $false (Get-AppearanceStatus).revision -OwnedResourcesOnly}) 'Resource-only path cannot bypass explicit initial takeover'
  New-Scenario 'disabled' -DisableDetails;Invoke-AppearanceControl 'Ensure'|Out-Null;$d=Get-Content -LiteralPath (Join-Path $script:ThemeRoot 'YMCC Game Details\config_USER.json') -Raw|ConvertFrom-Json;Assert (!$d.active -and !($script:Rows|Where-Object name -eq 'YMCC Game Details').enabled -and (Get-ConfigHash (Join-Path $script:ThemeRoot 'YMCC Game Details\shared.css')) -eq $expected) 'Upgrading a disabled details theme preserves its disabled selection'
  New-Scenario 'stale';$current=Get-AppearanceStatus;$path=Join-Path $script:ThemeRoot 'Round\config_USER.json';[IO.File]::WriteAllText($path,'{"active":true,"Intensity":"15px"}');Assert (Rejected {Invoke-OwnedPresetResourceUpdate $current}) 'Changed settings reject a stale resource-update revision before writing CSS'
  Assert ((Get-ConfigHash (Join-Path $script:ThemeRoot 'YMCC Game Details\shared.css')) -eq $legacy) 'Stale rejection leaves old resource and current user selection intact'
  New-Scenario 'reload-failed';$script:Mode='reload-failed';Assert (Rejected {Invoke-AppearanceControl 'Ensure'}) 'Unconfirmed backend reload never reports successful update'
  Assert ((Read-PresetState).pendingReload -and (Get-AppearanceStatus).packPrepared) 'New files remain recoverable with an explicit pending reload after transport failure'
  $backup=(Read-PresetState).latestBackup;$script:Mode='ok';Invoke-AppearanceControl 'Ensure'|Out-Null;Assert (!(Read-PresetState).pendingReload -and (Read-PresetState).latestBackup -eq $backup -and $script:Reloads -eq 2) 'A later Ensure completes reread without recopying resources or creating another backup'
  New-Scenario 'write-failed';$baseline=Hashes $script:ThemeRoot;$oldDigest=(Read-PresetState).packDigest;$script:RealWrite=${function:Write-PresetBytes};$script:Writes=0
  function Write-PresetBytes([string]$Path,[byte[]]$Bytes){$script:Writes++;if($script:Writes -eq 2){throw 'fixture mid-upgrade disk failure'};& $script:RealWrite $Path $Bytes}
  Assert (Rejected {Invoke-AppearanceControl 'Ensure'}) 'Interrupted resource write reports failure'
  ${function:Write-PresetBytes}=$script:RealWrite
  Assert ((Same $baseline (Hashes $script:ThemeRoot)) -and (Read-PresetState).packDigest -eq $oldDigest -and $script:Reloads -eq 0) 'Mid-upgrade failure rolls back files and owner digest before any backend reread'
  New-Scenario 'pending-legacy';$state=Read-PresetState;$state.pendingReload=$true;Save-PresetState $state;Invoke-AppearanceControl 'Ensure'|Out-Null
  Assert ($script:Reloads -eq 1 -and $script:Loads[0] -eq $expected -and !(Read-PresetState).pendingReload) 'Legacy pending-reload receipt loads the new package rather than restarting with old CSS'
  New-Scenario 'editor-race';$baseline=Hashes $script:ThemeRoot;$oldDigest=(Read-PresetState).packDigest;$script:EditorChecks=0
  function Test-CssDesktopEditing {$script:EditorChecks++;return $script:EditorChecks -ge 3}
  Assert (Rejected {Invoke-AppearanceControl 'Ensure'}) 'Editor opened during resource backup prevents the update commit'
  function Test-CssDesktopEditing {$script:Editing}
  Assert ((Same $baseline (Hashes $script:ThemeRoot)) -and (Read-PresetState).packDigest -eq $oldDigest -and $script:Reloads -eq 0) 'Editor race preserves old files, user configs and owned receipt'
  New-Scenario 'invalid-source';$badPack=Join-Path $Out 'invalid-source-pack';Copy-Item -LiteralPath $currentPack -Destination $badPack -Recurse;[IO.File]::WriteAllText((Join-Path $badPack 'themes\YMCC Game Details\shared.css'),'fixture package corruption');$script:PackRoot=$badPack;$baseline=Hashes $script:ThemeRoot
  Invoke-AppearanceControl 'Ensure'|Out-Null;$invalid=Get-AppearanceStatus
  Assert (!$invalid.packAvailable -and $invalid.packError -and (Same $baseline (Hashes $script:ThemeRoot)) -and $script:Reloads -eq 0) 'Invalid new package is reported without replacing current themes or claiming an upgrade'
  New-Scenario 'same-package';$script:Live=$false;Invoke-PresetPrepare $false (Get-AppearanceStatus).revision|Out-Null;Complete-PresetReload;$script:Live=$true;$script:Rows=@(Get-ThemeRows|ForEach-Object{[pscustomobject]@{name=$_.name;enabled=$_.configured}})
  $target=Join-Path $script:ThemeRoot 'YMCC Game Details\shared.css';$focus=Join-Path $script:ThemeRoot 'Darken Unfocused Games\global.css';$meta=Join-Path $script:ThemeRoot 'Darken Unfocused Games\theme.json'
  [IO.File]::WriteAllText($target,'/* shop replaced managed details CSS */');[IO.File]::WriteAllText($focus,'.BasicUI .appportrait_LibraryItemBox_WYgDg{filter:brightness(.5)}')
  $shopMeta=Get-Content -LiteralPath $meta -Raw -Encoding UTF8|ConvertFrom-Json;$shopMeta.version='v9999.0-store';Write-ConfigAtomic $meta ($shopMeta|ConvertTo-Json -Depth 16)
  $configsBefore=@{};foreach($f in Get-ChildItem -LiteralPath $script:ThemeRoot -Filter config_USER.json -Recurse){$configsBefore[$f.FullName]=(Get-ConfigHash $f.FullName)}
  $status=Get-AppearanceStatus;Assert (!$status.packUpdateAvailable -and $status.packRepairRequired) 'Reproduce store replacement while the owned YMCC package digest is unchanged'
  $tree=Hashes (Join-Path $Out 'same-package');Invoke-AppearanceControl 'Status'|Out-Null;Assert ((Same $tree (Hashes (Join-Path $Out 'same-package'))) -and $script:Reloads -eq 0) 'Passive Status reports store differences without writes, reloads or startup'
  $result=Invoke-AppearanceControl 'UpdateOwnedResources'
  Assert ((Get-ConfigHash $target) -eq $expected -and (Get-ConfigHash $focus) -eq (Get-ConfigHash (Join-Path $currentPack 'themes\Darken Unfocused Games\global.css'))) 'Same-package store CSS is overwritten by exact bundled YMCC resources'
  Assert ((Get-ConfigHash $meta) -eq (Get-ConfigHash (Join-Path $currentPack 'themes\Darken Unfocused Games\theme.json'))) 'Even a higher store theme version cannot replace the YMCC resource authority'
  Assert ($result.ok -and $result.reloaded -and $script:Reloads -eq 1 -and !(Read-PresetState).pendingReload -and (Get-AppearanceStatus).packPrepared) 'Existing backend rereads repaired same-package resources once and confirms readiness'
  foreach($entry in $configsBefore.GetEnumerator()){Assert ((Get-ConfigHash $entry.Key) -eq $entry.Value) ('Store repair preserves config byte for byte: '+[IO.Path]::GetFileName([IO.Path]::GetDirectoryName($entry.Key)))}
  $backup=Read-PresetBackup (Read-PresetState).latestBackup;Assert ($backup.journal.entries.Count -eq 3) 'Only the three replaced managed resources are backed up before overwrite'
  $tree=Hashes (Join-Path $Out 'same-package');Invoke-AppearanceControl 'UpdateOwnedResources'|Out-Null;Assert ((Same $tree (Hashes (Join-Path $Out 'same-package'))) -and $script:Reloads -eq 1) 'Matching resources do not create another backup, write or reload'
  [IO.File]::WriteAllText($focus,'/* user is editing in Desktop */');$script:Editing=$true;$tree=Hashes (Join-Path $Out 'same-package');Invoke-AppearanceControl 'UpdateOwnedResources'|Out-Null
  Assert ((Same $tree (Hashes (Join-Path $Out 'same-package'))) -and $script:Reloads -eq 1) 'Desktop editor protection also defers same-package replacement'
  $script:Editing=$false;Invoke-AppearanceControl 'UpdateOwnedResources'|Out-Null;Assert ((Get-ConfigHash $focus) -eq (Get-ConfigHash (Join-Path $currentPack 'themes\Darken Unfocused Games\global.css')) -and $script:Reloads -eq 2) 'Next authorized use repairs differences after Desktop editing ends'
  [IO.File]::WriteAllText($focus,'/* another store update while CSS backend is absent */');$script:Live=$false;$result=Invoke-AppearanceControl 'UpdateOwnedResources'
  Assert ($result.ok -and $result.saved -and !$script:Live -and $script:Reloads -eq 2 -and (Read-PresetState).pendingReload -and (Get-ConfigHash $focus) -eq (Get-ConfigHash (Join-Path $currentPack 'themes\Darken Unfocused Games\global.css'))) 'Missing-backend store repair writes resources and leaves pending reload without starting CSS or Steam'

  # Permanent retirement reuses the production backup/update/reread transaction.
  function Add-RetiredFixture([string]$Folder='Game Cover Reflections'){
   $d=Join-Path $script:ThemeRoot $Folder;New-Item -ItemType Directory -Path $d -Force|Out-Null
   [IO.File]::WriteAllText((Join-Path $d 'theme.json'),'{{"name":"Game Cover Reflections","manifest_version":3,"inject":{{"shared.css":["SP"]}}}}'.Replace('{{','{').Replace('}}','}'))
   [IO.File]::WriteAllText((Join-Path $d 'config_USER.json'),'{{"active":true,"Blur":"1"}}'.Replace('{{','{').Replace('}}','}'))
   [IO.File]::WriteAllText((Join-Path $d 'shared.css'),'/* retired reflection */');[IO.File]::WriteAllText((Join-Path $d 'LICENSE'),'retired fixture license')
   $script:Rows+=@([pscustomobject]@{name='Game Cover Reflections';enabled=$true})
  }
  New-Scenario 'retired';Add-RetiredFixture
  $saved=@{};foreach($f in Get-ChildItem -LiteralPath $script:ThemeRoot -Filter config_USER.json -File -Recurse){if($f.Directory.Name -ne 'Game Cover Reflections'){$saved[$f.FullName]=Get-ConfigHash $f.FullName}}
  $before=Get-AppearanceStatus;Assert ($before.packRepairRequired -and $before.foreignActiveCount -eq 0 -and $before.backendReloadRequired) 'Retired reflection requests migration without blocking the owned resource update as an unrelated user theme'
  Invoke-AppearanceControl 'UpdateOwnedResources'|Out-Null
  Assert (@(Get-RetiredPresetTargets).Count -eq 0 -and $script:Reloads -eq 1 -and @($script:Rows|Where-Object name -eq 'Game Cover Reflections').Count -eq 0 -and (Get-AppearanceStatus).packPrepared) 'Owned migration removes the four reflection files and rereads the same backend exactly once'
  foreach($entry in $saved.GetEnumerator()){Assert ((Get-ConfigHash $entry.Key) -eq $entry.Value) ('Retirement preserves current user config: '+[IO.Path]::GetFileName([IO.Path]::GetDirectoryName($entry.Key)))}
  Invoke-AppearanceControl 'UpdateOwnedResources'|Out-Null;Assert ($script:Reloads -eq 1) 'Repeated retirement checks do not repeat backend reloads'
  Add-RetiredFixture 'Store Copy';Invoke-AppearanceControl 'UpdateOwnedResources'|Out-Null
  Assert (!(Test-Path -LiteralPath (Join-Path $script:ThemeRoot 'Store Copy\theme.json')) -and $script:Reloads -eq 2) 'A later store download with an alternate folder is removed once and cannot restore the retired effect'
  Add-RetiredFixture;$script:Editing=$true;$baseline=Hashes $script:ThemeRoot;Invoke-AppearanceControl 'UpdateOwnedResources'|Out-Null
  Assert ((Same $baseline (Hashes $script:ThemeRoot)) -and $script:Reloads -eq 2) 'Desktop editor protection defers retirement without touching user files or reloading'
  $script:Editing=$false;$script:Live=$false;Invoke-AppearanceControl 'UpdateOwnedResources'|Out-Null
  Assert (@(Get-RetiredPresetTargets).Count -eq 0 -and !$script:Live -and $script:Reloads -eq 2 -and (Read-PresetState).pendingReload) 'Offline retirement only updates files and leaves pending reload without starting CSS or Steam'
  New-Scenario 'retired-delete-failed';Add-RetiredFixture;$baseline=Hashes $script:ThemeRoot;$oldDigest=(Read-PresetState).packDigest
  $script:FailRetiredDelete=$true
  function Remove-Item { [CmdletBinding()]param([string]$LiteralPath,[switch]$Force) if($script:FailRetiredDelete -and $LiteralPath -like '*Game Cover Reflections*shared.css'){$script:FailRetiredDelete=$false;throw 'fixture retired delete failure'};Microsoft.PowerShell.Management\Remove-Item @PSBoundParameters }
  Assert (Rejected {Invoke-AppearanceControl 'UpdateOwnedResources'}) 'A mid-retirement file deletion failure is surfaced'
  Microsoft.PowerShell.Management\Remove-Item Function:\Remove-Item -ErrorAction SilentlyContinue
  Assert ((Same $baseline (Hashes $script:ThemeRoot)) -and (Read-PresetState).packDigest -eq $oldDigest -and $script:Reloads -eq 0) 'Retirement rollback restores already deleted files and the owner receipt before any backend operation'
  New-Scenario 'retired-original-restore' -NoOwner;Add-RetiredFixture;Add-RetiredFixture 'Original Store Copy';$script:Live=$false;Invoke-PresetPrepare|Out-Null;Invoke-PresetRestore|Out-Null
  Assert (@(Get-RetiredPresetTargets).Count -eq 0 -and !(Test-Path -LiteralPath (Join-Path $script:ThemeRoot 'Original Store Copy\theme.json'))) 'Explicit previous-config restore retains historical backups without reintroducing retired themes under either canonical or alternate folders'
  Assert ($script:Stops -eq 0) 'All cases preserve processes and avoid CDP cleanup or Steam operations'
 }
 [ordered]@{ok=$true;passed=$cases.Count;cases=@($cases);componentRoot=$ComponentRoot;scope='Actual production Ensure/Prepare/reload/transaction in fresh isolated profiles; RPC and process visibility are explicit in-memory adapters; no installed or user-profile writes';realSteamOperations=0;installedApplicationModified=$false}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
 Write-Output ('PASS: '+$cases.Count+' owned theme upgrade checks')
}finally{$env:LOCALAPPDATA=$oldLocal;$env:USERPROFILE=$oldUser}
