[CmdletBinding()]
param([string]$ValidationRoot='G:\YeManCC-Work\Mainline\Build\Validation\TrainerCyberpunkTitleFix-20261015')
$ErrorActionPreference='Stop'
Import-Module (Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
$module=Get-Module TrainerBackend
$testRoot=Join-Path $ValidationRoot ('shared-target-tests-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$exe=Join-Path $testRoot 'Cyberpunk2077.exe';$db=Join-Path $testRoot 'db';$gate=Join-Path $testRoot 'game-target.json'
[IO.Directory]::CreateDirectory($db)|Out-Null;[IO.File]::WriteAllText($exe,'Never execute; identity fixture')
$rows=@(@{en_US='Cyberpunk 2077';zh_CN='赛博朋克 2077';steam_id=1091500},@{en_US='Cyberpunk 2078';zh_CN='赛博朋克 2078';steam_id=1091501},@{en_US='Dead by Daylight';steam_id=381210})
function Json([string]$path,$value){[IO.File]::WriteAllText($path,(ConvertTo-Json -InputObject $value -Depth 10),[Text.UTF8Encoding]::new($false))}
Json (Join-Path $db 'translations.json') $rows
& $module {param($p,$g)$script:titleObservation=@{pid=35788;created='134365342855297510';path=$p;title='Cyberpunk 2077 (C) 2020 by CD Projekt RED'};$script:TrainerGameTargetPath=$g;function script:Get-TrainerProcessObservation([int]$ProcessId,[bool]$RequireTitle=$true){return $script:titleObservation}} $exe $gate
function Gate([hashtable]$Extra=@{}){$t=@{valid=$true;pid=35788;processCreated='134365342855297510';path=$exe;title='Cyberpunk 2077 (C) 2020 by CD Projekt RED';name='cyberpunk2077';generation=1;lastSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()};foreach($k in $Extra.Keys){$t[$k]=$Extra[$k]};Json $gate $t}
Gate
$checks=[Collections.Generic.List[object]]::new()
function Assert($value,[string]$message='assertion failed'){if(-not $value){throw $message}}
function Reject([scriptblock]$body){$rejected=$false;try{& $body|Out-Null}catch{$rejected=$true};Assert $rejected 'Expected rejection'}
function Check([string]$name,[scriptblock]$body){try{& $body;$checks.Add(@{name=$name;ok=$true});Write-Output ('PASS '+$name)}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message});Write-Output ('FAIL '+$name+': '+$_.Exception.Message)}}
function Identity([string]$name='cyberpunk2077'){Resolve-TrainerIdentity $name $exe $db @{games=@{}} (Join-Path $testRoot 'no-library') 35788 '134365342855297510'}
Check '2077 consumes the same fresh native game-target as the top menu' {$r=Identity;Assert ($r.method -eq 'ymcc-game-target' -and $r.steamAppId -eq '1091500' -and $r.snapshotPath -eq $gate)}
Check 'canonical name and executable stem share the admitted target' {$r=Identity 'Cyberpunk 2077';Assert ($r.steamAppId -eq '1091500')}
Check 'another sequel cannot borrow the selected target' {Reject {Identity 'Cyberpunk 2078'}}
Check 'an expansion cannot borrow the selected base-game identity' {Reject {Identity 'Cyberpunk 2077 Phantom Liberty'}}
Check 'wrong birth remains rejected with the correct game name' {Reject {Resolve-TrainerIdentity 'cyberpunk2077' $exe $db @{games=@{}} (Join-Path $testRoot 'none') 35788 '134365342855297511'}}
Check 'raw copyright title remains diagnostic evidence' {$r=Identity;Assert ($r.windowTitle -ceq 'Cyberpunk 2077 (C) 2020 by CD Projekt RED' -and $r.gameName -eq 'cyberpunk2077')}
Check 'a missing shared snapshot cannot start a second recognizer' {[IO.File]::Delete($gate);try{Reject {Identity}}finally{Gate}}
Check 'expired snapshot is rejected even while its process is running' {Gate @{lastSeen=([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-11000)};try{Reject {Identity}}finally{Gate}}
Check 'future timestamp does not grant an indefinite target lease' {Gate @{lastSeen=([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()+60000)};try{Reject {Identity}}finally{Gate}}
Check 'revoked target and changed tuple cannot lend their identity' {foreach($x in @(@{valid=$false},@{pid=35789},@{processCreated='134365342855297511'},@{path=($exe+'.other')},@{generation=0})){Gate $x;try{Reject {Identity}}finally{Gate}}}
Check 'the shared top policy preserves legitimate by in titles' {Gate @{title='Dead by Daylight';name='shipping'};try{$r=Identity 'Dead by Daylight';Assert ($r.steamAppId -eq '381210');Reject {Identity 'Dead'}}finally{Gate}}
Check 'shared copyright and launch-mode cleanup agrees with top naming' {foreach($title in @('Cyberpunk 2077 (c) 2020','Cyberpunk 2077 [C] 2020 by CD Projekt RED','Cyberpunk 2077 © 2020 by CD Projekt RED','Cyberpunk 2077 Copyright 2020 by CD Projekt RED','Cyberpunk 2077 - DX12')){Gate @{title=$title;name='shipping'};try{$r=Identity;Assert ($r.steamAppId -eq '1091500') $title}finally{Gate}}}
Check 'publisher words without copyright are not stripped' {Gate @{title='Cyberpunk 2077 by Another Game';name='shipping'};try{Reject {Identity}}finally{Gate}}
Check 'titleless admitted games can use their existing top process name' {Gate @{title=''};try{$r=Identity;Assert ($r.method -eq 'ymcc-game-target')}finally{Gate}}
$corpus=@('Cyberpunk 2077 (C) 2020 by CD Projekt RED','Cyberpunk 2077 © 2020 by CD Projekt RED','Dead by Daylight','Dead by Daylight (C) 2016 by Behaviour','Cyberpunk 2077 - DX12','Cyberpunk 2077 (Vulkan)','Cyberpunk 2078','Cyberpunk 2077 Phantom Liberty','TMNT.exe',"Teenage Mutant Ninja Turtles: Shredder's Revenge",'赛博朋克 2077','Cyberpunk 2077 by Another Game','Game DX12 Vulkan','Game [C] 2020','Game Copyright 2020 by Publisher','Game (C) 2020 Deluxe Edition')
$normalized=@(foreach($title in $corpus){@{input=$title;cleaned=(& $module {param($v)ConvertTo-YmccGameTitle $v} $title)}})
Json (Join-Path $ValidationRoot 'title-policy-corpus.json') $normalized
$failed=@($checks|Where-Object{-not $_.ok});$report=@{passed=$checks.Count-$failed.Count;failed=$failed.Count;checks=$checks.ToArray();root=$testRoot;networkRequests=0;realLaunches=0}
Json (Join-Path $ValidationRoot 'window-title-tests.json') $report;[pscustomobject]$report|Select-Object passed,failed,root|ConvertTo-Json
if($failed.Count){exit 1}
