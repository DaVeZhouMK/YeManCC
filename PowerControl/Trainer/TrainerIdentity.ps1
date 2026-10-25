# Identity is derived from installed manifests and confirmed custom-library evidence.
. (Join-Path $PSScriptRoot 'TrainerCustomLibrary.ps1')
. (Join-Path $PSScriptRoot 'TrainerProcessIdentity.ps1')
function Get-TrainerSteamIdentity([string]$ExecutablePath) {
    if (-not $ExecutablePath -or -not [IO.Path]::IsPathRooted($ExecutablePath)) { return $null }
    $exe=[IO.Path]::GetFullPath($ExecutablePath)
    $match=[regex]::Match($exe,'(?i)^(?<steamapps>.+[\\/]steamapps)[\\/]common[\\/](?<directory>[^\\/]+)[\\/]')
    if(-not $match.Success){return $null}
    $steamapps=$match.Groups['steamapps'].Value
    try {Assert-TrainerNotReparse $steamapps}catch{return $null}
    $matches=@()
    foreach($manifest in (Get-ChildItem -LiteralPath $steamapps -Filter 'appmanifest_*.acf' -File -ErrorAction SilentlyContinue|Select-Object -First 1000)){
        if($manifest.Length -gt 1048576 -or $manifest.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
        $text=[IO.File]::ReadAllText($manifest.FullName,[Text.Encoding]::UTF8)
        $appid=[regex]::Match($text,'(?m)^\s*"appid"\s+"(?<value>[1-9][0-9]{0,9})"').Groups['value'].Value
        $directory=[regex]::Match($text,'(?m)^\s*"installdir"\s+"(?<value>[^"\\/]+)"').Groups['value'].Value
        if(-not $appid -or $manifest.Name -ine ('appmanifest_'+$appid+'.acf') -or $directory -ine $match.Groups['directory'].Value){continue}
        $name=[regex]::Match($text,'(?m)^\s*"name"\s+"(?<value>[^"\r\n]+)"').Groups['value'].Value
        $matches+=@{appId=$appid;name=$name;manifestPath=$manifest.FullName;installDirectory=$directory}
    }
    if($matches.Count -eq 1){return $matches[0]}
    return $null
}
function Resolve-TrainerIdentity([string]$GameName,[string]$ExecutablePath,[string]$DatabasePath,$Record,[string]$CustomLibraryRoot='D:\YeMan\CustomSteamLibrary\data',[int]$GameProcessId=0,[string]$GameProcessCreated='') {
    if($ExecutablePath -and -not [IO.File]::Exists($ExecutablePath)){throw '游戏 EXE 已不存在，未自动准备修改器'}
    if($GameProcessId -gt 0 -or $GameProcessCreated){Assert-TrainerSharedGameTarget $ExecutablePath $GameProcessId $GameProcessCreated|Out-Null}
    $result=@{aliases=@();method='exact-title';steamAppId='';manifestPath=''}
    $steam=Get-TrainerSteamIdentity $ExecutablePath
    if($steam){
        if($steam.appId -in @('228980','223850','993090')){throw '该 Steam 条目是工具程序，未自动准备修改器'}
        $result.aliases=@(Get-TrainerAppIdAliases $steam.appId $steam.name $DatabasePath)
        $result.method=if(@(Get-GcmCatalogRows $DatabasePath 'translations.json'|Where-Object{[string](Get-TrainerProperty $_ 'steam_id' '') -eq $steam.appId}).Count){'steam-appid'}else{'steam-manifest'}
        $result.steamAppId=$steam.appId;$result.manifestPath=$steam.manifestPath
        if($result.method -ne 'exact-title'){return $result}
    }
    # The admitted native target maps its own exact GCM identity; a library grouping is optional metadata.
    # An unselected candidate in a shared parent must not veto this verified live PID/path/birth.
    $live=Get-TrainerSharedTargetIdentity $GameName $ExecutablePath $GameProcessId $GameProcessCreated $DatabasePath
    if($live){return $live}
    $custom=Get-TrainerCustomLibraryIdentity $ExecutablePath $CustomLibraryRoot
    if($custom){
        if($custom.appId -in @('228980','223850','993090')){throw '该游戏库身份是工具程序，未自动准备修改器'}
        $result.aliases=@(Get-TrainerAppIdAliases $custom.appId $custom.name $DatabasePath)
        $result.method=[string](Get-TrainerProperty $custom 'method' 'custom-library-appid');$result.steamAppId=$custom.appId;$result.librarySourcePath=$custom.sourcePath
        return $result
    }
    $aliases=@(Resolve-TrainerAliases $GameName $DatabasePath)
    $result.aliases=$aliases
    if($ExecutablePath){
        $exe=[IO.Path]::GetFullPath($ExecutablePath)
        $binding=$Record.games[(Get-TrainerHash $exe.ToLowerInvariant())]
        if($binding){
            $boundName=[string]$binding.gameName
            $keys=@($aliases|ForEach-Object{Get-TrainerNameKey $_})
            $sameName=(Get-TrainerNameKey $boundName) -in $keys
            $hash=[string](Get-TrainerProperty $binding 'executableSha256' '')
            $sameFile=$false
            if(-not $sameName -and $hash -and [IO.File]::Exists($exe)){$sameFile=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -eq $hash}
            if($sameName -or $sameFile){$result.aliases=@(Resolve-TrainerAliases $boundName $DatabasePath);$result.method='exe-binding'}
        }
    }
    return $result
}
