[CmdletBinding()]
param([switch]$Elevated,[string]$ValidationRoot='', [ValidateSet('Pipeline','Cancel','Lease','LuaFailure')][string]$TestMode='Pipeline', [ValidatePattern('^[a-zA-Z0-9 ._-]+\.exe$')][string]$FixtureExeName='MappedFixture.exe', [string]$GameTitle='CE test fixture', [ValidatePattern('^[0-9]*$')][string]$SteamAppId='')
if(-not $ValidationRoot){$ValidationRoot=Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'Build/Validation/CeEngineBridge-selftest'}
$ErrorActionPreference='Stop';$out=$ValidationRoot;[IO.Directory]::CreateDirectory($out)|Out-Null
$sourceRoot=Split-Path $PSScriptRoot
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){$p=Start-Process -FilePath "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Elevated','-ValidationRoot',('"'+$out+'"'),'-TestMode',$TestMode,'-FixtureExeName',('"'+$FixtureExeName+'"'),'-GameTitle',('"'+$GameTitle+'"'),'-SteamAppId',$(if($SteamAppId){$SteamAppId}else{'0'}));@{testPid=$p.Id;dispatched=$true}|ConvertTo-Json;return}
function Json([string]$p,$v){$temp=$p+'.'+[guid]::NewGuid().ToString('N')+'.tmp';[IO.File]::WriteAllText($temp,($v|ConvertTo-Json -Depth 16 -Compress),[Text.UTF8Encoding]::new($false));if([IO.File]::Exists($p)){[IO.File]::Replace($temp,$p,[NullString]::Value)}else{[IO.File]::Move($temp,$p)}}
function Read([string]$p){$s=[IO.FileStream]::new($p,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete));try{$r=[IO.StreamReader]::new($s,[Text.Encoding]::UTF8);try{return $r.ReadToEnd()|ConvertFrom-Json}finally{$r.Dispose()}}finally{$s.Dispose()}}
function SettingsSnapshot {
 $rows=[Collections.Generic.List[object]]::new();$queue=[Collections.Queue]::new();$queue.Enqueue('Software\Cheat Engine')
 while($queue.Count){$name=[string]$queue.Dequeue();$key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($name,$false);if(-not $key){continue};try{foreach($value in ($key.GetValueNames()|Sort-Object)){$data=$key.GetValue($value,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames);$rows.Add(@{path=$name;name=$value;kind=[string]$key.GetValueKind($value);data=$data})};foreach($sub in ($key.GetSubKeyNames()|Sort-Object)){$queue.Enqueue($name+'\'+$sub)}}finally{$key.Dispose()}}
 return ,$rows.ToArray()
}
$run=Join-Path $out ('fixture-'+[guid]::NewGuid().ToString('N').Substring(0,8));$pc=Join-Path $run 'PowerControl';$session=Join-Path $run ([guid]::NewGuid().ToString('N'));foreach($dir in @((Join-Path $pc 'MemoryScan'),(Join-Path $session 'requests'),(Join-Path $session 'responses'))){[IO.Directory]::CreateDirectory($dir)|Out-Null}
$ceId=0;$report=@{ok=$false;mode=$TestMode;gameTitle=$GameTitle;steamAppId=$SteamAppId;realGameMemoryWrites=0;scope='Production CE plus adapter against owned 1MiB mapped fixture only';checks=@()};$fixture=$null;$helper=$null;$map=$null;$view=$null
try{
 $fixtureFile=Join-Path $run ('steamapps/common/'+$(if($SteamAppId){$SteamAppId}else{'fixture'})+'/bin/'+$FixtureExeName);[IO.Directory]::CreateDirectory((Split-Path $fixtureFile))|Out-Null;& "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /optimize+ /target:winexe /platform:x64 /r:System.Web.Extensions.dll ('/out:'+$fixtureFile) (Join-Path $PSScriptRoot 'game_value_scan_fixture.cs');if($LASTEXITCODE){throw 'Fixture compile failed'}
 $dataFile=Join-Path $run 'fixture-memory.bin';$stream=[IO.File]::Create($dataFile);$stream.SetLength(1048576);$stream.Dispose();$sharedStream=[IO.FileStream]::new($dataFile,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite);$map=[IO.MemoryMappedFiles.MemoryMappedFile]::CreateFromFile($sharedStream,[NullString]::Value,1048576,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite,$null,[IO.HandleInheritability]::None,$true);$view=$map.CreateViewAccessor();$view.Write(0,[int]731001);$view.Write(4,[int]731001)
 $ready=Join-Path $run 'fixture-ready.txt';$fixtureStop=Join-Path $run 'fixture-stop';$fixture=Start-Process -FilePath $fixtureFile -ArgumentList @(('"'+$dataFile+'"'),('"'+$ready+'"'),('"'+$fixtureStop+'"')) -WindowStyle Hidden -PassThru
 $until=[DateTime]::UtcNow.AddSeconds(5);while(-not [IO.File]::Exists($ready)-and[DateTime]::UtcNow-lt$until){Start-Sleep -Milliseconds 50};if(-not[IO.File]::Exists($ready)){throw 'Fixture not ready'}
 $token=[guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N');$owner=Get-Process -Id $PID
 Json (Join-Path $session 'config.json') @{token=$token;pid=$fixture.Id;processCreated=[string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc();path=$fixtureFile;ownerPid=$PID;ownerPath=$owner.Path;ownerCreated=[string]$owner.StartTime.ToUniversalTime().ToFileTimeUtc()}
 function Pulse {Json (Join-Path $session 'lease.json') @{token=$token;at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()};Json (Join-Path $pc 'game-target.json') @{valid=$true;generation=1;lastSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();pid=$fixture.Id;processCreated=[string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc();path=$fixtureFile;title=$GameTitle;name=$GameTitle;steamAppId=$SteamAppId}}
 function WaitFile([string]$path,[int]$seconds=25){$until=[DateTime]::UtcNow.AddSeconds($seconds);while(-not[IO.File]::Exists($path)-and[DateTime]::UtcNow-lt$until){Pulse;if($helper-and$helper.HasExited){throw ('Adapter exited: '+(Read (Join-Path $session 'status.json')).message)};Start-Sleep -Milliseconds 100};if(-not[IO.File]::Exists($path)){throw ('Missing '+$path)}}
 & (Join-Path $PSScriptRoot 'build-game-value-scan-host.ps1') -OutputDirectory (Join-Path $out 'helper') | Out-Null
 foreach($name in @('MemoryScanHost.exe','CeScanEngine.lua')){Copy-Item -LiteralPath (Join-Path $out ('helper/'+$name)) -Destination (Join-Path $pc ('MemoryScan/'+$name))}
 $before=SettingsSnapshot;Json (Join-Path $run 'ce-settings-before.json') $before
 Pulse;$helper=Start-Process -FilePath (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -ArgumentList ('"'+$session+'"') -WindowStyle Hidden -PassThru;WaitFile (Join-Path $session 'status.json');$status=Read (Join-Path $session 'status.json');if($status.stage-ne'idle'){throw $status.message};$report.initial=$status;$ceId=[int](Read (Join-Path $session 'ce-process.json')).pid;$report.checks+=@('CE starts hidden with current settings and no setup dialogs');$script:revision=0
 function Request([string]$action,[string]$value,[string]$expectError=''){Pulse;$id=[guid]::NewGuid().ToString('N');Json (Join-Path $session ('requests/'+$id+'.json')) @{token=$token;id=$id;action=$action;value=$value;revision=$script:revision};$response=Join-Path $session ('responses/'+$id+'.json');WaitFile $response;$result=Read $response;$script:revision=$result.state.revision;if($expectError){if($result.ok -or $result.error -notlike ('*'+$expectError+'*')){throw ('Expected '+$expectError+', got '+$result.error)}}elseif(-not$result.ok){throw ('CE '+$action+': '+$result.error)};return $result}
 if($TestMode -eq 'Cancel'){
  [IO.File]::WriteAllText((Join-Path $session 'cancel'),'cancel');$cancelled=Request first '731001' '取消';if($cancelled.state.count -ne 0){throw 'Cancelled scan published results'}
  if([IO.File]::Exists((Join-Path $session 'cancel'))){throw 'Cancellation marker leaked to later scans'};$report.checks+=@('Pending cancellation is consumed exactly once without dispatching a scan')
 }
 if($TestMode -eq 'Pipeline' -or $TestMode -eq 'Cancel'){
 $a=Request first '731001';if($a.state.count-lt2){throw 'Mapped values absent from CE first scan'};$report.first=$a.state;$report.checks+=@('CE first scan finds both fixture fields using saved scan defaults')
 $view.Write(0,[int]731002);$view.Write(4,[int]731002);$b=Request next '731002';if($b.state.count-ne2){throw ('Expected only 2 mapped values, got '+$b.state.count)};$report.next=$b.state;$report.checks+=@('CE next scan narrows to exactly two changed mapped fields')
 $c=Request writeAll '99999';if($view.ReadInt32(0)-ne99999-or$view.ReadInt32(4)-ne99999){throw 'CE write not observed in mapped fields'};$report.write=$c.state;$report.checks+=@('CE writeInteger changes both fixture values to 99999 and reads them back')

 }
 if($TestMode -eq 'LuaFailure'){
  [IO.File]::WriteAllText((Join-Path $session 'ce-command.txt'),([guid]::NewGuid().ToString('N')+"`n"+$token+"`nunsupported`n0`n"),[Text.UTF8Encoding]::new($false))
  $deadline=[DateTime]::UtcNow.AddSeconds(8);while(-not $helper.HasExited -and [DateTime]::UtcNow -lt $deadline){Pulse;Start-Sleep -Milliseconds 100}
  if(-not $helper.HasExited){throw 'Lua failure left adapter alive'};$report.checks+=@('Lua error closes owned CE and adapter without an active-loop leak')
 }elseif($TestMode -eq 'Lease'){
  if(-not $helper.WaitForExit(22000)){throw 'Expired frontend lease left adapter alive'};$report.checks+=@('Loss of YMCC heartbeat closes owned CE without touching other processes')
 }else{
  [IO.File]::WriteAllText((Join-Path $session 'stop'),'stop');if(-not $helper.WaitForExit(12000)){throw 'Adapter did not stop gracefully'}
 }
 if(Get-Process -Id $ceId -ErrorAction SilentlyContinue){throw 'Owned CE process leaked after adapter exit'};$report.ceExited=$true
 $after=SettingsSnapshot;Json (Join-Path $run 'ce-settings-after.json') $after;$report.settingsPreserved=(($before|ConvertTo-Json -Depth 16 -Compress)-ceq($after|ConvertTo-Json -Depth 16 -Compress));if(-not$report.settingsPreserved){throw 'CE registry changed; inspect exact before/after values'};$report.checks+=@('CE registry settings remain byte-for-byte equivalent after scanning and exit');$report.ok=$true
}catch{$report.error=$_.Exception.Message;$report.stack=$_.ScriptStackTrace}
finally{if([IO.Directory]::Exists($session)){[IO.File]::WriteAllText((Join-Path $session 'stop'),'stop')};if($helper){$report.adapterExited=$helper.WaitForExit(12000)};if($fixture){[IO.File]::WriteAllText((Join-Path $run 'fixture-stop'),'stop');$report.fixtureExited=$fixture.WaitForExit(5000)};if($view){$view.Dispose()};if($map){$map.Dispose()};$report.run=$run;$report.at=[DateTimeOffset]::Now.ToString('o');Json (Join-Path $out ('ce-fixture-'+$TestMode.ToLowerInvariant()+'-result.json')) $report
 if(-not $report.ok){Write-Error $report.error -ErrorAction Continue;exit 1}
 $report|ConvertTo-Json -Depth 3}