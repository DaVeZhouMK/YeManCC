# Consume the existing native game valve and the top consumer's shared name policy.
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'GameTitlePolicy.ps1')
$script:TrainerGameTargetPath=Join-Path (Split-Path $PSScriptRoot -Parent) 'game-target.json'
function Get-TrainerProcessObservation([int]$ProcessId,[bool]$RequireTitle=$true) {
    try {
        $process=Get-Process -Id $ProcessId -ErrorAction Stop
        $path=$process.Path;$created=[string]$process.StartTime.ToUniversalTime().ToFileTimeUtc()
        $title=$process.MainWindowTitle
        $process.Refresh()
        if($process.HasExited -or -not $path -or ($RequireTitle -and -not $title)){return $null}
        return @{pid=$ProcessId;path=$path;created=$created;title=$title}
    }catch{return $null}
}
function Throw-TrainerSharedTargetUnavailable([string]$Message) {
    $errorObject=[IO.IOException]::new($Message)
    $errorObject.Data['trainerCode']='shared-target-unavailable'
    $errorObject.Data['trainerRetryable']=$true
    throw $errorObject
}
function Read-TrainerSharedGameTarget {
    $path=$script:TrainerGameTargetPath
    Assert-TrainerNotReparse $path
    $target=Read-TrainerJson $path 65536
    if(-not $target){Throw-TrainerSharedTargetUnavailable '顶部共享游戏目标暂不可用，后台稍后自动重试'}
    $age=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-[long](Get-TrainerProperty $target 'lastSeen' 0)
    if((Get-TrainerProperty $target 'valid' $false) -ne $true -or [long](Get-TrainerProperty $target 'generation' 0) -le 0 -or $age -lt 0 -or $age -gt 10000){Throw-TrainerSharedTargetUnavailable '顶部共享游戏目标已过期或撤销，后台稍后重新核验'}
    return @{pid=[int](Get-TrainerProperty $target 'pid' 0);created=[string](Get-TrainerProperty $target 'processCreated' '');path=[string](Get-TrainerProperty $target 'path' '');title=[string](Get-TrainerProperty $target 'title' '');name=[string](Get-TrainerProperty $target 'name' '');snapshotPath=$path}
}
function Assert-TrainerSharedGameTarget([string]$ExecutablePath,[int]$ProcessId,[string]$Created) {
    $selected=Read-TrainerSharedGameTarget
    $observed=Get-TrainerProcessObservation $ProcessId $false
    foreach($target in @($selected,$observed)){
        if(-not $target -or $target.pid -ne $ProcessId -or $target.created -cne $Created -or (Get-TrainerPathKey $target.path) -ne (Get-TrainerPathKey $ExecutablePath)){throw '顶部游戏进程已退出或身份改变，未准备修改器'}
    }
    return $selected
}
function Get-TrainerSharedTargetIdentity([string]$GameName,[string]$ExecutablePath,[int]$ProcessId,[string]$Created,[string]$DatabasePath) {
    if($ProcessId -le 0 -or $Created -notmatch '^[1-9][0-9]*$'){return $null}
    $selected=Assert-TrainerSharedGameTarget $ExecutablePath $ProcessId $Created
    # GameName is the top consumer's existing detectedGameName result. GCM equality
    # maps this admitted game to a resource; it does not choose a process or parse titles.
    $key=Get-TrainerNameKey $GameName
    $selectedKeys=@(@($selected.title,$selected.name)|ForEach-Object{Get-TrainerNameKey (ConvertTo-YmccGameTitle $_)}|Where-Object{$_})
    if($key -notin $selectedKeys){throw '当前请求与顶部共享游戏名称不一致，请刷新游戏后重试'}
    $matched=@(Get-GcmCatalogRows $DatabasePath 'translations.json'|Where-Object{
        $row=$_
        @(foreach($language in @('en_US','zh_CN','zh_TW')){if((Get-TrainerNameKey ([string](Get-TrainerProperty $row $language ''))) -eq $key){$true}}).Count -gt 0
    })
    $ids=@($matched|ForEach-Object{Get-TrainerStoreAppId (Get-TrainerProperty $_ 'steam_id' '')}|Where-Object{$_}|Select-Object -Unique)
    $names=@($matched|ForEach-Object{Get-TrainerNameKey ([string](Get-TrainerProperty $_ 'en_US' ''))}|Select-Object -Unique)
    if($ids.Count -ne 1 -or $names.Count -ne 1){return $null}
    if($ids[0] -in @('228980','223850','993090')){throw '该顶部目标对应 Steam 工具，未准备修改器'}
    $aliases=@(Get-TrainerAppIdAliases $ids[0] ([string]$matched[0].en_US) $DatabasePath)
    $sha=(Get-FileHash -LiteralPath $ExecutablePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $after=Assert-TrainerSharedGameTarget $ExecutablePath $ProcessId $Created
    return @{method='ymcc-game-target';gameName=$GameName;steamAppId=$ids[0];aliases=$aliases;processId=$ProcessId;processCreated=$Created;executablePath=$ExecutablePath;executableSha256=$sha;windowTitle=$selected.title;snapshotPath=$selected.snapshotPath}
}
