[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot)
$ErrorActionPreference='Stop'
$ValidationRoot=[IO.Path]::GetFullPath($ValidationRoot)
if(-not $ValidationRoot.StartsWith('G:\YeManCC-Work\Mainline\Build\Validation\TrainerFiveGameLifecycle-', [StringComparison]::OrdinalIgnoreCase)){throw 'Validation root outside approved workspace'}
$games=[IO.File]::ReadAllText((Join-Path $ValidationRoot 'five-game-data.json'),[Text.Encoding]::UTF8)|ConvertFrom-Json
if(@($games).Count-ne5){throw 'Exactly five records required'}
$run=Join-Path $ValidationRoot ('runtime-'+[guid]::NewGuid().ToString('N').Substring(0,8));$evidence=Join-Path $ValidationRoot ('lifecycle-'+[guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Directory]::CreateDirectory($run)|Out-Null;[IO.Directory]::CreateDirectory($evidence)|Out-Null
$owned=[Collections.Generic.List[object]]::new();$checks=[Collections.Generic.List[object]]::new();$results=[Collections.Generic.List[object]]::new()
function Json([string]$p,$v){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null;[IO.File]::WriteAllText($p,(ConvertTo-Json -InputObject $v -Depth 18 -Compress),[Text.UTF8Encoding]::new($false))}
function Read([string]$p){[IO.File]::ReadAllText($p,[Text.Encoding]::UTF8)|ConvertFrom-Json}
function Assert($v,[string]$m){if(-not$v){throw $m}}
function Check([string]$name,[scriptblock]$body){try{&$body;$checks.Add(@{name=$name;ok=$true});Write-Output ('PASS '+$name)}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Output ('FAIL '+$name+': '+$_.Exception.Message)}}
function WaitFile([string]$p){$until=[DateTime]::UtcNow.AddSeconds(8);while(-not[IO.File]::Exists($p)-and[DateTime]::UtcNow-lt$until){Start-Sleep -Milliseconds 40};Assert ([IO.File]::Exists($p)) ('Missing lifecycle marker '+$p)}
function Own([int]$id,[string]$path,[string]$events){$p=Get-Process -Id $id;$owned.Add(@{pid=$id;birth=$p.StartTime.ToUniversalTime().ToFileTimeUtc();path=$path;events=$events});WaitFile (Join-Path $events ('opened-'+$id+'.json'));$p.Refresh();Assert ($p.Path-ieq$path) ('Unexpected launched process path: '+$p.Path+' expected '+$path)}
function CloseOwned([int]$id){$entry=@($owned|Where-Object {$_.pid-eq$id}|Select-Object -Last 1)[0];Assert ([bool]$entry) 'Unowned close request';$p=Get-Process -Id $id -ErrorAction SilentlyContinue;if(-not$p){return};Assert ($p.Path-ieq$entry.path-and$p.StartTime.ToUniversalTime().ToFileTimeUtc()-eq$entry.birth) 'Process identity changed before close';[IO.File]::WriteAllText((Join-Path $entry.events 'close-request'),'close');Assert ($p.WaitForExit(8000)) 'Owned process did not close';WaitFile (Join-Path $entry.events ('closed-'+$id+'.json'))}
function ResetClose([string]$events){$p=Join-Path $events 'close-request';if([IO.File]::Exists($p)){[IO.File]::Delete($p)}}
function RemoveFixtureFolder([string]$path){$full=[IO.Path]::GetFullPath($path);Assert ($full.StartsWith($run+'\',[StringComparison]::OrdinalIgnoreCase)) 'Removal escaped isolated runtime';$items=@(Get-ChildItem -LiteralPath $full -Recurse -Force);foreach($item in $items){Assert (-not($item.Attributes-band[IO.FileAttributes]::ReparsePoint)) 'Removal rejected a reparse point'};foreach($item in @($items|Where-Object {-not$_.PSIsContainer})){Remove-Item -LiteralPath $item.FullName -Force};foreach($item in @($items|Where-Object {$_.PSIsContainer}|Sort-Object FullName -Descending)){Remove-Item -LiteralPath $item.FullName -Force};Remove-Item -LiteralPath $full -Force}
$fixture=Join-Path $run 'LifecycleFixture.exe'
& (Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe') /nologo /optimize+ /target:winexe /platform:x64 /r:System.Web.Extensions.dll /r:System.Windows.Forms.dll ('/out:'+$fixture) (Join-Path $PSScriptRoot 'trainer_lifecycle_fixture.cs');if($LASTEXITCODE){throw 'Fixture compile failed'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
Import-Module (Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking;$module=Get-Module TrainerBackend
&$module {
 $script:sim=@{calls=0;payloads=0;mode='ok';html='';package='';payloadUrl='';pageUrl='';trace=@()}
 function script:Get-TrainerResponse($Context,[string]$Url,[string]$Destination='',[string]$Referer=''){
  $script:sim.calls++;$script:sim.trace+=@{url=$Url;payload=[bool]$Destination};Assert-TrainerActive $Context;Assert-TrainerRequestUri $Context $Url|Out-Null
  if($script:sim.mode-eq'offline'){throw 'Isolated simulated HTTP 503'}
  if(-not$Destination){if($Url-ne$script:sim.pageUrl){throw ('Unexpected simulated page URL '+$Url)};return $script:sim.html}
  if($Url-ne$script:sim.payloadUrl){throw ('Unexpected simulated payload URL '+$Url)}
  $script:sim.payloads++
  if($script:sim.mode-eq'oversize'){return @{bytes=([long]$Context.MaxDownloadBytes+1);url=$Url}}
  if($script:sim.mode-eq'cancel'){[IO.File]::WriteAllText($Context.CancelPath,'cancel')}
  if($script:sim.mode-eq'html'){[IO.File]::WriteAllText($Destination,'<html>blocked download</html>')}else{[IO.File]::Copy($script:sim.package,$Destination,$true)}
  $bytes=(Get-Item -LiteralPath $Destination).Length;$Context.DownloadBytes=$bytes
  if($script:sim.mode-eq'switch'){$t=Read-TrainerJson $script:TrainerGameTargetPath;$t.pid+=100000;Write-TrainerJson $script:TrainerGameTargetPath $t}
  return @{bytes=$bytes;url=$Url}
 }
}
function NewFixture([string]$path,[string]$title,[string]$events){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))|Out-Null;[IO.File]::Copy($fixture,$path,$true);Json ($path+'.fixture.json') @{title=$title;eventRoot=$events};return $path}
function Gate {Json $script:targetFile @{valid=$true;generation=1;lastSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();pid=$gp.Id;processCreated=[string]$gp.StartTime.ToUniversalTime().ToFileTimeUtc();path=$gameExe;title=$game.name;name=$game.name}}
function Prepare([string]$cache,[string]$settings,[string]$library,[switch]$Force){Gate;$id='prepare-'+[guid]::NewGuid().ToString('N');$out=Join-Path $cache ('requests/'+$id+'.json');Invoke-TrainerPrepare -GameName $game.name -GameExecutablePath $gameExe -GameProcessId $gp.Id -GameProcessCreated ([string]$gp.StartTime.ToUniversalTime().ToFileTimeUtc()) -CacheRoot $cache -ResultPath $out -RequestId $id -GcmSettingsRoot $settings -GcmDefaultRoot $library -CustomLibraryRoot (Join-Path $run 'empty-custom-library') -Force:$Force;return Read $out}
function Launch([string]$mode,$receipt,[string]$settings,[string]$cache,[switch]$WithGame,[string]$Birth=''){$id='launch-'+[guid]::NewGuid().ToString('N');$out=Join-Path $cache ('requests/'+$id+'.json');$args=@{Mode=$mode;CacheRoot=$cache;ResultPath=$out;RequestId=$id;GcmSettingsRoot=$settings;AppDirectory=$script:appDir};if($receipt){$args.TrainerPath=$receipt.trainerPath;$args.TrainerFolder=$receipt.trainerFolder;$args.ExpectedSha256=$receipt.sha256;$args.TrainerGameName=$receipt.trainerGameName;$args.TrainerOrigin=$receipt.origin;$args.GcmResourceId=$receipt.gcmResourceId};if($WithGame){$args.GameExecutablePath=$gameExe;$args.GameProcessId=$gp.Id;$args.GameProcessCreated=if($Birth){$Birth}else{[string]$gp.StartTime.ToUniversalTime().ToFileTimeUtc()};$args.ExpectedGameSha256=(Get-FileHash -LiteralPath $gameExe).Hash;Gate};Invoke-TrainerLaunch @args;return Read $out}
try {
 foreach($game in $games){
  $case=Join-Path $run ([string]$game.appid);$caseEvidence=Join-Path $evidence ([string]$game.appid);[IO.Directory]::CreateDirectory($caseEvidence)|Out-Null
  $settings=Join-Path $case 'GCM Settings';$library=Join-Path $case 'Game Cheats Manager/统一修改器库 [模拟]';$cache=Join-Path $case 'cache';$db=Join-Path $settings 'db';$script:targetFile=Join-Path $case 'PowerControl/game-target.json';$script:appDir=Join-Path $case 'suite/YMCC'
  $steamapps=Join-Path $case 'steamapps';$gameExe=Join-Path $steamapps ('common/'+$game.directory+'/bin/'+$game.exe);$gameEvents=Join-Path $caseEvidence 'events/game';NewFixture $gameExe $game.name $gameEvents|Out-Null
  $manifest=Join-Path $steamapps ('appmanifest_'+$game.appid+'.acf');[IO.File]::WriteAllText($manifest,('"AppState"'+"`n{`n"+'"appid" "'+$game.appid+'"'+"`n"+'"name" "'+$game.name+'"'+"`n"+'"installdir" "'+$game.directory+'"'+"`n}"),[Text.UTF8Encoding]::new($false))
  $managerEvents=Join-Path $caseEvidence 'events/manager';$ceEvents=Join-Path $caseEvidence 'events/ce-dispatch';$trainerEvents=Join-Path $caseEvidence 'events/trainer'
  $manager=NewFixture (Join-Path $case 'suite/Game Cheats Manager/Game Cheats Manager.exe') 'YMCC inert GCM manager' $managerEvents
  $ceDispatcher=NewFixture (Join-Path $case 'CE/cheatengine-x86_64.exe') 'YMCC inert CE dispatch verifier' $ceEvents
  $config=@{downloadPath=$library;managerPath=$manager;cePath=(Split-Path $ceDispatcher);safePath=$false;launchAsAdmin=$false;launchCTAsAdmin=$false;enableGCM=$true};Json (Join-Path $settings 'settings.json') $config
  $translations=@($game.originalTranslation,@{en_US=($game.name+' II');zh_CN='不同续作';steam_id=90000001},@{en_US=($game.name+' DLC');zh_CN='不同DLC';steam_id=90000002})
  Json (Join-Path $db 'translations.json') $translations;Json (Join-Path $db 'fling_main.json') @($game.fling)
  $ctCandidate=@{game_name=$game.name;origin='ct_other';author='YMCCFixture';gcm_url=('GCM/GCM Trainers/'+$game.name+'.ct');official_url=('https://fearlessrevolution.com/viewtopic.php?t='+$game.appid);version='isolated-test'};Json (Join-Path $db 'gcm_trainers.json') @($ctCandidate)
  $packageFolder=Join-Path $case 'package';NewFixture (Join-Path $packageFolder 'nested/Trainer Sim.exe') ('YMCC inert trainer '+$game.appid) $trainerEvents|Out-Null
  $package=Join-Path $case 'trainer-simulation.zip';[IO.Compression.ZipFile]::CreateFromDirectory($packageFolder,$package)
  $payloadUrl='https://flingtrainer.com/downloads/ymcc-isolated-'+$game.appid
  $html='<h1>'+[Net.WebUtility]::HtmlEncode($game.fling.game_name)+' Trainer</h1><tr><td><a href="'+$payloadUrl+'" class="attachment-link">'+$game.fling.game_name.Replace(' ','.')+'.Trainer-FLiNG.zip</a></td><td class="attachment-date">2026-10-17 00:00</td></tr>'
  &$module {param($t,$pkg,$html,$url,$page)$script:TrainerGameTargetPath=$t;$script:sim.mode='ok';$script:sim.package=$pkg;$script:sim.html=$html;$script:sim.payloadUrl=$url;$script:sim.pageUrl=$page;$script:sim.calls=0;$script:sim.payloads=0;$script:sim.trace=@()} $targetFile $package $html $payloadUrl $game.fling.official_url
  $gp=Start-Process -FilePath $gameExe -WindowStyle Hidden -PassThru;Own $gp.Id $gameExe $gameEvents;Gate
  $row=@{appid=$game.appid;name=$game.name;steamChartRank=$game.steamChart.rank;gameExe=$gameExe;receipts=@{};realGameMemoryAccess=$false}
  Check ($game.name+' / Steam and GCM exact-ID aliases') {$identity=Resolve-TrainerIdentity $game.name $gameExe $db @{games=@{}} (Join-Path $run 'empty') $gp.Id ([string]$gp.StartTime.ToUniversalTime().ToFileTimeUtc());Assert ($identity.steamAppId-eq[string]$game.appid-and$identity.method-eq'steam-appid') 'Steam/GCM identity mismatch';foreach($alias in @($game.name,$game.name.ToUpperInvariant(),[string]$game.originalTranslation.zh_CN)){if($alias){$other=Resolve-TrainerIdentity $alias $gameExe $db @{games=@{}} (Join-Path $run 'empty') $gp.Id ([string]$gp.StartTime.ToUniversalTime().ToFileTimeUtc());Assert ($other.steamAppId-eq$identity.steamAppId) 'Alias changed identity'}};$row.identity=$identity}
  $ready=Prepare $cache $settings $library
  Check ($game.name+' / automatic download installs in GCM, FLiNG wins over CT') {Assert ($ready.state-eq'ready'-and$ready.downloaded-eq$true) ('Not downloaded-ready: '+$ready.message);Assert ($ready.origin-eq'fling_main'-and$ready.gcmResourceId-ceq$game.fling.gcm_url) 'Wrong source/resource';Assert ([IO.File]::Exists($ready.trainerPath)-and$ready.trainerPath.StartsWith($library+'\')) 'Not in GCM root';Assert ((Get-FileHash -LiteralPath $ready.trainerPath).Hash-ieq$ready.sha256) 'Installed hash differs';$row.receipts.download=$ready}
  if($ready.state-ne'ready'){throw ('Cannot continue '+$game.name+': '+$ready.message)}
  Check ($game.name+' / reverse resource-to-ledger-to-live-game binding') {$ledger=Read (Join-Path $library 'ymcc-library.json');$bindings=@($ledger.games.PSObject.Properties|ForEach-Object{$_.Value});$resources=@($ledger.trainers.PSObject.Properties|ForEach-Object{$_.Value});Assert (@($bindings|Where-Object {$_.executablePath-ieq$gameExe-and$_.identity.processId-eq$gp.Id}).Count-eq1) 'Exact EXE/process binding absent';Assert (@($resources|Where-Object {$_.gcmResourceId-ceq$game.fling.gcm_url-and$_.steamAppId-eq[string]$game.appid}).Count-eq1) 'GCM ID absent';Assert (@($ledger.downloads).Count-eq1-and$ledger.downloads[0].chargedBytes-eq(Get-Item -LiteralPath $package).Length) 'Budget byte charge wrong';Json (Join-Path $caseEvidence 'ledger-after-download.json') $ledger}
  $beforeCalls=&$module {$script:sim.calls};$local=Prepare $cache $settings $library
  Check ($game.name+' / downloaded=false local reuse is ready without extra charge') {Assert ($local.state-eq'ready'-and$local.downloaded-eq$false) 'Local reuse not ready';Assert ((&$module {$script:sim.calls})-eq$beforeCalls) 'Local reuse used network';Assert (@((Read (Join-Path $library 'ymcc-library.json')).downloads).Count-eq1) 'Local reuse charged quota';$row.receipts.local=$local}
  Check ($game.name+' / real OS launch, duplicate click reuses one trainer') {$launch=Launch Trainer $ready $settings $cache -WithGame;Assert $launch.ok $launch.error;Own $launch.pid $ready.trainerPath $trainerEvents;$again=Launch Trainer $ready $settings $cache -WithGame;$row.receipts.duplicate=$again;if(-not$again.ok-and$again.error-match'系统未允许切换窗口'){Assert (@(Get-Process -Name 'Trainer Sim' -ErrorAction SilentlyContinue|Where-Object {$_.Path-ieq$ready.trainerPath}).Count-eq1) 'Duplicate process created on foreground denial'}else{Assert ($again.ok-and$again.reused-and$again.pid-eq$launch.pid) ('Duplicate trainer reuse failed: '+$again.error)};$started=Read (Join-Path $trainerEvents ('opened-'+$launch.pid+'.json'));Assert ($started.cwd-ieq(Split-Path $ready.trainerPath)) 'Wrong working directory';$row.receipts.launch=$launch;CloseOwned $launch.pid}
  Check ($game.name+' / manager real open, reuse, close') {$launch=Launch Manager $null $settings $cache;Assert $launch.ok $launch.error;Own $launch.pid $manager $managerEvents;$again=Launch Manager $null $settings $cache;if(-not$again.ok-and$again.error-match'系统未允许切换窗口'){Assert (@(Get-Process -Name 'Game Cheats Manager' -ErrorAction SilentlyContinue|Where-Object {$_.Path-ieq$manager}).Count-eq1) 'Duplicate manager created'}else{Assert ($again.reused-and$again.pid-eq$launch.pid) ('Manager reuse failed: '+$again.error)};$row.receipts.manager=$launch;CloseOwned $launch.pid}
  Check ($game.name+' / standalone CE dispatch opens configured executable then closes') {$launch=Launch CheatEngine $null $settings $cache;Assert $launch.ok $launch.error;Own $launch.pid $ceDispatcher $ceEvents;$row.receipts.ceDispatch=$launch;CloseOwned $launch.pid}
  Check ($game.name+' / wrong process birth is refused') {$denied=Launch Trainer $ready $settings $cache -WithGame -Birth '1';Assert (-not$denied.ok) 'Wrong process birth launched trainer';$row.receipts.stale=$denied}
  Check ($game.name+' / GCM root migration repairs cached absolute trainer path') {$moved=Join-Path $case '外置GCM/玩家改名 [目录]';[IO.Directory]::CreateDirectory((Split-Path $moved))|Out-Null;Assert ([IO.Path]::GetFullPath($ready.trainerFolder).StartsWith($run+'\')) 'Move source escaped runtime';Move-Item -LiteralPath $ready.trainerFolder -Destination $moved;$config.downloadPath=Split-Path $moved;Json (Join-Path $settings 'settings.json') $config;ResetClose $trainerEvents;$launch=Launch Trainer $ready $settings $cache -WithGame;Assert $launch.ok $launch.error;Assert ($launch.trainerPath-ne$ready.trainerPath-and$launch.trainerFolder-eq$moved) 'Dynamic GCM path not repaired';Own $launch.pid $launch.trainerPath $trainerEvents;$row.receipts.moved=$launch;CloseOwned $launch.pid;$row.movedPath=$launch.trainerPath;$row.movedFolder=$moved}
  Check ($game.name+' / deleting trainer files blocks stale readiness') {Assert ([bool]$row.movedFolder) 'Missing moved fixture';RemoveFixtureFolder $row.movedFolder;$denied=Launch Trainer $ready $settings $cache -WithGame;Assert (-not$denied.ok-and$denied.code-eq'trainer-unavailable') 'Deleted trainer still launches';$row.receipts.deleted=$denied;Assert (-not[IO.File]::Exists($row.movedPath)) 'Trainer file still present'}
  $config.downloadPath=$library;Json (Join-Path $settings 'settings.json') $config
  foreach($fault in @('offline','html','cancel','oversize','switch')){
   Check ($game.name+' / '+$fault+' refuses readiness and cleans staging') {
    $faultRoot=Join-Path $case ('fault-'+$fault);$faultSettings=Join-Path $faultRoot 'settings';[IO.Directory]::CreateDirectory($faultSettings)|Out-Null;Copy-Item -LiteralPath $db -Destination (Join-Path $faultSettings 'db') -Recurse;$faultConfig=@{}+$config;$faultConfig.downloadPath=Join-Path $faultRoot 'library';Json (Join-Path $faultSettings 'settings.json') $faultConfig
    &$module {param($mode)$script:sim.mode=$mode} $fault;$r=Prepare (Join-Path $faultRoot 'cache') $faultSettings $faultConfig.downloadPath -Force
    Assert ($r.state-ne'ready'-and-not$r.ok) ('Fault became ready: '+$fault);if($fault-eq'cancel'){Assert ($r.state-eq'cancelled') 'Cancel not reported'}
    Assert (@(Get-ChildItem -LiteralPath (Join-Path $faultRoot 'cache/staging') -Force -ErrorAction SilentlyContinue).Count-eq0) 'Partial download staging leaked';Json (Join-Path $caseEvidence ('fault-'+$fault+'.json')) $r
    &$module {$script:sim.mode='ok'};Gate
   }
  }
  Check ($game.name+' / exhausted budget prevents network even with Force') {$root=Join-Path $case 'fault-budget';$s=Join-Path $root 'settings';[IO.Directory]::CreateDirectory($s)|Out-Null;Copy-Item -LiteralPath $db -Destination (Join-Path $s 'db') -Recurse;$lib=Join-Path $root 'library';$cfg=@{}+$config;$cfg.downloadPath=$lib;Json (Join-Path $s 'settings.json') $cfg;Json (Join-Path $lib 'ymcc-library.json') @{schema=1;budget=@{windowHours=24;maxDownloads=1;maxBytes=1000};downloads=@(@{at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();chargedBytes=1000;bytes=1000;requestId='prior-budget';state='ready'});games=@{};trainers=@{}};$prior=&$module {$script:sim.calls};$limited=Prepare (Join-Path $root 'cache') $s $lib -Force;Assert ($limited.state-eq'budget-limited') 'Budget bypassed';Assert ((&$module {$script:sim.calls})-eq$prior) 'Budget limit contacted network';Json (Join-Path $caseEvidence 'budget-limited.json') $limited}
  Check ($game.name+' / no manifest maps the shared top game to exact GCM ID') {$off=Join-Path $case 'manifest-disabled.acf';Move-Item -LiteralPath $manifest -Destination $off;try{Gate;$identity=Resolve-TrainerIdentity $game.name $gameExe $db @{games=@{}} (Join-Path $run 'empty') $gp.Id ([string]$gp.StartTime.ToUniversalTime().ToFileTimeUtc());Assert ($identity.method-eq'ymcc-game-target'-and$identity.steamAppId-eq[string]$game.appid) 'No-manifest identity failed';$row.noManifestIdentity=$identity}finally{Move-Item -LiteralPath $off -Destination $manifest}}
  Check ($game.name+' / same-name different Steam ID blocks network') {$s=Join-Path $case 'wrong-id-settings';$conflict=@{};foreach($p in $game.originalTranslation.PSObject.Properties){$conflict[$p.Name]=$p.Value};$conflict.steam_id=90000999;Json (Join-Path $s 'db/translations.json') @($conflict);$cfg=@{}+$config;$cfg.downloadPath=Join-Path $case 'wrong-id-library';Json (Join-Path $s 'settings.json') $cfg;$prior=&$module {$script:sim.calls};$denied=Prepare (Join-Path $case 'wrong-id-cache') $s $cfg.downloadPath -Force;Assert (-not$denied.ok-and$denied.state-eq'failed') 'Wrong ID accepted';Assert ((&$module {$script:sim.calls})-eq$prior) 'Wrong ID contacted network';Json (Join-Path $caseEvidence 'wrong-id.json') $denied}
  Check ($game.name+' / local empty CT opens through configured CE and closes') {
   $tableFolder=Join-Path $library 'YMCC inert CT';$tablePath=Join-Path $tableFolder 'Inert.ct'
   [IO.Directory]::CreateDirectory($tableFolder)|Out-Null
   [IO.File]::WriteAllText($tablePath,'<CheatTable><CheatEntries/></CheatTable>')
   $ctInfo=@{}+$ctCandidate;$ctInfo.extension='Inert.ct';Json (Join-Path $tableFolder 'gcm_info.json') $ctInfo
   $before=&$module {$script:sim.calls};$table=Prepare (Join-Path $case 'ct-cache') $settings $library
   Assert ($table.state-eq'ready'-and$table.origin-eq'ct_other'-and-not$table.downloaded) ('Local CT not ready: '+$table.message)
   Assert ((&$module {$script:sim.calls})-eq$before) 'Local CT caused a download'
   ResetClose $ceEvents;$launch=Launch Trainer $table $settings $cache -WithGame;Assert $launch.ok $launch.error;Own $launch.pid $ceDispatcher $ceEvents
   $started=Read (Join-Path $ceEvents ('opened-'+$launch.pid+'.json'));Assert (@($started.args).Count-eq1-and$started.args[0]-eq$tablePath) 'Wrong CT argument';CloseOwned $launch.pid
   RemoveFixtureFolder $tableFolder;$denied=Launch Trainer $table $settings $cache -WithGame;Assert (-not$denied.ok) 'Deleted CT still launched';$row.receipts.ct=$table;$row.receipts.ctLaunch=$launch
  }
  Check ($game.name+' / same EXE restart rejects old instance and binds new readiness') {
   $oldId=$gp.Id;$oldBirth=[string]$gp.StartTime.ToUniversalTime().ToFileTimeUtc();CloseOwned $gp.Id;ResetClose $gameEvents
   $gp=Start-Process -FilePath $gameExe -WindowStyle Hidden -PassThru;Own $gp.Id $gameExe $gameEvents;Gate
   $errorText='';try{&$module {param($p,$id,$birth,$hash)Assert-TrainerLaunchTarget $p $id $birth $hash} $gameExe $oldId $oldBirth (Get-FileHash -LiteralPath $gameExe).Hash}catch{$errorText=$_.Exception.Message}
   Assert ([bool]$errorText) 'Old instance survived restart'
   $restart=Prepare (Join-Path $case 'restart-cache') $settings $library -Force
   Assert ($restart.state-eq'ready'-and$restart.identity.processId-eq$gp.Id-and$restart.identity.processCreated-ceq[string]$gp.StartTime.ToUniversalTime().ToFileTimeUtc()) ('New instance not rebound: '+$restart.message)
   $row.receipts.restarted=$restart
  }
  $row.http=&$module {$script:sim.trace};CloseOwned $gp.Id;$row.gameClosed=$true;$row.trainerDeleted=(-not[IO.File]::Exists($row.movedPath));$row.lifecycleCompleted=[bool]($row.receipts.launch.ok-and$row.receipts.moved.ok-and$row.trainerDeleted)
  Json (Join-Path $caseEvidence 'game-result.json') $row;$results.Add($row)
 }
}catch{$checks.Add(@{name='lifecycle orchestration';ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Output ('FAIL orchestration: '+$_.Exception.Message)}
finally {
 foreach($entry in $owned){try{CloseOwned $entry.pid}catch{$checks.Add(@{name='owned process cleanup';ok=$false;error=$_.Exception.Message})}}
 $remaining=@();foreach($entry in $owned){$p=Get-Process -Id $entry.pid -ErrorAction SilentlyContinue;if($p-and$p.StartTime.ToUniversalTime().ToFileTimeUtc()-eq$entry.birth){$remaining+=@{pid=$entry.pid;path=$entry.path}}}
 $report=@{ok=(@($checks|Where-Object {-not$_.ok}).Count-eq0-and$results.Count-eq5-and$remaining.Count-eq0);games=$results.ToArray();checks=$checks.ToArray();passed=@($checks|Where-Object {$_.ok}).Count;failed=@($checks|Where-Object {-not$_.ok}).Count;remainingProcesses=$remaining;ownedProcesses=$owned.ToArray();runtimeRoot=$run;evidenceRoot=$evidence;actualThirdPartyTrainerExecuted=$false;realUserGameAccess=$false;at=[DateTimeOffset]::Now.ToString('o')}
 Json (Join-Path $ValidationRoot 'five-game-lifecycle-result.json') $report
 Write-Output ('Five-game lifecycle: '+$report.passed+'/'+$checks.Count+'; games='+$results.Count)
}
if(-not$report.ok){exit 1}
