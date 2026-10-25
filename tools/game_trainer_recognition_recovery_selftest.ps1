[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$BackendPath='')
$ErrorActionPreference='Stop'
if(-not $BackendPath){$BackendPath=Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1'}
Import-Module $BackendPath -Force -DisableNameChecking
$module=Get-Module TrainerBackend
[IO.Directory]::CreateDirectory($ValidationRoot)|Out-Null
$checks=[Collections.Generic.List[object]]::new()
function Json([string]$p,$value){[IO.Directory]::CreateDirectory((Split-Path $p))|Out-Null;[IO.File]::WriteAllText($p,($value|ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($false))}
function Assert($value,[string]$message='assertion failed'){if(-not $value){throw $message}}
function Check([string]$name,[scriptblock]$body){try{&$body;$checks.Add(@{name=$name;ok=$true});Write-Output ('PASS '+$name)}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Output ('FAIL '+$name+': '+$_.Exception.Message)}}
$cases=@('stale-shared','cold-db','unsupported-live','wrong-instance')
foreach($kind in $cases){
 $root=Join-Path $ValidationRoot ($kind+'-'+[guid]::NewGuid().ToString('N').Substring(0,8));$settings=Join-Path $root 'settings';$db=Join-Path $settings 'db';$library=Join-Path $root 'GCM';$cache=Join-Path $root 'cache';$snapshot=Join-Path $root 'game-target.json'
 $gameName="Teenage Mutant Ninja Turtles: Shredder's Revenge";$exe=Join-Path $root 'Games/TMNT.exe';[IO.Directory]::CreateDirectory((Split-Path $exe))|Out-Null;[IO.File]::WriteAllText($exe,'inert game image metadata fixture')
 $table=Join-Path $root 'GCM/[GreenHouse] TMNT/Trainer.ct';[IO.Directory]::CreateDirectory((Split-Path $table))|Out-Null;[IO.File]::WriteAllText($table,'<?xml version="1.0"?><CheatTable CheatEngineTableVersion="45"><CheatEntries/></CheatTable>')
 Json (Join-Path (Split-Path $table) 'gcm_info.json') @{game_name=$gameName;origin='ct_other';extension='Trainer.ct'}
 Json (Join-Path $settings 'settings.json') @{downloadPath=$library}
 if($kind -ne 'cold-db'){Json (Join-Path $db 'translations.json') @(@{en_US=$gameName;zh_CN='忍者神龟：施莱德的复仇';steam_id='1361510'})}
 $now=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();$processId=9132;$created='134365281498356567'
 $target=@{valid=$true;generation=1;lastSeen=$now;pid=$processId;processCreated=$created;path=$exe;title=$gameName;name='TMNT'}
 if($kind -eq 'stale-shared'){$target.lastSeen=$now-60000}
 if($kind -eq 'unsupported-live'){$gameName='Bluey: The Videogame';$target.title=$gameName;$target.name='Project biscuits'}
 if($kind -eq 'wrong-instance'){$target.pid=9133}
 Json $snapshot $target
 & $module {
  param($snapshotPath,$image,$birth,$database,$name)
  $script:TrainerGameTargetPath=$snapshotPath;$script:fixtureImage=$image;$script:fixtureBirth=$birth;$script:fixtureDatabase=$database;$script:fixtureName=$name;$script:catalogRefreshes=0;$script:networkCalls=0
  function script:Get-TrainerProcessObservation([int]$ProcessId,[bool]$RequireTitle=$true){return @{pid=$ProcessId;path=$script:fixtureImage;created=$script:fixtureBirth;title=$script:fixtureName}}
  function script:Update-TrainerPublicCatalog($Context,[string]$LibraryRoot,[string]$DatabasePath){
   $script:catalogRefreshes++
   if($script:fixtureName -notlike 'Bluey*'){
    [IO.Directory]::CreateDirectory($DatabasePath)|Out-Null
    [IO.File]::WriteAllText((Join-Path $DatabasePath 'translations.json'),(ConvertTo-Json -InputObject @(@{en_US=$script:fixtureName;steam_id='1361510'}) -Depth 5),[Text.UTF8Encoding]::new($false))
   }
   return @{available=$true;updated=$true}
  }
  function script:Get-TrainerResponse { $script:networkCalls++;throw 'Unexpected real network operation' }
 } $snapshot $exe $created $db $gameName
 $requestId='recognition-'+[guid]::NewGuid().ToString('N');$result=Join-Path $cache ('requests/'+$requestId+'.json')
 Invoke-TrainerPrepare -GameName $gameName -GameExecutablePath $exe -GameProcessId $processId -GameProcessCreated $created -CacheRoot $cache -ResultPath $result -RequestId $requestId -GcmSettingsRoot $settings -GcmDefaultRoot $library -CustomLibraryRoot (Join-Path $root 'no-library')
 $receipt=[IO.File]::ReadAllText($result,[Text.Encoding]::UTF8)|ConvertFrom-Json;Json (Join-Path $root 'receipt.json') $receipt
 if($kind -eq 'stale-shared'){
  Check 'stale shared snapshot exposes retryable status with a short bounded delay' {Assert ($receipt.state -eq 'failed');Assert ($receipt.code -eq 'shared-target-unavailable');Assert $receipt.retryable;Assert ($receipt.retryAfter -lt ($now+20000));Assert ($receipt.gameExecutablePath -eq $exe)}
 }elseif($kind -eq 'cold-db'){
  Check 'fresh machine resolves identity after metadata import before publishing ready' {Assert ($receipt.state -eq 'ready') ('State: '+$receipt.state+' '+$receipt.message);Assert ($receipt.identity.method -eq 'ymcc-game-target');Assert ($receipt.identity.steamAppId -eq '1361510');Assert (-not $receipt.downloaded);Assert ((&$module{$script:catalogRefreshes}) -eq 1)}
 }elseif($kind -eq 'unsupported-live'){
  Check 'Bluey log sample without catalog support stays unconfirmed and downloads nothing' {Assert ($receipt.state -eq 'unavailable');Assert ($receipt.code -eq 'identity-unconfirmed');Assert (-not $receipt.retryable);Assert ((&$module{$script:networkCalls}) -eq 0)}
 }else{
  Check 'another shared process instance is rejected without retry authority' {Assert ($receipt.state -eq 'failed');Assert (-not $receipt.retryable);Assert ((&$module{$script:networkCalls}) -eq 0)}
 }
 Check ($kind+' never downloads a file or starts an executable') {Assert ((&$module{$script:networkCalls}) -eq 0);Assert (-not [IO.Directory]::Exists((Join-Path $cache 'staging')))}
}
Json (Join-Path $ValidationRoot 'result.json') @($checks.ToArray());$failed=@($checks|Where-Object{-not $_.ok});Write-Output ('Trainer recognition recovery: '+($checks.Count-$failed.Count)+'/'+$checks.Count);if($failed.Count){exit 1}