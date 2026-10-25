# Read-only adapter for the custom library's confirmed executable-to-store identity.
function Get-TrainerPathKey([string]$Path) {
    if (-not $Path -or -not [IO.Path]::IsPathRooted($Path)) { return '' }
    try { return [IO.Path]::GetFullPath($Path.Replace('/','\')).TrimEnd('\').ToLowerInvariant() } catch { return '' }
}
function Get-TrainerStoreAppId($Value) {
    $text=[string]$Value
    if($text -notmatch '^[1-9][0-9]{0,9}$'){return ''}
    $number=0L
    if(-not [long]::TryParse($text,[ref]$number) -or $number -gt 2147483647L){return ''}
    return $text
}
function Get-TrainerCustomLibraryIdentity([string]$ExecutablePath,[string]$Root) {
    $key=Get-TrainerPathKey $ExecutablePath
    if(-not $key -or -not $Root -or -not [IO.File]::Exists($ExecutablePath)){return $null}
    $scanPath=Join-Path $Root 'state/library-scan.json'
    try {
        Assert-TrainerNotReparse $scanPath
        $scan=Read-TrainerJson $scanPath 16777216
        if(-not $scan -or (Get-TrainerProperty $scan 'schemaVersion' 0) -ne 1 -or $scan.games -isnot [array]){return $null}
        $games=@($scan.games|Where-Object{(Get-TrainerPathKey ([string](Get-TrainerProperty $_ 'primaryExecutable' ''))) -eq $key})
    } catch {return $null}
    if($games.Count -gt 1){throw '自定义游戏库有多个相同 EXE 条目，暂不自动选择修改器'}
    if(-not $games.Count){
        # A known helper/unselected executable must not borrow a game's confirmed ID.
        foreach($game in $scan.games){foreach($candidate in @(Get-TrainerProperty $game 'candidates' @())){
            if((Get-TrainerPathKey ([string](Get-TrainerProperty $candidate 'path' ''))) -eq $key){throw '该 EXE 是游戏库中的辅助或未选程序，未自动准备修改器'}
        }}
        return $null
    }
    $game=$games[0]
    if((Get-TrainerProperty $game 'contentType' '') -notin @('game','steam-native') -or
       (Get-TrainerProperty $game 'primaryRole' 'main-game') -ne 'main-game' -or
       (Get-TrainerProperty $game 'unrecognizedTool' $false)){throw '该游戏库条目不是游戏主程序，未自动准备修改器'}
    if((Get-TrainerProperty $game 'status' '') -ne 'ready' -or -not (Get-TrainerProperty $game 'scanComplete' $false)){return $null}
    try {Assert-TrainerNotReparse $ExecutablePath}catch{return $null}
    $primary=@(Get-TrainerProperty $game 'candidates' @()|Where-Object{(Get-TrainerPathKey ([string](Get-TrainerProperty $_ 'path' ''))) -eq $key})
    if($primary.Count -eq 1){
        $size=[long](Get-TrainerProperty $primary[0] 'size' 0)
        if($size -gt 0 -and (Get-Item -LiteralPath $ExecutablePath).Length -ne $size){return $null}
    }
    $override=$null
    $overridePath=Join-Path $Root 'config/manual-overrides.json'
    try {
        Assert-TrainerNotReparse $overridePath
        $overrides=Read-TrainerJson $overridePath 8388608
        if($overrides){foreach($p in $overrides.items.PSObject.Properties){if((Get-TrainerPathKey $p.Name) -eq $key){$override=$p.Value;break}}}
    } catch {return $null}
    if(Get-TrainerProperty $override 'idCleared' $false){return $null}
    $planPath=Join-Path $Root 'state/steam-add-plan.json'
    try {
        Assert-TrainerNotReparse $planPath
        $plan=Read-TrainerJson $planPath 16777216
        if(-not $plan -or (Get-TrainerProperty $plan 'schemaVersion' 0) -ne 1 -or $plan.items -isnot [array]){return $null}
        if((Get-TrainerPathKey ([string](Get-TrainerProperty $plan 'dataRoot' ''))) -ne (Get-TrainerPathKey $Root) -or
           (Get-TrainerPathKey ([string](Get-TrainerProperty $plan 'libraryState' ''))) -ne (Get-TrainerPathKey $scanPath)){return $null}
        $entries=@($plan.items|Where-Object{(Get-TrainerPathKey ([string](Get-TrainerProperty $_ 'primaryExecutable' ''))) -eq $key})
    } catch {return $null}
    if($entries.Count -gt 1){throw '自定义游戏库存在多个 EXE 身份计划，暂不自动选择修改器'}
    if(-not $entries.Count){return Get-TrainerLibraryLaunchIdentity $game $ExecutablePath $override}
    $entry=$entries[0]
    if((Get-TrainerProperty $entry 'identityStatus' '') -ne 'verified' -or
       (Get-TrainerProperty $entry 'steamVerificationStatus' '') -ne 'steam-verified' -or
       (Get-TrainerProperty $entry 'resolverVersion' '') -notin @('steam-official-fuzzy-global-v4','steam-official-fuzzy-global-v2')){return Get-TrainerLibraryLaunchIdentity $game $ExecutablePath $override}
    foreach($field in @('steamStoreAppId','storefrontAppId')){
        $value=Get-TrainerProperty $entry $field ''
        if($null -ne $value -and [string]$value -ne '' -and -not (Get-TrainerStoreAppId $value)){throw '游戏库商店 ID 格式无效，未自动选择修改器'}
    }
    $ids=@(@('steamStoreAppId','storefrontAppId')|ForEach-Object{Get-TrainerStoreAppId (Get-TrainerProperty $entry $_ '')}|Where-Object{$_}|Select-Object -Unique)
    if($ids.Count -ne 1){throw '自定义游戏库的商店 ID 不一致，暂不自动选择修改器'}
    $appId=$ids[0]
    $base=Get-TrainerStoreAppId (Get-TrainerProperty $entry 'baseGameAppId' '')
    if(($base -and $base -ne $appId) -or (Get-TrainerProperty $entry 'matchedContentAppId' '') -or
       (Get-TrainerProperty $entry 'contentRelation' '') -ne 'canonical-base-game'){throw '游戏库记录为扩展内容或不同本体，未自动选择修改器'}
    foreach($field in @('steamId','storefrontSteamId')){
        $manual=Get-TrainerStoreAppId (Get-TrainerProperty $override $field '')
        if($manual -and $manual -ne $appId){throw '手动游戏 ID 与已确认身份不一致，请先在游戏库确认'}
    }
    $name=[string](Get-TrainerProperty $entry 'formalName' '')
    if(-not $name.Trim() -or $name -match '\.(exe|dll|bat|cmd)$'){return $null}
    return @{appId=$appId;name=$name;sourcePath=$planPath;executablePath=$ExecutablePath}
}
function Get-TrainerAppIdAliases([string]$AppId,[string]$Name,[string]$DatabasePath) {
    $rows=@(Get-GcmCatalogRows $DatabasePath 'translations.json')
    $matched=@($rows|Where-Object{[string](Get-TrainerProperty $_ 'steam_id' '') -eq $AppId})
    $names=@($matched|ForEach-Object{Get-TrainerNameKey ([string](Get-TrainerProperty $_ 'en_US' ''))}|Select-Object -Unique)
    if($names.Count -gt 1){
        # A verified manifest/library title may select an exact variant of this same ID.
        # Never merge aliases from other editions or use fuzzy/prefix title matching here.
        $canonical=Get-TrainerNameKey $Name
        $exact=@($matched|Where-Object{(Get-TrainerNameKey ([string](Get-TrainerProperty $_ 'en_US' ''))) -eq $canonical})
        if(-not $canonical -or $exact.Count -ne 1){throw 'Steam 游戏 ID 对应多个名称，暂不自动选择修改器'}
        $matched=$exact
        $names=@($canonical)
    }
    if($names.Count -eq 1){
        $aliases=@($matched|ForEach-Object{foreach($language in @('en_US','zh_CN','zh_TW')){[string](Get-TrainerProperty $_ $language '')}}|Where-Object{$_}|Select-Object -Unique)
        $safe=@(Select-TrainerSafeAliases $aliases $rows $matched)
        if(-not $safe.Count){throw '已确认游戏的别名均存在冲突，未自动选择修改器'}
        return $safe
    }
    if($Name){
        $key=Get-TrainerNameKey $Name
        $conflicting=@($rows|Where-Object{
            $id=Get-TrainerStoreAppId (Get-TrainerProperty $_ 'steam_id' '')
            $id -and $id -ne $AppId -and (Get-TrainerNameKey ([string](Get-TrainerProperty $_ 'en_US' ''))) -eq $key
        })
        if($conflicting.Count){throw '游戏名称相同但 GCM 记录的 Steam ID 不同，未自动下载'}
        return @($Name)
    }
    return @()
}
function Get-TrainerLibraryLaunchIdentity($Game,[string]$ExecutablePath,$Override) {
    # Two independent install files must agree, and the launcher must target this exact EXE.
    $directory=[string](Get-TrainerProperty $Game 'gameDirectory' '')
    if(-not $directory){return $null}
    try {Assert-TrainerChild $ExecutablePath $directory|Out-Null;Assert-TrainerNotReparse $directory}catch{return $null}
    $appidPath=Join-Path ([IO.Path]::GetDirectoryName($ExecutablePath)) 'steam_appid.txt'
    if(-not [IO.File]::Exists($appidPath) -or (Get-Item -LiteralPath $appidPath).Length -gt 64){return $null}
    try {Assert-TrainerNotReparse $appidPath;$id=Get-TrainerStoreAppId ([IO.File]::ReadAllText($appidPath).Trim())}catch{return $null}
    if(-not $id){return $null}
    $candidates=@((Join-Path $directory 'launcher-settings.json'),(Join-Path $directory 'launcher/launcher-settings.json'),(Join-Path ([IO.Path]::GetDirectoryName($ExecutablePath)) 'launcher-settings.json'))|Select-Object -Unique
    $found=@()
    foreach($path in $candidates){
        try{
            Assert-TrainerChild $path $directory|Out-Null;Assert-TrainerNotReparse $path
            $settings=Read-TrainerJson $path 262144
            if(-not $settings -or (Get-TrainerProperty $settings 'distPlatform' '') -ne 'steam'){continue}
            $relative=[string](Get-TrainerProperty $settings 'exePath' '')
            if(-not $relative){continue}
            $target=Get-TrainerPathKey (Join-Path ([IO.Path]::GetDirectoryName($path)) $relative)
            if($target -ne (Get-TrainerPathKey $ExecutablePath)){continue}
            $url=[string](Get-TrainerProperty $settings 'browserDlcUrl' '')
            $match=[regex]::Match($url,'^https://store\.steampowered\.com/dlc/([1-9][0-9]{0,9})(?:/|$)')
            if(-not $match.Success){continue}
            if($match.Groups[1].Value -ne $id){throw [InvalidOperationException]::new('游戏安装目录的两个 Steam ID 不一致')}
            $name=[string](Get-TrainerProperty $settings 'displayName' '')
            if($name.Trim()){$found+=@{appId=$id;name=$name;sourcePath=$path;appIdSourcePath=$appidPath;executablePath=$ExecutablePath;method='custom-library-install-metadata'}}
        }catch [InvalidOperationException]{throw}catch{continue}
    }
    if($found.Count -ne 1){return $null}
    foreach($field in @('steamId','storefrontSteamId')){
        $manual=Get-TrainerStoreAppId (Get-TrainerProperty $Override $field '')
        if($manual -and $manual -ne $id){throw '手动游戏 ID 与安装目录证据不一致，请先在游戏库确认'}
    }
    return $found[0]
}
