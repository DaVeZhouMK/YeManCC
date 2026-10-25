[CmdletBinding()]
param([string]$ValidationRoot='G:\YeManCC-Work\Mainline\Build\Validation\TrainerTmntConnect-20261015')
$ErrorActionPreference='Stop'
Import-Module (Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
$module=Get-Module TrainerBackend
function Assert($value,[string]$message='assertion failed'){if(-not $value){throw $message}}
function Reject([scriptblock]$body){$rejected=$false;try{& $body|Out-Null}catch{$rejected=$true};Assert $rejected 'Expected rejection'}
function Json([string]$path,$value){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))|Out-Null;[IO.File]::WriteAllText($path,(ConvertTo-Json -InputObject $value -Depth 16),[Text.Encoding]::UTF8)}
$testRoot=Join-Path $ValidationRoot ('tests-'+[guid]::NewGuid().ToString('N').Substring(0,8));[IO.Directory]::CreateDirectory($testRoot)|Out-Null
$settings=Join-Path $testRoot 'settings';$db=Join-Path $settings 'db';$library=Join-Path $testRoot 'library';$exe=Join-Path $testRoot 'nonsteam/TMNT.exe'
[IO.Directory]::CreateDirectory((Split-Path $exe))|Out-Null;[IO.File]::WriteAllText($exe,'Do not execute. Process-observation test fixture.')
$name="Teenage Mutant Ninja Turtles: Shredder's Revenge"
$candidate=@{game_name=$name;origin='ct_other';author='GreenHouse';official_url='https://fearlessrevolution.com/viewtopic.php?t=20545';gcm_url="GCM/GCM Trainers/Teenage Mutant Ninja Turtles_Shredder's Revenge Trainer [GreenHouse].ct";version='2022.6.17'}
Json (Join-Path $db 'translations.json') @(@{en_US=$name;zh_CN='忍者神龟：施莱德的复仇';steam_id=1361510})
Json (Join-Path $db 'gcm_trainers.json') @($candidate)
Json (Join-Path $db 'fling_main.json') @()
$pagePath=Join-Path $ValidationRoot 'research/tmnt-author-page.html'
$fixtureHtml='<h2 class="topic-title"><a>'+[Net.WebUtility]::HtmlEncode($name)+' [GreenHouse]</a></h2><div id="p255186" class="post has-profile"><a class="username">GreenHouse</a><a href="./download/file.php?id=43113&amp;sid=do-not-use">TMNT.CT</a></div><div id="p255187" class="post"><a href="./download/file.php?id=99999">Other.CT</a></div>'
$checks=[Collections.Generic.List[object]]::new()
function Check([string]$title,[scriptblock]$body){try{& $body;$checks.Add(@{name=$title;ok=$true});Write-Host ('PASS '+$title)}catch{$checks.Add(@{name=$title;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Host ('FAIL '+$title+' : '+$_.Exception.Message);Write-Host $_.ScriptStackTrace}}
Check 'exact GCM CT fallback uses the known resource and author' {$r=& $module {param($d,$s,$n)Find-GcmPublicCtCandidate $d @($n) $s} $db $settings $name;Assert ($r.gcm_url -eq $candidate.gcm_url)}
Check 'GCM enable switch disables its fallback' {Json (Join-Path $settings 'settings.json') @{enableGCM=$false};try{Assert ($null -eq (& $module {param($d,$s,$n)Find-GcmPublicCtCandidate $d @($n) $s} $db $settings $name))}finally{Json (Join-Path $settings 'settings.json') @{enableGCM=$true}}}
Check 'author post parsing drops sid and ignores reply attachments' {$r=& $module {param($h,$c)Get-GcmPublicCtDownload $h $c} $fixtureHtml $candidate;Assert ($r.url -ceq 'https://fearlessrevolution.com/download/file.php?id=43113')}
if(Test-Path -LiteralPath $pagePath){Check 'actual author page contains the same unique CT' {$r=& $module {param($h,$c)Get-GcmPublicCtDownload $h $c} ([IO.File]::ReadAllText($pagePath)) $candidate;Assert ($r.url -ceq 'https://fearlessrevolution.com/download/file.php?id=43113')}}
Check 'wrong author or title never supplies an attachment' {foreach($html in @($fixtureHtml.Replace('[GreenHouse]','[Other]'),$fixtureHtml.Replace('class="username">GreenHouse','class="username">Other'),$fixtureHtml.Replace('Shredder','Different'))){Reject {& $module {param($h,$c)Get-GcmPublicCtDownload $h $c} $html $candidate}}}
Check 'multiple attachments in author post reject automatic choice' {$html=$fixtureHtml.Replace('</div><div id="p255187"','<a href="./download/file.php?id=1234">Version2.CT</a></div><div id="p255187"');Reject {& $module {param($h,$c)Get-GcmPublicCtDownload $h $c} $html $candidate}}
Check 'CT network policy pins original page and single attachment' {
 $ctx=@{SourcePolicy='gcm-public-ct';SourcePage=$candidate.official_url;CtAttachmentUrl='https://fearlessrevolution.com/download/file.php?id=43113'}
 foreach($url in @('https://evil.example/download/file.php?id=43113','http://fearlessrevolution.com/download/file.php?id=43113','https://fearlessrevolution.com/download/file.php?id=99999','https://fearlessrevolution.com/viewtopic.php?t=1','https://fearlessrevolution.com/download/file.php?id=43113&sid=x')){Reject {& $module {param($c,$u)Assert-TrainerRequestUri $c $u} $ctx $url}}
 Assert ([bool](& $module {param($c,$u)Assert-TrainerRequestUri $c $u} $ctx $ctx.CtAttachmentUrl))
}
$ct=Join-Path $testRoot 'valid.ct';[IO.File]::WriteAllText($ct,'<?xml version="1.0"?><CheatTable CheatEngineTableVersion="45"><CheatEntries/></CheatTable>')
Check 'CT validates XML without executing table content' {Assert (& $module {param($p)Test-TrainerCt $p} $ct)}
Check 'HTML and external XML entities are rejected as CT' {
 foreach($content in @('<html>blocked</html>','<!DOCTYPE CheatTable [<!ENTITY secret SYSTEM "file:///never-read">]><CheatTable><CheatEntries>&secret;</CheatEntries></CheatTable>')){
  $path=Join-Path $testRoot ([guid]::NewGuid().ToString('N')+'.ct');[IO.File]::WriteAllText($path,$content);Assert (-not (& $module {param($p)Test-TrainerCt $p} $path))
 }
}
& $module {param($p,$n,$h,$ct)
 $script:tmntObservation=@{pid=9132;path=$p;created='134365281498356567';title=$n};$script:tmntHtml=$h;$script:tmntCt=$ct;$script:tmntHttpCalls=0;$script:tmntMode='valid'
 function script:Get-TrainerProcessObservation([int]$ProcessId){return $script:tmntObservation}
 function script:Read-TrainerSharedGameTarget {$t=$script:tmntObservation.Clone();$t.name='tmnt';$t.snapshotPath=Join-Path (Split-Path $script:tmntObservation.path) 'game-target.json';return $t}
 function script:Update-TrainerPublicCatalog {return @{available=$false}}
 function script:Get-TrainerResponse($Context,[string]$Url,[string]$Destination='',[string]$Referer=''){
  Assert-TrainerRequestUri $Context $Url|Out-Null;$script:tmntHttpCalls++
  if($Destination){if($script:tmntMode -eq 'html'){[IO.File]::WriteAllText($Destination,'<html>not a table</html>')}else{[IO.File]::Copy($script:tmntCt,$Destination)};if($script:tmntMode -eq 'cancel'){[IO.File]::WriteAllText($Context.CancelPath,'cancel')};return @{url=$Url;bytes=(Get-Item -LiteralPath $Destination).Length}}
  return $script:tmntHtml
 }
} $exe $name $fixtureHtml $ct
Check 'shared top target confirms the selected PID/path/birth' {$r=Resolve-TrainerIdentity $name $exe $db @{games=@{}} (Join-Path $testRoot 'none') 9132 '134365281498356567';Assert ($r.method -eq 'ymcc-game-target' -and $r.steamAppId -eq '1361510' -and $r.executablePath -eq $exe)}
Check 'same full title without a live process observation is not elevated to confirmed' {$r=Resolve-TrainerIdentity $name $exe $db @{games=@{}} (Join-Path $testRoot 'none');Assert ($r.method -eq 'exact-title')}
Check 'wrong birth, path, or top label cannot borrow the full game title' {
 Reject {Resolve-TrainerIdentity $name $exe $db @{games=@{}} (Join-Path $testRoot 'none') 9132 '123456'}
 Reject {Resolve-TrainerIdentity 'Unrelated Game' $exe $db @{games=@{}} (Join-Path $testRoot 'none') 9132 '134365281498356567'}
}
$groupedLibrary=Join-Path $testRoot 'grouped-custom-library'
Json (Join-Path $groupedLibrary 'state/library-scan.json') @{schemaVersion=1;games=@(@{primaryExecutable=(Join-Path $testRoot 'nonsteam/SunshineStory.exe');contentType='game';primaryRole='main-game';status='ready';scanComplete=$true;candidates=@(@{path=$exe;role='main-game';selected=$false;size=(Get-Item -LiteralPath $exe).Length})})}
Check 'real top TMNT is not vetoed by another game selected in a shared library parent' {
 $r=Resolve-TrainerIdentity $name $exe $db @{games=@{}} $groupedLibrary 9132 '134365281498356567'
 Assert ($r.method -eq 'ymcc-game-target' -and $r.steamAppId -eq '1361510' -and $r.processId -eq 9132)
 Assert ('SunshineStory' -notin $r.aliases)
}
Check 'unselected library candidate without live target still cannot borrow its parent identity' {Reject {Resolve-TrainerIdentity $name $exe $db @{games=@{}} $groupedLibrary}}
Check 'library conflict fix does not bypass the selected target name or process birth' {
 Reject {Resolve-TrainerIdentity 'SunshineStory' $exe $db @{games=@{}} $groupedLibrary 9132 '134365281498356567'}
 Reject {Resolve-TrainerIdentity $name $exe $db @{games=@{}} $groupedLibrary 9132 '134365281498356568'}
}
Check 'ambiguous GCM rows cannot use an unselected library candidate as a shortcut' {
 Json (Join-Path $db 'translations.json') @(@{en_US=$name;zh_CN='忍者神龟：施莱德的复仇';steam_id=1361510},@{en_US=$name;steam_id=999999})
 try{Reject {Resolve-TrainerIdentity $name $exe $db @{games=@{}} $groupedLibrary 9132 '134365281498356567'}}finally{Json (Join-Path $db 'translations.json') @(@{en_US=$name;zh_CN='忍者神龟：施莱德的复仇';steam_id=1361510})}
}
function Prepare([string]$suffix,[switch]$Force){$cache=Join-Path $testRoot $suffix;$id='tmnt-'+[guid]::NewGuid().ToString('N');$output=Join-Path $cache ('requests/'+$id+'.json');Invoke-TrainerPrepare -GameName $name -GameExecutablePath $exe -GameProcessId 9132 -GameProcessCreated '134365281498356567' -CacheRoot $cache -ResultPath $output -RequestId $id -GcmSettingsRoot $settings -GcmDefaultRoot $library -CustomLibraryRoot (Join-Path $testRoot 'none') -Force:$Force;return ([IO.File]::ReadAllText($output)|ConvertFrom-Json)}
Check 'top game to CT download to GCM ready and binding is complete' {
 $script:first=Prepare 'first';Assert ($first.state -eq 'ready') $first.message;Assert ($first.identity.method -eq 'ymcc-game-target');Assert ($first.identity.steamAppId -eq '1361510');Assert ($first.origin -eq 'ct_other');Assert ($first.downloaded)
 Assert ($first.trainerFolder -like '*[[]GreenHouse[]]*');Assert ($first.trainerPath.EndsWith('.ct'));Assert ((& $module {$script:tmntHttpCalls}) -eq 2)
 $record=[IO.File]::ReadAllText((Join-Path $library 'ymcc-library.json'))|ConvertFrom-Json;Assert (@($record.downloads).Count -eq 1);Assert (@($record.games.PSObject.Properties)[0].Value.identity.steamAppId -eq '1361510')
 Json (Join-Path $ValidationRoot 'tmnt-mocked-ready.json') $first
}
Check 'fresh frontend cache reuses GCM CT without second download' {$r=Prepare 'reopened';Assert ($r.state -eq 'ready') $r.message;Assert (-not $r.downloaded);Assert ((& $module {$script:tmntHttpCalls}) -eq 2)}

function Scenario([string]$folder,[switch]$Force){$lib=Join-Path $testRoot $folder;$cache=Join-Path $lib 'cache';$request='scenario-'+[guid]::NewGuid().ToString('N');$output=Join-Path $cache ('requests/'+$request+'.json');Invoke-TrainerPrepare -GameName $name -GameExecutablePath $exe -GameProcessId 9132 -GameProcessCreated '134365281498356567' -CacheRoot $cache -ResultPath $output -RequestId $request -GcmSettingsRoot $settings -GcmDefaultRoot $lib -CustomLibraryRoot (Join-Path $testRoot 'none') -Force:$Force;return ([IO.File]::ReadAllText($output)|ConvertFrom-Json)}
Check 'invalid CT downloads count toward shared rolling budget, Force cannot bypass' {
 & $module {$script:tmntMode='html'};$start=& $module {$script:tmntHttpCalls}
 try{
  for($i=0;$i -lt 3;$i++){$r=Scenario 'failed-budget' -Force;Assert ($r.state -eq 'failed') $r.message}
  $r=Scenario 'failed-budget' -Force;Assert ($r.state -eq 'budget-limited') $r.message;Assert ((& $module {$script:tmntHttpCalls}) -eq $start+6)
  $record=[IO.File]::ReadAllText((Join-Path $testRoot 'failed-budget/ymcc-library.json'))|ConvertFrom-Json;Assert (@($record.downloads).Count -eq 3);Assert (@($record.trainers.PSObject.Properties).Count -eq 0)
 }finally{& $module {$script:tmntMode='valid'}}
}
Check 'cancelled CT transfer never becomes an installed ready entry' {
 & $module {$script:tmntMode='cancel'}
 try{$r=Scenario 'cancelled';Assert ($r.state -eq 'cancelled') $r.message;Assert (-not $r.PSObject.Properties['trainerPath'])}finally{& $module {$script:tmntMode='valid'}}
}
Check 'PID mismatch and changing observations cannot confirm a non-Steam target' {
 $original=& $module {$script:tmntObservation.Clone()}
 try{
  & $module {$script:tmntObservation.pid=9999}
  Reject {Resolve-TrainerIdentity $name $exe $db @{games=@{}} (Join-Path $testRoot 'none') 9132 '134365281498356567'}
 }finally{& $module {param($value)$script:tmntObservation=$value} $original}
}
$failed=@($checks|Where-Object{-not $_.ok});$report=@{passed=$checks.Count-$failed.Count;failed=$failed.Count;checks=$checks.ToArray();realNetworkRequests=0;realLaunches=0;root=$testRoot}
Json (Join-Path $ValidationRoot 'tmnt-backend-tests.json') $report
[pscustomobject]$report|Select-Object passed,failed,root|ConvertTo-Json
if($failed.Count){exit 1}
