[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
Import-Module (Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
$module=Get-Module TrainerBackend
$checks=[Collections.Generic.List[object]]::new()
function Assert($v,[string]$why='assertion failed'){if(-not $v){throw $why}}
function Reject([scriptblock]$fn){$bad=$false;try{& $fn|Out-Null}catch{$bad=$true};Assert $bad 'expected rejection'}
function Json([string]$p,$v){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null;[IO.File]::WriteAllText($p,(ConvertTo-Json -InputObject $v -Depth 12),[Text.Encoding]::UTF8)}
function SaveFixture {
 Json (Join-Path $script:c.root 'state/library-scan.json') $script:c.scan
 Json (Join-Path $script:c.root 'state/steam-add-plan.json') $script:c.plan
 Json (Join-Path $script:c.root 'config/manual-overrides.json') $script:c.overrides
 Json (Join-Path $script:c.db 'translations.json') $script:c.translations
}
function NewFixture {
 $base=Join-Path $ValidationRoot ('case-'+$checks.Count)
 $exe=Join-Path $base 'Game/Game.exe';[IO.Directory]::CreateDirectory((Split-Path $exe))|Out-Null;[IO.File]::WriteAllText($exe,'game-fixture')
 $root=Join-Path $base 'custom';$db=Join-Path $base 'gcm/db'
 $item=@{primaryExecutable=$exe;identityStatus='verified';steamVerificationStatus='steam-verified';resolverVersion='steam-official-fuzzy-global-v4';steamStoreAppId=12345;storefrontAppId=12345;formalName='Alpha III';contentRelation='canonical-base-game';baseGameAppId=12345;shortAppId=3456789012L;longAppId='14845741365024165888'}
 $game=@{primaryExecutable=$exe;gameDirectory=(Split-Path $exe);contentType='game';primaryRole='main-game';status='ready';scanComplete=$true;candidates=@(@{path=$exe;size=(Get-Item -LiteralPath $exe).Length})}
 $script:c=@{base=$base;exe=$exe;root=$root;db=$db;item=$item;game=$game;plan=@{schemaVersion=1;dataRoot=$root;libraryState=(Join-Path $root 'state/library-scan.json');items=@($item)};scan=@{schemaVersion=1;games=@($game)};overrides=@{schemaVersion=1;items=@{}};translations=@(@{en_US='Alpha III';zh_CN='阿尔法3';steam_id=12345},@{en_US='Beta 3';zh_CN='贝塔3';steam_id=23456})}
 SaveFixture
}
function Resolve([string]$name='Arbitrary Window Title'){Resolve-TrainerIdentity $name $script:c.exe $script:c.db @{games=@{}} $script:c.root}
function Check([string]$name,[scriptblock]$body){try{NewFixture;& $body;$checks.Add(@{name=$name;ok=$true});Write-Host "PASS $name"}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Host "FAIL $name : $_";Write-Host $_.ScriptStackTrace}}
Check 'confirmed exact executable resolves display name and generic stem consistently' { $a=Resolve 'Custom Nickname';$b=Resolve 'Game';Assert ($a.method -eq 'custom-library-appid');Assert ($a.steamAppId -eq '12345');Assert (($a.aliases -join '|') -eq ($b.aliases -join '|'));Assert ('Alpha III' -in $a.aliases) }
Check 'verified canonical title selects only its exact same-ID edition' {
 $c.translations+=@{en_US='Alpha III Remastered';zh_CN='阿尔法3重制';steam_id=12345};SaveFixture
 $r=Resolve;Assert ('Alpha III' -in $r.aliases);Assert ('Alpha III Remastered' -notin $r.aliases);Assert ('阿尔法3重制' -notin $r.aliases)
}
Check 'same-ID variants without exact canonical title remain ambiguous' {
 $c.item.formalName='Alpha III Deluxe';$c.translations+=@{en_US='Alpha III Remastered';zh_CN='阿尔法3重制';steam_id=12345};SaveFixture;Reject {Resolve}
}
Check 'duplicate exact rows in a multi-name ID remain ambiguous' {
 $c.translations+=@{en_US='Alpha III';zh_CN='不同中文';steam_id=12345},@{en_US='Alpha III Remastered';steam_id=12345};SaveFixture;Reject {Resolve}
}
Check 'shortcut 32-bit and 64-bit IDs never become store IDs' { $c.item.Remove('steamStoreAppId');$c.item.Remove('storefrontAppId');SaveFixture;Reject {Resolve} }
Check 'case and slash changes preserve exact path ownership' { $c.item.primaryExecutable=$c.exe.ToUpperInvariant().Replace('\','/');SaveFixture;Assert ((Resolve).method -eq 'custom-library-appid') }
Check 'same basename in another directory does not borrow identity' { $c.item.primaryExecutable=Join-Path $c.base 'Other/Game.exe';SaveFixture;Assert ((Resolve).method -eq 'exact-title') }
Check 'directory-only plan match cannot overwrite an executable identity' { $c.item.primaryExecutable='';$c.item.gameDirectory=Split-Path $c.exe;SaveFixture;Assert ((Resolve).method -eq 'exact-title') }
Check 'different data-root plan is never trusted' { $c.plan.dataRoot=Join-Path $c.base 'foreign';SaveFixture;Assert ((Resolve).method -eq 'exact-title') }
Check 'cleared ID suppresses old verified identity' { $c.overrides.items[$c.exe.ToLowerInvariant()]=@{idCleared=$true};SaveFixture;Assert ((Resolve).method -eq 'exact-title') }
Check 'pending manual ID cannot replace the verified store ID silently' { $c.overrides.items[$c.exe.ToLowerInvariant()]=@{steamId=23456;identityStatus='pending-network'};SaveFixture;Reject {Resolve} }
Check 'pending identity is not accepted even if numeric IDs exist' { $c.item.identityStatus='pending-network';SaveFixture;Assert ((Resolve).method -eq 'exact-title') }
Check 'unknown resolver schema does not manufacture a verified identity' { $c.item.resolverVersion='future-v9';SaveFixture;Assert ((Resolve).method -eq 'exact-title') }
Check 'conflicting store IDs block automatic selection' { $c.item.storefrontAppId=23456;SaveFixture;Reject {Resolve} }
Check 'malformed second store ID is not silently ignored' { $c.item.storefrontAppId='invalid';SaveFixture;Reject {Resolve} }
Check 'DLC or expansion identity is not treated as a confirmed base game' { $c.item.matchedContentAppId=34567;$c.item.contentRelation='dlc';SaveFixture;Reject {Resolve} }
Check 'multiple primary executable rows are refused' { $c.scan.games=@($c.game,$c.game);SaveFixture;Reject {Resolve} }
Check 'multiple plans for the same executable are refused' { $c.plan.items=@($c.item,$c.item);SaveFixture;Reject {Resolve} }
Check 'known secondary helper is not treated as the game' { $c.game.primaryExecutable=Join-Path $c.base 'Game/Real.exe';SaveFixture;Reject {Resolve} }
Check 'non-game classification blocks identity borrowing' { $c.game.contentType='non-game';SaveFixture;Reject {Resolve} }
Check 'changed executable size drops stale library evidence' { [IO.File]::AppendAllText($c.exe,'updated');Assert ((Resolve).method -eq 'exact-title') }
Check 'half-written optional plan contributes no trusted game ID' { [IO.File]::WriteAllText((Join-Path $c.root 'state/steam-add-plan.json'),'{');Assert ((Resolve).method -eq 'exact-title') }
Check 'GCM same name with a different Steam ID fails closed' { $c.translations=@(@{en_US='Alpha III';steam_id=23456});SaveFixture;Reject {Resolve} }
Check 'GCM name aliases cannot escape the confirmed Steam ID' { $c.translations+=@{en_US='Alpha III';zh_CN='Wrong game';steam_id=99999};SaveFixture;Assert ('Wrong game' -notin (Resolve).aliases) }
Check 'real catalog singleton merge preserves resource ID and public URL' {
 Json (Join-Path $c.db 'fling_main.json') @(@{game_name='Alpha III';gcm_url='GCM/Alpha';official_url='https://flingtrainer.com/trainer/old/';version='1'})
 $lib=Join-Path $c.base 'library';Json (Join-Path $lib 'ymcc-fling-public.json') @{schema=1;rows=@(@{game_name='Alpha III';official_url='https://flingtrainer.com/trainer/alpha-iii-trainer/'})}
 $rows=@(& $module {param($db,$lib) Get-FlingCatalogRows $db $lib} $c.db $lib)
 Assert ($rows.Count -eq 1);Assert ($rows[0].gcm_url -eq 'GCM/Alpha');Assert ($rows[0].official_url.EndsWith('alpha-iii-trainer/'))
}
Check 'catalog content changes invalidate memory cache even at identical size and timestamp' {
 $a=Resolve;$p=Join-Path $c.db 'translations.json';$stamp=(Get-Item -LiteralPath $p).LastWriteTimeUtc
 $c.translations[0].en_US='Alpha IV!';SaveFixture;(Get-Item -LiteralPath $p).LastWriteTimeUtc=$stamp
 Assert ('Alpha IV!' -in (Resolve).aliases)
}
Check 'full background preparation uses confirmed library ID and local GCM file with zero network' {
 $settings=Join-Path $c.base 'gcm';$lib=Join-Path $c.base 'library';$folder=Join-Path $lib 'Alpha';$trainer=Join-Path $folder 'Trainer.exe'
 Json (Join-Path $settings 'settings.json') @{downloadPath=$lib}
 Json (Join-Path $folder 'gcm_info.json') @{game_name='Alpha III';origin='fling_main';extension='Trainer.exe'}
 $trainerFixtureBytes=New-Object byte[] 128;$trainerFixtureBytes[0]=0x4d;$trainerFixtureBytes[1]=0x5a;$trainerFixtureBytes[60]=80;$trainerFixtureBytes[80]=0x50;$trainerFixtureBytes[81]=0x45;[IO.File]::WriteAllBytes($trainer,$trainerFixtureBytes)
 & $module { $script:testNetworkCalls=0;function script:Get-TrainerResponse($Context,$Url,$Destination='',$Referer=''){$script:testNetworkCalls++;throw 'Unexpected network'} }
 $cache=Join-Path $c.base 'cache';$id='library-prepare-test';$result=Join-Path $cache ('requests/'+$id+'.json')
 Invoke-TrainerPrepare -GameName 'Window title Beta 3' -GameExecutablePath $c.exe -CacheRoot $cache -ResultPath $result -RequestId $id -GcmSettingsRoot $settings -CustomLibraryRoot $c.root
 $r=[IO.File]::ReadAllText($result,[Text.Encoding]::UTF8)|ConvertFrom-Json
 Assert ($r.state -eq 'ready') $r.message;Assert ($r.trainerPath -eq $trainer);Assert ($r.identity.method -eq 'custom-library-appid');Assert ((& $module {$script:testNetworkCalls}) -eq 0)
}
Check 'identity conflict never reaches the download budget or HTTP worker' {
 $settings=Join-Path $c.base 'gcm';$lib=Join-Path $c.base 'library';Json (Join-Path $settings 'settings.json') @{downloadPath=$lib}
 $c.item.storefrontAppId=23456;SaveFixture
 & $module { $script:testNetworkCalls=0 }
 $cache=Join-Path $c.base 'cache';$id='library-conflict-test';$result=Join-Path $cache ('requests/'+$id+'.json')
 Invoke-TrainerPrepare -GameName 'Alpha III' -GameExecutablePath $c.exe -CacheRoot $cache -ResultPath $result -RequestId $id -GcmSettingsRoot $settings -CustomLibraryRoot $c.root
 $r=[IO.File]::ReadAllText($result,[Text.Encoding]::UTF8)|ConvertFrom-Json
 Assert ($r.state -eq 'failed');Assert ((& $module {$script:testNetworkCalls}) -eq 0);Assert (-not [IO.File]::Exists((Join-Path $lib 'ymcc-library.json')))
}
Check 'library-owned launcher and sibling appid bind legacy shortcut names without fuzzy matching' {
 $c.item.identityStatus='legacy';SaveFixture
 [IO.File]::WriteAllText((Join-Path (Split-Path $c.exe) 'steam_appid.txt'),'12345')
 Json (Join-Path (Split-Path $c.exe) 'launcher-settings.json') @{distPlatform='steam';exePath='./Game.exe';displayName='Alpha III';browserDlcUrl='https://store.steampowered.com/dlc/12345/'}
 $r=Resolve 'HOI4';Assert ($r.method -eq 'custom-library-install-metadata');Assert ($r.steamAppId -eq '12345');Assert ('Alpha III' -in $r.aliases)
}
Check 'standalone steam_appid without exact launcher evidence is not trusted' {
 $c.item.identityStatus='legacy';SaveFixture
 [IO.File]::WriteAllText((Join-Path (Split-Path $c.exe) 'steam_appid.txt'),'12345')
 Assert ((Resolve).method -eq 'exact-title')
}
Check 'launcher targeting another EXE cannot lend its AppID' {
 $c.item.identityStatus='legacy';SaveFixture
 [IO.File]::WriteAllText((Join-Path (Split-Path $c.exe) 'steam_appid.txt'),'12345')
 Json (Join-Path (Split-Path $c.exe) 'launcher-settings.json') @{distPlatform='steam';exePath='./Other.exe';displayName='Alpha III';browserDlcUrl='https://store.steampowered.com/dlc/12345/'}
 Assert ((Resolve).method -eq 'exact-title')
}
Check 'conflicting install metadata IDs do not reach title fallback' {
 $c.item.identityStatus='legacy';SaveFixture
 [IO.File]::WriteAllText((Join-Path (Split-Path $c.exe) 'steam_appid.txt'),'12345')
 Json (Join-Path (Split-Path $c.exe) 'launcher-settings.json') @{distPlatform='steam';exePath='./Game.exe';displayName='Alpha III';browserDlcUrl='https://store.steampowered.com/dlc/23456/'}
 Reject {Resolve}
}
Check 'deleted executable is blocked even if its title matches a catalog' {
 [IO.File]::Delete($c.exe);Reject {Resolve 'Alpha III'}
}
Json (Join-Path $ValidationRoot 'result.json') $checks.ToArray()
$bad=@($checks|Where-Object{-not $_.ok});Write-Host ('Library integration: '+($checks.Count-$bad.Count)+'/'+$checks.Count+' passed; no real games or trainers launched.')
if($bad.Count){exit 1}
