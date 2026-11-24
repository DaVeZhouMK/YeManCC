param([string]$Out='G:\YeManCC-Work\Mainline\Temp\steam-appearance-validation\preset-v2',[string]$ComponentRoot='',[ValidateSet('All','DirectoryPathGuards','ForeignBounded')][string]$Case='All')
$ErrorActionPreference='Stop';if(!$ComponentRoot){$ComponentRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'};$source=Join-Path $ComponentRoot 'steam-appearance.ps1';# Offline fixtures must not query live Steam/CSS ownership.
function Get-Process {param($Name,$Id) @()}
function Get-NetTCPConnection {param($State,$LocalPort) @()}
if($Case -ne 'All'){. (Join-Path $PSScriptRoot 'ymcc_sim\global\decky-update-recovery\appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out}else{. $source -Action Library}
function Get-AppearanceSteamSession {$null} # Explicit offline public Steam facts for this owned fixture.
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$script:ThemeRoot=Join-Path $Out ('other-user-'+[guid]::NewGuid().ToString('N'));$script:PresetStateBase=Join-Path $Out 'states'
New-Item -ItemType Directory -Path $script:ThemeRoot -Force|Out-Null
function Get-BackendProcesses {@()};function Test-BackendOwner {$false}
$cases=New-Object Collections.Generic.List[string]
function Assert($value,[string]$message){if(!$value){throw $message};$cases.Add($message)}
function FixtureTheme([string]$Folder,[string]$Name,[string]$Config){$dir=Join-Path $script:ThemeRoot $Folder;New-Item -ItemType Directory -Path $dir -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $dir 'theme.json'),(@{name=$Name;manifest_version=1;inject=@{}}|ConvertTo-Json -Compress));[IO.File]::WriteAllText((Join-Path $dir 'config_USER.json'),$Config);$dir}
if($Case -eq 'All'){
$foreign=FixtureTheme 'Other User Theme' 'Other User Theme' '{"active":true,"Tone":"custom","nested":{"keep":23}}'
$duplicate=FixtureTheme 'Another Art Hero' 'Art Hero' '{"active":true,"Custom":"original"}'
$broken=FixtureTheme 'Broken Config' 'Broken Config' '{broken json'
$original=@{};foreach($file in Get-ChildItem -LiteralPath $script:ThemeRoot -File -Recurse){$original[$file.FullName]=[Convert]::ToBase64String([IO.File]::ReadAllBytes($file.FullName))}
$empty=@(Get-ThemeRows)
Assert ($empty.Count -eq 7 -and @($empty|Where-Object configured).Count -eq 7 -and @($empty|Where-Object installed).Count -eq 0) 'New profile selects all seven defaults without claiming installed files or creating themes'
Assert (!(Test-Path -LiteralPath (Join-Path $script:ThemeRoot 'Round'))) 'Reading defaults never installs or starts anything'
$before=Get-AppearanceStatus;Assert ($before.packAvailable -and !$before.packPrepared -and $before.foreignActiveCount -eq 3) 'New user is detected with active foreign, duplicate and malformed configurations'
$prepared=Invoke-PresetPrepare $false $before.revision
Assert ($prepared.ok -and $prepared.snapshot.packPrepared -and $prepared.snapshot.canRestore -and $prepared.snapshot.backendReloadRequired) 'One prepare installs a complete bundled preset with verified backup and reload requirement'
Assert (@($prepared.snapshot.themes|Where-Object installed).Count -eq $script:ThemeNames.Count) 'All managed themes are installed without relying on DaVe user paths'
$c=[IO.File]::ReadAllText((Join-Path $foreign 'config_USER.json'))|ConvertFrom-Json;Assert (!$c.active -and $c.Tone -eq 'custom' -and $c.nested.keep -eq 23) 'YMCC wins while foreign parameter values are preserved and inactive'
$j=[IO.File]::ReadAllText((Join-Path $duplicate 'theme.json'))|ConvertFrom-Json;Assert ($j.name -like '_ymcc-paused-*') 'Duplicate managed theme names are quarantined reversibly instead of deleting user folders'
Assert (([IO.File]::ReadAllText((Join-Path $broken 'config_USER.json'))|ConvertFrom-Json).active -eq $false) 'Malformed foreign config is safely paused while raw original is backed up'
$round=Join-Path $script:ThemeRoot 'Round\config_USER.json';$c=[IO.File]::ReadAllText($round)|ConvertFrom-Json;$c.active=$false;$c.Intensity='30px';Write-ConfigAtomic $round ($c|ConvertTo-Json -Compress)
$assets=Join-Path $script:ThemeRoot 'Round\shared.css';[IO.File]::WriteAllText($assets,'other user replaced this css')
$status=Get-AppearanceStatus;Assert ($status.packRepairRequired -and !$status.packPrepared) 'Modified managed assets are detected as needing repair'
$repair=Invoke-PresetPrepare $false $status.revision;$c=[IO.File]::ReadAllText($round)|ConvertFrom-Json
Assert ($repair.ok -and $c.Intensity -eq '30px' -and !$c.active) 'Repair prioritizes bundled CSS without resetting valid owned switches or parameter values'
$reset=Invoke-PresetPrepare $true;
Assert (@($reset.snapshot.themes|Where-Object {$_.installed -and $_.configured}).Count -eq $script:ThemeNames.Count) 'Explicit reset restores all managed enabled defaults, not the last personal selection'
Assert ((Get-Content -LiteralPath (Join-Path $script:ThemeRoot 'Art Hero\config_USER.json') -Raw|ConvertFrom-Json).'Footer Options' -eq 'Default') 'Reset keeps full-size footer button glyphs from the confirmed user reference'
foreach($name in $script:ThemeNames){Assert ((Get-ConfigHash (Join-Path $script:ThemeRoot ($name+'\config_USER.json'))) -eq (Get-ConfigHash (Join-Path $script:PackRoot ('themes\'+$name+'\config_USER.json')))) ('Reset copies exact bundled default bytes: '+$name)}
$c=[IO.File]::ReadAllText($round)|ConvertFrom-Json
Assert ($reset.ok -and $c.Intensity -eq '10px' -and $c.active) 'Explicit reset restores the packaged enabled defaults and exact user preset parameters'
$restored=Invoke-PresetRestore
Assert ($restored.ok -and !$restored.snapshot.packPrepared) 'Restore returns ownership to pre-YMCC configuration'
foreach($entry in $original.GetEnumerator()){Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($entry.Key)) -eq $entry.Value) ('Original foreign/duplicate/malformed bytes restored exactly: '+[IO.Path]::GetFileName([IO.Path]::GetDirectoryName($entry.Key))+'/'+[IO.Path]::GetFileName($entry.Key))}
Assert (@(Get-ChildItem -LiteralPath $script:ThemeRoot -Directory|Where-Object Name -in $script:ThemeNames).Count -eq 0) 'Restore removes only newly created managed files and empty folders'
# Stale revision must not create ownership or overwrite anything.
$old=Get-AppearanceStatus;[IO.File]::WriteAllText((Join-Path $foreign 'config_USER.json'),'{"active":true,"Tone":"latest"}')
# Revision deliberately covers managed configs; exercise an actual managed config change.
$dir=FixtureTheme 'Round' 'Round' '{"active":true,"Intensity":"15px"}';$old=Get-AppearanceStatus;[IO.File]::WriteAllText((Join-Path $dir 'config_USER.json'),'{"active":false,"Intensity":"15px"}')
$rejected=$false;try{Invoke-PresetPrepare $false $old.revision|Out-Null}catch{$rejected=$true};Assert $rejected 'Stale managed revision is rejected before takeover'
# Broken source pack fails before touching live profiles.
$validPack=$script:PackRoot;$badPack=Join-Path $Out ('tampered-pack-'+[guid]::NewGuid().ToString('N'));Copy-Item -LiteralPath $validPack -Destination $badPack -Recurse;$script:PackRoot=$badPack
[IO.File]::WriteAllText((Join-Path $badPack 'themes\Round\shared.css'),'tampered');$beforeHash=Get-ConfigHash (Join-Path $dir 'config_USER.json');$rejected=$false;try{Invoke-PresetPrepare|Out-Null}catch{$rejected=$true};Assert ($rejected -and (Get-ConfigHash (Join-Path $dir 'config_USER.json')) -eq $beforeHash) 'Source checksum tampering is rejected before any live write';$script:PackRoot=$validPack
$escaped=$false;try{Get-PresetRelative '..\outside.json'|Out-Null}catch{$escaped=$true};Assert $escaped 'Path traversal is rejected'
# Mid-transaction failure restores raw files; no half-installed owner marker.
$baseline=@{};foreach($file in Get-ChildItem -LiteralPath $script:ThemeRoot -File -Recurse){$baseline[$file.FullName]=Get-ConfigHash $file.FullName}
$script:RealWrite=${function:Write-PresetBytes};$script:Writes=0
function Write-PresetBytes([string]$Path,[byte[]]$Bytes){$script:Writes++;if($script:Writes -eq 4){throw 'fixture disk failure'};& $script:RealWrite $Path $Bytes}
$failed=$false;try{Invoke-PresetPrepare|Out-Null}catch{$failed=$true}
Assert $failed 'Injected disk failure is reported rather than optimistic success'
foreach($entry in $baseline.GetEnumerator()){Assert ((Get-ConfigHash $entry.Key) -eq $entry.Value) ('Transaction rollback preserved: '+[IO.Path]::GetFileName([IO.Path]::GetDirectoryName($entry.Key))+'/'+[IO.Path]::GetFileName($entry.Key))}
[pscustomobject]@{ok=$true;scope='Isolated second-user profile; packaged source themes; no production config changes';cases=@($cases)}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'preset-result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) bundled preset / other-user / reset / restore checks"
}

if($Case -eq 'ForeignBounded'){
 $limit=262144
 $bom=[Text.UTF8Encoding]::new($true)
 function Write-SizedJson([string]$Path,[string]$Json,[int]$Size){
  $padding=$Size-$bom.GetPreamble().Length-$bom.GetByteCount($Json)
  [IO.File]::WriteAllText($Path,($Json+(' '*$padding)),$bom)
 }
 $foreignName='Boundary '+[char]0x754c
 $valid=FixtureTheme 'exact' $foreignName '{"active":true}'
 $bigManifest=FixtureTheme 'large-manifest' 'Large Manifest' '{"active":false}'
 $bigConfig=FixtureTheme 'large-config' 'Large Config' '{"active":true}'
 $badActive=FixtureTheme 'bad-active' 'Bad Active' '{"active":"true"}'
 $missing=FixtureTheme 'missing-config' 'Missing Config' '{"active":false}'
 $live=FixtureTheme 'backend-only' 'Backend Only' '{"active":false}'
 [IO.File]::Delete((Join-Path $missing 'config_USER.json'))
 Write-SizedJson (Join-Path $valid 'theme.json') (@{name=$foreignName}|ConvertTo-Json -Compress) $limit
 Write-SizedJson (Join-Path $valid 'config_USER.json') '{"active":true}' $limit
 Write-SizedJson (Join-Path $bigManifest 'theme.json') '{"name":"Large Manifest"}' ($limit+1)
 Write-SizedJson (Join-Path $bigConfig 'config_USER.json') '{"active":true}' ($limit+1)

 $script:ForeignMetadataQueries=0
 function Get-Item([string]$LiteralPath){
  if([IO.Path]::GetFileName($LiteralPath) -in @('theme.json','config_USER.json')){$script:ForeignMetadataQueries++}
  Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath
 }
 $rows=@{}
 try{
  foreach($row in (Get-ForeignThemeRows @([pscustomobject]@{name='Backend Only';enabled=$true}))){$rows[$row.folder]=$row}
 }finally{Remove-Item -LiteralPath Function:\Get-Item}
 Assert ($rows.Count -eq 6) 'ForeignBounded returns all six fixture rows'
 $row=$rows['exact'];Assert ($row.name -ceq $foreignName -and $row.active -and $row.needsDisable -and !$row.invalid -and !$row.manifestInvalid) 'ForeignBounded preserves BOM decoding and accepts both JSON files at the inclusive limit'
 $row=$rows['large-manifest'];Assert ($row.name -ceq 'large-manifest' -and $row.manifestInvalid -and $row.needsDisable -and !$row.invalid) 'ForeignBounded oversized manifest gets the normal invalid-manifest fallback'
 $row=$rows['large-config'];Assert ($row.invalid -and !$row.active -and $row.needsDisable -and !$row.manifestInvalid) 'ForeignBounded oversized config is invalid rather than parsed as active'
 $row=$rows['bad-active'];Assert ($row.invalid -and !$row.active -and $row.needsDisable) 'ForeignBounded rejects string active values'
 $row=$rows['missing-config'];Assert (!$row.invalid -and !$row.active -and !$row.needsDisable) 'ForeignBounded missing config remains inactive and valid'
 $row=$rows['backend-only'];Assert ($row.live -and !$row.active -and $row.needsDisable) 'ForeignBounded backend-only conflict is still detected'

 $backendOnly=[pscustomobject]@{name='bAcKeNd OnLy';enabled=$true}
 $disabled=[pscustomobject]@{name='Backend Only';enabled=$false}
 $otherFlags=@(1..40 | ForEach-Object {[pscustomobject]@{name="Other $_";enabled=$false}})
 foreach($example in @(
   @{label='singleton backend object';themes=$backendOnly;expected=$true},
   @{label='large case-insensitive response with a disabled duplicate';themes=$otherFlags+@($disabled,$backendOnly);expected=$true},
   @{label='large response with entirely disabled rows';themes=$otherFlags+@($disabled);expected=$false},
   @{label='large response containing a legacy array name';themes=$otherFlags+@([pscustomobject]@{name=@('Other','Backend Only');enabled=$true});expected=$true},
   @{label='large response containing a legacy truthy flag';themes=$otherFlags+@([pscustomobject]@{name='Backend Only';enabled='false'});expected=$true}
 )){
  $probe=@(Get-ForeignThemeRows $example.themes|Where-Object folder -ceq 'backend-only')
  Assert ($probe.Count -eq 1 -and $probe[0].live -eq $example.expected -and $probe[0].needsDisable -eq $example.expected) ('ForeignBounded preserves backend conflict detection: '+$example.label)
 }

 $hint=Join-Path $Out 'bounded-hint.json'
 Write-SizedJson $hint '{}' $limit
 Assert ((Read-PresetTargetHintText $hint).TrimEnd() -ceq '{}') 'ForeignBounded target hint accepts the same inclusive byte limit'
 Write-SizedJson $hint '{}' ($limit+1)
 $hintError=''
 try{Read-PresetTargetHintText $hint|Out-Null}catch{$hintError=$_.Exception.Message}
 Assert ($hintError -ceq 'Invalid target hint file') 'ForeignBounded target hint preserves oversize rejection and message'
 Assert ($script:ForeignMetadataQueries -eq 0) ('ForeignBounded removes detached Get-Item length queries (observed '+$script:ForeignMetadataQueries+')')
 [pscustomobject]@{ok=$true;selectedCase=$Case;cases=@($cases);foreignRows=$rows.Count;metadataQueries=$script:ForeignMetadataQueries}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'preset-result.json') -Encoding UTF8
 Write-Output ('PASS: '+$cases.Count+' preset ForeignBounded checks')
}
if($Case -eq 'DirectoryPathGuards'){
 $guardLinks=New-Object Collections.Generic.List[object]
 $guardTarget=Join-Path $Out 'guard-link-target';New-Item -ItemType Directory -Path $guardTarget -Force|Out-Null
 $guardTargetFile=Join-Path $guardTarget 'kept.txt';[IO.File]::WriteAllText($guardTargetFile,'owned-target-must-survive')
 $guardTargetHash=Get-ConfigHash $guardTargetFile
 $themeFile=Join-Path $script:ThemeRoot 'Round\normal.css';New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($themeFile)) -Force|Out-Null;[IO.File]::WriteAllText($themeFile,'normal')
 $stateRoot=Get-PresetStateDir;$stateFile=Join-Path $stateRoot 'backups\owned\files\0001.bin';New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($stateFile)) -Force|Out-Null;[IO.File]::WriteAllText($stateFile,'state')
 function GuardReject([scriptblock]$Work){try{&$Work|Out-Null;return $false}catch{return $true}}
 function GuardTheme([string]$Path){(Assert-ThemeTarget $Path) -ceq [IO.Path]::GetFullPath($Path)}
 function GuardState([string]$Path){(Assert-PresetStateTarget $Path) -ceq [IO.Path]::GetFullPath($Path)}
 function GuardLink([string]$Path,[string]$Target,[string]$Kind){
  $null=New-Item -ItemType $Kind -Path $Path -Target $Target -ErrorAction Stop
  if(!([IO.File]::GetAttributes($Path) -band [IO.FileAttributes]::ReparsePoint)){throw 'OWNED_REPARSE_LINK_NOT_CREATED'}
  $guardLinks.Add([pscustomobject]@{path=$Path;directory=([IO.File]::GetAttributes($Path) -band [IO.FileAttributes]::Directory) -ne 0})
 }
 try {
  Assert (GuardTheme $themeFile) 'PathGuards existing theme file is accepted'
  Assert (GuardTheme (Join-Path $script:ThemeRoot 'Round\missing.css')) 'PathGuards absent theme leaf remains accepted'
  Assert (GuardTheme (Join-Path $script:ThemeRoot 'new folder\nested\missing.css')) 'PathGuards absent theme ancestry remains accepted'
  Assert (GuardState $stateFile) 'PathGuards existing state file is accepted'
  Assert (GuardState (Join-Path $stateRoot 'not-yet\journal.json')) 'PathGuards absent state ancestry remains accepted'
  $literal=Join-Path $script:ThemeRoot 'Round\literal[1]-空间.css';[IO.File]::WriteAllText($literal,'literal')
  Assert (GuardTheme $literal) 'PathGuards literal brackets and Unicode path are accepted'
  Assert (GuardTheme $themeFile.ToUpperInvariant()) 'PathGuards Windows path casing keeps original full-path return'
  Assert (GuardReject {Assert-ThemeTarget $guardTargetFile}) 'PathGuards theme root escape is rejected'
  Assert (GuardReject {Assert-PresetStateTarget $themeFile}) 'PathGuards state root escape is rejected'
  foreach($attribute in @([IO.FileAttributes]::Hidden,[IO.FileAttributes]::ReadOnly)){
   $prior=[IO.File]::GetAttributes($literal);try{[IO.File]::SetAttributes($literal,$prior -bor $attribute);Assert (GuardTheme $literal) ('PathGuards ordinary file attribute is accepted: '+$attribute)}finally{[IO.File]::SetAttributes($literal,$prior)}
  }
  $script:GuardRemovePath=''
  function Test-Path {param([string]$LiteralPath)
   if($script:GuardRemovePath -and [string]::Equals($LiteralPath,$script:GuardRemovePath,[StringComparison]::OrdinalIgnoreCase)){
    $script:GuardRemovePath='';[IO.File]::Delete($LiteralPath);return $true
   }
   Microsoft.PowerShell.Management\Test-Path -LiteralPath $LiteralPath
  }
  try{
   $raceTheme=Join-Path $script:ThemeRoot 'Round\race.css';[IO.File]::WriteAllText($raceTheme,'race');$script:GuardRemovePath=$raceTheme
   Assert (GuardReject {Assert-ThemeTarget $raceTheme}) 'PathGuards vanished theme leaf after existence stays rejected'
   $raceState=Join-Path $stateRoot 'race.bin';[IO.File]::WriteAllText($raceState,'race');$script:GuardRemovePath=$raceState
   Assert (GuardReject {Assert-PresetStateTarget $raceState}) 'PathGuards vanished state leaf after existence stays rejected'
  }finally{Remove-Item -LiteralPath Function:\Test-Path -ErrorAction Stop;$script:GuardRemovePath=''}
  $themeJunction=Join-Path $script:ThemeRoot 'linked-theme';GuardLink $themeJunction $guardTarget 'Junction'
  Assert (GuardReject {Assert-ThemeTarget (Join-Path $themeJunction 'kept.txt')}) 'PathGuards theme parent reparse is rejected'
  Assert (GuardReject {Assert-ThemeTarget $themeJunction}) 'PathGuards theme directory leaf reparse is rejected'
  $stateJunction=Join-Path $stateRoot 'linked-state';GuardLink $stateJunction $guardTarget 'Junction'
  Assert (GuardReject {Assert-PresetStateTarget (Join-Path $stateJunction 'kept.txt')}) 'PathGuards state parent reparse is rejected'
  Assert (GuardReject {Assert-PresetStateTarget $stateJunction}) 'PathGuards state directory leaf reparse is rejected'
 }finally{
  foreach($link in $guardLinks){
   $fullLink=[IO.Path]::GetFullPath($link.path);$fullOut=[IO.Path]::GetFullPath($Out).TrimEnd('\')+'\'
   if(!$fullLink.StartsWith($fullOut,[StringComparison]::OrdinalIgnoreCase) -or !([IO.File]::GetAttributes($fullLink) -band [IO.FileAttributes]::ReparsePoint)){throw 'OWNED_LINK_CLEANUP_BOUNDARY'}
   if($link.directory){[IO.Directory]::Delete($fullLink,$false)}else{[IO.File]::Delete($fullLink)}
  }
 }
 Assert ((Get-ConfigHash $guardTargetFile) -ceq $guardTargetHash) 'PathGuards link targets retain exact bytes after link disposal'
 Assert (@($guardLinks|Where-Object{Microsoft.PowerShell.Management\Test-Path -LiteralPath $_.path}).Count -eq 0) ('PathGuards owned reparse links are disposed: '+$guardLinks.Count)
 [pscustomobject]@{ok=$true;scope='Exact isolated theme/state path guards; original root containment and fresh existence checks; no installed process IO';selectedCase=$Case;cases=@($cases);ownedLinksDisposed=$true;realSteamOperations=0;realCssLaunches=0;realUserConfigWrites=0}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'preset-result.json') -Encoding UTF8
 Write-Output ('PASS: '+$cases.Count+' preset PathGuards checks')
}
