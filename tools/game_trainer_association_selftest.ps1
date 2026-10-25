[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$Phase='baseline',[switch]$EdgesOnly,[string]$SampleRoot='G:\YeManCC-Work\Mainline\Build\Validation\TrainerSteamPopular-20261014')
$ErrorActionPreference='Stop'
Import-Module (Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
$module=Get-Module TrainerBackend
$runRoot=Join-Path $ValidationRoot $Phase
[IO.Directory]::CreateDirectory($runRoot)|Out-Null
function Json([string]$p,$v){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null;[IO.File]::WriteAllText($p,(ConvertTo-Json -InputObject $v -Depth 18),[Text.Encoding]::UTF8)}
function ReadJson([string]$p){[IO.File]::ReadAllText($p,[Text.Encoding]::UTF8)|ConvertFrom-Json}
function Get-TrainerProperty($value,[string]$name,$fallback=''){if($null -ne $value -and $null -ne $value.PSObject.Properties[$name]){return $value.$name};return $fallback}
function Assert($value,[string]$why='assertion failed'){if(-not $value){throw $why}}
function Reject([scriptblock]$body){$rejected=$false;try{& $body|Out-Null}catch{$rejected=$true};Assert $rejected 'Expected an explicit ambiguity rejection'}
function New-InertPe([string]$p){[IO.Directory]::CreateDirectory((Split-Path $p))|Out-Null;$b=New-Object byte[] 128;$b[0]=0x4d;$b[1]=0x5a;$b[60]=80;$b[80]=0x50;$b[81]=0x45;[IO.File]::WriteAllBytes($p,$b)}
function FullWidth([string]$text){return -join @($text.ToCharArray()|ForEach-Object{if([int]$_ -ge 33 -and [int]$_ -le 126){[char]([int]$_+65248)}else{$_}})}
$checks=[Collections.Generic.List[object]]::new()
function Check([string]$category,[string]$name,[scriptblock]$body){try{& $body;$checks.Add(@{category=$category;name=$name;ok=$true})}catch{$checks.Add(@{category=$category;name=$name;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Host ('FAIL '+$category+' | '+$name+' | '+$_.Exception.Message)}}
$sample=ReadJson (Join-Path $SampleRoot 'verified.json');$db=Join-Path $SampleRoot 'fixtures/gcm/db'
$rows=@(& $module {param($d,$l)Get-FlingCatalogRows $d $l} $db (Join-Path $SampleRoot 'fixtures/library'))
$gameResults=[Collections.Generic.List[object]]::new()
if(-not $EdgesOnly){
foreach($game in $sample.rows){
 $main=$game.channels.'steam-store-title';$aliases=@($main.aliases);$id=[string]$game.appid
 $exe=Join-Path $SampleRoot ('simulation/'+$id+'/steamapps/common/SampleGame/bin/Game.exe');$customExe=Join-Path $SampleRoot ('simulation/'+$id+'/custom-install/Game.exe');$custom=Join-Path $SampleRoot ('simulation/'+$id+'/library')
 if($game.PSObject.Properties['expectedBlocked'] -and $game.expectedBlocked){
  foreach($entry in @(@{name=$game.name;path=$exe;custom=$custom},@{name='Game';path=$exe;custom=$custom},@{name='玩家自定义简称';path=$customExe;custom=$custom})){
   Check 'official-id-conflict' ($id+' | '+$entry.name) {Reject {Resolve-TrainerIdentity $entry.name $entry.path $db @{games=@{}} $entry.custom}}
  }
  $gameResults.Add(@{appid=$id;name=$game.name;expectedBlocked=$true;conflicts=$game.catalogConflicts});continue
 }
 $expected=$main.url;$caseRoot=Join-Path $runRoot $id;$local=Join-Path $caseRoot 'library';$folder=Join-Path $local 'Player custom folder';$trainer=Join-Path $folder 'Generic.exe';$info=Join-Path $folder 'gcm_info.json'
 [IO.Directory]::CreateDirectory($folder)|Out-Null;$inert=New-Object byte[] 128;$inert[0]=0x4d;$inert[1]=0x5a;$inert[60]=80;$inert[80]=0x50;$inert[81]=0x45;[IO.File]::WriteAllBytes($trainer,$inert)
 $key=Get-TrainerNameKey $aliases[0];$variants=@('Game','游戏启动器',$game.name,'玩家自定义简称',('other game '+$id))
 $initial=$checks.Count
 foreach($alias in $aliases){
  $variants+=@($alias,$alias.ToUpperInvariant(),('  '+($alias -replace ' ','   ')+'  '),(FullWidth $alias),($alias+' Trainer'),($alias+'修改器'),($alias.Replace('&','&amp;')))
  Check 'reverse-title' ($id+' | '+$alias) {
   Json $info @{game_name=$alias;extension='Generic.exe';origin='fling_main'}
   $found=Find-LocalTrainer @($local) $aliases $db
   Assert ($found.path -eq $trainer)
  }
 }
 foreach($inputName in @($variants|Select-Object -Unique)){
  Check 'trusted-forward' ($id+' | '+$inputName) {
   $identity=Resolve-TrainerIdentity $inputName $exe $db @{games=@{}} $custom
   Assert ($identity.steamAppId -eq $id);$candidate=Find-FlingCandidate $rows @($identity.aliases)
   $url=if($candidate){$candidate.official_url}else{''};Assert ($url -eq $expected) 'Trusted EXE identity must override misleading or short display titles'
  }
 }
 Check 'custom-shortname' $id {$identity=Resolve-TrainerIdentity '任意简称' $customExe $db @{games=@{}} $custom;Assert ($identity.steamAppId -eq $id);Assert ((Get-TrainerNameKey $identity.aliases[0]) -eq $key)}
 $negative=@(($aliases[0]+' 999'),($aliases[0]+' Remastered Deluxe'),($aliases[0]+' DLC'),($aliases[0]+' Soundtrack'),($aliases[0]+' Demo'))
 foreach($name in $negative){
  Check 'reject-title-extension' ($id+' | '+$name) {Json $info @{game_name=$name;extension='Generic.exe';origin='fling_main'};Assert ($null -eq (Find-LocalTrainer @($local) $aliases $db)) 'Edition or content suffix must not collapse to the base game'}
 }
 Check 'no-shortname-inference' $id {$short='YMCC_SHORT_'+$id;$resolved=Resolve-TrainerIdentity $short '' $db @{games=@{}} (Join-Path $caseRoot 'empty-custom');Assert ($resolved.method -eq 'exact-title');Assert ($null -eq (Find-FlingCandidate $rows @($resolved.aliases)))}
 $gameResults.Add(@{appid=$id;name=$game.name;checks=$checks.Count-$initial;expectedCatalog=$expected})
 Write-Host ($id+' | '+($checks.Count-$initial)+' word and identity checks')
}

}
Check 'real-collision' 'Dragon Quest I & II versus III have distinct names' {Assert ((Get-TrainerNameKey 'Dragon Quest I & II HD-2D Remake') -ne (Get-TrainerNameKey 'DRAGON QUEST III HD-2D Remake'))}
Check 'real-collision' 'I and II aliases do not include III' {$a=@(Resolve-TrainerAliases 'Dragon Quest I & II HD-2D Remake' $db);Assert ('DRAGON QUEST III HD-2D Remake' -notin $a)}
Check 'real-collision' 'III aliases do not include I and II' {$a=@(Resolve-TrainerAliases 'DRAGON QUEST III HD-2D Remake' $db);Assert ('Dragon Quest I & II HD-2D Remake' -notin $a)}
$pairTests=@(@('Grand Theft Auto V','Grand Theft Auto V Enhanced'),@('Monster Hunter: World','Monster Hunter Wilds'),@('The Witcher 3: Wild Hunt','The Witcher 3: Wild Hunt — Remastered'),@('Victoria 2','Victoria 3'),@('Age of Empires II','Age of Empires III'),@('Total War: WARHAMMER II','Total War: WARHAMMER III'),@('Red Dead Redemption','Red Dead Redemption 2'),@('Resident Evil 4','Resident Evil 4 Remake'))
foreach($pair in $pairTests){Check 'near-title' ($pair -join ' / ') {Assert ((Get-TrainerNameKey $pair[0]) -ne (Get-TrainerNameKey $pair[1]));$candidate=@(@{game_name=$pair[1];official_url='https://flingtrainer.com/trainer/association-fixture/'});Assert ($null -eq (Find-FlingCandidate $candidate @($pair[0])))}}
$conflictDb=Join-Path $runRoot 'conflicts/db';$local=Join-Path $runRoot 'conflicts/library';$folder=Join-Path $local 'A';$trainer=Join-Path $folder 'Trainer.exe';$info=Join-Path $folder 'gcm_info.json'
[IO.Directory]::CreateDirectory($folder)|Out-Null;New-InertPe $trainer
Json (Join-Path $conflictDb 'translations.json') @(@{en_US='Alpha II';zh_CN='共同别名';steam_id=12345},@{en_US='Alpha III';zh_CN='共同别名';steam_id=23456})
Check 'alias-conflict' 'shared Chinese alias never chooses either game' {Reject {Resolve-TrainerAliases '共同别名' $conflictDb}}
Check 'alias-conflict' 'confirmed ID drops a shared cross-ID alias' {$a=@(& $module {param($d)Get-TrainerAppIdAliases '12345' 'Alpha II' $d} $conflictDb);Assert ('共同别名' -notin $a);Assert ('Alpha II' -in $a)}
Check 'alias-conflict' 'English title cannot absorb an ambiguous Chinese alias' {$a=@(Resolve-TrainerAliases 'Alpha II' $conflictDb);Assert ('共同别名' -notin $a)}
Check 'alias-conflict' 'same English name under conflicting IDs is rejected' {Json (Join-Path $conflictDb 'translations.json') @(@{en_US='Alpha II';zh_CN='甲二';steam_id=12345},@{en_US='Alpha II';zh_CN='乙二';steam_id=23456});Reject {Resolve-TrainerAliases 'Alpha II' $conflictDb}}
Json (Join-Path $conflictDb 'fling_main.json') @(@{game_name='Alpha II';gcm_url='GCM/alpha-ii'},@{game_name='Alpha III';gcm_url='GCM/alpha-iii'})
Check 'resource-id' 'known resource resolves only to its owning game' {Json $info @{game_name='Alpha III';gcm_url='GCM/alpha-ii';origin='fling_main'};Assert ((Find-LocalTrainer @($local) @('Alpha II') $conflictDb).path -eq $trainer);Assert ($null -eq (Find-LocalTrainer @($local) @('Alpha III') $conflictDb))}
Check 'resource-id' 'unknown resource cannot lend a matching display title' {Json $info @{game_name='Alpha II';gcm_url='GCM/unknown';origin='fling_main'};Assert ($null -eq (Find-LocalTrainer @($local) @('Alpha II') $conflictDb))}
Check 'local-ambiguity' 'equal-priority trainers cannot choose by directory enumeration' {
 Json $info @{game_name='Alpha II';gcm_url='GCM/alpha-ii';origin='fling_main'};$second=Join-Path $local 'B';[IO.Directory]::CreateDirectory($second)|Out-Null;New-InertPe (Join-Path $second 'Trainer.exe');Json (Join-Path $second 'gcm_info.json') @{game_name='Alpha II';gcm_url='GCM/alpha-ii';origin='fling_main'}
 Reject {Find-LocalTrainer @($local) @('Alpha II') $conflictDb}
}

if($EdgesOnly){
 # Positive vocabulary uses real catalog aliases. No new synonym is fabricated.
 $wordDb=Join-Path $runRoot 'words/db'
 $translationRows=@(& $module {param($d)Get-GcmCatalogRows $d 'translations.json'} $db)
 $sampleIds=@($sample.rows|ForEach-Object{[string]$_.appid})
 Json (Join-Path $wordDb 'translations.json') @($translationRows|Where-Object{[string]$_.steam_id -in $sampleIds})
 foreach($game in $sample.rows){
  if($game.PSObject.Properties['expectedBlocked'] -and $game.expectedBlocked){continue}
  $aliases=@($game.channels.'steam-store-title'.aliases)
  Check 'verified-roundtrip' ([string]$game.appid) {
   $gameExe=Join-Path $SampleRoot ('simulation/'+$game.appid+'/steamapps/common/SampleGame/bin/Game.exe')
   $identity=Resolve-TrainerIdentity '联想简称' $gameExe $wordDb @{games=@{}} (Join-Path $runRoot 'empty')
   foreach($safeAlias in $identity.aliases){
    $reverse=@(Resolve-TrainerAliases $safeAlias $wordDb)
    $candidate=Find-FlingCandidate $rows $reverse
    $url=if($candidate){$candidate.official_url}else{''}
    Assert ($url -eq $game.channels.'steam-store-title'.url) 'Forward then reverse association changed the trainer resource'
   }
  }
  foreach($alias in $aliases){
   foreach($spelling in @($alias.ToUpperInvariant(),(FullWidth $alias),($alias+' Trainer'),($alias+'修改器'),($alias.Replace('&','&amp;')))|Select-Object -Unique){
    Check 'unbound-exact-vocabulary' ([string]$game.appid+' | '+$spelling) {
     Assert ((Get-TrainerNameKey $spelling) -eq (Get-TrainerNameKey $alias))
     # This saved Chinese spelling belongs to two distinct English rows under
     # RDR2's ID. Without an EXE/formal-title context, do not pick a row by order.
     if([string]$game.appid -eq '1174180' -and (Get-TrainerNameKey $spelling) -eq (Get-TrainerNameKey '荒野大镖客2')){
      Reject {Resolve-TrainerAliases $spelling $wordDb};return
     }
     $expanded=@(Resolve-TrainerAliases $spelling $wordDb)
     $expected=@($aliases|ForEach-Object{Get-TrainerNameKey $_})
     Assert (@($expanded|Where-Object{(Get-TrainerNameKey $_) -in $expected}).Count -gt 0)
     $known=@($translationRows|Where-Object{[string]$_.steam_id -eq [string]$game.appid}|ForEach-Object{foreach($language in @('en_US','zh_CN','zh_TW')){Get-TrainerNameKey ([string](Get-TrainerProperty $_ $language ''))}})
     foreach($name in $expanded){Assert (((Get-TrainerNameKey $name) -in $known) -or ((Get-TrainerNameKey $name) -eq (Get-TrainerNameKey $alias))) 'Vocabulary expansion escaped the same game ID'}
    }
   }
  }
 }
 $shortnames=@(@{id='1091500';words=@('2077','CP2077','赛博2077')},@{id='271590';words=@('GTA5','GTA V')},@{id='1174180';words=@('RDR2','大表哥2')},@{id='1245620';words=@('老头环','法环')},@{id='1086940';words=@('BG3','博德3')},@{id='394360';words=@('HOI4','钢四')},@{id='1158310';words=@('CK3','王国风云3')},@{id='582010';words=@('MHW','怪猎世界')})
 foreach($entry in $shortnames){foreach($word in $entry.words){
  Check 'contextual-shortname' ($entry.id+' | '+$word) {
   $exe=Join-Path $SampleRoot ('simulation/'+$entry.id+'/steamapps/common/SampleGame/bin/Game.exe')
   $known=Resolve-TrainerIdentity $word $exe $wordDb @{games=@{}} (Join-Path $runRoot 'empty')
   Assert ($known.steamAppId -eq $entry.id)
   $unbound=Resolve-TrainerIdentity $word '' $wordDb @{games=@{}} (Join-Path $runRoot 'empty')
   Assert ($unbound.method -eq 'exact-title' -and -not $unbound.steamAppId) 'A nickname alone cannot manufacture a verified AppID'
  }
 }}
 # Ambiguous display words must not block an independently confirmed executable.
 Json (Join-Path $conflictDb 'translations.json') @(@{en_US='Alpha II';zh_CN='共同别名';steam_id=12345},@{en_US='Alpha III';zh_CN='共同别名';steam_id=23456})
 $steamapps=Join-Path $runRoot 'trusted/steamapps';$exe=Join-Path $steamapps 'common/Alpha/Game.exe';[IO.Directory]::CreateDirectory((Split-Path $exe))|Out-Null;[IO.File]::WriteAllText($exe,'Non-executable game fixture')
 [IO.File]::WriteAllText((Join-Path $steamapps 'appmanifest_12345.acf'),("`"AppState`"`n{`n`"appid`" `"12345`"`n`"name`" `"Alpha II`"`n`"installdir`" `"Alpha`"`n}"))
 Check 'identity-first' 'trusted EXE wins over ambiguous display alias' {$r=Resolve-TrainerIdentity '共同别名' $exe $conflictDb @{games=@{}} (Join-Path $runRoot 'empty');Assert ($r.steamAppId -eq '12345');Assert ('共同别名' -notin $r.aliases)}
 Check 'same-id-edition' 'shared edition alias cannot reintroduce another edition' {
  Json (Join-Path $conflictDb 'translations.json') @(@{en_US='Alpha II';zh_CN='共享版本名';steam_id=12345},@{en_US='Alpha II Remastered';zh_CN='共享版本名';steam_id=12345})
  $a=@(& $module {param($d)Get-TrainerAppIdAliases '12345' 'Alpha II' $d} $conflictDb)
  Assert ('共享版本名' -notin $a);Assert ('Alpha II' -in $a);Assert ('Alpha II Remastered' -notin $a)
 }
 # Failures must stop before any download budget reservation, even in forced mode.
 & $module { $script:associationHttpCalls=0;function script:Get-TrainerResponse { $script:associationHttpCalls++;throw 'Offline test forbids network' } }
 $settings=Join-Path $runRoot 'conflicts'
 foreach($mode in @('ambiguous-title','ambiguous-local')){
  Check 'worker-boundary' $mode {
   if($mode -eq 'ambiguous-title'){$name='共同别名';$target='';Json (Join-Path $conflictDb 'translations.json') @(@{en_US='Alpha II';zh_CN='共同别名';steam_id=12345},@{en_US='Alpha III';zh_CN='共同别名';steam_id=23456})}else{$name='Game';$target=$exe}
   $cache=Join-Path $runRoot ('worker-'+$mode);$requestId='association-'+[guid]::NewGuid().ToString('N');$output=Join-Path $cache ('requests/'+$requestId+'.json')
   Invoke-TrainerPrepare -GameName $name -GameExecutablePath $target -CacheRoot $cache -ResultPath $output -RequestId $requestId -GcmSettingsRoot $settings -GcmDefaultRoot $local -CustomLibraryRoot (Join-Path $runRoot 'empty') -Force
   $receipt=ReadJson $output;Assert ($receipt.state -eq 'failed');Assert ((& $module {$script:associationHttpCalls}) -eq 0)
   $recordPath=Join-Path $local 'ymcc-library.json';if([IO.File]::Exists($recordPath)){Assert (@((ReadJson $recordPath).downloads).Count -eq 0)}
  }
 }
}
$keys=@('Dragon Quest I & II HD-2D Remake','Dragon Quest I and II HD-2D Remake','DRAGON QUEST III HD-2D Remake','Mount & Blade II: Bannerlord','Mount &amp; Blade II: Bannerlord','Cyberpunk 2077','赛博朋克 2077','赛博朋克２０７７','Victoria 2','Victoria 3','Total War: WARHAMMER II','Total War: WARHAMMER III',"No Man’s Sky",'No Man''s Sky')|ForEach-Object{@{input=$_;key=Get-TrainerNameKey $_}}
$failures=@($checks|Where-Object{-not $_.ok})
$result=@{phase=$Phase;samples=@($sample.rows).Count;total=$checks.Count;passed=$checks.Count-$failures.Count;failed=$failures.Count;checks=$checks.ToArray();games=$gameResults.ToArray();nameKeys=$keys;networkRequests=0;trainerDownloads=0;trainerLaunches=0}
Json (Join-Path $ValidationRoot ($Phase+'.json')) $result
[pscustomobject]@{phase=$Phase;total=$result.total;passed=$result.passed;failed=$result.failed}|ConvertTo-Json
if($Phase -notlike 'baseline*' -and $result.failed){throw 'Bidirectional association tests failed'}
