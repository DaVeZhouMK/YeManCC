[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$Phase='baseline')
$ErrorActionPreference='Stop'
Import-Module (Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
$module=Get-Module TrainerBackend
function Json([string]$p,$v){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null;[IO.File]::WriteAllText($p,(ConvertTo-Json -InputObject $v -Depth 12),[Text.Encoding]::UTF8)}
$sample=[IO.File]::ReadAllText((Join-Path $ValidationRoot 'steam/metadata-selection.json'),[Text.Encoding]::UTF8)|ConvertFrom-Json
$db=Join-Path $ValidationRoot 'fixtures/gcm/db';$library=Join-Path $ValidationRoot 'fixtures/library'
$catalog=@(& $module {param($d,$l) Get-FlingCatalogRows $d $l} $db $library)
$translations=@(& $module {param($d) Get-GcmCatalogRows $d 'translations.json'} $db)
$rows=[Collections.Generic.List[object]]::new()
foreach($game in $sample.eligible){
 $gameRoot=Join-Path $ValidationRoot ('simulation/'+$game.appid)
 $steamapps=Join-Path $gameRoot 'steamapps';$exe=Join-Path $steamapps 'common/SampleGame/bin/Game.exe'
 [IO.Directory]::CreateDirectory((Split-Path $exe))|Out-Null;[IO.File]::WriteAllText($exe,'Simulation-only placeholder; never execute')
 $acf='"AppState"'+"`n{`n"+'"appid" "'+$game.appid+'"'+"`n"+'"name" "'+$game.name.Replace('"','')+'"'+"`n"+'"installdir" "SampleGame"'+"`n}"
 [IO.File]::WriteAllText((Join-Path $steamapps ('appmanifest_'+$game.appid+'.acf')),$acf,[Text.Encoding]::UTF8)
 $customExe=Join-Path $gameRoot 'custom-install/Game.exe';[IO.Directory]::CreateDirectory((Split-Path $customExe))|Out-Null;[IO.File]::WriteAllText($customExe,'Simulation-only placeholder; never execute')
 $custom=Join-Path $gameRoot 'library'
 Json (Join-Path $custom 'state/library-scan.json') @{schemaVersion=1;games=@(@{primaryExecutable=$customExe;gameDirectory=(Split-Path $customExe);contentType='game';primaryRole='main-game';status='ready';scanComplete=$true})}
 Json (Join-Path $custom 'state/steam-add-plan.json') @{schemaVersion=1;dataRoot=$custom;libraryState=(Join-Path $custom 'state/library-scan.json');items=@(@{primaryExecutable=$customExe;formalName=$game.name;steamStoreAppId=$game.appid;storefrontAppId=$game.appid;baseGameAppId=$game.appid;identityStatus='verified';steamVerificationStatus='steam-verified';resolverVersion='steam-official-fuzzy-global-v4';contentRelation='canonical-base-game'})}
 Json (Join-Path $custom 'config/manual-overrides.json') @{schemaVersion=1;items=@{}}
 $zh=@($translations|Where-Object{[string]$_.steam_id -eq [string]$game.appid -and $_.PSObject.Properties['zh_CN'] -and $_.zh_CN}|Select-Object -First 1)
 $channels=@(@{key='steam-store-title';name=$game.name;exe=$exe},@{key='steam-generic-exe';name='Game';exe=$exe},@{key='custom-library';name='玩家自定义简称';exe=$customExe})
 if($zh.Count){$channels+=@{key='steam-chinese';name=$zh[0].zh_CN;exe=$exe}}
 $results=@{}
 foreach($channel in $channels){
  try{
   $identity=Resolve-TrainerIdentity $channel.name $channel.exe $db @{games=@{}} $custom
   if($identity.steamAppId -ne [string]$game.appid){throw 'Returned ID differs from official Steam sample'}
   $candidate=Find-FlingCandidate $catalog @($identity.aliases)
   $local=Find-LocalTrainer @((Join-Path $env:APPDATA 'GCM Trainers')) @($identity.aliases)
   $results[$channel.key]=@{input=$channel.name;appId=$identity.steamAppId;method=$identity.method;aliases=$identity.aliases;catalogName=if($candidate){$candidate.game_name}else{''};url=if($candidate){$candidate.official_url}else{''};localPath=if($local){$local.path}else{''};error=''}
  }catch{$results[$channel.key]=@{input=$channel.name;appId='';catalogName='';url='';localPath='';error=$_.Exception.Message}}
 }
 $states=@($results.Values|ForEach-Object{[string]$_.appId+'|'+[string]$_.url+'|'+[string]$_.localPath+'|'+[string]$_.error}|Select-Object -Unique)
 $main=$results['steam-store-title']
 # An official AppID/title disagreement with GCM is an expected refusal, not a
 # successful resolution. Keep it in the sample and prove every channel refuses.
 $conflicting=@($translations|Where-Object{(Get-TrainerNameKey ([string]$_.en_US)) -eq (Get-TrainerNameKey $game.name) -and [string]$_.steam_id -ne [string]$game.appid})
 $expectedBlocked=$conflicting.Count -gt 0 -and @($results.Values|Where-Object{$_.error -ne '游戏名称相同但 GCM 记录的 Steam ID 不同，未自动下载'}).Count -eq 0
 $rows.Add(@{name=$game.name;appid=$game.appid;rank=$game.rank;peakInGame=$game.peak_in_game;consistent=$states.Count -eq 1;identityResolved=-not [bool]$main.error;expectedBlocked=$expectedBlocked;catalogConflicts=@($conflicting|Select-Object en_US,steam_id);catalogMatched=[bool]$main.url;localReady=[bool]$main.localPath;channels=$results})
 Write-Host ($game.appid.ToString()+' | '+$game.name+' | '+$main.catalogName+' | '+$main.error)
}
$result=@{phase=$Phase;at=[DateTimeOffset]::Now.ToString('o');count=$rows.Count;consistent=@($rows|Where-Object{$_.consistent}).Count;identityResolved=@($rows|Where-Object{$_.identityResolved}).Count;expectedBlocked=@($rows|Where-Object{$_.expectedBlocked}).Count;catalogMatched=@($rows|Where-Object{$_.catalogMatched}).Count;localReady=@($rows|Where-Object{$_.localReady}).Count;networkRequests=0;trainerDownloads=0;trainerLaunches=0;rows=$rows.ToArray()}
Json (Join-Path $ValidationRoot ($Phase+'.json')) $result
[pscustomobject]$result|Select-Object phase,count,consistent,identityResolved,catalogMatched,localReady|ConvertTo-Json

if($Phase -ne 'baseline' -and ($result.consistent -ne $result.count -or $result.identityResolved+$result.expectedBlocked -ne $result.count)){throw 'Popular Steam simulation has unexpected unresolved identity or inconsistent channels'}
