[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
Import-Module (Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
$module=Get-Module TrainerBackend
[IO.Directory]::CreateDirectory($ValidationRoot)|Out-Null
$checks=[Collections.Generic.List[object]]::new()
function Assert($value,[string]$message='assertion failed'){if(-not $value){throw $message}}
function Reject([scriptblock]$action){$failed=$false;try{& $action|Out-Null}catch{$failed=$true};Assert $failed 'expected rejection'}
function Json([string]$path,$value){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))|Out-Null;[IO.File]::WriteAllText($path,(ConvertTo-Json -InputObject $value -Depth 12),[Text.Encoding]::UTF8)}
function Check([string]$name,[scriptblock]$test){try{& $test;$checks.Add(@{name=$name;ok=$true});Write-Host "PASS $name"}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Host "FAIL $name : $_";Write-Host $_.ScriptStackTrace}}
$settings=Join-Path $ValidationRoot 'settings'
$db=Join-Path $settings 'db'
$library=Join-Path $ValidationRoot 'library'
Json (Join-Path $settings 'settings.json') @{downloadPath=$library;safePath=$false;launchAsAdmin=$true;launchCTAsAdmin=$false;cePath=(Join-Path $ValidationRoot 'Cheat Engine')}
Json (Join-Path $db 'translations.json') @(@{en_US='Victoria 3';zh_CN='维多利亚3';steam_id='529340'})
Json (Join-Path $db 'fling_main.json') @(@{game_name='Victoria 3';official_url='https://flingtrainer.com/trainer/victoria-3-trainer/';gcm_url='GCM/fixture'})
$context=@{CancelPath=(Join-Path $ValidationRoot 'cancel');Deadline=[DateTime]::UtcNow.AddMinutes(5)}
& $module {
    $script:catalogCalls=0;$script:catalogMode='ok';$script:seenEtag=''
    function script:Get-TrainerResponse($Context,[string]$Url,[string]$Destination='',[string]$Referer='') {
        $script:catalogCalls++;$script:seenEtag=$Context.ConditionalETag
        if($script:catalogMode -eq 'fail'){throw 'network unavailable'}
        if($script:catalogMode -eq '304'){return @{notModified=$true;etag='fixture-etag';bytes=0L;text=''}}
        $html=(1..25|ForEach-Object{'<a href="https://flingtrainer.com/trainer/fixture-'+$_+'-trainer/">Fixture '+$_+' Trainer</a>'}) -join ''
        return @{notModified=$false;etag='fixture-etag';bytes=[Text.Encoding]::UTF8.GetByteCount($html);text=$html}
    }
}
function Refresh { & $module {param($c,$l,$d) Update-TrainerPublicCatalog $c $l $d} $context $library $db }
function Expire { $p=Join-Path $library 'ymcc-data-state.json';$s=Get-Content -LiteralPath $p -Raw -Encoding UTF8|ConvertFrom-Json;$s.nextCheckAt=0;Json $p $s }
Check 'single-row official database is imported without losing array shape' { $r=@(& $module {param($d) Get-GcmCatalogRows $d 'translations.json'} $db);Assert ($r.Count -eq 1);Assert ($r[0].en_US -eq 'Victoria 3') }
Check 'official file corruption falls back to validated snapshot' { [IO.File]::WriteAllText((Join-Path $db 'translations.json'),'[{');$r=@(& $module {param($d) Get-GcmCatalogRows $d 'translations.json'} $db);Assert ($r.Count -eq 1);Assert ($r[0].en_US -eq 'Victoria 3') }
Check 'updated official schema preserves known rows and ignores additive fields' { Json (Join-Path $db 'translations.json') @(@{en_US='Victoria 3';zh_CN='维多利亚3';steam_id='529340';new_vendor_field=@{v=1}},@{en_US='Other';steam_id='999'});$r=@(& $module {param($d) Get-GcmCatalogRows $d 'translations.json'} $db);Assert ($r.Count -eq 2) }
Check 'public refresh persists catalog and quota before returning success' { $r=Refresh;Assert $r.available;Assert ($r.count -eq 25);$s=Get-Content -LiteralPath (Join-Path $library 'ymcc-data-state.json') -Raw -Encoding UTF8|ConvertFrom-Json;Assert ($s.attempts.Count -eq 1);Assert ($s.attempts[0].state -eq 'ready') }
Check 'daily cooldown performs no second network request' { $before=& $module {$script:catalogCalls};$r=Refresh;Assert ($r.reason -eq 'cooldown');Assert ((& $module {$script:catalogCalls}) -eq $before) }
Check 'conditional ETag 304 reuses the last catalog' { Expire;& $module {$script:catalogMode='304'};$r=Refresh;Assert $r.available;Assert (-not $r.updated);Assert ((& $module {$script:seenEtag}) -eq 'fixture-etag');Assert ($r.count -eq 25) }
Check 'network failure retains known catalog and applies cooldown' { Expire;& $module {$script:catalogMode='fail'};$r=Refresh;Assert $r.available;Assert ($r.reason -eq 'failed');Assert ($r.nextCheckAt -gt [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) }
Check 'three metadata attempts exhaust quota across independent contexts' { Expire;$context=@{CancelPath=(Join-Path $ValidationRoot 'other-cancel');Deadline=[DateTime]::UtcNow.AddMinutes(5)};$before=& $module {$script:catalogCalls};$r=Refresh;Assert ($r.reason -eq 'metadata-budget');Assert ((& $module {$script:catalogCalls}) -eq $before) }
Check 'incomplete persisted quota fails closed instead of resetting allowance' { $p=Join-Path $library 'ymcc-data-state.json';$original=[IO.File]::ReadAllText($p);Json $p @{schema=1};try{Reject {Refresh}}finally{[IO.File]::WriteAllText($p,$original,[Text.Encoding]::UTF8)} }
$steamapps=Join-Path $ValidationRoot 'SteamLibrary/steamapps'
$exe=Join-Path $steamapps 'common/Victoria3/bin/Game.exe'
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($exe))|Out-Null
[IO.File]::WriteAllText($exe,'game fixture')
$manifest=Join-Path $steamapps 'appmanifest_529340.acf'
$acf='"AppState"' + "`n{`n" + '"appid" "529340"' + "`n" + '"name" "Victoria 3"' + "`n" + '"installdir" "Victoria3"' + "`n}"
[IO.File]::WriteAllText($manifest,$acf)
Check 'generic Game.exe resolves via Steam AppID to canonical GCM title' { $r=Resolve-TrainerIdentity 'Generic Game' $exe $db @{games=@{}};Assert ($r.method -eq 'steam-appid');Assert ($r.steamAppId -eq '529340');Assert ('Victoria 3' -in $r.aliases) }
Check 'manifest filename must agree with embedded AppID' { [IO.File]::WriteAllText($manifest,$acf.Replace('"appid" "529340"','"appid" "999"'));try{Assert ($null -eq (Get-TrainerSteamIdentity $exe))}finally{[IO.File]::WriteAllText($manifest,$acf)} }
Check 'two manifests for one directory do not choose an arbitrary game' { $extra=Join-Path $steamapps 'appmanifest_999.acf';[IO.File]::WriteAllText($extra,$acf.Replace('529340','999'));try{Assert ($null -eq (Get-TrainerSteamIdentity $exe))}finally{[IO.File]::Delete($extra)} }
Check 'conflicting translations for one AppID reject automatic selection' { $p=Join-Path $db 'translations.json';$original=[IO.File]::ReadAllText($p);Json $p @(@{en_US='Victoria 3 Ultimate';steam_id='529340'},@{en_US='Other';steam_id='529340'});try{Reject {Resolve-TrainerIdentity 'Game' $exe $db @{games=@{}}}}finally{[IO.File]::WriteAllText($p,$original,[Text.Encoding]::UTF8)} }
$folder=Join-Path $library 'fixture'
$trainer=Join-Path $folder 'nested/Trainer.exe'
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($trainer))|Out-Null
$fixtureBytes=New-Object byte[] 128;$fixtureBytes[0]=0x4d;$fixtureBytes[1]=0x5a;$fixtureBytes[60]=80;$fixtureBytes[80]=0x50;$fixtureBytes[81]=0x45
[IO.File]::WriteAllBytes($trainer,$fixtureBytes)
[IO.File]::WriteAllBytes((Join-Path $folder 'Other.exe'),$fixtureBytes)
$metadata=Join-Path $folder 'gcm_info.json'
Json $metadata @{game_name='Victoria 3';extension='nested/Trainer.exe';origin='fling_main'}
Check 'GCM exact nested launch filename wins over multiple executables' { $r=Find-LocalTrainer @($library) @('Victoria 3');Assert ($r.path -eq $trainer) }
Check 'folder-only GCM entries never become an executable automatically' { Json $metadata @{game_name='Victoria 3';extension='none'};Assert ($null -eq (Find-LocalTrainer @($library) @('Victoria 3'))) }
Check 'GCM metadata traversal cannot select files outside the trainer folder' { Json $metadata @{game_name='Victoria 3';extension='../other.exe'};Assert ($null -eq (Find-LocalTrainer @($library) @('Victoria 3'))) }
$sha=(Get-FileHash -LiteralPath $trainer -Algorithm SHA256).Hash
Json $metadata @{game_name='Victoria 3';extension='nested/Trainer.exe';origin='fling_main'}
Check 'EXE launch plan honors GCM admin preference and working directory' { $p=Resolve-TrainerLaunchPlan -Mode Trainer -TrainerPath $trainer -TrainerFolder $folder -ExpectedSha256 $sha -GcmSettingsRoot $settings;$p|Out-Null;Assert ($p.verb -eq 'runas');Assert ($p.workingDirectory -eq (Split-Path $trainer));Assert (-not $p.safePath) }
Check 'changed trainer hash is refused before launch plan is created' { Reject {Resolve-TrainerLaunchPlan -Mode Trainer -TrainerPath $trainer -TrainerFolder $folder -ExpectedSha256 ('a'*64) -GcmSettingsRoot $settings} }
$ce=Join-Path $ValidationRoot 'Cheat Engine/Cheat Engine.exe'
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($ce))|Out-Null
[IO.File]::WriteAllText($ce,'CE fixture; never execute')
$table=Join-Path $folder 'Table.ct';[IO.File]::WriteAllText($table,'<CheatTable/>')
Json $metadata @{game_name='Victoria 3';extension='Table.ct';origin='ct_other'}
Check 'CT files use configured Cheat Engine and its separate elevation setting' { $p=Resolve-TrainerLaunchPlan -Mode Trainer -TrainerPath $table -TrainerFolder $folder -ExpectedSha256 (Get-FileHash -LiteralPath $table -Algorithm SHA256).Hash -GcmSettingsRoot $settings;Assert ($p.program -eq $ce);Assert ($p.arguments -eq ('"'+$table+'"'));Assert ($p.verb -eq 'open');Assert ($p.workingDirectory -eq (Split-Path $ce)) }
Check 'emulator action uses configured Cheat Engine without fixed installation path' { $p=Resolve-TrainerLaunchPlan -Mode CheatEngine -GcmSettingsRoot $settings;Assert ($p.program -eq $ce);Assert (-not $p.arguments) }
Check 'manager launch reuses matching process via focus instead of second singleton' { $result=& $module { function Find-TrainerRunningProcess($Program){return @{Id=42}};function Show-TrainerProcess($Process){if($Process.Id -ne 42){throw 'wrong pid'}};Start-TrainerLaunchPlan @{arguments='';program='fixture';mode='Manager';safePath=$false} };Assert $result.reused;Assert ($result.pid -eq 42) }
Json (Join-Path $ValidationRoot 'result.json') @($checks.ToArray())
$failed=@($checks|Where-Object{-not $_.ok})
Write-Host ('Trainer reliability: '+($checks.Count-$failed.Count)+'/'+$checks.Count+' passed; no real trainer was executed.')
if($failed.Count){exit 1}
