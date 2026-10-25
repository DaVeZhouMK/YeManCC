[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$BackendPath='',[switch]$ExecuteInert)
$ErrorActionPreference='Stop'
if(-not $BackendPath){$BackendPath=Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1'}
Import-Module $BackendPath -Force -DisableNameChecking
$module=Get-Module TrainerBackend
$root=Join-Path $ValidationRoot ('paths-'+[guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Directory]::CreateDirectory($root)|Out-Null
$checks=[Collections.Generic.List[object]]::new()
function Assert($value,[string]$message='assertion failed'){if(-not $value){throw $message}}
function Check([string]$name,[scriptblock]$body){try{& $body;$checks.Add(@{name=$name;ok=$true});Write-Output ('PASS '+$name)}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Output ('FAIL '+$name+': '+$_.Exception.Message)}}
function Reject([scriptblock]$body,[string]$code=''){try{& $body|Out-Null}catch{if($code){Assert ($_.Exception.Data['trainerCode'] -eq $code) ('Wrong rejection: '+$_.Exception.Message)};return};throw 'Expected rejection'}
function Json([string]$file,$value){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))|Out-Null;[IO.File]::WriteAllText($file,($value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
function File([string]$relative,[string]$text=''){ $p=Join-Path $root $relative;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null;if($text){[IO.File]::WriteAllText($p,$text)}else{[IO.File]::Copy((Join-Path $env:WINDIR 'System32/notepad.exe'),$p,$true)};return $p }
$settings=Join-Path $root 'settings';$settingsFile=Join-Path $settings 'settings.json';$library=Join-Path $root '自定义 GCM 库 [外置盘]';$old=Join-Path $root '旧盘/旧库/[风灵]《十字军之王3》修改器';$oldPath=Join-Path $old 'Trainer.exe'
$trainer=File '自定义 GCM 库 [外置盘]/[风灵]《十字军之王3》修改器/nested/CK3 Special.exe';$folder=Split-Path (Split-Path $trainer)
$sha=(Get-FileHash -LiteralPath $trainer).Hash
$meta=@{game_name='Crusader Kings III';origin='fling_main';gcm_url='GCM/Fling/CK3.exe';extension='nested/CK3 Special.exe'}
Json (Join-Path $folder 'gcm_info.json') $meta
$ce=File '独立工具 [CE]/renamed-ce.exe'
$config=@{downloadPath=$library;safePath=$false;launchAsAdmin=$false;launchCTAsAdmin=$false;cePath=$ce}
Json $settingsFile $config
function Plan([string]$path=$oldPath,[string]$directory=$old,[string]$hash=$sha,[string]$name='Crusader Kings III',[string]$id='GCM/Fling/CK3.exe') {Resolve-TrainerLaunchPlan -Mode Trainer -TrainerPath $path -TrainerFolder $directory -ExpectedSha256 $hash -TrainerGameName $name -TrainerOrigin 'fling_main' -GcmResourceId $id -GcmSettingsRoot $settings}
Check 'library relocation resolves current GCM root and nested selected filename' {$plan=Plan;Assert ($plan.program -eq $trainer);Assert ($plan.workingDirectory -eq (Split-Path $trainer))}
Check 'old absolute path still existing cannot override current library' {[IO.Directory]::CreateDirectory($old)|Out-Null;[IO.File]::Copy($trainer,$oldPath);Assert ((Plan).program -eq $trainer)}
Check 'GCM folder rename resolves by resource identity and original hash' {$renamed=Join-Path $library '用户改名目录';[IO.Directory]::Move($folder,$renamed);$script:folder=$renamed;$script:trainer=Join-Path $renamed 'nested/CK3 Special.exe';Assert ((Plan).program -eq $trainer)}
Check 'GCM display label rename preserves stable resource ID' {$meta.game_name='我的 CK3';Json (Join-Path $folder 'gcm_info.json') $meta;Assert ((Plan).program -eq $trainer);$meta.game_name='Crusader Kings III';Json (Join-Path $folder 'gcm_info.json') $meta}
Check 'conflicting GCM ID is rejected even with matching name and bytes' {Reject {Plan -id 'GCM/Fling/Other.exe'} 'trainer-unavailable'}
Check 'conflicting game label cannot borrow a matching executable hash without an ID' {Reject {Plan -name 'Victoria 3' -id ''} 'trainer-unavailable'}
Check 'changed binary is rejected without downloading again' {Reject {Plan -hash ('b'*64)} 'trainer-changed'}
Check 'GCM explicit folder-only selection never launches the cached executable' {$meta.extension='none';Json (Join-Path $folder 'gcm_info.json') $meta;try{Reject {Plan} 'trainer-unavailable'}finally{$meta.extension='nested/CK3 Special.exe';Json (Join-Path $folder 'gcm_info.json') $meta}}
Check 'GCM selected updated executable requires renewed readiness' {$other=File '自定义 GCM 库 [外置盘]/用户改名目录/New.exe' 'different binary';$meta.extension='New.exe';Json (Join-Path $folder 'gcm_info.json') $meta;try{Reject {Plan} 'trainer-changed'}finally{$meta.extension='nested/CK3 Special.exe';Json (Join-Path $folder 'gcm_info.json') $meta}}
Check 'GCM traversal metadata is rejected' {$meta.extension='../escape.exe';Json (Join-Path $folder 'gcm_info.json') $meta;try{Reject {Plan} 'trainer-unavailable'}finally{$meta.extension='nested/CK3 Special.exe';Json (Join-Path $folder 'gcm_info.json') $meta}}
Check 'duplicate matching resources are reported as ambiguous' {$duplicate=Join-Path $library 'duplicate';[IO.Directory]::CreateDirectory((Join-Path $duplicate 'nested'))|Out-Null;[IO.File]::Copy($trainer,(Join-Path $duplicate 'nested/CK3 Special.exe'));Json (Join-Path $duplicate 'gcm_info.json') $meta;try{Reject {Plan} 'trainer-ambiguous'}finally{$metaCopy=@{}+$meta;$metaCopy.gcm_url='Other';Json (Join-Path $duplicate 'gcm_info.json') $metaCopy}}
Check 'CE accepts explicit executable selection with a nonstandard filename' {$plan=Resolve-TrainerLaunchPlan -Mode CheatEngine -GcmSettingsRoot $settings;Assert ($plan.program -eq $ce);Assert (-not $plan.arguments)}
Check 'CE still accepts GCM directory configuration' {$ceDir=Split-Path $ce;$standard=Join-Path $ceDir 'Cheat Engine.exe';[IO.File]::Copy($ce,$standard);$config.cePath=$ceDir;Json $settingsFile $config;Assert ((Resolve-TrainerLaunchPlan -Mode CheatEngine -GcmSettingsRoot $settings).program -eq $standard);[IO.File]::Delete($standard);$config.cePath=$ce;Json $settingsFile $config}
Check 'versioned CE installation resolves native architecture entry without a loader' {$ceDir=Split-Path $ce;$native=Join-Path $ceDir $(if([Environment]::Is64BitOperatingSystem){'cheatengine-x86_64.exe'}else{'cheatengine-i386.exe'});[IO.File]::Copy($ce,$native);$config.cePath=$ceDir;Json $settingsFile $config;Assert ((Resolve-TrainerLaunchPlan -Mode CheatEngine -GcmSettingsRoot $settings).program -eq $native);$config.cePath=$ce;Json $settingsFile $config}
Check 'missing configured CE gives a configuration error' {$config.cePath=Join-Path $root 'missing.exe';Json $settingsFile $config;try{Reject {Resolve-TrainerLaunchPlan -Mode CheatEngine -GcmSettingsRoot $settings}}finally{$config.cePath=$ce;Json $settingsFile $config}}
Check 'manager honors explicit portable path without a fixed drive' {$manager=File 'portable-tools/My Manager.exe';$plan=Resolve-TrainerLaunchPlan -Mode Manager -ManagerPath $manager -GcmSettingsRoot $settings;Assert ($plan.program -eq $manager)}
Check 'adjacent manager resolves or refuses ambiguous installed copies' {$manager=File 'portable-suite/Game Cheats Manager/Game Cheats Manager.exe';$appDir=Join-Path $root 'portable-suite/YMCC';[IO.Directory]::CreateDirectory($appDir)|Out-Null;try{$plan=Resolve-TrainerLaunchPlan -Mode Manager -AppDirectory $appDir -GcmSettingsRoot $settings;Assert ($plan.program -eq $manager)}catch{if($_.Exception.Message -notlike '*找到多个 Game Cheats Manager*'){throw};$plan=Resolve-TrainerLaunchPlan -Mode Manager -ManagerPath $manager -AppDirectory $appDir -GcmSettingsRoot $settings;Assert ($plan.program -eq $manager)}}
Check 'invalid explicit manager path never falls back to another install' {Reject {Resolve-TrainerLaunchPlan -Mode Manager -ManagerPath (Join-Path $root 'missing-manager.exe') -GcmSettingsRoot $settings}}
$table=File '自定义 GCM 库 [外置盘]/用户改名目录/表格.ct' '<CheatTable/>'; $tableMeta=@{}+$meta;$tableMeta.extension='表格.ct';$tableMeta.gcm_url='GCM/CT/CK3.ct';Json (Join-Path $folder 'gcm_info.json') $tableMeta
Check 'moved CT entry uses the configured CE executable and exact table argument' {$plan=Plan -path (Join-Path $old 'Old.ct') -hash (Get-FileHash -LiteralPath $table).Hash -id 'GCM/CT/CK3.ct';Assert ($plan.program -eq $ce);Assert ($plan.arguments -eq ('"'+$table+'"'));Assert ($plan.workingDirectory -eq (Split-Path $ce))}
Check 'failed launch records error code and attempted path in durable receipt' {$cache=Join-Path $root 'cache';$requestId='path-error-123456';$result=Join-Path $cache ('requests/'+$requestId+'.json');Invoke-TrainerLaunch -Mode Trainer -TrainerPath $oldPath -TrainerFolder $old -ExpectedSha256 ('c'*64) -CacheRoot $cache -ResultPath $result -RequestId $requestId -GcmSettingsRoot $settings -TrainerGameName 'Crusader Kings III' -TrainerOrigin 'fling_main' -GcmResourceId 'GCM/CT/CK3.ct';$receipt=Get-Content -LiteralPath $result -Raw -Encoding UTF8|ConvertFrom-Json;Assert (-not $receipt.ok);Assert ($receipt.code -eq 'trainer-changed');Assert ([IO.File]::Exists((Join-Path $cache 'last-launch-status.json')))}
if($ExecuteInert){
 Check 'real OS launch follows moved GCM entry and working directory using an inert fixture' {
  $marker=Join-Path $root 'inert-launched.txt';$source=Join-Path $root 'Inert.cs';$binary=Join-Path $root 'Inert.exe'
  [IO.File]::WriteAllText($source,('using System;using System.IO;class Inert{static void Main(){File.WriteAllText(@"'+$marker.Replace('"','""')+'",Environment.CurrentDirectory);}}'),[Text.UTF8Encoding]::new($true))
  & (Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe') /nologo /target:winexe ('/out:'+$binary) $source
  Assert ($LASTEXITCODE -eq 0) 'Inert compilation failed'
  [IO.File]::Copy($binary,$trainer,$true);Json (Join-Path $folder 'gcm_info.json') $meta
  $plan=Plan -hash (Get-FileHash -LiteralPath $trainer).Hash
  $result=& $module {param($p)Start-TrainerLaunchPlan $p} $plan
  $until=[DateTime]::UtcNow.AddSeconds(8);while(-not [IO.File]::Exists($marker) -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 40}
  Assert ([IO.File]::Exists($marker)) 'No OS launch marker';Assert ([IO.File]::ReadAllText($marker) -eq (Split-Path $trainer));Assert ($result.pid -gt 0)
 }
}

$repairRoot=Join-Path $root 'gcm-ce-repair';$repairFile=Join-Path $repairRoot 'settings.json'
$repairDirectory=Join-Path $root 'Existing CE';[IO.Directory]::CreateDirectory($repairDirectory)|Out-Null
$repairProgram=Join-Path $repairDirectory 'Cheat Engine.exe';[IO.File]::Copy($ce,$repairProgram)
Check 'missing CE address is written to GCM settings and its existing fields are preserved' {
 Json $repairFile @{downloadPath=$library;cePath='';safePath=$false;launchCTAsAdmin=$false;language='fixture';budget=@{daily=3}}
 $repaired=& $module {param($r,$d)Repair-TrainerGcmCePath -SettingsRoot $r -DefaultDirectory $d} $repairRoot $repairDirectory
 $saved=Get-Content -LiteralPath $repairFile -Raw -Encoding UTF8|ConvertFrom-Json
 Assert ($repaired.cePath -eq $repairDirectory);Assert ($saved.cePath -eq $repairDirectory);Assert ($saved.downloadPath -eq $library);Assert ($saved.language -eq 'fixture');Assert ($saved.budget.daily -eq 3)
 Assert ((Resolve-TrainerLaunchPlan -Mode CheatEngine -GcmSettingsRoot $repairRoot).program -eq $repairProgram)
}
Check 'an existing explicit CE path is not replaced or rewritten' {
 Json $repairFile @{cePath=$ce;safePath=$false};$before=[IO.File]::ReadAllText($repairFile);$stamp=[IO.File]::GetLastWriteTimeUtc($repairFile)
 $null=& $module {param($r,$d)Repair-TrainerGcmCePath -SettingsRoot $r -DefaultDirectory $d} $repairRoot $repairDirectory
 Assert ([IO.File]::ReadAllText($repairFile) -ceq $before);Assert ([IO.File]::GetLastWriteTimeUtc($repairFile) -eq $stamp)
}
Check 'an absent CE installation leaves the GCM address unconfigured' {
 Json $repairFile @{cePath='';downloadPath=$library};$before=[IO.File]::ReadAllText($repairFile)
 $null=& $module {param($r,$d)Repair-TrainerGcmCePath -SettingsRoot $r -DefaultDirectory $d} $repairRoot (Join-Path $root 'CE not installed')
 Assert ([IO.File]::ReadAllText($repairFile) -ceq $before)
}
Check 'a missing CE field is repaired but malformed GCM JSON is never overwritten' {
 Json $repairFile @{safePath=$false;downloadPath=$library}
 $null=& $module {param($r,$d)Repair-TrainerGcmCePath -SettingsRoot $r -DefaultDirectory $d} $repairRoot $repairDirectory
 Assert (((Get-Content -LiteralPath $repairFile -Raw -Encoding UTF8|ConvertFrom-Json).cePath) -eq $repairDirectory)
 [IO.File]::WriteAllText($repairFile,'{broken-json')
 Reject {& $module {param($r,$d)Repair-TrainerGcmCePath -SettingsRoot $r -DefaultDirectory $d} $repairRoot $repairDirectory}
 Assert ([IO.File]::ReadAllText($repairFile) -ceq '{broken-json')
}

Json (Join-Path $root 'result.json') @($checks.ToArray())
$failed=@($checks|Where-Object{-not $_.ok});Write-Output ('Trainer launch paths: '+($checks.Count-$failed.Count)+'/'+$checks.Count+'; '+$root)
if($failed.Count){exit 1}