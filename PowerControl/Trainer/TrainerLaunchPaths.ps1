# Resolve launch entries from the current GCM configuration; cached absolute paths are hints only.
function Throw-TrainerLaunchError([string]$Code,[string]$Message) {
    $exception=[IO.InvalidDataException]::new($Message)
    $exception.Data['trainerCode']=$Code
    throw $exception
}
function Resolve-TrainerCurrentEntry {
    param([string]$TrainerPath,[string]$TrainerFolder,[string]$ExpectedSha256,[string]$LibraryRoot,
        [string]$TrainerGameName='',[string]$TrainerOrigin='',[string]$GcmResourceId='')
    if($ExpectedSha256 -notmatch '^[a-fA-F0-9]{64}$') { Throw-TrainerLaunchError 'trainer-changed' '修改器缺少文件校验，请重新识别当前游戏' }
    if(-not [IO.Path]::IsPathRooted($TrainerFolder) -or -not [IO.Path]::IsPathRooted($TrainerPath)) { Throw-TrainerLaunchError 'trainer-unavailable' '修改器启动记录无效，请重新识别当前游戏' }
    $TrainerFolder=[IO.Path]::GetFullPath($TrainerFolder).TrimEnd('\','/')
    $TrainerPath=Assert-TrainerChild $TrainerPath $TrainerFolder
    Assert-TrainerNotReparse $LibraryRoot
    $folders=[Collections.Generic.List[string]]::new()
    try { $current=Assert-TrainerChild $TrainerFolder $LibraryRoot; $folders.Add($current) } catch { }
    $rebased=Join-Path $LibraryRoot ([IO.Path]::GetFileName($TrainerFolder))
    if(-not $folders.Contains($rebased)) { $folders.Add($rebased) }
    if([IO.Directory]::Exists($LibraryRoot)) {
        $items=@(Get-ChildItem -LiteralPath $LibraryRoot -Directory -ErrorAction Stop | Select-Object -First 2001)
        if($items.Count -gt 2000) { Throw-TrainerLaunchError 'trainer-unavailable' 'GCM 修改器目录过多，请在管理器确认当前修改器' }
        foreach($item in $items) {
            if(-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -and -not $folders.Contains($item.FullName)) { $folders.Add($item.FullName) }
        }
    }
    $matches=[Collections.Generic.List[object]]::new()
    $changed=$false
    foreach($folder in $folders) {
        if(-not [IO.Directory]::Exists($folder)) { continue }
        if(-not $TrainerGameName -and -not $GcmResourceId -and $folder -ine $TrainerFolder -and $folder -ine $rebased) { continue }
        $folder=Assert-TrainerChild $folder $LibraryRoot
        Assert-TrainerNotReparse $folder
        $metadataPath=Join-Path $folder 'gcm_info.json'
        $hasMetadata=[IO.File]::Exists($metadataPath)
        if($hasMetadata) {
            Assert-TrainerNotReparse $metadataPath
            try { $metadata=Read-TrainerJson $metadataPath 65536 } catch { continue }
            if($GcmResourceId) {
                if([string](Get-TrainerProperty $metadata 'gcm_url' '') -cne $GcmResourceId) { continue }
            } elseif($TrainerGameName -and (Get-TrainerNameKey ([string](Get-TrainerProperty $metadata 'game_name' ''))) -cne (Get-TrainerNameKey $TrainerGameName)) { continue }
            if($TrainerOrigin -and [string](Get-TrainerProperty $metadata 'origin' '') -cne $TrainerOrigin) { continue }
            # GCM can select a nested file or explicitly disable launching with extension=none.
            $selected=Get-TrainerLaunchFile $folder
        } else {
            # Compatibility for older exact-path requests; never discover anonymous executables.
            if($folder -ine $TrainerFolder) { continue }
            $selected=$TrainerPath
        }
        if(-not $selected -or -not [IO.File]::Exists($selected)) { continue }
        $selected=Assert-TrainerChild $selected $folder
        Assert-TrainerNotReparse $selected
        if((Get-FileHash -LiteralPath $selected -Algorithm SHA256).Hash -ine $ExpectedSha256) { $changed=$true; continue }
        if([IO.Path]::GetExtension($selected) -notin @('.exe','.ct','.cetrainer')) { Throw-TrainerLaunchError 'trainer-unavailable' '修改器文件无法启动，请在 GCM 中检查文件完整性' }
        $matches.Add(@{path=$selected;folder=$folder;sha256=$ExpectedSha256.ToLowerInvariant();libraryRoot=$LibraryRoot})
    }
    if($matches.Count -gt 1) { Throw-TrainerLaunchError 'trainer-ambiguous' 'GCM 中存在多个相同修改器，请在管理器保留或选择一个入口' }
    if($matches.Count -eq 1) { return $matches[0] }
    if($changed) { Throw-TrainerLaunchError 'trainer-changed' 'GCM 中的修改器已更新或启动入口已改变，请重新识别后启动' }
    Throw-TrainerLaunchError 'trainer-unavailable' ('未在 GCM 当前目录找到可启动的修改器：'+$LibraryRoot+'；请检查文件是否被移动或隔离')
}
function Resolve-TrainerConfiguredProgram([string]$Configured,[string]$FileName) {
    $value=[Environment]::ExpandEnvironmentVariables($Configured.Trim().Trim('"'))
    if(-not $value -or -not [IO.Path]::IsPathRooted($value)) { return '' }
    if([IO.Directory]::Exists($value)) { $value=Join-Path $value $FileName }
    if([IO.Path]::GetExtension($value) -ine '.exe' -or -not [IO.File]::Exists($value)) { return '' }
    return [IO.Path]::GetFullPath($value)
}
function Resolve-TrainerManagerProgram($Settings,[string]$ManagerPath,[string]$AppDirectory,[string]$SettingsRoot) {
    $explicit=$ManagerPath
    if(-not $explicit) { $explicit=[string](Get-TrainerProperty $Settings 'managerPath' '') }
    if(-not $explicit) { $explicit=$env:YEMAN_GCM_PATH }
    if($explicit) {
        $program=Resolve-TrainerConfiguredProgram $explicit 'Game Cheats Manager.exe'
        if($program) { return $program }
        throw ('配置的 Game Cheats Manager 不存在：'+$explicit)
    }
    $candidates=[Collections.Generic.List[string]]::new()
    foreach($process in [Diagnostics.Process]::GetProcessesByName('Game Cheats Manager')) {
        try { $candidates.Add($process.MainModule.FileName) } catch { }
    }
    foreach($hive in @('HKCU:','HKLM:')) {
        foreach($prefix in @('Software\Microsoft\Windows\CurrentVersion\App Paths','Software\WOW6432Node\Microsoft\Windows\CurrentVersion\App Paths')) {
            try { $key=Get-Item -LiteralPath ($hive+'\'+$prefix+'\Game Cheats Manager.exe') -ErrorAction Stop; $candidates.Add([string]$key.GetValue('')) } catch { }
        }
    }
    $shortcutRoots=@([Environment]::GetFolderPath('Programs'),[Environment]::GetFolderPath('CommonPrograms'),[Environment]::GetFolderPath('Desktop'),[Environment]::GetFolderPath('CommonDesktopDirectory'))
    $wsh=$null
    try {
        foreach($root in $shortcutRoots) {
            if(-not $root -or -not [IO.Directory]::Exists($root)) { continue }
            foreach($link in (Get-ChildItem -LiteralPath $root -Filter '*Game*Cheats*Manager*.lnk' -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 32)) {
                if(-not $wsh) { $wsh=New-Object -ComObject WScript.Shell }
                $shortcut=$wsh.CreateShortcut($link.FullName)
                try { $candidates.Add([string]$shortcut.TargetPath) } finally { [Runtime.InteropServices.Marshal]::ReleaseComObject($shortcut)|Out-Null }
            }
        }
    } finally { if($wsh) { [Runtime.InteropServices.Marshal]::ReleaseComObject($wsh)|Out-Null } }
    foreach($seed in @($AppDirectory,$PSScriptRoot,$SettingsRoot,$env:ProgramFiles,[Environment]::GetEnvironmentVariable('ProgramFiles(x86)'))) {
        if(-not $seed -or -not [IO.Path]::IsPathRooted($seed)) { continue }
        $directory=[IO.Path]::GetFullPath($seed)
        for($depth=0;$directory -and $depth -lt 4;$depth++) {
            $candidates.Add((Join-Path $directory 'Game Cheats Manager\Game Cheats Manager.exe'))
            $directory=[IO.Path]::GetDirectoryName($directory)
        }
    }
    $programs=@($candidates | ForEach-Object { Resolve-TrainerConfiguredProgram $_ 'Game Cheats Manager.exe' } | Where-Object { $_ } | Sort-Object -Unique)
    if($programs.Count -eq 1) { return $programs[0] }
    if($programs.Count -gt 1) { throw '找到多个 Game Cheats Manager，请通过 YEMAN_GCM_PATH 指定使用的管理器' }
    throw '未找到 Game Cheats Manager，请从安装位置打开一次管理器或配置 YEMAN_GCM_PATH'
}
function Resolve-TrainerCheatEngineProgram([string]$Configured) {
    $program=Resolve-TrainerConfiguredProgram $Configured 'Cheat Engine.exe'
    if($program) { return $program }
    $directory=[Environment]::ExpandEnvironmentVariables($Configured.Trim().Trim('"'))
    if($directory -and [IO.Path]::IsPathRooted($directory) -and [IO.Directory]::Exists($directory)) {
        $binary=if([Environment]::Is64BitOperatingSystem){'cheatengine-x86_64.exe'}else{'cheatengine-i386.exe'}
        $program=Resolve-TrainerConfiguredProgram (Join-Path $directory $binary) 'Cheat Engine.exe'
        if($program) { return $program }
    }
    throw ('GCM 配置的 Cheat Engine 不存在，请检查 cePath：'+$Configured)
}
# Keep CE configuration in GCM's own settings, shared by GCM and YMCC callers.
function Repair-TrainerGcmCePath {
    param([string]$SettingsRoot, [string]$DefaultDirectory='C:\SOFT\Cheat Engine')
    $settingsPath=Join-Path $SettingsRoot 'settings.json'
    $settings=Read-TrainerJson $settingsPath 65536
    if ($null -eq $settings) { return $settings }
    if ($settings -is [Array] -or $settings -isnot [pscustomobject]) { throw 'Invalid GCM settings object' }
    $configured=[string](Get-TrainerProperty $settings 'cePath' '')
    if ($configured.Trim()) { return $settings }
    # Only attach an existing CE installation. Never install CE or change its settings.
    try { $null=Resolve-TrainerCheatEngineProgram $DefaultDirectory } catch { return $settings }
    # Re-read before changing the single GCM field to retain recent unrelated edits.
    $settings=Read-TrainerJson $settingsPath 65536
    if ($null -eq $settings -or ([string](Get-TrainerProperty $settings 'cePath' '')).Trim()) { return $settings }
    $settings | Add-Member -MemberType NoteProperty -Name 'cePath' -Value $DefaultDirectory -Force
    Write-TrainerJson $settingsPath $settings
    return $settings
}
