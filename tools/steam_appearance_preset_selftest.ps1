param([string]$Out='G:\YeManCC-Work\Mainline\Temp\steam-appearance-validation\preset-v2')
$ErrorActionPreference='Stop';$source=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl\steam-appearance.ps1';# Offline fixtures must not query live Steam/CSS ownership.
function Get-Process {param($Name,$Id) @()}
function Get-NetTCPConnection {param($State,$LocalPort) @()}
. $source -Action Library
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$script:ThemeRoot=Join-Path $Out ('other-user-'+[guid]::NewGuid().ToString('N'));$script:PresetStateBase=Join-Path $Out 'states'
New-Item -ItemType Directory -Path $script:ThemeRoot -Force|Out-Null
function Get-BackendProcesses {@()};function Test-BackendOwner {$false}
$cases=New-Object Collections.Generic.List[string]
function Assert($value,[string]$message){if(!$value){throw $message};$cases.Add($message)}
function FixtureTheme([string]$Folder,[string]$Name,[string]$Config){$dir=Join-Path $script:ThemeRoot $Folder;New-Item -ItemType Directory -Path $dir -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $dir 'theme.json'),(@{name=$Name;manifest_version=1;inject=@{}}|ConvertTo-Json -Compress));[IO.File]::WriteAllText((Join-Path $dir 'config_USER.json'),$Config);$dir}
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