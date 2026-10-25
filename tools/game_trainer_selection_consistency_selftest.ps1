[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$Phase='before',[string]$BackendPath='')
$ErrorActionPreference='Stop';[IO.Directory]::CreateDirectory($ValidationRoot)|Out-Null
if(-not $BackendPath){$BackendPath=Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1'}
Import-Module $BackendPath -Force -DisableNameChecking
$module=Get-Module TrainerBackend;$checks=[Collections.Generic.List[object]]::new()
function Json([string]$p,$v){[IO.Directory]::CreateDirectory((Split-Path $p))|Out-Null;[IO.File]::WriteAllText($p,(ConvertTo-Json -InputObject $v -Depth 12 -Compress),[Text.UTF8Encoding]::new($false))}
function Assert($v,[string]$m='assertion failed'){if(-not $v){throw $m}}
function Check([string]$name,[scriptblock]$body){try{&$body;$checks.Add(@{name=$name;ok=$true});Write-Output ('PASS '+$name)}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Output ('FAIL '+$name+': '+$_.Exception.Message)}}
foreach($kind in @('shared-target-changes','steam-id-changes','exe-replaced','cancel-after-payload','unchanged')){
 $case=Join-Path $ValidationRoot ($Phase+'/'+$kind);$settings=Join-Path $case 'settings';$db=Join-Path $settings 'db';$library=Join-Path $case 'GCM';$cache=Join-Path $case 'cache'
 $steamapps=Join-Path $case 'steamapps';$exe=Join-Path $steamapps 'common/Cyberpunk2077/bin/x64/Cyberpunk2077.exe';[IO.Directory]::CreateDirectory((Split-Path $exe))|Out-Null;[IO.File]::WriteAllText($exe,'original-game-exe-fixture')
 $manifest=Join-Path $steamapps 'appmanifest_1091500.acf';$acf='"AppState"'+"`n{`n"+'"appid" "1091500"'+"`n"+'"name" "Cyberpunk 2077"'+"`n"+'"installdir" "Cyberpunk2077"'+"`n}";[IO.File]::WriteAllText($manifest,$acf)
 Json (Join-Path $db 'translations.json') @(@{en_US='Cyberpunk 2077';zh_CN='赛博朋克2077';steam_id=1091500},@{en_US='Other Game';steam_id=99999})
 Json (Join-Path $db 'fling_main.json') @(@{game_name='Cyberpunk 2077';official_url='https://flingtrainer.com/trainer/cyberpunk-2077-trainer/'})
 Json (Join-Path $settings 'settings.json') @{downloadPath=$library}
 $target=@{pid=7311001;created='134365580704383971';path=$exe;title='Cyberpunk 2077';name='Cyberpunk 2077'}
 & $module {param($target,$kind,$exe,$manifest,$acf)
  $script:SelectionTarget=$target.Clone();$script:SelectionObserved=$target.Clone();$script:SelectionKind=$kind;$script:SelectionExe=$exe;$script:SelectionManifest=$manifest;$script:SelectionAcf=$acf;$script:SelectionHttp=0
  function script:Read-TrainerSharedGameTarget{return $script:SelectionTarget}
  function script:Get-TrainerProcessObservation {param([int]$ProcessId,[bool]$RequireTitle=$true)if($ProcessId -eq $script:SelectionObserved.pid){return $script:SelectionObserved}}
  function script:Get-TrainerResponse {param($Context,[string]$Url,[string]$Destination='',[string]$Referer='')
   $script:SelectionHttp++
   if(-not $Destination){return '<h1>Cyberpunk 2077 Trainer</h1><tr><td><a href="https://flingtrainer.com/downloads/ymcc-consistency-fixture" class="attachment-link">Cyberpunk.2077.Trainer-FLiNG</a></td><td class="attachment-date">2026-10-16 00:00</td></tr>'}
   $bytes=New-Object byte[] 128;$bytes[0]=0x4d;$bytes[1]=0x5a;$bytes[60]=80;$bytes[80]=0x50;$bytes[81]=0x45;[IO.File]::WriteAllBytes($Destination,$bytes)
   switch($script:SelectionKind){
    'shared-target-changes' {$script:SelectionTarget=$script:SelectionTarget.Clone();$script:SelectionTarget.pid=7311002}
    'steam-id-changes' {[IO.File]::Delete($script:SelectionManifest);[IO.File]::WriteAllText((Join-Path (Split-Path $script:SelectionManifest) 'appmanifest_99999.acf'),$script:SelectionAcf.Replace('1091500','99999').Replace('Cyberpunk 2077','Other Game'))}
    'exe-replaced' {[IO.File]::WriteAllText($script:SelectionExe,'replacement-game-exe-fixture')}
    'cancel-after-payload' {[IO.File]::WriteAllText($Context.CancelPath,'cancel')}
   }
   return @{url=$Url;bytes=128}
  }
 } $target $kind $exe $manifest $acf
 $id='consistency-'+[guid]::NewGuid().ToString('N');$out=Join-Path $cache ('requests/'+$id+'.json')
 Invoke-TrainerPrepare -GameName 'Cyberpunk 2077' -GameExecutablePath $exe -GameProcessId $target.pid -GameProcessCreated $target.created -CacheRoot $cache -ResultPath $out -RequestId $id -GcmSettingsRoot $settings -GcmDefaultRoot $library -CustomLibraryRoot (Join-Path $case 'empty-custom')
 $receipt=[IO.File]::ReadAllText($out)|ConvertFrom-Json
 Check ($kind+' cannot publish a stale ready association') {
  if($kind -eq 'unchanged'){Assert ($receipt.state -eq 'ready') $receipt.message;Assert $receipt.downloaded}
  else {Assert ($receipt.state -in @('failed','cancelled')) ('Expected rejection, received '+$receipt.state)}
  $ledger=[IO.File]::ReadAllText((Join-Path $library 'ymcc-library.json'))|ConvertFrom-Json
  Assert (@($ledger.downloads).Count -eq 1) 'One failed/successful attempt must still be charged exactly once'
  Assert (@($ledger.games.PSObject.Properties).Count -eq $(if($kind -eq 'unchanged'){1}else{0})) 'Stale executable association persisted'
 }
}
& $module {param($target)$script:SelectionTarget=$target.Clone();$script:SelectionTarget.pid=7311002} $target
Check 'final trainer launch rejects a previously selected game that is still alive' { $rejected=$false;try{& $module {param($t)Assert-TrainerLaunchTarget $t.path $t.pid $t.created ''} $target}catch{$rejected=$true};Assert $rejected 'Old still-running game passed final launch guard'}
$failed=@($checks|Where-Object{-not $_.ok});$report=@{phase=$Phase;passed=$checks.Count-$failed.Count;failed=$failed.Count;checks=$checks.ToArray();realDownloads=0;realLaunches=0;networkRequests=0}
Json (Join-Path $ValidationRoot ($Phase+'.json')) $report;[pscustomobject]$report|Select-Object phase,passed,failed|ConvertTo-Json
if($Phase -ne 'before' -and $failed.Count){exit 1}