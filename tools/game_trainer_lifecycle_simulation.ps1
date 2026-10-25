[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$Phase='baseline',[string]$SampleRoot='G:\YeManCC-Work\Mainline\Build\Validation\TrainerSteamPopular-20261014')
$ErrorActionPreference='Stop'
Import-Module (Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
$module=Get-Module TrainerBackend
function Json([string]$p,$v){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null;[IO.File]::WriteAllText($p,(ConvertTo-Json -InputObject $v -Depth 16),[Text.Encoding]::UTF8)}
function ReadJson([string]$p){[IO.File]::ReadAllText($p,[Text.Encoding]::UTF8)|ConvertFrom-Json}
function Prop($o,[string]$key,$fallback=''){if($null -ne $o -and $o.PSObject.Properties[$key]){return $o.$key};return $fallback}
function Assert($v,[string]$message){if(-not $v){throw $message}}
$runRoot=Join-Path $ValidationRoot $Phase
if(Test-Path -LiteralPath $runRoot){throw 'Use a new phase directory to avoid reusing old test state'}
[IO.Directory]::CreateDirectory($runRoot)|Out-Null
$fixture=Join-Path $runRoot 'non-runnable-package.exe'
$bytes=New-Object byte[] 128;$bytes[0]=0x4d;$bytes[1]=0x5a;$bytes[60]=80;$bytes[80]=0x50;$bytes[81]=0x45
[IO.File]::WriteAllBytes($fixture,$bytes)
$sample=ReadJson (Join-Path $SampleRoot 'steam/metadata-selection.json')
$db=Join-Path $SampleRoot 'fixtures/gcm/db';$oldLibrary=Join-Path $SampleRoot 'fixtures/library'
$catalog=@(& $module {param($d,$l) Get-FlingCatalogRows $d $l} $db $oldLibrary)
$translations=@(& $module {param($d) Get-GcmCatalogRows $d 'translations.json'} $db)
& $module {
 param($package)
 $script:simulationPackage=$package;$script:simulationCalls=0;$script:simulationHtml='';$script:simulationUrl='';$script:allowSimulationDownload=$false
 function script:Get-TrainerResponse($Context,[string]$Url,[string]$Destination='',[string]$Referer=''){
  $script:simulationCalls++
  if(-not $script:allowSimulationDownload){throw 'Offline test forbids network fallback'}
  if($Url -eq $script:simulationUrl -and -not $Destination){return $script:simulationHtml}
  if($Url -eq 'https://flingtrainer.com/downloads/ymcc-simulation-only' -and $Destination){[IO.File]::Copy($script:simulationPackage,$Destination);return @{url=$Url;bytes=128}}
  throw 'Unexpected offline fixture URL'
 }
} $fixture
$all=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[object]]::new()
foreach($game in $sample.eligible){
 $caseRoot=Join-Path $runRoot ([string]$game.appid);$settings=Join-Path $caseRoot 'settings';$localLibrary=Join-Path $caseRoot 'local-library';$downloadLibrary=Join-Path $caseRoot 'download-library';$custom=Join-Path $caseRoot 'empty-custom-library'
 $steamapps=Join-Path $caseRoot 'steamapps';$exe=Join-Path $steamapps 'common/Sample/bin/Game.exe';$manifest=Join-Path $steamapps ('appmanifest_'+$game.appid+'.acf')
 [IO.Directory]::CreateDirectory((Split-Path $exe))|Out-Null;[IO.File]::WriteAllText($exe,('Never execute; sample game '+$game.appid))
 $acf='"AppState"'+"`n{`n"+'"appid" "'+$game.appid+'"'+"`n"+'"name" "'+$game.name.Replace('"','')+'"'+"`n"+'"installdir" "Sample"'+"`n}"
 [IO.File]::WriteAllText($manifest,$acf,[Text.Encoding]::UTF8)
 $identityConflict=@($translations|Where-Object{$_.PSObject.Properties['steam_id'] -and [string]$_.steam_id -match '^[1-9][0-9]{0,9}$' -and (Get-TrainerNameKey ([string]$_.en_US)) -eq (Get-TrainerNameKey $game.name) -and [string]$_.steam_id -ne [string]$game.appid})
 $officialRows=@($translations|Where-Object{[string]$_.steam_id -eq [string]$game.appid})
 if($identityConflict.Count -and -not $officialRows.Count){
  Json (Join-Path $settings 'db/translations.json') @($identityConflict)
  $cache=Join-Path $caseRoot 'blocked-cache';$rid='blocked-'+[guid]::NewGuid().ToString('N');$output=Join-Path $cache ('requests/'+$rid+'.json');$beforeCalls=& $module {$script:simulationCalls}
  Invoke-TrainerPrepare -GameName 'Game' -GameExecutablePath $exe -CacheRoot $cache -ResultPath $output -RequestId $rid -GcmSettingsRoot $settings -GcmDefaultRoot $downloadLibrary -CustomLibraryRoot $custom
  $receipt=ReadJson $output;$ledgerPath=Join-Path $downloadLibrary 'ymcc-library.json';$noDownloads=-not [IO.File]::Exists($ledgerPath);if(-not $noDownloads){$noDownloads=@((ReadJson $ledgerPath).downloads).Count -eq 0}
  $row=@{appid=$game.appid;name=$game.name;checks=@{conflictingIdBlocked=($receipt.state -eq 'failed' -and $receipt.message -eq '游戏名称相同但 GCM 记录的 Steam ID 不同，未自动下载');conflictingIdNoNetwork=((& $module {$script:simulationCalls}) -eq $beforeCalls);conflictingIdNoDownloadCharge=$noDownloads};receipts=@{blocked=$receipt};expectedBlocked=$true}
  $all.Add($row);Write-Host ($game.appid.ToString()+' | expected ID conflict, blocked without download');continue
 }
 $identity=Resolve-TrainerIdentity 'Game' $exe $db @{games=@{}} $custom
 $aliases=@($identity.aliases);$candidate=Find-FlingCandidate $catalog $aliases;$canonical=if($candidate){[string]$candidate.game_name}else{[string]$aliases[0]}
 $resourceId=if($candidate){[string](Prop $candidate 'gcm_url')}else{''}
 Json (Join-Path $settings 'db/translations.json') @($translations|Where-Object{[string]$_.steam_id -eq [string]$game.appid})
 $candidateRows=@();if($candidate){$candidateRows=@($candidate)}
 Json (Join-Path $settings 'db/fling_main.json') $candidateRows
 $safeName=$canonical;foreach($c in [IO.Path]::GetInvalidFileNameChars()){$safeName=$safeName.Replace([string]$c,'')}
 $localFolder=Join-Path $localLibrary ('[FLiNG]《'+$safeName+'》修改器');[IO.Directory]::CreateDirectory($localFolder)|Out-Null
 $trainer=Join-Path $localFolder 'GenericTrainer.exe';[IO.File]::Copy($fixture,$trainer)
 $info=Join-Path $localFolder 'gcm_info.json';$metadata=@{game_name=$canonical;origin='fling_main';extension='GenericTrainer.exe';gcm_url=$resourceId}
 Json $info $metadata
 $row=@{appid=$game.appid;name=$game.name;gameExe=$exe;resourceId=$resourceId;catalogMatched=[bool]$candidate;checks=@{};receipts=@{}}
 function Prepare([string]$cacheName,[string]$name,[string]$gameExe,[string]$library){
  $cache=Join-Path $caseRoot $cacheName;$id='lifecycle-'+[guid]::NewGuid().ToString('N');$output=Join-Path $cache ('requests/'+$id+'.json')
  Invoke-TrainerPrepare -GameName $name -GameExecutablePath $gameExe -CacheRoot $cache -ResultPath $output -RequestId $id -GcmSettingsRoot $settings -GcmDefaultRoot $library -CustomLibraryRoot $custom
  return ReadJson $output
 }
 try{
  $calls=& $module {$script:simulationCalls}
  $local=Prepare 'local-cache' 'Game' $exe $localLibrary
  $row.receipts.local=$local
  $row.checks.localWithoutBinding=($local.state -eq 'ready' -and [string]$local.identity.steamAppId -eq [string]$game.appid -and $local.trainerPath -eq $trainer -and (& $module {$script:simulationCalls}) -eq $calls)
  $record=ReadJson (Join-Path $localLibrary 'ymcc-library.json');$binding=@($record.games.PSObject.Properties)[0].Value
  $row.checks.bindingRecorded=($binding.executablePath -eq $exe -and $binding.executableSha256 -eq (Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant() -and [string]$binding.identity.steamAppId -eq [string]$game.appid)
  $row.checks.localMarkedAsNotDownloaded= -not [bool]$local.downloaded
  if($resourceId){
   $metadata.game_name='Player custom trainer label';Json $info $metadata
   $byId=Find-LocalTrainer @($localLibrary) $aliases (Join-Path $settings 'db')
   $idLibrary=Join-Path $caseRoot 'id-only-library';$idFolder=Join-Path $idLibrary 'Player renamed folder';[IO.Directory]::CreateDirectory($idFolder)|Out-Null
   [IO.File]::Copy($fixture,(Join-Path $idFolder 'GenericTrainer.exe'));Json (Join-Path $idFolder 'gcm_info.json') $metadata
   $calls=& $module {$script:simulationCalls};$byIdReceipt=Prepare 'id-cache' 'Game' $exe $idLibrary;$row.receipts.resourceId=$byIdReceipt
   $idRecord=ReadJson (Join-Path $idLibrary 'ymcc-library.json');$idBinding=@($idRecord.games.PSObject.Properties)[0].Value
   $row.checks.resourceIdReconnect=([bool]$byId -and $byId.path -eq $trainer -and $byIdReceipt.state -eq 'ready' -and [string]$byIdReceipt.identity.steamAppId -eq [string]$game.appid -and (& $module {$script:simulationCalls}) -eq $calls -and @($idRecord.downloads).Count -eq 0 -and [string]$idBinding.identity.steamAppId -eq [string]$game.appid)
   $row.checks.resourceIdInventoryUnique=(@($idRecord.trainers.PSObject.Properties).Count -eq 1)
   $metadata.game_name=$canonical;Json $info $metadata
  }
  [IO.File]::Move($info,$info+'.saved')
  try{$anonymous=Find-LocalTrainer @($localLibrary) $aliases (Join-Path $settings 'db');$row.checks.bareFilenameNotConfirmed=($null -eq $anonymous)}finally{[IO.File]::Move($info+'.saved',$info)}
  $unboundExe=Join-Path $caseRoot 'unbound/Game.exe';[IO.Directory]::CreateDirectory((Split-Path $unboundExe))|Out-Null;[IO.File]::WriteAllText($unboundExe,'Unconfirmed process; never execute')
  $unbound=Prepare 'unbound-cache' $canonical $unboundExe $localLibrary
  $row.receipts.unbound=$unbound;$row.unboundExe=$unboundExe
  $row.checks.titleAloneRemainsUnconfirmed=($unbound.state -eq 'ready' -and $unbound.identity.method -eq 'exact-title')
  $row.checks.otherGameNotMatched=($null -eq (Find-LocalTrainer @($localLibrary) @('A different game outside all aliases') (Join-Path $settings 'db')))
  if($candidate){
   $heading=[Net.WebUtility]::HtmlEncode($canonical+' Trainer')
   $html='<h1 class="entry-title">'+$heading+'</h1><tr><td><a href="https://flingtrainer.com/downloads/ymcc-simulation-only" class="attachment-link">'+[Net.WebUtility]::HtmlEncode($canonical+'.Trainer-FLiNG')+'</a></td><td class="attachment-date">2026-10-14 12:00</td></tr>'
   & $module {param($h,$url)$script:simulationHtml=$h;$script:simulationUrl=$url;$script:allowSimulationDownload=$true} $html $candidate.official_url
   $calls=& $module {$script:simulationCalls};$download=Prepare 'download-cache' 'Game' $exe $downloadLibrary
   & $module {$script:allowSimulationDownload=$false}
   $row.receipts.download=$download
   $row.checks.downloadIdentity=($download.state -eq 'ready' -and [string]$download.identity.steamAppId -eq [string]$game.appid -and [bool]$download.downloaded -and (& $module {$script:simulationCalls}) -eq $calls+2)
   Assert ($download.state -eq 'ready') $download.message
   $ledger=ReadJson (Join-Path $downloadLibrary 'ymcc-library.json');$recorded=@($ledger.games.PSObject.Properties)[0].Value
   $row.checks.downloadLedger=($recorded.executablePath -eq $exe -and [string]$recorded.identity.steamAppId -eq [string]$game.appid -and @($ledger.downloads).Count -eq 1 -and $ledger.downloads[0].chargedBytes -eq 128)
   $calls=& $module {$script:simulationCalls};$again=Prepare 'reopened-cache' 'Game' $exe $downloadLibrary;$row.receipts.reopened=$again
   $nextLedger=ReadJson (Join-Path $downloadLibrary 'ymcc-library.json')
   $row.checks.reopenNoDownload=($again.state -eq 'ready' -and $again.trainerPath -eq $download.trainerPath -and [string]$again.identity.steamAppId -eq [string]$game.appid -and (& $module {$script:simulationCalls}) -eq $calls -and @($nextLedger.downloads).Count -eq 1)
   $row.checks.reopenMarkedAsNotDownloaded= -not [bool]$again.downloaded
  }
 }catch{$row.error=$_.Exception.Message;$errors.Add(@{appid=$game.appid;error=$_.Exception.Message;stack=$_.ScriptStackTrace})}finally{& $module {$script:allowSimulationDownload=$false}}
 $all.Add($row)
 Write-Host ($game.appid.ToString()+' | checks '+@($row.checks.Values|Where-Object{$_}).Count+'/'+$row.checks.Count+' | '+[string]$row.error)
}
$stats=@{};foreach($row in $all){foreach($key in $row.checks.Keys){if(-not $stats.ContainsKey($key)){$stats[$key]=@{passed=0;total=0}};$stats[$key].total++;if($row.checks[$key]){$stats[$key].passed++}}}
$result=@{phase=$Phase;sampleCount=$all.Count;realNetworkRequests=0;realDownloads=0;realLaunches=0;mockHttpCalls=(& $module {$script:simulationCalls});stats=$stats;errors=$errors.ToArray();games=$all.ToArray()}
Json (Join-Path $ValidationRoot ($Phase+'.json')) $result
[pscustomobject]@{phase=$Phase;count=$all.Count;stats=$stats;errors=$errors.Count}|ConvertTo-Json -Depth 6
if($Phase -notlike 'baseline*' -and ($errors.Count -or @($stats.Values|Where-Object{$_.passed -ne $_.total}).Count)){throw 'Trainer lifecycle simulation failed'}
