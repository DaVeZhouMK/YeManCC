[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$Phase='baseline')
$ErrorActionPreference='Stop'
$source=Split-Path $PSScriptRoot
Import-Module (Join-Path $source 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
$module=Get-Module TrainerBackend
function ReadJson([string]$path){[IO.File]::ReadAllText($path,[Text.Encoding]::UTF8)|ConvertFrom-Json}
function Prop($o,[string]$key,$default=''){if($null -ne $o -and $o.PSObject.Properties[$key]){return $o.$key};return $default}
function Key([string]$p){return $p.Replace('/','\').TrimEnd('\').ToLowerInvariant()}
$custom=Join-Path $ValidationRoot 'fixtures/custom-library'
$games=ReadJson (Join-Path $custom 'state/library-scan.json')
$plan=ReadJson (Join-Path $custom 'state/steam-add-plan.json')
$overrides=ReadJson (Join-Path $custom 'config/manual-overrides.json')
$db=Join-Path $ValidationRoot 'fixtures/gcm/db'
$library=Join-Path $ValidationRoot 'fixtures/library'
$local=Join-Path $env:APPDATA 'GCM Trainers'
$record=& $module {param($p) Read-GcmLibraryRecord $p} (Join-Path $library 'ymcc-library.json')
$catalog=@(& $module {param($d,$l) Get-FlingCatalogRows $d $l} $db $library)
$rows=[Collections.Generic.List[object]]::new()
foreach($game in $games.games){
 $exe=[string]$game.primaryExecutable;$k=Key $exe
 $plans=@($plan.items|Where-Object{(Key ([string](Prop $_ 'primaryExecutable'))) -eq $k})
 $item=if($plans.Count -eq 1){$plans[0]}else{$null}
 $override=Prop $overrides.items $k $null
 $formal=[string](Prop $item 'formalName')
 $display=[string](Prop $override 'name')
 if(-not $display){$display=if($formal -and $formal -notmatch '\.(exe|dll|bat|cmd)$'){$formal}elseif($game.primaryProductName){$game.primaryProductName}else{$game.directoryName}}
 $names=@{library=$display;executable=[IO.Path]::GetFileNameWithoutExtension($exe)}
 if($formal -and $formal -notmatch '\.(exe|dll|bat|cmd)$'){$names.formal=$formal}
 $channels=@{}
 foreach($mode in $names.Keys){
  try{
   $parameters=@{GameName=$names[$mode];ExecutablePath=$exe;DatabasePath=$db;Record=$record}
   if((Get-Command Resolve-TrainerIdentity).Parameters.ContainsKey('CustomLibraryRoot')){$parameters.CustomLibraryRoot=$custom}
   $identity=Resolve-TrainerIdentity @parameters
   $found=Find-LocalTrainer @($local) @($identity.aliases)
   $candidate=Find-FlingCandidate $catalog @($identity.aliases)
   $channels[$mode]=@{input=$names[$mode];method=$identity.method;appId=$identity.steamAppId;aliases=$identity.aliases;local=if($found){$found.gameName}else{''};localPath=if($found){$found.path}else{''};catalog=if($candidate){$candidate.game_name}else{''};url=if($candidate){$candidate.official_url}else{''};error=''}
  }catch{$channels[$mode]=@{input=$names[$mode];error=$_.Exception.Message;local='';catalog='';appId=''}}
 }
 $resources=@($channels.Values|ForEach-Object{if($_.local){'local:'+ $_.local}elseif($_.catalog){'catalog:'+ $_.catalog}else{'unavailable'}}|Select-Object -Unique)
 $rows.Add(@{exe=$exe;exists=[IO.File]::Exists($exe);directory=$game.directoryName;type=$game.contentType;channels=$channels;consistent=$resources.Count -eq 1;verifiedStoreId=Prop $item 'steamStoreAppId';verification=Prop $item 'steamVerificationStatus';scanNativeAppId=Prop $game 'nativeSteamAppId'})
}
$result=@{phase=$Phase;at=[DateTimeOffset]::Now.ToString('o');count=$rows.Count;consistent=@($rows|Where-Object{$_.consistent}).Count;localLibraryMatches=@($rows|Where-Object{$_.channels.library.local}).Count;catalogLibraryMatches=@($rows|Where-Object{$_.channels.library.catalog}).Count;games=$rows.ToArray();trainerProcessesStarted=0;trainerDownloads=0;networkRequests=0}
[IO.File]::WriteAllText((Join-Path $ValidationRoot ($Phase+'.json')),(ConvertTo-Json -InputObject $result -Depth 12),[Text.Encoding]::UTF8)
[pscustomobject]$result|Select-Object phase,count,consistent,localLibraryMatches,catalogLibraryMatches|ConvertTo-Json
foreach($row in $rows){$c=$row.channels.library;Write-Host ($row.directory+' | '+$c.method+' | '+$c.local+' | '+$c.catalog+' | consistent='+$row.consistent+' | '+$c.error)}
