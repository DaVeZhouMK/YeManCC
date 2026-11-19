# Bundled presets and reversible, per-user takeover. Loaded by steam-appearance.ps1.
$script:PackRoot=$PSScriptRoot
$script:PresetTransactionActive=$false
$local=if($env:LOCALAPPDATA){$env:LOCALAPPDATA}else{Join-Path $env:USERPROFILE 'AppData\Local'}
$script:PresetStateBase=Join-Path $local 'YMCC\SteamAppearance'
function Get-PresetStateDir {
    $sha=[Security.Cryptography.SHA256]::Create()
    try {$key=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($script:ThemeRoot).ToLowerInvariant())))).Replace('-','').Substring(0,16)}finally{$sha.Dispose()}
    Join-Path $script:PresetStateBase $key
}
function Assert-ThemeTarget([string]$Path,[ref]$ResourceItem) {
    $root=[IO.Path]::GetFullPath($script:ThemeRoot).TrimEnd('\');$full=[IO.Path]::GetFullPath($Path)
    if(!$full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Preset target escaped theme root'}
    $parent=[IO.Path]::GetDirectoryName($full)
    while($parent -and $parent.Length -ge $root.Length){
        $attributes=$null
        if($ResourceItem){
            # Installed-resource reads only need current filesystem attributes.
            try{$attributes=[IO.File]::GetAttributes($parent)}catch [IO.FileNotFoundException],[IO.DirectoryNotFoundException]{}
        }elseif(Test-Path -LiteralPath $parent){$attributes=[IO.File]::GetAttributes($parent)}
        if($attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Theme directory links are not overwritten automatically'}
        $parent=[IO.Path]::GetDirectoryName($parent)
    }
    if($ResourceItem){
        $item=try{Get-Item -LiteralPath $full -Force -ErrorAction Stop}catch [Management.Automation.ItemNotFoundException]{$null}
        if($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Theme file link is not overwritten'}
        $ResourceItem.Value=$item
    }elseif(Test-Path -LiteralPath $full){if((Get-Item -LiteralPath $full -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Theme file link is not overwritten'}}
    $full
}
function Get-PresetRelative([string]$Relative) {
    $value=$Relative.Replace('/','\')
    if([IO.Path]::IsPathRooted($value) -or $value.Contains(':') -or $value.IndexOf([char]0) -ge 0){throw 'Invalid preset relative path'}
    foreach($part in $value.Split('\')){if($part -eq '..' -or $part -eq '.' -or $part -eq ''){throw 'Invalid preset relative path'}}
    $value
}
function Get-PresetResourceHash([string]$Path,[string]$InvalidFileMessage='',[switch]$InstalledTarget,[ref]$Raw) {
    if($InstalledTarget){
        # Reuse the checked leaf metadata; a missing or ordinary directory is repairable drift.
        $item=$null
        $Path=Assert-ThemeTarget $Path -ResourceItem ([ref]$item)
        if($item -isnot [IO.FileInfo]){return}
    }else{
        $item=try{Get-Item -LiteralPath $Path -Force -ErrorAction Stop}catch [Management.Automation.ItemNotFoundException]{if($InvalidFileMessage){throw $InvalidFileMessage};throw}
    }
    if($item -isnot [IO.FileInfo] -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){
        if($InvalidFileMessage){throw $InvalidFileMessage}
        throw 'Preset resource link or non-file denied'
    }
    # Oversized files cannot be backed up. Reject before hashing, and enforce
    # the same limit on the opened handle in case the file has grown.
    if($item.Length -gt 16777216){
        if($InvalidFileMessage){throw $InvalidFileMessage}
        throw 'Theme resource exceeds the 16 MiB safe backup limit; no theme files were changed'
    }
    $stamp=[string]$item.Length+':'+$item.LastWriteTimeUtc.Ticks+':'+$item.CreationTimeUtc.Ticks+':'+[int]$item.Attributes
    # Metadata can detect ordinary races, but cannot authorize an old digest.
    $hash=if($Raw){$snapshot=Read-AppearanceThemeConfig $Path;$snapshot.hash}else{
        try{Get-ConfigHash $Path -MaximumBytes 16777216}
        catch{if($InvalidFileMessage -and $_.Exception.Message -ceq 'Preset backup integrity check failed'){throw $InvalidFileMessage};throw}
    }
    $after=[IO.FileInfo]::new($Path)
    $after.Refresh()
    if(!$after.Exists -or ($after.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Preset resource changed during validation'}
    $afterStamp=[string]$after.Length+':'+$after.LastWriteTimeUtc.Ticks+':'+$after.CreationTimeUtc.Ticks+':'+[int]$after.Attributes
    if($afterStamp -ne $stamp){throw 'Preset resource changed during validation'}
    if($Raw){$Raw.Value=$snapshot.raw}
    return $hash
}
function Read-PresetManifest {
    $file=Join-Path $script:PackRoot 'manifest.json'
    if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw 'YMCC 自带主题包缺失，请修复安装'}
    # Parse and digest the same bounded bytes, then keep the final fresh check.
    $snapshot=Read-AppearanceThemeConfig $file
    $m=$snapshot.raw|ConvertFrom-Json
    if($m.id -ne 'ymcc-steam-appearance' -or $m.version -ne 1 -or @($m.themes).Count -ne $script:ThemeNames.Count -or @($m.files).Count -gt 256 -or @($m.files).Count -lt (2*$script:ThemeNames.Count)){throw 'Invalid bundled preset manifest'}
    foreach($name in $script:ThemeNames){if(@($m.themes|Where-Object name -eq $name).Count -ne 1){throw 'Bundled theme/name mismatch'}}
    $seen=@{};$schemaInputs=@{};$sourceRoot=[IO.Path]::Combine($script:PackRoot,'themes')
    foreach($entry in $m.files){
        $rel=Get-PresetRelative ([string]$entry.path);$parts=$rel.Split('\')
        if($parts[0] -notin $script:ThemeNames -or $seen.ContainsKey($rel)){throw 'Bundled preset path denied or duplicated'}
        $seen[$rel]=$true;$source=[IO.Path]::Combine($sourceRoot,$rel)
        $invalidFileMessage="Bundled preset checksum failed: $rel"
        $hash=if($parts.Count -eq 2 -and $parts[1] -in 'theme.json','config_USER.json'){
            # The schema and checksum must describe the same bounded bytes.
            $raw='';$value=Get-PresetResourceHash $source $invalidFileMessage -Raw ([ref]$raw)
            $schemaInputs[$rel]=$raw
            $value
        }else{Get-PresetResourceHash $source $invalidFileMessage}
        if($hash -ne $entry.sha256){throw $invalidFileMessage}
    }
    foreach($name in $script:ThemeNames){foreach($required in @('theme.json','config_USER.json')){if(!$seen.ContainsKey($name+'\'+$required)){throw 'Bundled preset incomplete'}}}
    foreach($name in $script:ThemeNames){
        $theme=$schemaInputs[$name+'\theme.json']|ConvertFrom-Json
        $config=$schemaInputs[$name+'\config_USER.json']|ConvertFrom-Json
        if($config.active -ne $true){throw "Bundled default must enable all managed themes: $name"}
        if($theme.name -ne $name -or $config.active -isnot [bool] -or !$theme.manifest_version){throw 'Bundled theme/config schema is invalid'}
    }
    $digest=Get-ConfigHash $file -MaximumBytes 262144
    if($digest -cne $snapshot.hash){throw 'Bundled preset manifest changed during validation'}
    [pscustomobject]@{manifest=$m;digest=$digest;files=@($m.files)}
}
function Assert-PresetStateTarget([string]$Path) {
    $root=[IO.Path]::GetFullPath((Get-PresetStateDir)).TrimEnd('\');$full=[IO.Path]::GetFullPath($Path)
    if(!$full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Preset state path escaped current profile'}
    $current=$full
    while($current -and $current.Length -ge $root.Length){if(Test-Path -LiteralPath $current){$attributes=if($current -ceq $full){(Get-Item -LiteralPath $current -Force).Attributes}else{[IO.File]::GetAttributes($current)};if($attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Preset state links are not followed'}};$current=[IO.Path]::GetDirectoryName($current)}
    $full
}
function Get-PresetTargetHintPath {
    $root=[IO.Path]::GetFullPath($script:PresetStateBase).TrimEnd('\');$path=Join-Path $root 'active-target.json';$current=$path
    while($current -and $current.Length -ge $root.Length){if(Test-Path -LiteralPath $current){if((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Preset target hint links are not followed'}};$current=[IO.Path]::GetDirectoryName($current)}
    $path
}
function Save-PresetTargetHint {
    $path=Get-PresetTargetHintPath;$hint=[pscustomobject]@{version=1;themeRoot=[IO.Path]::GetFullPath($script:ThemeRoot);backendExe=$script:BackendExe};$text=$hint|ConvertTo-Json -Compress
    if((Test-Path -LiteralPath $path -PathType Leaf) -and [IO.File]::ReadAllText($path,[Text.Encoding]::UTF8) -eq $text){return}
    New-Item -ItemType Directory -Path $script:PresetStateBase -Force|Out-Null
    Write-ConfigAtomic $path $text
}
function Initialize-PresetTargetHint([switch]$KeepBackendPath) {
    $path=Get-PresetTargetHintPath;if(!(Test-Path -LiteralPath $path)){return}
    try {
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -gt 262144){throw 'Invalid target hint file'}
        $hint=[IO.File]::ReadAllText($path,[Text.Encoding]::UTF8)|ConvertFrom-Json
        if($hint.version -ne 1 -or $hint.themeRoot -isnot [string] -or ![IO.Path]::IsPathRooted($hint.themeRoot) -or [IO.Path]::GetFullPath($hint.themeRoot).TrimEnd('\').Length -lt 8 -or $hint.backendExe -isnot [string] -or ![IO.Path]::IsPathRooted($hint.backendExe) -or [IO.Path]::GetFileName($hint.backendExe) -ine 'CssLoader-Standalone-Headless.exe'){throw 'Invalid target hint schema'}
        $script:ThemeRoot=[IO.Path]::GetFullPath($hint.themeRoot).TrimEnd('\')
        if(!$KeepBackendPath){$script:BackendExe=[IO.Path]::GetFullPath($hint.backendExe)}
    }catch{throw ('YMCC 外观目标记录损坏，已停止自动套用：'+$_.Exception.Message)}
}
function Read-PresetState {
    $path=Assert-PresetStateTarget (Join-Path (Get-PresetStateDir) 'owner.json')
    if(!(Test-Path -LiteralPath $path)){return $null}
    try {
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -gt 262144){throw 'Invalid ownership file'}
        $state=[IO.File]::ReadAllText($path,[Text.Encoding]::UTF8)|ConvertFrom-Json
        if($state.version -ne 1 -or $state.themeRoot -ne [IO.Path]::GetFullPath($script:ThemeRoot) -or $state.owned -isnot [bool] -or $state.pendingReload -isnot [bool] -or ($state.owned -and ($state.packDigest -notmatch '^[a-fA-F0-9]{64}$' -or $state.originalBackup -isnot [string]))){throw 'Invalid ownership schema'}
        return $state
    }catch{throw ('YMCC 接管记录损坏，已停止写入；请保留备份并修复 owner.json：'+$_.Exception.Message)}
}
function Save-PresetState($State) {
    $State|Add-Member -NotePropertyName backendExe -NotePropertyValue $script:BackendExe -Force
    $dir=Get-PresetStateDir;New-Item -ItemType Directory -Path $dir -Force|Out-Null
    Write-ConfigAtomic (Assert-PresetStateTarget (Join-Path $dir 'owner.json')) ($State|ConvertTo-Json -Depth 16 -Compress)
}
function Read-PresetUserConfig([string]$Path) {
    $Path=Assert-ThemeTarget $Path
    if(!(Test-Path -LiteralPath $Path)){return $null}
    try{
        if(!(Test-Path -LiteralPath $Path -PathType Leaf)){throw 'Config path is not a file'}
        if((Get-Item -LiteralPath $Path).Length -gt 262144){throw 'Config too large for safe automatic repair'}
        $raw=[IO.File]::ReadAllText($Path,[Text.Encoding]::UTF8)
    }catch{throw ('无法安全读取现有主题配置，已停止自动覆盖：'+$_.Exception.Message)}
    try{$config=$raw|ConvertFrom-Json;if($config.active -isnot [bool]){return $null};return $config}catch{return $null}
}
function Get-ForeignThemeRows($BackendThemes=@()) {
    if(!(Test-Path -LiteralPath $script:ThemeRoot -PathType Container)){return}
    foreach($dir in Get-ChildItem -LiteralPath $script:ThemeRoot -Directory -Force){
        if($dir.Name -in $script:ThemeNames){continue}
        $manifest=Join-Path $dir.FullName 'theme.json';if(!(Test-Path -LiteralPath $manifest -PathType Leaf)){continue}
        $name='';$bad=$false;$manifestInvalid=$false;$active=$false;$config=Join-Path $dir.FullName 'config_USER.json'
        try{if((Get-Item -LiteralPath $manifest).Length -gt 262144){throw 'Foreign manifest too large'};$j=[IO.File]::ReadAllText($manifest,[Text.Encoding]::UTF8)|ConvertFrom-Json;$name=[string]$j.name;if(!$name -or $name.Length -gt 256){throw 'Invalid foreign theme name'}}catch{$manifestInvalid=$true;$name=$dir.Name}
        if(Test-Path -LiteralPath $config){try{if((Get-Item -LiteralPath $config).Length -gt 262144){throw 'Foreign config too large'};$c=[IO.File]::ReadAllText($config,[Text.Encoding]::UTF8)|ConvertFrom-Json;if($c.active -isnot [bool]){throw 'Bad active field'};$active=$c.active}catch{$bad=$true}}
        $live=@($BackendThemes|Where-Object name -eq $name|Where-Object enabled).Count -gt 0
        [pscustomobject]@{name=$name;folder=$dir.Name;path=$config;manifestPath=$manifest;active=$active;invalid=$bad;manifestInvalid=$manifestInvalid;live=$live;needsDisable=($active -or $live -or $bad -or $manifestInvalid -or $name -in $script:ThemeNames);duplicate=($name -in $script:ThemeNames);retired=($name -in $script:RetiredThemeNames -or $dir.Name -in $script:RetiredThemeNames)}
    }
}
# Remove only known retired payload files; never traverse or recursively delete a user's theme directory.
# Backups remain intact so a failed transaction can restore the exact previous bytes.
function Get-RetiredPresetTargets($Foreign=@()) {
    $directories=@($script:RetiredThemeNames|ForEach-Object {Join-Path $script:ThemeRoot $_})
    $directories+=@($Foreign|Where-Object retired|ForEach-Object {Split-Path -Parent $_.manifestPath})
    foreach($directory in $directories|Select-Object -Unique){
        foreach($leaf in @('theme.json','config_USER.json','shared.css','LICENSE')){
            $target=Assert-ThemeTarget (Join-Path $directory $leaf)
            if(Test-Path -LiteralPath $target){
                if(!(Test-Path -LiteralPath $target -PathType Leaf)){throw 'Retired theme target is not a regular file'}
                $target
            }
        }
    }
}
# CSSLoader treats even empty directories as theme candidates. Only known
# retired directories without children are candidates; links remain forbidden.
# File backups recreate payload directories when the transaction rolls back.
function Get-EmptyRetiredPresetDirectories($Foreign=@()) {
    $directories=@($script:RetiredThemeNames|ForEach-Object {Join-Path $script:ThemeRoot $_})
    $directories+=@($Foreign|Where-Object retired|ForEach-Object {Split-Path -Parent $_.manifestPath})
    foreach($directory in $directories|Select-Object -Unique){
        $target=Assert-ThemeTarget $directory
        if((Test-Path -LiteralPath $target -PathType Container) -and @(Get-ChildItem -LiteralPath $target -Force -ErrorAction Stop).Count -eq 0){$target}
    }
}
function Get-PresetMetadata($Rows,$BackendThemes=@()) {
    $state=Read-PresetState;$available=$false;$digest='';$drift=0;$error=''
    try {
        $pack=Read-PresetManifest;$available=$true;$digest=$pack.digest
        foreach($file in $pack.files){
            if([IO.Path]::GetFileName($file.path) -eq 'config_USER.json'){continue}
            $target=[IO.Path]::Combine($script:ThemeRoot,(Get-PresetRelative $file.path))
            if((Get-PresetResourceHash $target -InstalledTarget) -ne $file.sha256){$drift++}
        }
    }catch{$error=$_.Exception.Message;$drift++}
    $foreign=@(Get-ForeignThemeRows $BackendThemes);$foreignCount=@($foreign|Where-Object {!$_.retired -and $_.needsDisable}).Count
    $retired=@(Get-RetiredPresetTargets $foreign)+@(Get-EmptyRetiredPresetDirectories $foreign)
    $retiredLive=@($BackendThemes|Where-Object {$_.name -in $script:RetiredThemeNames -and $_.enabled}).Count -gt 0
    $owned=$state -and $state.owned
    $prepared=[bool]($available -and $owned -and $state.packDigest -eq $digest -and $drift -eq 0 -and $retired.Count -eq 0 -and @($Rows|Where-Object {!$_.installed}).Count -eq 0)
    $canRestore=$false
    if($state -and $state.owned -and $state.originalBackup){try{Read-PresetBackup ([string]$state.originalBackup)|Out-Null;$canRestore=$true}catch{$error='接管备份不可用：'+$_.Exception.Message}}
    if($owned -and !$canRestore){$prepared=$false}
    [pscustomobject]@{packAvailable=$available;packOwned=[bool]$owned;packUpdateAvailable=[bool]($available -and $owned -and $canRestore -and $state.packDigest -ne $digest);packPrepared=$prepared;packRepairRequired=[bool]($available -and $owned -and !$prepared);backendReloadRequired=[bool](($state -and $state.pendingReload) -or $retiredLive);canRestore=$canRestore;foreignActiveCount=$foreignCount;packError=$error}
}
function Write-PresetBytes([string]$Path,[byte[]]$Bytes) {
    $Path=Assert-ThemeTarget $Path;$parent=Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $parent -Force|Out-Null
    $temp=$Path+'.ymcc-'+[guid]::NewGuid().ToString('N')+'.tmp';$old=$temp+'.bak'
    try {[IO.File]::WriteAllBytes($temp,$Bytes);if(Test-Path -LiteralPath $Path){[IO.File]::Replace($temp,$Path,$old)}else{[IO.File]::Move($temp,$Path)}}
    finally{foreach($file in @($temp,$old)){if(Test-Path -LiteralPath $file){Remove-Item -LiteralPath $file -Force}}}
}
function New-PresetBackup($Paths) {
    $pathsToSave=@($Paths|Select-Object -Unique);$totalBytes=0
    if($pathsToSave.Count -gt 2048){throw 'Too many preset backup targets'}
    foreach($target in $pathsToSave){$full=Assert-ThemeTarget $target;if(Test-Path -LiteralPath $full -PathType Leaf){$length=(Get-Item -LiteralPath $full).Length;$totalBytes+=$length;if($length -gt 16777216 -or $totalBytes -gt 134217728){throw 'Theme backup size exceeds safe limit; no theme files were changed'}}}
    $base=Join-Path (Get-PresetStateDir) 'backups';$dir=Join-Path $base ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))
    Assert-PresetStateTarget (Join-Path $dir 'journal.json')|Out-Null
    New-Item -ItemType Directory -Path (Join-Path $dir 'files') -Force|Out-Null
    $entries=New-Object Collections.Generic.List[object];$index=0
    foreach($path in $pathsToSave){
        $full=Assert-ThemeTarget $path;$exists=Test-Path -LiteralPath $full -PathType Leaf;$rel=$full.Substring([IO.Path]::GetFullPath($script:ThemeRoot).TrimEnd('\').Length+1);$blob=$index.ToString('D4')+'.bin';$hash=''
        if($exists){$bytes=[IO.File]::ReadAllBytes($full);[IO.File]::WriteAllBytes((Join-Path (Join-Path $dir 'files') $blob),$bytes);$hash=Get-ConfigHash (Join-Path (Join-Path $dir 'files') $blob)}
        $entries.Add([pscustomobject]@{relative=$rel;existed=$exists;blob=$blob;hash=$hash});$index++
    }
    $journal=[pscustomobject]@{version=1;themeRoot=[IO.Path]::GetFullPath($script:ThemeRoot);entries=$entries.ToArray()}
    [IO.File]::WriteAllText((Join-Path $dir 'journal.json'),($journal|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
    [pscustomobject]@{directory=$dir;journal=$journal}
}
function Assert-PresetBackupReadPath([string]$Root,[string]$Path,[string]$InvalidPath,[switch]$RequireFile) {
    $full=[IO.Path]::GetFullPath($Path)
    if(!$full.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase)){throw $InvalidPath}
    # Existence, file type and reparse status come from one attribute query
    # per component. Only a missing component is treated as absent.
    for($current=$full;$current -and $current.Length -ge $Root.Length;$current=[IO.Path]::GetDirectoryName($current)){
        $attributes=$null
        try{$attributes=[IO.File]::GetAttributes($current)}
        catch [IO.FileNotFoundException],[IO.DirectoryNotFoundException]{}
        if($attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Preset backup path links are not followed'}
        if($RequireFile -and $current -ceq $full -and ($null -eq $attributes -or ($attributes -band [IO.FileAttributes]::Directory))){throw $InvalidPath}
    }
    $full
}
function Read-PresetBackup([string]$Directory) {
    $stateRoot=[IO.Path]::GetFullPath((Get-PresetStateDir)).TrimEnd('\')
    $themeRoot=[IO.Path]::GetFullPath($script:ThemeRoot)
    $base=[IO.Path]::GetFullPath((Join-Path $stateRoot 'backups')).TrimEnd('\');$dir=[IO.Path]::GetFullPath($Directory)
    if(!$dir.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Backup escaped current user state directory'}
    $journalPath=Assert-PresetBackupReadPath $stateRoot (Join-Path $dir 'journal.json') 'Invalid preset backup journal file' -RequireFile
    # Check size on the same read handle that supplies the JSON bytes.
    $stream=[IO.File]::OpenRead($journalPath)
    try {
        if($stream.Length -gt 1048576){throw 'Invalid preset backup journal file'}
        $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8,$true,4096,$true)
        try{$raw=$reader.ReadToEnd()}finally{$reader.Dispose()}
    }finally{$stream.Dispose()}
    $j=$raw|ConvertFrom-Json
    if($j.version -ne 1 -or $j.themeRoot -ne $themeRoot -or $j.entries -isnot [array] -or @($j.entries).Count -gt 2048){throw 'Invalid preset backup'}
    $seen=@{};$blobs=@{};$totalBytes=0;$blobRoot=[IO.Path]::Combine($dir,'files');$themePathRoot=$themeRoot.TrimEnd('\')
    foreach($entry in $j.entries){
        if($entry.relative -isnot [string] -or $entry.existed -isnot [bool] -or $entry.blob -isnot [string] -or $entry.blob -notmatch '^\d{4}\.bin$' -or $entry.hash -isnot [string]){throw 'Invalid preset backup entry schema'}
        $rel=Get-PresetRelative $entry.relative
        if($seen.ContainsKey($rel) -or $blobs.ContainsKey($entry.blob)){throw 'Duplicate preset backup target/blob'}
        $seen[$rel]=$true;$blobs[$entry.blob]=$true
        $null=Assert-PresetBackupReadPath $themePathRoot ([IO.Path]::Combine($themeRoot,$rel)) 'Preset target escaped theme root'
        if($entry.existed){
            $blob=Assert-PresetBackupReadPath $stateRoot ([IO.Path]::Combine($blobRoot,$entry.blob)) 'Preset backup integrity check failed' -RequireFile
            $length=0
            if($entry.hash -notmatch '^[a-fA-F0-9]{64}$' -or (Get-ConfigHash $blob 16777216 ([ref]$length)) -ne $entry.hash){throw 'Preset backup integrity check failed'}
            $totalBytes+=$length;if($totalBytes -gt 134217728){throw 'Preset backup exceeds safe total size'}
        }elseif($entry.hash -ne ''){throw 'Unexpected hash for absent backup file'}
    }
    [pscustomobject]@{directory=$dir;journal=$j}
}
function Merge-PresetOriginalBackup([string]$OriginalDirectory,$Transaction) {
    # Keep the first pre-YMCC bytes for every path we ever touch, including late conflicts.
    # Never mutate a historical backup in place; switch owner only after this copy is verified.
    $original=Read-PresetBackup $OriginalDirectory;$seen=@{}
    foreach($entry in $original.journal.entries){$seen[(Get-PresetRelative $entry.relative)]=$true}
    $added=@($Transaction.journal.entries|Where-Object {!$seen.ContainsKey((Get-PresetRelative $_.relative))})
    if(!$added.Count){return $original.directory}
    if(@($original.journal.entries).Count+$added.Count -gt 2048){throw 'Too many reversible theme changes; restore before taking over more themes'}
    $merged=New-PresetBackup @();$entries=New-Object Collections.Generic.List[object];$index=0
    foreach($part in @([pscustomobject]@{backup=$original;entries=@($original.journal.entries)},[pscustomobject]@{backup=$Transaction;entries=$added})){
        foreach($entry in $part.entries){
            $blob=$index.ToString('D4')+'.bin';$hash=''
            if($entry.existed){$source=Join-Path (Join-Path $part.backup.directory 'files') $entry.blob;$target=Join-Path (Join-Path $merged.directory 'files') $blob;[IO.File]::WriteAllBytes($target,[IO.File]::ReadAllBytes($source));$hash=Get-ConfigHash $target}
            $entries.Add([pscustomobject]@{relative=$entry.relative;existed=[bool]$entry.existed;blob=$blob;hash=$hash});$index++
        }
    }
    $merged.journal.entries=$entries.ToArray()
    Write-ConfigAtomic (Join-Path $merged.directory 'journal.json') ($merged.journal|ConvertTo-Json -Depth 8)
    Read-PresetBackup $merged.directory|Out-Null
    return $merged.directory
}
function Remove-EmptyPresetDirectory([string]$Directory) {
    # Optional cleanup must never traverse a junction or prevent a completed file rollback.
    if(!(Test-Path -LiteralPath $Directory -PathType Container)){return}
    if((Get-Item -LiteralPath $Directory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){return}
    try{Assert-ThemeTarget (Join-Path $Directory 'check.tmp')|Out-Null}catch{return}
    foreach($child in @(Get-ChildItem -LiteralPath $Directory -Directory -Force)){Remove-EmptyPresetDirectory $child.FullName}
    if(@(Get-ChildItem -LiteralPath $Directory -Force).Count -eq 0){Remove-Item -LiteralPath $Directory -Force}
}
function Restore-PresetBackup($Backup,[switch]$KeepRetiredRemoved) {
    $retiredFolders=@{};foreach($name in $script:RetiredThemeNames){$retiredFolders[$name]=$true}
    if($KeepRetiredRemoved){
        foreach($entry in $Backup.journal.entries){
            $relative=Get-PresetRelative $entry.relative
            if(!$entry.existed -or [IO.Path]::GetFileName($relative) -ine 'theme.json'){continue}
            $blob=Join-Path (Join-Path $Backup.directory 'files') $entry.blob
            if((Get-Item -LiteralPath $blob).Length -gt 262144){continue}
            try{$theme=[IO.File]::ReadAllText($blob,[Text.Encoding]::UTF8)|ConvertFrom-Json;if($theme.name -in $script:RetiredThemeNames){$retiredFolders[$relative.Split('\')[0]]=$true}}catch{}
        }
    }
    foreach($entry in $Backup.journal.entries){
        if($KeepRetiredRemoved -and $retiredFolders.ContainsKey((Get-PresetRelative $entry.relative).Split('\')[0])){continue}
        $path=Assert-ThemeTarget (Join-Path $script:ThemeRoot (Get-PresetRelative $entry.relative))
        if($entry.existed){Write-PresetBytes $path ([IO.File]::ReadAllBytes((Join-Path (Join-Path $Backup.directory 'files') $entry.blob)))}elseif(Test-Path -LiteralPath $path -PathType Leaf){Remove-Item -LiteralPath $path -Force}
    }
    # Only empty, validated directories created for managed themes; never recursive delete.
    foreach($name in $script:ThemeNames){Remove-EmptyPresetDirectory (Join-Path $script:ThemeRoot $name)}
}
function Get-PresetBytesHash([byte[]]$Bytes) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try {([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','')}finally{$sha.Dispose()}
}
function Write-PresetStateBytes([string]$Path,[byte[]]$Bytes) {
    $Path=Assert-PresetStateTarget $Path;$temp=$Path+'.ymcc-'+[guid]::NewGuid().ToString('N')+'.tmp';$old=$temp+'.bak'
    try {[IO.File]::WriteAllBytes($temp,$Bytes);if(Test-Path -LiteralPath $Path){[IO.File]::Replace($temp,$Path,$old)}else{[IO.File]::Move($temp,$Path)}}
    finally{foreach($item in @($temp,$old)){if(Test-Path -LiteralPath $item){Remove-Item -LiteralPath $item -Force}}}
}
function Begin-PresetTransaction($Backup) {
    $marker=Assert-PresetStateTarget (Join-Path (Get-PresetStateDir) 'transaction.json')
    if($script:PresetTransactionActive -or (Test-Path -LiteralPath $marker)){throw 'Unfinished preset transaction must be recovered first'}
    Read-PresetBackup $Backup.directory|Out-Null
    $owner=Assert-PresetStateTarget (Join-Path (Get-PresetStateDir) 'owner.json');$existed=Test-Path -LiteralPath $owner -PathType Leaf
    $bytes=if($existed){[IO.File]::ReadAllBytes($owner)}else{New-Object byte[] 0}
    if($bytes.Length -gt 262144){throw 'Ownership record too large'}
    $txn=[pscustomobject]@{version=1;themeRoot=[IO.Path]::GetFullPath($script:ThemeRoot);backup=$Backup.directory;ownerExisted=[bool]$existed;ownerBase64=[Convert]::ToBase64String($bytes);ownerHash=(Get-PresetBytesHash $bytes)}
    Write-ConfigAtomic $marker ($txn|ConvertTo-Json -Depth 4 -Compress)
    $script:PresetTransactionActive=$true
}
function Complete-PresetTransaction {
    $marker=Assert-PresetStateTarget (Join-Path (Get-PresetStateDir) 'transaction.json')
    Remove-Item -LiteralPath $marker -Force -ErrorAction Stop
    $script:PresetTransactionActive=$false
}
function Recover-PresetTransaction([switch]$Force) {
    if($script:PresetTransactionActive -and !$Force){return}
    $marker=Assert-PresetStateTarget (Join-Path (Get-PresetStateDir) 'transaction.json')
    if(!(Test-Path -LiteralPath $marker)){return}
    Assert-CssDesktopNotEditing
    if(!(Test-Path -LiteralPath $marker -PathType Leaf) -or (Get-Item -LiteralPath $marker).Length -gt 524288){throw 'Invalid unfinished preset transaction'}
    try {
        $txn=[IO.File]::ReadAllText($marker,[Text.Encoding]::UTF8)|ConvertFrom-Json
        if($txn.version -ne 1 -or $txn.themeRoot -ne [IO.Path]::GetFullPath($script:ThemeRoot) -or $txn.ownerExisted -isnot [bool] -or $txn.ownerBase64 -isnot [string] -or $txn.ownerHash -notmatch '^[A-Fa-f0-9]{64}$' -or $txn.backup -isnot [string]){throw 'Invalid transaction schema'}
        $bytes=[Convert]::FromBase64String($txn.ownerBase64)
        if($bytes.Length -gt 262144 -or (Get-PresetBytesHash $bytes) -ne $txn.ownerHash -or (!$txn.ownerExisted -and $bytes.Length)){throw 'Transaction owner integrity check failed'}
        $rollback=Read-PresetBackup $txn.backup
        if(!$txn.recoveryBackup){
            $preimage=New-PresetBackup @($rollback.journal.entries|ForEach-Object {Join-Path $script:ThemeRoot (Get-PresetRelative $_.relative)})
        }
        # Backup reads can outlast an editor opening. Recheck before changing
        # the journal or restoring any files, including forced rollback.
        Assert-CssDesktopNotEditing
        if(!$txn.recoveryBackup){
            $txn|Add-Member -NotePropertyName recoveryBackup -NotePropertyValue $preimage.directory -Force
            Write-ConfigAtomic $marker ($txn|ConvertTo-Json -Depth 4 -Compress)
        }
        Restore-PresetBackup $rollback
        $owner=Assert-PresetStateTarget (Join-Path (Get-PresetStateDir) 'owner.json')
        if($txn.ownerExisted){Write-PresetStateBytes $owner $bytes}elseif(Test-Path -LiteralPath $owner -PathType Leaf){Remove-Item -LiteralPath $owner -Force}
        Complete-PresetTransaction
    }catch{throw ('未完成的外观事务无法自动恢复；已停止新的写入并保留备份：'+$_.Exception.Message)}
}
function Invoke-PresetPrepare([bool]$Reset=$false,[string]$ExpectedRevision='',[switch]$OwnedResourcesOnly,[switch]$ProtectEditor) {
    Recover-PresetTransaction
    $pack=Read-PresetManifest;$rows=@(Get-ThemeRows)
    if($ExpectedRevision -and (Get-ThemeRevision $rows) -ne $ExpectedRevision){throw 'Theme settings changed; reread before applying preset'}
    $state=Read-PresetState;$first=!($state -and $state.owned)
    if($OwnedResourcesOnly){
        if($Reset -or $first -or !$ExpectedRevision){throw 'Owned resource update requires an existing owner and exact settings revision'}
        Assert-CssDesktopNotEditing
    }
    if(!$first){Read-PresetBackup ([string]$state.originalBackup)|Out-Null}
    $foreign=@(Get-ForeignThemeRows $(if(Test-BackendOwner){@(Invoke-LoaderRpc 'get_themes')}else{@()}))
    $deletes=@(Get-RetiredPresetTargets $foreign);$emptyRetired=@(Get-EmptyRetiredPresetDirectories $foreign)
    if($deletes.Count -or $emptyRetired.Count){Assert-CssDesktopNotEditing}
    $writes=New-Object Collections.Generic.List[object]
    foreach($file in $pack.files){
        $rel=Get-PresetRelative $file.path;$target=Assert-ThemeTarget (Join-Path $script:ThemeRoot $rel);$isConfig=[IO.Path]::GetFileName($rel) -eq 'config_USER.json';$row=$rows|Where-Object name -eq $rel.Split('\')[0]
        # A package update replaces owned assets only, never a switch/parameter file.
        if($OwnedResourcesOnly -and $isConfig){continue}
        if($isConfig -and !$Reset -and !$first -and (Read-PresetUserConfig $target)){continue}
        if((Test-Path -LiteralPath $target -PathType Leaf) -and (Get-ConfigHash $target -MaximumBytes 16777216) -eq $file.sha256){continue}
        # Validate the bytes to be committed, not the earlier read of this path.
        $source=[IO.File]::OpenRead((Join-Path (Join-Path $script:PackRoot 'themes') $rel))
        $reader=$null
        try {
            $length=$source.Length
            if($length -gt 16777216){throw "Bundled preset changed before staging: $rel"}
            $reader=[IO.BinaryReader]::new($source)
            $bytes=$reader.ReadBytes([int]$length)
            if($bytes.Length -ne $length){throw "Bundled preset changed before staging: $rel"}
        }finally{if($reader){$reader.Dispose()};$source.Dispose()}
        if((Get-PresetBytesHash $bytes) -ne $file.sha256){throw "Bundled preset changed before staging: $rel"}
        $writes.Add([pscustomobject]@{path=$target;bytes=$bytes})
    }
    foreach($item in $foreign|Where-Object {!$OwnedResourcesOnly -and !$_.retired -and $_.needsDisable}){
        if($item.duplicate -or $item.manifestInvalid){
            # Reversible inactive shim avoids two backend themes sharing a managed name.
            $shim=@{name=('_ymcc-paused-'+[guid]::NewGuid().ToString('N'));author='YMCC compatibility placeholder';manifest_version=1;inject=@{};patches=@{}}|ConvertTo-Json -Compress
            $writes.Add([pscustomobject]@{path=$item.manifestPath;bytes=[Text.Encoding]::UTF8.GetBytes($shim)})
        }
        $config=Read-PresetUserConfig $item.path;if(!$config){$config=[pscustomobject]@{active=$false}}
        $config.active=$false;$writes.Add([pscustomobject]@{path=$item.path;bytes=[Text.Encoding]::UTF8.GetBytes(($config|ConvertTo-Json -Depth 64 -Compress))})
    }
    if(!$first -and !$Reset -and $writes.Count -eq 0 -and $deletes.Count -eq 0 -and $emptyRetired.Count -eq 0 -and $state.packDigest -eq $pack.digest){return [pscustomobject]@{ok=$true;message='YMCC 预设已就绪';snapshot=(Get-AppearanceStatus)}}
    $backupPaths=@($writes|ForEach-Object path)+@($deletes)
    if($first){$backupPaths+=@($pack.files|ForEach-Object {Join-Path $script:ThemeRoot (Get-PresetRelative $_.path)})}
    $backup=New-PresetBackup $backupPaths
    if($OwnedResourcesOnly){Assert-CssDesktopNotEditing;if((Get-ThemeRevision @(Get-ThemeRows)) -ne $ExpectedRevision){throw 'Theme settings changed during owned resource update'}}
    $original=$backup.directory
    $mergeOriginal=!$first -and $state.originalBackup
    if($ProtectEditor){
        # Stage the backup before the last editor check. Deferral here must
        # not open a rollback transaction over a new edit.
        if($mergeOriginal){$original=Merge-PresetOriginalBackup ([string]$state.originalBackup) $backup}
        if(!$OwnedResourcesOnly -and $ExpectedRevision -and (Get-ThemeRevision @(Get-ThemeRows -RevisionOnly)) -cne $ExpectedRevision){
            throw 'Appearance changed during automatic preparation; no preset written'
        }
        Assert-CssDesktopNotEditing
    }
    Begin-PresetTransaction $backup
    try{
        if(!$ProtectEditor -and $mergeOriginal){$original=Merge-PresetOriginalBackup ([string]$state.originalBackup) $backup}
        foreach($write in $writes){Write-PresetBytes $write.path $write.bytes}
        foreach($target in $deletes){Assert-CssDesktopNotEditing;Remove-Item -LiteralPath (Assert-ThemeTarget $target) -Force -ErrorAction Stop}
        # Nonrecursive OS operation fails if a concurrent writer adds any child.
        foreach($directory in @(Get-EmptyRetiredPresetDirectories $foreign)){
            Assert-CssDesktopNotEditing
            $emptyTarget=Assert-ThemeTarget $directory
            [IO.Directory]::Delete($emptyTarget,$false)
        }
        Save-PresetState ([pscustomobject]@{version=1;themeRoot=[IO.Path]::GetFullPath($script:ThemeRoot);owned=$true;packDigest=$pack.digest;originalBackup=$original;latestBackup=$backup.directory;pendingReload=$true})
        Complete-PresetTransaction
    }catch{$problem=$_.Exception.Message;try{Recover-PresetTransaction -Force}catch{throw ($problem+'; '+$_.Exception.Message)};throw $problem}
    [pscustomobject]@{ok=$true;saved=$true;message=$(if($Reset){'已恢复 YMCC 默认预设；需要重新加载美化后端'}elseif($OwnedResourcesOnly){'已更新 YMCC 主题资源，当前开关与个人参数保持不变'}else{'已备份原外观并套用 YMCC 自带预设'});snapshot=(Get-AppearanceStatus)}
}
# Called only by the existing Ensure operation, including its authorized
# single-helper Steam mode entry. Status/frontend mount stay read-only.
function Invoke-OwnedPresetResourceUpdate($Current) {
    # Managed resource bytes remain authoritative even when a store update keeps the same pack digest.
    if(!$Current.packAvailable -or !($Current.packUpdateAvailable -or $Current.packRepairRequired) -or !$Current.packOwned -or !$Current.canRestore -or $Current.foreignActiveCount -gt 0){return $Current}
    # Desktop editing owns the files too; a package upgrade is not an edit takeover.
    if(Test-CssDesktopEditing){return $Current}
    $result=Invoke-PresetPrepare $false $Current.revision -OwnedResourcesOnly
    if(!$result.ok -or !$result.snapshot){throw 'Owned theme resources were not updated'}
    return $result.snapshot
}
function Invoke-PresetRestore {
    Recover-PresetTransaction
    $state=Read-PresetState;if(!$state -or !$state.owned -or !$state.originalBackup){throw '没有可恢复的接管前外观备份'}
    $backup=Read-PresetBackup ([string]$state.originalBackup);$undo=New-PresetBackup @($backup.journal.entries|ForEach-Object {Join-Path $script:ThemeRoot (Get-PresetRelative $_.relative)})
    Begin-PresetTransaction $undo
    try{Restore-PresetBackup $backup -KeepRetiredRemoved;Save-PresetState ([pscustomobject]@{version=1;themeRoot=[IO.Path]::GetFullPath($script:ThemeRoot);owned=$false;packDigest='';originalBackup=$state.originalBackup;latestBackup=$undo.directory;pendingReload=$true});Complete-PresetTransaction}catch{$problem=$_.Exception.Message;try{Recover-PresetTransaction -Force}catch{throw ($problem+'; '+$_.Exception.Message)};throw $problem}
    [pscustomobject]@{ok=$true;saved=$true;message='已恢复接管前的外观配置；需要重新加载美化后端';snapshot=(Get-AppearanceStatus)}
}
function Complete-PresetReload {
    $state=Read-PresetState;if($state -and $state.pendingReload){$state.pendingReload=$false;Save-PresetState $state}
}
function Test-CefStyleCleanupAck($Response) {
    if($Response.id -ne 1 -or $Response.error -or $Response.result.exceptionDetails -or $Response.result.result.type -ne 'number' -or $Response.result.result.value -lt 0){throw 'Steam CEF cleanup was not acknowledged'}
}
function Invoke-CefStyleCleanup($Tab) {
    $socket=New-Object Net.WebSockets.ClientWebSocket;$cancel=New-Object Threading.CancellationTokenSource;$stream=New-Object IO.MemoryStream
    try{
        $cancel.CancelAfter(1000)
        $socket.ConnectAsync([uri]$Tab.webSocketDebuggerUrl,$cancel.Token).GetAwaiter().GetResult()|Out-Null
        $message=@{id=1;method='Runtime.evaluate';params=@{expression="(()=>{const nodes=document.querySelectorAll('style.css-loader-style');const n=nodes.length;nodes.forEach(x=>x.remove());document.getElementById('test_css_loaded')?.remove();return n;})()";returnByValue=$true}}|ConvertTo-Json -Depth 6 -Compress
        $bytes=[Text.Encoding]::UTF8.GetBytes($message);$segment=New-Object 'System.ArraySegment[byte]' -ArgumentList (,$bytes)
        $socket.SendAsync($segment,[Net.WebSockets.WebSocketMessageType]::Text,$true,$cancel.Token).GetAwaiter().GetResult()|Out-Null
        $buffer=New-Object byte[] 16384;$segment=New-Object 'System.ArraySegment[byte]' -ArgumentList (,$buffer)
        for($messages=0;$messages -lt 16;$messages++){
            $stream.SetLength(0)
            do{
                $received=$socket.ReceiveAsync($segment,$cancel.Token).GetAwaiter().GetResult()
                if($received.MessageType -ne [Net.WebSockets.WebSocketMessageType]::Text -or $stream.Length+$received.Count -gt 65536){throw 'Invalid/oversized CEF cleanup response'}
                $stream.Write($buffer,0,$received.Count)
            }while(!$received.EndOfMessage)
            $response=[Text.Encoding]::UTF8.GetString($stream.ToArray())|ConvertFrom-Json
            if($response.id -ne 1){continue} # Unrelated CDP events are not acknowledgements.
            Test-CefStyleCleanupAck $response
            return $true
        }
        throw 'No matching CEF cleanup response'
    }finally{$socket.Abort();$socket.Dispose();$cancel.Dispose();$stream.Dispose()}
}
function Clear-OldLoaderStyles {
    $required=$null -ne (Get-AppearanceSteamSession)
    $summary=[pscustomobject]@{required=$required;mainConfirmed=$false;attempted=0;cleaned=0;failed=0}
    $listeners=@(Get-NetTCPConnection -State Listen -LocalPort 8080 -ErrorAction SilentlyContinue);$verified=$false
    foreach($listener in $listeners){if($listener.LocalAddress -eq '127.0.0.1'){try{$owner=Get-Process -Id $listener.OwningProcess -ErrorAction Stop;if(Test-CurrentUserProcess $owner){$image=Get-AppearanceProcessPath $owner;if([IO.Path]::GetFileName($image) -ieq 'steamwebhelper.exe'){$verified=$true}}}catch{}}}
    if(!$verified){return $summary}
    try{$tabs=Invoke-RestMethod -Uri 'http://127.0.0.1:8080/json/list' -TimeoutSec 2}catch{return $summary}
    $pages=@($tabs|Where-Object {$_.type -eq 'page' -and $_.title -and $_.url})
    $mainPages=@($pages|Where-Object {$_.title -match '大屏|big.?picture|gamepad.?ui' -or $_.url -match 'browserType=3'})
    if(!$mainPages.Count){$summary.required=$false}
    $confirmedMain=0;$deadline=[DateTime]::UtcNow.AddSeconds(5)
    foreach($tab in $pages|Sort-Object @{Expression={if($_.title -match '大屏|big.?picture|gamepad.?ui' -or $_.url -match 'browserType=3'){0}else{1}}}){
        if([DateTime]::UtcNow -gt $deadline){break}
        if($tab.webSocketDebuggerUrl -notmatch '^ws://127\.0\.0\.1:8080/devtools/page/[A-Za-z0-9_-]+$'){continue}
        $summary.attempted++
        try{if(Invoke-CefStyleCleanup $tab){$summary.cleaned++;if($tab -in $mainPages){$confirmedMain++}}}catch{$summary.failed++}
    }
    $summary.mainConfirmed=$mainPages.Count -gt 0 -and $confirmedMain -eq $mainPages.Count
    return $summary
}
